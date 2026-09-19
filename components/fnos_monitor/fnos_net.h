#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wi-Fi STA（板载 C6 / ESP-Hosted）+ SNTP。幂等，非阻塞。
void        fnos_net_start(void);
bool        fnos_net_online(void);      // 拿到有效 IPv4 才算在线
const char *fnos_net_ip(void);          // 点分 IPv4；离线时返回 ""
int8_t      fnos_net_rssi(void);        // dBm；0 表示未知。纯内存读，UI 可随时调用

// 下面两个只能在普通任务里调用（绝不能放在 LVGL 定时器或 esp_timer 回调里）：
// 本板 Wi-Fi 在 C6 上，esp_wifi_* 是 ESP-Hosted 的**同步 RPC**，最坏会阻塞 5 秒。
void        fnos_net_service(void);     // 处理重连请求 + 起站失败重试
void        fnos_net_poll_rssi(void);   // 刷新 RSSI 缓存（5 秒 TTL）

#ifdef __cplusplus
}
#endif
