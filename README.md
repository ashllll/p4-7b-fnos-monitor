<p align="center">
  <img src="assets/readme/hero.svg" width="100%" alt="fnOS Monitor — 飞牛 NAS 的桌面状态屏；负载、流量、容量与温度，一眼可见。">
</p>

# fnOS Monitor

**把 NAS 的负载、流量、容量与温度放到桌面上。**

fnOS 配套应用负责采集，ESP32-P4 的 7 英寸触控屏负责显示。抬眼看总览，需要细节时进入存储、网络、系统、温度与告警页。

[快速开始](#快速开始) · [六页界面](#界面预览) · [验证状态](#验证状态) · [常见问题](#常见问题) · [文档导航](#文档导航)

![总览：CPU 与内存仪表、设备状态、网络、存储容量和最高温度](docs/images/ui-cuktech-ultra-2026-10-14/overview.png)

*2026-10-14 版本的真实 LVGL 主机渲染，使用匿名夹具与合成读数。图片保留当时布局；当前版本已加入附着游标和刷新优化。[全部 21 张样张与来源](docs/images/ui-cuktech-ultra-2026-10-14/README.md)。*

## 日常看状态，需要时看细节

- **六页触控界面**：底部导航与横向拖动切页，纵向滚动查看清单；默认黑底蓝青配色，支持夜间模式。
- **随实际设备展开**：卷、阵列、磁盘、网口、容器和温度通道动态创建，长名称与长清单可完整查看。
- **读数有明确状态**：区分实时、旧值、缺失、权限不足与离线；合法零值保留，缺失数据不会显示成健康。
- **在屏幕上完成连接**：配置 Wi-Fi、输入配对码、核对 HTTPS 证书指纹，无需向屏幕提供 NAS 管理员密码。

## 硬件要求

| 部分 | 当前支持 |
| --- | --- |
| 开发板 | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B |
| 显示与触摸 | 7 英寸、1024×600 横屏，MIPI-DSI、EK79007 / GT911 |
| 无线连接 | 板载 ESP32-C6，通过 ESP-Hosted SDIO |
| 固件工具链 | ESP-IDF 5.5.3，`esp32p4`，默认 P4 rev3.x |
| NAS 端 | fnOS、`python312`；安装包声明 `platform=x86` |
| 网络 | 开发板可访问 NAS 遥测端口，默认 HTTPS `8798` |

当前目标仅为上述开发板的 1024×600 横屏。其他分辨率、竖屏不在支持范围；pre-v3 与 rev3.x 固件不能互刷。温度、SMART 和容器数据取决于 NAS 驱动、工具与权限。

## 快速开始

先安装 NAS 应用，再构建屏幕固件，最后在屏幕上配对。

### 1. 安装 NAS 应用

1. 在 fnOS 应用中心安装 `python312`。
2. 下载 [飞牛监控 1.2.7](nas/fpk/nasscreencompanion.fpk)，通过「应用中心 → 手动安装」安装。
3. 保持默认 HTTPS 与配对令牌，确认遥测端口；需要容器状态时启用容器采集。
4. 从飞牛桌面打开「飞牛监控」，在「能力矩阵」查看可用的采集来源。

<details>
<summary>安装包校验与升级说明</summary>

当前安装包为 **104796 字节**，SHA-256：

```text
6feefe9dcbcfc9f4a902b3474188df1978d2693b2fd9ade2d22c84b366395f9d
```

该校验值对应仓库中的文件。安装、升级、TLS、权限与端口排障见 [飞牛应用说明](nas/fpk/README.md)；历史部署结果见 [FPK 性能与 1.2.7 阶段记录](docs/fpk-performance-2026-10-12.md)。本轮文档更新没有重新部署 NAS。

</details>

### 2. 构建并烧录屏幕固件

激活 ESP-IDF 环境，确保 `IDF_PATH`、Python 和 RISC-V 工具链可用：

```bash
git clone https://github.com/ashllll/p4-7b-fnos-monitor.git
cd p4-7b-fnos-monitor
cp components/fnos_monitor/fnos_config.example.h components/fnos_monitor/fnos_config.h
./idf.sh build
```

模板可直接构建，Wi-Fi 和 NAS 连接可稍后在屏幕上配置。真实参数放在被忽略的 `fnos_config.h`，个人固件和 NVS 留在本地。

识别开发板串口，替换示例路径后执行：

```bash
export FNOS_SERIAL_PORT='/dev/cu.YOUR_BOARD_PORT'
./idf.sh -p "$FNOS_SERIAL_PORT" -b 230400 flash
./idf.sh -p "$FNOS_SERIAL_PORT" monitor
```

已有 `sdkconfig` 不会被新默认值自动覆盖；升级前备份配置，并在 menuconfig 的 fnOS monitor 主题项选择 `cuktech-console`。依赖版本见 [dependencies.lock](dependencies.lock)。也可用 `FNOS_IDF_ENV=/path/to/activation.sh ./idf.sh build` 激活本地工具链。

### 3. 连 Wi-Fi，核对指纹，完成配对

1. 在屏幕的 Wi-Fi 面板扫描并连接网络。
2. 在 NAS 管理页「设备配对」生成六位配对码。
3. 在屏幕填写 NAS 地址、应用实际遥测端口及配对码。
4. 对照 NAS 管理页，逐段核对证书 SHA-256 指纹，一致后接受配对。
5. 等待「配对完成」，回到总览检查数据新鲜度和采集来源状态。

应用默认端口是 **8798**。模板中的 `8799` 是旧采集器回退值，保存的配对端点会覆盖模板。配对码过期需重新生成；更换证书需重新确认信任，撤销设备会拒绝其遥测请求。首次明文引导只提供公开证书，不能替代指纹核对。

<details>
<summary>查看首次连接流程图</summary>

![首次连接：准备两端、核对证书指纹、确认配对结果；失败时按网络、证书或保存阶段排查](docs/diagrams/first-connection.svg)

</details>

<details>
<summary>进阶：确认烧录来源与 GitHub 一致</summary>

在构建前核对发布工作区：

```bash
git fetch origin main
git status --short
git rev-parse HEAD origin/main
```

确认工作区无源码改动、两个提交哈希一致，再在同一目录构建与烧录。记录提交哈希和本地配置版本；每个写入区域都应出现 `Hash of data verified.`。

激活 ESP-IDF 环境后查看本次产物信息：

```bash
python -m esptool --chip esp32p4 image_info --version 2 build/fnos_monitor.bin
```

核对启动日志的 `ELF file SHA256`；运行诊断 `FNOS_DIAG` 的 `fw` 字段也提供指纹前缀。不同配置或构建环境可能生成不同固件哈希，因此源码提交与产物指纹应分别记录。Wi-Fi、令牌、NVS 和原始日志保留在本地。

</details>

## 界面预览

总览之后，五个页面分别回答一个问题。以下仍为 2026-10-14 匿名样张；详情窗口保留打开时的快照，便于稳定阅读。

- **存储**：可用容量、存储卷、阵列、磁盘健康与读写。
- **网络**：收发速率、双线趋势和接口状态。
- **系统**：系统摘要、容器运行与硬件状态。
- **温度**：最热设备及展开后的完整温度通道。
- **告警**：按严重程度筛选事件，无告警时查看采集体检。

![存储页：容量摘要、存储卷与阵列健康](docs/images/ui-cuktech-ultra-2026-10-14/storage.png)

<details>
<summary>展开网络、系统、温度与告警页面</summary>

### 网络

![网络页：收发速率、双线趋势与接口状态](docs/images/ui-cuktech-ultra-2026-10-14/network.png)

### 系统

![系统页：容器、硬件温度与系统摘要](docs/images/ui-cuktech-ultra-2026-10-14/system.png)

### 温度

![温度页：最高温、趋势与设备通道](docs/images/ui-cuktech-ultra-2026-10-14/temperature.png)

### 告警

![告警页：按严重程度筛选事件](docs/images/ui-cuktech-ultra-2026-10-14/alerts.png)

</details>

配网键盘、证书确认、详情、诊断和夜间模式见[完整界面图集](docs/images/ui-cuktech-ultra-2026-10-14/README.md)。夜间背光和触摸手感仍需在真实屏幕上体验。

## 两端如何连接

屏幕主动通过 HTTPS 拉取 NAS 遥测。网络任务更新内存快照，LVGL 线程读取并绘制；请求或解析失败时保留最后有效数据，并标明连接与新鲜度状态。

![数据流：NAS 本机采集与可选 Docker 数据，经管理网关和 HTTPS 遥测到达开发板，再由 LVGL 显示](docs/diagrams/runtime-data-flow.svg)

采集来自 Linux 本机接口，当前没有调用飞牛私有遥测 API。管理页通过飞牛应用网关打开，遥测端口供屏幕取数据。

<details>
<summary>采集来源、权限与资源预算</summary>

| 数据 | 来源与条件 |
| --- | --- |
| CPU、负载、内存 | `/proc/stat`、`/proc/loadavg`、`/proc/meminfo` |
| 网络 | `/proc/net/dev`、默认路由；各接口独立计算速率 |
| 存储、阵列、磁盘 IO | `/proc/mounts`、`statvfs`、`/proc/mdstat`、`/proc/diskstats` |
| 硬件温度 | `/sys/class/hwmon/*/temp*_input`；依驱动及可读通道 |
| 容器 | 显式启用后，由受限辅助进程查询固定 Docker GET |
| 可选 SMART | `smartctl -j -H -n standby,3`；依工具、设备、权限与启用选项 |

管理服务以普通包用户运行；生命周期脚本和 Docker 辅助进程使用各自所需权限。磁盘与网口清单可能包含逻辑或虚拟设备，条目数不等于物理设备数。

清单按实际数据分配，预算限制字节资源而非设备数量。超预算或解析失败会明确报错并保留最后有效快照。

| 配置 | 默认值 |
| --- | --- |
| `CONFIG_FNOS_STATUS_MAX_BYTES` | 整帧遥测 262144 B |
| `CONFIG_FNOS_SNAPSHOT_MAX_BYTES` | 单份清单与字符串 1048576 B |
| `CONFIG_FNOS_UI_LIST_MIN_WIDTH` | 首选清单宽度 260 px，按空间和文本决定列数 |
| `CONFIG_FNOS_UI_REDUCED_MOTION` | 关闭；启用后保留跟手，松手直接落定 |
| `CONFIG_FNOS_PALETTE` | `cuktech-console`，另有六个历史预置 |

详见 [遥测字段语义](docs/ui-telemetry-2026-10-12.md)、[硬件自适应验证](docs/ui-hardware-adaptive-audit-2026-10-08.md)与 [Kconfig](components/fnos_monitor/Kconfig)。

</details>

## 验证状态

最近设备验证：**2026-10-21**，源码基线 [`d453cac`](https://github.com/ashllll/p4-7b-fnos-monitor/commit/d453caca76eb9bfc2b7731b0d725dc20d4a7f1ad)，包含当前 UI、附着游标与解析优化。

- **构建与烧录**：ESP-IDF 5.5.3 构建通过；三个写入区域哈希校验通过，启动与运行诊断指纹匹配产物。
- **短时运行**：45 秒观察，轮询成功 150 次、失败 0 次，未发现崩溃或重启循环；Wi-Fi 与配对配置保留。
- **实体屏幕**：总览页已显示；照片存在反光和局部裁切，不能据此确认全部页面与交互。
- **解析回归**：ASan/UBSan、14 个历史场景、11 个合并场景及大帧解析通过；两个生产 C 文件静态分析零诊断。

**仍待验收**：完整物理触控、实屏动画手感、72 小时运行、掉电／旧 NVS 回退、真实 LVGL 内存耗尽恢复，以及更广泛的硬件和权限矩阵。构建、主机渲染和短时运行各有边界，详见[未闭环事项](docs/open-items-2026-10-11.md)。原始日志、回滚备份、个人固件和照片保留在本地。

<details>
<summary>最近迭代与版本边界</summary>

### 最近迭代与版本边界

- **解析优化（2026-10-21）**：快照字符串直接复制、空值复用常量，历史行单次顺序读取。匿名主机样本每帧分配从 1051 次降至 833 次、申请字节从 41926 B 降至 38220 B；2000 次解析 CPU 耗时中位数减少约 32%。这是主机样本测量，不能换算成 P4 实机性能。[条件与回归记录](docs/ponytail-optimization-2026-10-21.md)。
- **当前动效（2026-10-20）**：圆环、条形与曲线使用附着游标，复用 Ryan Juckett Damped Springs。默认响应 600 ms、频率因子 8、临界阻尼比 1.0，数字共享 600 ms 呈现节拍；显示平滑与真实采样历史分离。[当前合同 ADR-0005](docs/adr/0005-library-springs-and-attached-cursors.md) · [阶段实现与验证](docs/ui-ammeter-cursors-2026-10-20.md)。
- **视觉基线（2026-10-14）**：六页及配网、配对、详情统一为 ULTRA 黑底蓝青／朱红配色。[设计与阶段验证](docs/ui-ultra-release-2026-10-14.md)。

历史记录中的“尚未烧录”只指各自阶段，后续结果以上面的设备验证为准。安装包校验与 NAS 部署证据单独记录；历史性能和升级结果不自动扩展到其他构建产物。

</details>

## 开发与验证

主机预览使用同一份 `fnos_ui.c`、`ui_kit`、生成字库和 LVGL，板级接口由测试替身提供：

```bash
./idf.sh build
bash tools/preview/run.sh
python3 tools/stack_check.py
python3 tools/test_history.py
python3 tools/test_history_merge.py
bash tools/parsecheck/run.sh
```

默认预览生成 **126 张状态图 + 5 张刷新回归图**，检查字形、格式残留、溢出、对象越界、触控重叠和对比度，覆盖不同硬件清单及受控分配失败。动效、硬件矩阵和专项回归命令见[预览工具说明](tools/preview/README.md)。

字体 C 文件已生成，可直接构建。改界面文案或字库后，先运行 `bash tools/gen_fonts.sh` 再预览。固件发布使用独立发布目录与明确文件清单，真实配置、日志和实机产物不进入源码仓库。

## 常见问题

- **应用装不上或启动失败**：检查 `python312`、平台兼容性、应用中心日志与端口冲突，见[应用排障](nas/fpk/README.md#装不上起不来的时候)。
- **屏幕连不上 NAS**：核对 NAS 地址、实际遥测端口与监听状态，再按连接、TLS、令牌阶段排查；证书改变或设备被撤销时重新配对。
- **容器、温度或 SMART 缺失**：检查采集开关、能力矩阵、Docker 辅助进程、驱动和权限。未启用或不可读不等于零值。
- **名称缺字、冻结或显示异常**：检查字库、PSRAM、任务栈、IDF 与驱动配置，见[验证说明](docs/verification.md)。

## 文档导航

- **安装与使用**：[飞牛应用](nas/fpk/README.md) · [可选旧采集器](nas/README.md) · [完整界面图集](docs/images/ui-cuktech-ultra-2026-10-14/README.md)
- **设计与架构**：[设计总览](docs/design-overview-2026-10-14.md) · [原生 UI 架构](docs/ui-v12-lvgl-native.md) · [ADR](docs/adr/README.md) · [图表可编辑源文件](docs/diagrams/README.md)
- **验证与改进**：[主机预览](tools/preview/README.md) · [解析优化](docs/ponytail-optimization-2026-10-21.md) · [硬件自适应](docs/ui-hardware-adaptive-audit-2026-10-08.md) · [未闭环事项](docs/open-items-2026-10-11.md)
- **参与开发**：[固件源码](components/fnos_monitor/) · [共享 UI](components/fnos_monitor/ui_kit/) · [术语表](GLOSSARY.md) · [领域约定](docs/agents/domain.md) · [任务入口](docs/agents/issue-tracker.md)
- **回顾历史**：[1.2.4 发布](docs/release-1.2.4.md) · [ULTRA 发布](docs/ui-ultra-release-2026-10-14.md) · [历史 UI](docs/archive/ui/README.md)

## 许可证

项目自有固件、采集器与飞牛应用采用 [Apache License 2.0](LICENSE)。[Damped Springs](components/fnos_monitor/ui_kit/third_party/README.md) 保留 Ryan Juckett 的 zlib-style 许可；LVGL / cJSON 为 MIT；Inter、Noto Sans SC、Chakra Petch 字体为 SIL OFL，相关许可保留在 [fonts/](components/fnos_monitor/fonts/)。
