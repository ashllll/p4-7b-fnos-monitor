#!/usr/bin/env bash
# 三套配色 × 四页 的对比图：同一份几何、同一份数据，只换调色板。
# 用法：bash tools/preview/palettes.sh          （输出 tools/preview/palette-out/<名字>/）
#
# 为什么用同一份 preview 二进制：配色是运行时读的表（ui_kit/uk_theme.c），
# 如果每套配色各编一份、各写一份页面代码，"换个颜色顺手改坏对齐"就防不住了。
set -euo pipefail
set -f  # 配色名单按空白分词，禁止 glob 在校验之前展开。
cd "$(dirname "$0")"

PALETTES="${KK_PALETTES:-graphite abyss phosphor}"
OUT="palette-out"
BIN="build/preview"

count=0
for p in $PALETTES; do
    case "$p" in
        ""|*[!A-Za-z0-9_-]*) echo "!! 配色名非法" >&2; exit 2 ;;
    esac
    count=$((count+1))
done
if [ "$count" = 0 ]; then echo "!! 配色名单为空" >&2; exit 2; fi
if [ -L "$OUT" ]; then echo "!! 输出目录不能是软链" >&2; exit 2; fi
mkdir -p "$OUT"
for p in $PALETTES; do
    # 名字直接进 rm -rf 的路径（审计 M28）：KK_PALETTES='../../components' 会删到仓库里，
    # KK_PALETTES='*' 会被 glob 展开，rm -rf 拿到一堆路径。只收 [A-Za-z0-9_-]+。
    case "$p" in
        ""|*[!A-Za-z0-9_-]*) echo "!! 配色名非法：'$p'（只允许字母/数字/下划线/连字符）" >&2; exit 2 ;;
    esac
    rm -rf "${OUT:?}/${p}"; mkdir -p "$OUT/$p"
    KK_PALETTE="$p" "$BIN" "$OUT/$p" >/dev/null 2>"$OUT/$p/.log"
    if ! grep -q "palette: $p" "$OUT/$p/.log"; then
        echo "!! 配色 '${p}' 没生效（日志里没有 palette: ${p}）"; exit 1
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
