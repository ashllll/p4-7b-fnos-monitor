#pragma once
// fnOS（飞牛 NAS）状态数据层：轮询 NAS 上的 fnos-agent（HTTP JSON），
// 解析成定长快照 + 曲线环形缓冲，供 UI 层读取。
//
// 线程模型：本模块自己起一个轮询任务（栈在 PSRAM）；
// 快照与曲线由互斥量保护，UI 侧（LVGL 任务）只调用 fnos_data_get / fnos_data_hist_read。
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 每段能装多少条。**这不是随便定的**：NAS 侧按 DEFAULT_LIMITS 截断之后才发过来，
   板子装不下的部分会被静默丢掉（没有错误、没有提示，用户看到的就是"只有这么多"）。
   所以这几个数必须 >= NAS 的截断上限，`nas/fpk/contract_check.py` 会核对。
   改动前先看那边的 DEFAULT_LIMITS。 */
#define FNOS_MAX_VOLS    12     /* NAS 的 vols 上限是 12 */
#define FNOS_MAX_RAID    8      /* NAS 的 raid 上限是 8 */
#define FNOS_MAX_DISKS   10     /* NAS 的 disks 上限是 10 */
#define FNOS_MAX_TEMPS   48     /* NAS 的 temps 上限是 48。采集端现在报**每一个通道**
                                   （CPU 每核心、网卡 PHY/MAC、每块盘每个 Sensor），
                                   24 路是常态，多盘多核的机器很容易超过 24 */
#define FNOS_MAX_DOCKER  16     /* NAS 的 docker 上限是 16；容器一多的机器这里最容易撞 */
#define FNOS_MAX_ALERTS  8
#define FNOS_MAX_MODS    10
// 本机认识的采集端协议版本。比这更新的版本不拒绝，只是把新字段忽略掉并在
// 诊断页标出来（见 fnos_ui.c 的协议那一行）——旧板子遇到新应用要能用，
// 而不是整帧作废。
#define FNOS_PROTO_KNOWN 2

#ifndef CONFIG_FNOS_CHART_WINDOW
#define CONFIG_FNOS_CHART_WINDOW 180
#endif
#define FNOS_HIST_MAX 600                      // 环形缓冲容量（秒），>= 曲线窗口

typedef struct { char mnt[20]; char fs[12]; float total_gb, used_gb, free_gb, pct; } fnos_vol_t;
typedef struct { char dev[10]; char lvl[10]; char state[14]; bool ok; int have, want; float sync_pct; } fnos_raid_t;
typedef struct { char dev[10]; float rd_kbs, wr_kbs; } fnos_disk_t;
/* 温度：一条 = 一个**传感器通道**（不是一个设备）。
   dev/ch = 内核短名与通道名（enp1s0 / PHY、nvme2n1 / Composite…），保证"同一路永远
   认得住"；dn = **人读的设备名**（"Marvell AQC113 10GbE" / "Samsung SSD 990 PRO 2TB" /
   "Intel N100"）—— 用户要拿它决定"给谁降温"，所以界面上优先显示 dn，"NIC"这种缩写
   等于没说。老采集端两个字段都没有，解析层把它的拼接名塞进 dev，界面照旧显示。 */
typedef struct { char dev[20]; char ch[20]; char dn[28]; float c; } fnos_temp_t;
typedef struct { char n[24]; bool up; char s[40]; } fnos_docker_t;
typedef struct { char lv[8]; char m[60]; } fnos_alert_t;
// 采集段状态（协议 v2 的 modules 字段）。status 原样收下不认识的词，
// 显示时再决定怎么翻译——不能在解析层就把没见过的状态丢掉。
typedef struct { char name[12]; char status[16]; } fnos_mod_t;

typedef struct {
    bool     ever_ok;          // 至少成功拿到过一帧
    bool     online;           // 最近一次请求成功
    int64_t  recv_ms;          // 最近一次成功接收的时刻（esp_timer ms）
    int64_t  fail_ms;          // 最近一次失败的时刻
    int      http_ms;          // 最近一次请求耗时
    int      last_status;      // 最近一次 HTTP 状态码（0 = 连接层失败）
    uint32_t ok_count, fail_count;
    char     last_err[48];     // 最近一次失败原因（"timeout" / "http 500" / "connect" ...）

    char     host[32];
    uint32_t uptime_s;

    struct { float pct, load1, load5, load15, temp_c; int cores, runq, procs; } cpu;
    struct { float total_mb, used_mb, avail_mb, pct, swap_total_mb, swap_used_mb; } mem;
    struct { char ifname[16]; float rx_kbs, tx_kbs, rx_total_gb, tx_total_gb; } net;

    int nvols;   fnos_vol_t   vols[FNOS_MAX_VOLS];
    int nraid;   fnos_raid_t  raid[FNOS_MAX_RAID];
    int ndisks;  fnos_disk_t  disks[FNOS_MAX_DISKS];
    int ntemps;  fnos_temp_t  temps[FNOS_MAX_TEMPS];
    int ndocker; fnos_docker_t docker[FNOS_MAX_DOCKER];
    bool has_zfs; float zfs_arc_gb, zfs_hit_pct;
    int nalerts; fnos_alert_t alerts[FNOS_MAX_ALERTS];

    int proto;                              // 采集端协议版本（0 = 旧版应用，没上报）
    int nmods;  fnos_mod_t mods[FNOS_MAX_MODS];

    // 历史回填的时间语义：曲线横轴是"多少次采集"，用户要看的是"多长时间"。
    // 用首尾样本的 ts 相减（差值不受板子/NAS 时钟不同步影响），
    // hist_ts_ok=false 表示应用没给可用的 ts（旧版应用），界面退回按次数说。
    bool hist_ts_ok;
    int  hist_span_s;                       // 回填的历史覆盖的秒数
    int  hist_avg_gap_x10;                  // 平均采样间隔 ×10 秒（判断中间有没有断流）
} fnos_status_t;

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

#ifdef __cplusplus
}
#endif
