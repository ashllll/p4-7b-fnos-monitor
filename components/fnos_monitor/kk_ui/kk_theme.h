#pragma once
// v6 UniFi 设计令牌（LVGL 移植）。设计合同见 docs/ui-unifi-v6.md §2/§4。
// 页面代码只允许用这里的令牌；layout.json 里出现的十六进制必须能在本文件找到对应项。
//
// 与 v5（KK_UI_UMG）的关系：颜色/形状全部换血，结构契约不变。
//   v5 深蓝分层 + 内嵌标题条 + 左侧色条 + 16px 圆角 + 装饰渐变 → 全部作废。

#include <stdint.h>
#include "lvgl.h"

/* ── 画布与表面：靠"表面色阶"分层，不靠边框也不靠阴影 ───────────── */
#define KK_HEX_BG        0x131416   /* 画布 / 内容区底          neutral-00 */
#define KK_HEX_S1        0x1C1E21   /* 表面 L1：卡片、图标栏     neutral-01 */
#define KK_HEX_S2        0x282B2F   /* 表面 L2：胶囊底、嵌槽     neutral-02 */
#define KK_HEX_S3        0x34383D   /* 表面 L3：容量条轨道       neutral-03 */
#define KK_HEX_OFF       0x42474D   /* 关闭 / 离线              neutral-04 */

/* ── 文本四级 ─────────────────────────────────────────────────── */
#define KK_HEX_T1        0xF9FAFA   /* 主文本 / 数字            neutral-12 */
#define KK_HEX_T2        0xDEE0E3   /* 次级正文                 neutral-10 */
#define KK_HEX_T3        0xB7BCC2   /* 标签 / 行副标题 / 未选中图标 neutral-08 */
#define KK_HEX_T4        0x737C87   /* 轴标签 / 禁用 / 无数据    neutral-06 */

/* ── 交互色：品牌蓝只表示"可交互 / 选中"，绝不用来表示状态 ───────── */
#define KK_HEX_BLUE      0x006EFF   /* 填充块、选中指示条                  */
#define KK_HEX_BLUE_TXT  0x4797FF   /* 暗底上的蓝色文字                    */

/* ── 状态色（语义排他：绿=健康 橙=提示 红=故障 中性=关闭）─────────── */
#define KK_HEX_OK        0x37BE5F
#define KK_HEX_WARN      0xE79913
#define KK_HEX_DANGER    0xEE6368

/* ── 数据序列色（图表与迷你趋势；只出现在线条/面积/点上）─────────── */
#define KK_HEX_S_CPU     0x4797FF
#define KK_HEX_S_MEM     0x63BDE3   /* aqua  */
#define KK_HEX_S_TEMP    0xA0BD28   /* lime  */
#define KK_HEX_S_DOWN    0x63BDE3   /* --color-data-down */
#define KK_HEX_S_UP      0xB47CDE   /* --color-data-up   */
#define KK_HEX_S_ZFS     0xA0BD28

/* ── 分级阈值（容量条 / 热条 / 刻度线共用同一档位）─────────────── */
#define KK_BAR_WARM  60.0f      /* ≥60：绿 → 橙 */
#define KK_BAR_FULL  85.0f      /* ≥85：橙 → 红 */

/* ── 度量（令牌 appheader-standard / radius-medium / spacing-base-04）─ */
#define KK_SCR_W       1024
#define KK_SCR_H       600
#define KK_RAIL_W      76       /* 左图标栏宽（令牌 44 是桌面无标签态，本板要中文标签）*/
#define KK_HEAD_H      56       /* 页头高 */
#define KK_ROW_H       40       /* 清单行高（density-large）*/
#define KK_ROW_H_TIGHT 24       /* 紧凑行（温度）*/
#define KK_STATUS_H    32       /* 状态行 */
#define KK_RADIUS      8        /* 卡片圆角 radius-medium —— 不许 >8 */
#define KK_RADIUS_SM   4        /* 胶囊 / 按钮 radius-base */
#define KK_RADIUS_PILL 999      /* 圆形（点、色环）*/
#define KK_BAR_H       4        /* 容量条高 */
#define KK_BAR_H_HERO  8        /* 主容量条高 */
/* 列表行：行高 60（不是常见表格行高 44）—— 这一屏的行里有**三样**东西
   （主行 cjk_16 / 副行 cjk_12 / 可选进度条），44 装不下；硬塞会让副行贴条、
   条压行边。行内位移一律取 kk_layout.h 的 KK_ROW_*，构件层不许再写字面量。 */
/* 滚动条（列表卡片独占竖条）。LVGL 只在绘制时用 LV_PART_SCROLLBAR 的
   pad_right 决定条子右缘（不参与 MAIN 布局），所以条子固定内缩 KK_SCROLL_INSET；
   列表内容的右界则由调用方按 KK_SCROLL_GAP 收窄，保证右对齐数值/状态
   与条子之间永远留出这道净间隙（2026-10-05：内容占满滚动区时只剩 1px）。 */
#define KK_SCROLL_W     4       /* 滚动条粗细 */
#define KK_SCROLL_INSET 12      /* 条子右缘距滚动区右缘（= 卡片内边距内的留白） */
#define KK_SCROLL_GAP   (KK_SCROLL_INSET + 12)  /* 内容右界距滚动区右缘：条子左缘外留 12 净留白 */
#define KK_HAIRLINE    1
#define KK_DOT         8        /* 状态点直径 */

/* 发丝线颜色（--color-divider = rgba(249,250,250,.07)），带 alpha 的 8 位写法 */
#define KK_HAIRLINE_HEX "#F9FAFA12"

/* ══ v8：仪器感（科技风）令牌 ═══════════════════════════════════════
   试片见 tools/preview/style_proof.c（同款 Kconfig + RGB565 出图），
   结论：渐变 + 发丝边 + 发光在 LVGL 软渲染与 565 量化下都成立，
   但 1px 发丝边的白色不透明度必须 >=10% 才看得出（7% 量化后消失）。 */

/* 表面：S0 比画布更深，专门做"嵌槽"（分组内凹、指标格底、迷你图底） */
#define KK_HEX_S0        0x0E0F11   /* 嵌槽 / 内凹分组底 */

/* 卡片"微浮起"：竖向极淡渐变（顶亮底暗），替代阴影做层次 */
#define KK_HEX_SURF_T    0x1F2226   /* 卡片表面（原渐变的中间调，改纯色后取中值） */
#define KK_HEX_SURF_B    0x17191C   /* 已退役：渐变在这块屏幕上会出色带（保留以免别处引用报错） */

/* 内部结构线：比 KK_HAIRLINE_HEX 更亮一档，用于卡头下沿与密集区行分隔 */
#define KK_HEX_LINE      0x2E3238

/* 发丝边（卡片外沿）：7% 会被 RGB565 吃掉，10% 是下限，18% 明确可见 */
#define KK_EDGE_OPA   26        /* 10% 白：默认卡片外沿 */
#define KK_EDGE_STRONG 45       /* 18% 白：焦点卡片 / 选中卡片 */
/* 焦点环：比 KK_EDGE_STRONG 再亮一档，专给"当前所在/当前要按的那个"。
   交互件靠四样东西冗余编码"选中"，缺一样在 169PPI 的暗屏上都会含糊：
   左侧蓝条（位置）+ 白描边（边界）+ 高亮图标与文字（内容）+ 表面亮度差（面）。 */
#define KK_FOCUS_RING_OPA 87    /* 34% 白 */
/* 光晕：LVGL 的 shadow 是"均匀扩展 + 边缘渐隐"，0 偏移即光晕。
   宽度取 2×半径，发光件直径 8~12px 时 w=10~14 刚好一圈。 */
#define KK_GLOW_W     10
#define KK_GLOW_OPA   140

/* ── 密度：紧凑行高台阶（清单行 40 → 指标行 54 → 密集行 44）──────── */
#define KK_ROW_H_METRIC 54      /* 指标格：标签 + 值 + 单位 + 迷你图 */
#define KK_ROW_H_DENSE  44      /* 密集行：标签 + 值 + 副信息，无迷你图 */

/* ══ v8：左导航栏的模块化分层 ═════════════════════════════════════
   问题：整条 rail 一个纯色 KK_S1、导航项透明背景、标签 KK_T3 —— 模块之间
   没有任何边界，眼扫过去是一列糊在一起的灰字。
   做法（沿用"表面色阶分层"的约定，不引入新的表现手法）：
     rail 本身最深（内凹）→ 导航项抬到 KK_S1 档 → 选中再抬一档并叠品牌蓝。 */
/* 表面一律纯色：**这套屏幕上不许用渐变**。RGB565 只有 5/6/5 位，两点颜色相差
   16 级亮度时，470px 的卡片上只量得出 7 条色阶，而相邻色阶的舍入误差落在不同通道
   （阶差依次 −G、−B、−R…），结果是屏幕上出现一条条偏绿/偏蓝/偏红的横条 ——
   用户报的"花花绿绿"就是它。LVGL 渐变不做抖动，565 也没给抖动留余量。
   分层改为靠表面色阶：rail 最深 → 导航项浮起 → 选中再抬一档 + 叠蓝。 */
#define KK_HEX_RAIL_N     0x141518   /* 夜间 rail 底 */
#define KK_HEX_RAIL_DAY   0x1C1F22   /* 白天 rail 底（比卡片深一档 = 内凹槽） */
#define KK_HEX_NAV        0x232629   /* 导航项（比 rail 亮 = 浮起） */
#define KK_HEX_NAV_SEL    0x272A2D   /* 选中项（再抬一档） */
/* 选中项的蓝叠加：半透明填充，不遮住上面的图标与文字（KK_BLUE_TXT 仍读得清）。
   叠加色若换成不透明蓝，内容区会很刺眼，故固定为低不透明度。 */
#define KK_NAV_SEL_OPA 54        /* 21% 品牌蓝 */
#define KK_EDGE_RAIL   20        /* rail 右沿发丝边（8% 白：只做边界提示） */
/* 交互件（导航项/按钮）的边界：8% 白在 565 下太弱，"看得出这是一块能点的东西"
   比"优雅"重要 —— 交互件统一用 25%。 */
#define KK_EDGE_CTRL   64        /* 25% 白 */

/* ══ v9：版面合同 ══════════════════════════════════════════════════
   全部几何常量搬去 kk_layout.h（那是唯一来源）。这里只补字号阶梯，因为
   字号与"行高"绑死，而**行高才是版面的真实高度**（字号只是名义值）。
   实测行高（取自 components/fnos_monitor/fonts 下生成的字体 .c，改字号必须回来更新这张表。
   行高 ≠ 字号：lv_font_conv 没有 --line-height 选项，版面高度按**行高**算）：
     cjk_20 → 23px   cjk_16 → 19px   cjk_12 → 14px
     num_44 → 52px   num_32 → 39px   num_20 → 24px
     txt_15 → 18px   txt_12 → 15px
   v8 的旧阶梯（cjk_15 行高 19 / cjk_18 行高 21）只差 2px，同一行的两块文字
   永远差 1~2px 对不齐 —— 这就是"错位感"的来源之一，已整档换掉。 */

/* ══ 调色板（2026-10-06）：把颜色令牌变成**运行时**可换的一张表 ══════════
   动机：换配色方案不该动 200 处调用点，更不该让每套方案各留一份 fnos_ui.c。
   做法：颜色令牌的**名字不变**（下面仍是 KK_T1 这样的宏），只是改成读 g_kk_pal
   的字段。因此 "换一套配色" = 改 g_kk_pal，而不是改调用点。
   设备端在 app_main 早期调 fnos_ui_theme_use("abyss")（或留空用编译期默认）；
   主机预览用 KK_PALETTE=<name> 环境变量选，用于出对比图。 */
typedef struct {
    uint32_t bg_S1;     /* KK_BG */
    uint32_t S1;        /* KK_S1 */
    uint32_t S2;        /* KK_S2 */
    uint32_t S3;        /* KK_S3 */
    uint32_t s_off;     /* KK_OFF */
    uint32_t t1;        /* KK_T1 */
    uint32_t t2;        /* KK_T2 */
    uint32_t t3;        /* KK_T3 */
    uint32_t t4;        /* KK_T4 */
    uint32_t blue;      /* KK_BLUE */
    uint32_t blue_txt;  /* KK_BLUE_TXT */
    uint32_t ok;        /* KK_OK */
    uint32_t warn;      /* KK_WARN */
    uint32_t danger;    /* KK_DANGER */
    uint32_t s_cpu;     /* KK_S_CPU */
    uint32_t s_mem;     /* KK_S_MEM */
    uint32_t s_temp;    /* KK_S_TEMP */
    uint32_t s_down;    /* KK_S_DOWN */
    uint32_t s_up;      /* KK_S_UP */
    uint32_t s_zfs;     /* KK_S_ZFS */
    uint32_t s0;        /* KK_S0 */
    uint32_t surf_t;    /* KK_SURF_T */
    uint32_t surf_b;    /* KK_SURF_B */
    uint32_t line;      /* KK_LINE */
    uint32_t rail_n;    /* KK_RAIL_N */
    uint32_t rail_day;  /* KK_RAIL_DAY */
    uint32_t nav;       /* KK_NAV */
    uint32_t nav_sel;   /* KK_NAV_SEL */
} kk_palette_t;

/* 预置配色在 kk_theme.c：graphite（默认，中性石墨蓝灰）/ abyss（深空蓝）/
   phosphor（近黑 + 磷光绿）。选哪套由用户定，几何一律不变。 */
extern kk_palette_t g_kk_pal;
/* 这两个函数会被 main.cpp（C++）调用：不写 extern "C" 链接期就是
   `undefined reference to fnos_ui_theme_use(char const*)` —— C++ 会按名字修饰去找。
   本文件仍要能被纯 C 包含（kk_widgets.c / fnos_ui.c），所以用条件宏而不是直接 extern "C"。 */
#ifdef __cplusplus
extern "C" {
#endif
void fnos_ui_theme_use(const char *name);     /* 名字不认识就退回默认，不报错 */
const char *fnos_ui_theme_name(void);
#ifdef __cplusplus
}
#endif

#define KK_BG         (g_kk_pal.bg_S1)
#define KK_S1         (g_kk_pal.S1)
#define KK_S2         (g_kk_pal.S2)
#define KK_S3         (g_kk_pal.S3)
#define KK_OFF        (g_kk_pal.s_off)
#define KK_T1         (g_kk_pal.t1)
#define KK_T2         (g_kk_pal.t2)
#define KK_T3         (g_kk_pal.t3)
#define KK_T4         (g_kk_pal.t4)
#define KK_BLUE       (g_kk_pal.blue)
#define KK_BLUE_TXT   (g_kk_pal.blue_txt)
#define KK_OK         (g_kk_pal.ok)
#define KK_WARN       (g_kk_pal.warn)
#define KK_DANGER     (g_kk_pal.danger)
#define KK_S_CPU      (g_kk_pal.s_cpu)
#define KK_S_MEM      (g_kk_pal.s_mem)
#define KK_S_TEMP     (g_kk_pal.s_temp)
#define KK_S_DOWN     (g_kk_pal.s_down)
#define KK_S_UP       (g_kk_pal.s_up)
#define KK_S_ZFS      (g_kk_pal.s_zfs)
#define KK_S0         (g_kk_pal.s0)
#define KK_SURF_T     (g_kk_pal.surf_t)
#define KK_SURF_B     (g_kk_pal.surf_b)
#define KK_LINE       (g_kk_pal.line)
#define KK_RAIL_N     (g_kk_pal.rail_n)
#define KK_RAIL_DAY   (g_kk_pal.rail_day)
#define KK_NAV        (g_kk_pal.nav)
#define KK_NAV_SEL    (g_kk_pal.nav_sel)

static inline lv_color_t kk_c(uint32_t rgb) { return lv_color_hex(rgb); }
static inline lv_opa_t kk_opa(uint8_t a) { return (lv_opa_t)a; }
static inline lv_opa_t kk_opa_pct(float pct01)
{
    if (pct01 <= 0.f) return LV_OPA_TRANSP;
    if (pct01 >= 1.f) return LV_OPA_COVER;
    return (lv_opa_t)(pct01 * 255.f + 0.5f);
}
