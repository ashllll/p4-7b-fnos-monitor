#!/usr/bin/env bash
# 在 NAS 上以 root 运行：安装/升级/卸载 fnos-agent。由 install.sh 通过 scp+ssh 投送。
#
# 用法:
#   bash install-remote.sh install  [<py 源>] [<unit 源>]   # 首次安装与升级共用一条路径
#   bash install-remote.sh rotate-token                     # 显式换令牌（只打指纹，不打令牌）
#   bash install-remote.sh uninstall [--purge-token]        # 默认保留令牌文件
#
# 令牌从哪来（审计 M31）：fnos-agent.py 在没有 FNAS_TOKEN 时会**拒绝启动**（fail-closed），
# 而随包的 unit 里以前既没有 EnvironmentFile，也没有任何东西去创建 /etc/fnos-agent.env，
# 于是"照文档装完就能用"其实装不起来。现在这条路径是这样的：
#   · 首次安装：在 NAS 上生成 24 字节随机令牌，写进 /etc/fnos-agent.env（0600，root:root）；
#   · 升级：文件已存在且令牌非空就**原样保留**（板子上配好的令牌不会因为升级失效）；
#   · 任何情况下都不把令牌打进 stdout/stderr/日志，只打指纹（sha256 前 8 位）；
#   · 绝不自动加 --insecure-no-auth —— 关鉴权必须是操作者显式做的决定。
#
# 测试缝（只为主机回归，真机上都不需要设）：
#   FNAS_INSTALL_ROOT   前缀根目录，用于临时根里跑整套安装
#   SYSTEMCTL           覆盖 systemctl 命令，用于桩掉服务管理器
#   FNAS_AGENT_AUTH_URL  覆盖鉴权自证请求的地址（默认 http://127.0.0.1:8799/api/v1/status）
set -euo pipefail

ACTION="${1:-install}"
case "$ACTION" in
  install|rotate-token|uninstall) ;;
  *) echo "Usage: install-remote.sh [install|rotate-token|uninstall]" >&2; exit 2 ;;
esac
PY_SRC="${2:-/tmp/fnos-agent.py}"
UNIT_SRC="${3:-/tmp/fnos-agent.service}"

ROOT="${FNAS_INSTALL_ROOT:-}"
SYSTEMCTL="${SYSTEMCTL:-systemctl}"
AUTH_URL="${FNAS_AGENT_AUTH_URL:-http://127.0.0.1:8799/api/v1/status}"
PY_DST="$ROOT/usr/local/bin/fnos-agent.py"
UNIT_DST="$ROOT/etc/systemd/system/fnos-agent.service"
ENV_DST="$ROOT/etc/fnos-agent.env"

say() { echo "[fnos-agent] $*"; }

# 真机上是 root：装出来的文件必须归 root。主机回归里跑不成 root，就不带 -o/-g
#（bash 3.2 下空数组展开在 set -u 里会直接报错，所以这里用 if 而不是数组）。
install_file() {   # $1=src $2=dst $3=mode
  if [ "$(id -u)" = 0 ]; then
    install -m "$3" -o root -g root "$1" "$2"
  else
    install -m "$3" "$1" "$2"
  fi
}

env_token() {   # 从现有环境文件里读令牌；读不到就打空
  [ -f "$ENV_DST" ] || return 0
  sed -n 's/^FNAS_TOKEN=//p' "$ENV_DST" | head -1
}

token_fingerprint() {   # 只输出 sha256 前 8 位，永不输出令牌本身
  python3 - "$ENV_DST" <<'PYEOF'
import hashlib, sys
tok = ""
try:
    with open(sys.argv[1], "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("FNAS_TOKEN="):
                tok = line.split("=", 1)[1].strip()
                break
except OSError:
    pass
print(hashlib.sha256(tok.encode()).hexdigest()[:8] if tok else "")
PYEOF
}

# 确保 /etc/fnos-agent.env 存在、0600、令牌非空。已存在且非空 = 保留。
ensure_token_file() {
  mkdir -p "$(dirname "$ENV_DST")"
  local keep
  keep="$(env_token)"
  if [ -n "$keep" ]; then
    chmod 600 "$ENV_DST"
    say "保留已有令牌（指纹 $(token_fingerprint)），升级不会让板子上的配置失效"
  else
    local tok
    tok="$(python3 -c 'import secrets; print(secrets.token_hex(24))')" || {
      echo "[fnos-agent] 生成令牌失败：NAS 上缺可用的 python3" >&2; return 1; }
    local tmp="$ENV_DST.new.$$"
    ( umask 077; printf '# 由 nas/install-remote.sh 生成；改这里就等于换令牌，改完 restart 服务\nFNAS_TOKEN=%s\n' "$tok" > "$tmp" )
    chmod 600 "$tmp"
    mv -f "$tmp" "$ENV_DST"
    say "已生成新令牌（指纹 $(token_fingerprint)），写入 ${ENV_DST}（0600）"
    unset tok
  fi
  if [ "$(id -u)" = 0 ]; then chown root:root "$ENV_DST"; fi
  # 0600 是硬要求：这个文件里放的是唯一凭据，权限不对等于把 8799 交出去。
  local mode
  mode="$(stat -c '%a' "$ENV_DST" 2>/dev/null || stat -f '%Lp' "$ENV_DST")"
  if [ "$mode" != "600" ]; then
    chmod 600 "$ENV_DST"
    say "令牌文件权限是 ${mode}，已改回 0600"
  fi
  return 0
}

# 断言要装的 unit 真的会加载令牌，且没有偷偷关掉鉴权。校验的是**源文件**：unit 有
# 问题就先报错退出，绝不先装到系统上再回滚（审计复核：失败路径不能留下坏 unit）。
verify_unit() {
  grep -Eq '^EnvironmentFile=-?/etc/fnos-agent\.env[[:space:]]*$' "$1" || {
    echo "[fnos-agent] unit 里没有 EnvironmentFile：服务起不来（fail-closed）" >&2; return 1; }
  if grep -q -- '--insecure-no-auth' "$1"; then
    echo "[fnos-agent] unit 里出现了 --insecure-no-auth：拒绝安装这种配置" >&2; return 1
  fi
  return 0
}

if [ "$ACTION" = "rotate-token" ]; then
  mkdir -p "$(dirname "$ENV_DST")"
  tok="$(python3 -c 'import secrets; print(secrets.token_hex(24))')"
  tmp="$ENV_DST.new.$$"
  ( umask 077; printf '# 由 nas/install-remote.sh 生成；改这里就等于换令牌，改完 restart 服务\nFNAS_TOKEN=%s\n' "$tok" > "$tmp" )
  chmod 600 "$tmp"
  mv -f "$tmp" "$ENV_DST"
  unset tok
  say "令牌已更换（指纹 $(token_fingerprint)）；在板子上更新后重启本服务"
  exit 0
fi

if [ "$ACTION" = "uninstall" ]; then
  if ! "$SYSTEMCTL" disable --now fnos-agent.service; then
    echo "[fnos-agent] 停止/禁用服务失败：保留代码、unit 与令牌，拒绝假报卸载成功" >&2
    exit 1
  fi
  rm -f "$UNIT_DST" "$PY_DST"
  "$SYSTEMCTL" daemon-reload
  if [ "${2:-}" = "--purge-token" ]; then
    rm -f "$ENV_DST"
    say "uninstalled（令牌文件已按 --purge-token 删除）"
  else
    say "uninstalled（$ENV_DST 保留：删除它请加 --purge-token）"
  fi
  exit 0
fi

[ -f "$PY_SRC" ] || { echo "[fnos-agent] missing $PY_SRC" >&2; exit 1; }
[ -f "$UNIT_SRC" ] || { echo "[fnos-agent] missing $UNIT_SRC" >&2; exit 1; }

mkdir -p "$ROOT/usr/local/bin" "$ROOT/etc/systemd/system"
# unit 先校验再落地：装完才发现 unit 不对，等于把 NAS 留在"服务起不来"的状态。
verify_unit "$UNIT_SRC"
# 令牌先就位再装 unit：反过来的话 restart 会先因为缺令牌而失败一次。
ensure_token_file
install_file "$PY_SRC" "$PY_DST" 0755
install_file "$UNIT_SRC" "$UNIT_DST" 0644
python3 -m py_compile "$PY_DST" || { echo "[fnos-agent] py_compile 失败：$PY_DST" >&2; exit 1; }
"$SYSTEMCTL" daemon-reload
"$SYSTEMCTL" enable fnos-agent.service >/dev/null
"$SYSTEMCTL" restart fnos-agent.service
sleep "${FNAS_AGENT_WAIT_S:-2}"
"$SYSTEMCTL" is-active fnos-agent.service
"$SYSTEMCTL" --no-pager -l status fnos-agent.service | head -12

# 鉴权自证：不带令牌打本机受保护的 /api/v1/status 必须被拒（401）。
# /api/v1/health 是故意公开的存活探针，不能拿它证明鉴权。看不到 401 就说明
# 令牌没生效（比如被 --insecure-no-auth 或空文件放过去了），这里要如实报错。
code="$(python3 - "$AUTH_URL" <<'PYEOF'
import sys, urllib.error, urllib.request
try:
    with urllib.request.urlopen(sys.argv[1], timeout=3) as r:
        print(r.status)
except urllib.error.HTTPError as e:
    print(e.code)
except Exception as e:
    print("ERR:%s" % e)
PYEOF
)"
case "$code" in
  401) say "鉴权已生效：不带令牌的 $AUTH_URL 返回 401" ;;
  200) echo "[fnos-agent] 没带令牌却拿到 200：鉴权没生效，请检查 $ENV_DST 与 unit 的 EnvironmentFile" >&2; exit 1 ;;
  *)   echo "[fnos-agent] 鉴权自证没拿到明确结果（${code}）：请看 systemctl status 与 journalctl -u fnos-agent" >&2; exit 1 ;;
esac
say "令牌放在 ${ENV_DST}（0600）。要在板子上配置时，在 NAS 上执行： sudo grep '^FNAS_TOKEN=' ${ENV_DST}"
say "令牌不会出现在本脚本的任何输出里；换令牌用： bash $0 rotate-token"
