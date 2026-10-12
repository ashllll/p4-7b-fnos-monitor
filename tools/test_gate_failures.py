#!/usr/bin/env python3
"""Offline release and tool failure regressions; no NAS, hardware or real build."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]


class Gates(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def copy(self, name):
        dst = self.root / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / name, dst)
        return dst

    def script(self, name, body):
        dst = self.root / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text('#!/bin/bash\n' + body)
        dst.chmod(0o755)
        return dst

    def run_cmd(self, *args, env=None):
        return subprocess.run(args, cwd=self.root, env=env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, errors="replace", timeout=30)

    def test_stack(self):
        script = self.copy('tools/stack_check.py')
        elf = self.root / 'test.elf'
        elf.touch()
        obj = self.script('objdump', 'exit 1\n')
        args = (sys.executable, str(script), '--elf', str(elf), '--objdump', str(obj))
        self.assertEqual(self.run_cmd(*args).returncode, 2)
        obj.write_text('#!/bin/bash\nexit 0\n')
        self.assertEqual(self.run_cmd(*args).returncode, 2)
        for roots, expected in [('app_main', 2),
                                ('app_main lvgl_worker poll_task pair_task probe_task cli_task ui_retry_task', 0)]:
            obj.write_text('#!/bin/bash\n' + ''.join(
                f"echo '00000000 <{r}>:'\necho '  0: addi sp,sp,-16'\n" for r in roots.split()))
            result = self.run_cmd(*args)
            self.assertEqual(result.returncode, expected, result.stdout)
            if expected:
                self.assertNotIn('全部入口在预算内', result.stdout)
        obj.unlink()
        self.assertEqual(self.run_cmd(*args).returncode, 2)

    def release(self, body='mkdir -p build-release; echo generic > build-release/fnos_monitor.bin\n'):
        script = self.copy('tools/release_check.sh')
        self.script('idf.sh', body)
        cfg = self.root / 'components/fnos_monitor/fnos_config.h'
        cfg.parent.mkdir(parents=True, exist_ok=True)
        cfg.write_text(''.join(f'#define {key} "fixture-{key}"\n' for key in
                               ('APP_WIFI_SSID', 'APP_WIFI_PASS', 'FNOS_TOKEN', 'FNOS_HOST')))
        return script, cfg

    def test_release_success_and_build_failure_restore(self):
        script, cfg = self.release()
        original = cfg.read_bytes()
        result = self.run_cmd('bash', str(script))
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(cfg.read_bytes(), original)
        self.script('idf.sh', 'exit 5\n')
        self.assertNotEqual(self.run_cmd('bash', str(script)).returncode, 0)
        self.assertEqual(cfg.read_bytes(), original)

    def test_release_template_and_real_secret(self):
        script, cfg = self.release()
        example = cfg.with_name('fnos_config.example.h')
        example.write_bytes(cfg.read_bytes())
        self.script('idf.sh', "mkdir -p build-release; cat components/fnos_monitor/fnos_config.example.h > build-release/fnos_monitor.bin\n")
        result = self.run_cmd('bash', str(script))
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn('未作真实值对照', result.stdout)
        example.write_text('')
        self.script('idf.sh', "mkdir -p build-release; cat components/fnos_monitor/fnos_config.h.release-check > build-release/fnos_monitor.bin\n")
        result = self.run_cmd('bash', str(script))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn('命中', result.stdout)

    def test_release_stash_collision_and_move_failure(self):
        script, cfg = self.release('touch BUILD_WAS_RUN\n')
        stash = Path(str(cfg) + '.release-check')
        stash.write_text('existing backup')
        self.assertNotEqual(self.run_cmd('bash', str(script)).returncode, 0)
        self.assertEqual(stash.read_text(), 'existing backup')
        self.assertFalse((self.root / 'BUILD_WAS_RUN').exists())
        stash.unlink()
        self.script('bin/mv', 'exit 7\n')
        env = dict(os.environ, PATH=str(self.root / 'bin') + ':' + os.environ['PATH'])
        self.assertNotEqual(self.run_cmd('bash', str(script), env=env).returncode, 0)
        self.assertTrue(cfg.exists())
        self.assertFalse((self.root / 'BUILD_WAS_RUN').exists())

    def test_release_restore_failure(self):
        script, cfg = self.release()
        real_mv = shutil.which('mv')
        self.script('bin/mv', f'case "$1" in *.release-check) exit 9;; esac\nexec {real_mv} "$@"\n')
        env = dict(os.environ, PATH=str(self.root / 'bin') + ':' + os.environ['PATH'])
        result = self.run_cmd('bash', str(script), env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(Path(str(cfg) + '.release-check').exists())
        self.assertIn('恢复失败', result.stdout)

    def test_certificate_tool_failure(self):
        script = self.copy('tools/certcheck/run.sh')
        self.script('bin/placeholder', 'exit 0\n')
        for command in ('bash', 'dirname'):
            (self.root / 'bin' / command).symlink_to(shutil.which(command))
        env = dict(os.environ, PATH=str(self.root / 'bin'))
        result = self.run_cmd('bash', str(script), env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('三方一致', result.stdout)

    def test_memory_insufficient(self):
        for count in (0, 500, 2999):
            result = self.run_cmd(sys.executable, str(REPO / 'nas/fpk/mem_check.py'), str(count))
            self.assertEqual(result.returncode, 3, result.stdout)
            self.assertIn('未判定', result.stdout)

    def test_tls_disabled(self):
        # Replace req in the imported simulator; no sockets or real server needed.
        source = REPO / 'nas/fpk/board_sim.py'
        code = f'''import importlib.util,json,sys
s=importlib.util.spec_from_file_location('sim',{str(source)!r});m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
m.req=lambda *a,**kw:(200,json.dumps({{'tls':False,'cert_pem':'fixture'}}).encode())
sys.argv=['sim','--host','fixture','--port','1','--pair-code','123456']
sys.exit(m.main())'''
        result = self.run_cmd(sys.executable, '-c', code)
        self.assertEqual(result.returncode, 3, result.stdout)
        self.assertNotIn('都通了', result.stdout)

    def test_page_async_failure(self):
        # Exercise the actual main IIFE after an await, without reproducing its DOM fixture.
        script = self.copy('nas/fpk/page_check.js')
        source = script.read_text().replace('(async () => {',
            "(async () => { await Promise.resolve(); throw new Error('injected async failure');", 1)
        script.write_text(source)
        (self.root / 'page.html').write_text('<script></script>')
        (self.root / 'responses.json').write_text('{}')
        result = self.run_cmd('node', str(script), 'page.html', 'responses.json')
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn('injected async failure', result.stdout)

    def test_scroll_gap_selftest(self):
        result = self.run_cmd('node', str(REPO / 'tools/preview/scroll_gap.js'), '--self-test')
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
