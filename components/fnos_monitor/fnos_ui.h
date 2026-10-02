#pragma once
#include <stdbool.h>

// UIManager（KK_UI_UMG 生命周期层的 LVGL 等价物）：
//   * 持有 View / Store 实例，开机 Preload（建整棵树 + 静态首帧）
//   * Open = 原子换页（先隐藏全部旧页并归零偏移，再显示目标页，最后整屏失效）
//   * 500 ms LVGL tick 上跑 Controller_Tick：业务快照 → Store → Binder 刷屏
//   * set_night 只置标志（esp_timer 任务调用），落地在 LVGL 任务里（线程红线）
//
// 必须在持有显示锁（bsp_display_lock）时调用 fnos_ui_create()。
#ifdef __cplusplus
extern "C" {
#endif

void fnos_ui_create(void);        // 幂等：Preload + Open(0)
void fnos_ui_set_page(int idx);   // 0..3，越界循环
int  fnos_ui_page(void);
void fnos_ui_set_night(bool on);  // 夜间配色请求（异步落地）

#ifdef __cplusplus
}
#endif
