#pragma once
/* 主机预览：堆查询替身。控制器只用它显示"内部空闲内存"这一格。
   常量必须与 ESP-IDF 5.5.3 的 esp_heap_caps.h 一致（审计 M16）：
   原来写成 1<<0 / 1<<1，与真值错位，宿主上所谓"内部内存"查的其实是别的堆。 */
#include <stddef.h>

#define MALLOC_CAP_DMA        (1 << 3)   /* 可与 DMA 共用的内存 */
#define MALLOC_CAP_SPIRAM     (1 << 10)  /* 外部 PSRAM */
#define MALLOC_CAP_INTERNAL   (1 << 11)  /* 片内 RAM */
#define MALLOC_CAP_DEFAULT    (1 << 12)  /* 默认堆 */

#ifdef __cplusplus
extern "C" {
#endif

size_t heap_caps_get_free_size(int caps);
size_t esp_get_free_heap_size(void);

#ifdef __cplusplus
}
#endif
