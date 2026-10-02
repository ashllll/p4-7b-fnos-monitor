#pragma once
// 手写业务 partial 的接口（KK 的 <PackageId>Controller.cs 等价物）。
// 事件入口 OnXxxRequested 由生成的 view 头文件声明、本 partial 实现。
// 规则：只有 Controller 写 Store；View/Binder 不碰业务（fnos_data / fnos_net）。
#include "fnos_dash_view.generated.h"
#include "fnos_dash_store.generated.h"

void FnosDashboardController_Init(fnos_dash_view_t *v, fnos_dash_store_t *s);
void FnosDashboardController_Tick(void);   // 500 ms，LVGL 任务里调用
