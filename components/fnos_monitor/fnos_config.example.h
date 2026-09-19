#pragma once
// 复制成 fnos_config.h（已被 .gitignore 忽略）后填写真实值。
// 这个文件是模板，不含任何真实凭据。

// ── Wi-Fi（板载 ESP32-C6 经 ESP-Hosted 提供）─────────────────────────
#define APP_WIFI_SSID "your-ssid"
#define APP_WIFI_PASS "your-password"

// ── fnos-agent 采集端点（跑在 NAS 上，见仓库 nas/ 目录）───────────────
// 纯 HTTP，不用 TLS：本板内部 RAM 紧张，而且采集端只读、只在内网。
#define FNOS_HOST "192.168.0.119"
#define FNOS_PORT 8799
// 采集端设置了 FNAS_TOKEN 时填同样的值；没设就留空。
#define FNOS_TOKEN ""

// ── 显示与时钟 ───────────────────────────────────────────────────────
#define APP_TZ "CST-8"          // 正点时间：POSIX TZ
#define APP_BL_PCT 45           // 背光百分比（0-100）
#define APP_NIGHT_START 23      // 夜间降背光起始小时（含）
#define APP_NIGHT_END   7       // 夜间降背光结束小时（不含）
#define APP_BL_NIGHT_PCT 12
