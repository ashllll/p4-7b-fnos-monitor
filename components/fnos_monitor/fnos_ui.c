// 视觉层构件实现。这里不做任何数据判断，只负责"画成什么样"。
#include "fnos_ui.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "fnos_ui";

/* ───────────────────────────── 基础对象 */

lv_obj_t *ck_obj(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius, bool clickable)
{
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) return NULL;
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    // LVGL 9.5 每个对象默认可点：装饰件必须清掉，否则吞掉点击且不冒泡
    if (!clickable) lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN);
    return o;
}

lv_obj_t *ck_tile(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = ck_obj(parent, x, y, w, h, CK_PANEL, CK_RADIUS, false);
    if (o) {
        lv_obj_set_style_border_width(o, 1, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(CK_GUIDE), 0);
    }
    return o;
}

lv_obj_t *ck_label(lv_obj_t *parent, const char *txt, const lv_font_t *f, uint32_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    if (!l) return NULL;
    lv_label_set_text(l, txt ? txt : "");
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t *ck_label_r(lv_obj_t *parent, const char *txt, const lv_font_t *f, uint32_t color, int right_pad, int y)
{
    lv_obj_t *l = ck_label(parent, txt, f, color, 0, y);
    if (l) lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -right_pad, y);
    return l;
}

lv_obj_t *ck_divider(lv_obj_t *parent, int x, int y, int w, uint32_t color)
{
    return ck_obj(parent, x, y, w, 1, color, 0, false);
}

void ck_set(lv_obj_t *l, const char *txt)
{
    if (l && txt) lv_label_set_text(l, txt);
}

void ck_set_color(lv_obj_t *l, uint32_t color)
{
    if (l) lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
}

void ck_set_opa(lv_obj_t *o, lv_opa_t opa)
{
    if (o) lv_obj_set_style_opa(o, opa, 0);
}

/* ───────────────────────────── 进度条 */

void ck_bar_create(ck_bar_t *b, lv_obj_t *parent, int x, int y, int w, int h)
{
    if (!b) return;
    b->w = w;
    b->track = ck_obj(parent, x, y, w, h, 0x151A21, h / 2, false);
    b->fill = ck_obj(parent, x, y, 2, h, CK_OK, h / 2, false);
    if (b->fill) {
        lv_obj_set_style_bg_grad_dir(b->fill, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_grad_color(b->fill, lv_color_hex(CK_OK), 0);
    }
}

// 能量渐变档位：cool(充足) / warm(偏满) / full(告警)
static void bar_gradient(lv_obj_t *fill, float pct, uint32_t fixed)
{
    if (fixed) {
        lv_obj_set_style_bg_color(fill, lv_color_hex(fixed), 0);
        lv_obj_set_style_bg_grad_color(fill, lv_color_hex(fixed), 0);
        return;
    }
    if (pct >= 85.0f) {
        lv_obj_set_style_bg_color(fill, lv_color_hex(CK_WARN), 0);
        lv_obj_set_style_bg_grad_color(fill, lv_color_hex(CK_DANGER), 0);
    } else if (pct >= 60.0f) {
        lv_obj_set_style_bg_color(fill, lv_color_hex(CK_ZFS), 0);
        lv_obj_set_style_bg_grad_color(fill, lv_color_hex(CK_WARN), 0);
    } else {
        lv_obj_set_style_bg_color(fill, lv_color_hex(CK_NET_DOWN), 0);
        lv_obj_set_style_bg_grad_color(fill, lv_color_hex(CK_OK), 0);
    }
}

void ck_bar_set_color(ck_bar_t *b, float pct, uint32_t fixed)
{
    if (!b || !b->fill) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int wpx = (int)(b->w * pct / 100.0f + 0.5f);
    if (wpx < 3 && pct > 0) wpx = 3;
    lv_obj_set_width(b->fill, wpx);
    bar_gradient(b->fill, pct, fixed);
}

void ck_bar_set(ck_bar_t *b, float pct)
{
    ck_bar_set_color(b, pct, 0);
}

void ck_bar_tick(lv_obj_t *parent, int x, int y, int h)
{
    ck_obj(parent, x, y, 1, h, CK_GUIDE, 0, false);
}

/* ───────────────────────────── 弧表 */

lv_obj_t *ck_arc(lv_obj_t *parent, int x, int y, int size, uint32_t color)
{
    lv_obj_t *a = lv_arc_create(parent);
    if (!a) return NULL;
    lv_obj_set_pos(a, x, y);
    lv_obj_set_size(a, size, size);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);              // 开放式 270° 弧
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);   // 不要旋钮
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(0x151A21), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

void ck_arc_set(lv_obj_t *arc, int pct)
{
    if (!arc) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_arc_set_value(arc, pct);
}

/* ───────────────────────────── 状态胶囊 */

lv_obj_t *ck_chip(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *c = ck_obj(parent, x, y, w, h, color, h / 2, false);
    if (c) {
        lv_obj_set_style_bg_opa(c, LV_OPA_20, 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(color), 0);
    }
    return c;
}

void ck_chip_set(lv_obj_t *chip, const char *txt, uint32_t color)
{
    if (!chip) return;
    lv_obj_set_style_border_color(chip, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(color), 0);
    lv_obj_t *l = lv_obj_get_child(chip, 0);
    if (!l) {
        l = ck_label(chip, txt, CK_F_CMETA, color, 0, 0);
        lv_obj_center(l);
    }
    ck_set(l, txt);
    ck_set_color(l, color);
}

static void chip_pulse_cb(lv_timer_t *t)
{
    lv_obj_t *chip = (lv_obj_t *)lv_timer_get_user_data(t);
    if (chip) lv_obj_set_style_bg_opa(chip, LV_OPA_20, 0);
    lv_timer_delete(t);
}

void ck_chip_pulse(lv_obj_t *chip)
{
    if (!chip) return;
    lv_obj_set_style_bg_opa(chip, LV_OPA_50, 0);
    lv_timer_t *t = lv_timer_create(chip_pulse_cb, 260, chip);
    if (t) lv_timer_set_repeat_count(t, 1);
}

/* ───────────────────────────── 趋势 */

static lv_obj_t *trend_layer(lv_obj_t *parent, int x, int y, int w, int h, int pts, bool bars, uint32_t color)
{
    lv_obj_t *c = lv_chart_create(parent);
    if (!c) return NULL;
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_chart_set_type(c, bars ? LV_CHART_TYPE_BAR : LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, pts);
    lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(c, 0, 0);          // 参考线由页面自己画，保持稀疏
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    if (bars) {
        // 面积层是"纹理"，不是主角：柱子之间留 2px 缝、透明度压到 15%，
        // 否则密集柱会糊成一整块实心色板（实测第一版就是这样）
        lv_obj_set_style_pad_column(c, 2, LV_PART_ITEMS);
        lv_obj_set_style_radius(c, 1, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(c, LV_OPA_20, LV_PART_ITEMS);
    } else {
        lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);   // 不要数据点圆点
    }
    lv_chart_series_t *s = lv_chart_add_series(c, lv_color_hex(color), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_user_data(c, s);                    // 便于 push 时取回
    return c;
}

void ck_trend_create(ck_trend_t *t, lv_obj_t *parent, int x, int y, int w, int h,
                     uint32_t color, int pts)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->pts = pts;
    t->bar = trend_layer(parent, x, y, w, h, pts, true, color);
    t->line = trend_layer(parent, x, y, w, h, pts, false, color);
    if (t->bar) t->s_bar = (lv_chart_series_t *)lv_obj_get_user_data(t->bar);
    if (t->line) t->s_line = (lv_chart_series_t *)lv_obj_get_user_data(t->line);
    if (!t->bar || !t->line || !t->s_bar || !t->s_line) {
        ESP_LOGE(TAG, "trend create failed");
    }
}

void ck_trend_push(ck_trend_t *t, int v)
{
    if (!t) return;
    if (t->s_bar) lv_chart_set_next_value(t->bar, t->s_bar, v);
    if (t->s_line) lv_chart_set_next_value(t->line, t->s_line, v);
}

void ck_trend_range(ck_trend_t *t, int top)
{
    if (!t || top <= 0) return;
    if (t->bar) lv_chart_set_range(t->bar, LV_CHART_AXIS_PRIMARY_Y, 0, top);
    if (t->line) lv_chart_set_range(t->line, LV_CHART_AXIS_PRIMARY_Y, 0, top);
}

int ck_trend_peak(const ck_trend_t *t)
{
    if (!t || !t->line || !t->s_line) return 0;
    int32_t *a = lv_chart_get_series_y_array(t->line, t->s_line);
    if (!a) return 0;
    int peak = 0;
    for (int i = 0; i < t->pts; i++) {
        // 未写入的点是 LV_CHART_POINT_NONE(=INT32_MAX)，不能当数据
        if (a[i] != LV_CHART_POINT_NONE && a[i] > peak) peak = a[i];
    }
    return peak;
}

/* ───────────────────────────── Wi-Fi 信号条 */

void ck_signal_bars(lv_obj_t *parent, int x, int y, lv_obj_t *out[4])
{
    static const int hh[4] = { 7, 11, 15, 19 };
    for (int i = 0; i < 4; i++) {
        out[i] = ck_obj(parent, x + i * 7, y + (19 - hh[i]), 5, hh[i], CK_IDLE, 2, false);
    }
}

void ck_signal_set(lv_obj_t *bars[4], int8_t rssi)
{
    int lit = 0;
    if (rssi <= -1) {
        if (rssi >= -55)      lit = 4;
        else if (rssi >= -65) lit = 3;
        else if (rssi >= -75) lit = 2;
        else                  lit = 1;
    }
    for (int i = 0; i < 4; i++) {
        if (bars[i]) lv_obj_set_style_bg_color(bars[i], lv_color_hex(i < lit ? CK_OK : CK_IDLE), 0);
    }
}

/* ───────────────────────────── 图标（纯几何） */

void ck_icon(lv_obj_t *parent, int x, int y, int kind, uint32_t color)
{
    const int S = 26;                       // 图标画布 26×26
    switch (kind) {
    case CK_ICON_OVERVIEW:                  // 2×2 网格
        ck_obj(parent, x,      y,      11, 11, color, 3, false);
        ck_obj(parent, x + 14, y,      11, 11, color, 3, false);
        ck_obj(parent, x,      y + 14, 11, 11, color, 3, false);
        ck_obj(parent, x + 14, y + 14, 11, 11, color, 3, false);
        break;
    case CK_ICON_STORAGE:                   // 三层盘位
        ck_obj(parent, x, y,      S, 6, color, 3, false);
        ck_obj(parent, x, y + 10, S, 6, color, 3, false);
        ck_obj(parent, x, y + 20, S, 6, color, 3, false);
        break;
    case CK_ICON_NETWORK:                   // 信号柱
        ck_obj(parent, x,      y + 16, 5, 10, color, 2, false);
        ck_obj(parent, x + 7,  y + 10, 5, 16, color, 2, false);
        ck_obj(parent, x + 14, y + 4,  5, 22, color, 2, false);
        ck_obj(parent, x + 21, y,      5, 26, color, 2, false);
        break;
    case CK_ICON_SYSTEM:                    // 芯片：外框 + 内核
        ck_obj(parent, x, y, S, S, CK_GUIDE, 5, false);
        ck_obj(parent, x + 2, y + 2, S - 4, S - 4, CK_PANEL, 4, false);
        ck_obj(parent, x + 7, y + 7, 12, 12, color, 3, false);
        break;
    default:
        break;
    }
}
