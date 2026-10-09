#pragma once
#include <stdbool.h>
#include <stdint.h>

// 触控/渲染台架（CONFIG_FNOS_UI_PERF_BENCH）。
//
// 为什么要有它：屏幕"跟不跟手、点一下多久有反应"此前只能靠肉眼描述，而宿主的
// 32ms/180ms 预算是**虚拟时间**（docs 明确写了不能当设备帧率证据）。这里把两件事
// 变成可复现的数字：按下→首帧（P1）与拖动期帧间隔（P2），外加 ui_tick 耗时与各任务
// CPU 占比，命令只有串口 `bench` 一条（见 fnos_perf.c）。
//
// 关掉配置时下面三个函数都是空实现：生产固件里不建定时器、不注册回调、不读计数器 ——
// 仪表必须能证明自己没改变被测对象。

void fnos_perf_init(void);                 // 幂等；在 LVGL 任务里、持有显示锁时调
void fnos_perf_note_tick(uint32_t us);     // ui_tick 一次的总耗时
bool fnos_perf_cli(char *line);            // 串口命令 hook；没认出来返回 false
