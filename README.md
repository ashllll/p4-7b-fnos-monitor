# p4-7b-fnos-monitor · 飞牛 NAS 状态屏

把 **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B** 做成独立 NAS 监控屏：开发板运行 C / LVGL 9.5 原生界面，飞牛配套应用采集本机状态，两端通过 HTTPS 和设备配对连接。无需将 NAS 管理员账号或密码写入固件。

硬盘、存储卷、网口、容器和温度通道按实际数据创建卡片；名称换行、列数随可用空间调整，长清单通过滚动查看。采集失败、旧值、权限不足和未启用来源有各自的状态提示。

**历史实机验收记录以 `8cca07c` 固件和 FPK 1.2.7 为基线。** 当时已校验板上镜像并检查六页实拍；安装包核对了配置和 TLS 证书保留、包用户身份、容器资源与温度历史。连接复用修复的短时观察未见失败，范围见 [FPK 性能验证](docs/fpk-performance-2026-10-12.md)。2026-10-13 以 `d282c6a` 为基线完成的 [架构重构](docs/architecture-refactor-2026-10-13.md) 当时未部署或烧录。2026-10-14 的 CUKTECH 界面、连续阻尼与数据可信度修复已完成构建、烧录校验及短时串口观察，见 [最新 UI 验证](docs/ui-post-motion-audit-2026-10-14.md)；本轮未重新部署 NAS，长期运行与完整物理触控继续开放。历史审计限制见 [未闭环事项](docs/open-items-2026-10-11.md)。

| 范围 | 当前状态 |
| --- | --- |
| 本地候选源码 | 总览、存储、网络、系统、温度、独立告警六页，配对 / 配网 / 诊断覆盖层 |
| 架构重构基线 | `d282c6a`；后续源码提交见仓库 main 历史，设备验收单独记录 |
| 界面与动效固件基线 | `30abfd0`（2026-10-14）；CUKTECH 横屏仪表与连续阻尼，验证及未验收边界见最新 UI 记录 |
| 飞牛安装包 | [1.2.7](nas/fpk/nasscreencompanion.fpk)，真实 fnOS 升级及针对性运行检查通过；长期运行仍待验收 |
| 主机验证 | 固件构建、原生预览、匿名 NAS 生命周期及针对性故障负控；范围见未闭环事项 |
| 尚未闭环 | 真实 LVGL 内存耗尽恢复、旧 NVS 混代与回退／掉电、设备信任与异常权限边界、完整浏览器视觉、容量目标／30% 性能／72 小时长测 |
| 固件分发 | 源码与配置模板；个人实机 `.bin`、NVS 和凭据不随仓库分发 |

## 界面与监控内容

默认 `cuktech` 主题采用近黑背景、仪表数字和黄色交互焦点；当前只迭代 ESP32-P4 7B 的 **1024×600 横屏**，全宽内容配底部导航。首页六张多功能卡承载 CPU、内存、设备状态、网络读数、存储和温度；网速曲线只保留在网络页。存储、系统和告警支持分类筛选，设备行可打开只读详情。页面与配对／配网／诊断覆盖层复用同一套 `fnos_ui.c` / `ui_kit`；主机预览编译真实 LVGL 源码。

当前设计见 [CUKTECH 界面合同](docs/ui-cuktech-2026-10-14.md)，450 ms 临界阻尼、连续重定向和状态即时发布见 [动效记录](docs/ui-ammeter-motion-2026-10-14.md)，决策与取舍见 [ADR 索引](docs/adr/README.md)。原 [UniFi 全界面迭代](docs/ui-unifi-v11-2026-10-12.md) 保留为历史记录。

新增网口链路占用、卷剩余容量、内存余量、采集 P95、SMART 与磁盘延迟、容器资源与重启次数，以及最高温历史和详情入口。字段兼容性、缺失状态与采集条件见 [信息增强说明](docs/ui-telemetry-2026-10-12.md)。固件已烧录并检查画面，新增采集实现已在真实 NAS 的 1.2.7 中运行，容器 CPU、内存、重启次数及温度历史已读取验证。SMART 在当前权限下返回拒绝或不支持，尚未取得成功健康读数。

| 页面 | 主要内容 |
| --- | --- |
| 总览 | 六张状态卡：CPU / 内存环表与趋势、运行状态、网络数值、存储卷比较及最高温 |
| 存储 | 容量摘要、动态卷卡片、阵列状态与同步进度、磁盘读写活动 |
| 网络 | 收发速率、双线历史、累计流量、采集状态及各网络接口清单 |
| 系统 | 系统摘要、来源状态、容器服务及硬件温度摘要 |
| 温度 | 按设备身份分组的温度卡片；手动展开多通道，不自动跳切 |
| 告警 | 严重度与事件清单；健康态展示存储、温度、容器和采集维度 |
| 配对 / 配网 / 诊断 | NAS 地址与证书确认、Wi-Fi 扫描与键盘、连接阶段和失败原因 |

点导航显示对应页面；横向拖动跟随手指，短滑回弹，纵向操作交给清单滚动。夜间背光与温度、容量参考阈值可配置。颜色参考值不能替代各硬件厂商的健康阈值。

以下为历史 1.2.4 源码的匿名原生预览，不是当前候选的重新验收，**不是物理屏幕照片，也不包含个人 NAS 清单**：

![总览原生 LVGL 预览](docs/evidence/published-1.2.4/02-live-p0.png)

[存储](docs/evidence/published-1.2.4/02-live-p1.png) · [网络](docs/evidence/published-1.2.4/02-live-p2.png) · [系统](docs/evidence/published-1.2.4/02-live-p3.png) · [温度](docs/evidence/published-1.2.4/02-live-p4.png) · [配对](docs/evidence/published-1.2.4/07-pair-code.png)

## 两端如何连接

```mermaid
flowchart LR
    A["NAS 本机 /proc、/sys、statvfs"] --> B["飞牛监控 1.2.7：包用户采集与管理服务"]
    D["可选 Docker 只读辅助进程"] --> B
    B -->|HTTPS 遥测，默认端口 8798| C["ESP32-P4 / LVGL 状态屏"]
    E["飞牛桌面管理页"] -->|设置、能力矩阵、配对、诊断| B
```

管理页通过飞牛应用网关打开；遥测端口供屏幕取数据，两者分别工作。采集使用 Linux 本机来源，当前实现没有调用飞牛私有遥测 API。具体可读能力取决于本机驱动、挂载、权限及启用选项，以管理页「能力矩阵」为准。

| 数据 | 主要来源 |
| --- | --- |
| CPU、负载、内存 | `/proc/stat`、`/proc/loadavg`、`/proc/meminfo` |
| 网络接口与速率 | `/proc/net/dev`、默认路由；各接口按自己的计数器计算 |
| 存储卷、阵列、磁盘 IO | `/proc/mounts` + `statvfs`、`/proc/mdstat`、`/proc/diskstats` |
| 温度 | `/sys/class/hwmon/*/temp*_input`；只有实际可读通道才有测量值 |
| 容器服务 | 显式启用后，由受限辅助进程查询固定 Docker GET |
| 可选 SMART | 本机 `smartctl -j -H -n standby,3` 健康读取；受工具、设备与包用户权限限制 |

磁盘 IO 清单可能包含 md / dm 等逻辑设备；网络清单可能包含虚拟接口，不能将条目数视为物理硬盘或网口数量。SMART 为可选来源，工具缺失、权限不足、超时及设备不支持应明确显示状态；不承诺所有 SATA 温度或厂商专用传感器可用。

## 快速开始

### 1. 安装飞牛配套应用

以下是应用安装流程。**当前 1.2.7 包已通过主机回归与真实 NAS 升级、包用户身份和针对性遥测检查；完整触控与 72 小时运行仍待验收。**

1. 在 fnOS 应用中心安装依赖 `python312`。
2. 下载仓库内 [nasscreencompanion.fpk](nas/fpk/nasscreencompanion.fpk)，在「应用中心 → 手动安装」上传。
3. 保持默认 HTTPS、配对令牌和遥测端口 `8798`；按需启用容器采集。
4. 从飞牛桌面打开「飞牛监控」，检查「能力矩阵」及「诊断」。

管理服务的**进程身份是普通包用户**：`config/privilege` 声明的是 `run-as: root`（生命周期脚本要写 `/var/apps`、读 root 态 `docker.sock`），`cmd/main` 在 root 下启动 `cmd/docker_launch.py`，后者在 exec 管理服务**之前** `setgroups([])/setgid()/setuid()` 永久降权到包用户；独立 root 辅助进程仅提供固定的容器状态读取。应用无需 SSH 部署，也不会接管旧 `fnos-agent` 的 systemd 服务。安装、升级、证书与权限排障见 [飞牛应用说明](nas/fpk/README.md)。

当前 1.2.7 包 SHA-256：

```text
e727f32cb50ac1504380a5a6ffe30fe63bf865fc071933b997447d1b0a15f079
```

文件为 104759 字节；使用前应核对实际下载文件的大小与哈希。性能测量、包内源码核对和验收范围见 [FPK 验证记录](docs/fpk-performance-2026-10-12.md)。

### 2. 构建并烧录开发板

| 项目 | 已验证配置 |
| --- | --- |
| 开发板 | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B |
| 显示 / 触摸 | 7 英寸，1024×600 MIPI-DSI，EK79007 / GT911 |
| Wi-Fi | 板载 ESP32-C6，通过 ESP-Hosted SDIO |
| 工具链 / 目标 | ESP-IDF 5.5.3 / `esp32p4` |
| 硅片配置 | 默认 P4 rev3.x；pre-v3 与 rev3.x 固件不能互刷 |

以下克隆命令取得执行时的远端版本。先按自己的 ESP-IDF 安装方式激活环境，使 `IDF_PATH` 和所需 Python、RISC-V 工具链可用。例如使用 ESP-IDF 的 `export.sh`，或 EIM 提供的 activation script。

```bash
git clone https://github.com/ashllll/p4-7b-fnos-monitor.git
cd p4-7b-fnos-monitor
cp components/fnos_monitor/fnos_config.example.h components/fnos_monitor/fnos_config.h
./idf.sh build
```

模板可以直接构建；真实 Wi-Fi、NAS 地址及令牌可留在被忽略的 `fnos_config.h`，也可在屏幕上配置。`idf.sh` 支持 `FNOS_IDF_ENV=/path/to/activation.sh ./idf.sh build`，并自动设置 IDF major.minor 版本变量。模板的 `FNOS_PORT=8799` 是旧采集器回退值；使用飞牛应用时，在屏幕配对表单填写应用实际端口（默认 `8798`），保存后的运行时端点覆盖模板值。

识别自己的开发板串口，替换下面的示例路径。CH343 经 USB Hub 时，已验证的 230400 波特率可作为起点：

```bash
export FNOS_SERIAL_PORT='/dev/cu.YOUR_BOARD_PORT'
./idf.sh -p "$FNOS_SERIAL_PORT" -b 230400 flash
./idf.sh -p "$FNOS_SERIAL_PORT" monitor
```

依赖版本记录在 [dependencies.lock](dependencies.lock)。`sdkconfig.defaults` 是初始默认值；修改后已有 `sdkconfig` 不会自动覆盖，请先备份本地设置，再重新生成。本板的显示缓冲、LVGL 内存与任务栈使用 PSRAM，TLS 缓冲也配置为外部内存；移植时要保留相关配置。

### 3. 连 Wi-Fi 并配对

1. 在开发板 Wi-Fi 面板扫描并连接自己的无线网络；凭据保存到 NVS。
2. 在 NAS 管理页「设备配对」生成六位配对码。
3. 在开发板配对面板填写 NAS 地址、遥测端口及配对码。
4. 将开发板计算的证书 SHA-256 指纹与 NAS 管理页显示的指纹逐段比对，一致后确认配对。
5. 配对完成后查看总览的数据新鲜度，并检查需要的来源状态。

开发板保存地址、令牌和固定证书；NAS 只保存配对令牌的哈希。换证书需要重新确认信任；撤销设备后，该设备的后续遥测请求被拒绝。首次明文引导仅提供公开证书，不代替人工指纹确认。

## 硬件自适应与资源边界

1.2.4 采集器默认完整传送卷、阵列、磁盘、温度、容器及非 loopback 网络接口清单，名称、路径和通道身份不按固定长度截断。各接口独立测速，处理热插拔和计数器复位。若调用者显式配置数量策略，响应仍包含总数与遗漏统计。

固件的接收缓冲、动态快照和界面卡片按实际数据分配。清单增长、减少、空清单、多通道与长名称走同一布局逻辑，默认不轮播页面或温度分组。当前在 1024×600 横屏上验证不同硬件组合；早期多尺寸矩阵仅为历史证据，不继续开展窄屏或竖屏迭代。

| 配置 | 默认 | 含义 |
| --- | ---: | --- |
| `CONFIG_FNOS_STATUS_MAX_BYTES` | 262144 B | 遥测整帧字节预算 |
| `CONFIG_FNOS_SNAPSHOT_MAX_BYTES` | 1048576 B | 单份硬件清单与字符串存储预算 |
| `CONFIG_FNOS_UI_LIST_MIN_WIDTH` | 260 px | 首选清单宽度，实际列数由可用空间和文本推导 |
| `CONFIG_FNOS_UI_REDUCED_MOTION` | 关闭 | 开启后保留跟手，松手直接落定 |
| `CONFIG_FNOS_PALETTE` | `cuktech` | 也可选 `unifi`、`device`、`graphite`、`abyss`、`phosphor`；已有 sdkconfig 需同步更新 |

预算限制字节资源，**不是固定设备数量上限**。超预算或解析失败会保留最后有效快照并报错；部分受控分配失败会提示设备尚未显示；但当前真实 LVGL 内存分配失败先触发启用的断言并停留，调用者收到 NULL 时的回退测试不能证明真实内存耗尽可恢复（见[未闭环事项](docs/open-items-2026-10-11.md)）。旧来源、权限不足与真正零值不能混为一谈。完整实现和矩阵范围见 [硬件自适应记录](docs/ui-hardware-adaptive-audit-2026-10-08.md)。

## 开发与验证

以下命令都从仓库根目录执行；设备命令会操作指定开发板，请按当前任务选择阶段。

```bash
# 首次先构建固件，生成真实 LVGL 配置与依赖。
./idf.sh build
# 原生界面预览与布局审计；默认输出 tools/preview/out/。
bash tools/preview/run.sh
# ELF 静态栈预算；不代替运行期测量。
python3 tools/stack_check.py
# 生产历史合并、图表数值边界与故障负控；匿名主机检查。
python3 tools/test_history_merge.py

# 采集、容器 IPC、权限迁移与配置保留回归。
python3 nas/fpk/collector_check.py
python3 nas/fpk/docker_check.py
python3 nas/fpk/startup_permission_check.py
python3 nas/fpk/config_persistence_check.py
python3 nas/fpk/temps_check.py
python3 nas/fpk/test_telemetry_publication.py
python3 tools/test_http_connection.py
python3 nas/fpk/benchmark_telemetry.py
# 匿名大清单持续发布与真实 HTTP 内存检查；不访问 NAS。
python3 nas/fpk/mem_check.py 3000 --entities 1024 --publish-every 10
# 打包需要 fnpack；脚本清理缓存后检查包内文件。
bash nas/fpk/build.sh
# 在匿名临时布局中走安装、启动、设置、升级和卸载。
bash nas/fpk/test_lifecycle.sh
```

主机预览、硬件组合、首次配对、动效响应与分配失败命令见 [预览说明](tools/preview/README.md)。已有预览构建后，`bash tools/verify_all.sh` 汇总布局、固件构建和栈检查；`--flash --port "$FNOS_SERIAL_PORT"` 增加烧录及串口观察，`--photo` 调用本机 `capture-board`。

当前开发版页面编号为 0–5，可用 `python3 tools/page_shot.py --port "$FNOS_SERIAL_PORT" --page 4 --temp 0` 查看第一个多通道温度设备的展开态。拍屏需要 pyserial、ADB 授权的 Android 手机、前台相机及可用的 `capture-board`。打开或关闭串口可能使板子复位，图片必须实际查看后才能用于验收。

字体 C 文件已生成，可直接构建。改文案或字库时使用 `bash tools/gen_fonts.sh`；源字体要求、可扩展字符范围与未压缩格式说明见 [预览说明](tools/preview/README.md#边界与坑)。字库不是任意 Unicode 的完整覆盖，缺字要扩展字体范围并重新渲染检查。

### 最近迭代与版本边界

2026-10-08 的公开基线补齐动态硬件清单、完整名称、多网口测速、快照所有权及失败状态，同时发布飞牛应用 1.2.4。发布检查覆盖模板构建、原生预览、采集契约、Docker IPC、配置保留和生命周期，结果见 [1.2.4 发布记录](docs/release-1.2.4.md)。随后在一台真实 fnOS NAS 上完成升级与服务、容器采集检查，保留现有设置和 TLS 证书，并查看总览、网络、系统实机画面。这个结果不代表穷举所有 NAS 硬件。

此前开发仓的 `86de188`、`591e54a`、`a98c296`、`70b6527`、`cc2da56` 提交包含：

- 六页底部导航：总览、存储、网络、系统、温度、独立告警；告警健康态提供监控维度摘要。
- 首页与网络页按内容高度重新分配空间，清单保留滚动入口，避免页面底部裁掉卡片。
- 温度展开串口取证 `temp N`、`page_shot.py --temp N`，以及屏上未消费格式说明符审计。

上述提交与 84 张预览、十分钟观察是历史记录。当前 main 已包含六页、新取证参数及独立复核修复。固件构建、89 帧布局审计与 NAS 主机证据各有范围，均不能代替实际设备、浏览器视觉或长期运行验收；逐项残余见[未闭环事项](docs/open-items-2026-10-11.md)。

2026-10-12 已合入[触控调度优化](docs/perf-touch-2026-10-12.md)和[整机交互与动画优化](docs/perf-system-motion-2026-10-12.md)：新触控样本在同次调度交给手势控制器，清单布局按实际几何变化失效，页面过渡期间数字动画让出绘制资源。
同一台实机台架中，存储 / 系统页刷新耗时中位数下降约 83% / 81%，点击存储页松手到渲染就绪下降约 65%；连续滑动帧间隔没有一致改善，完整物理触控延迟与长期稳定性仍待验证。

## 常见问题

| 现象 | 先检查 |
| --- | --- |
| 应用无法启用或更新 | 应用中心日志、`python312`、端口冲突；日志与私有权限迁移见[应用排障](nas/fpk/README.md#装不上起不来的时候) |
| 容器清单没有数据 | 容器开关、能力矩阵、Docker Socket 和辅助进程状态；`disabled` / `denied` 不等于零容器 |
| 开发板连不上 NAS | 两端网络、地址与端口、监听是否仅本机；区分连接失败、TLS 失败和令牌拒绝 |
| 指纹改变或令牌被撤销 | 核对 NAS 当前证书，再按配对流程重新授权 |
| 清单遗漏或只见默认网口 | 确认采集器版本，查看遗漏元数据及字节预算；固件不能补回旧来源未传的数据 |
| 温度缺失 | 检查驱动、hwmon 节点及权限；SMART 另需工具和读取权限，缺失不代表 0°C |
| 屏幕冻结、文字破碎、Wi-Fi 起不来 | 检查 PSRAM/TLS、未压缩字库及 IDF 版本变量 / SDIO；见[验证记录](docs/verification.md) |

## 目录与文档

| 路径 | 职责 |
| --- | --- |
| [components/fnos_monitor/](components/fnos_monitor/) | UI、网络、配对、轮询、动态快照及字库 |
| [components/fnos_monitor/ui_kit/](components/fnos_monitor/ui_kit/) | 原生控件、共享布局、主题与动效令牌 |
| [components/fnos_monitor/Kconfig](components/fnos_monitor/Kconfig) | 内存预算、图表窗口、主题、参考阈值与调试设置 |
| [nas/fpk/](nas/fpk/README.md) | 飞牛应用、管理页、包构建及生命周期 |
| [nas/](nas/README.md) | 可选 Linux/systemd 旧采集器，默认 HTTP 8799，与 FPK 分开部署 |
| [tools/preview/](tools/preview/README.md) | 真实 LVGL 主机预览与交互 / 硬件矩阵 |
| [docs/ui-cuktech-2026-10-14.md](docs/ui-cuktech-2026-10-14.md) | 当前横屏仪表界面合同 |
| [docs/ui-ammeter-motion-2026-10-14.md](docs/ui-ammeter-motion-2026-10-14.md) | 连续阻尼参数、实现与阶段验证 |
| [docs/ui-post-motion-audit-2026-10-14.md](docs/ui-post-motion-audit-2026-10-14.md) | 最新缺陷回归、烧录及验收边界 |
| [docs/adr/](docs/adr/README.md) | 架构决策、取舍和编号索引 |
| [GLOSSARY.md](GLOSSARY.md) | 采集状态、读数与健康结论的领域术语 |
| [docs/agents/](docs/agents/domain.md) | skills 的领域文档、任务入口与分诊约定 |
| [docs/ui-device-reference.md](docs/ui-device-reference.md) | 原生跟手动效的历史参考 |
| [docs/ui-v12-lvgl-native.md](docs/ui-v12-lvgl-native.md) | 原生 UI 架构及迁移记录 |
| [docs/verification.md](docs/verification.md) | 验证入口与证据边界 |
| [docs/open-items-2026-10-11.md](docs/open-items-2026-10-11.md) | 当前验证结果、原审计编号去向与待验收事项 |
| [docs/fnos-market-submission.md](docs/fnos-market-submission.md) | 官方应用库提交准备与待完成项 |
| [docs/archive/ui/](docs/archive/ui/README.md) | 旧版设计与 HTML 原型，仅作历史参考 |

开发与发布目录分离；只同步经过审查的源码、模板、匿名测试与文档，保留已发布历史。真实配置、私钥、配对令牌、实机固件、原始日志、个人网络信息与物理相机截图保留在本地。

## 许可证

本项目固件、采集器与飞牛应用适用 [Apache License 2.0](LICENSE)。第三方组件及字体保留各自许可：LVGL / cJSON 为 MIT；Inter、Noto Sans SC、Chakra Petch 字体为 SIL OFL。字体许可随生成文件保留在 [fonts/](components/fnos_monitor/fonts/)。
