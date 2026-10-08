#!/usr/bin/env python3
"""Focused configuration-persistence tests using anonymous temporary data.

If integrated as nas/fpk/config_persistence_check.py:
  python3 nas/fpk/config_persistence_check.py
For a scratch copy:
  python3 /tmp/fnos-config-persistence-check.py --package <package-root>

The actual lifecycle sources run in a temporary staged package. Only identity,
data preparation, interpreter selection and service start are replaced with
local test stubs. No service, network listener or privileged operation is used.
The server loader is executed from its AST without importing server globals.
"""
import argparse
import ast
import builtins
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


PACKAGE = Path(__file__).resolve().parent / "nasscreencompanion"
TIMEOUT = 10.0
DEVICES = [{"name": "anonymous-board", "sha256": "fixture-not-a-real-hash",
            "last4": "test", "added_at": 1, "last_seen": 2}]
EXISTING = {"docker_enabled": True, "devices": DEVICES, "port": 12345,
            "bind": "127.0.0.1", "auth_mode": "pairing", "interval": 2.5,
            "tls_enabled": True, "hist_len": 90, "fixture_extra": "preserve"}


class ConfigPersistenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="fnos-config-persistence-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cmd = self.root / "target" / "cmd"
        self.cmd.mkdir(parents=True)
        self.data = self.root / "var"
        self.data.mkdir()
        self.config = self.data / "config.json"
        self.flag = self.data / "upgrade.was_running"
        self.started = self.root / "service-started"
        for name in ("_common.sh", "install_callback", "upgrade_callback"):
            shutil.copyfile(PACKAGE / "cmd" / name, self.cmd / name)
        # Appended definitions override only host-specific operations. Configuration
        # readers/writers and all callback control flow remain the production source.
        with (self.cmd / "_common.sh").open("a", encoding="utf-8") as stream:
            stream.write("""
as_package() { "$@"; }
prepare_package_data() { return 0; }
resolve_python() { printf '%s\\n' "$TEST_PYTHON"; }
service_start() { printf 'started\\n' >> "$TEST_START_MARKER"; }
""")
        self.interpreter = self.root / "fixture-python"
        self.interpreter.write_text(
            "#!" + sys.executable + "\n" + """
import builtins, os, sys
from unittest import mock
source = sys.stdin.read()
sys.argv = sys.argv[1:]
real_open = builtins.open
def fixture_open(path, mode="r", *args, **kwargs):
    if (os.environ.get("TEST_DENY_READ") == "1"
            and not isinstance(path, int)
            and os.fspath(path) == os.environ["TEST_CONFIG"]
            and "r" in mode):
        raise PermissionError("synthetic config read denied")
    return real_open(path, mode, *args, **kwargs)
with mock.patch("builtins.open", side_effect=fixture_open):
    exec(compile(source, "lifecycle-inline", "exec"), {"__name__": "__main__"})
""", encoding="utf-8")
        self.interpreter.chmod(0o700)
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith(("TRIM_", "NSC_", "wizard_", "TEST_"))}
        self.env.update({
            "TRIM_APPDEST": str(self.cmd.parent), "TRIM_PKGVAR": str(self.data),
            "TRIM_PKGETC": str(self.root / "etc"), "TRIM_OLD_APPVER": "fixture-old",
            "TRIM_APPVER": "fixture-new", "TEST_PYTHON": str(self.interpreter),
            "TEST_START_MARKER": str(self.started), "TEST_CONFIG": str(self.config),
            "PYTHONDONTWRITEBYTECODE": "1",
        })
        server = PACKAGE / "app" / "server" / "nas_companion_server.py"
        tree = ast.parse(server.read_text(encoding="utf-8"), filename=str(server))
        selected = [node for node in tree.body
                    if (isinstance(node, ast.FunctionDef) and node.name == "load_config")
                    or (isinstance(node, ast.Assign) and any(
                        isinstance(target, ast.Name) and target.id == "DEFAULT_CONFIG"
                        for target in node.targets))]
        self.assertEqual(len(selected), 2, "expected production loader and defaults")
        self.loader_namespace = {"json": json, "os": os, "cfg_path": lambda: str(self.config),
                                 "log": mock.Mock()}
        exec(compile(ast.Module(body=selected, type_ignores=[]), str(server), "exec"),
             self.loader_namespace)

    def write_config(self, value=EXISTING):
        self.config.write_bytes((json.dumps(value, ensure_ascii=False, indent=1) + "\n").encode())
        self.config.chmod(0o600)

    def snapshot(self):
        info = self.config.stat()
        return (self.config.read_bytes(), info.st_ino, info.st_mtime_ns,
                stat.S_IMODE(info.st_mode))

    def run_writer(self, writer, **environment):
        env = dict(self.env, **environment)
        if writer == "apply":
            # Explicit false is a real settings choice, not an absent wizard value.
            command = ['bash', '-c',
                       '. "$1"; apply_config "$TEST_PYTHON" "$2" "$3" "$4" "$5" "$6" "$7"',
                       'fixture', str(self.cmd / "_common.sh"),
                       str(EXISTING["port"]), EXISTING["bind"], EXISTING["auth_mode"],
                       "false", str(EXISTING["interval"]), "true"]
        else:
            command = ['bash', str(self.cmd / (writer + "_callback"))]
        return subprocess.run(command, env=env, capture_output=True, text=True,
                              timeout=TIMEOUT, check=False)

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def assert_preserved(self, previous):
        self.assertEqual(self.snapshot(), previous, "original config bytes/metadata changed")
        self.assertFalse(Path(str(self.config) + ".tmp").exists())

    def load_config(self):
        with mock.patch.dict(os.environ, self.env, clear=True):
            return self.loader_namespace["load_config"]()

    def test_reinstall_preserves_existing_docker_devices_and_exact_file(self):
        for wizard in ({}, {"wizard_docker": "false"}):
            with self.subTest(wizard=wizard):
                self.write_config()
                previous = self.snapshot()
                self.assert_success(self.run_writer("install", **wizard))
                self.assert_preserved(previous)
                self.assertFalse(self.started.exists())

    def test_new_install_applies_explicit_wizard_values(self):
        self.assert_success(self.run_writer(
            "install", wizard_port=str(EXISTING["port"]), wizard_bind="local",
            wizard_auth="pairing", wizard_docker="true", wizard_interval="2.5",
            wizard_tls="true"))
        cfg = json.loads(self.config.read_text())
        self.assertIs(cfg["docker_enabled"], True)
        self.assertEqual(cfg["devices"], [])
        self.assertEqual(cfg["port"], EXISTING["port"])
        self.assertEqual(cfg["bind"], EXISTING["bind"])
        self.assertEqual(stat.S_IMODE(self.config.stat().st_mode), 0o600)
        self.assertFalse(self.started.exists())

    def test_explicit_false_settings_still_disable_docker_and_keep_devices(self):
        self.write_config()
        self.assert_success(self.run_writer("apply"))
        cfg = json.loads(self.config.read_text())
        self.assertIs(cfg["docker_enabled"], False)
        self.assertEqual(cfg["devices"], DEVICES)
        self.assertEqual(cfg["hist_len"], EXISTING["hist_len"])
        self.assertEqual(cfg["fixture_extra"], EXISTING["fixture_extra"])

    def test_upgrade_keeps_existing_values_and_resumes_only_after_success(self):
        self.write_config({"docker_enabled": True, "devices": DEVICES,
                           "fixture_extra": EXISTING["fixture_extra"]})
        self.flag.write_bytes(b"running\n")
        self.assert_success(self.run_writer("upgrade"))
        cfg = json.loads(self.config.read_text())
        self.assertIs(cfg["docker_enabled"], True)
        self.assertEqual(cfg["devices"], DEVICES)
        self.assertEqual(cfg["fixture_extra"], EXISTING["fixture_extra"])
        self.assertIn("port", cfg)  # Add a missing default without clobbering known values.
        self.assertEqual(self.started.read_bytes(), b"started\n")
        self.assertFalse(self.flag.exists())

    def test_writer_read_errors_abort_without_replacing_config_or_starting(self):
        errors = {
            "denied": (json.dumps(EXISTING).encode(), {"TEST_DENY_READ": "1"}),
            "malformed": (b'{"docker_enabled": true,\n', {}),
            "list": (b'[{"name":"anonymous-board"}]\n', {}),
            "null": (b'null\n', {}),
        }
        for writer in ("apply", "upgrade"):
            for name, (content, environment) in errors.items():
                with self.subTest(writer=writer, error=name):
                    self.config.write_bytes(content)
                    self.config.chmod(0o600)
                    self.flag.write_bytes(b"running\n")
                    previous = self.snapshot()
                    result = self.run_writer(writer, **environment)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assert_preserved(previous)
                    self.assertEqual(self.flag.read_bytes(), b"running\n")
                    self.assertFalse(self.started.exists())

    def test_missing_file_is_normal_first_configuration_for_writers(self):
        for writer in ("apply", "upgrade", "install"):
            with self.subTest(writer=writer):
                if self.config.exists():
                    self.config.unlink()
                self.assert_success(self.run_writer(writer))
                cfg = json.loads(self.config.read_text())
                self.assertIs(cfg["docker_enabled"], False)
                self.assertEqual(cfg["devices"], [])
                self.assertFalse(self.started.exists())

    def test_loader_preserves_true_and_devices_and_accepts_explicit_false(self):
        for enabled in (True, False):
            with self.subTest(enabled=enabled):
                value = dict(EXISTING, docker_enabled=enabled)
                self.write_config(value)
                previous = self.snapshot()
                cfg = self.load_config()
                self.assertIs(cfg["docker_enabled"], enabled)
                self.assertEqual(cfg["devices"], DEVICES)
                self.assertEqual(cfg["port"], EXISTING["port"])
                self.assert_preserved(previous)

    def test_loader_missing_file_returns_defaults_without_writing(self):
        cfg = self.load_config()
        self.assertEqual(cfg, self.loader_namespace["DEFAULT_CONFIG"])
        self.assertFalse(self.config.exists())
        self.assertFalse(Path(str(self.config) + ".tmp").exists())

    def test_loader_rejects_read_errors_and_non_object_json_without_writing(self):
        cases = {
            "malformed": (b'{"docker_enabled": true,\n', json.JSONDecodeError),
            "list": (b'[{"name":"anonymous-board"}]\n', ValueError),
            "null": (b'null\n', ValueError),
        }
        for name, (content, exception) in cases.items():
            with self.subTest(error=name):
                self.config.write_bytes(content)
                previous = self.snapshot()
                with self.assertRaises(exception):
                    self.load_config()
                self.assert_preserved(previous)
        self.write_config()
        previous = self.snapshot()
        real_open = builtins.open

        def denied_open(path, mode="r", *args, **kwargs):
            if not isinstance(path, int) and os.fspath(path) == str(self.config) and "r" in mode:
                raise PermissionError("synthetic config read denied")
            return real_open(path, mode, *args, **kwargs)

        with mock.patch("builtins.open", side_effect=denied_open):
            with self.assertRaises(PermissionError):
                self.load_config()
        self.assert_preserved(previous)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=PACKAGE)
    parser.add_argument("--timeout", type=float, default=TIMEOUT)
    args, remaining = parser.parse_known_args()
    PACKAGE = args.package.resolve()
    TIMEOUT = args.timeout
    unittest.main(argv=[sys.argv[0], *remaining])
