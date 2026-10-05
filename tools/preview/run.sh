#!/bin/bash
# 主机预览一键跑：设备 Kconfig → 主机 LVGL → 渲染 4 页 × 3 状态 → PPM/PNG。
#
#   tools/preview/run.sh [输出目录]     默认 out/
#
# 前置：先跑过一次 ./idf.sh build（要有 build/config/sdkconfig.h）。改完 layout.json 记得先
# 跑 tools/ui_gen.py 重新生成 C，预览读的是生成物，不是 JSON。
set -euo pipefail
cd "$(dirname "$0")"
REPO=$(cd ../.. && pwd)
OUT=${1:-out}

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

cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1 -g" > /dev/null
cmake --build build -j8 2>&1 | tail -20

rm -rf "$OUT"
./build/preview "$OUT"
python3 ppm2png.py "$OUT"
