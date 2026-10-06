#!/usr/bin/env python3
"""开着 Python 开发模式跑一遍服务端，看它有没有在偷偷报怨。

为什么值得单独查一次：**静态检查和断言都看不见"没说出口的问题"**。开发模式
（`-X dev`）会把 `ResourceWarning`、弃用警告、以及别的默认被静默吞掉的信号打出来
——而这一类东西正好是长跑才会发作的那类：一个没关的 socket、一个没 close 的文件，
单次请求看不出任何异常，跑一天就是句柄耗尽。

这个脚本：把包按真机布局摆进临时目录 → 用 `python3 -X dev` 起服务 → 对着管理面
（Unix Socket）、TLS 面、明文引导面各打若干次 → 检查服务端输出里有没有 `Warning`。

用法：python3 nas/fpk/devmode_check.py [轮数]
退出码非 0 表示跑出了警告，或者服务中途死了。
"""

import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PKG = os.path.join(HERE, "nasscreencompanion")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def main():
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    pkg = DEFAULT_PKG
    if not os.path.isdir(pkg):
        print(f"找不到 {pkg}")
        return 2

    work = tempfile.mkdtemp(prefix="nsc-devmode-")
    stage = os.path.join(work, "target")
    var = os.path.join(work, "var")
    etc = os.path.join(work, "etc")
    os.makedirs(var)
    os.makedirs(etc)
    shutil.copytree(os.path.join(pkg, "app"), stage)
    shutil.copytree(os.path.join(pkg, "cmd"), os.path.join(work, "cmd"))

    port = free_port()
    env = dict(os.environ)
    # **向导字段名是小写的 `wizard_*`**，不是 `W_*`：`norm_wizard()` 会从
    # `${wizard_port:-…}` 重新算出 W_PORT/W_BIND/…，直接把 W_PORT 塞进环境里
    # 会被它覆盖掉——服务于是去监听默认的 8798，然后报"端口已被占用"退出。
    # （第一版就是这么写的，检查只会说"服务端中途退出了"，看不出真正原因。）
    env.update(TRIM_APPDEST=stage, TRIM_PKGVAR=var, TRIM_PKGETC=etc,
               TRIM_APPNAME="nasscreencompanion",
               wizard_port=str(port), wizard_bind="local", wizard_auth="pairing",
               wizard_docker="false", wizard_interval="0.2", wizard_tls="true",
               PYTHONDONTWRITEBYTECODE="1")
    subprocess.run([os.path.join(work, "cmd", "install_callback")], env=env,
                   capture_output=True)

    log = os.path.join(work, "server.log")
    srv = subprocess.Popen([sys.executable, "-X", "dev",
                            os.path.join(stage, "server", "nas_companion_server.py"),
                            "--serve"], env=env,
                           stdout=open(log, "w"), stderr=subprocess.STDOUT)
    try:
        sock = os.path.join(stage, "app.sock")
        for _ in range(50):
            if os.path.exists(sock):
                break
            time.sleep(0.2)
        time.sleep(1.5)

        import http.client

        class UnixConn(http.client.HTTPConnection):
            def connect(self):
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(sock)

        for i in range(rounds):
            try:
                c = UnixConn("x")
                c.request("GET", "/app/nasscreencompanion/api/state",
                          headers={"X-Trim-Userid": "1000", "X-Trim-Isadmin": "1"})
                c.getresponse().read()
                c.close()
            except OSError:
                pass
            for url in (f"https://127.0.0.1:{port}/api/v1/identity",
                        f"http://127.0.0.1:{port}/api/v1/identity"):
                try:
                    import ssl
                    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
                    ctx.check_hostname = False
                    ctx.verify_mode = ssl.CERT_NONE
                    urllib.request.urlopen(url, timeout=3,
                                           context=ctx if url.startswith("https") else None).read()
                except (urllib.error.URLError, OSError, ValueError):
                    pass
        time.sleep(1)
        alive = srv.poll() is None
    finally:
        srv.send_signal(signal.SIGTERM)
        try:
            srv.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv.kill()

    out = open(log, encoding="utf-8", errors="replace").read()
    warns = [l for l in out.splitlines() if "Warning" in l]
    shutil.rmtree(work, ignore_errors=True)

    print(f"打了 {rounds} 轮（管理面 + TLS 面 + 明文引导面）")
    if not alive:
        print("✗ 服务端中途退出了。它最后说的话：")
        for l in out.strip().splitlines()[-6:]:
            print("   ", l[:160])
        return 1
    if warns:
        print(f"✗ 开发模式下报了 {len(warns)} 条警告（长跑时这类东西会累积）：")
        for w in warns[:8]:
            print("   ", w.strip()[:160])
        return 1
    print("✓ 开发模式下零警告（没有未关闭的 socket/文件、没有弃用调用）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
