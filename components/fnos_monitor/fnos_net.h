#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wi-Fi STA（板载 C6 / ESP-Hosted）+ SNTP。幂等，非阻塞。
void        fnos_net_start(void);
// Wi-Fi 凭据状态：false = NVS 与编译期都没有可用凭据（占位值也算没有）。
// 界面据此提示"该配网了"，而不是让板子默默重连到天荒地老。
bool        fnos_net_configured(void);
// 给用户看的一行状态：未配置 / 正在连接 xx / 已连接 xx（ip）/ 连不上 xx：原因（第 N 次）。
// 返回指向静态缓冲的指针，下一次调用前有效；UI 每帧读一次即可。
const char *fnos_net_state_str(void);
// 现场配网入口：存进 NVS 并立刻按新凭据重连（串口命令与屏幕配网卡都走它）。
void        fnos_net_set_credentials(const char *ssid, const char *pass);
// 串口配网兜底（wifi set / wifi clear / wifi show）。fnos_net_start() 里会自动起。
void        fnos_wifi_cli_start(void);
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
