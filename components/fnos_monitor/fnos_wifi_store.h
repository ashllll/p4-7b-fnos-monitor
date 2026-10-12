#pragma once
// Wi-Fi 凭据的**运行时**存储（NVS）。
//
// 为什么要它：在这之前，SSID/口令只有编译期一条路（`fnos_config.h` 的
// APP_WIFI_SSID/APP_WIFI_PASS）。结果是"固件刷好之后没法配网"——换一个现场、
// 换一台路由器，就得回电脑上改配置重新烧录；刷了通用固件（占位值 your-ssid）
// 的板子更是只会默默重连，屏幕上连一句"你去配一下网"都没有。
//
// 优先级：**NVS > 编译期默认**。编译期的值退化成"出厂默认"，开发/测试时不用配；
// 现场配过之后以 NVS 为准。日志不回显口令；界面默认掩码，用户可临时选择显示。
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 以下接口限普通任务调用，串行存储IO；不能在ISR/持有调度锁时调用，等待时让出CPU。
// 读出当前生效凭据（NVS 优先，完整空存储/成功清除才用编译期默认）。
// 缺键/读取错误/未完成写入拒绝，返回false并清空输出；不把坏pass当开放网。
// 旧无屏障完整凭据兼容，旧混代无法追溯；回退旧固件不识别新屏障。
// 返回 true = 有**可用**凭据（非空、且不是 "your-ssid" 这类占位值）。
// ssid/pass 可以是 NULL（表示不关心那一项）；提供时容量必须足够，禁止截断凭据。
bool fnos_wifi_store_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);

// 写进 NVS（幂等）。ssid为空视为清除；SSID最多32字节，pass最多64字节，NULL视为空串。
// DIRTY屏障先于修改，完整成功后READY；失败可能禁用持久凭据，须重试而非假定旧值保留。
bool fnos_wifi_store_save(const char *ssid, const char *pass);

// 清掉 NVS 里的凭据（退回编译期默认）。返回 false = 擦除/提交失败，
// 调用方必须如实报错：说"已清除"而实际没删，等于旧口令下次启动复活。
bool fnos_wifi_store_clear(void);

// 占位值判定："your-ssid"、空串、"changeme"…… 统一在这里认，别到处写字符串比较。
bool fnos_wifi_store_placeholder(const char *ssid);

#ifdef __cplusplus
}
#endif
