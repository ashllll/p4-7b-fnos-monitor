# p4-7b-fnos-monitor

本目录是 ESP32-P4 7B 的开发工程。继承 `../AGENTS.md` 的嵌入式开发/发布分离规则与用户当前提供的全局协作、技能和设备约束。

- **本项目协作规则（开发目录与发布目录均适用）**：Codex 直接完成实现、调查与审核，禁用所有 DeepSeek Harness / DSH 任务委派；此规则覆盖旧的自动委派与 OCR + DSH 流程，只有用户再次明确授权才能恢复。
- 开发目录：`/Users/llll/code/esp/p4-7b-fnos-monitor`。
- 发布目录：`/Users/llll/code/esp/p4-7b-fnos-monitor-release`（2026-10-08 已核实独立克隆，main，origin `https://github.com/ashllll/p4-7b-fnos-monitor.git`）。禁止在开发目录执行源码发布、推送或 PR；发布目录保留既有远端历史。
- 真实配置 `components/fnos_monitor/fnos_config.h` 留本地，使用 `.example.h`；不发布个人固件、串口日志、截图或凭据。
- UI 唯一源码是 `components/fnos_monitor/fnos_ui.c` + `ui_kit` 构件；旧 JSON/MVVM 管线已移除。当前实现见 `docs/ui-v12-lvgl-native.md`，最新整改与验证见 `docs/ui-fixes-2026-10-07.md`；总览环表/趋势/卷比较设计合同与验证见 `docs/ui-overview-cuktech.md`；六页骨架重排（六页 IA + 12 栅格 + 首页 443px 纵向预算）见 `docs/ui-iter3-skeleton-2026-10-08.md`，剩余问题清零（告警页体检块、温度展开串口取证、结论带 %d 漏屏修复）见 `docs/ui-iter4-remaining-2026-10-08.md`。
- 当前视觉方向见 `docs/ui-cuktech-2026-10-14.md`：默认 `cuktech-console` 主题（ULTRA 黑底蓝青／朱红；配色与发布证据见 `docs/ui-ultra-release-2026-10-14.md`）、图表与仪表数字优先、全宽内容和底部页导航、按设备排列的遥测单元；六页、详情、诊断、配网与配对使用同一套组件语言。2026-10-20 的工艺整改、图标层与首页重排（读数与单位同原子、顶栏入口常驻、详情面板收回内容区、卡头/行内笔画图标、hero 首行 + 2×2 状态瓦片、告警事件流）见 `docs/ui-craft-2026-10-20.md`。原 UniFi 方案保留为历史记录 `docs/ui-unifi-v11-2026-10-12.md`；原生跟手动效的历史记录见 `docs/ui-device-reference.md`。改动页面控制器后同时运行 `PREVIEW_MOTION` 及首次入页/重复目标回归，命令见 `tools/preview/README.md`。
- **UI 目标屏幕（2026-10-14 用户修正）**：只迭代当前 ESP32-P4 7B 开发板的 1024×600 横屏；不再开展窄屏、竖屏版本或相关验收。动态设备数量、长名称、清单重排与滚动可达仍需验证。此要求覆盖历史文档中的多尺寸 UI 迭代要求。
- 改文案/字号后运行 `bash tools/gen_fonts.sh`，再运行 `bash tools/preview/run.sh`。不要手改生成字体。
- 改版面/控件尺寸后，`tools/preview/run.sh` 的四条审计会替你兜住"用户根本看不见"的错：字形覆盖、标签溢出、**子对象跑出父对象**、**可点击控件互相压住**。后两条是补上来的——配对键盘的四行键原先只给了装三行的盒子，第四行（删除/0/确认）整排掉到卡片外压在"关闭"按钮上，而字形审计与文字断言全都不说话：那三个键"存在"，只是画错了地方。**动盒子高度就必须回头算里面装的东西要多大。**
- **屏上不许出现没被消费掉的格式说明符**（`%d`/`%s`/`%u`）：`set_txt(x, "%s", cond ? "A" : "容器 %d/%d 运行", n, m)` 会把 `%d/%d` 原样印到设备屏上（2026-10-08 实机抓到，样张当时恰好没覆盖那条分支）。`tools/preview/preview.c` 的 `audit_format_residue()` 已挂进每个审计面（样张 / 夜间矩阵 / 夜间样张），出现即 abort；新增状态分支时也要给样张，否则规则管不到它。
- **动了 UI 结构（往启动路径或 500 ms tick 里加构件/加大局部变量）之后，先跑 `python3 tools/stack_check.py`**：它从 ELF 静态算各任务调用链的栈深下界，对上预算。本项目被栈溢出烧过两次（主任务栈默认只有 3584 B，已调到 16384；`fnos_ui_create()` 这类"一次性建一大堆构件"的函数单帧能到 11 KB 量级），症状是 `Stack protection fault` + 崩溃 PC 落在堆分配里、设备每几秒重启——**主机预览栈是 MB 级，永远测不出来**。这是静态下界，函数指针回调与中断嵌套不在内。
- `./idf.sh build` 使用已验证的 ESP-IDF 5.5.3 / esp32p4 rev3.x 环境。构建不自动授权烧录/复位、提交、推送或设备外部修改；执行这些阶段需依当前用户授权。
- 只有 LVGL 线程写 UI；数据通过 `fnos_data_get`/历史接口读取，不在 UI 内请求网络。
- NAS 硬件清单必须随实际数据自适应：不得按型号、设备数量或名称长度裁剪卷、阵列、磁盘、网口、容器、温度通道。清单增减、长名称换行、卡片重排和滚动可达都需验证，在 1024×600 横屏上覆盖不同硬件数据组合，不能只检查默认样例。
- 快照含动态清单与借用字符串；句柄用 `{0}` 初始化，跨刷新保留时使用 `fnos_status_copy`，结束用 `fnos_status_release`，禁止裸结构体复制。传输与快照使用 Kconfig 字节预算，超预算或 UI 内存不足必须明确提示，不能静默少显示设备。
- 硬件矩阵、分配失败及字库配置命令见 `tools/preview/README.md`，最新完整记录见 `docs/ui-hardware-adaptive-audit-2026-10-08.md`。测试 fixture 的数量不是生产上限。
- 物理屏幕验收按上级约定使用 `capture-board`，不能将主机预览/构建通过等同于实机验收。

## Agent skills

### Issue tracker

默认使用本地 Markdown，复用既有验收清单；新规格和票据按需创建。见 `docs/agents/issue-tracker.md`。

### Triage labels

本地分诊使用五个标准角色，不自动创建远端标签。见 `docs/agents/triage-labels.md`。

### Domain docs

单上下文：`GLOSSARY.md` + `docs/adr/`。探索、设计和评审先读术语及相关 ADR；共享 UI、遥测动效和数据可信度的阅读入口与当前验证边界见 `docs/agents/domain.md`。
