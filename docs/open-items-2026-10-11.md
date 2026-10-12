# 未闭环与未完成事项清单

更新日期：2026-10-11（Asia/Shanghai）。本文面向下一轮实施、验收与发布交接。

> 第二轮本地候选的独立审读、主机整改与明确清单同步已完成（§68），82编号与28个low分组均保留各自证据／残余，不代表全部产品问题关闭。当前源码、旧安装包与未推送边界见 [Codex 独立复核记录](codex-open-items-review-2026-10-11.md) 及 [问题账本](open-items-review-ledger-2026-10-11.json)。下文首批表格是历史快照，不能覆盖当前结论。

**当前结论：首批补丁完成了主机侧修复和验证，并随本文保存在本地提交中，但尚未推送、发布、烧录或完成真实 NAS／开发板闭环；全仓审计未清零，72 小时长测与 30% 性能目标均未完成。**

## 1. 文档口径与证据边界

来源：

- [原始全仓审计](full-repo-review-2026-10-10.md)：22 条 high、60 个编号的 medium，以及 low／文档漂移。原报告包含复核降级、撤回项和复合条目，不能把标题数字当成独立缺陷总数相加。
- [首批修复与验证](fixes-2026-10-11.md)：本轮 29 文件差异、回归证据和验证范围。
- 2026-10-11 当前发布工作区、Git 引用、远端 main 回读及本地 FPK 校验。
- 用户提供的 NAS 安装、性能、长测和设备运行交接。此类运行态未在本文编制期间重新连接设备验证，明确标为“交接记录／待刷新”。

状态定义：

| 状态 | 含义 | 能否认为整体交付完成 |
| --- | --- | --- |
| 待处理／待复核 | 本轮未修改相关路径；需确认当前触发条件，再实施或撤回 | 否 |
| 部分处理 | 同一编号中的部分子问题已修，仍有残余 | 否 |
| 主机验证通过 | 本地补丁及针对性主机检查通过；随本轮本地提交保留，尚无推送／发布证据 | 否 |
| 待实机闭环 | 需要 NAS、固件、串口及物理显示联合证据 | 否 |
| 旧阻塞已消除 | 当前 Git 状态证明旧交接描述不再适用 | 只关闭该项，不能替代发布 |

本文以下主体保留首批历史快照；第二轮独立差异审读与当前逐项结论另见上述复核记录和问题账本，不能用历史表替代当前验收。首批编制期间没有开展第二轮全仓代码审核。原审计中未触碰的发现以历史证据进入待办，不伪装成今天重新复现的结果。原报告行号属于旧快照，修改后应按函数定位。附录逐条保留原分析，用于防止遗漏；最终修法仍须服从当前源码和项目约束。

## 2. 当前仓库与发布状态

| 项目 | 当前可核实事实 | 仍缺什么 |
| --- | --- | --- |
| 开发／发布隔离 | 两个独立目录；本轮明确同步 29 个源码、测试、文档文件，内容一致 | 后续继续逐文件同步，不能整目录覆盖或复制私密配置 |
| 当前提交基线 | 本轮提交之前，本地 HEAD、origin/main、远端 main 均为 `b7bc60b5b1b053cb8523223a510736257e64d09f` | 本轮修复由包含本文的后续本地提交保存；远端仍需推送后回读 |
| 旧 main 分叉 | `4696601` 已收下公开线历史，包含 `d84039f` 父线且保留本地 UI 树 | 不是“仍需 force push”的状态；后续发布需重新检查远端变化 |
| 杂分支歧义 | 开发目录原杂分支已归档为 `archive/v1.1.0-repack`；发布目录仅 main | 归档分支尚未删除；它不是发布阻塞 |
| 本轮工作区 | 本轮提交范围为 25 个原有文件 + 5 个新文件（含本文） | 已复核、按明确文件清单暂存并本地提交；推送与提交后的远端回读尚未进行 |
| 源码秘密扫描 | 本轮修改文件 Gitleaks 与无回显真实值比对通过 | 发布时仍须扫描实际暂存内容和全部将推送的提交；不能借本次结果替代未来扫描 |
| 通用固件 | 发布门完整重跑成功，产物 7,036,672 B；与开发配置对照的 3 个非模板真实值均未命中 | 产物只留本地；未发布二进制、未烧录；空令牌不构成真实凭据测试 |
| 配置恢复 | 发布门结束后本地配置已逐字节恢复 | 后续检查仍需维护隔离、失败退出和恢复机制 |
| 自动审核覆盖 | 原审计 93/106 文件获得 OCR 完整扫描；剩余 13 个记录为人工复核覆盖 | 不应表述为“106 个文件全部被 OCR 扫完”；排除对象、生成资产及本轮之外差异仍有范围边界 |

本轮本地提交可用 `git log -1 -- docs/open-items-2026-10-11.md` 定位。推送和正式发布尚未发生。现阶段没有必要、也没有执行改写公开 main 历史。

## 3. 运行、产品与交付未闭环事项

### O01. NAS 应用安装与 RAID 显示联合验收

- **状态**：待用户安装／待实机闭环。没有新的安装完成确认；本文未连接真实 NAS。
- **本地对象**：`nas/fpk/nasscreencompanion.fpk`，83,741 B，SHA-256 `afed3abd9d910b13302b29a0d058b504ee5c279155f2c4b43b587193456bcc35`；这些只证明当前本地包身份。
- **重要限制**：该 1.2.5 包不是本轮所有源码补丁的新交付物。原审计 H5 指出包内 ZFS 类型回归；因此安装它可以取证，但不能直接当成最终发布候选。
- **下一步**：确认 NAS 已装包版本与包哈希；在本机私密配置中提供实际地址，读取 HTTPS status，核对 raid 行的 `health/what/have/want`；对比实际阵列状态，再采集板子存储页。
- **闭环标准**：真实 NAS 响应、解析结果、物理页面三者一致；覆盖健康、降级／维护、无阵列和权限失败。安装／升级不丢已有设备配对与配置。
- **证据**：脱敏验收记录、设备固件身份、包身份、串口时间关联及本地 `capture-board` 图片。原始接口响应和设备清单不得发布。

### O02. 30% 性能目标仍未达成

- **状态**：未完成；数值来自此前交接，未在本文编制时重测。
- **已有交接结果**：iv 26.9–28.0 ms，rs2rr 22.3–23.3 ms；差异在约 ±1.2 ms 噪声带。`buffer_height` 尝试受内部 RAM 限制，曾增加约 17 ms，已还原；round 7 profiler 未找到单一热点，主要时间分布在绘制和帧交接。
- **剩余工作**：确认指标定义、对照基线、场景、采样重复数与噪声判据；如继续跨越目标，需要评估厂商刷屏主路径改动。
- **决策边界**：厂商显示主路径本轮未改；不能把“允许继续处理审计”自动扩展为该架构／驱动改动。
- **闭环标准**：同硬件、同数据、同构建配置和相同负载下，重复测量达到约定的 30% 指标；同时通过内存、栈、触摸、整帧显示和长测，无明显回归。

### O03. 72 小时长测缺失（S1）

- **状态**：INCOMPLETE。此前只有 10 分钟冒烟；2 小时预热后的持续样本缺失。评价器 55 条断言通过不等于设备长测通过。
- **本轮改善**：零有效样本不再 PASS；PSRAM 底线改看历史最小值 `pmin`；诊断 SHA 测试跟随真实配置长度。
- **下一步**：先固定经修复的候选源码、配置、ELF 与对应已运行固件；核实串口及当前板子身份，再启动 72h 只读采集。
- **命令模板**（端口仅在本机设置，实际 ELF 必须对应板上固件）：

```bash
export FNOS_SOAK_PORT='<已核实串口>'
python3 tools/audit125/soak72.py capture --hours 72 --elf '<候选 ELF>' --output '<本地证据目录>'
python3 tools/audit125/soak72.py analyze --input '<本地证据目录>/metrics.jsonl' --hours 72 --elf '<同一候选 ELF>' --output '<本地评价目录>'
```

- **判据**：按工具当前实现和审计合同检查覆盖率、心跳间隔、任务栈下界、内部／DMA／PSRAM 堆底线、预热后的趋势、HTTP P95、最新成功接收年龄、`source_ts` 前进和故障窗口恢复。故障窗口必须事先登记；不能用事后补窗口掩盖失败。
- **闭环标准**：连续时段和有效样本满足判据、ELF 身份匹配、报告 verdict 为 PASS；中断、缺样本或固件身份不明都不能判完成。
- **边界**：启动采集不授权制造断网、重启 NAS 或烧录；这些阶段另按当前授权执行。

### O04. 状态传输与快照容量边界

- **状态**：容量保护的主机测试已补，真实容量规划与大清单实机验收未完成。
- **已确认配置**：状态传输预算 262,144 B，快照 arena 1,048,576 B，均可通过 Kconfig 配置。arena 同时容纳向量、字符串和账头，不能按 JSON 原始字节数等量估算。
- **旧探针**：870,055 B 可解析、994,736 B 报 `data capacity`；两者都不能直接代表当前 256 KiB 设备传输预算以内的端到端能力。
- **本轮证据**：主机使用设备配置预算；超容量整帧拒绝并保留上一份完整快照，未静默裁剪清单。
- **剩余工作**：测量实际 status/history 大小、最长字符串、各类清单增减及结构体开销；统一采集端、传输、解析、UI 和文档的容量契约；验证预算上调后的 RAM 与栈成本。
- **产品约束**：不得以型号、数量或名称长度静默裁剪硬件清单。优先配置预算、明确报错和完整性策略；若拟引入截断，必须重新明确产品合同，不能直接沿用旧建议。
- **闭环标准**：边界内完整清单可达、边界外提示明确、上一快照一致、恢复后可重新展示；真机多轮增减和超限恢复通过。

### O05. 独立 8799 agent 未同步、未运行验收

- **状态**：待处理；此前交接称 8799 未运行／空响应，本轮未刷新运行状态。
- **范围**：`nas/fnos-agent.py`、service 与安装脚本。不能把 8798 FPK 路径通过当成 8799 也通过。
- **剩余点**：RAID 逻辑差异、默认无 token、历史 `?n=`、FUSE 文件系统过滤、摘要字段契约等（见 M31/M32/M58 与附录）。
- **下一步**：决定仍维护独立 agent，还是明确退役／迁移；如维护，分别回归其安装、认证、采集、历史上限、RAID 与故障恢复。
- **闭环标准**：两条路径的支持范围文档清楚，所保留路径有独立测试与真实部署证据，端口和配对口径不冲突。

### O06. 真实证书、配对令牌与撤销链路

- **状态**：主机解析与模拟链路通过，真实设备链路未闭环。
- **缺口**：真实 NAS 证书握手、用户指纹确认、令牌交换与持久化、重启后的身份恢复、设备撤销后旧令牌拒绝、证书更换／错误证书拒绝及重新配对。
- **闭环标准**：每一步有 NAS 端与板端对应证据；撤销必须使旧令牌失效；TLS 关闭或握手失败明确标为未验证／失败，不能报告“TLS 固定通过”。
- **关联**：H4/H16、M01/M04/M31/M33 及 NVS 明文存储风险口径。配对令牌明文 NVS 的威胁模型与可接受性仍待文档决策；本轮没有引入 NVS 加密。

### O07. 首批四个产品修复待烧录及物理复验

- **状态**：源码和主机回归通过，尚未部署到板子。
- **必须复验**：上电首次入页、历史回填后二次轮询、KPI 分配失败后持续提示与恢复、畸形／截断 PEM 拒绝。
- **可见检查**：六页、首次和反复切页、配对／配网覆盖层、清单增减、长名称换行、滚动到底、格式符残留、夜间模式和诊断提示。
- **方式**：核对烧录候选与当前端口，按已授权阶段操作；需要观察显示时使用 `capture-board`，查看新图并关联串口与固件身份。
- **闭环标准**：真实板子运行修复候选；无复位／越界征兆；历史时间范围与断流提示正确；OOM 明确可见且恢复合理。主机 PNG、构建成功与串口无报错均不能单独替代屏幕验收。

### O08. 板上诊断固件与生产配置切换

- **状态**：此前交接称板子运行 `build_diag`、`CONFIG_FNOS_SOAK_DIAG=y`；本轮没有读取设备确认，也没有烧回生产配置。
- **决策**：继续长测则固定诊断候选；日常使用则在验收后选择生产配置。不能仅凭本地目录名断言板上固件。
- **闭环标准**：记录板上 ELF 身份、配置、构建时间和运行用途；切换后有启动、连接和物理显示证据。

### O09. 本地提交后的推送与正式发布

- **状态**：本轮修复、测试和本文已本地提交，尚未推送或正式发布；原 1.2.5 FPK 也未随本轮重新打包。
- **剩余工作**：处理发布阻塞发现；审阅完整差异；复核版本、manifest、README、包哈希与源码对应关系；发布目录占位构建与必要测试；扫描实际暂存和将推送历史；后续修复按明确文件清单追加提交；推送后回读远端提交、分支与内容。
- **禁止替代证据**：本地 build、预览、报告存在不等于发布。没有必要时不强推，不重新初始化既有发布仓库，不发布个人固件、配置、日志、截图或真实接口响应。
- **闭环标准**：可定位的提交／PR或发布引用、准确版本与包身份、远端回读结果、验收范围及未支持范围明确。

### O10. 仓库卫生、文档漂移与历史隐私

- **状态**：本轮没有全面清理，也没有重写历史。
- **交接遗留**：个人用户名、本机绝对路径、网络标识及历史中的内部地址需按实际路径清点；本文不复述真实网络值。
- **剩余工作**：分别清点当前待发布文件、已跟踪树和历史；区分个人标识、文档样例与真正秘密；做脱敏计划。发现真实秘密时按授权撤销／更换，不能只删最新一版。
- **决策边界**：已发布历史保持连续；清理／重写历史需要明确范围和回滚方案，不因普通修复而自动执行。
- **闭环标准**：当前内容脱敏、模板可复现、所有推送提交扫描通过；历史残留有明确处置记录，未处置项如实列出。

## 4. High 原编号状态台账

下表中的状态仅代表**主机侧差异**，逐条证据（命令、返回码、样本数）见 [本轮整改与验证记录](dsh-open-items-results-2026-10-11.md)。标为“本轮已修（主机验证）”的编号尚无推送／发布证据，产品项还须 O07 真机复验；H14/H16 仍有明确残余，不计为整条完成。

| ID | 问题 | 当前状态 | 未闭环内容／下一步 |
| --- | --- | --- | --- |
| H1 | 首帧 `NAV_TXT[-1]` | 主机验证通过 | 先选导航再刷新；ASan/首次入页回归通过；待烧录与物理启动验收 |
| H2 | 状态提交清除历史元数据 | 主机验证通过 | 连续提交回归通过；待真实历史跨度与断流提示验收 |
| H3 | KPI 分配失败 `memset(NULL)` | 主机验证通过 | 统一分配通道及调用点防护；待真实内存压力／提示与恢复验收 |
| H4 | PEM 跳过 NUL 越界 | 主机验证通过 | 畸形输入 ASan/UBSan 通过；待真实 TLS／配对闭环 |
| H5 | FPK 把 ZFS 对象改成数组 | 本轮已修（主机验证） | `normalize_snapshot_types()` 不再把 `zfs` 归入列表类强转，保持 dict/null 契约；`python3 nas/fpk/collector_check.py` rc=0；重新打包与 NAS／屏幕验收仍未做 |
| H6 | 主机标签类检查排除子类 | 主机验证通过 | 四种清单与 OOM 通过；后续待推送与候选发布 |
| H7 | 解析测试空清单崩溃 | 主机验证通过 | 空数组访问防护及空 fixture 通过；其余字段覆盖仍见 M16 |
| H8 | serial_capture 收尾 reset 未实现 | 本轮已修（主机验证） | 收尾与 `--reset` 真正调用 `hard_reset(ser)`（原先漏参被宽 `except` 吞掉），失败时打印 esptool 复用命令；`docs/verification.md` 措辞改为“尝试复位”；未对真实设备复位 |
| H9 | 长测零样本／堆最低值／SHA 替身 | 主机验证通过 | 评价器与诊断回归通过；72h 本身未完成，见 O03 |
| H10 | README 版本、公开边界和哈希过时 | 本轮已修 | README 对齐 1.2.5、83741 字节与实测 SHA `afed3abd…cc35`，公开边界与运行身份说明按当前实现重写 |
| H11 | 文档错误称审计对象不存在 | 本轮已修 | `docs/review-v1.2.5-audit-2026-10-10.md` 与 `tools/audit125/README.md` 加“2026-10-11 更正”：两份补丁与审计用例都在本仓且可运行 |
| H12 | TLS peek／握手阻塞 accept 线程 | 本轮已修（主机验证，检查已重写） | `get_request` 只做 `accept()`；明文探测／TLS 握手移到连接处理路径，peek 3.0s／握手 10.0s 有界超时 + 并发上限（`BoundedSemaphore`）；`nas/fpk/tls_accept_check.py` 重写后用**生产超时值**与真证书实测 rc=0：静默 3.0s、半握手 10.0s 关闭，超限连接 0.30s 被拒而 32 条继续走超时，负载下明文 0.004s／TLS 0.002s 仍 200，两条负控通过。旧检查把生产超时 monkeypatch 成 0.5s 且把 socket 超时算作"已关闭"，其 PASS 不成立。真机链路仍待 O06 |
| H13 | 证书工具失败仍三方一致 | 主机验证通过 | 工具失败与空指纹拒绝；待推送与候选发布 |
| H14 | 滚动条审计假红与零覆盖假绿 | 部分处理 | 覆盖率改为逐卡报告（44 处可判定、215 处如实记“未判定”），旧卡片表漏审已修；仍余 18 处历史净距（基线同 18 处），须先把轨道识别做成与位置无关再收紧判据，实验与回退记录见 [记录](dsh-open-items-results-2026-10-11.md) §7 |
| H15 | 页面异步错误 exit 0 | 主机验证通过 | 异常退出码、可见值与撤销请求回归通过；真实管理页网络错误仍见 M24 |
| H16 | TLS-off 模拟器跳过却报安全通过 | 部分处理 | TLS-off 已非零、缺 `cert_pem` 判失败、**自算指纹不再算通过**（必须有 NAS 提供的 `tls_fingerprint` 才能比对）；真实链路仍待 O06 |
| H17 | 内存样本不足等同通过 | 主机验证通过 | 独立非零码；端点 200 有效覆盖；仍需真 NAS 长期内存证据 |
| H18 | objdump 失败仍报栈安全 | 主机验证通过 | 读取失败拒绝；栈分析能力的潜伏局限仍见附录 C |
| H19 | 栈入口缺失仍放行 | 主机验证通过 | 缺任务入口非零；回调、中断及大帧识别仍不能据此全部证明 |
| H20 | 生命周期失败不影响报告退出码 | 主机验证通过 | 故障注入通过；报告对账顺序残余见 M51 |
| H21 | 长容器名截断后身份不可区分 | 本轮已修（主机验证） | `docker_api.py` 新增 `row_from_container()`：保留**完整**容器名 + 稳定 12 位 `id`，不再按 23 字节截断；契约与采集端检查（含共同前缀名可区分）通过 |
| H22 | manifest 检查了错误包／空字段 | 主机验证通过 | supplied package、非空字段与 JSON 类型回归通过；正式上架／真机安装仍未发生 |

**归类结果（2026-10-11 第二轮更新）**：原 6 个待处理编号（H5/H8/H10/H11/H12/H21）本轮全部修复并有主机证据；H14/H16 由“部分处理”收窄到只剩明确残余（H14 的 18 处历史净距、H16 的真机链路）。这个归类不是“只剩 1–2 个产品缺陷”：编号可能含多个子问题，真机／发布／长测仍全部未闭环。

**复核整改轮（同日）**：主机 Codex 首次复核打回 8 条缺陷（M31 令牌 fail-closed 破坏默认安装、M25 停止失败仍写配置、M21/M26 进程未死却删记录并谎报已释放、H12 旧证据不成立、M11 栈预算漏 `ui_retry_task`、M36 分支变量跨阶段泄漏、M57 软链越界、文档口径），已逐条整改并加**负向回归**；其中 H12 的检查被重写、M21/M25/M26 的修复过程中又自证抓到 4 处真 bug（`cmd/*` 报错分支自身因多字节变量展开而 rc=127 崩掉）。逐条证据见 [本轮整改与验证记录](dsh-open-items-results-2026-10-11.md) §0。

## 5. Medium 状态与处理原则

原报告的 60 个编号全部保留在附录 A。**2026-10-11 第二轮**已把 M01–M60 全部逐条复核，**复核整改轮**又把其中 7 条做成带负向回归的版本（M11/M21/M25/M26/M31/M36/M57）。统计口径：**56 条修复 + 1 条复核为误报（M20）+ 3 条复核确认无需改（M15/M38/M39）= 60**（此前写成“55 修复 + 1 误报 + 4 复核确认”是把 M20 重复计数、又把原本属修复的 M60 记成复核，已改正）。附录 A 各小节标题里的“待处理／待复核”是**原审计当时**的状态标签，权威现状以本节、§4 表与[本轮记录](dsh-open-items-results-2026-10-11.md) §3/§4 为准。

部分编号在原报告正文已经降级或部分撤回（例如 M17/M19/M27/M28/M45/M46/M50 等）。附录保持原编号方便对账，不把其中已撤回的子项重新算成缺陷。

两个需要避免照抄修复的细节：M20 的 mdstat 成员语义应对照 Linux 原始输出与权威定义重核后再决定赋值，不凭旧报告一句话交换；M27 原文“80 这类 1 位端口”有明显表述错误，实际需要验证的是 0、越界值和合法边界。H21 原报告建议进一步缩名，与当前不得裁剪身份／清单的产品合同冲突，应按完整名称与稳定 ID 设计。

## 6. 下一轮建议顺序与收尾条件

1. ~~先补状态台账与剩余门~~：**本轮已完成**（H14/H16 收窄，M16/M17/M36/M47/M51 已修；H14 的轨道识别与 H16 的真机链路是剩余子项）。
2. ~~处理高影响产品项~~：**本轮已完成**（H12 TLS 接入阻塞、H5 ZFS、H21 容器身份、M01 配对代次竞态、M31 默认无认证、M25/M33/M34 配置与生命周期风险均已修并有主机证据）；剩真机复验。
3. ~~复核整改轮~~：**已完成**（Codex 首次复核的 8 条缺陷逐条整改，7 条编号带负向回归并接入 `tools/test_report.sh` 与 `tools/verify_all.sh` 的 `L1.6`；附录 low 的 28 个分组逐组给出处置）。仍剩：真机面、`.fpk` 重打包（需授权）、H14 轨道识别。
3. **完成端到端容量与双采集路径契约**：O04/O05；按实际数据完整展示，不引入静默裁剪。
4. **固定可部署候选**：版本／包／源码一致、模板构建与秘密扫描通过、明确提交与发布授权范围。
5. **真实 NAS 与板端闭环**：O01/O06/O07/O08；取证不等于自动授权外部修改。
6. **72h 长测与性能**：O03 独立采集、评价；O02 如需厂商主路径改动先明确范围，单独设计实验与回滚。
7. **最后交付**：全部阻塞有处置结论；记录剩余非阻塞项、支持矩阵、正式提交和远端回读。

### 最终关闭检查表

- [x] 本文所有待处理项逐条有“修复／有证撤回／明确延期”的记录，复合项按子问题拆开（H1–H22、M01–M60 见 [本轮记录](dsh-open-items-results-2026-10-11.md)）。
- [x] H14/H16 与 M16/M51 等部分处理项的剩余子项已复核（H14 余 18 处历史净距、H16 余真机链路，均已写明）。
- [ ] 当前源码与正式包版本、哈希、README、支持范围一致（本批授权排除二进制，`nas/fpk/nasscreencompanion.fpk` 未重打包，见 [记录](dsh-open-items-results-2026-10-11.md) §7）。
- [x] 主机、构建、故障注入和必要安全回归针对同一候选通过（IDF 5.5.3 构建 rc=0、预览 84 帧 rc=0、生命周期端到端、12+6 项故障注入测试；不含真机）。
- [ ] 真 NAS 安装／升级／停止／卸载、配置保留和令牌撤销有证据。
- [ ] 开发板运行的是同一候选，六页及覆盖层有物理验收记录。
- [ ] 72h 数据完整并满足判据，不能用短测或评价器自测代替。
- [ ] 30% 指标达到，或用户明确变更目标且文档记录。
- [ ] 公开历史连续，真实值不进入源码／文档／构建产物发布。
- [ ] 明确文件提交与推送完成，远端内容回读一致。

## 附录 A. Medium 60 个原编号完整明细

以下每项“原审计记录”来自 2026-10-10 的固定报告，保留触发、影响与建议以防丢失。除标明主机验证或部分处理者外，均尚未在本轮重新复现；不把原文中的“CONFIRMED”当成今日复核签名。优先级和最终修法服从当前代码与合同。

### M01 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_data.c:194-197` TOCTOU —— 以为配好仍在明文轮询**：`s_conn_gen = fnos_pair_generation();`（`fnos_pair.c:872-888` 无锁）与 `fnos_pair_active(&s_conn);` 是两次独立读取；配对若恰在两者之间完成，`s_conn` 保存旧（明文/无 token）端点而 `s_conn_gen` 已是新一代 → `fnos_data.c:433` 的代次比较永不触发，设备静默继续轮询旧端点，UI 却显示"已配对"。修法：`fnos_pair_active` 在同锁内返回代数（`uint32_t fnos_pair_active(fnos_pair_cfg_t *out, uint32_t *gen);`），`fnos_data.c:196` 改用它。

### M02 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_snapshot.c:161-163` net 摘要漏字段**：摘要分支只解析 `if/rx_kbs/tx_kbs/rx_total_gb/tx_total_gb`，漏 `state/speed_mbps/physical`，而 `:202-207` 的 interfaces 循环解析了 → `net.state` 恒 `""`、`speed_mbps` 恒 0；采集端 `nas/fnos-agent.py:214-244`（`summary = dict(selected)`）确认三键已下发。修法：摘要复用数组循环的解析。

### M03 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_snapshot.c:150-151` `number()` 无 clamp**：`double` 直接赋给 `uint32_t/int64_t/int`，越界/NaN/±inf 是 UB，实机显示垃圾数（不崩）。

### M04 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_wifi_store.c` 凭据写入/擦除失败被吞**：`:72-75` `save()` 失败仍打印"凭据已保存"；`:82-86` `clear()` 丢弃 `nvs_open/erase/commit` 全部返回值且声明为 `void`（`fnos_wifi_store.h:27`）仍打印"已清除"；调用方 `:63` 直接 `{ clear(); return true; }` → CLI 无从感知"以为删了其实没删"（含安全语义）。

### M05 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_net.h:9` "幂等，非阻塞"误导 + LVGL 任务内同步 Wi-Fi RPC**：`fnos_ui.c:2523`（`wifi_pick`）、`fnos_ui.c:2667`（`wifi_connect_cb`）在 `lv_timer` 回调链（`ui_tick` 注册于 `fnos_ui.c:4656`）里调 `fnos_net_set_credentials` → `fnos_net.c:262` NVS 提交 + `:272-273` `esp_wifi_disconnect/set_config/connect`，最坏阻塞数秒（`fnos_net.h:36-37` 自认 5 s），直接顶住 LVGL 帧。修法：照 `fnos_net.c:68-71 s_connect_wanted` 范式改成置标志由 service 任务执行。

### M06 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_wifi_cli.c:110-111` 前缀匹配**：`!strncmp(sub,"set",3)` / `!strncmp(sub,"clear",5)` → `wifi clearing` 直接清 NVS、`wifi setfoo bar` 写 SSID=foo。修法：token 后 `strcmp` 精确匹配。

### M07 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_wifi_cli.c:127-128` 超长行被拆成多条命令**：`CLI_LINE_MAX=192`（`:22`）下 >191 B 的行被 `fgets` 截断成两条命令依次执行。修法：丢弃超长行并报错。

### M08 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_wifi_cli.c:99-101` `tls` 命令把日志级别提到 DEBUG 后从不恢复**（探针是 detached 任务 `fnos_pair.c:317-323`，`probe_task` 结束只 `vTaskDelete`）→ 之后串口永久刷 DEBUG 日志。

### M09 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_net.h:43` 扫描缓存无锁协议**：`fnos_net.c:341-357` 先 `s_ap_n = 0` 再回填同一数组；读侧 `fnos_net.c:369-374` 先读条数再逐条拷（`fnos_ui.c:2477` 每 tick、`:2530` 点击时）→ 同一帧内 SSID 撕裂（字符串始终有 NUL，无越界）。另 `fnos_net.c:228 static char buf[160]`、`:282 static char buf[16]` 跨任务共用。

### M10 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_net.c:113` `reason_text()` 不认 211/212** → 配对/连接失败时屏上显示"原因码 211"而非可执行提示。

### M11 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`main/main.cpp:210-224` `bsp_display_lock` 失败静默**：只打一条 `ESP_LOGE(TAG,"display lock failed")`，之后永久无 UI、无重试、无外显状态（`:232-235` 夜间定时器创建失败、`:236-242` HEAP_DEBUG 分支同样静默）。修法：有限重试 + 降级标志 + 屏上提示。

### M12 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**printf 格式属性缺失（AGENTS.md 硬约束⑤相关）**：`fnos_data.h:95` 缺 `__attribute__((format(printf,3,4)))`，`ui_kit/uk.h:86` 缺 `format(printf,2,3)`。今天调用点都是字面量 → 无实例缺陷，但本仓历史上漏过格式符，屏上残留 `%d/%s/%u` 正是同一类问题。

### M13 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`sdkconfig.defaults:30` `CONFIG_LV_USE_CLIB_MALLOC=y` 与 `:85` `CONFIG_LV_USE_CUSTOM_MALLOC=y` 互斥**（LVGL Kconfig:45-63 同一 choice，后写者胜；生成物 `sdkconfig:2893/2896` 证实 CUSTOM 生效）：删/移 `:85` → `components/lvgl_mem_psram` 的 `lv_*_core` 全部成为死代码、LVGL 对象回落内部 RAM（≈361 KB）→ OOM，而且**没有任何编译期报错**。修法：删 `:30` 或移到 `:85` 之后。

### M14 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`main/CMakeLists.txt:4,8` 缺 `esp_timer` 依赖**：`main.cpp:17/:100/:232` 使用 `esp_timer`，而 main 的 `REQUIRES`（fnos_monitor、BSP、lvgl_mem_psram、nvs_flash）+ `PRIV_REQUIRES esp_app_format` 都没有它；`esp_timer` 不属 IDF 公共依赖（IDF `build.cmake:275`），BSP 只 public `esp_driver_*`+fatfs，LVGL 用不传播的 `PRIV_REQUIRES ${IDF_COMPONENTS}` → 当前唯一供给是 `fnos_monitor:44` 的 public `esp_timer`（一旦上游收紧即断）。修法：`main/CMakeLists.txt:8` 显式加 `esp_timer`。

### M15 — 主机验证通过

**当前交接**：主机预算导出与超容量回归已补；待随候选交付。

**原审计记录**：

**`tools/preview/CMakeLists.txt:15` 导出正则漏 `CONFIG_FNOS_SNAPSHOT_MAX_BYTES`**：而 `preview.c:44` 编了 `fnos_snapshot.c`（`:15-16` `#ifndef` 兜底 `(1024*1024)`，`:37-38` 当快照池预算）；今天默认值（`sdkconfig:2551 =1048576`）与兜底相等无差，改成 Kconfig 允许的 65536 就会出现"真机丢弃 >64 KB 快照、主机门仍按 1 MB 通过"。

### M16 — 部分处理

**当前交接**：仅空清单访问、argv[7] 参数守卫与部分容量测试已处理。UTF-8 全字段、history 读满、snapshot_check 自动执行、图像/串口/stub 等子项仍待处理。

**原审计记录**：

**`tools/**` 验收门可信度**（逐条见附录 B）：
    - `tools/parsecheck/main.c:120` UTF-8 扫描漏 `temps[].dn`、`mods[].name/status`、`raid[].health/what`、`disks[].dev`，而 `docs/fnos-companion-test-report.md:66` 声称全字段已验；
    - `main.c:139-140` `hbuf[512*1024]` 静默截断报成 `history parse=FAIL bytes=524287`；
    - `main.c:166,171` 守卫 `argc >= 7` 却读 `atof(argv[7])`（`run.sh:50-52` 恒传 8 参，门路径不踩）；
    - `tools/preview/snapshot_check.c:52-54` 断言全写在 `assert()`：`CMakeLists.txt:83` 的 `-UNDEBUG` 与 sanitize 目标挡住了 NDEBUG，但 `run.sh:59/62` **只编译并只运行 `preview`，从不执行 `snapshot_check`**（唯一执行处是 `tools/preview/README.md:74` 的手工命令）→ ownership/并发/预算三项验收无自动化证据。修法：`run.sh` 执行它并把结果纳入报告，关键断言改显式 `if` + 退出码；
    - `tools/certcheck/main.c:26-29` 目录输入误报"PEM 读不出来或太长"；`:20-23` 读满不报"输入太大"；
    - `tools/photo_check.py:14/:20-22/:66-69/:74`（assert 魔数、未校验 `bit_depth/interlace/ctype`、忽略 alpha、`tot==0` 除零）；
    - `tools/serial_capture.py:26-29` `time.time()` 非单调（应 `monotonic`）、`:33-34` `buf` 无上限、`:41-42` 残留缓冲无时间戳；
    - `tools/preview/snapshot_check.c:50` `cJSON_PrintUnformatted` 未判空；
    - `tools/preview/stub/esp_heap_caps.h:5-6/:12-13` 常量与 IDF 5.5.3 不符（真值：DMA `1<<3`、SPIRAM `1<<10`、INTERNAL `1<<11`、DEFAULT `1<<12`）；`stub/sdkconfig.h:4` 与 `ui_config_host.h` 重定义冲突（实测设备值 360 胜出，该行纯死，可删）。

### M17 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/preview/preview.c` 主机门可信度**（part3，9 条原始评论；复核后 **1 条升 high（H5）、其余全部降级或撤回**，见 §4）：
    - `:158` `assert(fnos_status_create(s,&counts))`、`:3282/:3285` `assert(fread(...))` + `assert(fnos_status_parse(...))`、`:2672` `assert(fnos_data_get(&s_motion_fixture))` 把**有副作用**的调用写进 `assert`——副作用属实，但"NDEBUG 下被擦除"在本仓**所有常规构建路径都不成立**：`tools/preview/run.sh:57-58` 传 `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1 -g"`（覆掉 CMake 默认的 `-O2 -g -DNDEBUG`；实测 `flags.make` 里 `C_FLAGS = -O1 -g -std=gnu11 -arch arm64 …` 无 `-DNDEBUG`），sanitize（`CMakeLists.txt:70-74`）与 `snapshot_check`（`:83`）还显式 `-UNDEBUG`，且仓库无 CI。**唯一危险路径**是手工 `cmake -S tools/preview -B build`（`CMakeLists.txt:6-8` 默认 Release）：create 被擦除而 `:159 s->ever_ok = true` 仍在 ⇒ 旧快照被当"在线"；`:3283 static fnos_status_t fixture` 若全零，则 `:3311-3387` 整个硬件矩阵**空跑并报 PASS**（最危险的假绿形态）。→ **medium（潜伏）/ medium-latent**。修法：改成真 `if (!…) abort();`，并给 preview target 无条件加 `-UNDEBUG`；负向对照用 `-DCMAKE_BUILD_TYPE=Release` 在 `/tmp` 副本构一次。
    - `:1345-1346`（`verify_container_order` 的 `fnos_status_t st = {0}; fnos_data_get(&st);` 从不 `fnos_status_release`；同模式还有 `verify_overview_charts`/`verify_ui_regressions`/`verify_docker_module_states`/`verify_temperature_protocol_limit`；调用点 `:1346` 漏、`:2672` 漏、`:3294-3299` 正确、`:3389` 正常路径清理）、`:3337-3338`（`PREVIEW_ALLOC_FAIL_AT` 分支提前 `return` 跳过 `:3389`）→ 机制 CONFIRMED，影响 low（host 侧 `fnos_snapshot.c:28 return malloc(n)`；macOS ASan 无 LeakSanitizer，泄漏量不可实测）。
    - `:804` `SEEN_MAX(4096)` 溢出后静默丢弃 → 复核为 **low**：实测默认 `seen=1685 seen_dropped=0`、dense 夹具 `seen=3045 seen_dropped=0`（占 74%，余量 1.35×）；真溢出会抬高 `s_never_visible` 并在 `:3606-3610 abort()`（响亮但归因误导）；夹具模式下该审计在 `:3338` 早退不跑。
    - **已撤回**：`:694` `MAX_TAPPABLE(256)`（实测最大 **57**，余量 4.5×）、`:516-519` `flush_cb` 越界（`s_frame[SCR_MAX_W*SCR_MAX_H]` `:508-511` + 入口 `:3490-3495` 钳 `w<=1280/h<=800`，display 按 `s_scr_w/h` 建 `:3497`；`PREVIEW_PARTIAL` 是 opt-in `:3500-3502`；像素 memcmp 断言 `:2911/:2981/:2989/:3009/:3019/:3108/:3129/:3157/:3185/:3218` 都在默认 FULL 下跑）、`:1271-1273` `state_name()` 漏 `ST_LOCKBUSY`（唯一调用点 `:1313/:1320` 在 `run_state`；7 次调用 `:3552-3555/:3595/:3599` 都不会遇到该状态）→ 移入 §4。

### M18 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`run-as` 文档措辞与 `config/privilege` 不一致（原报告定为 high，**复核后降级**）**：`docs/fnos-companion-app-plan.md:67` 写"`config/privilege` 用 `run-as=package`"、`nas/fpk/README.md:172` 写"运行身份是专用包用户（`run-as: package`），不是 root"，而实测 `nas/fpk/nasscreencompanion/config/privilege:4` = `"run-as": "root"`。**但实际设计是对的**：`config/privilege` 为 root 是生命周期脚本（写 `/var/apps`、读 root 态 `docker.sock`）所需，服务本体由 `cmd/main:39-47` 在 root 下启动 `/usr/bin/python3 -I cmd/docker_launch.py --user "$package" …`，而 `cmd/docker_launch.py:220-222` 在 exec 前 `setgroups([])/setgid()/setuid()` **永久降权**，`cmd/_common.sh:49-58` 的 `as_package()` 也用 `runuser` 操作包内数据，`cmd/install_init:12` 的表述（"管理服务使用非 root 包用户；生命周期使用 root"）与实现一致。→ 安全承诺不成立的部分只是"`run-as: package`"这个**文件级措辞**（README.md:71 的"管理服务以普通包用户运行；独立 root 辅助进程仅提供固定的容器状态读取"与实现相符）。**修法**：把两处"`run-as: package`"改为"privilege 声明 `run-as: root`，服务进程启动时降权为包用户"。

### M19 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`components/fnos_monitor/fnos_ui.c` part3 条目（原 2 medium + 3 low，复核后全部降为 low，另有 1 条撤回）**：
    - `:3668-3669` `overview_refresh()` 卷扩容失败直接 `return` → 本拍半更新（容量数字已写、卷行与 `storage_note` 停旧值）。复核：`uk_realloc`（`ui_kit/uk.c:39-44`）走 `lv_realloc`，失败时**原块保留**且置 `s_alloc_failed=true` ⇒ 无 UAF，只是少了一拍更新（跳过 `:3583` 起的后续、卷段从 `:3675+`），下一拍自愈 → **low**。修法：`count = LV_MIN(count, capacity)` 后继续走完。
    - `:2615-2616` Wi-Fi 口令 `s_wifi_pw` 在 `wifi_close_cb`/`wifi_result_cb` ONLINE 分支后不清零、`s_wifi_reveal` 不复位 → 明文驻留 RAM。复核：`:2674` 成功后确有 `s_wifi_pw[0] = 0`（仅首字节），`:2610-2617` 关窗路径不擦；明文显示时 LVGL 标签另存堆副本；无日志引用该缓冲 ⇒ 需要物理接触/root 才能读 → **low**（机制成立，影响面小于原描述）。修法：close 时 `memset` + 复位 reveal。
    - `:864` 顶栏 Wi-Fi 按钮宽度用 `UK_FONT_CJK_16` 量（`:860 lv_text_get_size(&wifi_size,"Wi-Fi 未配置",UK_FONT_CJK_16,…)`）、标签却用 `UK_FONT_CJK_12` 渲染 → 宽出约 20px，从顶栏 hostcol（`:849-856`）抢宽，长主机名更早截断；同模式还在配对（`:875-876`）与设置（`:886`）按钮 → **low（恒触发、纯显示）**。修法：测量与渲染用同一字号。
    - `:4717-4719` `dump_card_map` 注释称"页号从祖先链读"，实际只比直接父对象（`lv_obj_get_parent(c) == s_ui.page[k]`）→ 挂在容器里的卡片全打 `p=-1`（`main_row :1281/:1292/:1305`、`s_ui.p3_cols :1471/:1483`、`grid :1881/:1908/:1915/:1959`），直接挂页的正常（`:1004/:1017/:1252/:1361/:1449/:1496/:1540/:1588/:1608/:1791/:1801/:1862/:2228`）；仅在 `PREVIEW_CARDMAP`（`preview.c:1195` → `:3527`）门控下调用 → **low（诊断专用）**。
    - **已撤回**：`:961-962` `chart_sample_count` 未判空（`r->chart = uk_trend_create(body, UI_HIST, 1)`（`:1039`）恒有 1 个序列）→ 移入 §4。

### M20 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/app/server/fnos_collector.py:382-383` mdstat `[m/n]` 的 have/want 赋值互换 —— CONFIRMED（降级数组显示反向数字）**：Linux mdstat 的 `[m/n]` 是"m 个工作成员 / 共 n 个"，而代码写 `cur["want"] = int(m.group(1)); cur["have"] = int(m.group(2))`。降级阵列 `[1/2] [U_]` 会产出 `have=2, want=1`，固件 `fnos_snapshot.c:176-177` 原样下发、`fnos_ui.c:4056` 打成 `"%s · %d/%d …"` → 屏上显示「2/1」。健康判定不受影响（`fnos_collector.py:131-132` 用 `have == want and "_" not in flags`，对称），所以只是数值撒谎。**修法**：交换两行赋值，并补一条 `[1/2] [U_]` 的解析用例。

### M21 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/cmd/main` 生命周期两条 —— CONFIRMED（复核后由 high 降 medium）**：
    - `:115` 在 `stop_process()` 里引用未定义的 `${port}`（该变量只在 `start_process`（`:19`，`local`）与 `status` 分支（`:139`）赋值），而 `_common.sh:10` 是 `set -u` → 走进这条路径（SIGKILL 后进程仍活：D 状态或权限）bash 立即以 "unbound variable" 中止，两条 `warn` 打不出来，且 `:116 as_package rm -f "$PID_FILE"`、`:117 cleanup_socket` 都不执行 → 残留 PID 文件与 socket，后续 `status/start` 判定失真。**修法**：函数内自行取端口或 `${port:-未知}`，并把清理动作置于消息之前（或用 `|| true` 保护）。
    - `:64-67` 就绪探测超时路径只 `fail + return 1`，不杀掉刚启动的进程、不删 PID 文件 → 服务活着占着端口，而应用中心显示"启动失败"，再点 start 会走 `is_running` 分支报"已在运行"并返回 0。**修法**：该路径执行与启动失败一致的清理（kill 本次 pid + 删 PID_FILE + cleanup_socket），或在文案中明确"进程仍在运行，请先 stop"。

### M22 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/build.sh:27` 打包后唯一一道内容校验会假绿 —— CONFIRMED**：`JUNK=$(tar tzf "$FPK"; tar xzOf "$FPK" app.tgz | tar tz) || true`。两条命令用 `;` 连接，`|| true` 覆盖整条；`fnpack build` 之后若包损坏或布局变化（`app.tgz` 不存在），两条 tar 都失败 → `JUNK` 空 → `BAD` 空 → `:38` 照样打印「✓ 包内没有字节码缓存 / .DS_Store / ._ 资源分叉」并 `exit 0`。这是分发前唯一一道包内容检查（注释自称"这个检查就是为它准备的"）。**修法**：分开取退出码，非 0 或列表为空即硬失败，例如 `if ! JUNK=$(tar tzf "$FPK" 2>/dev/null); then echo "✗ 打不开包，无法校验" >&2; exit 1; fi`，内层 `tar xzOf … | tar tz` 用 `PIPESTATUS[0]` 判第一段。

### M23 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/contract_check.py:387-390` "满载状态帧截断"检查已死但仍打印 ✓ —— CONFIRMED**：固件早已没有 `RX_BUF_SIZE`（现为 `components/fnos_monitor/fnos_data.c:56 HIST_BUF_SIZE (32*1024)`），`:388` 的正则永不命中 → `rx=0` → `:389/:391` 两个分支都不进；而 `main()` 在 `:320` 仍无条件打印「判定：板子读的每个字段，NAS 侧都在；容量上限也不丢数据 ✓」。注意这不是"整段被跳过"：`limits`（来自 `fnos_data.h` 的解析，`:351-354`）通常是有的，死的只是这一条检查 → NAS 侧上限调大后板子缓冲不够时门禁仍全绿。**修法**：改用现宏名（`HIST_BUF_SIZE`），或删掉该项并从 `:320` 的结论里排除它（§5「同名正则已静默空转」一句与本条合并）。

### M24 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/app/ui/www/index.html:363-366` `refresh()` 没有 `catch` —— CONFIRMED**：`async function refresh(){ … try{ … } finally { refreshing = false; } }`，`get('/api/state')` 在网络中断/服务重启时 reject → unhandled rejection；`$('hdot')` 停留在上一次状态、界面不显示任何错误，而 `setInterval` 每 2 s 再触发一次，控制台持续刷 rejection。`loadLogs()`、`gencode/cancelcode/rundiag` 与撤销按钮同样只有成功路径 → 点了没反应。**修法**：`catch(e){ $('hdot').className='dot err'; $('hhint').textContent='连接失败'; }`，操作类按钮在 catch 里给失败提示。

### M25 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/cmd/config_callback:9` 与运行中的服务并发改写 `config.json` —— CONFIRMED（窗口毫秒级，后果是丢配置）**：`apply_config`（`cmd/_common.sh:111-141`）是"读（`:118`）→ 合并（`:125-131`，特意保留 `devices`）→ 临时文件 + `os.replace`（`:133-137`）"的整文件重写；服务端 `nas_companion_server.py:407-424 save_config()` 也整文件重写同一路径（`os.replace` 在 `:424`），配对/令牌写回就走它 → 谁后写谁赢：服务在读到写之间完成配对，则新设备被旧副本覆盖；反之新的 `port/bind` 被旧副本覆盖。`config_callback:13` 才去停服务，所以窗口真实存在，而脚本仍打印「配置已更新（已配对设备保留）」。**修法**：先 `service_stop` 再写配置（或让 `apply_config` 经服务 API 提交），并在 `save_config` 侧加文件锁。

### M26 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/cmd/uninstall_callback:8` 无条件宣称已释放端口 —— CONFIRMED**：`:5 as_package rm -f "$PID_FILE"` 先删 PID 文件，`:6 cleanup_socket`，`:7` 直接 `say "已释放端口与 Socket。…"`，全程没有停服务或确认进程已退出（`uninstall_init` 的 `service_stop` 失败只 `warn`）。进程若仍在（SIGKILL 未生效、D 状态、权限），端口继续被占但 PID 文件已不存在 → `is_running` 永远为假，重装/重启报「端口被占用」且无迹可查。**修法**：先停并确认（含 `kill -0` 复核），失败就保留 PID 文件，文案改成"进程仍在运行（pid …）"。

### M27 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/wizard/config:16` 端口校验过宽 —— CONFIRMED（low）**：`{ "pattern": "^[0-9]{2,5}$" }` 接受 `00`/`00000`/`99999`（>65535），却拒绝 `80` 这类 1 位合法端口；向导通过后 `apply_config` 原样写进 `config.json`，要到服务启动绑定失败才暴露。**修法**：pattern 换成 `^([1-9][0-9]{0,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5])$`，或在 `apply_config` 里补 `1 <= int(port) <= 65535` 校验。

### M28 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/preview/palettes.sh:14-15` 未校验的 `KK_PALETTES` 直接进 `rm -rf` —— CONFIRMED（low，主机侧开发工具）**：`PALETTES="${KK_PALETTES:-graphite abyss phosphor}"`（`:11`）→ `for p in $PALETTES; do rm -rf "$OUT/$p"`；未加引号的展开既做词分割又做 glob（`KK_PALETTES='*'` 会删掉 `palette-out/` 下所有目录），`KK_PALETTES='../../components'` 之类可越出 `palette-out/`。**修法**：`case "$p" in ''|*[!A-Za-z0-9_-]*) echo "非法配色名：$p" >&2; exit 2;; esac` + `rm -rf "${OUT:?}/${p}"`，或改数组逐项校验并 `set -f`。

### M29 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/verify_all.sh:54` 几何审计的 grep 与实际输出对不上（并暴露 `PREVIEW_AUDIT_ALL` 计数是死变量）—— CONFIRMED（复核修正：不是"漏成 PASS"，而是"错报成字形问题"+ 探针模式静默）**：模式是 `grep -qiE "跑出父对象|压住|overlap|溢出|missing glyph"`，而 `preview.c` 实际打印的是英文 `child out of parent:`（`:607`）、`inline metric overflow %d > %d`（`:860`）、`text overflow …`（`:881`）、`missing glyph U+%04X in %s`（`:873`）、`text overlap: …`（`:978`）——中文词只存在于注释里，`overflow` 也不等于 `overlap`，因此 `child out of parent`、`inline metric overflow`、`text overflow` 这三类**永远匹配不到**（对照 `tools/preview/palettes.sh:21` 对同一份日志用的就是正确的英文模式）。**但"越界会漏成 PASS"这条不成立**：这些违规在 `preview.c` 里默认直接 `abort()`（`:612`/`:668`/`:860`/`:873`/`:881`/`:978`），preview 退出码非 0 → `verify_all.sh:46` 会走 `else` 分支并**误报**成「渲染失败（多为字形缺口，跑 `bash tools/gen_tools.sh`…）」——真问题是**归因错误**，不是放行。真正会静默的是文档化的探针模式 `PREVIEW_AUDIT_ALL=1`（`docs/ui-refactoring-ui-2026-10-08.md:215` 记录用过）：该模式下违规只做 `s_audit_fail++`（`:612`/`:668`），而 `s_audit_fail` **全文件只被写、从不被读**（`grep -n s_audit_fail` 只有 `:529/:612/:668`）→ preview 退出 0、不打印汇总，此时 `verify_all.sh:54` 的 grep 又匹配不到那三类文本 → 真的会显示「几何审计（无越界/压叠/字形缺口）」并 PASS。**修法**：①模式改成与 `preview.c` 一致（`child out of parent|inline metric overflow|text overflow|missing glyph|text overlap|list too short`）；②`verify_all.sh:49` 的失败文案按日志内容分流（越界/溢出 vs 字形）；③`preview.c` 在 `PREVIEW_AUDIT_ALL` 模式下末尾打印违规计数并以非 0 退出。

### M30 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/verify_all.sh:77` 把"栈深超预算"误诊成"环境没激活" —— CONFIRMED**：`if python3 tools/stack_check.py >"$OUT/stack.log" 2>&1; then … else bad "栈深检查跑不起来（先激活 IDF 环境或设置 FNOS_IDF_ENV）"; tail -5`。而 `tools/stack_check.py:187-190` 在超预算时打印「N 个入口超出预算：…」并 `return 1`（正常时打印「全部入口在预算内」`return 0`，见 `:191-193`）→ 任一入口超预算都会落进 `else`，把维护者引去折腾 IDF 环境；`then` 分支里那个 `bad "栈深超预算："` 反而几乎不可达（要求退出码 0 且输出里没有"全部入口在预算内"）。主任务栈只有 16384 B（硬约束），这条误诊会直接耽误真问题。**修法**：先按日志内容判定（`grep -q "超出预算" "$OUT/stack.log"` → `bad "栈深超预算"` + 打印该行），再兜底报环境问题。

### M31 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fnos-agent.py:812-814` + `:917`（含 `nas/fnos-agent.service:12`、`nas/install.sh`）默认安装下 8799 无认证对外 —— CONFIRMED**：`tok = getattr(self.server, "token", "")`，`if not tok: return True`（空 token = 鉴权关闭，fail-open）；`srv.token = os.environ.get("FNAS_TOKEN", "")`，而随包发布的 unit 是 `--bind 0.0.0.0 --port 8799` 且没有 `Environment=`，`nas/install.sh` 全仓零 `FNAS_TOKEN` 引用 → **默认安装就是"局域网任意主机可读"**：`GET /api/v1/status`、`/api/v1/history` 会给出挂载路径、卷名/容量、容器名与镜像、网卡 MAC/IP、温度、主机名与 300 条历史（只读，无 `do_POST` → 501；隔壁 FPK 伴侣默认是配对 + sha256，口径不一致）。**修法**：`install.sh` 生成随机 token 写进 0600 的 `EnvironmentFile=`；`main()` 在 token 为空时拒绝启动，除非显式 `--insecure-no-auth`（fail closed）。

### M32 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fnos-agent.py:833-839` 忽略 `?n=` → 历史回填静默永久失败 —— CONFIRMED**：`hist = list(col.history)` 返回整条缓冲，`--hist`/`FNAS_HIST`（`:871`）没有上界；而开发板恒定请求 `?n=600`（`components/fnos_monitor/fnos_data.c:205`），agent 完全不看这个参数。行 ≈ 35 B + 55 B 头，一旦缓冲 > ~930 行，响应体就超过开发板的 `HIST_BUF_SIZE (32*1024)`（`fnos_data.c:56`）→ 被截断 → cJSON 解析失败 → 回填永远不成功、每秒重试、界面无错误提示（曲线一直空白）。同端点的 FPK 伴侣则正确按 `n` 截断（`nas_companion_server.py:799-803 rows = rows[-want:]`）。**修法**：照伴侣实现，`want = int(n or 0) or len(hist); rows = hist[-min(want, len(hist)):]`。

### M33 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas_companion_server.py:624-634` `_read_json` 把坏请求体当 `{}` → 两处"报成功但没做" —— CONFIRMED**：请求体缺失/超过 64 KB/chunked/非 JSON 对象时返回 `{}`。`/api/config`（`:884-892`）于是 `_apply_config({})` → 回 `{"ok":true,"changed":[]}` 且管理页打印"已保存"，实际一条都没改；`/api/collector/docker`（`:917-925`）`on = bool({}.get("enabled"))` = False → **把容器采集静默关掉并持久化**，同样回 ok。管理面（AF_UNIX + 网关头）门控，属健壮性而非鉴权绕过，但"失败报成功"会让人查错方向。**修法**：Content-Length 缺失/超限/体不是 JSON 对象时一律 400；docker 端点要求 `enabled` 必须存在。

### M34 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas_companion_server.py:1248-1250` 只捕 `KeyboardInterrupt` → `finally: state.flush()` 在每次正常停止时都不执行 —— CONFIRMED**：`cmd/main:99` 用 `as_package kill -TERM "$pid"` 停服务，SIGTERM 的默认动作直接终止解释器、不走 `finally`（`:1189-1266`）→ `state.flush()`（`:542`）从不落盘，`last_ip`/`_dirty`（"退出时统一落盘"）每次升级/重启都丢；顺带留下陈旧的 af_unix socket 文件（下次启动 `:1126` 会 unlink，无害）。**修法**：进入等待循环前 `signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))`（SystemExit 仍会执行 `finally`）。

### M35 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/preview/hardware_fixtures.py:26` 无法表达"采集端省略" → 唯一一条运行期 `%d` 警告带从未被任何审计面渲染 —— CONFIRMED**：`"trunc":{"limits":{},"dropped":{}}` 是硬编码空字典，`tools/preview/preview.c` 全文没有 `"trunc"`/`source_dropped`，也没有任何 preview 状态去设置它 → `components/fnos_monitor/fnos_snapshot.c:210-214` 的 `source_dropped` 恒 0；而 `components/fnos_monitor/fnos_ui.c:3897` 的 `set_txt(s_ui.foot_txt,"采集端省略 %d 项 · 请检查采集限制",st->source_dropped)` 是全工程**唯一一条运行期 `%d` 提示**（非 KPI），它在字形/标签溢出/格式残留/点压四个审计面上**一次都没被渲染过**。AGENTS.md 把"硬件清单不得裁剪、超预算必须显式提示"定为硬约束，但"密集清单 + 采集端丢弃 N 项"这一组合在夹具里根本不可表达。**修法**：给夹具加 `dropped` 旋钮并在 `mixed`/`dense` 用上，例如 `"trunc":{"limits":{"vols":24},"dropped":{"vols":2,"disks":19}}`，另加一个 `source_dropped>0` 的状态跑门。

### M36 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/preview/run.sh:62`（配合 `:8`）不清理环境也不断言帧数 → 门可能只跑了个子集仍退 0 —— CONFIRMED**：`tools/preview/preview.c:3520/3522/3526/3535` 在 `PREVIEW_MOTION`/`PREVIEW_NUMBERS`/`PREVIEW_CARDMAP`/`PREVIEW_PAIR_FIRST_ENTRY` 任一被设置时渲染**子集**后 `return 0`；`run.sh` 既不 `env -u` 也不断言产物数量。于是调用方 shell 里恰好残留任一 `PREVIEW_*`（例如 `PREVIEW_MOTION=0,1,0 bash tools/preview/run.sh`）时，这条"一条命令的交付前门"只渲 7 或 ~30 帧就 `exit 0`，六页的图像/溢出/残留/点压四项审计**一个都没跑**，且没有任何输出说明（`:2` 还自称"4 页 × 3 状态"）。**修法**：`env -u PREVIEW_MOTION -u PREVIEW_NUMBERS -u PREVIEW_CARDMAP -u PREVIEW_PAIR_FIRST_ENTRY -u PREVIEW_DATA_MOTION ./build/preview "$OUT"`，并在 `:63` 之前断言 `[ "$(find "$OUT" -name '*.ppm' | wc -l)" -ge 84 ] || { echo "✗ 帧数不足：审计没跑全"; exit 1; }`。

### M37 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/preview/pair_msgs.py:36-38` 只挡"整体抠取失败"，少一个状态会静默缩小审计面 —— CONFIRMED**：唯一守卫是 `if not rows`（全都没抠到）。若某个状态的文案不再匹配 `set_view(FNOS_PAIR_X, "字面量")`——例如移进表/数组、先格式化进缓冲——抽取会**静默少一行**，`PAIR_MSG_COUNT` 随之变小，而 `preview.c:2276-2292` 对 `PAIR_MSGS[PAIR_MSG_COUNT-1].msg` 的断言**照样通过** → 配对卡审计在一个子集上变绿，正是这个工具存在的意义（其 docstring `:4-7`：7 条 FAILED 文案"从来没被渲染过"）。**修法**：`:35` 之后加 `missing = sorted(set(STATES) - {s for s, _ in rows}); if missing: print(f"✗ 这些状态一条都没抠到：{missing}"); return 1`。

### M38 — 主机验证通过

**当前交接**：按卡片可见文本断言，错误 CPU 负向对照通过。

**原审计记录**：

**`nas/fpk/page_check.js:328` 概览断言是整块 innerHTML 子串搜索 → 值全错也能过 —— CONFIRMED**：needle `"2.2"` 命中的是进度条内联 `style="width:2.2%"` 而不是卡片文本，把页面所有 CPU 数字改成 `0.0`（渲染 `0.0 %`）实测仍 `exit 0`。**修法**：按每个 `.card` 切片后再断言"标签 + 数值"。

### M39 — 主机验证通过

**当前交接**：从 HTML 提取点击处理器并实际验证撤销请求 ID；针对当前页面通过。

**原审计记录**：

**`nas/fpk/page_check.js:230-244` 点击表硬编码 5 个 id → 新控件永远不会被点到 —— CONFIRMED**：垫片的 `querySelectorAll('[data-revoke]')` 返回 `[]`，`index.html:324-327` 的设备"撤销"处理器从未被执行，检查却全绿。**修法**：从 HTML 扫出所有 `$('x').onclick=` 目标（照同文件 `:586` 的机械对账）。

### M40 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/api_check.py:121-123` 未登记端点直接 `continue`，仍打印"字段名服务端也都认 ✓" —— CONFIRMED（当前是潜在）**：`if ok is None: continue` 使请求体字段名**一条都不核**；当前 5 条路径恰好与 `accepted_keys()` 全覆盖，所以今天不触发（用假键复现：`/tmp/expA` → `exit 0`）。下次服务端加 POST 端点即无声逃检，而本脚本的唯一职责就是核字段名。**修法**：未登记端点显式报"未覆盖"并非 0。

### M41 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/board_sim.py:156,234` 服务端返回非 JSON 时裸 traceback 崩掉整个脚本 —— CONFIRMED**：`identity`/`status` 返回 500 HTML（或反代页面、应用崩）时 `json.loads` 抛 `JSONDecodeError: Expecting value: line 1 column 1 (char 0)`，④⑤⑥ 三段全不跑、无诊断（`exit 1`，**不是假绿**）。**修法**：`json.loads` 包 try，报"不是 JSON：<前 80 字节>"。

### M42 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/wizard_check.py:159-161` 无法求值的规则被当成"满足" —— CONFIRMED**：`except (TypeError, ValueError): return True` → 规则写坏（如 `{"min":"abc"}`）时判通过，脚本仍 `exit 0` 且打印"校验规则…都对得上 ✓"（`/tmp/expD2` 复现）。另 `:151-156` 对"数值 min/max + 字符串 initValue"用 `len(str(value))` 当长度比 → 误报「initValue='8798' 通不过这条规则」（`/tmp/expD` 复现 `exit 1`，低危）。**修法**：无法求值的规则判 FAIL 并报出该规则；数值比较按数值走，类型不符显式报错。

### M43 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/cmd/config_init:8-16` 端口预检在"我们不拥有该端口"时也判正常 —— CONFIRMED**：`port_busy` 为真时用 `is_running`（`cmd/_common.sh:91-103`）判断"是不是我们的服务在跑"，但 `is_running` 只证明**我们自己的进程活着**，从不检查该进程是否**持有被探测的那个端口**；于是"服务跑在 8080、用户提交已被别的进程占用的 8081"这一组合会走「正常」分支并 `rc=0`。而此时 `config_callback:19` 已经停掉旧实例、`apply_config` 把占用端口写进 `config.json`，`cmd/main:22-25` 随后拒绝启动 → 用户看到「配置已保存，但服务重启失败」：**应用下线且配置指向一个永远绑不上的端口**。**修法**：要求"拥有权"——`is_running && [ "$W_PORT" = "$(effective_port "$(resolve_python)")" ]`，否则失败；`effective_port`（`cmd/_common.sh:184`）已存在、`main` 在用，但这里从没用过。

### M44 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/cmd/_common.sh:173-174`（配合 `wizard/install:65`，消费端 `app/server/fnos_collector.py:200-202`）`interval=0` 未被拦，采集循环空转烧一个核 —— CONFIRMED**：字符过滤 `''|*[!0-9.]*` 只允许数字与点，`0` 通过；向导 pattern `^[0-9]+(\.[0-9]+)?$` 也接受 `0` → `apply_config` 写 `"interval": 0.0`；`load_config`（`nas_companion_server.py:370-407`）不做范围校验 → `CompanionCollector(interval=0.0)`（`:1276`）→ `Collector._loop` 的 `dt = self.interval - elapsed; if dt > 0: wait` **永不等待** → 采样循环死转 `/proc`、`/sys`，顶满一个核。管理 API 自己是有界的（`nas_companion_server.py:946` 强制 0.2–60），这条路径绕过了应用自己的策略。附带：`1.2.3` 能过同一个过滤但在 `float(interval)` 处炸，`config_callback:10` 会把它误报成「写权限」问题。**修法**：`norm_wizard` 里拒绝第二个小数点并做 0.2–60 范围校验（bash `case` + `awk`），与 API 同界。

### M45 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/install.sh:53,56,68,74` 远端 `$STAGE` 未加引号即进入 root 的 `rm -rf`/`mv` —— CONFIRMED（PARTIAL，low-medium）**：`STAGE` 来自 ssh 命令的标准输出，唯一守卫是 `[ -n "$STAGE" ]`（`:54`）；登录横幅/rc 噪声会让它变成多行或非路径，而 `run_root "rm -rf $STAGE"`（`:68/:74`）与 `mv $STAGE/...`（`:56`）都是未加引号的插值 → 多余行会变成 **root** `rm -rf` 的**额外操作数**，路径含空格也会被拆开。**修法**：先校验形状（`case "$STAGE" in /tmp/tmp.*) ;; *) exit 1;; esac`）并把每处都写成 `rm -rf -- "$STAGE"`。

### M46 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/test_lifecycle.sh:25`（跳过 `:52`、`:356`）断言下限与跳过路径不符 → 假红 —— CONFIRMED（low-medium）**：在 fnpack 缺席的环境（**文档记载的正常情况**：`docs/fnos-companion-app-plan.md:48` 装到 `$HOME/.local/bin`，而套件从不把它加进 `PATH`，只有 `nas/fpk/build.sh:14` 加了）跑，143 条断言（`docs/fnos-companion-test-report.md:35`）减去 `[ -n "$FPK" ]` 块里的 10 条 = 133 < `EXPECTED_MIN=142` → 抛「✗ 只跑了 133 条断言…」`FAIL=1`、`exit 1`，`tools/test_report.sh:36-45` 据此记一套红；没有 node 时再少 2 条（`:391/:392`）= 141 < 142，同样假红。而且这条路径上整包基线（缺文件、`__pycache__`、`.DS_Store`、密钥扫描）**一条也不跑**。**修法**：让下限随跳过感知（`[ -z "$FPK" ] && EXPECTED_MIN=$((EXPECTED_MIN-10))`；`command -v node >/dev/null || EXPECTED_MIN=$((EXPECTED_MIN-2))`），或像 `build.sh` 那样导出 `PATH`。

### M47 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/verify_all.sh:51` `ppm2png.py` 失败时既不记 pass 也不记 fail —— CONFIRMED**：`python3 tools/preview/ppm2png.py "$OUT/png" >"$OUT/ppm2png.log" 2>&1 && pass "PPM→PNG"` 只有 `&&` 分支 → 转换失败时报告里既没有这一行 PASS 也没有 FAIL，整条门仍然全绿，PNG 证据缺失却看不出来。**修法**：改成 `if …; then pass …; else bad "PPM→PNG 转换失败（见 $OUT/ppm2png.log）"; fi`。

### M48 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/plan_check.py:97,105,117,129-131` 计划/报告对账在四种情况下静默消失 —— CONFIRMED**：`:97` ROOTS 是相对路径而脚本已定义 `REPO` → 非仓库根目录运行时每个反引号路径都误判失败（或匹配到不相干的树，修法 `os.path.join(REPO, r, p)`）；`:105/:117` `board_fields.json`/`.fpk` 不存在时字段数与包体积检查直接消失（`if os.path.exists` 无 `else`）；`:129-131` 报告文件缺失抛 traceback 被上层吞成"没这条"，报告措辞漂移时**断言条数核对与唯一的"套件失败交叉验证"**（`if failed: problems.append`）双双静默，`rc=0`。**修法**：每处不存在/不匹配都 `problems.append`。

### M49 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/freshness_check.py:75-80` 构建产物陈旧检查的输入白名单漏掉构建文件 —— CONFIRMED**：允许的后缀只有 `.c/.h/.sh/.py` 加 `manifest/config/privilege/resource`；实测被它当"看不见"的构建输入有 `main/CMakeLists.txt`、`main/idf_component.yml`、`components/fnos_monitor/{CMakeLists.txt,Kconfig,idf_component.yml}`、`components/lvgl_mem_psram/CMakeLists.txt`、`components/esp32_p4_wifi6_touch_lcd_7b/{CMakeLists.txt,Kconfig,idf_component.yml}` → 改了这些之后 `build/fnos_monitor.bin` 仍报"不比源码旧"。**修法**：白名单补 `*.txt`、`*.yml/.yaml`、`Kconfig`、`*.csv`、`*.ld`，或改为显式列出构建输入。

### M50 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/soak_check.py:70-71`（配合 `:87-90,:100-108`）staged 副本缺失时"跑的是不是当前代码"检查静默失效 —— CONFIRMED（low-medium）**：`if not os.path.exists(sp) or not os.path.exists(src): continue` → `/tmp` 被清、`--stage` 指错、目录结构变化时该检查直接跳过，脚本最后仍打印「长测在跑、跑的是当前代码、两面都健康 ✓」并退 0——正是这个文件当初被写出来要抓的"第 18 轮"失效；同理 fd 采样 <3 条、rss 采样 <10 条时句柄/内存检查静默 no-op，汇总仍报健康。**修法**：staged 文件缺失记 problem，采样不足打印"窗口不足，未判定"。

### M51 — 部分处理

**当前交接**：缺工具链会记录未运行并失败；plan_check 对上一轮报告对账的顺序问题仍未处理。

**原审计记录**：

**`tools/test_report.sh:119-124`（配合 `:91` 与 `:246`）强制门被跳过只打控制台，且对账用的是上一次的报告 —— CONFIRMED（low-medium）**：`:119-124` 在 `$HOME/.platformio/…/riscv32-esp-elf-objdump` 不存在时**只打印一行提示**就跳过 AGENTS.md 要求的静态栈预算门（报告里没有这一行、不置 `HARD_FAIL`）→ 一份看起来完整的报告里有一道门从未运行，且这正是 H18 那条"objdump 读不了"的入口；另外 `:91` 的 `plan_check.py` 在 `:246` 重写报告**之前**运行 → 每次核对的都是上一轮的 `docs/plan.md`/报告（滞后一轮，也让 H18/H19 类问题晚一轮才被发现）。**修法**：跳过时写一行 `静态栈预算 | 跳过` 并（非交互时）失败；把 `plan_check` 移到报告重写之后。

### M52 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/perf_table.py:48-49`（配合 `:32`）缺列/缺参时抛异常而非按"无数据"处理 —— CONFIRMED（low-medium）**：`f"{med(d.get(case, []), 'iv_n'):.0f}"` 在两个 CSV 的 `--cases` 不一致或缺 `iv_n` 列时抛 `TypeError: unsupported format string passed to NoneType.__format__`（上面 METRICS 循环对 `None` 是打 `--` 的，这一行漏了）；无参数调用时 `files[0]` 抛 `IndexError`。**修法**：复用 `--` 守卫并校验 argv。

### M53 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/cmd/install_callback:14-19`（配合 `cmd/_common.sh:174`）表单校验失败被误诊成"写权限" —— CONFIRMED**：向导填 `interval=1.2.3` 时 `norm_wizard` 的 `*[!0-9.]*` 过滤放行（只挡非数字非点字符），随后 `float()` 抛 `ValueError`，`install_callback` 却把失败原因报成「请检查写权限」——**根因与提示相反**，用户会去改一个无关的权限问题（与 §3.44 的 `interval=0` 是同一处过滤器的两个出口）。**修法**：校验失败时把 `apply_config` 的真实输出透出，并在 `norm_wizard` 里做形状（单个小数点）+ 范围（0.2–60）校验。

### M54 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/nasscreencompanion/cmd/install_init:19-21`（配合 `cmd/_common.sh:147-150`、`wizard/install:14`）向导端口 8799 被放行，随后顶掉独立采集器 —— CONFIRMED（实测退 0）**：向导只把 8799 当提示文案（`wizard/install:14`「请勿填 8799」），`norm_wizard:147-150` 只校验 1..65535，`install_init` 打印「计划监听 0.0.0.0:8799」+「检查通过」并退 0；若独立采集器（`nas/fnos-agent.service:12 --bind 0.0.0.0 --port 8799 --port 8799`）未在跑就没人拦，伴侣应用随后绑定该端口，采集器起来时 `Restart=always` 反复 crash-loop，项目自己的规则（`cmd/_common.sh:7`「不触碰 8799」）被绕过。**修法**：`install_init`/`config_init` 显式拒绝 `W_PORT == DEV_AGENT_PORT`，或在 `norm_wizard` 里钳掉。

### M55 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/devmode_check.py:66-67` `install_callback` 失败时丢弃 returncode/stderr → 装失败被误诊成"服务端中途退出" —— CONFIRMED**：子进程的 `returncode` 与捕获到的 `stderr` 全丢，只按"服务没起来"推断原因，维护者会去查服务端日志而不是安装输出。**修法**：非 0 时打印捕获的 stderr 与退出码。

### M56 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fpk/perm_check.py:76-77` 断言与 `:69` 完全重复，标称验证的"目录"场景从未跑过 —— CONFIRMED（实测 `classify_read(真目录)=error`，IsADirectoryError）**：该行传的是**普通文件**（与 `:69` 同一用例），却挂着"listdir 应当成功"的意图；真正的目录用例会因 `IsADirectoryError` 失败，所以这条覆盖率是虚的。**修法**：删掉重复行，改成真目录用例或按函数契约显式断言。

### M57 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`tools/preview/run.sh:13,61` 输出目录来自位置参数却被直接 `rm -rf` —— CONFIRMED（数据丢失）**：`OUT=${1:-out}`（`:13`）后先 `cd "$(dirname "$0")"`（`:8`），`:61` 是 `rm -rf "$OUT"`，全程不校验取值。因此 `bash tools/preview/run.sh ..` 会删掉整个 `tools/`，`bash tools/preview/run.sh ../..` 会删掉仓库根——而这正是 README/文件头（`:4`「用法：`tools/preview/run.sh [输出目录]`」）鼓励的调用方式。与 §3.28 的 `tools/preview/palettes.sh:14` 属同一类缺守卫。**修法**：拒绝绝对路径与含 `..` 的取值（`case "$OUT" in /*|*..*) die;; esac`），或改成脚本自己在 `mktemp -d` 下建子目录、只允许删自己建的那个。

### M58 — 待处理／待复核

**当前交接**：本轮没有完成该编号全部子项的复现与整改。先按现源码确认触发，再补故障对照、修复和回归。

**原审计记录**：

**`nas/fnos-agent.py:70-78,253` `fstype` 精确匹配漏掉未列名的 FUSE 子类型 —— CONFIRMED**：`SKIP_FSTYPES` 显式列了 `"fuse"`、`"fuseblk"`、`"fuse.sshfs"`、`"fuse.mergerfs"`、`"fuse.rclone"`，而判定是 `if fstype in SKIP_FSTYPES`（`:253`）→ **`fuse.davfs2`、`fuse.gcsfuse`、`fuse.s3fs`、`fuse.cryfs`、`fuse.bindfs`、`fuse.encfs` 等一律漏过**，直接走到 `os.statvfs(mnt)`（`:260`）。而 `:75` 的注释正是为这类文件系统写的：「网络/融合文件系统：对端一挂，`statvfs` 会进不可中断的 D 态，采样线程再也回不来」——即实现没有兜住自己声明的风险（NAS 上 WebDAV/对象存储/加密盘挂载很常见）。**修法**：改成前缀判定 `fstype == "fuse" or fstype.startswith("fuse.") or fstype in SKIP_FSTYPES`（或把 `statvfs` 挪到带超时的子进程里）。

### M59 — 主机验证通过

**当前交接**：配置移动、恢复、冲突、构建失败与模板扫描均修；故障注入和实际发布门通过。

**原审计记录**：

**`tools/release_check.sh:20-24,44-46` 真实配置挪不动时，发布前的"秘密扫描"会打绿灯 —— CONFIRMED（假绿）**：脚本只有 `set -u`（`:15`），`mv -f "$CFG" "$STASH"`（`:24`）的失败无人检查；而内嵌 Python 对"没有真实配置可对照"的处理是 `print('（本机没有真实配置可对照，只报大小）', …); raise SystemExit(0)`（`:44-46`）→ 于是当 `mv` 因目录只读、文件带 `chflags uchg`（把真实配置钉住是常见做法，AGENTS.md 也要求它留本地）等原因失败时：真实配置仍留在 `$CFG`、`build-release/` **确实编进了真实 Wi-Fi 口令/令牌/NAS 地址**，而这道专门用来抓它们的门**打印一句安抚话后退 0**。**修法**：`mv` 后校验 `[ -f "$STASH" ]` 否则 `exit 1`；把"没有真实配置"与"挪不动配置"区分开，后者必须失败。

### M60 — 主机验证通过

**当前交接**：主机内存测试使用可访问鉴权模式并要求全部端点 200；3000 请求有效路径验证通过。

**原审计记录**：

**`nas/fpk/mem_check.py:53` 默认鉴权配置下有一半请求根本没进被测代码 —— CONFIRMED（实测 401）**：脚本用 `cfg.update(port=0, bind="127.0.0.1", tls_enabled=False)`，未改 `auth_mode`（`nas_companion_server.py:85` 默认 `"pairing"`）且未登记任何设备；按同样方式实跑（端口 0、无 TLS、空 devices）四个 `PATHS` 的响应是 `/api/v1/status → 401`、`/api/v1/health → 200`、`/api/v1/identity → 200`、`/api/v1/history → 401`（原文「未配对：请在 NAS 管理页生成配对码…」）→ 3000 次请求里有一半在鉴权层就被打回，**遥测载荷构造与历史查询这两条最可能泄漏的路径从未被压到**，却照样给出"内存不涨 ✓"的结论。**修法**：给 `state` 预置一个已配对设备（或把 `auth_mode` 设为 `lan`）并断言四个端点都回 200；与 H17（样本不足仍退 0）一起修，才是可信的取证。


---

## 附录 B. Low、文档及未纳入编号的剩余工作

下表是原报告 §5 的分组索引，包含复合条目；不是重新统计出的独立缺陷数量。原文细节仍以原报告为依据。
**逐组处置**（已修／部分已修／复核为非缺陷／未处理＋具体原因与下一轮入口）见[本轮整改与验证记录](dsh-open-items-results-2026-10-11.md) §5——28 个分组每组一行，不留“整块未处理”。

| 分组 | 遗留内容 | 状态／闭环要求 |
| --- | --- | --- |
| ui_kit 死 API／主题双真值 | 无调用 API、废弃颜色宏、字符串颜色常量 | 待确认真实调用与外部合同后清理，避免误删公开 API |
| 头文件契约 | 列数／data_fit、调色板数量、guard、页面与线程约束、时间戳语义、HTTP/TLS／双端口口径 | 待逐文件对齐当前实现 |
| 低危数据／并发 | perf 命令修改输入、夜间标志跨任务、NAS 时间上界、历史回填顺序 | 待触发验证；不能因主机 sanitizer 通过而认为竞态不存在 |
| UI 细节 | OOM 单拍半更新、Wi-Fi 口令清理／reveal、字体测量不一致、卡片祖先页号 | 待处理；清理敏感 RAM 需兼顾 LVGL 标签副本 |
| 数字动画 | 比较字符数而非实测宽度导致退场裁切；预算耗尽无统计 | 待实测宽度回归及降级可观测性 |
| 硬件 fixture | RAID dense 未给正常 health/what，缺维护状态变体 | 待补；本轮四种矩阵通过不等于这些语义分支已覆盖 |
| 快照／主机对象生命周期 | preview 多处状态句柄未 release，OOM 早退；SEEN_MAX 诊断边界 | 待逐路径核查；macOS 无 LeakSanitizer 不能证明无泄漏 |
| FPK shutdown／日志 | shutdown 仅关单监听器、每秒遥测刷日志、URL token 进入日志 | 待核实际调用与日志脱敏；板端使用头部 token 不代表其它调用安全 |
| FPK 设备并发／TLS 降级 | revoke/check_token/note_seen 锁协议；证书失败后明文启动而配置仍 TLS | 待故障／并发回归与明确失败策略 |
| 网络摘要回退 | agent 与 collector 回退摘要丢 state/speed/physical | 待与 M02 同步契约修复 |
| 图像产物真实性 | PPM EOF 死循环、长度／格式不校验、空目录成功；motion_view 仅验 PNG 签名 | 待损坏／截断／空目录负向测试，不能仅凭文件存在验收 |
| 滚动／页面断言细节 | 文件头阈值与实际阈值不一致；seq=0 自证 | 待明确阈值合同和真实历史字段负向对照 |
| 配置保留测试 | install_callback never-start 断言缺信息量 | 待断言真实 config/devices 内容 |
| 栈分析能力 | 只认 addi 栈帧、大帧寄存器减法未覆盖；回调和中断未计入 | 待增强反汇编识别与负向合成用例；静态结果仍为下界 |
| 工具链／验证入口 | verify_all 未解析工具路径，失败归因与预览格式转换门不完整 | 待与 M29/M30/M47 合并修复 |
| perf/reason/parse 工具 | 不完整 bench 行计入；reason 两侧零集合自证；历史环长期待值硬编码 | 待同源配置、零覆盖失败与完整行判据 |
| collector mock | patch monotonic 而实现用 time.time | 待改为命中真实被测路径并验证调用次数 |
| 升级生命周期 | 注释与先停后备份顺序不一致、同版本覆盖回滚点、FLAG 写失败静默 | 待故障注入、回滚点保护及真 NAS 升级验证 |
| 卸载令牌口径 | 文案称已删令牌，脚本未删除配置；平台是否清数据未知 | 待真实 fnOS 卸载复装验证，不能先宣称删除完成 |
| 安装预检 | A&&B||C 受输出命令退出码影响；只探 loopback 漏网卡地址占用 | 待明确条件分支和绑定地址矩阵 |
| 拍屏工具 | page_shot 未核 capture 返回码／图像路径，异常 JSON 处理弱 | 待失败注入；保存 PNG 不代表物理验收通过 |
| preview 配置 | LVGL 配置缺失时无响亮兜底、stub capability 位值与真实 IDF 不符 | 待配置缺失／冲突负向对照 |
| docker/devmode 检查 | finally os._exit(0) 掩盖异常、accept 无超时、协议异常处理、临时树／进程收尾 | 待收紧退出码、超时、wait 与清理 |
| 解释器／路径文档 | startup docstring 旧路径；idf.sh 版本环境值、解释器可执行性和 xcode 选择 | 待按实际支持环境验证，不打印个人配置 |
| cert_info 未覆盖 | certcheck 抽取器遗留“顺带测试 cert_info”的死桩与错误承诺 | 待删承诺／死桩或真正纳入 x509 函数测试；本轮只覆盖证书边界和指纹 |
| SSH 信任边界 | preview-naming 关闭主机密钥验证，口令路径与远端临时执行 | 待固定已确认主机身份与安全传输／执行；不擅自连接或改 NAS |
| 对外文档 | test 计数、旧 RX_BUF_SIZE、包体积、perf Kconfig 名、night API、四页／六页 | 待以最终候选一次对齐，注明历史快照 |
| 本轮已修的 low 子项 | test_report grep 计数、release 短值／缺定义扫描 | 针对性已处理；同组其它条目不因此自动关闭 |

## 附录 C. 已有证据能说明什么、不能说明什么

| 证据 | 已能支持 | 仍不能支持 |
| --- | --- | --- |
| 84 张 PNG 与四类审计 | 本轮默认样本可渲染并满足所检查几何／字形条件 | 全运行态、所有名称、所有清单、真实触控与物理显示 |
| 四种清单矩阵 + OOM | 当前夹具与注入路径通过 | 所有硬件组合、真正 PSRAM 枯竭、任意规模完整性 |
| ASan/UBSan | 覆盖输入下未再触发首帧／畸形 PEM 等错误 | 未覆盖路径、数据竞争、实机链接布局与长期运行 |
| 固件 build + 静态栈 | 当前配置可编译，识别到的直接调用链下界在预算内 | 回调／中断、大帧识别、运行期高水位、无重启 |
| 143 项模拟生命周期 | 本地模拟安装、管理、TLS、停止等路径的既有断言通过 | 真实 fnOS 权限、应用中心、卸载数据语义及真实 NAS 环境 |
| 12 个 gate 测试方法 | 已列故障注入不会按原路径假绿 | 全部工具错误、所有环境及未登记路径 |
| 通用构建／秘密扫描 | 当前模板候选与已检查真实值隔离，配置恢复完整 | 所有历史无隐私、未来构建无秘密、任何二进制可直接公开 |
| soak 自测 55／diag 162 | 评价逻辑与诊断抽取的指定合成场景通过 | 真实 72 小时 PASS、板上诊断固件身份 |
| OCR 增量复核 29 文件 | 本轮明确差异经过规则与人工复核 | 全仓所有问题已修、原审计全部闭环 |

原始测试日志位于本机临时目录；临时目录可能被清理，不能作为长期唯一证据。正式闭环应把脱敏摘要、候选身份、命令及退出码保存到持久验收记录，原始日志、真实配置、响应和物理图片保留在私密证据目录。

## 附录 D. 本文编制行为

本文整理阶段仅新增 Markdown；随后按用户要求，将前一轮修复、测试及两份交接文档一并本地提交。没有新增产品代码修复、重跑设备测试、烧录、复位、安装 NAS 应用、推送或改写历史。读取了现有审计／修复记录、当前 Git 与本地包身份，并只读回查远端 main。未对 60 个 medium 逐项重新复现。
