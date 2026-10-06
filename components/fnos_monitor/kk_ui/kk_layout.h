// 版面合同（v9）——所有页面只从这里取几何，禁止在页面里手写魔法数字。
//
// 为什么要有这个文件：v8 及以前每个页面的坐标都是手写的，结果同一行里"看起来该对齐"
// 的元素差 2~8px（最典型：KPI 卡里数值与单位的间距每张卡都不一样，4~64px 都有，
// 读起来像两个不相干的元素）。对齐不能靠人眼逐处校准，只能靠**唯一的几何来源**。
//
// 合同（改动前先读这一节）：
//   1. 间距阶梯只有 4/8/12/16/24/32；禁止 6/10/14/18 等自造值。
//   2. 页面外边距 12、卡间隙 12；内容区 948×544。
//   3. 纵向恒等式：544 = 12 + H1 + 12 + H2 + 12 + H3 + 12（三块页，ΣH=496）。
//   4. 卡片内边距：上 16、下 12、左右 16（KPI 与列表卡）；纯文本卡上 12 下 8。
//   5. 卡内只有两条轴：左轴 x=16（所有块），右轴 x=w-16（数值/状态列）。第三条轴禁止。
//   6. 同行内"值 + 单位"间距恒为 4px；表格数值列右对齐并固定小数位。
//   7. 分组靠间距：组内 4、组间 ≥12；有卡边框就不再画分隔线。
//   8. 行高 44（列表）/ 32（密集行）；行间 1px KK_LINE，末行不画。
//   9. 颜色只表状态，且必须带文字冗余；KK_T4 与品牌蓝不得用于 <20px 的文字。
//  10. 字体的"行高"才是版面的真实高度（见 kk_type.h 实测值），字号只是名义值。
#pragma once

#include "kk_theme.h"
#include "kk_metrics.h"

/* v10（自适应）：本文件现在只留**与屏幕尺寸无关的固定常量**（间距阶梯的基准值、
   KPI 内部的行高与组内间距、卡头行高）。凡是"随屏幕推导出来"的几何都改成读
   kk_metrics.h 的 KKM 表 —— 名字沿用 KK_*（调用点一处不动），但值来自
   kk_metrics_recompute()：换屏、改数据条数都能重解，而不再是一堆写死的数字。
   恒等式与推导过程在 kk_metrics.c 顶部。 */

// ── 栅格 ────────────────────────────────────────────────────────────────
#define KK_MARGIN      12   // 内容区外边距
#define KK_GAP         12   // 卡片间隙（同时是"组间"最小值）
#define KK_PAD         16   // 卡片左右内边距，也是卡内左轴
#define KK_PAD_T       16   // 卡片上内边距（KPI / 列表）
#define KK_PAD_B       12   // 卡片下内边距
#define KK_PAD_T_TEXT   8   // 纯文本卡（摘要、读数图）上内边距更紧，值在本卡是唯一主角
#define KK_PAD_B_TEXT   8
#define KK_INLINE       4   // 同行内元素间距（值↔单位、点名↔状态点）
#define KK_LIST_ROW    (KKM.list_row)   // 由"卡内要装几行"解出，夹在 [44,72]，见 kk_metrics.c
#define KK_LIST_ROW3   (KKM.list_row3)  // 系统页三列卡的行高
#define KK_CARD_H3COL  (KKM.h3col)      // 系统页三列卡的高度
                            // 用 60 而不是 Carbon 表的 44：本屏的行里是"主行 + 副行 + 进度条"
                            // 三样东西（44 装不下，会挤成 2px 贴边）。
#define KK_LIST_SUB    20   // 列表行内副行高
#define KK_CARD_TITLE  (KKM.card_title_h)

// ── 内容区与三块页的纵向分配（页头 56 / 导航 76 由 kk_theme.h 定义）────────
// 三块页的纵向算式（**先算总和再分高度，不要反着来**）：
//   可用 = 544 − 2×12（上下外边距）= 520；520 − 2×12（两道卡间隙）= 496 = H1 + H2 + H3
//   取 64 + 160 + 272 = 496 ✓；末块下沿 = 12+64+12+160+12+272 = 532 = 544 − 12 ✓
// 两块页（存储）：12 + 96 + 12 + HLA(230) + 6 + HLB(182) = 538… 
//   配平 = 12 + H2(96) + 12 + HLA(224) + 6 + HLB(182) = 532 = 544 − 12（下外边距）。
//   HLA/H… 的数字由**内容**反推：行高 60 ⇒ 下块要装 3 行 = 3×60 = 180，加卡头 36 + 内边距
//   8 ⇒ 182（再多 6px 就会出现"内容比卡片高 6px"的越界，audit_bounds 会抓）。
//   首块后留标准 12px；**两块之间用 6px**（6 = 半个阶梯，仅用于"两块本是同一类列表、
//   不该被一道大缝分开"这一处）；下块 176 = 卡头 36 + 3 行×44 = 168 刚好 + 8 余量。
#define KK_BODY_W  (KKM.body_w)
#define KK_BODY_H  (KKM.body_h)
#define KK_CARD_W  (KKM.card_w)
#define KK_CARD_H3 (KKM.h3)
#define KK_CARD_HT (KKM.ht)
#define KK_CARD_HM (KKM.hm)
#define KK_CARD_HB KK_CARD_HM                                  // 别名：页尾大块
#define KK_CARD_H2 (KKM.h2)
#define KK_CARD_HLA (KKM.hla)
#define KK_CARD_HLB (KKM.hlb)

// 列宽：924 = 4*C4 + 3*GAP = 2*C2 + GAP = 2*C3 + GAP
#define KK_COL4 (KKM.col4)
#define KK_COL2 (KKM.col2)
#define KK_COL3 (KKM.col3)
#define KK_COL3N (KKM.col3)
// 三栏卡（页头摘要/总量三栏）：924 内等分三栏
#define KK_SEG3  (KKM.seg3)

// 纵向网格：把"块起点"算出来，页面里只写 CARD_Y(n)
#define KK_CARD_Y(n) (KK_MARGIN + (n) * KK_GAP + \
                      ((n) == 0 ? 0 : (n) == 1 ? KK_CARD_H3 : (KK_CARD_H3 + KK_CARD_HT)))
// 两块页（存储）的块顶：12 / 120 / 338
/* 存储页栅格：第 1 行（三栏页头下方）的顶 */
#define KK_CARD_Y2  (KKM.margin + KKM.h3 + KKM.gap)
#define KK_CARD_Y2A (kk_card_y2a())
#define KK_CARD_Y2C (kk_card_y2c())
#define KK_CARD_Y2D (kk_card_y2d())
#define KK_CARD_Y2B (kk_card_y2b())

// ── KPI 卡内部（222×178，内高 154）──────────────────────────────────── ──
// 标签 23 → 值 52 → 副行 18 → 进度条 4，块间 12、组内 4
#define KK_KPI_LBL_Y   (KKM.kpi_lbl_y)
#define KK_KPI_VAL_Y   (KKM.kpi_val_y)
#define KK_KPI_SUB_Y   (KKM.kpi_sub_y)
#define KK_KPI_BAR_Y   (KKM.kpi_bar_y)

// ── 纯文本卡内部（页头摘要 / 读数图）────────────────────────────────────
#define KK_TXT_L1_Y     (KKM.txt_l1_y)
#define KK_TXT_L2_Y     (KKM.txt_l2_y)
#define KK_TXT_RULE_Y  (KKM.txt_rule_y)

// ── 列表卡内部（行高 44，主行 20px 字号 → 23 行高）──────────────────────
#define KK_ROW_NAME_Y  (KKM.row_name_y)
#define KK_ROW_SUB_Y   (KKM.row_sub_y)
#define KK_ROW_SUB_H   (KKM.row_sub_h)
#define KK_ROW_BAR_Y   (KKM.row_bar_y)
#define KK_ROW_BAR_H   (KKM.row_bar_h)
#define KK_STATUS_W    (KKM.status_w)
#define KK_DOT          8                       // 状态点直径

// ── 温度页的密集行（两行：设备名 / 通道名+数值；没有条）──────────────────
// 行高由度量表按屏幕推导（合同的"列表行 44"，夹在 [40,56]）。
// 为什么要两行：第一行要写**人读的设备名**（"Marvell AQC113 10GbE"），一行里再塞
// 通道名与数值必然被截断 —— 而用户看这一页就是为了认清"是哪台设备在热"。
#define KK_TROW_H       (KKM.trow_h)
#define KK_TROW_NAME_Y  (KKM.trow_name_y)       // 第一行：设备名（cjk_16）
#define KK_TROW_SUB_Y   (KKM.trow_sub_y)        // 第二行：通道名（cjk_12）+ 数值（cjk_16）
#define KK_TROW_VAL_W   72                      // 右对齐数值列宽（"74.2°C" 用 cjk_16 实测 ~58px）

// ── 左导航栏（rail）────────────────────────────────────────────────────
// 导航项从 4 增到 5（新增「温度」页）时，**项高与间距必须重解**：把第 5 项硬塞进
// 4 项的位置，末项会压在 432 的分隔线与 448 的配对入口上。
//   可用段 = logo 下沿(16+40=56) + 16 … 分隔线 432 − 16 = 72..416，共 344
//   5 项 × 60 + 4 × 8 = 332 ✓（末项下沿 404，离分隔线还有 28）
#define KK_NAV_Y0       72
#define KK_NAV_H        60
#define KK_NAV_PITCH    (KK_NAV_H + 8)          // 68
#define KK_NAV_ICON_Y   6                       // 24×24 图标
#define KK_NAV_LBL_Y    32                      // 标签行框（cjk_16 行高 19）
#define KK_NAV_MARK_Y   8                       // 选中态左沿蓝条
#define KK_NAV_MARK_H   44
#define KK_RAIL_SEP_Y   432                     // 导航 / 配对 的分组线
#define KK_RAIL_PAIR_Y  448                     // 配对入口（60 高）
#define KK_RAIL_FOOT_Y  538                     // 页脚分隔线
