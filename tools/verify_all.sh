#!/usr/bin/env bash
# 四级证据一键验收（见 docs/ui-v12-lvgl-native.md §7）：
#   L1 主机预览（版面/字库/越界/压叠）+ 自适应池几何
#   L2 固件构建 + 已登记入口静态栈深
#   L3 实机（--flash）：烧录 → 串口 30s 崩溃/复位扫描 + 堆
#   L3 拍屏（--photo）：capture-board 手机取景
#
# 用法：bash tools/verify_all.sh [--flash] [--photo] [--port /dev/cu.usbmodemXXXX]
# 任何一级失败即退出非零，并在最后打印 PASS/FAIL 汇总。
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
PROJ="$PWD"
OUT="${VERIFY_OUT:-/tmp/fnos-verify}"
FLASH=0; PHOTO=0; PORT="${FNOS_SERIAL_PORT:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --flash) FLASH=1 ;;
    --photo) PHOTO=1 ;;
    --port)
      [ $# -ge 2 ] && [ -n "$2" ] || { echo "--port requires a value" >&2; exit 2; }
      shift; PORT="$1" ;;
    *) echo "未知参数：$1"; exit 2 ;;
  esac
  shift
done

if [ "$FLASH" = 1 ] && [ -z "$PORT" ]; then
  echo "--flash requires --port or FNOS_SERIAL_PORT for the identified board" >&2
  exit 2
fi
if [ -n "${FNOS_IDF_ENV:-}" ]; then
  . "$FNOS_IDF_ENV" || exit 2
fi
# PREVIEW_* 会在 preview.c 里切成"只渲染子集"的模式（审计 M36）：残留一个就退 0 而审计没跑。
# Clear every exported preview selector, including newly introduced ones.
for preview_var in $(compgen -e); do
  case "$preview_var" in PREVIEW_*) unset "$preview_var" ;; esac
done
ok=0; fail=0
step() { printf '\n=== %s ===\n' "$1"; }
pass() { printf '  [PASS] %s\n' "$1"; ok=$((ok+1)); }
bad()  { printf '  [FAIL] %s\n' "$1"; fail=$((fail+1)); }

# Preserve pre-existing paths: never recursively delete a caller-provided output directory.
prepare_output() {
  mkdir -p -- "$1" || return 1
  mktemp -d "$1/run.XXXXXX"
}
# This validates the file only; physical display judgement belongs to the caller.
capture_board_photo() {
  python3 - "$1" <<'PY'
import json, shutil, sys
from pathlib import Path
sys.path.insert(0, 'tools')
from page_shot import capture_photo
from photo_check import decode_png
out = Path(sys.argv[1])
info = capture_photo()
image = out / 'board-photo.png'
shutil.copyfile(info['image'], image)
decode_png(image)
with (out / 'photo.json').open('w', encoding='utf-8') as target:
    json.dump(info, target)
print('Capture copied and decoded; visual acceptance remains pending')
PY
}

OUT=$(prepare_output "$OUT") || { echo "Cannot create verification output directory" >&2; exit 2; }
echo "验收结果目录：$OUT"

# All host checks share the report entry's registry, including the full preview.
step "L1 主机回归（共享清单）"
CHECK_INDEX=0
host_check() {
  local name="$1"; shift
  CHECK_INDEX=$((CHECK_INDEX+1))
  local log="$OUT/check-$CHECK_INDEX.log"
  if "$@" >"$log" 2>&1; then
    pass "$name"
  else
    bad "${name}（见 ${log}）"
    tail -5 "$log"
  fi
}
[ -r tools/host_checks.sh ] || { echo "Missing tools/host_checks.sh" >&2; exit 2; }
. tools/host_checks.sh || exit 2
fnos_host_checks host_check

if [ -n "${FNOS_BOARD_SIM_HOST:-}" ] && [ -n "${FNOS_BOARD_SIM_PORT:-}" ]; then
  if python3 nas/fpk/board_sim.py --host "$FNOS_BOARD_SIM_HOST" --port "$FNOS_BOARD_SIM_PORT" >"$OUT/board_sim.log" 2>&1; then
    pass "板子模拟器（对着真 NAS）"
  else
    bad "板子模拟器失败（见 $OUT/board_sim.log）"; tail -8 "$OUT/board_sim.log"
  fi
else
  echo "  [SKIP] 板子模拟器：没设 FNOS_BOARD_SIM_HOST/PORT（本机没有真 NAS，不算通过）"
fi

# ── L2：构建 + 栈深 ────────────────────────────────────────────────
step "L2 固件构建 + 栈深"
if ./idf.sh build >"$OUT/idf.log" 2>&1; then
  grep -E "binary size" "$OUT/idf.log" | tail -1 | sed 's/^/  /'
  pass "固件构建"
else
  bad "固件构建失败（见 $OUT/idf.log）"; tail -20 "$OUT/idf.log"; exit 1
fi
if python3 tools/stack_check.py >"$OUT/stack.log" 2>&1; then
  if grep -q "全部入口在预算内" "$OUT/stack.log"; then
    grep -E "合计|预算" "$OUT/stack.log" | sed 's/^/  /'
    pass "栈深在预算内"
  else
    bad "栈深超预算："; grep -E "合计" "$OUT/stack.log"; fi
else
  # 先看日志内容再归因（审计 M30）：stack_check.py 超预算时同样 return 1，以前一律报
  # "环境没激活"，把维护者引去折腾 IDF，而主任务栈 16384 B 才是硬约束。
  if grep -q "超出预算" "$OUT/stack.log"; then
    bad "栈深超预算："; grep -E "超出预算" "$OUT/stack.log" | head -10
  elif grep -q "栈检查未判定" "$OUT/stack.log"; then
    bad "栈深未判定：含无法静态解析的调整"; tail -5 "$OUT/stack.log"
  else
    bad "栈深检查跑不起来（先激活 IDF 环境或设置 FNOS_IDF_ENV）"; tail -5 "$OUT/stack.log"
  fi
fi

# ── L2.5：kk_ui 是否已出局（目标硬要求）────────────────────────────
# 判据分两层：① 目录必须消失；② **代码/构建**里不许再有 kk_* 的引用。
# 只查代码与构建文件（*.c/*.h/*.cpp/CMakeLists/Kconfig/*.sh/*.py），不查文档——
# "为什么换掉 kk_ui"这类说明性文字是有价值的，把散文一起当违规会逼人删注释，
# 检查也会因为自己提到 kk_ui 而永远不过（第一版就是这么写错的）。
step "kk_ui 出局检查"
if [ -d components/fnos_monitor/kk_ui ]; then
  bad "components/fnos_monitor/kk_ui 目录仍在（要求：全局只用 ui_kit/ LVGL 实现）"
else
  # 真正致命的两种引用：kk_ui/ 路径（include、CMake 源文件表）与 kk_xxx( 函数调用。
  # 本脚本自己要写这两个模式，必须排除自己——本机 BSD grep 的 --exclude 和 --include
  # 一起用时不生效（实测 2.6.0-FreeBSD），所以改用输出侧过滤。
  hits=$(grep -rInE --exclude-dir=build --exclude-dir=build-release --exclude-dir=.git \
              --exclude-dir=__pycache__ --exclude-dir=managed_components --exclude-dir=archive \
              --include=*.c --include=*.h --include=*.cpp --include=*.txt \
              --include=CMakeLists.txt --include=Kconfig --include=*.sh --include=*.py \
              -e 'kk_ui/|kk_[a-z_]+[[:space:]]*\(' components main tools 2>/dev/null \
         | grep -v '^tools/verify_all.sh:')
  if [ -z "$hits" ]; then pass "kk_ui 已删、代码与构建里无 kk_ 引用（ui_kit 为唯一实现）"; else
    bad "仍有 kk_ 代码引用："; printf '%s\n' "$hits" | sed 's/^/    /'
  fi
fi

[ "$fail" -eq 0 ] || exit 1

# ── L3：实机 ──────────────────────────────────────────────────────
if [ "$FLASH" = 1 ]; then
  step "L3 烧录 + 串口 30s"
  if ./idf.sh -p "$PORT" flash >"$OUT/flash.log" 2>&1; then pass "烧录"; else bad "烧录失败"; tail -10 "$OUT/flash.log"; exit 1; fi
  if python3 tools/serial_capture.py --port "$PORT" --seconds 30 --out "$OUT/serial.log" >"$OUT/serial_capture.log" 2>&1; then
    if python3 tools/serial_check.py "$OUT/serial.log" >"$OUT/serial_check.log" 2>&1; then
      pass "当前启动建树/刷新可用、无关键故障标记（尚非实机屏幕验收）"
    else
      bad "串口启动证据不合格（见 $OUT/serial_check.log）"
    fi
  else
    bad "串口采集失败（见 $OUT/serial_capture.log）；部分日志不能放行"
  fi
fi

if [ "$PHOTO" = 1 ]; then
  step "L3 拍屏（手机取景）"
  if PATH="/opt/homebrew/bin:$PATH" capture_board_photo "$OUT" >"$OUT/photo.log" 2>&1; then
    pass "拍屏文件已解码：$OUT/board-photo.png（待人工看图，未判实机合格）"
  else
    bad "拍屏或PNG解码失败（见 $OUT/photo.log）"
  fi
fi

printf '\n================ 汇总：%d PASS / %d FAIL ================\n' "$ok" "$fail"
[ "$fail" -eq 0 ]
