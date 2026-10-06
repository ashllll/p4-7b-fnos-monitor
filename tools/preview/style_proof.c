// style_proof.c —— 风格试片（主机预览专用，不参与固件构建）
//
// 目的：在"设备端同款 Kconfig + RGB565 + 全屏刷新"的渲染路径上，把候选的
// "科技风"手法一次画出来并出图，用眼睛定夺，而不是靠描述猜。
// 试片只验证渲染可行性（LVGL 软渲染下阴影/渐变/发光/发丝边/网格是否成立、
// 在 RGB565 量化后还剩多少对比度），定稿后再落到 kk_theme.h / kk_widgets.c。
//
// 用法：./build/style_proof out     → out/style-proof.ppm（再由 ppm2png.py 转 PNG）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lvgl.h"
#include "kk_theme.h"
#include "kk_widgets.h"
#include "fnos_fonts.h"

#define SCR_W 1024
#define SCR_H 600

/* ── 渲染通道（与 preview.c 一致：RGB565 全屏，取回 s_frame）── */
static uint16_t s_frame[SCR_W * SCR_H];
static uint32_t s_vms;
static uint32_t vtick_get(void) { return s_vms; }
static void     vdelay(uint32_t ms) { s_vms += ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    const uint16_t *src = (const uint16_t *)px_map;
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++)
            s_frame[y * SCR_W + x] = *src++;
    lv_display_flush_ready(d);
}

static void snap(const char *dir)
{
    lv_refr_now(lv_display_get_default());
    char path[512];
    snprintf(path, sizeof path, "%s/style-proof.ppm", dir);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        uint16_t v = s_frame[i];
        fputc((((v >> 11) & 0x1F) * 255) / 31, f);
        fputc((((v >>  5) & 0x3F) * 255) / 63, f);
        fputc((( v        & 0x1F) * 255) / 31, f);
    }
    fclose(f);
    printf("  %s\n", path);
}

/* ── 候选令牌（定稿后迁入 kk_theme.h）────────────────────────────── */
#define TH_S0      0x0E0F11   /* 更深一阶：嵌槽 / 指标格底 */
#define TH_GRID    0x4A90FF   /* 背景网格线（品牌蓝系，低不透明度）*/
#define TH_RAISE_T 0x272A2F
#define TH_RAISE_B 0x17191C
#define TH_LINE    0x2E3238

/* ── 小工具 ─────────────────────────────────────────────────────── */
static lv_obj_t *lbl(lv_obj_t *p, int x, int y, int w, int h, const lv_font_t *f,
                     uint32_t rgb, const char *txt)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, txt);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t rgb, int r, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h, bool raised, lv_opa_t hair)
{
    lv_obj_t *c = box(p, x, y, w, h, raised ? TH_RAISE_T : KK_S1, KK_RADIUS, LV_OPA_COVER);
    if (raised) {
        lv_obj_set_style_bg_grad_color(c, lv_color_hex(TH_RAISE_B), 0);
        lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_VER, 0);
    }
    if (hair) {
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(c, hair, 0);
    }
    return c;
}

int main(int argc, char **argv)
{
    const char *outdir = argc > 1 ? argv[1] : "out";
    mkdir(outdir, 0755);

    lv_init();
    lv_tick_set_cb(vtick_get);
    lv_delay_set_cb(vdelay);
    lv_display_t *d = lv_display_create(SCR_W, SCR_H);
    static uint16_t buf[SCR_W * SCR_H];
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(KK_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 上排：表面处理四档（v8 采用的是第 1 档）──────────────────── */
    const int sx[4] = { 24, 274, 524, 774 };
    const lv_opa_t hair[4] = { KK_EDGE_OPA, 0, 26, 45 };
    const bool raised[4] = { true, true, false, false };
    const char *st[4] = { "v8 采用：渐变+白10%", "纯色（v7 旧样）", "渐变+白10%", "渐变+白18%" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = card(scr, sx[i], 24, 226, 150, raised[i], hair[i]);
        if (!raised[i]) {  /* 旧样：纯 KK_S1，没有渐变 */
            lv_obj_set_style_bg_color(c, lv_color_hex(KK_S1), 0);
            lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_NONE, 0);
        }
        lbl(c, 14, 10, 198, 22, &ui_font_cjk_12, KK_T2, st[i]);
        lv_obj_t *ln = box(c, 14, 34, 198, 1, TH_LINE, 0, LV_OPA_COVER); (void)ln;
        lbl(c, 14, 46, 90, 44, &ui_font_num_44, KK_T1, "53");
        lbl(c, 78, 68, 40, 22, &ui_font_cjk_16, KK_T3, "%");
        lv_obj_t *tr = box(c, 14, 100, 198, 4, KK_S3, 2, LV_OPA_COVER); (void)tr;
        lv_obj_t *fl = box(c, 14, 100, 105, 4, KK_OK, 2, LV_OPA_COVER); (void)fl;
        lbl(c, 14, 114, 198, 20, &ui_font_cjk_12, KK_T3, "16.5 / 31.0 GB");
    }

    /* ── 中排四格：v8 实际用到的四个手法 ───────────────────────────── */
    /* 1) 状态点光晕：8px 点 + 同色 10px 外扩（0 偏移），文字仍为主编码 */
    lv_obj_t *c1 = card(scr, 24, 192, 226, 96, true, KK_EDGE_OPA);
    lbl(c1, 14, 10, 198, 20, &ui_font_cjk_12, KK_T3, "状态点光晕（顶栏/网卡）");
    {
        lv_obj_t *dot = box(c1, 16, 40, 8, 8, KK_OK, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        lv_obj_set_style_shadow_color(dot, lv_color_hex(KK_OK), 0);
        lv_obj_set_style_shadow_width(dot, 10, 0);
        lv_obj_set_style_shadow_spread(dot, 2, 0);
        lv_obj_set_style_shadow_opa(dot, KK_GLOW_OPA, 0);
        lbl(c1, 34, 34, 90, 22, &ui_font_cjk_12, KK_OK, "在线");
        lv_obj_t *d2 = box(c1, 130, 40, 8, 8, KK_WARN, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        lv_obj_set_style_shadow_color(d2, lv_color_hex(KK_WARN), 0);
        lv_obj_set_style_shadow_width(d2, 10, 0);
        lv_obj_set_style_shadow_opa(d2, KK_GLOW_OPA, 0);
        lbl(c1, 148, 34, 90, 22, &ui_font_cjk_12, KK_WARN, "离线");
    }
    lbl(c1, 14, 66, 198, 20, &ui_font_cjk_12, KK_T4, "w10 s2 opa140");

    /* 2) 导航选中态：3px 品牌蓝条 + 同色光晕 */
    lv_obj_t *c2 = card(scr, 274, 192, 226, 96, true, KK_EDGE_OPA);
    lbl(c2, 14, 10, 198, 20, &ui_font_cjk_12, KK_T3, "导航选中：蓝条+光晕");
    {
        lv_obj_t *nb = box(c2, 14, 36, 60, 50, KK_S2, KK_RADIUS, LV_OPA_COVER);
        lv_obj_t *mk = box(nb, 0, 8, 3, 34, KK_BLUE, 2, LV_OPA_COVER);
        lv_obj_set_style_shadow_color(mk, lv_color_hex(KK_BLUE), 0);
        lv_obj_set_style_shadow_width(mk, 10, 0);
        lv_obj_set_style_shadow_opa(mk, 130, 0);
        (void)nb;
        lbl(c2, 84, 44, 60, 22, &ui_font_cjk_12, KK_T4, "未选中");
        lv_obj_t *nb2 = box(c2, 148, 36, 60, 50, KK_S1, KK_RADIUS, LV_OPA_TRANSP);
        lv_obj_set_style_border_width(nb2, 1, 0);
        lv_obj_set_style_border_color(nb2, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(nb2, 14, 0);
        (void)nb2;
    }
    lbl(c2, 14, 66, 198, 20, &ui_font_cjk_12, KK_T4, "3px 条 w10 opa130");

    /* 3) 卡头结构线：把标题和数据区切开，不加高度 */
    lv_obj_t *c3 = card(scr, 524, 192, 226, 96, true, KK_EDGE_OPA);
    lbl(c3, 14, 10, 198, 20, &ui_font_cjk_12, KK_T3, "卡头结构线 #2E3238");
    {
        lv_obj_t *ln = box(c3, 14, 34, 198, 1, TH_LINE, 0, LV_OPA_COVER); (void)ln;
        lbl(c3, 14, 42, 120, 22, &ui_font_cjk_12, KK_T2, "容器 8/8 运行");
        lbl(c3, 14, 64, 198, 20, &ui_font_cjk_12, KK_T4, "负载 1.42 · 进程 412");
    }

    /* 4) 嵌槽分组 + 迷你趋势条 */
    lv_obj_t *c4 = card(scr, 774, 192, 226, 96, true, KK_EDGE_OPA);
    lbl(c4, 14, 10, 198, 20, &ui_font_cjk_12, KK_T3, "嵌槽分组 + 迷你趋势");
    {
        lv_obj_t *slot = box(c4, 14, 32, 198, 52, TH_S0, KK_RADIUS_SM, LV_OPA_COVER); (void)slot;
        static const int hh[12] = { 6, 9, 7, 12, 10, 15, 13, 18, 14, 21, 17, 24 };
        for (int k = 0; k < 12; k++) {
            lv_obj_t *b = box(c4, 22 + k * 15, 76 - hh[k], 8, hh[k], KK_S_CPU, LV_RADIUS_CIRCLE, LV_OPA_COVER);
            (void)b;
        }
    }

    /* ── 下排：v8 实际落地的两条信息条（P2 网卡条 / P3 系统摘要）────── */
    lv_obj_t *n = card(scr, 24, 304, 500, 96, true, KK_EDGE_OPA);
    {
        lv_obj_t *dot = box(n, 18, 38, 10, 10, KK_OK, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        lv_obj_set_style_shadow_color(dot, lv_color_hex(KK_OK), 0);
        lv_obj_set_style_shadow_width(dot, 10, 0);
        lv_obj_set_style_shadow_opa(dot, KK_GLOW_OPA, 0);
        lbl(n, 38, 30, 200, 22, &ui_font_cjk_12, KK_T4, "网卡接口");
        lbl(n, 38, 48, 220, 30, &ui_font_num_20, KK_T2, "eth0");
        const char *ik[4] = { "累计接收", "累计发送", "采集耗时", "成功率" };
        const char *iv[4] = { "8421.5 GB", "2210.3 GB", "23 ms", "99%" };
        for (int i = 0; i < 4; i++) {
            lbl(n, 200 + i * 76, 30, 74, 20, &ui_font_cjk_12, KK_T4, ik[i]);
            lbl(n, 200 + i * 76, 50, 74, 26, &ui_font_num_20, KK_T2, iv[i]);
        }
    }

    lv_obj_t *sm = card(scr, 544, 304, 456, 96, true, KK_EDGE_OPA);
    lbl(sm, 16, 12, 424, 22, &ui_font_cjk_16, KK_T1, "系统摘要");
    lv_obj_t *ln2 = box(sm, 16, 38, 424, 1, TH_LINE, 0, LV_OPA_COVER); (void)ln2;
    lbl(sm, 16, 46, 424, 22, &ui_font_cjk_16, KK_T2, "容器 8/8 运行 · 10 路温度 · 0 个事件");
    lbl(sm, 16, 70, 424, 20, &ui_font_cjk_12, KK_T3, "负载 1.42 / 1.18 / 0.96 · 进程 412");

    /* ── 左下：反面清单里被否掉的三样（留证：为什么不做）───────────── */
    lv_obj_t *bad = card(scr, 24, 412, 976, 88, false, KK_EDGE_OPA);
    lbl(bad, 16, 10, 500, 20, &ui_font_cjk_12, KK_T3, "已否掉：背景网格（拍频/摩尔纹）");
    for (int x = 0; x < 220; x += 16) { lv_obj_t *v = box(bad, 520 + x, 8, 1, 30, TH_GRID, 0, 40); (void)v; }
    for (int y = 8; y < 38; y += 16) { lv_obj_t *v = box(bad, 520, y, 220, 1, TH_GRID, 0, 40); (void)v; }
    lbl(bad, 16, 34, 500, 20, &ui_font_cjk_12, KK_T3, "已否掉：1px 竖细线（斜视会消失）");
    {
        for (int x = 0; x < 220; x += 6) { lv_obj_t *v = box(bad, 520 + x, 32, 1, 26, 0x4A90FF, 0, 60); (void)v; }
        lv_obj_t *sharp = box(bad, 520, 60, 220, 2, 0x4A90FF, 1, LV_OPA_COVER); (void)sharp;
        lbl(bad, 754, 32, 200, 20, &ui_font_cjk_12, KK_T4, "↑细线 / ↓2px 实线");
    }
    lbl(bad, 16, 58, 500, 20, &ui_font_cjk_12, KK_T3, "保留：渐变条（总渐变面积 ≤ 全屏 25%）");

    lv_timer_handler();
    snap(outdir);
    return 0;
}
