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

综合：**稳态（刷新开销、CPU、帧率）全面超过 30%；拖动跟手 -27.6~-27.9% / -29.0~-29.2%（差 0.8 / 0.4 ms）；
按下→首帧只有 8.2%。** 后两项不是没调好，而是不在 UI 侧：第四节十七条负结果里与这两项相关的每一条
都撞在同一组硬限制上——**帧间隔的地板 = 输入注入量化（~4.6 ms，两侧同值）+ TRIPLE_PARTIAL 每帧 flush
开销（拖动帧要 4 个切片）+ 60 Hz 面板节拍**；而"每帧固定成本"里最后两块可动的（整帧 cache msync、
切片数）本轮各试一次，一块是回归、一块让系统起不来（见下）。

**这一轮（goal round 3）新拿到的是归因，不是新速度。** 把"只换 `fnos_ui.c`"的对称实验和
"只改 BSP 绘制缓冲"的对照做完后，-27.9% 的来源被拆开了（同机、同台架、每组 3 次 `swipe:3:0:3`）：

| 固件 | iv p50 | 帧工作 `rs2rr` p50 | 帧间空档 `rr2rs` p50 |
|---|---|---|---|
| 真基线 = `a563000` 全套（缓冲 50 行、LVGL 日志开） | 38.6 / 38.8 / 38.6 ms | 31.0 / 31.4 / 31.3 ms | 7.3 / 7.3 / 13.8 ms |
| 真基线 + 只把 BSP 绘制缓冲 50→120 行 | 29.2 / 28.9 / 28.8 ms | 22.0 / 22.1 / 21.9 ms | 7.1 / 6.7 / 7.0 ms |
| 最终树（缓冲 120 + 本轮全部 UI 改动） | 26.9 / 27.0 / 27.0 ms | 22.4 / 22.4 / 22.4 ms | 4.6 / 4.6 / 4.8 ms |

- **最大的单项是 BSP 绘制缓冲**（全屏 12 切片 → 5 切片）：帧工作 **-29.6%**、iv **-25%**；
- **UI 文件本身只值 -6.6%**（iv 28.8→26.9），而且帧工作**没变好**（+1.8%），省下的是帧间空档
  `rr2rs` 6.8→4.7 ms（`refresh_soon()` 不再等 15 ms 刷新周期 + 读周期 15→8 ms）；
- 真基线 iv 38.6 ms 与 `base.csv` 的 37.6 ms 在噪声内 ⇒ **基线数字可复现**，-27.9% 不是测法造出来的；
- ⇒ 拖动这条线的钱已经从"等刷新 / 切片开销"里挣完了：剩下每帧 22 ms 里是 TRIPLE_PARTIAL 的
  固定成本（flush 8~10 ms）+ LVGL 实际绘制。

**这一轮（goal round 4）把上面那两个"固定成本"各砍一次，都失败，但地板因此钉死了**（同机、同台架、3 次
`swipe:3:0:3`）：

| 尝试 | 预期 | 实机结果 | 位置 |
|---|---|---|---|
| 适配器：删掉 flush 尾部**整帧** M2C（1.2 MB ≈1.2 ms/帧），改成按矩形维护 cache | -1 ms/帧 | **帧工作 +1.0~1.5 ms（回归）**：iv 28.8 / 27.7 / 28.0、rs2rr 23.9 / 23.4 / 23.3（对照 26.9 / 27.0 / 27.0、22.4 / 22.4 / 22.4）⇒ 已还原 pristine | 第四节 16 |
| BSP：绘制缓冲 120→300 / 600 行（每少一次 flush ≈1.3 ms，拖动帧只需 1 片） | -3.9 ms/帧 | **系统起不来**：LVGL 任务首刷后持锁不放、UI 没建出来、台架完全静默（300 与 600 同症状） | 第四节 12 |

⇒ **120 行（拖动帧 4 个切片）是这台机器上可用的最大切片**；帧工作 22.3 ms 里已没有可动的固定成本，
余下是 LVGL 绘制。要再往前只能换架构，三条路都已实测排除或需要外部条件：
① 面板支持 MADCTL 180°（本板 EK79007 视频模式忽略，第四节 11）⇒ 适配器走不旋转路径每帧省 4 ms
（实测 iv 24.2~25.4 ms，对基线 -31~-36%，能跨 30%）；② 换 tear-avoid / 缓冲策略（`DOUBLE_DIRECT`
起不来、`NONE` 不支持旋转，第四节 4）；③ 换掉注入型度量（台架合成手指的 8 ms 步进是 iv 的一部分，
真手指下这段不是 UI 的开销，见第四节 14）。

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
| 本轮（goal round 3） | 把手伸进适配器与输入侧各一次，然后把 -27.9% 的来源拆开：① 适配器"延迟交接"（flush 尾部的 `mark_busy`+等 VSYNC+换缓冲挪到下一帧开头）；② 触摸读回调外层采样 + indev 读周期 8→4 ms；③ 三组对照归因（真基线 / 真基线+只改 BSP 缓冲 120 行 / 最终树），全部用带拆分计数器的台架；④ 台架兜底上报（`pf_stop()` 若没出过 `[bench]` 行就补一行、arm 失败打印原因） | ①②全负（第四节 13/14），适配器已还原 pristine、UI 已 `checkout`；③ 见第一节归因表（**BSP 缓冲 -29.6% 帧工作**、UI 文件 -6.6% iv、真基线 38.6 ms 复现 `base.csv` 的 37.6 ms）；④ 工具修复，随本条提交（第四节 15） |
| 本轮（goal round 4） | 把两个"每帧固定成本"各砍一次：① 适配器整帧 M2C → 按矩形 cache 维护（顺带做了可复用的 `tools/patches` 交付骨架）；② 绘制缓冲 120 → 300 / 600 行（想每帧只 flush 一次） | 两条都不成立（第四节 16 / 12）：① 实测**回归** +1.0~1.5 ms 帧工作 ⇒ 适配器已还原 pristine（md5 4aa7024…）、`tools/patches` 已删；② 300/600 行 LVGL 锁饥饿、UI 建不出来 ⇒ `.buffer_height` 回到 120 并把症状写进 BSP 注释 |

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
12. **LVGL 绘制缓冲超过 120 行就跑不起来（本轮定案）**：BSP profile 的 `.buffer_height` 120→600
    （`esp32_p4_wifi6_touch_lcd_7b.c:580`）后 `bench swipe 3 0 3` 180 s 内不出 `[bench]` 行
    （boot 等 12 s 与 30 s 两次都复现）；300 行是同一个症状。**直接证据（600 行固件 + 交互探针
    `/tmp/probe600.py`）**：开机日志里
    `E (4662) esp_lvgl:adapter: esp_lv_adapter_lock(751): Failed to acquire LVGL lock` +
    `E (4670) main: display lock failed` —— LVGL 任务首刷之后再没让出锁，UI 从没建出来
    （`fnos_perf_init()` 在 UI 创建路径里 ⇒ 台架定时器也没建），所以 CLI 只回"已排队"、之后永远静默；
    CLI 本身正常（`bench help` / `page 0` / `bench idle 3 5` 都应答）。
    ⇒ **120 行（拖动帧 4 个切片）是这台机器上可用的最大切片**，`[dbg-flush]` 读数与 120 行相同的旧观测
    （round 2）由此得到解释；每少一次 flush 约省 1.3 ms 的算术（12→5 片省 9.3 ms 帧工作）到此封顶。
    已回退 `buffer_height = 120`，并把症状写进该处注释。
13. **"延迟交接"：把 flush 尾部的换缓冲挪到下一帧开头，省下等待但不省帧时间（本轮）**：适配器
    `display_bridge_v9_flush_partial_rotate` 尾部原本是 `mark_buf_busy` + `wait_free_buf`（等 VSYNC
    释放缓冲）+ 换 `disp_fb/draw_fb`；改成"上一帧只记 `swap_pending`，下一帧开头再交接"后，
    仪表读数：`blit` 7~9 µs（原先 5.6~7.5 ms，证明那 5.6~7.5 ms 全在 mark+wait）、`wait` 降到 1.2~3.6 ms，
    **但台架 iv p50 26,849 / 27,812 / 27,172（对照 26,932 / 27,006 / 26,991）基本不变**。
    ⇒ 与第 2、3 条一致的第二个独立证据：**拖动帧间隔不由 flush 的等待决定**。适配器已还原 pristine
    （`grep -c dbg_` = 0），不留补丁。
14. **触摸采样侧两条改动：实机全中性，而且台架根本看不见（本轮）**：① 把采样挂到触摸读回调外层
    （`lv_indev_set_read_cb` 包装，"读到新坐标就立刻起帧"）；② 读周期 8 ms → 4 ms。实机三次
    `swipe:3:0:3`：钩子+8 ms → iv 26,846 / 27,918 / 26,675；钩子+4 ms → 26,932 / 27,005 / 26,900；
    无钩子对照 → 26,932 / 27,006 / 26,991 ⇒ **三项（iv / rs2rr / rr2rs）都没变**。
    原因是台架自己包在最外层：`pf_read_cb`（`components/fnos_monitor/fnos_perf.c:152`）先转发真实读回调，
    **再覆写 `data->point/state`**（:160-162）⇒ 任何 UI 侧包装都只看得到没有手指的真实面板（等于空转）；
    而合成手指的位置只由 `pf_script_tick`（`lv_timer` 周期 `PF_STEP_MS`=8 ms，:484）每拍推进一步
    （`PF_DRAG_STEPS=15`，插值在 :331-341）⇒ 拖动期的 `rr2rs`（实测 4.5 ms）**就是这套注入量化的产物**
    （uniform[0, 8ms) 均值 4 ms）。**结论：触摸采样侧的任何改动在这个台架上不可判**；两条改动已
    `git checkout` 回退。没有为了凑数字去改台架的注入节奏。
15. **基线固件在新台架下静默停机（工具陷阱，已修）**：把 `fnos_ui.c` 换回 `a563000` 版本再跑
    `bench swipe 3 0 3`，设备只回 "已排队"、之后**一行 `[bench]` 都不出**，`tools/perf_bench.py` 180 s
    后抛 `TimeoutError: 等 [bench] 行超时`；`bench help` 正常、`bench idle`（不需要 arm）也静默 ⇒
    不是 arm 失败。根因：`fnos_perf_init()` / `fnos_perf_note_tick()` 的**调用点在 UI 文件里**
    （`components/fnos_monitor/fnos_ui.c:4644` / `:4587`），而 77c534b 给 `fnos_ui.c` 加的 28 行胶水
    正是这两处 ⇒ 换回旧 UI 后台架定时器从未创建，CLI 只排队不报错。
    复现基线要用「旧 UI + 胶水」：`git show a563000:components/fnos_monitor/fnos_ui.c > …/fnos_ui.c`，
    再 `git show 77c534b -- components/fnos_monitor/fnos_ui.c > /tmp/bench_glue.patch` +
    `patch -p1 -F3 < /tmp/bench_glue.patch`（`grep -c fnos_perf_init` = 1 才算对）。
    本轮已给 `pf_stop()` 加兜底上报（未出过 `[bench]` 行就报一次）+ arm 失败打印原因，
    以后同样的静默会直接变成一行可读的失败报告。
16. **把适配器的整帧 cache msync 换成按矩形维护 ⇒ 回归（本轮）**：假设"每帧 1.2 MB M2C 是纯开销"，
    于是删掉 `display_bridge_v9_flush_partial_rotate` 里无条件的
    `display_cache_msync_invalidate_framebuffer(impl->draw_fb, frame_buffer_size)`（`lvgl_bridge_v9.c:2768`），
    改成只在两条 CPU 真的碰帧缓冲的路径（`copy_diff_repair_from_front_to_back` :3009、
    `copy_unrendered_area_from_front_to_back` :3173 的 CPU 回退分支）前后按矩形
    `esp_cache_msync`（读前失效 / 写后回写），并为它建了 `tools/patches/`（patch 文件 + 幂等 apply.sh）。
    实机：iv **28.8 / 27.7 / 28.0 ms**、rs2rr **23.9 / 23.4 / 23.3 ms**（pristine 对照 26.9 / 27.0 / 27.0、
    22.4 / 22.4 / 22.4）⇒ **帧工作反而多 1.0~1.5 ms**。与第 10 条同型：**少做 cache/布局维护会让后续
    绘制更贵**（M2C 扫场像是把脏行提前写回，压掉了绘制期的 PSRAM 突刺）。已还原 pristine
    （md5 `4aa702438d34f72b4e96b89538bdc563` = 备份 `/tmp/v9_pristine.c`）、`tools/patches` 删除，
    非旋转路径的同类调用（:2604-2605）也没动。

17. **把面板帧缓冲 3→4 ⇒ 回归（这一枪，用户拍板「可以」后实测）**：假设"旋转 partial 每帧要等下一个
     VSYNC 才拿得到空闲缓冲"——3 个缓冲时 `display_bridge_pipeline_init_from_cfg`
     （`display_bridge_common.c:1079`）把 fb[0]/fb[1] 当显示/绘制对，`empty_list` 只剩 fb[2] 一个，
     `display_bridge_pipeline_wait_free_buf`（`:1200`）取不到就 `ulTaskNotifyTake` 死等，而归还由
     `on_refresh_done`（VSYNC 中断）驱动 ⇒ 每帧最多归还 1 个。于是把上限抬到 4：IDF
     `components/esp_lcd/dsi/mipi_dsi_priv.h:36 DPI_PANEL_MAX_FB_NUM` 3→4，并把
     `dpi_panel_draw_bitmap` 里"这块绘制缓冲本身就在帧缓冲里"的硬编码判定（原来只认 `fbs[0..2]`）
     改成 `for (i < num_fbs)` 循环；适配器 `adapter_internal.h:41 ESP_LV_ADAPTER_MAX_FRAME_BUFFERS` 3→4、
     `display_bridge_common.h:60 frame_buffers[3]` 换成宏、`display_manager_fetch_panel_frame_buffers`
     加 `fb3` 与 `required >= 4` 分支、`display_manager_required_frame_buffer_count` 旋转分支返回 4。
     实机（同机、同台架、同脚本，3×`swipe:3:0:3`，每组 180；回滚后同场复测的 3 缓冲即对照）：

     | 配置 | iv p50 (µs) | rs2rr p50 (µs) | rr2rs p50 (µs) | frames |
     |---|---|---|---|---|
     | 3 缓冲（对照） | 26,976 / 26,873 / 26,783 | 22,378 / 22,248 / 22,201 | 4,600 / 4,637 / 4,565 | 120 / 119 / 116 |
     | 4 缓冲 | 28,465 / 28,538 / 28,850 | 24,026 / 24,008 / 24,858 | 4,607 / 4,742 / 4,545 | 107 / 111 / 116 |

     ⇒ 每帧 iv **+1.5~2.1 ms**、帧工作 rs2rr **+1.6~2.7 ms**，帧数还少约 10%。`rr2rs` 没变（等待本来
     就不在这一项里），说明多出来的那个缓冲换来的"不等 VSYNC"确实到手了，但代价更大：绘制目标在 4 个
     缓冲之间轮流 ⇒ 它比过去陈旧 2~3 帧，`display_bridge_v9_prepare_partial_back_buffer` 的基线与修补面
     是按"上一次显示的那个缓冲"算的，目标越陈旧要补的越多。与第 10、16 条同型——**适配器这套 partial
     机制依赖那两步看起来多余的维护**（整帧 msync、layout 标记、紧耦合的双缓冲对）。已全部还原（IDF 2 处、
     适配器 3 处、BSP `Kconfig` 的 `range 1 4`、`sdkconfig.defaults`、`sdkconfig`），回滚后按生产配置重编、
     烧录并跑 `tools/verify_all.sh --flash` = **10 PASS / 0 FAIL**；为它准备的 `tools/patches/vendor`
     补丁与 apply 脚本一并删除（不为负收益的改动出交付物）。

18. **面板侧 `ROTATE_0` + LVGL 绘制期矩阵旋转 ⇒ 能跑、画面也正确，但更慢（这一枪）**：想法是让适配器
     不走 `flush_partial_rotate`（省 rot 1.7 + prep 1.2 + 整帧 blit），180° 交给 LVGL 在**绘制时**用矩阵
     摆正：`CONFIG_LV_USE_MATRIX=y` + `CONFIG_LV_DRAW_TRANSFORM_USE_MATRIX=y`，
     `lv_display_set_matrix_rotation(disp,true)` + `lv_display_set_rotation(disp,LV_DISPLAY_ROTATION_180)`，
     触摸补 `mirror_x=mirror_y=1` 抵消 `lv_indev.c:743 lv_display_rotate_point` 的反向变换。
     两个前提都得先绕：`lv_display_set_matrix_rotation`（`lv_display.c:1013`）直接拒 PARTIAL
     （本板只能跑 PARTIAL，见 19），临时改掉才测得到；`main.cpp` 的 `bsp_display_lock(0)`（try-lock）
     在矩阵路径下建 UI 时会失败（LVGL 任务正忙），改成 2 s 超时才起得来。实机（同机同台架）：

     | 配置 | iv p50 (µs) | rs2rr p50 (µs) | rr2rs p50 (µs) | frames | cpu_lvgl |
     |---|---|---|---|---|---|
     | 旋转在适配器（对照） | 26,976 / 26,873 / 26,783 | 22,378 / 22,248 / 22,201 | 4,600 / 4,637 / 4,565 | 120 / 119 / 116 | — |
     | 矩阵旋转（ROTATE_0） | 28,647 / 31,537 / 29,579 | 22,665 / 25,143 / 23,154 | 6,327 / 6,159 / 6,326 | 99 / 107 / 114 | 16~20% |

     ⇒ 省掉 rot+prep 却每帧 **+1.7~4.7 ms**：矩阵变换是**逐像素软件**做的（`tick_p50` 从 ~26 ms 涨到
     43~45 ms），比 PPA 硬件旋转贵得多。`capture-board` 照片取证：画面完整、朝向与改造前相差 180°
     （⇒ 矩阵真的生效了、没白测），所以这是纯性能负结果，不是"没接上"。
19. **FULL / DIRECT 渲染模式在本板起不来（第 18 条的两个变体）**：矩阵旋转要求 `render_mode == FULL ||
     DIRECT`，于是试了 `TRIPLE_FULL`（想拿 FULL 渲染 + 面板缓冲零拷贝换缓冲）与 `NONE`（想拿最省的
     按脏区 `draw_bitmap`、没有整帧 blit）：
     · `TRIPLE_FULL`：首帧 flush 里 LVGL 任务卡住 ⇒ `main` 卡在 `bsp_display_lock`，10 秒后 IDLE0
       看门狗触发，UI 根本没建出来（启动日志停在 `on_frame_buf_complete unavailable; buffer-switch
       release and dummy-draw may not function on MIPI DSI`）——与 `DOUBLE_DIRECT` 同族（第 4 条）。
     · `NONE`：卡死得更早（UI 创建前），连 `bsp_display_lock` 都没走到。
     ⇒ 本板 MIPI DSI 拿不到 buffer-switch 回调，凡是要"换缓冲"的模式都不可用，能跑的只有 PARTIAL 两兄弟
     （`DOUBLE_FULL` 依赖同一个 double-switch wait，没再试）。
20. **面板帧缓冲 3→2 + `DOUBLE_PARTIAL` ⇒ 反向验证了第 17 条的机制，但更慢**：第 17 条说"缓冲越多，
     绘制目标越陈旧 ⇒ 修补面越大"，那就反过来试 2 个（`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2` +
     `DOUBLE_PARTIAL`，配置级、不碰厂商代码）：iv p50 **33,125 / 33,125 / 33,125**、
     rs2rr **28,517 / 28,488 / 28,288**、rr2rs 4,487 / 4,518 / 4,354、frames 114 / 107 / 95。
     `rr2rs` 略降（修补确实少了）但帧间隔整体 **+6 ms**：2 个缓冲时 `empty_list` 一个备用都没有，
     每帧都得等 VSYNC 归还（正是第 17 条开头那段机制），等待成本远大于修补成本。⇒ **3 个缓冲是甜点**。

21. **绘制切片 120 → 300 行（官方 README 建议的方向）⇒ 撕裂保护静默失效 + 每帧 +17 ms**：第 12 条当时
     把 300/600 行跑不动归给"LVGL 任务在首刷后不再让出锁"，round 6 复查发现那条失败链的**入口是
     `main.cpp` 的 `bsp_display_lock(0)`（try-lock）**——LVGL 正忙就被判成死，和矩阵旋转实验（第 18 条）
     撞的是同一个坑。把该处改成 `bsp_display_lock(2000)` 后，300 行**能起来**了（`ui created` @5539 µs），
     但 boot 日志给出了真正的限制：
       `E esp_lvgl:disp: alloc partial draw buffer 614400 bytes failed`
       `W esp_lvgl:disp: tear mode 4 setup failed, falling back to allocated buffers`
     机制：`TRIPLE_PARTIAL` 的 partial 缓冲由适配器按**内部 RAM** 分配
     （`display_manager.c:1342 display_manager_alloc_draw_buffer(cfg->draw_buf_pixels * color_size, false)`
     —— 第二个参数硬编码 `false`，不吃 profile 的 `use_psram`）。120 行 = 1024×120×2 = **240 KB** 正好
     是本板内部 RAM（~361 KB 可用）装得下的最大值；300 行要 **600 KB**，分配失败 ⇒ 适配器**静默退回**
     普通 PARTIAL（PSRAM 双缓冲、没有三缓冲流水线 ⇒ 撕裂保护没了）。实测（退回路径，仍 300 行切片）：

     | 配置 | iv p50 (µs) | rs2rr p50 (µs) | rr2rs p50 (µs) | frames |
     |---|---|---|---|---|
     | 120 行 + TRIPLE_PARTIAL（对照） | 26,976 / 26,873 / 26,783 | 22,378 / 22,248 / 22,201 | 4,600 / 4,637 / 4,565 | 120 / 119 / 116 |
     | 300 行（撕裂保护静默失效） | 44,759 / 44,192 / 44,454 | 39,726 / 39,244 / 39,500 | 5,087 / 4,803 / 5,216 | 104 / 99 / 105 |

     ⇒ **+17 ms/帧**。这一条的价值有两个：① 官方 README「提高 `buffer_height` 提升吞吐」在本板被
     "撕裂保护缓冲走内部 RAM"这条硬约束卡死，120 行就是上限（第 12 条的结论保住了，但机制换成了内存，
     不是锁）；② 反过来证明 `TRIPLE_PARTIAL` 那套流水线是**承重**的——丢掉它比第 17 条的 3→4 缓冲
     代价大一个量级。已还原 `.buffer_height = 120`；**保留** `bsp_display_lock(2000)`（LVGL 忙不该被
     当成失败，UI 建不出来是静默故障）。
22. **`uk_set_text` 每 500 ms 给没变化的标签白标脏一次（已修，只省稳态）**：`uk_set_text`（`uk.c:274`）
     是文本更新的唯一咽喉，它调 `uk_number_set_text(...,false)`；该函数在非动画分支里先
     `clear_layers(obj)` 再比文本（`uk_number.c:154-158`，文本比较是有的），但 `clear_layers`（:69）结尾是
     **无条件的 `lv_obj_invalidate(obj)`** ⇒ 文本没变也把整个标签标脏一次，每个标签每 500 ms 一次。
     修法（根因、单点）：`clear_layers` 开头 `if (!n->layers && !n->active) return;`。
     注意**它不改善拖动指标**：拖动期间 `ui_tick` 在 `if (motion_busy())` 处提前返回（`fnos_ui.c:4562`），
     刷新根本不在拖动帧里跑 ⇒ 这条只省稳态/idle 的刷新与 CPU（辅助指标）。

## 五、未做（诚实清单）

- 远端 `d84039f` 里这几处未移植：曲线环 `EXT_RAM_BSS_ATTR`、`pool_layout` 布局跳过、
  `set_txt`/`uk_set_text` 栈缓冲、总览布局缓存。它们改善的是刷新/分配开销，而这两项
  已经超过 30%；曲线环要改图表数据结构，收益/风险不对称。
- 面板时序 / tear-avoid 缓冲策略（第四节 4/5/8 已证明 UI 侧到顶了，剩下的都在这一层：
  DOUBLE_DIRECT 起不来、NONE 不支持旋转、MADCTL 硬件旋转无效（第 11 条），要动得改适配器或面板驱动）。
- **要再拿回最后 0.5~0.8 ms，只剩"绕开适配器旋转路径"这一件事**，而这一轮把能在配置层做的五条路都堵死了：
  ① `prep`（每帧**整帧** cache msync 1.2 ms）换成按矩形维护 ⇒ **回归 +1.0~1.5 ms**（第四节 16，已还原）；
  ② 每帧 4 次 flush 的切片开销想用更高的绘制缓冲抹掉 ⇒ **300 行能起来但撕裂保护静默失效、每帧 +17 ms**
     （第四节 **21** 查明机制：`TRIPLE_PARTIAL` 的 partial 缓冲写死走内部 RAM，240 KB 是本板天花板）；
  ③ 面板侧不转、180° 交给 LVGL 绘制期矩阵 ⇒ **画面正确但每帧 +1.7~4.7 ms**（第四节 18，已还原）；
  ④ FULL/DIRECT 渲染模式（矩阵旋转和零拷贝换缓冲的前提）本板**根本起不来**（第四节 19）；
  ⑤ 帧缓冲 3→2 反向试 ⇒ 每帧 +6 ms（第四节 20）。⇒ **配置层没有剩余空间了。**
  剩下唯一没试的是**厂商组件的结构改动**：适配器的 PARTIAL flush 不再"先把脏区转进中间缓冲、再整帧 blit"，
  而是按脏区直接转着写当前扫描缓冲（PPA 按面积算，理论上比现在的整屏 PPA + 整帧 blit 便宜，但
  "省 ≈4 ms"这个上限来自第 11 条**完全不旋转**的配置，这条路仍要做旋转，实际能省多少**未验证**）。
  另外两条更重的：ⓐ 换一块吃 MADCTL 的屏 / 让 BSP 走 DSI 命令模式（本板 MADCTL 是 no-op，第 11 条）；
  ⓑ 改面板驱动的时序/刷新策略。交付方式用户已拍板 **B**（仓库内 patch + 幂等 apply 脚本，见 `tools/patches/`），
  但**负收益的改动不出交付物**——本轮的 18/19/20/21 都已还原（21 只保留 `bsp_display_lock(2000)` 这一处
  健壮性修复；22 是净收益但只影响稳态，不影响 iv/rs2rr）。
- 输入采样侧（读回调外层采样、读周期 8→4 ms）本轮已实测为**中性**，而且台架对这条线不可判
  （第四节 14：`pf_read_cb` 会覆写坐标、合成手指每 8 ms 走一步）⇒ 以后不要再用这套台架去调它。
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
注意上面那组数是**当时那套配置 + 当时那个日志级别**下的；本轮的三组对照里把 `CONFIG_LV_USE_LOG=y`
单独恢复回来（默认级别、缓冲 120 行、同一份 UI），量出来与关掉时一样（iv 28.83 vs 28.83 ms）
⇒ 复现旧数时要把日志级别一起对齐，别只改开关。

真基线复现（**换回旧 UI 一定要补台架胶水**，否则 `fnos_perf_init()` 没人调用、CLI 只排队不执行，
台架脚本只能等到 180 s 超时——第四节 15）：

```bash
git show a563000:components/fnos_monitor/fnos_ui.c > components/fnos_monitor/fnos_ui.c
git show 77c534b -- components/fnos_monitor/fnos_ui.c > /tmp/bench_glue.patch
patch -p1 -F3 --no-backup-if-mismatch < /tmp/bench_glue.patch   # grep -c fnos_perf_init 应为 1
# 真基线还要把这几处也回到 a563000（绘制缓冲 50 行、LVGL 日志开）：
git checkout a563000 -- components/esp32_p4_wifi6_touch_lcd_7b/esp32_p4_wifi6_touch_lcd_7b.c \
    main/main.cpp sdkconfig.defaults
# 归因三组：① 真基线；② 只 `git checkout HEAD -- <bsp 文件>`（缓冲 120）；
#           ③ `cp` 回当前 fnos_ui.c。每组烧录后跑 3× swipe:3:0:3，读 [bench] 行的
#           iv_p50_us / rs2rr_p50_us / rr2rs_p50_us（原始抓取脚本见下）。测完 git status 必须是干净的。
```

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
#    变体：/tmp/dbg_run_ho.py、/tmp/dbg_run_base.py（同一脚本、只换落盘文件名）——归因三组就靠它，
#    每次只打印三行 "iv_p50=… rs2rr_p50=… p2rs_p50=… rr2rs_p50=… frames=…"
#    注意：关串口＝板子复位；开端口后要等 ~25 s 再发命令。
# ④ p2rs/rs2rr/rr2rs 三个拆分计数器与 [bench] 行的对应字段是 bench-only 代码
#    （components/fnos_monitor/fnos_perf.c，默认关），拿 N=20 CSV 时可以直接看列。
# ⑤ 绘制缓冲高度 A/B：/tmp/run_buf_h.sh <行数>（sed 改 BSP 的 .buffer_height → 构建 → 烧录 →
#    /tmp/dbg_run_any.py 跑 3× swipe:3:0:3）；300/600 行会静默停机，用 /tmp/probe600.py
#    开串口看开机日志（esp_lv_adapter_lock / display lock failed）确认是这个原因。
```

台架用例顺序也必须保持一致：`swipe:3:0:3,tap:3:3,idle:3:5`（顺序会改数值，见第一节）。
