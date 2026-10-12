#!/usr/bin/env python3
"""只读端口预检：匿名内核表、生命周期桩和本机短时监听，无 NAS。"""
import io
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

CMD = Path(__file__).resolve().parent / 'nasscreencompanion/cmd'
COMMON = (CMD/'_common.sh').read_text()
PROBE = COMMON.split('"$py" - "$1" <<\'PYEOF\'\n', 1)[1].split('\nPYEOF', 1)[0]
HEADER = '  sl  local_address rem_address   st\n'

def row(address, port, state='0A'):
    return '0: %s:%04X 00000000:0000 %s\n' % (address, port, state)

class PortPreflight(unittest.TestCase):
    def probe(self, tables):
        def opened(path, *args, **kwargs):
            value = tables[path]
            if isinstance(value, Exception):
                raise value
            return io.StringIO(value)
        with patch('platform.system', return_value='Linux'), patch('builtins.open', side_effect=opened), patch('os.path.exists', return_value=True), patch.object(sys, 'argv', ['probe', '24680']), patch('sys.stderr', new_callable=io.StringIO):
            with self.assertRaises(SystemExit) as result:
                exec(compile(PROBE, '_common.sh:port_busy', 'exec'), {})
        return result.exception.code

    def test_nonloopback_ipv4_and_ipv6_and_state(self):
        empty = HEADER
        for address, table in [('1709A8C0', '/proc/net/tcp'), ('00000000000000000000000001000000', '/proc/net/tcp6')]:
            tables = {'/proc/net/tcp': empty, '/proc/net/tcp6': empty}
            tables[table] += row(address, 24680)
            self.assertEqual(self.probe(tables), 0)
            tables[table] = HEADER + row(address, 24680, '01') + row(address, 24681)
            self.assertEqual(self.probe(tables), 1)

    def test_unreadable_or_malformed_is_unknown(self):
        for bad in [PermissionError('fixture'), 'no header', HEADER+'broken\n', HEADER+row('00000000', 1).replace('0001', 'oops')]:
            self.assertEqual(self.probe({'/proc/net/tcp':bad, '/proc/net/tcp6':HEADER}), 2)

    def test_actual_local_listener_without_connecting(self):
        with socket.socket() as listener, tempfile.TemporaryDirectory() as temp:
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            port = listener.getsockname()[1]
            def check():
                return subprocess.run(['bash', '-c', '. "$1"; port_busy "$2"', 'probe', str(CMD/'_common.sh'), str(port)], env={**os.environ, 'TRIM_APPDEST':temp}, capture_output=True, timeout=10)
            result = check()
            self.assertEqual(result.returncode, 0, result.stderr)
            listener.settimeout(0.05)
            with self.assertRaises(socket.timeout):
                listener.accept()  # 预检没有建立连接
            listener.close()
            result = check()
            self.assertEqual(result.returncode, 1, result.stderr)

    def test_callers_reject_unknown_and_keep_own_port_exception(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cmd = root/'cmd'
            cmd.mkdir()
            for name in ('_common.sh', 'install_init', 'config_init', 'main'):
                shutil.copyfile(CMD/name, cmd/name)
            with (cmd/'_common.sh').open('a') as out:
                out.write("\nport_busy() { return \"$TEST_PORT_RESULT\"; }\nprepare_package_data() { return 0; }\nas_package() { \"$@\"; }\nresolve_python() { printf '%s' \"$TEST_PYTHON\"; }\nis_running() { [ \"${TEST_RUNNING:-0}\" = 1 ]; }\n")
            (root/'server').mkdir()
            (root/'server/nas_companion_server.py').touch()
            data = root/'var'
            data.mkdir()
            (data/'config.json').write_text('{"port":24680}')
            env = {k:v for k,v in os.environ.items() if not k.startswith(('TRIM_', 'TEST_', 'wizard_'))}
            env.update(TRIM_APPDEST=temp, TRIM_PKGVAR=str(data), TEST_PYTHON=sys.executable, wizard_port='24680')
            def run(name, result, running='0'):
                args = ['start'] if name == 'main' else []
                return subprocess.run(['bash', str(cmd/name), *args], env={**env,'TEST_PORT_RESULT':str(result),'TEST_RUNNING':running}, capture_output=True, text=True, timeout=10)
            for name in ('install_init', 'config_init', 'main'):
                result = run(name, 2)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('无法判定端口', result.stderr)
            for name in ('install_init', 'config_init'):
                self.assertEqual(run(name, 1).returncode, 0)
                self.assertNotEqual(run(name, 0).returncode, 0)
            self.assertEqual(run('config_init', 0, '1').returncode, 0)
            self.assertNotEqual(run('config_init', 2, '1').returncode, 0)

if __name__ == '__main__':
    unittest.main()
