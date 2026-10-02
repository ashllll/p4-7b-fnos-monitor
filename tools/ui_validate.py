#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ui_validate.py — Source package 校验 + validation.md 交付台账（对应 KK 的 Editor/Validators）。

用法：
    python3 tools/ui_validate.py [--source <SourcePackageDir>] [--mark-generate] [--mark-verify] [--runtime verified|pending]

只允许写 validation.md 里 ui-pipeline:validation-ledger 标记块内的内容；
标记块之外的手写笔记原样保留。Runtime 状态只有 Pending / Verified 两种取值。
"""
from __future__ import annotations

import argparse
import datetime
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ui_gen  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = REPO / "components/fnos_monitor/ui/Source/FnosDashboard"
LEDGER_START = "<!-- ui-pipeline:validation-ledger:start -->"
LEDGER_END = "<!-- ui-pipeline:validation-ledger:end -->"


def build_ledger(status: dict[str, str]) -> str:
    rows = [
        ("Validate", status.get("Validate", "Pending")),
        ("Generate", status.get("Generate", "Pending")),
        ("Verify", status.get("Verify", "Pending")),
        ("Runtime", status.get("Runtime", "Pending")),
    ]
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    lines = [LEDGER_START, "", "| 阶段 | 状态 |", "| --- | --- |"]
    lines += [f"| {k} | {v} |" for k, v in rows]
    lines += ["", f"_pipeline: ui_gen/ui_validate（KK_UI_UMG LVGL 移植）· {now}_", "", LEDGER_END]
    return "\n".join(lines)


def read_ledger_state(text: str) -> dict[str, str]:
    state = {}
    m = re.search(re.escape(LEDGER_START) + r"(.*?)" + re.escape(LEDGER_END), text, re.S)
    if m:
        for k, v in re.findall(r"\|\s*(Validate|Generate|Verify|Runtime)\s*\|\s*([A-Za-z]+)\s*\|", m.group(1)):
            state[k] = v
    return state


def write_ledger(path: Path, state: dict[str, str]) -> None:
    ledger = build_ledger(state)
    text = path.read_text(encoding="utf-8") if path.exists() else "# validation\n\n" + LEDGER_START + "\n" + LEDGER_END + "\n"
    if LEDGER_START in text:
        text = re.sub(re.escape(LEDGER_START) + r".*?" + re.escape(LEDGER_END), lambda _: ledger, text, flags=re.S)
    else:
        text = text.rstrip() + "\n\n" + ledger + "\n"
    path.write_text(text, encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default=str(DEFAULT_SOURCE))
    ap.add_argument("--mark-generate", action="store_true")
    ap.add_argument("--mark-verify", action="store_true")
    ap.add_argument("--runtime", choices=["pending", "verified"])
    args = ap.parse_args()

    source_dir = Path(args.source).resolve()
    try:
        ctx = ui_gen.Ctx(source_dir)
        errors = ui_gen.validate(ctx)
    except ui_gen.GenError as exc:
        print(f"validate: FAIL — {exc}", file=sys.stderr)
        return 1

    ledger_path = source_dir / "validation.md"
    state = read_ledger_state(ledger_path.read_text(encoding="utf-8") if ledger_path.exists() else "")

    if errors:
        print(f"validate: FAIL — {len(errors)} error(s)", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        state["Validate"] = "Fail"
        write_ledger(ledger_path, state)
        return 1

    state["Validate"] = "Pass"
    if args.mark_generate:
        state["Generate"] = "Pass"
        # KK 合同：Generate 后必须重新验收，Runtime 回到 Pending
        state["Runtime"] = "Pending"
    if args.mark_verify:
        state["Verify"] = "Pass"
    if args.runtime:
        state["Runtime"] = "Verified" if args.runtime == "verified" else "Pending"
    write_ledger(ledger_path, state)

    print("validate: PASS")
    print(f"  nodes={len(ctx.nodes)} fields={len(ctx.fields)} bindings={len(ctx.bound)} events={len(ctx.events)}")
    warns = ui_gen.lint(ctx)
    if warns:
        print(f"  lint: {len(warns)} warning(s)")
        for w in warns:
            print(f"    - {w}")
    else:
        print("  lint: 0 warning(s)")
    print(f"  ledger: Validate={state['Validate']} Generate={state.get('Generate', 'Pending')} "
          f"Verify={state.get('Verify', 'Pending')} Runtime={state.get('Runtime', 'Pending')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
