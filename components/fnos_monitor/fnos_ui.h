#pragma once
#include <stdbool.h>

// UIManager（KK_UI 生命周期层）：
//   * 开机 Preload：用 kk_ui 构件手写四页（JSON 清单 / 代码生成 / MVVM-C 已全部删除）
//   * Open = 原子换页（先隐藏全部旧页并归零偏移，再显示目标页，最后整屏失效）
//   * 500 ms LVGL tick 拉 fnos_data 快照并刷文案（本文件是唯一的 LVGL 写入者）
//   * set_night 只置标志（esp_timer 任务调用），落地在 LVGL 任务里（线程红线）
//
// 必须在持有显示锁（bsp_display_lock）时调用 fnos_ui_create()。

#define FNOS_UI_PAGE_COUNT 4

#ifdef __cplusplus
extern "C" {
#endif

void fnos_ui_create(void);        // 幂等：Preload + Open(0)
void fnos_ui_set_page(int idx);   // 0..FNOS_UI_PAGE_COUNT-1，越界循环
int  fnos_ui_page(void);
void fnos_ui_set_night(bool on);  // 夜间配色请求（异步落地）

#ifdef __cplusplus
}
#endif
