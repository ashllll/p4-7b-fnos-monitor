#pragma once
// KK_UI_UMG 视觉令牌（LVGL 移植）。设计合同见 docs/ui-kk.md §3。
// 页面代码只允许用这里的令牌，不允许散落十六进制颜色。
// 结构色与文本层级逐字取自 KK_UI_UMG 样例 layout.json；语义色是数据编码扩展。

#include <stdint.h>
#include "lvgl.h"

/* ── 结构色（KK 样例原色）────────────────────────────────────────── */
#define KK_BG        0x080B10   /* 底：KK Root #080B10CC 实心化          */
#define KK_PANEL     0x161E2B   /* 卡片底：KK #161E2BF2 实心化           */
#define KK_OVERLAY   0x1C2534   /* #FFFFFF10 叠 panel（选中 / 悬浮）     */
#define KK_INSET     0x19212E   /* #FFFFFF08 叠层（次级内层）            */
#define KK_INSET_HI  0x232E3F   /* #FFFFFF0F 叠层（表头 / 内层浮起）     */
#define KK_GUIDE     0x334052   /* 描边 / hairline（KK Button 底色）     */
#define KK_TRACK     0x3A516B   /* 轨道 / 弱元素（KK 次级填充）          */
#define KK_ACCENT    0x2F80ED   /* 强调蓝（KK 唯一强调色）               */

/* ── 文本四级（KK 样例原色）──────────────────────────────────────── */
#define KK_TEXT1     0xFFFFFF   /* 主数值 / 标题                         */
#define KK_TEXT2     0xBFC9D7   /* 标签 / 正文                           */
#define KK_TEXT3     0xD7DEE8   /* 次级正文                              */
#define KK_TEXT4     0xC8D2E0   /* 辅助 / meta                           */
#define KK_IDLE      0x5A6B80   /* 弱化 / 休眠（数据 idle 态，文本层级第 5 级） */

/* ── 语义扩展（数据编码：严重度 + 身份色，饱和度对齐 KK_ACCENT）──── */
#define KK_OK        0x2FBF71
#define KK_WARN      0xF2B01E
#define KK_DANGER    0xF0453D

#define KK_CPU       0x2F80ED
#define KK_MEM       0x4EC9E8
#define KK_TEMP      0xFF7A1A   /* 温度身份色＝橙；不得与 KK_WARN(黄) 同值，否则常态读成告警 */
#define KK_NET_DOWN  0x4EC9E8
#define KK_NET_UP    0x2F80ED
#define KK_ZFS       0xC8E63C

/* 用量分级阈值（Bar 运行时自动分级；容量/温度刻度线共用同一档位） */
#define KK_BAR_WARM  60.0f      /* ≥60：cool → warm */
#define KK_BAR_FULL  85.0f      /* ≥85：warm → full（告警档） */

/* 能量渐变：cool 段 / warm 段（存储占用条用） */
#define KK_GRAD_COOL_A 0x4EC9E8
#define KK_GRAD_COOL_B 0x2F80ED
#define KK_GRAD_COOL_C 0x2FBF71
#define KK_GRAD_WARM_A 0xC8E63C
#define KK_GRAD_WARM_B 0xF2B01E
#define KK_GRAD_WARM_C 0xE8622B

/* ── 形状 / 版面令牌（沿用 v3 实践，KK 无形状令牌）────────────────── */
#define KK_SCR_W     1024
#define KK_SCR_H     600
#define KK_TOP_H     64         /* v4.1 顶栏高度（docs/ui-kk-composition.md §2） */
#define KK_TAB_H     44         /* Tab 行（y 72-116） */
#define KK_BOT_H     36         /* 底栏（y 564-600） */
#define KK_PAD       20
/* 版面节奏三档（v4.2）：行内条带/瓦片 8、卡片组 12、区块 16 */
#define KK_GAP_ROW   8
#define KK_GAP       12
#define KK_GAP_SEC   16
#define KK_RADIUS    16         /* 卡片圆角                              */
#define KK_RADIUS_SM 8
#define KK_BAR_H     4          /* 细条                                  */
#define KK_HAIRLINE  1

static inline lv_color_t kk_c(uint32_t rgb) { return lv_color_hex(rgb); }
static inline lv_opa_t kk_opa(uint8_t a) { return (lv_opa_t)a; }
static inline lv_opa_t kk_opa_pct(float pct01)
{
    if (pct01 <= 0.f) return LV_OPA_TRANSP;
    if (pct01 >= 1.f) return LV_OPA_COVER;
    return (lv_opa_t)(pct01 * 255.f + 0.5f);
}
