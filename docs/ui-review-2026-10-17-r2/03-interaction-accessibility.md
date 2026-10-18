# 03 · 交互 / 动效 / 状态 / 可访问性审查（835de2c，1024×600）

审查对象：worktree `/Users/llll/code/esp/p4-7b-fnos-monitor-835de2c`（commit `835de2c`）。
新帧：`tools/preview/out/ui-review-r2-1024/`（112 张 + `refresh-work/` 3 张）。
日志：`docs/ui-review-2026-10-17-r2/preview-run.log`。
设计依据：`docs/ui-design-language-2026-10-15.md`、`docs/ui-motion-cadence-2026-10-15.md`、`docs/adr/0002-continuous-telemetry-motion.md`。

方法：只读源码；只运行已构建的 `tools/preview/build/preview`（未重编译，输出到 `/tmp`）；帧用 Pillow 逐像素量；静态帧 + 既有 `PREVIEW_*` 探针交叉验证。

## 0. 结论与计数

| 级别 | 数量 | 摘要 |
|---|---|---|
| P0 | 0 | 无阻断/数据破坏级问题 |
| P1 | 1 | 覆盖层未显式关闭时被壳层导航静默关闭，**Wi-Fi 已输入口令被无条件清空** |
| P2 | 4 | 首采/离线温度卡文案与实际值矛盾；LINK 未决文案指向不存在的按钮；首页零卷空卡；首页 43px 溢出 + 嵌套滚动 |
| P3 | 8 | 详情外点不关、覆盖层选中态不一致、焦点环 1px、reduced motion 仅编译期、48 路收起可发现性、20px 页脚提示、趋势空态重复文案、符号键盘双“返回字母”键 |

一句话：新壳层的“覆盖层在页内、正文互斥”实现是成立的，触控/动效/对比度主体达标；问题集中在**覆盖层关闭语义**（用户没点“关闭”却丢输入）和**状态文案与真实数据不一致**。任务里“10-wifi-pass 底部按钮 25–26px”的判断不成立（实测 44px，见 §5.1），不报；“48 路无收起入口”也不成立（入口在块底 12px 行，见 P3-5）。

## 1. 壳层与覆盖层交互矩阵

| 覆盖层 | 点当前底栏页 | 点其他底栏页 | 顶栏 Wi-Fi / 配对 / 设置 | 点详情外暗层 | 状态是否丢 |
|---|---|---|---|---|---|
| 实体详情 | 被遮罩挡住，不关闭（探针 PASS） | 被挡住 | 被挡住（遮罩全屏） | **无响应，不关闭** | 不丢；关闭后原页/原滚动保留 |
| Wi-Fi 配网 | **关闭卡 + 清口令** | **切页 + 清口令** | 关闭/切换 + 清口令 | — | **丢已输入口令**（P1） |
| 配对 | 关闭配对卡 | 切页 | 设置关配对；配对按钮不 toggle | — | 配对码保留（代码核对） |
| 设置/诊断 | 关闭诊断 | 切页 | Wi-Fi/配对会先关诊断 | — | 诊断滚动位置保留；无输入 |

关键源码顺序：`components/fnos_monitor/fnos_ui.c:5710-5727` 在 `if (idx == s_page)`（:5723）**之前**执行 `pair_show(false)`（:5715）和 `if (s_wifi_open) wifi_show(false)`（:5716）；`wifi_show(false)` 第一行就是 `wifi_clear_password()`（:2617-2619），清空 `s_wifi_pw`、`s_wifi_pw_n`、reveal，并把标签换回静态“口令/显示”（:2385-2394）。`wifi_set_stage(WIFI_ST_SCAN)` 也会清口令（:2463-2491）。所以“新设计仍有无条件 `wifi_clear_password` / `pair_show(false)`”这一条：**`pair_show(false)` 仍在，但配对码不丢；`wifi_clear_password` 仍在且确实丢输入**。

## 2. P1（1 条）

### P1-1 任何壳层切换都会静默关闭配网卡并清掉已输入口令

- **代码**：`components/fnos_monitor/fnos_ui.c:5710-5727`（`fnos_ui_set_page`）→ `:5716` `wifi_show(false)` → `:2617-2619` 无条件 `wifi_clear_password()` → `:2385-2394`。同类入口：`:2640-2651`（顶栏 Wi-Fi 按钮在卡开时再点=关闭）、`:640-651`（设置打开时先 `wifi_show(false)`）、`:2256-2264`（配对按钮先 `fnos_ui_set_page(3)`）。
- **帧**：`10-wifi-retry.png`（口令框 12 个掩码字符 + 底栏“系统”蓝线仍亮）、`10-wifi-input-limits.png`（63 字符口令）；此时点底栏任意项、点当前“系统”、或顶栏“设置/配对”，卡都会消失。没有一帧能证明“保留输入”，因为代码路径就是清空。
- **复现路径**：
  1. 顶栏“Wi-Fi 未配置”打开配网页，选一个加密 AP（如 Demo），进入 2/3“连接凭据”；
  2. 在口令框输入 ≥8 位（帧 `10-wifi-retry.png`）；
  3. 点底栏“系统”（当前页）或任意其他页，或顶栏“设置/配对”；
  4. 卡关闭且无提示；再打开配网，回到 1/3 扫描页，口令已空。
- **用户影响**：口令是设备上最难输入的内容（63 字符上限、屏幕键盘）。用户点导航只是“确认当前页/看看状态”，却丢掉整段输入，且没有任何“放弃输入？”提示。顶栏 Wi-Fi 文案仍是“Wi-Fi 未配置”，看起来像状态而不是“关闭”，进一步诱导误触。安全清理（审计 M19）的意图正确，但清理时机绑到了“任何页面切换”，不是“显式关闭”。
- **修复方向**（任选其一，优先 A）：
  - A. 覆盖层打开时把导航当作模态：`idx == s_page` 直接返回且**不关覆盖层**；要离开页面必须点显式“关闭/返回”，离开前若有未提交口令则二次确认。这样既不需要在卡关闭后保留口令，也不会误丢。
  - B. 若坚持“导航可离开”，把 `wifi_show(false)` 的清理拆成 `wifi_suspend()`（只隐藏，保留掩码草稿）与 `wifi_close_explicit()`（清空）；草稿只在本次开机内存中、关闭/成功/超时清掉。
  - C. 顶栏按钮在卡开时改成“关闭配网”文案（或加下拉指示），并把“点当前页”从关闭动作中移除。
  - 备注：`pair_show(false)` 不要跟着改；配对码目前是保留的，见 §5.5。

## 3. P2（4 条）

### P2-1 首采前与离线时温度卡写“显示上次快照”，却把值强制成 `--`

- **代码**：`components/fnos_monitor/fnos_ui.c:4375-4385`：`if (!st->online) { overview_note = why ? why : "离线，显示最后一次采集快照"; home_temp_value = "--"; home_temp_unit = ""; home_temp_name = "离线 · 显示上次快照"; ... }`。该分支只看 `!st->online`，**没有看 `st->ever_ok`**。同文件别处已有正确分支：`:4318`（`!ever_ok ? "等待采集" : ...`）、`:3939`（`!ever_ok → 等待采集`）、`:4950`（“尚无有效快照”）。
- **帧**：`00-startup.png`（顶栏“等待采集端”、chip“等待数据”，温度卡却是“离线 · 显示上次快照”+`--`）；`03-offline-p0.png`（顶栏“保留旧数据 · 1 分前”，同样 `--`+“显示上次快照”）；`11-connection-reason.png`（同）。
- **用户影响**：两种状态都在骗人：首次开机根本没有“上次快照”；真离线时声称显示上次快照，但温度值是空的、传感器名也不显示。用户无法区分“传感器坏了”“没采到”“数据太旧”。这也是页级状态机里唯一一处与 header/chip 明确矛盾的地方。
- **修复方向**：按 `ever_ok` 拆三态：`!ever_ok` → “等待采集/等待传感器读数”+`--`；`ever_ok && !online` → 如果保留上次温度就显示数值并打“旧值/年龄”，否则文案改成“离线 · 无实时读数”；`online` 走现有 `temperature_summary`。并给首次开机/离线各补一条样张断言。

### P2-2 LINK 未决时文案让用户点“修改密码”，而该按钮此时不存在

- **代码**：`fnos_ui.c:2771-2791`：`show(s_ui.wifi_done, on || bad)`（LINK 未决时 `on=bad=false`，按钮隐藏），但副行仍拼 `"%s\n请按原因排查后重试；要更换口令，可点「修改密码」。"`（:2789）。只有 ONLINE/FAILED 才显示“完成/修改密码”。
- **帧**：`10-wifi-linking.png` 与 `10-wifi-submitting.png`（同帧，sha256 `92b071…`）：“正在连接… / 正在按新凭据连接 Demo… / 请按原因排查后重试；要更换口令，可点「修改密码」。”，而 footer 只有“换一个网络/关闭”。
- **用户影响**：正常连接过程（可能持续数秒）被写成失败后的补救指引；用户会去找一个不存在的按钮。与同屏的“正在连接”语气冲突，也会让人以为已经失败。
- **修复方向**：按 `on/bad/pending` 三段拼文案：pending 只留“正在连接…（口令或加密方式不合适会在这里失败）”；bad 才追加原因与“修改密码”；on 走成功文案。给 LINK 未决单独加断言（现有帧已能覆盖）。

### P2-3 首页“卷占用”在 0 个卷时是一张空白卡

- **代码**：`fnos_ui.c:4149-4182`：卷行按 `count` 建/删，`count==0` 时只删池项；`storage_note` 的显示条件 `show(...,!storage_live || !overview_live(raid) || healthy< nraid)` 在该 fixture（存储 live、阵列 0/0 正常）为 false，且卷卡本身没有 `empty_box`。存储页有正确空态：`vol_empty="未采集到存储卷"`、`:4669-4671` 二选一显示。
- **帧**：`10-overview-empty-storage.png`（fixture `nvols=0`，`tools/preview/preview.c:2231-2234`）右下卡只有标题“卷占用”，正文全空；测试只断言了存储卡“没有可用容量数据”。
- **用户影响**：卡看起来像渲染失败；用户不知道是“没有卷”“采集失败”还是“被过滤掉了”。与存储页的空态处理不一致。
- **修复方向**：给卷卡加隐藏空态标签（“未采集到存储卷”/“0 个卷”），`overview_volume_made==0` 时显示；补 `10-overview-empty-storage` 的可见文本断言。

### P2-4 首页内容溢出 43px、卷卡内层还可滚 98px：嵌套滚动 + 无滚动条提示

- **代码/几何**：`fnos_ui.c:453-471` 页面可竖滚但 `LV_SCROLLBAR_MODE_OFF`；`PREVIEW_PROBE` 页 0 树：page root `scroll_bottom=43`，`卷占用` body `scroll_bottom=98`。即页需要再滚 43px 才能完整露出卡片底，卷卡内部又能独立滚 98px。
- **帧**：`02-live-p0.png`、`05-limits-p0.png` 只露 `/vol1–/vol3`，卡底被页边界切掉；没有滚动条或“还有更多”的提示。
- **探针**：README 的 `PREVIEW_MOTION=0,1,0 PREVIEW_MOTION_SCROLL=1` 在该构建上 **exit 134**：`Assertion failed: (lv_obj_get_scroll_y(viewport) > previous_y), function record_motion, file preview.c, line 3958`（垂直拖动 51 帧后 viewport 未滚动）；同样命令换页 1/3（`1,2,1`、`3,4,3`）均 exit 0、140 帧 PASS。`motion_vertical_viewport` 在 `preview.c:3564-3576` 取深度优先第一个可滚对象；页 0 上它选中的对象没有动（可能选到了卷卡 body 而非 page root），**根因尚未隔离**；`tools/preview/run.sh` 并不执行这条验收，所以默认流水线看不到。
- **用户影响**：在触摸屏上，手指落在卷卡上只会滚内层列表，外层那 43px 永远到不了；最后几行卷数据处于“滚不到/看不到”的边缘。没有滚动条，用户也不知道还能滚。
- **修复方向**：让页 0 在 1024×600 内自然放下（收卡片最小高/减行数），或让卷卡 body 不滚、由 page 统一滚（去掉嵌套）；同时给页 0 一个“可滚”的视觉提示。修复后把 `PREVIEW_MOTION_SCROLL` 失败纳入 `run.sh`，并确认 `motion_vertical_viewport` 选中的是 page root。若最终证明是探针选层错误，本条可降为 P3，但“卡底被切 + 无提示”的帧证据仍成立。

## 4. P3（8 条）

### P3-1 详情遮罩：点暗层不关闭，也没有返回手势/硬件键

- **代码**：`fnos_ui.c:1286-1323` 的 mask 只设背景/布局，**没有任何点击回调**；`entity_close_cb`（:1253-1257）仅由底部“返回列表”触发。遮罩全屏且挡住底栏/顶栏（`preview.c:2357-2450` 实测点“网络”页面不变、详情仍在）。
- **帧**：`13-volume-detail.png`、`13-long-detail.png`：左半屏是可点的暗层，实际无响应。
- **影响**：用户按常见“点外部关闭”习惯会点 2–3 次；唯一出口是底部按钮（好在固定可见）。属于设计选择，但在 1024×600 上暗层占了近一半屏，容易误判卡死。
- **方向**：加“点暗层关闭（按住拖动不关闭）”或加一行“点空白处或返回列表关闭”；同时补 `preview.c` 的外点用例。

### P3-2 覆盖层“当前在看它”的反馈不一致；配对按钮的选中底与顶栏同色

- **代码**：`fnos_ui.c:807` `on = (i == s_page && !s_pair_open)`；配对打开时导航全灭，但 Wi-Fi/诊断打开时仍保持“系统”选中。`:834` 配对按钮选中底用 `UK_SURF_S2`，而顶栏表面在 `ui_kit/uk.c:98` 也用 `UK_SURF_S2`（`UK_FLAT_ROWS`）——像素采样 `07-pair-code.png` 的配对按钮区与顶栏同为 `#181818`，**看不出“选中”**。
- **帧**：`07-pair-code.png`（底栏“系统”灰、无蓝条）对比 `06-diagnostics.png`、`10-wifi-pass.png`（底栏“系统”蓝字+蓝条）；`06-diagnostics` 与 `10-wifi-pass` 的底栏区域字节级相同。
- **影响**：三种覆盖层给了两套“当前状态”语义；用户会以为 Wi-Fi/诊断打开时系统页仍在正常显示，而配对打开时又像“离开了系统页”。配对入口的“你现在在看它”回执实际不可见。
- **方向**：统一策略（要么三种都清导航选中，要么三种都保留并给覆盖层标题/关闭按钮更强的层级）；配对按钮选中态改用与顶栏不同的色/描边/文字色，不要与顶栏同色。

### P3-3 输入焦点只有 1px 边框变色

- **代码**：`fnos_ui.c:2457-2461` `field_focus` 只改 `border_color`（`UK_BLUE` vs `UK_LINE`）与透明度，不加宽、不外扩、不改标签。
- **帧**：`10-wifi-pass.png`、`10-wifi-input-limits.png`：口令框仅 1px 蓝边；SSID 框默认边色。
- **影响**：1024×600 面板上 1px 蓝边与深底对比虽够，但两行输入框并排时“正在编辑哪一行”不够显眼；触屏没有键盘焦点概念，用户容易把字打进错误字段（尤其手动输入模式两行都可编辑）。
- **方向**：焦点改为 2px 蓝边或 1px 蓝边 + 左/下强调线；标签同步变亮；保持 44px 盒不变。

### P3-4 “减少动效”只有编译期 Kconfig，且覆盖不完整

- **代码**：`components/fnos_monitor/Kconfig:122-127` `FNOS_UI_REDUCED_MOTION` default n，help 描述“数字立即更新、保留跟手导航、松手直接落位”；`ui_kit/uk_theme.h:144-147` 编译期 `UK_NUMBER_REDUCED`；`fnos_ui.c:5475-5481` `motion_reduced()` 只在编译时返回；`:825` reduced 下导航图标仍走 `UK_NAV_FEEDBACK_MS=120ms` 的淡入（只是目标透明度不同）。全仓无运行期“减少动效”设置项。
- **证据**：`grep CONFIG_FNOS_UI_REDUCED_MOTION` 仅 Kconfig/uk_theme/fnos_ui 三处；`sdkconfig.defaults` 未开启。
- **影响**：对动效敏感的用户无法在设备上关；固件发布者必须预编译两版。导航 120ms 淡入在 reduced 下仍存在，覆盖不彻底。
- **方向**：若产品只做单固件，至少在设置页给一个运行时开关（可复用同一批 `uk_number_motion_enable`/`motion_reduced` 的运行时变量）；否则在文档里写明“编译期选项 + 哪些动画仍保留”。把 `:825` 的 `uk_anim_fade` 在 reduced 下直接落目标。

### P3-5 48 路展开后“收起”入口在块底、已滚出视口，可发现性弱

- **代码**：`fnos_ui.c:3553-3640` 设备块整体绑 `temp_toggle_cb`，`b->more=uk_label(...,UK_FONT_CJK_12,UK_T3,"")` 是块底 12px 行、初始隐藏；`:3679` 写入 `"%s %d 路 · ..."`，`collapsed ? "展开" : "收起"`；`:3519-3525` 点击整块 toggle。
- **帧**：`12-cpu-max-channels-expanded.png` 是 `tools/preview/preview.c:2725-2804` 里 `lv_obj_scroll_to_view_recursive(last)` 定位到 Core 47 的截图（不是摘要卡被压缩），所以卡头“Demo Processor”和“收起”行都不在视口；`12-cpu-max-channels-collapsed.png` 能看到“展开 48 路 · 40.1–44.8°C”。
- **影响**：展开后用户只看到 48 行数据，看不到“收起”；要收起得滚回块底或点块内任意位置（后者没有提示）。不是“没有入口”，是“入口不可见”。
- **方向**：把“收起/展开”做成卡头右侧的常驻小按钮（44 高），或展开时在视口底部吸附一条“收起 48 路”；保留整块点击作为补充。

### P3-6 仅内存不足时出现的页脚提示是 20px 高的可点目标

- **代码**：`fnos_ui.c:984-1010` 页脚 band 高 `CJK12 行高 + UK_S1`（≈20px），但 `LV_OBJ_FLAG_CLICKABLE` 且绑 `foot_band_cb`（:621 → 告警页）；`inventory_notice()`（:4198-4204）只在 `uk_alloc_failed() || 缺 KPI` 时显示，文案“界面内存不足 · 部分设备尚未显示”。
- **证据**：日志 `hardware inventory allocation failure and retry: PASS`；无该状态的样张（正常帧里 band 隐藏）。
- **影响**：内存不足是紧急状态，提示条却只有 20px 高且可点；用户很难点中，也不清楚点了会去哪。正常使用不出现，所以定 P3。
- **方向**：该提示要么提升到 44px 并明确“查看详情”，要么改成非交互的红色条 + 在告警页给入口。

### P3-7 空历史趋势卡把“等待历史采样”显示两遍

- **代码**：`fnos_ui.c:1189` `r->context` 初值“等待历史采样”；`:4057-4058` 图表无样本时 `context` 也设“等待历史采样”；`:1095` 横轴标签初值同为“等待历史采样”；`:4972` 另有 span 文案。两个独立标签在同一空态下取到同一字符串。
- **帧**：`08-night-p0.png`（放大后可见“等待历史采样”两行，右下还有 `--`）；`00-startup.png` 因 `readable=false` 第一行是“无有效读数”，所以只重复了第二行。
- **影响**：看起来像渲染重复/布局错误，浪费一行高度；用户会怀疑界面坏了。
- **方向**：空态只保留一处（例如 context 用“暂无历史”，横轴保留“等待历史采样”），或空态直接隐藏横轴标签。

### P3-8 符号键盘有两个“回字母”键：`abc` 与 `ABC`，行为完全相同

- **代码**：`fnos_ui.c:2285-2315`：字母层第 5 行 `"123"`，符号层第 4 行 `"abc"`、第 5 行 `"ABC"`，三个都是 `WK_LAYER`；`:2849-2856` `WK_LAYER` 只做 `s_wifi_layer = !s_wifi_layer; s_wifi_shift = false;`，没有大小写区分。
- **帧**：`10-wifi-symbols.png`：同屏同时出现 `abc`（第 4 行）和 `ABC`（第 5 行）。
- **影响**：用户会以为一个回小写、一个回大写（或一个切字母、一个切大写锁定），实际都回小写字母层；多一个无意义按键，也增加误触。
- **方向**：只留一个“ABC/abc”键；若要大小写，明确让“ABC”进入大写（shift=true）。

## 5. 已核对正确（含测量）

### 5.1 触控目标：任务里的 25–26px 不成立，实测 44

| 控件 | 实测/代码 | 结论 |
|---|---|---|
| 底栏 6 项 | 高 48px（y=548..595），宽 165；`UK_NAV_ITEM_H=64` 只用于侧栏 | ≥44 ✔ |
| 过滤 chip（全部/卷/阵列/磁盘、全部/容器/硬件、全部/严重/警告/提示） | 高 44px（`13-storage-filter` 磁盘 y=64..107） | ≥44 ✔ |
| 存储/容器/温度行 | `UK_ROW_H=44`，存储行实测节距 ≈52px | ≥44 ✔ |
| 展开/收起 | 整块设备块可点（远大于 44） | ✔（可发现性见 P3-5） |
| 底栏“关闭/返回/连接”等 | **44px**（`10-wifi-pass` 返回 y=463..506；关闭/连接同高） | ✔ 任务提示的 25–26px 是 16px 字+深色底造成的错觉 |
| PIN 键盘 1..9/删除/0/确认 | 键高 48px（行 y=243..290 等） | ≥44 ✔ |
| Wi-Fi 键盘 5 排 | 键高 47px（y=192..238） | ≥44 ✔ |
| 详情“返回列表” | 44px（y=539..582） | ✔ |
| 顶栏 Wi-Fi/配对/设置 | `flex_btn` 高 `UK_ROW_H=44`；顶栏高 56 | ✔ |
| 输入框/“显示” | 高 44；`min_width 44` | ✔ |
| 告警行 | 约 32px，但是 `uk_label`，**不可点**（`fnos_ui.c:4917-4943`） | 非触控目标，不适用 44 |
| 页脚提示 band | 20px、可点，仅内存不足时出现 | 见 P3-6 |

### 5.2 切页动效：临界阻尼、反向/重入/首次入页均 PASS

`motion_exec`（`fnos_ui.c:5590-5614`）用 65 项 Q16 表实现 `(d + b·t)·e^(-k·t)`，`motion_snap`（:5645-5681）把时长夹在 80..160ms、按剩余距离缩放；`UK_MOTION_DAMPING=10.0f`（临界阻尼）、`UK_MOTION_MS=160`、`UK_MOTION_MIN_MS=80`（`ui_kit/uk_theme.h:107-109`）。反向时交换 from/to 并把当前速度带入新目标；快速甩动超调被夹到阻尼界限。探针：
- `PREVIEW_MOTION=0,1,0 PREVIEW_MOTION_INPUT=api PREVIEW_MOTION_REPEAT_TARGET=1` → exit 0，96 帧，`motion native input, stable terminal pixels, rapid round-trip, frozen history PASS`；
- `PREVIEW_MOTION=0,3,0 PREVIEW_MOTION_FIRST_ENTRY=1` → exit 0，71 帧，PASS。

### 5.3 数字/仪表/趋势：180ms 临界阻尼 + 600ms 节拍

`ui_kit/uk_spring.h` 的 `uk_spring_response` 是 `(1+w·t)e^(-w·t)`（`w=UK_TELEMETRY_DAMPING/duration`）；`sdkconfig.defaults:118-119` 为 `NUMBER_MS=180`、`TELEMETRY_DAMPING=8`（自然频率 ≈44.44 rad/s）。`uk_meter.c:80-120`：首读直接落值、同目标不重启、中途以当前位置+速度重定向、反向惯性夹在量程内、2×180ms 收尾、分配失败直接写值。`uk_number.c:218-336`：同单位/同结构/可见/有旧值才滚，首读/单位变化/离线直接落值，600ms `presentation_gate`，预算失败回退原生 label。日志：`15 ms streaming cadence: 59 paints / 990 ms PASS`；`refresh work page=0..5 ticks=4 ..._style_changes=0`。

### 5.4 过滤栏：选中态、跨页保留、空结果都成立

`scope_cb`（`fnos_ui.c:1198-1206`）写 `s_scope[page]` 后同步并滚回页顶；`scope_sync`（:3840-3862）给选中项 1px 蓝框（`UK_FOCUS_RING_OPA=180`）+ `UK_NAV_SEL` 底 + 蓝色文字；`preview.c:2357-2450` 实测：页 1 选“磁盘”→去页 0→回页 1 仍是“磁盘”，点“全部”恢复；告警页“严重”过滤后空态显示“此等级暂无事件 · 选择全部查看”（`13-alert-filter-empty.png`），总数为 0 时显示“暂无告警事件”（`06-alerts-health.png`）。存储页空态 `未采集到存储卷/阵列/磁盘活动`（`fnos_ui.c:1536-1550`、:4669-4671）也在。唯一例外是首页卷卡，见 P2-3。

### 5.5 详情与配对：阻挡、滚动、冻结、状态都正确

- 详情遮罩（`fnos_ui.c:1286-1323`）全屏盖住底栏/顶栏；`preview.c:2381-2396` 点“网络”后 `fnos_ui_page()` 不变且详情仍在；`返回列表` 44px 固定卡底，长名称时 body 竖滚（`13-long-detail.png`），关闭后原页/原滚动保留；容器改名后详情仍显示打开时名字（`13-container-detail`，测试 `renamed-container` 不出现）。
- 配对：`pair_show(false)`（:2237-2254）只隐藏卡，不清 `s_pair_code/s_pair_len`；只有 `pair_cancel_cb`（:3266-3278）和成功 done 分支（:3250-3251）清码。因此 **Lead 复核里“配对码同理丢输入”不成立**；导航切走再打开配对，码还在。配对打开时 `theme_apply` 的 `!s_pair_open` 让 6 个导航项全灭（`07-pair-code.png` 放大确认无蓝条），这是三种覆盖层里唯一这样做的。
- 配对各态：`07-pair-busy`（“正在从 NAS 取证书…”，关闭常驻）、`07-pair-failed`（“配对成功但写入 NVS 失败”，重试码/失败原因同屏）、`07-pair-confirm`（4 行 24px 指纹 + 取消/接受）、`07-pair-done`（已配对/指纹/关闭/解除配对）文案完整；日志 `21 pairing state/message cases ... PASS`、`pairing panel ... PASS`。

### 5.6 配网状态机：LINK 关闭不断连，长输入/长 SSID 都处理了

- `fnos_net_set_credentials`（`components/fnos_monitor/fnos_net.c:422-442`）在临界区把 SSID/口令拷进 `s_pend_*` 并置 PENDING，**UI 清口令不会破坏进行中的连接**；LINK 阶段关闭卡后顶栏继续“连接中/未连接/已连接”。
- 状态：SCAN/PASS/LINK/FAILED；`关闭` 常驻（`wifi_set_stage` :2463-2491）；`返回` 回扫描；失败回 PASS 重输（`wifi_result_cb` :2910-2918）；校验文案（空 SSID、<8 位）明确；`显示/隐藏` 切换 44px；“密码不对（认证失败）”“凭据保存失败（NVS 写入错误）”等失败原因都上屏。
- 长输入：`WIFI_SSID_MAX=32`、`WIFI_PW_MAX=63` 用 `LV_LABEL_LONG_MODE_SCROLL`（`10-wifi-input-limits.png`）；长 SSID 在顶栏 `ABCDEFG…` / `连接中 · …` 用 `LV_LABEL_LONG_MODE_DOTS`（`10-wifi-long-ssid*.png`，`preview.c:3255-3270` 的 `assert_ellipsized_source`）。

### 5.7 离线/旧数据锁定

`03-offline-p0.png`、`11-connection-reason.png`：header“保留旧数据 · 1 分前/刚刚”、chip“离线”、CPU/内存“旧值”、趋势“旧历史 · 180 次采集”、存储“离线 · 旧值”；`refresh_page`（`fnos_ui.c:4995-5004`）在刷新期间 `uk_number_motion_enable(false)`，离线后 `s_st.online=false` 数字不再滚动；`motion_finish` 也按 `s_st.online` 收口。旧值不会伪装成实时值。唯一文案矛盾是温度卡（P2-1）。

### 5.8 对比度/可访问性：全部 >4.5，但最低是 6.07 不是 6.16

`preview.c:2898-2965` 的审计只取可见 label（opa>20），逐层合成有效背景，低于 `CONTRAST_MIN 4.5` 直接 abort；每张 snapshot 都跑（motion 帧跳过），所以日志值是“该帧最差可见文本”。实测：
- `05-limits-p4` **6.07:1**（`85.0` #FF5A45 on #121212）；
- `05-limits-p1/p3/p5`、`13-storage-filter`、`13-alert-filter`、`13-long-detail`、`08-night-p5` 6.16:1；
- `08-night-p1` 6.49:1；`07-pair-code` 8.00:1；多数帧 8.95:1。
全部满足 WCAG AA 普通文本；12px `UK_FONT_CJK_12` 在多帧正是“最差值”但仍达标。暗色遮罩下没有可读文本（详情卡在遮罩上层，保持 8.95:1）。任务口径“最低 6.16–8.95”应修正为“最低 6.07”。

### 5.9 日志与样张完整性

112 帧 + refresh-work 3 帧；`PASS: all 7 presets; 115 rendered frames without purple`；`never visible 35（基线 175）`；`PASS full large frame/long IDs/modules/concurrent readers/replacement/shrink/malformed input/byte budget rejection`；`hardware inventory empty/compact/mixed/dense PASS`；`allocation failure and retry PASS`；`stable refresh, repeated current-page navigation, same-count long-name reflow, filtered inventory reflow PASS`。`13-*` 七帧覆盖了长名称、容器冻结、空结果；`07-pair-*` 八帧、`10-wifi-*` 十九帧覆盖了配对/配网各态。

## 6. 不确定 / 未验证

1. **实机触摸**：所有“44px/可点”均为主机预览 + 真实指针事件；没有电容屏实测，边缘 4px 间隔、指腹遮挡、按下态在面板上的表现未验证。
2. **面板视角/伽马**：6.07–8.95:1 是主机渲染 sRGB 的理论值，不是面板实测；最低值 6.07 的余量比 4.5 高 35%，但深色屏在斜视/强光下的实际可读性需实机确认。
3. **页 0 滚动**：帧证明确有 43px 溢出与嵌套滚动；`PREVIEW_MOTION_SCROLL` 的断言失败究竟是“卷卡 body 抢先”还是 `motion_vertical_viewport` 选层问题，尚未隔离。已把失败命令、断言行和页 1/3 的对照结果写进 P2-4；不建议在根因不明时直接改页面高度。
4. **48 路收起**：代码与帧证明入口存在但在块底；真实手指在 48 行网格里“点块收起”与“滑动滚动”的区分（LVGL click 阈值）未在实机验证。
5. **自动配网时机**：`ui_tick`（:5078-5105）在首个 tick 后 3.0s 触发（约开机 3.5s），只弹一次；会 `set_page(3)` 并收起详情。这是首启引导的设计取舍，未计为缺陷；若要“不打断”，需产品确认。
6. **告警行是否应可点**：当前是纯展示 32px 行；若后续产品要求点开详情，必须提升到 44px 并补命中测试。
7. **夜间模式触发路径**：`08-night-*` 由预览请求驱动，设备端自动/手动夜间的入口不在本次范围。

## 7. 建议的优先级顺序

1. P1-1：先修“导航/顶栏关闭覆盖层”的关闭语义，确保口令不因非显式操作丢失（最高性价比，改动集中在 `fnos_ui_set_page` 与 `wifi_show` 的拆分）。
2. P2-1 / P2-2：两处文案会直接误导用户（“显示上次快照”却空白、LINK 未决指向不存在按钮），改文案/分支即可。
3. P2-3：首页零卷空卡，补一个空态标签和断言。
4. P2-4：与版面审查合并处理页 0 的 43px 溢出和嵌套滚动；顺手把滚动探针纳入 `run.sh`。
5. P3：按 §4 逐条排；其中 P3-2（覆盖层选中态统一）建议与 P1 一起做，避免再次出现“看起来还在系统页”的误导。
