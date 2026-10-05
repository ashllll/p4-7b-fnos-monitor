// 主机预览：不烧录，把真实的 fnos_ui（kk_ui 构件 + 字库）+ fnos_data 编译到 macOS 上渲染成 PPM。
//
// 为什么值得存在：v4/v5 的视觉迭代全靠"改 JSON → 生成 → 构建 → 烧录 → 拍照"，一轮十几分钟且
// 受手机翻拍质量影响。这里把同一份 LVGL 9.5.0 + 同一份字库 + 同一份 kk_widgets 搬到主机，
// 换页/取图/写盘全在进程内完成，一轮几秒，且像素级可信（v6 起界面由 fnos_ui.c 手写，不再有生成物）。
//
// 边界：只替身"板级"接口（esp_timer / esp_heap_caps / esp_log / fnos_data 轮询 / fnos_net Wi-Fi），
// 业务与 UI 代码一行不改。fixture 打在 fnos_status_t 上，因此格式化串、阈值配色、可信度降级
// 全都走真实的 fnos_ui 刷屏逻辑。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>

#include "lvgl.h"
#include "fnos_data.h"
#include "fnos_ui.h"

/* ── 板级替身 ─────────────────────────────────────────────────────── */

int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

size_t heap_caps_get_free_size(int caps) { (void)caps; return 214 * 1024; }
size_t esp_get_free_heap_size(void)      { return 214 * 1024; }

const char *fnos_net_ip(void)  { return "192.168.0.42"; }
int8_t      fnos_net_rssi(void){ return -54; }   /* 0 = 未知 */

/* ── 数据替身：一份打到上限的 NAS 快照 ────────────────────────────── */

typedef enum { ST_LIVE, ST_OFFLINE, ST_WARMING } preview_state_t;
static preview_state_t s_state = ST_LIVE;

static void fill_live(fnos_status_t *s)
{
    memset(s, 0, sizeof *s);
    s->ever_ok = true;
    s->online  = true;
    s->recv_ms = esp_timer_get_time() / 1000;
    s->fail_ms = s->recv_ms - 41000;
    s->http_ms = 23;
    s->last_status = 200;
    s->ok_count = 18422;
    s->fail_count = 7;
    s->last_err[0] = 0;
    snprintf(s->host, sizeof s->host, "%s", "fnos-nas");
    s->uptime_s = 2 * 86400 + 7 * 3600 + 41 * 60;

    s->cpu.pct = 37.4f; s->cpu.load1 = 1.42f; s->cpu.load5 = 1.18f; s->cpu.load15 = 0.96f;
    s->cpu.temp_c = 52.0f; s->cpu.cores = 8; s->cpu.runq = 2; s->cpu.procs = 412;

    s->mem.total_mb = 16384.0f; s->mem.used_mb = 10362.0f; s->mem.avail_mb = 6022.0f;
    s->mem.pct = 63.2f; s->mem.swap_total_mb = 2048.0f; s->mem.swap_used_mb = 137.0f;

    snprintf(s->net.ifname, sizeof s->net.ifname, "%s", "eth0");
    s->net.rx_kbs = 12482.0f; s->net.tx_kbs = 3127.0f;
    s->net.rx_total_gb = 8421.5f; s->net.tx_total_gb = 2210.3f;

    struct { const char *mnt, *fs; float tot, used; } v[] = {
        { "/vol1", "ext4",  3726.0f, 2004.0f },
        { "/vol2", "btrfs", 7452.0f, 5518.0f },
        { "/vol3", "ext4",  1863.0f,  214.0f },
        { "/vol4", "btrfs", 9315.0f, 8897.0f },
        { "/vol5", "ext4",   931.0f,  402.0f },
        { "/vol6", "ext4",  3726.0f, 1188.0f },
    };
    s->nvols = 6;
    for (int i = 0; i < s->nvols; i++) {
        snprintf(s->vols[i].mnt, sizeof s->vols[i].mnt, "%s", v[i].mnt);
        snprintf(s->vols[i].fs,  sizeof s->vols[i].fs,  "%s", v[i].fs);
        s->vols[i].total_gb = v[i].tot;
        s->vols[i].used_gb  = v[i].used;
        s->vols[i].free_gb  = v[i].tot - v[i].used;
        s->vols[i].pct      = 100.0f * v[i].used / v[i].tot;
    }

    struct { const char *dev, *lvl, *st; bool ok; int have, want; float sync; } r[] = {
        { "md0", "raid5", "clean",  true,  4, 4, 100.0f },
        { "md1", "raid1", "resync", false, 2, 2,  47.3f },
        { "md2", "raid0", "clean",  true,  2, 2, 100.0f },
        { "md3", "raid6", "degraded", false, 5, 6, 100.0f },
    };
    s->nraid = 4;
    for (int i = 0; i < s->nraid; i++) {
        snprintf(s->raid[i].dev,   sizeof s->raid[i].dev,   "%s", r[i].dev);
        snprintf(s->raid[i].lvl,   sizeof s->raid[i].lvl,   "%s", r[i].lvl);
        snprintf(s->raid[i].state, sizeof s->raid[i].state, "%s", r[i].st);
        s->raid[i].ok = r[i].ok; s->raid[i].have = r[i].have; s->raid[i].want = r[i].want;
        s->raid[i].sync_pct = r[i].sync;
    }

    struct { const char *dev; float rd, wr; } d[] = {
        { "sda",      412.0f,  1830.0f },
        { "sdb",       88.0f,   640.0f },
        { "nvme0n1", 2140.0f,  680.0f },
        { "nvme1n1",   12.0f,    0.0f },
    };
    s->ndisks = 4;
    for (int i = 0; i < s->ndisks; i++) {
        snprintf(s->disks[i].dev, sizeof s->disks[i].dev, "%s", d[i].dev);
        s->disks[i].rd_kbs = d[i].rd; s->disks[i].wr_kbs = d[i].wr;
    }

    struct { const char *n; float c; } t[] = {
        { "CPU",     52.0f }, { "NVMe0",  41.0f }, { "NVMe1",  38.0f }, { "HDD1",   36.0f },
        { "HDD2",    37.0f }, { "HDD3",   44.0f }, { "HDD4",   39.0f }, { "Board",  31.0f },
        { "PSU",     34.0f }, { "Chipset", 48.0f },
    };
    s->ntemps = 10;
    for (int i = 0; i < s->ntemps; i++) {
        snprintf(s->temps[i].n, sizeof s->temps[i].n, "%s", t[i].n);
        s->temps[i].c = t[i].c;
    }

    struct { const char *n; bool up; const char *st; } k[] = {
        { "jellyfin",     true,  "Up 3 days" },
        { "immich-server",true,  "Up 3 days" },
        { "qbittorrent",  true,  "Up 11 hours" },
        { "vaultwarden",  true,  "Up 3 days" },
        { "nginx-proxy",  true,  "Up 6 days" },
        { "code-server",  false, "Exited (0) 2 hours ago" },
        { "postgres",     true,  "Up 3 days (healthy)" },
        { "redis",        false, "Exited (137) 5 minutes ago" },
    };
    s->ndocker = 8;
    for (int i = 0; i < s->ndocker; i++) {
        snprintf(s->docker[i].n, sizeof s->docker[i].n, "%s", k[i].n);
        snprintf(s->docker[i].s, sizeof s->docker[i].s, "%s", k[i].st);
        s->docker[i].up = k[i].up;
    }

    s->has_zfs = true; s->zfs_arc_gb = 12.4f; s->zfs_hit_pct = 96.3f;

    /* 告警文案按 nas/fnos-agent.py:_alerts() 的真实格式造（全 ASCII，级别 crit/warn/info） */
    snprintf(s->alerts[0].lv, sizeof s->alerts[0].lv, "%s", "warn");
    snprintf(s->alerts[0].m,  sizeof s->alerts[0].m,  "%s", "/vol4 used 95%");
    snprintf(s->alerts[1].lv, sizeof s->alerts[1].lv, "%s", "crit");
    snprintf(s->alerts[1].m,  sizeof s->alerts[1].m,  "%s", "/vol3 free 4.6% left");
    snprintf(s->alerts[2].lv, sizeof s->alerts[2].lv, "%s", "warn");
    snprintf(s->alerts[2].m,  sizeof s->alerts[2].m,  "%s", "container qbittorrentee down");
    snprintf(s->alerts[3].lv, sizeof s->alerts[3].lv, "%s", "info");
    snprintf(s->alerts[3].m,  sizeof s->alerts[3].m,  "%s", "md1 resync 47.3%");
    s->nalerts = 4;
}

bool fnos_data_get(fnos_status_t *out)
{
    if (s_state == ST_WARMING) { memset(out, 0, sizeof *out); return false; }
    fill_live(out);
    if (s_state == ST_OFFLINE) {
        out->online = false;
        out->recv_ms -= 96000;             /* 96 s 前的旧帧 → 可信度降级 */
        out->last_status = 0;
        snprintf(out->last_err, sizeof out->last_err, "%s", "timeout");
        out->ok_count -= 3;
        out->fail_count += 12;
    }
    return true;
}

/* 曲线样本：确定性的正弦 + 噪声，让趋势/峰值/量程都吃到真实分布 */
static int64_t s_seq;
int fnos_data_hist_read(int64_t since_seq, fnos_sample_t *out, int max, int64_t *next_seq)
{
    int n = 0;
    while (n < max && since_seq + n < s_seq) {
        int64_t k = since_seq + n;
        double t = (double)k * 0.5;
        out[n].cpu    = (float)(34.0 + 11.0 * sin(t * 0.31) + 5.0 * sin(t * 1.7) + 2.0 * sin(t * 5.3));
        out[n].mem    = (float)(62.0 +  4.0 * sin(t * 0.11) + 1.5 * sin(t * 0.9));
        out[n].rx_kbs = (float)(9000.0 + 6500.0 * sin(t * 0.23) + 3200.0 * sin(t * 1.1) + 900.0 * sin(t * 4.7));
        out[n].tx_kbs = (float)(2600.0 + 1400.0 * sin(t * 0.19 + 1.0) + 700.0 * sin(t * 2.3));
        if (out[n].rx_kbs < 60.0f) out[n].rx_kbs = 60.0f;
        if (out[n].tx_kbs < 40.0f) out[n].tx_kbs = 40.0f;
        n++;
    }
    if (n > 0 && next_seq) *next_seq = since_seq + n;
    return n;
}
int64_t fnos_data_hist_seq(void) { return s_seq; }

/* ── 取图 ─────────────────────────────────────────────────────────── */

#define SCR_W 1024
#define SCR_H 600
static uint16_t s_frame[SCR_W * SCR_H];
static const char *s_outdir = "out";

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    const uint16_t *src = (const uint16_t *)px_map;
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++)
            s_frame[y * SCR_W + x] = *src++;
    lv_display_flush_ready(d);
}

static void snapshot(const char *name)
{
    lv_refr_now(lv_display_get_default());
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        uint16_t v = s_frame[i];
        fputc((((v >> 11) & 0x1F) * 255) / 31, f);
        fputc((((v >>  5) & 0x3F) * 255) / 63, f);
        fputc((( v        & 0x1F) * 255) / 31, f);
    }
    fclose(f);
    printf("  %s\n", path);
}

static const char *state_name(preview_state_t s)
{
    return s == ST_LIVE ? "live" : (s == ST_OFFLINE ? "offline" : "warming");
}

/* 虚拟时钟。设备上 500 ms 一跳；预览里同一段"20 秒"必须瞬时且可复现，所以直接推虚拟时间。
   两个都必须接管：LV_OS_NONE 下 lv_delay_ms() 是 busy-wait（`while(lv_tick_elaps(t) < ms)`），
   而虚拟 tick 只在我们自己推进时才前进 —— 不接管 delay cb 就会死循环烧 CPU。 */
static uint32_t s_vms;
static uint32_t vtick_get(void)          { return s_vms; }
static void     vtick_advance(uint32_t ms) { s_vms += ms; }
static void     vdelay(uint32_t ms)      { s_vms += ms; }

static void run_state(preview_state_t st, int index)
{
    s_state = st;
    if (st != ST_WARMING) {
        /* 20 s 虚拟时间：500 ms 一次 tick，喂满 90 点曲线 */
        for (int i = 0; i < 40; i++) {
            s_seq += 12;
            vtick_advance(500);
            lv_timer_handler();
        }
    } else {
        s_seq = 0;
        vtick_advance(500);
        lv_timer_handler();
    }
    /* create() 之后、任何 set_page 之前的"初始帧"：这一帧专抓"create 时没把初始页显示出来"
     * 这类只在设备上暴露的 bug（2026-10-05 实例见 docs/verification.md §17.3）。 */
    if (index == 1) snapshot("01-live-init");

    for (int p = 0; p < FNOS_UI_PAGE_COUNT; p++) {
        fnos_ui_set_page(p);
        vtick_advance(300);
        lv_timer_handler();
        char name[64];
        snprintf(name, sizeof name, "%02d-%s-p%d", index, state_name(st), p);
        snapshot(name);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) s_outdir = argv[1];
    mkdir(s_outdir, 0755);
    setvbuf(stdout, NULL, _IONBF, 0);   /* 重定向到文件时 stdout 是块缓冲的：不退出就看不到任何日志 */
    setvbuf(stderr, NULL, _IONBF, 0);

    lv_init();
    lv_tick_set_cb(vtick_get);
    lv_delay_set_cb(vdelay);
    lv_display_t *d = lv_display_create(SCR_W, SCR_H);
    static uint16_t buf[SCR_W * SCR_H];
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);

    fnos_ui_create();

    run_state(ST_LIVE,    1);
    run_state(ST_OFFLINE, 2);
    run_state(ST_WARMING, 3);

    printf("preview: %d page(s)/state → %s\n", FNOS_UI_PAGE_COUNT, s_outdir);
    return 0;
}
