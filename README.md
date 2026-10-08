# p4-7b-fnos-monitor

Waveshare ESP32-P4-WIFI6-Touch-LCD-7B 的飞牛 NAS 状态屏固件及配套应用。
界面使用 C / LVGL 9.5 与原生 `ui_kit`，提供总览、存储、网络、系统、温度五页，
以及配对、配网、诊断面板。总览包含环表、趋势和存储卷比较。

硬件清单按实际数据创建卡片，列数随可用宽度和文字尺寸调整，长名称换行，
清单增减后重排并可滚动访问全部条目。设备数量、型号及名称长度不作为显示上限。
来源的离线、旧值、权限不足和读取失败分别显示。

## 配套飞牛应用

[`nas/fpk/nasscreencompanion.fpk`](nas/fpk/nasscreencompanion.fpk) 为 **1.2.4** 安装包。
在飞牛应用中心手动安装，依赖 `python312`，当前面向 x86；尚未提交官方应用库。
安装、配置、配对和排障见 [应用说明](nas/fpk/README.md)。

应用读取本机 `/proc`、`/sys`、存储容量及可选 Docker 状态，提供默认 HTTPS 的只读遥测接口。
管理服务使用包用户；独立辅助进程仅查询固定 Docker GET，容器采集需要用户开启。
默认完整传送卷、阵列、磁盘、温度、容器和网口清单，网络接口分别计算速率。
固件以可配置字节预算控制接收与快照内存，超预算保留最后有效数据并提示失败。

首次使用：在屏幕连接 Wi-Fi，在应用管理页生成配对码，在屏幕输入 NAS 地址及配对码，
比对两端的证书指纹后确认。地址、令牌和证书保存在开发板 NVS；本仓库不包含个人配置。
旧应用未提供的网口或已裁剪的条目无法由固件补回，要使用完整清单需升级应用。

`nas/fnos-agent.py` 是可选的独立 Linux/systemd 采集器，部署说明见 [nas/README.md](nas/README.md)。

## 构建固件

已验证工具链：ESP-IDF **5.5.3**，目标 `esp32p4`；本项目默认面向 **P4 rev3.x**。
确认自己的硅片版本后选用相符配置，pre-v3 与 rev3.x 固件不能互刷。
显示配置为 1024×600 MIPI-DSI / EK79007 / GT911，Wi-Fi 经板载 C6 的 SDIO。

先按所用 ESP-IDF 安装方式激活环境，使 `IDF_PATH` 和工具链可用：

```bash
# 官方 ESP-IDF 5.x 安装通常使用此入口；使用 EIM 时改用对应 activation script。
. "$IDF_PATH/export.sh"
git clone https://github.com/ashllll/p4-7b-fnos-monitor.git
cd p4-7b-fnos-monitor
cp components/fnos_monitor/fnos_config.example.h components/fnos_monitor/fnos_config.h
./idf.sh build
```

模板可直接用于无个人凭据构建；Wi-Fi 与 NAS 可在屏幕上配置，也可修改被忽略的本地
`fnos_config.h`。令牌来自采集端设置或应用配对，不需要将 NAS 管理员密码写入固件。
`idf.sh` 也支持 `FNOS_IDF_ENV=/path/to/activation.sh ./idf.sh build`，并自动导出 IDF
major.minor 版本，避免 `esp_wifi_remote` 缺少版本变量而退回 SPI。
依赖版本和哈希记录在 `dependencies.lock`；生成的 `sdkconfig` 与构建产物不入库。

识别自己的串口后烧录，CH343 经 USB Hub 时建议从 230400 波特率开始：

```bash
./idf.sh -p <serial-port> -b 230400 flash
./idf.sh -p <serial-port> monitor
```

改 `sdkconfig.defaults` 后已有 `sdkconfig` 不会自动覆盖，应先备份本地配置再重新生成。
TLS 缓冲、LVGL 对象与任务栈使用 PSRAM。生成字体使用未压缩格式，避免并行绘制
共用 RLE 解码状态；中文动态名称默认覆盖基本汉字及可配置的扩展字符区间。

## 源码与验证

| 路径 | 内容 |
|---|---|
| `components/fnos_monitor/fnos_ui.c`、`ui_kit/` | 原生布局、控件、导航与动效 |
| `fnos_data.c`、`fnos_snapshot.c` | 轮询、动态快照、所有权及历史 |
| `fnos_pair.c`、`fnos_wifi_store.c` | 证书固定配对、运行时网络设置 |
| `nas/fpk/nasscreencompanion/` | 飞牛应用源码、生命周期与管理页 |
| `tools/preview/` | 原生 LVGL 预览、交互、硬件矩阵及内存失败回归 |

```bash
# 固件构建后生成匿名原生预览和布局审计。
bash tools/preview/run.sh
python3 nas/fpk/collector_check.py
python3 nas/fpk/docker_check.py
python3 nas/fpk/startup_permission_check.py
python3 nas/fpk/config_persistence_check.py
# 从干净应用源码打包，并验证包中没有缓存。
bash nas/fpk/build.sh
# 匿名临时目录中的安装、启动、配置、升级、卸载回归。
bash nas/fpk/test_lifecycle.sh
```

完整矩阵命令见 [预览说明](tools/preview/README.md)。生成字体需要在 `tools/fonts/` 放置
Inter Regular / Medium / SemiBold 与 Noto Sans SC Regular / Medium 字体；Chakra Petch
由脚本下载，许可随生成子集保留。已生成的字体 C 文件可直接构建。

[验证索引](docs/verification.md) 区分主机渲染、固件构建、物理屏幕与 NAS 安装证据。
[硬件自适应记录](docs/ui-hardware-adaptive-audit-2026-10-08.md) 记录已测尺寸、清单与失败路径；
[v12 实现](docs/ui-v12-lvgl-native.md) 说明原生界面结构。
旧版 UI 设计与 HTML 原型位于 `docs/archive/ui/`。
个人凭据、实机固件、日志、摄像头截图及真实接口响应保存在开发目录，发布副本只同步审查过的文件。
