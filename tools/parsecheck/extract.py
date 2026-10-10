#!/usr/bin/env python3
"""从 components/fnos_monitor 的 C 源里**原样**抠出解析状态帧的那几个函数。

为什么抠而不是抄：抄一份就等于"测的是我手抄的那份"，固件里改了测试不会知道。
被测的是：字符串 arena（`snapshot_malloc` / `owned_alloc` / `fnos_status_*`）、
cJSON 取值助手（`item` / `number` / `string` / `array` / `count`）、温度排序
（`temp_cmp`）、字段解析（`fnos_status_parse`），以及薄壳与历史回填
（`parse_status` / `parse_history` / `fnos_data_hist_read`）。

**解析层搬过家**：1.2.5 之前字段解析和 `jstr/jnum/jint/jbool` 都在 fnos_data.c；
现在 fnos_data.c 只剩薄壳（`parse_status()` 转发给 `fnos_status_parse()`），真正的
解析与字符串存储都在 fnos_snapshot.c。所以抠函数必须**两个文件一起看** —— 只看
fnos_data.c 会停在"找不到 jnum()"，那只是搬家的症状，不是解析层坏了。

用法：python3 tools/parsecheck/extract.py <fnos_data.c> <输出 .c>
（同目录的 fnos_snapshot.c 会自动一起搜。）
"""
import hashlib
import io
import os
import re
import sys

# 主源由命令行给（run.sh 传的就是 fnos_data.c），同目录这些文件自动跟上。
EXTRA_SOURCES = ["fnos_snapshot.c"]

# 按名字抠函数体：名字在哪个源里都能找到（找不到就报错，不静默跳过）。
FUNCS = [
    # fnos_data.c：薄壳 + 历史回填
    "parse_status", "parse_history", "fnos_data_hist_read",
    # fnos_snapshot.c：字符串 arena + 字段解析
    "snapshot_malloc", "owned_alloc",
    "fnos_status_create", "fnos_status_release", "fnos_status_copy", "fnos_status_text",
    "item", "number", "string", "array", "count",
    "temp_cmp",                          # 温度行的固定排序（fnos_status_parse 调它）
    "fnos_status_parse",
]

# 函数体要用到的**类型**也照抠（单行 typedef），免得手抄一份迟早跟源码脱节。
TYPES = ["block_t", "storage_t"]


def grab(src, name):
    """抠一个函数的完整定义。

    **三点别改回去**：
      1. `static` 是可选的 —— `fnos_data_hist_read()` 是公开函数（声明在 fnos_data.h），
         写死 `^static ` 会直接找不到它。
      2. 函数体按**花括号配对**切，不用非贪婪正则 —— `parse_status()` 里有嵌套的
         `if (...) { … }`，`\\{.*?\\n\\}` 会停在第一个内层块的收尾行上、抠出来少一半，
         而报出来的错看着像类型问题（certcheck 那边就踩过一次）。
      3. 定义必须**顶格**（`^` 后先出现标识符）：`item` / `number` / `string` / `count`
         这些名字在函数体里到处都是，允许前导空白就会把某一行缩进的**调用**当成定义
         （那行恰好以 `)` 结尾时尤其像），抠出来的是半截调用，编译错误还指向别处。
    """
    m = re.search(r"^(?:static\s+)?[A-Za-z_][^\n]*?\b%s\([^\n]*?\)\n" % re.escape(name), src, re.M)
    if not m:
        return None
    i = src.index("{", m.end() - 1)
    depth = 0
    j = i
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                break
    return src[m.start():j + 1]


def grab_type(src, name):
    """抠单行 typedef（多行的直接报错，别悄悄抠半句）。"""
    m = re.search(r"^typedef[^\n]*\b%s;[ \t]*$" % re.escape(name), src, re.M)
    return m.group(0) if m else None


def main():
    if len(sys.argv) != 3:
        print("用法：python3 tools/parsecheck/extract.py <fnos_data.c> <输出 .c>")
        return 1
    src_path, out_path = sys.argv[1], sys.argv[2]
    sources = [src_path] + [os.path.join(os.path.dirname(src_path), n) for n in EXTRA_SOURCES]
    texts = [(p, io.open(p, encoding="utf-8").read()) for p in sources if os.path.exists(p)]

    parts, types = [], []
    for fn in FUNCS:
        for path, src in texts:
            body = grab(src, fn)
            if body is not None:
                parts.append(body)
                break
        else:
            print("✗ 在 %s 里都找不到 %s() 的定义（改了签名或搬了文件就同步改 extract.py）"
                  % (" / ".join(os.path.basename(p) for p, _ in texts), fn))
            return 1
    for name in TYPES:
        for path, src in texts:
            line = grab_type(src, name)
            if line is not None:
                types.append(line)
                break
        else:
            print("✗ 找不到 typedef %s（改了定义就同步改 extract.py 的 TYPES）" % name)
            return 1

    blob = "\n\n".join(parts)
    digest = hashlib.sha256(blob.encode("utf-8")).hexdigest()

    header = f'''/* 本文件由 tools/parsecheck/extract.py 从 {os.path.relpath(src_path)} 等源文件自动生成，不要手改。
 * 里面是 fnos_data.c / fnos_snapshot.c 里解析状态帧那几个函数的原文（sha256 {digest}）。 */
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fnos_data.h"
#include "cJSON.h"

/* 抠出来的这几段会用到三个文件级静态：
     · s_nas_ts   —— parse_status 把 NAS 的挂钟时间记在这儿（adopt_nas_time() 用）
     · s_conn_err —— parse_status 把解析失败的原因记在这儿（只写不读）
     · s_h_*      —— 历史回填的环形缓冲，parse_history 往里写
   它们的值本身不参与解析，只是被赋值。缓冲尺寸照抄 fnos_data.h 的 FNOS_HIST_MAX。 */
int64_t s_nas_ts;
char s_conn_err[24];
float s_h_cpu[FNOS_HIST_MAX], s_h_mem[FNOS_HIST_MAX];
float s_h_rx[FNOS_HIST_MAX], s_h_tx[FNOS_HIST_MAX];
int64_t s_seq;
fnos_status_t s_status;

/* fnos_snapshot.c 里字符串 arena 的类型与容量上限：函数原文要用，类型照抠、上限照抄同样的兜底值。 */
{chr(10).join(types)}
#ifndef CONFIG_FNOS_SNAPSHOT_MAX_BYTES
#define CONFIG_FNOS_SNAPSHOT_MAX_BYTES (1024 * 1024)
#endif

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
    print(f"抠出 {len(FUNCS)} 个函数 + {len(TYPES)} 个类型：{len(blob)} 字节，sha256 {digest[:16]}…")
    return 0


if __name__ == "__main__":
    sys.exit(main())
