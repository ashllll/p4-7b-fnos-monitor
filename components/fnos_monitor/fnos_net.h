#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wi-Fi STA（板载 C6 / ESP-Hosted）+ SNTP。启动幂等、不等待取得 IP；
// 初始化包含同步 ESP-Hosted RPC，须在普通任务调用，不能在 LVGL 回调中启动。
// start/service/poll_rssi须由同一普通任务串行调用；允许启动任务先start再交接网络任务。
// 运行期配网/扫描请求只入队，由 fnos_net_service() 在普通任务中执行。
void        fnos_net_start(void);
// Wi-Fi 凭据状态：false = NVS 与编译期都没有可用凭据（占位值也算没有）。
// 界面据此提示"该配网了"，而不是让板子默默重连到天荒地老。
bool        fnos_net_configured(void);
// 给用户看的一行状态：未配置 / 正在连接 xx / 已连接 xx（ip）/ 连不上 xx：原因（第 N 次）。
// 返回指向静态缓冲的指针，下一次调用前有效 —— **只给单任务用**（串口 CLI 请用
// fnos_net_state_str_r：UI 和 CLI 同时读会互相截断对方正在读的串，审计 M09）。
const char *fnos_net_state_str(void);
// 可重入版本：写调用方的缓冲，UI 与串口各用各的，互不干扰。
void        fnos_net_state_str_r(char *out, size_t cap);
// 机器可读的同一件事：界面要按状态换标题（"正在连接…" / "连接成功" / "没连上"），
// 从中文串里反推状态是错的（改一次文案就崩）。
typedef enum {
    FNOS_NET_UNCONFIGURED = 0,   // 没有凭据
    FNOS_NET_CONNECTING,         // 有凭据，正在连
    FNOS_NET_ONLINE,             // 拿到有效 IPv4
    FNOS_NET_FAILED,             // 上一次尝试失败，正在退避重试
} fnos_net_state_t;
fnos_net_state_t fnos_net_state(void);
// ── 本轮配网提交的状态（审计 M04 + M05）──────────────────────────────────
// 屏幕不能从"有没有凭据 / 有没有 IP"反推"这次配网怎么样了"：第一次配网失败时
// s_configured 仍是 false（+失败原因）→ 旧逻辑报 UNCONFIGURED，屏幕上一直停在
// "正在连接…"；已经在线时改密码，s_online 还是 true → 屏幕上显示"连接成功"，
// 而这一刻正在按新口令重连。所以"这一次提交"必须有自己的 pending/成功/失败。
typedef enum {
    FNOS_WIFI_SUBMIT_IDLE = 0,   // 没有进行中的提交（日常连接与自动重连不算提交）
    FNOS_WIFI_SUBMIT_PENDING,    // 已提交（或正在按新凭据重连），还没有结论
    FNOS_WIFI_SUBMIT_FAILED,     // 这次提交失败：保存失败 / 连不上 / 超时
} fnos_wifi_submit_t;
fnos_wifi_submit_t fnos_net_submit_state(void);        // 纯内存读，UI 可随时调
// 这次提交失败的原因（人话）；没有失败时是 ""。可重入版本，UI 与串口各用各的缓冲。
void        fnos_net_submit_reason_r(char *out, size_t cap);
// 用户重新输入/换一个网络时清掉上一轮结论（提交进行中时不清，免得把"正在连接"抹掉）。
void        fnos_net_submit_clear(void);
// 当前正在用的 SSID（未配置时返回 ""）。配网卡上屏用，纯内存读。
// 同 fnos_net_state_str：静态缓冲只给单任务用，可重入版本见 fnos_net_ssid_r。
const char *fnos_net_ssid(void);
void        fnos_net_ssid_r(char *out, size_t cap);
// 现场配网入口：串口命令与屏幕配网卡都走它。**只把凭据拷进待办缓冲就返回**，
// 落 NVS 与按新凭据重连由 fnos_net_service() 执行（审计 M05：以前这里直接跑
// ESP-Hosted 同步 RPC，最坏几秒，而调用方是 LVGL 回调，整个界面一起冻住）。
void        fnos_net_set_credentials(const char *ssid, const char *pass);
// 串口配网兜底（wifi set / wifi clear / wifi show）。fnos_net_start() 里会自动起。
void        fnos_wifi_cli_start(void);
bool        fnos_net_online(void);      // 拿到有效 IPv4 才算在线
// 旧借用API：静态副本，调用方须串行化调用及使用；离线为空，不借用事件写入数组。
const char *fnos_net_ip(void);          // 点分 IPv4；离线时返回 ""
int8_t      fnos_net_rssi(void);        // dBm；0 表示未知。纯内存读，UI 可随时调用

// 下面两个只能在普通任务里调用（绝不能放在 LVGL 定时器或 esp_timer 回调里）：
// 本板 Wi-Fi 在 C6 上，esp_wifi_* 是 ESP-Hosted 的**同步 RPC**，最坏会阻塞 5 秒。
void        fnos_net_service(void);     // 处理重连请求 + 起站失败重试
void        fnos_net_poll_rssi(void);   // 刷新 RSSI 缓存（5 秒 TTL）

// ── 板上配网：扫描附近的 AP ──────────────────────────────────────────────
// 本板 esp_wifi_* 是到 C6 的**同步 RPC**（最坏阻塞数秒），所以请求扫描与取回结果
// 都只置位，真正的调用发生在 fnos_net_service()（普通网络任务）里 —— 界面可以随便调，
// 不会把 LVGL 卡住。去重/格式化在锁外完成，s_ap_mux 内只做一次定长拷贝发布，
// 读侧也拿同一把锁拷贝，不存在"读到半批"（审计 M09）。
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
