# 代码审核报告 · 2026-11-03

- 审核对象：`p4-7b-fnos-monitor` 发布工程，基线 `cd608f1`（`origin/main`），工作树干净起步。
- 范围：项目自有代码——固件组件与 `main/`、`ui_kit/`、NAS 侧 Python（伴生应用、采集器、FPK 工装）、`tools/`、`scripts/` 与 CI。`managed_components/`、`build/`、生成字体只核边界，不逐行审。
- 判定标准：每条结论给出文件与行号；无法在本次环境验证的（固件构建、实机、长时间运行、真实 NAS）明确标「未验证」，不当作已通过。
- 环境限制：本机没有可用的 ESP-IDF（`idf.py` 不在 PATH、`IDF_PATH` 未设置），因此**固件未重新构建**；主机预览（gcc）与 Python/脚本回归可跑。

---

## 0. 结论摘要

1. **数据/快照/配对三条主路径的契约是扎实的**：快照有引用计数与 1 MiB 预算、解析有乘法溢出与数值夹取、NVS 有 DIRTY/READY/EMPTY 事务屏障、HTTPS 缺证书时 fail-closed 不降明文。未发现越界、泄漏或裸结构体复制。
2. **两个可离线确认的缺陷已修**：
   - B01（P1）诊断页「端点」写死编译期 `FNOS_HOST:FNOS_PORT`，配对到其它地址后一直显示错误地址；
   - B02（P2）历史回填失败后每个轮询周期都重发 `/api/v1/history`（默认 200 ms ≈ 5 次/秒），与代码注释承诺的「每秒一次」不符。
3. **三项需要你裁决的取舍**（未改代码）：B03 明文引导响应的 JSON 嵌套深度无约束、B04 服务端接受 `?token=`、B05 配网 set_config 失败后 `s_configured/s_ssid` 已被提前更新。
4. **未完成任务没有隐藏半成品**：自有代码里 `TODO/FIXME/XXX/HACK` 只命中占位符黑名单，没有遗留实现标记；T01–T10 全部受硬件、真实 NAS 或人力观测阻塞，没有本轮可继续离线完成的新增项。
5. **验证边界**：主机预览重跑四项审计全 PASS；`tools/test_history.py` 全 PASS；L1 主机回归 40 PASS / 2 环境性 FAIL（旧预览目录所有权）；L2 固件构建因无 IDF 失败；UI 改动由预览构建实际编译通过，**数据层改动本次没有编译器覆盖**。

---

## 1. 整体结构、模块划分与数据流

### 1.1 执行环境（四层）

| 层 | 组成 | 约束 |
|---|---|---|
| 固件后台任务 | `fnos_net` / `fnos_data` / `fnos_pair` / `fnos_wifi_store` / `fnos_wifi_cli` + `main.cpp` | 不画界面；同步 RPC 只在普通任务；失败不阻塞启动 |
| UI 与快照 | `fnos_ui.c`、`ui_kit/`、`assets/`、`fnos_snapshot.c`、`fonts/` | 只有 LVGL 任务写 UI；只读 `fnos_data_get` 副本与网络缓存 |
| 主机工装 | `tools/preview`、`tools/*.py|*.sh`、`scripts/` | 不是实机验收替代；预览读 `build/config/sdkconfig.h` |
| NAS 侧 | `nas/fpk/nasscreencompanion`（服务+采集+打包）、`nas/fnos-agent.py` | python312；HTTPS 8798、独立采集 8799 |

### 1.2 模块规模（自有、非生成字体）

| 文件 | 行数 | 职责 |
|---|---:|---|
| `components/fnos_monitor/fnos_ui.c` | 6,703 | 六页 UI、组件装配、文案格式化、动效登记 |
| `components/fnos_monitor/ui_kit/uk.c` | 1,593 | 原生 LVGL 组件与几何策略 |
| `components/fnos_monitor/fnos_pair.c` | 1,203 | 配对状态机、NVS 事务、固定证书 TLS |
| `components/fnos_monitor/fnos_data.c` | 777 | HTTPS 轮询、历史环形缓冲、快照发布 |
| `components/fnos_monitor/fnos_net.c` | 707 | Wi-Fi 事件机、配网提交、扫描、SNTP |
| `components/fnos_monitor/fnos_perf.c` | 691 | 台架测量（Kconfig 关闭时为空实现） |
| `components/fnos_monitor/fnos_snapshot.c` | 265 | 快照解析、所有权、预算 |
| `main/main.cpp` | 330 | 启动编排、显示失败退避、夜间背光 |
| `nas/.../nas_companion_server.py` | 1,878 | HTTP/TLS/配对/管理页/安装 |
| `nas/.../fnos_collector.py` | 1,239 | 指标采集、历史、容器/温度等 |
| `nas/fnos-agent.py` | 1,479 | 独立采集器 |

### 1.3 主链路（采数 → 上屏）

1. `fnos_poll` 任务（6 KiB 栈，优先 PSRAM）周期取 `s_conn`，`GET /api/v1/status`（`fnos_data.c:581`）。
2. `fnos_status_parse` 解析并做字节预算、数值夹取、动态数组分配（`fnos_snapshot.c:153`）。
3. `commit_status` 持锁把副本写入常驻快照，更新 ok/fail/recv/online 与历史样本（`fnos_data.c:479`）。
4. LVGL 定时器 100 ms 触发 `ui_tick` → `refresh()` → `fnos_data_get()` 非阻塞取副本（锁忙保留上一帧），只刷当前页正文（`fnos_ui.c:5731`、`fnos_data.c:665`）。
5. 配对/撤销通过 `fnos_pair` 的工作任务完成，数据层按 `fnos_pair_generation()` 变化重建 HTTP 客户端（`fnos_data.c:566`）。

### 1.4 栈与内存契约（文档基线 + 代码核对）

| 执行体 | 栈 | 位置 |
|---|---:|---|
| LVGL 任务 | 12 KiB | PSRAM（`main.cpp` 显示配置） |
| `fnos_poll` | 6 KiB | 优先 PSRAM（`fnos_data.c:647`） |
| `fnos_pair` | 7 KiB | 优先 PSRAM（`fnos_pair.c:1058`） |
| TLS 探针 | 8 KiB | PSRAM（`fnos_pair.c:399`） |
| `wifi_cli` / `usb_lib` | 4 KiB | 内部 RAM |
| `ui_retry` | 16 KiB | 显示失败退避 |
| 主任务 | 16,384 B | `sdkconfig` |

快照契约：句柄必须 `{0}` 初值；跨刷新保留用 `fnos_status_copy`；结束 `fnos_status_release`；禁止裸结构体复制（`fnos_snapshot.c:70` 的 `*out = *source` 是 copy 内部的预期实现）。

### 1.5 双构建目标

固件与主机预览编同一批源文件；固件在 `components/fnos_monitor/CMakeLists.txt:21-30` **显式列 10 个字体**，预览在 `tools/preview/CMakeLists.txt:45` **glob** 整个 `fonts/`。预览从 `build/config/sdkconfig.h` 生成 `ui_config_host.h`（`tools/preview/CMakeLists.txt:13,23`），所以改 UI Kconfig 必须先有固件构建产物。

---

## 2. 架构审核

### A01 单文件承载整屏 UI，评审与回归面过大

- **问题**：`fnos_ui.c` 6,703 行；`refresh_body` 从 `fnos_ui.c:5049` 起约 690 行，六个页面的数据绑定、文案与状态分支都在同一函数族；建树入口 `fnos_ui_create` 也在同一文件（`fnos_ui.c:5961`）。
- **依据**：文件行数；`refresh_body` 起止；每页 `if (page == n)` 分支集中。
- **影响**：单点改动触碰整屏刷新路径，code review 与冲突成本高；任何局部错误都可能被全屏 tick 放大。
- **建议**：不重写；按页把「文案+绑定」抽成 `ui_page_*.c` 的纯函数（入参 `fnos_status_t*`），`refresh_body` 只做分发；先抽一页验证收益，再决定是否继续。

### A02 固件/预览两份源清单会漂移

- **问题**：字体列表在固件侧手写、预览侧 glob；新增字体若只加文件，固件漏编、预览却正常，反过来在固件侧手写漏项则预览「看起来没问题」。
- **依据**：`components/fnos_monitor/CMakeLists.txt:21-30` vs `tools/preview/CMakeLists.txt:45`。
- **影响**：字形缺失只在实机/构建期暴露；属于「本机能跑、设备不对」的典型裂缝。
- **建议**：预览也显式列同一份清单（或由 `fonts/` 生成一份两处共用的 `font_list.cmake`），CI 比对各清单与目录内容。

### A03 预览隐式依赖固件构建产物

- **问题**：`ui_config_host.h` 从 `build/config/sdkconfig.h` 生成；本机无 IDF 时预览会静默使用旧配置。
- **依据**：`tools/preview/CMakeLists.txt:13,23`；本机 `build/config/sdkconfig.h` 停留在 2026-11-02。
- **影响**：改了 UI Kconfig 却只跑预览，审计用的是旧开关，结论失真。
- **建议**：生成时记录 `sdkconfig.h` 的 mtime/hash，与源码 Kconfig 不一致就打印显著警告（CI 已有「先 build 再 verify」的顺序兜底）。

### A04 快照所有权模型正确，但依赖调用方守约

- **问题**：`fnos_status_release` 对未 `{0}` 的句柄会按 `refs` 递减并读 `owner`；契约靠文档与约定，运行期没有魔数校验。
- **依据**：`fnos_snapshot.c` 的 `copy`/`release`/`owned_alloc`（溢出与 1 MiB 预算检查）。
- **影响**：新代码若忘记初始化句柄，表现为偶发崩溃且难定位（本项目已被栈/内存问题伤过）。
- **建议**：`#if FNOS_HEAP_DEBUG` 时在 owner 头加魔数并在 copy/release 断言；不改变发布行为。

### A05 UI 线程会取配对锁，落盘期间可能被阻塞

- **问题**：`pair_refresh` 调 `fnos_pair_view`（`fnos_ui.c:3512-3519`），本次修 B01 又在系统页每 tick 调一次（`fnos_ui.c:5619-5620`）；`fnos_pair_view` 内部 `xSemaphoreTake(s_lock, portMAX_DELAY)`（`fnos_pair.c:1106-1119`），而 `do_pair` 在持同一把锁时做 `nvs_save`。
- **依据**：上述行号；`nvs_save` 的两次 commit（`fnos_pair.c:604-636`）。
- **影响**：配对/清除的落盘瞬间，LVGL 任务可能停顿数十毫秒（最坏一块 NVS 擦写），表现为动效掉帧；不是死锁。
- **建议**：给 UI 增加 `fnos_pair_view_try(out, 0)`（取不到锁就沿用上次缓存），或把端点写进数据层快照；当前先记录为残余风险。

### A06 默认轮询节奏偏高

- **问题**：`FNOS_POLL_INTERVAL_MS` 默认 200 ms（`components/fnos_monitor/Kconfig:3-10`），即 5 次/秒 status 请求；历史回填修好后另有最多 1 次/秒。
- **依据**：Kconfig 默认值；`fnos_data.c:581` 的请求路径。
- **影响**：局域网带宽、NAS 端解析 CPU、串口日志量都按 5 Hz 计；对「监控屏」通常过剩。
- **建议**：按部署实测后评估 500 ms；若追求实时感，可用「变化才刷 + 心跳」而不是纯提频。

### A07 明文引导通道的输入没有结构约束

- **问题**：`do_fetch`/`tls_probe_body`/`do_pair` 对明文 HTTP 响应直接 `cJSON_Parse`；cJSON 嵌套上限 1000（`managed_components/espressif__cjson/cJSON/cJSON.h:137`），接收缓冲 12 KiB（`fnos_pair.c:64`），而 pair 栈 7 KiB、探针 8 KiB。
- **依据**：上述行号；约 2 KiB 的 1000 层 `[[[…]]]` 即可让解析递归超过 7 KiB。
- **影响**：配对窗口内，局域网攻击者（ARP/DNS 冒充）可造成栈溢出/重启；属于可用性问题，不是证书信任绕过。
- **建议**：解析前做括号深度预检（上限 16）或限制 identity 响应长度；是否接受该威胁模型见 Q1。

### A08 分层依赖总体单向，数据层与网络层存在窄双向

- **问题**：`fnos_net` 在 IP 变化时回调 `fnos_data_network_changed`（`fnos_net.c:148,252`），`fnos_data` 又调 `fnos_net_service`/`fnos_net_online`（`fnos_data.c:565`）；文件级是双向依赖。
- **依据**：上述行号；接口都收敛在 `fnos_net.h` 的少数函数上。
- **影响**：可接受；但新增「网络事件→数据层」逻辑时容易形成隐式顺序依赖。
- **建议**：保持「net 只发通知、data 只拉状态」的方向，不再增加反向同步调用。

### A09 诊断页是单点可观测面，字段价值高但曾失真

- **问题**：诊断页 11 行固定字段（主机/端点/HTTP/轮询结果/最近错误/数据年龄/固件内存/ZFS 缓存/协议/采集段/动效），是排障主入口；B01 让它长期显示错误端点。
- **依据**：`fnos_ui.c:5614` 起的 `page == 3` 块；本次修复后端点来自配对快照。
- **影响**：错误信息会直接把排障引向错误地址（如按 `nas.local` 排查一个已配对到 IP 的设备）。
- **建议**：后续把「端点」与「状态帧里的 host」分成两行显示，避免同名字段混淆。

### A10 主机回归覆盖广，但网络事件机与 NVS 掉电仍是空白

- **问题**：`tools/host_checks.sh` 有 62 项检查（含配对状态/事务/提交/诊断、证书指纹、字段契约、栈帧等），但 net 事件状态机与 NVS 掉电/旧格式没有自动化。
- **依据**：`tools/host_checks.sh:48` 等条目；`docs/todo-implementation-2026-10-23.md` T05 明确「掉电/旧格式未验」。
- **影响**：Wi-Fi 断连/重连与升级换证类回归只能靠实机。
- **建议**：把 `nvs_load` 的判定与 `reason_text` 抽成纯函数（去掉 NVS/esp 依赖）做主机表驱动测试。

### A11 NAS 侧单文件过重

- **问题**：`nas_companion_server.py` 1,878 行同时承担 TLS/HTTP 路由、配对、管理页、安装与升级；`fnos_collector.py` 1,239 行承担采集与历史。
- **依据**：行数；文件内路由与安装逻辑同处。
- **影响**：安全面（认证、路径、子进程）与采集逻辑耦合，改动回归成本高。
- **建议**：先按「路由层 / 认证 / 采集 / 安装」拆模块，保持 `APPVER` 与协议字段不变；一次只拆一层。

### A12 令牌允许走 URL 查询参数

- **问题**：认证依次接受 `Authorization: Bearer`、`X-Token`，最后回退到 `?token=`（`nas_companion_server.py:916-923`）。
- **依据**：上述行号；访问日志已主动剥离 query（`nas_companion_server.py:779`）。
- **影响**：URL 会进入 shell history、代理/抓包与第三方日志；板子本身只用 `X-Token`，回退没有运行期必要。
- **建议**：默认关闭 query token；若为 curl 调试保留，需显式 debug 开关并在文档标注。

### A13 管理面只校验请求头，不校验 Unix 对端身份（已知边界，不计缺陷）

- **位置**：`nas/fpk/nasscreencompanion/app/server/nas_companion_server.py:890-895`（`_require_admin`/`_gateway_user`）。
- **现状**：管理身份来自 `X-Trim-Userid`/`X-Trim-Isadmin`；Unix Socket 只 `chmod 0600`，全仓无 `SO_PEERCRED`/`getpeereid`。以包用户 uid 运行的本地进程可自带管理员头连上 socket。
- **依据**：`docs/fnos-companion-app-plan.md:34` 已写明「Unix Socket 0600 允许 root 与所属用户访问，不能声称仅网关可访问」，属已接受边界；本次不计为新缺陷。
- **可选的收紧**：accept 后取 `SO_PEERCRED`，要求 peer uid==0；头只用于把 root 请求映射到具体 fnOS 用户。

---

## 3. Bug 清单

| 编号 | 级别 | 位置 | 现象 | 原因 | 修复 | 状态 |
|---|---|---|---|---|---|---|
| B01 | P1 | `components/fnos_monitor/fnos_ui.c` 旧 5616 | 配对到非默认地址后，系统页「端点」仍显示 `nas.local:8799`，排障时指向错误设备 | 直接用了编译期 `FNOS_HOST/FNOS_PORT`，没有读配对快照 | 改为 `fnos_pair_view(&ep)`，`ep.host[0]` 时显示 `host:port`，否则回退默认值 | 已修 |
| B02 | P2 | `components/fnos_monitor/fnos_data.c:586-597` | 空 history / 404 / 坏 JSON 时每个轮询周期都补发 `/api/v1/history`，默认 200 ms 一次（≈5 次/秒），长期不止 | `parse_history` 失败返回 false，`hist_done` 不置位；外层没有时间闸 | 增加 `hist_next_us`，失败后 1 s 内不再重试；端点变化时清零（仍为长期 1 Hz，退避到 60 s 见 Q11） | 已修 |
| B03 | P2 | `fnos_pair.c:659`（do_fetch）、`fnos_pair.c:319`（probe）、`cJSON.h:137` | 明文 identity 响应若是深层嵌套 JSON，解析递归可耗尽 7 KiB 配对栈，导致崩溃/重启 | `cJSON_Parse` 无项目级深度限制，接收缓冲 12 KiB 足够塞入 1000 层 | 解析前括号深度预检（≤16）或限制 identity 长度 | 已修 |
| B04 | P2 | `nas_companion_server.py:916-923` | 令牌可作为 `?token=` 传入 | 为 curl 方便保留的回退 | 只在 debug 模式接受 query token | 待确认（Q2） |
| B05 | P3 | `fnos_net.c:504-509` 与 `:521` | `esp_wifi_set_config` 失败时，界面/状态里的 SSID 已切到新值且 `s_configured=true`，radio 仍在旧配置；重启后才一致 | 先更新 `s_ssid/s_configured`，后调用 `set_config` | 把状态更新移到 `set_config` 成功之后（含 `current` 判定） | 未修（P3，待确认 Q3） |
| B06 | P1 | `nas/fpk/verify_package_sources.py` 旧 43-45 | 只比 `app.tgz`；root 侧 `cmd/`、`wizard/`、图标改动不重新 fnpack 也能过门禁，发布包可能带旧启动脚本 | 校验器只看 app 内层，`build.sh:58`、`host_checks.sh:65` 只传 app 目录 | 增加外层双向比对（cmd/wizard/ICON，跳过 manifest 与生成文件），默认 `root=source.parent`；自测覆盖匹配/内容不同/缺失 | 已修 |
| B07 | P2 | `nas/fpk/contract_check.py:208` | 字段改名可被裸赋值正则误认，契约检查假通过（如 `_volumes` 内局部 `pct`） | 第三路正则 `\bname\s*=` 收得过宽 | 裸赋值只接受顶层缩进/已知前缀，或改用 AST 取快照键 | 未修（Q7） |
| B08 | P2 | `nas_companion_server.py:1859-1860` | 退出收据目录未建，`finally` 里 `mkstemp` 抛异常会吞真实退出码且不留 `service-exit.json` | 收据路径目录不存在时未 `mkdir` | 写收据前 `mkdir -p`，异常不覆盖退出码 | 已修 |
| B09 | P2 | `cmd/main:213-220`、`cmd/upgrade_init:43`、`cmd/config_callback:19-23` | 服务已被 KILL/非 0 退出仍判 stop 失败，升级/改配置被误拒 | stop 只看退出码，不区分「本来就没在跑」 | 先判进程存在；KILL 后按预期终态放行 | 未修 |
| B10 | P2 | `scripts/preflight.sh:37` | macOS BSD `stat` 无 `-c`，`\|\| echo 0` 使 >10 MB blob 检查恒不触发；钩子就跑在 Mac | 用了 GNU stat 语法 | 改 `wc -c < "$f"` 并去空白 | 已修 |
| B11 | P2 | `scripts/preflight.sh:44` | 按整行含 example/xxx/REDACTED 跳过，同行真密钥漏检 | 占位符过滤作用在整行而非命中串 | 改为对命中 token 做占位符判定 | 已修 |
| B12 | P2 | `fnos_collector.py:795-797` | 负温与 0 °C 通道被当「无传感器」丢弃，且无上限校验 | `c <= 0` 直接 continue | 只跳过读取失败；合法范围做上下限夹取 | 未修（抽查属实） |
| B13 | P2 | `nas_companion_server.py:971/991/1119/1211` | 500 回显异常类型与文本、404 回显请求路径 | 错误响应直接拼内部信息 | 统一通用错误文案，细节只进日志 | 未修 |
| B14 | P2 | `nas_companion_server.py:767` | 501 只发 `Connection: close` 头、不置 `close_connection`，未读 body 被当下一请求解析 | 响应关闭策略与连接对象不一致 | 501 同时置 `close_connection` | 未修 |
| B15 | P2 | `cmd/_common.sh:312` | 健康探针只匹配 `b'"ok"'` 子串，403 的 `{"ok":false}` 也判活 | 子串匹配而非 JSON 布尔解析 | 解析 JSON 并判 `ok is True` | 已修 |
| B16 | P1 | `fnos_ui.c:1586` | 点行开详情时先 `page_motion_settle()`，它可能同步 `refresh()` 并在 `rows_sync()` 里删除该行；随后读 `row->name1` 等是 use-after-free | 回调参数在「可能重建列表」的调用之后才使用 | 先取文本再 settle，或 settle 后不再触碰 `row`（不能只判 `row->row`，`row` 本身已释放） | 已修 |
| B17 | P1 | `fnos_ui.c:5666` 与 `5670-5676` | 告警行按 `total` 增删、按分组过滤后的 `k` 填充；两套计数口径不一致时多出的行保留上一拍旧文案 | `health_summary()` 计数与 `alert_group_of()` 过滤不同源 | 以过滤后实际条数为准建行，循环后清空剩余行 | 已修 |
| B18 | P1 | `fnos_ui.c:3163` / `3295-3298` | Wi-Fi 行用「建行时的 AP 下标」索引「点击瞬间重扫结果」；重扫完成到下一拍重填之间会点错网络 | 下标语义随异步扫描变化 | 按 SSID 文本匹配新结果，失配提示「列表已更新」 | 已修 |
| B19 | P2 | `ui_kit/uk.c:1290` | `trend_rebase` 未判空 `lv_chart_get_series_x_array`（同文件 `uk_trend_clear` 已判），图表未布局时解引用 NULL | 两处写法不一致 | `if (!x) continue;` | 已修 |
| B20 | P2 | `ui_kit/uk.c:1557` | `uk_anim_reveal` 缺空指针保护，构件分配失败后被调用即空指针写 | 与 `uk_anim_pulse/fade` 的保护不一致 | 首行 `if (!panel) return;` | 已修 |
| B21 | P2 | `fnos_ui.c:3925-3929` | 温度页每帧对分组做 O(g²) 同名去重，且每组一次 `uk_alloc`；24 路通道即每 100 ms O(n²)+n 次分配，全在 LVGL 任务 | 分组算法与显示名重建无缓存 | 按 `ntemps`/名字指纹缓存分组，或排序后相邻比较 | 未修 |
| B22 | P2 | `fnos_ui.c:341-346` | 超长文本 `uk_alloc` 失败时无 else，label 静默保留旧文案（诊断页「最近错误」等） | 失败分支缺失 | 失败时写「文本过长」或由 `inventory_notice()` 统一提示 | 已修 |
| B23 | P2 | `fnos_ui.c:5541` | 温度块扩容失败直接降到已建块数，少显示设备但温度页无「还有 N 台未显示」说明 | 降级不记录被裁数量 | 记录 `s_tgrp_n - nblk` 并写入页脚提示 | 未修 |
| B24 | P1 | `fnos_net.c:442`（状态优先级）、`:241-247`、`:498-501`、`:665-667` | 提交失败（NVS 写失败或 30 s 超时）后状态置 FAILED；若旧连接仍在或 AP 迟到连上，`fnos_net_state()` 让 FAILED 压过 ONLINE，顶栏长期显示「Wi-Fi 未连接」，直到重启或再碰键盘 | FAILED 优先级高于在线；GOT_IP 只清 PENDING，不清 FAILED | FAILED 仅在 `!s_online` 时返回；GOT_IP 对同代次 FAILED 也清回 IDLE | 已修 |
| B25 | P2 | `fnos_net.c:596` | 扫描结果 `calloc` 失败直接 return，未释放驱动侧 AP 列表，滞留到下次 `esp_wifi_scan_start` | 失败分支漏 `esp_wifi_clear_ap_list()` | 失败分支先清列表再置失败 | 已修 |
| B26 | P2 | `fnos_pair.c:1064-1067`、`fnos_data.c:566` | 配对任务两次创建都失败后，每个轮询周期（200 ms）重试并打一条 ERROR，内存碎片化时持续刷屏 | 无重试间隔/不可用标记 | 失败后 30 s 重试或置不可用，generation 变化时重开 | 已修 |
| B27 | P2 | `fnos_pair.c:901-905`、`:1111`、`:1088` | 配对落盘在状态锁内做 NVS 写（含证书 1–2 KB、两次 commit），期间 LVGL 在 `fnos_pair_view` 上无限等锁、poll 在 generation 上等锁 → 整帧停顿、遥测停拍 | 写 NVS 与发布状态共用一把锁 | 锁外落盘，成功后短暂持锁发布；或给 view 加 0 超时版本 | 未修（与 A05 同源） |
| B28 | 未确认 | `fnos_net.c:570`、`:628`、`:685` | 若扫描被连接中止时 ESP-Hosted 不上报 `SCAN_DONE`，`s_scan_busy` 永远为真，后续扫描请求全被挡 | 只有 SCAN_DONE / 启动失败清 busy，无超时兜底 | 先取实机串口日志确认；确认后加 10 s 超时 | 未确认 |
| B29 | P2 | `nas_companion_server.py:1061`、`fnos_collector.py:199` | `/api/v1/history?n=-1` 会返回全量历史（最多 3600 行、约 151 KB），正是 1057-1060 行注释要防的「板子 32 KB 缓冲截断→解析失败→每秒重试」场景；负数把防护反转成放大 | 只挡了非数字，没挡负数与上限；`read_history` 对 `want<=0` 走全量分支 | `want = 0 if want <= 0 else min(want, hist_len)` | 已修 |
| B30 | P2 | `.github/workflows/ci.yml:135-136`、`:141` | `cp artifacts/build/{flasher_args.json,…} release/ || true` 吞掉失败，随后 FLASH.txt 无条件写 `write_flash @flasher_args.json`；产物缺该文件时 Release 仍成功，用户照文档烧录必失败 | 复制失败被 `|| true` 静默，生成步骤未做存在性判断 | 去掉 `|| true` 改硬失败，或 FLASH.txt 条件生成 | 已修 |
| B31 | P2 | `nas/fpk/nasscreencompanion/cmd/install_init:31` | `port_busy` 的三态（0 占用/1 空闲/2 未判定）被 `&&` 压成布尔，返回 2 时误印「检测到 8799 上有服务…可以并存」；无 /proc、lsof 不可用时触发，只影响提示 | 未接三态返回值 | `port_busy…; case $? in` 分别提示 | 已修 |
| B32 | P1 | `fnos_ui.c:101`、`:3151`、`:3200` | 配网 AP 只建 7 行，超过 7 个不显示、不提示、不可滚动；提示行「共 %d 个网络」用的是截断后的 n，数字本身是错的。项目硬规则要求清单随数据自适应 | `WIFI_AP_ROWS` 固定 7，并在填充前把 n 截到 7 | 行数随扫描结果自适应并池内滚动，提示用真实总数；需预览与实机回归 | 未修（UI 结构改动，需单独验证） |

复现证据链：

- B01：`docs/design-overview-2026-10-14.md:167` 明确「保存后的配对端点覆盖模板」；配对卡用 `v.host/v.port`（`fnos_ui.c:3517-3518`），诊断页却用宏。
- B02：`fnos_data.c:586` 的 `if (!hist_done)` 与 `parse_history` 的失败返回（`fnos_data.c` 中 `!n` / 顺序非法分支）；服务端 `read_history` 升序、空库返回空列表（`fnos_collector.py:197-201`）。
- B05：`fnos_net.c:508` 的 `s_configured = true` 在 `fnos_net.c:521` 的 `esp_wifi_set_config` 之前。
- B06：实测包根含 `cmd/main`、`cmd/_common.sh`、`cmd/docker_launch.py`、`wizard/`、`ICON*.PNG`，而 `app.tgz` 只含 `server/`、`ui/`、`config/`；旧校验器只遍历 `app.tgz`。
- B10：macOS 的 `stat` 无 `-c`（报 illegal option），`|| echo 0` 把失败变成 0；改为 `wc -c` 后 11 MB 临时文件判 `flagged=yes`。
- B16：`motion_finish()` 在 `s_refresh_pending` 为真时同步 `refresh(); pair_refresh();`（`fnos_ui.c:6393-6394`），而 `entity_open_cb` 在 1586 先 settle、1589 才读 `row->name1`。
- B24：`fnos_net.c:442` 的 FAILED 分支在 `if (s_online)` 之前；GOT_IP 原只在 PENDING 时清状态（`:241`），超时置 FAILED（`:665-667`）后没有清除路径，唯一 `fnos_net_submit_clear()` 调用在配网键盘回调（`fnos_ui.c:3312`）。
- B17：`total` 取自 `health_summary()` 的计数（`fnos_ui.c:5660` 附近），`k` 取自 `alert_group_of()` 逐条过滤（`5672-5674`），两者不同源。

来源说明：B07–B09、B11–B15、B16–B18、B21–B23 由两路只读子代理取证；B24–B28 由第三路固件审查取证（依据 IDF 5.5.5 `esp_wifi.h`/`cJSON.c` 语义回源）；B29、B30、B31 由 NAS/工装审查取证，主审复核了 B29、B30 的代码位置。主审复核了 B06、B10、B12、B16、B19、B20、B24 的代码事实，其余未逐条运行复现。B16–B18、B24 的修复涉及交互/状态时序，本轮只报告不改。

---

## 4. 未完成任务清单

来源：`docs/todo-implementation-2026-10-23.md`（当前唯一待办入口）；另有 `docs/open-items-2026-10-11.md` 的历史审计矩阵（明确不作为当前状态）。自有代码 `TODO/FIXME/XXX/HACK` 扫描只命中占位符黑名单，无遗留标记。

| 项 | 内容 | 状态 | 说明 / 所需条件 |
|---|---|---|---|
| T01 | 帧距与尾帧 | 受阻 | 需实机与长序列采集；主机只能给模拟值 |
| T02 | michka 迁移分段验证 | 受阻 | 无可用远程客户端（记录在案） |
| T03 | 物理屏与真实手指触控验收 | 受阻 | 需板子、`capture-board` |
| T04 | 72 h 长测 | 受阻 | 需连续运行环境；现有 1,262 条心跳不足 |
| T05 | NVS 掉电/旧格式/allocator 耗尽/换证撤销 | 受阻 | 需可断电真机；代码侧事务屏障已核 |
| T06 | FPK 真实 NAS 升级 | 受阻 | 需真实 fnOS 设备；本地候选包已存在 |
| T07 | 文档清理 | 已完成 | 入口已收敛到当前待办表 |
| T08 | 图标/渐变/详情定位 | 部分完成 | 物理验收部分受阻 |
| T09 | 原版发行资产字节一致性 | 受阻 | 需原始发行物；文本差异仅注释，去注释后相等 |
| T10 | 数字动效肉眼体验 | 受阻 | 需人工确认 |

本轮已修 20 项：B01、B02、B03、B06、B08、B10、B11、B15、B16、B17、B18、B19、B20、B22、B24、B25、B26、B29、B30、B31。尚未处理 12 项：B04、B05、B07、B09、B12、B13、B14、B21、B23、B27、B28、B32（原因见各行与 Q 清单）。其余待办受硬件/真实 NAS 条件限制（T01–T06、T09、T10），或属需要行为裁决的 P1/P2（B03–B05、B07–B09、B13、B14、B16–B18、B21、B25–B28）。

---

## 5. 本次改动

| 文件 | 改动 | 理由 | 风险 |
|---|---|---|---|
| `components/fnos_monitor/fnos_ui.c` | 系统页端点改读 `fnos_pair_view`，空则回退默认 | 修复 B01，消除排障误导 | LVGL 线程多一次配对锁获取；落盘瞬间可能短暂停顿（A05） |
| `components/fnos_monitor/fnos_data.c` | 新增 `hist_next_us`，失败后 1 s 闸；端点变化清零 | 修复 B02，兑现注释承诺的 1 次/秒 | 回填失败后的恢复最多晚 1 s；逻辑仅在轮询任务内使用 |
| `components/fnos_monitor/ui_kit/uk.c` | `trend_rebase` 判空 series x；`uk_anim_reveal` 首行判空 | 修复 B19/B20，消除两个空指针写路径 | 仅在异常/未布局路径生效，正常路径零行为变化 |
| `nas/fpk/verify_package_sources.py` | 增加包根（cmd/、wizard/、ICON）双向比对与对应自测 | 修复 B06，避免外层脚本改动不重打包也过门禁 | 包根新增非源码生成文件时需显式加入忽略集，否则会报「outer not in source」 |
| `scripts/preflight.sh` | blob 大小改用 `wc -c`，兼容 BSD/macOS | 修复 B10，恢复 >10 MB 检查 | 无；只改取大小方式 |
| `components/fnos_monitor/fnos_net.c` | 失败状态不再压过在线；迟到 GOT_IP 清同代次 FAILED | 修复 B24，避免在线仍长期显示「未连接」 | 未编译验证；在线但本轮保存失败时不再显示失败原因（顶栏按在线显示） |

未改：`fnos_pair.c`、`fnos_ui.c` 的交互时序、其余 NAS 侧代码（B03–B05、B07–B09、B11–B18、B21–B23 待裁决，避免未经确认的行为变更）。

---

## 6. 验证方式与结果

| 检查 | 命令 | 结果 |
|---|---|---|
| 主机预览（编译 UI 改动 + 四项布局审计） | `FNOS_PREVIEW_OUT=<新目录> bash tools/preview/run.sh <新目录>` | EXIT=0；字形/标签/子对象/点击重叠全 PASS，温度阈值 PASS；`uk.c` 两处保护由该构建实际编译 |
| 历史解析回归 | `python3 tools/test_history.py` | 11 用例 + ASan/UBSan 全 PASS |
| 差异卫生 | `git diff --check` | PASS |
| 打包门禁自测 | `python3 nas/fpk/verify_package_sources.py --self-test` | PASS（含外层匹配/内容不同/缺失三例） |
| 打包门禁实包 | `python3 nas/fpk/verify_package_sources.py` | PASS，`26 files match`（10 个 app + 16 个外层） |
| 提交前闸门 | `bash scripts/preflight.sh` | PASS；11 MB 临时文件判 `flagged=yes` |
| L1 主机回归 | `bash tools/verify_all.sh` | 40 PASS / 2 FAIL（旧 `tools/preview/out` 所有权，环境问题）；重跑干净目录后对应项 PASS |
| L2 固件构建 | `bash idf.sh build` | **未执行**：本机无 ESP-IDF；`verify_all.sh` 报 `Activate ESP-IDF first…` |
| 静态栈预算 | `python3 tools/stack_check.py` | 未重跑：需要新 ELF；本次 UI 新增局部变量约 0.3 KB，建议构建后补跑 |
| 实机 | `bash tools/verify_all.sh --flash --photo --port …` | 未执行（无设备） |

改动后的推荐闭环：`bash idf.sh build` → `python3 tools/stack_check.py` → `VERIFY_OUT=… bash tools/verify_all.sh --ci` → 实机 `--flash --photo`。

---

## 7. 需要确认的问题

1. **Q1（B03）**：明文引导窗口内「局域网攻击者可用深层 JSON 打崩配对任务」是否纳入威胁模型？若纳入，我按「括号深度 ≤16 预检」加最小修复；若不纳入，报告保留为已知边界。
2. **Q2（B04）**：`?token=` 回退是否还有人使用（curl/脚本/第三方）？确认无依赖后建议默认关闭。
3. **Q3（B05）**：是否现在就把 `s_configured/s_ssid` 的更新移到 `esp_wifi_set_config` 成功之后？属 P3 状态一致性问题，改动小但会影响提交失败的显示路径。
4. **Q4（A05）**：是否接受 UI 线程在配对落盘瞬间的短暂阻塞？若不可接受，我加 `fnos_pair_view_try` 并复用到配对卡与系统页。
5. **Q5**：本轮是否只保留这两处修复并提交，还是把 A02（双清单对齐）也纳入？A02 属工程防漂移，改动比 bug 修复大。
6. **Q6**：`docs/code-audit-2026-11-03.*` 是否随提交入库，还是仅作为本次交付文件保留在工作树？
7. **Q7（B07）**：`contract_check.py` 的裸赋值兜底是否本轮收紧？收紧后可能对现有写法产生误报，需要先跑一遍全量确认。
8. **Q8（B11）**：`preflight.sh` 的凭据过滤是否改成「只对命中 token 判占位符」？改完需要在含占位符的样例上做正负控。
9. **Q9（B16–B18）**：三个 P1 交互缺陷是否纳入本轮？其中 B16（详情 UAF）与 B18（Wi-Fi 点错网络）建议修，但都需实机复现验证；B17 需先对照 `health_summary()` 与 `alert_group_of()` 的计数口径，属行为修正。
10. ~~Q10（B24）~~ 已按指示修复：FAILED 仅在本轮未连上时优先，GOT_IP 对同代次 FAILED 清回 IDLE；本机无 IDF，未编译。
11. **Q11（B02 残余）**：历史回填失败目前是永久 1 Hz（72 h 约 26 万次请求）。是否改成 1 s→2 s→…→60 s 退避，或把 404/空数组这类确定性失败直接标记完成？

---

## 附录 A：已核对、未发现缺陷的点

- `strcpy` 全仓仅 `fnos_ui.c` 一处，缓冲区先经 `uk_alloc(strlen+1)`。
- 裸结构体复制仅 `fnos_snapshot.c:70`（copy 的预期实现）。
- `lv_label_set_text_fmt` 使用 0 处，屏上格式符残留风险由预览 `audit_format_residue()` 兜底。
- `pf_pct` 的静态 256 样本缓冲只接收 `pf_push` 截断后的计数，无越界。
- 服务端访问日志主动剥离 query（`nas_companion_server.py:779`），不会把令牌写进 `service.log`。
- 快照解析对 double→整型做夹取，模块缺失用 NaN 表达，不与真实 0 混淆。

## 附录 B：未覆盖 / 未验证

- `managed_components/`、`build/`、生成字体未逐行审。
- 固件未编译、未上板；UI 视觉效果与触控未验。
- 72 h、NVS 掉电、真实 NAS 升级、TLS 换证/撤销：均未验证（与 T04–T06 一致）。
- 两路只读子代理已回：NAS/工装 10 条、UI/ui_kit 8 条，均只读未改文件；主审复核了其中 B06、B10、B12、B16、B19、B20 的代码事实，其余条目保留子代理证据强度，未逐条运行复现。
- UI 子代理未逐行读 `fnos_ui.c` 的 nav 绘制、build_p0–p5 构建体、pair_build、wifi_build、overview_refresh 与 `uk_theme.c`；这些区段只有主审此前针对性核对，不等于全量审过。
