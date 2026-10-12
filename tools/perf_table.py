#!/usr/bin/env python3
"""把 perf_bench CSV 汇总成"每个用例一行"的表（对同用例的 N 次重复取中位数）。
用法: perf_tab.py a.csv [b.csv ...]   —— 第一个是基线，之后的给 Δ%。
口径: 每个指标先取该次重复的值，再对 N 次重复取中位数（抗单次抖动）；
      iv_n 是原始样本数，太小的用例（tap）标注为噪声不可判。"""
import csv, sys, statistics as st
import math
from perf_bench import FIELDS, INT_FIELDS

METRICS = [
    ("p2f_p50_us", "按下→首帧 p50"), ("p2f_p95_us", "按下→首帧 p95"),
    ("r2f_p50_us", "松手→首帧 p50"),
    ("iv_p50_us", "帧间隔 p50"), ("iv_p95_us", "帧间隔 p95"),
    ("tick_p50_us", "ui_tick p50"), ("tick_p95_us", "ui_tick p95"),
    ("fps_avg", "FPS"), ("cpu_lvgl", "LVGL CPU%"), ("frames", "帧数/轮"),
]

def load(path):
    cases = {}
    with open(path, newline="") as source:
        reader = csv.DictReader(source)
        fields = reader.fieldnames or []
        numeric = (set(FIELDS) - {"kind"}) | {"repeat"}
        present = numeric.intersection(fields)
        if ("case" not in fields or not {col for col, _ in METRICS}.intersection(fields)
                or len(fields) != len(set(fields))):
            raise ValueError(f"{path}: 缺少 case/指标或表头重复")
        for line, row in enumerate(reader, 2):
            if None in row or not row.get("case", "").strip():
                raise ValueError(f"{path}:{line}: CSV 行不完整")
            for col in present:
                try:
                    value = float(row[col])
                except (TypeError, ValueError):
                    raise ValueError(f"{path}:{line}: {col} 缺失或非数字") from None
                if not math.isfinite(value) or value < 0:
                    raise ValueError(f"{path}:{line}: {col} 非有限非负数")
                if col in INT_FIELDS | {"repeat"} and not value.is_integer():
                    raise ValueError(f"{path}:{line}: {col} 不是整数")
                row[col] = value
            cases.setdefault(row["case"], []).append(row)
    if not cases:
        raise ValueError(f"{path}: 没有样本")
    return cases

def med(rs, col):
    # 缺整列的历史 CSV 仍可对照；已声明的列不得静默丢弃坏单元格。
    vals = [r[col] for r in rs if col in r]
    return st.median(vals) if vals else None

def main():
    files = sys.argv[1:]
    if not files:
        # 以前这里直接 files[0] → IndexError 的 traceback（审计 M52）：人看到的是
        # 崩溃，不是"要传文件"。参数错了就说清楚怎么用。
        print("用法：python3 tools/perf_table.py <perf.csv> [更多 CSV…]", file=sys.stderr)
        return 2
    try:
        base = load(files[0])
        others = [(f.split("/")[-1].replace(".csv", ""), load(f)) for f in files[1:]]
    except (OSError, ValueError, csv.Error) as error:
        print("性能样本无效：" + str(error), file=sys.stderr)
        return 2
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
        print(f"| iv_n（帧间隔样本数中位数） | {'--' if ivn is None else f'{ivn:,.0f}'} | " + " | ".join(
            "--" if (v := med(d.get(case, []), "iv_n")) is None else f"{v:,.0f}"
            for _, d in others) + " | |")
        if ivn is not None and ivn < 8:
            print(f"  ⚠ {case}: iv_n 中位数 {ivn:.0f} < 8 —— 帧间隔统计量不可用于判定")


if __name__ == "__main__":
    sys.exit(main())
