#pragma once
// 手写视觉动效 partial（KK 的 View partial 等价物）：
// 只允许操作视觉属性（颜色/透明度/位置/动画），不碰 Store、不碰业务、不调生命周期。
#include "fnos_dash_view.generated.h"

void FnosDashAnim_SelectNav(fnos_dash_view_t *v, int page);  // 导航选中态 + 指示条滑动
void FnosDashAnim_SetNight(fnos_dash_view_t *v, bool on);    // 夜间配色
