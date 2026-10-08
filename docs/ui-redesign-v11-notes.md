# v11 重设计 · 设计合同素材（从会话主档固化，供 ui-redesign-v11.md 引用）

## 目标与硬约束
- 用户请求（m00001 原文）："参考固态科 UI skill，重新设计本项目的所有界面，并结合使用 interface design skill，将这两个 skill 结合起来使用。先不要做任何烧录，只出渲染图让我确认效果。所有的界面都需要并保证是完全自适应的。"（"固态科"=cuktech-screen-ui skill）
- ① 本轮不烧录、只出渲染图供确认；② 完全自适应=数据自适应+分辨率自适应（m00026 问答确认）；③ 目标=p4-7b-fnos-monitor 五页仪表盘（总览/存储/网络/系统/温度）。

## 双 skill 语法（冲突时硬件红线 > 视觉修饰）
- cuktech-screen-ui：设计合同先行；拓扑先于数字；稳定主值；身份/运行/信任三层独立编码；多通道状态（色+填充/描边+文案）；近黑底局部高能；卡片=对象非任意分区；图表=状态纹理；硬件输出为准。参考令牌 --ck-bg #000000/--ck-panel #0E1116/--ck-text #F5F7FA/--ck-dim #8A93A3/--ck-blue #3FA9F5/--ck-orange #FF7A1A/--ck-red #FF4557；type-hero 0.25–0.38S、type-metric 0.10–0.16S；safe-edge 0.035–0.06S；禁渐变（RGB565 banding，用户曾嫌"花花绿绿"）。
- interface-design：意图先行；领域探索（盘位/状态灯/机箱前面板/风道/10GbE/阵列/机架；阳极氧化黑铝/丝印灰/LED 绿琥珀红）；签名=「机箱前面板」灯条+盘位导轨 LED 行；令牌产品世界命名（--silk-*/--led-*/--plate-*）；tabular-nums；全组件状态；动效 <300ms ease-out；拒绝默认（卡片墙/渐变、KPI 网格、环形仪表、纯字号层级）。

## tokens.css 实值（原 docs/archive/ui/mockup-v11/css/tokens.css；设备端实现在 ui_kit/uk_theme.h）
- 底面 --chassis #0A0C0F / --plate #14181D / --plate-lift #1B2027 / --slot #0E1216 / --etch rgba(255,255,255,.08)。
- 丝印 --silk-1 #F2F5F8 / --silk-2 #AEB8C4 / --silk-3 #8A94A1 / --silk-4 #5C6674（t4 仅 ≥20px 用）。
- LED --led-ok #35C77A / --led-warn #FFB020 / --led-crit #FF4558 / --led-idle #4A5260。
- 身份色 --id-cpu #4D9BF0 / --id-mem #37C4D6 / --id-temp #FF8A1F / --id-down #37C4D6 / --id-up #7C8CF5 / --id-store #58C7B0；--focus #4D9BF0。
- 字号 --t-hero 44rem/lh52、--t-metric 32/39、--t-sub 20/24、--t-body 16/22、--t-label 13/18、--t-meta 12/16。
- 间距 --s1..--s6=4/8/12/16/24/32rem；--head-h 56rem/--foot-h 40rem/--rail-w 100rem。
- 阈值（全项目唯一，旧三套已统一）：用量 warm 80/full 90；温度 warm 60/full 75。

## 行高=版面高度合同（设备字体 tools/fonts Inter-{Regular,Medium,SemiBold}.ttf + NotoSansSC-{Regular,Medium}.ttf）
- cjk_20→23 / cjk_16→19 / cjk_12→14 / num_44→52 / num_32→39 / num_20→24 / txt_15→18 / txt_12→15。
- 值框+单位框定宽（120+64，间隙恒 4）；页边距 12；`kk_metrics_recompute(scr_w,scr_h)` 响应式几何（body_h=3×margin+2×gap+h3+ht+hm）。
- 对比度小字 ≥4.5:1：T3 #8A94A1 对 #14181D = 8.35:1 ok；T4 #737C87=3.77:1、品牌蓝 #006EFF=3.55:1 <22px 禁用。
- 密度=减行不缩字；放大=加留白不加卡；radius≤8；1rem=1u，u=min(100vw/1024,100vh/600)。

## 数据自适应规则
- 温度行形态：有 ch → 两行（l1=dn||dev，l2=ch）；无 ch → 一行；设备名永不缩写（只取 block model / pci.ids / cpuinfo）；行序永不随温度跳动（dev+ch 字典序=板端 fnos_data.c:148-153 行为）。
- `!online` 数值灰显不用危险色；stale=2–3× 预期间隔，标注"旧值·年龄"；!ever_ok→KPI "—"。

## 证据分级（交付必带）
- 静态 HTML/CSS 渲染图只证明设计，绝不称"硬件已验证"；LVGL/工具链/preview 流程留待实施阶段。
- 红线：ESP32-P4 rev v3.2；LVGL 9.5 对象默认 CLICKABLE；内部 RAM 361KB；CJK 必须 CJK 字形；lv_font_conv 需 --no-compress（RLE 竞态 LV_DRAW_SW_DRAW_UNIT_CNT>1）；主任务栈 16384；预览绿≠可交付。
