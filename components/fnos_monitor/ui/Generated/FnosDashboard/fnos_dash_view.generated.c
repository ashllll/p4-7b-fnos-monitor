/* 由 tools/ui_gen.py 生成，请勿手改。 */
#include "fnos_dash_view.generated.h"
#include "fnos_dash_strings.generated.h"
#include "kk_rect.h"
#include "kk_theme.h"
#include "fnos_fonts.h"

static void ev_nav_overview_on_click(lv_event_t *e)
{
    (void)e;
    FnosDashboardController_OnNavOverviewRequested();
}

static void ev_nav_storage_on_click(lv_event_t *e)
{
    (void)e;
    FnosDashboardController_OnNavStorageRequested();
}

static void ev_nav_network_on_click(lv_event_t *e)
{
    (void)e;
    FnosDashboardController_OnNavNetworkRequested();
}

static void ev_nav_system_on_click(lv_event_t *e)
{
    (void)e;
    FnosDashboardController_OnNavSystemRequested();
}

static void ev_p0_overview_on_swipe_left(void)
{
    FnosDashboardController_OnOverviewNextRequested();
}

static void ev_p0_overview_on_swipe_right(void)
{
    FnosDashboardController_OnOverviewPrevRequested();
}

static void ev_p1_storage_on_swipe_left(void)
{
    FnosDashboardController_OnStorageNextRequested();
}

static void ev_p1_storage_on_swipe_right(void)
{
    FnosDashboardController_OnStoragePrevRequested();
}

static void ev_p2_network_on_swipe_left(void)
{
    FnosDashboardController_OnNetworkNextRequested();
}

static void ev_p2_network_on_swipe_right(void)
{
    FnosDashboardController_OnNetworkPrevRequested();
}

static void ev_p3_system_on_swipe_left(void)
{
    FnosDashboardController_OnSystemNextRequested();
}

static void ev_p3_system_on_swipe_right(void)
{
    FnosDashboardController_OnSystemPrevRequested();
}

static void __attribute__((noinline)) build_hd_icon(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->hd_icon = kk_icon_create(parent, kk_rect(0, 0, 0, 0, 7, 7, 26, 26), KK_ICON_STORAGE, kk_c(0xFFFFFF));
    lv_obj_remove_flag(v->hd_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->hd_icon, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_hd_icon_block(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->hd_icon_block = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 12, 40, 40));
    kk_style_bg(v->hd_icon_block, kk_c(0x2F80ED), kk_opa(0xFF));
    lv_obj_set_style_radius(v->hd_icon_block, 10, 0);
    lv_obj_remove_flag(v->hd_icon_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->hd_icon_block, LV_OBJ_FLAG_SCROLLABLE);
    build_hd_icon(v, v->hd_icon_block);
}

static void __attribute__((noinline)) build_hd_host(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->hd_host = kk_label_create(parent, kk_rect(0, 0, 0, 0, 72, 6, 240, 32));
    lv_obj_remove_flag(v->hd_host, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->hd_host, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->hd_host, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->hd_host, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->hd_host, kk_opa(0xFF), 0);
    lv_label_set_text(v->hd_host, "");
    kk_label_vcenter(v->hd_host, 32);
}

static void __attribute__((noinline)) build_hd_endpoint(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->hd_endpoint = kk_label_create(parent, kk_rect(0, 0, 0, 0, 72, 41, 320, 17));
    lv_obj_remove_flag(v->hd_endpoint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->hd_endpoint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->hd_endpoint, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->hd_endpoint, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->hd_endpoint, kk_opa(0xFF), 0);
    lv_label_set_text(v->hd_endpoint, "");
    kk_label_vcenter(v->hd_endpoint, 17);
}

static void __attribute__((noinline)) build_hd_chip(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->hd_chip = kk_chip_create(parent, kk_rect(0, 0, 0, 0, 665, 17, 184, 30), kk_c(0xF2B01E));
    lv_obj_remove_flag(v->hd_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->hd_chip, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_hd_signal(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_signal_bars(parent, kk_rect(0, 0, 0, 0, 861, 22, 27, 19), v->hd_signal);
}

static void __attribute__((noinline)) build_hd_clock(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->hd_clock = kk_label_create(parent, kk_rect(0, 0, 0, 0, 904, 14, 100, 37));
    lv_obj_remove_flag(v->hd_clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->hd_clock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->hd_clock, &ui_font_num_28, 0);
    lv_obj_set_style_text_color(v->hd_clock, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->hd_clock, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->hd_clock, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->hd_clock, "");
    kk_label_vcenter(v->hd_clock, 37);
}

static void __attribute__((noinline)) build_top_bar(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->top_bar = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 0, 1024, 64));
    kk_style_bg(v->top_bar, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->top_bar, 8, 0);
    lv_obj_remove_flag(v->top_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->top_bar, LV_OBJ_FLAG_SCROLLABLE);
    build_hd_icon_block(v, v->top_bar);
    build_hd_host(v, v->top_bar);
    build_hd_endpoint(v, v->top_bar);
    build_hd_chip(v, v->top_bar);
    build_hd_signal(v, v->top_bar);
    build_hd_clock(v, v->top_bar);
}

static void __attribute__((noinline)) build_nav_overview_label(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nav_overview_label = kk_label_create(parent, kk_rect(0, 0, 0, 0, 0, 10, 237, 23));
    lv_obj_remove_flag(v->nav_overview_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nav_overview_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nav_overview_label, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->nav_overview_label, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nav_overview_label, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->nav_overview_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(v->nav_overview_label, FNOS_STR_CHROME_NAV_OVERVIEW);
    kk_label_vcenter(v->nav_overview_label, 23);
}

static void __attribute__((noinline)) build_nav_overview(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nav_overview = kk_button_create(parent, kk_rect(0, 0, 0, 0, 20, 0, 237, 44));
    kk_style_bg(v->nav_overview, kk_c(0x2F80ED), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nav_overview, 8, 0);
    build_nav_overview_label(v, v->nav_overview);
}

static void __attribute__((noinline)) build_nav_storage_label(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nav_storage_label = kk_label_create(parent, kk_rect(0, 0, 0, 0, 0, 10, 237, 23));
    lv_obj_remove_flag(v->nav_storage_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nav_storage_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nav_storage_label, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->nav_storage_label, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nav_storage_label, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->nav_storage_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(v->nav_storage_label, FNOS_STR_CHROME_NAV_STORAGE);
    kk_label_vcenter(v->nav_storage_label, 23);
}

static void __attribute__((noinline)) build_nav_storage(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nav_storage = kk_button_create(parent, kk_rect(0, 0, 0, 0, 269, 0, 237, 44));
    kk_style_bg(v->nav_storage, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nav_storage, 8, 0);
    build_nav_storage_label(v, v->nav_storage);
}

static void __attribute__((noinline)) build_nav_network_label(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nav_network_label = kk_label_create(parent, kk_rect(0, 0, 0, 0, 0, 10, 237, 23));
    lv_obj_remove_flag(v->nav_network_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nav_network_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nav_network_label, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->nav_network_label, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nav_network_label, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->nav_network_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(v->nav_network_label, FNOS_STR_CHROME_NAV_NETWORK);
    kk_label_vcenter(v->nav_network_label, 23);
}

static void __attribute__((noinline)) build_nav_network(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nav_network = kk_button_create(parent, kk_rect(0, 0, 0, 0, 518, 0, 237, 44));
    kk_style_bg(v->nav_network, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nav_network, 8, 0);
    build_nav_network_label(v, v->nav_network);
}

static void __attribute__((noinline)) build_nav_system_label(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nav_system_label = kk_label_create(parent, kk_rect(0, 0, 0, 0, 0, 10, 237, 23));
    lv_obj_remove_flag(v->nav_system_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nav_system_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nav_system_label, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->nav_system_label, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nav_system_label, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->nav_system_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(v->nav_system_label, FNOS_STR_CHROME_NAV_SYSTEM);
    kk_label_vcenter(v->nav_system_label, 23);
}

static void __attribute__((noinline)) build_nav_system(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nav_system = kk_button_create(parent, kk_rect(0, 0, 0, 0, 767, 0, 237, 44));
    kk_style_bg(v->nav_system, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nav_system, 8, 0);
    build_nav_system_label(v, v->nav_system);
}

static void __attribute__((noinline)) build_nav_indicator(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nav_indicator = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 44, 38, 189, 3));
    kk_style_bg(v->nav_indicator, kk_c(0xFFFFFF), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nav_indicator, 2, 0);
    lv_obj_remove_flag(v->nav_indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nav_indicator, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_rail(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->rail = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 72, 1024, 44));
    lv_obj_remove_flag(v->rail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->rail, LV_OBJ_FLAG_SCROLLABLE);
    build_nav_overview(v, v->rail);
    build_nav_storage(v, v->rail);
    build_nav_network(v, v->rail);
    build_nav_system(v, v->rail);
    build_nav_indicator(v, v->rail);
}

static void __attribute__((noinline)) build_ft_poll(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ft_poll = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 9, 520, 17));
    lv_obj_remove_flag(v->ft_poll, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ft_poll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ft_poll, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->ft_poll, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ft_poll, kk_opa(0xFF), 0);
    lv_label_set_text(v->ft_poll, "");
    kk_label_vcenter(v->ft_poll, 17);
}

static void __attribute__((noinline)) build_ft_alert_dot(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ft_alert_dot = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 9, 10, 10));
    kk_style_bg(v->ft_alert_dot, kk_c(0x2FBF71), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ft_alert_dot, 5, 0);
    lv_obj_remove_flag(v->ft_alert_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ft_alert_dot, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_ft_alert_lbl(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ft_alert_lbl = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 5, 378, 17));
    lv_obj_remove_flag(v->ft_alert_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ft_alert_lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ft_alert_lbl, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->ft_alert_lbl, kk_c(0x2FBF71), 0);
    lv_obj_set_style_text_opa(v->ft_alert_lbl, kk_opa(0xFF), 0);
    lv_label_set_text(v->ft_alert_lbl, "");
    kk_label_vcenter(v->ft_alert_lbl, 17);
}

static void __attribute__((noinline)) build_ft_alert_pill(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ft_alert_pill = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 584, 4, 420, 28));
    kk_style_bg(v->ft_alert_pill, kk_c(0x232E3F), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ft_alert_pill, 8, 0);
    lv_obj_remove_flag(v->ft_alert_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ft_alert_pill, LV_OBJ_FLAG_SCROLLABLE);
    build_ft_alert_dot(v, v->ft_alert_pill);
    build_ft_alert_lbl(v, v->ft_alert_pill);
}

static void __attribute__((noinline)) build_bottom(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->bottom = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 564, 1024, 36));
    kk_style_bg(v->bottom, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->bottom, 8, 0);
    lv_obj_remove_flag(v->bottom, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->bottom, LV_OBJ_FLAG_SCROLLABLE);
    build_ft_poll(v, v->bottom);
    build_ft_alert_pill(v, v->bottom);
}

static void __attribute__((noinline)) build_ov_health_accent(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_health_accent = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 16, 3, 106));
    kk_style_bg(v->ov_health_accent, kk_c(0x2FBF71), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_health_accent, 2, 0);
    lv_obj_remove_flag(v->ov_health_accent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_health_accent, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_ov_health_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_health_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 260, 32));
    lv_obj_remove_flag(v->ov_health_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_health_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_health_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->ov_health_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_health_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_health_title, FNOS_STR_OVERVIEW_HEALTH_TITLE);
    kk_label_vcenter(v->ov_health_title, 32);
}

static void __attribute__((noinline)) build_ov_health_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_health_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 280, 34));
    kk_style_bg(v->ov_health_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_health_head, 8, 0);
    lv_obj_remove_flag(v->ov_health_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_health_head, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_health_title(v, v->ov_health_head);
}

static void __attribute__((noinline)) build_ov_verdict(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_verdict = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 50, 120, 52));
    lv_obj_remove_flag(v->ov_verdict, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_verdict, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_verdict, &ui_font_cjk_40, 0);
    lv_obj_set_style_text_color(v->ov_verdict, kk_c(0xF2B01E), 0);
    lv_obj_set_style_text_opa(v->ov_verdict, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_verdict, "");
    kk_label_vcenter(v->ov_verdict, 52);
}

static void __attribute__((noinline)) build_ov_alert_label(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_alert_label = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 7, 70, 17));
    lv_obj_remove_flag(v->ov_alert_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_alert_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_alert_label, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->ov_alert_label, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->ov_alert_label, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_alert_label, FNOS_STR_OVERVIEW_ALERT_LABEL);
    kk_label_vcenter(v->ov_alert_label, 17);
}

static void __attribute__((noinline)) build_ov_verdict_sub(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_verdict_sub = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 28, 136, 17));
    lv_obj_remove_flag(v->ov_verdict_sub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_verdict_sub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_verdict_sub, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->ov_verdict_sub, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_verdict_sub, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_verdict_sub, "");
    kk_label_vcenter(v->ov_verdict_sub, 17);
}

static void __attribute__((noinline)) build_ov_alert_block(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_alert_block = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 136, 50, 156, 52));
    kk_style_bg(v->ov_alert_block, kk_c(0x232E3F), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_alert_block, 8, 0);
    lv_obj_remove_flag(v->ov_alert_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_alert_block, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_alert_label(v, v->ov_alert_block);
    build_ov_verdict_sub(v, v->ov_alert_block);
}

static void __attribute__((noinline)) build_ov_reason(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_reason = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 108, 280, 17));
    lv_obj_remove_flag(v->ov_reason, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_reason, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_reason, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->ov_reason, kk_c(0xF0453D), 0);
    lv_obj_set_style_text_opa(v->ov_reason, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_reason, "");
    kk_label_vcenter(v->ov_reason, 17);
}

static void __attribute__((noinline)) build_ov_health_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_health_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 0, 304, 138));
    kk_style_bg(v->ov_health_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_health_card, 16, 0);
    lv_obj_set_style_border_width(v->ov_health_card, 1, 0);
    lv_obj_set_style_border_color(v->ov_health_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->ov_health_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->ov_health_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_health_card, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_health_accent(v, v->ov_health_card);
    build_ov_health_head(v, v->ov_health_card);
    build_ov_verdict(v, v->ov_health_card);
    build_ov_alert_block(v, v->ov_health_card);
    build_ov_reason(v, v->ov_health_card);
}

static void __attribute__((noinline)) build_ov_cpu_card_accent(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_cpu_card_accent = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 16, 3, 106));
    kk_style_bg(v->ov_cpu_card_accent, kk_c(0x2F80ED), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_cpu_card_accent, 2, 0);
    lv_obj_remove_flag(v->ov_cpu_card_accent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_cpu_card_accent, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_ov_cpu_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_cpu_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 118, 32));
    lv_obj_remove_flag(v->ov_cpu_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_cpu_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_cpu_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->ov_cpu_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_cpu_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_cpu_title, FNOS_STR_OVERVIEW_METRIC_CPU);
    kk_label_vcenter(v->ov_cpu_title, 32);
}

static void __attribute__((noinline)) build_ov_cpu_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_cpu_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 138, 34));
    kk_style_bg(v->ov_cpu_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_cpu_head, 8, 0);
    lv_obj_remove_flag(v->ov_cpu_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_cpu_head, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_cpu_title(v, v->ov_cpu_head);
}

static void __attribute__((noinline)) build_ov_val0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_val0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 52, 112, 37));
    lv_obj_remove_flag(v->ov_val0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_val0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_val0, &ui_font_num_28, 0);
    lv_obj_set_style_text_color(v->ov_val0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_val0, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_val0, "");
    kk_label_vcenter(v->ov_val0, 37);
}

static void __attribute__((noinline)) build_ov_unit0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_unit0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 126, 69, 26, 20));
    lv_obj_remove_flag(v->ov_unit0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_unit0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_unit0, &ui_font_txt_15, 0);
    lv_obj_set_style_text_color(v->ov_unit0, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_unit0, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->ov_unit0, 1, 0);
    lv_label_set_text(v->ov_unit0, FNOS_STR_OVERVIEW_UNIT_PCT);
    kk_label_vcenter(v->ov_unit0, 20);
}

static void __attribute__((noinline)) build_ov_mbar0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->ov_mbar0, parent, kk_rect(0, 0, 0, 0, 12, 94, 138, 6), kk_c(0x2F80ED), 3);
}

static void __attribute__((noinline)) build_ov_sub0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_sub0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 106, 138, 17));
    lv_obj_remove_flag(v->ov_sub0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_sub0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_sub0, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->ov_sub0, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->ov_sub0, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_sub0, "");
    kk_label_vcenter(v->ov_sub0, 17);
}

static void __attribute__((noinline)) build_ov_cpu_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_cpu_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 312, 0, 162, 138));
    kk_style_bg(v->ov_cpu_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_cpu_card, 16, 0);
    lv_obj_set_style_border_width(v->ov_cpu_card, 1, 0);
    lv_obj_set_style_border_color(v->ov_cpu_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->ov_cpu_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->ov_cpu_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_cpu_card, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_cpu_card_accent(v, v->ov_cpu_card);
    build_ov_cpu_head(v, v->ov_cpu_card);
    build_ov_val0(v, v->ov_cpu_card);
    build_ov_unit0(v, v->ov_cpu_card);
    build_ov_mbar0(v, v->ov_cpu_card);
    build_ov_sub0(v, v->ov_cpu_card);
}

static void __attribute__((noinline)) build_ov_mem_card_accent(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_mem_card_accent = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 16, 3, 106));
    kk_style_bg(v->ov_mem_card_accent, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_mem_card_accent, 2, 0);
    lv_obj_remove_flag(v->ov_mem_card_accent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_mem_card_accent, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_ov_mem_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_mem_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 118, 32));
    lv_obj_remove_flag(v->ov_mem_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_mem_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_mem_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->ov_mem_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_mem_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_mem_title, FNOS_STR_OVERVIEW_METRIC_MEM);
    kk_label_vcenter(v->ov_mem_title, 32);
}

static void __attribute__((noinline)) build_ov_mem_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_mem_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 138, 34));
    kk_style_bg(v->ov_mem_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_mem_head, 8, 0);
    lv_obj_remove_flag(v->ov_mem_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_mem_head, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_mem_title(v, v->ov_mem_head);
}

static void __attribute__((noinline)) build_ov_val1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_val1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 52, 112, 37));
    lv_obj_remove_flag(v->ov_val1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_val1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_val1, &ui_font_num_28, 0);
    lv_obj_set_style_text_color(v->ov_val1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_val1, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_val1, "");
    kk_label_vcenter(v->ov_val1, 37);
}

static void __attribute__((noinline)) build_ov_unit1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_unit1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 126, 69, 26, 20));
    lv_obj_remove_flag(v->ov_unit1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_unit1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_unit1, &ui_font_txt_15, 0);
    lv_obj_set_style_text_color(v->ov_unit1, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_unit1, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->ov_unit1, 1, 0);
    lv_label_set_text(v->ov_unit1, FNOS_STR_OVERVIEW_UNIT_PCT);
    kk_label_vcenter(v->ov_unit1, 20);
}

static void __attribute__((noinline)) build_ov_mbar1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->ov_mbar1, parent, kk_rect(0, 0, 0, 0, 12, 94, 138, 6), kk_c(0x4EC9E8), 3);
}

static void __attribute__((noinline)) build_ov_sub1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_sub1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 106, 138, 17));
    lv_obj_remove_flag(v->ov_sub1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_sub1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_sub1, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->ov_sub1, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->ov_sub1, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_sub1, "");
    kk_label_vcenter(v->ov_sub1, 17);
}

static void __attribute__((noinline)) build_ov_mem_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_mem_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 482, 0, 162, 138));
    kk_style_bg(v->ov_mem_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_mem_card, 16, 0);
    lv_obj_set_style_border_width(v->ov_mem_card, 1, 0);
    lv_obj_set_style_border_color(v->ov_mem_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->ov_mem_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->ov_mem_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_mem_card, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_mem_card_accent(v, v->ov_mem_card);
    build_ov_mem_head(v, v->ov_mem_card);
    build_ov_val1(v, v->ov_mem_card);
    build_ov_unit1(v, v->ov_mem_card);
    build_ov_mbar1(v, v->ov_mem_card);
    build_ov_sub1(v, v->ov_mem_card);
}

static void __attribute__((noinline)) build_ov_temp_card_accent(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_temp_card_accent = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 16, 3, 106));
    kk_style_bg(v->ov_temp_card_accent, kk_c(0xFF7A1A), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_temp_card_accent, 2, 0);
    lv_obj_remove_flag(v->ov_temp_card_accent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_temp_card_accent, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_ov_temp_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_temp_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 118, 32));
    lv_obj_remove_flag(v->ov_temp_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_temp_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_temp_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->ov_temp_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_temp_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_temp_title, FNOS_STR_OVERVIEW_METRIC_TEMP);
    kk_label_vcenter(v->ov_temp_title, 32);
}

static void __attribute__((noinline)) build_ov_temp_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_temp_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 138, 34));
    kk_style_bg(v->ov_temp_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_temp_head, 8, 0);
    lv_obj_remove_flag(v->ov_temp_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_temp_head, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_temp_title(v, v->ov_temp_head);
}

static void __attribute__((noinline)) build_ov_val2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_val2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 52, 112, 37));
    lv_obj_remove_flag(v->ov_val2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_val2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_val2, &ui_font_num_28, 0);
    lv_obj_set_style_text_color(v->ov_val2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_val2, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_val2, "");
    kk_label_vcenter(v->ov_val2, 37);
}

static void __attribute__((noinline)) build_ov_unit2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_unit2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 126, 69, 26, 20));
    lv_obj_remove_flag(v->ov_unit2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_unit2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_unit2, &ui_font_txt_15, 0);
    lv_obj_set_style_text_color(v->ov_unit2, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_unit2, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->ov_unit2, 1, 0);
    lv_label_set_text(v->ov_unit2, FNOS_STR_OVERVIEW_UNIT_C);
    kk_label_vcenter(v->ov_unit2, 20);
}

static void __attribute__((noinline)) build_ov_mbar2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->ov_mbar2, parent, kk_rect(0, 0, 0, 0, 12, 94, 138, 6), kk_c(0xFF7A1A), 3);
}

static void __attribute__((noinline)) build_ov_sub2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_sub2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 106, 138, 17));
    lv_obj_remove_flag(v->ov_sub2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_sub2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_sub2, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->ov_sub2, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->ov_sub2, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_sub2, "");
    kk_label_vcenter(v->ov_sub2, 17);
}

static void __attribute__((noinline)) build_ov_temp_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_temp_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 652, 0, 162, 138));
    kk_style_bg(v->ov_temp_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_temp_card, 16, 0);
    lv_obj_set_style_border_width(v->ov_temp_card, 1, 0);
    lv_obj_set_style_border_color(v->ov_temp_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->ov_temp_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->ov_temp_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_temp_card, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_temp_card_accent(v, v->ov_temp_card);
    build_ov_temp_head(v, v->ov_temp_card);
    build_ov_val2(v, v->ov_temp_card);
    build_ov_unit2(v, v->ov_temp_card);
    build_ov_mbar2(v, v->ov_temp_card);
    build_ov_sub2(v, v->ov_temp_card);
}

static void __attribute__((noinline)) build_ov_uptime_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_uptime_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 118, 32));
    lv_obj_remove_flag(v->ov_uptime_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_uptime_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_uptime_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->ov_uptime_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_uptime_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_uptime_title, FNOS_STR_OVERVIEW_METRIC_UPTIME);
    kk_label_vcenter(v->ov_uptime_title, 32);
}

static void __attribute__((noinline)) build_ov_uptime_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_uptime_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 138, 34));
    kk_style_bg(v->ov_uptime_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_uptime_head, 8, 0);
    lv_obj_remove_flag(v->ov_uptime_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_uptime_head, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_uptime_title(v, v->ov_uptime_head);
}

static void __attribute__((noinline)) build_ov_val3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_val3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 52, 112, 37));
    lv_obj_remove_flag(v->ov_val3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_val3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_val3, &ui_font_num_28, 0);
    lv_obj_set_style_text_color(v->ov_val3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_val3, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_val3, "");
    kk_label_vcenter(v->ov_val3, 37);
}

static void __attribute__((noinline)) build_ov_unit3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_unit3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 126, 69, 26, 20));
    lv_obj_remove_flag(v->ov_unit3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_unit3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_unit3, &ui_font_txt_15, 0);
    lv_obj_set_style_text_color(v->ov_unit3, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_unit3, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->ov_unit3, 1, 0);
    lv_label_set_text(v->ov_unit3, FNOS_STR_OVERVIEW_UNIT_NONE);
    kk_label_vcenter(v->ov_unit3, 20);
}

static void __attribute__((noinline)) build_ov_sub3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_sub3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 106, 138, 17));
    lv_obj_remove_flag(v->ov_sub3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_sub3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_sub3, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->ov_sub3, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->ov_sub3, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_sub3, "");
    kk_label_vcenter(v->ov_sub3, 17);
}

static void __attribute__((noinline)) build_ov_uptime_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_uptime_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 822, 0, 162, 138));
    kk_style_bg(v->ov_uptime_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_uptime_card, 16, 0);
    lv_obj_set_style_border_width(v->ov_uptime_card, 1, 0);
    lv_obj_set_style_border_color(v->ov_uptime_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->ov_uptime_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->ov_uptime_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_uptime_card, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_uptime_head(v, v->ov_uptime_card);
    build_ov_val3(v, v->ov_uptime_card);
    build_ov_unit3(v, v->ov_uptime_card);
    build_ov_sub3(v, v->ov_uptime_card);
}

static void __attribute__((noinline)) build_ov_storage_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_storage_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 476, 32));
    lv_obj_remove_flag(v->ov_storage_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_storage_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_storage_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->ov_storage_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_storage_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_storage_title, FNOS_STR_OVERVIEW_STORAGE_TITLE);
    kk_label_vcenter(v->ov_storage_title, 32);
}

static void __attribute__((noinline)) build_ov_storage_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_storage_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 496, 34));
    kk_style_bg(v->ov_storage_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_storage_head, 8, 0);
    lv_obj_remove_flag(v->ov_storage_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_storage_head, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_storage_title(v, v->ov_storage_head);
}

static void __attribute__((noinline)) build_ov_vol_name0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_name0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 120, 23));
    lv_obj_remove_flag(v->ov_vol_name0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_name0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_name0, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_name0, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_vol_name0, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_vol_name0, "");
    kk_label_vcenter(v->ov_vol_name0, 23);
}

static void __attribute__((noinline)) build_ov_vol_pct0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_pct0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 404, 3, 80, 23));
    lv_obj_remove_flag(v->ov_vol_pct0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_pct0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_pct0, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_pct0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_vol_pct0, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->ov_vol_pct0, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->ov_vol_pct0, "");
    kk_label_vcenter(v->ov_vol_pct0, 23);
}

static void __attribute__((noinline)) build_ov_vol_bar0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->ov_vol_bar0, parent, kk_rect(0, 0, 0, 0, 12, 27, 472, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_ov_vol_row0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_row0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 48, 496, 36));
    kk_style_bg(v->ov_vol_row0, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_vol_row0, 8, 0);
    lv_obj_remove_flag(v->ov_vol_row0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_row0, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_vol_name0(v, v->ov_vol_row0);
    build_ov_vol_pct0(v, v->ov_vol_row0);
    build_ov_vol_bar0(v, v->ov_vol_row0);
}

static void __attribute__((noinline)) build_ov_vol_name1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_name1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 120, 23));
    lv_obj_remove_flag(v->ov_vol_name1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_name1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_name1, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_name1, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_vol_name1, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_vol_name1, "");
    kk_label_vcenter(v->ov_vol_name1, 23);
}

static void __attribute__((noinline)) build_ov_vol_pct1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_pct1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 404, 3, 80, 23));
    lv_obj_remove_flag(v->ov_vol_pct1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_pct1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_pct1, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_pct1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_vol_pct1, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->ov_vol_pct1, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->ov_vol_pct1, "");
    kk_label_vcenter(v->ov_vol_pct1, 23);
}

static void __attribute__((noinline)) build_ov_vol_bar1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->ov_vol_bar1, parent, kk_rect(0, 0, 0, 0, 12, 27, 472, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_ov_vol_row1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_row1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 86, 496, 36));
    kk_style_bg(v->ov_vol_row1, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_vol_row1, 8, 0);
    lv_obj_remove_flag(v->ov_vol_row1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_row1, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_vol_name1(v, v->ov_vol_row1);
    build_ov_vol_pct1(v, v->ov_vol_row1);
    build_ov_vol_bar1(v, v->ov_vol_row1);
}

static void __attribute__((noinline)) build_ov_vol_name2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_name2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 120, 23));
    lv_obj_remove_flag(v->ov_vol_name2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_name2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_name2, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_name2, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_vol_name2, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_vol_name2, "");
    kk_label_vcenter(v->ov_vol_name2, 23);
}

static void __attribute__((noinline)) build_ov_vol_pct2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_pct2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 404, 3, 80, 23));
    lv_obj_remove_flag(v->ov_vol_pct2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_pct2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_pct2, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_pct2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_vol_pct2, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->ov_vol_pct2, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->ov_vol_pct2, "");
    kk_label_vcenter(v->ov_vol_pct2, 23);
}

static void __attribute__((noinline)) build_ov_vol_bar2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->ov_vol_bar2, parent, kk_rect(0, 0, 0, 0, 12, 27, 472, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_ov_vol_row2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_row2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 124, 496, 36));
    kk_style_bg(v->ov_vol_row2, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_vol_row2, 8, 0);
    lv_obj_remove_flag(v->ov_vol_row2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_row2, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_vol_name2(v, v->ov_vol_row2);
    build_ov_vol_pct2(v, v->ov_vol_row2);
    build_ov_vol_bar2(v, v->ov_vol_row2);
}

static void __attribute__((noinline)) build_ov_vol_name3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_name3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 120, 23));
    lv_obj_remove_flag(v->ov_vol_name3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_name3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_name3, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_name3, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_vol_name3, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_vol_name3, "");
    kk_label_vcenter(v->ov_vol_name3, 23);
}

static void __attribute__((noinline)) build_ov_vol_pct3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_pct3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 404, 3, 80, 23));
    lv_obj_remove_flag(v->ov_vol_pct3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_pct3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_pct3, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_pct3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_vol_pct3, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->ov_vol_pct3, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->ov_vol_pct3, "");
    kk_label_vcenter(v->ov_vol_pct3, 23);
}

static void __attribute__((noinline)) build_ov_vol_bar3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->ov_vol_bar3, parent, kk_rect(0, 0, 0, 0, 12, 27, 472, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_ov_vol_row3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_row3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 162, 496, 36));
    kk_style_bg(v->ov_vol_row3, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_vol_row3, 8, 0);
    lv_obj_remove_flag(v->ov_vol_row3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_row3, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_vol_name3(v, v->ov_vol_row3);
    build_ov_vol_pct3(v, v->ov_vol_row3);
    build_ov_vol_bar3(v, v->ov_vol_row3);
}

static void __attribute__((noinline)) build_ov_vol_name4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_name4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 120, 23));
    lv_obj_remove_flag(v->ov_vol_name4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_name4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_name4, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_name4, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_vol_name4, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_vol_name4, "");
    kk_label_vcenter(v->ov_vol_name4, 23);
}

static void __attribute__((noinline)) build_ov_vol_pct4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_pct4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 404, 3, 80, 23));
    lv_obj_remove_flag(v->ov_vol_pct4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_pct4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_pct4, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_pct4, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_vol_pct4, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->ov_vol_pct4, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->ov_vol_pct4, "");
    kk_label_vcenter(v->ov_vol_pct4, 23);
}

static void __attribute__((noinline)) build_ov_vol_bar4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->ov_vol_bar4, parent, kk_rect(0, 0, 0, 0, 12, 27, 472, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_ov_vol_row4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_row4 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 200, 496, 36));
    kk_style_bg(v->ov_vol_row4, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_vol_row4, 8, 0);
    lv_obj_remove_flag(v->ov_vol_row4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_row4, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_vol_name4(v, v->ov_vol_row4);
    build_ov_vol_pct4(v, v->ov_vol_row4);
    build_ov_vol_bar4(v, v->ov_vol_row4);
}

static void __attribute__((noinline)) build_ov_vol_name5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_name5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 120, 23));
    lv_obj_remove_flag(v->ov_vol_name5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_name5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_name5, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_name5, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_vol_name5, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_vol_name5, "");
    kk_label_vcenter(v->ov_vol_name5, 23);
}

static void __attribute__((noinline)) build_ov_vol_pct5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_pct5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 404, 3, 80, 23));
    lv_obj_remove_flag(v->ov_vol_pct5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_pct5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_vol_pct5, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->ov_vol_pct5, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_vol_pct5, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->ov_vol_pct5, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->ov_vol_pct5, "");
    kk_label_vcenter(v->ov_vol_pct5, 23);
}

static void __attribute__((noinline)) build_ov_vol_bar5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->ov_vol_bar5, parent, kk_rect(0, 0, 0, 0, 12, 27, 472, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_ov_vol_row5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_vol_row5 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 238, 496, 36));
    kk_style_bg(v->ov_vol_row5, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_vol_row5, 8, 0);
    lv_obj_remove_flag(v->ov_vol_row5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_vol_row5, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_vol_name5(v, v->ov_vol_row5);
    build_ov_vol_pct5(v, v->ov_vol_row5);
    build_ov_vol_bar5(v, v->ov_vol_row5);
}

static void __attribute__((noinline)) build_ov_storage_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_storage_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 150, 520, 282));
    kk_style_bg(v->ov_storage_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_storage_card, 16, 0);
    lv_obj_set_style_border_width(v->ov_storage_card, 1, 0);
    lv_obj_set_style_border_color(v->ov_storage_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->ov_storage_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->ov_storage_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_storage_card, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_storage_head(v, v->ov_storage_card);
    build_ov_vol_row0(v, v->ov_storage_card);
    build_ov_vol_row1(v, v->ov_storage_card);
    build_ov_vol_row2(v, v->ov_storage_card);
    build_ov_vol_row3(v, v->ov_storage_card);
    build_ov_vol_row4(v, v->ov_storage_card);
    build_ov_vol_row5(v, v->ov_storage_card);
}

static void __attribute__((noinline)) build_ov_trend_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_trend_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 408, 32));
    lv_obj_remove_flag(v->ov_trend_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_trend_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_trend_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->ov_trend_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->ov_trend_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->ov_trend_title, FNOS_STR_OVERVIEW_TREND_TITLE);
    kk_label_vcenter(v->ov_trend_title, 32);
}

static void __attribute__((noinline)) build_ov_trend_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_trend_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 428, 34));
    kk_style_bg(v->ov_trend_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_trend_head, 8, 0);
    lv_obj_remove_flag(v->ov_trend_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_trend_head, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_trend_title(v, v->ov_trend_head);
}

static void __attribute__((noinline)) build_ov_cpu(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_trend_create(&v->ov_cpu, parent, kk_rect(0, 0, 0, 0, 12, 52, 316, 100), kk_c(0x2F80ED), 90, 0, 2, 0);
}

static void __attribute__((noinline)) build_ov_legend_cpu_dot(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_legend_cpu_dot = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 340, 88, 10, 10));
    kk_style_bg(v->ov_legend_cpu_dot, kk_c(0x2F80ED), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_legend_cpu_dot, 5, 0);
    lv_obj_remove_flag(v->ov_legend_cpu_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_legend_cpu_dot, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_ov_legend_cpu_lbl(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_legend_cpu_lbl = kk_label_create(parent, kk_rect(0, 0, 0, 0, 352, 83, 100, 20));
    lv_obj_remove_flag(v->ov_legend_cpu_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_legend_cpu_lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_legend_cpu_lbl, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->ov_legend_cpu_lbl, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_legend_cpu_lbl, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->ov_legend_cpu_lbl, 1, 0);
    lv_label_set_text(v->ov_legend_cpu_lbl, "");
    kk_label_vcenter(v->ov_legend_cpu_lbl, 20);
}

static void __attribute__((noinline)) build_ov_mem(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_trend_create(&v->ov_mem, parent, kk_rect(0, 0, 0, 0, 12, 52, 316, 100), kk_c(0x4EC9E8), 90, 0, 2, 0);
}

static void __attribute__((noinline)) build_ov_legend_mem_dot(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_legend_mem_dot = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 340, 118, 10, 10));
    kk_style_bg(v->ov_legend_mem_dot, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_legend_mem_dot, 5, 0);
    lv_obj_remove_flag(v->ov_legend_mem_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_legend_mem_dot, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_ov_legend_mem_lbl(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_legend_mem_lbl = kk_label_create(parent, kk_rect(0, 0, 0, 0, 352, 113, 100, 20));
    lv_obj_remove_flag(v->ov_legend_mem_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_legend_mem_lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_legend_mem_lbl, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->ov_legend_mem_lbl, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_legend_mem_lbl, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->ov_legend_mem_lbl, 1, 0);
    lv_label_set_text(v->ov_legend_mem_lbl, "");
    kk_label_vcenter(v->ov_legend_mem_lbl, 20);
}

static void __attribute__((noinline)) build_ov_temp_trend(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_trend_create(&v->ov_temp_trend, parent, kk_rect(0, 0, 0, 0, 12, 164, 316, 100), kk_c(0xFF7A1A), 90, 12, 2, 1);
}

static void __attribute__((noinline)) build_ov_legend_temp_dot(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_legend_temp_dot = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 340, 198, 10, 10));
    kk_style_bg(v->ov_legend_temp_dot, kk_c(0xFF7A1A), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_legend_temp_dot, 5, 0);
    lv_obj_remove_flag(v->ov_legend_temp_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_legend_temp_dot, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_ov_legend_temp_lbl(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_legend_temp_lbl = kk_label_create(parent, kk_rect(0, 0, 0, 0, 352, 193, 100, 20));
    lv_obj_remove_flag(v->ov_legend_temp_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_legend_temp_lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->ov_legend_temp_lbl, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->ov_legend_temp_lbl, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->ov_legend_temp_lbl, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->ov_legend_temp_lbl, 1, 0);
    lv_label_set_text(v->ov_legend_temp_lbl, "");
    kk_label_vcenter(v->ov_legend_temp_lbl, 20);
}

static void __attribute__((noinline)) build_ov_trend_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->ov_trend_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 532, 150, 452, 282));
    kk_style_bg(v->ov_trend_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->ov_trend_card, 16, 0);
    lv_obj_set_style_border_width(v->ov_trend_card, 1, 0);
    lv_obj_set_style_border_color(v->ov_trend_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->ov_trend_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->ov_trend_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->ov_trend_card, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_trend_head(v, v->ov_trend_card);
    build_ov_cpu(v, v->ov_trend_card);
    build_ov_legend_cpu_dot(v, v->ov_trend_card);
    build_ov_legend_cpu_lbl(v, v->ov_trend_card);
    build_ov_mem(v, v->ov_trend_card);
    build_ov_legend_mem_dot(v, v->ov_trend_card);
    build_ov_legend_mem_lbl(v, v->ov_trend_card);
    build_ov_temp_trend(v, v->ov_trend_card);
    build_ov_legend_temp_dot(v, v->ov_trend_card);
    build_ov_legend_temp_lbl(v, v->ov_trend_card);
}

static void __attribute__((noinline)) build_p0_overview(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->p0_overview = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 0, 984, 432));
    kk_style_bg(v->p0_overview, kk_c(0x080B10), kk_opa(0xFF));
    lv_obj_add_flag(v->p0_overview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->p0_overview, LV_OBJ_FLAG_SCROLLABLE);
    build_ov_health_card(v, v->p0_overview);
    build_ov_cpu_card(v, v->p0_overview);
    build_ov_mem_card(v, v->p0_overview);
    build_ov_temp_card(v, v->p0_overview);
    build_ov_uptime_card(v, v->p0_overview);
    build_ov_storage_card(v, v->p0_overview);
    build_ov_trend_card(v, v->p0_overview);
}

static void __attribute__((noinline)) build_st_hero_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_hero_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 8, 340, 32));
    lv_obj_remove_flag(v->st_hero_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_hero_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_hero_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->st_hero_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_hero_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_hero_title, FNOS_STR_STORAGE_HERO_TITLE);
    kk_label_vcenter(v->st_hero_title, 32);
}

static void __attribute__((noinline)) build_st_hero(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_hero = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 40, 170, 73));
    lv_obj_remove_flag(v->st_hero, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_hero, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_hero, &ui_font_num_56, 0);
    lv_obj_set_style_text_color(v->st_hero, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_hero, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_hero, "");
    kk_label_vcenter(v->st_hero, 73);
}

static void __attribute__((noinline)) build_st_used(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_used = kk_label_create(parent, kk_rect(0, 0, 0, 0, 196, 90, 130, 23));
    lv_obj_remove_flag(v->st_used, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_used, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_used, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_used, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->st_used, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_used, "");
    kk_label_vcenter(v->st_used, 23);
}

static void __attribute__((noinline)) build_st_cnt(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_cnt = kk_label_create(parent, kk_rect(0, 0, 0, 0, 648, 12, 320, 17));
    lv_obj_remove_flag(v->st_cnt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_cnt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_cnt, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_cnt, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_cnt, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_cnt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_cnt, "");
    kk_label_vcenter(v->st_cnt, 17);
}

static void __attribute__((noinline)) build_st_free(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_free = kk_label_create(parent, kk_rect(0, 0, 0, 0, 648, 40, 320, 23));
    lv_obj_remove_flag(v->st_free, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_free, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_free, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_free, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_free, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_free, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_free, "");
    kk_label_vcenter(v->st_free, 23);
}

static void __attribute__((noinline)) build_st_seg0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_seg0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 16, 116, 8, 14));
    kk_style_bg(v->st_seg0, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_seg0, 3, 0);
    lv_obj_remove_flag(v->st_seg0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_seg0, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_seg1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_seg1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 27, 116, 8, 14));
    kk_style_bg(v->st_seg1, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_seg1, 3, 0);
    lv_obj_remove_flag(v->st_seg1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_seg1, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_seg2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_seg2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 38, 116, 8, 14));
    kk_style_bg(v->st_seg2, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_seg2, 3, 0);
    lv_obj_remove_flag(v->st_seg2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_seg2, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_seg3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_seg3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 49, 116, 8, 14));
    kk_style_bg(v->st_seg3, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_seg3, 3, 0);
    lv_obj_remove_flag(v->st_seg3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_seg3, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_seg4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_seg4 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 60, 116, 8, 14));
    kk_style_bg(v->st_seg4, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_seg4, 3, 0);
    lv_obj_remove_flag(v->st_seg4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_seg4, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_seg5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_seg5 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 71, 116, 8, 14));
    kk_style_bg(v->st_seg5, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_seg5, 3, 0);
    lv_obj_remove_flag(v->st_seg5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_seg5, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_zfs_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_zfs_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 4, 76, 23));
    lv_obj_remove_flag(v->st_zfs_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_zfs_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_zfs_title, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_zfs_title, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->st_zfs_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_zfs_title, FNOS_STR_STORAGE_ZFS_TITLE);
    kk_label_vcenter(v->st_zfs_title, 23);
}

static void __attribute__((noinline)) build_st_zfs_arc(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_zfs_arc = kk_label_create(parent, kk_rect(0, 0, 0, 0, 92, 4, 216, 23));
    lv_obj_remove_flag(v->st_zfs_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_zfs_arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_zfs_arc, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_zfs_arc, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_zfs_arc, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_zfs_arc, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_zfs_arc, "");
    kk_label_vcenter(v->st_zfs_arc, 23);
}

static void __attribute__((noinline)) build_st_zfs_hit(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_zfs_hit = kk_label_create(parent, kk_rect(0, 0, 0, 0, 92, 30, 216, 23));
    lv_obj_remove_flag(v->st_zfs_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_zfs_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_zfs_hit, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_zfs_hit, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_zfs_hit, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_zfs_hit, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_zfs_hit, "");
    kk_label_vcenter(v->st_zfs_hit, 23);
}

static void __attribute__((noinline)) build_st_zfs_bar(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_zfs_bar, parent, kk_rect(0, 0, 0, 0, 12, 56, 296, 6), kk_c(0xC8E63C), 3);
}

static void __attribute__((noinline)) build_st_zfs_block(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_zfs_block = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 648, 68, 320, 64));
    kk_style_bg(v->st_zfs_block, kk_c(0x232E3F), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_zfs_block, 8, 0);
    lv_obj_remove_flag(v->st_zfs_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_zfs_block, LV_OBJ_FLAG_SCROLLABLE);
    build_st_zfs_title(v, v->st_zfs_block);
    build_st_zfs_arc(v, v->st_zfs_block);
    build_st_zfs_hit(v, v->st_zfs_block);
    build_st_zfs_bar(v, v->st_zfs_block);
}

static void __attribute__((noinline)) build_st_hero_strip(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_hero_strip = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 0, 984, 138));
    kk_style_bg(v->st_hero_strip, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_hero_strip, 8, 0);
    lv_obj_remove_flag(v->st_hero_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_hero_strip, LV_OBJ_FLAG_SCROLLABLE);
    build_st_hero_title(v, v->st_hero_strip);
    build_st_hero(v, v->st_hero_strip);
    build_st_used(v, v->st_hero_strip);
    build_st_cnt(v, v->st_hero_strip);
    build_st_free(v, v->st_hero_strip);
    build_st_seg0(v, v->st_hero_strip);
    build_st_seg1(v, v->st_hero_strip);
    build_st_seg2(v, v->st_hero_strip);
    build_st_seg3(v, v->st_hero_strip);
    build_st_seg4(v, v->st_hero_strip);
    build_st_seg5(v, v->st_hero_strip);
    build_st_zfs_block(v, v->st_hero_strip);
}

static void __attribute__((noinline)) build_st_volume_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_volume_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 436, 32));
    lv_obj_remove_flag(v->st_volume_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_volume_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_volume_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->st_volume_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_volume_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_volume_title, FNOS_STR_STORAGE_VOLUME_TITLE);
    kk_label_vcenter(v->st_volume_title, 32);
}

static void __attribute__((noinline)) build_st_volume_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_volume_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 456, 34));
    kk_style_bg(v->st_volume_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_volume_head, 8, 0);
    lv_obj_remove_flag(v->st_volume_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_volume_head, LV_OBJ_FLAG_SCROLLABLE);
    build_st_volume_title(v, v->st_volume_head);
}

static void __attribute__((noinline)) build_st_vol_name0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_name0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 90, 23));
    lv_obj_remove_flag(v->st_vol_name0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_name0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_name0, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_vol_name0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_name0, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_name0, "");
    kk_label_vcenter(v->st_vol_name0, 23);
}

static void __attribute__((noinline)) build_st_vol_fs0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_fs0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 106, 6, 64, 17));
    lv_obj_remove_flag(v->st_vol_fs0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_fs0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_fs0, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_vol_fs0, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_vol_fs0, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_fs0, "");
    kk_label_vcenter(v->st_vol_fs0, 17);
}

static void __attribute__((noinline)) build_st_vol_use0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_use0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 174, 6, 180, 17));
    lv_obj_remove_flag(v->st_vol_use0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_use0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_use0, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_vol_use0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_use0, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_use0, "");
    kk_label_vcenter(v->st_vol_use0, 17);
}

static void __attribute__((noinline)) build_st_vol_pct0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_pct0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 364, 3, 80, 23));
    lv_obj_remove_flag(v->st_vol_pct0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_pct0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_pct0, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_vol_pct0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_pct0, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_vol_pct0, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_vol_pct0, "");
    kk_label_vcenter(v->st_vol_pct0, 23);
}

static void __attribute__((noinline)) build_st_vol_bar0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_vol_bar0, parent, kk_rect(0, 0, 0, 0, 12, 27, 432, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_st_vol_tick600(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick600 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 271, 18, 1, 18));
    kk_style_bg(v->st_vol_tick600, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick600, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick600, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_tick850(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick850 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 379, 18, 1, 18));
    kk_style_bg(v->st_vol_tick850, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick850, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick850, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_card0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_card0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 48, 456, 36));
    kk_style_bg(v->st_vol_card0, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_vol_card0, 8, 0);
    lv_obj_remove_flag(v->st_vol_card0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_card0, LV_OBJ_FLAG_SCROLLABLE);
    build_st_vol_name0(v, v->st_vol_card0);
    build_st_vol_fs0(v, v->st_vol_card0);
    build_st_vol_use0(v, v->st_vol_card0);
    build_st_vol_pct0(v, v->st_vol_card0);
    build_st_vol_bar0(v, v->st_vol_card0);
    build_st_vol_tick600(v, v->st_vol_card0);
    build_st_vol_tick850(v, v->st_vol_card0);
}

static void __attribute__((noinline)) build_st_vol_name1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_name1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 90, 23));
    lv_obj_remove_flag(v->st_vol_name1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_name1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_name1, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_vol_name1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_name1, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_name1, "");
    kk_label_vcenter(v->st_vol_name1, 23);
}

static void __attribute__((noinline)) build_st_vol_fs1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_fs1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 106, 6, 64, 17));
    lv_obj_remove_flag(v->st_vol_fs1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_fs1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_fs1, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_vol_fs1, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_vol_fs1, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_fs1, "");
    kk_label_vcenter(v->st_vol_fs1, 17);
}

static void __attribute__((noinline)) build_st_vol_use1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_use1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 174, 6, 180, 17));
    lv_obj_remove_flag(v->st_vol_use1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_use1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_use1, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_vol_use1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_use1, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_use1, "");
    kk_label_vcenter(v->st_vol_use1, 17);
}

static void __attribute__((noinline)) build_st_vol_pct1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_pct1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 364, 3, 80, 23));
    lv_obj_remove_flag(v->st_vol_pct1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_pct1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_pct1, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_vol_pct1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_pct1, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_vol_pct1, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_vol_pct1, "");
    kk_label_vcenter(v->st_vol_pct1, 23);
}

static void __attribute__((noinline)) build_st_vol_bar1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_vol_bar1, parent, kk_rect(0, 0, 0, 0, 12, 27, 432, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_st_vol_tick601(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick601 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 271, 18, 1, 18));
    kk_style_bg(v->st_vol_tick601, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick601, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick601, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_tick851(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick851 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 379, 18, 1, 18));
    kk_style_bg(v->st_vol_tick851, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick851, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick851, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_card1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_card1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 86, 456, 36));
    kk_style_bg(v->st_vol_card1, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_vol_card1, 8, 0);
    lv_obj_remove_flag(v->st_vol_card1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_card1, LV_OBJ_FLAG_SCROLLABLE);
    build_st_vol_name1(v, v->st_vol_card1);
    build_st_vol_fs1(v, v->st_vol_card1);
    build_st_vol_use1(v, v->st_vol_card1);
    build_st_vol_pct1(v, v->st_vol_card1);
    build_st_vol_bar1(v, v->st_vol_card1);
    build_st_vol_tick601(v, v->st_vol_card1);
    build_st_vol_tick851(v, v->st_vol_card1);
}

static void __attribute__((noinline)) build_st_vol_name2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_name2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 90, 23));
    lv_obj_remove_flag(v->st_vol_name2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_name2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_name2, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_vol_name2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_name2, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_name2, "");
    kk_label_vcenter(v->st_vol_name2, 23);
}

static void __attribute__((noinline)) build_st_vol_fs2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_fs2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 106, 6, 64, 17));
    lv_obj_remove_flag(v->st_vol_fs2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_fs2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_fs2, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_vol_fs2, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_vol_fs2, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_fs2, "");
    kk_label_vcenter(v->st_vol_fs2, 17);
}

static void __attribute__((noinline)) build_st_vol_use2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_use2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 174, 6, 180, 17));
    lv_obj_remove_flag(v->st_vol_use2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_use2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_use2, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_vol_use2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_use2, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_use2, "");
    kk_label_vcenter(v->st_vol_use2, 17);
}

static void __attribute__((noinline)) build_st_vol_pct2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_pct2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 364, 3, 80, 23));
    lv_obj_remove_flag(v->st_vol_pct2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_pct2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_pct2, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_vol_pct2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_pct2, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_vol_pct2, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_vol_pct2, "");
    kk_label_vcenter(v->st_vol_pct2, 23);
}

static void __attribute__((noinline)) build_st_vol_bar2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_vol_bar2, parent, kk_rect(0, 0, 0, 0, 12, 27, 432, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_st_vol_tick602(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick602 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 271, 18, 1, 18));
    kk_style_bg(v->st_vol_tick602, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick602, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick602, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_tick852(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick852 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 379, 18, 1, 18));
    kk_style_bg(v->st_vol_tick852, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick852, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick852, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_card2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_card2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 124, 456, 36));
    kk_style_bg(v->st_vol_card2, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_vol_card2, 8, 0);
    lv_obj_remove_flag(v->st_vol_card2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_card2, LV_OBJ_FLAG_SCROLLABLE);
    build_st_vol_name2(v, v->st_vol_card2);
    build_st_vol_fs2(v, v->st_vol_card2);
    build_st_vol_use2(v, v->st_vol_card2);
    build_st_vol_pct2(v, v->st_vol_card2);
    build_st_vol_bar2(v, v->st_vol_card2);
    build_st_vol_tick602(v, v->st_vol_card2);
    build_st_vol_tick852(v, v->st_vol_card2);
}

static void __attribute__((noinline)) build_st_vol_name3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_name3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 90, 23));
    lv_obj_remove_flag(v->st_vol_name3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_name3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_name3, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_vol_name3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_name3, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_name3, "");
    kk_label_vcenter(v->st_vol_name3, 23);
}

static void __attribute__((noinline)) build_st_vol_fs3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_fs3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 106, 6, 64, 17));
    lv_obj_remove_flag(v->st_vol_fs3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_fs3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_fs3, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_vol_fs3, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_vol_fs3, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_fs3, "");
    kk_label_vcenter(v->st_vol_fs3, 17);
}

static void __attribute__((noinline)) build_st_vol_use3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_use3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 174, 6, 180, 17));
    lv_obj_remove_flag(v->st_vol_use3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_use3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_use3, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_vol_use3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_use3, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_use3, "");
    kk_label_vcenter(v->st_vol_use3, 17);
}

static void __attribute__((noinline)) build_st_vol_pct3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_pct3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 364, 3, 80, 23));
    lv_obj_remove_flag(v->st_vol_pct3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_pct3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_pct3, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_vol_pct3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_pct3, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_vol_pct3, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_vol_pct3, "");
    kk_label_vcenter(v->st_vol_pct3, 23);
}

static void __attribute__((noinline)) build_st_vol_bar3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_vol_bar3, parent, kk_rect(0, 0, 0, 0, 12, 27, 432, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_st_vol_tick603(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick603 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 271, 18, 1, 18));
    kk_style_bg(v->st_vol_tick603, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick603, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick603, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_tick853(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick853 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 379, 18, 1, 18));
    kk_style_bg(v->st_vol_tick853, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick853, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick853, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_card3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_card3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 162, 456, 36));
    kk_style_bg(v->st_vol_card3, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_vol_card3, 8, 0);
    lv_obj_remove_flag(v->st_vol_card3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_card3, LV_OBJ_FLAG_SCROLLABLE);
    build_st_vol_name3(v, v->st_vol_card3);
    build_st_vol_fs3(v, v->st_vol_card3);
    build_st_vol_use3(v, v->st_vol_card3);
    build_st_vol_pct3(v, v->st_vol_card3);
    build_st_vol_bar3(v, v->st_vol_card3);
    build_st_vol_tick603(v, v->st_vol_card3);
    build_st_vol_tick853(v, v->st_vol_card3);
}

static void __attribute__((noinline)) build_st_vol_name4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_name4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 90, 23));
    lv_obj_remove_flag(v->st_vol_name4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_name4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_name4, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_vol_name4, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_name4, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_name4, "");
    kk_label_vcenter(v->st_vol_name4, 23);
}

static void __attribute__((noinline)) build_st_vol_fs4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_fs4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 106, 6, 64, 17));
    lv_obj_remove_flag(v->st_vol_fs4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_fs4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_fs4, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_vol_fs4, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_vol_fs4, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_fs4, "");
    kk_label_vcenter(v->st_vol_fs4, 17);
}

static void __attribute__((noinline)) build_st_vol_use4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_use4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 174, 6, 180, 17));
    lv_obj_remove_flag(v->st_vol_use4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_use4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_use4, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_vol_use4, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_use4, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_use4, "");
    kk_label_vcenter(v->st_vol_use4, 17);
}

static void __attribute__((noinline)) build_st_vol_pct4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_pct4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 364, 3, 80, 23));
    lv_obj_remove_flag(v->st_vol_pct4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_pct4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_pct4, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_vol_pct4, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_pct4, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_vol_pct4, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_vol_pct4, "");
    kk_label_vcenter(v->st_vol_pct4, 23);
}

static void __attribute__((noinline)) build_st_vol_bar4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_vol_bar4, parent, kk_rect(0, 0, 0, 0, 12, 27, 432, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_st_vol_tick604(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick604 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 271, 18, 1, 18));
    kk_style_bg(v->st_vol_tick604, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick604, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick604, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_tick854(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick854 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 379, 18, 1, 18));
    kk_style_bg(v->st_vol_tick854, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick854, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick854, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_card4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_card4 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 200, 456, 36));
    kk_style_bg(v->st_vol_card4, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_vol_card4, 8, 0);
    lv_obj_remove_flag(v->st_vol_card4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_card4, LV_OBJ_FLAG_SCROLLABLE);
    build_st_vol_name4(v, v->st_vol_card4);
    build_st_vol_fs4(v, v->st_vol_card4);
    build_st_vol_use4(v, v->st_vol_card4);
    build_st_vol_pct4(v, v->st_vol_card4);
    build_st_vol_bar4(v, v->st_vol_card4);
    build_st_vol_tick604(v, v->st_vol_card4);
    build_st_vol_tick854(v, v->st_vol_card4);
}

static void __attribute__((noinline)) build_st_vol_name5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_name5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 3, 90, 23));
    lv_obj_remove_flag(v->st_vol_name5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_name5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_name5, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_vol_name5, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_name5, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_name5, "");
    kk_label_vcenter(v->st_vol_name5, 23);
}

static void __attribute__((noinline)) build_st_vol_fs5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_fs5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 106, 6, 64, 17));
    lv_obj_remove_flag(v->st_vol_fs5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_fs5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_fs5, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_vol_fs5, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_vol_fs5, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_fs5, "");
    kk_label_vcenter(v->st_vol_fs5, 17);
}

static void __attribute__((noinline)) build_st_vol_use5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_use5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 174, 6, 180, 17));
    lv_obj_remove_flag(v->st_vol_use5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_use5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_use5, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_vol_use5, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_use5, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_vol_use5, "");
    kk_label_vcenter(v->st_vol_use5, 17);
}

static void __attribute__((noinline)) build_st_vol_pct5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_pct5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 364, 3, 80, 23));
    lv_obj_remove_flag(v->st_vol_pct5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_pct5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_vol_pct5, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_vol_pct5, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_vol_pct5, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_vol_pct5, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_vol_pct5, "");
    kk_label_vcenter(v->st_vol_pct5, 23);
}

static void __attribute__((noinline)) build_st_vol_bar5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_vol_bar5, parent, kk_rect(0, 0, 0, 0, 12, 27, 432, 6), kk_c(0x2FBF71), 3);
}

static void __attribute__((noinline)) build_st_vol_tick605(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick605 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 271, 18, 1, 18));
    kk_style_bg(v->st_vol_tick605, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick605, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick605, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_tick855(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_tick855 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 379, 18, 1, 18));
    kk_style_bg(v->st_vol_tick855, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->st_vol_tick855, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_tick855, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_vol_card5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_vol_card5 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 238, 456, 36));
    kk_style_bg(v->st_vol_card5, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_vol_card5, 8, 0);
    lv_obj_remove_flag(v->st_vol_card5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_vol_card5, LV_OBJ_FLAG_SCROLLABLE);
    build_st_vol_name5(v, v->st_vol_card5);
    build_st_vol_fs5(v, v->st_vol_card5);
    build_st_vol_use5(v, v->st_vol_card5);
    build_st_vol_pct5(v, v->st_vol_card5);
    build_st_vol_bar5(v, v->st_vol_card5);
    build_st_vol_tick605(v, v->st_vol_card5);
    build_st_vol_tick855(v, v->st_vol_card5);
}

static void __attribute__((noinline)) build_st_volume_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_volume_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 150, 480, 282));
    kk_style_bg(v->st_volume_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_volume_card, 16, 0);
    lv_obj_set_style_border_width(v->st_volume_card, 1, 0);
    lv_obj_set_style_border_color(v->st_volume_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->st_volume_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->st_volume_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_volume_card, LV_OBJ_FLAG_SCROLLABLE);
    build_st_volume_head(v, v->st_volume_card);
    build_st_vol_card0(v, v->st_volume_card);
    build_st_vol_card1(v, v->st_volume_card);
    build_st_vol_card2(v, v->st_volume_card);
    build_st_vol_card3(v, v->st_volume_card);
    build_st_vol_card4(v, v->st_volume_card);
    build_st_vol_card5(v, v->st_volume_card);
}

static void __attribute__((noinline)) build_st_raid_accent(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_accent = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 16, 3, 103));
    kk_style_bg(v->st_raid_accent, kk_c(0xC8E63C), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_raid_accent, 2, 0);
    lv_obj_remove_flag(v->st_raid_accent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_accent, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_st_raid_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 448, 32));
    lv_obj_remove_flag(v->st_raid_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->st_raid_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_raid_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_raid_title, FNOS_STR_STORAGE_RAID_TITLE);
    kk_label_vcenter(v->st_raid_title, 32);
}

static void __attribute__((noinline)) build_st_raid_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 468, 34));
    kk_style_bg(v->st_raid_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_raid_head, 8, 0);
    lv_obj_remove_flag(v->st_raid_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_head, LV_OBJ_FLAG_SCROLLABLE);
    build_st_raid_title(v, v->st_raid_head);
}

static void __attribute__((noinline)) build_st_raid_name0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_name0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 7, 50, 23));
    lv_obj_remove_flag(v->st_raid_name0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_name0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_name0, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_raid_name0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_raid_name0, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_raid_name0, "");
    kk_label_vcenter(v->st_raid_name0, 23);
}

static void __attribute__((noinline)) build_st_raid_lvl0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_lvl0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 64, 10, 44, 17));
    lv_obj_remove_flag(v->st_raid_lvl0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_lvl0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_lvl0, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_raid_lvl0, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_raid_lvl0, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_raid_lvl0, "");
    kk_label_vcenter(v->st_raid_lvl0, 17);
}

static void __attribute__((noinline)) build_st_raid_state0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_state0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 115, 10, 105, 17));
    lv_obj_remove_flag(v->st_raid_state0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_state0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_state0, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_raid_state0, kk_c(0x2FBF71), 0);
    lv_obj_set_style_text_opa(v->st_raid_state0, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_raid_state0, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_raid_state0, "");
    kk_label_vcenter(v->st_raid_state0, 17);
}

static void __attribute__((noinline)) build_st_raid_bar0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_raid_bar0, parent, kk_rect(0, 0, 0, 0, 160, 30, 60, 4), kk_c(0x2F80ED), 2);
}

static void __attribute__((noinline)) build_st_raid_row0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_row0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 46, 230, 36));
    kk_style_bg(v->st_raid_row0, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_raid_row0, 8, 0);
    lv_obj_remove_flag(v->st_raid_row0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_row0, LV_OBJ_FLAG_SCROLLABLE);
    build_st_raid_name0(v, v->st_raid_row0);
    build_st_raid_lvl0(v, v->st_raid_row0);
    build_st_raid_state0(v, v->st_raid_row0);
    build_st_raid_bar0(v, v->st_raid_row0);
}

static void __attribute__((noinline)) build_st_raid_name1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_name1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 7, 50, 23));
    lv_obj_remove_flag(v->st_raid_name1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_name1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_name1, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_raid_name1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_raid_name1, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_raid_name1, "");
    kk_label_vcenter(v->st_raid_name1, 23);
}

static void __attribute__((noinline)) build_st_raid_lvl1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_lvl1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 64, 10, 44, 17));
    lv_obj_remove_flag(v->st_raid_lvl1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_lvl1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_lvl1, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_raid_lvl1, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_raid_lvl1, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_raid_lvl1, "");
    kk_label_vcenter(v->st_raid_lvl1, 17);
}

static void __attribute__((noinline)) build_st_raid_state1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_state1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 115, 10, 105, 17));
    lv_obj_remove_flag(v->st_raid_state1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_state1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_state1, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_raid_state1, kk_c(0x2FBF71), 0);
    lv_obj_set_style_text_opa(v->st_raid_state1, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_raid_state1, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_raid_state1, "");
    kk_label_vcenter(v->st_raid_state1, 17);
}

static void __attribute__((noinline)) build_st_raid_bar1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_raid_bar1, parent, kk_rect(0, 0, 0, 0, 160, 30, 60, 4), kk_c(0x2F80ED), 2);
}

static void __attribute__((noinline)) build_st_raid_row1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_row1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 250, 46, 230, 36));
    kk_style_bg(v->st_raid_row1, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_raid_row1, 8, 0);
    lv_obj_remove_flag(v->st_raid_row1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_row1, LV_OBJ_FLAG_SCROLLABLE);
    build_st_raid_name1(v, v->st_raid_row1);
    build_st_raid_lvl1(v, v->st_raid_row1);
    build_st_raid_state1(v, v->st_raid_row1);
    build_st_raid_bar1(v, v->st_raid_row1);
}

static void __attribute__((noinline)) build_st_raid_name2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_name2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 7, 50, 23));
    lv_obj_remove_flag(v->st_raid_name2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_name2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_name2, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_raid_name2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_raid_name2, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_raid_name2, "");
    kk_label_vcenter(v->st_raid_name2, 23);
}

static void __attribute__((noinline)) build_st_raid_lvl2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_lvl2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 64, 10, 44, 17));
    lv_obj_remove_flag(v->st_raid_lvl2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_lvl2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_lvl2, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_raid_lvl2, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_raid_lvl2, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_raid_lvl2, "");
    kk_label_vcenter(v->st_raid_lvl2, 17);
}

static void __attribute__((noinline)) build_st_raid_state2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_state2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 115, 10, 105, 17));
    lv_obj_remove_flag(v->st_raid_state2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_state2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_state2, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_raid_state2, kk_c(0x2FBF71), 0);
    lv_obj_set_style_text_opa(v->st_raid_state2, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_raid_state2, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_raid_state2, "");
    kk_label_vcenter(v->st_raid_state2, 17);
}

static void __attribute__((noinline)) build_st_raid_bar2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_raid_bar2, parent, kk_rect(0, 0, 0, 0, 160, 30, 60, 4), kk_c(0x2F80ED), 2);
}

static void __attribute__((noinline)) build_st_raid_row2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_row2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 90, 230, 36));
    kk_style_bg(v->st_raid_row2, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_raid_row2, 8, 0);
    lv_obj_remove_flag(v->st_raid_row2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_row2, LV_OBJ_FLAG_SCROLLABLE);
    build_st_raid_name2(v, v->st_raid_row2);
    build_st_raid_lvl2(v, v->st_raid_row2);
    build_st_raid_state2(v, v->st_raid_row2);
    build_st_raid_bar2(v, v->st_raid_row2);
}

static void __attribute__((noinline)) build_st_raid_name3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_name3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 7, 50, 23));
    lv_obj_remove_flag(v->st_raid_name3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_name3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_name3, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_raid_name3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_raid_name3, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_raid_name3, "");
    kk_label_vcenter(v->st_raid_name3, 23);
}

static void __attribute__((noinline)) build_st_raid_lvl3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_lvl3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 64, 10, 44, 17));
    lv_obj_remove_flag(v->st_raid_lvl3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_lvl3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_lvl3, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->st_raid_lvl3, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->st_raid_lvl3, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_raid_lvl3, "");
    kk_label_vcenter(v->st_raid_lvl3, 17);
}

static void __attribute__((noinline)) build_st_raid_state3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_state3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 115, 10, 105, 17));
    lv_obj_remove_flag(v->st_raid_state3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_state3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_raid_state3, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->st_raid_state3, kk_c(0x2FBF71), 0);
    lv_obj_set_style_text_opa(v->st_raid_state3, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_raid_state3, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_raid_state3, "");
    kk_label_vcenter(v->st_raid_state3, 17);
}

static void __attribute__((noinline)) build_st_raid_bar3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->st_raid_bar3, parent, kk_rect(0, 0, 0, 0, 160, 30, 60, 4), kk_c(0x2F80ED), 2);
}

static void __attribute__((noinline)) build_st_raid_row3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_row3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 250, 90, 230, 36));
    kk_style_bg(v->st_raid_row3, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_raid_row3, 8, 0);
    lv_obj_remove_flag(v->st_raid_row3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_row3, LV_OBJ_FLAG_SCROLLABLE);
    build_st_raid_name3(v, v->st_raid_row3);
    build_st_raid_lvl3(v, v->st_raid_row3);
    build_st_raid_state3(v, v->st_raid_row3);
    build_st_raid_bar3(v, v->st_raid_row3);
}

static void __attribute__((noinline)) build_st_raid_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_raid_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 492, 150, 492, 135));
    kk_style_bg(v->st_raid_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_raid_card, 16, 0);
    lv_obj_set_style_border_width(v->st_raid_card, 1, 0);
    lv_obj_set_style_border_color(v->st_raid_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->st_raid_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->st_raid_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_raid_card, LV_OBJ_FLAG_SCROLLABLE);
    build_st_raid_accent(v, v->st_raid_card);
    build_st_raid_head(v, v->st_raid_card);
    build_st_raid_row0(v, v->st_raid_card);
    build_st_raid_row1(v, v->st_raid_card);
    build_st_raid_row2(v, v->st_raid_card);
    build_st_raid_row3(v, v->st_raid_card);
}

static void __attribute__((noinline)) build_st_io_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 1, 448, 32));
    lv_obj_remove_flag(v->st_io_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_io_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->st_io_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_io_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_io_title, FNOS_STR_STORAGE_IO_TITLE);
    kk_label_vcenter(v->st_io_title, 32);
}

static void __attribute__((noinline)) build_st_io_head(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_head = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 10, 468, 34));
    kk_style_bg(v->st_io_head, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_io_head, 8, 0);
    lv_obj_remove_flag(v->st_io_head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_head, LV_OBJ_FLAG_SCROLLABLE);
    build_st_io_title(v, v->st_io_head);
}

static void __attribute__((noinline)) build_st_io_name0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_name0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 7, 80, 23));
    lv_obj_remove_flag(v->st_io_name0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_name0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_io_name0, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_io_name0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_io_name0, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_io_name0, "");
    kk_label_vcenter(v->st_io_name0, 23);
}

static void __attribute__((noinline)) build_st_io_val0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_val0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 110, 7, 110, 23));
    lv_obj_remove_flag(v->st_io_val0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_val0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_io_val0, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_io_val0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_io_val0, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_io_val0, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_io_val0, "");
    kk_label_vcenter(v->st_io_val0, 23);
}

static void __attribute__((noinline)) build_st_io_row0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_row0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 46, 230, 36));
    kk_style_bg(v->st_io_row0, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_io_row0, 8, 0);
    lv_obj_remove_flag(v->st_io_row0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_row0, LV_OBJ_FLAG_SCROLLABLE);
    build_st_io_name0(v, v->st_io_row0);
    build_st_io_val0(v, v->st_io_row0);
}

static void __attribute__((noinline)) build_st_io_name1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_name1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 7, 80, 23));
    lv_obj_remove_flag(v->st_io_name1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_name1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_io_name1, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_io_name1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_io_name1, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_io_name1, "");
    kk_label_vcenter(v->st_io_name1, 23);
}

static void __attribute__((noinline)) build_st_io_val1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_val1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 110, 7, 110, 23));
    lv_obj_remove_flag(v->st_io_val1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_val1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_io_val1, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_io_val1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_io_val1, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_io_val1, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_io_val1, "");
    kk_label_vcenter(v->st_io_val1, 23);
}

static void __attribute__((noinline)) build_st_io_row1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_row1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 250, 46, 230, 36));
    kk_style_bg(v->st_io_row1, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_io_row1, 8, 0);
    lv_obj_remove_flag(v->st_io_row1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_row1, LV_OBJ_FLAG_SCROLLABLE);
    build_st_io_name1(v, v->st_io_row1);
    build_st_io_val1(v, v->st_io_row1);
}

static void __attribute__((noinline)) build_st_io_name2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_name2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 7, 80, 23));
    lv_obj_remove_flag(v->st_io_name2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_name2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_io_name2, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_io_name2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_io_name2, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_io_name2, "");
    kk_label_vcenter(v->st_io_name2, 23);
}

static void __attribute__((noinline)) build_st_io_val2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_val2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 110, 7, 110, 23));
    lv_obj_remove_flag(v->st_io_val2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_val2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_io_val2, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_io_val2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_io_val2, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_io_val2, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_io_val2, "");
    kk_label_vcenter(v->st_io_val2, 23);
}

static void __attribute__((noinline)) build_st_io_row2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_row2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 90, 230, 36));
    kk_style_bg(v->st_io_row2, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_io_row2, 8, 0);
    lv_obj_remove_flag(v->st_io_row2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_row2, LV_OBJ_FLAG_SCROLLABLE);
    build_st_io_name2(v, v->st_io_row2);
    build_st_io_val2(v, v->st_io_row2);
}

static void __attribute__((noinline)) build_st_io_name3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_name3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 10, 7, 80, 23));
    lv_obj_remove_flag(v->st_io_name3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_name3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_io_name3, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->st_io_name3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_io_name3, kk_opa(0xFF), 0);
    lv_label_set_text(v->st_io_name3, "");
    kk_label_vcenter(v->st_io_name3, 23);
}

static void __attribute__((noinline)) build_st_io_val3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_val3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 110, 7, 110, 23));
    lv_obj_remove_flag(v->st_io_val3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_val3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->st_io_val3, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->st_io_val3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->st_io_val3, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->st_io_val3, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->st_io_val3, "");
    kk_label_vcenter(v->st_io_val3, 23);
}

static void __attribute__((noinline)) build_st_io_row3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_row3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 250, 90, 230, 36));
    kk_style_bg(v->st_io_row3, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_io_row3, 8, 0);
    lv_obj_remove_flag(v->st_io_row3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_row3, LV_OBJ_FLAG_SCROLLABLE);
    build_st_io_name3(v, v->st_io_row3);
    build_st_io_val3(v, v->st_io_row3);
}

static void __attribute__((noinline)) build_st_io_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->st_io_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 492, 297, 492, 135));
    kk_style_bg(v->st_io_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->st_io_card, 16, 0);
    lv_obj_set_style_border_width(v->st_io_card, 1, 0);
    lv_obj_set_style_border_color(v->st_io_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->st_io_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->st_io_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->st_io_card, LV_OBJ_FLAG_SCROLLABLE);
    build_st_io_head(v, v->st_io_card);
    build_st_io_row0(v, v->st_io_card);
    build_st_io_row1(v, v->st_io_card);
    build_st_io_row2(v, v->st_io_card);
    build_st_io_row3(v, v->st_io_card);
}

static void __attribute__((noinline)) build_p1_storage(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->p1_storage = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 0, 984, 432));
    kk_style_bg(v->p1_storage, kk_c(0x080B10), kk_opa(0xFF));
    lv_obj_add_flag(v->p1_storage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->p1_storage, LV_OBJ_FLAG_SCROLLABLE);
    build_st_hero_strip(v, v->p1_storage);
    build_st_volume_card(v, v->p1_storage);
    build_st_raid_card(v, v->p1_storage);
    build_st_io_card(v, v->p1_storage);
}

static void __attribute__((noinline)) build_nw_down_accent(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_down_accent = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 16, 3, 132));
    kk_style_bg(v->nw_down_accent, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_down_accent, 2, 0);
    lv_obj_remove_flag(v->nw_down_accent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_down_accent, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_nw_down_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_down_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 16, 240, 32));
    lv_obj_remove_flag(v->nw_down_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_down_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_down_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->nw_down_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_down_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_down_title, FNOS_STR_NETWORK_DOWN_TITLE);
    kk_label_vcenter(v->nw_down_title, 32);
}

static void __attribute__((noinline)) build_nw_down_val(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_down_val = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 52, 196, 58));
    lv_obj_remove_flag(v->nw_down_val, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_down_val, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_down_val, &ui_font_num_44, 0);
    lv_obj_set_style_text_color(v->nw_down_val, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_down_val, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_down_val, "");
    kk_label_vcenter(v->nw_down_val, 58);
}

static void __attribute__((noinline)) build_nw_down_unit(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_down_unit = kk_label_create(parent, kk_rect(0, 0, 0, 0, 224, 90, 72, 20));
    lv_obj_remove_flag(v->nw_down_unit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_down_unit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_down_unit, &ui_font_txt_15, 0);
    lv_obj_set_style_text_color(v->nw_down_unit, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_down_unit, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->nw_down_unit, 1, 0);
    lv_label_set_text(v->nw_down_unit, "");
    kk_label_vcenter(v->nw_down_unit, 20);
}

static void __attribute__((noinline)) build_nw_down_sub(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_down_sub = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 118, 260, 17));
    lv_obj_remove_flag(v->nw_down_sub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_down_sub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_down_sub, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->nw_down_sub, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->nw_down_sub, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_down_sub, "");
    kk_label_vcenter(v->nw_down_sub, 17);
}

static void __attribute__((noinline)) build_nw_mini_down(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_trend_create(&v->nw_mini_down, parent, kk_rect(0, 0, 0, 0, 300, 44, 164, 96), kk_c(0x4EC9E8), 90, 12, 2, 0);
}

static void __attribute__((noinline)) build_nw_down_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_down_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 0, 486, 164));
    kk_style_bg(v->nw_down_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_down_card, 16, 0);
    lv_obj_set_style_border_width(v->nw_down_card, 1, 0);
    lv_obj_set_style_border_color(v->nw_down_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->nw_down_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->nw_down_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_down_card, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_down_accent(v, v->nw_down_card);
    build_nw_down_title(v, v->nw_down_card);
    build_nw_down_val(v, v->nw_down_card);
    build_nw_down_unit(v, v->nw_down_card);
    build_nw_down_sub(v, v->nw_down_card);
    build_nw_mini_down(v, v->nw_down_card);
}

static void __attribute__((noinline)) build_nw_up_accent(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_up_accent = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 16, 3, 132));
    kk_style_bg(v->nw_up_accent, kk_c(0x2F80ED), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_up_accent, 2, 0);
    lv_obj_remove_flag(v->nw_up_accent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_up_accent, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_nw_up_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_up_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 16, 240, 32));
    lv_obj_remove_flag(v->nw_up_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_up_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_up_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->nw_up_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_up_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_up_title, FNOS_STR_NETWORK_UP_TITLE);
    kk_label_vcenter(v->nw_up_title, 32);
}

static void __attribute__((noinline)) build_nw_up_val(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_up_val = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 52, 196, 58));
    lv_obj_remove_flag(v->nw_up_val, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_up_val, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_up_val, &ui_font_num_44, 0);
    lv_obj_set_style_text_color(v->nw_up_val, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_up_val, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_up_val, "");
    kk_label_vcenter(v->nw_up_val, 58);
}

static void __attribute__((noinline)) build_nw_up_unit(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_up_unit = kk_label_create(parent, kk_rect(0, 0, 0, 0, 224, 90, 72, 20));
    lv_obj_remove_flag(v->nw_up_unit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_up_unit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_up_unit, &ui_font_txt_15, 0);
    lv_obj_set_style_text_color(v->nw_up_unit, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_up_unit, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->nw_up_unit, 1, 0);
    lv_label_set_text(v->nw_up_unit, "");
    kk_label_vcenter(v->nw_up_unit, 20);
}

static void __attribute__((noinline)) build_nw_up_sub(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_up_sub = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 118, 260, 17));
    lv_obj_remove_flag(v->nw_up_sub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_up_sub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_up_sub, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->nw_up_sub, kk_c(0x5A6B80), 0);
    lv_obj_set_style_text_opa(v->nw_up_sub, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_up_sub, "");
    kk_label_vcenter(v->nw_up_sub, 17);
}

static void __attribute__((noinline)) build_nw_mini_up(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_trend_create(&v->nw_mini_up, parent, kk_rect(0, 0, 0, 0, 300, 44, 164, 96), kk_c(0x2F80ED), 90, 12, 2, 0);
}

static void __attribute__((noinline)) build_nw_up_card(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_up_card = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 498, 0, 486, 164));
    kk_style_bg(v->nw_up_card, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_up_card, 16, 0);
    lv_obj_set_style_border_width(v->nw_up_card, 1, 0);
    lv_obj_set_style_border_color(v->nw_up_card, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->nw_up_card, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->nw_up_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_up_card, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_up_accent(v, v->nw_up_card);
    build_nw_up_title(v, v->nw_up_card);
    build_nw_up_val(v, v->nw_up_card);
    build_nw_up_unit(v, v->nw_up_card);
    build_nw_up_sub(v, v->nw_up_card);
    build_nw_mini_up(v, v->nw_up_card);
}

static void __attribute__((noinline)) build_nw_trend_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_trend_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 12, 420, 32));
    lv_obj_remove_flag(v->nw_trend_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_trend_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_trend_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->nw_trend_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_trend_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_trend_title, FNOS_STR_NETWORK_TREND_TITLE);
    kk_label_vcenter(v->nw_trend_title, 32);
}

static void __attribute__((noinline)) build_nw_down_legend_dot(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_down_legend_dot = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 798, 21, 10, 10));
    kk_style_bg(v->nw_down_legend_dot, kk_c(0x4EC9E8), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_down_legend_dot, 5, 0);
    lv_obj_remove_flag(v->nw_down_legend_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_down_legend_dot, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_nw_down_legend_lbl(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_down_legend_lbl = kk_label_create(parent, kk_rect(0, 0, 0, 0, 816, 18, 60, 20));
    lv_obj_remove_flag(v->nw_down_legend_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_down_legend_lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_down_legend_lbl, &ui_font_txt_15, 0);
    lv_obj_set_style_text_color(v->nw_down_legend_lbl, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_down_legend_lbl, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->nw_down_legend_lbl, 1, 0);
    lv_label_set_text(v->nw_down_legend_lbl, FNOS_STR_NETWORK_LEGEND_DOWN);
    kk_label_vcenter(v->nw_down_legend_lbl, 20);
}

static void __attribute__((noinline)) build_nw_up_legend_dot(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_up_legend_dot = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 902, 21, 10, 10));
    kk_style_bg(v->nw_up_legend_dot, kk_c(0x2F80ED), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_up_legend_dot, 5, 0);
    lv_obj_remove_flag(v->nw_up_legend_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_up_legend_dot, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_nw_up_legend_lbl(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_up_legend_lbl = kk_label_create(parent, kk_rect(0, 0, 0, 0, 920, 18, 44, 20));
    lv_obj_remove_flag(v->nw_up_legend_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_up_legend_lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_up_legend_lbl, &ui_font_txt_15, 0);
    lv_obj_set_style_text_color(v->nw_up_legend_lbl, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_up_legend_lbl, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->nw_up_legend_lbl, 1, 0);
    lv_label_set_text(v->nw_up_legend_lbl, FNOS_STR_NETWORK_LEGEND_UP);
    kk_label_vcenter(v->nw_up_legend_lbl, 20);
}

static void __attribute__((noinline)) build_nw_down_trend(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_trend_create(&v->nw_down_trend, parent, kk_rect(0, 0, 0, 0, 8, 8, 836, 100), kk_c(0x4EC9E8), 90, 12, 2, 0);
}

static void __attribute__((noinline)) build_nw_up_trend(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_trend_create(&v->nw_up_trend, parent, kk_rect(0, 0, 0, 0, 8, 8, 836, 100), kk_c(0x2F80ED), 90, 12, 2, 0);
}

static void __attribute__((noinline)) build_nw_trend_well(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_trend_well = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 48, 852, 116));
    kk_style_bg(v->nw_trend_well, kk_c(0x232E3F), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_trend_well, 8, 0);
    lv_obj_remove_flag(v->nw_trend_well, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_trend_well, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_down_trend(v, v->nw_trend_well);
    build_nw_up_trend(v, v->nw_trend_well);
}

static void __attribute__((noinline)) build_nw_axis0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_axis0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 880, 46, 84, 17));
    lv_obj_remove_flag(v->nw_axis0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_axis0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_axis0, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->nw_axis0, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_axis0, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_axis0, "");
    kk_label_vcenter(v->nw_axis0, 17);
}

static void __attribute__((noinline)) build_nw_axis1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_axis1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 880, 98, 84, 17));
    lv_obj_remove_flag(v->nw_axis1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_axis1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_axis1, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->nw_axis1, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_axis1, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_axis1, "");
    kk_label_vcenter(v->nw_axis1, 17);
}

static void __attribute__((noinline)) build_nw_axis2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_axis2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 880, 150, 84, 17));
    lv_obj_remove_flag(v->nw_axis2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_axis2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_axis2, &ui_font_txt_13, 0);
    lv_obj_set_style_text_color(v->nw_axis2, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_axis2, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_axis2, "");
    kk_label_vcenter(v->nw_axis2, 17);
}

static void __attribute__((noinline)) build_nw_trend_panel(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_trend_panel = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 176, 984, 176));
    kk_style_bg(v->nw_trend_panel, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_trend_panel, 16, 0);
    lv_obj_set_style_border_width(v->nw_trend_panel, 1, 0);
    lv_obj_set_style_border_color(v->nw_trend_panel, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->nw_trend_panel, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->nw_trend_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_trend_panel, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_trend_title(v, v->nw_trend_panel);
    build_nw_down_legend_dot(v, v->nw_trend_panel);
    build_nw_down_legend_lbl(v, v->nw_trend_panel);
    build_nw_up_legend_dot(v, v->nw_trend_panel);
    build_nw_up_legend_lbl(v, v->nw_trend_panel);
    build_nw_trend_well(v, v->nw_trend_panel);
    build_nw_axis0(v, v->nw_trend_panel);
    build_nw_axis1(v, v->nw_trend_panel);
    build_nw_axis2(v, v->nw_trend_panel);
}

static void __attribute__((noinline)) build_nw_ctx_lbl0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_lbl0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 10, 150, 23));
    lv_obj_remove_flag(v->nw_ctx_lbl0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_lbl0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_ctx_lbl0, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->nw_ctx_lbl0, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_ctx_lbl0, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_ctx_lbl0, FNOS_STR_NETWORK_CTX_PEAK_DOWN);
    kk_label_vcenter(v->nw_ctx_lbl0, 23);
}

static void __attribute__((noinline)) build_nw_ctx_val0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_val0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 35, 158, 23));
    lv_obj_remove_flag(v->nw_ctx_val0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_val0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_ctx_val0, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->nw_ctx_val0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_ctx_val0, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_ctx_val0, "");
    kk_label_vcenter(v->nw_ctx_val0, 23);
}

static void __attribute__((noinline)) build_nw_ctx_card0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_card0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 1, 364, 190, 68));
    kk_style_bg(v->nw_ctx_card0, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_ctx_card0, 8, 0);
    lv_obj_remove_flag(v->nw_ctx_card0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_card0, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_ctx_lbl0(v, v->nw_ctx_card0);
    build_nw_ctx_val0(v, v->nw_ctx_card0);
}

static void __attribute__((noinline)) build_nw_ctx_lbl1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_lbl1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 10, 150, 23));
    lv_obj_remove_flag(v->nw_ctx_lbl1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_lbl1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_ctx_lbl1, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->nw_ctx_lbl1, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_ctx_lbl1, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_ctx_lbl1, FNOS_STR_NETWORK_CTX_PEAK_UP);
    kk_label_vcenter(v->nw_ctx_lbl1, 23);
}

static void __attribute__((noinline)) build_nw_ctx_val1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_val1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 35, 158, 23));
    lv_obj_remove_flag(v->nw_ctx_val1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_val1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_ctx_val1, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->nw_ctx_val1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_ctx_val1, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_ctx_val1, "");
    kk_label_vcenter(v->nw_ctx_val1, 23);
}

static void __attribute__((noinline)) build_nw_ctx_card1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_card1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 199, 364, 190, 68));
    kk_style_bg(v->nw_ctx_card1, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_ctx_card1, 8, 0);
    lv_obj_remove_flag(v->nw_ctx_card1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_card1, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_ctx_lbl1(v, v->nw_ctx_card1);
    build_nw_ctx_val1(v, v->nw_ctx_card1);
}

static void __attribute__((noinline)) build_nw_ctx_lbl2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_lbl2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 10, 150, 23));
    lv_obj_remove_flag(v->nw_ctx_lbl2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_lbl2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_ctx_lbl2, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->nw_ctx_lbl2, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_ctx_lbl2, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_ctx_lbl2, FNOS_STR_NETWORK_CTX_LATENCY);
    kk_label_vcenter(v->nw_ctx_lbl2, 23);
}

static void __attribute__((noinline)) build_nw_ctx_val2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_val2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 35, 158, 23));
    lv_obj_remove_flag(v->nw_ctx_val2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_val2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_ctx_val2, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->nw_ctx_val2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_ctx_val2, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_ctx_val2, "");
    kk_label_vcenter(v->nw_ctx_val2, 23);
}

static void __attribute__((noinline)) build_nw_ctx_card2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_card2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 397, 364, 190, 68));
    kk_style_bg(v->nw_ctx_card2, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_ctx_card2, 8, 0);
    lv_obj_remove_flag(v->nw_ctx_card2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_card2, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_ctx_lbl2(v, v->nw_ctx_card2);
    build_nw_ctx_val2(v, v->nw_ctx_card2);
}

static void __attribute__((noinline)) build_nw_ctx_lbl3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_lbl3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 10, 150, 23));
    lv_obj_remove_flag(v->nw_ctx_lbl3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_lbl3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_ctx_lbl3, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->nw_ctx_lbl3, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_ctx_lbl3, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_ctx_lbl3, FNOS_STR_NETWORK_CTX_SAMPLING);
    kk_label_vcenter(v->nw_ctx_lbl3, 23);
}

static void __attribute__((noinline)) build_nw_ctx_val3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_val3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 35, 158, 23));
    lv_obj_remove_flag(v->nw_ctx_val3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_val3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_ctx_val3, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->nw_ctx_val3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_ctx_val3, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_ctx_val3, "");
    kk_label_vcenter(v->nw_ctx_val3, 23);
}

static void __attribute__((noinline)) build_nw_ctx_card3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_card3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 595, 364, 190, 68));
    kk_style_bg(v->nw_ctx_card3, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_ctx_card3, 8, 0);
    lv_obj_remove_flag(v->nw_ctx_card3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_card3, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_ctx_lbl3(v, v->nw_ctx_card3);
    build_nw_ctx_val3(v, v->nw_ctx_card3);
}

static void __attribute__((noinline)) build_nw_ctx_lbl4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_lbl4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 10, 150, 23));
    lv_obj_remove_flag(v->nw_ctx_lbl4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_lbl4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_ctx_lbl4, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->nw_ctx_lbl4, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->nw_ctx_lbl4, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_ctx_lbl4, FNOS_STR_NETWORK_CTX_TOTAL);
    kk_label_vcenter(v->nw_ctx_lbl4, 23);
}

static void __attribute__((noinline)) build_nw_total_val(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_total_val = kk_label_create(parent, kk_rect(0, 0, 0, 0, 16, 35, 158, 23));
    lv_obj_remove_flag(v->nw_total_val, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_total_val, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->nw_total_val, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->nw_total_val, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->nw_total_val, kk_opa(0xFF), 0);
    lv_label_set_text(v->nw_total_val, "");
    kk_label_vcenter(v->nw_total_val, 23);
}

static void __attribute__((noinline)) build_nw_ctx_card4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->nw_ctx_card4 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 793, 364, 190, 68));
    kk_style_bg(v->nw_ctx_card4, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->nw_ctx_card4, 8, 0);
    lv_obj_remove_flag(v->nw_ctx_card4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->nw_ctx_card4, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_ctx_lbl4(v, v->nw_ctx_card4);
    build_nw_total_val(v, v->nw_ctx_card4);
}

static void __attribute__((noinline)) build_p2_network(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->p2_network = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 0, 984, 432));
    kk_style_bg(v->p2_network, kk_c(0x080B10), kk_opa(0xFF));
    lv_obj_add_flag(v->p2_network, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->p2_network, LV_OBJ_FLAG_SCROLLABLE);
    build_nw_down_card(v, v->p2_network);
    build_nw_up_card(v, v->p2_network);
    build_nw_trend_panel(v, v->p2_network);
    build_nw_ctx_card0(v, v->p2_network);
    build_nw_ctx_card1(v, v->p2_network);
    build_nw_ctx_card2(v, v->p2_network);
    build_nw_ctx_card3(v, v->p2_network);
    build_nw_ctx_card4(v, v->p2_network);
}

static void __attribute__((noinline)) build_sy_container_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_container_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 12, 150, 32));
    lv_obj_remove_flag(v->sy_container_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_container_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_container_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->sy_container_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_container_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_container_title, FNOS_STR_SYSTEM_CONTAINER_TITLE);
    kk_label_vcenter(v->sy_container_title, 32);
}

static void __attribute__((noinline)) build_sy_cnt(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_cnt = kk_label_create(parent, kk_rect(0, 0, 0, 0, 180, 16, 120, 17));
    lv_obj_remove_flag(v->sy_cnt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_cnt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_cnt, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_cnt, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_cnt, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_cnt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_cnt, "");
    kk_label_vcenter(v->sy_cnt, 17);
}

static void __attribute__((noinline)) build_sy_dot0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dot0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 10, 10));
    kk_style_bg(v->sy_dot0, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dot0, 5, 0);
    lv_obj_remove_flag(v->sy_dot0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dot0, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_dock0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 0, 238, 23));
    lv_obj_remove_flag(v->sy_dock0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock0, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_dock0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_dock0, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock0, "");
    kk_label_vcenter(v->sy_dock0, 23);
}

static void __attribute__((noinline)) build_sy_dock_state0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_state0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 19, 238, 17));
    lv_obj_remove_flag(v->sy_dock_state0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_state0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock_state0, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_dock_state0, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_dock_state0, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock_state0, "");
    kk_label_vcenter(v->sy_dock_state0, 17);
}

static void __attribute__((noinline)) build_sy_dock_row0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_row0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 44, 280, 36));
    kk_style_bg(v->sy_dock_row0, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dock_row0, 8, 0);
    lv_obj_remove_flag(v->sy_dock_row0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_row0, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_dot0(v, v->sy_dock_row0);
    build_sy_dock0(v, v->sy_dock_row0);
    build_sy_dock_state0(v, v->sy_dock_row0);
}

static void __attribute__((noinline)) build_sy_dot1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dot1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 10, 10));
    kk_style_bg(v->sy_dot1, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dot1, 5, 0);
    lv_obj_remove_flag(v->sy_dot1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dot1, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_dock1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 0, 238, 23));
    lv_obj_remove_flag(v->sy_dock1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock1, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_dock1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_dock1, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock1, "");
    kk_label_vcenter(v->sy_dock1, 23);
}

static void __attribute__((noinline)) build_sy_dock_state1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_state1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 19, 238, 17));
    lv_obj_remove_flag(v->sy_dock_state1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_state1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock_state1, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_dock_state1, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_dock_state1, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock_state1, "");
    kk_label_vcenter(v->sy_dock_state1, 17);
}

static void __attribute__((noinline)) build_sy_dock_row1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_row1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 82, 280, 36));
    kk_style_bg(v->sy_dock_row1, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dock_row1, 8, 0);
    lv_obj_remove_flag(v->sy_dock_row1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_row1, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_dot1(v, v->sy_dock_row1);
    build_sy_dock1(v, v->sy_dock_row1);
    build_sy_dock_state1(v, v->sy_dock_row1);
}

static void __attribute__((noinline)) build_sy_dot2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dot2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 10, 10));
    kk_style_bg(v->sy_dot2, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dot2, 5, 0);
    lv_obj_remove_flag(v->sy_dot2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dot2, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_dock2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 0, 238, 23));
    lv_obj_remove_flag(v->sy_dock2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock2, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_dock2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_dock2, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock2, "");
    kk_label_vcenter(v->sy_dock2, 23);
}

static void __attribute__((noinline)) build_sy_dock_state2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_state2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 19, 238, 17));
    lv_obj_remove_flag(v->sy_dock_state2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_state2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock_state2, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_dock_state2, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_dock_state2, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock_state2, "");
    kk_label_vcenter(v->sy_dock_state2, 17);
}

static void __attribute__((noinline)) build_sy_dock_row2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_row2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 120, 280, 36));
    kk_style_bg(v->sy_dock_row2, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dock_row2, 8, 0);
    lv_obj_remove_flag(v->sy_dock_row2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_row2, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_dot2(v, v->sy_dock_row2);
    build_sy_dock2(v, v->sy_dock_row2);
    build_sy_dock_state2(v, v->sy_dock_row2);
}

static void __attribute__((noinline)) build_sy_dot3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dot3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 10, 10));
    kk_style_bg(v->sy_dot3, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dot3, 5, 0);
    lv_obj_remove_flag(v->sy_dot3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dot3, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_dock3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 0, 238, 23));
    lv_obj_remove_flag(v->sy_dock3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock3, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_dock3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_dock3, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock3, "");
    kk_label_vcenter(v->sy_dock3, 23);
}

static void __attribute__((noinline)) build_sy_dock_state3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_state3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 19, 238, 17));
    lv_obj_remove_flag(v->sy_dock_state3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_state3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock_state3, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_dock_state3, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_dock_state3, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock_state3, "");
    kk_label_vcenter(v->sy_dock_state3, 17);
}

static void __attribute__((noinline)) build_sy_dock_row3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_row3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 158, 280, 36));
    kk_style_bg(v->sy_dock_row3, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dock_row3, 8, 0);
    lv_obj_remove_flag(v->sy_dock_row3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_row3, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_dot3(v, v->sy_dock_row3);
    build_sy_dock3(v, v->sy_dock_row3);
    build_sy_dock_state3(v, v->sy_dock_row3);
}

static void __attribute__((noinline)) build_sy_dot4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dot4 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 10, 10));
    kk_style_bg(v->sy_dot4, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dot4, 5, 0);
    lv_obj_remove_flag(v->sy_dot4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dot4, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_dock4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 0, 238, 23));
    lv_obj_remove_flag(v->sy_dock4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock4, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_dock4, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_dock4, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock4, "");
    kk_label_vcenter(v->sy_dock4, 23);
}

static void __attribute__((noinline)) build_sy_dock_state4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_state4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 19, 238, 17));
    lv_obj_remove_flag(v->sy_dock_state4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_state4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock_state4, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_dock_state4, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_dock_state4, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock_state4, "");
    kk_label_vcenter(v->sy_dock_state4, 17);
}

static void __attribute__((noinline)) build_sy_dock_row4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_row4 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 196, 280, 36));
    kk_style_bg(v->sy_dock_row4, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dock_row4, 8, 0);
    lv_obj_remove_flag(v->sy_dock_row4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_row4, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_dot4(v, v->sy_dock_row4);
    build_sy_dock4(v, v->sy_dock_row4);
    build_sy_dock_state4(v, v->sy_dock_row4);
}

static void __attribute__((noinline)) build_sy_dot5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dot5 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 10, 10));
    kk_style_bg(v->sy_dot5, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dot5, 5, 0);
    lv_obj_remove_flag(v->sy_dot5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dot5, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_dock5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 0, 238, 23));
    lv_obj_remove_flag(v->sy_dock5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock5, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_dock5, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_dock5, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock5, "");
    kk_label_vcenter(v->sy_dock5, 23);
}

static void __attribute__((noinline)) build_sy_dock_state5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_state5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 19, 238, 17));
    lv_obj_remove_flag(v->sy_dock_state5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_state5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock_state5, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_dock_state5, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_dock_state5, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock_state5, "");
    kk_label_vcenter(v->sy_dock_state5, 17);
}

static void __attribute__((noinline)) build_sy_dock_row5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_row5 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 234, 280, 36));
    kk_style_bg(v->sy_dock_row5, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dock_row5, 8, 0);
    lv_obj_remove_flag(v->sy_dock_row5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_row5, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_dot5(v, v->sy_dock_row5);
    build_sy_dock5(v, v->sy_dock_row5);
    build_sy_dock_state5(v, v->sy_dock_row5);
}

static void __attribute__((noinline)) build_sy_dock_divider(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_divider = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 348, 280, 1));
    kk_style_bg(v->sy_dock_divider, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->sy_dock_divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_divider, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_alert_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 350, 80, 23));
    lv_obj_remove_flag(v->sy_alert_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_alert_title, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_alert_title, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_alert_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_alert_title, FNOS_STR_SYSTEM_ALERT_TITLE);
    kk_label_vcenter(v->sy_alert_title, 23);
}

static void __attribute__((noinline)) build_sy_alert_dot0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_dot0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 4, 8, 8));
    kk_style_bg(v->sy_alert_dot0, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_alert_dot0, 4, 0);
    lv_obj_remove_flag(v->sy_alert_dot0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_dot0, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_alert_lbl0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_lbl0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 28, 0, 240, 17));
    lv_obj_remove_flag(v->sy_alert_lbl0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_lbl0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_alert_lbl0, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_alert_lbl0, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_alert_lbl0, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_alert_lbl0, "");
    kk_label_vcenter(v->sy_alert_lbl0, 17);
}

static void __attribute__((noinline)) build_sy_alert_dot1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_dot1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 18, 8, 8));
    kk_style_bg(v->sy_alert_dot1, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_alert_dot1, 4, 0);
    lv_obj_remove_flag(v->sy_alert_dot1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_dot1, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_alert_lbl1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_lbl1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 28, 14, 240, 17));
    lv_obj_remove_flag(v->sy_alert_lbl1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_lbl1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_alert_lbl1, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_alert_lbl1, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_alert_lbl1, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_alert_lbl1, "");
    kk_label_vcenter(v->sy_alert_lbl1, 17);
}

static void __attribute__((noinline)) build_sy_alert_dot2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_dot2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 32, 8, 8));
    kk_style_bg(v->sy_alert_dot2, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_alert_dot2, 4, 0);
    lv_obj_remove_flag(v->sy_alert_dot2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_dot2, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_alert_lbl2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_lbl2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 28, 28, 240, 17));
    lv_obj_remove_flag(v->sy_alert_lbl2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_lbl2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_alert_lbl2, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_alert_lbl2, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_alert_lbl2, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_alert_lbl2, "");
    kk_label_vcenter(v->sy_alert_lbl2, 17);
}

static void __attribute__((noinline)) build_sy_alert_dot3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_dot3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 46, 8, 8));
    kk_style_bg(v->sy_alert_dot3, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_alert_dot3, 4, 0);
    lv_obj_remove_flag(v->sy_alert_dot3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_dot3, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_alert_lbl3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_lbl3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 28, 42, 240, 17));
    lv_obj_remove_flag(v->sy_alert_lbl3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_lbl3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_alert_lbl3, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_alert_lbl3, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_alert_lbl3, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_alert_lbl3, "");
    kk_label_vcenter(v->sy_alert_lbl3, 17);
}

static void __attribute__((noinline)) build_sy_alert_block(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_alert_block = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 373, 280, 59));
    kk_style_bg(v->sy_alert_block, kk_c(0x232E3F), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_alert_block, 8, 0);
    lv_obj_remove_flag(v->sy_alert_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_alert_block, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_alert_dot0(v, v->sy_alert_block);
    build_sy_alert_lbl0(v, v->sy_alert_block);
    build_sy_alert_dot1(v, v->sy_alert_block);
    build_sy_alert_lbl1(v, v->sy_alert_block);
    build_sy_alert_dot2(v, v->sy_alert_block);
    build_sy_alert_lbl2(v, v->sy_alert_block);
    build_sy_alert_dot3(v, v->sy_alert_block);
    build_sy_alert_lbl3(v, v->sy_alert_block);
}

static void __attribute__((noinline)) build_sy_dot6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dot6 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 10, 10));
    kk_style_bg(v->sy_dot6, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dot6, 5, 0);
    lv_obj_remove_flag(v->sy_dot6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dot6, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_dock6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock6 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 0, 238, 23));
    lv_obj_remove_flag(v->sy_dock6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock6, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock6, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_dock6, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_dock6, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock6, "");
    kk_label_vcenter(v->sy_dock6, 23);
}

static void __attribute__((noinline)) build_sy_dock_state6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_state6 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 19, 238, 17));
    lv_obj_remove_flag(v->sy_dock_state6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_state6, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock_state6, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_dock_state6, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_dock_state6, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock_state6, "");
    kk_label_vcenter(v->sy_dock_state6, 17);
}

static void __attribute__((noinline)) build_sy_dock_row6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_row6 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 272, 280, 36));
    kk_style_bg(v->sy_dock_row6, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dock_row6, 8, 0);
    lv_obj_remove_flag(v->sy_dock_row6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_row6, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_dot6(v, v->sy_dock_row6);
    build_sy_dock6(v, v->sy_dock_row6);
    build_sy_dock_state6(v, v->sy_dock_row6);
}

static void __attribute__((noinline)) build_sy_dot7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dot7 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 10, 10));
    kk_style_bg(v->sy_dot7, kk_c(0x5A6B80), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dot7, 5, 0);
    lv_obj_remove_flag(v->sy_dot7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dot7, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_dock7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock7 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 0, 238, 23));
    lv_obj_remove_flag(v->sy_dock7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock7, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock7, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_dock7, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_dock7, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock7, "");
    kk_label_vcenter(v->sy_dock7, 23);
}

static void __attribute__((noinline)) build_sy_dock_state7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_state7 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 30, 19, 238, 17));
    lv_obj_remove_flag(v->sy_dock_state7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_state7, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_dock_state7, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_dock_state7, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_dock_state7, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_dock_state7, "");
    kk_label_vcenter(v->sy_dock_state7, 17);
}

static void __attribute__((noinline)) build_sy_dock_row7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_dock_row7 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 310, 280, 36));
    kk_style_bg(v->sy_dock_row7, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_dock_row7, 8, 0);
    lv_obj_remove_flag(v->sy_dock_row7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_dock_row7, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_dot7(v, v->sy_dock_row7);
    build_sy_dock7(v, v->sy_dock_row7);
    build_sy_dock_state7(v, v->sy_dock_row7);
}

static void __attribute__((noinline)) build_sy_container_panel(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_container_panel = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 0, 320, 432));
    kk_style_bg(v->sy_container_panel, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_container_panel, 16, 0);
    lv_obj_set_style_border_width(v->sy_container_panel, 1, 0);
    lv_obj_set_style_border_color(v->sy_container_panel, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->sy_container_panel, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->sy_container_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_container_panel, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_container_title(v, v->sy_container_panel);
    build_sy_cnt(v, v->sy_container_panel);
    build_sy_dock_row0(v, v->sy_container_panel);
    build_sy_dock_row1(v, v->sy_container_panel);
    build_sy_dock_row2(v, v->sy_container_panel);
    build_sy_dock_row3(v, v->sy_container_panel);
    build_sy_dock_row4(v, v->sy_container_panel);
    build_sy_dock_row5(v, v->sy_container_panel);
    build_sy_dock_divider(v, v->sy_container_panel);
    build_sy_alert_title(v, v->sy_container_panel);
    build_sy_alert_block(v, v->sy_container_panel);
    build_sy_dock_row6(v, v->sy_container_panel);
    build_sy_dock_row7(v, v->sy_container_panel);
}

static void __attribute__((noinline)) build_sy_temp_accent(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_accent = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 16, 3, 400));
    kk_style_bg(v->sy_temp_accent, kk_c(0xFF7A1A), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_accent, 2, 0);
    lv_obj_remove_flag(v->sy_temp_accent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_accent, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_temp_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 12, 200, 32));
    lv_obj_remove_flag(v->sy_temp_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_temp_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->sy_temp_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_temp_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_temp_title, FNOS_STR_SYSTEM_TEMP_TITLE);
    kk_label_vcenter(v->sy_temp_title, 32);
}

static void __attribute__((noinline)) build_sy_tname0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname0, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname0, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname0, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname0, 1, 0);
    lv_label_set_text(v->sy_tname0, "");
    kk_label_vcenter(v->sy_tname0, 23);
}

static void __attribute__((noinline)) build_sy_tbar0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar0, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval0, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval0, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval0, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval0, "");
    kk_label_vcenter(v->sy_tval0, 23);
}

static void __attribute__((noinline)) build_sy_temp_row0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 44, 280, 36));
    kk_style_bg(v->sy_temp_row0, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row0, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row0, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname0(v, v->sy_temp_row0);
    build_sy_tbar0(v, v->sy_temp_row0);
    build_sy_tval0(v, v->sy_temp_row0);
}

static void __attribute__((noinline)) build_sy_tname1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname1, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname1, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname1, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname1, 1, 0);
    lv_label_set_text(v->sy_tname1, "");
    kk_label_vcenter(v->sy_tname1, 23);
}

static void __attribute__((noinline)) build_sy_tbar1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar1, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval1, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval1, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval1, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval1, "");
    kk_label_vcenter(v->sy_tval1, 23);
}

static void __attribute__((noinline)) build_sy_temp_row1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 82, 280, 36));
    kk_style_bg(v->sy_temp_row1, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row1, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row1, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname1(v, v->sy_temp_row1);
    build_sy_tbar1(v, v->sy_temp_row1);
    build_sy_tval1(v, v->sy_temp_row1);
}

static void __attribute__((noinline)) build_sy_tname2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname2, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname2, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname2, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname2, 1, 0);
    lv_label_set_text(v->sy_tname2, "");
    kk_label_vcenter(v->sy_tname2, 23);
}

static void __attribute__((noinline)) build_sy_tbar2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar2, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval2, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval2, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval2, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval2, "");
    kk_label_vcenter(v->sy_tval2, 23);
}

static void __attribute__((noinline)) build_sy_temp_row2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 120, 280, 36));
    kk_style_bg(v->sy_temp_row2, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row2, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row2, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname2(v, v->sy_temp_row2);
    build_sy_tbar2(v, v->sy_temp_row2);
    build_sy_tval2(v, v->sy_temp_row2);
}

static void __attribute__((noinline)) build_sy_tname3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname3, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname3, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname3, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname3, 1, 0);
    lv_label_set_text(v->sy_tname3, "");
    kk_label_vcenter(v->sy_tname3, 23);
}

static void __attribute__((noinline)) build_sy_tbar3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar3, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval3, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval3, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval3, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval3, "");
    kk_label_vcenter(v->sy_tval3, 23);
}

static void __attribute__((noinline)) build_sy_temp_row3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 158, 280, 36));
    kk_style_bg(v->sy_temp_row3, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row3, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row3, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname3(v, v->sy_temp_row3);
    build_sy_tbar3(v, v->sy_temp_row3);
    build_sy_tval3(v, v->sy_temp_row3);
}

static void __attribute__((noinline)) build_sy_tname4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname4, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname4, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname4, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname4, 1, 0);
    lv_label_set_text(v->sy_tname4, "");
    kk_label_vcenter(v->sy_tname4, 23);
}

static void __attribute__((noinline)) build_sy_tbar4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar4, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval4, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval4, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval4, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval4, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval4, "");
    kk_label_vcenter(v->sy_tval4, 23);
}

static void __attribute__((noinline)) build_sy_temp_row4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row4 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 196, 280, 36));
    kk_style_bg(v->sy_temp_row4, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row4, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row4, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname4(v, v->sy_temp_row4);
    build_sy_tbar4(v, v->sy_temp_row4);
    build_sy_tval4(v, v->sy_temp_row4);
}

static void __attribute__((noinline)) build_sy_tname5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname5, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname5, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname5, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname5, 1, 0);
    lv_label_set_text(v->sy_tname5, "");
    kk_label_vcenter(v->sy_tname5, 23);
}

static void __attribute__((noinline)) build_sy_tbar5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar5, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval5, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval5, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval5, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval5, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval5, "");
    kk_label_vcenter(v->sy_tval5, 23);
}

static void __attribute__((noinline)) build_sy_temp_row5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row5 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 234, 280, 36));
    kk_style_bg(v->sy_temp_row5, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row5, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row5, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname5(v, v->sy_temp_row5);
    build_sy_tbar5(v, v->sy_temp_row5);
    build_sy_tval5(v, v->sy_temp_row5);
}

static void __attribute__((noinline)) build_sy_tname6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname6 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname6, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname6, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname6, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname6, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname6, 1, 0);
    lv_label_set_text(v->sy_tname6, "");
    kk_label_vcenter(v->sy_tname6, 23);
}

static void __attribute__((noinline)) build_sy_tbar6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar6, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval6 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval6, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval6, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval6, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval6, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval6, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval6, "");
    kk_label_vcenter(v->sy_tval6, 23);
}

static void __attribute__((noinline)) build_sy_temp_row6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row6 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 272, 280, 36));
    kk_style_bg(v->sy_temp_row6, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row6, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row6, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname6(v, v->sy_temp_row6);
    build_sy_tbar6(v, v->sy_temp_row6);
    build_sy_tval6(v, v->sy_temp_row6);
}

static void __attribute__((noinline)) build_sy_tname7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname7 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname7, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname7, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname7, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname7, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname7, 1, 0);
    lv_label_set_text(v->sy_tname7, "");
    kk_label_vcenter(v->sy_tname7, 23);
}

static void __attribute__((noinline)) build_sy_tbar7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar7, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval7 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval7, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval7, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval7, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval7, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval7, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval7, "");
    kk_label_vcenter(v->sy_tval7, 23);
}

static void __attribute__((noinline)) build_sy_temp_row7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row7 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 310, 280, 36));
    kk_style_bg(v->sy_temp_row7, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row7, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row7, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname7(v, v->sy_temp_row7);
    build_sy_tbar7(v, v->sy_temp_row7);
    build_sy_tval7(v, v->sy_temp_row7);
}

static void __attribute__((noinline)) build_sy_tname8(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname8 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname8, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname8, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname8, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname8, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname8, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname8, 1, 0);
    lv_label_set_text(v->sy_tname8, "");
    kk_label_vcenter(v->sy_tname8, 23);
}

static void __attribute__((noinline)) build_sy_tbar8(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar8, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval8(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval8 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval8, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval8, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval8, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval8, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval8, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval8, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval8, "");
    kk_label_vcenter(v->sy_tval8, 23);
}

static void __attribute__((noinline)) build_sy_temp_row8(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row8 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 348, 280, 36));
    kk_style_bg(v->sy_temp_row8, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row8, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row8, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row8, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname8(v, v->sy_temp_row8);
    build_sy_tbar8(v, v->sy_temp_row8);
    build_sy_tval8(v, v->sy_temp_row8);
}

static void __attribute__((noinline)) build_sy_tname9(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tname9 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 6, 88, 23));
    lv_obj_remove_flag(v->sy_tname9, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tname9, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tname9, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_tname9, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_tname9, kk_opa(0xFF), 0);
    lv_obj_set_style_text_letter_space(v->sy_tname9, 1, 0);
    lv_label_set_text(v->sy_tname9, "");
    kk_label_vcenter(v->sy_tname9, 23);
}

static void __attribute__((noinline)) build_sy_tbar9(fnos_dash_view_t *v, lv_obj_t *parent)
{
    kk_bar_create(&v->sy_tbar9, parent, kk_rect(0, 0, 0, 0, 106, 14, 76, 8), kk_c(0xF2B01E), 4);
}

static void __attribute__((noinline)) build_sy_tval9(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_tval9 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 188, 7, 80, 23));
    lv_obj_remove_flag(v->sy_tval9, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_tval9, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_tval9, &ui_font_num_17, 0);
    lv_obj_set_style_text_color(v->sy_tval9, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_tval9, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_tval9, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_tval9, "");
    kk_label_vcenter(v->sy_tval9, 23);
}

static void __attribute__((noinline)) build_sy_temp_row9(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_row9 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 386, 280, 36));
    kk_style_bg(v->sy_temp_row9, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_row9, 8, 0);
    lv_obj_remove_flag(v->sy_temp_row9, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_row9, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_tname9(v, v->sy_temp_row9);
    build_sy_tbar9(v, v->sy_temp_row9);
    build_sy_tval9(v, v->sy_temp_row9);
}

static void __attribute__((noinline)) build_sy_ttick60(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_ttick60 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 172, 44, 1, 378));
    kk_style_bg(v->sy_ttick60, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->sy_ttick60, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_ttick60, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_ttick75(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_ttick75 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 183, 44, 1, 378));
    kk_style_bg(v->sy_ttick75, kk_c(0x334052), kk_opa(0xFF));
    lv_obj_remove_flag(v->sy_ttick75, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_ttick75, LV_OBJ_FLAG_SCROLLABLE);
}

static void __attribute__((noinline)) build_sy_temp_panel(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_temp_panel = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 332, 0, 320, 432));
    kk_style_bg(v->sy_temp_panel, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_temp_panel, 16, 0);
    lv_obj_set_style_border_width(v->sy_temp_panel, 1, 0);
    lv_obj_set_style_border_color(v->sy_temp_panel, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->sy_temp_panel, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->sy_temp_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_temp_panel, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_temp_accent(v, v->sy_temp_panel);
    build_sy_temp_title(v, v->sy_temp_panel);
    build_sy_temp_row0(v, v->sy_temp_panel);
    build_sy_temp_row1(v, v->sy_temp_panel);
    build_sy_temp_row2(v, v->sy_temp_panel);
    build_sy_temp_row3(v, v->sy_temp_panel);
    build_sy_temp_row4(v, v->sy_temp_panel);
    build_sy_temp_row5(v, v->sy_temp_panel);
    build_sy_temp_row6(v, v->sy_temp_panel);
    build_sy_temp_row7(v, v->sy_temp_panel);
    build_sy_temp_row8(v, v->sy_temp_panel);
    build_sy_temp_row9(v, v->sy_temp_panel);
    build_sy_ttick60(v, v->sy_temp_panel);
    build_sy_ttick75(v, v->sy_temp_panel);
}

static void __attribute__((noinline)) build_sy_kv_title(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_title = kk_label_create(parent, kk_rect(0, 0, 0, 0, 20, 12, 280, 32));
    lv_obj_remove_flag(v->sy_kv_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_title, &ui_font_cjk_24, 0);
    lv_obj_set_style_text_color(v->sy_kv_title, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_title, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_title, FNOS_STR_SYSTEM_KV_TITLE);
    kk_label_vcenter(v->sy_kv_title, 32);
}

static void __attribute__((noinline)) build_sy_kv_lbl0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl0, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl0, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl0, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl0, FNOS_STR_SYSTEM_KV_HOST);
    kk_label_vcenter(v->sy_kv_lbl0, 17);
}

static void __attribute__((noinline)) build_sy_kv_val0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val0 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val0, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val0, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val0, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val0, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val0, "");
    kk_label_vcenter(v->sy_kv_val0, 23);
}

static void __attribute__((noinline)) build_sy_kv_row0(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row0 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 44, 280, 32));
    kk_style_bg(v->sy_kv_row0, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row0, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row0, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row0, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl0(v, v->sy_kv_row0);
    build_sy_kv_val0(v, v->sy_kv_row0);
}

static void __attribute__((noinline)) build_sy_kv_lbl1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl1, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl1, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl1, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl1, FNOS_STR_SYSTEM_KV_ENDPOINT);
    kk_label_vcenter(v->sy_kv_lbl1, 17);
}

static void __attribute__((noinline)) build_sy_kv_val1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val1 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val1, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val1, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val1, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val1, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val1, "");
    kk_label_vcenter(v->sy_kv_val1, 23);
}

static void __attribute__((noinline)) build_sy_kv_row1(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row1 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 76, 280, 32));
    kk_style_bg(v->sy_kv_row1, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row1, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row1, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl1(v, v->sy_kv_row1);
    build_sy_kv_val1(v, v->sy_kv_row1);
}

static void __attribute__((noinline)) build_sy_kv_lbl2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl2, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl2, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl2, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl2, FNOS_STR_SYSTEM_KV_HTTP);
    kk_label_vcenter(v->sy_kv_lbl2, 17);
}

static void __attribute__((noinline)) build_sy_kv_val2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val2 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val2, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val2, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val2, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val2, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val2, "");
    kk_label_vcenter(v->sy_kv_val2, 23);
}

static void __attribute__((noinline)) build_sy_kv_row2(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row2 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 108, 280, 32));
    kk_style_bg(v->sy_kv_row2, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row2, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row2, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl2(v, v->sy_kv_row2);
    build_sy_kv_val2(v, v->sy_kv_row2);
}

static void __attribute__((noinline)) build_sy_kv_lbl3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl3, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl3, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl3, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl3, FNOS_STR_SYSTEM_KV_POLL);
    kk_label_vcenter(v->sy_kv_lbl3, 17);
}

static void __attribute__((noinline)) build_sy_kv_val3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val3 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val3, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val3, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val3, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val3, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val3, "");
    kk_label_vcenter(v->sy_kv_val3, 23);
}

static void __attribute__((noinline)) build_sy_kv_row3(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row3 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 140, 280, 32));
    kk_style_bg(v->sy_kv_row3, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row3, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row3, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row3, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl3(v, v->sy_kv_row3);
    build_sy_kv_val3(v, v->sy_kv_row3);
}

static void __attribute__((noinline)) build_sy_kv_lbl4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl4, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl4, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl4, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl4, FNOS_STR_SYSTEM_KV_LAST_ERROR);
    kk_label_vcenter(v->sy_kv_lbl4, 17);
}

static void __attribute__((noinline)) build_sy_kv_val4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val4 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val4, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val4, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val4, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val4, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val4, "");
    kk_label_vcenter(v->sy_kv_val4, 23);
}

static void __attribute__((noinline)) build_sy_kv_row4(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row4 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 172, 280, 32));
    kk_style_bg(v->sy_kv_row4, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row4, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row4, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row4, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl4(v, v->sy_kv_row4);
    build_sy_kv_val4(v, v->sy_kv_row4);
}

static void __attribute__((noinline)) build_sy_kv_lbl5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl5, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl5, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl5, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl5, FNOS_STR_SYSTEM_KV_DATA_AGE);
    kk_label_vcenter(v->sy_kv_lbl5, 17);
}

static void __attribute__((noinline)) build_sy_kv_val5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val5 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val5, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val5, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val5, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val5, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val5, "");
    kk_label_vcenter(v->sy_kv_val5, 23);
}

static void __attribute__((noinline)) build_sy_kv_row5(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row5 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 204, 280, 32));
    kk_style_bg(v->sy_kv_row5, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row5, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row5, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row5, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl5(v, v->sy_kv_row5);
    build_sy_kv_val5(v, v->sy_kv_row5);
}

static void __attribute__((noinline)) build_sy_kv_lbl6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl6 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl6, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl6, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl6, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl6, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl6, FNOS_STR_SYSTEM_KV_WIFI);
    kk_label_vcenter(v->sy_kv_lbl6, 17);
}

static void __attribute__((noinline)) build_sy_kv_val6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val6 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val6, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val6, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val6, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val6, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val6, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val6, "");
    kk_label_vcenter(v->sy_kv_val6, 23);
}

static void __attribute__((noinline)) build_sy_kv_row6(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row6 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 236, 280, 32));
    kk_style_bg(v->sy_kv_row6, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row6, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row6, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row6, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl6(v, v->sy_kv_row6);
    build_sy_kv_val6(v, v->sy_kv_row6);
}

static void __attribute__((noinline)) build_sy_kv_lbl7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl7 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl7, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl7, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl7, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl7, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl7, FNOS_STR_SYSTEM_KV_HEAP);
    kk_label_vcenter(v->sy_kv_lbl7, 17);
}

static void __attribute__((noinline)) build_sy_kv_val7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val7 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val7, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val7, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val7, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val7, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val7, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val7, "");
    kk_label_vcenter(v->sy_kv_val7, 23);
}

static void __attribute__((noinline)) build_sy_kv_row7(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row7 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 268, 280, 32));
    kk_style_bg(v->sy_kv_row7, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row7, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row7, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row7, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl7(v, v->sy_kv_row7);
    build_sy_kv_val7(v, v->sy_kv_row7);
}

static void __attribute__((noinline)) build_sy_kv_lbl8(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl8 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl8, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl8, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl8, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl8, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl8, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl8, FNOS_STR_SYSTEM_KV_PROCS);
    kk_label_vcenter(v->sy_kv_lbl8, 17);
}

static void __attribute__((noinline)) build_sy_kv_val8(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val8 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val8, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val8, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val8, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val8, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val8, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val8, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val8, "");
    kk_label_vcenter(v->sy_kv_val8, 23);
}

static void __attribute__((noinline)) build_sy_kv_row8(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row8 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 300, 280, 32));
    kk_style_bg(v->sy_kv_row8, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row8, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row8, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row8, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl8(v, v->sy_kv_row8);
    build_sy_kv_val8(v, v->sy_kv_row8);
}

static void __attribute__((noinline)) build_sy_kv_lbl9(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl9 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl9, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl9, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl9, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl9, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl9, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl9, FNOS_STR_SYSTEM_KV_SWAP);
    kk_label_vcenter(v->sy_kv_lbl9, 17);
}

static void __attribute__((noinline)) build_sy_kv_val9(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val9 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val9, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val9, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val9, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val9, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val9, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val9, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val9, "");
    kk_label_vcenter(v->sy_kv_val9, 23);
}

static void __attribute__((noinline)) build_sy_kv_row9(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row9 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 332, 280, 32));
    kk_style_bg(v->sy_kv_row9, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row9, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row9, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row9, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl9(v, v->sy_kv_row9);
    build_sy_kv_val9(v, v->sy_kv_row9);
}

static void __attribute__((noinline)) build_sy_kv_lbl10(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl10 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl10, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl10, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl10, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl10, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl10, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl10, FNOS_STR_SYSTEM_KV_MEM_TOTAL);
    kk_label_vcenter(v->sy_kv_lbl10, 17);
}

static void __attribute__((noinline)) build_sy_kv_val10(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val10 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val10, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val10, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val10, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val10, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val10, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val10, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val10, "");
    kk_label_vcenter(v->sy_kv_val10, 23);
}

static void __attribute__((noinline)) build_sy_kv_row10(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row10 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 364, 280, 32));
    kk_style_bg(v->sy_kv_row10, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row10, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row10, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row10, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl10(v, v->sy_kv_row10);
    build_sy_kv_val10(v, v->sy_kv_row10);
}

static void __attribute__((noinline)) build_sy_kv_lbl11(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_lbl11 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 12, 8, 56, 17));
    lv_obj_remove_flag(v->sy_kv_lbl11, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_lbl11, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_lbl11, &ui_font_cjk_13, 0);
    lv_obj_set_style_text_color(v->sy_kv_lbl11, kk_c(0xBFC9D7), 0);
    lv_obj_set_style_text_opa(v->sy_kv_lbl11, kk_opa(0xFF), 0);
    lv_label_set_text(v->sy_kv_lbl11, FNOS_STR_SYSTEM_KV_UPTIME);
    kk_label_vcenter(v->sy_kv_lbl11, 17);
}

static void __attribute__((noinline)) build_sy_kv_val11(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_val11 = kk_label_create(parent, kk_rect(0, 0, 0, 0, 76, 5, 192, 23));
    lv_obj_remove_flag(v->sy_kv_val11, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_val11, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(v->sy_kv_val11, &ui_font_cjk_17, 0);
    lv_obj_set_style_text_color(v->sy_kv_val11, kk_c(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(v->sy_kv_val11, kk_opa(0xFF), 0);
    lv_obj_set_style_text_align(v->sy_kv_val11, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v->sy_kv_val11, "");
    kk_label_vcenter(v->sy_kv_val11, 23);
}

static void __attribute__((noinline)) build_sy_kv_row11(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_row11 = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 396, 280, 32));
    kk_style_bg(v->sy_kv_row11, kk_c(0x1C2534), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_row11, 8, 0);
    lv_obj_remove_flag(v->sy_kv_row11, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_row11, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_lbl11(v, v->sy_kv_row11);
    build_sy_kv_val11(v, v->sy_kv_row11);
}

static void __attribute__((noinline)) build_sy_kv_panel(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->sy_kv_panel = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 664, 0, 320, 432));
    kk_style_bg(v->sy_kv_panel, kk_c(0x161E2B), kk_opa(0xFF));
    lv_obj_set_style_radius(v->sy_kv_panel, 16, 0);
    lv_obj_set_style_border_width(v->sy_kv_panel, 1, 0);
    lv_obj_set_style_border_color(v->sy_kv_panel, kk_c(0x334052), 0);
    lv_obj_set_style_border_opa(v->sy_kv_panel, kk_opa(0xFF), 0);
    lv_obj_remove_flag(v->sy_kv_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->sy_kv_panel, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_kv_title(v, v->sy_kv_panel);
    build_sy_kv_row0(v, v->sy_kv_panel);
    build_sy_kv_row1(v, v->sy_kv_panel);
    build_sy_kv_row2(v, v->sy_kv_panel);
    build_sy_kv_row3(v, v->sy_kv_panel);
    build_sy_kv_row4(v, v->sy_kv_panel);
    build_sy_kv_row5(v, v->sy_kv_panel);
    build_sy_kv_row6(v, v->sy_kv_panel);
    build_sy_kv_row7(v, v->sy_kv_panel);
    build_sy_kv_row8(v, v->sy_kv_panel);
    build_sy_kv_row9(v, v->sy_kv_panel);
    build_sy_kv_row10(v, v->sy_kv_panel);
    build_sy_kv_row11(v, v->sy_kv_panel);
}

static void __attribute__((noinline)) build_p3_system(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->p3_system = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 0, 0, 984, 432));
    kk_style_bg(v->p3_system, kk_c(0x080B10), kk_opa(0xFF));
    lv_obj_add_flag(v->p3_system, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->p3_system, LV_OBJ_FLAG_SCROLLABLE);
    build_sy_container_panel(v, v->p3_system);
    build_sy_temp_panel(v, v->p3_system);
    build_sy_kv_panel(v, v->p3_system);
}

static void __attribute__((noinline)) build_content_area(fnos_dash_view_t *v, lv_obj_t *parent)
{
    v->content_area = kk_panel_create(parent, kk_rect(0, 0, 0, 0, 20, 124, 984, 432));
    kk_style_bg(v->content_area, kk_c(0x080B10), kk_opa(0xFF));
    lv_obj_set_style_radius(v->content_area, 0, 0);
    lv_obj_remove_flag(v->content_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->content_area, LV_OBJ_FLAG_SCROLLABLE);
    build_p0_overview(v, v->content_area);
    build_p1_storage(v, v->content_area);
    build_p2_network(v, v->content_area);
    build_p3_system(v, v->content_area);
}

void fnos_dash_view_build(fnos_dash_view_t *v, lv_obj_t *parent)
{
    lv_obj_t *scr = parent ? parent : lv_screen_active();
    v->root = kk_panel_create(scr, kk_rect(0, 0, 0, 0, 0, 0, 1024, 600));
    lv_obj_remove_flag(v->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(v->root, LV_OBJ_FLAG_SCROLLABLE);
    kk_style_bg(v->root, kk_c(0x080B10), kk_opa(0xFF));
    /* 构建整棵树（每节点一个函数，控制栈帧：v4 栈溢出修正） */
    build_top_bar(v, v->root);
    build_rail(v, v->root);
    build_bottom(v, v->root);
    build_content_area(v, v->root);
    v->page[0] = v->p0_overview;
    v->page[1] = v->p1_storage;
    v->page[2] = v->p2_network;
    v->page[3] = v->p3_system;

    lv_obj_add_event_cb(v->nav_overview, ev_nav_overview_on_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(v->nav_storage, ev_nav_storage_on_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(v->nav_network, ev_nav_network_on_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(v->nav_system, ev_nav_system_on_click, LV_EVENT_CLICKED, NULL);
    kk_swipe_attach(v->p0_overview, ev_p0_overview_on_swipe_left, ev_p0_overview_on_swipe_right);
    kk_swipe_attach(v->p1_storage, ev_p1_storage_on_swipe_left, ev_p1_storage_on_swipe_right);
    kk_swipe_attach(v->p2_network, ev_p2_network_on_swipe_left, ev_p2_network_on_swipe_right);
    kk_swipe_attach(v->p3_system, ev_p3_system_on_swipe_left, ev_p3_system_on_swipe_right);
}
