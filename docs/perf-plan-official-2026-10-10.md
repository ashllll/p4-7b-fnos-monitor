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
2. 真正还没用、且官方明确支持的只有三条：
   - **A1** 把 LVGL 绘制缓冲从 PSRAM 挪进片内 SRAM（BSP 一个字段：`profile.use_psram = false`）。
   - **A2** 打开 LVGL 上游的 **Espressif PPA 绘制单元**（`CONFIG_LV_USE_PPA=y`）。
   - **B1** 按官方"成本 ∝ 数据块大小"的原理**压缩每帧的无效区**（UI 架构层的功夫，不是硬件开关）。
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

## 四、执行顺序（每步都有独立验收，任何一步为负立即还原）

1. **复现基线**（同机同台架同脚本）：确认 `iv p50 ≈26.9 ms`、`rs2rr p50 ≈22.3 ms`、`rr2rs ≈4.6 ms`、frames ≈120。
2. **A1**：`use_psram=false` + 片内余量日志 → build/flash → bench（三组）→ 与第 1 步逐项对比。
   - 正向（哪怕 0.3 ms）就保留；负向立即 `git checkout -- components/esp32_p4_wifi6_touch_lcd_7b/esp32_p4_wifi6_touch_lcd_7b.c`。
3. **B1**：先用现有计数器（`CONFIG_FNOS_UI_MOTION_STATS` + 适配器 flush 统计）量出"每帧脏区来自哪个页/哪个控件"，
   再按 1→2→3 的顺序做；每改一处跑一次 bench，避免混变量。
4. **A2**：`CONFIG_LV_USE_PPA=y`（并做 `DRAW_UNIT_CNT` 1 vs 2 的交叉 A/B）→ 主要看 `cpu_lvgl`，
   FPS 不变不算失败。
5. **收口**：`tools/verify_all.sh --flash` 必须 10 PASS / 0 FAIL；`capture-board` 拍一张确认朝向与触摸；
   把正向项与负向项都写进 `docs/perf-report-2026-10-10.md`（负结果同样要留，别让下一个人重踩）。

## 五、期望值与边界（不吹）

- 30% 目标的算术：真基线 `rs2rr 31.0 ms → 目标 ≤21.7 ms`，现在 22.2~22.4 ms（差 0.5~0.7 ms）；
  `iv 37.6 ms → 目标 ≤26.3 ms`，现在 26.8~27.0 ms（差 0.5~0.7 ms）。
- 这 0.5~0.7 ms 只能来自"每帧要处理的像素更少/更近"：A1（写目标进片内）+ B1（脏区更小）正对这两点。
- **不承诺**：pipeline 每帧的整帧 handoff 与 VSYNC 等待（第 17/19/20 条）不吃 UI 优化，
  如果做完 A1+B1 仍差一点点，那就是架构边界，届时如实报告，而不是继续堆负收益的改动。
