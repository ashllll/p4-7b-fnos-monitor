#!/usr/bin/env python3
"""Offline failure injection for validation gates; never contacts a NAS or hardware."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

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
                                ('app_main lvgl_worker poll_task pair_task', 0)]:
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

    def test_page_visible_cpu_and_revoke(self):
        state = dict(ver='fixture-version', uid=1000,
            config=dict(port=8798, interval=3, auth_mode='pairing', bind='127.0.0.1',
                        hist_len=600, docker_enabled=False, tls_enabled=True),
            tls=dict(configured=True, enabled=True, fingerprint='AA:BB:CC:DD', subject='fixture'),
            devices=[], listen=dict(tcp='fixture:8798', socket='/fixture/app.sock'),
            collector=dict(ready=True, age_s=1, seq=3),
            paths=dict(appdest='/fixture/app', pkgetc='/fixture/etc', pkgvar='/fixture/var'))
        snapshot = dict(cpu=dict(pct=2.2, cores=4), mem=dict(total_mb=8192),
            net={'if': 'fixture0'}, host='fixture-host', modules={'cpu': dict(status='ok')})
        responses = {'GET /api/state': state, 'GET /api/status-now': snapshot,
            'GET /api/probe': dict(uid=1000, sources=[dict(path='/fixture/source', status='ok')],
                                  modules={'cpu': dict(status='ok')}),
            'GET /api/diagnostics': dict(checks=[dict(name='fixture-check', status='ok', detail='fixture-detail')]),
            'GET /api/logs': dict(lines=['fixture-log']),
            'POST /api/pairing/new': dict(code='123456', expires=3600),
            'POST /api/pairing/cancel': dict(ok=True), 'POST /api/config': dict(ok=True, changed=[]),
            'POST /api/collector/docker': dict(ok=True, docker_enabled=False),
            'POST /api/devices/revoke': dict(removed=0)}
        fixture = self.root / 'responses.json'
        fixture.write_text(json.dumps(responses))
        html = (REPO / 'nas/fpk/nasscreencompanion/app/ui/www/index.html').read_text()
        target = self.root / 'index.html'
        args = ('node', str(REPO / 'nas/fpk/page_check.js'), str(target), str(fixture))
        target.write_text(html)
        baseline = self.run_cmd(*args)
        self.assertEqual(baseline.returncode, 0, baseline.stdout)
        for broken, message in (
                (html.replace("num(cpu.pct,1)+'<span", "'0.0'+'<span", 1), 'CPU 占用'),
                (html.replace('{id: b.dataset.revoke}', "{id: 'wrong'}", 1), '设备 ID')):
            self.assertNotEqual(broken, html)
            target.write_text(broken)
            result = self.run_cmd(*args)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(message, result.stdout)

    def report_harness(self):
        script = self.copy('tools/test_report.sh')
        (self.root / 'docs').mkdir()
        (self.root / 'nas/fpk/nasscreencompanion/cmd').mkdir(parents=True)
        # All unrelated checks succeed; the gate under test has observable injected output.
        self.script('bin/python3', 'exit 0\n')
        self.script('tools/preview/run.sh', 'exit 0\n')
        self.script('tools/parsecheck/run.sh', 'exit 0\n')
        self.script('tools/certcheck/run.sh', 'exit 0\n')
        self.script('tools/release_check.sh', 'exit 0\n')
        home = self.root / 'home'
        self.script('home/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-objdump', 'exit 0\n')
        env = dict(os.environ, HOME=str(home), PATH=str(self.root / 'bin') + ':' + os.environ['PATH'])
        return script, env

    def test_report_lifecycle_and_release_failure(self):
        script, env = self.report_harness()
        suite = self.script('nas/fpk/test_lifecycle.sh', "printf '  \\033[32m✓ test\\n结果：1 项通过，0 项失败\\n'\n")
        result = self.run_cmd('bash', str(script), env=env)
        self.assertEqual(result.returncode, 0, result.stdout)
        for body in ("echo '结果：0 项通过，1 项失败'; exit 0\n",
                     "echo '结果：0 项通过，0 项失败'; exit 4\n", 'exit 0\n'):
            suite.write_text('#!/bin/bash\n' + body)
            self.assertNotEqual(self.run_cmd('bash', str(script), env=env).returncode, 0)
        suite.write_text("#!/bin/bash\nprintf '  \\033[32m✓ test\\n结果：1 项通过，0 项失败\\n'\n")
        self.script('tools/release_check.sh', 'exit 5\n')
        self.assertNotEqual(self.run_cmd('bash', str(script), '--full', env=env).returncode, 0)
        obj = self.root / 'home/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-objdump'
        obj.unlink()
        self.assertNotEqual(self.run_cmd('bash', str(script), '--quick', env=env).returncode, 0)
        report = (self.root / 'docs/fnos-companion-test-report.md').read_text()
        self.assertIn('未运行**（缺少工具链）', report)
        self.assertIn('本次没有重跑', report)
        self.assertNotIn('通过（已并入套件', report)

    def png(self, name, track=True, content=True, band=False, overlap=False):
        w, h = 200, 130
        data = bytearray()
        for y in range(h):
            data.append(0)
            for x in range(w):
                color = (22, 30, 43)
                if band and 60 <= y < 90:
                    color = (110, 65, 20)
                if track and 180 <= x <= 183 and 45 <= y < 115:
                    color = (95, 101, 115)
                if content and (170 if overlap else 140) <= x <= (178 if overlap else 160) and 70 <= y < 80:
                    color = (230, 230, 230)
                data.extend(color)
        def chunk(kind, payload):
            return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
        (self.root / 'images' / name).write_bytes(b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(data)) + chunk(b'IEND', b''))

    def test_scroll_gap_coverage_and_background(self):
        script = self.copy('tools/preview/scroll_gap.js')
        self.script('tools/preview/build/preview', "echo '[card] p=1 x=0 y=0 w=200 h=130 list=1'\n")
        (self.root / 'images').mkdir()
        args = ('node', str(script), str(self.root / 'images'))
        self.assertEqual(self.run_cmd(*args).returncode, 2)
        for track, content, band, overlap, rc in (
                (False, True, False, False, 2), (True, False, False, False, 1),
                (True, True, False, False, 0), (True, True, True, False, 0),
                (True, True, True, True, 1)):
            self.png('fixture-p1.png', track, content, band, overlap)
            result = self.run_cmd(*args)
            self.assertEqual(result.returncode, rc, result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
