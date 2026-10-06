// Wi-Fi 凭据的运行时存储（NVS）。见 fnos_wifi_store.h 的说明。
#include "fnos_wifi_store.h"

#include <string.h>
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
    char s[64] = "";
    char p[96] = "";

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof s;
        if (nvs_get_str(h, KEY_SSID, s, &n) != ESP_OK) s[0] = 0;
        n = sizeof p;
        if (nvs_get_str(h, KEY_PASS, p, &n) != ESP_OK) p[0] = 0;
        nvs_close(h);
    }
    // NVS 里没有 → 用编译期默认（这就是"出厂默认"）
    if (!s[0]) {
        strncpy(s, APP_WIFI_SSID, sizeof s - 1);
        strncpy(p, APP_WIFI_PASS, sizeof p - 1);
    }

    if (ssid && ssid_cap) { strncpy(ssid, s, ssid_cap - 1); ssid[ssid_cap - 1] = 0; }
    if (pass && pass_cap) { strncpy(pass, p, pass_cap - 1); pass[pass_cap - 1] = 0; }
    return !fnos_wifi_store_placeholder(s);
}

bool fnos_wifi_store_save(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) { fnos_wifi_store_clear(); return true; }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err)); return false; }
    err = nvs_set_str(h, KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_PASS, pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    // 只打 SSID 长度，不打内容也不打口令
    ESP_LOGI(TAG, "凭据已保存（ssid %u 字符，口令 %u 字符）",
             (unsigned)strlen(ssid), (unsigned)strlen(pass ? pass : ""));
    return err == ESP_OK;
}

void fnos_wifi_store_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, KEY_SSID);
    nvs_erase_key(h, KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "凭据已清除（退回编译期默认）");
}
