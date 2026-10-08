# v3 现状盘点（UniFi 令牌版）—— KK_UI_UMG 重构输入

> 只读盘点自 `/Users/llll/code/esp/p4-7b-fnos-monitor`，坐标与颜色均逐字抄自源码。
> 行数校正：`fnos_view.c` 实际 **1034 行**（盘点任务里写的 1070 不符）；`fnos_ui.c` 341 行相符。
> 其余：`fnos_data.c` 568 / `fnos_data.h` 76 / `fnos_ui.h` 112 / `fnos_fonts.h` 14 / `fnos_view.h` 17 / `fnos_net.c` 212 / `main/main.cpp` 147。

---

## 0. 文件清单

| 文件 | 行数 | 角色 |
|---|---|---|
| `components/fnos_monitor/fnos_view.c` | 1034 | 页面构建 + 刷新 + 翻页 + 夜间（核心） |
| `components/fnos_monitor/fnos_ui.h` / `fnos_ui.c` | 112 / 341 | 设计令牌 + 复用构件（纯视觉层） |
| `fnos_data.h` / `fnos_data.c` | 76 / 568 | 1 Hz 轮询 + JSON 解析 + 快照/曲线环形缓冲 |
| `fnos_net.h` / `fnos_net.c` | 22 / 212 | Wi-Fi STA（ESP-Hosted/C6）+ SNTP + RSSI 缓存 |
| `fnos_fonts.h` | 14 | 字体声明（gen_fonts.sh 生成） |
| `fnos_config.h` / `fnos_config.example.h` | 16 / 21 | 本地凭据（gitignored）/ 模板 |
| `main/main.cpp` | 147 | 启动、显示/触摸配置、夜间 esp_timer |
| `docs/ui-redesign.md` | 130 | **设计合同 v2**（与实现有偏差，见 §9.18） |
| `docs/ui-unifi.md` | 66 | v3 UniFi 视觉体系（实际采用的令牌） |
| `tools/gen_fonts.sh` | — | 字体生成（含 RLE 禁用原因） |
| `nas/fnos-agent.py` | — | 采集端（告警文案为英文/ASCII） |

---

## 1. 版面常量与设计令牌（`fnos_ui.h` 逐字）

**版面**：`CK_SCR_W 1024` / `CK_SCR_H 600` / `CK_TOP_H 56` / `CK_RAIL_W 104` / `CK_BOT_H 40` / `CK_PAD 20` / `CK_GAP 16` / `CK_CONT_X (CK_RAIL_W+CK_PAD)`=**124** / `CK_CONT_Y (CK_TOP_H+16)`=**72** / `CK_CONT_W (CK_SCR_W-CK_CONT_X-CK_PAD)`=**880** / `CK_CONT_H (CK_SCR_H-CK_BOT_H-CK_CONT_Y-16)`=**472** / `CK_RADIUS 16` / `CK_BAR_H 6`。

**颜色令牌**：

| 令牌 | hex | 令牌 | hex |
|---|---|---|---|
| `CK_BG` | `0x0E1114` | `CK_ACCENT` | `0x3B82F6`（未用） |
| `CK_PANEL` | `0x1A1E24` | `CK_ACCENT_D` | `0x0559D6`（未用） |
| `CK_PANEL_HI` | `0x22272E` | `CK_CPU` | `0x3B82F6` |
| `CK_NAV_SEL` | `0x14243A` | `CK_MEM` | `0x22D3EE` |
| `CK_GUIDE` | `0x262B33` | `CK_TEMP` | `0xF5A623` |
| `CK_LINE` | `0x22272E`（未用） | `CK_NET_DOWN` | `0x22D3EE` |
| `CK_TRACK` | `0x2A3038` | `CK_NET_UP` | `0x3B82F6` |
| `CK_TEXT` | `0xF5F7FA` | `CK_ZFS` | `0x22D3EE` |
| `CK_DIM` | `0x9BA3AB` | `CK_OK` | `0x22C55E` |
| `CK_IDLE` | `0x5A6472` | `CK_WARN` | `0xF5A623` |
| | | `CK_DANGER` | `0xFF3B30` |

未使用项：`CK_ACCENT`、`CK_ACCENT_D`、`CK_LINE`、`CK_GAP`、`CK_BAR_H`（仅注释）；死 API：`ck_arc` / `ck_arc_set` / `ck_chip_pulse` / `ck_set_opa`。

**字体映射**：`CK_F_HERO=&ui_font_num_56`、`CK_F_NUML=&ui_font_num_44`、`CK_F_NUMM=&ui_font_num_28`、`CK_F_NUMS=&ui_font_num_17`、`CK_F_TITLE=&ui_font_num_28`、`CK_F_LABEL=&ui_font_txt_15`、`CK_F_META=&ui_font_txt_13`、`CK_F_CVERDICT=&ui_font_cjk_40`、`CK_F_CTITLE=&ui_font_cjk_24`（未用）、`CK_F_CLABEL=&ui_font_cjk_17`、`CK_F_CMETA=&ui_font_cjk_13`。

**构件语义**（`fnos_ui.c`）：
- `ck_obj(parent,x,y,w,h,bg,radius,clickable)` = `lv_obj_create` + pos/size + radius + bg(LV_OPA_COVER) + border 0 + outline 0 + pad_all 0；`clickable=false` 时清 `LV_OBJ_FLAG_CLICKABLE`；始终清 `SCROLLABLE`/`SCROLL_CHAIN`。
- `ck_tile` = `ck_obj(CK_PANEL, CK_RADIUS)` + `border_width 1` + `border_color CK_GUIDE`。
- `ck_label` = label + 字体 + 颜色，**仅 `CK_F_LABEL` 加 `text_letter_space=1`**，pos(x,y)，清 CLICKABLE。
- `ck_label_r(parent,txt,f,color,right_pad,y)` = 先 `ck_label(0,y)`，再 `lv_obj_align(LV_ALIGN_TOP_RIGHT, -right_pad, y)`。
- `ck_divider` = `ck_obj(w,1)`。
- `ck_bar_create` = track(CK_TRACK, 圆角 `h/2`) + fill(初始宽 2, CK_OK, `bg_grad_dir=HOR`)；`ck_bar_set_color(b,pct,fixed)`：clamp 0..100，fill 宽 `(int)(w*pct/100.0f+0.5f)`（pct>0 时最小 **3px**），再 `bar_gradient`（fixed 非 0 时两端同色；否则 pct≥85 → `CK_WARN→CK_DANGER`，≥60 → `CK_ZFS→CK_WARN`，其他 → `CK_NET_DOWN→CK_OK`）。
- `ck_bar_tick(parent,x,y,h)` = 1px 宽 `CK_GUIDE` 竖线。
- `ck_chip(parent,x,y,w,h,color)` = 圆角 `h/2` + `bg_opa LV_OPA_20` + `border 1px color`；`ck_chip_set` 更新边框/底色并**懒建 child0 标签**（`CK_F_CMETA`，`lv_obj_center`）。
- `ck_signal_bars(parent,x,y,out[4])`：4 根 `ck_obj(x+i*7, y+(19-hh[i]), 5, hh[i], CK_IDLE, 2)`，`hh={7,11,15,19}`；`ck_signal_set`：rssi≤-1 时 ≥-55→4 格、≥-65→3、≥-75→2、否则 1，亮格 `CK_OK`，暗格 `CK_IDLE`。
- `ck_trend_t` = **同位置两张 lv_chart**（bar 层：`TYPE_BAR`、`pad_column 2`、ITEMS `radius 1`、ITEMS `bg_opa LV_OPA_10`；line 层：`TYPE_LINE`、ITEMS `line_width 2`、INDICATOR `size 0`）；初始 `range 0..100`、`div_line_count 0,0`；series 指针存在 **obj 的 user_data**；`ck_trend_peak` 扫 y 数组并跳过 `LV_CHART_POINT_NONE`。
- `ck_icon(parent,x,y,kind,color)` 画布 26×26：OVERVIEW=四个 11×11 r3（`x,y` / `x+14,y` / `x,y+14` / `x+14,y+14`）；STORAGE=三条 `26×6 r3`（`y`/`y+10`/`y+20`）；NETWORK=四柱 5 宽 r2（`x,y+16,h10` / `x+7,y+10,h16` / `x+14,y+4,h22` / `x+21,y,h26`）；SYSTEM=外框 `26×26 CK_GUIDE r5` + 内 `22×22 CK_PANEL r4` + 中心 `12×12 color r3`。

---

## 2. 页面结构树（坐标相对父对象，逐字）

屏幕 `lv_screen_active()`：bg `CK_BG`(COVER)，清 SCROLLABLE（`s_chrome_scr`）。
内容区 `s_chrome_content = ck_obj(scr, 124, 72, 880, 472, CK_BG, 0, false)`。
4 页均为内容区子对象：`make_page = ck_obj(content, 0, 0, 880, 472, CK_BG, 0, true)` + `on_press(PRESSED)`/`on_release(RELEASED)`（:301-309）。页面坐标相对内容区原点，`apply_page()` 里 `set_pos(0,0)` 即回位。

### 顶栏 `build_top` (:245)

| 控件 | 类型 | 位置/尺寸 | 文本 | 字体 | 颜色 | 圆角/描边 |
|---|---|---|---|---|---|---|
| `s_chrome_top` | lv_obj | 0, 0, 1024, 56 | — | — | `CK_PANEL` | r0，无描边 |
| 分隔线 | lv_obj | 0, 55, 1024, 1 | — | — | `CK_GUIDE` | — |
| `s_hd_host` | label | x=20, y=18 | `"NAS"` | `CK_F_TITLE` | `CK_TEXT` | — |
| `s_hd_endpoint` | label | 初 x=150,y=26；实际 `place_unit(host,endpoint,20,26)` → x=20+主机名宽+10 | 运行时填 | `CK_F_META` | `CK_DIM` | — |
| `s_hd_chip` | chip | 420, 17, 184, 30 | `"等待数据"`（初始） | 子标签 `CK_F_CMETA` | `CK_WARN` | r=15，bg_opa 20%，1px 边框 |
| `s_hd_clock` | label | `TOP_RIGHT, -20, 14`（右缘 1004） | `"--:--"` | `CK_F_NUMM` | `CK_TEXT` | — |
| `s_sig[4]` | lv_obj×4 | 起点 874,20；`x=874+i*7`，`y=32/28/24/20`，`5×7/11/15/19` | — | — | `CK_IDLE`（亮 `CK_OK`） | r=2 |

### 导航栏 `build_rail` (:268)

- `s_chrome_rail = ck_obj(scr, 0, 56, 104, 504, CK_BG, 0, false)`
- `names[4] = { "总览", "存储", "网络", "系统" }`
- `s_nav[i] = ck_obj(scr, 12, 74+96*i, 80, 88, CK_BG, 12, true)` → y=74/170/266/362
  - `ck_icon(item, 14, 14, i, CK_DIM)`（kind 0..3）
  - `s_nav_lbl[i] = ck_label(item, names[i], CK_F_CLABEL, CK_DIM, 0, 52)` + `align(TOP_MID, 0, 52)`（水平居中）
  - 事件：`on_nav(PRESSED)`、`on_nav(CLICKED)`，`user_data=(void*)(intptr_t)i`（:279-280）
- `s_nav_ind = ck_obj(scr, 12, 86, 4, 64, CK_TEXT, 2, false)` —— **白色 4×64 r2** 指示条（不是文档说的品牌蓝）

### 底栏 `build_bottom` (:286)

- `s_chrome_bot = ck_obj(scr, 0, 560, 1024, 40, CK_PANEL, 0, false)`；分隔 `ck_divider(scr, 0, 560, 1024, CK_GUIDE)`
- `s_ft_poll = ck_label(f, "", CK_F_CMETA, CK_DIM, 20, 10)`
- `s_ft_alert_dot = ck_obj(f, 556, 14, 8, 8, CK_OK, 4, false)`（=1024-20-448）
- `s_ft_alert_lbl = ck_label(f, "无告警", CK_F_CMETA, CK_OK, 572, 9)`（=1024-20-432）

### P0 总览 `build_overview` (:331)

```
页 880×472
├ 健康卡 h = ck_tile(p, 0, 0, 430, 176)
│   ├ "NAS 健康"        CK_F_CLABEL CK_DIM   (20,14)
│   ├ s_ov_verdict      ck_label "等待"  CK_F_CVERDICT CK_WARN (20,46)
│   ├ s_ov_verdict_sub  "等待首个数据帧" CK_F_CMETA CK_DIM (20,128)
│   └ s_ov_reason       ""  CK_F_CMETA CK_DANGER (20,150)
├ 存储空间卡 sp = ck_tile(p, 444, 0, 436, 176)
│   ├ "存储空间" CK_F_CLABEL CK_DIM (16,12)
│   └ 6 行 i=0..5, y = 42 + i*22 (42/64/86/108/130/152)
│       ├ s_ov_vol_name[i] ck_label("", CK_F_NUMS, CK_DIM, 16, y-4)
│       ├ s_ov_vol_bar[i]  ck_bar_create(sp, 130, y, 220, 8)   轨道 CK_TRACK r=4
│       └ s_ov_vol_pct[i]  ck_label_r("", CK_F_NUMS, CK_TEXT, 16, y-5)
├ 4 张遥测卡 ck_tile(p, x, 190, 209, 118)
│   ├ name    CK_F_CLABEL CK_DIM (14,10)
│   ├ s_ov_val[idx]   ck_label "--", CK_F_NUMM, CK_TEXT (14,38)
│   ├ s_ov_unit[idx]  ck_label "",  CK_F_META, CK_DIM   (85,53)   ← 运行时改 x
│   ├ s_ov_sub[idx]   ck_label "",  CK_F_CMETA, CK_IDLE (14,92)
│   └ with_bar: ck_bar_create(&s_ov_mbar[idx], t, 14, 80, 181, 6)，fill 双色=身份色
│     参数：(0,x=0,"CPU",CK_CPU,true) (1,223,"内存",CK_MEM,true)
│           (2,446,"最高温度",CK_TEMP,true) (3,669,"运行时长",CK_ZFS,false)
└ 趋势卡 tr = ck_tile(p, 0, 322, 880, 150)
    ├ "CPU / 内存 · 百分比 · 近 3 分钟" CK_F_CMETA CK_DIM (16,8)
    ├ 图例 ck_obj(640,12,10,10,CK_CPU,3) + "CPU" CK_F_META CK_CPU (658,8)
    │      ck_obj(750,12,10,10,CK_MEM,3) + "MEM" CK_F_META CK_MEM (768,8)
    ├ s_ov_cpu = ck_trend_create(tr, 14, 34, 852, 46, CK_CPU, TREND_PTS)
    └ s_ov_mem = ck_trend_create(tr, 14, 88, 852, 46, CK_MEM, TREND_PTS)   两者 range 100
```

### P1 存储 `build_storage` (:381)

```
├ Hero 卡 h = ck_tile(p, 0, 0, 880, 150)
│   ├ "已用容量 / 总容量"  CK_F_CLABEL CK_DIM (20,12)
│   ├ s_st_hero = ck_label "--", CK_F_HERO, CK_TEXT (20,34)
│   ├ s_st_used = ck_label "TB / 总量", CK_F_CLABEL, CK_DIM (250,74)
│   │             → place_unit(hero, used, 20, 80)：x=20+hero宽+10, y=80
│   ├ s_st_cnt  = ck_label_r "", CK_F_CMETA, CK_DIM (right 20, y=16)
│   ├ s_st_free = ck_label_r "", CK_F_CLABEL, CK_OK  (right 20, y=66)
│   └ 堆叠段 s_st_seg[i] = ck_obj(h, 20, 128, 0, 12, CK_NET_DOWN, 3, false) i=0..5（宽运行时算）
├ 卷格 volume_cell(p,i)：col=i%2, row=i/2；x=col*450(0/450)，y=162+row*58(162/220/278)
│   c = ck_tile(p, x, y, 430, 54)
│   ├ s_st_vname[i] CK_F_NUMS CK_TEXT (14,8)
│   ├ s_st_vfs[i]   CK_F_META CK_IDLE (120,12)
│   ├ s_st_vuse[i]  CK_F_META CK_DIM  (220,12)
│   ├ s_st_vpct[i]  ck_label_r CK_F_NUMS CK_TEXT (right 14, y=7)
│   ├ ck_bar_create(&s_st_vbar[i], c, 14, 36, 402, 10)
│   └ ck_bar_tick(c, 335, 32, 18) + ck_bar_tick(c, 375, 32, 18)   ← 14+(int)(402*0.80f)/0.90f
├ RAID 面板 r = ck_tile(p, 0, 336, 430, 136)
│   ├ "RAID 阵列" CK_F_CLABEL CK_DIM (16,10)
│   └ 4 行 y=34+i*20 (34/54/74/94)：name CK_F_NUMS CK_TEXT (16,y)
│      lvl CK_F_META CK_IDLE (110,y+4)、state CK_F_CMETA CK_OK (190,y+3)
└ 磁盘活动面板 d = ck_tile(p, 450, 336, 430, 136)
    ├ "磁盘活动" (16,10)
    └ 4 行 y=34+i*20：name CK_F_NUMS CK_TEXT (16,y)、val ck_label_r CK_F_NUMS CK_DIM (right 16)
```

### P2 网络 `build_network` (:415)

```
├ 双主值卡（k=0 下行 / k=1 上行）t = ck_tile(p, down?0:447, 0, 433, 170)
│   ├ 标题 "下行 DOWN"/"上行 UP"  CK_F_CLABEL  CK_NET_DOWN/CK_NET_UP (18,12)
│   ├ s_nw_*_v = ck_label "--", CK_F_NUML, CK_TEXT (18,44)
│   ├ s_nw_*_u = ck_label "KB/s", CK_F_LABEL, CK_DIM (20,94)
│   ├ s_nw_*_sub = ck_label "", CK_F_CMETA, CK_IDLE (18,126)
│   └ 迷你趋势 ck_trend_create(mini, t, 214, 44, 203, 96, color, TREND_PTS)，range 256
│       mini = down ? &s_nw_mini_down : &s_nw_mini_up
├ 吞吐趋势面板 tr = ck_tile(p, 0, 184, 880, 190)
│   ├ "吞吐趋势 · KB/s · 近 3 分钟" CK_F_CMETA CK_DIM (16,8)
│   ├ 图例 ck_obj(640,12,10,10,CK_NET_DOWN,3) + "DOWN" CK_F_META CK_NET_DOWN (658,8)
│   │      ck_obj(750,12,10,10,CK_NET_UP,3)  + "UP"   CK_F_META CK_NET_UP   (768,8)
│   ├ s_nw_down = ck_trend_create(tr, 14, 36, 760, 60, CK_NET_DOWN, TREND_PTS)
│   ├ s_nw_up   = ck_trend_create(tr, 14, 108, 760, 60, CK_NET_UP, TREND_PTS)  初始 range 256
│   └ s_nw_axis[i] = ck_label(tr, "", CK_F_META, CK_DIM, 774, 44+i*56) i=0..2
└ 4 张上下文瓦片：lbl = {"峰值下行","峰值上行","采集延迟","曲线采样"}
    w=(880-3*14)/4=209；ck_tile(p, i*223, 388, 209, 84)
    s_nw_ctx_l[i] CK_F_CLABEL CK_DIM (14,10)；s_nw_ctx_v[i] CK_F_NUMS CK_TEXT (14,38)
```

### P3 系统 `build_system` (:460)

```
├ 容器面板 c = ck_tile(p, 0, 0, 430, 472)
│   ├ "容器" CK_F_CLABEL CK_DIM (16,12)；s_sy_cnt ck_label_r CK_F_CMETA CK_DIM (right 16, y=16)
│   ├ 6 行 y=52+i*44 (52/96/140/184/228/272)
│   │   dot ck_obj(c, 16, y+8, 10, 10, CK_IDLE, 5)、name CK_F_NUMS CK_TEXT (36,y)、sub CK_F_CMETA CK_DIM (36,y+22)
│   ├ ck_divider(c, 16, 332, 398, CK_GUIDE)；"告警" CK_F_CLABEL CK_DIM (16,344)
│   └ 3 行告警：dot ck_obj(c, 16, 386+i*30, 8, 8, CK_IDLE, 4)、label CK_F_CMETA CK_DIM (34, 380+i*30)
├ 温度面板 t = ck_tile(p, 450, 0, 430, 238)
│   ├ "温度" (16,12)
│   └ 7 行 y=42+i*28 (42/70/98/126/154/182/210)
│       name CK_F_LABEL CK_DIM (16,y)、ck_bar_create(&s_sy_tbar[i], t, 150, y+5, 170, 8)、
│       val ck_label_r CK_F_NUMS CK_TEXT (right 16, y=y-2)
└ 采集端点面板 k = ck_tile(p, 450, 252, 430, 220)
    ├ "采集端点" (16,10)
    └ 8 行 y=36+i*20 (36..176)：label CK_F_CMETA CK_DIM (16,y)、value ck_label_r CK_F_CMETA CK_TEXT (right 16)
```

---

## 3. 动态数据绑定表（格式串逐字；全部在 `view_tick` 每 **500 ms** 无条件全量刷新）

数据源：`s_st = fnos_status_t`；`have() = s_st.ever_ok`；`data_age_s() = (now_ms - recv_ms)/1000`（recv_ms=0 → -1）。

### update_top (:509)

| 控件 | 格式/取值 | 来源 |
|---|---|---|
| `s_hd_host` | `s_st.host[0] ? s_st.host : "NAS"` | `host` |
| `s_hd_endpoint` | `"%s:%d  %s"`（FNOS_HOST, FNOS_PORT, `ip[0]?ip:"无 IP"`） | 配置 + `fnos_net_ip()` |
| `s_hd_chip` | LIVE `"%llds"`→`"在线 · %llds"` `CK_OK`；WARMING `"等待数据"` `CK_WARN`；STALE `"陈旧 %llds"` `CK_WARN`；OFFLINE `"采集端离线"` `CK_DANGER` | trust_state + age |
| `s_hd_clock` | `"%02d:%02d"`（tm_year>120）否则 `"--:--"` | `time()`/`localtime_r` |
| `s_sig` | `ck_signal_set(s_sig, fnos_net_rssi())` | Wi-Fi RSSI |

### update_bottom (:539)

| 控件 | 格式 | 来源 |
|---|---|---|
| `s_ft_poll` | have: `"采集 %s:%d · 正常 %u / 失败 %u · %d ms"`(HOST,PORT,ok_count,fail_count,http_ms)；else `"采集 %s:%d · 等待首个数据帧"` | `ok_count/fail_count/http_ms` |
| `s_ft_alert_lbl`/dot | OFFLINE→`CK_DANGER`+(`last_err[0]?last_err:"采集端不可达"`)；`nalerts>0`→(`alerts[0].lv=="crit"?CK_DANGER:CK_WARN`)+`alerts[0].m`；否则 `"无告警"` `CK_OK` | `alerts/last_err` |

### update_overview (:564)

| 控件 | 格式 | 来源 |
|---|---|---|
| `s_ov_verdict` | `"离线"`/`"危险"`/`"注意"`/`"等待"`/`"正常"`，色 `CK_DANGER/CK_DANGER/CK_WARN/CK_WARN/CK_OK` | trust + crit/warn 计数 |
| `s_ov_verdict_sub` | `"6 项检查 · %d 条告警"`(nalerts) 或 `"等待首个数据帧"` | `nalerts` |
| `s_ov_reason` | `"采集端不可达：%s"`(last_err?:"poll failed")；`"%s  (+%d)"`/`"%s"`(alerts[0].m, nalerts-1) | `alerts/last_err` |
| `s_ov_val[0]` | `"%.0f"`(cpu.pct)；unit `"%"`（x=14+val宽+8,y=53）；sub `"负载 %.2f · %d 核"`(load1,cores)；条 `(cpu.pct, CK_CPU)` | `cpu.pct/load1/cores` |
| `s_ov_val[1]` | `"%.0f"`(mem.pct)；`"%"`；sub `"%.1f / %.0f GB"`(used_mb/1024,total_mb/1024)；条 `(mem.pct, CK_MEM)` | `mem.*` |
| `s_ov_val[2]` | `"%.0f"`(temps 最大 c)；unit `"C"`；sub `"%s %.0f°C"`(temps[0].n,temps[0].c) 或 `"--"`；条 `(hot, CK_TEMP)` | `temps[]` |
| `s_ov_val[3]` | `fmt_uptime`：`"%ud %02uh"`/`"%uh %02um"`/`"%um"`；unit `""`；sub `"NAS 持续运行"`（无条） | `uptime_s` |
| `s_ov_vol_name/pct` | name=`vols[i].mnt`；pct `"%.0f%%"`，色 ≥90 `CK_DANGER` / ≥80 `CK_WARN` / ≥60 `CK_ZFS` / 其他 `CK_OK`；条 `ck_bar_set(pct)` | `vols[]` |

### update_storage (:659)

| 控件 | 格式 | 来源 |
|---|---|---|
| `s_st_hero` | `snprintf(b, tb?"%.1f":"%.0f", tb?used/1024.0f:used)`，`tb=(used>=1024)`；`s_st_used` = `"TB / 总量"`/`"GB / 总量"` | vols 求和 |
| `s_st_cnt` | `"%s / %s"`(fmt_cap(used), fmt_cap(total)) | 同上 |
| `s_st_free` | `"可用 %s"`(fmt_cap(free))；`fmt_cap`：gb≥1024 → `"%.1f TB"`(gb/1024) 否则 `"%.0f GB"` | 同上 |
| `s_st_seg[i]` | `wpx=(int)(825*(total_gb/total)+0.5f)`（min 8），x 从 20 起 `+wpx+3`，`set_pos(x,128)`；色 ≥85 `CK_DANGER`/≥70 `CK_WARN`/≥45 `CK_ZFS`/其他 `CK_NET_DOWN` | `vols[]` |
| `s_st_vname/vfs/vpct/vuse/bar` | mnt / fs / `"%.0f%%"`（≥90 危 / ≥80 警）/ `"%s / %s"`(fmt_cap(used_gb), fmt_cap(total_gb)) / `ck_bar_set(pct)` | `vols[]` |
| `s_st_raid_*` | `sync_pct>=0`→`"同步 %.0f%%"` `CK_NET_DOWN`；`ok`→`"正常 %d/%d"`(have,want) `CK_OK`；else `"降级 %s"`(state) `CK_DANGER` | `raid[]` |
| `s_st_io_name/val` | dev / `"R %.0f W %.0f"`(rd_kbs, wr_kbs) | `disks[]` |

### update_network (:773)

| 控件 | 格式 | 来源 |
|---|---|---|
| `s_nw_down_v/up_v` | `fmt_rate`：MB 态 `"%.2f"`(kbs/1024)；≥100 `"%.0f"`；<100 `"%.1f"`；迟滞 HI=1024/LO=950（静态 `s_mb_down`/`s_mb_up`）；单位 `"MB/s"`/`"KB/s"` | `net.rx_kbs/tx_kbs` |
| `s_nw_down_sub/up_sub` | `"累计接收 %s"` / `"累计发送 %s"`(fmt_cap) | `net.rx_total_gb/tx_total_gb` |
| `s_nw_ctx_v[0]/[1]` | `fmt_rate(ck_trend_peak(...))` | 图表峰值 |
| `s_nw_ctx_v[2]` | `"%d ms"` | `http_ms` |
| `s_nw_ctx_v[3]` | `"%lld"` | `s_hist_seq` |
| `s_nw_axis[i]` | `fmt_rate(top - (top/2)*i)`；`top=nice_max(max(peak_down,peak_up))`，min 64；4 条趋势 `ck_trend_range(top)` | 同上 |

### update_system (:823)

| 控件 | 格式 | 来源 |
|---|---|---|
| `s_sy_cnt` | `"%d / %d 运行中"`(up, ndocker) | `docker[].up` |
| `s_sy_dock/dock_s/dot` | n / `up?"运行中":s`；色 `CK_OK:CK_IDLE`；空槽清空 + dot `bg_opa=TRANSP` | `docker[]` |
| `s_sy_tname/tval/tbar` | n / `"%.1f°C"`；色 ≥75 `CK_DANGER`/≥60 `CK_WARN`/其他 `CK_OK`；条 `(c, c)` fixed | `temps[]` |
| `s_sy_alert_lbl/dot` | `alerts[i].m`，crit→`CK_DANGER` 否则 `CK_WARN`；无告警且 have：行 0 `"无告警"` `CK_OK` + dot，其余空 + dot TRANSP | `alerts[]` |
| KV `k[8]={"主机","端点","HTTP","轮询","最近错误","数据年龄","Wi-Fi","内部内存"}` | 0 `"%s"`(host?:"--")；1 `"%s:%d"`；2 `"%d ms (状态 %d)"`(http_ms,last_status)；3 `"%u / %u"`(ok,fail)；4 `"%s"`(last_err?:"无")；5 `fmt_age`(`"--"`/`"%llds"`/`"%lldm"`/`"%lldh"`，阈 90/5400)；6 rssi≠0→`"%s  %d dBm"`(ip?:"--",rssi) 否则 `"%s"`(ip?:"未连接")；7 `"%u KB"`(`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024`) | 混合 |

### 曲线 `drain_history` (:909)

`fnos_data_hist_read(s_hist_seq, smp, 12, &next)`；`static int div` **隔点入图**（`(div++ & 1)==0`）→ 每 2 秒一点；push `(int)(x+0.5f)` 到 `s_ov_cpu / s_ov_mem / s_nw_down / s_nw_up / s_nw_mini_down / s_nw_mini_up`；`s_hist_seq = next`。`TREND_PTS = CONFIG_FNOS_CHART_WINDOW/2 = 90`（=180 s），`HIST_PTS = 180`。

---

## 4. 事件与输入

| 事件 | 回调 / 行号 | 行为 |
|---|---|---|
| 导航项 `LV_EVENT_PRESSED` | `on_nav` (:213) | 项 bg 置 `CK_PANEL_HI`（0x22272E）并 return |
| 导航项 `LV_EVENT_CLICKED` | `on_nav` (:220) | `fnos_view_set_page((int)(intptr_t)user_data)` |
| 页面 `LV_EVENT_PRESSED` | `on_press` (:223) | `s_press_x = lv_indev_get_point().x`（静态全局，单指） |
| 页面 `LV_EVENT_RELEASED` | `on_release` (:231) | `dx<=-70` → 页+1；`dx>=70` → 页-1（阈值硬编码） |

- `fnos_view_set_page` (:202)：idx<0→3、idx≥4→0、同页 no-op。
- `apply_page` (:176)：**原子换页**——4 页先 `set_pos(0,0)`（回内容区原点）+ 全部 HIDDEN，再显示目标页（注释：全屏页 + partial buffer，双页位移动画会留残影）；导航项 bg `i==s_page?CK_NAV_SEL:CK_BG`、标签 `CK_TEXT:CK_DIM`；`s_nav_ind` 用 `lv_anim` **180 ms ease-out** 滑到 `y = CK_TOP_H + 18 + s_page*96 + 12`（86/182/278/374），exec_cb=`nav_ind_anim`(:263，仅 `lv_obj_set_y`)；最后 `lv_obj_invalidate(lv_screen_active())`。
- **不使用 `LV_EVENT_GESTURE`**（sdkconfig 未开 `LV_USE_GESTURE_RECOGNITION`），翻页纯靠 PRESS/RELEASE 位移差。
- 可点性：LVGL 9.5 默认全对象 `CLICKABLE`；`ck_obj(clickable=false)` 清标志（`fnos_ui.c:23-24`），标签/条/图表/圆点全清 → 点击落到最近可点祖先（页面对象或导航项）。
- 导航按压高亮**无独立还原**，只在 `apply_page()` 重涂全部导航项时复位。
- 自动轮播：`main.cpp:63 auto_page_cb`（仅 `CONFIG_FNOS_AUTO_PAGE_SEC>0`），当前 sdkconfig=0 → 未编译。

---

## 5. 定时与任务 / 数据落地

### LVGL timer

| 名 | 周期 | 回调 | 说明 |
|---|---|---|---|
| （无名，user_data=NULL） | **500 ms** | `view_tick` (:929，创建 :1032) | 落地夜间 → `fnos_data_get(&s_st)` → `drain_history` → 6 个 update_* 全量刷 |
| （条件编译） | `CONFIG_FNOS_AUTO_PAGE_SEC*1000` | `auto_page_cb` (main.cpp:63) | 自动翻页验收用 |
| （一次性 260 ms） | — | `chip_pulse_cb` (fnos_ui.c:197) | 仅 `ck_chip_pulse` 调用，**当前死代码** |

### esp_timer（esp_timer 任务）

| 名 | 周期 | 回调 | 说明 |
|---|---|---|---|
| `"night"` | 60 s + 启动即调 | `night_timer_cb` (main.cpp:41) | hour=(tm_year>120?tm_hour:12)；`APP_NIGHT_START(23)…APP_NIGHT_END(7)` 支持跨零点；变灯才 `bsp_display_brightness_set(45/12)` + `fnos_view_set_night(on)` |
| `"heap"` | 10 s | `heap_timer_cb` (main.cpp:71) | 仅 `CONFIG_FNOS_HEAP_DEBUG`（**当前 =y**） |
| `"wifi_retry"` | 一次性，`1000<<attempt`（cap 5，max 30 s） | `wifi_retry_cb` (fnos_net.c) | 只置 `s_connect_wanted`（ESP-Hosted 同步 RPC 最坏阻塞 5 s） |

### FreeRTOS 任务

`poll_task`（`"fnos_poll"`，栈 6 KB，`xTaskCreatePinnedToCoreWithCaps(..., MALLOC_CAP_SPIRAM)`，失败退回普通创建，优先级 4，`tskNO_AFFINITY`）：成功延时 `POLL_INTERVAL_MS=1000`；失败 backoff +1000 上限 5000 ms；HTTP 超时 3000 ms；rx 缓冲 8 KB、history 缓冲 32 KB（PSRAM）；每 30 轮打统计日志。

### 数据 → UI 的落地方式

**不是 volatile 标志，而是互斥量保护的快照**——`EXT_RAM_BSS_ATTR fnos_status_t s_status` + `SemaphoreHandle_t s_lock`（PSRAM BSS）；UI 每 tick `fnos_data_get(&s_st)`（锁超时 50 ms，拿不到锁返回 `s_valid` 并保留上一帧，绝不刷 "--"）；曲线环 `s_h_cpu/mem/rx/tx[600]`（float, PSRAM），UI 用 `fnos_data_hist_read(since_seq,...)` 增量拉，游标 `s_hist_seq`。

**volatile 标志清单**：`s_night_req`（fnos_view.c，esp_timer→LVGL 任务唯一入口）；`s_valid`、`s_session_reset`（fnos_data.c）；`s_connect_wanted`、`s_rssi`、`s_rssi_ms`、`s_online`（fnos_net.c）；`s_ip` 用 `portMUX` 临界区（事件任务写/LVGL 读）。

**夜间/背光红线**（fnos_view.c:965-972）：`fnos_view_set_night()` **只写标志**；`apply_night()` 在 LVGL 任务里改：bg `on?0x000000:CK_BG`（screen/rail/content）、面板 `on?0x0B0E12:CK_PANEL`（仅 top/bot）、6 条趋势 bar 层 `bg_opa on?LV_OPA_10:LV_OPA_20`（LV_PART_ITEMS），整屏 invalidate；不做 `opa_layered`。

---

## 6. 状态编码实现

**数据结构（fnos_data.h）**：`fnos_status_t{ ever_ok, online, recv_ms, fail_ms, http_ms, last_status, ok_count, fail_count, last_err[48], host[32], uptime_s, cpu{pct,load1,load5,load15,temp_c,cores,runq}, mem{total_mb,used_mb,avail_mb,pct}, net{ifname[16],rx_kbs,tx_kbs,rx_total_gb,tx_total_gb}, nvols→vols[8]{mnt[20],fs[12],total_gb,used_gb,free_gb,pct}, nraid→raid[6]{dev[10],lvl[10],state[14],ok,have,want,sync_pct}, ndisks→disks[8]{dev[10],rd_kbs,wr_kbs}, ntemps→temps[10]{n[12],c}, ndocker→docker[12]{n[24],up,s[40]}, has_zfs,zfs_arc_gb,zfs_hit_pct, nalerts→alerts[8]{lv[8],m[60]} }`；`fnos_sample_t{cpu,mem,rx_kbs,tx_kbs}`。

- **可信度**：`trust_t{TR_WARMING,TR_LIVE,TR_STALE,TR_OFFLINE}`（:148）；`trust_state()`（:155）：`!ever_ok→WARMING`；`!online→OFFLINE`；`age<3s→LIVE`（`STALE_MS 3000`）否则 `STALE`。
- **状态胶囊**（`ck_chip`）：圆角 `h/2`、`bg_opa 20%`、1px 同色边框、文字 `CK_F_CMETA` 居中；文案/色见 §3。
- **告警带**：底栏 8×8 r4 圆点 + 一行文案，`crit→CK_DANGER`、其余→`CK_WARN`、无→`CK_OK`；OFFLINE 优先显示错误串。P3 另有 3 行告警列表（dot 8×8 r4）。
- **严重度色阶（四套并存）**：容量数字 P0 `≥90/80/60`、P1 `≥90/80`；容量堆叠段 `≥85/70/45`；温度 `≥75/60`；条渐变 `bar_gradient ≥85/60`。
- **no-data**：一律 `"--"`（绝不 0 冒充）；数组项缺失 → 文字清空 + 条 fill 宽 0 + dot `bg_opa TRANSP`（隐藏空槽）。
- **身份色**（跨页稳定）：`CK_CPU/CK_MEM/CK_TEMP/CK_NET_DOWN/CK_NET_UP/CK_ZFS`；**严重度色** `CK_OK/CK_WARN/CK_DANGER` 只表健康度。
- `alert_count("crit"/"warn")` 是对 `alerts[i].lv` 的字符串比较；agent 会发 `"info"`——不被计数但计入 `nalerts`，底栏按 `WARN` 上色。

---

## 7. 字体清单（`tools/gen_fonts.sh` 生成；全部 bpp=4、`--no-compress`、`--force-fast-kern-format`）

| 符号名 | 字体/字重 | 字号 | 字符范围 | 用途 |
|---|---|---|---|---|
| `ui_font_num_56` | Inter-SemiBold | 56 | 仅 `"0123456789.,:%-+/ °"` | `CK_F_HERO` 存储 Hero |
| `ui_font_num_44` | Inter-SemiBold | 44 | 0x20-0x7E,0xB0,0xB7 | `CK_F_NUML` DOWN/UP 主值 |
| `ui_font_num_28` | Inter-Medium | 28 | 同上 | `CK_F_NUMM`/`CK_F_TITLE` 主机名、时钟、列表值 |
| `ui_font_num_17` | Inter-Medium | 17 | 同上 | `CK_F_NUMS` 行内数值/设备名 |
| `ui_font_txt_15` | Inter-Medium | 15 | 同上 | `CK_F_LABEL` 拉丁标签（letter_space=1） |
| `ui_font_txt_13` | Inter-Regular | 13 | 同上 | `CK_F_META` 单位/辅助 |
| `ui_font_cjk_40` | NotoSansSC-Medium | 40 | ASCII + CJK 扫描子集 | `CK_F_CVERDICT` 健康结论 |
| `ui_font_cjk_24` | NotoSansSC-Medium | 24 | 同上 | `CK_F_CTITLE`（**未使用**） |
| `ui_font_cjk_17` | NotoSansSC-Medium | 17 | 同上 | `CK_F_CLABEL` 中文标题/导航标签 |
| `ui_font_cjk_13` | NotoSansSC-Regular | 13 | 同上 | `CK_F_CMETA` 中文辅助/告警行 |

CJK 子集来源：脚本用 Python 扫 `fnos_view.c/fnos_ui.c/fnos_ui.h/fnos_view.h` 的**字符串字面量**（剥注释）→ 运行时来自 NAS 的字符串不在子集内；`nas/fnos-agent.py` 的告警文案是英文（如 `"RAID %s degraded (%s)"`），故目前安全。`fonts/` 还有 9 个**未进 CMake 的遗留字体**：`cjk_15/cjk_20/mono_20/mono_34/mono_52/mono_84/sans_15/sans_20/sans_24`。⚠️ 必须 `--no-compress`：RLE 字体在 LVGL 9.5 + `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2` 下有全局解压状态竞争 → 字形碎裂/闪烁。

---

## 8. 构建接线

- **`components/fnos_monitor/CMakeLists.txt`**：SRCS **显式列出**（不 glob，防新增字体漏编）：`fnos_net.c/fnos_data.c/fnos_ui.c/fnos_view.c` + 10 个 `fonts/ui_font_*.c`；`INCLUDE_DIRS` 组件根；`REQUIRES esp_wifi esp_netif esp_event esp_http_client esp_timer lwip nvs_flash espressif__cjson esp32_p4_wifi6_touch_lcd_7b espressif__esp_lvgl_adapter lvgl__lvgl`；PRIVATE `-Wno-format -Wno-unused-variable`。
- **`main/CMakeLists.txt`**：`SRCS "main.cpp"`；`REQUIRES fnos_monitor esp32_p4_wifi6_touch_lcd_7b lvgl_mem_psram nvs_flash`；PUBLIC `-Wno-missing-field-initializers`；`get_component_library("lvgl")` 加 `-Wno-attributes`，对 `lvgl__lvgl` 加 `-Wno-error=attributes -Wno-error=cpp`。
- **`main.cpp` 启动顺序**：`nvs_flash_init` → `fnos_net_start()` → `esp_lv_adapter_config_t{task_stack_size=12*1024, stack_in_psram=true}`（默认 false 必须显式改）→ `bsp_display_cfg_t{rotation=ESP_LV_ADAPTER_ROTATE_180, tear_avoid_mode=ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI, touch_flags={swap_xy=0,mirror_x=0,mirror_y=0}}` → `bsp_display_start_with_config` → `bsp_display_backlight_on()` 再 `bsp_display_brightness_set(APP_BL_PCT)` → `bsp_display_lock(0)` 内 `fnos_view_create()`（+条件 auto page timer）→ unlock → `fnos_data_start()` → esp_timer `"night"`(60s) →（可选）`"heap"`(10s)。
- **Kconfig**（menu "fnOS monitor"）：`CONFIG_FNOS_HEAP_DEBUG`（bool，默认 n，**当前 =y**）、`CONFIG_FNOS_AUTO_PAGE_SEC`（0..120，默认 0，当前 0）、`CONFIG_FNOS_CHART_WINDOW`（60..600，默认 180，当前 180）。无 project Kconfig.projbuild。
- **配置头**：`__has_include("fnos_config.h")` 否则 `fnos_config.example.h`；宏 `APP_WIFI_SSID/APP_WIFI_PASS/FNOS_HOST("192.168.0.119")/FNOS_PORT(8799)/FNOS_TOKEN/APP_TZ("CST-8")/APP_BL_PCT(45)/APP_NIGHT_START(23)/APP_NIGHT_END(7)/APP_BL_NIGHT_PCT(12)`（`FNOS_HOST/FNOS_PORT` 在 `fnos_data.c` 另有 `#ifndef` 兜底）。
- **工程级**：`components/` 另有 `esp32_p4_wifi6_touch_lcd_7b`（本地 BSP 副本，已打补丁 `buffer_height=50, use_psram=true`）与 `lvgl_mem_psram`（自定义 LVGL 分配器，CMake `WHOLE_ARCHIVE`）。sdkconfig 关键项：`CONFIG_LV_USE_CUSTOM_MALLOC=y`、`CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`、`CONFIG_LV_DEF_REFR_PERIOD=15`、`CONFIG_ESP_LVGL_ADAPTER_LVGL_THREAD_STACK_IN_PSRAM=y`、`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y`、`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`、`CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`、SPIRAM 全套、自定义分区 `partitions.csv`（nvs 0x9000/24K、phy 0xf000/4K、factory app 9M）；**未开** `CONFIG_LV_USE_GESTURE_RECOGNITION`。构建入口 `./idf.sh`（bash 包装：source IDF + `export ESP_IDF_VERSION=5.5`，修 ESP-Hosted 退回 SPI 而本板 C6 是 SDIO 的坑）。

---

## 9. 移植注意点

1. **坐标体系隐式耦合**：页面内坐标全部按内容区 880×472 排布；`CK_RAIL_W/CK_PAD/CK_TOP_H/CK_BOT_H` 任一改动，全页错位（`fnos_ui.h:41` 明写"别改这个值"）。
2. **列宽不统一**：P0 `430+444`(gap 14)、P1/P3 `430+450`(gap 20)、P2 `433+447`(gap 14)；末列常 878/880 收边。
3. **堆叠条总宽硬编码**：`const int W = 840 - 5*3`（fnos_view.c:687）与 `CK_CONT_W` 无计算关系，改版面即溢出。
4. **导航指示条三处耦合**：`apply_page` 的 `CK_TOP_H+18+s_page*96+12` 必须与 `build_rail` 的 `y=CK_TOP_H+18+i*96`、项高 88 同步改。
5. **底栏魔数**：告警带 x=`CK_SCR_W-CK_PAD-448/-432`；dot y=14 与 label y=9 靠字体高度手工对齐。
6. **依赖字体度量的定位**：顶部 endpoint `place_unit`（=20+主机名宽+10）、P0 单位 `x=14+val宽+8,y=53`、P1 hero 单位 `place_unit(hero,used,20,80)`——换字体/字号会飘。
7. **阈值四套并存**（90/80/60、90/80、85/70/45、75/60、条 85/60），且 v3 评审要求"数值统一白、颜色只给条"，但 P0/P1 百分比数值仍按阈值染色。
8. **夜间模式漏改卡片**：`apply_night` 只改 screen/rail/content 底 + top/bot 面板 + 趋势纹理；页面内所有 `ck_tile` 仍是 `CK_PANEL 0x1A1E24`，夜间对比度不降。
9. **直接改 LVGL 样式的地方**：`apply_page()`（导航 bg/文字色）、`update_*`（fill/圆点/堆叠段的 `set_style_bg_color`、`set_style_bg_opa`、`set_width`、`set_pos`）——都必须在 LVGL 任务里跑（红线注释 :967）。
10. **重复声明**：`s_sy_alert_dot/s_sy_alert_lbl` 在 :81 与 :85 重复声明。
11. **单位不一致**：`s_ov_unit[2]="C"`（无度符号），同卡副行 `"%.0f°C"`、P3 `"%.1f°C"`（num 字体含 °，可用）。
12. **图表实现**：每条趋势 = 两张重叠 lv_chart（bar 10% 面积层 + line 层），series 塞在 obj **user_data**；`ck_trend_peak` 必须跳过 `LV_CHART_POINT_NONE`；隔点入图靠 `static int div`（2 s/点），图表文案写死"近 3 分钟"，改 `CHART_WINDOW` 文案不变。
13. **`fmt_rate` 迟滞** 1024/950，状态存在静态 `s_mb_down/s_mb_up`；传 NULL（峰值/纵轴）则无迟滞。
14. **LVGL 9.5 CLICKABLE**：新增装饰对象必须清 CLICKABLE，否则吞点击；手势不能用 `LV_EVENT_GESTURE`。
15. **线程红线**：esp_timer 回调只能置标志（`s_night_req/s_connect_wanted/s_session_reset`）；`esp_wifi_*` 是 ESP-Hosted 同步 RPC（最坏 5 s）只能在 `poll_task` 走 `fnos_net_service()/fnos_net_poll_rssi()`；`fnos_view_create()` 必须持 `bsp_display_lock`。
16. **内存红线**：快照/历史/cJSON/HTTP 缓冲/任务栈全 PSRAM（cJSON 走 `cJSON_InitHooks`，满时退内部）；BSP 补丁 `use_psram=true`、`lvgl_mem_psram`(WHOLE_ARCHIVE)、`FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`、`ESP_LVGL_ADAPTER_LVGL_THREAD_STACK_IN_PSRAM` 缺一即复现"界面冻结 + 触摸失效"。
17. **字体再生成陷阱**：`gen_fonts.sh` 按 `fonts/` 下**所有** `ui_font_*.c` 重写 `fnos_fonts.h`（含 9 个未用字体），且只扫 4 个 UI 文件字面量（不含 `main.cpp`/运行时字符串）；新增中文要重跑脚本并手工同步 CMake 显式列表，否则链接 undefined reference。
18. **文档≠实现**（重写时别照文档抄）：`docs/ui-redesign.md` 是 v2 合同（顶栏 h=64、指示条 3px、身份色 `#3FA9F5/#37C4D6/#FF7A1A`、IBM Plex Mono hero84/52/34/20、stale 降 62% 不透明度、告警脉冲 300ms、首帧后才开背光），实现是 v3 UniFi（顶栏 56、指示条 4px 且用 `CK_TEXT` **白色**而非品牌蓝、Inter/Noto Sans SC、stale **无**降透明度、脉冲未接线、背光先亮再压到 45%）；`gen_fonts.sh` 头注释还写着 IBM Plex，命令实际是 Inter/NotoSansSC。
19. **行为小坑**：`apply_page` 对所有页 `set_pos(0,0)`（否则页面会盖住顶栏/导航——`build_bottom` 注释记录过该视觉事故）；`fnos_data_get` 锁超时沿用上一帧并返回 `s_valid`；`"info"` 告警不计数但仍计 `nalerts` 且底栏按 WARN 上色；P3 告警只显示前 3 条，P0 reason 只显示第 1 条 + `" (+N)"`。
20. **启动/调试**：`fnos_view_create` 幂等（`s_created`），4 页一次性全建（无懒加载）；trend 创建失败直接 return（不置 `s_created`，可重试）；当前 `CONFIG_FNOS_HEAP_DEBUG=y`，串口每 20 tick 刷 uidbg 坐标、每 10 s 刷堆日志，量产前应关。
