#pragma once
// 仪表盘视觉层：设计令牌 + 复用构件（"是什么颜色/多大/怎么画"，不含业务数据）。
// 设计合同见 docs/ui-redesign.md；页面代码只允许用这里的令牌与构件，
// 不要在页面函数里散落十六进制颜色或魔法坐标。
#include "lvgl.h"
#include "fnos_fonts.h"
#include <stdbool.h>
#include <stdint.h>

/* ── 颜色令牌 ───────────────────────────────────────────────────────── */
#define CK_BG        0x06080B   // 底色（近黑）
#define CK_PANEL     0x0E1116   // 对象底
#define CK_PANEL_HI  0x161C24   // 选中/激活底
#define CK_NAV_SEL   0x22303E   // 导航选中底（比 PANEL_HI 亮一档，斜视/照片下可辨）
#define CK_GUIDE     0x25313E   // 参考线、非焦点描边
#define CK_TEXT      0xF5F7FA
#define CK_DIM       0x8A93A3
#define CK_IDLE      0x4A5260   // 休眠/不可用

#define CK_CPU       0x3FA9F5   // 身份色：跨页稳定
#define CK_MEM       0x37C4D6
#define CK_TEMP      0xFF7A1A
#define CK_NET_DOWN  0x37C4D6
#define CK_NET_UP    0x3FA9F5
#define CK_ZFS       0xC8E63C

#define CK_OK        0x36D27E   // 严重度色：只表达健康程度
#define CK_WARN      0xFFC800
#define CK_DANGER    0xFF4057

/* ── 版面令牌 ───────────────────────────────────────────────────────── */
#define CK_SCR_W   1024
#define CK_SCR_H   600
#define CK_TOP_H   64
#define CK_RAIL_W  104
#define CK_BOT_H   40
#define CK_PAD     20
#define CK_GAP     14
#define CK_CONT_X  (CK_RAIL_W + CK_PAD)
#define CK_CONT_Y  (CK_TOP_H + 16)
#define CK_CONT_W  (CK_SCR_W - CK_CONT_X - CK_PAD)
#define CK_CONT_H  (CK_SCR_H - CK_BOT_H - CK_CONT_Y - 16)
#define CK_RADIUS  14

/* ── 字号令牌 ───────────────────────────────────────────────────────── */
#define CK_F_HERO   (&ui_font_mono_84)
#define CK_F_NUML   (&ui_font_mono_52)
#define CK_F_NUMM   (&ui_font_mono_34)
#define CK_F_NUMS   (&ui_font_mono_20)
#define CK_F_TITLE  (&ui_font_sans_24)
#define CK_F_LABEL  (&ui_font_sans_20)
#define CK_F_META   (&ui_font_sans_15)
#define CK_F_CVERDICT (&ui_font_cjk_40)
#define CK_F_CTITLE (&ui_font_cjk_24)
#define CK_F_CLABEL (&ui_font_cjk_20)
#define CK_F_CMETA  (&ui_font_cjk_15)

/* ── 基础构件 ───────────────────────────────────────────────────────── */
lv_obj_t *ck_obj(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius, bool clickable);
lv_obj_t *ck_tile(lv_obj_t *parent, int x, int y, int w, int h);          // 对象底（panel + 1px 描边 + 圆角）
lv_obj_t *ck_label(lv_obj_t *parent, const char *txt, const lv_font_t *f, uint32_t color, int x, int y);
lv_obj_t *ck_label_r(lv_obj_t *parent, const char *txt, const lv_font_t *f, uint32_t color, int right_pad, int y);
lv_obj_t *ck_divider(lv_obj_t *parent, int x, int y, int w, uint32_t color);
void      ck_set(lv_obj_t *l, const char *txt);                            // NULL 安全
void      ck_set_color(lv_obj_t *l, uint32_t color);
void      ck_set_opa(lv_obj_t *o, lv_opa_t opa);                           // 陈旧数据降强调用

/* ── 进度条（能量渐变） ─────────────────────────────────────────────── */
typedef struct { lv_obj_t *track; lv_obj_t *fill; int w; } ck_bar_t;
void ck_bar_create(ck_bar_t *b, lv_obj_t *parent, int x, int y, int w, int h);
void ck_bar_set(ck_bar_t *b, float pct);                                   // 按百分比自动选渐变档
void ck_bar_set_color(ck_bar_t *b, float pct, uint32_t fixed);             // 固定色（身份色）
void ck_bar_tick(lv_obj_t *parent, int x, int y, int h);                   // 阈值刻度（80% / 90% 由调用方定位）

/* ── 弧表 ───────────────────────────────────────────────────────────── */
lv_obj_t *ck_arc(lv_obj_t *parent, int x, int y, int size, uint32_t color);
void      ck_arc_set(lv_obj_t *arc, int pct);

/* ── 状态胶囊 ───────────────────────────────────────────────────────── */
lv_obj_t *ck_chip(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);
void      ck_chip_set(lv_obj_t *chip, const char *txt, uint32_t color);
void      ck_chip_pulse(lv_obj_t *chip);                                   // 一次性进入动画（告警用）

/* ── 趋势：低透明度柱（面积感）+ 亮线 ───────────────────────────────── */
typedef struct {
    lv_obj_t *bar;                 // 面积层
    lv_obj_t *line;                // 亮线层
    lv_chart_series_t *s_bar;
    lv_chart_series_t *s_line;
    int pts;
} ck_trend_t;

void ck_trend_create(ck_trend_t *t, lv_obj_t *parent, int x, int y, int w, int h,
                     uint32_t color, int pts);
void ck_trend_push(ck_trend_t *t, int v);
void ck_trend_range(ck_trend_t *t, int top);                               // 纵轴上限（0 = 底部基线）
int  ck_trend_peak(const ck_trend_t *t);                                   // 跳过 LV_CHART_POINT_NONE

/* ── Wi-Fi 信号条 ───────────────────────────────────────────────────── */
void ck_signal_bars(lv_obj_t *parent, int x, int y, lv_obj_t *out[4]);
void ck_signal_set(lv_obj_t *bars[4], int8_t rssi);

/* ── 图标（纯几何绘制，避免依赖图标字体） ───────────────────────────── */
enum { CK_ICON_OVERVIEW = 0, CK_ICON_STORAGE, CK_ICON_NETWORK, CK_ICON_SYSTEM };
void ck_icon(lv_obj_t *parent, int x, int y, int kind, uint32_t color);
