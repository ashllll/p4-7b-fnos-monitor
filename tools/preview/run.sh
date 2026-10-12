#!/bin/bash
# 主机预览一键跑：设备 Kconfig → 主机 LVGL → 渲染 4 页 × 3 状态 → PPM/PNG。
#
#   tools/preview/run.sh [输出目录]     默认 out/
#
# 前置：先跑过一次 ./idf.sh build（要有 build/config/sdkconfig.h）。改完 fnos_ui.c 的文案
# 记得先跑 tools/gen_fonts.sh 重建字库子集，否则新字在预览里是方块。
set -euo pipefail
cd "$(dirname "$0")"
REPO=$(cd ../.. && pwd)
OUT=${1:-out}

# preview.c 会读的**全部**分支变量（tools/preview/preview.c 里的 getenv("PREVIEW_…")）。
# 每个验收阶段都要先把它们清干净、再设置本阶段真正要用的那几个（审计 M36）：以前只清 5 个，
# 调用方 shell 里残留的 PREVIEW_HARDWARE_FIXTURE / PREVIEW_ALLOC_FAIL_AT / PREVIEW_PROBE …
# 会漏进后面的阶段——"渲染全部状态"跑成子集、"硬件矩阵"跑成别的夹具、OOM 阶段带着上一次的
# 夹具。清单必须跟 preview.c 同步：下面有一步现抓 getenv 清单来比对，漏一个就报错退出。
PREVIEW_BRANCH_VARS="PREVIEW_ALLOC_FAIL_AT PREVIEW_ALLOC_SEED_ONE PREVIEW_AUDIT_ALL
PREVIEW_BANDS_AT PREVIEW_BARMAP PREVIEW_CARDLIST PREVIEW_CARDMAP PREVIEW_CARDMAP_AT PREVIEW_CARD_OVERLAP
PREVIEW_CHART_MAP PREVIEW_DATA_MOTION PREVIEW_ENTER_MID PREVIEW_HARDWARE_FIXTURE PREVIEW_MOTION
PREVIEW_MOTION_FIRST_ENTRY PREVIEW_MOTION_INPUT PREVIEW_MOTION_REPEAT_TARGET
PREVIEW_MOTION_RESPONSIVENESS PREVIEW_MOTION_SCROLL PREVIEW_NUMBERS PREVIEW_OVERLAP_TRACE
PREVIEW_PAGE_CARDS PREVIEW_PAIR_FIRST_ENTRY PREVIEW_PARTIAL PREVIEW_PROBE PREVIEW_SIZE
PREVIEW_TRACE_CARDS PREVIEW_WHO PREVIEW_WHOALL"

# 供 env 用的 "-u NAME" 串。变量名都是大写标识符，展开后不会带空格或通配符。
preview_env_args() {
  local v
  for v in $PREVIEW_BRANCH_VARS; do printf ' -u %s' "$v"; done
}

# 输出目录是本脚本要 rm -rf 的东西，而它来自位置参数（审计 M57）。以前
# `bash tools/preview/run.sh ..` 会删掉整个 tools/，`../..` 会删掉仓库根。
# 只接受本目录下的相对路径，绝不接受绝对路径或含 .. 的取值。
case "$OUT" in
  ""|/*|.|..|*..*) echo "输出目录只能是 tools/preview/ 下的相对路径（收到：'$OUT'）" >&2; exit 2 ;;
esac

# 光挡 ".." 还不够（审计 M57）：`tools/preview/link` 是指向 /tmp/x 的符号链接时，
# `link/sub` 里没有 ".."，可 `rm -rf` 删掉的是 /tmp/x/sub —— 仓库外的数据。所以按
# **真实路径**再判一次：解析（含最后一级符号链接）之后必须还在 tools/preview/ 里，
# 而且不能就是 tools/preview 本身（那等于把源目录删了）。pwd -P 拿物理路径，
# 因为 macOS 上 /tmp、/var 本身就是符号链接。
PREVIEW_DIR=$(pwd -P)
OUT_REAL=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$PREVIEW_DIR/$OUT")
if [ "$OUT_REAL" = "$PREVIEW_DIR" ]; then
  echo "输出目录不能就是 tools/preview/ 本身（收到：'$OUT'）" >&2; exit 2
fi
case "$OUT_REAL" in
  "$PREVIEW_DIR"/*) ;;
  *) echo "输出目录经符号链接解析后落在 tools/preview/ 之外（收到：'$OUT' → '$OUT_REAL'）" >&2
     exit 2 ;;
esac

# Only replace a previous output owned by this workflow, never a source file or
# nonempty source directory selected by an otherwise in-repository argument.
python3 - "$OUT_REAL" <<'PYOUT'
import os, pathlib, sys
p = pathlib.Path(sys.argv[1])
if p.exists():
    if not p.is_dir():
        raise SystemExit("输出目标是现有文件，拒绝删除")
    if any(p.iterdir()):
        marker = p / ".fnos-preview-output"
        if marker.is_symlink() or not marker.is_file() or marker.read_text() != "fnos-preview-output-v1\n":
            raise SystemExit("输出目录非空且没有本流程所有权标记；请指定新的输出目录")
PYOUT

# 变量清单与 preview.c 现抓的 getenv 清单必须一致（新增分支变量时别忘同步，否则
# "清干净了"只是自我感觉良好 —— 漏掉的那个照样从调用方漏进来）。
ACTUAL_VARS=$(grep -o 'getenv("PREVIEW_[A-Z_]*"' preview.c | sed 's/.*"\(.*\)"/\1/' | sort -u | tr '\n' ' ')
KNOWN_VARS=$(printf '%s\n' $PREVIEW_BRANCH_VARS | sort -u | tr '\n' ' ')
if [ "$ACTUAL_VARS" != "$KNOWN_VARS" ]; then
  echo "✗ PREVIEW_BRANCH_VARS 与 preview.c 的 getenv 清单不一致" >&2
  echo "  preview.c：$ACTUAL_VARS" >&2
  echo "  脚本里  ：$KNOWN_VARS" >&2
  exit 1
fi

# macOS 上 git/make/cc 需要显式指定 CommandLineTools
export DEVELOPER_DIR=${DEVELOPER_DIR:-/Library/Developer/CommandLineTools}

if [ ! -f "$REPO/build/config/sdkconfig.h" ]; then
  echo "缺少 $REPO/build/config/sdkconfig.h —— 先跑 ./idf.sh build" >&2
  exit 1
fi

# 主机 LVGL 用设备端真实 Kconfig（含 CONFIG_LV_CONF_SKIP=1，于是全靠 CONFIG_LV_*）
grep -E '^#define CONFIG_LV_' "$REPO/build/config/sdkconfig.h" > lv_kconfig_host.h
# 只有三处必须偏离设备：LVGL 9.5 的 lv_conf_kconfig.h 会把 CONFIG_LV_OS_FREERTOS 翻成
# LV_OS_FREERTOS、把 CONFIG_LV_USE_CUSTOM_MALLOC 翻成 LV_STDLIB_CUSTOM（设备那边由
# esp_lvgl_port 提供 lv_malloc_core，主机上没有 → 落到 LVGL 的 weak 桩，永远返回 NULL）。
# 主机改成 LV_OS_NONE + libc malloc，语义等价（设备是 FreeRTOS 任务 + esp 堆）。
cat >> lv_kconfig_host.h <<'EOF'

/* ---- host-only overrides (见 run.sh 注释) ---- */
#undef CONFIG_LV_OS_FREERTOS
#undef CONFIG_LV_USE_FREERTOS_TASK_NOTIFY
#undef CONFIG_LV_USE_CUSTOM_MALLOC
#define LV_USE_OS 0                /* LV_OS_NONE */
#define LV_USE_STDLIB_MALLOC 1     /* LV_STDLIB_CLIB   */
#define LV_USE_STDLIB_STRING 1     /* 设备端 CONFIG_LV_USE_CLIB_STRING  */
#define LV_USE_STDLIB_SPRINTF 1    /* 设备端 CONFIG_LV_USE_CLIB_SPRINTF */
#define LV_DRAW_SW_DRAW_UNIT_CNT 1 /* 设备是 2（双核并行）；主机单线程，>1 会要求 LV_USE_OS */

/* LVGL 默认 LV_ASSERT_HANDLER 是 while(1); —— 断言失败 = 无声 100% CPU 死循环。
   主机上改成打 stderr 再 abort（stderr 无缓冲，重定向到文件也立刻可见）。 */
#include <stdio.h>
#include <stdlib.h>
#define LV_ASSERT_HANDLER                                                       \
    do {                                                                        \
        fprintf(stderr, "\n*** LVGL ASSERT %s:%d (%s) ***\n",                   \
                __FILE__, __LINE__, __func__);                                  \
        fflush(stderr);                                                         \
        abort();                                                                \
    } while(0);
EOF
echo "lv_kconfig_host.h: $(wc -l < lv_kconfig_host.h) lines ($(grep -c '^#define CONFIG_LV_' lv_kconfig_host.h) kconfig + host overrides)"

# 设备端每一条配对消息 → 一副 C 数组（见 pair_msgs.py 头部说明：桩件与设备必须一致）
# 注意：本脚本开头 `cd "$(dirname "$0")"`，所以这里的路径都相对 tools/preview/
python3 pair_msgs.py ../../components/fnos_monitor/fnos_pair.c pair_msgs.h || exit 1

cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1 -g" > /dev/null
cmake --build build -j8 2>&1 | tail -20

rm -rf "${OUT_REAL:?}"
mkdir -p "$OUT_REAL"
printf 'fnos-preview-output-v1\n' > "$OUT_REAL/.fnos-preview-output"
env $(preview_env_args) PREVIEW_CHART_MAP=1 ./build/preview "$OUT_REAL/chart-map"
# 先清干净 PREVIEW_* 再跑（审计 M36）：preview.c 只要看到 PREVIEW_MOTION / PREVIEW_NUMBERS /
# PREVIEW_CARDMAP / PREVIEW_PAIR_FIRST_ENTRY / PREVIEW_DATA_MOTION 任一被设置，就只渲子集
# 然后 return 0。调用方 shell 里残留一个变量，这条"交付前一条命令的验收门"就会退 0，
# 而六页的图像/溢出/格式残留/点压四项审计一个都没跑。
env $(preview_env_args) PREVIEW_BARMAP="$OUT_REAL/barmap.txt" ./build/preview "$OUT_REAL"
FRAMES=$(find "$OUT_REAL" -name '*.ppm' | wc -l | tr -d ' ')
if [ "$FRAMES" -lt 84 ]; then
  echo "✗ 只渲染了 $FRAMES 帧（应 ≥84）：审计没跑全，别把这次结果当验收" >&2
  exit 1
fi
echo "渲染帧数：$FRAMES"
python3 ppm2png.py "$OUT_REAL"

# snapshot_check 是 ownership / 并发 / 容量预算三项验收的**唯一**自动化证据。
# 以前 CMake 建了它、run.sh 却只编译 preview、从不执行（审计 M16），唯一执行处是
# README 里的一条手工命令 —— 也就是说这三项验收的"自动化"其实一直不存在。
if [ ! -x ./build/snapshot_check ]; then
  echo "✗ 没有 ./build/snapshot_check（CMake 目标没建出来），快照验收无法执行" >&2
  exit 1
fi
./build/snapshot_check

# Exercise dynamic inventories with the same renderer and audits as the samples.
MATRIX=$(mktemp -d)
trap 'rm -rf "$MATRIX"' EXIT
python3 hardware_fixtures.py "$MATRIX/fixtures" > "$MATRIX/fixtures.log"
for fixture in empty compact mixed dense; do
  # 每个夹具阶段也先把所有分支变量清干净（审计 M36）：只清自己认识的那几个，
  # 调用方残留的 PREVIEW_ALLOC_FAIL_AT=… 会让"硬件矩阵"这一格跑成 OOM 路径。
  if ! env $(preview_env_args) \
       PREVIEW_HARDWARE_FIXTURE="$MATRIX/fixtures/$fixture.json" \
       ./build/preview "$MATRIX/$fixture" > "$MATRIX/$fixture.log" 2>&1; then
    tail -20 "$MATRIX/$fixture.log" >&2
    exit 1
  fi
  echo "hardware inventory $fixture: PASS"
done
if ! env $(preview_env_args) PREVIEW_ALLOC_FAIL_AT=0 \
     PREVIEW_HARDWARE_FIXTURE="$MATRIX/fixtures/mixed.json" \
     ./build/preview "$MATRIX/oom" > "$MATRIX/oom.log" 2>&1; then
  tail -20 "$MATRIX/oom.log" >&2
  exit 1
fi
echo "hardware inventory allocation failure and retry: PASS"
