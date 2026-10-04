/* 由 tools/ui_gen.py 生成，请勿手改。源头：Source/FnosDashboard/bindings.json */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "kk_widgets.h"

typedef enum {
    FNOS_DASH_FIELD_HdHost = 0,
    FNOS_DASH_FIELD_HdEndpoint = 1,
    FNOS_DASH_FIELD_HdClock = 2,
    FNOS_DASH_FIELD_HdChipText = 3,
    FNOS_DASH_FIELD_HdChipColor = 4,
    FNOS_DASH_FIELD_HdSignalValue = 5,
    FNOS_DASH_FIELD_FtPoll = 6,
    FNOS_DASH_FIELD_FtAlertLbl = 7,
    FNOS_DASH_FIELD_FtAlertDotColor = 8,
    FNOS_DASH_FIELD_FtAlertLblColor = 9,
    FNOS_DASH_FIELD_OvVerdict = 10,
    FNOS_DASH_FIELD_OvVerdictSub = 11,
    FNOS_DASH_FIELD_OvReason = 12,
    FNOS_DASH_FIELD_OvVerdictColor = 13,
    FNOS_DASH_FIELD_OvVolName0 = 14,
    FNOS_DASH_FIELD_OvVolBar0Value = 15,
    FNOS_DASH_FIELD_OvVolPct0 = 16,
    FNOS_DASH_FIELD_OvVolName1 = 17,
    FNOS_DASH_FIELD_OvVolBar1Value = 18,
    FNOS_DASH_FIELD_OvVolPct1 = 19,
    FNOS_DASH_FIELD_OvVolName2 = 20,
    FNOS_DASH_FIELD_OvVolBar2Value = 21,
    FNOS_DASH_FIELD_OvVolPct2 = 22,
    FNOS_DASH_FIELD_OvVolName3 = 23,
    FNOS_DASH_FIELD_OvVolBar3Value = 24,
    FNOS_DASH_FIELD_OvVolPct3 = 25,
    FNOS_DASH_FIELD_OvVolName4 = 26,
    FNOS_DASH_FIELD_OvVolBar4Value = 27,
    FNOS_DASH_FIELD_OvVolPct4 = 28,
    FNOS_DASH_FIELD_OvVolName5 = 29,
    FNOS_DASH_FIELD_OvVolBar5Value = 30,
    FNOS_DASH_FIELD_OvVolPct5 = 31,
    FNOS_DASH_FIELD_OvVal0 = 32,
    FNOS_DASH_FIELD_OvSub0 = 33,
    FNOS_DASH_FIELD_OvMbar0Value = 34,
    FNOS_DASH_FIELD_OvVal1 = 35,
    FNOS_DASH_FIELD_OvSub1 = 36,
    FNOS_DASH_FIELD_OvMbar1Value = 37,
    FNOS_DASH_FIELD_OvVal2 = 38,
    FNOS_DASH_FIELD_OvSub2 = 39,
    FNOS_DASH_FIELD_OvMbar2Value = 40,
    FNOS_DASH_FIELD_OvVal3 = 41,
    FNOS_DASH_FIELD_OvSub3 = 42,
    FNOS_DASH_FIELD_OvCpuPoints = 43,
    FNOS_DASH_FIELD_OvMemPoints = 44,
    FNOS_DASH_FIELD_StHero = 45,
    FNOS_DASH_FIELD_StUsed = 46,
    FNOS_DASH_FIELD_StCnt = 47,
    FNOS_DASH_FIELD_StFree = 48,
    FNOS_DASH_FIELD_StSeg0X = 49,
    FNOS_DASH_FIELD_StSeg0Width = 50,
    FNOS_DASH_FIELD_StSeg0Color = 51,
    FNOS_DASH_FIELD_StSeg1X = 52,
    FNOS_DASH_FIELD_StSeg1Width = 53,
    FNOS_DASH_FIELD_StSeg1Color = 54,
    FNOS_DASH_FIELD_StSeg2X = 55,
    FNOS_DASH_FIELD_StSeg2Width = 56,
    FNOS_DASH_FIELD_StSeg2Color = 57,
    FNOS_DASH_FIELD_StSeg3X = 58,
    FNOS_DASH_FIELD_StSeg3Width = 59,
    FNOS_DASH_FIELD_StSeg3Color = 60,
    FNOS_DASH_FIELD_StSeg4X = 61,
    FNOS_DASH_FIELD_StSeg4Width = 62,
    FNOS_DASH_FIELD_StSeg4Color = 63,
    FNOS_DASH_FIELD_StSeg5X = 64,
    FNOS_DASH_FIELD_StSeg5Width = 65,
    FNOS_DASH_FIELD_StSeg5Color = 66,
    FNOS_DASH_FIELD_StVolName0 = 67,
    FNOS_DASH_FIELD_StVolFs0 = 68,
    FNOS_DASH_FIELD_StVolUse0 = 69,
    FNOS_DASH_FIELD_StVolPct0 = 70,
    FNOS_DASH_FIELD_StVolBar0Value = 71,
    FNOS_DASH_FIELD_StVolName1 = 72,
    FNOS_DASH_FIELD_StVolFs1 = 73,
    FNOS_DASH_FIELD_StVolUse1 = 74,
    FNOS_DASH_FIELD_StVolPct1 = 75,
    FNOS_DASH_FIELD_StVolBar1Value = 76,
    FNOS_DASH_FIELD_StVolName2 = 77,
    FNOS_DASH_FIELD_StVolFs2 = 78,
    FNOS_DASH_FIELD_StVolUse2 = 79,
    FNOS_DASH_FIELD_StVolPct2 = 80,
    FNOS_DASH_FIELD_StVolBar2Value = 81,
    FNOS_DASH_FIELD_StVolName3 = 82,
    FNOS_DASH_FIELD_StVolFs3 = 83,
    FNOS_DASH_FIELD_StVolUse3 = 84,
    FNOS_DASH_FIELD_StVolPct3 = 85,
    FNOS_DASH_FIELD_StVolBar3Value = 86,
    FNOS_DASH_FIELD_StVolName4 = 87,
    FNOS_DASH_FIELD_StVolFs4 = 88,
    FNOS_DASH_FIELD_StVolUse4 = 89,
    FNOS_DASH_FIELD_StVolPct4 = 90,
    FNOS_DASH_FIELD_StVolBar4Value = 91,
    FNOS_DASH_FIELD_StVolName5 = 92,
    FNOS_DASH_FIELD_StVolFs5 = 93,
    FNOS_DASH_FIELD_StVolUse5 = 94,
    FNOS_DASH_FIELD_StVolPct5 = 95,
    FNOS_DASH_FIELD_StVolBar5Value = 96,
    FNOS_DASH_FIELD_StRaidName0 = 97,
    FNOS_DASH_FIELD_StRaidLvl0 = 98,
    FNOS_DASH_FIELD_StRaidState0 = 99,
    FNOS_DASH_FIELD_StRaidState0Color = 100,
    FNOS_DASH_FIELD_StRaidName1 = 101,
    FNOS_DASH_FIELD_StRaidLvl1 = 102,
    FNOS_DASH_FIELD_StRaidState1 = 103,
    FNOS_DASH_FIELD_StRaidState1Color = 104,
    FNOS_DASH_FIELD_StRaidName2 = 105,
    FNOS_DASH_FIELD_StRaidLvl2 = 106,
    FNOS_DASH_FIELD_StRaidState2 = 107,
    FNOS_DASH_FIELD_StRaidState2Color = 108,
    FNOS_DASH_FIELD_StRaidName3 = 109,
    FNOS_DASH_FIELD_StRaidLvl3 = 110,
    FNOS_DASH_FIELD_StRaidState3 = 111,
    FNOS_DASH_FIELD_StRaidState3Color = 112,
    FNOS_DASH_FIELD_StIoName0 = 113,
    FNOS_DASH_FIELD_StIoVal0 = 114,
    FNOS_DASH_FIELD_StIoName1 = 115,
    FNOS_DASH_FIELD_StIoVal1 = 116,
    FNOS_DASH_FIELD_StIoName2 = 117,
    FNOS_DASH_FIELD_StIoVal2 = 118,
    FNOS_DASH_FIELD_StIoName3 = 119,
    FNOS_DASH_FIELD_StIoVal3 = 120,
    FNOS_DASH_FIELD_NwDownVal = 121,
    FNOS_DASH_FIELD_NwDownUnit = 122,
    FNOS_DASH_FIELD_NwDownSub = 123,
    FNOS_DASH_FIELD_NwMiniDownPoints = 124,
    FNOS_DASH_FIELD_NwUpVal = 125,
    FNOS_DASH_FIELD_NwUpUnit = 126,
    FNOS_DASH_FIELD_NwUpSub = 127,
    FNOS_DASH_FIELD_NwMiniUpPoints = 128,
    FNOS_DASH_FIELD_NwDownTrendPoints = 129,
    FNOS_DASH_FIELD_NwUpTrendPoints = 130,
    FNOS_DASH_FIELD_NwTrendTop = 131,
    FNOS_DASH_FIELD_NwAxis0 = 132,
    FNOS_DASH_FIELD_NwAxis1 = 133,
    FNOS_DASH_FIELD_NwAxis2 = 134,
    FNOS_DASH_FIELD_NwCtxVal0 = 135,
    FNOS_DASH_FIELD_NwCtxVal1 = 136,
    FNOS_DASH_FIELD_NwCtxVal2 = 137,
    FNOS_DASH_FIELD_NwCtxVal3 = 138,
    FNOS_DASH_FIELD_SyCnt = 139,
    FNOS_DASH_FIELD_SyDock0 = 140,
    FNOS_DASH_FIELD_SyDock1 = 141,
    FNOS_DASH_FIELD_SyDock2 = 142,
    FNOS_DASH_FIELD_SyDock3 = 143,
    FNOS_DASH_FIELD_SyDock4 = 144,
    FNOS_DASH_FIELD_SyDock5 = 145,
    FNOS_DASH_FIELD_SyDockState0 = 146,
    FNOS_DASH_FIELD_SyDockState1 = 147,
    FNOS_DASH_FIELD_SyDockState2 = 148,
    FNOS_DASH_FIELD_SyDockState3 = 149,
    FNOS_DASH_FIELD_SyDockState4 = 150,
    FNOS_DASH_FIELD_SyDockState5 = 151,
    FNOS_DASH_FIELD_SyDot0Color = 152,
    FNOS_DASH_FIELD_SyDot1Color = 153,
    FNOS_DASH_FIELD_SyDot2Color = 154,
    FNOS_DASH_FIELD_SyDot3Color = 155,
    FNOS_DASH_FIELD_SyDot4Color = 156,
    FNOS_DASH_FIELD_SyDot5Color = 157,
    FNOS_DASH_FIELD_SyDockState0Color = 158,
    FNOS_DASH_FIELD_SyDockState1Color = 159,
    FNOS_DASH_FIELD_SyDockState2Color = 160,
    FNOS_DASH_FIELD_SyDockState3Color = 161,
    FNOS_DASH_FIELD_SyDockState4Color = 162,
    FNOS_DASH_FIELD_SyDockState5Color = 163,
    FNOS_DASH_FIELD_SyAlertLbl0 = 164,
    FNOS_DASH_FIELD_SyAlertLbl1 = 165,
    FNOS_DASH_FIELD_SyAlertLbl2 = 166,
    FNOS_DASH_FIELD_SyAlertLbl0Color = 167,
    FNOS_DASH_FIELD_SyAlertLbl1Color = 168,
    FNOS_DASH_FIELD_SyAlertLbl2Color = 169,
    FNOS_DASH_FIELD_SyAlertDot0Color = 170,
    FNOS_DASH_FIELD_SyAlertDot1Color = 171,
    FNOS_DASH_FIELD_SyAlertDot2Color = 172,
    FNOS_DASH_FIELD_SyTName0 = 173,
    FNOS_DASH_FIELD_SyTName1 = 174,
    FNOS_DASH_FIELD_SyTName2 = 175,
    FNOS_DASH_FIELD_SyTName3 = 176,
    FNOS_DASH_FIELD_SyTName4 = 177,
    FNOS_DASH_FIELD_SyTName5 = 178,
    FNOS_DASH_FIELD_SyTName6 = 179,
    FNOS_DASH_FIELD_SyTVal0 = 180,
    FNOS_DASH_FIELD_SyTVal1 = 181,
    FNOS_DASH_FIELD_SyTVal2 = 182,
    FNOS_DASH_FIELD_SyTVal3 = 183,
    FNOS_DASH_FIELD_SyTVal4 = 184,
    FNOS_DASH_FIELD_SyTVal5 = 185,
    FNOS_DASH_FIELD_SyTVal6 = 186,
    FNOS_DASH_FIELD_SyTVal0Color = 187,
    FNOS_DASH_FIELD_SyTVal1Color = 188,
    FNOS_DASH_FIELD_SyTVal2Color = 189,
    FNOS_DASH_FIELD_SyTVal3Color = 190,
    FNOS_DASH_FIELD_SyTVal4Color = 191,
    FNOS_DASH_FIELD_SyTVal5Color = 192,
    FNOS_DASH_FIELD_SyTVal6Color = 193,
    FNOS_DASH_FIELD_SyTBar0Value = 194,
    FNOS_DASH_FIELD_SyTBar1Value = 195,
    FNOS_DASH_FIELD_SyTBar2Value = 196,
    FNOS_DASH_FIELD_SyTBar3Value = 197,
    FNOS_DASH_FIELD_SyTBar4Value = 198,
    FNOS_DASH_FIELD_SyTBar5Value = 199,
    FNOS_DASH_FIELD_SyTBar6Value = 200,
    FNOS_DASH_FIELD_SyKvVal0 = 201,
    FNOS_DASH_FIELD_SyKvVal1 = 202,
    FNOS_DASH_FIELD_SyKvVal2 = 203,
    FNOS_DASH_FIELD_SyKvVal3 = 204,
    FNOS_DASH_FIELD_SyKvVal4 = 205,
    FNOS_DASH_FIELD_SyKvVal5 = 206,
    FNOS_DASH_FIELD_SyKvVal6 = 207,
    FNOS_DASH_FIELD_SyKvVal7 = 208,
    FNOS_DASH_FIELD_OvTempTrendPoints = 209,
    FNOS_DASH_FIELD_StZfsArc = 210,
    FNOS_DASH_FIELD_StZfsHit = 211,
    FNOS_DASH_FIELD_StZfsBarValue = 212,
    FNOS_DASH_FIELD_StRaidBar0Value = 213,
    FNOS_DASH_FIELD_StRaidBar1Value = 214,
    FNOS_DASH_FIELD_StRaidBar2Value = 215,
    FNOS_DASH_FIELD_StRaidBar3Value = 216,
    FNOS_DASH_FIELD_NwTotalVal = 217,
    FNOS_DASH_FIELD_SyDock6 = 218,
    FNOS_DASH_FIELD_SyDockState6 = 219,
    FNOS_DASH_FIELD_SyDockState6Color = 220,
    FNOS_DASH_FIELD_SyDot6Color = 221,
    FNOS_DASH_FIELD_SyDock7 = 222,
    FNOS_DASH_FIELD_SyDockState7 = 223,
    FNOS_DASH_FIELD_SyDockState7Color = 224,
    FNOS_DASH_FIELD_SyDot7Color = 225,
    FNOS_DASH_FIELD_SyAlertLbl3 = 226,
    FNOS_DASH_FIELD_SyAlertLbl3Color = 227,
    FNOS_DASH_FIELD_SyAlertDot3Color = 228,
    FNOS_DASH_FIELD_SyTName7 = 229,
    FNOS_DASH_FIELD_SyTVal7 = 230,
    FNOS_DASH_FIELD_SyTVal7Color = 231,
    FNOS_DASH_FIELD_SyTBar7Value = 232,
    FNOS_DASH_FIELD_SyTName8 = 233,
    FNOS_DASH_FIELD_SyTVal8 = 234,
    FNOS_DASH_FIELD_SyTVal8Color = 235,
    FNOS_DASH_FIELD_SyTBar8Value = 236,
    FNOS_DASH_FIELD_SyTName9 = 237,
    FNOS_DASH_FIELD_SyTVal9 = 238,
    FNOS_DASH_FIELD_SyTVal9Color = 239,
    FNOS_DASH_FIELD_SyTBar9Value = 240,
    FNOS_DASH_FIELD_SyKvVal8 = 241,
    FNOS_DASH_FIELD_SyKvVal9 = 242,
    FNOS_DASH_FIELD_SyKvVal10 = 243,
    FNOS_DASH_FIELD_SyKvVal11 = 244,
    FNOS_DASH_FIELD_OvLegendCpuLbl = 245,
    FNOS_DASH_FIELD_OvLegendMemLbl = 246,
    FNOS_DASH_FIELD_OvLegendTempLbl = 247,
    FNOS_DASH_FIELD_OvHealthAccentColor = 248,
    FNOS_DASH_FIELD_OvVal0Color = 249,
    FNOS_DASH_FIELD_OvVal1Color = 250,
    FNOS_DASH_FIELD_OvVal2Color = 251,
    FNOS_DASH_FIELD_OvVal3Color = 252,
    FNOS_DASH_FIELD_StHeroColor = 253,
    FNOS_DASH_FIELD_NwDownValColor = 254,
    FNOS_DASH_FIELD_NwUpValColor = 255,
    FNOS_DASH_FIELD_OvTrendTop = 256,
    FNOS_DASH_FIELD_SyDockRow0Alpha = 257,
    FNOS_DASH_FIELD_SyDockRow1Alpha = 258,
    FNOS_DASH_FIELD_SyDockRow2Alpha = 259,
    FNOS_DASH_FIELD_SyDockRow3Alpha = 260,
    FNOS_DASH_FIELD_SyDockRow4Alpha = 261,
    FNOS_DASH_FIELD_SyDockRow5Alpha = 262,
    FNOS_DASH_FIELD_SyDockRow6Alpha = 263,
    FNOS_DASH_FIELD_SyDockRow7Alpha = 264,
    FNOS_DASH_FIELD_SyTempRow0Alpha = 265,
    FNOS_DASH_FIELD_SyTempRow1Alpha = 266,
    FNOS_DASH_FIELD_SyTempRow2Alpha = 267,
    FNOS_DASH_FIELD_SyTempRow3Alpha = 268,
    FNOS_DASH_FIELD_SyTempRow4Alpha = 269,
    FNOS_DASH_FIELD_SyTempRow5Alpha = 270,
    FNOS_DASH_FIELD_SyTempRow6Alpha = 271,
    FNOS_DASH_FIELD_SyTempRow7Alpha = 272,
    FNOS_DASH_FIELD_SyTempRow8Alpha = 273,
    FNOS_DASH_FIELD_SyTempRow9Alpha = 274,
    FNOS_DASH_FIELD_OvVolUse0 = 275,
    FNOS_DASH_FIELD_OvVolUse1 = 276,
    FNOS_DASH_FIELD_OvVolUse2 = 277,
    FNOS_DASH_FIELD_OvVolUse3 = 278,
    FNOS_DASH_FIELD_OvVolUse4 = 279,
    FNOS_DASH_FIELD_OvVolUse5 = 280,
    FNOS_DASH_FIELD_OvPeakCpu = 281,
    FNOS_DASH_FIELD_OvPeakMem = 282,
    FNOS_DASH_FIELD_OvPeakTemp = 283,
    FNOS_DASH_FIELD_COUNT
} fnos_dash_field_id_t;

#define FNOS_DASH_SERIES_OVCPUPOINTS_CAP 90
#define FNOS_DASH_SERIES_OVMEMPOINTS_CAP 90
#define FNOS_DASH_SERIES_NWMINIDOWNPOINTS_CAP 90
#define FNOS_DASH_SERIES_NWMINIUPPOINTS_CAP 90
#define FNOS_DASH_SERIES_NWDOWNTRENDPOINTS_CAP 90
#define FNOS_DASH_SERIES_NWUPTRENDPOINTS_CAP 90
#define FNOS_DASH_SERIES_OVTEMPTRENDPOINTS_CAP 90
#define FNOS_DASH_DIRTY_WORDS ((FNOS_DASH_FIELD_COUNT + 31) / 32)

typedef struct fnos_dash_store {
    char hd_host[32];
    char hd_endpoint[48];
    char hd_clock[24];
    char hd_chip_text[80];
    char hd_chip_color[10];
    int32_t hd_signal_value;
    char ft_poll[96];
    char ft_alert_lbl[80];
    char ft_alert_dot_color[10];
    char ft_alert_lbl_color[10];
    char ov_verdict[48];
    char ov_verdict_sub[48];
    char ov_reason[80];
    char ov_verdict_color[10];
    char ov_vol_name0[32];
    float ov_vol_bar0_value;
    char ov_vol_pct0[24];
    char ov_vol_name1[32];
    float ov_vol_bar1_value;
    char ov_vol_pct1[24];
    char ov_vol_name2[32];
    float ov_vol_bar2_value;
    char ov_vol_pct2[24];
    char ov_vol_name3[32];
    float ov_vol_bar3_value;
    char ov_vol_pct3[24];
    char ov_vol_name4[32];
    float ov_vol_bar4_value;
    char ov_vol_pct4[24];
    char ov_vol_name5[32];
    float ov_vol_bar5_value;
    char ov_vol_pct5[24];
    char ov_val0[24];
    char ov_sub0[48];
    float ov_mbar0_value;
    char ov_val1[24];
    char ov_sub1[48];
    float ov_mbar1_value;
    char ov_val2[24];
    char ov_sub2[48];
    float ov_mbar2_value;
    char ov_val3[24];
    char ov_sub3[48];
    kk_series_t ov_cpu_points;
    kk_series_t ov_mem_points;
    char st_hero[24];
    char st_used[48];
    char st_cnt[24];
    char st_free[24];
    int32_t st_seg0_x;
    int32_t st_seg0_width;
    char st_seg0_color[10];
    int32_t st_seg1_x;
    int32_t st_seg1_width;
    char st_seg1_color[10];
    int32_t st_seg2_x;
    int32_t st_seg2_width;
    char st_seg2_color[10];
    int32_t st_seg3_x;
    int32_t st_seg3_width;
    char st_seg3_color[10];
    int32_t st_seg4_x;
    int32_t st_seg4_width;
    char st_seg4_color[10];
    int32_t st_seg5_x;
    int32_t st_seg5_width;
    char st_seg5_color[10];
    char st_vol_name0[32];
    char st_vol_fs0[48];
    char st_vol_use0[48];
    char st_vol_pct0[24];
    float st_vol_bar0_value;
    char st_vol_name1[32];
    char st_vol_fs1[48];
    char st_vol_use1[48];
    char st_vol_pct1[24];
    float st_vol_bar1_value;
    char st_vol_name2[32];
    char st_vol_fs2[48];
    char st_vol_use2[48];
    char st_vol_pct2[24];
    float st_vol_bar2_value;
    char st_vol_name3[32];
    char st_vol_fs3[48];
    char st_vol_use3[48];
    char st_vol_pct3[24];
    float st_vol_bar3_value;
    char st_vol_name4[32];
    char st_vol_fs4[48];
    char st_vol_use4[48];
    char st_vol_pct4[24];
    float st_vol_bar4_value;
    char st_vol_name5[32];
    char st_vol_fs5[48];
    char st_vol_use5[48];
    char st_vol_pct5[24];
    float st_vol_bar5_value;
    char st_raid_name0[32];
    char st_raid_lvl0[48];
    char st_raid_state0[48];
    char st_raid_state0_color[10];
    char st_raid_name1[32];
    char st_raid_lvl1[48];
    char st_raid_state1[48];
    char st_raid_state1_color[10];
    char st_raid_name2[32];
    char st_raid_lvl2[48];
    char st_raid_state2[48];
    char st_raid_state2_color[10];
    char st_raid_name3[32];
    char st_raid_lvl3[48];
    char st_raid_state3[48];
    char st_raid_state3_color[10];
    char st_io_name0[32];
    char st_io_val0[24];
    char st_io_name1[32];
    char st_io_val1[24];
    char st_io_name2[32];
    char st_io_val2[24];
    char st_io_name3[32];
    char st_io_val3[24];
    char nw_down_val[24];
    char nw_down_unit[48];
    char nw_down_sub[48];
    kk_series_t nw_mini_down_points;
    char nw_up_val[24];
    char nw_up_unit[48];
    char nw_up_sub[48];
    kk_series_t nw_mini_up_points;
    kk_series_t nw_down_trend_points;
    kk_series_t nw_up_trend_points;
    float nw_trend_top;
    char nw_axis0[24];
    char nw_axis1[24];
    char nw_axis2[24];
    char nw_ctx_val0[24];
    char nw_ctx_val1[24];
    char nw_ctx_val2[24];
    char nw_ctx_val3[24];
    char sy_cnt[40];
    char sy_dock0[32];
    char sy_dock1[32];
    char sy_dock2[32];
    char sy_dock3[32];
    char sy_dock4[32];
    char sy_dock5[32];
    char sy_dock_state0[48];
    char sy_dock_state1[48];
    char sy_dock_state2[48];
    char sy_dock_state3[48];
    char sy_dock_state4[48];
    char sy_dock_state5[48];
    char sy_dot0_color[10];
    char sy_dot1_color[10];
    char sy_dot2_color[10];
    char sy_dot3_color[10];
    char sy_dot4_color[10];
    char sy_dot5_color[10];
    char sy_dock_state0_color[10];
    char sy_dock_state1_color[10];
    char sy_dock_state2_color[10];
    char sy_dock_state3_color[10];
    char sy_dock_state4_color[10];
    char sy_dock_state5_color[10];
    char sy_alert_lbl0[80];
    char sy_alert_lbl1[80];
    char sy_alert_lbl2[80];
    char sy_alert_lbl0_color[10];
    char sy_alert_lbl1_color[10];
    char sy_alert_lbl2_color[10];
    char sy_alert_dot0_color[10];
    char sy_alert_dot1_color[10];
    char sy_alert_dot2_color[10];
    char sy_tname0[48];
    char sy_tname1[48];
    char sy_tname2[48];
    char sy_tname3[48];
    char sy_tname4[48];
    char sy_tname5[48];
    char sy_tname6[48];
    char sy_tval0[24];
    char sy_tval1[24];
    char sy_tval2[24];
    char sy_tval3[24];
    char sy_tval4[24];
    char sy_tval5[24];
    char sy_tval6[24];
    char sy_tval0_color[10];
    char sy_tval1_color[10];
    char sy_tval2_color[10];
    char sy_tval3_color[10];
    char sy_tval4_color[10];
    char sy_tval5_color[10];
    char sy_tval6_color[10];
    float sy_tbar0_value;
    float sy_tbar1_value;
    float sy_tbar2_value;
    float sy_tbar3_value;
    float sy_tbar4_value;
    float sy_tbar5_value;
    float sy_tbar6_value;
    char sy_kv_val0[32];
    char sy_kv_val1[48];
    char sy_kv_val2[40];
    char sy_kv_val3[24];
    char sy_kv_val4[80];
    char sy_kv_val5[24];
    char sy_kv_val6[48];
    char sy_kv_val7[24];
    kk_series_t ov_temp_trend_points;
    char st_zfs_arc[24];
    char st_zfs_hit[24];
    float st_zfs_bar_value;
    float st_raid_bar0_value;
    float st_raid_bar1_value;
    float st_raid_bar2_value;
    float st_raid_bar3_value;
    char nw_total_val[24];
    char sy_dock6[32];
    char sy_dock_state6[48];
    char sy_dock_state6_color[10];
    char sy_dot6_color[10];
    char sy_dock7[32];
    char sy_dock_state7[48];
    char sy_dock_state7_color[10];
    char sy_dot7_color[10];
    char sy_alert_lbl3[80];
    char sy_alert_lbl3_color[10];
    char sy_alert_dot3_color[10];
    char sy_tname7[48];
    char sy_tval7[24];
    char sy_tval7_color[10];
    float sy_tbar7_value;
    char sy_tname8[48];
    char sy_tval8[24];
    char sy_tval8_color[10];
    float sy_tbar8_value;
    char sy_tname9[48];
    char sy_tval9[24];
    char sy_tval9_color[10];
    float sy_tbar9_value;
    char sy_kv_val8[32];
    char sy_kv_val9[32];
    char sy_kv_val10[32];
    char sy_kv_val11[32];
    char ov_legend_cpu_lbl[24];
    char ov_legend_mem_lbl[24];
    char ov_legend_temp_lbl[24];
    char ov_health_accent_color[10];
    char ov_val0_color[10];
    char ov_val1_color[10];
    char ov_val2_color[10];
    char ov_val3_color[10];
    char st_hero_color[10];
    char nw_down_val_color[10];
    char nw_up_val_color[10];
    float ov_trend_top;
    float sy_dock_row0_alpha;
    float sy_dock_row1_alpha;
    float sy_dock_row2_alpha;
    float sy_dock_row3_alpha;
    float sy_dock_row4_alpha;
    float sy_dock_row5_alpha;
    float sy_dock_row6_alpha;
    float sy_dock_row7_alpha;
    float sy_temp_row0_alpha;
    float sy_temp_row1_alpha;
    float sy_temp_row2_alpha;
    float sy_temp_row3_alpha;
    float sy_temp_row4_alpha;
    float sy_temp_row5_alpha;
    float sy_temp_row6_alpha;
    float sy_temp_row7_alpha;
    float sy_temp_row8_alpha;
    float sy_temp_row9_alpha;
    char ov_vol_use0[48];
    char ov_vol_use1[48];
    char ov_vol_use2[48];
    char ov_vol_use3[48];
    char ov_vol_use4[48];
    char ov_vol_use5[48];
    char ov_peak_cpu[16];
    char ov_peak_mem[16];
    char ov_peak_temp[16];
    uint32_t dirty[FNOS_DASH_DIRTY_WORDS];
} fnos_dash_store_t;

static inline bool fnos_dash_field_dirty(const fnos_dash_store_t *s, unsigned f)
{
    return (s->dirty[f >> 5] >> (f & 31)) & 1u;
}

void fnos_dash_store_init(fnos_dash_store_t *s);
bool fnos_dash_store_update_str(fnos_dash_store_t *s, fnos_dash_field_id_t f, const char *v);
bool fnos_dash_store_update_int(fnos_dash_store_t *s, fnos_dash_field_id_t f, int32_t v);
bool fnos_dash_store_update_float(fnos_dash_store_t *s, fnos_dash_field_id_t f, float v);
bool fnos_dash_store_update_bool(fnos_dash_store_t *s, fnos_dash_field_id_t f, bool v);
bool fnos_dash_store_series_push(fnos_dash_store_t *s, fnos_dash_field_id_t f, float v);
