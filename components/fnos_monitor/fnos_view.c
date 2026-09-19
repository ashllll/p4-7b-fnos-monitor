// fnOS NAS 仪表盘：1024x600、4 页、触摸导航。
//
// 约定：
//  * 所有装饰对象一律清掉 CLICKABLE —— LVGL 9.5 里每个对象默认可点，装饰件会吞掉
//    点击且事件不冒泡；只留"导航项"和"每页内容容器"两个触摸目标：前者切页，
//    后者按 PRESSED/RELEASED 的横向位移翻页（本工程没开 LV_USE_GESTURE_RECOGNITION）。
//  * 本文件不建任务、不加锁，全部刷新都跑在 LVGL 任务里的一个定时器上。
//  * 界面文本全 ASCII：LVGL 内置 Montserrat 只有 ASCII 字形。
#include "fnos_view.h"
#include "fnos_data.h"
#include "fnos_net.h"
#if __has_include("fnos_config.h")
#include "fnos_config.h"
#else
#include "fnos_config.example.h"
#endif

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "fnos_view";

// ── 配色
#define BG       0x0E1114
#define PANEL    0x191C1F
#define PANEL2   0x14171A
#define TRACK    0x2A2F35
#define BORDER   0x2C3137
#define TEXT     0xEBEDF0
#define MUTED    0x9299A3
#define DIM      0x6A7078
#define BLUE     0x579AFF
#define PURPLE   0xAC8CEB
#define GREEN    0x73B880
#define AMBER    0xD9B56D
#define RED      0xE67E80
#define SEL      0x1C304D

// ── 版面
#define SCR_W    1024
#define SCR_H    600
#define HEADER_H 56
#define FOOTER_H 36
#define RAIL_W   132
#define NAV_TOP  (HEADER_H + 18)
#define NAV_H    92
#define NAV_STEP (NAV_H + 6)
#define CONT_X   (RAIL_W + 16)
#define CONT_W   (SCR_W - CONT_X - 16)
#define CONT_Y   (HEADER_H + 16)
#define CONT_H   (SCR_H - FOOTER_H - CONT_Y - 14)

// ── 字号
#define F_BIG    (&lv_font_montserrat_44)
#define F_NUM    (&lv_font_montserrat_32)
#define F_HEAD   (&lv_font_montserrat_24)
#define F_BODY   (&lv_font_montserrat_20)
#define F_NAV    (&lv_font_montserrat_16)
#define F_META   (&lv_font_montserrat_14)
#define F_TINY   (&lv_font_montserrat_12)

#define CHART_PTS CONFIG_FNOS_CHART_WINDOW
#define STALE_MS  6000

// STORAGE 页逐行显示的存储空间数量（标题行仍会汇总全部数量）
#define ST_VOL_ROWS 6

enum { PG_OVERVIEW = 0, PG_STORAGE, PG_NETWORK, PG_SYSTEM, PG_COUNT };
static const char *PAGE_NAME[PG_COUNT] = { "OVERVIEW", "STORAGE", "NETWORK", "SYSTEM" };

static bool  s_created;
static int   s_page;
static lv_obj_t *s_pages[PG_COUNT];
static lv_obj_t *s_navs[PG_COUNT];
static lv_obj_t *s_nav_bars[PG_COUNT];
static lv_obj_t *s_nav_lbls[PG_COUNT];
static int   s_press_x;

static EXT_RAM_BSS_ATTR fnos_status_t s_st;      // UI 侧快照
static int64_t s_hist_seq;

static lv_obj_t *s_hd_host, *s_hd_sub, *s_hd_pill, *s_hd_pill_lbl, *s_hd_clock;
static lv_obj_t *s_ft_left, *s_ft_mid, *s_ft_alert;

static lv_obj_t *s_ov_val[4], *s_ov_sub[4];
static lv_obj_t *s_ov_chart;
static lv_chart_series_t *s_ov_ser_cpu, *s_ov_ser_mem;
static lv_obj_t *s_ov_temp[5], *s_ov_temp_v[5];
static lv_obj_t *s_ov_vol[6], *s_ov_vol_v[6], *s_ov_vol_bar[6];
static int       s_ov_vol_w;

static lv_obj_t *s_st_total;
static lv_obj_t *s_st_mnt[FNOS_MAX_VOLS], *s_st_fs[FNOS_MAX_VOLS], *s_st_pct[FNOS_MAX_VOLS];
static lv_obj_t *s_st_bar[FNOS_MAX_VOLS], *s_st_use[FNOS_MAX_VOLS];
static int       s_st_bar_w;
static lv_obj_t *s_st_raid[FNOS_MAX_RAID], *s_st_raid_v[FNOS_MAX_RAID];
static lv_obj_t *s_st_zfs;
static lv_obj_t *s_st_raid_panel, *s_st_io_panel;
static lv_obj_t *s_st_io_dev[5], *s_st_io_val[5];
static int       s_st_last_nvols = -1;

static lv_obj_t *s_nw_rx, *s_nw_tx, *s_nw_rx_sub, *s_nw_tx_sub;
static lv_obj_t *s_nw_chart;
static lv_chart_series_t *s_nw_ser_rx, *s_nw_ser_tx;
static lv_obj_t *s_nw_axis[3], *s_nw_info;

static lv_obj_t *s_sy_dot[FNOS_MAX_DOCKER], *s_sy_dock[FNOS_MAX_DOCKER], *s_sy_dock_s[FNOS_MAX_DOCKER];
static lv_obj_t *s_sy_temp[8], *s_sy_temp_v[8];
static lv_obj_t *s_sy_kv[10];

// ───────────────────────────── 构件

static void set_text(lv_obj_t *l, const char *txt)
{
    if (l && txt) lv_label_set_text(l, txt);
}

static lv_obj_t *base(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius, bool clickable)
{
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) return NULL;
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN);
    if (!clickable) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = base(parent, x, y, w, h, PANEL, 8, false);
    if (o) {
        lv_obj_set_style_border_width(o, 1, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(BORDER), 0);
    }
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    if (!l) return NULL;
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *text_r(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t color, int right_pad, int y)
{
    lv_obj_t *l = text(parent, txt, font, color, 0, y);
    if (l) lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -right_pad, y);
    return l;
}

// 进度条：返回轨道，轨道内的填充条通过出参给出
static void hbar(lv_obj_t *parent, int x, int y, int w, int h, lv_obj_t **out_fill)
{
    base(parent, x, y, w, h, TRACK, h / 2, false);
    lv_obj_t *f = base(parent, x, y, 2, h, GREEN, h / 2, false);
    if (out_fill) *out_fill = f;
}

static void hbar_set(lv_obj_t *fill, int w, float pct, bool alert_high)
{
    if (!fill) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int wpx = (int)(w * pct / 100.0f + 0.5f);
    if (wpx < 2) wpx = 2;
    lv_obj_set_width(fill, wpx);
    uint32_t col = GREEN;
    if (alert_high) {
        if (pct >= 90) col = RED;
        else if (pct >= 80) col = AMBER;
        else if (pct >= 60) col = BLUE;
    }
    lv_obj_set_style_bg_color(fill, lv_color_hex(col), 0);
}

static uint32_t temp_color(float c)
{
    if (c >= 75) return RED;
    if (c >= 60) return AMBER;
    if (c >= 45) return BLUE;
    return GREEN;
}

// ── 文本格式化（全 ASCII）
static void fmt_rate(char *b, size_t n, float kbs)
{
    if (kbs >= 1024.0f)     snprintf(b, n, "%.2f MB/s", kbs / 1024.0f);
    else if (kbs >= 100.0f) snprintf(b, n, "%.0f KB/s", kbs);
    else                    snprintf(b, n, "%.1f KB/s", kbs);
}

static void fmt_cap(char *b, size_t n, float gb)
{
    if (gb >= 1024.0f) snprintf(b, n, "%.1f TB", gb / 1024.0f);
    else               snprintf(b, n, "%.0f GB", gb);
}

static void fmt_uptime(char *b, size_t n, uint32_t s)
{
    uint32_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if (d > 0)      snprintf(b, n, "%ud %02uh", (unsigned)d, (unsigned)h);
    else if (h > 0) snprintf(b, n, "%uh %02um", (unsigned)h, (unsigned)m);
    else            snprintf(b, n, "%um", (unsigned)m);
}

static int nice_max(int v)
{
    if (v <= 10) return 10;
    int scale = 1;
    while (v / scale >= 10) scale *= 10;
    int lead = v / scale;
    int nice = (lead <= 1) ? 1 : (lead <= 2) ? 2 : (lead <= 5) ? 5 : 10;
    return nice * scale;
}

// ───────────────────────────── 翻页 / 触摸

static void apply_page(void)
{
    for (int i = 0; i < PG_COUNT; i++) {
        if (s_pages[i]) {
            if (i == s_page) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            else             lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (s_navs[i]) {
            lv_obj_set_style_bg_color(s_navs[i], lv_color_hex(i == s_page ? SEL : PANEL2), 0);
        }
        if (s_nav_bars[i]) {
            lv_obj_set_style_bg_opa(s_nav_bars[i], i == s_page ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
        if (s_nav_lbls[i]) {
            lv_obj_set_style_text_color(s_nav_lbls[i], lv_color_hex(i == s_page ? TEXT : MUTED), 0);
        }
    }
}

void fnos_view_set_page(int idx)
{
    if (idx < 0) idx = PG_COUNT - 1;
    if (idx >= PG_COUNT) idx = 0;
    if (idx == s_page) return;
    s_page = idx;
    apply_page();
}

int fnos_view_page(void)
{
    return s_page;
}

static void on_nav_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    fnos_view_set_page(idx);
}

static void on_content_press(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = { 0, 0 };
    if (indev) {                    // LVGL 9 里 lv_indev_get_point 返回 void
        lv_indev_get_point(indev, &p);
        s_press_x = p.x;
    }
}

static void on_content_release(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p = { 0, 0 };
    lv_indev_get_point(indev, &p);
    int dx = p.x - s_press_x;
    if (dx <= -70)     fnos_view_set_page(s_page + 1);
    else if (dx >= 70) fnos_view_set_page(s_page - 1);
}

// ───────────────────────────── 构建

static void build_header(lv_obj_t *scr)
{
    base(scr, 0, 0, SCR_W, HEADER_H, PANEL, 0, false);
    base(scr, 0, HEADER_H - 1, SCR_W, 1, BORDER, 0, false);
    s_hd_host = text(scr, "NAS", F_HEAD, TEXT, 16, 12);
    s_hd_sub  = text(scr, "wifi connecting...", F_META, MUTED, 150, 22);
    s_hd_pill = base(scr, 470, 14, 140, 28, 0x3A1F22, 14, false);
    s_hd_pill_lbl = text(s_hd_pill, "OFFLINE", F_NAV, RED, 0, 0);
    if (s_hd_pill_lbl) lv_obj_center(s_hd_pill_lbl);
    s_hd_clock = text_r(scr, "--:--", F_HEAD, TEXT, 16, 12);
}

static void build_rail(lv_obj_t *scr)
{
    base(scr, 0, HEADER_H, RAIL_W, SCR_H - HEADER_H - FOOTER_H, PANEL2, 0, false);
    for (int i = 0; i < PG_COUNT; i++) {
        lv_obj_t *item = base(scr, 8, NAV_TOP + i * NAV_STEP, RAIL_W - 16, NAV_H, PANEL2, 8, true);
        s_navs[i] = item;
        s_nav_bars[i] = base(item, 0, 10, 3, NAV_H - 20, BLUE, 2, false);
        if (s_nav_bars[i]) lv_obj_set_style_bg_opa(s_nav_bars[i], LV_OPA_TRANSP, 0);
        s_nav_lbls[i] = text(item, PAGE_NAME[i], F_NAV, MUTED, 14, 22);
        const char *sub = (i == 0) ? "cpu mem temp" : (i == 1) ? "volumes raid" :
                          (i == 2) ? "throughput" : "docker temps";
        text(item, sub, F_TINY, DIM, 14, 50);
        lv_obj_add_event_cb(item, on_nav_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void build_footer(lv_obj_t *scr)
{
    base(scr, 0, SCR_H - FOOTER_H, SCR_W, FOOTER_H, PANEL, 0, false);
    base(scr, 0, SCR_H - FOOTER_H, SCR_W, 1, BORDER, 0, false);
    s_ft_left  = text(scr, "poll --", F_TINY, MUTED, 16, 11);
    s_ft_mid   = text(scr, "no data", F_TINY, DIM, 400, 11);
    s_ft_alert = text_r(scr, "", F_TINY, GREEN, 16, 11);
}

static lv_obj_t *make_page(lv_obj_t *scr)
{
    lv_obj_t *p = base(scr, CONT_X, CONT_Y, CONT_W, CONT_H, BG, 0, true);
    if (p) {
        lv_obj_add_event_cb(p, on_content_press, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(p, on_content_release, LV_EVENT_RELEASED, NULL);
    }
    return p;
}

static void build_overview(lv_obj_t *p)
{
    static const char *title[4] = { "CPU", "MEMORY", "HOTTEST", "UPTIME" };
    int tw = (CONT_W - 3 * 16) / 4;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *t = panel(p, i * (tw + 16), 0, tw, 132);
        text(t, title[i], F_META, MUTED, 16, 12);
        s_ov_val[i] = text(t, "--", F_BIG, TEXT, 16, 38);
        s_ov_sub[i] = text(t, "", F_META, DIM, 16, 100);
    }
    lv_obj_t *cp = panel(p, 0, 148, 576, 200);
    text(cp, "CPU / MEM  %", F_META, MUTED, 16, 10);
    s_ov_chart = lv_chart_create(cp);
    if (s_ov_chart) {
        lv_obj_set_pos(s_ov_chart, 12, 34);
        lv_obj_set_size(s_ov_chart, 552, 154);
        lv_chart_set_type(s_ov_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(s_ov_chart, CHART_PTS);
        lv_chart_set_range(s_ov_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_div_line_count(s_ov_chart, 4, 6);
        lv_obj_set_style_bg_opa(s_ov_chart, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_ov_chart, 0, LV_PART_MAIN);
        lv_obj_set_style_line_color(s_ov_chart, lv_color_hex(BORDER), LV_PART_MAIN);
        lv_obj_set_style_line_width(s_ov_chart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(s_ov_chart, 0, 0, LV_PART_INDICATOR);
        lv_obj_clear_flag(s_ov_chart, LV_OBJ_FLAG_CLICKABLE);
        s_ov_ser_cpu = lv_chart_add_series(s_ov_chart, lv_color_hex(BLUE), LV_CHART_AXIS_PRIMARY_Y);
        s_ov_ser_mem = lv_chart_add_series(s_ov_chart, lv_color_hex(PURPLE), LV_CHART_AXIS_PRIMARY_Y);
    }
    lv_obj_t *tp = panel(p, 592, 148, CONT_W - 592, 200);
    text(tp, "TEMPERATURES", F_META, MUTED, 16, 10);
    for (int i = 0; i < 5; i++) {
        int y = 40 + i * 30;
        s_ov_temp[i] = text(tp, "", F_BODY, MUTED, 16, y);
        s_ov_temp_v[i] = text_r(tp, "", F_BODY, TEXT, 16, y);
    }
    int sp_h = CONT_H - 364;
    lv_obj_t *sp = panel(p, 0, 364, CONT_W, sp_h);
    text(sp, "VOLUMES", F_META, MUTED, 16, 10);
    s_ov_vol_w = (CONT_W - 32 - 5 * 10) / 6;
    for (int i = 0; i < 6; i++) {
        int x = 16 + i * (s_ov_vol_w + 10);
        s_ov_vol[i] = text(sp, "--", F_NAV, MUTED, x, 40);
        s_ov_vol_v[i] = text(sp, "--", F_NUM, TEXT, x, 56);
        hbar(sp, x, sp_h - 22, s_ov_vol_w, 8, &s_ov_vol_bar[i]);
    }
}

static void build_storage(lv_obj_t *p)
{
    text(p, "STORAGE", F_HEAD, TEXT, 0, 0);
    s_st_total = text(p, "", F_BODY, MUTED, 150, 6);

    // 行高 44（原来 52）+ 固定 6 行：6 个存储空间时下半区还剩 160 px，
    // RAID 4 行 + 磁盘 5 行都放得下（8 行会把 RAID/磁盘区挤出屏幕）
    const int y0 = 46, row = 44;
    s_st_bar_w = 380;
    for (int i = 0; i < ST_VOL_ROWS; i++) {
        int y = y0 + i * row;
        s_st_mnt[i] = text(p, "", F_BODY, TEXT, 0, y);
        s_st_fs[i]  = text(p, "", F_TINY, DIM, 120, y + 7);
        hbar(p, 200, y + 5, s_st_bar_w, 18, &s_st_bar[i]);
        s_st_pct[i] = text(p, "", F_BODY, TEXT, 592, y);
        s_st_use[i] = text_r(p, "", F_META, MUTED, 8, y + 5);
    }
    // 下半区：RAID 阵列 + 磁盘活动。位置按"实际有几个存储空间"在 update_storage()
    // 里修正（首次拿到数据时才知道），这里先按 6 个预留，避免面板被压成一条缝。
    int ry = y0 + ST_VOL_ROWS * row + 8;
    s_st_raid_panel = panel(p, 0, ry, 560, CONT_H - ry);
    text(s_st_raid_panel, "RAID ARRAYS", F_META, MUTED, 16, 8);
    for (int i = 0; i < FNOS_MAX_RAID; i++) {
        int y = 32 + i * 21;
        s_st_raid[i] = text(s_st_raid_panel, "", F_META, MUTED, 16, y);
        s_st_raid_v[i] = text(s_st_raid_panel, "", F_META, TEXT, 200, y);
    }
    s_st_zfs = text_r(s_st_raid_panel, "", F_TINY, DIM, 16, 10);

    s_st_io_panel = panel(p, 576, ry, CONT_W - 576, CONT_H - ry);
    text(s_st_io_panel, "DISK ACTIVITY", F_META, MUTED, 16, 8);
    for (int i = 0; i < 5; i++) {
        int y = 32 + i * 21;
        s_st_io_dev[i] = text(s_st_io_panel, "", F_META, MUTED, 16, y);
        s_st_io_val[i] = text_r(s_st_io_panel, "", F_TINY, TEXT, 16, y + 3);
    }
}

static void build_network(lv_obj_t *p)
{
    int tw = (CONT_W - 16) / 2;
    lv_obj_t *a = panel(p, 0, 0, tw, 150);
    text(a, "DOWNLOAD", F_META, MUTED, 16, 12);
    s_nw_rx = text(a, "--", F_BIG, BLUE, 16, 40);
    s_nw_rx_sub = text(a, "", F_META, DIM, 16, 106);

    lv_obj_t *b = panel(p, tw + 16, 0, tw, 150);
    text(b, "UPLOAD", F_META, MUTED, 16, 12);
    s_nw_tx = text(b, "--", F_BIG, PURPLE, 16, 40);
    s_nw_tx_sub = text(b, "", F_META, DIM, 16, 106);

    int cp_h = CONT_H - 166 - 30;
    lv_obj_t *cp = panel(p, 0, 166, CONT_W, cp_h);
    text(cp, "THROUGHPUT  KB/s", F_META, MUTED, 16, 10);
    s_nw_chart = lv_chart_create(cp);
    if (s_nw_chart) {
        lv_obj_set_pos(s_nw_chart, 12, 34);
        lv_obj_set_size(s_nw_chart, CONT_W - 140, cp_h - 50);
        lv_chart_set_type(s_nw_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(s_nw_chart, CHART_PTS);
        lv_chart_set_range(s_nw_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
        lv_chart_set_div_line_count(s_nw_chart, 4, 6);
        lv_obj_set_style_bg_opa(s_nw_chart, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_nw_chart, 0, LV_PART_MAIN);
        lv_obj_set_style_line_color(s_nw_chart, lv_color_hex(BORDER), LV_PART_MAIN);
        lv_obj_set_style_line_width(s_nw_chart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(s_nw_chart, 0, 0, LV_PART_INDICATOR);
        lv_obj_clear_flag(s_nw_chart, LV_OBJ_FLAG_CLICKABLE);
        s_nw_ser_rx = lv_chart_add_series(s_nw_chart, lv_color_hex(BLUE), LV_CHART_AXIS_PRIMARY_Y);
        s_nw_ser_tx = lv_chart_add_series(s_nw_chart, lv_color_hex(PURPLE), LV_CHART_AXIS_PRIMARY_Y);
    }
    for (int i = 0; i < 3; i++) {
        s_nw_axis[i] = text(cp, "", F_TINY, DIM, CONT_W - 118, 44 + i * (cp_h - 100) / 2);
    }
    s_nw_info = text(p, "", F_META, MUTED, 0, CONT_H - 22);
}

static void build_system(lv_obj_t *p)
{
    int lw = 420, rw = CONT_W - lw - 16;
    lv_obj_t *a = panel(p, 0, 0, lw, CONT_H);
    text(a, "CONTAINERS", F_META, MUTED, 16, 10);
    for (int i = 0; i < FNOS_MAX_DOCKER; i++) {
        int y = 42 + i * 34;
        s_sy_dot[i] = base(a, 16, y + 5, 10, 10, DIM, 5, false);
        s_sy_dock[i] = text(a, "", F_BODY, TEXT, 36, y);
        s_sy_dock_s[i] = text_r(a, "", F_TINY, DIM, 16, y + 7);
    }

    // 温度 205 + 端点信息 (CONT_H-205=273)：原来温度占 264 时下面只剩 198，
    // 10 行端点信息放不下（LVGL 会把超出父对象的子对象裁掉，最后几行永远看不见）
    lv_obj_t *b = panel(p, lw + 16, 0, rw, 205);
    text(b, "TEMPERATURES", F_META, MUTED, 16, 10);
    for (int i = 0; i < 8; i++) {
        int col = i / 4, line = i % 4;
        int x = 16 + col * (rw / 2);
        int y = 40 + line * 30;
        s_sy_temp[i] = text(b, "", F_BODY, MUTED, x, y);
        s_sy_temp_v[i] = text(b, "", F_BODY, TEXT, x + 128, y);
    }

    lv_obj_t *c = panel(p, lw + 16, 205, rw, CONT_H - 205);
    text(c, "AGENT / LINK", F_META, MUTED, 16, 10);
    for (int i = 0; i < 10; i++) {
        // 行距 20：36+9*20+15 = 231 < 273
        s_sy_kv[i] = text(c, "", F_TINY, MUTED, 16, 36 + i * 20);
    }
}

// ───────────────────────────── 刷新

static void update_header(bool have)
{
    char b[96];
    const char *ip = fnos_net_ip();
    if (have) {
        set_text(s_hd_host, s_st.host[0] ? s_st.host : "NAS");
        snprintf(b, sizeof(b), "%s:%d  %s", FNOS_HOST, FNOS_PORT, ip[0] ? ip : "no ip");
    } else {
        set_text(s_hd_host, "NAS");
        snprintf(b, sizeof(b), "%s:%d  %s", FNOS_HOST, FNOS_PORT,
                 ip[0] ? "waiting for agent" : "wifi connecting...");
    }
    set_text(s_hd_sub, b);

    int64_t age_ms = s_st.recv_ms ? (esp_timer_get_time() / 1000 - s_st.recv_ms) : -1;
    // ONLINE：最近一次轮询成功且数据新鲜
    // OFFLINE：拿到过数据、但最近一次轮询失败（采集端/网络断了）——监控器上必须显眼
    // STALE  ：轮询没失败，但数据已经 6 秒没更新（卡在别处）
    // NO DATA：还从来没成功过（开机、Wi-Fi 没连上、地址填错）
    const char *pill;
    uint32_t bg, fg;
    if (have && s_st.online && age_ms >= 0 && age_ms < STALE_MS) {
        pill = "ONLINE"; bg = 0x1B3A28; fg = GREEN;
    } else if (have && !s_st.online) {
        pill = "OFFLINE"; bg = 0x3A1F22; fg = RED;
    } else if (have) {
        pill = "STALE"; bg = 0x3A2F1B; fg = AMBER;
    } else {
        pill = "NO DATA"; bg = 0x3A2F1B; fg = AMBER;
    }
    set_text(s_hd_pill_lbl, pill);
    if (s_hd_pill) lv_obj_set_style_bg_color(s_hd_pill, lv_color_hex(bg), 0);
    if (s_hd_pill_lbl) lv_obj_set_style_text_color(s_hd_pill_lbl, lv_color_hex(fg), 0);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (tmv.tm_year > 120) snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    else                   snprintf(b, sizeof(b), "--:--");
    set_text(s_hd_clock, b);
}

static void update_footer(bool have)
{
    char b[128];
    if (have) {
        snprintf(b, sizeof(b), "poll %s:%d  ok %u  err %u  api %d ms  %s",
                 FNOS_HOST, FNOS_PORT, (unsigned)s_st.ok_count, (unsigned)s_st.fail_count,
                 s_st.http_ms, s_st.online ? "live" : "retrying");
    } else {
        snprintf(b, sizeof(b), "poll %s:%d  waiting...", FNOS_HOST, FNOS_PORT);
    }
    set_text(s_ft_left, b);

    int64_t age_s = s_st.recv_ms ? (esp_timer_get_time() / 1000 - s_st.recv_ms) / 1000 : -1;
    if (have) {
        snprintf(b, sizeof(b), "age %llds  wifi %d dBm  heap %u KB", (long long)age_s,
                 (int)fnos_net_rssi(), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    } else {
        snprintf(b, sizeof(b), "wifi %d dBm  heap %u KB", (int)fnos_net_rssi(),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    }
    set_text(s_ft_mid, b);

    if (have && !s_st.online) {
        // 采集端失联优先于 NAS 自身告警：这时看到的所有数值都是旧的
        snprintf(b, sizeof(b), "AGENT UNREACHABLE: %s",
                 s_st.last_err[0] ? s_st.last_err : "poll failed");
        set_text(s_ft_alert, b);
        lv_obj_set_style_text_color(s_ft_alert, lv_color_hex(RED), 0);
    } else if (have && s_st.nalerts > 0) {
        set_text(s_ft_alert, s_st.alerts[0].m);
        lv_obj_set_style_text_color(s_ft_alert,
            lv_color_hex(strcmp(s_st.alerts[0].lv, "crit") == 0 ? RED : AMBER), 0);
    } else {
        set_text(s_ft_alert, have ? "no alerts" : "");
        if (have) lv_obj_set_style_text_color(s_ft_alert, lv_color_hex(GREEN), 0);
    }
}

static void update_overview(bool have)
{
    char b[64], s[64];
    if (have) {
        snprintf(b, sizeof(b), "%.0f%%", s_st.cpu.pct);
        set_text(s_ov_val[0], b);
        snprintf(s, sizeof(s), "load %.2f  %d cores", s_st.cpu.load1, s_st.cpu.cores);
        set_text(s_ov_sub[0], s);

        snprintf(b, sizeof(b), "%.0f%%", s_st.mem.pct);
        set_text(s_ov_val[1], b);
        snprintf(s, sizeof(s), "%.1f / %.0f GB", s_st.mem.used_mb / 1024.0f, s_st.mem.total_mb / 1024.0f);
        set_text(s_ov_sub[1], s);

        float hot = 0;
        for (int i = 0; i < s_st.ntemps; i++) if (s_st.temps[i].c > hot) hot = s_st.temps[i].c;
        snprintf(b, sizeof(b), "%.0fC", hot);
        set_text(s_ov_val[2], b);
        if (s_ov_val[2]) lv_obj_set_style_text_color(s_ov_val[2], lv_color_hex(temp_color(hot)), 0);
        snprintf(s, sizeof(s), "cpu %.0fC", s_st.cpu.temp_c);
        set_text(s_ov_sub[2], s);

        fmt_uptime(b, sizeof(b), s_st.uptime_s);
        set_text(s_ov_val[3], b);
        set_text(s_ov_sub[3], "nas uptime");
    } else {
        for (int i = 0; i < 4; i++) { set_text(s_ov_val[i], "--"); set_text(s_ov_sub[i], ""); }
    }

    for (int i = 0; i < 5; i++) {
        if (have && i < s_st.ntemps) {
            char nb[20];
            snprintf(nb, sizeof(nb), "%s", s_st.temps[i].n);
            set_text(s_ov_temp[i], nb);
            snprintf(b, sizeof(b), "%.0fC", s_st.temps[i].c);
            set_text(s_ov_temp_v[i], b);
            if (s_ov_temp_v[i]) lv_obj_set_style_text_color(s_ov_temp_v[i], lv_color_hex(temp_color(s_st.temps[i].c)), 0);
        } else {
            set_text(s_ov_temp[i], "");
            set_text(s_ov_temp_v[i], "");
        }
    }

    for (int i = 0; i < 6; i++) {
        if (have && i < s_st.nvols) {
            set_text(s_ov_vol[i], s_st.vols[i].mnt);
            snprintf(b, sizeof(b), "%.0f%%", s_st.vols[i].pct);
            set_text(s_ov_vol_v[i], b);
            uint32_t col = GREEN;
            if (s_st.vols[i].pct >= 90) col = RED;
            else if (s_st.vols[i].pct >= 80) col = AMBER;
            else if (s_st.vols[i].pct >= 60) col = BLUE;
            if (s_ov_vol_v[i]) lv_obj_set_style_text_color(s_ov_vol_v[i], lv_color_hex(col), 0);
            hbar_set(s_ov_vol_bar[i], s_ov_vol_w, s_st.vols[i].pct, true);
        } else {
            set_text(s_ov_vol[i], "");
            set_text(s_ov_vol_v[i], "");
            if (s_ov_vol_bar[i]) lv_obj_set_width(s_ov_vol_bar[i], 0);
        }
    }
}

static void update_storage(bool have)
{
    char b[64];
    if (have) {
        float total = 0, used = 0;
        for (int i = 0; i < s_st.nvols; i++) { total += s_st.vols[i].total_gb; used += s_st.vols[i].used_gb; }
        char t1[24], t2[24];
        fmt_cap(t1, sizeof(t1), total);
        fmt_cap(t2, sizeof(t2), used);
        snprintf(b, sizeof(b), "%d volumes  %s total  %s used", s_st.nvols, t1, t2);
        set_text(s_st_total, b);
    } else {
        set_text(s_st_total, "");
    }

    for (int i = 0; i < ST_VOL_ROWS; i++) {
        if (have && i < s_st.nvols) {
            set_text(s_st_mnt[i], s_st.vols[i].mnt);
            set_text(s_st_fs[i], s_st.vols[i].fs);
            snprintf(b, sizeof(b), "%.0f%%", s_st.vols[i].pct);
            set_text(s_st_pct[i], b);
            uint32_t col = GREEN;
            if (s_st.vols[i].pct >= 90) col = RED;
            else if (s_st.vols[i].pct >= 80) col = AMBER;
            if (s_st_pct[i]) lv_obj_set_style_text_color(s_st_pct[i], lv_color_hex(col), 0);
            char c1[24], c2[24];
            fmt_cap(c1, sizeof(c1), s_st.vols[i].used_gb);
            fmt_cap(c2, sizeof(c2), s_st.vols[i].total_gb);
            snprintf(b, sizeof(b), "%s / %s", c1, c2);
            set_text(s_st_use[i], b);
            hbar_set(s_st_bar[i], s_st_bar_w, s_st.vols[i].pct, true);
        } else {
            set_text(s_st_mnt[i], "");
            set_text(s_st_fs[i], "");
            set_text(s_st_pct[i], "");
            set_text(s_st_use[i], "");
            if (s_st_bar[i]) lv_obj_set_width(s_st_bar[i], 0);
        }
    }

    // 存储空间数量与首帧假设不同时，把下半区面板挪到最后一行的正下方
    if (have && s_st.nvols != s_st_last_nvols) {
        s_st_last_nvols = s_st.nvols;
        int n = s_st.nvols > 0 ? s_st.nvols : 1;
        if (n > ST_VOL_ROWS) n = ST_VOL_ROWS;      // 多出来的只体现在标题汇总里
        int ry = 46 + n * 44 + 8;
        if (s_st_raid_panel) {
            lv_obj_set_y(s_st_raid_panel, ry);
            lv_obj_set_height(s_st_raid_panel, CONT_H - ry);
        }
        if (s_st_io_panel) {
            lv_obj_set_y(s_st_io_panel, ry);
            lv_obj_set_height(s_st_io_panel, CONT_H - ry);
        }
    }

    for (int i = 0; i < FNOS_MAX_RAID; i++) {
        if (have && i < s_st.nraid) {
            snprintf(b, sizeof(b), "%s  %s", s_st.raid[i].dev, s_st.raid[i].lvl);
            set_text(s_st_raid[i], b);
            if (s_st.raid[i].sync_pct >= 0) {
                snprintf(b, sizeof(b), "SYNC %.1f%%", s_st.raid[i].sync_pct);
                if (s_st_raid_v[i]) lv_obj_set_style_text_color(s_st_raid_v[i], lv_color_hex(BLUE), 0);
            } else if (s_st.raid[i].ok) {
                snprintf(b, sizeof(b), "OK  %d/%d", s_st.raid[i].have, s_st.raid[i].want);
                if (s_st_raid_v[i]) lv_obj_set_style_text_color(s_st_raid_v[i], lv_color_hex(GREEN), 0);
            } else {
                snprintf(b, sizeof(b), "DEGRADED %s", s_st.raid[i].state);
                if (s_st_raid_v[i]) lv_obj_set_style_text_color(s_st_raid_v[i], lv_color_hex(RED), 0);
            }
            set_text(s_st_raid_v[i], b);
        } else {
            set_text(s_st_raid[i], "");
            set_text(s_st_raid_v[i], "");
        }
    }

    if (have && s_st.has_zfs) {
        char c1[24];
        fmt_cap(c1, sizeof(c1), s_st.zfs_arc_gb);
        snprintf(b, sizeof(b), "ZFS ARC %s  hit %.0f%%", c1, s_st.zfs_hit_pct);
        set_text(s_st_zfs, b);
    } else {
        set_text(s_st_zfs, "");
    }

    // 磁盘活动：按吞吐排序的前 5 个设备（采集端已经排好序）
    for (int i = 0; i < 5; i++) {
        if (have && i < s_st.ndisks) {
            char r1[20], r2[20];
            fmt_rate(r1, sizeof(r1), s_st.disks[i].rd_kbs);
            fmt_rate(r2, sizeof(r2), s_st.disks[i].wr_kbs);
            set_text(s_st_io_dev[i], s_st.disks[i].dev);
            snprintf(b, sizeof(b), "R %s  W %s", r1, r2);
            set_text(s_st_io_val[i], b);
        } else {
            set_text(s_st_io_dev[i], "");
            set_text(s_st_io_val[i], "");
        }
    }
}

static void update_network(bool have)
{
    char b[64], s[64];
    if (have) {
        fmt_rate(b, sizeof(b), s_st.net.rx_kbs);
        set_text(s_nw_rx, b);
        fmt_rate(b, sizeof(b), s_st.net.tx_kbs);
        set_text(s_nw_tx, b);
        char c1[24], c2[24];
        fmt_cap(c1, sizeof(c1), s_st.net.rx_total_gb);
        fmt_cap(c2, sizeof(c2), s_st.net.tx_total_gb);
        snprintf(s, sizeof(s), "total received %s", c1);
        set_text(s_nw_rx_sub, s);
        snprintf(s, sizeof(s), "total sent %s", c2);
        set_text(s_nw_tx_sub, s);
        snprintf(b, sizeof(b), "interface %s   poll 1 s   agent %d ms   samples %lld",
                 s_st.net.ifname[0] ? s_st.net.ifname : "?", s_st.http_ms, (long long)s_hist_seq);
        set_text(s_nw_info, b);
    } else {
        set_text(s_nw_rx, "--");
        set_text(s_nw_tx, "--");
        set_text(s_nw_rx_sub, "");
        set_text(s_nw_tx_sub, "");
        set_text(s_nw_info, "waiting for agent...");
    }
}

static void update_system(bool have)
{
    char b[96];
    for (int i = 0; i < FNOS_MAX_DOCKER; i++) {
        if (have && i < s_st.ndocker) {
            set_text(s_sy_dock[i], s_st.docker[i].n);
            set_text(s_sy_dock_s[i], s_st.docker[i].up ? "up" : s_st.docker[i].s);
            if (s_sy_dot[i]) lv_obj_set_style_bg_color(s_sy_dot[i], lv_color_hex(s_st.docker[i].up ? GREEN : RED), 0);
        } else {
            set_text(s_sy_dock[i], "");
            set_text(s_sy_dock_s[i], "");
            if (s_sy_dot[i]) lv_obj_set_style_bg_color(s_sy_dot[i], lv_color_hex(TRACK), 0);
        }
    }
    for (int i = 0; i < 8; i++) {
        if (have && i < s_st.ntemps) {
            set_text(s_sy_temp[i], s_st.temps[i].n);
            snprintf(b, sizeof(b), "%.1fC", s_st.temps[i].c);
            set_text(s_sy_temp_v[i], b);
            if (s_sy_temp_v[i]) lv_obj_set_style_text_color(s_sy_temp_v[i], lv_color_hex(temp_color(s_st.temps[i].c)), 0);
        } else {
            set_text(s_sy_temp[i], "");
            set_text(s_sy_temp_v[i], "");
        }
    }

    int64_t age_s = s_st.recv_ms ? (esp_timer_get_time() / 1000 - s_st.recv_ms) / 1000 : -1;
    snprintf(b, sizeof(b), "host        %s", have ? s_st.host : "--");
    set_text(s_sy_kv[0], b);
    snprintf(b, sizeof(b), "endpoint    %s:%d", FNOS_HOST, FNOS_PORT);
    set_text(s_sy_kv[1], b);
    snprintf(b, sizeof(b), "http        %d ms  (status %d)", s_st.http_ms, s_st.last_status);
    set_text(s_sy_kv[2], b);
    snprintf(b, sizeof(b), "poll        %u ok / %u err", (unsigned)s_st.ok_count, (unsigned)s_st.fail_count);
    set_text(s_sy_kv[3], b);
    snprintf(b, sizeof(b), "last error  %s", s_st.last_err[0] ? s_st.last_err : "none");
    set_text(s_sy_kv[4], b);
    snprintf(b, sizeof(b), "data age    %lld s", (long long)age_s);
    set_text(s_sy_kv[5], b);
    snprintf(b, sizeof(b), "wifi        %s  %d dBm", fnos_net_ip()[0] ? fnos_net_ip() : "offline", (int)fnos_net_rssi());
    set_text(s_sy_kv[6], b);
    snprintf(b, sizeof(b), "internal    %u KB free", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    set_text(s_sy_kv[7], b);
    snprintf(b, sizeof(b), "psram       %u KB free", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    set_text(s_sy_kv[8], b);
    snprintf(b, sizeof(b), "charts      %d s window", CHART_PTS);
    set_text(s_sy_kv[9], b);
}

static void drain_history(void)
{
    fnos_sample_t smp[12];
    int64_t next = s_hist_seq;
    int n = fnos_data_hist_read(s_hist_seq, smp, 12, &next);
    if (n <= 0) return;
    for (int i = 0; i < n; i++) {
        lv_chart_set_next_value(s_ov_chart, s_ov_ser_cpu, (int32_t)(smp[i].cpu + 0.5f));
        lv_chart_set_next_value(s_ov_chart, s_ov_ser_mem, (int32_t)(smp[i].mem + 0.5f));
        lv_chart_set_next_value(s_nw_chart, s_nw_ser_rx, (int32_t)(smp[i].rx_kbs + 0.5f));
        lv_chart_set_next_value(s_nw_chart, s_nw_ser_tx, (int32_t)(smp[i].tx_kbs + 0.5f));
    }
    s_hist_seq = next;

    if (s_nw_chart && s_nw_ser_rx && s_nw_ser_tx) {
        int32_t peak = 0;
        int32_t *a = lv_chart_get_series_y_array(s_nw_chart, s_nw_ser_rx);
        int32_t *b = lv_chart_get_series_y_array(s_nw_chart, s_nw_ser_tx);
        if (a && b) {
            for (int i = 0; i < CHART_PTS; i++) {
                // 未被写入的点是 LV_CHART_POINT_NONE(=INT32_MAX)，不能当数据：
                // 否则开机后图表没填满的那几分钟纵轴会变成 2e9 这种荒诞刻度。
                if (a[i] != LV_CHART_POINT_NONE && a[i] > peak) peak = a[i];
                if (b[i] != LV_CHART_POINT_NONE && b[i] > peak) peak = b[i];
            }
        }
        int top = nice_max(peak < 64 ? 64 : peak);
        lv_chart_set_range(s_nw_chart, LV_CHART_AXIS_PRIMARY_Y, 0, top);
        char buf[24];
        for (int i = 0; i < 3; i++) {
            fmt_rate(buf, sizeof(buf), (float)(top - (top / 2) * i));
            set_text(s_nw_axis[i], buf);
        }
    }
}

static void view_tick(lv_timer_t *t)
{
    (void)t;
    bool have = fnos_data_get(&s_st);
    drain_history();
    update_header(have);
    update_footer(have);
    update_overview(have);
    update_storage(have);
    update_network(have);
    update_system(have);
}

void fnos_view_create(void)
{
    if (s_created) return;
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header(scr);
    build_rail(scr);
    build_footer(scr);

    for (int i = 0; i < PG_COUNT; i++) {
        s_pages[i] = make_page(scr);
        if (!s_pages[i]) {
            ESP_LOGE(TAG, "page %d alloc failed", i);
            return;
        }
    }
    build_overview(s_pages[PG_OVERVIEW]);
    build_storage(s_pages[PG_STORAGE]);
    build_network(s_pages[PG_NETWORK]);
    build_system(s_pages[PG_SYSTEM]);

    if (!s_ov_chart || !s_nw_chart || !s_ov_ser_cpu || !s_nw_ser_rx) {
        ESP_LOGE(TAG, "chart creation failed - UI would hang on refresh");
        return;
    }

    s_page = 0;
    apply_page();
    s_created = true;
    lv_timer_create(view_tick, 500, NULL);
    ESP_LOGI(TAG, "dashboard created (%d pages, %d s window)", PG_COUNT, CHART_PTS);
}
