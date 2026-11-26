#!/usr/bin/env python3
"""把 perf_bench CSV 汇总成"每个用例一行"的表。中位数只作描述；验收按逐轮 CSV 逐轮判定，不用多轮中位数合并通过。
用法: perf_tab.py a.csv [b.csv ...]   —— 第一个是基线，之后的给 Δ%。
口径: 每个指标先取该次重复的值，再对 N 次重复取中位数（抗单次抖动）；
      iv_n 是全段间隔总数；iv_sampled 是实际进入分位数缓冲的样本数；
      iv_capped=1 时 p50/p95/p99 只基于缓冲，iv_max/iv_long_n 为全段；
      样本太小的用例（tap）标注为噪声不可判。"""
import csv, sys, statistics as st
from perf_bench import (FIELDS, INT_FIELDS, AUX_FIELDS, AUX_INT_FIELDS, DERIVED_FIELDS,
                        BENCH_OPTIONAL_FIELDS, add_derived, number_ok)

METRICS = [
    ("p2f_p50_us", "按下→首帧 p50"), ("p2f_p95_us", "按下→首帧 p95"),
    ("r2f_p50_us", "松手→首帧 p50"),
    ("iv_p50_us", "帧间隔 p50"), ("iv_p95_us", "帧间隔 p95"),
    ("iv_p99_us", "帧间隔 p99"), ("iv_long_n", "长帧数 ≥33.3ms"),
    ("iv_sampled", "帧间隔样本数（缓冲）"), ("iv_capped", "帧间隔截断（1=只统计缓冲）"),
    ("tick_p50_us", "ui_tick p50"), ("tick_p95_us", "ui_tick p95"),
    ("fps_avg", "FPS"), ("cpu_lvgl", "LVGL CPU%"), ("frames", "帧数/轮"),
    ("part_flush_us_pf", "flush µs/帧"), ("part_ppa_us_pf", "PPA µs/帧"),
    ("part_dma2d_us_pf", "DMA2D µs/帧"), ("part_submit_us_pf", "submit µs/帧"),
    ("draw_blend_us_pf", "blend µs/帧"), ("draw_text_us_pf", "文字 µs/帧"),
    ("ui_page_paint_us_pf", "page_paint µs/帧"), ("ui_page_layout_us_pc", "page_layout µs/次"),
    ("ui_chart_draw_us_pf", "chart µs/手势帧"), ("ui_chart_draw_us_pf_chart", "chart µs/带图帧"),
    ("tick_motion_p50_us", "运动期 tick p50"), ("tick_full_p50_us", "整刷 tick p50"),
    ("ui_gesture_frames", "手势帧数"), ("ui_chart_less_frames", "无图帧数"),
]

def load(path):
    cases = {}
    with open(path, newline="") as source:
        reader = csv.DictReader(source)
        fields = reader.fieldnames or []
        numeric = (set(FIELDS) - {"kind"}) | {"repeat"} | set(AUX_FIELDS) | set(DERIVED_FIELDS)
        present = numeric.intersection(fields)
        if ("case" not in fields or not {col for col, _ in METRICS}.intersection(fields)
                or len(fields) != len(set(fields))):
            raise ValueError(f"{path}: 缺少 case/指标或表头重复")
        for line, row in enumerate(reader, 2):
            if None in row or not (row.get("case") or "").strip():
                raise ValueError(f"{path}:{line}: CSV 行不完整")
            for col in present:
                raw = row.get(col)
                # Auxiliary measurements are optional (old firmware / idle runs);
                # a missing cell stays None and is skipped by med().
                if (col in AUX_FIELDS or col in DERIVED_FIELDS
                        or col in BENCH_OPTIONAL_FIELDS) and raw in (None, ""):
                    row[col] = None
                    continue
                try:
                    value = float(raw)
                except (TypeError, ValueError):
                    raise ValueError(f"{path}:{line}: {col} 缺失或非数字") from None
                if not number_ok(col, value):
                    raise ValueError(f"{path}:{line}: {col} 非有限/非法负数")
                if col in INT_FIELDS | AUX_INT_FIELDS | {"repeat"} and not value.is_integer():
                    raise ValueError(f"{path}:{line}: {col} 不是整数")
                row[col] = value
            add_derived(row)  # 派生列以总量重算，CSV 里只是缓存
            cases.setdefault(row["case"], []).append(row)
    if not cases:
        raise ValueError(f"{path}: 没有样本")
    return cases

def med(rs, col):
    # 缺整列的历史 CSV 仍可对照；已声明的列不得静默丢弃坏单元格。
    vals = [r[col] for r in rs if r.get(col) is not None]
    return st.median(vals) if vals else None

def main():
    files = sys.argv[1:]
    if not files:
        # 没有输入文件时给用法，不抛 traceback。
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
        print(f"| iv_n（帧间隔总数中位数，全段） | {'--' if ivn is None else f'{ivn:,.0f}'} | " + " | ".join(
            "--" if (v := med(d.get(case, []), "iv_n")) is None else f"{v:,.0f}"
            for _, d in others) + " | |")
        if ivn is not None and ivn < 8:
            print(f"  ⚠ {case}: iv_n 中位数 {ivn:.0f} < 8 —— 帧间隔统计量不可用于判定")
        sampled = med(base[case], "iv_sampled")
        capped = med(base[case], "iv_capped")
        if capped or (sampled is not None and ivn is not None and sampled < ivn):
            print(f"  ⚠ {case}: 帧间隔采样截断（样本 {sampled if sampled is not None else '?'} / 全段 {ivn if ivn is not None else '?'}）；"
                  f"p50/p95/p99 仅基于缓冲，iv_max/iv_long 为全段")


if __name__ == "__main__":
    sys.exit(main())
