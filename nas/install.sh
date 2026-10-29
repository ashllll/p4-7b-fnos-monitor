#!/usr/bin/env bash
# 从本机（macOS）把 fnos-agent 部署到飞牛 NAS。
#
# 需要：
#   NAS_HOST / NAS_USER （部署时显式指定）
#   SSHPASS              ssh 密码（本机装了 sshpass 时用；不设则走公钥或已有的多路复用连接）
#   NAS_SUDO_PASS        sudo 密码（NAS 上 sudo 需要密码时）
#
# 例：
#   SSHPASS='***' NAS_SUDO_PASS='***' ./install.sh
#   SSHPASS='***' NAS_SUDO_PASS='***' ./install.sh uninstall
set -euo pipefail
cd "$(dirname "$0")"

NAS_HOST="${NAS_HOST:?Set NAS_HOST to your NAS address}"
NAS_USER="${NAS_USER:?Set NAS_USER to your SSH user}"
# 先通过可信渠道核对 NAS 主机指纹并加入 known_hosts；未知/变更密钥必须失败。
# 不复用已有 master：它可能由旧版绕过校验的脚本建立。
SSH_OPTS=(-o StrictHostKeyChecking=yes
          -o "UserKnownHostsFile=${FNOS_SSH_KNOWN_HOSTS:-$HOME/.ssh/known_hosts}"
          -o ControlMaster=no -o ControlPath=none -o LogLevel=ERROR
          -o ConnectTimeout=8)
TARGET="${NAS_USER}@${NAS_HOST}"
ACTION="${1:-install}"
case "$ACTION" in
  install|uninstall|verify) ;;
  *) echo "Usage: install.sh [install|uninstall|verify]" >&2; exit 2 ;;
esac
SUDO_PASS="${NAS_SUDO_PASS:-}"

step() { printf '\033[36m==> %s\033[0m\n' "$*"; }

# 注意：这里刻意不用「数组 + 展开」的写法 —— macOS 自带的 bash 3.2 在 set -u 下对空数组
# 展开会直接报 unbound variable（bash 4.4 才修），"提前配好公钥"那条路径会 100% 失败。
run_ssh() {
  if [ -n "${SSHPASS:-}" ] && command -v sshpass >/dev/null 2>&1; then
    sshpass -e ssh "${SSH_OPTS[@]}" "$@"
  else
    ssh "${SSH_OPTS[@]}" "$@"
  fi
}

run_scp() {
  if [ -n "${SSHPASS:-}" ] && command -v sshpass >/dev/null 2>&1; then
    sshpass -e scp "${SSH_OPTS[@]}" "$@"
  else
    scp "${SSH_OPTS[@]}" "$@"
  fi
}

run_root() {   # $1 = 远端命令（不要在里面用单引号）
  if [ -n "$SUDO_PASS" ]; then
    printf '%s\n' "$SUDO_PASS" | run_ssh "$TARGET" "sudo -S -p '' bash -c '$1'"
  else
    run_ssh "$TARGET" "sudo -n bash -c '$1'"
  fi
}

# 中转目录必须是 0700：待安装的文件最终以 root 身份执行，不能经过世界可写的 /tmp
stage_on_nas() {
  STAGE=$(run_ssh "$TARGET" 'mktemp -d /tmp/fnos-install.XXXXXXXXXX')
  case "$STAGE" in
    /tmp/fnos-install.*) ;;
    *) echo "NAS 中转目录无效" >&2; exit 1 ;;
  esac
  case "${STAGE#/tmp/fnos-install.}" in
    ""|*[!A-Za-z0-9]*) echo "NAS 中转目录后缀无效" >&2; exit 1 ;;
  esac
  trap cleanup_stage EXIT
  run_scp -q fnos-agent.py fnos-agent.service install-remote.sh "$TARGET:$STAGE/"
  run_ssh "$TARGET" "mv \"$STAGE/install-remote.sh\" \"$STAGE/fnos-agent-install-remote.sh\""
}

cleanup_stage() {
  local result=$?
  trap - EXIT
  if ! run_root "rm -rf -- \"$STAGE\""; then
    echo "NAS 中转目录清理失败" >&2
    result=1
  fi
  exit "$result"
}

if [ "$ACTION" != "verify" ]; then
  step "投送文件到 $TARGET 的 0700 中转目录"
  stage_on_nas
  if [ "$ACTION" = "uninstall" ]; then
    step "卸载 NAS 上的 fnos-agent"
    run_root "bash \"$STAGE/fnos-agent-install-remote.sh\" uninstall"
    exit 0
  fi
  step "以 root 安装并启动 systemd 服务"
  run_root "bash \"$STAGE/fnos-agent-install-remote.sh\" install \"$STAGE/fnos-agent.py\" \"$STAGE/fnos-agent.service\""
fi

step "本机验证 http://$NAS_HOST:8799 —— 先证明鉴权真的生效"
# 审计 M31：令牌是 fail-closed 的，所以"不带令牌能拿到 200"才是故障而不是成功。
# /api/v1/health 是故意公开的存活探针，不能拿它证明鉴权；这里探受保护的
# /api/v1/status，必须看到 401；看到 200 说明令牌没生效（unit 缺
# EnvironmentFile、文件为空、或者谁加了 --insecure-no-auth），直接判失败。
NOAUTH_CODE=$(curl -s -m 6 -o /dev/null -w '%{http_code}' "http://$NAS_HOST:8799/api/v1/status" || true)
if [ "$NOAUTH_CODE" = "401" ]; then
  echo "  ✓ 不带令牌：HTTP 401（鉴权生效）"
else
  echo "  ✗ 不带令牌：HTTP ${NOAUTH_CODE:-无响应} —— 期望 401" >&2
  echo "    在 NAS 上核对：sudo systemctl cat fnos-agent | grep EnvironmentFile" >&2
  echo "                    sudo stat -c '%a %U' /etc/fnos-agent.env   # 应为 600 root" >&2
  exit 1
fi

# 令牌只在 NAS 上（0600），本机默认拿不到，所以字段汇总这一步默认是"未做"，
# 而不是假装通过。想看的话：在 NAS 上 sudo grep '^FNAS_TOKEN=' /etc/fnos-agent.env，
# 然后 FNAS_TOKEN=<值> ./install.sh verify 重跑本段。
if [ -z "${FNAS_TOKEN:-}" ]; then
  echo "  ! 未带令牌，跳过字段汇总（本机没有 /etc/fnos-agent.env 里的令牌）"
  echo "    要核对字段：在 NAS 上取令牌后 FNAS_TOKEN=<值> 重跑本脚本"
else
  # 令牌不进 argv（argv 对本机 ps 可见）：用 curl --config - 从 stdin 读头部。
  printf 'header = "X-Token: %s"\n' "$FNAS_TOKEN" \
    | curl -s -m 8 --config - -o /tmp/fnos-status.json \
        -w 'http=%{http_code} bytes=%{size_download} time=%{time_total}s\n' \
        "http://$NAS_HOST:8799/api/v1/status"
  python3 - <<'EOF'
import json
j = json.load(open('/tmp/fnos-status.json'))
print('host=%s ts=%s ready=%s uptime=%.1fh' % (j['host'], j['ts'], j.get('ready'), j['uptime_s']/3600))
print('cpu %s%% temp=%s load=%s runq=%s procs=%s' % (j['cpu']['pct'], j['cpu']['temp_c'], j['cpu']['load1'],
                                                     j['cpu'].get('runq'), j['cpu'].get('procs')))
print('mem %s%% of %s MB' % (j['mem']['pct'], j['mem']['total_mb']))
print('net %s rx=%s KB/s tx=%s KB/s' % (j['net']['if'], j['net']['rx_kbs'], j['net']['tx_kbs']))
print('vols: ' + ', '.join('%s %s%%(%sT)' % (v['mnt'], v['pct'], v['total_gb']/1024) for v in j['vols']))
print('raid: ' + ', '.join('%s/%s ok=%s %s have=%s' % (r['dev'], r['lvl'], r['ok'], r['state'], r['have']) for r in j['raid']))
print('temps: ' + ', '.join('%s %s' % (t['n'], t['c']) for t in j['temps']))
print('docker: ' + ', '.join('%s=%s' % (c['n'], 'up' if c['up'] else c['s']) for c in j['docker']))
print('zfs: %s' % j['zfs'])
print('alerts: %s' % json.dumps(j['alerts'], ensure_ascii=False))
print('errors: %s' % j.get('errors'))
EOF
fi
