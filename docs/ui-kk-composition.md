# v4.1 版面重排合同（KK_UI_UMG 构图语言）

用户口径：**连版面一起按 KK 样例的风格重排**——不只是换色，构图、密度、卡片形态都要一眼看出是 KK 的设计语言。
信息架构（四页各回答什么问题）与数据合同（`docs/ui-redesign.md` v2 §2-4）**不变**；已验证的可读性约束（数值统一白、语义色只给条/点/描边、no-data 显示 `--`）**不变**。

## 1. KK 构图语法（从样例 layout.json 提取）

| 语法 | 样例证据 | 本工程落地 |
| --- | --- | --- |
| 分层半透明表面 | `#080B10CC` 底 → `#161E2BF2` 主面板 → `#FFFFFF10` 头条 → `#FFFFFF08/0F` 内层 | 三层表面：底 `#080B10` / 卡 `#161E2B` / **卡内条带 `#1C2534`** / 嵌套 `#232E3F`（等效白色叠层） |
| 顶部条 + 图标块 | Header `#FFFFFF10` + 44×44 图标 + 34px 标题 | 顶栏 = 全宽条带：**40×40 强调蓝图标块** + 标题 + 副行 + 右侧状态/时钟 |
| 工具行（横向） | FilterBar 一排控件（Input/Dropdown/Toggle/Slider） | **导航改为顶栏下的横向 Tab 行**（样例没有侧栏）；四页靠 Tab + 左右滑切换 |
| 列表＝卡内条带行 | item slot `#263242E8` 圆角、图标 52×52、主名 + 副名 | 所有成组数据（存储卷/容器/温度/KV/RAID/磁盘）统一成**行条带**（`#1C2534` 圆角 8、高 36-44） |
| 按钮/动作 | `#334052` 圆角矩形 + 白字 | 胶囊/按钮用 `#334052`；**激活/选中用 `#2F80ED`** |
| 强调蓝克制 | `#2F80ED` 只出现在进度填充 | 蓝色只给：选中 Tab、卡片左强调条（3px）、进度/趋势填充、焦点 |
| 文本层级 | 34 / 26 / 22 / 20 / 19 / 18 + 四级灰 | 见 §3 映射 |

## 2. 新骨架（1024×600，替换旧的 顶栏56/侧栏104/底栏40）

```
┌ 顶栏 Header  y 0-64   ────────────────────────────────────────────┐
│ [■ 图标块 40×40] fnOS NAS            [胶囊] [Wi-Fi] [时钟 28]     │
│                192.168.0.119:8799                                │
├ Tab 行       y 72-116  ──────────────────────────────────────────┤
│ [总览][存储][网络][系统]              （选中=蓝底白字，未选=#1C2534）│
├ 内容区       y 124-556  左右边距 20 → 984×432                     │
│ （四页各自的构图，见 §4）                                          │
└ 底栏         y 564-600  ─────────────────────────────────────────┘
│ 轮询统计（左，meta）            [告警胶囊（右）]                   │
```

## 3. 令牌与字体映射（沿用 `kk_ui/kk_theme.h`）

```
表面：bg #080B10 · 卡 #161E2B(+1px #334052 描边, radius 16) · 条带 #1C2534(radius 8)
      嵌套 #232E3F · 描边 #334052 · 轨道 #3A516B · 强调 #2F80ED
文本：#FFFFFF 主数值/标题 · #BFC9D7 标签 · #D7DEE8 正文 · #C8D2E0 辅助 · #5A6B80 弱化
```

| KK 样例字号 | 本工程字体 | 用途 |
| --- | --- | --- |
| 34（面板标题） | `font.cjkTitle`(24) | 页面标题、卡标题 |
| 26 | `font.numM`(28) / `font.cjkTitle` | Hero 副值、时钟 |
| 22/20 | `font.numS`(17) / `font.cjkLabel`(17) | 行主文本、区块标签 |
| 19/18 | `font.label`(15, letterSpace 1) / `font.cjkMeta`(13) | 标签、单位、meta |
| —（数据 Hero） | `font.hero`(56) / `font.numL`(44) | 容量 Hero、DOWN/UP 主值 |

## 4. 四页构图（信息不变，构图重排）

- **P0 总览**：上排 = 健康卡（约 300 宽：卡头条 + 状态大字 `font.cjkVerdict` + 副行 + 右侧告警计数块）+ 遥测四卡横排（每个约 162）：卡内"标签条带 + 主数值 + 单位 + 细条 + 副行"三段式；下排 = 存储空间卡（6 条带行 + 细条）+ CPU/内存趋势卡（两条趋势 + 线尾图例）左右分。
- **P1 存储**：上 = 全宽容量 Hero 条带（大数字 + 单位 + 可用 + **容量堆叠条** StSeg0..5）；下 = 左"存储空间"6 行条带（名称/FS/用量/百分比/条 + 80/90 刻度），右"RAID 阵列"与"磁盘活动"两张卡竖排。
- **P2 网络**：上 = DOWN / UP 两张对称主值卡（主值 44 + 单位 + 副行 + 迷你趋势）；中 = 全宽吞吐趋势卡（双趋势 + 纵轴 NwAxis0..2 + 图例）；下 = 峰值下行/峰值上行/采集延迟/曲线采样 四张条带瓦片。
- **P3 系统**：三栏卡：容器（6 行条带：状态点 + 名称 + 状态词 + 底部告警区 3 行）、温度（7 行条带：名称 + 热条 + 数值）、采集端点（8 行 KV 条带）。

## 5. 硬约束（重排不得违反）

1. **必须保留的 id**（bindings.json 不改的前提）：
   - chrome：`TopBar` `Bottom` `ContentArea` `HdHost` `HdEndpoint` `HdChip` `HdClock` `HdSignal` `FtPoll` `FtAlertDot` `FtAlertLbl`
   - 导航（Tab 化后 id 不变）：`NavOverview` `NavStorage` `NavNetwork` `NavSystem`（Button）+ `NavOverviewLabel` `NavStorageLabel` `NavNetworkLabel` `NavSystemLabel`（Text）+ `NavIndicator`（Panel）
   - 页面根：`P0Overview` `P1Storage` `P2Network` `P3System`（保留左右滑事件）
   - 动态控件 id 与**类型**全部保留（P0 34 个 / P1 60 个 / P2 17 个 / P3 54 个，清单见下）：`Ov*`、`St*`、`Nw*`、`Sy*`（Bar/Trend/Panel 的绑定属性不可改类型）。
2. 行条带统一：圆角 8、内边距 12、行高 36-44；同类内容同一形态。
3. 数值一律 `#FFFFFFFF`；语义色只给条/点/描边（状态色照旧）。
4. 不做运行时重定位；所有 Text 高度 = `ceil(fontSize*1.3)`；右对齐 Text 用锚 [1,0] + `position:[-(right_pad+w), y]` + `"align":"right"`。
5. 静态文案走 locKey（`<域>.<snake>`，域 ∈ chrome/overview/storage/network/system），新增文案写进 strings 片段（zh-Hans + en-US）。
6. rect 语义、控件词汇、绑定命名规则见 `docs/ui-kk-authoring.md`（继续有效）。

## 6. v4.2 优化落点（2026-09-29，实机照片 + manifest 量测驱动）

本节记录在 v4.1 构图之上做的设计优化；§1–5 的骨架与硬约束**继续有效**（既有 id 与控件类型一个没动）。

| # | 优化 | 落点 |
| --- | --- | --- |
| A1 | 身份色 ≠ 严重度色 | `KK_TEMP` 0xF2B01E → **0xFF7A1A**（温度条/趋势/图例点/温度栏强调条）；`KK_WARN` 独占黄 |
| A2 | 卡片左条＝语义 | 健康条走严重度（`OvHealthAccentColor` 动态绑定）；CPU/MEM/TEMP/NET/ZFS 给身份色；聚合卡（存储/趋势/卷/IO/KV/容器）**不再加条**；运行时长卡无身份→无条 |
| A3 | 可信度分层落到数值 | `trust_t`≠LIVE 时主值色 `#FFFFFF7A`（白 48%）：`OvVal0..3Color`/`StHeroColor`/`NwDownValColor`/`NwUpValColor` |
| A4 | 趋势可比 + 有刻度 | P0：CPU+MEM **合轨**（共 `OvTrendTop` 量程）+ 温度独立轨；P2：DOWN/UP **合轨**（共享 `NwTrendTop`），`NwAxis0..2` 三档刻度与轨道对齐；全部 Trend `gridLines: 2` |
| A5 | 动态名称字体回退 | 卷名/RAID/磁盘/容器/温度名 → `font.cjkLabel`；`StVolUse`（含"余"）→ `font.cjkMeta`（原来用无汉字的 txt 字体会画豆腐块） |
| B6 | 容量堆叠段 | 铺满 Hero 条带（x20→964，总宽 929），段高 12→14 |
| B7 | 阈值刻度统一档位 | 容量刻度 80/90 → **60/85**（`StVolTick60*`/`StVolTick85*`）；温度栏新增 `SyTTick60`/`SyTTick75` 竖刻度；数值分级与 `KK_BAR_WARM/FULL` 同档 |
| B8 | 顶栏/底栏去重 | 端点只在顶栏（"采集 host:port"）；底栏专管采集质量 + **数据年龄**（"轮询 n · 失败 n · n ms · 数据 Ns 前"） |
| B9 | P0 主次 | 健康卡 300→**304**（唯一主对象），遥测卡去装饰条 |
| C10 | 版面节奏三档 | 卡缝：P0 行 8、P2 卡组 16→12、P2 瓦片行居中（左右各 1px）；`KK_GAP_ROW/GAP/GAP_SEC` = 8/12/16 |
| C11 | 行高 | RAID/磁盘活动行 38→36（与卷格/容器行同高）；**KV 行 32 保留**：12 行 × 32 + 标题恰满 432，是密度例外 |
| C12 | 不越父 | `NwCtxVal*` 180→158、趋势图例标签 110→76（`ui_validate` lint 自动拦） |
| C13 | 单位基线 | `NwDownUnit`/`NwUpUnit` y 86→90，与主值基线齐 |
| C14 | 文本预算合同 | `tools/ui_gen.py: lint()`（越界 + 文本预算，按文化估宽 CJK 1.0em / ASCII 0.55em）；en-US 标签缩到预算内（Endpoint→Addr、Last Error→Error、Data Age→Age、Internal Heap→Heap、Memory→Mem） |
| D15 | 溢出格式 | Label 长文本 `LONG_CLIP` → **`LONG_DOT`**（省略号收尾，不再留悬空"·"） |
| D16 | DEMO 态 | `FNOS_DEMO_MODE`（默认 0）：开启时顶栏常驻 "DEMO 演示数据"，演示数据不得冒充实测 |

**验证口径**：`python3 tools/ui_validate.py`（含 lint，0 warning）→ `python3 tools/ui_gen.py` → `./idf.sh build` 全绿；
实机视觉仍按 §7 走拍照验收（本次改动尚未烧录实机，`validation.md` 台账 Runtime 回到 `Pending`）。

## 7. v5 优化落点（2026-10-02，密度优先 + 大数字 + fnOS 术语）

完整推导、真实字宽量测与验收流程见 `docs/ui-kk-iteration-v5.md`；本节只记落点与它对本合同的影响。

| 层 | 方向 | 落点 |
| --- | --- | --- |
| L1 版面语法 | 信息密度优先 | V5.0-A KPI 卡内部分层（`OvVal0..2` `[12,44] 112×58` `font.numL`、`OvVal3` 保持 numM、`OvUnit0..3` `[126,80]`、`OvMbar0..2` `[12,106]`、`OvSub0..3` `[12,118]`）；V5.0-B 存储行新增 `OvVolUse0..5` `[144,3] 248×23`（`已用 … · 可用 …` 表格化）；V5.0-C P2 五块瓦片标签降级 + 数值放大 |
| L2 数字排版 | 极简大数字 | 三张 KPI 主值 28→**44px**（36px 中间档不存在于现有字库，故一刀切到 44）；单位列右移贴基线 |
| L3 术语与信息架构 | fnOS 对齐 | "NAS 健康"→**系统状态**、"RAID 阵列"→**阵列状态**、"磁盘活动"→**硬盘活动**、"内部内存"→**固件内存**（zh-Hans + en-US 同步，改的是 `strings.json`，不动布局） |
| 可读性 | 弱色正文与 idle 色分离 | 17 个 Text 节点 `#5A6B80`→`#7B8BA1`（= 新令牌 `KK_TEXT5`）；`KK_IDLE` 不再用于文本 |
| 统计列 | 趋势卡峰值 | 新增 `OvPeakCpu/OvPeakMem/OvPeakTemp`（`[352,105/151/197] 100×17`），图例行距 30→46 |

**对硬约束的影响**：本次**只改位置/尺寸/字体/色值与文案**——id、控件类型、绑定属性、行高（36）、间距三档（8/12/16）全部不变；新增节点仍走 locKey XOR 动态文本绑定（KK TXT003）。`tools/text_width.py`（读生成字库 `adv_w` 的真实字宽量测）成为字号/框宽变更的前置手段（lint 的估宽偏乐观，见 v5 文档 §3 的 `NwTotalVal` 实例）。

**验证口径（v5）**：`tools/text_width.py --layout`（真实字宽，0 问题）→ `bash tools/gen_fonts.sh` → `python3 tools/ui_gen.py` → `python3 tools/ui_validate.py`（0 warning）→ `python3 tools/audit_fonts.py`（0/0）→ `./idf.sh build` → 烧录 + 串口 + 拍照（P0 已验收；P2 瓦片见 v5 文档 §6）。
