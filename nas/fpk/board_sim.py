#!/usr/bin/env python3
"""开发板模拟器：把板子配对时的**动作序列**原样走一遍，验证 NAS 侧真的配合。

为什么要有它：固件那一半没法在主机上跑（要真的 ESP32-P4），所以"板子上配对到底
成不成"在真机之前是空白。但板子做的事其实只有五步，全都是标准 HTTP/TLS，可以在
主机上一步步复现——这样在把 `.fpk` 装到 NAS 上之后，**不用等板子**就能先把 NAS
那一半验通：

  1. 明文 GET /api/v1/identity      —— 建 TLS 之前先拿证书（板子此刻还不知道信谁）
  2. 自己从 cert_pem 算 SHA-256      —— 板子只信自己算出来的指纹，不信服务器给的字段
  3. 用这张证书做**固定**建 TLS       —— 不是 --cacert 到系统信任库，是"只认这一张"
  4. POST /api/v1/pair 换令牌
  5. 带令牌 GET /api/v1/status       —— 确认这条链路真的能取到数据

外加三条**否定断言**（不通过就说明安全属性没落地）：
  - 固定一张**别的**证书时必须握手失败（证明固定是真在校验，不是摆设）
  - 明文直接读 /api/v1/status 必须 403（引导通道只放行 identity）
  - 没有令牌读 /api/v1/status 必须 401

用法：
    # 对着本机测试实例（测试套件里就是这么调的）
    python3 nas/fpk/board_sim.py --host 127.0.0.1 --port 8797 --pair-code 123456

    # 对着真 NAS（先在管理页「设备配对」生成配对码）
    python3 nas/fpk/board_sim.py --host <NAS_IP> --port 8798 --pair-code 123456

    # 配对码也可以让脚本自己去管理面 Socket 取（只在能访问 Socket 的机器上可用）
    python3 nas/fpk/board_sim.py --host 127.0.0.1 --port 8797 --socket /var/apps/nasscreencompanion/target/app.sock

退出码非 0 表示有断言没过。
"""

import argparse
import base64
import hashlib
import http.client  # noqa: F401  （req() 内部用）
import json
import os
import socket
import ssl
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

PASS, FAIL = 0, 0


def ok(msg):
    global PASS
    PASS += 1
    print(f"  \033[32m✓\033[0m {msg}")


def bad(msg):
    global FAIL
    FAIL += 1
    print(f"  \033[31m✗\033[0m {msg}")


def check(cond, msg, detail=""):
    (ok if cond else bad)(msg if cond or not detail else f"{msg}（{detail}）")


def jload(data, what):
    """把响应体当 JSON 读；读不出来时给一句人能看的话，而不是 traceback。

    审计 M41：服务端 500 时 body 是 HTML 错误页，裸 json.loads 会甩出
    "Expecting value: line 1 column 1" 的调用栈，把整场验收淹掉 —— 看起来
    像脚本坏了，实际是"被验收的那一端返回了非 JSON"。
    """
    try:
        value = json.loads(data)
        if not isinstance(value, dict):
            raise ValueError("object required")
        return value
    except (ValueError, TypeError):
        bad(f"{what} 的响应不是有效 JSON 对象")
        return None


def pem_fingerprint(pem):
    """与固件 fnos_pair.c 的 pem_fingerprint() 同一套算法：取 PEM 第一段证书的
    base64 正文，解码后算 SHA-256，输出大写冒号分隔。**不读服务器给的
    tls_fingerprint 字段** —— 板子只信自己算的，这里也一样。"""
    body = []
    in_body = False
    for line in pem.splitlines():
        if line.startswith("-----BEGIN"):
            in_body = True
            continue
        if line.startswith("-----END"):
            break
        if in_body:
            body.append(line.strip())
    der = base64.b64decode("".join(body))
    digest = hashlib.sha256(der).hexdigest().upper()
    return ":".join(digest[i:i + 2] for i in range(0, len(digest), 2))


def pinned_ctx(pem):
    """只认这一张证书：不用系统信任库，也不校验主机名。
    板子是同样的取舍——信任锚点是用户当面比对过的那张证书，不是名字。"""
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_REQUIRED
    ctx.load_verify_locations(cadata=pem)
    return ctx


def req(host, port, path, ctx=None, token=None, method="GET", body=None, timeout=5):
    conn = (http.client.HTTPSConnection(host, port, context=ctx, timeout=timeout)
            if ctx else http.client.HTTPConnection(host, port, timeout=timeout))
    headers = {}
    if token:
        headers["Authorization"] = "Bearer " + token
    if body is not None:
        headers["Content-Type"] = "application/json"
    try:
        conn.request(method, path, body=body, headers=headers)
        r = conn.getresponse()
        data = r.read(16 * 1024 * 1024 + 1)
        if len(data) > 16 * 1024 * 1024:
            raise ValueError("response exceeds simulator budget")
        return r.status, data
    finally:
        conn.close()


def make_impostor_cert(dirpath):
    """造一张"别人的"自签证书，用来证明固定真的在校验。"""
    crt = os.path.join(dirpath, "impostor.crt")
    key = os.path.join(dirpath, "impostor.key")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", key, "-out", crt, "-days", "1",
                    "-subj", "/CN=definitely-not-the-nas"],
                   check=True, capture_output=True)
    with open(crt, encoding="utf-8") as f:
        return f.read()


def code_from_socket(path):
    """走管理面 Socket 取一个配对码（与测试套件里 admin() 等价）。"""
    import http.client as hc
    class UnixConn(hc.HTTPConnection):
        def connect(self):
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(path)
    c = UnixConn("x", timeout=5)
    try:
        c.request("POST", "/app/nasscreencompanion/api/pairing/new", body="{}",
                  headers={"X-Trim-Userid": "1000", "X-Trim-Isadmin": "1",
                           "Content-Type": "application/json"})
        r = c.getresponse()
        raw = r.read(1024 * 1024 + 1)
        if len(raw)>1024*1024:
            raise ValueError("pairing-code response exceeds budget")
        data = jload(raw, "管理面配对码")
        code = data.get("code") if data else None
        if r.status != 200 or not isinstance(code, str) or len(code)!=6 or any(c not in "0123456789" for c in code):
            raise ValueError("invalid pairing-code response")
        return code
    finally:
        c.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--pair-code")
    ap.add_argument("--socket", help="管理面 Socket 路径，用来自动取配对码")
    ap.add_argument("--device-name", default="p4-7b-lcd")
    a = ap.parse_args()

    print(f"== 对着 {a.host}:{a.port} 走一遍板子的配对序列 ==")

    # ── 1. 明文取证书（板子此刻建不了 TLS：esp-tls 没有任何 CA 来源时会直接拒连）
    try:
        status, data = req(a.host, a.port, "/api/v1/identity")
    except OSError as e:
        bad(f"明文取 identity 失败：{e}")
        return 1
    check(status == 200, "① 明文 GET /api/v1/identity 返回 200", f"实际 {status}")
    ident = jload(data, "① GET /api/v1/identity")
    if ident is None:
        return 1
    check("cert_pem" in ident and ident["cert_pem"], "① 响应里带 cert_pem")
    tls_on = bool(ident.get("tls"))
    check("uid" not in ident and "auth_mode" not in ident,
          "① 明文响应已裁剪（不含 uid/auth_mode）",
          f"实际字段 {sorted(ident)}")

    if not tls_on:
        bad("NAS 未启用 TLS：未验证 TLS 固定，配对验收未完成")
        return 3
    else:
        pem = ident.get("cert_pem")
        if not isinstance(pem,str) or not pem:
            bad("缺少有效证书，停止配对"); return 1

    # ── 2. 自己算指纹
    if pem:
        fp = pem_fingerprint(pem)
        if ident.get("tls_fingerprint"):
            ok(f"② 从 cert_pem 自算指纹 {fp[:23]}…")
            check(fp == ident["tls_fingerprint"].upper(),
                  "② 自算指纹与 NAS 报告的指纹一致（不一致用户会看到两张不同的表）",
                  f"自算 {fp} vs 报告 {ident['tls_fingerprint']}")
        else:
            # 审计 H16：以前这里是**无条件**打 ✓，可 NAS 不报指纹时根本没有"对过"这回事，
            # 板子上那一步只能靠用户肉眼比 —— 验收脚本不能说它过了。
            bad("② NAS 没报 tls_fingerprint：自算的指纹无从对照，指纹固定未验收")
    else:
        bad("② /api/v1/identity 没给 cert_pem：没东西可固定，配对验收未完成")

    ctx = pinned_ctx(pem) if pem else None

    # ── 3. 用固定的证书建 TLS
    if ctx:
        try:
            status, _ = req(a.host, a.port, "/api/v1/identity", ctx=ctx)
            check(status == 200, "③ 固定证书后 TLS 握手成功")
        except ssl.SSLError as e:
            bad(f"③ 固定证书后 TLS 握手失败：{e}")
            return 1
        # 否定断言：固定别的证书必须失败，否则"固定"是假的。
        # 光看"失败了"不够 —— 服务器宕机、端口写错也会失败，那样这条断言就是空的。
        # 所以配一个**对照**：同一张错误证书、但关掉校验时必须连得上。
        # 两个一起看才说明"失败是因为校验"，而不是因为连不通。
        with tempfile.TemporaryDirectory() as d:
            wrong = make_impostor_cert(d)
            try:
                req(a.host, a.port, "/api/v1/identity", ctx=pinned_ctx(wrong))
                bad("③ 固定一张别的证书竟然也连上了 —— 固定没有生效")
            except ssl.SSLError:
                ok("③ 固定一张别的证书时握手失败（固定确实在校验）")
            try:
                lax = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
                lax.check_hostname = False
                lax.verify_mode = ssl.CERT_NONE
                st_control, _ = req(a.host, a.port, "/api/v1/identity", ctx=lax)
                check(st_control == 200,
                      "③ 对照：同样这条连接不校验时是通的（说明上面的失败来自校验）",
                      f"实际 {st_control}")
            except OSError as e:
                bad(f"③ 对照连接也不通，上面的断言可能是假阳性：{e}")

    # ── 否定断言：明文不许读别的端点
    try:
        status, _ = req(a.host, a.port, "/api/v1/status")
        if tls_on:
            check(status == 403, "③ 明文读 /api/v1/status 被拒（403）", f"实际 {status}")
        else:
            check(status in (200, 401), "③ 未开 TLS 时明文可读（由令牌把关）", f"实际 {status}")
    except OSError as e:
        bad(f"③ 明文读 status 出错：{e}")

    # Trust and negative assertions must pass before this simulator creates a device.
    if FAIL:
        return 1

    # ── 4. 配对换令牌
    code = a.pair_code
    if not code and a.socket:
        code = code_from_socket(a.socket)
        print("  （已从管理面 Socket 取得配对码）")
    if not code:
        bad("④ 没有配对码（用 --pair-code 或 --socket）")
        return 1
    if not isinstance(code,str) or len(code)!=6 or any(c not in "0123456789" for c in code):
        bad("④ 配对码必须为六位ASCII数字"); return 1
    body = json.dumps({"code": code, "name": a.device_name})
    try:
        status, data = req(a.host, a.port, "/api/v1/pair", ctx=ctx, method="POST", body=body)
    except ssl.SSLError as e:
        bad(f"④ 配对请求握手失败：{e}")
        return 1
    reply = jload(data, "④ POST /api/v1/pair")
    if reply is None:
        return 1
    token = reply.get("token", "")
    if not isinstance(token,str): token = ""
    check(status == 200 and token, "④ 配对换到令牌", f"HTTP {status}，令牌存在={bool(token)}")

    if status != 200 or not token:
        return 1

    # ── 否定断言：错误配对码必须被拒
    try:
        status2, _ = req(a.host, a.port, "/api/v1/pair", ctx=ctx, method="POST",
                          body=json.dumps({"code": "invalid", "name": "x"}))
        check(status2 in (400, 403), "④ 错误配对码被拒", f"实际 {status2}")
    except ssl.SSLError as e:
        bad(f"④ 错误配对码请求握手失败，未验证拒绝行为：{e}")

    if not token:
        return 1

    # ── 5. 带令牌取数据
    try:
        status, _ = req(a.host, a.port, "/api/v1/status", ctx=ctx)
        check(status == 401, "⑤ 不带令牌读 status 被拒（401）", f"实际 {status}")
        status, data = req(a.host, a.port, "/api/v1/status", ctx=ctx, token=token)
        st = jload(data, "⑤ GET /api/v1/status")
        if st is None: return 1
        check(status == 200 and st.get("ready") is not None,
              "⑤ 带令牌读 status 成功且 ready 字段在", f"HTTP {status}")
        status, _ = req(a.host, a.port, "/api/v1/history", ctx=ctx, token=token)
        check(status == 200, "⑤ 带令牌读 history 成功", f"实际 {status}")
    except (OSError, ValueError) as e:
        bad(f"⑤ 取数据失败：{e}")

    # ── 6. 拿真实响应核对"板子读的每个字段"
    #     这是方案里 P0 那一步的落地：主机上 24/49 个字段只能对着采集端源码核
    #     （macOS 没有 /proc、/sys），真 NAS 上 49 个字段会**全部**走真实响应。
    #     直接把刚取到的这一帧交给 contract_check.py，不另写一套比对。
    if token:
        try:
            status, data = req(a.host, a.port, "/api/v1/status", ctx=ctx, token=token)
            if status == 200 and data:
                # （tempfile 在模块顶部已经 import；这里再写一次会让它变成函数局部名，
                #   上面早先那段 with tempfile.TemporaryDirectory() 就会 UnboundLocalError）
                with tempfile.TemporaryDirectory(prefix="fnos-board-contract-") as work:
                    tmp = os.path.join(work,"status.json")
                    with open(tmp,"wb") as f:
                        f.write(data)
                    checker = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                           "contract_check.py")
                    r = subprocess.run([sys.executable, checker, "--status", tmp],
                                       capture_output=True, text=True, timeout=60)
                    tail = [l for l in r.stdout.strip().splitlines() if l.strip()]
                    if r.returncode == 0:
                        verdict = next((l for l in reversed(tail) if "判定" in l or "容量上限" in l), "")
                        ok("响应字段契约核对通过" + (f"（{verdict}）" if verdict else ""))
                    else:
                        bad("字段契约检查失败；详见本地主机契约检查")

            else:
                bad(f"字段契约缺少成功响应：HTTP {status}")
        except (OSError, ValueError, subprocess.TimeoutExpired) as e:
            bad(f"字段契约核对失败：{type(e).__name__}")

    print(f"\n结果：{PASS} 项通过，{FAIL} 项失败")
    if FAIL == 0:
        print("这台 NAS 的配对链路、TLS 固定、字段契约都通了。"
              "下一步：在板子上输入配对码，逐段比对指纹。")
    return 1 if FAIL else 0


if __name__ == "__main__":
    try:
        result = main()
    except (OSError, ValueError, TypeError, KeyError, subprocess.SubprocessError) as e:
        bad(f"模拟器未完成：{type(e).__name__}")
        result = 1
    sys.exit(result)
