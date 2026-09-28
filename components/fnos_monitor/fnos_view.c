// fnOS NAS 仪表盘 v2：1024×600 四页，按 docs/ui-redesign.md 的设计合同实现。
//
// 阅读顺序：每页只回答一个问题，自上而下"第一眼 → 第二眼 → 第三眼"。
// 状态分三层：身份色（对象是谁）/ 运行状态 / 数据可信度，三者互不覆盖。
// 切页一律原子换页（先隐藏全部旧页并归零偏移，再显示目标页）：
// 本板是"全屏页 + partial buffer"，双页同时做位移动画会留残影。
#include "fnos_view.h"
#include "fnos_ui.h"
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

#define PAGE_N 4
#define STALE_MS 3000
#define HIST_PTS CONFIG_FNOS_CHART_WINDOW
// 趋势图的点数 = 2 秒一个点：180 秒的 1 Hz 数据如果 1:1 画成柱，
// 柱子会挤成一块实心色板（实测就是这样），隔点显示才有"波形"的纹理感。
#define TREND_PTS (CONFIG_FNOS_CHART_WINDOW / 2)

enum { PG_OVERVIEW = 0, PG_STORAGE, PG_NETWORK, PG_SYSTEM };

static bool s_created;
static bool s_night;
static volatile bool s_night_req;   // 非 LVGL 任务只写这个标志
static void apply_night(void);
static int  s_page, s_press_x;
static lv_obj_t *s_chrome_scr, *s_chrome_top, *s_chrome_rail, *s_chrome_bot, *s_chrome_content;
static lv_obj_t *s_pages[PAGE_N];
static lv_obj_t *s_nav[PAGE_N], *s_nav_lbl[PAGE_N];
static lv_obj_t *s_nav_ind;            // 唯一的选中指示条（切页时滑动过去）
static EXT_RAM_BSS_ATTR fnos_status_t s_st;
static int64_t s_hist_seq;

/* 顶栏 / 底栏 */
static lv_obj_t *s_hd_host, *s_hd_endpoint, *s_hd_chip, *s_hd_clock, *s_sig[4];
static lv_obj_t *s_ft_poll, *s_ft_alert_lbl, *s_ft_alert_dot;

/* P0 总览 */
static lv_obj_t *s_ov_verdict, *s_ov_verdict_sub, *s_ov_reason;
static lv_obj_t *s_ov_val[4], *s_ov_unit[4], *s_ov_sub[4];
static ck_bar_t  s_ov_mbar[3];
static lv_obj_t *s_ov_vol_name[6], *s_ov_vol_pct[6];
static ck_bar_t  s_ov_vol_bar[6];
static ck_trend_t s_ov_cpu, s_ov_mem;

/* P1 存储 */
static lv_obj_t *s_st_hero, *s_st_used, *s_st_free, *s_st_cnt;
static lv_obj_t *s_st_seg[6];
static lv_obj_t *s_st_vname[6], *s_st_vfs[6], *s_st_vpct[6], *s_st_vuse[6];
static ck_bar_t  s_st_vbar[6];
static lv_obj_t *s_st_raid_name[4], *s_st_raid_lvl[4], *s_st_raid_state[4];
static lv_obj_t *s_st_io_name[4], *s_st_io_val[4];

/* P2 网络 */
static lv_obj_t *s_nw_down_v, *s_nw_down_u, *s_nw_down_sub;
static lv_obj_t *s_nw_up_v, *s_nw_up_u, *s_nw_up_sub;
static ck_trend_t s_nw_down, s_nw_up;
static ck_trend_t s_nw_mini_down, s_nw_mini_up;   // 网络瓦片右侧的迷你趋势（补留白）
static lv_obj_t *s_nw_axis[3];
static lv_obj_t *s_nw_ctx_v[4], *s_nw_ctx_l[4];

/* P3 系统 */
static lv_obj_t *s_sy_dot[6], *s_sy_dock[6], *s_sy_dock_s[6];
static lv_obj_t *s_sy_cnt;
static lv_obj_t *s_sy_alert_dot[3], *s_sy_alert_lbl[3];
static lv_obj_t *s_sy_tname[7], *s_sy_tval[7];
static ck_bar_t  s_sy_tbar[7];
static lv_obj_t *s_sy_kv_l[8], *s_sy_kv_v[8];
static lv_obj_t *s_sy_alert_dot[3], *s_sy_alert_lbl[3];

static bool s_mb_down, s_mb_up;

static void nav_ind_anim(void *var, int32_t v);   // apply_page 里用到

/* ───────────────────────────── 格式化（统一格式，避免刷新抖动） */

static void fmt_rate(char *b, size_t n, float kbs, bool *use_mb)
{
    const float HI = 1024.0f, LO = 950.0f;
    if (use_mb) {
        if (*use_mb && kbs < LO) *use_mb = false;
        else if (!*use_mb && kbs > HI) *use_mb = true;
        if (*use_mb) { snprintf(b, n, "%.2f", kbs / 1024.0f); return; }
    } else if (kbs > HI) {
        snprintf(b, n, "%.2f", kbs / 1024.0f);
        return;
    }
    if (kbs >= 100.0f) snprintf(b, n, "%.0f", kbs);
    else               snprintf(b, n, "%.1f", kbs);
}

static const char *rate_unit(bool use_mb, float kbs)
{
    bool mb = use_mb ? use_mb : (kbs > 1024.0f);
    return mb ? "MB/s" : "KB/s";
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

static void fmt_age(char *b, size_t n, int64_t sec)
{
    if (sec < 0)         snprintf(b, n, "--");
    else if (sec < 90)   snprintf(b, n, "%llds", (long long)sec);
    else if (sec < 5400) snprintf(b, n, "%lldm", (long long)(sec / 60));
    else                 snprintf(b, n, "%lldh", (long long)(sec / 3600));
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

/* ───────────────────────────── 数据可信度 */

typedef enum { TR_WARMING, TR_LIVE, TR_STALE, TR_OFFLINE } trust_t;

static int64_t data_age_s(void)
{
    return s_st.recv_ms ? (esp_timer_get_time() / 1000 - s_st.recv_ms) / 1000 : -1;
}

static trust_t trust_state(void)
{
    if (!s_st.ever_ok) return TR_WARMING;
    if (!s_st.online) return TR_OFFLINE;
    int64_t age = data_age_s();
    return (age >= 0 && age < STALE_MS / 1000) ? TR_LIVE : TR_STALE;
}

static bool have(void) { return s_st.ever_ok; }

static int alert_count(const char *lv)
{
    int n = 0;
    for (int i = 0; i < s_st.nalerts; i++) {
        if (strcmp(s_st.alerts[i].lv, lv) == 0) n++;
    }
    return n;
}

/* ───────────────────────────── 翻页 */

static void apply_page(void)
{
    for (int i = 0; i < PAGE_N; i++) {
        if (s_pages[i]) {
            lv_obj_set_pos(s_pages[i], 0, 0);
            if (i == s_page) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            else             lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (s_nav[i])     lv_obj_set_style_bg_color(s_nav[i], lv_color_hex(i == s_page ? CK_NAV_SEL : CK_BG), 0);
        if (s_nav_lbl[i]) lv_obj_set_style_text_color(s_nav_lbl[i], lv_color_hex(i == s_page ? CK_TEXT : CK_DIM), 0);
    }
    // 切页过渡：指示条滑向新项（180ms ease-out），页面本体瞬时切换（原子换页）
    if (s_nav_ind) {
        int target = CK_TOP_H + 18 + s_page * 96 + 12;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_nav_ind);
        lv_anim_set_exec_cb(&a, nav_ind_anim);
        lv_anim_set_values(&a, lv_obj_get_y(s_nav_ind), target);
        lv_anim_set_duration(&a, 180);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
    lv_obj_invalidate(lv_screen_active());
}

void fnos_view_set_page(int idx)
{
    if (idx < 0) idx = PAGE_N - 1;
    if (idx >= PAGE_N) idx = 0;
    if (idx == s_page) return;
    s_page = idx;
    apply_page();
}

int fnos_view_page(void) { return s_page; }

static void on_nav(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        lv_obj_t *o = lv_event_get_target(e);
        if (o) lv_obj_set_style_bg_color(o, lv_color_hex(CK_PANEL_HI), 0);
        return;
    }
    fnos_view_set_page((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_press(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = { 0, 0 };
    if (indev) { lv_indev_get_point(indev, &p); s_press_x = p.x; }
}

static void on_release(lv_event_t *e)
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

/* ───────────────────────────── 构建：框架 */

static void build_top(lv_obj_t *scr)
{
    s_chrome_top = ck_obj(scr, 0, 0, CK_SCR_W, CK_TOP_H, CK_PANEL, 0, false);
    ck_divider(scr, 0, CK_TOP_H - 1, CK_SCR_W, CK_GUIDE);

    s_hd_host = ck_label(scr, "NAS", CK_F_TITLE, CK_TEXT, CK_PAD, 18);
    s_hd_endpoint = ck_label(scr, "", CK_F_META, CK_DIM, CK_PAD + 130, 26);
    // 位置在 update_top 里按主机名实际宽度定位（place_unit）

    s_hd_chip = ck_chip(scr, 420, 17, 184, 30, CK_WARN);
    ck_chip_set(s_hd_chip, "等待数据", CK_WARN);

    s_hd_clock = ck_label_r(scr, "--:--", CK_F_NUMM, CK_TEXT, CK_PAD, 14);
    ck_signal_bars(scr, CK_SCR_W - CK_PAD - 130, 20, s_sig);
}

// 切页过渡：只让 4px 宽的选中指示条滑到新导航项（180ms），页面本体原子换页。
// 不做整页 translate/淡入：本板是"全屏页 + partial buffer"，双页同时合成会留残影。
static void nav_ind_anim(void *var, int32_t v)   // lv_anim_exec_xcb_t 签名
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void build_rail(lv_obj_t *scr)
{
    static const char *names[PAGE_N] = { "总览", "存储", "网络", "系统" };
    s_chrome_rail = ck_obj(scr, 0, CK_TOP_H, CK_RAIL_W, CK_SCR_H - CK_TOP_H - CK_BOT_H, CK_BG, 0, false);
    for (int i = 0; i < PAGE_N; i++) {
        int y = CK_TOP_H + 18 + i * 96;
        lv_obj_t *item = ck_obj(scr, 12, y, CK_RAIL_W - 24, 88, CK_BG, 12, true);
        s_nav[i] = item;
        ck_icon(item, 14, 14, i, CK_DIM);
        s_nav_lbl[i] = ck_label(item, names[i], CK_F_CLABEL, CK_DIM, 0, 52);
        if (s_nav_lbl[i]) lv_obj_align(s_nav_lbl[i], LV_ALIGN_TOP_MID, 0, 52);
        lv_obj_add_event_cb(item, on_nav, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(item, on_nav, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    // 唯一的指示条，挂在屏幕（rail 区域内），初始在第 1 项
    s_nav_ind = ck_obj(scr, 12, CK_TOP_H + 18 + 12, 4, 64, CK_TEXT, 2, false);
}

static void build_bottom(lv_obj_t *scr)
{
    // 底栏文字必须挂在底栏面板上（y 相对面板）。
    // 曾经挂在 screen 上写 y=11/12/16，整行画到屏幕顶部压住顶栏 ——
    // 视觉评审报的"红色告警压住在线胶囊 / 采集与 nas 重叠"根因就是它。
    lv_obj_t *f = ck_obj(scr, 0, CK_SCR_H - CK_BOT_H, CK_SCR_W, CK_BOT_H, CK_PANEL, 0, false);
    s_chrome_bot = f;
    ck_divider(scr, 0, CK_SCR_H - CK_BOT_H, CK_SCR_W, CK_GUIDE);
    s_ft_poll = ck_label(f, "", CK_F_CMETA, CK_DIM, CK_PAD, 10);
    s_ft_alert_dot = ck_obj(f, CK_SCR_W - CK_PAD - 448, 14, 8, 8, CK_OK, 4, false);
    s_ft_alert_lbl = ck_label(f, "无告警", CK_F_CMETA, CK_OK, CK_SCR_W - CK_PAD - 432, 9);
}

// 页面是内容容器的子对象，坐标从内容区左上角算起；
// 这样 apply_page() 里把页面位置归零 = 回到内容区原点，而不是跑到屏幕左上角去盖住顶栏/导航。
static lv_obj_t *make_page(lv_obj_t *content)
{
    lv_obj_t *p = ck_obj(content, 0, 0, CK_CONT_W, CK_CONT_H, CK_BG, 0, true);
    if (p) {
        lv_obj_add_event_cb(p, on_press, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(p, on_release, LV_EVENT_RELEASED, NULL);
    }
    return p;
}

/* ───────────────────────────── 构建：P0 总览 */

static void metric_tile(lv_obj_t *p, int idx, int x, const char *name, uint32_t color, bool with_bar)
{
    lv_obj_t *t = ck_tile(p, x, 190, 209, 118);
    ck_label(t, name, CK_F_CLABEL, CK_DIM, 14, 10);
    s_ov_val[idx] = ck_label(t, "--", CK_F_NUMM, CK_TEXT, 14, 38);
    s_ov_unit[idx] = ck_label(t, "", CK_F_META, CK_DIM, 85, 53);
    // 副标题里会出现中文（"负载 … 核"、"NAS 持续运行"），必须用带中文的混排字体，
    // 否则 LVGL 找不到字形会画成豆腐块（实测踩过）
    s_ov_sub[idx] = ck_label(t, "", CK_F_CMETA, CK_IDLE, 14, 92);
    if (with_bar) {
        ck_bar_create(&s_ov_mbar[idx], t, 14, 80, 181, 6);
        if (s_ov_mbar[idx].fill) {
            lv_obj_set_style_bg_color(s_ov_mbar[idx].fill, lv_color_hex(color), 0);
            lv_obj_set_style_bg_grad_color(s_ov_mbar[idx].fill, lv_color_hex(color), 0);
        }
    }
}

static void build_overview(lv_obj_t *p)
{
    lv_obj_t *h = ck_tile(p, 0, 0, 430, 176);
    ck_label(h, "NAS 健康", CK_F_CLABEL, CK_DIM, 20, 14);
    s_ov_verdict = ck_label(h, "等待", CK_F_CVERDICT, CK_WARN, 20, 46);
    s_ov_verdict_sub = ck_label(h, "等待首个数据帧", CK_F_CMETA, CK_DIM, 20, 128);
    s_ov_reason = ck_label(h, "", CK_F_CMETA, CK_DANGER, 20, 150);

    lv_obj_t *sp = ck_tile(p, 444, 0, 436, 176);
    ck_label(sp, "存储空间", CK_F_CLABEL, CK_DIM, 16, 12);
    for (int i = 0; i < 6; i++) {
        int y = 42 + i * 22;
        s_ov_vol_name[i] = ck_label(sp, "", CK_F_NUMS, CK_DIM, 16, y - 4);
        ck_bar_create(&s_ov_vol_bar[i], sp, 130, y, 220, 8);
        s_ov_vol_pct[i] = ck_label_r(sp, "", CK_F_NUMS, CK_TEXT, 16, y - 5);
    }

    metric_tile(p, 0, 0,   "CPU", CK_CPU, true);
    metric_tile(p, 1, 223, "内存", CK_MEM, true);
    metric_tile(p, 2, 446, "最高温度", CK_TEMP, true);
    metric_tile(p, 3, 669, "运行时长", CK_ZFS, false);

    lv_obj_t *tr = ck_tile(p, 0, 322, CK_CONT_W, CK_CONT_H - 322);
    ck_label(tr, "CPU / 内存 · 百分比 · 近 3 分钟", CK_F_CMETA, CK_DIM, 16, 8);
    ck_obj(tr, CK_CONT_W - 240, 12, 10, 10, CK_CPU, 3, false);
    ck_label(tr, "CPU", CK_F_META, CK_CPU, CK_CONT_W - 222, 8);
    ck_obj(tr, CK_CONT_W - 130, 12, 10, 10, CK_MEM, 3, false);
    ck_label(tr, "MEM", CK_F_META, CK_MEM, CK_CONT_W - 112, 8);
    ck_trend_create(&s_ov_cpu, tr, 14, 34, CK_CONT_W - 28, 46, CK_CPU, TREND_PTS);
    ck_trend_create(&s_ov_mem, tr, 14, 88, CK_CONT_W - 28, 46, CK_MEM, TREND_PTS);
    ck_trend_range(&s_ov_cpu, 100);
    ck_trend_range(&s_ov_mem, 100);
}

/* ───────────────────────────── 构建：P1 存储 */

static void volume_cell(lv_obj_t *p, int i)
{
    int col = i % 2, row = i / 2;
    int x = col * 450, y = 162 + row * 58;
    lv_obj_t *c = ck_tile(p, x, y, 430, 54);
    s_st_vname[i] = ck_label(c, "", CK_F_NUMS, CK_TEXT, 14, 8);
    s_st_vfs[i] = ck_label(c, "", CK_F_META, CK_IDLE, 120, 12);
    s_st_vuse[i] = ck_label(c, "", CK_F_META, CK_DIM, 220, 12);
    s_st_vpct[i] = ck_label_r(c, "", CK_F_NUMS, CK_TEXT, 14, 7);
    ck_bar_create(&s_st_vbar[i], c, 14, 36, 402, 10);
    ck_bar_tick(c, 14 + (int)(402 * 0.80f), 32, 18);
    ck_bar_tick(c, 14 + (int)(402 * 0.90f), 32, 18);
}

static void build_storage(lv_obj_t *p)
{
    lv_obj_t *h = ck_tile(p, 0, 0, CK_CONT_W, 150);
    ck_label(h, "已用容量 / 总容量", CK_F_CLABEL, CK_DIM, 20, 12);
    s_st_hero = ck_label(h, "--", CK_F_HERO, CK_TEXT, 20, 34);
    s_st_used = ck_label(h, "TB / 总量", CK_F_CLABEL, CK_DIM, 250, 74);
    s_st_cnt = ck_label_r(h, "", CK_F_CMETA, CK_DIM, 20, 16);
    s_st_free = ck_label_r(h, "", CK_F_CLABEL, CK_OK, 20, 66);
    for (int i = 0; i < 6; i++) {
        s_st_seg[i] = ck_obj(h, 20, 128, 0, 12, CK_NET_DOWN, 3, false);
    }

    for (int i = 0; i < 6; i++) volume_cell(p, i);

    lv_obj_t *r = ck_tile(p, 0, 336, 430, CK_CONT_H - 336);
    ck_label(r, "RAID 阵列", CK_F_CLABEL, CK_DIM, 16, 10);
    for (int i = 0; i < 4; i++) {
        int y = 34 + i * 20;          // 4 行 × 20 + 起点 34 → 116，面板 128，留 12px
        s_st_raid_name[i] = ck_label(r, "", CK_F_NUMS, CK_TEXT, 16, y);
        s_st_raid_lvl[i] = ck_label(r, "", CK_F_META, CK_IDLE, 110, y + 4);
        s_st_raid_state[i] = ck_label(r, "", CK_F_CMETA, CK_OK, 190, y + 3);
    }

    lv_obj_t *d = ck_tile(p, 450, 336, 430, CK_CONT_H - 336);
    ck_label(d, "磁盘活动", CK_F_CLABEL, CK_DIM, 16, 10);
    for (int i = 0; i < 4; i++) {
        int y = 34 + i * 20;
        s_st_io_name[i] = ck_label(d, "", CK_F_NUMS, CK_TEXT, 16, y);
        s_st_io_val[i] = ck_label_r(d, "", CK_F_NUMS, CK_DIM, 16, y);
    }
}

/* ───────────────────────────── 构建：P2 网络 */

static void build_network(lv_obj_t *p)
{
    for (int k = 0; k < 2; k++) {
        bool down = (k == 0);
        lv_obj_t *t = ck_tile(p, down ? 0 : 447, 0, 433, 170);
        uint32_t color = down ? CK_NET_DOWN : CK_NET_UP;
        ck_label(t, down ? "下行 DOWN" : "上行 UP", CK_F_CLABEL, color, 18, 12);
        lv_obj_t *v = ck_label(t, "--", CK_F_NUML, CK_TEXT, 18, 44);
        lv_obj_t *u = ck_label(t, "KB/s", CK_F_LABEL, CK_DIM, 20, 94);
        lv_obj_t *s = ck_label(t, "", CK_F_CMETA, CK_IDLE, 18, 126);
        // 右半留白放迷你趋势：即时"在涨还是在跌"的纹理，
        // 下方吞吐趋势面板仍是 3 分钟详图
        ck_trend_t *mini = down ? &s_nw_mini_down : &s_nw_mini_up;
        ck_trend_create(mini, t, 214, 44, 203, 96, color, TREND_PTS);
        ck_trend_range(mini, 256);
        if (down) { s_nw_down_v = v; s_nw_down_u = u; s_nw_down_sub = s; }
        else      { s_nw_up_v = v;   s_nw_up_u = u;   s_nw_up_sub = s; }
    }

    lv_obj_t *tr = ck_tile(p, 0, 184, CK_CONT_W, 190);
    ck_label(tr, "吞吐趋势 · KB/s · 近 3 分钟", CK_F_CMETA, CK_DIM, 16, 8);
    // 图例：色块 + 序列名（解决"两条曲线分不清谁是谁"）
    ck_obj(tr, CK_CONT_W - 240, 12, 10, 10, CK_NET_DOWN, 3, false);
    ck_label(tr, "DOWN", CK_F_META, CK_NET_DOWN, CK_CONT_W - 222, 8);
    ck_obj(tr, CK_CONT_W - 130, 12, 10, 10, CK_NET_UP, 3, false);
    ck_label(tr, "UP", CK_F_META, CK_NET_UP, CK_CONT_W - 112, 8);
    ck_trend_create(&s_nw_down, tr, 14, 36, CK_CONT_W - 120, 60, CK_NET_DOWN, TREND_PTS);
    ck_trend_create(&s_nw_up, tr, 14, 108, CK_CONT_W - 120, 60, CK_NET_UP, TREND_PTS);
    ck_trend_range(&s_nw_down, 256);
    ck_trend_range(&s_nw_up, 256);
    for (int i = 0; i < 3; i++) {
        s_nw_axis[i] = ck_label(tr, "", CK_F_META, CK_DIM, CK_CONT_W - 106, 44 + i * 56);
    }

    static const char *lbl[4] = { "峰值下行", "峰值上行", "采集延迟", "曲线采样" };
    int w = (CK_CONT_W - 3 * 14) / 4;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = ck_tile(p, i * (w + 14), 388, w, CK_CONT_H - 388);
        s_nw_ctx_l[i] = ck_label(c, lbl[i], CK_F_CLABEL, CK_DIM, 14, 10);
        s_nw_ctx_v[i] = ck_label(c, "--", CK_F_NUMS, CK_TEXT, 14, 38);
    }
}

/* ───────────────────────────── 构建：P3 系统 */

static void build_system(lv_obj_t *p)
{
    lv_obj_t *c = ck_tile(p, 0, 0, 430, CK_CONT_H);
    ck_label(c, "容器", CK_F_CLABEL, CK_DIM, 16, 12);
    s_sy_cnt = ck_label_r(c, "", CK_F_CMETA, CK_DIM, 16, 16);
    for (int i = 0; i < 6; i++) {
        int y = 52 + i * 44;
        s_sy_dot[i] = ck_obj(c, 16, y + 8, 10, 10, CK_IDLE, 5, false);
        s_sy_dock[i] = ck_label(c, "", CK_F_NUMS, CK_TEXT, 36, y);
        s_sy_dock_s[i] = ck_label(c, "", CK_F_CMETA, CK_DIM, 36, y + 22);
    }
    // 下半部：告警列表 —— 把"危险/注意"的具体原因逐条列出来
    ck_divider(c, 16, 332, 430 - 32, CK_GUIDE);
    ck_label(c, "告警", CK_F_CLABEL, CK_DIM, 16, 344);
    for (int i = 0; i < 3; i++) {
        s_sy_alert_dot[i] = ck_obj(c, 16, 386 + i * 30, 8, 8, CK_IDLE, 4, false);
        s_sy_alert_lbl[i] = ck_label(c, "", CK_F_CMETA, CK_DIM, 34, 380 + i * 30);
    }

    lv_obj_t *t = ck_tile(p, 450, 0, 430, 238);
    ck_label(t, "温度", CK_F_CLABEL, CK_DIM, 16, 12);
    for (int i = 0; i < 7; i++) {
        int y = 42 + i * 28;                 // 7 行 × 28 = 196，起点 42 → 238 正好铺满面板
        s_sy_tname[i] = ck_label(t, "", CK_F_LABEL, CK_DIM, 16, y);
        ck_bar_create(&s_sy_tbar[i], t, 150, y + 5, 170, 8);
        s_sy_tval[i] = ck_label_r(t, "", CK_F_NUMS, CK_TEXT, 16, y - 2);
    }

    lv_obj_t *k = ck_tile(p, 450, 252, 430, CK_CONT_H - 252);
    ck_label(k, "采集端点", CK_F_CLABEL, CK_DIM, 16, 10);
    // 两列：标签左、值右对齐。行距 20：36+7*20=176，行高 18 → 194，面板 212，
    // 底部留 18px（原来 22 行距 + 40 起点 → 最后一行贴住底边框）
    for (int i = 0; i < 8; i++) {
        int y = 36 + i * 20;
        s_sy_kv_l[i] = ck_label(k, "", CK_F_CMETA, CK_DIM, 16, y);
        s_sy_kv_v[i] = ck_label_r(k, "", CK_F_CMETA, CK_TEXT, 16, y);
    }
}

/* ───────────────────────────── 刷新 */

// 单位/后缀标签紧跟参考标签的实际宽度（固定 x 会让单位飘在半空）
static void place_unit(lv_obj_t *ref, lv_obj_t *unit, int x0, int y)
{
    if (!ref || !unit) return;
    int w = lv_obj_get_self_width(ref);
    lv_obj_set_pos(unit, x0 + w + 10, y);
}

static void update_top(void)
{
    char b[96], chip[48];
    const char *ip = fnos_net_ip();
    ck_set(s_hd_host, s_st.host[0] ? s_st.host : "NAS");
    snprintf(b, sizeof(b), "%s:%d  %s", FNOS_HOST, FNOS_PORT, ip[0] ? ip : "无 IP");
    ck_set(s_hd_endpoint, b);
    place_unit(s_hd_host, s_hd_endpoint, CK_PAD, 26);

    int64_t age = data_age_s();
    const char *txt;
    uint32_t col;
    switch (trust_state()) {
    case TR_LIVE:    col = CK_OK;     snprintf(chip, sizeof(chip), "在线 · %llds", (long long)age); break;
    case TR_WARMING: col = CK_WARN;   snprintf(chip, sizeof(chip), "等待数据"); break;
    case TR_STALE:   col = CK_WARN;   snprintf(chip, sizeof(chip), "陈旧 %llds", (long long)age); break;
    default:         col = CK_DANGER; snprintf(chip, sizeof(chip), "采集端离线"); break;
    }
    txt = chip;
    ck_chip_set(s_hd_chip, txt, col);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (tmv.tm_year > 120) snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    else                   snprintf(b, sizeof(b), "--:--");
    ck_set(s_hd_clock, b);
    ck_signal_set(s_sig, fnos_net_rssi());
}

static void update_bottom(void)
{
    char b[128];
    if (have()) {
        snprintf(b, sizeof(b), "采集 %s:%d · 正常 %u / 失败 %u · %d ms",
                 FNOS_HOST, FNOS_PORT, (unsigned)s_st.ok_count, (unsigned)s_st.fail_count, s_st.http_ms);
    } else {
        snprintf(b, sizeof(b), "采集 %s:%d · 等待首个数据帧", FNOS_HOST, FNOS_PORT);
    }
    ck_set(s_ft_poll, b);

    uint32_t col = CK_OK;
    const char *msg = "无告警";
    if (trust_state() == TR_OFFLINE) {
        col = CK_DANGER;
        msg = s_st.last_err[0] ? s_st.last_err : "采集端不可达";
    } else if (s_st.nalerts > 0) {
        col = strcmp(s_st.alerts[0].lv, "crit") == 0 ? CK_DANGER : CK_WARN;
        msg = s_st.alerts[0].m;
    }
    ck_set(s_ft_alert_lbl, msg);
    ck_set_color(s_ft_alert_lbl, col);
    if (s_ft_alert_dot) lv_obj_set_style_bg_color(s_ft_alert_dot, lv_color_hex(col), 0);
}

static void update_overview(void)
{
    char b[64], s[64];
    bool ok = have();

    int crit = alert_count("crit"), warn = alert_count("warn");
    trust_t tr = trust_state();
    const char *word;
    uint32_t col;
    if (tr == TR_OFFLINE)                { word = "离线"; col = CK_DANGER; }
    else if (crit > 0)                   { word = "危险"; col = CK_DANGER; }
    else if (warn > 0 || tr == TR_STALE) { word = "注意"; col = CK_WARN; }
    else if (tr == TR_WARMING)           { word = "等待"; col = CK_WARN; }
    else                                 { word = "正常"; col = CK_OK; }
    ck_set(s_ov_verdict, word);
    ck_set_color(s_ov_verdict, col);

    if (ok) snprintf(b, sizeof(b), "6 项检查 · %d 条告警", s_st.nalerts);
    else    snprintf(b, sizeof(b), "等待首个数据帧");
    ck_set(s_ov_verdict_sub, b);

    // 原因行：让"注意/危险"这个结论立刻可解释
    char why[48];
    if (trust_state() == TR_OFFLINE) {
        snprintf(why, sizeof(why), "采集端不可达：%s", s_st.last_err[0] ? s_st.last_err : "poll failed");
        ck_set_color(s_ov_reason, CK_DANGER);
    } else if (s_st.nalerts > 0) {
        if (s_st.nalerts > 1) snprintf(why, sizeof(why), "%s  (+%d)", s_st.alerts[0].m, s_st.nalerts - 1);
        else                  snprintf(why, sizeof(why), "%s", s_st.alerts[0].m);
        ck_set_color(s_ov_reason, strcmp(s_st.alerts[0].lv, "crit") == 0 ? CK_DANGER : CK_WARN);
    } else {
        why[0] = 0;
    }
    ck_set(s_ov_reason, why);

    if (ok) {
        snprintf(b, sizeof(b), "%.0f", s_st.cpu.pct);
        ck_set(s_ov_val[0], b);
        ck_set(s_ov_unit[0], "%");
        lv_obj_set_pos(s_ov_unit[0], 14 + lv_obj_get_width(s_ov_val[0]) + 8, 53);
        snprintf(s, sizeof(s), "负载 %.2f · %d 核", s_st.cpu.load1, s_st.cpu.cores);
        ck_set(s_ov_sub[0], s);
        ck_bar_set_color(&s_ov_mbar[0], s_st.cpu.pct, CK_CPU);

        snprintf(b, sizeof(b), "%.0f", s_st.mem.pct);
        ck_set(s_ov_val[1], b);
        ck_set(s_ov_unit[1], "%");
        lv_obj_set_pos(s_ov_unit[1], 14 + lv_obj_get_width(s_ov_val[1]) + 8, 53);
        snprintf(s, sizeof(s), "%.1f / %.0f GB", s_st.mem.used_mb / 1024.0f, s_st.mem.total_mb / 1024.0f);
        ck_set(s_ov_sub[1], s);
        ck_bar_set_color(&s_ov_mbar[1], s_st.mem.pct, CK_MEM);

        float hot = 0;
        for (int i = 0; i < s_st.ntemps; i++) if (s_st.temps[i].c > hot) hot = s_st.temps[i].c;
        snprintf(b, sizeof(b), "%.0f", hot);
        ck_set(s_ov_val[2], b);
        ck_set(s_ov_unit[2], "C");
        lv_obj_set_pos(s_ov_unit[2], 14 + lv_obj_get_width(s_ov_val[2]) + 8, 53);
        if (s_st.ntemps > 0) snprintf(s, sizeof(s), "%s %.0f°C", s_st.temps[0].n, s_st.temps[0].c);
        else                 snprintf(s, sizeof(s), "--");
        ck_set(s_ov_sub[2], s);
        ck_bar_set_color(&s_ov_mbar[2], hot, CK_TEMP);

        fmt_uptime(b, sizeof(b), s_st.uptime_s);
        ck_set(s_ov_val[3], b);
        ck_set(s_ov_unit[3], "");
        ck_set(s_ov_sub[3], "NAS 持续运行");
    } else {
        for (int i = 0; i < 4; i++) {
            ck_set(s_ov_val[i], "--");
            ck_set(s_ov_unit[i], "");
            ck_set(s_ov_sub[i], "");
        }
    }

    for (int i = 0; i < 6; i++) {
        if (ok && i < s_st.nvols) {
            ck_set(s_ov_vol_name[i], s_st.vols[i].mnt);
            snprintf(b, sizeof(b), "%.0f%%", s_st.vols[i].pct);
            ck_set(s_ov_vol_pct[i], b);
            // 严重度色阶：绿→黄绿→黄→红，与存储页用量条的渐变一致
            uint32_t c = CK_OK;
            if (s_st.vols[i].pct >= 90) c = CK_DANGER;
            else if (s_st.vols[i].pct >= 80) c = CK_WARN;
            else if (s_st.vols[i].pct >= 60) c = CK_ZFS;
            ck_set_color(s_ov_vol_pct[i], c);
            ck_bar_set(&s_ov_vol_bar[i], s_st.vols[i].pct);
        } else {
            ck_set(s_ov_vol_name[i], "");
            ck_set(s_ov_vol_pct[i], "");
            if (s_ov_vol_bar[i].fill) lv_obj_set_width(s_ov_vol_bar[i].fill, 0);
        }
    }
}

static void update_storage(void)
{
    char b[64], s[64];
    bool ok = have();
    float total = 0, used = 0, free = 0;
    for (int i = 0; i < s_st.nvols; i++) {
        total += s_st.vols[i].total_gb;
        used  += s_st.vols[i].used_gb;
        free  += s_st.vols[i].free_gb;
    }

    if (ok && total > 0) {
        bool tb = used >= 1024.0f;
        snprintf(b, sizeof(b), tb ? "%.1f" : "%.0f", tb ? used / 1024.0f : used);
        ck_set(s_st_hero, b);
        ck_set(s_st_used, tb ? "TB / 总量" : "GB / 总量");
        place_unit(s_st_hero, s_st_used, 20, 80);
        char t1[24], t2[24], t3[24];
        fmt_cap(t1, sizeof(t1), used);
        fmt_cap(t2, sizeof(t2), total);
        fmt_cap(t3, sizeof(t3), free);
        snprintf(s, sizeof(s), "%s / %s", t1, t2);
        ck_set(s_st_cnt, s);
        snprintf(s, sizeof(s), "可用 %s", t3);
        ck_set(s_st_free, s);

        int x = 20;
        // 总宽要减掉段间缝（5 × 3px），否则整条会溢出面板
        const int W = 840 - 5 * 3;
        for (int i = 0; i < 6; i++) {
            if (i >= s_st.nvols || !s_st_seg[i]) continue;
            int wpx = (int)(W * (s_st.vols[i].total_gb / total) + 0.5f);
            if (wpx < 8) wpx = 8;
            lv_obj_set_pos(s_st_seg[i], x, 128);   // 与 build_storage 的创建位置一致（不要留在旧版 y=92）
            lv_obj_set_width(s_st_seg[i], wpx);
            float pct = s_st.vols[i].pct;
            uint32_t c = CK_NET_DOWN;
            if (pct >= 85) c = CK_DANGER;
            else if (pct >= 70) c = CK_WARN;
            else if (pct >= 45) c = CK_ZFS;
            lv_obj_set_style_bg_color(s_st_seg[i], lv_color_hex(c), 0);
            x += wpx + 3;
        }
        for (int i = s_st.nvols; i < 6; i++) {
            if (s_st_seg[i]) lv_obj_set_width(s_st_seg[i], 0);
        }
    } else {
        ck_set(s_st_hero, "--");
        ck_set(s_st_cnt, "");
        ck_set(s_st_free, "");
    }

    for (int i = 0; i < 6; i++) {
        if (ok && i < s_st.nvols) {
            ck_set(s_st_vname[i], s_st.vols[i].mnt);
            ck_set(s_st_vfs[i], s_st.vols[i].fs);
            snprintf(b, sizeof(b), "%.0f%%", s_st.vols[i].pct);
            ck_set(s_st_vpct[i], b);
            uint32_t c = CK_OK;
            if (s_st.vols[i].pct >= 90) c = CK_DANGER;
            else if (s_st.vols[i].pct >= 80) c = CK_WARN;
            ck_set_color(s_st_vpct[i], c);
            char c1[24], c2[24];
            fmt_cap(c1, sizeof(c1), s_st.vols[i].used_gb);
            fmt_cap(c2, sizeof(c2), s_st.vols[i].total_gb);
            snprintf(b, sizeof(b), "%s / %s", c1, c2);
            ck_set(s_st_vuse[i], b);
            ck_bar_set(&s_st_vbar[i], s_st.vols[i].pct);
        } else {
            ck_set(s_st_vname[i], "");
            ck_set(s_st_vfs[i], "");
            ck_set(s_st_vpct[i], "");
            ck_set(s_st_vuse[i], "");
            if (s_st_vbar[i].fill) lv_obj_set_width(s_st_vbar[i].fill, 0);
        }
    }

    for (int i = 0; i < 4; i++) {
        if (ok && i < s_st.nraid) {
            ck_set(s_st_raid_name[i], s_st.raid[i].dev);
            ck_set(s_st_raid_lvl[i], s_st.raid[i].lvl);
            uint32_t c;
            if (s_st.raid[i].sync_pct >= 0) {
                snprintf(b, sizeof(b), "同步 %.0f%%", s_st.raid[i].sync_pct);
                c = CK_NET_DOWN;
            } else if (s_st.raid[i].ok) {
                snprintf(b, sizeof(b), "正常 %d/%d", s_st.raid[i].have, s_st.raid[i].want);
                c = CK_OK;
            } else {
                snprintf(b, sizeof(b), "降级 %s", s_st.raid[i].state);
                c = CK_DANGER;
            }
            ck_set(s_st_raid_state[i], b);
            ck_set_color(s_st_raid_state[i], c);
        } else {
            ck_set(s_st_raid_name[i], "");
            ck_set(s_st_raid_lvl[i], "");
            ck_set(s_st_raid_state[i], "");
        }
    }

    for (int i = 0; i < 4; i++) {
        if (ok && i < s_st.ndisks) {
            ck_set(s_st_io_name[i], s_st.disks[i].dev);
            // 同一列小数位必须统一（176 / 72.0 混着最难看）
            snprintf(b, sizeof(b), "R %.0f W %.0f", s_st.disks[i].rd_kbs, s_st.disks[i].wr_kbs);
            ck_set(s_st_io_val[i], b);
        } else {
            ck_set(s_st_io_name[i], "");
            ck_set(s_st_io_val[i], "");
        }
    }
}

static void update_network(void)
{
    char b[64], s[64];
    bool ok = have();
    if (ok) {
        fmt_rate(b, sizeof(b), s_st.net.rx_kbs, &s_mb_down);
        ck_set(s_nw_down_v, b);
        ck_set(s_nw_down_u, rate_unit(s_mb_down, s_st.net.rx_kbs));
        fmt_rate(b, sizeof(b), s_st.net.tx_kbs, &s_mb_up);
        ck_set(s_nw_up_v, b);
        ck_set(s_nw_up_u, rate_unit(s_mb_up, s_st.net.tx_kbs));

        char c1[24], c2[24];
        fmt_cap(c1, sizeof(c1), s_st.net.rx_total_gb);
        fmt_cap(c2, sizeof(c2), s_st.net.tx_total_gb);
        snprintf(s, sizeof(s), "累计接收 %s", c1);
        ck_set(s_nw_down_sub, s);
        snprintf(s, sizeof(s), "累计发送 %s", c2);
        ck_set(s_nw_up_sub, s);

        char p1[24], p2[24];
        fmt_rate(p1, sizeof(p1), (float)ck_trend_peak(&s_nw_down), NULL);
        fmt_rate(p2, sizeof(p2), (float)ck_trend_peak(&s_nw_up), NULL);
        ck_set(s_nw_ctx_v[0], p1);
        ck_set(s_nw_ctx_v[1], p2);
        snprintf(b, sizeof(b), "%d ms", s_st.http_ms);
        ck_set(s_nw_ctx_v[2], b);
        snprintf(b, sizeof(b), "%lld", (long long)s_hist_seq);
        ck_set(s_nw_ctx_v[3], b);
    } else {
        ck_set(s_nw_down_v, "--");
        ck_set(s_nw_up_v, "--");
        ck_set(s_nw_down_sub, "");
        ck_set(s_nw_up_sub, "");
        for (int i = 0; i < 4; i++) ck_set(s_nw_ctx_v[i], "--");
    }

    int pd = ck_trend_peak(&s_nw_down), pu = ck_trend_peak(&s_nw_up);
    int top = nice_max(pd > pu ? pd : pu);
    if (top < 64) top = 64;
    ck_trend_range(&s_nw_down, top);
    ck_trend_range(&s_nw_up, top);
    ck_trend_range(&s_nw_mini_down, top);
    ck_trend_range(&s_nw_mini_up, top);
    for (int i = 0; i < 3; i++) {
        fmt_rate(b, sizeof(b), (float)(top - (top / 2) * i), NULL);
        ck_set(s_nw_axis[i], b);
    }
}

static void update_system(void)
{
    char b[96];
    bool ok = have();

    int up = 0;
    for (int i = 0; i < s_st.ndocker; i++) if (s_st.docker[i].up) up++;
    if (ok) snprintf(b, sizeof(b), "%d / %d 运行中", up, s_st.ndocker);
    else    b[0] = 0;
    ck_set(s_sy_cnt, b);

    for (int i = 0; i < 6; i++) {
        if (ok && i < s_st.ndocker) {
            ck_set(s_sy_dock[i], s_st.docker[i].n);
            ck_set(s_sy_dock_s[i], s_st.docker[i].up ? "运行中" : s_st.docker[i].s);
            uint32_t c = s_st.docker[i].up ? CK_OK : CK_IDLE;
            ck_set_color(s_sy_dock_s[i], c);
            if (s_sy_dot[i]) lv_obj_set_style_bg_color(s_sy_dot[i], lv_color_hex(c), 0);
        } else {
            // 空槽位整个隐藏：只留圆点会被看成"多了一个无名字的容器"
            ck_set(s_sy_dock[i], "");
            ck_set(s_sy_dock_s[i], "");
            if (s_sy_dot[i]) lv_obj_set_style_bg_opa(s_sy_dot[i], LV_OPA_TRANSP, 0);
        }
    }

    for (int i = 0; i < 7; i++) {
        if (ok && i < s_st.ntemps) {
            ck_set(s_sy_tname[i], s_st.temps[i].n);
            snprintf(b, sizeof(b), "%.1f°C", s_st.temps[i].c);
            ck_set(s_sy_tval[i], b);
            uint32_t c = CK_OK;
            if (s_st.temps[i].c >= 75) c = CK_DANGER;
            else if (s_st.temps[i].c >= 60) c = CK_WARN;
            ck_set_color(s_sy_tval[i], c);
            ck_bar_set_color(&s_sy_tbar[i], s_st.temps[i].c, c);
        } else {
            ck_set(s_sy_tname[i], "");
            ck_set(s_sy_tval[i], "");
            if (s_sy_tbar[i].fill) lv_obj_set_width(s_sy_tbar[i].fill, 0);
        }
    }

    for (int i = 0; i < 3; i++) {
        if (ok && i < s_st.nalerts) {
            uint32_t ac = strcmp(s_st.alerts[i].lv, "crit") == 0 ? CK_DANGER : CK_WARN;
            ck_set(s_sy_alert_lbl[i], s_st.alerts[i].m);
            ck_set_color(s_sy_alert_lbl[i], ac);
            if (s_sy_alert_dot[i]) {
                lv_obj_set_style_bg_color(s_sy_alert_dot[i], lv_color_hex(ac), 0);
                lv_obj_set_style_bg_opa(s_sy_alert_dot[i], LV_OPA_COVER, 0);
            }
        } else {
            ck_set(s_sy_alert_lbl[i], (i == 0 && ok) ? "无告警" : "");
            ck_set_color(s_sy_alert_lbl[i], (i == 0 && ok) ? CK_OK : CK_DIM);
            if (s_sy_alert_dot[i]) {
                lv_obj_set_style_bg_opa(s_sy_alert_dot[i], (i == 0 && ok) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
                if (i == 0 && ok) lv_obj_set_style_bg_color(s_sy_alert_dot[i], lv_color_hex(CK_OK), 0);
            }
        }
    }

    static const char *k[8] = { "主机", "端点", "HTTP", "轮询", "最近错误", "数据年龄", "Wi-Fi", "内部内存" };
    char a1[16];
    fmt_age(a1, sizeof(a1), data_age_s());
    const char *ip = fnos_net_ip();
    for (int i = 0; i < 8; i++) {
        char val[64];
        switch (i) {
        case 0: snprintf(val, sizeof(val), "%s", ok ? s_st.host : "--"); break;
        case 1: snprintf(val, sizeof(val), "%s:%d", FNOS_HOST, FNOS_PORT); break;
        case 2: snprintf(val, sizeof(val), "%d ms (状态 %d)", s_st.http_ms, s_st.last_status); break;
        case 3: snprintf(val, sizeof(val), "%u / %u", (unsigned)s_st.ok_count, (unsigned)s_st.fail_count); break;
        case 4: snprintf(val, sizeof(val), "%s", s_st.last_err[0] ? s_st.last_err : "无"); break;
        case 5: snprintf(val, sizeof(val), "%s", a1); break;
        case 6:
            if (fnos_net_rssi() != 0) snprintf(val, sizeof(val), "%s  %d dBm", ip[0] ? ip : "--", (int)fnos_net_rssi());
            else                      snprintf(val, sizeof(val), "%s", ip[0] ? ip : "未连接");
            break;
        default: snprintf(val, sizeof(val), "%u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024)); break;
        }
        ck_set(s_sy_kv_l[i], k[i]);
        ck_set(s_sy_kv_v[i], val);
    }
}

static void drain_history(void)
{
    fnos_sample_t smp[12];
    int64_t next = s_hist_seq;
    int n = fnos_data_hist_read(s_hist_seq, smp, 12, &next);
    if (n <= 0) return;
    static int div;                       // 隔点入图：图表 2 秒一个点，窗口仍是 3 分钟
    for (int i = 0; i < n; i++) {
        if ((div++ & 1) == 0) {
            ck_trend_push(&s_ov_cpu, (int)(smp[i].cpu + 0.5f));
            ck_trend_push(&s_ov_mem, (int)(smp[i].mem + 0.5f));
            ck_trend_push(&s_nw_down, (int)(smp[i].rx_kbs + 0.5f));
            ck_trend_push(&s_nw_up, (int)(smp[i].tx_kbs + 0.5f));
            ck_trend_push(&s_nw_mini_down, (int)(smp[i].rx_kbs + 0.5f));
            ck_trend_push(&s_nw_mini_up, (int)(smp[i].tx_kbs + 0.5f));
        }
    }
    s_hist_seq = next;
}

static void view_tick(lv_timer_t *t)
{
    (void)t;
#if CONFIG_FNOS_HEAP_DEBUG
    // 排障：单位标签在照片上看不到，把它的真实坐标/尺寸/状态打进串口（每 10 秒一次）
    {
        static int dbg_div;
        if ((dbg_div++ % 20) == 0 && s_ov_unit[0]) {
            lv_obj_t *par = lv_obj_get_parent(s_ov_unit[0]);
            ESP_LOGI("uidbg", "unit0 x=%d y=%d w=%d h=%d style_x=%d style_y=%d hidden=%d text='%s' par_w=%d",
                     (int)lv_obj_get_x(s_ov_unit[0]), (int)lv_obj_get_y(s_ov_unit[0]),
                     (int)lv_obj_get_width(s_ov_unit[0]), (int)lv_obj_get_height(s_ov_unit[0]),
                     (int)lv_obj_get_style_x(s_ov_unit[0], 0), (int)lv_obj_get_style_y(s_ov_unit[0], 0),
                     (int)lv_obj_has_flag(s_ov_unit[0], LV_OBJ_FLAG_HIDDEN),
                     lv_label_get_text(s_ov_unit[0]),
                     par ? (int)lv_obj_get_width(par) : -1);
            ESP_LOGI("uidbg", "val0 x=%d y=%d w=%d h=%d text='%s'",
                     (int)lv_obj_get_x(s_ov_val[0]), (int)lv_obj_get_y(s_ov_val[0]),
                     (int)lv_obj_get_width(s_ov_val[0]), (int)lv_obj_get_height(s_ov_val[0]),
                     lv_label_get_text(s_ov_val[0]));
        }
    }
#endif
    if (s_night_req != s_night) apply_night();   // LVGL 任务里落地夜间配色
    fnos_data_get(&s_st);
    drain_history();
    update_top();
    update_bottom();
    update_overview();
    update_storage();
    update_network();
    update_system();
}

/* ───────────────────────────── 入口 */

// 夜间配色（由 main.cpp 的夜间定时器调用）：纯黑底、面板更暗、趋势纹理降透明度。
// 不做整屏 alpha 合成（opa_layered 会让每帧都走一遍全屏图层，这板子绘制预算有限）。
// ⚠️ 线程安全红线：从 esp_timer 任务调用，**只能置标志**；
// 直接改 LVGL 样式会让 esp_timer 卡死刷爆看门狗、LVGL 任务停摆（闪屏/白屏）。
void fnos_view_set_night(bool on)
{
    s_night_req = on;
}

static void apply_night(void)
{
    bool on = s_night_req;
    if (s_night == on) return;
    s_night = on;
    uint32_t bg = on ? 0x000000 : CK_BG;
    uint32_t panel = on ? 0x0B0E12 : CK_PANEL;
    lv_opa_t bar_opa = on ? LV_OPA_10 : LV_OPA_20;
    if (s_chrome_scr)  lv_obj_set_style_bg_color(s_chrome_scr, lv_color_hex(bg), 0);
    if (s_chrome_top)  lv_obj_set_style_bg_color(s_chrome_top, lv_color_hex(panel), 0);
    if (s_chrome_bot)  lv_obj_set_style_bg_color(s_chrome_bot, lv_color_hex(panel), 0);
    if (s_chrome_rail) lv_obj_set_style_bg_color(s_chrome_rail, lv_color_hex(bg), 0);
    if (s_chrome_content) lv_obj_set_style_bg_color(s_chrome_content, lv_color_hex(bg), 0);
    lv_obj_t *bars[4] = { s_ov_cpu.bar, s_ov_mem.bar, s_nw_down.bar, s_nw_up.bar,
                          /* 只处理这 4 条主趋势；瓦片内迷你趋势同样降 */ };
    for (int i = 0; i < 4; i++) {
        if (bars[i]) lv_obj_set_style_bg_opa(bars[i], bar_opa, LV_PART_ITEMS);
    }
    lv_obj_t *mini[2] = { s_nw_mini_down.bar, s_nw_mini_up.bar };
    for (int i = 0; i < 2; i++) {
        if (mini[i]) lv_obj_set_style_bg_opa(mini[i], bar_opa, LV_PART_ITEMS);
    }
    lv_obj_invalidate(lv_screen_active());
}

void fnos_view_create(void)
{
    if (s_created) return;
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;
    lv_obj_set_style_bg_color(scr, lv_color_hex(CK_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_chrome_scr = scr;
    build_top(scr);
    build_rail(scr);
    build_bottom(scr);

    lv_obj_t *content = ck_obj(scr, CK_CONT_X, CK_CONT_Y, CK_CONT_W, CK_CONT_H, CK_BG, 0, false);
    s_chrome_content = content;
    if (!content) { ESP_LOGE(TAG, "content container alloc failed"); return; }
    for (int i = 0; i < PAGE_N; i++) {
        s_pages[i] = make_page(content);
        if (!s_pages[i]) { ESP_LOGE(TAG, "page %d alloc failed", i); return; }
    }
    build_overview(s_pages[PG_OVERVIEW]);
    build_storage(s_pages[PG_STORAGE]);
    build_network(s_pages[PG_NETWORK]);
    build_system(s_pages[PG_SYSTEM]);

    if (!s_ov_cpu.line || !s_ov_mem.line || !s_nw_down.line || !s_nw_up.line) {
        ESP_LOGE(TAG, "trend creation failed");
        return;
    }
    s_page = 0;
    apply_page();
    s_created = true;
    lv_timer_create(view_tick, 500, NULL);
    ESP_LOGI(TAG, "dashboard v2 created (4 pages, %d s window)", HIST_PTS);
}
