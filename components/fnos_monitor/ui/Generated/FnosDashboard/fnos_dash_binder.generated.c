/* 由 tools/ui_gen.py 生成，请勿手改。Binder：唯一允许把 Store 写回 LVGL 的地方。 */
#include "fnos_dash_view.generated.h"
#include "fnos_dash_store.generated.h"
#include "kk_theme.h"
#include <stdio.h>
#include <string.h>

void fnos_dash_binder_flush(fnos_dash_view_t *v, fnos_dash_store_t *s)
{
    (void)v;
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_HdHost)) {
        lv_label_set_text(v->hd_host, s->hd_host);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_HdEndpoint)) {
        lv_label_set_text(v->hd_endpoint, s->hd_endpoint);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_HdClock)) {
        lv_label_set_text(v->hd_clock, s->hd_clock);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_HdChipText)) {
        kk_chip_set_text(v->hd_chip, s->hd_chip_text);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_HdChipColor)) {
        kk_chip_set_color(v->hd_chip, kk_color_parse(s->hd_chip_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_HdSignalValue)) {
        kk_signal_set(v->hd_signal, (int8_t)s->hd_signal_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_FtPoll)) {
        lv_label_set_text(v->ft_poll, s->ft_poll);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_FtAlertLbl)) {
        lv_label_set_text(v->ft_alert_lbl, s->ft_alert_lbl);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_FtAlertDotColor)) {
        kk_style_bg(v->ft_alert_dot, kk_color_parse(s->ft_alert_dot_color), kk_opa_parse(s->ft_alert_dot_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_FtAlertLblColor)) {
        lv_obj_set_style_text_color(v->ft_alert_lbl, kk_color_parse(s->ft_alert_lbl_color), 0);
        lv_obj_set_style_text_opa(v->ft_alert_lbl, kk_opa_parse(s->ft_alert_lbl_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVerdict)) {
        lv_label_set_text(v->ov_verdict, s->ov_verdict);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVerdictSub)) {
        lv_label_set_text(v->ov_verdict_sub, s->ov_verdict_sub);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvReason)) {
        lv_label_set_text(v->ov_reason, s->ov_reason);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVerdictColor)) {
        lv_obj_set_style_text_color(v->ov_verdict, kk_color_parse(s->ov_verdict_color), 0);
        lv_obj_set_style_text_opa(v->ov_verdict, kk_opa_parse(s->ov_verdict_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolName0)) {
        lv_label_set_text(v->ov_vol_name0, s->ov_vol_name0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolBar0Value)) {
        kk_bar_set(&v->ov_vol_bar0, s->ov_vol_bar0_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolPct0)) {
        lv_label_set_text(v->ov_vol_pct0, s->ov_vol_pct0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolName1)) {
        lv_label_set_text(v->ov_vol_name1, s->ov_vol_name1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolBar1Value)) {
        kk_bar_set(&v->ov_vol_bar1, s->ov_vol_bar1_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolPct1)) {
        lv_label_set_text(v->ov_vol_pct1, s->ov_vol_pct1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolName2)) {
        lv_label_set_text(v->ov_vol_name2, s->ov_vol_name2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolBar2Value)) {
        kk_bar_set(&v->ov_vol_bar2, s->ov_vol_bar2_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolPct2)) {
        lv_label_set_text(v->ov_vol_pct2, s->ov_vol_pct2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolName3)) {
        lv_label_set_text(v->ov_vol_name3, s->ov_vol_name3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolBar3Value)) {
        kk_bar_set(&v->ov_vol_bar3, s->ov_vol_bar3_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolPct3)) {
        lv_label_set_text(v->ov_vol_pct3, s->ov_vol_pct3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolName4)) {
        lv_label_set_text(v->ov_vol_name4, s->ov_vol_name4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolBar4Value)) {
        kk_bar_set(&v->ov_vol_bar4, s->ov_vol_bar4_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolPct4)) {
        lv_label_set_text(v->ov_vol_pct4, s->ov_vol_pct4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolName5)) {
        lv_label_set_text(v->ov_vol_name5, s->ov_vol_name5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolBar5Value)) {
        kk_bar_set(&v->ov_vol_bar5, s->ov_vol_bar5_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolPct5)) {
        lv_label_set_text(v->ov_vol_pct5, s->ov_vol_pct5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVal0)) {
        lv_label_set_text(v->ov_val0, s->ov_val0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvSub0)) {
        lv_label_set_text(v->ov_sub0, s->ov_sub0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvMbar0Value)) {
        kk_bar_set(&v->ov_mbar0, s->ov_mbar0_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVal1)) {
        lv_label_set_text(v->ov_val1, s->ov_val1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvSub1)) {
        lv_label_set_text(v->ov_sub1, s->ov_sub1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvMbar1Value)) {
        kk_bar_set(&v->ov_mbar1, s->ov_mbar1_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVal2)) {
        lv_label_set_text(v->ov_val2, s->ov_val2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvSub2)) {
        lv_label_set_text(v->ov_sub2, s->ov_sub2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvMbar2Value)) {
        kk_bar_set(&v->ov_mbar2, s->ov_mbar2_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVal3)) {
        lv_label_set_text(v->ov_val3, s->ov_val3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvSub3)) {
        lv_label_set_text(v->ov_sub3, s->ov_sub3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvCpuPoints)) {
        kk_trend_sync_series(&v->ov_cpu, &s->ov_cpu_points);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvMemPoints)) {
        kk_trend_sync_series(&v->ov_mem, &s->ov_mem_points);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StHero)) {
        lv_label_set_text(v->st_hero, s->st_hero);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StUsed)) {
        lv_label_set_text(v->st_used, s->st_used);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StCnt)) {
        lv_label_set_text(v->st_cnt, s->st_cnt);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StFree)) {
        lv_label_set_text(v->st_free, s->st_free);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg0X)) {
        lv_obj_set_x(v->st_seg0, (int32_t)s->st_seg0_x);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg0Width)) {
        lv_obj_set_width(v->st_seg0, (int32_t)s->st_seg0_width);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg0Color)) {
        kk_style_bg(v->st_seg0, kk_color_parse(s->st_seg0_color), kk_opa_parse(s->st_seg0_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg1X)) {
        lv_obj_set_x(v->st_seg1, (int32_t)s->st_seg1_x);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg1Width)) {
        lv_obj_set_width(v->st_seg1, (int32_t)s->st_seg1_width);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg1Color)) {
        kk_style_bg(v->st_seg1, kk_color_parse(s->st_seg1_color), kk_opa_parse(s->st_seg1_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg2X)) {
        lv_obj_set_x(v->st_seg2, (int32_t)s->st_seg2_x);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg2Width)) {
        lv_obj_set_width(v->st_seg2, (int32_t)s->st_seg2_width);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg2Color)) {
        kk_style_bg(v->st_seg2, kk_color_parse(s->st_seg2_color), kk_opa_parse(s->st_seg2_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg3X)) {
        lv_obj_set_x(v->st_seg3, (int32_t)s->st_seg3_x);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg3Width)) {
        lv_obj_set_width(v->st_seg3, (int32_t)s->st_seg3_width);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg3Color)) {
        kk_style_bg(v->st_seg3, kk_color_parse(s->st_seg3_color), kk_opa_parse(s->st_seg3_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg4X)) {
        lv_obj_set_x(v->st_seg4, (int32_t)s->st_seg4_x);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg4Width)) {
        lv_obj_set_width(v->st_seg4, (int32_t)s->st_seg4_width);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg4Color)) {
        kk_style_bg(v->st_seg4, kk_color_parse(s->st_seg4_color), kk_opa_parse(s->st_seg4_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg5X)) {
        lv_obj_set_x(v->st_seg5, (int32_t)s->st_seg5_x);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg5Width)) {
        lv_obj_set_width(v->st_seg5, (int32_t)s->st_seg5_width);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StSeg5Color)) {
        kk_style_bg(v->st_seg5, kk_color_parse(s->st_seg5_color), kk_opa_parse(s->st_seg5_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolName0)) {
        lv_label_set_text(v->st_vol_name0, s->st_vol_name0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolFs0)) {
        lv_label_set_text(v->st_vol_fs0, s->st_vol_fs0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolUse0)) {
        lv_label_set_text(v->st_vol_use0, s->st_vol_use0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolPct0)) {
        lv_label_set_text(v->st_vol_pct0, s->st_vol_pct0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolBar0Value)) {
        kk_bar_set(&v->st_vol_bar0, s->st_vol_bar0_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolName1)) {
        lv_label_set_text(v->st_vol_name1, s->st_vol_name1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolFs1)) {
        lv_label_set_text(v->st_vol_fs1, s->st_vol_fs1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolUse1)) {
        lv_label_set_text(v->st_vol_use1, s->st_vol_use1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolPct1)) {
        lv_label_set_text(v->st_vol_pct1, s->st_vol_pct1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolBar1Value)) {
        kk_bar_set(&v->st_vol_bar1, s->st_vol_bar1_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolName2)) {
        lv_label_set_text(v->st_vol_name2, s->st_vol_name2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolFs2)) {
        lv_label_set_text(v->st_vol_fs2, s->st_vol_fs2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolUse2)) {
        lv_label_set_text(v->st_vol_use2, s->st_vol_use2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolPct2)) {
        lv_label_set_text(v->st_vol_pct2, s->st_vol_pct2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolBar2Value)) {
        kk_bar_set(&v->st_vol_bar2, s->st_vol_bar2_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolName3)) {
        lv_label_set_text(v->st_vol_name3, s->st_vol_name3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolFs3)) {
        lv_label_set_text(v->st_vol_fs3, s->st_vol_fs3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolUse3)) {
        lv_label_set_text(v->st_vol_use3, s->st_vol_use3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolPct3)) {
        lv_label_set_text(v->st_vol_pct3, s->st_vol_pct3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolBar3Value)) {
        kk_bar_set(&v->st_vol_bar3, s->st_vol_bar3_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolName4)) {
        lv_label_set_text(v->st_vol_name4, s->st_vol_name4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolFs4)) {
        lv_label_set_text(v->st_vol_fs4, s->st_vol_fs4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolUse4)) {
        lv_label_set_text(v->st_vol_use4, s->st_vol_use4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolPct4)) {
        lv_label_set_text(v->st_vol_pct4, s->st_vol_pct4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolBar4Value)) {
        kk_bar_set(&v->st_vol_bar4, s->st_vol_bar4_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolName5)) {
        lv_label_set_text(v->st_vol_name5, s->st_vol_name5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolFs5)) {
        lv_label_set_text(v->st_vol_fs5, s->st_vol_fs5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolUse5)) {
        lv_label_set_text(v->st_vol_use5, s->st_vol_use5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolPct5)) {
        lv_label_set_text(v->st_vol_pct5, s->st_vol_pct5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StVolBar5Value)) {
        kk_bar_set(&v->st_vol_bar5, s->st_vol_bar5_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidName0)) {
        lv_label_set_text(v->st_raid_name0, s->st_raid_name0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidLvl0)) {
        lv_label_set_text(v->st_raid_lvl0, s->st_raid_lvl0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidState0)) {
        lv_label_set_text(v->st_raid_state0, s->st_raid_state0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidState0Color)) {
        lv_obj_set_style_text_color(v->st_raid_state0, kk_color_parse(s->st_raid_state0_color), 0);
        lv_obj_set_style_text_opa(v->st_raid_state0, kk_opa_parse(s->st_raid_state0_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidName1)) {
        lv_label_set_text(v->st_raid_name1, s->st_raid_name1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidLvl1)) {
        lv_label_set_text(v->st_raid_lvl1, s->st_raid_lvl1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidState1)) {
        lv_label_set_text(v->st_raid_state1, s->st_raid_state1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidState1Color)) {
        lv_obj_set_style_text_color(v->st_raid_state1, kk_color_parse(s->st_raid_state1_color), 0);
        lv_obj_set_style_text_opa(v->st_raid_state1, kk_opa_parse(s->st_raid_state1_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidName2)) {
        lv_label_set_text(v->st_raid_name2, s->st_raid_name2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidLvl2)) {
        lv_label_set_text(v->st_raid_lvl2, s->st_raid_lvl2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidState2)) {
        lv_label_set_text(v->st_raid_state2, s->st_raid_state2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidState2Color)) {
        lv_obj_set_style_text_color(v->st_raid_state2, kk_color_parse(s->st_raid_state2_color), 0);
        lv_obj_set_style_text_opa(v->st_raid_state2, kk_opa_parse(s->st_raid_state2_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidName3)) {
        lv_label_set_text(v->st_raid_name3, s->st_raid_name3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidLvl3)) {
        lv_label_set_text(v->st_raid_lvl3, s->st_raid_lvl3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidState3)) {
        lv_label_set_text(v->st_raid_state3, s->st_raid_state3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidState3Color)) {
        lv_obj_set_style_text_color(v->st_raid_state3, kk_color_parse(s->st_raid_state3_color), 0);
        lv_obj_set_style_text_opa(v->st_raid_state3, kk_opa_parse(s->st_raid_state3_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StIoName0)) {
        lv_label_set_text(v->st_io_name0, s->st_io_name0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StIoVal0)) {
        lv_label_set_text(v->st_io_val0, s->st_io_val0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StIoName1)) {
        lv_label_set_text(v->st_io_name1, s->st_io_name1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StIoVal1)) {
        lv_label_set_text(v->st_io_val1, s->st_io_val1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StIoName2)) {
        lv_label_set_text(v->st_io_name2, s->st_io_name2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StIoVal2)) {
        lv_label_set_text(v->st_io_val2, s->st_io_val2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StIoName3)) {
        lv_label_set_text(v->st_io_name3, s->st_io_name3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StIoVal3)) {
        lv_label_set_text(v->st_io_val3, s->st_io_val3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwDownVal)) {
        lv_label_set_text(v->nw_down_val, s->nw_down_val);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwDownUnit)) {
        lv_label_set_text(v->nw_down_unit, s->nw_down_unit);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwDownSub)) {
        lv_label_set_text(v->nw_down_sub, s->nw_down_sub);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwMiniDownPoints)) {
        kk_trend_sync_series(&v->nw_mini_down, &s->nw_mini_down_points);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwTrendTop)) {
        kk_trend_range(&v->nw_mini_down, s->nw_trend_top);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwUpVal)) {
        lv_label_set_text(v->nw_up_val, s->nw_up_val);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwUpUnit)) {
        lv_label_set_text(v->nw_up_unit, s->nw_up_unit);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwUpSub)) {
        lv_label_set_text(v->nw_up_sub, s->nw_up_sub);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwMiniUpPoints)) {
        kk_trend_sync_series(&v->nw_mini_up, &s->nw_mini_up_points);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwTrendTop)) {
        kk_trend_range(&v->nw_mini_up, s->nw_trend_top);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwDownTrendPoints)) {
        kk_trend_sync_series(&v->nw_down_trend, &s->nw_down_trend_points);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwTrendTop)) {
        kk_trend_range(&v->nw_down_trend, s->nw_trend_top);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwUpTrendPoints)) {
        kk_trend_sync_series(&v->nw_up_trend, &s->nw_up_trend_points);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwTrendTop)) {
        kk_trend_range(&v->nw_up_trend, s->nw_trend_top);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwAxis0)) {
        lv_label_set_text(v->nw_axis0, s->nw_axis0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwAxis1)) {
        lv_label_set_text(v->nw_axis1, s->nw_axis1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwAxis2)) {
        lv_label_set_text(v->nw_axis2, s->nw_axis2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwCtxVal0)) {
        lv_label_set_text(v->nw_ctx_val0, s->nw_ctx_val0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwCtxVal1)) {
        lv_label_set_text(v->nw_ctx_val1, s->nw_ctx_val1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwCtxVal2)) {
        lv_label_set_text(v->nw_ctx_val2, s->nw_ctx_val2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwCtxVal3)) {
        lv_label_set_text(v->nw_ctx_val3, s->nw_ctx_val3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyCnt)) {
        lv_label_set_text(v->sy_cnt, s->sy_cnt);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDot0Color)) {
        kk_style_bg(v->sy_dot0, kk_color_parse(s->sy_dot0_color), kk_opa_parse(s->sy_dot0_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDock0)) {
        lv_label_set_text(v->sy_dock0, s->sy_dock0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState0)) {
        lv_label_set_text(v->sy_dock_state0, s->sy_dock_state0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState0Color)) {
        lv_obj_set_style_text_color(v->sy_dock_state0, kk_color_parse(s->sy_dock_state0_color), 0);
        lv_obj_set_style_text_opa(v->sy_dock_state0, kk_opa_parse(s->sy_dock_state0_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDot1Color)) {
        kk_style_bg(v->sy_dot1, kk_color_parse(s->sy_dot1_color), kk_opa_parse(s->sy_dot1_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDock1)) {
        lv_label_set_text(v->sy_dock1, s->sy_dock1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState1)) {
        lv_label_set_text(v->sy_dock_state1, s->sy_dock_state1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState1Color)) {
        lv_obj_set_style_text_color(v->sy_dock_state1, kk_color_parse(s->sy_dock_state1_color), 0);
        lv_obj_set_style_text_opa(v->sy_dock_state1, kk_opa_parse(s->sy_dock_state1_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDot2Color)) {
        kk_style_bg(v->sy_dot2, kk_color_parse(s->sy_dot2_color), kk_opa_parse(s->sy_dot2_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDock2)) {
        lv_label_set_text(v->sy_dock2, s->sy_dock2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState2)) {
        lv_label_set_text(v->sy_dock_state2, s->sy_dock_state2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState2Color)) {
        lv_obj_set_style_text_color(v->sy_dock_state2, kk_color_parse(s->sy_dock_state2_color), 0);
        lv_obj_set_style_text_opa(v->sy_dock_state2, kk_opa_parse(s->sy_dock_state2_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDot3Color)) {
        kk_style_bg(v->sy_dot3, kk_color_parse(s->sy_dot3_color), kk_opa_parse(s->sy_dot3_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDock3)) {
        lv_label_set_text(v->sy_dock3, s->sy_dock3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState3)) {
        lv_label_set_text(v->sy_dock_state3, s->sy_dock_state3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState3Color)) {
        lv_obj_set_style_text_color(v->sy_dock_state3, kk_color_parse(s->sy_dock_state3_color), 0);
        lv_obj_set_style_text_opa(v->sy_dock_state3, kk_opa_parse(s->sy_dock_state3_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDot4Color)) {
        kk_style_bg(v->sy_dot4, kk_color_parse(s->sy_dot4_color), kk_opa_parse(s->sy_dot4_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDock4)) {
        lv_label_set_text(v->sy_dock4, s->sy_dock4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState4)) {
        lv_label_set_text(v->sy_dock_state4, s->sy_dock_state4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState4Color)) {
        lv_obj_set_style_text_color(v->sy_dock_state4, kk_color_parse(s->sy_dock_state4_color), 0);
        lv_obj_set_style_text_opa(v->sy_dock_state4, kk_opa_parse(s->sy_dock_state4_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDot5Color)) {
        kk_style_bg(v->sy_dot5, kk_color_parse(s->sy_dot5_color), kk_opa_parse(s->sy_dot5_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDock5)) {
        lv_label_set_text(v->sy_dock5, s->sy_dock5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState5)) {
        lv_label_set_text(v->sy_dock_state5, s->sy_dock_state5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState5Color)) {
        lv_obj_set_style_text_color(v->sy_dock_state5, kk_color_parse(s->sy_dock_state5_color), 0);
        lv_obj_set_style_text_opa(v->sy_dock_state5, kk_opa_parse(s->sy_dock_state5_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertDot0Color)) {
        kk_style_bg(v->sy_alert_dot0, kk_color_parse(s->sy_alert_dot0_color), kk_opa_parse(s->sy_alert_dot0_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertLbl0)) {
        lv_label_set_text(v->sy_alert_lbl0, s->sy_alert_lbl0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertLbl0Color)) {
        lv_obj_set_style_text_color(v->sy_alert_lbl0, kk_color_parse(s->sy_alert_lbl0_color), 0);
        lv_obj_set_style_text_opa(v->sy_alert_lbl0, kk_opa_parse(s->sy_alert_lbl0_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertDot1Color)) {
        kk_style_bg(v->sy_alert_dot1, kk_color_parse(s->sy_alert_dot1_color), kk_opa_parse(s->sy_alert_dot1_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertLbl1)) {
        lv_label_set_text(v->sy_alert_lbl1, s->sy_alert_lbl1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertLbl1Color)) {
        lv_obj_set_style_text_color(v->sy_alert_lbl1, kk_color_parse(s->sy_alert_lbl1_color), 0);
        lv_obj_set_style_text_opa(v->sy_alert_lbl1, kk_opa_parse(s->sy_alert_lbl1_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertDot2Color)) {
        kk_style_bg(v->sy_alert_dot2, kk_color_parse(s->sy_alert_dot2_color), kk_opa_parse(s->sy_alert_dot2_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertLbl2)) {
        lv_label_set_text(v->sy_alert_lbl2, s->sy_alert_lbl2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertLbl2Color)) {
        lv_obj_set_style_text_color(v->sy_alert_lbl2, kk_color_parse(s->sy_alert_lbl2_color), 0);
        lv_obj_set_style_text_opa(v->sy_alert_lbl2, kk_opa_parse(s->sy_alert_lbl2_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName0)) {
        lv_label_set_text(v->sy_tname0, s->sy_tname0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal0)) {
        lv_label_set_text(v->sy_tval0, s->sy_tval0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal0Color)) {
        lv_obj_set_style_text_color(v->sy_tval0, kk_color_parse(s->sy_tval0_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval0, kk_opa_parse(s->sy_tval0_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar0Value)) {
        kk_bar_set(&v->sy_tbar0, s->sy_tbar0_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName1)) {
        lv_label_set_text(v->sy_tname1, s->sy_tname1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal1)) {
        lv_label_set_text(v->sy_tval1, s->sy_tval1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal1Color)) {
        lv_obj_set_style_text_color(v->sy_tval1, kk_color_parse(s->sy_tval1_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval1, kk_opa_parse(s->sy_tval1_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar1Value)) {
        kk_bar_set(&v->sy_tbar1, s->sy_tbar1_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName2)) {
        lv_label_set_text(v->sy_tname2, s->sy_tname2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal2)) {
        lv_label_set_text(v->sy_tval2, s->sy_tval2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal2Color)) {
        lv_obj_set_style_text_color(v->sy_tval2, kk_color_parse(s->sy_tval2_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval2, kk_opa_parse(s->sy_tval2_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar2Value)) {
        kk_bar_set(&v->sy_tbar2, s->sy_tbar2_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName3)) {
        lv_label_set_text(v->sy_tname3, s->sy_tname3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal3)) {
        lv_label_set_text(v->sy_tval3, s->sy_tval3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal3Color)) {
        lv_obj_set_style_text_color(v->sy_tval3, kk_color_parse(s->sy_tval3_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval3, kk_opa_parse(s->sy_tval3_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar3Value)) {
        kk_bar_set(&v->sy_tbar3, s->sy_tbar3_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName4)) {
        lv_label_set_text(v->sy_tname4, s->sy_tname4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal4)) {
        lv_label_set_text(v->sy_tval4, s->sy_tval4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal4Color)) {
        lv_obj_set_style_text_color(v->sy_tval4, kk_color_parse(s->sy_tval4_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval4, kk_opa_parse(s->sy_tval4_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar4Value)) {
        kk_bar_set(&v->sy_tbar4, s->sy_tbar4_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName5)) {
        lv_label_set_text(v->sy_tname5, s->sy_tname5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal5)) {
        lv_label_set_text(v->sy_tval5, s->sy_tval5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal5Color)) {
        lv_obj_set_style_text_color(v->sy_tval5, kk_color_parse(s->sy_tval5_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval5, kk_opa_parse(s->sy_tval5_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar5Value)) {
        kk_bar_set(&v->sy_tbar5, s->sy_tbar5_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName6)) {
        lv_label_set_text(v->sy_tname6, s->sy_tname6);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal6)) {
        lv_label_set_text(v->sy_tval6, s->sy_tval6);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal6Color)) {
        lv_obj_set_style_text_color(v->sy_tval6, kk_color_parse(s->sy_tval6_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval6, kk_opa_parse(s->sy_tval6_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar6Value)) {
        kk_bar_set(&v->sy_tbar6, s->sy_tbar6_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal0)) {
        lv_label_set_text(v->sy_kv_val0, s->sy_kv_val0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal1)) {
        lv_label_set_text(v->sy_kv_val1, s->sy_kv_val1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal2)) {
        lv_label_set_text(v->sy_kv_val2, s->sy_kv_val2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal3)) {
        lv_label_set_text(v->sy_kv_val3, s->sy_kv_val3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal4)) {
        lv_label_set_text(v->sy_kv_val4, s->sy_kv_val4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal5)) {
        lv_label_set_text(v->sy_kv_val5, s->sy_kv_val5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal6)) {
        lv_label_set_text(v->sy_kv_val6, s->sy_kv_val6);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal7)) {
        lv_label_set_text(v->sy_kv_val7, s->sy_kv_val7);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvTempTrendPoints)) {
        kk_trend_sync_series(&v->ov_temp_trend, &s->ov_temp_trend_points);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StZfsArc)) {
        lv_label_set_text(v->st_zfs_arc, s->st_zfs_arc);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StZfsHit)) {
        lv_label_set_text(v->st_zfs_hit, s->st_zfs_hit);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StZfsBarValue)) {
        kk_bar_set(&v->st_zfs_bar, s->st_zfs_bar_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidBar0Value)) {
        kk_bar_set(&v->st_raid_bar0, s->st_raid_bar0_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidBar1Value)) {
        kk_bar_set(&v->st_raid_bar1, s->st_raid_bar1_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidBar2Value)) {
        kk_bar_set(&v->st_raid_bar2, s->st_raid_bar2_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StRaidBar3Value)) {
        kk_bar_set(&v->st_raid_bar3, s->st_raid_bar3_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwTotalVal)) {
        lv_label_set_text(v->nw_total_val, s->nw_total_val);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDock6)) {
        lv_label_set_text(v->sy_dock6, s->sy_dock6);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState6)) {
        lv_label_set_text(v->sy_dock_state6, s->sy_dock_state6);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState6Color)) {
        lv_obj_set_style_text_color(v->sy_dock_state6, kk_color_parse(s->sy_dock_state6_color), 0);
        lv_obj_set_style_text_opa(v->sy_dock_state6, kk_opa_parse(s->sy_dock_state6_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDot6Color)) {
        kk_style_bg(v->sy_dot6, kk_color_parse(s->sy_dot6_color), kk_opa_parse(s->sy_dot6_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDock7)) {
        lv_label_set_text(v->sy_dock7, s->sy_dock7);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState7)) {
        lv_label_set_text(v->sy_dock_state7, s->sy_dock_state7);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockState7Color)) {
        lv_obj_set_style_text_color(v->sy_dock_state7, kk_color_parse(s->sy_dock_state7_color), 0);
        lv_obj_set_style_text_opa(v->sy_dock_state7, kk_opa_parse(s->sy_dock_state7_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDot7Color)) {
        kk_style_bg(v->sy_dot7, kk_color_parse(s->sy_dot7_color), kk_opa_parse(s->sy_dot7_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertLbl3)) {
        lv_label_set_text(v->sy_alert_lbl3, s->sy_alert_lbl3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertLbl3Color)) {
        lv_obj_set_style_text_color(v->sy_alert_lbl3, kk_color_parse(s->sy_alert_lbl3_color), 0);
        lv_obj_set_style_text_opa(v->sy_alert_lbl3, kk_opa_parse(s->sy_alert_lbl3_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyAlertDot3Color)) {
        kk_style_bg(v->sy_alert_dot3, kk_color_parse(s->sy_alert_dot3_color), kk_opa_parse(s->sy_alert_dot3_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName7)) {
        lv_label_set_text(v->sy_tname7, s->sy_tname7);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal7)) {
        lv_label_set_text(v->sy_tval7, s->sy_tval7);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal7Color)) {
        lv_obj_set_style_text_color(v->sy_tval7, kk_color_parse(s->sy_tval7_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval7, kk_opa_parse(s->sy_tval7_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar7Value)) {
        kk_bar_set(&v->sy_tbar7, s->sy_tbar7_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName8)) {
        lv_label_set_text(v->sy_tname8, s->sy_tname8);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal8)) {
        lv_label_set_text(v->sy_tval8, s->sy_tval8);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal8Color)) {
        lv_obj_set_style_text_color(v->sy_tval8, kk_color_parse(s->sy_tval8_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval8, kk_opa_parse(s->sy_tval8_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar8Value)) {
        kk_bar_set(&v->sy_tbar8, s->sy_tbar8_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTName9)) {
        lv_label_set_text(v->sy_tname9, s->sy_tname9);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal9)) {
        lv_label_set_text(v->sy_tval9, s->sy_tval9);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTVal9Color)) {
        lv_obj_set_style_text_color(v->sy_tval9, kk_color_parse(s->sy_tval9_color), 0);
        lv_obj_set_style_text_opa(v->sy_tval9, kk_opa_parse(s->sy_tval9_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTBar9Value)) {
        kk_bar_set(&v->sy_tbar9, s->sy_tbar9_value);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal8)) {
        lv_label_set_text(v->sy_kv_val8, s->sy_kv_val8);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal9)) {
        lv_label_set_text(v->sy_kv_val9, s->sy_kv_val9);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal10)) {
        lv_label_set_text(v->sy_kv_val10, s->sy_kv_val10);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyKvVal11)) {
        lv_label_set_text(v->sy_kv_val11, s->sy_kv_val11);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvLegendCpuLbl)) {
        lv_label_set_text(v->ov_legend_cpu_lbl, s->ov_legend_cpu_lbl);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvLegendMemLbl)) {
        lv_label_set_text(v->ov_legend_mem_lbl, s->ov_legend_mem_lbl);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvLegendTempLbl)) {
        lv_label_set_text(v->ov_legend_temp_lbl, s->ov_legend_temp_lbl);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvHealthAccentColor)) {
        kk_style_bg(v->ov_health_accent, kk_color_parse(s->ov_health_accent_color), kk_opa_parse(s->ov_health_accent_color));
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVal0Color)) {
        lv_obj_set_style_text_color(v->ov_val0, kk_color_parse(s->ov_val0_color), 0);
        lv_obj_set_style_text_opa(v->ov_val0, kk_opa_parse(s->ov_val0_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVal1Color)) {
        lv_obj_set_style_text_color(v->ov_val1, kk_color_parse(s->ov_val1_color), 0);
        lv_obj_set_style_text_opa(v->ov_val1, kk_opa_parse(s->ov_val1_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVal2Color)) {
        lv_obj_set_style_text_color(v->ov_val2, kk_color_parse(s->ov_val2_color), 0);
        lv_obj_set_style_text_opa(v->ov_val2, kk_opa_parse(s->ov_val2_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVal3Color)) {
        lv_obj_set_style_text_color(v->ov_val3, kk_color_parse(s->ov_val3_color), 0);
        lv_obj_set_style_text_opa(v->ov_val3, kk_opa_parse(s->ov_val3_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_StHeroColor)) {
        lv_obj_set_style_text_color(v->st_hero, kk_color_parse(s->st_hero_color), 0);
        lv_obj_set_style_text_opa(v->st_hero, kk_opa_parse(s->st_hero_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwDownValColor)) {
        lv_obj_set_style_text_color(v->nw_down_val, kk_color_parse(s->nw_down_val_color), 0);
        lv_obj_set_style_text_opa(v->nw_down_val, kk_opa_parse(s->nw_down_val_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_NwUpValColor)) {
        lv_obj_set_style_text_color(v->nw_up_val, kk_color_parse(s->nw_up_val_color), 0);
        lv_obj_set_style_text_opa(v->nw_up_val, kk_opa_parse(s->nw_up_val_color), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvTrendTop)) {
        kk_trend_range(&v->ov_cpu, s->ov_trend_top);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvTrendTop)) {
        kk_trend_range(&v->ov_mem, s->ov_trend_top);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockRow0Alpha)) {
        lv_obj_set_style_opa(v->sy_dock_row0, kk_opa_pct(s->sy_dock_row0_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockRow1Alpha)) {
        lv_obj_set_style_opa(v->sy_dock_row1, kk_opa_pct(s->sy_dock_row1_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockRow2Alpha)) {
        lv_obj_set_style_opa(v->sy_dock_row2, kk_opa_pct(s->sy_dock_row2_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockRow3Alpha)) {
        lv_obj_set_style_opa(v->sy_dock_row3, kk_opa_pct(s->sy_dock_row3_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockRow4Alpha)) {
        lv_obj_set_style_opa(v->sy_dock_row4, kk_opa_pct(s->sy_dock_row4_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockRow5Alpha)) {
        lv_obj_set_style_opa(v->sy_dock_row5, kk_opa_pct(s->sy_dock_row5_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockRow6Alpha)) {
        lv_obj_set_style_opa(v->sy_dock_row6, kk_opa_pct(s->sy_dock_row6_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyDockRow7Alpha)) {
        lv_obj_set_style_opa(v->sy_dock_row7, kk_opa_pct(s->sy_dock_row7_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow0Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row0, kk_opa_pct(s->sy_temp_row0_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow1Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row1, kk_opa_pct(s->sy_temp_row1_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow2Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row2, kk_opa_pct(s->sy_temp_row2_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow3Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row3, kk_opa_pct(s->sy_temp_row3_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow4Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row4, kk_opa_pct(s->sy_temp_row4_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow5Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row5, kk_opa_pct(s->sy_temp_row5_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow6Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row6, kk_opa_pct(s->sy_temp_row6_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow7Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row7, kk_opa_pct(s->sy_temp_row7_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow8Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row8, kk_opa_pct(s->sy_temp_row8_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_SyTempRow9Alpha)) {
        lv_obj_set_style_opa(v->sy_temp_row9, kk_opa_pct(s->sy_temp_row9_alpha), 0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolUse0)) {
        lv_label_set_text(v->ov_vol_use0, s->ov_vol_use0);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolUse1)) {
        lv_label_set_text(v->ov_vol_use1, s->ov_vol_use1);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolUse2)) {
        lv_label_set_text(v->ov_vol_use2, s->ov_vol_use2);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolUse3)) {
        lv_label_set_text(v->ov_vol_use3, s->ov_vol_use3);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolUse4)) {
        lv_label_set_text(v->ov_vol_use4, s->ov_vol_use4);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvVolUse5)) {
        lv_label_set_text(v->ov_vol_use5, s->ov_vol_use5);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvPeakCpu)) {
        lv_label_set_text(v->ov_peak_cpu, s->ov_peak_cpu);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvPeakMem)) {
        lv_label_set_text(v->ov_peak_mem, s->ov_peak_mem);
    }
    if (fnos_dash_field_dirty(s, FNOS_DASH_FIELD_OvPeakTemp)) {
        lv_label_set_text(v->ov_peak_temp, s->ov_peak_temp);
    }
    memset(s->dirty, 0, sizeof(s->dirty));
}
