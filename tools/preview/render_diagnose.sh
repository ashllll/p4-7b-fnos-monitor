#!/usr/bin/env bash
# 用法：render_diagnose.sh <preview 退出码> <preview 日志>
#
# 把"主机预览这一步为什么红"分成三类，并把证据行（最多 10 行）一起打出来：
#   0 = 没失败、也没有审计行
#   3 = 几何问题：越界 / 压叠 / 溢出（child out of parent、inline metric overflow、
#       text overflow、text overlap、list too short）
#   4 = 字形缺口（missing glyph）—— 这一种才该去补字库；与几何问题同时出现时取 4（两种都会说明）
#   5 = 渲染非零退出，但日志里没有任何审计关键字（崩溃 / 断言 / 环境问题）——
#       没有证据就不许说成字形问题
#
# 为什么要独立成脚本：verify_all.sh 以前在渲染非零退出时直接印一句
# "渲染失败（多为字形缺口，跑 bash tools/gen_fonts.sh 再试）" 然后 exit 1，
# 于是它下面那段按关键字分诊的代码**永远走不到**，越界/压叠/溢出被一律说成缺字，
# 用户照着补一遍字库还是红（审计 M29）。关键字与分诊现在只有这一份，且可被单测直接喂日志。
set -uo pipefail
rc="${1-}"
log="${2-}"
if [ "$#" != 2 ] || [ -z "$rc" ] || [ -z "$log" ]; then
  echo "用法：render_diagnose.sh <preview 退出码> <preview 日志>" >&2
  exit 2
fi
case "$rc" in
  *[!0-9]*|????*) echo "预览退出码无效" >&2; exit 2 ;;
esac
if [ "$rc" -gt 255 ]; then echo "预览退出码无效" >&2; exit 2; fi
if [ ! -f "$log" ] || [ ! -r "$log" ]; then
  echo "找不到预览日志：$log"
  exit 5
fi
GEOM_RE='child out of parent|inline metric overflow|text overflow|text overlap|list too short'
GLYPH_RE='missing glyph'
geom_rc=0; glyph_rc=0
geom=$(grep -icE "$GEOM_RE" "$log" 2>/dev/null) || geom_rc=$?
glyph=$(grep -icE "$GLYPH_RE" "$log" 2>/dev/null) || glyph_rc=$?
if [ "$geom_rc" -gt 1 ] || [ "$glyph_rc" -gt 1 ]; then
  echo "预览日志读取/匹配失败，无法判定" >&2
  exit 5
fi
geom=${geom:-0}; glyph=${glyph:-0}
head_txt="几何/字形审计有失败项"
[ "$rc" = "0" ] || head_txt="渲染失败（rc=${rc}）"
if [ "$geom" -gt 0 ] || [ "$glyph" -gt 0 ]; then
  if [ "$glyph" -gt 0 ] && [ "$geom" -gt 0 ]; then
    what="**既有字形缺口、也有几何问题**（各 $glyph / $geom 行）—— 先补字库（bash tools/gen_fonts.sh），几何问题另查越界/压叠/溢出"
    code=4
  elif [ "$glyph" -gt 0 ]; then
    what="**字形缺口**（$glyph 行）—— 跑 bash tools/gen_fonts.sh 再试"
    code=4
  else
    what="**几何问题**（越界/压叠/溢出，$geom 行），不是字形缺口，别去补字库"
    code=3
  fi
  echo "${head_txt}：$what"
  grep -iE "$GEOM_RE|$GLYPH_RE" "$log" 2>/dev/null | head -10 | sed 's/^/  /'
  exit "$code"
fi
if [ "$rc" -eq 0 ]; then
  exit 0
fi
echo "渲染失败（rc=${rc}），日志里没有审计关键字（崩溃/断言/环境问题？）："
tail -10 "$log" | sed 's/^/  /'
exit 5
