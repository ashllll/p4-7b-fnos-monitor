// uk_theme.h — ui_kit 设计令牌（颜色 / 阈值 / 字体 / 度量）
//
// 值沿用 v9/v11 合同（与已归档原型 docs/archive/ui/mockup-v11/css/tokens.css 同源），但**不再推导坐标**：
// 本文件只有"长什么样"的令牌，"摆在哪"由 LVGL flex 决定。
//
// 硬约束（RGB565 实测踩过）：不许渐变——470px 宽的卡上渐变只能量出 7 条色阶，
// 还会出现偏绿/偏蓝横条（用户报过"花花绿绿"）；分层一律靠表面色阶 S0/S1/S2/S3。
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "lvgl.h"
#include "sdkconfig.h"

/* ── 中性 / 表面 ──────────────────────────────────────────────────── */
#define UK_HEX_BG        0x131416   /* 画布                           */
#define UK_HEX_S0        0x0E0F11   /* 嵌槽 / 内凹分组底              */
#define UK_HEX_S1        0x1C1E21   /* 表面 L1：卡片                  */
#define UK_HEX_SURF_T    0x1F2226   /* 卡片表面（原渐变中间调→纯色）  */
#define UK_HEX_S2        0x282B2F   /* 表面 L2：胶囊底、嵌槽          */
#define UK_HEX_S3        0x34383D   /* 表面 L3：容量条轨道            */
#define UK_HEX_OFF       0x42474D   /* 关闭 / 离线                    */
#define UK_HEX_LINE      0x2E3238   /* 分隔线                         */

/* ── 文本 ─────────────────────────────────────────────────────────── */
#define UK_HEX_T1        0xF9FAFA   /* 主文本 / 数字                  */
#define UK_HEX_T2        0xDEE0E3   /* 次级正文                       */
#define UK_HEX_T3        0xB7BCC2   /* 标签 / 行副标题                */
#define UK_HEX_T4        0x8F98A2   /* 轴标签 / 禁用 / 无数据         */

/* ── 品牌与状态 ───────────────────────────────────────────────────── */
#define UK_HEX_BLUE      0x0068F5   /* 主操作填充                     */
#define UK_HEX_BLUE_TXT  0x4797FF   /* 暗底上的蓝色文字               */
#define UK_HEX_OK        0x37BE5F
#define UK_HEX_WARN      0xE79913
#define UK_HEX_DANGER    0xF5777C

/* ── 数据序列 ─────────────────────────────────────────────────────── */
#define UK_HEX_S_CPU     0x4797FF
#define UK_HEX_S_MEM     0x63BDE3
#define UK_HEX_S_TEMP    0xA0BD28
#define UK_HEX_S_DOWN    0x63BDE3
#define UK_HEX_S_UP      0xB47CDE
#define UK_HEX_S_ZFS     0xA0BD28

/* ── 导航（rail） ─────────────────────────────────────────────────── */
#define UK_HEX_RAIL_N    0x141518
#define UK_HEX_RAIL_DAY  0x1C1F22
#define UK_HEX_NAV       0x232629
#define UK_HEX_NAV_SEL   0x272A2D
#define UK_NAV_SEL_OPA   54
#define UK_EDGE_RAIL     20
#define UK_EDGE_OPA      28         /* Quiet edges distinguish adjacent product surfaces */
#define UK_EDGE_STRONG   45         /* 焦点 / 选中卡片（18% 白）      */
#define UK_EDGE_CTRL     64         /* 按钮边界（25% 白）             */
#define UK_FOCUS_RING_OPA 180
#define UK_HAIRLINE_HEX  "#F9FAFA12"
#define UK_ROW_TINT_OPA  LV_OPA_20  /* 清单按相同语义使用同一表面     */
#define UK_NIGHT_MIX     LV_OPA_50
#define UK_NIGHT_LIGHT_MIX LV_OPA_70 /* 浅色摘要转为深色底 + 亮正文   */

/* UI reference ranges are configured independently of collector health events. */
#define UK_PCT_WARM      ((float)CONFIG_FNOS_UI_USAGE_WARM)
#define UK_PCT_FULL      ((float)CONFIG_FNOS_UI_USAGE_HIGH)
#define UK_TEMP_WARM     ((float)CONFIG_FNOS_UI_TEMP_WARM)
#define UK_TEMP_DANGER   ((float)CONFIG_FNOS_UI_TEMP_HIGH)
#define UK_LIST_MIN_WIDTH CONFIG_FNOS_UI_LIST_MIN_WIDTH

/* ── 度量 ─────────────────────────────────────────────────────────── */
#define UK_RADIUS        (g_uk_pal.radius)
#define UK_RADIUS_SM     (g_uk_pal.radius_sm)
#define UK_FLAT_ROWS     (g_uk_pal.flat_rows)
#define UK_BAR_H         8          /* 阈值条高                       */
#define UK_BAR_H_HERO    10          /* 主容量条高（michka 磁盘轨 9，取栅格 10） */
/* Continuous telemetry shares one damped response; native animation styles
   retain the duration for callers that inspect it. */
#define UK_BAR_ANIM_MS   UK_NUMBER_MS
#define UK_SCR_W         1024
#define UK_SCR_H         600
#define UK_RAIL_W        76         /* 左导航宽（中文标签，令牌 44 是桌面值） */
#define UK_HEAD_H        44
#define UK_DOCK_H        56
#define UK_NAV_ITEM_H    64         /* Icon + Chinese label + touch padding */
#define UK_NAV_ICON_SIZE 24         /* Navigation glyph canvas; geometry scales within it */
#define UK_NAV_FEEDBACK_MS 120
#define UK_NAV_ICON_STROKE 2        /* Shared outline weight at the nominal canvas size */
#ifndef CONFIG_FNOS_UI_TREND_SMOOTH_SAMPLES
#define CONFIG_FNOS_UI_TREND_SMOOTH_SAMPLES 3
#endif
#define UK_TREND_SMOOTH_SAMPLES CONFIG_FNOS_UI_TREND_SMOOTH_SAMPLES
#define UK_FLOW_SMOOTH_SAMPLES CONFIG_FNOS_UI_FLOW_SMOOTH_SAMPLES
#ifndef CONFIG_FNOS_UI_TREND_SCROLL_MS
#define CONFIG_FNOS_UI_TREND_SCROLL_MS 2000
#endif
#ifndef CONFIG_FNOS_UI_TREND_FPS
#define CONFIG_FNOS_UI_TREND_FPS 60
#endif
#define UK_TREND_SCROLL_MS CONFIG_FNOS_UI_TREND_SCROLL_MS
#define UK_TREND_FPS CONFIG_FNOS_UI_TREND_FPS
#define UK_TREND_STROKE 2
#define UK_ENTITY_MIN_WIDTH (3 * UK_LIST_MIN_WIDTH / 2) /* Identity + metric comparison columns */
#define UK_DETAIL_WIDTH (2 * UK_LIST_MIN_WIDTH)
#define UK_CONTENT_COLUMNS 3       /* Preferred comparison columns before rail collapses */
#define UK_PANEL_GAP     4
#define UK_HERO_GROW     2
#define UK_AUX_GROW      1
#define UK_MOTION_MS     160
#define UK_MOTION_MIN_MS 80
#define UK_MOTION_DAMPING 10.0f /* critical damping: residual below one logical pixel */
#ifndef CONFIG_FNOS_UI_NUMBER_MS
#define CONFIG_FNOS_UI_NUMBER_MS 600
#endif
#ifndef CONFIG_FNOS_UI_NUMBER_PRESENT_MS
#define CONFIG_FNOS_UI_NUMBER_PRESENT_MS 600
#endif
#ifndef CONFIG_FNOS_UI_NUMBER_STAGGER_MS
#define CONFIG_FNOS_UI_NUMBER_STAGGER_MS 0
#endif
#ifndef CONFIG_FNOS_UI_NUMBER_MAX_STAGGER_MS
#define CONFIG_FNOS_UI_NUMBER_MAX_STAGGER_MS 0
#endif
#ifndef CONFIG_FNOS_UI_NUMBER_TRAVEL_PCT
#define CONFIG_FNOS_UI_NUMBER_TRAVEL_PCT 4
#endif
#ifndef CONFIG_FNOS_UI_NUMBER_BLUR_PCT
#define CONFIG_FNOS_UI_NUMBER_BLUR_PCT 0
#endif
#ifndef CONFIG_FNOS_UI_NUMBER_BUDGET_BYTES
#define CONFIG_FNOS_UI_NUMBER_BUDGET_BYTES 131072
#endif
/* Fixed-point native widget values retain sub-percent movement. */
#define UK_METER_SCALE 100
#ifndef CONFIG_FNOS_UI_TELEMETRY_DAMPING
#define CONFIG_FNOS_UI_TELEMETRY_DAMPING 8
#endif
#define UK_TELEMETRY_DAMPING CONFIG_FNOS_UI_TELEMETRY_DAMPING
#ifndef CONFIG_FNOS_UI_TELEMETRY_RATIO_PERMILLE
#define CONFIG_FNOS_UI_TELEMETRY_RATIO_PERMILLE 1000
#endif
#define UK_TELEMETRY_RATIO (CONFIG_FNOS_UI_TELEMETRY_RATIO_PERMILLE / 1000.f)
#define UK_NUMBER_BUDGET_BYTES CONFIG_FNOS_UI_NUMBER_BUDGET_BYTES
#define UK_NUMBER_MS CONFIG_FNOS_UI_NUMBER_MS
#define UK_NUMBER_PRESENT_MS CONFIG_FNOS_UI_NUMBER_PRESENT_MS
#define UK_NUMBER_STAGGER_MS CONFIG_FNOS_UI_NUMBER_STAGGER_MS
#define UK_NUMBER_MAX_STAGGER_MS CONFIG_FNOS_UI_NUMBER_MAX_STAGGER_MS
#define UK_NUMBER_TRAVEL_PCT CONFIG_FNOS_UI_NUMBER_TRAVEL_PCT
#define UK_NUMBER_BLUR_PCT CONFIG_FNOS_UI_NUMBER_BLUR_PCT
#ifdef CONFIG_FNOS_UI_REDUCED_MOTION
#define UK_NUMBER_REDUCED 1
#else
#define UK_NUMBER_REDUCED 0
#endif
#define UK_GESTURE_POLL_MS 8
#define UK_STAGGER_MS    35
#define UK_REVEAL_MS     150
#define UK_SWIPE_LOCK_PCT 1
#define UK_SWIPE_COMMIT_PCT 24
#define UK_SWIPE_FLICK_PCT 60 /* viewport percent per second */
#define UK_ROW_H         44         /* 清单行基准高（装不下时长高）   */
#define UK_ROW_MIN       UK_ROW_H   /* Rows and controls share the touch floor. */
/* 清单可读性下限：清单容器至少完整露出几行才算"看得清"（见 uk.h 的
   uk_list_readable_min）。这里是**行数**不是像素 —— 行高一律实测。
   没有这道下限时，"这卡能显示几行"全靠每个调用点手算常量：p1 的三张卡
   按 UK_ROW_H 手算成一行（池 55px），首页卷池更算成 21px（比一行还矮）。 */
#define UK_LIST_MIN_ROWS 2
#define UK_DOT           8
#define UK_SCROLL_W      4
#define UK_SCROLL_INSET  12
#define UK_GLOW_W        0          /* Status dots remain flat at every severity */
#define UK_GLOW_OPA      LV_OPA_TRANSP

/* ── 字体（tools/gen_fonts.sh 生成的子集字库；行高才是真实占位） ───── */
#define UK_FONT_CJK_12   (&ui_font_cjk_12)
#define UK_FONT_CJK_16   (&ui_font_cjk_16)
#define UK_FONT_CJK_20   (&ui_font_cjk_20)
#define UK_FONT_NUM_20   (&ui_font_num_20)
#define UK_FONT_NUM_32   (&ui_font_num_32)
#define UK_FONT_NUM_44   (&ui_font_num_44)
#define UK_FONT_TXT_12   (&ui_font_txt_12)
#define UK_FONT_TXT_15   (&ui_font_txt_15)
#define UK_FONT_DISPLAY_64 (&ui_font_display_64)
#define UK_FONT_DISPLAY_96 (&ui_font_display_96)

/* Semantic typography: choose a role, never a page-specific numeric size. */
#define UK_TYPE_HEADING  UK_FONT_CJK_20
#define UK_TYPE_TITLE    UK_FONT_CJK_16
#define UK_TYPE_BODY     UK_FONT_CJK_16
#define UK_TYPE_CAPTION  UK_FONT_CJK_12
#define UK_TYPE_METRIC   UK_FONT_NUM_32
#define UK_TYPE_READING  UK_FONT_NUM_20
#define UK_TYPE_UNIT     UK_FONT_TXT_12

#include "fnos_fonts.h"   /* LV_FONT_DECLARE(ui_font_*) */

/* Runtime themes, including cuktech-console, are listed in uk_theme.c. */
typedef struct {
    uint32_t bg, s0, s1, surf_t, s2, s3, off, line;
    uint32_t t1, t2, t3, t4;
    uint32_t blue, blue_txt, ok, warn, danger;
    uint32_t s_cpu, s_mem, s_temp, s_down, s_up, s_zfs;
    uint32_t rail_n, rail_day, nav, nav_sel;
    uint32_t resource_primary, resource_secondary, summary, on_summary, on_primary, on_danger;
    uint32_t usage_low, usage_warn, usage_high;
    bool summary_light;
    uint8_t radius, radius_sm;
    bool flat_rows; /* Console lists use separators rather than nested tiles. */
} uk_pal_t;

extern uk_pal_t g_uk_pal;

#define UK_BG        (g_uk_pal.bg)
#define UK_S0        (g_uk_pal.s0)
#define UK_SURF_S1   (g_uk_pal.s1)
#define UK_SURF_T    (g_uk_pal.surf_t)
#define UK_SURF_S2   (g_uk_pal.s2)
#define UK_SURF_S3   (g_uk_pal.s3)
#define UK_OFF       (g_uk_pal.off)
#define UK_LINE      (g_uk_pal.line)
#define UK_T1        (g_uk_pal.t1)
#define UK_T2        (g_uk_pal.t2)
#define UK_T3        (g_uk_pal.t3)
#define UK_T4        (g_uk_pal.t4)
#define UK_BLUE      (g_uk_pal.blue)
#define UK_BLUE_TXT  (g_uk_pal.blue_txt)
#define UK_OK        (g_uk_pal.ok)
#define UK_WARN      (g_uk_pal.warn)
#define UK_DANGER    (g_uk_pal.danger)
#define UK_S_CPU     (g_uk_pal.s_cpu)
#define UK_S_MEM     (g_uk_pal.s_mem)
#define UK_S_TEMP    (g_uk_pal.s_temp)
#define UK_S_DOWN    (g_uk_pal.s_down)
#define UK_S_UP      (g_uk_pal.s_up)
#define UK_S_ZFS     (g_uk_pal.s_zfs)
#define UK_RAIL_N    (g_uk_pal.rail_n)
#define UK_RAIL_DAY  (g_uk_pal.rail_day)
#define UK_NAV       (g_uk_pal.nav)
#define UK_NAV_SEL   (g_uk_pal.nav_sel)
#define UK_RESOURCE_PRIMARY      (g_uk_pal.resource_primary)
#define UK_RESOURCE_SECONDARY      (g_uk_pal.resource_secondary)
#define UK_SUMMARY     (g_uk_pal.summary)
#define UK_ON_SUMMARY       (g_uk_pal.on_summary)
#define UK_ON_PRIMARY (g_uk_pal.on_primary)
#define UK_ON_DANGER  (g_uk_pal.on_danger)
#define UK_USAGE_LOW  (g_uk_pal.usage_low)
#define UK_USAGE_WARN (g_uk_pal.usage_warn)
#define UK_USAGE_HIGH (g_uk_pal.usage_high)

#ifdef __cplusplus
extern "C" {
#endif
/* 这两条被 main.cpp（C++）调用；名字沿用 kk_ui 时代（避免改 main.cpp），
   实现现在就在 uk_theme.c（kk_ui 整目录已删）。 */
void fnos_ui_theme_use(const char *name);
const char *fnos_ui_theme_name(void);
void uk_theme_use(const char *name);
const char *uk_theme_name(void);
/* 令牌 → lv_color_t（LVGL 9 起 lv_color_hex 已可直用，这里只做统一入口）。 */
static inline lv_color_t uk_c(uint32_t hex) { return lv_color_hex(hex); }
/* 运行时“精简动效”开关：诊断页可以切；编译期 Kconfig 仍然是默认/强制。 */
void uk_motion_set_reduced(bool reduced);
bool uk_motion_reduced(void);
#ifdef __cplusplus
}
#endif
