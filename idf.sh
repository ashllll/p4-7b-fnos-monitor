#!/usr/bin/env bash
# 注意：必须用 bash —— use-esp-idf-5.5.3.sh 里定义的是 `idf.py()` 函数，
# POSIX sh 不接受带点的函数名。
# 本项目专用 idf.py 包装。
#
# 为什么需要它：
#   本机的 ESP-IDF 5.5.3 来自 PlatformIO 包（framework-espidf@3.50503.0），只通过
#   /Users/llll/code/esp/use-esp-idf-5.5.3.sh 设置 IDF_PATH/PATH，没有走 IDF 官方的
#   export.sh，因此环境变量 ESP_IDF_VERSION 缺失。
#   而 esp_wifi_remote 的 Kconfig 里是 `orsource "./Kconfig.idf_v$ESP_IDF_VERSION.in"`：
#   变量为空时该文件被静默跳过，于是依赖它的 SLAVE_IDF_TARGET 选项不存在，
#   ESP-Hosted 便退回默认的 SPI 传输——本板 C6 走的是 SDIO，Wi-Fi 会起不来。
#   IDF 官方 get_idf_version() 导出的是 major.minor（如 "5.5"），这里照此补上。
#
# 用法：
#   ./idf.sh set-target esp32p4
#   ./idf.sh build
#   ./idf.sh -p /dev/tty.usbmodemXXXX flash monitor
set -e

. /Users/llll/code/esp/use-esp-idf-5.5.3.sh

export ESP_IDF_VERSION="${ESP_IDF_VERSION:-5.5}"

idf.py "$@"
