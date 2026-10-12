"""Bounded read-only Docker transport, direct in tests or through the inherited reader FD."""
import http.client
import json
import math
import re
import os
import socket
import struct
import threading
import time

DOCKER_SOCKET = '/var/run/docker.sock'
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


# The reader accepts no URL/ID from clients: IDs come only from Docker inventory.
_metrics_cache = {}
_metrics_previous = {}
_metrics_cursor = 0

def _metrics_get(identifier, suffix):
    if not re.fullmatch(r'[0-9a-f]{64}', identifier) or suffix not in ('json', 'stats?stream=false&one-shot=true'):
        raise ValueError('invalid read-only Docker metric request')
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as raw:
        raw.settimeout(0.3)
        raw.connect(DOCKER_SOCKET)
        request = ('GET /containers/%s/%s HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n' % (identifier, suffix)).encode('ascii')
        raw.sendall(request)
        with http.client.HTTPResponse(raw) as response:
            response.begin()
            if response.status != 200:
                raise RuntimeError('Docker metrics unavailable')
            body = response.read(MAX_RESPONSE+1)
    if len(body)>MAX_RESPONSE:
        raise ValueError('Docker metric response exceeds budget')
    result=json.loads(body)
    if not isinstance(result,dict):
        raise ValueError('invalid Docker metrics')
    return result

def _metric_value(value):
    return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value) and value>=0

def _enrich_metrics(raw_rows, rows):
    """Rotate through every entity; bounded work per frame, explicit cache timestamp."""
    global _metrics_cache, _metrics_previous, _metrics_cursor
    identities=[str(c.get('Id','')) for c in raw_rows]
    ids={i for i in identities if re.fullmatch(r'[0-9a-f]{64}',i)}
    _metrics_cache={k:v for k,v in _metrics_cache.items() if k in ids}
    _metrics_previous={k:v for k,v in _metrics_previous.items() if k in ids}
    eligible=sorted(ids)
    if eligible:
        identifier=eligible[_metrics_cursor % len(eligible)]
        _metrics_cursor+=1
        metrics={}
        # A failed inspection cannot publish old restart/resource values as new.
        try:
            info=_metrics_get(identifier,'json')
            count=info.get('RestartCount')
            if isinstance(count,int) and not isinstance(count,bool) and count>=0:
                metrics['restarts']=count
        except (OSError, ValueError, RuntimeError, http.client.HTTPException):
            pass
        container=next(c for c in raw_rows if c.get('Id')==identifier)
        if container.get('State')=='running':
            try:
                stats=_metrics_get(identifier,'stats?stream=false&one-shot=true')
                cpu=stats.get('cpu_stats') or {}
                usage=(cpu.get('cpu_usage') or {}).get('total_usage')
                system=cpu.get('system_cpu_usage')
                cores=cpu.get('online_cpus') or len((cpu.get('cpu_usage') or {}).get('percpu_usage') or [])
                prior=_metrics_previous.get(identifier)
                if all(_metric_value(v) for v in (usage,system,cores)):
                    if prior and usage>=prior[0] and system>prior[1] and cores>0:
                        metrics['cpu_pct']=round((usage-prior[0])/(system-prior[1])*cores*100,2)
                    _metrics_previous[identifier]=(usage,system)
                memory=stats.get('memory_stats') or {}
                used=memory.get('usage')
                inactive=(memory.get('stats') or {}).get('total_inactive_file', (memory.get('stats') or {}).get('inactive_file',0))
                if _metric_value(used) and _metric_value(inactive):
                    metrics['mem_mb']=round(max(0,used-inactive)/1024**2,2)
            except (OSError, ValueError, RuntimeError, http.client.HTTPException):
                _metrics_previous.pop(identifier,None)
        else:
            _metrics_previous.pop(identifier,None)
        if metrics: metrics['metrics_ts']=int(time.time())
        _metrics_cache[identifier]=metrics
    # Rows are still in source order here. Sorting happens after enrichment.
    for original,row in zip(raw_rows,rows):
        identifier=str(original.get('Id',''))
        row.update(id=identifier,cpu_pct=None,mem_mb=None,restarts=None,metrics_ts=0)
        row.update(_metrics_cache.get(identifier,{}))


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
    result = [row_from_container(c) for c in rows]
    _enrich_metrics(rows, result)
    result.sort(key=lambda c: (not c['up'], c['n']))
    return {'rows': result, 'total': len(result), 'ts': int(time.time())}
