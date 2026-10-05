// fnos_ui.c - 界面骨架（v6）：只用 kk_ui 构件手写四页。
//
// 与 v5 的区别：JSON 清单、ui_gen.py 代码生成、MVVM-C（View/Store/Binder/Controller）
// 全部删除。本文件既建树也格式化文案，并且是**唯一的 LVGL 写入者**（线程红线不变）。
// 数据只从 fnos_data 取快照、网络状态只从 fnos_net 读缓存（不在这里发起任何请求）。
//
// 版面：左导航 76px + 顶栏 56px + 内容区 948×544；四页 = 总览 / 存储 / 网络 / 系统。
#include "fnos_ui.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

#include "fnos_config.h"
#include "fnos_data.h"
#include "fnos_fonts.h"
#include "fnos_net.h"
#include "kk_theme.h"
#include "kk_widgets.h"

static const char *TAG = "fnos_ui";

/* ── kk_ui 兼容垫片 ────────────────────────────────────────────────────
 * 本骨架用的是 kk_ui v6 的令牌命名（KK_S1 / KK_T1 / KK_BLUE / KK_RAIL_W 这一套）。v6 的 kk_theme.h 由另一
 * 会话在写，落地时间不固定；这里对**本文件用到的每个令牌**给一份 #ifndef 兜底，
 * 保证任何时候单看提交历史也能构建（一旦 kk_ui 侧定义齐全，这些分支自动失效）。
 * 只有颜色/尺寸常量，没有逻辑——真源头仍是 kk_ui/kk_theme.h。 */
#include "kk_theme.h"
#ifndef KK_S1
#define KK_S1      0x1C1E21   /* 卡片底 */
#endif
#ifndef KK_BLUE
#define KK_BLUE    0x006EFF
#endif
#ifndef KK_T1
#define KK_T1      0xF9FAFA
#endif
#ifndef KK_T2
#define KK_T2      0xDEE0E3
#endif
#ifndef KK_T3
#define KK_T3      0xB7BCC2
#endif
#ifndef KK_T4
#define KK_T4      0x737C87
#endif
#ifndef KK_OFF
#define KK_OFF     0x42474D
#endif
#ifndef KK_S_CPU
#define KK_S_CPU   0x4797FF
#endif
#ifndef KK_S_MEM
#define KK_S_MEM   0x63BDE3
#endif
#ifndef KK_S_UP
#define KK_S_UP    0xB47CDE
#endif
#ifndef KK_S_DOWN
#define KK_S_DOWN  0x63BDE3
#endif
#ifndef KK_RAIL_W
#define KK_RAIL_W  76
#endif
#ifndef KK_HEAD_H
#define KK_HEAD_H  56
#endif

/* ── 版面常量 ─────────────────────────────────────────────────────── */
#define UI_HIST        180                  /* 趋势窗口 180 × 500 ms ≈ 90 s */
#define UI_CONTENT_W   (KK_SCR_W - KK_RAIL_W)   /* 948 */
#define UI_CONTENT_H   (KK_SCR_H - KK_HEAD_H)   /* 544 */
#define UI_ROWS_VOL     6
#define UI_ROWS_RAID    6
#define UI_ROWS_DISK    8
#define UI_ROWS_TEMP   10
#define UI_ROWS_DOCK    8
#define UI_ROWS_ALERT   4
#define UI_AGENT_ROWS   7
#define UI_CARDS       24

/* ── 构件包装 ─────────────────────────────────────────────────────── */
typedef struct {
    lv_obj_t *name, *detail, *pct;
    kk_bar_t  bar;
    bool      has_bar;
    bool      full_detail;   /* true = 详情铺满整行（硬盘读写、容器状态），没有百分比位 */
} ui_row_t;

typedef struct {
    lv_obj_t *value, *unit, *sub;
    kk_bar_t  bar;
    bool      has_bar;
} ui_kpi_t;

typedef struct {
    lv_obj_t *value, *sub;
} ui_big_t;

static struct {
    lv_obj_t *screen;
    lv_obj_t *cards[UI_CARDS];
    int       ncards;
    lv_obj_t *page[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav_lbl[FNOS_UI_PAGE_COUNT];
    /* 顶栏 */
    lv_obj_t *h_host, *h_ep, *h_chip, *h_bars[4];
    /* P0 总览 */
    ui_kpi_t  kpi[4];
    ui_row_t  vol0[UI_ROWS_VOL];
    kk_trend_t tr_cpu, tr_mem;
    lv_obj_t *tr_cpu_lbl, *tr_mem_lbl;
    /* P1 存储 */
    ui_row_t  vol1[UI_ROWS_VOL];
    ui_row_t  raid[UI_ROWS_RAID];
    ui_row_t  disk[UI_ROWS_DISK];
    lv_obj_t *zfs_lbl;
    /* P2 网络 */
    ui_big_t  big[4];
    kk_trend_t tr_rx, tr_tx;
    lv_obj_t *tr_rx_lbl, *tr_tx_lbl;
    ui_row_t  dock[UI_ROWS_DOCK];
    /* P3 系统 */
    ui_row_t  temp[UI_ROWS_TEMP];
    ui_row_t  agent[UI_AGENT_ROWS];
    ui_row_t  alert[UI_ROWS_ALERT];
    lv_obj_t *alert_none;
} s_ui;

static const char *const NAV_TXT[FNOS_UI_PAGE_COUNT] = { "总览", "存储", "网络", "系统" };
static const char *const AGENT_KEY[UI_AGENT_ROWS] = {
    "主机", "端点", "HTTP", "轮询", "最近错误", "数据年龄", "固件内存"
};

/* ── 运行时状态 ───────────────────────────────────────────────────── */
static bool          s_created;
static int           s_page;
static bool          s_night, s_night_req;
static fnos_status_t s_st;
static int64_t       s_seq;
static float         s_cpu_buf[UI_HIST], s_mem_buf[UI_HIST];
static float         s_rx_buf[UI_HIST],  s_tx_buf[UI_HIST];
static kk_series_t   s_cpu, s_mem, s_rx, s_tx;

/* ── 小工具 ───────────────────────────────────────────────────────── */
static void set_txt(lv_obj_t *l, const char *fmt, ...)
{
    if (!l) return;
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    lv_label_set_text(l, b);
}

static lv_obj_t *mk_label(lv_obj_t *parent, int x, int y, int w, int h,
                          const lv_font_t *f, uint32_t rgb, const char *txt)
{
    lv_obj_t *l = kk_label_create(parent, kk_rect(0, 0, 0, 0, x, y, w, h));
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    if (txt) lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *mk_right(lv_obj_t *parent, int x, int y, int w, int h,
                          const lv_font_t *f, uint32_t rgb)
{
    lv_obj_t *l = mk_label(parent, x, y, w, h, f, rgb, NULL);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    return l;
}

static lv_obj_t *mk_card(lv_obj_t *page, int x, int y, int w, int h, const char *title)
{
    lv_obj_t *c = kk_panel_create(page, kk_rect(0, 0, 0, 0, x, y, w, h));
    lv_obj_set_style_bg_color(c, lv_color_hex(KK_S1), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, KK_RADIUS, 0);
    if (s_ui.ncards < UI_CARDS) s_ui.cards[s_ui.ncards++] = c;
    if (title) mk_label(c, 16, 12, w - 32, 22, &ui_font_cjk_15, KK_T1, title);
    return c;
}

static lv_obj_t *mk_page(lv_obj_t *content)
{
    lv_obj_t *p = kk_panel_create(content, kk_rect(0, 0, 0, 0, 0, 0, UI_CONTENT_W, UI_CONTENT_H));
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static void mk_row(ui_row_t *r, lv_obj_t *parent, int x, int y, int w, int h,
                   bool with_bar, int bar_h)
{
    int name_h = with_bar ? (h - bar_h - 6) : h;
    r->name   = mk_label(parent, x, y, 130, name_h, &ui_font_cjk_13, KK_T2, NULL);
    r->detail = mk_label(parent, x + 130, y, w - 130 - 70, name_h, &ui_font_cjk_13, KK_T3, NULL);
    r->pct    = mk_right(parent, x + w - 70, y, 70, name_h, &ui_font_num_16, KK_T2);
    if (with_bar) {
        kk_bar_create(&r->bar, parent,
                      kk_rect(0, 0, 0, 0, x, y + h - bar_h, w, bar_h), kk_c(KK_OK), bar_h / 2);
        r->has_bar = true;
    }
}

/* 只显示"名字 + 右侧长文本"的行（硬盘读写、容器状态）：详情铺满剩余宽度并右对齐，
 * 百分比位永久隐藏（mk_row 默认给 detail 只留 w-200，长文案会被 LONG_DOT 截成 "写 1..."）。 */
static void row_full_detail(ui_row_t *r, int x, int w, int name_w)
{
    r->full_detail = true;
    if (r->name) lv_obj_set_width(r->name, name_w);
    if (r->detail) {
        lv_obj_set_x(r->detail, x + name_w);   /* 只挪 x/宽度；y 保持 mk_row 给的父相对值（get_y 是绝对坐标） */
        lv_obj_set_width(r->detail, w - name_w);
        lv_obj_set_style_text_align(r->detail, LV_TEXT_ALIGN_RIGHT, 0);
    }
    if (r->pct) lv_obj_add_flag(r->pct, LV_OBJ_FLAG_HIDDEN);
}

static void row_visible(ui_row_t *r, bool on)
{
    lv_obj_t *objs[3] = { r->name, r->detail, r->pct };
    for (int i = 0; i < 3; i++) {
        if (i == 2 && r->full_detail) continue;
        if (!objs[i]) continue;
        if (on) lv_obj_remove_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!r->has_bar) return;
    lv_obj_t *bars[2] = { r->bar.track, r->bar.fill };
    for (int i = 0; i < 2; i++) {
        if (!bars[i]) continue;
        if (on) lv_obj_remove_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void row_set(ui_row_t *r, const char *name, const char *detail, const char *pct, float bar_pct)
{
    set_txt(r->name, "%s", name ? name : "");
    set_txt(r->detail, "%s", detail ? detail : "");
    set_txt(r->pct, "%s", pct ? pct : "");
    if (r->has_bar) kk_bar_set(&r->bar, bar_pct);
}

/* 容量 / 速率 / 时长 / 数据年龄 */
static const char *fmt_cap(char *b, size_t n, float gb)
{
    if (gb >= 1024.0f)     snprintf(b, n, "%.1f TB", gb / 1024.0f);
    else if (gb >= 100.0f) snprintf(b, n, "%.0f GB", gb);
    else                   snprintf(b, n, "%.1f GB", gb);
    return b;
}

static const char *fmt_rate(char *b, size_t n, float kbs)
{
    if (kbs >= 1024.0f)    snprintf(b, n, "%.1f MB/s", kbs / 1024.0f);
    else if (kbs >= 10.0f) snprintf(b, n, "%.0f KB/s", kbs);
    else                   snprintf(b, n, "%.1f KB/s", kbs);
    return b;
}

static const char *fmt_uptime(char *b, size_t n, uint32_t s)
{
    uint32_t d = s / 86400u, h = (s % 86400u) / 3600u, m = (s % 3600u) / 60u;
    if (d)      snprintf(b, n, "%ud %02uh", (unsigned)d, (unsigned)h);
    else if (h) snprintf(b, n, "%uh %02um", (unsigned)h, (unsigned)m);
    else        snprintf(b, n, "%um", (unsigned)m);
    return b;
}

static const char *fmt_age(char *b, size_t n, int ms)
{
    if (ms < 1500)       snprintf(b, n, "刚刚");
    else if (ms < 60000) snprintf(b, n, "%d 秒前", ms / 1000);
    else                 snprintf(b, n, "%d 分前", ms / 60000);
    return b;
}

/* ── 建树 ─────────────────────────────────────────────────────────── */
static void build_rail(lv_obj_t *scr)
{
    lv_obj_t *rail = kk_panel_create(scr, kk_rect(0, 0, 0, 0, 0, 0, KK_RAIL_W, KK_SCR_H));
    lv_obj_set_style_bg_color(rail, lv_color_hex(KK_S1), 0);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rail, 0, 0);
    if (s_ui.ncards < UI_CARDS) s_ui.cards[s_ui.ncards++] = rail;

    lv_obj_t *logo = kk_panel_box(rail, 18, 14, 40, 40, KK_BLUE, KK_RADIUS);
    lv_obj_t *lt = mk_label(logo, 0, 10, 40, 22, &ui_font_txt_14, 0xFFFFFF, "FN");
    lv_obj_set_style_text_align(lt, LV_TEXT_ALIGN_CENTER, 0);

    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        lv_obj_t *b = kk_button_create(rail, kk_rect(0, 0, 0, 0, 8, 78 + i * 62, KK_RAIL_W - 16, 54));
        lv_obj_set_style_radius(b, KK_RADIUS, 0);
        s_ui.nav[i] = b;
        s_ui.nav_lbl[i] = mk_label(b, 0, 16, KK_RAIL_W - 16, 22, &ui_font_cjk_13, KK_T3, NAV_TXT[i]);
        lv_obj_set_style_text_align(s_ui.nav_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void build_header(lv_obj_t *scr)
{
    lv_obj_t *h = kk_panel_create(scr, kk_rect(0, 0, 0, 0, KK_RAIL_W, 0, UI_CONTENT_W, KK_HEAD_H));
    lv_obj_set_style_bg_opa(h, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(h, 0, 0);

    s_ui.h_host = mk_label(h, 16, 8, 360, 22, &ui_font_cjk_15, KK_T1, "fnos");
    s_ui.h_ep   = mk_label(h, 16, 30, 460, 18, &ui_font_cjk_13, KK_T4, "采集 -");
    kk_signal_bars(h, kk_rect(0, 0, 0, 0, UI_CONTENT_W - 16 - 104 - 44, 16, 36, 24), s_ui.h_bars);
    s_ui.h_chip = kk_chip_create(h, kk_rect(0, 0, 0, 0, UI_CONTENT_W - 16 - 104, 14, 104, 28),
                                 kk_c(KK_OFF));
    kk_chip_set(s_ui.h_chip, "等待数据", kk_c(KK_T4));
}

static void mk_kpi(ui_kpi_t *k, lv_obj_t *card, const char *title,
                   const lv_font_t *vf, bool with_bar, bool with_unit)
{
    mk_label(card, 16, 10, 196, 20, &ui_font_cjk_13, KK_T2, title);
    k->value = mk_label(card, 16, 30, 196, 52, vf, KK_T1, "-");
    k->unit  = with_unit ? mk_label(card, 112, 58, 80, 22, &ui_font_txt_14, KK_T3, "") : NULL;
    k->sub   = mk_label(card, 16, 78, 196, 18, &ui_font_cjk_13, KK_T4, "");
    if (with_bar) {
        kk_bar_create(&k->bar, card, kk_rect(0, 0, 0, 0, 16, 92, 196, 4), kk_c(KK_OK), 2);
        k->has_bar = true;
    }
}

static void build_p0(lv_obj_t *page)
{
    static const char *const KPI_TXT[4] = { "CPU", "内存", "最高温度", "运行时长" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = mk_card(page, 8 + i * 236, 8, 228, 104, NULL);
        /* 前三张是 44px 数字 + 单位 + 分级条；运行时长是混排文本，用 20px 中文字号 */
        mk_kpi(&s_ui.kpi[i], c, KPI_TXT[i],
               i < 3 ? &ui_font_num_44 : &ui_font_cjk_20, i < 3, i < 3);
    }

    lv_obj_t *vol = mk_card(page, 8, 120, 560, 416, "存储空间");
    for (int i = 0; i < UI_ROWS_VOL; i++) mk_row(&s_ui.vol0[i], vol, 16, 48 + i * 60, 528, 40, true, 4);

    lv_obj_t *tr = mk_card(page, 576, 120, 364, 416, "CPU / 内存 · 近 3 分钟");
    kk_trend_create(&s_ui.tr_cpu, tr, kk_rect(0, 0, 0, 0, 16, 48, 332, 248),
                    kk_c(KK_S_CPU), UI_HIST, 0, 3, 1);
    kk_trend_create(&s_ui.tr_mem, tr, kk_rect(0, 0, 0, 0, 16, 48, 332, 248),
                    kk_c(KK_S_MEM), UI_HIST, 0, 0, 1);
    mk_label(tr, 16, 312, 120, 20, &ui_font_cjk_13, KK_S_CPU, "CPU");
    s_ui.tr_cpu_lbl = mk_right(tr, 140, 312, 208, 20, &ui_font_cjk_13, KK_T2);
    mk_label(tr, 16, 344, 120, 20, &ui_font_cjk_13, KK_S_MEM, "内存");
    s_ui.tr_mem_lbl = mk_right(tr, 140, 344, 208, 20, &ui_font_cjk_13, KK_T2);
}

static void build_p1(lv_obj_t *page)
{
    lv_obj_t *vol = mk_card(page, 8, 8, 560, 528, "存储空间");
    for (int i = 0; i < UI_ROWS_VOL; i++) mk_row(&s_ui.vol1[i], vol, 16, 48 + i * 80, 528, 60, true, 8);

    lv_obj_t *raid = mk_card(page, 576, 8, 364, 300, "阵列状态");
    for (int i = 0; i < UI_ROWS_RAID; i++) {
        mk_row(&s_ui.raid[i], raid, 16, 48 + i * 34, 332, 26, false, 0);
        /* pct 位放的是"正常/降级"中文，必须用中文字库（num_16 只有 ASCII） */
        lv_obj_set_style_text_font(s_ui.raid[i].pct, &ui_font_cjk_13, 0);
    }
    s_ui.zfs_lbl = mk_label(raid, 16, 258, 332, 20, &ui_font_cjk_13, KK_T3, "");

    lv_obj_t *disk = mk_card(page, 576, 316, 364, 220, "硬盘活动");
    for (int i = 0; i < UI_ROWS_DISK; i++) {
        mk_row(&s_ui.disk[i], disk, 16, 46 + i * 21, 332, 18, false, 0);
        row_full_detail(&s_ui.disk[i], 16, 332, 90);   /* 盘名短（nvme0n1），把宽度让给"读 x · 写 y" */
    }
}

static void build_p2(lv_obj_t *page)
{
    static const char *const BIG_TXT[4] = { "上行", "下行", "双向合计", "采集延迟" };
    for (int i = 0; i < 4; i++) {
        int x = (i < 2) ? (8 + i * 236) : (480 + (i - 2) * 244);
        int w = (i < 2) ? 228 : 236;
        lv_obj_t *c = mk_card(page, x, 8, w, 104, NULL);
        mk_label(c, 16, 10, w - 32, 20, &ui_font_cjk_13, KK_T2, BIG_TXT[i]);
        s_ui.big[i].value = mk_label(c, 16, 32, w - 32, 40, &ui_font_num_30, KK_T1, "-");
        s_ui.big[i].sub   = mk_label(c, 16, 78, w - 32, 18, &ui_font_cjk_13, KK_T4, "");
    }

    lv_obj_t *tr = mk_card(page, 8, 120, 560, 416, "上行 / 下行 · 近 3 分钟");
    kk_trend_create(&s_ui.tr_tx, tr, kk_rect(0, 0, 0, 0, 16, 48, 528, 240),
                    kk_c(KK_S_UP), UI_HIST, 0, 3, 1);
    kk_trend_create(&s_ui.tr_rx, tr, kk_rect(0, 0, 0, 0, 16, 48, 528, 240),
                    kk_c(KK_S_DOWN), UI_HIST, 0, 0, 1);
    mk_label(tr, 16, 304, 120, 20, &ui_font_cjk_13, KK_S_UP, "上行");
    s_ui.tr_tx_lbl = mk_right(tr, 140, 304, 404, 20, &ui_font_cjk_13, KK_T2);
    mk_label(tr, 16, 336, 120, 20, &ui_font_cjk_13, KK_S_DOWN, "下行");
    s_ui.tr_rx_lbl = mk_right(tr, 140, 336, 404, 20, &ui_font_cjk_13, KK_T2);

    lv_obj_t *dock = mk_card(page, 576, 120, 364, 416, "容器");
    for (int i = 0; i < UI_ROWS_DOCK; i++) {
        mk_row(&s_ui.dock[i], dock, 16, 48 + i * 46, 332, 34, false, 0);
        row_full_detail(&s_ui.dock[i], 16, 332, 130);
    }
}

static void build_p3(lv_obj_t *page)
{
    lv_obj_t *temps = mk_card(page, 8, 8, 560, 528, "温度");
    for (int i = 0; i < UI_ROWS_TEMP; i++) {
        lv_obj_t *r = temps;
        int y = 48 + i * 46;
        s_ui.temp[i].name = mk_label(r, 16, y + 2, 96, 22, &ui_font_cjk_13, KK_T2, NULL);
        kk_bar_create(&s_ui.temp[i].bar, r, kk_rect(0, 0, 0, 0, 112, y + 8, 300, 8), kk_c(KK_OK), 4);
        s_ui.temp[i].has_bar = true;
        s_ui.temp[i].detail = mk_right(r, 420, y + 2, 124, 22, &ui_font_num_22, KK_T2);
        s_ui.temp[i].pct = NULL;
    }

    lv_obj_t *agent = mk_card(page, 576, 8, 364, 300, "采集端点");
    for (int i = 0; i < UI_AGENT_ROWS; i++) {
        mk_label(agent, 16, 48 + i * 34, 130, 22, &ui_font_cjk_13, KK_T4, AGENT_KEY[i]);
        s_ui.agent[i].name = NULL;
        s_ui.agent[i].pct = NULL;
        s_ui.agent[i].detail = mk_right(agent, 150, 48 + i * 34, 198, 22, &ui_font_cjk_13, KK_T2);
    }

    lv_obj_t *al = mk_card(page, 576, 316, 364, 220, "告警");
    for (int i = 0; i < UI_ROWS_ALERT; i++) {
        s_ui.alert[i].detail = mk_label(al, 16, 48 + i * 34, 332, 22, &ui_font_cjk_13, KK_WARN, NULL);
        s_ui.alert[i].name = NULL;
        s_ui.alert[i].pct = NULL;
    }
    s_ui.alert_none = mk_label(al, 16, 48, 332, 22, &ui_font_cjk_13, KK_OK, "无告警");
}

/* ── 换页 / 夜间 ──────────────────────────────────────────────────── */
static void nav_select(int idx)
{
    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        bool on = (i == idx);
        if (s_ui.nav[i]) {
            lv_obj_set_style_bg_color(s_ui.nav[i], lv_color_hex(on ? KK_BLUE : KK_S1), 0);
            lv_obj_set_style_bg_opa(s_ui.nav[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
        if (s_ui.nav_lbl[i]) {
            lv_obj_set_style_text_color(s_ui.nav_lbl[i], lv_color_hex(on ? 0xFFFFFF : KK_T3), 0);
        }
    }
}

static void nav_cb(lv_event_t *e)
{
    fnos_ui_set_page((int)(intptr_t)lv_event_get_user_data(e));
}

static void swipe_next(void) { fnos_ui_set_page(s_page + 1); }
static void swipe_prev(void) { fnos_ui_set_page(s_page - 1); }

static void night_apply(void)
{
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(s_night ? 0x000000 : KK_BG), 0);
    for (int i = 0; i < s_ui.ncards; i++) {
        if (!s_ui.cards[i]) continue;
        lv_obj_set_style_bg_color(s_ui.cards[i], lv_color_hex(s_night ? 0x0E1012 : KK_S1), 0);
    }
}

/* ── 刷屏 ─────────────────────────────────────────────────────────── */
static void refresh(void)
{
    const fnos_status_t *st = &s_st;
    char b[160], c1[32], c2[32];

    /* 顶栏：主机 / 端点 / 状态胶囊 / 信号 */
    set_txt(s_ui.h_host, "%s", st->host[0] ? st->host : "fnos");
    const char *ip = fnos_net_ip();
    set_txt(s_ui.h_ep, "采集 %s:%d · 本机 %s", FNOS_HOST, FNOS_PORT,
            (ip && ip[0]) ? ip : "-");
    if (!st->ever_ok)      kk_chip_set(s_ui.h_chip, "等待数据", kk_c(KK_T4));
    else if (st->online)   kk_chip_set(s_ui.h_chip, "在线", kk_c(KK_OK));
    else                   kk_chip_set(s_ui.h_chip, "离线", kk_c(KK_WARN));
    kk_signal_set(s_ui.h_bars, fnos_net_rssi());

    /* P0：四张 KPI + 存储 + 趋势 */
    ui_kpi_t *k = &s_ui.kpi[0];
    set_txt(k->value, "%.0f", st->cpu.pct);
    if (k->unit) set_txt(k->unit, "%%");
    set_txt(k->sub, "负载 %.2f/%.2f/%.2f", st->cpu.load1, st->cpu.load5, st->cpu.load15);
    if (k->has_bar) kk_bar_set(&k->bar, st->cpu.pct);

    k = &s_ui.kpi[1];
    set_txt(k->value, "%.0f", st->mem.pct);
    if (k->unit) set_txt(k->unit, "%%");
    set_txt(k->sub, "%.1f/%.1f GB · 余 %.1f", st->mem.used_mb / 1024.0f,
            st->mem.total_mb / 1024.0f, st->mem.avail_mb / 1024.0f);
    if (k->has_bar) kk_bar_set(&k->bar, st->mem.pct);

    k = &s_ui.kpi[2];
    set_txt(k->value, "%.0f", st->cpu.temp_c);
    if (k->unit) set_txt(k->unit, "C");
    set_txt(k->sub, "CPU %.0f°C · %d 核", st->cpu.temp_c, st->cpu.cores);
    if (k->has_bar) kk_bar_set(&k->bar, st->cpu.temp_c);

    k = &s_ui.kpi[3];
    set_txt(k->value, "%s", fmt_uptime(b, sizeof b, st->uptime_s));
    set_txt(k->sub, "进程 %d · runq %d", st->cpu.procs, st->cpu.runq);

    for (int i = 0; i < UI_ROWS_VOL; i++) {
        if (i >= st->nvols) { row_visible(&s_ui.vol0[i], false); continue; }
        const fnos_vol_t *v = &st->vols[i];
        row_visible(&s_ui.vol0[i], true);
        row_set(&s_ui.vol0[i], v->mnt,
                (snprintf(b, sizeof b, "已用 %s · 可用 %s",
                          fmt_cap(c1, sizeof c1, v->used_gb),
                          fmt_cap(c2, sizeof c2, v->free_gb)), b),
                (snprintf(c1, sizeof c1, "%.0f%%", v->pct), c1),
                v->pct);
    }

    kk_trend_sync_series(&s_ui.tr_cpu, &s_cpu);
    kk_trend_sync_series(&s_ui.tr_mem, &s_mem);
    set_txt(s_ui.tr_cpu_lbl, "%.0f%% · 峰 %d%%", st->cpu.pct, kk_trend_peak(&s_ui.tr_cpu));
    set_txt(s_ui.tr_mem_lbl, "%.0f%% · 峰 %d%%", st->mem.pct, kk_trend_peak(&s_ui.tr_mem));

    /* P1：卷 / 阵列 / 硬盘 */
    for (int i = 0; i < UI_ROWS_VOL; i++) {
        if (i >= st->nvols) { row_visible(&s_ui.vol1[i], false); continue; }
        const fnos_vol_t *v = &st->vols[i];
        row_visible(&s_ui.vol1[i], true);
        row_set(&s_ui.vol1[i], v->mnt,
                (snprintf(b, sizeof b, "已用 %s · 可用 %s",
                          fmt_cap(c1, sizeof c1, v->used_gb),
                          fmt_cap(c2, sizeof c2, v->free_gb)), b),
                (snprintf(c1, sizeof c1, "%.0f%%", v->pct), c1),
                v->pct);
    }
    for (int i = 0; i < UI_ROWS_RAID; i++) {
        if (i >= st->nraid) { row_visible(&s_ui.raid[i], false); continue; }
        const fnos_raid_t *r = &st->raid[i];
        row_visible(&s_ui.raid[i], true);
        row_set(&s_ui.raid[i], r->dev, r->lvl,
                r->ok ? "正常" : "降级", 0);
    }
    if (st->has_zfs) {
        set_txt(s_ui.zfs_lbl, "ZFS ARC %.1f GB · 命中 %.1f%%", st->zfs_arc_gb, st->zfs_hit_pct);
    } else {
        set_txt(s_ui.zfs_lbl, "");
    }
    for (int i = 0; i < UI_ROWS_DISK; i++) {
        if (i >= st->ndisks) { row_visible(&s_ui.disk[i], false); continue; }
        const fnos_disk_t *d = &st->disks[i];
        row_visible(&s_ui.disk[i], true);
        row_set(&s_ui.disk[i], d->dev,
                (snprintf(b, sizeof b, "读 %s · 写 %s",
                          fmt_rate(c1, sizeof c1, d->rd_kbs),
                          fmt_rate(c2, sizeof c2, d->wr_kbs)), b),
                NULL, 0);
    }

    /* P2：流量 + 容器 */
    set_txt(s_ui.big[0].value, "%s", fmt_rate(b, sizeof b, st->net.tx_kbs));
    set_txt(s_ui.big[0].sub, "累计 %s", fmt_cap(c1, sizeof c1, st->net.tx_total_gb));
    set_txt(s_ui.big[1].value, "%s", fmt_rate(b, sizeof b, st->net.rx_kbs));
    set_txt(s_ui.big[1].sub, "累计 %s", fmt_cap(c1, sizeof c1, st->net.rx_total_gb));
    set_txt(s_ui.big[2].value, "%s", fmt_rate(b, sizeof b, st->net.rx_kbs + st->net.tx_kbs));
    set_txt(s_ui.big[2].sub, "%s", st->net.ifname[0] ? st->net.ifname : "");
    set_txt(s_ui.big[3].value, "%d ms", st->http_ms);
    set_txt(s_ui.big[3].sub, "轮询 %u · 失败 %u", (unsigned)st->ok_count, (unsigned)st->fail_count);

    kk_trend_sync_series(&s_ui.tr_tx, &s_tx);
    kk_trend_sync_series(&s_ui.tr_rx, &s_rx);
    set_txt(s_ui.tr_tx_lbl, "%s · 峰 %d KB/s", fmt_rate(c1, sizeof c1, st->net.tx_kbs),
            kk_trend_peak(&s_ui.tr_tx));
    set_txt(s_ui.tr_rx_lbl, "%s · 峰 %d KB/s", fmt_rate(c1, sizeof c1, st->net.rx_kbs),
            kk_trend_peak(&s_ui.tr_rx));

    for (int i = 0; i < UI_ROWS_DOCK; i++) {
        if (i >= st->ndocker) { row_visible(&s_ui.dock[i], false); continue; }
        const fnos_docker_t *d = &st->docker[i];
        row_visible(&s_ui.dock[i], true);
        row_set(&s_ui.dock[i], d->n, d->up ? "运行中" : d->s, NULL, 0);
        if (s_ui.dock[i].detail) {
            lv_obj_set_style_text_align(s_ui.dock[i].detail, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_color(s_ui.dock[i].detail,
                                        lv_color_hex(d->up ? KK_OK : KK_T4), 0);
        }
    }

    /* P3：温度 / 采集端点 / 告警 */
    for (int i = 0; i < UI_ROWS_TEMP; i++) {
        if (i >= st->ntemps) { row_visible(&s_ui.temp[i], false); continue; }
        const fnos_temp_t *t = &st->temps[i];
        row_visible(&s_ui.temp[i], true);
        set_txt(s_ui.temp[i].name, "%s", t->n);
        set_txt(s_ui.temp[i].detail, "%.1f°C", t->c);
        kk_bar_set(&s_ui.temp[i].bar, t->c);
    }

    set_txt(s_ui.agent[0].detail, "%s", st->host[0] ? st->host : "-");
    set_txt(s_ui.agent[1].detail, "%s:%d", FNOS_HOST, FNOS_PORT);
    set_txt(s_ui.agent[2].detail, "%d ms (状态 %d)", st->http_ms, st->last_status);
    set_txt(s_ui.agent[3].detail, "%u / %u", (unsigned)st->ok_count, (unsigned)st->fail_count);
    set_txt(s_ui.agent[4].detail, "%s", st->last_err[0] ? st->last_err : "无");
    set_txt(s_ui.agent[5].detail, "%s", fmt_age(c1, sizeof c1, st->recv_ms));
    set_txt(s_ui.agent[6].detail, "%u KB",
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    if (st->nalerts == 0) {
        row_visible(&s_ui.alert[0], false);
        if (s_ui.alert_none) lv_obj_remove_flag(s_ui.alert_none, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s_ui.alert_none) lv_obj_add_flag(s_ui.alert_none, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < UI_ROWS_ALERT; i++) {
            if (i >= st->nalerts) {
                row_visible(&s_ui.alert[i], false);
                continue;
            }
            if (!s_ui.alert[i].detail) continue;
            row_visible(&s_ui.alert[i], true);
            /* agent 的告警级别是 "crit" / "warn" / "info"（见 nas/fnos-agent.py:_alerts） */
            uint32_t ac = (strcmp(st->alerts[i].lv, "crit") == 0) ? KK_DANGER
                        : (strcmp(st->alerts[i].lv, "warn") == 0) ? KK_WARN : KK_T3;
            lv_obj_set_style_text_color(s_ui.alert[i].detail, lv_color_hex(ac), 0);
            set_txt(s_ui.alert[i].detail, "%s", st->alerts[i].m);
        }
    }
}

/* ── tick / 生命周期 ──────────────────────────────────────────────── */
static void ui_tick(lv_timer_t *t)
{
    (void)t;
    if (s_night_req != s_night) {
        s_night = s_night_req;
        night_apply();
    }

    fnos_status_t st;
    if (fnos_data_get(&st)) s_st = st;

    fnos_sample_t smp[64];
    int64_t next = s_seq;
    int n = fnos_data_hist_read(s_seq, smp, 64, &next);
    for (int i = 0; i < n; i++) {
        kk_series_push(&s_cpu, smp[i].cpu);
        kk_series_push(&s_mem, smp[i].mem);
        kk_series_push(&s_rx,  smp[i].rx_kbs);
        kk_series_push(&s_tx,  smp[i].tx_kbs);
    }
    if (n > 0) s_seq = next;

    refresh();
}

static lv_obj_t *build_page(lv_obj_t *content, int idx)
{
    lv_obj_t *p = mk_page(content);
    switch (idx) {
    case 0: build_p0(p); break;
    case 1: build_p1(p); break;
    case 2: build_p2(p); break;
    default: build_p3(p); break;
    }
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

void fnos_ui_create(void)
{
    if (s_created) return;

    kk_series_init(&s_cpu, s_cpu_buf, UI_HIST);
    kk_series_init(&s_mem, s_mem_buf, UI_HIST);
    kk_series_init(&s_rx,  s_rx_buf,  UI_HIST);
    kk_series_init(&s_tx,  s_tx_buf,  UI_HIST);
    memset(&s_st, 0, sizeof s_st);

    lv_obj_t *scr = lv_screen_active();
    s_ui.screen = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(KK_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    build_rail(scr);
    build_header(scr);

    lv_obj_t *content = kk_panel_create(scr, kk_rect(0, 0, 0, 0, KK_RAIL_W, KK_HEAD_H,
                                                     UI_CONTENT_W, UI_CONTENT_H));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);

    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        s_ui.page[i] = build_page(content, i);
        kk_swipe_attach(s_ui.page[i], swipe_next, swipe_prev);
        if (s_ui.nav[i]) lv_obj_add_event_cb(s_ui.nav[i], nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    /* 顺序要紧：fnos_ui_set_page() 见到 !s_created 会直接返回，
     * 所以必须先置位再激活首页 —— 否则四页全被 HIDDEN，实机内容区一片黑
     * （预览逐页调用 set_page，看不到这个 bug；见 docs/verification.md §17.3）。 */
    s_created = true;
    s_page = -1;
    fnos_ui_set_page(0);
    night_apply();

    lv_timer_create(ui_tick, 500, NULL);
    ESP_LOGI(TAG, "ui created (pages=%d)", FNOS_UI_PAGE_COUNT);
}

void fnos_ui_set_page(int idx)
{
    if (!s_created) return;
    const int n = FNOS_UI_PAGE_COUNT;
    while (idx < 0) idx += n;
    while (idx >= n) idx -= n;
    if (idx == s_page) return;
    s_page = idx;

    // 原子换页：本板是"全屏页 + partial buffer"，双页位移动画会留残影，只做整页切换
    for (int i = 0; i < n; i++) {
        if (!s_ui.page[i]) continue;
        lv_obj_set_pos(s_ui.page[i], 0, 0);
        lv_obj_add_flag(s_ui.page[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.page[idx]) lv_obj_remove_flag(s_ui.page[idx], LV_OBJ_FLAG_HIDDEN);
    nav_select(idx);
    lv_obj_invalidate(lv_screen_active());
}

int fnos_ui_page(void)
{
    return s_page;
}

void fnos_ui_set_night(bool on)
{
    s_night_req = on;                            // 只置标志，落地在 ui_tick
}
