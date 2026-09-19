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

#define FNOS_MAX_VOLS    8
#define FNOS_MAX_RAID    6
#define FNOS_MAX_DISKS   8
#define FNOS_MAX_TEMPS   10
#define FNOS_MAX_DOCKER  12
#define FNOS_MAX_ALERTS  8

#ifndef CONFIG_FNOS_CHART_WINDOW
#define CONFIG_FNOS_CHART_WINDOW 180
#endif
#define FNOS_HIST_MAX 600                      // 环形缓冲容量（秒），>= 曲线窗口

typedef struct { char mnt[20]; char fs[12]; float total_gb, used_gb, free_gb, pct; } fnos_vol_t;
typedef struct { char dev[10]; char lvl[10]; char state[14]; bool ok; int have, want; float sync_pct; } fnos_raid_t;
typedef struct { char dev[10]; float rd_kbs, wr_kbs; } fnos_disk_t;
typedef struct { char n[12]; float c; } fnos_temp_t;
typedef struct { char n[24]; bool up; char s[40]; } fnos_docker_t;
typedef struct { char lv[8]; char m[60]; } fnos_alert_t;

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

    struct { float pct, load1, load5, load15, temp_c; int cores, runq; } cpu;
    struct { float total_mb, used_mb, avail_mb, pct; } mem;
    struct { char ifname[16]; float rx_kbs, tx_kbs, rx_total_gb, tx_total_gb; } net;

    int nvols;   fnos_vol_t   vols[FNOS_MAX_VOLS];
    int nraid;   fnos_raid_t  raid[FNOS_MAX_RAID];
    int ndisks;  fnos_disk_t  disks[FNOS_MAX_DISKS];
    int ntemps;  fnos_temp_t  temps[FNOS_MAX_TEMPS];
    int ndocker; fnos_docker_t docker[FNOS_MAX_DOCKER];
    bool has_zfs; float zfs_arc_gb, zfs_hit_pct;
    int nalerts; fnos_alert_t alerts[FNOS_MAX_ALERTS];
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
