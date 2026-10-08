"""Bounded read-only Docker transport, direct in tests or through the inherited reader FD."""
import http.client
import json
import os
import socket
import struct
import threading
import time

MAX_RESPONSE = 4 * 1024 * 1024
MAX_FRAME = 256 * 1024
_lock = threading.Lock()
_reader = None
_broken = False


def _reap_reader():
    global _broken
    try:
        os.waitpid(int(os.environ['NSC_DOCKER_READER_PID']), 0)
    except ChildProcessError:
        return
    with _lock:
        _broken = True
        if _reader is not None:
            _reader.close()


# Reap this specific child without changing SIGCHLD semantics for TLS subprocesses.
if os.environ.get('NSC_DOCKER_READER_PID'):
    threading.Thread(target=_reap_reader, name='docker-reader-reaper', daemon=True).start()


def _receive(sock, size, deadline):
    data = bytearray()
    while len(data) < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('container reader deadline exceeded')
        sock.settimeout(remaining)
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError('container reader closed')
        data.extend(chunk)
    return bytes(data)


def containers():
    global _reader, _broken
    fd = os.environ.get('NSC_DOCKER_READER_FD')
    if fd:
        with _lock:
            if _broken:
                raise ConnectionError('container reader unavailable; restart the application')
            if _reader is None:
                _reader = socket.socket(fileno=int(fd))
            try:
                deadline = time.monotonic() + 4
                _reader.settimeout(4)
                _reader.sendall(b'R')
                size = struct.unpack('!I', _receive(_reader, 4, deadline))[0]
                if not 0 < size <= MAX_FRAME:
                    raise ValueError('container reader frame exceeds byte budget')
                frame = json.loads(_receive(_reader, size, deadline))
                if not isinstance(frame, dict):
                    raise ValueError('invalid container reader frame')
            except Exception:
                _reader.close()
                _broken = True
                raise
        if 'errno' in frame:
            raise OSError(frame['errno'], frame.get('error', 'container reader failed'))
        return frame
    # Standalone collector / local tests. No arbitrary URLs, methods or command input.
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as raw:
        raw.settimeout(2)
        raw.connect('/var/run/docker.sock')
        raw.sendall(b'GET /containers/json?all=1 HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n')
        with http.client.HTTPResponse(raw) as response:
            response.begin()
            if response.status != 200:
                raise RuntimeError('Docker HTTP %d' % response.status)
            body = response.read(MAX_RESPONSE + 1)
    if len(body) > MAX_RESPONSE:
        raise ValueError('Docker response exceeds byte budget')
    rows = json.loads(body)
    if not isinstance(rows, list) or any(not isinstance(c, dict) for c in rows):
        raise ValueError('Docker response is not a container list')
    result = [{'n': str((c.get('Names') or [str(c.get('Id', '?'))[:12]])[0]).lstrip('/')[:23],
               'up': c.get('State') == 'running', 's': str(c.get('Status') or '')[:39]}
              for c in rows]
    result.sort(key=lambda c: (not c['up'], c['n']))
    return {'rows': result, 'total': len(result), 'ts': int(time.time())}
