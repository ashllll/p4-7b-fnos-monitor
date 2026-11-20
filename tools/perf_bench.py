#!/usr/bin/env python3
"""实机触控/渲染台架采集：串口发 `bench …` → 解析 `[bench]` 行 → CSV。

用法:
  python3 tools/perf_bench.py --label base --repeat 20 --out /tmp/perf-base.csv
  python3 tools/perf_bench.py --label perf --cases tap:3,swipe:3,drag:2:600:2000,idle:3 --boot 25

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
import math
import re
import statistics as st
import sys
import time


FIELDS = ["kind", "page", "cycles", "misses",
          "p2f_n", "p2f_p50_us", "p2f_p95_us", "p2f_max_us",
          "r2f_n", "r2f_p50_us", "r2f_p95_us", "r2f_max_us",
          "iv_n", "iv_sampled", "iv_capped", "iv_p50_us", "iv_p95_us", "iv_max_us",
          "p2rs_n", "p2rs_p50_us",
          "rs2rr_n", "rs2rr_p50_us", "rs2rr_p95_us",
          "rr2rs_n", "rr2rs_p50_us", "rr2rs_p95_us",
          "frames", "wall_ms", "fps_avg", "fps_p50",
          "tick_n", "tick_p50_us", "tick_p95_us", "tick_max_us",
          "cpu_lvgl", "cpu_idle", "iv_p99_us", "iv_long_n", "drag_hold_ms"]
INT_FIELDS = {f for f in FIELDS if not f.startswith("fps") and not f.startswith("cpu") and f != "kind"}
# 旧 [bench] 行没有这些字段；缺了按未测量处理，不能把旧日志判坏。
BENCH_OPTIONAL_FIELDS = {"iv_sampled", "iv_capped", "iv_p99_us", "iv_long_n", "drag_hold_ms"}
KV = re.compile(r"([a-z0-9_]+)=([^\s]+)")
# 串口与其它任务的 ESP_LOG 共用：日志记录会直接接在当前行尾（实测
# [bench_parts] 最后一个值被写成 "11470W (57753) esp-tls..."）。采集窗口里
# 出现这种尾巴时在日志记录开头截断，别把 "11470W" 当数值。
LOG_TAIL = re.compile(r"[WEI] \(\d+\)")
# 记录前也可能接着未换行的 ESP_LOG 文本（串口复用）；从记录标记开始解析。
RECORD_MARK = re.compile(r"\[bench(?:_[a-z]+)?\]")


def clean_line(line):
    line = line.strip()
    mark = RECORD_MARK.search(line)
    if mark and mark.start() > 0:
        line = line[mark.start():]
    tail = LOG_TAIL.search(line)
    if tail:
        line = line[:tail.start()].rstrip()
    return line

# Auxiliary rows are printed before [bench] by the same firmware build. They are
# optional so a bench binary without them (the 30 FPS control build) still parses;
# when a family is present, every field of that family is required.
AUX_LINES = {"[bench_parts]": "part_", "[bench_draw]": "draw_",
             "[bench_ui]": "ui_", "[bench_tick]": "tick_"}
AUX_REQUIRED = {
    "part_": ("frames", "frame_us", "flush_us", "flush_wait_us",
              "ppa_us", "ppa_n", "ppa_max_us", "dma2d_us", "dma2d_n", "dma2d_max_us",
              "submit_us", "submit_n", "submit_max_us", "freebuf_us", "freebuf_n", "freebuf_max_us"),
    "draw_": ("frames", "units", "worker_n", "cpu_worker",
              "create_us", "create_n", "create_max_us", "pick_us", "pick_n", "pick_max_us",
              "blend_us", "blend_n", "blend_max_us", "text_us", "text_n", "text_max_us"),
    "ui_": ("frames", "gesture_frames", "gesture_fps", "chart_frames", "draw_stages",
            "page_paint_us", "page_paint_n", "page_paint_max_us",
            "page_layout_us", "page_layout_n", "page_layout_max_us",
            "chart_draw_us", "chart_draw_n", "chart_draw_max_us"),
    "tick_": ("motion_n", "motion_p50_us", "motion_p95_us", "motion_max_us",
              "full_n", "full_p50_us", "full_p95_us", "full_max_us"),
}
AUX_FIELDS = [prefix + name for prefix, names in AUX_REQUIRED.items() for name in names]
AUX_FLOAT_FIELDS = {"draw_cpu_worker"}
AUX_INT_FIELDS = set(AUX_FIELDS) - AUX_FLOAT_FIELDS
# Totals stay raw; per-frame values divide by the same family's frame count, so
# settle/tail frames cannot dilute a window that never covered them.
DERIVED_FIELDS = [
    "part_flush_us_pf", "part_flush_wait_us_pf", "part_ppa_us_pf", "part_dma2d_us_pf",
    "part_submit_us_pf", "part_freebuf_us_pf", "draw_blend_us_pf", "draw_text_us_pf",
    "ui_page_paint_us_pf", "ui_chart_draw_us_pf", "ui_chart_draw_us_pf_chart",
    "ui_page_layout_us_pc", "ui_chart_less_frames",
]


def _aux_number(raw, col):
    value = float(raw)
    if not math.isfinite(value) or value < 0:
        raise ValueError("无效 bench aux 数值：" + col)
    if col in AUX_FLOAT_FIELDS:
        return value
    if not value.is_integer():
        raise ValueError("无效 bench aux 整数：" + col)
    return int(value)


def parse_aux(line):
    prefix = next((p for head, p in AUX_LINES.items() if line.startswith(head)), None)
    if prefix is None:
        return None
    row = {}
    for key, raw in KV.findall(line):
        if key == "kind":
            if raw not in {"tap", "swipe", "drag", "idle"}:
                raise ValueError("无效 bench aux kind：" + raw)
            if "_kind" in row:
                raise ValueError("bench aux 字段重复：kind")
            row["_kind"] = raw
            continue
        if key == "page":
            if "_page" in row:
                raise ValueError("bench aux 字段重复：page")
            row["_page"] = _aux_number(raw, prefix + "page")
            continue
        col = prefix + key
        if col not in AUX_FIELDS:
            continue
        if col in row:
            raise ValueError("bench aux 字段重复：" + col)
        row[col] = _aux_number(raw, col)
    if "_kind" not in row or "_page" not in row:
        raise ValueError("bench aux 行缺少 kind/page")
    missing = [prefix + name for name in AUX_REQUIRED[prefix] if prefix + name not in row]
    if missing:
        raise ValueError("bench aux 行不完整，缺少：" + ", ".join(missing))
    return row


def _ratio(row, total, denom):
    value = row.get(total)
    divisor = row.get(denom)
    if value is None or not divisor:
        return None
    return value / divisor


def add_derived(row):
    for src, denom, dst in (
            ("part_flush_us", "part_frames", "part_flush_us_pf"),
            ("part_flush_wait_us", "part_frames", "part_flush_wait_us_pf"),
            ("part_ppa_us", "part_frames", "part_ppa_us_pf"),
            ("part_dma2d_us", "part_frames", "part_dma2d_us_pf"),
            ("part_submit_us", "part_frames", "part_submit_us_pf"),
            ("part_freebuf_us", "part_frames", "part_freebuf_us_pf"),
            ("draw_blend_us", "draw_frames", "draw_blend_us_pf"),
            ("draw_text_us", "draw_frames", "draw_text_us_pf"),
            ("ui_page_paint_us", "ui_frames", "ui_page_paint_us_pf"),
            ("ui_page_layout_us", "ui_page_layout_n", "ui_page_layout_us_pc"),
            ("ui_chart_draw_us", "ui_gesture_frames", "ui_chart_draw_us_pf"),
            ("ui_chart_draw_us", "ui_chart_frames", "ui_chart_draw_us_pf_chart")):
        row[dst] = _ratio(row, src, denom)   # None = 不可算，写 CSV 时留空
    gesture = row.get("ui_gesture_frames")
    chart = row.get("ui_chart_frames")
    row["ui_chart_less_frames"] = (gesture - chart
                                   if gesture is not None and chart is not None and gesture >= chart
                                   else None)


def parse_bench(line):
    if not line.startswith("[bench]"):
        return None
    row = {}
    for k, v in KV.findall(line):
        if k in FIELDS:
            if k in row:
                raise ValueError("bench 字段重复：" + k)
            if k == "kind":
                row[k] = v
            elif k in INT_FIELDS:
                value = float(v)
                if not math.isfinite(value) or value < 0 or not value.is_integer():
                    raise ValueError("无效 bench 整数：%s" % k)
                row[k] = int(value)
            else:
                row[k] = float(v)
                if not math.isfinite(row[k]) or row[k] < 0:
                    raise ValueError("无效 bench 数值：%s" % k)
    missing = (set(FIELDS) - BENCH_OPTIONAL_FIELDS) - row.keys()
    if missing:
        raise ValueError("bench 记录不完整，缺少：" + ", ".join(sorted(missing)))
    if row["kind"] not in {"tap", "swipe", "drag", "idle"}:
        raise ValueError("无效 bench kind")
    return row


def median_positive(rows, field, only_pos=True):
    """旧固件缺 aux 时派生列是 None：跳过而不是拿 None 参与比较/中位数。"""
    v = [r[field] for r in rows if r.get(field) is not None and (not only_pos or r[field] > 0)]
    return st.median(v) if v else 0


def require_draw(row):
    """Attribution rows must come from the DRAW=y firmware, never a DRAW=n build."""
    got = row.get("ui_draw_stages")
    if got != 1:
        raise RuntimeError("本行 draw_stages=%s，不能用于软件绘制归因" % got)


def require_cap(row, cap):
    """A --cap run only counts if the firmware reports that cap on this row."""
    got = row.get("ui_gesture_fps")
    if got != cap:
        raise RuntimeError("cap 未生效：期望 %s，样本报告 %s" % (cap, got))


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
    if kind == "drag":
        return "bench drag %s %s %s" % (p[1] if len(p) > 1 else 2,
                                        p[2] if len(p) > 2 else 600,
                                        p[3] if len(p) > 3 else 2000)
    if kind == "idle":
        return "bench idle %s %s" % (p[1] if len(p) > 1 else 3, p[2] if len(p) > 2 else 5)
    raise SystemExit("认不出的用例 '%s'（tap:页[:轮] / swipe:页[:px:轮] / drag:页[:px:ms] / idle:页[:秒]）" % case)


def case_page(case):
    """请求页（与 case_to_cmd 的默认值一致）：drag 默认 2，其余 3。"""
    p = case.split(":")
    if len(p) > 1 and p[1]:
        return int(p[1])
    return 2 if p[0] == "drag" else 3


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
    requested_page = case_page(case)
    drain(ser, quiet=0.2, cap=1.0)
    ser.write((cmd + "\n").encode())
    ser.flush()
    t0 = time.time()
    buf = ""
    aux = {}
    aux_ident = None
    first = cmd.split()[0]
    while time.time() - t0 < timeout:
        chunk = ser.read(4096).decode("utf-8", "replace")
        if chunk:
            buf += chunk
            # 只消费换行结束的记录；跨 read 的半行留待下次拼接。
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = clean_line(line)
                extra = parse_aux(line)
                if extra is not None:
                    ident = (extra.pop("_kind"), extra.pop("_page"))
                    if aux_ident is None:
                        aux_ident = ident
                    elif aux_ident != ident:
                        raise ValueError("bench aux 行 kind/page 不一致")
                    for key, value in extra.items():
                        if key in aux:
                            raise ValueError("bench aux 字段重复：" + key)
                        aux[key] = value
                    continue
                if line.startswith("[bench]"):
                    row = parse_bench(line)
                    kind = case.split(":", 1)[0]
                    if row["kind"] != kind:
                        raise ValueError("bench kind 与请求用例不符")
                    if row["page"] != requested_page:
                        raise ValueError("bench page 与请求用例不符：请求 %d，样本 %d"
                                         % (requested_page, row["page"]))
                    if row["kind"] != "idle":
                        requested = 1 if kind == "drag" else int(cmd.split()[-1])
                        requested = requested if requested > 0 else 3
                        if (row["cycles"] != requested or row["misses"]
                                or row["p2f_n"] != requested or row["r2f_n"] != requested):
                            raise ValueError("bench 手势计时不完整：请求 %d 轮，cycles=%d misses=%d p2f_n=%d r2f_n=%d"
                                             % (requested, row["cycles"], row["misses"],
                                                row["p2f_n"], row["r2f_n"]))
                    if aux_ident is not None:
                        if aux_ident != (row["kind"], row["page"]):
                            raise ValueError("bench aux 与主行 kind/page 不一致")
                        if aux_ident[1] != requested_page:
                            raise ValueError("bench aux page 与请求用例不符：请求 %d，样本 %d"
                                             % (requested_page, aux_ident[1]))
                        for family in ("part_", "draw_", "ui_"):
                            frames = aux.get(family + "frames")
                            if frames is not None and frames > row["frames"]:
                                raise ValueError("bench aux frames 大于主行 frames")
                        chart_frames = aux.get("ui_chart_frames")
                        if chart_frames is not None and row["frames"] and chart_frames > aux["ui_frames"]:
                            raise ValueError("bench chart_frames 大于 ui_frames")
                        gesture_frames = aux.get("ui_gesture_frames")
                        if chart_frames is not None and gesture_frames is not None and chart_frames > gesture_frames:
                            raise ValueError("bench chart_frames 大于 gesture_frames")
                        if gesture_frames is not None and row["frames"] and gesture_frames > row["frames"]:
                            raise ValueError("bench gesture_frames 大于 frames")
                        draw_stages = aux.get("ui_draw_stages")
                        has_draw = any(k.startswith("draw_") for k in aux)
                        if draw_stages == 0 and has_draw:
                            raise ValueError("bench draw_stages=0 但出现 draw 行")
                        if draw_stages == 1 and not has_draw:
                            raise ValueError("bench draw_stages=1 但缺少 draw 行")
                    row.update(aux)
                    add_derived(row)
                    return row, line
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
                    help="逗号分隔；tap:页[:轮数] / swipe:页[:px:轮数] / drag:页[:px:ms] / idle:页[:秒]")
    ap.add_argument("--repeat", type=int, default=20)
    ap.add_argument("--warmup", type=int, default=1, help="每个用例先丢掉几次（开机抖动）")
    ap.add_argument("--boot", type=float, default=25.0, help="复位后等首次采集的秒数")
    ap.add_argument("--cap", type=int, default=None,
                    help="开端口后先发 bench cap N（0=不设限），再采 CSV；与样本同一会话")
    ap.add_argument("--require-draw", action="store_true",
                    help="拒绝 draw_stages=0 的行；归因采样用它防止误用 DRAW=n 固件")
    ap.add_argument("--timeout", type=float, default=180.0, help="单个用例等待上限")
    ap.add_argument("--out", default="/tmp/perf.csv")
    args = ap.parse_args()
    if not args.port:
        ap.error("set --port or FNOS_SERIAL_PORT to the board serial port")

    if (args.repeat <= 0 or args.warmup < 0 or args.boot < 0 or args.timeout <= 0
            or not math.isfinite(args.boot) or not math.isfinite(args.timeout)
            or (args.cap is not None and not (0 <= args.cap <= 1000))):
        ap.error("repeat/timeout 必须为正，warmup/boot 不得为负，cap 须在 0..1000")
    import serial
    ser = serial.Serial(args.port, 115200, timeout=0.3)
    ser.dtr = False
    ser.rts = False
    rows = []
    try:
        print("等待开机 + 首次采集（%.0fs）…" % args.boot, flush=True)
        time.sleep(args.boot)
        flush_console(ser)
        if args.cap is not None:
            ser.write(("bench cap %d\n" % args.cap).encode())
            ser.flush()
            ack = drain(ser, quiet=0.4, cap=2.0)
            if ("gesture_fps=%d" % args.cap) not in ack:
                raise SystemExit("bench cap %d 未确认；串口尾部：%r" % (args.cap, ack[-200:]))
        for case in args.cases.split(","):
            case = case.strip()
            if not case:
                continue
            for i in range(args.repeat + args.warmup):
                row, _ = run_case(ser, case, args.timeout)
                if args.cap is not None:
                    require_cap(row, args.cap)
                if args.require_draw:
                    require_draw(row)
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

    cols = ["label", "case", "repeat"] + FIELDS + AUX_FIELDS + DERIVED_FIELDS
    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore", restval="")
        w.writeheader()
        for r in rows:
            w.writerow(r)

    print("\n== %s ==" % args.label)
    for case in dict.fromkeys(r["case"] for r in rows):
        sub = [r for r in rows if r["case"] == case]
        def med(f, only_pos=True):
            return median_positive(sub, f, only_pos)
        print("%-12s n=%2d p2f(p50/p95)=%6.0f/%6.0f  r2f=%6.0f/%6.0f  iv(p50/p95)=%6.0f/%6.0f  fps=%5.1f  tick(p50/p95)=%5.0f/%5.0f  cpu_lvgl=%4.1f%%  misses=%d"
              % (case, len(sub), med("p2f_p50_us"), med("p2f_p95_us"), med("r2f_p50_us"), med("r2f_p95_us"),
                 med("iv_p50_us"), med("iv_p95_us"), med("fps_avg"), med("tick_p50_us"), med("tick_p95_us"),
                 med("cpu_lvgl"), int(med("misses", only_pos=False))))
        if any(r.get(f) is not None for r in sub for f in DERIVED_FIELDS):
            def nice(f):
                v = med(f)
                return "%5.0f" % v if v else "  n/a"
            print("%-12s 归因(µs) page_paint/帧=%s page_layout/次=%s chart/帧=%s chart/图帧=%s text/帧=%s blend/帧=%s flush/帧=%s"
                  % (case, nice("ui_page_paint_us_pf"), nice("ui_page_layout_us_pc"),
                     nice("ui_chart_draw_us_pf"), nice("ui_chart_draw_us_pf_chart"),
                     nice("draw_text_us_pf"), nice("draw_blend_us_pf"), nice("part_flush_us_pf")))
    print("CSV → %s（%d 行）" % (args.out, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
