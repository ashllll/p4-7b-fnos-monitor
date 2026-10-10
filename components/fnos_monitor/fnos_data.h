#pragma once
// fnOS（飞牛 NAS）状态数据层：轮询 NAS 上的 fnos-agent（HTTP JSON），
// 解析成动态快照 + 曲线环形缓冲，供 UI 层读取。
//
// 线程模型：本模块自己起一个轮询任务（栈在 PSRAM）；
// 快照与曲线由互斥量保护，UI 侧（LVGL 任务）只调用 fnos_data_get / fnos_data_hist_read。
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hardware inventories are sized from each frame, never from a particular NAS.
   Strings and vectors share an owned snapshot; initialize handles to {0}, and
   use fnos_status_copy/release instead of raw assignment across owners. */
// 本机认识的采集端协议版本。比这更新的版本不拒绝，只是把新字段忽略掉并在
// 诊断页标出来（见 fnos_ui.c 的协议那一行）——旧板子遇到新应用要能用，
// 而不是整帧作废。
#define FNOS_PROTO_KNOWN 2

#ifndef CONFIG_FNOS_CHART_WINDOW
#define CONFIG_FNOS_CHART_WINDOW 180
#endif
#define FNOS_HIST_MAX 600                      // 环形缓冲容量（秒），>= 曲线窗口

typedef struct { const char *ifname; float rx_kbs, tx_kbs, rx_total_gb, tx_total_gb;
                 const char *state; int speed_mbps; bool physical; } fnos_netif_t;
typedef struct { const char *mnt, *fs; float total_gb, used_gb, free_gb, pct; } fnos_vol_t;
/* health = 采集端给的**结构**健康度（ok/degraded/inactive/readonly/container/unknown），
   what = 阵列正在做的维护动作（resync/recovery/check/repair…）。
   两者都是追加字段：老采集端不给就是空串，界面当"未知"处理。
   以前界面是在 state 里 strstr "sync"/"recover"/"reshape" 判断进度——而动作词在
   what 里、state 只有首词（"active"），那个分支对真实数据**永远为假**，
   于是降级阵列的进度百分比从来没显示过（审计 §8.2）。 */
typedef struct { const char *dev, *lvl, *state, *health, *what; bool ok;
                 int have, want; float sync_pct; } fnos_raid_t;
typedef struct { const char *dev; float rd_kbs, wr_kbs; } fnos_disk_t;
/* 温度：一条 = 一个**传感器通道**（不是一个设备）。
   dev/ch = 内核短名与通道名（enp1s0 / PHY、nvme2n1 / Composite…），保证"同一路永远
   认得住"；dn = **人读的设备名**（"Marvell AQC113 10GbE" / "Samsung SSD 990 PRO 2TB" /
   "Intel N100"）—— 用户要拿它决定"给谁降温"，所以界面上优先显示 dn，"NIC"这种缩写
   等于没说。老采集端两个字段都没有，解析层把它的拼接名塞进 dev，界面照旧显示。 */
typedef struct { const char *dev, *ch, *dn; float c; } fnos_temp_t;
typedef struct { const char *n; bool up; const char *s; } fnos_docker_t;
typedef struct { const char *lv, *m; } fnos_alert_t;
// 采集段状态（协议 v2 的 modules 字段）。status 原样收下不认识的词，
// 显示时再决定怎么翻译——不能在解析层就把没见过的状态丢掉。
typedef struct { const char *name, *status; } fnos_mod_t;

typedef struct {
    bool     ever_ok;          // 至少成功拿到过一帧
    bool     online;           // 最近一次请求成功
    int64_t  recv_ms;          // 最近一次成功接收的时刻（esp_timer ms）
    int64_t  fail_ms;          // 最近一次失败的时刻
    int      http_ms;          // 最近一次请求耗时
    int      last_status;      // 最近一次 HTTP 状态码（0 = 连接层失败）
    uint32_t ok_count, fail_count;
    char     last_err[48];     // 最近一次失败原因（"timeout" / "http 500" / "connect" ...）

    const char *host;
    uint32_t uptime_s;

    struct { float pct, load1, load5, load15, temp_c; int cores, runq, procs; } cpu;
    struct { float total_mb, used_mb, avail_mb, pct, swap_total_mb, swap_used_mb; } mem;
    fnos_netif_t net;

    int nnets; fnos_netif_t *nets;
    int nvols;   fnos_vol_t *vols;
    int nraid;   fnos_raid_t *raid;
    int ndisks;  fnos_disk_t *disks;
    int ntemps;  fnos_temp_t *temps;
    int ndocker; fnos_docker_t *docker;
    bool has_zfs; float zfs_arc_gb, zfs_hit_pct;
    int nalerts; fnos_alert_t *alerts;

    int source_dropped;                   // older/configured collectors may still limit lists
    int64_t source_ts;
    void *storage;                        // private immutable inventory owner
    int proto;                              // 采集端协议版本（0 = 旧版应用，没上报）
    int nmods;  fnos_mod_t *mods;

    // 历史回填的时间语义：曲线横轴是"多少次采集"，用户要看的是"多长时间"。
    // 用首尾样本的 ts 相减（差值不受板子/NAS 时钟不同步影响），
    // hist_ts_ok=false 表示应用没给可用的 ts（旧版应用），界面退回按次数说。
    bool hist_ts_ok;
    int  hist_span_s;                       // 回填的历史覆盖的秒数
    int  hist_avg_gap_x10;                  // 平均采样间隔 ×10 秒（判断中间有没有断流）
} fnos_status_t;

/* Count is data, byte budget is a configurable resource policy. */
typedef struct { int vols, raid, disks, temps, docker, alerts, mods, nets; } fnos_counts_t;
bool fnos_status_create(fnos_status_t *out, const fnos_counts_t *counts);
bool fnos_status_text(fnos_status_t *out, const char **field, const char *format, ...);
bool fnos_status_parse(const char *json, fnos_status_t *out, const char **reason);
void fnos_status_copy(fnos_status_t *out, const fnos_status_t *source);
void fnos_status_release(fnos_status_t *out);

typedef struct { float cpu, mem, rx_kbs, tx_kbs; } fnos_sample_t;

// 启动轮询任务（幂等）。未联网时任务自己等待，不阻塞调用方。
void fnos_data_start(void);
// 网络状态变化（断线/重连）：丢弃 HTTP 长连接与会话，避免继续用失效 socket。
void fnos_data_network_changed(void);
// 复制当前快照（线程安全）。返回 false 表示还没有任何有效数据。
bool fnos_data_get(fnos_status_t *out);
// 增量读取曲线样本：取 seq > 起始序号（since_seq 传 0 表示从头）的最多 max 个样本，
// 并把下一个游标写回 *next_seq。返回写入 out 的样本数。UI 侧每帧调用一次即可。
int  fnos_data_hist_read(int64_t since_seq, fnos_sample_t *out, int max, int64_t *next_seq);
// 当前已产生的样本总数（用于判断有无新数据）。
int64_t fnos_data_hist_seq(void);

/* ── 只读诊断（72h 长测采集用）──────────────────────────────────────────
   这几个接口只读、不加锁以外的工作、不改轮询节奏，也不参与任何控制路径：
   打开 CONFIG_FNOS_SOAK_DIAG 后由 LVGL 事件循环每 10 秒抄一份打到串口。
   时间戳单位是 esp_timer 毫秒（不是 epoch）；source_ts 是 NAS 那侧的采集时间
   （epoch 秒，0 = 还没有成功帧）。p95_ms 是最近 64 次请求耗时的 P95——
   失败也计入，因为"连不上"同样属于用户看到的延迟。 */
typedef struct {
    uint32_t ok_count, fail_count;
    int64_t  recv_ms;      // 最近一次成功接收的时刻（0 = 从未成功）
    int64_t  source_ts;    // 最近成功帧里 NAS 的采集时间戳
    int      http_ms;      // 最近一次请求耗时
    int      p95_ms;       // 最近 64 次请求耗时的 P95（样本不足时取已有个数）
    uint32_t uptime_s;     // NAS 上报的开机时长
    bool     online;
} fnos_data_diag_t;

// 复制一份诊断快照（线程安全）。任何字段缺失都是 0/false，不返回错误。
void fnos_data_diag(fnos_data_diag_t *out);
// 轮询任务栈的剩余高水位（字节，ESP-IDF 该 API 的单位就是字节）。
// 0 = 任务还没起来 ⇒ 调用方要按"测不到"处理，不能当成 0 字节可用栈。
uint32_t fnos_data_stack_min_free(void);

#ifdef __cplusplus
}
#endif
