#!/usr/bin/env python3
"""诊断心跳回归：**抽取 main.cpp 里真实的 diag_timer_cb** 编译到主机上跑。

为什么需要它：72 小时长测的全部结论都来自这一行 `FNOS_DIAG {...}`。如果字段名写错、
单位写错（毫秒当秒、字节当 KB）或者某个字段在"还没有成功帧"时补了个 0，采集脚本会
一本正经地给出错误结论——而这种错误在板上完全看不出来（日志长得挺正常）。
所以这里不复制实现，而是把真实函数体抠出来编译，逐字段对着契约断言。

它证明什么、不证明什么：
  * 证明：字段集合/类型/单位、派生值（io.age、src=-1、ui.age 间隔）、异常复位标记、
    mem 的三个容量口径没有串台、栈余量为 0 时如实上报"测不到"、以及回调**不取快照
    引用**（没有获取/释放要平衡，也就没有那条泄漏面）。
  * 不证明：板子上 LVGL 任务是否真的每 10 秒跑到（那是实机 72h 的事）、也不测帧耗时
    与触摸延迟。ESP-IDF/FreeRTOS/LVGL 全部是替身，语义按官方文档对齐（字节、ms、秒）。

用法：
    python3 tools/audit125/test_diag.py            # 退出码 0 = 全过
    CXX=g++ python3 tools/audit125/test_diag.py    # 指定主机 C++ 编译器
"""
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
MAIN_CPP = os.path.join(REPO, "main", "main.cpp")
FNOS_DATA_H = os.path.join(REPO, "components", "fnos_monitor", "fnos_data.h")

SIG = "static void diag_timer_cb(lv_timer_t *t)"

FAILS = []
CHECKS = 0


def check(cond, what, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        return True
    FAILS.append(f"{what}{(' — ' + detail) if detail else ''}")
    print(f"  ✗ {what}" + (f" — {detail}" if detail else ""))
    return False


def extract_callback():
    """从 main.cpp 抠出 diag_timer_cb 的完整函数体（按大括号配对，不复制实现）。"""
    src = open(MAIN_CPP, encoding="utf-8").read()
    start = src.find(SIG)
    if start < 0:
        sys.exit(f"main.cpp 里找不到 {SIG}（诊断心跳被删了？）")
    brace = src.find("{", start)
    depth, i = 0, brace
    while i < len(src):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = src[start:i + 1]
    if "FNOS_DIAG" not in body:
        sys.exit("抠出来的函数里没有 FNOS_DIAG，抽取锚点错了")
    if "#if" in body:
        sys.exit("函数体里出现预处理指令，抽取不可靠")
    return body


HARNESS = r"""
// 主机替身：只实现 diag_timer_cb 用到的接口，语义按 ESP-IDF 官方文档对齐。
// fnos_data_diag_t 用**真实头文件**，字段布局不会因为替身而走样。
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct lv_timer_t lv_timer_t;

// 真实头文件先 include：fnos_data_diag_t 的字段布局必须来自生产代码，不能靠替身复制。
#include "sdkconfig.h"
#include "fnos_data.h"

// 复位原因枚举替身：取值与 ESP-IDF esp_system.h 的 ESP_RST_* 一致。
#define ESP_RST_UNKNOWN    0
#define ESP_RST_POWERON    1
#define ESP_RST_EXT        2
#define ESP_RST_SW         3
#define ESP_RST_PANIC      4
#define ESP_RST_INT_WDT    5
#define ESP_RST_TASK_WDT   6
#define ESP_RST_WDT        7
#define ESP_RST_DEEPSLEEP  8
#define ESP_RST_BROWNOUT   9
#define ESP_RST_SDIO      10
#define ESP_RST_USB       11
#define ESP_RST_JTAG      12
#define ESP_RST_EFUSE     13
#define ESP_RST_PWR_GLITCH 14
#define ESP_RST_CPU_LOCKUP 15

static int64_t g_now_us;
static int     g_reset;
static unsigned g_cap[3];       // [0]=INTERNAL [1]=DMA [2]=SPIRAM
static unsigned g_imin, g_imax, g_dmin, g_pmin;
static unsigned g_stk_lvgl, g_stk_poll, g_stk_pair;
static uint32_t g_ok, g_fail; static int64_t g_recv_ms, g_src_ts; static int g_p95;

#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_DMA      2u
#define MALLOC_CAP_SPIRAM   4u

static const char *TAG = "main";
#define ESP_LOGI(tag, fmt, ...) do { printf("I (0) %s: ", (tag)); printf(fmt, ##__VA_ARGS__); printf("\n"); } while (0)

static int64_t esp_timer_get_time(void) { return g_now_us; }
static int esp_reset_reason(void) { return g_reset; }
static int esp_app_get_elf_sha256(char *dst, size_t size)
{
    static const char *HEX = "0123456789abcdef";
    char sha[64];
    for (int i = 0; i < 64; i++) sha[i] = HEX[(i * 7) % 16];
    size_t n = (size < 65) ? size : 65;
    if (n < 2) return 0;
    memcpy(dst, sha, n - 1);
    dst[n - 1] = 0;
    return (int)n;
}
static size_t heap_caps_get_free_size(unsigned c) { return g_cap[c == 1u ? 0 : (c == 2u ? 1 : 2)]; }
static size_t heap_caps_get_minimum_free_size(unsigned c) { return (c == 1u) ? g_imin : ((c == 2u) ? g_dmin : g_pmin); }
static size_t heap_caps_get_largest_free_block(unsigned c) { return (void)c, g_imax; }
static unsigned uxTaskGetStackHighWaterMark(void *t) { return (void)t, g_stk_lvgl; }
// 这三个是 fnos_data.h 里声明的生产接口，替身定义不能加 static（否则与头文件声明冲突）。
void fnos_data_diag(fnos_data_diag_t *o)
{
    o->ok_count = g_ok; o->fail_count = g_fail; o->recv_ms = g_recv_ms; o->source_ts = g_src_ts;
    o->http_ms = 0; o->p95_ms = g_p95; o->uptime_s = 1234; o->online = g_recv_ms != 0;
}
uint32_t fnos_data_stack_min_free(void) { return g_stk_poll; }
static uint32_t fnos_pair_stack_min_free(void) { return g_stk_pair; }

__CBODY__

int main(int argc, char **argv)
{
    const char *cs = (argc > 1) ? argv[1] : "fresh";
    g_cap[0] = 100000; g_cap[1] = 90000; g_cap[2] = 3000000;
    g_imin = 80000; g_imax = 65536; g_dmin = 70000; g_pmin = 2500000;
    g_stk_lvgl = 3000; g_stk_poll = 2100; g_stk_pair = 1800;
    g_reset = 1;                     // ESP_RST_POWERON
    g_now_us = 5000000;              // 开机 5 秒

    if (!strcmp(cs, "fresh")) {
        diag_timer_cb(NULL);
    } else if (!strcmp(cs, "steady")) {
        g_now_us = 100 * 1000000;    // 100 秒：recv_ms 是毫秒，97 秒前收到过帧
        g_ok = 4200; g_fail = 1; g_recv_ms = 97000; g_src_ts = 1697000000; g_p95 = 180;
        diag_timer_cb(NULL);
        g_recv_ms += 12 * 1000;      // 健康设备一直在收帧：最新成功接收也前进 12 秒
        g_now_us += 12 * 1000000;    // 心跳晚到 12 秒：ui.age 要如实变成 12
        diag_timer_cb(NULL);
    } else if (!strcmp(cs, "stall")) {
        g_now_us = 100 * 1000000;
        g_recv_ms = 97000;
        diag_timer_cb(NULL);
        g_now_us += 20 * 1000000;
        diag_timer_cb(NULL);
    } else if (!strcmp(cs, "panic")) {
        g_reset = 4;                 // ESP_RST_PANIC
        diag_timer_cb(NULL);
    } else if (!strcmp(cs, "taskwdt")) {
        g_reset = 6;                 // ESP_RST_TASK_WDT
        diag_timer_cb(NULL);
    } else if (!strcmp(cs, "stacks_absent")) {
        g_stk_poll = 0; g_stk_pair = 0;   // 任务还没起来：如实报"测不到"
        diag_timer_cb(NULL);
    } else {
        fprintf(stderr, "unknown case %s\n", cs);
        return 2;
    }
    return 0;
}
"""

# 契约：字段名与层级完全固定，多一个少一个都算失败（采集脚本按它取值）。
TOP = {"n", "up", "hz", "fw", "rst", "ev", "io", "ui", "mem", "stk"}
SUB = {
    "io": {"ok", "fail", "age", "p95", "src"},
    "ui": {"age"},
    "mem": {"ifree", "imin", "imax", "dfree", "dmin", "pfree", "pmin"},
    "stk": {"lvgl", "poll", "pair"},
}
HEX64 = re.compile(r"^[0-9a-f]{64}$")


def compile_harness(tmp, body):
    with open(os.path.join(tmp, "sdkconfig.h"), "w", encoding="utf-8") as f:
        f.write("/* 主机替身：诊断回调只看 CONFIG_FNOS_* 之外的东西，留空即可 */\n")
    src = HARNESS.replace("__CBODY__", body)
    path = os.path.join(tmp, "diag_host.cpp")
    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    cxx = os.environ.get("CXX") or os.environ.get("CC") or "c++"
    exe = os.path.join(tmp, "diag_host")
    cmd = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-I", tmp, "-I", os.path.dirname(FNOS_DATA_H), path, "-o", exe]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"编译失败（{cxx}）：\n{p.stdout}\n{p.stderr}")
    return exe, cxx


def run_case(exe, case):
    p = subprocess.run([exe, case], capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"用例 {case} 退出码 {p.returncode}：{p.stderr}")
    lines = [ln for ln in p.stdout.splitlines() if "FNOS_DIAG" in ln]
    out = []
    for ln in lines:
        blob = ln[ln.index("FNOS_DIAG") + len("FNOS_DIAG"):].strip()
        out.append(json.loads(blob))       # 与采集脚本同样的"先定位标记再解析"
    return out


def check_schema(rec, case):
    check(set(rec) == TOP, f"[{case}] 顶层字段集合",
          f"多={sorted(set(rec) - TOP)} 少={sorted(TOP - set(rec))}")
    for k, want in SUB.items():
        got = rec.get(k)
        check(isinstance(got, dict) and set(got) == want, f"[{case}] {k} 子字段集合",
              f"实际={sorted(got) if isinstance(got, dict) else got}")
    check(isinstance(rec.get("n"), int) and rec["n"] >= 1, f"[{case}] n 是正整数")
    check(isinstance(rec.get("up"), int) and rec["up"] >= 0, f"[{case}] up 是毫秒整数")
    check(rec.get("hz") == 10, f"[{case}] hz==10", f"实际={rec.get('hz')}")
    check(bool(HEX64.match(str(rec.get("fw", "")))), f"[{case}] fw 是 64 位小写 hex",
          f"实际={rec.get('fw')}")
    check(isinstance(rec.get("rst"), int), f"[{case}] rst 是整数")
    check(isinstance(rec.get("ev"), list) and all(isinstance(x, str) for x in rec["ev"]),
          f"[{case}] ev 是字符串数组")
    for k in SUB["io"] | SUB["ui"] | SUB["mem"] | SUB["stk"]:
        rec_k = rec["io"] if k in SUB["io"] else (rec["ui"] if k in SUB["ui"]
                                                 else (rec["mem"] if k in SUB["mem"] else rec["stk"]))
        check(isinstance(rec_k[k], int), f"[{case}] {k} 是整数", f"实际={rec_k[k]!r}")


def main():
    body = extract_callback()
    print(f"抽取 main/main.cpp 的真实回调：{len(body.splitlines())} 行\n")

    # 不取快照引用 = 没有获取/释放要平衡（源码级检查；运行时用 POD 复制）。
    for bad in ("fnos_data_get(", "fnos_status_copy(", "fnos_status_release("):
        check(bad not in body, f"回调不调用 {bad}", "诊断路径不该碰快照所有权")
    for bad in (r"\bfree\s*\(", r"\bmalloc\s*\(", r"\bcalloc\s*\(", r"\brealloc\s*\("):
        # 词边界是必要的：fnos_data_stack_min_free() / heap_caps_get_free_size() 不是堆释放。
        check(re.search(bad, body) is None, f"回调不调用 {bad}", "诊断路径不该自己管内存")

    tmp = tempfile.mkdtemp(prefix="fnos-diag-")
    exe, cxx = compile_harness(tmp, body)
    print(f"主机 C++ 编译器：{cxx}\n")

    print("① fresh：还没有成功帧")
    fresh = run_case(exe, "fresh")
    check(len(fresh) == 1, "[fresh] 一次调用一行")
    r = fresh[0]
    check_schema(r, "fresh")
    check(r["n"] == 1, "[fresh] n 从 1 开始", f"实际={r['n']}")
    check(r["up"] == 5000, "[fresh] up 是毫秒（5e6 us → 5000）", f"实际={r['up']}")
    check(r["io"]["age"] == -1, "[fresh] 没有成功帧时 io.age=-1（不是 0）", f"实际={r['io']['age']}")
    check(r["io"]["src"] == -1, "[fresh] 没有成功帧时 io.src=-1（不是 0）", f"实际={r['io']['src']}")
    check(r["io"]["ok"] == 0 and r["io"]["fail"] == 0, "[fresh] 计数从 0 开始")
    check(r["ui"]["age"] == 0, "[fresh] 第一条 ui.age=0（还没有上一次心跳）")
    check(r["rst"] == 1 and r["ev"] == [], "[fresh] 计划内复位不报异常",
          f"rst={r['rst']} ev={r['ev']}")
    check(r["mem"] == {"ifree": 100000, "imin": 80000, "imax": 65536, "dfree": 90000,
                       "dmin": 70000, "pfree": 3000000, "pmin": 2500000},
          "[fresh] mem 七个口径各就各位、单位是字节（没有除 1024）", f"实际={r['mem']}")
    check(r["stk"] == {"lvgl": 3000, "poll": 2100, "pair": 1800},
          "[fresh] 三个任务栈余量分别落到 lvgl/poll/pair", f"实际={r['stk']}")

    print("\n② steady：有成功帧 + 心跳晚到 12 秒")
    steady = run_case(exe, "steady")
    check(len(steady) == 2, "[steady] 两次调用两行")
    a, b = steady
    check_schema(a, "steady1")
    check_schema(b, "steady2")
    for rec in steady:
        check(rec["io"]["age"] == 3, "[steady] io.age=3 秒（97s 收帧、100s 打点）",
              f"实际={rec['io']['age']}")
        check(rec["io"]["src"] == 1697000000, "[steady] io.src 原样透传 epoch 秒")
        check(rec["io"]["p95"] == 180, "[steady] p95 原样透传毫秒")
        check(rec["io"]["ok"] == 4200 and rec["io"]["fail"] == 1, "[steady] 成功/失败计数")
    check(a["ui"]["age"] == 0 and b["ui"]["age"] == 12,
          "[steady] ui.age 是两次心跳的间隔秒数（第一条 0，第二条 12）",
          f"实际={a['ui']['age']}/{b['ui']['age']}")
    check(b["n"] == 2 and b["up"] == 112000, "[steady] n/up 随心跳推进",
          f"n={b['n']} up={b['up']}")

    print("\n③ stall：心跳停了 20 秒")
    stall = run_case(exe, "stall")
    check(len(stall) == 2 and stall[1]["ui"]["age"] == 20,
          "[stall] 20 秒的停顿如实报成 ui.age=20（>15s 由采集端判负）",
          f"实际={stall[1]['ui']['age'] if len(stall) > 1 else None}")

    print("\n④ panic / task_wdt：异常复位必须在 ev 里点名")
    panic = run_case(exe, "panic")[0]
    check(panic["rst"] == 4 and panic["ev"] == ["panic"],
          "[panic] rst=4 且 ev=[panic]", f"rst={panic['rst']} ev={panic['ev']}")
    twdt = run_case(exe, "taskwdt")[0]
    check(twdt["rst"] == 6 and twdt["ev"] == ["task_wdt"],
          "[taskwdt] rst=6 且 ev=[task_wdt]", f"rst={twdt['rst']} ev={twdt['ev']}")

    print("\n⑤ stacks_absent：任务没起来时如实报 0（采集端按 INCOMPLETE，不当成栈耗尽）")
    sa = run_case(exe, "stacks_absent")[0]
    check_schema(sa, "stacks_absent")
    check(sa["stk"]["poll"] == 0 and sa["stk"]["pair"] == 0 and sa["stk"]["lvgl"] == 3000,
          "[stacks_absent] 缺任务的栈余量是 0 而不是缺失字段", f"实际={sa['stk']}")

    print()
    if FAILS:
        print(f"FAIL：{len(FAILS)}/{CHECKS} 项不通过")
        for f in FAILS:
            print(f"  - {f}")
        return 1
    print(f"PASS {CHECKS} 项：真实回调的字段/单位/派生值/异常标记全部符合契约"
          f"（替身只替换 ESP-IDF 接口，板端行为仍需实机 72h）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
