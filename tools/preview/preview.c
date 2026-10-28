// 主机预览：不烧录，把真实的 fnos_ui（ui_kit 构件 + 字库） 编译到 macOS 上渲染成 PPM。
//
// 为什么值得存在：v4/v5 的视觉迭代全靠"改 JSON → 生成 → 构建 → 烧录 → 拍照"，一轮十几分钟且
// 受手机翻拍质量影响。这里把同一份 LVGL 9.5.0 + 同一份字库 + 同一份 ui_kit 搬到主机，
// 换页/取图/写盘全在进程内完成，一轮几秒，且像素级可信（v6 起界面由 fnos_ui.c 手写，不再有生成物）。
//
// 边界：只替身"板级"接口（esp_timer / esp_heap_caps / esp_log / fnos_data 轮询 / fnos_net Wi-Fi），
// 业务与 UI 代码一行不改。fixture 打在 fnos_status_t 上，因此格式化串、阈值配色、可信度降级
// 全都走真实的 fnos_ui 刷屏逻辑。
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <float.h>
#include <sys/stat.h>

#include "lvgl.h"
#include "src/misc/lv_text_private.h"
#include "src/widgets/chart/lv_chart_private.h"
#include "fnos_data.h"
#include "fnos_ui.h"
#include "fnos_net.h"
#include "ui_kit/uk_theme.h"
#include "ui_kit/uk.h"

/* ── 板级替身 ─────────────────────────────────────────────────────── */

static bool s_motion_clock_frozen;
static int64_t s_motion_clock_us;

/* UI pixel comparisons must not change when a render crosses a wall-clock minute. */
time_t fnos_preview_time(time_t *out)
{
    static time_t captured;
    if (!captured) captured = time(NULL);
    if (out) *out = captured;
    return captured;
}

int64_t esp_timer_get_time(void)
{
    if (s_motion_clock_frozen) return s_motion_clock_us;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

size_t heap_caps_get_free_size(int caps) { (void)caps; return 214 * 1024; }
size_t esp_get_free_heap_size(void)      { return 214 * 1024; }

const char *fnos_net_ip(void)  { return "192.0.2.42"; }
int8_t      fnos_net_rssi(void){ return -54; }   /* 0 = 未知 */

/* ── Wi-Fi 替身 ────────────────────────────────────────────────────────
   配网卡（fnos_ui.c 的 wifi_* ）的真实数据全部来自 fnos_net_*：扫描列表、
   连接状态、当前 SSID。所以这里给一组**可控**的替身，让预览能把四个阶段都拍下来：
   没配网（顶栏橙字）/ 扫到网络 / 输口令 / 连上或连不上。
   真机上这些函数是 ESP-Hosted 的同步 RPC + NVS，这里只是几个静态变量。 */
static bool        s_wifi_cfg;                 /* 有凭据 */
static bool        s_wifi_on;                  /* 已连接（拿到 IP） */
static char        s_wifi_ssid_stub[33] = "";
static char        s_wifi_reason[64]    = "";  /* 非空 = 连不上的原因 */
/* 本轮配网提交的状态：与真机 fnos_net.c 同一套语义（审计 M04 + M05）。
   0=IDLE 1=PENDING 2=FAILED（用 int 存，避免在这里重复 typedef）。 */
static int         s_wifi_submit;
static char        s_wifi_submit_reason[128] = "";
static fnos_ap_t   s_wifi_aps[FNOS_AP_MAX];
static int         s_wifi_ap_n;

/* Synthetic public networks cover encrypted/open entries and signal ordering. */
static void wifi_stub_reset(void)
{
    static const struct { const char *ssid; int8_t rssi; bool secure; } def[] = {
        { "Demo",           -46, true  },
        { "Demo-Guest",  -61, true  },
        { "Demo-IoT",      -72, true  },
        { "Guest-Open",     -80, false },
    };
    s_wifi_ap_n = (int)(sizeof def / sizeof def[0]);
    for (int i = 0; i < s_wifi_ap_n; i++) {
        snprintf(s_wifi_aps[i].ssid, sizeof s_wifi_aps[i].ssid, "%s", def[i].ssid);
        s_wifi_aps[i].rssi   = def[i].rssi;
        s_wifi_aps[i].secure = def[i].secure;
    }
}
static void wifi_stub(bool cfg, bool online, const char *ssid, const char *reason)
{
    s_wifi_cfg  = cfg;
    s_wifi_on   = online;
    snprintf(s_wifi_ssid_stub, sizeof s_wifi_ssid_stub, "%s", ssid ? ssid : "");
    snprintf(s_wifi_reason, sizeof s_wifi_reason, "%s", reason ? reason : "");
    /* 真机的对应关系：拿到 IP → 本轮提交成功（回到 IDLE，成功由 ONLINE 表达）；
       收到带原因的断开 → 本轮提交失败。这样"第一次配网失败"与"在线改密码"
       两条路径在预览里也能被渲染出来（审计 M04 + M05）。 */
    if (reason && reason[0]) {
        s_wifi_submit = FNOS_WIFI_SUBMIT_FAILED;
        snprintf(s_wifi_submit_reason, sizeof s_wifi_submit_reason, "%s", reason);
    } else {
        s_wifi_submit = FNOS_WIFI_SUBMIT_IDLE;
        s_wifi_submit_reason[0] = 0;
    }
    wifi_stub_reset();
}
bool        fnos_net_configured(void) { return s_wifi_cfg; }
/* 凭据存储的替身：界面开机判断"要不要弹配网卡"问的是存储本身（不问网络层标志），
   主机上就返回同一个 s_wifi_cfg，好让"未配置 → 自动弹卡"这条路径照样被渲染到。 */
bool fnos_wifi_store_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    (void)pass; (void)pass_cap;
    if (ssid && ssid_cap) snprintf(ssid, ssid_cap, "%s", s_wifi_cfg ? s_wifi_ssid_stub : "");
    return s_wifi_cfg;
}
bool        fnos_net_online(void)     { return s_wifi_on; }
const char *fnos_net_ssid(void)       { return s_wifi_ssid_stub; }
void        fnos_net_ssid_r(char *out, size_t cap)
{
    snprintf(out, cap, "%s", s_wifi_ssid_stub);
}
fnos_net_state_t fnos_net_state(void)
{
    /* 与真机 fnos_net.c 同序：本轮提交的状态优先（审计 M04 + M05） */
    if (s_wifi_submit == FNOS_WIFI_SUBMIT_PENDING) return FNOS_NET_CONNECTING;
    if (s_wifi_submit == FNOS_WIFI_SUBMIT_FAILED)  return FNOS_NET_FAILED;
    if (!s_wifi_cfg)          return FNOS_NET_UNCONFIGURED;
    if (s_wifi_on)            return FNOS_NET_ONLINE;
    if (s_wifi_reason[0])     return FNOS_NET_FAILED;
    return FNOS_NET_CONNECTING;
}
const char *fnos_net_state_str(void)
{
    static char b[160];
    fnos_net_state_str_r(b, sizeof b);
    return b;
}
void fnos_net_state_str_r(char *out, size_t cap)
{
    if (!out || !cap) return;
    if (s_wifi_submit == FNOS_WIFI_SUBMIT_PENDING)
        snprintf(out, cap, "正在按新凭据连接 %s…（口令或加密方式不合适会在这里失败）",
                 s_wifi_ssid_stub);
    else if (s_wifi_submit == FNOS_WIFI_SUBMIT_FAILED)
        snprintf(out, cap, "连不上 %s：%s（第 2 次重试）", s_wifi_ssid_stub,
                 s_wifi_submit_reason[0] ? s_wifi_submit_reason : "这一轮没连上");
    else if (!s_wifi_cfg)     snprintf(out, cap, "未配置 Wi-Fi");
    else if (s_wifi_on)       snprintf(out, cap, "已连接 %s（192.0.2.42）", s_wifi_ssid_stub);
    else if (s_wifi_reason[0])snprintf(out, cap, "连不上 %s：%s（第 2 次重试）",
                                       s_wifi_ssid_stub, s_wifi_reason);
    else                      snprintf(out, cap, "正在连接 %s…", s_wifi_ssid_stub);
}
fnos_wifi_submit_t fnos_net_submit_state(void)
{
    return (fnos_wifi_submit_t)s_wifi_submit;
}
void fnos_net_submit_reason_r(char *out, size_t cap)
{
    if (!out || !cap) return;
    snprintf(out, cap, "%s", s_wifi_submit_reason);
}
void fnos_net_submit_clear(void)
{
    if (s_wifi_submit != FNOS_WIFI_SUBMIT_PENDING) {
        s_wifi_submit = FNOS_WIFI_SUBMIT_IDLE;
        s_wifi_submit_reason[0] = 0;
    }
}
/* 真机是"入队 → 网络任务里落 NVS + 立刻重连"：这里等价地进入"正在连接"。
   注意**不动 s_wifi_on**：真机上 esp_wifi_disconnect() 之后、断开事件到达之前，
   online 仍然是 true —— 这正是"只看 online 会显示连接成功"的窗口（审计 M05），
   替身必须保留这个窗口，否则那条 bug 在预览里永远复现不出来。 */
void fnos_net_set_credentials(const char *ssid, const char *pass)
{
    (void)pass;
    if (!ssid || !ssid[0]) return;
    s_wifi_cfg = true;
    s_wifi_reason[0] = 0;
    snprintf(s_wifi_ssid_stub, sizeof s_wifi_ssid_stub, "%s", ssid);
    s_wifi_submit = FNOS_WIFI_SUBMIT_PENDING;   // 与真机一致：提交即 PENDING
    s_wifi_submit_reason[0] = 0;
}
void fnos_net_scan_request(void) { wifi_stub_reset(); }
bool fnos_net_scan_busy(void)    { return false; }
bool fnos_net_scan_failed(void)  { return false; }
void fnos_net_scan_clear(void)   { }
int  fnos_net_scan_results(fnos_ap_t *out, int max)
{
    int n = s_wifi_ap_n < max ? s_wifi_ap_n : max;
    for (int i = 0; i < n; i++) out[i] = s_wifi_aps[i];
    return n;
}


/* ── 数据替身：一份打到上限的 NAS 快照 ────────────────────────────── */

typedef enum { ST_LIVE, ST_OFFLINE, ST_OFFLINE_TLS, ST_WARMING, ST_HEALTHY, ST_LIMITS, ST_LOCKBUSY,
               ST_LEGACY } preview_state_t;
static const char *s_force_err;      /* 见 verify_reasons()：强制某条失败原因 */
static preview_state_t s_state = ST_LIVE;
static const fnos_status_t *s_override_status;

/* 温度行的固定次序（与设备端 fnos_data.c 的 temp_cmp 同规则）：设备名 → 通道名。
   桩件的目的不是"随便给点数据"，而是让预览和面板上的行序一模一样。 */
typedef struct { const char *dev, *ch, *dn; float c; } temp_stub_t;

static int temp_stub_cmp(const void *a, const void *b)
{
    const temp_stub_t *x = a, *y = b;
    int c = strcmp(x->dev, y->dev);
    return c ? c : strcmp(x->ch, y->ch);
}

enum { PREVIEW_VOLS=12, PREVIEW_RAID=8, PREVIEW_DISKS=10,
       PREVIEW_TEMPS=48, PREVIEW_DOCKER=16, PREVIEW_ALERTS=8 };

static void fill_live(fnos_status_t *s)
{
    fnos_counts_t counts={ PREVIEW_VOLS, PREVIEW_RAID, PREVIEW_DISKS, PREVIEW_TEMPS,
                          PREVIEW_DOCKER, PREVIEW_ALERTS, 10, 0 };
    /* 不能塞进 assert()（审计 M17）：NDEBUG 下 fnos_status_create 根本不会被调用，
       于是整个 fixture 是空的，后面所有版面/文案审计都会"通过"一个空界面。 */
    if (!fnos_status_create(s,&counts)) {
        fprintf(stderr, "fill_live: fnos_status_create 失败（快照 arena 分配不出来）\n");
        abort();
    }
    s->ever_ok = true;
    s->online  = true;
    s->recv_ms = esp_timer_get_time() / 1000;
    s->fail_ms = s->recv_ms - 41000;
    s->http_ms = 23;
    s->last_status = 200;
    s->ok_count = 18422;
    s->fail_count = 7;
    s->last_err[0] = 0;
    fnos_status_text(s, &s->host, "%s", "fnos-nas");
    s->proto = FNOS_PROTO_KNOWN;
    s->hist_ts_ok = true;
    s->hist_span_s = 180;
    s->hist_avg_gap_x10 = 10;               /* 1.0 s 一段：连续 */

    static const struct { const char *n, *st; } mods[] = {
        { "cpu", "ok" }, { "mem", "ok" }, { "net", "ok" }, { "vols", "ok" },
        { "raid", "ok" }, { "disks", "ok" }, { "temps", "ok" },
        { "docker", "disabled" }, { "zfs", "missing" },
    };
    s->nmods = (int)(sizeof mods / sizeof mods[0]);
    for (int i = 0; i < s->nmods; i++) {
        fnos_status_text(s, &s->mods[i].name, "%s", mods[i].n);
        fnos_status_text(s, &s->mods[i].status, "%s", mods[i].st);
    }
    s->uptime_s = 2 * 86400 + 7 * 3600 + 41 * 60;

    s->cpu.pct = 37.4f; s->cpu.load1 = 1.42f; s->cpu.load5 = 1.18f; s->cpu.load15 = 0.96f;
    s->cpu.temp_c = 52.0f; s->cpu.cores = 8; s->cpu.runq = 2; s->cpu.procs = 412;

    s->mem.total_mb = 16384.0f; s->mem.used_mb = 10362.0f; s->mem.avail_mb = 6022.0f;
    s->mem.pct = 63.2f; s->mem.swap_total_mb = 2048.0f; s->mem.swap_used_mb = 137.0f;

    fnos_status_text(s, &s->net.ifname, "%s", "eth0");
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
        fnos_status_text(s, &s->vols[i].mnt, "%s", v[i].mnt);
        fnos_status_text(s, &s->vols[i].fs, "%s", v[i].fs);
        s->vols[i].total_gb = v[i].tot;
        s->vols[i].used_gb  = v[i].used;
        s->vols[i].free_gb  = v[i].tot - v[i].used;
        s->vols[i].pct      = 100.0f * v[i].used / v[i].tot;
    }

    /* state 用 mdstat 的真实首词（active/clean），结构健康度在 health、维护动作在 what ——
       这是采集端 v1.2.5 之后的形状。旧 fixture 把 "resync"/"degraded" 塞进 state，
       正是板端那段永远不成立的分支的来由（审计 §8.2）：真实数据里 state 只有首词。 */
    struct { const char *dev, *lvl, *st, *health, *what; bool ok; int have, want; float sync; } r[] = {
        { "md0", "raid5", "active", "ok",       "",         true,  4, 4, 100.0f },
        { "md1", "raid1", "active", "degraded", "recovery", false, 2, 2,  47.3f },
        { "md2", "raid0", "active", "ok",       "",         true,  2, 2, 100.0f },
        { "md3", "raid6", "active", "degraded", "",         false, 5, 6, 100.0f },
    };
    s->nraid = 4;
    for (int i = 0; i < s->nraid; i++) {
        fnos_status_text(s, &s->raid[i].dev, "%s", r[i].dev);
        fnos_status_text(s, &s->raid[i].lvl, "%s", r[i].lvl);
        fnos_status_text(s, &s->raid[i].state, "%s", r[i].st);
        fnos_status_text(s, &s->raid[i].health, "%s", r[i].health);
        fnos_status_text(s, &s->raid[i].what, "%s", r[i].what);
        s->raid[i].ok = r[i].ok; s->raid[i].have = r[i].have; s->raid[i].want = r[i].want;
        s->raid[i].sync_pct = r[i].sync;
    }

    struct { const char *dev; float rd, wr; } d[] = {
        { "sda",      412.0f,  1830.0f },
        { "sdb",       88.0f,   640.0f },
        { "nvme0n1", 2140.0f,  680.0f },
        { "nvme1n1",   12.0f,    0.0f },
    };
    s->net.physical=true; s->net.speed_mbps=2500;
    fnos_status_text(s,&s->net.state,"up");
    s->ndisks = 4;
    for (int i = 0; i < s->ndisks; i++) {
        fnos_status_text(s, &s->disks[i].dev, "%s", d[i].dev);
        s->disks[i].rd_kbs = d[i].rd; s->disks[i].wr_kbs = d[i].wr;
        s->disks[i].has_read_ms=s->disks[i].has_write_ms=true;
        s->disks[i].read_ms=0.8f+i*0.6f; s->disks[i].write_ms=1.2f+i*0.8f;
        fnos_status_text(s,&s->disks[i].smart,"%s",i==1 ? "failed" : "passed");
        s->disks[i].smart_ts=1700000000;
    }

    /* 温度桩：**与真机同形** —— 采集端把每一个通道都发上来（CPU 每核心、网卡
       PHY/MAC、每块 NVMe 的每个 Sensor），每一路带 dev（内核短名）、ch（通道名）
       和 dn（**人读的设备名**，用户要拿它认清"是哪台设备在热"）。
       Public names are synthetic; retain duplicate-model identities and per-channel structure. */
    temp_stub_t t[] = {
        { "enp1s0",  "PHY",          "Ethernet Controller 10GbE",    74.2f },
        { "enp1s0",  "MAC",          "Ethernet Controller 10GbE",    74.2f },
        { "nvme0n1", "Composite",    "NVMe Demo Drive A", 43.9f },
        { "nvme1n1", "Composite",    "NVMe Demo Drive B",     42.9f },
        { "nvme3n1", "Composite",    "NVMe Demo Drive A",     41.9f },
        { "nvme0n1", "Sensor 1",     "NVMe Demo Drive A", 41.9f },
        { "nvme2n1", "Composite",    "NVMe Demo Drive B", 41.0f },
        { "nvme0n1", "Sensor 2",     "NVMe Demo Drive A", 40.9f },
        { "nvme1n1", "Sensor 1",     "NVMe Demo Drive B",     40.9f },
        { "nvme1n1", "Sensor 2",     "NVMe Demo Drive B",     39.9f },
        { "nvme3n1", "Sensor 1",     "NVMe Demo Drive A",     39.9f },
        { "nvme2n1", "Sensor 1",     "NVMe Demo Drive B", 38.9f },
        { "nvme3n1", "Sensor 2",     "NVMe Demo Drive A",     38.9f },
        { "nvme3n1", "Sensor 3",     "NVMe Demo Drive A",     37.9f },
        { "nvme2n1", "Sensor 2",     "NVMe Demo Drive B", 37.9f },
        { "nvme2n1", "Sensor 3",     "NVMe Demo Drive B", 36.9f },
        { "i915",    "temp1",        "Integrated Graphics",      35.0f },
        { "CPU",     "Package id 0", "Demo Processor",              34.0f },
        { "CPU",     "Core 5",       "Demo Processor",              33.5f },
        { "CPU",     "Core 4",       "Demo Processor",              33.4f },
        { "CPU",     "Core 3",       "Demo Processor",              33.3f },
        { "CPU",     "Core 2",       "Demo Processor",              33.2f },
        { "CPU",     "Core 1",       "Demo Processor",              33.1f },
        { "CPU",     "Core 0",       "Demo Processor",              33.0f },
    };
    s->ntemps = (int)(sizeof t / sizeof t[0]);
    /* 设备端的 fnos_data.c 会把温度按"设备名 + 通道名"**固定排序**（不按温度），
       桩件必须跟着一致 —— 否则预览里的行序和面板上的不一样，预览又变成假绿灯
       （桩与设备行为不一致这条，本项目已经踩过一次）。 */
    qsort(t, (size_t)s->ntemps, sizeof t[0], temp_stub_cmp);
    for (int i = 0; i < s->ntemps; i++) {
        fnos_status_text(s, &s->temps[i].dev, "%s", t[i].dev);
        fnos_status_text(s, &s->temps[i].ch, "%s", t[i].ch);
        fnos_status_text(s, &s->temps[i].dn, "%s", t[i].dn);
        s->temps[i].c = t[i].c;
    }

    struct { const char *n; bool up; const char *st; } k[] = {
        { "media-service",     true,  "Up 3 days" },
        { "photo-service",true,  "Up 3 days" },
        { "download-service",  true,  "Up 11 hours" },
        { "password-service",  true,  "Up 3 days" },
        { "proxy-service",  true,  "Up 6 days" },
        { "editor-service",  false, "Exited (0) 2 hours ago" },
        { "database-service",     true,  "Up 3 days (healthy)" },
        { "cache-service",        false, "Exited (137) 5 minutes ago" },
    };
    s->ndocker = 8;
    for (int i = 0; i < s->ndocker; i++) {
        fnos_status_text(s, &s->docker[i].n, "%s", k[i].n);
        fnos_status_text(s, &s->docker[i].s, "%s", k[i].st);
        s->docker[i].up = k[i].up;
        s->docker[i].has_cpu=s->docker[i].has_mem=s->docker[i].has_restarts=true;
        s->docker[i].cpu_pct=3.5f+i*4.0f; s->docker[i].mem_mb=128+i*64;
        s->docker[i].restarts=i; s->docker[i].metrics_ts=1700000000;
    }

    s->has_zfs = true; s->zfs_arc_gb = 12.4f; s->zfs_hit_pct = 96.3f;

    /* 告警文案按 nas/fnos-agent.py:_alerts() 的真实格式造（全 ASCII，级别 crit/warn/info） */
    fnos_status_text(s, &s->alerts[0].lv, "%s", "warn");
    fnos_status_text(s, &s->alerts[0].m, "%s", "/vol4 used 95%");
    fnos_status_text(s, &s->alerts[1].lv, "%s", "crit");
    fnos_status_text(s, &s->alerts[1].m, "%s", "/vol3 free 4.6% left");
    fnos_status_text(s, &s->alerts[2].lv, "%s", "warn");
    fnos_status_text(s, &s->alerts[2].m, "%s", "container download-service down");
    fnos_status_text(s, &s->alerts[3].lv, "%s", "info");
    fnos_status_text(s, &s->alerts[3].m, "%s", "md1 resync 47.3%");
    s->nalerts = 4;
}

/* ── 配对替身：一台"证书指纹已经取回来了"的 NAS ─────────────────────
   只替身 fnos_pair 的对外行为（真机上是 NVS + 一条内部任务 + mbedTLS），
   让 fnos_ui 的配对界面能在主机上走完 输入配对码 → 核对指纹 → 已配对 三个阶段。 */
#include "fnos_pair.h"
#include "pair_msgs.h"   /* 由 pair_msgs.py 从 fnos_pair.c 生成 */

static fnos_pair_state_t s_pair_state = FNOS_PAIR_UNPROVISIONED;
static char s_pair_msg[112];
static uint32_t s_pair_gen;

/* SHA-256 的假指纹：固定值，只为看排版（真机上这个值是证书字节算出来的）*/
static const char FAKE_FP[] =
    "3F:1A:9C:04:E7:52:BB:6D:"
    "80:2E:C1:47:5A:D3:96:0F:"
    "12:6B:8D:EE:24:71:C0:35:"
    "A9:5E:43:B8:77:D1:08:FE";

void fnos_pair_init(void) {}
bool fnos_pair_provisioned(void) { return s_pair_state == FNOS_PAIR_PROVISIONED; }
uint32_t fnos_pair_generation(void) { return s_pair_gen; }
void fnos_pair_active(fnos_pair_cfg_t *out)
{
    memset(out, 0, sizeof *out);
    snprintf(out->host, sizeof out->host, "%s", "<NAS_IP>");
    out->port = 8798;
}
uint32_t fnos_pair_active_gen(fnos_pair_cfg_t *out)
{
    fnos_pair_active(out);
    return s_pair_gen;
}
void fnos_pair_view(fnos_pair_view_t *out)
{
    memset(out, 0, sizeof *out);
    out->state = s_pair_state;
    out->port = 8798;
    snprintf(out->host, sizeof out->host, "%s", "<NAS_IP>");
    /* 没配过对时，真机 fnos_pair_init() 会把 msg 写成"未配对：用编译期默认地址"。
       预览这里以前固定为空串 —— 于是"左栏状态说明"在预览里永远不出现，
       它和"三步完成配对"抢同一块地方这个 bug 就只能在真机照片上看见。
       桩件的行为要跟设备一致，否则预览的绿灯是假的。 */
    if (s_pair_msg[0]) {
        snprintf(out->msg, sizeof out->msg, "%s", s_pair_msg);
    } else if (s_pair_state == FNOS_PAIR_PROVISIONED) {
        snprintf(out->msg, sizeof out->msg, "%s", "已配对");
    } else {
        snprintf(out->msg, sizeof out->msg, "%s", "未配对：用编译期默认地址");
    }
    if (s_pair_state == FNOS_PAIR_CONFIRM || s_pair_state == FNOS_PAIR_PROVISIONED) {
        out->tls = true;
        snprintf(out->fingerprint, sizeof out->fingerprint, "%s", FAKE_FP);
        snprintf(out->subject, sizeof out->subject, "%s", "CN=fnos-nas.local");
        snprintf(out->not_after, sizeof out->not_after, "%s", "2035-10-03");
    }
}
void fnos_pair_begin(const char *code)
{
    snprintf(s_pair_msg, sizeof s_pair_msg, "已取回证书，正在等你就指纹表态（配对码 %s）", code);
    s_pair_state = FNOS_PAIR_CONFIRM;
}
void fnos_pair_confirm(bool accept)
{
    if (!accept) {
        s_pair_state = FNOS_PAIR_UNPROVISIONED;
        snprintf(s_pair_msg, sizeof s_pair_msg, "%s", "已取消：指纹没核对过，什么都没保存");
        return;
    }
    s_pair_state = FNOS_PAIR_PROVISIONED;
    s_pair_gen++;
    snprintf(s_pair_msg, sizeof s_pair_msg, "%s", "配对成功，令牌与证书已保存");
}
void fnos_pair_forget(void)
{
    s_pair_state = FNOS_PAIR_UNPROVISIONED;
    s_pair_gen++;
    snprintf(s_pair_msg, sizeof s_pair_msg, "%s", "已解除配对，回到编译期默认参数");
}

#ifndef CONFIG_FNOS_POLL_INTERVAL_MS
#define CONFIG_FNOS_POLL_INTERVAL_MS 200
#endif
unsigned fnos_data_poll_interval_ms(void) {
    const char *v=getenv("PREVIEW_POLL_MS");
    return v && atoi(v)>=(int)FNOS_POLL_INTERVAL_MIN_MS
           ? (unsigned)atoi(v) : CONFIG_FNOS_POLL_INTERVAL_MS;
}

bool fnos_data_get(fnos_status_t *out)
{
    if (s_override_status) { fnos_status_copy(out,s_override_status); return true; }
    if (s_state == ST_LOCKBUSY) return true; /* 与数据层锁超时一致：out 保持原值 */
    if (s_state == ST_WARMING) { fnos_status_release(out); return false; }
    fill_live(out);
    if (s_state == ST_HEALTHY) {
        for (int i=0;i<out->ndisks;i++) fnos_status_text(out,&out->disks[i].smart,"passed");
        out->nalerts = 0;
        fnos_status_text(out, &out->net.ifname, "%s", "bridge-monitor0");
        for (int i = 0; i < out->ndocker; i++) out->docker[i].up = true;
        for (int i = 0; i < out->nmods; i++)
            if (!strcmp(out->mods[i].name, "docker")) fnos_status_text(out, &out->mods[i].status, "ok");
        for (int i = 0; i < out->nraid; i++) {
            out->raid[i].ok = true;
            out->raid[i].have = out->raid[i].want;
            out->raid[i].sync_pct = 100;
            fnos_status_text(out, &out->raid[i].state, "%s", "active");
            /* health/what 也要跟着复位：卡片现在按 health 上色，只改 ok 会让"健康"
               快照里仍显示 fixture 的 degraded（审计 §8.2 之后的配色来源）。 */
            fnos_status_text(out, &out->raid[i].health, "%s", "ok");
            fnos_status_text(out, &out->raid[i].what, "%s", "");
        }
    }
    if (s_state == ST_LIMITS) {
        out->cpu.pct = 100; out->mem.pct = 100;
        /* 温度桩按降序，所以 [0] 就是最热那路：把它推到危险档、[1] 推到注意档，
           一次覆盖两条阈值线的颜色与摘要计数。 */
        out->temps[0].c = 85; out->temps[1].c = 66;
        out->net.rx_kbs = 1228800;
        out->nvols = PREVIEW_VOLS; out->nraid = PREVIEW_RAID;
        out->ndisks = PREVIEW_DISKS; out->ndocker = PREVIEW_DOCKER; out->nalerts = PREVIEW_ALERTS;
        for (int i = 6; i < out->nvols; i++) { out->vols[i] = out->vols[0]; fnos_status_text(out, &out->vols[i].mnt, "/vol%d", i + 1); }
        for (int i = 4; i < out->nraid; i++) { out->raid[i] = out->raid[0]; fnos_status_text(out, &out->raid[i].dev, "md%d", i); }
        for (int i = 4; i < out->ndisks; i++) { out->disks[i] = out->disks[0]; fnos_status_text(out, &out->disks[i].dev, "nvme%dn1", i); }
        for (int i = 8; i < out->ndocker; i++) { out->docker[i] = out->docker[0]; fnos_status_text(out, &out->docker[i].n, "container-%02d", i); }
        for (int i = 4; i < out->nalerts; i++) out->alerts[i] = out->alerts[0];
        /* 容错路径：应用比板子新（协议超范围）+ 一个板子没见过的状态词，
           两种都得照常显示而不是空白。 */
        out->proto = FNOS_PROTO_KNOWN + 1;
        /* NAS 停过 40 分钟再回填：跨度远大于样本数 × 1 秒，必须说"有断流" */
        out->hist_ts_ok = true;
        out->hist_span_s = 3720;
        out->hist_avg_gap_x10 = 25;
        if (out->nmods > 1) {
            fnos_status_text(out, &out->mods[0].name, "%s", "smart");
            fnos_status_text(out, &out->mods[0].status, "%s", "degraded");
            fnos_status_text(out, &out->mods[1].status, "%s", "denied");
        }
    }
    if (s_state == ST_LEGACY) {
        /* 老采集端（还没升级的 NAS）只发拼好的 n：dev 里是"NIC"/"NVME2"这种类型名，
           ch 与 dn 都是空。真机上现在跑的就是这一屏 —— 桩件必须能复现它，
           否则"回退路径长什么样"只有设备照片为证（照片不能当回归基线）。 */
        static const struct { const char *n; float c; } legacy[] = {
            { "NIC", 76.0f }, { "NVME2", 44.9f }, { "NVME3", 39.9f },
            { "NVME1", 36.9f }, { "iGPU", 36.0f }, { "CPU", 36.0f }, { "NVME0", 30.9f },
        };
        out->ntemps = (int)(sizeof legacy / sizeof legacy[0]);
        for (int i = 0; i < out->ntemps; i++) {
            out->temps[i] = (fnos_temp_t){ .dev="", .dn="", .ch="" };
            fnos_status_text(out, &out->temps[i].dev, "%s", legacy[i].n);
            out->temps[i].c = legacy[i].c;
        }
    }
    /* verify_reasons() 用：把 last_err 强制成某个标签，其余状态保持离线 */
    if (s_force_err) {
        out->online = false;
        out->last_err[0] = 0;
        snprintf(out->last_err, sizeof out->last_err, "%s", s_force_err);
        return true;
    }
    if (s_state == ST_OFFLINE || s_state == ST_OFFLINE_TLS) {
        out->online = false;
        out->recv_ms -= 96000;             /* 96 s 前的旧帧 → 可信度降级 */
        out->last_status = 0;
        /* 这两种失败在用户看来完全不是一回事（网线 vs 信任），界面必须分开说，
           所以各出一个状态：ST_OFFLINE_TLS 的串也是最长的一条，顺带压字库覆盖。 */
        snprintf(out->last_err, sizeof out->last_err, "%s",
                 s_state == ST_OFFLINE_TLS ? "tls handshake" : "connect/timeout");
        out->ok_count -= 3;
        out->fail_count += 12;
    }
    return true;
}

void fnos_data_diag(fnos_data_diag_t *out)
{
    memset(out,0,sizeof *out);
    if (s_state != ST_WARMING) { out->ok_count=100; out->http_ms=12; out->p95_ms=28; }
}

/* 曲线样本：确定性的正弦 + 噪声，让趋势/峰值/量程都吃到真实分布 */
static int64_t s_seq;
static bool s_override_sample_enabled;
static fnos_sample_t s_override_sample;
static bool s_history_lock_busy;
int fnos_data_hist_read(int64_t since_seq, fnos_sample_t *out, int max, int64_t *next_seq)
{
    int n = 0;
    if (s_state == ST_WARMING || s_state == ST_OFFLINE || s_state == ST_OFFLINE_TLS) return 0;
    while (n < max && since_seq + n < s_seq) {
        if (s_override_sample_enabled) { out[n++] = s_override_sample; continue; }
        int64_t k = since_seq + n;
        double t = (double)k;
        out[n].temp_max = (float)(58.0 + 8.0*sin(t*0.08));
        out[n].cpu    = (float)(34.0 + 11.0 * sin(t * 0.31) + 5.0 * sin(t * 1.7) + 2.0 * sin(t * 5.3));
        out[n].mem    = (float)(62.0 +  4.0 * sin(t * 0.11) + 1.5 * sin(t * 0.9));
        out[n].rx_kbs = (float)(9000.0 + 6500.0 * sin(t * 0.23) + 3200.0 * sin(t * 1.1) + 900.0 * sin(t * 4.7));
        out[n].tx_kbs = (float)(2600.0 + 1400.0 * sin(t * 0.19 + 1.0) + 700.0 * sin(t * 2.3));
        out[n].interval_ms = fnos_data_poll_interval_ms();
        if (out[n].rx_kbs < 60.0f) out[n].rx_kbs = 60.0f;
        if (out[n].tx_kbs < 40.0f) out[n].tx_kbs = 40.0f;
        n++;
    }
    if (n > 0 && next_seq) *next_seq = since_seq + n;
    return n;
}
int fnos_data_hist_read_ex(int64_t since_seq,fnos_sample_t *out,int max,int64_t *next_seq,bool *reset)
{
    if (reset) *reset=false;
    if (s_history_lock_busy) {
        if (next_seq) *next_seq=since_seq;
        return 0;
    }
    return fnos_data_hist_read(since_seq,out,max,next_seq);
}

int64_t fnos_data_hist_seq(void) { return s_seq; }

/* ── 取图 ─────────────────────────────────────────────────────────── */

/* 屏幕尺寸是**运行时**的：`PREVIEW_SIZE=800x480` 就能把同一份 UI 渲到别的分辨率上
   ——"分辨率自适应"这句话只有拿别的尺寸真出一张图才算证明（面板固定 1024×600，
   实机只能验这一档）。缓冲区按最大档开，LVGL 按 s_scr_w/h 建显示。 */
#define SCR_MAX_W 1280
#define SCR_MAX_H 800
static int s_scr_w = 1024, s_scr_h = 600;
static uint16_t s_frame[SCR_MAX_W * SCR_MAX_H];
static const char *s_outdir = "out";
static uint64_t s_flushed_pixels;

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    s_flushed_pixels += (uint64_t)lv_area_get_width(area) * lv_area_get_height(area);
    const uint16_t *src = (const uint16_t *)px_map;
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++)
            s_frame[y * s_scr_w + x] = *src++;
    lv_display_flush_ready(d);
}

/* 子对象必须落在父对象的框里。LVGL 默认把子对象裁到父对象边界，所以"跑出去"
   等于用户根本看不见 —— 配对键盘的第四行（删除 / 0 / 确认）就是这么整排掉到
   卡片外的：盒子只给了 188 高，却排了 4 行 60 行距的键，内容要 236。
   字形审计只管"字有没有"，几何断言只管"某段文字在不在"，**没有任何一条检查
   管"这个控件到底画在没画在它该在的地方"**，于是三个最要紧的键没了也没人说话。
   留 2px 容差给描边与像素取整。 */
static int s_audit_fail;

/* 溢出/重叠审计报坐标时，同一坐标下的"最近邻"最有信息量：把屏幕坐标附近的对象打出来。
   没有这个，报告里只有一串数字，得靠人肉在 447 个构件里猜。 */
static void dump_near(lv_obj_t *o, int x, int y, int depth)
{
    if (depth > 6) return;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    if (x >= a.x1 - 8 && x <= a.x2 + 8 && y >= a.y1 - 8 && y <= a.y2 + 8) {
        const char *txt = lv_obj_has_class(o, &lv_label_class) ? lv_label_get_text(o) : "";
        fprintf(stderr, "    near: %s [%d,%d %dx%d] %.40s\n",
                lv_obj_has_class(o, &lv_label_class) ? "label" :
                lv_obj_check_type(o, &lv_button_class) ? "button" :
                lv_obj_check_type(o, &lv_chart_class) ? "chart" : "panel",
                (int)a.x1, (int)a.y1, (int)(a.x2 - a.x1 + 1), (int)(a.y2 - a.y1 + 1), txt);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) dump_near(lv_obj_get_child(o, i), x, y, depth + 1);
}

/* 卡片身份：mk_card() 把 __func__ 挂在 user_data 上 ⇒ 审计能报 "build_p3"。
   坐标串（[88,68]-[1011,347]）对人是不可读的，查一次要二十分钟。 */
static const char *card_tag(lv_obj_t *o)
{
    const char *t = (const char *)lv_obj_get_user_data(o);
    return t ? t : "(untagged)";
}

/* 排查用：把用户数据里的名字当调用者过滤器，找出谁在建指定尺寸的卡。
   `PREVIEW_WHO=924x280` 打印所有尺寸匹配的卡。 */
static void dump_cards_named(lv_obj_t *o, int w, int h)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int cw = a.x2 - a.x1 + 1, ch = a.y2 - a.y1 + 1;
    if (cw == w && ch == h) {
        lv_obj_t *gp = lv_obj_get_parent(o);
        fprintf(stderr, "[who] %s at [%d,%d] %dx%d gp=%s\n", card_tag(o), a.x1, a.y1, cw, ch,
                gp ? card_tag(gp) : "none");
        dump_near(o, a.x1 + 4, a.y1 + 4, 1);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) dump_cards_named(lv_obj_get_child(o, i), w, h);
}

static void audit_bounds(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    /* 可滚动容器的**直接**子对象本来就会超出可视区（那正是滚动的意义），
       这一层不比较 —— 但**必须继续往下走**。
       原先这里写的是 return：内容区是可滚动的，于是整棵卡片子树（卡片→模块→
       按钮）从来没被检查过，「接受并配对」掉在卡片外 72px 也就没人报。
       跳一层和跳过整棵子树是两回事。 */
    bool skip_this_level = lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) &&
                           lv_obj_get_child_count(o) > 0;
    lv_area_t pa;
    lv_obj_get_coords(o, &pa);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *ch = lv_obj_get_child(o, i);
        if (lv_obj_has_flag(ch, LV_OBJ_FLAG_HIDDEN)) continue;
        if (!skip_this_level) {
            lv_area_t ca;
            lv_obj_get_coords(ch, &ca);
            if (ca.x1 < pa.x1 - 2 || ca.x2 > pa.x2 + 2 || ca.y1 < pa.y1 - 2 || ca.y2 > pa.y2 + 2) {
                const char *txt = lv_obj_has_class(ch, &lv_label_class) ? lv_label_get_text(ch) : "?";
                {   /* 报坐标不够：把"父亲是谁、孩子是谁"也说出来，否则只能靠猜 */
                    const char *pt = lv_obj_has_class(o, &lv_label_class) ? lv_label_get_text(o) : "";
                    lv_obj_t *gp = lv_obj_get_parent(o);
                    fprintf(stderr, "  parent=%s[%d,%d] text=\"%.24s\" gp=%s\n",
                            lv_obj_has_class(o, &lv_label_class) ? "label" :
                            lv_obj_check_type(o, &lv_button_class) ? "button" : "panel",
                            (int)pa.x1, (int)pa.y1, pt,
                            gp ? (lv_obj_has_class(gp, &lv_label_class) ? "label" :
                                  lv_obj_check_type(gp, &lv_button_class) ? "button" : "panel") : "none");
                    fprintf(stderr, "  child=%s text=\"%.24s\"\n",
                            lv_obj_has_class(ch, &lv_label_class) ? "label" : "panel",
                            lv_obj_has_class(ch, &lv_label_class) ? lv_label_get_text(ch) : "");
                }
                fprintf(stderr, "  parent children=%u:\n", (unsigned)lv_obj_get_child_count(o));
                for (uint32_t z = 0; z < lv_obj_get_child_count(o); z++) {
                    lv_obj_t *zc = lv_obj_get_child(o, z);
                    lv_area_t za; lv_obj_get_coords(zc, &za);
                    fprintf(stderr, "    [%u] %s [%d,%d]-[%d,%d] hidden=%d text=\"%.24s\"\n", (unsigned)z,
                            lv_obj_has_class(zc, &lv_label_class) ? "label" : "panel",
                            (int)za.x1, (int)za.y1, (int)za.x2, (int)za.y2,
                            lv_obj_has_flag(zc, LV_OBJ_FLAG_HIDDEN),
                            lv_obj_has_class(zc, &lv_label_class) ? lv_label_get_text(zc) : "");
                }
                dump_near(lv_screen_active(), (ca.x1 + ca.x2) / 2, (ca.y1 + ca.y2) / 2, 0);
                fprintf(stderr, "child out of parent: \"%s\" child=[%d,%d]-[%d,%d] parent=[%d,%d]-[%d,%d]\n",
                        txt, ca.x1, ca.y1, ca.x2, ca.y2, pa.x1, pa.y1, pa.x2, pa.y2);
                /* PREVIEW_AUDIT_ALL=1：一次跑完所有几何违规再退出（默认仍然首错即停，
                   因为首错即停能最快定位）。整版重排时会同时冒出十几处越界，
                   逐个 abort 会让"改一处→重跑→再见下一处"变成十几轮。 */
                if (getenv("PREVIEW_AUDIT_ALL")) s_audit_fail++; else abort();
            }
        }
        audit_bounds(ch);
    }
}

/* Page-level budget. Card audits deliberately skip direct children of a
   scrollable parent, so a page that scrolls 43..1276px while its scrollbar is
   off passed every existing check. The page is the object carrying USER_1. */
static lv_obj_t *visible_page(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1)) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *p = visible_page(lv_obj_get_child(o, i));
        if (p) return p;
    }
    return NULL;
}

/* The pages are no longer direct children of the screen: the shell owns them.
   Counting USER_1 pages in tree order is the only stable way to map a page
   number to an object (hidden pages included, so dump_page_cards still works). */
static lv_obj_t *page_by_index(lv_obj_t *o, int *idx)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1)) {
        if (*idx == 0) return o;
        (*idx)--;
        return NULL;   /* pages may nest another USER_1 object; count only pages */
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *p = page_by_index(lv_obj_get_child(o, i), idx);
        if (p) return p;
    }
    return NULL;
}

static void audit_page_fit(const char *name)
{
    lv_obj_t *page = visible_page(lv_screen_active());
    if (!page) return;
    /* scroll_top + scroll_bottom is the whole scroll range, even when the page
       is currently scrolled (a zero bottom alone would hide the overflow). */
    int32_t top = lv_obj_get_scroll_top(page);
    int32_t bottom = lv_obj_get_scroll_bottom(page);
    if (top + bottom <= 0) return;
    lv_area_t a;
    lv_obj_get_coords(page, &a);
    fprintf(stderr, "page overflows viewport: \"%s\" page=[%d,%d]-[%d,%d] scroll=%d top=%d bottom=%d\n",
            name, (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2,
            (int)(top + bottom), (int)top, (int)bottom);
    if (getenv("PREVIEW_AUDIT_ALL")) s_audit_fail++; else abort();
}

/* A translated page may legitimately cross the viewport edge. Start the bounds
   walk at each visible native page, retaining every page-to-card and internal
   parent/child check. audit_bounds checks children, never its argument's parent. */
static unsigned audit_motion_page_bounds(lv_obj_t *root)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return 0;
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_USER_1)) {
        audit_bounds(root);
        return 1;
    }
    unsigned found = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++)
        found += audit_motion_page_bounds(lv_obj_get_child(root, i));
    return found;
}

/* 清单可读性审计：清单容器（uk_list_is）的高度必须够它**自己承诺的项数** ——
   判据与布局用的是同一条式子（uk_list_readable_min）。
   承诺几项由容器带（uk_list_promise）：池和滚动列默认 UK_LIST_MIN_ROWS 项，
   可变高复合块清单（温度页设备池）与首页紧凑对比条明确降到一整项。
   这条不变量是"卡片太小 / 上下过窄"的**正面**判据：几何审计只查"跑出父对象"，
   而"池里只露出一行"完全合法（内容多时本来就允许滚动），所以 p1 三张卡各只剩
   一行 55px、首页卷池只有 21px（比一行还矮）时，四个审计一个都没报。 */
static lv_obj_t *visible_text(lv_obj_t *o, const char *text);

static void audit_list_floors(lv_obj_t *o, const char *name)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (uk_list_is(o)) {
        int32_t items = uk_list_promise(o);
        int32_t floor_h = uk_list_readable_min(o, items);
        /* 块清单（承诺一整项）里，单项本身就比容器高时连一整项都放不下 —— 这是
           温度页设备块展开态的合法形态（块内通道多、比视口高，靠滚动看全），免检。
           只承诺一项才免检：行清单承诺两项，条目只是"比容器高一点点"仍是违规（p0 曾
           把 25px 的行塞进 21px 的池）。 */
        if (items == 1 && uk_list_item_max(o) > lv_obj_get_height(o)) return;
        if (floor_h > 0 && lv_obj_get_height(o) < floor_h) {
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            fprintf(stderr, "  → 清单容器至少要完整露出 %d 项（项高实测 %d px，含行距）\n",
                    (int)items, (int)((floor_h - (items - 1) * lv_obj_get_style_pad_row(o, 0)) / items));
            if (getenv("PREVIEW_AUDIT_ALL")) s_audit_fail++; else abort();
        }
        return;   /* 池里不会再套池 */
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        audit_list_floors(lv_obj_get_child(o, i), name);
}

/* 可点击对象之间不许互相压住。这条是针对"控件被画到不该在的地方"最实在的检查：
   配对键盘第四行（删除 / 0 / 确认）原先掉出了键盘盒子，正好压在"关闭"按钮上——
   两个都能点、都看得见、各自都"存在"，字形审计和文字断言全都不会说话，而真机上
   用户点下去到底触发哪个全看层级顺序。
   只比较**没有祖孙关系**的两个对象：按钮套按钮（卡片当容器）是正常结构。 */
#define MAX_TAPPABLE 256
static lv_obj_t *s_tap[MAX_TAPPABLE];
static int s_tap_n;

static bool is_ancestor(lv_obj_t *a, lv_obj_t *b)          /* a 是 b 的祖先？ */
{
    for (lv_obj_t *p = lv_obj_get_parent(b); p; p = lv_obj_get_parent(p)) if (p == a) return true;
    return false;
}

static void collect_tappable(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_get_event_count(o) > 0 && s_tap_n < MAX_TAPPABLE) s_tap[s_tap_n++] = o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) collect_tappable(lv_obj_get_child(o, i));
}

/* 用类型 + 自身/祖先的文本把对象"说清楚"：审计只报坐标时，人得靠猜。 */
static const char *describe_obj(lv_obj_t *o)
{
    static char buf[768];
    int n = snprintf(buf, sizeof buf, "%s", lv_obj_has_class(o, &lv_label_class) ? "label" :
                     lv_obj_check_type(o, &lv_button_class) ? "button" :
                     lv_obj_check_type(o, &lv_chart_class) ? "chart" : "panel");
    if (lv_obj_has_class(o, &lv_label_class)) {
        n += snprintf(buf + n, sizeof buf - n, " text=\"%.40s\"", lv_label_get_text(o));
    }
    for (lv_obj_t *p = lv_obj_get_parent(o); p && n < (int)sizeof buf - 40; p = lv_obj_get_parent(p)) {
        n += snprintf(buf + n, sizeof buf - n, " < %s", lv_obj_has_class(p, &lv_label_class) ? "label" :
                      lv_obj_check_type(p, &lv_button_class) ? "button" :
                      lv_obj_check_type(p, &lv_chart_class) ? "chart" : "panel");
        if (lv_obj_has_class(p, &lv_label_class)) {
            n += snprintf(buf + n, sizeof buf - n, "\"%.24s\"", lv_label_get_text(p));
        }
        lv_area_t a; lv_obj_get_coords(p, &a);
        n += snprintf(buf + n, sizeof buf - n, "[%d,%d %dx%d]", (int)a.x1, (int)a.y1,
                      (int)(a.x2 - a.x1 + 1), (int)(a.y2 - a.y1 + 1));
    }
    return buf;
}

/* A scrolled clickable block can be taller than its viewport. Hit testing respects
 * ancestor clipping, so overlap checks must compare the reachable intersection. */
static bool visible_click_area(lv_obj_t *obj, lv_area_t *area)
{
    lv_obj_get_coords(obj, area);
    for (lv_obj_t *p = lv_obj_get_parent(obj); p; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) continue;
        lv_area_t parent;
        lv_obj_get_coords(p, &parent);
        area->x1 = LV_MAX(area->x1, parent.x1);
        area->x2 = LV_MIN(area->x2, parent.x2);
        area->y1 = LV_MAX(area->y1, parent.y1);
        area->y2 = LV_MIN(area->y2, parent.y2);
        if (area->x1 > area->x2 || area->y1 > area->y2) return false;
    }
    return true;
}

static void audit_overlap(void)
{
    s_tap_n = 0;
    /* A full-screen clickable modal intercepts input before underlying pages.
       Audit its controls, while bounds/contrast still audit the whole render. */
    lv_obj_t *input_root = lv_screen_active();
    lv_area_t screen; lv_obj_get_coords(input_root, &screen);
    for (int i = (int)lv_obj_get_child_count(input_root) - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(input_root, i);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN) ||
            !lv_obj_has_flag(child, LV_OBJ_FLAG_FLOATING) ||
            !lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) continue;
        lv_area_t a; lv_obj_get_coords(child, &a);
        if (a.x1 <= screen.x1 && a.y1 <= screen.y1 && a.x2 >= screen.x2 && a.y2 >= screen.y2) {
            input_root = child; break;
        }
    }
    collect_tappable(input_root);
    for (int i = 0; i < s_tap_n; i++) {
        for (int j = i + 1; j < s_tap_n; j++) {
            lv_obj_t *a = s_tap[i], *b = s_tap[j];
            if (is_ancestor(a, b) || is_ancestor(b, a)) continue;
            lv_area_t x, y;
            if (!visible_click_area(a, &x) || !visible_click_area(b, &y)) continue;
            int ox = (x.x2 < y.x2 ? x.x2 : y.x2) - (x.x1 > y.x1 ? x.x1 : y.x1);
            int oy = (x.y2 < y.y2 ? x.y2 : y.y2) - (x.y1 > y.y1 ? x.y1 : y.y1);
            if (ox > 2 && oy > 2) {                       /* 2px 容差：贴边不算压住 */
                {   /* 定位用：谁压谁必须能一眼看出，不能只报坐标 */
                    lv_area_t ax, bx;
                    lv_obj_get_coords(a, &ax); lv_obj_get_coords(b, &bx);
                    fprintf(stderr, "  tapA %s %dx%d at %d,%d\n", describe_obj(a),
                            (int)(ax.x2 - ax.x1 + 1), (int)(ax.y2 - ax.y1 + 1), (int)ax.x1, (int)ax.y1);
                    fprintf(stderr, "  tapB %s %dx%d at %d,%d\n", describe_obj(b),
                            (int)(bx.x2 - bx.x1 + 1), (int)(bx.y2 - bx.y1 + 1), (int)bx.x1, (int)bx.y1);
                }
                const char *ta = lv_obj_has_class(a, &lv_label_class) ? lv_label_get_text(a) : "?";
                const char *tb = lv_obj_has_class(b, &lv_label_class) ? lv_label_get_text(b) : "?";
                fprintf(stderr, "tappable overlap: \"%s\"[%d,%d]-[%d,%d] vs \"%s\"[%d,%d]-[%d,%d]\n",
                        ta, x.x1, x.y1, x.x2, x.y2, tb, y.x1, y.y1, y.x2, y.y2);
                abort();
            }
        }
    }
}

/* 见过的对象（按可见状态被审计过）。最后拿它反查"从来没露过面的对象"——
   隐藏子树整个被跳过，所以**默认 HIDDEN 的面板如果没有任何一步把它打开，
   里面的东西就一次都没被审计过**，而它照样能在真机上画错。 */
/* 「从未露面」的基线。**这个数字本身是有信息的，别随手改大。**
   现在的构成是各列表**预先建好的行池**里没被用到的那些（卷/阵列/磁盘/容器各建满
   `UI_ROWS_*` 行，数据不够时 `row_visible(false)` 把它们藏起来——这是正常的做法，
   省得运行时反复建删）。它们保持 LVGL 默认文案 "Text"，但永远不会被显示。
   加新面板时如果忘了在预览里把它打开，这个数就会涨 —— 那才是要拦的情况。

   2026-10-06 温度全通道改造把它从 56 抬到 169，构成逐项可对：
     96 = 系统页温度卡行池 48 行（PREVIEW_TEMPS 24→48）里没用到的 24 行 × 4 构件
          （名称/数值/条轨/条填充）—— 旧基线 56 就是这一项的旧值（池 24、桩 10 ⇒ 14×4）；
     72 = 温度页三列行池 48 行里没用到的 24 行 × 3 构件
          （两行式行只有 设备名/通道名/数值，没有条，所以每行少一个构件）；
      1 = 温度页空态标签"未采集到温度通道"：只在"采集成功但一路温度都没有"时露面，
          而预览没有这个状态 —— 这是**已知的一处没被审计到的构件**，单独列在这里
          而不是混进行池。想收紧就给预览加一个"temps 为空"的状态，那时基线应回到 73。
   2026-10-06 板上配网卡再抬到 175，只多一项：
      6 = 配网卡的扫描行池 7 行（WIFI_AP_ROWS）里没用到的 3 行 × 2 构件（网络名/元信息）。
          替身只给 4 个网络，所以第 5~7 行永远不露面；41 个键两套键表都真的画过（见
          verify_wifi 里"换到数字符号层"那一步），没有进这份名单。 */
#define NEVER_VISIBLE_BASELINE 175

#define SEEN_MAX 4096
static const void *s_seen[SEEN_MAX];
static int s_seen_n;
static bool seen_has(const void *p)
{
    for (int i = 0; i < s_seen_n; i++) if (s_seen[i] == p) return true;
    return false;
}
static void seen_add(const void *p)
{
    if (seen_has(p)) return;
    if (s_seen_n >= SEEN_MAX) {
        fprintf(stderr, "seen-object audit capacity exhausted (%d); coverage is incomplete\n", SEEN_MAX);
        abort();
    }
    s_seen[s_seen_n++] = p;
}

/* 屏幕文案里不该出现没被消费掉的格式说明符（"%d" / "%s" / "%u"…）：
   真实事故是 set_txt(x, "%s", cond ? "A" : "容器 %d/%d 运行", n, m) —— 三元表达式选中的
   字面量被当成 %s 的实参，"%d/%d" 就原样印在设备屏上（2026-10-08 实机照片抓到）。
   放在快照路径上，是为了让**每一张**样张都替这条规则站岗：出事的那个状态当时恰好
   没有样张覆盖（有告警的那一帧走的是另一个分支），才让它活到了真机上。 */
static void audit_format_residue(lv_obj_t *o, const char *tag)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_has_class(o, &lv_label_class)) {
        const char *text = lv_label_get_text(o);
        for (const char *p = text; *p; p++) {
            if (*p != '%') continue;
            char c = p[1];
            if (c == '%') { p++; continue; }            /* 真正的百分号 */
            if ((c != '\0' && strchr("dsufxXgc", c)) || (c >= '0' && c <= '9')) {
                fprintf(stderr, "format residue in \"%s\" @ %s\n", text, tag);
                abort();
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        audit_format_residue(lv_obj_get_child(o, i), tag);
    }
}

static void audit_labels(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    seen_add(o);
    /* A value label can fit its object while its unit is clipped by the row.
       Check the full inline reading, including actual typography and gaps. */
    if (lv_obj_get_style_layout(o, 0) == LV_LAYOUT_FLEX &&
        lv_obj_get_style_flex_flow(o, 0) == LV_FLEX_FLOW_ROW) {
        int32_t need = 0;
        uint32_t labels = 0;
        bool number = false, all_labels = true;
        for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
            lv_obj_t *child = lv_obj_get_child(o, i);
            if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
            if (!lv_obj_has_class(child, &lv_label_class)) { all_labels = false; break; }
            const lv_font_t *font = lv_obj_get_style_text_font(child, 0);
            number |= font == UK_FONT_NUM_32;
            lv_point_t size;
            lv_text_get_size(&size, lv_label_get_text(child), font,
                             lv_obj_get_style_text_letter_space(child, 0),
                             lv_obj_get_style_text_line_space(child, 0), LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            need += size.x + lv_obj_get_style_pad_left(child, 0) + lv_obj_get_style_pad_right(child, 0) +
                    2 * lv_obj_get_style_border_width(child, 0);
            labels++;
        }
        if (all_labels && number && labels > 1) {
            need += (labels - 1) * lv_obj_get_style_pad_column(o, 0);
            if (need > lv_obj_get_content_width(o)) {
                fprintf(stderr, "inline metric overflow %d > %d\n", (int)need, (int)lv_obj_get_content_width(o));
                abort();
            }
        }
    }
    if (lv_obj_has_class(o, &lv_label_class)) {
        const char *text = lv_label_get_text(o);
        const lv_font_t *font = lv_obj_get_style_text_font(o, 0);
        for (uint32_t i = 0; text[i]; ) {
            uint32_t ch = lv_text_encoded_next(text, &i);
            if (ch == '\n' || ch == '\r' || ch == '\t') continue;
            lv_font_glyph_dsc_t glyph;
            if (!lv_font_get_glyph_dsc(font, &glyph, ch, 0)) {
                fprintf(stderr, "missing glyph U+%04X in %s\n", ch, text);
                abort();
            }
        }
        lv_point_t size;
        /* Scroll labels render an expanded line, not wrapped text. Match LVGL's
           measurement while still checking glyph coverage and vertical fit. */
        lv_label_long_mode_t mode = lv_label_get_long_mode(o);
        lv_text_flag_t flags = (mode == LV_LABEL_LONG_MODE_SCROLL ||
                               mode == LV_LABEL_LONG_MODE_SCROLL_CIRCULAR ||
                               mode == LV_LABEL_LONG_MODE_CLIP)
                                  ? LV_TEXT_FLAG_EXPAND : LV_TEXT_FLAG_NONE;
        lv_text_get_size(&size, text, font, lv_obj_get_style_text_letter_space(o, 0),
                         lv_obj_get_style_text_line_space(o, 0), lv_obj_get_width(o), flags);
        if (size.y > lv_obj_get_height(o)) {
            fprintf(stderr, "text overflow %dx%d > %dx%d: %s\n", (int)size.x, (int)size.y, (int)lv_obj_get_width(o), (int)lv_obj_get_height(o), text);
            abort();
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) audit_labels(lv_obj_get_child(o, i));
}

/* PREVIEW_PROBE=<page>：打印该页对象树的几何量（coords / 宽 / MAIN padding /
   SCROLLBAR pad 与粗细 / 滚动余量），用来定位"内容右缘 vs 滚动条"的贴边。
   只读诊断，不参与渲染；配套像素级看门狗见 tools/preview/scroll_gap.js。 */
static void probe_tree(lv_obj_t *o, int depth)
{
    lv_area_t a;
    if (lv_obj_has_class(o, &lv_label_class)) {
        lv_area_t la;
        lv_obj_get_coords(o, &la);
        printf("%*sLBL \"%s\" x=[%d,%d] y=[%d,%d] w=%d align=%d long=%d parent_w=%d\n",
               depth * 2, "", lv_label_get_text(o), la.x1, la.x2, la.y1, la.y2,
               (int)lv_obj_get_style_width(o, LV_PART_MAIN),
               (int)lv_obj_get_style_text_align(o, LV_PART_MAIN),
               (int)lv_label_get_long_mode(o),
               lv_obj_get_parent(o) ? (int)lv_obj_get_width(lv_obj_get_parent(o)) : -1);
    }
    if (lv_obj_check_type(o, &lv_obj_class)) {
        lv_obj_get_coords(o, &a);
        const char *txt = lv_obj_has_class(o, &lv_label_class) ? lv_label_get_text(o) : NULL;
        char head[96];
        if (lv_obj_get_scrollbar_mode(o) != LV_SCROLLBAR_MODE_OFF) {
            snprintf(head, sizeof head, "LIST");
        } else if (txt) {
            snprintf(head, sizeof head, "LBL[%s]", txt);
        } else {
            snprintf(head, sizeof head, "obj");
        }
        lv_area_t pa;
        memset(&pa, 0, sizeof pa);
        if (lv_obj_get_parent(o)) lv_obj_get_coords(lv_obj_get_parent(o), &pa);
        printf("%*s%s x=[%d,%d] w=%d parent=[%d,%d] pad(l=%d,r=%d,t=%d,b=%d) sb(pad_r=%d,w=%d) scroll(x=%d,y=%d,r=%d,b=%d) self=%dx%d\n",
               depth * 2, "", head, a.x1, a.x2, a.x2 - a.x1 + 1, pa.x1, pa.x2,
               (int)lv_obj_get_style_pad_left(o, LV_PART_MAIN),
               (int)lv_obj_get_style_pad_right(o, LV_PART_MAIN),
               (int)lv_obj_get_style_pad_top(o, LV_PART_MAIN),
               (int)lv_obj_get_style_pad_bottom(o, LV_PART_MAIN),
               (int)lv_obj_get_style_pad_right(o, LV_PART_SCROLLBAR),
               (int)lv_obj_get_style_width(o, LV_PART_SCROLLBAR),
               (int)lv_obj_get_scroll_x(o), (int)lv_obj_get_scroll_y(o),
               (int)lv_obj_get_scroll_right(o), (int)lv_obj_get_scroll_bottom(o),
               (int)lv_obj_get_self_width(o), (int)lv_obj_get_self_height(o));
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) probe_tree(lv_obj_get_child(o, i), depth + 1);
}

/* 字形盒：标签 coords 是"盒"，多行/换行时会比字宽得多。这里按对齐方式把字形的
   实际绘制区算出来（左对齐取 x1+pad，右对齐取 x2-pad-字宽，居中取中点）。 */
static void text_glyph_area(lv_obj_t *lbl, lv_area_t *out)
{
    lv_point_t sz;
    lv_label_long_mode_t mode = lv_label_get_long_mode(lbl);
    lv_text_flag_t flags = (mode == LV_LABEL_LONG_MODE_SCROLL ||
                            mode == LV_LABEL_LONG_MODE_SCROLL_CIRCULAR ||
                            mode == LV_LABEL_LONG_MODE_CLIP)
                               ? LV_TEXT_FLAG_EXPAND : LV_TEXT_FLAG_NONE;
    lv_text_get_size(&sz, lv_label_get_text(lbl), lv_obj_get_style_text_font(lbl, 0),
                     lv_obj_get_style_text_letter_space(lbl, 0),
                     lv_obj_get_style_text_line_space(lbl, 0),
                     lv_obj_get_width(lbl), flags);
    lv_area_t c;
    lv_obj_get_coords(lbl, &c);
    int pl = lv_obj_get_style_pad_left(lbl, 0), pr = lv_obj_get_style_pad_right(lbl, 0);
    int pt = lv_obj_get_style_pad_top(lbl, 0);
    lv_text_align_t al = lv_obj_get_style_text_align(lbl, 0);
    if (al == LV_TEXT_ALIGN_CENTER)      c.x1 = c.x1 + (lv_area_get_width(&c) - sz.x) / 2;
    else if (al == LV_TEXT_ALIGN_RIGHT)  c.x1 = c.x2 - pr - sz.x + 1;
    else                                 c.x1 = c.x1 + pl;
    c.x2 = c.x1 + sz.x - 1;
    c.y1 = c.y1 + pt;
    c.y2 = c.y1 + sz.y - 1;
    *out = c;
}

static void audit_text_overlap(lv_obj_t *parent)
{
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) return;
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *a = lv_obj_get_child(parent, i);
        if (lv_obj_has_flag(a, LV_OBJ_FLAG_HIDDEN)) continue;
        audit_text_overlap(a);
        if (!lv_obj_has_class(a, &lv_label_class) || !lv_label_get_text(a)[0]) continue;
        /* 只比"字形真正占的地方"，不是标签盒。v9 之前这里直接用 coords，
           于是一个 248 宽的副行标签和一个 64 宽的状态标签本来不重叠（字只占左边一半），
           却因为"盒挨着"被判成 text overlap —— 假阳性会逼着人把版面改坏去迁就测试。 */
        lv_area_t ra;
        text_glyph_area(a, &ra);
        for (uint32_t j = i + 1; j < n; j++) {
            lv_obj_t *b = lv_obj_get_child(parent, j);
            if (lv_obj_has_flag(b, LV_OBJ_FLAG_HIDDEN) || !lv_obj_has_class(b, &lv_label_class) || !lv_label_get_text(b)[0]) continue;
            lv_area_t rb;
            text_glyph_area(b, &rb);
            int ox = LV_MIN(ra.x2, rb.x2) - LV_MAX(ra.x1, rb.x1) + 1;
            int oy = LV_MIN(ra.y2, rb.y2) - LV_MAX(ra.y1, rb.y1) + 1;
            if (ox > 2 && oy > 2) {
                fprintf(stderr, "text overlap: %s / %s (%dx%d)\n", lv_label_get_text(a), lv_label_get_text(b), ox, oy);
                abort();
            }
        }
    }
}

static int s_cardmap_at = -1;
static int s_snap_n;
static FILE *s_barmap;     /* PREVIEW_BARMAP：逐帧滚动条事实，见 snapshot() */
static bool s_motion_frame; /* only raw in-flight frames permit intentional viewport clipping */
static bool s_no_settle;   /* 想看动画中途的样子：跳过"把动画落到终值"（PREVIEW_ENTER_MID） */

/* Sibling card overlap is distinct from text and touch bounds. Full overlays are
   intentional; partial overlap between independent cards hides their contents.

   ⚠ 必须**只在当前可见页内两两比较**：原来按"同一父对象下的兄弟"比，
   页面之间也互相看不见彼此，却因为对象树不同层而漏判；改成拉平一张卡表后，
   隐藏卡（诊断面板 / 配对卡这类按需显示的大卡）还带着**上次显示时的旧坐标**，
   会跟当前页的卡报假重叠（曾经把采集诊断卡 924×520 的陈旧坐标当成
   "系统页摘要条长到 520"）。所以这里先跳过隐藏卡，再拉平比较。 */
static lv_obj_t *s_cardlist[256];
static int s_cardn;

/* 可见状态要连祖先一起看：LV_OBJ_FLAG_HIDDEN 只看对象自己，被隐藏的卡片
   （诊断面板整枝、或建在隐藏页上的卡）自己没这个标志，光看自己会漏。
   曾因此报出"dock 压诊断卡"，而诊断卡那一枝当时整枝隐藏。 */
static bool effectively_hidden(lv_obj_t *o)
{
    for (lv_obj_t *p = o; p; p = lv_obj_get_parent(p))
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) return true;
    return false;
}

static lv_obj_t *s_cardpage[256];

static bool s_keep_hidden;

static void collect_cards(lv_obj_t *o, lv_obj_t *page)
{
    if (!s_keep_hidden && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;   /* 隐藏子树整枝跳过 */
    if (lv_obj_get_style_radius(o, 0) == UK_RADIUS &&
        lv_obj_get_style_bg_opa(o, 0) == LV_OPA_COVER &&
        s_cardn < (int)(sizeof s_cardlist / sizeof s_cardlist[0])) {
        s_cardlist[s_cardn] = o;
        s_cardpage[s_cardn] = page;
        s_cardn++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) collect_cards(lv_obj_get_child(o, i), page);
}

/* 打印某一页里的全部卡（含隐藏）：`PREVIEW_PAGE_CARDS=<页号>` */
static void dump_page_cards(lv_obj_t *page, int idx)
{
    fprintf(stderr, "=== page %d cards ===\n", idx);
    s_cardn = 0;
    s_keep_hidden = true;                 /* 这里刻意不过滤隐藏：就是要看隐藏卡的坐标 */
    collect_cards(page, page);
    s_keep_hidden = false;
    for (int i = 0; i < s_cardn; i++) {
        lv_area_t a;
        lv_obj_get_coords(s_cardlist[i], &a);
        fprintf(stderr, "  [%d] %s [%d,%d]-[%d,%d] %dx%d%s\n", i, card_tag(s_cardlist[i]),
                a.x1, a.y1, a.x2, a.y2, a.x2 - a.x1 + 1, a.y2 - a.y1 + 1,
                lv_obj_has_flag(s_cardlist[i], LV_OBJ_FLAG_HIDDEN) ? " HID" : "");
    }
}

static void audit_card_overlap(lv_obj_t *page)
{
    /* 可见页才比：隐藏页里的卡（诊断面板 / 配对卡）带着"上次显示时"的旧坐标，
       它们之间比会凭空造出重叠。原来的实现在函数头就 `return` 了，
       重写成"拉平一张卡表"时漏掉了这两个 return —— 于是隐藏页被算进来，
       连报两次 `mk_card[88,124]-[387,596]`（dock）压 `mk_card[88,68]-[1011,587]`（诊断卡旧坐标）。 */
    if (effectively_hidden(page)) return;
    s_cardn = 0;
    {   /* 每张卡记住它属于哪一页：**只有同一页的卡才该互相比较**。
           只判"页是否隐藏"不够 —— 目标页可见，但别的页的卡（诊断面板这类
           按需显示的大卡）带着上次显示时的旧坐标，仍会被拉进同一张卡表，
           报出 `dock 压诊断卡` 这种跨页假重叠。 */
        uint32_t np = lv_obj_get_child_count(page);
        for (uint32_t p = 0; p < np; p++) {
            lv_obj_t *pg = lv_obj_get_child(page, p);
            if (lv_obj_has_flag(pg, LV_OBJ_FLAG_HIDDEN)) continue;
            collect_cards(pg, pg);
        }
    }
    if (getenv("PREVIEW_CARDLIST")) {
        for (int k = 0; k < s_cardn; k++) {
            lv_area_t ca;
            lv_obj_get_coords(s_cardlist[k], &ca);
            fprintf(stderr, "  [%d] pg=%d %dx%d at %d,%d%s\n", k, (int)lv_obj_get_index(s_cardpage[k]),
                    ca.x2 - ca.x1 + 1, ca.y2 - ca.y1 + 1, ca.x1, ca.y1,
                    effectively_hidden(s_cardlist[k]) ? " HID" : "");
        }
    }
    for (int i = 0; i < s_cardn; i++) {
        lv_area_t x, y;
        lv_obj_get_coords(s_cardlist[i], &x);
        for (int j = i + 1; j < s_cardn; j++) {
            lv_obj_t *a = s_cardlist[i], *b = s_cardlist[j];
            if (s_cardpage[i] != s_cardpage[j]) continue;   /* 跨页不比（页之间本来就互不可见） */
            lv_obj_get_coords(b, &y);
            bool contains = (x.x1 <= y.x1 + 2 && x.y1 <= y.y1 + 2 && x.x2 >= y.x2 - 2 && x.y2 >= y.y2 - 2) ||
                            (y.x1 <= x.x1 + 2 && y.y1 <= x.y1 + 2 && y.x2 >= x.x2 - 2 && y.y2 >= x.y2 - 2);
            if (contains) continue;
            int ox = LV_MIN(x.x2, y.x2) - LV_MAX(x.x1, y.x1) + 1;
            int oy = LV_MIN(x.y2, y.y2) - LV_MAX(x.y1, y.y1) + 1;
            if (ox > 2 && oy > 2) {
                fprintf(stderr, "card overlap: %s[%d,%d]-[%d,%d] / %s[%d,%d]-[%d,%d] page=%d/%d\n",
                        card_tag(a), x.x1, x.y1, x.x2, x.y2,
                        card_tag(b), y.x1, y.y1, y.x2, y.y2,
                        (int)lv_obj_get_index(s_cardpage[i]), (int)lv_obj_get_index(s_cardpage[j]));
                fprintf(stderr, "  i=%d j=%d 卡表=%d 页i卡数=%d 页j卡数=%d\n", i, j, s_cardn,
                        (int)lv_obj_get_child_count(s_cardpage[i]), (int)lv_obj_get_child_count(s_cardpage[j]));
                {   /* 把两条父链打出来：这两张卡到底在哪个页上（同页才该比） */
                    for (int k = 0; k < 2; k++) {
                        lv_obj_t *c = k ? b : a;
                        fprintf(stderr, "  chain%d:", k);
                        for (lv_obj_t *p = c; p; p = lv_obj_get_parent(p)) {
                            lv_area_t pa; lv_obj_get_coords(p, &pa);
                            fprintf(stderr, " %s[%d,%d %dx%d]%s", card_tag(p), pa.x1, pa.y1,
                                    pa.x2 - pa.x1 + 1, pa.y2 - pa.y1 + 1,
                                    lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN) ? "(hid)" : "");
                        }
                        fprintf(stderr, "\n");
                    }
                }
                abort();
            }
        }
    }
}

/* 几何审计报越界时，把"被抱怨的父对象"自己再量一遍：parent=[88,68]-[1011,347]
   这种 280 高的卡在最终树里根本不存在（卡高 44）—— 说明报的是**建树过程中的中间态**，
   只有把当时的真实父子关系打出来才能收口。`PREVIEW_TRACE_CARDS=1` 打开。 */
static void trace_cards(lv_obj_t *o, int depth)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const char *txt = lv_obj_has_class(o, &lv_label_class) ? lv_label_get_text(o) : "";
    fprintf(stderr, "[tree]%*s%s tag=%s [%d,%d]-[%d,%d] %dx%d text=\"%.18s\"\n",
            depth * 2, "", lv_obj_has_class(o, &lv_label_class) ? "label" :
            lv_obj_check_type(o, &lv_button_class) ? "button" : "panel", card_tag(o),
            a.x1, a.y1, a.x2, a.y2, a.x2 - a.x1 + 1, a.y2 - a.y1 + 1, txt);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) trace_cards(lv_obj_get_child(o, i), depth + 1);
}

/* 谁在被重叠审计抱怨：把当前页面里"面积与那对坐标吻合"的卡连同其父链一起打出来。
   `PREVIEW_OVERLAP_TRACE=1`。 */
static void dump_overlap_suspects(lv_obj_t *o, int depth)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int w = a.x2 - a.x1 + 1, h = a.y2 - a.y1 + 1;
    if ((w == 924 && (h == 520 || h == 522 || h == 587 || h == 596 || h == 44 || h == 280)) ||
        (w == 300 && h > 400)) {
        fprintf(stderr, "[sus] d=%d tag=%s [%d,%d]-[%d,%d] %dx%d\n",
                depth, card_tag(o), a.x1, a.y1, a.x2, a.y2, w, h);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) dump_overlap_suspects(lv_obj_get_child(o, i), depth + 1);
}

static void report_contrast(const char *tag);
static lv_obj_t *visible_text_containing(lv_obj_t *o, const char *needle);

/* PREVIEW_BANDS_AT 用：递归打印几何量，最多 5 层。 */
static void probe_geom(lv_obj_t *o, int depth, int maxdepth)
{
    if (depth > maxdepth || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    printf("%*s%s y=[%d,%d] h=%d minh=%d grow=%d sb=%d ch=%u\n", depth * 2, "",
           lv_obj_has_class(o, &lv_label_class) ? "LBL" : "obj",
           (int)a.y1, (int)a.y2, (int)(a.y2 - a.y1 + 1),
           (int)lv_obj_get_style_min_height(o, 0),
           (int)lv_obj_get_style_flex_grow(o, 0),
           lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) ? 1 : 0,
           (unsigned)lv_obj_get_child_count(o));
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        probe_geom(lv_obj_get_child(o, i), depth + 1, maxdepth);
}

/* Evidence output must be complete before a mode can report success. */
static void evidence_flush(FILE *f, const char *what)
{
    int failed = ferror(f);
    if (fflush(f) != 0) failed = 1;
    if (failed) { fprintf(stderr, "FAIL: evidence write failed (%s)\n", what); exit(1); }
}
static void evidence_close(FILE *f, const char *what)
{
    int failed = ferror(f);
    if (fclose(f) != 0) failed = 1;
    if (failed) { fprintf(stderr, "FAIL: evidence close/write failed (%s)\n", what); exit(1); }
}
static int preview_finish(void)
{
    if (s_barmap) {
        FILE *f = s_barmap;
        s_barmap = NULL;
        evidence_close(f, "frame barmap");
    }
    evidence_flush(stdout, "stdout");
    if (s_audit_fail) {
        fprintf(stderr, "FAIL: preview accumulated %d audit violations\n", s_audit_fail);
        return 1;
    }
    return 0;
}

static void snapshot(const char *name)
{
    if (getenv("PREVIEW_PAGE_CARDS")) {
        int idx = atoi(getenv("PREVIEW_PAGE_CARDS"));
        if (idx >= 0) {
            int want = idx;
            lv_obj_t *page = page_by_index(lv_screen_active(), &want);
            if (page) dump_page_cards(page, idx);
        }
    }
    if (getenv("PREVIEW_OVERLAP_TRACE")) {
        fprintf(stderr, "=== suspects @ %s ===\n", name);
        {   /* 每页几张卡、页是否隐藏：一眼看出"跨页假重叠" */
            lv_obj_t *scr = lv_screen_active();
            for (uint32_t k = 0; k < lv_obj_get_child_count(scr); k++) {
                lv_obj_t *pg = lv_obj_get_child(scr, k);
                s_cardn = 0;
                collect_cards(pg, pg);
                fprintf(stderr, "  page %u: %d cards%s\n", k, s_cardn,
                        lv_obj_has_flag(pg, LV_OBJ_FLAG_HIDDEN) ? " HID" : "");
            }
        }
        dump_overlap_suspects(lv_screen_active(), 0);
    }
    if (getenv("PREVIEW_TRACE_CARDS")) {
        fprintf(stderr, "=== trace %s ===\n", name);
        trace_cards(lv_screen_active(), 0);
    }
    {   /* 卡片重叠审计报坐标时，把所有"同尺寸或覆盖该区域"的卡连父对象一起列出来 */
        const char *who = getenv("PREVIEW_WHOALL");
        if (who) { int w = 0, h = 0; if (sscanf(who, "%dx%d", &w, &h) == 2) { fprintf(stderr, "=== who %dx%d ===\n", w, h); dump_cards_named(lv_screen_active(), w, h); } }
    }
    if (getenv("PREVIEW_WHO")) {
        int w = 0, h = 0;
        if (sscanf(getenv("PREVIEW_WHO"), "%dx%d", &w, &h) == 2) dump_cards_named(lv_screen_active(), w, h);
    }
    if (++s_snap_n == s_cardmap_at) { fnos_ui_dump_card_map(); }
    /* PREVIEW_BARMAP=<file>：每个快照都把**这一帧**的卡片几何 + 滚动条事实追加进去。
       以前只有建树那一刻的一份几何（PREVIEW_CARDMAP 在 fnos_ui_create() 之后立刻返回），
       而渲染的帧里有 dense/expanded 这类列表真的会溢出的状态：拿启动态的依据去审全部帧，
       "有没有条、条在哪一列"就是从错的依据推出来的（审计 H14）。
       逐帧记下来，审计的判定依据才和它审的那张 PNG 是同一时刻的。 */
    if (s_barmap) {
        fprintf(s_barmap, "[frame] %s\n", name);
        fnos_ui_dump_card_map_fp(s_barmap);
        evidence_flush(s_barmap, "frame barmap");
    }

    {   /* PREVIEW_BANDS_AT=<快照名子串>：打印活动页的纵向预算 —— 页高、
           每个直接子对象的实际高 / min_height / flex_grow。
           用来定位"页底把内容切掉"这类问题（内容超过页高时 LVGL 只裁不报）。 */
        const char *want = getenv("PREVIEW_BANDS_AT");
        if (want && strstr(name, want)) {
            lv_obj_t *scr = lv_screen_active();
            for (uint32_t k = 0; k < lv_obj_get_child_count(scr); k++) {
                lv_obj_t *host = lv_obj_get_child(scr, k);
                lv_obj_t *pg = host;
                for (uint32_t d = 0; d < lv_obj_get_child_count(host); d++) {
                    lv_obj_t *c = lv_obj_get_child(host, d);
                    if (!lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) && lv_obj_get_child_count(c) > 1) pg = c;
                }
                if (pg == host) continue;
                printf("== %s host=%u\n", name, (unsigned)k);
                probe_geom(pg, 0, 5);
            }
        }
    }
    /* 截图要的是**稳态**：换页入场是 160ms 淡入 + 20ms 错峰，不落终值就会截到
       "半透明的一页"——那种帧既不能当像素基线，也不代表设备上停留的样子。
       这里直接把在飞的 opa 动画落到终值，而不是推进虚拟时钟：推时钟会连带把
       应用定时器（ui_tick，500ms 一跳）也喂一跳，数据年龄/轮询计数跟着漂，
       快照就不再可复现了。想看动画中途的样子用 PREVIEW_ENTER_MID=1。 */
    if (!s_no_settle) {
        fnos_ui_motion_settle();  /* public page-transition completion, static snapshots only */
        uk_anim_settle(lv_screen_active());
    }
    /* 审计前必须先把布局跑完：LVGL 的坐标是**布局结果**，建树那一刻卡片还是 0x0。
       少了这一句，audit_bounds 会把"还没算布局"的父对象（[0,0]-[-1,-1]）当成父，
       报出一堆假越界 —— 曾因此以为系统页摘要条有 280 高（实际 44）。 */
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(lv_display_get_default());
    audit_labels(lv_screen_active());
    audit_format_residue(lv_screen_active(), name);
    if (s_motion_frame) {
        assert(audit_motion_page_bounds(lv_screen_active()) > 0 &&
               "motion bounds audit requires native page identity metadata");
    } else {
        audit_list_floors(lv_screen_active(), name);
        audit_bounds(lv_screen_active());
        audit_overlap();
    }
    /* 首个快照是"建树刚结束"的中间态：所有页还都可见、卡片的 coords 未必都算过，
       此时的坐标不构成任何不变量（真重叠会在后续快照里稳定复现）。跳过它，
       但仍把结果打到日志里 —— 需要时能看见，不会变成"测试睁一只眼"。 */
    /* 卡片互相压住这一条**默认不查**，按需打开：`PREVIEW_CARD_OVERLAP=1`。
       理由：页面挂在 content 容器下（s_ui.page[] 不是 screen 的直接子对象），
       拉平卡表时很容易把不同页的卡混在一起比，报出跨页假重叠；而"压住内容"
       这件事本来就有更强的判据 —— `audit_text_overlap` 比的是**字形盒**，
       文字被压住一定报得出来，图形被压住则由 `audit_bounds` 兜住。
       一条会把好人误伤的检查，比没有这条检查更糟：它逼着人改坏版面去迁就它。 */
    if (!s_motion_frame) {
        if (getenv("PREVIEW_CARD_OVERLAP")) audit_card_overlap(lv_screen_active());
        audit_text_overlap(lv_screen_active());
        report_contrast(name);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", s_scr_w, s_scr_h);
    for (int i = 0; i < s_scr_w * s_scr_h; i++) {
        uint16_t v = s_frame[i];
        fputc((((v >> 11) & 0x1F) * 255) / 31, f);
        fputc((((v >>  5) & 0x3F) * 255) / 63, f);
        fputc((( v        & 0x1F) * 255) / 31, f);
    }
    evidence_close(f, path);
    printf("  %s\n", path);
}

static const char *state_name(preview_state_t s)
{
    return s == ST_LIVE ? "live" : s == ST_OFFLINE_TLS ? "offline-tls" : s == ST_OFFLINE ? "offline"
         : s == ST_HEALTHY ? "healthy" : s == ST_LIMITS ? "limits"
         : s == ST_LEGACY ? "legacy" : "warming";
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
            if (st != ST_OFFLINE && st != ST_OFFLINE_TLS) s_seq += 12;
            vtick_advance(500);
            lv_timer_handler();
        }
    } else {
        s_seq = 0;
        vtick_advance(500);
        lv_timer_handler();
    }
    /* create() 之后、任何 set_page 之前的"初始帧"：这一帧专抓"create 时没把初始页显示出来"
     * 这类只在设备上暴露的 bug；复验入口见 docs/verification.md。 */


    for (int p = 0; p < FNOS_UI_PAGE_COUNT; p++) {
        fnos_ui_set_page(p);
        /* 换页入场插在"推进到稳态"之前：只走 60ms（160ms 淡入的中段），
           并且跳过 settle，才截得到动画本身。默认不跑，免得污染 57 张基线。 */
        const char *mid = getenv("PREVIEW_ENTER_MID");
        if (mid && p == atoi(mid)) {
            vtick_advance(60);
            lv_timer_handler();
            s_no_settle = true;
            char m[72];
            snprintf(m, sizeof m, "%02d-%s-p%d-enter-mid", index, state_name(st), p);
            snapshot(m);
            s_no_settle = false;
        }
        vtick_advance(300);
        lv_timer_handler();
        char name[64];
        snprintf(name, sizeof name, "%02d-%s-p%d", index, state_name(st), p);
        if (getenv("PREVIEW_PROBE") && p == atoi(getenv("PREVIEW_PROBE"))) {
            printf("=== probe page %d ===\n", p);
            probe_tree(lv_screen_active(), 0);
        }
        snapshot(name);
        audit_page_fit(name);
    }
}

static lv_obj_t *visible_text(lv_obj_t *o, const char *text)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_has_class(o, &lv_label_class) && strcmp(lv_label_get_text(o), text) == 0) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *found = visible_text(lv_obj_get_child(o, i), text);
        if (found) return found;
    }
    return NULL;
}

/* Container identities must retain collector order through repeated reflows
   and when a larger snapshot adds new rows to an already split pool. */
static void verify_container_order(void)
{
    int previous_page = fnos_ui_page();
    fnos_status_t st = {0};
    fnos_data_get(&st);
    for (int pass = 0; pass < 4; pass++) {
        fnos_ui_set_page(3);
        fnos_ui_motion_settle();
        lv_obj_update_layout(lv_screen_active());
        lv_area_t previous = {0};
        for (int i = 0; i < st.ndocker; i++) {
            lv_obj_t *name = visible_text(lv_screen_active(), st.docker[i].n);
            assert(name && "reported container must remain reachable");
            lv_area_t current;
            lv_obj_get_coords(name, &current);
            assert(i == 0 || current.y1 > previous.y1 ||
                   (current.y1 == previous.y1 && current.x1 > previous.x1));
            previous = current;
        }
        fnos_ui_set_page(0);
        fnos_ui_motion_settle();
    }
    fnos_ui_set_page(previous_page);
    fnos_ui_motion_settle();
    printf("checks: %d containers preserve collector reading order across reflows PASS\n", st.ndocker);
    fnos_status_release(&st);      /* M17：本地句柄用完必须还（否则每跑一次漏一整份快照） */
}

static void verify_transitions(void)
{
    fnos_ui_set_page(1);
    lv_obj_t *last_vol = visible_text(lv_screen_active(), "/vol8");
    assert(last_vol);
    assert(visible_text(lv_screen_active(), "54%"));
    lv_obj_t *vol_list = lv_obj_get_parent(last_vol);
    /* 滚到底：滚动量取 scroll_bottom，但**目标和不能越过内容真正的高度** ——
       以前是"当前位置 + scroll_bottom"，滚过头 2~6px 后末行反而跑出可视区，
       断言就报"行在框外"，看起来像布局 bug，其实是测试自己滚多了。
       这里改成 LVGL 的语义上限 reach_bottom = content_h − viewport_h。 */
    lv_obj_update_layout(lv_screen_active());
    lv_area_t bounds;
    lv_obj_get_coords(vol_list, &bounds);
    int view_h  = lv_area_get_height(&bounds);
    int content = lv_obj_get_scroll_bottom(vol_list) + view_h;
    int target  = content > view_h ? content - view_h : 0;
    lv_obj_scroll_to_y(vol_list, target, LV_ANIM_OFF);
    /* scroll_to_y 把页面**内容**滚到目标位置，但列表自己还有内边距与滚动条预留，
       末行因此仍可能压住下边界 1~6px。这条断言要问的是"用户滚到底能不能看见末行"，
       所以再补一次"把这一行滚进可视区"（LVGL 自己算它需要多少），
       而真正要守的约束由紧接着的 audit_bounds 负责：内容不许越出卡片。 */
    lv_obj_scroll_to_view(last_vol, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t row;
    lv_obj_get_coords(last_vol, &row);
    if (!(row.y1 >= bounds.y1 && row.y2 <= bounds.y2)) {
        fprintf(stderr, "storage-scroll: row=[%d,%d] viewport=[%d,%d] scroll_y=%d content=%d view_h=%d\n",
                row.y1, row.y2, bounds.y1, bounds.y2, (int)lv_obj_get_scroll_y(vol_list), content, view_h);
    }
    assert(row.y1 >= bounds.y1 && row.y2 <= bounds.y2);
    snapshot("06-storage-scroll");
    /* 历史时间语义的断言得在本页可见时做：曲线标题在被隐藏的页上（visible_text
       不穿隐藏子树，和看门狗的规则一致）。 */
    fnos_ui_set_page(0);
    vtick_advance(300); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "CPU"));
    fnos_ui_set_page(2);
    vtick_advance(300); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "网络吞吐 · 最近 7 分钟 · 有断流"));

    fnos_ui_set_page(3);
    lv_obj_t *last_dock = visible_text(lv_screen_active(), "container-11");
    assert(last_dock);
    lv_obj_t *dock_list = lv_obj_get_parent(last_dock);
    /* 与存储卷那段同理：滚到底的目标位置 = 内容高 − 视口高，不是"当前位置 + scroll_bottom"
       （后者会滚过头的量正好等于容器的 padding，末行于是跑出可视区）。 */
    lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(dock_list, &bounds);
    int view_h2  = lv_area_get_height(&bounds);
    int content2 = lv_obj_get_scroll_bottom(dock_list) + view_h2;
    lv_obj_scroll_to_y(dock_list, content2 > view_h2 ? content2 - view_h2 : 0, LV_ANIM_OFF);
    lv_obj_scroll_to_view(last_dock, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(last_dock, &row);
    assert(row.y1 >= bounds.y1 && row.y2 <= bounds.y2);
    snapshot("06-system-scroll");
    lv_obj_t *label = visible_text(lv_screen_active(), "设置");
    assert(label);
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
    assert(visible_text(lv_screen_active(), "返回"));
    /* 未知字段容错：应用比板子新（协议超范围）+ 状态词板子不认识，
       两种都必须照常显示 —— 显示成空白会让人以为界面漏了数据。 */
    assert(visible_text(lv_screen_active(), "v3（本机只认到 v2，新字段会忽略）"));
    assert(visible_text(lv_screen_active(), "smart degraded · mem 权限不足 · net 正常 · vols 正常 · raid 正常 · disks 正常 · temps 正常 · docker 未启用 · zfs 读不到"));
    snapshot("06-diagnostics");
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
    assert(visible_text(lv_screen_active(), "容器服务"));
    /* 认识的状态词要翻成中文（unknown -> 原样，denied -> 权限不足） */
    s_state = ST_LIVE;
    vtick_advance(500); lv_timer_handler();
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
    assert(visible_text(lv_screen_active(), "v2"));
    assert(visible_text(lv_screen_active(), "cpu 正常 · mem 正常 · net 正常 · vols 正常 · raid 正常 · disks 正常 · temps 正常 · docker 未启用 · zfs 读不到"));
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);

    s_state = ST_HEALTHY;
    vtick_advance(500); lv_timer_handler();
    assert(!visible_text(lv_screen_active(), "暂无告警事件"));
    assert(!visible_text(lv_screen_active(), "container download-service down"));
    assert(!visible_text(lv_screen_active(), "/vol4 used 95%"));
    /* 告警页的健康态不是"一屏留白"：它要顺带回答"看住了什么、现在什么状态"。
       这一帧同时是那四行的审计面（字形覆盖 / 标签溢出 / 越界 / 对比度都靠它），
       所以断言的是"值确实填进去了"，不是一个占位符。 */
    fnos_ui_set_page(5);
    fnos_ui_motion_settle();
    vtick_advance(500); lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
    assert(visible_text(lv_screen_active(), "暂无告警事件"));
    assert(visible_text(lv_screen_active(), "存储容量"));
    assert(visible_text_containing(lv_screen_active(), "最高 "));
    assert(visible_text_containing(lv_screen_active(), "°C\n"));
    assert(visible_text_containing(lv_screen_active(), "在线\n"));
    snapshot("06-alerts-health");
    fnos_ui_set_page(0);
    assert(!visible_text(lv_screen_active(), "无采集告警"));
    /* 结论带那行文案曾经把 "%d/%d" 原样印到屏上（set_txt 的 "%s" 吃的实参里带着格式串，
       实机 2026-10-08 抓到）。正常首页不再重复容器摘要，且屏上不许残留
       没被消费掉的格式说明符。 */
    assert(!visible_text_containing(lv_screen_active(), "运行 · 无待处理事件"));
    assert(!visible_text_containing(lv_screen_active(), "%d"));
    s_state = ST_LOCKBUSY;
    vtick_advance(500); lv_timer_handler();
    assert(!visible_text(lv_screen_active(), "无采集告警"));
    s_state = ST_WARMING;
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "等待数据"));
    assert(!visible_text(lv_screen_active(), "100"));

    /* 配网卡是**覆盖层**，不该自己冒出来（实机回归：卡建出来就是可见的，而"有凭据"
       时开机规则不跑，于是系统页被它永远盖住；预览此前恰好都走"没凭据"那条路，
       卡先被打开、再被 set_page 收掉，正好绕开了这个状态）。
       判据取卡上的按钮文字：卡隐藏时它们不可见。 */
    bool cfg0 = s_wifi_cfg, on0 = s_wifi_on;
    char ssid0[64], reason0[160];
    snprintf(ssid0, sizeof ssid0, "%s", s_wifi_ssid_stub);
    snprintf(reason0, sizeof reason0, "%s", s_wifi_reason);
    wifi_stub(true, true, "Demo", NULL);          /* 有凭据、已连接 */
    fnos_ui_set_page(0);
    vtick_advance(500); lv_timer_handler();
    fnos_ui_set_page(3);
    assert(!visible_text(lv_screen_active(), "重新扫描"));
    assert(!visible_text(lv_screen_active(), "手动输入"));
    assert(!visible_text(lv_screen_active(), "告警与事件"));   /* 告警仅在独立告警页显示 */
    /* 顶栏入口点开 → 卡出现（同槽位，正文让位）；标签此时是 SSID（已连接） */
    lv_obj_t *wbtn = visible_text(lv_screen_active(), "Demo");
    assert(wbtn);
    lv_obj_send_event(lv_obj_get_parent(wbtn), LV_EVENT_CLICKED, NULL);
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "重新扫描"));
    /* 复位 fixture：这组断言改了全局配网状态，不复位会把后面的样张一起带偏 */
    wifi_stub(cfg0, on0, ssid0, reason0);
    puts("checks: wifi card stays hidden on p3 when credentials exist, opens from the top bar PASS");
    puts("checks: maximum-list scrolling, glyph coverage, diagnostics toggle, alert clearing, healthy/waiting/lock-timeout transitions PASS");
}

/* 配对界面：走完 输入配对码 → 核对指纹 → 已配对 → 解除配对 四个画面。
   点按钮一律用 lv_obj_send_event(parent_of_label)，与真机触摸同一条事件路径。 */
/* 按文字点一个控件。**必须先找"父对象挂了事件回调"的那个**：同一段文字在界面上
   可能出现不止一次——配对面板的步骤编号就是 "1"/"2"/"3"，和键盘的 1/2/3 撞个正着。
   按树序取第一个会点到装饰性的步骤编号上，什么都不会发生，而且不报错；后面的断言
   只会以"输入没进槽位"的形式在别处失败，排查起来完全对不上号。
   （这个坑一直被前面的字形审计崩掉挡着没暴露。） */
static lv_obj_t *visible_text_clickable(lv_obj_t *o, const char *text)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_has_class(o, &lv_label_class) && strcmp(lv_label_get_text(o), text) == 0) {
        lv_obj_t *p = lv_obj_get_parent(o);
        if (p && lv_obj_get_event_count(p) > 0) return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *found = visible_text_clickable(lv_obj_get_child(o, i), text);
        if (found) return found;
    }
    return NULL;
}

static lv_point_t s_touch_point;
static bool s_touch_down;
static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = s_touch_point;
    data->state = s_touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Feed the pointer driver: this verifies actual hit testing, including decorations. */
static void click_text(const char *text)
{
    lv_obj_t *l = visible_text_clickable(lv_screen_active(), text);
    assert(l);
    lv_obj_t *b = lv_obj_get_parent(l);
    lv_obj_scroll_to_view_recursive(b, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t a;
    lv_obj_get_coords(b, &a);
    s_touch_point.x = (a.x1 + a.x2) / 2;
    s_touch_point.y = (a.y1 + a.y2) / 2;
    s_touch_down = true;
    vtick_advance(40); lv_timer_handler();
    s_touch_down = false;
    vtick_advance(40); lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
}

/* Regression checks exercise actual pointer hit testing and dynamic rendering. */
static lv_obj_t *visible_text_containing(lv_obj_t *o, const char *needle)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_has_class(o, &lv_label_class) && strstr(lv_label_get_text(o), needle)) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *found = visible_text_containing(lv_obj_get_child(o, i), needle);
        if (found) return found;
    }
    return NULL;
}

static void regression_tick(void)
{
    vtick_advance(500);
    lv_timer_handler();
    uk_anim_settle(lv_screen_active());
    lv_obj_update_layout(lv_screen_active());
}

static lv_obj_t *find_widget(lv_obj_t *root, const lv_obj_class_t *type)
{
    /* Numeric labels and streaming charts retain their native base interfaces. */
    if (lv_obj_has_class(root, type)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = find_widget(lv_obj_get_child(root, i), type);
        if (found) return found;
    }
    return NULL;
}

static lv_obj_t *overview_object(const char *title, const lv_obj_class_t *type)
{
    lv_obj_t *label = visible_text(lv_screen_active(), title);
    assert(label);
    lv_obj_t *card = lv_obj_get_parent(lv_obj_get_parent(label));
    return find_widget(card, type);
}

static int assert_usage_bars(lv_obj_t *root, uint32_t color)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return 0;
    int count = 0;
    if (lv_obj_has_class(root, &lv_bar_class)) {
        assert(lv_color_eq(lv_obj_get_style_bg_color(root, LV_PART_INDICATOR), uk_c(color)));
        count++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++)
        count += assert_usage_bars(lv_obj_get_child(root, i), color);
    return count;
}

/* Load colours agree across gauges and bars; histories keep metric identity. */
static void verify_usage_colors(void)
{
    const int previous_page = fnos_ui_page();
    fnos_status_t fixture = {0}; fill_live(&fixture);
    fixture.nmods = fixture.nalerts = 0;
    fixture.nvols = 1;
    s_override_status = &fixture;
    const float values[] = {UK_PCT_WARM / 2, UK_PCT_WARM, 100};
    const uint32_t colors[] = {UK_USAGE_LOW, UK_PCT_WARM < UK_PCT_FULL ? UK_USAGE_WARN : UK_USAGE_HIGH, UK_USAGE_HIGH};
    const char *const frames[] = {"16-usage-low", "16-usage-warn", "16-usage-high"};
    assert(uk_pct_color(-1) == UK_OFF);
    assert(uk_pct_color(UK_PCT_WARM - 0.25f) == colors[0]);
    assert(uk_pct_color(UK_PCT_FULL - 0.25f) == (UK_PCT_WARM < UK_PCT_FULL ? colors[1] : colors[0]));
    assert(uk_pct_color(100) == colors[2]);
    assert(uk_pct_color(NAN) == UK_OFF && uk_pct_color(INFINITY) == UK_OFF);
    for (int i = 0; i < 3; i++) {
        fixture.cpu.pct = fixture.mem.pct = fixture.vols[0].pct = values[i];
        fixture.vols[0].total_gb = 100;
        fixture.vols[0].used_gb = values[i];
        fixture.vols[0].free_gb = 100 - values[i];
        assert(uk_pct_color(values[i]) == colors[i]);
        fnos_ui_set_page(0); regression_tick();
        const char *const titles[] = {"CPU", "内存"};
        for (int j = 0; j < 2; j++) {
            lv_obj_t *arc = overview_object(titles[j], &lv_arc_class);
            assert(arc && lv_color_eq(lv_obj_get_style_arc_color(arc, LV_PART_INDICATOR), uk_c(colors[i])));
            lv_obj_t *reading = lv_obj_get_child(arc, 0);
            lv_obj_t *digits = lv_obj_get_child(reading, 0);
            int32_t inner = lv_obj_get_width(arc) - 2 * lv_obj_get_style_arc_width(arc, LV_PART_MAIN);
            assert(inner >= lv_obj_get_width(digits) + 2 * UK_S2 &&
                   "gauge must reserve room for three digits and inner clearance");
            assert(lv_obj_get_width(arc) == lv_obj_get_height(arc));
            lv_obj_t *chart = overview_object(j == 0 ? "CPU趋势" : "内存趋势", &lv_chart_class);
            assert(chart && lv_color_eq(lv_chart_get_series_color(chart, uk_trend_series(chart, 0)), uk_c(j == 0 ? UK_S_CPU : UK_S_MEM)));
        }
        assert(assert_usage_bars(overview_object("存储容量", &lv_obj_class), colors[i]) == 1);
        assert(assert_usage_bars(overview_object("卷占用", &lv_obj_class), colors[i]) == 1);
        snapshot(frames[i]);
        fnos_ui_set_page(1); regression_tick();
        click_text("全部"); regression_tick();
        lv_obj_t *used = visible_text(lv_screen_active(), "已用空间"); assert(used);
        lv_obj_t *body = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(used)));
        assert(assert_usage_bars(body, colors[i]) == 1);
        assert(assert_usage_bars(overview_object("存储卷", &lv_obj_class), colors[i]) == 1);
        char frame[64]; snprintf(frame, sizeof frame, "%s-storage", frames[i]); snapshot(frame);
    }
    /* Actual Alerts tile, including exact shared thresholds and health overrides. */
    const float capacity[] = {UK_PCT_WARM - 0.25f, UK_PCT_WARM,
        (UK_PCT_WARM + UK_PCT_FULL) / 2.f, UK_PCT_FULL, 96.f};
    for (int i = 0; i < 5; i++) {
        fixture.vols[0].pct = capacity[i];
        for (int r = 0; r < fixture.nraid; r++) fixture.raid[r].ok = true;
        fnos_ui_set_page(5); regression_tick();
        lv_obj_t *reading = visible_text_containing(lv_screen_active(), "最高 ");
        assert(reading);
        uint32_t color = uk_pct_color(capacity[i]);
        assert(lv_color_eq(lv_obj_get_style_text_color(reading, 0),
            uk_c(color == UK_USAGE_LOW ? UK_T1 : color)));
        lv_obj_t *tile = lv_obj_get_parent(reading);
        lv_obj_t *dot = lv_obj_get_child(lv_obj_get_child(tile, 0), 0);
        assert(lv_color_eq(lv_obj_get_style_bg_color(dot, 0), uk_c(color)));
        if (fixture.nraid) {
            fixture.raid[0].ok = false;
            regression_tick();
            uint32_t health = color == UK_USAGE_HIGH ? UK_USAGE_HIGH : UK_USAGE_WARN;
            assert(lv_color_eq(lv_obj_get_style_text_color(reading, 0), uk_c(health)));
            assert(lv_color_eq(lv_obj_get_style_bg_color(dot, 0), uk_c(health)));
        }
    }
    /* Health concerns remain warnings even when occupancy itself is low. */
    fixture.nmods = 2;
    fixture.mods[0].name = "vols"; fixture.mods[1].name = "raid";
    fixture.mods[0].status = "stale"; fixture.mods[1].status = "ok";
    for (int r = 0; r < fixture.nraid; r++) fixture.raid[r].ok = true;
    for (int high = 0; high < 2; high++) {
        fixture.vols[0].pct = high ? 96 : 20;
        regression_tick();
        lv_obj_t *reading = visible_text_containing(lv_screen_active(), "最高 ");
        assert(reading && lv_color_eq(lv_obj_get_style_text_color(reading, 0),
                                     uk_c(high ? UK_USAGE_HIGH : UK_USAGE_WARN)));
    }
    fixture.nvols = 0; regression_tick();
    lv_obj_t *missing = visible_text(lv_screen_active(), "未发现卷");
    assert(missing && lv_color_eq(lv_obj_get_style_text_color(missing, 0), uk_c(UK_USAGE_WARN)));
    fixture.mods[0].status = "denied"; regression_tick();
    lv_obj_t *denied = visible_text(lv_screen_active(), "权限不足");
    assert(denied && lv_color_eq(lv_obj_get_style_text_color(denied, 0), uk_c(UK_USAGE_WARN)));
    fixture.nmods = 0; fixture.nvols = 1;
    fixture.online = false;
    fnos_ui_set_page(0); regression_tick();
    assert(lv_color_eq(lv_obj_get_style_arc_color(overview_object("CPU", &lv_arc_class), LV_PART_INDICATOR), uk_c(UK_OFF)));
    assert(assert_usage_bars(overview_object("存储容量", &lv_obj_class), UK_OFF) == 1);
    assert(assert_usage_bars(overview_object("卷占用", &lv_obj_class), UK_OFF) == 1);
    s_override_status = NULL;
    fnos_ui_set_page(previous_page); regression_tick();
    fnos_status_release(&fixture);
    puts("checks: occupancy colours agree across gauges and bars; history identity stays stable; stale is neutral PASS");
}

static int chart_values_equal(lv_obj_t *chart, int series_index, int32_t value)
{
    const int32_t *data = lv_chart_get_series_y_array(chart, uk_trend_raw_series(chart, series_index));
    int count = 0;
    for (uint32_t i = 0; i < lv_chart_get_point_count(chart); i++) if (data[i] == value) count++;
    return count;
}

static void assert_overview_storage_neutral(const char *note)
{
    lv_obj_t *card = overview_object("存储容量", &lv_obj_class);
    lv_obj_t *used = visible_text(card, "900");
    assert(visible_text(card, note) && used);
    assert(visible_text(lv_obj_get_parent(used), "GB"));
    assert(lv_color_eq(lv_obj_get_style_text_color(used, 0), uk_c(UK_T3)));
    assert(assert_usage_bars(card, UK_OFF) == 1);
    assert(visible_text(card, "可用 50.0 GB · 1 个卷"));
}

/* Module freshness must agree between summaries and entity readings. */
static void verify_module_freshness(void)
{
    preview_state_t previous = s_state;
    int previous_page = fnos_ui_page();
    fnos_status_t fixture = {0}; fill_live(&fixture);
    fixture.nalerts = 0;
    int temps = -1, cpu = -1, vols = -1, raid = -1, net = -1;
    for (int i = 0; i < fixture.nmods; i++) {
        if (!strcmp(fixture.mods[i].name, "temps")) temps = i;
        if (!strcmp(fixture.mods[i].name, "cpu")) cpu = i;
        if (!strcmp(fixture.mods[i].name, "vols")) vols = i;
        if (!strcmp(fixture.mods[i].name, "raid")) raid = i;
        if (!strcmp(fixture.mods[i].name, "net")) net = i;
    }
    assert(temps >= 0 && cpu >= 0 && vols >= 0 && raid >= 0 && net >= 0);
    for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 45.6f;
    fixture.cpu.temp_c = 32;
    s_override_status = &fixture;
    fnos_status_text(&fixture, &fixture.mods[temps].status, "stale");
    fnos_ui_set_page(3); regression_tick();
    lv_obj_t *value = visible_text(lv_screen_active(), "45.6");
    assert(value && lv_color_eq(lv_obj_get_style_text_color(value, 0), uk_c(UK_T3)));
    if (fnos_ui_refresh_period_ms() < UK_NUMBER_PRESENT_MS) {
        vtick_advance(UK_NUMBER_PRESENT_MS); lv_timer_handler();
        for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 46.7f;
        vtick_advance(fnos_ui_refresh_period_ms()); lv_timer_handler();
        assert(visible_text(lv_screen_active(), "46.7"));
        assert(!visible_text(lv_screen_active(), "45.6"));
        for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 45.6f;
    }
    fnos_ui_set_page(4); regression_tick();
    value = visible_text(lv_screen_active(), "45.6");
    assert(value && lv_color_eq(lv_obj_get_style_text_color(value, 0), uk_c(UK_T3)));
    snapshot("14-temperature-stale");
    fnos_status_text(&fixture, &fixture.mods[temps].status, "ok");
    fnos_status_text(&fixture, &fixture.mods[cpu].status, "denied");
    fnos_ui_set_page(0); regression_tick();
    value = visible_text(lv_screen_active(), "45.6");
    assert(value && lv_color_eq(lv_obj_get_style_text_color(value, 0), uk_c(UK_T1)));
    fnos_ui_set_page(5); regression_tick();
    assert(visible_text(lv_screen_active(), "45.6°C\n无超限"));
    for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 0;
    regression_tick();
    assert(visible_text(lv_screen_active(), "0.0°C\n无超限"));
    for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 45.6f;
    fnos_status_text(&fixture, &fixture.mods[cpu].status, "ok");
    fixture.cpu.temp_c = 85;
    regression_tick();
    assert(visible_text(lv_screen_active(), "85.0°C\n关注 1 路"));
    fnos_status_text(&fixture, &fixture.mods[cpu].status, "stale");
    regression_tick();
    assert(visible_text(lv_screen_active(), "旧值 85.0°C\n关注 1 路 · 旧值"));
    fnos_status_text(&fixture, &fixture.mods[cpu].status, "ok");
    fnos_status_text(&fixture, &fixture.mods[temps].status, "stale");
    for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 80;
    regression_tick();
    char old_count[64];
    snprintf(old_count, sizeof old_count, "85.0°C\n关注 %d 路 · 旧值", fixture.ntemps);
    assert(visible_text(lv_screen_active(), old_count));
    fnos_status_text(&fixture, &fixture.mods[temps].status, "denied");
    fnos_ui_set_page(4); regression_tick();
    assert(!visible_text(lv_screen_active(), "80.0"));
    assert(visible_text(lv_screen_active(), "权限不足"));
    fnos_status_text(&fixture, &fixture.mods[temps].status, "ok");
    for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 30000000.f;
    regression_tick();
    value = visible_text(lv_screen_active(), "30000000.0");
    assert(value);
    lv_obj_t *bar = overview_object("设备温度", &lv_bar_class);
    assert(bar && lv_bar_get_value(bar) == lv_bar_get_max_value(bar));
    for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = FLT_MAX;
    regression_tick();
    assert(lv_bar_get_value(bar) == lv_bar_get_max_value(bar));
    for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = -5;
    regression_tick();
    assert(lv_bar_get_value(bar) == 0);
    for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 0;
    regression_tick();
    assert(lv_bar_get_value(bar) == 0);
    fnos_status_text(&fixture, &fixture.mods[temps].status, "denied");
    fnos_status_text(&fixture, &fixture.mods[vols].status, "stale");
    fnos_status_text(&fixture, &fixture.mods[raid].status, "stale");
    if (fnos_ui_refresh_period_ms() < UK_NUMBER_PRESENT_MS) {
        const int nvols = fixture.nvols;
        const float total = fixture.vols[0].total_gb;
        fixture.nvols = 1;
        fixture.vols[0].total_gb = 2048;
        fnos_ui_set_page(0); regression_tick();
        assert(visible_text(lv_screen_active(), "/ 2.0 TB"));
        vtick_advance(UK_NUMBER_PRESENT_MS); lv_timer_handler();
        fixture.vols[0].total_gb = 3072;
        vtick_advance(fnos_ui_refresh_period_ms()); lv_timer_handler();
        assert(visible_text(lv_screen_active(), "/ 3.0 TB"));
        assert(!visible_text(lv_screen_active(), "/ 2.0 TB"));
        fixture.nvols = nvols;
        fixture.vols[0].total_gb = total;
    }
    fnos_ui_set_page(1); regression_tick();
    value = visible_text(lv_screen_active(), "17.8");
    assert(value && lv_color_eq(lv_obj_get_style_text_color(value, 0), uk_c(UK_T3)));
    assert(visible_text(lv_obj_get_parent(value), "TB"));
    value = visible_text(lv_screen_active(), "54%");
    assert(value && lv_color_eq(lv_obj_get_style_text_color(value, 0), uk_c(UK_T3)));
    value = visible_text(lv_screen_active(), "正常");
    assert(value && lv_color_eq(lv_obj_get_style_text_color(value, 0), uk_c(UK_T3)));
    snapshot("14-storage-stale");
    fnos_status_text(&fixture, &fixture.mods[vols].status, "denied");
    regression_tick();
    assert(!visible_text(lv_screen_active(), "17.8"));
    assert(!visible_text(lv_screen_active(), "TB"));
    assert(!visible_text(lv_screen_active(), fixture.vols[0].mnt));
    fnos_status_text(&fixture, &fixture.mods[net].status, "stale");
    fnos_ui_set_page(2); regression_tick();
    value = visible_text(lv_screen_active(), "12.2");
    assert(value && lv_color_eq(lv_obj_get_style_text_color(value, 0), uk_c(UK_T3)));
    snapshot("14-network-stale");
    fnos_status_text(&fixture, &fixture.mods[net].status, "denied");
    regression_tick();
    assert(!visible_text(lv_screen_active(), "12.2"));
    assert(visible_text_containing(lv_screen_active(), "权限不足"));
    snapshot("14-network-denied");
    fixture.net.rx_kbs = fixture.net.tx_kbs = 0;
    regression_tick();
    assert(!visible_text(lv_screen_active(), "0.0"));
    fnos_status_text(&fixture, &fixture.mods[net].status, "ok");
    regression_tick();
    value = visible_text(lv_screen_active(), "0.0");
    assert(value && !lv_color_eq(lv_obj_get_style_text_color(value, 0), uk_c(UK_T3)));
    s_override_status = NULL; s_state = previous;
    fnos_ui_set_page(previous_page); regression_tick();
    fnos_status_release(&fixture);
    puts("checks: module freshness summaries and entity readings PASS");
}

/* Real zero histories, monotonic cursor, local stale modules, and full-volume scroll. */
static void verify_overview_charts(void)
{
    preview_state_t previous = s_state;
    char previous_ssid[sizeof s_wifi_ssid_stub];
    memcpy(previous_ssid, s_wifi_ssid_stub, sizeof previous_ssid);
    snprintf(s_wifi_ssid_stub, sizeof s_wifi_ssid_stub, "PREVIEW-LAN");
    fnos_status_t fixture = {0};
    fill_live(&fixture);
    fixture.nalerts = 0;
    fixture.nmods = 7;
    static const char *const names[] = { "cpu", "mem", "net", "vols", "raid", "temps", "docker" };
    for (int i = 0; i < fixture.nmods; i++) {
        fnos_status_text(&fixture, &fixture.mods[i].name, "%s", names[i]);
        fnos_status_text(&fixture, &fixture.mods[i].status, "ok");
    }
    fixture.cpu.pct = fixture.mem.pct = fixture.net.rx_kbs = fixture.net.tx_kbs = 0;
    fixture.mem.used_mb = 0;
    s_override_status = &fixture;
    s_override_sample_enabled = true;
    memset(&s_override_sample, 0, sizeof s_override_sample);
    s_state = ST_LIVE;
    fnos_ui_set_page(0);
    s_seq += CONFIG_FNOS_CHART_WINDOW;
    for (int i = 0; i < (CONFIG_FNOS_CHART_WINDOW + 63) / 64 + 1; i++) regression_tick();
    lv_obj_t *cpu = overview_object("CPU趋势", &lv_chart_class);
    lv_obj_t *mem = overview_object("内存趋势", &lv_chart_class);
    lv_obj_t *temp = overview_object("最热温度", &lv_chart_class);
    lv_obj_t *net = overview_object("网络吞吐", &lv_chart_class);
    assert(cpu && mem && temp && !net); /* Network history remains on its dedicated page. */
    assert(lv_chart_get_point_count(cpu) == CONFIG_FNOS_CHART_WINDOW);
    assert(lv_chart_get_point_count(mem) == CONFIG_FNOS_CHART_WINDOW);
    assert(lv_chart_get_point_count(temp) == CONFIG_FNOS_CHART_WINDOW);
    assert(chart_values_equal(cpu, 0, 0) == CONFIG_FNOS_CHART_WINDOW);
    assert(chart_values_equal(mem, 0, 0) == CONFIG_FNOS_CHART_WINDOW);
    assert(chart_values_equal(temp, 0, 0) == CONFIG_FNOS_CHART_WINDOW);

    /* Nine equal cards stay grouped as 3 + 3 + 3 and fit the initial 1024x600 viewport. */
    static const char *const overview_titles[] = {
        "CPU", "内存", "设备状态", "CPU趋势", "内存趋势", "最热温度", "网络吞吐", "存储容量", "卷占用"
    };
    _Static_assert(sizeof overview_titles / sizeof overview_titles[0] == 9, "overview card coverage");
    lv_obj_t *cards[sizeof overview_titles / sizeof overview_titles[0]];
    lv_obj_t *page = NULL;
    for (size_t i = 0; i < sizeof overview_titles / sizeof overview_titles[0]; i++) {
        cards[i] = overview_object(overview_titles[i], &lv_obj_class);
        assert(cards[i] && lv_obj_get_width(cards[i]) > 0 && lv_obj_get_height(cards[i]) > 0);
        assert(visible_text(cards[i], overview_titles[i]));
        if (!page) {
            for (page = lv_obj_get_parent(cards[i]); page &&
                 !lv_obj_has_flag(page, LV_OBJ_FLAG_USER_1); page = lv_obj_get_parent(page)) {}
            assert(page);
            lv_obj_scroll_to_y(page, 0, LV_ANIM_OFF); /* Start at the natural top-of-page position. */
        }
    }
    lv_obj_update_layout(lv_screen_active());
    lv_area_t screen_area, page_area;
    lv_obj_get_coords(lv_screen_active(), &screen_area);
    lv_obj_get_coords(page, &page_area);
    assert(lv_area_get_width(&screen_area) == 1024 && lv_area_get_height(&screen_area) == 600);
    assert(lv_obj_get_scroll_y(page) == 0);
    /* 2026-10-20 首页重排：hero 行（3 张等宽、最高）+ 趋势带（2 张等宽）+ 指标带
       （4 张，网络卡双宽，因为两列「数值 + 单位」要各自站得下）。行内同父同高。 */
    lv_obj_t *hero_row = lv_obj_get_parent(cards[0]);
    lv_obj_t *trend_row = lv_obj_get_parent(cards[3]);
    lv_obj_t *index_row = lv_obj_get_parent(cards[6]);
    assert(hero_row && trend_row && index_row &&
           hero_row != trend_row && trend_row != index_row && hero_row != index_row);
    assert(lv_obj_get_parent(cards[1]) == hero_row && lv_obj_get_parent(cards[2]) == hero_row);
    assert(lv_obj_get_child_count(hero_row) == 3);
    assert(lv_obj_get_parent(cards[4]) == trend_row && lv_obj_get_child_count(trend_row) == 2);
    assert(lv_obj_get_parent(cards[5]) == index_row && lv_obj_get_parent(cards[7]) == index_row &&
           lv_obj_get_parent(cards[8]) == index_row && lv_obj_get_child_count(index_row) == 4);
    assert(lv_obj_get_height(cards[0]) >= lv_obj_get_height(cards[3]) + 60);
    for (size_t i = 0; i < 3; i++) {
        assert(lv_obj_get_height(cards[i]) == lv_obj_get_height(cards[0]));
        assert(LV_ABS((int32_t)lv_obj_get_width(cards[i]) - (int32_t)lv_obj_get_width(cards[0])) <= 1);
    }
    assert(lv_obj_get_height(cards[4]) == lv_obj_get_height(cards[3]));
    assert(LV_ABS((int32_t)lv_obj_get_width(cards[4]) - (int32_t)lv_obj_get_width(cards[3])) <= 1);
    for (size_t i = 5; i < 9; i++) assert(lv_obj_get_height(cards[i]) == lv_obj_get_height(cards[5]));
    assert(LV_ABS((int32_t)lv_obj_get_width(cards[6]) - 2 * (int32_t)lv_obj_get_width(cards[5])) <= 2);
    for (size_t i = 0; i < sizeof cards / sizeof cards[0]; i++) {
        lv_area_t card_area, visible_area;
        lv_obj_get_coords(cards[i], &card_area);
        assert(card_area.x1 >= screen_area.x1 && card_area.x2 <= screen_area.x2 &&
               card_area.y1 >= screen_area.y1 && card_area.y2 <= screen_area.y2);
        assert(card_area.x1 >= page_area.x1 && card_area.x2 <= page_area.x2 &&
               card_area.y1 >= page_area.y1 && card_area.y2 <= page_area.y2);
        assert(visible_click_area(cards[i], &visible_area));
        assert(visible_area.x1 == card_area.x1 && visible_area.x2 == card_area.x2 &&
               visible_area.y1 == card_area.y1 && visible_area.y2 == card_area.y2);
        for (lv_obj_t *ancestor = cards[i]; ancestor && ancestor != page;
             ancestor = lv_obj_get_parent(ancestor))
            if (lv_obj_has_flag(ancestor, LV_OBJ_FLAG_SCROLLABLE))
                assert(lv_obj_get_scroll_y(ancestor) == 0);
    }
    for (int i = 0; i < 2; i++) {
        lv_obj_t *card = cards[i];
        assert(find_widget(card, &lv_arc_class));
        assert(!find_widget(card, &lv_chart_class));
        assert(!find_widget(card, &lv_bar_class));
        assert(visible_text(card, "0") && visible_text(card, "%"));
    }
    /* Gauges and numeric values share the same real-zero contract. */
    assert(lv_arc_get_value(overview_object("CPU", &lv_arc_class)) == 0);
    assert(lv_arc_get_value(overview_object("内存", &lv_arc_class)) == 0);
    assert(visible_text(overview_object("CPU", &lv_obj_class), "0"));
    assert(visible_text(overview_object("内存", &lv_obj_class), "0"));
    snapshot("10-overview-zero");
    lv_obj_t *temp_card = overview_object("最热温度", &lv_obj_class);
    assert(visible_text(temp_card, "传感器历史"));
    /* 150px row budget: p0 trend cards intentionally omit the per-card x-axis. */

    s_override_sample = (fnos_sample_t){ .cpu = 17, .mem = 42, .rx_kbs = 128, .tx_kbs = 64,
                                         .temp_max = 71 };
    fixture.cpu.temp_c = 95;
    for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 60;
    fixture.cpu.pct = 17; fixture.mem.pct = 42;
    fixture.mem.used_mb = fixture.mem.total_mb * 0.42f;
    fixture.net.rx_kbs = 128; fixture.net.tx_kbs = 64;
    s_seq++;
    regression_tick();
    assert(chart_values_equal(cpu, 0, 17) == 1 && chart_values_equal(mem, 0, 42) == 1);
    assert(chart_values_equal(temp, 0, 71) == 1);
    /* Raw history updates on this sample; current labels share the 600 ms publish beat. */
    for (int i = 0; i < 8; i++) regression_tick();
    assert(visible_text(temp_card, "CPU") && visible_text(temp_card, "95.0"));
    assert(chart_values_equal(cpu, 0, 17) == 1);
    assert(lv_arc_get_value(overview_object("CPU", &lv_arc_class)) == 17 * UK_METER_SCALE);
    assert(lv_arc_get_value(overview_object("内存", &lv_arc_class)) == 42 * UK_METER_SCALE);

    /* Free capacity comes from the supplied per-volume free_gb field. */
    fixture.nvols = 1;
    fixture.vols[0].total_gb = 1000;
    fixture.vols[0].used_gb = 900;
    fixture.vols[0].free_gb = 50;
    fixture.vols[0].pct = 90;
    regression_tick();
    /* Capacity numerics use the same timed publication as CPU and memory. */
    for (int i = 0; i < 8; i++) regression_tick();
    lv_obj_t *capacity_card = overview_object("存储容量", &lv_obj_class);
    assert(visible_text(capacity_card, "可用 50.0 GB · 1 个卷"));
    fnos_status_text(&fixture, &fixture.mods[3].status, "stale");
    regression_tick();
    assert_overview_storage_neutral("旧值");
    fnos_ui_set_page(1); regression_tick();
    lv_obj_t *free_title = visible_text(lv_screen_active(), "可用空间");
    assert(free_title);
    lv_obj_t *free_value = visible_text(lv_obj_get_parent(free_title), "50.0");
    assert(free_value && visible_text(lv_obj_get_parent(free_value), "GB"));
    assert(lv_color_eq(lv_obj_get_style_text_color(free_value, 0), uk_c(UK_T3)));
    fnos_ui_set_page(0); regression_tick();
    fnos_status_text(&fixture, &fixture.mods[3].status, "partial");
    regression_tick();
    assert_overview_storage_neutral("部分可读");
    fnos_status_text(&fixture, &fixture.mods[3].status, "ok");
    regression_tick();

    fnos_ui_set_page(2); regression_tick();
    lv_obj_t *title = visible_text_containing(lv_screen_active(), "网络吞吐");
    assert(title);
    lv_obj_t *detail = find_widget(lv_obj_get_parent(lv_obj_get_parent(title)), &lv_chart_class);
    assert(detail);
    /* 2026-10-22 复刻 michka：图内不再画 Y 刻度，量程改由卡头文字承担。
       刻度钉在基线这件事随设计一起去掉，但"量程说过话"仍然要查。 */
    lv_obj_t *net_card = lv_obj_get_parent(lv_obj_get_parent(detail));
    lv_obj_t *range_note = uk_card_note(net_card);
    assert(range_note && strstr(lv_label_get_text(range_note), "量程"));
    assert(chart_values_equal(detail, 0, 64) == 1);
    assert(chart_values_equal(detail, 1, 128) == 1);
    size_t bytes = CONFIG_FNOS_CHART_WINDOW * sizeof(int32_t);
    fnos_ui_set_page(0); regression_tick();

    fnos_status_text(&fixture, &fixture.mods[0].status, "stale");
    fnos_status_text(&fixture, &fixture.mods[2].status, "denied");
    fnos_status_text(&fixture, &fixture.mods[4].status, "stale");
    fnos_status_text(&fixture, &fixture.mods[5].status, "denied");
    fnos_status_text(&fixture, &fixture.mods[6].status, "denied");
    s_seq += 2;
    regression_tick();
    assert(chart_values_equal(cpu, 0, 17) == 1); // stale resource is frozen
    assert(chart_values_equal(mem, 0, 42) == 3); // healthy module still advances
    assert(visible_text(lv_screen_active(), "权限不足"));
    assert(!overview_object("网络吞吐", &lv_chart_class));
    assert(visible_text_containing(lv_screen_active(), "阵列旧值"));
    assert(visible_text_containing(lv_screen_active(), "温度旧值"));
    assert(lv_obj_get_style_opa(cpu, 0) == LV_OPA_40);
    snapshot("10-overview-partial");

    for (int i = 0; i < fixture.nmods; i++) fnos_status_text(&fixture, &fixture.mods[i].status, "ok");
    s_seq++;
    regression_tick();
    assert(chart_values_equal(cpu, 0, LV_CHART_POINT_NONE) == 1);
    s_override_sample.temp_max = NAN;
    s_seq++;
    regression_tick();
    assert(chart_values_equal(temp, 0, LV_CHART_POINT_NONE) == 1);
    s_override_sample.temp_max = 71;
    fixture.online = false;
    snprintf(fixture.last_err, sizeof fixture.last_err, "timeout");
    int32_t old[CONFIG_FNOS_CHART_WINDOW];
    memcpy(old, lv_chart_get_series_y_array(cpu, uk_trend_raw_series(cpu, 0)), bytes);
    s_seq++;
    regression_tick();
    assert(!memcmp(old, lv_chart_get_series_y_array(cpu, uk_trend_raw_series(cpu, 0)), bytes));
    assert(visible_text(lv_screen_active(), "旧值"));
    snapshot("10-overview-old-history");

    fixture.online = true;
    fixture.nvols = PREVIEW_VOLS;
    for (int i = 0; i < fixture.nvols; i++) {
        fnos_status_text(&fixture, &fixture.vols[i].mnt, "volume-%02d-long", i);
        fixture.vols[i].total_gb = 100; fixture.vols[i].used_gb = i * 8;
        fixture.vols[i].free_gb = 100 - i * 8; fixture.vols[i].pct = i * 8;
    }
    regression_tick();
    char last[64];
    snprintf(last, sizeof last, "volume-%02d-long", PREVIEW_VOLS - 1);
    lv_obj_t *last_vol = visible_text(lv_screen_active(), last);
    assert(last_vol);
    lv_obj_t *last_row = lv_obj_get_parent(lv_obj_get_parent(last_vol));
    lv_obj_t *volume_list = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(last_vol)));
    /* A short screen scrolls the page before its nested list can be dragged. */
    lv_obj_scroll_to_view_recursive(volume_list, LV_ANIM_OFF);
    lv_obj_scroll_to_y(volume_list, 0, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t a, view;
    lv_obj_get_coords(volume_list, &view);
    /* Drag through the real indev hit-test chain, including bars/labels in the pool. */
    for (int drag = 0; drag < PREVIEW_VOLS * 2; drag++) {
        /* Native scroll chaining may move the parent page as well as the list. */
        assert(visible_click_area(volume_list, &view));
        lv_obj_get_coords(last_row, &a);
        if (a.y1 >= view.y1 && a.y2 <= view.y2) break;
        s_touch_point.x = (view.x1 + view.x2) / 2;
        int start = view.y2 - 2;
        int distance = lv_area_get_height(&view) - 4;
        s_touch_point.y = start;
        s_touch_down = true;
        vtick_advance(30); lv_timer_handler();
        for (int step = 1; step <= 8; step++) {
            s_touch_point.y = start - distance * step / 8;
            vtick_advance(30); lv_timer_handler();
        }
        s_touch_down = false;
        vtick_advance(30); lv_timer_handler();
        regression_tick();
    }
    /* Let native momentum/elastic recovery settle after releasing the pointer. */
    for (int settle = 0; settle < 3; settle++) regression_tick();
    assert(visible_click_area(volume_list, &view));
    lv_obj_get_coords(last_row, &a);
    if (!(a.y1 >= view.y1 && a.y2 <= view.y2)) {
        fprintf(stderr, "overview last row=[%d,%d] viewport=[%d,%d] scroll=%d bottom=%d\n",
                a.y1, a.y2, view.y1, view.y2, (int)lv_obj_get_scroll_y(volume_list),
                (int)lv_obj_get_scroll_bottom(volume_list));
        snapshot("10-overview-drag-failure");
    }
    assert(a.y1 >= view.y1 && a.y2 <= view.y2);
    snapshot("10-overview-volumes-bottom");

    fixture.nvols = 0; fixture.ntemps = 0; fixture.cpu.temp_c = 0;
    regression_tick();
    assert(visible_text(lv_screen_active(), "没有可用容量数据"));
    snapshot("10-overview-empty-storage");
    fixture.ever_ok = false; fixture.online = false;
    regression_tick();
    assert(chart_values_equal(cpu, 0, LV_CHART_POINT_NONE) == CONFIG_FNOS_CHART_WINDOW);
    assert(chart_values_equal(mem, 0, LV_CHART_POINT_NONE) == CONFIG_FNOS_CHART_WINDOW);
    assert(chart_values_equal(temp, 0, LV_CHART_POINT_NONE) == CONFIG_FNOS_CHART_WINDOW);
    snapshot("10-overview-reset");
    s_override_sample_enabled = false;
    s_override_status = NULL;
    s_state = previous;
    memcpy(s_wifi_ssid_stub, previous_ssid, sizeof previous_ssid);
    regression_tick();
    fnos_status_release(&fixture);     /* M17：句柄用完必须还 */
}

static void click_object_center(lv_obj_t *obj)
{
    assert(obj && !effectively_hidden(obj));
    lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    s_touch_point.x = (a.x1 + a.x2) / 2;
    s_touch_point.y = (a.y1 + a.y2) / 2;
    s_touch_down = true;
    vtick_advance(40); lv_timer_handler();
    s_touch_down = false;
    vtick_advance(40); lv_timer_handler();
    regression_tick();
}

/* Exercise actual pointer hit testing after moving navigation into a side rail.
   API-only page changes would miss a clipped or non-clickable rail button. */
static void verify_navigation_hit_targets(void)
{
    /* 覆盖层现在是模态的（导航不再替用户关掉它）；这里先把上一个用例留下的
       配网/配对卡显式关掉，再测导航命中。 */
    lv_obj_t *overlay_close = visible_text_clickable(lv_screen_active(), "关闭");
    if (overlay_close) click_object_center(overlay_close);
    static const char *const pages[] = { "总览", "存储", "网络", "系统", "温度", "告警" };
    _Static_assert(sizeof pages / sizeof pages[0] == FNOS_UI_PAGE_COUNT, "navigation coverage");
    for (int p = FNOS_UI_PAGE_COUNT - 1; p >= 0; p--) {
        lv_obj_t *label = visible_text_clickable(lv_screen_active(), pages[p]);
        assert(label);
        lv_obj_t *button = lv_obj_get_parent(label);
        lv_obj_scroll_to_view_recursive(button, LV_ANIM_OFF);
        lv_obj_update_layout(lv_screen_active());
        lv_area_t visible;
        assert(visible_click_area(button, &visible));
        assert(lv_area_get_width(&visible) >= 44 && lv_area_get_height(&visible) >= 44);
        click_object_center(button);
        assert(fnos_ui_page() == p);
        audit_bounds(lv_screen_active());
    }
    static const struct { const char *title; int page; } details[] = {
        {"CPU",3},{"内存",3},{"设备状态",3},{"网络吞吐",2},{"存储容量",1},{"最热温度",4}
    };
    for (size_t i=0;i<sizeof details/sizeof details[0];i++) {
        fnos_ui_set_page(0); regression_tick();
        lv_obj_t *title=visible_text(lv_screen_active(),details[i].title);
        assert(title);
        lv_obj_scroll_to_view_recursive(title,LV_ANIM_OFF);
        lv_obj_update_layout(lv_screen_active());
        click_object_center(title);
        assert(fnos_ui_page()==details[i].page);
    }
    fnos_ui_set_page(0); regression_tick();
    puts("checks: every navigation target is reachable through pointer input PASS");
}

static void verify_enhanced_metrics(void)
{
    int saved_state=s_state; s_state=ST_LIVE;
    fnos_status_t fixture={0}; fill_live(&fixture);
    fixture.source_ts=1700000015;
    s_override_status=&fixture;
    fixture.nnets=0; fixture.net.physical=true; fixture.net.speed_mbps=2500;
    fixture.net.rx_kbs=125000; fixture.net.tx_kbs=62500;
    fnos_status_text(&fixture,&fixture.net.state,"up");
    fnos_ui_set_page(2); regression_tick();
    assert(visible_text_containing(lv_screen_active(),"链路占用 收 41.0% / 发 20.5%"));
    snapshot("12-link-utilization");
    fixture.net.speed_mbps=0; regression_tick();
    assert(!visible_text_containing(lv_screen_active(),"链路占用"));
    fixture.net.speed_mbps=2500; fixture.net.physical=false; regression_tick();
    assert(!visible_text_containing(lv_screen_active(),"链路占用"));
    fixture.net.physical=true;

    fixture.disks[0].has_read_ms=false; fixture.disks[0].has_write_ms=true; fixture.disks[0].write_ms=0;
    fnos_ui_set_page(1); regression_tick();
    assert(!visible_text_containing(lv_screen_active(),"平均延迟 -- / 0.00 ms"));
    lv_obj_t *disk=visible_text(lv_screen_active(),fixture.disks[0].dev);
    assert(disk);
    lv_obj_scroll_to_view_recursive(disk,LV_ANIM_OFF); regression_tick();
    click_object_center(disk);
    assert(visible_text_containing(lv_screen_active(),"平均延迟 -- / 0.00 ms"));
    click_object_center(visible_text_clickable(lv_screen_active(),"返回列表"));
    lv_obj_t *last=visible_text(lv_screen_active(),fixture.disks[fixture.ndisks-1].dev);
    assert(last); lv_obj_scroll_to_view_recursive(last,LV_ANIM_OFF); regression_tick();
    snapshot("12-disk-metrics");
    for (int i=0;i<fixture.nmods;i++) if (!strcmp(fixture.mods[i].name,"docker"))
        fnos_status_text(&fixture,&fixture.mods[i].status,"ok");
    fnos_ui_set_page(3); regression_tick();
    assert(!visible_text_containing(lv_screen_active(),"CPU 3.5% · 内存 128 MB"));
    lv_obj_t *container=visible_text(lv_screen_active(),fixture.docker[0].n);
    assert(container);
    lv_obj_scroll_to_view_recursive(container,LV_ANIM_OFF); regression_tick();
    click_object_center(container);
    assert(visible_text_containing(lv_screen_active(),"CPU 3.5% · 内存 128 MB"));
    snapshot("12-container-metrics");
    click_object_center(visible_text_clickable(lv_screen_active(),"返回列表"));
    for (int i=0;i<fixture.ndocker;i++) {
        fixture.docker[i].has_cpu=fixture.docker[i].has_mem=fixture.docker[i].has_restarts=false;
        fixture.docker[i].metrics_ts=0;
    }
    regression_tick();
    container=visible_text(lv_screen_active(),fixture.docker[0].n);
    lv_obj_scroll_to_view_recursive(container,LV_ANIM_OFF); regression_tick();
    click_object_center(container);
    assert(visible_text_containing(lv_screen_active(),"CPU -- · 内存 --"));
    assert(visible_text_containing(lv_screen_active(),"重启 -- · 资源未采集"));
    snapshot("12-metrics-unavailable");
    click_object_center(visible_text_clickable(lv_screen_active(),"返回列表"));
    s_override_status=NULL; s_state=saved_state; regression_tick(); fnos_status_release(&fixture);
    puts("checks: physical link utilization, unknown/virtual links, nullable latency, container values/age and unavailable metrics PASS");
}

/* Exercise filters and owned detail snapshots through real pointer input. */
static void pointer_text(const char *text)
{
    lv_obj_t *l = visible_text_clickable(lv_screen_active(), text);
    assert(l); click_object_center(lv_obj_get_parent(l));
}

/* Mirror fnos_ui.c:alert_text_cn(): the panel localizes collector alert text. */
static const char *alert_expected_cn(const char *raw, char *buf, size_t cap)
{
    int vol = 0, md = 0, pct = 0;
    double fpct = 0.0;
    char name[64] = "";
    if (sscanf(raw, "/vol%d used %d%%", &vol, &pct) == 2)
        snprintf(buf, cap, "卷 /vol%d 已用 %d%%", vol, pct);
    else if (sscanf(raw, "/vol%d free %lf%% left", &vol, &fpct) == 2)
        snprintf(buf, cap, "卷 /vol%d 剩余 %.1f%%", vol, fpct);
    else if (sscanf(raw, "container %63s down", name) == 1)
        snprintf(buf, cap, "容器 %s 已停止", name);
    else if (sscanf(raw, "md%d resync %lf%%", &md, &fpct) == 2)
        snprintf(buf, cap, "阵列 md%d 重建 %.1f%%", md, fpct);
    else
        snprintf(buf, cap, "%s", raw);
    return buf;
}

static void verify_unifi_views(void)
{
    int saved = s_state; s_state = ST_LIVE;
    fnos_status_t fixture = {0}; fill_live(&fixture);
    s_override_status = &fixture;
    fnos_ui_set_page(1); regression_tick();
    pointer_text("磁盘");
    assert(!visible_text(lv_screen_active(), "存储卷"));
    assert(visible_text(lv_screen_active(), "磁盘活动"));
    snapshot("13-storage-filter");
    fnos_ui_set_page(0); regression_tick();
    fnos_ui_set_page(1); regression_tick();
    assert(!visible_text(lv_screen_active(), "存储卷")); /* choice survives navigation */
    pointer_text("全部");
    assert(visible_text(lv_screen_active(), "存储卷"));
    lv_obj_t *entity = visible_text(lv_screen_active(), fixture.vols[0].mnt);
    assert(entity); click_object_center(entity);
    assert(visible_text(lv_screen_active(), "实体详情"));
    int before_page = fnos_ui_page();
    click_object_center(lv_obj_get_parent(visible_text_clickable(lv_screen_active(), "网络")));
    /* 点暗层关闭详情，但页面本身仍不切换。 */
    assert(fnos_ui_page() == before_page);
    assert(!visible_text(lv_screen_active(), "实体详情"));
    entity = visible_text(lv_screen_active(), fixture.vols[0].mnt);
    assert(entity); click_object_center(entity);
    assert(visible_text(lv_screen_active(), "实体详情"));
    lv_area_t da;
    lv_obj_t *detail_head = visible_text(lv_screen_active(), "实体详情");
    lv_obj_get_coords(lv_obj_get_parent(lv_obj_get_parent(detail_head)), &da);
    lv_obj_t *host = visible_text_containing(lv_screen_active(), fixture.host);
    assert(host);
    lv_area_t header;
    lv_obj_get_coords(lv_obj_get_parent(lv_obj_get_parent(host)), &header);
    assert(da.y1 > header.y2 && "entity details must start below the actual header");
    s_touch_point.x = da.x2 - UK_S4; s_touch_point.y = (da.y1 + da.y2) / 2;
    s_touch_down = true; vtick_advance(40); lv_timer_handler();
    for (int k = 0; k < 8; k++) {
        s_touch_point.x -= lv_area_get_width(&da) / 12;
        vtick_advance(40); lv_timer_handler();
    }
    s_touch_down = false; vtick_advance(40); lv_timer_handler(); regression_tick();
    assert(fnos_ui_page() == before_page && visible_text(lv_screen_active(), "实体详情"));
    snapshot("13-volume-detail");
    pointer_text("返回列表");
    assert(!visible_text(lv_screen_active(), "实体详情"));
    const char *long_identity = "/very-long-volume/完整名称必须可读/";
    assert(fnos_status_text(&fixture, &fixture.vols[0].mnt, "%s%s%s%s%s%s%s%s%s%s%s%s",
            long_identity,long_identity,long_identity,long_identity,long_identity,long_identity,
            long_identity,long_identity,long_identity,long_identity,long_identity,long_identity));
    regression_tick();
    entity = visible_text(lv_screen_active(), fixture.vols[0].mnt);
    assert(entity); click_object_center(entity);
    assert(visible_text(lv_screen_active(), "实体详情"));
    snapshot("13-long-detail");
    pointer_text("返回列表"); /* Fixed action remains reachable on compact screens. */
    fnos_ui_set_page(3); regression_tick();
    pointer_text("硬件");
    assert(!visible_text(lv_screen_active(), "容器服务"));
    snapshot("13-system-filter");
    pointer_text("全部");
    entity = visible_text(lv_screen_active(), fixture.docker[0].n);
    assert(entity); click_object_center(entity);
    lv_obj_t *heading = visible_text(lv_screen_active(), "实体详情");
    assert(heading);
    lv_obj_t *detail = lv_obj_get_parent(lv_obj_get_parent(heading));
    assert(visible_text(detail, fixture.docker[0].n));
    /* A live inventory rename must not silently rebind the captured detail. */
    char selected[128]; snprintf(selected, sizeof selected, "%s", fixture.docker[0].n);
    assert(fnos_status_text(&fixture, &fixture.docker[0].n, "renamed-container"));
    regression_tick();
    assert(visible_text(detail, selected));
    assert(!visible_text(detail, "renamed-container"));
    snapshot("13-container-detail");
    pointer_text("返回列表");
    fnos_ui_set_page(5); regression_tick();
    pointer_text("严重");
    for (int i = 0; i < fixture.nalerts; i++) {
        bool critical = !strcmp(fixture.alerts[i].lv, "crit");
        char expect[128];
        alert_expected_cn(fixture.alerts[i].m, expect, sizeof expect);
        assert((visible_text(lv_screen_active(), expect) != NULL) == critical);
    }
    snapshot("13-alert-filter");
    for (int i = 0; i < fixture.nalerts; i++)
        assert(fnos_status_text(&fixture, &fixture.alerts[i].lv, "warn"));
    regression_tick();
    assert(visible_text(lv_screen_active(), "此等级暂无事件 · 选择全部查看"));
    snapshot("13-alert-filter-empty");
    fixture.nalerts = 0; regression_tick();
    assert(visible_text(lv_screen_active(), "暂无告警事件"));
    pointer_text("全部");
    s_override_status = NULL; s_state = saved; regression_tick();
    fnos_status_release(&fixture);
    puts("checks: UniFi filters, restore-all, selection persistence, detail pointer input and frozen identity PASS");
}

static void verify_detail_freshness(void)
{
    preview_state_t previous = s_state;
    int previous_page = fnos_ui_page();
    fnos_status_t fixture = {0}; fill_live(&fixture);
    fixture.nvols = fixture.nraid = fixture.ntemps = 1;
    fixture.temps[0].c = 45.6f;
    const char *identities[] = { fixture.vols[0].mnt, fixture.raid[0].dev,
        fixture.temps[0].dn[0] ? fixture.temps[0].dn : fixture.temps[0].dev };
    static const char *const modules[] = { "vols", "raid", "temps" };
    static const char *const readings[] = { "54%", "正常", "45.6 °C" };
    static const char *const states[] = { "stale", "partial" };
    s_state = ST_LIVE; s_override_status = &fixture;
    for (unsigned kind = 0; kind < sizeof modules / sizeof modules[0]; kind++) {
        int module = -1;
        for (int i = 0; i < fixture.nmods; i++)
            if (!strcmp(fixture.mods[i].name, modules[kind])) module = i;
        assert(module >= 0);
        if (kind != 1 && UK_NUMBER_PRESENT_MS > fnos_ui_refresh_period_ms()) {
            assert(fnos_status_text(&fixture, &fixture.mods[module].status, "ok"));
            fixture.vols[0].pct = 11; fixture.temps[0].c = 11;
            fnos_ui_set_page(kind == 2 ? 3 : 1); regression_tick(); regression_tick();
            vtick_advance(UK_NUMBER_PRESENT_MS + 1); lv_timer_handler();
            assert(uk_number_presentation_open());
            assert(fnos_status_text(&fixture, &fixture.mods[module].status, "stale"));
            fixture.vols[0].pct = 54; fixture.temps[0].c = 45.6f;
            vtick_advance(fnos_ui_refresh_period_ms()); lv_timer_handler();
            uk_anim_settle(lv_screen_active());
            assert(!uk_number_presentation_open());
            lv_obj_t *entity = visible_text(lv_screen_active(), identities[kind]);
            assert(entity);
            lv_obj_t *bar = NULL;
            for (lv_obj_t *row = entity; row && !bar; row = lv_obj_get_parent(row))
                bar = find_widget(row, &lv_bar_class);
            assert(bar);
            assert(lv_bar_get_value(bar) == (kind == 0 ? 54 : 45) * UK_METER_SCALE);
        }
        for (unsigned state = 0; state < sizeof states / sizeof states[0]; state++) {
            assert(fnos_status_text(&fixture, &fixture.mods[module].status, "%s", states[state]));
            fixture.vols[0].pct = 54; fixture.raid[0].ok = true; fixture.temps[0].c = 45.6f;
            fnos_ui_set_page(kind == 2 ? 3 : 1); regression_tick();
            pointer_text("全部");
            lv_obj_t *entity = visible_text(lv_screen_active(), identities[kind]);
            assert(entity); click_object_center(entity);
            lv_obj_t *heading = visible_text(lv_screen_active(), "实体详情");
            assert(heading);
            lv_obj_t *detail = lv_obj_get_parent(lv_obj_get_parent(heading));
            lv_obj_t *context = visible_text_containing(detail, "打开时快照");
            assert(context);
            assert(strstr(lv_label_get_text(context), state == 0 ? "旧值" : "部分"));
            assert(visible_text(detail, identities[kind]));
            assert(visible_text(detail, readings[kind]));
            char shot[64];
            snprintf(shot, sizeof shot, "18-detail-%s-%s", modules[kind], states[state]);
            snapshot(shot);
            char captured_context[256];
            snprintf(captured_context, sizeof captured_context, "%s", lv_label_get_text(context));
            /* A later successful sample cannot upgrade the opened retained reading. */
            assert(fnos_status_text(&fixture, &fixture.mods[module].status, "ok"));
            fixture.vols[0].pct = 17; fixture.raid[0].ok = false; fixture.temps[0].c = 67.8f;
            regression_tick();
            assert(visible_text(detail, captured_context));
            assert(visible_text(detail, identities[kind]));
            assert(visible_text(detail, readings[kind]));
            assert(!visible_text(detail, kind == 0 ? "17%" : kind == 1 ? "状态未知" : "67.8 °C"));
            pointer_text("返回列表");
        }
    }
    s_override_status = NULL; s_state = previous;
    fnos_ui_set_page(previous_page); regression_tick(); fnos_status_release(&fixture);
    puts("checks: retained and partial volume/RAID/temperature details keep freshness and captured values PASS");
}

static void verify_diagnostic_freshness(void)
{
    preview_state_t previous = s_state;
    int previous_page = fnos_ui_page();
    fnos_status_t fixture = {0}; fill_live(&fixture);
    int cpu = -1, mem = -1;
    for (int i = 0; i < fixture.nmods; i++) {
        if (!strcmp(fixture.mods[i].name, "cpu")) cpu = i;
        if (!strcmp(fixture.mods[i].name, "mem")) mem = i;
    }
    assert(cpu >= 0 && mem >= 0);
    s_state = ST_LIVE; s_override_status = &fixture;
    fnos_ui_set_page(3); regression_tick(); pointer_text("设置");
    lv_obj_t *heading = visible_text(lv_screen_active(), "设置与诊断");
    assert(heading);
    lv_obj_t *diagnostics = lv_obj_get_parent(lv_obj_get_parent(heading));
    /* These zeros model omitted fields, then genuine zero measurements below. */
    fixture.cpu.load1 = fixture.cpu.load5 = fixture.cpu.load15 = 0;
    fixture.cpu.procs = 0;
    fixture.mem.swap_used_mb = fixture.mem.swap_total_mb = 0;
    static const char *const unavailable[] = { "missing", "denied" };
    for (unsigned i = 0; i < sizeof unavailable / sizeof unavailable[0]; i++) {
        assert(fnos_status_text(&fixture, &fixture.mods[cpu].status, "%s", unavailable[i]));
        assert(fnos_status_text(&fixture, &fixture.mods[mem].status, "%s", unavailable[1 - i]));
        regression_tick();
        assert(!visible_text_containing(diagnostics, "负载 0.00/0.00/0.00"));
        assert(!visible_text_containing(diagnostics, "进程 0"));
        assert(!visible_text_containing(diagnostics, "交换 0.0/0.0"));
        lv_obj_t *summary = visible_text_containing(diagnostics, "CPU --");
        assert(summary);
        const char *text = lv_label_get_text(summary);
        assert(strstr(text, i == 0 ? "读不到" : "权限不足"));
        assert(strstr(text, "内存 --"));
        assert(strstr(text, i == 0 ? "权限不足" : "读不到"));
        lv_obj_scroll_to_view_recursive(summary, LV_ANIM_OFF);
        char shot[80];
        snprintf(shot, sizeof shot, "18-diagnostics-cpu-%s-mem-%s", unavailable[i], unavailable[1 - i]);
        snapshot(shot);
    }
    assert(fnos_status_text(&fixture, &fixture.mods[cpu].status, "ok"));
    assert(fnos_status_text(&fixture, &fixture.mods[mem].status, "ok"));
    regression_tick();
    assert(visible_text_containing(diagnostics, "负载 0.00/0.00/0.00"));
    assert(visible_text_containing(diagnostics, "进程 0"));
    assert(visible_text_containing(diagnostics, "交换 0.0/0.0"));
    lv_obj_scroll_to_view_recursive(visible_text_containing(diagnostics, "负载 0.00/0.00/0.00"), LV_ANIM_OFF);
    snapshot("18-diagnostics-real-zero");
    fixture.cpu.load1 = 1.42f; fixture.cpu.load5 = 1.18f; fixture.cpu.load15 = 0.96f;
    fixture.cpu.procs = 412;
    fixture.mem.swap_used_mb = 137; fixture.mem.swap_total_mb = 2048;
    static const char *const retained[] = { "stale", "partial" };
    for (unsigned i = 0; i < sizeof retained / sizeof retained[0]; i++) {
        assert(fnos_status_text(&fixture, &fixture.mods[cpu].status, "%s", retained[i]));
        assert(fnos_status_text(&fixture, &fixture.mods[mem].status, "%s", retained[1 - i]));
        regression_tick();
        lv_obj_t *summary = visible_text_containing(diagnostics, "负载 1.42/1.18/0.96");
        assert(summary);
        const char *text = lv_label_get_text(summary);
        assert(strstr(text, "进程 412") && strstr(text, "交换 0.1/2.0"));
        assert(strstr(text, "旧值") && strstr(text, "部分可读"));
        lv_obj_scroll_to_view_recursive(summary, LV_ANIM_OFF);
        char shot[80];
        snprintf(shot, sizeof shot, "18-diagnostics-cpu-%s-mem-%s", retained[i], retained[1 - i]);
        snapshot(shot);
    }
    pointer_text("返回");
    s_override_status = NULL; s_state = previous;
    fnos_ui_set_page(previous_page); regression_tick(); fnos_status_release(&fixture);
    puts("checks: diagnostics distinguish unavailable values, real zero, retained CPU and partial memory PASS");
}

static void verify_alert_text_preservation(void)
{
    preview_state_t previous = s_state;
    int previous_page = fnos_ui_page();
    fnos_status_t fixture = {0}; fill_live(&fixture);
    /* Fixed expected prose is independent of the product's template parser. */
    static const char long_raw[] =
        "container download-service-0123456789abcdefghijklmnopqrstuvwxyz-"
        "0123456789abcdefghijklmnopqrstuvwxyz-0123456789abcdefghijklmnopqrstuvwxyz-"
        "0123456789abcdefghijklmnopqrstuvwxyz-0123456789abcdefghijklmnopqrstuvwxyz-"
        "full-identity-tail down";
    static const char long_expected[] =
        "容器 download-service-0123456789abcdefghijklmnopqrstuvwxyz-"
        "0123456789abcdefghijklmnopqrstuvwxyz-0123456789abcdefghijklmnopqrstuvwxyz-"
        "0123456789abcdefghijklmnopqrstuvwxyz-0123456789abcdefghijklmnopqrstuvwxyz-"
        "full-identity-tail 已停止";
    static const char *const unknown[] = {
        "container exporter restart loop",
        "container exporter down · restart limit reached",
        "/vol1 used 85% · snapshot quota unavailable",
        "md0 resync 47.0% · paused by administrator"
    };
    assert(strlen(long_expected) > 192);
    fixture.nalerts = 1;
    assert(fnos_status_text(&fixture, &fixture.alerts[0].lv, "warn"));
    assert(fnos_status_text(&fixture, &fixture.alerts[0].m, "%s", long_raw));
    s_state = ST_LIVE; s_override_status = &fixture;
    fnos_ui_set_page(5); regression_tick(); pointer_text("全部");
    lv_obj_t *message = visible_text(lv_screen_active(), long_expected);
    assert(message);
    lv_obj_scroll_to_view_recursive(message, LV_ANIM_OFF); regression_tick();
    assert(visible_text(lv_screen_active(), long_expected));
    snapshot("14-alert-long-container");
    for (unsigned i = 0; i < sizeof unknown / sizeof unknown[0]; i++) {
        assert(fnos_status_text(&fixture, &fixture.alerts[0].m, "%s", unknown[i]));
        regression_tick();
        assert(visible_text(lv_screen_active(), unknown[i]));
        assert(!visible_text(lv_screen_active(), "容器 exporter 已停止"));
    }
    s_override_status = NULL; s_state = previous;
    fnos_ui_set_page(previous_page); regression_tick(); fnos_status_release(&fixture);
    puts("checks: long container alert identity is complete and unmatched templates remain verbatim PASS");
}

static lv_obj_t *temperature_device_block(lv_obj_t *name)
{
    /* Find the closest column container with a click handler: the semantic device block.
       The label lives inside a transparent width slot and a horizontal heading row. */
    for (lv_obj_t *p = name; p; p = lv_obj_get_parent(p)) {
        if (lv_obj_get_style_flex_flow(p, LV_PART_MAIN) == LV_FLEX_FLOW_COLUMN &&
            lv_obj_get_event_count(p) > 0 && lv_obj_has_flag(p, LV_OBJ_FLAG_CLICKABLE)) return p;
    }
    return NULL;
}

/* Temperature severity is shared by the visible readouts, dot and device bar. */
static unsigned assert_temperature_readings(lv_obj_t *object, const char *text, uint32_t color)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return 0;
    unsigned count = 0;
    if (lv_obj_has_class(object, &lv_label_class) && !strcmp(lv_label_get_text(object), text)) {
        assert(lv_color_eq(lv_obj_get_style_text_color(object, 0), uk_c(color)));
        count++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++)
        count += assert_temperature_readings(lv_obj_get_child(object, i), text, color);
    return count;
}

static void verify_temperature_colors(void)
{
    preview_state_t previous = s_state;
    int previous_page = fnos_ui_page();
    s_state = ST_HEALTHY;
    fnos_status_t fixture = {0};
    if (!fnos_data_get(&fixture)) abort();
    fixture.nmods = 0;
    fixture.ntemps = 3;
    for (int i = 0; i < fixture.ntemps; i++) {
        assert(fnos_status_text(&fixture, &fixture.temps[i].dev, "temperature-test"));
        assert(fnos_status_text(&fixture, &fixture.temps[i].dn, "Temperature device"));
        assert(fnos_status_text(&fixture, &fixture.temps[i].ch, "Sensor %d", i));
        fixture.temps[i].c = 20;
    }
    s_override_status = &fixture;
    fnos_ui_set_page(4); regression_tick();
    lv_obj_t *name = visible_text(lv_screen_active(), "Temperature device");
    assert(name);
    lv_obj_t *devices_card = overview_object("设备温度", &lv_obj_class);
    assert(!visible_text(lv_screen_active(), "温度趋势"));
    assert(!find_widget(lv_obj_get_parent(devices_card), &lv_chart_class));
    assert(lv_obj_get_height(devices_card) >= 360);
    click_object_center(name);
    lv_obj_t *block = temperature_device_block(name);
    assert(block && visible_text(block, "Sensor 0"));
    const float values[] = {-5, 0, UK_TEMP_WARM - .1f, UK_TEMP_WARM,
                           (UK_TEMP_WARM + UK_TEMP_DANGER) / 2, UK_TEMP_DANGER, UK_TEMP_DANGER + 5};
    const uint32_t colors[] = {UK_OK, UK_OK, UK_OK, UK_WARN, UK_WARN, UK_DANGER, UK_DANGER};
    for (int night = 0; night < 2; night++) {
        fnos_ui_set_night(night != 0);
        for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++) {
            for (int k = 0; k < fixture.ntemps; k++) fixture.temps[k].c = values[i];
            regression_tick(); regression_tick(); /* Cross the existing 600ms numeric presentation beat. */
            char value[32]; snprintf(value, sizeof value, "%.1f", values[i]);
            assert(assert_temperature_readings(lv_screen_active(), value, colors[i]) >= 5);
            assert(assert_usage_bars(block, colors[i]) == 1);
            lv_obj_t *dot = lv_obj_get_child(lv_obj_get_parent(lv_obj_get_parent(name)), 0);
            assert(lv_color_eq(lv_obj_get_style_bg_color(dot, 0), uk_c(colors[i])));
            char shot[72]; snprintf(shot, sizeof shot, "temperature-%s-%u", night ? "night" : "day", i);
            snapshot(shot);
        }
    }
    fixture.nmods = 1;
    fixture.mods[0].name = "temps";
    fixture.mods[0].status = "stale";
    regression_tick();
    char value[32]; snprintf(value, sizeof value, "%.1f", values[6]);
    assert(assert_temperature_readings(lv_screen_active(), value, UK_T3) >= 5);
    assert(assert_usage_bars(block, UK_OFF) == 1);
    snapshot("temperature-stale");
    fixture.mods[0].status = "ok";
    const float mixed[] = {UK_TEMP_WARM - 5, (UK_TEMP_WARM + UK_TEMP_DANGER) / 2, UK_TEMP_DANGER + 5};
    const uint32_t mixed_colors[] = {UK_OK, UK_WARN, UK_DANGER};
    for (int i = 0; i < fixture.ntemps; i++) {
        assert(fnos_status_text(&fixture, &fixture.temps[i].dev, "temperature-%d", i));
        assert(fnos_status_text(&fixture, &fixture.temps[i].dn, "Temperature device %d", i));
        fixture.temps[i].c = mixed[i];
    }
    fnos_ui_set_night(false);
    regression_tick(); regression_tick();
    lv_obj_t *devices = overview_object("设备温度", &lv_obj_class);
    for (int i = 0; i < 3; i++) {
        char device[64]; snprintf(device, sizeof device, "Temperature device %d", i);
        lv_obj_t *mixed_name = visible_text_containing(devices, device);
        assert(mixed_name);
        lv_obj_t *mixed_block = temperature_device_block(mixed_name);
        assert(mixed_block && assert_usage_bars(mixed_block, mixed_colors[i]) == 1);
    }
    snapshot("temperature-mixed");
    fnos_ui_set_night(false);
    s_override_status = NULL; s_state = previous;
    fnos_ui_set_page(previous_page); regression_tick();
    fnos_status_release(&fixture);
    puts("checks: temperature thresholds, hero/device/channel/bar colors, night and stale states PASS");
}

typedef struct { lv_obj_t *obj; lv_area_t area; } regression_position_t;
static regression_position_t s_positions[128];
static int s_position_n;

static void capture_positions(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_get_style_flex_flow(o, LV_PART_MAIN) == LV_FLEX_FLOW_COLUMN &&
        lv_obj_get_event_count(o) > 0 && lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        assert(s_position_n < (int)(sizeof s_positions / sizeof s_positions[0]));
        s_positions[s_position_n].obj = o;
        lv_obj_get_coords(o, &s_positions[s_position_n++].area);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) capture_positions(lv_obj_get_child(o, i));
}

static void assert_positions_unchanged(void)
{
    for (int i = 0; i < s_position_n; i++) {
        lv_area_t now;
        assert(lv_obj_is_valid(s_positions[i].obj));
        assert(!effectively_hidden(s_positions[i].obj));
        lv_obj_get_coords(s_positions[i].obj, &now);
        const lv_area_t *old = &s_positions[i].area;
        if (now.x1 != old->x1 || now.x2 != old->x2 || now.y1 != old->y1 || now.y2 != old->y2) {
            fprintf(stderr, "device moved without an input/topology/size change: [%d,%d]-[%d,%d] -> [%d,%d]-[%d,%d]\n",
                    old->x1, old->y1, old->x2, old->y2, now.x1, now.y1, now.x2, now.y2);
            abort();
        }
    }
}

static int audit_stale_running_labels(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return 0;
    int count = 0;
    if (lv_obj_has_class(o, &lv_label_class)) {
        const char *text = lv_label_get_text(o);
        if (strstr(text, "运行中")) {
            assert(strstr(text, "上次") || strstr(text, "旧") || strstr(text, "历史") || strstr(text, "部分可读"));
            assert(lv_color_to_u32(lv_obj_get_style_text_color(o, LV_PART_MAIN)) != lv_color_to_u32(uk_c(UK_OK)));
            count++;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) count += audit_stale_running_labels(lv_obj_get_child(o, i));
    return count;
}

/* Segment failures must not become a healthy empty list or current running rows. */
static void verify_docker_module_states(void)
{
    preview_state_t previous = s_state;
    int previous_page = fnos_ui_page();
    fnos_status_t fixture = {0};
    fill_live(&fixture);
    int docker_mod = -1;
    for (int i = 0; i < fixture.nmods; i++)
        if (!strcmp(fixture.mods[i].name, "docker")) docker_mod = i;
    assert(docker_mod >= 0);
    int populated = fixture.ndocker;
    assert(populated > 0);
    for (int i = 0; i < populated; i++) fixture.docker[i].up = true;
    char first_name[strlen(fixture.docker[0].n)+1];
    snprintf(first_name, sizeof first_name, "%s", fixture.docker[0].n);
    fnos_status_text(&fixture, &fixture.mods[docker_mod].status, "ok");
    s_override_status = &fixture;
    fnos_ui_set_page(3);
    regression_tick();
    lv_obj_t *title = visible_text(lv_screen_active(), "容器服务");
    assert(title);
    lv_obj_t *card = lv_obj_get_parent(lv_obj_get_parent(title));
    lv_obj_t *running = visible_text(card, "运行中");
    assert(running);
    assert(lv_color_to_u32(lv_obj_get_style_text_color(running, LV_PART_MAIN)) == lv_color_to_u32(uk_c(UK_T2)));

    fixture.ndocker = 0;
    regression_tick();
    assert(visible_text(card, "暂无容器"));
    assert(!visible_text(card, first_name));
    assert(!visible_text_containing(lv_screen_active(), "容器 0/0"));
    static const struct { const char *status, *reason; } failures[] = {
        { "disabled", "未启用" }, { "denied", "权限" },
        { "missing", "来源" }, { "error", "失败" }, { "future_v3", "future_v3" },
    };
    for (size_t i = 0; i < sizeof failures / sizeof failures[0]; i++) {
        fnos_status_text(&fixture, &fixture.mods[docker_mod].status, "%s", failures[i].status);
        regression_tick();
        assert(visible_text_containing(card, failures[i].reason));
        assert(!visible_text(card, "暂无容器"));
        assert(!visible_text_containing(lv_screen_active(), "容器 0/0"));
        assert(!visible_text(card, first_name));
        if (!strcmp(failures[i].status, "disabled")) snapshot("11-docker-disabled");
        fnos_ui_set_page(0);
        regression_tick();
        lv_obj_t *status = overview_object("设备状态", &lv_obj_class);
        assert(status && visible_text_containing(status, failures[i].reason));
        if (strcmp(failures[i].status, "disabled"))
            assert(!visible_text_containing(status, "未启用"));
        fnos_ui_set_page(3);
        regression_tick();
    }

    fixture.ndocker = populated;
    static const char *const retained[] = { "stale", "partial", "denied" };
    for (size_t i = 0; i < sizeof retained / sizeof retained[0]; i++) {
        fnos_status_text(&fixture, &fixture.mods[docker_mod].status, "%s", retained[i]);
        regression_tick();
        assert(visible_text(card, first_name));
        assert(audit_stale_running_labels(card) == populated);
    }
    fnos_status_text(&fixture, &fixture.mods[docker_mod].status, "ok");
    fixture.online = false;
    regression_tick();
    assert(audit_stale_running_labels(card) == populated);
    snapshot("11-docker-retained-offline");

    fixture.online = true;
    fixture.ever_ok = false; /* Even a retained array must not imply first-sample success. */
    regression_tick();
    assert(visible_text_containing(card, "等待"));
    assert(!visible_text(card, first_name));
    fixture.ever_ok = true;
    regression_tick();
    running = visible_text(card, "运行中");
    assert(running);
    assert(lv_color_to_u32(lv_obj_get_style_text_color(running, LV_PART_MAIN)) == lv_color_to_u32(uk_c(UK_T2)));
    fixture.nmods = 0; /* Collector v1 has no segment metadata. */
    regression_tick();
    assert(visible_text(card, first_name));
    running = visible_text(card, "运行中");
    assert(running);
    assert(lv_color_to_u32(lv_obj_get_style_text_color(running, LV_PART_MAIN)) == lv_color_to_u32(uk_c(UK_T2)));

    s_override_status = NULL;
    s_state = previous;
    fnos_ui_set_page(previous_page);
    regression_tick();
    fnos_status_release(&fixture);     /* M17：句柄用完必须还 */
}

static void verify_ui_regressions(void)
{
    preview_state_t old_state = s_state;
    int old_page = fnos_ui_page();
    fnos_status_t fixture = {0};
    fill_live(&fixture);
    fnos_status_text(&fixture, &fixture.host, "%s", "nas-with-a-long-host-name-ABCDEFGHIJKLMNOPQRSTUVWXYZ123456");
    s_override_status = &fixture;
    fnos_ui_set_page(0);
    regression_tick();
    snapshot("11-long-host");

    /* Collection alerts belong exclusively to the Alerts page. */
    fnos_ui_set_page(0);
    regression_tick();
    assert(strcmp(fixture.alerts[0].lv, "warn") == 0);
    assert(strcmp(fixture.alerts[1].lv, "crit") == 0);
    char expect1[128];
    alert_expected_cn(fixture.alerts[1].m, expect1, sizeof expect1);
    for (int page = 0; page < 5; page++) {
        fnos_ui_set_page(page);
        regression_tick();
        assert(!visible_text(lv_screen_active(), expect1));
        assert(!visible_text(lv_screen_active(), "存在严重告警"));
    }
    fnos_ui_set_page(5);
    regression_tick();
    assert(visible_text(lv_screen_active(), expect1));
    int dropped = fixture.source_dropped;
    fixture.source_dropped = 3;
    regression_tick();
    assert(visible_text_containing(lv_screen_active(), "采集端省略 3 项"));
    fixture.source_dropped = dropped;
    fnos_ui_set_page(0);
    regression_tick();
    assert(!visible_text_containing(lv_screen_active(), "采集端省略"));

    /* Future levels remain informational on the Alerts page. */
    int alert_count = fixture.nalerts;
    const char *level0 = fixture.alerts[0].lv, *level1 = fixture.alerts[1].lv;
    fixture.nalerts = 2;
    fnos_status_text(&fixture, &fixture.alerts[0].lv, "caution");
    fnos_status_text(&fixture, &fixture.alerts[1].lv, "future");
    regression_tick();
    char expect0[128];
    alert_expected_cn(fixture.alerts[0].m, expect0, sizeof expect0);
    assert(!visible_text(lv_screen_active(), expect0));
    assert(!visible_text(lv_screen_active(), "无采集告警"));
    fnos_ui_set_page(5); regression_tick();
    assert(visible_text(lv_screen_active(), "严重 0 · 警告 0 · 提示 2"));
    fnos_status_text(&fixture, &fixture.alerts[0].lv, "%s", level0);
    fnos_status_text(&fixture, &fixture.alerts[1].lv, "%s", level1);
    fixture.nalerts = alert_count;

    /* Explicit stale state on each container row, not only the page summary. */
    fixture.online = false;
    fixture.recv_ms -= 96000;
    snprintf(fixture.last_err, sizeof fixture.last_err, "%s", "connect/timeout");
    fnos_ui_set_page(3);
    regression_tick();
    int expected_up = 0;
    for (int i = 0; i < fixture.ndocker; i++) if (fixture.docker[i].up) expected_up++;
    assert(audit_stale_running_labels(lv_screen_active()) == expected_up);

    /* The diagnostic toggle path already exists. Current navigation selection is the missing path. */
    click_text("设置");
    assert(visible_text(lv_screen_active(), "返回"));
    regression_tick(); /* The opening fade owns input until the surface has arrived. */
    click_text("系统");
    assert(!visible_text(lv_screen_active(), "返回"));
    assert(visible_text(lv_screen_active(), "容器服务"));
    click_text("系统"); click_text("系统");
    assert(visible_text(lv_screen_active(), "容器服务"));

    fixture.online = true;
    fixture.nalerts = 0;
    fixture.last_err[0] = 0;
    fixture.recv_ms = esp_timer_get_time() / 1000;
    fnos_ui_set_page(4);
    regression_tick();
    lv_obj_t *name = visible_text(lv_screen_active(), "Demo Processor");
    assert(name);
    lv_obj_t *cpu = temperature_device_block(name);
    assert(cpu);
    /* Start in compact form even when an earlier preview step left manual details open. */
    if (visible_text(lv_screen_active(), "Core 0")) click_object_center(name);
    assert(!visible_text(lv_screen_active(), "Core 0"));
    int32_t compact_height = lv_obj_get_height(cpu);
    for (int cycle = 0; cycle < 2; cycle++) {
        click_object_center(name);
        assert(visible_text(lv_screen_active(), "Core 0"));
        int32_t expanded_height = lv_obj_get_height(cpu);
        assert(expanded_height > compact_height);
        click_object_center(name);
        assert(!visible_text(lv_screen_active(), "Core 0"));
        assert(lv_obj_get_height(cpu) == compact_height);
    }
    snapshot("11-temperature-collapsed");

    /* Same channel/device set and display size, changing temperatures only. */
    s_position_n = 0;
    capture_positions(lv_screen_active());
    assert(s_position_n > 0);
    float temperatures[PREVIEW_TEMPS];
    for (int i = 0; i < fixture.ntemps; i++) temperatures[i] = fixture.temps[i].c;
    for (int frame = 0; frame < 40; frame++) {
        float jitter = (frame % 2) ? 0.1f : -0.1f; /* input scenario, not a UI risk threshold */
        for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = temperatures[i] + jitter;
        fixture.recv_ms = esp_timer_get_time() / 1000;
        regression_tick();
        assert(!visible_text(lv_screen_active(), "Core 0"));
        assert_positions_unchanged();
    }
    snapshot("11-temperature-stable-refresh");
    s_override_status = NULL;
    s_state = old_state;
    fnos_ui_set_page(old_page);
    regression_tick();
    puts("checks: current-system navigation exits diagnostics, alerts confined to Alerts page, stale containers, temperature expand/collapse height recovery, stable refresh positions PASS");
    fnos_status_release(&fixture);     /* M17：句柄用完必须还 */
}


/* 收集当前可见的所有标签文字，用于"加了这条原因之后，界面上多出了哪句话"这种差分断言 */
#define MAX_TEXTS 400
static const char *s_texts[MAX_TEXTS];
static int s_texts_n;
/* Protocol capacity and persisted user choice are verified through the displayed UI. */
static void verify_temperature_protocol_limit(void)
{
    preview_state_t old_state = s_state;
    int old_page = fnos_ui_page();
    fnos_status_t fixture = {0};
    fill_live(&fixture);
    fixture.nalerts = 0;
    fixture.ntemps = PREVIEW_TEMPS;
    for (int i = 0; i < fixture.ntemps; i++) {
        fnos_status_text(&fixture, &fixture.temps[i].dev, "%s", "CPU");
        fnos_status_text(&fixture, &fixture.temps[i].dn, "%s", "Demo Processor");
        fnos_status_text(&fixture, &fixture.temps[i].ch, "Core %02d", i);
        fixture.temps[i].c = 40.0f + i * 0.1f;
    }
    s_override_status = &fixture;
    fnos_ui_set_page(4);
    regression_tick();
    lv_obj_t *name = visible_text(lv_screen_active(), "Demo Processor");
    assert(name);
    lv_obj_t *cpu = temperature_device_block(name);
    assert(cpu);
    if (visible_text(lv_screen_active(), "Core 00")) click_object_center(name);
    int32_t compact = lv_obj_get_height(cpu);
    click_object_center(name);
    assert(lv_obj_get_height(cpu) > compact);
    for (int i = 0; i < fixture.ntemps; i++) {
        char channel[strlen(fixture.temps[i].ch)+1];
        snprintf(channel, sizeof channel, "Core %02d", i);
        assert(visible_text(lv_screen_active(), channel));
    }
    char last_name[64];
    snprintf(last_name, sizeof last_name, "Core %02d", fixture.ntemps - 1);
    lv_obj_t *last = visible_text(lv_screen_active(), last_name);
    lv_obj_scroll_to_view_recursive(last, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_t *viewport = lv_obj_get_parent(last);
    while (viewport && !lv_obj_has_flag(viewport, LV_OBJ_FLAG_SCROLLABLE)) viewport = lv_obj_get_parent(viewport);
    assert(viewport);
    lv_area_t content, row;
    lv_obj_get_content_coords(viewport, &content);
    lv_obj_get_coords(last, &row);
    assert(row.y1 >= content.y1 - 2 && row.y2 <= content.y2 + 2);
    snapshot("12-cpu-max-channels-expanded");

    /* Refresh must preserve the explicitly opened details and their geometry. */
    s_position_n = 0;
    capture_positions(lv_screen_active());
    for (int frame = 0; frame < 20; frame++) {
        for (int i = 0; i < fixture.ntemps; i++) fixture.temps[i].c = 40.0f + i * 0.1f + (frame % 2 ? 0.1f : -0.1f);
        fixture.recv_ms = esp_timer_get_time() / 1000;
        regression_tick();
        assert(visible_text(lv_screen_active(), last_name));
        assert_positions_unchanged();
    }
    /* Insert a device earlier in the sort order, then remove it. CPU must keep its user choice. */
    fnos_temp_t channels[PREVIEW_TEMPS];
    memcpy(channels, fixture.temps, sizeof channels);
    memmove(&fixture.temps[1], &fixture.temps[0], (PREVIEW_TEMPS - 1) * sizeof fixture.temps[0]);
    fnos_status_text(&fixture, &fixture.temps[0].dev, "%s", "BOARD");
    fnos_status_text(&fixture, &fixture.temps[0].dn, "%s", "Board");
    fnos_status_text(&fixture, &fixture.temps[0].ch, "%s", "temp1");
    regression_tick();
    assert(visible_text(lv_screen_active(), "Core 00"));
    snapshot("12-temperature-device-added");
    memcpy(fixture.temps, channels, sizeof channels);
    regression_tick();
    name = visible_text(lv_screen_active(), "Demo Processor");
    cpu = temperature_device_block(name);
    assert(visible_text(lv_screen_active(), last_name));
    click_object_center(name);
    assert(!visible_text(lv_screen_active(), last_name));
    assert(lv_obj_get_height(cpu) == compact);
    snapshot("12-cpu-max-channels-collapsed");

    /* 串口 'temp n' 的落地点（fnos_ui_request_temp_expand）必须与手指点一下等效：
       板上没有触摸自动化，实机的"展开态"照片只有走这条路才拍得到。 */
    fnos_ui_request_temp_expand(0);
    regression_tick();
    assert(visible_text(lv_screen_active(), last_name));
    assert(lv_obj_get_height(cpu) > compact);
    fnos_ui_request_temp_expand(99);     /* 越界索引：吞掉，不许崩也不许乱展开 */
    regression_tick();
    assert(lv_obj_get_height(cpu) > compact);
    click_object_center(name);           /* 折回去，别把展开态留给后面的用例 */
    assert(lv_obj_get_height(cpu) == compact);

    s_override_status = NULL;
    s_state = old_state;
    fnos_ui_set_page(old_page);
    regression_tick();
    puts("checks: all protocol temperature channels manually expand, last channel scrolls into view, explicit details persist, height recovers PASS");
    fnos_status_release(&fixture);     /* M17：句柄用完必须还 */
}

static void collect_texts(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_has_class(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        if (t && t[0] && s_texts_n < MAX_TEXTS) s_texts[s_texts_n++] = t;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) collect_texts(lv_obj_get_child(o, i));
}
static int has_text(const char *t)
{
    for (int i = 0; i < s_texts_n; i++) if (!strcmp(s_texts[i], t)) return 1;
    return 0;
}

/* 连接失败的原因标签是**跨文件的契约**：`fnos_data.c` 产出短标签，`fnos_ui.c` 的
   `link_reason()` 翻成人话。`tools/reason_check.py` 静态核对两边集合；这里补运行时
   那一半——把每个标签都灌进去，断言界面上**确实多出一句话**。
   少一个标签的后果很隐蔽：`link_reason()` 返回 NULL，那行字整条消失，界面看着只是
   "没写原因"，用户不知道该干什么。断言用差分（跟"没有原因时"比），所以不用把
   那 11 句人话在预览里再抄一遍——抄一遍就又有了漂移的机会。 */
/* 连接失败的原因标签是**跨文件的契约**：`fnos_data.c` 产出短标签，`fnos_ui.c` 的
   `link_reason()` 翻成人话，显示在总览页健康卡右侧那行。`tools/reason_check.py`
   静态核对两边的集合；这里补运行时那一半——**逐个标签断言翻得出话、每句都不一样、
   而且真的画到了屏幕上**。

   为什么不用"界面上有没有新文字"这种弱断言：link_reason() 不认得某个标签时会回落
   到通用文案「离线，显示的是最后一次采集的快照」，那也是"新文字"；界面上的顶栏时间
   每换一个状态也会变。弱断言会把这两种都当成人话放过去——试过，`no cert` 漏掉了。 */
static void verify_reasons(void)
{
    static const char *TAGS[] = {
        "no wifi", "dns fail", "connect/timeout", "tls handshake", "tls setup",
        "cert parse", "no cert", "token rejected", "bad payload", "http status", "read error",
    };
    const int NT = (int)(sizeof TAGS / sizeof TAGS[0]);
    fnos_status_t st = {0};
    memset(&st, 0, sizeof st);
    st.ever_ok = true;
    st.online = false;

    static char reason[16][96];
    int bad = 0;
    for (int k = 0; k < NT; k++) {
        snprintf(st.last_err, sizeof st.last_err, "%s", TAGS[k]);
        const char *why = fnos_ui_link_reason(&st);
        if (!why || !why[0]) {
            fprintf(stderr, "reason \"%s\" 翻不出说明（fnos_ui_link_reason 返回 NULL）—— "
                            "这种失败下总览页那行「为什么连不上」会整条消失\n", TAGS[k]);
            bad++;
            continue;
        }
        snprintf(reason[k], sizeof reason[k], "%s", why);
    }
    for (int i = 0; i < NT; i++) {
        for (int j = i + 1; j < NT && reason[i][0] && reason[j][0]; j++) {
            if (!strcmp(reason[i], reason[j])) {
                fprintf(stderr, "\"%s\" 与 \"%s\" 翻出同一句话「%s」—— 至少有一条没被分开\n",
                        TAGS[i], TAGS[j], reason[i]);
                bad++;
            }
        }
    }

    /* 还要确认它真的画到了屏幕上（不是只翻得出、却没接上界面） */
    static const char *FALLBACK[] = { "正在连接采集端…", "离线，显示的是最后一次采集的快照" };
    fnos_ui_set_page(0);
    s_state = ST_LIVE;                        /* 上个 verify_* 可能停在 ST_LOCKBUSY */
    for (int k = 0; k < NT; k++) {
        if (!reason[k][0]) continue;
        s_force_err = TAGS[k];
        vtick_advance(500); lv_timer_handler();
        if (!strcmp(TAGS[k], "token rejected")) snapshot("11-connection-reason");
        if (!visible_text(lv_screen_active(), reason[k])) {
            fprintf(stderr, "reason \"%s\" 翻成了「%s」，但屏幕上找不到这句话\n", TAGS[k], reason[k]);
            bad++;
        }
    }
    s_force_err = NULL;
    vtick_advance(500); lv_timer_handler();
    (void)FALLBACK;
    assert(bad == 0);
    printf("checks: 11 条连接失败原因各自翻出不同的说明、且都画到了屏幕上 PASS\n");
}

/* 把设备端**每一条**配对消息都真的渲染一遍。
   预览的桩件只覆盖 UNPROVISIONED/CONFIRM/PROVISIONED，FAILED 那条分支
   （八条文案，全是出事时用户看到的话）从没被画出来过 —— 而包装标签会不会截断、
   会不会压到别的构件，只有画出来才知道。audit_labels 会 abort，所以这里只要能
   走完就说明都没问题。 */
/* ── 对比度审计 ────────────────────────────────────────────────────────
   夜间模式是**一整套独立的配色**（rail、屏幕底色、卡片底色、卡片描边都换），
   而预览从来没有渲染过它 —— 也就是说夜里的配色长什么样，谁也没看过。
   这类问题的典型表现不是"画错了"而是"看不清"：某个标签的字色正好落在
   背景色附近，白天能看、夜里贴到一起。眼睛看样张不一定看得出来，
   但对比度是能算的。这里按 WCAG 的相对亮度公式算，**先把实测值打出来**。 */
static double srgb_lin(int c8)
{
    double c = c8 / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
static double luminance(uint32_t rgb)
{
    return 0.2126 * srgb_lin((rgb >> 16) & 0xFF) + 0.7152 * srgb_lin((rgb >> 8) & 0xFF)
         + 0.0722 * srgb_lin(rgb & 0xFF);
}
static double contrast(uint32_t a, uint32_t b)
{
    double la = luminance(a), lb = luminance(b);
    if (la < lb) { double t = la; la = lb; lb = t; }
    return (la + 0.05) / (lb + 0.05);
}
/* Composite transparent containers and text against the actual ancestor surface. */
static uint32_t eff_bg(lv_obj_t *o)
{
    if (!o) return 0;
    lv_opa_t opa = lv_obj_get_style_bg_opa(o, 0);
    uint32_t color = lv_color_to_u32(lv_obj_get_style_bg_color(o, 0)) & 0xFFFFFFu;
    if (opa == LV_OPA_COVER) return color;
    uint32_t parent = eff_bg(lv_obj_get_parent(o));
    return lv_color_to_u32(lv_color_mix(lv_color_hex(color), lv_color_hex(parent), opa)) & 0xFFFFFFu;
}
/* Most labels are 12–16px; a blanket large-text threshold missed real failures.
   Check every static state and page against the normal-text minimum. */
#define CONTRAST_MIN 4.5

static double s_worst_contrast;
static const char *s_worst_text = "";
static uint32_t s_worst_fg, s_worst_bg;
static void audit_contrast(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_has_class(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        if (t && t[0]) {
            lv_opa_t opa = lv_obj_get_style_text_opa(o, 0);
            if (opa > LV_OPA_20) {
                uint32_t fg = lv_color_to_u32(lv_obj_get_style_text_color(o, 0)) & 0xFFFFFFu;
                uint32_t bg = eff_bg(lv_obj_get_parent(o));
                fg = lv_color_to_u32(lv_color_mix(lv_color_hex(fg), lv_color_hex(bg), opa)) & 0xFFFFFFu;
                double c = contrast(fg, bg);
                if (s_worst_contrast == 0 || c < s_worst_contrast) {
                    s_worst_contrast = c; s_worst_text = t; s_worst_fg = fg; s_worst_bg = bg;
                }
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) audit_contrast(lv_obj_get_child(o, i));
}
static void report_contrast(const char *tag)
{
    s_worst_contrast = 0; s_worst_text = "";
    audit_contrast(lv_screen_active());
    printf("  对比度[%s] 最低 %.2f:1 —— 「%s」字色 #%06X on 底色 #%06X\n",
           tag, s_worst_contrast, s_worst_text, (unsigned)s_worst_fg, (unsigned)s_worst_bg);
    if (s_worst_contrast < CONTRAST_MIN) {
        fprintf(stderr, "对比度不足（%s）：「%s」只有 %.2f:1，低于 %.1f:1\n",
                tag, s_worst_text, s_worst_contrast, CONTRAST_MIN);
        abort();
    }
}

static void verify_pair_messages(void)
{
    /* verify_pairing() 收尾会点「关闭」，所以这里点一次就是"打开"。
       s_pair_open 是 fnos_ui.c 里的 static，预览取不到——所以**用屏幕上的字来确认面板真的开了**，
       而不是假设它开了（下面的断言就是干这个的）。 */
    click_text("配对");
    int shown = 0;
    for (int i = 0; i < PAIR_MSG_COUNT; i++) {
        s_pair_state = (fnos_pair_state_t)PAIR_MSGS[i].state;
        snprintf(s_pair_msg, sizeof s_pair_msg, "%s", PAIR_MSGS[i].msg);
        vtick_advance(500);               /* 走一遍真实的刷新路径（ui_tick 挂在这上面） */
        lv_timer_handler();
        lv_refr_now(lv_display_get_default());
        audit_labels(lv_screen_active()); /* 缺字/超高都会在这里 abort */
        audit_format_residue(lv_screen_active(), "nightly-matrix");
        audit_bounds(lv_screen_active());
        char want[192];
        if (s_pair_state == FNOS_PAIR_FAILED) {
            snprintf(want, sizeof want, "失败原因：%s", PAIR_MSGS[i].msg);
        } else if (s_pair_state == FNOS_PAIR_CONFIRM) {
            snprintf(want, sizeof want, "%s", "核对证书指纹");
        } else if (s_pair_state == FNOS_PAIR_UNPROVISIONED) {
            snprintf(want, sizeof want, "%s", "与 NAS 配对");
        } else {
            snprintf(want, sizeof want, "%s", PAIR_MSGS[i].msg);
        }
        if (!visible_text(lv_screen_active(), want)) {
            fprintf(stderr, "pair state %d row %d expected visible text: %s\n", s_pair_state, i, want);
            abort();
        }
        if (s_pair_state == FNOS_PAIR_FETCHING) snapshot("07-pair-fetching");
        shown++;
    }
    printf("checks: %d pairing state/message cases audited with visible branch assertions PASS\n", shown);
    /* 留一张 FAILED 的样张：这条分支以前从来没有出现在任何图里 */
    s_pair_state = FNOS_PAIR_FAILED;
    snprintf(s_pair_msg, sizeof s_pair_msg, "%s", "配对成功但写入 NVS 失败");
    vtick_advance(500);
    lv_timer_handler();
    assert(visible_text_containing(lv_screen_active(), "本地保存失败"));
    snapshot("07-pair-failed");
}

/* 板上配网卡：出厂固件没有凭据时用户要走的整条路。
   这张卡是新加的，四个阶段（扫描 / 输口令 / 连接中 / 结果）各拍一张，
   顺带把 41 键键盘的两层键表都渲染过 —— 否则"字库覆盖"就是假的。 */
static void wifi_click(const char *txt)
{
    printf("  [wifi] 点 \"%s\"\n", txt);
    fflush(stdout);
    click_text(txt);
}

/* LVGL DOTS temporarily replaces the label buffer tail. Restore it to inspect
   the complete source text, then restore DOTS before rendering and bounds checks. */
static void assert_ellipsized_source(const char *prefix, const char *expected)
{
    lv_obj_t *label = visible_text_containing(lv_screen_active(), prefix);
    assert(label);
    lv_label_long_mode_t mode = lv_label_get_long_mode(label);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_update_layout(lv_screen_active());
    /* 不留 assert：这条断言真正跑起来时要能说清"到底显示成了什么"。
       只写 assert 的话失败信息只有 expected，实际文本要重新加 printf 才能看到。 */
    const char *actual = lv_label_get_text(label);
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "verify: 标签原文对不上 —— 实际=\"%s\" 期望=\"%s\"（前缀 \"%s\"）\n",
                actual, expected, prefix);
        abort();
    }
    lv_label_set_long_mode(label, mode);
}

static void verify_wifi_footer(const char *anchor)
{
    lv_obj_t *label = visible_text_clickable(lv_screen_active(), anchor);
    assert(label);
    lv_obj_t *foot = lv_obj_get_parent(lv_obj_get_parent(label));
    lv_area_t bounds;
    lv_obj_get_coords(foot, &bounds);
    for (uint32_t i = 0; i < lv_obj_get_child_count(foot); i++) {
        lv_obj_t *button = lv_obj_get_child(foot, i);
        if (lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_area_t a;
        lv_obj_get_coords(button, &a);
        assert(a.x1 >= bounds.x1 && a.x2 <= bounds.x2 && "Wi-Fi action must fit without horizontal scrolling");
        lv_obj_scroll_to_view_recursive(button, LV_ANIM_OFF);
        lv_obj_update_layout(lv_screen_active());
        lv_obj_get_coords(button, &a);
        assert(a.y1 >= 0 && a.y2 < s_scr_h && "Wi-Fi action must be reachable");
    }
}

static void verify_wifi_keyboard(void)
{
    lv_obj_t *space = visible_text_clickable(lv_screen_active(), "空格");
    assert(space);
    lv_obj_t *keyboard = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(space)));
    lv_obj_t *body = lv_obj_get_parent(keyboard);
    lv_obj_t *close = lv_obj_get_parent(visible_text_clickable(lv_screen_active(), "关闭"));
    lv_area_t before, after;
    lv_obj_get_coords(close, &before);
    for (uint32_t r = 0; r < lv_obj_get_child_count(keyboard); r++) {
        lv_obj_t *row = lv_obj_get_child(keyboard, r);
        for (uint32_t i = 0; i < lv_obj_get_child_count(row); i++) {
            lv_obj_t *key = lv_obj_get_child(row, i);
            lv_area_t ra, ka;
            lv_obj_get_coords(row, &ra);
            lv_obj_get_coords(key, &ka);
            assert(ka.y1 >= ra.y1 && ka.y2 <= ra.y2 && "keyboard key must fit its row");
            lv_obj_scroll_to_view_recursive(key, LV_ANIM_OFF);
            lv_obj_update_layout(lv_screen_active());
            lv_obj_get_coords(key, &ka);
            lv_area_t view;
            lv_obj_get_content_coords(body, &view);
            assert(ka.y1 >= view.y1 && ka.y2 <= view.y2 && "keyboard key must be reachable inside the scroll viewport");
        }
    }
    lv_obj_get_coords(close, &after);
    assert(before.x1 == after.x1 && before.x2 == after.x2 &&
           before.y1 == after.y1 && before.y2 == after.y2 && "Wi-Fi footer must stay pinned while typing");
    lv_obj_scroll_to_y(body, 0, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t view, last;
    lv_obj_get_content_coords(body, &view);
    lv_obj_get_coords(lv_obj_get_child(keyboard, -1), &last);
    assert(last.y1 >= view.y1 && last.y2 <= view.y2 &&
           "all keyboard rows must be fully visible on entry");
}

static void verify_wifi(void)
{
    /* 配对卡现在是模态的；上一个用例可能留着它，先显式关掉再走配网流程。 */
    lv_obj_t *leftover = visible_text_clickable(lv_screen_active(), "关闭");
    if (leftover) click_object_center(leftover);
    s_state = ST_LIVE;
    wifi_stub(false, false, "", "");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "Wi-Fi 未配置"));
    wifi_click("Wi-Fi 未配置");                    /* 顶栏入口（真机首次开机由 ui_tick 自动弹） */
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "接入 Wi-Fi"));
    assert(visible_text(lv_screen_active(), "重新扫描"));
    assert(visible_text(lv_screen_active(), "Demo-Guest"));   /* 扫描结果真的填进去了 */
    snapshot("10-wifi-scan");
    verify_wifi_footer("重新扫描");

    /* 选一个加密网络 → 输口令页：整块键盘第一次露面（41 键 + 两行输入 + 显示/隐藏） */
    wifi_click("Demo");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "空格"));
    snapshot("10-wifi-pass");
    verify_wifi_footer("返回");
    verify_wifi_keyboard();

    /* 显示口令后关闭：连隐藏标签也应复位，重新选择网络不能复用旧口令。 */
    for (const char *k = "abcdefghi"; *k; k++) { char t[2] = { *k, 0 }; wifi_click(t); }
    wifi_click("显示");
    lv_obj_t *password_label = visible_text(lv_screen_active(), "abcdefghi");
    assert(password_label);
    uk_test_alloc_fail_after(0);  /* 短 SSID 与口令掩码无需临时分配。 */
    wifi_click("隐藏");
    assert(!uk_test_alloc_was_triggered());
    assert(!strcmp(lv_label_get_text(password_label), "*********"));
    assert(visible_text(lv_screen_active(), "显示"));
    uk_test_alloc_fail_after(-1);
    uk_alloc_reset();
    wifi_click("显示");
    assert(!strcmp(lv_label_get_text(password_label), "abcdefghi"));
    wifi_click("关闭");
    assert(!strcmp(lv_label_get_text(password_label), "口令"));
    wifi_click("Wi-Fi 未配置");
    wifi_click("Demo");
    assert(visible_text(lv_screen_active(), "口令"));
    assert(visible_text(lv_screen_active(), "显示"));

    /* 口令不足 8 位：必须拒绝并说明原因 —— 真机上这条最容易白折腾 */
    wifi_click("a"); wifi_click("b"); wifi_click("c");
    wifi_click("连接");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(),
           "WPA2 口令至少 8 位 —— 再检查一遍（区分大小写），现在是 3 位"));

    /* 大写（⇧）与退格各走一遍，再把键表换到数字符号层 —— 两套键表都要真的画过 */
    wifi_click("大写"); wifi_click("A");
    wifi_click("退格"); wifi_click("c");
    wifi_click("123");
    vtick_advance(300); lv_timer_handler();
    snapshot("10-wifi-symbols");
    wifi_click("abc");

    /* 够 8 位就放行 → 连接页 */
    for (const char *k = "abcdefgh"; *k; k++) { char t[2] = { *k, 0 }; wifi_click(t); }
    wifi_click("连接");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "正在连接…"));
    snapshot("10-wifi-linking");
    verify_wifi_footer("关闭");

    /* 替身：拿到 IP 了。真机上 GOT_IP 事件同时会把"本轮提交"判成成功（清回 IDLE），
       所以这里必须走 wifi_stub（它带这个结论），不能只手改 s_wifi_on —— 否则
       提交状态还停在 PENDING，屏幕按 M04 的规则会继续显示"正在连接…"。 */
    wifi_stub(true, true, "Demo", "");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "连接成功"));
    assert(visible_text(lv_screen_active(),
           "已连接 Demo（192.0.2.42） · 信号 -54 dBm\n"
           "板子已经记住这个网络，下次开机自动连。"));
    snapshot("10-wifi-linked");
    verify_wifi_footer("关闭");

    /* 连不上：标题与原因都要说人话（不能一直写"正在连接…"） */
    wifi_stub(true, false, "Demo", "密码不对（认证失败）");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "没连上"));
    assert(!visible_text(lv_screen_active(), "连接成功"));
    assert(visible_text(lv_screen_active(), "修改密码"));
    assert(!visible_text(lv_screen_active(), "完成"));
    assert(visible_text(lv_screen_active(), "Wi-Fi 未连接"));
    snapshot("10-wifi-failed");
    verify_wifi_footer("关闭");
    wifi_stub(true, false, "Demo", "凭据保存失败（NVS 写入错误）");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text_containing(lv_screen_active(), "凭据保存失败（NVS 写入错误）"));
    assert(visible_text_containing(lv_screen_active(), "请按原因排查后重试"));
    assert(!visible_text_containing(lv_screen_active(), "2.4G"));
    snapshot("10-wifi-store-failed");
    verify_wifi_footer("关闭");
    wifi_click("修改密码");
    assert(visible_text(lv_screen_active(), "空格"));
    assert(visible_text(lv_screen_active(), "Demo"));
    snapshot("10-wifi-retry");
    verify_wifi_footer("返回");
    /* ── M04：第一次配网就失败 ────────────────────────────────────────────
       以前凭据还是空的 → 状态被判成"未配置" → 屏幕永远停在"正在连接…"，
       用户根本不知道这一轮已经失败了。现在这一轮提交有自己的结论。 */
    wifi_click("连接");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "正在连接…"));      /* 提交中：绝不能是成功 */
    assert(!visible_text(lv_screen_active(), "连接成功"));
    snapshot("10-wifi-submitting");
    wifi_stub(false, false, "Demo", "密码不对（认证失败）");    /* 这一轮提交的结论：失败 */
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "没连上"));
    assert(!visible_text(lv_screen_active(), "正在连接…"));
    assert(visible_text(lv_screen_active(), "修改密码"));       /* 给用户一条出路 */
    assert(visible_text(lv_screen_active(), "Wi-Fi 未连接"));   /* 顶栏也是结论，不是"未配置" */
    assert(!visible_text(lv_screen_active(), "Wi-Fi 未配置"));
    assert(visible_text_containing(lv_screen_active(), "密码不对（认证失败）"));  /* 原因来自这一轮提交 */
    snapshot("10-wifi-first-fail");
    verify_wifi_footer("关闭");
    wifi_click("修改密码");
    assert(visible_text(lv_screen_active(), "Demo"));
    wifi_click("连接");
    wifi_stub(true, true, "Demo", "");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "完成"));
    wifi_click("完成");
    vtick_advance(UK_REVEAL_MS + 20); lv_timer_handler();
    assert(!visible_text(lv_screen_active(), "接入 Wi-Fi"));

    /* ── M05：已经在线时改密码 ────────────────────────────────────────────
       旧逻辑看 s_online 还是 true → 屏幕上写"连接成功"，而这一刻其实正在按新口令重连。
       让板子先在 Demo-5G 上在线（顶栏文字与列表里的行不同名，点谁不含糊），
       用户再去连另一个网络 Demo。 */
    wifi_stub(true, true, "Demo-5G", "");
    vtick_advance(500); lv_timer_handler();
    wifi_click("Demo-5G");                                   /* 顶栏按钮 → 配网卡（扫描页） */
    vtick_advance(500); lv_timer_handler();
    wifi_click("Demo");                                      /* 选加密网络 → 输口令页 */
    for (const char *k = "abcdefgh"; *k; k++) { char t[2] = { *k, 0 }; wifi_click(t); }
    wifi_click("连接");                                      /* 提交：此刻 online 仍是 true */
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "正在连接…"));
    assert(!visible_text(lv_screen_active(), "连接成功"));
    snapshot("10-wifi-change-while-online");
    verify_wifi_footer("关闭");
    wifi_stub(true, true, "Demo", "");                        /* 新凭据连上了 */
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "完成"));
    wifi_click("完成");                                       /* 关掉卡（下面那段要接着验顶栏） */

    /* 关掉卡：顶栏要一直看得见状态；配好了就该是 SSID，不是"未配置" */
    vtick_advance(500); lv_timer_handler();
    assert(!visible_text(lv_screen_active(), "接入 Wi-Fi"));
    wifi_stub(true, true, "Demo", "");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "Demo"));
    assert(!visible_text(lv_screen_active(), "Wi-Fi 未配置"));
    const char *long_ssid = "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456";
    assert(strlen(long_ssid) == 32);
    wifi_stub(true, true, long_ssid, "");
    vtick_advance(500); lv_timer_handler();
    assert_ellipsized_source("ABC", long_ssid);
    snapshot("10-wifi-long-ssid");
    wifi_stub(true, false, long_ssid, "");
    vtick_advance(500); lv_timer_handler();
    assert_ellipsized_source("连接中", "连接中 · ABCDEFGHIJKLMNOPQRSTUVWXYZ123456");
    snapshot("10-wifi-long-ssid-linking");
    wifi_stub(true, false, "ABCDEFGHIJKLMNOPQRSTUVWXYZ12345", "");
    vtick_advance(500); lv_timer_handler();
    assert_ellipsized_source("连接中", "连接中 · ABCDEFGHIJKLMNOPQRSTUVWXYZ12345");
    snapshot("10-wifi-long-ssid-linking-31");
    wifi_stub(true, true, "Demo", "");
    vtick_advance(500); lv_timer_handler();

    /* Narrower side-by-side fields must retain protocol-length input and a full keyboard. */
    wifi_click("Demo");
    wifi_click("手动输入");
    vtick_advance(500); lv_timer_handler();
    char input_ssid[33], input_password[64];
    for (int i = 0; i < 32; i++) input_ssid[i] = i % 2 ? 'z' : 'w';
    input_ssid[32] = 0;
    for (int i = 0; i < 63; i++) input_password[i] = i % 2 ? 'z' : 'w';
    input_password[63] = 0;
    for (const char *k = input_ssid; *k; k++) { char t[2] = { *k, 0 }; wifi_click(t); }
    lv_obj_t *ssid_input = visible_text(lv_screen_active(), input_ssid);
    assert(ssid_input);
    assert(lv_label_get_long_mode(ssid_input) == LV_LABEL_LONG_MODE_SCROLL);
    wifi_click("口令");
    for (const char *k = input_password; *k; k++) { char t[2] = { *k, 0 }; wifi_click(t); }
    wifi_click("显示");
    lv_obj_t *password_input = visible_text(lv_screen_active(), input_password);
    assert(password_input && lv_label_get_long_mode(password_input) == LV_LABEL_LONG_MODE_SCROLL);
    verify_wifi_keyboard();
    snapshot("10-wifi-input-limits");
    wifi_click("关闭");
    assert(!strcmp(lv_label_get_text(password_input), "口令"));
}

/* First-entry keypad checks must not repair clipping by scrolling to a key. */
static void assert_pair_keypad_visible(void)
{
    static const char *const keys[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "删除", "0", "确认"
    };
    lv_obj_update_layout(lv_screen_active());
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        lv_obj_t *label = visible_text_clickable(lv_screen_active(), keys[i]);
        assert(label);
        lv_obj_t *button = lv_obj_get_parent(label);
        lv_area_t full, visible;
        lv_obj_get_coords(button, &full);
        assert(visible_click_area(button, &visible));
        if (full.x1 != visible.x1 || full.x2 != visible.x2 ||
            full.y1 != visible.y1 || full.y2 != visible.y2)
            fprintf(stderr, "first-entry pair key %s is clipped\n", keys[i]);
        assert(full.x1 == visible.x1 && full.x2 == visible.x2 &&
               full.y1 == visible.y1 && full.y2 == visible.y2);
    }
}

static void pair_key_in_place(const char *text)
{
    assert_pair_keypad_visible();
    lv_obj_t *label = visible_text_clickable(lv_screen_active(), text);
    assert(label);
    lv_area_t area;
    lv_obj_get_coords(lv_obj_get_parent(label), &area);
    s_touch_point.x = (area.x1 + area.x2) / 2;
    s_touch_point.y = (area.y1 + area.y2) / 2;
    s_touch_down = true; vtick_advance(40); lv_timer_handler();
    s_touch_down = false; vtick_advance(40); lv_timer_handler();
    regression_tick();
}

static void verify_pairing(void)
{
    s_state = ST_HEALTHY;   /* 只有健康快照才有完整的温度通道，用 ST_LIVE 会断言到空数据 */
    vtick_advance(500); lv_timer_handler();
    click_text("配对");                       /* 侧栏入口：切到系统页并盖上配对卡 */
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    assert_pair_keypad_visible();
    pair_key_in_place("0"); pair_key_in_place("确认");
    assert(visible_text(lv_screen_active(), "配对码是 6 位数字，还差 5 位"));
    pair_key_in_place("删除"); pair_key_in_place("确认");
    assert(visible_text(lv_screen_active(), "配对码是 6 位数字，还差 6 位"));
    /* 导航入口是选择，不是开关：重复点击留在配对，系统入口明确返回摘要。 */
    click_text("配对"); click_text("配对");
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    click_text("系统");
    /* 覆盖层现在是模态：导航点击不能把配对卡收掉（否则会顺手清掉配网口令）。 */
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    assert(!visible_text(lv_screen_active(), "容器 8/8 运行 · 24 路温度"));
    click_text("关闭");
    vtick_advance(500); lv_timer_handler();
    assert(!visible_text(lv_screen_active(), "与 NAS 配对"));
    assert(visible_text(lv_screen_active(), "容器 8/8 运行 · 24 路温度"));
    click_text("配对");
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    click_text("设置");
    /* 顶栏也是模态的一部分：配对卡开着时“设置”不生效。 */
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    assert(!visible_text(lv_screen_active(), "返回"));
    click_text("关闭");
    click_text("设置");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "返回"));
    assert(!visible_text(lv_screen_active(), "与 NAS 配对"));
    click_text("配对");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "返回"));
    assert(!visible_text(lv_screen_active(), "与 NAS 配对"));
    click_text("返回");
    vtick_advance(500); lv_timer_handler();
    click_text("配对");
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    /* 面板改成"三步指引 + 配对码槽位 + 键盘 + 操作条"四个模块，标题也从长句
       收成"配对码"（出处移到槽位下面那句提示里）。改版后这几处必须同步。 */
    assert(visible_text(lv_screen_active(), "配对码"));
    assert(visible_text(lv_screen_active(), "三步完成配对"));
    assert(visible_text(lv_screen_active(), "在 NAS 管理页「设备配对」生成 6 位配对码"));
    assert(visible_text(lv_screen_active(), "在 NAS 管理页「设备配对」生成，把 6 位数字输进下方的键盘"));
    /* 左栏（x16 w336）只有一块地方：状态说明与三步指引不能同时出现 —— 真机上
       两者叠在一起过一次（"未配对：用编译期默认地址"盖在"三步完成配对"上）。 */
    assert(!visible_text(lv_screen_active(), "未配对：用编译期默认地址"));
    snapshot("07-pair-code");
    /* Actions and all four keypad rows are visible together without scrolling. */
    lv_obj_t *close = lv_obj_get_parent(visible_text_clickable(lv_screen_active(), "关闭"));
    lv_obj_t *code_title = visible_text(lv_screen_active(), "配对码");
    lv_obj_t *grid = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(code_title)));
    lv_area_t close_before, close_after;
    lv_obj_get_coords(close, &close_before);
    if (close_before.y1 < 0 || close_before.y2 >= lv_display_get_vertical_resolution(NULL))
        fprintf(stderr, "pair actions outside viewport: [%d,%d]-[%d,%d]\n",
                (int)close_before.x1, (int)close_before.y1, (int)close_before.x2, (int)close_before.y2);
    assert(close_before.y1 >= 0 && close_before.y2 < lv_display_get_vertical_resolution(NULL));
    lv_obj_scroll_by(grid, 0, -lv_obj_get_scroll_bottom(grid), LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(close, &close_after);
    assert(memcmp(&close_before, &close_after, sizeof close_before) == 0);
    lv_obj_scroll_to_y(grid, 0, LV_ANIM_OFF);
    assert_pair_keypad_visible();
    lv_obj_t *key = lv_obj_get_parent(visible_text_clickable(lv_screen_active(), "1"));
    uint32_t callbacks = lv_obj_get_event_count(key);
    for (int i = 0; i < 60; i++) { vtick_advance(500); lv_timer_handler(); }
    assert(lv_obj_get_event_count(key) == callbacks);
    lv_obj_t *key_label = lv_obj_get_child(key, 0);
    lv_area_t kr, lr;
    lv_obj_get_coords(key, &kr); lv_obj_get_coords(key_label, &lr);
    assert(abs((kr.y1 + kr.y2) - (lr.y1 + lr.y2)) <= 2);
    s_pair_state = FNOS_PAIR_FETCHING;
    snprintf(s_pair_msg, sizeof s_pair_msg, "%s", "正在从 NAS 取证书…");
    vtick_advance(500); lv_timer_handler();
    snapshot("07-pair-busy");
    assert(!visible_text(lv_screen_active(), "配对码"));
    s_pair_state = FNOS_PAIR_UNPROVISIONED;
    s_pair_msg[0] = 0;
    vtick_advance(500); lv_timer_handler();

    /* 键位：3 位不够，应当只给提示不发起请求。
       提示挂在**右侧配对码块**里（和"配对码/槽位/出处"讲同一件事），不再占用
       左栏 —— 左栏只放"三步指引"或"状态说明"其中之一。 */
    click_text("1"); click_text("2"); click_text("3"); click_text("确认");
    assert(visible_text(lv_screen_active(), "配对码是 6 位数字，还差 3 位"));
    snapshot("07-pair-hint");
    for (int i = 0; i < 3; i++) click_text("删除");
    assert(!visible_text(lv_screen_active(), "配对码是 6 位数字，还差 3 位"));
    click_text("8"); click_text("配对"); click_text("配对");
    click_text("6"); click_text("4"); click_text("2"); click_text("9"); click_text("5");
    snapshot("07-pair-code-filled");
    click_text("确认");

    /* 取到证书：标题改成"核对证书指纹"，四个指纹行应当逐行可见 */
    assert(visible_text(lv_screen_active(), "核对证书指纹"));
    assert(visible_text(lv_screen_active(), "3F:1A:9C:04:E7:52:BB:6D"));
    assert(visible_text(lv_screen_active(), "A9:5E:43:B8:77:D1:08:FE"));
    assert(visible_text(lv_screen_active(), "CN=fnos-nas.local · 到期 2035-10-03"));
    assert(visible_text(lv_screen_active(), "接受并配对"));
    /* 指纹必须落在卡片的右栏里（改版后它是"右上模块"而不是整宽），
       并且整行都在卡片竖条之间——压到卡边或溢出都说明版面被改坏了。 */
    lv_area_t card, fp;
    lv_obj_t *fp_lbl = visible_text(lv_screen_active(), "3F:1A:9C:04:E7:52:BB:6D");
    lv_obj_get_coords(fp_lbl, &fp);
    lv_obj_get_content_coords(lv_obj_get_parent(fp_lbl), &card);
    assert(fp.x1 >= card.x1 && fp.x2 <= card.x2);
    assert(fp.y1 >= card.y1 && fp.y2 <= card.y2);
    snapshot("07-pair-confirm");

    click_text("接受并配对");
    assert(visible_text(lv_screen_active(), "已配对"));
    assert(visible_text(lv_screen_active(), "<NAS_IP>:8798 · HTTPS · CN=fnos-nas.local"));
    assert(visible_text(lv_screen_active(), "解除配对"));
    snapshot("07-pair-done");

    /* 解除要按两次：第一次只换文案，绝不直接清掉凭据 */
    click_text("解除配对");
    assert(visible_text(lv_screen_active(), "再点一次确认解除"));
    assert(visible_text(lv_screen_active(), "已配对"));
    click_text("再点一次确认解除");
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    assert(visible_text(lv_screen_active(), "配对"));       /* 侧栏回到未配对态 */
    /* 收尾把面板关掉：它和「采集诊断」共用系统页的同一个槽位，留着会让后面
       run_state 的样张全都拍成配对面板。 */
    click_text("关闭");
    assert(visible_text(lv_screen_active(), "配对") && !visible_text(lv_screen_active(), "解除配对"));
    puts("checks: pairing panel (code entry, mismatch hint, fingerprint lines, done, two-step forget) PASS");
}

/* 走完整棵树（这次不跳过 HIDDEN），找出一次都没被审计过的可见内容。
   **标记成 HIDDEN 是允许的**（配对卡、诊断面板就是这样），但每一步都该有一步
   把它打开——否则那些像素在真机上什么样，谁也没看过。 */
static int s_never_visible;
static int s_never_printed;
static void audit_never_visible(lv_obj_t *o)
{
    if (!seen_has(o)) {
        bool interesting = lv_obj_has_class(o, &lv_label_class) ||
                           lv_obj_get_child_count(o) == 0;
        if (interesting) {
            lv_area_t c;
            lv_obj_get_coords(o, &c);
            const char *what = lv_obj_has_class(o, &lv_label_class)
                             ? lv_label_get_text(o) : "(叶子容器)";
            /* 只打前 8 条：全量输出会把真正有用的失败信息淹掉（57 行噪声里找一行错误）。 */
            if (s_never_printed++ < 8)
            fprintf(stderr, "never visible in any preview step: [%d,%d %dx%d] 文本=「%s」\n",
                    (int)c.x1, (int)c.y1, (int)(c.x2 - c.x1 + 1), (int)(c.y2 - c.y1 + 1),
                    what);
            s_never_visible++;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) audit_never_visible(lv_obj_get_child(o, i));
}

/* Optional deterministic motion probe; the default full audit suite is untouched.
   PREVIEW_MOTION=0,1,0 selects page targets (indexes < FNOS_UI_PAGE_COUNT).
   PREVIEW_MOTION_INPUT=swipe (default) uses the native pointer driver; api is
   available for isolating animation from gesture recognition.
   Timing inputs in ms: STEP=40, HOLD=800, DRAG=120, RAPID=40, WARMUP=3000.
   A recording deliberately permits intermediate viewport clipping. Stable
   terminal frames still run every existing snapshot geometry/touch audit. */
static fnos_status_t s_motion_fixture;

static unsigned motion_ms(const char *name, unsigned fallback, bool allow_zero)
{
    const char *text = getenv(name);
    if (!text) return fallback;
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || end == text || *end || value > 10000 || (!allow_zero && !value)) {
        fprintf(stderr, "%s: expected %s0..10000 milliseconds\n", name, allow_zero ? "" : ">");
        exit(1);
    }
    return (unsigned)value;
}

static void motion_prepare(void)
{
    if (!getenv("PREVIEW_MOTION") && !getenv("PREVIEW_DATA_MOTION") && !getenv("PREVIEW_REFRESH_WORK")) return;
    /* Freeze both clock-dependent telemetry text and the history cursor; advancing
       native LVGL time must not manufacture new NAS samples or change freshness. */
    s_motion_clock_us = 1000000000LL;
    s_motion_clock_frozen = true;
    s_state = ST_HEALTHY;
    /* 同样不能塞进 assert()（审计 M17）：录制基准拿不到就该停，而不是录一片空屏。 */
    if (!fnos_data_get(&s_motion_fixture)) {
        fprintf(stderr, "motion_prepare: fnos_data_get 失败，动效录制的基准快照拿不到\n");
        abort();
    }
    s_override_status = &s_motion_fixture;
    s_override_sample_enabled = true;
    s_override_sample = (fnos_sample_t){
        .cpu = s_motion_fixture.cpu.pct, .mem = s_motion_fixture.mem.pct,
        .rx_kbs = s_motion_fixture.net.rx_kbs, .tx_kbs = s_motion_fixture.net.tx_kbs,
    };
    s_seq = CONFIG_FNOS_CHART_WINDOW;
    wifi_stub(true, true, "PREVIEW-LAN", "");
}

typedef struct {
    FILE *timeline;
    lv_indev_t *pointer;
    unsigned frame, step, hold, drag, rapid, warmup;
    uint32_t start;
    bool swipe;
} motion_record_t;

static void motion_frame(motion_record_t *record, const char *phase, bool terminal)
{
    /* Host artifact budget, independent of UI topology or device dimensions. */
    if (record->frame >= 512) {
        fprintf(stderr, "motion: frame budget exceeded; increase STEP or shorten timing inputs\n");
        exit(1);
    }
    char name[96];
    snprintf(name, sizeof name, "motion-%04u-%s", record->frame++, phase);
    s_motion_frame = !terminal;
    snapshot(name);  /* s_no_settle remains true for the complete recording. */
    s_motion_frame = false;
    fprintf(record->timeline, "%s.ppm,%u,%d,%s\n", name,
            (unsigned)(vtick_get() - record->start), fnos_ui_page(), phase);
}

static void motion_wait(motion_record_t *record, unsigned ms, const char *phase, bool capture)
{
    unsigned elapsed = 0;
    while (elapsed < ms) {
        unsigned dt = LV_MIN(record->step, ms - elapsed);
        vtick_advance(dt);
        lv_timer_handler();
        elapsed += dt;
        if (capture) motion_frame(record, phase, false);
    }
}

/* Read the real pointer driver immediately so a short input is not lost between
   polling ticks. Native timers still run at each requested sampling step. */
static void motion_pointer(motion_record_t *record, int x, int y, bool down)
{
    s_touch_point = (lv_point_t){ .x = x, .y = y };
    s_touch_down = down;
    lv_indev_read(record->pointer);
}

static void motion_drag(motion_record_t *record, int x1, int y1, int x2, int y2, const char *phase)
{
    motion_pointer(record, x1, y1, true);
    motion_frame(record, phase, false);
    unsigned elapsed = 0;
    while (elapsed < record->drag) {
        unsigned dt = LV_MIN(record->step, record->drag - elapsed);
        vtick_advance(dt);
        elapsed += dt;
        int x = x1 + (x2 - x1) * (int)elapsed / (int)record->drag;
        int y = y1 + (y2 - y1) * (int)elapsed / (int)record->drag;
        motion_pointer(record, x, y, true);
        lv_timer_handler();
        motion_frame(record, phase, false);
    }
    motion_pointer(record, x2, y2, false);
    lv_timer_handler();
    motion_frame(record, phase, false);
}

static lv_obj_t *motion_vertical_viewport(lv_obj_t *root)
{
    if (effectively_hidden(root)) return NULL;
    lv_area_t area;
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_SCROLLABLE) &&
        lv_obj_get_scroll_bottom(root) > 0 && visible_click_area(root, &area) &&
        lv_area_get_height(&area) > 4) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = motion_vertical_viewport(lv_obj_get_child(root, i));
        if (found) return found;
    }
    return NULL;
}

static void motion_target(motion_record_t *record, int target, const char *phase)
{
    int previous = fnos_ui_page();
    if (!record->swipe || target == previous) {
        fnos_ui_set_page(target);       /* public route, never a private callback */
        motion_frame(record, phase, false);
    } else {
        int direction;
        if (target == (previous + 1) % FNOS_UI_PAGE_COUNT) direction = -1;
        else if (target == (previous + FNOS_UI_PAGE_COUNT - 1) % FNOS_UI_PAGE_COUNT) direction = 1;
        else {
            fprintf(stderr, "motion: swipe targets must be adjacent; %d -> %d\n", previous, target);
            exit(1);
        }
        int width = lv_display_get_horizontal_resolution(lv_display_get_default());
        int height = lv_display_get_vertical_resolution(lv_display_get_default());
        int start_x = direction < 0 ? width * 3 / 4 : width / 4;
        int end_x = direction < 0 ? width / 4 : width * 3 / 4;
        int y = height / 2;
        motion_drag(record, start_x, y, end_x, y, phase);
    }
    if (fnos_ui_page() != target) {
        fprintf(stderr, "motion: native %s input selected page %d, expected %d\n",
                record->swipe ? "pointer" : "API", fnos_ui_page(), target);
        exit(1);
    }
}

/* Native page objects are built in public navigation order. Discover them by
   runtime metadata, without depending on a card title, coordinates or count. */
static lv_obj_t *motion_page_by_index(lv_obj_t *root, int wanted, int *seen)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_USER_1))
        return (*seen)++ == wanted ? root : NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *page = motion_page_by_index(lv_obj_get_child(root, i), wanted, seen);
        if (page) return page;
    }
    return NULL;
}

typedef struct { int32_t width, height; uint32_t children; } motion_size_t;

static size_t motion_visible_nodes(lv_obj_t *root)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return 0;
    size_t count = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++)
        count += motion_visible_nodes(lv_obj_get_child(root, i));
    return count;
}

static void motion_save_sizes(lv_obj_t *root, motion_size_t *sizes, size_t *at)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return;
    sizes[(*at)++] = (motion_size_t){
        lv_obj_get_width(root), lv_obj_get_height(root), lv_obj_get_child_count(root),
    };
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++)
        motion_save_sizes(lv_obj_get_child(root, i), sizes, at);
}

/* This probe queues raw pointer samples. Unlike motion_pointer(), it does not
   force lv_indev_read: both native input sampling and motion scheduling run. */
static int motion_min_content_opa(lv_obj_t *root)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return LV_OPA_COVER;
    int minimum = lv_obj_get_style_opa(root, 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++)
        minimum = LV_MIN(minimum, motion_min_content_opa(lv_obj_get_child(root, i)));
    return minimum;
}

/* Keep stable handles: a native clip parent can change traversal order while
   a transition is active, without changing public navigation identity. */
static lv_obj_t *s_motion_pages[FNOS_UI_PAGE_COUNT];

static lv_obj_t *motion_native_page(int index)
{
    assert(index >= 0 && index < FNOS_UI_PAGE_COUNT && s_motion_pages[index]);
    return s_motion_pages[index];
}

static int motion_visible_width(lv_obj_t *page)
{
    lv_area_t area;
    return effectively_hidden(page) || !visible_click_area(page, &area) ? 0 : lv_area_get_width(&area);
}

static void motion_reveal_probe(motion_record_t *record, int base, int target)
{
    assert(target == (base + 1) % FNOS_UI_PAGE_COUNT);
    assert(lv_display_get_render_mode(lv_display_get_default()) == LV_DISPLAY_RENDER_MODE_PARTIAL &&
           "reveal repaint probe requires PREVIEW_PARTIAL=1");
    lv_obj_t *outgoing = motion_native_page(base), *incoming = motion_native_page(target);
    lv_area_t original;
    lv_obj_get_coords(outgoing, &original);
    int width = lv_area_get_width(&original);
    int x = original.x1 + width * 3 / 4;
    int y = (original.y1 + original.y2) / 2;
    motion_pointer(record, x, y, true);
    motion_pointer(record, x - width / 4, y, true);
    motion_wait(record, record->step, "reveal-ready", false);
    motion_frame(record, "reveal-quarter", false);
    lv_area_t a, b;
    lv_obj_get_coords(outgoing, &a);
    lv_obj_get_coords(incoming, &b);
    assert(!memcmp(&a, &original, sizeof a) && !memcmp(&b, &original, sizeof b) &&
           "reveal must keep both pages' content stationary under the moving boundary");
    lv_point_t hit_points[] = {
        { original.x1 + width / 4, y }, { original.x1 + width * 7 / 8, y },
    };
    for (size_t i = 0; i < 2; i++) {
        lv_obj_t *hit = lv_indev_search_obj(lv_screen_active(), &hit_points[i]);
        bool incoming_hit = false;
        for (lv_obj_t *p = hit; p; p = lv_obj_get_parent(p))
            if (p == incoming) incoming_hit = true;
        assert(incoming_hit == (i == 1) &&
               "native hit testing must follow the revealed region");
    }
    size_t bytes = (size_t)s_scr_w * s_scr_h * sizeof *s_frame;
    uint16_t *before = malloc(bytes);
    assert(before);
    memcpy(before, s_frame, bytes);
    uint64_t flushed = s_flushed_pixels;
    motion_pointer(record, x - width / 4 - width / 100, y, true);
    motion_wait(record, record->step, "reveal-step", false);
    motion_frame(record, "reveal-step", false);
    uint64_t pixels = s_flushed_pixels - flushed;
    uint64_t viewport_pixels = (uint64_t)width * lv_area_get_height(&original);
    printf("reveal: small-step flushed=%llu viewport=%llu pixels\n",
           (unsigned long long)pixels, (unsigned long long)viewport_pixels);
    assert(pixels && pixels < viewport_pixels / 4 &&
           "a small reveal step must repaint a strip, not the full viewport");
    lv_area_t visible;
    assert(visible_click_area(incoming, &visible));
    int changed = 0;
    for (int yy = original.y1; yy <= original.y2; yy++)
        for (int xx = original.x1; xx <= original.x2; xx++) {
            size_t at = (size_t)yy * s_scr_w + xx;
            if (s_frame[at] != before[at]) {
                changed++;
                assert(xx >= visible.x1 && xx <= visible.x1 + width / 50 &&
                       "stationary content outside the advanced edge must not change");
            }
        }
    assert(changed);
    free(before);
    motion_pointer(record, x, y, true); /* reverse the same gesture */
    motion_wait(record, record->step, "reveal-reverse", false);
    motion_pointer(record, x, y, false);
    motion_wait(record, record->hold, "reveal-cancel", false);
    motion_frame(record, "reveal-cancel", true);
    assert(fnos_ui_page() == base);
    puts("checks: stationary native content, strip repaint, reverse/cancel PASS");
}

static void motion_probe_log(FILE *csv, motion_record_t *record, const char *name,
                             unsigned elapsed, lv_obj_t *outgoing, lv_obj_t *incoming)
{
    lv_point_t observed;
    lv_indev_get_point(record->pointer, &observed);
    fprintf(csv, "%s,%u,%d,%d,%d,%d,%d,%d\n", name, elapsed,
            (int)s_touch_point.x, (int)observed.x,
            lv_indev_get_state(record->pointer) == LV_INDEV_STATE_PRESSED,
            motion_visible_width(incoming),
            motion_min_content_opa(incoming), fnos_ui_page());
}

static bool motion_page_at_rest(lv_obj_t *page, lv_obj_t *other)
{
    return !lv_obj_has_flag(page, LV_OBJ_FLAG_HIDDEN) &&
           lv_obj_has_flag(other, LV_OBJ_FLAG_HIDDEN) &&
           lv_obj_get_style_translate_x(page, 0) == 0;
}

static void motion_response_probe(motion_record_t *record, int base, int target,
                                  const uint16_t *base_pixels, const uint16_t *target_pixels,
                                  size_t bytes)
{
    assert(target == (base + 1) % FNOS_UI_PAGE_COUNT &&
           "response probe requires the next public navigation page");
    unsigned response_budget = motion_ms("PREVIEW_MOTION_RESPONSE_MS", 32, true);
    unsigned complete_budget = motion_ms("PREVIEW_MOTION_COMPLETE_MS", 180, true);
    bool enforce = response_budget || complete_budget;
    char path[512];
    snprintf(path, sizeof path, "%s/response.csv", s_outdir);
    FILE *csv = fopen(path, "w");
    assert(csv);
    fputs("case,time_ms,requested_x,observed_x,pressed,incoming_visible_width,min_content_opa,page\n", csv);
    lv_obj_t *outgoing = motion_native_page(base), *incoming = motion_native_page(target);
    lv_obj_t *viewport = lv_obj_get_parent(outgoing);
    lv_area_t area;
    assert(visible_click_area(viewport, &area));
    int width = lv_obj_get_content_width(viewport);
    int x = area.x1 + lv_area_get_width(&area) * 3 / 4;
    int y = area.y1 + lv_area_get_height(&area) / 2;
    int64_t history_before = s_seq;
    fnos_sample_t sample_before = s_override_sample;

    fnos_ui_set_page(base);
    motion_wait(record, record->hold, "response-ready", false);
    fnos_ui_set_page(target);
    int first_move = -1, page_complete = -1, content_complete = -1;
    motion_frame(record, "response-api-start", false);
    if (enforce) assert(motion_min_content_opa(incoming) == LV_OPA_COVER &&
                        "navigation must not start by hiding incoming content");
    for (unsigned t = 1; t <= record->hold; t++) {
        vtick_advance(1); lv_timer_handler();
        motion_probe_log(csv, record, "api", t, outgoing, incoming);
        if (first_move < 0 && motion_visible_width(incoming) > 0) {
            first_move = (int)t;
            motion_frame(record, "response-api-first", false);
        }
        if (page_complete < 0 && motion_page_at_rest(incoming, outgoing)) page_complete = (int)t;
        if (content_complete < 0 && motion_min_content_opa(incoming) == LV_OPA_COVER)
            content_complete = (int)t;
    }
    int api_complete = LV_MAX(page_complete, content_complete);
    printf("response: api first=%dms page=%dms content=%dms complete=%dms\n",
           first_move, page_complete, content_complete, api_complete);
    if (response_budget) assert(first_move >= 0 && (unsigned)first_move <= response_budget);
    if (complete_budget)
        assert(page_complete >= 0 && content_complete >= 0 && (unsigned)api_complete <= complete_budget);
    motion_frame(record, "response-api-stable", true);
    assert(!memcmp(target_pixels, s_frame, bytes));

    /* These are observable interaction examples, independent of production lock,
       distance and velocity formulas: one-pixel jitter, a small horizontal step,
       a slow short drag, and a fast short drag. Dimensions use the real viewport. */
    struct { const char *name; int dx, dy; unsigned duration; bool step, follow, commit; } cases[] = {
        { "jitter", -1, 0, 48, true, false, false },
        { "small-horizontal", -LV_MAX(1, width / 50), LV_MAX(1, width / 500), 48, true, true, false },
        { "short-slow", -LV_MAX(1, width / 12), 0, 180, false, true, false },
        { "short-fast", -LV_MAX(1, width / 10), 0, 32, false, true, true },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        fnos_ui_set_page(base);
        motion_wait(record, record->hold, "response-reset", false);
        s_touch_point = (lv_point_t){ .x = x, .y = y };
        s_touch_down = true;
        unsigned press_wait = 0;
        while (lv_indev_get_state(record->pointer) != LV_INDEV_STATE_PRESSED && press_wait < record->hold) {
            vtick_advance(1); lv_timer_handler(); press_wait++;
        }
        assert(lv_indev_get_state(record->pointer) == LV_INDEV_STATE_PRESSED);
        lv_point_t observed;
        lv_indev_get_point(record->pointer, &observed);
        assert(observed.x == x && observed.y == y);
        int first_sample = -1, first_follow = -1, released = -1, complete = -1;
        bool moved = false;
        int wanted = cases[i].commit ? target : base;
        lv_obj_t *wanted_page = cases[i].commit ? incoming : outgoing;
        lv_obj_t *other_page = cases[i].commit ? outgoing : incoming;
        for (unsigned t = 0; t < cases[i].duration + record->hold; t++) {
            if (t < cases[i].duration) {
                unsigned fraction = cases[i].step ? cases[i].duration : t + 1;
                s_touch_point = (lv_point_t){
                    .x = x + cases[i].dx * (int)fraction / (int)cases[i].duration,
                    .y = y + cases[i].dy * (int)fraction / (int)cases[i].duration,
                };
            } else s_touch_down = false;
            vtick_advance(1); lv_timer_handler();
            motion_probe_log(csv, record, cases[i].name, t + 1, outgoing, incoming);
            lv_indev_get_point(record->pointer, &observed);
            if (first_sample < 0 && observed.x != x) first_sample = (int)t + 1;
            if (motion_visible_width(incoming) > 0) {
                moved = true;
                if (first_follow < 0) {
                    first_follow = (int)t + 1;
                    motion_frame(record, cases[i].name, false);
                }
            }
            if (t >= cases[i].duration &&
                lv_indev_get_state(record->pointer) == LV_INDEV_STATE_RELEASED) {
                if (released < 0) released = (int)(t + 1 - cases[i].duration);
                if (complete < 0 && fnos_ui_page() == wanted &&
                    motion_page_at_rest(wanted_page, other_page) &&
                    motion_min_content_opa(wanted_page) == LV_OPA_COVER)
                    complete = (int)(t + 1 - cases[i].duration);
            }
        }
        printf("response: %s press=%ums sample=%dms first=%dms sample_to_follow=%dms release=%dms complete=%dms page=%d expected=%d\n",
               cases[i].name, press_wait, first_sample, first_follow,
               first_follow < 0 ? -1 : first_follow - first_sample,
               released, complete, fnos_ui_page(), wanted);
        fflush(stdout);
        if (enforce) {
            assert(moved == cases[i].follow);
            assert(fnos_ui_page() == wanted);
            if (cases[i].step && cases[i].follow)
                assert(first_follow == first_sample &&
                       "a sampled horizontal drag must not wait for another polling tick");
            if (response_budget && cases[i].follow) {
                assert(first_sample >= 0 && first_follow >= 0);
                unsigned response = (unsigned)(first_follow - (cases[i].step || cases[i].commit ? 0 : first_sample));
                assert(response <= response_budget);
            }
            if (complete_budget) assert(complete >= 0 && (unsigned)complete <= complete_budget);
        }
        motion_frame(record, cases[i].name, true);
        if (enforce) assert(!memcmp(cases[i].commit ? target_pixels : base_pixels, s_frame, bytes));
        assert(!s_touch_down && lv_indev_get_state(record->pointer) == LV_INDEV_STATE_RELEASED);
        assert(s_seq == history_before && !memcmp(&sample_before, &s_override_sample, sizeof sample_before));
    }
    evidence_close(csv, "response.csv");
    fnos_ui_set_page(base);
    motion_wait(record, record->hold, "response-return", false);
    motion_frame(record, "response-return", true);
    assert(!memcmp(base_pixels, s_frame, bytes));
    puts(enforce ? "checks: native timed pointer response and completion budgets PASS" :
                   "checks: native timed pointer response measured (no acceptance budgets supplied)");
}

static void motion_repeat_target(motion_record_t *record, int base, int target,
                                 const uint16_t *expected, size_t bytes)
{
    /* First pause a real in-flight API transition with a real pointer drag.
       Re-selecting its reported target must not strand either page halfway. */
    fnos_ui_set_page(base);
    motion_wait(record, record->hold, "repeat-ready", false);
    fnos_ui_set_page(target);
    motion_wait(record, LV_MAX(1, UK_MOTION_MS / 4), "repeat-active", true);
    assert(fnos_ui_page() == target);
    int x = s_scr_w * 3 / 4, y = s_scr_h / 2;
    motion_pointer(record, x, y, true);
    motion_pointer(record, s_scr_w / 2, y, true);
    motion_wait(record, record->step, "repeat-paused", true);
    assert(s_touch_down);
    assert(memcmp(expected, s_frame, bytes) &&
           "repeat-target probe must actually pause an intermediate frame");
    fnos_ui_set_page(fnos_ui_page());    /* repeat the current public target */
    motion_frame(record, "repeat-selected", false);
    motion_pointer(record, s_scr_w / 2, y, false);
    lv_timer_handler();
    motion_frame(record, "repeat-release", false);
    motion_wait(record, record->hold, "repeat-recovery", true);
    motion_frame(record, "repeat-stable", true);
    assert(fnos_ui_page() == target && !s_touch_down);
    assert(!memcmp(expected, s_frame, bytes) &&
           "repeat-target release must reach the ordinary target pixels");
    puts("checks: paused transition, repeated current target, pointer release PASS");
}

/* Latest third target waits for the visible pair, without changing its pixels. */
static void motion_third_target(motion_record_t *record, int base, int target)
{
    if (record->swipe || UK_NUMBER_REDUCED || uk_motion_reduced()) return;
    fnos_ui_set_page(base);
    motion_wait(record, record->hold, "third-ready", false);
    fnos_ui_set_page(target);
    motion_wait(record, 32, "third-before", true);
    lv_obj_t *incoming = motion_native_page(target);
    int revealed = motion_visible_width(incoming);
    assert(revealed > 0 && revealed < s_scr_w);
    int third = (target + 2) % FNOS_UI_PAGE_COUNT;
    if (third == base) third = (third + 1) % FNOS_UI_PAGE_COUNT;
    fnos_ui_set_page(third);
    assert(fnos_ui_page() == target && "third target must not force the current pair to finish");
    assert(motion_visible_width(incoming) == revealed);
    motion_frame(record, "third-queued", false);
    int latest = (third + 1) % FNOS_UI_PAGE_COUNT;
    if (latest == base || latest == target) latest = third;
    fnos_ui_set_page(latest);
    motion_wait(record, record->hold, "third-continue", true);
    assert(fnos_ui_page() == latest);
    assert(!lv_obj_has_flag(motion_native_page(latest), LV_OBJ_FLAG_HIDDEN));
    motion_frame(record, "third-stable", true);
    fnos_ui_set_page(base);
    motion_wait(record, record->hold, "third-restore", false);
    /* A paused segment can reach its exact endpoint without an animation. */
    fnos_ui_set_page(target);
    motion_wait(record, 32, "third-drag-ready", false);
    fnos_ui_set_page(third);
    lv_area_t area;
    lv_obj_get_coords(motion_native_page(base), &area);
    int width = lv_area_get_width(&area);
    int x = area.x2 - 1, y = (area.y1 + area.y2) / 2;
    int travel = motion_visible_width(motion_native_page(target));
    motion_pointer(record, x, y, true);
    motion_pointer(record, x - (width - travel), y, true);
    motion_wait(record, 16, "third-drag-endpoint", false);
    motion_pointer(record, x - (width - travel), y, false);
    motion_wait(record, record->hold, "third-drag-continue", true);
    assert(fnos_ui_page() == third && "zero-distance finish must consume the pending target");
    fnos_ui_set_page(base);
    motion_wait(record, record->hold, "third-drag-restore", false);
    puts("checks: latest third target waits without forcing the visible pair PASS");
}

static void record_motion(lv_indev_t *pointer)
{
    const char *sequence = getenv("PREVIEW_MOTION");
    assert(sequence);
    size_t count = 1;
    for (const char *p = sequence; *p; p++) if (*p == ',') count++;
    if (count < 2 || count > 32) {
        fputs("PREVIEW_MOTION: expected 2..32 comma-separated page indexes\n", stderr);
        exit(1);
    }
    int *pages = calloc(count, sizeof *pages);
    assert(pages);
    const char *p = sequence;
    for (size_t i = 0; i < count; i++) {
        char *end = NULL;
        long value = strtol(p, &end, 10);
        if (end == p || value < 0 || value >= FNOS_UI_PAGE_COUNT ||
            (i + 1 < count ? *end != ',' : *end != '\0')) {
            fprintf(stderr, "PREVIEW_MOTION: invalid page sequence: %s\n", sequence);
            exit(1);
        }
        pages[i] = (int)value;
        p = end + (i + 1 < count ? 1 : 0);
    }
    size_t alternate = 1;
    while (alternate < count && pages[alternate] == pages[0]) alternate++;
    if (alternate == count) {
        fputs("PREVIEW_MOTION: include at least two distinct pages\n", stderr);
        exit(1);
    }

    const char *input = getenv("PREVIEW_MOTION_INPUT");
    if (input && strcmp(input, "swipe") && strcmp(input, "api")) {
        fputs("PREVIEW_MOTION_INPUT: expected swipe or api\n", stderr);
        exit(1);
    }
    motion_record_t record = {
        .pointer = pointer,
        .step = motion_ms("PREVIEW_MOTION_STEP_MS", 40, false),
        .hold = motion_ms("PREVIEW_MOTION_HOLD_MS", 800, false),
        .drag = motion_ms("PREVIEW_MOTION_DRAG_MS", 120, false),
        .rapid = motion_ms("PREVIEW_MOTION_RAPID_MS", 40, true),
        .warmup = motion_ms("PREVIEW_MOTION_WARMUP_MS", 3000, false),
        .swipe = !input || !strcmp(input, "swipe"),
    };
    bool first_entry = getenv("PREVIEW_MOTION_FIRST_ENTRY") &&
                       !strcmp(getenv("PREVIEW_MOTION_FIRST_ENTRY"), "1");
    if (first_entry && (record.swipe || count != 3 || pages[0] != pages[2])) {
        fputs("PREVIEW_MOTION_FIRST_ENTRY: use API input with base,target,base sequence\n", stderr);
        exit(1);
    }
    bool repeat_target = getenv("PREVIEW_MOTION_REPEAT_TARGET") &&
                         !strcmp(getenv("PREVIEW_MOTION_REPEAT_TARGET"), "1");
    bool response_probe = getenv("PREVIEW_MOTION_RESPONSIVENESS") &&
                          !strcmp(getenv("PREVIEW_MOTION_RESPONSIVENESS"), "1");
    char path[512];
    snprintf(path, sizeof path, "%s/motion.csv", s_outdir);
    record.timeline = fopen(path, "w");
    assert(record.timeline);
    fputs("file,time_ms,page,phase\n", record.timeline);
    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        int seen = 0;
        s_motion_pages[i] = motion_page_by_index(lv_screen_active(), i, &seen);
        assert(s_motion_pages[i]);
    }
    bool previous_no_settle = s_no_settle;
    s_no_settle = true;
    /* Exercise the real startup ordering: hidden pages can be laid out before
       the first NAS snapshot arrives. They must not reveal that old empty state. */
    static const fnos_status_t boot_empty;
    const fnos_status_t *online_fixture = s_override_status;
    if (first_entry) s_override_status = &boot_empty;
    fnos_ui_set_page(pages[0]);
    /* Pre-roll uses real timers; neither initial animation nor chart loading is
       completed by forcing object styles to their destination. */
    motion_wait(&record, record.warmup, "warmup", false);
    if (first_entry) {
        s_override_status = online_fixture;
        motion_wait(&record, record.warmup, "first-online", false);
    }
    record.start = vtick_get();
    motion_frame(&record, "ready", true);

    size_t bytes = (size_t)s_scr_w * s_scr_h * sizeof *s_frame;
    uint16_t *baseline = malloc(bytes), *terminal = malloc(bytes);
    uint16_t *alternate_pixels = (repeat_target || response_probe) ? malloc(bytes) : NULL;
    assert(baseline && terminal && (!(repeat_target || response_probe) || alternate_pixels));
    memcpy(baseline, s_frame, bytes);
    int64_t sequence_before = s_seq;
    if (response_probe) {
        motion_reveal_probe(&record, pages[0], pages[alternate]);
        assert(!memcmp(baseline, s_frame, bytes));
    }

    if (record.swipe) {
        /* A jitter on an action is a legitimate tap. Resolve a chart's current
           geometry instead of assuming that the old screen centre is inert. */
        lv_obj_t *page = motion_native_page(pages[0]);
        lv_obj_t *plot = find_widget(page, &lv_chart_class);
        lv_area_t inert;
        int x, y;
        if (plot && visible_click_area(plot, &inert)) {
            x = (inert.x1 + inert.x2) / 2; y = (inert.y1 + inert.y2) / 2;
        } else {
            lv_obj_get_coords(page, &inert); x = inert.x1 + 1; y = inert.y1 + 1;
        }
        /* A single-pixel finger jitter is a cancellation input, not a guessed
           production swipe threshold. The page must return to its original pixels. */
        motion_drag(&record, x, y, x - 1, y, "short-cancel");
        motion_wait(&record, record.hold, "short-cancel", true);
        motion_frame(&record, "cancel-stable", true);
        assert(fnos_ui_page() == pages[0]);
        assert(!memcmp(baseline, s_frame, bytes));
    }

    const char *scroll_probe = getenv("PREVIEW_MOTION_SCROLL");
    if (scroll_probe && !strcmp(scroll_probe, "1")) {
        /* Search the active content page: the shell/navigation may also report
           overflow, but dragging a navigation button tests a different action. */
        lv_obj_t *viewport = motion_vertical_viewport(motion_native_page(pages[0]));
        assert(viewport && "motion vertical probe needs a visible overflowing list");
        lv_area_t area;
        assert(visible_click_area(viewport, &area));
        int previous_y = lv_obj_get_scroll_y(viewport);
        int x = (area.x1 + area.x2) / 2;
        int height = lv_area_get_height(&area);
        motion_drag(&record, x, area.y1 + height * 3 / 4,
                    x, area.y1 + height / 4, "vertical-scroll");
        motion_wait(&record, record.hold, "vertical-scroll", true);
        assert(fnos_ui_page() == pages[0]);       /* vertical drag cannot navigate */
        assert(lv_obj_get_scroll_y(viewport) > previous_y);  /* real content moved */
        motion_frame(&record, "vertical-stable", true);
        lv_obj_scroll_to_y(viewport, previous_y, LV_ANIM_OFF);
        motion_wait(&record, record.hold, "vertical-restore", false);
        motion_frame(&record, "vertical-restored", true);
        assert(!memcmp(baseline, s_frame, bytes));
    }

    for (size_t i = 1; i < count; i++) {
        char phase[48];
        snprintf(phase, sizeof phase, "hop-%02u", (unsigned)i);
        motion_target(&record, pages[i], phase);
        lv_obj_t *entry_page = NULL;
        size_t entry_count = 0;
        motion_size_t *entry_sizes = NULL;
        if (first_entry && i == 1) {
            entry_page = motion_native_page(pages[i]);
            assert(entry_page && !effectively_hidden(entry_page));
            /* The first motion frame has already run native layout, but no UI
               timer tick has yet been advanced after the API navigation. */
            entry_count = motion_visible_nodes(entry_page);
            entry_sizes = calloc(entry_count, sizeof *entry_sizes);
            assert(entry_count && entry_sizes);
            size_t at = 0;
            motion_save_sizes(entry_page, entry_sizes, &at);
            assert(at == entry_count);
        }
        motion_wait(&record, record.hold, phase, true);
        motion_frame(&record, "stable", true);
        memcpy(terminal, s_frame, bytes);
        motion_wait(&record, record.step, phase, false);
        motion_frame(&record, "stable-check", true);
        assert(!memcmp(terminal, s_frame, bytes));
        if (entry_sizes) {
            size_t stable_count = motion_visible_nodes(entry_page);
            if (stable_count != entry_count)
                fprintf(stderr, "first entry: visible nodes %zu -> %zu\n", entry_count, stable_count);
            assert(stable_count == entry_count &&
                   "first API entry must already contain its settled visible content");
            motion_size_t *stable_sizes = calloc(entry_count, sizeof *stable_sizes);
            assert(stable_sizes);
            size_t at = 0;
            motion_save_sizes(entry_page, stable_sizes, &at);
            assert(at == entry_count);
            for (size_t j = 0; j < entry_count; j++) {
                if (memcmp(&entry_sizes[j], &stable_sizes[j], sizeof *entry_sizes))
                    fprintf(stderr, "first entry: node %zu size %dx%d -> %dx%d, children %u -> %u\n",
                            j, (int)entry_sizes[j].width, (int)entry_sizes[j].height,
                            (int)stable_sizes[j].width, (int)stable_sizes[j].height,
                            entry_sizes[j].children, stable_sizes[j].children);
            }
            assert(!memcmp(entry_sizes, stable_sizes, entry_count * sizeof *entry_sizes) &&
                   "first API entry must have its settled layout before a UI timer refresh");
            free(stable_sizes);
            free(entry_sizes);
            puts("checks: first API entry sizes already match settled page PASS");
        }
        if ((repeat_target || response_probe) && i == alternate) memcpy(alternate_pixels, s_frame, bytes);
        if (first_entry && i + 1 == count) {
            assert(fnos_ui_page() == pages[0]);
            assert(!memcmp(baseline, s_frame, bytes));
            puts("checks: first API entry, internal page bounds, return pixels PASS");
        }
    }
    if (response_probe)
        motion_response_probe(&record, pages[0], pages[alternate], baseline, alternate_pixels, bytes);
    if (repeat_target)
        motion_repeat_target(&record, pages[0], pages[alternate], alternate_pixels, bytes);

    motion_third_target(&record, pages[0], pages[alternate]);

    /* Finish an ordinary return, then interrupt an outgoing transition before
       it settles. Both directions use the same public/native input path. */
    fnos_ui_set_page(pages[0]);
    motion_wait(&record, record.hold, "rapid-ready", false);
    motion_target(&record, pages[alternate], "rapid-out");
    motion_wait(&record, record.rapid, "rapid-out", true);
    memcpy(terminal, s_frame, bytes);
    motion_target(&record, pages[0], "rapid-back");
    if (!record.swipe) {
        lv_area_t content;
        lv_obj_get_coords(motion_native_page(pages[0]), &content);
        for (int y = content.y1; y <= content.y2; y++)
            assert(!memcmp(terminal + y * s_scr_w + content.x1,
                           s_frame + y * s_scr_w + content.x1,
                           (size_t)lv_area_get_width(&content) * sizeof *s_frame) &&
                   "an API reversal must preserve the current revealed pixels");
        puts("checks: API reversal preserves current content pixels PASS");
    }
    if (response_probe) {
        int complete = -1;
        lv_obj_t *returned = motion_native_page(pages[0]);
        lv_obj_t *departed = motion_native_page(pages[alternate]);
        for (unsigned t = 1; t <= record.hold; t++) {
            vtick_advance(1); lv_timer_handler();
            if (t % record.step == 0) motion_frame(&record, "rapid-back", false);
            if (complete < 0 && fnos_ui_page() == pages[0] &&
                motion_page_at_rest(returned, departed) &&
                motion_min_content_opa(returned) == LV_OPA_COVER) complete = (int)t;
        }
        unsigned budget = motion_ms("PREVIEW_MOTION_COMPLETE_MS", 180, true);
        printf("response: rapid final redirect complete=%dms\n", complete);
        if (budget) assert(complete >= 0 && (unsigned)complete <= budget);
    } else motion_wait(&record, record.hold, "rapid-back", true);
    motion_frame(&record, "roundtrip-stable", true);
    assert(fnos_ui_page() == pages[0]);
    assert(!memcmp(baseline, s_frame, bytes));  /* no residual translation/opacity */
    assert(sequence_before == s_seq);          /* no invented history */
    assert(!s_touch_down);
    puts("checks: motion native input, stable terminal pixels, rapid round-trip, frozen history PASS");
    printf("motion: %u frames, step=%ums input=%s -> %s\n",
           record.frame, record.step, record.swipe ? "swipe" : "api", path);
    evidence_close(record.timeline, "motion.csv");
    free(alternate_pixels);
    free(terminal);
    free(baseline);
    free(pages);
    s_no_settle = previous_no_settle;
}

static lv_obj_t *hardware_contains(lv_obj_t *object, const char *text)
{
    if (lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_has_class(object,&lv_label_class) && strstr(lv_label_get_text(object),text)) return object;
    for (uint32_t i=0;i<lv_obj_get_child_count(object);i++) {
        lv_obj_t *found=hardware_contains(lv_obj_get_child(object,i),text);
        if (found) return found;
    }
    return NULL;
}

static void hardware_reachable(lv_obj_t *label)
{
    assert(label);
    lv_obj_scroll_to_view_recursive(label,LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_t *viewport=lv_obj_get_parent(label);
    while (viewport && !lv_obj_has_flag(viewport,LV_OBJ_FLAG_SCROLLABLE)) viewport=lv_obj_get_parent(viewport);
    assert(viewport);
    lv_area_t a,v; lv_obj_get_coords(label,&a); lv_obj_get_coords(viewport,&v);
    assert(a.x1>=v.x1 && a.x2<=v.x2);
    if (lv_area_get_height(&a)<=lv_area_get_height(&v))
        assert(a.y1>=v.y1 && a.y2<=v.y2);
}

static lv_obj_t *hardware_temperature_title(const fnos_status_t *fixture, int first, int end)
{
    const fnos_temp_t *t=&fixture->temps[first];
    const char *name=t->dn[0] ? t->dn : t->dev;
    int matches=0;
    for (int k=0;k<fixture->ntemps;k++) {
        const fnos_temp_t *other=&fixture->temps[k];
        const char *other_name=other->dn[0] ? other->dn : other->dev;
        if (!strcmp(other_name,name) &&
            (k==0 || strcmp(fixture->temps[k].dev,fixture->temps[k-1].dev))) matches++;
    }
    const char *suffix=matches>1 ? t->dev : (end-first==1 ? t->ch : "");
    size_t length=strlen(name)+strlen(suffix)+sizeof " · ";
    char *header=malloc(length); assert(header);
    snprintf(header,length,"%s%s%s",name,suffix[0] ? " · " : "",suffix);
    lv_obj_t *title=visible_text(lv_screen_active(),header);
    free(header);
    return title;
}

/* 硬件夹具的读取与解析**不能藏在 assert 里**（审计 M17）：Release 构建曾把
   `assert(fread(...))` / `assert(fnos_status_parse(...))` 整条预处理掉，
   于是"读文件"和"解析"这两件**有副作用的事**根本没发生，而 PASS 照打、退出码照 0，
   后面按 `fixture.nvols` 之类遍历自然全是 0 次 —— 一条永远为真的断言。
   现在：有副作用的动作用显式判断 + 响亮退出（rc 非零、写清哪个文件），
   无副作用的判据继续用 assert（构建已无条件 -UNDEBUG，见 CMakeLists.txt）。 */
static FILE *must_open_fixture(const char *path)
{
    FILE *f=fopen(path,"rb");
    if (!f) { fprintf(stderr,"FAIL: 打不开硬件夹具 %s: %s\n",path,strerror(errno)); exit(1); }
    return f;
}
static void must_parse_fixture(const char *json, fnos_status_t *out, const char *what)
{
    const char *reason=NULL;
    if (!fnos_status_parse(json,out,&reason)) {
        fprintf(stderr,"FAIL: 硬件夹具解析失败（%s）%s%s\n",what,reason?"：":"",reason?reason:"");
        exit(1);
    }
}

static void hardware_inventory(const char *path)
{
    FILE *file=must_open_fixture(path);
    fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
    if (size<=0) { fprintf(stderr,"FAIL: 硬件夹具是空的：%s\n",path); exit(1); }
    char *json=malloc((size_t)size+1);
    if (!json) { fprintf(stderr,"FAIL: 硬件夹具 %ld 字节，内存不足\n",size); exit(1); }
    size_t got=fread(json,1,(size_t)size,file); fclose(file); json[size]=0;
    if (got!=(size_t)size) {
        fprintf(stderr,"FAIL: 硬件夹具只读到 %zu/%ld 字节（文件被截断？）：%s\n",got,size,path);
        exit(1);
    }
    static fnos_status_t fixture;
    must_parse_fixture(json,&fixture,"主夹具"); free(json);
    fixture.ever_ok=fixture.online=true; fixture.last_status=200;
    fixture.recv_ms=esp_timer_get_time()/1000; fixture.ok_count=1;
    s_override_status=&fixture;
    wifi_stub(true,true,"PREVIEW-LAN",NULL);
    const char *failure=getenv("PREVIEW_ALLOC_FAIL_AT");
    if (failure) {
        if (getenv("PREVIEW_ALLOC_SEED_ONE")) {
            fnos_status_t seed={0};
            must_parse_fixture("{\"ready\":true,\"host\":\"seed\",\"vols\":[{\"mnt\":\"seed-volume\"}],\"raid\":[{\"dev\":\"seed-array\"}],\"disks\":[{\"dev\":\"seed-disk\"}],\"docker\":[{\"n\":\"seed-service\"}],\"alerts\":[{\"m\":\"seed-event\"}],\"temps\":[{\"dev\":\"seed-sensor\",\"dn\":\"seed-model\",\"ch\":\"seed-channel\",\"c\":40}],\"net\":{\"if\":\"seed-port\"}}",&seed,"seed 夹具");
            seed.ever_ok=seed.online=true;
            s_override_status=&seed;
            regression_tick();
            s_override_status=&fixture;
            fnos_status_release(&seed);
        }
        uk_test_alloc_fail_after(atoi(failure));
        regression_tick();
        bool fired=uk_test_alloc_was_triggered();
        if (fired) {
            assert(uk_alloc_failed());
            assert(visible_text(lv_screen_active(),"界面内存不足 · 部分设备尚未显示"));
        }
        uk_test_alloc_fail_after(-1);
        regression_tick();
        assert(!uk_alloc_failed());
        fnos_ui_set_page(1); fnos_ui_motion_settle(); regression_tick();
        for (int i=0;i<fixture.nvols;i++) assert(visible_text(lv_screen_active(),fixture.vols[i].mnt));
        for (int i=0;i<fixture.nraid;i++) assert(visible_text(lv_screen_active(),fixture.raid[i].dev));
        for (int i=0;i<fixture.ndisks;i++) assert(visible_text(lv_screen_active(),fixture.disks[i].dev));
        fnos_ui_set_page(2); fnos_ui_motion_settle(); regression_tick();
        for (int i=0;i<fixture.nnets;i++) assert(hardware_contains(lv_screen_active(),fixture.nets[i].ifname));
        /* 系统页的告警卡只列前三条（UI_ALERT_PANEL_ROWS）；九条都在告警页上。 */
        fnos_ui_set_page(5); fnos_ui_motion_settle(); regression_tick();
        for (int i=0;i<fixture.nalerts;i++) {
            char e[128];
            alert_expected_cn(fixture.alerts[i].m, e, sizeof e);
            assert(visible_text(lv_screen_active(),e));
        }
        fnos_ui_set_page(4); fnos_ui_motion_settle(); regression_tick();
        for (int first=0;first<fixture.ntemps;) {
            int end=first+1;
            while (end<fixture.ntemps && !strcmp(fixture.temps[first].dev,fixture.temps[end].dev)) end++;
            lv_obj_t *title=hardware_temperature_title(&fixture,first,end);
            assert(title);
            if (end-first>1) {
                hardware_reachable(title);
                uk_test_alloc_fail_after(atoi(failure));
                click_object_center(title); /* Includes expansion/channel realloc failures. */
                uk_test_alloc_fail_after(-1);
                regression_tick();
                for (int k=first;k<end;k++) assert(visible_text(lv_screen_active(),fixture.temps[k].ch));
            }
            first=end;
        }
        printf("checks: allocation failure %d fired=%d, retry and every inventory PASS\n",atoi(failure),fired);
        s_override_status = NULL;
        fnos_status_release(&fixture);
        return;
    }
    regression_tick();
    for (int page=0;page<FNOS_UI_PAGE_COUNT;page++) {
        fnos_ui_set_page(page); fnos_ui_motion_settle(); regression_tick();
        char name[64]; snprintf(name,sizeof name,"hardware-page-%d",page); snapshot(name);
        if (page==1) {
            for (int i=0;i<fixture.nvols;i++) hardware_reachable(visible_text(lv_screen_active(),fixture.vols[i].mnt));
            for (int i=0;i<fixture.nraid;i++) hardware_reachable(visible_text(lv_screen_active(),fixture.raid[i].dev));
            for (int i=0;i<fixture.ndisks;i++) hardware_reachable(visible_text(lv_screen_active(),fixture.disks[i].dev));
        } else if (page==2) {
            for (int i=0;i<fixture.nnets;i++) hardware_reachable(hardware_contains(lv_screen_active(),fixture.nets[i].ifname));
        } else if (page==3) {
            for (int i=0;i<fixture.ndocker;i++) hardware_reachable(visible_text(lv_screen_active(),fixture.docker[i].n));
        } else if (page==4 && fixture.ntemps) {
            int first=0;
            while (first<fixture.ntemps) {
                int end=first+1;
                while (end<fixture.ntemps && !strcmp(fixture.temps[end].dev,fixture.temps[first].dev)) end++;
                lv_obj_t *title=hardware_temperature_title(&fixture,first,end);
                assert(title); hardware_reachable(title);
                if (end-first>1) click_object_center(title);
                for (int k=first;k<end && end-first>1;k++)
                    hardware_reachable(visible_text(lv_screen_active(),fixture.temps[k].ch));
                if (end-first>1) { hardware_reachable(title); click_object_center(title); }
                first=end;
            }
        }
        snprintf(name,sizeof name,"hardware-last-page-%d",page); snapshot(name);
    }
    if (fixture.nnets) {
        int net_module=-1;
        for (int i=0;i<fixture.nmods;i++) if (!strcmp(fixture.mods[i].name,"net")) net_module=i;
        assert(net_module>=0);
        fnos_status_text(&fixture,&fixture.mods[net_module].status,"stale");
        fnos_ui_set_page(2); fnos_ui_motion_settle(); regression_tick();
        lv_obj_t *old_rate=hardware_contains(lv_screen_active(),"上次 · 下行");
        assert(old_rate && lv_color_eq(lv_obj_get_style_text_color(old_rate,0),uk_c(UK_T3)));
        snapshot("hardware-network-stale");
        fnos_status_text(&fixture,&fixture.mods[net_module].status,"denied");
        regression_tick();
        assert(hardware_contains(lv_screen_active(),"权限不足"));
        assert(!hardware_contains(lv_screen_active(),"上次 · 下行"));
        assert(visible_text(lv_screen_active(),"--"));
    }
    /* Shrink back to an empty frame: no removed device may return as an old tile. */
    char *old=fixture.nvols ? strdup(fixture.vols[fixture.nvols-1].mnt) : NULL;
    must_parse_fixture("{\"ready\":true,\"host\":\"empty\"}",&fixture,"空帧夹具");
    fixture.ever_ok=fixture.online=true;
    for (int page=0;page<FNOS_UI_PAGE_COUNT;page++) {
        fnos_ui_set_page(page); fnos_ui_motion_settle(); regression_tick();
        if (old) assert(!hardware_contains(lv_screen_active(),old));
    }
    free(old); fnos_status_release(&fixture); s_override_status=NULL;
    puts("PASS: all hardware identities and final entries reachable, channel expansion and inventory shrink");
}

static FILE *number_timeline(void)
{
    char path[512]; snprintf(path,sizeof path,"%s/motion.csv",s_outdir);
    FILE *f=fopen(path,"w"); if (!f) { perror(path); exit(1); }
    fputs("file,time_ms,page,phase\n",f); return f;
}
static void number_frame(FILE *timeline, const char *name, int page, const char *phase)
{
    snapshot(name);
    fprintf(timeline,"%s.ppm,%u,%d,%s\n",name,vtick_get(),page,phase);
}

/* Rapid real samples must not override the configured readable presentation beat. */
static void record_number_stream(FILE *timeline)
{
    lv_obj_t *value=uk_number_label_create(lv_screen_active());
    lv_obj_t *unit=lv_label_create(lv_screen_active());
    lv_obj_t *bar=lv_bar_create(lv_screen_active());
    lv_obj_t *arc=lv_arc_create(lv_screen_active());
    assert(value && unit && bar && arc);
    lv_bar_set_range(bar,0,100*UK_METER_SCALE);
    lv_arc_set_range(arc,0,100*UK_METER_SCALE);
    uk_meter_set(bar,50*UK_METER_SCALE,true);
    uk_meter_set(arc,50*UK_METER_SCALE,true);
    lv_obj_set_style_text_font(value,UK_FONT_NUM_44,0);
    lv_obj_set_style_text_color(value,uk_c(UK_T1),0);
    lv_obj_set_style_text_color(unit,uk_c(UK_T1),0);
    lv_obj_set_width(value,240);
    uk_number_set_reading(value,unit,"50","KB/s",false);
    lv_obj_update_layout(lv_screen_active());
    vtick_advance(UK_NUMBER_PRESENT_MS+1); lv_timer_handler();
    unsigned changes=0, min_gap=UINT_MAX, last_change=0;
    bool gated=!UK_NUMBER_REDUCED && UK_NUMBER_MS && UK_NUMBER_PRESENT_MS;
    unsigned visible_period=gated ? ((UK_NUMBER_PRESENT_MS+199)/200)*200 : 200;
    char latest[24]="50", previous[24]="50";
    s_no_settle=true;
    for (unsigned elapsed=0;elapsed<3000;elapsed+=25) {
        if (elapsed%200==0) {
            snprintf(latest,sizeof latest,"%u",10+(elapsed/200*37)%89);
            uk_number_refresh_begin();
            uk_number_set_reading(value,unit,latest,"KB/s",true);
            uk_meter_set(bar,atoi(latest)*UK_METER_SCALE,true);
            uk_meter_set(arc,atoi(latest)*UK_METER_SCALE,true);
            const char *shown=lv_label_get_text(value);
            assert(!strcmp(shown,elapsed%visible_period==0 ? latest : previous));
            if (strcmp(previous,shown)) {
                if (changes) min_gap=LV_MIN(min_gap,elapsed-last_change);
                changes++; last_change=elapsed;
                snprintf(previous,sizeof previous,"%s",shown);
            }
            assert(!strcmp(lv_label_get_text(unit),"KB/s"));
        }
        if (elapsed<=1200 && elapsed%100==0) {
            char name[64]; snprintf(name,sizeof name,"number-stream-%04u",elapsed);
            number_frame(timeline,name,0,"rapid-stream");
        }
        vtick_advance(25); lv_timer_handler();
    }
    assert(changes==(3000+visible_period-1)/visible_period);
    if (gated && changes>1) assert(min_gap>=UK_NUMBER_PRESENT_MS);
    printf("numeric stream: samples=15 changes=%u min_gap_ms=%u presentation_ms=%u\n",
           changes,min_gap,UK_NUMBER_PRESENT_MS);
    vtick_advance(UK_NUMBER_PRESENT_MS+1); lv_timer_handler();
    uk_number_refresh_begin();
    uk_number_set_reading(value,unit,latest,"KB/s",true);
    uk_meter_set(bar,atoi(latest)*UK_METER_SCALE,true);
    uk_meter_set(arc,atoi(latest)*UK_METER_SCALE,true);
    uk_meter_settle(bar); uk_meter_settle(arc);
    assert(lv_bar_get_value(bar)==atoi(latest)*UK_METER_SCALE);
    assert(lv_arc_get_value(arc)==atoi(latest)*UK_METER_SCALE);
    assert(!strcmp(lv_label_get_text(value),latest)); /* Latest target, no backlog replay. */
    uk_number_set_reading(value,unit,"--","",false);
    assert(!strcmp(lv_label_get_text(value),"--") && !*lv_label_get_text(unit));
    s_no_settle=false;
    lv_obj_delete(value); lv_obj_delete(unit);
    lv_obj_delete(bar); lv_obj_delete(arc);
}

/* Render the actual numeric widget with a deterministic LVGL clock. */
static void record_numbers(void)
{
    FILE *timeline=number_timeline();
    lv_obj_t *root=lv_screen_active();
    lv_obj_set_style_bg_color(root,uk_c(UK_BG),0);
    lv_obj_set_flex_flow(root,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root,UK_S4,0);
    uk_number_selfcheck(root);
    const char *initial[]={"65%","69%","99","43.9", "9.9 MB/s", "01:59"};
    const char *next[]={"66%","70%","100","44.0", "10.0 MB/s", "02:00"};
    lv_obj_t *labels[sizeof initial/sizeof initial[0]];
    for (unsigned i=0;i<sizeof labels/sizeof labels[0];i++) {
        labels[i]=uk_label(root,UK_FONT_NUM_44,UK_T1,initial[i]);
        lv_obj_remove_flag(labels[i],LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_update_layout(root);
    number_frame(timeline,"number-before",0,"before");
    for (unsigned i=0;i<sizeof labels/sizeof labels[0];i++) uk_number_set_text(labels[i],next[i],true);
    s_no_settle=true;
    for (unsigned elapsed=0;elapsed<=UK_NUMBER_MS+UK_NUMBER_MAX_STAGGER_MS+100;elapsed+=25) {
        char name[64]; snprintf(name,sizeof name,"number-%03u",elapsed);
        number_frame(timeline,name,0,"transition"); vtick_advance(25); lv_timer_handler();
    }
    s_no_settle=false;
    number_frame(timeline,"number-after",0,"after");
    for (unsigned i=0;i<sizeof labels/sizeof labels[0];i++) {
        assert(!strcmp(lv_label_get_text(labels[i]),next[i]));
        uk_number_set_text(labels[i],"--",false);
    }
    number_frame(timeline,"number-unavailable",0,"unavailable");
    for (unsigned i=0;i<sizeof labels/sizeof labels[0];i++) lv_obj_delete(labels[i]);
    record_number_stream(timeline);
    evidence_close(timeline, "numeric motion.csv");
    puts(UK_NUMBER_MS == 0 || UK_NUMBER_REDUCED ?
         "checks: in-place numeric replacements, stable origin, zero, missing and no glyph motion PASS" :
         "checks: damped telemetry, meter/glyph velocity continuity, carry, repeat, missing, allocation and deletion PASS");
}

/* Check the actual product rows, including separate units, across digit carry. */
static unsigned verify_numeric_unit_positions(lv_obj_t *root)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return 0;
    unsigned checked = 0;
    uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i + 1 < count; i++) {
        lv_obj_t *value = lv_obj_get_child(root, i), *unit = lv_obj_get_child(root, i + 1);
        if (!lv_obj_has_class(value, &lv_label_class) || !lv_obj_has_class(unit, &lv_label_class)) continue;
        const lv_font_t *font = lv_obj_get_style_text_font(value, 0);
        if (font != UK_FONT_NUM_20 && font != UK_FONT_NUM_32 && font != UK_FONT_NUM_44) continue;
        const char *u = lv_label_get_text(unit);
        if (strcmp(u, "%") && strcmp(u, "°C") && strcmp(u, "KB/s") && strcmp(u, "MB/s") &&
            strcmp(u, "GB/s") && strcmp(u, "GB") && strcmp(u, "TB") && strcmp(u, "ms")) continue;
        char *original = strdup(lv_label_get_text(value)); assert(original);
        lv_obj_update_layout(root);
        /* Ring units have their own centred line. Inline units share a baseline. */
        if (!lv_obj_has_class(lv_obj_get_parent(root), &lv_arc_class)) {
            lv_area_t v, ubox;
            lv_obj_get_content_coords(value, &v); lv_obj_get_content_coords(unit, &ubox);
            const lv_font_t *ufont = lv_obj_get_style_text_font(unit, 0);
            int baseline = v.y1 + font->line_height - font->base_line;
            int unit_baseline = ubox.y1 + ufont->line_height - ufont->base_line;
            printf("inline baseline: page=%d value=%s unit=%s delta=%d px\n",
                   fnos_ui_page(), original, u, unit_baseline-baseline);
            assert(unit_baseline == baseline);
        }
        lv_area_t origin, fixed; lv_obj_get_coords(value, &origin); lv_obj_get_coords(unit, &fixed);
        const char *targets[] = { "99", "100", "0", "--", "99.9", "100.0", "-9.9" };
        unsigned target_count = !strcmp(u, "%") ? 4 : sizeof targets / sizeof targets[0];
        for (unsigned j = 0; j < target_count; j++) {
            uk_number_set_text(value, targets[j], false);
            lv_obj_update_layout(root);
            lv_area_t v, now; lv_obj_get_coords(value, &v); lv_obj_get_coords(unit, &now);
            const bool centered = lv_obj_get_style_align(value,0)==LV_ALIGN_CENTER;
            const bool anchored = centered ? abs((v.x1+v.x2)-(origin.x1+origin.x2))<=1 :
                                  v.x1==origin.x1 || v.x2==origin.x2;
            if (!anchored || v.y1!=origin.y1)
                fprintf(stderr,"numeric anchor moved: page=%d original=%s target=%s unit=%s "
                    "[%d,%d,%d,%d] -> [%d,%d,%d,%d]\n",fnos_ui_page(),original,targets[j],u,
                    (int)origin.x1,(int)origin.y1,(int)origin.x2,(int)origin.y2,
                    (int)v.x1,(int)v.y1,(int)v.x2,(int)v.y2);
            assert(anchored && v.y1==origin.y1);
            if (now.x1 != fixed.x1 || now.y1 != fixed.y1)
                fprintf(stderr, "numeric unit moved: page=%d original=%s target=%s unit=%s [%d,%d] -> [%d,%d]\n",
                        fnos_ui_page(), original, targets[j], u, (int)fixed.x1, (int)fixed.y1, (int)now.x1, (int)now.y1);
            assert(now.x1 == fixed.x1 && now.y1 == fixed.y1);
        }
        uk_number_set_text(value, original, false); free(original);
        checked++;
    }
    for (uint32_t i = 0; i < count; i++) checked += verify_numeric_unit_positions(lv_obj_get_child(root, i));
    return checked;
}

static void record_data_numbers(void)
{
    FILE *timeline=number_timeline();
    unsigned unit_rows = 0;
    for (int page=0;page<FNOS_UI_PAGE_COUNT;page++) {
        fnos_ui_set_page(page);
        vtick_advance(500); lv_timer_handler();
        fnos_ui_motion_settle(); uk_anim_settle(lv_screen_active());
        unit_rows += verify_numeric_unit_positions(lv_screen_active());
        char name[64]; snprintf(name,sizeof name,"data-p%d-before",page); number_frame(timeline,name,page,"before");
        s_motion_fixture.cpu.pct=page%2 ? 37 : 63;
        s_motion_fixture.cpu.load1+=.12f;
        s_motion_fixture.mem.pct=page%2 ? 52 : 67;
        s_motion_fixture.uptime_s+=60;
        s_motion_fixture.net.rx_kbs+=1024;
        s_motion_fixture.net.tx_kbs+=512;
        for (int i=0;i<s_motion_fixture.ntemps;i++) s_motion_fixture.temps[i].c+=.2f;
        for (int i=0;i<s_motion_fixture.nvols;i++) {
            s_motion_fixture.vols[i].used_gb+=1;
            s_motion_fixture.vols[i].pct+=1;
        }
        /* 磁盘行（只有存储页有）此前完全没进夹具：它的读写速率是第二行读数，
           动效恰恰要靠这一跳才看得出来。 */
        for (int i=0;i<s_motion_fixture.ndisks;i++) {
            s_motion_fixture.disks[i].rd_kbs+=1024;
            s_motion_fixture.disks[i].wr_kbs+=512;
        }
        s_no_settle=true;
        vtick_advance(UK_NUMBER_PRESENT_MS + fnos_ui_refresh_period_ms()); lv_timer_handler();
        for (unsigned elapsed=0;elapsed<=2*UK_NUMBER_MS+UK_NUMBER_MAX_STAGGER_MS;elapsed+=25) {
            snprintf(name,sizeof name,"data-p%d-%03u",page,elapsed); number_frame(timeline,name,page,"transition");
            vtick_advance(25); lv_timer_handler();
        }
        s_no_settle=false;
        snprintf(name,sizeof name,"data-p%d-after",page); number_frame(timeline,name,page,"after");
    }
    assert(unit_rows >= 5);
    printf("checks: %u product numeric/unit rows keep position across integer/decimal carry, negative, zero and missing PASS\n", unit_rows);
    evidence_close(timeline, "numeric motion.csv");
    puts("checks: six-page numeric transition frames and geometry PASS");
}

/* Actual native frames with 200 ms input and independent 40 ms rendering. */
static void verify_live_batch_motion(void)
{
    lv_obj_t *title = visible_text_containing(lv_screen_active(), "网络吞吐");
    assert(title);
    lv_obj_t *chart = find_widget(lv_obj_get_parent(lv_obj_get_parent(title)), &lv_chart_class);
    assert(chart);
    lv_chart_series_t *series = uk_trend_series(chart, 0);
    uk_trend_settle(chart);
    uint32_t points = lv_chart_get_point_count(chart);
    uint32_t last = (lv_chart_get_x_start_point(chart, series) + points - 1) % points;
    lv_point_t before, immediate, moving;
    lv_chart_get_point_pos_by_id(chart, series, last, &before);
    s_override_sample.interval_ms = 200;
    s_seq += 2; /* A delayed UI consumes two real arrivals together. */
    vtick_advance(200); lv_timer_handler();
    lv_chart_get_point_pos_by_id(chart, series, last, &immediate);
    assert(abs(immediate.x - before.x) <= 1);
    vtick_advance(80); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, last, &moving);
    assert(moving.x < before.x);
    vtick_advance(400); lv_timer_handler();
    puts("checks: a coalesced live batch preserves continuous travel instead of snapping PASS");
}

static void verify_backfill_after_busy_read(void)
{
    lv_obj_t *title = visible_text_containing(lv_screen_active(), "网络吞吐");
    assert(title);
    lv_obj_t *chart = find_widget(lv_obj_get_parent(lv_obj_get_parent(title)), &lv_chart_class);
    assert(chart);
    lv_chart_series_t *series = uk_trend_series(chart, 0);
    s_override_sample.interval_ms = 200;
    s_seq += 66;
    vtick_advance(200); lv_timer_handler(); /* Full 64-point backfill batch. */
    uint32_t points = lv_chart_get_point_count(chart);
    uint32_t last = (lv_chart_get_x_start_point(chart, series) + points - 1) % points;
    lv_point_t before, immediate, later;
    lv_chart_get_point_pos_by_id(chart, series, last, &before);
    s_history_lock_busy = true;
    vtick_advance(200); lv_timer_handler(); /* Failed empty read must preserve syncing. */
    s_history_lock_busy = false;
    vtick_advance(200); lv_timer_handler(); /* Final two-point backfill tail. */
    lv_chart_get_point_pos_by_id(chart, series, last, &immediate);
    assert(immediate.x < before.x);
    vtick_advance(80); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, last, &later);
    assert(later.x == immediate.x);
    puts("checks: full backfill, failed empty read and partial tail settle without replay PASS");
}

static void record_streaming(void)
{
    FILE *timeline = number_timeline();
    s_override_sample_enabled = false;
    for (int i = 0; i < (CONFIG_FNOS_CHART_WINDOW + 63) / 64 + 1; i++) regression_tick();
    s_override_sample_enabled = true;
    const int pages[] = { 0, 2, 4 }; /* All product views containing live history. */
    unsigned units = 0;
    s_no_settle = true;
    for (unsigned p = 0; p < sizeof pages / sizeof pages[0]; p++) {
        fnos_ui_set_page(pages[p]);
        vtick_advance(200); lv_timer_handler(); fnos_ui_motion_settle();
        uk_anim_settle(lv_screen_active());
        if (pages[p] == 2) {
            verify_live_batch_motion();
            verify_backfill_after_busy_read();
        }
        units += verify_numeric_unit_positions(lv_screen_active());
        for (unsigned frame = 0; frame < 60; frame++) {
            if (frame % 5 == 0) {
                double t = (double)s_seq;
                s_override_sample = (fnos_sample_t){
                    .cpu = (float)(34 + 11 * sin(t * .31) + 5 * sin(t * 1.7)),
                    .mem = (float)(62 + 4 * sin(t * .11)),
                    .rx_kbs = (float)(9000 + 6500 * sin(t * .23) + 1800 * sin(t * 1.1)),
                    .tx_kbs = (float)(2600 + 1400 * sin(t * .19 + 1) + 700 * sin(t * 2.3)),
                    .temp_max = (float)(58 + 8 * sin(t * .08)), .interval_ms = 200,
                };
                s_motion_fixture.cpu.pct = s_override_sample.cpu;
                s_motion_fixture.mem.pct = s_override_sample.mem;
                s_motion_fixture.net.rx_kbs = s_override_sample.rx_kbs;
                s_motion_fixture.net.tx_kbs = s_override_sample.tx_kbs;
                if (s_motion_fixture.ntemps) s_motion_fixture.temps[0].c = s_override_sample.temp_max;
                s_seq++;
            }
            vtick_advance(40); lv_timer_handler();
            char name[64]; snprintf(name, sizeof name, "stream-p%d-%03u", pages[p], frame);
            number_frame(timeline, name, pages[p], "stream");
        }
    }
    s_no_settle = false;
    assert(units >= 5);
    evidence_close(timeline, "streaming motion.csv");
    puts("checks: all three trend views, independent input/render cadence and stable units PASS");
}

/* Dedicated production-LVGL coordinate check, before building the FNOS tree. */
static void verify_attached_cursors(void)
{
    lv_obj_t *chart=uk_trend_create(lv_screen_active(),8,2);
    lv_obj_set_size(chart,320,160);
    for (int i=0;i<8;i++) {
        uk_trend_push(chart,0,20,0);
        uk_trend_push(chart,1,70,0);
    }
    lv_obj_update_layout(chart);
    lv_refr_now(lv_display_get_default());
    lv_chart_cursor_t *cursor;
    unsigned count=0;
    LV_LL_READ(&((lv_chart_t *)chart)->cursor_ll,cursor) {
        lv_point_t point;
        uint32_t at=(lv_chart_get_x_start_point(chart,cursor->ser)+7)%8;
        lv_chart_get_point_pos_by_id(chart,cursor->ser,at,&point);
        assert(cursor->dir!=LV_DIR_NONE && cursor->pos_set);
        assert(cursor->pos.x==point.x && cursor->pos.y==point.y);
        assert(lv_color_eq(cursor->color,lv_chart_get_series_color(chart,cursor->ser)));
        count++;
    }
    assert(count==2);
    /* While the next point is offscreen, the cursor stays on the visible
       constant-height line, rather than jumping offscreen with that sample. */
    uk_trend_push(chart,0,20,200);
    uk_trend_push(chart,1,70,200);
    vtick_advance(50); lv_anim_refr_now();
    lv_refr_now(lv_display_get_default());
    LV_LL_READ(&((lv_chart_t *)chart)->cursor_ll,cursor) {
        assert(cursor->dir!=LV_DIR_NONE);
        assert(cursor->pos.x==lv_obj_get_content_width(chart));
        lv_point_t point;
        lv_chart_get_point_pos_by_id(chart,cursor->ser,
            (lv_chart_get_x_start_point(chart,cursor->ser)+7)%8,&point);
        assert(cursor->pos.y==point.y);
    }
    lv_obj_set_size(chart,240,120);
    lv_obj_update_layout(chart);
    lv_refr_now(lv_display_get_default());
    LV_LL_READ(&((lv_chart_t *)chart)->cursor_ll,cursor)
        assert(cursor->pos.x<=lv_obj_get_content_width(chart));
    uk_trend_push(chart,0,LV_CHART_POINT_NONE,0);
    lv_chart_hide_series(chart,uk_trend_series(chart,1),true);
    lv_refr_now(lv_display_get_default());
    LV_LL_READ(&((lv_chart_t *)chart)->cursor_ll,cursor)
        assert(cursor->dir==LV_DIR_NONE);
    uk_trend_clear(chart);
    lv_refr_now(lv_display_get_default());
    LV_LL_READ(&((lv_chart_t *)chart)->cursor_ll,cursor)
        assert(cursor->dir==LV_DIR_NONE);
    lv_obj_delete(chart);
    puts("checks: attached curve cursors track time, resize, gaps and hidden series PASS");
}

static unsigned s_trend_line_tasks;
static int32_t s_trend_gap_x = INT32_MIN;
static void count_trend_tasks(lv_event_t *e)
{
    lv_draw_line_dsc_t *line = lv_draw_task_get_line_dsc(lv_event_get_draw_task(e));
    if (!line || line->base.part != LV_PART_ITEMS) return;
    s_trend_line_tasks++;
    if (line->points && line->point_cnt) {
        lv_area_t bounds = {LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MIN, LV_COORD_MIN};
        for (uint32_t i = 0; i < line->point_cnt; i++) {
            bounds.x1 = LV_MIN(bounds.x1, line->points[i].x);
            bounds.x2 = LV_MAX(bounds.x2, line->points[i].x);
            bounds.y1 = LV_MIN(bounds.y1, line->points[i].y);
            bounds.y2 = LV_MAX(bounds.y2, line->points[i].y);
        }
        lv_area_increase(&bounds, line->width, line->width);
        lv_area_t area;
        lv_draw_task_get_area(lv_event_get_draw_task(e), &area);
        assert(area.x1 == bounds.x1 && area.y1 == bounds.y1 &&
               area.x2 == bounds.x2 && area.y2 == bounds.y2 &&
               "polyline bounds must add stroke padding once");
    }
    if (line->points) for (uint32_t i = 1; i < line->point_cnt; i++) {
        assert(line->points[i].x >= line->points[i-1].x);
        if (s_trend_gap_x != INT32_MIN)
            assert(!(line->points[i-1].x < s_trend_gap_x && line->points[i].x > s_trend_gap_x));
    }
}

static void verify_trend_draw_budget(void)
{
    lv_obj_t *chart = uk_trend_create(lv_screen_active(), 180, 2);
    lv_obj_set_size(chart, 960, 240);
    uk_trend_enable_flow(chart);
    for (int i = 0; i < 180; i++) {
        uk_trend_push(chart, 0, 50 + 30 * sinf(i * .12f), 0);
        uk_trend_push(chart, 1, 40 + 20 * cosf(i * .09f), 0);
    }
    lv_obj_update_layout(chart);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(chart, count_trend_tasks, LV_EVENT_DRAW_TASK_ADDED, NULL);
    s_trend_line_tasks = 0;
    int64_t started = esp_timer_get_time();
    lv_refr_now(lv_display_get_default());
    printf("trend draw: 180 points x 2, tasks=%u render_us=%lld\n",
           s_trend_line_tasks, (long long)(esp_timer_get_time() - started));
    /* Keep continuous histories within a small draw-task budget per frame. */
    assert(s_trend_line_tasks > 0 && s_trend_line_tasks <= 40);
    lv_chart_hide_series(chart, uk_trend_series(chart, 1), true);
    lv_chart_series_t *series = uk_trend_series(chart, 0);
    lv_point_t gap;
    lv_chart_get_point_pos_by_id(chart, series, 90, &gap);
    s_trend_gap_x = chart->coords.x1 + gap.x;
    lv_chart_set_value_by_id(chart, series, 90, LV_CHART_POINT_NONE);
    s_trend_line_tasks = 0;
    lv_refr_now(lv_display_get_default());
    assert(s_trend_line_tasks > 0 && s_trend_line_tasks <= 40);
    s_trend_gap_x = INT32_MIN;
    lv_obj_delete(chart);
}

#include "chart_range_regression.h"

static void verify_chart_mapping(void)
{
    verify_chart_range_continuity();
    verify_trend_runtime_reduced();
    verify_attached_cursors();
    verify_trend_draw_budget();
    lv_obj_t *chart = lv_chart_create(lv_screen_active());
    assert(chart);
    lv_obj_set_size(chart, 400, 400);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 3);
    lv_chart_series_t *series = lv_chart_add_series(chart, lv_color_hex(0x00ff00), LV_CHART_AXIS_PRIMARY_Y);
    assert(series);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 20000000);
    lv_chart_set_value_by_id(chart, series, 0, 0);
    lv_chart_set_value_by_id(chart, series, 1, 10000000);
    lv_chart_set_value_by_id(chart, series, 2, 20000000);
    lv_obj_update_layout(chart);
    lv_point_t point;
    lv_chart_get_point_pos_by_id(chart, series, 1, &point);
    assert(point.y == lv_obj_get_content_height(chart) / 2);
    lv_refr_now(lv_display_get_default());
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, INT32_MIN, INT32_MAX);
    lv_chart_set_value_by_id(chart, series, 0, INT32_MIN);
    lv_chart_set_value_by_id(chart, series, 1, 0);
    lv_chart_set_value_by_id(chart, series, 2, INT32_MAX - 1);
    lv_chart_get_point_pos_by_id(chart, series, 1, &point);
    int32_t h = lv_obj_get_content_height(chart);
    assert(point.y >= h / 2 && point.y <= h / 2 + 1);
    lv_refr_now(lv_display_get_default());
    lv_chart_set_type(chart, LV_CHART_TYPE_SCATTER);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, 2);
    lv_chart_set_series_value_by_id2(chart, series, 0, 0, INT32_MIN);
    lv_chart_set_series_value_by_id2(chart, series, 1, 1, 0);
    lv_chart_set_series_value_by_id2(chart, series, 2, 2, INT32_MAX - 1);
    lv_chart_get_point_pos_by_id(chart, series, 1, &point);
    assert(point.y >= h / 2 && point.y <= h / 2 + 1);
    lv_refr_now(lv_display_get_default());
    lv_obj_delete(chart);
    puts("checks: production LVGL line chart large/full-int32 coordinates and draw PASS");

    chart = uk_trend_create(lv_screen_active(), 32, 2);
    for (int i = 0; i < 32; i++) {
        uk_trend_push(chart, 0, i % 2 ? 80 : 20, 0);
        uk_trend_push(chart, 1, 40, 0);
    }
    series = uk_trend_series(chart, 0);
    uint32_t start = lv_chart_get_x_start_point(chart, series);
    int32_t *values = lv_chart_get_series_y_array(chart, series);
    int32_t latest = values[(start + 31) % 32];
    if (UK_TREND_SMOOTH_SAMPLES > 1) assert(latest >= 40 && latest <= 60);
    else assert(latest == 80);
    series = uk_trend_raw_series(chart, 0);
    values = lv_chart_get_series_y_array(chart, series);
    for (int i = 0; i < 32; i++) assert(values[i] == (i % 2 ? 80 : 20));
    series = uk_trend_series(chart, 1);
    values = lv_chart_get_series_y_array(chart, series);
    for (int i = 0; i < 32; i++) assert(values[i] == 40);
    uk_trend_push(chart, 0, LV_CHART_POINT_NONE, 0);
    uk_trend_push(chart, 0, 100, 0);
    series = uk_trend_series(chart, 0);
    start = lv_chart_get_x_start_point(chart, series);
    values = lv_chart_get_series_y_array(chart, series);
    assert(values[(start + 30) % 32] == LV_CHART_POINT_NONE);
    assert(values[(start + 31) % 32] == 100);
    lv_obj_delete(chart);
    puts("checks: smooth trends attenuate oscillation, retain constant values and reset at gaps PASS");

    chart = uk_trend_create(lv_screen_active(), 32, 1);
    lv_obj_set_size(chart, 400, 200);
    for (int i = 0; i < 32; i++) uk_trend_push(chart, 0, i, 0);
    lv_obj_update_layout(chart);
    series = uk_trend_series(chart, 0);
    lv_point_t before, immediate, quarter, halfway, end;
    lv_chart_get_point_pos_by_id(chart, series, 31, &before);
    vtick_advance(200);
    uk_trend_push(chart, 0, 32, 200);
    lv_chart_get_point_pos_by_id(chart, series, 31, &immediate);
    vtick_advance(50); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, 31, &quarter);
    vtick_advance(50); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, 31, &halfway);
    vtick_advance(100); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, 31, &end);
    /* An existing peak must travel continuously, rather than jump one column. */
    assert(abs(immediate.x - before.x) <= 1);
    assert(before.x > quarter.x && quarter.x > halfway.x && halfway.x > end.x);
    assert(abs((before.x - halfway.x) * 2 - (before.x - end.x)) <= 2);
    assert(lv_chart_get_series_y_array(chart, uk_trend_raw_series(chart, 0))[0] == 32);
    /* Early and late UI consumption must use the same producer velocity. */
    uk_trend_push(chart, 0, 33, 200);
    lv_chart_get_point_pos_by_id(chart, series, 31, &before);
    vtick_advance(50); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, 31, &quarter);
    vtick_advance(100); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, 31, &halfway);
    uk_trend_push(chart, 0, 34, 200); /* consumed after 150 ms */
    lv_chart_get_point_pos_by_id(chart, series, 31, &immediate);
    assert(abs(immediate.x - halfway.x) <= 1);
    vtick_advance(50); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, 31, &end);
    assert(abs((before.x - quarter.x) - (halfway.x - end.x)) <= 1);
    vtick_advance(250); lv_anim_refr_now();
    uk_trend_push(chart, 0, 35, 200); /* consumed late, duration remains source-based */
    lv_chart_get_point_pos_by_id(chart, series, 31, &before);
    vtick_advance(50); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, 31, &quarter);
    vtick_advance(150); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, 31, &end);
    assert(abs((before.x - quarter.x) * 4 - (before.x - end.x)) <= 3);
    uk_trend_clear(chart);
    values = lv_chart_get_series_y_array(chart, uk_trend_raw_series(chart, 0));
    for (int i = 0; i < 32; i++) assert(values[i] == LV_CHART_POINT_NONE);
    /* Long-running position rebasing must preserve raw values and the window. */
    for (int i = 0; i < 1000; i++) uk_trend_push(chart, 0, i, 0);
    start = lv_chart_get_x_start_point(chart, series);
    lv_chart_get_point_pos_by_id(chart, series, (start + 31) % 32, &point);
    assert(abs(point.x - lv_obj_get_content_width(chart)) <= 1);
    assert(values[(start + 31) % 32] == 999);
    vtick_advance(200); uk_trend_push(chart, 0, 1000, 200);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_HIDDEN);
    vtick_advance(100); lv_anim_refr_now();
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_HIDDEN);
    start = lv_chart_get_x_start_point(chart, series);
    lv_chart_get_point_pos_by_id(chart, series, (start + 31) % 32, &point);
    assert(abs(point.x - lv_obj_get_content_width(chart)) <= 1);
    /* Suppressed paints must not accumulate permanent time-axis lag. */
    uk_trend_clear(chart);
    for (int i = 0; i < 32; i++) uk_trend_push(chart, 0, i, 0);
    for (int i = 32; i < 52; i++) {
        vtick_advance(200);
        uk_trend_push(chart, 0, i, 200); /* no animation repaint between arrivals */
    }
    start = lv_chart_get_x_start_point(chart, series);
    lv_chart_get_point_pos_by_id(chart, series, (start + 31) % 32, &point);
    int32_t width = lv_obj_get_content_width(chart);
    assert(point.x >= width && point.x <= width + width / 30 + 2);
    vtick_advance(200); lv_anim_refr_now();
    lv_chart_get_point_pos_by_id(chart, series, (start + 31) % 32, &point);
    assert(abs(point.x - width) <= 1);
    lv_obj_delete(chart);
    puts("checks: streaming chart moves existing points continuously at constant speed PASS");
    /* A 15 ms scheduler must not halve a configured 60 FPS paint budget. */
    chart = uk_trend_create(lv_screen_active(), 32, 1);
    lv_obj_set_size(chart, 10000, 200);
    for (int i = 0; i < 32; i++) uk_trend_push(chart, 0, i, 0);
    lv_obj_update_layout(chart);
    series = uk_trend_series(chart, 0);
    uk_trend_push(chart, 0, 32, 2000);
    lv_chart_get_point_pos_by_id(chart, series, 31, &before);
    unsigned paints = 0;
    for (int i = 0; i < 66; i++) {
        vtick_advance(15); lv_anim_refr_now();
        lv_chart_get_point_pos_by_id(chart, series, 31, &point);
        if (point.x != before.x) paints++;
        before = point;
    }
    if (!UK_NUMBER_REDUCED && UK_TREND_FPS == 60 && UK_TREND_SCROLL_MS >= 2000)
        assert(paints >= 58 && paints <= 61);
    printf("checks: 15 ms streaming cadence: %u paints / 990 ms PASS\n", paints);
    lv_obj_delete(chart);
}

static unsigned s_refresh_size_changes, s_refresh_nav_styles, s_refresh_home_styles;

static void refresh_work_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SIZE_CHANGED) s_refresh_size_changes++;
    if (lv_event_get_code(e) == LV_EVENT_STYLE_CHANGED) {
        unsigned *count = lv_event_get_user_data(e);
        if (count) (*count)++;
        else s_refresh_nav_styles++;
    }
}

static void verify_inventory_rename(const char **field, const char *sample)
{
    const char *original = *field;
    lv_obj_t *label = visible_text(lv_screen_active(), original);
    assert(label);
    int32_t original_height = lv_obj_get_height(label);
    char expanded[CONFIG_FNOS_UI_TEXT_SCRATCH_BYTES * 2 + 1];
    const char words[] = "Long device name ";
    for (size_t i = 0; i < sizeof expanded - 1; i++) expanded[i] = words[i % (sizeof words - 1)];
    expanded[sizeof expanded - 1] = 0;
    bool renamed = fnos_status_text(&s_motion_fixture, field, "%s", expanded);
    assert(renamed);
    uk_test_alloc_fail_after(0);
    regression_tick();
    assert(uk_test_alloc_was_triggered() && uk_alloc_failed());
    assert(visible_text(lv_screen_active(), original));
    assert(visible_text(lv_screen_active(), "界面内存不足 · 部分设备尚未显示"));
    uk_test_alloc_fail_after(-1);
    regression_tick();
    label = visible_text(lv_screen_active(), expanded);
    /* 列表名现在是一行裁切（CLIP），长名不再撑高行；行高必须保持不变，
       完整名由实体详情承载。 */
    assert(label && lv_obj_get_height(label) == original_height);
    hardware_reachable(label);
    snapshot(sample);
    bool restored = fnos_status_text(&s_motion_fixture, field, "%s", original);
    assert(restored);
    regression_tick();
    label = visible_text(lv_screen_active(), original);
    assert(label && lv_obj_get_height(label) == original_height);
}

static void assert_ring_reading_center(lv_obj_t *gauge, lv_obj_t *value)
{
    lv_area_t ring, label, ink = {SCR_MAX_W, SCR_MAX_H, -1, -1};
    lv_obj_get_coords(gauge, &ring);
    lv_obj_get_content_coords(value, &label);
    uint16_t color = lv_color_to_u16(lv_obj_get_style_text_color(value, 0));
    for (int y = LV_MAX(0, label.y1); y <= LV_MIN(s_scr_h-1, label.y2); y++)
        for (int x = LV_MAX(0, label.x1); x <= LV_MIN(s_scr_w-1, label.x2); x++) {
            if (s_frame[y*s_scr_w+x] != color) continue;
            ink.x1 = LV_MIN(ink.x1, x); ink.x2 = LV_MAX(ink.x2, x);
            ink.y1 = LV_MIN(ink.y1, y); ink.y2 = LV_MAX(ink.y2, y);
        }
    assert(ink.x2 >= ink.x1 && ink.y2 >= ink.y1);
    int dx2 = ink.x1 + ink.x2 - ring.x1 - ring.x2;
    int dy2 = ink.y1 + ink.y2 - ring.y1 - ring.y2;
    printf("ring ink center: value=%s dx=%.1f dy=%.1f px\n",
           lv_label_get_text(value), dx2/2.f, dy2/2.f);
    assert(abs(dx2) <= 2 && abs(dy2) <= 2);
}

static void verify_overview_number_masks(void)
{
    const fnos_status_t *previous = s_override_status;
    int page = fnos_ui_page();
    fnos_status_t fixture = {0};
    fill_live(&fixture);
    fixture.nmods = 0;
    fixture.cpu.pct = fixture.mem.pct = 100;
    s_override_status = &fixture;
    fnos_ui_set_page(0);
    for (int i = 0; i < 4; i++) regression_tick();
    const char *titles[] = { "CPU", "内存" };
    lv_obj_t *labels[2];
    for (int i = 0; i < 2; i++) {
        labels[i] = visible_text(overview_object(titles[i], &lv_obj_class), "100");
        assert(labels[i]);
    }
    const int percentages[] = {0, 9, 37, 99, 100};
    for (unsigned p = 0; p < sizeof percentages / sizeof percentages[0]; p++) {
        fixture.cpu.pct = fixture.mem.pct = percentages[p];
        for (int settle = 0; settle < 4; settle++) regression_tick();
        lv_refr_now(lv_display_get_default());
        for (int i = 0; i < 2; i++)
            assert_ring_reading_center(overview_object(titles[i], &lv_arc_class), labels[i]);
        char name[48]; snprintf(name, sizeof name, "alignment-ring-%d", percentages[p]);
        snapshot(name);
    }
    /* Keep the real numeric publication gate active between samples. A new
       one-digit target must not crop a still-presented three-digit reading. */
    for (int step = 0; step < 12; step++) {
        fixture.cpu.pct = fixture.mem.pct = step % 2 ? 100 : 9;
        vtick_advance(fnos_ui_refresh_period_ms());
        lv_timer_handler();
        lv_obj_update_layout(lv_screen_active());
        for (int i = 0; i < 2; i++) {
            const lv_font_t *font = lv_obj_get_style_text_font(labels[i], 0);
            lv_point_t size;
            lv_text_get_size(&size, lv_label_get_text(labels[i]), font, 0, 0,
                             LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > lv_obj_get_content_width(labels[i]))
                fprintf(stderr, "reading mask: text=%s needs=%d available=%d\n",
                        lv_label_get_text(labels[i]), (int)size.x,
                        (int)lv_obj_get_content_width(labels[i]));
            assert(size.x <= lv_obj_get_content_width(labels[i]));
            assert(lv_obj_get_height(labels[i]) == lv_font_get_line_height(font));
        }
    }
    fixture.uptime_s = UINT32_MAX;
    for (int i = 0; i < 4; i++) regression_tick();
    snapshot("17-overview-long-uptime");
    lv_obj_t *status = overview_object("设备状态", &lv_obj_class);
    assert(visible_text(status, "运行 49710 天") && visible_text(status, "06:28"));
    fixture.nmods = 1;
    fnos_status_text(&fixture, &fixture.mods[0].name, "cpu");
    fnos_status_text(&fixture, &fixture.mods[0].status, "stale");
    regression_tick();
    assert(visible_text(status, "运行 49710 天 · 旧值") && visible_text(status, "06:28"));
    snapshot("17-overview-old-uptime");
    fnos_status_text(&fixture, &fixture.mods[0].status, "ok");
    fixture.uptime_s = 86340;
    for (int i = 0; i < 4; i++) regression_tick();
    assert(visible_text(status, "运行时长") && visible_text(status, "23:59"));
    fixture.uptime_s = 86400;
    vtick_advance(fnos_ui_refresh_period_ms());
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
    assert(visible_text(status, "运行 1 天") && visible_text(status, "00:00"));
    s_override_status = previous;
    fnos_ui_set_page(page);
    regression_tick();
    fnos_status_release(&fixture);
    puts("checks: gated one/three-digit overview readings remain complete PASS");
}

static void verify_refresh_work(void)
{
    lv_display_t *display = lv_display_get_default();
    /* A page prepared on the current numeric beat must show the new snapshot
       immediately, even when the global presentation batch was already used. */
    assert(s_motion_fixture.nvols > 0);
    fnos_ui_set_page(1); fnos_ui_motion_settle(); regression_tick();
    float original_pct = s_motion_fixture.vols[0].pct;
    fnos_ui_set_page(0); fnos_ui_motion_settle(); regression_tick();
    s_motion_fixture.vols[0].pct = 17;
    regression_tick();
    fnos_ui_set_page(1); fnos_ui_motion_settle();
    assert(visible_text(lv_screen_active(), "17%"));
    s_motion_fixture.vols[0].pct = original_pct;
    fnos_ui_set_page(0); fnos_ui_motion_settle(); regression_tick();
    lv_obj_t *nav_label = visible_text(lv_screen_active(), "总览");
    assert(nav_label);
    lv_obj_t *nav = lv_obj_get_parent(nav_label);
    lv_obj_add_event_cb(nav, refresh_work_event, LV_EVENT_STYLE_CHANGED, NULL);
    for (int page = 0; page < FNOS_UI_PAGE_COUNT; page++) {
        fnos_ui_set_page(page);
        fnos_ui_motion_settle();
        for (int settle = 0; settle < 4; settle++) regression_tick();
        lv_refr_now(display);
        lv_obj_t *inventory = NULL;
        if (page == 1) {
            lv_obj_t *title = visible_text(lv_screen_active(), "存储卷");
            assert(title);
            inventory = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(title)));
            lv_obj_add_event_cb(inventory, refresh_work_event, LV_EVENT_SIZE_CHANGED, NULL);
        }
        unsigned network_styles = 0;
        lv_obj_t *network_row = NULL;
        if (page == 2) {
            lv_obj_t *card = overview_object("网口与接口", &lv_obj_class);
            lv_obj_t *pool = lv_obj_get_child(uk_card_body(card), 0);
            assert(lv_obj_get_child_count(pool) > 0);
            network_row = lv_obj_get_child(pool, 0);
            lv_obj_add_event_cb(network_row, refresh_work_event, LV_EVENT_STYLE_CHANGED, &network_styles);
        }
        lv_obj_t *home_card = page == 0 ? overview_object("CPU", &lv_obj_class) : NULL;
        if (home_card) lv_obj_add_event_cb(home_card, refresh_work_event, LV_EVENT_STYLE_CHANGED, &s_refresh_home_styles);
        s_refresh_size_changes = s_refresh_nav_styles = s_refresh_home_styles = 0;
        s_flushed_pixels = 0;
        for (int sample = 0; sample < 4; sample++) {
            vtick_advance(500);
            lv_timer_handler();
        }
        lv_refr_now(display);
        printf("refresh work: page=%d ticks=4 inventory_size_changes=%u nav_style_changes=%u home_style_changes=%u flushed_pixels=%llu\n",
               page, s_refresh_size_changes, s_refresh_nav_styles, s_refresh_home_styles,
               (unsigned long long)s_flushed_pixels);
        /* A stable snapshot must not restyle unrelated navigation on each tick. */
        assert(s_refresh_nav_styles == 0);
        assert(s_refresh_size_changes == 0);
        assert(s_refresh_home_styles == 0);
        if (network_row) {
            printf("refresh work: unchanged network row style changes=%u\n", network_styles);
            assert(network_styles == 0 && "stable network telemetry must not reset row geometry");
            lv_obj_remove_event_cb(network_row, refresh_work_event);
        }
        /* Four unchanged home ticks must paint less than one entire frame.
           Full render mode deliberately flushes all pixels and is excluded. */
        if (page == 0 && getenv("PREVIEW_PARTIAL"))
            assert(s_flushed_pixels < (uint64_t)s_scr_w * s_scr_h);
        if (home_card) lv_obj_remove_event_cb(home_card, refresh_work_event);
        if (inventory) lv_obj_remove_event_cb(inventory, refresh_work_event);
        s_refresh_nav_styles = 0;
        fnos_ui_set_page(page);
        assert(s_refresh_nav_styles == 0);
    }
    lv_obj_remove_event_cb(nav, refresh_work_event);
    fnos_ui_set_page(1); fnos_ui_motion_settle(); regression_tick();
    assert(s_motion_fixture.nvols > 0);
    verify_inventory_rename(&s_motion_fixture.vols[0].mnt, "refresh-long-volume");
    fnos_ui_set_page(3); fnos_ui_motion_settle(); regression_tick();
    assert(s_motion_fixture.ndocker > 0);
    verify_inventory_rename(&s_motion_fixture.docker[0].n, "refresh-long-container");
    fnos_ui_set_page(1); fnos_ui_motion_settle(); regression_tick();
    lv_obj_t *volume_title = visible_text(lv_screen_active(), "存储卷");
    assert(volume_title);
    lv_obj_t *volume_card = lv_obj_get_parent(lv_obj_get_parent(volume_title));
    int32_t original_width = lv_obj_get_width(volume_card);
    /* Exercise section reflow on the actual landscape board. */
    pointer_text("卷");
    regression_tick();
    assert(lv_obj_get_width(volume_card) != original_width);
    snapshot("refresh-filtered-inventory");
    pointer_text("全部");
    regression_tick();
    assert(lv_obj_get_width(volume_card) == original_width);
    verify_overview_number_masks();
    puts("checks: stable refresh, repeated current-page navigation, same-count long-name reflow, filtered inventory reflow and untruncated formatting PASS");
}

#include "overlay_regression.h"

int main(int argc, char **argv)
{
    if (argc > 1) s_outdir = argv[1];
    if (mkdir(s_outdir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "FAIL: cannot create preview output directory\n");
        return 1;
    }
    struct stat out_stat;
    if (stat(s_outdir, &out_stat) != 0 || !S_ISDIR(out_stat.st_mode)) {
        fprintf(stderr, "FAIL: preview output is not a directory\n");
        return 1;
    }
    setvbuf(stdout, NULL, _IONBF, 0);   /* 重定向到文件时 stdout 是块缓冲的：不退出就看不到任何日志 */
    setvbuf(stderr, NULL, _IONBF, 0);

    lv_init();
    lv_tick_set_cb(vtick_get);
    lv_delay_set_cb(vdelay);
    /* PREVIEW_SIZE=WxH：分辨率自适应取证（默认跟板子一致 1024×600） */
    const char *sz = getenv("PREVIEW_SIZE");
    if (sz) {
        char *mid = NULL, *end = NULL;
        errno = 0;
        long w = strtol(sz, &mid, 10);
        if (errno || mid == sz || *mid != 'x') {
            fputs("FAIL: invalid PREVIEW_SIZE\n", stderr); return 2;
        }
        long h = strtol(mid + 1, &end, 10);
        if (errno || end == mid + 1 || *end || w <= 0 || h <= 0 ||
            w > SCR_MAX_W || h > SCR_MAX_H) {
            fputs("FAIL: PREVIEW_SIZE outside buffer or malformed\n", stderr); return 2;
        }
        s_scr_w = (int)w; s_scr_h = (int)h;
    }
    lv_display_t *d = lv_display_create(s_scr_w, s_scr_h);
    _Alignas(LV_DRAW_BUF_ALIGN) static uint16_t buf[SCR_MAX_W * SCR_MAX_H];
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    if (getenv("PREVIEW_PARTIAL"))
        lv_display_set_buffers(d,buf,NULL,(size_t)s_scr_w*50*sizeof(uint16_t),LV_DISPLAY_RENDER_MODE_PARTIAL);
    else lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);

    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read);
    lv_indev_set_display(touch, d);

    /* 预览用 KK_PALETTE=<名字> 选配色；不设时跟随固件的 CONFIG_FNOS_PALETTE，
       避免默认审计渲染的是一套非出货配色。必须在 fnos_ui_create() 之前 ——
       颜色在建构件时就被拷进样式里了。 */
#ifndef CONFIG_FNOS_PALETTE
#define CONFIG_FNOS_PALETTE "cuktech-console"
#endif
    const char *palette = getenv("KK_PALETTE");
    fnos_ui_theme_use(palette && palette[0] ? palette : CONFIG_FNOS_PALETTE);
    fprintf(stderr, "palette: %s\n", fnos_ui_theme_name());

    if (getenv("PREVIEW_CHART_MAP")) { verify_chart_mapping(); return preview_finish(); }
    motion_prepare();
    /* First NAS pairing on an already configured network, without relying on
       verify_transitions() to set up the Wi-Fi fixture first. */
    if (getenv("PREVIEW_PAIR_FIRST_ENTRY") || getenv("PREVIEW_HARDWARE_FIXTURE"))
        wifi_stub(true, true, "PREVIEW-LAN", NULL);
    /* PREVIEW_BARMAP=<file>：逐帧写（见 snapshot()）。渲染时开一次，审计就按帧取依据；
       打不开就是硬失败 —— 静默少了逐帧表，审计会退回"启动态几何审全部帧"的老毛病。 */
    {   const char *bp = getenv("PREVIEW_BARMAP");
        if (bp && *bp) {
            s_barmap = fopen(bp, "w");
            if (!s_barmap) { fprintf(stderr, "FAIL: 打不开逐帧滚动条表 %s\n", bp); return 1; }
        }
    }

    if (getenv("PREVIEW_NUMBERS")) { record_numbers(); return preview_finish(); }
    fnos_ui_create();
    if (getenv("PREVIEW_REFRESH_WORK")) { verify_refresh_work(); return preview_finish(); }
    if (getenv("PREVIEW_DATA_MOTION")) {
        if (!strcmp(getenv("PREVIEW_DATA_MOTION"), "temperature")) verify_temperature_colors();
        else if (!strcmp(getenv("PREVIEW_DATA_MOTION"), "range")) verify_product_range_motion();
        else if (!strcmp(getenv("PREVIEW_DATA_MOTION"), "capacity")) verify_usage_colors();
        else if (!strcmp(getenv("PREVIEW_DATA_MOTION"), "boot-wifi")) verify_boot_wifi_motion();
        else if (!strcmp(getenv("PREVIEW_DATA_MOTION"), "overlays")) verify_overlay_motion(touch);
        else if (!strcmp(getenv("PREVIEW_DATA_MOTION"), "reduced")) verify_runtime_reduced_motion(touch);
        else if (!strcmp(getenv("PREVIEW_DATA_MOTION"), "stream")) record_streaming();
        else record_data_numbers();
        return preview_finish();
    }

    /* 卡片几何的"唯一来源"：把已建好的卡片按真实 coords 吐出来，
       看门狗（scroll_gap.js）读它。手抄一张卡片表 → 抄错一次就报 15 处假不合格。 */
    if (getenv("PREVIEW_CARDMAP")) {
        fnos_ui_dump_card_map();
        return preview_finish();
    }
    /* PREVIEW_CARDMAP_AT=<n>：在第 n 次 snapshot 之前再吐一遍卡片几何，
       用来核对"建树那一刻"与"跑完状态之后"的 coords 是否一致 —— 曾经差过 76px，
       看门狗拿到的 y 因此是错的。 */
    s_cardmap_at = getenv("PREVIEW_CARDMAP_AT") ? atoi(getenv("PREVIEW_CARDMAP_AT")) : -1;

    if (getenv("PREVIEW_MOTION")) {
        record_motion(touch);
        return preview_finish();
    }
    if (getenv("PREVIEW_PAIR_FIRST_ENTRY")) {
        verify_pairing();
        verify_pair_messages();
        puts("checks: first pairing entry and all pairing stages PASS");
        return preview_finish();
    }

    if (getenv("PREVIEW_HARDWARE_FIXTURE")) {
        hardware_inventory(getenv("PREVIEW_HARDWARE_FIXTURE"));
        return preview_finish();
    }

    /* A static state matrix must not race the real three-second provisioning
       deadline while writing images. Wi-Fi flows below advance their own
       fixture explicitly; keep board time stable just as in motion recordings. */
    s_motion_clock_us = esp_timer_get_time();
    s_motion_clock_frozen = true;
    snapshot("00-startup");
    run_state(ST_WARMING, 1);
    run_state(ST_LIVE, 2);
    run_state(ST_OFFLINE, 3);
    run_state(ST_HEALTHY, 4);
    verify_container_order();
    run_state(ST_LIMITS, 5);
    verify_container_order();

    verify_transitions();
    verify_navigation_hit_targets();
    verify_enhanced_metrics();
    verify_unifi_views();
    verify_detail_freshness();
    verify_diagnostic_freshness();
    verify_alert_text_preservation();
    verify_reasons();
    verify_pairing();
    verify_pair_messages();
    verify_wifi();
    verify_ui_regressions();
    verify_docker_module_states();
    verify_temperature_protocol_limit();
    verify_overview_charts();
    verify_overview_number_masks();
    verify_usage_colors();
    verify_temperature_colors();
    verify_module_freshness();

    /* ── 夜间模式：一整套独立配色，预览从来没渲染过 ──
       run_state 会把四页都拍一遍，所以这里先量白天的对比度、再开夜间量一次。 */
    report_contrast("白天");
    fnos_ui_set_night(true);
    vtick_advance(600);
    lv_timer_handler();
    audit_labels(lv_screen_active());
    audit_format_residue(lv_screen_active(), "night");
    audit_bounds(lv_screen_active());
    report_contrast("夜间");
    /* 夜间那张样张：四页各一张，配色问题至少要有一张图能看 */
    for (int p = 0; p < FNOS_UI_PAGE_COUNT; p++) {
        fnos_ui_set_page(p);
        vtick_advance(300);
        lv_timer_handler();
        char name[64];
        snprintf(name, sizeof name, "08-night-p%d", p);
        snapshot(name);
    }
    fnos_ui_set_night(false);
    vtick_advance(600);
    lv_timer_handler();

    /* 老采集端那一屏（面板上现在跑的就是它）：只发 n、没有 ch/dn。
       放在这里是为了让"回退长什么样"成为**可回归的图**，而不是只靠设备照片。 */
    run_state(ST_LEGACY, 9);

    /* 放在断言之后：verify_transitions 依赖 LIMITS 那一屏（卷数拉满）的布局，
       换状态会让它读到别的屏。 */
    run_state(ST_OFFLINE_TLS, 6);

    /* 最后一步：所有页面与面板都走过之后，反查"一次都没露过面"的对象。
       隐藏子树在 audit_labels 里被整个跳过，所以**默认 HIDDEN 又没有任何一步
       把它打开的构件，四条版面审计一次都没看过它** —— 而它在真机上照样能画错。
       （配对卡与诊断面板就是这么被漏掉过一次的，只是后来恰好补上了打开它们的步骤。） */
    audit_never_visible(lv_screen_active());
    if (s_never_visible > NEVER_VISIBLE_BASELINE) {
        fprintf(stderr, "有 %d 个构件在整个预览流程里从未可见（基线 %d）—— "
                        "新增的那些没有被任何审计看过\n",
                s_never_visible, NEVER_VISIBLE_BASELINE);
        abort();
    }
    printf("preview: 见过的构件 %d 个；从未露面的 %d 个（基线 %d，都是行池里用不到的那些）\n",
           s_seen_n, s_never_visible, NEVER_VISIBLE_BASELINE);

    printf("preview: %d page(s)/state → %s\n", FNOS_UI_PAGE_COUNT, s_outdir);
    return preview_finish();
}
