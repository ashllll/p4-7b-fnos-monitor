#pragma once
/* 主机预览：把 ESP-IDF 日志宏降级成可选的 stderr 输出。 */
#include <stdio.h>

#ifndef PREVIEW_LOG
#define PREVIEW_LOG 0
#endif

#define ESP_LOGE(tag, fmt, ...) do { if (PREVIEW_LOG) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, fmt, ...) do { if (PREVIEW_LOG) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGI(tag, fmt, ...) do { if (PREVIEW_LOG) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGD(tag, fmt, ...) do { } while (0)
#define ESP_LOGV(tag, fmt, ...) do { } while (0)
