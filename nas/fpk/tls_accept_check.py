#!/usr/bin/env python3
"""审计 H12：accept 线程不能被"连上不说话 / 握手打一半"的客户端钉死。

1.2.5 里 `recv(1, MSG_PEEK)`（默认无限等）和 `wrap_socket`（10 秒）都在 accept
线程里做，于是几个半开连接就能把整个服务的入口堵住：板子的遥测请求排在后面积压，
屏上数据停更，服务日志里一个字都没有（表现为"NAS 那边好好的，板子就是不刷新"）。

这条检查必须能证伪，所以它做四件事，全部按**生产值**跑（不 monkeypatch 超时——
上一版把 3.0 改成 0.5 再断言"很快返回"，那是在验证测试自己，不是在验证服务）：

  1. 生产常量核对：`TLS_PEEK_TIMEOUT_S` / `TLS_HANDSHAKE_TIMEOUT_S` 仍是 3.0 / 10.0，
     且**用这两个值**等着看连接是怎么被关掉的；
  2. 静默 + 半握手各来一批，同时打普通 HTTP（明文兼容路径，板子首次取证书走的
     就是它）和真 TLS 请求，两者都必须在很短时间内拿到 200；
  3. 关闭语义要分清：静默连接在 peek 超时（3 秒）后被服务端关掉、半握手连接在握手
     超时（10 秒）后被服务端关掉，两者的**关闭时刻**必须分别落在 3 秒 / 10 秒附近
     ——**客户端自己的 timeout 不算"被服务端关掉"**（closure() 会报 timeout，另有
     一条针对 closure() 本身的负控）；关闭类型只做记录：半握手时 wrap_socket 已经
     把那 4 个字节读进 SSL 缓冲，所以内核收包队列是空的，服务端关出来是干净 FIN
     （eof），不是 RST——RST 与否取决于缓冲，不能拿来当判据；
     另外把 probe 并发上限跑满，证明超限的连接是立刻被关而不是无限堆线程；
  4. 负控：把 1.2.5 的 get_request 原样装回去，同一套并发下普通请求必须拿不到响应
     ——不然这条测试根本没在测 H12。

openssl 不可用时返回 2（未判定），绝不假装通过。
"""
import importlib.util
from contextlib import ExitStack
import select
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler
from pathlib import Path

SERVER = Path(__file__).parent / "nasscreencompanion/app/server/nas_companion_server.py"
PEEK_S = 3.0            # 期望的生产值
HANDSHAKE_S = 10.0

FAILED = []


def check(name, cond, detail=""):
    print("%s %s%s" % ("✓" if cond else "✗", name, ("　—— " + detail) if detail else ""))
    if not cond:
        FAILED.append(name)
    return cond


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        body = b"ok"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def old_get_request(self):
    """1.2.5 的实现：recv/wrap_socket 都在 accept 线程里，没有任何超时。"""
    conn, addr = self.socket.accept()
    if self.ssl_context is None:
        return conn, addr
    head = conn.recv(1, socket.MSG_PEEK)
    if head[:1] == b"\x16":
        conn = self.ssl_context.wrap_socket(conn, server_side=True)
    return conn, addr


def start_server(module, ctx, impl=None):
    srv = module.ThreadedHTTPServer(("127.0.0.1", 0), Handler)
    if impl is not None:
        srv.get_request = impl.__get__(srv, type(srv))
    srv.ssl_context = ctx
    try:
        thread = threading.Thread(target=srv.serve_forever,
                                  kwargs={"poll_interval": 0.05}, daemon=True)
        srv._accept_test_thread = thread
        thread.start()
    except BaseException:
        srv.server_close()
        raise
    return srv


def stop_server(srv):
    # A legacy negative-control accept can block. Failure to stop is a failed
    # test, never a successful cleanup receipt from an abandoned daemon.
    errors = []
    def shutdown():
        try:
            srv.shutdown()
        except BaseException as exc:
            errors.append(exc)
    t = threading.Thread(target=shutdown, daemon=True)
    try:
        t.start()
        t.join(timeout=3.0)
        if t.is_alive():
            raise TimeoutError("测试监听器shutdown未完成")
        if errors:
            raise errors[0]
    finally:
        srv.server_close()
    accept_thread = getattr(srv, "_accept_test_thread", None)
    if accept_thread is not None:
        accept_thread.join(timeout=3.0)
        if accept_thread.is_alive():
            raise TimeoutError("测试accept线程未退出")


def connect(host, port):
    return socket.create_connection((host, port), timeout=5.0)


def http_get(host, port, use_tls, ctx=None, wait=6.0):
    """返回 (是否拿到 200, 用时)。明文走 peek 之后放行的兼容路径。"""
    t0 = time.monotonic()
    ok = False
    try:
        raw = connect(host, port)
    except OSError:
        return False, time.monotonic() - t0
    s = raw
    try:
        raw.settimeout(wait)
        if use_tls:
            s = ctx.wrap_socket(raw, server_hostname="127.0.0.1")
            s.settimeout(wait)
        s.sendall(b"GET /api/v1/health HTTP/1.0\r\nHost: x\r\n\r\n")
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = s.recv(256)
            if not chunk:
                break
            buf += chunk
            if len(buf) > 16384:
                return False, time.monotonic() - t0
        status = buf.split(b"\r\n", 1)[0].split()
        ok = (b"\r\n\r\n" in buf and len(status) >= 2
              and status[0].startswith(b"HTTP/") and status[1] == b"200")
    except OSError:
        ok = False
    finally:
        for owned in ((s,) if s is raw else (s, raw)):
            try:
                owned.close()
            except OSError:
                ok = False
    return ok, time.monotonic() - t0


def closure_many(socks, t0s, wait):
    """并发等一批连接被服务端关掉，返回 [(种类, 从 connect 算起的关闭时刻)]。

    串行 recv 分不出"3 秒 peek 超时"和"probe 上限立刻拒绝"：第 33 条连接可能 0.3 秒
    就被关了，但等前面 32 条各等 3 秒才轮到它，从 connect 算过去已经是 3.5 秒。用
    select 同时盯着所有连接的 fd，谁先关就记谁。
    """
    res = [None] * len(socks)
    pending = {}
    for i, sock in enumerate(socks):
        sock.settimeout(wait)
        pending[sock.fileno()] = (i, sock)
    deadline = time.monotonic() + wait
    while pending and time.monotonic() < deadline:
        ready, _, _ = select.select(list(pending.keys()), [], [], 0.05)
        for fd in ready:
            i, sock = pending.pop(fd)
            elapsed = time.monotonic() - t0s[i]
            try:
                data = sock.recv(16)
            except socket.timeout:
                res[i] = ("timeout", time.monotonic() - t0s[i])
                continue
            except OSError as exc:      # RST 一类
                res[i] = ("rst(%s)" % type(exc).__name__, elapsed)
                continue
            res[i] = ("eof" if data == b"" else "data(%r)" % data[:16], elapsed)
    for i, sock in enumerate(socks):
        if res[i] is None:
            res[i] = ("timeout", time.monotonic() - t0s[i])
    return res


def closure(sock, wait, t0=None):
    """等服务端关连接。返回 (种类, 用时)；'timeout' 表示服务端根本没关。

    用时默认从 connect 成功的时刻（t0）算起：几个连接是串行收的，从 recv 那一刻
    开始计时会把"早就被关掉、缓冲区里躺着 EOF"的连接算成 0.0s。
    """
    sock.settimeout(wait)
    if t0 is None:
        t0 = time.monotonic()
    try:
        data = sock.recv(16)
    except socket.timeout:
        return "timeout", time.monotonic() - t0
    except OSError as exc:          # RST：ConnectionResetError / ConnectionAbortedError…
        return "rst(%s)" % type(exc).__name__, time.monotonic() - t0
    if data == b"":
        return "eof", time.monotonic() - t0
    return "data(%r)" % data[:16], time.monotonic() - t0


def make_cert(dirpath):
    """真证书：这条检查要跑真握手，"假 ctx 直接抛异常"验不了成功路径。"""
    cert = Path(dirpath) / "cert.pem"
    key = Path(dirpath) / "key.pem"
    try:
        rc = subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
             "-subj", "/CN=127.0.0.1", "-keyout", str(key), "-out", str(cert)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20).returncode
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None, None
    if rc != 0:
        return None, None
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=str(cert), keyfile=str(key))
    return ctx, ctx


def client_ctx():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    return ctx


def main():
    FAILED.clear()
    if not SERVER.exists():
        print("✗ 找不到服务端源码：%s" % SERVER)
        return 1
    spec = importlib.util.spec_from_file_location("nas_companion_server", SERVER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    # 1) 生产值 + 静态断言：probe 不许回到 accept 线程里
    check("TLS_PEEK_TIMEOUT_S 是生产值 %.1fs" % PEEK_S,
          module.TLS_PEEK_TIMEOUT_S == PEEK_S, "实际 %s" % module.TLS_PEEK_TIMEOUT_S)
    check("TLS_HANDSHAKE_TIMEOUT_S 是生产值 %.1fs" % HANDSHAKE_S,
          module.TLS_HANDSHAKE_TIMEOUT_S == HANDSHAKE_S,
          "实际 %s" % module.TLS_HANDSHAKE_TIMEOUT_S)
    # 看字节码里的调用名，而不是 getsource：docstring 里就写着 recv/wrap_socket，
    # 拿源码字符串断言会把注释当成实现。
    names = set(module.ThreadedHTTPServer.get_request.__code__.co_names)
    check("accept 线程里没有 recv/wrap_socket（probe 已挪到连接线程）",
          "recv" not in names and "wrap_socket" not in names,
          "get_request 实际调用的名字：%s" % (sorted(names) or "（空）"))
    check("probe 有并发上限（超限关连接，不是无限堆线程）",
          getattr(module.ThreadedHTTPServer, "probe_limit", 0) >= 8,
          "probe_limit=%s" % getattr(module.ThreadedHTTPServer, "probe_limit", None))

    with tempfile.TemporaryDirectory() as tmp, ExitStack() as resources:
        server_ctx, _ = make_cert(tmp)
        if server_ctx is None or not _:
            print("! 本机没有可用的 openssl，真握手测不了 —— 未判定（不算通过）")
            return 1 if FAILED else 2
        cctx = client_ctx()
        def tracked_connect(host, port):
            return resources.enter_context(connect(host, port))
        def tracked_server(impl=None):
            server = start_server(module, server_ctx, impl=impl)
            resources.callback(stop_server, server)
            return server

        # 2) 静默 + 半握手一批，普通请求（明文 / 真 TLS）都不该被拖慢
        srv = tracked_server()
        host, port = srv.server_address
        silent, t_silent = [], []
        for _ in range(3):
            silent.append(tracked_connect(host, port))
            t_silent.append(time.monotonic())
        half, t_half = [], []
        for _ in range(3):
            s = tracked_connect(host, port)
            t_half.append(time.monotonic())
            s.sendall(b"\x16\x03\x01\x00\x50")       # 一个说了半句的 ClientHello
            half.append(s)
        time.sleep(0.3)
        ok_plain, t_plain = http_get(host, port, use_tls=False)
        ok_tls, t_tls = http_get(host, port, use_tls=True, ctx=cctx)
        check("半开连接占着时，明文请求仍拿到 200", ok_plain, "%.3fs" % t_plain)
        check("半开连接占着时，真 TLS 握手请求仍拿到 200", ok_tls, "%.3fs" % t_tls)
        check("明文请求没有被拖到秒级", t_plain < 1.0, "%.3fs" % t_plain)
        check("TLS 请求没有被拖到秒级", t_tls < 2.0, "%.3fs" % t_tls)

        # 3a) 静默连接：peek 超时（3 秒）之后被服务端关掉，且不能是"客户端自己等超时"。
        #     用时从 connect 算起（t0），不是从 recv 算起。
        sil = closure_many(silent, t_silent, wait=8.0)
        kinds = sorted(set(k.split("(")[0] for k, _ in sil))
        times = [t for _, t in sil]
        check("静默连接被服务端关掉（不是客户端超时）", "timeout" not in kinds,
              "%s，%.1f~%.1fs" % ("/".join(kinds), min(times), max(times)))
        check("静默连接的关闭时刻对得上 peek 超时 %.1fs" % PEEK_S,
              all(PEEK_S - 1.0 <= t <= PEEK_S + 3.0 for _, t in sil),
              "%.1f~%.1fs" % (min(times), max(times)))
        check("静默连接（缓冲区为空）是 EOF 关掉的", "eof" in kinds, "/".join(kinds))

        # 3b) 半握手连接：握手超时（10 秒）之后被关掉，缓冲区里有没读的数据 → RST。
        hal = closure_many(half, t_half, wait=14.0)
        hkinds = sorted(set(k.split("(")[0] for k, _ in hal))
        htimes = [t for _, t in hal]
        check("半握手连接被服务端关掉（不是客户端超时）", "timeout" not in hkinds,
              "%s，%.1f~%.1fs" % ("/".join(hkinds), min(htimes), max(htimes)))
        check("半握手连接的关闭时刻对得上握手超时 %.1fs" % HANDSHAKE_S,
              all(HANDSHAKE_S - 1.5 <= t <= HANDSHAKE_S + 3.0 for _, t in hal),
              "%.1f~%.1fs" % (min(htimes), max(htimes)))
        check("半握手连接是被真实关闭的（EOF/RST），不是客户端超时",
              all(k in ("eof", "rst") for k in hkinds), "/".join(hkinds))

        # 3c) 握手失败后服务还活着：说 0x16 但内容是垃圾
        bad = tracked_connect(host, port)
        bad.sendall(b"\x16\x03\x01\x10\x00" + b"\x00" * 16)
        badkind, badt = closure(bad, wait=13.0)
        ok_after, t_after = http_get(host, port, use_tls=True, ctx=cctx)
        check("坏 TLS 客户端被关掉、服务照常答题", ok_after and badkind != "timeout",
              "坏连接=%s/%.1fs，之后 TLS 请求 %s/%.3fs" % (badkind, badt, ok_after, t_after))
        stop_server(srv)

        # 3d) probe 并发上限：把上限跑满再多来一批，多出来的必须**立刻**被关
        srv = tracked_server()
        host, port = srv.server_address
        limit = module.ThreadedHTTPServer.probe_limit
        many, t_many = [], []
        for _ in range(limit + 8):
            many.append(tracked_connect(host, port))
            t_many.append(time.monotonic())
        time.sleep(0.3)
        res = closure_many(many, t_many, wait=8.0)
        fast = [t for k, t in res if k != "timeout" and t < 2.0]
        slow = [t for _, t in res if t >= 2.0]
        # 多出来的 8 条（limit+8 减去 limit 个槽位）必须立刻被关，而不是排到 peek 超时。
        check("超出 probe 上限的连接被立刻关掉（而不是无限堆线程）",
              len(fast) >= 8 and "timeout" not in [k for k, _ in res],
              "上限 %d，%d 条在 2 秒内被关（%.2f~%.2fs），另外 %d 条走完了 peek 超时"
              % (limit, len(fast), min(fast) if fast else -1, max(fast) if fast else -1,
                 len(slow)))
        stop_server(srv)

        # 3e) closure() 自己的负控：一个只 listen、永不关闭的"服务端"必须让 closure()
        #     报 timeout。不然上面所有"被服务端关掉了"的判定都不成立。
        bare = resources.enter_context(socket.socket())
        bare.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        bare.bind(("127.0.0.1", 0))
        bare.listen(1)
        c_nc = tracked_connect(*bare.getsockname())
        kind_nc, _ = closure(c_nc, wait=0.6)
        check("负控：服务端不关连接时 closure() 报超时（不会把'没关'当成'关了'）",
              kind_nc == "timeout", "种类=%s" % kind_nc)
        c_nc.close()
        bare.close()

        # 4) 负控：把 1.2.5 的 get_request 装回去，同一个场景必须挂
        srv = tracked_server(impl=old_get_request)
        host, port = srv.server_address
        stuck = [tracked_connect(host, port) for _ in range(3)]
        time.sleep(0.3)
        ok_old, t_old = http_get(host, port, use_tls=False, wait=2.5)
        check("负控：1.2.5 的实现下，静默连接会把后到的请求挡在门外", not ok_old,
              "%.1fs 内拿到响应=%s" % (t_old, ok_old))
        for s in stuck:
            s.close()
        stop_server(srv)

    if FAILED:
        print("✗ %d 项没通过：%s" % (len(FAILED), "、".join(FAILED)))
        return 1
    print("PASS: accept 线程只做 accept；probe 有超时与并发上限，半开连接既不堵服务、"
          "也不会被误判成'已关闭'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
