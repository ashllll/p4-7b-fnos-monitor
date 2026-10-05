#pragma once
/* 主机预览：堆查询替身。控制器只用它显示"内部空闲内存"这一格。 */
#include <stddef.h>

#define MALLOC_CAP_INTERNAL  (1 << 0)
#define MALLOC_CAP_DEFAULT   (1 << 1)

#ifdef __cplusplus
extern "C" {
#endif

size_t heap_caps_get_free_size(int caps);
size_t esp_get_free_heap_size(void);

#ifdef __cplusplus
}
#endif
