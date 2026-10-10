#!/usr/bin/env python3
"""把 perf_bench CSV 汇总成"每个用例一行"的表（对同用例的 N 次重复取中位数）。
用法: perf_tab.py a.csv [b.csv ...]   —— 第一个是基线，之后的给 Δ%。
口径: 每个指标先取该次重复的值，再对 N 次重复取中位数（抗单次抖动）；
      iv_n 是原始样本数，太小的用例（tap）标注为噪声不可判。"""
import csv, sys, statistics as st

METRICS = [
    ("p2f_p50_us", "按下→首帧 p50"), ("p2f_p95_us", "按下→首帧 p95"),
    ("r2f_p50_us", "松手→首帧 p50"),
    ("iv_p50_us", "帧间隔 p50"), ("iv_p95_us", "帧间隔 p95"),
    ("tick_p50_us", "ui_tick p50"), ("tick_p95_us", "ui_tick p95"),
    ("fps_avg", "FPS"), ("cpu_lvgl", "LVGL CPU%"), ("frames", "帧数/轮"),
]

def load(path):
    rows = list(csv.DictReader(open(path)))
    cases = {}
    for r in rows:
        cases.setdefault(r["case"], []).append(r)
    return cases

def med(rs, col):
    vals = []
    for r in rs:
        try: vals.append(float(r[col]))
        except (KeyError, ValueError): pass
    return st.median(vals) if vals else None

def main():
    files = sys.argv[1:]
    base = load(files[0])
    others = [(f.split("/")[-1].replace(".csv", ""), load(f)) for f in files[1:]]
    for case in base:
        print(f"\n## {case}  (N={len(base[case])})")
        hdr = "| 指标 | 基线 | " + " | ".join(n for n, _ in others) + " | Δ% |"
        print(hdr); print("|" + "---|" * (len(others) + 3))
        for col, label in METRICS:
            b = med(base[case], col)
            if b is None: continue
            cells, deltas = [], []
            for n, data in others:
                v = med(data.get(case, []), col)
                cells.append("--" if v is None else f"{v:,.1f}")
                if v is not None and b: deltas.append(f"{(v - b) * 100 / b:+.1f}%")
            print(f"| {label} | {b:,.1f} | " + " | ".join(cells) + " | " + " / ".join(deltas) + " |")
        ivn = med(base[case], "iv_n")
        print(f"| iv_n（帧间隔样本数中位数） | {ivn:.0f} | " + " | ".join(
            f"{med(d.get(case, []), 'iv_n'):.0f}" for _, d in others) + " | |")
        if ivn is not None and ivn < 8:
            print(f"  ⚠ {case}: iv_n 中位数 {ivn:.0f} < 8 —— 帧间隔统计量不可用于判定")
main()
