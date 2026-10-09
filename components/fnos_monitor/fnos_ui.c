// fnos_ui.c - 监控界面：全部由 ui_kit（LVGL 原生 flex）搭建。
//
// 手写 C + ui_kit 构件。本文件既建树也格式化文案，只有 LVGL 任务写入界面。
// 数据只从 fnos_data 取快照、网络状态只从 fnos_net 读缓存（不在这里发起任何请求）。
//
// 版面：顶栏 + 自适应内容视口 + 底部导航；五页 = 总览 / 存储 / 网络 / 系统 / 温度。
#include "fnos_ui.h"
#include <stdlib.h>   /* getenv：仅用于主机侧自描述（KK_UI_CARDMAP） */

#include <stdarg.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

// 本地真实配置（Wi-Fi 口令、NAS 地址）不入库，也没有时用模板兜底：
// 这里只需要 FNOS_HOST/FNOS_PORT 两个宏（诊断页显示端点用）。
// 注意要用 __has_include 守卫——直接 #include 会让"不含个人配置的发布构建"编不出来。
#if __has_include("fnos_config.h")
#include "fnos_config.h"
#else
#include "fnos_config.example.h"
#endif
#include "fnos_data.h"
#include "fnos_fonts.h"
#include "fnos_net.h"
#include "fnos_pair.h"
#include "fnos_wifi_store.h"   /* 开机"有没有凭据"要问存储本身，别问网络层的标志 */
#include "ui_kit/uk.h"   /* 新自适应层：LVGL 原生 flex，几何不写死 */

static const char *TAG = "fnos_ui";

/* ── 版面常量 ─────────────────────────────────────────────────────── */
#define UI_HIST        CONFIG_FNOS_CHART_WINDOW
/* 一律引用 fnos_data.h 的常量，别手写数字：手写的那个不会跟着改，
   而"界面少一行"和"数据被丢了"看起来一模一样。 */
#define UI_AGENT_ROWS   10
#define UI_CARDS       32
/* 设备和通道数量沿用协议上限；详情按需分配，不按某台 NAS 的数量限流。 */

/* Overview objects keep the same place and identity across samples and states. */
typedef struct {
    lv_obj_t *card, *value, *unit, *meta, *gauge, *chart, *context;
} overview_resource_t;
typedef struct {
    lv_obj_t *box, *name, *value, *bar;
} overview_volume_t;

/* 一台设备（= 一个 dev）在本帧里的分组结果 */
typedef struct {
    char       *name;        /* 显示名：dn；同名设备多于一台时 "dn · dev" */
    const char *dev;             /* 稳定 id（分组键，指向 s_st.temps，只在本帧有效） */
    int         first, count;    /* 在 st->temps[] 里的区间（已按 dev,ch 排过序） */
    int         hot;             /* 组内最热那一路的下标 */
    int         inline_ch;       /* 单通道设备：通道名并进块头，不再单起一行 */
    int         dup;             /* dn 同名的设备多于一台 ⇒ 要靠 dev 消歧 */
    float       max_c, min_c;
    bool        collapsed;
} temp_grp_t;

typedef struct {
    lv_obj_t *line, *name, *val;
} temp_ch_t;

/* 一个设备一个块；用户展开状态与稳定设备 ID 绑定，采样不改变布局。 */
typedef struct {
    lv_obj_t *box, *grid, *led, *name, *vmax, *more;
    temp_ch_t *channels;
    char *dev;
    int capacity, made, shown_lines, count;
    bool collapsed;
} temp_blk_t;

/* ── 配对 ─────────────────────────────────────────────────────────
   指纹一行 8 字节（"AA:BB:CC:DD:EE:FF:GG:HH" = 23 字符），SHA-256 正好 4 行。
   分组固定是刻意的：本工程没有等宽字体，靠"同样的分行 + 同样的分组"让人逐段比对，
   而不是靠字符位置对齐。 */
#define PAIR_FP_LINES   4
#define PAIR_FP_PER     8
#define PAIR_PAD_KEYS  12
#define PAIR_PAD_COLS  4     /* 键盘列数：4 列 × 3 行（3 列 × 4 行会把整卡顶出视口） */
/* 告警与事件卡的最高高度（见 build_p3）：再多就交给卡内滚动，别把上面两张卡挤没。 */
#define UI_ALERT_PANEL_MAX_H 150
/* 系统页那张告警卡只当"摘要"：最多列三条，其余去告警页看。 */
#define UI_ALERT_PANEL_ROWS  3
#define PAIR_CODE_LEN   6
#define PAIR_STEPS      3

/* ── 板上配网卡的几何与状态（定义在 s_ui 之前：结构体里要按这些尺寸开数组） ── */
#define WIFI_AP_ROWS  7          /* 扫描列表一屏显示几条（按信号取前 7） */
#define WIFI_KB_KEYS  41         /* 10 + 10 + 9 + 9 + 3 */
#define WIFI_PW_MAX   63         /* WPA2 口令最长 63 个 ASCII 字符 */
#define WIFI_SSID_MAX 32
enum { WIFI_ST_SCAN = 0, WIFI_ST_PASS, WIFI_ST_LINK };

static struct {
    lv_obj_t *screen, *viewport;
    lv_obj_t *overview_clock, *overview_clock_note, *overview_services, *overview_service_note;
    lv_obj_t *cards[UI_CARDS];
    int       ncards;
    lv_color_t card_colors[UI_CARDS];
    bool card_colors_saved;
    lv_obj_t *page[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav_lbl[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav_icon[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav_mark[FNOS_UI_PAGE_COUNT];
    /* 顶栏 */
    lv_obj_t *h_host, *h_ep, *h_chip, *h_bars[4];
    lv_obj_t *health, *overview_note;
    lv_obj_t *capacity, *capacity_detail, *storage_note;
    lv_obj_t *capacity_bar;             /* hero 条（8 高） */
    lv_obj_t *net_axis, *net_note, *system_state, *system_note, *system_note2;
    lv_obj_t *p3_note, *p3_cols;    /* 系统页正文：三张覆盖层（诊断/配对/配网）显示时要收起来 */
    lv_obj_t *vol_empty, *raid_empty, *disk_empty, *dock_empty, *temp_empty;
    lv_obj_t *dock_empty_label;
    lv_obj_t *diagnostics, *diagnostics_button, *diagnostics_label;
    /* P0: two resource gauges/trends, network flow, volume capacity comparison. */
    overview_resource_t overview_resource[2];
    lv_obj_t *overview_net, *overview_net_context;
    lv_obj_t *overview_rate[2], *overview_unit[2]; /* DOWN, UP */
    lv_obj_t *overview_volumes;
    overview_volume_t *overview_volume;
    int overview_volume_capacity;
    int overview_volume_made;
    /* P1 存储：卷 / 阵列 / 磁盘各一个自适应池（ui_kit），行按需建、按需显示 */
    lv_obj_t *storage_cards;
    lv_obj_t *vol_card,  *vol_pool;
    lv_obj_t *raid_card, *raid_pool;
    lv_obj_t *disk_card, *disk_pool;
    uk_row_t **vol_row;
    uk_row_t **raid_row;
    uk_row_t **disk_row;
    int       vol_made, raid_made, disk_made;
    lv_obj_t *storage_value[3];
    /* P2 网络：4 张 uk_kpi + 一条双序列趋势（上行/下行共用一张图，量程统一） */
    uk_kpi_t *kpi2[4];
    lv_obj_t *net_card, *net_pool;
    uk_row_t **net_rows;
    int net_made;
    lv_obj_t *tr_net;
    lv_obj_t *tr_rx_lbl, *tr_tx_lbl;
    lv_obj_t *net_title;
    /* P3: adaptive container and temperature lists, plus a scrolling event column. */
    lv_obj_t *dock_card, *dock_pool;
    uk_row_t **dock_row;
    int       dock_made;
    lv_obj_t *alert_card;
    lv_obj_t *p3_temp_card, *p3_temp_pool;
    uk_row_t **p3_temp_row;
    int       p3_temp_made;
    /* P4: one summary block per device, with manually opened channel details. */
    lv_obj_t *temp_pool, *temp_card;
    temp_blk_t *temp_blk;
    int temp_blk_capacity;
    int        temp_blk_made;
    lv_obj_t *temp_hero, *temp_hero_unit, *temp_hero_dot;   /* 摘要卡：最热温度大字 */
    lv_obj_t *temp_note, *temp_note2, *temp_empty_all;
    lv_obj_t *agent_detail[UI_AGENT_ROWS];
    lv_obj_t *alert_col, **alert_lbl, *alert_none, *alert_none_box;
    int alert_made;

    /* 首页（P0）：顶部"结论带" + 最热温度磁贴。带子把"健康/告警"从两行小字
       升级成整页宽的一行结论，点它进告警页。 */
    lv_obj_t *home_band, *home_band_dot, *home_band_more, *home_band_msg;
    lv_obj_t *home_temp_card, *home_temp_value, *home_temp_unit, *home_temp_name, *home_temp_dot;
    /* P1 存储页的容量条（首页那条属于首页的存储磁贴，两条各自绑定同一次计算） */
    lv_obj_t *storage_bar;
    /* P5 告警页：摘要 + 三组（严重/警告/提示）分行 */
    lv_obj_t *alert_page_sum, *alert_page_note, *alert_page_empty;
    lv_obj_t *alert_page_head[3], *alert_page_cap[3], *alert_page_col[3];
    int alert_page_made[3];
    /* P5 健康态体检块：告警为空时这里回答"看住了什么、现在什么状态" */
    lv_obj_t *alert_health;
    lv_obj_t *alert_health_dot[4], *alert_health_val[4];

    /* 左导航栏（rail）自身与它的徽标：三档表面由 theme_apply() 统一落地 */
    lv_obj_t *rail, *logo_lbl;
    lv_obj_t *foot_poll, *foot_dot, *foot_txt;   /* 底栏：轮询统计 + 告警带 */

    /* 配对（覆盖在系统页上的一张整页卡，与"采集诊断"同槽位、互斥显示）*/
    lv_obj_t *pair_btn, *pair_btn_lbl, *pair_dot;
    lv_obj_t *pair_card, *pair_title, *pair_hint, *pair_grid, *pair_foot;
    lv_obj_t *pair_fp[PAIR_FP_LINES], *pair_meta, *pair_msg;
    lv_obj_t *pair_steps, *pair_steps_lbl, *pair_step_no[PAIR_STEPS], *pair_step_txt[PAIR_STEPS];
    lv_obj_t *pair_code_panel, *pair_info_panel, *pair_codecol, *pair_msg_panel, *pair_info_lbl;
    lv_obj_t *pair_code_lbl, *pair_code_sub, *pair_slot[PAIR_CODE_LEN];
    lv_obj_t *pair_pad, *pair_pad_lbl, *pair_key[PAIR_PAD_KEYS];
    lv_obj_t *pair_ok, *pair_cancel, *pair_forget;

    /* 配网卡（与配对卡同槽位：盖在系统页上的整页卡，互斥显示） */
    lv_obj_t *wifi_btn, *wifi_btn_lbl;       /* 顶栏那颗常驻按钮 */
    lv_obj_t *wifi_card, *wifi_title, *wifi_hint, *wifi_list_hint;
    lv_obj_t *wifi_ap_pool;                  /* 扫描结果：一列行的自适应池 */
    uk_row_t *wifi_ap_row[WIFI_AP_ROWS];
    int       wifi_ap_made;
    lv_obj_t *wifi_foot;                     /* 底部一行：各阶段的键都建在这里，按阶段显隐 */
    lv_obj_t *wifi_input_panel, *wifi_ssid_box, *wifi_ssid_lbl;
    lv_obj_t *wifi_pw_box, *wifi_pw_lbl, *wifi_eye, *wifi_eye_lbl;
    lv_obj_t *wifi_kb, *wifi_key[WIFI_KB_KEYS], *wifi_shift_btn, *wifi_shift_lbl;
    lv_obj_t *wifi_rescan, *wifi_manual, *wifi_close, *wifi_back, *wifi_connect;
    lv_obj_t *wifi_link_panel, *wifi_link_big, *wifi_link_sub, *wifi_done, *wifi_again;
} s_ui;

static const char *const NAV_TXT[FNOS_UI_PAGE_COUNT] = { "总览", "存储", "网络", "系统", "温度", "告警" };
/* NAV_TXT 的条数必须与页数一致：曾经把 FNOS_UI_PAGE_COUNT 改成 5 而文案只有 4 条，
   多出来的那个导航项读到 NULL 文案、还把页面建树多跑了一轮（build_page 的 default
   把它当第 4 页又建了一遍），表现为"侧栏多出一个空按钮压住配对入口"。
   这类错误必须在编译期就断掉。 */
_Static_assert(sizeof(NAV_TXT) / sizeof(NAV_TXT[0]) == FNOS_UI_PAGE_COUNT,
               "NAV_TXT 条数必须等于 FNOS_UI_PAGE_COUNT");
static const char *const AGENT_KEY[UI_AGENT_ROWS] = {
    "主机", "端点", "HTTP", "轮询", "最近错误", "数据年龄", "固件内存", "ZFS 缓存",
    "协议", "采集段"
};
/* 诊断页的行按"排查顺序"分三组：连接（通不通）→ 采集（数据新不新）→ 运行（板子自己）。
   十行等权平铺时，最该看的"轮询失败数 / 最近错误"埋在中段，分组后才读得出来。 */
#define UI_AGENT_GROUPS 3
static const struct { const char *title; int first, last; } AGENT_GROUP[UI_AGENT_GROUPS] = {
    { "连接", 0, 2 }, { "采集", 3, 5 }, { "运行", 6, 9 },
};

/* 采集段状态词 -> 中文。认识的翻，不认识的**原样返回**：应用升级后加了新状态，
   诊断页要能显示出来（哪怕是个英文词），而不是显示成空白让人以为板子坏了。 */
static const char *mod_status_cn(const char *s)
{
    if (!strcmp(s, "ok"))       return "正常";
    if (!strcmp(s, "stale"))    return "旧值";
    if (!strcmp(s, "missing"))  return "读不到";
    if (!strcmp(s, "denied"))   return "没权限";
    if (!strcmp(s, "error"))    return "出错";
    if (!strcmp(s, "disabled")) return "已关闭";
    if (!strcmp(s, "partial"))  return "部分可读";
    return s;
}

/* ── 运行时状态 ───────────────────────────────────────────────────── */
static bool          s_created;
static int           s_page;
static bool          s_night, s_night_req;
static volatile int  s_page_req = -1;   /* 串口 'page n' 只置标志，落地在 ui_tick */
static volatile int  s_temp_expand_req = -1;  /* 串口 'temp n'：切温度页并展开第 n 台设备 */
static bool          s_diagnostics;
static fnos_status_t s_st = { .host="", .net={ .ifname="", .state="" } };
static int64_t       s_seq;
static bool          s_overview_gap[3]; /* CPU, memory, network; one gap on recovery. */

/* 各页自适应池的"上次解"：条数与池尺寸都没变就不重排（照 p4 的 s_p4_* 写法）。
   放在文件作用域而不是 refresh() 里的 static：同一个页面有多张池卡，
   函数内 static 会互相看不见谁是谁。 */
static int s_p1_vol_n  = -1, s_p1_vol_w,  s_p1_vol_h;
static int s_p1_raid_n = -1, s_p1_raid_w, s_p1_raid_h;
static int s_p1_disk_n = -1, s_p1_disk_w, s_p1_disk_h;
static int s_p3_dock_n = -1, s_p3_dock_w, s_p3_dock_h;
static int s_p3_temp_n = -1, s_p3_temp_w, s_p3_temp_h;
static int s_p4_n      = -1, s_p4_w,      s_p4_h;
static int s_wifi_ap_n = -1, s_wifi_ap_w, s_wifi_ap_h;

/* 配对界面：s_pair_open 只表示"这张卡现在盖在系统页上"，真正的阶段由 fnos_pair 的
   状态机决定（见 pair_stage()）——不要在本地再实现一套状态机，否则两边会漂。 */
static bool          s_pair_open;
static bool          s_wifi_open;   /* 配网卡是否盖在系统页上（定义在配网那一段之前，p3_overlay_sync 要用） */
static bool          s_pair_forget_armed;   /* 解除配对要按两次 */
static char          s_pair_code[PAIR_CODE_LEN + 1];
static char          s_pair_last[PAIR_CODE_LEN + 1];  /* 已提交的码，核对指纹时还看得见 */
static int           s_pair_len;
/* 本地提示（校验错误、二次确认）压过 fnos_pair 的 msg，但只在"说它的那个阶段"有效：
   否则"正在从 NAS 取证书…"会一路挂到核对指纹那一屏，把真正的指示盖掉。 */
static char          s_pair_hint[96];
static int           s_pair_hint_stage;      /* PAIR_HINT_ANY = 跟阶段无关 */
#define PAIR_HINT_ANY  (-1)
/* 配对入口的状态色（未配对=告警橙 / 进行中=交互蓝 / 已配对=主文本）。
   由 pair_refresh() 从 fnos_pair 快照里算出来，theme_apply() 负责画到状态点上。 */
static uint32_t      s_pair_alert = 0xE79913;

/* 配对面板的定义在文件后半（挨着 digit 键盘那一片），建树在 build_rail/build_p3 里，
   所以先声明。 */
static void motion_init(void);
static bool motion_busy(void);
static void pages_warm(void);
static bool s_refresh_pending;

static void pair_build(lv_obj_t *page);
static void pair_show(bool on);
static void wifi_show(bool on);
static void pair_refresh(void);
static void pair_btn_cb(lv_event_t *e);
static void wifi_build(lv_obj_t *page);
static void wifi_btn_cb(lv_event_t *e);
static void show(lv_obj_t *o, bool on);
/* 按钮五档皮肤（btn_skin/flex_btn 用；定义在文件后半，声明提到这里） */
enum { BTN_PRIMARY = 0, BTN_SECONDARY, BTN_NEUTRAL, BTN_GHOST, BTN_DANGER };
static void not_clickable(lv_obj_t *o);
static lv_obj_t *flex_btn(lv_obj_t *parent, const char *txt, int kind,
                         const lv_font_t *font, int32_t w);
/* 左栏的表面色由 theme_apply() 统一落地（建树、换页、夜间、配对状态都走它一处），
   而它的定义在 build_rail 之前，这里不需要额外的前向声明。 */

/* ── 小工具 ───────────────────────────────────────────────────────── */
/* 160 字节对中文来说太小了：一个汉字 3 字节，稍长的一句话就会被**截断在多字节
   序列中间**，画出来是乱码或豆腐块，而且不报错。配对卡片那段"接受之后…"的说明
   有 200 多字节，就是这么被切断的（预览的字形审计报 U+0000 才暴露出来）。
   384 字节够放 120 多个汉字；再长就该直接用 lv_label_set_text 而不是走格式化。 */
static void set_text_v(lv_obj_t *label, bool animate, const char *fmt, va_list ap)
{
    if (!label) return;
    va_list measure;
    va_copy(measure,ap);
    int n=vsnprintf(NULL,0,fmt,measure);
    va_end(measure);
    char *text=n>=0 ? uk_alloc((size_t)n+1) : NULL;
    if (text) {
        vsnprintf(text,(size_t)n+1,fmt,ap);
        uk_number_set_text(label,text,animate);
        lv_free(text);
    }
}

static void set_txt(lv_obj_t *label, const char *fmt, ...)
{
    va_list ap; va_start(ap,fmt); set_text_v(label,false,fmt,ap); va_end(ap);
}
static void set_num(lv_obj_t *label, const char *fmt, ...)
{
    va_list ap; va_start(ap,fmt); set_text_v(label,true,fmt,ap); va_end(ap);
}



/* 顶栏状态点的颜色 + 同色光晕：光晕让"在线/离线"在余光里也能被看见，
   但文字仍是主编码（发光只是冗余，不承担唯一含义）。chip 的第 0 个子对象就是状态点。 */
static void chip_set_glow(lv_obj_t *chip, const char *txt, uint32_t rgb)
{
    if (!chip) return;
    lv_obj_t *dot = lv_obj_get_child(chip, 0);   /* 第 0 个是状态点 */
    lv_obj_t *lbl = lv_obj_get_child(chip, 1);   /* 第 1 个是文字 */
    if (lbl) {
        set_txt(lbl, "%s", txt);
        lv_obj_set_style_text_color(lbl, uk_c(rgb), 0);
    }
    if (dot) {
        lv_obj_set_style_bg_color(dot, uk_c(rgb), 0);
        lv_obj_set_style_shadow_color(dot, uk_c(rgb), 0);
        lv_obj_set_style_shadow_width(dot, UK_GLOW_W, 0);
        lv_obj_set_style_shadow_opa(dot, 0, 0);
    }
}




static lv_obj_t *mk_page(lv_obj_t *content)
{
    /* 一页就是内容区的全部：宽高都跟父容器走（外壳底栏出现后，内容区高度变小，
       页面必须跟着缩，不能再钉死成设计尺寸）。 */
    lv_obj_t *p = lv_obj_create(content);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(p, uk_c(UK_BG), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_USER_1); /* page boundary for preview audits */
    return p;
}






/* 容量 / 速率 / 时长 / 数据年龄 */
static const char *fmt_cap(char *b, size_t n, float gb)
{
    if (gb >= 1024.0f)     snprintf(b, n, "%.1f TB", gb / 1024.0f);
    else if (gb >= 100.0f) snprintf(b, n, "%.0f GB", gb);
    else                   snprintf(b, n, "%.1f GB", gb);
    return b;
}

static const char *fmt_rate(char *b, size_t n, float kbs)
{
    if (kbs >= 1048576.0f) snprintf(b, n, "%.1f GB/s", kbs / 1048576.0f);
    else if (kbs >= 1024.0f) snprintf(b, n, "%.1f MB/s", kbs / 1024.0f);
    else if (kbs >= 10.0f) snprintf(b, n, "%.0f KB/s", kbs);
    else                   snprintf(b, n, "%.1f KB/s", kbs);
    return b;
}

static const char *fmt_uptime(char *b, size_t n, uint32_t s)
{
    uint32_t d = s / 86400u, h = (s % 86400u) / 3600u, m = (s % 3600u) / 60u;
    if (d)      snprintf(b, n, "%ud %02uh", (unsigned)d, (unsigned)h);
    else if (h) snprintf(b, n, "%uh %02um", (unsigned)h, (unsigned)m);
    else        snprintf(b, n, "%um", (unsigned)m);
    return b;
}

static const char *fmt_age(char *b, size_t n, int64_t ms)
{
    if (ms < 1500)       snprintf(b, n, "刚刚");
    else if (ms < 60000) snprintf(b, n, "%d 秒前", (int)(ms / 1000));
    else                 snprintf(b, n, "%d 分前", (int)(ms / 60000));
    return b;
}

/* ── ui_kit 时代的通用小工具 ─────────────────────────────────────────
   几何一律交给 flex：这里只摆"结构"（页根、栏、卡的伸缩、池的重排守门），
   不再出现任何 mk_xx(x, y, w, h)。 */

/* 页根：flex column + 统一页边距。所有页的 build_pX 都从这里开始。 */
static void ui_page(lv_obj_t *page)
{
    /* 页面自己不再加内边距：外壳的 content 已经给了 UK_S3，再加一层就是 24px 双份
       —— 横向会把 924 的版面压成 900（和 mockup 对不上），纵向会白吃掉 24px，
       P1 的卷池/盘池正好差这 20 多像素而被迫滚动。 */
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, UK_CARD_GAP, 0);
    /* Intrinsic card content can exceed a short viewport after text wraps. */
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    /* 但页面从不画滚动条：内容偶尔超出十几像素时，右缘那根近乎满高的竖条会被读成
       "右边还有一张卡被切掉了"（卡片其实都收在页内，数据也没丢）。可滚能力留给
       "换行把卡撑高"的极端情况。 */
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
}

/* 卡内的一"栏"（标签 + 数值）：flex column；grow>0 时等分横排宽度。
   uk.h 里没有"列"这一层，但在 flex 里它就是 lv_obj + FLOW_COLUMN + flex_grow。 */
/* 卡片统一从这里建：uk_card 是 ui_kit 的，不认识本文件的"卡片表"，
   而夜间模式要把所有卡面压暗（旧实现靠 mk_card 登记，迁移后得补上这一步）。 */
static lv_obj_t *ui_card(lv_obj_t *parent, const char *title, const char *note)
{
    lv_obj_t *c = uk_card(parent, title, note);
    if (s_ui.ncards < UI_CARDS) s_ui.cards[s_ui.ncards++] = c;
    return c;
}

static void panel_text_tone(lv_obj_t *obj, uint32_t color)
{
    if (lv_obj_has_class(obj, &lv_label_class)) lv_obj_set_style_text_color(obj, uk_c(color), 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
        panel_text_tone(lv_obj_get_child(obj, i), color);
}

static void panel_peach(lv_obj_t *panel)
{
    lv_obj_set_style_bg_color(panel, uk_c(UK_PEACH), 0);
    panel_text_tone(panel, UK_INK);
}

static lv_obj_t *uk_col(lv_obj_t *parent, int32_t grow)
{
    lv_obj_t *c = lv_obj_create(parent);
    /* 宽默认内容宽：lv_obj_create() 的默认宽高是 LV_DPI_DEF(130)，
       不写死就会给每个"栏"凭空 130px（grow>0 时由 flex 接管，写内容宽也无害）。 */
    lv_obj_set_width(c, LV_SIZE_CONTENT);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_pad_row(c, UK_S1, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_min_width(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    if (grow > 0) lv_obj_set_flex_grow(c, (uint8_t)grow);
    return c;
}

/* 空态：整张卡正中一句话。只在左上角留一行"未采集到…"、下面几百像素全空着，
   会被当成没写完的界面 —— 而离线态是长期状态，不是一闪而过的瞬态。
   返回的是**盒子**（要连它一起隐藏，不然它占着一半卡高）；*out_label 给出里面那句话。 */
static lv_obj_t *empty_box(lv_obj_t *body, const char *txt, lv_obj_t **out_label)
{
    lv_obj_t *box = uk_col(body, 1);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = uk_label(box, UK_FONT_CJK_16, UK_T3, txt);
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    if (out_label) *out_label = l;
    return box;
}

/* 可以自由排版的滚动列（告警那种长短不一、还要换行的内容用不了自适应池：
   池要求条目等高等宽，还会给每个条目 flex_grow）。滚动条画在自己右边缘。 */
static lv_obj_t *scroll_col(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_pad_row(c, UK_S3, 0);
    lv_obj_set_style_pad_right(c, UK_SCROLL_INSET, 0);
    lv_obj_set_style_min_height(c, 0, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_width(c, UK_SCROLL_W, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(c, uk_c(UK_SURF_S3), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(c, UK_SCROLL_W / 2, LV_PART_SCROLLBAR);
    return c;
}

/* 池重排守门：只有"条目数或池尺寸变了"才重排（照 p4 的 s_p4_* 写法，四页共用）。
   读尺寸前必须 update_layout —— lv_obj_get_width() 读的是布局结果，不是实时计算；
   页面还藏着（HIDDEN）时子树不参与 flex 布局，尺寸是 0，这时不能重排，
   留成 cols<1 的脏态，等这一页真的显示出来后的第一次 refresh 再排。 */
static void pool_layout(int *n0, int *w0, int *h0, int n, lv_obj_t *pool,
                        int32_t min_col_w, bool data_fit, lv_obj_t *note)
{
    if (!pool) return;
    lv_obj_update_layout(pool);
    int w = lv_obj_get_width(pool), h = lv_obj_get_height(pool);
    if (n == *n0 && w == *w0 && h == *h0 && uk_pool_cols(pool) > 0) return;
    *n0 = n; *w0 = w; *h0 = h;
    if (w < 32 || h < 32) return;          /* 还没布局出来，下次刷新再试 */
    uk_pool_relayout(pool, min_col_w, data_fit, note);
    if (getenv("UK_POOL_TRACE"))
        fprintf(stderr, "[pool] n=%d W=%d H=%d cols=%d shown=%d\n",
                n, w, h, (int)uk_pool_cols(pool), (int)uk_pool_shown(pool));
}

/* 一张卡里"按需建行"的共用写法：条目数变少时**销毁**多余的行走（不是 HIDDEN）。
   两个理由：① uk_pool_relayout() 会把池里所有非列子对象都当成条目，而且
   pool_reset() 会清掉它们的 HIDDEN —— 留着"隐藏的行"会被它重新显示出来，
   表现为"已经没数据了，屏幕上还是上一帧的旧行"；② 用不到的行如果只是藏起来，
   会永久留在预览的"从未露面"统计里（基线只有 175，是紧的）。 */
/* The registry size follows the visible inventory. No hardware-count ceiling. */
static void row_trim(uk_row_t **rows, int *made, int cap, int count)
{
    (void)cap;
    for (int i=count; i<*made; i++) { lv_obj_delete(rows[i]->row); lv_free(rows[i]); rows[i]=NULL; }
    if (*made>count) *made=count;
}

static int rows_sync(uk_row_t ***registry, int *made, int count,
                     lv_obj_t *pool, bool led, bool bar)
{
    uk_row_t **rows = *registry;
    if (count > *made) {
        uk_row_t **grown = uk_realloc(rows, (size_t)count * sizeof *rows);
        if (!grown) return *made;
        rows = grown;
        *registry = rows;
        while (*made < count) {
            uk_row_t *row = uk_row_create(pool, led, bar);
            if (!row) return *made;
            rows[(*made)++] = row;
        }
    }
    for (int i = count; i < *made; i++) {
        lv_obj_delete(rows[i]->row);
        lv_free(rows[i]);
    }
    *registry = rows;
    *made = count;
    if (!count) { lv_free(rows); *registry = NULL; }
    return count;
}


/* ── 建树 ─────────────────────────────────────────────────────────── */
static void nav_cb(lv_event_t *e);
static void refresh(void);
static void header_refresh(void);

static void foot_band_cb(lv_event_t *e);
static void page_jump_cb(lv_event_t *e);

/* 跳页回调：任何"点一下去某页"的构件都用它，省掉一堆只差页号的回调。
   页号通过 user_data 传（与 nav_cb 同一套做法）。 */
static void page_jump_cb(lv_event_t *e)
{
    fnos_ui_set_page((int)(intptr_t)lv_event_get_user_data(e));
}

/* 底栏告警带 / 首页结论带：点进告警页（第 5 页）。 */
static void foot_band_cb(lv_event_t *e) { (void)e; fnos_ui_set_page(5); }



/* 系统页正文 ↔ 覆盖层（诊断 / 配对 / 配网）互斥。
   旧实现里这三张是"同槽位、同尺寸的整页卡"，靠后建的盖住先建的；flex 里没有
   绝对定位的浮层 —— 覆盖层要独占内容区，就只能把正文收起来（三者都隐藏时再放回）。 */
static void p3_overlay_sync(void)
{
    if (!s_ui.p3_note) return;
    bool any = s_diagnostics || s_pair_open || s_wifi_open;
    /* 告警卡挂在页上（整页宽，不跟另外两张挤一排），所以它不在 p3_cols 里 ——
       收正文时得单独跟着收，否则覆盖层底下会露出系统页的一张卡。 */
    lv_obj_t *body[] = { s_ui.p3_note, s_ui.p3_cols, s_ui.alert_card };
    for (unsigned i = 0; i < sizeof body / sizeof body[0]; i++) {
        if (!body[i]) continue;
        if (any) lv_obj_add_flag(body[i], LV_OBJ_FLAG_HIDDEN);
        else     lv_obj_remove_flag(body[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void diagnostics_cb(lv_event_t *e)
{
    fnos_ui_motion_settle();
    (void)e;
    pair_show(false);
    if (s_wifi_open) wifi_show(false);
    s_diagnostics = !s_diagnostics;
    if (s_diagnostics) lv_obj_remove_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);
    set_txt(s_ui.diagnostics_label, "%s", s_diagnostics ? "返回" : "设置");
    p3_overlay_sync();
}

/* ── 外壳：rail / head / foot ────────────────────────────────────────
   uk_shell() 切出三块容器（rail 固定宽、head 固定高、foot 按内容），这里只往里
   放东西 —— 位置全部由 flex 决定，一个绝对坐标都不写。
   ui_kit 没有图标字体：五枚导航图标用 LVGL 原语摆（嵌套的行/列 + 小实心盒），
   每枚都画在 24×24 的画布上，写死的只有"笔画尺寸"。 */
static void icon_set_color(lv_obj_t *o, uint32_t hex)
{
    if (!o) return;
    if (lv_obj_has_class(o, &lv_arc_class)) {
        lv_obj_set_style_arc_color(o, uk_c(hex), LV_PART_MAIN);
        lv_obj_set_style_arc_color(o, uk_c(hex), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(o, uk_c(hex), 0);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        icon_set_color(lv_obj_get_child(o, i), hex);
}

/* 图标的一笔：不参与点击、不滚动的实心盒。 */
static lv_obj_t *icon_px(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, uk_c(UK_T2), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* 图标的容器：fixed=true 时是 24×24 的画布，否则按内容撑开（嵌套用）。 */
static lv_obj_t *icon_box(lv_obj_t *parent, bool column, int32_t gap,
                          lv_flex_align_t main, bool fixed)
{
    lv_obj_t *c = lv_obj_create(parent);
    if (fixed) lv_obj_set_size(c, 24, 24);
    else       { lv_obj_set_width(c, LV_SIZE_CONTENT); lv_obj_set_height(c, LV_SIZE_CONTENT); }
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    if (column) lv_obj_set_style_pad_row(c, gap, 0);
    else        lv_obj_set_style_pad_column(c, gap, 0);
    lv_obj_set_flex_flow(c, column ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

/* kind = 页序：0 总览（表盘环）/ 1 存储（三张盘）/ 2 网络（三根柱）/ 3 系统（栅格）/
   4 温度（温度计）/ 5 告警（感叹号）。 */
static lv_obj_t *nav_icon_create(lv_obj_t *parent, int kind)
{
    lv_obj_t *box = icon_box(parent, true, UK_S1, LV_FLEX_ALIGN_CENTER, true);
    switch (kind) {
    case 0: {
        lv_obj_t *a = lv_arc_create(box);
        lv_obj_set_size(a, 20, 20);
        lv_arc_set_rotation(a, 135);
        lv_arc_set_bg_angles(a, 0, 270);
        lv_arc_set_range(a, 0, 100);
        lv_arc_set_value(a, 100);
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(a, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, 2, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
        break;
    }
    case 1:
        for (int i = 0; i < 3; i++) icon_px(box, 18, 4, 1);
        break;
    case 2: {
        lv_obj_t *row = icon_box(box, false, 2, LV_FLEX_ALIGN_END, false);
        static const int32_t hh[3] = { 8, 13, 18 };
        for (int i = 0; i < 3; i++) icon_px(row, 4, hh[i], 1);
        break;
    }
    case 3: {
        lv_obj_t *col = icon_box(box, true, 2, LV_FLEX_ALIGN_CENTER, false);
        for (int r = 0; r < 2; r++) {
            lv_obj_t *row = icon_box(col, false, 2, LV_FLEX_ALIGN_CENTER, false);
            for (int c = 0; c < 2; c++) icon_px(row, 9, 9, 1);
        }
        break;
    }
    case 4:
        icon_px(box, 6, 12, 3);
        icon_px(box, 12, 12, 6);
        break;
    default: {   /* 5 告警：竖排感叹号（杆 + 点） */
        lv_obj_t *col = icon_box(box, true, 2, LV_FLEX_ALIGN_CENTER, false);
        icon_px(col, 4, 10, 2);
        icon_px(col, 4, 4, 2);
        break;
    }
    }
    return box;
}

/* 左栏的表面色集中在这里：建树、换页、夜间切换都调它，保证三处不漂。
   三档层次：rail 最深（内凹槽）→ 导航项浮起一档 → 选中项再抬一档 + 品牌蓝叠加。
   flex 版只改颜色：坐标是 flex 的事，这里再也不用复位任何位置。 */
static void theme_apply(void)
{
    if (s_ui.rail) {
        lv_obj_set_style_bg_color(s_ui.rail, uk_c(s_night ? UK_RAIL_N : UK_RAIL_DAY), 0);
        lv_obj_set_style_bg_opa(s_ui.rail, LV_OPA_COVER, 0);
    }
    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        lv_obj_t *b = s_ui.nav[i];
        if (!b) continue;
        bool on = (i == s_page && !s_pair_open);
        /* 纯色 + 选中态抬一档表面：不做整块不透明蓝底（那会让导航比数据更抢眼） */
        lv_obj_set_style_bg_color(b, uk_c(on ? UK_NAV_SEL : UK_NAV), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(b, on ? uk_c(UK_PEACH) : lv_color_white(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        /* 蜜桃色边与底部标记共同标识当前页；普通边不与数据争主次。 */
        lv_obj_set_style_border_opa(b, on ? LV_OPA_COVER : UK_EDGE_OPA, 0);
        lv_obj_set_style_radius(b, UK_RADIUS, 0);
        if (s_ui.nav_mark[i]) {
            lv_obj_set_style_shadow_opa(s_ui.nav_mark[i], 0, 0);
            show(s_ui.nav_mark[i], on);
        }
        /* 选中项的图标用白色：21% 蓝底上的蓝图标对比不足，白图标才立得住 */
        icon_set_color(s_ui.nav_icon[i], on ? UK_T1 : UK_T2);
        if (s_ui.nav_lbl[i])
            lv_obj_set_style_text_color(s_ui.nav_lbl[i], uk_c(on ? UK_T1 : UK_T2), 0);
    }
    if (s_ui.pair_btn) {
        /* 配对入口从 dock 挪到顶栏后是"幽灵档入口"：不抢眼，状态全部交给那颗点。
           只有配对卡真的开着时才给它一块浅底，作为"你现在在看它"的回执。 */
        lv_obj_set_style_bg_color(s_ui.pair_btn, uk_c(UK_SURF_S2), 0);
        lv_obj_set_style_bg_opa(s_ui.pair_btn, s_pair_open ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (s_ui.pair_btn_lbl)
            lv_obj_set_style_text_color(s_ui.pair_btn_lbl, uk_c(UK_T2), 0);
        if (s_ui.pair_dot) {   /* 入口的状态点：颜色随配对阶段走（见 pair_refresh） */
            lv_obj_set_style_bg_color(s_ui.pair_dot, uk_c(s_pair_alert), 0);
            lv_obj_set_style_shadow_color(s_ui.pair_dot, uk_c(s_pair_alert), 0);
            lv_obj_set_style_shadow_opa(s_ui.pair_dot, 0, 0);
        }
    }
}

static void build_rail(lv_obj_t *rail)
{
    s_ui.rail = rail;
    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        lv_obj_t *b = lv_obj_create(rail);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_size(b, 0, LV_PCT(100));
        lv_obj_set_style_min_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_style_pad_column(b, UK_S2, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        /* A press acknowledges input immediately without moving the dock. */
        lv_obj_set_style_bg_color(b, uk_c(UK_NAV_SEL), LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(b, UK_FOCUS_RING_OPA, LV_STATE_PRESSED);
        s_ui.nav[i] = b;
        s_ui.nav_mark[i] = icon_px(b, UK_S5, UK_S1, 1);
        lv_obj_add_flag(s_ui.nav_mark[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(s_ui.nav_mark[i], LV_ALIGN_BOTTOM_MID, 0, -UK_S1);
        lv_obj_set_style_bg_color(s_ui.nav_mark[i], uk_c(UK_PEACH), 0);
        s_ui.nav_icon[i] = nav_icon_create(b, i);
        s_ui.nav_lbl[i] = uk_label(b, UK_FONT_CJK_16, UK_T2, NAV_TXT[i]);
        not_clickable(s_ui.nav_lbl[i]);
        lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    /* 配对不再占一格导航（它不是一页）：入口在顶栏，见 build_header。 */
}

static void build_signal_bars(lv_obj_t *parent)
{
    static const int32_t hh[4] = { 7, 11, 15, 19 };
    lv_obj_t *row = icon_box(parent, false, 2, LV_FLEX_ALIGN_END, false);
    lv_obj_set_height(row, 19);
    for (int i = 0; i < 4; i++) {
        s_ui.h_bars[i] = icon_px(row, 5, hh[i], 1);
        lv_obj_set_style_bg_color(s_ui.h_bars[i], uk_c(UK_SURF_S3), 0);
    }
}

static void signal_set(int8_t rssi)
{
    int lit = 0;
    if (rssi <= -1) {
        if (rssi >= -55)      lit = 4;
        else if (rssi >= -65) lit = 3;
        else if (rssi >= -75) lit = 2;
        else                  lit = 1;
    }
    for (int i = 0; i < 4; i++) {
        /* 信号强度不是"健康状态"：用文本色点亮，不借用状态绿 */
        if (s_ui.h_bars[i])
            lv_obj_set_style_bg_color(s_ui.h_bars[i], uk_c(i < lit ? UK_T1 : UK_SURF_S3), 0);
    }
}

/* 状态胶囊：左边一个会发光的状态点 + 右边一句话。chip 的第 0 个子对象是状态点。 */
static lv_obj_t *chip_create(lv_obj_t *parent, const char *txt, uint32_t rgb)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_height(c, 32);
    lv_obj_set_width(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, uk_c(UK_SURF_S2), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, UK_RADIUS_SM, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_hor(c, UK_S3, 0);
    lv_obj_set_style_pad_column(c, UK_S2, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    uk_dot(c, rgb, UK_DOT);
    lv_obj_t *lbl = uk_label(c, UK_FONT_CJK_12, UK_T3, txt);
    not_clickable(lbl);
    not_clickable(c);
    return c;
}

static void build_header(lv_obj_t *h)
{
    /* Controls take their text width; the host column uses the actual remaining width. */
    lv_obj_t *hostcol = uk_col(h, 1);
    lv_obj_set_style_min_width(hostcol, 0, 0);
    s_ui.h_host = uk_label(hostcol, UK_FONT_CJK_16, UK_T1, "fnOS");
    lv_obj_set_width(s_ui.h_host, LV_PCT(100));
    lv_obj_set_height(s_ui.h_host, lv_font_get_line_height(UK_FONT_CJK_16));
    s_ui.h_ep = uk_label(hostcol, UK_FONT_CJK_12, UK_T3, "等待采集");
    lv_obj_set_width(s_ui.h_ep, LV_PCT(100));
    lv_obj_set_height(s_ui.h_ep, lv_font_get_line_height(UK_FONT_CJK_12));
    not_clickable(hostcol);

    lv_point_t wifi_size, diagnostics_size;
    lv_text_get_size(&wifi_size, "Wi-Fi 未配置", UK_FONT_CJK_16, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&diagnostics_size, "设置", UK_FONT_CJK_16, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    /* 顶栏这两颗是"随时可按的入口"，不是状态，用幽灵档（无底无框）。
       这样顶栏唯一的高饱和元素就只剩状态胶囊（在线/离线）。 */
    s_ui.wifi_btn = flex_btn(h, "Wi-Fi 未配置", BTN_GHOST, UK_FONT_CJK_12, wifi_size.x + 2 * UK_S2);
    s_ui.wifi_btn_lbl = lv_obj_get_child(s_ui.wifi_btn, 0);
    lv_obj_set_width(s_ui.wifi_btn_lbl, LV_PCT(100));
    lv_obj_set_height(s_ui.wifi_btn_lbl, lv_font_get_line_height(UK_FONT_CJK_12));
    lv_obj_set_style_text_align(s_ui.wifi_btn_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(s_ui.wifi_btn, wifi_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 诊断按钮只在系统页露面（它进的是那一页的覆盖层） */
    /* 配对入口：原先是 dock 里的第七格（与六页导航并列，等于告诉用户"配对也是一页"），
       现在收进顶栏 = 设备域入口。标签保持"配对"，预览断言与用户话术都不用改。 */
    lv_point_t pair_size;
    lv_text_get_size(&pair_size, "配对", UK_FONT_CJK_16, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    s_ui.pair_btn = flex_btn(h, "配对", BTN_GHOST, UK_FONT_CJK_12, pair_size.x + UK_DOT + 4 * UK_S2);
    s_ui.pair_btn_lbl = lv_obj_get_child(s_ui.pair_btn, 0);
    lv_obj_set_style_pad_column(s_ui.pair_btn, UK_S2, 0);
    /* 状态点插到文字前面（flex_btn 只建了一个文字子对象） */
    s_ui.pair_dot = uk_dot(s_ui.pair_btn, UK_WARN, UK_DOT);
    lv_obj_move_to_index(s_ui.pair_dot, 0);
    lv_obj_add_event_cb(s_ui.pair_btn, pair_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 设置按钮（原"采集诊断"覆盖层）：与 Wi-Fi 并列的设备域入口，常驻可见 ——
       诊断不再是"系统页里的一个按钮"，而是全局可达的一层。 */
    s_ui.diagnostics_button = flex_btn(h, "设置", BTN_GHOST, UK_FONT_CJK_12, diagnostics_size.x + 2 * UK_S2);
    s_ui.diagnostics_label = lv_obj_get_child(s_ui.diagnostics_button, 0);
    lv_obj_add_event_cb(s_ui.diagnostics_button, diagnostics_cb, LV_EVENT_CLICKED, NULL);

    build_signal_bars(h);
    s_ui.h_chip = chip_create(h, "等待数据", UK_T3);

}

/* 底栏：轮询统计 + 告警带（v11 版面的 foot）。带子里的那条告警是"最严重的一条"，
   点它进系统页看全部 —— 老实现没有底栏，这是按设计稿补的全局状态行。 */
static void build_foot(lv_obj_t *foot)
{
    s_ui.foot_poll = uk_label(foot, UK_FONT_CJK_12, UK_T3, "轮询 1s · ok 0 · fail 0");
    not_clickable(s_ui.foot_poll);

    lv_obj_t *band = lv_obj_create(foot);
    lv_obj_set_flex_grow(band, 1);
    lv_obj_set_height(band, lv_font_get_line_height(UK_FONT_CJK_12) + UK_S1);
    lv_obj_set_style_bg_color(band, uk_c(UK_S0), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(band, UK_RADIUS_SM, 0);
    lv_obj_set_style_border_color(band, uk_c(UK_LINE), 0);
    lv_obj_set_style_border_width(band, 1, 0);
    lv_obj_set_style_pad_hor(band, UK_S3, 0);
    lv_obj_set_style_pad_column(band, UK_S2, 0);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    /* 底栏这条带子升级成"随时可进的告警入口"：哪一页都能点，点它进告警页。
       文案一个字不改（预览断言按文案查），只多一个按压回执。 */
    lv_obj_set_style_bg_color(band, uk_c(UK_SURF_S1), LV_STATE_PRESSED);
    lv_obj_add_flag(band, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(band, foot_band_cb, LV_EVENT_CLICKED, NULL);
    s_ui.foot_dot = uk_dot(band, UK_OK, UK_DOT);
    s_ui.foot_txt = uk_label(band, UK_FONT_CJK_12, UK_T3, "等待数据");
    lv_obj_set_flex_grow(s_ui.foot_txt, 1);
    lv_obj_set_style_min_width(s_ui.foot_txt, 0, 0);
    not_clickable(s_ui.foot_txt);
}


/* 卡内的"整宽粗条"（总容量那种 hero 条）：uk_kpi 里的条是 4 高，这里要 8 高 */
static lv_obj_t *hero_bar(lv_obj_t *parent)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_height(b, UK_BAR_H_HERO);
    lv_bar_set_range(b, 0, 100);
    lv_obj_set_style_radius(b, UK_BAR_H_HERO / 2, 0);
    lv_obj_set_style_bg_color(b, uk_c(UK_SURF_S3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, uk_c(UK_OK), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    return b;
}

/* ── 图表读数 ────────────────────────────────────────────────────────
   uk_trend 是原生 lv_chart，只负责画；"峰值"这类业务读数得自己遍历序列。
   未写入的点是 LV_CHART_POINT_NONE(=INT32_MAX)，不能当数据参与比较。 */
static int32_t chart_peak(lv_obj_t *chart, int32_t idx)
{
    if (!chart) return 0;
    lv_chart_series_t *se = uk_trend_series(chart, idx);
    int32_t *a = se ? lv_chart_get_series_y_array(chart, se) : NULL;
    if (!a) return 0;
    int32_t peak = 0;
    for (int i = 0; i < UI_HIST; i++)
        if (a[i] != LV_CHART_POINT_NONE && a[i] > peak) peak = a[i];
    return peak;
}

static int chart_sample_count(lv_obj_t *chart)
{
    lv_chart_series_t *series = uk_trend_series(chart, 0);
    const int32_t *values = lv_chart_get_series_y_array(chart, series);
    int count = 0;
    for (uint32_t i = 0; i < lv_chart_get_point_count(chart); i++)
        if (values[i] != LV_CHART_POINT_NONE) count++;
    return count;
}

/* 把整条曲线推空（"从没采到过数据"时不留上一轮的残影） */
static void chart_clear(lv_obj_t *chart, int32_t series)
{
    for (int32_t i = 0; i < series; i++) {
        lv_chart_series_t *se = uk_trend_series(chart, i);
        if (se) lv_chart_set_all_values(chart, se, LV_CHART_POINT_NONE);
    }
}

/* 状态点光晕：0 偏移 + spread = 均匀光晕，不是投影 */
static void dot_glow(lv_obj_t *o, uint32_t hex, lv_opa_t opa)
{
    if (!o) return;
    lv_obj_set_style_shadow_color(o, uk_c(hex), 0);
    lv_obj_set_style_shadow_width(o, UK_GLOW_W, 0);
    lv_obj_set_style_shadow_offset_x(o, 0, 0);
    lv_obj_set_style_shadow_offset_y(o, 0, 0);
    lv_obj_set_style_shadow_spread(o, 2, 0);
    lv_obj_set_style_shadow_opa(o, opa, 0);
}

/* "12.2 MB/s" → 数值与单位拆开：uk_kpi 的 val/unit 是两个 label，
   单位要小一号（数字 32、单位 12），拼成一串就只能一样大。 */
static void rate_split(char *b, const char **val, const char **unit)
{
    char *sp = strchr(b, ' ');
    if (sp) { *sp = '\0'; *val = b; *unit = sp + 1; }
    else    { *val = b; *unit = ""; }
}

/* Trend bodies fill remaining height. The overview row also fills the cross axis;
   the network page leaves its card height to column flex growth. */
static lv_obj_t *trend_card(lv_obj_t *page, const char *title, int32_t series,
                            lv_obj_t **out_card, lv_obj_t **out_note)
{
    lv_obj_t *card = ui_card(page, title, NULL);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_min_height(card, 140, 0);
    lv_obj_set_flex_grow(uk_card_body(card), 1);
    if (out_card) *out_card = card;
    if (out_note) *out_note = uk_card_note(card);
    return uk_trend_create(uk_card_body(card), UI_HIST, series);
}

/* A compact current-value gauge and a real history track share one object card.
   Geometry comes from typography and flex allocation, not panel coordinates. */
static lv_obj_t *overview_card(lv_obj_t *parent, const char *title, const char *note)
{
    lv_obj_t *card = ui_card(parent, title, note);
    uk_card_flex(card, 1);
    lv_obj_set_style_pad_all(card, UK_S2, 0);
    lv_obj_set_style_pad_row(card, UK_S1, 0);
    lv_obj_set_style_pad_row(uk_card_body(card), UK_S1, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(uk_card_body(card), LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *overview_resource_create(lv_obj_t *parent, int index,
                                          const char *title, uint32_t identity)
{
    const bool compact = lv_display_get_vertical_resolution(NULL) < UK_SCR_H;
    const lv_font_t *number = compact ? UK_FONT_NUM_20 : UK_FONT_NUM_32;
    overview_resource_t *r = &s_ui.overview_resource[index];
    lv_obj_t *card = overview_card(parent, title, NULL);
    r->card = card;
    lv_obj_set_style_bg_color(card, uk_c(index == 0 ? UK_HERO : UK_MINT), 0);
    lv_obj_set_style_text_color(uk_card_note(card), uk_c(UK_T2), 0);
    lv_obj_t *body = uk_card_body(card);
    lv_obj_set_style_pad_row(body, UK_S1, 0);
    r->chart = uk_trend_create(body, UI_HIST, 1);
    uk_trend_set_range(r->chart, 0, 100);
    lv_chart_set_series_color(r->chart, uk_trend_series(r->chart, 0), uk_c(identity));
    lv_chart_set_div_line_count(r->chart, 0, 0);
    lv_obj_set_style_line_opa(r->chart, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_min_height(r->chart, UK_S4, 0);
    if (index == 0) {
        lv_obj_t *space = uk_col(body, 1);
        lv_obj_set_width(space, LV_PCT(100));
        lv_obj_set_style_min_height(space, 0, 0);
        if (compact) show(space, false);
        /* 首页的优先级阶梯里"运行时长"排在最后（告警 > 温度 > 阵列 > 网络 > 容器 > 运行时长），
           而 92px 的时钟要吃掉 443px 页高的 1/5 —— 下面那排磁贴就会被页底静默裁掉。
           62px 仍然是这一页最重的字，但不再挤掉真正要突出的读数。 */
        const lv_font_t *font = UK_FONT_DISPLAY_64;
        s_ui.overview_clock = uk_label(body, font, UK_T1, "--:--");
        const lv_font_t *note_font = compact ? UK_FONT_CJK_12 : UK_FONT_CJK_16;
        s_ui.overview_clock_note = uk_label(body, note_font, UK_T2, "等待运行时长");
        lv_obj_set_width(s_ui.overview_clock_note, LV_PCT(100));
        lv_obj_set_height(s_ui.overview_clock_note, lv_font_get_line_height(note_font));
    }
    lv_obj_t *metric = uk_row_box(body, 0);
    lv_obj_set_style_pad_column(metric, UK_S1, 0);
    lv_obj_set_flex_align(metric, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    if (index == 0) uk_label(metric, UK_FONT_CJK_16, UK_T2, "CPU");
    r->value = uk_label(metric, number, UK_T1, "--");
    r->unit = uk_label(metric, UK_FONT_CJK_12, UK_T3, "%");
    /* 英雄卡占满整页宽，两条 CJK12 脚注并排成一行：省下一整行 21px 给下面那排磁贴。
       右列的内存卡只有 1/3 页宽，两串并排会互相挤，仍保持上下两行。 */
    lv_obj_t *footnotes = index == 0 ? uk_row_box(body, 0) : body;
    if (index == 0) lv_obj_set_style_pad_column(footnotes, UK_S2, 0);
    r->meta = uk_label(footnotes, UK_FONT_CJK_12, UK_T3, "等待数据");
    if (index != 0) lv_obj_set_width(r->meta, LV_PCT(100));
    lv_obj_set_height(r->meta, lv_font_get_line_height(UK_FONT_CJK_12));
    r->context = uk_label(footnotes, UK_FONT_CJK_12, UK_T3, "等待历史采样");
    if (index != 0) lv_obj_set_width(r->context, LV_PCT(100));
    lv_obj_set_height(r->context, lv_font_get_line_height(UK_FONT_CJK_12));
    if (compact) show(r->context, false);
    if (index == 1) {
        int32_t minimum = 2 * (UK_S2 + 1) + lv_font_get_line_height(UK_FONT_CJK_16) +
                          lv_font_get_line_height(number) +
                          lv_font_get_line_height(UK_FONT_CJK_12) * (compact ? 1 : 2) +
                          UK_S4 + UK_S1 * (compact ? 3 : 4);
        lv_obj_set_style_min_height(card, minimum, 0);
    }
    if (index == 0) {
        lv_obj_set_flex_grow(r->chart, 0);
        lv_obj_set_height(r->chart, UK_S5);
        lv_obj_move_to_index(r->chart, -1);
    }
    return card;
}

static void build_p0(lv_obj_t *page)
{
    ui_page(page);
    /* 页首"结论带"：把原来那两行小字（健康结论 + 最热温度）升级成整页宽的一条。
       健康时它是一行安静的绿点状态；有严重告警时它是红底 + 首条消息 + 「全部 →」。
       它在最上面、可点、随时能进告警页 —— 这是"重要性 > 版面整洁"的第一条实现。 */
    lv_obj_t *band = lv_obj_create(page);
    lv_obj_set_width(band, LV_PCT(100));
    lv_obj_set_height(band, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(band, uk_c(UK_SURF_S1), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(band, UK_RADIUS, 0);
    lv_obj_set_style_border_color(band, uk_c(UK_LINE), 0);
    lv_obj_set_style_border_width(band, 1, 0);
    lv_obj_set_style_pad_hor(band, UK_S3, 0);
    lv_obj_set_style_pad_ver(band, UK_S2, 0);
    lv_obj_set_style_pad_column(band, UK_S2, 0);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(band, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(band, uk_c(UK_SURF_S2), LV_STATE_PRESSED);
    lv_obj_add_event_cb(band, foot_band_cb, LV_EVENT_CLICKED, NULL);
    s_ui.home_band = band;
    s_ui.home_band_dot = uk_dot(band, UK_OK, UK_DOT);
    s_ui.health = uk_label(band, UK_FONT_CJK_16, UK_T1, "等待数据");
    s_ui.home_band_msg = uk_label(band, UK_FONT_CJK_12, UK_T3, "");
    lv_obj_set_flex_grow(s_ui.home_band_msg, 1);
    lv_obj_set_style_min_width(s_ui.home_band_msg, 0, 0);
    lv_label_set_long_mode(s_ui.home_band_msg, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_ui.home_band_msg, lv_font_get_line_height(UK_FONT_CJK_12));
    s_ui.home_band_more = uk_label(band, UK_FONT_CJK_12, UK_T3, "全部 →");
    lv_obj_add_flag(s_ui.home_band_more, LV_OBJ_FLAG_HIDDEN);

    /* The unboxed status line belongs to the resource group, with a tighter internal gap. */
    lv_obj_t *resource_group = uk_col(page, UK_HERO_GROW);
    lv_obj_set_width(resource_group, LV_PCT(100));
    lv_obj_set_height(resource_group, 0);
    lv_obj_set_style_pad_row(resource_group, UK_S1, 0);

    lv_obj_t *resources = uk_row_box(resource_group, 1);
    lv_obj_set_style_pad_column(resources, UK_CARD_GAP, 0);
    lv_obj_t *hero = overview_resource_create(resources, 0, "NAS · 运行时长", UK_S_CPU);
    lv_obj_set_flex_grow(hero, UK_HERO_GROW);
    lv_obj_t *aux = uk_col(resources, UK_AUX_GROW);
    lv_obj_set_height(aux, LV_PCT(100));
    lv_obj_set_style_pad_row(aux, UK_CARD_GAP, 0);
    lv_obj_t *memory = overview_resource_create(aux, 1, "内存使用率", UK_S_MEM);
    lv_obj_set_flex_grow(memory, 2);
    lv_obj_set_width(memory, LV_PCT(100));
    lv_obj_set_height(memory, 0);
    /* 容器明细归系统页（那里有整列容器卡，点一行还能展开）。首页只在结论带里
       说一句「容器 N/N 运行」——重复一张卡会把右列顶高 104px，而页高只有 443px。 */
    s_ui.overview_services = NULL;
    s_ui.overview_service_note = NULL;
    lv_obj_set_style_min_height(resources, lv_obj_get_style_min_height(memory, 0), 0);
    lv_obj_set_style_min_height(resource_group, lv_obj_get_style_min_height(resources, 0), 0);

    /* 磁贴行：网络 / 存储 / 最热温度 三格等宽（12 栅格里的 4+4+4）。
       每格都是"点一下进那一页"的入口：概览只放结论，明细在下钻页里。 */
    lv_obj_t *objects = uk_grid_row(page, 1);
    lv_obj_t *network = overview_card(objects, "网络吞吐", "下行 / 上行");
    uk_grid_span(network, 4);
    lv_obj_t *body = uk_card_body(network);
    lv_obj_t *rates = uk_row_box(body, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *col = uk_col(rates, 1);
        lv_obj_t *vr = uk_row_box(col, 0);
        lv_obj_set_style_pad_column(vr, UK_S1, 0);
        lv_obj_set_flex_align(vr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        s_ui.overview_rate[i] = uk_label(vr, UK_FONT_NUM_20, i == 0 ? UK_S_DOWN : UK_S_UP, "--");
        s_ui.overview_unit[i] = uk_label(vr, UK_FONT_CJK_12, UK_T3, "");
    }
    /* 采样数与量程紧跟读数，图表排在最后：图表是这一列里唯一会伸缩的构件，
       卡片被行高挤压时先让图表少几个像素，而不是把半行文字切在卡片下沿。 */
    s_ui.overview_net_context = uk_label(body, UK_FONT_CJK_12, UK_T3, "等待历史采样");
    lv_obj_set_width(s_ui.overview_net_context, LV_PCT(100));
    lv_obj_set_height(s_ui.overview_net_context, lv_font_get_line_height(UK_FONT_CJK_12));
    s_ui.overview_net = uk_trend_create(body, UI_HIST, 2);
    lv_chart_set_series_color(s_ui.overview_net, uk_trend_series(s_ui.overview_net, 0), uk_c(UK_S_DOWN));
    lv_chart_set_series_color(s_ui.overview_net, uk_trend_series(s_ui.overview_net, 1), uk_c(UK_S_UP));
    lv_chart_set_div_line_count(s_ui.overview_net, 0, 0);
    lv_obj_set_style_min_height(s_ui.overview_net, UK_S4, 0);

    lv_obj_t *storage = overview_card(objects, "存储容量", NULL);
    uk_grid_span(storage, 4);
    body = uk_card_body(storage);
    lv_obj_t *sr = uk_row_box(body, 0);
    s_ui.capacity = uk_label(sr, UK_FONT_NUM_20, UK_T1, "--");
    s_ui.capacity_detail = uk_label(sr, UK_FONT_CJK_12, UK_T3, "等待存储数据");
    lv_obj_set_flex_grow(s_ui.capacity_detail, 1);
    lv_obj_set_height(s_ui.capacity_detail, lv_font_get_line_height(UK_FONT_CJK_12));
    s_ui.capacity_bar = hero_bar(body);
    /* Compact comparison strip; full identities and capacity are on Storage.
       The pool still scrolls when its measured content exceeds this viewport. */
    s_ui.overview_volumes = uk_col(body, 1);
    lv_obj_set_width(s_ui.overview_volumes, LV_PCT(100));
    /* 只要求露出一整行（其余靠滚动与 uk_viewport_snap）：UK_ROW_MIN 的 40px
       下限会把整行高度顶高 11px，磁贴因此挤掉上面那排卡。 */
    int32_t list_min = lv_font_get_line_height(UK_FONT_CJK_12) + UK_S1 + UK_BAR_H;
    lv_obj_set_style_min_height(s_ui.overview_volumes, list_min, 0);
    lv_obj_set_style_pad_row(s_ui.overview_volumes, UK_S1, 0);
    lv_obj_set_style_pad_right(s_ui.overview_volumes, UK_SCROLL_W + UK_S1, 0);
    lv_obj_set_style_width(s_ui.overview_volumes, UK_SCROLL_W, LV_PART_SCROLLBAR);
    lv_obj_add_flag(s_ui.overview_volumes, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_ui.overview_volumes, LV_DIR_VER);
    s_ui.storage_note = uk_label(body, UK_FONT_CJK_12, UK_T3, "");
    lv_obj_set_width(s_ui.storage_note, LV_PCT(100));
    lv_obj_set_height(s_ui.storage_note, lv_font_get_line_height(UK_FONT_CJK_12));

    /* 最热温度磁贴：首页要能一眼看到"有没有一路在发烫"。
       大字是温度本身，副行是传感器名，脚注沿用原来那行"最高 … · 容器状态"。 */
    lv_obj_t *temp = overview_card(objects, "最热温度", NULL);
    uk_grid_span(temp, 4);
    s_ui.home_temp_card = temp;

    /* 三格磁贴整卡可点：概览只给结论，明细在那一页里（按下有底色反馈）。 */
    lv_obj_t *jump[] = { network, storage, temp };
    const int jump_page[] = { 2, 1, 4 };
    for (unsigned j = 0; j < sizeof jump / sizeof jump[0]; j++) {
        lv_obj_add_flag(jump[j], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(jump[j], uk_c(UK_SURF_S2), LV_STATE_PRESSED);
        lv_obj_add_event_cb(jump[j], page_jump_cb, LV_EVENT_CLICKED, (void *)(intptr_t)jump_page[j]);
    }
    body = uk_card_body(temp);
    lv_obj_t *tv = uk_row_box(body, 0);
    lv_obj_set_style_pad_column(tv, UK_S1, 0);
    lv_obj_set_flex_align(tv, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_ui.home_temp_dot = uk_dot(tv, UK_T3, UK_DOT);
    s_ui.home_temp_value = uk_label(tv, UK_FONT_NUM_32, UK_T1, "--");
    s_ui.home_temp_unit = uk_label(tv, UK_FONT_CJK_12, UK_T3, "°C");
    /* 传感器名并到数值行里（原来自己占一整行 21px）：名字长就省略号，不折行。 */
    s_ui.home_temp_name = uk_label(tv, UK_FONT_CJK_12, UK_T2, "等待温度采集");
    lv_obj_set_flex_grow(s_ui.home_temp_name, 1);
    lv_obj_set_style_min_width(s_ui.home_temp_name, 0, 0);
    lv_label_set_long_mode(s_ui.home_temp_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_ui.home_temp_name, lv_font_get_line_height(UK_FONT_CJK_12));
    lv_obj_set_style_pad_bottom(s_ui.home_temp_name, UK_S1, 0);
    s_ui.overview_note = uk_label(body, UK_FONT_CJK_12, UK_T3, "");
    lv_obj_set_width(s_ui.overview_note, LV_PCT(100));
    lv_obj_set_height(s_ui.overview_note, lv_font_get_line_height(UK_FONT_CJK_12));

    int32_t strip_min = 2 * (UK_S2 + 1) + lv_font_get_line_height(UK_FONT_CJK_16) +
                        lv_font_get_line_height(UK_FONT_NUM_20) +
                        lv_font_get_line_height(UK_FONT_CJK_12) +
                        UK_BAR_H_HERO + list_min + 4 * UK_S1;
    lv_obj_set_style_min_height(objects, strip_min, 0);
}

static void build_p1(lv_obj_t *page)
{
    /* 三块页（v12：几何全由 flex 决定，不再有栅格常量）：
         [已用空间 / 总容量 / 可用空间  三栏页头]
         [存储卷池（吃满余量，2/3 宽）][阵列健康]
         [磁盘活动（整行：读写在第二行，窄列放不下两行行）]
       "容量摘要"从一张独立卡改成页头一条：三栏只有"标签 + 数值"两行，
       单独占 88px 高是浪费，而那 88px 正好留给池的行高。 */
    ui_page(page);
    lv_obj_t *summary = ui_card(page, NULL, NULL);
    uk_card_flex(summary, 0);
    static const char *const titles[] = { "已用空间", "总容量", "可用空间" };
    lv_obj_set_style_pad_all(summary, UK_S2, 0);   /* 12 → 8：摘要卡只有标签+数值两行 */
    lv_obj_t *srow = uk_row_box(uk_card_body(summary), 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *col = uk_col(srow, 1);
        uk_label(col, UK_FONT_CJK_12, UK_T3, titles[i]);
        s_ui.storage_value[i] = uk_label(col, UK_FONT_NUM_32, UK_T1, "-");
        lv_obj_set_width(s_ui.storage_value[i], LV_PCT(100));
        lv_label_set_long_mode(s_ui.storage_value[i], LV_LABEL_LONG_MODE_WRAP);
    }

    /* 一条"用了多少"的横条压在三个数字下面：数字要读，横条要一眼看出来。
       它是这一页的焦点（容量结论），下面三张卡是明细。 */
    s_ui.storage_bar = hero_bar(uk_card_body(summary));

    panel_peach(summary);

    /* 主区：卷清单占 2/3 宽、整行高（这一页的主问题是"空间还够吗"），
       阵列健康贴在右侧（它只有一两行，不需要更多）。
       高度分配 3:2 是量出来的：阵列卡只有一列（列宽 278 放不下两列），
       4 行 × 44 需要 176px 净高，加上卡头与内边距就是 230 —— 均分（各 1）
       时主区只有 208，第 4 行会被卡底边切掉半行。 */
    lv_obj_t *main_row = uk_row_box(page, 1);
    s_ui.storage_cards = main_row;
    lv_obj_set_flex_flow(main_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(main_row, UK_CARD_GAP, 0);
    lv_obj_set_style_pad_column(main_row, UK_CARD_GAP, 0);
    s_ui.vol_card = ui_card(main_row, "存储卷", NULL);
    uk_card_flex(s_ui.vol_card, 2);
    {
        lv_obj_t *body = uk_card_body(s_ui.vol_card);
        s_ui.vol_empty = empty_box(body, "未采集到存储卷", NULL);
        s_ui.vol_pool = uk_pool_create(body, UK_LIST_MIN_WIDTH);
        lv_obj_set_style_pad_row(s_ui.vol_pool, UK_ITEM_GAP, 0);
        uk_pool_stretch(s_ui.vol_pool, false);
        s_ui.vol_made = 0;
    }

    s_ui.raid_card = ui_card(main_row, "阵列健康", NULL);
    uk_card_flex(s_ui.raid_card, 1);
    {
        lv_obj_t *body = uk_card_body(s_ui.raid_card);
        s_ui.raid_empty = empty_box(body, "未采集到阵列", NULL);
        s_ui.raid_pool = uk_pool_create(body, UK_LIST_MIN_WIDTH);
        lv_obj_set_style_pad_row(s_ui.raid_pool, UK_ITEM_GAP, 0);
        uk_pool_stretch(s_ui.raid_pool, false);
        s_ui.raid_made = 0;
    }

    /* 磁盘活动：整行。行的第二行放"读 / 写"，所以池的列宽给得比卷池宽
       （窄列里设备名会换行，两行行变三行，10 块盘就一屏放不下了）。 */
    s_ui.disk_card = ui_card(main_row, "磁盘活动", NULL);
    uk_card_flex(s_ui.disk_card, 2);
    {
        lv_obj_t *body = uk_card_body(s_ui.disk_card);
        s_ui.disk_empty = empty_box(body, "未采集到磁盘活动", NULL);
        s_ui.disk_pool = uk_pool_create(body, UK_LIST_MIN_WIDTH);
        lv_obj_set_style_pad_row(s_ui.disk_pool, UK_ITEM_GAP, 0);
        uk_pool_stretch(s_ui.disk_pool, false);
        s_ui.disk_made = 0;
    }
}

static void build_p2(lv_obj_t *page)
{
    ui_page(page);


    /* 吞吐趋势：标题与量程随数据窗口变（在刷新里改），图区吃掉卡内剩余高度 */
    lv_obj_t *tcard = NULL;
    s_ui.tr_net = trend_card(page, "网络吞吐 · 最近 3 分钟", 2, &tcard, &s_ui.net_axis);
    s_ui.net_title = uk_card_title(tcard);
    /* 图表优先：这一页的主问题是"链路现在什么状态"，图先给，KPI 条在它下面。 */
    lv_obj_set_flex_grow(tcard, 2);
    lv_chart_series_t *se_tx = uk_trend_series(s_ui.tr_net, 0);
    lv_chart_series_t *se_rx = uk_trend_series(s_ui.tr_net, 1);
    if (se_tx) lv_chart_set_series_color(s_ui.tr_net, se_tx, uk_c(UK_S_UP));
    if (se_rx) lv_chart_set_series_color(s_ui.tr_net, se_rx, uk_c(UK_S_DOWN));
    lv_obj_t *rd = uk_row_box(uk_card_body(tcard), 0);
    uk_label(rd, UK_FONT_CJK_12, UK_S_UP, "上行");
    s_ui.tr_tx_lbl = uk_label(rd, UK_FONT_CJK_16, UK_T2, "-");
    lv_obj_set_flex_grow(s_ui.tr_tx_lbl, 1);
    uk_label(rd, UK_FONT_CJK_12, UK_S_DOWN, "下行");
    s_ui.tr_rx_lbl = uk_label(rd, UK_FONT_CJK_16, UK_T2, "-");
    lv_obj_set_flex_grow(s_ui.tr_rx_lbl, 1);

    /* Seed from typography; network_layout resolves the actual content after wrapping. */
    static const char *const title[] = { "上行速率", "下行速率", "双向合计", "采集延迟" };
    const uint32_t           tcol[]  = { UK_S_UP, UK_S_DOWN, UK_T1, UK_T1 };  /* 调色板是运行时变量，不能 static */
    lv_obj_t *krow = uk_row_box(page, 0);
    lv_obj_set_flex_flow(krow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(krow, UK_CARD_GAP, 0);
    lv_obj_set_style_pad_column(krow, UK_CARD_GAP, 0);
    lv_obj_set_height(krow, lv_font_get_line_height(UK_FONT_NUM_32) +
                           2 * lv_font_get_line_height(UK_FONT_CJK_12) + 2 * UK_S3 + 3 * UK_S1);
    for (int i = 0; i < 4; i++) {
        s_ui.kpi2[i] = uk_kpi_create(krow, title[i]);
        if (s_ui.ncards < UI_CARDS) s_ui.cards[s_ui.ncards++] = s_ui.kpi2[i]->box;
        /* 数值色 = 曲线色：上行紫、下行青，和趋势图两条线一一对应 */
        lv_obj_set_style_text_color(s_ui.kpi2[i]->val, uk_c(tcol[i]), 0);
    }

    /* 这一页只留三段：吞吐趋势（此刻链路）→ 四张速率 KPI（一眼读数，注脚已带
       "累计 X"与接口名）→ 网口清单（自适应 N 个口）。原来的"网卡信息条"与 KPI 条
       完全重复（累计收发在 KPI 注脚、接口名在 KPI 注脚与清单行、采集耗时=采集延迟、
       成功率=ok/fail），删掉它省下 134px —— 否则网口清单会被 443px 的页底静默裁掉。 */
    s_ui.net_note = NULL;
    s_ui.net_card = ui_card(page, "网口与接口", NULL);
    uk_card_flex(s_ui.net_card, 1);
    lv_obj_set_style_min_height(s_ui.net_card,
        2*UK_S3 + 2*lv_font_get_line_height(UK_FONT_CJK_16) + UK_ROW_H, 0);
    s_ui.net_pool = uk_pool_create(uk_card_body(s_ui.net_card), UK_LIST_MIN_WIDTH);
    uk_pool_stretch(s_ui.net_pool, false);
}

static void network_layout(void)
{
    if (!s_ui.kpi2[0]) return;
    lv_obj_t *row = lv_obj_get_parent(s_ui.kpi2[0]->box);
    lv_obj_update_layout(row);
    int32_t width = lv_obj_get_content_width(row);
    if (width <= 0) return;
    const unsigned count = sizeof s_ui.kpi2 / sizeof s_ui.kpi2[0];
    int32_t min_width = 0;
    for (unsigned i = 0; i < count; i++) {
        uk_kpi_t *k = s_ui.kpi2[i];
        lv_point_t value, unit, title;
        lv_text_get_size(&value, lv_label_get_text(k->val), lv_obj_get_style_text_font(k->val, 0),
                         lv_obj_get_style_text_letter_space(k->val, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_text_get_size(&unit, lv_label_get_text(k->unit), lv_obj_get_style_text_font(k->unit, 0),
                         lv_obj_get_style_text_letter_space(k->unit, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_text_get_size(&title, lv_label_get_text(k->label), lv_obj_get_style_text_font(k->label, 0),
                         lv_obj_get_style_text_letter_space(k->label, 0), 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int32_t need = LV_MAX(title.x, value.x + unit.x +
                             lv_obj_get_style_pad_column(lv_obj_get_parent(k->val), 0));
        need += lv_obj_get_style_pad_left(k->box, 0) + lv_obj_get_style_pad_right(k->box, 0) +
                2 * lv_obj_get_style_border_width(k->box, 0) + UK_S2;
        min_width = LV_MAX(min_width, need);
    }
    int32_t gap = lv_obj_get_style_pad_column(row, 0);
    int32_t cols = LV_MIN((int32_t)count, LV_MAX(1, (width + gap) / (min_width + gap)));
    int32_t lines = ((int32_t)count + cols - 1) / cols;
    cols = ((int32_t)count + lines - 1) / lines;
    int32_t card_width = (width - (cols - 1) * gap) / cols;
    for (unsigned i = 0; i < count; i++) {
        lv_obj_set_flex_grow(s_ui.kpi2[i]->box, 0);
        lv_obj_set_width(s_ui.kpi2[i]->box, card_width);
    }
    lv_obj_update_layout(row);
    int32_t height = 0;
    for (unsigned i = 0; i < count; i++) {
        lv_obj_t *box = s_ui.kpi2[i]->box;
        if (lv_obj_get_content_width(box) <= 0) return;
        int32_t need = lv_obj_get_style_pad_top(box, 0) + lv_obj_get_style_pad_bottom(box, 0) +
                       2 * lv_obj_get_style_border_width(box, 0);
        int n = 0;
        for (uint32_t j = 0; j < lv_obj_get_child_count(box); j++) {
            lv_obj_t *child = lv_obj_get_child(box, j);
            if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
            int32_t child_h = lv_obj_get_height(child);
            if (lv_obj_has_class(child, &lv_label_class)) {
                lv_point_t size;
                lv_text_get_size(&size, lv_label_get_text(child), lv_obj_get_style_text_font(child, 0),
                                 lv_obj_get_style_text_letter_space(child, 0), lv_obj_get_style_text_line_space(child, 0),
                                 lv_obj_get_content_width(box), LV_TEXT_FLAG_NONE);
                child_h = size.y;
            }
            need += child_h;
            n++;
        }
        if (n > 1) need += (n - 1) * lv_obj_get_style_pad_row(box, 0);
        if (need > height) height = need;
    }
    height += UK_S3;
    for (unsigned i = 0; i < count; i++) lv_obj_set_height(s_ui.kpi2[i]->box, height);
    int32_t rows = ((int32_t)count + cols - 1) / cols;
    int32_t row_height = rows * height + (rows - 1) * lv_obj_get_style_pad_row(row, 0);
    if (lv_obj_get_height(row) != row_height) lv_obj_set_height(row, row_height);
}

static void build_p3(lv_obj_t *page)
{
    /* 系统页 = 状态摘要 + 三行（容器服务 / 硬件温度 / 告警与事件）。
       诊断卡、配对卡、配网卡都盖在同一块内容区上（旧实现是"同槽位的三张整页卡"，
       靠后建的盖住先建的）；flex 里没有绝对定位的浮层，改成"显示覆盖层就收起正文"，
       见 p3_overlay_sync()。 */
    ui_page(page);

    /* 摘要卡不设卡头标题：状态行本身就是这一卡的标题（"容器 8/8 运行 · 24 路温度 ·
       0 个事件"），再加一个"系统摘要"就是用两个标题说同一件事。 */
    s_ui.p3_note = ui_card(page, NULL, NULL);
    uk_card_flex(s_ui.p3_note, 0);
    {
        bool compact = lv_display_get_vertical_resolution(lv_display_get_default()) < UK_SCR_H;
        if (compact) lv_obj_set_style_pad_all(s_ui.p3_note, UK_S2, 0);
        lv_obj_t *body = uk_card_body(s_ui.p3_note);
        s_ui.system_state = uk_label(body, UK_FONT_CJK_20, UK_INK, "等待数据");
        lv_obj_set_width(s_ui.system_state, LV_PCT(100));
        s_ui.system_note = uk_label(body, compact ? UK_FONT_CJK_12 : UK_FONT_CJK_16, UK_T2, "等待数据");
        lv_obj_set_width(s_ui.system_note, LV_PCT(100));
        s_ui.system_note2 = uk_label(body, UK_FONT_CJK_12, UK_T3, "");
        lv_obj_set_width(s_ui.system_note2, LV_PCT(100));
    }

    panel_peach(s_ui.p3_note);
    s_ui.p3_cols = uk_row_box(page, 1);
    lv_obj_set_style_pad_row(s_ui.p3_cols, UK_CARD_GAP, 0);
    lv_obj_set_style_pad_column(s_ui.p3_cols, UK_CARD_GAP, 0);
    lv_obj_set_flex_flow(s_ui.p3_cols, LV_FLEX_FLOW_ROW_WRAP);

    /* 容器服务：一行一个容器（名字左、状态右），不带进度条也不带状态点 ——
       运行中的绿灯已经在状态文字的颜色里了。 */
    s_ui.dock_card = ui_card(s_ui.p3_cols, "容器服务", NULL);
    uk_card_flex(s_ui.dock_card, 1);
    {
        lv_obj_t *body = uk_card_body(s_ui.dock_card);
        s_ui.dock_empty = empty_box(body, "等待容器采集", &s_ui.dock_empty_label);
        s_ui.dock_pool = uk_pool_create(body, UK_LIST_MIN_WIDTH);
        lv_obj_set_style_pad_row(s_ui.dock_pool, UK_ITEM_GAP, 0);
        uk_pool_stretch(s_ui.dock_pool, false);
        s_ui.dock_made = 0;
    }

    /* Device identity, hottest channel, and configured visual reference colors. */
    s_ui.p3_temp_card = ui_card(s_ui.p3_cols, "硬件温度", NULL);
    uk_card_flex(s_ui.p3_temp_card, 1);
    {
        lv_obj_t *body = uk_card_body(s_ui.p3_temp_card);
        s_ui.temp_empty = empty_box(body, "未采集到温度", NULL);
        s_ui.p3_temp_pool = uk_pool_create(body, UK_LIST_MIN_WIDTH);
        lv_obj_set_style_pad_row(s_ui.p3_temp_pool, UK_ITEM_GAP, 0);
        uk_pool_stretch(s_ui.p3_temp_pool, false);
        s_ui.p3_temp_made = 0;
    }

    /* 告警与事件：一条告警可能两三行（消息本身是整句中文），所以这里不用池，
       用可以自己滚的列（池要求条目等高等宽）。 */
    s_ui.alert_card = ui_card(page, "告警与事件", "查看全部 →");
    /* 不跟另外两张同排：三分之一宽会把一句中文折成四五行；而三张卡等高时，
       只有一两条告警的那张会空出大半张卡面，像没画完。改成整页宽、按内容定高、
       最高 UI_ALERT_PANEL_MAX_H —— 内容少时它只占一条，内容多时由卡内滚动接管。
       这里刻意不用 flex_grow：它一旦参与抢余量，就和上面的池卡等比瓜分，
       健康态下池卡只剩一半高（列表被挤成两三条）。余量留给 p3_cols。 */
    lv_obj_set_flex_grow(s_ui.alert_card, 0);
    lv_obj_set_height(s_ui.alert_card, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_ui.alert_card, UI_ALERT_PANEL_MAX_H, 0);
    /* 卡片本身就是入口：点它去告警页看全部。 */
    lv_obj_add_flag(s_ui.alert_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_ui.alert_card, uk_c(UK_SURF_S2), LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_ui.alert_card, page_jump_cb, LV_EVENT_CLICKED, (void *)(intptr_t)5);
    {
        /* 卡片按内容定高 ⇒ body 也必须按内容定高：uk_card 默认给 body 的是 flex_grow 1，
           在"按内容定高"的父容器里会被算成 0 高，整张卡塌成一条线（里面的字全跑到卡外，
           几何审计直接报 child out of parent）。 */
        lv_obj_t *body = uk_card_body(s_ui.alert_card);
        lv_obj_set_flex_grow(body, 0);
        lv_obj_set_height(body, LV_SIZE_CONTENT);
        s_ui.alert_none_box = empty_box(body, "等待数据", &s_ui.alert_none);
        /* 空态盒子同理：它本来是"吃满卡高、自己居中"，这张卡不按卡高算，得给它一个真实高度。 */
        lv_obj_set_flex_grow(s_ui.alert_none_box, 0);
        lv_obj_set_height(s_ui.alert_none_box, lv_font_get_line_height(UK_FONT_CJK_16));
        s_ui.alert_col = scroll_col(body);
        /* 告警列也按内容定高，但封顶在"卡片上限 - 卡头与内边距"：再多就自己滚。
           于是卡片高度 = 内容高度（最多 150）：既不会空着半张卡，也不会撑破页面。 */
        lv_obj_set_flex_grow(s_ui.alert_col, 0);
        lv_obj_set_height(s_ui.alert_col, LV_SIZE_CONTENT);
        /* 系统页只列 UI_ALERT_PANEL_ROWS 条：卡片按内容定高，多了在这里自己滚，
           完整的九条在告警页（点卡进）。 */
        lv_obj_set_style_max_height(s_ui.alert_col,
                                    UI_ALERT_PANEL_ROWS * lv_font_get_line_height(UK_FONT_CJK_16)
                                        + (UI_ALERT_PANEL_ROWS - 1) * UK_S3, 0);
    }

    lv_obj_t *status_cards[] = { s_ui.dock_card, s_ui.p3_temp_card, s_ui.alert_card };
    for (unsigned i = 0; i < sizeof status_cards / sizeof status_cards[0]; i++) {
        lv_obj_set_style_pad_all(status_cards[i], UK_S2, 0);
        lv_obj_set_style_pad_row(status_cards[i], UK_S1, 0);
    }

    /* 采集诊断：一张整页卡，键值两列（左列键名定宽，右列拿剩下的宽度）。
       组名 + 分隔线把十行切成三段（见 AGENT_GROUP），行距因此收紧一档 UK_S1。 */
    s_ui.diagnostics = ui_card(page, "设置与诊断", NULL);
    uk_card_flex(s_ui.diagnostics, 1);
    {
        lv_obj_t *body = uk_card_body(s_ui.diagnostics);
        lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(body, LV_DIR_VER);
        lv_obj_set_style_pad_row(body, UK_S1, 0);
        /* 这张卡同时是"设置"页：设备域的两个入口放在最上面（诊断行在下），
           顶栏那个按钮只负责开合，"设置"里的动作在这里。 */
        lv_obj_t *ent = uk_row_box(body, 0);
        lv_obj_set_style_pad_column(ent, UK_S2, 0);
        lv_obj_t *wifi_entry = flex_btn(ent, "Wi-Fi 设置", BTN_GHOST, UK_FONT_CJK_16, 0);
        lv_obj_add_event_cb(wifi_entry, wifi_btn_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *pair_entry = flex_btn(ent, "设备配对", BTN_GHOST, UK_FONT_CJK_16, 0);
        lv_obj_add_event_cb(pair_entry, pair_btn_cb, LV_EVENT_CLICKED, NULL);
        uk_hairline(body);
        lv_point_t key_size;
        lv_text_get_size(&key_size, "固件内存", UK_FONT_CJK_20, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        for (int g = 0; g < UI_AGENT_GROUPS; g++) {
            if (g > 0) uk_hairline(body);
            lv_obj_t *gt = uk_label(body, UK_FONT_CJK_12, UK_T4, AGENT_GROUP[g].title);
            lv_obj_set_width(gt, LV_PCT(100));
            lv_obj_set_height(gt, lv_font_get_line_height(UK_FONT_CJK_12));
            for (int i = AGENT_GROUP[g].first; i <= AGENT_GROUP[g].last; i++) {
                lv_obj_t *r = uk_row_box(body, 0);
                lv_obj_t *k = uk_label(r, UK_FONT_CJK_20, UK_T3, AGENT_KEY[i]);
                lv_obj_set_width(k, key_size.x + UK_S3);
                s_ui.agent_detail[i] = uk_label(r, UK_FONT_CJK_20, UK_T2, "-");
                lv_obj_set_flex_grow(s_ui.agent_detail[i], 1);
                lv_obj_set_style_min_width(s_ui.agent_detail[i], 0, 0);
            }
        }
    }
    lv_obj_add_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);

    pair_build(page);   /* 与诊断同槽位的整页卡，见 pair_show() */
    wifi_build(page);   /* 第三张：板上配网（出厂固件没有凭据时开机自动弹，见 ui_tick） */
}

/* ── P4: device summaries; every reported channel is available on demand. ── */
static void build_p4(lv_obj_t *page)
{
    /* v12：本页不再算坐标——摘要卡按内容自然高，通道卡吃掉剩余高度，
       通道的分列与行高由自适应池解（屏幕多大就用多少）。 */
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);   /* 页 = flex 列 */
    lv_obj_set_style_pad_all(page, 0, 0);             /* shell owns inset */
    lv_obj_set_style_pad_row(page, UK_CARD_GAP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *note = ui_card(page, NULL, NULL);
    uk_card_flex(note, 0);                       /* 摘要卡不抢空间：内容多高就多高 */
    lv_obj_t *nbody = uk_card_body(note);
    /* 摘要卡 = 一行「最热温度大字 + 结论」+ 一行参考区间。
       温度页要回答的第一个问题是"有没有一路在发烫"，所以把最热那一路的值做成大字，
       设备名与通道名退到它右边（CJK16，仍比正文大一档），计数与阈值放到第二行。 */
    lv_obj_t *srow = uk_row_box(nbody, 0);
    lv_obj_set_style_pad_column(srow, UK_S2, 0);
    lv_obj_set_flex_align(srow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_ui.temp_hero_dot = uk_dot(srow, UK_T3, UK_DOT);
    s_ui.temp_hero = uk_label(srow, UK_FONT_NUM_32, UK_T1, "--");
    s_ui.temp_hero_unit = uk_label(srow, UK_FONT_CJK_12, UK_T3, "°C");
    lv_obj_set_style_pad_bottom(s_ui.temp_hero_unit, UK_S1, 0);
    s_ui.temp_note = uk_label(srow, UK_FONT_CJK_16, UK_T2, "等待数据");
    lv_obj_set_flex_grow(s_ui.temp_note, 1);
    lv_obj_set_style_min_width(s_ui.temp_note, 0, 0);
    lv_label_set_long_mode(s_ui.temp_note, LV_LABEL_LONG_MODE_WRAP);
    s_ui.temp_note2 = uk_label(nbody, UK_FONT_CJK_12, UK_T3, "");
    lv_obj_set_width(s_ui.temp_note2, LV_PCT(100));

    s_ui.temp_card = ui_card(page, "设备温度", "点按设备查看通道");
    uk_card_flex(s_ui.temp_card, 1);             /* 卡片吃满余量 */
    lv_obj_t *body = uk_card_body(s_ui.temp_card);

    /* 空态：池外的一条提示（池里没有行时它就是卡片里唯一的内容） */
    s_ui.temp_empty_all = uk_label(body, UK_FONT_CJK_16, UK_T3, "未采集到温度通道");
    lv_obj_set_width(s_ui.temp_empty_all, LV_PCT(100));
    lv_obj_add_flag(s_ui.temp_empty_all, LV_OBJ_FLAG_HIDDEN);

    panel_peach(note);

    /* Column count follows available content width and configured preferred width. */
    s_ui.temp_pool = uk_pool_create(body, UK_LIST_MIN_WIDTH);
    lv_obj_set_style_pad_row(s_ui.temp_pool, UK_CARD_GAP, 0);
    /* 一块一台设备、块高参差：不要让池把余量平分进每块（那会在块与块之间留大片空白） */
    uk_pool_stretch(s_ui.temp_pool, false);
    s_ui.temp_blk_made = 0;

}

/* ── 配对（与 NAS 建立受信连接）────────────────────────────────────
   覆盖在系统页上的一张整页卡，和"采集诊断"同槽位、互斥显示。

   流程与 NAS 端约定的一致：用户在 NAS 管理页生成 6 位配对码 → 在板上输入 →
   板子用明文取回证书（只放行 identity 的那条引导通道）→ **板子自己**算 SHA-256
   并把指纹显示出来 → 用户拿它和 NAS 管理页上显示的指纹逐段比对 → 接受 → 配对。

   指纹一定是从证书字节现算的（fnos_pair 里算完才给到这里），绝不显示服务器
   JSON 里的 fingerprint 字段：否则中间人同时伪造证书和那个字段就能骗过肉眼。 */
enum { PST_CODE = 0, PST_BUSY, PST_CONFIRM, PST_DONE, PST_FAIL };

static void pair_pad_cb(lv_event_t *e);
static void pair_ok_cb(lv_event_t *e);
static void pair_cancel_cb(lv_event_t *e);
static void pair_forget_cb(lv_event_t *e);

static void pair_fp_line(const char *fp, int i, char *out, size_t cap)
{
    out[0] = 0;
    if (!fp) return;
    size_t len = strlen(fp);
    size_t off = (size_t)i * PAIR_FP_PER * 3;
    if (off >= len) return;
    size_t n = len - off;
    if (n > PAIR_FP_PER * 3 - 1) n = PAIR_FP_PER * 3 - 1;   /* 去掉行尾那个 ':' */
    if (n > cap - 1) n = cap - 1;
    memcpy(out, fp + off, n);
    out[n] = 0;
}

/* ── 按钮状态：五种用途各有一套"背景 + 文字 + 描边 + 按下反馈"────────────────
   为什么要成套：按钮是整屏唯一"能按"的东西，必须一眼看出**能不能按、按了会怎样**。
   - 主操作（BTN_PRIMARY）使用主题的主色及其配套前景色。
   - 次操作（BTN_SECONDARY）深底 + 25% 白描边：看得出边界，但明显不如主操作抢眼。
   - 危险操作（BTN_DANGER）不常驻红底——红底会把"取消/关闭"这种安全出口也染成警告；
     只在二次确认武装后才变红。
   - 每个按钮都响应 LV_EVENT_PRESSED/PRESS_LOST/RELEASED：不动的按钮在触摸屏上
     分不清"没按到"和"按了没反应"，这是"清晰"的一半。 */


/* 按钮文字：mk_btn 在按钮正中放一个宽高=按钮的标签，所以**改字号不影响居中**，
   但盒子尺寸是按建树时的字号算的 —— 换更宽的字号前先把盒子撑到按钮大小，
   否则运行时会静默裁字（中文尤其容易，15px 的三个字就要 45px）。 */
static void btn_label(lv_obj_t *b, const lv_font_t *f, uint32_t txt_rgb, const char *txt)
{
    if (!b) return;
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (!l) return;
    if (f) lv_obj_set_style_text_font(l, f, 0);
    if (txt_rgb) lv_obj_set_style_text_color(l, uk_c(txt_rgb), 0);
    if (txt) set_txt(l, "%s", txt);
}

/* ── 柔性按钮与内嵌面板（配对页 / 配网页共用） ─────────────────────────────
   ui_kit 没有通用按钮：卡片、清单行、KPI、趋势都不是"按下去做一件事"的东西，
   而这两张卡要十几个按钮 + 53 个键，就地做两个小件。

   **标签必须清掉 CLICKABLE**：LVGL 9 的对象默认可点，按钮里那张标签会把点击
   吞掉，事件永远到不了按钮自己（uk_label 里已经统一清掉）
   keep(l, LV_OBJ_FLAG_CLICKABLE) 同一招；preview 又是"找文本 → 点它的父对象"，
   漏了这步就是整屏按钮失灵（真机也一样）。 */
static void not_clickable(lv_obj_t *o)
{
    if (!o) return;                      /* uk_row_t 里没建的那个部件就是 NULL */
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) not_clickable(lv_obj_get_child(o, i));
}

static void btn_skin(lv_obj_t *b, int kind)
{
    /* 幽灵档（BTN_GHOST）：无底色、无描边，只有文字 —— 给"常驻入口"用。
       顶栏挂着一颗有底有边的按钮时，它比状态胶囊（在线/离线）还抢眼，
       而它其实只是"去配网"的意思。按下仍然给一块底色，反馈不丢。 */
    const bool ghost = (kind == BTN_GHOST);
    uint32_t bg = (kind == BTN_PRIMARY)   ? UK_BLUE
                : (kind == BTN_DANGER)    ? UK_DANGER
                : (kind == BTN_SECONDARY) ? UK_SURF_S2 : UK_SURF_S1;
    lv_obj_set_style_bg_color(b, uk_c(bg), 0);
    lv_obj_set_style_bg_opa(b, ghost ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, UK_RADIUS_SM, 0);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_border_width(b, ghost ? 0 : 1, 0);
    /* 主操作给一点白色高光边，屏幕暗处也立得住（沿用老 btn_style 的三档） */
    lv_obj_set_style_border_opa(b, ghost ? LV_OPA_TRANSP
                                : (kind == BTN_PRIMARY || kind == BTN_DANGER) ? 45
                                : (kind == BTN_SECONDARY) ? UK_EDGE_CTRL : 36, 0);
    lv_obj_set_style_bg_color(b, uk_c(kind == BTN_PRIMARY || kind == BTN_DANGER ? bg : UK_SURF_S3), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(b, 1, LV_STATE_PRESSED);
}

/* w > 0：定宽；w == 0：在行里平分宽度。 */
static lv_obj_t *flex_btn(lv_obj_t *parent, const char *txt, int kind,
                          const lv_font_t *font, int32_t w)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_height(b, UK_ROW_H);
    lv_obj_set_style_pad_all(b, UK_S2, 0);
    lv_obj_set_style_pad_column(b, UK_S1, 0);
    lv_obj_set_style_min_width(b, 0, 0);
    if (w > 0) lv_obj_set_width(b, w);
    else       lv_obj_set_flex_grow(b, 1);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    btn_skin(b, kind);
    uint32_t text = kind == BTN_PRIMARY ? UK_ON_PRIMARY : kind == BTN_DANGER ? UK_ON_DANGER
                   : (kind == BTN_NEUTRAL || kind == BTN_GHOST) ? UK_T2 : UK_T1;
    lv_obj_t *l = uk_label(b, font, text, txt);
    not_clickable(l);
    return b;
}

/* 深色内嵌面板：配对码块 / 键盘块 / 指纹块都用它包一层。 */
static lv_obj_t *panel_box(lv_obj_t *parent, int32_t grow)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_style_bg_color(p, uk_c(UK_S0), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, UK_RADIUS, 0);
    lv_obj_set_style_pad_all(p, UK_S3, 0);
    lv_obj_set_style_pad_row(p, UK_S2, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    if (grow > 0) lv_obj_set_flex_grow(p, (uint8_t)grow);
    else          lv_obj_set_height(p, LV_SIZE_CONTENT);
    return p;
}


/* 告警分级 → 告警页的三个分组（0 严重 / 1 警告 / 2 提示）。
   采集端可能给空串或没见过的级别，一律落进"提示"，不丢事件。 */
static int alert_group_of(const char *lv)
{
    if (lv && lv[0]) {
        if (lv[0] == 'c') return 0;
        if (lv[0] == 'w') return 1;
    }
    return 2;
}

/* ── P5: 告警页。日期时间采集端没给，所以不做时间线；按严重度分三组，
   每组一个组头（点 + 组名 + 条数）和一列消息。组内行高随消息长短变，
   用列而不是等高等宽的池。 ── */
static void build_p5(lv_obj_t *page)
{
    ui_page(page);

    /* 摘要：先回答"有几条、什么级别"，明细在下面。 */
    s_ui.alert_page_sum = ui_card(page, NULL, NULL);
    uk_card_flex(s_ui.alert_page_sum, 0);
    lv_obj_set_style_pad_all(s_ui.alert_page_sum, UK_S2, 0);
    {
        lv_obj_t *body = uk_card_body(s_ui.alert_page_sum);
        s_ui.alert_page_note = uk_label(body, UK_FONT_CJK_20, UK_INK, "等待数据");
        lv_obj_set_width(s_ui.alert_page_note, LV_PCT(100));
    }
    panel_peach(s_ui.alert_page_sum);

    lv_obj_t *card = ui_card(page, "告警与事件", NULL);
    uk_card_flex(card, 1);
    {
        lv_obj_t *body = uk_card_body(card);
        lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(body, LV_DIR_VER);
        lv_obj_set_style_pad_row(body, UK_S2, 0);
        static const char *const gname[] = { "严重", "警告", "提示" };
        const uint32_t gcol[] = { UK_DANGER, UK_WARN, UK_T2 };   /* 调色板是运行时变量，不能 static */
        for (int g = 0; g < 3; g++) {
            lv_obj_t *head = uk_row_box(body, 0);
            lv_obj_set_style_pad_column(head, UK_S2, 0);
            uk_dot(head, gcol[g], UK_DOT);
            s_ui.alert_page_head[g] = uk_label(head, UK_FONT_CJK_12, UK_T3, gname[g]);
            s_ui.alert_page_cap[g] = uk_label(head, UK_FONT_CJK_12, UK_T4, "0 条");
            lv_obj_set_flex_grow(s_ui.alert_page_cap[g], 1);
            lv_obj_set_style_text_align(s_ui.alert_page_cap[g], LV_TEXT_ALIGN_RIGHT, 0);
            s_ui.alert_page_col[g] = uk_col(body, 0);
            lv_obj_set_width(s_ui.alert_page_col[g], LV_PCT(100));
            lv_obj_set_style_pad_row(s_ui.alert_page_col[g], UK_S2, 0);
            s_ui.alert_page_made[g] = 0;
        }
        s_ui.alert_page_empty = empty_box(body, "暂无告警事件", NULL);

        /* 空态不能只报"没有"：告警页的健康态是**长期**状态，一整屏留白会被读成
           "这页还没做完"。下面四行把"看住了什么、现在什么状态"逐条摆出来，
           全部取自当前快照（与首页磁贴/系统页同源），不编历史、不编趋势。 */
        s_ui.alert_health = uk_col(body, 0);
        lv_obj_set_width(s_ui.alert_health, LV_PCT(100));
        lv_obj_set_style_pad_row(s_ui.alert_health, UK_S1, 0);
        lv_obj_add_flag(s_ui.alert_health, LV_OBJ_FLAG_HIDDEN);
        {
            static const char *const hname[] = { "存储容量", "温度", "容器", "采集" };
            uk_label(s_ui.alert_health, UK_FONT_CJK_12, UK_T4, "当前监控维度");
            for (int i = 0; i < 4; i++) {
                lv_obj_t *row = uk_row_box(s_ui.alert_health, 0);
                lv_obj_set_style_pad_column(row, UK_S2, 0);
                s_ui.alert_health_dot[i] = uk_dot(row, UK_OK, UK_DOT);
                lv_obj_t *nm = uk_label(row, UK_FONT_CJK_12, UK_T4, hname[i]);
                lv_obj_set_width(nm, 68);
                s_ui.alert_health_val[i] = uk_label(row, UK_FONT_CJK_12, UK_T2, "-");
                lv_obj_set_flex_grow(s_ui.alert_health_val[i], 1);
                lv_obj_set_style_min_width(s_ui.alert_health_val[i], 0, 0);
                lv_label_set_long_mode(s_ui.alert_health_val[i], LV_LABEL_LONG_MODE_DOTS);
            }
        }
    }
}

static void pair_build(lv_obj_t *page)
{
    /* Peer cards share the page rhythm. Only the card grid scrolls; actions stay reachable. */
    lv_obj_t *root = uk_col(page, 1);
    s_ui.pair_card = root;
    lv_obj_set_width(root, LV_PCT(100));
    lv_obj_set_height(root, 0);
    lv_obj_set_style_pad_row(root, UK_CARD_GAP, 0);
    lv_obj_t *status = ui_card(root, "与 NAS 配对", NULL);
    uk_card_flex(status, 0);
    s_ui.pair_title = uk_card_title(status);
    lv_obj_t *body = uk_card_body(status);
    s_ui.pair_hint = uk_label(body, UK_FONT_CJK_12, UK_T3, "");
    lv_obj_set_width(s_ui.pair_hint, LV_PCT(100));
    lv_label_set_long_mode(s_ui.pair_hint, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *grid = uk_row_box(root, 1);
    s_ui.pair_grid = grid;
    lv_obj_set_height(grid, 0);
    lv_obj_set_style_min_height(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, UK_CARD_GAP, 0);
    lv_obj_set_style_pad_column(grid, UK_CARD_GAP, 0);
    lv_obj_set_style_pad_right(grid, UK_S3, 0);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

    s_ui.pair_steps = ui_card(grid, "三步完成配对", NULL);
    uk_card_flex(s_ui.pair_steps, 0);
    body = uk_card_body(s_ui.pair_steps);
    for (int i = 0; i < PAIR_STEPS; i++) {
        lv_obj_t *r = uk_row_box(body, 0);
        lv_obj_set_width(r, LV_PCT(100));
        lv_obj_t *badge = lv_obj_create(r);
        int32_t badge_size = lv_font_get_line_height(UK_FONT_NUM_20) + UK_S2;
        lv_obj_set_size(badge, badge_size, badge_size);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(badge, uk_c(UK_SURF_S3), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        char no[4];
        snprintf(no, sizeof no, "%d", i + 1);
        s_ui.pair_step_no[i] = badge;
        (void)uk_label(badge, UK_FONT_NUM_20, UK_T1, no);
        s_ui.pair_step_txt[i] = uk_label(r, UK_FONT_CJK_12, UK_T2, "");
        lv_label_set_long_mode(s_ui.pair_step_txt[i], LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_flex_grow(s_ui.pair_step_txt[i], 1);
        lv_obj_set_style_min_width(s_ui.pair_step_txt[i], 0, 0);
    }

    s_ui.pair_msg_panel = ui_card(grid, "连接状态", NULL);
    uk_card_flex(s_ui.pair_msg_panel, 0);
    s_ui.pair_msg = uk_label(uk_card_body(s_ui.pair_msg_panel), UK_FONT_CJK_16, UK_T2, "");
    lv_label_set_long_mode(s_ui.pair_msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_ui.pair_msg, LV_PCT(100));
    lv_obj_set_height(s_ui.pair_msg, LV_SIZE_CONTENT);

    s_ui.pair_codecol = ui_card(grid, "配对码", NULL);
    uk_card_flex(s_ui.pair_codecol, 0);
    s_ui.pair_code_panel = s_ui.pair_codecol;
    s_ui.pair_code_lbl = uk_card_title(s_ui.pair_codecol);
    body = uk_card_body(s_ui.pair_codecol);
    lv_obj_t *slots = uk_row_box(body, 0);
    lv_obj_set_width(slots, LV_PCT(100));
    for (int i = 0; i < PAIR_CODE_LEN; i++) {
        s_ui.pair_slot[i] = uk_label(slots, UK_FONT_NUM_32, UK_T4, "-");
        lv_obj_set_flex_grow(s_ui.pair_slot[i], 1);
        lv_obj_set_style_text_align(s_ui.pair_slot[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    s_ui.pair_code_sub = uk_label(body, UK_FONT_CJK_12, UK_T3, "");
    lv_obj_set_width(s_ui.pair_code_sub, LV_PCT(100));
    lv_label_set_long_mode(s_ui.pair_code_sub, LV_LABEL_LONG_MODE_WRAP);

    /* 键盘 4 列 × 3 行：3 列 × 4 行要 4×44 + 3×8 = 200px，配对码整卡就会被 grid 视口
       截掉，"确认"只露半个（底栏正好压在上面）。4 列后只要 3 行 ≈ 140px，整卡进得去。
       键位按列排：前三列仍是手机键盘的 1-4-7 / 2-5-8 / 3-6-9，第四列是三个功能键
       （删除 / 0 / 确认）—— 确认落在右下角，正是右手拇指的落点。 */
    s_ui.pair_pad = uk_col(body, 0);
    lv_obj_set_width(s_ui.pair_pad, LV_PCT(100));
    lv_obj_set_style_pad_row(s_ui.pair_pad, UK_S1, 0);
    static const struct { const char *t; int k; } PAD[PAIR_PAD_KEYS] = {
        { "1", 1 }, { "2", 2 }, { "3", 3 }, { "删除", 10 },
        { "4", 4 }, { "5", 5 }, { "6", 6 }, { "0", 0 },
        { "7", 7 }, { "8", 8 }, { "9", 9 }, { "确认", 11 },
    };
    lv_obj_t *prow = NULL;
    for (int i = 0; i < PAIR_PAD_KEYS; i++) {
        if (i % PAIR_PAD_COLS == 0) {
            prow = uk_row_box(s_ui.pair_pad, 0);
            lv_obj_set_width(prow, LV_PCT(100));
            lv_obj_set_style_min_height(prow, UK_ROW_H, 0);
            lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        }
        const bool wide = (PAD[i].k == 10 || PAD[i].k == 11);   /* 删除 / 确认 */
        const bool ok   = (PAD[i].k == 11);
        s_ui.pair_key[i] = flex_btn(prow, PAD[i].t, ok ? BTN_PRIMARY : BTN_NEUTRAL,
                                    wide ? UK_FONT_CJK_16 : UK_FONT_NUM_20, 0);
        btn_label(s_ui.pair_key[i], NULL, ok ? UK_ON_PRIMARY : UK_T1, NULL);
        lv_obj_add_event_cb(s_ui.pair_key[i], pair_pad_cb, LV_EVENT_CLICKED, (void *)(intptr_t)PAD[i].k);
    }

    s_ui.pair_info_panel = ui_card(grid, "核对证书指纹", NULL);
    uk_card_flex(s_ui.pair_info_panel, 0);
    s_ui.pair_info_lbl = uk_card_title(s_ui.pair_info_panel);
    body = uk_card_body(s_ui.pair_info_panel);
    for (int i = 0; i < PAIR_FP_LINES; i++) {
        s_ui.pair_fp[i] = uk_label(body, UK_FONT_NUM_32, UK_T1, "");
        lv_obj_set_width(s_ui.pair_fp[i], LV_SIZE_CONTENT);
    }
    s_ui.pair_meta = uk_label(body, UK_FONT_CJK_12, UK_T3, "");
    lv_obj_set_width(s_ui.pair_meta, LV_PCT(100));
    lv_label_set_long_mode(s_ui.pair_meta, LV_LABEL_LONG_MODE_WRAP);

    /* ── 底部按钮条 ── */
    lv_obj_t *foot = uk_row_box(root, 0);
    s_ui.pair_foot = foot;
    lv_obj_set_width(foot, LV_PCT(100));
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(foot, UK_S2, 0);
    s_ui.pair_cancel = flex_btn(foot, "关闭", BTN_SECONDARY, UK_FONT_CJK_16, 0);
    s_ui.pair_forget = flex_btn(foot, "解除配对", BTN_NEUTRAL, UK_FONT_CJK_16, 0);
    s_ui.pair_ok     = flex_btn(foot, "确认", BTN_PRIMARY, UK_FONT_CJK_16, 0);
    lv_obj_t *buttons[] = { s_ui.pair_cancel, s_ui.pair_forget, s_ui.pair_ok };
    for (unsigned i = 0; i < sizeof buttons / sizeof buttons[0]; i++) {
        lv_obj_set_flex_grow(buttons[i], 0);
        lv_obj_set_width(buttons[i], LV_SIZE_CONTENT);
        lv_obj_set_style_min_width(buttons[i], UK_ROW_H, 0);
    }
    lv_obj_add_event_cb(s_ui.pair_ok,     pair_ok_cb,      LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.pair_cancel, pair_cancel_cb,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.pair_forget, pair_forget_cb,  LV_EVENT_CLICKED, NULL);

    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}


static void pair_show(bool on)
{
    if (on) fnos_ui_motion_settle();
    s_pair_open = on;
    if (on) s_pair_forget_armed = false;
    if (!s_ui.pair_card) return;
    if (on) { lv_obj_remove_flag(s_ui.pair_card, LV_OBJ_FLAG_HIDDEN);
              lv_obj_scroll_to_y(s_ui.pair_grid, 0, LV_ANIM_OFF); }
    else    lv_obj_add_flag(s_ui.pair_card, LV_OBJ_FLAG_HIDDEN);
    theme_apply();
    if (on && s_diagnostics) {
        s_diagnostics = false;
        if (s_ui.diagnostics) lv_obj_add_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);
        set_txt(s_ui.diagnostics_label, "%s", "设置");
    }
    p3_overlay_sync();
}

static void pair_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_pair_open && s_page == 3) return;
    fnos_ui_set_page(3);
    pair_show(true);
    /* 立刻按当前阶段摆一次，否则最长 500ms 里会先闪一下"键盘+指纹"混在一起的样子 */
    pair_refresh();
}

/* ── 板上配网卡 ──────────────────────────────────────────────────────────
   用户的原话："固件刷好之后，也需要提示用户接入内网 WiFi。" 出厂固件没有凭据，
   所以第一次开机要做的不是看数据，而是让用户在现场把 SSID 与口令填进去。

   与配对卡同槽位（整页覆盖、互斥显示），三个阶段、一屏一个问题：
     ① 扫描   附近有哪些网络（信号从强到弱），点一条继续；也能手动输入隐藏网络；
     ② 输密码 上半屏是选中的 SSID 与口令（可显示/隐藏），下半屏是键盘；
     ③ 连接   把 fnos_net_state_str() 的进展放大显示，成功或失败都能重来。

   两条必须守的约束：
   * esp_wifi_* 在本板是到 C6 的**同步 RPC**：扫描只能"请求"，由网络任务发起
     （见 fnos_net.h），界面这边只读缓存。
   * 键盘一次建 41 个键，字母层与数字符号层**复用同一批按钮**，只换文案与键值 ——
     不然光键盘就多出 40 个构件，还要多一份几何审计。
   WIFI_AP_ROWS / WIFI_KB_KEYS / WIFI_PW_MAX / WIFI_SSID_MAX / WIFI_ST_* 定义在
   文件上方 —— s_ui 要按它们开数组，所以不能挪到这里。 */

/* 键的行为：字符 / 大小写 / 退格 / 换层 / 空格 / 连接 */
enum { WK_CH = 0, WK_SHIFT, WK_BACK, WK_LAYER, WK_SPACE, WK_OK };
typedef struct { const char *txt; uint8_t kind; char ch; } wifi_key_t;
#define K_CH(c, t) { t, WK_CH, c }

static const wifi_key_t WIFI_KB_LETTERS[WIFI_KB_KEYS] = {
    K_CH('1', "1"), K_CH('2', "2"), K_CH('3', "3"), K_CH('4', "4"), K_CH('5', "5"),
    K_CH('6', "6"), K_CH('7', "7"), K_CH('8', "8"), K_CH('9', "9"), K_CH('0', "0"),
    K_CH('q', "q"), K_CH('w', "w"), K_CH('e', "e"), K_CH('r', "r"), K_CH('t', "t"),
    K_CH('y', "y"), K_CH('u', "u"), K_CH('i', "i"), K_CH('o', "o"), K_CH('p', "p"),
    K_CH('a', "a"), K_CH('s', "s"), K_CH('d', "d"), K_CH('f', "f"), K_CH('g', "g"),
    K_CH('h', "h"), K_CH('j', "j"), K_CH('k', "k"), K_CH('l', "l"),
    { "大写", WK_SHIFT, 0 },
    K_CH('z', "z"), K_CH('x', "x"), K_CH('c', "c"), K_CH('v', "v"), K_CH('b', "b"),
    K_CH('n', "n"), K_CH('m', "m"),
    { "退格", WK_BACK, 0 },
    { "123", WK_LAYER, 0 }, { "空格", WK_SPACE, 0 }, { "连接", WK_OK, 0 },
};
static const wifi_key_t WIFI_KB_SYMBOLS[WIFI_KB_KEYS] = {
    K_CH('1', "1"), K_CH('2', "2"), K_CH('3', "3"), K_CH('4', "4"), K_CH('5', "5"),
    K_CH('6', "6"), K_CH('7', "7"), K_CH('8', "8"), K_CH('9', "9"), K_CH('0', "0"),
    K_CH('!', "!"), K_CH('@', "@"), K_CH('#', "#"), K_CH('$', "$"), K_CH('%', "%"),
    K_CH('^', "^"), K_CH('&', "&"), K_CH('*', "*"), K_CH('(', "("), K_CH(')', ")"),
    K_CH('-', "-"), K_CH('_', "_"), K_CH('=', "="), K_CH('+', "+"), K_CH('[', "["),
    K_CH(']', "]"), K_CH('{', "{"), K_CH('}', "}"), K_CH('\\', "\\"),
    { "abc", WK_LAYER, 0 },
    K_CH(';', ";"), K_CH(':', ":"), K_CH('\'', "'"), K_CH('"', "\""), K_CH(',', ","),
    K_CH('.', "."), K_CH('?', "?"),
    { "退格", WK_BACK, 0 },
    { "ABC", WK_LAYER, 0 }, { "空格", WK_SPACE, 0 }, { "连接", WK_OK, 0 },
};

/* 校验失败的说明要"粘"住：刷新每 500ms 跑一次，如果只 set_txt 一次，用户还没看清
   就被下一拍的阶段提示盖掉（预览就是靠这条断言抓出来的）。任何一次按键/换页都清掉它。 */
static char s_wifi_err[120];
static int  s_wifi_stage = WIFI_ST_SCAN;
static bool s_wifi_dismissed;            /* 本次开机用户主动关过：不再自动弹 */
static bool s_wifi_manual;               /* 手动输入模式（隐藏网络）：SSID 也可编辑 */
static int  s_wifi_field;                /* 手动模式下正在编辑哪个：0=SSID 1=口令 */
static char s_wifi_ssid[WIFI_SSID_MAX + 1];
static char s_wifi_pw[WIFI_PW_MAX + 1];
static int  s_wifi_pw_n;
static bool s_wifi_reveal;               /* 口令是否明文显示 */
static int  s_wifi_layer;                /* 键盘层 0=字母 1=数字符号 */
static bool s_wifi_shift;
static int  s_wifi_sel = -1;             /* 选中的 AP 下标（-1 = 手动输入） */
static lv_obj_t *s_wifi_key_lbl[WIFI_KB_KEYS];

static void wifi_refresh(void);          /* 前置声明：回调里要用 */
static void field_focus(lv_obj_t *box, bool on);   /* 输入框高亮（定义在 wifi_set_stage 前） */
static void wifi_ap_cb(lv_event_t *e);
static void wifi_key_cb(lv_event_t *e);
static void wifi_rescan_cb(lv_event_t *e);
static void wifi_manual_cb(lv_event_t *e);
static void wifi_close_cb(lv_event_t *e);
static void wifi_field_cb(lv_event_t *e);
static void wifi_eye_cb(lv_event_t *e);
static void wifi_back_cb(lv_event_t *e);
static void wifi_connect_cb(lv_event_t *e);
static void wifi_again_cb(lv_event_t *e);
static void wifi_result_cb(lv_event_t *e);


/* 信号强度：用"格数 + dBm"两种说法（颜色之外还有文字，见版面合同第 9 条） */
static int wifi_bars(int8_t rssi)
{
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

static void wifi_apply_layer(void)
{
    const wifi_key_t *tab = s_wifi_layer ? WIFI_KB_SYMBOLS : WIFI_KB_LETTERS;
    for (int i = 0; i < WIFI_KB_KEYS; i++) {
        if (!s_wifi_key_lbl[i]) continue;
        const char *t = tab[i].txt;
        char up[2] = { 0, 0 };
        if (tab[i].kind == WK_CH && s_wifi_shift && !s_wifi_layer &&
            tab[i].ch >= 'a' && tab[i].ch <= 'z') {
            up[0] = (char)(tab[i].ch - 'a' + 'A');
            t = up;
        }
        lv_label_set_text(s_wifi_key_lbl[i], t);
        /* 换层只换文案与键值，样式一起跟着走：连接键永远是主色，退格/换层是弱色 */
        int kind = tab[i].kind;
        lv_obj_t *btn = lv_obj_get_parent(s_wifi_key_lbl[i]);
        if (kind == WK_OK)       { btn_skin(btn, BTN_PRIMARY);   btn_label(btn, NULL, UK_ON_PRIMARY, NULL); }
        else if (kind == WK_SHIFT && s_wifi_shift && !s_wifi_layer)
                                 { btn_skin(btn, BTN_PRIMARY);   btn_label(btn, NULL, UK_ON_PRIMARY, NULL); }
        else if (kind == WK_CH)  { btn_skin(btn, BTN_NEUTRAL);   btn_label(btn, NULL, UK_T1, NULL); }
        else                     { btn_skin(btn, BTN_SECONDARY); btn_label(btn, NULL, UK_T2, NULL); }
    }
    /* 大写键的文案说"按下去会得到什么"（现在是"大写"= 按了就变大写）。
       数字符号层那一位是"回字母层"，文案由键表决定，别在这里覆盖掉。 */
    if (s_ui.wifi_shift_btn && !s_wifi_layer) {
        set_txt(s_ui.wifi_shift_lbl, "%s", s_wifi_shift ? "小写" : "大写");
    }
}

static void wifi_render_input(void)
{
    /* 手动模式两行都能编辑；扫描模式只编辑口令。正在编辑的那一行描边高亮。 */
    bool manual = s_wifi_manual;
    if (s_ui.wifi_ssid_lbl) {
        set_txt(s_ui.wifi_ssid_lbl, "%s", s_wifi_ssid[0] ? s_wifi_ssid : "（点这里输入网络名）");
        lv_obj_set_style_text_color(s_ui.wifi_ssid_lbl,
                                    uk_c(s_wifi_ssid[0] ? UK_T1 : UK_T3), 0);
        field_focus(s_ui.wifi_ssid_box, manual && s_wifi_field == 0);
    }
    if (s_ui.wifi_pw_lbl) {
        if (s_wifi_reveal || s_wifi_pw_n == 0) {
            set_txt(s_ui.wifi_pw_lbl, "%s", s_wifi_pw_n ? s_wifi_pw : "口令");
        } else {
            char mask[WIFI_PW_MAX + 1];
            int n = s_wifi_pw_n > WIFI_PW_MAX ? WIFI_PW_MAX : s_wifi_pw_n;
            for (int i = 0; i < n; i++) mask[i] = '*';
            mask[n] = 0;
            set_txt(s_ui.wifi_pw_lbl, "%s", mask);
        }
        lv_obj_set_style_text_color(s_ui.wifi_pw_lbl,
                                    uk_c(s_wifi_pw_n ? UK_T1 : UK_T3), 0);
        field_focus(s_ui.wifi_pw_box, !manual || s_wifi_field == 1);
    }
    if (s_ui.wifi_eye_lbl) set_txt(s_ui.wifi_eye_lbl, "%s", s_wifi_reveal ? "隐藏" : "显示");
}

static lv_obj_t *wifi_field_box(lv_obj_t *parent, int32_t grow, lv_obj_t **out_lbl)
{
    lv_obj_t *b = lv_obj_create(parent);
    if (grow > 0) { lv_obj_set_flex_grow(b, (uint8_t)grow); lv_obj_set_width(b, 120); }
    else          { lv_obj_set_width(b, LV_PCT(100)); }
    lv_obj_set_height(b, 36);
    lv_obj_set_style_bg_color(b, uk_c(UK_S1), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, UK_RADIUS_SM, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, uk_c(UK_LINE), 0);
    lv_obj_set_style_border_opa(b, UK_EDGE_CTRL, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_pad_left(b, UK_S3, 0);
    lv_obj_set_style_pad_right(b, UK_S3, 0);
    lv_obj_set_style_pad_column(b, UK_S2, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    *out_lbl = uk_label(b, UK_FONT_CJK_16, UK_T1, NULL);
    if (*out_lbl) {
        lv_obj_set_flex_grow(*out_lbl, 1);
        lv_obj_set_style_min_width(*out_lbl, 0, 0);
        not_clickable(*out_lbl);           /* 点的是输入框，不是框里的字 */
    }
    return b;
}

/* 正在编辑的那一行描蓝边：整块是白边的时候看不出"键盘往哪儿打字" */
static void field_focus(lv_obj_t *box, bool on)
{
    lv_obj_set_style_border_color(box, uk_c(on ? UK_BLUE : UK_LINE), 0);
    lv_obj_set_style_border_opa(box, on ? LV_OPA_COVER : UK_EDGE_CTRL, 0);
}

static void wifi_set_stage(int st)
{
    s_wifi_stage = st;
    s_wifi_err[0] = 0;
    bool scan = (st == WIFI_ST_SCAN), pass = (st == WIFI_ST_PASS), link = (st == WIFI_ST_LINK);
    /* 三个阶段各自成组：整组收起，别让隐藏的一栏还占着 flex 的高度 */
    show(s_ui.wifi_ap_pool,     scan);
    show(s_ui.wifi_list_hint,   scan);
    show(s_ui.wifi_rescan,      scan);
    show(s_ui.wifi_manual,      scan);
    show(s_ui.wifi_input_panel, pass);
    show(s_ui.wifi_kb,          pass);
    show(s_ui.wifi_back,        pass);
    show(s_ui.wifi_connect,     pass);
    show(s_ui.wifi_link_panel,  link);
    show(s_ui.wifi_again,       link);
    show(s_ui.wifi_done,        link);
    /* 「关闭」常驻，不参与切换 */
    if (pass) { wifi_apply_layer(); wifi_render_input(); }
    if (link && s_ui.wifi_link_sub) set_txt(s_ui.wifi_link_sub, "%s", "");
}

static void wifi_build(lv_obj_t *page)
{
    lv_obj_t *c = ui_card(page, "接入 Wi-Fi", NULL);
    uk_card_flex(c, 1);
    s_ui.wifi_card  = c;
    s_ui.wifi_title = uk_card_title(c);
    lv_obj_t *body = uk_card_body(c);
    lv_obj_set_style_pad_row(body, UK_S1, 0);   /* 这张卡的正文行距紧一档，省给键盘 */
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    s_ui.wifi_hint = uk_label(body, UK_FONT_CJK_12, UK_T3, NULL);
    lv_obj_set_width(s_ui.wifi_hint, LV_PCT(100));

    /* ── ① 扫描：网络行（整行可点） + 一句说明 + 底部三键 ─────────────── */
    s_ui.wifi_ap_pool = uk_pool_create(body, 860);     /* 一列：SSID 不做缩写 */
    s_ui.wifi_ap_made = 0;
    for (int i = 0; i < WIFI_AP_ROWS; i++) s_ui.wifi_ap_row[i] = NULL;

    s_ui.wifi_list_hint = uk_label(body, UK_FONT_CJK_12, UK_T3, "正在扫描…");
    lv_obj_set_width(s_ui.wifi_list_hint, LV_PCT(100));

    /* ── ② 输密码：两个输入框 + 41 键键盘 + 返回/连接 ─────────────────── */
    s_ui.wifi_input_panel = panel_box(body, 0);
    lv_obj_set_style_pad_all(s_ui.wifi_input_panel, UK_S1, 0);   /* 两个 36 的框 + 一条缝 = 88 */
    s_ui.wifi_ssid_box = wifi_field_box(s_ui.wifi_input_panel, 0, &s_ui.wifi_ssid_lbl);
    lv_obj_t *prow = uk_row_box(s_ui.wifi_input_panel, 0);
    s_ui.wifi_pw_box = wifi_field_box(prow, 1, &s_ui.wifi_pw_lbl);
    s_ui.wifi_eye = flex_btn(prow, "显示", BTN_SECONDARY, UK_FONT_CJK_16, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(s_ui.wifi_eye, UK_ROW_H, 0);
    s_ui.wifi_eye_lbl = lv_obj_get_child(s_ui.wifi_eye, 0);
    lv_obj_add_event_cb(s_ui.wifi_ssid_box, wifi_field_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    lv_obj_add_event_cb(s_ui.wifi_pw_box,   wifi_field_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(s_ui.wifi_eye,      wifi_eye_cb,   LV_EVENT_CLICKED, NULL);

    /* 键盘：五行，行内等分；键距按老版面的观感给（10 键行窄、9 键行宽、底排按 22:38:24 分） */
    s_ui.wifi_kb = panel_box(body, 1);
    /* 键盘要装得下五行 44 的键：面板内边距收紧到 8/4，自然高 252（老版面是 250）。
       不改的话自然高 276 > 中间剩给它的空间，最后一行会被挤出面板底边。 */
    lv_obj_set_style_pad_all(s_ui.wifi_kb, UK_S2, 0);
    lv_obj_set_style_pad_row(s_ui.wifi_kb, UK_S1, 0);
    lv_obj_t *krow[5];
    const int32_t key_rows = sizeof krow / sizeof krow[0];
    lv_obj_set_style_min_height(s_ui.wifi_kb,
                               key_rows * UK_ROW_H + (key_rows - 1) * UK_S1 + 2 * UK_S2, 0);
    for (int r = 0; r < 5; r++) {
        krow[r] = uk_row_box(s_ui.wifi_kb, 0);
        lv_obj_set_style_pad_column(krow[r], (r < 2) ? UK_S1 : (r < 4) ? UK_S4 : UK_S2, 0);
        /* 五行平分键盘面板的高度：键高写死会把最后一行挤到面板外（底栏吃掉 42px 之后） */
        lv_obj_set_flex_grow(krow[r], 1);
        lv_obj_set_style_min_height(krow[r], UK_ROW_H, 0);
    }
    for (int i = 0; i < WIFI_KB_KEYS; i++) {
        int row = (i < 10) ? 0 : (i < 20) ? 1 : (i < 29) ? 2 : (i < 38) ? 3 : 4;
        const wifi_key_t *tab = WIFI_KB_LETTERS;
        lv_obj_t *b = flex_btn(krow[row], tab[i].txt, BTN_NEUTRAL,
                               tab[i].kind == WK_CH ? UK_FONT_CJK_16 : UK_FONT_CJK_12, 0);
        if (row == 4) {                    /* 换层 / 空格 / 连接 */
            int col = i - 38;
            lv_obj_set_flex_grow(b, (uint8_t)(col == 0 ? 22 : col == 1 ? 38 : 24));
        }
        lv_obj_set_height(b, LV_PCT(100));            /* 高度由行给，行高由 flex 给 */
        lv_obj_set_style_min_height(b, UK_ROW_H, 0);
        s_ui.wifi_key[i] = b;
        s_wifi_key_lbl[i] = lv_obj_get_child(b, 0);
        lv_obj_add_event_cb(b, wifi_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    s_ui.wifi_shift_btn = s_ui.wifi_key[29];
    s_ui.wifi_shift_lbl = s_wifi_key_lbl[29];

    /* ── ③ 连接中/结果：一句话说清现在到哪一步了 ─────────────────────── */
    s_ui.wifi_link_panel = panel_box(body, 1);
    s_ui.wifi_link_big = uk_label(s_ui.wifi_link_panel, UK_FONT_CJK_20, UK_T1, "正在连接…");
    lv_obj_set_width(s_ui.wifi_link_big, LV_PCT(100));
    s_ui.wifi_link_sub = uk_label(s_ui.wifi_link_panel, UK_FONT_CJK_16, UK_T3, NULL);
    lv_obj_set_width(s_ui.wifi_link_sub, LV_PCT(100));
    lv_label_set_long_mode(s_ui.wifi_link_sub, LV_LABEL_LONG_MODE_WRAP);

    /* ── 底排：三个阶段的键都建在这一行里，按阶段显隐 ─────────────────────
       页脚与滚动正文同级，短屏也能直接关闭或连接；「关闭」三个阶段都在，
       所以它不参与 set_stage 的显隐。 */
    s_ui.wifi_foot = uk_row_box(c, 0);
    lv_obj_set_flex_flow(s_ui.wifi_foot, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_ui.wifi_foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_ui.wifi_foot, UK_S2, 0);
    s_ui.wifi_rescan = flex_btn(s_ui.wifi_foot, "重新扫描", BTN_SECONDARY, UK_FONT_CJK_16, LV_SIZE_CONTENT);
    s_ui.wifi_manual = flex_btn(s_ui.wifi_foot, "手动输入", BTN_NEUTRAL,   UK_FONT_CJK_16, LV_SIZE_CONTENT);
    s_ui.wifi_back   = flex_btn(s_ui.wifi_foot, "返回",     BTN_NEUTRAL,   UK_FONT_CJK_16, LV_SIZE_CONTENT);
    s_ui.wifi_again  = flex_btn(s_ui.wifi_foot, "换一个网络", BTN_NEUTRAL, UK_FONT_CJK_16, LV_SIZE_CONTENT);
    s_ui.wifi_close   = flex_btn(s_ui.wifi_foot, "关闭", BTN_NEUTRAL, UK_FONT_CJK_16, LV_SIZE_CONTENT);
    s_ui.wifi_connect = flex_btn(s_ui.wifi_foot, "连接", BTN_PRIMARY, UK_FONT_CJK_16, LV_SIZE_CONTENT);
    s_ui.wifi_done    = flex_btn(s_ui.wifi_foot, "完成", BTN_PRIMARY, UK_FONT_CJK_16, LV_SIZE_CONTENT);
    for (uint32_t i = 0; i < lv_obj_get_child_count(s_ui.wifi_foot); i++)
        lv_obj_set_style_min_width(lv_obj_get_child(s_ui.wifi_foot, i), UK_ROW_H, 0);
    lv_obj_add_event_cb(s_ui.wifi_rescan,  wifi_rescan_cb,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_manual,  wifi_manual_cb,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_back,    wifi_back_cb,    LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_again,   wifi_again_cb,   LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_close,   wifi_close_cb,   LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_connect, wifi_connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_done,    wifi_result_cb,   LV_EVENT_CLICKED, NULL);


    wifi_set_stage(WIFI_ST_SCAN);

    /* 建完先收起来：这张卡是**覆盖层**，不是系统页的常驻内容。
       它以前建出来就是可见的，而"有凭据"时开机规则压根不跑（没人调 wifi_show），
       于是实机上系统页被配网卡永远盖着——只有 s_wifi_open 与"卡是否可见"一致，
       set_page() 里那句 `if (s_wifi_open) wifi_show(false)` 才收得掉它。
       主机预览没抓到这个：预览的 fixture 恰好都是"没凭据"，那条路径会先把卡
       打开（s_wifi_open=true），随后 set_page 就把它收掉了——正好绕开这个状态。 */
    if (s_ui.wifi_card) lv_obj_add_flag(s_ui.wifi_card, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_show(bool on)
{
    if (on) fnos_ui_motion_settle();
    s_wifi_open = on;
    if (!s_ui.wifi_card) return;
    if (on) {
        s_wifi_dismissed = false;
        lv_obj_remove_flag(s_ui.wifi_card, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.wifi_card, LV_OBJ_FLAG_HIDDEN);
    }
    /* 同槽位互斥：配网卡开了就把配对卡与诊断面板收起来，否则三层会叠在一起 */
    if (on && s_pair_open) pair_show(false);
    if (on && s_diagnostics) {
        s_diagnostics = false;
        if (s_ui.diagnostics) lv_obj_add_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);
        set_txt(s_ui.diagnostics_label, "%s", "设置");
    }
    theme_apply();
    p3_overlay_sync();
}

static void wifi_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_wifi_open) { wifi_show(false); return; }
    fnos_ui_set_page(3);
    s_wifi_manual = false;
    s_wifi_sel = -1;
    wifi_set_stage(WIFI_ST_SCAN);
    fnos_net_scan_request();
    wifi_show(true);
    wifi_refresh();
}

/* ── 刷新：把"网络层的事实"翻到卡上（每 500ms 由 ui_tick 调一次） ─────────
   这张卡的每一句话都必须来自 fnos_net_* 的实际状态，不能靠界面自己记：
   连接可能在任何时刻成功或失败，界面只负责如实显示。 */

static const char *wifi_stage_hint(void)
{
    if (s_wifi_err[0]) return s_wifi_err;        /* 用户刚犯的错，先说这个 */
    switch (s_wifi_stage) {
    case WIFI_ST_PASS:
        if (s_wifi_pw_n > 0 && s_wifi_pw_n < 8)
            return "WPA2 口令至少 8 位 —— 现在还不够，检查一下有没有漏字符";
        return s_wifi_manual ? "填网络名（SSID）与口令：点输入框切换正在编辑的那一行"
                             : "输入这个网络的口令（区分大小写）";
    case WIFI_ST_LINK:
        return "用刚填的凭据连过去，成功后会写进板子的 NVS，下次开机自动连";
    default:
        return "板子的 Wi-Fi 是 2.4G，所以这里只列 2.4G 的网络（按信号从强到弱）";
    }
}

/* 扫描列表：行数固定建好（WIFI_AP_ROWS 行），用不上的隐藏 —— 与页面其他列表同规矩 */
static void wifi_fill_list(void)
{
    fnos_ap_t aps[FNOS_AP_MAX];
    int n = fnos_net_scan_results(aps, FNOS_AP_MAX);
    if (n > WIFI_AP_ROWS) n = WIFI_AP_ROWS;
    if (n > s_ui.wifi_ap_made) {                     /* 按需建行：事件回调只认行下标 */
        for (int i = s_ui.wifi_ap_made; i < n; i++) {
            uk_row_t *r = uk_row_create(s_ui.wifi_ap_pool, true, false);
            if (!r) { n = i; break; }
            /* 右侧说明是"3 格 · -54 dBm · 加密"，不是数字，得换 CJK 字体的 16px */
            lv_obj_set_style_text_font(r->val, UK_FONT_CJK_16, 0);
            /* 点的是整行：行本身必须留着 CLICKABLE，行里的字必须清掉
               （LVGL 9 命中测试会停在可点的子对象上，事件就传不到行了） */
            not_clickable(r->name1); not_clickable(r->name2);
            not_clickable(r->val);   not_clickable(r->unit);
            not_clickable(r->led);   not_clickable(r->bar);
            lv_obj_add_event_cb(r->row, wifi_ap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            /* 触摸落在"文字所在的那个盒子"上（label 的父对象），所以同一份回调也要挂在
               名字盒上：点 SSID 文字和点这一行的空白处效果完全一样。 */
            lv_obj_add_event_cb(lv_obj_get_parent(r->name1), wifi_ap_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);
            s_ui.wifi_ap_row[i] = r;
        }
        s_ui.wifi_ap_made = n;
    } else if (n < s_ui.wifi_ap_made) {
        row_trim(s_ui.wifi_ap_row, &s_ui.wifi_ap_made, WIFI_AP_ROWS, n);
    }
    for (int i = 0; i < n; i++) {
        const fnos_ap_t *a = &aps[i];
        int  bars = wifi_bars(a->rssi);
        uint32_t col = bars >= 3 ? UK_OK : bars == 2 ? UK_WARN : UK_T3;
        char meta[48];
        snprintf(meta, sizeof meta, "%d 格 · %d dBm · %s", bars, (int)a->rssi,
                 a->secure ? "加密" : "开放");
        /* 颜色之外还有"格数 + dBm"两种说法，色弱也读得出（版面合同第 9 条） */
        uk_row_set(s_ui.wifi_ap_row[i], a->ssid, NULL, meta, NULL, -1, col);
        lv_obj_set_style_text_color(s_ui.wifi_ap_row[i]->val, uk_c(col), 0);
        /* 从输密码页退回来时，让用户一眼看到刚才点的是哪一条 */
        bool sel = (s_wifi_sel == i);
        lv_obj_set_style_bg_color(s_ui.wifi_ap_row[i]->row, uk_c(UK_S2), 0);
        lv_obj_set_style_bg_opa(s_ui.wifi_ap_row[i]->row, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    pool_layout(&s_wifi_ap_n, &s_wifi_ap_w, &s_wifi_ap_h, n, s_ui.wifi_ap_pool, 860, false, NULL);

    if (!s_ui.wifi_list_hint) return;
    if (fnos_net_scan_busy())        set_txt(s_ui.wifi_list_hint, "%s", "正在扫描…");
    else if (fnos_net_scan_failed()) set_txt(s_ui.wifi_list_hint, "%s",
                                             "扫描没成功：Wi-Fi 还没起好，点「重新扫描」再试一次");
    else if (n == 0)                 set_txt(s_ui.wifi_list_hint, "%s",
                                             "没扫到网络：点「重新扫描」；隐藏网络点「手动输入」直接填名字");
    else {
        /* 有网络时这行留给"有多少个、怎么下手"，别再重复卡片副标题（预览截图里一眼看出的重复） */
        char b[96];
        snprintf(b, sizeof b, "共 %d 个网络（2.4G），按信号从强到弱 —— 点一行填口令", n);
        set_txt(s_ui.wifi_list_hint, "%s", b);
    }
}

static void wifi_refresh(void)
{
    if (!s_ui.wifi_card) return;

    /* 顶栏那颗常驻按钮：任何时候都要说清"板子现在有没有网"。
       没配置 = 橙色（要去处理）；配了但没连上 = 普通色 + "连接中"；连上了 = 亮色 SSID。 */
    if (s_ui.wifi_btn_lbl) {
        char b[WIFI_SSID_MAX + sizeof("连接中 · ")];
        const char *ssid = fnos_net_ssid();
        if (!fnos_net_configured())       snprintf(b, sizeof b, "Wi-Fi 未配置");
        else if (fnos_net_online())       snprintf(b, sizeof b, "%s", ssid[0] ? ssid : "已连接");
        else if (fnos_net_state() == FNOS_NET_FAILED) snprintf(b, sizeof b, "Wi-Fi 未连接");
        else if (ssid[0])                 snprintf(b, sizeof b, "连接中 · %s", ssid);
        else                              snprintf(b, sizeof b, "连接中…");
        set_txt(s_ui.wifi_btn_lbl, "%s", b);
        lv_obj_set_style_text_color(s_ui.wifi_btn_lbl,
                                    uk_c(!fnos_net_configured() ? UK_WARN
                                        : fnos_net_online()     ? UK_T1 : UK_T2), 0);
    }

    if (!s_wifi_open) return;
    if (s_ui.wifi_hint) set_txt(s_ui.wifi_hint, "%s", wifi_stage_hint());

    if (s_wifi_stage == WIFI_ST_SCAN) {
        wifi_fill_list();
    } else if (s_wifi_stage == WIFI_ST_PASS) {
        wifi_render_input();
    } else {                                     /* WIFI_ST_LINK */
        fnos_net_state_t ns = fnos_net_state();
        bool on = (ns == FNOS_NET_ONLINE);
        bool bad = (ns == FNOS_NET_FAILED);
        show(s_ui.wifi_done, on || bad);
        btn_label(s_ui.wifi_done, UK_FONT_CJK_16, UK_ON_PRIMARY, on ? "完成" : "修改密码");
        if (s_ui.wifi_link_big) {
            set_txt(s_ui.wifi_link_big, "%s", on ? "连接成功" : bad ? "没连上" : "正在连接…");
            lv_obj_set_style_text_color(s_ui.wifi_link_big,
                                        uk_c(on ? UK_OK : bad ? UK_WARN : UK_T1), 0);
        }
        if (s_ui.wifi_link_sub) {
            char b[220];
            if (on) {
                snprintf(b, sizeof b, "%s · 信号 %d dBm\n板子已经记住这个网络，下次开机自动连。",
                         fnos_net_state_str(), (int)fnos_net_rssi());
            } else {
                snprintf(b, sizeof b,
                         "%s\n连接失败可点「修改密码」重试：口令区分大小写，"
                         "也确认一下这个网络是不是 2.4G。", fnos_net_state_str());
            }
            set_txt(s_ui.wifi_link_sub, "%s", b);
        }
    }
}

/* ── 交互 ─────────────────────────────────────────────────────────────── */

/* 选中一个网络：开放的直接连，加密的进输密码页。
   选中的 SSID 不给编辑（想改就点「手动输入」）—— 手滑改错一个字符的失败最难查。 */
static void wifi_pick(const fnos_ap_t *a)
{
    snprintf(s_wifi_ssid, sizeof s_wifi_ssid, "%s", a->ssid);
    s_wifi_manual = false;
    s_wifi_field  = 1;
    s_wifi_pw[0]  = 0;
    s_wifi_pw_n   = 0;
    s_wifi_reveal = false;
    s_wifi_layer  = 0;
    s_wifi_shift  = false;
    if (!a->secure) {
        wifi_set_stage(WIFI_ST_LINK);
        fnos_net_set_credentials(s_wifi_ssid, "");
    } else {
        wifi_set_stage(WIFI_ST_PASS);
    }
    wifi_refresh();
}

static void wifi_ap_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    fnos_ap_t aps[FNOS_AP_MAX];
    int n = fnos_net_scan_results(aps, FNOS_AP_MAX);
    if (idx < 0 || idx >= n) return;
    s_wifi_sel = idx;
    s_wifi_err[0] = 0;
    wifi_pick(&aps[idx]);
}

static void wifi_key_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= WIFI_KB_KEYS) return;
    s_wifi_err[0] = 0;                       /* 用户一动键盘，刚才那句错误就该消失 */
    const wifi_key_t k = (s_wifi_layer ? WIFI_KB_SYMBOLS : WIFI_KB_LETTERS)[i];

    bool edit_ssid = (s_wifi_manual && s_wifi_field == 0);
    char  *buf = edit_ssid ? s_wifi_ssid : s_wifi_pw;
    size_t cap = edit_ssid ? WIFI_SSID_MAX : WIFI_PW_MAX;
    bool consumed = false;

    switch (k.kind) {
    case WK_CH:
    case WK_SPACE: {
        char ch = (k.kind == WK_SPACE) ? ' ' : k.ch;
        if (s_wifi_shift && !s_wifi_layer && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        size_t n = strlen(buf);
        if (n < cap) { buf[n] = ch; buf[n + 1] = 0; }
        consumed = true;
        break;
    }
    case WK_BACK: {
        size_t n = strlen(buf);
        if (n) buf[n - 1] = 0;
        break;
    }
    case WK_SHIFT:
        s_wifi_shift = !s_wifi_shift;
        break;
    case WK_LAYER:
        s_wifi_layer = !s_wifi_layer;
        s_wifi_shift = false;
        break;
    case WK_OK:
        wifi_connect_cb(NULL);
        return;
    }
    if (consumed) s_wifi_shift = false;          /* 打完一个字符自动回小写，和手机一致 */
    s_wifi_pw_n = (int)strlen(s_wifi_pw);
    wifi_apply_layer();
    wifi_render_input();
    wifi_refresh();
}

static void wifi_rescan_cb(lv_event_t *e)
{
    (void)e;
    fnos_net_scan_clear();
    fnos_net_scan_request();
    wifi_refresh();
}

static void wifi_manual_cb(lv_event_t *e)
{
    (void)e;
    s_wifi_manual = true;
    s_wifi_sel    = -1;
    s_wifi_field  = 0;
    s_wifi_ssid[0] = 0;
    s_wifi_pw[0]   = 0;
    s_wifi_pw_n    = 0;
    s_wifi_reveal  = false;
    s_wifi_layer   = 0;
    s_wifi_shift   = false;
    wifi_set_stage(WIFI_ST_PASS);
    wifi_refresh();
}

static void wifi_close_cb(lv_event_t *e)
{
    (void)e;
    /* 关掉 = 这次开机不再自动弹（用户明确表示"我知道，先不看"），
       但入口一直在顶栏上，橙色提醒也一直在。 */
    s_wifi_dismissed = true;
    wifi_show(false);
}

static void wifi_result_cb(lv_event_t *e)
{
    if (fnos_net_state() == FNOS_NET_ONLINE) { wifi_close_cb(e); return; }
    if (fnos_net_state() != FNOS_NET_FAILED) return;
    s_wifi_field = 1;
    s_wifi_reveal = false;
    wifi_set_stage(WIFI_ST_PASS);
    wifi_refresh();
}

static void wifi_field_cb(lv_event_t *e)
{
    if (!s_wifi_manual) return;                  /* 选中的网络名不给改 */
    s_wifi_field = (int)(intptr_t)lv_event_get_user_data(e) ? 1 : 0;
    wifi_render_input();
}

static void wifi_eye_cb(lv_event_t *e)
{
    (void)e;
    s_wifi_reveal = !s_wifi_reveal;
    if (s_ui.wifi_eye_lbl) set_txt(s_ui.wifi_eye_lbl, "%s", s_wifi_reveal ? "隐藏" : "显示");
    wifi_render_input();
}

static void wifi_back_cb(lv_event_t *e)
{
    (void)e;
    wifi_set_stage(WIFI_ST_SCAN);
    fnos_net_scan_request();                     /* 列表可能是空的（刚开机就进来的） */
    wifi_refresh();
}

static void wifi_connect_cb(lv_event_t *e)
{
    (void)e;
    if (!s_wifi_ssid[0]) {
        snprintf(s_wifi_err, sizeof s_wifi_err, "先填网络名（SSID）再连接");
        wifi_refresh();
        return;
    }
    if (s_wifi_pw_n > 0 && s_wifi_pw_n < 8) {
        snprintf(s_wifi_err, sizeof s_wifi_err,
                 "WPA2 口令至少 8 位 —— 再检查一遍（区分大小写），现在是 %d 位", s_wifi_pw_n);
        wifi_refresh();
        return;
    }
    wifi_set_stage(WIFI_ST_LINK);
    fnos_net_set_credentials(s_wifi_ssid, s_wifi_pw);
    wifi_refresh();
}

static void wifi_again_cb(lv_event_t *e)
{
    (void)e;
    s_wifi_pw[0]  = 0;
    s_wifi_pw_n   = 0;
    s_wifi_reveal = false;
    wifi_set_stage(WIFI_ST_SCAN);
    fnos_net_scan_clear();
    fnos_net_scan_request();
    wifi_refresh();
}

/* 把采集端的失败标记（fnos_data.c 的 classify_conn_err / note_failure 写的
   last_err）翻成用户能照着处理的短句。板子连不上 NAS 的原因差别很大：网线没插
   和"NAS 换过证书"要做的处理完全不同，笼统显示"离线"会让人白折腾。 */

static void pair_layout(bool code, bool fingerprint)
{
    lv_obj_t *grid = s_ui.pair_grid;
    lv_obj_update_layout(s_ui.pair_card);
    int32_t min_w = 0;
    int32_t chrome = lv_obj_get_style_pad_left(s_ui.pair_codecol, 0) +
                     lv_obj_get_style_pad_right(s_ui.pair_codecol, 0) +
                     2 * lv_obj_get_style_border_width(s_ui.pair_codecol, 0);
    lv_point_t size;
    if (code) {
        lv_text_get_size(&size, "0", UK_FONT_NUM_32, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_t *slots = lv_obj_get_parent(s_ui.pair_slot[0]);
        min_w = PAIR_CODE_LEN * size.x + (PAIR_CODE_LEN - 1) * lv_obj_get_style_pad_column(slots, 0);
        int32_t key_w = UK_ROW_H;
        for (int i = 0; i < PAIR_PAD_KEYS; i++) {
            lv_obj_t *key = s_ui.pair_key[i];
            lv_obj_t *label = lv_obj_get_child(key, 0);
            lv_text_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, 0),
                             0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            int32_t need = size.x + lv_obj_get_style_pad_left(key, 0) +
                           lv_obj_get_style_pad_right(key, 0) + 2 * lv_obj_get_style_border_width(key, 0);
            if (need > key_w) key_w = need;
        }
        int32_t keys_w = PAIR_PAD_COLS * key_w +
                         (PAIR_PAD_COLS - 1) * lv_obj_get_style_pad_column(lv_obj_get_parent(s_ui.pair_key[0]), 0);
        if (keys_w > min_w) min_w = keys_w;
    }
    if (fingerprint) {
        for (int i = 0; i < PAIR_FP_LINES; i++) {
            lv_text_get_size(&size, lv_label_get_text(s_ui.pair_fp[i]), UK_FONT_NUM_20,
                             0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > min_w) min_w = size.x;
        }
    }
    min_w += chrome;
    int32_t w = lv_obj_get_content_width(grid);
    int32_t gap = lv_obj_get_style_pad_column(grid, 0);
    lv_obj_t *cards[] = { s_ui.pair_steps, s_ui.pair_msg_panel, s_ui.pair_codecol, s_ui.pair_info_panel };
    int visible = 0;
    for (unsigned i = 0; i < sizeof cards / sizeof cards[0]; i++)
        if (!lv_obj_has_flag(cards[i], LV_OBJ_FLAG_HIDDEN)) visible++;
    int cols = visible > 1 && w >= 2 * min_w + gap ? 2 : 1;
    int32_t cw = (w - gap * (cols - 1)) / cols;
    for (unsigned i = 0; i < sizeof cards / sizeof cards[0]; i++) lv_obj_set_width(cards[i], cw);
    lv_obj_update_layout(grid);
    if (fingerprint) {
        int32_t available = lv_obj_get_content_width(uk_card_body(s_ui.pair_info_panel));
        const lv_font_t *font = UK_FONT_NUM_32;
        for (int i = 0; i < PAIR_FP_LINES; i++) {
            lv_text_get_size(&size, lv_label_get_text(s_ui.pair_fp[i]), UK_FONT_NUM_32,
                             0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > available) font = UK_FONT_NUM_20;
        }
        for (int i = 0; i < PAIR_FP_LINES; i++) lv_obj_set_style_text_font(s_ui.pair_fp[i], font, 0);
    }
    lv_obj_update_layout(s_ui.pair_card);
}

static void pair_refresh(void)
{
    if (!s_ui.pair_btn_lbl) return;

    fnos_pair_view_t v;
    fnos_pair_view(&v);
    bool prov = fnos_pair_provisioned();

    int stage;
    switch (v.state) {
    case FNOS_PAIR_FETCHING:
    case FNOS_PAIR_PAIRING:    stage = PST_BUSY;    break;
    case FNOS_PAIR_CONFIRM:    stage = PST_CONFIRM; break;
    case FNOS_PAIR_PROVISIONED: stage = PST_DONE;   break;
    case FNOS_PAIR_FAILED:     stage = PST_FAIL;    break;
    default:                   stage = PST_CODE;    break;
    }

    /* 侧栏入口：入口文字保持主文本色（它是"去哪儿"，不是状态），状态由状态点表示：
       未配对 = 告警橙（需要你动手），进行中 = 交互蓝，已配对 = 主文本色不发光。 */
    if (stage == PST_BUSY) {
        set_txt(s_ui.pair_btn_lbl, "%s", "配对中");
        s_pair_alert = UK_BLUE_TXT;
    } else if (prov) {
        set_txt(s_ui.pair_btn_lbl, "%s", "已配对");
        s_pair_alert = UK_T1;
    } else {
        set_txt(s_ui.pair_btn_lbl, "%s", "配对");
        s_pair_alert = UK_WARN;
    }
    theme_apply();   /* 颜色只在这一处落地，别在别处再写一遍 */

    if (!s_pair_open || !s_ui.pair_card) return;

    for (int i = 0; i < PAIR_FP_LINES; i++) {
        char line[PAIR_FP_PER * 3];
        pair_fp_line(v.fingerprint, i, line, sizeof line);
        set_txt(s_ui.pair_fp[i], "%s", line);
    }

    /* 六位码：每位一个槽。已输入的位显示数字并转成主文本色，空位保持"–"与次级色 ——
       "还差几位"要能一眼看出来，这是键盘输入类界面最基本的要求。 */
    const char *shown = (stage == PST_CONFIRM) ? s_pair_last : s_pair_code;
    int shown_len = (stage == PST_CONFIRM) ? (int)strlen(s_pair_last) : s_pair_len;
    for (int i = 0; i < PAIR_CODE_LEN; i++) {
        if (i < shown_len) {
            char d[2] = { shown[i], 0 };
            set_txt(s_ui.pair_slot[i], "%s", d);
            lv_obj_set_style_text_color(s_ui.pair_slot[i], uk_c(UK_T1), 0);
        } else {
            set_txt(s_ui.pair_slot[i], "%s", "-");
            lv_obj_set_style_text_color(s_ui.pair_slot[i], uk_c(UK_T4), 0);
        }
    }

    char meta[192] = "";
    switch (stage) {
    case PST_CODE:
        set_txt(s_ui.pair_title, "%s", "与 NAS 配对");
        set_txt(s_ui.pair_hint, "%s", "板子还没有和这台 NAS 建立信任关系");
        set_txt(s_ui.pair_code_lbl, "%s", "配对码");
        set_txt(s_ui.pair_code_sub, "%s",
                s_pair_len ? "输入完 6 位后点键盘上的「确认」"
                           : "在 NAS 管理页「设备配对」生成，把 6 位数字输进下方的键盘");
        break;
    case PST_BUSY:
        set_txt(s_ui.pair_title, "%s", "正在配对");
        set_txt(s_ui.pair_hint, "%s", "正在与 NAS 建立受信连接，请不要断电");
        break;
    case PST_CONFIRM:
        set_txt(s_ui.pair_title, "%s", "核对证书指纹");
        set_txt(s_ui.pair_hint, "%s", "逐段比对下面 4 行指纹，与 NAS 管理页「传输加密」里的一致才接受");
        set_txt(s_ui.pair_code_lbl, "%s", "已提交的配对码");
        set_txt(s_ui.pair_code_sub, "%s", "板子已取回证书，正在等你确认指纹");
        if (v.subject[0]) {
            snprintf(meta, sizeof meta, "%s · 到期 %s", v.subject, v.not_after);
        }
        break;
    case PST_DONE:
        set_txt(s_ui.pair_title, "%s", "已配对");
        set_txt(s_ui.pair_hint, "%s", v.tls ? "开发板已固定这台 NAS 的证书，此后只走加密连接" : "已配对，当前使用明文 HTTP 连接");
        snprintf(meta, sizeof meta, "%s:%d · %s", v.host, v.port, v.tls ? "HTTPS" : "明文 HTTP");
        if (v.subject[0]) {
            size_t n = strlen(meta);
            snprintf(meta + n, sizeof meta - n, " · %s", v.subject);
        }
        break;
    default:
        set_txt(s_ui.pair_title, "%s", "配对失败");
        const char *recovery = "确认 NAS 上应用在运行，并且配对码没有过期";
        if (strstr(v.msg, "NVS") || strstr(v.msg, "写入"))
            recovery = "本地保存失败：重启设备并检查存储，再重新配对";
        else if (strstr(v.msg, "内存") || strstr(v.msg, "装不下"))
            recovery = "本地资源不足：重启设备；证书过长请在 NAS 更换后重试";
        set_txt(s_ui.pair_hint, "%s", recovery);
        set_txt(s_ui.pair_code_lbl, "%s", "重试配对码");
        set_txt(s_ui.pair_code_sub, "%s", "核对下面的失败原因，改正后重新输入配对码");
        /* **失败原因必须真的显示出来。**
           这个阶段左栏被三步指引占着（见上面 show_steps_l 的说明），所以 pair_msg
           在这个阶段是不更新的；指纹与 meta 两行又只在 CONFIRM/DONE 才可见。
           结果就是：界面写着"核对下面的失败原因"，而下面根本没有原因 ——
           设备端为此准备了 8 条文案（"证书太长，装不下"、"配对成功但写入 NVS 失败"…），
           一条都到不了用户眼前。
           放在 code_sub 这行：它本来就写着"核对下面的失败原因"，是这句话本人的位置。 */
        if (v.msg[0]) {
            char sub[192];
            snprintf(sub, sizeof sub, "失败原因：%s", v.msg);
            set_txt(s_ui.pair_code_sub, "%s", sub);
        }
        break;
    }
    set_txt(s_ui.pair_meta, "%s", meta);

    /* 三步指引的文案：只在"还没配对/失败"时出现，说的是"现在这一步该做什么" */
    if (stage == PST_CODE || stage == PST_FAIL) {
        static const char *STEP[PAIR_STEPS] = {
            "在 NAS 管理页「设备配对」生成 6 位配对码",
            "用键盘把 6 位数字输进上面的槽位，点「确认」",
            "核对证书指纹，一致才点「接受并配对」",
        };
        for (int i = 0; i < PAIR_STEPS; i++) set_txt(s_ui.pair_step_txt[i], "%s", STEP[i]);
    }

    /* Guidance and connection details occupy one card slot according to the pairing stage. */
    bool show_steps_l = (stage == PST_CODE || stage == PST_FAIL);
    if (!show_steps_l) {
        set_txt(s_ui.pair_msg, "%s", v.msg);
    }
    /* 交互提示（"还差 3 位"、"正在从 NAS 取证书…"）改挂在右侧信息块的最后一行：
       它和"配对码/槽位/出处"讲的是同一件事，放在一起比放左栏更好读，
       也不会再去抢左栏那块地方。 */
    char right_hint[128] = "";
    if (s_pair_hint[0] && (s_pair_hint_stage == PAIR_HINT_ANY || s_pair_hint_stage == stage)) {
        snprintf(right_hint, sizeof right_hint, "%s", s_pair_hint);
    }
    static const char *ACCEPT_NOTE =
        "接受之后，板子会把这张证书的指纹和配对令牌写进自己的存储器，"
        "以后每次采样都只认这张证书。\n\n"
        "指纹对不上就点「取消」——那说明中间可能有人在转发证书。";

    bool show_code   = (stage == PST_CODE || stage == PST_FAIL);
    bool show_pad    = (stage == PST_CODE || stage == PST_FAIL);
    bool show_fp     = (stage == PST_CONFIRM || stage == PST_DONE);
    bool show_steps  = show_code;
    bool show_note   = (stage == PST_CONFIRM);
    bool show_ok     = (stage == PST_CONFIRM);
    bool show_forget = (prov && stage != PST_CONFIRM && stage != PST_BUSY);

    /* 显隐只需要动四个容器：flex 里"藏起来"和"不占地方"是一回事，但**只有容器
       藏了才算数** —— 栏本身还在就会继续分走宽度（左右两栏都是 grow 1），
       剩下的半栏会摊成一整片空白。细粒度开关（槽位/指纹行/键盘）由容器代管。 */
    show(s_ui.pair_codecol,      show_code || show_pad);
    show(s_ui.pair_info_panel,   show_fp);
    show(s_ui.pair_steps,        show_steps);
    bool show_msg = show_note || (!show_steps_l && v.msg[0] != 0);
    if (show_note) set_txt(s_ui.pair_msg, "%s", ACCEPT_NOTE);
    set_txt(uk_card_title(s_ui.pair_msg_panel), "%s", show_note ? "配对说明" : "连接状态");
    show(s_ui.pair_msg_panel,    show_msg);
    (void)show_code; (void)show_pad; (void)show_fp;
    for (int i = 0; i < PAIR_CODE_LEN; i++) show(s_ui.pair_slot[i], true);
    for (int i = 0; i < PAIR_FP_LINES; i++) show(s_ui.pair_fp[i], true);
    /* 右侧信息块最后一行：交互提示。放这里就不必在左栏和"三步指引"抢地方。 */
    if (s_ui.pair_code_sub) {
        if (right_hint[0]) {
            set_txt(s_ui.pair_code_sub, "%s", right_hint);
            lv_obj_set_style_text_color(s_ui.pair_code_sub, uk_c(UK_WARN), 0);
        } else {
            lv_obj_set_style_text_color(s_ui.pair_code_sub, uk_c(UK_T3), 0);
        }
    }
    if (show_ok) lv_obj_remove_flag(s_ui.pair_ok, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(s_ui.pair_ok, LV_OBJ_FLAG_HIDDEN);
    if (show_forget) lv_obj_remove_flag(s_ui.pair_forget, LV_OBJ_FLAG_HIDDEN);
    else             lv_obj_add_flag(s_ui.pair_forget, LV_OBJ_FLAG_HIDDEN);

    /* 按钮文案与"分量"随阶段换：主操作永远是"这一步要你点的那个"，
       并且永远待在右下角同一个位置（肌肉记忆）。 */
    btn_label(s_ui.pair_ok, UK_FONT_CJK_16, UK_ON_PRIMARY,
              stage == PST_CONFIRM ? "接受并配对" : "确认");
    btn_label(s_ui.pair_cancel, UK_FONT_CJK_16, UK_T1,
              stage == PST_CONFIRM ? "取消" : "关闭");
    if (s_pair_forget_armed) {
        btn_skin(s_ui.pair_forget, BTN_DANGER);
        btn_label(s_ui.pair_forget, UK_FONT_CJK_12, UK_ON_DANGER, "再点一次确认解除");
    } else {
        btn_skin(s_ui.pair_forget, BTN_NEUTRAL);
        btn_label(s_ui.pair_forget, UK_FONT_CJK_16, UK_T2, "解除配对");
    }
    pair_layout(show_code, show_fp);
    static int previous_stage = -1;
    if (previous_stage != stage) lv_obj_scroll_to_y(s_ui.pair_grid, 0, LV_ANIM_OFF);
    previous_stage = stage;
}

/* 数字键盘：0..9 追加、10 退格、11 提交。 */
static void pair_pad_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    s_pair_hint[0] = 0;
    if (k <= 9) {
        if (s_pair_len < PAIR_CODE_LEN) {
            s_pair_code[s_pair_len++] = (char)('0' + k);
            s_pair_code[s_pair_len] = 0;
        }
    } else if (k == 10) {
        if (s_pair_len > 0) s_pair_code[--s_pair_len] = 0;
    } else {
        if (s_pair_len != PAIR_CODE_LEN) {
            snprintf(s_pair_hint, sizeof s_pair_hint, "配对码是 6 位数字，还差 %d 位",
                     PAIR_CODE_LEN - s_pair_len);
            s_pair_hint_stage = PST_CODE;
        } else {
            fnos_pair_begin(s_pair_code);
            /* 码本身留着：核对指纹那一步要能看见"我提交的是哪 6 位"，
               否则用户没法判断失败到底是码错还是指纹错 */
            memcpy(s_pair_last, s_pair_code, sizeof s_pair_last);
            s_pair_len = 0;
            s_pair_code[0] = 0;
            snprintf(s_pair_hint, sizeof s_pair_hint, "正在从 NAS 取证书…");
            s_pair_hint_stage = PST_BUSY;
        }
    }
    pair_refresh();
}

static void pair_ok_cb(lv_event_t *e)
{
    (void)e;
    s_pair_hint[0] = 0;
    fnos_pair_confirm(true);
    pair_refresh();
}

static void pair_cancel_cb(lv_event_t *e)
{
    (void)e;
    s_pair_hint[0] = 0;
    s_pair_len = 0;
    s_pair_code[0] = 0;
    s_pair_last[0] = 0;
    /* 停在"待确认"上时关闭 = 放弃这次：指纹没核对过，什么都不能留下。 */
    if (!fnos_pair_provisioned()) fnos_pair_confirm(false);
    pair_show(false);
}

static void pair_forget_cb(lv_event_t *e)
{
    (void)e;
    if (!s_pair_forget_armed) {
        s_pair_forget_armed = true;
        snprintf(s_pair_hint, sizeof s_pair_hint,
                 "再点一次清除板端配对；NAS 端需另行撤销设备");
        s_pair_hint_stage = PAIR_HINT_ANY;
    } else {
        s_pair_forget_armed = false;
        s_pair_hint[0] = 0;
        fnos_pair_forget();
    }
    pair_refresh();
}

/* ── 换页 / 夜间 ──────────────────────────────────────────────────── */
static void nav_select(int idx)
{
    /* 只负责"谁被选中"这件事；选中长什么样统一由 theme_apply() 落地
       （它同时管日夜配色，选中项的蓝色叠加与发丝边都在那里面）。 */
    s_page = idx;
    theme_apply();
    header_refresh();
}

static void nav_cb(lv_event_t *e)
{
    fnos_ui_set_page((int)(intptr_t)lv_event_get_user_data(e));
}


static void night_apply(void)
{
    for (int i = 0; i < s_ui.ncards; i++) {
        lv_obj_t *card = s_ui.cards[i];
        if (!card) continue;
        if (!s_ui.card_colors_saved) s_ui.card_colors[i] = lv_obj_get_style_bg_color(card, 0);
        uk_anim_reveal_settle(card);
        const bool light = lv_color_eq(s_ui.card_colors[i], uk_c(UK_PEACH));
        lv_obj_set_style_bg_color(card, s_night ? lv_color_mix(uk_c(UK_BG), s_ui.card_colors[i],
                                       light ? UK_NIGHT_LIGHT_MIX : UK_NIGHT_MIX) : s_ui.card_colors[i], 0);
        if (light) panel_text_tone(card, s_night ? UK_T2 : UK_INK);
        lv_obj_set_style_border_opa(card, s_night ? 0 : UK_EDGE_OPA, 0);
    }
    s_ui.card_colors_saved = true;
    theme_apply();
}

static void show(lv_obj_t *o, bool on)
{
    if (!o) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static int64_t data_age_ms(const fnos_status_t *st)
{
    int64_t age = esp_timer_get_time() / 1000 - st->recv_ms;
    return age < 0 ? 0 : age;
}

/* ── 刷屏 ─────────────────────────────────────────────────────────── */

/* 曲线横轴：能算出时间就说时间，算不出（旧版应用没给 ts）就说次数。
   "有断流"只在平均采样间隔明显大于 1 秒时说 —— NAS 停过一阵再回填时，
   曲线上那段空白必须有个说法，否则会被读成"那段时间负载为 0"。 */
static const char *hist_span_text(char *b, size_t cap, const fnos_status_t *st)
{
    if (!st->hist_ts_ok || st->hist_span_s <= 0) {
        snprintf(b, cap, "最近 %d 次采集", UI_HIST);
        return b;
    }
    /* 说的是"这张图上这一段有多宽"：窗口里的点数 × 平均采样间隔。
       用平均间隔而不是历史总跨度——总跨度含了窗口之外的时间，说出去对不上图。 */
    int sec = st->hist_avg_gap_x10 > 0
                  ? (int)((int64_t)(UI_HIST - 1) * st->hist_avg_gap_x10 / 10)
                  : st->hist_span_s;
    if (sec < 90)         snprintf(b, cap, "最近 %d 秒", sec);
    else if (sec < 5400)  snprintf(b, cap, "最近 %d 分钟", (sec + 30) / 60);
    else                  snprintf(b, cap, "最近 %.1f 小时", sec / 3600.0);
    size_t n = strlen(b);
    if (st->hist_avg_gap_x10 >= 20 && n + 16 < cap) {
        snprintf(b + n, cap - n, "%s", " · 有断流");
    }
    return b;
}

static const char *link_reason(const fnos_status_t *st)
{
    if (!st->last_err[0]) return NULL;
    if (!strcmp(st->last_err, "no wifi"))          return "Wi-Fi 没连上";
    if (!strcmp(st->last_err, "dns fail"))         return "地址解析不了，检查 NAS 地址";
    if (!strcmp(st->last_err, "connect/timeout"))  return "连不上 NAS：应用没启动或端口不对";
    if (!strcmp(st->last_err, "tls handshake"))    return "TLS 握手失败：证书多半换了，需要重新配对";
    if (!strcmp(st->last_err, "tls setup"))        return "TLS 初始化失败，看串口日志";
    if (!strcmp(st->last_err, "cert parse"))       return "本机存的证书读不出来，需要重新配对";
    if (!strcmp(st->last_err, "no cert"))          return "配的是 HTTPS 但本机没有证书，需要重新配对";
    if (!strcmp(st->last_err, "token rejected"))   return "NAS 拒绝了这个令牌：在管理页撤销过？重新配对";
    if (!strcmp(st->last_err, "bad payload"))      return "NAS 返回的数据解析不了";
    if (!strcmp(st->last_err, "http status"))      return "NAS 返回了错误状态";
    if (!strcmp(st->last_err, "data capacity"))     return "设备数据超出内存预算，请调整采集或固件配置";
    if (!strcmp(st->last_err, "read error"))       return "读到一半连接断了";
    return NULL;
}

/* link_reason 的公开形式（声明在 fnos_ui.h）。纯函数、无状态——公开出来只是为了让
   主机预览能逐个标签断言"翻得出话、且每句都不一样"。不认识的标签返回 NULL 时，
   总览页那行「为什么连不上」会整条消失，而界面看上去只是"没写原因"。 */
const char *fnos_ui_link_reason(const fnos_status_t *st);
const char *fnos_ui_link_reason(const fnos_status_t *st)
{
    return link_reason(st);
}

/* 底栏那个"最严重告警"状态点：只有严重级别才呼吸。
   呼吸是 700ms 往复的无限动画，而 refresh() 每秒都会跑一次——每次都重启动画的话
   透明度会在 1 秒处被硬拉回 255，看起来是"卡一下"而不是呼吸。所以只在**状态翻转**时
   才调用 uk_anim_pulse()。 */
static bool s_pulse_on;

/* ── 温度清单：分组 / 建块 / 填值（p4 用块，p3 用行，分组只算一次）───────── */
static temp_grp_t *s_tgrp;
static int s_tgrp_capacity;
static int        s_tgrp_n;
/* 分组：temps[] 已按 (dev, ch) 排好（fnos_data.c 的 temp_cmp），同设备必然相邻。
   dn 重名（同型号多块盘）时把 dev 附上 —— 否则用户看到的是"N 份一模一样的行"。 */
static bool temp_groups(void)
{
    const fnos_status_t *st = &s_st;
    const int n = st->ntemps;
    for (int i = 0; i < s_tgrp_n; i++) { lv_free(s_tgrp[i].name); s_tgrp[i].name = NULL; }
    s_tgrp_n = 0; /* Old dev pointers belong to the previous snapshot. */
    if (n > s_tgrp_capacity) {
        temp_grp_t *grown = uk_realloc(s_tgrp, (size_t)n * sizeof *grown);
        if (!grown) return false;
        memset(grown + s_tgrp_capacity, 0, (n - s_tgrp_capacity) * sizeof *grown);
        s_tgrp = grown;
        s_tgrp_capacity = n;
    }
    s_tgrp_n = 0;
    for (int i = 0; i < n; ) {
        temp_grp_t *g = &s_tgrp[s_tgrp_n];
        g->dev   = st->temps[i].dev[0] ? st->temps[i].dev : st->temps[i].ch;
        g->first = i;
        g->hot   = i;
        g->max_c = st->temps[i].c;
        int j = i;
        while (j < n) {
            const char *d = st->temps[j].dev[0] ? st->temps[j].dev : st->temps[j].ch;
            if (strcmp(d, g->dev) != 0) break;
            if (st->temps[j].c > g->max_c) { g->max_c = st->temps[j].c; g->hot = j; }
            j++;
        }
        g->count = j - i;
        g->min_c  = g->max_c;
        for (int k = i; k < j; k++) {
            if (st->temps[k].c < g->min_c) g->min_c = st->temps[k].c;
            if (st->temps[k].c > g->max_c) g->max_c = st->temps[k].c;
        }
        s_tgrp_n++;
        i = j;
    }
    for (int a = 0; a < s_tgrp_n; a++) {
        const fnos_temp_t *ta = &st->temps[s_tgrp[a].first];
        const char *dna = ta->dn[0] ? ta->dn : ta->dev;
        int same = 0;
        for (int b = 0; b < s_tgrp_n; b++) {
            const fnos_temp_t *tb = &st->temps[s_tgrp[b].first];
            const char *dnb = tb->dn[0] ? tb->dn : tb->dev;
            if (strcmp(dna, dnb) == 0) same++;
        }
        const char *suffix = same > 1 ? s_tgrp[a].dev :
            (s_tgrp[a].count == 1 && ta->ch[0] ? ta->ch : "");
        size_t size = strlen(dna) + strlen(suffix) + sizeof " · ";
        s_tgrp[a].name = uk_alloc(size);
        if (!s_tgrp[a].name) {
            for (int k = 0; k < s_tgrp_n; k++) { lv_free(s_tgrp[k].name); s_tgrp[k].name = NULL; }
            s_tgrp_n = 0;
            return false;
        }
        snprintf(s_tgrp[a].name, size, "%s%s%s", dna, suffix[0] ? " · " : "", suffix);
        s_tgrp[a].dup = (same > 1);
        /* 单通道设备（核显、单传感器 NVMe、老采集端）：通道名并进块头，不再单起一行 ——
           否则块头写 "Intel UHD Graphics 35.0"、块体又写 "temp1 35.0"，同一件事说两遍。
           同名设备（same>1）不并：那时块头那行要留给 dev 消歧。 */
        s_tgrp[a].inline_ch = (s_tgrp[a].count == 1 && same == 1 && ta->ch[0]) ? 1 : 0;

    }
    return true;
}

/* 块内的一行：左标签（通道名）+ 右数值。不复用 uk_row —— 它带 UK_ROW_MIN=40 的最小高，
   24 路按 40px 排必然溢出；这里的行高由字体决定（num_20 行高 24），块内只留 4px 行距。 */
static lv_obj_t *temp_line(lv_obj_t *parent, bool top_align)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_height(l, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_pad_column(l, UK_S2, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW);
    /* 块头要 top 对齐：设备名可能折两行，居中对齐会把最热温度推到第二行的位置，
       一块一个位置，看起来就是"数值没对齐"；顶对齐后状态点、名字首行、最热温度
       永远在同一条基线上。通道行只有一行字，用居中更稳。 */
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START,
                          top_align ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                          top_align ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return l;
}

/* 数值 + 单位：**必须装在一个底对齐的小盒子里**。
   直接把 num_20 的数值和 txt_12 的 "°C" 并排放在居中对齐的 flex 行里，单位会被
   垂直居中再被 pad_bottom 往上顶 —— 12px 的字坐在 24px 行中间，看起来就是上标
   （用户第二次反馈的"温度界面还有错位"就是它：43.9 的基线在下面，˚C 浮在上面）。
   uk_row 里的 valbox 用的是交叉轴 END 对齐 + unit pad_bottom 3，那套是验证过的，
   这里照抄。 */
static lv_obj_t *temp_value_box(lv_obj_t *parent, lv_obj_t **out_val)
{
    lv_obj_t *vbox = lv_obj_create(parent);
    lv_obj_set_size(vbox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(vbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vbox, 0, 0);
    lv_obj_set_style_pad_all(vbox, 0, 0);
    lv_obj_set_style_pad_column(vbox, UK_S1, 0);   /* 数值与单位之间 4px：2px 时"34.0˚C"看着连成一片 */
    lv_obj_set_flex_flow(vbox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vbox, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_min_width(vbox, 0, 0);
    lv_obj_clear_flag(vbox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    *out_val = uk_label(vbox, UK_FONT_NUM_20, UK_T1, "");
    lv_obj_t *u = uk_label(vbox, UK_FONT_TXT_12, UK_T3, "°C");
    lv_obj_set_style_pad_bottom(u, 3, 0);        /* 与数字基线对齐（同 uk_row 的 unit） */
    return vbox;
}

static void temp_toggle_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_tgrp_n || s_tgrp[i].count <= 1) return;
    s_ui.temp_blk[i].collapsed = !s_ui.temp_blk[i].collapsed;
    s_p4_n = -1;
    refresh();
}

/* 展开的通道排成网格：列数按块宽自适应（整页宽 ~1000px 时 4 列，池里三块并排
   ~325px 时 2 列）。之前无论多宽都是一列 —— 12 路会把整块撑到视口外，右侧还空一半。
   格子宽度是算出来的像素值而不是百分比：百分比在窄块里会把数值挤出格子，而且
   4 列 × 24% 会在右边留一条 4% 的缝。宽高都由这里算，池改列数后重算一次即可。 */
#define TEMP_GRID_CELL_MIN 150
#define TEMP_GRID_MAX_COLS 4

static void temp_grid_relayout(temp_blk_t *b)
{
    if (!b->grid || !lv_obj_is_valid(b->grid)) return;
    int32_t w = lv_obj_get_content_width(b->grid);
    if (w <= 0) return;                     /* 页面还藏着：子树没参与 flex，等显示后再排 */
    int32_t cols = (w + UK_S2) / (TEMP_GRID_CELL_MIN + UK_S2);
    if (cols < 1) cols = 1;
    if (cols > TEMP_GRID_MAX_COLS) cols = TEMP_GRID_MAX_COLS;
    int32_t cw = (w - (cols - 1) * UK_S2) / cols;
    for (int k = 0; k < b->made; k++) {
        lv_obj_t *line = b->channels[k].line;
        if (line && lv_obj_is_valid(line) && lv_obj_get_width(line) != cw)
            lv_obj_set_width(line, cw);
    }
}

static void temp_ch_build(temp_blk_t *b, int k)
{
    temp_ch_t *ch = &b->channels[k];
    ch->line = temp_line(b->grid, false);
    ch->name = uk_label(ch->line, UK_FONT_CJK_12, UK_T3, "");
    lv_obj_set_flex_grow(ch->name, 1);
    lv_label_set_long_mode(ch->name, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(ch->name, LV_SIZE_CONTENT);
    (void)temp_value_box(ch->line, &ch->val);
    b->made = k + 1;
}

static void temp_blk_build(int i)
{
    temp_blk_t *b = &s_ui.temp_blk[i];
    memset(b, 0, sizeof *b);
    b->collapsed = true;
    lv_obj_t *box = lv_obj_create(s_ui.temp_pool);
    b->box = box;
    lv_obj_add_event_cb(box, temp_toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    /* 块底一条 1px 下线 + 块内 4px 行距：原来是"每行一条下线、行距 0"，正是"间距过近"。 */
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_side(box, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(box, uk_c(UK_LINE), 0);
    lv_obj_set_style_border_opa(box, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_top(box, UK_S1, 0);
    lv_obj_set_style_pad_bottom(box, UK_S3, 0);      /* 设备之间的留白 */
    lv_obj_set_style_pad_row(box, UK_S1, 0);         /* 通道行之间 */
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = temp_line(box, true);
    b->led  = uk_dot(h, UK_OFF, UK_DOT);
    /* Transparent width slot forwards pointer input to the device block. */
    lv_obj_t *nslot = lv_obj_create(h);
    lv_obj_set_flex_grow(nslot, 1);
    lv_obj_set_height(nslot, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(nslot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nslot, 0, 0);
    lv_obj_set_style_pad_all(nslot, 0, 0);
    lv_obj_set_style_min_width(nslot, 0, 0);
    lv_obj_clear_flag(nslot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* Reserve two text lines: temperature digit-width changes cannot grow the block. */
    b->name = uk_label(nslot, UK_FONT_CJK_12, UK_T1, "");
    lv_obj_set_width(b->name, LV_PCT(100));
    lv_label_set_long_mode(b->name, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(b->name, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(b->name, 2 * lv_font_get_line_height(UK_FONT_CJK_12), 0);
    (void)temp_value_box(h, &b->vmax);
    /* 通道网格：ROW_WRAP + 定宽格子 ⇒ 块窄了自动少列、块宽了自动多列，永远不出容器。
       既是容器就得把 CLICKABLE/SCROLLABLE 清掉，否则展开块的点击与滚动都被它吃掉。 */
    b->grid = lv_obj_create(box);
    lv_obj_set_width(b->grid, LV_PCT(100));
    lv_obj_set_height(b->grid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(b->grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b->grid, 0, 0);
    lv_obj_set_style_pad_all(b->grid, 0, 0);
    lv_obj_set_style_pad_row(b->grid, UK_S1, 0);
    lv_obj_set_style_pad_column(b->grid, UK_S2, 0);
    lv_obj_set_flex_flow(b->grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(b->grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(b->grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    b->more = uk_label(box, UK_FONT_CJK_12, UK_T3, "");
    lv_obj_set_width(b->more, LV_PCT(100));
    lv_obj_set_height(b->more, lv_font_get_line_height(UK_FONT_CJK_12));
    uk_label_ellipsis(b->more);
    lv_obj_add_flag(b->more, LV_OBJ_FLAG_HIDDEN);
}

static void temp_blk_fill(int i)
{
    const temp_grp_t *g = &s_tgrp[i];
    temp_blk_t *b = &s_ui.temp_blk[i];
    const bool on = s_st.online;
    const uint32_t color = on ? uk_temp_color(g->max_c) : UK_T3;
    if (strcmp(b->dev ? b->dev : "", g->dev)) {
        char *dev = uk_alloc(strlen(g->dev) + 1);
        if (!dev) return;
        strcpy(dev, g->dev);
        lv_free(b->dev);
        b->dev = dev;
        s_p4_n = -1;
    }
    if (strcmp(lv_label_get_text(b->name), g->name)) s_p4_n = -1;
    set_txt(b->name, "%s", g->name);
    lv_obj_set_style_text_color(b->name, uk_c(on ? UK_T1 : UK_T3), 0);
    set_num(b->vmax, "%.1f", g->max_c);
    lv_obj_set_style_text_color(b->vmax, uk_c(color), 0);
    lv_obj_set_style_bg_color(b->led, uk_c(color), 0);
    dot_glow(b->led, color, on ? UK_GLOW_OPA : 0);

    int count = b->collapsed || g->inline_ch ? 0 : g->count;
    if (count > b->capacity) {
        temp_ch_t *channels = uk_realloc(b->channels, (size_t)count * sizeof *channels);
        if (channels) { b->channels = channels; b->capacity = count; }
        else count = LV_MIN(count, b->made);
    }
    for (int k = 0; k < count; k++) {
        const fnos_temp_t *t = &s_st.temps[g->first + k];
        if (k >= b->made) temp_ch_build(b, k);
        temp_ch_t *ch = &b->channels[k];
        set_txt(ch->name, "%s", t->ch[0] ? t->ch : "温度");
        set_num(ch->val, "%.1f", t->c);
        lv_obj_set_style_text_color(ch->val, uk_c(on ? uk_temp_color(t->c) : UK_T3), 0);
        show(ch->line, true);
    }
    for (int k = count; k < b->made; k++) show(b->channels[k].line, false);
    if (b->shown_lines != count || b->count != g->count) s_p4_n = -1;
    b->count = g->count;
    b->shown_lines = count;
    if (g->count > 1) {
        /* Always reserve the same summary line: numeric refresh cannot change block height. */
        set_num(b->more, "%d 路 · %.1f-%.1f°C · %s", g->count,
                g->min_c, g->max_c, b->collapsed ? "展开" : "收起");
        show(b->more, true);
        lv_obj_move_to_index(b->more, -1);
    } else show(b->more, false);
    lv_obj_set_style_text_color(b->more, uk_c(UK_T3), 0);
    temp_grid_relayout(b);
}

/* Section geometry is measured from this frame's card content. When the
   viewport is short, section cards also wrap; no fixture ratios or item caps. */
static void inventory_sections(lv_obj_t *container, lv_obj_t **cards,
                               lv_obj_t **pools, const int *counts, int n)
{
    lv_obj_set_height(container,0);
    lv_obj_set_flex_grow(container,1);
    lv_obj_update_layout(container);
    int32_t w=lv_obj_get_content_width(container), h=lv_obj_get_content_height(container);
    if (w<=0 || h<=0) return;
    int32_t gap=lv_obj_get_style_pad_column(container,0);
    int32_t rowgap=lv_obj_get_style_pad_row(container,0);
    int32_t min_sum=0;
    for (int i=0;i<n;i++) {
        min_sum+=2*UK_S2 + lv_font_get_line_height(UK_FONT_CJK_16) +
                 (counts[i] ? UK_ROW_H : lv_font_get_line_height(UK_FONT_CJK_16));
    }
    min_sum+=(n-1)*rowgap;
    int cols=1;
    if (h<min_sum) cols=LV_MIN(n,LV_MAX(1,(w+gap)/(UK_LIST_MIN_WIDTH+2*UK_S3+gap)));
    int rows=(n+cols-1)/cols;
    int32_t *want=uk_alloc((size_t)rows*sizeof *want);
    int32_t *minimum=uk_alloc((size_t)rows*sizeof *minimum);
    if (!want || !minimum) { lv_free(want); lv_free(minimum); return; }
    memset(want,0,(size_t)rows*sizeof *want);
    memset(minimum,0,(size_t)rows*sizeof *minimum);
    for (int i=0;i<n;i++) {
        int32_t cw=(w-gap*(cols-1))/cols;
        if (i==n-1 && n%cols==1) cw=w;
        lv_obj_set_flex_grow(cards[i],0);
        lv_obj_set_width(cards[i],cw);
        lv_obj_update_layout(cards[i]);
        int32_t chrome=lv_obj_get_style_pad_top(cards[i],0)+lv_obj_get_style_pad_bottom(cards[i],0)+
            2*lv_obj_get_style_border_width(cards[i],0)+lv_obj_get_height(lv_obj_get_child(cards[i],0))+
            lv_obj_get_style_pad_row(cards[i],0);
        int32_t content=lv_font_get_line_height(UK_FONT_CJK_16), first=0,last=0;
        int32_t item_min=content;
        if (pools[i] && counts[i]) {
            if (lv_obj_has_flag(pools[i],LV_OBJ_FLAG_USER_2))
                uk_pool_relayout(pools[i],UK_LIST_MIN_WIDTH,false,NULL);
            lv_obj_update_layout(pools[i]);
            int items=(int)lv_obj_get_child_count(pools[i]);
            for (int j=0;j<items;j++) {
                lv_obj_t *item=lv_obj_get_child(pools[i],j);
                lv_area_t a; lv_obj_get_coords(item,&a);
                if (!j || a.y1<first) first=a.y1;
                if (!j || a.y2>last) last=a.y2;
                item_min=LV_MAX(item_min,lv_obj_get_height(item));
            }
            if (items) content=last-first+1;
        }
        int r=i/cols;
        minimum[r]=LV_MAX(minimum[r],chrome+item_min);
        want[r]=LV_MAX(want[r],chrome+content);
    }
    int32_t min_total=(rows-1)*rowgap,desired=(rows-1)*rowgap;
    for (int r=0;r<rows;r++) { min_total+=minimum[r]; desired+=want[r]; }
    int32_t target=LV_MAX(min_total,LV_MIN(h,desired));
    int32_t remaining=target-min_total,extra=desired-min_total;
    for (int i=0;i<n;i++) {
        int r=i/cols;
        int32_t height=minimum[r]+(extra>0 ? (int64_t)remaining*(want[r]-minimum[r])/extra : 0);
        lv_obj_set_height(cards[i],height);
    }
    if (target<h) { lv_obj_set_flex_grow(container,0); lv_obj_set_height(container,target); }
    lv_obj_update_layout(container);
    lv_free(want); lv_free(minimum);
}

static void storage_layout(void)
{
    if (!s_ui.storage_cards || lv_obj_has_flag(s_ui.page[1],LV_OBJ_FLAG_HIDDEN)) return;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *value = s_ui.storage_value[i];
        int32_t width = lv_obj_get_content_width(lv_obj_get_parent(value));
        lv_point_t text;
        lv_text_get_size(&text, lv_label_get_text(value), UK_FONT_NUM_32,
                         0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_set_style_text_font(value, text.x <= width ? UK_FONT_NUM_32 : UK_FONT_NUM_20, 0);
    }
    lv_obj_update_layout(s_ui.page[1]);
    lv_obj_t *cards[]={s_ui.vol_card,s_ui.raid_card,s_ui.disk_card};
    lv_obj_t *pools[]={s_ui.vol_pool,s_ui.raid_pool,s_ui.disk_pool};
    const int counts[]={s_ui.vol_made,s_ui.raid_made,s_ui.disk_made};
    inventory_sections(s_ui.storage_cards,cards,pools,counts,sizeof counts/sizeof counts[0]);
}

static void system_layout(void)
{
    if (!s_ui.p3_cols || lv_obj_has_flag(s_ui.page[3],LV_OBJ_FLAG_HIDDEN) ||
        s_diagnostics || s_pair_open || s_wifi_open) return;
    /* 告警卡不在这两张之列：它整页宽、按内容定高，不参与这一排的等高分栏。 */
    lv_obj_t *cards[]={s_ui.dock_card,s_ui.p3_temp_card};
    lv_obj_t *pools[]={s_ui.dock_pool,s_ui.p3_temp_pool};
    const int counts[]={s_ui.dock_made,s_ui.p3_temp_made};
    inventory_sections(s_ui.p3_cols,cards,pools,counts,sizeof counts/sizeof counts[0]);
}

/* No module entry is the legacy contract. Explicit denied/missing/error must not
   turn an absent reading into a healthy zero in the new overview. */
static const char *overview_module_state(const fnos_status_t *st, const char *name)
{
    for (int i = 0; i < st->nmods; i++)
        if (!strcmp(st->mods[i].name, name)) return st->mods[i].status;
    return NULL;
}

static bool overview_readable(const fnos_status_t *st, const char *module)
{
    const char *state = overview_module_state(st, module);
    return st->ever_ok && (!state || !strcmp(state, "ok") || !strcmp(state, "stale") || !strcmp(state, "partial"));
}

static bool overview_live(const fnos_status_t *st, const char *module)
{
    const char *state = overview_module_state(st, module);
    return st->online && (!state || !strcmp(state, "ok"));
}

/* The HTTP connection and the Docker segment have separate freshness states. */
static const char *docker_module_note(const fnos_status_t *st)
{
    if (!st->ever_ok) return "等待采集";
    if (!st->online) return "离线";
    const char *state = overview_module_state(st, "docker");
    if (!state || !strcmp(state, "ok")) return ""; /* Legacy collector. */
    if (!strcmp(state, "disabled")) return "未启用";
    if (!strcmp(state, "denied")) return "权限不足";
    if (!strcmp(state, "missing")) return "来源不存在";
    if (!strcmp(state, "error")) return "读取失败";
    return mod_status_cn(state); /* Includes stale/partial and future status words. */
}

static void overview_layout(void)
{
    /* 英雄卡自己量一遍高度：时钟字号、副行文案、采样脚注都随视口和数据变，
       写死常数会算漏。右列现在只有内存卡，所以 resources 的 min 就是
       「英雄卡 / 内存卡」取大 —— 这两个数决定首页会不会被页底裁掉。 */
    lv_obj_t *hero = s_ui.overview_resource[0].card;
    if (hero && lv_obj_get_width(hero) > 0) {
        lv_obj_t *hero_body = uk_card_body(hero);
        lv_obj_update_layout(hero);
        int32_t hero_min = lv_obj_get_height(lv_obj_get_child(hero, 0)) +
            lv_obj_get_style_pad_top(hero, 0) + lv_obj_get_style_pad_bottom(hero, 0) +
            2 * lv_obj_get_style_border_width(hero, 0) + lv_obj_get_style_pad_row(hero, 0);
        int visible = 0;
        for (int i = 0; i < (int)lv_obj_get_child_count(hero_body); i++) {
            lv_obj_t *child = lv_obj_get_child(hero_body, i);
            if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
            hero_min += lv_obj_get_style_flex_grow(child, 0) ?
                        lv_obj_get_style_min_height(child, 0) : lv_obj_get_height(child);
            visible++;
        }
        if (visible > 1) hero_min += (visible - 1) * lv_obj_get_style_pad_row(hero_body, 0);
        lv_obj_set_style_min_height(hero, hero_min, 0);
        int32_t minimum = LV_MAX(hero_min,
            lv_obj_get_style_min_height(s_ui.overview_resource[1].card, 0));
        lv_obj_t *resources = lv_obj_get_parent(hero);
        lv_obj_set_style_min_height(resources, minimum, 0);
        lv_obj_set_style_min_height(lv_obj_get_parent(resources), minimum, 0);
    }

}

static void overview_refresh(const fnos_status_t *st)
{
    static const char *const modules[] = { "cpu", "mem" };
    char b[160], c1[32], c2[32];
    for (int i = 0; i < 2; i++) {
        overview_resource_t *r = &s_ui.overview_resource[i];
        const char *state = overview_module_state(st, modules[i]);
        bool readable = overview_readable(st, modules[i]);
        bool live = readable && overview_live(st, modules[i]);
        float pct = i == 0 ? st->cpu.pct : st->mem.pct;
        (live ? set_num : set_txt)(r->value, readable ? "%.0f" : "--", pct);
        set_txt(r->unit, "%s", readable ? "%" : "");
        if (!readable) (live ? set_num : set_txt)(r->meta, "%s", st->ever_ok && state ? mod_status_cn(state) : "等待数据");
        else if (i == 0) (live ? set_num : set_txt)(r->meta, "%d 核 · 负载 %.2f · 队列 %d", st->cpu.cores, st->cpu.load1, st->cpu.runq);
        else (live ? set_num : set_txt)(r->meta, "%.1f / %.1f GB · 交换 %.1fG", st->mem.used_mb / 1024.f, st->mem.total_mb / 1024.f, st->mem.swap_used_mb / 1024.f);
        lv_obj_set_style_text_color(r->value, uk_c(live ? UK_T1 : UK_T3), 0);

        show(r->chart, readable);
        lv_obj_set_style_opa(r->chart, live ? LV_OPA_COVER : LV_OPA_40, 0);
        if (!readable) (live ? set_num : set_txt)(r->context, "无有效读数");
        else if (!chart_sample_count(r->chart)) (live ? set_num : set_txt)(r->context, "等待历史采样");
        else (live ? set_num : set_txt)(r->context, "%s%d 次采集 · 峰 %d%%", live ? "" : "旧历史 · ", chart_sample_count(r->chart), chart_peak(r->chart, 0));
    }

    const bool system_live = overview_live(st, "cpu");
    if (overview_readable(st, "cpu")) {
        const uint32_t hours = st->uptime_s / 3600u;
        (system_live ? set_num : set_txt)(s_ui.overview_clock, "%02u:%02u", (unsigned)(hours % 24u),
                (unsigned)(st->uptime_s / 60u % 60u));
        set_txt(s_ui.overview_clock_note, "%s运行 %s", system_live ? "" : "旧快照 · ",
                fmt_uptime(b, sizeof b, st->uptime_s));
    } else {
        set_num(s_ui.overview_clock, "--:--");
        set_txt(s_ui.overview_clock_note, "运行时长不可用");
    }
    lv_obj_set_style_text_color(s_ui.overview_clock, uk_c(system_live ? UK_T1 : UK_T3), 0);

    overview_layout();

    bool net_readable = overview_readable(st, "net");
    bool net_live = net_readable && overview_live(st, "net");
    for (int i = 0; i < 2; i++) {
        const char *v, *unit;
        snprintf(b, sizeof b, "%s", fmt_rate(c1, sizeof c1, i == 0 ? st->net.rx_kbs : st->net.tx_kbs));
        rate_split(b, &v, &unit);
        (net_live ? set_num : set_txt)(s_ui.overview_rate[i], "%s", net_readable ? v : "--");
        set_txt(s_ui.overview_unit[i], "%s", net_readable ? unit : "");
        lv_obj_set_style_text_color(s_ui.overview_rate[i], uk_c(net_live ? UK_T1 : UK_T3), 0);
    }
    show(s_ui.overview_net, net_readable);
    lv_obj_set_style_opa(s_ui.overview_net, net_live ? LV_OPA_COVER : LV_OPA_40, 0);
    if (!net_readable) {
        const char *state = overview_module_state(st, "net");
        set_num(s_ui.overview_net_context, "%s", st->ever_ok && state ? mod_status_cn(state) : "等待网络数据");
    } else {
        float top = chart_peak(s_ui.overview_net, 0);
        if (top < chart_peak(s_ui.overview_net, 1)) top = chart_peak(s_ui.overview_net, 1);
        if (top < st->net.rx_kbs) top = st->net.rx_kbs;
        if (top < st->net.tx_kbs) top = st->net.tx_kbs;
        top = top > 0 ? top * 1.2f : 1;  /* Zero is valid; never collapse the scale. */
        int32_t ymax = (int32_t)top;
        if ((float)ymax < top) ymax++;
        uk_trend_set_range(s_ui.overview_net, 0, ymax);
        if (!net_live) set_num(s_ui.overview_net_context, "旧历史 · 暂停更新");
        else if (!chart_sample_count(s_ui.overview_net)) set_num(s_ui.overview_net_context, "等待历史采样");
        else set_num(s_ui.overview_net_context, "%d 次采集 · 0-%s", chart_sample_count(s_ui.overview_net), fmt_rate(b, sizeof b, ymax));
    }

    float used = 0, total = 0, free_gb = 0;
    int healthy_raid = 0;
    for (int i = 0; i < st->nvols; i++) {
        used += st->vols[i].used_gb; total += st->vols[i].total_gb; free_gb += st->vols[i].free_gb;
    }
    for (int i = 0; i < st->nraid; i++) if (st->raid[i].ok) healthy_raid++;
    bool storage = overview_readable(st, "vols") && st->nvols > 0 && total > 0;
    bool storage_live = storage && overview_live(st, "vols");
    (storage_live ? set_num : set_txt)(s_ui.capacity, "%s", storage ? fmt_cap(b, sizeof b, used) : "--");
    lv_obj_set_style_text_color(s_ui.capacity, uk_c(storage_live ? UK_T1 : UK_T3), 0);
    if (storage) set_num(s_ui.capacity_detail, "已用 / 总计 %s · 可用 %s", fmt_cap(c1, sizeof c1, total), fmt_cap(c2, sizeof c2, free_gb));
    else set_num(s_ui.capacity_detail, "%s", st->ever_ok ? "没有可用容量数据" : "等待存储数据");
    show(s_ui.capacity_bar, storage);
    uk_bar_set(s_ui.capacity_bar, total > 0 ? (int32_t)(used / total * 100) : 0);
    lv_obj_set_style_bg_color(s_ui.capacity_bar, uk_c(storage_live ? UK_S_CPU : UK_OFF), LV_PART_INDICATOR);
    int count = storage ? st->nvols : 0;
    if (count > s_ui.overview_volume_capacity) {
        overview_volume_t *grown = uk_realloc(s_ui.overview_volume, (size_t)count * sizeof *grown);
        if (!grown) return;
        memset(grown + s_ui.overview_volume_capacity, 0,
               (count - s_ui.overview_volume_capacity) * sizeof *grown);
        s_ui.overview_volume = grown;
        s_ui.overview_volume_capacity = count;
    }
    for (int i = 0; i < count; i++) {
        overview_volume_t *row = &s_ui.overview_volume[i];
        if (!row->box) {
            row->box = uk_col(s_ui.overview_volumes, 0);
            lv_obj_set_width(row->box, LV_PCT(100));
            lv_obj_set_style_pad_row(row->box, UK_S1, 0);
            lv_obj_t *line = uk_row_box(row->box, 0);
            row->name = uk_label(line, UK_FONT_CJK_12, UK_T3, "");
            lv_obj_set_flex_grow(row->name, 1);
            lv_obj_set_height(row->name, lv_font_get_line_height(UK_FONT_CJK_12));
            row->value = uk_label(line, UK_FONT_CJK_12, UK_T2, "");
            row->bar = hero_bar(row->box);
            lv_obj_set_height(row->bar, UK_BAR_H);
        }
        const fnos_vol_t *vol = &st->vols[i];
        set_txt(row->name, "%s", vol->mnt);
        (storage_live ? set_num : set_txt)(row->value, vol->total_gb > 0 ? "%.0f%%" : "--", vol->pct);
        show(row->bar, vol->total_gb > 0);
        uk_bar_set(row->bar, (int32_t)vol->pct);
        lv_obj_set_style_bg_color(row->bar, uk_c(storage_live ? UK_S_CPU : UK_OFF), LV_PART_INDICATOR);
        lv_obj_set_style_text_color(row->value, uk_c(storage_live ? uk_pct_color((int32_t)vol->pct) : UK_T3), 0);
        not_clickable(row->box);
    }
    for (int i = count; i < s_ui.overview_volume_made; i++) {
        lv_obj_delete(s_ui.overview_volume[i].box);
        memset(&s_ui.overview_volume[i], 0, sizeof s_ui.overview_volume[i]);
    }
    s_ui.overview_volume_made = count;
    /* 卷清单超出一屏时，/volN 的半行不再挂在卡片下沿。 */
    uk_viewport_snap(s_ui.overview_volumes, UK_S1);
    if (!storage) set_txt(s_ui.storage_note, "各卷详情在存储页");
    else if (!storage_live) set_txt(s_ui.storage_note, "旧快照 · 阵列状态未知");
    else if (!overview_readable(st, "raid")) set_txt(s_ui.storage_note, "%d 个卷 · 阵列不可用", st->nvols);
    else if (!overview_live(st, "raid")) set_txt(s_ui.storage_note, "%d 个卷 · 阵列旧值 %d/%d", st->nvols, healthy_raid, st->nraid);
    else if (!st->nraid) set_txt(s_ui.storage_note, "%d 个卷 · 未发现阵列", st->nvols);
    else set_txt(s_ui.storage_note, "%d 个卷 · 阵列正常 %d/%d", st->nvols, healthy_raid, st->nraid);
}

static void header_refresh(void)
{
    const fnos_status_t *st = &s_st;
    char age[32];
    if (!st->ever_ok) set_txt(s_ui.h_ep, "%s · 等待采集端", NAV_TXT[s_page]);
    else if (!st->online) set_txt(s_ui.h_ep, "%s · 保留旧数据 · %s", NAV_TXT[s_page], fmt_age(age, sizeof age, data_age_ms(st)));
    else set_txt(s_ui.h_ep, "%s · 每秒更新", NAV_TXT[s_page]);
}

static void inventory_notice(void)
{
    if (!uk_alloc_failed()) return;
    lv_label_set_text_static(s_ui.foot_txt, "界面内存不足 · 部分设备尚未显示");
    lv_obj_set_style_text_color(s_ui.foot_txt, uk_c(UK_WARN), 0);
    lv_obj_set_style_bg_color(s_ui.foot_dot, uk_c(UK_WARN), 0);
}

/* 告警页健康态的"体检"四行（见 build_p5 的注释）。所有数字都来自当前快照：
   某一段读不到就写它的状态词 + 黄点 —— 绝不静默显示成 0，因为 0 的含义是
   "看过了、确实是 0"。 */
static void alert_health_sync(const fnos_status_t *st)
{
    if (!s_ui.alert_health) return;
    const bool on = st->ever_ok && st->online && st->nalerts == 0;
    /* "暂无告警事件"那句：体检块露面时收成一行让位，单独出现时仍整卡居中。 */
    if (s_ui.alert_page_empty) lv_obj_set_flex_grow(s_ui.alert_page_empty, on ? 0 : 1);
    show(s_ui.alert_health, on);
    if (!on) return;

    char v0[96], v1[96], v2[64], v3[96], age[32];
    const char *txt[4];
    bool warn[4] = { false, false, false, false };
    const char *state;

    /* ① 存储容量：卷数 · 占用最高的卷 · 阵列健康数（与首页存储磁贴同一套判据） */
    state = overview_module_state(st, "vols");
    if (!overview_readable(st, "vols")) {
        snprintf(v0, sizeof v0, "%s", state ? mod_status_cn(state) : "读不到");
        warn[0] = true;
    } else if (st->nvols <= 0) {
        snprintf(v0, sizeof v0, "未发现卷");
        warn[0] = true;
    } else {
        int worst = 0, healthy = 0;
        for (int i = 1; i < st->nvols; i++) if (st->vols[i].pct > st->vols[worst].pct) worst = i;
        for (int i = 0; i < st->nraid; i++) if (st->raid[i].ok) healthy++;
        if (!overview_readable(st, "raid"))
            snprintf(v0, sizeof v0, "%d 个卷 · 最高 %s %.0f%% · 阵列读不到",
                     st->nvols, st->vols[worst].mnt, (double)st->vols[worst].pct);
        else
            snprintf(v0, sizeof v0, "%d 个卷 · 最高 %s %.0f%% · 阵列 %d/%d 正常",
                     st->nvols, st->vols[worst].mnt, (double)st->vols[worst].pct, healthy, st->nraid);
        warn[0] = !overview_readable(st, "raid") || healthy < st->nraid || !overview_live(st, "vols");
    }

    /* ② 温度：路数 · 最热 · 关注路数（阈值与温度页同一对宏，不另立标准） */
    state = overview_module_state(st, "temps");
    if (!overview_readable(st, "temps") && !overview_readable(st, "cpu")) {
        snprintf(v1, sizeof v1, "%s", state ? mod_status_cn(state) : "读不到");
        warn[1] = true;
    } else {
        float hot = 0;
        int warm = 0;
        for (int i = 0; i < st->ntemps; i++) {
            if (st->temps[i].c > hot) hot = st->temps[i].c;
            if (st->temps[i].c >= UK_TEMP_WARM) warm++;
        }
        if (hot <= 0) hot = st->cpu.temp_c;
        if (hot <= 0) {
            snprintf(v1, sizeof v1, "读不到");
            warn[1] = true;
        } else {
            const bool old = !overview_live(st, "cpu") || !overview_live(st, "temps");
            if (warm > 0)
                snprintf(v1, sizeof v1, "%s%d 路 · 最热 %.0f°C · ≥%.0f°C 关注 %d 路",
                         old ? "旧值 · " : "", st->ntemps, (double)hot, (double)UK_TEMP_WARM, warm);
            else
                snprintf(v1, sizeof v1, "%s%d 路 · 最热 %.0f°C · 无超限通道",
                         old ? "旧值 · " : "", st->ntemps, (double)hot);
            warn[1] = old || warm > 0;
        }
    }

    /* ③ 容器：运行数 / 总数 */
    state = overview_module_state(st, "docker");
    if (!overview_readable(st, "docker")) {
        snprintf(v2, sizeof v2, "%s", state ? mod_status_cn(state) : "读不到");
        warn[2] = true;
    } else if (st->ndocker <= 0) {
        snprintf(v2, sizeof v2, "未发现容器");
    } else {
        int up = 0;
        for (int i = 0; i < st->ndocker; i++) if (st->docker[i].up) up++;
        snprintf(v2, sizeof v2, "%s%d/%d 运行", overview_live(st, "docker") ? "" : "旧值 · ", up, st->ndocker);
        warn[2] = up < st->ndocker || !overview_live(st, "docker");
    }

    /* ④ 采集：这条链路自己的状态 —— 成功/失败次数 + 数据新鲜度 */
    snprintf(v3, sizeof v3, "轮询 %u 次 · 失败 %u 次 · 数据 %s",
             (unsigned)st->ok_count, (unsigned)st->fail_count, fmt_age(age, sizeof age, data_age_ms(st)));
    warn[3] = st->last_status != 200;

    txt[0] = v0; txt[1] = v1; txt[2] = v2; txt[3] = v3;
    for (int i = 0; i < 4; i++) {
        set_txt(s_ui.alert_health_val[i], "%s", txt[i]);
        lv_obj_set_style_bg_color(s_ui.alert_health_dot[i], uk_c(warn[i] ? UK_WARN : UK_OK), 0);
    }
}

static void refresh(void)
{
    const fnos_status_t *st = &s_st;
    char b[160], c1[32], c2[32];

    uk_number_motion_enable(st->online);

    /* 顶栏：主机 / 端点 / 状态胶囊 / 信号 */
    set_txt(s_ui.h_host, "%s", st->host[0] ? st->host : "fnos");
    header_refresh();
    if (!st->ever_ok)      chip_set_glow(s_ui.h_chip, "等待数据", UK_T4);
    else if (st->online)   chip_set_glow(s_ui.h_chip, "在线", UK_OK);
    else                   chip_set_glow(s_ui.h_chip, "离线", UK_WARN);
    signal_set(fnos_net_rssi());

    /* 底栏：左边是轮询统计，右边是"最严重的一条告警"。底栏只说**一件事**：
       现在要不要动手；全量告警在系统页那一列里。 */
    if (s_ui.foot_poll)
        set_num(s_ui.foot_poll, "轮询 1s · ok %u · fail %u",
                (unsigned)st->ok_count, (unsigned)st->fail_count);
    if (s_ui.foot_txt && s_ui.foot_dot) {
        uint32_t band = UK_OK;
        int first = -1, rank = -1;
        for (int i = 0; i < st->nalerts; i++) {
            int r = !strcmp(st->alerts[i].lv, "crit") ? 2 : !strcmp(st->alerts[i].lv, "warn") ? 1 : 0;
            if (r > rank) { rank = r; first = i; }
        }
        if (!st->ever_ok) {
            band = UK_T3; set_txt(s_ui.foot_txt, "等待采集端…");
        } else if (!st->online) {
            band = UK_WARN;
            if (first >= 0) set_txt(s_ui.foot_txt, "采集端离线 · 上次事件：%s", st->alerts[first].m);
            else set_txt(s_ui.foot_txt, "告警状态未知 · 保留旧数据");
        } else if (first >= 0) {
            band = rank == 2 ? UK_DANGER : rank == 1 ? UK_WARN : UK_T3;
            set_txt(s_ui.foot_txt, "%s · 共 %d 个事件", st->alerts[first].m, st->nalerts);
        } else if (st->source_dropped) { band = UK_WARN; set_txt(s_ui.foot_txt, "采集端省略 %d 项 · 请检查采集限制", st->source_dropped); }
        else set_txt(s_ui.foot_txt, "无告警事件 · 采集正常");
        lv_obj_set_style_bg_color(s_ui.foot_dot, uk_c(band), 0);
        lv_obj_set_style_shadow_color(s_ui.foot_dot, uk_c(band), 0);
        lv_obj_set_style_shadow_width(s_ui.foot_dot, UK_GLOW_W, 0);
        lv_obj_set_style_shadow_opa(s_ui.foot_dot, band == UK_OK ? 0 : UK_GLOW_OPA, 0);
        lv_obj_set_style_text_color(s_ui.foot_txt,
                                    uk_c(band == UK_OK ? UK_T3 : band), 0);
        /* 只有严重告警才呼吸：注意/信息级别常驻亮着就够，闪起来反而像故障灯。 */
        bool pulse = (band == UK_DANGER);
        if (pulse != s_pulse_on) {
            uk_anim_pulse(s_ui.foot_dot, pulse);
            s_pulse_on = pulse;
        }
    }

    /* 健康结论先区分可信度，再报告采集器的告警。 */
    int critical = 0, warnings = 0, running = 0;
    for (int i = 0; i < st->nalerts; i++) {
        if (strcmp(st->alerts[i].lv, "crit") == 0) critical++;
        else if (strcmp(st->alerts[i].lv, "warn") == 0) warnings++;
    }
    for (int i = 0; i < st->ndocker; i++) if (st->docker[i].up) running++;
    uint32_t health_color = !st->ever_ok ? UK_T3 : !st->online ? UK_WARN : critical ? UK_DANGER : warnings ? UK_WARN : UK_OK;
    const char *health_note = !st->ever_ok ? "等待数据" : !st->online ? "采集端离线" : critical ? "存在严重告警" : warnings ? "需要关注" : "无采集告警";
    set_txt(s_ui.health, "%s", health_note);
    set_txt(s_ui.system_state, "%s", health_note);
    lv_obj_set_style_text_color(s_ui.health, uk_c(health_color), 0);
    /* 首页结论带：点是状态灯，正文是最要紧的一件事，右侧"全部 →"只在有事件时出现。
       文案与底栏那条同源，不重复算逻辑。 */
    if (s_ui.home_band_dot) {
        lv_obj_set_style_bg_color(s_ui.home_band_dot, uk_c(health_color), 0);
        lv_obj_set_style_shadow_color(s_ui.home_band_dot, uk_c(health_color), 0);
        lv_obj_set_style_shadow_width(s_ui.home_band_dot, UK_GLOW_W, 0);
        lv_obj_set_style_shadow_opa(s_ui.home_band_dot, health_color == UK_OK ? 0 : UK_GLOW_OPA, 0);
        if (st->nalerts > 0) {
            set_txt(s_ui.home_band_msg, "%s", st->alerts[0].m);
            show(s_ui.home_band_more, true);
        } else if (!st->ever_ok) {
            set_txt(s_ui.home_band_msg, "%s", "等待采集端首次采集");
            show(s_ui.home_band_more, false);
        } else if (!st->online) {
            set_txt(s_ui.home_band_msg, "%s", "采集端离线 · 显示最后一次采集快照");
            show(s_ui.home_band_more, false);
        } else {
            /* 别把带 %d 的文案塞进 set_txt 的 "%s" 里当**实参**：%s 会原样印出
               "容器 %d/%d 运行"（实机 2026-10-08 抓到），必须让格式串当格式串用。 */
            set_txt(s_ui.home_band_msg, "容器 %d/%d 运行 · 无待处理事件", running, st->ndocker);
            show(s_ui.home_band_more, false);
        }
    }
    /* 健康卡右侧那行摘要同时是"为什么连不上"的公告栏：在线时报告运行概况，
       离线时报告原因（见 link_reason）——比再来一行"离线"有用得多。 */
    const char *why = st->online ? NULL : link_reason(st);

    /* P0: current readings and histories use the same snapshot/cursor as detail pages. */
    overview_refresh(st);

    float used = 0, total = 0, free_gb = 0;
    for (int i = 0; i < st->nvols; i++) {
        used += st->vols[i].used_gb;
        total += st->vols[i].total_gb;
        free_gb += st->vols[i].free_gb;
    }
    float capacities[] = { used, total, free_gb };
    for (int i = 0; i < 3; i++)
        set_num(s_ui.storage_value[i], "%s", st->ever_ok && st->nvols ? fmt_cap(b, sizeof b, capacities[i]) : "-");
    /* P1 的容量条与上面的三栏数字同源：一眼看出"还剩多少"。 */
    if (s_ui.storage_bar) {
        int32_t pct = (st->ever_ok && st->nvols && total > 0) ? (int32_t)(used / total * 100.0f + 0.5f) : 0;
        uk_bar_set(s_ui.storage_bar, pct);
        lv_obj_set_style_bg_color(s_ui.storage_bar, uk_c(uk_pct_color((float)pct)), LV_PART_INDICATOR);
    }
    if (!st->online) {
        set_txt(s_ui.overview_note, "%s", why ? why : "离线，显示最后一次采集快照");
        set_num(s_ui.home_temp_value, "--");
        set_txt(s_ui.home_temp_unit, "");
        set_txt(s_ui.home_temp_name, "%s", "离线 · 显示上次快照");
        lv_obj_set_style_bg_color(s_ui.home_temp_dot, uk_c(UK_T3), 0);
        lv_obj_set_style_text_color(s_ui.home_temp_value, uk_c(UK_T3), 0);
    } else {
        float hottest = st->cpu.temp_c;
        bool have = hottest > 0 && overview_readable(st, "cpu");
        bool old_temp = !overview_live(st, "cpu") || !overview_live(st, "temps");
        char sensor[64] = "CPU";
        for (int i = 0; overview_readable(st, "temps") && i < st->ntemps; i++) {
            const fnos_temp_t *t = &st->temps[i];
            if (have && t->c <= hottest) continue;
            hottest = t->c;
            have = true;
            snprintf(sensor, sizeof sensor, "%s%s%s", t->dn[0] ? t->dn : t->dev, t->ch[0] ? " · " : "", t->ch);
        }
        char containers[64];
        if (!overview_readable(st, "docker")) snprintf(containers, sizeof containers, "容器不可用");
        else snprintf(containers, sizeof containers, "%s容器 %d/%d", overview_live(st, "docker") ? "" : "旧值 · ", running, st->ndocker);
        /* 脚注只留"行内看不到"的两件事：温度读数与容器状态。传感器名与温度都已经在
           同一张磁贴的数值行里（home_temp_name / home_temp_value），再抄一遍就会把
           "容器 4/4" 挤到磁贴外（1/3 页宽只放得下 ~24 个 CJK12 字，DOTS 截断）。 */
        if (have) set_txt(s_ui.overview_note, "%s %.0f°C · %s", old_temp ? "温度旧值" : "最高", hottest, containers);
        else set_txt(s_ui.overview_note, "温度不可用 · %s · 运行 %s", containers, fmt_uptime(b, sizeof b, st->uptime_s));
        /* 首页"最热温度"磁贴：与上面同一次扫描，值按温度上色，点进去看全部通道。 */
        if (have) {
            (old_temp ? set_txt : set_num)(s_ui.home_temp_value, "%.0f", hottest);
            set_txt(s_ui.home_temp_unit, "°C");
            set_txt(s_ui.home_temp_name, "%s", sensor);
            lv_obj_set_style_text_color(s_ui.home_temp_value, uk_c(old_temp ? UK_T3 : UK_T1), 0);
            lv_obj_set_style_bg_color(s_ui.home_temp_dot, uk_c(uk_temp_color(hottest)), 0);
        } else {
            set_num(s_ui.home_temp_value, "--");
            set_txt(s_ui.home_temp_unit, "");
            set_txt(s_ui.home_temp_name, "%s", containers);
            lv_obj_set_style_bg_color(s_ui.home_temp_dot, uk_c(UK_T3), 0);
        }
    }
    {
        char nb[64];
        set_txt(s_ui.net_title, "网络吞吐 · %s", hist_span_text(nb, sizeof nb, st));
    }
    /* P1：卷 / 阵列 / 硬盘。行按需建、**多出来的销毁**（不能只 HIDDEN，理由见
       row_trim 的注释）；池的重排只在"条数或池尺寸变了"时才发生（pool_layout 判脏）。 */
    int nvol = st->nvols;
    nvol = rows_sync(&s_ui.vol_row, &s_ui.vol_made, nvol, s_ui.vol_pool, false, true);
    for (int i = 0; i < nvol; i++) {
        uk_row_t *row = s_ui.vol_row[i];
        if (!row) continue;
        const fnos_vol_t *v = &st->vols[i];
        /* 卷行副行带上文件系统（数据层一直有 vols[].fs，界面此前没用）：
           "已用 2.0 TB / 总计 3.6 TB · ext4"。 */
        snprintf(b, sizeof b, "已用 %s / 总计 %s · %s",
                 fmt_cap(c1, sizeof c1, v->used_gb), fmt_cap(c2, sizeof c2, v->total_gb),
                 v->fs[0] ? v->fs : "-");
        snprintf(c1, sizeof c1, "%.0f%%", v->pct);
        /* 条色/数字色都走配置后的 uk_pct_color，
           与旧实现的 80/90 不一致是**有意改的**：全项目只能有一套阈值。
           百分号写在数值里（val = "54%"）而不是拆到 unit：预览会断言
           "54%" 这个**整串**在屏幕上，拆成两个 label 就找不到了。 */
        uk_row_set(row, v->mnt, " ", c1, NULL, (int32_t)v->pct, 0);
        set_txt(row->name2,"已用 %s / 总计 %s · %s",fmt_cap(c1,sizeof c1,v->used_gb),
                fmt_cap(c2,sizeof c2,v->total_gb),v->fs[0] ? v->fs : "-");
        lv_obj_set_style_text_color(row->val, uk_c(uk_pct_color((int32_t)v->pct)), 0);
    }
    pool_layout(&s_p1_vol_n, &s_p1_vol_w, &s_p1_vol_h, nvol, s_ui.vol_pool, UK_LIST_MIN_WIDTH, false, NULL);

    int nraid = st->nraid;
    nraid = rows_sync(&s_ui.raid_row, &s_ui.raid_made, nraid, s_ui.raid_pool, true, false);
    for (int i = 0; i < nraid; i++) {
        uk_row_t *row = s_ui.raid_row[i];
        if (!row) continue;
        const fnos_raid_t *r = &st->raid[i];
        snprintf(b, sizeof b, "%s · %d/%d · %s", r->lvl, r->have, r->want, r->state);
        bool syncing = r->sync_pct < 100 && (strstr(r->state, "sync") || strstr(r->state, "recover") || strstr(r->state, "reshape"));
        uint32_t col = r->ok ? UK_OK : syncing ? UK_WARN : UK_DANGER;
        const char *vtxt = r->ok ? "正常" : "降级";
        if (syncing) {
            snprintf(c1, sizeof c1, "%.0f%%", r->sync_pct);
            vtxt = c1;
            col = UK_WARN;
        }
        uk_row_set(row,r->dev," ",vtxt,NULL,-1,col);
        set_txt(row->name2,"%s · %d/%d · %s",r->lvl,r->have,r->want,r->state);
        /* 右侧那列默认是等宽数字字体；写中文（正常/降级）时必须换回 CJK，
           否则预览直接报缺字（num 字体里没有汉字）。 */
        if (!syncing) lv_obj_set_style_text_font(row->val, UK_FONT_CJK_16, 0);
        else          lv_obj_set_style_text_font(row->val, UK_FONT_NUM_20, 0);
        lv_obj_set_style_text_color(row->val, uk_c(col), 0);
    }
    pool_layout(&s_p1_raid_n, &s_p1_raid_w, &s_p1_raid_h, nraid, s_ui.raid_pool, UK_LIST_MIN_WIDTH, false, NULL);

    int ndisk = st->ndisks;
    ndisk = rows_sync(&s_ui.disk_row, &s_ui.disk_made, ndisk, s_ui.disk_pool, false, false);
    for (int i = 0; i < ndisk; i++) {
        uk_row_t *row = s_ui.disk_row[i];
        if (!row) continue;
        const fnos_disk_t *d = &st->disks[i];
        uk_row_set(row, d->dev,
                   (snprintf(b, sizeof b, "读 %s · 写 %s",
                             fmt_rate(c1, sizeof c1, d->rd_kbs),
                             fmt_rate(c2, sizeof c2, d->wr_kbs)), b),
                   NULL, NULL, -1, st->online ? UK_OK : UK_OFF);
    }
    pool_layout(&s_p1_disk_n, &s_p1_disk_w, &s_p1_disk_h, ndisk, s_ui.disk_pool, UK_LIST_MIN_WIDTH, false, NULL);

    /* ── P2：网络 ─────────────────────────────────────────────────────
       4 张速率 KPI + 吞吐趋势 + 网卡信息条。数值与单位分给两个 label
       （uk_kpi 的 val/unit），单位才会小一号。 */
    {
        char nb[32], sb[64];
        const char *v, *u;

        snprintf(nb, sizeof nb, "%s", fmt_rate(b, sizeof b, st->net.tx_kbs));
        rate_split(nb, &v, &u);
        snprintf(sb, sizeof sb, "累计 %s", fmt_cap(c1, sizeof c1, st->net.tx_total_gb));
        uk_kpi_set(s_ui.kpi2[0], v, u, sb, -1);

        snprintf(nb, sizeof nb, "%s", fmt_rate(b, sizeof b, st->net.rx_kbs));
        rate_split(nb, &v, &u);
        snprintf(sb, sizeof sb, "累计 %s", fmt_cap(c1, sizeof c1, st->net.rx_total_gb));
        uk_kpi_set(s_ui.kpi2[1], v, u, sb, -1);

        snprintf(nb, sizeof nb, "%s", fmt_rate(b, sizeof b, st->net.rx_kbs + st->net.tx_kbs));
        rate_split(nb, &v, &u);
        uk_kpi_set(s_ui.kpi2[2], v, u, st->net.ifname[0] ? st->net.ifname : "—", -1);

        snprintf(nb, sizeof nb, "%d ms", st->http_ms);
        rate_split(nb, &v, &u);
        snprintf(sb, sizeof sb, "轮询 %u · 失败 %u", (unsigned)st->ok_count, (unsigned)st->fail_count);
        uk_kpi_set(s_ui.kpi2[3], v, u, sb, -1);
    }

    /* 两条曲线共用一个上界（各画各的量程就没法比大小）。峰值取自**图内实际数据**，
       不是历史最大：断流重连后旧的尖峰不会再压扁新数据。 */
    {
        int32_t peak_tx = chart_peak(s_ui.tr_net, 0);
        int32_t peak_rx = chart_peak(s_ui.tr_net, 1);
        float top = (peak_rx > peak_tx ? peak_rx : peak_tx) * 1.2f;
        if (top < st->net.rx_kbs * 1.2f) top = st->net.rx_kbs * 1.2f;
        if (top < st->net.tx_kbs * 1.2f) top = st->net.tx_kbs * 1.2f;
        if (top < 20) top = 20;
        uk_trend_set_range(s_ui.tr_net, 0, (int32_t)top);
        set_num(s_ui.net_axis, "量程 0 - %s", fmt_rate(b, sizeof b, top));
        set_num(s_ui.tr_tx_lbl, "%s · 峰 %s",
                fmt_rate(c1, sizeof c1, st->net.tx_kbs), fmt_rate(c2, sizeof c2, peak_tx));
        set_num(s_ui.tr_rx_lbl, "%s · 峰 %s",
                fmt_rate(c1, sizeof c1, st->net.rx_kbs), fmt_rate(c2, sizeof c2, peak_rx));
    }

    /* Older collectors supply one selected interface. New frames supply all
       ports, bonds, VLANs and virtual interfaces without a fixed row count. */
    bool net_readable = overview_readable(st, "net");
    bool net_live = net_readable && overview_live(st, "net");
    int nnets = net_readable ? (st->nnets ? st->nnets : (st->net.ifname[0] ? 1 : 0)) : 0;
    nnets = rows_sync(&s_ui.net_rows, &s_ui.net_made, nnets, s_ui.net_pool, true, false);
    for (int i = 0; i < nnets; i++) {
        const fnos_netif_t *net = st->nnets ? &st->nets[i] : &st->net;
        uk_row_t *row = s_ui.net_rows[i];
        if (net->state[0] && net->speed_mbps>0)
            snprintf(b,sizeof b,"%s · %s · %d Mbps",net->ifname,net->state,net->speed_mbps);
        else if (net->state[0]) snprintf(b,sizeof b,"%s · %s",net->ifname,net->state);
        else snprintf(b,sizeof b,"%s",net->ifname);
        uint32_t led = net_live && (!net->state[0] || !strcmp(net->state,"up")) ? UK_OK : UK_T3;
        /* Reset on identity/link changes; retain the presented rate on ordinary refresh. */
        if (strcmp(lv_label_get_text(row->name1),b))
            uk_row_set(row,b,"","","",-1,led);
        else if (row->led) lv_obj_set_style_bg_color(row->led,uk_c(led),0);
        (net_live ? set_num : set_txt)(row->name2, "%s下行 %s · 上行 %s", net_live ? "" : "上次 · ", fmt_rate(c1,sizeof c1,net->rx_kbs),
                                                  fmt_rate(c2,sizeof c2,net->tx_kbs));
        show(lv_obj_get_parent(row->name2),true);
        lv_obj_set_style_text_color(row->name2,uk_c(net_live ? UK_T2 : UK_T3),0);
    }
    show(s_ui.net_card, nnets>0);
    uk_pool_relayout(s_ui.net_pool, UK_LIST_MIN_WIDTH, false, NULL);

    const char *dock_state = overview_module_state(st, "docker");
    const char *dock_note = docker_module_note(st);
    bool dock_live = st->ever_ok && overview_live(st, "docker");
    bool dock_partial = st->online && dock_state && !strcmp(dock_state, "partial");
    int ndock = st->ever_ok ? st->ndocker : 0;
    char dock_summary[96];
    if (dock_live) {
        if (ndock > 0) snprintf(dock_summary, sizeof dock_summary, "容器 %d/%d 运行", running, ndock);
        else snprintf(dock_summary, sizeof dock_summary, "暂无容器");
    } else if (ndock > 0 && overview_readable(st, "docker")) {
        snprintf(dock_summary, sizeof dock_summary, "容器%s · 上次 %d/%d 运行", dock_note, running, ndock);
    } else {
        snprintf(dock_summary, sizeof dock_summary, "容器%s", dock_note);
    }
    if (st->online) set_txt(s_ui.system_note, "%s · %d 路温度 · 严重 %d · 警告 %d", dock_summary, st->ntemps, critical, warnings);
    else set_txt(s_ui.system_note, "旧快照 · %s · %d 路温度 · %d 个上次事件", dock_summary, st->ntemps, st->nalerts);
    if (dock_live) set_txt(s_ui.dock_empty_label, "暂无容器");
    else set_txt(s_ui.dock_empty_label, "容器%s", dock_note);
    /* 数据层早就有、v7 界面一直没用上的字段：负载 / 进程数 / 交换 / 运行时长 */
    /* 文案长度受盒宽约束（892px，cjk_12）：40 个中文字 ≈ 890px 就到顶。
       "0.1 / 2.0 GB" 写成 "0.1/2.0G" 省 60px，否则会折行、第二行被卡片裁掉
       （audit_bounds 报 `child out of parent`）。 */
    set_num(s_ui.system_note2, "负载 %.2f/%.2f/%.2f · 进程 %d · 交换 %.1f/%.1fG · 运行 %s",
            st->cpu.load1, st->cpu.load5, st->cpu.load15, st->cpu.procs,
            st->mem.swap_used_mb / 1024.f, st->mem.swap_total_mb / 1024.f,
            fmt_uptime(c2, sizeof c2, st->uptime_s));
    /* 空态与池二选一：空态盒子要独占卡体才能居中，所以池空的时候把它收起来
       （池空着呢，藏不藏都一样看不见；下次有数据时尺寸变化会触发重排）。 */
    show(s_ui.vol_empty, st->nvols == 0); show(s_ui.vol_pool, st->nvols > 0);
    show(s_ui.raid_empty, st->nraid == 0); show(s_ui.raid_pool, st->nraid > 0);
    show(s_ui.disk_empty, st->ndisks == 0); show(s_ui.disk_pool, st->ndisks > 0);
    show(s_ui.dock_empty, ndock == 0); show(s_ui.dock_pool, ndock > 0);
    show(s_ui.temp_empty, st->ntemps == 0); show(s_ui.p3_temp_pool, st->ntemps > 0);
    if (!st->ever_ok) {
        for (int i = 0; i < 4; i++) uk_kpi_set(s_ui.kpi2[i], "-", "", "等待数据", -1);
        set_num(s_ui.tr_tx_lbl, "等待数据");
        set_num(s_ui.tr_rx_lbl, "等待数据");
        set_num(s_ui.net_axis, "无数据");
        /* 从没采到过数据：把图清空，不留上一轮的残影 */
        chart_clear(s_ui.tr_net, 2);
        set_txt(s_ui.system_note, "等待采集，尚无有效快照");
        set_num(s_ui.system_note2, "");
    }
    /* ── P3：容器 / 温度 ───────────────────────────────────────────────
       两张清单都是自适应池：行按需建、多出来的行销毁（不是隐藏 —— 池的条目收集
       不看 HIDDEN，隐藏的行下一拍会被重新显示成上一帧的旧行）。 */
    network_layout();
    ndock = rows_sync(&s_ui.dock_row, &s_ui.dock_made, ndock, s_ui.dock_pool, false, false);
    for (int i = 0; i < ndock; i++) {
        const fnos_docker_t *d = &st->docker[i];
        uk_row_t *r = s_ui.dock_row[i];
        const char *prefix = dock_live ? "" : (dock_partial ? "部分可读 · " : "上次");
        if (strcmp(lv_label_get_text(r->name1), d->n)) s_p3_dock_n = -1;
        /* Name and state each own a line; a long raw Docker status cannot erase identity. */
        uk_row_set(r,d->n," ","",NULL,-1,0);
        set_txt(r->name2,"%s%s",prefix,d->up ? "运行中" : d->s);
        lv_obj_set_style_text_color(r->name2, uk_c(dock_live && d->up ? UK_OK : UK_T3), 0);
    }
    pool_layout(&s_p3_dock_n, &s_p3_dock_w, &s_p3_dock_h, ndock,
                s_ui.dock_pool, UK_LIST_MIN_WIDTH, false, NULL);

    /* ── P3：硬件温度摘要 = **一台设备一行** ─────────────────────────────
       原来按"通道"一行（24 路 24 行），设备名与通道名混在一行里、"PCIe-8-SSD 512GB"
       这种同型号的盘看起来就是一堆重复行。现在一台设备一行：设备名 + 最热通道 + 路数，
       全量通道在「温度」页。分组见 temp_groups()。 */
    if (temp_groups()) {
    int ndev = s_tgrp_n;
    ndev = rows_sync(&s_ui.p3_temp_row, &s_ui.p3_temp_made, ndev, s_ui.p3_temp_pool, true, true);
    for (int i = 0; i < ndev; i++) {
        const temp_grp_t *g = &s_tgrp[i];
        uk_row_t *r = s_ui.p3_temp_row[i];
        if (!r) continue;
        const fnos_temp_t *ht = &st->temps[g->hot];
        const fnos_temp_t *g0 = &st->temps[g->first];
        char sub[80], vb[16];
        /* Stable device ID comes before incidental channel text, so clipping keeps identity. */
        if (g->dup) snprintf(sub, sizeof sub, "%s · %d 路 · %s", g->dev, g->count, ht->ch);
        else if (ht->ch[0]) snprintf(sub, sizeof sub, "最热 %s · %d 路", ht->ch, g->count);
        else snprintf(sub, sizeof sub, "%d 路", g->count);
        const char *nm = g0->dn[0] ? g0->dn : g0->dev;
        snprintf(vb, sizeof vb, "%.1f", g->max_c);
        uk_row_set(r, nm, sub, vb, "°C", (int32_t)g->max_c, 0);
        if (g->dup) set_txt(r->name2,"%s · %d 路 · %s",g->dev,g->count,ht->ch);
        else if (ht->ch[0]) set_txt(r->name2,"最热 %s · %d 路",ht->ch,g->count);
        else set_txt(r->name2,"%d 路",g->count);
        uint32_t tc = st->online ? uk_temp_color(g->max_c) : UK_T3;
        lv_obj_set_style_text_color(r->val, uk_c(tc), 0);
        if (r->bar) lv_obj_set_style_bg_color(r->bar, uk_c(tc), LV_PART_INDICATOR);
    }
    pool_layout(&s_p3_temp_n, &s_p3_temp_w, &s_p3_temp_h, ndev,
                s_ui.p3_temp_pool, UK_LIST_MIN_WIDTH, false, NULL);

    /* P4: topology and user choice drive layout; ordinary samples update values. */
    int nblk = s_tgrp_n;
    for (int i = 0; i < nblk; i++) {
        s_tgrp[i].collapsed = true;
        for (int old = 0; old < s_ui.temp_blk_made; old++) {
            if (s_ui.temp_blk[old].dev && !strcmp(s_ui.temp_blk[old].dev, s_tgrp[i].dev)) {
                s_tgrp[i].collapsed = s_ui.temp_blk[old].collapsed;
                break;
            }
        }
    }
    if (nblk > s_ui.temp_blk_capacity) {
        temp_blk_t *grown = uk_realloc(s_ui.temp_blk, (size_t)nblk * sizeof *grown);
        if (grown) {
            memset(grown + s_ui.temp_blk_capacity, 0, (nblk - s_ui.temp_blk_capacity) * sizeof *grown);
            s_ui.temp_blk = grown;
            s_ui.temp_blk_capacity = nblk;
        } else nblk = LV_MIN(nblk, s_ui.temp_blk_made);
    }
    for (int i = s_ui.temp_blk_made; i < nblk; i++) temp_blk_build(i);
    /* 设备数变少时**销毁**多余的块（不是隐藏）：池重排会把池里的条目重新显示出来，
       留着"隐藏的块"下一拍就会带着上一帧的旧温度回到屏幕上。 */
    if (nblk < s_ui.temp_blk_made) {
        for (int i = nblk; i < s_ui.temp_blk_made; i++) {
            if (s_ui.temp_blk[i].box) lv_obj_delete(s_ui.temp_blk[i].box);
            lv_free(s_ui.temp_blk[i].channels);
            lv_free(s_ui.temp_blk[i].dev);
            memset(&s_ui.temp_blk[i], 0, sizeof s_ui.temp_blk[i]);
        }
    }
    s_ui.temp_blk_made = nblk;

    for (int i = 0; i < nblk; i++) { s_ui.temp_blk[i].collapsed = s_tgrp[i].collapsed; temp_blk_fill(i); }
    pool_layout(&s_p4_n, &s_p4_w, &s_p4_h, nblk,
                s_ui.temp_pool, UK_LIST_MIN_WIDTH, false, NULL);
    if (s_ui.temp_empty_all) {
        bool none = st->ever_ok && st->ntemps == 0;
        if (none) lv_obj_remove_flag(s_ui.temp_empty_all, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.temp_empty_all, LV_OBJ_FLAG_HIDDEN);
    }
    } /* A failed grouping keeps copied labels and owned device IDs for retry. */
    /* Visual reference counts are separate from collector health alerts. */
    if (!st->ever_ok) {
        set_num(s_ui.temp_hero, "--");
        set_txt(s_ui.temp_hero_unit, "");
        set_txt(s_ui.temp_note, "等待数据");
        set_txt(s_ui.temp_note2, "");
        lv_obj_set_style_bg_color(s_ui.temp_hero_dot, uk_c(UK_T3), 0);
        dot_glow(s_ui.temp_hero_dot, UK_T3, 0);
    } else if (st->ntemps == 0) {
        set_num(s_ui.temp_hero, "--");
        set_txt(s_ui.temp_hero_unit, "°C");
        set_txt(s_ui.temp_note, "采集端没有上报温度通道");
        set_txt(s_ui.temp_note2, "看「设置」页的采集段一行：temps 可能是 denied / missing");
        lv_obj_set_style_bg_color(s_ui.temp_hero_dot, uk_c(UK_T3), 0);
        dot_glow(s_ui.temp_hero_dot, UK_T3, 0);
    } else {
        int danger = 0, warn = 0;
        const fnos_temp_t *h = &st->temps[0];
        for (int i = 0; i < st->ntemps; i++) {
            if (st->temps[i].c >= UK_TEMP_DANGER) danger++;
            if (st->temps[i].c >= UK_TEMP_WARM) warn++;
            /* 列表按"设备名+通道名"固定排序（见 fnos_data.c），所以最热的那一路
               不再固定在第 0 行 —— 这里自己扫一遍 max，别假设 temps[0] 是最热的。 */
            if (st->temps[i].c > h->c) h = &st->temps[i];
        }
        /* 摘要是全宽 900px，放得下完整设备名 —— 这里优先 dn（"Marvell AQC113 10GbE"），
           没有才退回 dev（enp1s0）。 */
        const char *hn = h->dn[0] ? h->dn : h->dev;
        /* 大字沿用桃色卡自带的墨色（夜间由 theme_apply 改成 UK_T2），严重度只由
           左侧信号点承载：彩色数字压在暖底上对比度不够（01-warming-p4 曾掉到 1.13:1）。 */
        const uint32_t hc = st->online ? uk_temp_color(h->c) : UK_T3;
        set_num(s_ui.temp_hero, "%.1f", h->c);
        set_txt(s_ui.temp_hero_unit, "°C");
        lv_obj_set_style_bg_color(s_ui.temp_hero_dot, uk_c(hc), 0);
        dot_glow(s_ui.temp_hero_dot, hc, st->online ? UK_GLOW_OPA : 0);
        if (h->ch[0]) set_txt(s_ui.temp_note, "最热 %s · %s", hn, h->ch);
        else          set_txt(s_ui.temp_note, "最热 %s", hn);
        set_txt(s_ui.temp_note2, "%d 路传感器 · 参考区间 ≥%.0f°C 关注 %d 路 · ≥%.0f°C 高温 %d 路 · 采集告警见系统",
                st->ntemps, (double)UK_TEMP_WARM, warn, (double)UK_TEMP_DANGER, danger);
    }

    set_txt(s_ui.agent_detail[0], "%s", st->host[0] ? st->host : "-");
    set_txt(s_ui.agent_detail[1], "%s:%d", FNOS_HOST, FNOS_PORT);
    set_num(s_ui.agent_detail[2], "%d ms (状态 %d)", st->http_ms, st->last_status);
    set_num(s_ui.agent_detail[3], "%u / %u", (unsigned)st->ok_count, (unsigned)st->fail_count);
    set_txt(s_ui.agent_detail[4], "%s", st->last_err[0] ? st->last_err : "无");
    set_num(s_ui.agent_detail[5], "%s", fmt_age(c1, sizeof c1, data_age_ms(st)));
    set_num(s_ui.agent_detail[6], "%u KB",
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    set_num(s_ui.agent_detail[7], st->has_zfs ? "%.1f GB · 命中 %.1f%%" : "未采集到 ZFS 数据", st->zfs_arc_gb, st->zfs_hit_pct);

    /* 协议：只在真比本机新时说清楚"哪些东西看不到"，不然用户会以为界面漏了数据。 */
    if (st->proto <= 0)        set_txt(s_ui.agent_detail[8], "未上报（旧版应用，按 v1 读）");
    else if (st->proto > FNOS_PROTO_KNOWN)
        set_txt(s_ui.agent_detail[8], "v%d（本机只认到 v%d，新字段会忽略）", st->proto, FNOS_PROTO_KNOWN);
    else                       set_txt(s_ui.agent_detail[8], "v%d", st->proto);

    /* Diagnostics retain every module name and status, including future keys. */
    if (!st->nmods) set_txt(s_ui.agent_detail[9],"未上报（旧版应用）");
    else {
        size_t cap=1;
        for (int i=0;i<st->nmods;i++)
            cap+=strlen(st->mods[i].name)+strlen(mod_status_cn(st->mods[i].status))+sizeof " · ";
        char *segments=uk_alloc(cap);
        if (segments) {
            size_t at=0;
            for (int i=0;i<st->nmods;i++)
                at+=(size_t)snprintf(segments+at,cap-at,"%s%s %s",i ? " · " : "",
                    st->mods[i].name,mod_status_cn(st->mods[i].status));
            set_txt(s_ui.agent_detail[9],"%s",segments);
            lv_label_set_long_mode(s_ui.agent_detail[9],LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_height(s_ui.agent_detail[9],LV_SIZE_CONTENT);
            lv_free(segments);
        }
    }
    int nalerts = st->nalerts;
    if (nalerts > s_ui.alert_made) {
        lv_obj_t **grown = uk_realloc(s_ui.alert_lbl, (size_t)nalerts * sizeof *grown);
        if (grown) {
            s_ui.alert_lbl = grown;
            for (int i = s_ui.alert_made; i < nalerts; i++) {
                grown[i] = uk_label(s_ui.alert_col, UK_FONT_CJK_16, UK_WARN, "");
                lv_obj_set_width(grown[i], LV_PCT(100));
                lv_label_set_long_mode(grown[i], LV_LABEL_LONG_MODE_WRAP);
            }
            s_ui.alert_made = nalerts;
        } else nalerts = s_ui.alert_made;
    }
    for (int i = nalerts; i < s_ui.alert_made; i++) lv_obj_delete(s_ui.alert_lbl[i]);
    s_ui.alert_made = nalerts;
    if (nalerts == 0) {
        for (int i = 0; i < s_ui.alert_made; i++) show(s_ui.alert_lbl[i], false);
        show(s_ui.alert_col, false);          /* 空态盒子要独占卡体才能居中 */
        show(s_ui.alert_none_box, true);
        set_txt(s_ui.alert_none, "%s",
                !st->ever_ok ? (why ? why : "等待告警数据")
                             : !st->online ? (why ? why : "采集端离线\n告警状态未知")
                                           : "暂无告警事件");
    } else {
        show(s_ui.alert_none_box, false);
        show(s_ui.alert_col, true);
        for (int i = 0; i < s_ui.alert_made; i++) {
            if (i >= st->nalerts) { show(s_ui.alert_lbl[i], false); continue; }
            show(s_ui.alert_lbl[i], true);
            /* agent 的告警级别是 "crit" / "warn" / "info"（见 nas/fnos-agent.py:_alerts） */
            uint32_t ac = (strcmp(st->alerts[i].lv, "crit") == 0) ? UK_DANGER
                        : (strcmp(st->alerts[i].lv, "warn") == 0) ? UK_WARN : UK_T3;
            lv_obj_set_style_text_color(s_ui.alert_lbl[i], uk_c(st->online ? ac : UK_T3), 0);
            set_txt(s_ui.alert_lbl[i], "%s%s", st->online ? "" : "上次采集：", st->alerts[i].m);
        }
        uk_viewport_snap(s_ui.alert_col, UK_S3);
    }
    storage_layout();
    system_layout();
    /* P5：告警页与上面同源，按严重度分三组。组内行数多退少补 —— 不是 HIDDEN：
       隐藏的行仍然占位（见 row_trim 的注释）。 */
    if (s_ui.alert_page_note) {
        int cnt[3] = { 0, 0, 0 };
        for (int i = 0; i < st->nalerts; i++) cnt[alert_group_of(st->alerts[i].lv)]++;
        for (int g = 0; g < 3; g++) {
            lv_obj_t *col = s_ui.alert_page_col[g];
            if (!col) continue;
            uint32_t have = lv_obj_get_child_count(col);
            while (have < (uint32_t)cnt[g]) {
                lv_obj_t *l = uk_label(col, UK_FONT_CJK_16, UK_T2, "");
                lv_obj_set_width(l, LV_PCT(100));
                lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
                have++;
            }
            while (have > (uint32_t)cnt[g]) {
                lv_obj_delete(lv_obj_get_child(col, have - 1));
                have--;
            }
            int k = 0;
            for (int i = 0; i < st->nalerts; i++) {
                if (alert_group_of(st->alerts[i].lv) != g) continue;
                lv_obj_t *l = lv_obj_get_child(col, k++);
                if (!l) break;
                set_txt(l, "%s%s", st->online ? "" : "上次采集：", st->alerts[i].m);
                uint32_t ac = g == 0 ? UK_DANGER : g == 1 ? UK_WARN : UK_T3;
                lv_obj_set_style_text_color(l, uk_c(st->online ? ac : UK_T3), 0);
            }
            lv_obj_t *hrow = lv_obj_get_parent(s_ui.alert_page_head[g]);
            if (cnt[g] > 0) { show(hrow, true); show(col, true); }
            else            { show(hrow, false); show(col, false); }
            set_txt(s_ui.alert_page_cap[g], "%d 条", cnt[g]);
        }
        if (!st->ever_ok) set_txt(s_ui.alert_page_note, "%s", why ? why : "等待采集端 · 尚无有效快照");
        else if (!st->online) set_txt(s_ui.alert_page_note, "采集端离线 · 严重 %d · 警告 %d · 提示 %d", cnt[0], cnt[1], cnt[2]);
        else set_txt(s_ui.alert_page_note, "严重 %d · 警告 %d · 提示 %d", cnt[0], cnt[1], cnt[2]);
        show(s_ui.alert_page_empty, st->nalerts == 0);
        alert_health_sync(st);
    }
    inventory_notice();
}

/* ── tick / 生命周期 ──────────────────────────────────────────────── */
static void ui_tick(lv_timer_t *t)
{
    (void)t;
    uk_alloc_reset();
    if (s_night_req != s_night) {
        s_night = s_night_req;
        night_apply();
    }
    /* 串口 'page n' 的落地点：别的任务只能置标志（本板规矩：LVGL 只许在 LVGL 任务里调）。
       实机验收靠它——板上没有触摸自动化，不这样切页就只能拍到开机那一页。 */
    int want = s_page_req;
    if (want >= 0) {
        s_page_req = -1;
        fnos_ui_set_page(want);
    }
    /* 串口 'temp n' 的落地点挪到本函数末尾的 refresh() 之后（见那里）。 */

    /* 出厂固件没有 Wi-Fi 凭据：开机 ~3 秒后自动把配网卡推出来 —— 用户的原话是
       "固件刷好之后，也需要提示用户接入内网 WiFi"，而他看到的本来只是一块"离线"的
       屏，不知道该点哪里。用户主动关过就不再弹（入口留在顶栏）。

       判据必须是**存储本身**（fnos_wifi_store_load，跟 `wifi show` 同源），不能问
       fnos_net_configured()：那个标志由网络层读完 NVS 才置真，而这里到点就往弹，
       于是 NVS 稍慢一步就误判成"没凭据"——实机上真出现过：凭据明明在（wifi show
       回显 凭据：有 / 已连接 llll），系统页却被配网卡盖住，还自动起了一次扫描。 */
    static int boot_ticks;
    if (boot_ticks >= 0 && ++boot_ticks > 6) {
        boot_ticks = -1;
        char ssid[64];
        bool have = fnos_wifi_store_load(ssid, sizeof ssid, NULL, 0);
        if (!have && !s_wifi_dismissed && !s_wifi_open && s_ui.wifi_card) {
            ESP_LOGI(TAG, "没有 Wi-Fi 凭据：自动弹出配网卡（顶栏按钮同样能打开）");
            fnos_ui_set_page(3);
            s_wifi_manual = false;
            s_wifi_sel    = -1;
            wifi_set_stage(WIFI_ST_SCAN);
            fnos_net_scan_request();
            wifi_show(true);
        }
    }
    wifi_refresh();

    /* 直接读进 s_st：fnos_data_get 拿不到锁时**不碰 out**（保留上一帧），
       成功时整份覆盖 —— 语义与"先拷一份、再拷回来"完全一致，但少一个
       fnos_status_t 的栈上副本。这个结构有 2 KB 量级，而本函数跑在
       LVGL 任务上（栈 12 KB），UI 任务上每一百字节都值得省。 */
    fnos_data_get(&s_st);

    /* smp 是纯中转缓冲，且 ui_tick 只在 LVGL 任务里跑（lv_timer 回调），
       所以放 static 而不是压在栈上 —— 又是 1 KB。 */
    static fnos_sample_t smp[64];
    int64_t next = s_seq;
    int n = s_st.ever_ok ? fnos_data_hist_read(s_seq, smp, 64, &next) : 0;
    if (!s_st.ever_ok) {
        /* uk_trend 自己持有数据（原生 lv_chart），没有中转环要清 */
        chart_clear(s_ui.overview_resource[0].chart, 1);
        chart_clear(s_ui.overview_resource[1].chart, 1);
        chart_clear(s_ui.overview_net, 2);
        chart_clear(s_ui.tr_net, 2);
        memset(s_overview_gap, 0, sizeof s_overview_gap);
    }
    bool live[] = { overview_live(&s_st, "cpu"), overview_live(&s_st, "mem"), overview_live(&s_st, "net") };
    /* History has no per-point validity. Do not plot retained module values as
       new measurements while the current module explicitly reports stale/error. */
    if (s_st.ever_ok)
        for (int k = 0; k < 3; k++) if (!live[k]) s_overview_gap[k] = true;
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 2; k++) if (live[k]) {
            lv_obj_t *chart = s_ui.overview_resource[k].chart;
            if (s_overview_gap[k]) uk_trend_push(chart, 0, LV_CHART_POINT_NONE);
            uk_trend_push(chart, 0, (int32_t)(k == 0 ? smp[i].cpu : smp[i].mem));
            s_overview_gap[k] = false;
        }
        if (live[2]) {
            if (s_overview_gap[2]) {
                uk_trend_push(s_ui.overview_net, 0, LV_CHART_POINT_NONE);
                uk_trend_push(s_ui.overview_net, 1, LV_CHART_POINT_NONE);
            }
            uk_trend_push(s_ui.overview_net, 0, smp[i].rx_kbs);
            uk_trend_push(s_ui.overview_net, 1, smp[i].tx_kbs);
            s_overview_gap[2] = false;
        }
        uk_trend_push(s_ui.tr_net, 0, smp[i].tx_kbs);
        uk_trend_push(s_ui.tr_net, 1, smp[i].rx_kbs);
    }
    if (n > 0) s_seq = next;

    /* Consume real samples even during a long drag. Only the expensive text and
       pool refresh waits for the transition to finish. */
    if (motion_busy()) { s_refresh_pending = true; return; }
    s_refresh_pending = false;
    refresh();
    /* 串口 'temp n' 的落地点。温度块的展开态在真机上只能用手指点，而板上没有触摸
       自动化 —— 这是让"12 路网格"进实机照片的唯一通道（展开判据与 temp_toggle_cb
       一致，别再写第二套）。**必须排在 refresh() 之后**：温度块是 refresh 里建的
       （temp_blk_build），开机后 p4 从未布局过时这里才拿得到 box。请求是一次性的：
       索引越界/没有数据/这台设备只有一路通道，都直接作废，不留悬着的状态。 */
    int expand = s_temp_expand_req;
    s_temp_expand_req = -1;
    if (expand >= 0 && expand < s_tgrp_n && s_tgrp[expand].count > 1 &&
        s_ui.temp_blk && expand < s_ui.temp_blk_made && s_ui.temp_blk[expand].box) {
        s_ui.temp_blk[expand].collapsed = false;
        s_p4_n = -1;                     /* 折叠/展开改了池内条目的高，让布局重跑 */
        refresh();
    }
    pair_refresh();
    pages_warm();
    inventory_notice();
}

static lv_obj_t *build_page(lv_obj_t *content, int idx)
{
    lv_obj_t *p = mk_page(content);
    switch (idx) {
    case 0: build_p0(p); break;
    case 1: build_p1(p); break;
    case 2: build_p2(p); break;
    case 3: build_p3(p); break;
    case 4: build_p4(p); break;      /* 温度：全部传感器通道 */
    case 5: build_p5(p); break;      /* 告警：按严重度分组的全部事件 */
    default: build_p3(p); break;     /* 到不了（页数由 FNOS_UI_PAGE_COUNT 决定） */
    }
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

void fnos_ui_create(void)
{
    if (s_created) return;

    fnos_status_release(&s_st);

    /* 外壳（rail / head / foot + 内容区）整块交给 ui_kit：uk_shell 把它切成四份，
       位置全部由 flex 决定，这里一个坐标都不算。 */
    lv_obj_t *scr = uk_screen();
    s_ui.screen = scr;
    lv_obj_t *rail = NULL, *head = NULL, *foot = NULL;
    lv_obj_t *content = uk_shell(scr, &rail, &head, &foot);
    s_ui.viewport = content;
    build_rail(rail);
    build_header(head);
    build_foot(foot);

    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        s_ui.page[i] = build_page(content, i);
        /* 建完就把非当前页收起来：fnos_ui_set_page(0) 在"已经在第 0 页"时会提前返回，
           于是"只有当前页可见"这条不变量在 set_page 第一次真正换页之前并不成立 ——
           四个页面同时可见，几何审计会拿不同页的卡互相比较，报出跨页假重叠
           （曾经连报两次"磁盘活动卡压诊断卡"，而两张卡根本不在同一页）。
           不变量要么在出生时就成立，要么它就不是不变量。 */
        if (i > 0) show(s_ui.page[i], false);
    }

    /* 顺序要紧：fnos_ui_set_page() 见到 !s_created 会直接返回，
     * 所以必须先置位再激活首页 —— 否则四页全被 HIDDEN，实机内容区一片黑
     * （预览逐页调用 set_page，看不到这个 bug；见 docs/verification.md §17.3）。 */
    s_created = true;
    s_page = -1;
    fnos_ui_set_page(0);
    overview_layout(); /* Measure placeholder content before the first data tick. */
    night_apply();

    lv_timer_create(ui_tick, 500, NULL);
    motion_init();
    ESP_LOGI(TAG, "ui created (pages=%d)", FNOS_UI_PAGE_COUNT);
}

/* 自描述：把已建好的卡片几何按行吐给主机工具。看门狗（tools/preview/scroll_gap.js）
   的卡片表以前是手抄的，抄错一次就报 15 处假不合格 —— 现在几何只有这一处来源。
   p=0 对页面没意义（建树时四页都建），看门狗是按尺寸与自己的表对上号的。 */
void fnos_ui_dump_card_map(void)
{
    lv_obj_update_layout(lv_screen_active());   /* 不跑一次布局，coords 全是 0 */
    /* 页面对象自身也要报一句：卡片 coords 是**屏幕绝对**坐标，页对象一旦被挪动，
       同一张卡的坐标就整体平移 —— 看门狗拿它去量滚动条净距会全错。
       并排打印"页坐标 + 页内偏移"就是让人一眼能验算。 */
    for (int k = 0; k < FNOS_UI_PAGE_COUNT; k++) {
        if (!s_ui.page[k]) continue;
        lv_area_t pa;
        lv_obj_get_coords(s_ui.page[k], &pa);
        fprintf(stdout, "[page] k=%d x=%d y=%d w=%d h=%d hidden=%d\n", k,
                (int)pa.x1, (int)pa.y1, (int)(pa.x2 - pa.x1 + 1), (int)(pa.y2 - pa.y1 + 1),
                lv_obj_has_flag(s_ui.page[k], LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    }
    /* 卡片在树里是挂在各页下面的：页号从祖先链上读，别用 s_page（建树时它是 0）。 */
    for (int i = 0; i < s_ui.ncards; i++) {
        lv_obj_t *c = s_ui.cards[i];
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        /* 有没有列表：卡片的直接子对象里有没有"可滚动"的那个。
           注意不能拿 scroll_bottom>0 判断 —— 预览里列表全是空态（行池都当空行），
           内容并不溢出，但它照样是列表卡。看门狗再用像素去确认条子在不在。 */
        bool scrolls = false;
        int n = (int)lv_obj_get_child_count(c);
        for (int k = 0; k < n; k++) {
            lv_obj_t *ch = lv_obj_get_child(c, k);
            if (lv_obj_has_flag(ch, LV_OBJ_FLAG_SCROLLABLE)) { scrolls = true; break; }
        }
        int pg = -1;
        for (int k = 0; k < FNOS_UI_PAGE_COUNT; k++)
            if (s_ui.page[k] && lv_obj_get_parent(c) == s_ui.page[k]) pg = k;
        fprintf(stdout, "[card] p=%d x=%d y=%d w=%d h=%d list=%d\n", pg, a.x1, a.y1,
                a.x2 - a.x1 + 1, a.y2 - a.y1 + 1, scrolls ? 1 : 0);
    }
    fflush(stdout);
}

/* Page translation is confined to an opaque viewport. Invalidate it on every
   frame, including completion, so partial-buffer displays repaint exposed pixels. */
static struct {
    int from, to, direction;
    int32_t offset, start, end, width, press_offset;
    bool active, tracking, dragging, swallow_click;
    lv_point_t press, last;
    uint32_t last_tick;
    int32_t velocity;
    uint32_t duration;
    float phase_velocity;
    lv_indev_t *pointer;
} s_motion;

#ifdef CONFIG_FNOS_UI_MOTION_STATS
static struct {
    int64_t prepared, render_start, render_total, render_max;
    uint32_t frames, started;
} s_motion_stats;

static void motion_render_cb(lv_event_t *e)
{
    if (!s_motion.active) return;
    if (lv_event_get_code(e) == LV_EVENT_RENDER_START)
        s_motion_stats.render_start = esp_timer_get_time();
    else if (lv_event_get_code(e) == LV_EVENT_RENDER_READY && s_motion_stats.render_start) {
        int64_t elapsed = esp_timer_get_time() - s_motion_stats.render_start;
        s_motion_stats.render_total += elapsed;
        if (elapsed > s_motion_stats.render_max) s_motion_stats.render_max = elapsed;
        s_motion_stats.frames++;
        s_motion_stats.render_start = 0;
    }
}
#endif

static bool motion_reduced(void)
{
#ifdef CONFIG_FNOS_UI_REDUCED_MOTION
    return CONFIG_FNOS_UI_REDUCED_MOTION;
#else
    return false;
#endif
}

static bool motion_busy(void) { return s_motion.active; }

static void page_layout(int idx)
{
    /* All pages already hold the latest snapshot. Navigation only has to resolve
       the incoming geometry, never format and relayout the entire application. */
    lv_obj_update_layout(s_ui.page[idx]);
    if (idx == 1) {
        storage_layout();
        pool_layout(&s_p1_vol_n, &s_p1_vol_w, &s_p1_vol_h, s_ui.vol_made, s_ui.vol_pool, UK_LIST_MIN_WIDTH, false, NULL);
        pool_layout(&s_p1_raid_n, &s_p1_raid_w, &s_p1_raid_h, s_ui.raid_made, s_ui.raid_pool, UK_LIST_MIN_WIDTH, false, NULL);
        pool_layout(&s_p1_disk_n, &s_p1_disk_w, &s_p1_disk_h, s_ui.disk_made, s_ui.disk_pool, UK_LIST_MIN_WIDTH, false, NULL);
    } else if (idx == 2) {
        network_layout();
        if (s_ui.net_pool) uk_pool_relayout(s_ui.net_pool,UK_LIST_MIN_WIDTH,false,NULL);
    } else if (idx == 3) {
        system_layout();
        pool_layout(&s_p3_dock_n, &s_p3_dock_w, &s_p3_dock_h, s_ui.dock_made, s_ui.dock_pool, UK_LIST_MIN_WIDTH, false, NULL);
        pool_layout(&s_p3_temp_n, &s_p3_temp_w, &s_p3_temp_h, s_ui.p3_temp_made, s_ui.p3_temp_pool, UK_LIST_MIN_WIDTH, false, NULL);
    } else if (idx == 4) {
    pool_layout(&s_p4_n, &s_p4_w, &s_p4_h, s_ui.temp_blk_made, s_ui.temp_pool, UK_LIST_MIN_WIDTH, false, NULL);
    /* 池刚给每块分了宽度：展开的通道网格跟着重算列数（池列数变了块宽就变）。 */
    for (int i = 0; i < s_ui.temp_blk_made; i++) temp_grid_relayout(&s_ui.temp_blk[i]);
    }
    lv_obj_update_layout(s_ui.page[idx]);
}

static void pages_warm(void)
{
    if (s_pair_open || s_wifi_open || s_diagnostics || s_motion.active) return;
    static struct { bool ready; int32_t width, height; int count[3]; } prepared[FNOS_UI_PAGE_COUNT];
    int32_t width = lv_obj_get_content_width(s_ui.viewport);
    int32_t height = lv_obj_get_content_height(s_ui.viewport);
    for (int idx = 1; idx < FNOS_UI_PAGE_COUNT; idx++) {
        int count[3] = {0};
        if (idx == 1) {
            count[0] = s_ui.vol_made; count[1] = s_ui.raid_made; count[2] = s_ui.disk_made;
        } else if (idx == 3) {
            count[0] = s_ui.dock_made; count[1] = s_ui.p3_temp_made; count[2] = s_st.nalerts;
        } else if (idx == 4) {
            count[0] = s_ui.temp_blk_made; count[1] = s_st.ntemps;
        } else continue;
        if (prepared[idx].ready && prepared[idx].width == width && prepared[idx].height == height &&
            !memcmp(prepared[idx].count, count, sizeof count)) continue;
        /* A single UI task resolves offscreen geometry before returning to the
           renderer. No temporary page becomes visible on the physical display. */
        bool hidden = lv_obj_has_flag(s_ui.page[idx], LV_OBJ_FLAG_HIDDEN);
        if (hidden) {
            lv_obj_set_style_translate_x(s_ui.page[idx], width, 0);
            show(s_ui.page[idx], true);
        }
        page_layout(idx);
        if (hidden) { show(s_ui.page[idx], false); lv_obj_set_style_translate_x(s_ui.page[idx], 0, 0); }
        prepared[idx].ready = true; prepared[idx].width = width; prepared[idx].height = height;
        memcpy(prepared[idx].count, count, sizeof count);
    }
}

static void motion_paint(void)
{
    lv_obj_set_style_translate_x(s_ui.page[s_motion.from], s_motion.offset, 0);
    lv_obj_set_style_translate_x(s_ui.page[s_motion.to], s_motion.offset + s_motion.direction * s_motion.width, 0);
    lv_obj_invalidate(s_ui.viewport);
}

static void motion_exec(void *unused, int32_t value)
{
    (void)unused;
    /* Closed-form critical damping remains stable with dropped frames and
       preserves the current velocity when a tap reverses an in-flight page. */
    float t = value / 1024.0f;
    float displacement = s_motion.start - s_motion.end;
    float b = s_motion.phase_velocity + UK_MOTION_DAMPING * displacement;
    float decay = expf(-UK_MOTION_DAMPING * t);
    int32_t offset = s_motion.end + (int32_t)lroundf((displacement + b * t) * decay);
    s_motion.velocity = (int32_t)lroundf((b - UK_MOTION_DAMPING * (displacement + b * t)) * decay
                                       * 1000.0f / s_motion.duration);
    if (value == 1024) { offset = s_motion.end; s_motion.velocity = 0; }
    if (offset == s_motion.offset) return;
    s_motion.offset = offset;
    motion_paint();
}

static void motion_finish(void)
{
#ifdef CONFIG_FNOS_UI_MOTION_STATS
    if (s_motion.active && s_motion_stats.frames) {
        ESP_LOGI(TAG, "motion %d->%d prepare_us=%lld frames=%lu render_avg_us=%lld render_max_us=%lld elapsed_ms=%lu",
                 s_motion.from, s_motion.to, (long long)s_motion_stats.prepared,
                 (unsigned long)s_motion_stats.frames,
                 (long long)(s_motion_stats.render_total / s_motion_stats.frames),
                 (long long)s_motion_stats.render_max,
                 (unsigned long)lv_tick_elaps(s_motion_stats.started));
    }
#endif
    s_motion.active = false;
    s_motion.velocity = 0;
    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        show(s_ui.page[i], i == s_page);
        lv_obj_set_style_translate_x(s_ui.page[i], 0, 0);
    }
    lv_obj_invalidate(s_ui.viewport);
    if (s_refresh_pending) {
        s_refresh_pending = false;
        refresh(); pair_refresh();
    }
}

static void motion_done(lv_anim_t *a) { (void)a; motion_finish(); }

void fnos_ui_motion_settle(void)
{
    lv_anim_delete(&s_motion, motion_exec);
    s_motion.tracking = s_motion.dragging = false;
    if (s_created) motion_finish();
}

static void motion_snap(bool commit)
{
    s_motion.tracking = false;
    s_motion.dragging = false;
    s_page = commit ? s_motion.to : s_motion.from;
    nav_select(s_page);
    show(s_ui.diagnostics_button, s_page == 3);
    if (motion_reduced()) {
        motion_finish();
        return;
    }
    s_motion.start = s_motion.offset;
    s_motion.end = commit ? -s_motion.direction * s_motion.width : 0;
    int32_t remaining = abs(s_motion.end - s_motion.start);
    if (!remaining) { motion_finish(); return; }
    s_motion.duration = remaining * UK_MOTION_MS / s_motion.width;
    if (s_motion.duration < UK_MOTION_MIN_MS) s_motion.duration = UK_MOTION_MIN_MS;
    if (s_motion.duration > UK_MOTION_MS) s_motion.duration = UK_MOTION_MS;
    s_motion.phase_velocity = s_motion.velocity * s_motion.duration / 1000.0f;
    /* A very fast release must not overshoot the selected page. Reversals keep
       their away-from-target velocity, so interruption has no sudden stop. */
    float d = s_motion.start - s_motion.end;
    if (d * s_motion.phase_velocity < 0 && fabsf(s_motion.phase_velocity) > UK_MOTION_DAMPING * fabsf(d))
        s_motion.phase_velocity = -UK_MOTION_DAMPING * d;
    s_motion.active = true;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_motion);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, s_motion.duration);
    lv_anim_set_exec_cb(&a, motion_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, motion_done);
    lv_anim_start(&a);
}

static void motion_prepare(int from, int to, int direction)
{
#ifdef CONFIG_FNOS_UI_MOTION_STATS
    int64_t prepared_at = esp_timer_get_time();
    memset(&s_motion_stats, 0, sizeof(s_motion_stats));
    s_motion_stats.started = lv_tick_get();
#endif
    s_motion.from = from; s_motion.to = to; s_motion.direction = direction;
    s_motion.width = lv_obj_get_content_width(s_ui.viewport);
    if (s_motion.width < 1) s_motion.width = lv_display_get_horizontal_resolution(NULL);
    s_motion.offset = 0;
    s_motion.velocity = 0;
    s_motion.active = true;
    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) show(s_ui.page[i], i == from || i == to);
    page_layout(to);
    motion_paint();
#ifdef CONFIG_FNOS_UI_MOTION_STATS
    s_motion_stats.prepared = esp_timer_get_time() - prepared_at;
#endif
}

void fnos_ui_set_page(int idx)
{
    if (!s_created) return;
    const int n = FNOS_UI_PAGE_COUNT;
    idx = (idx % n + n) % n;
    pair_show(false);
    if (s_wifi_open) wifi_show(false);
    s_diagnostics = false;
    show(s_ui.diagnostics, false);
    set_txt(s_ui.diagnostics_label, "设置");
    p3_overlay_sync();
    s_motion.tracking = false;
    s_motion.dragging = false;
    if (idx == s_page) {
        bool pending = s_refresh_pending;
        fnos_ui_motion_settle();
        if (!pending) refresh();
        return;
    }
    lv_anim_delete(&s_motion, motion_exec);
    if (s_page < 0 || motion_reduced()) {
        bool pending = s_refresh_pending;
        nav_select(idx); motion_finish();
        if (!pending) refresh();
    } else if (s_motion.active && idx == s_motion.from) {
        /* A quick tap back reverses from the current frame, not from zero. */
        int old = s_motion.from;
        s_motion.from = s_motion.to; s_motion.to = old;
        s_motion.offset += s_motion.direction * s_motion.width;
        s_motion.direction = -s_motion.direction;
        motion_snap(true);
    } else {
        int from = s_page;
        fnos_ui_motion_settle();
        motion_prepare(from, idx, idx > from ? 1 : -1);
        motion_snap(true);
    }
    show(s_ui.diagnostics_button, idx == 3);
    lv_obj_invalidate(s_ui.viewport);
}

static void motion_release(void)
{
    if (!s_motion.tracking) return;
    if (!s_motion.dragging) {
        s_motion.tracking = false;
        if (s_motion.active) motion_snap(s_page == s_motion.to);
        return;
    }
    int32_t travel = -s_motion.offset * s_motion.direction;
    bool distance = travel * 100 >= s_motion.width * UK_SWIPE_COMMIT_PCT;
    if (lv_tick_elaps(s_motion.last_tick) > UK_MOTION_MS / 2) s_motion.velocity = 0;
    bool flick = travel * 100 >= s_motion.width * UK_SWIPE_LOCK_PCT * 2 &&
                 abs(s_motion.velocity) * 100 >= s_motion.width * UK_SWIPE_FLICK_PCT &&
                 s_motion.velocity * s_motion.direction < 0;
    motion_snap(distance || flick);
}

static void motion_pointer_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_motion.swallow_click = false;
        if (s_pair_open || s_wifi_open || s_diagnostics) return;
        lv_point_t point; lv_indev_get_point(indev, &point);
        lv_area_t bounds; lv_obj_get_coords(s_ui.viewport, &bounds);
        if (point.x < bounds.x1 || point.x > bounds.x2 || point.y < bounds.y1 || point.y > bounds.y2) return;
        lv_anim_delete(&s_motion, motion_exec);
        s_motion.pointer = indev;
        s_motion.press = s_motion.last = point;
        s_motion.press_offset = s_motion.active ? s_motion.offset : 0;
        if (!s_motion.active) s_motion.width = lv_obj_get_content_width(s_ui.viewport);
        s_motion.last_tick = lv_tick_get();
        s_motion.velocity = 0;
        s_motion.tracking = true;
    } else if (code == LV_EVENT_RELEASED) {
        motion_release();
    } else if ((code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_CLICKED) && s_motion.swallow_click) {
        lv_indev_stop_processing(indev);
    }
}

static void motion_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!s_motion.tracking || !s_motion.pointer) return;
    if (lv_indev_get_state(s_motion.pointer) != LV_INDEV_STATE_PRESSED) { motion_release(); return; }
    lv_point_t point; lv_indev_get_point(s_motion.pointer, &point);
    int32_t dx = point.x - s_motion.press.x, dy = point.y - s_motion.press.y;
    int32_t lock = s_motion.width * UK_SWIPE_LOCK_PCT / 100;
    if (lock < UK_S2) lock = UK_S2;
    if (!s_motion.dragging) {
        if (abs(dy) > lock && abs(dy) > abs(dx)) {
            s_motion.tracking = false;
            if (s_motion.active) motion_snap(s_page == s_motion.to);
            return;
        }
        if (abs(dx) < lock || abs(dx) < abs(dy) * 2) return;
        if (!s_motion.active) {
            int direction = dx < 0 ? 1 : -1;
            motion_prepare(s_page, (s_page + direction + FNOS_UI_PAGE_COUNT) % FNOS_UI_PAGE_COUNT, direction);
        }

        s_motion.dragging = true;
        s_motion.swallow_click = true;
    }
    uint32_t elapsed = lv_tick_elaps(s_motion.last_tick);
    if (elapsed && point.x != s_motion.last.x) {
        s_motion.velocity = (point.x - s_motion.last.x) * 1000 / (int32_t)elapsed;
        s_motion.last = point; s_motion.last_tick = lv_tick_get();
    }
    int32_t offset = s_motion.press_offset + point.x - s_motion.press.x;
    /* Keep the active pair under the finger; resistance beyond its endpoints. */
    int32_t travel = -offset * s_motion.direction;
    if (travel < 0) travel /= 3;
    if (travel > s_motion.width) travel = s_motion.width + (travel - s_motion.width) / 3;
    offset = -travel * s_motion.direction;
    if (offset == s_motion.offset) return;
    s_motion.offset = offset;
    motion_paint();
}

static void motion_init(void)
{
#ifdef CONFIG_FNOS_UI_MOTION_STATS
    lv_display_add_event_cb(lv_display_get_default(), motion_render_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(lv_display_get_default(), motion_render_cb, LV_EVENT_RENDER_READY, NULL);
#endif
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev))
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER)
            lv_indev_add_event_cb(indev, motion_pointer_cb, LV_EVENT_ALL, NULL);
    lv_timer_create(motion_tick, UK_GESTURE_POLL_MS, NULL);
}

int fnos_ui_page(void)
{
    return s_page;
}

void fnos_ui_set_night(bool on)
{
    s_night_req = on;                            // 只置标志，落地在 ui_tick
}

void fnos_ui_request_page(int idx)
{
    if (idx < 0 || idx >= FNOS_UI_PAGE_COUNT) return;
    s_page_req = idx;                            // 只置标志，落地在 ui_tick
}

void fnos_ui_request_temp_expand(int idx)
{
    if (idx < 0) return;
    s_temp_expand_req = idx;                     // 只置标志，落地在 ui_tick
}
