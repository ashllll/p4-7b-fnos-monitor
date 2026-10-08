#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NAS 屏幕伴侣 —— 飞牛应用中心版后端服务（nasscreencompanion）。

一个进程、两个对外面，权限模型完全不同，因此物理隔离：

  1. 管理面（Unix Socket，默认 ${TRIM_APPDEST}/app.sock）
     对接飞牛统一网关。浏览器从 fnOS 桌面点开应用后，网关校验登录态再把
     请求转发到这个 Socket，并带上 X-Trim-Userid / X-Trim-Isadmin /
     X-Trim-Username。Socket 权限 0600，只有网关（root）能连，
     所以"能连上"本身已等价于"经过了网关鉴权"。
     网关会带前缀 /app/nasscreencompanion，这里做前缀归一化。

  2. 遥测面（TCP，默认 0.0.0.0:8798）
     给 ESP32-P4 开发板轮询。严格只读，不含任何管理动作，
     默认要求配对令牌（auth_mode=pairing）。

刻意不做的事：
  * 不监听、不探测、不改动 8799 —— 那是开发版采集器（SSH + systemd）的端口，
    两者可以并存，本服务遇到端口冲突只会报错退出，绝不 kill 别人的进程；
  * 不写 /usr/local/bin、不装 systemd 单元、不需要 NAS 管理员密码；
  * 采集层只读 /proc、/sys、statvfs 与 docker.sock —— Docker Socket 默认关闭。

用法：
  python3 nas_companion_server.py --serve
  python3 nas_companion_server.py --check-port 8798     # 端口冲突检测（给 cmd/install_init 用）
  python3 nas_companion_server.py --selftest            # 不起服务，自检配置与采集
"""

import argparse
import base64
import errno
import hashlib
import hmac
import json
import os
import secrets
import shutil
import socket
import socketserver
import ssl
import subprocess
import sys
import threading
import time
import urllib.parse
from email.utils import parsedate_to_datetime
from http.server import BaseHTTPRequestHandler, HTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import docker_api  # Reap the inherited reader even while collection is disabled.

from fnos_collector import (                       # noqa: E402
    CompanionCollector, MODULE_ORDER, PROTO_VERSION, ST_OK, ST_STALE,
    _current_user, probe_capabilities,
)

APPNAME = "nasscreencompanion"
APPVER = os.environ.get("TRIM_APPVER") or "1.2.3"   # manifest 里的 version 由应用中心经 TRIM_APPVER 注入；这个默认值给开发模式兜底
GATEWAY_PREFIX = "/app/" + APPNAME
SOCKET_NAME = "app.sock"                 # 必须位于 target 目录（网关要求）
LOG_TAIL = 400                           # 内存里保留的日志行数（供管理页看）
class ConfigError(Exception):
    """配置校验没过。

    以前这里是把错误信息塞进 `_apply_config()` 的第一个返回值（也就是 `changed`），
    接口再回 `{"ok": true, "changed": ["hist_len 必须在 30~3600 之间"]}`——**前端拿到的
    是一次"成功保存"**，管理页会把它当成字段名显示成「已保存：hist_len 必须在 30~3600
    之间」，而设置根本没变。校验失败必须走 error 那条路。
    """


PAIR_ATTEMPT_LIMIT = 5                   # 每 IP 每窗口的配对尝试次数
PAIR_ATTEMPT_WINDOW = 300.0

DEFAULT_CONFIG = {
    "bind": "0.0.0.0",
    "port": 8798,
    "interval": 1.0,
    "hist_len": 300,
    "docker_enabled": False,             # Docker Socket 不是只读权限，默认关
    "tls_enabled": True,                 # 遥测面默认 HTTPS（方案要求，不允许静默明文）
    "auth_mode": "pairing",              # pairing | lan
    "devices": [],                       # [{name, sha256, last4, added_at, last_seen, last_ip}]
}


# ---------------------------------------------------------------- 路径

def app_dest():
    """target 目录：网关要求的 app.sock 必须放这里。"""
    return os.environ.get("TRIM_APPDEST") or os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def pkg_var():
    """运行数据目录（fd -> @appdata）。"""
    return os.environ.get("TRIM_PKGVAR") or os.path.join(app_dest(), "var")


def pkg_etc():
    return os.environ.get("TRIM_PKGETC") or os.path.join(app_dest(), "etc")


def cfg_path():
    return os.path.join(pkg_var(), "config.json")


def socket_path():
    return os.path.join(app_dest(), SOCKET_NAME)


# ---------------------------------------------------------------- TLS
#
# 遥测面默认走 HTTPS：开发板拿到的是"这台 NAS 的整体负载画像"，明文广播在
# 局域网上不合适。TLS 材料和配对令牌是两件事——令牌证明"这台板子被允许读"，
# 证书指纹证明"对面确实是那台 NAS"，两者都做。
#
# 自签证书在首次启动时生成，指纹由管理页展示，配对时由用户确认后固定在板子里；
# 板子不做 CA 链校验（局域网没有公共域名），只认这一枚指纹。校验没有被关掉，
# 只是信任的锚点从公共 CA 换成了用户当面确认过的那一枚证书。

TLS_DIRNAME = "tls"
CERT_DAYS = 3650                       # 十年：这是自签设备证书，不是公开站点证书


def tls_dir():
    return os.path.join(pkg_var(), TLS_DIRNAME)


def tls_cert_path():
    return os.path.join(tls_dir(), "server.crt")


def tls_key_path():
    return os.path.join(tls_dir(), "server.key")


def pem_fingerprint(path):
    """从 PEM 第一段证书直接算 SHA-256 指纹，返回 AA:BB:… 形式。

    刻意不调 openssl：这条路径要在"openssl 装不上/被裁掉"的机器上也说得清楚
    "证书到底长什么样"，而 DER 哈希用标准库就够了。
    """
    try:
        with open(path, "r", encoding="ascii", errors="replace") as f:
            body = f.read()
    except OSError:
        return None
    chunks = []
    inside = False
    for line in body.splitlines():
        if line.startswith("-----BEGIN CERTIFICATE"):
            inside = True
            continue
        if line.startswith("-----END CERTIFICATE"):
            break
        if inside:
            chunks.append(line.strip())
    if not chunks:
        return None
    try:
        der = base64.b64decode("".join(chunks))
    except Exception:                              # noqa: BLE001
        return None
    return ":".join("%02X" % b for b in hashlib.sha256(der).digest())


def _openssl_bin():
    for cand in ("/usr/bin/openssl", "/bin/openssl", "/usr/local/bin/openssl"):
        if os.path.exists(cand):
            return cand
    return shutil.which("openssl")


def read_cert_meta(path):
    """用 openssl 读 subject / notAfter。读不到就返回 (None, None, None)。

    读证书失败不是致命问题（指纹那条路不依赖 openssl），所以这里一律软失败。
    """
    exe = _openssl_bin()
    if not exe:
        return None, None, None
    try:
        proc = subprocess.run([exe, "x509", "-in", path, "-noout", "-subject", "-enddate"],
                              capture_output=True, text=True, timeout=15)
    except Exception:                              # noqa: BLE001
        return None, None, None
    if proc.returncode != 0:
        return None, None, None
    subject = not_after = None
    for line in (proc.stdout or "").splitlines():
        if line.startswith("subject="):
            subject = line[len("subject="):].strip()
        elif line.startswith("notAfter="):
            not_after = line[len("notAfter="):].strip()
    epoch = None
    if not_after:
        try:
            epoch = parsedate_to_datetime(not_after).timestamp()
        except Exception:                          # noqa: BLE001
            epoch = None
    return subject, not_after, epoch


def _local_ips():
    """本机 IPv4 列表，只用于写进证书 SAN，不发任何包。"""
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # 连一个不可路由的地址只为让内核挑出口网卡；UDP connect 不发包
            s.connect(("192.0.2.1", 9))
            ips.add(s.getsockname()[0])
        finally:
            s.close()
    except OSError:
        pass
    return sorted(ip for ip in ips if ip and ip != "0.0.0.0")


def generate_self_signed():
    """生成自签证书，返回 (ok, message)。"""
    exe = _openssl_bin()
    if not exe:
        return False, "系统里找不到 openssl，无法生成自签证书"
    try:
        os.makedirs(tls_dir(), exist_ok=True)
        os.chmod(tls_dir(), 0o700)
    except OSError as exc:
        return False, "创建 %s 失败：%s" % (tls_dir(), exc)

    host = (socket.gethostname() or "nas").strip() or "nas"
    cn = host.replace("/", "_").replace(" ", "_")
    san = ["DNS:%s" % cn, "DNS:localhost", "IP:127.0.0.1", "IP:::1"]
    san += ["IP:%s" % ip for ip in _local_ips()]
    base = [exe, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", tls_key_path(), "-out", tls_cert_path(),
            "-days", str(CERT_DAYS), "-subj", "/CN=%s" % cn]
    # -addext 需要 openssl 1.1.1+；老版本去掉它再试一次（没有 SAN 只影响浏览器
    # 和 curl 的域名校验，板子认的是指纹，不受影响）
    last = "openssl 生成失败"
    for cmd in (base + ["-addext", "subjectAltName=" + ",".join(san)], base):
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        except Exception as exc:                   # noqa: BLE001
            last = "%s: %s" % (type(exc).__name__, exc)
            continue
        if proc.returncode == 0 and os.path.exists(tls_cert_path()) and os.path.exists(tls_key_path()):
            try:
                os.chmod(tls_key_path(), 0o600)
                os.chmod(tls_cert_path(), 0o644)
            except OSError:
                pass
            return True, "已生成自签证书（CN=%s，%d 天，SAN=%s）" % (cn, CERT_DAYS, ",".join(san))
        tail = [ln for ln in (proc.stderr or proc.stdout or "").strip().splitlines() if ln.strip()]
        last = tail[-1].strip() if tail else "openssl 退出码 %d" % proc.returncode
    return False, last


def prepare_tls(regen=False):
    """确保证书可用，返回一份状态字典（永远返回，不抛）。"""
    info = {
        "configured": True,                  # 调用方按 cfg["tls_enabled"] 决定要不要调
        "enabled": False,
        "cert": tls_cert_path(),
        "key": tls_key_path(),
        "fingerprint": None,
        "subject": None,
        "not_after": None,
        "generated": False,
        "error": None,
    }
    if regen:
        for p in (tls_cert_path(), tls_key_path()):
            try:
                os.unlink(p)
            except OSError:
                pass

    missing = not (os.path.exists(tls_cert_path()) and os.path.exists(tls_key_path()))
    if missing:
        ok, msg = generate_self_signed()
        info["generated"] = ok
        if not ok:
            info["error"] = "生成自签证书失败：%s" % msg
            return info
        log("TLS：%s", msg)

    subject, not_after, epoch = read_cert_meta(tls_cert_path())
    if epoch is not None and epoch < time.time():
        # 过期证书会让板子那边的校验直接失败，静默换一张并说清楚
        log("TLS：证书已于 %s 过期，正在重新生成", not_after)
        ok, msg = generate_self_signed()
        info["generated"] = ok
        if not ok:
            info["error"] = "证书已过期且重新生成失败：%s" % msg
            return info
        subject, not_after, epoch = read_cert_meta(tls_cert_path())

    fp = pem_fingerprint(tls_cert_path())
    if not fp:
        info["error"] = "证书文件存在但读不出指纹（%s）" % tls_cert_path()
        return info
    info.update({"enabled": True, "fingerprint": fp,
                 "subject": subject, "not_after": not_after})
    return info


def load_ssl_context(info):
    """把证书装进 SSLContext。失败返回 (None, 原因)。"""
    try:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(info["cert"], info["key"])
        return ctx, None
    except Exception as exc:                       # noqa: BLE001
        return None, "%s: %s" % (type(exc).__name__, exc)


# ---------------------------------------------------------------- 日志

class Log:
    """写文件 + stderr，并在内存里留一份尾巴给管理页。"""

    def __init__(self, path, tail=LOG_TAIL):
        self.path = path
        self.tail = tail
        self._lines = []
        self._lock = threading.Lock()

    def __call__(self, fmt, *args):
        msg = fmt % args if args else fmt
        line = "%s %s" % (time.strftime("%Y-%m-%d %H:%M:%S"), msg)
        with self._lock:
            self._lines.append(line)
            if len(self._lines) > self.tail:
                del self._lines[:len(self._lines) - self.tail]
        try:
            sys.stderr.write(line + "\n")
            sys.stderr.flush()
        except Exception:                          # noqa: BLE001
            pass
        try:
            if os.path.exists(self.path) and os.path.getsize(self.path) > 2 * 1024 * 1024:
                os.replace(self.path, self.path + ".1")
            with open(self.path, "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except Exception:                          # noqa: BLE001 - 日志失败不能拖垮服务
            pass

    def lines(self, n=200):
        with self._lock:
            return list(self._lines[-n:])


log = Log(os.path.join(pkg_var(), "service.log"))


# ---------------------------------------------------------------- 配置

def load_config():
    cfg = dict(DEFAULT_CONFIG)
    try:
        with open(cfg_path(), "r", encoding="utf-8") as f:
            stored = json.load(f)
        if not isinstance(stored, dict):
            raise ValueError("config.json 必须是 JSON 对象")
        for k, v in stored.items():
            if k in DEFAULT_CONFIG and k != "devices":
                cfg[k] = v
        if isinstance(stored.get("devices"), list):
            cfg["devices"] = stored["devices"]
    except FileNotFoundError:
        pass
    except Exception as exc:                       # noqa: BLE001
        log("配置读取失败，保留原文件并停止启动：%s: %s", type(exc).__name__, exc)
        raise
    # 环境变量（生命周期脚本传入）优先级最高，但只覆盖"可被安装向导指定"的项
    if os.environ.get("TRIM_SERVICE_PORT"):
        try:
            cfg["port"] = int(os.environ["TRIM_SERVICE_PORT"])
        except ValueError:
            pass
    for env, key, cast in (("NSC_BIND", "bind", str), ("NSC_INTERVAL", "interval", float),
                           ("NSC_HIST", "hist_len", int), ("NSC_AUTH", "auth_mode", str)):
        if os.environ.get(env):
            try:
                cfg[key] = cast(os.environ[env])
            except ValueError:
                pass
    if os.environ.get("NSC_DOCKER") in ("1", "true", "yes", "on"):
        cfg["docker_enabled"] = True
    # TLS 默认是开，所以这个环境变量要能两个方向都表达（不能只认"开"）
    if os.environ.get("NSC_TLS") is not None:
        cfg["tls_enabled"] = os.environ["NSC_TLS"].strip().lower() in ("1", "true", "yes", "on")
    return cfg


def save_config(cfg):
    """原子写 + 0600：配置里有设备令牌哈希，不能让别的用户读。"""
    path = cfg_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(cfg, f, ensure_ascii=False, indent=2)
            f.flush()
            os.fsync(f.fileno())
    except Exception:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise
    os.replace(tmp, path)


# ---------------------------------------------------------------- 运行状态

def is_private_ip(ip):
    """仅用于 auth_mode=lan 的放行判断（RFC1918 / 回环 / 链路本地）。"""
    try:
        parts = [int(p) for p in ip.split(".")]
    except (ValueError, AttributeError):
        return False
    if len(parts) != 4 or any(p < 0 or p > 255 for p in parts):
        return False
    a, b = parts[0], parts[1]
    return (a == 10 or a == 127 or (a == 172 and 16 <= b <= 31)
            or (a == 192 and b == 168) or (a == 169 and b == 254))


def _token_hash(token):
    import hashlib
    return hashlib.sha256(("nsc:" + token).encode("utf-8")).hexdigest()


class State:
    """服务运行态：配置 + 采集器 + 配对。"""

    def __init__(self, cfg):
        self.cfg = cfg
        self.started_at = time.time()
        self.collector = CompanionCollector(
            interval=cfg["interval"], hist_len=cfg["hist_len"],
            docker_enabled=bool(cfg["docker_enabled"]),
        )
        self.collector.hostname = socket.gethostname()
        self.pending_pair = None               # {"code":..., "expires":..., "created":...}
        self.pair_attempts = {}                # ip -> [ts, ...]
        self.socket_error = None               # 管理面 Socket 绑定失败时的原因
        self.tls = {}                          # prepare_tls() 的结果，绑定后填充
        self._lock = threading.Lock()

    # ---- 配对
    def new_pair_code(self, ttl=300):
        code = "%06d" % secrets.randbelow(1000000)
        with self._lock:
            self.pending_pair = {"code": code, "created": time.time(), "expires": time.time() + ttl}
        log("已生成配对码（%d 秒内有效）", ttl)
        return code, ttl

    def cancel_pair_code(self):
        with self._lock:
            had = self.pending_pair is not None
            self.pending_pair = None
        return had

    def pair_allowed(self, ip):
        now = time.time()
        with self._lock:
            hist = [t for t in self.pair_attempts.get(ip, []) if now - t < PAIR_ATTEMPT_WINDOW]
            if len(hist) >= PAIR_ATTEMPT_LIMIT:
                self.pair_attempts[ip] = hist
                return False, PAIR_ATTEMPT_WINDOW - (now - hist[0])
            hist.append(now)
            self.pair_attempts[ip] = hist
        return True, 0

    def try_pair(self, code, name, ip):
        with self._lock:
            pend = self.pending_pair
            if not pend:
                return None, "没有正在进行的配对，请先在 NAS 管理页生成配对码"
            if time.time() > pend["expires"]:
                self.pending_pair = None
                return None, "配对码已过期，请重新生成"
            if not hmac.compare_digest(str(code), pend["code"]):
                return None, "配对码不正确"
            self.pending_pair = None          # 一次性
            token = secrets.token_urlsafe(24)
            dev = {
                "name": (name or "未命名设备")[:40],
                "sha256": _token_hash(token),
                "last4": token[-4:],
                "added_at": int(time.time()),
                "last_seen": 0,
                "last_ip": ip,
            }
            self.cfg["devices"].append(dev)
            save_config(self.cfg)
        log("新设备已配对：%s（%s）", dev["name"], ip)
        return token, None

    def revoke(self, last4_or_name):
        before = len(self.cfg["devices"])
        self.cfg["devices"] = [d for d in self.cfg["devices"]
                               if not (d.get("last4") == last4_or_name
                                       or d.get("name") == last4_or_name)]
        removed = before - len(self.cfg["devices"])
        if removed:
            save_config(self.cfg)
            log("已撤销 %d 个设备令牌", removed)
        return removed

    def check_token(self, token):
        if not token:
            return False, None
        h = _token_hash(token)
        for d in self.cfg["devices"]:
            if hmac.compare_digest(d.get("sha256", ""), h):
                d["last_seen"] = int(time.time())
                return True, d
        return False, None

    def note_seen(self, device, ip):
        if device is None:
            return
        device["last_ip"] = ip
        # 只更新内存即可，避免每个采样都写盘；退出时统一落盘
        device["_dirty"] = True

    def flush(self):
        try:
            for d in self.cfg["devices"]:
                d.pop("_dirty", None)
            save_config(self.cfg)
        except Exception as exc:                   # noqa: BLE001
            log("配置落盘失败：%s: %s", type(exc).__name__, exc)


# ---------------------------------------------------------------- HTTP

def json_bytes(obj):
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    server_version = "nasscreencompanion/" + APPVER
    protocol_version = "HTTP/1.1"

    # ---- 基础
    def address_string(self):
        try:
            return self.client_address[0] or "-"
        except (TypeError, IndexError):
            return "-"                             # AF_UNIX 的 client_address 是空串

    def log_message(self, fmt, *args):
        # 采样请求太频繁，不进日志文件，只留在内存尾巴里（便于管理页排查）
        log("%s %s", self.address_string(), fmt % args)

    @property
    def is_unix(self):
        return getattr(self.server, "is_unix", False)

    @property
    def is_tls(self):
        """这条连接是不是 TLS。

        直接看套接字类型，不额外打标记：TLS 连接是 wrap_socket 出来的
        SSLSocket，明文连接是 accept 出来的普通 socket。socket.socket 有
        __slots__，本来也没法往上面挂自定义属性。
        """
        return isinstance(self.request, ssl.SSLSocket)

    @property
    def is_plain_bootstrap(self):
        """遥测面上的**明文**连接。

        板子第一次连接时还不知道该信任哪张证书，没法直接建 TLS；所以遥测端口
        用首字节分派（见 ThreadedHTTPServer.get_request）额外接受明文，但明文
        连接**只能**读 /api/v1/identity 取证书，其余一律拒绝——不然这个口子就
        成了绕过 TLS 和令牌的旁路。管理面走 Unix Socket，与这里无关。

        只在本面**确实启用了 TLS** 时才算引导连接：配置里把 TLS 关掉时（明文
        排障模式）整个遥测面本来就是明文的，那时不该把功能砍到只剩 identity。
        """
        if self.is_unix or self.is_tls:
            return False
        return getattr(self.server, "ssl_context", None) is not None

    def _send(self, code, body, ctype="application/json; charset=utf-8", extra=None):
        if isinstance(body, (dict, list)):
            body = json_bytes(body)
        elif isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

    def _err(self, code, msg):
        self._send(code, {"ok": False, "error": msg})

    def _read_json(self, limit=64 * 1024):
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = 0
        if n <= 0 or n > limit:
            return {}
        try:
            return json.loads(self.rfile.read(n).decode("utf-8")) or {}
        except Exception:                          # noqa: BLE001
            return {}

    # ---- 网关鉴权：能连上 0600 的 Socket 就已经过了网关，但仍校验身份头
    def _gateway_user(self):
        uid = (self.headers.get("X-Trim-Userid") or "").strip()
        isadmin = (self.headers.get("X-Trim-Isadmin") or "").strip().lower()
        return (uid, isadmin in ("1", "true", "yes"))

    def _require_admin(self):
        if not self.is_unix:
            self._err(403, "管理接口只能通过飞牛统一网关访问")
            return None
        uid, isadmin = self._gateway_user()
        if not uid:
            self._err(403, "缺少网关身份头，请从 fnOS 桌面打开本应用")
            return None
        if not isadmin:
            self._err(403, "该操作需要管理员账号")
            return None
        return uid

    # ---- 遥测鉴权
    def _telemetry_ok(self):
        mode = self.server.state.cfg.get("auth_mode", "pairing")
        if mode == "lan":
            ok = is_private_ip(self.address_string())
            return ok, "仅允许内网地址访问" if not ok else None, None
        auth = self.headers.get("Authorization") or ""
        token = ""
        if auth.lower().startswith("bearer "):
            token = auth[7:].strip()
        if not token:
            token = self.headers.get("X-Token") or ""
        if not token:
            token = (urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
                     .get("token", [""])[0])
        ok, dev = self.server.state.check_token(token)
        if not ok:
            return False, ("未配对：请在 NAS 管理页生成配对码，"
                           "或在设备上调用 POST /api/v1/pair 换取令牌"), None
        self.server.state.note_seen(dev, self.address_string())
        return True, None, dev

    # ---- 路由
    def _normalize(self):
        p = urllib.parse.urlparse(self.path)
        path = p.path or "/"
        if path == GATEWAY_PREFIX:
            path = "/"
        elif path.startswith(GATEWAY_PREFIX + "/"):
            path = path[len(GATEWAY_PREFIX):]
        return path, urllib.parse.parse_qs(p.query)

    def do_HEAD(self):
        self.do_GET()

    def _bootstrap_guard(self, path):
        """明文引导连接只放行 /api/v1/identity，其它一律 403。

        放在 do_GET/do_POST 的最前面，而不是塞进各个路由里：路由以后还会增加，
        拦在入口才能保证新加的路由不会顺着这个口子漏出去。
        """
        if not self.is_plain_bootstrap:
            return False
        if self.command in ("GET", "HEAD") and path == "/api/v1/identity":
            return False
        self._err(403, "这个端口上只有 /api/v1/identity 接受明文连接；"
                       "其余接口都要走 HTTPS（证书在 identity 里）")
        return True

    def do_GET(self):
        path, q = self._normalize()
        if self._bootstrap_guard(path):
            return
        try:
            self._route_get(path, q)
        except Exception as exc:                   # noqa: BLE001
            log("处理 %s 出错：%s: %s", path, type(exc).__name__, exc)
            self._err(500, "%s: %s" % (type(exc).__name__, exc))

    def do_POST(self):
        path, q = self._normalize()
        if self._bootstrap_guard(path):
            return
        try:
            self._route_post(path, q)
        except Exception as exc:                   # noqa: BLE001
            log("处理 %s 出错：%s: %s", path, type(exc).__name__, exc)
            self._err(500, "%s: %s" % (type(exc).__name__, exc))

    # -- 只读端点（两个面都能访问）
    def _route_get(self, path, q):
        st = self.server.state
        coll = st.collector

        if path in ("/api/v1/health", "/healthz"):
            snap = coll.snapshot
            return self._send(200, {
                "ok": True, "app": APPNAME, "ver": APPVER,
                "ts": int(time.time()),
                "age_s": round(max(0.0, time.time() - snap.get("ts", 0)), 1) if snap.get("ts") else None,
                "host": coll.hostname, "ready": bool(snap.get("ready")),
            })

        if path == "/api/v1/identity":
            # 这个端点在鉴权之前：板子第一次连接时还不知道该信任谁，需要先拿到
            # 证书，由用户在屏幕上和管理页比对指纹后固定下来（TOFU，但由人确认）。
            # 证书本来就是公开信息——任何能发起 TLS 握手的客户端都会收到它，
            # 所以放在这里不泄漏任何新东西。
            #
            # 注意给的是完整 PEM 而不是只给指纹：板子必须**自己**对收到的证书算
            # SHA-256 再显示给用户。如果板子直接显示 JSON 里的 tls_fingerprint，
            # 中间人只要同时伪造证书和这个字段就能骗过肉眼比对。
            tls = getattr(st, "tls", {}) or {}
            cert_pem = None
            if tls.get("enabled") and tls.get("cert"):
                try:
                    with open(tls["cert"], "r") as fp:
                        cert_pem = fp.read()
                except OSError:
                    cert_pem = None
            payload = {
                "app": APPNAME, "ver": APPVER, "proto": PROTO_VERSION,
                "host": coll.hostname, "uid": os.getuid(),
                "auth_mode": st.cfg.get("auth_mode"),
                "paired_devices": len(st.cfg.get("devices", [])),
                "tls": bool(tls.get("enabled")),
                "tls_fingerprint": tls.get("fingerprint") if tls.get("enabled") else None,
                "cert_pem": cert_pem,
            }
            if self.is_plain_bootstrap:
                # 明文连接上少说两句：uid / auth_mode / 已配对设备数在明文里谁都
                # 能读，而引导只需要证书。指纹留着——板子要用它自校验一遍自己
                # 算出来的值。
                return self._send(200, {
                    "app": APPNAME, "ver": APPVER, "proto": PROTO_VERSION,
                    "host": coll.hostname,
                    "tls": bool(tls.get("enabled")),
                    "tls_fingerprint": payload["tls_fingerprint"],
                    "cert_pem": cert_pem,
                })
            return self._send(200, payload)

        if path == "/api/v1/pair":
            return self._err(405, "配对请用 POST")

        if path in ("/api/v1/status", "/api/v1/history"):
            if not self.is_unix:
                ok, why, _dev = self._telemetry_ok()
                if not ok:
                    return self._send(401, {"ok": False, "error": why,
                                            "how": "POST /api/v1/pair {\"code\":\"123456\",\"name\":\"lcd\"}"})
            if path == "/api/v1/status":
                snap = dict(coll.snapshot)
                snap["age_s"] = round(max(0.0, time.time() - snap.get("ts", 0)), 1) if snap.get("ts") else None
                snap["app"] = APPNAME
                snap["app_ver"] = APPVER
                return self._send(200, snap)
            # `?n=` 让客户端声明"我最多能收多少行"。板子的接收缓冲是 32 KB，
            # 而 hist_len 允许配到 3600（约 151 KB）——不声明的话，超出的部分会被
            # 板子截断，JSON 解析失败，于是它每秒重试一次、曲线回填永远不成功。
            # 只取**最近** n 行（曲线要的是靠后的一段）。不带 n 时行为不变。
            with coll.lock:
                rows = list(coll.history)
            try:
                want = int((q.get("n") or [""])[0])
            except (TypeError, ValueError):
                want = 0
            if want > 0:
                rows = rows[-want:]
            return self._send(200, {"cols": ["ts", "cpu", "mem", "rx_kbs", "tx_kbs"],
                                    "rows": rows, "n": len(rows)})

        # -- 管理端点（仅网关）
        if path in ("/", "/index.html"):
            if not self.is_unix:
                return self._err(404, "not found")
            uid, _ = self._gateway_user()
            if not uid:
                return self._err(403, "缺少网关身份头，请从 fnOS 桌面打开本应用")
            return self._send(200, self._admin_html(), "text/html; charset=utf-8")

        if path == "/api/state":
            if self._require_admin() is None:
                return
            snap = coll.snapshot
            return self._send(200, {
                "ok": True,
                "app": APPNAME, "ver": APPVER, "proto": PROTO_VERSION,
                "host": coll.hostname,
                "pid": os.getpid(), "uid": os.getuid(), "user": _current_user(),
                "uptime_s": int(time.time() - st.started_at),
                "collector": {
                    "ready": bool(snap.get("ready")), "seq": snap.get("seq", 0),
                    "ts": snap.get("ts", 0),
                    "age_s": round(max(0.0, time.time() - snap.get("ts", 0)), 1) if snap.get("ts") else None,
                    "interval": coll.interval, "hist_len": coll.hist_len,
                    "samples": len(coll.history),
                },
                "config": {k: v for k, v in st.cfg.items() if k != "devices"},
                "devices": [{k: v for k, v in d.items() if k not in ("sha256", "_dirty")}
                            for d in st.cfg.get("devices", [])],
                "pairing": ({"expires_in": int(st.pending_pair["expires"] - time.time())}
                            if st.pending_pair else None),
                "listen": {"socket": socket_path(), "tcp": "%s:%s" % (st.cfg["bind"], st.cfg["port"])},
                "tls": {k: v for k, v in (getattr(st, "tls", {}) or {}).items() if k != "key"},
                "paths": {"appdest": app_dest(), "pkgvar": pkg_var(), "pkgetc": pkg_etc()},
            })

        if path == "/api/probe":
            if self._require_admin() is None:
                return
            return self._send(200, probe_capabilities(
                include_docker=bool(st.cfg.get("docker_enabled"))))

        if path == "/api/diagnostics":
            if self._require_admin() is None:
                return
            return self._send(200, self._diagnostics())

        if path == "/api/logs":
            if self._require_admin() is None:
                return
            return self._send(200, {"ok": True, "lines": log.lines(200)})

        if path == "/api/status-now":
            if self._require_admin() is None:
                return
            return self._send(200, coll.snapshot)

        return self._err(404, "not found: " + path)

    # -- 写端点
    def _route_post(self, path, q):
        st = self.server.state

        if path == "/api/v1/pair":
            if self.is_unix:
                return self._err(403, "配对请在遥测端口调用")
            body = self._read_json()
            allowed, wait = st.pair_allowed(self.address_string())
            if not allowed:
                return self._err(429, "尝试过于频繁，请 %d 秒后再试" % int(wait))
            token, why = st.try_pair(body.get("code"), body.get("name"), self.address_string())
            if token is None:
                return self._send(403, {"ok": False, "error": why})
            return self._send(200, {
                "ok": True, "token": token, "app": APPNAME, "proto": PROTO_VERSION,
                "note": "令牌只在本次响应里出现，请写入设备并妥善保存",
                "usage": "Authorization: Bearer <token>",
            })

        if path == "/api/config":
            if self._require_admin() is None:
                return
            body = self._read_json()
            try:
                changed, restart = self._apply_config(body)
            except ConfigError as exc:
                return self._err(400, str(exc))
            return self._send(200, {"ok": True, "changed": changed, "restart_required": restart})

        if path == "/api/pairing/new":
            if self._require_admin() is None:
                return
            body = self._read_json()
            try:
                ttl = max(60, min(1800, int(body.get("ttl") or 300)))
            except (TypeError, ValueError):
                ttl = 300
            code, ttl = st.new_pair_code(ttl)
            return self._send(200, {"ok": True, "code": code, "ttl": ttl})

        if path == "/api/pairing/cancel":
            if self._require_admin() is None:
                return
            return self._send(200, {"ok": True, "cancelled": st.cancel_pair_code()})

        if path == "/api/devices/revoke":
            if self._require_admin() is None:
                return
            body = self._read_json()
            who = body.get("id") or body.get("last4") or body.get("name")
            if not who:
                return self._err(400, "缺少 id")
            return self._send(200, {"ok": True, "removed": st.revoke(who)})

        if path == "/api/collector/docker":
            if self._require_admin() is None:
                return
            body = self._read_json()
            on = bool(body.get("enabled"))
            st.cfg["docker_enabled"] = on
            st.collector.docker_enabled = on
            save_config(st.cfg)
            log("容器采集已%s", "开启" if on else "关闭")
            return self._send(200, {"ok": True, "docker_enabled": on})

        if path == "/api/shutdown":
            # 仅供 cmd/main 的优雅停止兜底；不对外暴露（仍是网关+管理员）
            if self._require_admin() is None:
                return
            self._send(200, {"ok": True})
            threading.Thread(target=self.server.shutdown, daemon=True).start()
            return

        return self._err(404, "not found: " + path)

    # ---- 配置写入
    def _apply_config(self, body):
        st = self.server.state
        changed, restart = [], []
        for key, cast, lo, hi in (("interval", float, 0.2, 60.0),
                                  ("hist_len", int, 30, 3600),
                                  ("port", int, 1, 65535)):
            if key in body:
                try:
                    val = cast(body[key])
                except (TypeError, ValueError):
                    raise ConfigError("%s 取值非法" % key)
                if not (lo <= val <= hi):
                    raise ConfigError("%s 必须在 %s~%s 之间" % (key, lo, hi))
                if st.cfg.get(key) != val:
                    st.cfg[key] = val
                    changed.append(key)
                    if key == "port":
                        restart.append(key)
        if "bind" in body:
            b = str(body["bind"]).strip()
            if b in ("0.0.0.0", "127.0.0.1", "::"):
                if st.cfg.get("bind") != b:
                    st.cfg["bind"] = b
                    changed.append("bind")
                    restart.append("bind")
            else:
                raise ConfigError("bind 只允许 0.0.0.0 / 127.0.0.1")
        if "auth_mode" in body:
            m = str(body["auth_mode"]).strip()
            if m not in ("pairing", "lan"):
                raise ConfigError("auth_mode 只允许 pairing / lan")
            if st.cfg.get("auth_mode") != m:
                st.cfg["auth_mode"] = m
                changed.append("auth_mode")
        if "tls_enabled" in body:
            v = body["tls_enabled"]
            v = v if isinstance(v, bool) else str(v).strip().lower() in ("1", "true", "yes", "on")
            if bool(st.cfg.get("tls_enabled", True)) != v:
                st.cfg["tls_enabled"] = v
                changed.append("tls_enabled")
                restart.append("tls_enabled")     # 换/停 TLS 必须重新绑定监听套接字
        if changed:
            st.collector.interval = float(st.cfg["interval"])
            st.collector.set_hist_len(int(st.cfg["hist_len"]))   # 会顺手截断已有历史
            save_config(st.cfg)
            log("配置已更新：%s", ", ".join(changed))
        return changed, restart

    # ---- 诊断
    def _diagnostics(self):
        st = self.server.state
        out = {"ok": True, "checks": []}

        def add(name, status, detail):
            out["checks"].append({"name": name, "status": status, "detail": detail})

        sock = socket_path()
        add("网关 Socket", "ok" if os.path.exists(sock) else "error",
            "%s（%s）" % (sock, "存在" if os.path.exists(sock) else "不存在"))
        if st.socket_error:
            add("网关 Socket 绑定", "error", st.socket_error)
        if os.path.exists(sock):
            try:
                add("Socket 权限", "ok" if oct(os.stat(sock).st_mode & 0o777) == "0o600" else "warn",
                    oct(os.stat(sock).st_mode & 0o777))
            except OSError as exc:
                add("Socket 权限", "warn", str(exc))

        # 8799 是开发版采集器的端口：只报告，绝不触碰
        dev_busy = _port_busy("127.0.0.1", 8799)
        add("开发版端口 8799", "info",
            "被占用（开发版采集器在用，本应用不会改动它）" if dev_busy else "空闲")

        add("遥测端口", "ok" if _port_busy(st.cfg["bind"] if st.cfg["bind"] != "0.0.0.0" else "127.0.0.1",
                                          st.cfg["port"]) else "error",
            "%s:%s" % (st.cfg["bind"], st.cfg["port"]))
        add("鉴权模式", "warn" if st.cfg["auth_mode"] == "lan" else "ok",
            "内网免令牌（仅排障用）" if st.cfg["auth_mode"] == "lan" else "配对令牌")

        # TLS：打开着就必须是 ok；没打开而配置要求打开 = error，绝不静默降级
        tls = getattr(st, "tls", {}) or {}
        if tls.get("enabled"):
            add("遥测面 TLS", "ok", "%s，指纹 SHA256 %s，到期 %s"
                % (tls.get("subject") or "自签证书", tls.get("fingerprint"),
                   tls.get("not_after") or "未知"))
            add("首次取证书通道", "ok",
                "明文（只放行 /api/v1/identity，别的路径一律 403）：开发板在固定证书之前"
                "没法建 TLS，靠这一次明文连接把证书取回去，由用户在屏幕上比对指纹")
        elif tls.get("configured"):
            add("遥测面 TLS", "error", "%s（配置要求启用但当前是明文 HTTP）"
                % (tls.get("error") or "未知原因"))
        else:
            add("遥测面 TLS", "warn", "配置里已关闭（tls_enabled=false），当前为明文 HTTP")

        add("容器采集", "warn" if st.cfg["docker_enabled"] else "ok",
            "已开启，会读取 docker.sock" if st.cfg["docker_enabled"] else "已关闭（默认）")
        add("运行用户", "ok", "uid=%d" % os.getuid())
        return out

    # ---- 管理页
    def _admin_html(self):
        # fnOS 把 app.tgz 的内容直接解到 target（$TRIM_APPDEST）下，所以管理页
        # 在 $TRIM_APPDEST/ui/www/；开发机直接对着包目录跑时在 <包根>/app/ui/www/。
        # 两种布局都试，外加"和 server/ 同级"这一次兜底。
        here = os.path.dirname(os.path.abspath(__file__))
        for cand in (os.path.join(app_dest(), "ui", "www", "index.html"),
                     os.path.join(app_dest(), "app", "ui", "www", "index.html"),
                     os.path.join(os.path.dirname(here), "ui", "www", "index.html")):
            try:
                with open(cand, "r", encoding="utf-8") as f:
                    return f.read()
            except OSError:
                continue
        return ("<!doctype html><meta charset=utf-8><h1>飞牛监控</h1>"
                "<p>管理页文件缺失（ui/www/index.html）。服务本身运行中，"
                "遥测接口可用。</p>")


def _port_busy(host, port, timeout=1.0):
    """只做连接探测，绝不 bind 占位、绝不 kill 任何进程。"""
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


# ---------------------------------------------------------------- 服务器

class ThreadedHTTPServer(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True
    request_queue_size = 64
    is_tls = False
    ssl_context = None

    def get_request(self):
        """accept 一条连接，按首字节决定走 TLS 还是明文。

        为什么不在监听套接字上直接 wrap：开发板首次连接时还不知道该信任哪张
        证书，没法建 TLS，而 ESP-IDF 的 esp-tls 在没有任何 CA 来源时会**直接
        拒绝连接**（"No server verification option set"，除非全局打开
        ESP_TLS_INSECURE）。与其让板子全局放宽校验，不如让这里多收一种明文
        连接、只用来发证书——板子的 TLS 配置保持严格。

        分派依据是 TLS 记录头第一个字节 0x16（handshake）。用 MSG_PEEK 而不是
        recv：真正建 TLS 时这一个字节必须还在缓冲区里，否则握手会因为记录被
        截断而失败。
        """
        conn, addr = self.socket.accept()
        if self.ssl_context is None:
            return conn, addr
        try:
            head = conn.recv(1, socket.MSG_PEEK)
        except OSError:
            head = b""
        if head[:1] == b"\x16":
            try:
                conn = self.ssl_context.wrap_socket(conn, server_side=True)
            except OSError:
                # 握手失败的连接没人接手，这里自己关掉（socketserver 只在
                # get_request 成功后才会接管生命周期）
                try:
                    conn.close()
                except OSError:
                    pass
                raise
        return conn, addr

    def handle_error(self, request, client_address):
        # 握手/读写中途的 TLS 错误是正常的网络噪音（扫描器、明文探测），不该把
        # traceback 刷进服务日志——真正的处理异常照旧抛。
        if isinstance(sys.exc_info()[1], ssl.SSLError):
            return
        super().handle_error(request, client_address)


class UnixHTTPServer(ThreadedHTTPServer):
    """AF_UNIX 版：HTTPServer.server_bind 会对路径做 getfqdn，必须绕开。"""
    address_family = socket.AF_UNIX
    is_unix = True

    def server_bind(self):
        if os.path.exists(self.server_address):
            os.unlink(self.server_address)
        socketserver.TCPServer.server_bind(self)
        self.server_name = "nasscreencompanion"
        self.server_port = 0

    def server_activate(self):
        socketserver.TCPServer.server_activate(self)
        try:
            os.chmod(self.server_address, 0o600)
        except OSError as exc:
            log("Socket 权限设置失败（%s），继续启动", exc)


def make_servers(state, cfg, bind_socket=True):
    """两个面各自独立绑定。

    管理面 Socket 建不起来（target 目录不可写、路径过长……）时不能让整个进程
    死掉——遥测面是开发板要的东西，把它一起拖下水对用户没有任何好处。所以这里
    分别捕获，把失败原因交回给 run() 决定怎么报。
    """
    servers = []
    problems = []

    if bind_socket:
        try:
            srv = UnixHTTPServer(socket_path(), Handler)
            srv.state = state
            servers.append(srv)
        except OSError as exc:
            problems.append(("管理面 Unix Socket", socket_path(), exc))

    # TLS 只包遥测面：管理面走的是 Unix Socket，外面已经有飞牛的网关会话，
    # 再套一层 TLS 没有意义（网关到 Socket 是本机通信）。
    tls_info = {"configured": bool(cfg.get("tls_enabled", True)), "enabled": False,
                "cert": tls_cert_path(), "key": tls_key_path(),
                "fingerprint": None, "subject": None, "not_after": None,
                "generated": False, "error": None}
    ssl_ctx = None
    if tls_info["configured"]:
        tls_info = prepare_tls()
        if tls_info["enabled"]:
            ssl_ctx, why = load_ssl_context(tls_info)
            if ssl_ctx is None:
                tls_info["enabled"] = False
                tls_info["error"] = "加载证书失败：%s" % why
    state.tls = tls_info

    try:
        tcp = ThreadedHTTPServer((cfg["bind"], int(cfg["port"])), Handler)
        tcp.state = state
        tcp.is_unix = False
        tcp.is_tls = bool(ssl_ctx)
        # 不在这里 wrap 监听套接字：改成每条连接在 get_request() 里按首字节
        # 分派，好让板子用一次明文 GET 取走证书（见 ThreadedHTTPServer）。
        tcp.ssl_context = ssl_ctx
        servers.append(tcp)
    except OSError as exc:
        problems.append(("遥测面 TCP", "%s:%s" % (cfg["bind"], cfg["port"]), exc))

    return servers, problems


def run(cfg):
    state = State(cfg)
    servers, problems = make_servers(state, cfg)

    if not any(not getattr(s, "is_unix", False) for s in servers):
        # 遥测端口起不来才是真正的致命错误：开发板什么也读不到
        detail = ""
        for what, where, exc in problems:
            if what == "遥测面 TCP":
                detail = "%s（%s）" % (exc, where)
                break
        if "Address already in use" in detail or (problems and
                                                  getattr(problems[-1][2], "errno", None) == errno.EADDRINUSE):
            msg = ("遥测端口 %s:%s 已被占用。为避免影响正在运行的服务，"
                   "本应用不会结束该进程。请改端口，或先确认占用者是谁。" % (cfg["bind"], cfg["port"]))
        else:
            msg = "遥测端口 %s:%s 无法监听：%s" % (cfg["bind"], cfg["port"], detail or "未知错误")
        log("启动失败：%s", msg)
        print(msg, file=sys.stderr)
        return 3

    log("启动：app=%s ver=%s proto=%d uid=%d", APPNAME, APPVER, PROTO_VERSION, os.getuid())
    state.socket_error = None
    for what, where, exc in problems:
        state.socket_error = "%s 绑定失败：%s（%s）" % (what, exc, where)
        log("警告：%s——管理页会打不开，遥测面不受影响", state.socket_error)
    if any(getattr(s, "is_unix", False) for s in servers):
        log("管理面 Socket：%s（0600，走飞牛统一网关）", socket_path())
    if state.tls.get("enabled"):
        log("遥测面 TLS：已启用（%s，指纹 SHA256 %s，到期 %s）",
            state.tls.get("subject") or "自签证书", state.tls.get("fingerprint"),
            state.tls.get("not_after") or "未知")
        log("首次取证书通道：同一个端口上另收明文连接，但只放行 /api/v1/identity"
            "（开发板在固定证书之前建不了 TLS，靠它把证书取回去比对指纹）")
    elif state.tls.get("configured"):
        log("遥测面 TLS：未能启用（%s）——本次为明文 HTTP，配对令牌仍然必需。"
            "管理页「诊断」里可以看到详情。", state.tls.get("error") or "未知原因")
    else:
        log("遥测面 TLS：配置里被关闭（tls_enabled=false）——明文 HTTP，仅限排障")
    log("遥测面 TCP：%s:%s（auth_mode=%s，docker=%s，tls=%s）",
        cfg["bind"], cfg["port"], cfg["auth_mode"], cfg["docker_enabled"],
        "on" if state.tls.get("enabled") else "off")

    state.collector.start()
    # 先绑定再等首帧：端口早一刻可用，板子不会连不上
    deadline = time.time() + 5.0
    while time.time() < deadline and not state.collector.snapshot.get("ready"):
        time.sleep(0.1)

    threads = []
    for srv in servers:
        t = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.5},
                             name="http", daemon=True)
        t.start()
        threads.append(t)

    log("就绪：seq=%s ready=%s", state.collector.snapshot.get("seq"),
        state.collector.snapshot.get("ready"))
    try:
        while any(t.is_alive() for t in threads):
            time.sleep(0.5)
    except KeyboardInterrupt:
        log("收到中断信号，正在停止")
    finally:
        for srv in servers:
            try:
                srv.shutdown()
                srv.server_close()
            except Exception:                      # noqa: BLE001
                pass
        state.collector.stop()
        state.flush()
        try:
            os.unlink(socket_path())
        except OSError:
            pass
        log("已停止")
    return 0


def selftest():
    cfg = load_config()
    print(json.dumps({"config": {k: v for k, v in cfg.items() if k != "devices"},
                      "devices": len(cfg["devices"]),
                      "paths": {"appdest": app_dest(), "pkgvar": pkg_var(),
                                "socket": socket_path()}},
                     ensure_ascii=False, indent=2))
    coll = CompanionCollector(interval=cfg["interval"], hist_len=cfg["hist_len"],
                              docker_enabled=cfg["docker_enabled"])
    coll._cpu()
    coll._net()
    coll._disk_io()
    time.sleep(1.05)
    snap = coll.sample()
    print(json.dumps({"seq": snap["seq"], "caps": snap["caps"], "trunc": snap["trunc"],
                      "modules": snap["modules"]}, ensure_ascii=False, indent=2))
    return 0


def tls_status_cmd(regen=False):
    """--tls-status / --tls-regen：给生命周期脚本和运维看的一行结论。"""
    cfg = load_config()
    if not regen and not cfg.get("tls_enabled", True):
        print("TLS 已关闭（config.json 里 tls_enabled=false）——遥测面是明文 HTTP", file=sys.stderr)
        return 1
    info = prepare_tls(regen=regen)
    if info.get("enabled"):
        print("TLS 已启用｜%s｜指纹 SHA256 %s｜到期 %s"
              % (info.get("subject") or "自签证书", info.get("fingerprint"),
                 info.get("not_after") or "未知"))
        return 0
    print("TLS 未启用：%s" % (info.get("error") or "未知原因"), file=sys.stderr)
    return 1


def main(argv=None):
    ap = argparse.ArgumentParser(description="NAS 屏幕伴侣后端服务")
    ap.add_argument("--serve", action="store_true", help="启动服务（生命周期脚本用）")
    ap.add_argument("--selftest", action="store_true", help="自检后退出")
    ap.add_argument("--tls-status", action="store_true",
                    help="打印证书状态与指纹（缺证书时生成），未启用则退出码 1")
    ap.add_argument("--tls-regen", action="store_true",
                    help="重新生成自签证书（指纹会变，已配对的板子需要重新确认）")
    ap.add_argument("--check-port", type=int, metavar="PORT",
                    help="检测端口是否被占用（占用则退出码 1），不做任何改动")
    args = ap.parse_args(argv)

    if args.check_port:
        cfg = load_config()
        host = cfg["bind"] if cfg["bind"] != "0.0.0.0" else "127.0.0.1"
        if _port_busy(host, args.check_port):
            print("端口 %d 已被占用" % args.check_port, file=sys.stderr)
            return 1
        print("端口 %d 可用" % args.check_port)
        return 0
    if args.tls_regen:
        return tls_status_cmd(regen=True)
    if args.tls_status:
        return tls_status_cmd()
    if args.selftest:
        return selftest()
    return run(load_config())


if __name__ == "__main__":
    sys.exit(main())
