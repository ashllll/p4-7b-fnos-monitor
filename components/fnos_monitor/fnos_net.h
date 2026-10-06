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
// 机器可读的同一件事：界面要按状态换标题（"正在连接…" / "连接成功" / "没连上"），
// 从中文串里反推状态是错的（改一次文案就崩）。
typedef enum {
    FNOS_NET_UNCONFIGURED = 0,   // 没有凭据
    FNOS_NET_CONNECTING,         // 有凭据，正在连
    FNOS_NET_ONLINE,             // 拿到有效 IPv4
    FNOS_NET_FAILED,             // 上一次尝试失败，正在退避重试
} fnos_net_state_t;
fnos_net_state_t fnos_net_state(void);
// 当前正在用的 SSID（未配置时返回 ""）。配网卡上屏用，纯内存读。
const char *fnos_net_ssid(void);
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

// ── 板上配网：扫描附近的 AP ──────────────────────────────────────────────
// 本板 esp_wifi_* 是到 C6 的**同步 RPC**（最坏阻塞数秒），所以请求扫描与取回结果
// 都只置位，真正的调用发生在 fnos_net_service()（普通网络任务）里 —— 界面可以随便调，
// 不会把 LVGL 卡住。结果写缓存时先清零条数再回填，读侧只会看到"空"或"完整一批"。
typedef struct {
    char    ssid[33];
    int8_t  rssi;      // dBm，负值；越接近 0 越强
    bool    secure;    // true = 需要密码（WPA/WPA2/WPA3…），false = 开放网络
} fnos_ap_t;
#define FNOS_AP_MAX 24

void        fnos_net_scan_request(void);                     // 请求重新扫描（可安全地在 LVGL 任务里调）
bool        fnos_net_scan_busy(void);                        // true = 正在扫描
bool        fnos_net_scan_failed(void);                      // 上一次扫描失败（起站未就绪 / 驱动报错）
int         fnos_net_scan_results(fnos_ap_t *out, int max);   // 返回条数；按信号从强到弱，同名只留最强
void        fnos_net_scan_clear(void);                       // 清空缓存（关掉配网卡时调用）

#ifdef __cplusplus
}
#endif
