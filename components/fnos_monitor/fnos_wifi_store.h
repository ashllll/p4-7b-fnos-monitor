#pragma once
// Wi-Fi 凭据的**运行时**存储（NVS）。
//
// 为什么要它：在这之前，SSID/口令只有编译期一条路（`fnos_config.h` 的
// APP_WIFI_SSID/APP_WIFI_PASS）。结果是"固件刷好之后没法配网"——换一个现场、
// 换一台路由器，就得回电脑上改配置重新烧录；刷了通用固件（占位值 your-ssid）
// 的板子更是只会默默重连，屏幕上连一句"你去配一下网"都没有。
//
// 优先级：**NVS > 编译期默认**。编译期的值退化成"出厂默认"，开发/测试时不用配；
// 现场配过之后以 NVS 为准。口令只走 NVS，日志与界面永不回显。
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 读出当前生效凭据（NVS 优先，没有就用编译期默认）。
// 返回 true = 有**可用**凭据（非空、且不是 "your-ssid" 这类占位值）。
// ssid/pass 可以是 NULL（表示不关心那一项）。
bool fnos_wifi_store_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);

// 写进 NVS（幂等）。ssid 为空视为清除。
bool fnos_wifi_store_save(const char *ssid, const char *pass);

// 清掉 NVS 里的凭据（退回编译期默认）。
void fnos_wifi_store_clear(void);

// 占位值判定："your-ssid"、空串、"changeme"…… 统一在这里认，别到处写字符串比较。
bool fnos_wifi_store_placeholder(const char *ssid);

#ifdef __cplusplus
}
#endif
