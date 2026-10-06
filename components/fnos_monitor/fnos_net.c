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
static volatile int8_t s_rssi;          // 缓存值：UI 只读它，不触发 RPC
static volatile int64_t s_rssi_ms;
static volatile bool s_connect_wanted;  // 重连请求标志（定时器只置位，不直接调 esp_wifi_connect）
static volatile bool s_online;
static volatile uint8_t s_wifi_retry_count;
static esp_timer_handle_t s_wifi_retry_timer;
static bool s_started;                  // fnos_net_start() 幂等标志
static bool s_wifi_inited;

/* ── 扫描缓存（板上配网用）──────────────────────────────────────────────
   写入方是 fnos_net_service()（普通任务），读取方是 LVGL 任务。跨任务靠"先清零
   条数、填完再放条数"来保证一致：读侧只会看到空表或完整的一批，不会看到半条。 */
static fnos_ap_t s_aps[FNOS_AP_MAX];
static volatile int  s_ap_n;
static volatile bool s_scan_busy;
static volatile bool s_scan_failed;
static volatile bool s_scan_want;       // 界面请求扫描 → service() 里真正发起
static volatile bool s_scan_done;       // 事件任务收到 SCAN_DONE → service() 里取结果

static int ap_cmp_rssi(const void *a, const void *b) {
    int ra = ((const wifi_ap_record_t *)a)->rssi, rb = ((const wifi_ap_record_t *)b)->rssi;
    return rb - ra;                     // 信号强的排前面（rssi 是负值，越大越强）
}
static bool s_handlers_registered;
// ── 给用户看的 Wi-Fi 状态 ──────────────────────────────────────────
// 以前只有 ESP_LOG，屏幕上看不到"为什么连不上"。配网引导要靠这两样：
// 有没有凭据（configured）、以及最近一次失败的人话原因。
static bool s_configured;               // NVS/编译期里有可用凭据
static char s_ssid[64] = "";
static char s_reason[72] = "";          // 最近一次断线原因（人话），连上就清空

// 断线重连：指数退避，最长 30s；凭据只在配置头里，日志永不打印。
// 回调跑在 esp_timer 任务里（LVGL 的 tick 也在那个任务），而 esp_wifi_connect() 是
// ESP-Hosted 同步 RPC，最坏阻塞 5 秒会把整个界面（含触摸）一起卡住 —— 所以这里只置标志，
// 真正的调用交给 fnos_net_service() 在轮询任务里做。
static void wifi_retry_cb(void *arg) {
    (void)arg;
    if (!s_online) s_connect_wanted = true;
}

static void wifi_schedule_retry(void) {
    if (!s_wifi_retry_timer) return;
    uint8_t attempt = s_wifi_retry_count > 5 ? 5 : s_wifi_retry_count;
    uint64_t delay_ms = 1000ULL << attempt;
    if (delay_ms > 30000) delay_ms = 30000;
    esp_timer_stop(s_wifi_retry_timer);
    esp_timer_start_once(s_wifi_retry_timer, delay_ms * 1000ULL);
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
    default:                                 return NULL;
    }
}

static void on_net_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_configured) esp_wifi_connect();
        else ESP_LOGW(TAG, "没有 Wi-Fi 凭据：不自动连接，等待现场配网");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_retry_count = s_wifi_retry_count < 15 ? s_wifi_retry_count + 1 : 15;
        mark_offline();
        wifi_schedule_retry();
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        uint8_t rs = d ? d->reason : 0;
        const char *rt = reason_text(rs);
        if (rt) snprintf(s_reason, sizeof s_reason, "%s", rt);
        else    snprintf(s_reason, sizeof s_reason, "原因码 %u", (unsigned)rs);
        ESP_LOGW(TAG, "wifi disconnected reason=%u (%s) retry=%u", (unsigned)rs,
                 s_reason, (unsigned)s_wifi_retry_count);
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
        s_reason[0] = 0;                    // 连上了，上一次的失败原因作废
        s_wifi_retry_count = 0;
        if (s_wifi_retry_timer) esp_timer_stop(s_wifi_retry_timer);
        if (s_eg) xEventGroupSetBits(s_eg, BIT_IP);
        ESP_LOGI(TAG, "got ip %s", s_ip);
        fnos_data_network_changed();
        sntp_start_once();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        mark_offline();
        ESP_LOGW(TAG, "ip lost");
    }
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
    if (!s_wifi_retry_timer) {
        const esp_timer_create_args_t retry_args = { .callback = wifi_retry_cb, .name = "wifi_retry" };
        err = esp_timer_create(&retry_args, &s_wifi_retry_timer);
        if (err != ESP_OK) {
            s_wifi_retry_timer = NULL;      // 少一个定时器只是失去自动重连，不阻断起站
            ESP_LOGW(TAG, "wifi retry timer unavailable: %s", esp_err_to_name(err));
        }
    }
    char ssid[64] = "", pass[96] = "";
    s_configured = fnos_wifi_store_load(ssid, sizeof ssid, pass, sizeof pass);
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
    if (!s_configured) {
        ESP_LOGW(TAG, "Wi-Fi 未配置（NVS 与编译期都没有可用 SSID）——起站但不自动连接");
    }
    wifi_config_t cfg = { 0 };
    // 用 snprintf 而不是 strncpy：源缓冲比 wifi_config_t 的字段大，strncpy 会触发
    // -Werror=stringop-truncation（构建直接失败）。两者都是"截断即安全"的语义。
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", pass);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
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

const char *fnos_net_state_str(void)
{
    static char buf[160];
    if (!s_configured) {
        snprintf(buf, sizeof buf, "未配置 Wi-Fi");
    } else if (s_online) {
        char ip[16];
        portENTER_CRITICAL(&s_ip_mux);
        snprintf(ip, sizeof ip, "%s", s_ip);
        portEXIT_CRITICAL(&s_ip_mux);
        snprintf(buf, sizeof buf, "已连接 %s（%s）", s_ssid, ip);
    } else if (s_reason[0]) {
        snprintf(buf, sizeof buf, "连不上 %s：%s（第 %u 次重试）", s_ssid, s_reason,
                 (unsigned)s_wifi_retry_count);
    } else {
        snprintf(buf, sizeof buf, "正在连接 %s…", s_ssid);
    }
    return buf;
}

const char *fnos_net_ssid(void)
{
    return s_ssid;
}

fnos_net_state_t fnos_net_state(void)
{
    if (!s_configured)     return FNOS_NET_UNCONFIGURED;
    if (s_online)          return FNOS_NET_ONLINE;
    if (s_reason[0])       return FNOS_NET_FAILED;
    return FNOS_NET_CONNECTING;
}

void fnos_net_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return;
    if (!fnos_wifi_store_save(ssid, pass)) return;
    s_configured = true;
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
    s_reason[0] = 0;
    s_wifi_retry_count = 0;
    // 立刻按新凭据重连：先断开（可能正连着旧的），再 set_config + connect
    wifi_config_t cfg = { 0 };
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", pass ? pass : "");
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) == ESP_OK) esp_wifi_connect();
    else ESP_LOGE(TAG, "set_config 失败，新凭据要重启后才生效");
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
    if (esp_wifi_scan_get_ap_num(&num) != ESP_OK || num == 0) {
        s_ap_n = 0;
        s_scan_busy = false;
        ESP_LOGI(TAG, "scan done: 0 个 AP");
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
    s_ap_n = 0;                                         // ← 先清零：读侧不会看到半批数据
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
    free(recs);
    s_ap_n = n;                                         // ← 填完再放条数
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
    int n = s_ap_n;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = s_aps[i];
    return n;
}

void fnos_net_scan_clear(void) {
    s_ap_n = 0;
    s_scan_failed = false;
}

// 只在非 UI 任务里调用：重连 + 起站重试 + 扫描（扫描是 RPC，绝不能进 UI 任务）
void fnos_net_service(void) {
    if (s_scan_want && s_wifi_inited && !s_scan_busy) {
        s_scan_want = false;
        scan_begin();
    }
    if (s_scan_done) {
        s_scan_done = false;
        scan_collect();
    }
    if (s_connect_wanted) {
        s_connect_wanted = false;
        esp_wifi_connect();
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
