/* 由 tools/ui_gen.py 生成，请勿手改。 */
#include "fnos_dash_store.generated.h"
#include <stdio.h>
#include <string.h>

static float fnos_dash_ov_cpu_points_buf[FNOS_DASH_SERIES_OVCPUPOINTS_CAP];
static float fnos_dash_ov_mem_points_buf[FNOS_DASH_SERIES_OVMEMPOINTS_CAP];
static float fnos_dash_nw_mini_down_points_buf[FNOS_DASH_SERIES_NWMINIDOWNPOINTS_CAP];
static float fnos_dash_nw_mini_up_points_buf[FNOS_DASH_SERIES_NWMINIUPPOINTS_CAP];
static float fnos_dash_nw_down_trend_points_buf[FNOS_DASH_SERIES_NWDOWNTRENDPOINTS_CAP];
static float fnos_dash_nw_up_trend_points_buf[FNOS_DASH_SERIES_NWUPTRENDPOINTS_CAP];
static float fnos_dash_ov_temp_trend_points_buf[FNOS_DASH_SERIES_OVTEMPTRENDPOINTS_CAP];

void fnos_dash_store_init(fnos_dash_store_t *s)
{
    memset(s, 0, sizeof(*s));
    snprintf(s->hd_host, sizeof(s->hd_host), "%s", "NAS");
    snprintf(s->hd_endpoint, sizeof(s->hd_endpoint), "%s", "");
    snprintf(s->hd_clock, sizeof(s->hd_clock), "%s", "--:--");
    snprintf(s->hd_chip_text, sizeof(s->hd_chip_text), "%s", "等待数据");
    snprintf(s->hd_chip_color, sizeof(s->hd_chip_color), "%s", "#F2B01E");
    s->hd_signal_value = 0;
    snprintf(s->ft_poll, sizeof(s->ft_poll), "%s", "");
    snprintf(s->ft_alert_lbl, sizeof(s->ft_alert_lbl), "%s", "无告警");
    snprintf(s->ft_alert_dot_color, sizeof(s->ft_alert_dot_color), "%s", "#2FBF71");
    snprintf(s->ft_alert_lbl_color, sizeof(s->ft_alert_lbl_color), "%s", "#2FBF71");
    snprintf(s->ov_verdict, sizeof(s->ov_verdict), "%s", "等待");
    snprintf(s->ov_verdict_sub, sizeof(s->ov_verdict_sub), "%s", "等待首个数据帧");
    snprintf(s->ov_reason, sizeof(s->ov_reason), "%s", "");
    snprintf(s->ov_verdict_color, sizeof(s->ov_verdict_color), "%s", "#F2B01E");
    snprintf(s->ov_vol_name0, sizeof(s->ov_vol_name0), "%s", "");
    s->ov_vol_bar0_value = 0.0f;
    snprintf(s->ov_vol_pct0, sizeof(s->ov_vol_pct0), "%s", "");
    snprintf(s->ov_vol_name1, sizeof(s->ov_vol_name1), "%s", "");
    s->ov_vol_bar1_value = 0.0f;
    snprintf(s->ov_vol_pct1, sizeof(s->ov_vol_pct1), "%s", "");
    snprintf(s->ov_vol_name2, sizeof(s->ov_vol_name2), "%s", "");
    s->ov_vol_bar2_value = 0.0f;
    snprintf(s->ov_vol_pct2, sizeof(s->ov_vol_pct2), "%s", "");
    snprintf(s->ov_vol_name3, sizeof(s->ov_vol_name3), "%s", "");
    s->ov_vol_bar3_value = 0.0f;
    snprintf(s->ov_vol_pct3, sizeof(s->ov_vol_pct3), "%s", "");
    snprintf(s->ov_vol_name4, sizeof(s->ov_vol_name4), "%s", "");
    s->ov_vol_bar4_value = 0.0f;
    snprintf(s->ov_vol_pct4, sizeof(s->ov_vol_pct4), "%s", "");
    snprintf(s->ov_vol_name5, sizeof(s->ov_vol_name5), "%s", "");
    s->ov_vol_bar5_value = 0.0f;
    snprintf(s->ov_vol_pct5, sizeof(s->ov_vol_pct5), "%s", "");
    snprintf(s->ov_val0, sizeof(s->ov_val0), "%s", "--");
    snprintf(s->ov_sub0, sizeof(s->ov_sub0), "%s", "");
    s->ov_mbar0_value = 0.0f;
    snprintf(s->ov_val1, sizeof(s->ov_val1), "%s", "--");
    snprintf(s->ov_sub1, sizeof(s->ov_sub1), "%s", "");
    s->ov_mbar1_value = 0.0f;
    snprintf(s->ov_val2, sizeof(s->ov_val2), "%s", "--");
    snprintf(s->ov_sub2, sizeof(s->ov_sub2), "%s", "");
    s->ov_mbar2_value = 0.0f;
    snprintf(s->ov_val3, sizeof(s->ov_val3), "%s", "--");
    snprintf(s->ov_sub3, sizeof(s->ov_sub3), "%s", "");
    kk_series_init(&s->ov_cpu_points, fnos_dash_ov_cpu_points_buf, FNOS_DASH_SERIES_OVCPUPOINTS_CAP);
    kk_series_init(&s->ov_mem_points, fnos_dash_ov_mem_points_buf, FNOS_DASH_SERIES_OVMEMPOINTS_CAP);
    snprintf(s->st_hero, sizeof(s->st_hero), "%s", "--");
    snprintf(s->st_used, sizeof(s->st_used), "%s", "TB / 总量");
    snprintf(s->st_cnt, sizeof(s->st_cnt), "%s", "");
    snprintf(s->st_free, sizeof(s->st_free), "%s", "");
    s->st_seg0_x = 20;
    s->st_seg0_width = 0;
    snprintf(s->st_seg0_color, sizeof(s->st_seg0_color), "%s", "#4EC9E8");
    s->st_seg1_x = 23;
    s->st_seg1_width = 0;
    snprintf(s->st_seg1_color, sizeof(s->st_seg1_color), "%s", "#4EC9E8");
    s->st_seg2_x = 26;
    s->st_seg2_width = 0;
    snprintf(s->st_seg2_color, sizeof(s->st_seg2_color), "%s", "#4EC9E8");
    s->st_seg3_x = 29;
    s->st_seg3_width = 0;
    snprintf(s->st_seg3_color, sizeof(s->st_seg3_color), "%s", "#4EC9E8");
    s->st_seg4_x = 32;
    s->st_seg4_width = 0;
    snprintf(s->st_seg4_color, sizeof(s->st_seg4_color), "%s", "#4EC9E8");
    s->st_seg5_x = 35;
    s->st_seg5_width = 0;
    snprintf(s->st_seg5_color, sizeof(s->st_seg5_color), "%s", "#4EC9E8");
    snprintf(s->st_vol_name0, sizeof(s->st_vol_name0), "%s", "");
    snprintf(s->st_vol_fs0, sizeof(s->st_vol_fs0), "%s", "");
    snprintf(s->st_vol_use0, sizeof(s->st_vol_use0), "%s", "");
    snprintf(s->st_vol_pct0, sizeof(s->st_vol_pct0), "%s", "");
    s->st_vol_bar0_value = 0.0f;
    snprintf(s->st_vol_name1, sizeof(s->st_vol_name1), "%s", "");
    snprintf(s->st_vol_fs1, sizeof(s->st_vol_fs1), "%s", "");
    snprintf(s->st_vol_use1, sizeof(s->st_vol_use1), "%s", "");
    snprintf(s->st_vol_pct1, sizeof(s->st_vol_pct1), "%s", "");
    s->st_vol_bar1_value = 0.0f;
    snprintf(s->st_vol_name2, sizeof(s->st_vol_name2), "%s", "");
    snprintf(s->st_vol_fs2, sizeof(s->st_vol_fs2), "%s", "");
    snprintf(s->st_vol_use2, sizeof(s->st_vol_use2), "%s", "");
    snprintf(s->st_vol_pct2, sizeof(s->st_vol_pct2), "%s", "");
    s->st_vol_bar2_value = 0.0f;
    snprintf(s->st_vol_name3, sizeof(s->st_vol_name3), "%s", "");
    snprintf(s->st_vol_fs3, sizeof(s->st_vol_fs3), "%s", "");
    snprintf(s->st_vol_use3, sizeof(s->st_vol_use3), "%s", "");
    snprintf(s->st_vol_pct3, sizeof(s->st_vol_pct3), "%s", "");
    s->st_vol_bar3_value = 0.0f;
    snprintf(s->st_vol_name4, sizeof(s->st_vol_name4), "%s", "");
    snprintf(s->st_vol_fs4, sizeof(s->st_vol_fs4), "%s", "");
    snprintf(s->st_vol_use4, sizeof(s->st_vol_use4), "%s", "");
    snprintf(s->st_vol_pct4, sizeof(s->st_vol_pct4), "%s", "");
    s->st_vol_bar4_value = 0.0f;
    snprintf(s->st_vol_name5, sizeof(s->st_vol_name5), "%s", "");
    snprintf(s->st_vol_fs5, sizeof(s->st_vol_fs5), "%s", "");
    snprintf(s->st_vol_use5, sizeof(s->st_vol_use5), "%s", "");
    snprintf(s->st_vol_pct5, sizeof(s->st_vol_pct5), "%s", "");
    s->st_vol_bar5_value = 0.0f;
    snprintf(s->st_raid_name0, sizeof(s->st_raid_name0), "%s", "");
    snprintf(s->st_raid_lvl0, sizeof(s->st_raid_lvl0), "%s", "");
    snprintf(s->st_raid_state0, sizeof(s->st_raid_state0), "%s", "");
    snprintf(s->st_raid_state0_color, sizeof(s->st_raid_state0_color), "%s", "#2FBF71");
    snprintf(s->st_raid_name1, sizeof(s->st_raid_name1), "%s", "");
    snprintf(s->st_raid_lvl1, sizeof(s->st_raid_lvl1), "%s", "");
    snprintf(s->st_raid_state1, sizeof(s->st_raid_state1), "%s", "");
    snprintf(s->st_raid_state1_color, sizeof(s->st_raid_state1_color), "%s", "#2FBF71");
    snprintf(s->st_raid_name2, sizeof(s->st_raid_name2), "%s", "");
    snprintf(s->st_raid_lvl2, sizeof(s->st_raid_lvl2), "%s", "");
    snprintf(s->st_raid_state2, sizeof(s->st_raid_state2), "%s", "");
    snprintf(s->st_raid_state2_color, sizeof(s->st_raid_state2_color), "%s", "#2FBF71");
    snprintf(s->st_raid_name3, sizeof(s->st_raid_name3), "%s", "");
    snprintf(s->st_raid_lvl3, sizeof(s->st_raid_lvl3), "%s", "");
    snprintf(s->st_raid_state3, sizeof(s->st_raid_state3), "%s", "");
    snprintf(s->st_raid_state3_color, sizeof(s->st_raid_state3_color), "%s", "#2FBF71");
    snprintf(s->st_io_name0, sizeof(s->st_io_name0), "%s", "");
    snprintf(s->st_io_val0, sizeof(s->st_io_val0), "%s", "");
    snprintf(s->st_io_name1, sizeof(s->st_io_name1), "%s", "");
    snprintf(s->st_io_val1, sizeof(s->st_io_val1), "%s", "");
    snprintf(s->st_io_name2, sizeof(s->st_io_name2), "%s", "");
    snprintf(s->st_io_val2, sizeof(s->st_io_val2), "%s", "");
    snprintf(s->st_io_name3, sizeof(s->st_io_name3), "%s", "");
    snprintf(s->st_io_val3, sizeof(s->st_io_val3), "%s", "");
    snprintf(s->nw_down_val, sizeof(s->nw_down_val), "%s", "--");
    snprintf(s->nw_down_unit, sizeof(s->nw_down_unit), "%s", "KB/s");
    snprintf(s->nw_down_sub, sizeof(s->nw_down_sub), "%s", "");
    kk_series_init(&s->nw_mini_down_points, fnos_dash_nw_mini_down_points_buf, FNOS_DASH_SERIES_NWMINIDOWNPOINTS_CAP);
    snprintf(s->nw_up_val, sizeof(s->nw_up_val), "%s", "--");
    snprintf(s->nw_up_unit, sizeof(s->nw_up_unit), "%s", "KB/s");
    snprintf(s->nw_up_sub, sizeof(s->nw_up_sub), "%s", "");
    kk_series_init(&s->nw_mini_up_points, fnos_dash_nw_mini_up_points_buf, FNOS_DASH_SERIES_NWMINIUPPOINTS_CAP);
    kk_series_init(&s->nw_down_trend_points, fnos_dash_nw_down_trend_points_buf, FNOS_DASH_SERIES_NWDOWNTRENDPOINTS_CAP);
    kk_series_init(&s->nw_up_trend_points, fnos_dash_nw_up_trend_points_buf, FNOS_DASH_SERIES_NWUPTRENDPOINTS_CAP);
    s->nw_trend_top = 0.0f;
    snprintf(s->nw_axis0, sizeof(s->nw_axis0), "%s", "");
    snprintf(s->nw_axis1, sizeof(s->nw_axis1), "%s", "");
    snprintf(s->nw_axis2, sizeof(s->nw_axis2), "%s", "");
    snprintf(s->nw_ctx_val0, sizeof(s->nw_ctx_val0), "%s", "--");
    snprintf(s->nw_ctx_val1, sizeof(s->nw_ctx_val1), "%s", "--");
    snprintf(s->nw_ctx_val2, sizeof(s->nw_ctx_val2), "%s", "--");
    snprintf(s->nw_ctx_val3, sizeof(s->nw_ctx_val3), "%s", "--");
    snprintf(s->sy_cnt, sizeof(s->sy_cnt), "%s", "");
    snprintf(s->sy_dock0, sizeof(s->sy_dock0), "%s", "");
    snprintf(s->sy_dock1, sizeof(s->sy_dock1), "%s", "");
    snprintf(s->sy_dock2, sizeof(s->sy_dock2), "%s", "");
    snprintf(s->sy_dock3, sizeof(s->sy_dock3), "%s", "");
    snprintf(s->sy_dock4, sizeof(s->sy_dock4), "%s", "");
    snprintf(s->sy_dock5, sizeof(s->sy_dock5), "%s", "");
    snprintf(s->sy_dock_state0, sizeof(s->sy_dock_state0), "%s", "");
    snprintf(s->sy_dock_state1, sizeof(s->sy_dock_state1), "%s", "");
    snprintf(s->sy_dock_state2, sizeof(s->sy_dock_state2), "%s", "");
    snprintf(s->sy_dock_state3, sizeof(s->sy_dock_state3), "%s", "");
    snprintf(s->sy_dock_state4, sizeof(s->sy_dock_state4), "%s", "");
    snprintf(s->sy_dock_state5, sizeof(s->sy_dock_state5), "%s", "");
    snprintf(s->sy_dot0_color, sizeof(s->sy_dot0_color), "%s", "");
    snprintf(s->sy_dot1_color, sizeof(s->sy_dot1_color), "%s", "");
    snprintf(s->sy_dot2_color, sizeof(s->sy_dot2_color), "%s", "");
    snprintf(s->sy_dot3_color, sizeof(s->sy_dot3_color), "%s", "");
    snprintf(s->sy_dot4_color, sizeof(s->sy_dot4_color), "%s", "");
    snprintf(s->sy_dot5_color, sizeof(s->sy_dot5_color), "%s", "");
    snprintf(s->sy_dock_state0_color, sizeof(s->sy_dock_state0_color), "%s", "");
    snprintf(s->sy_dock_state1_color, sizeof(s->sy_dock_state1_color), "%s", "");
    snprintf(s->sy_dock_state2_color, sizeof(s->sy_dock_state2_color), "%s", "");
    snprintf(s->sy_dock_state3_color, sizeof(s->sy_dock_state3_color), "%s", "");
    snprintf(s->sy_dock_state4_color, sizeof(s->sy_dock_state4_color), "%s", "");
    snprintf(s->sy_dock_state5_color, sizeof(s->sy_dock_state5_color), "%s", "");
    snprintf(s->sy_alert_lbl0, sizeof(s->sy_alert_lbl0), "%s", "");
    snprintf(s->sy_alert_lbl1, sizeof(s->sy_alert_lbl1), "%s", "");
    snprintf(s->sy_alert_lbl2, sizeof(s->sy_alert_lbl2), "%s", "");
    snprintf(s->sy_alert_lbl0_color, sizeof(s->sy_alert_lbl0_color), "%s", "");
    snprintf(s->sy_alert_lbl1_color, sizeof(s->sy_alert_lbl1_color), "%s", "");
    snprintf(s->sy_alert_lbl2_color, sizeof(s->sy_alert_lbl2_color), "%s", "");
    snprintf(s->sy_alert_dot0_color, sizeof(s->sy_alert_dot0_color), "%s", "");
    snprintf(s->sy_alert_dot1_color, sizeof(s->sy_alert_dot1_color), "%s", "");
    snprintf(s->sy_alert_dot2_color, sizeof(s->sy_alert_dot2_color), "%s", "");
    snprintf(s->sy_tname0, sizeof(s->sy_tname0), "%s", "");
    snprintf(s->sy_tname1, sizeof(s->sy_tname1), "%s", "");
    snprintf(s->sy_tname2, sizeof(s->sy_tname2), "%s", "");
    snprintf(s->sy_tname3, sizeof(s->sy_tname3), "%s", "");
    snprintf(s->sy_tname4, sizeof(s->sy_tname4), "%s", "");
    snprintf(s->sy_tname5, sizeof(s->sy_tname5), "%s", "");
    snprintf(s->sy_tname6, sizeof(s->sy_tname6), "%s", "");
    snprintf(s->sy_tval0, sizeof(s->sy_tval0), "%s", "");
    snprintf(s->sy_tval1, sizeof(s->sy_tval1), "%s", "");
    snprintf(s->sy_tval2, sizeof(s->sy_tval2), "%s", "");
    snprintf(s->sy_tval3, sizeof(s->sy_tval3), "%s", "");
    snprintf(s->sy_tval4, sizeof(s->sy_tval4), "%s", "");
    snprintf(s->sy_tval5, sizeof(s->sy_tval5), "%s", "");
    snprintf(s->sy_tval6, sizeof(s->sy_tval6), "%s", "");
    snprintf(s->sy_tval0_color, sizeof(s->sy_tval0_color), "%s", "");
    snprintf(s->sy_tval1_color, sizeof(s->sy_tval1_color), "%s", "");
    snprintf(s->sy_tval2_color, sizeof(s->sy_tval2_color), "%s", "");
    snprintf(s->sy_tval3_color, sizeof(s->sy_tval3_color), "%s", "");
    snprintf(s->sy_tval4_color, sizeof(s->sy_tval4_color), "%s", "");
    snprintf(s->sy_tval5_color, sizeof(s->sy_tval5_color), "%s", "");
    snprintf(s->sy_tval6_color, sizeof(s->sy_tval6_color), "%s", "");
    s->sy_tbar0_value = 0.0f;
    s->sy_tbar1_value = 0.0f;
    s->sy_tbar2_value = 0.0f;
    s->sy_tbar3_value = 0.0f;
    s->sy_tbar4_value = 0.0f;
    s->sy_tbar5_value = 0.0f;
    s->sy_tbar6_value = 0.0f;
    snprintf(s->sy_kv_val0, sizeof(s->sy_kv_val0), "%s", "");
    snprintf(s->sy_kv_val1, sizeof(s->sy_kv_val1), "%s", "");
    snprintf(s->sy_kv_val2, sizeof(s->sy_kv_val2), "%s", "");
    snprintf(s->sy_kv_val3, sizeof(s->sy_kv_val3), "%s", "");
    snprintf(s->sy_kv_val4, sizeof(s->sy_kv_val4), "%s", "");
    snprintf(s->sy_kv_val5, sizeof(s->sy_kv_val5), "%s", "");
    snprintf(s->sy_kv_val6, sizeof(s->sy_kv_val6), "%s", "");
    snprintf(s->sy_kv_val7, sizeof(s->sy_kv_val7), "%s", "");
    kk_series_init(&s->ov_temp_trend_points, fnos_dash_ov_temp_trend_points_buf, FNOS_DASH_SERIES_OVTEMPTRENDPOINTS_CAP);
    snprintf(s->st_zfs_arc, sizeof(s->st_zfs_arc), "%s", "");
    snprintf(s->st_zfs_hit, sizeof(s->st_zfs_hit), "%s", "");
    s->st_zfs_bar_value = 0.0f;
    s->st_raid_bar0_value = 0.0f;
    s->st_raid_bar1_value = 0.0f;
    s->st_raid_bar2_value = 0.0f;
    s->st_raid_bar3_value = 0.0f;
    snprintf(s->nw_total_val, sizeof(s->nw_total_val), "%s", "");
    snprintf(s->sy_dock6, sizeof(s->sy_dock6), "%s", "");
    snprintf(s->sy_dock_state6, sizeof(s->sy_dock_state6), "%s", "");
    snprintf(s->sy_dock_state6_color, sizeof(s->sy_dock_state6_color), "%s", "");
    snprintf(s->sy_dot6_color, sizeof(s->sy_dot6_color), "%s", "");
    snprintf(s->sy_dock7, sizeof(s->sy_dock7), "%s", "");
    snprintf(s->sy_dock_state7, sizeof(s->sy_dock_state7), "%s", "");
    snprintf(s->sy_dock_state7_color, sizeof(s->sy_dock_state7_color), "%s", "");
    snprintf(s->sy_dot7_color, sizeof(s->sy_dot7_color), "%s", "");
    snprintf(s->sy_alert_lbl3, sizeof(s->sy_alert_lbl3), "%s", "");
    snprintf(s->sy_alert_lbl3_color, sizeof(s->sy_alert_lbl3_color), "%s", "");
    snprintf(s->sy_alert_dot3_color, sizeof(s->sy_alert_dot3_color), "%s", "");
    snprintf(s->sy_tname7, sizeof(s->sy_tname7), "%s", "");
    snprintf(s->sy_tval7, sizeof(s->sy_tval7), "%s", "");
    snprintf(s->sy_tval7_color, sizeof(s->sy_tval7_color), "%s", "");
    s->sy_tbar7_value = 0.0f;
    snprintf(s->sy_tname8, sizeof(s->sy_tname8), "%s", "");
    snprintf(s->sy_tval8, sizeof(s->sy_tval8), "%s", "");
    snprintf(s->sy_tval8_color, sizeof(s->sy_tval8_color), "%s", "");
    s->sy_tbar8_value = 0.0f;
    snprintf(s->sy_tname9, sizeof(s->sy_tname9), "%s", "");
    snprintf(s->sy_tval9, sizeof(s->sy_tval9), "%s", "");
    snprintf(s->sy_tval9_color, sizeof(s->sy_tval9_color), "%s", "");
    s->sy_tbar9_value = 0.0f;
    snprintf(s->sy_kv_val8, sizeof(s->sy_kv_val8), "%s", "");
    snprintf(s->sy_kv_val9, sizeof(s->sy_kv_val9), "%s", "");
    snprintf(s->sy_kv_val10, sizeof(s->sy_kv_val10), "%s", "");
    snprintf(s->sy_kv_val11, sizeof(s->sy_kv_val11), "%s", "");
    snprintf(s->ov_legend_cpu_lbl, sizeof(s->ov_legend_cpu_lbl), "%s", "");
    snprintf(s->ov_legend_mem_lbl, sizeof(s->ov_legend_mem_lbl), "%s", "");
    snprintf(s->ov_legend_temp_lbl, sizeof(s->ov_legend_temp_lbl), "%s", "");
    snprintf(s->ov_health_accent_color, sizeof(s->ov_health_accent_color), "%s", "#2FBF71");
    snprintf(s->ov_val0_color, sizeof(s->ov_val0_color), "%s", "#FFFFFF");
    snprintf(s->ov_val1_color, sizeof(s->ov_val1_color), "%s", "#FFFFFF");
    snprintf(s->ov_val2_color, sizeof(s->ov_val2_color), "%s", "#FFFFFF");
    snprintf(s->ov_val3_color, sizeof(s->ov_val3_color), "%s", "#FFFFFF");
    snprintf(s->st_hero_color, sizeof(s->st_hero_color), "%s", "#FFFFFF");
    snprintf(s->nw_down_val_color, sizeof(s->nw_down_val_color), "%s", "#FFFFFF");
    snprintf(s->nw_up_val_color, sizeof(s->nw_up_val_color), "%s", "#FFFFFF");
    s->ov_trend_top = 100.0f;
    s->sy_dock_row0_alpha = 1.0f;
    s->sy_dock_row1_alpha = 1.0f;
    s->sy_dock_row2_alpha = 1.0f;
    s->sy_dock_row3_alpha = 1.0f;
    s->sy_dock_row4_alpha = 1.0f;
    s->sy_dock_row5_alpha = 1.0f;
    s->sy_dock_row6_alpha = 1.0f;
    s->sy_dock_row7_alpha = 1.0f;
    s->sy_temp_row0_alpha = 1.0f;
    s->sy_temp_row1_alpha = 1.0f;
    s->sy_temp_row2_alpha = 1.0f;
    s->sy_temp_row3_alpha = 1.0f;
    s->sy_temp_row4_alpha = 1.0f;
    s->sy_temp_row5_alpha = 1.0f;
    s->sy_temp_row6_alpha = 1.0f;
    s->sy_temp_row7_alpha = 1.0f;
    s->sy_temp_row8_alpha = 1.0f;
    s->sy_temp_row9_alpha = 1.0f;
    snprintf(s->ov_vol_use0, sizeof(s->ov_vol_use0), "%s", "");
    snprintf(s->ov_vol_use1, sizeof(s->ov_vol_use1), "%s", "");
    snprintf(s->ov_vol_use2, sizeof(s->ov_vol_use2), "%s", "");
    snprintf(s->ov_vol_use3, sizeof(s->ov_vol_use3), "%s", "");
    snprintf(s->ov_vol_use4, sizeof(s->ov_vol_use4), "%s", "");
    snprintf(s->ov_vol_use5, sizeof(s->ov_vol_use5), "%s", "");
    snprintf(s->ov_peak_cpu, sizeof(s->ov_peak_cpu), "%s", "");
    snprintf(s->ov_peak_mem, sizeof(s->ov_peak_mem), "%s", "");
    snprintf(s->ov_peak_temp, sizeof(s->ov_peak_temp), "%s", "");
    memset(s->dirty, 0, sizeof(s->dirty));
}

bool fnos_dash_store_update_str(fnos_dash_store_t *s, fnos_dash_field_id_t f, const char * v)
{
    if ((unsigned)f >= FNOS_DASH_FIELD_COUNT) return false;
    switch (f) {
    case FNOS_DASH_FIELD_HdHost:
        if (strcmp(s->hd_host, v ? v : "") == 0) return true;
        snprintf(s->hd_host, sizeof(s->hd_host), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_HdEndpoint:
        if (strcmp(s->hd_endpoint, v ? v : "") == 0) return true;
        snprintf(s->hd_endpoint, sizeof(s->hd_endpoint), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_HdClock:
        if (strcmp(s->hd_clock, v ? v : "") == 0) return true;
        snprintf(s->hd_clock, sizeof(s->hd_clock), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_HdChipText:
        if (strcmp(s->hd_chip_text, v ? v : "") == 0) return true;
        snprintf(s->hd_chip_text, sizeof(s->hd_chip_text), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_HdChipColor:
        if (strcmp(s->hd_chip_color, v ? v : "") == 0) return true;
        snprintf(s->hd_chip_color, sizeof(s->hd_chip_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_FtPoll:
        if (strcmp(s->ft_poll, v ? v : "") == 0) return true;
        snprintf(s->ft_poll, sizeof(s->ft_poll), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_FtAlertLbl:
        if (strcmp(s->ft_alert_lbl, v ? v : "") == 0) return true;
        snprintf(s->ft_alert_lbl, sizeof(s->ft_alert_lbl), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_FtAlertDotColor:
        if (strcmp(s->ft_alert_dot_color, v ? v : "") == 0) return true;
        snprintf(s->ft_alert_dot_color, sizeof(s->ft_alert_dot_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_FtAlertLblColor:
        if (strcmp(s->ft_alert_lbl_color, v ? v : "") == 0) return true;
        snprintf(s->ft_alert_lbl_color, sizeof(s->ft_alert_lbl_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVerdict:
        if (strcmp(s->ov_verdict, v ? v : "") == 0) return true;
        snprintf(s->ov_verdict, sizeof(s->ov_verdict), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVerdictSub:
        if (strcmp(s->ov_verdict_sub, v ? v : "") == 0) return true;
        snprintf(s->ov_verdict_sub, sizeof(s->ov_verdict_sub), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvReason:
        if (strcmp(s->ov_reason, v ? v : "") == 0) return true;
        snprintf(s->ov_reason, sizeof(s->ov_reason), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVerdictColor:
        if (strcmp(s->ov_verdict_color, v ? v : "") == 0) return true;
        snprintf(s->ov_verdict_color, sizeof(s->ov_verdict_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolName0:
        if (strcmp(s->ov_vol_name0, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_name0, sizeof(s->ov_vol_name0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolPct0:
        if (strcmp(s->ov_vol_pct0, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_pct0, sizeof(s->ov_vol_pct0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolName1:
        if (strcmp(s->ov_vol_name1, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_name1, sizeof(s->ov_vol_name1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolPct1:
        if (strcmp(s->ov_vol_pct1, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_pct1, sizeof(s->ov_vol_pct1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolName2:
        if (strcmp(s->ov_vol_name2, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_name2, sizeof(s->ov_vol_name2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolPct2:
        if (strcmp(s->ov_vol_pct2, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_pct2, sizeof(s->ov_vol_pct2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolName3:
        if (strcmp(s->ov_vol_name3, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_name3, sizeof(s->ov_vol_name3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolPct3:
        if (strcmp(s->ov_vol_pct3, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_pct3, sizeof(s->ov_vol_pct3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolName4:
        if (strcmp(s->ov_vol_name4, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_name4, sizeof(s->ov_vol_name4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolPct4:
        if (strcmp(s->ov_vol_pct4, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_pct4, sizeof(s->ov_vol_pct4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolName5:
        if (strcmp(s->ov_vol_name5, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_name5, sizeof(s->ov_vol_name5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolPct5:
        if (strcmp(s->ov_vol_pct5, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_pct5, sizeof(s->ov_vol_pct5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVal0:
        if (strcmp(s->ov_val0, v ? v : "") == 0) return true;
        snprintf(s->ov_val0, sizeof(s->ov_val0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvSub0:
        if (strcmp(s->ov_sub0, v ? v : "") == 0) return true;
        snprintf(s->ov_sub0, sizeof(s->ov_sub0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVal1:
        if (strcmp(s->ov_val1, v ? v : "") == 0) return true;
        snprintf(s->ov_val1, sizeof(s->ov_val1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvSub1:
        if (strcmp(s->ov_sub1, v ? v : "") == 0) return true;
        snprintf(s->ov_sub1, sizeof(s->ov_sub1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVal2:
        if (strcmp(s->ov_val2, v ? v : "") == 0) return true;
        snprintf(s->ov_val2, sizeof(s->ov_val2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvSub2:
        if (strcmp(s->ov_sub2, v ? v : "") == 0) return true;
        snprintf(s->ov_sub2, sizeof(s->ov_sub2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVal3:
        if (strcmp(s->ov_val3, v ? v : "") == 0) return true;
        snprintf(s->ov_val3, sizeof(s->ov_val3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvSub3:
        if (strcmp(s->ov_sub3, v ? v : "") == 0) return true;
        snprintf(s->ov_sub3, sizeof(s->ov_sub3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StHero:
        if (strcmp(s->st_hero, v ? v : "") == 0) return true;
        snprintf(s->st_hero, sizeof(s->st_hero), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StUsed:
        if (strcmp(s->st_used, v ? v : "") == 0) return true;
        snprintf(s->st_used, sizeof(s->st_used), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StCnt:
        if (strcmp(s->st_cnt, v ? v : "") == 0) return true;
        snprintf(s->st_cnt, sizeof(s->st_cnt), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StFree:
        if (strcmp(s->st_free, v ? v : "") == 0) return true;
        snprintf(s->st_free, sizeof(s->st_free), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StSeg0Color:
        if (strcmp(s->st_seg0_color, v ? v : "") == 0) return true;
        snprintf(s->st_seg0_color, sizeof(s->st_seg0_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StSeg1Color:
        if (strcmp(s->st_seg1_color, v ? v : "") == 0) return true;
        snprintf(s->st_seg1_color, sizeof(s->st_seg1_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StSeg2Color:
        if (strcmp(s->st_seg2_color, v ? v : "") == 0) return true;
        snprintf(s->st_seg2_color, sizeof(s->st_seg2_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StSeg3Color:
        if (strcmp(s->st_seg3_color, v ? v : "") == 0) return true;
        snprintf(s->st_seg3_color, sizeof(s->st_seg3_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StSeg4Color:
        if (strcmp(s->st_seg4_color, v ? v : "") == 0) return true;
        snprintf(s->st_seg4_color, sizeof(s->st_seg4_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StSeg5Color:
        if (strcmp(s->st_seg5_color, v ? v : "") == 0) return true;
        snprintf(s->st_seg5_color, sizeof(s->st_seg5_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolName0:
        if (strcmp(s->st_vol_name0, v ? v : "") == 0) return true;
        snprintf(s->st_vol_name0, sizeof(s->st_vol_name0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolFs0:
        if (strcmp(s->st_vol_fs0, v ? v : "") == 0) return true;
        snprintf(s->st_vol_fs0, sizeof(s->st_vol_fs0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolUse0:
        if (strcmp(s->st_vol_use0, v ? v : "") == 0) return true;
        snprintf(s->st_vol_use0, sizeof(s->st_vol_use0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolPct0:
        if (strcmp(s->st_vol_pct0, v ? v : "") == 0) return true;
        snprintf(s->st_vol_pct0, sizeof(s->st_vol_pct0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolName1:
        if (strcmp(s->st_vol_name1, v ? v : "") == 0) return true;
        snprintf(s->st_vol_name1, sizeof(s->st_vol_name1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolFs1:
        if (strcmp(s->st_vol_fs1, v ? v : "") == 0) return true;
        snprintf(s->st_vol_fs1, sizeof(s->st_vol_fs1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolUse1:
        if (strcmp(s->st_vol_use1, v ? v : "") == 0) return true;
        snprintf(s->st_vol_use1, sizeof(s->st_vol_use1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolPct1:
        if (strcmp(s->st_vol_pct1, v ? v : "") == 0) return true;
        snprintf(s->st_vol_pct1, sizeof(s->st_vol_pct1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolName2:
        if (strcmp(s->st_vol_name2, v ? v : "") == 0) return true;
        snprintf(s->st_vol_name2, sizeof(s->st_vol_name2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolFs2:
        if (strcmp(s->st_vol_fs2, v ? v : "") == 0) return true;
        snprintf(s->st_vol_fs2, sizeof(s->st_vol_fs2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolUse2:
        if (strcmp(s->st_vol_use2, v ? v : "") == 0) return true;
        snprintf(s->st_vol_use2, sizeof(s->st_vol_use2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolPct2:
        if (strcmp(s->st_vol_pct2, v ? v : "") == 0) return true;
        snprintf(s->st_vol_pct2, sizeof(s->st_vol_pct2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolName3:
        if (strcmp(s->st_vol_name3, v ? v : "") == 0) return true;
        snprintf(s->st_vol_name3, sizeof(s->st_vol_name3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolFs3:
        if (strcmp(s->st_vol_fs3, v ? v : "") == 0) return true;
        snprintf(s->st_vol_fs3, sizeof(s->st_vol_fs3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolUse3:
        if (strcmp(s->st_vol_use3, v ? v : "") == 0) return true;
        snprintf(s->st_vol_use3, sizeof(s->st_vol_use3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolPct3:
        if (strcmp(s->st_vol_pct3, v ? v : "") == 0) return true;
        snprintf(s->st_vol_pct3, sizeof(s->st_vol_pct3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolName4:
        if (strcmp(s->st_vol_name4, v ? v : "") == 0) return true;
        snprintf(s->st_vol_name4, sizeof(s->st_vol_name4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolFs4:
        if (strcmp(s->st_vol_fs4, v ? v : "") == 0) return true;
        snprintf(s->st_vol_fs4, sizeof(s->st_vol_fs4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolUse4:
        if (strcmp(s->st_vol_use4, v ? v : "") == 0) return true;
        snprintf(s->st_vol_use4, sizeof(s->st_vol_use4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolPct4:
        if (strcmp(s->st_vol_pct4, v ? v : "") == 0) return true;
        snprintf(s->st_vol_pct4, sizeof(s->st_vol_pct4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolName5:
        if (strcmp(s->st_vol_name5, v ? v : "") == 0) return true;
        snprintf(s->st_vol_name5, sizeof(s->st_vol_name5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolFs5:
        if (strcmp(s->st_vol_fs5, v ? v : "") == 0) return true;
        snprintf(s->st_vol_fs5, sizeof(s->st_vol_fs5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolUse5:
        if (strcmp(s->st_vol_use5, v ? v : "") == 0) return true;
        snprintf(s->st_vol_use5, sizeof(s->st_vol_use5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StVolPct5:
        if (strcmp(s->st_vol_pct5, v ? v : "") == 0) return true;
        snprintf(s->st_vol_pct5, sizeof(s->st_vol_pct5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidName0:
        if (strcmp(s->st_raid_name0, v ? v : "") == 0) return true;
        snprintf(s->st_raid_name0, sizeof(s->st_raid_name0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidLvl0:
        if (strcmp(s->st_raid_lvl0, v ? v : "") == 0) return true;
        snprintf(s->st_raid_lvl0, sizeof(s->st_raid_lvl0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidState0:
        if (strcmp(s->st_raid_state0, v ? v : "") == 0) return true;
        snprintf(s->st_raid_state0, sizeof(s->st_raid_state0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidState0Color:
        if (strcmp(s->st_raid_state0_color, v ? v : "") == 0) return true;
        snprintf(s->st_raid_state0_color, sizeof(s->st_raid_state0_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidName1:
        if (strcmp(s->st_raid_name1, v ? v : "") == 0) return true;
        snprintf(s->st_raid_name1, sizeof(s->st_raid_name1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidLvl1:
        if (strcmp(s->st_raid_lvl1, v ? v : "") == 0) return true;
        snprintf(s->st_raid_lvl1, sizeof(s->st_raid_lvl1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidState1:
        if (strcmp(s->st_raid_state1, v ? v : "") == 0) return true;
        snprintf(s->st_raid_state1, sizeof(s->st_raid_state1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidState1Color:
        if (strcmp(s->st_raid_state1_color, v ? v : "") == 0) return true;
        snprintf(s->st_raid_state1_color, sizeof(s->st_raid_state1_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidName2:
        if (strcmp(s->st_raid_name2, v ? v : "") == 0) return true;
        snprintf(s->st_raid_name2, sizeof(s->st_raid_name2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidLvl2:
        if (strcmp(s->st_raid_lvl2, v ? v : "") == 0) return true;
        snprintf(s->st_raid_lvl2, sizeof(s->st_raid_lvl2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidState2:
        if (strcmp(s->st_raid_state2, v ? v : "") == 0) return true;
        snprintf(s->st_raid_state2, sizeof(s->st_raid_state2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidState2Color:
        if (strcmp(s->st_raid_state2_color, v ? v : "") == 0) return true;
        snprintf(s->st_raid_state2_color, sizeof(s->st_raid_state2_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidName3:
        if (strcmp(s->st_raid_name3, v ? v : "") == 0) return true;
        snprintf(s->st_raid_name3, sizeof(s->st_raid_name3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidLvl3:
        if (strcmp(s->st_raid_lvl3, v ? v : "") == 0) return true;
        snprintf(s->st_raid_lvl3, sizeof(s->st_raid_lvl3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidState3:
        if (strcmp(s->st_raid_state3, v ? v : "") == 0) return true;
        snprintf(s->st_raid_state3, sizeof(s->st_raid_state3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StRaidState3Color:
        if (strcmp(s->st_raid_state3_color, v ? v : "") == 0) return true;
        snprintf(s->st_raid_state3_color, sizeof(s->st_raid_state3_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StIoName0:
        if (strcmp(s->st_io_name0, v ? v : "") == 0) return true;
        snprintf(s->st_io_name0, sizeof(s->st_io_name0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StIoVal0:
        if (strcmp(s->st_io_val0, v ? v : "") == 0) return true;
        snprintf(s->st_io_val0, sizeof(s->st_io_val0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StIoName1:
        if (strcmp(s->st_io_name1, v ? v : "") == 0) return true;
        snprintf(s->st_io_name1, sizeof(s->st_io_name1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StIoVal1:
        if (strcmp(s->st_io_val1, v ? v : "") == 0) return true;
        snprintf(s->st_io_val1, sizeof(s->st_io_val1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StIoName2:
        if (strcmp(s->st_io_name2, v ? v : "") == 0) return true;
        snprintf(s->st_io_name2, sizeof(s->st_io_name2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StIoVal2:
        if (strcmp(s->st_io_val2, v ? v : "") == 0) return true;
        snprintf(s->st_io_val2, sizeof(s->st_io_val2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StIoName3:
        if (strcmp(s->st_io_name3, v ? v : "") == 0) return true;
        snprintf(s->st_io_name3, sizeof(s->st_io_name3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StIoVal3:
        if (strcmp(s->st_io_val3, v ? v : "") == 0) return true;
        snprintf(s->st_io_val3, sizeof(s->st_io_val3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwDownVal:
        if (strcmp(s->nw_down_val, v ? v : "") == 0) return true;
        snprintf(s->nw_down_val, sizeof(s->nw_down_val), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwDownUnit:
        if (strcmp(s->nw_down_unit, v ? v : "") == 0) return true;
        snprintf(s->nw_down_unit, sizeof(s->nw_down_unit), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwDownSub:
        if (strcmp(s->nw_down_sub, v ? v : "") == 0) return true;
        snprintf(s->nw_down_sub, sizeof(s->nw_down_sub), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwUpVal:
        if (strcmp(s->nw_up_val, v ? v : "") == 0) return true;
        snprintf(s->nw_up_val, sizeof(s->nw_up_val), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwUpUnit:
        if (strcmp(s->nw_up_unit, v ? v : "") == 0) return true;
        snprintf(s->nw_up_unit, sizeof(s->nw_up_unit), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwUpSub:
        if (strcmp(s->nw_up_sub, v ? v : "") == 0) return true;
        snprintf(s->nw_up_sub, sizeof(s->nw_up_sub), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwAxis0:
        if (strcmp(s->nw_axis0, v ? v : "") == 0) return true;
        snprintf(s->nw_axis0, sizeof(s->nw_axis0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwAxis1:
        if (strcmp(s->nw_axis1, v ? v : "") == 0) return true;
        snprintf(s->nw_axis1, sizeof(s->nw_axis1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwAxis2:
        if (strcmp(s->nw_axis2, v ? v : "") == 0) return true;
        snprintf(s->nw_axis2, sizeof(s->nw_axis2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwCtxVal0:
        if (strcmp(s->nw_ctx_val0, v ? v : "") == 0) return true;
        snprintf(s->nw_ctx_val0, sizeof(s->nw_ctx_val0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwCtxVal1:
        if (strcmp(s->nw_ctx_val1, v ? v : "") == 0) return true;
        snprintf(s->nw_ctx_val1, sizeof(s->nw_ctx_val1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwCtxVal2:
        if (strcmp(s->nw_ctx_val2, v ? v : "") == 0) return true;
        snprintf(s->nw_ctx_val2, sizeof(s->nw_ctx_val2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwCtxVal3:
        if (strcmp(s->nw_ctx_val3, v ? v : "") == 0) return true;
        snprintf(s->nw_ctx_val3, sizeof(s->nw_ctx_val3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyCnt:
        if (strcmp(s->sy_cnt, v ? v : "") == 0) return true;
        snprintf(s->sy_cnt, sizeof(s->sy_cnt), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDock0:
        if (strcmp(s->sy_dock0, v ? v : "") == 0) return true;
        snprintf(s->sy_dock0, sizeof(s->sy_dock0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDock1:
        if (strcmp(s->sy_dock1, v ? v : "") == 0) return true;
        snprintf(s->sy_dock1, sizeof(s->sy_dock1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDock2:
        if (strcmp(s->sy_dock2, v ? v : "") == 0) return true;
        snprintf(s->sy_dock2, sizeof(s->sy_dock2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDock3:
        if (strcmp(s->sy_dock3, v ? v : "") == 0) return true;
        snprintf(s->sy_dock3, sizeof(s->sy_dock3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDock4:
        if (strcmp(s->sy_dock4, v ? v : "") == 0) return true;
        snprintf(s->sy_dock4, sizeof(s->sy_dock4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDock5:
        if (strcmp(s->sy_dock5, v ? v : "") == 0) return true;
        snprintf(s->sy_dock5, sizeof(s->sy_dock5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState0:
        if (strcmp(s->sy_dock_state0, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state0, sizeof(s->sy_dock_state0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState1:
        if (strcmp(s->sy_dock_state1, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state1, sizeof(s->sy_dock_state1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState2:
        if (strcmp(s->sy_dock_state2, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state2, sizeof(s->sy_dock_state2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState3:
        if (strcmp(s->sy_dock_state3, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state3, sizeof(s->sy_dock_state3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState4:
        if (strcmp(s->sy_dock_state4, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state4, sizeof(s->sy_dock_state4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState5:
        if (strcmp(s->sy_dock_state5, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state5, sizeof(s->sy_dock_state5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDot0Color:
        if (strcmp(s->sy_dot0_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dot0_color, sizeof(s->sy_dot0_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDot1Color:
        if (strcmp(s->sy_dot1_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dot1_color, sizeof(s->sy_dot1_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDot2Color:
        if (strcmp(s->sy_dot2_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dot2_color, sizeof(s->sy_dot2_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDot3Color:
        if (strcmp(s->sy_dot3_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dot3_color, sizeof(s->sy_dot3_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDot4Color:
        if (strcmp(s->sy_dot4_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dot4_color, sizeof(s->sy_dot4_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDot5Color:
        if (strcmp(s->sy_dot5_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dot5_color, sizeof(s->sy_dot5_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState0Color:
        if (strcmp(s->sy_dock_state0_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state0_color, sizeof(s->sy_dock_state0_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState1Color:
        if (strcmp(s->sy_dock_state1_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state1_color, sizeof(s->sy_dock_state1_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState2Color:
        if (strcmp(s->sy_dock_state2_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state2_color, sizeof(s->sy_dock_state2_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState3Color:
        if (strcmp(s->sy_dock_state3_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state3_color, sizeof(s->sy_dock_state3_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState4Color:
        if (strcmp(s->sy_dock_state4_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state4_color, sizeof(s->sy_dock_state4_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState5Color:
        if (strcmp(s->sy_dock_state5_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state5_color, sizeof(s->sy_dock_state5_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertLbl0:
        if (strcmp(s->sy_alert_lbl0, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_lbl0, sizeof(s->sy_alert_lbl0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertLbl1:
        if (strcmp(s->sy_alert_lbl1, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_lbl1, sizeof(s->sy_alert_lbl1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertLbl2:
        if (strcmp(s->sy_alert_lbl2, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_lbl2, sizeof(s->sy_alert_lbl2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertLbl0Color:
        if (strcmp(s->sy_alert_lbl0_color, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_lbl0_color, sizeof(s->sy_alert_lbl0_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertLbl1Color:
        if (strcmp(s->sy_alert_lbl1_color, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_lbl1_color, sizeof(s->sy_alert_lbl1_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertLbl2Color:
        if (strcmp(s->sy_alert_lbl2_color, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_lbl2_color, sizeof(s->sy_alert_lbl2_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertDot0Color:
        if (strcmp(s->sy_alert_dot0_color, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_dot0_color, sizeof(s->sy_alert_dot0_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertDot1Color:
        if (strcmp(s->sy_alert_dot1_color, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_dot1_color, sizeof(s->sy_alert_dot1_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertDot2Color:
        if (strcmp(s->sy_alert_dot2_color, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_dot2_color, sizeof(s->sy_alert_dot2_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName0:
        if (strcmp(s->sy_tname0, v ? v : "") == 0) return true;
        snprintf(s->sy_tname0, sizeof(s->sy_tname0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName1:
        if (strcmp(s->sy_tname1, v ? v : "") == 0) return true;
        snprintf(s->sy_tname1, sizeof(s->sy_tname1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName2:
        if (strcmp(s->sy_tname2, v ? v : "") == 0) return true;
        snprintf(s->sy_tname2, sizeof(s->sy_tname2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName3:
        if (strcmp(s->sy_tname3, v ? v : "") == 0) return true;
        snprintf(s->sy_tname3, sizeof(s->sy_tname3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName4:
        if (strcmp(s->sy_tname4, v ? v : "") == 0) return true;
        snprintf(s->sy_tname4, sizeof(s->sy_tname4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName5:
        if (strcmp(s->sy_tname5, v ? v : "") == 0) return true;
        snprintf(s->sy_tname5, sizeof(s->sy_tname5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName6:
        if (strcmp(s->sy_tname6, v ? v : "") == 0) return true;
        snprintf(s->sy_tname6, sizeof(s->sy_tname6), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal0:
        if (strcmp(s->sy_tval0, v ? v : "") == 0) return true;
        snprintf(s->sy_tval0, sizeof(s->sy_tval0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal1:
        if (strcmp(s->sy_tval1, v ? v : "") == 0) return true;
        snprintf(s->sy_tval1, sizeof(s->sy_tval1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal2:
        if (strcmp(s->sy_tval2, v ? v : "") == 0) return true;
        snprintf(s->sy_tval2, sizeof(s->sy_tval2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal3:
        if (strcmp(s->sy_tval3, v ? v : "") == 0) return true;
        snprintf(s->sy_tval3, sizeof(s->sy_tval3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal4:
        if (strcmp(s->sy_tval4, v ? v : "") == 0) return true;
        snprintf(s->sy_tval4, sizeof(s->sy_tval4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal5:
        if (strcmp(s->sy_tval5, v ? v : "") == 0) return true;
        snprintf(s->sy_tval5, sizeof(s->sy_tval5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal6:
        if (strcmp(s->sy_tval6, v ? v : "") == 0) return true;
        snprintf(s->sy_tval6, sizeof(s->sy_tval6), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal0Color:
        if (strcmp(s->sy_tval0_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval0_color, sizeof(s->sy_tval0_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal1Color:
        if (strcmp(s->sy_tval1_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval1_color, sizeof(s->sy_tval1_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal2Color:
        if (strcmp(s->sy_tval2_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval2_color, sizeof(s->sy_tval2_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal3Color:
        if (strcmp(s->sy_tval3_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval3_color, sizeof(s->sy_tval3_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal4Color:
        if (strcmp(s->sy_tval4_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval4_color, sizeof(s->sy_tval4_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal5Color:
        if (strcmp(s->sy_tval5_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval5_color, sizeof(s->sy_tval5_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal6Color:
        if (strcmp(s->sy_tval6_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval6_color, sizeof(s->sy_tval6_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal0:
        if (strcmp(s->sy_kv_val0, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val0, sizeof(s->sy_kv_val0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal1:
        if (strcmp(s->sy_kv_val1, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val1, sizeof(s->sy_kv_val1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal2:
        if (strcmp(s->sy_kv_val2, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val2, sizeof(s->sy_kv_val2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal3:
        if (strcmp(s->sy_kv_val3, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val3, sizeof(s->sy_kv_val3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal4:
        if (strcmp(s->sy_kv_val4, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val4, sizeof(s->sy_kv_val4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal5:
        if (strcmp(s->sy_kv_val5, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val5, sizeof(s->sy_kv_val5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal6:
        if (strcmp(s->sy_kv_val6, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val6, sizeof(s->sy_kv_val6), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal7:
        if (strcmp(s->sy_kv_val7, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val7, sizeof(s->sy_kv_val7), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StZfsArc:
        if (strcmp(s->st_zfs_arc, v ? v : "") == 0) return true;
        snprintf(s->st_zfs_arc, sizeof(s->st_zfs_arc), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StZfsHit:
        if (strcmp(s->st_zfs_hit, v ? v : "") == 0) return true;
        snprintf(s->st_zfs_hit, sizeof(s->st_zfs_hit), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwTotalVal:
        if (strcmp(s->nw_total_val, v ? v : "") == 0) return true;
        snprintf(s->nw_total_val, sizeof(s->nw_total_val), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDock6:
        if (strcmp(s->sy_dock6, v ? v : "") == 0) return true;
        snprintf(s->sy_dock6, sizeof(s->sy_dock6), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState6:
        if (strcmp(s->sy_dock_state6, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state6, sizeof(s->sy_dock_state6), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState6Color:
        if (strcmp(s->sy_dock_state6_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state6_color, sizeof(s->sy_dock_state6_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDot6Color:
        if (strcmp(s->sy_dot6_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dot6_color, sizeof(s->sy_dot6_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDock7:
        if (strcmp(s->sy_dock7, v ? v : "") == 0) return true;
        snprintf(s->sy_dock7, sizeof(s->sy_dock7), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState7:
        if (strcmp(s->sy_dock_state7, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state7, sizeof(s->sy_dock_state7), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDockState7Color:
        if (strcmp(s->sy_dock_state7_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dock_state7_color, sizeof(s->sy_dock_state7_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyDot7Color:
        if (strcmp(s->sy_dot7_color, v ? v : "") == 0) return true;
        snprintf(s->sy_dot7_color, sizeof(s->sy_dot7_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertLbl3:
        if (strcmp(s->sy_alert_lbl3, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_lbl3, sizeof(s->sy_alert_lbl3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertLbl3Color:
        if (strcmp(s->sy_alert_lbl3_color, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_lbl3_color, sizeof(s->sy_alert_lbl3_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyAlertDot3Color:
        if (strcmp(s->sy_alert_dot3_color, v ? v : "") == 0) return true;
        snprintf(s->sy_alert_dot3_color, sizeof(s->sy_alert_dot3_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName7:
        if (strcmp(s->sy_tname7, v ? v : "") == 0) return true;
        snprintf(s->sy_tname7, sizeof(s->sy_tname7), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal7:
        if (strcmp(s->sy_tval7, v ? v : "") == 0) return true;
        snprintf(s->sy_tval7, sizeof(s->sy_tval7), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal7Color:
        if (strcmp(s->sy_tval7_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval7_color, sizeof(s->sy_tval7_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName8:
        if (strcmp(s->sy_tname8, v ? v : "") == 0) return true;
        snprintf(s->sy_tname8, sizeof(s->sy_tname8), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal8:
        if (strcmp(s->sy_tval8, v ? v : "") == 0) return true;
        snprintf(s->sy_tval8, sizeof(s->sy_tval8), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal8Color:
        if (strcmp(s->sy_tval8_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval8_color, sizeof(s->sy_tval8_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTName9:
        if (strcmp(s->sy_tname9, v ? v : "") == 0) return true;
        snprintf(s->sy_tname9, sizeof(s->sy_tname9), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal9:
        if (strcmp(s->sy_tval9, v ? v : "") == 0) return true;
        snprintf(s->sy_tval9, sizeof(s->sy_tval9), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyTVal9Color:
        if (strcmp(s->sy_tval9_color, v ? v : "") == 0) return true;
        snprintf(s->sy_tval9_color, sizeof(s->sy_tval9_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal8:
        if (strcmp(s->sy_kv_val8, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val8, sizeof(s->sy_kv_val8), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal9:
        if (strcmp(s->sy_kv_val9, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val9, sizeof(s->sy_kv_val9), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal10:
        if (strcmp(s->sy_kv_val10, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val10, sizeof(s->sy_kv_val10), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_SyKvVal11:
        if (strcmp(s->sy_kv_val11, v ? v : "") == 0) return true;
        snprintf(s->sy_kv_val11, sizeof(s->sy_kv_val11), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvLegendCpuLbl:
        if (strcmp(s->ov_legend_cpu_lbl, v ? v : "") == 0) return true;
        snprintf(s->ov_legend_cpu_lbl, sizeof(s->ov_legend_cpu_lbl), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvLegendMemLbl:
        if (strcmp(s->ov_legend_mem_lbl, v ? v : "") == 0) return true;
        snprintf(s->ov_legend_mem_lbl, sizeof(s->ov_legend_mem_lbl), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvLegendTempLbl:
        if (strcmp(s->ov_legend_temp_lbl, v ? v : "") == 0) return true;
        snprintf(s->ov_legend_temp_lbl, sizeof(s->ov_legend_temp_lbl), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvHealthAccentColor:
        if (strcmp(s->ov_health_accent_color, v ? v : "") == 0) return true;
        snprintf(s->ov_health_accent_color, sizeof(s->ov_health_accent_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVal0Color:
        if (strcmp(s->ov_val0_color, v ? v : "") == 0) return true;
        snprintf(s->ov_val0_color, sizeof(s->ov_val0_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVal1Color:
        if (strcmp(s->ov_val1_color, v ? v : "") == 0) return true;
        snprintf(s->ov_val1_color, sizeof(s->ov_val1_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVal2Color:
        if (strcmp(s->ov_val2_color, v ? v : "") == 0) return true;
        snprintf(s->ov_val2_color, sizeof(s->ov_val2_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVal3Color:
        if (strcmp(s->ov_val3_color, v ? v : "") == 0) return true;
        snprintf(s->ov_val3_color, sizeof(s->ov_val3_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_StHeroColor:
        if (strcmp(s->st_hero_color, v ? v : "") == 0) return true;
        snprintf(s->st_hero_color, sizeof(s->st_hero_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwDownValColor:
        if (strcmp(s->nw_down_val_color, v ? v : "") == 0) return true;
        snprintf(s->nw_down_val_color, sizeof(s->nw_down_val_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_NwUpValColor:
        if (strcmp(s->nw_up_val_color, v ? v : "") == 0) return true;
        snprintf(s->nw_up_val_color, sizeof(s->nw_up_val_color), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolUse0:
        if (strcmp(s->ov_vol_use0, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_use0, sizeof(s->ov_vol_use0), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolUse1:
        if (strcmp(s->ov_vol_use1, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_use1, sizeof(s->ov_vol_use1), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolUse2:
        if (strcmp(s->ov_vol_use2, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_use2, sizeof(s->ov_vol_use2), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolUse3:
        if (strcmp(s->ov_vol_use3, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_use3, sizeof(s->ov_vol_use3), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolUse4:
        if (strcmp(s->ov_vol_use4, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_use4, sizeof(s->ov_vol_use4), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvVolUse5:
        if (strcmp(s->ov_vol_use5, v ? v : "") == 0) return true;
        snprintf(s->ov_vol_use5, sizeof(s->ov_vol_use5), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvPeakCpu:
        if (strcmp(s->ov_peak_cpu, v ? v : "") == 0) return true;
        snprintf(s->ov_peak_cpu, sizeof(s->ov_peak_cpu), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvPeakMem:
        if (strcmp(s->ov_peak_mem, v ? v : "") == 0) return true;
        snprintf(s->ov_peak_mem, sizeof(s->ov_peak_mem), "%s", v ? v : "");
        break;
    case FNOS_DASH_FIELD_OvPeakTemp:
        if (strcmp(s->ov_peak_temp, v ? v : "") == 0) return true;
        snprintf(s->ov_peak_temp, sizeof(s->ov_peak_temp), "%s", v ? v : "");
        break;
    default:
        return false;
    }
    s->dirty[(unsigned)f >> 5] |= 1u << ((unsigned)f & 31);
    return true;
}

bool fnos_dash_store_update_int(fnos_dash_store_t *s, fnos_dash_field_id_t f, int32_t v)
{
    if ((unsigned)f >= FNOS_DASH_FIELD_COUNT) return false;
    switch (f) {
    case FNOS_DASH_FIELD_HdSignalValue:
        if (s->hd_signal_value == v) return true;
        s->hd_signal_value = v;
        break;
    case FNOS_DASH_FIELD_StSeg0X:
        if (s->st_seg0_x == v) return true;
        s->st_seg0_x = v;
        break;
    case FNOS_DASH_FIELD_StSeg0Width:
        if (s->st_seg0_width == v) return true;
        s->st_seg0_width = v;
        break;
    case FNOS_DASH_FIELD_StSeg1X:
        if (s->st_seg1_x == v) return true;
        s->st_seg1_x = v;
        break;
    case FNOS_DASH_FIELD_StSeg1Width:
        if (s->st_seg1_width == v) return true;
        s->st_seg1_width = v;
        break;
    case FNOS_DASH_FIELD_StSeg2X:
        if (s->st_seg2_x == v) return true;
        s->st_seg2_x = v;
        break;
    case FNOS_DASH_FIELD_StSeg2Width:
        if (s->st_seg2_width == v) return true;
        s->st_seg2_width = v;
        break;
    case FNOS_DASH_FIELD_StSeg3X:
        if (s->st_seg3_x == v) return true;
        s->st_seg3_x = v;
        break;
    case FNOS_DASH_FIELD_StSeg3Width:
        if (s->st_seg3_width == v) return true;
        s->st_seg3_width = v;
        break;
    case FNOS_DASH_FIELD_StSeg4X:
        if (s->st_seg4_x == v) return true;
        s->st_seg4_x = v;
        break;
    case FNOS_DASH_FIELD_StSeg4Width:
        if (s->st_seg4_width == v) return true;
        s->st_seg4_width = v;
        break;
    case FNOS_DASH_FIELD_StSeg5X:
        if (s->st_seg5_x == v) return true;
        s->st_seg5_x = v;
        break;
    case FNOS_DASH_FIELD_StSeg5Width:
        if (s->st_seg5_width == v) return true;
        s->st_seg5_width = v;
        break;
    default:
        return false;
    }
    s->dirty[(unsigned)f >> 5] |= 1u << ((unsigned)f & 31);
    return true;
}

bool fnos_dash_store_update_float(fnos_dash_store_t *s, fnos_dash_field_id_t f, float v)
{
    if ((unsigned)f >= FNOS_DASH_FIELD_COUNT) return false;
    switch (f) {
    case FNOS_DASH_FIELD_OvVolBar0Value:
        if (s->ov_vol_bar0_value == v) return true;
        s->ov_vol_bar0_value = v;
        break;
    case FNOS_DASH_FIELD_OvVolBar1Value:
        if (s->ov_vol_bar1_value == v) return true;
        s->ov_vol_bar1_value = v;
        break;
    case FNOS_DASH_FIELD_OvVolBar2Value:
        if (s->ov_vol_bar2_value == v) return true;
        s->ov_vol_bar2_value = v;
        break;
    case FNOS_DASH_FIELD_OvVolBar3Value:
        if (s->ov_vol_bar3_value == v) return true;
        s->ov_vol_bar3_value = v;
        break;
    case FNOS_DASH_FIELD_OvVolBar4Value:
        if (s->ov_vol_bar4_value == v) return true;
        s->ov_vol_bar4_value = v;
        break;
    case FNOS_DASH_FIELD_OvVolBar5Value:
        if (s->ov_vol_bar5_value == v) return true;
        s->ov_vol_bar5_value = v;
        break;
    case FNOS_DASH_FIELD_OvMbar0Value:
        if (s->ov_mbar0_value == v) return true;
        s->ov_mbar0_value = v;
        break;
    case FNOS_DASH_FIELD_OvMbar1Value:
        if (s->ov_mbar1_value == v) return true;
        s->ov_mbar1_value = v;
        break;
    case FNOS_DASH_FIELD_OvMbar2Value:
        if (s->ov_mbar2_value == v) return true;
        s->ov_mbar2_value = v;
        break;
    case FNOS_DASH_FIELD_StVolBar0Value:
        if (s->st_vol_bar0_value == v) return true;
        s->st_vol_bar0_value = v;
        break;
    case FNOS_DASH_FIELD_StVolBar1Value:
        if (s->st_vol_bar1_value == v) return true;
        s->st_vol_bar1_value = v;
        break;
    case FNOS_DASH_FIELD_StVolBar2Value:
        if (s->st_vol_bar2_value == v) return true;
        s->st_vol_bar2_value = v;
        break;
    case FNOS_DASH_FIELD_StVolBar3Value:
        if (s->st_vol_bar3_value == v) return true;
        s->st_vol_bar3_value = v;
        break;
    case FNOS_DASH_FIELD_StVolBar4Value:
        if (s->st_vol_bar4_value == v) return true;
        s->st_vol_bar4_value = v;
        break;
    case FNOS_DASH_FIELD_StVolBar5Value:
        if (s->st_vol_bar5_value == v) return true;
        s->st_vol_bar5_value = v;
        break;
    case FNOS_DASH_FIELD_NwTrendTop:
        if (s->nw_trend_top == v) return true;
        s->nw_trend_top = v;
        break;
    case FNOS_DASH_FIELD_SyTBar0Value:
        if (s->sy_tbar0_value == v) return true;
        s->sy_tbar0_value = v;
        break;
    case FNOS_DASH_FIELD_SyTBar1Value:
        if (s->sy_tbar1_value == v) return true;
        s->sy_tbar1_value = v;
        break;
    case FNOS_DASH_FIELD_SyTBar2Value:
        if (s->sy_tbar2_value == v) return true;
        s->sy_tbar2_value = v;
        break;
    case FNOS_DASH_FIELD_SyTBar3Value:
        if (s->sy_tbar3_value == v) return true;
        s->sy_tbar3_value = v;
        break;
    case FNOS_DASH_FIELD_SyTBar4Value:
        if (s->sy_tbar4_value == v) return true;
        s->sy_tbar4_value = v;
        break;
    case FNOS_DASH_FIELD_SyTBar5Value:
        if (s->sy_tbar5_value == v) return true;
        s->sy_tbar5_value = v;
        break;
    case FNOS_DASH_FIELD_SyTBar6Value:
        if (s->sy_tbar6_value == v) return true;
        s->sy_tbar6_value = v;
        break;
    case FNOS_DASH_FIELD_StZfsBarValue:
        if (s->st_zfs_bar_value == v) return true;
        s->st_zfs_bar_value = v;
        break;
    case FNOS_DASH_FIELD_StRaidBar0Value:
        if (s->st_raid_bar0_value == v) return true;
        s->st_raid_bar0_value = v;
        break;
    case FNOS_DASH_FIELD_StRaidBar1Value:
        if (s->st_raid_bar1_value == v) return true;
        s->st_raid_bar1_value = v;
        break;
    case FNOS_DASH_FIELD_StRaidBar2Value:
        if (s->st_raid_bar2_value == v) return true;
        s->st_raid_bar2_value = v;
        break;
    case FNOS_DASH_FIELD_StRaidBar3Value:
        if (s->st_raid_bar3_value == v) return true;
        s->st_raid_bar3_value = v;
        break;
    case FNOS_DASH_FIELD_SyTBar7Value:
        if (s->sy_tbar7_value == v) return true;
        s->sy_tbar7_value = v;
        break;
    case FNOS_DASH_FIELD_SyTBar8Value:
        if (s->sy_tbar8_value == v) return true;
        s->sy_tbar8_value = v;
        break;
    case FNOS_DASH_FIELD_SyTBar9Value:
        if (s->sy_tbar9_value == v) return true;
        s->sy_tbar9_value = v;
        break;
    case FNOS_DASH_FIELD_OvTrendTop:
        if (s->ov_trend_top == v) return true;
        s->ov_trend_top = v;
        break;
    case FNOS_DASH_FIELD_SyDockRow0Alpha:
        if (s->sy_dock_row0_alpha == v) return true;
        s->sy_dock_row0_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyDockRow1Alpha:
        if (s->sy_dock_row1_alpha == v) return true;
        s->sy_dock_row1_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyDockRow2Alpha:
        if (s->sy_dock_row2_alpha == v) return true;
        s->sy_dock_row2_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyDockRow3Alpha:
        if (s->sy_dock_row3_alpha == v) return true;
        s->sy_dock_row3_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyDockRow4Alpha:
        if (s->sy_dock_row4_alpha == v) return true;
        s->sy_dock_row4_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyDockRow5Alpha:
        if (s->sy_dock_row5_alpha == v) return true;
        s->sy_dock_row5_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyDockRow6Alpha:
        if (s->sy_dock_row6_alpha == v) return true;
        s->sy_dock_row6_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyDockRow7Alpha:
        if (s->sy_dock_row7_alpha == v) return true;
        s->sy_dock_row7_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow0Alpha:
        if (s->sy_temp_row0_alpha == v) return true;
        s->sy_temp_row0_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow1Alpha:
        if (s->sy_temp_row1_alpha == v) return true;
        s->sy_temp_row1_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow2Alpha:
        if (s->sy_temp_row2_alpha == v) return true;
        s->sy_temp_row2_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow3Alpha:
        if (s->sy_temp_row3_alpha == v) return true;
        s->sy_temp_row3_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow4Alpha:
        if (s->sy_temp_row4_alpha == v) return true;
        s->sy_temp_row4_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow5Alpha:
        if (s->sy_temp_row5_alpha == v) return true;
        s->sy_temp_row5_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow6Alpha:
        if (s->sy_temp_row6_alpha == v) return true;
        s->sy_temp_row6_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow7Alpha:
        if (s->sy_temp_row7_alpha == v) return true;
        s->sy_temp_row7_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow8Alpha:
        if (s->sy_temp_row8_alpha == v) return true;
        s->sy_temp_row8_alpha = v;
        break;
    case FNOS_DASH_FIELD_SyTempRow9Alpha:
        if (s->sy_temp_row9_alpha == v) return true;
        s->sy_temp_row9_alpha = v;
        break;
    default:
        return false;
    }
    s->dirty[(unsigned)f >> 5] |= 1u << ((unsigned)f & 31);
    return true;
}

bool fnos_dash_store_update_bool(fnos_dash_store_t *s, fnos_dash_field_id_t f, bool v)
{
    if ((unsigned)f >= FNOS_DASH_FIELD_COUNT) return false;
    switch (f) {
    default:
        return false;
    }
    s->dirty[(unsigned)f >> 5] |= 1u << ((unsigned)f & 31);
    return true;
}

bool fnos_dash_store_series_push(fnos_dash_store_t *s, fnos_dash_field_id_t f, float v)
{
    switch (f) {
    case FNOS_DASH_FIELD_OvCpuPoints:
        kk_series_push(&s->ov_cpu_points, v);
        break;
    case FNOS_DASH_FIELD_OvMemPoints:
        kk_series_push(&s->ov_mem_points, v);
        break;
    case FNOS_DASH_FIELD_NwMiniDownPoints:
        kk_series_push(&s->nw_mini_down_points, v);
        break;
    case FNOS_DASH_FIELD_NwMiniUpPoints:
        kk_series_push(&s->nw_mini_up_points, v);
        break;
    case FNOS_DASH_FIELD_NwDownTrendPoints:
        kk_series_push(&s->nw_down_trend_points, v);
        break;
    case FNOS_DASH_FIELD_NwUpTrendPoints:
        kk_series_push(&s->nw_up_trend_points, v);
        break;
    case FNOS_DASH_FIELD_OvTempTrendPoints:
        kk_series_push(&s->ov_temp_trend_points, v);
        break;
    default:
        return false;
    }
    s->dirty[(unsigned)f >> 5] |= 1u << ((unsigned)f & 31);
    return true;
}
