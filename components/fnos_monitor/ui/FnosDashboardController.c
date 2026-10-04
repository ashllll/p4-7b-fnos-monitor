// 手写业务 partial（KK_UI_UMG 的 <PackageId>Controller.cs 等价物）。
//
// 边界（docs/ui-kk.md §5）：
//   * 只有本文件允许写 Store；View/Binder 不碰业务（fnos_data / fnos_net）。
//   * 一次 tick = 一次业务通知：批量 update_*，最后一次性 binder_flush。
//   * 所有显示格式化（printf 串、单位切换、阈值语义色）都在这里，Store 存最终字符串。
//   * 事件入口 OnXxxRequested 由生成的 view 转发进来，只做状态迁移（切页）。
#include "FnosDashboardController.h"
#include "fnos_dash_binder.generated.h"
#include "fnos_data.h"
#include "fnos_net.h"
#include "fnos_ui.h"
#include "kk_theme.h"          /* 分级阈值 KK_BAR_WARM/KK_BAR_FULL 与刻度共用 */

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#if __has_include("fnos_config.h")
#include "fnos_config.h"
#else
#include "fnos_config.example.h"
#endif

#ifndef FNOS_DEMO_MODE
#define FNOS_DEMO_MODE 0   /* 1 = 演示数据模式：顶栏常驻 DEMO 标识，不得冒充实测 */
#endif

/* ── 语义色（字符串形式进 Store，Binder 解析；与 kk_theme.h 同值）── */
#define C_OK     "#2FBF71"
#define C_WARN   "#F2B01E"
#define C_DANGER "#F0453D"
#define C_SYNC   "#4EC9E8"
#define C_IDLE   "#5A6B80"
#define C_HIDE   "#161E2B00"   /* alpha=0：隐藏空槽位圆点 */

#define STALE_MS 3000
#define TREND_PTS 90           /* 180 s 窗口 ÷ 2 s/点 */

static fnos_dash_view_t  *s_v;
static fnos_dash_store_t *s_s;
static int64_t s_hist_seq;
static bool s_mb_down, s_mb_up;      /* fmt_rate 的 MB 切换迟滞状态 */

#define SETS(id, ...)  do { char b_[96]; snprintf(b_, sizeof b_, __VA_ARGS__); \
                             fnos_dash_store_update_str(s_s, FNOS_DASH_FIELD_##id, b_); } while (0)
#define SETF(id, val)   fnos_dash_store_update_float(s_s, FNOS_DASH_FIELD_##id, (val))
#define SETI(id, val)   fnos_dash_store_update_int(s_s, FNOS_DASH_FIELD_##id, (val))
#define PUSH(id, val)   fnos_dash_store_series_push(s_s, FNOS_DASH_FIELD_##id, (val))
/* 带下标的字段走 id 表（C 预处理器拼不出 OvVal%d 这种名字） */
#define SETS_F(fid, ...) do { char b_[96]; snprintf(b_, sizeof b_, __VA_ARGS__); \
                              fnos_dash_store_update_str(s_s, (fid), b_); } while (0)
#define SETF_F(fid, val) fnos_dash_store_update_float(s_s, (fid), (val))
#define SETI_F(fid, val) fnos_dash_store_update_int(s_s, (fid), (val))

#define F_OV_VAL(i)        ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_OvVal0, FNOS_DASH_FIELD_OvVal1, FNOS_DASH_FIELD_OvVal2, FNOS_DASH_FIELD_OvVal3 }[(i)])
#define F_OV_SUB(i)        ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_OvSub0, FNOS_DASH_FIELD_OvSub1, FNOS_DASH_FIELD_OvSub2, FNOS_DASH_FIELD_OvSub3 }[(i)])
#define F_OV_MBAR(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_OvMbar0Value, FNOS_DASH_FIELD_OvMbar1Value, FNOS_DASH_FIELD_OvMbar2Value }[(i)])
#define F_OV_VOL_NAME(i)   ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_OvVolName0, FNOS_DASH_FIELD_OvVolName1, FNOS_DASH_FIELD_OvVolName2, FNOS_DASH_FIELD_OvVolName3, FNOS_DASH_FIELD_OvVolName4, FNOS_DASH_FIELD_OvVolName5 }[(i)])
#define F_OV_VOL_PCT(i)    ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_OvVolPct0, FNOS_DASH_FIELD_OvVolPct1, FNOS_DASH_FIELD_OvVolPct2, FNOS_DASH_FIELD_OvVolPct3, FNOS_DASH_FIELD_OvVolPct4, FNOS_DASH_FIELD_OvVolPct5 }[(i)])
#define F_OV_VOL_BAR(i)    ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_OvVolBar0Value, FNOS_DASH_FIELD_OvVolBar1Value, FNOS_DASH_FIELD_OvVolBar2Value, FNOS_DASH_FIELD_OvVolBar3Value, FNOS_DASH_FIELD_OvVolBar4Value, FNOS_DASH_FIELD_OvVolBar5Value }[(i)])
#define F_OV_VOL_USE(i)    ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_OvVolUse0, FNOS_DASH_FIELD_OvVolUse1, FNOS_DASH_FIELD_OvVolUse2, FNOS_DASH_FIELD_OvVolUse3, FNOS_DASH_FIELD_OvVolUse4, FNOS_DASH_FIELD_OvVolUse5 }[(i)])
#define F_ST_SEG_X(i)      ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StSeg0X, FNOS_DASH_FIELD_StSeg1X, FNOS_DASH_FIELD_StSeg2X, FNOS_DASH_FIELD_StSeg3X, FNOS_DASH_FIELD_StSeg4X, FNOS_DASH_FIELD_StSeg5X }[(i)])
#define F_ST_SEG_W(i)      ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StSeg0Width, FNOS_DASH_FIELD_StSeg1Width, FNOS_DASH_FIELD_StSeg2Width, FNOS_DASH_FIELD_StSeg3Width, FNOS_DASH_FIELD_StSeg4Width, FNOS_DASH_FIELD_StSeg5Width }[(i)])
#define F_ST_SEG_C(i)      ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StSeg0Color, FNOS_DASH_FIELD_StSeg1Color, FNOS_DASH_FIELD_StSeg2Color, FNOS_DASH_FIELD_StSeg3Color, FNOS_DASH_FIELD_StSeg4Color, FNOS_DASH_FIELD_StSeg5Color }[(i)])
#define F_ST_VOL_NAME(i)   ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StVolName0, FNOS_DASH_FIELD_StVolName1, FNOS_DASH_FIELD_StVolName2, FNOS_DASH_FIELD_StVolName3, FNOS_DASH_FIELD_StVolName4, FNOS_DASH_FIELD_StVolName5 }[(i)])
#define F_ST_VOL_FS(i)     ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StVolFs0, FNOS_DASH_FIELD_StVolFs1, FNOS_DASH_FIELD_StVolFs2, FNOS_DASH_FIELD_StVolFs3, FNOS_DASH_FIELD_StVolFs4, FNOS_DASH_FIELD_StVolFs5 }[(i)])
#define F_ST_VOL_USE(i)    ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StVolUse0, FNOS_DASH_FIELD_StVolUse1, FNOS_DASH_FIELD_StVolUse2, FNOS_DASH_FIELD_StVolUse3, FNOS_DASH_FIELD_StVolUse4, FNOS_DASH_FIELD_StVolUse5 }[(i)])
#define F_ST_VOL_PCT(i)    ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StVolPct0, FNOS_DASH_FIELD_StVolPct1, FNOS_DASH_FIELD_StVolPct2, FNOS_DASH_FIELD_StVolPct3, FNOS_DASH_FIELD_StVolPct4, FNOS_DASH_FIELD_StVolPct5 }[(i)])
#define F_ST_VOL_BAR(i)    ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StVolBar0Value, FNOS_DASH_FIELD_StVolBar1Value, FNOS_DASH_FIELD_StVolBar2Value, FNOS_DASH_FIELD_StVolBar3Value, FNOS_DASH_FIELD_StVolBar4Value, FNOS_DASH_FIELD_StVolBar5Value }[(i)])
#define F_ST_RAID_NAME(i)  ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StRaidName0, FNOS_DASH_FIELD_StRaidName1, FNOS_DASH_FIELD_StRaidName2, FNOS_DASH_FIELD_StRaidName3 }[(i)])
#define F_ST_RAID_LVL(i)   ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StRaidLvl0, FNOS_DASH_FIELD_StRaidLvl1, FNOS_DASH_FIELD_StRaidLvl2, FNOS_DASH_FIELD_StRaidLvl3 }[(i)])
#define F_ST_RAID_ST(i)    ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StRaidState0, FNOS_DASH_FIELD_StRaidState1, FNOS_DASH_FIELD_StRaidState2, FNOS_DASH_FIELD_StRaidState3 }[(i)])
#define F_ST_RAID_STC(i)   ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StRaidState0Color, FNOS_DASH_FIELD_StRaidState1Color, FNOS_DASH_FIELD_StRaidState2Color, FNOS_DASH_FIELD_StRaidState3Color }[(i)])
#define F_ST_IO_NAME(i)    ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StIoName0, FNOS_DASH_FIELD_StIoName1, FNOS_DASH_FIELD_StIoName2, FNOS_DASH_FIELD_StIoName3 }[(i)])
#define F_ST_IO_VAL(i)     ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StIoVal0, FNOS_DASH_FIELD_StIoVal1, FNOS_DASH_FIELD_StIoVal2, FNOS_DASH_FIELD_StIoVal3 }[(i)])
#define F_NW_AXIS(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_NwAxis0, FNOS_DASH_FIELD_NwAxis1, FNOS_DASH_FIELD_NwAxis2 }[(i)])
#define F_SY_DOT(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyDot0Color, FNOS_DASH_FIELD_SyDot1Color, FNOS_DASH_FIELD_SyDot2Color, FNOS_DASH_FIELD_SyDot3Color, FNOS_DASH_FIELD_SyDot4Color, FNOS_DASH_FIELD_SyDot5Color, FNOS_DASH_FIELD_SyDot6Color, FNOS_DASH_FIELD_SyDot7Color }[(i)])
#define F_SY_DOCK(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyDock0, FNOS_DASH_FIELD_SyDock1, FNOS_DASH_FIELD_SyDock2, FNOS_DASH_FIELD_SyDock3, FNOS_DASH_FIELD_SyDock4, FNOS_DASH_FIELD_SyDock5, FNOS_DASH_FIELD_SyDock6, FNOS_DASH_FIELD_SyDock7 }[(i)])
#define F_SY_DOCK_ST(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyDockState0, FNOS_DASH_FIELD_SyDockState1, FNOS_DASH_FIELD_SyDockState2, FNOS_DASH_FIELD_SyDockState3, FNOS_DASH_FIELD_SyDockState4, FNOS_DASH_FIELD_SyDockState5, FNOS_DASH_FIELD_SyDockState6, FNOS_DASH_FIELD_SyDockState7 }[(i)])
#define F_SY_DOCK_STC(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyDockState0Color, FNOS_DASH_FIELD_SyDockState1Color, FNOS_DASH_FIELD_SyDockState2Color, FNOS_DASH_FIELD_SyDockState3Color, FNOS_DASH_FIELD_SyDockState4Color, FNOS_DASH_FIELD_SyDockState5Color, FNOS_DASH_FIELD_SyDockState6Color, FNOS_DASH_FIELD_SyDockState7Color }[(i)])
#define F_SY_TNAME(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyTName0, FNOS_DASH_FIELD_SyTName1, FNOS_DASH_FIELD_SyTName2, FNOS_DASH_FIELD_SyTName3, FNOS_DASH_FIELD_SyTName4, FNOS_DASH_FIELD_SyTName5, FNOS_DASH_FIELD_SyTName6, FNOS_DASH_FIELD_SyTName7, FNOS_DASH_FIELD_SyTName8, FNOS_DASH_FIELD_SyTName9 }[(i)])
#define F_SY_DOCK_ROWA(i) ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyDockRow0Alpha, FNOS_DASH_FIELD_SyDockRow1Alpha, FNOS_DASH_FIELD_SyDockRow2Alpha, FNOS_DASH_FIELD_SyDockRow3Alpha, FNOS_DASH_FIELD_SyDockRow4Alpha, FNOS_DASH_FIELD_SyDockRow5Alpha, FNOS_DASH_FIELD_SyDockRow6Alpha, FNOS_DASH_FIELD_SyDockRow7Alpha }[(i)])
#define F_SY_TROWA(i)     ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyTempRow0Alpha, FNOS_DASH_FIELD_SyTempRow1Alpha, FNOS_DASH_FIELD_SyTempRow2Alpha, FNOS_DASH_FIELD_SyTempRow3Alpha, FNOS_DASH_FIELD_SyTempRow4Alpha, FNOS_DASH_FIELD_SyTempRow5Alpha, FNOS_DASH_FIELD_SyTempRow6Alpha, FNOS_DASH_FIELD_SyTempRow7Alpha, FNOS_DASH_FIELD_SyTempRow8Alpha, FNOS_DASH_FIELD_SyTempRow9Alpha }[(i)])
#define F_SY_TVAL(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyTVal0, FNOS_DASH_FIELD_SyTVal1, FNOS_DASH_FIELD_SyTVal2, FNOS_DASH_FIELD_SyTVal3, FNOS_DASH_FIELD_SyTVal4, FNOS_DASH_FIELD_SyTVal5, FNOS_DASH_FIELD_SyTVal6, FNOS_DASH_FIELD_SyTVal7, FNOS_DASH_FIELD_SyTVal8, FNOS_DASH_FIELD_SyTVal9 }[(i)])
#define F_SY_TVALC(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyTVal0Color, FNOS_DASH_FIELD_SyTVal1Color, FNOS_DASH_FIELD_SyTVal2Color, FNOS_DASH_FIELD_SyTVal3Color, FNOS_DASH_FIELD_SyTVal4Color, FNOS_DASH_FIELD_SyTVal5Color, FNOS_DASH_FIELD_SyTVal6Color, FNOS_DASH_FIELD_SyTVal7Color, FNOS_DASH_FIELD_SyTVal8Color, FNOS_DASH_FIELD_SyTVal9Color }[(i)])
#define F_SY_TBAR(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyTBar0Value, FNOS_DASH_FIELD_SyTBar1Value, FNOS_DASH_FIELD_SyTBar2Value, FNOS_DASH_FIELD_SyTBar3Value, FNOS_DASH_FIELD_SyTBar4Value, FNOS_DASH_FIELD_SyTBar5Value, FNOS_DASH_FIELD_SyTBar6Value, FNOS_DASH_FIELD_SyTBar7Value, FNOS_DASH_FIELD_SyTBar8Value, FNOS_DASH_FIELD_SyTBar9Value }[(i)])
#define F_SY_ALERT_LBL(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyAlertLbl0, FNOS_DASH_FIELD_SyAlertLbl1, FNOS_DASH_FIELD_SyAlertLbl2, FNOS_DASH_FIELD_SyAlertLbl3 }[(i)])
#define F_SY_ALERT_LBLC(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyAlertLbl0Color, FNOS_DASH_FIELD_SyAlertLbl1Color, FNOS_DASH_FIELD_SyAlertLbl2Color, FNOS_DASH_FIELD_SyAlertLbl3Color }[(i)])
#define F_SY_ALERT_DOT(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyAlertDot0Color, FNOS_DASH_FIELD_SyAlertDot1Color, FNOS_DASH_FIELD_SyAlertDot2Color, FNOS_DASH_FIELD_SyAlertDot3Color }[(i)])
#define F_ST_RAID_BAR(i)  ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_StRaidBar0Value, FNOS_DASH_FIELD_StRaidBar1Value, FNOS_DASH_FIELD_StRaidBar2Value, FNOS_DASH_FIELD_StRaidBar3Value }[(i)])
#define F_SY_KV(i)       ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_SyKvVal0, FNOS_DASH_FIELD_SyKvVal1, FNOS_DASH_FIELD_SyKvVal2, FNOS_DASH_FIELD_SyKvVal3, FNOS_DASH_FIELD_SyKvVal4, FNOS_DASH_FIELD_SyKvVal5, FNOS_DASH_FIELD_SyKvVal6, FNOS_DASH_FIELD_SyKvVal7, FNOS_DASH_FIELD_SyKvVal8, FNOS_DASH_FIELD_SyKvVal9, FNOS_DASH_FIELD_SyKvVal10, FNOS_DASH_FIELD_SyKvVal11 }[(i)])
#define F_OV_VALC(i)     ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_OvVal0Color, FNOS_DASH_FIELD_OvVal1Color, FNOS_DASH_FIELD_OvVal2Color, FNOS_DASH_FIELD_OvVal3Color }[(i)])

/* ── 格式化（与 v3 行为逐字一致）────────────────────────────────── */

static void fmt_cap(char *out, size_t n, float gb)
{
    if (gb >= 1024.0f) snprintf(out, n, "%.1f TB", gb / 1024.0f);
    else               snprintf(out, n, "%.0f GB", gb);
}

// 返回是否处于 MB 态；state 非 NULL 时带 1024/950 迟滞
static bool fmt_rate(char *out, size_t n, float kbs, bool *state)
{
    bool mb;
    if (state) {
        if (*state) { if (kbs < 950.0f)  *state = false; }
        else        { if (kbs >= 1024.0f) *state = true; }
        mb = *state;
    } else {
        mb = kbs >= 1024.0f;
    }
    if (mb) snprintf(out, n, "%.2f", kbs / 1024.0f);
    else if (kbs >= 100.0f) snprintf(out, n, "%.0f", kbs);
    else                    snprintf(out, n, "%.1f", kbs);
    return mb;
}

static void fmt_uptime(char *out, size_t n, uint32_t s)
{
    uint32_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60;
    if (d > 0)      snprintf(out, n, "%ud %02uh", (unsigned)d, (unsigned)h);
    else if (h > 0) snprintf(out, n, "%uh %02um", (unsigned)h, (unsigned)m);
    else            snprintf(out, n, "%um", (unsigned)m);
}

static void fmt_age(char *out, size_t n, int64_t s)
{
    if (s < 0)          snprintf(out, n, "--");
    else if (s < 90)    snprintf(out, n, "%llds", (long long)s);
    else if (s < 5400)  snprintf(out, n, "%lldm", (long long)(s / 60));
    else                snprintf(out, n, "%lldh", (long long)(s / 3600));
}

static int64_t data_age_s(const fnos_status_t *st)
{
    if (st->recv_ms <= 0) return -1;
    return (esp_timer_get_time() / 1000 - st->recv_ms) / 1000;
}

typedef enum { TR_WARMING, TR_LIVE, TR_STALE, TR_OFFLINE } trust_t;

static trust_t trust_state(const fnos_status_t *st)
{
    if (!st->ever_ok) return TR_WARMING;
    if (!st->online)  return TR_OFFLINE;
    return (data_age_s(st) < 3) ? TR_LIVE : TR_STALE;
}

/* 可信度 → 主值强调：LIVE 全白；stale/warming/offline 的旧值降透明度，
 * 不得看起来像实时读数（技能"身份/运行/可信度分层"要求）。色值仍走 #RRGGBBAA。 */
static const char *val_color(trust_t tr)
{
    return tr == TR_LIVE ? "#FFFFFFFF" : "#FFFFFF7A";   /* 白 48% */
}

static int alert_count(const fnos_status_t *st, const char *lv)
{
    int n = 0;
    for (int i = 0; i < st->nalerts && i < FNOS_MAX_ALERTS; i++)
        if (strcmp(st->alerts[i].lv, lv) == 0) n++;
    return n;
}

static int series_peak(const kk_series_t *s)
{
    int peak = 0;
    for (int i = 0; i < s->count; i++) {
        int v = (int)(s->v[i] + 0.5f);
        if (v > peak) peak = v;
    }
    return peak;
}

static int nice_max(int v)
{
    int n = 64;
    while (n < v && n < 65536) n *= 2;
    return n;
}

/* ── chrome：顶栏 + 底栏 ─────────────────────────────────────────── */

static void update_top(const fnos_status_t *st)
{
    SETS(HdHost, "%s", st->host[0] ? st->host : "NAS");

    /* B8：端点只出现一次（在顶栏）；本机 IP 与采集统计在 P3 采集端点 / 底栏 */
    SETS(HdEndpoint, "采集 %s:%d", FNOS_HOST, FNOS_PORT);

    int64_t age = data_age_s(st);
    switch (trust_state(st)) {
    case TR_LIVE:    SETS(HdChipText, "在线");                           SETS(HdChipColor, "%s", C_OK);     break;   // 秒数后缀已移除：1 Hz 稳态下永远 0~2s，无信息量
    case TR_WARMING: SETS(HdChipText, "等待数据");                     SETS(HdChipColor, "%s", C_WARN);   break;
    case TR_STALE:   SETS(HdChipText, "陈旧 %llds", (long long)age);   SETS(HdChipColor, "%s", C_WARN);   break;
    default:         SETS(HdChipText, "采集端离线");                   SETS(HdChipColor, "%s", C_DANGER); break;
    }
    /* D16：DEMO 态全局持续标识（演示/模拟数据不得冒充实测） */
    if (FNOS_DEMO_MODE) { SETS(HdChipText, "DEMO 演示数据"); SETS(HdChipColor, "%s", C_WARN); }

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (tmv.tm_year > 120) SETS(HdClock, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    else                   SETS(HdClock, "--:--");

    SETI(HdSignalValue, fnos_net_rssi());
}

static void update_bottom(const fnos_status_t *st)
{
    if (st->ever_ok) {
        char age[24];
        fmt_age(age, sizeof age, data_age_s(st));
        /* B8：端点已在顶栏，底栏专管采集质量 + 数据新鲜度 */
        SETS(FtPoll, "轮询 %u · 失败 %u · %d ms · 数据 %s 前",
             (unsigned)st->ok_count, (unsigned)st->fail_count, st->http_ms, age);
    } else {
        SETS(FtPoll, "等待首个数据帧");
    }

    if (trust_state(st) == TR_OFFLINE) {
        SETS(FtAlertLbl, "%s", st->last_err[0] ? st->last_err : "采集端不可达");
        SETS(FtAlertDotColor, "%s", C_DANGER);
        SETS(FtAlertLblColor, "%s", C_DANGER);
    } else if (st->nalerts > 0) {
        bool crit = (strcmp(st->alerts[0].lv, "crit") == 0);
        SETS(FtAlertLbl, "%s", st->alerts[0].m);
        SETS(FtAlertDotColor, "%s", crit ? C_DANGER : C_WARN);
        SETS(FtAlertLblColor, "%s", crit ? C_DANGER : C_WARN);
    } else {
        SETS(FtAlertLbl, "无告警");
        SETS(FtAlertDotColor, "%s", C_OK);
        SETS(FtAlertLblColor, "%s", C_OK);
    }
}

/* ── P0 总览 ─────────────────────────────────────────────────────── */

static void update_overview(const fnos_status_t *st)
{
    bool have = st->ever_ok;
    trust_t tr = trust_state(st);
    int crit = alert_count(st, "crit");
    int warn = alert_count(st, "warn");

    const char *vcol = C_OK;
    if (tr == TR_OFFLINE)        { SETS(OvVerdict, "离线"); vcol = C_DANGER; }
    else if (crit > 0)           { SETS(OvVerdict, "危险"); vcol = C_DANGER; }
    else if (warn > 0 || tr == TR_STALE) { SETS(OvVerdict, "注意"); vcol = C_WARN; }
    else if (tr == TR_WARMING)   { SETS(OvVerdict, "等待"); vcol = C_WARN; }
    else                         { SETS(OvVerdict, "正常"); vcol = C_OK; }
    SETS(OvVerdictColor, "%s", vcol);
    SETS(OvHealthAccentColor, "%s", vcol);   /* A2：健康卡左条＝严重度，不再是装饰蓝 */

    if (have) SETS(OvVerdictSub, "6 项检查 · %d 条告警", st->nalerts);
    else      SETS(OvVerdictSub, "等待首个数据帧");

    if (tr == TR_OFFLINE) {
        SETS(OvReason, "采集端不可达：%s", st->last_err[0] ? st->last_err : "poll failed");
    } else if (st->nalerts > 0) {
        if (st->nalerts > 1) SETS(OvReason, "%s  (+%d)", st->alerts[0].m, st->nalerts - 1);
        else                 SETS(OvReason, "%s", st->alerts[0].m);
    } else {
        SETS(OvReason, "%s", "");
    }

    // 遥测四卡：数值统一白，颜色只给条（能量渐变由 Bar 承担）
    float hot = 0;
    for (int i = 0; i < st->ntemps && i < FNOS_MAX_TEMPS; i++)
        if (st->temps[i].c > hot) hot = st->temps[i].c;

    if (have) {
        SETS(OvVal0, "%.0f", (double)st->cpu.pct);
        /* V2：副行 138px 预算＝约 10 个汉字；核数/进程数已无位置（进程在运行时长卡） */
        SETS(OvSub0, "负载 %.2f/%.2f/%.2f",
             (double)st->cpu.load1, (double)st->cpu.load5, (double)st->cpu.load15);
        SETF(OvMbar0Value, st->cpu.pct);

        SETS(OvVal1, "%.0f", (double)st->mem.pct);
        SETS(OvSub1, "%.1f/%.0f GB · 余 %.1f",
             (double)(st->mem.used_mb / 1024.0f), (double)(st->mem.total_mb / 1024.0f),
             (double)(st->mem.avail_mb / 1024.0f));
        SETF(OvMbar1Value, st->mem.pct);

        SETS(OvVal2, "%.0f", (double)hot);
        SETS(OvLegendCpuLbl, "CPU %.0f%%", (double)st->cpu.pct);
        SETS(OvLegendMemLbl, "MEM %.0f%%", (double)st->mem.pct);
        SETS(OvLegendTempLbl, "温度 %.0f°", (double)hot);
        /* V5.1：曲线窗口的峰值统计行（与曲线同源，全部走 series_peak） */
        SETS(OvPeakCpu, "峰 %d%%", series_peak(&s_s->ov_cpu_points));
        SETS(OvPeakMem, "峰 %d%%", series_peak(&s_s->ov_mem_points));
        SETS(OvPeakTemp, "峰 %d°", series_peak(&s_s->ov_temp_trend_points));
        if (st->ntemps > 0) SETS(OvSub2, "CPU %.0f°C · 最高 %s", (double)st->cpu.temp_c, st->temps[0].n);
        else                SETS(OvSub2, "CPU %.0f°C", (double)st->cpu.temp_c);
        SETF(OvMbar2Value, hot);

        char up[24];
        fmt_uptime(up, sizeof up, st->uptime_s);
        SETS(OvVal3, "%s", up);
        SETS(OvSub3, "进程 %d · runq %d", st->cpu.procs, st->cpu.runq);
    } else {
        for (int i = 0; i < 4; i++) { SETS_F(F_OV_VAL(i), "--"); SETS_F(F_OV_SUB(i), "%s", ""); }
        SETS(OvLegendCpuLbl, "CPU --");
        SETS(OvLegendMemLbl, "MEM --");
        SETS(OvLegendTempLbl, "温度 --");
        SETS(OvPeakCpu, "%s", "");
        SETS(OvPeakMem, "%s", "");
        SETS(OvPeakTemp, "%s", "");
    }

    for (int i = 0; i < 4; i++) SETS_F(F_OV_VALC(i), "%s", val_color(tr));   /* A3：可信度降级 */

    /* A4：CPU/MEM 合轨后必须共享量程（同为百分比），否则两条曲线各按峰值定标、跨轨不可比 */
    {
        int pk_cpu = series_peak(&s_s->ov_cpu_points), pk_mem = series_peak(&s_s->ov_mem_points);
        SETF(OvTrendTop, (float)nice_max(pk_cpu > pk_mem ? pk_cpu : pk_mem));
    }

    // 存储空间 6 行（数值白色；条走能量渐变）
    for (int i = 0; i < 6; i++) {
        if (i < st->nvols && i < FNOS_MAX_VOLS) {
            char bu[24], bf[24];
            fmt_cap(bu, sizeof bu, st->vols[i].used_gb);
            fmt_cap(bf, sizeof bf, st->vols[i].free_gb);
            SETS_F(F_OV_VOL_NAME(i), "%s", st->vols[i].mnt);
            /* V5.0-B：填补"名称 120px"与"百分比 80px"之间约 250px 的空白（单位随量级） */
            SETS_F(F_OV_VOL_USE(i), "已用 %s · 可用 %s", bu, bf);
            SETS_F(F_OV_VOL_PCT(i), "%.0f%%", (double)st->vols[i].pct);
            SETF_F(F_OV_VOL_BAR(i), st->vols[i].pct);
        } else {
            SETS_F(F_OV_VOL_NAME(i), "%s", "");
            SETS_F(F_OV_VOL_USE(i), "%s", "");
            SETS_F(F_OV_VOL_PCT(i), "%s", "");
            SETF_F(F_OV_VOL_BAR(i), 0);
        }
    }
}

/* ── P1 存储 ─────────────────────────────────────────────────────── */

static void update_storage(const fnos_status_t *st)
{
    float total = 0, used = 0, free = 0;
    for (int i = 0; i < st->nvols && i < FNOS_MAX_VOLS; i++) {
        total += st->vols[i].total_gb;
        used  += st->vols[i].used_gb;
        free  += st->vols[i].free_gb;
    }

    char a[24], b[24];
    if (used >= 1024.0f) { SETS(StHero, "%.1f", (double)(used / 1024.0f)); SETS(StUsed, "TB / 总量"); }
    else                 { SETS(StHero, "%.0f", (double)used);             SETS(StUsed, "GB / 总量"); }
    SETS(StHeroColor, "%s", val_color(trust_state(st)));   /* A3 */
    fmt_cap(a, sizeof a, used);
    fmt_cap(b, sizeof b, total);
    SETS(StCnt, "%s / %s", a, b);
    fmt_cap(a, sizeof a, free);
    SETS(StFree, "可用 %s", a);

    // 容量堆叠段：铺满 Hero 条带（x 20 → 964）、缝 3、最小 8px；
    // 分级与容量/温度刻度共用 KK_BAR_WARM/KK_BAR_FULL 档位（60/85）
    const int W = 929, GAP = 3, SEG_X0 = 20;
    int x = SEG_X0;
    for (int i = 0; i < 6; i++) {
        if (i < st->nvols && i < FNOS_MAX_VOLS && total > 0) {
            int wpx = (int)(W * (st->vols[i].total_gb / total) + 0.5f);
            if (wpx < 8) wpx = 8;
            float pct = st->vols[i].pct;
            SETI_F(F_ST_SEG_X(i), x);
            SETI_F(F_ST_SEG_W(i), wpx);
            SETS_F(F_ST_SEG_C(i), "%s", pct >= KK_BAR_FULL ? C_DANGER :
                                        pct >= KK_BAR_WARM ? "#C8E63C" : C_SYNC);
            x += wpx + GAP;
        } else {
            SETI_F(F_ST_SEG_X(i), x);
            SETI_F(F_ST_SEG_W(i), 0);
        }
    }

    // 卷格 6 个
    for (int i = 0; i < 6; i++) {
        if (i < st->nvols && i < FNOS_MAX_VOLS) {
            SETS_F(F_ST_VOL_NAME(i), "%s", st->vols[i].mnt);
            SETS_F(F_ST_VOL_FS(i), "%s", st->vols[i].fs);
            fmt_cap(b, sizeof b, st->vols[i].total_gb);
            char c_[24];
            fmt_cap(c_, sizeof c_, st->vols[i].free_gb);
            /* V2：180px 预算放不下"已用/总量 · 余"三段；用量% 已在右列，这里给总量+余量 */
            SETS_F(F_ST_VOL_USE(i), "总量 %s · 余 %s", b, c_);
            SETS_F(F_ST_VOL_PCT(i), "%.0f%%", (double)st->vols[i].pct);
            SETF_F(F_ST_VOL_BAR(i), st->vols[i].pct);
        } else {
            SETS_F(F_ST_VOL_NAME(i), "%s", "");
            SETS_F(F_ST_VOL_FS(i), "%s", "");
            SETS_F(F_ST_VOL_USE(i), "%s", "");
            SETS_F(F_ST_VOL_PCT(i), "%s", "");
            SETF_F(F_ST_VOL_BAR(i), 0);
        }
    }

    // ZFS ARC（has_zfs=false 时留空）
    if (st->has_zfs) {
        SETS(StZfsArc, "%.1f GB", (double)st->zfs_arc_gb);
        SETS(StZfsHit, "命中 %.1f%%", (double)st->zfs_hit_pct);
        SETF(StZfsBarValue, st->zfs_hit_pct);
    } else {
        SETS(StZfsArc, "%s", "");
        SETS(StZfsHit, "%s", "");
        SETF(StZfsBarValue, 0);
    }

    // RAID 4 行（状态色是语义，保留）
    for (int i = 0; i < 4; i++) {
        if (i < st->nraid && i < FNOS_MAX_RAID) {
            const fnos_raid_t *r = &st->raid[i];
            SETS_F(F_ST_RAID_NAME(i), "%s", r->dev);
            SETS_F(F_ST_RAID_LVL(i), "%s", r->lvl);
            SETF_F(F_ST_RAID_BAR(i), r->sync_pct >= 0 ? r->sync_pct : 0);
            if (r->sync_pct >= 0) {
                SETS_F(F_ST_RAID_ST(i), "同步 %.0f%%", (double)r->sync_pct);
                SETS_F(F_ST_RAID_STC(i), "%s", C_SYNC);
            } else if (r->ok) {
                SETS_F(F_ST_RAID_ST(i), "正常 %d/%d", r->have, r->want);
                SETS_F(F_ST_RAID_STC(i), "%s", C_OK);
            } else {
                SETS_F(F_ST_RAID_ST(i), "降级 %s", r->state);
                SETS_F(F_ST_RAID_STC(i), "%s", C_DANGER);
            }
        } else {
            SETF_F(F_ST_RAID_BAR(i), 0);
            SETS_F(F_ST_RAID_NAME(i), "%s", "");
            SETS_F(F_ST_RAID_LVL(i), "%s", "");
            SETS_F(F_ST_RAID_ST(i), "%s", "");
        }
    }

    // 磁盘活动 4 行
    for (int i = 0; i < 4; i++) {
        if (i < st->ndisks && i < FNOS_MAX_DISKS) {
            SETS_F(F_ST_IO_NAME(i), "%s", st->disks[i].dev);
            SETS_F(F_ST_IO_VAL(i), "R %.0f W %.0f", (double)st->disks[i].rd_kbs, (double)st->disks[i].wr_kbs);
        } else {
            SETS_F(F_ST_IO_NAME(i), "%s", "");
            SETS_F(F_ST_IO_VAL(i), "%s", "");
        }
    }
}

/* ── P2 网络 ─────────────────────────────────────────────────────── */

static void update_network(const fnos_status_t *st)
{
    char a[24];

    bool mb = fmt_rate(a, sizeof a, st->net.rx_kbs, &s_mb_down);
    SETS(NwDownVal, "%s", a);
    SETS(NwDownValColor, "%s", val_color(trust_state(st)));   /* A3 */
    SETS(NwDownUnit, "%s", mb ? "MB/s" : "KB/s");
    fmt_cap(a, sizeof a, st->net.rx_total_gb);
    SETS(NwDownSub, "累计 %s · %s", a, st->net.ifname[0] ? st->net.ifname : "-");

    mb = fmt_rate(a, sizeof a, st->net.tx_kbs, &s_mb_up);
    SETS(NwUpVal, "%s", a);
    SETS(NwUpValColor, "%s", val_color(trust_state(st)));   /* A3 */
    SETS(NwUpUnit, "%s", mb ? "MB/s" : "KB/s");
    fmt_cap(a, sizeof a, st->net.tx_total_gb);
    SETS(NwUpSub, "累计 %s · %s", a, st->net.ifname[0] ? st->net.ifname : "-");
    {
        bool mb_t = fmt_rate(a, sizeof a, st->net.rx_kbs + st->net.tx_kbs, NULL);
        SETS(NwTotalVal, "%s %s", a, mb_t ? "MB/s" : "KB/s");
    }

    int peak_down = series_peak(&s_s->nw_down_trend_points);
    int peak_up   = series_peak(&s_s->nw_up_trend_points);
    fmt_rate(a, sizeof a, (float)peak_down, NULL); SETS(NwCtxVal0, "%s", a);
    fmt_rate(a, sizeof a, (float)peak_up, NULL);   SETS(NwCtxVal1, "%s", a);
    SETS(NwCtxVal2, "%d ms", st->http_ms);
    SETS(NwCtxVal3, "%lld", (long long)s_hist_seq);

    int top = nice_max(peak_down > peak_up ? peak_down : peak_up);
    SETF(NwTrendTop, (float)top);
    for (int i = 0; i < 3; i++) {
        fmt_rate(a, sizeof a, (float)(top - (top / 2) * i), NULL);
        SETS_F(F_NW_AXIS(i), "%s", a);
    }
}

/* ── P3 系统 ─────────────────────────────────────────────────────── */

static void update_system(const fnos_status_t *st)
{
    int up = 0;
    for (int i = 0; i < st->ndocker && i < FNOS_MAX_DOCKER; i++)
        if (st->docker[i].up) up++;
    if (st->ndocker > 0) SETS(SyCnt, "%d / %d 运行中", up, st->ndocker);
    else                 SETS(SyCnt, "%s", "");

    for (int i = 0; i < 8; i++) {
        if (i < st->ndocker && i < FNOS_MAX_DOCKER) {
            const fnos_docker_t *d = &st->docker[i];
            SETS_F(F_SY_DOCK(i), "%s", d->n);
            SETS_F(F_SY_DOCK_ST(i), "%s", d->up ? "运行中" : d->s);
            SETS_F(F_SY_DOCK_STC(i), "%s", d->up ? C_OK : C_IDLE);
            SETS_F(F_SY_DOT(i), "%s", d->up ? C_OK : C_IDLE);
            SETF_F(F_SY_DOCK_ROWA(i), 1.0f);
        } else {
            SETS_F(F_SY_DOCK(i), "%s", "");
            SETS_F(F_SY_DOCK_ST(i), "%s", "");
            SETS_F(F_SY_DOT(i), "%s", C_HIDE);
            SETF_F(F_SY_DOCK_ROWA(i), 0.0f);   /* 空槽整行隐藏（alpha 连条轨一起隐） */
        }
    }

    for (int i = 0; i < 10; i++) {
        if (i < st->ntemps && i < FNOS_MAX_TEMPS) {
            const fnos_temp_t *t = &st->temps[i];
            SETS_F(F_SY_TNAME(i), "%s", t->n);
            SETS_F(F_SY_TVAL(i), "%.1f°C", (double)t->c);
            SETS_F(F_SY_TVALC(i), "%s", t->c >= KK_BAR_FULL ? C_DANGER :
                                       t->c >= KK_BAR_WARM ? C_WARN : C_OK);   /* 与刻度 60/85 同档 */
            SETF_F(F_SY_TBAR(i), t->c);
            SETF_F(F_SY_TROWA(i), 1.0f);
        } else {
            SETS_F(F_SY_TNAME(i), "%s", "");
            SETS_F(F_SY_TVAL(i), "%s", "");
            SETF_F(F_SY_TBAR(i), 0);
            SETF_F(F_SY_TROWA(i), 0.0f);   /* 空槽整行隐藏（alpha 连条轨一起隐） */
        }
    }

    for (int i = 0; i < 4; i++) {
        if (i < st->nalerts && i < FNOS_MAX_ALERTS) {
            bool crit = (strcmp(st->alerts[i].lv, "crit") == 0);
            SETS_F(F_SY_ALERT_LBL(i), "%s", st->alerts[i].m);
            SETS_F(F_SY_ALERT_LBLC(i), "%s", crit ? C_DANGER : C_WARN);
            SETS_F(F_SY_ALERT_DOT(i), "%s", crit ? C_DANGER : C_WARN);
        } else if (i == 0 && st->ever_ok && st->online) {
            SETS_F(F_SY_ALERT_LBL(i), "无告警");
            SETS_F(F_SY_ALERT_LBLC(i), "%s", C_OK);
            SETS_F(F_SY_ALERT_DOT(i), "%s", C_OK);
        } else {
            SETS_F(F_SY_ALERT_LBL(i), "%s", "");
            SETS_F(F_SY_ALERT_DOT(i), "%s", C_HIDE);
        }
    }

    const char *ip = fnos_net_ip();
    int8_t rssi = fnos_net_rssi();
    SETS(SyKvVal0, "%s", st->host[0] ? st->host : "--");
    SETS(SyKvVal1, "%s:%d", FNOS_HOST, FNOS_PORT);
    SETS(SyKvVal2, "%d ms (状态 %d)", st->http_ms, st->last_status);
    SETS(SyKvVal3, "%u / %u", (unsigned)st->ok_count, (unsigned)st->fail_count);
    SETS(SyKvVal4, "%s", st->last_err[0] ? st->last_err : "无");
    char a[24];
    fmt_age(a, sizeof a, data_age_s(st));
    SETS(SyKvVal5, "%s", a);
    if (rssi != 0) SETS(SyKvVal6, "%s  %d dBm", (ip && ip[0]) ? ip : "--", (int)rssi);
    else           SETS(SyKvVal6, "%s", (ip && ip[0]) ? ip : "未连接");
    SETS(SyKvVal7, "%u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    SETS(SyKvVal8, "%d", st->cpu.procs);
    SETS(SyKvVal9, "%.1f / %.1f GB",
         (double)(st->mem.swap_used_mb / 1024.0f), (double)(st->mem.swap_total_mb / 1024.0f));
    SETS(SyKvVal10, "%.1f GB", (double)(st->mem.total_mb / 1024.0f));
    {
        char up_[24];
        fmt_uptime(up_, sizeof up_, st->uptime_s);
        SETS(SyKvVal11, "%s", up_);
    }
}

/* ── 曲线：增量拉历史，隔点入图（2 s/点）─────────────────────────── */

static void drain_history(const fnos_status_t *st)
{
    fnos_sample_t smp[12];
    float hot = 0;
    for (int i = 0; i < st->ntemps && i < FNOS_MAX_TEMPS; i++)
        if (st->temps[i].c > hot) hot = st->temps[i].c;
    int64_t next = s_hist_seq;
    int n = fnos_data_hist_read(s_hist_seq, smp, 12, &next);
    static int div;
    for (int i = 0; i < n; i++) {
        if ((div++ & 1) != 0) continue;
        PUSH(OvCpuPoints,     smp[i].cpu);
        PUSH(OvMemPoints,     smp[i].mem);
        PUSH(NwDownTrendPoints,    smp[i].rx_kbs);
        PUSH(NwUpTrendPoints,      smp[i].tx_kbs);
        PUSH(NwMiniDownPoints,     smp[i].rx_kbs);
        PUSH(NwMiniUpPoints,       smp[i].tx_kbs);
        PUSH(OvTempTrendPoints,    hot);
    }
    if (n > 0) s_hist_seq = next;
}

/* ── 生命周期 ────────────────────────────────────────────────────── */

void FnosDashboardController_Init(fnos_dash_view_t *v, fnos_dash_store_t *s)
{
    s_v = v;
    s_s = s;
    s_hist_seq = 0;
}

void FnosDashboardController_Tick(void)
{
    if (!s_s) return;
    // 静态快照：fnos_status_t 有 2.5 KB，放栈上会打爆 main/LVGL 任务栈（v4 实测踩过）
    static fnos_status_t st;
    if (!fnos_data_get(&st)) {
        memset(&st, 0, sizeof st);   // 冷启动：全零 = warming/no-data，绝不拿旧值冒充
    }

    drain_history(&st);
    update_top(&st);
    update_bottom(&st);
    update_overview(&st);
    update_storage(&st);
    update_network(&st);
    update_system(&st);

    fnos_dash_binder_flush(s_v, s_s);   // 一次 tick 一次刷屏
}

/* ── 事件入口（生成的 view 转发；只做状态迁移）───────────────────── */

void FnosDashboardController_OnNavOverviewRequested(void) { fnos_ui_set_page(0); }
void FnosDashboardController_OnNavStorageRequested(void)  { fnos_ui_set_page(1); }
void FnosDashboardController_OnNavNetworkRequested(void)  { fnos_ui_set_page(2); }
void FnosDashboardController_OnNavSystemRequested(void)   { fnos_ui_set_page(3); }

void FnosDashboardController_OnOverviewNextRequested(void) { fnos_ui_set_page(fnos_ui_page() + 1); }
void FnosDashboardController_OnOverviewPrevRequested(void) { fnos_ui_set_page(fnos_ui_page() - 1); }
void FnosDashboardController_OnStorageNextRequested(void)  { fnos_ui_set_page(fnos_ui_page() + 1); }
void FnosDashboardController_OnStoragePrevRequested(void)  { fnos_ui_set_page(fnos_ui_page() - 1); }
void FnosDashboardController_OnNetworkNextRequested(void)  { fnos_ui_set_page(fnos_ui_page() + 1); }
void FnosDashboardController_OnNetworkPrevRequested(void)  { fnos_ui_set_page(fnos_ui_page() - 1); }
void FnosDashboardController_OnSystemNextRequested(void)   { fnos_ui_set_page(fnos_ui_page() + 1); }
void FnosDashboardController_OnSystemPrevRequested(void)   { fnos_ui_set_page(fnos_ui_page() - 1); }
