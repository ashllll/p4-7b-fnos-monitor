#!/usr/bin/env python3
"""Exercise the manifest gate against supplied synthetic packages, never the default tree."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
CHECKER = REPO / "nas/fpk/manifest_check.py"
spec = importlib.util.spec_from_file_location("manifest_check", CHECKER)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ManifestGateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="manifest-gate-")
        self.addCleanup(self.temp.cleanup)
        self.pkg = Path(self.temp.name)
        for directory in ("app/ui", "app/server", "wizard"):
            (self.pkg / directory).mkdir(parents=True)
        self.manifest = {
            "appname": "synthetic", "version": "1.0.0", "display_name": "Synthetic",
            "desc": "Synthetic gate fixture", "source": "thirdparty", "platform": "all",
            "maintainer": "Synthetic", "maintainer_url": "https://invalid.test",
            "desktop_uidir": "ui", "desktop_applaunchname": "synthetic.Application",
            "service_port": "12345",
        }
        self.config = {".url": {"synthetic.Application": {
            "title": "Synthetic", "gatewayPrefix": "/app/synthetic", "url": "/app/synthetic",
            "gatewaySocket": "app.sock", "icon": "synthetic.png",
        }}}
        (self.pkg / "app/server/nas_companion_server.py").write_text(
            'APPNAME = "synthetic"\nGATEWAY_PREFIX = "/app/" + APPNAME\n'
            'SOCKET_NAME = "app.sock"\nDEFAULT_CONFIG = {"port": 12345}\n')
        (self.pkg / "wizard/install").write_text(
            '{"field": "wizard_port", "initValue": "12345"}')

    def run_gate(self):
        (self.pkg / "manifest").write_text("".join(
            f"{k} = {v}\n" for k, v in self.manifest.items()))
        (self.pkg / "app/ui/config").write_text(json.dumps(self.config))
        result = subprocess.run([sys.executable, str(CHECKER), str(self.pkg)],
                                text=True, capture_output=True)
        self.assertNotIn("Traceback", result.stderr)
        return result

    def assert_rejected(self, diagnostic):
        result = self.run_gate()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(diagnostic, result.stdout)
        self.assertNotIn("manifest 取值合法", result.stdout)

    def test_valid_alternate_package(self):
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("appname=synthetic", result.stdout)

    def test_supplied_package_wiring_mismatch(self):
        self.manifest["appname"] = "otherpkg"
        self.manifest["service_port"] = "9999"
        self.assert_rejected("服务端的 APPNAME=synthetic")

    def test_required_fields_cannot_be_empty(self):
        for key in sorted(checker.REQUIRED):
            with self.subTest(key=key):
                old = self.manifest[key]
                self.manifest[key] = " "
                self.assert_rejected(f"必要字段 {key} 不能为空")
                self.manifest[key] = old

    def test_config_top_level_must_be_object(self):
        for config in ([], None, "text", 1):
            with self.subTest(config=config):
                self.config = config
                self.assert_rejected("顶层必须是 JSON 对象")

    def test_config_url_group_and_entry_must_be_objects(self):
        for config in ({".url": []}, {".url": {"synthetic.Application": []}}):
            with self.subTest(config=config):
                self.config = config
                self.assert_rejected("必须是 JSON 对象")

    def test_default_package_still_valid(self):
        result = subprocess.run([sys.executable, str(CHECKER)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
