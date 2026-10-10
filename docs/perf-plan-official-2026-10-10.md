# 优化方案（官方文档版）2026-10-10

前提：只走**厂商/官方支持**的开关与 API，不引入自造机制。所有结论都能在本仓库里对着官方源
（ESP-IDF 5.5.3 源码 + LVGL 9.5.0 源码 + `esp_lvgl_adapter` 官方组件）核到行号，实测数字一律来自
实机计数器（`CONFIG_FNOS_UI_PERF_BENCH=y`，同台架同脚本）。

官方依据（本方案引用的原文都在这些地方）：

- ESP-IDF PPA 文档：<https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/ppa.html>
  （重点：Buffer Alignment、Performance Overview 两节）
- ESP-IDF 内存同步 `esp_cache_msync`：<https://docs.espressif.com/projects/esp-idf/zh_CN/v6.0.2/esp32p4/api-reference/system/mm_sync.html>
- `esp_lvgl_adapter` 官方组件：<https://components.espressif.com/components/espressif/esp_lvgl_adapter>
  （本仓库内同一份文档：`managed_components/espressif__esp_lvgl_adapter/README_CN.md`，含「推荐的 SDKCONFIG 配置」
  「缓冲调优」「限制与注意事项」「PPA/DMA2D 加速」「ESP-IDF 补丁说明」）
- ESP32-P4 数据手册：<https://documentation.espressif.com/esp32-p4_datasheet_en.html>
- LVGL 9.5.0 官方 Kconfig 与源码：`managed_components/lvgl__lvgl/Kconfig:550`（`LV_USE_PPA`）、
  `managed_components/lvgl__lvgl/src/draw/espressif/ppa/`
- 芯片能力宏：`$IDF_PATH/components/soc/esp32p4/include/soc/soc_caps.h:108`（`SOC_SIMD_INSTRUCTION_SUPPORTED`）、
  `:190`（`SOC_CPU_HAS_PIE`）、`:204`（`SOC_SIMD_PREFERRED_DATA_ALIGNMENT 16`）

---

## 一、结论速览

1. **官方推荐配置清单已经全部满足**（逐条核对见第二节表），没有"漏开官方开关"这种事可捡。
2. 官方支持的两条**硬件层**杠杆当天就测完了，**都是负结果**（明细见 2.3/2.4）：
   - **A1** 绘制缓冲进片内 SRAM（`profile.use_psram = false`）⇒ 帧间隔 ±0.3~+1.5 ms，无收益，已还原。
   - **A2** LVGL 上游 PPA 绘制单元（`CONFIG_LV_USE_PPA=y` + `LV_DRAW_BUF_ALIGN=128`）⇒ 每帧 +1.2~2.1 ms，已还原。
3. 因此**只剩 UI 架构层的 B1**（压缩每帧无效区），这是官方文档里唯一还没被实测排掉、且直接对
   「PPA 成本 ∝ 数据块大小」这条原理的杠杆 —— 见第三节。
3. 结构性地拿不到的部分（第 17~20 条实测）：每帧的"整帧 handoff + 等 VSYNC"是这套 partial pipeline
   的固有成本，换缓冲数量、换渲染模式、换旋转实现都只会更慢或起不来。**30% 的缺口里有一部分是这块，
   不再指望用配置消灭它。**
4. 目标与预期（诚实版）：A1/B1 打的是 `iv`（按下→首帧）里"渲染 + PPA 读源 + 修补"那几段；
   A2 官方原文说"**显著降低 CPU 占用、通常不会带来明显的 FPS 提升**"，所以它的收益记在辅助指标
   （`cpu_lvgl`／功耗）上，不要拿它当 30% 的主力。

---

## 二、处理器能力层：官方清单核对 + 三条动作

### 2.1 官方推荐 sdkconfig 逐条核对（`esp_lvgl_adapter/README_CN.md:143-201`）

| 官方建议 | 本仓库现状 | 结论 |
|---|---|---|
| `CONFIG_FREERTOS_HZ=1000` | `sdkconfig:1722` = 1000 | ✅ |
| `CONFIG_LV_OS_FREERTOS=y` | `sdkconfig:2916` | ✅ |
| `CONFIG_LV_USE_CLIB_STRING/SPRINTF=y` | `:2897` / `:2900` | ✅ |
| `CONFIG_LV_USE_CLIB_MALLOC=y` | `:2892` 未开 | ⚠️ 可试（LVGL 走标准 malloc） |
| `CONFIG_LV_DEF_REFR_PERIOD=15` | `:2907` = 15 | ✅ |
| `CONFIG_LV_OBJ_STYLE_CACHE=y` | `:3009` | ✅ |
| `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`（多核并行渲染） | `:2948` = 2 | ✅ |
| ESP32-P4 PSRAM：`CONFIG_SPIRAM_XIP_FROM_PSRAM=y` | `:1475` | ✅ |
| `CONFIG_CACHE_L2_CACHE_256KB=y` | `:1519` | ✅ |
| `CONFIG_CACHE_L2_CACHE_LINE_128B=y` | `:1523` | ✅ |
| `CONFIG_COMPILER_OPTIMIZATION_PERF=y` | `:776` | ✅ |
| （另）PSRAM 200M、CPU 400MHz | `:1469`、`:1513` | ✅ |

⇒ **官方给的"照抄就能提速"的开关一个没漏**。这也解释了为什么前几轮在 UI/适配器侧抠出来的都是零点几毫秒。

### 2.2 官方对"防撕裂模式 + 缓冲数"的推荐，与本机一致

`README_CN.md:252-264` 的模式表：`TRIPLE_PARTIAL` 的适用场景正是「**旋转（90°/270°）+ 高分辨率流畅 UI**」，
缓冲数 3；`README_CN.md:247-250` 给出 `num_fbs` 规则（非 0° 旋转需要 3）。
本机就是 `TRIPLE_PARTIAL` + `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3` + `ROTATE_180` ⇒ **与官方推荐一致**。
第 17/19/20 条的实测（4 缓冲更慢、2 缓冲更慢、FULL/DIRECT 起不来）是这条推荐的三个反证，
**不要再动这一块**。

### 2.3 A1｜绘制缓冲进片内 SRAM（官方开关，一行）

- **官方依据**：
  - 适配器把 LVGL 绘制缓冲的分配位置做成显式开关：
    `display_manager.c:1169` `const uint32_t caps = use_psram ? MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT : MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;`
    并统一用 `heap_caps_aligned_alloc(align, …)`（`display_manager.c:1172`，对齐取 `esp_cache_get_alignment()`）。
  - PPA 官方文档 *Performance Overview*：「**PPA performance highly relies on the PSRAM bandwidth** … when there
    are quite a few peripherals reading and writing to the PSRAM at the same time, the performance of PPA
    operation will be greatly reduced.」本机正是 DSI 扫描输出 + CPU 渲染 + PPA 三方同时打 PSRAM。
  - 适配器官方说明（`README_CN.md:345-348`）：PSRAM 时延高于片内 RAM。
- **动作**：`components/esp32_p4_wifi6_touch_lcd_7b/esp32_p4_wifi6_touch_lcd_7b.c:586`
  `.use_psram = true` → `false`（该 BSP 在本仓库受版本管理，不是厂商只读目录，属正常仓库改动）。
- **预期**：LVGL 渲染的写目标（1024×120×2 ≈ **245 KB**）与 PPA 的读源不再落 PSRAM，
  渲染期省掉 PSRAM 写回与缓存抖动；`iv` 有望改善，`rot`（PPA 写面板帧缓冲）仍受 PSRAM 带宽限制。
- **先决条件／风险**：片内 SRAM 余量必须够 245 KB。执行前先量
  `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`（在 `bsp_display_start_with_config()` 之后打印一次即可）；
  不够就把 `profile.buffer_height` 从 120 降档（官方 `README_CN.md:295-301`：降低 `buffer_height` 省 RAM、代价是 flush 次数变多）。
  失败会走 `display_manager_alloc_draw_buffer()` 的报错路径（不会静默降级）。
- **验收**：bench 同脚本 `iv/rs2rr/rr2rs` 三组数字 + `cpu_lvgl` + 片内余量日志 + `tools/verify_all.sh --flash` 仍 10 PASS。

**实测（2026-10-10，同机同台架同脚本，bench 配置，3×`swipe:3:0:3`）——负结果，已还原：**

| 配置 | iv p50 (µs) | rs2rr p50 (µs) | rr2rs p50 (µs) | frames | cpu_lvgl |
|---|---|---|---|---|---|
| 绘制缓冲在 PSRAM（现状） | 26,976 / 26,873 / 26,783 | 22,378 / 22,248 / 22,201 | 4,600 / 4,637 / 4,565 | 120 / 119 / 116 | ~16% |
| 绘制缓冲在片内 SRAM | 28,528 / 27,046 / 27,277 | 23,902 / 22,424 / 22,561 | 4,897 / 4,622 / 5,024 | 117 / 125 / 94 | 16.1 / 13.6 / 11.8% |

⇒ 一片内 SRAM 分配成功（启动无报错、`ui created` 5536 µs 正常），但**帧间隔没变好**（第一组 +1.5 ms，后两组 ±0.3 ms），
CPU 也没降。原因：每帧真正写进绘制缓冲的只有脏区那 0.26~0.30 MB（130~154 kpx），PSRAM 200M 完全吃得住；
瓶颈在整帧 handoff + 等 VSYNC（第 17/19/20 条），与"缓冲在哪儿"无关。**已 `git checkout` 还原。**

### 2.4 A2｜LVGL 上游 PPA 绘制单元（官方开关）

- **官方依据**：LVGL 9.5.0 自带 Espressif PPA 绘制单元：
  `Kconfig:550 config LV_USE_PPA  bool "Use Espressif's PPA accelerator for ESP SoCs"`（默认 n），
  配套 `LV_USE_PPA_IMG`、`LV_PPA_BURST_LENGTH`（默认 128 B），源码在
  `managed_components/lvgl__lvgl/src/draw/espressif/ppa/`。本机现状：`sdkconfig:2968 # CONFIG_LV_USE_PPA is not set`。
- **动作**：`CONFIG_LV_USE_PPA=y`（必要时再开 `CONFIG_LV_USE_PPA_IMG=y`）。
  官方同时提示（`README_CN.md:726-732`）「启用 PPA 加速时，建议 `LV_DRAW_SW_DRAW_UNIT_CNT == 1`」
  ⇒ 与当前 `=2` 做 A/B 两组，别一次改两个变量。
- **预期**：官方原文「PPA 可显著降低 CPU 占用；通常不会带来明显的 FPS 提升」⇒ 记在 `cpu_lvgl`／功耗，
  FPS 若无变化属正常，不作为失败判据。
- **风险**：PPA 与 DSI 扫描输出争 PSRAM 带宽（同 A1 的官方结论）；可能出现争用导致 `iv` 反而变差 ⇒ 必须 A/B。

**硬前提（第一次 build 就撞上了，官方断言）**：LVGL 的 PPA 单元要求绘制缓冲对齐等于 L2 cache line 尺寸 ——
`managed_components/lvgl__lvgl/src/draw/espressif/ppa/lv_draw_ppa_private.h:41`：
`#error "CONFIG_LV_DRAW_BUF_ALIGN must be equal to CONFIG_CACHE_L2_CACHE_LINE_SIZE!"`
⇒ 我们 `CONFIG_LV_DRAW_BUF_ALIGN=4`（`sdkconfig:2930`）而 L2 是 128（`:1523`），必须一起改成 `CONFIG_LV_DRAW_BUF_ALIGN=128` 才能编过。

**实测（同上条件）——负结果，已还原：**

| 配置 | iv p50 (µs) | rs2rr p50 (µs) | rr2rs p50 (µs) | frames | cpu_lvgl |
|---|---|---|---|---|---|
| 现状 | 26,976 / 26,873 / 26,783 | 22,378 / 22,248 / 22,201 | 4,600 / 4,637 / 4,565 | 120 / 119 / 116 | ~16% |
| `LV_USE_PPA=y` + `LV_DRAW_BUF_ALIGN=128` | 28,185 / 29,011 / 28,354 | 23,941 / 24,077 / 24,104 | 4,694 / 4,952 / 4,604 | 120 / 96 / 112 | 15.7 / 11.9 / 12.4% |

⇒ 每帧 **+1.2~2.1 ms**、CPU 也没省：LVGL 的 PPA 绘制与适配器的 PPA SRM（每帧旋转）**抢同一个 PPA + 同一条 PSRAM 通路**，
官方那句"通常不会带来明显的 FPS 提升"在本机还要更差一点。**已还原（`LV_USE_PPA` 与 `LV_DRAW_BUF_ALIGN` 都回原值）。**

### 2.5 已核对但**不适用**的官方项（避免重复踩）

| 项 | 官方说法 | 本机结论 |
|---|---|---|
| `LV_DRAW_SW_ASM_RISCV_V`（`Kconfig:366`） | LVGL 的 RISC-V **Vector** 汇编后端 | P4 是 **PIE/SIMD**（`soc_caps.h:108/190`），不是 V 扩展 ⇒ 不适用；P4 的 PIE 优化由 IDF 的 memcpy 等例程覆盖 |
| `esp_async_memcpy`（2D-DMA，官方 `async_memcpy.rst`） | 异步内存拷贝 | 适配器在矩形修补/基线拷贝里已经优先走 DMA2D、只在窗口不兼容时退 CPU ⇒ 无增量 |
| PPA 输出缓冲对齐（官方 *Buffer Alignment*：可缓存内存里 `out.buffer` 与 `out.buffer_size` 必须按 cache line 对齐） | 必须对齐 | 适配器已用 `heap_caps_aligned_alloc(esp_cache_get_alignment(...))` 满足 ✅（L1 64 B / L2 128 B，见 `sdkconfig:1524-1525`） |
| flash encryption × PPA SRM（官方：外存缓冲 + flash 加密时 SRM 不可用） | 限制 | `CONFIG_SECURE_FLASH_ENC_ENABLED` 未开（`sdkconfig:676`）⇒ 不触发 |
| PPA 卡死补丁（`README_CN.md:774-807`，适用于 IDF v6.0） | TRIPLE_PARTIAL + 旋转下 PPA 可能导致画面卡死，补丁移除 `ppa_srm.c` 里的 DIG-734 workaround | 本机 IDF 5.5.3 的 `components/esp_driver_ppa/src/ppa_srm.c:159` 仍是 workaround 版本 ⇒ **若后续出现画面卡死，按官方路径评估升 IDF/打该补丁**，不要自造 workaround |

---

## 三、UI 架构层：把"每帧要处理的像素"降下来

官方原理（PPA 文档 *Performance Overview*）：「**the time it takes to complete a PPA transaction is proportional
to the amount of the data in the block. The size of the entire picture has no influence on the performance.**」

⇒ 关键不是"画得多快"，而是"**每帧被标脏的面积**"。现在每帧 dirty ≈ 130~154 kpx，对应 `rot` 1.7~1.9 ms，
`prep`（基线/修补）1.2 ms；**这两项都随面积走**，所以 UI 层压面积是直接省时间。

### B1｜无效区最小化（按收益排序做）

1. **值不变就不写**：`uk_set_text` / `uk_number_*` / `uk_row_set` 在文本未变化时提前 return，
   避免无谓的 `lv_label_set_text` → invalidate。先审计这几个函数的实现，再补"相同即跳过"。
2. **趋势图只重画变化的列**：现在每次 push 触发整图重绘（120×40 px 级），
   改用官方 `lv_obj_invalidate_area(obj, &col_area)` 只标脏最后一列（LVGL 官方 API，非自造机制）。
3. **清单页更新而非重建**：行/池已有 `rows_sync` 复用路径，审计每 tick 是否仍有"销毁+重建"的临时对象
   （每个新对象都会让父容器重排、重绘）。
4. **保持不透明、不要阴影/渐变**（官方 LVGL 性能常识；本仓库已核：`uk_theme.c` 里 shadow/gradient 计数都是 0，
   卡片与屏用 `LV_OPA_COVER`，容器用 `LV_OPA_TRANSP`）⇒ 无需改动，**保持这条纪律**即可。
5. **对象数与层深**：合并纯装饰的中间容器、避免无谓嵌套（每个对象都要参与 layout + draw 遍历）。

### B2｜交互响应（P1：按下→首帧）

- **立即刷新**：官方 API `esp_lv_adapter_refresh_now(disp)`（`README_CN.md:513-519`）。
  在触摸事件回调里对"需要马上反馈的交互"调一次，可以不等 `LV_DEF_REFR_PERIOD=15 ms` 的自然节拍。
- **触摸已走中断模式**：BSP `esp32_p4_wifi6_touch_lcd_7b.c:524 .int_gpio_num = BSP_LCD_TOUCH_INT` ⇒ 不用改。
- 实测地板（第 14 条 + 第 9 条）：台架合成手指 8 ms 步进本身贡献 ≈4.6 ms 量化，
  加上"半帧"≈8.35 ms，P1 的 30% 在同一架构里已不可达；B1/A1 只能改善其中"渲染+PPA"那一段。

### B3｜字体（可选，质量优先时不动）

`tools/gen_fonts.sh`：CJK 走 `CJK_BPP=${FNOS_FONT_CJK_BPP:-2}`（已是 2bpp），Latin 是 4bpp。
如果要再省，只能往 1bpp 走 —— 会掉抗锯齿观感，**默认不做**，只在"密集清单行"上单独评估。

---

## 四、执行状态（每步独立验收，负向立即还原）

| # | 步骤 | 状态 | 实测 |
|---|---|---|---|
| 0 | 复现基线（同机同台架同脚本） | ✅ | `iv 26,976/26,873/26,783`、`rs2rr 22,378/22,248/22,201`、`rr2rs 4,600/4,637/4,565`、frames 120/119/116 |
| 1 | **A1** 绘制缓冲进片内 SRAM | ❌ 已还原 | 见 2.3 表：无收益 |
| 2 | **A2** LVGL 官方 PPA 绘制单元（含 `LV_DRAW_BUF_ALIGN=128` 前提） | ❌ 已还原 | 见 2.4 表：每帧 +1.2~2.1 ms |
| 3 | 官方 README 的 `buffer_height` 吞吐方向（120 → 300 行） | ❌ 已还原 | 报告第 21 条：**撕裂保护静默失效**（`TRIPLE_PARTIAL` 的 partial 缓冲走内部 RAM，240 KB 是本板天花板）⇒ 每帧 **+17 ms** |
| 4 | **B1** 无效区最小化 | 🟡 部分落地 | ① `clear_layers` 的**无条件标脏**已修（报告第 22 条，只省稳态）；②③⑤ 对 `iv/rs2rr` **无作用**——拖动期间 `ui_tick` 在 `motion_busy()` 处提前 return（`fnos_ui.c:4562`），刷新根本不在拖动帧里跑 ⇒ 不再投入 |
| 5 | 收口 | ✅ | `tools/verify_all.sh --flash` = **10 PASS / 0 FAIL**（含这两处保留改动）；报告第 21/22 条已写入 `docs/perf-report-2026-10-10.md` |

**官方文档这条线的结论（round 6 收口）**：能给的杠杆已经用尽——A1/A2 实测负（2.3/2.4）、官方
`buffer_height` 方向被内部 RAM 天花板堵死（报告第 21 条）、B1 里唯一真正白烧的一条已修但只影响稳态
（第 22 条）、B2 触摸本来就是中断模式且官方 `refresh_now` 改善不了 P1 的地板（4.6 ms 注入量化 +
8.35 ms 半帧）、B3 字体已是 2bpp 质量下限。剩下的 0.5~0.7 ms 落在"每帧绘制 ≈12 ms + 整帧 handoff
≈10 ms"这两块上，而这两块**不吃 UI/配置侧的优化**；要再跨线只能动厂商组件的刷屏主路径（用户未拍板）。

**测量噪声（重要）**：同一份固件重复跑同一脚本，`iv p50` 在本轮出现 26,867 ~ 28,029 µs 的散布
（±1.2 ms），`cpu_lvgl` 出现 12.6 ~ 17.5 的散布 ⇒ **小于 5% 的差异这台台架判不了**；比较必须同场次、
看最好值，并且不要把噪声当收益/回退。P2 的 30% 目标（iv ≤26.3 ms）恰好落在这个噪声带里。

生产状态已恢复：`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3`、`use_psram=true`、`LV_USE_PPA` 关闭、`LV_DRAW_BUF_ALIGN=4`，
重新 build+flash 后启动日志 `ui created (pages=6)` @5513 µs / `boot complete` @5542 µs（与 10 PASS 那次逐字同刻）。

## 五、期望值与边界（不吹）

- 30% 目标的算术：真基线 `rs2rr 31.0 ms → 目标 ≤21.7 ms`，现在 22.2~22.4 ms（差 0.5~0.7 ms）；
  `iv 37.6 ms → 目标 ≤26.3 ms`，现在 26.8~27.0 ms（差 0.5~0.7 ms）。
- 这 0.5~0.7 ms 只能来自"每帧要处理的像素更少"：硬件层的两条（A1/A2）当天实测为负，
  所以剩下的希望全在 **B1（脏区更小）**：`rot` 1.7~1.9 ms 与 `prep` 1.2 ms 都随面积走，
  把每帧 130~154 kpx 压下来就是按比例省时间。
- **不承诺**：pipeline 每帧的整帧 handoff 与 VSYNC 等待（第 17/19/20 条）不吃 UI 优化，
  如果做完 B1 仍差一点点，那就是架构边界，届时如实报告，而不是继续堆负收益的改动。
