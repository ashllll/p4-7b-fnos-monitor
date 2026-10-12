#pragma once
/* 主机预览专用：设备端由 ESP-IDF 生成 build/config/sdkconfig.h。
 * 这里只补本工程真正读到的非 LVGL 配置项（CONFIG_LV_* 由 -include lv_kconfig_host.h 提供）。
 *
 * 不要再在这里写 #define CONFIG_FNOS_* —— CMakeLists.txt 会把设备端 sdkconfig.h 里的
 * CONFIG_FNOS_UI_/CHART_WINDOW/STATUS_MAX_BYTES/SNAPSHOT_MAX_BYTES 导成 ui_config_host.h
 * 并在下面 include 进来。这里早先写死过 CONFIG_FNOS_CHART_WINDOW=180，与设备真值冲突：
 * 后面的 include 覆盖它，于是这行看着像配置、实际是死的（审计 M16）。 */
#include "ui_config_host.h"
