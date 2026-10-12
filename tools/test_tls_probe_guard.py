#!/usr/bin/env python3
"""No-network ASan/UBSan regression for the production TLS probe ownership window."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'components/fnos_monitor/fnos_pair.c').read_text()

def function(name):
    match = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\s*\{', SOURCE, re.M)
    if not match:
        raise RuntimeError('missing production function: ' + name)
    start = SOURCE.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (SOURCE[end] == '{') - (SOURCE[end] == '}')
        end += 1
    return SOURCE[match.start():end]

PREAMBLE = r'''
#include "fnos_pair.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define ESP_LOG_DEBUG 5
#define pdPASS 1
#define tskNO_AFFINITY 0
#define MALLOC_CAP_SPIRAM 0
typedef int esp_log_level_t;
static fnos_pair_cfg_t s_probe_cfg;
static char s_probe_pem[FNOS_PAIR_PEM_MAX];
static atomic_flag s_probe_guard = ATOMIC_FLAG_INIT;
static atomic_flag s_probe_async_guard = ATOMIC_FLAG_INIT;
static int levels[3] = {1, 2, 3};
static int tag_index(const char *tag) {
    if (!strcmp(tag, "esp-tls")) return 0;
    if (!strcmp(tag, "mbedtls")) return 1;
    assert(!strcmp(tag, "HTTP_CLIENT")); return 2;
}
static int esp_log_level_get(const char *tag) { return levels[tag_index(tag)]; }
static void esp_log_level_set(const char *tag, int level) { levels[tag_index(tag)] = level; }
static bool snapshot_available = true, body_ok = true;
static atomic_int entered, leave_body, body_calls;
static atomic_bool body_active;
uint32_t fnos_pair_active_gen(fnos_pair_cfg_t *out) {
    memset(out, 0, sizeof *out);
    if (!snapshot_available) return 0;
    strcpy(out->host, "fixture.invalid"); out->port = 24680;
    return 7;
}
static bool tls_probe_body(char *out, size_t cap) {
    assert(!atomic_exchange(&body_active, true));
    atomic_fetch_add(&body_calls, 1);
    assert(!strcmp(s_probe_cfg.host, "fixture.invalid") && s_probe_cfg.port == 24680);
    for (int i = 0; i < 3; i++) assert(levels[i] == ESP_LOG_DEBUG);
    strcpy(s_probe_pem, "anonymous scratch");
    atomic_store(&entered, 1);
    while (!atomic_load(&leave_body)) { }
    snprintf(out, cap, "mock probe result");
    atomic_store(&body_active, false); return body_ok;
}
static void (*queued)(void *);
static int task_count;
static bool create_ok = true;
static int create_task(void (*fn)(void *), const char *name, int size, void *arg,
                       int priority, void *handle, int core, int caps) {
    (void)name; (void)size; (void)arg; (void)priority; (void)handle; (void)core; (void)caps;
    task_count++;
    if (!create_ok) return 0;
    queued = fn; return pdPASS;
}
#define xTaskCreatePinnedToCoreWithCaps create_task
static void vTaskDelete(void *task) { (void)task; }
'''
FUNCTIONS = '\n'.join(function(name) for name in (
    'fnos_pair_tls_probe', 'probe_task', 'fnos_pair_tls_probe_async'))
MAIN = r'''
static void restored(void) { assert(levels[0] == 1 && levels[1] == 2 && levels[2] == 3); }
static void *run(void *arg) {
    char msg[80]; (void)arg;
    assert(fnos_pair_tls_probe(msg, sizeof msg)); return NULL;
}
int main(void) {
    char out[80];
    assert(!fnos_pair_tls_probe(NULL, 80));
    assert(!fnos_pair_tls_probe(out, 0)); restored();
    snapshot_available = false;
    assert(!fnos_pair_tls_probe(out, sizeof out));
    assert(strstr(out, "unavailable") && !body_calls); restored();
    snapshot_available = true;
    pthread_t worker; assert(!pthread_create(&worker, NULL, run, NULL));
    while (!atomic_load(&entered)) { }
    assert(!fnos_pair_tls_probe(out, sizeof out));
    assert(strstr(out, "busy") && body_calls == 1);
    atomic_store(&leave_body, 1); assert(!pthread_join(worker, NULL)); restored();
    assert(s_probe_pem[0] == 0 && s_probe_cfg.host[0] == 0);
    body_ok = false;
    assert(!fnos_pair_tls_probe(out, sizeof out)); restored();
    body_ok = true;
    assert(fnos_pair_tls_probe(out, 1)); assert(out[0] == 0); restored();
    create_ok = false; fnos_pair_tls_probe_async(); assert(task_count == 1);
    create_ok = true; fnos_pair_tls_probe_async(); assert(task_count == 2 && queued);
    fnos_pair_tls_probe_async(); assert(task_count == 2);
    queued(NULL); restored();
    fnos_pair_tls_probe_async(); assert(task_count == 3);
    queued(NULL); restored();
    puts("TLS probe ownership: invalid/snapshot/busy/failure/recovery/async PASS");
    return 0;
}
'''

# Verify request diagnostics belong to the caller, including initialization failure.
http = function('http_oneshot')
assert 's_http_diag' not in http
assert 'char *diag, size_t diag_cap' in http
assert http.index('*out_len = 0') < http.index('esp_http_client_init')
assert 'HTTP client allocation failed' in http
probe = function('tls_probe_body')
assert 's_cfg.' not in probe and 's_http_diag' not in probe
assert 'http_oneshot(true' not in probe and 'PAIR_PATH' not in probe
assert probe.count('diag, sizeof diag)') == 2
assert 'strlen(jpem->valuestring) >= sizeof s_probe_pem' in probe

with tempfile.TemporaryDirectory(prefix='fnos-probe-guard-') as tmp:
    path = Path(tmp) / 'test.c'
    binary = Path(tmp) / 'test'
    for negative in (False, True):
        funcs = FUNCTIONS
        if negative:
            funcs = funcs.replace('atomic_flag_test_and_set(&s_probe_guard)', 'false')
        path.write_text(PREAMBLE + funcs + MAIN)
        subprocess.run(['cc', '-std=c11', '-pthread', '-UNDEBUG', '-fsanitize=address,undefined',
                        '-g', '-I', str(ROOT/'components/fnos_monitor'), str(path), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        if negative:
            assert result.returncode != 0, 'missing reentry guard was accepted'
        else:
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            print(result.stdout.strip())
print('TLS probe guard negative control rejected; per-request diagnostic wiring PASS')

HTTP_MOCKS = r'''
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HTTP_TIMEOUT_MS 1
#define HTTP_METHOD_POST 1
#define HTTP_METHOD_GET 0
#define ESP_OK 0
#define MALLOC_CAP_INTERNAL 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int esp_err_t;
typedef struct { const char *url, *cert_pem; int method, timeout_ms, buffer_size, buffer_size_tx;
    bool keep_alive_enable, disable_auto_redirect, skip_cert_common_name_check; } esp_http_client_config_t;
typedef struct { int error; } *esp_http_client_handle_t;
static atomic_int arrivals;
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    if (!strcmp(cfg->url, "allocation")) return NULL;
    esp_http_client_handle_t c = malloc(sizeof *c); assert(c);
    c->error = !strcmp(cfg->url, "probe") ? 101 : 202; return c;
}
static int esp_http_client_open(esp_http_client_handle_t c, int size) {
    (void)c; (void)size; atomic_fetch_add(&arrivals, 1);
    while (atomic_load(&arrivals) < 2) { }
    return -1;
}
static const char *esp_err_to_name(int err) { (void)err; return "mock-open-error"; }
static int esp_http_client_get_errno(esp_http_client_handle_t c) { return c->error; }
static int heap_caps_get_free_size(int caps) { (void)caps; return 1234; }
static void esp_http_client_cleanup(esp_http_client_handle_t c) { free(c); }
static void esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *val) { (void)c; (void)key; (void)val; }
static void esp_http_client_set_post_field(esp_http_client_handle_t c, const char *body, int len) { (void)c; (void)body; (void)len; }
static int esp_http_client_write(esp_http_client_handle_t c, const char *body, int len) { (void)c; (void)body; return len; }
static int esp_http_client_fetch_headers(esp_http_client_handle_t c) { (void)c; return 0; }
static int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return 200; }
static int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len) { (void)c; (void)buf; (void)len; return 0; }
static bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { (void)c; return true; }
'''
HTTP_MAIN = r'''
struct request { const char *url; char diag[96]; };
static void *request(void *arg) {
    struct request *r = arg; char rx[16] = "stale"; int status=99, len=99;
    assert(!http_oneshot(false, r->url, NULL, NULL, rx, sizeof rx, &status, &len, r->diag, sizeof r->diag));
    assert(status == 0 && len == 0 && rx[0] == 0); return NULL;
}
int main(void) {
    struct request a = {"probe", "stale"}, b = {"pair", "stale"}, c = {"allocation", "stale"};
    pthread_t first, second;
    assert(!pthread_create(&first, NULL, request, &a));
    assert(!pthread_create(&second, NULL, request, &b));
    assert(!pthread_join(first, NULL)); assert(!pthread_join(second, NULL));
    assert(strstr(a.diag, "101") && !strstr(a.diag, "202"));
    assert(strstr(b.diag, "202") && !strstr(b.diag, "101"));
    request(&c); assert(strstr(c.diag, "allocation failed"));
    puts("HTTP concurrent diagnostics and allocation-failure outputs PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='fnos-probe-http-') as tmp:
    path = Path(tmp) / 'http.c'
    binary = Path(tmp) / 'http'
    path.write_text(HTTP_MOCKS + http + HTTP_MAIN)
    subprocess.run(['cc', '-std=c11', '-pthread', '-UNDEBUG', '-fsanitize=address,undefined',
                    '-g', str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=20)

BODY_MOCKS = r'''
#include "fnos_pair.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RX_MAX 12288
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define PAIR_APP_PORT 8798
#define IDENT_PATH "/api/v1/identity"
#define PROBE_HEALTH_PATH "/api/v1/health"
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static fnos_pair_cfg_t s_probe_cfg;
static char s_probe_pem[FNOS_PAIR_PEM_MAX];
static bool oom, missing_cert, identity_fail, health_fail;
static int requests;
static char cert[FNOS_PAIR_PEM_MAX + 1];
static void *probe_malloc(size_t cap) { return oom ? NULL : malloc(cap); }
#define malloc probe_malloc
static void *heap_caps_malloc(size_t cap, int flags) { (void)flags; return probe_malloc(cap); }
typedef struct { char *valuestring; } cJSON;
static cJSON root, jpem;
static cJSON *cJSON_Parse(const char *input) { (void)input; return &root; }
static const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *obj, const char *key) {
    (void)obj;
    if (strcmp(key, "cert_pem")) return NULL;
    jpem.valuestring = cert; return missing_cert ? NULL : &jpem;
}
static bool cJSON_IsString(const cJSON *obj) { return obj != NULL; }
static bool cJSON_IsTrue(const cJSON *obj) { return obj != NULL; }
static void cJSON_Delete(cJSON *obj) { (void)obj; }
static bool http_oneshot(bool post, const char *url, const char *body, const char *pem,
                         char *rx, size_t rx_cap, int *status, int *len, char *diag, size_t diag_cap) {
    assert(!post && body == NULL);
    assert(strstr(url, "fixture.invalid")); requests++;
    bool health = strstr(url, PROBE_HEALTH_PATH) != NULL;
    assert(health || strstr(url, IDENT_PATH));
    assert(health ? pem == s_probe_pem : pem == NULL);
    snprintf(rx, rx_cap, "anonymous response"); *len = (int)strlen(rx);
    *status = (health ? health_fail : identity_fail) ? 503 : 200;
    snprintf(diag, diag_cap, "%s", *status == 200 ? "" : "mock response failure");
    return true;
}
'''
BODY_MAIN = r'''
int main(void) {
    char out[160]; strcpy(s_probe_cfg.host, "fixture.invalid"); s_probe_cfg.port=8798;
    strcpy(cert, "anonymous certificate");
    assert(tls_probe_body(out, sizeof out)); assert(requests == 2 && strstr(out, "200"));
    requests = 0; assert(tls_probe_body(out, 1)); assert(out[0] == 0 && requests == 2);
    health_fail = true; assert(!tls_probe_body(out, sizeof out)); health_fail = false;
    identity_fail = true; assert(!tls_probe_body(out, sizeof out)); identity_fail = false;
    missing_cert = true; assert(!tls_probe_body(out, sizeof out)); missing_cert = false;
    memset(cert, 'x', sizeof cert - 1); cert[sizeof cert - 1] = 0;
    requests = 0; assert(!tls_probe_body(out, sizeof out));
    assert(requests == 1 && strstr(out, "capacity"));
    oom = true; requests=0; assert(!tls_probe_body(out, sizeof out)); assert(requests == 0);
    puts("Production probe body: GET-only/health-failure/identity/cert-capacity/OOM/tiny-output PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='fnos-probe-body-') as tmp:
    path = Path(tmp) / 'body.c'
    binary = Path(tmp) / 'body'
    path.write_text(BODY_MOCKS + probe + BODY_MAIN)
    subprocess.run(['cc', '-std=c11', '-UNDEBUG', '-fsanitize=address,undefined', '-g',
                    '-I', str(ROOT/'components/fnos_monitor'), str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
