# 触控/渲染性能报告（2026-10-10）

目标（用户口径）：拉取 GitHub 最新代码 → 判定性能提交是否真有效 → 在其上继续改进，
**触控与性能提升 30%**。数字一律来自实机计数器；宿主只做回归门禁。
基线固件 = `a563000` + 纯仪表（零优化），基线与测法见 `docs/perf-baseline-2026-10-09.md`。

## 一、结论速览

数字来源：`final.csv`（最终树，N=20）与 `final3.csv`（同一份代码的另一次会话，N=20）对
`base.csv`（基线固件，N=20），同机同测法、同样用例顺序；更早的 `rs.csv` 是第三次复测——
见本节末"跨会话复现"。

| 面 | 指标（N=20 中位数） | 基线 | 现在（final / final3） | Δ | 30% 目标 |
|---|---|---|---|---|---|
| 稳态 S2 | `ui_tick` p50 / p95（idle / swipe） | 75.6 / 94.5 ms | 35.5 / 35.6 · 39.6 / 40.0 ms | **-53.0% / -57~-58%** | ✅ |
| 稳态 S3 | LVGL 任务 CPU（idle / tap） | 6.3 / 14.4 % | 3.8 / 7.7 %（两次同值） | **-40.5% / -46.5%** | ✅ |
| 稳态 S1 | FPS（idle / tap / swipe） | 16.0 / 17.0 / 18.4 | 19.2 / 24.1 / 22.0 · 19.0 / 23.3 / 22.9 | **+18.8% / +37.5% / +24.5%** | ✅ |
| **P2 拖动跟手** | 拖动帧间隔 p50 / p95 | 37.6 / 49.1 ms | 27.1 / 34.8 · 27.2 / 34.9 ms | **-27.6~-27.9% / -29.0~-29.2%** | ❌ 差 0.8~1.0 pt |
| **P1 按下→首帧** | tap p2f | 11.64 ms | 10.69 · 10.68 ms | **-8.1% / -8.2%** | ❌ |
| P3 切页 | 松手→首帧 r2f（tap / swipe） | 119.9 / 45.9 ms | 71.9 / 32.4 · 71.6 / 32.5 ms | **-40.1~-40.3% / -29.2~-29.4%** | ✅ |

实测口径的坑（写进判读规则，别拿它下结论）：

- `tap:3:3` 每轮只有 2~6 个帧间隔样本（`iv_n` 中位数 3），它的 iv 统计量在基线上就在 16.5 / 181 / 50 ms
  之间乱跳——**tap 的 iv 不可用**；可用的是 tap 的 p2f/r2f/tick/fps。`idle` 无帧间隔样本。
- **用例顺序会改数值**：`swipe:3:0:3` 作为一轮的第一个用例时 tick p50 = 35.7 ms，作为第三个用例时
  51.4 ms（先滑过别的页 ⇒ 当前页积了脏页，下一次刷新要把图表补齐）。比较必须保持同样的用例顺序。
- **`cpu_lvgl` 跨会话抖动 ±40%**：同一份代码三个会话的 swipe 值是 7.1 / 7.5 / 10.5 %（基线 10.6）。
  判 CPU 用 idle/tap（三个会话一致），别拿 swipe 的单次值说事。

综合：**稳态（刷新开销、CPU、帧率）全面超过 30%；拖动跟手 -27.6~-27.9% / -29.0~-29.2%（差 0.8~1.0 个百分点）；
按下→首帧只有 8.2%。** 后两项不是没调好，而是不在 UI 侧——九条独立实验（第四节）都指向同一个地板：
TRIPLE_PARTIAL 每帧固定成本（整帧 blit + 整帧 cache msync ≈ 8 ms/帧）+ 60 Hz 面板节拍 + 输入采样周期。

## 二、远端 `d84039f` 判定

远端提交：`d84039f perf: 触控与系统响应优化，手势渲染与分页刷新降耗约 50%+`
（本地与远端已分叉：本地 `main`=a563000，远端 `main`=d84039f，逐条比对而非整体 merge）。

| 机制（远端 diff 中的位置） | 声称 | 实测 | 判定 |
|---|---|---|---|
| 按页刷新：`page_on`/脏页位/`refresh_page`（fnos_ui.c） | 分页刷新降耗 50%+ | `ui_tick` p50 75.2→35.4 ms（**-52.9%**）、p95 -58~-63%、LVGL CPU -24~-42%、FPS +12~+29% | **真有效**，已移植（`41a2a9c`） |
| 手势渲染：`motion_paint` 偏移未变不重绘 / 整数弹簧表 / 轮询暂停（fnos_ui.c） | 手势渲染降耗 50%+ | 与 B1/采样周期合并实测：拖动帧间隔 **-27.5% / -29.4%**、CPU -31.7%、FPS +18.8% | **部分有效**（不到 50%） |
| 绘制缓冲 50→120 行（BSP） | 切片 12→5 | 同上（合并实测） | 部分有效 |
| 关 LVGL 日志（sdkconfig） | 去掉每控件/每刷新的 printf | 同上（合并实测） | 有效（本地默认级别下日志量极大） |
| 曲线环 `EXT_RAM_BSS_ATTR` / `pool_layout` 布局跳过 / `set_txt` 栈缓冲 / `uk_set_text` 栈缓冲 / 总览布局缓存 | 减分配与重排 | 未移植 | 未判定（见第五节） |

结论：**"分页刷新降耗 50%+"成立且可复现；"手势渲染降耗 50%+"不成立，实测约 27~30%。**
两半合起来仍是一次高质量的性能提交——刷新路径的重构是本轮最大收益来源。

## 三、本轮在本地做了什么

分支 `perf/eval-20261010`（`main` 未动）：

| 提交 | 内容 | 证据 |
|---|---|---|
| `77c534b` | 台架仪表：串口 `bench tap/swipe/idle` + `fnos_perf.c` 逐帧计数器（按下→首帧、松手→首帧、帧间隔、`ui_tick` 耗时、LVGL/IDLE CPU），默认关 | 仪表自身不改被测对象：`bench` 只在显式命令下接管指针读回调并立即还原 |
| `d3026c1` | 修两处测量滞回：卡片定高 chrome 改手算（不再用"卡高-池高"实测，会把余量固化）、池行距复位 | 84 状态与 pre-修复逐字节相同 |
| `41a2a9c` | 移植远端按页刷新（`page_on`+脏页位+`refresh_page`，`ui_tick` 只刷当前页） | `ui_tick` p50 -53%、稳态 CPU -42%（idle） |
| `fe6c552` | 基线与对比工具：`docs/perf-baseline-2026-10-09.md`、`tools/perf_table.py` | 基线 60 行 CSV 可复核 |
| `db655d4` | 手势与稳态第一批：绘制缓冲 120 行、整数弹簧（Q16 查表替 `expf`+`lroundf`）、轮询空闲暂停、采样周期 15→8 ms、关 LVGL 日志 | 拖动帧间隔 -27.5%（p50）/ -29.4%（p95）、CPU -31.7%、FPS +18.8% |
| `1fcdd29` | `refresh_soon()`：按下/拖动/切页不等 15 ms 刷新周期 | 每轮产帧数 +21~42%、FPS +27~47%、r2f -30~-40%；对 iv/p2f 无改善 |
| 本条 | 收尾：阶段分解仪表（p2rs/rs2rr/rr2rs，bench-only 默认关）、`main.cpp` 记录两条 tear-avoid 死路、本报告补负结果与跨会话复现 | `final.csv` 对 `base.csv`：tick -53.0%、r2f -40.3%/-29.4%、FPS +21~43%、iv -27.9%/-29.2%；宿主 9 PASS + 运动预算 PASS |
| 本轮（goal round 2） | 把 UI 侧最后三个候选逐个实测掉：flush 内部阶段分解仪表（`[dbg-flush]` rot/prep/msync/blit/dirty，只在 gitignored 的适配器里）、布局仪表（`[dbg-layout]` 每次 `layout_update_core` 的耗时）、translate 布局标记两探针、面板 MADCTL 硬件 180°、绘制缓冲 600 行 | 四条全负（第四节 9~12）；**没有一条改动留在树里**——`final3.csv` 复测确认 `final.csv` 的每一项（iv -27.6%/-29.0%、tick -52.9%、FPS +24.5%） |

门禁（每条提交后都跑）：宿主 84 状态渲染 + 几何/字形/溢出/重叠审计全绿；
`PREVIEW_MOTION` 响应与完成预算 PASS；固件构建 + 四条入口栈深在预算内。

## 四、关键负结果（都做了实验，别重复踩）

1. **按下→首帧不是 UI 侧延迟**。`refresh_soon()` 让"按下反馈帧"跳过 15 ms 刷新周期等待，
   并让采样周期从 15 ms 降到 8 ms，p2f 只从 11.68 ms 动到 10.77 ms（-7.8%）——
   按下到出帧这条链被"输入采样（8 ms 一档）+ 面板/DMA flush 完成"量化，UI 侧只剩 1~3 ms。
   要证伪/证实需要把 p2f 拆成"按下→RENDER_START"与"RENDER_START→RENDER_READY"两段
   （本轮已拆，见第 8 条：地板 = 输入段 2.9 ms + 8.35 ms，UI 侧只剩零点几毫秒）。
2. **拖动帧间隔的地板与绘制面积无关**。做了"只失效两页交界那条缝"的实验：拖动一帧真正变的
   只有 |Δoffset|×屏高（约 40×443 px），比整视口失效（1008×443）小 ~25 倍。宿主 PARTIAL
   模式（与设备同为 partial/triple 缓冲）下 151/151 帧与整视口失效**逐字节相同**（等价性证明），
   但设备上 iv 仍是 27.7 / 34.8 ms（对比不含该改动的 28.1 / 34.6 ms，噪声内），CPU/FPS 也没动。
   → 瓶颈在 DPI 面板扫描/缓冲握手这条流水线，不在绘制量；该改动已回退（不留无收益的复杂度）。
   要继续压只能动面板时序或 tear-avoid 缓冲策略，属硬件侧，风险不对称。
3. **`tap` 用例的帧间隔样本只有 2~6 个**，统计量不可判；本报告对 tap 只用 p2f/r2f/tick/fps。
4. **换 tear-avoid 模式两条路都堵死**（想绕开"整帧 blit + 等 VSYNC"）：
   `DOUBLE_DIRECT` 起不来——开机 `E esp_lvgl:adapter: esp_lv_adapter_lock(751): Failed to acquire LVGL lock`
   （适配器任务在首帧 flush 里等 VSYNC 通知、一直持锁）⇒ UI 从没建出来，bench 超时；
   `NONE` 不支持本板需要的 180° 旋转——`E esp_lvgl:disp: rotation not supported under TEAR_AVOID_MODE_NONE`，
   并在 `bsp_display_start_with_config`（esp32_p4_wifi6_touch_lcd_7b.c:634）断言重启。两条都写进
   `main/main.cpp` 注释，别再试。
5. **拖动快照（出页拍成画布）也是负结果**：拖动期间出页不变，理论上可以拍一次快照、每帧只平移一张
   1008×443 的 RGB565 画布（`lv_snapshot_take` + `lv_canvas`，`lv_snapshot_free` 已弃用要用
   `lv_draw_buf_destroy`）。设备实测 iv p50 28.2 ms（-25.0%）、**按下→首帧 110.6 ms（+65.7%）**——
   一次性快照 ~110 ms 正好砸在拖动第一帧上，而且 LVGL CPU 没降（10.4%，与不拍快照同值）⇒ 每帧拷
   一张画布并不比画控件树便宜。已整体回退。宿主 A/B 记录（临时开关，事后删）：151 帧里 132 帧逐字节
   相同，19 个拖动帧差 3~6 像素（最大通道差 24/255，在页面被裁边缘行）——方案能做对，但收益是负的。
6. **加采样率、抢刷新时机都没用**：手势轮询 8→4 ms 反而更差（iv -21.8% vs -25.3%、CPU 9.2 vs 7.4——
   多出来的采样全挤在流水线里）；在 `LV_EVENT_RENDER_READY` 里 `lv_timer_ready(手势轮询)` 想消掉帧间
   那 5~7 ms 空档，实测与不加同值（iv p50 29.4 vs 29.3 ms）。两条都已回退。
7. **"手势期间抑制 500 ms 全量刷新"不成立**：让 `ui_tick` 在 `motion_busy()` 时整轮早退，实测无差异——
   贵的那段（`refresh()`）早被既有的 `if (motion_busy())` 门挡掉了，前面只剩数据读取。已回退。
8. **为什么 UI 侧没杠杆了（阶段分解）**：加 `LV_EVENT_RENDER_START` 钩子把帧拆成
   按下→START（`p2rs`）/ START→READY（`rs2rr`＝绘制 + flush 提交）/ READY→下一个 START（`rr2rs`＝帧间空档）：

| 用例（N=10 中位数 µs） | iv50 | p2rs50 | rs2rr50 | rs2rr95 | rr2rs50 | tick50 |
|---|---|---|---|---|---|---|
| swipe:5:0:3（轻页） | 30,204 | 32,331 | 22,588 | 30,382 | 5,397 | 7,699 |
| swipe:1:0:3（重页） | 33,394 | 70,171 | 26,512 | 29,061 | 6,937 | 63,947 |
| swipe:3:0:3 | 29,293 | 61,972 | 22,715 | 25,329 | 6,811 | 51,224 |
| tap:3:3（不拖动） | —（iv_n 太少） | 2,942 | 8,351 | — | — | — |

   - `rs2rr` 22~27 ms 吃掉帧间隔的绝大部分，**而它与页面内容几乎无关**（轻页 22.6 vs 重页 26.5，同期
     tick 差 8.6 倍）⇒ 固定成本主导：TRIPLE_PARTIAL 每帧都要"脏区 DMA2D 拷进中间缓冲 → 整帧
     `esp_lcd_panel_draw_bitmap(0,0,全屏)`（1.2 MB）→ 等 VSYNC ISR 放缓冲"（`lvgl_bridge_v9.c:2462`
     → `display_bridge_common.c:521`；再看 tap 行：小脏区一帧也要 8.35 ms）。
   - `rr2rs` 只有 5~7 ms，本来就等于"等下一次手势采样"，没有可抢的东西。
   - P1 的地板是同一个来源：tap 的 `p2rs` 2.9 ms（输入段）+ `rs2rr` 8.35 ms（≈半个 60 Hz 周期）≈ 11.2 ms，
     基线 11.68 ms ⇒ **-30% 在结构上不可达**。
   → 再往下压只能动面板时序 / tear-avoid 缓冲策略（硬件侧，风险不对称，本轮不做）。
9. **每帧 flush 内部真的拆开了（本轮）**：在 gitignored 的适配器里给
   `display_bridge_v9_flush_partial_rotate`（`lvgl_bridge_v9.c:2720`）加计时，每 50 帧打印
   `[dbg-flush] n= rot= prep= msync= blit= dirty_kpx= copied_kpx= base= rep=`：

   | 每帧（N=3×~110 帧） | rot | prep | msync | blit | dirty | copied |
   |---|---|---|---|---|---|---|
   | swipe:3:0:3 | 1.7~1.9 ms | 1.2 ms | 8~10 µs | 5.6~7.5 ms | 130~154 kpx | 0 |

   `rot`＝PPA 把脏区转着写进 `draw_fb`；`prep`＝整帧 cache msync；`blit`＝整帧
   `esp_lcd_panel_draw_bitmap(0,0,全屏)` + 等空闲流水线缓冲；`copied_kpx=0`、`base=2`（只在开机）
   ⇒ **"每帧整屏 front→back 基线拷贝"这条假设被证伪**：repair 路径几乎不拷像素，msync 也不是瓶颈。
   每帧预算 = 绘制 ≈12~13 ms（由 `[dbg-layout]` 侧证）+ flush ≈10 ms + 输入采样空档 4.5 ms ≈ 27 ms。
10. **translate 的布局标记是"承重"的，搬去廉价对象是死路（本轮，两个探针）**：`motion_paint()` 每帧
    `lv_obj_set_style_translate_x()`，而 `lv_style.c:50` 给 `LV_STYLE_TRANSLATE_X` 挂了
    `LAYOUT_UPDATE | PARENT_LAYOUT_UPDATE` ⇒ 页自己 + 父对象每帧标脏布局。仪表实测
    **34 次布局应用/帧 × 103 µs ≈ 3.5 ms/帧**。两个探针都想把这 3.5 ms 拿掉：
    · 把 `lv_style.c:50` 的标记清 0（布局完全不跑）→ iv **34.2 / 37.8 / 35.5 ms**（对照 28.9 / 28.1 / 28.8）；
    · 把 translate 改成"只标父、不标自己"（＝模拟"translate 打在外壳上"）→ iv **35.2 / 37.0 / 35.1 ms**。
    两次都更慢 ~7 ms，且 `[dbg-layout]` 次数只从 11,250 降到 9,950 ⇒ 布局过程顺带把对象几何归一化，
    让后续无效区/绘制任务更省。**结论：布局必须跑，"外壳 + 廉价对象"方向不成立**（已还原探针，
    复测对照回到 26.9 / 27.0 / 27.0 ms）。
11. **面板 MADCTL 硬件 180° 在本板是 no-op（本轮）**：给面板加 `esp_lcd_panel_mirror(panel,true,true)`
    （EK79007 MADCTL 的 SHLR|UPDN）并把适配器改成 `ROTATE_0` → **速度大赢**：iv **24.8 / 24.2 / 25.4 ms**
    （每帧省 ≈4 ms，`[dbg-flush]` 完全不打印＝确实不再走旋转路径），但**画面反 180°**。
    把适配器改回 `ROTATE_180`（面板翻转保留）后照片与改前一致 ⇒ 像素取证：Z(无翻转+ROT180) vs
    Y(翻转+ROT180) **背景 0/14880、屏幕中段 4/49920 变化**（板子和手机都没动过）⇒ DSI video 模式下
    面板忽略 MADCTL。两个文件已 `git checkout` 还原。代价：180° 只能继续在适配器里租 PPA
    （rot 1.7 + prep 1.2 ≈ 3 ms/帧）。
12. **LVGL 绘制缓冲 600 行（单切片）跑不动（本轮）**：BSP profile 的 `.buffer_height` 120→600
    （`esp32_p4_wifi6_touch_lcd_7b.c:572`）后 `[dbg-flush]` 的 rot/prep/blit/dirty 与 120 行**完全一致**，
    而且 `bench swipe 3 0 3` 180 s 内不出 `[bench]` 行（boot 等 12 s 与 30 s 两次都复现）⇒ 已回退到 120 行。

## 五、未做（诚实清单）

- 远端 `d84039f` 里这几处未移植：曲线环 `EXT_RAM_BSS_ATTR`、`pool_layout` 布局跳过、
  `set_txt`/`uk_set_text` 栈缓冲、总览布局缓存。它们改善的是刷新/分配开销，而这两项
  已经超过 30%；曲线环要改图表数据结构，收益/风险不对称。
- 面板时序 / tear-avoid 缓冲策略（第四节 4/5/8 已证明 UI 侧到顶了，剩下的都在这一层：
  DOUBLE_DIRECT 起不来、NONE 不支持旋转、MADCTL 硬件旋转无效（第 11 条），要动得改适配器或面板驱动）。
- **要再拿回最后 0.8~1.0 个百分点，只剩适配器 flush 路径这一件事**，且它现在被量化得很清楚：
  每帧 `prep` 1.2 ms（`display_bridge_v9_prepare_partial_back_buffer` 的**整帧** cache msync）
  + `blit` 5.6~7.5 ms（整帧 `draw_bitmap` + 等 VSYNC 放缓冲）。两条改法都在 gitignored 的
  `managed_components/espressif__esp_lvgl_adapter/` 里：① 只 msync 脏区对应范围；② 不再整帧 blit，
  而是按脏区直接写当前扫描缓冲（＝NONE 的行为 + 自己转那 180°）。本轮没做：它是厂商组件的
  结构改动，要么留成本地补丁（不进仓库、别人 clone 后没有），要么做成仓库内的 patch 文件 +
  apply 脚本，需要先定交付方式。
- P2 的 p95 尾巴（34.9 ms 里偶发的 45 ms+ 帧）只查到"脏区大 + 等 VSYNC"这一步，
  没有进一步归因到具体某一帧。
- 未推送远端：本地 `main` 未动，成果都在 `perf/eval-20261010`，等确认后再决定怎么合。

## 六、复现

```bash
# 宿主门禁（84 状态 + 几何审计 + 运动预算）
bash tools/preview/run.sh /tmp/fx
PREVIEW_MOTION=0,1,0 PREVIEW_MOTION_SCROLL=1 PREVIEW_MOTION_RESPONSIVENESS=1 \
  tools/preview/build/preview /tmp/fxmotion

# 实机台架（需 CONFIG_FNOS_UI_PERF_BENCH=y）
python3 tools/perf_bench.py --port /dev/cu.usbmodemXXXX --label cur \
  --cases swipe:3:0:3,tap:3:3,idle:3:5 --repeat 20 --warmup 1 --boot 25 \
  --out /tmp/fnos-perf/cur.csv
python3 tools/perf_table.py /tmp/fnos-perf/base.csv /tmp/fnos-perf/cur.csv
```

台架 sdkconfig（本机未跟踪）：`CONFIG_FNOS_UI_PERF_BENCH=y`、`CONFIG_FNOS_UI_MOTION_STATS=y`、
`CONFIG_LV_USE_LOG=n`（**必须 n**，开着日志时同一用例 iv 34 ms、tick 63 ms、p2f 104 ms，差一个量级）；
生产配置的 sdkconfig 另有备份，验收前还原。

跨会话复现（同一份代码、不同会话，N=20，中位数）：swipe iv p50 27.1 / 27.2 / 28.2、iv p95
34.8 / 34.9 / 34.7、r2f 32.4 / 32.5 / 32.3、tick 35.7 / 35.6 / 35.6（ms）；idle tick 35.5 / 35.5；FPS
swipe 22.0 / 22.9。⇒ 这几项稳定可复核；`cpu_lvgl` 抖 ±40%（见第一节），别单次取信。

本轮临时仪表（都在 gitignored 的 `managed_components/` 里，测完已还原；下面写清位置以便复现）：

```bash
# ① flush 内部分解：lvgl_bridge_v9.c 的 display_bridge_v9_flush_partial_rotate（:2720）
#    计时 rot/prep/msync/blit + 脏区像素，每 50 帧打印 [dbg-flush] …
# ② 布局开销：lvgl__lvgl/src/core/lv_obj_pos.c 的 layout_update_core（:1531）里
#    给 obj->layout_inv 那段加 esp_timer，每 50 次打印 [dbg-layout] n= us_avg=
# ③ 串口原始抓取（复现单用例，不经 perf_bench 的 CSV）：/tmp/dbg_run.py
#    python3 /tmp/dbg_run.py        # 3× swipe:3:0:3，打印 iv/rs2rr/p2rs/rr2rs + 全部 [dbg-*] 行
#    注意：关串口＝板子复位；开端口后要等 ~25 s 再发命令。
```

台架用例顺序也必须保持一致：`swipe:3:0:3,tap:3:3,idle:3:5`（顺序会改数值，见第一节）。
