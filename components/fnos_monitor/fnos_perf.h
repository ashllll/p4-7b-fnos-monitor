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

/* 落定记账：只接受第一个“松手后新起动画”的手势，page/方向/快照必须来自同一次
   松手（调用方把快照一起传入，避免多次落定互相覆盖）。rel_iv_max_us/rel_long_n
   与 rel_* 一样只描述这次落定；全段最大值/长帧数看 [bench] 的 iv_max_us / iv_long_n
   （其余 *_max 是采样窗口内的最大值）。 */
void fnos_perf_note_motion_settle(int page, uint32_t seq, bool new_snapshot);
void fnos_perf_note_fin_skip(void);
void fnos_perf_note_refresh_parts(uint32_t health_us, uint32_t shell_us, uint32_t body_us);
void fnos_perf_note_pair(uint32_t us);
void fnos_perf_note_refresh_prep(uint32_t us);
/* motion_finish 内部分段：几何复位 / 整页布局 / 同步 refresh；motion_prepare 里的目标页布局 */
void fnos_perf_note_settle_parts(uint32_t fix_us, uint32_t layout_us, uint32_t refresh_us);
void fnos_perf_note_prep_page(uint32_t us);
void fnos_perf_note_p1_parts(uint32_t vol_us, uint32_t raid_us, uint32_t disk_us, uint32_t inv_us);
void fnos_perf_note_prep_refresh(uint32_t us);
void fnos_perf_note_pool_relayout(uint32_t mask);
/* 布局归因窗口：1=motion_prepare，2=落定 refresh，3=普通 ui_tick refresh；0=关闭。 */
void fnos_perf_layout_window(int win);
/* kind：0=network_pool_layout 1=inventory_sections_layout 2=pool_layout；relayout 记真实重排次数。 */
void fnos_perf_note_layout(int kind, uint32_t us, bool relayout);
/* 网口池 shape 失配字段掩码：1 池宽 2 卡片宽 4 数量 8 行距 16 行高 32 子部件高度。 */
void fnos_perf_note_shape_mismatch(uint32_t mask);
void fnos_perf_note_selfsize(uint32_t us, bool changed);
void fnos_perf_note_scr_dirty(bool dirty);
void fnos_perf_note_motion_start(void);

/* 给 trend chart 挂绘制计时；void* 是为了让本头不依赖 lvgl.h。生产构建里是空操作。 */
void fnos_perf_track_chart(void *chart);

bool fnos_perf_cli(char *line);

#ifdef __cplusplus
}
#endif
