// uk_theme.c — 运行时配色与表面/前景色语义。
#include "uk_theme.h"

#include <string.h>
#include <math.h>

/* ── 预置 ─────────────────────────────────────────────────────────── */
/* Warm instrument palette: ivory and champagne identify data directions.
   Status colours remain independent of identity and selection. */
static const uk_pal_t P_CUKTECH = {
    .bg = 0x080807, .s0 = 0x0E0D0B, .s1 = 0x151411, .surf_t = 0x1B1A16,
    .s2 = 0x24221D, .s3 = 0x35322A, .off = 0xA59D90, .line = 0x353129,
    .t1 = 0xF1EDE4, .t2 = 0xD8D1C5, .t3 = 0xB9B0A2, .t4 = 0xABA294,
    .blue = 0xC7AD7D, .blue_txt = 0xC7AD7D,
    .ok = 0xA8BF8D, .warn = 0xE2B66E, .danger = 0xE88B78,
    .s_cpu = 0xE8E2D6, .s_mem = 0xBCA580, .s_temp = 0xD3A286,
    .s_down = 0xE8E2D6, .s_up = 0xC7AD7D, .s_zfs = 0xB9B0A2,
    .rail_n = 0x080807, .rail_day = 0x0E0D0B, .nav = 0x0E0D0B, .nav_sel = 0x29261F,
    .resource_primary = 0x191814, .resource_secondary = 0x151411,
    .summary = 0x1B1915, .on_summary = 0xF1EDE4, .summary_light = false,
    .on_primary = 0x151411, .on_danger = 0x151411,
    .usage_low = 0x75BFFF, .usage_warn = 0xF2D34F, .usage_high = 0xFF8780,
    .radius = 4, .radius_sm = 3, .flat_rows = false,
};

/* Legacy preset names remain configuration-compatible. Utilization has shared
   blue/yellow/red state colours; base surfaces remain neutral or warm neutral. */
/* UniFi Network dark-console inspired candidate. Neutralized surfaces and
   interaction white are project adaptations to the user's no-blue constraint. */
static const uk_pal_t P_UNIFI = {
    .bg = 0x101010, .s0 = 0x101010, .s1 = 0x181818, .surf_t = 0x202020,
    .s2 = 0x282828, .s3 = 0x383838, .off = 0xACACA6, .line = 0x303030,
    .t1 = 0xF7F7F5, .t2 = 0xDEDEDA, .t3 = 0xBEBEB8, .t4 = 0xACACA6,
    .blue = 0xE8E8E4, .blue_txt = 0xE8E8E4,
    .ok = 0x98C581, .warn = 0xE0B56C, .danger = 0xE58680,
    .s_cpu = 0xE8E8E4, .s_mem = 0xBDBDAB, .s_temp = 0xDDB775,
    .s_down = 0xA2C78B, .s_up = 0xDDB775, .s_zfs = 0xBDBDAB,
    .rail_n = 0x101010, .rail_day = 0x181818, .nav = 0x181818, .nav_sel = 0x303030,
    .resource_primary = 0x202020, .resource_secondary = 0x181818,
    .summary = 0x202020, .on_summary = 0xF7F7F5, .summary_light = false,
    .on_primary = 0x101010, .on_danger = 0x101010,
    .usage_low = 0x75BFFF, .usage_warn = 0xF2D34F, .usage_high = 0xFF8780,
    .radius = 8, .radius_sm = 4, .flat_rows = true,
};

/* User's ULTRA reference: black, white readings, small orange/cyan accents.
   The user's follow-up replaces yellow focus/warning accents with blue. */
static const uk_pal_t P_CUKTECH_CONSOLE = {
    .bg = 0x000000, .s0 = 0x080808, .s1 = 0x101010, .surf_t = 0x181818,
    .s2 = 0x181818, .s3 = 0x303030, .off = 0xB0B0B0, .line = 0x282828,
    .t1 = 0xF8F8F8, .t2 = 0xE0E0E0, .t3 = 0xB8B8B8, .t4 = 0xA8A8A8,
    .blue = 0x75BFFF, .blue_txt = 0x75BFFF,
    .ok = 0x8ED49C, .warn = 0x75BFFF, .danger = 0xFF5A45,
    .s_cpu = 0xFFA23A, .s_mem = 0x8ED7E5, .s_temp = 0xE0E0E0,
    .s_down = 0x8ED7E5, .s_up = 0xFFA23A, .s_zfs = 0x8ED49C,
    .rail_n = 0x000000, .rail_day = 0x101010, .nav = 0x080808, .nav_sel = 0x181818,
    .resource_primary = 0x101010, .resource_secondary = 0x101010,
    .summary = 0x101010, .on_summary = 0xF8F8F8, .summary_light = false,
    .on_primary = 0x101010, .on_danger = 0x101010,
    .usage_low = 0x8ED7E5, .usage_warn = 0x75BFFF, .usage_high = 0xFF5A45,
    .radius = 8, .radius_sm = 4, .flat_rows = true,
};

static const uk_pal_t P_DEVICE = {
    .bg = 0x14110E, .s0 = 0x17130F, .s1 = 0x201B15, .surf_t = 0x28211A,
    .s2 = 0x30281F, .s3 = 0x40372C, .off = 0xA59D90, .line = 0x40372C,
    .t1 = 0xF1EDE4, .t2 = 0xD8D1C5, .t3 = 0xB9B0A2, .t4 = 0xABA294,
    .blue = 0xD4AE8C, .blue_txt = 0xD4AE8C,
    .ok = 0xA8BF8D, .warn = 0xE2B66E, .danger = 0xE88B78,
    .s_cpu = 0xE8E2D6, .s_mem = 0xBCA580, .s_temp = 0xD3A286,
    .s_down = 0xE8E2D6, .s_up = 0xD4AE8C, .s_zfs = 0xB9B0A2,
    .rail_n = 0x080807, .rail_day = 0x0E0D0B, .nav = 0x17130F, .nav_sel = 0x352A20,
    .resource_primary = 0x191814, .resource_secondary = 0x151411, .summary = 0x1B1915, .on_summary = 0xF1EDE4,
    .summary_light = false,
    .on_primary = 0x151411, .on_danger = 0x151411,
    .usage_low = 0x75BFFF, .usage_warn = 0xF2D34F, .usage_high = 0xFF8780,
    .radius = 4, .radius_sm = 3, .flat_rows = false,
};

static const uk_pal_t P_GRAPHITE = {
    .bg = 0x10100F, .s0 = 0x141413, .s1 = 0x1B1B18, .surf_t = 0x22221E,
    .s2 = 0x292822, .s3 = 0x38362F, .off = 0xA59D90, .line = 0x353129,
    .t1 = 0xF1EDE4, .t2 = 0xD8D1C5, .t3 = 0xB9B0A2, .t4 = 0xABA294,
    .blue = 0xD4C7AF, .blue_txt = 0xD4C7AF,
    .ok = 0xA8BF8D, .warn = 0xE2B66E, .danger = 0xE88B78,
    .s_cpu = 0xE8E2D6, .s_mem = 0xBCA580, .s_temp = 0xD3A286,
    .s_down = 0xE8E2D6, .s_up = 0xC7AD7D, .s_zfs = 0xB9B0A2,
    .rail_n = 0x080807, .rail_day = 0x0E0D0B, .nav = 0x141413, .nav_sel = 0x302D27,
    .resource_primary = 0x191814, .resource_secondary = 0x151411, .summary = 0x1B1915, .on_summary = 0xF1EDE4,
    .summary_light = false,
    .on_primary = 0x151411, .on_danger = 0x151411,
    .usage_low = 0x75BFFF, .usage_warn = 0xF2D34F, .usage_high = 0xFF8780,
    .radius = 4, .radius_sm = 3, .flat_rows = false,
};

static const uk_pal_t P_ABYSS = {
    .bg = 0x050504, .s0 = 0x090908, .s1 = 0x11110F, .surf_t = 0x181713,
    .s2 = 0x211F1A, .s3 = 0x302D25, .off = 0xA59D90, .line = 0x353129,
    .t1 = 0xF1EDE4, .t2 = 0xD8D1C5, .t3 = 0xB9B0A2, .t4 = 0xABA294,
    .blue = 0xC9A875, .blue_txt = 0xC9A875,
    .ok = 0xA8BF8D, .warn = 0xE2B66E, .danger = 0xE88B78,
    .s_cpu = 0xE8E2D6, .s_mem = 0xBCA580, .s_temp = 0xD3A286,
    .s_down = 0xE8E2D6, .s_up = 0xC7AD7D, .s_zfs = 0xB9B0A2,
    .rail_n = 0x080807, .rail_day = 0x0E0D0B, .nav = 0x090908, .nav_sel = 0x282319,
    .resource_primary = 0x191814, .resource_secondary = 0x151411, .summary = 0x1B1915, .on_summary = 0xF1EDE4,
    .summary_light = false,
    .on_primary = 0x151411, .on_danger = 0x151411,
    .usage_low = 0x75BFFF, .usage_warn = 0xF2D34F, .usage_high = 0xFF8780,
    .radius = 4, .radius_sm = 3, .flat_rows = false,
};

/* Olive-tinted warm preset. The legacy blue field means interaction accent;
   status green remains independent of focus and metric identity. */
static const uk_pal_t P_PHOSPHOR = {
    .bg = 0x090A08, .s0 = 0x10110D, .s1 = 0x171912, .surf_t = 0x1D2017,
    .s2 = 0x26291E, .s3 = 0x35382B, .off = 0xA59D90, .line = 0x353129,
    .t1 = 0xF1EDE4, .t2 = 0xD8D1C5, .t3 = 0xB9B0A2, .t4 = 0xABA294,
    .blue = 0xC4B281, .blue_txt = 0xC4B281,
    .ok = 0xB0C394, .warn = 0xE2B66E, .danger = 0xE88B78,
    .s_cpu = 0xE8E2D6, .s_mem = 0xBCA580, .s_temp = 0xD3A286,
    .s_down = 0xE8E2D6, .s_up = 0xC7AD7D, .s_zfs = 0xB9B0A2,
    .rail_n = 0x080807, .rail_day = 0x0E0D0B, .nav = 0x10110D, .nav_sel = 0x292B20,
    .resource_primary = 0x191814, .resource_secondary = 0x151411, .summary = 0x1B1915, .on_summary = 0xF1EDE4,
    .summary_light = false,
    .on_primary = 0x151411, .on_danger = 0x151411,
    .usage_low = 0x75BFFF, .usage_warn = 0xF2D34F, .usage_high = 0xFF8780,
    .radius = 4, .radius_sm = 3, .flat_rows = false,
};

/* michka（michka.org 1.6.0）默认 slate sensor-panel 皮肤，逐值抄自上游
   src/Monitor/wwwroot/css/app.css 的 THEME 段与 .board .box 规则：
   黑底、中性灰气泡、白方块帽的居中大写卡头、粗胶囊条、平涂填充。
   rgba 边框/轨道按气泡色合成为实色（LVGL 只有不透明度，合成值更好对）。 */
static const uk_pal_t P_MICHKA = {
    .bg = 0x050506, .s0 = 0x0A0A0C, .s1 = 0x2C2C30, .surf_t = 0x252528,
    .s2 = 0x252528, .s3 = 0x434347, .off = 0x6E7079, .line = 0x434347,
    .t1 = 0xE9EBF2, .t2 = 0xD5D8E4, .t3 = 0xB4B6C0, .t4 = 0x9A9CA6,
    .blue = 0x6F8CFF, .blue_txt = 0x8FA6FF,
    .ok = 0x36E07A, .warn = 0xFF9F43, .danger = 0xFF6B6B,  /* 上游 #ff5470：灰气泡上 4.47:1 且触发本项目反紫审计，取暖红等价 */
    .s_cpu = 0x36E07A, .s_mem = 0x6F8CFF, .s_temp = 0x36C5FF,
    .s_down = 0x36C5FF, .s_up = 0xFF9F43, .s_zfs = 0xFFD24A,  /* 上游 ZFS 紫 #a06bff 触反紫审计，用同色板黄 */
    .rail_n = 0x050506, .rail_day = 0x0A0A0C, .nav = 0x0A0A0C, .nav_sel = 0x2C2C30,
    .resource_primary = 0x2C2C30, .resource_secondary = 0x252528,
    .summary = 0x2C2C30, .on_summary = 0xE9EBF2, .summary_light = false,
    .on_primary = 0x141416, .on_danger = 0x141416,
    .usage_low = 0x36E07A, .usage_warn = 0xFF9F43, .usage_high = 0xFF6B6B,
    .radius = 16, .radius_sm = 10, .flat_rows = true,
};

static const struct { const char *name; const uk_pal_t *p; } PRESETS[] = {
    { "michka", &P_MICHKA },
    { "cuktech", &P_CUKTECH },
    { "unifi", &P_UNIFI },
    { "cuktech-console", &P_CUKTECH_CONSOLE },
    { "device", &P_DEVICE },
    { "graphite", &P_GRAPHITE },
    { "abyss",    &P_ABYSS },
    { "phosphor", &P_PHOSPHOR },
};

uk_pal_t g_uk_pal = P_CUKTECH_CONSOLE;
static const char *s_pal_name = "cuktech-console";

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
    uk_theme_use("cuktech-console");
}

const char *fnos_ui_theme_name(void) { return s_pal_name; }

/* ── 阈值配色：条、点、数字共用同一处判断 ────────────────────────── */
uint32_t uk_pct_color(float pct)
{
    if (!isfinite(pct) || pct < 0) return UK_OFF;
    if (pct >= UK_PCT_FULL) return UK_USAGE_HIGH;
    if (pct >= UK_PCT_WARM) return UK_USAGE_WARN;
    return UK_USAGE_LOW;
}

uint32_t uk_temp_color(float c)
{
    if (c >= UK_TEMP_DANGER) return UK_DANGER;
    if (c >= UK_TEMP_WARM)   return UK_WARN;
    return UK_T2;
}
