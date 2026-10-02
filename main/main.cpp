// ESP32-P4-WIFI6-Touch-LCD-7B：飞牛 NAS（fnOS）状态监视器。
//
// 形态：专用全屏仪表盘，开机直进，没有启动器/系统外壳。
// 底座沿用 p4-7b-unifi-app 在这块板子上验证过的全部显示/触摸/内存结论：
//   * touch_flags 全 0（官方示例的 mirror_x/mirror_y 在本板是多做一次 180° 翻转）
//   * tear_avoid = TRIPLE_PARTIAL（MIPI-DSI 推荐值，DOUBLE_DIRECT 会警告并卡死）
//   * LVGL 绘制缓冲/对象/任务栈搬家到 PSRAM（内部 RAM 留给 Wi-Fi/显示/DMA）
//   * 本工程数据链路走纯 HTTP（无 TLS），从根上避开 mbedTLS 吃光内部 RAM 的坑
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "fnos_net.h"
#include "fnos_data.h"
#include "fnos_ui.h"
#if __has_include("fnos_config.h")
#include "fnos_config.h"
#else
#include "fnos_config.example.h"
#endif

static const char *TAG = "main";

static void log_heap(const char *stage)
{
    ESP_LOGI(TAG, "heap %-14s internal=%uKB largest=%uKB dma=%uKB psram=%uKB", stage,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

// 夜间自动降背光（SNTP 授时后生效；时间不可信时按白天处理）
static void night_timer_cb(void *arg)
{
    (void)arg;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    int h = (tmv.tm_year > 120) ? tmv.tm_hour : 12;
    bool night = (APP_NIGHT_START <= APP_NIGHT_END)
                     ? (h >= APP_NIGHT_START && h < APP_NIGHT_END)
                     : (h >= APP_NIGHT_START || h < APP_NIGHT_END);
    static bool last_night = false;
    static bool first = true;
    if (!first && night == last_night) return;
    first = false;
    last_night = night;
    bsp_display_brightness_set(night ? APP_BL_NIGHT_PCT : APP_BL_PCT);
    fnos_ui_set_night(night);            // 夜间配色：纯黑底 + 面板更暗 + 趋势纹理降透明度
    ESP_LOGI(TAG, "backlight %d%% (%s)", night ? APP_BL_NIGHT_PCT : APP_BL_PCT, night ? "night" : "day");
}

#if CONFIG_FNOS_AUTO_PAGE_SEC > 0
// 视觉验收辅助：自动轮播 4 个页面（LVGL 定时器，跑在 LVGL 任务里，无需加锁）
static void auto_page_cb(lv_timer_t *t)
{
    (void)t;
    fnos_ui_set_page(fnos_ui_page() + 1);
}
#endif

#if CONFIG_FNOS_HEAP_DEBUG
static void heap_timer_cb(void *arg)
{
    (void)arg;
    log_heap("periodic");
}
#endif

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "fnOS NAS dashboard booting (agent http://%s:%d)", FNOS_HOST, FNOS_PORT);
    log_heap("boot");

    // Wi-Fi 先起：数据层要等联网，UI 不需要等
    fnos_net_start();

    // LVGL 任务栈 12 KB 且放 PSRAM（ESP_LV_ADAPTER_DEFAULT_CONFIG() 里 stack_in_psram 写死 false）
    esp_lv_adapter_config_t lv_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    lv_cfg.task_stack_size = 12 * 1024;
    lv_cfg.stack_in_psram = true;

    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = lv_cfg,
        .rotation = ESP_LV_ADAPTER_ROTATE_180,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI,
        // 触摸不做任何镜像：adapter 只旋转 framebuffer，输入路径没有坐标变换，
        // 官方示例的 mirror_x/mirror_y 在本板 + ROTATE_180 下等于再做一次 180° 翻转。
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    if (!bsp_display_start_with_config(&cfg)) {
        ESP_LOGE(TAG, "display start failed");
        return;
    }
    bsp_display_backlight_on();               // 先点亮（这时是 100%）
    bsp_display_brightness_set(APP_BL_PCT);   // 再压到配置值
    log_heap("display");

    if (bsp_display_lock(0)) {
        fnos_ui_create();
#if CONFIG_FNOS_AUTO_PAGE_SEC > 0
        lv_timer_create(auto_page_cb, CONFIG_FNOS_AUTO_PAGE_SEC * 1000, NULL);
        ESP_LOGW(TAG, "VERIFY MODE: auto page every %d s", CONFIG_FNOS_AUTO_PAGE_SEC);
#endif
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "display lock failed");
    }
    log_heap("ui");

    fnos_data_start();
    log_heap("polling");

    const esp_timer_create_args_t night_args = { .callback = night_timer_cb, .name = "night" };
    esp_timer_handle_t night_timer = NULL;
    if (esp_timer_create(&night_args, &night_timer) == ESP_OK) {
        night_timer_cb(NULL);
        esp_timer_start_periodic(night_timer, 60ULL * 1000 * 1000);
    }
#if CONFIG_FNOS_HEAP_DEBUG
    const esp_timer_create_args_t heap_args = { .callback = heap_timer_cb, .name = "heap" };
    esp_timer_handle_t heap_timer = NULL;
    if (esp_timer_create(&heap_args, &heap_timer) == ESP_OK) {
        esp_timer_start_periodic(heap_timer, 10ULL * 1000 * 1000);
    }
#endif
    ESP_LOGI(TAG, "boot complete");
}
