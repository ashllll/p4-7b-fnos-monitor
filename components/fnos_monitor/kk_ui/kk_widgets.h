#pragma once
// KK_UI_UMG LVGL 运行时构件（对应 KK 的 Runtime 组件层）。
// 这里不做任何业务判断，只负责"画成什么样"。视觉令牌见 kk_theme.h。
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "kk_rect.h"
#include "kk_theme.h"

/* ── 基础 ─────────────────────────────────────────────────────────── */
void      kk_style_bg(lv_obj_t *o, lv_color_t color, lv_opa_t opa);
lv_color_t kk_color_parse(const char *hex);          /* "#RRGGBB[AA]" */
lv_opa_t   kk_opa_parse(const char *hex);           /* 取 alpha 通道，缺省 0xFF */
lv_obj_t *kk_panel_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb, int radius);

lv_obj_t *kk_panel_create(lv_obj_t *parent, kk_rect_t r);
lv_obj_t *kk_button_create(lv_obj_t *parent, kk_rect_t r);
lv_obj_t *kk_label_create(lv_obj_t *parent, kk_rect_t r);
void      kk_label_vcenter(lv_obj_t *label, int box_h);

/* ── 控件 ─────────────────────────────────────────────────────────── */
lv_obj_t *kk_chip_create(lv_obj_t *parent, kk_rect_t r, lv_color_t color);
void      kk_chip_set(lv_obj_t *chip, const char *txt, lv_color_t color);
void      kk_chip_set_text(lv_obj_t *chip, const char *txt);
void      kk_chip_set_color(lv_obj_t *chip, lv_color_t color);
void      kk_chip_pulse(lv_obj_t *chip);             /* 视觉动效：一次性脉冲 */

lv_obj_t *kk_arc_create(lv_obj_t *parent, kk_rect_t r, lv_color_t color);
void      kk_arc_set(lv_obj_t *arc, int pct);

lv_obj_t *kk_icon_create(lv_obj_t *parent, kk_rect_t r, int kind, lv_color_t color);
enum { KK_ICON_OVERVIEW = 0, KK_ICON_STORAGE, KK_ICON_NETWORK, KK_ICON_SYSTEM };

typedef struct { lv_obj_t *track; lv_obj_t *fill; int w; } kk_bar_t;
void kk_bar_create(kk_bar_t *b, lv_obj_t *parent, kk_rect_t r, lv_color_t color, int radius);
void kk_bar_set(kk_bar_t *b, float pct);             /* 0..100，能量渐变 */
void kk_bar_set_fixed(kk_bar_t *b, float pct, lv_color_t color);

typedef struct {
    lv_obj_t *bar;
    lv_obj_t *line;
    lv_chart_series_t *s_bar;
    lv_chart_series_t *s_line;
    int pts;
    int auto_range;      /* 1 = y 量程随数据自适应（无 top 绑定的趋势） */
} kk_trend_t;
void kk_trend_create(kk_trend_t *t, lv_obj_t *parent, kk_rect_t r,
                     lv_color_t color, int pts, int area_opa_pct, int grid_lines, int auto_range);
void kk_trend_sync(kk_trend_t *t, const float *vals, int count);   /* 全量同步环形采样 */
void kk_trend_range(kk_trend_t *t, float top);
int  kk_trend_peak(const kk_trend_t *t);

void kk_signal_bars(lv_obj_t *parent, kk_rect_t r, lv_obj_t *out[4]);
void kk_signal_set(lv_obj_t *bars[4], int8_t rssi);

/* ── 手势：PRESS/RELEASE 差分实现的左右滑（不依赖 LV_USE_GESTURE_RECOGNITION）── */
typedef void (*kk_swipe_cb_t)(void);
void kk_swipe_attach(lv_obj_t *obj, kk_swipe_cb_t on_left, kk_swipe_cb_t on_right);

/* ── Store 的 series 字段（定长环形采样）─────────────────────────── */
typedef struct {
    float *v;
    uint16_t cap;
    uint16_t head;
    uint16_t count;
} kk_series_t;
void kk_series_init(kk_series_t *s, float *buf, uint16_t cap);
void kk_series_push(kk_series_t *s, float v);

/* 趋势与 Store series 字段同步（按时间序整体重绘） */
void kk_trend_sync_series(kk_trend_t *t, const kk_series_t *s);
