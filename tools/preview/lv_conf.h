/**
 * 主机预览用的空 lv_conf.h —— 只为满足 LVGL 桌面版 CMake 的存在性检查。
 *
 * 真正的配置不在这里：run.sh 会把设备端 build/config/sdkconfig.h 里的 CONFIG_LV_* 抽成
 * lv_kconfig_host.h 并用 -include 强制包含，LVGL 的 lv_conf_kconfig.h 见到
 * CONFIG_LV_CONF_SKIP=1 就定义 LV_CONF_SKIP，于是整份配置 100% 来自设备 Kconfig。
 * 本文件故意一个宏都不定义（若被包含也不会改变任何默认值）。
 */
#ifndef LV_CONF_H
#define LV_CONF_H
#endif
