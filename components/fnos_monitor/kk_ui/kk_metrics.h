// 版面度量表（自适应版）—— v9 的"几何唯一来源"，从"编译期常量"升级为"按真实显示尺寸
// 推导出来的一张表"。
//
// 为什么要有这一层：v9 之前几何是手写常量，1024×600 上对得齐，一换屏（或数据条数一变）
// 就全错。这里把几何分成两类：
//   · **恒等式**：必须永远成立的关系（三块页 Σ块高 + 外边距 + 卡间隙 = 内容区高；
//     块高由内容反推）。它们是算出来的，不是填进去的。
//   · **比例**：间距阶梯与内边距随屏幕尺寸缩放，但必须落在阶梯上（4/8/12/16/24/32），
//     所以缩放后按 2px 取整 —— 半像素与奇数间隔在 169PPI 上就是"错位感"的来源。
//
// 用法：kk_metrics_recompute(宽, 高) 在**建任何构件之前**调一次（fnos_ui_create 里），
// 之后页面代码只读表，禁止再写字面量。数据条数变化引起的重排用 kk_layout_rows()
// 把卡片高度重新解一遍（自适应，而不是把行挤小或溢出卡片）。

#pragma once

#include <stdbool.h>

typedef struct {
    /* 屏幕与分区 */
    int scr_w, scr_h;              // 物理分辨率
    int rail_w, head_h;            // 左导航栏宽 / 页头高
    int body_w, body_h;            // 内容区 = scr - rail / head
    int margin, gap, pad_x, pad_t, pad_b, inline_gap;
    int half_gap;                  // 两块页"两块之间"的半档间隙（6）

    /* 内容块 */
    int card_w;                    // 整块宽（924 @1024）
    int col4, col2, col3, seg3;    // 四栏 / 两栏 / 三栏 / 三等分栏宽

    /* 三块页（总览 / 网络）：H3 页头摘要 · HT KPI 行 · HM 尾块 */
    int h3, ht, hm;
    /* 两块页（存储）：H2 总量三栏 · HLA 上块 · HLB 下块 */
    int h2, hla, hlb;

    /* 列表行 */
    int list_row;                  // 行高（由"卡内要装几行"解出来，并夹在 [44, 72]）
    int rows_raid, rows_disk, rows_vol;      // 下块要同时装下的两种行数（取大者解行高）
    int row_name_y, row_sub_y, row_sub_h, row_bar_y, row_bar_h, row_air_b, status_w;
    int h_note;              /* 系统页摘要条高度（固定 44：两行文字，不参与恒等式） */
    int list_row3, h3col;    /* 系统页三列卡：行高与卡高（7 行一张卡装得下） */
    /* 温度页的密集行：**两行**——第一行"设备名"（人读的，如 Marvell AQC113 10GbE），
       第二行"通道名 + 数值"。行高取版面合同的"列表行 44"，随屏幕缩放但夹在 [40,56]。
       不做进度条：数值本身带阈值色，条会把这 44px 挤成两条 8px 的碎字。 */
    int trow_h, trow_name_y, trow_sub_y;

    /* KPI 卡内部（由卡高反推，底部锚定） */
    int kpi_lbl_y, kpi_val_y, kpi_sub_y, kpi_sub_h, kpi_bar_y;
    /* 纯文本卡内部 */
    int txt_l1_y, txt_l2_y, txt_rule_y;
    /* 趋势卡（资源趋势 / 网络吞吐）内部：绘图区与读数行。
       实测 v9 的绘图区只占 152px，而读数行以下还有几十像素死区 —— 这组值就是
       "把死区还给曲线"的落点，按卡高现算（两张趋势卡高度不同）。 */
    int trend_plot_y;               // 绘图区顶（标题下沿起）
    int trend_axis_w;               // 左侧 Y 轴标签列宽（绘图区左边留白）
    int trend_readout_gap;          // 两条读数行的行框间距
    int card_title_h;              // 卡头行高（cjk_20 实测行高 23 → 24）
} kk_metrics_t;

extern kk_metrics_t KKM;           // 全局只读；由 kk_metrics_recompute() 填

/* 按显示尺寸重算全表。必须在建构件之前调用；重复调用是安全的（幂等）。 */
void kk_metrics_recompute(int scr_w, int scr_h);
/* 当前表是否可用（没调过 recompute 时为 false，页面里可用它兜底报错） */
bool kk_metrics_ready(void);

/* 纵向网格：块起点。总览/网络页用 kk_card_y(n)，存储页用 kk_card_y2a/y2b。 */
int kk_card_y(int n);
int kk_card_y2c(void);   /* 存储页右栏第一块的顶 */
int kk_card_y2d(void);   /* 存储页右栏第二块的顶 */
int kk_card_y2a(void);
int kk_card_y2b(void);

/* 自检：把整张表打到 stdout（参数变了拿它对账，别靠眼睛看） */
void kk_metrics_dump(void);

/* 趋势卡内部几何：按"这块卡多高"现算绘图区与读数行的位置。
   读数是贴卡底排的（主行在下不会随卡高漂移），绘图区吃掉中间的全部空间。 */
typedef struct {
    int plot_y, plot_h;             // 绘图区（折线画在这里）
    int axis_w;                     // 左侧 Y 轴标签列宽
    int readout_y, readout_gap;     // 第一条读数行的行框顶 / 两条读数行的间距
    int xaxis_y;                    // X 轴端点标签（较早 / 现在）行框顶；-1 = 这块卡放不下
} kk_trend_layout_t;
kk_trend_layout_t kk_trend_layout(int card_h);

/* 数据驱动的重排：卡内要装 n 行时，这块卡该多高（不足时给最小高，不硬压行高）。 */
int kk_card_h_for_rows(int n_rows);
