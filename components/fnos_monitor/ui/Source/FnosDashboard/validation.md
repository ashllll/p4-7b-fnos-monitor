# validation — FnosDashboard

交付台账。标记块由 `tools/ui_validate.py` 维护，标记块之外的手写笔记不会被覆盖。

<!-- ui-pipeline:validation-ledger:start -->

| 阶段 | 状态 |
| --- | --- |
| Validate | Pass |
| Generate | Pass |
| Verify | Pass |
| Runtime | Verified |

_pipeline: ui_gen/ui_validate（KK_UI_UMG LVGL 移植）· 2026-10-05 03:19_

<!-- ui-pipeline:validation-ledger:end -->

## 手写笔记

- v4 = KK_UI_UMG 设计语言全量移植（Manifest 源头 + MVVM-C + KK 视觉语法），取代 v3 UniFi 视觉体系。
- 几何与文案事实来源：`docs/ui-audit-v3.md`；编写合同：`docs/ui-kk-authoring.md`。
- 容量百分比数值统一白色（修 v3 评审遗留的"数值染色与条不一致"）；颜色只给条/状态。
- 指示条改 KK 强调蓝 `#2F80ED`（v3 是白色）；导航按下高亮取消（v3 实现里本来就没有还原逻辑）。

## 2026-09-30/10-01 v4.2 实机验收记录（烧录 + 手机拍照逐页判读）

- 三轮烧录复验，最终 7/7 PASS：界面活性（轮询 +13/帧、时钟推进）、四页轮播覆盖、存储行 `总量 X · 余 Y`
  完整带单位（余量和 12.56 TB ≈ 页首可用 12.5 TB）、温度卡 7 条无空槽、容器卡无空行、
  总览图表 CPU/MEM 双折线可分辨 + 温度面积纹理、存储刻度 59.9%/85.0%、顶栏/底栏文案完整。
- 实机暴露并修复 3 个真问题：
  1. 存储行"余 36"丢末位＝**Store 字节预算截断**（`StVolUse*` maxLen 24 装不下 27 字节 UTF-8）→ 48；
  2. 顶栏"采集"/底栏"前"豆腐块＝字体类型/子集缺口（`font.meta` 无汉字 + cjk 子集缺"前"）→ cjkMeta + `gen_fonts.sh`；
  3. UI 冻结 61 秒＝`probe_cb` 无判空解引用 `ov_cpu.bar`（v4.2.2 把 areaOpacity=0 的趋势改成只建折线层）→
     assert handler 死循环、task_wdt 每 5s dump 同一 PC；判空后恢复。
- LVGL chart 的 bar 绘制把 `bg_opa` 写死 COVER：`areaOpacity` 之前从未生效，现改为 `lv_color_mix` 混色模拟。

## 2026-10-02 交付复核轮（代码复审 + 字体审计工具 + 关掉轮播）

- 交付前逐文件复审（数据层 / 控制层 / 控件层 / 生成器），修掉 3 个真问题：遗留调试探针 `probe_cb`
  （v4.2.2 冻结事故的根因代码仍在交付固件里跑）、字体子集扫描清单漏 `bindings.json`、
  9 个僵尸字库（≈1 MB，不在构建清单里却被 `fnos_fonts.h` 声明）。
- 新增 `tools/audit_fonts.py`：豆腐块 / RLE / 僵尸字库 / 字段缓冲预算六项静态审计（可独立跑）。
  它本轮也修掉了自身三处"静默失效"（只留字面量导致 `SETS` 正则失效、`SETS_F?` 漏掉 `SETS(`、
  宏表二次切前缀得到空串）并加了"解析到 0 条即报错"的自检。
- 关轮播：`CONFIG_FNOS_AUTO_PAGE_SEC` 12 → 0，重建 + 烧录。串口 50 s：`VERIFY MODE` 0 次、
  无 task_wdt / 断言、`poll ok=30 fail=0 12ms`、内部 RAM 稳态 217 KB、PSRAM 27314 KB 无下降。
- 实机复拍两张相隔 36 s：时钟 15:39 → 15:40、CPU 2% → 8%、负载 1.27/0.62/0.49 → 2.06/0.90/0.58、
  曲线出现新峰（UI 活性）；页面仍停在总览、导航指示条不动（轮播已关）。

## 2026-10-02 v5 迭代验收记录（V5.0-A/B/C/D + V5.1 + V5.2，四页拍照）

- 交付固件：`CONFIG_FNOS_AUTO_PAGE_SEC=0`，`build/fnos_monitor.bin` 1823696 B（19:27），
  串口 `logs/v5-final-nocarousel.log` + 复位重启捕获 `logs/v5-final-boot.log`（142 行，`VERIFY MODE` 0 次）。
- **P0（交付固件两帧，相隔 38 s）**：三张 KPI 主值 44px（CPU 2 / 内存 64 / 温度 73）且单位贴基线；
  `运行时长 7d 20h` 保持 28px（112px 框装不下 44px 的它，故意不放大）；存储六行 `已用 … · 可用 …`
  弱色可读无截断；趋势图例三行各带峰值行（`峰 5% / 峰 65% / 峰 73°`）；底栏 `轮询 78 · 失败 0 · 9 ms · 数据 0s 前`。
  两帧同页同钟点而轮询计数在走 ⇒ **轮播关闭 + 界面活性正常**。
- **P1/P2/P3（临时轮播构建逐页取证）**：P1 `总容量 … · 余 …` 六行 + `阵列状态`/`硬盘活动` 新术语；
  P2 五块瓦片 `峰值下行 / 峰值上行 / 采集延迟 / 曲线采样 / 双向合计`，实测 `双向合计 157 KB/s`、
  `曲线采样 288`、`采集延迟 9 ms`、`峰值上行 180`，28px 数值无截断；P3 温度栏 7 条全满（无空槽/空轨道）、
  `固件内存 221 KB` 新术语、`无告警`。四页均无越界/截断/豆腐块/重叠。
- 取证手段补记：交付固件只能拍 P0，逐页验收必须**临时**把 `CONFIG_FNOS_AUTO_PAGE_SEC` 改 8（先备份原
  sdkconfig），验收后改回 0 重新构建 + 烧录 + 复验串口——临时构建与交付构建的差异仅此一项。
- 工具链：`tools/text_width.py --layout`（真实字宽，抓出 `NwTotalVal` 158px 框放不下 `118.00 MB/s` 162.3px）
  → `gen_fonts.sh`（cjk_13 补 硬/固/件）→ `ui_gen.py` → `ui_validate.py`（nodes 393 / fields 284 / bindings 288，
  lint 0 warning）→ `audit_fonts.py`（0/0）→ `idf.sh build`。

## 2026-10-05 v5.1 修复（用户报障：系统页温度卡"两条竖线一直在"）

- 报障原文："系统界面 温度模块有两条竖线bug 需要处理解决 一直在显示着"。
- 根因：P3 温度卡的 `SyTTick60`/`SyTTick75` 两个 `Divider` 是 **1×378**（x=172/183 本身没错，
  正是 76px 宽温度条的 60%/75% 档位），但 378px = 10 行行高之和 → 1px 灰线（Divider 默认 `#334052`）
  纵穿所有行底与条轨，且右对齐数值文字框卡内 x=148..228（`73.0°C` 实占 ~183..228）正好贴它，
  静止不动 ⇒ 读作渲染残迹而非刻度。分级语义本就由**数值文本 + 条的分级色**（`KK_BAR_WARM/FULL`）承担。
- 修法：删掉这两个节点（`/tmp/patch_ui_v51.py`：断言父节点 `SyTempPanel`、几何 [1,378]@[172,44]/[183,44]、
  无子节点、四类源文件无引用，再写盘；改动前 layout.json 备份 `/tmp/v51_backup/`）。`layout.json` −1456 B；
  生成物 `fnos_dash_view.generated.c` −18 行、`.h` −2 行；`ui_validate` nodes 393 → **391**，
  fields/bindings/lint 不变（0 warning）。容量页行内 1×18 刻度（`StVolTick60*/85*`）**保留**：被约束在行内，读作刻度。
- 实机复验（临时轮播 8 s 构建，拍完改回 0 重建交付）：P3 照片 `/tmp/v51p_21.png`（03:15，`轮询 118 · 失败 0 · 24 ms`）
  + 放大裁切 `/tmp/v51_temp_zoom.png` —— 温度栏 7 条（NIC 71.0°C、NVME2 41.9°C、NVME3 37.9°C、iGPU/CPU 31.0°C、
  NVME1 29.9°C、NVME0 27.9°C）条与数值干净，**无任何纵向穿行线**；对照修复前 `/tmp/v5p_5.png` 与
  `/tmp/p3_temp_zoom.png` 可见同位置灰线。其余元素（采集端点卡 12 行、容器 4/4、无告警）无回归。
- 交付态：改回 `CONFIG_FNOS_AUTO_PAGE_SEC=0` 重建 + 烧录，**交付固件 1823440 B（03:17）**；
  `logs/v51-final-nocarousel.log`（143 行）`[4.09] fnos_ui: ui created (pages=4)`、`VERIFY MODE` 0 次、
  无 watchdog/断言，`poll ok=30 fail=0 11ms cpu=1.6% mem=41.2%`、internal 225KB / psram 27304KB。
- 交付固件 P0 抽检 `/tmp/v51final_2.png`（03:19，停在总览页）：`系统状态 正常`、`运行时长 10d 03h`、
  三张 KPI 卡 `71 / 42 / 2`、存储六行 `已用 … · 可用 …`、趋势三行峰值 `峰 6% / 峰 43% / 峰 71°`、
  底栏 `轮询 53 · 失败 0 · 14 ms · 数据 0s 前` —— 无回归。
