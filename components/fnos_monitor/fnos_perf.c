#include "fnos_perf.h"

#include "sdkconfig.h"   /* 本仓库不强制 -include sdkconfig.h；这个编译单元没有别的头带它进来 */

#ifdef CONFIG_FNOS_UI_PERF_BENCH

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "fnos_ui.h"

static const char *TAG = "perf";

#define PF_SAMPLES        256     /* 每种样本的上限；满了丢新样本（报里的 n 让人看得见） */
#define PF_STEP_MS        8       /* 脚本推进节奏 = UK_GESTURE_POLL_MS */
#define PF_SETTLE_MS      800     /* 换页请求由 ui_tick(500ms) 落地，留一跳余量 */
#define PF_TAP_HOLD_MS    60      /* 按住时长：短于读周期就可能一次都没报按下 */
#define PF_ARM_TIMEOUT_MS 300     /* 置了按下却等不到 indev 上报，判 miss */
#define PF_RELEASE_MS     80      /* 松开后等 indev 把 RELEASED 读走 */
#define PF_TAIL_MS        500     /* 松手后收尾：吸附动画 + 一帧静止 */
#define PF_DRAG_STEPS     15      /* 15 步 × 8ms ≈ 120ms 的横滑 */
#define PF_MAX_INDEVS     4
#define PF_TASKS          24
#define PF_REQ_POLL_MS    100

typedef enum { PF_NONE = 0, PF_TAP, PF_SWIPE, PF_IDLE } pf_kind_t;
typedef enum { PH_IDLE = 0, PH_SETTLE, PH_HOLD, PH_DRAG, PH_RELEASE, PH_TAIL } pf_phase_t;

/* 串口任务写、LVGL 任务读：只有 valid 一个字的交接，够用且不用锁
   （写完参数才置 valid；读方见 valid 先清再取参数）。 */
static struct {
    volatile bool valid;
    pf_kind_t kind;
    int page, px, cycles, seconds;
} s_req;

static struct {
    bool active;
    pf_kind_t kind;
    int page, px, cycles, done, misses, idle_ms;
    pf_phase_t phase;
    int64_t phase_us, run_us;
    int32_t x0, y0, x, y, span;
    int step, steps, dir;
    bool pressed, press_seen, rel_seen;
    bool wait_first, wait_rel, frame_arm;
    int64_t press_us, release_us;
    uint32_t frames;
} s_run;

static uint32_t s_p2f[PF_SAMPLES];   /* 按下 → 首帧：有没有触控反馈、反馈多快 */
static uint32_t s_r2f[PF_SAMPLES];   /* 松手 → 首帧：点一下多久有反应（换页那一帧） */
static uint32_t s_iv[PF_SAMPLES];    /* 相邻帧间隔：拖动跟手 / 稳态帧率 */
static uint32_t s_tick[PF_SAMPLES];  /* ui_tick 总耗时（数据刷新 + 图表 + 行重建） */
static int s_np2f, s_nr2f, s_niv, s_ntick;

static int64_t s_last_frame_us;

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

static void pf_push(uint32_t *buf, int *n, uint32_t v)
{
    if (*n < PF_SAMPLES) buf[(*n)++] = v;
}

/* ------------------------------------------------------- 各任务 CPU 占比 */

typedef struct {
    char name[configMAX_TASK_NAME_LEN];
    uint32_t run;
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
    }
    return k;
}

/* 双核：把「各任务 delta 之和」当 100%，于是 lvgl/idle 的占比可以跨核比较。 */
static void pf_cpu_delta(uint32_t *total, uint32_t *lvgl, uint32_t *idle)
{
    uint32_t t = 0, l = 0, i2 = 0;
    for (int i = 0; i < s_ncpu1; i++) {
        for (int j = 0; j < s_ncpu0; j++) {
            if (strcmp(s_cpu0[j].name, s_cpu1[i].name) != 0) continue;
            uint32_t d = s_cpu1[i].run - s_cpu0[j].run;   /* 计数器回绕概率可忽略 */
            t += d;
            if (strcmp(s_cpu1[i].name, "lvgl") == 0) l += d;
            if (strncmp(s_cpu1[i].name, "IDLE", 4) == 0) i2 += d;
            break;
        }
    }
    *total = t; *lvgl = l; *idle = i2;
}

/* ------------------------------------------------------------ 合成指针 */

static struct {
    lv_indev_t *indev;
    lv_indev_read_cb_t cb;
} s_real[PF_MAX_INDEVS];
static int s_nreal;

static void pf_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    /* 先照常问真实面板：adapter 靠这次调用做触摸握手与记账，跳过它就改变了被测对象。 */
    for (int i = 0; i < s_nreal; i++) {
        if (s_real[i].indev == indev && s_real[i].cb) { s_real[i].cb(indev, data); break; }
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
        s_run.frame_arm  = true;
    } else if (!s_run.pressed && s_run.press_seen && !s_run.rel_seen) {
        s_run.rel_seen   = true;
        s_run.release_us = esp_timer_get_time();
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

static void pf_render_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RENDER_READY) return;
    int64_t now = esp_timer_get_time();
    if (!s_run.active) { s_last_frame_us = now; return; }

    s_run.frames++;
    if (s_run.wait_first) { s_run.wait_first = false; pf_push(s_p2f, &s_np2f, (uint32_t)(now - s_run.press_us)); }
    if (s_run.wait_rel)   { s_run.wait_rel   = false; pf_push(s_r2f, &s_nr2f, (uint32_t)(now - s_run.release_us)); }
    if (s_run.frame_arm && s_last_frame_us) pf_push(s_iv, &s_niv, (uint32_t)(now - s_last_frame_us));
    s_last_frame_us = now;
}

/* ---------------------------------------------------------------- 报告 */

static void pf_report(void)
{
    uint32_t wall_ms = (uint32_t)((esp_timer_get_time() - s_run.run_us) / 1000);
    s_ncpu1 = pf_snap_tasks(s_cpu1);
    uint32_t total = 0, lvgl = 0, idle = 0;
    pf_cpu_delta(&total, &lvgl, &idle);
    uint32_t iv50 = pf_pct(s_iv, s_niv, 50);

    printf("[bench] kind=%s page=%d cycles=%d misses=%d p2f_n=%d p2f_p50_us=%u p2f_p95_us=%u p2f_max_us=%u"
           " r2f_n=%d r2f_p50_us=%u r2f_p95_us=%u r2f_max_us=%u"
           " iv_n=%d iv_p50_us=%u iv_p95_us=%u iv_max_us=%u frames=%u wall_ms=%u fps_avg=%.1f fps_p50=%.1f"
           " tick_n=%d tick_p50_us=%u tick_p95_us=%u tick_max_us=%u cpu_lvgl=%.1f cpu_idle=%.1f\n",
           s_run.kind == PF_TAP ? "tap" : (s_run.kind == PF_SWIPE ? "swipe" : "idle"),
           s_run.page, s_run.cycles, s_run.misses,
           s_np2f, pf_pct(s_p2f, s_np2f, 50), pf_pct(s_p2f, s_np2f, 95), pf_max(s_p2f, s_np2f),
           s_nr2f, pf_pct(s_r2f, s_nr2f, 50), pf_pct(s_r2f, s_nr2f, 95), pf_max(s_r2f, s_nr2f),
           s_niv, iv50, pf_pct(s_iv, s_niv, 95), pf_max(s_iv, s_niv),
           s_run.frames, wall_ms, wall_ms ? 1000.0 * s_run.frames / wall_ms : 0.0,
           iv50 ? 1000000.0 / iv50 : 0.0,
           s_ntick, pf_pct(s_tick, s_ntick, 50), pf_pct(s_tick, s_ntick, 95), pf_max(s_tick, s_ntick),
           total ? 100.0 * lvgl / total : 0.0, total ? 100.0 * idle / total : 0.0);
    fflush(stdout);
}

static lv_timer_t *s_script_timer;

static void pf_stop(void)
{
    s_run.active = false;
    pf_detach();
    if (s_script_timer) lv_timer_pause(s_script_timer);
}

/* ------------------------------------------------------------ 脚本状态机 */

static void pf_arm(int64_t now)
{
    if (s_run.kind == PF_SWIPE) {
        /* 左右交替，免得连着同方向滑把页面滑到边上。 */
        if (!fnos_ui_swipe_point(s_run.dir, &s_run.x0, &s_run.y0)) { s_run.misses++; pf_stop(); return; }
        if (s_run.px <= 0) {
            lv_display_t *d = lv_display_get_default();
            s_run.px = d ? lv_display_get_horizontal_resolution(d) / 2 : 512;
        }
        s_run.span  = s_run.px;
        s_run.steps = PF_DRAG_STEPS;
        s_run.x = s_run.x0;
        s_run.y = s_run.y0;
    } else {
        /* 点按导航键中心：按下有反馈、松手换页，两个时延都在同一次手势里量到。 */
        if (!fnos_ui_nav_center(s_run.page, &s_run.x0, &s_run.y0)) { s_run.misses++; pf_stop(); return; }
        s_run.x = s_run.x0;
        s_run.y = s_run.y0;
        s_run.steps = 0;
    }
    s_run.pressed = true;
    s_run.phase = (s_run.kind == PF_SWIPE) ? PH_DRAG : PH_HOLD;
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
    } else if (s_run.kind == PF_SWIPE) {
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
            s_run.step++;
            s_run.x = s_run.x0 + (int32_t)((int64_t)s_run.dir * s_run.span * s_run.step / s_run.steps);
            if (s_run.step >= s_run.steps) { s_run.pressed = false; s_run.phase = PH_RELEASE; s_run.phase_us = now; }
        }
        break;

    case PH_RELEASE:
        if (el >= PF_RELEASE_MS) { s_run.frame_arm = false; s_run.phase = PH_TAIL; s_run.phase_us = now; }
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
    memset(s_p2f, 0, sizeof s_p2f);
    memset(s_r2f, 0, sizeof s_r2f);
    memset(s_iv, 0, sizeof s_iv);
    memset(s_tick, 0, sizeof s_tick);
    s_np2f = s_nr2f = s_niv = s_ntick = 0;
    s_last_frame_us = 0;

    pf_detach();   /* 上一轮留下的挂载先摘干净，否则会把 pf_read_cb 当成"真实回调"套娃 */
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
    s_run.dir    = -1;
    s_run.cycles = kind == PF_IDLE ? 1 : (s_req.cycles > 0 ? s_req.cycles : 3);
    s_run.idle_ms = (s_req.seconds > 0 ? s_req.seconds : 5) * 1000;
    s_run.run_us = now;
    s_ncpu0 = pf_snap_tasks(s_cpu0);

    if (s_run.kind == PF_IDLE) {
        /* 稳态：停在目标页不动，只收帧间隔 / ui_tick / CPU。 */
        fnos_ui_set_page(s_run.page);
        s_run.phase = PH_SETTLE;
        s_run.phase_us = now;
    } else {
        s_run.phase = PH_SETTLE;
        s_run.phase_us = now;
        /* 点按要有一页可切：先落到目标页，第一轮再由 pf_next_cycle 切走。 */
        fnos_ui_set_page(s_run.page);
    }
    if (s_script_timer) lv_timer_resume(s_script_timer);
    ESP_LOGI(TAG, "bench start kind=%d page=%d cycles=%d idle_ms=%d", (int)kind, s_run.page, s_run.cycles, s_run.idle_ms);
}

/* 串口任务只置请求：lv_timer 只能在 LVGL 任务里建/改，所以由本定时器接手。 */
static void pf_req_poll(lv_timer_t *t)
{
    (void)t;
    if (!s_req.valid) return;
    pf_kind_t kind = s_req.kind;
    int page = s_req.page, px = s_req.px, cycles = s_req.cycles, sec = s_req.seconds;
    s_req.valid = false;
    if (s_run.active) pf_stop();          /* 新命令顶掉旧运行 */
    s_req.kind = kind; s_req.page = page; s_req.px = px; s_req.cycles = cycles; s_req.seconds = sec;
    pf_start();
}

/* ---------------------------------------------------------------- 公开 API */

void fnos_perf_note_tick(uint32_t us)
{
    if (s_run.active) pf_push(s_tick, &s_ntick, us);
}

static int pf_int(const char *s, int def)
{
    if (!s || !*s) return def;
    int v = atoi(s);
    return v >= 0 ? v : def;
}

bool fnos_perf_cli(char *line)
{
    char *cmd = strtok(line, " \t");
    if (!cmd || strcmp(cmd, "bench") != 0) return false;

    char *a1 = strtok(NULL, " \t");
    char *a2 = strtok(NULL, " \t");
    char *a3 = strtok(NULL, " \t");
    char *a4 = strtok(NULL, " \t");
    if (!a1 || strcmp(a1, "help") == 0) {
        printf("bench tap <page 0-5> [cycles]            按下→首帧 / 松手→首帧（合成点按导航键）\n"
               "bench swipe <page 0-5> [px] [cycles]     拖动期帧间隔（合成横滑，左右交替；px 0 = 半个显示宽）\n"
               "bench idle <page 0-5> [seconds]          稳态帧间隔 / ui_tick / 各任务 CPU\n"
               "结束打印一行 [bench] kind=… 供 tools/perf_bench.py 解析\n");
        return true;
    }
    if (strcmp(a1, "tap") == 0)        s_req.kind = PF_TAP;
    else if (strcmp(a1, "swipe") == 0) s_req.kind = PF_SWIPE;
    else if (strcmp(a1, "idle") == 0)  s_req.kind = PF_IDLE;
    else { printf("bench: 认不出 '%s'（试 bench help）\n", a1); return true; }

    int page = pf_int(a2, 0);
    if (page >= FNOS_UI_PAGE_COUNT) page = 0;
    s_req.page = page;
    s_req.px = 0;
    s_req.cycles = 0;
    s_req.seconds = 0;
    if (s_req.kind == PF_SWIPE)    { s_req.px = pf_int(a3, 0); s_req.cycles = pf_int(a4, 3); }
    else if (s_req.kind == PF_TAP) s_req.cycles = pf_int(a3, 3);
    else                           s_req.seconds = pf_int(a3, 5);
    s_req.valid = true;
    printf("bench: 已排队 %s page=%d px=%d cycles=%d seconds=%d\n",
           a1, s_req.page, s_req.px, s_req.cycles, s_req.seconds);
    return true;
}

void fnos_perf_init(void)
{
    static bool done;
    if (done) return;
    done = true;

    lv_display_t *disp = lv_display_get_default();
    if (disp) lv_display_add_event_cb(disp, pf_render_cb, LV_EVENT_RENDER_READY, NULL);
    s_script_timer = lv_timer_create(pf_script_tick, PF_STEP_MS, NULL);
    lv_timer_pause(s_script_timer);
    lv_timer_create(pf_req_poll, PF_REQ_POLL_MS, NULL);
    ESP_LOGI(TAG, "bench 就绪：串口 `bench help`");
}

#else  /* !CONFIG_FNOS_UI_PERF_BENCH */

void fnos_perf_init(void) {}
void fnos_perf_note_tick(uint32_t us) { (void)us; }
bool fnos_perf_cli(char *line) { (void)line; return false; }

#endif
