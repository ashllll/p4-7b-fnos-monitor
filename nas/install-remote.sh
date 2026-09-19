#!/usr/bin/env bash
# 在 NAS 上以 root 运行：安装/卸载 fnos-agent。由 install.sh 通过 scp+ssh 投送。
# 用法: bash install-remote.sh install  /tmp/fnos-agent.py /tmp/fnos-agent.service
#       bash install-remote.sh uninstall
set -euo pipefail

ACTION="${1:-install}"
PY_SRC="${2:-/tmp/fnos-agent.py}"
UNIT_SRC="${3:-/tmp/fnos-agent.service}"
PY_DST="/usr/local/bin/fnos-agent.py"
UNIT_DST="/etc/systemd/system/fnos-agent.service"

if [ "$ACTION" = "uninstall" ]; then
  systemctl disable --now fnos-agent.service 2>/dev/null || true
  rm -f "$UNIT_DST" "$PY_DST"
  systemctl daemon-reload
  echo "uninstalled"
  exit 0
fi

[ -f "$PY_SRC" ] || { echo "missing $PY_SRC" >&2; exit 1; }
[ -f "$UNIT_SRC" ] || { echo "missing $UNIT_SRC" >&2; exit 1; }

install -m 0755 -o root -g root "$PY_SRC" "$PY_DST"
install -m 0644 -o root -g root "$UNIT_SRC" "$UNIT_DST"
python3 -m py_compile "$PY_DST"
systemctl daemon-reload
systemctl enable fnos-agent.service >/dev/null
systemctl restart fnos-agent.service
sleep 2
systemctl is-active fnos-agent.service
systemctl --no-pager -l status fnos-agent.service | head -12
