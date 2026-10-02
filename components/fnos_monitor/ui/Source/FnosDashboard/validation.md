# validation — FnosDashboard

交付台账。标记块由 `tools/ui_validate.py` 维护，标记块之外的手写笔记不会被覆盖。

<!-- ui-pipeline:validation-ledger:start -->

| 阶段 | 状态 |
| --- | --- |
| Validate | Pass |
| Generate | Pass |
| Verify | Pass |
| Runtime | Verified |

_pipeline: ui_gen/ui_validate（KK_UI_UMG LVGL 移植）· 2026-10-02 15:41_

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
