#!/usr/bin/env bash
# 全组合 measure（口径 v2：S=滚动列表 / E=省略号 / C=设计折行 / !=真缺陷）
# 注意：--dump-dom 测量窗口吃 87px 窗口外框 → 窗口高必须 H+87：
#   目标视口 1024x600 → 窗口 1024x687；800x480 → 800x567；1280x800 → 1280x887
# 用法：bash measure-all.sh > measure-report.txt 2>/dev/null
set -u
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
cd "$(dirname "$0")"

combos=(
  "overview alert" "storage alert" "network live" "system alert" "temp alert"
  "diag stale" "pair live" "overview live" "overview stale" "overview sparse"
  "temp oldagent" "system sparse"
)
wins=("1024x687" "800x567" "1280x887")

for w in "${wins[@]}"; do
  W="${w%x*}"; H="${w#*x}"
  for c in "${combos[@]}"; do
    set -- $c; page="$1"; fix="$2"
    dom=$("$CHROME" --headless --disable-gpu --hide-scrollbars \
      --window-size=${W},${H} --virtual-time-budget=4000 --dump-dom \
      "file://$PWD/render.html?page=${page}&fixture=${fix}&measure=1" 2>/dev/null)
    block=$(printf '%s' "$dom" | python3 -c '
import re,sys,html
m=re.search(r"<pre id=\"measure\">(.*?)</pre>",sys.stdin.read(),re.S)
print(html.unescape(m.group(1)) if m else "NO-MEASURE-BLOCK")')
    echo "== ${page}/${fix} @${W}x${H} (target ${W}x$((H-87)))"
    echo "$block" | sed 's/^/   /'
  done
done
