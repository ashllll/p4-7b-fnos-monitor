#!/usr/bin/env python3
"""Portable filesystem acceptance tests for docker_launch.restore_data.

Run from the project root:
  python3 /tmp/fnos-startup-permission-tests.py \
    --helper nas/fpk/nasscreencompanion/cmd/docker_launch.py

If integrated into nas/fpk, the sibling package is the default helper path.
Only synthetic temporary data and the current user's uid/gid are used.
Foreign ownership and device-node fixtures require a separate privileged test.
"""
import argparse
import errno
import importlib.util
import multiprocessing
import os
from pathlib import Path
import socket
import stat
import sys
import tempfile
import unittest
from unittest import mock


HELPER = Path(__file__).resolve().parent / "nasscreencompanion/cmd/docker_launch.py"
FIFO_TIMEOUT = 5.0  # Test-process deadline, configurable with --timeout.


def load_helper(path):
    spec = importlib.util.spec_from_file_location("startup_permission_target", path)
    if spec is None or spec.loader is None:
        raise ImportError("cannot load startup helper")
    module = importlib.util.module_from_spec(spec)
    previous_bytecode = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        spec.loader.exec_module(module)  # __main__ is not entered; no service is started.
    finally:
        sys.dont_write_bytecode = previous_bytecode
    if not callable(getattr(module, "restore_data", None)):
        raise AttributeError("startup helper does not expose restore_data(fd, uid, gid)")
    return module


def data_fd(path):
    return os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_NONBLOCK)


def inode_state(path):
    s = os.lstat(path)
    return (s.st_dev, s.st_ino, s.st_uid, s.st_gid, stat.S_IMODE(s.st_mode),
            stat.S_IFMT(s.st_mode), s.st_nlink)


def fifo_worker(helper_path, directory, uid, gid, connection):
    """A blocked FIFO open cannot hang the unit-test runner."""
    fd = None
    try:
        helper = load_helper(helper_path)
        fd = data_fd(directory)
        try:
            helper.restore_data(fd, uid, gid)
        except (OSError, ValueError) as exc:
            connection.send(("rejected", type(exc).__name__))
        except Exception as exc:
            connection.send(("unexpected", type(exc).__name__))
        else:
            connection.send(("returned", "unsafe FIFO was accepted"))
    except Exception as exc:
        connection.send(("unexpected", type(exc).__name__))
    finally:
        if fd is not None:
            try:
                os.close(fd)
            except OSError:
                pass
        connection.close()


@unittest.skipUnless(os.name == "posix", "requires POSIX directory fds and permissions")
class RestoreDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.helper = load_helper(HELPER)
        cls.uid, cls.gid = os.getuid(), os.getgid()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="fnos-restore-data-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.private = self.root / "private"
        self.outside = self.root / "outside"
        self.private.mkdir()
        self.outside.mkdir()
        os.chmod(self.private, 0o777)
        os.chmod(self.outside, 0o750)

    def restore(self):
        fd = data_fd(self.private)
        try:
            self.helper.restore_data(fd, self.uid, self.gid)
            os.fstat(fd)  # The caller retains ownership of the supplied fd.
        finally:
            os.close(fd)

    def assert_rejected(self):
        fd = data_fd(self.private)
        try:
            # A later fd check or cleanup error must not masquerade as refusal.
            with self.assertRaises((OSError, ValueError)):
                self.helper.restore_data(fd, self.uid, self.gid)
            os.fstat(fd)
        finally:
            os.close(fd)

    def assert_private(self, path, directory=False):
        s = os.lstat(path)
        self.assertEqual((s.st_uid, s.st_gid), (self.uid, self.gid))
        self.assertEqual(stat.S_IMODE(s.st_mode), 0o700 if directory else 0o600)
        self.assertTrue(stat.S_ISDIR(s.st_mode) if directory else stat.S_ISREG(s.st_mode))

    def populated_tree(self):
        payloads = {
            "config.json": b'{"fixture":true}\n',
            "tls/server.crt": b"SYNTHETIC CERTIFICATE\n",
            "tls/server.key": bytes(range(256)) + b"\x00fixture-key\n",
            "backup/previous/config.json": b'{"version":"previous-fixture"}\n',
            "backup/previous/tls/server.crt": b"PREVIOUS SYNTHETIC CERTIFICATE\n",
        }
        for relative, data in payloads.items():
            path = self.private / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            os.chmod(path, 0o666)
        for path in (self.private, *self.private.rglob("*")):
            if path.is_dir():
                os.chmod(path, 0o777)
        return payloads

    def test_recursive_tls_backup_and_file_bytes(self):
        payloads = self.populated_tree()
        self.restore()
        for path in (self.private, *self.private.rglob("*")):
            self.assert_private(path, directory=path.is_dir())
        for relative, data in payloads.items():
            self.assertEqual((self.private / relative).read_bytes(), data)

    def test_restore_is_repeatable(self):
        payloads = self.populated_tree()
        self.restore()
        paths = (self.private, *self.private.rglob("*"))
        before = {path: inode_state(path) for path in paths}
        self.restore()
        self.assertEqual({path: inode_state(path) for path in paths}, before)
        for relative, data in payloads.items():
            self.assertEqual((self.private / relative).read_bytes(), data)

    def test_upgrade_staging_uses_installed_framework_binding(self):
        staging = self.root / 'appcenter-downloads' / 'fixture-1.2.1-tpk'
        (staging / 'cmd').mkdir(parents=True)
        manifest = staging / 'manifest'
        manifest.write_text('appname = fixture\nversion = 1.2.1\n')
        with mock.patch.object(self.helper, '__file__', str(staging / 'cmd/docker_launch.py')), \
                mock.patch.object(self.helper, 'trusted_code') as trust:
            self.assertEqual(self.helper.installed_app_dir(), '/var/apps/fixture')
            self.assertEqual(trust.call_args_list,
                             [mock.call(str(manifest)), mock.call('/var/apps/fixture')])

    def test_invalid_manifest_identity_cannot_redirect_data(self):
        staging = self.root / 'staging'
        (staging / 'cmd').mkdir(parents=True)
        manifest = staging / 'manifest'
        with mock.patch.object(self.helper, '__file__', str(staging / 'cmd/docker_launch.py')), \
                mock.patch.object(self.helper, 'trusted_code') as trust:
            for identity in ('../outside', '/outside', 'fixture\nappname = outside'):
                manifest.write_text('appname = ' + identity + '\n')
                trust.reset_mock()
                with self.assertRaises(ValueError):
                    self.helper.installed_app_dir()
                trust.assert_called_once_with(str(manifest))

    def test_initial_sealing_failure_restores_owner_and_keeps_original_error(self):
        payloads = self.populated_tree()
        fd = data_fd(self.private)
        original_error = PermissionError(errno.EACCES, "synthetic initial fchmod failure")
        real_fchmod = os.fchmod
        try:
            with mock.patch.object(self.helper.os, "geteuid", return_value=0), \
                 mock.patch.object(self.helper.os, "fchown") as chown, \
                 mock.patch.object(self.helper.os, "fchmod",
                                   side_effect=[original_error, mock.DEFAULT],
                                   wraps=real_fchmod) as chmod:
                with self.assertRaises(PermissionError) as raised:
                    self.helper.restore_data(fd, self.uid, self.gid)
                self.assertIs(raised.exception, original_error)
                self.assertEqual(chown.call_args_list,
                                 [mock.call(fd, 0, 0), mock.call(fd, self.uid, self.gid)])
                self.assertEqual(chmod.call_args_list,
                                 [mock.call(fd, 0o700), mock.call(fd, 0o700)])
            os.fstat(fd)
            self.assert_private(self.private, directory=True)
            for relative, data in payloads.items():
                self.assertEqual((self.private / relative).read_bytes(), data)
        finally:
            os.close(fd)

    def test_file_symlink_cannot_change_external_file(self):
        external = self.outside / "external.txt"
        external.write_bytes(b"EXTERNAL BYTES MUST NOT CHANGE\n")
        os.chmod(external, 0o640)
        before = inode_state(external), external.read_bytes()
        link = self.private / "linked-file"
        link.symlink_to(external)
        self.assert_rejected()
        self.assertEqual((inode_state(external), external.read_bytes()), before)
        self.assertTrue(link.is_symlink())
        self.assertEqual(os.readlink(link), str(external))

    def test_directory_symlink_cannot_recurse_into_external_tree(self):
        external = self.outside / "nested"
        external.mkdir()
        os.chmod(external, 0o750)
        secret = external / "external.txt"
        secret.write_bytes(b"EXTERNAL TREE MUST NOT CHANGE\n")
        os.chmod(secret, 0o640)
        before = inode_state(external), inode_state(secret), secret.read_bytes()
        link = self.private / "tls"
        link.symlink_to(external, target_is_directory=True)
        self.assert_rejected()
        self.assertEqual((inode_state(external), inode_state(secret), secret.read_bytes()), before)
        self.assertTrue(link.is_symlink())

    def test_hardlink_cannot_change_external_inode(self):
        external = self.outside / "external.txt"
        external.write_bytes(b"EXTERNAL HARDLINK BYTES\n")
        os.chmod(external, 0o640)
        linked = self.private / "config.json"
        os.link(external, linked)
        before = inode_state(external), external.read_bytes()
        self.assertGreater(os.lstat(linked).st_nlink, 1)
        self.assert_rejected()
        self.assertEqual((inode_state(external), external.read_bytes()), before)
        self.assertEqual(os.lstat(linked).st_ino, os.lstat(external).st_ino)

    def test_fifo_is_rejected_without_blocking(self):
        fifo = self.private / "config.json"
        os.mkfifo(fifo)
        os.chmod(fifo, 0o660)
        before = inode_state(fifo)
        context = multiprocessing.get_context("spawn")
        parent, child = context.Pipe(duplex=False)
        process = context.Process(target=fifo_worker,
                                  args=(str(HELPER), str(self.private), self.uid, self.gid, child))
        try:
            process.start()
            child.close()
            process.join(FIFO_TIMEOUT)
            if process.is_alive():
                process.terminate()
                process.join(FIFO_TIMEOUT)
                if process.is_alive():
                    process.kill()
                    process.join(FIFO_TIMEOUT)
                self.fail("restore_data blocked on a FIFO instead of rejecting it")
            self.assertEqual(process.exitcode, 0)
            self.assertTrue(parent.poll(), "FIFO worker produced no verdict")
            self.assertEqual(parent.recv()[0], "rejected")
            self.assertEqual(inode_state(fifo), before)
        finally:
            child.close()
            parent.close()
            if process.pid is not None and process.is_alive():
                process.kill()
                process.join(FIFO_TIMEOUT)
            if process.pid is not None:
                process.close()

    def test_unix_socket_is_rejected(self):
        path = self.private / "config.json"
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as fixture_socket:
            fixture_socket.bind(str(path))  # Unique local pathname; no TCP port is used.
            os.chmod(path, 0o660)
            before = inode_state(path)
            self.assert_rejected()
            self.assertEqual(inode_state(path), before)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--helper", type=Path, default=HELPER)
    parser.add_argument("--timeout", type=float, default=FIFO_TIMEOUT)
    options, unittest_args = parser.parse_known_args()
    if options.timeout <= 0:
        parser.error("--timeout must be positive")
    HELPER = options.helper.resolve()
    FIFO_TIMEOUT = options.timeout
    unittest.main(argv=[sys.argv[0], *unittest_args])
