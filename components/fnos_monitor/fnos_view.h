#pragma once
// 1024x600 触摸面板上的 fnOS 仪表盘（LVGL 9.5）。
// 必须在持有显示锁（bsp_display_lock）时调用 fnos_view_create()：
// 本模块自己不建任务、不加锁，所有刷新都跑在 LVGL 任务里的定时器上。
#ifdef __cplusplus
extern "C" {
#endif

void fnos_view_create(void);   // 幂等
void fnos_view_set_page(int idx);
int  fnos_view_page(void);

#ifdef __cplusplus
}
#endif
