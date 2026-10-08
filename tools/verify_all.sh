#!/usr/bin/env bash
# 四级证据一键验收（见 docs/ui-v12-lvgl-native.md §7）：
#   L1 主机预览（版面/字库/越界/压叠）+ 自适应池几何
#   L2 固件构建 + 三条任务栈深
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
    --port) shift; PORT="$1" ;;
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
ok=0; fail=0
step() { printf '\n=== %s ===\n' "$1"; }
pass() { printf '  [PASS] %s\n' "$1"; ok=$((ok+1)); }
bad()  { printf '  [FAIL] %s\n' "$1"; fail=$((fail+1)); }

rm -rf "$OUT"; mkdir -p "$OUT"

# ── L1：主机预览 ───────────────────────────────────────────────────
step "L1 主机预览（真正的 UI 源码，不是原型）"
if DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build tools/preview/build -j8 >"$OUT/build.log" 2>&1; then
  pass "预览构建"
else
  bad "预览构建失败（见 $OUT/build.log）"; tail -20 "$OUT/build.log"; exit 1
fi
if DEVELOPER_DIR=/Library/Developer/CommandLineTools tools/preview/build/preview "$OUT/png" >"$OUT/preview.log" 2>&1; then
  pass "渲染全部状态"
else
  bad "渲染失败（多为字形缺口，跑 bash tools/gen_fonts.sh 再试）"; tail -20 "$OUT/preview.log"; exit 1
fi
python3 tools/preview/ppm2png.py "$OUT/png" >"$OUT/ppm2png.log" 2>&1 && pass "PPM→PNG"

# 审计：越界 / 压叠 / 字形 是硬失败；"从未露面"只是信息级
if grep -qiE "跑出父对象|压住|overlap|溢出|missing glyph" "$OUT/preview.log"; then
  bad "几何/字形审计有失败项："; grep -iE "跑出父对象|压住|overlap|溢出|missing glyph" "$OUT/preview.log" | head -10
else
  pass "几何审计（无越界/压叠/字形缺口）"
fi
grep -h "见过的构件" "$OUT/preview.log" | sed 's/^/  /'
echo "  验收图：$OUT/png/（$(ls "$OUT/png" | wc -l | tr -d ' ') 张）"

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
  bad "栈深检查跑不起来（先激活 IDF 环境或设置 FNOS_IDF_ENV）"; tail -5 "$OUT/stack.log"
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

# ── L3：实机 ──────────────────────────────────────────────────────
if [ "$FLASH" = 1 ]; then
  step "L3 烧录 + 串口 30s"
  if ./idf.sh -p "$PORT" flash >"$OUT/flash.log" 2>&1; then pass "烧录"; else bad "烧录失败"; tail -10 "$OUT/flash.log"; exit 1; fi
  python3 - "$PORT" >"$OUT/serial.log" 2>&1 <<'PY'
import serial, sys, time
s = serial.Serial(sys.argv[1], 115200, timeout=0.3)
t0 = time.time(); buf = []
while time.time() - t0 < 30:
    l = s.readline()
    if l: buf.append(l.decode('utf-8', 'replace').rstrip())
s.close()
print('\n'.join(buf))
PY
  if [ -s "$OUT/serial.log" ]; then
    if grep -qE "Guru|abort\(\)|assert|Backtrace|panic|Rebooting|Stack protection" "$OUT/serial.log"; then
      bad "串口出现崩溃/复位标记："; grep -E "Guru|abort\(\)|assert|Backtrace|panic|Rebooting|Stack protection" "$OUT/serial.log" | head -5
    else
      pass "无崩溃/复位标记"
    fi
    grep -E "boot complete|ui created|poll ok|heap periodic" "$OUT/serial.log" | tail -4 | sed 's/^/  /'
    if grep -q "boot complete" "$OUT/serial.log"; then pass "启动完成"; else bad "30s 内没看到 boot complete"; fi
  else
    bad "串口没读到任何内容（端口对不对？${PORT}）"
  fi
fi

if [ "$PHOTO" = 1 ]; then
  step "L3 拍屏（手机取景）"
  if PATH="/opt/homebrew/bin:$PATH" capture-board >"$OUT/photo.json" 2>"$OUT/photo.err"; then
    img=$(python3 -c "import json,sys;print(json.load(open('$OUT/photo.json'))['image'])" 2>/dev/null)
    if [ -n "${img:-}" ] && [ -f "$img" ]; then cp "$img" "$OUT/board-photo.png"; pass "拍屏：$OUT/board-photo.png"; else bad "拍屏返回但没找到文件"; fi
  else
    bad "拍屏失败（手机相机要开着并对准板子；见 $OUT/photo.err）"
  fi
fi

printf '\n================ 汇总：%d PASS / %d FAIL ================\n' "$ok" "$fail"
[ "$fail" -eq 0 ]
