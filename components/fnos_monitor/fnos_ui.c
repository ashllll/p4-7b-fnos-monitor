// UIManager 实现：页面生命周期 + LVGL tick（KK_UI_UMG 的 UIManager 等价物）。
// 职责边界：本文件不写业务数据、不格式化文案；只管 Preload/Open 与 tick 调度。
// 线程红线：所有 LVGL API 都在 LVGL 任务里调（fnos_ui_create 持显示锁；tick 是 lv_timer）。
#include "fnos_ui.h"
#include <stdio.h>
#include <string.h>

#include "fnos_dash_view.generated.h"
#include "fnos_dash_store.generated.h"
#include "fnos_dash_binder.generated.h"
#include "FnosDashboardController.h"
#include "FnosDashboardViewAnim.h"
#include "kk_theme.h"

#include "esp_log.h"

static const char *TAG = "fnos_ui";

static fnos_dash_view_t  s_view;
static fnos_dash_store_t s_store;
static bool s_created;
static int  s_page;
static bool s_night;                 // LVGL 任务内生效值
static volatile bool s_night_req;    // esp_timer → LVGL 任务的唯一入口


static void ui_tick(lv_timer_t *t)
{
    (void)t;
    if (s_night_req != s_night) {
        s_night = s_night_req;
        FnosDashAnim_SetNight(&s_view, s_night);
    }
    // 业务快照 → Store（Controller 唯一写入者）→ Binder 刷屏（唯一 LVGL 写入者）
    FnosDashboardController_Tick();
}

void fnos_ui_create(void)
{
    if (s_created) return;

    fnos_dash_store_init(&s_store);
    fnos_dash_view_build(&s_view, NULL);
    FnosDashboardController_Init(&s_view, &s_store);

    // 静态首帧：Store 默认值整体刷一遍上屏（warming 文案就是这么来的，之后靠脏位增量刷）
    memset(s_store.dirty, 0xFF, sizeof(s_store.dirty));
    fnos_dash_binder_flush(&s_view, &s_store);

    s_page = 0;
    for (int i = 0; i < FNOS_DASH_PAGE_COUNT; i++) {
        if (!s_view.page[i]) continue;
        lv_obj_set_pos(s_view.page[i], 0, 0);   // 页必须钉在内容区原点（否则会盖住顶栏/导航）
        if (i == 0) lv_obj_remove_flag(s_view.page[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_view.page[i], LV_OBJ_FLAG_HIDDEN);
    }
    FnosDashAnim_SelectNav(&s_view, 0);

    FnosDashboardController_Tick();             // 静态首帧（warming 状态）先落地

    lv_timer_create(ui_tick, 500, NULL);
    s_created = true;
    ESP_LOGI(TAG, "ui created (pages=%d)", FNOS_DASH_PAGE_COUNT);
}

void fnos_ui_set_page(int idx)
{
    if (!s_created) return;
    const int n = FNOS_DASH_PAGE_COUNT;
    while (idx < 0) idx += n;
    while (idx >= n) idx -= n;
    if (idx == s_page) return;
    s_page = idx;

    // 原子换页：本板是"全屏页 + partial buffer"，双页位移动画会留残影，只做整页切换
    for (int i = 0; i < n; i++) {
        if (!s_view.page[i]) continue;
        lv_obj_set_pos(s_view.page[i], 0, 0);
        lv_obj_add_flag(s_view.page[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_view.page[idx]) lv_obj_remove_flag(s_view.page[idx], LV_OBJ_FLAG_HIDDEN);
    FnosDashAnim_SelectNav(&s_view, idx);
    lv_obj_invalidate(lv_screen_active());
}

int fnos_ui_page(void)
{
    return s_page;
}

void fnos_ui_set_night(bool on)
{
    s_night_req = on;                            // 只置标志，落地在 ui_tick
}
