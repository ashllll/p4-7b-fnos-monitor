#!/usr/bin/env bash
# 在 NAS 上跑一次温度命名试算：不动正在跑的采集器，仅写本次私有临时目录、
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

# 先通过可信渠道核对 NAS 主机指纹并加入 known_hosts；未知/变更密钥必须失败。
# 不复用已有 master：它可能由旧版绕过校验的脚本建立。
SSH_OPTS=(-o StrictHostKeyChecking=yes
          -o "UserKnownHostsFile=${FNOS_SSH_KNOWN_HOSTS:-$HOME/.ssh/known_hosts}"
          -o ControlMaster=no -o ControlPath=none -o LogLevel=ERROR
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

REMOTE_DIR=$(run_ssh "$TARGET" 'mktemp -d /tmp/fnos-preview.XXXXXXXXXX')
case "$REMOTE_DIR" in
  /tmp/fnos-preview.*) ;;
  *) echo "NAS 临时目录无效" >&2; exit 1 ;;
esac
case "${REMOTE_DIR#/tmp/fnos-preview.}" in
  ""|*[!A-Za-z0-9]*) echo "NAS 临时目录后缀无效" >&2; exit 1 ;;
esac
REMOTE="$REMOTE_DIR/agent.py"
# 仅清理验证过的本次目录；失败会使检查非零，不伪报全部成功。
cleanup() {
  local result=$?
  trap - EXIT
  if ! run_ssh "$TARGET" "rm -rf -- '$REMOTE_DIR'"; then
    echo "NAS 临时目录清理失败" >&2
    result=1
  fi
  exit "$result"
}
trap cleanup EXIT

echo "==> 把 agent 拷到本次 NAS 私有临时目录（跑完删除）"
run_scp -q "$HERE/fnos-agent.py" "$TARGET:$REMOTE"
echo "==> 只读试算（--temps）"
run_ssh "$TARGET" "python3 '$REMOTE' --temps"

echo
echo "==> 觉得名字对，就部署：cd $(dirname "$HERE")/nas && ./install.sh"
echo "    （部署后板子无需重烧：面板会把 NIC 这类类型名换成上面的设备名）"
