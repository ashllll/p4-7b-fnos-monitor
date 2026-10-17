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
    match = re.search(r'^(?:static )?(?:void|int|uint32_t) ' + name + r'\([^\n]*\)\n\{', source, re.M)
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
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "fnos_perf.h"
typedef int TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
typedef int lv_indev_t;
typedef int lv_timer_t;
typedef struct { int code; } lv_event_t;
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
'''
state = source[source.index('#define PF_SAMPLES'):source.index('/* ---------------------------------------------------------------- 统计 */')]
stubs = r'''
static int pointer;
static struct { lv_indev_t *indev; void (*cb)(lv_indev_t *); } s_real[PF_MAX_INDEVS];
static int s_nreal, s_ncpu0, s_ncpu1;
static pf_task_t s_cpu0[PF_TASKS], s_cpu1[PF_TASKS];
static lv_timer_t *s_script_timer;
static bool s_reported;
static void pf_detach(void) {}
static void pf_read_cb(lv_indev_t *i) { (void)i; }
static lv_indev_t *lv_indev_get_next(lv_indev_t *i) { return i ? NULL : &pointer; }
static int lv_indev_get_type(lv_indev_t *i) { (void)i; return LV_INDEV_TYPE_POINTER; }
static void (*lv_indev_get_read_cb(lv_indev_t *i))(lv_indev_t *) { (void)i; return pf_read_cb; }
static void lv_indev_set_read_cb(lv_indev_t *i, void (*cb)(lv_indev_t *)) { (void)i; (void)cb; }
static int pf_snap_tasks(void *p) { (void)p; return 0; }
static void fnos_ui_set_page(int p) { (void)p; }
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
    s_run.active = false;
    event(LV_EVENT_RENDER_START, 3100);
    event(LV_EVENT_FLUSH_START, 3200);
    event(LV_EVENT_FLUSH_FINISH, 3500);
    event(LV_EVENT_RENDER_READY, 3600);
    assert(s_parts.frames == 0 && s_parts.flush_us == 0);
    /* Equal native task names must not merge two different runtime counters. */
    s_ncpu0 = s_ncpu1 = 2; s_parts.worker = 12;
    s_cpu0[0] = (pf_task_t){.name="swdraw", .handle=11, .run=100};
    s_cpu0[1] = (pf_task_t){.name="swdraw", .handle=12, .run=200};
    s_cpu1[0] = (pf_task_t){.name="swdraw", .handle=11, .run=150};
    s_cpu1[1] = (pf_task_t){.name="swdraw", .handle=12, .run=225};
    uint32_t total, ui, idle, worker;
    pf_cpu_delta(&total, &ui, &idle, &worker);
    assert(total == 75 && worker == 25 && ui == 0 && idle == 0);
    puts("production timing callbacks: idle, task ownership, frame scope, reset passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'timing.c'
    task_type_start = source.index('typedef struct {\n    char name[configMAX_TASK_NAME_LEN]')
    task_type_end = source.index('} pf_task_t;', task_type_start) + len('} pf_task_t;')
    c.write_text(prefix + state + source[task_type_start:task_type_end] + stubs + '\n'.join(function(n) for n in
                 ['pf_push', 'pf_start_cb', 'pf_render_cb', 'pf_start', 'pf_cpu_delta']) + checks)
    exe = Path(tmp) / 'timing'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-I', str(ROOT / 'components/fnos_monitor'), str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
