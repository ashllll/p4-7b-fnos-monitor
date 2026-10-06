// 串口配网兜底：`wifi set <ssid> <口令>` / `wifi clear` / `wifi show`。
//
// 为什么要有它：屏幕配网卡（下一步的 UI）是给人用的主路径，但总有屏幕点不动、
// 或者板子还没进系统的时候——比如刚烧录完、屏幕还没起来、或者现场只有一根
// USB 线。这条命令是那两种情况下的唯一入口，所以它必须**永远可用**：
// 不依赖 UI、不依赖网络，只依赖 NVS 和控制台。
//
// 口令只写不读：`wifi show` 只报"有没有凭据、SSID 是什么、连到哪了"，永不回显口令。
#include "fnos_net.h"
#include "fnos_wifi_store.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "wifi_cli";
#define LINE_MAX 192

static void usage(void)
{
    printf("\n配网命令：\n"
           "  wifi show                     看当前状态（不回显口令）\n"
           "  wifi set <SSID> <口令>        写入并立刻重连（口令里有空格也没关系）\n"
           "  wifi clear                    清掉保存的凭据（退回出厂默认）\n"
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
    if (!strncmp(line, "wifi", 4)) {
        char *sub = line + 4;
        while (*sub == ' ') sub++;
        if (!*sub || !strcmp(sub, "show")) { cmd_show(); return; }
        if (!strncmp(sub, "set", 3))       { cmd_set(sub + 3); return; }
        if (!strncmp(sub, "clear", 5))     { fnos_wifi_store_clear(); printf("凭据已清除，重启后生效（或再 wifi set 一次）\n"); return; }
        usage();
        return;
    }
    printf("不认识的命令：%s（输入 help 看用法）\n", line);
}

static void cli_task(void *arg)
{
    static char line[LINE_MAX];
    // 起站之后再等一下，免得和开机日志抢串口
    vTaskDelay(pdMS_TO_TICKS(3000));
    printf("\n[配网] 串口命令可用：wifi set <SSID> <口令> / wifi show / help\n");
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
