// 预置配色（调色板）。结构声明与令牌宏见 kk_theme.h。
//
// 为什么单独一个文件：配色是"工艺参数"，几何是"合同"。分开之后，
//   · 换配色 = 改这张表，调用点一处不动（200+ 处 KK_* 引用自动跟着变）；
//   · 换版面 = 改 kk_layout.h，配色一行不动。
// 这样才不会出现"为了换个颜色顺手改坏了对齐"。
//
// 选色红线（实测，见 docs/ui-redesign-v9.md）：
//   · 卡面 #1F2226 上 T4 #737C87 = 3.77:1、品牌蓝 #006EFF = 3.55:1，低于 WCAG 4.5:1，
//     所以这两支**只能用于 22px 以上的字**；小字一律用 T3(#B7BCC2, 8.35:1) 以上。
//   · 表面只留四级 + 一条分隔线：靠"表面色阶"分层，不靠边框不靠阴影。
//   · **不用渐变**：RGB565 量化会把低对比渐变切成色相不对的横条（用户报过"花花绿绿"）。
//   · 状态色语义排他：绿=健康、橙=提示、红=故障，别的场合一律不许借色。

#include <string.h>
#include "kk_theme.h"

kk_palette_t g_kk_pal;
static const char *s_name = "graphite";

/* ── graphite：默认。中性深灰 + 品牌蓝，最"素"，长时间看不累 ─────────── */
static const kk_palette_t P_GRAPHITE = {
    .bg_S1 = 0x131416, .S1 = 0x1C1E21, .S2 = 0x282B2F, .S3 = 0x34383D, .s_off = 0x42474D,
    .t1 = 0xF9FAFA, .t2 = 0xDEE0E3, .t3 = 0xB7BCC2, .t4 = 0x737C87,
    .blue = 0x006EFF, .blue_txt = 0x4797FF,
    .ok = 0x37BE5F, .warn = 0xE79913, .danger = 0xEE6368,
    .s_cpu = 0x4797FF, .s_mem = 0x63BDE3, .s_temp = 0xA0BD28,
    .s_down = 0x63BDE3, .s_up = 0xB47CDE, .s_zfs = 0xA0BD28,
    .s0 = 0x0E0F11, .surf_t = 0x1F2226, .surf_b = 0x17191C, .line = 0x2E3238,
    .rail_n = 0x141518, .rail_day = 0x1C1F22, .nav = 0x232629, .nav_sel = 0x272A2D,
};

/* ── abyss：深空蓝。背景更蓝更深，卡片是"浮起来的深蓝板"，
      青色做数据主色 —— 像网络运维终端。代价：蓝色系多，状态色要更亮才分得开。 */
static const kk_palette_t P_ABYSS = {
    .bg_S1 = 0x0A1018, .S1 = 0x121A24, .S2 = 0x1B2632, .S3 = 0x27343F, .s_off = 0x3A4A57,
    .t1 = 0xF7FBFF, .t2 = 0xD6E2EC, .t3 = 0xAFC1CF, .t4 = 0x7A8FA0,
    .blue = 0x1E6BFF, .blue_txt = 0x55A0FF,
    .ok = 0x3FD07A, .warn = 0xF0A62B, .danger = 0xFF6B70,
    .s_cpu = 0x35C7F0, .s_mem = 0x7FA8FF, .s_temp = 0xB8D43C,
    .s_down = 0x35C7F0, .s_up = 0xC08BF0, .s_zfs = 0xB8D43C,
    .s0 = 0x060A10, .surf_t = 0x14202C, .surf_b = 0x0D151D, .line = 0x2A3A48,
    .rail_n = 0x0B1219, .rail_day = 0x121A24, .nav = 0x1A2532, .nav_sel = 0x1F2C3B,
};

/* ── phosphor：近黑 + 磷光绿。底最黑、字最亮，代价是"绿=健康"和"绿=交互"都挤在
      同一色域、状态语义会变模糊。交互色（选中条/可点的边）改用琥珀以示区分：
      琥珀只做"可交互/选中"，绝不表示状态，与"绿=健康、橙=提示、红=故障"不冲突。
      注意 KK_BLUE 还兼着左上角 App 图标块的底色 —— 那块上面的白字要 3:1 以上，
      所以它必须仍是**蓝**而不是琥珀（曾把两支都设成琥珀，对比度审计直接判 1.83:1）。 */
static const kk_palette_t P_PHOSPHOR = {
    .bg_S1 = 0x08090A, .S1 = 0x111315, .S2 = 0x1B1F21, .S3 = 0x2A2F31, .s_off = 0x3C4245,
    .t1 = 0xFFFFFF, .t2 = 0xDCE3E0, .t3 = 0xAEB8B4, .t4 = 0x76807C,
    .blue = 0x2B62D9, .blue_txt = 0xFFC44D,
    .ok = 0x3BE07A, .warn = 0xFFB000, .danger = 0xFF5F56,
    .s_cpu = 0x3BE07A, .s_mem = 0x39C6D6, .s_temp = 0xC8E04A,
    .s_down = 0x39C6D6, .s_up = 0xE07AC8, .s_zfs = 0xC8E04A,
    .s0 = 0x040506, .surf_t = 0x141719, .surf_b = 0x0E1012, .line = 0x272C2E,
    .rail_n = 0x0B0D0E, .rail_day = 0x111315, .nav = 0x191C1E, .nav_sel = 0x1E2224,
};

/* ── macos：照 macOS 26/27（Tahoe）深色设计语言的实测数值做的一套。
      数值来源：本机 AppKit 运行时取色（NSColor → CGColor → 位图取样）+ HIG 官方规格表，
      不是"看着像"的近似（调查方法与全部原始值见 docs/ui-redesign-v9.md 第八节）。
      可照搬的：表面色阶（#1E1E1E 内容底 / #282828 页面底 / #3D3D3D 分隔线）、
                文字四级（白 85/55/25/10% 预混）、强调色官方值、圆角 8pt、控件高 22pt。
      必须放弃的：背景模糊、真 alpha 合成（RGB565 上是预混实心色）、Liquid Glass 的折射。
      ⚠ 该体系的 tertiary 文字只有 2.5:1 —— 本工程小字下限是 4.5:1，所以
        .t4（最弱的一支）**不取 tertiary**，取一个并到 secondary 与 tertiary 之间的值，
        否则副行/轴标在 1024×600 上会糊掉（对比度审计会直接判不合格）。 */
static const kk_palette_t P_MACOS = {
    /* 表面：页面底 #1E1E1E 略收暗做最底层；卡片用 #1E1E1E→#282828 的层次 */
    .bg_S1 = 0x161617, .S1 = 0x1E1E1E, .S2 = 0x2C2C2E, .S3 = 0x3A3A3C, .s_off = 0x48484A,
    /* 文字：白 85% / 70% / 55% / 38% 预混在 #1E1E1E 上（85% = #DEDEDE 12.4:1） */
    .t1 = 0xDEDEDE, .t2 = 0xC7C7C7, .t3 = 0x9E9E9E, .t4 = 0x7E7E82,
    /* 强调色：文字蓝取"增强对比"档 #5CB8FF 以便压小字；
       KK_BLUE 还兼着左上角 App 图标块的底色，官方深色强调值 #0091FF 压白字只有
       3.23:1（压 #DEDEDE 正文色更低到 2.40:1 —— 被对比度审计直接拦下），
       所以 ICON 底色用 Apple 无障碍档蓝 #0066CC（压白 5.57:1 / 压正文色 4.14:1），
       与"官方值"的差别只有图标那一块，且在审计的允许范围内。 */
    .blue = 0x0066CC, .blue_txt = 0x5CB8FF,
    .ok = 0x30D158, .warn = 0xFF9230, .danger = 0xFF4245,
    /* 数据序列：直接借用系统强调色（同一套色卡，色相分布均匀） */
    .s_cpu = 0x0091FF, .s_mem = 0x00D2E0, .s_temp = 0x30D158,
    .s_down = 0x00D2E0, .s_up = 0xDB34F2, .s_zfs = 0x30D158,
    /* 嵌槽比内容底再暗一档；卡片微浮起（#1E1E1E → #2A2A2C）；分隔线 #3D3D3D */
    .s0 = 0x121212, .surf_t = 0x1E1E1E, .surf_b = 0x2A2A2C, .line = 0x3D3D3D,
    .rail_n = 0x1A1A1A, .rail_day = 0x222222, .nav = 0x2A2A2C, .nav_sel = 0x363638,
};

/* 开机即把调色板装好：g_kk_pal 是零初始化全局，若没人赋值，所有颜色都是 0（纯黑）。
   用 constructor 而不是"要求调用方记得先调一次" —— 忘了调是黑屏，这种错误必须在
   结构上不可能发生。C（不是 C++）里 attribute((constructor)) 在 IDF 上也生效。 */
__attribute__((constructor)) static void kk_palette_boot(void)
{
    g_kk_pal = P_GRAPHITE;
    s_name = "graphite";
}

static const struct { const char *name; const kk_palette_t *p; } PRESETS[] = {
    { "graphite", &P_GRAPHITE },
    { "abyss",    &P_ABYSS    },
    { "phosphor", &P_PHOSPHOR },
    { "macos",    &P_MACOS    },
};

void fnos_ui_theme_use(const char *name)
{
    if (!name || !*name) return;              /* 空 = 保持当前，不当作错误 */
    for (unsigned i = 0; i < sizeof PRESETS / sizeof PRESETS[0]; i++) {
        if (strcmp(name, PRESETS[i].name) == 0) {
            g_kk_pal = *PRESETS[i].p;
            s_name = PRESETS[i].name;
            return;
        }
    }
    /* 名字不认识：退回默认而不是崩 —— 配错名字的设备也要能开机。
       返回后调用方可用 fnos_ui_theme_name() 打日志核对。 */
    g_kk_pal = P_GRAPHITE;
    s_name = "graphite";
}

const char *fnos_ui_theme_name(void)
{
    return s_name;
}
