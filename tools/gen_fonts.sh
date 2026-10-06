#!/usr/bin/env bash
# 生成仪表盘用的 LVGL 字体（Inter 拉丁 + Noto Sans SC 中文子集）。
#
# 为什么要有这个脚本：界面标签是中文 + 拉丁数字混排，字体必须覆盖代码里真正用到的字形，
# 而且要在换字号/换字体时能一键复现。中文字符集直接从源码里扫出来，不手写清单。
#
# 依赖：node/npm（用 npx 拉 lv_font_conv@1.5.2）、tools/fonts/ 下的 TTF。
# 用法：bash tools/gen_fonts.sh
#
# ⚠️ 必须 --no-compress（RLE 压缩字体在这块板子上会"字体花屏 + 闪烁"）：
#   lv_font_conv 默认输出 RLE 压缩字体（描述符里 .bitmap_format = 1），而 LVGL 9.5
#   的解压器用的是**一份全局状态**（src/core/lv_global.h 的 LV_GLOBAL_DEFAULT()->font_fmt_rle，
#   rle_init/rle_next 都在它上面跑，font/fmt_txt/lv_font_fmt_txt.c 里没有任何锁）。
#   本工程开着 CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2，LVGL 会把一帧拆成两个 tile 用两个线程并行绘制，
#   两个线程同时解压不同字形就会互相踩状态 → 字形碎裂、每次重绘碎裂的位置还不一样（看起来就是闪烁）。
#   LVGL 内置的 Montserrat（.bitmap_format = 0，未压缩）没这个问题，所以 v1 界面是干净的，
#   换成自定义压缩字体后才暴露。代价是字体体积大 2~3 倍（本工程 10 个字库约 +0.3 MB Flash，分区绰绰有余）。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/tools/fonts"
OUT="$ROOT/components/fnos_monitor/fonts"
LV="npx --yes lv_font_conv@1.5.2"

mkdir -p "$OUT"
cd "$SRC"

# 本次要产出的字库（与下面每条生成命令一一对应）。生成完会删掉 OUT 里其余的 ui_font_*.c：
# 上一代字库留在目录里没有任何东西引用，却会被下面的 glob 声明进 fnos_fonts.h，白占仓库体积、
# 也让"到底哪些字体在用"变得难查（v1 的 9 个 sans/mono/cjk 字库就是这么残留了 ≈1.0 MB）。
# ui_font_txt_13 是**兼容窗口**里的老字号：已提交的 kk_ui（v5 版 kk_widgets.c）引用它，
# 正在写的 v6 版 kk_widgets.c 改引用 ui_font_txt_12。两者都产出，等 v6 落地后删掉这一项。
# v9 字号阶梯（4px 栅格友好：行高全部落在 4 的倍数附近）。
# 为什么不用"整档"字号：这块屏的版面合同按 4px 栅格写，字号必须让**行高**落进栅格
# （LVGL 里行高 = 版面的真实高度，字号只是名义值）。旧阶梯的 15px/18px 行高是 19/21，
# 两者差 2px，导致"同一行的两块文字差 1~2px"这种永远对不齐的错位。
EXPECT="ui_font_num_44 ui_font_num_32 ui_font_num_20 \
        ui_font_txt_12 ui_font_txt_15 \
        ui_font_cjk_20 ui_font_cjk_16 ui_font_cjk_12"

# 1) 只从界面源码的**字符串字面量**里扫中文（先剥掉注释）：
#    注释里也有大量中文，全扫进来会让字体白胖好几倍（562 vs 约 90 个字）。
#    清单必须覆盖"所有能把文字送上屏的文件"：fnos_ui.c/h（版面文案与格式化串）
#    + fnos_pair.c/h（配对状态机的 msg 会原样显示在配对卡上）
#    + kk_widgets.c（构件内建文案）+ main.cpp。v6 起 JSON 清单与代码生成物已删除。
#    style_proof.c 是主机试片（不进固件），但它也把中文送上屏 —— 一并扫，省得样张里
#    全是豆腐块（样张的价值就在"用眼睛定夺"，缺字会让人误判成渲染故障）。
#    漏一个文件就可能出现"库里有这个字、字库里没有"的豆腐块（v4.2 的"前"就是这么来的）。
#    对照检查：build 后看预览/实机截图，或 `strings` 扫 elf 里的中文字面量。
CJK=$(python3 - "$ROOT" <<'PYEOF'
import io, os, re, sys
root = sys.argv[1]
UI = ['components/fnos_monitor/fnos_ui.c', 'components/fnos_monitor/fnos_ui.h',
      'components/fnos_monitor/fnos_pair.c', 'components/fnos_monitor/fnos_pair.h',
      'components/fnos_monitor/kk_ui/kk_widgets.c',
      'tools/preview/preview.c',
      'tools/preview/style_proof.c',
      'main/main.cpp']
pat = re.compile(r'[\u2010-\u205e\u3000-\u303f\u4e00-\u9fff\uff00-\uffef]')

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
# 摘掉各种破折号（em dash — U+2014 / en dash – / 减号 − / 全角 －）。它们会被原样拼进
# --symbols，而 lv_font_conv 底层是 commander：**以非 ASCII 破折号开头的参数会被当成
# 命令行选项**，报 "arg Argument starts with non-ascii dash"，那个字库根本不会生成，
# 文件静默停在上一代版本（脚本却照旧往下走，还列出一份"产物"看上去像成功）。
# 破折号用 -r 显式范围带上，见每个生成命令里的 -r 0x2014。
dash = re.compile(r'[\u2013\u2014\u2212\uff0d]')
low  = sorted(c for c in chars if dash.match(c))
rest = sorted(c for c in chars if not dash.match(c))
for c in low:
    sys.stderr.write('避开命令行解析：U+%04X 走 -r 范围\n' % ord(c))
# 手工补一批"仪表盘常用词"：字库是从源码字符串里扫出来的，但**格式化串里的字**
# 也会上屏（"峰 %d%%"这种），如果某个字只出现在以后才会加的文案里，扫不到就是豆腐块。
# 这份清单只放仪表盘高频词，宁可多几个字形，也不要真机上出现方块。
extras = '峰均值高低总计率更新闻秒分时天年月日卷阵列盘温度负载进程交换容器服务网络上下行'
extras += '接收发送延迟成功失败诊断采集在线离线等待正常警告严重状态容量可用已空间'
chars.update(extras)
low  = sorted(c for c in chars if dash.match(c))
rest = sorted(c for c in chars if not dash.match(c))
for c in low:
    sys.stderr.write('避开命令行解析：U+%04X 走 -r 范围\n' % ord(c))
print(''.join(rest))
PYEOF
)
# 保险带：上面按 Unicode 语义摘，这里再按字符本身剥一遍（万一以后有人往字符集里
# 加了别的破折号变体，也不至于让字库整批生成失败）。
for CH in $'—' $'–' $'−' $'－'; do
  CJK="${CJK//$CH/}"
done
echo "中文字形数: $(printf '%s' "$CJK" | python3 -c 'import sys;print(len(sys.stdin.read().strip()))')"

# v6（UniFi）字号阶梯：KPI 数字一律 600（SemiBold），次级数字与标签 500（Medium），
# 元信息 400（Regular）。整套比 v5 小一档 —— 这是"克制"的一半，别随手调大。
echo "== 数值（Inter SemiBold） =="
$LV --font Inter-SemiBold.ttf --size 44 --bpp 4 --format lvgl --no-compress --force-fast-kern-format \
    -r 0x20-0x7E -r 0xB0 -r 0xB7 -r 0x2014 -r 0x2026 -r 0x2190-0x2193 -r 0x2264-0x2265 --lv-include lvgl.h -o "$OUT/ui_font_num_44.c"
$LV --font Inter-SemiBold.ttf --size 32 --bpp 4 --format lvgl --no-compress --force-fast-kern-format \
    -r 0x20-0x7E -r 0xB0 -r 0xB7 -r 0x2014 -r 0x2026 -r 0x2190-0x2193 -r 0x2264-0x2265 --lv-include lvgl.h -o "$OUT/ui_font_num_32.c"
$LV --font Inter-Medium.ttf --size 20 --bpp 4 --format lvgl --no-compress --force-fast-kern-format \
    -r 0x20-0x7E -r 0xB0 -r 0xB7 -r 0x2014 -r 0x2026 -r 0x2190-0x2193 -r 0x2264-0x2265 --lv-include lvgl.h -o "$OUT/ui_font_num_20.c"

echo "== 文本（Inter Regular / Medium） =="
$LV --font Inter-Medium.ttf --size 15 --bpp 4 --format lvgl --no-compress --force-fast-kern-format \
    -r 0x20-0x7E -r 0xB0 -r 0xB7 -r 0x2014 -r 0x2026 -r 0x2190-0x2193 -r 0x2264-0x2265 --lv-include lvgl.h -o "$OUT/ui_font_txt_15.c"
$LV --font Inter-Regular.ttf --size 12 --bpp 4 --format lvgl --no-compress --force-fast-kern-format \
    -r 0x20-0x7E -r 0xB0 -r 0xB7 -r 0x2014 -r 0x2026 -r 0x2190-0x2193 -r 0x2264-0x2265 --lv-include lvgl.h -o "$OUT/ui_font_txt_12.c"

echo "== 中文标签（Noto Sans SC 子集，含 ASCII 便于混排） =="
for spec in "20:Medium:ui_font_cjk_20" "16:Medium:ui_font_cjk_16" "12:Regular:ui_font_cjk_12"; do
  size="${spec%%:*}"; rest="${spec#*:}"; weight="${rest%%:*}"; name="${rest#*:}"
  $LV --font "NotoSansSC-${weight}.ttf" --size "$size" --bpp 4 --format lvgl --no-compress --force-fast-kern-format \
      -r 0x20-0x7E -r 0xB0 -r 0xB7 -r 0x2014 -r 0x2026 -r 0x2190-0x2193 -r 0x2264-0x2265 --symbols "$CJK" --lv-include lvgl.h -o "$OUT/$name.c"
done

# 2) 清理上一代产物（只保留本次生成的；EXPECT 之外的 ui_font_*.c 一律删）
for f in "$OUT"/ui_font_*.c; do
  [ -e "$f" ] || continue
  b="$(basename "$f" .c)"
  case " $EXPECT " in
    *" $b "*) ;;
    *) echo "清理上一代字库: $b.c"; rm -f "$f" ;;
  esac
done

# 3) 统一的声明头
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

# 4) 自检：某个字库命令失败时，下面"产物"列表照样会列出文件（那是上一代留下的），
#    看着像成功。所以这里逐个查"到底有没有字形"，缺一个就整体失败——豆腐块要在
#    生成阶段拦住，不能等预览里发现（v8 的"删除"就是这么缺的）。
echo "== 自检：每个字库都必须含字形 =="
for f in "$OUT"/ui_font_*.c; do
  b="$(basename "$f" .c)"
  n="$(grep -c 'glyph_dsc\[' "$f" || true)"
  if [ "${n:-0}" -lt 1 ] || ! grep -q '0x' "$f"; then
    echo "字库没有字形（生成失败？）: $b.c" >&2
    exit 1
  fi
  printf '  %-18s 有字形表\n' "$b"
done

echo "== 产物 =="
ls -la "$OUT" | awk '{print $5, $9}'
du -sh "$OUT"