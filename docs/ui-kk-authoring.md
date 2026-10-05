# KK Manifest 编写合同（LVGL 移植版）

> ⚠ **历史文档（2026-10-05 归档）**：JSON Manifest（`ui/Source/FnosDashboard/*.json`）与生成管线
> （`tools/{ui_gen,ui_validate,text_width,audit_fonts}.py`）已按用户要求整体删除，界面改为手写 C
> （`components/fnos_monitor/fnos_ui.c` + `kk_ui/`）。本文只作设计史与判据来源保留，
> **其中的流程/命令不再可执行**；当前写法见 `docs/ui-kk.md` §8，删除记录见 `docs/verification.md` §17。

给编写 `ui/Source/FnosDashboard/*.json` 的人/代理用。上位合同：`docs/ui-kk.md`（设计语言）。
先读 `tools/ui_gen.py`（schema 与校验规则的最终定义）与 `docs/ui-audit-v3.md`（v3 现状：几何、文案、格式串的唯一事实来源）。

## A. 节点与 rect

```json
{ "type": "Panel", "id": "PascalCase", "rect": {...}, "image": {...}, "children": [] }
```

`rect.position` = **相对锚点的左上角偏移**；`size` 负值 = `父尺寸 + 该值`（拉伸边距）：

| 场景 | anchorMin | anchorMax | position | size |
| --- | --- | --- | --- | --- |
| 绝对定位（审计里的 `(x,y,w,h)`） | [0,0] | [0,0] | [x,y] | [w,h] |
| 右对齐标签（审计 `ck_label_r(right_pad, y)`，文本宽 w） | [1,0] | [1,0] | [-(right_pad+w), y] | [w, lineH] |
| 横向拉伸（顶栏/底栏/分隔线） | [0,0] | [1,0] | [0,y] | [0,h] |
| 内容区 | [0,0] | [0,0] | [124,72] | [880,472] |
| 页面（父=内容区） | [0,0] | [0,0] | [0,0] | [880,472] |

- Text 的 `rect.size[1]` = 该字号行高（`ceil(fontSize*1.3)`）；生成器会做垂直居中。
- 右对齐 Text 必须配 `"align": "right"`。
- **不做运行时重定位**（v3 的 `place_unit()` 按字体宽度挪单位标签，在 KK 语言里由稳定 rect 取代）：单位/副行给固定坐标。
- 根节点 `root` 是 `{"type":"Panel","id":"Root","children":[...]}`（无 rect，生成器给 1024×600）。

## B. 控件词汇（超出 KK 的部分是 LVGL 目标扩展，见 docs/ui-kk.md §4）

| type | spec 键 | 说明 |
| --- | --- | --- |
| Panel | `image:{color,radius,borderWidth,borderColor}` | 卡片/容器/圆点/色块（圆点 `radius: 5`、`size:[10,10]`） |
| Text | `text:{locKey 或绑定,fontAsset,color,align,letterSpace}` | 静态必须 `locKey` |
| Button | `image:{...}` + `button:{interactable:true}` | 可点，事件 onClick |
| Divider | `divider:{color}` | 1px 线；80%/90% 刻度用 `size:[1,18]` |
| Bar | `bar:{color,radius}` | 细进度条；`radius` = h/2 取整 |
| Trend | `trend:{color,points,areaOpacity,gridLines}` | 面积层+亮线双图；`points:90`、`areaOpacity:12`、`gridLines:0` |
| Chip | `chip:{color}` | 状态胶囊（圆角自动全圆） |
| Icon | `icon:{kind,color}` | kind ∈ overview/storage/network/system（几何图标） |
| SignalBars | 无 spec | Wi-Fi 4 格；rect `size:[27,19]`（锚点 [0,0]） |

卡片（审计 `ck_tile`）统一：`image:{color:"#161E2BFF",radius:16,borderWidth:1,borderColor:"#334052FF"}`。

## C. 视觉令牌（写进 JSON 的色值；与 kk_theme.h 一致）

```
BG #080B10FF   PANEL #161E2BFF   OVERLAY #1C2534FF   INSET #19212EFF   INSET_HI #232E3FFF
GUIDE #334052FF   TRACK #3A516BFF   ACCENT #2F80EDFF
TEXT1 #FFFFFFFF  TEXT2 #BFC9D7FF  TEXT3 #D7DEE8FF  TEXT4 #C8D2E0FF  IDLE #5A6B80FF
OK #2FBF71FF   WARN #F2B01EFF   DANGER #F0453DFF
CPU #2F80EDFF  MEM #4EC9E8FF  TEMP #FF7A1AFF  NET_DOWN #4EC9E8FF  NET_UP #2F80EDFF  ZFS #C8E63CFF
（v4.2：TEMP 由 #F2B01E 改 #FF7A1A——身份色不得与 WARN 同值；降级/隐藏用 #RRGGBBAA，
  如主值 stale = #FFFFFF7A、隐藏 = #161E2B00）
```

v3 → KK 换色表（照抄审计几何，颜色按此替换）：

| v3 | KK | v3 | KK |
| --- | --- | --- | --- |
| `CK_BG 0x0E1114` | `#080B10FF` | `CK_TEXT 0xF5F7FA` | `#FFFFFFFF` |
| `CK_PANEL 0x1A1E24` | `#161E2BFF` | `CK_DIM 0x9BA3AB` | `#BFC9D7FF` |
| `CK_PANEL_HI 0x22272E` | `#1C2534FF` | `CK_IDLE 0x5A6472` | `#5A6B80FF` |
| `CK_NAV_SEL 0x14243A` | `#2F80ED33`（选中底＝15% 强调蓝） | `CK_OK 0x22C55E` | `#2FBF71FF` |
| `CK_GUIDE 0x262B33` | `#334052FF` | `CK_WARN 0xF5A623` | `#F2B01EFF` |
| `CK_TRACK 0x2A3038` | `#3A516BFF` | `CK_DANGER 0xFF3B30` | `#F0453DFF` |
| `CK_CPU/UP 0x3B82F6` | `#2F80EDFF` | `CK_MEM/DOWN/ZFS 0x22D3EE` | MEM/DOWN `#4EC9E8FF` |
| `CK_TEMP 0xF5A623` | `#F2B01EFF` | （ZFS 身份色改走 `#C8E63CFF`） | |

字体映射（审计 `CK_F_*` → `text.fontAsset`）：

| 审计 | fontAsset | 审计 | fontAsset |
| --- | --- | --- | --- |
| `CK_F_HERO` | `font.hero` | `CK_F_LABEL` | `font.label`（`letterSpace: 1`） |
| `CK_F_NUML` | `font.numL` | `CK_F_META` | `font.meta` |
| `CK_F_NUMM`/`CK_F_TITLE` | `font.numM` | `CK_F_CVERDICT` | `font.cjkVerdict` |
| `CK_F_NUMS` | `font.numS` | `CK_F_CLABEL` | `font.cjkLabel` |
| | | `CK_F_CMETA` | `font.cjkMeta` |

## D. id 规则（全局唯一，校验器强制）

- 动态控件 id = 审计 C 变量名去掉 `s_`、转 PascalCase、数组加 0 基下标：`s_ov_val[2]` → `OvVal2`，`s_st_seg[0]` → `StSeg0`。
- 静态标题/装饰 id = 语义 PascalCase，页前缀：`Hd/Ft/Nav`（chrome）、`Ov`（P0）、`St`（P1）、`Nw`（P2）、`Sy`（P3）：如 `OvHealthTitle`。
- 根 `Root`；页面 Panel 必须叫 `P0Overview` / `P1Storage` / `P2Network` / `P3System`；内容区 `ContentArea`；顶栏 `TopBar`；导航 `Rail`；底栏 `Bottom`。

## E. 静态文案（strings.json 片段）

- 审计里**写死的文案**（标题、单位、标签、"无告警"等）一律 `text.locKey`，key = `<域>.<snake名>`，域 ∈ `chrome/overview/storage/network/system`（如 `overview.health_title`、`system.kv_endpoint`）。
- 文化：`zh-Hans` 必填，`en-US` 一并给（可与中文同值）。
- 审计里标"运行时填"或初值 `""`/`"--"` 由数据决定的，**不要**做 locKey：做成动态绑定（见 F），初值文案写进字段 default。
- 一个 Text 节点不能同时有 `locKey` 和 `text` 绑定（校验器 TXT003）。

## F. bindings.json 片段（字段 + 绑定）

字段类型只有 `string/int/float/bool/series`。**所有格式化（printf 串）在 Controller 做**，字段传最终字符串（KK 里 Store 存什么由 Controller 决定）。

| 控件 | 绑定 property | 字段 id | 字段类型 |
| --- | --- | --- | --- |
| Text | `text` | = 控件 id | `string`（`maxLen`：主机名/卷名/容器名 32，格式化数值 24，告警/错误串 80，其余 48） |
| Text | `color` | `<Id>Color` | `string`（`maxLen: 10`，存 `#RRGGBB`） |
| Panel/Image | `color` / `alpha` / `x` / `width` | `<Id>Color` / `<Id>Alpha` / `<Id>X` / `<Id>Width` | `string` / `float` / `int` / `int` |
| Bar | `value` | `<Id>Value` | `float`（0..100） |
| Trend | `points` / `top` | `<Id>Points` / 复用共用 `top` 字段 | `series`（`capacity: 90`）/ `float` |
| Chip | `text` / `color` | `<Id>Text` / `<Id>Color` | `string(80)` / `string(10)` |
| SignalBars | `value` | `<Id>Value` | `int` |

- 绑定条目：`{"controlId":..., "fieldId":..., "mode":"OneWay", "property":...}`。
- 字段 default：字符串 `""`、float/int `0`、bool `false`、series `null`。
- 一个字段可绑定多个控件（如 4 条趋势共用 `NwTrendTop`）。
- 动态面板位置（如 P1 容量堆叠段）用 `x`/`width` 绑定：字段 `<Id>X` / `<Id>Width`，初值给审计的 x 与 0 宽（生成器最小宽 1px，运行时 Controller 置真值）。

## G. 事件

| 控件 | event | handler |
| --- | --- | --- |
| `NavOverview/NavStorage/NavNetwork/NavSystem`（Button） | `onClick` | `OnNavOverviewRequested` / `OnNavStorageRequested` / `OnNavNetworkRequested` / `OnNavSystemRequested` |
| `P0Overview` | `onSwipeLeft` / `onSwipeRight` | `OnOverviewNextRequested` / `OnOverviewPrevRequested` |
| `P1Storage` | 同上 | `OnStorageNextRequested` / `OnStoragePrevRequested` |
| `P2Network` | 同上 | `OnNetworkNextRequested` / `OnNetworkPrevRequested` |
| `P3System` | 同上 | `OnSystemNextRequested` / `OnSystemPrevRequested` |

handler 全局唯一、`OnXxx` 驼峰（校验器强制）。翻页不使用 LVGL gesture（本工程未开 `LV_USE_GESTURE_RECOGNITION`），生成器对滑动页挂 PRESS/RELEASE 差分。

## H. 自检清单（交稿前跑）

1. 片段 JSON 能被 `python3 -m json.tool` 解析；
2. 所有 id 唯一；每个动态控件都有绑定条目；每个 Text 要么 locKey 要么 text 绑定；
3. 所有 locKey 都出现在自己的 strings 片段里；
4. 坐标与审计逐字一致（只换颜色/圆角/字体），无"大约值"；
5. `python3 tools/ui_validate.py` 必须 **lint: 0 warning(s)**——lint 会拦两类实机踩过的坑：
   子节点越出父宽高、文本估算宽度超出文本框（按文化分别估宽：CJK 1.0em / ASCII 0.55em）。
   动态文案按字段 `default` 估预算：格式串在 Controller 里就要算进 max-chars，别指望运行时能放下；
   再跑 `python3 tools/audit_fonts.py` 必须 **PASS**（0 error）——它按"实际会送上屏的文案"查豆腐块
   （Text 节点 locKey/默认值 + Controller `SETS/SETS_F` 格式串）、字库 RLE、僵尸字库，
   以及字段缓冲预算（格式串最坏字节数 vs `maxLen`，v4.2.2 的截断就是这么漏出去的）；
   改过中文字形就重跑 `bash tools/gen_fonts.sh`（它会顺带清掉非本次产出的旧字库）；
   **动过字号或框宽**再跑 `python3 tools/text_width.py --layout`（读生成字库的真实 `adv_w`，0 问题才交稿）——
   lint 的 0.55em 估算偏乐观：v5 就是它把 `NwTotalVal` 的 158px 框算过关、真实字宽 162.3px 放不下
   `118.00 MB/s`；两者互补，改版面时都要过；
6. 换色先查语义：**身份色不得与 OK/WARN/DANGER 同值**（v4.2 的 TEMP 撞 WARN 就是这么来的）；
   文本弱色用 `KK_TEXT5`（≥4.5:1），`KK_IDLE` 只给数据 idle 圆点（v5）。
7. **改完版面先看主机预览再烧录**：`python3 tools/ui_gen.py` 之后跑 `bash tools/preview/run.sh`，
   看 `out/01-live-p0..p3.png`（另含 `02-offline-*` / `03-warming-*`，共 12 张）——同一份 LVGL + 字库 +
   `kk_widgets`/Controller 在 macOS 上渲染，像素级可信，增量一轮 8 s，替代"构建→烧录→拍照"的十几分钟。
   静态工具管"放不放得下/有没有豆腐块"，预览管"好不好看、对不对齐"；触摸手势、刷新率、PSRAM/DMA 采样路径
   与真实 Wi-Fi 时序仍必须烧录实机验证（前置与坑见 `tools/preview/README.md`）。
