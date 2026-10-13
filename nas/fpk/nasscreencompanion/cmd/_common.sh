#!/bin/bash
# NAS 屏幕伴侣（nasscreencompanion）生命周期脚本 —— 公共前言
# 由 cmd/ 下 9 个脚本共同 source。改这里等于同时改 9 个脚本，改完请重跑
# nas/fpk/test_lifecycle.sh 或手工走一遍 安装→启动→改设置→升级→卸载。
#
# 三条硬约束，改动前请先读：
#   1. 不触碰 8799：那是开发版采集器（SSH + systemd）的端口，本应用只报告不干预；
#   2. 不 kill 别人的进程：端口冲突时只报错退出，绝不"清理"占用者；
#   3. 可重复执行：每个脚本都要能在已经做过一遍的情况下再跑一遍而不出错。
set -u

APPNAME=nasscreencompanion

# 目录布局（两种都要认，写死任何一种都会在真机上挂）：
#   真机安装：/var/apps/{appname}/target -> /vol{n}/@appcenter/{appname}，
#             fnOS 把 app.tgz 的内容直接解到 target 下，于是 server/ 在
#             $TRIM_APPDEST/server/，Socket 在 $TRIM_APPDEST/app.sock。
#   开发机  ：直接对着包目录跑，内容在 <包根>/app/ 下。
TARGET="${TRIM_APPDEST:-$(cd "$(dirname "$0")/.." && pwd)}"
if [ -d "$TARGET/app/server" ] && [ ! -d "$TARGET/server" ]; then
  APP_ROOT="$TARGET/app"
else
  APP_ROOT="$TARGET"
fi

PKG_VAR="${TRIM_PKGVAR:-$TARGET/var}"
PKG_ETC="${TRIM_PKGETC:-$TARGET/etc}"
PID_FILE="$PKG_VAR/app.pid"
SOCK_FILE="$TARGET/app.sock"
INFO_LOG="$PKG_VAR/info.log"
SERVER="$APP_ROOT/server/nas_companion_server.py"
DEV_AGENT_PORT=8799

say()  { echo "[$APPNAME] $*"; }
# 警告也要写进 $TRIM_TEMP_LOGFILE：那是应用中心展示给用户看的日志。
# 只写 stderr 的话，用户在界面上只会看到最后那句笼统的"检查未通过"，
# 真正的原因（比如依赖的 python312 还没装）他根本看不到。
warn() {
  echo "[$APPNAME] WARN: $*" >&2
  [ -n "${TRIM_TEMP_LOGFILE:-}" ] && echo "[$APPNAME] 警告：$*" >> "$TRIM_TEMP_LOGFILE" 2>/dev/null
  return 0
}
fail() {
  echo "[$APPNAME] ERROR: $*" >&2
  [ -n "${TRIM_TEMP_LOGFILE:-}" ] && echo "[$APPNAME] $*" >> "$TRIM_TEMP_LOGFILE" 2>/dev/null
  return 1
}

# Lifecycle callbacks may start as root; package data and code always use its identity.
as_package() {
  if [ "$(id -u)" = 0 ]; then
    local package="${TRIM_USERNAME:-}" group
    [ -n "$package" ] && [ "$(id -u "$package" 2>/dev/null)" != 0 ] || {
      fail "缺少有效的非 root 包用户"; return 1;
    }
    group=$(id -gn "$package") || return 1
    runuser -u "$package" -g "$group" -- "$@"
  else
    "$@"
  fi
}

prepare_package_data() {
  [ "$(id -u)" = 0 ] || return 0
  local package="${TRIM_USERNAME:-}" group diagnostic
  [ -n "$package" ] || { fail "缺少包用户身份"; return 1; }
  group=$(id -gn "$package") || return 1
  # Foreground diagnostics reach the application center before any daemon redirection.
  if diagnostic=$(/usr/bin/python3 -I "$CMD_DIR/docker_launch.py" \
      --prepare --user "$package" --group "$group" 2>&1); then
    say "$diagnostic"
  else
    fail "私有数据权限检查失败：$diagnostic"
    return 1
  fi
}

resolve_python() {
  for c in /var/apps/python312/target/bin/python3 /var/apps/python312/target/bin/python; do
    [ -x "$c" ] && { printf '%s' "$c"; return 0; }
  done
  command -v python3 2>/dev/null && return 0
  return 1
}

# 只读监听表，覆盖非 loopback/IPv6；不 bind、不占位、不 kill。
# 返回 0=占用，1=未见监听，2=未能判定。调用方不能把 2 当空闲。
port_busy() {
  local py
  py=$(resolve_python) || return 2
  "$py" - "$1" <<'PYEOF'
import os, platform, subprocess, sys
try:
    port = int(sys.argv[1])
    if not 1 <= port <= 65535:
        raise ValueError("非法端口")
    if platform.system() == "Linux":
        busy = False
        for path in ("/proc/net/tcp", "/proc/net/tcp6"):
            if path.endswith("tcp6") and not os.path.exists(path):
                continue  # IPv6 未启用
            with open(path) as f:
                if "local_address" not in f.readline():
                    raise ValueError("监听表缺少表头")
                for line in f:
                    fields = line.split()
                    if len(fields) < 4:
                        raise ValueError("监听表记录不完整")
                    local_port = int(fields[1].rsplit(":", 1)[1], 16)
                    busy |= fields[3] == "0A" and local_port == port
    elif platform.system() == "Darwin":
        # 本机生命周期测试；NAS 使用上面的内核表。
        result = subprocess.run(["/usr/sbin/lsof", "-w", "-nP", "-iTCP:%d" % port,
                                 "-sTCP:LISTEN", "-F", "n"],
                                capture_output=True, text=True, timeout=5)
        if result.stderr or result.returncode not in (0, 1):
            raise ValueError("lsof 未能读取监听表")
        busy = any(line.startswith("n") for line in result.stdout.splitlines())
        if (result.returncode == 0) != busy:
            raise ValueError("lsof 返回值与监听记录不一致")
    else:
        raise ValueError("系统缺少受支持的只读监听表")
except (OSError, ValueError, IndexError, subprocess.SubprocessError) as exc:
    print("端口预检未判定：%s" % exc, file=sys.stderr)
    sys.exit(2)
sys.exit(0 if busy else 1)
PYEOF
}

is_running() {
  [ -f "$PID_FILE" ] || return 1
  local pid
  pid=$(as_package cat "$PID_FILE" 2>/dev/null)
  case "$pid" in ''|*[!0-9]*) return 1 ;; esac
  # 用 kill -0 判断存活：POSIX 通用，不依赖 /proc（本地 macOS 上也能跑通测试）
  as_package kill -0 "$pid" 2>/dev/null || return 1
  # 防 PID 复用：Linux 上再确认这个 pid 确实是本应用的服务进程
  if [ -r "/proc/$pid/cmdline" ]; then
    tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -q "nas_companion_server.py" || return 1
  fi
  return 0
}

cleanup_socket() {
  if [ -S "$SOCK_FILE" ]; then
    as_package rm -f "$SOCK_FILE" || return 1
  fi
  return 0
}

# 把向导/配置值合并进 config.json：关键是保留已配对设备，不能整体覆盖
apply_config() {
  local py="$1" port="$2" bind="$3" auth="$4" docker="$5" interval="$6" tls="${7:-true}"
  as_package "$py" - "$PKG_VAR/config.json" "$port" "$bind" "$auth" "$docker" "$interval" "$tls" <<'PYEOF'
import json, os, sys, tempfile, fcntl, stat
path, port, bind, auth, docker, interval, tls = sys.argv[1:8]
os.makedirs(os.path.dirname(path), exist_ok=True)
lock_fd = os.open(path + ".lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
with os.fdopen(lock_fd, "a") as lock:
    if not stat.S_ISREG(os.fstat(lock.fileno()).st_mode):
        raise ValueError("配置锁不是普通文件")
    fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
    cfg = {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
    except FileNotFoundError:
        pass
    if not isinstance(cfg, dict):
        raise ValueError("config.json 必须是 JSON 对象，原文件未修改")
    devices = cfg.get("devices") if isinstance(cfg.get("devices"), list) else []
    cfg.update({
        "port": int(port), "bind": bind, "auth_mode": auth,
        "docker_enabled": (docker == "true"), "interval": float(interval),
        "tls_enabled": (tls == "true"),
    })
    cfg.setdefault("hist_len", 300)
    cfg["devices"] = devices                     # 重新配置不能把开发板踢下线
    revision = cfg.get("_revision", 0)
    if type(revision) is not int or revision < 0:
        raise ValueError("配置修订号非法")
    cfg["_revision"] = revision + 1
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".config-", suffix=".tmp", dir=os.path.dirname(path))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            fd = None
            json.dump(cfg, f, ensure_ascii=False, indent=2)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    finally:
        if fd is not None:
            os.close(fd)
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass

    print("config.json: port=%s bind=%s auth=%s docker=%s interval=%s tls=%s devices=%d"
          % (port, bind, auth, docker, interval, tls, len(devices)))
PYEOF
}

# 归一化向导输入（值都是字符串，必须在这里再校验一遍）
norm_wizard() {
  # 端口优先级：向导里填的 > 系统分配的 TRIM_SERVICE_PORT > 8798。
  # 反过来会把用户在向导里的选择悄悄覆盖掉，所以向导优先。
  W_PORT="${wizard_port:-${TRIM_SERVICE_PORT:-8798}}"
  case "$W_PORT" in
    ''|*[!0-9]*)
      # 不声不响地换掉用户填的值是最坏的一种"成功"（审计 M27）：界面上填了 abc，
      # 安装说通过，实际监听 8798。回退照旧，但必须留下痕迹。
      if [ -n "${wizard_port:-}" ]; then warn "端口 \"$wizard_port\" 不是纯数字，已回退到 8798"; fi
      W_PORT=8798 ;;
  esac
  if ! { [ "$W_PORT" -ge 1 ] && [ "$W_PORT" -le 65535 ]; }; then
    warn "端口 $W_PORT 超出 1~65535，已回退到 8798"
    W_PORT=8798
  fi
  # 去掉前导零："08798" 和 "8798" 是同一个端口，后面要拿它跟 config.json 里的值比对
  W_PORT=$((10#$W_PORT))

  case "${wizard_bind:-lan}" in
    local|localhost|127.0.0.1) W_BIND=127.0.0.1 ;;
    *) W_BIND=0.0.0.0 ;;
  esac

  case "${wizard_auth:-pairing}" in
    lan|open) W_AUTH=lan ;;
    *) W_AUTH=pairing ;;
  esac

  case "$(printf '%s' "${wizard_docker:-false}" | tr 'A-Z' 'a-z')" in
    true|1|yes|on) W_DOCKER=true ;;
    *) W_DOCKER=false ;;
  esac

  # TLS 默认开：方案明确不允许"上架版本默认明文"。只有用户在向导里明确关掉
  # 才写 false，取值缺失/为空一律当作开。
  case "$(printf '%s' "${wizard_tls:-true}" | tr 'A-Z' 'a-z')" in
    false|0|no|off) W_TLS=false ;;
    *) W_TLS=true ;;
  esac

  # 采样间隔：服务端认的合法区间是 0.05~60 秒（nas_companion_server.py 的同一组
  # 上下限）。这里以前只挡了非数字，"0" 能一路写进 config.json —— 采集线程算出负的
  # sleep 就干脆不睡，CPU 直接跑满（审计 M44）。范围外的值回退到 1.0 并留日志。
  W_INTERVAL="${wizard_interval:-1.0}"
  case "$W_INTERVAL" in
    ''|*[!0-9.]*|*.*.*)
      if [ -n "${wizard_interval:-}" ]; then warn "采样间隔 \"$wizard_interval\" 不是数字，已回退到 1.0"; fi
      W_INTERVAL=1.0 ;;
  esac
  if ! awk -v v="$W_INTERVAL" 'BEGIN { exit !(v + 0 >= 0.05 && v + 0 <= 60.0) }'; then
    warn "采样间隔 ${W_INTERVAL}s 不在 0.05~60s 内，已回退到 1.0"
    W_INTERVAL=1.0
  fi
}

# 服务控制统一走 cmd/main：stop/start 的判定逻辑（PID 校验、端口冲突、
# 就绪等待）只写一份，避免这里和 main 各写一套、日后改一处漏一处。
CMD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
service_start() { "$CMD_DIR/main" start; }
service_stop()  { "$CMD_DIR/main" stop; }

# ---------------------------------------------------------------- 工具
effective_port() {
  local p
  p=$(as_package "$1" -c 'import json,sys
try: print(int(json.load(open(sys.argv[1])).get("port") or 8798))
except Exception: print(8798)' "$PKG_VAR/config.json" 2>/dev/null)
  case "$p" in ''|*[!0-9]*) p=8798 ;; esac
  printf '%s' "$p"
}

http_health() {
  local py="$1" port="$2"
  [ -n "$py" ] && [ -n "$port" ] || return 1
  as_package "$py" - "$port" <<'PYEOF'
import socket, ssl, sys
port = int(sys.argv[1])
# 先按 TLS 探（默认就是 TLS），不行再退回明文——两种模式的就绪判断共用这段
# 代码，不用去读配置。这是本机存活探测，不校验证书。
#
# recv 必须循环读：走 TLS 时响应会被拆成多个记录，只读一次往往只拿到响应头，
# 于是"服务明明起来了"却被判成没起来（明文下碰巧一次读全，所以这个坑只在
# 启用 TLS 之后才出现）。
for use_tls in (True, False):
    try:
        raw = socket.create_connection(("127.0.0.1", port), timeout=2)
    except Exception:
        continue
    try:
        s = raw
        if use_tls:
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            s = ctx.wrap_socket(raw, server_hostname="127.0.0.1")
        s.settimeout(2)
        s.sendall(b"GET /api/v1/health HTTP/1.0\r\nHost: x\r\n\r\n")
        buf = b""
        while len(buf) < 8192:
            try:
                chunk = s.recv(1024)
            except Exception:
                break
            if not chunk:
                break
            buf += chunk
            if b'"ok"' in buf:
                sys.exit(0)
    except Exception:
        pass
    finally:
        try:
            raw.close()
        except Exception:
            pass
sys.exit(1)
PYEOF
}
