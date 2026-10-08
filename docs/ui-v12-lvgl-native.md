# UI v12：LVGL 原生自适应改造（ui_kit）


总览的新环表、独立趋势、上下行图与各卷比较，见 [CUKTECH 总览设计合同](ui-overview-cuktech.md)。以下迁移记录保留当时进展。
> 当前温度展开、参考阈值和自适应布局已继续整改；最新行为及验收边界见 [UI 整改验证](ui-fixes-2026-10-07.md)。下文包含早期阶段记录。

> 目的（用户指令 m01672）：**自适应第一优先**；推进**实机验证**；**清理**残留 UI 方案；
> **不再使用 kk_ui**；全局切换为 **LVGL 原生**构件；**保证动画效果**。
> 前置设计合同见 `docs/ui-redesign-v11.md`（视觉/令牌/阈值/页面问题），本文件只讲实现层怎么换。

## 1. 为什么必须换掉 kk_ui

| | kk_ui（旧） | ui_kit（新） |
|---|---|---|
| 几何来源 | `kk_metrics_recompute()` 先解出一张坐标表，再 `mk_xx(x,y,w,h)` 绝对摆放 | 只给语义（卡片/行/池/KPI），几何交给 **LVGL flex** |
| 换分辨率 | 改推导公式，回归面 = 所有坐标 | 不用改：容器自己伸缩 |
| 余量怎么办 | 公式里留固定高度 ⇒ 要么留白要么滚动 | 行 `flex-grow` 吃掉余量 ⇒ 不留白、不出滚动条 |
| 控件 | 自绘（kk_bar / kk_trend 手工画） | LVGL 原生：`lv_chart` / `lv_bar` / `lv_arc` / `lv_buttonmatrix` |
| 动画 | 无（`grep -c "lv_anim\|LV_ANIM"` = 0） | LVGL 原生 `lv_anim` / `lv_screen_load_anim` / `LV_ANIM_ON` |

kk_ui 的 `kk_layout.h` 只保留"间距阶梯/圆角/阈值/字体行高"这类**令牌语义**（已搬进 `ui_kit/uk_theme.h`），
**坐标推导一律不再保留**。

## 2. 分层

```
fnos_ui.c            页面语义：数据 → 文案 → 填入构件（不做几何）
ui_kit/uk.h + uk.c   自适应布局层：外壳 / 卡片 / 行 / 池 / KPI / 动画（LVGL flex）
ui_kit/uk_theme.h    令牌：颜色（含三套预置）/ 阈值 / 字体 / 度量
LVGL 9.5 原生         flex、grid、chart、bar、arc、buttonmatrix、anim
```

- 只有 **LVGL 任务**能调 `ui_kit` 与 LVGL（红线，见 AGENTS.md）。
- 屏幕上**不允许出现绝对坐标**：唯一例外是圆点直径、发丝线高度这类"尺寸"，
  以及 `lv_screen_load_anim` 自带的位移。

## 3. 自适应模型

**页面** = `flex column`：页边距 `UK_S3`，卡间距 `UK_S3`。
摘要/提示卡 `uk_card_flex(card, 0)`（按内容自然高），主内容卡 `uk_card_flex(card, 1)`（吃掉余量）。

**卡内** = `flex column`：头部（标题 + 右上角说明）固定高，`body` `flex-grow 1`。

**自适应池 `uk_pool`（核心）**：不定长清单（温度通道 / 存储卷 / 容器 / 采集段 / 风险卷）都进池。

1. 条目按需建（温度页只在 `ntemps` 增加时建新行），先挂在池上；
2. `uk_pool_relayout()` 先量**自然行高**（取最大条目高：两行条目 > 一行条目）；
3. `cols = ceil(n / floor(poolH / rowH))`，再被 `floor(poolW / min_col_w)` 封顶；
4. **收敛**：列变窄会让设备名换行、行变高 ⇒ 用"列内高度总和"判超，超了就 `cols--` 重试（≤6 次）；
5. 重挂进各列，行 `flex-grow 1` ⇒ 余量变成行高（这是"不留白、不滚动"的关键）；
6. `data_fit`：仍装不下就隐藏条目并把 `显示 N / M` 写进卡右上角说明（摘要卡专用）。

**数据自适应规则**（沿用 v11 合同，逐条保留）：
设备名永不缩写（放不下换行）；次级文本可省略号；行形态由数据形态决定（有通道名两行、无则一行）；
温度数值色 = 阈值色，但**离线/旧帧降为 T3**（数据可信度压过阈值色，颜色不越权）；
空态/缺失态有专门文案，不与真实零值同形。

## 4. 动画规范（"保证动画效果"）

**实际接线（2026-10-07 Round 2 收口，只留真在用的三件）**

| 用途 | 实现 | 时长 / 曲线 |
|---|---|---|
| 换页入场 | `page_enter()` 逐个子对象 `uk_anim_fade(c, LV_OPA_20, 160, delay)` | 160ms ease-out，错峰 20ms（最多等 120ms） |
| 阈值条/容量条 | `uk_bar_set()` → `lv_bar_set_value(bar, v, LV_ANIM_ON)` | 180ms |
| 严重告警点 | `uk_anim_pulse(dot, on)` 透明度呼吸（**仅在级别翻转时**调用） | 700ms 往复 |

约束：本板 CPU 400MHz，动画只用不触发布局重算的属性（opa / 原生值条）；
**不做布局动画**（宽高动画会每帧重排整池）。

两条踩过的坑（都写进了代码注释，别再犯）：

- **不要用位移做入场**：`translate_y` 会算进对象 coords（`lv_obj_pos.c` 的 `refr_pos`），
  贴边卡片在动画那一帧就被父对象裁掉，主机预览的 `audit_bounds` 会直接报越界。
- **不要每次刷新都重启动呼吸动画**：`refresh()` 每秒跑一次，而呼吸是 700ms 往复；
  每次都 `lv_anim_start` 会把透明度在 1 秒处硬拉回 255，看起来是"卡一下"而不是呼吸 ⇒
  只在状态翻转时调 `uk_anim_pulse()`。

曾经备而不用、现已删除的 API：`uk_page_load()`（`lv_screen_load_anim` 需要第二个 screen，
本板是"一个 screen + 五个隐藏页"，留着只会诱导误用）、`uk_anim_int()`（数值补间没有调用点，
值条观感由 `uk_bar_set` 的原生动画给到）。

## 5. 迁移步骤（每步都要能编译、能看）

1. `ui_kit/` 落地 + 主机证明（`tools/preview/uk_proof.*`，三档分辨率渲染 PNG + 越界/重叠=0）；
2. 逐页替换：**温度（p4）→ 总览（p0）→ 存储（p1）→ 系统（p3）→ 网络（p2）→ 诊断/配对/配网**；
   每页替换后跑 `bash tools/preview/run.sh`（主机渲染）确认无回归；
3. 全部替换完后：外壳改 flex（screen → row[rail | column[head | content]]），删除 `kk_ui/`；
4. `bash tools/gen_fonts.sh`（字库随新文案重新生成）→ `python3 tools/stack_check.py`（栈深）→ `./idf.sh build`；
5. 烧录 + `capture-board` 实机验收（主机预览通过 ≠ 实机通过）。

## 6. 清理清单（残留 UI 方案）

| 对象 | 处置 | 状态 |
|---|---|---|
| `components/fnos_monitor/fnos_ui.c.orig`（1903 行旧备份） | 删除 | ✅ 已删 |
| `build/esp_brookesia_demo.{bin,elf,map}`（2026-09-20 的 brookesia demo 产物） | 删除 | ✅ 已删 |
| `components/fnos_monitor/kk_ui/`（6 个文件） | 全部页面迁完后删除 | ✅ 已删（`grep -rIl kk_` 全树为 0，`tools/verify_all.sh` L2.5 守着） |
| `tools/preview/style_proof.c` + CMake target + `run.sh` 里那一段 + `gen_fonts.sh` 里的扫描项 | 删除（kk_ui 风格试片） | ✅ 已删 |
| `docs/ui-kk*.md`、`ui-unifi*.md`、`ui-redesign-v7/8/9.md`、`ui-audit-v3.md` | 移到 `docs/archive/ui/`（历史资料，不再作为实现依据） | ✅ 已移 |
| `tools/mockup-v11/`（HTML/CSS 原型 + 18 张渲染图 + 测量脚本） | 移到 `docs/archive/ui/mockup-v11/`（只作"当初为什么定这些数字"的可追溯性，不再构建/测量/交付） | ✅ 已移（`git mv`，全仓引用同步改到新路径） |
| `uk_page_load()` / `uk_anim_int()`（备而不用的动画 API） | 删除（`lv_screen_load_anim` 在本板没有第二个 screen 可 load；数值补间没有调用点） | ✅ 已删 |
| README.md 里 brookesia 示例的踩坑说明 | **保留**（那是官方示例的硬件教训，不是本项目的 UI 方案） | ✅ 不动 |

## 7. 证据分级（不许越级）

- L1 主机预览 PNG（`tools/preview/out/`）：只能证明"版面/字库/越界"。
- L2 构建通过 + 栈深检查：只能证明"能编译、栈够"。
- L3 实机 `capture-board` 照片：唯一能证明"这块屏上真的长这样"。
- L4 长跑（≥30min，看内存/重启）：唯一能证明"稳定"。

**当前状态（2026-10-07 Round 2）**：五页 + 诊断/配对/配网**全部**迁到 ui_kit（kk_ui 已删），并逐页做到 L3——
主机预览 57 张全绿后烧录，用 `python3 tools/page_shot.py --page N` 串口切页 + `capture-board` 拍照，
p0/p1/p3/p4 四页都在真实数据下拍屏确认（p2 网络页未单独拍，逻辑与其余页同源）。
主机预览另加了一条断言，专盯"配网卡不该自己冒出来"这个实机回归（详见 §10 Round 2）。

## 10. 进度记录（2026-10-07，Round 1）

**已落地**：`ui_kit`（`uk_theme.h` / `uk_theme.c` / `uk.h` / `uk.c`）—— LVGL 原生 flex 的自适应层；**温度页（p4）已迁到 ui_kit**，是"自适应池"的第一个真机样板。

**四级证据**（按本文档 §9 的分级）

| 级别 | 证据 | 结果 |
|---|---|---|
| L1 主机预览 | `tools/preview/build/preview /tmp/pv6` → 57 张 PNG | 无字形缺口、无"子对象跑出父对象 / 可点击互相压住"；死构件 103（基线 175） |
| L1 池解 | `[pool] W=898 H=353 n=24 row_h=44 max_cols=3 cols=3 per_col=8 shown=24` | 与 HTML 原型 `866x353 cols=3 need=352` 一致：1024×600 一屏 3 列 × 8 行装满 24 路 |
| L2 构建+栈深 | `./idf.sh build` / `tools/stack_check.py` | `fnos_monitor.bin 0x1b1d90`（81% free）；lvgl_worker 6832/12288、poll_task 2480/6144、pair_task 3072/7168 |
| L3 实机 | 烧录后串口 45s + 手机拍屏 | `boot complete`、`poll ok=30 fail=0`、堆 `internal=207KB psram=27141KB` 稳定、**0 条崩溃/复位标记**；照片 本地实机验收截图（不随源码分发） 可见 24 路 3 列 × 8 行、设备名不缩写、阈值色正确 |

**复现命令**：`tools/preview/build/preview <dir>` → `python3 tools/preview/ppm2png.py <dir>`；`source <idf-activation-script> && python3 tools/stack_check.py`；`./idf.sh build`；`./idf.sh -p /dev/cu.usbmodemBOARD flash`。

**未完成**：p0 总览 / p1 存储 / p2 网络 / p3 系统 / 诊断 / 配对 / Wi-Fi 六处仍走 kk_ui；外壳（rail/head/foot）与动画（`uk_page_load`、`uk_anim_*` 已备但未接入）；`kk_ui/` 待全部迁完后删除。

## 11. 进度记录（2026-10-07，Round 2：动画接线 + 实机逐页取证 + 配网卡缺陷）

**动画（"保证动画效果"）落地**：换页入场 = 逐个子对象 `uk_anim_fade(c, LV_OPA_20, 160, ±20ms 错峰)`；
严重告警 = 底栏状态点 `uk_anim_pulse()` 700ms 呼吸（**只在级别翻转时**调用，否则每秒被 refresh 重启动、
看起来像卡顿）；值条 = `uk_bar_set()` 原生 180ms。`uk_page_load()`/`uk_anim_int()` 备而不用，已删。

**自适应证据（主机）**：`UK_POOL_TRACE=1` 实测 24 路温度页 `cols=3 shown=24`、12 卷 `cols=2 shown=12`、
10 块磁盘 `cols=3 shown=10`、限量态 16 容器 `cols=1`（滚动而非静默截断）⇒ 列数随池宽/条数自适应。

**实机证据（L3）**：`./idf.sh build` → `./idf.sh -p /dev/tty.usbmodemBOARD -b 230400 flash`
（1 774 144 B，45.2 s，hash verified）→ 串口 25 s 无 assert/panic（heap internal=215KB psram=27097KB）→
`python3 tools/page_shot.py --page N` 逐页拍照并**亲眼看图**：
p0 总览（CPU 5% / 内存 50% / 最高温 74℃ / 存储 14.4 of 26.7 TB / 6 卷 54% / 阵列 3/3 / ok 96 fail 0）、
p1 存储（6 卷两列 + 阵列 3 行 + 磁盘 5 项两列）、
p4 温度（24 路三列全显，MAC 74.0℃ 琥珀，ok 241 fail 1）、
p3 系统（三列，容器空态 / 硬件温度 / 暂无告警事件）。

**实机缺陷与修复（本轮最有价值的一条）**：p3 上"接入 Wi-Fi"卡自行弹出并盖住系统页，而 `wifi show`
回显"凭据：有 / 已连接"。第一次以为是开机规则的竞态（判据 `fnos_net_configured()` 要等网络层读完 NVS），
改成问存储本身（`fnos_wifi_store_load()`）后**串口证明开机规则确实没触发，卡却还在** ⇒ 真根因是
`wifi_build()` 建出来的卡**默认可见**（没加 HIDDEN），而"有凭据"时没人调过 `wifi_show()`，
`set_page()` 里那句 `if (s_wifi_open) wifi_show(false)` 也就收不掉它。修复：建完即 HIDDEN。
主机预览同步加断言（有凭据 + p3 ⇒ 卡上的"重新扫描/手动输入"不可见；点顶栏入口才出现），
复跑后**只有 6 张 p3 相关样张变化**，其余 51 张逐字节不变。修完重新烧录拍屏：p3 恢复正常三列、卡不再出现。

**教训**：主机预览的 fixture 全是"没凭据"，那条路径会先开卡再被 `set_page` 收掉，正好绕开
"有凭据且从未开卡"这个真实状态 —— **fixture 覆盖不到的路径，只有实机能证伪**。

**未完成**：p2 网络页未单独拍屏；长跑（L4，≥30 min 看内存/重启）未做；本轮改动**未提交**（AGENTS.md §12
要求提交/推送需用户当下授权）。

## 12. 进度记录（2026-10-07，Round 3：温度清单按设备分组）

用户反馈："温度显示全部错位 间距过近而且传感器存在多个重复 系统与温度界面"。详见 `docs/verification.md` §24。

- **对象拓扑优先于数值**：温度清单原来是"一路一行"，设备名在每行重复印 —— 同型号多块 NVMe 的 `dn`
  完全相同，屏幕上看就是一片重复。现在**一台设备一个块**（p4）/ **一台设备一行**（p3 摘要），
  设备名只出现一次；`dn` 同名时把唯一 id `dev`（`nvme0n1`…）附上，消歧用的那截**不许被省略号吃掉**
  （p4 允许块头折两行；p3 把 dev 放到第二行 `最热 … · N 路 · nvme2n1`）。
- **间距**：块内通道行距 4px、块底 12px + 1px 线；通道行行高由字体决定（num_20 → 24px），
  不复用带 `UK_ROW_MIN=40` 的 `uk_row`。
- **池引擎两处配套**（`ui_kit/uk.c`）：① 列分配"保持列主序 + 按剩余高度容量换列"（等高条目逐像素不变，
  块高悬殊时不再切出装不下的列）；② 列容器预留滚动条条带 `pad_right = UK_SCROLL_W + 2`
  （原来 `°C` 被竖条从中间劈开，用户看到的"错位"之一）。
- **fixture 补真机条件**：预览温度桩改成与真机相同的重复型号（`PCIe-8-SSD 512GB` ×2、
  `ZHITAI TiPlus7100 1TB` ×2）。原来 8 个设备名全唯一、恰好绕开"同型号多块盘"。
- **L3 实机**：刷录后 p3/p4 拍屏确认（本地实机验收截图（不随源码分发））。
