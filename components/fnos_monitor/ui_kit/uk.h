// uk.h — ui_kit：LVGL 原生自适应 UI 层（替代 kk_ui 的绝对坐标构件）
//
// 设计前提（用户 m01672）：自适应第一优先；不用 kk_ui；全局用 LVGL 原生构件；保证动画。
//
// 与 kk_ui 的根本差别：
//   kk_ui  = 先算几何（kk_metrics_recompute 解出一堆坐标）再 mk_xx(x,y,w,h) 绝对摆放。
//   ui_kit = 只给"语义"（卡片 / 行 / 池 / KPI），几何交给 LVGL flex：
//            - 页面 = flex column，卡片 flex-grow 吃掉余量；
//            - 卡内 = flex column，头部固定、body flex-grow；
//            - 不定长清单 = 自适应池（uk_pool）：按可用宽高解列数，行 flex-grow 把余量变成行高。
//   于是"屏幕多大就用多少"由 LVGL 自己保证，不再需要主机侧解坐标；换分辨率不重算宏。
//
// 只有 LVGL 任务能调本层（红线：非 LVGL 任务只置标志位）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "uk_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Inventory metadata and formatted text can fail without stopping LVGL.
   Reset/read this flag on the UI thread for each complete refresh. */
void *uk_alloc(size_t size);
void *uk_realloc(void *ptr, size_t size);
void uk_alloc_reset(void);
bool uk_alloc_failed(void);
#ifdef FNOS_UI_TESTING
void uk_test_alloc_fail_after(int successful_allocations);
bool uk_test_alloc_was_triggered(void);
#endif

/* ── 间距阶梯（唯一来源：4/8/12/16/24/32，不许出现阶梯外的值） ───────── */
#define UK_S1 4
#define UK_S2 8
#define UK_S3 12
#define UK_S4 16
#define UK_S5 24
#define UK_S6 32

/* Distinct cards breathe more than their internal text; compact displays use one step less. */
#define UK_CARD_GAP (lv_display_get_vertical_resolution(NULL) < UK_SCR_H ? UK_S2 : UK_S3)
#define UK_ITEM_GAP (lv_display_get_vertical_resolution(NULL) < UK_SCR_H ? UK_S1 : UK_S2)

/* 12 栅格：一行之内的宽度份额。格子声明自己占几列，列距 = 卡片间距。
   栅格只管"同排谁宽谁窄"，行高仍由内容决定（要等高用 uk_grid_equalize）。 */
#define UK_GRID_COLS 12

/* ── 骨架：页根 / 外壳 ─────────────────────────────────────────────── */
/* 页根：满屏 flex column，底色 UK_BG。所有页面都从这里开始。 */
lv_obj_t *uk_screen(void);

/* 外壳：把页根切成 [顶栏][左 rail | 内容区][底部状态行]，返回内容区。
   rail/head/foot 可为 NULL（不需要）。内容区是 flex column、gap UK_S3、pad UK_S3。 */
lv_obj_t *uk_shell(lv_obj_t *scr, lv_obj_t **out_rail, lv_obj_t **out_head, lv_obj_t **out_foot);

/* 内容区里的行容器：flex row、gap UK_S3、flex-grow 1（= kk_layout 的"一行卡片"）。
   grow 传 0 表示按内容自然高（表头/提示行）。 */
lv_obj_t *uk_row_box(lv_obj_t *content, int32_t grow);

/* 12 栅格的一行：列距 UK_CARD_GAP、行高按内容（grow>0 时吃满剩余高度）。
   uk_grid_span() 让格子按"列数份额"分宽（flex grow 值即份额）；span<=0 视为 1。
   uk_grid_equalize() 把一行里可见格子的高度统一成本行最高者（不改宽度）。 */
lv_obj_t *uk_grid_row(lv_obj_t *parent, int32_t grow);
void uk_grid_span(lv_obj_t *item, int32_t span);
void uk_grid_equalize(lv_obj_t *row);

/* ── 卡片 ─────────────────────────────────────────────────────────── */
/* 卡片：flex column；返回卡片本身，body 用 uk_card_body()。
   title/note 可 NULL。note 显示在卡片右上角（"显示 3 / 6"这类）。 */
lv_obj_t *uk_card(lv_obj_t *parent, const char *title, const char *note);
lv_obj_t *uk_card_body(lv_obj_t *card);      /* flex column，gap UK_S2，flex-grow 1 */
lv_obj_t *uk_card_note(lv_obj_t *card);      /* 右上角说明 label（可 set_text） */
lv_obj_t *uk_card_title(lv_obj_t *card);
/* 卡片在行里的伸缩权重（默认 1）；传 0 = 不抢空间。 */
void uk_card_flex(lv_obj_t *card, int32_t grow);

/* ── 文本 ─────────────────────────────────────────────────────────── */
/* 常用组合：主文本 / 次文本 / 行标签 / 轴标签 / 数字。 */
lv_obj_t *uk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t hex, const char *txt);
void uk_set_text(lv_obj_t *label, const char *fmt, ...);   /* printf 风格，自动 lv_label_set_text */
void uk_label_ellipsis(lv_obj_t *label);                   /* 超宽省略号（默认不换行） */
lv_obj_t *uk_hairline(lv_obj_t *parent);                   /* 1px 分隔线，撑满宽 */
lv_obj_t *uk_dot(lv_obj_t *parent, uint32_t hex, int32_t d);  /* 状态点（圆形） */

/* ── 清单行（行 = 一个对象：设备/通道/卷/容器） ────────────────────── */
typedef struct {
    lv_obj_t *row;    /* 行容器：flex row，name 段 flex-grow，值段固定 */
    lv_obj_t *name1;  /* 主名（设备名，永不缩写：放不下换行） */
    lv_obj_t *name2;  /* 副名 / 通道名（可省略号） */
    lv_obj_t *val;    /* 数值（num 字体） */
    lv_obj_t *unit;   /* 单位 */
    lv_obj_t *led;    /* 状态点（NULL 表示不带） */
    lv_obj_t *bar;    /* 阈值条（NULL 表示不带） */
} uk_row_t;

/* 建一行。with_led/with_bar 决定行内是否带状态点与阈值条。
   行本身 flex-grow 1（余量长进行高时不压缩内容：内部两侧都居中）。 */
uk_row_t *uk_row_create(lv_obj_t *parent, bool with_led, bool with_bar);
/* 填一行；pct < 0 表示不画条；led_hex = 0 表示点用 UK_OFF。 */
void uk_row_set(uk_row_t *r, const char *l1, const char *l2,
                const char *val, const char *unit, int32_t pct, uint32_t led_hex);
void uk_row_show(uk_row_t *r, bool show);   /* data-fit：装不下就隐藏（不销毁） */

/* ── 自适应池（核心） ─────────────────────────────────────────────── */
/* 池：flex row，内部按需切成 N 列 .uk-col（每列 flex column）。
   条目先全部挂到池上（uk_row_create/uk_pool_add），最后调一次 uk_pool_relayout()：
     1) 量"自然行高"（最大条目高，两行行 > 一行行）；
     2) cols = ceil(n / floor(poolH / rowH))，再被 floor(poolW / min_col_w) 封顶；
     3) 收敛：列变窄会让设备名换行（行变高）→ 用"列内高度总和"判超，超了就少一列；
     4) 重挂进各列；行 flex-grow 1 ⇒ 余量平均变成行高（不留白、不出滚动条）。
   data_fit=true 时：仍装不下的条目不显示（uk_row_show(false)），
   并把"显示 N / M"写进 note_label（传 NULL 就不写），对应 HTML 原型的 data-fit。 */
void uk_pool_relayout(lv_obj_t *pool, int32_t min_col_w, bool data_fit, lv_obj_t *note_label);
lv_obj_t *uk_pool_create(lv_obj_t *parent, int32_t min_col_w);
void       uk_pool_stretch(lv_obj_t *pool, bool on);   /* false = 条目按自然高紧凑排，不平分余量 */
void uk_pool_add(lv_obj_t *pool, lv_obj_t *child);
int32_t uk_pool_shown(lv_obj_t *pool);
int32_t uk_pool_cols(lv_obj_t *pool);
/* 池的列数策略可覆盖（默认 0 = 自动）：>0 时强制该列数（不收敛）。 */
void uk_pool_force_cols(lv_obj_t *pool, int32_t cols);
/* 视口行对齐：容器纵向溢出时把"半行"赶出视口 —— 只调行距、不动行高、不藏数据
   （容器装得下时行距回到 base_gap）。base_gap = 本来要用的那个行距。 */
void uk_viewport_snap(lv_obj_t *box, int32_t base_gap);

/* ── KPI 格（标签 / 大数字 / 单位 / 副读数 / 阈值条） ──────────────── */
typedef struct {
    lv_obj_t *box, *label, *val, *unit, *sub, *bar;
} uk_kpi_t;
uk_kpi_t *uk_kpi_create(lv_obj_t *parent, const char *label);
void uk_kpi_set(uk_kpi_t *k, const char *val, const char *unit, const char *sub, int32_t pct);

/* ── 原生控件的轻封装（自适应尺寸交给 flex，动画交给 LVGL） ───────── */
/* 趋势图：原生 lv_chart（LINE），点数组在 PSRAM 分配；返回图表对象。 */
lv_obj_t *uk_trend_create(lv_obj_t *parent, int32_t points, int32_t series);
lv_chart_series_t *uk_trend_series(lv_obj_t *chart, int32_t idx);
void uk_trend_set_range(lv_obj_t *chart, int32_t ymin, int32_t ymax);
void uk_trend_push(lv_obj_t *chart, int32_t series_idx, int32_t v);

/* 数字键盘：原生 lv_buttonmatrix，回调收到按钮文字。 */
lv_obj_t *uk_numpad_create(lv_obj_t *parent, void (*cb)(const char *key, void *user), void *user);

/* ── 动画（"保证动画效果"：全部走 LVGL 原生 lv_anim） ─────────────── */
/* 只保留真在用的：换页淡入、严重告警呼吸、值条原生补间。
   （`uk_page_load()` 与 `uk_anim_int()` 已删——见 uk.c 里那段说明。） */
void uk_anim_pulse(lv_obj_t *obj, bool on);
/* 进场：从下方 dy 滑入 + 淡入（列表/卡片用，delay 支持错峰）。
   注意 dy≠0 时对象坐标会离开原位（LVGL 的 translate 会算进 refr_pos），
   贴边的卡片在这一帧就会被父对象裁掉——所以要动整块卡片时用 uk_anim_fade。 */
void uk_anim_enter(lv_obj_t *obj, int32_t dy, uint32_t ms, uint32_t delay_ms);
/* 淡入：只改 opa，不动坐标，父对象不会裁到它（换页入场用这个）。 */
void uk_anim_fade(lv_obj_t *obj, lv_opa_t from, uint32_t ms, uint32_t delay_ms);
/* 把子树里在飞的 opa 动画立刻落到终值（主机预览截"稳态帧"用；设备上不用调）。 */
void uk_anim_settle(lv_obj_t *root);
/* Draw panel outlines immediately; fill and content follow with a stagger. */
void uk_anim_reveal(lv_obj_t *panel, uint32_t ms, uint32_t delay_ms);
void uk_anim_reveal_settle(lv_obj_t *panel);
/* 值条：原生动画（LV_ANIM_ON），时长固定 180ms，避免每帧重排。 */
void uk_bar_set(lv_obj_t *bar, int32_t v);

/* ── 阈值配色（唯一来源，供条/点/数字复用） ──────────────────────── */
uint32_t uk_pct_color(int32_t pct);
uint32_t uk_temp_color(float c);

#ifdef __cplusplus
}
#endif
