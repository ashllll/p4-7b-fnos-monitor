#!/usr/bin/env bash
# 在 NAS 上跑一次**只读**的"温度命名试算"：不动正在跑的采集器、不写 NAS 上的文件、
# 不占 8799 端口。用来在正式部署前先看清"这一版会把每一路温度叫成什么"。
#
# 用法：
#   bash nas/preview-naming.sh user@nas.example.test
#   bash nas/preview-naming.sh user@host            # 指定目标
#   SSHPASS='...' bash nas/preview-naming.sh        # 密码认证（本机装了 sshpass 时）
#
# 输出就是采集端的 --temps 表：设备名(dn) / 通道(ch) / °C / 内核名(dev)。
# 觉得没问题再跑同目录的 ./install.sh 真正部署（部署后板子不用重烧就会显示这些名字）。
set -euo pipefail

TARGET="${1:?Usage: preview-naming.sh user@nas-host}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REMOTE=/tmp/fnos-agent-preview-$$.py

SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR
          -o ConnectTimeout=8)

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

echo "==> 把 agent 拷到 ${TARGET}:${REMOTE}（只放 /tmp，跑完就删）"
run_scp -q "$HERE/fnos-agent.py" "$TARGET:$REMOTE"
# trap 保证异常退出也会清理；--temps 只读 /sys 与 /proc，不启服务
trap 'run_ssh "$TARGET" "rm -f $REMOTE" >/dev/null 2>&1 || true' EXIT

echo "==> 只读试算（--temps）"
run_ssh "$TARGET" "python3 $REMOTE --temps"

echo
echo "==> 觉得名字对，就部署：cd $(dirname "$HERE")/nas && ./install.sh"
echo "    （部署后板子无需重烧：面板会把 NIC 这类类型名换成上面的设备名）"
