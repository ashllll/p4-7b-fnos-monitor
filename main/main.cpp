// ESP32-P4-WIFI6-Touch-LCD-7B：飞牛 NAS（fnOS）状态监视器。
//
// 形态：专用全屏仪表盘，开机直进，没有启动器/系统外壳。
// 底座沿用 p4-7b-unifi-app 在这块板子上验证过的全部显示/触摸/内存结论：
//   * touch_flags 全 0（官方示例的 mirror_x/mirror_y 在本板是多做一次 180° 翻转）
//   * tear_avoid = TRIPLE_PARTIAL（MIPI-DSI 推荐值，DOUBLE_DIRECT 会警告并卡死）
//   * LVGL 绘制缓冲/对象/任务栈搬家到 PSRAM（内部 RAM 留给 Wi-Fi/显示/DMA）
//   * 数据链路可以走 HTTPS：已开 CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC 与
//     CONFIG_MBEDTLS_DYNAMIC_BUFFER，mbedTLS 缓冲从 PSRAM 走，不再吃光内部 RAM
//     （这一点与 unifi 那版结论相同；证书固定与配对在 fnos_pair 里）
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <atomic>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fnos_net.h"
#include "fnos_data.h"
#include "fnos_ui.h"
#include "ui_kit/uk_theme.h"   /* fnos_ui_theme_use/name：调色板切在 ui_kit 里 */
#include "ui_kit/uk.h"         /* optional diagnostic number-cache statistics */
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
// 视觉验收辅助：自动轮播每一页（页数取 FNOS_UI_PAGE_COUNT，加页不用改这里）
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

#if CONFIG_FNOS_SOAK_DIAG
/* ── 72 小时长测诊断心跳（给 tools/audit125/soak72.py 只读采集）──────────────
   故意跑在 **LVGL 事件循环**里：这个 lv_timer 由适配器的 LVGL 任务调用，所以
   "有没有心跳"直接等于"LVGL 任务还活着"，两次心跳的间隔就是验收表里那条 ≤15 秒。
   它**不测帧耗时、不测触摸延迟**——那两件事要另做屏幕/触摸验收，心跳再稳也不能
   当流畅度证据。看堆余量给人读请开 FNOS_HEAP_DEBUG，这条只给机器解析。 */
#include "esp_app_desc.h"     // esp_app_get_elf_sha256：固件 ELF 的 SHA-256
#include "esp_system.h"       // esp_reset_reason：异常复位原因
#include "fnos_pair.h"        // fnos_pair_stack_min_free
#include "freertos/task.h"    // uxTaskGetStackHighWaterMark

static void diag_timer_cb(lv_timer_t *t)
{
    (void)t;
    static uint32_t n;
    static int64_t  last_us;
    static char     line[768];
    static char     ev_name[16];          // 本次启动的异常复位名（只判定一次）

    int64_t now_us = esp_timer_get_time();
    int64_t gap_s = last_us ? (now_us - last_us) / 1000000 : 0;   // LVGL 心跳间隔
    last_us = now_us;

    if (!ev_name[0]) {
        // 只有"上一轮跑挂了"才算异常：掉电/USB/软复位都是计划内动作，
        // 长测前换固件重启不该被记成非计划复位（审计 §4：0 次非计划复位）。
        const char *nm = NULL;
        switch (esp_reset_reason()) {
        case ESP_RST_PANIC:      nm = "panic";      break;
        case ESP_RST_INT_WDT:    nm = "int_wdt";    break;
        case ESP_RST_TASK_WDT:   nm = "task_wdt";   break;
        case ESP_RST_WDT:        nm = "wdt";        break;
        case ESP_RST_BROWNOUT:   nm = "brownout";   break;
        case ESP_RST_CPU_LOCKUP: nm = "cpu_lockup"; break;
        default: break;
        }
        if (nm) snprintf(ev_name, sizeof(ev_name), "%s", nm);
    }

    uk_number_stats_t motion;
    uk_number_stats(&motion);
    fnos_data_diag_t d;
    fnos_data_diag(&d);
    int64_t age_s = d.recv_ms ? (now_us / 1000 - d.recv_ms) / 1000 : -1;
    char fw[80] = { 0 };
    esp_app_get_elf_sha256(fw, sizeof(fw));      // 64 位小写 hex；和本地 sha256(ELF) 对齐
    char ev_field[24] = "";
    if (ev_name[0]) snprintf(ev_field, sizeof(ev_field), "\"%s\"", ev_name);

    // 一次 snprintf 再一行日志：串口里不会出现被别的任务插进半行的情况。
    n++;
    snprintf(line, sizeof(line),
             "FNOS_DIAG {\"n\":%u,\"up\":%lld,\"hz\":10,\"fw\":\"%s\",\"rst\":%d,\"ev\":[%s],"
             "\"io\":{\"ok\":%u,\"fail\":%u,\"age\":%lld,\"p95\":%d,\"src\":%lld},"
             "\"ui\":{\"age\":%lld},"
             "\"motion\":{\"bytes\":%u,\"peak\":%u,\"budget\":%u,\"alloc\":%u,\"mask\":%u,\"anim\":%u},"
             "\"mem\":{\"ifree\":%u,\"imin\":%u,\"imax\":%u,\"dfree\":%u,\"dmin\":%u,"
             "\"pfree\":%u,\"pmin\":%u},"
             "\"stk\":{\"lvgl\":%u,\"poll\":%u,\"pair\":%u}}",
             (unsigned)n, (long long)(now_us / 1000), fw, (int)esp_reset_reason(), ev_field,
             (unsigned)d.ok_count, (unsigned)d.fail_count, (long long)age_s, d.p95_ms,
             (long long)(d.source_ts ? d.source_ts : -1), (long long)gap_s,
             (unsigned)motion.bytes, (unsigned)motion.peak_bytes,
             (unsigned)motion.budget_fallbacks, (unsigned)motion.allocation_fallbacks,
             (unsigned)motion.mask_fallbacks, (unsigned)motion.animation_fallbacks,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)uxTaskGetStackHighWaterMark(NULL),   // 当前任务就是 LVGL 任务
             (unsigned)fnos_data_stack_min_free(),
             (unsigned)fnos_pair_stack_min_free());
    ESP_LOGI(TAG, "%s", line);
}
#endif

/* ── 界面到底起来没有（审计 M11）───────────────────────────────────────────
   以前拿不到显示锁只留一句 display lock failed：屏幕永远黑的，而日志里没有一条
   持续存在的"这台板子现在处于降级状态"的标记，长测看起来一切正常。这条路真的会
   被走到（见下面 DOUBLE_DIRECT 的注释：适配器 LVGL 任务持锁等 VSYNC 就是这个症状）。
   现在：初次 3 次尝试 → 失败置降级标志 → 一个重试任务按退避最多再试五轮；
   期间数据采集与串口 CLI 照常，日志与启动收尾行都带着 degraded 状态。 */
// Only this status bit is shared; it does not publish UI objects or other state.
static std::atomic_bool s_ui_degraded{false};

static bool start_periodic_timer(const esp_timer_create_args_t *args, uint64_t period_us)
{
    esp_timer_handle_t timer = NULL;
    esp_err_t err = esp_timer_create(args, &timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s timer create failed: %s", args->name, esp_err_to_name(err));
        return false;
    }
    err = esp_timer_start_periodic(timer, period_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s timer start failed: %s", args->name, esp_err_to_name(err));
        const esp_err_t cleanup_err = esp_timer_delete(timer);
        if (cleanup_err != ESP_OK)
            ESP_LOGE(TAG, "%s timer cleanup failed: %s", args->name, esp_err_to_name(cleanup_err));
        return false;
    }
    return true;
}

static bool ui_build_locked(void)
{
    for (int i = 0; i < 3; i++) {
        /* 不能用 try-lock（0 超时）：LVGL 正在首刷时"忙"会被误判成"失败"，
           UI 一次都建不出来。2026-10-10 的矩阵旋转与绘制切片两次实验都撞在这上面
           （改成 2s 后立刻通过，见 docs/perf-report-2026-10-10.md 第 12/18 条）。 */
        if (!bsp_display_lock(2000)) {
            ESP_LOGW(TAG, "display lock failed（第 %d/3 次，2 秒超时）", i + 1);
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
        fnos_ui_create();
        if (!fnos_ui_ready()) {
            bsp_display_unlock();
            ESP_LOGE(TAG, "UI refresh unavailable（第 %d/3 次）", i + 1);
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
#if CONFIG_FNOS_SOAK_DIAG
        // 长测心跳：建在 LVGL 任务里，所以它活着 = LVGL 事件循环还在跑。
        if (lv_timer_create(diag_timer_cb, 10 * 1000, NULL))
            ESP_LOGW(TAG, "SOAK DIAG: 每 10 秒一行 FNOS_DIAG（长测专用，生产版请关掉）");
        else
            ESP_LOGE(TAG, "SOAK DIAG timer allocation failed: no FNOS_DIAG heartbeat");
#endif
#if CONFIG_FNOS_AUTO_PAGE_SEC > 0
        if (lv_timer_create(auto_page_cb, CONFIG_FNOS_AUTO_PAGE_SEC * 1000, NULL))
            ESP_LOGW(TAG, "VERIFY MODE: auto page every %d s", CONFIG_FNOS_AUTO_PAGE_SEC);
        else
            ESP_LOGE(TAG, "VERIFY MODE timer allocation failed: no auto page");
#endif
        bsp_display_unlock();
        return true;
    }
    return false;
}

/* 降级后的后台恢复：不阻塞开机，按 5/10/20/40/60 秒退避重试 5 轮（退避共 2 分 15 秒，另计每轮锁等待）。
   拿不到锁的板子重启后会再走一遍这条路；重试期间数据采集与串口 CLI 都照常。 */
static void ui_retry_task(void *arg)
{
    (void)arg;
    static const int delays_s[] = { 5, 10, 20, 40, 60 };
    for (int i = 0; i < (int)(sizeof delays_s / sizeof delays_s[0]); i++) {
        vTaskDelay(pdMS_TO_TICKS(delays_s[i] * 1000));
        if (ui_build_locked()) {
            s_ui_degraded.store(false, std::memory_order_relaxed);
            ESP_LOGW(TAG, "界面恢复：第 %d 次重试完成建树并启动刷新", i + 1);
            vTaskDelete(NULL);
            return;
        }
    }
    ESP_LOGE(TAG, "界面始终不可用（显示锁或刷新定时器失败）：只剩数据采集与串口 CLI，重启后才会再试");
    vTaskDelete(NULL);
}

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
        // 显示路径 A/B 都试过、都不可用（别重复踩）：
        //  · DOUBLE_DIRECT：LVGL 直渲面板整帧缓冲、flush 只切地址，但本板起不来 ——
        //    适配器 LVGL 任务在首帧 flush 里等 VSYNC 通知，一直持锁 → app_main 的
        //    esp_lv_adapter_lock 超时（"display lock failed"），UI 根本没建出来。
        //  · NONE：flush 只按脏区 draw_bitmap（面积成正比、没有整帧 blit），但适配器
        //    明确拒绝旋转："rotation not supported under TEAR_AVOID_MODE_NONE"，
        //    本板要 ROTATE_180。
        // TRIPLE_PARTIAL 每帧固定成本 = 局部区拷进中间缓冲 + 整帧 blit（1024×600×2B ×2），
        // 实测约 8ms/帧且与内容无关（轻页 22.6ms vs 重页 26.5ms，tick 差 8 倍而帧成本只差 17%）。
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI,
        // 触摸不做任何镜像：adapter 只旋转 framebuffer，输入路径没有坐标变换，
        // 官方示例的 mirror_x/mirror_y 在本板 + ROTATE_180 下等于再做一次 180° 翻转。
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    if (bsp_display_start_with_config(&cfg)) {
        bsp_display_backlight_on();               // 先点亮（这时是 100%）
        bsp_display_brightness_set(APP_BL_PCT);   // 再压到配置值
    } else {
        /* 显示起不来就别再往下走了：LVGL 任务不存在，建 UI/持显示锁都没有意义。
           但要说清后果，别只留一句 failed（审计 M11）——这台板子现在是"没屏幕、
           也没数据采集"的状态，串口是唯一出口。 */
        ESP_LOGE(TAG, "display start failed：本次开机没有界面，不会启动数据采集；已启动的网络与串口 CLI 保持原状态");
        return;
    }
    log_heap("display");

    /* 配色在**建任何构件之前**定下来：颜色令牌是运行时读调色板的，
       建完之后再换，已经建好的对象还留着旧颜色（LVGL 样式是拷贝语义）。 */
    fnos_ui_theme_use(CONFIG_FNOS_PALETTE);

    if (ui_build_locked()) {
        ESP_LOGI(TAG, "界面已建起来");
    } else {
        s_ui_degraded.store(true, std::memory_order_relaxed);
        ESP_LOGE(TAG, "界面不可用（显示锁或刷新定时器失败）：数据照采，已启动的串口 CLI 保留，后台继续重试");
        if (xTaskCreate(ui_retry_task, "ui_retry", 16 * 1024, NULL, 3, NULL) != pdPASS) {
            ESP_LOGE(TAG, "重试任务也起不来（内存不足）：本次开机不会有界面");
        }
    }
    log_heap("ui");

    fnos_data_start();
    log_heap("polling");

    const esp_timer_create_args_t night_args = { .callback = night_timer_cb, .name = "night" };
    night_timer_cb(NULL);
    if (!start_periodic_timer(&night_args, 60ULL * 1000 * 1000))
        ESP_LOGE(TAG, "夜间背光不会自动更新：保留本次应用值");
#if CONFIG_FNOS_HEAP_DEBUG
    const esp_timer_create_args_t heap_args = { .callback = heap_timer_cb, .name = "heap" };
    if (!start_periodic_timer(&heap_args, 10ULL * 1000 * 1000))
        ESP_LOGE(TAG, "周期 heap 日志不可用（长测少一路观测）");
#endif
    /* 这里只汇总建树与刷新timer；created 不代表所有控件/诊断通道或实机验收通过。 */
    ESP_LOGI(TAG, "boot complete (ui=%s)", s_ui_degraded.load(std::memory_order_relaxed) ? "degraded" : "created");
}
