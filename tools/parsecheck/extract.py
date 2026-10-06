#!/usr/bin/env python3
"""从 components/fnos_monitor/fnos_data.c 里**原样**抠出解析状态帧的那几个函数。

为什么抠而不是抄：抄一份就等于"测的是我手抄的那份"，固件里改了测试不会知道。
被测的是：`jstr` / `jnum` / `jint` / `jbool` 四个取值助手 + `parse_status()`。

用法：python3 tools/parsecheck/extract.py <fnos_data.c> <输出 .c>
"""
import hashlib
import io
import os
import re
import sys

FUNCS = ["jnum", "jint", "jbool", "jstr",
         "temp_cmp",              # 温度行的固定排序（parse_status 调它）
         "parse_status", "parse_history",
         "fnos_data_hist_read"]


def grab(src, name):
    """抠一个函数的完整定义。

    **两点别改回去**：
      1. `static` 是可选的 —— `fnos_data_hist_read()` 是公开函数（声明在 fnos_data.h），
         写死 `^static ` 会直接找不到它。
      2. 函数体按**花括号配对**切，不用非贪婪正则 —— `parse_status()` 里有嵌套的
         `if (...) { … }`，`\{.*?\n\}` 会停在第一个内层块的收尾行上、抠出来少一半，
         而报出来的错看着像类型问题（certcheck 那边就踩过一次）。
    """
    m = re.search(r"^(?:static\s+)?[^\n]*?\b%s\([^\n]*?\)\n" % re.escape(name), src, re.M)
    if not m:
        print(f"✗ 在 fnos_data.c 里找不到 {name}() 的定义（改了签名就同步改 extract.py）")
        return None
    i = src.index("{", m.end() - 1)
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                break
    return src[m.start():j + 1]


def main():
    src_path, out_path = sys.argv[1], sys.argv[2]
    src = io.open(src_path, encoding="utf-8").read()
    parts = []
    for fn in FUNCS:
        body = grab(src, fn)
        if body is None:
            return 1
        parts.append(body)
    blob = "\n\n".join(parts)
    digest = hashlib.sha256(blob.encode("utf-8")).hexdigest()

    header = f'''/* 本文件由 tools/parsecheck/extract.py 从 {os.path.relpath(src_path)} 自动生成，不要手改。
 * 里面是 jstr/jnum/jint/jbool 与 parse_status() 的原文（sha256 {digest}）。 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "fnos_data.h"
#include "cJSON.h"

/* 抠出来的这几段会用到两个文件级静态：
     · s_nas_ts —— parse_status 把 NAS 的挂钟时间记在这儿（adopt_nas_time() 用）
     · s_h_*    —— 历史回填的环形缓冲，parse_history 往里写
   它们的值本身不参与解析，只是被赋值。缓冲尺寸照抄 fnos_data.h 的 FNOS_HIST_MAX。 */
int64_t s_nas_ts;
float s_h_cpu[FNOS_HIST_MAX], s_h_mem[FNOS_HIST_MAX];
float s_h_rx[FNOS_HIST_MAX], s_h_tx[FNOS_HIST_MAX];
int64_t s_seq;
fnos_status_t s_status;

/* 主机上没有 FreeRTOS，锁与日志用空实现顶掉——**顶掉的只是"锁"和"日志"这两件
   主机上不存在的事，解析逻辑本身是原文**。parse_history 在真机上是在持锁状态下
   写的，这里单线程跑，语义等价。 */
int s_lock = 1;                      /* 非 NULL：真机上 s_lock 是个已创建的互斥量 */
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((ms) / portTICK_PERIOD_MS)
#define portTICK_PERIOD_MS 1
#define portMAX_DELAY 0xFFFFFFFF
#define xSemaphoreTake(l, t) ((l) ? pdTRUE : 0)
#define xSemaphoreGive(l) ((void)0)
#define ESP_LOGI(tag, ...) ((void)0)
#define ESP_LOGW(tag, ...) ((void)0)
#define TAG "parsecheck"

#define static
{blob}
#undef static
'''
    io.open(out_path, "w", encoding="utf-8").write(header)
    print(f"抠出 {len(FUNCS)} 个函数：{len(blob)} 字节，sha256 {digest[:16]}…")
    return 0


if __name__ == "__main__":
    sys.exit(main())
