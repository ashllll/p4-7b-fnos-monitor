#!/bin/bash
# 批量出图：用法 bash render-all.sh（在 tools/mockup-v11 下执行）
set -e
cd "$(dirname "$0")"
CHROME="${FNOS_CHROME_BIN:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
mkdir -p renders

# 名称 页 夹具 宽 高
while read -r name page fixture w h; do
  echo "$name $page $fixture $w $h"
done <<'EOF' | xargs -P 4 -n 5 bash -c '
  CHROME="${FNOS_CHROME_BIN:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
  "$CHROME" --headless --disable-gpu --hide-scrollbars --window-size=$4,$5 \
    --virtual-time-budget=4000 --screenshot=renders/$1.png \
    "file://$PWD/render.html?page=$2&fixture=$3" 2>/dev/null
  echo "$1.png $(stat -f%z renders/$1.png)B"
' _
01-overview-alert overview alert 1024 600
02-storage-alert storage alert 1024 600
03-network-live network live 1024 600
04-system-alert system alert 1024 600
05-temp-alert temp alert 1024 600
06-diag-stale diag stale 1024 600
07-pair-live pair live 1024 600
08-overview-live overview live 1024 600
09-overview-stale overview stale 1024 600
10-overview-sparse overview sparse 1024 600
11-temp-oldagent temp oldagent 1024 600
12-system-sparse system sparse 1024 600
13-overview-800x480 overview alert 800 480
14-storage-800x480 storage alert 800 480
15-temp-800x480 temp alert 800 480
16-overview-1280x800 overview alert 1280 800
17-storage-1280x800 storage alert 1280 800
18-temp-1280x800 temp alert 1280 800
EOF
