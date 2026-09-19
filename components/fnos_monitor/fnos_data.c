// fnOS 状态数据层：1 Hz 轮询 NAS 上的 fnos-agent，解析 JSON 成定长快照 + 曲线环形缓冲。
//
// 为什么不用 TLS：本板内部 RAM 只有 ~361 KB，mbedTLS 收发缓冲历史上把内部分配打光，
// 导致 LVGL 任务卡死（见 p4-7b-unifi-app README）。采集端只在内网、只读，
// 所以这条链路走纯 HTTP，HTTP 客户端与 cJSON 的缓冲都放 PSRAM。
#include "fnos_data.h"
#include "fnos_net.h"
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
#include "cJSON.h"

#ifndef FNOS_HOST
#define FNOS_HOST "192.168.0.119"
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
#define RX_BUF_SIZE      (8 * 1024)
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

static char s_url_status[96];
static char s_url_hist[96];

// ────────────────────────────── 小工具

static float jnum(const cJSON *o, const char *k)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(i) ? (float)i->valuedouble : 0.0f;
}

static int jint(const cJSON *o, const char *k)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(i) ? (int)i->valuedouble : 0;
}

static bool jbool(const cJSON *o, const char *k)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsTrue(i);
}

static void jstr(const cJSON *o, const char *k, char *dst, size_t cap)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    dst[0] = 0;
    if (cJSON_IsString(i) && i->valuestring) {
        strncpy(dst, i->valuestring, cap - 1);
        dst[cap - 1] = 0;
    }
}

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
    cJSON *root = cJSON_Parse(js);
    if (!root) {
        return false;
    }
    if (!jbool(root, "ready")) {            // 采集端自检未就绪：当作无效帧
        cJSON_Delete(root);
        return false;
    }

    jstr(root, "host", st->host, sizeof(st->host));
    st->uptime_s = (uint32_t)jint(root, "uptime_s");

    const cJSON *cpu = cJSON_GetObjectItemCaseSensitive(root, "cpu");
    if (cJSON_IsObject(cpu)) {
        st->cpu.pct     = jnum(cpu, "pct");
        st->cpu.load1   = jnum(cpu, "load1");
        st->cpu.load5   = jnum(cpu, "load5");
        st->cpu.load15  = jnum(cpu, "load15");
        st->cpu.temp_c  = jnum(cpu, "temp_c");
        st->cpu.cores   = jint(cpu, "cores");
        st->cpu.runq    = jint(cpu, "runq");
    }
    const cJSON *mem = cJSON_GetObjectItemCaseSensitive(root, "mem");
    if (cJSON_IsObject(mem)) {
        st->mem.total_mb = jnum(mem, "total_mb");
        st->mem.used_mb  = jnum(mem, "used_mb");
        st->mem.avail_mb = jnum(mem, "avail_mb");
        st->mem.pct      = jnum(mem, "pct");
    }
    const cJSON *net = cJSON_GetObjectItemCaseSensitive(root, "net");
    if (cJSON_IsObject(net)) {
        jstr(net, "if", st->net.ifname, sizeof(st->net.ifname));
        st->net.rx_kbs      = jnum(net, "rx_kbs");
        st->net.tx_kbs      = jnum(net, "tx_kbs");
        st->net.rx_total_gb = jnum(net, "rx_total_gb");
        st->net.tx_total_gb = jnum(net, "tx_total_gb");
    }

    st->nvols = 0;
    const cJSON *vols = cJSON_GetObjectItemCaseSensitive(root, "vols");
    const cJSON *it = NULL;
    cJSON_ArrayForEach(it, vols) {
        if (st->nvols >= FNOS_MAX_VOLS) break;
        fnos_vol_t *v = &st->vols[st->nvols];
        jstr(it, "mnt", v->mnt, sizeof(v->mnt));
        jstr(it, "fs", v->fs, sizeof(v->fs));
        v->total_gb = jnum(it, "total_gb");
        v->used_gb  = jnum(it, "used_gb");
        v->free_gb  = jnum(it, "free_gb");
        v->pct      = jnum(it, "pct");
        st->nvols++;
    }

    st->nraid = 0;
    const cJSON *raids = cJSON_GetObjectItemCaseSensitive(root, "raid");
    cJSON_ArrayForEach(it, raids) {
        if (st->nraid >= FNOS_MAX_RAID) break;
        fnos_raid_t *r = &st->raid[st->nraid];
        jstr(it, "dev", r->dev, sizeof(r->dev));
        jstr(it, "lvl", r->lvl, sizeof(r->lvl));
        jstr(it, "state", r->state, sizeof(r->state));
        r->ok       = jbool(it, "ok");
        r->have     = jint(it, "have");
        r->want     = jint(it, "want");
        r->sync_pct = jnum(it, "sync_pct");
        st->nraid++;
    }

    st->ndisks = 0;
    const cJSON *disks = cJSON_GetObjectItemCaseSensitive(root, "disks");
    cJSON_ArrayForEach(it, disks) {
        if (st->ndisks >= FNOS_MAX_DISKS) break;
        fnos_disk_t *d = &st->disks[st->ndisks];
        jstr(it, "dev", d->dev, sizeof(d->dev));
        d->rd_kbs = jnum(it, "rd_kbs");
        d->wr_kbs = jnum(it, "wr_kbs");
        st->ndisks++;
    }

    st->ntemps = 0;
    const cJSON *temps = cJSON_GetObjectItemCaseSensitive(root, "temps");
    cJSON_ArrayForEach(it, temps) {
        if (st->ntemps >= FNOS_MAX_TEMPS) break;
        fnos_temp_t *t = &st->temps[st->ntemps];
        jstr(it, "n", t->n, sizeof(t->n));
        t->c = jnum(it, "c");
        st->ntemps++;
    }

    st->ndocker = 0;
    const cJSON *dockers = cJSON_GetObjectItemCaseSensitive(root, "docker");
    cJSON_ArrayForEach(it, dockers) {
        if (st->ndocker >= FNOS_MAX_DOCKER) break;
        fnos_docker_t *d = &st->docker[st->ndocker];
        jstr(it, "n", d->n, sizeof(d->n));
        d->up = jbool(it, "up");
        jstr(it, "s", d->s, sizeof(d->s));
        st->ndocker++;
    }

    const cJSON *zfs = cJSON_GetObjectItemCaseSensitive(root, "zfs");
    st->has_zfs = cJSON_IsObject(zfs);
    if (st->has_zfs) {
        st->zfs_arc_gb  = jnum(zfs, "arc_gb");
        st->zfs_hit_pct = jnum(zfs, "hit_pct");
    }

    st->nalerts = 0;
    const cJSON *alerts = cJSON_GetObjectItemCaseSensitive(root, "alerts");
    cJSON_ArrayForEach(it, alerts) {
        if (st->nalerts >= FNOS_MAX_ALERTS) break;
        fnos_alert_t *a = &st->alerts[st->nalerts];
        jstr(it, "lv", a->lv, sizeof(a->lv));
        jstr(it, "m", a->m, sizeof(a->m));
        st->nalerts++;
    }

    cJSON_Delete(root);
    return true;
}

// 历史行格式：{"cols":["ts","cpu","mem","rx_kbs","tx_kbs"],"rows":[[...],...]}
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
    cJSON_ArrayForEach(row, rows) {
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

// 返回 true 表示读到了完整的响应体；*out_status 为 HTTP 状态码，*out_ms 为耗时。
static bool http_get(const char *url, char *buf, size_t cap, int *out_status, int *out_len, int *out_ms)
{
    if (!s_client) {
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = POLL_TIMEOUT_MS,
            .buffer_size = 1536,
            .buffer_size_tx = 512,
            .keep_alive_enable = true,
            .disable_auto_redirect = true,
        };
        s_client = esp_http_client_init(&cfg);
        if (!s_client) {
            return false;
        }
    } else {
        esp_http_client_set_url(s_client, url);
    }
    if (FNOS_TOKEN[0]) {
        esp_http_client_set_header(s_client, "X-Token", FNOS_TOKEN);
    }

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_open(s_client, 0);
    if (err != ESP_OK) {
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
    while (total < (int)cap - 1) {
        int r = esp_http_client_read(s_client, buf + total, (int)cap - 1 - total);
        if (r < 0) {
            err = ESP_FAIL;
            break;
        }
        if (r == 0) {
            break;
        }
        total += r;
    }
    buf[total] = 0;
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

static void commit_status(const fnos_status_t *st, int http_ms, int status_code)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    fnos_status_t *dst = &s_status;
    uint32_t ok = dst->ok_count, fail = dst->fail_count;
    *dst = *st;                             // 整帧替换，避免残留上一帧的数组元素
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
    xSemaphoreGive(s_lock);
}

// ────────────────────────────── 轮询任务

static void poll_task(void *arg)
{
    (void)arg;
    char *buf = heap_caps_malloc(RX_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = malloc(RX_BUF_SIZE);
    }
    if (!buf) {
        ESP_LOGE(TAG, "rx buffer alloc failed");
        s_task = NULL;                      // 不清句柄的话 fnos_data_start() 永远重启不了
        vTaskDelete(NULL);
        return;
    }
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
        if (!fnos_net_online()) {
            mark_link_down();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        fnos_net_poll_rssi();               // 刷新 RSSI 缓存，UI 那边只读内存

        int status_code = 0, len = 0, ms = 0;
        memset(&tmp, 0, sizeof(tmp));
        bool ok = http_get(s_url_status, buf, RX_BUF_SIZE, &status_code, &len, &ms);
        if (ok && status_code == 200 && parse_status(buf, &tmp)) {
            commit_status(&tmp, ms, status_code);
            backoff = 0;
            if (!hist_done) {               // 首次成功：回填开机前的曲线
                char *hbuf = heap_caps_malloc(HIST_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!hbuf) {
                    hist_done = true;       // 分配不到就别每轮重试
                } else {
                    int hs = 0, hl = 0, hm = 0;
                    if (http_get(s_url_hist, hbuf, HIST_BUF_SIZE, &hs, &hl, &hm) && hs == 200) {
                        hist_done = parse_history(hbuf);   // 失败下一轮再试，别静默放弃
                    }
                    free(hbuf);
                }
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
            const char *why = !ok ? (status_code == 0 ? "connect/timeout" : "read error")
                                  : (status_code != 200 ? "http status" : "bad payload");
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

    snprintf(s_url_status, sizeof(s_url_status), "http://%s:%d/api/v1/status", FNOS_HOST, FNOS_PORT);
    snprintf(s_url_hist, sizeof(s_url_hist), "http://%s:%d/api/v1/history", FNOS_HOST, FNOS_PORT);

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
    *out = s_status;
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
