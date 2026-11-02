# 模块边界与依赖梳理（2026-11-02）

配套交互图：[`docs/diagrams/architecture-fnos-monitor.html`](diagrams/architecture-fnos-monitor.html)

本文回答一个问题：**这个仓库里哪些代码属于哪一层、谁依赖谁、哪些边界是编译器强制的**。它不复述
`README.md` 已有的「架构与数据流」（执行体表、遥测接口清单、一次请求的 4 步链路），只补那一节没有
编码的部分：模块依赖边、双构建目标的源码清单差异、编译期开关面、以及几条"改了就炸"的红线。

数据来源：基线提交 `9d80d49fbb2b417df57b3901a3c8979e2ca83e7e` 的源码、头文件契约与
`CMakeLists.txt` 源码列表；codegraph 索引用于导航，索引规模不等同于手写源码规模。

## 1. 规模事实

| 项 | 数值 | 说明 |
|---|---|---|
| 非字体 C/C++ 源文件 | **14,006 行** | 已跟踪的 `components/fnos_monitor` + `main` 下 `.c` / `.cpp`，排除 `fonts/`；包含图标资源 |
| 生成字体 | 占绝对多数行数 | `ui_font_cjk_20.c` 341,722 行等，由 `tools/gen_fonts.sh` 生成，**禁止手改** |
| Kconfig 开关 | 32 个 | 全部在 `components/fnos_monitor/Kconfig` |
| `tools/` 顶层 Python 文件 | 42 个 `.py` | 包含检查器与测试；完整主机检查入口是 `tools/host_checks.sh` |
| UI 页面 | 6 页 | `FNOS_UI_PAGE_COUNT`，`_Static_assert` 锁死导航条与页数一致 |

最大的手写文件是 `components/fnos_monitor/fnos_ui.c`（6,698 行），集中承担界面建树、页面控制和读数呈现。
第 3 节列出它依赖的主要接口。

## 2. 三层边界

仓库里跑的是**三种不同的执行环境**，它们的失败代价互不相通，所以边界必须由构建系统强制，不能靠约定。

| 层 | 代码位置 | 职责 | 不能做什么 |
|---|---|---|---|
| **固件层** | `components/fnos_monitor/{fnos_net,fnos_data,fnos_pair,fnos_wifi_store,fnos_wifi_cli}.c` + `main/main.cpp` | 联网、配对、轮询、解析、快照 | 不画界面；不在回调里调同步 RPC |
| **共享 UI 与快照层** | `fnos_ui.c`、`ui_kit/*.c`、`assets/michka_host_status/`、`fnos_snapshot.c`、`fonts/` | LVGL 绘制与快照解析；主机构建使用平台替身 | 遥测经 `fnos_data_get` / 历史接口读取，网络状态读缓存；配网与配对请求交后台任务 |
| **主机工装层** | `tools/preview/*`、`tools/*.py`、`tools/*.sh` | PC 上渲染同一份 UI 并做四类几何审计 | 不参与固件产物；不是实机验收的替代品 |

NAS 侧（`nas/fpk/nasscreencompanion/app/server/`）是**第四个执行环境**（python312），但它不在固件构建里，
只通过 TCP 8798 的 TLS 遥测面与板子耦合。

这条分层的可执行证据是第 5 节的源码清单：平台无关层能编进 PC 程序，固件层编不进去。

## 3. 模块依赖边

`fnos_ui.c` 依赖以下项目接口；这里列的是包含关系，不是函数调用图的入度排名：

```
fnos_ui.c
├── fnos_ui.h          页数/页面切换/几何 dump
├── fnos_config.h      sdkconfig 宿主（真实文件留本地，仓库只有 .example.h）
├── fnos_data.h        取数与历史环
├── fnos_fonts.h       字体声明
├── fnos_net.h         Wi-Fi 状态与提交
├── fnos_pair.h        配对状态机
├── fnos_perf.h        性能探针（关闭时空实现）
├── fnos_wifi_store.h  配网凭据
├── ui_kit/uk.h        设计系统构件
├── ui_kit/uk_spring.h 弹簧动效
└── assets/michka_host_status/michka_host_status.h 图标资源
```

网络层与数据层存在以下双向接口依赖：

```
fnos_net.c ──include──► fnos_data.h   （连接状态一变就调 fnos_data_network_changed()）
fnos_data.c ──include──► fnos_net.h   （轮询前问 fnos_net_online()，并驱动 service/poll_rssi）
```

数据层读取 `fnos_net_online()`，并在轮询任务中调用 `fnos_net_service()` 与 `fnos_net_poll_rssi()`；
网络层通过 `fnos_data_network_changed()` 通知数据层重建会话，两者不共享可写状态结构。
`fnos_data.c` 里 `fnos_net_service()` 的调用点（`components/fnos_monitor/fnos_data.c:564`）还带着注释
"同步 RPC 只能在普通任务里发"，这是第 8 节红线的落地位置。

其他主要接口关系：

| 模块 | 依赖 | 对外职责 |
|---|---|---|
| `fnos_net.c` | `net.h` `config.h` `data.h` `wifi_store.h` | Wi-Fi STA + SNTP；**同步 RPC**，见第 8 节红线 |
| `fnos_pair.c` | `pair.h` `config.h` | TOFU 证书指纹状态机 |
| `fnos_snapshot.c` | `data.h` | 帧解析、引用计数快照、字节预算 |
| `fnos_wifi_cli.c` | `net.h` `pair.h` `perf.h` `wifi_store.h` `ui.h` `data.h` | 串口命令面；由 `fnos_net.c:337` 拉起 |
| `fnos_perf.c` | `perf.h` `ui.h` | P1/P2 计时探针 |
| `fnos_wifi_store.c` | `wifi_store.h` `config.h` | NVS 凭据 |
| `ui_kit/uk*.c` | `uk.h`（`uk_theme.c` 另加 `uk_theme.h`） | 设计系统构件库 |
| `main/main.cpp` | 全部 | 启动编排 |

`fnos_wifi_cli.c` 的启动入口是 `fnos_net.c:337` 的
`fnos_wifi_cli_start()`，注释写着"串口配网兜底：屏幕配网不可用时的唯一入口"。也就是说触屏配网失败时，
这个模块是恢复通道，不能当可选件删。它的逻辑也不应被别的模块复用（命令解析与打印只服务串口）。

## 4. 执行体与栈预算

栈是本项目被烧过两次的地方（症状 `Stack protection fault` + 崩溃 PC 落在堆分配里，设备几秒一重启），
而主机预览的栈是 MB 级，**永远测不出来**。所以下表是契约，不是参考值。

| 任务 | 栈 | 创建位置 | 备注 |
|---|---|---|---|
| LVGL 任务 | 12 KiB | `main/main.cpp` | `lv_cfg.stack_in_psram = true` |
| `fnos_poll` | 6 KiB | `components/fnos_monitor/fnos_data.c:647` | `tskNO_AFFINITY`，不固定 CPU 核 |
| `fnos_pair` | 7 KiB | `components/fnos_monitor/fnos_pair.c:1058` | TLS 握手走这里 |
| `tls_probe` | 8 KiB | `components/fnos_monitor/fnos_pair.c:399` | 临时探针 |
| `wifi_cli` | 4 KiB | `components/fnos_monitor/fnos_wifi_cli.c:232` | 串口命令 |
| `usb_lib` | 4 KiB | `components/esp32_p4_wifi6_touch_lcd_7b/esp32_p4_wifi6_touch_lcd_7b.c:727` | BSP 自带 |
| `ui_retry` | 16 KiB | `main/main.cpp:310` | 显示锁失败后的重试兜底 |

主任务栈配置为 16384 B。`fnos_ui_create()`（`components/fnos_monitor/fnos_ui.c:5956`）的建树路径
曾出现 11 KB 量级栈帧，当前需求须以对应 ELF 的检查结果为准。**往启动路径或数据帧 tick 增加构件后，必须先跑
`python3 tools/stack_check.py`**——它从 ELF 静态算各任务调用链的栈深下界。

线程归属只有一条规则：**只有 LVGL 线程写 UI**。数据经 `fnos_data_get`/历史接口读取，UI 内部不发网络请求；
页面切换与温度展开用 `fnos_ui_request_page` / `fnos_ui_request_temp_expand` 只置标志，由 LVGL 线程自己消费。

## 5. 双构建目标：一份 UI 源码，两个宿主

这是现有文档没有编码、但决定了整个可测试性的边界。

`components/fnos_monitor/CMakeLists.txt` 与 `tools/preview/CMakeLists.txt` **编的是同一批 `.c` 文件**，
差异只在固件专属模块与 ESP 依赖：

| 源码 | 固件 | PC 预览器 |
|---|---|---|
| `fnos_ui.c` | ✅ | ✅ |
| `fnos_snapshot.c` | ✅ | ✅ |
| `ui_kit/{uk,uk_number,uk_meter,uk_spring,uk_theme}.c` | ✅ | ✅ |
| `assets/michka_host_status/michka_host_status.c` | ✅ | ✅ |
| `fnos_perf.c` | ✅ | ✅（排除 `CONFIG_FNOS_UI_PERF_BENCH`） |
| `fonts/*.c`（10 个） | ✅ | ✅ |
| `fnos_net.c` / `fnos_data.c` / `fnos_pair.c` / `fnos_wifi_*.c` | ✅ | ❌ 用 `stub/` 顶掉 |

`fnos_ui_create()` 的两个运行宿主是 `main/main.cpp` 与
`tools/preview/preview.c`。同一份代码被两个宿主拉起，预览器的几何才有资格当审计基准。

两个构建的配置来源不同：固件走 `sdkconfig`；预览器从固件的 `build/config/sdkconfig.h` 里抠出
`CONFIG_FNOS_UI_*` 等生成 `ui_config_host.h`。所以**改 UI 相关 Kconfig 后必须先 build 固件再跑预览**，
否则预览器读的是上一次的开关值。

一处容易踩的差异：固件侧 `components/fnos_monitor/CMakeLists.txt` **逐个列出 10 个字体文件**
（第 21–30 行），预览侧却用 `file(GLOB FONT_SRC "${COMP}/fonts/*.c")` 扫目录。所以新增字体时，固件侧
必须手动加一行、预览侧自动带上；两边不同步的症状是"预览能渲染、固件链接失败"。

预览器的 CMakeLists 还额外编了一个 `snapshot_check` 可执行文件（`snapshot_check.c` + `fnos_snapshot.c` +
cJSON），只验解析与字节预算、不画界面。所以同一份 `fnos_snapshot.c` 实际被**三个目标**链接：固件、
预览器、解析自检。新增平台依赖时必须维护主机替身或隔离条件，否则会破坏主机构建。

## 6. 一次遥测的调用链

```
fnos_poll 任务（6 KiB）
  └─ fnos_data.c:545  poll_task
       └─ HTTPS GET /api/v1/status
            └─ fnos_snapshot.c:153  fnos_status_parse   解析 + 字节预算
                 └─ 持互斥量写入常驻 PSRAM 快照
                      └─ LVGL 定时器（CONFIG_FNOS_UI_REFRESH_MS，默认 100 ms）
                           └─ fnos_ui.c:5726  refresh()
                                └─ fnos_data.c:665  fnos_data_get()   非阻塞；锁忙则保留上次
                                     └─ 只刷当前页正文，非当前页标 dirty
```

两个细节值得记住：

- `fnos_data_get()` **非阻塞**是硬要求——它在 LVGL 线程里跑，锁忙时保留原 out 值而不是等。等锁会把
  整帧卡在渲染线程上。
- 快照含动态清单与借用字符串。句柄用 `{0}` 初始化，跨刷新保留要用 `fnos_status_copy`，结束
  `fnos_status_release`；**禁止裸结构体复制**。

## 7. 编译期开关面（32 个）

改行为前先看清它属于哪一类——这几组开关的耦合方式完全不同。

**节奏与预算**

| 开关 | 默认 | 牵动 |
|---|---|---|
| `FNOS_POLL_INTERVAL_MS` | 200 | 轮询周期（允许区间 50–60000 ms，由 `fnos_data.h` 的两个宏定义） |
| `FNOS_UI_REFRESH_MS` | 100 | LVGL 刷新节拍 |
| `FNOS_STATUS_MAX_BYTES` / `FNOS_SNAPSHOT_MAX_BYTES` | 256 KiB / 1 MiB | 传输与快照字节预算，超预算拒绝当前帧并报告容量错误 |
| `FNOS_UI_TEXT_SCRATCH_BYTES` | — | 文本暂存 |
| `FNOS_UI_NUMBER_BUDGET_BYTES` | — | 动效数字预算，耗尽后走 `uk_number_stats` 回退计数 |
| `FNOS_CHART_WINDOW` | 180 | 趋势窗口点数（`FNOS_HIST_MAX` 600 为环容量） |
| `FNOS_UI_LIST_MIN_WIDTH` | — | 自适应多列清单的最小列宽 |

**动效**

`FNOS_UI_REDUCED_MOTION`、`FNOS_UI_NUMBER_MS`、`FNOS_UI_NUMBER_PRESENT_MS`、`FNOS_UI_NUMBER_STAGGER_MS`、
`FNOS_UI_NUMBER_MAX_STAGGER_MS`、`FNOS_UI_NUMBER_TRAVEL_PCT`、`FNOS_UI_NUMBER_BLUR_PCT`、
`FNOS_UI_TELEMETRY_DAMPING`、`FNOS_UI_TELEMETRY_RATIO_PERMILLE`、`FNOS_UI_TREND_SMOOTH_SAMPLES`、
`FNOS_UI_FLOW_SMOOTH_SAMPLES`、`FNOS_UI_TREND_SCROLL_MS`、`FNOS_UI_TREND_FPS`。

**验收与诊断**：`FNOS_UI_PERF_BENCH`、`FNOS_UI_PERF_DRAW`、`FNOS_UI_MOTION_STATS`、`FNOS_HEAP_DEBUG`、
`FNOS_SOAK_DIAG`、`FNOS_AUTO_PAGE_SEC`。

**外观**：`FNOS_PALETTE`（默认 `michka`，可选 `cuktech` / `unifi` / `cuktech-console` / `device` /
`graphite` / `abyss` / `phosphor`；本地 `sdkconfig` 优先）。

**阈值**：`FNOS_UI_USAGE_WARM`、`FNOS_UI_USAGE_HIGH`、`FNOS_UI_TEMP_WARM`、`FNOS_UI_TEMP_HIGH`。

`FNOS_SOAK_DIAG` 打开后，每 10 s 由 LVGL 事件循环打一行 `FNOS_DIAG {...}` JSON（堆/栈/P95/复位原因/
ELF SHA-256），供 `tools/audit125/soak72.py` 采集；该心跳不等同于帧耗时或触摸延迟验收。

## 8. 失败与降级路径

这几条是"看似可选、实则唯一正确"的选择，改动前请先读它们为什么长这样。

**显示启动失败与 UI 创建失败分别处理**

`main/main.cpp` 里 `bsp_display_lock(2000)` 最多试 3 次。失败则置 `s_ui_degraded` 并起 `ui_retry`
任务按 5/10/20/40/60 秒退避（最多 5 轮），期间数据采集与已启动的串口 CLI 继续运行。
只有更早的 `bsp_display_start_with_config()` 失败才直接 return，不启动数据采集；网络与串口保持原状态。

**刷屏模式是实测选的**

注释记录了被否掉的路径：`DOUBLE_DIRECT` 在首帧 flush 等 VSYNC、持锁导致 `display lock failed`；
`NONE` 不支持 rotation。当前使用 `TRIPLE_PARTIAL` 路径；源码注释记录过约 8 ms 的拷贝成本
（整帧 blit 1024×600×2B×2）。这是既有测量记录，不能当作所有工作负载的帧耗时保证。

**Wi-Fi 同步 RPC 红线**

`esp_wifi_*` 是板载 C6 走 ESP-Hosted 的**同步 RPC，最坏阻塞 5 秒**。它只能在普通任务里调，
**绝不能在 LVGL 定时器或 esp_timer 回调里调**。所以扫描是 `fnos_net_scan_request()` 只入队、由
`fnos_poll` 任务调用 `fnos_net_service()` 真正执行。同理 `fnos_net_state_str()` 返回静态缓冲、只可单任务使用，要重入用 `_r` 版本。

**配对：信任锚点不由服务器给**

TOFU 状态机 `UNPROVISIONED → FETCHING → CONFIRM → PAIRING → PROVISIONED / FAILED`。证书由板子自己取、
自己算 SHA-256，指纹上屏由人比对。**确认前不发任何带令牌的请求，也不接受服务器 JSON 里的指纹字段**——
接受了这一步，TOFU 就退化成"服务器说什么就是什么"。明文连接只放行 `GET /api/v1/identity`。

**超预算不静默**

传输或快照超字节预算时拒绝当前帧、保留旧快照并报告 `data capacity`。`source_dropped` 则统计服务端
`trunc.dropped` 声明的缺失条目，两者是不同的失败来源。硬件清单必须随实际数据自适应，不能静默裁剪。

## 9. 验证面

| 层 | 手段 | 兜住什么 |
|---|---|---|
| 几何 | `bash tools/preview/run.sh` 的四条审计 | 字形覆盖、标签溢出、**子对象跑出父对象**、**可点击控件互相压住** |
| 文本 | `preview.c` 的 `audit_format_residue()` | 屏上出现未消费的 `%d`/`%s`/`%u`（挂进样张/夜间矩阵/夜间样张三个面，出现即 abort） |
| 栈 | `python3 tools/stack_check.py` | 各任务调用链栈深下界 vs 预算（静态下界，不含函数指针回调与中断嵌套） |
| 逻辑 | `tools/*.py` 42 个脚本 | 配对消息、调色板、硬件 fixture、原因文案、freshness、plan 等 |
| 实机 | `soak72.py` + `FNOS_SOAK_DIAG` | 长跑期堆/栈/P95/复位原因回归 |

后两条几何审计是补上来的，代价是一次真实事故：配对键盘四行键只给了装三行的盒子，第四行（删除/0/确认）
整排掉到卡片外压在"关闭"按钮上——而字形审计与文字断言全都不说话，那三个键"存在"，只是画错了地方。
**动盒子高度就必须回头算里面装的东西要多大。**

物理屏验收按项目约定用 `capture-board`；主机预览与构建通过**不等于**实机验收。

## 10. 交互图说明

[`docs/diagrams/architecture-fnos-monitor.html`](diagrams/architecture-fnos-monitor.html) 是同一份结论的
可视化版本（单文件、内联 SVG、深浅色可切换、边轨可追迹）：上排是从 NAS 采集到板子快照的数据路径，
下排是渲染回流到屏幕，中间信任带是配对锚点，右下角是主机工装对同一份 UI 源码的复用。

可编辑规格为同目录的 [`architecture-fnos-monitor.spec.json`](diagrams/architecture-fnos-monitor.spec.json)。
安装 Archify 3.0.1 后，将 `ARCHIFY_CLI` 指向其 `bin/archify.mjs`，在仓库根目录运行：

```bash
node "$ARCHIFY_CLI" \
  finalize architecture docs/diagrams/architecture-fnos-monitor.spec.json \
  docs/diagrams/architecture-fnos-monitor.html --repo-root "$PWD" --quality showcase --json
```

命令执行 validate、deliver、check 和真实浏览器 browser-check 四项检查。
生成回执包含本机路径，留在本地忽略文件中；检查通过不代表固件或实机验收。

几何约束：`fromSide` 的第一段必须严格正交，`top` 要竖直向上；跨层路径使用行间走廊，避免穿过其他关系。
