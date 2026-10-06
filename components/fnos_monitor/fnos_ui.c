// fnos_ui.c - 监控界面（v7）：只用 kk_ui 构件手写四页。
//
// 沿用手写 C + kk_ui 构件。本文件既建树也格式化文案，只有 LVGL 任务写入界面。
// 数据只从 fnos_data 取快照、网络状态只从 fnos_net 读缓存（不在这里发起任何请求）。
//
// 版面：左导航 76px + 顶栏 56px + 内容区 948×544；四页 = 总览 / 存储 / 网络 / 系统。
#include "fnos_ui.h"
#include <stdlib.h>   /* getenv：仅用于主机侧自描述（KK_UI_CARDMAP） */

#include <stdarg.h>
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
#include "kk_theme.h"
#include "kk_widgets.h"
#include "kk_layout.h"   /* v9 版面合同：所有几何常量的唯一来源 */

static const char *TAG = "fnos_ui";

/* ── 版面常量 ─────────────────────────────────────────────────────── */
#define UI_HIST        180                  /* 数据层每秒一个采样，约 3 分钟 */
#define UI_CONTENT_W   (KK_SCR_W - KK_RAIL_W)   /* 948 */
#define UI_CONTENT_H   (KK_SCR_H - KK_HEAD_H)   /* 544 */
/* 一律引用 fnos_data.h 的常量，别手写数字：手写的那个不会跟着改，
   而"界面少一行"和"数据被丢了"看起来一模一样。 */
#define UI_ROWS_VOL     FNOS_MAX_VOLS
#define UI_ROWS_RAID    FNOS_MAX_RAID
#define UI_ROWS_DISK    FNOS_MAX_DISKS
#define UI_ROWS_TEMP    FNOS_MAX_TEMPS
#define UI_ROWS_DOCK    FNOS_MAX_DOCKER
#define UI_ROWS_ALERT   FNOS_MAX_ALERTS
#define UI_AGENT_ROWS   10
#define UI_CARDS       32
/* 温度页：把采集端报来的**每一路**通道摊成三列密集行（一行 44px，一列 8 行上下）。
   行池 = FNOS_MAX_TEMPS，每列 ceil(总数/3) —— 现在这台 NAS 是 24 路，正好 3×8 一屏放下。 */
#define UI_TEMP_COLS   3
#define UI_TEMP_ROWS   ((FNOS_MAX_TEMPS + UI_TEMP_COLS - 1) / UI_TEMP_COLS)

/* ── 配对 ─────────────────────────────────────────────────────────
   指纹一行 8 字节（"AA:BB:CC:DD:EE:FF:GG:HH" = 23 字符），SHA-256 正好 4 行。
   分组固定是刻意的：本工程没有等宽字体，靠"同样的分行 + 同样的分组"让人逐段比对，
   而不是靠字符位置对齐。 */
#define PAIR_FP_LINES   4
#define PAIR_FP_PER     8
#define PAIR_PAD_KEYS  12
#define PAIR_CODE_LEN   6
#define PAIR_STEPS      3

/* ── 构件包装 ─────────────────────────────────────────────────────── */
typedef struct {
    lv_obj_t *name, *detail, *pct;
    kk_bar_t  bar;
    bool      has_bar;
    bool      full_detail;   /* 磁盘/容器的详情占满第二行，隐藏百分比 */
} ui_row_t;

typedef struct {
    lv_obj_t *obj;
    int       w;            /* 滚动区可用内容宽（已扣掉滚动条竖槽）*/
    int       bar_w;        /* 滚动区整宽（w-32）：仅用于需要铺满到滚动条下的元素 */
} ui_list_t;

typedef struct {
    lv_obj_t *value, *unit, *sub;
    kk_bar_t  bar;
    bool      has_bar;
} ui_kpi_t;

typedef struct {
    lv_obj_t *value, *sub;
} ui_big_t;

/* ── 板上配网卡的几何与状态（定义在 s_ui 之前：结构体里要按这些尺寸开数组） ── */
#define WIFI_AP_ROWS  7          /* 扫描列表一屏显示几条（按信号取前 7） */
#define WIFI_KB_KEYS  41         /* 10 + 10 + 9 + 9 + 3 */
#define WIFI_PW_MAX   63         /* WPA2 口令最长 63 个 ASCII 字符 */
#define WIFI_SSID_MAX 32
enum { WIFI_ST_SCAN = 0, WIFI_ST_PASS, WIFI_ST_LINK };

static struct {
    lv_obj_t *screen;
    lv_obj_t *cards[UI_CARDS];
    int       ncards;
    lv_obj_t *page[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav_lbl[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav_icon[FNOS_UI_PAGE_COUNT];
    lv_obj_t *nav_mark[FNOS_UI_PAGE_COUNT];
    /* 顶栏 */
    lv_obj_t *h_host, *h_ep, *h_chip, *h_bars[4];
    lv_obj_t *health, *overview_note;
    lv_obj_t *capacity, *capacity_detail, *storage_note;
    kk_bar_t capacity_bar;
    lv_obj_t *net_axis, *net_note, *system_note, *system_note2;
    lv_obj_t *if_dot, *if_name, *info_val[4];
    lv_obj_t *vol_empty, *raid_empty, *disk_empty, *dock_empty, *temp_empty;
    lv_obj_t *diagnostics, *diagnostics_button, *diagnostics_label;
    /* P0 总览 */
    ui_kpi_t  kpi[4];

    kk_trend_t tr_cpu, tr_mem;
    lv_obj_t *tr_cpu_lbl, *tr_mem_lbl;
    lv_obj_t *tr_title;                 // 曲线横轴：说时间而不是"多少次采集"
    /* P1 存储 */
    ui_row_t  vol1[UI_ROWS_VOL];
    ui_row_t  raid[UI_ROWS_RAID];
    ui_row_t  disk[UI_ROWS_DISK];
    lv_obj_t *storage_value[3];
    /* P2 网络 */
    ui_big_t  big[4];
    kk_trend_t tr_rx, tr_tx;
    lv_obj_t *tr_rx_lbl, *tr_tx_lbl;
    lv_obj_t *net_title;
    ui_row_t  dock[UI_ROWS_DOCK];
    /* P3 系统 */
    ui_row_t  temp[UI_ROWS_TEMP];
    /* P4 温度：全部通道摊成三列（每列一池行，列内可滚动） */
    ui_row_t  temp_all[UI_TEMP_COLS][UI_TEMP_ROWS];
    lv_obj_t *temp_note, *temp_note2, *temp_empty_all;
    ui_row_t  agent[UI_AGENT_ROWS];
    ui_row_t  alert[UI_ROWS_ALERT];
    lv_obj_t *alert_none;

    /* 左导航栏（rail）自身与它的徽标：三档表面由 theme_apply() 统一落地 */
    lv_obj_t *rail, *logo_lbl;

    /* 配对（覆盖在系统页上的一张整页卡，与"采集诊断"同槽位、互斥显示）*/
    lv_obj_t *pair_btn, *pair_btn_lbl, *pair_dot;
    lv_obj_t *pair_card, *pair_title, *pair_hint;
    lv_obj_t *pair_fp[PAIR_FP_LINES], *pair_meta, *pair_msg;
    lv_obj_t *pair_steps, *pair_steps_lbl, *pair_step_no[PAIR_STEPS], *pair_step_txt[PAIR_STEPS];
    lv_obj_t *pair_code_panel, *pair_info_panel;
    lv_obj_t *pair_code_lbl, *pair_code_sub, *pair_slot[PAIR_CODE_LEN];
    lv_obj_t *pair_pad, *pair_pad_lbl, *pair_key[PAIR_PAD_KEYS];
    lv_obj_t *pair_ok, *pair_cancel, *pair_forget;

    /* 配网卡（与配对卡同槽位：盖在系统页上的整页卡，互斥显示） */
    lv_obj_t *wifi_btn, *wifi_btn_lbl;       /* 顶栏那颗常驻按钮 */
    lv_obj_t *wifi_card, *wifi_title, *wifi_hint, *wifi_list_hint;
    lv_obj_t *wifi_ap_row[WIFI_AP_ROWS], *wifi_ap_name[WIFI_AP_ROWS], *wifi_ap_meta[WIFI_AP_ROWS];
    lv_obj_t *wifi_input_panel, *wifi_ssid_box, *wifi_ssid_lbl;
    lv_obj_t *wifi_pw_box, *wifi_pw_lbl, *wifi_eye, *wifi_eye_lbl;
    lv_obj_t *wifi_kb, *wifi_key[WIFI_KB_KEYS], *wifi_shift_btn, *wifi_shift_lbl;
    lv_obj_t *wifi_rescan, *wifi_manual, *wifi_close, *wifi_back, *wifi_connect;
    lv_obj_t *wifi_link_panel, *wifi_link_big, *wifi_link_sub, *wifi_done, *wifi_again;
} s_ui;

/* 温度页每列的**可用文字宽度**（建页时量一次）。刷新时要按行的数据形态改标签宽度：
   有通道名 → 设备名独占一行（全宽）；没有通道名（老采集端）→ 整行收成"名字 + 数值"，
   名字要让出数值那一列。标签的父容器宽度含内边距，不能用它反推，所以在这里记下来。 */
static int s_temp_col_w[UI_TEMP_COLS];

static const char *const NAV_TXT[FNOS_UI_PAGE_COUNT] = { "总览", "存储", "网络", "系统", "温度" };
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
static bool          s_diagnostics;
static fnos_status_t s_st;
static int64_t       s_seq;
static float         s_cpu_buf[UI_HIST], s_mem_buf[UI_HIST];
static float         s_rx_buf[UI_HIST],  s_tx_buf[UI_HIST];
static kk_series_t   s_cpu, s_mem, s_rx, s_tx;

/* 配对界面：s_pair_open 只表示"这张卡现在盖在系统页上"，真正的阶段由 fnos_pair 的
   状态机决定（见 pair_stage()）——不要在本地再实现一套状态机，否则两边会漂。 */
static bool          s_pair_open;
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
static void pair_build(lv_obj_t *page);
static void pair_btn_cb(lv_event_t *e);
static void wifi_btn_cb(lv_event_t *e);
static void wifi_build(lv_obj_t *page);
static void pair_show(bool on);
static void pair_refresh(void);
/* 左栏的表面色由 theme_apply() 统一落地（建树、换页、夜间、配对状态都走它一处），
   而它的定义在 build_rail 之前，这里不需要额外的前向声明。 */

/* ── 小工具 ───────────────────────────────────────────────────────── */
/* 160 字节对中文来说太小了：一个汉字 3 字节，稍长的一句话就会被**截断在多字节
   序列中间**，画出来是乱码或豆腐块，而且不报错。配对卡片那段"接受之后…"的说明
   有 200 多字节，就是这么被切断的（预览的字形审计报 U+0000 才暴露出来）。
   384 字节够放 120 多个汉字；再长就该直接用 lv_label_set_text 而不是走格式化。 */
static void set_txt(lv_obj_t *l, const char *fmt, ...)
{
    if (!l) return;
    char b[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    lv_label_set_text(l, b);
}

static lv_obj_t *mk_label(lv_obj_t *parent, int x, int y, int w, int h,
                          const lv_font_t *f, uint32_t rgb, const char *txt)
{
    lv_obj_t *l = kk_label_create(parent, kk_rect(0, 0, 0, 0, x, y, w, h));
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    if (txt) lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *mk_right(lv_obj_t *parent, int x, int y, int w, int h,
                          const lv_font_t *f, uint32_t rgb)
{
    lv_obj_t *l = mk_label(parent, x, y, w, h, f, rgb, NULL);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    return l;
}

/* 顶栏状态点的颜色 + 同色光晕：光晕让"在线/离线"在余光里也能被看见，
   但文字仍是主编码（发光只是冗余，不承担唯一含义）。chip 的第 0 个子对象就是状态点。 */
static void chip_set_glow(lv_obj_t *chip, const char *txt, uint32_t rgb)
{
    kk_chip_set(chip, txt, kk_c(rgb));
    if (!chip) return;
    lv_obj_t *dot = lv_obj_get_child(chip, 0);
    if (dot) kk_glow(dot, rgb, KK_GLOW_OPA);
}

/* 卡片表面（v8）：纯色 + 10% 白发丝边。
   **这里不能做竖向渐变** —— 原因不是审美，是 RGB565 的量化：KK_SURF_T(0x272A2F) →
   KK_SURF_B(0x17191C) 只差 16 级亮度，在 470px 高的卡片上只量得出 7 条色阶，而相邻
   色阶的舍入误差落在**不同通道**（阶差依次 −G、−B、−R、−G、−B、−R），屏幕上出现的
   就不是"深浅渐隐"而是一条条偏绿/偏蓝/偏红的横条 —— 用户看到的"花花绿绿"就是它。
   LVGL 的渐变不做抖动，RGB565 也没给抖动留余量。分层靠表面色阶，不靠渐变。
   发丝边 10% 是 RGB565 下看得见的下限（7% 被吃掉，见 tools/preview/style_proof.c 实测）。 */
static void card_surface(lv_obj_t *c, lv_opa_t edge)
{
    lv_obj_set_style_bg_color(c, lv_color_hex(KK_SURF_T), 0);
    lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, KK_RADIUS, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_white(), 0);
    lv_obj_set_style_border_opa(c, edge, 0);
}

static lv_obj_t *mk_card(lv_obj_t *page, int x, int y, int w, int h, const char *title)
{
    lv_obj_t *c = kk_panel_create(page, kk_rect(0, 0, 0, 0, x, y, w, h));
    /* 卡片自己绝不滚动：内容越界必须是"看得见的越界"（audit_bounds 会报），
       不能被自动滚动悄悄藏起来 —— 那会让越界 bug 在预览里隐身。 */
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    card_surface(c, KK_EDGE_OPA);
    /* 记账：把"哪张卡是哪个 build 函数建的"挂在对象上。几何审计报 `card overlap:
       两串坐标` 时，人得花二十分钟猜是谁（曾经真的这么查过一次）—— 有了这个 tag，
       预览能把 `build_p3/dock` 直接打出来。preview 用，设备端零成本。 */
    lv_obj_set_user_data(c, (void *)__builtin_FUNCTION());
    if (getenv("KK_CARD_TRACE"))
        fprintf(stderr, "[mk] want x=%d y=%d w=%d h=%d got x=%d y=%d w=%d h=%d\n",
                x, y, w, h, (int)lv_obj_get_x(c), (int)lv_obj_get_y(c),
                (int)lv_obj_get_width(c), (int)lv_obj_get_height(c));
    if (s_ui.ncards < UI_CARDS) s_ui.cards[s_ui.ncards++] = c;
    if (title) mk_label(c, 16, 12, w - 32, 22, &ui_font_cjk_16, KK_T1, title);
    return c;
}

/* 通用按钮：kk_button_create 只给一块可点的面，文案/配色在这里统一补上。 */
static lv_obj_t *mk_btn(lv_obj_t *parent, int x, int y, int w, int h,
                        const lv_font_t *f, uint32_t txt_rgb, uint32_t bg_rgb, const char *txt)
{
    lv_obj_t *b = kk_button_create(parent, kk_rect(0, 0, 0, 0, x, y, w, h));
    lv_obj_set_style_bg_color(b, lv_color_hex(bg_rgb), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, KK_RADIUS_SM, 0);
    lv_obj_t *l = kk_label_create(b, kk_rect(0, 0, 0, 0, 0, 0, w - 4, f->line_height));
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(txt_rgb), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    if (txt) lv_label_set_text(l, txt);
    return b;
}

static lv_obj_t *mk_page(lv_obj_t *content)
{
    lv_obj_t *p = kk_panel_create(content, kk_rect(0, 0, 0, 0, 0, 0, UI_CONTENT_W, UI_CONTENT_H));
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static void mk_row(ui_row_t *r, lv_obj_t *parent, int x, int y, int w, int h,
                   bool with_bar, int bar_h)
{
    /* with_bar = 存储卷那种"主行 + 副行 + 进度条"的行；副行右端止于数值列左侧
       （数值列占 KK_STATUS_W + KK_PAD），否则副行会钻到百分比下面。 */
    /* 行内三段（主行 / 副行 / 进度条）全部按 kk_layout.h 的行内常量排，
       右侧数值列右缘落在 x + w - KK_PAD（= 卡片右轴）。 */
    r->name = mk_label(parent, x, y + KK_ROW_NAME_Y, w - KK_PAD - KK_STATUS_W, 20,
                       &ui_font_cjk_16, KK_T2, NULL);
    r->detail = mk_label(parent, x, y + KK_ROW_SUB_Y,
                         with_bar ? w - KK_PAD - KK_STATUS_W - KK_INLINE : w - KK_PAD,
                         KK_ROW_SUB_H, &ui_font_cjk_12, KK_T3, NULL);
    r->pct = mk_right(parent, x + w - KK_PAD - KK_STATUS_W, y + KK_ROW_NAME_Y, KK_STATUS_W, 20,
                      &ui_font_cjk_16, KK_T2);
    if (with_bar) {
        (void)h; (void)bar_h;
        kk_bar_create(&r->bar, parent,
                      kk_rect(0, 0, 0, 0, x, y + KK_ROW_BAR_Y, w - KK_INLINE, KK_ROW_BAR_H),
                      kk_c(KK_OK), KK_ROW_BAR_H / 2);
        r->has_bar = true;
    }
}

/* 磁盘/容器使用上下两行，详情占满宽度，隐藏不适用的百分比。 */
static void row_full_detail(ui_row_t *r, int x, int w, int name_w)
{
    (void)name_w;
    r->full_detail = true;
    lv_obj_set_width(r->name, w);
    lv_obj_set_x(r->detail, x);
    /* 副行占满行宽，但**右端让出数值列**：RAID 行右侧有"正常/降级"状态列，
       副行写满会钻到它下面（audit_bounds 报 text overlap 64x14）。 */
    lv_obj_set_width(r->detail, w - KK_PAD - KK_STATUS_W - KK_INLINE);
    /* 这些行本来就不显示百分比，**那就别建它**：以前是建出来再打上 HIDDEN，
       于是磁盘 + 容器那些行各留一个永远不显示的 80x24 空标签（实测 82 个）。
       `set_txt()`/`row_visible()` 都已经能接受 NULL，直接删掉即可。 */
    if (r->pct) { lv_obj_delete(r->pct); r->pct = NULL; }
}

static void row_visible(ui_row_t *r, bool on)
{
    lv_obj_t *objs[3] = { r->name, r->detail, r->pct };
    for (int i = 0; i < 3; i++) {
        if (i == 2 && r->full_detail) continue;
        if (!objs[i]) continue;
        if (on) lv_obj_remove_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!r->has_bar) return;
    lv_obj_t *bars[2] = { r->bar.track, r->bar.fill };
    for (int i = 0; i < 2; i++) {
        if (!bars[i]) continue;
        if (on) lv_obj_remove_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* 行池收拢：v9 起行按真实条数**逐个下移**（不再把用不到的行留在下标位置）。
   之前行距 88、用不到的行挂在 y=352…704 —— 这些行被列表裁掉一半、越出卡片，
   既不显示又污染"从未露面"统计（基线 56 就是这么来的）。现在行距 60，
   若还留在原位就会**压到卡片外面**（audit_bounds 直接报 child out of parent），
   所以收拢是必须的，不是美化。 */
static void row_group_move(ui_row_t *r, int y)
{
    lv_obj_t *objs[3] = { r->name, r->detail, r->pct };
    for (int i = 0; i < 3; i++) {
        if (i == 2 && r->full_detail) continue;
        if (objs[i]) lv_obj_set_y(objs[i], y + (i == 0 ? KK_ROW_NAME_Y : KK_ROW_SUB_Y));
    }
    if (!r->has_bar) return;
    if (r->bar.track) lv_obj_set_y(r->bar.track, y + KK_ROW_BAR_Y);
    if (r->bar.fill)  lv_obj_set_y(r->bar.fill,  y + KK_ROW_BAR_Y);
}

static void row_set(ui_row_t *r, const char *name, const char *detail, const char *pct, float bar_pct)
{
    set_txt(r->name, "%s", name ? name : "");
    set_txt(r->detail, "%s", detail ? detail : "");
    set_txt(r->pct, "%s", pct ? pct : "");
    if (r->has_bar) kk_bar_set_fixed(&r->bar, bar_pct, kk_c(bar_pct >= 90 ? KK_DANGER : bar_pct >= 80 ? KK_WARN : KK_OK));
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

/* ── 建树 ─────────────────────────────────────────────────────────── */
static void nav_cb(lv_event_t *e);

static ui_list_t mk_list_at(lv_obj_t *card, int x, int y, int w, int h)
{
    lv_obj_t *list = kk_panel_create(card, kk_rect(0, 0, 0, 0, x, y, w, h));
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    /* 滚动条从右缘内缩 KK_SCROLL_INSET：LVGL 用 LV_PART_SCROLLBAR 的 pad_right
       当内缩量，不参与 MAIN 布局，所以内容不会被推移。 */
    lv_obj_set_style_width(list, KK_SCROLL_W, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, KK_SCROLL_INSET, LV_PART_SCROLLBAR);
    ui_list_t l;
    l.obj   = list;
    /* 内容右界再收 KK_SCROLL_GAP，让右对齐数值/状态列给滚动条让出位置
       （几何见 kk_theme.h 的 KK_SCROLL_*）。 */
    l.w     = w - KK_SCROLL_GAP;
    l.bar_w = w;
    return l;
}

/* 卡片里的滚动列表：位置按"卡内左轴 + 卡头下沿"的固定关系算。
   温度页要在一张卡里并排三列，所以把 x/y 也放开成参数（mk_list 保持不变）。 */
static ui_list_t mk_list(lv_obj_t *card, int w, int h)
{
    return mk_list_at(card, 16, 48, w - 32, h - 60);
}

static void diagnostics_cb(lv_event_t *e)
{
    (void)e;
    pair_show(false);
    s_diagnostics = !s_diagnostics;
    if (s_diagnostics) lv_obj_remove_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);
    set_txt(s_ui.diagnostics_label, "%s", s_diagnostics ? "返回系统" : "采集诊断");
}

/* 左栏的表面色集中在这里：建树、换页、夜间切换都调它，保证三处不漂。
   三档层次：rail 最深（内凹槽）→ 导航项浮起一档 → 选中项再抬一档 + 品牌蓝叠加。 */
static void theme_apply(void)
{
    if (s_ui.rail) {
        /* 纯色：rail 高 600px，渐变在这里只会量出更多偏色横条（见 card_surface 注释） */
        lv_obj_set_style_bg_color(s_ui.rail, lv_color_hex(s_night ? KK_RAIL_N : KK_RAIL_DAY), 0);
        lv_obj_set_style_bg_grad_dir(s_ui.rail, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(s_ui.rail, LV_OPA_COVER, 0);
    }
    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        lv_obj_t *b = s_ui.nav[i];
        if (!b) continue;
        bool on = (i == s_page && !s_pair_open);
        /* 纯色 + 选中态叠蓝：叠蓝那层用 bg_opa 压到 21%，露出下面的导航项底色。
           （原来用渐变时 bg_opa 与 bg_grad_opa 得分别设，改成纯色后只剩一个。） */
        lv_obj_set_style_bg_color(b, lv_color_hex(on ? KK_NAV_SEL : KK_NAV), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        /* 选中项描边加到 34% 白（普通项 10%）：这是"焦点环"，
           和左侧蓝条、白图标一起冗余编码"当前页"。 */
        lv_obj_set_style_border_opa(b, on ? KK_FOCUS_RING_OPA : KK_EDGE_CTRL, 0);
        lv_obj_set_style_radius(b, KK_RADIUS, 0);
        /* 图标+文字作为一组在 78 高的块里垂直居中：图标 24（y=15）+ 间距 6 + 文字盒 30（y=45），
           上下各留 15。文字盒比字形高，居中后字形落在块的视觉中线上。 */
        /* 图标+文字作为一组在块里垂直居中：几何只认 kk_layout.h 的 KK_NAV_*。
           这里**必须跟着一起改** —— 它们是同一份几何的第二处落点，只改 build_rail
           会被 theme_apply() 在开机时按旧坐标复位（导航从 4 项变 5 项时踩过：
           标签被放回 43，直接越出 60 高的按钮，被预览的越界审计抓住）。 */
        if (s_ui.nav_icon[i]) lv_obj_set_pos(s_ui.nav_icon[i], 17, KK_NAV_ICON_Y);
        if (s_ui.nav_lbl[i])  lv_obj_set_pos(s_ui.nav_lbl[i], 0, KK_NAV_LBL_Y);
        if (s_ui.nav_mark[i]) lv_obj_set_pos(s_ui.nav_mark[i], 0, KK_NAV_MARK_Y);
        if (s_ui.nav_mark[i]) {
            lv_obj_set_style_shadow_opa(s_ui.nav_mark[i], on ? 130 : 0, 0);
            if (on) lv_obj_remove_flag(s_ui.nav_mark[i], LV_OBJ_FLAG_HIDDEN);
            else    lv_obj_add_flag(s_ui.nav_mark[i], LV_OBJ_FLAG_HIDDEN);
        }
        /* 选中项的图标用白色：3px 蓝条离图标有 20px 远，近处唯一能确认"就是这一页"的
           就是图标本身。21% 蓝底 + 蓝图标对比不足，白图标在蓝底上才立得住。 */
        kk_icon_set_color(s_ui.nav_icon[i], kk_c(on ? KK_T1 : KK_T2));
        if (s_ui.nav_lbl[i]) {
            lv_obj_set_style_text_color(s_ui.nav_lbl[i], lv_color_hex(on ? KK_T1 : KK_T2), 0);
        }
    }
    if (s_ui.pair_btn) {
        /* 配对按钮不是一个页面，但仍然是一个模块：给它和导航项同一档表面 + 发丝边。
           它是**入口**不是状态：文字用主文本色，状态由左边的状态点表示
           （橙色点 = 未配对）—— 拿整块橙色标题当状态，会把入口染成告警。 */
        lv_obj_set_style_bg_color(s_ui.pair_btn, lv_color_hex(s_pair_open ? KK_NAV_SEL : KK_NAV), 0);
        lv_obj_set_style_bg_grad_dir(s_ui.pair_btn, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(s_ui.pair_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_ui.pair_btn, lv_color_white(), 0);
        lv_obj_set_style_border_width(s_ui.pair_btn, 1, 0);
        lv_obj_set_style_border_opa(s_ui.pair_btn, s_pair_open ? KK_FOCUS_RING_OPA : KK_EDGE_CTRL, 0);
        lv_obj_set_style_radius(s_ui.pair_btn, KK_RADIUS, 0);
        lv_obj_set_style_text_color(s_ui.pair_btn_lbl, lv_color_hex(KK_T1), 0);
        lv_obj_set_style_bg_opa(s_ui.pair_btn_lbl, LV_OPA_TRANSP, 0);
        if (s_ui.pair_btn_lbl) lv_obj_center(s_ui.pair_btn_lbl);
        if (s_ui.pair_dot) {   /* 入口的状态点：颜色随配对阶段走（见 pair_refresh） */
            lv_obj_set_style_bg_color(s_ui.pair_dot, lv_color_hex(s_pair_alert), 0);
            lv_obj_set_style_shadow_color(s_ui.pair_dot, lv_color_hex(s_pair_alert), 0);
            /* 只有"需要你动手"（未配对 / 失败）才发光；其余保持静止，免得侧栏一直闪 */
            bool alert = (s_pair_alert == KK_WARN || s_pair_alert == KK_DANGER);
            lv_obj_set_style_shadow_opa(s_ui.pair_dot, alert ? KK_GLOW_OPA : 0, 0);
        }
    }
    if (s_ui.logo_lbl) lv_obj_set_style_bg_opa(s_ui.logo_lbl, LV_OPA_TRANSP, 0);
}

static void build_rail(lv_obj_t *scr)
{
    lv_obj_t *rail = kk_panel_create(scr, kk_rect(0, 0, 0, 0, 0, 0, KK_RAIL_W, KK_SCR_H));
    s_ui.rail = rail;
    if (s_ui.ncards < UI_CARDS) s_ui.cards[s_ui.ncards++] = rail;
    /* 右沿发丝边：把 rail 和内容区切开（分组靠边界，不靠间距猜） */
    lv_obj_set_style_border_color(rail, lv_color_white(), 0);
    lv_obj_set_style_border_width(rail, 1, 0);
    lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_opa(rail, KK_EDGE_RAIL, 0);

    lv_obj_t *logo = kk_panel_box(rail, 18, 16, 40, 40, KK_BLUE, KK_RADIUS);
    s_ui.logo_lbl = mk_label(logo, 0, 10, 40, 22, &ui_font_txt_15, KK_T1, "FN");
    lv_obj_set_style_text_align(s_ui.logo_lbl, LV_TEXT_ALIGN_CENTER, 0);

    /* 导航图标：一页一个，条数必须与页数一致 —— 少了会在建树时越界读到垃圾指针
       （NAV_TXT 那条断言只保证文案够，图标是另一张表）。 */
    static const int icons[] = { KK_ICON_OVERVIEW, KK_ICON_STORAGE, KK_ICON_TRAFFIC,
                                 KK_ICON_SERVICES, KK_ICON_THERMO };
    _Static_assert(sizeof(icons) / sizeof(icons[0]) == FNOS_UI_PAGE_COUNT,
                   "icons 条数必须等于 FNOS_UI_PAGE_COUNT");
    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        /* 五项导航的纵向几何见 kk_layout.h：72 + i*68，项高 60，末项下沿 404 */
        lv_obj_t *b = kk_button_create(rail, kk_rect(0, 0, 0, 0, 8,
                                                     KK_NAV_Y0 + i * KK_NAV_PITCH,
                                                     KK_RAIL_W - 16, KK_NAV_H));
        s_ui.nav[i] = b;
        /* 选中态 = 左侧 3px 品牌蓝条 + 同色光晕 + 抬高一档的表面（见 theme_apply）。
           不做整块不透明蓝底：那会让导航比数据更抢眼。 */
        s_ui.nav_mark[i] = kk_panel_box(b, 0, KK_NAV_MARK_Y, 3, KK_NAV_MARK_H, KK_BLUE, 2);
        lv_obj_set_style_shadow_color(s_ui.nav_mark[i], kk_c(KK_BLUE), 0);
        lv_obj_set_style_shadow_width(s_ui.nav_mark[i], 10, 0);
        lv_obj_set_style_shadow_opa(s_ui.nav_mark[i], 0, 0);
        s_ui.nav_icon[i] = kk_icon_create(b, kk_rect(0, 0, 0, 0, 17, KK_NAV_ICON_Y, 24, 24),
                                          icons[i], kk_c(KK_T2));
        s_ui.nav_lbl[i] = mk_label(b, 0, KK_NAV_LBL_Y, KK_RAIL_W - 18, 26, &ui_font_cjk_16, KK_T2, NAV_TXT[i]);
        lv_obj_set_style_text_align(s_ui.nav_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    /* 分组分隔线：导航 / 配对 —— 这里以下的东西不属于任何一页数据 */
    kk_panel_box(rail, 8, KK_RAIL_SEP_Y, KK_RAIL_W - 16, 1, KK_LINE, 0);
    /* 配对入口放侧栏底部：它不属于任何一页数据，但必须随时够得着。
       它是**入口**不是状态：文字用主文本色，状态另用一个状态点表示（见 theme_apply）。 */
    s_ui.pair_btn = kk_button_create(rail, kk_rect(0, 0, 0, 0, 8, KK_RAIL_PAIR_Y, KK_RAIL_W - 16, 60));
    /* 标签盒：宽 46 = 三个中文字（15px 字号下 "已配对" 要 45px，40 会被裁成"已配"），
       高 38 = 该字号的行高（预览的 text overflow 断言查这两项）。 */
    s_ui.pair_btn_lbl = mk_label(s_ui.pair_btn, 0, 0, 46, ui_font_cjk_12.line_height, &ui_font_cjk_12, KK_T1, "配对");
    lv_obj_set_style_text_align(s_ui.pair_btn_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_ui.pair_btn_lbl);
    /* 状态点在左边：填充圆点直径 8，光晕靠 shadow 外扩（与顶栏"在线"胶囊同一手法） */
    s_ui.pair_dot = kk_panel_box(s_ui.pair_btn, 25, 8, KK_DOT, KK_DOT, KK_WARN, KK_RADIUS_PILL);
    lv_obj_set_style_shadow_color(s_ui.pair_dot, kk_c(KK_WARN), 0);
    lv_obj_set_style_shadow_width(s_ui.pair_dot, KK_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(s_ui.pair_dot, 0, 0);
    lv_obj_add_event_cb(s_ui.pair_btn, pair_btn_cb, LV_EVENT_CLICKED, NULL);

    kk_panel_box(rail, 8, 538, KK_RAIL_W - 16, 1, KK_LINE, 0);
    mk_label(rail, 14, 556, 50, 22, &ui_font_txt_12, KK_T3, "NAS");
}

static void build_header(lv_obj_t *scr)
{
    lv_obj_t *h = kk_panel_create(scr, kk_rect(0, 0, 0, 0, KK_RAIL_W, 0, UI_CONTENT_W, KK_HEAD_H));
    s_ui.h_host = mk_label(h, 16, 2, 380, 24, &ui_font_cjk_20, KK_T1, "fnOS");
    s_ui.h_ep   = mk_label(h, 16, 32, 400, 16, &ui_font_cjk_12, KK_T3, "等待采集");
    kk_signal_bars(h, kk_rect(0, 0, 0, 0, 760, 18, 32, 24), s_ui.h_bars);
    s_ui.h_chip = kk_chip_create(h, kk_rect(0, 0, 0, 0, 802, 12, 130, 32), kk_c(KK_OFF));
    chip_set_glow(s_ui.h_chip, "等待数据", KK_T3);
    s_ui.diagnostics_button = kk_button_create(h, kk_rect(0, 0, 0, 0, 588, 4, 148, 48));
    lv_obj_set_style_bg_color(s_ui.diagnostics_button, kk_c(KK_S2), 0);
    lv_obj_set_style_bg_opa(s_ui.diagnostics_button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ui.diagnostics_button, KK_RADIUS, 0);
    s_ui.diagnostics_label = mk_label(s_ui.diagnostics_button, 14, 12, 120, 24, &ui_font_cjk_16, KK_T2, "采集诊断");
    lv_obj_add_event_cb(s_ui.diagnostics_button, diagnostics_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_ui.diagnostics_button, LV_OBJ_FLAG_HIDDEN);

    /* 顶栏常驻的配网入口。位置与"采集诊断"同一条横带（430+150=580 < 588），
       两者不重叠；诊断按钮只在系统页露面，配网按钮一直在 —— 出厂固件没凭据时
       它就是屏幕上唯一橙色的东西，用户照着点即可。 */
    s_ui.wifi_btn = kk_button_create(h, kk_rect(0, 0, 0, 0, 430, 4, 150, 48));
    lv_obj_set_style_bg_color(s_ui.wifi_btn, kk_c(KK_S2), 0);
    lv_obj_set_style_bg_opa(s_ui.wifi_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ui.wifi_btn, KK_RADIUS, 0);
    s_ui.wifi_btn_lbl = mk_label(s_ui.wifi_btn, 12, 12, 126, 24,
                                 &ui_font_cjk_16, KK_T2, "Wi-Fi 未配置");
    lv_obj_add_event_cb(s_ui.wifi_btn, wifi_btn_cb, LV_EVENT_CLICKED, NULL);
}

static void mk_kpi(ui_kpi_t *k, lv_obj_t *card, const char *title,
                   bool with_bar, const char *unit)
{
    /* v9：卡内只有一条左轴（KK_PAD）与一条右轴（w-KK_PAD）。
       值用 num_44（行高 52）、单位用 num_20（行高 24），两者**共基线**：
       单位行框顶 = 值行框顶 + (52-24)/2 = 值顶 + 14，间距恒为 KK_INLINE(4)。
       这样四张卡的"值→单位"关系完全一致，不会一张贴得远一张贴得近。 */
    mk_label(card, KK_PAD, KK_KPI_LBL_Y, KK_COL4 - 2 * KK_PAD, 24, &ui_font_cjk_16, KK_T2, title);
    /* 值与单位都**左对齐、间距恒 KK_INLINE(4)**：距值多远只由字号决定，与值有几位无关。
       曾把单位右对齐到卡右缘 —— 看着"对齐了"，实际是"12.2"离"MB/s" 40px、"37"离"%"
       130px（用户原话："看着像两个不相干的元素"）。也试过把单位框定宽 48 右对齐，
       结果"MB/s"被截成"M…"。轴线：**卡片只有一条左轴**，值与单位都贴它。 */
    k->value = mk_label(card, KK_PAD, KK_KPI_VAL_Y, 120, 52, &ui_font_num_44, KK_T1, "-");
    k->unit  = mk_label(card, KK_PAD + 120 + KK_INLINE, KK_KPI_VAL_Y + 14, 64, 24,
                        &ui_font_num_20, KK_T3, NULL);
    set_txt(k->unit, "%s", unit);
    k->sub   = mk_label(card, KK_PAD, KK_KPI_SUB_Y, KK_COL4 - 2 * KK_PAD, 18,
                        &ui_font_cjk_12, KK_T3, "");
    if (with_bar) {
        kk_bar_create(&k->bar, card,
                      kk_rect(0, 0, 0, 0, KK_PAD, KK_KPI_BAR_Y, KK_COL4 - 2 * KK_PAD, KK_BAR_H),
                      kk_c(KK_OK), 2);
        k->has_bar = true;
    }
}

static void build_p0(lv_obj_t *page)
{
    /* v9：三块页恒等式 12 + 64 + 12 + 178 + 12 + 304 + 12 = 544。
       页头摘要的左块与右块同轴（都在 y=16 起），并排的两块文字**行框顶边必须相同**。 */
    lv_obj_t *health = mk_card(page, KK_MARGIN, KK_CARD_Y(0), KK_CARD_W, KK_CARD_H3, NULL);
    /* 页头 44 高（原 64）：两行文字要在卡内居中 —— 上 4、行高 20，组间 2，下 4。
       左块状态（cjk_16）与右块说明（cjk_12）的**行框顶边必须相同**，否则细看会"错位"。 */
    s_ui.health = mk_label(health, KK_PAD, 0, 204, 20, &ui_font_cjk_16, KK_T1, "等待数据");
    s_ui.overview_note = mk_right(health, 228, 2, KK_CARD_W - 228 - KK_PAD, 20,
                                  &ui_font_cjk_12, KK_T3);

    static const char *const title[] = { "CPU 使用率", "内存使用率", "最高温度", "网络下行" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = mk_card(page, KK_MARGIN + i * (KK_COL4 + KK_GAP), KK_CARD_Y(1),
                              KK_COL4, KK_CARD_HT, NULL);
        mk_kpi(&s_ui.kpi[i], c, title[i], i < 3, i < 2 ? "%" : (i == 2 ? "°C" : "KB/s"));
    }

    lv_obj_t *tr = mk_card(page, KK_MARGIN, KK_CARD_Y(2), 608, KK_CARD_HB, NULL);
    s_ui.tr_title = mk_label(tr, KK_PAD, 8, 448, KK_CARD_TITLE, &ui_font_cjk_16, KK_T1,
                             "资源趋势 · 最近 3 分钟");
    /* 趋势卡内部几何全部由 kk_trend_layout(卡高) 现算：读数行贴卡底、
       绘图区吃掉中间剩余空间（旧版绘图区只占 152 高，卡底还留着一大块死区；
       而且读数行用的是"卡高 272"时的常量，卡其实只有 260 高 → 整行掉出卡外）。 */
    kk_trend_layout_t tl = kk_trend_layout(KK_CARD_HB);
    kk_trend_create(&s_ui.tr_cpu, tr,
                    kk_rect(0, 0, 0, 0, tl.axis_w, tl.plot_y, 608 - tl.axis_w - KK_PAD, tl.plot_h),
                    kk_c(KK_S_CPU), UI_HIST, 0, 3, 0);
    kk_trend_create(&s_ui.tr_mem, tr,
                    kk_rect(0, 0, 0, 0, tl.axis_w, tl.plot_y, 608 - tl.axis_w - KK_PAD, tl.plot_h),
                    kk_c(KK_S_MEM), UI_HIST, 0, 0, 0);
    kk_trend_range(&s_ui.tr_cpu, 100); kk_trend_range(&s_ui.tr_mem, 100);
    if (s_ui.tr_cpu.line) lv_obj_set_style_line_color(s_ui.tr_cpu.line, kk_c(KK_S3), LV_PART_MAIN);
    /* Y 轴刻度贴绘图区上/下沿，X 轴端点标签在绘图区正下方 */
    mk_label(tr, 0, tl.plot_y, 40, 18, &ui_font_txt_12, KK_T3, "100%");
    mk_label(tr, 0, tl.plot_y + tl.plot_h - 18, 40, 18, &ui_font_txt_12, KK_T3, "0%");
    /* X 轴端点标签（较早 / 现在）只在高卡上画：矮卡上绘图区下沿离读数行只有十几像素，
       标签会压到读数行上（audit_text_overlap 抓到过"现在 / 等待数据"）。 */
    if (tl.xaxis_y >= 0) {
        int xy = tl.xaxis_y;
        mk_label(tr, tl.axis_w, xy, 120, 18, &ui_font_cjk_12, KK_T4, "较早");
        lv_obj_t *now = mk_right(tr, 608 - KK_PAD - 120, xy, 120, 18, &ui_font_cjk_12, KK_T4);
        set_txt(now, "%s", "现在");
    }
    /* 读数行（不是图例）：点名贴左轴，读数贴右轴右对齐 —— 值永远在同一个 x 收尾。 */
    mk_label(tr, KK_PAD, tl.readout_y, 120, 24, &ui_font_cjk_16, KK_S_CPU, "CPU");
    s_ui.tr_cpu_lbl = mk_right(tr, 144, tl.readout_y, 608 - 144 - KK_PAD, 24, &ui_font_cjk_16, KK_T2);
    mk_label(tr, KK_PAD, tl.readout_y + tl.readout_gap, 120, 24, &ui_font_cjk_16, KK_S_MEM, "内存");
    s_ui.tr_mem_lbl = mk_right(tr, 144, tl.readout_y + tl.readout_gap,
                               608 - 144 - KK_PAD, 24, &ui_font_cjk_16, KK_T2);

    lv_obj_t *cap = mk_card(page, 632, KK_CARD_Y(2), 304, KK_CARD_HB, "存储摘要");
    kk_panel_box(cap, KK_PAD, 36, 304 - 2 * KK_PAD, 1, KK_LINE, 0);
    s_ui.capacity = mk_label(cap, KK_PAD, 40, 272, 52, &ui_font_num_44, KK_T1, "-");
    s_ui.capacity_detail = mk_label(cap, KK_PAD, 100, 272, 40, &ui_font_cjk_16, KK_T3, "等待数据");
    kk_bar_create(&s_ui.capacity_bar, cap, kk_rect(0, 0, 0, 0, KK_PAD, 148, 272, KK_BAR_H_HERO), kk_c(KK_OK), 4);
    s_ui.storage_note = mk_label(cap, KK_PAD, 168, 272, 92, &ui_font_cjk_16, KK_T3, "");
    lv_label_set_long_mode(s_ui.storage_note, LV_LABEL_LONG_WRAP);
}

static void build_p1(lv_obj_t *page)
{
    /* 3 列 × 2 行栅格（几何全部来自度量表，见 kk_metrics.c 的"两块页"注释）：
         [已用/总容量/可用 三栏页头 924×48]
         [存储卷 924×202  ←─ 占满整行，3 行不滚动]
         [阵列健康 548×202][磁盘活动 364×252]
       "容量摘要"从一张独立卡改成页头一条：三栏只有"标签 + 数值"两行，
       单独占 88px 高是浪费，而那 88px 正好够列表行从 48 长到 54（不重叠的下限）。 */
    lv_obj_t *summary = mk_card(page, KK_MARGIN, KK_CARD_Y(0), KK_CARD_W, KK_CARD_H3, NULL);
    static const char *const titles[] = { "已用空间", "总容量", "可用空间" };
    for (int i = 0; i < 3; i++) {
        int x = KK_PAD + i * KK_SEG3;
        mk_label(summary, x, 0, KK_SEG3 - KK_PAD, 20, &ui_font_cjk_16, KK_T2, titles[i]);
        /* 字号按卡高选：num_32 的"−"字形盒 41px 装不进 44 高的页头（预览报
           `child out of parent`）；num_20 的盒 26px，上下各留 9px 正好居中。
           页头三个总量是"速览"不是"凝视"，20px 在这屏（169.5 PPI ≈ 12pt）够读。 */
        s_ui.storage_value[i] = mk_label(summary, x, 18, KK_SEG3 - KK_PAD, 26,
                                         &ui_font_num_20, KK_T1, "-");
    }
    /* 栏间分隔线：三栏是"同类并列"，一条低对比竖线即可，不构成第三条轴 */
    kk_panel_box(summary, KK_PAD + KK_SEG3 - 12, 8, 1, 36, KK_LINE, 0);
    kk_panel_box(summary, KK_PAD + 2 * KK_SEG3 - 12, 8, 1, 36, KK_LINE, 0);

    lv_obj_t *vol = mk_card(page, KK_MARGIN, KK_CARD_Y2, 548, KK_CARD_HLA, "存储卷 · 上下滑动");
    ui_list_t list = mk_list(vol, 548, KK_CARD_HLA);
    for (int i = 0; i < UI_ROWS_VOL; i++)
        mk_row(&s_ui.vol1[i], list.obj, 0, i * KK_LIST_ROW, list.w, KK_LIST_ROW, true, 0);
    s_ui.vol_empty = mk_label(list.obj, 0, 8, list.w, 24, &ui_font_cjk_16, KK_T3, "未采集到存储卷");

    lv_obj_t *raid = mk_card(page, 572, KK_CARD_Y2C, 364, KK_CARD_HLA, "阵列健康");
    list = mk_list(raid, 364, KK_CARD_HLA);
    for (int i = 0; i < UI_ROWS_RAID; i++)
        mk_row(&s_ui.raid[i], list.obj, 0, i * KK_LIST_ROW, list.w, KK_LIST_ROW, false, 0);
    s_ui.raid_empty = mk_label(list.obj, 0, 8, list.w, 24, &ui_font_cjk_16, KK_T3, "未采集到阵列");

    lv_obj_t *disk = mk_card(page, 572, KK_CARD_Y2D, 364, KK_CARD_HLB, "磁盘活动");
    list = mk_list(disk, 364, KK_CARD_HLB);
    for (int i = 0; i < UI_ROWS_DISK; i++) {
        mk_row(&s_ui.disk[i], list.obj, 0, i * KK_LIST_ROW, list.w, KK_LIST_ROW, false, 0);
        row_full_detail(&s_ui.disk[i], 0, list.w, 0);
    }
    s_ui.disk_empty = mk_label(list.obj, 0, 8, list.w, 24, &ui_font_cjk_16, KK_T3, "未采集到磁盘活动");
}

static void build_p2(lv_obj_t *page)
{
    /* 四张速率卡高 160；数值带单位用 num_32，完整显示在同一行。 */
    static const char *const title[] = { "上行速率", "下行速率", "双向合计", "采集延迟" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = mk_card(page, KK_MARGIN + i * (KK_COL4 + KK_GAP), KK_CARD_Y(0),
                              KK_COL4, KK_CARD_HT, NULL);
        uint32_t col = i == 0 ? KK_S_UP : i == 1 ? KK_S_DOWN : KK_T1;
        mk_label(c, KK_PAD, KK_KPI_LBL_Y, KK_COL4 - 2 * KK_PAD, 24, &ui_font_cjk_16, KK_T2, title[i]);
        s_ui.big[i].value = mk_label(c, KK_PAD, KK_KPI_VAL_Y, KK_COL4 - 2 * KK_PAD, 40, &ui_font_num_32, col, "-");
        s_ui.big[i].sub = mk_label(c, KK_PAD, 112, KK_COL4 - 2 * KK_PAD, 18, &ui_font_cjk_12, KK_T3, "");
    }

    /* 544 = 12 + 160 + 12 + 244 + 12 + 92 + 12.
       本页首块是 KPI 高度，不能套用首块为 64 的总览 CARD_Y。 */
    const int trend_y = KK_MARGIN + KK_CARD_HT + KK_GAP;
    const int trend_h = 244;
    const int info_y = trend_y + trend_h + KK_GAP;
    const int info_h = KK_BODY_H - info_y - KK_MARGIN;
    lv_obj_t *tr = mk_card(page, KK_MARGIN, trend_y, KK_CARD_W, trend_h, NULL);
    s_ui.net_title = mk_label(tr, KK_PAD, 8, 560, KK_CARD_TITLE, &ui_font_cjk_16, KK_T1,
                              "网络吞吐 · 最近 3 分钟");
    s_ui.net_axis = mk_right(tr, 588, 12, KK_CARD_W - 588 - KK_PAD, 20, &ui_font_cjk_12, KK_T3);
    /* 卡内 188 = 标题 8..32 ┊ 图区 36..138 ┊ 时间行 138..156 ┊ 读数行 160..184/188。
       **排完先加一遍下沿**：曾经把读数行排到 200，整行掉出卡片（audit_bounds 抓出）。 */
    kk_trend_layout_t tl = kk_trend_layout(trend_h);
    kk_trend_create(&s_ui.tr_tx, tr,
                    kk_rect(0, 0, 0, 0, tl.axis_w, tl.plot_y, KK_CARD_W - tl.axis_w - KK_PAD, tl.plot_h),
                    kk_c(KK_S_UP), UI_HIST, 0, 4, 0);
    kk_trend_create(&s_ui.tr_rx, tr,
                    kk_rect(0, 0, 0, 0, tl.axis_w, tl.plot_y, KK_CARD_W - tl.axis_w - KK_PAD, tl.plot_h),
                    kk_c(KK_S_DOWN), UI_HIST, 0, 0, 0);
    if (s_ui.tr_tx.line) lv_obj_set_style_line_color(s_ui.tr_tx.line, kk_c(KK_S3), LV_PART_MAIN);
    mk_label(tr, 0, tl.plot_y, 40, 18, &ui_font_txt_12, KK_T3, "MAX");
    mk_label(tr, 0, tl.plot_y + tl.plot_h - 18, 40, 18, &ui_font_txt_12, KK_T3, "0");
    if (tl.xaxis_y >= 0) {
        lv_obj_t *now = mk_right(tr, KK_CARD_W - KK_PAD - 120, tl.xaxis_y, 120, 18,
                                 &ui_font_cjk_12, KK_T4);
        set_txt(now, "%s", "现在");
    }
    mk_label(tr, KK_PAD, tl.readout_y, 120, 24, &ui_font_cjk_16, KK_S_UP, "上行");
    s_ui.tr_tx_lbl = mk_right(tr, 144, tl.readout_y, KK_CARD_W - 144 - KK_PAD, 24,
                              &ui_font_cjk_16, KK_T2);
    mk_label(tr, KK_PAD, tl.readout_y + tl.readout_gap, 120, 24, &ui_font_cjk_16, KK_S_DOWN, "下行");
    s_ui.tr_rx_lbl = mk_right(tr, 144, tl.readout_y + tl.readout_gap,
                              KK_CARD_W - 144 - KK_PAD, 24, &ui_font_cjk_16, KK_T2);

    /* 网卡信息条：左轴上的"接口名"是主值，右侧 4 格是同类并列的读数（栏间同距）。 */
    lv_obj_t *info = mk_card(page, KK_MARGIN, info_y, KK_CARD_W, info_h, NULL);
    s_ui.if_dot = kk_panel_box(info, KK_PAD, 30, 10, 10, KK_T4, LV_RADIUS_CIRCLE);
    mk_label(info, KK_PAD + 20, 20, 220, 18, &ui_font_cjk_12, KK_T4, "网卡接口");
    s_ui.if_name = mk_label(info, KK_PAD + 20, 40, 240, 40, &ui_font_num_32, KK_T2, "-");
    static const char *const ik[] = { "累计接收", "累计发送", "采集耗时", "采集成功率" };
    for (int i = 0; i < 4; i++) {
        int x = 296 + i * 158;
        mk_label(info, x, 24, 150, 18, &ui_font_cjk_12, KK_T4, ik[i]);
        s_ui.info_val[i] = mk_label(info, x, 44, 150, 28, &ui_font_num_20, KK_T2, "-");
    }
    s_ui.net_note = NULL;
}

static void build_p3(lv_obj_t *page)
{
    /* 摘要高 64，下方三列使用页面剩余的 444 高度。
       924 = 3×300 + 2×12；列表标题和底边统一对齐。
       ⚠ 摘要卡内两行文字必须在卡内：上 8 + 行高 22 + 组间 4 + 行高 20 + 下 8 = 62 ≤ 64。
       曾经把副行放在 y=42（盒 20）⇒ 下沿 62 越出卡片 111 到 130，被 audit_bounds 抓住。 */
    /* 页头摘要不设卡头标题：状态行本身就是这一卡的标题（"容器 8/8 运行 · 10 路温度 ·
       0 个事件"），再加一个"系统摘要"就是用两个标题说同一件事，而且会把状态行挤到
       规则线下面去（曾经因此重叠 2px，被 audit_bounds 抓住）。 */
    lv_obj_t *note = mk_card(page, KK_MARGIN, KK_CARD_Y(0), KK_CARD_W, KKM.h_note, NULL);
    s_ui.system_note = mk_label(note, KK_PAD, 0, KK_CARD_W - 2 * KK_PAD, 20, &ui_font_cjk_16, KK_T2, "等待数据");
    s_ui.system_note2 = mk_label(note, KK_PAD, 22, KK_CARD_W - 2 * KK_PAD, 20, &ui_font_cjk_12, KK_T3, "");

    /* 三列卡高与行高都来自度量表：卡高 = 列表顶 48 + 7 行 + 卡底 12，
       行高 = (可用高 − 48) / 7 ⇒ 卡片自己吃满页面高度（原来写死"填满剩余"反而留 220px 空白）。 */
    const int list_h = KK_CARD_H3COL;
    const int CW3 = (KK_CARD_W - 2 * KK_GAP) / 3;   /* 300 */
    lv_obj_t *dock = mk_card(page, KK_MARGIN, KK_CARD_Y(1), CW3, list_h, "容器服务");
    ui_list_t list = mk_list(dock, CW3, list_h);
    for (int i = 0; i < UI_ROWS_DOCK; i++) {
        mk_row(&s_ui.dock[i], list.obj, 0, i * KK_LIST_ROW3, list.w, KK_LIST_ROW3, false, 0);
        row_full_detail(&s_ui.dock[i], 0, list.w, 0);
    }
    s_ui.dock_empty = mk_label(list.obj, 0, 8, list.w, 24, &ui_font_cjk_16, KK_T3, "未采集到容器");

    lv_obj_t *temps = mk_card(page, KK_MARGIN + CW3 + KK_GAP, KK_CARD_Y(1), CW3, list_h, "硬件温度");
    list = mk_list(temps, CW3, list_h);
    for (int i = 0; i < UI_ROWS_TEMP; i++) {
        int y = i * KK_LIST_ROW3;
        /* 名称列要放得下"设备名 · 通道名"（enp1s0 · PHY / nvme2n1 · Composite），
           所以名称宽度由数值列反推，不再是写死的 120。 */
        s_ui.temp[i].name = mk_label(list.obj, 0, y + KK_ROW_NAME_Y,
                                     list.w - KK_TROW_VAL_W - KK_INLINE, 20, &ui_font_cjk_16, KK_T2, NULL);
        s_ui.temp[i].detail = mk_right(list.obj, list.w - KK_TROW_VAL_W, y + KK_ROW_NAME_Y,
                                       KK_TROW_VAL_W, 20, &ui_font_cjk_16, KK_T2);
        kk_bar_create(&s_ui.temp[i].bar, list.obj,
                      kk_rect(0, 0, 0, 0, 0, y + KK_ROW_BAR_Y, list.w - KK_INLINE, KK_ROW_BAR_H),
                      kk_c(KK_OK), KK_ROW_BAR_H / 2);
        s_ui.temp[i].has_bar = true;
    }
    s_ui.temp_empty = mk_label(list.obj, 0, 8, list.w, 24, &ui_font_cjk_16, KK_T3, "未采集到温度");

    lv_obj_t *al = mk_card(page, KK_MARGIN + 2 * (CW3 + KK_GAP), KK_CARD_Y(1), CW3, list_h, "告警与事件");
    list = mk_list(al, CW3, list_h);
    for (int i = 0; i < UI_ROWS_ALERT; i++) {
        s_ui.alert[i].detail = mk_label(list.obj, 0, i * 100, list.w, 92, &ui_font_cjk_16, KK_WARN, NULL);
        lv_label_set_long_mode(s_ui.alert[i].detail, LV_LABEL_LONG_WRAP);
    }
    s_ui.alert_none = mk_label(list.obj, 0, 8, list.w, 24, &ui_font_cjk_16, KK_T3, "等待数据");

    s_ui.diagnostics = mk_card(page, KK_MARGIN, KK_CARD_Y(0), KK_CARD_W, 520, "采集诊断");
    for (int i = 0; i < UI_AGENT_ROWS; i++) {
        mk_label(s_ui.diagnostics, 24, 64 + i * 46, 176, 30, &ui_font_cjk_20, KK_T3, AGENT_KEY[i]);
        s_ui.agent[i].detail = mk_label(s_ui.diagnostics, 220, 64 + i * 46, 672, 30, &ui_font_cjk_20, KK_T2, "-");
    }
    lv_obj_add_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);

    pair_build(page);   /* 同槽位的第二张整页卡，见 pair_show() */
    wifi_build(page);   /* 第三张：板上配网（出厂固件没有凭据时开机自动弹，见 ui_tick） */
}

/* ── P4：温度（全部传感器通道）──────────────────────────────────────
   为什么单独占一页：只显示"一个最高温度"时，75°C 那个数字很容易被读成整机温度，
   而它其实是网卡 PHY 的结温。用户的原话是"应该每一个能读到温度的传感器都显示出来，
   并标出是什么设备"。所以这一页把采集端报来的**每一路通道**都摊开，每路都写清
   "设备名 · 通道名"；三列并排，列内可滚动（列各有各的滚动条，互不牵动）。 */
static void build_p4(lv_obj_t *page)
{
    /* 摘要卡：与系统页摘要条同规格（KKM.h_note 高、两行、无分隔线）——
       卡高从 h3 缩到 44 后，原来的 12/40/42 三段会把第二行顶出卡片。 */
    lv_obj_t *note = mk_card(page, KK_MARGIN, KK_CARD_Y(0), KK_CARD_W, KKM.h_note, NULL);
    s_ui.temp_note  = mk_label(note, KK_PAD, 0, KK_CARD_W - 2 * KK_PAD, 20, &ui_font_cjk_16, KK_T2, "等待数据");
    s_ui.temp_note2 = mk_label(note, KK_PAD, 22, KK_CARD_W - 2 * KK_PAD, 20, &ui_font_cjk_12, KK_T3, "");

    const int list_y = KK_CARD_Y(1);
    const int list_h = KK_BODY_H - list_y - KK_MARGIN;
    lv_obj_t *card = mk_card(page, KK_MARGIN, list_y, KK_CARD_W, list_h, "温度传感器 · 全部通道");

    /* 三列：列宽 = (924 − 2×16 内边距 − 2×12 列间隙) / 3 = 289 */
    const int cw = (KK_CARD_W - 2 * KK_PAD - (UI_TEMP_COLS - 1) * KK_GAP) / UI_TEMP_COLS;
    ui_list_t col0 = {0};
    for (int c = 0; c < UI_TEMP_COLS; c++) {
        ui_list_t col = mk_list_at(card, KK_PAD + c * (cw + KK_GAP), 48, cw, list_h - 60);
        if (c == 0) col0 = col;
        for (int r = 0; r < UI_TEMP_ROWS; r++) {
            ui_row_t *row = &s_ui.temp_all[c][r];
            int y = r * KK_TROW_H;
            s_temp_col_w[c] = col.w;
            /* 两种数据形态共用这三个标签（宽度/位置在刷新时按行决定）：
               ① 有通道名（新采集端）：行1 = 设备名（全列宽），行2 = 通道名 + 数值；
               ② 无通道名（老采集端）：整行 = 设备名 + 数值，不留下半行空白。
               没有进度条：数值自带阈值色，条会把两行字挤碎。 */
            row->name   = mk_label(col.obj, 0, y + KK_TROW_NAME_Y, col.w, 20,
                                   &ui_font_cjk_16, KK_T2, NULL);
            row->detail = mk_right(col.obj, col.w - KK_TROW_VAL_W, y + KK_TROW_NAME_Y,
                                   KK_TROW_VAL_W, 20, &ui_font_cjk_16, KK_T2);
            row->pct    = mk_label(col.obj, 0, y + KK_TROW_SUB_Y,
                                   col.w - KK_TROW_VAL_W - KK_INLINE, 16,
                                   &ui_font_cjk_12, KK_T3, NULL);
        }
    }
    /* 空态只写一次（放第一列的列表里），不然三列会各喊一遍"未采集到温度" */
    s_ui.temp_empty_all = col0.obj
        ? mk_label(col0.obj, 0, 4, col0.w, 24, &ui_font_cjk_16, KK_T3, "未采集到温度通道")
        : NULL;
    if (s_ui.temp_empty_all) lv_obj_add_flag(s_ui.temp_empty_all, LV_OBJ_FLAG_HIDDEN);
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
   - 主操作（BTN_PRIMARY）蓝底白字，是整屏对比最强的一块。
   - 次操作（BTN_SECONDARY）深底 + 25% 白描边：看得出边界，但明显不如主操作抢眼。
   - 危险操作（BTN_DANGER）不常驻红底——红底会把"取消/关闭"这种安全出口也染成警告；
     只在二次确认武装后才变红。
   - 每个按钮都响应 LV_EVENT_PRESSED/PRESS_LOST/RELEASED：不动的按钮在触摸屏上
     分不清"没按到"和"按了没反应"，这是"清晰"的一半。 */
enum { BTN_PRIMARY = 0, BTN_SECONDARY, BTN_NEUTRAL, BTN_DANGER };

/* 参数顺序与 mk_btn 保持一致（先文字色、后背景色）。这个顺序是有意的：
   曾经写成 (…, bg_rgb, txt_rgb)，调用点却按 mk_btn 的习惯传了 (KK_T1, KK_BLUE)，
   于是"主按钮"的背景色被当成了 0 → 整块变黑（真机上"接受并配对"是个黑按钮）。
   参数顺序不一致时，这种错编译器不会报。 */
static void btn_style(lv_obj_t *b, int kind, const lv_font_t *f,
                      uint32_t txt_rgb, uint32_t bg_rgb)
{
    if (!b) return;
    if (f) lv_obj_set_style_text_font(b, f, LV_PART_MAIN);
    lv_obj_set_style_radius(b, KK_RADIUS_SM, 0);

    switch (kind) {
    case BTN_PRIMARY:
        lv_obj_set_style_bg_color(b, kk_c(bg_rgb), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_opa(b, 45, 0);      /* 18% 白：给蓝块一圈高光边，屏幕暗处也立得住 */
        break;
    case BTN_SECONDARY:
        lv_obj_set_style_bg_color(b, kk_c(KK_S2), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_opa(b, 64, 0);      /* 25% 白：清晰的边界，但不抢主操作 */
        break;
    case BTN_NEUTRAL:
        lv_obj_set_style_bg_color(b, kk_c(KK_S1), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_opa(b, 36, 0);
        break;
    default:                                        /* BTN_DANGER */
        lv_obj_set_style_bg_color(b, kk_c(KK_DANGER), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_opa(b, 45, 0);
        break;
    }
    if (txt_rgb) lv_obj_set_style_text_color(b, kk_c(txt_rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, kk_c(KK_S3), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(b, 1, LV_STATE_PRESSED);
}

/* 按钮文字：mk_btn 在按钮正中放一个宽高=按钮的标签，所以**改字号不影响居中**，
   但盒子尺寸是按建树时的字号算的 —— 换更宽的字号前先把盒子撑到按钮大小，
   否则运行时会静默裁字（中文尤其容易，15px 的三个字就要 45px）。 */
static void btn_label(lv_obj_t *b, const lv_font_t *f, uint32_t txt_rgb, const char *txt)
{
    if (!b) return;
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (!l) return;
    if (f) lv_obj_set_style_text_font(l, f, 0);
    if (txt_rgb) lv_obj_set_style_text_color(l, kk_c(txt_rgb), 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    const lv_font_t *font = lv_obj_get_style_text_font(l, 0);
    lv_obj_set_size(l, lv_obj_get_style_width(b, 0) - 4, font->line_height);
    lv_obj_center(l);
    if (txt) lv_label_set_text(l, txt);
}

/* 键盘上的"确认"是这一步的主操作：用它自己的品牌蓝，而不是把角落那个大按钮复制到键盘里。 */
static void pair_pad_restyle(void)
{
    for (int i = 0; i < PAIR_PAD_KEYS; i++) {
        btn_style(s_ui.pair_key[i], i == 11 ? BTN_PRIMARY : BTN_NEUTRAL,
                  NULL, KK_T1, i == 11 ? KK_BLUE : KK_S1);
    }
}

static void pair_build(lv_obj_t *page)
{
    /* 924×522. Header 68; body 80..424; footer 444..510. */
    lv_obj_t *c = mk_card(page, 12, 8, 924, 522, NULL);
    s_ui.pair_card = c;
    s_ui.pair_title = mk_label(c, 16, 12, 892, 26, &ui_font_cjk_20, KK_T1, "与 NAS 配对");
    s_ui.pair_hint = mk_label(c, 16, 40, 892, 22, &ui_font_cjk_12, KK_T3, NULL);
    kk_panel_box(c, 16, 68, 892, 1, KK_LINE, 0);

    s_ui.pair_steps = kk_panel_box(c, 16, 80, 336, 344, KK_S0, KK_RADIUS);
    s_ui.pair_steps_lbl = mk_label(s_ui.pair_steps, 16, 14, 304, 22,
                                  &ui_font_cjk_12, KK_T2, "三步完成配对");
    for (int i = 0; i < PAIR_STEPS; i++) {
        int y = 56 + i * 90;
        char no[4]; snprintf(no, sizeof no, "%d", i + 1);
        lv_obj_t *badge = kk_panel_box(s_ui.pair_steps, 16, y, 28, 28, KK_S3, KK_RADIUS_PILL);
        lv_obj_t *nl = mk_label(badge, 0, 0, 28, ui_font_num_20.line_height, &ui_font_num_20, KK_T1, no);
        lv_obj_set_style_text_align(nl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(nl);
        s_ui.pair_step_no[i] = badge;
        s_ui.pair_step_txt[i] = mk_label(s_ui.pair_steps, 56, y, 264, 72,
                                         &ui_font_cjk_12, KK_T2, NULL);
        lv_label_set_long_mode(s_ui.pair_step_txt[i], LV_LABEL_LONG_WRAP);
    }
    s_ui.pair_msg = mk_label(c, 32, 96, 304, 312, &ui_font_cjk_16, KK_T2, NULL);
    lv_label_set_long_mode(s_ui.pair_msg, LV_LABEL_LONG_WRAP);

    s_ui.pair_code_panel = kk_panel_box(c, 364, 80, 544, 144, KK_S0, KK_RADIUS);
    lv_obj_t *code = s_ui.pair_code_panel;
    s_ui.pair_code_lbl = mk_label(code, 16, 8, 512, 22, &ui_font_cjk_12, KK_T2, "配对码");
    for (int i = 0; i < PAIR_CODE_LEN; i++) {
        s_ui.pair_slot[i] = mk_label(code, 16 + i * 86, 32, 82, 54, &ui_font_num_44, KK_T1, "-");
        lv_obj_set_style_text_align(s_ui.pair_slot[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    s_ui.pair_code_sub = mk_label(code, 16, 94, 512, 44, &ui_font_cjk_12, KK_T3, NULL);
    lv_label_set_long_mode(s_ui.pair_code_sub, LV_LABEL_LONG_WRAP);

    s_ui.pair_pad = kk_panel_box(c, 364, 232, 544, 192, KK_S0, KK_RADIUS);
    static const struct { const char *t; int k; } PAD[PAIR_PAD_KEYS] = {
        { "1", 1 }, { "2", 2 }, { "3", 3 }, { "4", 4 }, { "5", 5 }, { "6", 6 },
        { "7", 7 }, { "8", 8 }, { "9", 9 }, { "删除", 10 }, { "0", 0 }, { "确认", 11 },
    };
    for (int i = 0; i < PAIR_PAD_KEYS; i++) {
        s_ui.pair_key[i] = mk_btn(s_ui.pair_pad, 12 + (i % 3) * 176, 4 + (i / 3) * 46,
                                  168, 44, (i == 9 || i == 11) ? &ui_font_cjk_16 : &ui_font_num_20,
                                  KK_T1, KK_S1, PAD[i].t);
        lv_obj_add_event_cb(s_ui.pair_key[i], pair_pad_cb, LV_EVENT_CLICKED, (void *)(intptr_t)PAD[i].k);
    }
    pair_pad_restyle();

    s_ui.pair_info_panel = kk_panel_box(c, 364, 80, 544, 344, KK_S0, KK_RADIUS);
    for (int i = 0; i < PAIR_FP_LINES; i++) {
        s_ui.pair_fp[i] = mk_label(s_ui.pair_info_panel, 16, 20 + i * 54, 512, 40,
                                  &ui_font_num_32, KK_T1, NULL);
    }
    s_ui.pair_meta = mk_label(s_ui.pair_info_panel, 16, 254, 512, 70, &ui_font_cjk_12, KK_T3, NULL);
    lv_label_set_long_mode(s_ui.pair_meta, LV_LABEL_LONG_WRAP);
    kk_panel_box(c, 16, 440, 892, 1, KK_LINE, 0);
    s_ui.pair_cancel = mk_btn(c, 16, 458, 160, 52, &ui_font_cjk_16, KK_T1, KK_S2, "关闭");
    s_ui.pair_forget = mk_btn(c, 520, 458, 164, 52, &ui_font_cjk_16, KK_T2, KK_S1, "解除配对");
    s_ui.pair_ok = mk_btn(c, 700, 458, 208, 52, &ui_font_cjk_16, KK_T1, KK_BLUE, "确认");
    btn_style(s_ui.pair_cancel, BTN_SECONDARY, NULL, 0, 0);
    btn_style(s_ui.pair_forget, BTN_NEUTRAL, NULL, 0, 0);
    btn_style(s_ui.pair_ok, BTN_PRIMARY, NULL, KK_T1, KK_BLUE);
    lv_obj_add_event_cb(s_ui.pair_ok, pair_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.pair_cancel, pair_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.pair_forget, pair_forget_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
}

static void pair_show(bool on)
{
    s_pair_open = on;
    if (on) s_pair_forget_armed = false;
    if (!s_ui.pair_card) return;
    if (on) lv_obj_remove_flag(s_ui.pair_card, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(s_ui.pair_card, LV_OBJ_FLAG_HIDDEN);
    theme_apply();
    if (on && s_diagnostics) {
        s_diagnostics = false;
        if (s_ui.diagnostics) lv_obj_add_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);
        set_txt(s_ui.diagnostics_label, "%s", "采集诊断");
    }
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

static bool s_wifi_open;
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

/* 一行可点的网络：整行都是热区（配网列表要能点，和只读的监控行不是一回事） */
static lv_obj_t *mk_tap_row(lv_obj_t *parent, int x, int y, int w, int h,
                            lv_obj_t **name_out, lv_obj_t **meta_out)
{
    lv_obj_t *b = kk_button_create(parent, kk_rect(0, 0, 0, 0, x, y, w, h));
    lv_obj_set_style_bg_color(b, kk_c(KK_S1), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, KK_RADIUS_SM, 0);
    *name_out = mk_label(b, 14, (h - 22) / 2, w - 176, 22, &ui_font_cjk_16, KK_T1, NULL);
    *meta_out = mk_label(b, w - 156, (h - 18) / 2, 142, 18, &ui_font_cjk_12, KK_T3, NULL);
    if (*meta_out) lv_obj_set_style_text_align(*meta_out, LV_TEXT_ALIGN_RIGHT, 0);
    return b;
}

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
        if (kind == WK_OK)       btn_style(btn, BTN_PRIMARY, NULL, KK_T1, KK_BLUE);
        else if (kind == WK_SHIFT && s_wifi_shift && !s_wifi_layer)
                                 btn_style(btn, BTN_PRIMARY, NULL, KK_T1, KK_BLUE);
        else if (kind == WK_CH)  btn_style(btn, BTN_NEUTRAL, NULL, KK_T1, KK_S2);
        else                     btn_style(btn, BTN_SECONDARY, NULL, KK_T2, KK_S1);
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
                                    kk_c(s_wifi_ssid[0] ? KK_T1 : KK_T3), 0);
        lv_obj_set_style_border_opa(s_ui.wifi_ssid_box,
                                    (manual && s_wifi_field == 0) ? KK_FOCUS_RING_OPA : KK_EDGE_CTRL, 0);
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
                                    kk_c(s_wifi_pw_n ? KK_T1 : KK_T3), 0);
        lv_obj_set_style_border_opa(s_ui.wifi_pw_box,
                                    (!manual || s_wifi_field == 1) ? KK_FOCUS_RING_OPA : KK_EDGE_CTRL, 0);
    }
    if (s_ui.wifi_eye_lbl) set_txt(s_ui.wifi_eye_lbl, "%s", s_wifi_reveal ? "隐藏" : "显示");
}

static void wifi_set_stage(int st)
{
    s_wifi_stage = st;
    s_wifi_err[0] = 0;
    bool scan = (st == WIFI_ST_SCAN), pass = (st == WIFI_ST_PASS), link = (st == WIFI_ST_LINK);
    /* 扫描页：列表 + 底部三键；输密码页：输入行 + 键盘 + 返回/连接；连接页：一行大字 */
    for (int i = 0; i < WIFI_AP_ROWS; i++) {
        if (!s_ui.wifi_ap_row[i]) continue;
        if (scan) lv_obj_remove_flag(s_ui.wifi_ap_row[i], LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_ap_row[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_list_hint) {
        if (scan) lv_obj_remove_flag(s_ui.wifi_list_hint, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_list_hint, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_input_panel) {
        if (pass) lv_obj_remove_flag(s_ui.wifi_input_panel, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_input_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_kb) {
        if (pass) lv_obj_remove_flag(s_ui.wifi_kb, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_link_panel) {
        if (link) lv_obj_remove_flag(s_ui.wifi_link_panel, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_link_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_rescan) {
        if (scan) lv_obj_remove_flag(s_ui.wifi_rescan, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_rescan, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_manual) {
        if (scan) lv_obj_remove_flag(s_ui.wifi_manual, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_manual, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_back) {
        if (pass) lv_obj_remove_flag(s_ui.wifi_back, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_back, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_connect) {
        if (pass) lv_obj_remove_flag(s_ui.wifi_connect, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_connect, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_done) {
        if (link) lv_obj_remove_flag(s_ui.wifi_done, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_done, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.wifi_again) {
        if (link) lv_obj_remove_flag(s_ui.wifi_again, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.wifi_again, LV_OBJ_FLAG_HIDDEN);
    }
    if (pass) { wifi_apply_layer(); wifi_render_input(); }
    if (link && s_ui.wifi_link_sub) set_txt(s_ui.wifi_link_sub, "%s", "");
}

static void wifi_build(lv_obj_t *page)
{
    lv_obj_t *c = mk_card(page, 12, 8, 924, 522, NULL);
    s_ui.wifi_card = c;
    s_ui.wifi_title = mk_label(c, 16, 12, 892, 26, &ui_font_cjk_20, KK_T1, "接入 Wi-Fi");
    s_ui.wifi_hint  = mk_label(c, 16, 40, 892, 22, &ui_font_cjk_12, KK_T3, NULL);
    kk_panel_box(c, 16, 68, 892, 1, KK_LINE, 0);

    /* ── ① 扫描：七行网络（整行可点） + 底部三键 ───────────────────────── */
    for (int i = 0; i < WIFI_AP_ROWS; i++) {
        s_ui.wifi_ap_row[i] = mk_tap_row(c, 16, 80 + i * 48, 892, 44,
                                         &s_ui.wifi_ap_name[i], &s_ui.wifi_ap_meta[i]);
        lv_obj_add_event_cb(s_ui.wifi_ap_row[i], wifi_ap_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    s_ui.wifi_list_hint = mk_label(c, 16, 80 + WIFI_AP_ROWS * 48 + 6, 892, 22,
                                   &ui_font_cjk_12, KK_T3, "正在扫描…");
    s_ui.wifi_rescan = mk_btn(c, 16, 458, 180, 52, &ui_font_cjk_16, KK_T1, KK_S2, "重新扫描");
    s_ui.wifi_manual = mk_btn(c, 208, 458, 200, 52, &ui_font_cjk_16, KK_T2, KK_S1, "手动输入");
    s_ui.wifi_close  = mk_btn(c, 744, 458, 164, 52, &ui_font_cjk_16, KK_T2, KK_S1, "关闭");
    btn_style(s_ui.wifi_rescan, BTN_SECONDARY, NULL, 0, 0);
    btn_style(s_ui.wifi_manual, BTN_NEUTRAL, NULL, 0, 0);
    btn_style(s_ui.wifi_close,  BTN_NEUTRAL, NULL, 0, 0);
    lv_obj_add_event_cb(s_ui.wifi_rescan, wifi_rescan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_manual, wifi_manual_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_close,  wifi_close_cb,  LV_EVENT_CLICKED, NULL);

    /* ── ② 输密码：两行输入 + 键盘 ─────────────────────────────────────── */
    s_ui.wifi_input_panel = kk_panel_box(c, 16, 80, 892, 108, KK_S0, KK_RADIUS);
    s_ui.wifi_ssid_box = kk_panel_box(s_ui.wifi_input_panel, 12, 8, 868, 40, KK_S1, KK_RADIUS_SM);
    s_ui.wifi_ssid_lbl = mk_label(s_ui.wifi_ssid_box, 12, 9, 844, 24, &ui_font_cjk_16, KK_T1, NULL);
    s_ui.wifi_pw_box = kk_panel_box(s_ui.wifi_input_panel, 12, 56, 700, 40, KK_S1, KK_RADIUS_SM);
    s_ui.wifi_pw_lbl = mk_label(s_ui.wifi_pw_box, 12, 9, 676, 24, &ui_font_cjk_16, KK_T1, NULL);
    s_ui.wifi_eye = mk_btn(s_ui.wifi_input_panel, 720, 56, 160, 40,
                           &ui_font_cjk_16, KK_T2, KK_S2, "显示");
    s_ui.wifi_eye_lbl = lv_obj_get_child(s_ui.wifi_eye, 0);
    btn_style(s_ui.wifi_eye, BTN_SECONDARY, NULL, 0, 0);
    lv_obj_add_event_cb(s_ui.wifi_ssid_box, wifi_field_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    lv_obj_add_event_cb(s_ui.wifi_pw_box,   wifi_field_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(s_ui.wifi_eye,      wifi_eye_cb,   LV_EVENT_CLICKED, NULL);

    s_ui.wifi_kb = kk_panel_box(c, 16, 196, 892, 250, KK_S0, KK_RADIUS);
    for (int i = 0; i < WIFI_KB_KEYS; i++) {
        int row = i < 10 ? 0 : i < 20 ? 1 : i < 29 ? 2 : i < 38 ? 3 : 4;
        int col = (row == 0) ? i : (row == 1) ? i - 10 : (row == 2) ? i - 20
                : (row == 3) ? i - 29 : i - 38;
        int ncol = (row < 2) ? 10 : (row < 4) ? 9 : 3;
        int kw = (row >= 4 && col == 1) ? 400 : (row == 4 ? 240 : 82);
        int gap = (892 - 24 - ncol * 82) / (ncol > 1 ? ncol - 1 : 1);
        int x = 12 + col * (82 + gap);
        if (row == 4) {                     /* 底排：换层 / 空格 / 连接（220 + 380 + 240 + 2×14 = 868） */
            x  = (col == 0) ? 12 : (col == 1) ? 246 : 640;
            kw = (col == 1) ? 380 : (col == 0) ? 220 : 240;
        }
        const wifi_key_t *tab = WIFI_KB_LETTERS;
        lv_obj_t *b = mk_btn(s_ui.wifi_kb, x, 8 + row * 48, kw, 44,
                             tab[i].kind == WK_CH ? &ui_font_cjk_16 : &ui_font_cjk_12,
                             KK_T1, KK_S2, tab[i].txt);
        s_ui.wifi_key[i] = b;
        s_wifi_key_lbl[i] = lv_obj_get_child(b, 0);
        lv_obj_add_event_cb(b, wifi_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (s_ui.wifi_kb) {                      /* 换层键的文案提示放在键盘右上角的说明里 */
        s_ui.wifi_shift_btn = s_ui.wifi_key[29];
        s_ui.wifi_shift_lbl = s_wifi_key_lbl[29];
    }
    s_ui.wifi_back = mk_btn(c, 16, 458, 160, 52, &ui_font_cjk_16, KK_T2, KK_S1, "返回");
    s_ui.wifi_connect = mk_btn(c, 520, 458, 200, 52, &ui_font_cjk_16, KK_T1, KK_BLUE, "连接");
    btn_style(s_ui.wifi_back, BTN_NEUTRAL, NULL, 0, 0);
    btn_style(s_ui.wifi_connect, BTN_PRIMARY, NULL, KK_T1, KK_BLUE);
    lv_obj_add_event_cb(s_ui.wifi_back, wifi_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_connect, wifi_connect_cb, LV_EVENT_CLICKED, NULL);

    /* ── ③ 连接中/结果：一句话说清现在到哪一步了 ───────────────────────── */
    s_ui.wifi_link_panel = kk_panel_box(c, 16, 96, 892, 300, KK_S0, KK_RADIUS);
    s_ui.wifi_link_big = mk_label(s_ui.wifi_link_panel, 24, 60, 844, 60,
                                  &ui_font_cjk_20, KK_T1, "正在连接…");
    s_ui.wifi_link_sub = mk_label(s_ui.wifi_link_panel, 24, 140, 844, 120,
                                  &ui_font_cjk_16, KK_T3, NULL);
    lv_label_set_long_mode(s_ui.wifi_link_sub, LV_LABEL_LONG_WRAP);
    s_ui.wifi_done  = mk_btn(c, 520, 458, 200, 52, &ui_font_cjk_16, KK_T1, KK_BLUE, "完成");
    s_ui.wifi_again = mk_btn(c, 16, 458, 200, 52, &ui_font_cjk_16, KK_T2, KK_S1, "换一个网络");
    btn_style(s_ui.wifi_done, BTN_PRIMARY, NULL, KK_T1, KK_BLUE);
    btn_style(s_ui.wifi_again, BTN_NEUTRAL, NULL, 0, 0);
    lv_obj_add_event_cb(s_ui.wifi_done, wifi_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.wifi_again, wifi_again_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    wifi_set_stage(WIFI_ST_SCAN);
}

static void wifi_show(bool on)
{
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
        set_txt(s_ui.diagnostics_label, "%s", "采集诊断");
    }
    theme_apply();
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
    for (int i = 0; i < WIFI_AP_ROWS; i++) {
        if (!s_ui.wifi_ap_row[i]) continue;
        if (i < n) {
            const fnos_ap_t *a = &aps[i];
            int  bars = wifi_bars(a->rssi);
            char meta[48];
            snprintf(meta, sizeof meta, "%d 格 · %d dBm · %s", bars, (int)a->rssi,
                     a->secure ? "加密" : "开放");
            set_txt(s_ui.wifi_ap_name[i], "%s", a->ssid);
            set_txt(s_ui.wifi_ap_meta[i], "%s", meta);
            /* 颜色之外还有"格数 + dBm"两种说法，色弱也读得出（版面合同第 9 条） */
            lv_obj_set_style_text_color(s_ui.wifi_ap_meta[i],
                                        kk_c(bars >= 3 ? KK_OK : bars == 2 ? KK_WARN : KK_T3), 0);
            lv_obj_remove_flag(s_ui.wifi_ap_row[i], LV_OBJ_FLAG_HIDDEN);
            /* 从输密码页退回来时，让用户一眼看到刚才点的是哪一条 */
            lv_obj_set_style_bg_color(s_ui.wifi_ap_row[i],
                                      kk_c(s_wifi_sel == i ? KK_S2 : KK_S1), 0);
        } else {
            lv_obj_add_flag(s_ui.wifi_ap_row[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
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
        char b[40];
        const char *ssid = fnos_net_ssid();
        if (!fnos_net_configured())       snprintf(b, sizeof b, "Wi-Fi 未配置");
        else if (fnos_net_online())       snprintf(b, sizeof b, "%s", ssid);
        else if (ssid[0])                 snprintf(b, sizeof b, "%s 连接中…", ssid);
        else                              snprintf(b, sizeof b, "连接中…");
        set_txt(s_ui.wifi_btn_lbl, "%s", b);
        lv_obj_set_style_text_color(s_ui.wifi_btn_lbl,
                                    kk_c(!fnos_net_configured() ? KK_WARN
                                        : fnos_net_online()     ? KK_T1 : KK_T2), 0);
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
        if (s_ui.wifi_link_big) {
            set_txt(s_ui.wifi_link_big, "%s", on ? "连接成功" : bad ? "没连上" : "正在连接…");
            lv_obj_set_style_text_color(s_ui.wifi_link_big,
                                        kk_c(on ? KK_OK : bad ? KK_WARN : KK_T1), 0);
        }
        if (s_ui.wifi_link_sub) {
            char b[220];
            if (on) {
                snprintf(b, sizeof b, "%s · 信号 %d dBm\n板子已经记住这个网络，下次开机自动连。",
                         fnos_net_state_str(), (int)fnos_net_rssi());
            } else {
                snprintf(b, sizeof b,
                         "%s\n一直连不上就点「换一个网络」重来：口令区分大小写，"
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
        s_pair_alert = KK_BLUE_TXT;
    } else if (prov) {
        set_txt(s_ui.pair_btn_lbl, "%s", "已配对");
        s_pair_alert = KK_T1;
    } else {
        set_txt(s_ui.pair_btn_lbl, "%s", "配对");
        s_pair_alert = KK_WARN;
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
            lv_obj_set_style_text_color(s_ui.pair_slot[i], kk_c(KK_T1), 0);
        } else {
            set_txt(s_ui.pair_slot[i], "%s", "-");
            lv_obj_set_style_text_color(s_ui.pair_slot[i], kk_c(KK_T4), 0);
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
                           : "在 NAS 管理页「设备配对」生成，把 6 位数字输进右边的键盘");
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
        set_txt(s_ui.pair_hint, "%s", "确认 NAS 上应用在运行，并且配对码没有过期");
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

    /* 核对阶段把"接受之后会发生什么"放到左边一栏；这个阶段没有步骤可讲 */
    bool show_steps_l = (stage == PST_CODE || stage == PST_FAIL);
    /* 左栏（x16 w336）只有一块地方：'
       "输码/失败"阶段放三步指引，其余阶段放状态说明，两者互斥。
       左栏以前是"只要 v.msg 非空就显示状态说明" —— 未配对时 v.msg 就是
       "未配对：用编译期默认地址"，于是在输码阶段它和"三步完成配对"同时出现，
       两段文字叠在一起（真机照片上一眼可见，预览当时因为桩件 msg 恒为空而全绿）。 */
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

    if (show_code) lv_obj_remove_flag(s_ui.pair_code_panel, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.pair_code_panel, LV_OBJ_FLAG_HIDDEN);
    if (show_fp) lv_obj_remove_flag(s_ui.pair_info_panel, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.pair_info_panel, LV_OBJ_FLAG_HIDDEN);
    if (show_code) lv_obj_remove_flag(s_ui.pair_code_lbl, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_add_flag(s_ui.pair_code_lbl, LV_OBJ_FLAG_HIDDEN);
    if (show_code) lv_obj_remove_flag(s_ui.pair_code_sub, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_add_flag(s_ui.pair_code_sub, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < PAIR_CODE_LEN; i++) {
        if (show_code) lv_obj_remove_flag(s_ui.pair_slot[i], LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(s_ui.pair_slot[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (show_pad)  lv_obj_remove_flag(s_ui.pair_pad, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_add_flag(s_ui.pair_pad, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < PAIR_FP_LINES; i++) {
        if (show_fp) lv_obj_remove_flag(s_ui.pair_fp[i], LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(s_ui.pair_fp[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (show_fp) lv_obj_remove_flag(s_ui.pair_meta, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(s_ui.pair_meta, LV_OBJ_FLAG_HIDDEN);
    if (show_steps) lv_obj_remove_flag(s_ui.pair_steps, LV_OBJ_FLAG_HIDDEN);
    else            lv_obj_add_flag(s_ui.pair_steps, LV_OBJ_FLAG_HIDDEN);
    if (show_note) { lv_label_set_text(s_ui.pair_msg, ACCEPT_NOTE);
                     lv_obj_remove_flag(s_ui.pair_msg, LV_OBJ_FLAG_HIDDEN); }
    else if (!show_steps_l && v.msg[0]) lv_obj_remove_flag(s_ui.pair_msg, LV_OBJ_FLAG_HIDDEN);
    else                                lv_obj_add_flag(s_ui.pair_msg, LV_OBJ_FLAG_HIDDEN);
    /* 右侧信息块最后一行：交互提示。放这里就不必在左栏和"三步指引"抢地方。 */
    if (s_ui.pair_code_sub) {
        if (right_hint[0]) {
            set_txt(s_ui.pair_code_sub, "%s", right_hint);
            lv_obj_set_style_text_color(s_ui.pair_code_sub, kk_c(KK_WARN), 0);
        } else {
            lv_obj_set_style_text_color(s_ui.pair_code_sub, kk_c(KK_T3), 0);
        }
    }
    if (show_ok) lv_obj_remove_flag(s_ui.pair_ok, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(s_ui.pair_ok, LV_OBJ_FLAG_HIDDEN);
    if (show_forget) lv_obj_remove_flag(s_ui.pair_forget, LV_OBJ_FLAG_HIDDEN);
    else             lv_obj_add_flag(s_ui.pair_forget, LV_OBJ_FLAG_HIDDEN);

    /* 按钮文案与"分量"随阶段换：主操作永远是"这一步要你点的那个"，
       并且永远待在右下角同一个位置（肌肉记忆）。 */
    btn_label(s_ui.pair_ok, &ui_font_cjk_16, KK_T1,
              stage == PST_CONFIRM ? "接受并配对" : "确认");
    btn_label(s_ui.pair_cancel, &ui_font_cjk_16, KK_T1,
              stage == PST_CONFIRM ? "取消" : "关闭");
    if (s_pair_forget_armed) {
        btn_style(s_ui.pair_forget, BTN_DANGER, &ui_font_cjk_12, KK_T1, 0);
        btn_label(s_ui.pair_forget, &ui_font_cjk_12, KK_T1, "再点一次确认解除");
    } else {
        btn_style(s_ui.pair_forget, BTN_NEUTRAL, &ui_font_cjk_16, KK_T2, 0);
        btn_label(s_ui.pair_forget, &ui_font_cjk_16, KK_T2, "解除配对");
    }
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
                 "再点一次就解除配对：板子会忘掉令牌和证书，NAS 那边要另行撤销这台设备");
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
}

static void nav_cb(lv_event_t *e)
{
    fnos_ui_set_page((int)(intptr_t)lv_event_get_user_data(e));
}

static void swipe_next(void) { fnos_ui_set_page(s_page + 1); }
static void swipe_prev(void) { fnos_ui_set_page(s_page - 1); }

static void night_apply(void)
{
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(s_night ? 0x000000 : KK_BG), 0);
    for (int i = 0; i < s_ui.ncards; i++) {
        if (!s_ui.cards[i]) continue;
        /* 夜间只压暗纯色表面；发丝边降到 0 免得黑底发灰 */
        lv_obj_set_style_bg_color(s_ui.cards[i], lv_color_hex(s_night ? 0x121417 : KK_SURF_T), 0);
        lv_obj_set_style_bg_grad_dir(s_ui.cards[i], LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_border_opa(s_ui.cards[i], s_night ? 0 : KK_EDGE_OPA, 0);
    }
    /* 左栏与配对按钮自己有一套三档表面（rail → 导航项 → 选中），不走卡片那两档，
       否则夜间会把 rail 抹成和卡片一样的灰。 */
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
        snprintf(b, cap, "%s", "最近 180 次采集");
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

static void refresh(void)
{
    const fnos_status_t *st = &s_st;
    char b[160], c1[32], c2[32];

    /* 顶栏：主机 / 端点 / 状态胶囊 / 信号 */
    set_txt(s_ui.h_host, "%s", st->host[0] ? st->host : "fnos");
    if (!st->ever_ok) set_txt(s_ui.h_ep, "%s · 等待采集端", NAV_TXT[s_page]);
    else if (!st->online) set_txt(s_ui.h_ep, "%s · 保留旧数据 · %s", NAV_TXT[s_page], fmt_age(c1, sizeof c1, data_age_ms(st)));
    else set_txt(s_ui.h_ep, "%s · 每秒更新", NAV_TXT[s_page]);
    if (!st->ever_ok)      chip_set_glow(s_ui.h_chip, "等待数据", KK_T4);
    else if (st->online)   chip_set_glow(s_ui.h_chip, "在线", KK_OK);
    else                   chip_set_glow(s_ui.h_chip, "离线", KK_WARN);
    kk_signal_set(s_ui.h_bars, fnos_net_rssi());

    /* 健康结论先区分可信度，再报告采集器的告警。 */
    int critical = 0, warnings = 0, running = 0, healthy_raid = 0;
    for (int i = 0; i < st->nalerts; i++) {
        if (strcmp(st->alerts[i].lv, "crit") == 0) critical++;
        else if (strcmp(st->alerts[i].lv, "warn") == 0) warnings++;
    }
    for (int i = 0; i < st->ndocker; i++) if (st->docker[i].up) running++;
    for (int i = 0; i < st->nraid; i++) if (st->raid[i].ok) healthy_raid++;
    uint32_t health_color = !st->ever_ok ? KK_T3 : !st->online ? KK_WARN : critical ? KK_DANGER : warnings ? KK_WARN : KK_OK;
    set_txt(s_ui.health, "%s", !st->ever_ok ? "等待数据" : !st->online ? "采集端离线" : critical ? "存在严重告警" : warnings ? "需要关注" : "无采集告警");
    lv_obj_set_style_text_color(s_ui.health, kk_c(health_color), 0);
    /* 健康卡右侧那行摘要同时是"为什么连不上"的公告栏：在线时报告运行概况，
       离线时报告原因（见 link_reason）——比再来一行"离线"有用得多。 */
    const char *why = st->online ? NULL : link_reason(st);

    ui_kpi_t *k = &s_ui.kpi[0];
    set_txt(k->value, "%.0f", st->cpu.pct);
    /* 进程数与运行队列是"数据层一直有、界面从来没用"的两个字段（见 ui-redesign-v9 §7）。
       运行队列 > 核数就是过载信号，比负载平均值更即时；两个都塞进这一行
       （cjk_12 × 约 26 字符 ≈ 320px，盒宽 190 —— 超了会自动省略号，不会撞边）。 */
    set_txt(k->sub, "%d 核 · 负载 %.2f · 队列 %d", st->cpu.cores, st->cpu.load1, st->cpu.runq);
    kk_bar_set_fixed(&k->bar, st->cpu.pct, kk_c(st->cpu.pct >= 90 ? KK_DANGER : st->cpu.pct >= 80 ? KK_WARN : KK_S_CPU));
    k = &s_ui.kpi[1];
    set_txt(k->value, "%.0f", st->mem.pct);
    /* 交换分区：数据层一直采、界面从来没用过（见 ui-redesign-v9 §7）。
       "换 0.1/2.0G" 而不是"交换 0.1 / 2.0 GB" —— 盒宽 190px / cjk_12，
       长写法 258px 会被 LONG_DOT 静默截成省略号（截断不报 overflow，只能靠肉眼看）。 */
    set_txt(k->sub, "%.1f / %.1f GB · 换 %.1f/%.1fG",
            st->mem.used_mb / 1024.0f, st->mem.total_mb / 1024.0f,
            st->mem.swap_used_mb / 1024.0f, st->mem.swap_total_mb / 1024.0f);
    kk_bar_set_fixed(&k->bar, st->mem.pct, kk_c(st->mem.pct >= 90 ? KK_DANGER : st->mem.pct >= 80 ? KK_WARN : KK_S_MEM));
    k = &s_ui.kpi[2];
    /* "最高温度" = max(CPU 温度, 每一路 temps)。副标签必须写清是**哪个设备的哪个
       通道**：只写个 "NIC" 时 75°C 会被读成整机温度（用户报障的起点就是这个）。
       采集端按温度降序发，这里仍走一遍 max，顺序变了也不会标错。 */
    float hottest = st->cpu.temp_c;
    char sensor[48];
    snprintf(sensor, sizeof sensor, "%s", st->cpu.temp_c > 0 ? "CPU" : "");
    for (int i = 0; i < st->ntemps; i++) {
        if (st->temps[i].c <= hottest) continue;
        hottest = st->temps[i].c;
        const fnos_temp_t *t = &st->temps[i];
        const char *nm = t->dn[0] ? t->dn : t->dev;   /* 优先人读设备名 */
        if (t->ch[0]) snprintf(sensor, sizeof sensor, "%s · %s", nm, t->ch);
        else          snprintf(sensor, sizeof sensor, "%s", nm);
    }
    set_txt(k->value, hottest > 0 ? "%.0f" : "-", hottest);
    set_txt(k->sub, "%s", hottest > 0 ? sensor : "温度不可用");
    kk_bar_set_fixed(&k->bar, hottest, kk_c(hottest >= 75 ? KK_DANGER : hottest >= 60 ? KK_WARN : KK_OK));
    k = &s_ui.kpi[3];
    float divisor = st->net.rx_kbs >= 1048576 ? 1048576 : st->net.rx_kbs >= 1024 ? 1024 : 1;
    set_txt(k->value, divisor > 1 ? "%.1f" : "%.0f", st->net.rx_kbs / divisor);
    set_txt(k->unit, "%s", divisor == 1048576 ? "GB/s" : divisor == 1024 ? "MB/s" : "KB/s");
    set_txt(k->sub, "上行 %s", fmt_rate(b, sizeof b, st->net.tx_kbs));
    for (int i = 0; i < 4; i++) {
        if (!st->ever_ok) { set_txt(s_ui.kpi[i].value, "-"); set_txt(s_ui.kpi[i].sub, "等待数据"); }
        lv_obj_set_style_text_color(s_ui.kpi[i].value, kk_c(st->online ? KK_T1 : KK_T3), 0);
    }

    float used = 0, total = 0, free_gb = 0;
    for (int i = 0; i < st->nvols; i++) { used += st->vols[i].used_gb; total += st->vols[i].total_gb; free_gb += st->vols[i].free_gb; }
    set_txt(s_ui.capacity, "%s", st->ever_ok && st->nvols ? fmt_cap(b, sizeof b, used) : "-");
    set_txt(s_ui.capacity_detail, "总计 %s\n可用 %s", fmt_cap(c1, sizeof c1, total), fmt_cap(c2, sizeof c2, free_gb));
    float usage = total > 0 ? used / total * 100 : 0;
    kk_bar_set_fixed(&s_ui.capacity_bar, usage, kk_c(usage >= 90 ? KK_DANGER : usage >= 80 ? KK_WARN : KK_OK));
    float capacities[] = { used, total, free_gb };
    for (int i = 0; i < 3; i++) set_txt(s_ui.storage_value[i], "%s", st->ever_ok && st->nvols ? fmt_cap(b, sizeof b, capacities[i]) : "-");
    set_txt(s_ui.storage_note, "%d 个卷 · 用量 %.0f%%\n阵列正常 %d / %d", st->nvols, total > 0 ? used / total * 100 : 0, healthy_raid, st->nraid);
    /* 单行右对齐（标签是 LONG_DOT，放不下自动省略，不会折行撞上卡片底） */
    if (!st->ever_ok) {
        set_txt(s_ui.overview_note, "%s", why ? why : "正在连接采集端…");
    } else if (!st->online) {
        set_txt(s_ui.overview_note, "%s", why ? why : "离线，显示的是最后一次采集的快照");
    } else {
        set_txt(s_ui.overview_note, "运行 %s · 容器 %d/%d · 严重 %d · 警告 %d",
                fmt_uptime(b, sizeof b, st->uptime_s), running, st->ndocker, critical, warnings);
    }
    if (!st->ever_ok) { set_txt(s_ui.capacity_detail, "等待存储数据"); set_txt(s_ui.storage_note, ""); }

    {
        char tb[64];
        set_txt(s_ui.tr_title, "资源趋势 · %s", hist_span_text(tb, sizeof tb, st));
        char nb[64];
        set_txt(s_ui.net_title, "网络吞吐 · %s", hist_span_text(nb, sizeof nb, st));
    }
    kk_trend_sync_series(&s_ui.tr_cpu, &s_cpu);
    kk_trend_sync_series(&s_ui.tr_mem, &s_mem);
    set_txt(s_ui.tr_cpu_lbl, "%.0f%% · 峰 %d%%", st->cpu.pct, kk_trend_peak(&s_ui.tr_cpu));
    set_txt(s_ui.tr_mem_lbl, "%.0f%% · 峰 %d%%", st->mem.pct, kk_trend_peak(&s_ui.tr_mem));

    if (!st->ever_ok) { set_txt(s_ui.tr_cpu_lbl, "等待数据"); set_txt(s_ui.tr_mem_lbl, "等待数据"); }
    /* P1：卷 / 阵列 / 硬盘 */
    int live = 0;
    for (int i = 0; i < UI_ROWS_VOL; i++) {
        if (i >= st->nvols) { row_visible(&s_ui.vol1[i], false); continue; }
        const fnos_vol_t *v = &st->vols[i];
        row_visible(&s_ui.vol1[i], true);
        row_group_move(&s_ui.vol1[i], live++ * KK_LIST_ROW);
        /* 详情和百分比共用临时缓冲，先顺序格式化，避免 C 参数求值顺序覆盖。 */
        /* 卷行副行带上文件系统（数据层一直有 vols[].fs，界面此前没用）：
           "已用 2.0 TB / 总计 3.6 TB · ext4"。盒宽 list.w − 数值列 − 内间距，
           cjk_12 下 28 个字符 ≈ 430px，远不到 512 的右界，不会撞数值列。 */
        snprintf(b, sizeof b, "已用 %s / 总计 %s · %s",
                 fmt_cap(c1, sizeof c1, v->used_gb), fmt_cap(c2, sizeof c2, v->total_gb),
                 v->fs[0] ? v->fs : "-");
        snprintf(c1, sizeof c1, "%.0f%%", v->pct);
        row_set(&s_ui.vol1[i], v->mnt, b, c1, v->pct);
    }
    live = 0;
    for (int i = 0; i < UI_ROWS_RAID; i++) {
        if (i >= st->nraid) { row_visible(&s_ui.raid[i], false); continue; }
        const fnos_raid_t *r = &st->raid[i];
        row_visible(&s_ui.raid[i], true);
        row_group_move(&s_ui.raid[i], live++ * KK_LIST_ROW);
        snprintf(b, sizeof b, "%s · %d/%d · %s", r->lvl, r->have, r->want, r->state);
        bool syncing = r->sync_pct < 100 && (strstr(r->state, "sync") || strstr(r->state, "recover") || strstr(r->state, "reshape"));
        if (syncing) snprintf(c1, sizeof c1, "%.0f%%", r->sync_pct);
        if (!syncing) snprintf(c1, sizeof c1, "%s", r->ok ? "正常" : "降级");
        row_set(&s_ui.raid[i], r->dev, b, c1, 0);
        lv_obj_set_style_text_color(s_ui.raid[i].pct, kk_c(r->ok ? KK_OK : syncing ? KK_WARN : KK_DANGER), 0);
    }
    live = 0;
    for (int i = 0; i < UI_ROWS_DISK; i++) {
        if (i >= st->ndisks) { row_visible(&s_ui.disk[i], false); continue; }
        const fnos_disk_t *d = &st->disks[i];
        row_visible(&s_ui.disk[i], true);
        row_group_move(&s_ui.disk[i], live++ * KK_LIST_ROW);
        row_set(&s_ui.disk[i], d->dev,
                (snprintf(b, sizeof b, "读 %s · 写 %s",
                          fmt_rate(c1, sizeof c1, d->rd_kbs),
                          fmt_rate(c2, sizeof c2, d->wr_kbs)), b),
                NULL, 0);
    }

    /* P2 网络与 P3 容器 */
    set_txt(s_ui.big[0].value, "%s", fmt_rate(b, sizeof b, st->net.tx_kbs));
    set_txt(s_ui.big[0].sub, "累计 %s", fmt_cap(c1, sizeof c1, st->net.tx_total_gb));
    set_txt(s_ui.big[1].value, "%s", fmt_rate(b, sizeof b, st->net.rx_kbs));
    set_txt(s_ui.big[1].sub, "累计 %s", fmt_cap(c1, sizeof c1, st->net.rx_total_gb));
    set_txt(s_ui.big[2].value, "%s", fmt_rate(b, sizeof b, st->net.rx_kbs + st->net.tx_kbs));
    set_txt(s_ui.big[2].sub, "%s", st->net.ifname[0] ? st->net.ifname : "");
    set_txt(s_ui.big[3].value, "%d ms", st->http_ms);
    set_txt(s_ui.big[3].sub, "轮询 %u · 失败 %u", (unsigned)st->ok_count, (unsigned)st->fail_count);

    kk_trend_sync_series(&s_ui.tr_tx, &s_tx);
    kk_trend_sync_series(&s_ui.tr_rx, &s_rx);
    int peak_tx = kk_trend_peak(&s_ui.tr_tx), peak_rx = kk_trend_peak(&s_ui.tr_rx);
    float top = (peak_rx > peak_tx ? peak_rx : peak_tx) * 1.2f;
    if (top < st->net.rx_kbs * 1.2f) top = st->net.rx_kbs * 1.2f;
    if (top < st->net.tx_kbs * 1.2f) top = st->net.tx_kbs * 1.2f;
    if (top < 20) top = 20;
    kk_trend_range(&s_ui.tr_tx, top); kk_trend_range(&s_ui.tr_rx, top);
    set_txt(s_ui.net_axis, "量程 0 - %s", fmt_rate(b, sizeof b, top));
    /* 网卡信息条：接口 + 累计收发（GB）+ 采集耗时 + 成功率；状态点与右上角"在线"冗余编码 */
    set_txt(s_ui.if_name, "%s", st->net.ifname[0] ? st->net.ifname : "-");
    {
        uint32_t dc = st->online ? KK_OK : (st->ever_ok ? KK_WARN : KK_T4);
        lv_obj_set_style_bg_color(s_ui.if_dot, kk_c(dc), 0);
        kk_glow(s_ui.if_dot, dc, st->online ? KK_GLOW_OPA : 0);
    }
    set_txt(s_ui.info_val[0], "%.1f GB", st->net.rx_total_gb);
    set_txt(s_ui.info_val[1], "%.1f GB", st->net.tx_total_gb);
    set_txt(s_ui.info_val[2], "%d ms", st->http_ms);
    {
        uint32_t tot = st->ok_count + st->fail_count;
        set_txt(s_ui.info_val[3], "%u%%", tot ? (unsigned)(st->ok_count * 100u / tot) : 0u);
    }
    set_txt(s_ui.system_note, "容器 %d/%d 运行 · %d 路温度 · %d 个事件%s", running, st->ndocker, st->ntemps, st->nalerts, st->online ? "" : " · 旧数据");
    /* 数据层早就有、v7 界面一直没用上的字段：负载 / 进程数 / 交换 / 运行时长 */
    /* 文案长度受盒宽约束（892px，cjk_12）：40 个中文字 ≈ 890px 就到顶。
       "0.1 / 2.0 GB" 写成 "0.1/2.0G" 省 60px，否则会折行、第二行被卡片裁掉
       （audit_bounds 报 `child out of parent`）。 */
    set_txt(s_ui.system_note2, "负载 %.2f/%.2f/%.2f · 进程 %d · 交换 %.1f/%.1fG · 运行 %s",
            st->cpu.load1, st->cpu.load5, st->cpu.load15, st->cpu.procs,
            st->mem.swap_used_mb / 1024.f, st->mem.swap_total_mb / 1024.f,
            fmt_uptime(c2, sizeof c2, st->uptime_s));
    show(s_ui.vol_empty, st->nvols == 0); show(s_ui.raid_empty, st->nraid == 0);
    show(s_ui.disk_empty, st->ndisks == 0); show(s_ui.dock_empty, st->ndocker == 0); show(s_ui.temp_empty, st->ntemps == 0);
    if (!st->ever_ok) {
        for (int i = 0; i < 4; i++) { set_txt(s_ui.big[i].value, "-"); set_txt(s_ui.big[i].sub, "等待数据"); }
        for (int i = 0; i < 4; i++) set_txt(s_ui.info_val[i], "-");
        set_txt(s_ui.if_name, "-");
        set_txt(s_ui.system_note, "等待采集，尚无有效快照");
        set_txt(s_ui.system_note2, "");
    }
    set_txt(s_ui.tr_tx_lbl, "%s · 峰 %d KB/s", fmt_rate(c1, sizeof c1, st->net.tx_kbs),
            kk_trend_peak(&s_ui.tr_tx));
    set_txt(s_ui.tr_rx_lbl, "%s · 峰 %d KB/s", fmt_rate(c1, sizeof c1, st->net.rx_kbs),
            kk_trend_peak(&s_ui.tr_rx));

    if (!st->ever_ok) { set_txt(s_ui.tr_tx_lbl, "等待数据"); set_txt(s_ui.tr_rx_lbl, "等待数据"); }
    live = 0;
    for (int i = 0; i < UI_ROWS_DOCK; i++) {
        if (i >= st->ndocker) { row_visible(&s_ui.dock[i], false); continue; }
        const fnos_docker_t *d = &st->docker[i];
        row_visible(&s_ui.dock[i], true);
        row_group_move(&s_ui.dock[i], live++ * KK_LIST_ROW3);
        row_set(&s_ui.dock[i], d->n, d->up ? "运行中" : d->s, NULL, 0);
        if (s_ui.dock[i].detail) {
            lv_obj_set_style_text_align(s_ui.dock[i].detail, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_color(s_ui.dock[i].detail,
                                        lv_color_hex(d->up ? KK_OK : KK_T4), 0);
        }
    }

    /* P3：温度 / 采集端点 / 告警 */
    live = 0;
    for (int i = 0; i < UI_ROWS_TEMP; i++) {
        if (i >= st->ntemps) { row_visible(&s_ui.temp[i], false); continue; }
        const fnos_temp_t *t = &st->temps[i];
        row_visible(&s_ui.temp[i], true);
        /* 温度行的三段跟着 KK_LIST_ROW3 收拢（三列卡的行高与存储页不同） */
        if (s_ui.temp[i].name)   lv_obj_set_y(s_ui.temp[i].name,   live * KK_LIST_ROW3 + KK_ROW_NAME_Y);
        if (s_ui.temp[i].detail) lv_obj_set_y(s_ui.temp[i].detail, live * KK_LIST_ROW3 + KK_ROW_NAME_Y);
        if (s_ui.temp[i].bar.track) lv_obj_set_y(s_ui.temp[i].bar.track, live * KK_LIST_ROW3 + KK_ROW_BAR_Y);
        if (s_ui.temp[i].bar.fill)  lv_obj_set_y(s_ui.temp[i].bar.fill,  live * KK_LIST_ROW3 + KK_ROW_BAR_Y);
        live++;
        /* 一行要能回答"这是哪个设备的哪个通道"：dev 是设备名（enp1s0 / nvme2n1 /
           coretemp），ch 是通道名（PHY / Composite / Core 3）。老采集端没有 ch，
           那就只显示设备名/拼接名，不去补一个假的通道名。 */
        /* 系统页这张卡只有 ~164px 名称列，放不下"人读设备名 + 通道名"两样，
           所以**通道名放前面**：截断时先丢设备名的尾巴，至少"PHY / MAC / Core 3"
           还认得出是哪一个通道（NIC 的 PHY 与 MAC 名字完全一样，丢通道名就分不清了）。
           完整的"设备名 / 通道 / 数值"在「温度」页。 */
        const char *nm = t->dn[0] ? t->dn : t->dev;
        if (t->ch[0]) set_txt(s_ui.temp[i].name, "%s · %s", t->ch, nm);
        else          set_txt(s_ui.temp[i].name, "%s", nm);
        set_txt(s_ui.temp[i].detail, "%.1f°C", t->c);
        kk_bar_set_fixed(&s_ui.temp[i].bar, t->c, kk_c(t->c >= 75 ? KK_DANGER : t->c >= 60 ? KK_WARN : KK_OK));
        lv_obj_set_style_text_color(s_ui.temp[i].detail, kk_c(t->c >= 75 ? KK_DANGER : t->c >= 60 ? KK_WARN : KK_T2), 0);
    }

    /* ── P4：温度全量表 ───────────────────────────────────────────────
       所有通道摊成三列，每列 per 条（列内可滚动）。采集端已按温度降序发过来，
       所以"左上角那个就是当前最热的传感器"。 */
    int per = (st->ntemps + UI_TEMP_COLS - 1) / UI_TEMP_COLS;
    if (per < 1) per = 1;
    for (int c = 0; c < UI_TEMP_COLS; c++) {
        for (int r = 0; r < UI_TEMP_ROWS; r++) {
            ui_row_t *row = &s_ui.temp_all[c][r];
            int i = c * per + r;
            /* 每列只用前 per 行：没有这条界限时 r 会一直排到 UI_TEMP_ROWS，
               第 0 列的第 per+1 行会重复显示第 1 列的第一个通道（预览里一眼可见）。 */
            if (r >= per || i >= st->ntemps) { row_visible(row, false); continue; }
            const fnos_temp_t *t = &st->temps[i];
            row_visible(row, true);
            /* 一行还是两行，由**数据形态**决定（不是由状态决定，所以整屏是一致的）：
               有新采集端的通道名 → 设备名独占第一行、通道名与数值在第二行；
               只有老采集端的类型名（没有通道）→ 收成一行"名字 + 数值"，不留半行空白。 */
            const bool two = t->ch[0] != 0;
            const int  cw  = s_temp_col_w[c];
            int y = r * KK_TROW_H;
            if (row->name) {
                lv_obj_set_y(row->name, y + KK_TROW_NAME_Y);
                /* 两行制：名字拿整列宽（"Samsung SSD 990 PRO 2TB" 要 ~230px）；
                   一行制：让出数值那一列，避免名字压到数值上。 */
                lv_obj_set_width(row->name,
                                 two ? cw : cw - KK_TROW_VAL_W - KK_INLINE);
            }
            if (row->detail) lv_obj_set_y(row->detail, y + (two ? KK_TROW_SUB_Y : KK_TROW_NAME_Y));
            if (row->pct) {
                lv_obj_set_y(row->pct, y + KK_TROW_SUB_Y);
                if (two) lv_obj_remove_flag(row->pct, LV_OBJ_FLAG_HIDDEN);
                else     lv_obj_add_flag(row->pct, LV_OBJ_FLAG_HIDDEN);
            }
            set_txt(row->name, "%s", t->dn[0] ? t->dn : t->dev);
            set_txt(row->detail, "%.1f°C", t->c);
            if (two) set_txt(row->pct, "%s", t->ch);
            /* 数据可信度与阈值色分开表达：离线/旧帧时数值降为 T3（不再用危险色喊
               "75°C"）—— 页头那行"保留旧数据 · 年龄"说明原因，颜色不越权。
               KK_T4 按版面合同不得用于 <20px 的文字，所以降级色用 T3。 */
            uint32_t vcol = !st->online ? KK_T3
                          : t->c >= 75 ? KK_DANGER : t->c >= 60 ? KK_WARN : KK_T2;
            lv_obj_set_style_text_color(row->detail, kk_c(vcol), 0);
            lv_obj_set_style_text_color(row->name, kk_c(st->online ? KK_T2 : KK_T3), 0);
        }
    }
    if (s_ui.temp_empty_all) {
        bool none = st->ever_ok && st->ntemps == 0;
        if (none) lv_obj_remove_flag(s_ui.temp_empty_all, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_ui.temp_empty_all, LV_OBJ_FLAG_HIDDEN);
    }
    /* 摘要两行：第一行"多少路 + 最热的是谁"，第二行给阈值口径。
       "危险/注意"的条数在这里用文字写出来 —— 颜色不是唯一的信息载体。 */
    if (!st->ever_ok) {
        set_txt(s_ui.temp_note, "等待数据");
        set_txt(s_ui.temp_note2, "");
    } else if (st->ntemps == 0) {
        set_txt(s_ui.temp_note, "采集端没有上报温度通道");
        set_txt(s_ui.temp_note2, "看「采集诊断」页的采集段一行：temps 可能是 denied / missing");
    } else {
        int danger = 0, warn = 0;
        const fnos_temp_t *h = &st->temps[0];
        for (int i = 0; i < st->ntemps; i++) {
            if (st->temps[i].c >= 75) danger++;
            else if (st->temps[i].c >= 60) warn++;
            /* 列表按"设备名+通道名"固定排序（见 fnos_data.c），所以最热的那一路
               不再固定在第 0 行 —— 这里自己扫一遍 max，别假设 temps[0] 是最热的。 */
            if (st->temps[i].c > h->c) h = &st->temps[i];
        }
        /* 摘要是全宽 900px，放得下完整设备名 —— 这里优先 dn（"Marvell AQC113 10GbE"），
           没有才退回 dev（enp1s0）。 */
        const char *hn = h->dn[0] ? h->dn : h->dev;
        if (h->ch[0]) set_txt(s_ui.temp_note, "%d 路传感器 · 最热 %s · %s %.1f°C",
                              st->ntemps, hn, h->ch, h->c);
        else          set_txt(s_ui.temp_note, "%d 路传感器 · 最热 %s %.1f°C",
                              st->ntemps, hn, h->c);
        set_txt(s_ui.temp_note2, "%d 路 ≥75°C 危险 · %d 路 ≥60°C 注意 · 逐路写明设备与通道",
                danger, warn);
    }

    set_txt(s_ui.agent[0].detail, "%s", st->host[0] ? st->host : "-");
    set_txt(s_ui.agent[1].detail, "%s:%d", FNOS_HOST, FNOS_PORT);
    set_txt(s_ui.agent[2].detail, "%d ms (状态 %d)", st->http_ms, st->last_status);
    set_txt(s_ui.agent[3].detail, "%u / %u", (unsigned)st->ok_count, (unsigned)st->fail_count);
    set_txt(s_ui.agent[4].detail, "%s", st->last_err[0] ? st->last_err : "无");
    set_txt(s_ui.agent[5].detail, "%s", fmt_age(c1, sizeof c1, data_age_ms(st)));
    set_txt(s_ui.agent[6].detail, "%u KB",
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    set_txt(s_ui.agent[7].detail, st->has_zfs ? "%.1f GB · 命中 %.1f%%" : "未采集到 ZFS 数据", st->zfs_arc_gb, st->zfs_hit_pct);

    /* 协议：只在真比本机新时说清楚"哪些东西看不到"，不然用户会以为界面漏了数据。 */
    if (st->proto <= 0)        set_txt(s_ui.agent[8].detail, "未上报（旧版应用，按 v1 读）");
    else if (st->proto > FNOS_PROTO_KNOWN)
        set_txt(s_ui.agent[8].detail, "v%d（本机只认到 v%d，新字段会忽略）", st->proto, FNOS_PROTO_KNOWN);
    else                       set_txt(s_ui.agent[8].detail, "v%d", st->proto);

    /* 采集段：只列不正常的那几段（正常的没必要占地方），最多两段，状态词原样翻。 */
    {
        char seg[160];
        int n = 0, bad = 0;
        if (st->nmods == 0) {
            snprintf(seg, sizeof seg, "%s", "未上报（旧版应用）");
        } else {
            for (int i = 0; i < st->nmods; i++) if (strcmp(st->mods[i].status, "ok") != 0) bad++;
            n = snprintf(seg, sizeof seg, "%d 段", st->nmods);
            if (bad == 0) {
                if (n < (int)sizeof seg) snprintf(seg + n, sizeof seg - n, "%s", " · 全部正常");
            } else {
                int shown = 0;
                for (int i = 0; i < st->nmods && shown < 2; i++) {
                    if (strcmp(st->mods[i].status, "ok") == 0) continue;
                    if (n < 0 || n >= (int)sizeof seg) break;
                    n += snprintf(seg + n, sizeof seg - n, "%s%s %s",
                                  shown == 0 ? " · " : "、", st->mods[i].name,
                                  mod_status_cn(st->mods[i].status));
                    shown++;
                }
                /* 省略号只在"确实还有没显示出来的"时候加：明明只有两段异常
                   却画个 …，会让人以为还有别的段出问题。 */
                if (bad > shown && n > 0 && n < (int)sizeof seg)
                    snprintf(seg + n, sizeof seg - n, "%s", " …");
            }
        }
        set_txt(s_ui.agent[9].detail, "%s", seg);
    }
    if (st->nalerts == 0) {
        for (int i = 0; i < UI_ROWS_ALERT; i++) row_visible(&s_ui.alert[i], false);
        set_txt(s_ui.alert_none, "%s",
                !st->ever_ok ? (why ? why : "等待告警数据")
                             : !st->online ? (why ? why : "采集端离线\n告警状态未知")
                                           : "暂无告警事件");
        if (s_ui.alert_none) lv_obj_remove_flag(s_ui.alert_none, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s_ui.alert_none) lv_obj_add_flag(s_ui.alert_none, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < UI_ROWS_ALERT; i++) {
            if (i >= st->nalerts) {
                row_visible(&s_ui.alert[i], false);
                continue;
            }
            if (!s_ui.alert[i].detail) continue;
            row_visible(&s_ui.alert[i], true);
            /* agent 的告警级别是 "crit" / "warn" / "info"（见 nas/fnos-agent.py:_alerts） */
            uint32_t ac = (strcmp(st->alerts[i].lv, "crit") == 0) ? KK_DANGER
                        : (strcmp(st->alerts[i].lv, "warn") == 0) ? KK_WARN : KK_T3;
            lv_obj_set_style_text_color(s_ui.alert[i].detail, lv_color_hex(ac), 0);
            set_txt(s_ui.alert[i].detail, "%s", st->alerts[i].m);
        }
    }
}

/* ── tick / 生命周期 ──────────────────────────────────────────────── */
static void ui_tick(lv_timer_t *t)
{
    (void)t;
    if (s_night_req != s_night) {
        s_night = s_night_req;
        night_apply();
    }

    /* 出厂固件没有 Wi-Fi 凭据：开机 ~3 秒（等网络层读完 NVS 再判断）后自动把配网卡
       推出来 —— 用户的原话是"固件刷好之后，也需要提示用户接入内网 WiFi"，而他看到的
       本来只是一块"离线"的屏，不知道该点哪里。用户主动关过就不再弹（入口留在顶栏）。 */
    static int boot_ticks;
    if (boot_ticks >= 0 && ++boot_ticks > 6) {
        boot_ticks = -1;
        if (!fnos_net_configured() && !s_wifi_dismissed && !s_wifi_open && s_ui.wifi_card) {
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
        s_cpu.count = s_mem.count = s_rx.count = s_tx.count = 0;
    }
    for (int i = 0; i < n; i++) {
        kk_series_push(&s_cpu, smp[i].cpu);
        kk_series_push(&s_mem, smp[i].mem);
        kk_series_push(&s_rx,  smp[i].rx_kbs);
        kk_series_push(&s_tx,  smp[i].tx_kbs);
    }
    if (n > 0) s_seq = next;

    refresh();
    pair_refresh();
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
    default: build_p3(p); break;     /* 到不了（页数由 FNOS_UI_PAGE_COUNT 决定） */
    }
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

void fnos_ui_create(void)
{
    /* 自适应：几何表按真实显示尺寸推导一次，之后所有页面只读它。
       放在建任何构件之前 —— 建的时候就把坐标算进样式里了，之后再改不生效。 */
    kk_metrics_recompute(KK_SCR_W, KK_SCR_H);
    if (getenv("KK_METRICS_TRACE")) kk_metrics_dump();
    if (s_created) return;

    kk_series_init(&s_cpu, s_cpu_buf, UI_HIST);
    kk_series_init(&s_mem, s_mem_buf, UI_HIST);
    kk_series_init(&s_rx,  s_rx_buf,  UI_HIST);
    kk_series_init(&s_tx,  s_tx_buf,  UI_HIST);
    memset(&s_st, 0, sizeof s_st);

    lv_obj_t *scr = lv_screen_active();
    s_ui.screen = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(KK_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    build_rail(scr);
    build_header(scr);

    lv_obj_t *content = kk_panel_create(scr, kk_rect(0, 0, 0, 0, KK_RAIL_W, KK_HEAD_H,
                                                     UI_CONTENT_W, UI_CONTENT_H));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);

    for (int i = 0; i < FNOS_UI_PAGE_COUNT; i++) {
        s_ui.page[i] = build_page(content, i);
        kk_swipe_attach(s_ui.page[i], swipe_next, swipe_prev);
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
    night_apply();

    lv_timer_create(ui_tick, 500, NULL);
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

void fnos_ui_set_page(int idx)
{
    if (!s_created) return;
    const int n = FNOS_UI_PAGE_COUNT;
    while (idx < 0) idx += n;
    while (idx >= n) idx -= n;
    /* 导航选择页面，即使点的是当前系统页，也要退出配对/诊断覆盖层。 */
    pair_show(false);
    if (s_wifi_open) wifi_show(false);
    s_diagnostics = false;
    lv_obj_add_flag(s_ui.diagnostics, LV_OBJ_FLAG_HIDDEN);
    set_txt(s_ui.diagnostics_label, "%s", "采集诊断");
    if (idx == s_page) return;
    s_page = idx;
    if (idx == 3) lv_obj_remove_flag(s_ui.diagnostics_button, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.diagnostics_button, LV_OBJ_FLAG_HIDDEN);

    // 原子换页：本板是"全屏页 + partial buffer"，双页位移动画会留残影，只做整页切换
    for (int i = 0; i < n; i++) {
        if (!s_ui.page[i]) continue;
        lv_obj_set_pos(s_ui.page[i], 0, 0);
        lv_obj_add_flag(s_ui.page[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.page[idx]) lv_obj_remove_flag(s_ui.page[idx], LV_OBJ_FLAG_HIDDEN);
    nav_select(idx);
    refresh();
    lv_obj_invalidate(lv_screen_active());
}

int fnos_ui_page(void)
{
    return s_page;
}

void fnos_ui_set_night(bool on)
{
    s_night_req = on;                            // 只置标志，落地在 ui_tick
}
