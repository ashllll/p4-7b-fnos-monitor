// Wi-Fi 凭据的运行时存储（NVS）。见 fnos_wifi_store.h 的说明。
#include "fnos_wifi_store.h"

#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"

#if __has_include("fnos_config.h")
#include "fnos_config.h"
#else
#include "fnos_config.example.h"
#endif

static const char *TAG = "wifi_store";
#define NVS_NS   "wifi"
#define KEY_SSID "ssid"
#define KEY_PASS "pass"
#define KEY_TXN  "txn_v1"
enum { WIFI_STORE_DIRTY = 0, WIFI_STORE_READY = 1, WIFI_STORE_EMPTY = 2 };

// 普通任务专用。读/写/清除串行，防止两个writer交错发布READY或reader看见中间态。
// 不在临界区里做Flash IO；等待者让出CPU。
static atomic_flag s_store_guard = ATOMIC_FLAG_INIT;
static void store_lock(void)
{
    while (atomic_flag_test_and_set(&s_store_guard)) vTaskDelay(1);
}
static void store_unlock(void) { atomic_flag_clear(&s_store_guard); }
static void store_zero(void *buffer, size_t size)
{
    volatile unsigned char *p = buffer;
    while (size--) *p++ = 0;
}
static esp_err_t store_barrier(nvs_handle_t h, int32_t state)
{
    esp_err_t err = nvs_set_i32(h, KEY_TXN, state);
    return err == ESP_OK ? nvs_commit(h) : err;
}
static bool store_lengths_valid(const char *ssid, const char *pass)
{
    return ssid && pass && ssid[0] && strlen(ssid) <= 32 && strlen(pass) <= 64;
}

// 编译期默认（可能压根没定义，那就当空）
#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID ""
#endif
#ifndef APP_WIFI_PASS
#define APP_WIFI_PASS ""
#endif

bool fnos_wifi_store_placeholder(const char *ssid)
{
    if (!ssid || !ssid[0]) return true;
    static const char *const bad[] = { "your-ssid", "YOUR_SSID", "changeme", "ssid", "TODO" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (strcmp(ssid, bad[i]) == 0) return true;
    }
    return false;
}

bool fnos_wifi_store_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    char s[64] = "", p[96] = "";
    if (ssid && ssid_cap) memset(ssid, 0, ssid_cap);
    if (pass && pass_cap) memset(pass, 0, pass_cap);
    bool ok = false, defaults = false;
    store_lock();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        defaults = true;
    } else if (err == ESP_OK) {
        int32_t txn = WIFI_STORE_DIRTY;
        esp_err_t marker = nvs_get_i32(h, KEY_TXN, &txn);
        size_t n = sizeof s;
        esp_err_t es = nvs_get_str(h, KEY_SSID, s, &n);
        n = sizeof p;
        esp_err_t ep = nvs_get_str(h, KEY_PASS, p, &n);
        nvs_close(h);
        if (marker != ESP_OK && marker != ESP_ERR_NVS_NOT_FOUND) goto done;
        if (marker == ESP_OK && txn != WIFI_STORE_READY && txn != WIFI_STORE_EMPTY) goto done;
        bool empty = es == ESP_ERR_NVS_NOT_FOUND && ep == ESP_ERR_NVS_NOT_FOUND;
        if (marker == ESP_OK && txn == WIFI_STORE_EMPTY) {
            if (!empty) goto done;
            defaults = true;
        } else if (marker == ESP_ERR_NVS_NOT_FOUND && empty) {
            defaults = true; // 兼容旧固件未配网的空namespace。
        } else {
            // 空口令必须是成功读取的空串，不能把读错/缺键当开放网络。
            if (es != ESP_OK || ep != ESP_OK || !store_lengths_valid(s, p)) goto done;
        }
    } else {
        goto done;
    }
    if (defaults) {
        if (!store_lengths_valid(APP_WIFI_SSID, APP_WIFI_PASS)) goto done;
        memcpy(s, APP_WIFI_SSID, strlen(APP_WIFI_SSID) + 1);
        memcpy(p, APP_WIFI_PASS, strlen(APP_WIFI_PASS) + 1);
    }
    if (fnos_wifi_store_placeholder(s) ||
        (ssid && ssid_cap <= strlen(s)) || (pass && pass_cap <= strlen(p))) goto done;
    if (ssid) memcpy(ssid, s, strlen(s) + 1);
    if (pass) memcpy(pass, p, strlen(p) + 1);
    ok = true;
done:
    store_zero(p, sizeof p);
    store_unlock();
    return ok;
}

bool fnos_wifi_store_save(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return fnos_wifi_store_clear();
    if (!pass) pass = "";
    if (!store_lengths_valid(ssid, pass)) return false;
    store_lock();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        // set直接持久化；先落DIRTY，完整保存后才允许重启loader使用。
        err = store_barrier(h, WIFI_STORE_DIRTY);
        if (err == ESP_OK) err = nvs_set_str(h, KEY_SSID, ssid);
        if (err == ESP_OK) err = nvs_set_str(h, KEY_PASS, pass);
        if (err == ESP_OK) err = nvs_commit(h);
        if (err == ESP_OK) err = store_barrier(h, WIFI_STORE_READY);
        nvs_close(h);
    }
    store_unlock();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "凭据保存失败：%s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "凭据已保存（ssid %u 字节，口令 %u 字节）",
             (unsigned)strlen(ssid), (unsigned)strlen(pass));
    return true;
}

bool fnos_wifi_store_clear(void)
{
    store_lock();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = store_barrier(h, WIFI_STORE_DIRTY);
        if (err == ESP_OK) {
            err = nvs_erase_key(h, KEY_SSID);
            if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        }
        if (err == ESP_OK) {
            err = nvs_erase_key(h, KEY_PASS);
            if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        }
        if (err == ESP_OK) err = nvs_commit(h);
        // 保留屏障，不能删marker后让半清除状态回落旧loader分支。
        if (err == ESP_OK) err = store_barrier(h, WIFI_STORE_EMPTY);
        nvs_close(h);
    }
    store_unlock();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "凭据清除失败：%s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "凭据已清除（退回编译期默认）");
    return true;
}
