// fnOS 状态数据层：1 Hz 轮询 NAS 上的采集端，解析 JSON 成定长快照 + 曲线环形缓冲。
//
// 连接参数（地址/端口/令牌/是否 HTTPS/固定证书）来自 fnos_pair：没配对时就是
// 编译期默认值（明文 HTTP），配对之后换成"独立遥测端口 + 配对令牌 + 固定证书"，
// 行为与配对前完全一致的那条路不会被弄坏。
//
// 关于 TLS 与内存：本板内部 RAM 只有 ~361 KB，mbedTLS 收发缓冲历史上把内部分配
// 打光、导致 LVGL 任务卡死（见 p4-7b-unifi-app README）。本工程已打开
// CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC 与 CONFIG_MBEDTLS_DYNAMIC_BUFFER，mbedTLS 的
// 缓冲改从 PSRAM 走；HTTP 客户端与 cJSON 的缓冲本来就在 PSRAM。
#include "fnos_data.h"
#include "fnos_net.h"
#include "fnos_pair.h"
#if __has_include("fnos_config.h")
#include "fnos_config.h"
#else
#include "fnos_config.example.h"
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_tls_errors.h"          // 连接层错误归类（见 classify_conn_err）
#include <sys/time.h>                 // settimeofday：SNTP 不可用时改用 NAS 的时间
#include <time.h>
#include "cJSON.h"

#ifndef FNOS_HOST
// 中性兜底（同 fnos_pair.c）：通用构建里不能带某台 NAS 的真实地址。
#define FNOS_HOST "nas.local"
#endif
#ifndef FNOS_PORT
#define FNOS_PORT 8799
#endif
#ifndef FNOS_TOKEN
#define FNOS_TOKEN ""
#endif

static const char *TAG = "fnos_data";

#define POLL_INTERVAL_MS 1000
#define POLL_TIMEOUT_MS  3000
#define POLL_BACKOFF_MAX 5000
/* Byte budgets are configuration, not hardware inventory limits. */
#ifndef CONFIG_FNOS_STATUS_MAX_BYTES
#define CONFIG_FNOS_STATUS_MAX_BYTES (256 * 1024)
#endif
#define HIST_BUF_SIZE    (32 * 1024)

// 大块数据都放 PSRAM（内部 RAM 留给 Wi-Fi/显示/DMA）
static EXT_RAM_BSS_ATTR fnos_status_t s_status;
static EXT_RAM_BSS_ATTR float s_h_cpu[FNOS_HIST_MAX];
static EXT_RAM_BSS_ATTR float s_h_mem[FNOS_HIST_MAX];
static EXT_RAM_BSS_ATTR float s_h_rx[FNOS_HIST_MAX];
static EXT_RAM_BSS_ATTR float s_h_tx[FNOS_HIST_MAX];

static int64_t s_seq;                       // 已产生样本总数（单调递增）
static volatile bool s_valid;               // 是否拿到过有效帧（与 s_status 同锁维护）
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static volatile bool s_session_reset;

/* 最近 64 次请求耗时（含失败），给长测的 P95 用。只在这里写、只在诊断接口里读，
   不进 PSRAM 大对象，也不参与调度决策。 */
#define HTTP_RING_N 64
static int s_http_ring[HTTP_RING_N];
static int s_http_ring_n;                    // 已收集样本数（<= HTTP_RING_N）
static int s_http_ring_i;                    // 下一个写入位置

static char s_url_status[160];
static char s_url_hist[160];

// 生效的连接参数（由 fnos_pair 提供）。s_cert_pem 必须是文件级静态：esp_http_client
// 不复制 cert_pem，句柄活着期间指针得一直有效。
static EXT_RAM_BSS_ATTR fnos_pair_cfg_t s_conn;
static EXT_RAM_BSS_ATTR char s_cert_pem[FNOS_PAIR_PEM_MAX];
static char s_token[80];
static bool s_conn_tls;
static bool s_conn_usable;             // false = 配了 HTTPS 却没有证书，拒绝连接
static uint32_t s_conn_gen;
/* 最近一次连接层失败归类（http_get 填，poll_task 拿它当 last_err）*/
static char s_conn_err[24];
/* 最近一帧里 NAS 的挂钟时间（epoch 秒），poll_task 用它给板子校时 */
static int64_t s_nas_ts;

// ────────────────────────────── 小工具

// cJSON 的节点分配默认落内部 RAM；这个 payload 有几百个节点，必须改道 PSRAM。
static void *cjson_psram_malloc(size_t sz)
{
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(sz);              // PSRAM 满时退回内部 RAM，不直接失败
}

static void cjson_psram_free(void *p)
{
    free(p);
}

// ────────────────────────────── JSON 解析

static bool parse_status(const char *js, fnos_status_t *st)
{
    const char *reason = NULL;
    if (!fnos_status_parse(js, st, &reason)) {
        if (reason) snprintf(s_conn_err, sizeof s_conn_err, "%s", reason);
        return false;
    }
    s_nas_ts = st->source_ts;
    return true;
}

static bool parse_history(const char *js)
{
    cJSON *root = cJSON_Parse(js);
    if (!root) return false;
    const cJSON *rows = cJSON_GetObjectItemCaseSensitive(root, "rows");
    const cJSON *row = NULL;
    int n = 0;
    if (!s_lock || xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        cJSON_Delete(root);
        return false;
    }
    int64_t seq = s_seq;                    // 接在已有样本之后：别把刚收到的实时样本冲掉
    int64_t ts_first = 0, ts_last = 0;      // 第 0 列是采集端的 epoch 秒
    cJSON_ArrayForEach(row, rows) {
        const cJSON *t = cJSON_GetArrayItem(row, 0);
        /* 时间戳只用来算"跨度"（首尾相减），绝对值和板子的时钟无关，
           所以 SNTP 没同步也不影响；但明显不是 epoch 的值（0、负数、毫秒）要挡掉。 */
        if (cJSON_IsNumber(t) && t->valuedouble > 1e9 && t->valuedouble < 4e9) {
            if (!ts_first) ts_first = (int64_t)t->valuedouble;
            ts_last = (int64_t)t->valuedouble;
        }
        const cJSON *v0 = cJSON_GetArrayItem(row, 1);
        const cJSON *v1 = cJSON_GetArrayItem(row, 2);
        const cJSON *v2 = cJSON_GetArrayItem(row, 3);
        const cJSON *v3 = cJSON_GetArrayItem(row, 4);
        if (!cJSON_IsNumber(v0) || !cJSON_IsNumber(v1) || !cJSON_IsNumber(v2) || !cJSON_IsNumber(v3)) {
            continue;
        }
        int idx = (int)(seq % FNOS_HIST_MAX);
        s_h_cpu[idx] = (float)v0->valuedouble;
        s_h_mem[idx] = (float)v1->valuedouble;
        s_h_rx[idx]  = (float)v2->valuedouble;
        s_h_tx[idx]  = (float)v3->valuedouble;
        seq++;
        n++;
    }
    if (n > 0) {
        s_seq = seq;
        /* 直接写进快照：fnos_data_get 是整份拷贝，UI 不用再多一个取数接口。
           此刻已经持有 s_lock。 */
        if (ts_first && ts_last > ts_first) {
            s_status.hist_ts_ok = true;
            s_status.hist_span_s = (int)(ts_last - ts_first);
            s_status.hist_avg_gap_x10 = n > 1 ? (int)((ts_last - ts_first) * 10 / (n - 1)) : 0;
        } else {
            s_status.hist_ts_ok = false;
            s_status.hist_span_s = 0;
            s_status.hist_avg_gap_x10 = 0;
        }
    }
    xSemaphoreGive(s_lock);
    cJSON_Delete(root);
    if (n > 0) {
        ESP_LOGI(TAG, "history backfilled: %d samples", n);
    }
    return n > 0;
}

// ────────────────────────────── HTTP

static esp_http_client_handle_t s_client;

static void http_drop_client(void)
{
    if (s_client) {
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }
}

// 从 fnos_pair 取一次生效参数，重建 URL。参数变了就丢弃现有连接——换了证书或
// 换了协议之后，旧句柄里缓存的 TLS 上下文和 keep-alive 连接都不能再用。
// 只在轮询任务里调用（s_url_* 是它自己读写的）。
static void apply_endpoint(void)
{
    fnos_pair_active(&s_conn);
    s_conn_gen = fnos_pair_generation();

    snprintf(s_url_status, sizeof(s_url_status), "%s://%s:%d/api/v1/status",
             s_conn.tls ? "https" : "http", s_conn.host, s_conn.port);
    /* 带上 ?n=：声明板子最多能收多少行。NAS 的 hist_len 可以配到 3600 行
       （约 151 KB），而这里的接收缓冲只有 HIST_BUF_SIZE —— 超出的部分会被截断、
       JSON 解不出来，于是每秒重试一次、曲线回填永远不成功，而且**用户完全看不出**。
       板子自己的环形缓冲就只有 FNOS_HIST_MAX 格，多要也没用。 */
    snprintf(s_url_hist, sizeof(s_url_hist), "%s://%s:%d/api/v1/history?n=%d",
             s_conn.tls ? "https" : "http", s_conn.host, s_conn.port, FNOS_HIST_MAX);
    snprintf(s_token, sizeof(s_token), "%s", s_conn.token);
    s_conn_tls = s_conn.tls;
    snprintf(s_cert_pem, sizeof(s_cert_pem), "%s", s_conn.cert_pem);

    // 不允许静默降级：配了 HTTPS 却没有证书时宁可连不上并报错，也不退回明文。
    // 板子侧没有任何"跳过证书校验"的开关，这是有意为之（见 docs 里的 P2 节）。
    s_conn_usable = !(s_conn.tls && s_cert_pem[0] == 0);
    if (!s_conn_usable) {
        ESP_LOGE(TAG, "配了 HTTPS 但没有固定证书，拒绝连接（不会退回明文）");
    }
    http_drop_client();
}

// 返回 true 表示读到了完整的响应体；*out_status 为 HTTP 状态码，*out_ms 为耗时。
/* 把连接层错误归类成几个短标记，UI 再翻成人话（见 fnos_ui.c 的 link_reason）。
   esp-tls 不区分"证书对不上"和别的握手失败，都报 HANDSHAKE_FAILED，所以这里只能
   说"握手失败"——配上 401/403 归出来的 "token rejected"，用户至少能分清
   "网络不通"和"信任出问题"这两类完全不同的故障，而不是统统看到"离线"。 */
static const char *classify_conn_err(esp_err_t err)
{
    switch (err) {
    case ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED:    return "tls handshake";
    case ESP_ERR_MBEDTLS_SSL_SETUP_FAILED:        return "tls setup";
    case ESP_ERR_MBEDTLS_X509_CRT_PARSE_FAILED:   return "cert parse";
    case ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME: return "dns fail";
    case ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET:
    case ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST:
    case ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT:      return "connect/timeout";
    default:                                      return NULL;
    }
}

static bool http_get(const char *url, char **buffer, size_t *capacity, size_t budget, int *out_status, int *out_len, int *out_ms)
{
    s_conn_err[0] = 0;
    if (!s_conn_usable) {
        snprintf(s_conn_err, sizeof s_conn_err, "%s", "no cert");
        *out_status = 0;
        *out_len = 0;
        *out_ms = 0;
        return false;
    }
    if (!s_client) {
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = POLL_TIMEOUT_MS,
            .buffer_size = 1536,
            .buffer_size_tx = 512,
            .keep_alive_enable = true,
            .disable_auto_redirect = true,
            // 信任锚点是配对时用户核对过的那一张证书，不是证书里的名字：
            // NAS 换 IP（DHCP）时名字校验会让一条本来完全可信的连接失败。
            .skip_cert_common_name_check = true,
        };
        if (s_conn_tls && s_cert_pem[0]) {
            cfg.cert_pem = s_cert_pem;
        }
        s_client = esp_http_client_init(&cfg);
        if (!s_client) {
            return false;
        }
    } else {
        esp_http_client_set_url(s_client, url);
    }
    if (s_token[0]) {
        esp_http_client_set_header(s_client, "X-Token", s_token);
    }

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_open(s_client, 0);
    if (err != ESP_OK) {
        const char *cls = classify_conn_err(err);
        if (cls) snprintf(s_conn_err, sizeof s_conn_err, "%s", cls);
        *out_status = 0;
        *out_ms = (int)((esp_timer_get_time() - t0) / 1000);
        http_drop_client();                 // 连接层失败：下一轮重建
        return false;
    }
    // fetch_headers 失败（头没读全）时必须丢弃句柄：keep-alive 连接里残留的 body
    // 会被下一次请求当成响应解析，而 status_code 只在头解析成功时才更新 —— 一次失败
    // 会污染后面若干轮，属于"静默且粘滞"的故障。
    if (esp_http_client_fetch_headers(s_client) < 0) {
        *out_status = 0;
        *out_ms = (int)((esp_timer_get_time() - t0) / 1000);
        http_drop_client();
        return false;
    }
    *out_status = esp_http_client_get_status_code(s_client);

    int total = 0;
    while (!esp_http_client_is_complete_data_received(s_client)) {
        if ((size_t)total + 1 >= *capacity) {
            size_t next = *capacity ? *capacity * 2 : 4096;
            if (next > budget + 1) next = budget + 1;
            char *grown = next > *capacity ? heap_caps_realloc(*buffer, next,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
            if (!grown) {
                snprintf(s_conn_err, sizeof s_conn_err, "%s", "data capacity");
                err = ESP_ERR_NO_MEM;
                break;
            }
            *buffer = grown;
            *capacity = next;
        }
        int r = esp_http_client_read(s_client, *buffer + total, (int)*capacity - 1 - total);
        if (r < 0) { err = ESP_FAIL; break; }
        if (r == 0) break;
        total += r;
    }
    if (*buffer) (*buffer)[total] = 0;
    *out_len = total;
    *out_ms = (int)((esp_timer_get_time() - t0) / 1000);
    // 半截响应（缓冲写满 / 连接中断）同样会污染 keep-alive 连接，必须丢弃句柄
    if (err != ESP_OK || !esp_http_client_is_complete_data_received(s_client)) {
        http_drop_client();
        return false;
    }
    return true;
}

// ────────────────────────────── 快照更新

/* 板子的挂钟只来自 SNTP（ntp.aliyun.com）。NAS 用户把外网掐掉是常事，那时
 * SNTP 永远不成功，time() 停在 1970 —— 夜间降背光的时段判断就会错得离谱，
 * 而这条链路上本来就有个现成的时间源：NAS 每秒回一帧，里面带 ts。
 *
 * 只在本机时钟**明显不可信**（早于 2020-09）时才采纳，而且只采纳一次：
 * 一旦 SNTP 或 NAS 把时间调对了就不再插手，绝不会和 SNTP 互相打架。
 * 阈值取 1600000000 ≈ 2020-09-13，正常设备不可能早于它。 */
static void adopt_nas_time(void)
{
    if (s_nas_ts < 1600000000) return;      // NAS 自己都没时间，别跟着错
    time_t now = time(NULL);
    if (now >= 1600000000) return;          // 本机时间可信，不插手
    struct timeval tv = { .tv_sec = (time_t)s_nas_ts, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) == 0) {
        ESP_LOGW(TAG, "本机时钟还没同步（%ld），改用 NAS 的时间 %ld", (long)now, (long)s_nas_ts);
    }
}

static void mark_link_down(void)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_status.online = false;
        s_status.last_status = 0;
        snprintf(s_status.last_err, sizeof(s_status.last_err), "no wifi");
        s_status.fail_ms = esp_timer_get_time() / 1000;
        xSemaphoreGive(s_lock);
    }
}

/* 调用方必须已持锁。失败也记：连不上同样是用户感受到的等待。 */
static void http_ring_push(int ms)
{
    if (ms < 0) ms = 0;
    s_http_ring[s_http_ring_i] = ms;
    s_http_ring_i = (s_http_ring_i + 1) % HTTP_RING_N;
    if (s_http_ring_n < HTTP_RING_N) s_http_ring_n++;
}

static void commit_status(const fnos_status_t *st, int http_ms, int status_code)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    fnos_status_t *dst = &s_status;
    uint32_t ok = dst->ok_count, fail = dst->fail_count;
    fnos_status_copy(dst, st);              // 保留不可变动态清单，旧引用独立释放
    dst->ever_ok = true;
    s_valid = true;
    dst->online = true;
    dst->ok_count = ok + 1;
    dst->fail_count = fail;
    dst->http_ms = http_ms;
    dst->last_status = status_code;
    dst->last_err[0] = 0;
    dst->recv_ms = esp_timer_get_time() / 1000;
    dst->fail_ms = 0;
    http_ring_push(http_ms);

    int idx = (int)(s_seq % FNOS_HIST_MAX);
    s_h_cpu[idx] = dst->cpu.pct;
    s_h_mem[idx] = dst->mem.pct;
    s_h_rx[idx]  = dst->net.rx_kbs;
    s_h_tx[idx]  = dst->net.tx_kbs;
    s_seq++;
    xSemaphoreGive(s_lock);
}

static void note_failure(const char *why, int status_code, int http_ms)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_status.online = false;
    s_status.fail_count++;
    s_status.last_status = status_code;
    s_status.http_ms = http_ms;
    snprintf(s_status.last_err, sizeof(s_status.last_err), "%s", why);
    s_status.fail_ms = esp_timer_get_time() / 1000;
    http_ring_push(http_ms);
    xSemaphoreGive(s_lock);
}

// ────────────────────────────── 轮询任务

static void poll_task(void *arg)
{
    (void)arg;
    char *buf = NULL;
    size_t buf_capacity = 0;
    static EXT_RAM_BSS_ATTR fnos_status_t tmp;   // 解析暂存（PSRAM）
    bool hist_done = false;
    int backoff = 0;
    int log_div = 0;

    while (true) {
        if (s_session_reset) {
            s_session_reset = false;
            http_drop_client();
            if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                s_status.online = false;   // 保留上一帧数值，只把在线标志打掉
                xSemaphoreGive(s_lock);
            }
        }
        fnos_net_service();                 // 重连 / 起站重试：同步 RPC 只能在普通任务里发
        if (fnos_pair_generation() != s_conn_gen) {
            ESP_LOGI(TAG, "连接参数已更新，重建 HTTP 客户端");
            apply_endpoint();               // 刚配对完 / 刚撤销：换到新端点
        }
        if (!fnos_net_online()) {
            mark_link_down();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        fnos_net_poll_rssi();               // 刷新 RSSI 缓存，UI 那边只读内存

        int status_code = 0, len = 0, ms = 0;
        bool ok = http_get(s_url_status, &buf, &buf_capacity, CONFIG_FNOS_STATUS_MAX_BYTES, &status_code, &len, &ms);
        if (ok && status_code == 200 && parse_status(buf, &tmp)) {
            commit_status(&tmp, ms, status_code);
            adopt_nas_time();
            backoff = 0;
            if (!hist_done) {               // 首次成功：回填开机前的曲线
                char *hbuf = NULL;
                size_t hcap = 0;
                int hs = 0, hl = 0, hm = 0;
                if (http_get(s_url_hist, &hbuf, &hcap, HIST_BUF_SIZE, &hs, &hl, &hm) && hs == 200)
                    hist_done = parse_history(hbuf);
                free(hbuf);
            }
            if (++log_div >= 30) {          // 每 30 秒一条，够串口核对
                log_div = 0;
                ESP_LOGI(TAG, "poll ok=%u fail=%u %dms cpu=%.1f%% mem=%.1f%% rx=%.1f tx=%.1f KB/s alerts=%d "
                              "internal=%uKB dma=%uKB psram=%uKB",
                         (unsigned)s_status.ok_count, (unsigned)s_status.fail_count, ms,
                         tmp.cpu.pct, tmp.mem.pct, tmp.net.rx_kbs, tmp.net.tx_kbs, tmp.nalerts,
                         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024),
                         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            }
            vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
        } else {
            const char *why = !ok ? (s_conn_err[0] ? s_conn_err
                                                   : (status_code == 0 ? "connect/timeout" : "read error"))
                                  : (status_code == 401 || status_code == 403) ? "token rejected"
                                  : (status_code != 200 ? "http status" : (s_conn_err[0] ? s_conn_err : "bad payload"));
            note_failure(why, status_code, ms);
            ESP_LOGW(TAG, "poll failed: %s status=%d %dms", why, status_code, ms);
            backoff += 1000;
            if (backoff > POLL_BACKOFF_MAX) backoff = POLL_BACKOFF_MAX;
            vTaskDelay(pdMS_TO_TICKS(backoff));
        }
    }
}

// ────────────────────────────── 对外接口

void fnos_data_start(void)
{
    if (s_task) {
        return;
    }
    cJSON_Hooks hooks = { .malloc_fn = cjson_psram_malloc, .free_fn = cjson_psram_free };
    cJSON_InitHooks(&hooks);

    fnos_pair_init();                       // 先读 NVS：连接参数可能在 NVS 里
    apply_endpoint();

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            ESP_LOGE(TAG, "mutex alloc failed");
            return;
        }
    }
    s_status.online = false;
    s_status.ever_ok = false;
    snprintf(s_status.last_err, sizeof(s_status.last_err), "starting");

    BaseType_t rc = xTaskCreatePinnedToCoreWithCaps(poll_task, "fnos_poll", 6 * 1024, NULL, 4,
                                                    &s_task, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
    if (rc != pdPASS) {                     // PSRAM 栈建不起来时退回内部 RAM
        rc = xTaskCreatePinnedToCore(poll_task, "fnos_poll", 6 * 1024, NULL, 4, &s_task, tskNO_AFFINITY);
    }
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "poll task create failed");
        s_task = NULL;
        return;
    }
    ESP_LOGI(TAG, "polling %s every %d ms", s_url_status, POLL_INTERVAL_MS);
}

void fnos_data_network_changed(void)
{
    s_session_reset = true;
}

bool fnos_data_get(fnos_status_t *out)
{
    if (!out) {
        return false;
    }
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        // 拿不到锁：保留上一帧数值，并沿用上次的有效性——不能因为一次锁超时
        // 就把整屏数值刷成 "--"、状态丸刷成 NO DATA
        return s_valid;
    }
    fnos_status_copy(out, &s_status);
    bool ok = s_status.ever_ok;
    xSemaphoreGive(s_lock);
    return ok;
}

int64_t fnos_data_hist_seq(void)
{
    return s_seq;
}

int fnos_data_hist_read(int64_t since_seq, fnos_sample_t *out, int max, int64_t *next_seq)
{
    if (!out || max <= 0 || !s_lock) {
        return 0;
    }
    int n = 0;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        if (next_seq) *next_seq = since_seq;
        return 0;
    }
    int64_t seq = s_seq;
    if (since_seq < 0) since_seq = 0;
    if (seq - since_seq > FNOS_HIST_MAX) {  // 落后太多：只给最近一窗
        since_seq = seq - FNOS_HIST_MAX;
    }
    while (since_seq < seq && n < max) {
        int idx = (int)(since_seq % FNOS_HIST_MAX);
        out[n].cpu = s_h_cpu[idx];
        out[n].mem = s_h_mem[idx];
        out[n].rx_kbs = s_h_rx[idx];
        out[n].tx_kbs = s_h_tx[idx];
        n++;
        since_seq++;
    }
    if (next_seq) *next_seq = since_seq;
    xSemaphoreGive(s_lock);
    return n;
}

// ────────────────────────────── 只读诊断（72h 长测）

/* P95：把环形缓冲拷出来插排。64 个 int 的插入排序在诊断回调里跑，
   最坏 ~2000 次比较、每 10 秒一次，代价可以忽略。 */
static int http_ring_p95_locked(void)
{
    if (s_http_ring_n <= 0) return 0;
    int v[HTTP_RING_N];
    for (int i = 0; i < s_http_ring_n; i++) v[i] = s_http_ring[i];
    for (int i = 1; i < s_http_ring_n; i++) {
        int x = v[i], j = i - 1;
        while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
        v[j + 1] = x;
    }
    int idx = (s_http_ring_n * 95) / 100;        // 上取整语义：n=64 -> 60
    if (idx >= s_http_ring_n) idx = s_http_ring_n - 1;
    return v[idx];
}

void fnos_data_diag(fnos_data_diag_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;                                  // 拿不到锁：全 0，调用方按"测不到"处理
    }
    out->ok_count   = s_status.ok_count;
    out->fail_count = s_status.fail_count;
    out->recv_ms    = s_status.recv_ms;
    out->source_ts  = s_status.source_ts;
    out->http_ms    = s_status.http_ms;
    out->uptime_s   = s_status.uptime_s;
    out->online     = s_status.online && s_valid;
    out->p95_ms     = http_ring_p95_locked();
    xSemaphoreGive(s_lock);
}

uint32_t fnos_data_stack_min_free(void)
{
    // ESP-IDF FreeRTOS：uxTaskGetStackHighWaterMark 返回**字节**（本工程没开
    // CONFIG_FREERTOS_SMP，走 IDF 自带实现）。任务没起来就返回 0，让长测
    // 按"测不到"判 INCOMPLETE，而不是当成"栈只剩 0 字节"。
    return s_task ? (uint32_t)uxTaskGetStackHighWaterMark(s_task) : 0;
}
