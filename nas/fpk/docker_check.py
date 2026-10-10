#!/usr/bin/env python3
"""Docker fixed GET / IPC integration and per-module freshness regression checks."""
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
from types import SimpleNamespace
from unittest.mock import patch

sys.dont_write_bytecode = True
BASE = Path(__file__).resolve().parent / 'nasscreencompanion'
sys.path.insert(0, str(BASE / 'app/server'))
import docker_api as api
import fnos_collector as collector
spec = importlib.util.spec_from_file_location('docker_launch', BASE / 'cmd/docker_launch.py')
launch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launch)


def publish(c):
    snap = c.sample()
    c.snapshot = snap
    return snap


def main():
    c = collector.CompanionCollector(docker_enabled=False)
    with patch.object(api, 'containers', side_effect=AssertionError('disabled must not query')):
        assert publish(c)['modules']['docker']['status'] == 'disabled'
        assert collector.probe_capabilities(False)['modules']['docker'] == 'disabled'
    c.docker_enabled = True
    with patch.object(api, 'containers', side_effect=PermissionError(13, 'test denied')), patch.object(c, '_mem', return_value={'pct': 95}):
        failed = publish(c)
        assert failed['modules']['docker']['status'] == 'denied'
        # 值必须是数组：板子按类型解析 payload（v1 契约，test_lifecycle.sh 7b 盯着这点）。
        # "查不到"与"没有容器"的区别写在 modules.docker.status=denied 与 errors 里，
        # 不靠把类型写成 None —— 那会让老固件的解析直接退化成"没有容器"，还破坏类型保证。
        assert failed['docker'] == []
        assert 'docker' in (failed.get('errors') or [])
        assert any(a['m'] == 'MEM used 95%' for a in failed['alerts'])
        assert collector.probe_capabilities(True)['modules']['docker'] == 'denied'
    count = 257  # Cross the previous fixed inventory limit using anonymous data.
    frame = {'rows': [{'n': 'test-%d' % i, 's': 'Up', 'up': True} for i in range(count)],
             'total': count, 'ts': 1234}
    with patch.object(api, 'containers', return_value=frame) as get:
        fresh = publish(c)
        cached = publish(c)
        assert get.call_count == 1
        assert cached['modules']['docker'] == {'status': 'ok', 'ts': 1234, 'error': None}
        assert cached['trunc']['totals']['docker'] == count
        assert len(cached['docker']) == count
        assert cached['trunc']['dropped']['docker'] == 0
    c._docker_cache = (0, c._docker_cache[1])
    with patch.object(api, 'containers', side_effect=FileNotFoundError(2, 'test missing')):
        stale = publish(c)
        assert stale['modules']['docker']['status'] == 'stale'
        assert stale['modules']['docker']['ts'] == 1234
        assert stale['docker'] == fresh['docker']
        assert stale['trunc']['totals']['docker'] == count
    with patch.object(api, 'containers', return_value={'rows': [], 'total': 0, 'ts': 2345}):
        empty = publish(c)
        assert empty['modules']['docker']['status'] == 'ok'
        assert empty['modules']['docker']['ts'] == 2345
        assert empty['docker'] == []
    c.docker_enabled = False
    publish(c)
    c.docker_enabled = True
    c._docker_cache = (0, [])
    with patch.object(api, 'containers', side_effect=RuntimeError('bad JSON')):
        assert publish(c)['modules']['docker']['status'] == 'error'

    # Real Unix HTTP framing, including chunked response; no actual Docker needed.
    with tempfile.TemporaryDirectory() as tmp:
        path = str(Path(tmp) / 'docker.sock')
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(path); server.listen()
        request = []
        long_name = 'running-' + '容器名称' * 30
        long_status = 'Up ' + 'status-details ' * 12
        full_id = 'a' * 64
        body = json.dumps([{'Names': ['/stopped'], 'State': 'exited', 'Status': 'Exited'},
                           {'Names': ['/' + long_name], 'State': 'running', 'Status': long_status},
                           {'Id': full_id, 'State': 'exited', 'Status': 'Exited'}]).encode()
        def serve():
            for _ in range(3):
                conn, _ = server.accept()
                with conn:
                    request.append(conn.recv(4096))
                    conn.sendall(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n'
                                 + ('%x\r\n' % len(body)).encode() + body + b'\r\n0\r\n\r\n')
        thread = threading.Thread(target=serve); thread.start()
        with patch.object(launch, 'DOCKER_SOCKET', path):
            got = launch.snapshot()
            limited = launch.snapshot(1)
            zero = launch.snapshot(0)
        thread.join(3); server.close()
        assert not thread.is_alive()
        assert request[0].startswith(b'GET /containers/json?all=1 HTTP/1.0\r\n')
        assert got['total'] == 3 and len(got['rows']) == 3
        assert got['rows'][0] == {'n': long_name, 'up': True, 's': long_status}
        assert any(row['n'] == full_id for row in got['rows'])
        assert limited['rows'] == got['rows'][:1] and limited['total'] == 3
        assert zero['rows'] == [] and zero['total'] == 3

    # Exercise the real privileged entry's argument validation without changing
    # host privileges or forking: stop at its first service socket allocation.
    class ReachedServiceStart(Exception):
        pass
    args = ['docker_launch.py', '--user', 'test-package', '--group', 'test-package',
            '--python', '/test/python', '--server', '/test/server.py', '--log', '/test/info.log']
    for extra, accepted in (([], True), (['--limit', '0'], True),
                            (['--limit', '3'], True), (['--limit', '-1'], False)):
        with patch.object(sys, 'argv', args + extra), patch.object(launch.os, 'getuid', return_value=0), \
             patch.object(launch, 'trusted_code'), \
             patch.object(launch.pwd, 'getpwnam', return_value=SimpleNamespace(pw_uid=123)), \
             patch.object(launch.grp, 'getgrnam', return_value=SimpleNamespace(gr_gid=123)), \
             patch.object(launch.socket, 'socketpair', side_effect=ReachedServiceStart):
            try:
                launch.main()
                raise AssertionError('entry unexpectedly returned')
            except ReachedServiceStart:
                assert accepted
            except ValueError:
                assert not accepted

    # The privileged reader protocol accepts R only and closes on EOF / invalid input.
    for command in (b'R', b'X', b''):
        client, worker = socket.socketpair()
        with patch.object(launch, 'snapshot', return_value=frame):
            pid = os.fork()
            if not pid:
                client.close()
                try: launch.reader(worker, count, os.getppid())
                finally: os._exit(0)
        worker.close(); client.settimeout(4)
        if command == b'': client.shutdown(socket.SHUT_WR)
        else: client.sendall(command)
        if command == b'R':
            size = struct.unpack('!I', api._receive(client, 4, __import__('time').monotonic() + 4))[0]
            assert json.loads(api._receive(client, size, __import__('time').monotonic() + 4)) == frame
            client.shutdown(socket.SHUT_WR)
        assert client.recv(1) == b''
        client.close(); assert os.waitpid(pid, 0)[1] == 0

    # A damaged frame is terminal: never read a late reply as the next snapshot.
    client, worker = socket.socketpair()
    worker.sendall(struct.pack('!I', api.MAX_FRAME + 1))
    with patch.dict(os.environ, NSC_DOCKER_READER_FD=str(client.fileno())):
        api._reader = client; api._broken = False
        try: api.containers(); raise AssertionError('oversize accepted')
        except ValueError: pass
        try: api.containers(); raise AssertionError('broken stream reused')
        except ConnectionError: pass
    worker.close(); api._reader = None; api._broken = False
    print('PASS: disabled / denied / full inventory / full names and IDs / explicit policy / root startup defaults / cached totals / stale timestamp / recovery / empty / fixed HTTP GET / IPC framing / reader exit')


if __name__ == '__main__':
    main()
