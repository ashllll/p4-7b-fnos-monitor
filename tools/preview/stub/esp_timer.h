#pragma once
/* 主机预览：esp_timer 的最小替身（单调微秒）。 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time(void);   /* 微秒，单调 */

#ifdef __cplusplus
}
#endif
