// 手写视觉动效 partial 实现（KK View partial 规则：只动视觉属性）。
// v4.1：导航改为横向 Tab（docs/ui-kk-composition.md），选中态与指示条随之横移。
// 注意：运行时取坐标没问题（对象已刷新）；build 期取坐标才是坑（见 MEMORY）。
#include "FnosDashboardViewAnim.h"
#include "kk_theme.h"
#include "lvgl.h"

static void nav_ind_anim_x(void *var, int32_t x)
{
    lv_obj_set_x((lv_obj_t *)var, x);
}

static void nav_ind_move(lv_obj_t *ind, lv_obj_t *tab)
{
    if (!ind || !tab) return;
    // 指示条贴在选中 Tab 底边居中（几何运行时取，与 manifest 初始位置无关）
    int32_t target_x = lv_obj_get_x(tab) + (lv_obj_get_width(tab) - lv_obj_get_width(ind)) / 2;
    int32_t target_y = lv_obj_get_y(tab) + lv_obj_get_height(tab) - lv_obj_get_height(ind);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ind);
    lv_anim_set_values(&a, lv_obj_get_x(ind), target_x);
    lv_anim_set_time(&a, 180);
    lv_anim_set_exec_cb(&a, nav_ind_anim_x);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    lv_obj_set_y(ind, target_y);
}

void FnosDashAnim_SelectNav(fnos_dash_view_t *v, int page)
{
    if (!v) return;
    lv_obj_t *items[4] = { v->nav_overview, v->nav_storage, v->nav_network, v->nav_system };
    lv_obj_t *labels[4] = { v->nav_overview_label, v->nav_storage_label,
                            v->nav_network_label, v->nav_system_label };
    for (int i = 0; i < 4; i++) {
        if (items[i]) {
            // 选中 Tab = 强调蓝实底；未选 = 条带底（KK 工具行语法）
            lv_obj_set_style_bg_color(items[i],
                lv_color_hex(i == page ? KK_ACCENT : KK_INSET), 0);
            lv_obj_set_style_bg_opa(items[i], LV_OPA_COVER, 0);
        }
        if (labels[i]) {
            lv_obj_set_style_text_color(labels[i],
                lv_color_hex(i == page ? KK_TEXT1 : KK_TEXT2), 0);
        }
    }
    nav_ind_move(v->nav_indicator, items[page]);
}

void FnosDashAnim_SetNight(fnos_dash_view_t *v, bool on)
{
    if (!v || !v->root) return;
    // 夜间：只换底色（纯黑），不叠全屏压暗层（v4 教训：压暗层是"绿屏闪烁"头号嫌疑）。
    // 背光调光由 main.cpp 的夜间定时器负责（12%→20%，远离低占空比感知区）。
    lv_obj_set_style_bg_color(v->root, lv_color_hex(on ? 0x000000 : KK_BG), 0);
}
