#!/usr/bin/python3
"""Root lifecycle entry: fixed Docker GET child; drop privileges before the web service.

This file lives in fnOS's root-owned cmd directory, never in writable target.
The child has no listener and accepts only R on an inherited socketpair.
"""
import argparse
import errno
import grp
import http.client
import json
import math
import os
import pwd
import re
import signal
import socket
import stat
import struct
import sys
import time

MAX_RESPONSE = 4 * 1024 * 1024
MAX_FRAME = 256 * 1024
DOCKER_SOCKET = '/var/run/docker.sock'
DOCKER_REQUEST = b'GET /containers/json?all=1 HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n'


# BEGIN GENERATED DOCKER METRICS
# Canonical Docker metric implementation; entry files supply stdlib imports,
# DOCKER_SOCKET and MAX_RESPONSE. Sync with python3 nas/sync_docker_metrics.py --write.
# Embedded at build time: root never imports code from a writable app directory.

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

class DockerMetrics:
    """One collector owns rotation, cached readings and CPU baselines.

    request reads a fixed Docker metric endpoint; clock is an optional wall clock.
    enrich updates panel rows in the same source order as raw_rows, before sorting.
    """
    def __init__(self, request, clock=None):
        self._request = request
        self._clock = clock
        self._cache = {}
        self._previous = {}
        self._cursor = 0

    def enrich(self, raw_rows, rows):
        """Rotate through every entity; bounded work per frame, explicit cache timestamp."""
        identities=[str(c.get('Id','')) for c in raw_rows]
        ids={i for i in identities if re.fullmatch(r'[0-9a-f]{64}',i)}
        self._cache={k:v for k,v in self._cache.items() if k in ids}
        self._previous={k:v for k,v in self._previous.items() if k in ids}
        eligible=sorted(ids)
        if eligible:
            identifier=eligible[self._cursor % len(eligible)]
            self._cursor+=1
            metrics={}
            # A failed inspection cannot publish old restart/resource values as new.
            try:
                info=self._request(identifier,'json')
                count=info.get('RestartCount')
                if isinstance(count,int) and not isinstance(count,bool) and count>=0:
                    metrics['restarts']=count
            except (OSError, ValueError, RuntimeError, http.client.HTTPException):
                pass
            container=next(c for c in raw_rows if c.get('Id')==identifier)
            if container.get('State')=='running':
                try:
                    stats=self._request(identifier,'stats?stream=false&one-shot=true')
                    cpu=stats.get('cpu_stats') or {}
                    usage=(cpu.get('cpu_usage') or {}).get('total_usage')
                    system=cpu.get('system_cpu_usage')
                    cores=cpu.get('online_cpus') or len((cpu.get('cpu_usage') or {}).get('percpu_usage') or [])
                    prior=self._previous.get(identifier)
                    if all(_metric_value(v) for v in (usage,system,cores)):
                        if prior and usage>=prior[0] and system>prior[1] and cores>0:
                            metrics['cpu_pct']=round((usage-prior[0])/(system-prior[1])*cores*100,2)
                        self._previous[identifier]=(usage,system)
                    memory=stats.get('memory_stats') or {}
                    used=memory.get('usage')
                    inactive=(memory.get('stats') or {}).get('total_inactive_file', (memory.get('stats') or {}).get('inactive_file',0))
                    if _metric_value(used) and _metric_value(inactive):
                        metrics['mem_mb']=round(max(0,used-inactive)/1024**2,2)
                except (OSError, ValueError, RuntimeError, http.client.HTTPException):
                    self._previous.pop(identifier,None)
            else:
                self._previous.pop(identifier,None)
            if metrics: metrics['metrics_ts']=int(self._clock() if self._clock else time.time())
            self._cache[identifier]=metrics
        # Rows are still in source order here. Sorting happens after enrichment.
        for original,row in zip(raw_rows,rows):
            identifier=str(original.get('Id',''))
            row.update(id=identifier,cpu_pct=None,mem_mb=None,restarts=None,metrics_ts=0)
            row.update(self._cache.get(identifier,{}))
# END GENERATED DOCKER METRICS

_metrics = DockerMetrics(_metrics_get)


def snapshot(limit=None, metrics=False):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as raw:
        raw.settimeout(2)
        raw.connect(DOCKER_SOCKET)
        raw.sendall(DOCKER_REQUEST)
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
    result = [{'n': str((c.get('Names') or [str(c.get('Id', '?'))])[0]).lstrip('/'),
               'up': c.get('State') == 'running', 's': str(c.get('Status') or '')}
              for c in rows]
    if metrics: _metrics.enrich(rows, result)
    result.sort(key=lambda c: (not c['up'], c['n']))
    return {'rows': result if limit is None else result[:limit],
            'total': len(result), 'ts': int(time.time())}


def reader(sock, limit, parent_pid):
    def expired(signum, frame):
        raise TimeoutError('Docker request deadline exceeded')
    signal.signal(signal.SIGALRM, expired)
    with sock:
        sock.settimeout(2)
        while os.getppid() == parent_pid:
            try:
                command = sock.recv(1)
            except socket.timeout:
                continue
            if command != b'R':
                return
            try:
                signal.setitimer(signal.ITIMER_REAL, 3)
                frame = snapshot(limit, metrics=True)
            except Exception as exc:
                code = getattr(exc, 'errno', None)
                frame = {'errno': code if isinstance(code, int) else errno.EIO,
                         'error': '%s: %s' % (type(exc).__name__, exc)}
            finally:
                signal.setitimer(signal.ITIMER_REAL, 0)
            data = json.dumps(frame, separators=(',', ':')).encode()
            if len(data) > MAX_FRAME:
                return
            try:
                sock.sendall(struct.pack('!I', len(data)) + data)
            except OSError:
                return


def trusted_code(path):
    path = os.path.realpath(path)
    while True:
        s = os.stat(path)
        if s.st_uid != 0 or stat.S_IMODE(s.st_mode) & 0o022:
            raise PermissionError('root entry or parent is writable by a non-root user')
        parent = os.path.dirname(path)
        if parent == path:
            return
        path = parent


def restore_data(fd, uid, gid):
    """Repair fnOS's root-mode ownership reset without following data links."""
    info = os.fstat(fd)
    if info.st_uid not in (0, uid):
        raise PermissionError('data belongs to another user')
    directory = stat.S_ISDIR(info.st_mode)
    if not directory and (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1):
        raise PermissionError('data must be a directory or a single-link regular file')
    if not directory:
        os.fchown(fd, uid, gid)
        os.fchmod(fd, 0o600)
        return
    # Seal the directory while traversing: mode 0700 also clears the POSIX ACL
    # mask, preventing the package user from replacing entries under root's feet.
    try:
        if os.geteuid() == 0:
            os.fchown(fd, 0, 0)
        os.fchmod(fd, 0o700)
        for name in os.listdir(fd):
            entry = os.stat(name, dir_fd=fd, follow_symlinks=False)
            if not stat.S_ISDIR(entry.st_mode) and not stat.S_ISREG(entry.st_mode):
                raise PermissionError('unsupported data entry: ' + name)
            flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC
            if stat.S_ISDIR(entry.st_mode):
                flags |= os.O_DIRECTORY
            child = os.open(name, flags, dir_fd=fd)
            try:
                restore_data(child, uid, gid)
            finally:
                os.close(child)
    finally:
        # Failure must not leave the existing app data sealed away from its user.
        os.fchown(fd, uid, gid)
        os.fchmod(fd, 0o700)


def installed_app_dir():
    # fnOS runs upgrade_init from a temporary unpack directory. Read its trusted
    # manifest identity, then use the framework's installed bindings, never staging/var.
    manifest = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), 'manifest')
    trusted_code(manifest)
    with open(manifest, encoding='utf-8') as source:
        names = [line.split('=', 1)[1].strip() for line in source
                 if line.split('=', 1)[0].strip() == 'appname' and '=' in line]
    if len(names) != 1 or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', names[0]):
        raise ValueError('invalid application identity')
    app_dir = os.path.join('/var/apps', names[0])
    trusted_code(app_dir)
    return app_dir


def prepare_data(user, group):
    # TRIM_PKGVAR and caller-supplied paths cannot redirect root's metadata changes.
    app_dir = installed_app_dir()
    paths = []
    for name in ('var', 'etc'):
        binding = os.path.join(app_dir, name)
        info = os.lstat(binding)
        if info.st_uid != 0 and (stat.S_ISLNK(info.st_mode) or info.st_uid != user.pw_uid):
            raise PermissionError('untrusted data binding: ' + name)
        path = os.path.realpath(binding)
        trusted_code(os.path.dirname(path))
        fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
        try:
            restore_data(fd, user.pw_uid, group.gr_gid)
        finally:
            os.close(fd)
        paths.append(path)
    # Verify access with exactly the identity the service will use, including no groups.
    pid = os.fork()
    if pid == 0:
        try:
            os.setgroups([])
            os.setgid(group.gr_gid)
            os.setuid(user.pw_uid)
            for path in paths:
                for directory, _, files in os.walk(path):
                    if not os.access(directory, os.R_OK | os.W_OK | os.X_OK):
                        raise PermissionError('package data directory is inaccessible')
                    for name in files:
                        if not os.access(os.path.join(directory, name), os.R_OK | os.W_OK):
                            raise PermissionError('package data file is inaccessible: ' + name)
            with open(os.path.join(paths[0], 'info.log'), 'a'):
                pass
        except Exception as exc:
            print('package access check failed: %s' % exc, file=sys.stderr, flush=True)
            os._exit(1)
        os._exit(0)
    if os.waitpid(pid, 0)[1] != 0:
        raise PermissionError('package access check failed')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--user', required=True)
    ap.add_argument('--group', required=True)
    ap.add_argument('--prepare', action='store_true')
    ap.add_argument('--server')
    ap.add_argument('--python')
    ap.add_argument('--log')
    ap.add_argument('--limit', type=int)
    args = ap.parse_args()
    if os.getuid() != 0:
        raise PermissionError('root lifecycle required')
    trusted_code(__file__)
    trusted_code(sys.executable)
    user = pwd.getpwnam(args.user)
    group = grp.getgrnam(args.group)
    if not user.pw_uid or not group.gr_gid:
        raise ValueError('invalid package identity')
    if args.prepare:
        prepare_data(user, group)
        print('private data ownership and package access verified')
        return
    if not all((args.server, args.python, args.log)) or (args.limit is not None and args.limit < 0):
        raise ValueError('invalid service arguments or container budget')
    parent, child = socket.socketpair()
    child_pid = os.fork()
    if child_pid == 0:
        parent.close()
        try:
            reader(child, args.limit, os.getppid())
        finally:
            os._exit(0)
    child.close()
    try:
        os.setgroups([])
        os.setgid(group.gr_gid)
        os.setuid(user.pw_uid)
        if os.getuid() == 0 or os.getgid() == 0:
            raise PermissionError('privilege drop failed')
        # Open a package-controlled log only AFTER the permanent privilege drop.
        with open(args.log, 'a') as log:
            os.dup2(log.fileno(), 1)
            os.dup2(log.fileno(), 2)
        parent.set_inheritable(True)
        os.environ['NSC_DOCKER_READER_FD'] = str(parent.fileno())
        os.environ['NSC_DOCKER_READER_PID'] = str(child_pid)
        os.execv(args.python, [args.python, args.server, '--serve'])
    finally:
        parent.close()  # failed exec/drop: EOF makes the reader exit without extra privileges
        os.waitpid(child_pid, 0)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('startup failed: %s: %s' % (type(exc).__name__, exc), file=sys.stderr, flush=True)
        sys.exit(1)
