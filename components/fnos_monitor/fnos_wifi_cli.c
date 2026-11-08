// 串口配网兜底：`wifi set <ssid> <口令>` / `wifi clear` / `wifi show`。
//
// 为什么要有它：屏幕配网卡（下一步的 UI）是给人用的主路径，但总有屏幕点不动、
// 或者板子还没进系统的时候——比如刚烧录完、屏幕还没起来、或者现场只有一根
// USB 线。这条命令是那两种情况下的唯一入口，所以它必须**永远可用**：
// 不依赖 UI、不依赖网络，只依赖 NVS 和控制台。
//
// 口令只写不读：`wifi show` 只报"有没有凭据、SSID 是什么、连到哪了"，永不回显口令。
#include "fnos_net.h"
#include "fnos_pair.h"
#include "fnos_perf.h"
#include "fnos_wifi_store.h"
#include "fnos_ui.h"
#include "fnos_data.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_hosted.h"

static const char *TAG = "wifi_cli";
#define CLI_LINE_MAX 192

static void cmd_shot(void)
{
    /* 板上没有快门，相机权限也未必给：像素证据走"LVGL 渲染 → 串口"。
       协议给 tools/shot_pull.py 用：SHOT-BEGIN <w> <h> / 若干行 base64 / SHOT-END。
       512x300 RGB565 = 300 KB，base64 后 400 KB，115200 下约 35 秒——慢但一次能看清版面。 */
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    fnos_ui_request_shot();
    for (int i = 0; i < 300 && !fnos_ui_shot_done(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    int w = 0, h = 0;
    const uint8_t *px = fnos_ui_shot_pixels(&w, &h);
    if (!px) {
        printf("截图失败：UI 线程 3 秒内没给出快照（页面还没建起来？）\n");
        return;
    }
    /* 一帧 400 KB 要吐 40 秒，期间任何一条日志都会插进 base64 行中间把行撕碎
       （实测 66 行里 3 行；只静音几个 tag 后又换别的 tag 撕），主机那边只看到
       "padding 错"。所以整段把根级别压到 NONE，吐完恢复根级别和运行时改过的那几个
       tag（fnos_pair.c 会把 esp-tls/mbedtls/HTTP_CLIENT 抬到 DEBUG）。 */
    const esp_log_level_t root = esp_log_level_get(NULL);
    static const char *const keep[] = { "esp-tls", "mbedtls", "HTTP_CLIENT", "fnos_data", "fnos_ui" };
    enum { KEEP_N = sizeof keep / sizeof keep[0] };
    esp_log_level_t kept[KEEP_N];
    for (int i = 0; i < KEEP_N; ++i) kept[i] = esp_log_level_get(keep[i]);
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("SHOT-BEGIN %d %d\n", w, h);
    const size_t total = (size_t)w * (size_t)h * 2;
    char line[516];
    for (size_t off = 0; off < total; off += 384) {
        const size_t n = (total - off < 384) ? (total - off) : 384;
        size_t o = 0;
        for (size_t i = 0; i < n; i += 3) {
            const uint32_t a = px[off + i];
            const uint32_t b = (i + 1 < n) ? px[off + i + 1] : 0;
            const uint32_t c = (i + 2 < n) ? px[off + i + 2] : 0;
            const uint32_t v = (a << 16) | (b << 8) | c;
            line[o++] = b64[(v >> 18) & 0x3F];
            line[o++] = b64[(v >> 12) & 0x3F];
            line[o++] = (i + 1 < n) ? b64[(v >> 6) & 0x3F] : '=';
            line[o++] = (i + 2 < n) ? b64[v & 0x3F] : '=';
        }
        line[o] = '\0';
        printf("%s\n", line);
    }
    printf("SHOT-END\n");
    esp_log_level_set("*", root);
    for (int i = 0; i < KEEP_N; ++i) esp_log_level_set(keep[i], kept[i]);
}

static void usage(void)
{
    printf("\n串口命令：\n"
           "  wifi show                     看当前状态（不回显口令）\n"
           "  wifi version                  只读查询联网协处理器固件版本\n"
           "  wifi set <SSID> <口令>        写入并立刻重连（口令里有空格也没关系）\n"
           "  wifi clear                    清掉保存的凭据（退回出厂默认）\n"
           "  page <0-5>                    切页：总览/存储/网络/系统/温度/告警（实机验收用）\n"
           "  temp <n>                      切到温度页并展开第 n 台设备（0 起）的通道网格\n"
           "  shot [pair|detail]            截图：当前页渲染成 512x300 RGB565 经串口吐出；\n"
           "                                pair 先摊开配对卡，detail 先开当前页第一行的详情\n"
           "  tls                           HTTPS 自检：明文取证书 + 用证书探一次 /health\n"
           "  bench help                    触控/渲染台架（合成手势 + 计数器；仅带台架的固件有）\n"
           "  help                          显示这段说明\n\n");
}

static void cmd_show(void)
{
    char ssid[64] = "";
    bool ok = fnos_wifi_store_load(ssid, sizeof ssid, NULL, 0);
    printf("凭据：%s\n", ok ? "有" : "没有（未配网）");
    if (ssid[0]) printf("SSID：%s\n", ssid);
    char st[160];
    fnos_net_state_str_r(st, sizeof st);        // 本地缓冲：不跟 UI 抢静态串（审计 M09）
    printf("状态：%s\n", st);
}

static void cmd_version(void)
{
    if (!fnos_net_online()) {
        printf("Wi-Fi not online; coprocessor firmware query skipped\n");
        return;
    }
    esp_hosted_coprocessor_fwver_t version = {0};
    esp_err_t err = esp_hosted_get_coprocessor_fwversion(&version);
    if (err != ESP_OK) {
        printf("coprocessor firmware query failed: %s\n", esp_err_to_name(err));
        return;
    }
    printf("coprocessor firmware=%u.%u.%u (RPC, read-only)\n",
           (unsigned)version.major1, (unsigned)version.minor1, (unsigned)version.patch1);
}

static void cmd_set(char *args)
{
    while (*args == ' ' || *args == '\t') args++;
    if (!*args) { usage(); return; }
    char *sp = strpbrk(args, " \t");
    if (!sp) {
        printf("缺少口令：wifi set <SSID> <口令>（开放网络就写 wifi set <SSID> \"\"）\n");
        return;
    }
    *sp = 0;
    char *pass = sp + 1;
    while (*pass == ' ' || *pass == '\t') pass++;
    if (!strcmp(pass, "\"\"")) pass = "";
    fnos_net_set_credentials(args, pass);
    /* 入队即返回（审计 M05）：落 NVS 与重连由网络任务做，成功与否用 wifi show 看——
       所以这里只能说"已提交"，不能说"已写入"。 */
    printf("已提交，正在重连：%s\n（口令不在此回显；用 wifi show 看结果）\n", args);
}

static bool command_is(const char *line, const char *command)
{
    size_t n = strlen(command);
    return !strncmp(line, command, n) &&
           (!line[n] || line[n] == ' ' || line[n] == '\t');
}

static int command_index(const char *arg)
{
    while (*arg == ' ' || *arg == '\t') arg++;
    if (*arg < '0' || *arg > '9') return -1;
    errno = 0;
    char *end;
    long value = strtol(arg, &end, 10);
    while (*end == ' ' || *end == '\t') end++;
    if (errno == ERANGE || value > INT_MAX || *end) return -1;
    return (int)value;
}

static void clear_line(char *line, size_t size)
{
    volatile unsigned char *p = (volatile unsigned char *)line;
    while (size--) *p++ = 0;
}

static void handle(char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
    if (!n) return;

    if (!strcmp(line, "help") || !strcmp(line, "?")) { usage(); return; }
    if (command_is(line, "poll")) {
        int ms = command_index(line + 4);
        if (ms < 0 || !fnos_data_set_poll_interval_ms((unsigned)ms)) {
            printf("poll <%u-%u ms> (volatile; current=%u ms)\n",
                   FNOS_POLL_INTERVAL_MIN_MS, FNOS_POLL_INTERVAL_MAX_MS,
                   fnos_data_poll_interval_ms());
            return;
        }
        printf("poll period=%u ms; UI data frame=%u ms (volatile)\n",
               fnos_data_poll_interval_ms(), fnos_ui_refresh_period_ms());
        return;
    }
    if (command_is(line, "page")) {
        int n = command_index(line + 4);
        if (n < 0 || n >= FNOS_UI_PAGE_COUNT) { printf("用法：page <0-%d>\n", FNOS_UI_PAGE_COUNT - 1); return; }
        fnos_ui_request_page(n);
        printf("切到第 %d 页（下一跳生效，动画照常）\n", n);
        return;
    }
    if (command_is(line, "temp")) {
        /* 温度块的展开态本来只能靠手指点，而实机验收时人不在板子跟前、
           又没有触摸自动化 —— 这条命令是"12 路网格"进实机照片的唯一通道。 */
        int n = command_index(line + 4);
        if (n < 0) { printf("用法：temp <设备序号，0 起>（切到温度页并展开该设备的通道网格）\n"); return; }
        fnos_ui_request_page(4);                 /* 4 = 温度页（页序见 fnos_ui.h） */
        fnos_ui_request_temp_expand(n);
        printf("温度页：展开第 %d 台设备（下一跳生效）\n", n);
        return;
    }
    if (command_is(line, "shot")) {
        /* 覆盖层（配对卡 / 实体详情）只能点开，板上没有触摸自动化：先摊开、
           等淡入落定，再照旧截当前页。分两跳是因为截的是"落定后的那一帧"。 */
        char *what = line + 4;
        while (*what == ' ' || *what == '\t') what++;
        int kind = -1;
        if (*what == '\0') kind = -1;
        else if (!strcmp(what, "pair")) kind = 0;
        else if (!strcmp(what, "detail")) kind = 1;
        else {
            printf("用法：shot [pair|detail]（pair=配对卡，detail=当前页第一行的详情，"
                   "先 page 1/2/3 选好页）\n");
            return;
        }
        if (kind >= 0) {
            fnos_ui_request_overlay(kind);
            vTaskDelay(pdMS_TO_TICKS(800));      /* 覆盖层淡入 + 布局落定 */
            printf("覆盖层已摊开（%s），开始截图…\n", what);
        }
        cmd_shot();
        return;
    }
    if (!strcmp(line, "tls")) {
        /* 只读自检：跑一次真 HTTPS。mbedTLS/esp-tls 的失败原因只有 DEBUG 级别
           才打得出来，而这条命令正是为"配对握手失败"准备的——抬高级别的窗口
           由 fnos_pair_tls_probe() 自己开关（审计 M08：以前抬了不关）。 */
        printf("正在跑 HTTPS 自检（下面几行是 mbedTLS/esp-tls 的细节）…\n");
        fnos_pair_tls_probe_async();
        return;
    }
    if (command_is(line, "wifi")) {
        char *sub = line + 4;
        while (*sub == ' ' || *sub == '\t') sub++;
        /* 先把子命令切出来做**整词**比较：以前用 strncmp(sub,"set",3)/"clear",5，
           于是 `wifi clearing` 会去清 NVS、`wifi setfoo bar` 会写 SSID=foo
           （审计 M06）。切完 token 后 rest 指向参数部分。 */
        char *arg = sub;
        while (*arg && *arg != ' ' && *arg != '\t') arg++;
        char sep = *arg;
        *arg = 0;
        char *rest = sep ? arg + 1 : arg;
        while (*rest == ' ' || *rest == '\t') rest++;
        if (*rest && strcmp(sub, "set")) { usage(); return; }
        if (!*sub || !strcmp(sub, "show")) { cmd_show(); return; }
        if (!strcmp(sub, "version"))      { cmd_version(); return; }
        if (!strcmp(sub, "set"))           { cmd_set(rest); return; }
        if (!strcmp(sub, "clear")) {
            if (fnos_wifi_store_clear()) {
                printf("凭据已清除，重启后生效（或再 wifi set 一次）\n");
            } else {
                printf("凭据清除失败（NVS 写入错误），保存状态未确定，请重试\n");
            }
            return;
        }
        usage();
        return;
    }
    /* 台架命令放在最后兜底之前：它自成一套（bench …），认不出来会返回 false 交给下面报错。 */
    if (fnos_perf_cli(line)) return;
    printf("不认识的命令（输入 help 看用法）\n");
}

static void cli_task(void *arg)
{
    (void)arg;
    static char line[CLI_LINE_MAX];
    size_t used = 0;
    bool discard = false;
    // 起站之后再等一下，免得和开机日志抢串口
    vTaskDelay(pdMS_TO_TICKS(3000));
    printf("\n[串口] 命令可用：wifi set <SSID> <口令> / wifi show / page <0-5> / temp <n> / tls（HTTPS 自检）/ help\n");
    for (;;) {
        /* USB stdin may return a short fragment before newline. Retain it even
           across a transient EOF; only a complete line may change device state. */
        if (!fgets(line + used, sizeof line - used, stdin)) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        used += strlen(line + used);
        if (!used || line[used - 1] != '\n') {
            if (used == sizeof line - 1) {
                discard = true;
                used = 0;
                clear_line(line, sizeof line);
            }
            continue;
        }
        /* A discarded overlong line remains discarded through its final
           fragment, including a tail that looks like another valid command. */
        if (discard) printf("命令太长（上限 %d 字节），整行已忽略\n", CLI_LINE_MAX - 1);
        else handle(line);
        used = 0;
        discard = false;
        clear_line(line, sizeof line);
        printf("> ");
        fflush(stdout);
    }
}

void fnos_wifi_cli_start(void)
{
    if (xTaskCreate(cli_task, "wifi_cli", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "串口配网任务没起来（内存不足），屏幕配网路径不受影响");
    }
}
