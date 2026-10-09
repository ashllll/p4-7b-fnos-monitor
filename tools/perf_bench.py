#!/usr/bin/env python3
"""实机触控/渲染台架采集：串口发 `bench …` → 解析 `[bench]` 行 → CSV。

用法:
  python3 tools/perf_bench.py --label base --repeat 20 --out /tmp/perf-base.csv
  python3 tools/perf_bench.py --label perf --cases tap:3,swipe:3,idle:3 --boot 25

为什么需要它：屏幕"跟不跟手、点一下多久有反应"以前只能靠肉眼描述，而宿主的
32ms/180ms 预算是虚拟时间（docs 声明过不能当设备帧率证据）。固件里的台架
（CONFIG_FNOS_UI_PERF_BENCH，串口 `bench`）负责合成手势并打印计数器，本脚本负责
重复 N 次、把每次的 p50/p95 收成表 —— 单次采样在 60fps 的屏上噪声太大。

端口规矩与 tools/page_shot.py 完全一样（同一个坑）：pyserial 打开端口会拉 DTR/RTS，
**关端口就让板子复位**，所以全程握着端口；复位后的半行杂散先发空行冲掉。
"""
import argparse
import csv
import os
import re
import statistics as st
import sys
import time

import serial

FIELDS = ["kind", "page", "cycles", "misses",
          "p2f_n", "p2f_p50_us", "p2f_p95_us", "p2f_max_us",
          "r2f_n", "r2f_p50_us", "r2f_p95_us", "r2f_max_us",
          "iv_n", "iv_p50_us", "iv_p95_us", "iv_max_us",
          "p2rs_n", "p2rs_p50_us",
          "rs2rr_n", "rs2rr_p50_us", "rs2rr_p95_us",
          "rr2rs_n", "rr2rs_p50_us", "rr2rs_p95_us",
          "frames", "wall_ms", "fps_avg", "fps_p50",
          "tick_n", "tick_p50_us", "tick_p95_us", "tick_max_us",
          "cpu_lvgl", "cpu_idle"]
INT_FIELDS = {f for f in FIELDS if not f.startswith("fps") and not f.startswith("cpu") and f != "kind"}
KV = re.compile(r"([a-z0-9_]+)=([^\s]+)")


def parse_bench(line):
    if not line.startswith("[bench]"):
        return None
    row = {}
    for k, v in KV.findall(line):
        if k in FIELDS:
            if k == "kind":
                row[k] = v
            elif k in INT_FIELDS:
                row[k] = int(float(v))
            else:
                row[k] = float(v)
    return row if "kind" in row else None


def case_to_cmd(case):
    """`tap:3:2` → `bench tap 3 2`（固件收的是空格分隔的参数，别把冒号发过去）。"""
    p = case.split(":")
    kind = p[0]
    if kind == "tap":
        return "bench tap %s %s" % (p[1] if len(p) > 1 else 3, p[2] if len(p) > 2 else 3)
    if kind == "swipe":
        return "bench swipe %s %s %s" % (p[1] if len(p) > 1 else 3,
                                         p[2] if len(p) > 2 else 0,
                                         p[3] if len(p) > 3 else 3)
    if kind == "idle":
        return "bench idle %s %s" % (p[1] if len(p) > 1 else 3, p[2] if len(p) > 2 else 5)
    raise SystemExit("认不出的用例 '%s'（tap:页[:轮] / swipe:页[:px:轮] / idle:页[:秒]）" % case)


def drain(ser, quiet=0.25, cap=4.0):
    """读到串口安静为止（boot 日志尾巴 + 冷启动杂散字节）。"""
    t0 = time.time()
    last = time.time()
    buf = ""
    while time.time() - t0 < cap and time.time() - last < quiet:
        chunk = ser.read(4096)
        if chunk:
            buf += chunk.decode("utf-8", "replace")
            last = time.time()
    return buf


def flush_console(ser):
    """开端口会让板子复位，且**第一次写会带一个杂散字节**（实测 "不认识的命令：\\ufffd"）：
    先发两行空行把脏字节冲成两条空命令，再把回显读光。"""
    for _ in range(2):
        ser.write(b"\n")
        ser.flush()
        time.sleep(0.3)
        drain(ser, quiet=0.15, cap=0.8)


def run_case(ser, case, timeout):
    cmd = case_to_cmd(case)
    drain(ser, quiet=0.2, cap=1.0)
    ser.write((cmd + "\n").encode())
    ser.flush()
    t0 = time.time()
    buf = ""
    first = cmd.split()[0]
    while time.time() - t0 < timeout:
        chunk = ser.read(4096).decode("utf-8", "replace")
        if chunk:
            buf += chunk
            for line in buf.splitlines():
                line = line.strip()
                if line.startswith("[bench]"):
                    return parse_bench(line), buf
                # 只把"针对本命令"的报错当失败；冷启动那条杂散字节的报错不算
                bad = ("认不出" in line and first in line) or ("bench" in line and "认不出" in line) \
                      or (first in line and "不认识的命令" in line and len(line) > 12)
                if bad:
                    raise RuntimeError("设备拒绝了 %r：%s" % (cmd, line))
        else:
            time.sleep(0.02)
    raise TimeoutError("等 [bench] 行超时（%.0fs）cmd=%r；串口尾部：%s" % (timeout, cmd, buf[-400:]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=os.environ.get("FNOS_SERIAL_PORT"))
    ap.add_argument("--label", default="run", help="写进 CSV 的固件标签（base / perf / c1 …）")
    ap.add_argument("--cases", default="tap:3,swipe:3,idle:3",
                    help="逗号分隔；tap:页[:轮数] / swipe:页[:px:轮数] / idle:页[:秒]")
    ap.add_argument("--repeat", type=int, default=20)
    ap.add_argument("--warmup", type=int, default=1, help="每个用例先丢掉几次（开机抖动）")
    ap.add_argument("--boot", type=float, default=25.0, help="复位后等首次采集的秒数")
    ap.add_argument("--timeout", type=float, default=180.0, help="单个用例等待上限")
    ap.add_argument("--out", default="/tmp/perf.csv")
    args = ap.parse_args()
    if not args.port:
        ap.error("set --port or FNOS_SERIAL_PORT to the board serial port")

    ser = serial.Serial(args.port, 115200, timeout=0.3)
    ser.dtr = False
    ser.rts = False
    rows = []
    try:
        print("等待开机 + 首次采集（%.0fs）…" % args.boot, flush=True)
        time.sleep(args.boot)
        flush_console(ser)
        for case in args.cases.split(","):
            case = case.strip()
            if not case:
                continue
            for i in range(args.repeat + args.warmup):
                row, _ = run_case(ser, case, args.timeout)
                if i < args.warmup:
                    print("  [warmup] %-14s kind=%s p2f_p50=%s" % (case, row["kind"], row.get("p2f_p50_us")), flush=True)
                    continue
                row = dict(row)
                row["label"] = args.label
                row["case"] = case
                row["repeat"] = i - args.warmup
                rows.append(row)
                print("  %-14s #%02d p2f_p50=%5s p2f_p95=%5s r2f_p50=%5s iv_p95=%5s tick_p50=%5s cpu_lvgl=%4s"
                      % (case, i - args.warmup, row.get("p2f_p50_us"), row.get("p2f_p95_us"),
                         row.get("r2f_p50_us"), row.get("iv_p95_us"), row.get("tick_p50_us"), row.get("cpu_lvgl")),
                      flush=True)
    finally:
        ser.close()   # 关端口 = 板子复位：本脚本的已知副作用（与 page_shot.py 同）

    if not rows:
        print("没有任何样本", file=sys.stderr)
        return 2

    cols = ["label", "case", "repeat"] + FIELDS
    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)

    print("\n== %s ==" % args.label)
    for case in dict.fromkeys(r["case"] for r in rows):
        sub = [r for r in rows if r["case"] == case]
        def med(f, only_pos=True):
            v = [r[f] for r in sub if f in r and (not only_pos or r[f] > 0)]
            return st.median(v) if v else 0
        print("%-12s n=%2d p2f(p50/p95)=%6.0f/%6.0f  r2f=%6.0f/%6.0f  iv(p50/p95)=%6.0f/%6.0f  fps=%5.1f  tick(p50/p95)=%5.0f/%5.0f  cpu_lvgl=%4.1f%%  misses=%d"
              % (case, len(sub), med("p2f_p50_us"), med("p2f_p95_us"), med("r2f_p50_us"), med("r2f_p95_us"),
                 med("iv_p50_us"), med("iv_p95_us"), med("fps_avg"), med("tick_p50_us"), med("tick_p95_us"),
                 med("cpu_lvgl"), int(med("misses", only_pos=False))))
    print("CSV → %s（%d 行）" % (args.out, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
