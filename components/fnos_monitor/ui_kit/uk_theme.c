// uk_theme.c — 运行时配色与表面/前景色语义。
#include "uk_theme.h"

#include <string.h>

/* ── 预置 ─────────────────────────────────────────────────────────── */
/* Instrument palette: yellow is focus, cyan/blue identify data directions.
   Status colours remain independent of identity and selection. */
static const uk_pal_t P_CUKTECH = {
    .bg = 0x000000, .s0 = 0x04080C, .s1 = 0x080D12, .surf_t = 0x101B24,
    .s2 = 0x12202B, .s3 = 0x243441, .off = 0x4A5260, .line = 0x25313E,
    .t1 = 0xF5F7FA, .t2 = 0xD4DFE8, .t3 = 0xA5B4C2, .t4 = 0x94A3B1,
    .blue = 0xFFC800, .blue_txt = 0xFFC800,
    .ok = 0x36D27E, .warn = 0xFFAD54, .danger = 0xFF6878,
    .s_cpu = 0x3FA9F5, .s_mem = 0xC8E63C, .s_temp = 0xFFC800,
    .s_down = 0x37C4D6, .s_up = 0x3FA9F5, .s_zfs = 0xC8E63C,
    .rail_n = 0x000000, .rail_day = 0x000000, .nav = 0x000000, .nav_sel = 0x24200A,
    .resource_primary = 0x080D12, .resource_secondary = 0x080D12,
    .summary = 0x0A131B, .on_summary = 0xF5F7FA, .summary_light = false,
    .on_primary = 0x080D12, .on_danger = 0x080D12,
};

/* Network 10.6.106 dark observations, adapted for a NAS touch display.
   Surfaces/text/accent follow the recorded baseline; chart/status colours are
   project choices with separate semantics, not an official UniFi token set. */
static const uk_pal_t P_UNIFI = {
    .bg = 0x111316, .s0 = 0x0E1115, .s1 = 0x1A1E23, .surf_t = 0x20252B,
    .s2 = 0x252B32, .s3 = 0x343D47, .off = 0x42474D, .line = 0x323B45,
    .t1 = 0xF9FAFA, .t2 = 0xDEE0E3, .t3 = 0xB7BCC2, .t4 = 0xA0A7B0,
    .blue = 0x0068F5, .blue_txt = 0x4797FF,
    .ok = 0x62D28A, .warn = 0xF4BB60, .danger = 0xFF8C94,
    .s_cpu = 0x4797FF, .s_mem = 0x70C9E8, .s_temp = 0xD5BC72,
    .s_down = 0x70C9E8, .s_up = 0xB99AE8, .s_zfs = 0x62D28A,
    .rail_n = 0x111316, .rail_day = 0x111316, .nav = 0x111316, .nav_sel = 0x1D304A,
    .resource_primary = 0x1A1E23, .resource_secondary = 0x1A1E23,
    .summary = 0x202833, .on_summary = 0xDEE0E3, .summary_light = false,
    .on_primary = 0xFFFFFF, .on_danger = 0x19202A,
};

static const uk_pal_t P_DEVICE = {
    .bg = 0x15131F, .s0 = 0x11101B, .s1 = 0x211E2F, .surf_t = 0x252235,
    .s2 = 0x302B40, .s3 = 0x423A50, .off = 0x51495F, .line = 0x4A435C,
    .t1 = 0xBCC7F5, .t2 = 0xB9B8D6, .t3 = 0xAAA8C6, .t4 = 0x9C96B0,
    .blue = 0xD69C82, .blue_txt = 0xE9B49A,
    .ok = 0x8BD1AD, .warn = 0xE9B08C, .danger = 0xFF8E98,
    .s_cpu = 0xAFC2EE, .s_mem = 0xAFC2EE, .s_temp = 0xD69C82,
    .s_down = 0x8FC8BB, .s_up = 0xD7A68D, .s_zfs = 0x8BD1AD,
    .rail_n = 0x15131F, .rail_day = 0x15131F, .nav = 0x1C1929, .nav_sel = 0x302737,
    .resource_primary = 0x264439, .resource_secondary = 0x233D39, .summary = 0xCA957D, .on_summary = 0x242131,
    .summary_light = true,
    .on_primary = 0x242131, .on_danger = 0x242131,
};

static const uk_pal_t P_GRAPHITE = {
    .bg = 0x131416, .s0 = 0x0E0F11, .s1 = 0x1C1E21, .surf_t = 0x1F2226,
    .s2 = 0x282B2F, .s3 = 0x34383D, .off = 0x42474D, .line = 0x2E3238,
    .t1 = 0xF9FAFA, .t2 = 0xDEE0E3, .t3 = 0xB7BCC2, .t4 = 0x8F98A2,
    .blue = 0x0068F5, .blue_txt = 0x4797FF,
    .ok = 0x37BE5F, .warn = 0xE79913, .danger = 0xF5777C,
    .s_cpu = 0x4797FF, .s_mem = 0x63BDE3, .s_temp = 0xA0BD28,
    .s_down = 0x63BDE3, .s_up = 0xB47CDE, .s_zfs = 0xA0BD28,
    .rail_n = 0x141518, .rail_day = 0x1C1F22, .nav = 0x232629, .nav_sel = 0x272A2D,
    .resource_primary = 0x294D43, .resource_secondary = 0x203833, .summary = 0xBE937E, .on_summary = 0x19202A,
    .summary_light = true,
    .on_primary = 0xFFFFFF, .on_danger = 0x19202A,
};

static const uk_pal_t P_ABYSS = {
    .bg = 0x0A1018, .s0 = 0x060A10, .s1 = 0x121A24, .surf_t = 0x14202C,
    .s2 = 0x1B2632, .s3 = 0x27343F, .off = 0x3A4A57, .line = 0x2A3A48,
    .t1 = 0xF7FBFF, .t2 = 0xD6E2EC, .t3 = 0xAFC1CF, .t4 = 0x8097A8,
    .blue = 0x1E6BFF, .blue_txt = 0x55A0FF,
    .ok = 0x3FD07A, .warn = 0xF0A62B, .danger = 0xFF6B70,
    .s_cpu = 0x35C7F0, .s_mem = 0x7FA8FF, .s_temp = 0xB8D43C,
    .s_down = 0x35C7F0, .s_up = 0xC08BF0, .s_zfs = 0xB8D43C,
    .rail_n = 0x0B1219, .rail_day = 0x121A24, .nav = 0x1A2532, .nav_sel = 0x1F2C3B,
    .resource_primary = 0x244A48, .resource_secondary = 0x193335, .summary = 0xC99A7C, .on_summary = 0x12202C,
    .summary_light = true,
    .on_primary = 0xFFFFFF, .on_danger = 0x12202C,
};

/* 磷光：底最黑、字最亮；交互色（选中条/可点边）用琥珀，与"绿=健康"分色域。
   blue 仍是蓝：它兼着左上角 App 图标块底色，白字要 3:1 以上。 */
static const uk_pal_t P_PHOSPHOR = {
    .bg = 0x08090A, .s0 = 0x040506, .s1 = 0x111315, .surf_t = 0x141719,
    .s2 = 0x1B1F21, .s3 = 0x2A2F31, .off = 0x3C4245, .line = 0x272C2E,
    .t1 = 0xFFFFFF, .t2 = 0xDCE3E0, .t3 = 0xAEB8B4, .t4 = 0x848E89,
    .blue = 0x2B62D9, .blue_txt = 0xFFC44D,
    .ok = 0x3BE07A, .warn = 0xFFB000, .danger = 0xFF5F56,
    .s_cpu = 0x3BE07A, .s_mem = 0x39C6D6, .s_temp = 0xC8E04A,
    .s_down = 0x39C6D6, .s_up = 0xE07AC8, .s_zfs = 0xC8E04A,
    .rail_n = 0x0B0D0E, .rail_day = 0x111315, .nav = 0x191C1E, .nav_sel = 0x1E2224,
    .resource_primary = 0x254A36, .resource_secondary = 0x1B3227, .summary = 0xCB9E71, .on_summary = 0x15221A,
    .summary_light = true,
    .on_primary = 0xFFFFFF, .on_danger = 0x15221A,
};

static const struct { const char *name; const uk_pal_t *p; } PRESETS[] = {
    { "cuktech", &P_CUKTECH },
    { "unifi", &P_UNIFI },
    { "device", &P_DEVICE },
    { "graphite", &P_GRAPHITE },
    { "abyss",    &P_ABYSS },
    { "phosphor", &P_PHOSPHOR },
};

uk_pal_t g_uk_pal = P_CUKTECH;
static const char *s_pal_name = "cuktech";

void uk_theme_use(const char *name)
{
    if (!name || !name[0]) return;
    for (size_t i = 0; i < sizeof PRESETS / sizeof PRESETS[0]; i++) {
        if (strcmp(PRESETS[i].name, name) == 0) {
            g_uk_pal = *PRESETS[i].p;
            s_pal_name = PRESETS[i].name;
            return;
        }
    }
    /* 名字不认识就保持现状，不报错（调用方可能是环境变量/命令行） */
}

const char *uk_theme_name(void) { return s_pal_name; }

/* main.cpp（C++）按名字切配色；名字沿用 kk_ui 时代那几个。不认识的退回默认而不是崩：
   配错名字的设备也要能开机（kk_ui 删掉后，调色板只剩 ui_kit 这一份）。 */
void fnos_ui_theme_use(const char *name)
{
    if (!name || !name[0]) return;
    for (size_t i = 0; i < sizeof PRESETS / sizeof PRESETS[0]; i++) {
        if (strcmp(PRESETS[i].name, name) == 0) { uk_theme_use(name); return; }
    }
    uk_theme_use("cuktech");
}

const char *fnos_ui_theme_name(void) { return s_pal_name; }

/* ── 阈值配色：条、点、数字共用同一处判断 ────────────────────────── */
uint32_t uk_pct_color(int32_t pct)
{
    if (pct < 0) return UK_OFF;
    if ((float)pct >= UK_PCT_FULL) return UK_DANGER;
    if ((float)pct >= UK_PCT_WARM) return UK_WARN;
    return UK_OK;
}

uint32_t uk_temp_color(float c)
{
    if (c >= UK_TEMP_DANGER) return UK_DANGER;
    if (c >= UK_TEMP_WARM)   return UK_WARN;
    return UK_T2;
}
