// KK_UI_UMG LVGL 运行时构件实现（视觉层，不含任何业务判断）。
// 视觉令牌见 kk_theme.h；设计合同见 docs/ui-kk.md。
#include "kk_widgets.h"
#include "fnos_fonts.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define KK_TREND_MAX_PTS 192

/* ───────────────────────────── 基础对象 */

void kk_style_bg(lv_obj_t *o, lv_color_t color, lv_opa_t opa)
{
    if (!o) return;
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
}

lv_color_t kk_color_parse(const char *hex)
{
    if (!hex || hex[0] != '#') return lv_color_hex(KK_TEXT1);
    unsigned rgb = 0;
    sscanf(hex + 1, "%6x", &rgb);
    return lv_color_hex(rgb);
}

lv_opa_t kk_opa_parse(const char *hex)
{
    if (!hex || hex[0] != '#' || hex[7] == '\0') return LV_OPA_COVER;
    unsigned a = 0xFF;
    sscanf(hex + 7, "%2x", &a);
    return (lv_opa_t)a;
}

static lv_obj_t *kk_base_create(lv_obj_t *parent, kk_rect_t r);   /* fwd */

lv_obj_t *kk_panel_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb, int radius)
{
    lv_obj_t *o = kk_base_create(parent, kk_rect(0, 0, 0, 0, x, y, w, h));
    if (!o) return NULL;
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *kk_base_create(lv_obj_t *parent, kk_rect_t r)
{
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) return NULL;
    kk_place(o, parent, r);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN);
    return o;
}

lv_obj_t *kk_panel_create(lv_obj_t *parent, kk_rect_t r)
{
    lv_obj_t *o = kk_base_create(parent, r);
    // LVGL 9.5 每个对象默认可点：装饰件必须清掉，否则吞掉点击且 CLICKED 不冒泡
    if (o) lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *kk_button_create(lv_obj_t *parent, kk_rect_t r)
{
    lv_obj_t *o = kk_base_create(parent, r);
    if (o) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    }
    return o;
}

lv_obj_t *kk_label_create(lv_obj_t *parent, kk_rect_t r)
{
    lv_obj_t *l = lv_label_create(parent);
    if (!l) return NULL;
    kk_place(l, parent, r);
    // LONG_DOT：溢出用省略号收尾（LONG_CLIP 会留悬空分隔符，实机照片已踩到）
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(l, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(KK_TEXT1), 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    return l;
}

void kk_label_vcenter(lv_obj_t *label, int box_h)
{
    // 高度显式传入：lv_obj_get_height 在对象刚建、coords 未刷新时不可靠
    if (!label) return;
    const lv_font_t *f = lv_obj_get_style_text_font(label, 0);
    int lh = f ? (int)f->line_height : 16;
    int pad = (box_h - lh) / 2;
    if (pad > 0) lv_obj_set_style_pad_top(label, pad, 0);
}

/* ───────────────────────────── 状态胶囊 */

lv_obj_t *kk_chip_create(lv_obj_t *parent, kk_rect_t r, lv_color_t color)
{
    lv_obj_t *c = kk_base_create(parent, r);
    if (!c) return NULL;
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, color, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_20, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, color, 0);
    return c;
}

static lv_obj_t *chip_label(lv_obj_t *chip)
{
    lv_obj_t *l = lv_obj_get_child(chip, 0);
    if (!l) {
        l = lv_label_create(chip);
        if (!l) return NULL;
        lv_obj_set_style_text_font(l, &ui_font_cjk_13, 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(l);
    }
    return l;
}

void kk_chip_set_color(lv_obj_t *chip, lv_color_t color)
{
    if (!chip) return;
    lv_obj_set_style_border_color(chip, color, 0);
    lv_obj_set_style_bg_color(chip, color, 0);
    lv_obj_t *l = chip_label(chip);
    if (l) lv_obj_set_style_text_color(l, color, 0);
}

void kk_chip_set_text(lv_obj_t *chip, const char *txt)
{
    lv_obj_t *l = chip ? chip_label(chip) : NULL;
    if (l && txt) lv_label_set_text(l, txt);
}

void kk_chip_set(lv_obj_t *chip, const char *txt, lv_color_t color)
{
    kk_chip_set_color(chip, color);
    kk_chip_set_text(chip, txt);
}

static void chip_pulse_cb(lv_timer_t *t)
{
    lv_obj_t *chip = (lv_obj_t *)lv_timer_get_user_data(t);
    if (chip) lv_obj_set_style_bg_opa(chip, LV_OPA_20, 0);
    lv_timer_delete(t);
}

void kk_chip_pulse(lv_obj_t *chip)
{
    if (!chip) return;
    lv_obj_set_style_bg_opa(chip, LV_OPA_50, 0);
    lv_timer_t *t = lv_timer_create(chip_pulse_cb, 260, chip);
    if (t) lv_timer_set_repeat_count(t, 1);
}

/* ───────────────────────────── 弧表 */

lv_obj_t *kk_arc_create(lv_obj_t *parent, kk_rect_t r, lv_color_t color)
{
    lv_obj_t *a = lv_arc_create(parent);
    if (!a) return NULL;
    kk_place(a, parent, r);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);              // 开放式 270° 弧
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(KK_INSET), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

void kk_arc_set(lv_obj_t *arc, int pct)
{
    if (!arc) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_arc_set_value(arc, pct);
}

/* ───────────────────────────── 进度条（细条 + 能量渐变） */

void kk_bar_create(kk_bar_t *b, lv_obj_t *parent, kk_rect_t r, lv_color_t color, int radius)
{
    if (!b) return;
#define pw_of(rect) ((rect).size_w)
    kk_rect_t fill_r = r;
    fill_r.size_w = 2;
    fill_r.amax_x = fill_r.amin_x;
    fill_r.size_h = r.size_h;
    b->track = kk_panel_create(parent, r);
    b->fill = kk_panel_create(parent, fill_r);
    b->w = (int)(pw_of(r));          // 直接取 rect 宽，不查未刷新的 coords
    if (b->track) {
        lv_obj_set_style_radius(b->track, radius, 0);
        lv_obj_set_style_bg_color(b->track, lv_color_hex(KK_TRACK), 0);
        lv_obj_set_style_bg_opa(b->track, LV_OPA_COVER, 0);
    }
#undef pw_of
    if (b->fill) {
        lv_obj_set_style_radius(b->fill, radius, 0);
        lv_obj_set_style_bg_color(b->fill, color, 0);
        lv_obj_set_style_bg_grad_color(b->fill, color, 0);
        lv_obj_set_style_bg_grad_dir(b->fill, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(b->fill, LV_OPA_COVER, 0);
    }
}

// 能量渐变档位：cool(充足) / warm(偏满) / full(告警)
static void bar_gradient(lv_obj_t *fill, float pct, lv_color_t fixed, bool use_fixed)
{
    if (use_fixed) {
        lv_obj_set_style_bg_color(fill, fixed, 0);
        lv_obj_set_style_bg_grad_color(fill, fixed, 0);
        return;
    }
    if (pct >= KK_BAR_FULL) {
        lv_obj_set_style_bg_color(fill, lv_color_hex(KK_GRAD_WARM_B), 0);
        lv_obj_set_style_bg_grad_color(fill, lv_color_hex(KK_GRAD_WARM_C), 0);
    } else if (pct >= KK_BAR_WARM) {
        lv_obj_set_style_bg_color(fill, lv_color_hex(KK_GRAD_WARM_A), 0);
        lv_obj_set_style_bg_grad_color(fill, lv_color_hex(KK_GRAD_WARM_B), 0);
    } else {
        lv_obj_set_style_bg_color(fill, lv_color_hex(KK_GRAD_COOL_A), 0);
        lv_obj_set_style_bg_grad_color(fill, lv_color_hex(KK_GRAD_COOL_C), 0);
    }
}

static void bar_apply(kk_bar_t *b, float pct, lv_color_t fixed, bool use_fixed)
{
    if (!b || !b->fill) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int wpx = (int)(b->w * pct / 100.0f + 0.5f);
    if (wpx < 3 && pct > 0) wpx = 3;
    lv_obj_set_width(b->fill, wpx);
    bar_gradient(b->fill, pct, fixed, use_fixed);
}

void kk_bar_set(kk_bar_t *b, float pct)
{
    bar_apply(b, pct, lv_color_hex(KK_ACCENT), false);
}

void kk_bar_set_fixed(kk_bar_t *b, float pct, lv_color_t color)
{
    bar_apply(b, pct, color, true);
}

/* ───────────────────────────── 趋势（低透明度面积层 + 亮线） */

static lv_obj_t *trend_layer(lv_obj_t *parent, kk_rect_t r, int pts, bool bars,
                             lv_color_t color, int area_opa_pct, int grid_lines)
{
    lv_obj_t *c = lv_chart_create(parent);
    if (!c) return NULL;
    kk_place(c, parent, r);
    lv_chart_set_type(c, bars ? LV_CHART_TYPE_BAR : LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, pts);
    lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(c, grid_lines, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    if (bars) {
        // 面积层是"纹理"不是主角：柱间留缝。
        // ⚠ LVGL 的 bar 绘制把 bg_opa 写死 COVER（lv_chart.c draw_series_bar），
        //   style 的 bg_opa 无效——低透明度只能靠"系列色向底色混合"模拟（v4.2 实机照片坐实）。
        lv_obj_set_style_pad_column(c, 2, LV_PART_ITEMS);
        lv_obj_set_style_radius(c, 1, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_ITEMS);
        color = lv_color_mix(color, lv_color_hex(KK_PANEL), kk_opa_pct(area_opa_pct / 100.0f));
    } else {
        lv_obj_set_style_line_width(c, 3, LV_PART_ITEMS);
        lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);   // 不要数据点圆点
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_ITEMS);   // 线层不要面积填充
    }
    lv_chart_series_t *s = lv_chart_add_series(c, color, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_user_data(c, s);
    return c;
}

void kk_trend_create(kk_trend_t *t, lv_obj_t *parent, kk_rect_t r,
                     lv_color_t color, int pts, int area_opa_pct, int grid_lines, int auto_range)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->pts = pts;
    t->auto_range = auto_range;
    /* areaOpacity=0 → 只留折线（合轨对比时面积层会把两条线糊成一块色） */
    t->bar = area_opa_pct > 0 ? trend_layer(parent, r, pts, true, color, area_opa_pct, grid_lines) : NULL;
    t->line = trend_layer(parent, r, pts, false, color, area_opa_pct, grid_lines);
    if (t->bar) t->s_bar = (lv_chart_series_t *)lv_obj_get_user_data(t->bar);
    if (t->line) t->s_line = (lv_chart_series_t *)lv_obj_get_user_data(t->line);
}

void kk_trend_sync_series(kk_trend_t *t, const kk_series_t *s)
{
    if (!t || !s || !s->v || t->pts <= 0) return;
    int pts = t->pts > KK_TREND_MAX_PTS ? KK_TREND_MAX_PTS : t->pts;
    int n = s->count < pts ? s->count : pts;
    static int32_t buf[KK_TREND_MAX_PTS];     // LVGL 任务内单线程使用
    // 环形缓冲 → 时间序；旧点留 LV_CHART_POINT_NONE 不画
    for (int i = 0; i < pts; i++) {
        int32_t v = LV_CHART_POINT_NONE;
        int from_end = n - i;                 // 第 i 列画倒数 from_end 个样本
        if (from_end > 0) {
            int idx = (int)s->head - from_end;
            while (idx < 0) idx += s->cap;
            v = (int32_t)(s->v[idx] + 0.5f);
        }
        buf[i] = v;
    }
    if (t->bar && t->s_bar)  lv_chart_set_series_values(t->bar, t->s_bar, buf, pts);
    if (t->line && t->s_line) lv_chart_set_series_values(t->line, t->s_line, buf, pts);
    /* 自适应量程：固定 0..100 会让 CPU 这种 1~10% 的曲线贴底"看不见" */
    if (t->auto_range) {
        int peak = 0;
        for (int i = 0; i < pts; i++)
            if (buf[i] != LV_CHART_POINT_NONE && buf[i] > peak) peak = buf[i];
        int top = (int)(peak * 1.25f);
        if (top < 20) top = 20;
        kk_trend_range(t, (float)top);
    }
}

void kk_trend_range(kk_trend_t *t, float top)
{
    if (!t || top <= 0) return;
    if (t->bar) lv_chart_set_range(t->bar, LV_CHART_AXIS_PRIMARY_Y, 0, (int32_t)top);
    if (t->line) lv_chart_set_range(t->line, LV_CHART_AXIS_PRIMARY_Y, 0, (int32_t)top);
}

int kk_trend_peak(const kk_trend_t *t)
{
    if (!t || !t->line || !t->s_line) return 0;
    int32_t *a = lv_chart_get_series_y_array(t->line, t->s_line);
    if (!a) return 0;
    int peak = 0;
    for (int i = 0; i < t->pts; i++) {
        // 未写入的点是 LV_CHART_POINT_NONE(=INT32_MAX)，不能当数据
        if (a[i] != LV_CHART_POINT_NONE && a[i] > peak) peak = (int)a[i];
    }
    return peak;
}

/* ───────────────────────────── Wi-Fi 信号条 */

void kk_signal_bars(lv_obj_t *parent, kk_rect_t r, lv_obj_t *out[4])
{
    static const int hh[4] = { 7, 11, 15, 19 };
    for (int i = 0; i < 4; i++) {
        kk_rect_t br = r;
        br.pos_x = r.pos_x + i * 7;
        br.pos_y = r.pos_y + (19 - hh[i]);
        br.size_w = 5;
        br.size_h = hh[i];
        br.amin_x = br.amax_x;
        br.amin_y = br.amax_y;
        out[i] = kk_panel_create(parent, br);
        if (out[i]) {
            lv_obj_set_style_radius(out[i], 2, 0);
            lv_obj_set_style_bg_color(out[i], lv_color_hex(KK_TRACK), 0);
            lv_obj_set_style_bg_opa(out[i], LV_OPA_COVER, 0);
        }
    }
}

void kk_signal_set(lv_obj_t *bars[4], int8_t rssi)
{
    int lit = 0;
    if (rssi <= -1) {
        if (rssi >= -55)      lit = 4;
        else if (rssi >= -65) lit = 3;
        else if (rssi >= -75) lit = 2;
        else                  lit = 1;
    }
    for (int i = 0; i < 4; i++) {
        if (bars[i]) {
            lv_obj_set_style_bg_color(bars[i],
                lv_color_hex(i < lit ? KK_OK : KK_TRACK), 0);
        }
    }
}

/* ───────────────────────────── 图标（纯几何） */

lv_obj_t *kk_icon_create(lv_obj_t *parent, kk_rect_t r, int kind, lv_color_t color)
{
    // 图标画布 26×26；返回画布容器，几何块作为其子对象
    lv_obj_t *box = kk_panel_create(parent, r);
    if (!box) return NULL;
    const int S = 26;
    uint32_t c = lv_color_to_u32(color);
    switch (kind) {
    case KK_ICON_OVERVIEW:                       // 2×2 网格
        kk_panel_box(box, 0, 0, 11, 11, c, 3);
        kk_panel_box(box, 14, 0, 11, 11, c, 3);
        kk_panel_box(box, 0, 14, 11, 11, c, 3);
        kk_panel_box(box, 14, 14, 11, 11, c, 3);
        break;
    case KK_ICON_STORAGE:                        // 三层盘位
        kk_panel_box(box, 0, 0, S, 6, c, 3);
        kk_panel_box(box, 0, 10, S, 6, c, 3);
        kk_panel_box(box, 0, 20, S, 6, c, 3);
        break;
    case KK_ICON_NETWORK:                        // 信号柱
        kk_panel_box(box, 0, 16, 5, 10, c, 2);
        kk_panel_box(box, 7, 10, 5, 16, c, 2);
        kk_panel_box(box, 14, 4, 5, 22, c, 2);
        kk_panel_box(box, 21, 0, 5, 26, c, 2);
        break;
    case KK_ICON_SYSTEM:                         // 芯片：外框 + 内核
        kk_panel_box(box, 0, 0, S, S, KK_GUIDE, 5);
        kk_panel_box(box, 2, 2, S - 4, S - 4, KK_PANEL, 4);
        kk_panel_box(box, 7, 7, 12, 12, c, 3);
        break;
    default:
        break;
    }
    return box;
}

/* ───────────────────────────── 手势：PRESS/RELEASE 差分 */

typedef struct {
    kk_swipe_cb_t on_left;
    kk_swipe_cb_t on_right;
    int press_x;
} kk_swipe_ctx_t;

static void swipe_event(lv_event_t *e)
{
    kk_swipe_ctx_t *ctx = (kk_swipe_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p;
    if (!indev) return;
    lv_indev_get_point(indev, &p);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        ctx->press_x = p.x;
        return;
    }
    int dx = p.x - ctx->press_x;                  // 阈值 ±70px（与 v3 一致）
    if (dx <= -70 && ctx->on_left)  ctx->on_left();
    if (dx >= 70 && ctx->on_right) ctx->on_right();
}

void kk_swipe_attach(lv_obj_t *obj, kk_swipe_cb_t on_left, kk_swipe_cb_t on_right)
{
    if (!obj || (!on_left && !on_right)) return;
    kk_swipe_ctx_t *ctx = (kk_swipe_ctx_t *)lv_malloc(sizeof(kk_swipe_ctx_t));
    if (!ctx) return;
    ctx->on_left = on_left;
    ctx->on_right = on_right;
    ctx->press_x = 0;
    lv_obj_add_event_cb(obj, swipe_event, LV_EVENT_PRESSED, ctx);
    lv_obj_add_event_cb(obj, swipe_event, LV_EVENT_RELEASED, ctx);
}

/* ───────────────────────────── Store series 字段 */

void kk_series_init(kk_series_t *s, float *buf, uint16_t cap)
{
    if (!s) return;
    s->v = buf;
    s->cap = cap;
    s->head = 0;
    s->count = 0;
}

void kk_series_push(kk_series_t *s, float v)
{
    if (!s || !s->v || s->cap == 0) return;
    s->v[s->head] = v;
    s->head = (uint16_t)((s->head + 1) % s->cap);
    if (s->count < s->cap) s->count++;
}
