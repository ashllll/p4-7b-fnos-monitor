#!/usr/bin/env bash
# 从本机（macOS）把 fnos-agent 部署到飞牛 NAS。
#
# 需要：
#   NAS_HOST / NAS_USER （默认 192.168.0.119 / llll）
#   SSHPASS              ssh 密码（本机装了 sshpass 时用；不设则走公钥或已有的多路复用连接）
#   NAS_SUDO_PASS        sudo 密码（NAS 上 sudo 需要密码时）
#
# 例：
#   SSHPASS='***' NAS_SUDO_PASS='***' ./install.sh
#   SSHPASS='***' NAS_SUDO_PASS='***' ./install.sh uninstall
set -euo pipefail
cd "$(dirname "$0")"

NAS_HOST="${NAS_HOST:-192.168.0.119}"
NAS_USER="${NAS_USER:-llll}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR
          -o ConnectTimeout=8 -o ControlMaster=auto
          -o ControlPath=/tmp/nas-ctl-%r@%h:%p -o ControlPersist=600)
TARGET="${NAS_USER}@${NAS_HOST}"
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
  STAGE=$(run_ssh "$TARGET" 'd=$(mktemp -d) && chmod 700 "$d" && echo "$d"')
  [ -n "$STAGE" ] || { echo "无法在 NAS 上创建中转目录" >&2; exit 1; }
  run_scp -q fnos-agent.py fnos-agent.service install-remote.sh "$TARGET:$STAGE/"
  run_ssh "$TARGET" "mv $STAGE/install-remote.sh $STAGE/fnos-agent-install-remote.sh"
  run_ssh "$TARGET" 'rm -f /tmp/fnos-agent.py /tmp/fnos-agent.service /tmp/fnos-agent-install-remote.sh' || true
}

ACTION="${1:-install}"

step "投送文件到 $TARGET 的 0700 中转目录"
stage_on_nas

if [ "$ACTION" = "uninstall" ]; then
  step "卸载 NAS 上的 fnos-agent"
  run_root "bash $STAGE/fnos-agent-install-remote.sh uninstall"
  run_root "rm -rf $STAGE"
  exit 0
fi

step "以 root 安装并启动 systemd 服务"
run_root "bash $STAGE/fnos-agent-install-remote.sh install $STAGE/fnos-agent.py $STAGE/fnos-agent.service"
run_root "rm -rf $STAGE"

step "本机验证 http://$NAS_HOST:8799/api/v1/status"
curl -s -m 6 "http://$NAS_HOST:8799/api/v1/health" && echo
curl -s -m 6 -o /tmp/fnos-status.json -w 'http=%{http_code} bytes=%{size_download} time=%{time_total}s\n' \
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
