# DSH 整改与主机验证记录（2026-10-11）

> 独立复核提示：本文是 DSH 的历史实施回执，包含未被接受的关闭结论。第二轮已停止 DSH，由 Codex 接管；最新缺陷、重开项及证据见 [独立复核记录](codex-open-items-review-2026-10-11.md)。本文的修复数量不是 Codex 验收数量。

本文件记录在隔离工作树（detached HEAD）上，对 [未闭环清单](open-items-2026-10-11.md)（22 条 H + 60 个 M + 附录 low）
逐条复核、整改与实跑的**真实结果**，含**复核整改轮**（针对主机 Codex 首次复核打回的 8 条缺陷）。
基线 `HEAD = 53e1bf868bee0e2b66b05a8df8df342ba6ee29a4`（启动时 `git status` 为空，无脏文件）。

**一句话结论**：H5/H8/H10/H11/H12/H21 六条待处理项已修并有主机证据，其中 H12 在本轮复核后**重写**了检查
（旧检查把生产超时 monkeypatch 成 0.5s、并把 socket 超时算作"已关闭"，其 PASS 不成立）；H14/H16 收窄到只剩明确残余；
60 个 M 编号全部逐条复核：**56 条修复、1 条复核为误报（M20）、3 条复核确认无需改（M15/M38/M39）**；
附录 low 的 **28 个分组逐组给出处置**（§5），无"整块未处理"。
仍有两项**明确未闭环**：分发用应用包 `.fpk` 未重打包（本批授权明确排除二进制产物）、
滚动条看门狗 18 处历史净距（§7）。**未声称** 30% 性能目标达成、**未声称**真机验收：
本文件所有 PASS 都只代表主机侧（模拟器／桩件／静态分析），真机面见 §7.3。

## 0. 复核整改轮（主机 Codex 打回的 8 条）

| # | 复核缺陷 | 处置 | 证据（命令／rc／关键输出） |
| --- | --- | --- | --- |
| 1 | M31：令牌 fail-closed 破坏默认安装；unit 无 `EnvironmentFile`，安装器从不创建 `/etc/fnos-agent.env`，而文案却称已自动生成 | 新增 `nas/install-remote.sh`（`install`／`rotate-token`／`uninstall [--purge-token]`）：保留已有非空令牌、首次生成 48 hex、`umask 077`+`chmod 600`+原子 `mv`、**从不打印令牌**（只打印 sha256[:8] 指纹）、显式拒绝 `--insecure-no-auth` 的 unit；`fnos-agent.service` 增 `EnvironmentFile=-/etc/fnos-agent.env`；`install.sh` 的本机验证段改为"不带凭据必须 401（200 即失败）"，带令牌时才汇总字段且令牌经 stdin（不进 argv） | `python3 nas/fpk/test_install_token.py` **rc=0**：40 条断言，A 首次安装（令牌 0600、脚本 0755、unit 0644、指纹存在、systemctl reload/enable/restart/is-active、自检 401）、B 升级令牌字节不变且修回 0600、C `rotate-token` 才换、D/E 坏 unit 与非零退出且**不落任何文件**、F 健康检查 200 判失败、G 缺源非零、H 卸载默认保留令牌、I 出厂静态度量、J 令牌从不进命令行。`bash -n nas/install.sh` rc=0 |
| 2 | M25：`config_callback` 在 `service_stop` 失败后仍继续 `apply_config`，旧进程可能用内存旧值覆盖新配置 | 停止失败即 `exit 1`：不写配置、不起第二个实例，并打印"已放弃本次设置修改" | `bash nas/fpk/test_process_lifecycle.sh` **rc=0** 的 S2：`var/config.json` 字节级（shasum）不变、无"设置已生效"、端口未被二次占用；负控（基线脚本）确实改写了配置 |
| 3 | M21/M26：进程仍在时 `stop_process` 就删 PID/socket，`uninstall_callback` 因此看到 `is_running=false`，报"已释放"并 exit 0（假绿）；`abort_started` 同样在确认死亡前删记录 | `cmd/main` 的 `abort_started`/`stop_process`：KILL 后仍存活则**保留** PID 记录与 socket、返回非零并说明；`cmd/uninstall_callback` 用真实存活判定，仍活则 exit 1 且保留记录 | 同测试 S1/S4：rc≠0、"PID 记录保留"、`status` 仍看得到该进程、无"已释放端口与 Socket"；负控（基线脚本）rc=0 谎报已释放。**本轮自证抓到 4 处真 bug**：`cmd/main:119/:162`、`cmd/config_callback:21`、`cmd/uninstall_callback:22` 的 `$VAR（` 多字节展开在 macOS bash 3.2 下会 `unbound variable` 直接炸掉**报错分支自身**（rc=127，正是"失败时再失败一次"），已全部改为 `${VAR}` |
| 4 | H12：`get_request` 仍在单一 accept 线程里等 3s peek + 10s 握手，几条静默/半握手连接就能拖住正常遥测；旧检查 monkeypatch 生产超时并统计口径错误，其 PASS 不成立 | `nas_companion_server.py`：`get_request` 只做 `accept()`；明文探测/TLS 握手移到连接处理路径，配并发上限（`BoundedSemaphore`）与有界超时（peek 3.0s／握手 10.0s），失败路径安静关连接并记日志，明文兼容与真实 TLS 都保留 | 重写 `nas/fpk/tls_accept_check.py`（真证书、**生产**超时值、并发静默/半握手、明文与 TLS 并存、按 EOF/RST 判"真实关闭"、探针上限拒绝、两条负控）**rc=0**：静默连接 3.0~3.0s 关闭、半握手 10.0~10.0s 关闭、8 条超限连接 0.30~0.31s 被拒而其余 32 条走 peek 超时、负载下明文 0.004s／TLS 0.002s 仍 200、坏 TLS 客户端被关且服务继续应答；负控 1（旧 `get_request`）2.5s 内拿不到响应，负控 2（纯 listen 不关）被判 `timeout` |
| 5 | M11：新增的 `ui_retry_task` 跑完整 `fnos_ui_create` 却不在栈预算清单里 | `tools/stack_check.py` 的 `ROOTS` 增加 `ui_retry_task`（16 KiB、回调上限 0），注释写明它与 `app_main` 是同一条建 UI 路径；`main/main.cpp:191` 的注释改为与实现一致的"按 5/10/20/40/60 秒退避重试 5 次（约 2 分 15 秒）" | 重建后 `python3 tools/stack_check.py` **rc=0**（新 ELF 5060 个函数）：`ui_retry_task 静态最深 3952 B / 16384 B = 24%`，`app_main 4096/16384`、`lvgl_worker 3856/12288`、`poll_task 2736/6144`、`pair_task 3088/7168` 全部在预算内 |
| 6 | M36：`run.sh` 只清 5 个环境变量，`PREVIEW_HARDWARE_FIXTURE`、`PREVIEW_ALLOC_FAIL_AT` 等分支变量会从调用方漏进来，OOM 阶段尤其 | `tools/preview/run.sh` 声明全部 **27** 个 `PREVIEW_*` 分支变量，`preview_env_args()` 在每个调用点用 `env -u` 逐阶段隔离，并加 getenv 清单漂移检查（c 源码 vs 脚本清单，不一致即 rc=1） | `bash tools/preview/test_run_guard.sh` **rc=0**：脏环境（27 个变量全部设成敌意值）下 rc=0、84 帧、5 条硬件矩阵 PASS；日志证明 **6 个阶段**各自只拿到自己设的变量（主渲染 0 个、4 个夹具各 1 个、OOM 2 个），"没有阶段拿到调用方残留的分支变量"；负控：基线脚本 27/27 全部可漏 |
| 7 | M57：`run.sh` 拒绝 `..` 与绝对路径，但输出目录的父目录若是软链就能指向仓库外，`rm -rf` 会删到别处 | `run.sh` 先用 `pwd -P` 取真实工作目录，再用 `realpath` 求输出目录**真实路径**，要求它落在允许目录内且不等于该目录本身，`rm -rf "${OUT_REAL:?}"` | 同测试：绝对路径、`..`、`../..`、`.`、路径中段 `..`、`/` 全部 exit 2；`guard-link/sub`（`guard-link` 是指向临时哨兵目录的软链）被拒，且哨兵文件与子目录**内容与存在性均未变** |
| 8 | 文档：H1–H22／M01–M60／附录 low 需要逐条处置、M20 重复计数、声称与实修不符、不得把主机 PASS 当设备验收、18 处 scroll_gap／215 处未判定／过期 FPK 要如实保留、不得出现个人绝对路径或内网地址 | 本文件 §3（22 条 H）、§4（60 条 M）、§5（附录 low **28 个分组逐组**）给出处置；统计口径修正为 **56 修复 + 1 误报 + 3 复核确认 = 60**；新增 §0（复核整改轮）与 §6.4（新回归实跑）；§7 保留全部未闭环项并明确"主机 PASS ≠ 真机验收" | 本文件不含个人绝对路径与内网地址；§7 如实列出 `.fpk` 过期、18 处净距、215 处未判定 |

复核项还带来两条工程性修正：**(a)** `tools/test_report.sh` 增加 3 行新回归、`tools/verify_all.sh` 增加 `L1.6 本轮修复回归` 步骤，
三条回归都只跑在临时目录里（桩 `kill`／桩 `systemctl`／派生的预览副本），不碰真 NAS、真服务或仓库外数据；
**(b)** 新增一个按 shebang 扫描全仓的"多字节变量展开"lint（原 lint 只看 `*.sh`，因此漏掉了无扩展名的 `cmd/*` 脚本——
本轮 4 处真 bug 就是这么漏过去的），并把 `nas/` 与仓库根纳入扫描范围。

## 1. 环境、边界与不做的事

| 项目 | 实际值 |
| --- | --- |
| 工作目录 | 隔离 worktree（detached HEAD，路径不含于本文档） |
| 基线 | `53e1bf868bee0e2b66b05a8df8df342ba6ee29a4`，起始工作树干净 |
| 固件工具链 | ESP-IDF 5.5.3，`esp-14.2.0_20251107`（riscv32-esp），经 `./idf.sh build`（激活方式见 §8） |
| 主机预览 | `bash tools/preview/run.sh`（84 帧 PNG + 4 类几何审计 + 4 组硬件夹具 + OOM） |
| 打包工具 | `fnpack` 存在于 `PATH`（`command -v fnpack`），但本批**不产出新二进制** |
| 未触碰 | `fnos_config.h`、字体生成物、`tools/patches/**`、`docs/archive/**`、原审计 `docs/full-repo-review-2026-10-10.md`、仓库内 `.fpk`、厂商显示主路径 |
| 无 | 真实 NAS、开发板、串口、摄像头、凭据、私密配置、会话 |

所有运行日志都在 `/tmp`（如 `/tmp/fnos-idf-final.log`、`/tmp/fnos-tls-accept3.log`、`/tmp/fnos-proc-lifecycle.log`、
`/tmp/fnos-preview-guard.log`、`/tmp/fnos-install-token.log`），仓库内不留运行噪声。

## 2. 改动清单

`git status --porcelain` 与 `git diff --numstat` 为准（本批授权上限 150 文件 / 30000 行）。
本轮（复核整改）新增文件：`nas/install-remote.sh`、`nas/fpk/test_install_token.py`、`nas/fpk/test_process_lifecycle.sh`、
`tools/preview/test_run_guard.sh`；`nas/fpk/tls_accept_check.py` 为第二轮新增并在本轮重写。

固件与 UI：`components/fnos_monitor/fnos_snapshot.c`、`fnos_data.c`、`fnos_data.h`、`fnos_net.c`、`fnos_net.h`、
`fnos_pair.c`、`fnos_pair.h`、`fnos_ui.c`、`fnos_wifi_cli.c`、`fnos_wifi_store.c`、`fnos_wifi_store.h`、
`ui_kit/uk.h`、`main/main.cpp`、`main/CMakeLists.txt`、`sdkconfig.defaults`。

NAS 侧：`nas/fnos-agent.py`、`nas/fnos-agent.service`、`nas/install.sh`、`nas/install-remote.sh`、
`nas/fpk/README.md`、`nas/fpk/build.sh`、`nas/fpk/test_lifecycle.sh`（含多字节 lint 扩范围）、
`nas/fpk/{api_check,board_sim,collector_check,contract_check,devmode_check,perm_check,wizard_check}.py`、
`nas/fpk/tls_accept_check.py`、`nas/fpk/test_install_token.py`、`nas/fpk/test_process_lifecycle.sh`、
`nas/fpk/nasscreencompanion/cmd/{main,_common.sh,config_callback,config_init,install_callback,install_init,uninstall_callback}`、
`nas/fpk/nasscreencompanion/wizard/{config,install}`、
`nas/fpk/nasscreencompanion/app/server/{nas_companion_server.py,docker_api.py,fnos_collector.py}`、
`nas/fpk/nasscreencompanion/app/ui/www/index.html`。

工具与文档：`tools/{certcheck/main.c,parsecheck/extract.py,parsecheck/main.c,freshness_check.py,perf_table.py,photo_check.py,plan_check.py,serial_capture.py,soak_check.py,test_report.sh,verify_all.sh}`、
`tools/preview/{preview.c,run.sh,scroll_gap.js,pair_msgs.py,hardware_fixtures.py,palettes.sh,snapshot_check.c,CMakeLists.txt,stub/esp_heap_caps.h,stub/sdkconfig.h}`、
`tools/preview/test_run_guard.sh`、`tools/stack_check.py`、`tools/audit125/{README.md,test_raid.py}`、
`README.md`、`docs/{dsh-open-items-results-2026-10-11.md,fnos-companion-app-plan.md,open-items-2026-10-11.md,review-v1.2.5-audit-2026-10-10.md,verification.md}`。

## 3. H1–H22 逐条状态

"主机验证通过"＝有本轮实跑证据但不含真机；"本轮已修"＝本轮改动 + 本轮实跑证据；"部分处理"＝有明确残余子项。

| ID | 当前状态 | 证据／原因 |
| --- | --- | --- |
| H1 | 主机验证通过 | 首帧先选导航再刷新（`NAV_TXT[-1]`）；主机预览 84 帧全过 |
| H2 | 主机验证通过 | 提交时保留历史元数据（同一把锁）；`docs/fnos-companion-test-report.md` 回归通过 |
| H3 | 主机验证通过 | KPI 分配统一失败通道；预览 OOM 夹具通过 |
| H4 | 主机验证通过 | PEM 有序边界；`bash tools/certcheck/run.sh` 通过 |
| H5 | 本轮已修（主机验证） | `fnos_collector.py` 的 `normalize_snapshot_types()` 不再把 `zfs` 归入列表类强转，改 dict/null 契约；`nas/fpk/collector_check.py` rc=0 |
| H6 | 主机验证通过 | 四种清单 + OOM 通过（84 帧再跑） |
| H7 | 主机验证通过 | 空清单防护；`bash tools/parsecheck/run.sh` rc=0 |
| H8 | 本轮已修（主机验证） | `tools/serial_capture.py` 收尾与 `--reset` 真正调 `hard_reset(ser)`（原漏参被宽 `except` 吞掉），失败打印 esptool 命令；未对真实设备复位 |
| H9 | 主机验证通过 | 72h 评价器判据回归 55 项通过（`tools/audit125/test_soak72.py`）；72h 本身未做（O03） |
| H10 | 本轮已修 | `README.md` 对齐 1.2.5、83741 字节与实测 SHA `afed3abd…cc35`、mermaid 版本、run-as 说明 |
| H11 | 本轮已修 | `docs/review-v1.2.5-audit-2026-10-10.md` §0 与 `tools/audit125/README.md` 加"2026-10-11 更正"：两份补丁与审计用例都在本仓且可运行 |
| H12 | **本轮已修（主机验证，检查已重写）** | accept 线程只 `accept()`；探针/握手在连接处理路径，有界超时 3.0s/10.0s + 并发上限；`nas/fpk/tls_accept_check.py` rc=0（真证书、生产超时、并发静默/半握手、明文与 TLS 并存、EOF/RST 判关闭、两条负控）。真机 TLS 链路仍待 O06 |
| H13 | 主机验证通过 | 证书工具失败/空指纹非零；`certcheck` 再跑通过 |
| H14 | 部分处理 | 覆盖率改为逐卡报告：44 处可判定、**215 处如实记"未判定"**；仍余 **18 处**历史净距（基线同 18 处），须先做与位置无关的轨道识别，实验与完整回退记录见 §7.2 |
| H15 | 主机验证通过 | 页面异步错误非零退出；M24 补的管理页错误可见性 |
| H16 | 部分处理 | `board_sim.py`：TLS-off 非零、缺 `cert_pem` 判失败、自算指纹不算通过；真实链路仍需 O06 |
| H17 | 主机验证通过 | 内存样本不足独立非零码；`mem_check` 3000 请求实跑 |
| H18 | 主机验证通过 | objdump 读取失败拒绝；本轮 `tools/stack_check.py` rc=0 |
| H19 | 主机验证通过 | 缺任务入口非零；本轮栈检查**五个**入口齐全（含 `ui_retry_task`） |
| H20 | 主机验证通过 | 故障注入报告退出码；`test_report.sh --full` 实跑 |
| H21 | 本轮已修（主机验证） | `docker_api.py` 新增 `row_from_container()`：保留完整容器名 + 稳定 12 位 `id`，不再 `[:23]` 截断；契约与采集端检查同步 |
| H22 | 主机验证通过 | 供给包与空字段校验；`python3 tools/test_manifest_failures.py` 6 项通过 |

**归类**：原 6 个待处理编号（H5/H8/H10/H11/H12/H21）本轮修复并有主机证据；H14/H16 只剩明确残余。
这不代表"只剩 1–2 个产品缺陷"：编号可含多个子问题，真机／发布／长测仍全部未闭环（§7）。

## 4. M01–M60 逐条状态

| ID | 状态 | 处置 |
| --- | --- | --- |
| M01 | 已修 | 新增 `fnos_pair_active_gen()`：配置与代次在同一把锁内读出，`fnos_data.c` 改用它，消除 TOCTOU |
| M02 | 已修 | 摘要补 `state/speed_mbps/physical`；`nas/fnos-agent.py` 与 `fnos_collector.py` 的空回退也补齐同形字段 |
| M03 | 已修 | 新增 `clampd/inum/unum/lnum/fnum`，全部数值字段经类型化钳制（NaN→下界） |
| M04 | 已修 | `fnos_wifi_store` 的保存返回 `clear/save` 真实结果，擦除失败记日志 |
| M05 | 已修 | `fnos_net.h` 契约注释：仅 LVGL 线程写、缓存所有权与锁协议 |
| M06 | 已修 | `wifi set/clear/show` 改整 token 比较，`wifi set foo` 不再误命中 `foobar` |
| M07 | 已修 | 诊断 CLI 超长行（≥191 字节且无换行）整行丢弃并提示 |
| M08 | 已修 | 日志级别窗口移入 `fnos_pair_tls_probe()`；`tls` 命令不再改全局级别 |
| M09 | 已修 | 扫描缓存加锁（`s_ap_mux`）并写清读写方 |
| M10 | 已修 | `reason_text()` 补 211/212/213 中文原因 |
| M11 | 已修（本轮加强） | `bsp_display_lock` 失败不再静默、退避重试并记录；**本轮**把 `ui_retry_task` 纳入 `tools/stack_check.py` 的 ROOTS 并重建实测 3952/16384，注释与 5 次退避实现对齐 |
| M12 | 已修 | `uk_set_text` 加 `__attribute__((format(printf,2,3)))`，暴露的实参问题已修 |
| M13 | 已修 | `sdkconfig.defaults` 删除冲突的 `CONFIG_LV_USE_CLIB_MALLOC`，保留 `LV_USE_CUSTOM_MALLOC`；本轮 IDF 构建无 kconfgen 覆盖告警 |
| M14 | 已修 | `main/CMakeLists.txt` 补 `esp_timer` 依赖 |
| M15 | 复核确认 | `tools/preview/CMakeLists.txt` 导出正则已含 `SNAPSHOT_MAX_BYTES`，无需改 |
| M16 | 已修 | parsecheck 补 UTF-8 字段、512KB 截断显式失败、argv 边界；抠函数清单同步 `clampd/inum/unum/lnum/fnum`；certcheck 区分"目录输入/输入过大"；photo_check 校验 PNG 头/位深/交错/色型；serial_capture 改 `monotonic`、限缓冲；snapshot_check 检查 `cJSON_PrintUnformatted`；stub 堆常量按 IDF 5.5.3 真值修正 |
| M17 | 已修 | `preview.c` 副作用断言改 `if(!…)abort()`；5 个函数补 `fnos_status_release()`；`-UNDEBUG` 在位 |
| M18 | 已修 | `docs/fnos-companion-app-plan.md`、`nas/fpk/README.md` 的 run-as 措辞与 `config/privilege`+降权实现一致 |
| M19 | 已修 | 四处 UI：量宽字体与渲染字体一致、明文态凭据提示、OOM 可见提示、卡片页归属按祖先链 |
| M20 | **复核为误报**（不改代码） | 内核 `raid1.c` 的 `[%d/%d]` 首个参数是**成员总数**（真实 `[2/1]` 即 2 成员缺 1），采集端 `want=总数/have=在役` 正确；`tools/audit125/test_raid.py` 补 4 条断言钉住语义（33 项通过）。此条**不计入 56 条修复** |
| M21 | 已修（本轮加强） | `cmd/main` 生命周期：KILL 后仍存活则**保留** PID 记录与 socket、返回非零（原假绿）；本轮另修 `cmd/main:119/:162` 两处多字节展开 bug，并有 S1/S4 回归 + 基线负控 |
| M22 | 已修 | `build.sh` 打包后校验不再 `\|\| true` 假绿 |
| M23 | 已修 | `contract_check.py` 死检查由 `RX_BUF_SIZE` 改为 `HIST_BUF_SIZE`，不再无条件打 ✓ |
| M24 | 已修 | 管理页 `get/post` 捕获网络异常；`refresh/loadLogs/cancelcode/rundiag/revoke` 都有可见失败文案（`node --check` 通过） |
| M25 | 已修（本轮加强） | `config_callback` 停止失败即 `exit 1`：不写配置、不起第二实例（原"仍继续写配置"）；S2 断言配置字节不变 + 基线负控证明旧脚本会改写 |
| M26 | 已修（本轮加强） | `uninstall_callback` 按真实存活判定：进程还在则 exit 1 且保留记录，不再无条件宣称"已释放端口与 Socket" |
| M27 | 已修 | `wizard/config` 端口校验覆盖 0/越界/合法边界（原审计"80 这类 1 位端口"系表述错误） |
| M28 | 已修 | `palettes.sh` 校验 `KK_PALETTES` 每项并 `rm -rf "${OUT:?}/${p}"` |
| M29 | 已修 | `verify_all.sh` 改用 preview 真实英文输出匹配，"越界/压叠"与"字形缺口"分开归因 |
| M30 | 已修 | 栈超预算先看 `超出预算`，不再误诊为"环境没激活" |
| M31 | 已修（本轮重建） | 采集器无 `FNAS_TOKEN` 拒绝启动，需显式 `--insecure-no-auth`；**本轮**新增 `nas/install-remote.sh` 打通默认安装/升级/轮换/卸载：令牌自动生成 0600 且保留旧值、从不打印、坏 unit 拒绝安装；`install.sh` 验证段改为"不带凭据必须 401"；40 条断言回归 rc=0 |
| M32 | 已修 | `?n=` 历史回填不再静默失败 |
| M33 | 已修 | `_read_json` 坏请求体返回错误而非 `{}` |
| M34 | 已修 | 非 `KeyboardInterrupt` 退出路径也走 `finally` 落盘 |
| M35 | 已修 | `hardware_fixtures.py` 可表达"采集端省略"，mixed/dense 夹具真产出 `source_dropped` |
| M36 | 已修（本轮加强） | `run.sh` 按**27 个**分支变量逐阶段 `env -u` 隔离 + getenv 清单漂移检查 + ≥84 帧门；脏环境回归 rc=0（6 阶段互不泄漏），基线负控 27/27 可漏 |
| M37 | 已修 | `pair_msgs.py` 缺失状态即失败；`FETCHING` 例外有源码依据（源码里本就没有该状态的文案入口） |
| M38 | 复核确认（首轮已修） | 概览断言不再整块子串搜索 |
| M39 | 复核确认（首轮已修） | 点击表不再硬编码 5 个 id |
| M40 | 已修 | 未登记端点不再计入"字段名都认"，并报未覆盖数 |
| M41 | 已修 | `board_sim.py` 非 JSON 响应打印前 80 字节，不裸 traceback |
| M42 | 已修 | 无法求值的规则判失败；数值 min/max 按数值比较 |
| M43 | 已修 | `config_init` 端口预检 |
| M44 | 已修 | `_common.sh` 与 `wizard/install` 的端口处理一致 |
| M45 | 已修 | `nas/install.sh` 远端 `$STAGE` 全部加引号 + mktemp 形状白名单 |
| M46 | 已修 | `test_lifecycle.sh` 断言下限感知跳过（`SKIPPED`），缺工具不再报成"有用例没跑" |
| M47 | 已修 | `verify_all.sh` 的 ppm2png 失败记 fail，不再消失 |
| M48 | 已修 | `plan_check.py` 的 ROOTS 拼接仓库根；缺失报告/字段即记问题 |
| M49 | 已修 | `freshness_check.py` 白名单覆盖构建输入（`*.txt/*.yml/Kconfig/*.csv/*.ld`） |
| M50 | 已修 | `soak_check.py` 三种状态（0 在跑且健康／1 有问题／2 未判定），staged 副本缺失与样本不足都记问题；`test_report.sh` 把"没在跑"写成**未判定**行 |
| M51 | 已修 | `test_report.sh` 跳过也记行；`plan_check` 移到报告重写之后 |
| M52 | 已修 | `perf_table.py` 缺列/缺参给 `--` 与用法提示，不抛异常 |
| M53 | 已修 | `install_callback` 与 `_common.sh` 契约一致 |
| M54 | 已修 | `install_init` 同批一致化 |
| M55 | 已修 | `devmode_check.py` 打印 `install_callback` 的返回码与 stderr |
| M56 | 已修 | `perm_check.py` 用真目录做目录用例 |
| M57 | 已修（本轮加强） | `run.sh` 拒绝 `..`／绝对路径／空之外，**本轮**用 `pwd -P`+`realpath` 校验真实父路径必须落在允许目录内，软链越界被拒且哨兵数据未被删除 |
| M58 | 已修 | FUSE 判断改前缀匹配（`fuse.*`），未列名子类型不再穿透到 `os.statvfs` |
| M59 | 已修 | `release_check.sh` 配置挪动失败即非零；`bash tools/test_report.sh --full` 实跑该行 ✓ |
| M60 | 已修 | `mem_check.py` 用已配对/带令牌配置，3000 请求真正打到 payload 分支 |

**统计（口径已修正）**：**56 条修复**（含本轮加强的 M11/M21/M25/M26/M31/M36/M57）＋**1 条复核为误报**（M20）＋**3 条复核确认无需改**（M15/M38/M39）＝ **60**。
此前版本写成"55 修复 + 1 误报 + 4 复核确认"，把 M20 同时算进两类（重复计数）、又把 M60 记成"复核确认"而其实际是修复，现已改正。

## 5. 附录 low 分组逐组处置

对应 [清单附录 B](open-items-2026-10-11.md) 的 28 个分组，逐组给出处置，不留"整块未处理"。
"未处理"一律带**具体原因**与下一轮入口；"复核为非缺陷/已澄清"给出依据文件与行为。

| 分组 | 处置 | 依据／原因 |
| --- | --- | --- |
| ui_kit 死 API／主题双真值 | 未处理（需产品决策） | 仓库内无调用者，但这些是公开 API/颜色宏，删除会动外部合同；需先确认真实调用方与主题双真值口径。入口 `components/fnos_monitor/ui_kit/uk.h`、`uk_theme.h` |
| 头文件契约 | 部分已修 | `fnos_net.h` 的线程/所有权/锁协议注释已补（M05）；列数·data_fit、调色板数量、guard、时间戳语义、HTTP/TLS 双端口口径仍待逐文件对齐（`fnos_data.h`/`uk.h`/`fnos_pair.h`） |
| 低危数据／并发 | 部分已修 | M01（配对代次同锁读）、M09（扫描缓存加锁）已修；perf 命令改输入、夜间标志跨任务、NAS 时间上界、历史回填顺序**未触发验证**——主机 sanitizer 通过不能证明竞态不存在 |
| UI 细节 | 部分已修 | M19 覆盖量宽/渲染字体一致、明文凭据提示、OOM 可见提示、卡片祖先页号；Wi-Fi 口令清理与 reveal 未处理（要兼顾 LVGL 标签副本，本机无验证路径） |
| 数字动画 | 未处理（超出本批授权） | 需按实测宽度回归并给预算耗尽的可观测性；改动落在 UI 主路径，本批明确不含厂商显示主路径重写 |
| 硬件 fixture | 部分已修 | M35 已让 mixed/dense 夹具真产出 `source_dropped`；RAID dense 的正常 `health/what` 与维护状态变体未补，入口 `tools/preview/hardware_fixtures.py` |
| 快照／主机对象生命周期 | 部分已修 | M17 已补 5 处 `fnos_status_release()` 并把副作用断言改成 `if(!…)abort()`；`SEEN_MAX` 诊断边界未处理；macOS 无 LeakSanitizer，不能据此宣称无泄漏 |
| FPK shutdown／日志 | 未处理（需先核实际调用） | shutdown 只关单监听器、每秒遥测刷日志、URL token 进日志三项都要先核真实调用点与脱敏策略；板端走 `X-Token` 头不代表其它调用安全 |
| FPK 设备并发／TLS 降级 | 部分已修 | M08（日志级别窗口）与 H12（TLS 接入超时+并发上限）已修；`revoke/check_token/note_seen` 锁协议与"证书失败后明文启动而配置仍 TLS"的失败策略未做，需故障/并发回归 |
| 网络摘要回退 | **已修** | M02/H5：`nas/fnos-agent.py` 与 `fnos_collector.py` 的回退摘要都补齐 `state/speed_mbps/physical` |
| 图像产物真实性 | 部分已修 | M47（ppm2png 失败记 fail）、M28（逐项校验 + 引号保护）已修；PPM EOF 死循环、长度/格式不校验、空目录成功仍未做——需损坏/截断/空目录负向用例 |
| 滚动／页面断言细节 | 部分已修 | M38/M39（概览断言、点击表）已修，H14 的覆盖率改为逐卡报告（44/215/18）；文件头阈值与实际阈值不一致、`seq=0` 自证未做，前提是先做与位置无关的轨道识别（§7.2） |
| 配置保留测试 | **已修（本轮）** | 新增 `nas/fpk/test_process_lifecycle.sh`：S2 断言 `$PKG_VAR/config.json` 字节级不变，并用基线脚本作负控证明它会改写。**此前该断言打在包根 `config.json` 上，属假通过**——真实配置在 `var/config.json` |
| 栈分析能力 | 部分已修 | M11 把 `ui_retry_task` 纳入预算（3952/16384）；只认 addi 栈帧、大帧寄存器减法、回调/中断未计入仍未做，静态结果仍是下界（`stack_check.py` 输出里已写明） |
| 工具链／验证入口 | 已修 | M29/M30/M47/M49/M51，本轮再加 `verify_all.sh` 的 `L1.6` 与新回归；脚本按 `FNOS_IDF_ENV`/`IDF_PATH` 激活，不硬编码个人路径 |
| perf/reason/parse 工具 | 部分已修 | M52 已修（缺列/缺参给 `--` 与用法）；不完整 bench 行计入、reason 两侧零集合自证、历史环长期待值硬编码未做 |
| collector mock | **复核为非缺陷** | `nas/fpk/collector_check.py:29` patch `module.time.monotonic`，而被测的 `_net()`（`fnos_collector.py:286`）与磁盘速率（`:425`）用的就是 `time.monotonic()`；`time.time()` 只服务快照 `ts` 与循环节拍（`:187/:198/:200`），不参与速率换算 |
| 升级生命周期 | 部分已修 | M34（退出路径落盘）、M22（打包校验不假绿）已修；注释与"先停后备份"顺序不一致、同版本覆盖回滚点、FLAG 写失败静默未做——需真 NAS 升级验证（O01） |
| 卸载令牌口径 | **复核为非缺陷（就现状而言）** | `cmd/uninstall_callback` 全文无"已删令牌"表述（`grep` 无命中），只在真正确认进程退出后才说"已释放端口与 Socket"；令牌保留策略落在 `nas/install-remote.sh uninstall`：默认保留、`--purge-token` 才删，且有断言。平台是否清数据仍待真机（O01） |
| 安装预检 | 部分已修 | M43/M44（端口预检一致）、M45（白名单+引号）已修；本批重写的本机验证段不再用 `A && B \|\| C` 串联，而是先取 HTTP 码再显式比较（`\|\| true` 只防 `set -e` 中断）。"只探 loopback、漏网卡地址占用"未做，需真机绑定矩阵 |
| 拍屏工具 | 未处理（明确原因） | `tools/page_shot.py:73` 未核 capture 返回码/图像路径，坏 JSON 会抛 traceback；但该脚本必须连真串口 + capture-board 才能执行，本批无设备、改了也拿不到证据 → 归入真机面（O01/O08）。**保存 PNG 不等于物理验收通过** |
| preview 配置 | 部分已修 | M15（导出正则含 `SNAPSHOT_MAX_BYTES`）、M16（stub 堆常量按 IDF 5.5.3 真值）已修；LVGL 配置缺失时无响亮兜底仍未做 |
| docker/devmode 检查 | 部分已修 | M42、M55 已修；`finally os._exit(0)` 掩盖异常、accept 无超时、临时树/进程收尾未做 |
| 解释器／路径文档 | 已修 | M18/H10/H11 已修；本文档与脚本不打印个人路径，激活方式只写变量名（§8） |
| cert_info 未覆盖 | 已澄清（未新增覆盖） | `tools/certcheck/extract.py:27` 明确"只验 `pem_fingerprint`：`cert_info` 依赖 mbedtls 的 x509 整套"，文档不再承诺覆盖它；真正纳入需 mbedtls 主机环境 |
| SSH 信任边界 | 未处理（且不擅自改） | `nas/install.sh` 的 ssh/scp 仍依赖操作者已确认的主机身份；本批授权禁止连接或修改真 NAS，需操作者提供已知主机与传输策略后再动 |
| 对外文档 | 部分已修 | H10（版本/包大小/SHA/mermaid）、M16（旧 `RX_BUF_SIZE` 引用）、M49/M51（报告对账）已修；测试计数与包体积须在**最终候选**上一次性对齐，本批不重打包（§7.1） |
| 本轮已修的 low 子项 | **已修** | `test_report.sh` 的 grep 计数、`release_check.sh` 短值/缺定义扫描（M59）；同组其它条目不因此自动关闭（逐条见上） |

## 6. 本轮实跑检查与返回码

### 6.1 固件、预览与工具链

| 命令 | 返回码 | 关键输出 |
| --- | --- | --- |
| `./idf.sh build`（IDF 5.5.3 / esp-14.2.0_20251107，**复核整改轮重建**） | 0 | `fnos_monitor.bin binary size 0x6ad8c0 bytes … 0x252740 bytes (26%) free`，无 kconfgen 覆盖告警 |
| `python3 tools/stack_check.py`（新 ELF，5060 函数） | 0 | `全部入口在预算内`：app_main 4096/16384、lvgl_worker 3856/12288、poll_task 2736/6144、pair_task 3088/7168、**ui_retry_task 3952/16384** |
| `bash tools/preview/run.sh` | 0 | 84 张 PNG；4 类几何审计 + 4 组硬件夹具 + OOM + `snapshot_check` 全部执行 |
| `node tools/preview/scroll_gap.js tools/preview/out` | **1** | 44 处可判定、**215 处未判定**、**18 处不合格**（残余，见 §7.2） |
| `python3 tools/freshness_check.py` | **1** | 固件 ✓、预览 ✓、**NAS 应用包 ✗**（源码改动未重打包，见 §7.1） |
| `python3 tools/plan_check.py` | 0 | 字段数 62、包大小 83741、断言条数对账通过 |
| `python3 tools/reason_check.py` | 0 | 12 个 reason 标签都有中文文案 |

### 6.2 NAS 侧契约与采集端

| 命令 | 返回码 | 关键输出 |
| --- | --- | --- |
| `python3 nas/fpk/contract_check.py` | 0 | `固件读了 62 个字段；真实响应 24 个 + 采集端源码核对`；H21 后 docker 行形状移到 `row_from_container()`，核对范围已同步 |
| `python3 nas/fpk/api_check.py` / `collector_check.py` / `perm_check.py` / `wizard_check.py` | 0 / 0 / 0 / 0 | 请求体字段登记、完整清单与类型契约、目录用例、规则可求值 |
| `python3 nas/fpk/tls_accept_check.py`（**本轮重写**） | 0 | 生产超时 3.0s/10.0s；静默 3.0s、半握手 10.0s 关闭；探针上限拒绝 8 条（0.30s）而 32 条走超时；负载下明文/TLS 仍 200；两条负控 |
| `python3 nas/fpk/board_sim.py`（无 `--host/--port`） | 2 | 明确拒绝：本机没有真 NAS，不算通过 |
| `python3 tools/audit125/test_raid.py` / `test_pair_nvs.py` / `test_diag.py` / `test_soak72.py` | 0 / 0 / 0 / 0 | `PASS 33` / `PASS 37` / `PASS 162` / `PASS 55` 项 |
| `python3 tools/test_gate_failures.py` / `test_manifest_failures.py` | 0 / 0 | `Ran 12 tests … OK` / `Ran 6 tests … OK` |

### 6.3 汇总入口与"三种状态"

| 命令 | 返回码 | 关键输出 |
| --- | --- | --- |
| `bash nas/fpk/test_lifecycle.sh` | 0 | 端到端（打包→真机布局解包→安装/启动/改设置/升级/卸载）全通过；基线 `53e1bf8` 为 142 通过 / 1 失败（唯一失败是本批修掉的 `test_report.sh` 多字节 lint） |
| `bash tools/test_report.sh --full` | **1** | 全部 ✓，1 条**未判定**（长测没在跑），1 条 ✗（NAS 应用包过期，§7.1）。报告照常写出 `docs/fnos-companion-test-report.md` |
| `python3 tools/soak_check.py` | **2** | `长测未在运行（…）——未判定，不算失败`；旧采样不能当本次候选的证据 |
| `bash tools/verify_all.sh` | **1** | 唯一 FAIL 是滚动条间隙审计（H14 残余 44/215/18）；预览、`snapshot_check`、几何审计、NAS 侧检查、固件构建、栈深、新增 `L1.6` 三条回归全部 PASS，板子模拟器如实 SKIP |
| Release/NDEBUG 预览构建（`-DCMAKE_BUILD_TYPE=Release`，`-UNDEBUG` 仍在位） | 0 | Release 下 84 帧、无断言失败；与 `if(!…)abort()` 一起兜住"副作用写在 assert 里"的风险 |

### 6.4 复核整改轮新增回归（全部在临时目录用桩件运行）

| 命令 | 返回码 | 关键输出 |
| --- | --- | --- |
| `python3 nas/fpk/test_install_token.py` | 0 | 40 条断言（A–J）：首次安装/升级保留令牌/轮换/坏 unit 不落文件/健康检查 200 判失败/缺源/卸载保留/静态度量/令牌不进 argv |
| `bash nas/fpk/test_process_lifecycle.sh` | 0 | S1 停止失败保留记录且不谎报、S2 配置字节不变且不起第二实例、S3 正常停止照旧、S4 启动失败保留记录；每项都带基线负控（旧脚本确实假绿） |
| `bash tools/preview/test_run_guard.sh` | 0 | 6 个输出目录守卫用例 + 软链越界哨兵存活 + 27 变量按阶段隔离（脏环境 84 帧 / 5 条 PASS）+ 基线负控 27/27 可漏 |
| `python3 tools/stack_check.py`（M11 新入口） | 0 | 见 §6.1 |

三条已接入 `tools/test_report.sh` 与 `tools/verify_all.sh` 的 `L1.6`，便于下一轮直接复跑。

## 7. 未闭环项与阻塞（不掩盖）

1. **应用包未重打包（O04/O05 交付前置）**：`tools/freshness_check.py` 里唯一过期产物就是
   `nas/fpk/nasscreencompanion.fpk`（包内文件比源码旧；固件与主机预览都 ✓）。本批授权明确排除二进制/FPK 产物，
   因此**不重打包、不声称包内含本批修复**。授权后执行 `(cd nas/fpk && fnpack build --directory nasscreencompanion)`
   再核对 README 的版本/大小/SHA。注意 `nas/fpk/test_lifecycle.sh` 自己会用当前源码现打临时包，
   所以 `cmd/**`、`app/**`、`nas/install-remote.sh` 的修复**已被端到端跑过**；未进包的是仓库里那个分发用 `.fpk`。
2. **滚动条看门狗 18 处历史净距（H14 残余）**：全部是同一类——轨道右缘距卡片外缘 3px，判据要求 ≥4px；
   涉及页 02-live-p3、03-offline-p3、04-healthy-p3、05-limits-p3、06-offline-tls-p3、06-system-scroll、
   08-night-p3、09-legacy-p3。**不是本批回归**：用本批卡片几何去审基线（53e1bf8）的 PNG 得到完全相同的 44/215/18。
   已试过一版修法（三处容器的 `LV_PART_SCROLLBAR` 加 2px 内缩）：结果是 24 条净距 15→11px、18 条 3→2px 且
   "内容→条"出现 18 处 2px——说明看门狗的轨道识别窗（`x+w-80 … x+w-3`）会把移出窗口后的其它竖线当成轨道，
   判据本身对条子位置敏感。该改动**已完整回退**（`uk.c`/`uk_theme.h` 回到基线，`fnos_ui.c` 无残留），
   数据留在这里供下一轮：先把"轨道识别"做成与位置无关，再谈放宽/收紧阈值。
3. **真机面**：O01/O02/O03/O06/O07/O08/O09/O10 全部仍为 INCOMPLETE／blocked；72h 采集未开始；
   30% 目标未声称达成；物理屏与实机安装/升级/卸载未验收。**本文档所有 PASS 都只到主机侧为止**。
   需要设备才能闭环的条目（拍屏工具、SSH 信任边界、loopback 之外的本机绑定矩阵、TLS 真实链路）已在 §5 写明原因。
4. **需产品决策**：`FNOS_PAIR_FETCHING` 死状态（删或接）、ui_kit 公开 API 清理范围。本轮只记录、不动。

## 8. 复现命令

```bash
# 固件构建：先按你的环境激活 ESP-IDF（export IDF_PATH=<...>），或给一个激活脚本路径：
#   export FNOS_IDF_ENV=<idf 激活脚本>；再 ./idf.sh build
./idf.sh build && python3 tools/stack_check.py          # 栈预算（含 ui_retry_task）

bash tools/preview/run.sh && node tools/preview/scroll_gap.js tools/preview/out

# 本轮复核整改的三条回归（都在临时目录里跑，用桩 kill / 桩 systemctl / 派生预览副本）：
python3 nas/fpk/test_install_token.py                   # M31 安装令牌 fail-closed
bash nas/fpk/test_process_lifecycle.sh                  # M21/M25/M26 停止失败不删记录不写配置
bash tools/preview/test_run_guard.sh                    # M36/M57 分支变量隔离 + 输出目录越界
python3 nas/fpk/tls_accept_check.py                     # H12 TLS 接入不阻塞（生产超时值）

# NAS 侧契约与端到端：
python3 nas/fpk/contract_check.py && python3 nas/fpk/collector_check.py
bash nas/fpk/test_lifecycle.sh                          # 安装→启动→配置→升级→卸载

# 汇总（报告写到 docs/fnos-companion-test-report.md）：
bash tools/test_report.sh --full
bash tools/verify_all.sh
```

原始日志留在 `/tmp`，可被清理，因此不能作为长期唯一证据；正式闭环应把脱敏摘要、候选身份、命令与退出码
保存到持久验收记录，原始日志与物理图片留在私密证据目录。
