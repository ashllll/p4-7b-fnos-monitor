"""MPT temperature read contract; the fake ioctl emulates firmware reply buffers."""
import ctypes
import importlib.util
import pathlib
import os
import socket
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'nasscreencompanion/app/server'))
spec = importlib.util.spec_from_file_location('mpt_launch', HERE / 'nasscreencompanion/cmd/docker_launch.py')
launch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launch)


class TemperatureTests(unittest.TestCase):
    def test_reader_fixed_operations_cache_and_unknown_command(self):
        parent, child = socket.socketpair()
        pid = os.fork()
        if pid == 0:
            parent.close()
            try:
                with patch.object(launch, 'mpt_temperatures', return_value={'rows': [{'c': 68}], 'errors': []}) as read, \
                     patch.object(launch, 'snapshot', return_value={'rows': [], 'total': 0}):
                    launch.reader(child, None, os.getppid())
                    os._exit(0 if read.call_count == 1 else 1)
            except BaseException:
                os._exit(2)
        child.close()
        try:
            parent.settimeout(3)
            import json
            def frame(command):
                parent.sendall(command)
                size = struct.unpack('!I', parent.recv(4))[0]
                body = bytearray()
                while len(body) < size:
                    body.extend(parent.recv(size-len(body)))
                return json.loads(body)
            self.assertEqual(frame(b'T')['rows'][0]['c'], 68)
            self.assertEqual(frame(b'T')['rows'][0]['c'], 68)
            self.assertEqual(frame(b'R')['total'], 0)
            parent.sendall(b'X')
            self.assertEqual(parent.recv(1), b'')
        finally:
            parent.close()
            _, status = os.waitpid(pid, 0)
        self.assertEqual(status, 0)

    def test_fixed_read_only_mpi_contract(self):
        actions = []
        def ioctl(fd, opcode, command, mutate):
            self.assertEqual((fd, opcode, mutate), (7, 0xC0484C14, True))
            h = struct.unpack_from('@IIII4P5I', command)
            self.assertEqual(h[:4], (3, 0, 0, 10))
            self.assertEqual((h[6], h[7], h[10], h[11], h[12]), (0, 0, 0, 0, 7))
            request = command[68:]
            self.assertEqual((request[3], request[22], request[23]), (4, 7, 0))
            self.assertEqual(request[24:28], bytes(4))
            action = request[0]; actions.append(action)
            self.assertIn(action, (0, 1))
            reply = bytearray(128); reply[3] = 4; reply[20:24] = bytes((5, 10, 7, 0))
            ctypes.memmove(h[4], bytes(reply), len(reply))
            if action:
                self.assertEqual(h[9], 40)
                data = bytearray(40); data[:4] = bytes((5, 10, 7, 0))
                struct.pack_into('<HB', data, 16, 68, 2)
                ctypes.memmove(h[5], bytes(data), len(data))
            else:
                self.assertEqual((h[5], h[9]), (0, 0))
        with patch('fcntl.ioctl', side_effect=ioctl):
            page = launch.mpt_page7(7, 3)
        self.assertEqual(actions, [0, 1])
        self.assertEqual(struct.unpack_from('<HB', page, 16), (68, 2))

    def test_invalid_firmware_reply_is_not_a_temperature(self):
        def ioctl(fd, opcode, command, mutate):
            h = struct.unpack_from('@IIII4P5I', command)
            reply = bytearray(128); struct.pack_into('<H', reply, 14, 0x22)
            ctypes.memmove(h[4], bytes(reply), len(reply))
        with patch('fcntl.ioctl', side_effect=ioctl), self.assertRaises(OSError):
            launch.mpt_page7(7, 0)

    def test_controller_identity_units_and_missing_channel(self):
        with tempfile.TemporaryDirectory() as root:
            host = pathlib.Path(root) / '0000:09:00.0/host0'
            host.mkdir(parents=True)
            for name, value in {'proc_name': 'mpt3sas', 'unique_id': '0',
                                'version_mpi': '205.2a', 'board_name': 'Demo SAS HBA'}.items():
                (host/name).write_text(value)
            page = bytearray(40)
            struct.pack_into('<HB', page, 16, 158, 1)  # 70 Celsius.
            struct.pack_into('<HB', page, 20, 65535, 0)  # Absent, never display 65535.
            with patch('glob.glob', side_effect=lambda p: [str(host)] if 'scsi_host' in p else []), \
                 patch.object(launch.os, 'open', return_value=7) as opened, \
                 patch.object(launch.os, 'close'), patch.object(launch, 'mpt_page7', return_value=bytes(page)):
                result = launch.mpt_temperatures()
            self.assertEqual(result['errors'], [])
            self.assertEqual(result['rows'], [{'n': '0000:09:00.0 IOC', 'dev': '0000:09:00.0',
                              'dn': 'HBA Demo SAS HBA', 'ch': 'IOC', 'c': 70.0}])
            self.assertEqual(opened.call_args.args[0], '/dev/mpt3ctl')
            self.assertFalse(opened.call_args.args[1] & launch.os.O_RDWR)

    def test_controller_rows_join_hwmon_and_failures_stay_explicit(self):
        import fnos_collector as collector
        import docker_api
        c = collector.Collector.__new__(collector.Collector)
        c._totals = {}; c._dev_cache = None; c._pci_cache = {}; c._cpu_name = None
        row = {'n': 'pci IOC', 'dev': 'pci', 'dn': 'HBA Demo', 'ch': 'IOC', 'c': 68.0}
        with patch.object(c, '_ls', return_value=[]), \
             patch.object(docker_api, 'controller_temperatures', return_value={'rows': [row], 'errors': []}):
            self.assertEqual(c._temps(), [row])
            self.assertIsNone(c._temps_partial)
        with patch.object(c, '_ls', return_value=[]), \
             patch.object(docker_api, 'controller_temperatures', return_value={'rows': [], 'errors': ['read denied']}):
            self.assertEqual(c._temps(), [])
            self.assertEqual(c._temps_partial, 'read denied')


if __name__ == '__main__':
    unittest.main()
