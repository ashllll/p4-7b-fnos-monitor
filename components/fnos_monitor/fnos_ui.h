#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fnos_data.h"     /* fnos_status_t：fnos_ui_link_reason() 要按值看它的 last_err */

// UIManager（KK_UI 生命周期层）：
//   * 开机 Preload：用 ui_kit（LVGL 原生 flex）手写五页（JSON 清单 / 代码生成 / MVVM-C 已全部删除）
//   * Open = 原子换页（先隐藏全部旧页并归零偏移，再显示目标页，最后整屏失效）
//   * 500 ms LVGL tick 拉 fnos_data 快照并刷文案（本文件是唯一的 LVGL 写入者）
//   * set_night 只置标志（esp_timer 任务调用），落地在 LVGL 任务里（线程红线）
//
// 必须在持有显示锁（bsp_display_lock）时调用 fnos_ui_create()。

/* 六页：总览 / 存储 / 网络 / 系统 / 温度 / 告警。配对、设置（原"采集诊断"）与配网
   都是覆盖层，不是页（见 fnos_ui_set_page 注释）。
   ⚠️ 改这个数字必须同时改 fnos_ui.c 的 NAV_TXT 与 icons[]：两张表都由
   _Static_assert 卡着，少一条编译期就报错，不会等到真机上出现"空导航项压住入口"。 */
#define FNOS_UI_PAGE_COUNT 6

#ifdef __cplusplus
extern "C" {
#endif

void fnos_ui_create(void);        // 幂等：Preload + Open(0)
void fnos_ui_set_page(int idx);   // 0..FNOS_UI_PAGE_COUNT-1，越界循环

/* 主机侧自描述：把卡片几何吐成 [card] x= y= w= h= 若干行，
   tools/preview/scroll_gap.js 用它当几何唯一来源（别再手抄卡片表）。
   只给主机预览工具用，固件里没人调。 */
void fnos_ui_dump_card_map(void);
/* Complete the current transition for deterministic previews / diagnostics. */
void fnos_ui_motion_settle(void);
int  fnos_ui_page(void);
void fnos_ui_set_night(bool on);  // 夜间配色请求（异步落地）
/* 串口 'page n' 用：只置标志，下一跳 ui_tick 里换页（LVGL 只许在 LVGL 任务调）。
   实机验收时人不在板子跟前、又没有触摸自动化，靠它把五页逐页拍下来。 */
void fnos_ui_request_page(int idx);
/* 串口 'temp n' 用：切到温度页并展开第 n 台设备的通道网格（同样只置标志）。
   加它是因为"展开态"必须先点一下才看得到，而实机验收时人不在板子跟前。 */
void fnos_ui_request_temp_expand(int idx);

/* 测量台架（fnos_perf.c 的合成手势）专用：**只读**几何探针，屏幕坐标。
   加它们是因为"按下→首帧"必须打在真实控件上，而坐标只有 UI 自己知道；
   要点的导航按钮、要滑的显示区都随分辨率/布局变，脚本里写死坐标等于在测别的东西。
   对象还没建或索引越界时返回 false。生产代码不调。 */
bool fnos_ui_nav_center(int idx, int32_t *x, int32_t *y);
bool fnos_ui_swipe_point(int dir, int32_t *x, int32_t *y);

/* 把采集端写在 last_err 里的失败标签翻成用户能照着处理的一句话；不认识的标签返回 NULL。
   它是 refresh() 里那行"为什么连不上"的唯一来源，**公开出来是为了能被断言**：
   主机预览逐个标签核对"翻得出话、且每句都不一样"，否则某个失败场景下那行字会
   整条消失而没人发现。纯函数，不碰任何状态。 */
const char *fnos_ui_link_reason(const fnos_status_t *st);

#ifdef __cplusplus
}
#endif
