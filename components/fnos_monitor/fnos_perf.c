#include "fnos_perf.h"

#include "sdkconfig.h"   /* 本仓库不强制 -include sdkconfig.h；这个编译单元没有别的头带它进来 */

#ifdef CONFIG_FNOS_UI_PERF_BENCH

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#if CONFIG_LV_USE_PROFILER
/* 配置结构体只定义在私有头里（公开头里是个不完整类型，栈上开不出来）。 */
#include "src/debugging/profiler/lv_profiler_builtin_private.h"
#endif

#include "fnos_ui.h"
#include "ui_kit/uk.h"

static const char *TAG = "perf";

#if CONFIG_LV_USE_PROFILER
/* ---- LVGL 官方 profiler（只编进台架固件，生产固件没有 CONFIG_LV_USE_PROFILER）----
   用途：把"每帧绘制 ~12ms 到底花在哪些函数上"用官方工具量出来，而不是靠猜。
   入口：串口 `prof once <ms>` = 清缓冲 → 开记录 → 等 ms → 关 → 把 FTrace 文本打到串口；
        `prof on|off|dump` 是手动挡。
   节奏要点：本函数跑在 wifi_cli 任务里（不是 LVGL 任务），vTaskDelay 不挡渲染，
        所以采到的就是拖动帧本身——代价是 profiler 自己那点记账开销（只看占比，别当绝对耗时）。 */
static uint64_t pf_prof_tick(void) { return (uint64_t)esp_timer_get_time(); }   /* µs */
static void pf_prof_flush(const char *buf) { fputs(buf, stdout); }

static void pf_prof_arm(void)
{
    lv_profiler_builtin_config_t cfg;
    lv_profiler_builtin_config_init(&cfg);
    cfg.buf_size     = CONFIG_LV_PROFILER_BUILTIN_BUF_SIZE;
    cfg.tick_per_sec = 1000000;      /* esp_timer_get_time() 给的是微秒 */
    cfg.tick_get_cb  = pf_prof_tick;
    cfg.flush_cb     = pf_prof_flush;
    /* 重新 init = 清空缓冲：官方 flush 只打印、不重置游标，不清就会把上一轮的记录再打一遍。 */
    lv_profiler_builtin_init(&cfg);
    lv_profiler_builtin_set_enable(true);
}
#endif

#define PF_SAMPLES        256     /* 每种样本的上限；满了丢新样本（报里的 n 让人看得见） */
#define PF_STEP_MS        8       /* 脚本推进节奏 = UK_GESTURE_POLL_MS */
#define PF_SETTLE_MS      800     /* 换页请求由 ui_tick 落地（一个数据帧周期，默认 200 ms），留余量 */
#define PF_TAP_HOLD_MS    60      /* 按住时长：短于读周期就可能一次都没报按下 */
#define PF_ARM_TIMEOUT_MS 300     /* 置了按下却等不到 indev 上报，判 miss */
#define PF_RELEASE_MS     80      /* 松开后等 indev 把 RELEASED 读走 */
#define PF_TAP_RELEASE_MS 200     /* tap 松开后再等到页面落定附近（80–160 ms 转场） */
#define PF_TAIL_MS        500     /* 松手后收尾：吸附动画 + 一帧静止 */
#define PF_DRAG_STEPS     15      /* 15 步 × 8ms ≈ 120ms 的横滑 */
#define PF_DRAG_MIN_MS    200     /* bench drag 最短保持时间 */
#define PF_DRAG_MAX_MS    10000   /* bench drag 最长保持时间 */
#define PF_DRAG_MAX_SPAN  700     /* 1024 宽屏内，从 72%/28% 起点留出安全行程 */
#define PF_MAX_INDEVS     4
#define PF_TASKS          24
#define PF_REQ_POLL_MS    100
#define PF_LONG_FRAME_US  33300   /* 验收长帧阈值：≥33.3 ms */

typedef enum { PF_NONE = 0, PF_TAP, PF_SWIPE, PF_DRAG, PF_IDLE } pf_kind_t;
typedef enum { PH_IDLE = 0, PH_SETTLE, PH_HOLD, PH_DRAG, PH_RELEASE, PH_TAIL } pf_phase_t;

/* 串口任务写、LVGL 任务读：只有 valid 一个字的交接，够用且不用锁
   （写完参数才置 valid；读方见 valid 先清再取参数）。 */
static struct {
    volatile bool valid;
    pf_kind_t kind;
    int page, px, cycles, seconds, drag_ms;
} s_req;

/* cap/probe 也由串口任务置请求、LVGL 任务执行：uk 的全局变量与 s_ui 都归 LVGL 线程。 */
static volatile bool s_cap_req_valid;
static volatile int s_cap_req;
static volatile bool s_probe_req_valid;
static volatile int s_probe_req;

static struct {
    bool active;
    pf_kind_t kind;
    int page, px, cycles, done, misses, idle_ms, drag_ms;
    pf_phase_t phase;
    int64_t phase_us, run_us;
    int32_t x0, y0, x, y, span;
    int step, steps, dir;
    bool pressed, press_seen, rel_seen;
    bool wait_first, wait_rel, frame_arm, wait_p2rs;
    int64_t press_us, release_us;
    uint32_t frames;
    uint32_t rel_frames;   /* 松手→落定之间渲染完成的帧数（独立于 frame_arm 80ms 窗口） */
    uint32_t rel_gen;      /* 松手时的 motion 代数：只有之后新起的手势动画才算本次落定 */
    int rel_dir;           /* 该次松手的拖动方向：落定被接受后与 page/快照一起归档 */
    uint32_t hold_ms;   /* drag：press_seen 到脚本释放的实际墙钟时长 */
} s_run;
static bool s_release_pending;  /* 取消时欠 LVGL 一次合成的 RELEASED */
static int s_drag_dir = -1;     /* A 段连续拖动左右交替 */

static uint32_t s_p2f[PF_SAMPLES];   /* 按下 → 首帧：有没有触控反馈、反馈多快 */
static uint32_t s_r2f[PF_SAMPLES];   /* 松手 → 首帧：点一下多久有反应（换页那一帧） */
static uint32_t s_iv[PF_SAMPLES];    /* 相邻帧间隔：拖动跟手 / 稳态帧率 */
/* 全段计数：数组只是前 PF_SAMPLES 个样本，长段/长按不能拿它冒充整段。 */
static uint32_t s_niv_total, s_niv_long, s_iv_max_all;
/* 松手→落定：motion_finish 由 UI 通过 fnos_perf_note_motion_settle 打点。
   frame_arm 80ms 之后才落定的动画，iv 窗口看不到；这两个数独立记整段。 */
static uint32_t s_rel_settle_us, s_rel_frames;
static int s_rel_page = -1;
static int s_rel_dir = 2;   /* 被接受落定的方向；没有落定时报告回退到整轮最后一次方向 */
static bool s_rel_settled;
/* 松手→落定这一段自己的帧间隔（frame_arm 的 iv 只看 80ms，落定帧与释放长帧都在窗外）。
   每次松手重新累计、只到第一个被接受的落定，因此不是整轮最大值（整轮最大值是 iv_max_us）。 */
static uint32_t s_rel_iv[PF_SAMPLES], s_rel_iv_max, s_rel_nlong;
static int s_nrel_iv;
static uint32_t s_final_us;    /* 落定 → 下一帧 READY：最终画面真正呈现的时刻 */
static uint32_t s_final_rs_us; /* 落定 → 该帧 RENDER_START（处理器内耗时） */
static uint32_t s_fin_skip;
static uint32_t s_fin_fix_us, s_fin_layout_us, s_fin_refresh_us, s_prep_page_us, s_prep_refresh_us;
static uint32_t s_ref_health_us, s_ref_shell_us, s_ref_body_us;
static uint32_t s_p1_vol_us, s_p1_raid_us, s_p1_disk_us, s_p1_inv_us;
static uint32_t s_rel_seq; static bool s_rel_new;
static uint32_t s_pair_us;
static uint32_t s_ref_prep_us;
static uint32_t s_pool_rl_mask;
/* 布局归因：3 个时序窗口 × 3 个布局点，各记调用数/总耗时/最大耗时/真实重排数。 */
typedef struct { uint32_t n, us, max_us, relayouts; } pf_lay_t;
static pf_lay_t s_lay[4][3];
static int s_lay_win;
static uint32_t s_shape_mask, s_shape_hits;
/* 原生自测高开销与整屏脏位观测 */
static uint32_t s_self_n, s_self_chg, s_self_us, s_scr_calls, s_scr_dirty;
static int64_t s_finish_us;
static bool s_final_pending;
/* motion_prepare 会先起动画再回头刷数据；用代数把"本次手势的动画"和进页前的 settle 分开。 */
static uint32_t s_motion_gen;
static uint32_t s_tick[PF_SAMPLES];  /* ui_tick 总耗时（数据刷新 + 图表 + 行重建） */
static int s_np2f, s_nr2f, s_niv, s_ntick;

/* 帧的三段分解（按下→首帧 与 拖动帧间隔 都只是一个总数，拆开才知道该优化哪一段）：
   p2rs = 按下 → RENDER_START（等采样/等刷新周期/等上一次 flush）
   rs2rr= RENDER_START → RENDER_READY（真正的绘制 + flush 提交）
   rr2rs= 上一帧 RENDER_READY → 下一帧 RENDER_START（帧间空档：刷新定时器周期 + flush 等待） */
static uint32_t s_p2rs[PF_SAMPLES], s_rs2rr[PF_SAMPLES], s_rr2rs[PF_SAMPLES];
static int s_np2rs, s_nrs2rr, s_nrr2rs;

static int64_t s_last_frame_us;
static int64_t s_frame_start_us, s_last_ready_us;
static bool s_have_ready;

/* LVGL owns frame windows; native software drawing can run on another task. */
static TaskHandle_t s_owner;
static portMUX_TYPE s_stage_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_stage_frame;
static struct {
    uint64_t frame_us, flush_us, flush_wait_us;
    uint32_t frames, worker_calls;
    TaskHandle_t worker;
    int64_t flush_start, wait_start;
    struct { uint64_t us; uint32_t calls, max_us; } stage[FNOS_PERF_STAGE_COUNT];
} s_parts;

/* 帧间阶段（动画 exec / ui_tick）在 RENDER_START..READY 门外，用独立累计器，
   不与 flush 子阶段或绘制任务相加。 */
typedef struct {
    uint64_t us[FNOS_PERF_STAGE_COUNT];
    uint32_t calls[FNOS_PERF_STAGE_COUNT];
    uint32_t max_us[FNOS_PERF_STAGE_COUNT];
} pf_ui_acc_t;
static pf_ui_acc_t s_ui;
static bool s_probe_mode;   /* bench probe：只测探针自身，不污染 s_ui */
static uint32_t s_chart_frames;   /* frame_arm 帧中至少绘过一次 chart 的帧数 */
static bool s_chart_drew;         /* 本帧已发生 chart 绘制；RENDER_START 清零 */

static uint32_t s_tick_motion[PF_SAMPLES];   /* 页面运动期的 ui_tick */
static uint32_t s_tick_full[PF_SAMPLES];    /* 整刷期 ui_tick（motion_busy()==false；含空 tick） */
static int s_ntick_motion, s_ntick_full;

static void pf_stage_gate(bool open)
{
    portENTER_CRITICAL(&s_stage_lock);
    s_stage_frame = open;
    portEXIT_CRITICAL(&s_stage_lock);
}

void fnos_perf_note_stage(fnos_perf_stage_t stage, uint32_t us)
{
    if ((unsigned)stage >= FNOS_PERF_STAGE_COUNT) return;
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    bool owner = current == s_owner;
    if (!owner && stage < FNOS_PERF_BLEND) return;
    /* Frame close synchronizes with the last worker update before report/reset. */
    portENTER_CRITICAL(&s_stage_lock);
    if (s_stage_frame) {
        s_parts.stage[stage].us += us;
        s_parts.stage[stage].calls++;
        if (!owner) { s_parts.worker_calls++; s_parts.worker = current; }
        if (us > s_parts.stage[stage].max_us) s_parts.stage[stage].max_us = us;
    }
    portEXIT_CRITICAL(&s_stage_lock);
}

void fnos_perf_note_ui_stage(fnos_perf_stage_t stage, uint32_t us)
{
    if (stage < FNOS_PERF_PAGE_PAINT || stage >= FNOS_PERF_STAGE_COUNT) return;
    /* 整个 bench 运行窗口都算，不要求 frame_arm：换页布局与吸附发生在手势帧之外；
       分母按阶段取（活动帧 / 调用次数 / 带图帧），chart 的帧门在 chart 回调里。 */
    if (!s_probe_mode && !s_run.active) return;
    s_ui.us[stage] += us;
    s_ui.calls[stage]++;
    if (us > s_ui.max_us[stage]) s_ui.max_us[stage] = us;
}

static void pf_parts_cb(lv_event_t *e)
{
    if (!s_run.active || !s_run.frame_arm || !s_frame_start_us) return;
    int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(e)) {
    case LV_EVENT_FLUSH_START: s_parts.flush_start = now; break;
    case LV_EVENT_FLUSH_FINISH:
        if (s_parts.flush_start) s_parts.flush_us += now - s_parts.flush_start;
        s_parts.flush_start = 0;
        break;
    case LV_EVENT_FLUSH_WAIT_START: s_parts.wait_start = now; break;
    case LV_EVENT_FLUSH_WAIT_FINISH:
        if (s_parts.wait_start) s_parts.flush_wait_us += now - s_parts.wait_start;
        s_parts.wait_start = 0;
        break;
    default: break;
    }
}

/* ---------------------------------------------------------------- 统计 */

static int pf_cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static uint32_t pf_pct(const uint32_t *src, int n, int pct)
{
    if (n <= 0) return 0;
    static uint32_t tmp[PF_SAMPLES];
    memcpy(tmp, src, (size_t)n * sizeof tmp[0]);
    qsort(tmp, (size_t)n, sizeof tmp[0], pf_cmp);
    return tmp[(int)((int64_t)(n - 1) * pct / 100)];
}

static uint32_t pf_max(const uint32_t *src, int n)
{
    uint32_t m = 0;
    for (int i = 0; i < n; i++) if (src[i] > m) m = src[i];
    return m;
}

static bool pf_push(uint32_t *buf, int *n, uint32_t v)
{
    if (*n >= PF_SAMPLES) return false;
    buf[(*n)++] = v;
    return true;
}

/* ----------------------------------------------------- chart 绘制追踪 */

#define PF_CHARTS 8
static struct { int64_t start; } s_chart[PF_CHARTS];
static int s_nchart;

static void pf_chart_begin(lv_event_t *e)
{
    if (!s_run.active) return;
    if (s_run.frame_arm) s_chart_drew = true;
    *(int64_t *)lv_event_get_user_data(e) = esp_timer_get_time();
}

static void pf_chart_end(lv_event_t *e)
{
    int64_t *start = lv_event_get_user_data(e);
    if (!s_run.active || !*start) return;
    int64_t dt = esp_timer_get_time() - *start;
    *start = 0;
    /* chart_frames 只数 frame_arm 帧，所以绘制耗时也只收同一窗口。 */
    if (s_run.frame_arm) fnos_perf_note_ui_stage(FNOS_PERF_CHART_DRAW, (uint32_t)dt);
}

void fnos_perf_track_chart(void *chart)
{
    if (!chart) return;
    /* UI 重建（分配失败重试）会把旧 chart 全销毁；槽满即视作新一代，重头挂。 */
    if (s_nchart >= PF_CHARTS) s_nchart = 0;
    lv_obj_t *obj = chart;
    int64_t *start = &s_chart[s_nchart++].start;
    lv_obj_add_event_cb(obj, pf_chart_begin, LV_EVENT_DRAW_MAIN_BEGIN, start);
    lv_obj_add_event_cb(obj, pf_chart_end, LV_EVENT_DRAW_MAIN_END, start);
}

/* ------------------------------------------------------- 各任务 CPU 占比 */

typedef struct {
    char name[configMAX_TASK_NAME_LEN];
    uint32_t run;
    TaskHandle_t handle;
} pf_task_t;

static pf_task_t s_cpu0[PF_TASKS], s_cpu1[PF_TASKS];
static int s_ncpu0, s_ncpu1;

static int pf_snap_tasks(pf_task_t *out)
{
    static TaskStatus_t st[PF_TASKS];
    UBaseType_t n = uxTaskGetSystemState(st, PF_TASKS, NULL);
    int k = (int)n;
    if (k > PF_TASKS) k = PF_TASKS;
    for (int i = 0; i < k; i++) {
        snprintf(out[i].name, sizeof out[i].name, "%s", st[i].pcTaskName);
        out[i].run = (uint32_t)st[i].ulRunTimeCounter;
        out[i].handle = st[i].xHandle;
    }
    return k;
}

/* 双核：把「各任务 delta 之和」当 100%，于是 lvgl/idle 的占比可以跨核比较。 */
static void pf_cpu_delta(uint32_t *total, uint32_t *lvgl, uint32_t *idle, uint32_t *worker)
{
    uint32_t t = 0, l = 0, i2 = 0, w = 0;
    for (int i = 0; i < s_ncpu1; i++) {
        for (int j = 0; j < s_ncpu0; j++) {
            if (s_cpu0[j].handle != s_cpu1[i].handle) continue;
            uint32_t d = s_cpu1[i].run - s_cpu0[j].run;   /* 计数器回绕概率可忽略 */
            t += d;
            if (strcmp(s_cpu1[i].name, "lvgl") == 0) l += d;
            if (strncmp(s_cpu1[i].name, "IDLE", 4) == 0) i2 += d;
            if (s_cpu1[i].handle == s_parts.worker) w += d;
            break;
        }
    }
    *total = t; *lvgl = l; *idle = i2; *worker = w;
}

/* ------------------------------------------------------------ 合成指针 */

static struct {
    lv_indev_t *indev;
    lv_indev_read_cb_t cb;
} s_real[PF_MAX_INDEVS];
static int s_nreal;

static void pf_detach(void);

static void pf_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    /* 先照常问真实面板：adapter 靠这次调用做触摸握手与记账，跳过它就改变了被测对象。 */
    for (int i = 0; i < s_nreal; i++) {
        if (s_real[i].indev == indev && s_real[i].cb) { s_real[i].cb(indev, data); break; }
    }
    if (s_release_pending) {
        /* 取消发生在按住期间：先把 RELEASED 送到 LVGL 再摘回调，避免旧合成按下悬空。 */
        data->point.x = s_run.x;
        data->point.y = s_run.y;
        data->state   = LV_INDEV_STATE_RELEASED;
        s_release_pending = false;
        pf_detach();
        return;
    }
    if (!s_run.active || s_run.kind == PF_IDLE) return;

    data->point.x = s_run.x;
    data->point.y = s_run.y;
    data->state   = s_run.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    if (s_run.pressed && !s_run.press_seen) {
        /* P1 的 t0 必须记在"LVGL 真的看到按下"这一刻，不能记在脚本置位那刻 ——
           否则 15ms 的 indev 采样周期会被算进 UI 的账。 */
        s_run.press_seen = true;
        s_run.press_us   = esp_timer_get_time();
        s_run.wait_first = true;
        s_run.wait_p2rs  = true;
        s_run.frame_arm  = true;
    } else if (!s_run.pressed && s_run.press_seen && !s_run.rel_seen) {
        s_run.rel_seen   = true;
        s_run.release_us = esp_timer_get_time();
        s_run.rel_gen    = s_motion_gen;
        s_run.rel_dir    = s_run.dir;   /* 落定可能晚于下一次翻向，方向必须随松手记 */
        if (!s_rel_settled) {
            /* 新一次松手重新开窗：迟到落定前，上一轮帧数/最大间隔不得混进本轮。 */
            s_run.rel_frames = 0;
            s_rel_iv_max = 0;
            s_rel_nlong = 0;
            s_nrel_iv = 0;
        }
        s_run.wait_rel   = true;
    }
}

static void pf_detach(void)
{
    for (int i = 0; i < s_nreal; i++) {
        if (s_real[i].indev) lv_indev_set_read_cb(s_real[i].indev, s_real[i].cb);
    }
    s_nreal = 0;
}

/* ---------------------------------------------------------------- 帧事件 */

static void pf_start_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RENDER_START) return;
    LV_PROFILER_BEGIN_TAG("frame");   /* profiler：一帧的顶，配对在 RENDER_READY */
    int64_t now = esp_timer_get_time();
    s_frame_start_us = now;
    pf_stage_gate(s_run.active && s_run.frame_arm);
    if (!s_run.active) { s_last_ready_us = 0; s_have_ready = false; return; }
    if (s_final_pending && !s_final_rs_us) s_final_rs_us = (uint32_t)(now - s_finish_us);
    if (s_run.frame_arm) s_chart_drew = false;
    if (s_run.frame_arm && s_have_ready && s_last_ready_us) {
        pf_push(s_rr2rs, &s_nrr2rs, (uint32_t)(now - s_last_ready_us));   /* 与 iv 同一批样本 */
    }
    if (s_run.wait_p2rs) { s_run.wait_p2rs = false; pf_push(s_p2rs, &s_np2rs, (uint32_t)(now - s_run.press_us)); }
}

static void pf_render_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RENDER_READY) return;
    LV_PROFILER_END_TAG("frame");
    int64_t now = esp_timer_get_time();
    pf_stage_gate(false);
    if (!s_run.active) { s_last_frame_us = now; s_frame_start_us = 0; return; }

    s_run.frames++;
    if (s_run.rel_seen && !s_rel_settled) {
        s_run.rel_frames++;
        if (s_last_frame_us) {          /* 释放后动画自己的帧间隔，含 80ms 窗外的落定帧 */
            uint32_t d = (uint32_t)(now - s_last_frame_us);
            if (d > s_rel_iv_max) s_rel_iv_max = d;
            if (d >= PF_LONG_FRAME_US) s_rel_nlong++;
            pf_push(s_rel_iv, &s_nrel_iv, d);
        }
    }
    if (s_final_pending) { s_final_us = (uint32_t)(now - s_finish_us); s_final_pending = false; }
    if (s_run.wait_first) { s_run.wait_first = false; pf_push(s_p2f, &s_np2f, (uint32_t)(now - s_run.press_us)); }
    if (s_run.wait_rel)   { s_run.wait_rel   = false; pf_push(s_r2f, &s_nr2f, (uint32_t)(now - s_run.release_us)); }
    if (s_run.frame_arm && s_last_frame_us) {
        uint32_t iv = (uint32_t)(now - s_last_frame_us);
        s_niv_total++;
        if (iv >= PF_LONG_FRAME_US) s_niv_long++;
        if (iv > s_iv_max_all) s_iv_max_all = iv;
        pf_push(s_iv, &s_niv, iv);
    }
    if (s_frame_start_us) {
        if (s_run.frame_arm) {
            s_parts.frames++;
            s_parts.frame_us += now - s_frame_start_us;
        }
        if (s_run.frame_arm) pf_push(s_rs2rr, &s_nrs2rr, (uint32_t)(now - s_frame_start_us));
        s_frame_start_us = 0;
    }
    if (s_run.frame_arm && s_chart_drew) s_chart_frames++;
    s_last_frame_us = now;
    s_last_ready_us = now;
    s_have_ready    = true;
}

/* ---------------------------------------------------------------- 报告 */

static bool s_reported;   /* 本轮是否已经落过 [bench] 行（兜底上报用） */

static const char *pf_kind_name(void)
{
    return s_run.kind == PF_IDLE ? "idle" : s_run.kind == PF_SWIPE ? "swipe"
         : s_run.kind == PF_DRAG ? "drag" : "tap";
}

/* [bench] 的 dir：有被接受的落定时用它那次松手的方向；没有落定时保持旧口径
   （drag 取整轮最后一次方向，其它手势 2）。 */
static int pf_report_dir(void)
{
    if (s_run.kind != PF_DRAG) return 2;
    int dir = s_rel_settled ? s_rel_dir : s_run.dir;
    return dir < 0 ? 0 : 1;
}

static void pf_report(void)
{
    uint32_t wall_ms = (uint32_t)((esp_timer_get_time() - s_run.run_us) / 1000);
    s_ncpu1 = pf_snap_tasks(s_cpu1);
    uint32_t total = 0, lvgl = 0, idle = 0, worker = 0;
    pf_cpu_delta(&total, &lvgl, &idle, &worker);
    uint32_t iv50 = pf_pct(s_iv, s_niv, 50);
    uint32_t iv99 = pf_pct(s_iv, s_niv, 99);
    uint32_t iv_long_n = s_niv_long;

    /* Nested probes are subsets of flush_us, not additional frame time. */
    printf("[bench_parts] kind=%s page=%d frames=%" PRIu32 " frame_us=%llu flush_us=%llu flush_wait_us=%llu",
           pf_kind_name(), s_run.page, s_parts.frames, (unsigned long long)s_parts.frame_us,
           (unsigned long long)s_parts.flush_us, (unsigned long long)s_parts.flush_wait_us);
    static const char *const names[] = {"ppa", "dma2d", "submit", "freebuf"};
    for (unsigned i = 0; i < FNOS_PERF_TASK_CREATE; i++)
        printf(" %s_us=%llu %s_n=%" PRIu32 " %s_max_us=%" PRIu32, names[i],
               (unsigned long long)s_parts.stage[i].us, names[i], s_parts.stage[i].calls,
               names[i], s_parts.stage[i].max_us);
    printf("\n");

    /* Text contains blend time; these are inclusive probes, never summed.
       With the draw probes compiled out these counters stay zero, so do not
       print the row at all: a missing row means "not measured", never 0. */
#ifdef CONFIG_FNOS_UI_PERF_DRAW
    printf("[bench_draw] kind=%s page=%d frames=%" PRIu32 " units=%" PRIu32 " worker_n=%" PRIu32 " cpu_worker=%.1f",
           pf_kind_name(), s_run.page, s_parts.frames, (uint32_t)lv_draw_get_unit_count(), s_parts.worker_calls,
           total ? 100.0 * worker / total : 0.0);
    static const char *const draw_names[] = {"create", "pick", "blend", "text"};
    for (unsigned i = FNOS_PERF_TASK_CREATE; i <= FNOS_PERF_TEXT; i++) {
        const char *name = draw_names[i - FNOS_PERF_TASK_CREATE];
        printf(" %s_us=%llu %s_n=%" PRIu32 " %s_max_us=%" PRIu32, name,
               (unsigned long long)s_parts.stage[i].us, name, s_parts.stage[i].calls,
               name, s_parts.stage[i].max_us);
    }
    printf("\n");
#else
    (void)total;
    (void)worker;
#endif

    /* UI 阶段：page_paint 用活动帧数，page_layout 用调用次数，chart 用带图帧数。 */
    static const char *const ui_names[] = {"page_paint", "page_layout", "chart_draw"};
    printf("[bench_ui] kind=%s page=%d frames=%" PRIu32 " gesture_frames=%" PRIu32 " gesture_fps=%" PRIu32 " chart_frames=%" PRIu32 " draw_stages=%d",
           pf_kind_name(), s_run.page, s_run.frames, s_parts.frames, uk_trend_gesture_fps(), s_chart_frames,
#ifdef CONFIG_FNOS_UI_PERF_DRAW
           1
#else
           0
#endif
           );
    for (unsigned i = FNOS_PERF_PAGE_PAINT; i <= FNOS_PERF_CHART_DRAW; i++) {
        const char *name = ui_names[i - FNOS_PERF_PAGE_PAINT];
        printf(" %s_us=%llu %s_n=%" PRIu32 " %s_max_us=%" PRIu32, name,
               (unsigned long long)s_ui.us[i], name, s_ui.calls[i], name, s_ui.max_us[i]);
    }
    printf("\n");

    printf("[bench_lay] kind=%s page=%d", pf_kind_name(), s_run.page);
    for (int w = 1; w <= 3; w++)
        for (int k = 0; k < 3; k++)
            printf(" w%d_k%d_n=%" PRIu32 " us=%" PRIu32 " max=%" PRIu32 " rl=%" PRIu32,
                   w, k, s_lay[w][k].n, s_lay[w][k].us, s_lay[w][k].max_us, s_lay[w][k].relayouts);
    printf(" pool_rl_mask=%" PRIu32 " shape_hits=%" PRIu32 " shape_mask=%" PRIu32 " selfh_n=%" PRIu32 " selfh_chg=%" PRIu32
           " selfh_us=%" PRIu32 " scr_n=%" PRIu32 " scr_dirty=%" PRIu32 "\n",
           s_pool_rl_mask, s_shape_hits, s_shape_mask, s_self_n, s_self_chg, s_self_us, s_scr_calls, s_scr_dirty);

    printf("[bench_tick] kind=%s page=%d motion_n=%d motion_p50_us=%" PRIu32 " motion_p95_us=%" PRIu32 " motion_max_us=%" PRIu32
           " full_n=%d full_p50_us=%" PRIu32 " full_p95_us=%" PRIu32 " full_max_us=%" PRIu32 "\n",
           pf_kind_name(), s_run.page,
           s_ntick_motion, pf_pct(s_tick_motion, s_ntick_motion, 50),
           pf_pct(s_tick_motion, s_ntick_motion, 95), pf_max(s_tick_motion, s_ntick_motion),
           s_ntick_full, pf_pct(s_tick_full, s_ntick_full, 50),
           pf_pct(s_tick_full, s_ntick_full, 95), pf_max(s_tick_full, s_ntick_full));

    printf("[bench] kind=%s page=%d cycles=%d misses=%d p2f_n=%d p2f_p50_us=%" PRIu32 " p2f_p95_us=%" PRIu32 " p2f_max_us=%" PRIu32
           " r2f_n=%d r2f_p50_us=%" PRIu32 " r2f_p95_us=%" PRIu32 " r2f_max_us=%" PRIu32
           " iv_n=%" PRIu32 " iv_sampled=%d iv_capped=%d iv_p50_us=%" PRIu32 " iv_p95_us=%" PRIu32 " iv_max_us=%" PRIu32 " frames=%" PRIu32 " wall_ms=%" PRIu32 " fps_avg=%.1f fps_p50=%.1f"
           " p2rs_n=%d p2rs_p50_us=%" PRIu32 " rs2rr_n=%d rs2rr_p50_us=%" PRIu32 " rs2rr_p95_us=%" PRIu32
           " rr2rs_n=%d rr2rs_p50_us=%" PRIu32 " rr2rs_p95_us=%" PRIu32
           " tick_n=%d tick_p50_us=%" PRIu32 " tick_p95_us=%" PRIu32 " tick_max_us=%" PRIu32 " cpu_lvgl=%.1f cpu_idle=%.1f iv_p99_us=%" PRIu32 " iv_long_n=%" PRIu32 " drag_hold_ms=%" PRIu32 " dir=%u span=%" PRIu32 " rel_settle_us=%" PRIu32 " rel_frames=%" PRIu32
           " rel_page=%d rel_iv_max_us=%" PRIu32 " rel_long_n=%" PRIu32 " final_us=%" PRIu32 " final_rs_us=%" PRIu32 " fin_skip=%" PRIu32
           " fin_fix_us=%" PRIu32 " fin_layout_us=%" PRIu32 " fin_refresh_us=%" PRIu32 " prep_page_us=%" PRIu32 " prep_refresh_us=%" PRIu32
           " ref_health_us=%" PRIu32 " ref_shell_us=%" PRIu32 " ref_body_us=%" PRIu32 " pair_us=%" PRIu32 " ref_prep_us=%" PRIu32 " p1_vol_us=%" PRIu32 " p1_raid_us=%" PRIu32 " p1_disk_us=%" PRIu32 " p1_inv_us=%" PRIu32 " rel_seq=%" PRIu32 " rel_new=%d\n",
           pf_kind_name(), s_run.page, s_run.cycles, s_run.misses,
           s_np2f, pf_pct(s_p2f, s_np2f, 50), pf_pct(s_p2f, s_np2f, 95), pf_max(s_p2f, s_np2f),
           s_nr2f, pf_pct(s_r2f, s_nr2f, 50), pf_pct(s_r2f, s_nr2f, 95), pf_max(s_r2f, s_nr2f),
           s_niv_total, s_niv, s_niv < (int)s_niv_total, iv50, pf_pct(s_iv, s_niv, 95), s_iv_max_all,
           s_run.frames, wall_ms, wall_ms ? 1000.0 * s_run.frames / wall_ms : 0.0,
           iv50 ? 1000000.0 / iv50 : 0.0,
           s_np2rs, pf_pct(s_p2rs, s_np2rs, 50),
           s_nrs2rr, pf_pct(s_rs2rr, s_nrs2rr, 50), pf_pct(s_rs2rr, s_nrs2rr, 95),
           s_nrr2rs, pf_pct(s_rr2rs, s_nrr2rs, 50), pf_pct(s_rr2rs, s_nrr2rs, 95),
           s_ntick, pf_pct(s_tick, s_ntick, 50), pf_pct(s_tick, s_ntick, 95), pf_max(s_tick, s_ntick),
           total ? 100.0 * lvgl / total : 0.0, total ? 100.0 * idle / total : 0.0, iv99, iv_long_n, s_run.hold_ms,
           (unsigned)pf_report_dir(), (uint32_t)s_run.span,
           s_rel_settle_us, s_rel_frames, s_rel_page, s_rel_iv_max, s_rel_nlong, s_final_us, s_final_rs_us, s_fin_skip,
           s_fin_fix_us, s_fin_layout_us, s_fin_refresh_us, s_prep_page_us, s_prep_refresh_us,
           s_ref_health_us, s_ref_shell_us, s_ref_body_us, s_pair_us, s_ref_prep_us,
           s_p1_vol_us, s_p1_raid_us, s_p1_disk_us, s_p1_inv_us, s_rel_seq, s_rel_new ? 1 : 0);
    fflush(stdout);
    s_reported = true;
}

static lv_timer_t *s_script_timer;

static void pf_stop(void)
{
    /* 兜底：任何提前退出（arm 失败、超时、被新命令顶掉）都必须留下一行 [bench]，
       否则 tools/perf_bench.py 只能干等到 180 s 超时，看不出断在哪一步
       —— 2026-10-10 复现真基线时踩过：换回旧 fnos_ui.c 后 fnos_perf_init() 没人调用
       （调用点就在 UI 文件里），定时器从未创建，CLI 只回 "已排队" 就再无输出。 */
    if (s_run.active && !s_reported) pf_report();
    s_run.active = false;
    if (s_run.pressed) {
        /* 让下一次 indev 读先发合成的 RELEASED；此后才摘回调。 */
        s_run.pressed = false;
        s_release_pending = true;
    } else if (!s_release_pending) {
        pf_detach();
    }
    if (s_script_timer) lv_timer_pause(s_script_timer);
}

/* ------------------------------------------------------------ 脚本状态机 */

/* 以按下时刻为原点的墙钟位置：渲染慢时直接跳过中间位置，不在每个 timer tick
   上累积步数（旧实现把 2 s 拖成 7–10 s，且位置与时间脱钩）。 */
static int32_t pf_drag_pos(int32_t x0, int32_t span, int dir, uint32_t elapsed_ms, uint32_t drag_ms)
{
    if (drag_ms == 0 || elapsed_ms >= drag_ms) return x0 + dir * span;
    return x0 + (int32_t)((int64_t)dir * span * elapsed_ms / drag_ms);
}

static void pf_arm(int64_t now)
{
    /* Every cycle needs fresh sample/response latches. Keeping press_seen or
       step from the previous drag skips its timing and can release immediately. */
    s_run.press_seen = s_run.rel_seen = false;
    s_run.wait_first = s_run.wait_rel = s_run.wait_p2rs = false;
    s_run.frame_arm = false;
    s_run.step = 0;
    s_run.hold_ms = 0;
    s_run.press_us = s_run.release_us = 0;
    s_last_frame_us = s_last_ready_us = s_frame_start_us = 0;
    s_have_ready = false;
    if (s_run.kind == PF_SWIPE || s_run.kind == PF_DRAG) {
        /* 每次起滑都从整屏 72%/28% 的滑动起点；drag 方向跨轮左右交替，不靠固定坐标长按。 */
        if (!fnos_ui_swipe_point(s_run.dir, &s_run.x0, &s_run.y0)) {
            printf("[bench] arm failed: swipe point unavailable (page=%d dir=%d)\n", s_run.page, s_run.dir);
            fflush(stdout);
            s_run.misses++; pf_stop(); return;
        }
        if (s_run.px <= 0) {
            lv_display_t *d = lv_display_get_default();
            s_run.px = d ? lv_display_get_horizontal_resolution(d) / 2 : 512;
        }
        if (s_run.px > PF_DRAG_MAX_SPAN) s_run.px = PF_DRAG_MAX_SPAN;
        s_run.span  = s_run.px;
        s_run.steps = s_run.kind == PF_SWIPE ? PF_DRAG_STEPS : 0;
        s_run.x = s_run.x0;
        s_run.y = s_run.y0;
    } else {
        /* 点按导航键中心：按下有反馈、松手换页，两个时延都在同一次手势里量到。 */
        if (!fnos_ui_nav_center(s_run.page, &s_run.x0, &s_run.y0)) {
            printf("[bench] arm failed: nav center unavailable (page=%d)\n", s_run.page);
            fflush(stdout);
            s_run.misses++; pf_stop(); return;
        }
        s_run.x = s_run.x0;
        s_run.y = s_run.y0;
        s_run.steps = 0;
    }
    s_run.pressed = true;
    s_run.phase = (s_run.kind == PF_SWIPE || s_run.kind == PF_DRAG) ? PH_DRAG : PH_HOLD;
    s_run.phase_us = now;
}

static void pf_next_cycle(int64_t now)
{
    s_run.done++;
    if (s_run.done >= s_run.cycles) { pf_report(); pf_stop(); return; }

    /* 点按必须先把页面切走：只点当前页的导航键，页面没变化 → 首帧可能提前出现，
       "按下→首帧"就退化成 indev 采样周期，数字虚好。每轮先落到别页再点回目标页，
       于是每轮都带一次真实的整页重建 + 进页动画。 */
    if (s_run.kind == PF_TAP) {
        int alt = (s_run.page + 1 + s_run.done) % FNOS_UI_PAGE_COUNT;
        if (alt == s_run.page) alt = (alt + 1) % FNOS_UI_PAGE_COUNT;
        fnos_ui_set_page(alt);
    } else if (s_run.kind == PF_SWIPE || s_run.kind == PF_DRAG) {
        fnos_ui_set_page(s_run.page);   /* 滑完停在别页：先回到目标页，起点一致 */
        s_run.dir = -s_run.dir;
    }
    s_run.phase = PH_SETTLE;
    s_run.phase_us = now;
}

static void pf_script_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_run.active) return;
    int64_t now = esp_timer_get_time();
    uint32_t el = (uint32_t)((now - s_run.phase_us) / 1000);

    switch (s_run.phase) {
    case PH_SETTLE:
        if (s_run.kind == PF_IDLE) {
            if (el >= (uint32_t)s_run.idle_ms) { pf_report(); pf_stop(); }
        } else if (el >= PF_SETTLE_MS) {
            pf_arm(now);
        }
        break;

    case PH_HOLD:
        if (!s_run.press_seen) {                       /* 等 indev 上报按下 */
            if (el > PF_ARM_TIMEOUT_MS) { s_run.misses++; s_run.pressed = false; pf_next_cycle(now); }
            break;
        }
        if (el >= PF_TAP_HOLD_MS) { s_run.pressed = false; s_run.phase = PH_RELEASE; s_run.phase_us = now; }
        break;

    case PH_DRAG:
        if (!s_run.press_seen && el > PF_ARM_TIMEOUT_MS) {
            s_run.misses++;
            s_run.pressed = false;
            pf_next_cycle(now);
            break;
        }
        if (s_run.press_seen) {
            if (s_run.kind == PF_DRAG) {
                uint32_t elapsed_ms = (uint32_t)((now - s_run.press_us) / 1000);
                s_run.x = pf_drag_pos(s_run.x0, s_run.span, s_run.dir, elapsed_ms, (uint32_t)s_run.drag_ms);
                if (elapsed_ms >= (uint32_t)s_run.drag_ms) {
                    s_run.hold_ms = elapsed_ms;
                    s_run.pressed = false;
                    s_run.phase = PH_RELEASE;
                    s_run.phase_us = now;
                }
            } else {
                s_run.step++;
                s_run.x = s_run.x0 + (int32_t)((int64_t)s_run.dir * s_run.span * s_run.step / s_run.steps);
                if (s_run.step >= s_run.steps) { s_run.pressed = false; s_run.phase = PH_RELEASE; s_run.phase_us = now; }
            }
        }
        break;

    case PH_RELEASE:
        /* tap 的转场在松开后才开始；窗口留到落定附近，swipe 仍按原 80 ms。 */
        if (el >= (s_run.kind == PF_TAP ? PF_TAP_RELEASE_MS : PF_RELEASE_MS)) {
            s_run.frame_arm = false;
            s_run.phase = PH_TAIL;
            s_run.phase_us = now;
        }
        break;

    case PH_TAIL:
        if (el >= PF_TAIL_MS) pf_next_cycle(now);
        break;

    default:
        break;
    }
}

static void pf_start(void)
{
    /* Initialization can run on main/ui_retry; this callback runs on LVGL. */
    s_owner = xTaskGetCurrentTaskHandle();
    pf_stage_gate(false);
    memset(s_p2f, 0, sizeof s_p2f);
    memset(s_r2f, 0, sizeof s_r2f);
    memset(s_iv, 0, sizeof s_iv);
    memset(s_tick, 0, sizeof s_tick);
    memset(s_tick_motion, 0, sizeof s_tick_motion);
    memset(s_tick_full, 0, sizeof s_tick_full);
    s_np2f = s_nr2f = s_niv = s_ntick = 0;
    s_niv_total = s_niv_long = s_iv_max_all = 0;
    s_ntick_motion = s_ntick_full = 0;
    s_np2rs = s_nrs2rr = s_nrr2rs = 0;
    s_frame_start_us = 0;
    s_last_ready_us = 0;
    s_have_ready = false;
    s_last_frame_us = 0;
    s_reported = false;
    s_run.rel_frames = 0;
    s_run.rel_gen = 0;
    s_rel_settle_us = 0;
    s_rel_frames = 0;
    s_rel_settled = false;
    s_rel_page = -1;
    s_rel_dir = 2;
    s_nrel_iv = 0;
    s_rel_iv_max = 0;
    s_rel_nlong = 0;
    s_final_us = 0;
    s_final_rs_us = 0;
    s_fin_skip = 0;
    s_ref_health_us = s_ref_shell_us = s_ref_body_us = 0;
    s_p1_vol_us = s_p1_raid_us = s_p1_disk_us = s_p1_inv_us = 0;
    s_rel_seq = 0; s_rel_new = false;
    s_pair_us = 0;
    s_ref_prep_us = 0;
    s_final_pending = false;
    s_fin_fix_us = s_fin_layout_us = s_fin_refresh_us = s_prep_page_us = s_prep_refresh_us = 0;
    s_pool_rl_mask = 0;
    memset(s_lay, 0, sizeof s_lay);
    s_lay_win = 0;
    s_shape_mask = s_shape_hits = 0;
    s_self_n = s_self_chg = s_self_us = s_scr_calls = s_scr_dirty = 0;
    memset(&s_parts, 0, sizeof s_parts);
    memset(&s_ui, 0, sizeof s_ui);
    s_chart_frames = 0;
    s_chart_drew = false;

    pf_detach();   /* 上一轮留下的挂载先摘干净，否则会把 pf_read_cb 当成"真实回调"套娃 */
    s_release_pending = false;
    s_nreal = 0;
    for (lv_indev_t *i = lv_indev_get_next(NULL); i && s_nreal < PF_MAX_INDEVS; i = lv_indev_get_next(i)) {
        if (lv_indev_get_type(i) != LV_INDEV_TYPE_POINTER) continue;
        s_real[s_nreal].indev = i;
        s_real[s_nreal].cb    = lv_indev_get_read_cb(i);
        lv_indev_set_read_cb(i, pf_read_cb);
        s_nreal++;
    }
    if (s_nreal == 0) { printf("[bench] err=no-pointer-indev\n"); fflush(stdout); return; }

    int64_t now = esp_timer_get_time();
    pf_kind_t kind = s_req.kind;
    memset(&s_run, 0, sizeof s_run);
    s_run.active = true;
    s_run.kind   = kind;
    s_run.page   = s_req.page;
    s_run.px     = s_req.px;
    s_run.dir    = kind == PF_DRAG ? s_drag_dir : -1;
    if (kind == PF_DRAG) s_drag_dir = -s_drag_dir;   /* 连续 A 段左右交替，两端都覆盖 */
    s_run.cycles = kind == PF_IDLE ? 1 : (s_req.cycles > 0 ? s_req.cycles : 3);
    s_run.idle_ms = (s_req.seconds > 0 ? s_req.seconds : 5) * 1000;
    s_run.drag_ms = s_req.drag_ms > 0 ? s_req.drag_ms : 2000;
    s_run.run_us = now;
    s_ncpu0 = pf_snap_tasks(s_cpu0);

    if (s_run.kind == PF_IDLE) {
        s_run.frame_arm = true;
        /* 稳态：停在目标页不动，只收帧间隔 / ui_tick / CPU。 */
        fnos_ui_set_page(s_run.page);
        s_run.phase = PH_SETTLE;
        s_run.phase_us = now;
    } else {
        s_run.phase = PH_SETTLE;
        s_run.phase_us = now;
        /* 首轮也先落到别页：每次点按都必须真的切回目标页。 */
        fnos_ui_set_page(s_run.kind == PF_TAP
                         ? (s_run.page + 1) % FNOS_UI_PAGE_COUNT : s_run.page);
    }
    if (s_script_timer) lv_timer_resume(s_script_timer);
    ESP_LOGI(TAG, "bench start kind=%d page=%d cycles=%d idle_ms=%d", (int)kind, s_run.page, s_run.cycles, s_run.idle_ms);
}

/* 串口任务只置请求：lv_timer 只能在 LVGL 任务里建/改，所以由本定时器接手。 */
static void pf_probe(int n)
{
    if (s_run.active) { printf("bench probe: 本轮采样进行中，结束后再量\n"); return; }
    pf_ui_acc_t save = s_ui;
    memset(&s_ui, 0, sizeof s_ui);
    s_probe_mode = true;
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < n; i++) {
        int64_t p0 = esp_timer_get_time();
        int64_t p1 = esp_timer_get_time();
        fnos_perf_note_ui_stage(FNOS_PERF_PAGE_PAINT, (uint32_t)(p1 - p0));
    }
    int64_t t1 = esp_timer_get_time();
    s_probe_mode = false;
    s_ui = save;
    printf("[bench_probe] n=%d ns_per_probe=%lld\n", n, (long long)((t1 - t0) * 1000 / n));
}

static void pf_req_poll(lv_timer_t *t)
{
    (void)t;
    if (s_cap_req_valid) {
        s_cap_req_valid = false;
        uk_trend_set_gesture_fps((uint32_t)s_cap_req);
    }
    if (s_probe_req_valid) {
        s_probe_req_valid = false;
        pf_probe(s_probe_req);
    }
    /* 取消的 RELEASED 还没送进 LVGL 时不开新 run，否则新按下会被当成旧手势的移动。 */
    if (s_release_pending) return;
    if (!s_req.valid) return;
    pf_kind_t kind = s_req.kind;
    int page = s_req.page, px = s_req.px, cycles = s_req.cycles, sec = s_req.seconds;
    int drag_ms = s_req.drag_ms;
    s_req.valid = false;
    if (s_run.active) {
        pf_stop();                        /* 新命令顶掉旧运行 */
        if (s_release_pending) {          /* 先把合成的 RELEASED 送进 LVGL，下一轮再启动 */
            s_req.valid = true;
            return;
        }
    }
    s_req.kind = kind; s_req.page = page; s_req.px = px; s_req.cycles = cycles; s_req.seconds = sec;
    s_req.drag_ms = drag_ms;
    pf_start();
}

/* ---------------------------------------------------------------- 公开 API */

/* 松手→motion_finish：动画落定的真实时长与窗口内帧数。只读，不改 UI 时序。 */
void fnos_perf_note_refresh_prep(uint32_t us)
{
    if (s_run.active && s_lay_win == 2) s_ref_prep_us = us;
}

void fnos_perf_note_pair(uint32_t us)
{
    if (s_run.active) s_pair_us = us;
}

void fnos_perf_note_refresh_parts(uint32_t health_us, uint32_t shell_us, uint32_t body_us)
{
    if (!s_run.active || s_lay_win != 2) return;
    s_ref_health_us = health_us;
    s_ref_shell_us = shell_us;
    s_ref_body_us = body_us;
}

void fnos_perf_note_fin_skip(void)
{
    if (s_run.active) s_fin_skip++;
}

void fnos_perf_note_motion_settle(int page, uint32_t seq, bool new_snapshot)
{
    if (!s_run.active || !s_run.rel_seen || s_rel_settled) return;
    /* 只有松手之后新起的动画才算本次落定；进页前的 page_motion_settle 不算呈现。 */
    if (s_motion_gen == s_run.rel_gen) return;
    int64_t now = esp_timer_get_time();
    s_rel_settle_us = (uint32_t)(now - s_run.release_us);
    s_rel_frames    = s_run.rel_frames;
    s_rel_page      = page;
    s_rel_dir       = s_run.rel_dir;
    /* 方向、page、时长、快照在同一次接受里写入；后续 cycle 的落定整条丢弃。 */
    s_rel_seq       = seq;
    s_rel_new       = new_snapshot;
    s_finish_us     = now;
    s_final_pending = true;
    s_rel_settled   = true;
}

void fnos_perf_note_motion_start(void)
{
    s_motion_gen++;
}

/* 落定帧的三段成本；每轮只记最大值，退出时不留运行期串口输出。 */
void fnos_perf_note_settle_parts(uint32_t fix_us, uint32_t layout_us, uint32_t refresh_us)
{
    if (!s_run.active) return;
    if (fix_us > s_fin_fix_us) s_fin_fix_us = fix_us;
    if (layout_us > s_fin_layout_us) s_fin_layout_us = layout_us;
    if (refresh_us > s_fin_refresh_us) s_fin_refresh_us = refresh_us;
}

void fnos_perf_note_prep_refresh(uint32_t us)
{
    if (s_run.active && us > s_prep_refresh_us) s_prep_refresh_us = us;
}

void fnos_perf_note_pool_relayout(uint32_t mask)
{
    if (s_run.active) s_pool_rl_mask |= mask;
}

void fnos_perf_note_p1_parts(uint32_t vol_us, uint32_t raid_us, uint32_t disk_us, uint32_t inv_us)
{
    if (s_lay_win != 2) return; /* only accept parts from the settle refresh itself */
    s_p1_vol_us = vol_us; s_p1_raid_us = raid_us; s_p1_disk_us = disk_us; s_p1_inv_us = inv_us;
}

void fnos_perf_note_prep_page(uint32_t us)
{
    if (s_run.active && us > s_prep_page_us) s_prep_page_us = us;
}

void fnos_perf_layout_window(int win)
{
    s_lay_win = (win >= 0 && win <= 3) ? win : 0;
}

void fnos_perf_note_layout(int kind, uint32_t us, bool relayout)
{
    if (!s_run.active || s_lay_win <= 0 || kind < 0 || kind > 2) return;
    pf_lay_t *l = &s_lay[s_lay_win][kind];
    l->n++;
    l->us += us;
    if (us > l->max_us) l->max_us = us;
    if (relayout) l->relayouts++;
}

void fnos_perf_note_selfsize(uint32_t us, bool changed)
{
    if (!s_run.active) return;
    s_self_n++;
    s_self_us += us;
    if (changed) s_self_chg++;
}

void fnos_perf_note_scr_dirty(bool dirty)
{
    if (!s_run.active) return;
    s_scr_calls++;
    if (dirty) s_scr_dirty++;
}

void fnos_perf_note_shape_mismatch(uint32_t mask)
{
    if (!s_run.active) return;
    s_shape_mask |= mask;
    s_shape_hits++;
}

void fnos_perf_note_tick(uint32_t us, bool motion)
{
    if (!s_run.active) return;
    /* 主缓冲拒收后，分相也不能继续收：否则运动期与整刷期的并集会超过主行样本数。 */
    if (!pf_push(s_tick, &s_ntick, us)) return;
    pf_push(motion ? s_tick_motion : s_tick_full,
            motion ? &s_ntick_motion : &s_ntick_full, us);
}

/* 一次内存快照：free 是当前量，min 是开机以来的最低水位，largest 是最大连续块。
   三条线分开是因为内部 RAM / DMA / PSRAM 的用途和失败后果完全不同。 */
static void pf_mem_line(void)
{
    printf("[mem] internal free=%u min=%u largest=%u dma free=%u min=%u largest=%u "
           "psram free=%u min=%u largest=%u\n",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024),
           (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_DMA) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_DMA) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
    fflush(stdout);
}

static int pf_int(const char *s, int def)
{
    if (!s || !*s) return def;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno == ERANGE || end == s || v < 0 || v > INT_MAX) return def;
    while (*end == ' ' || *end == '\t') end++;
    if (*end) return def;                 /* 整串必须是数字，别把 "3x" 当 3 */
    return (int)v;
}

bool fnos_perf_cli(char *line)
{
    char *cmd = strtok(line, " \t");
    if (cmd && strcmp(cmd, "prof") == 0) {
#if CONFIG_LV_USE_PROFILER
        const char *sub = strtok(NULL, " \t");
        if (!sub || strcmp(sub, "on") == 0) {
            pf_prof_arm();
            printf("prof: 已开记录（buf=%d B，item≈%d）\n",
                   (int)CONFIG_LV_PROFILER_BUILTIN_BUF_SIZE,
                   (int)(CONFIG_LV_PROFILER_BUILTIN_BUF_SIZE / 24));
        } else if (strcmp(sub, "off") == 0) {
            lv_profiler_builtin_set_enable(false);
            printf("prof: 已停\n");
        } else if (strcmp(sub, "once") == 0) {
            int ms = pf_int(strtok(NULL, " \t"), 120);
            pf_prof_arm();
            printf("=== PROF BEGIN %dms ===\n", ms);
            vTaskDelay(pdMS_TO_TICKS(ms));
            lv_profiler_builtin_set_enable(false);
            lv_profiler_builtin_flush();
            printf("=== PROF END ===\n");
        } else if (strcmp(sub, "dump") == 0) {
            lv_profiler_builtin_set_enable(false);
            printf("=== PROF BEGIN dump ===\n");
            lv_profiler_builtin_flush();
            printf("=== PROF END ===\n");
        } else {
            printf("prof: 认不出 '%s'（on | off | once <ms> | dump）\n", sub);
        }
#else
        printf("prof: 这份固件没编进 LVGL profiler（要 CONFIG_LV_USE_PROFILER=y）\n");
#endif
        return true;
    }

    if (!cmd || strcmp(cmd, "bench") != 0) return false;

    char *a1 = strtok(NULL, " \t");
    char *a2 = strtok(NULL, " \t");
    char *a3 = strtok(NULL, " \t");
    char *a4 = strtok(NULL, " \t");
    if (!a1 || strcmp(a1, "help") == 0) {
        printf("bench tap <page 0-5> [cycles]            按下→首帧 / 松手→首帧（合成点按导航键）\n"
               "bench swipe <page 0-5> [px] [cycles]     120ms 合成横滑（左右交替；px 0=半宽，上限 700）\n"
               "bench drag <page 0-5> [px] [ms]          连续单向拖动（px 0=半宽，上限 700；ms 200-10000，默认 2000）\n"
               "bench idle <page 0-5> [seconds]          稳态帧间隔 / ui_tick / 各任务 CPU\n"
               "bench cap [fps|off]                     页面运动期趋势重绘上限（默认 0 = 不设限）\n"
               "bench probe [n]                         量一次 UI 探针自身的 ns 开销\n"
               "bench mem                               internal/dma/psram 的 free/min/largest（KB）\n"
               "prof once <ms>                           LVGL 官方 profiler：采 ms 毫秒的 FTrace 文本\n"
               "结束打印一行 [bench] kind=… 供 tools/perf_bench.py 解析\n");
        return true;
    }
    if (strcmp(a1, "cap") == 0) {
        if (!a2) {
            printf("bench cap: 当前值看 [bench_ui] gesture_fps；设置用 bench cap <fps|off>\n");
            return true;
        }
        int fps = strcmp(a2, "off") == 0 ? 0 : pf_int(a2, -1);
        if (fps < 0 || fps > 1000) { printf("bench cap: 认不出 '%s'（0/off 或 1-1000）\n", a2); return true; }
        s_cap_req = fps;
        s_cap_req_valid = true;
        printf("bench cap: 已排队 gesture_fps=%d（LVGL 任务应用）\n", fps);
        return true;
    }
    if (strcmp(a1, "mem") == 0) {
        pf_mem_line();
        return true;
    }
    if (strcmp(a1, "probe") == 0) {
        int n = pf_int(a2, 2000);
        if (n < 1 || n > 100000) n = 2000;
        s_probe_req = n;
        s_probe_req_valid = true;
        printf("bench probe: 已排队 n=%d（LVGL 任务执行）\n", n);
        return true;
    }
    if (strcmp(a1, "tap") == 0)        s_req.kind = PF_TAP;
    else if (strcmp(a1, "swipe") == 0) s_req.kind = PF_SWIPE;
    else if (strcmp(a1, "drag") == 0)  s_req.kind = PF_DRAG;
    else if (strcmp(a1, "idle") == 0)  s_req.kind = PF_IDLE;
    else { printf("bench: 认不出 '%s'（试 bench help）\n", a1); return true; }

    int page = pf_int(a2, 0);
    if (page >= FNOS_UI_PAGE_COUNT) page = 0;
    s_req.page = page;
    s_req.px = 0;
    s_req.cycles = 0;
    s_req.seconds = 0;
    s_req.drag_ms = 0;
    if (s_req.kind == PF_SWIPE)    { s_req.px = pf_int(a3, 0); s_req.cycles = pf_int(a4, 3); }
    else if (s_req.kind == PF_DRAG) {
        s_req.px = pf_int(a3, 600);
        s_req.drag_ms = pf_int(a4, 2000);
        if (s_req.drag_ms < PF_DRAG_MIN_MS) s_req.drag_ms = PF_DRAG_MIN_MS;
        if (s_req.drag_ms > PF_DRAG_MAX_MS) s_req.drag_ms = PF_DRAG_MAX_MS;
        s_req.cycles = 1;
    }
    else if (s_req.kind == PF_TAP) s_req.cycles = pf_int(a3, 3);
    else {
        s_req.seconds = pf_int(a3, 5);
        if (s_req.seconds > 3600) s_req.seconds = 3600;  /* 毫秒乘法不溢出 */
    }
    s_req.valid = true;
    printf("bench: 已排队 %s page=%d px=%d cycles=%d seconds=%d drag_ms=%d\n",
           a1, s_req.page, s_req.px, s_req.cycles, s_req.seconds, s_req.drag_ms);
    return true;
}

void fnos_perf_init(void)
{
    static bool done;
    if (done) return;
    done = true;

    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_display_add_event_cb(disp, pf_render_cb, LV_EVENT_RENDER_READY, NULL);
        lv_display_add_event_cb(disp, pf_start_cb, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(disp, pf_parts_cb, LV_EVENT_ALL, NULL);
    }
    s_script_timer = lv_timer_create(pf_script_tick, PF_STEP_MS, NULL);
    lv_timer_pause(s_script_timer);
    lv_timer_create(pf_req_poll, PF_REQ_POLL_MS, NULL);
    ESP_LOGI(TAG, "bench 就绪：串口 `bench help`");
}

#else  /* !CONFIG_FNOS_UI_PERF_BENCH */

void fnos_perf_init(void) {}
void fnos_perf_note_refresh_prep(uint32_t us) { (void)us; }
void fnos_perf_note_pair(uint32_t us) { (void)us; }
void fnos_perf_note_refresh_parts(uint32_t a, uint32_t b, uint32_t c) { (void)a; (void)b; (void)c; }
void fnos_perf_note_fin_skip(void) {}

void fnos_perf_note_motion_settle(int page, uint32_t seq, bool n) { (void)page; (void)seq; (void)n; }
void fnos_perf_note_settle_parts(uint32_t a, uint32_t b, uint32_t c) { (void)a; (void)b; (void)c; }
void fnos_perf_note_prep_page(uint32_t us) { (void)us; }
void fnos_perf_note_p1_parts(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { (void)a; (void)b; (void)c; (void)d; }
void fnos_perf_note_prep_refresh(uint32_t us) { (void)us; }
void fnos_perf_note_pool_relayout(uint32_t mask) { (void)mask; }
void fnos_perf_layout_window(int w) { (void)w; }
void fnos_perf_note_layout(int k, uint32_t us, bool r) { (void)k; (void)us; (void)r; }
void fnos_perf_note_shape_mismatch(uint32_t m) { (void)m; }
void fnos_perf_note_selfsize(uint32_t us, bool c) { (void)us; (void)c; }
void fnos_perf_note_scr_dirty(bool d) { (void)d; }
void fnos_perf_note_motion_start(void) {}
void fnos_perf_note_tick(uint32_t us, bool motion) { (void)us; (void)motion; }
void fnos_perf_note_stage(fnos_perf_stage_t stage, uint32_t us) { (void)stage; (void)us; }
void fnos_perf_note_ui_stage(fnos_perf_stage_t stage, uint32_t us) { (void)stage; (void)us; }
void fnos_perf_track_chart(void *chart) { (void)chart; }
bool fnos_perf_cli(char *line) { (void)line; return false; }

#endif
