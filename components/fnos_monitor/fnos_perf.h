#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 串口台架（CONFIG_FNOS_UI_PERF_BENCH）：默认构建里全是空实现，不进生产路径。 */

void fnos_perf_init(void);

/* ui_tick 一次的总耗时；motion 表示这次 tick 落在页面运动里（与整刷 tick 分开看）。 */
void fnos_perf_note_tick(uint32_t us, bool motion);

typedef enum {
    FNOS_PERF_PPA,          /* flush 子阶段：PPA 缩放/旋转 */
    FNOS_PERF_DMA2D,        /* flush 子阶段：DMA2D 拷贝 */
    FNOS_PERF_SUBMIT,       /* flush 子阶段：panel draw_bitmap 提交 */
    FNOS_PERF_FREE_BUFFER,  /* flush 子阶段：等空闲缓冲 */
    FNOS_PERF_TASK_CREATE,  /* 绘制任务：创建 */
    FNOS_PERF_TASK_PICK,    /* 绘制任务：取任务 */
    FNOS_PERF_BLEND,        /* 绘制任务：软件混合（含 text） */
    FNOS_PERF_TEXT,         /* 绘制任务：文字（含 blend） */
    FNOS_PERF_PAGE_PAINT,   /* UI：页面揭示一帧（motion_paint） */
    FNOS_PERF_PAGE_LAYOUT,  /* UI：页面布局（page_layout） */
    FNOS_PERF_CHART_DRAW,   /* UI：一张 trend chart 的绘制 */
    FNOS_PERF_STAGE_COUNT
} fnos_perf_stage_t;

void fnos_perf_note_stage(fnos_perf_stage_t stage, uint32_t us);

/* 帧间阶段（动画 exec / ui_tick）不走 RENDER_START..READY 门，单独累计；
   chart_draw 另受 frame_arm 窗口约束，以便与 chart_frames 对齐。 */
void fnos_perf_note_ui_stage(fnos_perf_stage_t stage, uint32_t us);

/* 给 trend chart 挂绘制计时；void* 是为了让本头不依赖 lvgl.h。生产构建里是空操作。 */
void fnos_perf_track_chart(void *chart);

bool fnos_perf_cli(char *line);

#ifdef __cplusplus
}
#endif
