#!/usr/bin/env python3
"""真实升级回调控制流，临时匿名配置与服务/故障桩；不操作 NAS。"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

CMD = Path(__file__).resolve().parent / 'nasscreencompanion/cmd'

class UpgradeSafety(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cmd = self.root / 'cmd'
        self.cmd.mkdir()
        self.data = self.root / 'var'
        self.data.mkdir()
        for name in ('_common.sh', 'upgrade_init', 'upgrade_callback'):
            shutil.copyfile(CMD / name, self.cmd / name)
        with (self.cmd / '_common.sh').open('a') as out:
            out.write('''
prepare_package_data() { return 0; }
as_package() {
  [ "${TEST_FAIL:-}" != "$1" ] || return 1
  "$@"
}
resolve_python() { printf '%s' "$TEST_PYTHON"; }
is_running() { [ -f "$PKG_VAR/running" ]; }
service_stop() {
  echo stop >> "$PKG_VAR/events"
  [ -f "$PKG_VAR/config.json.pre-fixture" ] || return 1
  [ "${TEST_FAIL:-}" != stop ] || return 1
  rm -f "$PKG_VAR/running"
}
service_start() {
  echo start >> "$PKG_VAR/events"
  [ "${TEST_FAIL:-}" != start ] || return 1
  touch "$PKG_VAR/running"
}
''')
        self.config = self.data / 'config.json'
        self.config.write_text(json.dumps({'devices': [{'name': 'anonymous'}], 'interval': 2}))
        self.backup = self.data / 'config.json.pre-fixture'
        self.flag = self.data / 'upgrade.was_running'
        self.running = self.data / 'running'
        self.running.touch()
        self.env = {k:v for k,v in os.environ.items() if not k.startswith(('TRIM_', 'TEST_', 'wizard_', 'NSC_'))}
        self.env.update(TRIM_APPDEST=str(self.root), TRIM_PKGVAR=str(self.data),
                        TRIM_PKGETC=str(self.root/'etc'), TRIM_OLD_APPVER='fixture', TEST_PYTHON=sys.executable)

    def run_script(self, name, fault=''):
        result = subprocess.run(['bash', str(self.cmd/name)], env={**self.env, 'TEST_FAIL':fault}, capture_output=True, text=True, timeout=10)
        return result.returncode

    def test_backup_before_stop_and_repeated_upgrade(self):
        original = self.config.read_bytes()
        self.assertEqual(self.run_script('upgrade_init'), 0)
        self.assertEqual(self.backup.read_bytes(), original)
        self.assertTrue(self.flag.exists())
        self.assertFalse(self.running.exists())
        self.config.write_text('{"devices":[],"interval":3}')
        self.assertEqual(self.run_script('upgrade_init'), 0)
        self.assertEqual(self.backup.read_bytes(), original)
        self.assertTrue(self.flag.exists())
        self.assertEqual(self.run_script('upgrade_callback'), 0)
        self.assertTrue(self.running.exists())
        self.assertFalse(self.flag.exists())

    def test_backup_failure_does_not_stop(self):
        self.backup.mkdir()
        self.assertNotEqual(self.run_script('upgrade_init'), 0)
        self.assertTrue(self.running.exists())
        self.assertFalse((self.data/'events').exists())

    def test_flag_write_failure_does_not_stop(self):
        self.assertNotEqual(self.run_script('upgrade_init', 'touch'), 0)
        self.assertTrue(self.running.exists())
        self.assertFalse((self.data/'events').exists())

    def test_stop_failure_keeps_restore_intent(self):
        self.assertNotEqual(self.run_script('upgrade_init', 'stop'), 0)
        self.assertTrue(self.running.exists())
        self.assertTrue(self.flag.exists())
        self.assertTrue(self.backup.exists())

    def test_failed_restart_and_marker_cleanup_can_retry(self):
        self.assertEqual(self.run_script('upgrade_init'), 0)
        self.assertNotEqual(self.run_script('upgrade_callback', 'start'), 0)
        self.assertTrue(self.flag.exists())
        self.assertNotEqual(self.run_script('upgrade_callback', 'rm'), 0)
        self.assertTrue(self.flag.exists())
        self.assertTrue(self.running.exists())
        self.assertEqual(self.run_script('upgrade_callback'), 0)
        self.assertFalse(self.flag.exists())

    def test_stopped_service_stays_stopped(self):
        self.running.unlink()
        self.assertEqual(self.run_script('upgrade_init'), 0)
        self.assertEqual(self.run_script('upgrade_callback'), 0)
        self.assertFalse(self.running.exists())

    def test_symlink_checkpoint_rejected(self):
        untouched = self.root/'unrelated'
        untouched.write_text('keep')
        self.backup.symlink_to(untouched)
        self.assertNotEqual(self.run_script('upgrade_init'), 0)
        self.assertEqual(untouched.read_text(), 'keep')
        self.assertTrue(self.running.exists())

    def test_directory_failure(self):
        self.assertNotEqual(self.run_script('upgrade_init', 'mkdir'), 0)
        self.assertTrue(self.running.exists())
        self.assertNotEqual(self.run_script('upgrade_callback', 'mkdir'), 0)

    def test_invalid_marker_and_source_leave_unrelated_files(self):
        untouched = self.root/'unrelated'
        untouched.write_text('keep')
        original = self.config.read_bytes()
        self.flag.symlink_to(untouched)
        for name in ('upgrade_init', 'upgrade_callback'):
            self.assertNotEqual(self.run_script(name), 0)
        self.assertEqual(self.config.read_bytes(), original)
        self.flag.unlink()
        self.config.unlink()
        self.config.symlink_to(untouched)
        self.assertNotEqual(self.run_script('upgrade_init'), 0)
        self.assertEqual(untouched.read_text(), 'keep')
        self.assertTrue(self.running.exists())

    def test_version_path_escape_rejected(self):
        self.env['TRIM_OLD_APPVER'] = '../../unrelated'
        self.assertNotEqual(self.run_script('upgrade_init'), 0)
        self.assertFalse((self.data/'events').exists())
        self.assertTrue(self.running.exists())

if __name__ == '__main__':
    unittest.main()
