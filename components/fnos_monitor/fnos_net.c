// 网络起站：netif + Wi-Fi STA（板载 C6 / ESP-Hosted）+ SNTP。
// 与 p4-7b-unifi-app 的 unifi_net.c 同源，去掉 UniFi 相关的回调钩子。
#include "fnos_net.h"
#if __has_include("fnos_config.h")
#include "fnos_config.h"
#else
#include "fnos_config.example.h"
#endif
#include "fnos_data.h"
#include "fnos_wifi_store.h"

#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_sntp.h"

static const char *TAG = "fnos_net";
#define BIT_IP (1 << 0)
#ifndef APP_TZ
#define APP_TZ "CST-8"
#endif

static EventGroupHandle_t s_eg;
static char s_ip[16] = "";              // 点分 IPv4；空串表示离线
static portMUX_TYPE s_ip_mux = portMUX_INITIALIZER_UNLOCKED;
/* 保护 s_ssid / s_reason / 待办凭据：写侧是事件任务、service 任务与调用方（LVGL），
   读侧是 UI 与串口 CLI。整串 memcpy 进出，读侧永远看到一条完整的串。 */
static portMUX_TYPE s_cfg_mux = portMUX_INITIALIZER_UNLOCKED;
static _Atomic int8_t s_rssi;          // 缓存值：UI 只读它，不触发 RPC
static int64_t s_rssi_ms;             // 仅网络 service 所属任务读写
static atomic_bool s_connect_wanted;  // 重连请求标志（定时器只置位，不直接调 esp_wifi_connect）
static atomic_bool s_reassociate;      // LOST_IP 请求强制重新关联，由 service 任务消费
static atomic_bool s_online;
static _Atomic uint8_t s_wifi_retry_count;
static int64_t s_retry_due_us;          // s_cfg_mux；0表示没有待办重连
static bool s_started;                  // fnos_net_start() 幂等标志
static bool s_wifi_inited;
/* 待办凭据：fnos_net_set_credentials() 只往这里拷贝，service() 才落盘+重连（审计 M05）。 */
static char s_pend_ssid[64];
static char s_pend_pass[96];
static bool s_cred_want;

/* ── 扫描缓存（板上配网用）──────────────────────────────────────────────
   写入方是 fnos_net_service()（普通任务），读取方是 LVGL 任务。
   条数和整批记录始终在 s_ap_mux 内发布/复制；通知位使用原子交换消费。 */
static fnos_ap_t s_aps[FNOS_AP_MAX];
static portMUX_TYPE s_ap_mux = portMUX_INITIALIZER_UNLOCKED;  // 见 fnos_net.h 的说明（审计 M09）
static int s_ap_n;
static atomic_bool s_scan_busy;
static atomic_bool s_scan_failed;
static atomic_bool s_scan_want;       // 界面请求扫描 → service() 里真正发起
static atomic_bool s_scan_done;       // 事件任务收到 SCAN_DONE → service() 里取结果

static int ap_cmp_rssi(const void *a, const void *b) {
    int ra = ((const wifi_ap_record_t *)a)->rssi, rb = ((const wifi_ap_record_t *)b)->rssi;
    return rb - ra;                     // 信号强的排前面（rssi 是负值，越大越强）
}
static bool s_handlers_registered;
/* ── UTF-8 安全的拷贝（审计 M10）───────────────────────────────────────
   屏幕上的中文是 UTF-8 多字节串，而"复制到定长缓冲"这种操作如果按字节截断，
   最后一个汉字会被切成半个：字库拿不到完整码点 → 那一行字**整条不显示**，
   比截断本身更难查。原因串有两句各 76 / 96 字节（见 reason_text），
   而当时的目标缓冲只有 72 字节，正好踩在这上面。
   规则：最多写 cap-1 字节，且保证不切在多字节字符中间；永远以 NUL 结尾。
   返回写入的字节数（不含结尾 NUL）。 */
/* WIFI_REASON_UTF8_BEGIN —— tools/wifi_reason_check.py 会原样抠出这一段到主机上单测 */
static size_t fnos_utf8_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return 0;
    size_t max = cap - 1;
    size_t len = 0;
    if (src) while (src[len] && len < max) len++;
    /* 截断点上如果正踩在"续字节"（10xxxxxx）上，就往回退到字符边界：
       宁可少拷一两个字，也不产出一段非法 UTF-8。 */
    if (src && src[len]) {
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    }
    if (src) memcpy(dst, src, len);
    dst[len] = 0;
    return len;
}
/* WIFI_REASON_UTF8_END */

// ── 给用户看的 Wi-Fi 状态 ──────────────────────────────────────────
// 以前只有 ESP_LOG，屏幕上看不到"为什么连不上"。配网引导要靠这两样：
// 有没有凭据（configured）、以及最近一次失败的人话原因。
static atomic_bool s_configured;               // NVS/编译期里有可用凭据
static char s_ssid[64] = "";
/* 原因串容量：必须装得下 reason_text() 里最长的一句（96 字节）+ 结尾 NUL。
   72 字节时那两句 96/76 字节的新原因会被截断成非法 UTF-8（审计 M10），
   容量本身也有回归钉着（tools/wifi_reason_check.py 逐句核对）。 */
#define WIFI_REASON_CAP 128
#define WIFI_REASON_TEXT_MAX 192        // 成型原因串的本地缓冲（只在本文件里用）
/* 状态整串的成型缓冲：要装得下 ssid + 最长原因 + 前后缀，绝不在这里截断
   （截断统一交给 fnos_utf8_copy 按字符边界做，审计 M10）。 */
#define WIFI_STATE_STR_MAX 256
static char s_reason[WIFI_REASON_CAP] = "";   // 最近一次断线原因（人话），连上就清空

/* ── 本轮配网提交的状态（审计 M04 + M05）─────────────────────────────────
   屏幕要能说清"这一次提交"是 pending / 成功 / 失败，而不是从"有没有凭据、有没有 IP"
   反推 —— 首次配网失败会反推成"未配置 → 正在连接…"，在线改密码会反推成"连接成功"。
   转移：fnos_net_set_credentials() → PENDING；拿到 IP → IDLE（成功由 state==ONLINE 表达）；
   保存失败 / 断开（带原因）/ 超时 → FAILED，并单独留一条这次的原因。 */
static int s_submit_state;             // fnos_wifi_submit_t
static char s_submit_reason[WIFI_REASON_CAP] = "";
static uint32_t s_submit_generation;
static uint32_t s_submit_applied_generation;
static uint32_t s_submit_associated_generation;
static int64_t s_submit_started_us;    // 提交时刻，用于超时兜底
/* 一次提交最多等这么久还没有结论（既没拿到 IP、也没有带原因的断开事件）就判失败：
   屏幕上不许永远停在"正在连接…"。30 s 覆盖 esp_wifi 的内部重试节奏。 */
#define WIFI_SUBMIT_TIMEOUT_US (30LL * 1000 * 1000)

// 断线重连：指数退避，最长30s。截止时间在同锁下发布，由普通service任务消费。
// 不依赖定时器分配/启动，事件回调不发同步ESP-Hosted RPC。
static void wifi_schedule_retry(void) {
    uint8_t attempt = atomic_load(&s_wifi_retry_count);
    if (attempt > 5) attempt = 5;
    uint64_t delay_ms = 1000ULL << attempt;
    if (delay_ms > 30000) delay_ms = 30000;
    int64_t due = esp_timer_get_time() + (int64_t)(delay_ms * 1000ULL);
    portENTER_CRITICAL(&s_cfg_mux);
    s_retry_due_us = s_configured ? due : 0;
    portEXIT_CRITICAL(&s_cfg_mux);
}

// 链路失效：清地址与在线标志，并让数据层复位会话（丢旧 HTTP 句柄与历史）。
static void mark_offline(void) {
    s_online = false;
    portENTER_CRITICAL(&s_ip_mux);
    s_ip[0] = 0;
    portEXIT_CRITICAL(&s_ip_mux);
    if (s_eg) xEventGroupClearBits(s_eg, BIT_IP);
    fnos_data_network_changed();
}

static void sntp_start_once(void) {
    if (esp_sntp_enabled()) return;
    setenv("TZ", APP_TZ, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();
}

// 断线原因 → 人话。只覆盖现场最可能遇到的几种；其余留给数字兜底，
// 宁可说"原因码 205"也不要编一句错的解释。
static const char *reason_text(uint8_t r)
{
    switch (r) {
    case WIFI_REASON_NO_AP_FOUND:            return "找不到这个 Wi-Fi（名字不对或不在范围内）";
    case WIFI_REASON_AUTH_FAIL:              return "认证失败（口令不对）";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "握手超时（多半是口令不对）";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:      return "握手超时（多半是口令不对）";
    case WIFI_REASON_ASSOC_FAIL:             return "路由器拒绝了连接";
    case WIFI_REASON_CONNECTION_FAIL:        return "连接失败（信号太弱？）";
    case WIFI_REASON_BEACON_TIMEOUT:         return "与路由器失联（信号不好）";
    /* 下面三个以前只显示数字，而它们恰恰是"明明在范围内却连不上"最常见的答案：
       加密网络按 WPA2 阈值，开放网络单独放宽到 OPEN（见 wifi_authmode_for_password）；
       WEP/WPA-PSK 以及加密方式不合要求的 AP 仍会被这条阈值挡掉（审计 M10）。 */
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "找到了网络但加密方式不合适（路由器是混合/旧加密？）";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "这个网络的加密方式不被接受（需要 WPA2 及以上；WEP/WPA 太旧）";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "信号太弱，达不到连接阈值（靠近路由器再试）";
    default:                                 return NULL;
    }
}

static void on_net_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_configured) s_connect_wanted = true;
        else ESP_LOGW(TAG, "没有 Wi-Fi 凭据：不自动连接，等待现场配网");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        uint8_t retries = atomic_load(&s_wifi_retry_count);
        while (retries < 15 && !atomic_compare_exchange_weak(&s_wifi_retry_count, &retries, (uint8_t)(retries + 1))) {}
        mark_offline();
        wifi_schedule_retry();
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        uint8_t rs = d ? d->reason : 0;
        char reason[WIFI_REASON_TEXT_MAX];       // 先在本地成型，锁里只做拷贝（不在锁里打日志）
        const char *rt = reason_text(rs);
        if (rt) fnos_utf8_copy(reason, sizeof reason, rt);
        else    snprintf(reason, sizeof reason, "原因码 %u", (unsigned)rs);
        portENTER_CRITICAL(&s_cfg_mux);         // UI/CLI 可能正在读这条串
        s_submit_associated_generation = 0;
        fnos_utf8_copy(s_reason, sizeof s_reason, reason);   // UTF-8 安全（审计 M10）
        /* 如果这次断开属于"本轮配网提交"，它就给出了结论：失败（审计 M04）。
           日常自动重连（没有进行中的提交）不动提交状态。 */
        if (s_submit_state == FNOS_WIFI_SUBMIT_PENDING &&
            s_submit_applied_generation == s_submit_generation &&
            rs != WIFI_REASON_ASSOC_LEAVE) {
            s_submit_state = FNOS_WIFI_SUBMIT_FAILED;
            fnos_utf8_copy(s_submit_reason, sizeof s_submit_reason, reason);
        }
        portEXIT_CRITICAL(&s_cfg_mux);
        ESP_LOGW(TAG, "wifi disconnected reason=%u (%s) retry=%u", (unsigned)rs,
                 reason, (unsigned)s_wifi_retry_count);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *e = (wifi_event_sta_connected_t *)data;
        portENTER_CRITICAL(&s_cfg_mux);
        if (e && s_submit_state == FNOS_WIFI_SUBMIT_PENDING &&
            s_submit_applied_generation == s_submit_generation &&
            e->ssid_len == strlen(s_ssid) &&
            memcmp(e->ssid, s_ssid, e->ssid_len) == 0) {
            s_submit_associated_generation = s_submit_generation;
        }
        portEXIT_CRITICAL(&s_cfg_mux);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        s_scan_done = true;             // 只置位：取结果是 RPC，放到 service() 里做
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        if (!e || e->ip_info.ip.addr == 0) {   // 不把 0.0.0.0 当在线
            mark_offline();
            return;
        }
        char ipstr[16];
        esp_ip4addr_ntoa(&e->ip_info.ip, ipstr, sizeof(ipstr));
        portENTER_CRITICAL(&s_ip_mux);      // 事件任务写、LVGL 任务读
        memcpy(s_ip, ipstr, sizeof(s_ip));
        portEXIT_CRITICAL(&s_ip_mux);
        s_online = true;
        portENTER_CRITICAL(&s_cfg_mux);
        s_reason[0] = 0;                    // 连上了，上一次的失败原因作废
        s_retry_due_us = 0;
        if (s_submit_state == FNOS_WIFI_SUBMIT_PENDING &&
            s_submit_applied_generation == s_submit_generation &&
            s_submit_associated_generation == s_submit_generation) {
            /* 本轮提交有结论了：成功。成功状态由 state==ONLINE 表达，这里回到日常态 */
            s_submit_state = FNOS_WIFI_SUBMIT_IDLE;
            s_submit_reason[0] = 0;
        }
        portEXIT_CRITICAL(&s_cfg_mux);
        s_wifi_retry_count = 0;
        if (s_eg) xEventGroupSetBits(s_eg, BIT_IP);
        ESP_LOGI(TAG, "got ip %s", s_ip);
        fnos_data_network_changed();
        sntp_start_once();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        mark_offline();
        /* LOST_IP 不保证伴随 DISCONNECTED：只清 IP 会永远停在离线/连接中。
           事件任务只置位；disconnect/connect 是 ESP-Hosted 同步 RPC，交给 service。 */
        atomic_store(&s_reassociate, true);
        ESP_LOGW(TAG, "ip lost");
    }
}

/* 开放网络没有口令，阈值必须放宽到 OPEN；否则驱动会以
   NO_AP_FOUND_IN_AUTHMODE_THRESHOLD 直接拒绝（审计 M10）。 */
static wifi_auth_mode_t wifi_authmode_for_password(const char *pass)
{
    return (pass && pass[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
}

static bool wifi_start(void) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err)); return false; }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) sta = esp_netif_create_default_wifi_sta();
    if (!sta) { ESP_LOGE(TAG, "create default wifi sta netif failed"); return false; }
    if (!s_wifi_inited) {
        wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&wc);
        if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
        if (err != ESP_OK) { ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err)); return false; }
        s_wifi_inited = true;
    }
    if (!s_handlers_registered) {
        // 必须在 esp_wifi_start() 之前注册，否则会漏掉 STA_START。
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_net_event, NULL);
        if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_net_event, NULL);
        if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &on_net_event, NULL);
        if (err != ESP_OK) { ESP_LOGE(TAG, "event handler register failed: %s", esp_err_to_name(err)); return false; }
        s_handlers_registered = true;
    }
    char ssid[64] = "", pass[96] = "";
    bool configured = fnos_wifi_store_load(ssid, sizeof ssid, pass, sizeof pass);
    portENTER_CRITICAL(&s_cfg_mux);
    s_configured = configured;
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
    portEXIT_CRITICAL(&s_cfg_mux);
    if (!s_configured) {
        ESP_LOGW(TAG, "Wi-Fi 未配置（NVS 与编译期都没有可用 SSID）——起站但不自动连接");
    }
    wifi_config_t cfg = { 0 };
    // store已校验32/64字节上限；SDK字段是定宽字节数组，满宽不另留NUL。
    // snprintf会把合法32字节SSID/64字节PSK各丢最后一个字节。
    memcpy(cfg.sta.ssid, ssid, strnlen(ssid, sizeof cfg.sta.ssid));
    memcpy(cfg.sta.password, pass, strnlen(pass, sizeof cfg.sta.password));
    cfg.sta.threshold.authmode = wifi_authmode_for_password(pass);
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) { ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err)); return false; }
    return true;
}

void fnos_net_start(void) {
    if (s_started) return;                 // 幂等：重复调用无副作用，也不等待联网
    if (!s_eg) {
        s_eg = xEventGroupCreate();
        if (!s_eg) { ESP_LOGE(TAG, "event group alloc failed"); return; }
    }
    if (!wifi_start()) return;             // 失败时保持未启动，允许稍后重试
    s_started = true;
    ESP_LOGI(TAG, "wifi station started; awaiting ip");
    fnos_wifi_cli_start();      // 串口配网兜底：屏幕配网不可用时的唯一入口
}

bool fnos_net_configured(void) { return s_configured; }

void fnos_net_state_str_r(char *out, size_t cap)
{
    if (!out || !cap) return;
    out[0] = 0;
    char ssid[sizeof s_ssid], reason[sizeof s_reason], sub_reason[sizeof s_submit_reason];
    int sub;
    portENTER_CRITICAL(&s_cfg_mux);         // 整串进出：读侧不会看到被改写一半的串
    memcpy(ssid, s_ssid, sizeof ssid);
    memcpy(reason, s_reason, sizeof reason);
    memcpy(sub_reason, s_submit_reason, sizeof sub_reason);
    sub = s_submit_state;
    portEXIT_CRITICAL(&s_cfg_mux);
    /* 先在一段足够大的本地缓冲里成型，最后一次性做 UTF-8 安全截断。
       以前直接 snprintf 到调用方的 160 字节缓冲：原因串本身可以有近百字节，
       拼出来超过 160 就会被按字节砍掉半个汉字（审计 M10）。 */
    char full[WIFI_STATE_STR_MAX];
    if (sub == FNOS_WIFI_SUBMIT_PENDING) {
        snprintf(full, sizeof full, "正在按新凭据连接 %s…（口令或加密方式不合适会在这里失败）", ssid);
    } else if (sub == FNOS_WIFI_SUBMIT_FAILED) {
        snprintf(full, sizeof full, "连不上 %s：%s（第 %u 次重试）", ssid,
                 sub_reason[0] ? sub_reason : "这一轮没连上", (unsigned)s_wifi_retry_count);
    } else if (!s_configured) {
        snprintf(full, sizeof full, "未配置 Wi-Fi");
    } else if (s_online) {
        char ip[16];
        portENTER_CRITICAL(&s_ip_mux);
        snprintf(ip, sizeof ip, "%s", s_ip);
        portEXIT_CRITICAL(&s_ip_mux);
        snprintf(full, sizeof full, "已连接 %s（%s）", ssid, ip);
    } else if (reason[0]) {
        snprintf(full, sizeof full, "连不上 %s：%s（第 %u 次重试）", ssid, reason,
                 (unsigned)s_wifi_retry_count);
    } else {
        snprintf(full, sizeof full, "正在连接 %s…", ssid);
    }
    fnos_utf8_copy(out, cap, full);         // 只可能在字符边界上截断（审计 M10）
}

const char *fnos_net_state_str(void)
{
    static char buf[WIFI_STATE_STR_MAX];
    fnos_net_state_str_r(buf, sizeof buf);
    return buf;
}

/* ── 本轮配网提交状态的对外接口（审计 M04 + M05）──────────────────── */

fnos_wifi_submit_t fnos_net_submit_state(void)
{
    portENTER_CRITICAL(&s_cfg_mux);
    fnos_wifi_submit_t state = (fnos_wifi_submit_t)s_submit_state;
    portEXIT_CRITICAL(&s_cfg_mux);
    return state;
}

void fnos_net_submit_reason_r(char *out, size_t cap)
{
    if (!out || !cap) return;
    portENTER_CRITICAL(&s_cfg_mux);
    fnos_utf8_copy(out, cap, s_submit_reason);
    portEXIT_CRITICAL(&s_cfg_mux);
}

void fnos_net_submit_clear(void)
{
    portENTER_CRITICAL(&s_cfg_mux);
    /* 提交还在进行中时不清：否则用户一碰输入框，"正在连接"就消失了。 */
    if (s_submit_state != FNOS_WIFI_SUBMIT_PENDING) {
        s_submit_state = FNOS_WIFI_SUBMIT_IDLE;
        s_submit_reason[0] = 0;
    }
    portEXIT_CRITICAL(&s_cfg_mux);
}

const char *fnos_net_ssid(void)
{
    static char buf[sizeof s_ssid];
    fnos_net_ssid_r(buf, sizeof buf);
    return buf;
}

void fnos_net_ssid_r(char *out, size_t cap)
{
    if (!out || !cap) return;
    portENTER_CRITICAL(&s_cfg_mux);
    snprintf(out, cap, "%s", s_ssid);
    portEXIT_CRITICAL(&s_cfg_mux);
}

fnos_net_state_t fnos_net_state(void)
{
    /* 本轮提交的状态优先（审计 M04）：提交中就报"正在连接"，提交失败就报"没连上"，
       不管此刻有没有旧凭据、旧连接 —— 否则首次配网失败会显示成"正在连接…"，
       在线改密码会显示成"连接成功"。 */
    portENTER_CRITICAL(&s_cfg_mux);
    int sub = s_submit_state;
    bool configured = s_configured;
    bool has_reason = s_reason[0] != 0;
    portEXIT_CRITICAL(&s_cfg_mux);
    if (sub == FNOS_WIFI_SUBMIT_PENDING) return FNOS_NET_CONNECTING;
    if (sub == FNOS_WIFI_SUBMIT_FAILED)  return FNOS_NET_FAILED;
    if (!configured)       return FNOS_NET_UNCONFIGURED;
    if (s_online)          return FNOS_NET_ONLINE;
    if (has_reason)        return FNOS_NET_FAILED;
    return FNOS_NET_CONNECTING;
}

void fnos_net_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return;
    /* 调用方是 LVGL 回调（fnos_ui.c 的配网页）与串口 CLI：这里只把凭据拷进待办
       缓冲就返回。落 NVS 与 esp_wifi_disconnect/set_config/connect 都是 ESP-Hosted
       同步 RPC（最坏几秒），交给 fnos_net_service() 在普通任务里做（审计 M05）。 */
    portENTER_CRITICAL(&s_cfg_mux);
    snprintf(s_pend_ssid, sizeof s_pend_ssid, "%s", ssid);
    snprintf(s_pend_pass, sizeof s_pend_pass, "%s", pass ? pass : "");
    s_cred_want = true;
    s_submit_generation++;
    if (!s_submit_generation) s_submit_generation++;
    s_submit_applied_generation = 0;
    s_submit_associated_generation = 0;
    /* 从这一刻起屏幕进入"按新凭据连接中"（审计 M04 + M05）：不看旧凭据、不看旧连接，
       看这一次提交什么时候有结论。 */
    s_submit_state = FNOS_WIFI_SUBMIT_PENDING;
    s_submit_reason[0] = 0;
    s_submit_started_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_cfg_mux);
}

/* service() 里执行：落 NVS → 按新凭据重连。全程同步 RPC，只能在普通任务里跑。 */
static void submit_failure(uint32_t generation, const char *reason)
{
    portENTER_CRITICAL(&s_cfg_mux);
    if (generation == s_submit_generation && s_submit_state == FNOS_WIFI_SUBMIT_PENDING) {
        s_submit_state = FNOS_WIFI_SUBMIT_FAILED;
        fnos_utf8_copy(s_submit_reason, sizeof s_submit_reason, reason);
    }
    portEXIT_CRITICAL(&s_cfg_mux);
}

static void apply_pending_credentials(void)
{
    char ssid[sizeof s_pend_ssid], pass[sizeof s_pend_pass];
    uint32_t generation;
    portENTER_CRITICAL(&s_cfg_mux);
    if (!s_cred_want) { portEXIT_CRITICAL(&s_cfg_mux); return; }
    s_cred_want = false;
    generation = s_submit_generation;
    memcpy(ssid, s_pend_ssid, sizeof ssid);
    memcpy(pass, s_pend_pass, sizeof pass);
    memset(s_pend_pass, 0, sizeof s_pend_pass);
    portEXIT_CRITICAL(&s_cfg_mux);
    ssid[sizeof ssid - 1] = 0;
    pass[sizeof pass - 1] = 0;
    if (!ssid[0]) return;
    if (!fnos_wifi_store_save(ssid, pass)) {
        submit_failure(generation, "凭据保存失败（NVS 写入错误）");
        memset(pass, 0, sizeof pass);
        ESP_LOGE(TAG, "凭据保存失败：新凭据未生效");
        return;
    }
    portENTER_CRITICAL(&s_cfg_mux);
    bool current = generation == s_submit_generation;
    if (current) {
        snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
        s_reason[0] = 0;
        s_configured = true;
        s_retry_due_us = 0;
    }
    portEXIT_CRITICAL(&s_cfg_mux);
    if (!current) { memset(pass, 0, sizeof pass); return; }
    s_wifi_retry_count = 0;
    wifi_config_t cfg = { 0 };
    memcpy(cfg.sta.ssid, ssid, strnlen(ssid, sizeof cfg.sta.ssid));
    memcpy(cfg.sta.password, pass, strnlen(pass, sizeof cfg.sta.password));
    cfg.sta.threshold.authmode = wifi_authmode_for_password(pass);
    memset(pass, 0, sizeof pass);
    esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        submit_failure(generation, "连接失败（信号太弱？）");
        memset(&cfg, 0, sizeof cfg);
        return;
    }
    memset(&cfg, 0, sizeof cfg);
    portENTER_CRITICAL(&s_cfg_mux);
    current = generation == s_submit_generation;
    if (current) s_submit_applied_generation = generation;
    portEXIT_CRITICAL(&s_cfg_mux);
    if (current && esp_wifi_connect() != ESP_OK) {
        submit_failure(generation, "连接失败（信号太弱？）");
        wifi_schedule_retry();
    }
}

bool fnos_net_online(void) {
    return s_online;
}

const char *fnos_net_ip(void) {
    static char buf[16];                    // 返回值在下次调用前有效
    portENTER_CRITICAL(&s_ip_mux);
    memcpy(buf, s_ip, sizeof(buf));
    buf[sizeof(buf) - 1] = 0;
    portEXIT_CRITICAL(&s_ip_mux);
    return (s_online && buf[0]) ? buf : "";
}

// 纯内存读：UI 每帧都会调，绝不能在这里发 RPC
int8_t fnos_net_rssi(void) {
    return s_online ? s_rssi : 0;
}

// 只在非 UI 任务里调用：短 TTL 的 RSSI 刷新
void fnos_net_poll_rssi(void) {    int64_t now = esp_timer_get_time() / 1000;
    if (now - s_rssi_ms < 5000) return;
    s_rssi_ms = now;
    if (!s_online) return;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_rssi = (int8_t)ap.rssi;
}

/* ── 扫描实现（两个都只能在普通任务里跑，见 fnos_net.h 的说明）────────── */

static void scan_begin(void) {
    wifi_scan_config_t cfg = { 0 };
    cfg.show_hidden = false;                    // 隐藏网络没名字，配网列表里选了也没法确认
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;      // 主动扫描：结果全、快；代价是稍微费点电
    s_scan_failed = false;
    s_scan_busy = true;
    esp_err_t err = esp_wifi_scan_start(&cfg, false);   // false = 异步，扫完发 SCAN_DONE
    if (err != ESP_OK) {
        s_scan_busy = false;
        s_scan_failed = true;
        ESP_LOGW(TAG, "scan start failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "scan started");
    }
}

static void scan_collect(void) {
    uint16_t num = 0;
    esp_err_t err = esp_wifi_scan_get_ap_num(&num);
    if (err != ESP_OK || num == 0) {
        s_scan_failed = err != ESP_OK;
        portENTER_CRITICAL(&s_ap_mux);
        s_ap_n = 0;
        portEXIT_CRITICAL(&s_ap_mux);
        s_scan_busy = false;
        if (err != ESP_OK) ESP_LOGW(TAG, "scan result query failed: %s", esp_err_to_name(err));
        else ESP_LOGI(TAG, "scan done: 0 个 AP");
        return;
    }
    if (num > 64) num = 64;                     // 驱动上限内收一批，再按信号筛
    wifi_ap_record_t *recs = calloc(num, sizeof *recs);
    if (!recs) { s_scan_busy = false; s_scan_failed = true; return; }
    uint16_t got = num;
    if (esp_wifi_scan_get_ap_records(&got, recs) != ESP_OK) {
        free(recs);
        s_scan_busy = false;
        s_scan_failed = true;
        return;
    }
    qsort(recs, got, sizeof *recs, ap_cmp_rssi);        // 先排序：同名去重留下的就是最强的那条
    /* 整段（清零条数 → 回填 → 放条数）在锁里做，读侧拿同一把锁拷贝整批：
       以前的"先清零再回填"只保证条数不撕裂，UI 仍可能读到正在被改写一半的 SSID
       （审计 M09）。锁里只有纯内存操作，不放任何 RPC。 */
    portENTER_CRITICAL(&s_ap_mux);
    s_ap_n = 0;
    int n = 0;
    for (int i = 0; i < (int)got && n < FNOS_AP_MAX; i++) {
        if (recs[i].ssid[0] == 0) continue;
        bool dup = false;
        for (int k = 0; k < n; k++) {
            if (strcmp(s_aps[k].ssid, (const char *)recs[i].ssid) == 0) { dup = true; break; }
        }
        if (dup) continue;                              // 同一 SSID 的多个 BSS：只留最强
        snprintf(s_aps[n].ssid, sizeof s_aps[n].ssid, "%s", (const char *)recs[i].ssid);
        s_aps[n].rssi = (int8_t)recs[i].rssi;
        s_aps[n].secure = recs[i].authmode != WIFI_AUTH_OPEN;
        n++;
    }
    s_ap_n = n;
    portEXIT_CRITICAL(&s_ap_mux);
    free(recs);
    s_scan_busy = false;
    ESP_LOGI(TAG, "scan done: %d 个 AP", n);
}

void fnos_net_scan_request(void) {
    s_scan_failed = false;
    s_scan_want = true;
}

bool fnos_net_scan_busy(void)   { return s_scan_busy || s_scan_want; }
bool fnos_net_scan_failed(void) { return s_scan_failed; }

int fnos_net_scan_results(fnos_ap_t *out, int max) {
    if (!out || max <= 0) return 0;
    int n;
    portENTER_CRITICAL(&s_ap_mux);      // 与 scan_collect 同一把锁：只会看到完整的一批
    n = s_ap_n;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = s_aps[i];
    portEXIT_CRITICAL(&s_ap_mux);
    return n;
}

void fnos_net_scan_clear(void) {
    portENTER_CRITICAL(&s_ap_mux);
    s_ap_n = 0;
    portEXIT_CRITICAL(&s_ap_mux);
    s_scan_failed = false;
}

// 只在非 UI 任务里调用：重连 + 起站重试 + 扫描（扫描是 RPC，绝不能进 UI 任务）
void fnos_net_service(void) {
    /* 本轮配网提交的超时兜底（审计 M04）：PENDING 超过 WIFI_SUBMIT_TIMEOUT_US
       还没有任何结论（既没拿到 IP、也没有带原因的断开事件，例如 AP 一直不回），
       就判失败 —— 屏幕上不许永远停在"正在连接…"。 */
    int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_cfg_mux);
    if (s_submit_state == FNOS_WIFI_SUBMIT_PENDING &&
        now_us - s_submit_started_us > WIFI_SUBMIT_TIMEOUT_US) {
        s_submit_state = FNOS_WIFI_SUBMIT_FAILED;
        if (!s_submit_reason[0])
            fnos_utf8_copy(s_submit_reason, sizeof s_submit_reason,
                           "连接超时（口令、加密方式或信号？）");
    }
    portEXIT_CRITICAL(&s_cfg_mux);
    apply_pending_credentials();
    if (atomic_exchange(&s_reassociate, false) && s_configured && !s_online) {
        esp_err_t err = esp_wifi_disconnect();
        if (err != ESP_OK) ESP_LOGW(TAG, "wifi reassociate disconnect failed: %s", esp_err_to_name(err));
        /* DISCONNECTED 通常也会排重连；这里兜底一次，防止事件丢失后停在离线。 */
        wifi_schedule_retry();
    }
    portENTER_CRITICAL(&s_cfg_mux);
    bool retry_due = s_retry_due_us && now_us >= s_retry_due_us;
    if (retry_due) s_retry_due_us = 0;
    portEXIT_CRITICAL(&s_cfg_mux);
    if (retry_due && !s_online && s_configured) s_connect_wanted = true;
    if (s_wifi_inited && !s_scan_busy && atomic_exchange(&s_scan_want, false)) {
        scan_begin();
    }
    if (atomic_exchange(&s_scan_done, false)) {
        scan_collect();
    }
    if (atomic_exchange(&s_connect_wanted, false) && !s_online && s_configured) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "wifi reconnect failed: %s", esp_err_to_name(err));
            wifi_schedule_retry();
        }
    }
    if (!s_started) {
        // 起站失败（esp_wifi_init/start 出错）时周期重试，但别每 500 ms 试一次刷日志
        static int64_t last_try_ms;
        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_try_ms > 5000) {
            last_try_ms = now;
            fnos_net_start();
        }
    }
}
