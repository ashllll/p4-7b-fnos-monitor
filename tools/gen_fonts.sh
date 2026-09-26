#!/usr/bin/env bash
# 生成仪表盘用的 LVGL 字体（IBM Plex Sans/Mono + Noto Sans SC 子集）。
#
# 为什么要有这个脚本：界面标签是中文 + 拉丁数字混排，字体必须覆盖代码里真正用到的字形，
# 而且要在换字号/换字体时能一键复现。中文字符集直接从源码里扫出来，不手写清单。
#
# 依赖：node/npm（用 npx 拉 lv_font_conv@1.5.2）、tools/fonts/ 下的 TTF。
# 用法：bash tools/gen_fonts.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/tools/fonts"
OUT="$ROOT/components/fnos_monitor/fonts"
LV="npx --yes lv_font_conv@1.5.2"

mkdir -p "$OUT"
cd "$SRC"

# 1) 只从界面源码的**字符串字面量**里扫中文（先剥掉注释）：
#    注释里也有大量中文，全扫进来会让字体白胖好几倍（562 vs 约 90 个字）。
CJK=$(python3 - "$ROOT" <<'PYEOF'
import io, os, re, sys
root = sys.argv[1]
UI = ['components/fnos_monitor/fnos_view.c', 'components/fnos_monitor/fnos_ui.c',
      'components/fnos_monitor/fnos_ui.h', 'components/fnos_monitor/fnos_view.h']
pat = re.compile(r'[\u3000-\u303f\u4e00-\u9fff\uff00-\uffef]')

def keep_strings(s):
    """只保留字符串字面量，剥掉 // 与 /* */ 注释。"""
    out, i, n = [], 0, len(s)
    while i < n:
        if s.startswith('/*', i):
            j = s.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif s.startswith('//', i):
            j = s.find(chr(10), i)
            i = n if j < 0 else j
        elif s[i] in ('"', chr(39)):
            q = s[i]; j = i + 1
            while j < n and s[j] != q:
                j += 2 if s[j] == chr(92) else 1
            out.append(s[i:j + 1]); i = j + 1
        else:
            i += 1
    return ''.join(out)

chars = set()
for rel in UI:
    p = os.path.join(root, rel)
    if os.path.exists(p):
        chars.update(pat.findall(keep_strings(io.open(p, encoding='utf-8', errors='ignore').read())))
print(''.join(sorted(chars)))
PYEOF
)
echo "中文字形数: $(printf '%s' "$CJK" | python3 -c 'import sys;print(len(sys.stdin.read().strip()))')"

echo "== 等宽数字（数值列，tabular） =="
$LV --font PlexMono-SemiBold.ttf --size 84 --bpp 4 --format lvgl --force-fast-kern-format \
    --symbols "0123456789.,:%-+/ " --lv-include lvgl.h -o "$OUT/ui_font_mono_84.c"
$LV --font PlexMono-SemiBold.ttf --size 52 --bpp 4 --format lvgl --force-fast-kern-format \
    -r 0x20-0x7E --lv-include lvgl.h -o "$OUT/ui_font_mono_52.c"
$LV --font PlexMono-Medium.ttf --size 34 --bpp 4 --format lvgl --force-fast-kern-format \
    -r 0x20-0x7E --lv-include lvgl.h -o "$OUT/ui_font_mono_34.c"
$LV --font PlexMono-Medium.ttf --size 20 --bpp 4 --format lvgl --force-fast-kern-format \
    -r 0x20-0x7E --lv-include lvgl.h -o "$OUT/ui_font_mono_20.c"

echo "== 拉丁标签（Plex Sans） =="
$LV --font PlexSans-SemiBold.ttf --size 24 --bpp 4 --format lvgl --force-fast-kern-format \
    -r 0x20-0x7E -r 0xB0 -r 0xB7 --lv-include lvgl.h -o "$OUT/ui_font_sans_24.c"
$LV --font PlexSans-Medium.ttf --size 20 --bpp 4 --format lvgl --force-fast-kern-format \
    -r 0x20-0x7E -r 0xB0 -r 0xB7 --lv-include lvgl.h -o "$OUT/ui_font_sans_20.c"
$LV --font PlexSans-Regular.ttf --size 15 --bpp 4 --format lvgl --force-fast-kern-format \
    -r 0x20-0x7E -r 0xB0 -r 0xB7 --lv-include lvgl.h -o "$OUT/ui_font_sans_15.c"

echo "== 中文标签（Noto Sans SC 子集，含 ASCII 便于混排） =="
for spec in "40:Medium:ui_font_cjk_40" "24:Medium:ui_font_cjk_24" "20:Medium:ui_font_cjk_20" "15:Regular:ui_font_cjk_15"; do
  size="${spec%%:*}"; rest="${spec#*:}"; weight="${rest%%:*}"; name="${rest#*:}"
  $LV --font "NotoSansSC-${weight}.ttf" --size "$size" --bpp 4 --format lvgl --force-fast-kern-format \
      -r 0x20-0x7E -r 0xB0 -r 0xB7 --symbols "$CJK" --lv-include lvgl.h -o "$OUT/$name.c"
done

# 2) 统一的声明头
{
  echo "// 由 tools/gen_fonts.sh 生成，请勿手改。"
  echo "#pragma once"
  echo "#include \"lvgl.h\""
  echo
  for f in "$OUT"/ui_font_*.c; do
    b=$(basename "$f" .c)
    echo "LV_FONT_DECLARE($b);"
  done
} > "$ROOT/components/fnos_monitor/fnos_fonts.h"   # 声明头放在组件根目录（include 路径）

echo "== 产物 =="
ls -la "$OUT" | awk '{print $5, $9}'
du -sh "$OUT"
