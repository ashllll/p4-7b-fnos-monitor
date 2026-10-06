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

resolve_python() {
  for c in /var/apps/python312/target/bin/python3 /var/apps/python312/target/bin/python; do
    [ -x "$c" ] && { printf '%s' "$c"; return 0; }
  done
  command -v python3 2>/dev/null && return 0
  return 1
}

# connect 探测：不 bind、不占位、不 kill，只判断有没有人在听
port_busy() {
  (exec 3<>"/dev/tcp/127.0.0.1/$1") >/dev/null 2>&1
}

is_running() {
  [ -f "$PID_FILE" ] || return 1
  local pid
  pid=$(cat "$PID_FILE" 2>/dev/null)
  case "$pid" in ''|*[!0-9]*) return 1 ;; esac
  # 用 kill -0 判断存活：POSIX 通用，不依赖 /proc（本地 macOS 上也能跑通测试）
  kill -0 "$pid" 2>/dev/null || return 1
  # 防 PID 复用：Linux 上再确认这个 pid 确实是本应用的服务进程
  if [ -r "/proc/$pid/cmdline" ]; then
    tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -q "nas_companion_server.py" || return 1
  fi
  return 0
}

cleanup_socket() {
  [ -S "$SOCK_FILE" ] && rm -f "$SOCK_FILE"
  return 0
}

# 把向导/配置值合并进 config.json：关键是保留已配对设备，不能整体覆盖
apply_config() {
  local py="$1" port="$2" bind="$3" auth="$4" docker="$5" interval="$6" tls="${7:-true}"
  "$py" - "$PKG_VAR/config.json" "$port" "$bind" "$auth" "$docker" "$interval" "$tls" <<'PYEOF'
import json, os, sys
path, port, bind, auth, docker, interval, tls = sys.argv[1:8]
cfg = {}
try:
    with open(path, "r", encoding="utf-8") as f:
        cfg = json.load(f) or {}
except Exception:
    cfg = {}
if not isinstance(cfg, dict):
    cfg = {}
devices = cfg.get("devices") if isinstance(cfg.get("devices"), list) else []
cfg.update({
    "port": int(port), "bind": bind, "auth_mode": auth,
    "docker_enabled": (docker == "true"), "interval": float(interval),
    "tls_enabled": (tls == "true"),
})
cfg.setdefault("hist_len", 300)
cfg["devices"] = devices                     # 重新配置不能把开发板踢下线
os.makedirs(os.path.dirname(path), exist_ok=True)
tmp = path + ".tmp"
fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
with os.fdopen(fd, "w", encoding="utf-8") as f:
    json.dump(cfg, f, ensure_ascii=False, indent=2)
os.replace(tmp, path)
print("config.json: port=%s bind=%s auth=%s docker=%s interval=%s tls=%s devices=%d"
      % (port, bind, auth, docker, interval, tls, len(devices)))
PYEOF
}

# 归一化向导输入（值都是字符串，必须在这里再校验一遍）
norm_wizard() {
  # 端口优先级：向导里填的 > 系统分配的 TRIM_SERVICE_PORT > 8798。
  # 反过来会把用户在向导里的选择悄悄覆盖掉，所以向导优先。
  W_PORT="${wizard_port:-${TRIM_SERVICE_PORT:-8798}}"
  case "$W_PORT" in ''|*[!0-9]*) W_PORT=8798 ;; esac
  if ! { [ "$W_PORT" -ge 1 ] && [ "$W_PORT" -le 65535 ]; }; then W_PORT=8798; fi

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

  W_INTERVAL="${wizard_interval:-1.0}"
  case "$W_INTERVAL" in ''|*[!0-9.]*) W_INTERVAL=1.0 ;; esac
}

# 服务控制统一走 cmd/main：stop/start 的判定逻辑（PID 校验、端口冲突、
# 就绪等待）只写一份，避免这里和 main 各写一套、日后改一处漏一处。
CMD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
service_start() { "$CMD_DIR/main" start; }
service_stop()  { "$CMD_DIR/main" stop; }

# ---------------------------------------------------------------- 工具
effective_port() {
  local p
  p=$("$1" -c 'import json,sys
try: print(int(json.load(open(sys.argv[1])).get("port") or 8798))
except Exception: print(8798)' "$PKG_VAR/config.json" 2>/dev/null)
  case "$p" in ''|*[!0-9]*) p=8798 ;; esac
  printf '%s' "$p"
}

http_health() {
  local py="$1" port="$2"
  [ -n "$py" ] && [ -n "$port" ] || return 1
  "$py" - "$port" <<'PYEOF'
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

