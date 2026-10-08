# FNOS 监视器 UI 重设计 · v11 设计合同

> 双 skill 合同：`cuktech-screen-ui`（小屏状态/遥测语法）× `interface-design`（产品界面工艺）。
> 本轮交付物 = **渲染图**（HTML/CSS 原型经 Chrome headless 截图），不是实机验证。烧录/LVGL 移植属下一阶段。
> 原型源码（**已归档，只作历史参考**）：`docs/archive/ui/mockup-v11/`（tokens.css / shell.css / widgets.css / fixtures.js / pages.js / render.html）。
> 实现早已换到设备端 `components/fnos_monitor/ui_kit/` + LVGL 原生，见 `docs/ui-v12-lvgl-native.md`。

---

## 1. 设备合同

| 项 | 值 | 依据 |
|---|---|---|
| 主控 | ESP32-P4 rev v3.2 | AGENTS.md 红线 |
| UI 框架 | LVGL 9.5（对象默认 CLICKABLE） | AGENTS.md 红线 |
| 目标分辨率 | **1024×600（主参考）** / 800×480 / 1280×800 | 三款 7"~10" 屏 |
| 像素模型 | `u = min(100vw/1024, 100vh/600)`，布局恒 1024u 宽 | 三档均 width-bound（0.781/1.0/1.25 < 高度比），分辨率自适应 = u 缩放 + 纵向呼吸 |
| 色深 | RGB565（禁大面积渐变，防 banding） | 用户先前反馈"花花绿绿" |
| 内部 RAM | 361 KB；lv_font_conv 必须 `--no-compress`（RLE 与 `LV_DRAW_SW_DRAW_UNIT_CNT>1` 竞态） | AGENTS.md |
| 字体 | `tools/fonts/Inter-{Regular,Medium,SemiBold}.ttf` + `NotoSansSC-{Regular,Medium}.ttf`，原型与设备共用 | 字形一致，CJK 不落豆腐块 |
| 交互 | 触摸；点击目标 ≥44rem（WCAG 44×44 等效） | `.bay`/`.ev`/`.nav-item` 最小 40–44rem |

## 2. 数据合同（`GET /api/v1/status`，fnos-agent.py :8799）

- 顶层：`v/host/ts/ready/uptime_s/errors[]`
- `cpu{pct,cores,load1/5/15,temp_c|null,runq,procs}` · `mem{total_mb,used_mb,avail_mb,pct,swap_*}` · `net{if,rx_kbs,tx_kbs,rx_total_gb,tx_total_gb}`
- `vols[]{mnt,fs,total_gb,used_gb,free_gb,pct}` · `raid[]{dev,state,lvl,members,ok,have,want,sync_pct(-1=none),what}` · `disks[]{dev,rd_kbs,wr_kbs}`
- `temps[]{c,dev≤20,ch≤20,dn≤28}`（板端 fnos_data.c:148-153 按 dev+ch 字典序重排，UI 行序与之一致，不随温度跳动）
- `docker[]` · `zfs|null` · `alerts[]{lv,m}`；裁剪上限：`disks[:10]`/`docker[:16]`/`alerts[:8]`/temps 48（FNOS_MAX_VOLS 12 / TEMPS 48 / DISKS 10 / ALERTS 8）
- **缺失/异常语义（UI 必须区分，不得混同）**：
  - `!ever_ok` → KPI 显示 "—"，不用 0 冒充；
  - `hottest≤0` → "温度不可用"；`ntemps=0` → "采集端没有上报温度通道"；
  - `dn` 空 → 退化为 `dev`；`ch` 空 → 单行行形态（形态由数据决定）；
  - `stale` = 2–3× 期望轮询间隔 → 徽标"旧值"，值灰显（silk-3），**绝不用危险色**；
  - `!online` → "保留旧数据·年龄"，旧数据灰显、带年龄，不闪不跳。
- **统一阈值（全项目唯一一套）**：用量 80 warm / 90 full；温度 60 warm / 75 full。

## 3. 页面问题（每页一个主问题）

| 页 | 主问题 | 主视觉锚 |
|---|---|---|
| 总览 | 现在有没有事？哪台对象出的事？ | 健康带 + 最高优先级事件 + 双 hero（CPU/内存） |
| 存储 | 空间还够吗？阵列安全吗？ | 风险卷置顶 + 卷用量条 + 阵列/同步 + 磁盘活动 |
| 网络 | 进出多少？谁在跑？ | DOWN/UP 双 hero + 趋势 + 连接明细 |
| 系统 | 系统健康吗？什么不正常？ | KPI 行 + **异常优先**事件流 + 容器状态 |
| 温度 | 谁在发热？危险几路？ | 最高温度 hero + 3 列通道池（24 路） |
| 诊断 | 采集链路可信吗？哪段没上报？ | 端点/可信度 + 原因与建议 + 全采集段状态表 |
| 配对 | 现在要做哪一步？ | 每阶段一个主操作（步骤条/配对码槽/指纹/数字键盘） |

## 4. 对象拓扑与签名

- **签名元素：「机箱前面板」**——页眉灯条（健康灯 + 可信度胶囊 + Wi-Fi 条 + 时钟）与 **盘位导轨 LED 行**（`.rail-list`：左 5rem 导轨竖线 + LED 落线 + 盘位行）。
- 对象顺序跨页稳定：温度通道 = dev+ch 字典序；卷 = 挂载点序（风险卷置顶仅存储页）；磁盘 = dev 序；事件 = 严重度→年龄。
- 身份色跨页恒定：`--id-cpu/mem/temp/down/up/store`，运行状态用 LED 色，数据可信度用徽标+灰显，三者独立编码。

## 5. 设计令牌（原 `docs/archive/ui/mockup-v11/css/tokens.css`，现落在 `components/fnos_monitor/ui_kit/uk_theme.h`）

- 面：`--chassis #0A0C0F` / `--plate #14181D` / `--plate-lift #1B2027` / `--slot #0E1216` / `--etch rgba(255,255,255,.08)`
- 丝印字：`--silk-1 #F2F5F8` / `--silk-2 #AEB8C4` / `--silk-3 #8A94A1` / `--silk-4 #5C6674`
- LED：`--led-ok #35C77A` / `--led-warn #FFB020` / `--led-crit #FF4558` / `--led-idle #4A5260`
- 字号：`--t-hero 44/52 · --t-metric 32/39 · --t-sub 20/24 · --t-body 16/22 · --t-label 13/18 · --t-meta 12/16`
- 间距 `--s1..--s6 = 4/8/12/16/24/32rem`；`--head-h 56rem / --foot-h 40rem / --rail-w 100rem`
- 对比度实测（WCAG 相对亮度）：silk-1 ≥14.9:1、silk-2 ≥8.1:1、silk-3 ≥5.3:1（小字安全线 4.5:1 之上）；
  **例外条款**：`--silk-4`（3.06–3.36:1）与 `--led-idle`（2.26:1）只用于非文本装饰（分隔符 "·"/"—"、off 态 Wi-Fi 条、状态点填充），信息从不由单一色相承载（色+形+文案三通道），故不构成文本对比度违规。

## 6. 自适应规则（"完全自适应" = 数据自适应 + 分辨率自适应）

**总原则**：屏幕有多大就用多少——富余空间必须变成**可见的行/列**或**行间距**，绝不允许"卡片里空着一块、信息却要滚动才看得到"。

**自适应池（`.pool`，本轮核心机制）**
- 任何不定长列表（温度通道 / 存储卷 / 容器 / 采集段 / 风险卷）都渲染成 `<div class="pool poolrail" data-pool data-mincol="N">` + 等量条目；渲染后由池引擎统一排布（`render.html` 的 `adaptPools()`，DOM 走 `cleanAnonText() → adaptPools()`）：
  1. `minCol`（最小列宽 × u）定 `maxCols = floor(poolW / minCol)`，即池宽最多放几列；
  2. 量"自然行高" `h0`（取池内**最大**条目高——两行条目比一行高），估列数 `cols = ceil(n / floor(poolH / h0))`；
  3. 收敛：列变窄会让设备名换行 → 用"列自然高度总和"`colSums()` 判定，超出池高就少一列重试（≤6 次），保证既不为塞列数压扁行、也不留横向空白；
  4. 行高保持自然、不强制统一；余量由 `.pool > .pool-col > * { flex: 1 0 auto }` **长进行高**（测量期先加 `.pool-measure` 关掉增长，让列数/收纳判定基于自然行高，排布完再移除）。绝不把两行条目的高度强加给一行条目——曾因此把 `system/alert @800x480` 挤出假滚动条（`155x192>155x159`）。
- 效果：数据少 → 行稀疏铺满整池；数据多 → 自动多列；永远先"用满屏幕"，只有真放不下才走滚动通道。
- `data-fit`（摘要卡专用）：装不下的条目直接收起，`显示 N / M` 写进 card-note——总览「存储风险」卡在 1024×600 显示 2/6、1280×800 显示 3/6，任何分辨率都不出现滚动条。
- `.pool.poolrail` 保留导轨竖线（`::before`），多列时"一列一条导轨"。

**数据自适应**
1. 温度行形态由数据决定：有 `ch` → 两行（dn/ch），无 `ch` → 一行；行序永远字典序，不随温度重排。
2. 设备名**永不缩写**：放不下就换行（`.bay .name .l1` `white-space:normal; overflow-wrap:anywhere`），不用省略号。
3. 次级文本（`.l2`/`.why`/事件原因）允许省略号截断（口径 E）。
4. 池真的放不下时才滚动（口径 S）；`data-fit` 卡改为收起 + 计数，不滚动。
5. KPI 副读数按**设计好的两行**渲染（`subLines()` → `.kpi .sub{display:flex;flex-direction:column}` + `.sub-l{white-space:nowrap;text-overflow:ellipsis}`），不再用 `-webkit-line-clamp` 自动折行——避免把语义单元劈开（曾出现「… · 峰」/「82%」）；严重度前置（`危险 N 路 · 注意 N 路 · …`），任何时候先看到风险。
6. 趋势图量程按**窗口内数据**自适应：`niceCeil(max(series) * 1.25)`（1/1.5/2/3/5/7.5 ×10ⁿ 取整）；**不并入文字读数里的历史峰值**（并入会让曲线重新贴底、图表九成留白）。网络页量程因此从固定 100 变为 30 MB/s。
7. 配对页数字键盘占满卡片剩余高度（`.numpad{grid-template-rows:repeat(4,minmax(44rem,1fr));height:100%}`），键更大且仍 ≥44rem 触控目标；左卡真数据顶部对齐、状态行 `margin-top:auto` 贴底。
8. 空态/缺失态有专门文案与灰显，绝不与真实零值同形。

**分辨率自适应**
1. `u = min(100vw/1024, 100vh/600)`（css/tokens.css `:root{font-size}`）；三档实测均 width-bound，结构不变，池的列数/行距随视口高变化。
2. 纵向：卡高由 `1fr` 行 + 池 `flex:1 1 auto` 吸收余量；放大 = 显示更多行 / 更大行距。
3. 测量窗口必须 H+87（Chrome 窗口外框）：1024x687 / 800x567 / 1280x887，`--dump-dom` 才落在真实视口。

**版面基线（1024u，rem）**：head 56 / rail 100 / foot 40，body 预算 480；总览 120/140/1fr（右下风险卡 = pool + `data-fit`）；存储 112/1fr（右列 auto 1fr）；网络 132/1fr/92；系统 auto/1fr（事件卡 `.events` `max-height:40vh`，条目少时按内容收缩）；温度 68/1fr（池 3 列 × 8 行装满 24 路）；诊断 96/88/1fr/60；配对 48/1fr。

**实测（池几何，1024×600 视口；dbg 自检行）**：温度 866x353 cols=3 need=352（24 路全见）；存储卷 499x309 cols=2 need=154、行长进到 ~103 填满；系统容器 410x193 cols=2 need=130（8 容器全见）；系统温度 410x193 cols=1；诊断 866x153 cols=3 need=120；温度/oldagent 866x353；总览风险 @1280 328x209 cols=1 fit=3。三分辨率 × 12 页/夹具 = 36 组全部 `defects=0 notes=0`。

## 7. 客观测量口径（v2，render.html `?measure=1`）

- `S` = overflow-y 滚动列表超出 —— 设计允许；
- `E` = 省略号截断 —— 设计允许（`.l1` 设备名除外）；
- `C` = line-clamp 设计折行 —— 设计允许；
- `!` = **真缺陷**：纵向裁切、无省略号横向裁切、`.l1` 名字被截。
- 自检脚本（已归档）：`docs/archive/ui/mockup-v11/measure-all.sh`（12 页面×夹具组合 × 3 真窗口 = 36 组）；
  设备端的对应自检是 `bash tools/preview/run.sh`（57 张样张 + 四条审计）。
- `?measure=1` 另输出两段自检：`=== pools ===`（每池 `WxH / cols / row / 溢出 / n / h0 / need / maxCols / cols / perCol`，`data-fit` 池追加 `fit=N`）与 `=== geo ===`（池的父卡几何 + 各列 `items/sum/need`，用于定位"有余量却滚动"的版面）。

## 8. 验收合同（证据分级）

| 级别 | 证据 | 本轮状态 |
|---|---|---|
| L1 原型渲染 | Chrome headless 18 张 PNG + 36 组 measure 报告（**36/36 defects=0 notes=0**） | ✅ 本轮交付 |
| L2 构建 | LVGL 固件编译通过 | ⏳ 下阶段 |
| L3 烧录运行 | 实机运行日志 | ⏳ 下阶段 |
| L4 实机视觉 | 目标屏拍照/截图（色偏、残影、首帧、背光） | ⏳ 下阶段 |

**声明**：本轮全部图像为 HTML/CSS 原型的浏览器渲染，属 L1 证据；未经目标屏实测，不构成"实机视觉通过"。字体位深、缓冲/刷新方式、降级路径在 LVGL 移植阶段按 `hardware-rendering` 参考核验。

## 9. 两 skill 冲突裁决

- `interface-design`（视觉工艺）与 `cuktech-screen-ui`（硬件语法）冲突时，**硬件红线赢**：例——禁渐变（RGB565 banding）优先于"局部高能渐变"；密度不足时减行不缩字；对比度 4.5:1 优先于丝印灰的美感。
- 拒绝的默认做法：卡片墙/渐变背景、KPI 网格盒子、环形仪表、纯字号层级；改为对象行（盘位导轨）、字重+色阶层级、阈值条（80/90 刻度）。
- 检验：签名测试（灯条/导轨/盘位行/阈值条/丝印灰 ≥5 处可指认）；令牌测试（`--silk-*`/`--led-*`/`--plate-*`/`--chassis` 读出即"机箱世界"）。
