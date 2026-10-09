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

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "wifi_cli";
#define CLI_LINE_MAX 192

static void usage(void)
{
    printf("\n串口命令：\n"
           "  wifi show                     看当前状态（不回显口令）\n"
           "  wifi set <SSID> <口令>        写入并立刻重连（口令里有空格也没关系）\n"
           "  wifi clear                    清掉保存的凭据（退回出厂默认）\n"
           "  page <0-5>                    切页：总览/存储/网络/系统/温度/告警（实机验收用）\n"
           "  temp <n>                      切到温度页并展开第 n 台设备（0 起）的通道网格\n"
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
    printf("状态：%s\n", fnos_net_state_str());
}

static void cmd_set(char *args)
{
    while (*args == ' ') args++;
    if (!*args) { usage(); return; }
    char *sp = strchr(args, ' ');
    if (!sp) {
        printf("缺少口令：wifi set <SSID> <口令>（开放网络就写 wifi set <SSID> \"\"）\n");
        return;
    }
    *sp = 0;
    char *pass = sp + 1;
    while (*pass == ' ') pass++;
    fnos_net_set_credentials(args, pass);
    printf("已写入并开始重连：%s\n（口令不在此回显）\n", args);
}

static void handle(char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
    if (!n) return;

    if (!strcmp(line, "help") || !strcmp(line, "?")) { usage(); return; }
    if (!strncmp(line, "page", 4)) {
        const char *arg = line + 4;
        while (*arg == ' ') arg++;
        int n = (*arg >= '0' && *arg <= '9') ? *arg - '0' : -1;
        if (n < 0 || n >= FNOS_UI_PAGE_COUNT) { printf("用法：page <0-%d>\n", FNOS_UI_PAGE_COUNT - 1); return; }
        fnos_ui_request_page(n);
        printf("切到第 %d 页（下一跳生效，动画照常）\n", n);
        return;
    }
    if (!strncmp(line, "temp", 4)) {
        /* 温度块的展开态本来只能靠手指点，而实机验收时人不在板子跟前、
           又没有触摸自动化 —— 这条命令是"12 路网格"进实机照片的唯一通道。 */
        const char *arg = line + 4;
        while (*arg == ' ') arg++;
        int n = -1;
        if (*arg >= '0' && *arg <= '9') {
            n = 0;
            while (*arg >= '0' && *arg <= '9') n = n * 10 + (*arg++ - '0');
        }
        if (n < 0) { printf("用法：temp <设备序号，0 起>（切到温度页并展开该设备的通道网格）\n"); return; }
        fnos_ui_request_page(4);                 /* 4 = 温度页（页序见 fnos_ui.h） */
        fnos_ui_request_temp_expand(n);
        printf("温度页：展开第 %d 台设备（下一跳生效）\n", n);
        return;
    }
    if (!strcmp(line, "tls")) {
        /* 只读自检：开详细日志 → 跑一次真 HTTPS → 关回去。
           mbedTLS/esp-tls 的失败原因只有 DEBUG 级别才打得出来，而这条命令
           正是为"配对握手失败"准备的，所以在这里临时抬高日志级别。 */
        esp_log_level_set("esp-tls", ESP_LOG_DEBUG);
        esp_log_level_set("mbedtls", ESP_LOG_DEBUG);
        esp_log_level_set("HTTP_CLIENT", ESP_LOG_DEBUG);
        printf("正在跑 HTTPS 自检（下面几行是 mbedTLS/esp-tls 的细节）…\n");
        fnos_pair_tls_probe_async();
        return;
    }
    if (!strncmp(line, "wifi", 4)) {
        char *sub = line + 4;
        while (*sub == ' ') sub++;
        if (!*sub || !strcmp(sub, "show")) { cmd_show(); return; }
        if (!strncmp(sub, "set", 3))       { cmd_set(sub + 3); return; }
        if (!strncmp(sub, "clear", 5))     { fnos_wifi_store_clear(); printf("凭据已清除，重启后生效（或再 wifi set 一次）\n"); return; }
        usage();
        return;
    }
    /* 台架命令放在最后兜底之前：它自成一套（bench …），认不出来会返回 false 交给下面报错。 */
    if (fnos_perf_cli(line)) return;
    printf("不认识的命令：%s（输入 help 看用法）\n", line);
}

static void cli_task(void *arg)
{
    static char line[CLI_LINE_MAX];
    // 起站之后再等一下，免得和开机日志抢串口
    vTaskDelay(pdMS_TO_TICKS(3000));
    printf("\n[串口] 命令可用：wifi set <SSID> <口令> / wifi show / page <0-5> / temp <n> / tls（HTTPS 自检）/ help\n");
    for (;;) {
        if (!fgets(line, sizeof line, stdin)) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        handle(line);
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
