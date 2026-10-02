# 界面 v4：KK_UI_UMG 设计语言全量移植

用户指定**完全使用 [KK_UI_UMG](https://github.com/KyleKK04/KK_UI_UMG) 的设计语言**重构四页仪表盘。
该 skill 的设计语言有两层，本次两层都移植：

1. **结构语言（Manifest 是唯一源头）**：UI 的源头是可评审的 JSON Source package，
   C 代码是生成物；View / Controller / Store / Binder 边界固定；静态文案走 `strings.json`，
   动态数据走 `bindings.json` → Store → Binder；交付状态写 `validation.md` 台账。
2. **视觉语言（KK 样例的视觉语法）**：深海军蓝分层表面 + 半透明白叠层 + 细描边 +
   单一强调蓝 + 克制的文本四级层级。

> skill 本体是 Unity UGUI 流水线（C# / Prefab / Addressables），本机无 Unity。
> 本文件记录它在 ESP32-P4 + LVGL 9.5 上的等价移植：**Manifest → 代码生成 → MVVM-C**。
> 上游 skill 已安装在 `~/.agents/skills/kk-ui-umg/`（schema 见其 `references/schema-v054.md`）。

## 1. 设计取舍（与 KK 核心合同一致）

| KK 合同 | 本工程等价物 |
| --- | --- |
| JSON Manifest 是唯一源头，Prefab 是生成物 | `components/fnos_monitor/ui/Source/FnosDashboard/*.json` 是唯一源头，`ui/Generated/` 是生成物（可删可重建） |
| Generated 不可手改 | `*.generated.c/h` 全部由 `tools/ui_gen.py` 生成，手改会被覆盖 |
| 手写 partial 放在生成包根目录 | `FnosDashboardController.c`（业务）、`FnosDashboardViewAnim.c`（纯视觉动效） |
| View 只转发事件 / Controller 写 Store / Binder 写 UGUI | View.generated 只建对象树 + 转发事件；Controller 是唯一 Store 写入者；Binder 是唯一 LVGL 写入者 |
| 静态文案 `locKey` + `strings.json` | 同名机制，生成 `fnos_dash_strings.generated.h` 常量 |
| 动态数据进 `bindings.json` | 同名机制，生成 Store 字段 + Binder 刷屏 |
| Runtime 不读 Source JSON | 固件里没有任何 JSON 解析，manifest 只在构建期消费 |
| `validation.md` 交付台账 | `ui/Source/FnosDashboard/validation.md`，`tools/ui_validate.py` 写 `ui-pipeline:validation-ledger` 标记块 |
| 业务只经 Controller + Service Adapter | `fnos_service_t`（函数表）注册进 UI Manager，Controller `require_service()` 后取快照 |

## 2. 目录

```text
components/fnos_monitor/
├─ ui/
│  ├─ Source/FnosDashboard/            # 人和 AI 维护的唯一源头
│  │  ├─ package.json  layout.json  bindings.json  codegen.json
│  │  ├─ strings.json  assets.json  README.md  validation.md
│  ├─ Generated/FnosDashboard/         # 生成物，可删除后重建
│  │  ├─ fnos_dash_view.generated.c/h      # View.Generated：对象树 + 事件转发
│  │  ├─ fnos_dash_store.generated.c/h     # ViewModelStore：字段 + 脏位 + Update
│  │  ├─ fnos_dash_binder.generated.c      # Binder：Store → LVGL（唯一刷屏点）
│  │  └─ fnos_dash_strings.generated.h     # 静态文案
│  ├─ FnosDashboardController.c/.h     # 手写业务 partial（唯一写 Store 的地方）
│  └─ FnosDashboardViewAnim.c          # 手写视觉动效 partial（只动视觉属性）
├─ kk_ui/                              # LVGL 版 KK Runtime 适配层
│  ├─ kk_theme.h                       # 视觉令牌（§3）
│  ├─ kk_rect.c/h                      # anchor/position/size → LVGL 坐标
│  └─ kk_widgets.c/h                   # Bar/Arc/Trend/Chip/Icon/Signal/Divider 构件
└─ fnos_ui.c/h                         # UIManager：页面生命周期 + LVGL tick
tools/
├─ ui_gen.py                           # Manifest → Generated C（对应 Editor/Generators）
└─ ui_validate.py                      # Manifest 校验 + 写台账（对应 Editor/Validators）
```

## 3. 视觉令牌（KK 语法 = 结构色 + 文本层级；语义色为数据编码扩展）

结构色与文本层级**逐字取自 KK 样例**（`Sample/InventoryPanelSample/Source/KkSampleInventoryPanel/layout.json`）：

```c
/* 结构色（KK） */
KK_BG        0x080B10   /* 底：KK Root #080B10CC 的实心化            */
KK_PANEL     0x161E2B   /* 卡片底：KK #161E2BF2 实心化               */
KK_OVERLAY   0x1C2534   /* #FFFFFF10 叠在 panel 上的等效色（选中/悬浮）*/
KK_INSET     0x19212E   /* #FFFFFF08 叠层（次级内层）                 */
KK_INSET_HI  0x232E3F   /* #FFFFFF0F 叠层（表头/内层浮起）            */
KK_GUIDE     0x334052   /* 描边 / hairline（KK Button 底色复用为描边） */
KK_TRACK     0x3A516B   /* 轨道 / 弱元素（KK 次级填充）               */
KK_ACCENT    0x2F80ED   /* 强调蓝（KK 唯一强调色）                    */

/* 文本层级（KK 四级，逐字取样） */
KK_TEXT1     0xFFFFFF   /* 主数值、标题                              */
KK_TEXT2     0xBFC9D7   /* 标签、正文                                */
KK_TEXT3     0xD7DEE8   /* 次级正文                                  */
KK_TEXT4     0xC8D2E0   /* 辅助、meta                                */

/* 语义扩展（数据编码：身份色 + 严重度；饱和度与 KK_ACCENT 同级） */
KK_OK 0x2FBF71   KK_WARN 0xF2B01E   KK_DANGER 0xF0453D
身份：见 kk_theme.h（CPU 0x2F80ED、MEM 0x4EC9E8、TEMP 0xFF7A1A、
      NET-DOWN 0x4EC9E8、NET-UP 0x2F80ED、ZFS 0xC8E63C）
      ⚠ v4.2 起 TEMP＝橙 0xFF7A1A：此前与 KK_WARN 同值 0xF2B01E，导致温度条/趋势
      常态读成"告警黄"（实机照片坐实）。身份色不得与严重度色同值。
能量渐变：cool 0x4EC9E8 → 0x2F80ED → 0x2FBF71；warm 0xC8E63C → 0xF2B01E → 0xE8622B
用量分级阈值：KK_BAR_WARM 60 / KK_BAR_FULL 85（Bar 运行时自动分级；容量/温度刻度线共用）
```

- **数值统一白（KK_TEXT1），颜色只给条/点**（沿用 v3 评审结论，属于数据可读性约束）。
  例外（状态色照旧）：可信度非 LIVE 时主值降为 `#FFFFFF7A`（白 48%，A3），温度数值按分级给 OK/WARN/DANGER。
- **卡片左强调条＝语义，不是装饰（v4.2）**：有身份对象给身份色（CPU/MEM/TEMP/NET/ZFS）、健康卡给严重度色（动态绑定 `OvHealthAccentColor`）、聚合卡不加条。
- 形状令牌沿用 v3 实践（KK 样例的形状由 sprite 承载、manifest 无形状令牌）：
  卡片圆角 16 / 胶囊 999 / 细条 4 / hairline 1（KK_GUIDE）/ 内边距 18。
  **间距三档（v4.2 版面节奏）**：行内条带·瓦片 8（`KK_GAP_ROW`）、卡片组 12（`KK_GAP`）、区块 16（`KK_GAP_SEC`）。
- 字体沿用现有生成集（Inter + Noto Sans SC，全部 `--no-compress`，见 `fnos_fonts.h`），
  对应 KK 的文本层级：`hero 56 / num 44 / title 28 / label 17 / meta 13` + `cjk 40/24/17/13`。
  KK 样例本身不指定字面（font 走 `fontAsset` 引用），层级关系即其排印语言。

## 4. Schema 扩展（LVGL 目标）

KK schema（v0.54）面向 UGUI。本移植保持其**节点模型**（`type/id/rect/children` +
控件 spec + `locKey`/`bindings`/`events`），按目标扩展：

| 扩展 | 说明 |
| --- | --- |
| `package.json:platform` | `"esp32-lvgl-9.5"`，`designResolution` = 1024×600 |
| 控件词汇 | 保留 `Panel/Text/Image/Button`；新增 `Divider/Bar/Arc/Trend/Chip/Icon/SignalBars`（由 `kk_widgets` 实现）。清单/输入控件本 UI 不使用 |
| `rect` | 语义不变（anchorMin/anchorMax/position/size，负 size = 父边距），由 `kk_rect` 落到 LVGL |
| `text` 扩展 | `fontAsset` 指向 assets.json 的 `LvglFont`；新增 `align`、`letterSpace`、`lineSpace` |
| `image` 扩展 | 新增 `radius`、`borderWidth`、`borderColor`（UGUI 里由 sprite 承载） |
| 字段类型 | `string/int/float/bool` 与 KK 一致；新增 `series`（定长环形采样，绑定属性 `points`） |
| 绑定属性 | `text/color/alpha` 同 KK；扩展 `value`（Bar/Arc/Signal）、`points`（Trend）、`text+color`（Chip） |
| 动态布局绑定 | Panel/Image 扩展 `x`/`width`（int）——数据驱动的堆叠条（P1 容量段）需要运行时算宽/位；其余一律稳定 rect |
| 颜色字符串 | `#RRGGBB[AA]` 带 alpha 通道：`…00` 表达"隐藏"（如空槽位圆点），Binder 同步写 bg_opa/text_opa |
| 事件 | `onClick → OnXxxRequested` 同 KK；扩展 `onSwipeLeft/onSwipeRight → OnXxxRequested`（翻页） |
| `assets.json` | 类型 `LvglFont`（`symbol` 指向 `fnos_fonts.h` 符号）；不伪造贴图路径 |

其余硬规则照抄 KK：Text 不得同时有 `locKey` 与动态 `text` 绑定；静态文案不建 Store 字段；
一个节点最多一个 layout group；`strings.json` 不放运行时数据；不做双向绑定。

## 5. MVVM-C 链路（本工程）

```text
业务（fnos_net / fnos_data = Service）
  → FnosDashboardController.c（唯一 Store 写入者，一次通知批量 update）
  → fnos_store_update_*（脏位）
  → fnos_dash_binder_flush（唯一 LVGL 写入者，只写脏字段）

触摸 / 手势
  → fnos_dash_view.generated.c（只转发）
  → FnosDashboardController_OnXxxRequested
  → Controller 决定状态迁移（切页 / 事件）→ Store → Binder
```

生命周期由 `fnos_ui.c`（UIManager）持有：开机 `Preload`（建整棵树 + 静态首帧）→
`Open` 目标页（原子换页：先隐藏全部旧页并归零偏移，再显示目标页，最后整屏失效）→
LVGL timer tick 上跑 `Controller_Tick`（1 Hz 数据落地，遵守"非 LVGL 任务禁止调 LVGL API"红线）。

## 6. 生成与校验

```bash
python3 tools/ui_validate.py        # 校验 manifest + 写 validation.md 台账
python3 tools/ui_gen.py             # 生成 ui/Generated/FnosDashboard/*
./idf.sh build                      # 构建（生成物进组件编译）
```

- `ui_gen.py` 幂等；`Generated/` 可整体删除重建。
- 台账口径沿用 KK：`Validate / Generate / Verify / Runtime(Pending|Verified)`。
  `Runtime: Verified` 只有实机 PlayMode 等价验收（真机逐页目视）后才允许写。

## 7. 验收合同（沿用 v2 §9，新增结构层证据）

| 证据层级 | 本项目做法 |
| --- | --- |
| 结构 | 本文件 + Source package（manifest 即版面/绑定/文案合同） |
| 构建 | `python3 tools/ui_validate.py && python3 tools/ui_gen.py && ./idf.sh build` 全绿 |
| 烧录 | `idf.py flash` + hash 校验（需用户授权） |
| 运行 | 串口：首帧、首个样本、轮询统计、无断言 |
| 实机视觉 | 手机固定机位逐页拍照 + 判读：裁切、字糊、对比度、切页残留 |

不得用"构建成功/烧录成功/串口正常"替代实机视觉验收。
