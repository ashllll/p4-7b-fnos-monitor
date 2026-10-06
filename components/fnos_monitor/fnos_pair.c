// 配对与信任锚点：取证书 → 用户核对指纹 → 用配对码换令牌 → 全部写进 NVS。
// 详见 fnos_pair.h 顶部的设计说明。
//
// 线程模型：本模块自己起一个配对任务（栈在 PSRAM），UI 侧只调 fnos_pair_begin /
// fnos_pair_confirm / fnos_pair_view —— 都是投递请求或加锁复制，不会阻塞 LVGL 任务
// （一次 HTTP 最长 6 秒，放 UI 线程里就是整屏卡死）。
#include "fnos_pair.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "cJSON.h"

#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "mbedtls/x509_crt.h"

#if __has_include("fnos_config.h")
#include "fnos_config.h"
#else
#include "fnos_config.example.h"
#endif

#ifndef FNOS_HOST
// 兜底值只在"配置头文件没提供 FNOS_HOST"时用到。曾经这里是这台 NAS 的真实
// 局域网地址，等于把个人环境写进了通用构建；通用构建里必须是中性占位，
// 真实地址由配对流程写进 NVS。
#define FNOS_HOST "nas.local"
#endif
#ifndef FNOS_PORT
#define FNOS_PORT 8799
#endif
#ifndef FNOS_TOKEN
#define FNOS_TOKEN ""
#endif
#ifndef FNOS_PAIR_DEVICE_NAME
#define FNOS_PAIR_DEVICE_NAME "p4-7b-lcd"
#endif

static const char *TAG = "fnos_pair";

#define NVS_NS        "fnos_pair"
#define KEY_HOST      "host"
#define KEY_PORT      "port"
#define KEY_TOKEN     "token"
#define KEY_CERT      "cert"
#define KEY_FP        "fp"
#define IDENT_PATH    "/api/v1/identity"
#define PAIR_PATH     "/api/v1/pair"
#define HTTP_TIMEOUT_MS 6000
#define RX_MAX        (12 * 1024)      // identity 带证书 PEM，比状态帧大

enum { ACT_NONE = 0, ACT_FETCH, ACT_CONFIRM, ACT_FORGET };

// 生效参数：数据层按它连。放 PSRAM（结构里有 2 KB PEM）。
static EXT_RAM_BSS_ATTR fnos_pair_cfg_t s_cfg;
static uint32_t s_generation;          // 每次 s_cfg 变化 +1
static bool     s_provisioned;

// 待确认的证书（取回来到用户点"确认"之间停在这里）
static EXT_RAM_BSS_ATTR char s_pend_pem[FNOS_PAIR_PEM_MAX];
static char s_pend_fp[FNOS_PAIR_FP_MAX];
static char s_pend_cn[80];
static char s_pend_until[16];
static bool s_pend_tls;
// 配对目标是**飞牛应用**，约定端口 8798；开发版采集器是 8799，而它根本没有
// /api/v1/identity —— 拿当前配置的地址直接取证书只会拿到 404，屏幕上就是
// "读证书失败"（用户实测踩到的就是这个）。所以未配对时按候选端口依次试，
// 谁答对就用谁，并把**那个端口**写进配对结果（否则配完还在读 8799）。
#define PAIR_APP_PORT 8798
static int  s_pend_port;        // 真正答对 identity 的端口；0 = 还没定

static EXT_RAM_BSS_ATTR fnos_pair_view_t s_view;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;

// 请求槽：UI 侧写字段再置动作，任务侧在锁里取走并清零
static int  s_act;
static bool s_accept;
static char s_code[16];

// ────────────────────────────── 小工具

// 解码/哈希用的中间缓冲放文件级：EXT_RAM_BSS_ATTR 只对全局变量有保证，
// 而且这几个都是 KB 级，不能占配对任务的栈。
static EXT_RAM_BSS_ATTR char s_b64[FNOS_PAIR_PEM_MAX];
static EXT_RAM_BSS_ATTR unsigned char s_der[1600];

static bool has_cert(const char *pem)
{
    return pem && pem[0] && strstr(pem, "BEGIN CERTIFICATE") != NULL;
}

// 把 PEM 正文（去掉头尾行和所有空白）base64 解码成 DER，再算 SHA-256，
// 输出 "AA:BB:…"。**不解析服务器给的指纹字段**：板子必须自己算，否则中间人
// 同时伪造证书和那个字段就能骗过肉眼比对。
static bool pem_fingerprint(const char *pem, char *out, size_t out_cap)
{
    size_t n = 0;
    for (const char *p = pem; *p; p++) {
        if (*p == '-') {
            /* 只认第一段证书：遇到 "-----END" 就收工。PEM 里若带了证书链（或者
               将来换成更长的证书），把所有段的 base64 拼起来会撑爆 s_b64，
               函数返回 false —— 而服务端那边是"取第一段"，两边行为就对不上，
               用户在屏幕上只会看到"证书解不开"，完全没法判断是证书问题还是
               代码问题。两端都只认第一段，是让它们由构造保持一致。 */
            if (strncmp(p, "-----END", 8) == 0) {
                break;
            }
            while (*p && *p != '\n') {     // "-----BEGIN …-----" 整行跳过
                p++;
            }
            continue;
        }
        if (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') {
            continue;
        }
        if (n + 1 >= sizeof(s_b64)) {
            return false;
        }
        s_b64[n++] = *p;
    }
    s_b64[n] = 0;
    if (n == 0) {
        return false;
    }

    size_t der_len = 0;
    if (mbedtls_base64_decode(s_der, sizeof(s_der), &der_len, (const unsigned char *)s_b64, n) != 0) {
        return false;
    }
    unsigned char hash[32];
    mbedtls_sha256(s_der, der_len, hash, 0);

    size_t off = 0;
    for (int i = 0; i < 32; i++) {
        int w = snprintf(out + off, out_cap - off, i ? ":%02X" : "%02X", hash[i]);
        if (w < 0 || (size_t)w >= out_cap - off) {
            return false;
        }
        off += (size_t)w;
    }
    return true;
}

// 从证书里取 CN 和到期日，纯粹是给用户认人用的辅助信息（认人的是指纹）。
static void cert_info(const char *pem, char *cn, size_t cn_cap, char *until, size_t until_cap)
{
    cn[0] = 0;
    until[0] = 0;
    mbedtls_x509_crt *crt = heap_caps_calloc(1, sizeof(*crt), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!crt) {
        crt = calloc(1, sizeof(*crt));
    }
    if (!crt) {
        return;
    }
    mbedtls_x509_crt_init(crt);
    // buflen 要把结尾的 '\0' 算进去，这是 mbedtls 的约定
    if (mbedtls_x509_crt_parse(crt, (const unsigned char *)pem, strlen(pem) + 1) == 0) {
        char dn[128] = {0};
        if (mbedtls_x509_dn_gets(dn, sizeof(dn), &crt->subject) > 0) {
            snprintf(cn, cn_cap, "%s", dn);
        }
        snprintf(until, until_cap, "%04d-%02d-%02d", crt->valid_to.year, crt->valid_to.mon,
                 crt->valid_to.day);
    }
    mbedtls_x509_crt_free(crt);
    free(crt);
}

// 取证书/等确认/换令牌这几个阶段里，界面该显示"实际要连的那个端口"
static bool pair_state_busy(fnos_pair_state_t st)
{
    return st == FNOS_PAIR_FETCHING || st == FNOS_PAIR_CONFIRM || st == FNOS_PAIR_PAIRING;
}

static void set_view(fnos_pair_state_t state, const char *msg)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_view.state = state;
    snprintf(s_view.msg, sizeof(s_view.msg), "%s", msg ? msg : "");
    snprintf(s_view.fingerprint, sizeof(s_view.fingerprint), "%s", s_pend_fp);
    snprintf(s_view.subject, sizeof(s_view.subject), "%s", s_pend_cn);
    snprintf(s_view.not_after, sizeof(s_view.not_after), "%s", s_pend_until);
    snprintf(s_view.host, sizeof(s_view.host), "%s", s_cfg.host);
    s_view.port = (s_pend_port && pair_state_busy(state)) ? s_pend_port : s_cfg.port;
    s_view.tls = s_cfg.tls;
    xSemaphoreGive(s_lock);
}

// 文件级静态：esp_http_client 不复制 cert_pem，句柄活着期间指针必须有效
static EXT_RAM_BSS_ATTR char s_http_cert[FNOS_PAIR_PEM_MAX];

/* 配对成功、写 NVS 之前的暂存。fnos_pair_cfg_t 里有 2 KB 的 cert_pem，
   放栈上会让 do_pair 的单帧到 2 KB 量级（配对任务栈 7 KB）。
   do_pair 只由 pair_task 调用、不可能重入，所以文件级静态是安全的。 */
static EXT_RAM_BSS_ATTR fnos_pair_cfg_t s_next;

// 最近一次请求卡在哪一步（open / fetch_headers / read）。用户屏幕上原来只有一句
// "证书或网络问题"，既分不清是哪一步，也没有可上报的错误码 —— 现场排查全靠猜。
static char s_http_diag[96];

const char *fnos_pair_last_http_diag(void) { return s_http_diag; }

// 探针要先于定义处使用 http_oneshot（它在本文件后面）
static bool http_oneshot(bool post, const char *url, const char *body, const char *cert_pem,
                         char *rx, size_t rx_cap, int *out_status, int *out_len);

/* ── 串口 / 屏幕都能用的 HTTPS 自检 ─────────────────────────────────────
   配对卡在"握手没完成"时，屏幕只有一句话，而真正的原因（mbedTLS/esp-tls）只在
   日志里。这个探针**不碰配对码、不改任何状态**：明文取一次 identity 拿到要固定的
   那张证书 → 用这张证书 HTTPS 探一次 /api/v1/health → 把每一层的结果打出来。
   换 NAS、换证书、TLS 握手出问题时用它自检，比在配对流里猜快得多。 */
#define PROBE_HEALTH_PATH "/api/v1/health"
static EXT_RAM_BSS_ATTR char s_probe_pem[FNOS_PAIR_PEM_MAX];

bool fnos_pair_tls_probe(char *out, size_t out_cap)
{
    if (out_cap) out[0] = 0;
    char *rx = heap_caps_malloc(RX_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rx) rx = malloc(RX_MAX);
    if (!rx) { snprintf(out, out_cap, "内存不足（%d 字节）", (int)RX_MAX); return false; }

    const char *host = s_cfg.host[0] ? s_cfg.host : "?";
    int ports[2] = { PAIR_APP_PORT, s_cfg.port };
    int nports = (s_cfg.port == PAIR_APP_PORT) ? 1 : 2;
    char url[160];
    int status = 0, len = 0, port = 0;
    bool got = false;

    for (int i = 0; i < nports; i++) {
        snprintf(url, sizeof url, "http://%s:%d%s", host, ports[i], IDENT_PATH);
        status = 0; len = 0;
        if (http_oneshot(false, url, NULL, NULL, rx, RX_MAX, &status, &len) && status == 200) {
            port = ports[i];
            got = true;
            break;
        }
        ESP_LOGW(TAG, "探针：明文 identity 没通 %s ok_status=%d diag=%s", url, status, s_http_diag);
    }
    if (!got) {
        snprintf(out, out_cap, "明文 identity 没通（%s）：%s", url, s_http_diag);
        free(rx);
        return false;
    }

    cJSON *root = cJSON_Parse(rx);
    const cJSON *jpem = root ? cJSON_GetObjectItemCaseSensitive(root, "cert_pem") : NULL;
    const cJSON *jtls = root ? cJSON_GetObjectItemCaseSensitive(root, "tls") : NULL;
    if (!cJSON_IsString(jpem) || !jpem->valuestring || !jpem->valuestring[0]) {
        snprintf(out, out_cap, "identity 里没有 cert_pem（应用没开 TLS？tls=%d）",
                 (int)cJSON_IsTrue(jtls));
        if (root) cJSON_Delete(root);
        free(rx);
        return false;
    }
    /* 先拷出来再删 cJSON：jpem 的指针随 root 一起失效 */
    snprintf(s_probe_pem, sizeof s_probe_pem, "%s", jpem->valuestring);
    if (root) cJSON_Delete(root);
    ESP_LOGI(TAG, "探针：identity 来自 %s:%d，证书 %d 字节", host, port, (int)strlen(s_probe_pem));

    snprintf(url, sizeof url, "https://%s:%d%s", host, port, PROBE_HEALTH_PATH);
    status = 0; len = 0;
    bool replied = http_oneshot(false, url, NULL, s_probe_pem, rx, RX_MAX, &status, &len);
    bool ok = replied && status == 200;
    snprintf(out, out_cap, "%s %s（ok=%d status=%d len=%d%s%s）",
             ok ? "HTTPS 通过" : "HTTPS 失败", url, (int)replied, status, len,
             s_http_diag[0] ? " · " : "", s_http_diag);
    if (!ok) { free(rx); return false; }

    /* 再走一遍**真正会失败的那条路**：POST 一个假配对码。
       期望服务端回 403 + {"error":"配对码不正确"} —— 这也算通过：说明请求体发出去了、
       响应也收回来了（这一步以前必挂：body 根本没写出去，6 秒后超时）。 */
    snprintf(url, sizeof url, "https://%s:%d%s", host, port, PAIR_PATH);
    status = 0; len = 0;
    bool posted = http_oneshot(true, url, "{\"code\":\"000000\",\"name\":\"probe\"}",
                               s_probe_pem, rx, RX_MAX, &status, &len);
    char body[96] = "";
    if (posted && len > 0) {
        cJSON *r = cJSON_Parse(rx);
        const cJSON *je = r ? cJSON_GetObjectItemCaseSensitive(r, "error") : NULL;
        if (cJSON_IsString(je) && je->valuestring) snprintf(body, sizeof body, "：%s", je->valuestring);
        if (r) cJSON_Delete(r);
    }
    char tail[160];
    snprintf(tail, sizeof tail, " ｜ POST %s ok=%d status=%d%s%s", PAIR_PATH, (int)posted, status,
             body, s_http_diag[0] ? " · " : "");
    strncat(out, tail, out_cap - strlen(out) - 1);
    free(rx);
    return posted && status > 0;
}

/* 探针要跑 mbedTLS 握手，栈不能小；从串口命令里直接跑会撑爆命令任务的栈，
   所以丢到一个 PSRAM 栈的一次性任务里跑。 */
static void probe_task(void *arg)
{
    char msg[240];
    bool ok = fnos_pair_tls_probe(msg, sizeof msg);
    printf("\n[tls] %s%s\n> ", msg, ok ? "" : "  ← 见上面 mbedTLS/esp-tls 的详细日志");
    fflush(stdout);
    vTaskDelete(NULL);
}

void fnos_pair_tls_probe_async(void)
{
    if (xTaskCreatePinnedToCoreWithCaps(probe_task, "tls_probe", 8 * 1024, NULL, 4,
                                        NULL, tskNO_AFFINITY, MALLOC_CAP_SPIRAM) != pdPASS) {
        printf("\n[tls] 起不了探针任务（内存不足）\n> ");
    }
}


// 一次性请求（配对是低频操作，不复用连接）。cert_pem 非空才走证书校验。
static bool http_oneshot(bool post, const char *url, const char *body, const char *cert_pem,
                         char *rx, size_t rx_cap, int *out_status, int *out_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
        .disable_auto_redirect = true,
        // 信任锚点是"这一张证书"，不是证书里的名字：NAS 换了 IP（DHCP）时
        // 名字校验会让一条本来完全可信的连接失败，而固定的证书本身没变。
        // 攻击者要冒充得先拿到那张自签证书的私钥，名字校验在这里加不了安全性。
        .skip_cert_common_name_check = true,
    };
    if (cert_pem && cert_pem[0]) {
        cfg.cert_pem = cert_pem;
    }

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        *out_status = 0;
        return false;
    }
    if (post && body) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, body, (int)strlen(body));
    }

    bool ok = false;
    s_http_diag[0] = 0;
    esp_err_t oerr = esp_http_client_open(c, post && body ? (int)strlen(body) : 0);
    if (oerr != ESP_OK) {
        snprintf(s_http_diag, sizeof s_http_diag, "open 失败 %s（errno %d）",
                 esp_err_to_name(oerr), esp_http_client_get_errno(c));
        ESP_LOGE(TAG, "http open failed: %s errno=%d internal_heap=%u", esp_err_to_name(oerr),
                 esp_http_client_get_errno(c),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        esp_http_client_cleanup(c);
        *out_status = 0;
        *out_len = 0;
        return false;
    }
    /* **必须自己把请求体写出去**：esp_http_client_open(len) 只负责连接 + 记住
       Content-Length，headers/body 是在第一次 write/fetch_headers 时才发的。
       少了这一步，服务端会一直等 body、板子一直等响应，直到 6 秒超时 ——
       应用侧日志里那句 SSLEOFError 就是这么来的（2026-10-06 实测：GET 通、POST 必挂）。 */
    if (post && body) {
        int blen = (int)strlen(body);
        int wrote = esp_http_client_write(c, body, blen);
        if (wrote != blen) {
            snprintf(s_http_diag, sizeof s_http_diag, "写请求体失败 %d/%d（errno %d）",
                     wrote, blen, esp_http_client_get_errno(c));
            ESP_LOGE(TAG, "http write body failed: %d/%d errno=%d", wrote, blen,
                     esp_http_client_get_errno(c));
            esp_http_client_cleanup(c);
            *out_status = 0;
            *out_len = 0;
            return false;
        }
    }
    if (esp_http_client_fetch_headers(c) < 0) {
        snprintf(s_http_diag, sizeof s_http_diag, "读响应头失败（errno %d）",
                 esp_http_client_get_errno(c));
        ESP_LOGE(TAG, "http fetch_headers failed: errno=%d internal_heap=%u",
                 esp_http_client_get_errno(c),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        esp_http_client_cleanup(c);
        *out_status = 0;
        *out_len = 0;
        return false;
    }
    {
        *out_status = esp_http_client_get_status_code(c);
        int total = 0;
        while (total < (int)rx_cap - 1) {
            int r = esp_http_client_read(c, rx + total, (int)rx_cap - 1 - total);
            if (r <= 0) {
                break;
            }
            total += r;
        }
        rx[total] = 0;
        *out_len = total;
        ok = esp_http_client_is_complete_data_received(c);
        if (!ok) {
            snprintf(s_http_diag, sizeof s_http_diag, "响应没收完（%d 字节，errno %d）",
                     total, esp_http_client_get_errno(c));
            ESP_LOGW(TAG, "response incomplete: %d bytes errno=%d", total,
                     esp_http_client_get_errno(c));
        }
    }
    esp_http_client_cleanup(c);
    return ok;
}

// ────────────────────────────── NVS

static void nvs_load(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    snprintf(s_cfg.host, sizeof(s_cfg.host), "%s", FNOS_HOST);
    s_cfg.port = FNOS_PORT;
    s_cfg.tls = false;
    snprintf(s_cfg.token, sizeof(s_cfg.token), "%s", FNOS_TOKEN);
    s_provisioned = false;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    char host[sizeof(s_cfg.host)] = {0};
    size_t hlen = sizeof(host);
    char token[sizeof(s_cfg.token)] = {0};
    size_t tlen = sizeof(token);
    int32_t port = 0;
    char cert[FNOS_PAIR_PEM_MAX] = {0};
    size_t clen = sizeof(cert);
    char fp[FNOS_PAIR_FP_MAX] = {0};
    size_t flen = sizeof(fp);

    bool ok = (nvs_get_str(h, KEY_HOST, host, &hlen) == ESP_OK) &&
              (nvs_get_i32(h, KEY_PORT, &port) == ESP_OK) &&
              (nvs_get_str(h, KEY_TOKEN, token, &tlen) == ESP_OK);
    if (ok) {
        snprintf(s_cfg.host, sizeof(s_cfg.host), "%s", host);
        s_cfg.port = (int)port;
        snprintf(s_cfg.token, sizeof(s_cfg.token), "%s", token);
        if (nvs_get_str(h, KEY_CERT, cert, &clen) == ESP_OK && has_cert(cert)) {
            snprintf(s_cfg.cert_pem, sizeof(s_cfg.cert_pem), "%s", cert);
            s_cfg.tls = true;
            if (nvs_get_str(h, KEY_FP, fp, &flen) == ESP_OK) {
                snprintf(s_cfg.fingerprint, sizeof(s_cfg.fingerprint), "%s", fp);
            }
        }
        // 有证书才算"配对过"。NAS 关掉 TLS 时没有证书，但令牌仍然有效——
        // 这种回落只在确实没有证书时才成立，且下面数据层不会静默降级。
        s_provisioned = true;
    }
    nvs_close(h);
    s_generation++;
    ESP_LOGI(TAG, "loaded: %s:%d tls=%d token=%s cert=%s", s_cfg.host, s_cfg.port,
             (int)s_cfg.tls, s_cfg.token[0] ? "yes" : "no", has_cert(s_cfg.cert_pem) ? "yes" : "no");
}

static bool nvs_save(const fnos_pair_cfg_t *c)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_str(h, KEY_HOST, c->host) == ESP_OK &&
              nvs_set_i32(h, KEY_PORT, c->port) == ESP_OK &&
              nvs_set_str(h, KEY_TOKEN, c->token) == ESP_OK;
    if (ok && has_cert(c->cert_pem)) {
        ok = nvs_set_str(h, KEY_CERT, c->cert_pem) == ESP_OK &&
             nvs_set_str(h, KEY_FP, c->fingerprint) == ESP_OK;
    }
    if (ok) {
        ok = nvs_commit(h) == ESP_OK;
    }
    nvs_close(h);
    return ok;
}

static void nvs_wipe(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
}

// ────────────────────────────── 配对流程（跑在配对任务里）

static void do_pair(void);

static void do_fetch(const char *code)
{
    char *rx = heap_caps_malloc(RX_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rx) {
        rx = malloc(RX_MAX);
    }
    if (!rx) {
        set_view(FNOS_PAIR_FAILED, "内存不足，取不到证书");
        return;
    }

    /* 候选端口：先试应用约定端口 8798，再试当前配置里的端口（应用可能装在非默认
       端口，板子也可能本来就指着开发版采集器）。去重，最多两个。 */
    int ports[2] = { PAIR_APP_PORT, s_cfg.port };
    int nports = (s_cfg.port == PAIR_APP_PORT) ? 1 : 2;
    char tried[80] = "";
    int status = 0, len = 0;
    bool got = false;
    for (int i = 0; i < nports; i++) {
        char url[128];
        snprintf(url, sizeof(url), "http://%s:%d%s", s_cfg.host, ports[i], IDENT_PATH);
        status = 0; len = 0;
        bool ok = http_oneshot(false, url, NULL, NULL, rx, RX_MAX, &status, &len);
        if (ok && status == 200) {
            s_pend_port = ports[i];
            ESP_LOGI(TAG, "identity 来自 %s:%d", s_cfg.host, ports[i]);
            got = true;
            break;
        }
        ESP_LOGW(TAG, "identity failed: %s ok=%d status=%d", url, (int)ok, status);
        char one[28];
        snprintf(one, sizeof one, "%s%d:%s", tried[0] ? " " : "", ports[i],
                 ok ? "非 200" : "连不上");
        strncat(tried, one, sizeof tried - strlen(tried) - 1);
    }
    if (!got) {
        char msg[112];
        /* 把"试过哪些端口、结果如何"写上屏 —— 只说"读证书失败"等于让用户猜 */
        snprintf(msg, sizeof msg, "读证书失败（%s）：%s %s", s_cfg.host,
                 tried[0] ? tried : "没有可用地址", s_http_diag);
        ESP_LOGE(TAG, "pair 失败：%s", msg);
        set_view(FNOS_PAIR_FAILED, msg);
        free(rx);
        return;
    }

    cJSON *root = cJSON_Parse(rx);
    free(rx);
    if (!root) {
        set_view(FNOS_PAIR_FAILED, "NAS 返回的内容无法解析");
        return;
    }

    const cJSON *jtls = cJSON_GetObjectItemCaseSensitive(root, "tls");
    const cJSON *jpem = cJSON_GetObjectItemCaseSensitive(root, "cert_pem");
    bool tls = cJSON_IsTrue(jtls);
    const char *pem = (cJSON_IsString(jpem) && jpem->valuestring) ? jpem->valuestring : "";

    if (!tls) {
        // NAS 侧把 TLS 关了（明文排障模式）：没有证书可固定，直接进配对。
        s_pend_tls = false;
        s_pend_pem[0] = 0;
        s_pend_fp[0] = 0;
        s_pend_cn[0] = 0;
        s_pend_until[0] = 0;
        cJSON_Delete(root);
        ESP_LOGW(TAG, "NAS 未启用 TLS，将以明文配对");
        snprintf(s_code, sizeof(s_code), "%s", code);
        set_view(FNOS_PAIR_PAIRING, "NAS 未启用 HTTPS，正在配对…");
        do_pair();
        return;
    }

    if (!has_cert(pem)) {
        cJSON_Delete(root);
        set_view(FNOS_PAIR_FAILED, "NAS 说启用了 HTTPS，却没给证书");
        return;
    }
    if (strlen(pem) >= sizeof(s_pend_pem)) {
        cJSON_Delete(root);
        set_view(FNOS_PAIR_FAILED, "证书太长，装不下");
        return;
    }
    snprintf(s_pend_pem, sizeof(s_pend_pem), "%s", pem);
    cJSON_Delete(root);

    if (!pem_fingerprint(s_pend_pem, s_pend_fp, sizeof(s_pend_fp))) {
        set_view(FNOS_PAIR_FAILED, "证书解不开，没法核对指纹");
        return;
    }
    cert_info(s_pend_pem, s_pend_cn, sizeof(s_pend_cn), s_pend_until, sizeof(s_pend_until));
    s_pend_tls = true;

    ESP_LOGI(TAG, "cert fp=%s cn=%s until=%s", s_pend_fp, s_pend_cn, s_pend_until);
    snprintf(s_code, sizeof(s_code), "%s", code);
    set_view(FNOS_PAIR_CONFIRM, "请把屏幕上的指纹与 NAS 管理页逐段比对");
}

static void do_pair(void)
{
    char url[128];
    snprintf(url, sizeof(url), "%s://%s:%d%s", s_pend_tls ? "https" : "http", s_cfg.host,
             s_pend_port ? s_pend_port : s_cfg.port, PAIR_PATH);

    char body[128];
    snprintf(body, sizeof(body), "{\"code\":\"%s\",\"name\":\"%s\"}", s_code,
             FNOS_PAIR_DEVICE_NAME);

    char *rx = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rx) {
        rx = malloc(2048);
    }
    if (!rx) {
        set_view(FNOS_PAIR_FAILED, "内存不足，配对没发出去");
        return;
    }

    if (s_pend_tls) {
        snprintf(s_http_cert, sizeof(s_http_cert), "%s", s_pend_pem);
    } else {
        s_http_cert[0] = 0;
    }

    int status = 0, len = 0;
    bool ok = http_oneshot(true, url, body, s_http_cert[0] ? s_http_cert : NULL, rx, 2048,
                           &status, &len);

    /* 服务端在非 200 时也会给 {"ok":false,"error":"…"}（比如"配对码已过期，请重新生成"）。
       只在 ok 时才解析，屏幕上就永远只有一句"HTTP 403"——把可执行的话丢掉了。
       所以：只要收到了字节就解析。 */
    cJSON *root = (ok || len > 0) ? cJSON_Parse(rx) : NULL;
    const cJSON *jtok = root ? cJSON_GetObjectItemCaseSensitive(root, "token") : NULL;
    const char *token = (jtok && cJSON_IsString(jtok) && jtok->valuestring) ? jtok->valuestring : NULL;

    if (!token || !token[0]) {
        const cJSON *jerr = root ? cJSON_GetObjectItemCaseSensitive(root, "error") : NULL;
        const char *err = (jerr && cJSON_IsString(jerr)) ? jerr->valuestring : NULL;
        char msg[112];
        if (err) {
            snprintf(msg, sizeof(msg), "配对失败：%s", err);
        } else if (status == 0) {
            snprintf(msg, sizeof(msg), "配对请求没连上：%s",
                     s_http_diag[0] ? s_http_diag : "原因未知");
        } else {
            snprintf(msg, sizeof(msg), "配对失败：HTTP %d", status);
        }
        ESP_LOGW(TAG, "pair failed: status=%d err=%s", status, err ? err : "-");
        if (root) {
            cJSON_Delete(root);
        }
        free(rx);
        set_view(FNOS_PAIR_FAILED, msg);
        return;
    }

    fnos_pair_cfg_t *next = &s_next;      // 见 s_next 的注释：不压在配对任务的栈上
    memset(next, 0, sizeof(*next));
    snprintf(next->host, sizeof(next->host), "%s", s_cfg.host);
    next->port = s_pend_port ? s_pend_port : s_cfg.port;
    next->tls = s_pend_tls;
    snprintf(next->token, sizeof(next->token), "%s", token);
    if (s_pend_tls) {
        snprintf(next->cert_pem, sizeof(next->cert_pem), "%s", s_pend_pem);
        snprintf(next->fingerprint, sizeof(next->fingerprint), "%s", s_pend_fp);
    }
    if (root) {
        cJSON_Delete(root);
    }
    free(rx);

    if (!nvs_save(next)) {
        set_view(FNOS_PAIR_FAILED, "配对成功但写入 NVS 失败");
        return;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_cfg = *next;
        s_provisioned = true;
        s_generation++;
        xSemaphoreGive(s_lock);
    }
    ESP_LOGI(TAG, "paired: %s:%d tls=%d fp=%s", s_cfg.host, s_cfg.port, (int)s_cfg.tls,
             s_cfg.fingerprint);
    set_view(FNOS_PAIR_PROVISIONED, "配对完成");
}

static void do_forget(void)
{
    nvs_wipe();
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        memset(&s_cfg, 0, sizeof(s_cfg));
        snprintf(s_cfg.host, sizeof(s_cfg.host), "%s", FNOS_HOST);
        s_cfg.port = FNOS_PORT;
        s_cfg.tls = false;
        snprintf(s_cfg.token, sizeof(s_cfg.token), "%s", FNOS_TOKEN);
        s_provisioned = false;
        s_generation++;
        s_pend_pem[0] = 0;
        s_pend_fp[0] = 0;
        s_pend_cn[0] = 0;
        s_pend_until[0] = 0;
        xSemaphoreGive(s_lock);
    }
    ESP_LOGW(TAG, "配对已清除，退回编译期默认地址");
    set_view(FNOS_PAIR_UNPROVISIONED, "已清除本机配对（NAS 上的设备记录还没撤销）");
}

static void pair_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        int act;
        bool accept;
        char code[sizeof(s_code)];
        if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        act = s_act;
        accept = s_accept;
        memcpy(code, s_code, sizeof(code));
        s_act = ACT_NONE;
        xSemaphoreGive(s_lock);

        if (act == ACT_FETCH) {
            do_fetch(code);
        } else if (act == ACT_CONFIRM) {
            if (!accept) {
                s_pend_pem[0] = 0;
                s_pend_fp[0] = 0;
                set_view(FNOS_PAIR_UNPROVISIONED, "已取消：指纹没核对过，什么都没保存");
            } else if (has_cert(s_pend_pem) || !s_pend_tls) {
                set_view(FNOS_PAIR_PAIRING, "正在配对…");
                do_pair();
            } else {
                set_view(FNOS_PAIR_FAILED, "没有待确认的证书，请重新发起配对");
            }
        } else if (act == ACT_FORGET) {
            do_forget();
        }
    }
}

// ────────────────────────────── 对外接口

void fnos_pair_init(void)
{
    if (s_task) {
        return;
    }
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            ESP_LOGE(TAG, "mutex alloc failed");
            return;
        }
    }
    nvs_load();
    memset(&s_view, 0, sizeof(s_view));
    s_view.state = s_provisioned ? FNOS_PAIR_PROVISIONED : FNOS_PAIR_UNPROVISIONED;
    snprintf(s_view.msg, sizeof(s_view.msg), "%s",
             s_provisioned ? "已配对" : "未配对：用编译期默认地址");
    snprintf(s_view.host, sizeof(s_view.host), "%s", s_cfg.host);
    s_view.port = s_cfg.port;
    s_view.tls = s_cfg.tls;

    BaseType_t rc = xTaskCreatePinnedToCoreWithCaps(pair_task, "fnos_pair", 7 * 1024, NULL, 4,
                                                    &s_task, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
    if (rc != pdPASS) {
        rc = xTaskCreatePinnedToCore(pair_task, "fnos_pair", 7 * 1024, NULL, 4, &s_task,
                                     tskNO_AFFINITY);
    }
    if (rc != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "pair task create failed");
    }
}

bool fnos_pair_provisioned(void)
{
    return s_provisioned;
}

uint32_t fnos_pair_generation(void)
{
    return s_generation;
}

void fnos_pair_active(fnos_pair_cfg_t *out)
{
    if (!out) {
        return;
    }
    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        *out = s_cfg;
        xSemaphoreGive(s_lock);
    } else {
        *out = s_cfg;
    }
}

void fnos_pair_view(fnos_pair_view_t *out)
{
    if (!out) {
        return;
    }
    if (s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        *out = s_view;
        xSemaphoreGive(s_lock);
    } else {
        *out = s_view;
    }
}

static void post_action(int act)
{
    if (!s_task || !s_lock) {
        return;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_act = act;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
}

void fnos_pair_begin(const char *code)
{
    if (!s_task || !s_lock) {
        return;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    snprintf(s_code, sizeof(s_code), "%s", code ? code : "");
    s_act = ACT_FETCH;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
}

void fnos_pair_confirm(bool accept)
{
    if (!s_task || !s_lock) {
        return;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_accept = accept;
    s_act = ACT_CONFIRM;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
}

void fnos_pair_forget(void)
{
    post_action(ACT_FORGET);
}
