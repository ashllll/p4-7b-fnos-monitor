#!/usr/bin/env bash
# 三套配色 × 四页 的对比图：同一份几何、同一份数据，只换调色板。
# 用法：bash tools/preview/palettes.sh          （输出 tools/preview/palette-out/<名字>/）
#
# 为什么用同一份 preview 二进制：配色是运行时读的表（ui_kit/uk_theme.c），
# 如果每套配色各编一份、各写一份页面代码，"换个颜色顺手改坏对齐"就防不住了。
set -euo pipefail
cd "$(dirname "$0")"

PALETTES="${KK_PALETTES:-graphite abyss phosphor}"
OUT="palette-out"
BIN="build/preview"

for p in $PALETTES; do
    rm -rf "$OUT/$p"; mkdir -p "$OUT/$p"
    KK_PALETTE="$p" "$BIN" "$OUT/$p" >/dev/null 2>"$OUT/$p/.log"
    if ! grep -q "palette: $p" "$OUT/$p/.log"; then
        echo "!! 配色 '$p' 没生效（日志里没有 palette: $p）"; exit 1
    fi
    # 几何审计：配色不该影响几何，只要有一处越界/缺字/重叠就说明"换色换坏了"
    if grep -qE "child out of parent|text overflow|missing glyph|text overlap" "$OUT/$p/.log"; then
        echo "!! 配色 '$p' 出现几何违规："; grep -E "child out of parent|text overflow|missing glyph|text overlap" "$OUT/$p/.log" | head -5
        exit 1
    fi
    # 滚动条净距看门狗
    if ! node scroll_gap.js "$OUT/$p" >"$OUT/$p/.gap" 2>&1; then
        echo "!! 配色 '$p' 滚动条净距不合格："; tail -3 "$OUT/$p/.gap"; exit 1
    fi
    python3 ppm2png.py "$OUT/$p" >/dev/null
    echo "== $p: $(grep -c . "$OUT/$p/.log") 行日志 / $(tail -1 "$OUT/$p/.gap")"
done
echo "对比图在 tools/preview/$OUT/<配色>/"
