/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LVGL 的对象/样式/缓存内存改走 PSRAM（"三件搬运"之二）。
 *
 * 背景：本工程 sdkconfig 里 `CONFIG_SPIRAM_USE_CAPS_ALLOC=y`，普通 malloc 永远拿不到
 * PSRAM；LVGL 默认用 libc malloc（CONFIG_LV_USE_CLIB_MALLOC），于是控件、样式、
 * chart 点数组这些全落在内部 RAM。内部 RAM 只有 ~361 KB 可用（还要养 ESP-Hosted /
 * DSI / 任务栈），而 PSRAM 闲着 24 MB。
 *
 * 用法：sdkconfig 选 `CONFIG_LV_USE_CUSTOM_MALLOC=y`，LVGL 就会 extern 下面这一整套
 * （接口定义见 lvgl/src/stdlib/lv_mem.h 与 clib 参考实现 lv_mem_core_clib.c）。
 */
#include "esp_heap_caps.h"
#include <string.h>

#include "lvgl.h"

#define LVGL_MEM_CAPS_PRIMARY   (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LVGL_MEM_CAPS_FALLBACK  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
    /* 无需初始化：分配直接走 heap_caps */
}

void lv_mem_deinit(void)
{
    /* 无需释放：没有自建内存池 */
}

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, LVGL_MEM_CAPS_PRIMARY);
    if (p == NULL) {
        /* PSRAM 用尽时退回内部 RAM：宁可挤一点也不要直接 OOM */
        p = heap_caps_malloc(size, LVGL_MEM_CAPS_FALLBACK);
    }
    return p;
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    if (p == NULL) {
        return lv_malloc_core(new_size);
    }

    /* 定点测试：改用 heap_caps_realloc（同区域内 realloc，失败再手动搬） */
    void *q = heap_caps_realloc(p, new_size, LVGL_MEM_CAPS_PRIMARY);
    if (q == NULL) q = heap_caps_realloc(p, new_size, LVGL_MEM_CAPS_FALLBACK);
    return q;
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    if (mon_p == NULL) {
        return;
    }

    size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_biggest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    *mon_p = (lv_mem_monitor_t){
        .total_size = total,
        .free_cnt = 1,
        .free_size = free_size,
        .free_biggest_size = free_biggest,
        .used_cnt = 0,
        .max_used = (total > free_size) ? (total - free_size) : 0,
        .used_pct = total ? (uint8_t)(((total - free_size) * 100) / total) : 0,
        .frag_pct = free_size ? (uint8_t)(((free_size - free_biggest) * 100) / free_size) : 0,
    };
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}
