#!/usr/bin/env python3
"""LVGL 官方 profiler 的 FTrace 文本 → "一帧的时间花在哪些函数上"。

用法:
  python3 tools/perf_trace.py /tmp/prof_raw.txt            # self 时间排行（默认 20 行）
  python3 tools/perf_trace.py /tmp/prof_raw.txt --top 40
  python3 tools/perf_trace.py /tmp/prof_raw.txt --func lv_draw_sw_blend   # 单函数统计

数据怎么来: 台架固件（CONFIG_LV_USE_PROFILER=y）串口 `prof once 120`
（见 components/fnos_monitor/fnos_perf.c），LVGL 用官方 LV_PROFILER_BEGIN/END 写成
Perfetto/FTrace 文本：`LVGL-<tid> [<cpu>] <sec>.<nsec>: tracing_mark_write: B|1|<func>`。
本脚本按 tid 维护调用栈还原区间，self = 区间长 − 直接子区间之和，再排行。
固件里每帧还会打一对 `frame`（LV_EVENT_RENDER_START/READY），所以有帧数时多一列
ms/frame —— 那才是"每帧要花多少"的口径。
"""
import argparse
import collections
import re
import sys

LINE = re.compile(r"LVGL-(\d+) \[(\d+)\] (\d+)\.(\d+): tracing_mark_write: ([BE])\|1\|(\S+)")


def parse(path):
    spans = []                      # (tid, func, t0_us, t1_us, self_us)
    stack = collections.defaultdict(list)   # tid -> [[func, t0, child_sum], ...]
    events = 0
    t_first = t_last = None
    for line in open(path, "r", errors="replace"):
        m = LINE.search(line)
        if not m:
            continue
        tid, _cpu, sec, nsec, tag, func = m.groups()
        t = int(sec) * 1_000_000 + int(nsec) // 1000
        tid = int(tid)
        events += 1
        t_first = t if t_first is None else t_first
        t_last = t
        if tag == "B":
            stack[tid].append([func, t, 0])
            continue
        st = stack[tid]
        idx = next((i for i in range(len(st) - 1, -1, -1) if st[i][0] == func), None)
        if idx is None:
            continue                # 没配上的 E（缓冲被截断/跨 flush）：丢掉，不猜
        while len(st) > idx + 1:    # 未闭合的兄弟：结算到当前时刻再退栈
            f, t0, cs = st.pop()
            spans.append((tid, f, t0, t, t - t0 - cs))
            st[-1][2] += t - t0
        f, t0, cs = st.pop()
        spans.append((tid, f, t0, t, t - t0 - cs))
        if st:
            st[-1][2] += t - t0
    return spans, events, t_first, t_last


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--func")
    a = ap.parse_args()

    spans, events, t0, t1 = parse(a.path)
    if not events:
        sys.exit("没找到 FTrace 行 —— 确认固件开了 CONFIG_LV_USE_PROFILER 且采到了 `prof once` 的区间")
    frames = [s for s in spans if s[1] == "frame"]
    nframes = max(len(frames), 1)

    by = collections.defaultdict(lambda: [0, 0, 0])       # func -> [n, total, self]
    for _tid, f, s0, s1, sf in spans:
        r = by[f]
        r[0] += 1
        r[1] += s1 - s0
        r[2] += sf

    print("事件 %d  span %d  帧 %d  跨度 %.1f ms  单帧均 %.2f ms" % (
        events, len(spans), len(frames),
        (t1 - t0) / 1000.0, (t1 - t0) / 1000.0 / nframes))
    if a.func:
        r = by.get(a.func, [0, 0, 0])
        print("%s: n=%d total=%.2f ms self=%.2f ms self/帧=%.2f ms" % (
            a.func, r[0], r[1] / 1000.0, r[2] / 1000.0, r[2] / 1000.0 / nframes))
        return

    total_self = sum(r[2] for r in by.values()) or 1
    print("%9s %9s %9s %7s %6s  %s" % ("self/帧ms", "self ms", "total ms", "n", "self%", "func"))
    for f, (n, tt, ss) in sorted(by.items(), key=lambda kv: -kv[1][2])[:a.top]:
        print("%9.2f %9.2f %9.2f %7d %5.1f%%  %s" % (
            ss / 1000.0 / nframes, ss / 1000.0, tt / 1000.0, n, 100 * ss / total_self, f))


if __name__ == "__main__":
    main()
