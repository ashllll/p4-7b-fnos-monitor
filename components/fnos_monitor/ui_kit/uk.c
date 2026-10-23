// uk.c — ui_kit 实现：LVGL 原生 flex 的自适应布局层。
//
// 这里只有"怎么摆"，"摆什么"在 fnos_ui.c。所有几何都由 flex 决定；
// 唯一手写的尺寸是圆点直径、发丝线高度、条高（这些是"尺寸"不是"位置"）。
#include "uk.h"
#include "src/widgets/chart/lv_chart_private.h"
#include "src/core/lv_obj_class_private.h"
#include "src/misc/lv_area_private.h"
#include "third_party/d3_basis.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool s_alloc_failed;
/* 运行时精简动效；编译期 Kconfig 在 fnos_ui.c 侧叠加，不在这里覆盖。 */
static bool s_motion_reduced;
void uk_motion_set_reduced(bool reduced) { s_motion_reduced = reduced; }
bool uk_motion_reduced(void) { return s_motion_reduced; }
#ifdef FNOS_UI_TESTING
static int s_fail_after = -1;
static bool s_fail_triggered;
void uk_test_alloc_fail_after(int successful_allocations)
{
    s_fail_after = successful_allocations;
    s_fail_triggered = false;
}
bool uk_test_alloc_was_triggered(void) { return s_fail_triggered; }
static bool alloc_injected_failure(void)
{
    if (s_fail_after < 0) return false;
    if (s_fail_after-- != 0) return false;
    s_fail_triggered = true;
    return true;
}
#else
static bool alloc_injected_failure(void) { return false; }
#endif

void *uk_alloc(size_t size)
{
    void *ptr = alloc_injected_failure() ? NULL : lv_malloc(size);
    if (!ptr && size) s_alloc_failed = true;
    return ptr;
}

void *uk_realloc(void *ptr, size_t size)
{
    void *grown = alloc_injected_failure() ? NULL : lv_realloc(ptr, size);
    if (!grown && size) s_alloc_failed = true;
    return grown;
}

void uk_alloc_reset(void) { s_alloc_failed = false; }
bool uk_alloc_failed(void) { return s_alloc_failed; }

/* ── 内部：样式工具 ───────────────────────────────────────────────── */
static void style_surface(lv_obj_t *o, uint32_t bg, int32_t radius)
{
    lv_obj_set_style_bg_color(o, uk_c(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_card_edge(lv_obj_t *o)
{
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, uk_c(UK_LINE), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
}

/* michka 卡头两侧的白方块帽（CSS ::before/::after，4px）。 */
static void uk_cap(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 4, 4);
    lv_obj_set_style_bg_color(c, uk_c(UK_T3), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 0, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *mk_label_raw(lv_obj_t *parent, const lv_font_t *font, uint32_t hex)
{
    lv_obj_t *l = uk_number_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, uk_c(hex), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_min_width(l, 0, 0);   /* 允许在 flex 里被压缩（否则只会撑破父容器） */
    return l;
}

/* ── 骨架 ─────────────────────────────────────────────────────────── */
lv_obj_t *uk_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, uk_c(UK_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW);
    return scr;
}

lv_obj_t *uk_shell(lv_obj_t *scr, lv_obj_t **out_rail, lv_obj_t **out_head, lv_obj_t **out_foot)
{
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(scr);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(head, UK_HEAD_H, 0);
    style_surface(head, UK_FLAT_ROWS ? UK_SURF_S2 : UK_SURF_S1, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_border_color(head, uk_c(UK_LINE), 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(head, UK_S2, 0);
    lv_obj_set_style_pad_ver(head, UK_S1, 0);
    lv_obj_set_style_pad_column(head, UK_S2, 0);

    /* Full-width instrument workspace; page selection lives in the bottom dock. */
    lv_obj_t *workspace = lv_obj_create(scr);
    lv_obj_set_width(workspace, LV_PCT(100));
    lv_obj_set_flex_grow(workspace, 1);
    style_surface(workspace, UK_BG, 0);
    lv_obj_set_style_pad_all(workspace, 0, 0);
    lv_obj_set_style_pad_column(workspace, 0, 0);
    lv_obj_set_style_min_height(workspace, 0, 0);
    lv_obj_set_flex_flow(workspace, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(workspace, LV_OBJ_FLAG_SCROLLABLE);

    /* A solid, clipped viewport repaints exposed pixels during gestures on
       TRIPLE_PARTIAL; pages are siblings, outside flex, sharing its geometry. */
    lv_obj_t *content = lv_obj_create(workspace);
    lv_obj_set_width(content, 0);
    lv_obj_set_height(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_min_width(content, 0, 0);
    style_surface(content, UK_BG, 0);
    lv_obj_set_style_pad_all(content, UK_S2, 0);
    lv_obj_set_style_pad_row(content, 0, 0);
    lv_obj_set_style_min_height(content, 0, 0);
    /* 内容区不画滚动条：内容偶尔超出十几像素时，右缘那根近乎满高的竖条会被读成
       "右边还有一张卡被切掉了"。可滚能力保留（真超高时数据仍然够得着），
       只是不把这根条画到卡片右缘上。 */
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *foot = lv_obj_create(scr);
    lv_obj_set_size(foot, LV_PCT(100), LV_SIZE_CONTENT);
    style_surface(foot, UK_BG, 0);
    lv_obj_set_style_pad_hor(foot, UK_S2, 0);
    lv_obj_set_style_pad_ver(foot, UK_S1, 0);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(foot, UK_S2, 0);

    lv_obj_t *rail = lv_obj_create(scr);
    lv_obj_set_size(rail, LV_PCT(100), UK_DOCK_H);
    lv_obj_move_to_index(rail, 2); /* Content -> page dock -> event footer. */
    style_surface(rail, UK_FLAT_ROWS ? UK_NAV : UK_BG, 0);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_row(rail, UK_S1, 0);
    lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(rail, UK_S2, 0);
    lv_obj_set_style_pad_ver(rail, UK_S1, 0);
    lv_obj_add_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(rail, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(rail, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_column(rail, UK_PANEL_GAP, 0);

    if (out_rail) *out_rail = rail;
    if (out_head) *out_head = head;
    if (out_foot) *out_foot = foot;
    return content;
}

lv_obj_t *uk_row_box(lv_obj_t *content, int32_t grow)
{
    lv_obj_t *r = lv_obj_create(content);
    lv_obj_set_width(r, LV_PCT(100));
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_column(r, UK_PANEL_GAP, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    /* Rows are layout, not scroll surfaces: a 1px content overshoot used to paint a
       faint 3px scrollbar strip down the row's right edge (the stray hairlines on the
       network chart and inside cards). Nothing is reachable by scrolling a row anyway. */
    lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
    if (grow > 0) lv_obj_set_flex_grow(r, (uint8_t)grow);
    else          lv_obj_set_height(r, LV_SIZE_CONTENT);
    return r;
}

/* ── 12 栅格 ──────────────────────────────────────────────────────────
   “一排里谁宽谁窄”用列数份额表达：格子拿到 flex grow = 自己的列数，
   LVGL 对 grow 条目直接按 grow 值分剩余宽度（看不到内容宽），于是 span/12
   就是列宽比——不需要主机侧解坐标。行高仍由内容决定；要“同排等高”
   调 uk_grid_equalize()（只改高度，不改宽度）。 */
lv_obj_t *uk_grid_row(lv_obj_t *parent, int32_t grow)
{
    lv_obj_t *r = uk_row_box(parent, grow);
    lv_obj_set_style_pad_column(r, UK_CARD_GAP, 0);
    lv_obj_set_style_min_height(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

void uk_grid_span(lv_obj_t *item, int32_t span)
{
    if (!item) return;
    if (span < 1) span = 1;
    if (span > UK_GRID_COLS) span = UK_GRID_COLS;
    lv_obj_set_flex_grow(item, (uint8_t)span);
    lv_obj_set_style_min_width(item, 0, 0);
}

void uk_grid_equalize(lv_obj_t *row)
{
    if (!row) return;
    lv_obj_update_layout(row);
    int32_t max_h = 0;
    uint32_t n = lv_obj_get_child_count(row);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(row, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        int32_t h = lv_obj_get_height(c);
        if (h > max_h) max_h = h;
    }
    if (max_h <= 0) return;
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(row, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_get_height(c) != max_h) lv_obj_set_height(c, max_h);
    }
}

/* ── 卡片 ─────────────────────────────────────────────────────────── */
lv_obj_t *uk_card(lv_obj_t *parent, const char *title, const char *note)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    style_surface(c, UK_SURF_S1, UK_RADIUS);
    /* michka .box: linear-gradient(180deg, --f-bubble, --f-bubble-2) —— 纯色卡看着是"贴纸"，
       细微的竖向渐变才让它像一块浮起来的玻璃。 */
    lv_obj_set_style_bg_grad_color(c, uk_c(UK_SURF_T), 0);
    lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_VER, 0);
    style_card_edge(c);
    lv_obj_set_style_pad_hor(c, UK_CARD_PAD, 0);
    lv_obj_set_style_pad_ver(c, UK_CARD_PAD_V, 0);
    lv_obj_set_style_pad_row(c, UK_CONTENT_GAP, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_min_height(c, 0, 0);

    /* 头部：标题左、说明右（始终建，空的也建 —— 调用方可能后填） */
    lv_obj_t *head = lv_obj_create(c);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_set_style_pad_column(head, UK_S2, 0);
    lv_obj_set_style_pad_bottom(head, UK_S1, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(head, 1, 0);      /* michka：卡头下面一条细线 */
    lv_obj_set_style_border_color(head, uk_c(UK_LINE), 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* michka 的卡头：居中的大字距标签，两侧各一个 4px 白方块帽（CSS ::before/::after）。
       帽子仍是头行的直接子对象，标题保持"头行第 2 个孩子"的既有结构契约；
       note 用一个不参与排版的浮动标签钉在右上角，免得把居中算偏。 */
    uk_cap(head);
    lv_obj_t *tl = mk_label_raw(head, UK_TYPE_CAPTION, UK_T3);
    lv_obj_set_style_text_letter_space(tl, 1, 0);
    lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(tl, 0);
    lv_label_set_text(tl, title ? title : "");   /* 不给就空着，别留 LVGL 默认的 "Text" */
    uk_cap(head);

    /* note 留在排版里而不是浮在右上角：浮动标签会和居中的标题压在一起
       （预览审计按像素查重叠，压了就是错）。michka 本来是"帽·标题·帽"整组居中，
       这里把右上角说明接在右帽之后，整组仍然居中。 */
    lv_obj_t *nt = mk_label_raw(head, UK_TYPE_CAPTION, UK_T4);
    lv_obj_set_style_text_align(nt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_flex_grow(nt, 0);
    lv_label_set_text(nt, note ? note : "");

    /* 头行连一个字都没有（title/note 都空）时整行收起：空 label 也有字体行高，
       白吃 30px —— 卡片按内容定高时这 30px 直接就是池少掉的一行。
       两个 getter 仍然返回这两个 label，语义不变。 */
    if ((!title || !title[0]) && (!note || !note[0])) lv_obj_add_flag(head, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *body = lv_obj_create(c);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_row(body, UK_CONTENT_GAP, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_min_height(body, 0, 0);

    lv_obj_set_user_data(c, body);   /* 取 body 不用再数子对象 */
    return c;
}

lv_obj_t *uk_card_body(lv_obj_t *card) { return (lv_obj_t *)lv_obj_get_user_data(card); }
lv_obj_t *uk_card_title(lv_obj_t *card)
{
    lv_obj_t *h = lv_obj_get_child(card, 0);
    return h ? lv_obj_get_child(h, 1) : NULL;   /* 0=左帽 1=标题 2=右帽 3=note */
}
lv_obj_t *uk_card_note(lv_obj_t *card)
{
    lv_obj_t *h = lv_obj_get_child(card, 0);
    return h ? lv_obj_get_child(h, 3) : NULL;
}

void uk_card_flex(lv_obj_t *card, int32_t grow)
{
    /* 横排里"长高"= 高度按父容器 100%（LVGL 的 flex 没有 cross 轴 stretch，靠百分比）。
       body 的 grow 必须跟着走：卡按内容定高时，body 也 grow 的话它分不到余量，
       会被算成 0 高 —— 卡片看着有框、里面一个字都不显示（踩过）。 */
    lv_obj_t *body = uk_card_body(card);
    if (grow > 0) {
        lv_obj_set_flex_grow(card, (uint8_t)grow);
        lv_obj_set_height(card, LV_PCT(100));
        if (body) { lv_obj_set_flex_grow(body, 1); lv_obj_set_height(body, LV_SIZE_CONTENT); }
    } else {
        lv_obj_set_flex_grow(card, 0);
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        if (body) { lv_obj_set_flex_grow(body, 0); lv_obj_set_height(body, LV_SIZE_CONTENT); }
    }
}

/* ── 文本与装饰 ───────────────────────────────────────────────────── */
lv_obj_t *uk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t hex, const char *txt)
{
    lv_obj_t *l = mk_label_raw(parent, font, hex);
    if (txt) lv_label_set_text(l, txt);
    return l;
}

void uk_reading_align_baseline(lv_obj_t *value, lv_obj_t *unit)
{
    const lv_font_t *number_font = lv_obj_get_style_text_font(value, 0);
    const lv_font_t *unit_font = lv_obj_get_style_text_font(unit, 0);
    lv_obj_set_style_pad_bottom(unit, LV_MAX(0, number_font->base_line - unit_font->base_line), 0);
}

void uk_set_text(lv_obj_t *label, const char *fmt, ...)
{
    if (!label) return;
    va_list ap, measure;
    va_start(ap, fmt); va_copy(measure, ap);
    int n=vsnprintf(NULL,0,fmt,measure);
    va_end(measure);
    char *b=n>=0 ? uk_alloc((size_t)n+1) : NULL;
    if (b) {
        vsnprintf(b,(size_t)n+1,fmt,ap);
        uk_number_set_text(label,b,false);
        lv_free(b);
    }
    va_end(ap);
}

void uk_label_ellipsis(lv_obj_t *label)
{
    if (label) lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
}

lv_obj_t *uk_hairline(lv_obj_t *parent)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_height(l, 1);
    lv_obj_set_style_bg_color(l, uk_c(UK_LINE), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_radius(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    return l;
}

lv_obj_t *uk_dot(lv_obj_t *parent, uint32_t hex, int32_t d)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, uk_c(hex), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* ── 清单行 ───────────────────────────────────────────────────────── */
/* Row order: identity + right-aligned reading, then optional metadata and bar.
   Full names wrap within the width left by the reading; metadata wraps below. */
static lv_obj_t *mk_line(lv_obj_t *parent, int32_t gap)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_height(l, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_pad_column(l, gap, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    /* "行"是排版盒子，不是滚动区：默认 SCROLLABLE 会让它内容超高时长出一条
       竖向滚动条（行里冒出滚动条既不是设计，也会挡住"子对象跑出父对象"审计）。 */
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    return l;
}

uk_row_t *uk_row_create(lv_obj_t *parent, bool with_led, bool with_bar)
{
    uk_row_t *r = uk_alloc(sizeof *r);
    if (!r) return NULL;
    memset(r, 0, sizeof *r);
    const bool separated = lv_obj_get_style_pad_row(parent, 0) > 0;
    const bool tile = separated && !UK_FLAT_ROWS;

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, uk_c(UK_SURF_T), 0);
    lv_obj_set_style_bg_opa(row, tile ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(row, tile ? UK_RADIUS_SM : 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    /* Inventory and Wi-Fi rows share the same insets and touch floor. */
    lv_obj_set_style_pad_hor(row, UK_ROW_PAD_H, 0);
    lv_obj_set_style_pad_ver(row, UK_ROW_PAD_V, 0);
    lv_obj_set_style_pad_row(row, UK_CONTENT_GAP, 0);
    /* 1px 下边线 = 清单分隔线（也是行与行之间的视觉间距）；
       行高只有 1px 的代价，比 pad_row 便宜，24 路才能一屏排下。 */
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, tile ? LV_BORDER_SIDE_FULL : LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, uk_c(UK_LINE), 0);
    lv_obj_set_style_border_opa(row, LV_OPA_60, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_min_height(row, UK_ROW_MIN, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    r->row = row;

    lv_obj_t *line1 = mk_line(row, UK_S2);
    if (with_led) r->led = uk_dot(line1, UK_OFF, UK_DOT);

    lv_obj_t *namebox = lv_obj_create(line1);
    lv_obj_set_flex_grow(namebox, 1);
    lv_obj_set_height(namebox, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(namebox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(namebox, 0, 0);
    lv_obj_set_style_pad_all(namebox, 0, 0);
    lv_obj_set_flex_flow(namebox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(namebox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_min_width(namebox, 0, 0);
    lv_obj_clear_flag(namebox, LV_OBJ_FLAG_SCROLLABLE);

    r->name1 = mk_label_raw(namebox, UK_TYPE_BODY, UK_T2);
    /* 列表里一行裁切；完整名称进详情。长名若换行会把行高撑过点击目标，
       还会把后面的清单顶出卡外。 */
    lv_label_set_long_mode(r->name1, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(r->name1, LV_PCT(100));

    lv_obj_t *line2 = mk_line(row, UK_S2);
    r->name2 = mk_label_raw(line2, UK_TYPE_CAPTION, UK_T3);
    lv_obj_set_flex_grow(r->name2, 1);
    lv_obj_set_width(r->name2, LV_PCT(100));
    /* Metadata retains its full text; the pool measures the wrapped height. */
    lv_label_set_long_mode(r->name2, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(r->name2, LV_SIZE_CONTENT);

    lv_obj_t *valbox = lv_obj_create(line2);
    lv_obj_set_width(valbox, LV_SIZE_CONTENT);   /* 不写就是 lv_obj 的默认 130（LV_DPI_DEF）。
                                                   130px 会从行里白吃掉一块：名字列被压到 141px
                                                   （"已用 2.0 TB / 总计 3.6 T…"就是这么被截的）。 */
    lv_obj_set_height(valbox, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(valbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(valbox, 0, 0);
    lv_obj_set_style_pad_all(valbox, 0, 0);
    lv_obj_set_style_pad_column(valbox, UK_S1, 0);
    lv_obj_set_flex_flow(valbox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(valbox, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_min_width(valbox, 0, 0);
    lv_obj_clear_flag(valbox, LV_OBJ_FLAG_SCROLLABLE);

    r->val  = mk_label_raw(valbox, UK_TYPE_READING, UK_T1);
    lv_label_set_long_mode(r->val, LV_LABEL_LONG_MODE_CLIP);
    r->unit = mk_label_raw(valbox, UK_TYPE_UNIT, UK_T3);
    uk_reading_align_baseline(r->val, r->unit);

    if (with_bar) {
        r->bar = lv_bar_create(row);
        lv_obj_set_width(r->bar, LV_PCT(100));
        lv_obj_set_height(r->bar, UK_BAR_H);
        lv_bar_set_range(r->bar, 0, 100 * UK_METER_SCALE);
        lv_obj_set_style_radius(r->bar, UK_BAR_H / 2, 0);
        lv_obj_set_style_bg_color(r->bar, uk_c(UK_SURF_S3), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(r->bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(r->bar, uk_c(UK_OK), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(r->bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(r->bar, UK_BAR_H / 2, LV_PART_INDICATOR);
        lv_obj_set_style_anim_duration(r->bar, UK_BAR_ANIM_MS, LV_PART_MAIN);
    }
    lv_obj_set_style_pad_column(row, UK_S3, 0);
    return r;
}

void uk_row_set(uk_row_t *r, const char *l1, const char *l2,
                const char *val, const char *unit, int32_t pct, uint32_t led_hex)
{
    if (!r) return;
    const bool two = (l2 && l2[0]);
    /* "还是同一行"只看名字和单位。第二行装的是**读数**（"已用 5.4 TB / 总计 7.3 TB"、
       "读 12 MB/s · 写 3 MB/s"），它每秒都在变，把它算进身份判定，val 的过渡就永远
       进不来——存储页的卷百分比/阵列状态一直是这样被写死的（2026-10-09 实测：
       p1 过渡帧与终帧逐像素相同，p0/p2/p3/p4 都有 250 ms 滑动）。 */
    const bool same_reading = !strcmp(lv_label_get_text(r->unit), unit ? unit : "") &&
        !strcmp(lv_label_get_text(r->name1), l1 ? l1 : "");

    uk_set_text(r->name1, "%s", l1 ? l1 : "");
    /* 第二行同样是 number label（mk_label_raw），没理由比 val 少一层动效：
       卷容量、阵列成员、磁盘读写速率都要逐字符滑动。 */
    uk_number_set_text(r->name2, two ? l2 : "", true);
    uk_number_set_text(r->val,val ? val : "",same_reading);
    uk_set_text(r->unit, "%s", unit ? unit : "");

    /* Identity and value share the first line; metadata always occupies the next. */
    lv_obj_t *line1 = lv_obj_get_child(r->row, 0);
    lv_obj_t *line2 = lv_obj_get_child(r->row, 1);
    lv_obj_t *valbox = lv_obj_get_parent(r->val);
    lv_obj_t *want = line1;
    if (valbox && lv_obj_get_parent(valbox) != want) lv_obj_set_parent(valbox, want);
    if (line2) {
        if (two) lv_obj_remove_flag(line2, LV_OBJ_FLAG_HIDDEN);
        else     lv_obj_add_flag(line2, LV_OBJ_FLAG_HIDDEN);
    }

    if (r->led) lv_obj_set_style_bg_color(r->led, uk_c(led_hex ? led_hex : UK_OFF), 0);
    if (r->bar) {
        if (pct < 0) lv_obj_add_flag(r->bar, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_remove_flag(r->bar, LV_OBJ_FLAG_HIDDEN);
            uk_bar_set(r->bar, pct);
            lv_obj_set_style_bg_color(r->bar, uk_c(uk_pct_color(pct)), LV_PART_INDICATOR);
        }
    }
}

void uk_row_show(uk_row_t *r, bool show)
{
    if (!r || !r->row) return;
    if (show) lv_obj_remove_flag(r->row, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
}

/* ── 自适应池 ─────────────────────────────────────────────────────── */
#define UK_COL_FLAG LV_OBJ_FLAG_USER_1      /* 标在列容器上：和"条目"区分开 */

typedef struct {
    int32_t min_col_w, force_cols, cols, shown, no_stretch;
} uk_pool_t;

static void pool_delete_cb(lv_event_t *e)
{
    lv_free(lv_obj_get_user_data(lv_event_get_target(e)));
}

void uk_pool_stretch(lv_obj_t *pool, bool on)
{
    uk_pool_t *st = pool ? lv_obj_get_user_data(pool) : NULL;
    if (st) st->no_stretch = !on;
}

lv_obj_t *uk_pool_create(lv_obj_t *parent, int32_t min_col_w)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_flex_grow(p, 1);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_pad_column(p, UK_ITEM_GAP, 0);
    lv_obj_set_style_pad_row(p, UK_ITEM_GAP, 0);
    lv_obj_set_style_pad_right(p, UK_SCROLL_INSET + UK_S2, 0);
    lv_obj_set_style_width(p, UK_SCROLL_W, LV_PART_SCROLLBAR);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_min_height(p, 0, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_USER_2);
    uk_list_mark(p);   /* 池建出来就是清单：默认承诺 UK_LIST_MIN_ROWS 项，见 uk_list_mark_one */
    uk_pool_t *st = lv_malloc(sizeof *st);
    LV_ASSERT_MALLOC(st);
    memset(st, 0, sizeof *st);
    st->min_col_w = min_col_w > 0 ? min_col_w : UK_LIST_MIN_WIDTH;
    st->no_stretch = 1;
    lv_obj_set_user_data(p, st);
    lv_obj_add_event_cb(p, pool_delete_cb, LV_EVENT_DELETE, NULL);
    return p;
}

void uk_pool_add(lv_obj_t *pool, lv_obj_t *child) { lv_obj_set_parent(child, pool); }
void uk_pool_force_cols(lv_obj_t *pool, int32_t cols)
{ uk_pool_t *st=lv_obj_get_user_data(pool); if (st) { st->force_cols=cols; st->cols=0; } }
int32_t uk_pool_cols(lv_obj_t *pool)
{ uk_pool_t *st=lv_obj_get_user_data(pool); return st ? st->cols : 0; }
int32_t uk_pool_shown(lv_obj_t *pool)
{ uk_pool_t *st=lv_obj_get_user_data(pool); return st ? st->shown : 0; }

/* ── 清单可读性下限 ───────────────────────────────────────────────────
   池和滚动列建出来就带标记（见 uk_pool_create / fnos_ui.c 的 scroll_col），
   默认承诺 UK_LIST_MIN_ROWS 项；条目是可变高复合块的清单用 uk_list_mark_one()
   把承诺降到一整项。 */
void uk_list_mark(lv_obj_t *list)     { if (list) lv_obj_add_flag(list, UK_LIST_FLAG); }
void uk_list_mark_one(lv_obj_t *list) { if (list) lv_obj_add_flag(list, UK_LIST_FLAG | UK_LIST_ONE_FLAG); }
bool uk_list_is(lv_obj_t *o) { return o && lv_obj_has_flag(o, UK_LIST_FLAG); }
bool uk_pool_is(lv_obj_t *o) { return o && lv_obj_has_flag(o, LV_OBJ_FLAG_USER_2); }
int32_t uk_list_promise(lv_obj_t *list)
{ return (list && lv_obj_has_flag(list, UK_LIST_ONE_FLAG)) ? 1 : UK_LIST_MIN_ROWS; }

/* 清单里最高的一条（没有可见条目时 0）。单项就比视口高的块清单靠它豁免下限审计。 */
int32_t uk_list_item_max(lv_obj_t *list)
{
    int32_t item=0;
    if (!list) return 0;
    for (uint32_t i=0;i<lv_obj_get_child_count(list);i++) {
        lv_obj_t *it=lv_obj_get_child(list,i);
        if (lv_obj_has_flag(it,LV_OBJ_FLAG_HIDDEN)) continue;
        item=LV_MAX(item,lv_obj_get_height(it));
    }
    return item;
}

/* 至少完整露出前面 items 项需要多高。只用**条目高度**和列数算，不用坐标：
   重排之后（尤其是非活动页）条目的 coords 可能还是上一次布局的旧值，而高度是新的。
   项高按"最高条目"保守取，行距取容器当前行距（viewport_snap 撑大过就算撑大后的）。
   条目本来就没那么多项时返回内容总高 —— 空清单返回 0，不会误报。 */
int32_t uk_list_readable_min(lv_obj_t *list, int32_t items)
{
    if (!list || items <= 0) return 0;
    int32_t item=uk_list_item_max(list), seen=0;
    if (!item) return 0;
    for (uint32_t i=0;i<lv_obj_get_child_count(list);i++)
        if (!lv_obj_has_flag(lv_obj_get_child(list,i),LV_OBJ_FLAG_HIDDEN)) seen++;
    int32_t cols=uk_pool_is(list) ? LV_MAX(1, uk_pool_cols(list)) : 1;
    int32_t total=(seen+cols-1)/cols;
    int32_t want=LV_MIN(items,total);
    return want*item+(want-1)*lv_obj_get_style_pad_row(list,0);
}

/* 全部条目摊开需要的高度（"内容定高"里的 content）。 */
int32_t uk_list_content_min(lv_obj_t *list) { return uk_list_readable_min(list, 0x7fffffff); }

/* ── 视口行对齐：把"半行"赶出视口 ──────────────────────────────────────
   滚动容器（自适应池 / 事件列 / 卷清单）内容超出视口时，底边常把最后一行切成半截：
   屏幕上就是"字被拦腰截断"，比"内容没显示全"更容易被读成画错了。

   做法：不动行高（行高由内容决定，拉高只会让表格显得空），只把**行距**撑到刚好让
   "能整行放下的那几行"铺满视口，下一行整条落到视口外。数据一条都没藏，仍然滚得
   到；容器装得下时行距回到 base_gap。

   只读子对象的高度与 y 分组、不读当前 pad_row，所以反复调用结果一致。
   base_gap = 调用方本来要用的行距（池 UK_ITEM_GAP、事件列/卷清单 UK_S1~UK_S3）。 */
#define UK_SNAP_MAX_ROWS 64
void uk_viewport_snap(lv_obj_t *box, int32_t base_gap)
{
    if (!box || base_gap < 0) return;
    uint32_t n = lv_obj_get_child_count(box);
    if (n < 2) return;
    lv_obj_update_layout(box);
    int32_t pad_top = lv_obj_get_style_pad_top(box, 0);
    int32_t h_avail = lv_obj_get_content_height(box);
    if (h_avail <= 0) return;
    /* 按 y 分组成行：水平 flex 换行（池）与每行一个对象（清单列）都成立。 */
    int32_t row_h[UK_SNAP_MAX_ROWS];
    int32_t rows = 0, y_prev = 0;
    for (uint32_t i = 0; i < n && rows < UK_SNAP_MAX_ROWS; i++) {
        lv_obj_t *c = lv_obj_get_child(box, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        int32_t y = lv_obj_get_y(c) - pad_top;
        int32_t h = lv_obj_get_height(c);
        if (h <= 0) continue;
        if (rows == 0 || y != y_prev) { row_h[rows] = h; y_prev = y; rows++; }
        else if (h > row_h[rows - 1]) row_h[rows - 1] = h;
    }
    if (rows < 2) return;
    int32_t used = 0, k = 0;
    for (int32_t r = 0; r < rows; r++) {
        int32_t add = row_h[r] + (k ? base_gap : 0);
        if (used + add > h_avail) break;
        used += add; k++;
    }
    int32_t g_new = base_gap;
    if (k < rows && k >= 1) {
        g_new = (k == 1) ? h_avail - used
                         : (h_avail - used + k - 2) / (k - 1);   /* ceil */
        if (g_new < base_gap) g_new = base_gap;
    }
    if (lv_obj_get_style_pad_row(box, 0) == g_new) return;
    lv_obj_set_style_pad_row(box, g_new, 0);
    lv_obj_update_layout(box);
    /* 复查：每个子对象要么整个在视口里、要么整个在视口外。不成立说明这份几何不是
       "调行距"能治的（同一行里行高差得远），退回基准行距 —— 宁可露半行。 */
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(box, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        int32_t y = lv_obj_get_y(c) - pad_top;
        int32_t h = lv_obj_get_height(c);
        if (y < h_avail && y + h > h_avail) {
            lv_obj_set_style_pad_row(box, base_gap, 0);
            lv_obj_update_layout(box);
            return;
        }
    }
}

/* Row-major cards wrap at the available width. Counts and heights are data,
   and every item remains reachable through one vertical viewport. */
void uk_pool_relayout(lv_obj_t *pool, int32_t min_col_w, bool data_fit, lv_obj_t *note)
{
    (void)data_fit; /* Inventory views never omit the tail to fill one screen. */
    uk_pool_t *st=lv_obj_get_user_data(pool);
    if (!st) return;
    if (min_col_w>0) st->min_col_w=min_col_w;
    int32_t base_gap=UK_ITEM_GAP;
    /* 行距可能被上一次的行对齐撑大过（uk_viewport_snap）：先回到基准，
       下面的"几行装得下"必须按基准行距算，否则列数会越算越少。 */
    if (lv_obj_get_style_pad_row(pool,0)!=base_gap)
        lv_obj_set_style_pad_row(pool,base_gap,0);
    lv_obj_update_layout(pool);
    int32_t n=(int32_t)lv_obj_get_child_count(pool);
    int32_t w=lv_obj_get_content_width(pool);
    if (w<=0) return;
    int32_t gap=lv_obj_get_style_pad_column(pool,0);
    int32_t preferred=st->min_col_w;
    /* Unbroken numeric text must fit alongside a readable identity fragment.
       Names and descriptions themselves use natural multi-line wrapping. */
    for (int32_t i=0;i<n;i++) {
        lv_obj_t *it=lv_obj_get_child(pool,i);
        uint32_t lines=lv_obj_get_child_count(it);
        for (uint32_t j=0;j<lines;j++) {
            lv_obj_t *line=lv_obj_get_child(it,j);
            uint32_t parts=lv_obj_get_child_count(line);
            if (parts<2) continue;
            lv_obj_t *last=lv_obj_get_child(line,-1);
            if (lv_obj_get_style_width(last,0)!=LV_SIZE_CONTENT) continue;
            int32_t need=lv_obj_get_width(last)+2*UK_S2+UK_DOT+
                         4*lv_font_get_line_height(UK_FONT_CJK_12);
            if (need>preferred) preferred=need;
        }
    }
    int32_t cols=(w+gap)/(preferred+gap);
    if (cols<1) cols=1;
    if (st->force_cols>0) cols=st->force_cols;
    if (n && cols>n) cols=n;
    /* 宽度允许的最多列数：每列至少 min_col_w 宽（再宽就不好读了）。 */
    int32_t cols_max=(w+gap)/(st->min_col_w+gap);
    if (cols_max<1) cols_max=1;
    if (n && cols_max>n) cols_max=n;
    if (st->force_cols>0 || cols_max<cols) cols_max=cols;
    int32_t cw=(w-gap*(cols-1))/cols;
    for (int32_t i=0;i<n;i++) {
        lv_obj_t *it=lv_obj_get_child(pool,i);
        lv_obj_remove_flag(it,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_flex_grow(it,0);
        lv_obj_set_style_min_height(it,UK_ROW_MIN,0);
        lv_obj_set_width(it,cw);
        lv_obj_set_height(it,LV_SIZE_CONTENT);

    }
    lv_obj_update_layout(pool);
    /* 一屏装得下优先：行数放不下时加列（列越多每行越矮），到宽度上限为止。
       12 路通道排在 3 列里要 4 行、卡里只放得下 2 行时，屏幕上就是"半行被切"，
       而右边还空着一半 —— 加一列比露半行好。列窄了设备名会换行、行会变高，
       所以每加一列都要重新量行高，不能一次算死。装不下也不藏数据：
       剩下的行交给滚动，半行交给 uk_viewport_snap()。 */
    int32_t row_h=0;
    for (int32_t i=0;i<n;i++) {
        int32_t ih=lv_obj_get_height(lv_obj_get_child(pool,i));
        if (ih>row_h) row_h=ih;
    }
    int32_t h_avail=lv_obj_get_content_height(pool);
    while (cols<cols_max && row_h>0 && h_avail>0) {
        int32_t rows=(n+cols-1)/cols;
        if (rows*(row_h+base_gap)-base_gap<=h_avail) break;
        cols++;
        cw=(w-gap*(cols-1))/cols;
        for (int32_t i=0;i<n;i++) lv_obj_set_width(lv_obj_get_child(pool,i),cw);
        lv_obj_update_layout(pool);
        row_h=0;
        for (int32_t i=0;i<n;i++) {
            int32_t ih=lv_obj_get_height(lv_obj_get_child(pool,i));
            if (ih>row_h) row_h=ih;
        }
    }
    lv_obj_update_layout(pool);
    /* 池里所有条目统一成一个行高：行高不一致时 uk_viewport_snap 只能退回基准
       行距，卡底那行就会被切一半。统一之后行距摊得匀，露出来的都是整行。 */
    {
        int32_t row_h=0;
        for (int32_t i=0;i<n;i++) {
            int32_t ih=lv_obj_get_height(lv_obj_get_child(pool,i));
            if (ih>row_h) row_h=ih;
        }
        if (row_h>0) {
            for (int32_t i=0;i<n;i++) {
                if (lv_obj_get_height(lv_obj_get_child(pool,i))!=row_h)
                    lv_obj_set_height(lv_obj_get_child(pool,i),row_h);
            }
            lv_obj_update_layout(pool);
        }
    }
    st->cols=cols; st->shown=n;
    if (note) uk_set_text(note,"%d / %d",n,n);
    uk_viewport_snap(pool,base_gap);
}

/* ── KPI ──────────────────────────────────────────────────────────── */
uk_kpi_t *uk_kpi_create(lv_obj_t *parent, const char *label)
{
    uk_kpi_t *k = uk_alloc(sizeof *k);
    if (!k) return NULL;
    memset(k, 0, sizeof *k);
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_height(box, LV_PCT(100));
    style_surface(box, UK_SURF_S1, UK_RADIUS);
    style_card_edge(box);
    lv_obj_set_style_pad_all(box, UK_CARD_PAD, 0);
    lv_obj_set_style_pad_row(box, UK_CONTENT_GAP, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_min_width(box, 0, 0);
    k->box = box;

    k->label = mk_label_raw(box, UK_TYPE_TITLE, UK_T3);
    lv_label_set_text(k->label, label ? label : "");

    lv_obj_t *vr = lv_obj_create(box);
    lv_obj_set_width(vr, LV_PCT(100));
    lv_obj_set_height(vr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(vr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vr, 0, 0);
    lv_obj_set_style_pad_all(vr, 0, 0);
    lv_obj_set_style_pad_column(vr, UK_S1, 0);
    lv_obj_set_flex_flow(vr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    /* The reading is one atom: the unit sits right after the value. Letting the
       value grow pushed "MB/s" to the far edge of the card, where it read as a
       separate element instead of the number's unit. */
    k->val  = mk_label_raw(vr, UK_TYPE_METRIC, UK_T1);
    /* A measured minimum preserves the unit anchor across decimal carries;
       unusually large readings can still expand instead of being truncated. */
    lv_point_t reading_size;
    lv_text_get_size(&reading_size, "100.0", UK_TYPE_METRIC,
                     lv_obj_get_style_text_letter_space(k->val, 0), 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_style_min_width(k->val, reading_size.x, 0);
    lv_obj_set_style_text_align(k->val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(k->val, LV_LABEL_LONG_MODE_CLIP);
    k->unit = mk_label_raw(vr, UK_TYPE_CAPTION, UK_T3);
    uk_reading_align_baseline(k->val, k->unit);

    k->sub = mk_label_raw(box, UK_TYPE_CAPTION, UK_T3);
    lv_obj_set_width(k->sub, LV_PCT(100));
    lv_obj_set_flex_grow(k->sub, 1);
    lv_obj_set_style_text_align(k->sub, LV_TEXT_ALIGN_LEFT, 0);

    k->bar = lv_bar_create(box);
    lv_obj_set_width(k->bar, LV_PCT(100));
    lv_obj_set_height(k->bar, UK_BAR_H);
    lv_bar_set_range(k->bar, 0, 100 * UK_METER_SCALE);
    lv_obj_set_style_radius(k->bar, UK_BAR_H / 2, 0);
    lv_obj_set_style_bg_color(k->bar, uk_c(UK_SURF_S3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(k->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(k->bar, uk_c(UK_OK), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(k->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(k->bar, UK_BAR_ANIM_MS, LV_PART_MAIN);
    return k;
}

void uk_kpi_set(uk_kpi_t *k, const char *val, const char *unit, const char *sub, int32_t pct)
{
    if (!k) return;
    const bool same_unit = !strcmp(lv_label_get_text(k->unit),unit ? unit : "");
    uk_number_set_reading(k->val, k->unit, val, unit, true);
    uk_number_set_text(k->sub,sub ? sub : "",same_unit);
    if (k->bar) {
        if (pct < 0) lv_obj_add_flag(k->bar, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_remove_flag(k->bar, LV_OBJ_FLAG_HIDDEN);
            uk_bar_set(k->bar, pct);
            lv_obj_set_style_bg_color(k->bar, uk_c(uk_pct_color(pct)), LV_PART_INDICATOR);
        }
    }
}

/* ── 原生控件封装 ─────────────────────────────────────────────────── */

/* Native XY charts let the time axis advance without moving the grid or labels.
   Fixed-point sample positions provide subpixel motion at any viewport width. */
enum { TREND_UNIT = 256, TREND_PROGRESS = 65536 };
typedef struct {
    lv_chart_t chart;
    int32_t right, from, target;
    uint32_t last_paint, started, duration;
    bool sampled, flowing;
} trend_t;

static void trend_step(void *obj, int32_t progress);
typedef struct {
    float x, y, xscale, yscale;
    int32_t xmin, ymax;
} trend_projection_t;

static trend_projection_t trend_projection(lv_obj_t *obj, lv_chart_series_t *s)
{
    lv_chart_t *chart = (lv_chart_t *)obj;
    float dx = (float)((int64_t)chart->xmax[s->x_axis_sec] - chart->xmin[s->x_axis_sec]);
    float dy = (float)((int64_t)chart->ymax[s->y_axis_sec] - chart->ymin[s->y_axis_sec]);
    lv_area_t content; lv_obj_get_content_coords(obj, &content);
    return (trend_projection_t){
        .x = content.x1 - lv_obj_get_scroll_left(obj),
        .y = content.y1 - lv_obj_get_scroll_top(obj),
        .xscale = dx > 0 ? lv_obj_get_content_width(obj) / dx : 0,
        .yscale = dy > 0 ? lv_obj_get_content_height(obj) / dy : 0,
        .xmin = chart->xmin[s->x_axis_sec], .ymax = chart->ymax[s->y_axis_sec]
    };
}

static bool trend_project(lv_chart_t *chart, lv_chart_series_t *s, uint32_t index,
                          const trend_projection_t *map, d3_basis_point_t *out)
{
    if (index >= chart->point_cnt || map->xscale <= 0 || map->yscale <= 0) return false;
    uint32_t at = (s->start_point + index) % chart->point_cnt;
    if (s->y_points[at] == LV_CHART_POINT_NONE) return false;
    out->x = map->x + (float)((int64_t)s->x_points[at] - map->xmin) * map->xscale;
    out->y = map->y + (float)((int64_t)map->ymax - s->y_points[at]) * map->yscale;
    return true;
}

static bool trend_point(lv_obj_t *obj, lv_chart_series_t *s, uint32_t index,
                        d3_basis_point_t *out)
{
    trend_projection_t map = trend_projection(obj, s);
    return trend_project((lv_chart_t *)obj, s, index, &map, out);
}
static bool trend_segment(lv_obj_t *obj, lv_chart_series_t *s, uint32_t index,
                           d3_basis_segment_t *curve, d3_basis_point_t *a, d3_basis_point_t *b)
{
    if (!trend_point(obj, s, index, a) || !trend_point(obj, s, index + 1, b)) return false;
    d3_basis_point_t previous = *a, next = *b;
    if (index) trend_point(obj, s, index - 1, &previous);
    trend_point(obj, s, index + 2, &next);
    *curve = d3_basis_segment(previous, *a, *b, next);
    return true;
}
/* LVGL copies submitted points; batches bound the stack and share draw-task
   allocation/dispatch across many segments without limiting history length. */
enum { TREND_DRAW_POINTS = 64 };
typedef struct {
    lv_draw_line_dsc_t line;
    lv_point_precise_t points[TREND_DRAW_POINTS];
    unsigned count;
} trend_batch_t;

static void trend_batch_flush(trend_batch_t *batch)
{
    if (batch->count > 1) {
        batch->line.points = batch->points;
        batch->line.point_cnt = batch->count;
        lv_draw_line(batch->line.base.layer, &batch->line);
    }
    batch->count = 0;
}

static void trend_batch_point(trend_batch_t *batch, d3_basis_point_t p)
{
    lv_point_precise_t point = {lroundf(p.x), lroundf(p.y)};
    unsigned n = batch->count;
    if (n && batch->points[n-1].x == point.x && batch->points[n-1].y == point.y) return;
    if (n == TREND_DRAW_POINTS) {
        lv_point_precise_t tail = batch->points[n-1];
        trend_batch_flush(batch);
        batch->points[batch->count++] = tail;
    }
    batch->points[batch->count++] = point;
}

static void trend_draw_series(lv_obj_t *obj, lv_layer_t *layer, lv_chart_series_t *series)
{
    trend_batch_t batch = {0};
    lv_draw_line_dsc_init(&batch.line);
    lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &batch.line);
    batch.line.base.layer = layer;
    batch.line.color = series->color;
    if (!batch.line.width || batch.line.opa <= LV_OPA_MIN) return;
    trend_projection_t map = trend_projection(obj, series);
    unsigned max_steps = (unsigned)LV_MAX(1, 2*lv_obj_get_content_width(obj));
    d3_basis_point_t previous = {0}, a = {0}, b = {0}, next = {0};
    bool have_previous = false;
    bool have_a = trend_project((lv_chart_t *)obj, series, 0, &map, &a);
    bool have_b = trend_project((lv_chart_t *)obj, series, 1, &map, &b);
    for (uint32_t index = 0; index + 1 < ((lv_chart_t *)obj)->point_cnt; index++) {
        bool have_next = trend_project((lv_chart_t *)obj, series, index + 2, &map, &next);
        if (have_a && have_b) {
            d3_basis_segment_t c = d3_basis_segment(have_previous ? previous : a,
                                                   a, b, have_next ? next : b);
            if (!have_previous) trend_batch_point(&batch, a);
            float bend = 0;
            for (unsigned i = 0; i < 2; i++) {
                bend = fmaxf(bend, fabsf(c.p[i].x - 2*c.p[i+1].x + c.p[i+2].x));
                bend = fmaxf(bend, fabsf(c.p[i].y - 2*c.p[i+1].y + c.p[i+2].y));
            }
            /* Original 1/4-pixel chord bound, also during page transitions. */
            unsigned steps = (unsigned)fmaxf(1, ceilf(sqrtf(3*bend)));
            steps = LV_MIN(steps, max_steps);
            for (unsigned j = 0; j <= steps; j++)
                trend_batch_point(&batch, d3_basis_at(c, (float)j/steps));
            if (!have_next) trend_batch_point(&batch, b);
        } else trend_batch_flush(&batch); /* Missing samples never bridge. */
        previous = a; a = b; b = next;
        have_previous = have_a; have_a = have_b; have_b = have_next;
    }
    trend_batch_flush(&batch);
}

static void trend_event(const lv_obj_class_t *cls, lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_chart_t *chart = (lv_chart_t *)obj;
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN || !((trend_t *)obj)->flowing) {
        lv_obj_event_base(cls, e);
        return;
    }
    /* Keep native background, grid and attached cursors, bypass only the
       per-segment scatter renderer. The public chart remains SCATTER. */
    lv_chart_type_t type = chart->type;
    chart->type = LV_CHART_TYPE_NONE;
    lv_result_t result = lv_obj_event_base(cls, e);
    chart->type = type;
    if (result != LV_RESULT_OK) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t saved = layer->_clip_area, clip;
    if (!lv_area_intersect(&clip, &obj->coords, &saved)) return;
    layer->_clip_area = clip;
    lv_chart_series_t *series;
    LV_LL_READ_BACK(&chart->series_ll, series)
        if (!series->hidden) trend_draw_series(obj, layer, series);
    layer->_clip_area = saved;
}
static void trend_cursors(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_chart_t *chart = (lv_chart_t *)obj;
    lv_chart_cursor_t *cursor;
    /* Resolve the visible line head before native LVGL draws its cursors.
       Only geometry is interpolated; the sampled series remains untouched. */
    LV_LL_READ(&chart->cursor_ll, cursor) {
        lv_chart_series_t *s = cursor->ser;
        cursor->dir = LV_DIR_NONE;
        if (!s || s->hidden || !chart->point_cnt) continue;
        uint32_t at = (s->start_point + chart->point_cnt - 1) % chart->point_cnt;
        if (s->y_points[at] == LV_CHART_POINT_NONE) continue;
        lv_point_t head;
        lv_chart_get_point_pos_by_id(obj, s, at, &head);
        d3_basis_point_t projected;
        bool flowing = ((trend_t *)obj)->flowing;
        /* Native point lookup clamps future X to the viewport edge. The curve
           head needs its true X so clipping resolves the visible cubic. */
        if (flowing && trend_point(obj, s, chart->point_cnt - 1, &projected))
            head = (lv_point_t){lroundf(projected.x) - obj->coords.x1,
                                lroundf(projected.y) - obj->coords.y1};
        int32_t right = lv_obj_get_style_pad_left(obj, LV_PART_MAIN) +
                        lv_obj_get_content_width(obj);
        for (uint32_t i = 1; head.x > right && i < chart->point_cnt; i++) {
            at = (at + chart->point_cnt - 1) % chart->point_cnt;
            if (s->y_points[at] == LV_CHART_POINT_NONE) break;
            lv_point_t previous;
            lv_chart_get_point_pos_by_id(obj, s, at, &previous);
            uint32_t index = (at + chart->point_cnt - s->start_point) % chart->point_cnt;
            if (flowing && trend_point(obj, s, index, &projected))
                previous = (lv_point_t){lroundf(projected.x) - obj->coords.x1,
                                        lroundf(projected.y) - obj->coords.y1};
            if (previous.x <= right && head.x > previous.x) {
                d3_basis_segment_t curve;
                d3_basis_point_t a, b;
                float x = obj->coords.x1 + right;
                if (flowing && trend_segment(obj, s, index, &curve, &a, &b)) {
                    if (x < curve.p[0].x && index) {
                        /* This point belongs to the preceding cubic. */
                        trend_segment(obj, s, index - 1, &curve, &a, &b);
                    }
                    float y;
                    if (x < curve.p[0].x) {
                        float width = curve.p[0].x - a.x;
                        y = width > 0 ? a.y + (curve.p[0].y - a.y) * (x - a.x)/width : a.y;
                    } else if (x > curve.p[3].x) {
                        float width = b.x - curve.p[3].x;
                        y = width > 0 ? curve.p[3].y + (b.y - curve.p[3].y) *
                            (x - curve.p[3].x)/width : b.y;
                    } else {
                        float lo = 0, hi = 1;
                        for (unsigned k = 0; k < 16; k++) {
                            float mid = (lo + hi)/2;
                            if (d3_basis_at(curve, mid).x < x) lo = mid; else hi = mid;
                        }
                        y = d3_basis_at(curve, (lo + hi)/2).y;
                    }
                    head.y = lroundf(y) - obj->coords.y1;
                } else head.y = previous.y + (int64_t)(head.y - previous.y) *
                    (right - previous.x) / (head.x - previous.x);
                head.x = right;
                break;
            }
            head = previous;
        }
        if (head.x > right || head.x < lv_obj_get_style_pad_left(obj, LV_PART_MAIN)) continue;
        cursor->pos = head;
        cursor->pos_set = true;
        cursor->color = s->color;
        cursor->dir = LV_DIR_RIGHT; /* Point enabled, crosshair hidden by style. */
    }
}
static void trend_destroy(const lv_obj_class_t *cls, lv_obj_t *obj)
{
    (void)cls;
    lv_anim_delete(obj, trend_step);
}
static const lv_obj_class_t trend_class = {
    .base_class = &lv_chart_class, .event_cb = trend_event, .destructor_cb = trend_destroy,
    .instance_size = sizeof(trend_t), .name = "uk_streaming_trend"
};

static int32_t trend_span(lv_obj_t *chart)
{
    /* One guard point remains just outside the left edge during a shift. */
    uint32_t points = lv_chart_get_point_count(chart);
    return (int32_t)(points > 2 ? points - 2 : 1) * TREND_UNIT;
}
static void trend_window(lv_obj_t *chart, int32_t right)
{
    trend_t *t = (trend_t *)chart;
    t->right = right;
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_X, right - trend_span(chart), right);
}
static void trend_step(void *obj, int32_t progress)
{
    trend_t *t = obj;
    uint32_t now = lv_tick_get();
    if (!lv_obj_is_visible(obj) || UK_NUMBER_REDUCED) {
        trend_window(obj, t->target);
        t->duration = 0;
        lv_anim_delete(obj, trend_step);
        return;
    }
    /* Use fixed time slots: restarting a 33/16 ms wait at each paint would
       quantize a 15 ms LVGL cadence down to 22/33 FPS. */
    if (progress != TREND_PROGRESS && UK_TREND_FPS > 0 &&
        (uint64_t)now * UK_TREND_FPS / 1000U ==
        (uint64_t)t->last_paint * UK_TREND_FPS / 1000U) return;
    t->last_paint = now;
    trend_window(obj, t->from + (int32_t)((int64_t)(t->target - t->from) * progress / TREND_PROGRESS));
    if (progress == TREND_PROGRESS) t->duration = 0;
}
void uk_trend_settle(lv_obj_t *chart)
{
    if (!chart || !lv_obj_has_class(chart, &trend_class)) return;
    trend_t *t = (trend_t *)chart;
    lv_anim_delete(chart, trend_step);
    trend_window(chart, t->target);
    t->duration = 0;
}
void uk_trend_clear(lv_obj_t *chart)
{
    if (!chart) return;
    uk_trend_settle(chart);
    uint32_t count = lv_chart_get_point_count(chart);
    for (lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL); s;
         s = lv_chart_get_series_next(chart, s)) {
        lv_chart_set_all_values(chart, s, LV_CHART_POINT_NONE);
        int32_t *x = lv_chart_get_series_x_array(chart, s);
        if (x) memset(x, 0, count * sizeof *x);
    }
    trend_t *t = (trend_t *)chart;
    t->from = t->target = 0;
    t->sampled = false;
    trend_window(chart, 0);
}
static void trend_rebase(lv_obj_t *chart)
{
    trend_t *t = (trend_t *)chart;
    int32_t span = trend_span(chart);
    if (t->target <= 4 * span) return;
    int32_t shift = t->target - span;
    uint32_t count = lv_chart_get_point_count(chart);
    for (lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL); s;
         s = lv_chart_get_series_next(chart, s)) {
        int32_t *x = lv_chart_get_series_x_array(chart, s);
        for (uint32_t i = 0; i < count; i++) x[i] = LV_MAX((int64_t)x[i] - shift, -span);
    }
    t->from -= shift;
    t->target -= shift;
    trend_window(chart, t->right - shift);
}
lv_obj_t *uk_trend_create(lv_obj_t *parent, int32_t points, int32_t series)
{
    lv_obj_t *c = lv_obj_class_create_obj(&trend_class, parent);
    if (!c) return NULL;
    lv_obj_class_init_obj(c);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_line_width(c, UK_TREND_STROKE, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(c, true, LV_PART_ITEMS);
    lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);      /* 只要线，不要点 */
    lv_obj_set_style_size(c, 3 * UK_TREND_STROKE, 3 * UK_TREND_STROKE, LV_PART_CURSOR);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_border_width(c, 1, LV_PART_CURSOR);
    lv_obj_set_style_border_color(c, uk_c(UK_T1), LV_PART_CURSOR);
    lv_obj_set_style_line_opa(c, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_add_event_cb(c, trend_cursors, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_line_color(c, uk_c(UK_LINE), LV_PART_MAIN);
    lv_obj_set_style_line_opa(c, LV_OPA_40, LV_PART_MAIN);
    /* SCATTER uses native straight line segments with explicit time positions. */
    lv_chart_set_type(c, LV_CHART_TYPE_SCATTER);
    lv_chart_set_point_count(c, (uint32_t)points);
    lv_chart_set_div_line_count(c, 2, 4);
    const uint32_t series_colors[] = { UK_S_DOWN, UK_S_UP, UK_S_TEMP };
    for (int32_t i = 0; i < series; i++) {
        /* LVGL owns both rings. The hidden ring remains an exact source for
           current/peak/quality calculations; only the second ring is drawn. */
        lv_chart_series_t *raw = lv_chart_add_series(c, uk_c(series_colors[i % 3]), LV_CHART_AXIS_PRIMARY_Y);
        if (raw) lv_chart_hide_series(c, raw, true);
        lv_chart_series_t *line = lv_chart_add_series(c, uk_c(series_colors[i % 3]), LV_CHART_AXIS_PRIMARY_Y);
        if (line) {
            lv_chart_cursor_t *cursor = lv_chart_add_cursor(c, line->color, LV_DIR_NONE);
            if (cursor) lv_chart_set_cursor_point(c, cursor, line, LV_CHART_POINT_NONE);
        }
    }
    uk_trend_clear(c);
    return c;
}

void uk_trend_enable_flow(lv_obj_t *chart)
{
    if (!chart || !lv_obj_has_class(chart, &trend_class) || ((trend_t *)chart)->flowing) return;
    ((trend_t *)chart)->flowing = true;
    lv_obj_invalidate(chart);
}

/* LVGL 不提供"按索引取序列"，只有 next 遍历 —— 自己走一遍链 */
static lv_chart_series_t *series_at(lv_obj_t *chart, int32_t idx)
{
    lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL);
    for (int32_t i = 0; i < idx && s; i++) s = lv_chart_get_series_next(chart, s);
    return s;
}

lv_chart_series_t *uk_trend_series(lv_obj_t *chart, int32_t idx)
{
    return idx < 0 ? NULL : series_at(chart, 2 * idx + 1);
}

lv_chart_series_t *uk_trend_raw_series(lv_obj_t *chart, int32_t idx)
{
    return idx < 0 ? NULL : series_at(chart, 2 * idx);
}

void uk_trend_set_range(lv_obj_t *chart, int32_t ymin, int32_t ymax)
{
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, ymin, ymax);
}

void uk_trend_push(lv_obj_t *chart, int32_t series_idx, int32_t v, uint32_t interval_ms)
{
    lv_chart_series_t *raw = uk_trend_raw_series(chart, series_idx);
    lv_chart_series_t *line = uk_trend_series(chart, series_idx);
    if (!raw || !line) return;
    trend_rebase(chart);
    trend_t *t = (trend_t *)chart;
    uint32_t points = lv_chart_get_point_count(chart);
    uint32_t previous = (lv_chart_get_x_start_point(chart, raw) + points - 1) % points;
    bool continuous = v != LV_CHART_POINT_NONE &&
        lv_chart_get_series_y_array(chart, raw)[previous] != LV_CHART_POINT_NONE;
    int32_t x = LV_MAX(lv_chart_get_series_x_array(chart, raw)[previous] + TREND_UNIT, t->target);
    lv_chart_set_next_value2(chart, raw, x, v);
    if (v != LV_CHART_POINT_NONE) {
        uint32_t points = lv_chart_get_point_count(chart);
        uint32_t at = lv_chart_get_x_start_point(chart, raw);
        const int32_t *values = lv_chart_get_series_y_array(chart, raw);
        int64_t sum = 0;
        uint32_t count = 0;
        uint32_t window = t->flowing ? UK_FLOW_SMOOTH_SAMPLES : UK_TREND_SMOOTH_SAMPLES;
        /* Two moving-average passes equal a triangular FIR. Evaluate against
           the existing raw ring; no extra state, rounding pass or gap leakage. */
        window = LV_MIN(points, t->flowing ? 2 * window - 1 : window);
        for (uint32_t i = 0; i < window; i++) {
            at = (at + points - 1) % points;
            if (values[at] == LV_CHART_POINT_NONE) break;
            uint32_t weight = t->flowing ? LV_MIN(i + 1, window - i) : 1;
            sum += (int64_t)values[at] * weight;
            count += weight;
        }
        v = (int32_t)(sum / count); /* The current valid sample guarantees count > 0. */
    }
    lv_chart_set_next_value2(chart, line, x, v);
    if (x <= t->target) return; /* Other series share the same time advance. */
    bool animate = t->sampled && continuous && points > 2 &&
        interval_ms > 0 && interval_ms <= UK_TREND_SCROLL_MS && UK_TREND_FPS > 0 &&
        !UK_NUMBER_REDUCED && lv_obj_is_visible(chart);
    t->sampled = true;
    /* Retarget from elapsed time, even if the last paint was skipped. Otherwise
       each throttled frame adds permanent lag to the live time window. */
    t->from = t->duration ? t->from + (int32_t)((int64_t)(t->target - t->from) *
        LV_MIN(lv_tick_elaps(t->started), t->duration) / t->duration) : t->right;
    t->target = x;
    t->duration = 0;
    lv_anim_delete(chart, trend_step);
    if (!animate) { trend_window(chart, x); return; }
    trend_window(chart, t->from);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, chart);
    lv_anim_set_values(&a, 0, TREND_PROGRESS);
    /* A new sample can arrive before the preceding travel finishes. Preserve
       velocity by scaling duration with remaining distance, rather than restart
       a full interval and decelerate when LVGL consumes a sample early. */
    uint32_t duration = (uint32_t)(((int64_t)t->target - t->from) * interval_ms / TREND_UNIT);
    t->duration = LV_MAX(duration, 1);
    t->started = lv_tick_get();
    lv_anim_set_duration(&a, t->duration);
    lv_anim_set_exec_cb(&a, trend_step);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    if (!lv_anim_start(&a)) { trend_window(chart, x); t->duration = 0; }
}

/* ── 数字键盘 ─────────────────────────────────────────────────────── */
static void numpad_event_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target(e);
    void (*cb)(const char *, void *) = (void (*)(const char *, void *))lv_event_get_user_data(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(bm);
    const char *txt = lv_buttonmatrix_get_button_text(bm, id);
    /* 回调是"按键文字 + 用户指针"两参，这里的 user_data 存的是用户指针，
       所以真正的用户指针通过父对象的 user_data 传（见 uk_numpad_create 注释）。 */
    void *user = lv_obj_get_user_data(lv_obj_get_parent(bm));
    if (cb && txt) cb(txt, user);
}

lv_obj_t *uk_numpad_create(lv_obj_t *parent, void (*cb)(const char *key, void *user), void *user)
{
    static const char *map[] = {
        "1", "2", "3", "\n",
        "4", "5", "6", "\n",
        "7", "8", "9", "\n",
        LV_SYMBOL_CLOSE, "0", LV_SYMBOL_BACKSPACE, ""
    };
    lv_obj_t *bm = lv_buttonmatrix_create(parent);
    lv_obj_set_width(bm, LV_PCT(100));
    lv_obj_set_flex_grow(bm, 1);
    lv_buttonmatrix_set_map(bm, map);
    lv_buttonmatrix_set_button_ctrl_all(bm, LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bm, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bm, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(bm, UK_S2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bm, uk_c(UK_SURF_S2), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(bm, UK_RADIUS_SM, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bm, uk_c(UK_T1), LV_PART_ITEMS);
    lv_obj_set_style_text_font(bm, UK_FONT_NUM_20, LV_PART_ITEMS);
    /* 键高下限 44：触摸目标不能低于它（LVGL 用 padding 撑，这里给最小高度） */
    lv_obj_set_style_min_height(bm, UK_ROW_MIN, LV_PART_ITEMS);
    lv_obj_set_user_data(lv_obj_get_parent(bm), user);   /* 见 numpad_event_cb */
    lv_obj_add_event_cb(bm, numpad_event_cb, LV_EVENT_VALUE_CHANGED, (void *)cb);
    return bm;
}

/* ── 动画 ─────────────────────────────────────────────────────────── */
/* 这里只留**真在用**的三件：换页淡入（uk_anim_fade）、严重告警呼吸（uk_anim_pulse）、
   值条原生补间（uk_bar_set）。曾经还有两个 API 备而不用，已删：
   `uk_page_load()`（`lv_screen_load_anim`——本板是"一个 screen + 五个隐藏页"，
   根本没有第二个 screen 可 load，留着只会诱导误用）和 `uk_anim_int()`
   （数值补间：值条的观感已由 `uk_bar_set` 的原生动画给到，KPI 文字是格式化字符串，
   没有调用点；将来真要做数字滚动再按需加回来，别提前备着）。 */
void uk_bar_set(lv_obj_t *bar, int32_t v)
{
    if (bar) uk_meter_set(bar, LV_CLAMP(0, v, 100) * UK_METER_SCALE, true);
}

static void opa_anim_cb(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

void uk_anim_pulse(lv_obj_t *obj, bool on)
{
    if (!obj) return;
    lv_anim_delete(obj, opa_anim_cb);
    if (!on) { lv_obj_set_style_opa(obj, LV_OPA_COVER, 0); return; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_COVER, 90);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_playback_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_start(&a);
}

typedef struct { int32_t dy; uint32_t opa_to; } uk_enter_t;

static void enter_anim_cb(void *o, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)o;
    lv_obj_set_style_translate_y(obj, v, 0);
}

void uk_anim_enter(lv_obj_t *obj, int32_t dy, uint32_t ms, uint32_t delay_ms)
{
    if (!obj) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, dy, 0);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_path_cb(&a, uk_anim_path_damped);
    lv_anim_set_exec_cb(&a, enter_anim_cb);
    lv_anim_start(&a);
}

void uk_anim_fade(lv_obj_t *obj, lv_opa_t from, uint32_t ms, uint32_t delay_ms)
{
    if (!obj) return;
    lv_anim_delete(obj, opa_anim_cb);          /* 同一对象只留一条 opa 动画 */
    if (from >= LV_OPA_COVER) { lv_obj_set_style_opa(obj, LV_OPA_COVER, 0); return; }
    /* 先落到起点：lv_anim 的 delay 期间不会回调，不预置就会先全亮一帧再变暗。 */
    lv_obj_set_style_opa(obj, from, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, LV_OPA_COVER);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_path_cb(&a, uk_anim_path_damped);
    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_start(&a);
}

static void reveal_anim_cb(void *obj, int32_t value)
{
    lv_obj_t *panel = obj;
    lv_obj_set_style_bg_opa(panel, (lv_opa_t)value, 0);
    lv_obj_set_style_border_opa(panel, UK_EDGE_OPA + (LV_OPA_60 - UK_EDGE_OPA) * (255 - value) / 255, 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(panel); i++)
        lv_obj_set_style_opa(lv_obj_get_child(panel, i), (lv_opa_t)value, 0);
}

void uk_anim_reveal(lv_obj_t *panel, uint32_t ms, uint32_t delay_ms)
{
    lv_anim_delete(panel, reveal_anim_cb);
    reveal_anim_cb(panel, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_values(&a, 0, LV_OPA_COVER);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_path_cb(&a, uk_anim_path_damped);
    lv_anim_set_exec_cb(&a, reveal_anim_cb);
    lv_anim_start(&a);
}

void uk_anim_reveal_settle(lv_obj_t *panel)
{
    if (!panel || !lv_anim_get(panel, reveal_anim_cb)) return;
    lv_anim_delete(panel, reveal_anim_cb);
    reveal_anim_cb(panel, LV_OPA_COVER);
}

void uk_anim_settle(lv_obj_t *root)
{
    if (!root) return;
    /* 在飞的 opa 动画一律落到终值 255：主机预览靠它拿到"稳态帧"。
       不这么做就得推进虚拟时钟，而时钟一推，挂在 500ms 上的 ui_tick 也跟着走，
       数据年龄/轮询计数就漂了，快照不再可复现。 */
    if (lv_anim_get(root, opa_anim_cb)) {
        lv_anim_delete(root, opa_anim_cb);
        lv_obj_set_style_opa(root, LV_OPA_COVER, 0);
    }
    uk_meter_settle(root);
    uk_number_settle(root);
    uk_anim_reveal_settle(root);
    uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; i++) uk_anim_settle(lv_obj_get_child(root, i));
}
