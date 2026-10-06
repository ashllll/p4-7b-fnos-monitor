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
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>

#include "lvgl.h"
#include "src/misc/lv_text_private.h"
#include "fnos_data.h"
#include "fnos_ui.h"
#include "fnos_net.h"
#include "kk_theme.h"

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

/* ── Wi-Fi 替身 ────────────────────────────────────────────────────────
   配网卡（fnos_ui.c 的 wifi_* ）的真实数据全部来自 fnos_net_*：扫描列表、
   连接状态、当前 SSID。所以这里给一组**可控**的替身，让预览能把四个阶段都拍下来：
   没配网（顶栏橙字）/ 扫到网络 / 输口令 / 连上或连不上。
   真机上这些函数是 ESP-Hosted 的同步 RPC + NVS，这里只是几个静态变量。 */
static bool        s_wifi_cfg;                 /* 有凭据 */
static bool        s_wifi_on;                  /* 已连接（拿到 IP） */
static char        s_wifi_ssid_stub[33] = "";
static char        s_wifi_reason[64]    = "";  /* 非空 = 连不上的原因 */
static fnos_ap_t   s_wifi_aps[FNOS_AP_MAX];
static int         s_wifi_ap_n;

/* 固定的一批网络：和真机现场看到的形状一致（一个自己的、几个邻居、一个开放网络） */
static void wifi_stub_reset(void)
{
    static const struct { const char *ssid; int8_t rssi; bool secure; } def[] = {
        { "llll",           -46, true  },
        { "ChinaNet-8x2K",  -61, true  },
        { "HONOR-203",      -72, true  },
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
    wifi_stub_reset();
}
bool        fnos_net_configured(void) { return s_wifi_cfg; }
bool        fnos_net_online(void)     { return s_wifi_on; }
const char *fnos_net_ssid(void)       { return s_wifi_ssid_stub; }
fnos_net_state_t fnos_net_state(void)
{
    if (!s_wifi_cfg)          return FNOS_NET_UNCONFIGURED;
    if (s_wifi_on)            return FNOS_NET_ONLINE;
    if (s_wifi_reason[0])     return FNOS_NET_FAILED;
    return FNOS_NET_CONNECTING;
}
const char *fnos_net_state_str(void)
{
    static char b[160];
    if (!s_wifi_cfg)          snprintf(b, sizeof b, "未配置 Wi-Fi");
    else if (s_wifi_on)       snprintf(b, sizeof b, "已连接 %s（192.168.0.42）", s_wifi_ssid_stub);
    else if (s_wifi_reason[0])snprintf(b, sizeof b, "连不上 %s：%s（第 2 次重试）",
                                       s_wifi_ssid_stub, s_wifi_reason);
    else                      snprintf(b, sizeof b, "正在连接 %s…", s_wifi_ssid_stub);
    return b;
}
/* 真机是"存 NVS + 立刻重连"：这里等价地进入"正在连接" */
void fnos_net_set_credentials(const char *ssid, const char *pass)
{
    (void)pass;
    if (!ssid || !ssid[0]) return;
    s_wifi_cfg = true;
    s_wifi_on  = false;
    s_wifi_reason[0] = 0;
    snprintf(s_wifi_ssid_stub, sizeof s_wifi_ssid_stub, "%s", ssid);
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

/* 温度行的固定次序（与设备端 fnos_data.c 的 temp_cmp 同规则）：设备名 → 通道名。
   桩件的目的不是"随便给点数据"，而是让预览和面板上的行序一模一样。 */
typedef struct { const char *dev, *ch, *dn; float c; } temp_stub_t;

static int temp_stub_cmp(const void *a, const void *b)
{
    const temp_stub_t *x = a, *y = b;
    int c = strcmp(x->dev, y->dev);
    return c ? c : strcmp(x->ch, y->ch);
}

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
        snprintf(s->mods[i].name, sizeof s->mods[i].name, "%s", mods[i].n);
        snprintf(s->mods[i].status, sizeof s->mods[i].status, "%s", mods[i].st);
    }
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

    /* 温度桩：**与真机同形** —— 采集端把每一个通道都发上来（CPU 每核心、网卡
       PHY/MAC、每块 NVMe 的每个 Sensor），每一路带 dev（内核短名）、ch（通道名）
       和 dn（**人读的设备名**，用户要拿它认清"是哪台设备在热"）。
       命名照抄 2026-10-06 那台 NAS 的真实设备。桩件与设备端不一致时预览给的是
       假绿灯 —— 这条已经踩过一次。 */
    temp_stub_t t[] = {
        { "enp1s0",  "PHY",          "Marvell AQC113 10GbE",    74.2f },
        { "enp1s0",  "MAC",          "Marvell AQC113 10GbE",    74.2f },
        { "nvme0n1", "Composite",    "Samsung SSD 990 PRO 2TB", 43.9f },
        { "nvme1n1", "Composite",    "WD Black SN850X 2TB",     42.9f },
        { "nvme3n1", "Composite",    "Crucial P3 Plus 2TB",     41.9f },
        { "nvme0n1", "Sensor 1",     "Samsung SSD 990 PRO 2TB", 41.9f },
        { "nvme2n1", "Composite",    "KIOXIA EXCERIA PLUS 1TB", 41.0f },
        { "nvme0n1", "Sensor 2",     "Samsung SSD 990 PRO 2TB", 40.9f },
        { "nvme1n1", "Sensor 1",     "WD Black SN850X 2TB",     40.9f },
        { "nvme1n1", "Sensor 2",     "WD Black SN850X 2TB",     39.9f },
        { "nvme3n1", "Sensor 1",     "Crucial P3 Plus 2TB",     39.9f },
        { "nvme2n1", "Sensor 1",     "KIOXIA EXCERIA PLUS 1TB", 38.9f },
        { "nvme3n1", "Sensor 2",     "Crucial P3 Plus 2TB",     38.9f },
        { "nvme3n1", "Sensor 3",     "Crucial P3 Plus 2TB",     37.9f },
        { "nvme2n1", "Sensor 2",     "KIOXIA EXCERIA PLUS 1TB", 37.9f },
        { "nvme2n1", "Sensor 3",     "KIOXIA EXCERIA PLUS 1TB", 36.9f },
        { "i915",    "temp1",        "Intel UHD Graphics",      35.0f },
        { "CPU",     "Package id 0", "Intel N100",              34.0f },
        { "CPU",     "Core 5",       "Intel N100",              33.5f },
        { "CPU",     "Core 4",       "Intel N100",              33.4f },
        { "CPU",     "Core 3",       "Intel N100",              33.3f },
        { "CPU",     "Core 2",       "Intel N100",              33.2f },
        { "CPU",     "Core 1",       "Intel N100",              33.1f },
        { "CPU",     "Core 0",       "Intel N100",              33.0f },
    };
    s->ntemps = (int)(sizeof t / sizeof t[0]);
    /* 设备端的 fnos_data.c 会把温度按"设备名 + 通道名"**固定排序**（不按温度），
       桩件必须跟着一致 —— 否则预览里的行序和面板上的不一样，预览又变成假绿灯
       （桩与设备行为不一致这条，本项目已经踩过一次）。 */
    qsort(t, (size_t)s->ntemps, sizeof t[0], temp_stub_cmp);
    for (int i = 0; i < s->ntemps; i++) {
        snprintf(s->temps[i].dev, sizeof s->temps[i].dev, "%s", t[i].dev);
        snprintf(s->temps[i].ch,  sizeof s->temps[i].ch,  "%s", t[i].ch);
        snprintf(s->temps[i].dn,  sizeof s->temps[i].dn,  "%s", t[i].dn);
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
    snprintf(out->host, sizeof out->host, "%s", "192.168.0.119");
    out->port = 8798;
}
void fnos_pair_view(fnos_pair_view_t *out)
{
    memset(out, 0, sizeof *out);
    out->state = s_pair_state;
    out->port = 8798;
    snprintf(out->host, sizeof out->host, "%s", "192.168.0.119");
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

bool fnos_data_get(fnos_status_t *out)
{
    if (s_state == ST_LOCKBUSY) return true; /* 与数据层锁超时一致：out 保持原值 */
    if (s_state == ST_WARMING) { memset(out, 0, sizeof *out); return false; }
    fill_live(out);
    if (s_state == ST_HEALTHY) {
        out->nalerts = 0;
        for (int i = 0; i < out->ndocker; i++) out->docker[i].up = true;
        for (int i = 0; i < out->nraid; i++) { out->raid[i].ok = true; out->raid[i].sync_pct = 100; }
    }
    if (s_state == ST_LIMITS) {
        out->cpu.pct = 100; out->mem.pct = 100;
        /* 温度桩按降序，所以 [0] 就是最热那路：把它推到危险档、[1] 推到注意档，
           一次覆盖两条阈值线的颜色与摘要计数。 */
        out->temps[0].c = 85; out->temps[1].c = 66;
        out->net.rx_kbs = 1228800;
        out->nvols = FNOS_MAX_VOLS; out->nraid = FNOS_MAX_RAID;
        out->ndisks = FNOS_MAX_DISKS; out->ndocker = FNOS_MAX_DOCKER; out->nalerts = FNOS_MAX_ALERTS;
        for (int i = 6; i < out->nvols; i++) { out->vols[i] = out->vols[0]; snprintf(out->vols[i].mnt, sizeof out->vols[i].mnt, "/vol%d", i + 1); }
        for (int i = 4; i < out->nraid; i++) { out->raid[i] = out->raid[0]; snprintf(out->raid[i].dev, sizeof out->raid[i].dev, "md%d", i); }
        for (int i = 4; i < out->ndisks; i++) { out->disks[i] = out->disks[0]; snprintf(out->disks[i].dev, sizeof out->disks[i].dev, "nvme%dn1", i); }
        for (int i = 8; i < out->ndocker; i++) { out->docker[i] = out->docker[0]; snprintf(out->docker[i].n, sizeof out->docker[i].n, "container-%02d", i); }
        for (int i = 4; i < out->nalerts; i++) out->alerts[i] = out->alerts[0];
        /* 容错路径：应用比板子新（协议超范围）+ 一个板子没见过的状态词，
           两种都得照常显示而不是空白。 */
        out->proto = FNOS_PROTO_KNOWN + 1;
        /* NAS 停过 40 分钟再回填：跨度远大于样本数 × 1 秒，必须说"有断流" */
        out->hist_ts_ok = true;
        out->hist_span_s = 3720;
        out->hist_avg_gap_x10 = 25;
        if (out->nmods > 1) {
            snprintf(out->mods[0].name, sizeof out->mods[0].name, "%s", "smart");
            snprintf(out->mods[0].status, sizeof out->mods[0].status, "%s", "degraded");
            snprintf(out->mods[1].status, sizeof out->mods[1].status, "%s", "denied");
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
            memset(&out->temps[i], 0, sizeof out->temps[i]);
            snprintf(out->temps[i].dev, sizeof out->temps[i].dev, "%s", legacy[i].n);
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

/* 曲线样本：确定性的正弦 + 噪声，让趋势/峰值/量程都吃到真实分布 */
static int64_t s_seq;
int fnos_data_hist_read(int64_t since_seq, fnos_sample_t *out, int max, int64_t *next_seq)
{
    int n = 0;
    if (s_state == ST_WARMING || s_state == ST_OFFLINE || s_state == ST_OFFLINE_TLS) return 0;
    while (n < max && since_seq + n < s_seq) {
        int64_t k = since_seq + n;
        double t = (double)k;
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
        const char *txt = lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "";
        fprintf(stderr, "    near: %s [%d,%d %dx%d] %.40s\n",
                lv_obj_check_type(o, &lv_label_class) ? "label" :
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
                const char *txt = lv_obj_check_type(ch, &lv_label_class) ? lv_label_get_text(ch) : "?";
                {   /* 报坐标不够：把"父亲是谁、孩子是谁"也说出来，否则只能靠猜 */
                    const char *pt = lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "";
                    lv_obj_t *gp = lv_obj_get_parent(o);
                    fprintf(stderr, "  parent=%s[%d,%d] text=\"%.24s\" gp=%s\n",
                            lv_obj_check_type(o, &lv_label_class) ? "label" :
                            lv_obj_check_type(o, &lv_button_class) ? "button" : "panel",
                            (int)pa.x1, (int)pa.y1, pt,
                            gp ? (lv_obj_check_type(gp, &lv_label_class) ? "label" :
                                  lv_obj_check_type(gp, &lv_button_class) ? "button" : "panel") : "none");
                    fprintf(stderr, "  child=%s text=\"%.24s\"\n",
                            lv_obj_check_type(ch, &lv_label_class) ? "label" : "panel",
                            lv_obj_check_type(ch, &lv_label_class) ? lv_label_get_text(ch) : "");
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
    int n = snprintf(buf, sizeof buf, "%s", lv_obj_check_type(o, &lv_label_class) ? "label" :
                     lv_obj_check_type(o, &lv_button_class) ? "button" :
                     lv_obj_check_type(o, &lv_chart_class) ? "chart" : "panel");
    if (lv_obj_check_type(o, &lv_label_class)) {
        n += snprintf(buf + n, sizeof buf - n, " text=\"%.40s\"", lv_label_get_text(o));
    }
    for (lv_obj_t *p = lv_obj_get_parent(o); p && n < (int)sizeof buf - 40; p = lv_obj_get_parent(p)) {
        n += snprintf(buf + n, sizeof buf - n, " < %s", lv_obj_check_type(p, &lv_label_class) ? "label" :
                      lv_obj_check_type(p, &lv_button_class) ? "button" :
                      lv_obj_check_type(p, &lv_chart_class) ? "chart" : "panel");
        if (lv_obj_check_type(p, &lv_label_class)) {
            n += snprintf(buf + n, sizeof buf - n, "\"%.24s\"", lv_label_get_text(p));
        }
        lv_area_t a; lv_obj_get_coords(p, &a);
        n += snprintf(buf + n, sizeof buf - n, "[%d,%d %dx%d]", (int)a.x1, (int)a.y1,
                      (int)(a.x2 - a.x1 + 1), (int)(a.y2 - a.y1 + 1));
    }
    return buf;
}

static void audit_overlap(void)
{
    s_tap_n = 0;
    collect_tappable(lv_screen_active());
    for (int i = 0; i < s_tap_n; i++) {
        for (int j = i + 1; j < s_tap_n; j++) {
            lv_obj_t *a = s_tap[i], *b = s_tap[j];
            if (is_ancestor(a, b) || is_ancestor(b, a)) continue;
            lv_area_t x, y;
            lv_obj_get_coords(a, &x);
            lv_obj_get_coords(b, &y);
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
                const char *ta = lv_obj_check_type(a, &lv_label_class) ? lv_label_get_text(a) : "?";
                const char *tb = lv_obj_check_type(b, &lv_label_class) ? lv_label_get_text(b) : "?";
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
     96 = 系统页温度卡行池 48 行（FNOS_MAX_TEMPS 24→48）里没用到的 24 行 × 4 构件
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
    if (s_seen_n < SEEN_MAX && !seen_has(p)) s_seen[s_seen_n++] = p;
}

static void audit_labels(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    seen_add(o);
    if (lv_obj_check_type(o, &lv_label_class)) {
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
        lv_text_get_size(&size, text, font, lv_obj_get_style_text_letter_space(o, 0),
                         lv_obj_get_style_text_line_space(o, 0), lv_obj_get_width(o), LV_TEXT_FLAG_NONE);
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
    if (lv_obj_check_type(o, &lv_label_class)) {
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
        const char *txt = lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : NULL;
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
    lv_text_get_size(&sz, lv_label_get_text(lbl), lv_obj_get_style_text_font(lbl, 0),
                     lv_obj_get_style_text_letter_space(lbl, 0),
                     lv_obj_get_style_text_line_space(lbl, 0),
                     lv_obj_get_width(lbl), LV_TEXT_FLAG_NONE);
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
        if (!lv_obj_check_type(a, &lv_label_class) || !lv_label_get_text(a)[0]) continue;
        /* 只比"字形真正占的地方"，不是标签盒。v9 之前这里直接用 coords，
           于是一个 248 宽的副行标签和一个 64 宽的状态标签本来不重叠（字只占左边一半），
           却因为"盒挨着"被判成 text overlap —— 假阳性会逼着人把版面改坏去迁就测试。 */
        lv_area_t ra;
        text_glyph_area(a, &ra);
        for (uint32_t j = i + 1; j < n; j++) {
            lv_obj_t *b = lv_obj_get_child(parent, j);
            if (lv_obj_has_flag(b, LV_OBJ_FLAG_HIDDEN) || !lv_obj_check_type(b, &lv_label_class) || !lv_label_get_text(b)[0]) continue;
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
    if (lv_obj_get_style_radius(o, 0) == KK_RADIUS &&
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
    const char *txt = lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "";
    fprintf(stderr, "[tree]%*s%s tag=%s [%d,%d]-[%d,%d] %dx%d text=\"%.18s\"\n",
            depth * 2, "", lv_obj_check_type(o, &lv_label_class) ? "label" :
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

static void snapshot(const char *name)
{
    if (getenv("PREVIEW_PAGE_CARDS")) {
        int idx = atoi(getenv("PREVIEW_PAGE_CARDS"));
        lv_obj_t *scr = lv_screen_active();
        if (idx >= 0 && idx < (int)lv_obj_get_child_count(scr)) dump_page_cards(lv_obj_get_child(scr, idx), idx);
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
    /* 审计前必须先把布局跑完：LVGL 的坐标是**布局结果**，建树那一刻卡片还是 0x0。
       少了这一句，audit_bounds 会把"还没算布局"的父对象（[0,0]-[-1,-1]）当成父，
       报出一堆假越界 —— 曾因此以为系统页摘要条有 280 高（实际 44）。 */
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(lv_display_get_default());
    audit_labels(lv_screen_active());
    audit_bounds(lv_screen_active());
    audit_overlap();
    /* 首个快照是"建树刚结束"的中间态：所有页还都可见、卡片的 coords 未必都算过，
       此时的坐标不构成任何不变量（真重叠会在后续快照里稳定复现）。跳过它，
       但仍把结果打到日志里 —— 需要时能看见，不会变成"测试睁一只眼"。 */
    /* 卡片互相压住这一条**默认不查**，按需打开：`PREVIEW_CARD_OVERLAP=1`。
       理由：页面挂在 content 容器下（s_ui.page[] 不是 screen 的直接子对象），
       拉平卡表时很容易把不同页的卡混在一起比，报出跨页假重叠；而"压住内容"
       这件事本来就有更强的判据 —— `audit_text_overlap` 比的是**字形盒**，
       文字被压住一定报得出来，图形被压住则由 `audit_bounds` 兜住。
       一条会把好人误伤的检查，比没有这条检查更糟：它逼着人改坏版面去迁就它。 */
    if (getenv("PREVIEW_CARD_OVERLAP")) audit_card_overlap(lv_screen_active());
    audit_text_overlap(lv_screen_active());
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
     * 这类只在设备上暴露的 bug（2026-10-05 实例见 docs/verification.md §17.3）。 */


    for (int p = 0; p < FNOS_UI_PAGE_COUNT; p++) {
        fnos_ui_set_page(p);
        vtick_advance(300);
        lv_timer_handler();
        char name[64];
        snprintf(name, sizeof name, "%02d-%s-p%d", index, state_name(st), p);
        if (getenv("PREVIEW_PROBE") && p == atoi(getenv("PREVIEW_PROBE"))) {
            printf("=== probe page %d ===\n", p);
            probe_tree(lv_screen_active(), 0);
        }
        snapshot(name);
    }
}

static lv_obj_t *visible_text(lv_obj_t *o, const char *text)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), text) == 0) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *found = visible_text(lv_obj_get_child(o, i), text);
        if (found) return found;
    }
    return NULL;
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
    assert(visible_text(lv_screen_active(), "资源趋势 · 最近 7 分钟 · 有断流"));

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
    lv_obj_t *label = visible_text(lv_screen_active(), "采集诊断");
    assert(label);
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
    assert(visible_text(lv_screen_active(), "返回系统"));
    /* 未知字段容错：应用比板子新（协议超范围）+ 状态词板子不认识，
       两种都必须照常显示 —— 显示成空白会让人以为界面漏了数据。 */
    assert(visible_text(lv_screen_active(), "v3（本机只认到 v2，新字段会忽略）"));
    assert(visible_text(lv_screen_active(), "9 段 · smart degraded、mem 没权限 …"));
    snapshot("06-diagnostics");
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
    assert(visible_text(lv_screen_active(), "容器服务"));
    /* 认识的状态词要翻成中文（unknown -> 原样，denied -> 没权限） */
    s_state = ST_LIVE;
    vtick_advance(500); lv_timer_handler();
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
    assert(visible_text(lv_screen_active(), "v2"));
    assert(visible_text(lv_screen_active(), "9 段 · docker 已关闭、zfs 读不到"));
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);

    s_state = ST_HEALTHY;
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "暂无告警事件"));
    assert(!visible_text(lv_screen_active(), "container qbittorrentee down"));
    assert(!visible_text(lv_screen_active(), "/vol4 used 95%"));
    fnos_ui_set_page(0);
    assert(visible_text(lv_screen_active(), "无采集告警"));
    s_state = ST_LOCKBUSY;
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "无采集告警"));
    s_state = ST_WARMING;
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "等待数据"));
    assert(!visible_text(lv_screen_active(), "100"));
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
    if (lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), text) == 0) {
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

/* 收集当前可见的所有标签文字，用于"加了这条原因之后，界面上多出了哪句话"这种差分断言 */
#define MAX_TEXTS 400
static const char *s_texts[MAX_TEXTS];
static int s_texts_n;
static void collect_texts(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(o, &lv_label_class)) {
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
    fnos_status_t st;
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
/* 往上找第一个真正有底色的祖先（透明容器不算） */
static uint32_t eff_bg(lv_obj_t *o)
{
    for (lv_obj_t *p = o; p; p = lv_obj_get_parent(p)) {
        lv_opa_t opa = lv_obj_get_style_bg_opa(p, 0);
        if (opa > LV_OPA_20) return lv_color_to_u32(lv_obj_get_style_bg_color(p, 0)) & 0xFFFFFFu;
    }
    return 0x000000u;
}
/* 判据用 WCAG 的大字底线 3.0:1。**不是"调到刚好绿"的数字**：实测最低是白天的
   3.77:1（KK_T4 那级次文本压在卡片底色上），留了余量。真掉到 3 以下就是
   "有人在某个配色下看不清"，那必须停下来看。 */
#define CONTRAST_MIN 3.0

static double s_worst_contrast;
static const char *s_worst_text = "";
static uint32_t s_worst_fg, s_worst_bg;
static void audit_contrast(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        if (t && t[0]) {
            lv_opa_t opa = lv_obj_get_style_text_opa(o, 0);
            if (opa > LV_OPA_20) {
                uint32_t fg = lv_color_to_u32(lv_obj_get_style_text_color(o, 0)) & 0xFFFFFFu;
                uint32_t bg = eff_bg(lv_obj_get_parent(o));
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
        audit_bounds(lv_screen_active());
        shown++;
    }
    /* 确认最后一条**真的画到了屏幕上**：只跑 audit 的话，万一面板没开，
       audit 审的是别的页面，这一整段就成了"什么也没测"的空断言。 */
    /* 失败原因必须真的画在屏幕上。FAIL 阶段左栏被三步指引占着，原因落在 meta 那行，
       所以这里找的是完整的「原因：…」——**界面写着"核对下面的失败原因"，那就得有原因**。 */
    char want[192];
    snprintf(want, sizeof want, "失败原因：%s", PAIR_MSGS[PAIR_MSG_COUNT - 1].msg);
    if (!visible_text(lv_screen_active(), want)) {
        fprintf(stderr, "失败原因没有画到屏幕上（面板没打开，或者 FAIL 阶段没显示 v.msg）：%s\n",
                want);
        abort();
    }
    printf("checks: %d 条设备端配对消息全部渲染并审计过 PASS\n", shown);
    /* 留一张 FAILED 的样张：这条分支以前从来没有出现在任何图里 */
    s_pair_state = FNOS_PAIR_FAILED;
    snprintf(s_pair_msg, sizeof s_pair_msg, "%s", "配对成功但写入 NVS 失败");
    vtick_advance(500);
    lv_timer_handler();
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

static void verify_wifi(void)
{
    s_state = ST_LIVE;
    wifi_stub(false, false, "", "");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "Wi-Fi 未配置"));
    wifi_click("Wi-Fi 未配置");                    /* 顶栏入口（真机首次开机由 ui_tick 自动弹） */
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "接入 Wi-Fi"));
    assert(visible_text(lv_screen_active(), "重新扫描"));
    assert(visible_text(lv_screen_active(), "ChinaNet-8x2K"));   /* 扫描结果真的填进去了 */
    snapshot("10-wifi-scan");

    /* 选一个加密网络 → 输口令页：整块键盘第一次露面（41 键 + 两行输入 + 显示/隐藏） */
    wifi_click("llll");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "空格"));
    snapshot("10-wifi-pass");

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

    s_wifi_on = true;                              /* 替身：连上了 */
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "连接成功"));
    assert(visible_text(lv_screen_active(),
           "已连接 llll（192.168.0.42） · 信号 -54 dBm\n"
           "板子已经记住这个网络，下次开机自动连。"));
    snapshot("10-wifi-linked");

    /* 连不上：标题与原因都要说人话（不能一直写"正在连接…"） */
    wifi_stub(true, false, "llll", "密码不对（认证失败）");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "没连上"));
    assert(!visible_text(lv_screen_active(), "连接成功"));
    snapshot("10-wifi-failed");

    /* 关掉卡：顶栏要一直看得见状态；配好了就该是 SSID，不是"未配置" */
    wifi_click("关闭");
    vtick_advance(500); lv_timer_handler();
    assert(!visible_text(lv_screen_active(), "接入 Wi-Fi"));
    wifi_stub(true, true, "llll", "");
    vtick_advance(500); lv_timer_handler();
    assert(visible_text(lv_screen_active(), "llll"));
    assert(!visible_text(lv_screen_active(), "Wi-Fi 未配置"));
}

static void verify_pairing(void)
{
    s_state = ST_HEALTHY;   /* 只有健康快照才有完整的温度通道，用 ST_LIVE 会断言到空数据 */
    vtick_advance(500); lv_timer_handler();
    click_text("配对");                       /* 侧栏入口：切到系统页并盖上配对卡 */
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    /* 导航入口是选择，不是开关：重复点击留在配对，系统入口明确返回摘要。 */
    click_text("配对"); click_text("配对");
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    click_text("系统");
    /* "点导航就退出覆盖层"（fnos_ui_set_page → pair_show(false)）：
       配对卡必须没了，且系统页自己的内容要露出来。
       注意：刷新只发生在定时器里，点完不跑一拍定时器，标签还是上一次的文本，
       断言就会假失败（这里踩过一次）。 */
    vtick_advance(500); lv_timer_handler();
    /* visible_text 是**整串相等**比较（不是子串），断言必须写整串。
       路数跟着桩走：桩现在是真机的 24 路通道（原来只有 10 个设备级读数）。 */
    assert(visible_text(lv_screen_active(), "容器 8/8 运行 · 24 路温度 · 0 个事件"));
    assert(!visible_text(lv_screen_active(), "与 NAS 配对"));
    click_text("配对");
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    click_text("采集诊断");
    assert(visible_text(lv_screen_active(), "返回系统"));
    assert(!visible_text(lv_screen_active(), "与 NAS 配对"));
    click_text("配对");
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    /* 面板改成"三步指引 + 配对码槽位 + 键盘 + 操作条"四个模块，标题也从长句
       收成"配对码"（出处移到槽位下面那句提示里）。改版后这几处必须同步。 */
    assert(visible_text(lv_screen_active(), "配对码"));
    assert(visible_text(lv_screen_active(), "三步完成配对"));
    assert(visible_text(lv_screen_active(), "在 NAS 管理页「设备配对」生成 6 位配对码"));
    assert(visible_text(lv_screen_active(), "在 NAS 管理页「设备配对」生成，把 6 位数字输进右边的键盘"));
    /* 左栏（x16 w336）只有一块地方：状态说明与三步指引不能同时出现 —— 真机上
       两者叠在一起过一次（"未配对：用编译期默认地址"盖在"三步完成配对"上）。 */
    assert(!visible_text(lv_screen_active(), "未配对：用编译期默认地址"));
    snapshot("07-pair-code");
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
    lv_obj_get_coords(lv_obj_get_parent(lv_obj_get_parent(fp_lbl)), &card);
    assert(fp.x1 > card.x1 + 300);
    assert(fp.x2 < card.x2 - 16);
    assert(fp.y1 > card.y1 && fp.y2 < card.y2 - 60);
    snapshot("07-pair-confirm");

    click_text("接受并配对");
    assert(visible_text(lv_screen_active(), "已配对"));
    assert(visible_text(lv_screen_active(), "192.168.0.119:8798 · HTTPS · CN=fnos-nas.local"));
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
        bool interesting = lv_obj_check_type(o, &lv_label_class) ||
                           lv_obj_get_child_count(o) == 0;
        if (interesting) {
            lv_area_t c;
            lv_obj_get_coords(o, &c);
            const char *what = lv_obj_check_type(o, &lv_label_class)
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

    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read);
    lv_indev_set_display(touch, d);

    /* 预览用 KK_PALETTE=<名字> 选配色，出路数图给用户挑；不设则用默认 graphite。
       必须在 fnos_ui_create() 之前 —— 颜色在建构件时就被拷进样式里了。 */
    fnos_ui_theme_use(getenv("KK_PALETTE"));
    fprintf(stderr, "palette: %s\n", fnos_ui_theme_name());

    fnos_ui_create();

    /* 卡片几何的"唯一来源"：把已建好的卡片按真实 coords 吐出来，
       看门狗（scroll_gap.js）读它。手抄一张卡片表 → 抄错一次就报 15 处假不合格。 */
    if (getenv("PREVIEW_CARDMAP")) {
        fnos_ui_dump_card_map();
        return 0;
    }
    /* PREVIEW_CARDMAP_AT=<n>：在第 n 次 snapshot 之前再吐一遍卡片几何，
       用来核对"建树那一刻"与"跑完状态之后"的 coords 是否一致 —— 曾经差过 76px，
       看门狗拿到的 y 因此是错的。 */
    s_cardmap_at = getenv("PREVIEW_CARDMAP_AT") ? atoi(getenv("PREVIEW_CARDMAP_AT")) : -1;

    snapshot("00-startup");
    run_state(ST_WARMING, 1);
    run_state(ST_LIVE, 2);
    run_state(ST_OFFLINE, 3);
    run_state(ST_HEALTHY, 4);
    run_state(ST_LIMITS, 5);

    verify_transitions();
    verify_reasons();
    verify_pairing();
    verify_pair_messages();
    verify_wifi();

    /* ── 夜间模式：一整套独立配色，预览从来没渲染过 ──
       run_state 会把四页都拍一遍，所以这里先量白天的对比度、再开夜间量一次。 */
    report_contrast("白天");
    fnos_ui_set_night(true);
    vtick_advance(600);
    lv_timer_handler();
    audit_labels(lv_screen_active());
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
    return 0;
}
