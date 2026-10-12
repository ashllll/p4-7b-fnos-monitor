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


def row_from_container(c):
    """容器 → 板上读的三个字段。容器名**不截断**（审计 H21）。

    以前这里是 `…lstrip('/')[:23]`：共享前 23 个字符的两个容器在屏上同名，
    而"这一行还是不是同一个容器"正是按名字比的 —— 截短名字就是把身份弄丢。
    名字是身份；省略显示是**渲染端**的事（长名字走预览里的溢出/字形审计）。
    `id` 只作稳定身份备用，板子不读它也不受影响。
    """
    return {'n': str((c.get('Names') or [str(c.get('Id', '?'))[:12]])[0]).lstrip('/'),
            'id': str(c.get('Id') or '')[:12],
            'up': c.get('State') == 'running',
            's': str(c.get('Status') or '')[:39]}


def rows_from(raw):
    """Docker API 的原始列表 → 排序后的面板行。"""
    if not isinstance(raw, list) or any(not isinstance(c, dict) for c in raw):
        raise ValueError('Docker response is not a container list')
    out = [row_from_container(c) for c in raw]
    out.sort(key=lambda c: (not c['up'], c['n']))
    return out


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
    result = rows_from(rows)
    return {'rows': result, 'total': len(result), 'ts': int(time.time())}
