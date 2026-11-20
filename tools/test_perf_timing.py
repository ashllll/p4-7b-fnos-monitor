#!/usr/bin/env python3
"""Run production bench callbacks with a controlled clock; no device required."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'components/fnos_monitor/fnos_perf.c').read_text()


def function(name):
    import re
    match = re.search(r'^(?:static )?(?:void|int|int32_t|uint32_t|bool) ' + name + r'\([^\n]*\)\n\{', source, re.M)
    if not match:
        raise AssertionError('production function missing: ' + name)
    end = source.index('{', match.start())
    depth = 1
    end += 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


prefix = r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fnos_perf.h"
typedef int TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
typedef int lv_indev_t;
typedef int lv_timer_t;
typedef int lv_display_t;
typedef struct { int code; } lv_event_t;
typedef struct { struct { int x, y; } point; int state; } lv_indev_data_t;
enum { LV_INDEV_STATE_RELEASED, LV_INDEV_STATE_PRESSED };
enum { LV_EVENT_RENDER_START, LV_EVENT_RENDER_READY, LV_EVENT_FLUSH_START,
       LV_EVENT_FLUSH_FINISH, LV_EVENT_FLUSH_WAIT_START, LV_EVENT_FLUSH_WAIT_FINISH };
#define LV_INDEV_TYPE_POINTER 1
#define configMAX_TASK_NAME_LEN 16
#define FNOS_UI_PAGE_COUNT 6
#define LV_PROFILER_BEGIN_TAG(x) ((void)0)
#define LV_PROFILER_END_TAG(x) ((void)0)
#define ESP_LOGI(...) ((void)0)
static int64_t clock_us;
static TaskHandle_t task = 1;
static int64_t esp_timer_get_time(void) { return clock_us; }
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return task; }
static int lv_event_get_code(lv_event_t *e) { return e->code; }
static void *event_user;
static void *lv_event_get_user_data(lv_event_t *e) { (void)e; return event_user; }
'''
state = source[source.index('#define PF_SAMPLES'):source.index('/* ---------------------------------------------------------------- 统计 */')]
stubs = r'''
static int pointer;
static struct { lv_indev_t *indev; void (*cb)(lv_indev_t *, lv_indev_data_t *); } s_real[PF_MAX_INDEVS];
static int s_nreal, s_ncpu0, s_ncpu1;
static pf_task_t s_cpu0[PF_TASKS], s_cpu1[PF_TASKS];
static lv_timer_t *s_script_timer;
static bool s_reported;
static void pf_detach(void) {}
static void pf_stop(void);
static void pf_report(void) {}
static void pf_read_cb(lv_indev_t *i, lv_indev_data_t *d);
static void lv_timer_pause(lv_timer_t *t) { (void)t; }
static void pf_probe(int n) { (void)n; }
static void uk_trend_set_gesture_fps(uint32_t fps) { (void)fps; }
static lv_indev_t *lv_indev_get_next(lv_indev_t *i) { return i ? NULL : &pointer; }
static int lv_indev_get_type(lv_indev_t *i) { (void)i; return LV_INDEV_TYPE_POINTER; }
static void (*lv_indev_get_read_cb(lv_indev_t *i))(lv_indev_t *, lv_indev_data_t *) { (void)i; return pf_read_cb; }
static void lv_indev_set_read_cb(lv_indev_t *i, void (*cb)(lv_indev_t *, lv_indev_data_t *)) { (void)i; (void)cb; }
static int pf_snap_tasks(void *p) { (void)p; return 0; }
static void fnos_ui_set_page(int p) { (void)p; }
static bool fnos_ui_nav_center(int idx, int32_t *x, int32_t *y) { (void)idx; *x = 300; *y = 400; return true; }
static int32_t s_swipe_x = 737, s_swipe_y = 310;
static bool fnos_ui_swipe_point(int dir, int32_t *x, int32_t *y) { (void)dir; *x = s_swipe_x; *y = s_swipe_y; return true; }
static lv_display_t *lv_display_get_default(void) { return NULL; }
static int32_t lv_display_get_horizontal_resolution(lv_display_t *d) { (void)d; return 1024; }
static void lv_timer_resume(lv_timer_t *t) { (void)t; }
'''
checks = r'''
static void event(int code, int64_t at) {
    clock_us = at;
    lv_event_t e = {code};
    if (code == LV_EVENT_RENDER_START) pf_start_cb(&e);
    else if (code == LV_EVENT_RENDER_READY) pf_render_cb(&e);
    else pf_parts_cb(&e);
}
int main(void) {
    /* Bench parameters come off the serial CLI: reject junk and overflow instead of atoi UB. */
    assert(pf_int("120", 5) == 120);
    assert(pf_int("0", 5) == 0);
    assert(pf_int(" 4 ", 5) == 4);
    assert(pf_int(NULL, 5) == 5);
    assert(pf_int("", 7) == 7);
    assert(pf_int("-1", 5) == 5);
    assert(pf_int("12x", 5) == 5);
    assert(pf_int("2147483647", 5) == 2147483647);
    assert(pf_int("2147483648", 5) == 5);
    assert(pf_int("999999999999999999999999", 5) == 5);
    (void)s_cap_req; (void)s_cap_req_valid; (void)s_probe_req; (void)s_probe_req_valid;
    s_owner = 99; /* UI creation may have happened on main, not LVGL. */
    s_req.kind = PF_IDLE; s_req.seconds = 1;
    clock_us = 1000; pf_start();
    assert(s_owner == task);
    assert(s_run.frame_arm); /* Idle must sample timing without pointer presses. */
    event(LV_EVENT_RENDER_START, 1100);
    task = 2; fnos_perf_note_stage(FNOS_PERF_PPA, 9000); task = 1;
    fnos_perf_note_stage(FNOS_PERF_PPA, 100);
    task = 2;
    fnos_perf_note_stage(FNOS_PERF_BLEND, 25);
    fnos_perf_note_stage(FNOS_PERF_TEXT, 80);
    task = 1;
    event(LV_EVENT_FLUSH_START, 1200);
    event(LV_EVENT_FLUSH_FINISH, 1500);
    event(LV_EVENT_FLUSH_WAIT_START, 1501);
    event(LV_EVENT_FLUSH_WAIT_FINISH, 1551);
    event(LV_EVENT_RENDER_READY, 2000);
    assert(s_parts.frames == 1 && s_parts.frame_us == 900);
    assert(s_parts.flush_us == 300 && s_parts.flush_wait_us == 50);
    assert(s_parts.stage[FNOS_PERF_PPA].calls == 1);
    assert(s_parts.stage[FNOS_PERF_PPA].us == 100);
    assert(s_parts.worker_calls == 2);
    assert(s_parts.stage[FNOS_PERF_BLEND].us == 25 && s_parts.stage[FNOS_PERF_TEXT].us == 80);
    fnos_perf_note_stage(FNOS_PERF_PPA, 9000); /* Outside a frame must not count. */
    task = 2; fnos_perf_note_stage(FNOS_PERF_BLEND, 9000); task = 1;
    assert(s_parts.stage[FNOS_PERF_BLEND].us == 25);
    event(LV_EVENT_RENDER_START, 2100);
    event(LV_EVENT_RENDER_READY, 2600);
    assert(s_niv == 1 && s_iv[0] == 600 && s_nrs2rr == 2);
    assert(s_parts.stage[FNOS_PERF_PPA].us == 100);
    clock_us = 3000; pf_start();
    assert(s_parts.frames == 0 && s_parts.flush_us == 0 && s_parts.stage[0].calls == 0);
    /* UI stages accumulate across the whole run, not only inside RENDER_START..READY. */
    fnos_perf_note_ui_stage(FNOS_PERF_PAGE_PAINT, 100);
    fnos_perf_note_ui_stage(FNOS_PERF_PAGE_PAINT, 60);
    fnos_perf_note_ui_stage(FNOS_PERF_PAGE_LAYOUT, 40);
    assert(s_ui.calls[FNOS_PERF_PAGE_PAINT] == 2 && s_ui.us[FNOS_PERF_PAGE_PAINT] == 160);
    assert(s_ui.max_us[FNOS_PERF_PAGE_PAINT] == 100);
    assert(s_ui.us[FNOS_PERF_PAGE_LAYOUT] == 40);
    fnos_perf_note_tick(100, true);
    fnos_perf_note_tick(50, false);
    assert(s_ntick == 2 && s_ntick_motion == 1 && s_ntick_full == 1);
    assert(s_tick_motion[0] == 100 && s_tick_full[0] == 50);
    /* chart_frames counts frame_arm frames that drew a chart, not draw calls. */
    int64_t chart_start = 0;
    lv_event_t ce = {0};
    event_user = &chart_start;
    event(LV_EVENT_RENDER_START, 3300);
    clock_us = 3310; pf_chart_begin(&ce);
    clock_us = 3340; pf_chart_end(&ce);
    assert(s_chart_drew && s_ui.calls[FNOS_PERF_CHART_DRAW] == 1 && s_ui.us[FNOS_PERF_CHART_DRAW] == 30);
    event(LV_EVENT_RENDER_READY, 3400);
    assert(s_chart_frames == 1);
    event(LV_EVENT_RENDER_START, 3500);          /* RENDER_START 清本帧标记 */
    assert(!s_chart_drew);
    event(LV_EVENT_RENDER_READY, 3600);          /* 本帧没有 chart 绘制 */
    assert(s_chart_frames == 1);
    s_run.frame_arm = false;                     /* settle 期绘制既不数帧也不计时 */
    event(LV_EVENT_RENDER_START, 3700);
    clock_us = 3710; pf_chart_begin(&ce);
    clock_us = 3750; pf_chart_end(&ce);
    assert(s_ui.calls[FNOS_PERF_CHART_DRAW] == 1);
    event(LV_EVENT_RENDER_READY, 3800);
    assert(s_chart_frames == 1);
    s_run.frame_arm = true;
    uint32_t frames_before = s_parts.frames;
    s_run.active = false;
    fnos_perf_note_ui_stage(FNOS_PERF_PAGE_PAINT, 7);
    fnos_perf_note_tick(9, true);
    assert(s_ui.calls[FNOS_PERF_PAGE_PAINT] == 2 && s_ntick == 2);
    s_probe_mode = true;   /* bench probe measures the active path without a run */
    fnos_perf_note_ui_stage(FNOS_PERF_PAGE_PAINT, 5);
    assert(s_ui.us[FNOS_PERF_PAGE_PAINT] == 165);
    s_probe_mode = false;
    event(LV_EVENT_RENDER_START, 4100);
    event(LV_EVENT_FLUSH_START, 4200);
    event(LV_EVENT_FLUSH_FINISH, 4500);
    event(LV_EVENT_RENDER_READY, 4600);
    assert(s_parts.frames == frames_before && s_parts.flush_us == 0);
    /* Equal native task names must not merge two different runtime counters. */
    s_ncpu0 = s_ncpu1 = 2; s_parts.worker = 12;
    s_cpu0[0] = (pf_task_t){.name="swdraw", .handle=11, .run=100};
    s_cpu0[1] = (pf_task_t){.name="swdraw", .handle=12, .run=200};
    s_cpu1[0] = (pf_task_t){.name="swdraw", .handle=11, .run=150};
    s_cpu1[1] = (pf_task_t){.name="swdraw", .handle=12, .run=225};
    uint32_t total, ui, idle, worker;
    pf_cpu_delta(&total, &ui, &idle, &worker);
    assert(total == 75 && worker == 25 && ui == 0 && idle == 0);
    s_run.active = true;
    /* 墙钟位置：时间跳变只跳到当前应到的位置，不在慢回调里补跑旧步数。 */
    assert(pf_drag_pos(737, 600, -1, 0, 2000) == 737);
    assert(pf_drag_pos(737, 600, -1, 200, 2000) == 677);
    assert(pf_drag_pos(737, 600, -1, 1000, 2000) == 437);
    assert(pf_drag_pos(737, 600, -1, 1999, 2000) == 138);   /* 截断，不越过终点 */
    assert(pf_drag_pos(737, 600, -1, 2000, 2000) == 137);
    assert(pf_drag_pos(737, 600, -1, 3000, 2000) == 137);
    assert(pf_drag_pos(737, 600, 1, 1000, 2000) == 1037);
    for (uint32_t t = 0; t <= 2000; t += 37) {
        int32_t l = pf_drag_pos(737, 600, -1, t, 2000);
        int32_t r = pf_drag_pos(737, 600, 1, t, 2000);
        assert(l <= 737 && r >= 737 && (737 - l) == (r - 737));
    }
    /* 慢回调/时间跳跃：按下在 816 ms，脚本 tick 到 2.85 s 才跑；必须一次到终点并释放。 */
    s_nreal = 0;   /* test stub's get_read_cb returns pf_read_cb; avoid self-recursion */
    s_run.px = 600; s_run.drag_ms = 2000; s_run.kind = PF_DRAG; s_run.dir = -1;
    s_run.active = true; s_run.phase = PH_SETTLE; s_run.phase_us = 800000;
    clock_us = 800000; pf_arm(clock_us);
    assert(s_run.pressed && s_run.phase == PH_DRAG && s_run.x0 == 737 && s_run.span == 600);
    clock_us = 816000;
    lv_indev_data_t press = {0}; press.state = LV_INDEV_STATE_PRESSED;
    pf_read_cb(&pointer, &press);
    assert(s_run.press_seen && s_run.press_us == 816000);
    clock_us = 1016000; pf_script_tick(NULL);
    assert(s_run.x == 677 && s_run.pressed);
    clock_us = 2850000; pf_script_tick(NULL);
    assert(!s_run.pressed && s_run.phase == PH_RELEASE);
    assert(s_run.x == 137 && s_run.hold_ms == 2034);
    s_req.kind = PF_DRAG; s_req.page = 2; s_req.px = 600; s_req.drag_ms = 2000; s_req.cycles = 1;
    clock_us = 5000; pf_start();
    assert(s_run.kind == PF_DRAG && s_run.drag_ms == 2000 && s_run.cycles == 1);
    assert(s_run.dir == -1);
    clock_us = 6000; pf_start();
    assert(s_run.dir == 1);   /* 下一次 drag 反向，左右两端都覆盖 */

    /* Cancel while pressed: one synthetic RELEASED must reach LVGL before detach. */
    s_run.active = true; s_run.kind = PF_DRAG; s_run.pressed = true; s_run.x = 123; s_run.y = 45;
    s_nreal = 1; s_real[0].indev = &pointer; s_real[0].cb = NULL; s_script_timer = NULL;
    pf_stop();
    assert(!s_run.active && !s_run.pressed && s_release_pending);
    lv_indev_data_t rd = {0};
    rd.state = LV_INDEV_STATE_PRESSED;
    pf_read_cb(&pointer, &rd);
    assert(rd.state == LV_INDEV_STATE_RELEASED && rd.point.x == 123 && rd.point.y == 45);
    assert(!s_release_pending);
    s_nreal = 0;
    s_run.active = true;   /* 继续跑后面的 tick 饱和断言 */

    /* 主缓冲饱和后分相缓冲也停收：并集必须仍等于主行样本数。 */
    for (int i = 0; i < 300; i++) fnos_perf_note_tick((uint32_t)i, true);
    for (int i = 0; i < 100; i++) fnos_perf_note_tick((uint32_t)i, false);
    assert(s_ntick == PF_SAMPLES);
    assert(s_ntick_motion + s_ntick_full == s_ntick);
    s_run.active = false;

    /* 第 257 个间隔才是最长帧：全段 total/long/max 必须覆盖它，
       分位数仍只基于前 PF_SAMPLES 个样本，截断标记必须如实。 */
    s_req.kind = PF_IDLE; s_req.seconds = 1; s_req.page = 2;
    s_nreal = 1; s_real[0].indev = &pointer; s_real[0].cb = NULL;
    clock_us = 100000; pf_start();
    assert(s_run.active && s_run.frame_arm);
    event(LV_EVENT_RENDER_START, 100000);
    event(LV_EVENT_RENDER_READY, 101000);          /* 建立 last，无间隔样本 */
    for (int i = 0; i < PF_SAMPLES; i++) {
        int64_t t = 100000 + (int64_t)(i + 1) * 20000;
        event(LV_EVENT_RENDER_START, t);
        event(LV_EVENT_RENDER_READY, t + 1000);    /* 前 256 个间隔都是 20 ms */
    }
    int64_t long_ready = 100000 + (int64_t)PF_SAMPLES * 20000 + 1000 + 100000;
    event(LV_EVENT_RENDER_START, long_ready - 1000);
    event(LV_EVENT_RENDER_READY, long_ready);      /* 第 257 个间隔 100 ms，缓冲已满不入选 */
    assert(s_niv_total == PF_SAMPLES + 1);
    assert(s_niv == PF_SAMPLES && s_niv < (int)s_niv_total);
    assert(s_niv_long == 1 && s_iv_max_all == 100000);
    for (int i = 0; i < PF_SAMPLES; i++) assert(s_iv[i] == 20000);
    s_run.active = false;

    /* 新命令在拖动按住期间到达：必须先送 RELEASED，不能直接 pf_start 把取消丢掉。 */
    s_run.active = true; s_run.pressed = true; s_run.x = 7; s_run.y = 8;
    s_nreal = 1; s_real[0].indev = &pointer; s_real[0].cb = NULL; s_script_timer = NULL;
    s_req.valid = true; s_req.kind = PF_DRAG; s_req.page = 2; s_req.px = 600;
    s_req.drag_ms = 2000; s_req.cycles = 1;
    s_cap_req_valid = false; s_probe_req_valid = false;
    pf_req_poll(NULL);
    assert(s_req.valid && s_release_pending && !s_run.active);
    lv_indev_data_t rd2 = {0};
    pf_read_cb(&pointer, &rd2);
    assert(!s_release_pending && rd2.state == LV_INDEV_STATE_RELEASED);
    pf_req_poll(NULL);
    assert(!s_req.valid && s_run.active);
    fprintf(stderr, "production timing callbacks: argument parsing, idle, task ownership, frame scope, reset passed\n");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'timing.c'
    task_type_start = source.index('typedef struct {\n    char name[configMAX_TASK_NAME_LEN]')
    task_type_end = source.index('} pf_task_t;', task_type_start) + len('} pf_task_t;')
    c.write_text(prefix + state + source[task_type_start:task_type_end] + stubs + '\n'.join(function(n) for n in
                 ['pf_push', 'pf_int', 'pf_drag_pos', 'pf_arm', 'pf_next_cycle', 'pf_script_tick', 'pf_start_cb', 'pf_render_cb', 'pf_start', 'pf_stop', 'pf_read_cb', 'pf_req_poll', 'pf_cpu_delta', 'fnos_perf_note_tick', 'pf_chart_begin', 'pf_chart_end']) + checks)
    exe = Path(tmp) / 'timing'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-I', str(ROOT / 'components/fnos_monitor'), str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
