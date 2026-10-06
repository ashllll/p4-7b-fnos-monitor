// 版面度量表的实现。全表在这里算出来，别处只读。
//
// 三条纪律（改这个文件前先读）：
//  1. **先列恒等式再解未知量**。三块页：`body_h = 2*margin + 2*gap + h3 + ht + hm`，
//     其中 h3/ht 是"内容要多少"，只有 hm 是剩余量 —— 所以解 hm，不要反过来。
//  2. **缩放后落到阶梯上**。间距阶梯 4/8/12/16/24/32，按屏幕尺寸缩放后按 2px 取整；
//     出现 7、13 这类值就是错位的来源（v9 之前 KPI 卡里数值与单位的间距 4~64px 都出现过）。
//  3. **行高由"卡内要装几行"解出来**，不是先定行高再塞内容。解出的行高夹在 [44, 72]：
//     低于 44 装不下"主行+副行+进度条"三样东西；高于 72 会出现大片空行。

#include "kk_metrics.h"
#include "kk_layout.h"
#include "kk_theme.h"
#include <stdio.h>

kk_metrics_t KKM;
static bool s_ready;

/* 落回阶梯：按 2px 取整并夹在 [lo, hi]。
   **只对"结果"取整，绝不对"比例/倍数"取整** —— 曾经写成 snap(960*width/948, ...)，
   结果比例先被取整成 2 再把基准乘了 2 倍，卡片高度直接翻倍。 */
static int snap(int v, int lo, int hi)
{
    v = (v + 1) & ~1;                    /* 偶数对齐 */
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

void kk_metrics_recompute(int scr_w, int scr_h)
{
        KKM.scr_w = scr_w;
    KKM.scr_h = scr_h;
    KKM.rail_w = KK_RAIL_W;
    KKM.head_h = KK_HEAD_H;

    /* 内容区：留出导航栏与页头，再留一圈外边距给卡片 */
    KKM.body_w = scr_w - KKM.rail_w;
    KKM.body_h = scr_h - KKM.head_h;

    /* 间距阶梯随屏宽缩放，但落回阶梯（1024 上是 12/16/12） */
    int scale = KKM.body_w;              /* 以内容区宽为基准 */
    KKM.margin    = snap(KK_MARGIN * scale / 948, 8, 24);
    KKM.gap       = KKM.margin;
    KKM.pad_x     = snap(KK_PAD    * scale / 948, 12, 32);
    KKM.pad_t     = KKM.pad_x;
    KKM.pad_b     = snap(KK_PAD_B  * scale / 948, 8, 24);
    KKM.inline_gap = 4;
    KKM.half_gap  = 6;                   /* 半档：两块页两道块之间用一次 */

    KKM.card_w = KKM.body_w - 2 * KKM.margin;
    KKM.col4 = (KKM.card_w - 3 * KKM.gap) / 4;
    KKM.col2 = (KKM.card_w - KKM.gap) / 2;
    KKM.col3 = (KKM.card_w - KKM.gap) / 2;
    KKM.seg3 = (KKM.card_w - 2 * KKM.pad_x) / 3;

    /* ── 三块页：h3 与 ht 由内容定，hm 吃掉剩下的（恒等式解出来）─────────
       h3：页头摘要一行状态 + 一行副信息（cjk_20 行高 23 与 cjk_16 行高 19，
           再加各自的上下留白）→ 23 + 19 + 12 + 10 = 64 @1024
       ht：KPI 卡 = 标签 19 + 值 52 + 副行 14 + 进度条 4 + 四道组间 4 + 上下内边距
           19+52+14+4 = 89，加 label→value 4、value→sub 4、sub→bar 4 = 101，
           加 pad_t 16 + pad_b 12 ⇒ 129；再加"值行留白"31 ⇒ 160（值要显眼就得留白） */
    /* ht = 160：KPI 卡（标签 + 值 + 副行 + 条）在这一屏的舒适高度。
       ht 与 h3、hm 由三条恒等式互相约束，改动任何一个都要回头验另外两个
       （1024×600 当前解：h3=44、ht=160、hm=280、hla/hlb=222）。 */
    KKM.ht = snap(160 * scale / 948, 120, 200);
    KKM.card_title_h = 24;
    KKM.h_note = 44;   /* 摘要条固定高：两行文字（19 + 3 + 20 + 上下 2），不随屏高变 */


    /* ── 两块页（存储）：3 列 2 行栅格 ────────────────────────────────
       版面： [已用/总容量/可用 三栏页头 64]
              [存储卷 924×?  ──────────────── ]   ← 占满整行
              [阵列健康 548×hla][磁盘活动 364×hlb]
       纵向： blocks 区 = body_h − 2×margin − h3 − gap = **444**
              hla + half_gap + hlb = 444
       横向： 528 ÷ 7 行解出行高
             hlb = chrome + rows_max×row   （下块 = 磁盘活动，装 rows_max 行）
             hla = 444 − half_gap − hlb    （上块 = 谁的恒等式的余量）
       chrome = 卡头 26 + 列表视图下沿到卡底 12 = 38。 */
    /* 页头卡高 44：三栏只需"标签 19 + 值 26（num_20 字形盒）"。**尾块高度必须在本行之后算** ——
       曾经在上面用旧的 h3=64 先算 hm，结果系统页摘要条长到 260 高、把三列卡压掉一半
       （audit_bounds 报 `card overlap`）。顺序错了不会报错，只会把版面画错。 */
    KKM.h3 = snap(44 * scale / 948, 40, 88);

    /* 三块页恒等式：body_h = margin + h3 + gap + ht + gap + hm + margin
       ⇒ hm = body_h − 3×margin − 2×gap − h3 − ht = 280（1024×600）。
       总览页的趋势卡、系统页的摘要条都用它。 */
    {
        int fixed3 = 3 * KKM.margin + 2 * KKM.gap + KKM.h3 + KKM.ht;
        KKM.hm = KKM.body_h - fixed3;
        if (KKM.hm < 160) KKM.hm = 160;             /* 极矮屏兜底：宁可溢出也不静默压扁 */
    }
    KKM.rows_raid = 3;                                 /* "阵列健康"要装 3 行 */
    KKM.rows_disk = 3;                                 /* "磁盘活动"装 3 行（更多滚动看；4 行卡会挤掉页头三栏） */
    KKM.rows_vol  = 3;                                 /* "存储卷"装 3 行 */

    int blocks_avail = KKM.body_h - 2 * KKM.margin - KKM.h3 - KKM.gap;
    int rows_max = KKM.rows_disk > KKM.rows_raid ? KKM.rows_disk : KKM.rows_raid;
    int nrows    = rows_max + KKM.rows_vol;
    int chrome   = KKM.card_title_h + 12;             /* 卡头 24 + 列表视图下沿到卡底 12 */
    /* 行高由"横向 528px 装 7 行"解出（下块 4 行 + 上块 3 行，两块共用一套行高）；
       解出 56 时行内三段的舒适布局正好成立（见下面的 air 分支）。
       行高的下限不是拍脑袋：行里固定塞着 行首 8 + 主行 19 + 组内 4 + 副行 14 + 2 + 条 4 + 行尾 8 = 59。 */
    KKM.row_bar_h = 4;
    /* row_cost 只用于报告"舒适行高"；**卡高不按它算**，见下。 */
    int row_cost = 8 + 19 + 4 + 14 + 2 + KKM.row_bar_h + 8;
    (void)row_cost;
    /* 卡高必须让"列表视图正好装下 n 行"：卡高 = 列表顶(48) + n×row + 卡内下留白(12)。
       曾经用 row = (blocks_avail − 2×chrome) / n 反解，结果列表视图比 3 行矮 2px，
       第 3 行永远显示不全（几何审计看不出来：行没越出列表，只是被裁）。 */
    KKM.list_row = 54;                                /* 1024×600：3 行卡 222、4 行卡 276 */
    KKM.hla = 48 + KKM.rows_vol * KKM.list_row + 12;
    KKM.hlb = 48 + rows_max     * KKM.list_row + 12;
    if (KKM.hla + KKM.half_gap + KKM.hlb > blocks_avail) {   /* 装不下先缩下块的行数 */
        KKM.hlb = blocks_avail - KKM.half_gap - KKM.hla;
    }
    if (getenv("KK_METRICS_TRACE"))
        fprintf(stderr, "[kkmdbg] blocks=%d chrome=%d row=%d vol=%d max=%d hla=%d hlb=%d\n",
                blocks_avail, chrome, KKM.list_row, KKM.rows_vol, rows_max, KKM.hla, KKM.hlb);

    /* ── 系统页（三列卡）：一张卡要装 7 行（容器服务 / 硬件温度都可能有 8 行，
       多出来的滚动看）。卡高与行高同样是"列表视图正好装下 n 行"的解：
         卡高 = 列表顶(48) + n×row + 卡底留白(12)
         行高 = (可用高 − 卡头 24 − 列表顶 48) / n   —— 让卡自己吃满页面高度 */
    {
        int col_avail = KKM.body_h - KKM.margin - KKM.h_note - KKM.gap;   /* 三列卡能吃的高度 */
        int nrow3 = 7;                             /* 一屏装 7 行：容器/温度常见的条数；第 8 条起滚动 */
        /* chrome 与两块页同理：卡头 24 + 列表顶内边距 24（三列卡是"主行 + 副行"两段，
           行首 8 比存储页的三段行更宽松）= 48；卡底再留 12。
           **先解行高、再由行高反推卡高**，否则 7×61 会把第 7 行挤到卡外。 */
        int lr = (col_avail - 48 - 12) / nrow3;
        if (lr > 64) lr = 64;
        if (lr < 44) lr = 44;                     /* 三列卡是"主行 + 副行"，44 是下限 */
        KKM.list_row3 = lr;
        KKM.h3col = 48 + nrow3 * lr + 12;
        if (KKM.h3col > col_avail) KKM.h3col = col_avail;
    }

    /* ── 列表行内部：三段固定在行顶与行底 ─────────────────────────────
       主行 19 行高、副行 18 框高、进度条 4 高；条子贴行底（下留 pad_b）。 */
    /* 三段在行内的纵向排布：行高够（≥59）就用舒适值，不够就按比例压留白。
       主行 19、副行 14 是字形高度，不动；压缩只吃掉"行首/组内/行尾"这些空气。 */
    KKM.row_sub_h = 14;                                       /* 副行只占字高 */
    int air = KKM.list_row - (19 + KKM.row_sub_h + 2 + KKM.row_bar_h);  /* 可分配的空气 */
    if (air >= 22) {                                          /* 舒适：8 / 4 / 8 */
        KKM.row_name_y = 8;  KKM.row_sub_y = 8 + 19 + 4;      KKM.row_air_b = 8;
    } else if (air >= 14) {                                   /* 常见：6 / 3 / 5 */
        KKM.row_name_y = 6;  KKM.row_sub_y = 6 + 19 + 3;      KKM.row_air_b = 5;
    } else {                                                  /* 紧凑：4 / 3 / 2 */
        KKM.row_name_y = 4;  KKM.row_sub_y = 4 + 19 + 3;      KKM.row_air_b = 2;
    }
    KKM.row_bar_y = KKM.row_sub_y + KKM.row_sub_h + 2;        /* 条子紧跟副行 */
    KKM.status_w   = snap(64 * scale / 948, 48, 96);

    /* ── 温度页密集行：主行"设备名"（占满列宽），副行"通道名 + 数值" ──────
       主行 19px（cjk_16）+ 副行 19px（数值 cjk_16 / 通道 cjk_12），上下各留 2px：
         2 + 19 + 2 + 19 + 2 = 44 = 合同的"列表行 44"。
       **设备名独占一行**：它是这一页唯一要回答的问题（"是谁在热"），跟数值挤一行
       会被截成 "Samsung SSD 990 PRO …"，等于没写。数值退到副行右端 —— 但它仍与
       通道名同行，不会孤零零飘着；没有通道名的数据（老采集端）由 fnos_ui.c 把整行
       收成一行"名字 + 数值"，不会留下半行空白。 */
    KKM.trow_h      = snap(44 * scale / 948, 40, 56);
    KKM.trow_name_y = 2;                                      /* 主行：设备名（全列宽） */
    KKM.trow_sub_y  = KKM.trow_h - 19 - 2;                    /* 副行贴行底：通道名 + 数值 */

    /* ── KPI 卡：底部锚定（副行与进度条贴卡底），标签与值从顶部排 ──────
       这样卡高变化时"值→副行"之间多出来的空间自然落在值下方，
       不会把副行与进度条挤到卡外（v9 曾经把读数行排到卡外 12px）。 */
    KKM.kpi_lbl_y = 14;                                       /* 卡内上内边距 16 − 2 视觉补偿 */
    KKM.kpi_val_y = KKM.kpi_lbl_y + 19 + 3;                   /* 标签行高 19 + 组内 3 */
    KKM.kpi_sub_h = 18;
    KKM.kpi_bar_y = KKM.ht - 16;                              /* KPI 卡下内边距 16（比列表卡大 4） */
    KKM.kpi_sub_y = KKM.kpi_bar_y - KKM.kpi_sub_h - 4;
    if (KKM.kpi_sub_y < KKM.kpi_val_y + 52)                   /* 值行 52 高，不能压 */
        KKM.kpi_sub_y = KKM.kpi_val_y + 52;

    /* ── 趋势卡内部：见文件末尾 kk_trend_layout() ───────────────────── */
    KKM.trend_plot_y = 44;                                    /* 卡头 36 + 8 */
    KKM.trend_axis_w = 48;                                    /* Y 轴标签 40 + 8 间隙 */
    KKM.trend_readout_gap = 24;                               /* 两条读数行框顶之差 */

    /* ── 纯文本卡 ─────────────────────────────────────────────────── */
    KKM.txt_l1_y   = 8;
    KKM.txt_l2_y   = KKM.txt_l1_y + 23 + 13;
    KKM.txt_rule_y = KKM.h3 - KKM.pad_b - 1;

    s_ready = true;
}

bool kk_metrics_ready(void) { return s_ready; }

int kk_card_y(int n)
{
    switch (n) {
    case 0:  return KKM.margin;
    case 1:  return KKM.margin + KKM.h3 + KKM.gap;
    default: return KKM.margin + KKM.h3 + KKM.gap + KKM.ht + KKM.gap;
    }
}

int kk_card_y2a(void) { return KKM.margin + KKM.h2 + KKM.gap; }
/* 存储页：左栏"存储卷"卡从栅格第一行起；右栏从页头下沿起（两块叠放）。 */
int kk_card_y2c(void) { return KKM.margin + KKM.h3 + KKM.gap; }
int kk_card_y2d(void) { return kk_card_y2c() + KKM.hlb + KKM.half_gap; }

int kk_card_y2b(void) { return kk_card_y2a() + KKM.hla + KKM.half_gap; }

/* 自检：把整张表打出来。第 1024×600 屏上必须与 v9 手写常量**逐项相等**，
   否则"自适应"就已经悄悄改了版面（曾经 31 个宏一次性改成表，靠这个对齐）。 */
void kk_metrics_dump(void)
{
    fprintf(stdout,
            "[kkm] scr=%dx%d body=%dx%d margin=%d gap=%d pad=%d/%d/%d half=%d\n"
            "[kkm] card_w=%d col4=%d col2=%d seg3=%d\n"
            "[kkm] h3=%d ht=%d hm=%d h2=%d hla=%d hlb=%d title=%d\n"
            "[kkm] row=%d name_y=%d sub_y=%d sub_h=%d bar_y=%d bar_h=%d status_w=%d\n"
            "[kkm] kpi lbl_y=%d val_y=%d sub_y=%d sub_h=%d bar_y=%d\n"
            "[kkm] card_y=(%d,%d,%d) y2a=%d y2b=%d\n",
            KKM.scr_w, KKM.scr_h, KKM.body_w, KKM.body_h, KKM.margin, KKM.gap,
            KKM.pad_x, KKM.pad_t, KKM.pad_b, KKM.half_gap,
            KKM.card_w, KKM.col4, KKM.col2, KKM.seg3,
            KKM.h3, KKM.ht, KKM.hm, KKM.h2, KKM.hla, KKM.hlb, KKM.card_title_h,
            KKM.list_row, KKM.row_name_y, KKM.row_sub_y, KKM.row_sub_h,
            KKM.row_bar_y, KKM.row_bar_h, KKM.status_w,
            KKM.kpi_lbl_y, KKM.kpi_val_y, KKM.kpi_sub_y, KKM.kpi_sub_h, KKM.kpi_bar_y,
            kk_card_y(0), kk_card_y(1), kk_card_y(2), kk_card_y2a(), kk_card_y2b());
}

kk_trend_layout_t kk_trend_layout(int card_h)
{
    kk_trend_layout_t L;
    L.axis_w = KKM.trend_axis_w;
    L.readout_gap = KKM.trend_readout_gap;
    /* 读数行贴卡底：两条行各 24 高、行间 8，末尾留 pad_b */
    int ro_need = 2 * 24 + 8;
    L.readout_y = card_h - KKM.pad_b - ro_need;
    L.plot_y = KKM.trend_plot_y;
    /* 读数行贴卡底 ⇒ 绘图区只能吃到"读数行 − 12"为止；X 轴端点标签就挤在这 12px 里，
       压到读数行上（audit_text_overlap 抓到"现在 / 等待数据"）。所以：卡够高才排
       X 轴标签，排得下就把绘图区让出 22px 给它。 */
    L.xaxis_y = -1;
    L.plot_h = (L.readout_y - 12) - L.plot_y;
    if (L.plot_h >= 150) {
        L.xaxis_y = L.plot_y + L.plot_h - 16;
        L.plot_h -= 22;
    }
    if (L.plot_h < 64) L.plot_h = 64;                         /* 极矮卡兜底 */
    return L;
}

int kk_card_h_for_rows(int n_rows)
{
    int h = KKM.card_title_h + KKM.pad_b + n_rows * KKM.list_row + 4;
    return h;
}
