#!/usr/bin/env bash
# 回归：渲染失败的分诊不许把几何问题说成缺字（审计 M29）
#
# verify_all.sh 原来在 `preview` 非零退出时**直接**印一句
#   "渲染失败（多为字形缺口，跑 bash tools/gen_fonts.sh 再试）" 然后 exit 1，
# 而它下面那段按关键字分诊的代码因此永远走不到。后果是：越界/压叠/溢出
# （child out of parent、inline metric overflow、text overflow…）也会被指去补字库，
# 照着做一遍还是红。分诊逻辑现在搬进 tools/preview/render_diagnose.sh，这里直接喂 8 种日志。
#
# 全部在临时目录里造日志，不跑编译、不碰真机。
set -euo pipefail
cd "$(dirname "$0")/../.." || exit 2
TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT
DIAG=tools/preview/render_diagnose.sh
fail=0
bad() { printf '  ✗ %s\n' "$1"; fail=1; }

# check <名字> <期望退出码> <rc 参数> <日志内容> <必须出现> <禁止出现>
check() {
  local name="$1" want="$2" rc="$3" body="$4" must="$5" mustnot="$6"
  local log="$TMP/$(echo "$name" | tr -c 'a-zA-Z0-9' '_').log"
  printf '%s\n' "$body" >"$log"
  local out got
  got=0; out=$(bash "$DIAG" "$rc" "$log" 2>&1) || got=$?
  if [ "$got" != "$want" ]; then
    bad "${name}：期望退出码 ${want}，实际 ${got}（输出：$(printf '%s' "$out" | head -1)）"
    return
  fi
  if [ -n "$must" ] && ! printf '%s' "$out" | grep -q "$must"; then
    bad "${name}：输出里没有「${must}」—— $(printf '%s' "$out" | head -1)"
    return
  fi
  if [ -n "$mustnot" ] && printf '%s' "$out" | grep -q "$mustnot"; then
    bad "${name}：输出里不该出现「${mustnot}」—— $(printf '%s' "$out" | head -1)"
    return
  fi
  printf '  ✓ %s\n' "$name"
}

# 1) 字形缺口：唯一该建议补字库的一类
check "字形缺口 → 建议补字库" 4 1 \
  "preview: missing glyph U+4E2D in font 16" "字形缺口" ""
# 2) 几何三类 + 两类容易漏的（overflow ≠ overlap）
check "child out of parent → 几何" 3 1 \
  "child out of parent: card 3 x=300 w=120" "几何问题" "gen_fonts"
check "inline metric overflow → 几何" 3 1 \
  "inline metric overflow: label 宽度 120 > 容器 110" "几何问题" "gen_fonts"
check "text overflow → 几何" 3 1 \
  "text overflow: 24px 字宽超过控件" "几何问题" "gen_fonts"
check "text overlap → 几何" 3 1 \
  "text overlap: [12,40 90x20] 与 [90,40 90x20]" "几何问题" "gen_fonts"
check "list too short → 几何" 3 1 \
  "list too short: 行池 3 < 需要 5" "几何问题" "gen_fonts"
# 3) 渲染成功但审计有行：同样是失败，且不指去补字库
check "rc=0 但有几何行 → 仍是几何" 3 0 \
  "child out of parent: card 1" "几何/字形审计有失败项" "gen_fonts"
# 4) 两类同时出现：两种都要说，退出码取字形（可操作的那一步）
check "几何+字形同时出现 → 两种都说明" 4 1 \
  "missing glyph U+3002
child out of parent: card 2" "既有字形缺口、也有几何问题" ""
# 5) 没有关键字：不许猜成字形问题
check "崩溃且无关键字 → 不说字形" 5 1 \
  "Segmentation fault: 11" "没有审计关键字" "gen_fonts"
# 6) 干净通过：静默 exit 0
check "无失败无审计行 → 0" 0 0 "preview: 见过的构件 1676 个" "" ""
# 7) 日志不存在：报出来，别静默通过
got=0; out=$(bash "$DIAG" 1 "$TMP/nope.log" 2>&1) || got=$?
if [ "$got" != 5 ]; then bad "日志缺失：期望 5，实际 $got"; else printf '  ✓ 日志缺失 → 5（不静默通过）\n'; fi
# 8) 参数不全：用法错误退出 2
got=0; out=$(bash "$DIAG" 2>&1) || got=$?
if [ "$got" != 2 ]; then bad "缺参数：期望 2，实际 $got"; else printf '  ✓ 缺参数 → 2\n'; fi
# 9) 负向对照：老的那句误导性文案不许再出现
printf "%s\n" "text overflow: anonymous" >"$TMP/geometry.log"
got=0; out=$(bash "$DIAG" 1 "$TMP/geometry.log" 2>&1) || got=$?
[ "$got" = 3 ] || bad "几何对照未走到几何分支"
if printf '%s' "$out" | grep -q "多为字形缺口"; then
  bad "负向对照失败：几何问题仍然被告知「多为字形缺口」"
else
  printf '  ✓ 负向对照：几何问题不再被指去补字库（旧文案「多为字形缺口」不出现）\n'
fi

for rc in bad -1 256 999999; do
  got=0; bash "$DIAG" "$rc" "$TMP/geometry.log" >/dev/null 2>&1 || got=$?
  [ "$got" = 2 ] || bad "非法退出码未拒绝"
done
mkdir "$TMP/bin"
printf '#!/bin/sh\nexit 2\n' >"$TMP/bin/grep"; chmod +x "$TMP/bin/grep"
got=0; PATH="$TMP/bin:$PATH" bash "$DIAG" 0 "$TMP/geometry.log" >/dev/null 2>&1 || got=$?
[ "$got" = 5 ] || bad "日志读取错误假绿"
# 去读错误判断的负控：同样grep失败时会静默返回0。
python3 - "$DIAG" "$TMP/old.sh" <<'PYNEG'
import sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
s=s.replace('if [ "$geom_rc" -gt 1 ] || [ "$glyph_rc" -gt 1 ]; then','if false; then')
Path(sys.argv[2]).write_text(s)
PYNEG
got=0; PATH="$TMP/bin:$PATH" bash "$TMP/old.sh" 0 "$TMP/geometry.log" >/dev/null 2>&1 || got=$?
[ "$got" = 0 ] || bad "旧日志读取假绿负控未区分"
if [ "$fail" = 0 ]; then
  echo "PASS: 渲染失败按关键字分诊（字形 / 几何 / 无关键字三分），几何问题不再被误报成缺字"
else
  echo "FAIL: 渲染分诊回归未通过（见上面 ✗）"
fi
exit "$fail"
