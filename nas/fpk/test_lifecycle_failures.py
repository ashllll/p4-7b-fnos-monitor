#!/usr/bin/env python3
"""匿名临时包：PID 写失败与收尾失败不能报成功；只控制本次测试子进程。"""
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import unittest

CMD = Path(__file__).resolve().parent/'nasscreencompanion/cmd'

class LifecycleFailures(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cmd = self.root/'cmd'
        self.cmd.mkdir()
        (self.root/'var').mkdir()
        (self.root/'server').mkdir()
        for name in ('_common.sh', 'main', 'uninstall_callback', 'install_callback'):
            shutil.copyfile(CMD/name, self.cmd/name)
        (self.root/'server/nas_companion_server.py').write_text('import time; time.sleep(60)')
        with (self.cmd/'_common.sh').open('a') as out:
            out.write(r"""
prepare_package_data() { return 0; }
resolve_python() { printf '%s' "$TEST_PYTHON"; }
port_busy() { return 1; }
http_health() { [ "${TEST_FAULT:-}" != unhealthy ]; }
is_running() { [ "${TEST_RUNNING:-0}" = 1 ]; }
as_package() {
  if [ "$1" = mkdir ] && [ "${TEST_FAULT:-}" = mkdir ]; then return 1; fi
  if [ "$1" = /bin/bash ] && [ "${TEST_FAULT:-}" = pidwrite ]; then
    printf '%s' "$5" > "$PKG_VAR/test-child"
    return 1
  fi
  if [ "$1" = rm ]; then
    case "${TEST_FAULT:-}:$3" in
      pidremove:*/app.pid|flagremove:*/upgrade.was_running|socketremove:*/app.sock) return 1 ;;
    esac
  fi
  "$@"
}
if [ "${TEST_FAULT:-}" = socket ]; then cleanup_socket() { return 1; }; fi
""")
        self.env = {k:v for k,v in os.environ.items() if not k.startswith(('TRIM_', 'TEST_', 'wizard_'))}
        self.env.update(TRIM_APPDEST=str(self.root), TRIM_PKGVAR=str(self.root/'var'),
                        TEST_PYTHON=sys.executable)
        self.addCleanup(self.kill_test_child)

    def kill_test_child(self):
        path = self.root/'var/test-child'
        if path.exists():
            pid = int(path.read_text())
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def run_script(self, name, fault, *args, running=False):
        return subprocess.run(['bash', str(self.cmd/name), *args], env={**self.env,'TEST_FAULT':fault,'TEST_RUNNING':str(int(running))}, capture_output=True, text=True, timeout=15)

    def test_install_directory_failure_with_existing_config(self):
        (self.root/'var/config.json').write_text('{}')
        r = self.run_script('install_callback', 'mkdir')
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('无法创建应用数据目录', r.stderr)
        self.assertNotIn('安装完成', r.stdout)
        # 恢复漏检旧实现必须暴露假成功，夹具不能被已有配置分支绕开。
        path = self.cmd/'install_callback'
        src = path.read_text()
        path.write_text(src.replace(' || {\n  fail "无法创建应用数据目录"\n  exit 1\n}', ''))
        old = self.run_script('install_callback', 'mkdir')
        self.assertEqual(old.returncode, 0, old.stderr)
        self.assertIn('安装完成', old.stdout)

    def test_failed_pid_write_stops_new_child(self):
        r = self.run_script('main', 'pidwrite', 'start')
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn('无法保存 PID 记录', r.stderr)
        self.assertNotIn('启动成功', r.stdout)
        pid = int((self.root/'var/test-child').read_text())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)
        (self.root/'var/test-child').unlink()

    def test_uninstall_delete_failures_are_not_success(self):
        for fault, filename in [('pidremove','app.pid'), ('flagremove','upgrade.was_running')]:
            path = self.root/'var'/filename
            path.write_text('fixture')
            r = self.run_script('uninstall_callback', fault)
            self.assertNotEqual(r.returncode, 0)
            self.assertNotIn('已释放端口', r.stdout)
            self.assertTrue(path.exists())
            path.unlink()

    def test_socket_cleanup_failure_propagates(self):
        for name, args in [('uninstall_callback',()), ('main',('stop',)), ('main',('status',)), ('main',('start',))]:
            r = self.run_script(name, 'socket', *args)
            self.assertNotEqual(r.returncode, 0)
            self.assertNotIn('启动成功', r.stdout)
            self.assertNotIn('已释放端口', r.stdout)

    def test_early_directory_or_pid_remove_failure_does_not_launch(self):
        for fault in ('mkdir','pidremove'):
            r = self.run_script('main', fault, 'start')
            self.assertNotEqual(r.returncode, 0)
            self.assertFalse((self.root/'var/test-child').exists())
        r = self.run_script('main', 'pidremove', 'stop')
        self.assertNotEqual(r.returncode, 0)
        self.assertNotIn('已停止', r.stdout)

    def test_normal_uninstall_and_stopped_state(self):
        for name, args in [('uninstall_callback',()), ('main',('stop',))]:
            r = self.run_script(name, '', *args)
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_existing_unhealthy_instance_cannot_report_started(self):
        pid_file = self.root/'var/app.pid'
        pid_file.write_text('123456')  # is_running 桩，不操作这个 PID
        r = self.run_script('main', 'unhealthy', 'start', running=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('服务未就绪', r.stderr)
        self.assertEqual(pid_file.read_text(), '123456')
        r = self.run_script('main', '', 'start', running=True)
        self.assertEqual(r.returncode, 0)
        self.assertIn('已在运行', r.stdout)

    def test_real_socket_helper_propagates_remove_failure(self):
        path = self.root/'app.sock'
        with socket.socket(socket.AF_UNIX) as listener:
            listener.bind(str(path))
            for name, args in [('uninstall_callback',()), ('main',('stop',)), ('main',('status',)), ('main',('start',))]:
                r = self.run_script(name, 'socketremove', *args)
                self.assertNotEqual(r.returncode, 0)
                self.assertTrue(path.exists())
                self.assertNotIn('已释放端口', r.stdout)


    def test_service_exit_receipt_reader(self):
        import json
        source=(CMD/'main').read_text();a=source.index('service_exit_result() {');b=source.index('\n}\n',a)+3
        command='as_package() { "$@"; };\n'+source[a:b]+'\nservice_exit_result "$TEST_PYTHON" 123\n'
        env=dict(os.environ,PKG_VAR=str(self.root/'var'),TEST_PYTHON=sys.executable)
        receipt=self.root/'var/service-exit.json'
        for value,expected in (({'pid':123,'code':0},0),({'pid':123,'code':1},1),({'pid':124,'code':0},2),({'pid':123,'code':True},2)):
            receipt.write_text(json.dumps(value))
            r=subprocess.run(['bash','-c',command],env=env,capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,expected,r.stderr)
        for raw in ('','x'*4097):
            receipt.write_text(raw)
            self.assertEqual(subprocess.run(['bash','-c',command],env=env,capture_output=True,timeout=5).returncode,2)
        receipt.unlink()
        self.assertEqual(subprocess.run(['bash','-c',command],env=env,capture_output=True,timeout=5).returncode,2)
        stop=source[source.index('stop_process() {'):]
        self.assertIn('if ! service_exit_result "$exit_py" "$pid";',stop)

if __name__ == '__main__':
    unittest.main()
