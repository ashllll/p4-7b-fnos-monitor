#!/usr/bin/env python3
"""真实字形宽度核对：直接读生成字库的 `adv_w`，量"最坏文案"是否放得下。

为什么需要它：`tools/ui_gen.py` 的 lint 用 "ASCII 0.55em / CJK 1.0em" 估算宽度，
`tools/audit_fonts.py` 的宽度预算也因此全是误报而被删掉。估算对中文够用，对
"数字 + 空格 + 单位"的混排会偏差 10%~20%，正好能把"能不能放下 118.00 MB/s"
这种边界判断做反。本工具用字库里的真实 advance 宽度，误差只来自字距微调（kern）。

用法：
    python3 tools/text_width.py                     # 跑内置的界面预算清单
    python3 tools/text_width.py ui_font_num_28.c "118.00 MB/s" "2.50 MB/s"
    python3 tools/text_width.py --layout            # 按 layout.json 逐节点核对（见下）

`--layout` 模式会遍历 manifest 里所有 Text 节点，用节点自身的字体与框宽核对
locKey 文案（各 culture）与绑定字段的 default，输出"放不下"的清单。

实现要点（都是踩过的坑）：
  * `adv_w` 单位是 1/16 px，必须除以 16。
  * ASCII 段是 `CMAP_FORMAT0_TINY`：glyph_id = glyph_id_start + (cp - range_start)。
  * CJK 段是 `CMAP_SPARSE_TINY`：**列表里存的是相对 range_start 的偏移**，不是码点
    （实测 0x4d5a + 0xB0 = U+4E0A "上"）。
  * 缺字形必须报出来：字库是子集，漏字就是屏上的豆腐块（v4.2 的"前"）。
"""
from __future__ import annotations

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "components/fnos_monitor/fonts")
UI = os.path.join(ROOT, "components/fnos_monitor/ui/Source/FnosDashboard")

# assets.json 的 font.* → 生成的字体文件（新增字库时必须同步这里）
FONT_FILE = {
    "font.numS": "ui_font_num_17.c", "font.numM": "ui_font_num_28.c",
    "font.numL": "ui_font_num_44.c", "font.hero": "ui_font_num_56.c",
    "font.label": "ui_font_txt_15.c", "font.meta": "ui_font_txt_13.c",
    "font.cjkMeta": "ui_font_cjk_13.c", "font.cjkLabel": "ui_font_cjk_17.c",
    "font.cjkTitle": "ui_font_cjk_24.c", "font.cjkVerdict": "ui_font_cjk_40.c",
}

_cache: dict[str, tuple] = {}


def parse_font(path: str):
    src = open(path, encoding="utf-8").read()
    dsc_block = src.split("glyph_dsc[] = {", 1)[1].split("};", 1)[0]
    adv = [int(a) / 16.0 for a in re.findall(r"\.adv_w = (\d+)", dsc_block)]

    cmaps_block = src.split("cmaps[] =", 1)[1].split("\n};", 1)[0]
    cmaps = []
    for entry in re.findall(r"\{([^{}]*)\}", cmaps_block):
        def field(name):
            m = re.search(rf"\.{name} = ([^,]+),", entry)
            return m.group(1).strip() if m else None

        ulist = None
        ulist_name = field("unicode_list")
        if ulist_name and ulist_name != "NULL":
            arr = re.search(rf"{ulist_name}\[\] = \{{(.*?)\}};", src, re.S)
            ulist = [int(x, 16) for x in re.findall(r"0[xX]([0-9a-fA-F]+)", arr.group(1))]
        cmaps.append({"start": int(field("range_start") or 0), "len": int(field("range_length") or 0),
                      "gid0": int(field("glyph_id_start") or 0), "ulist": ulist})
    return adv, cmaps


def font_of(asset: str):
    if asset not in _cache:
        _cache[asset] = parse_font(os.path.join(FONTS, FONT_FILE[asset]))
    return _cache[asset]


def gid_of(cmaps, ch: str):
    c = ord(ch)
    for cm in cmaps:
        if cm["ulist"] is not None:
            off = c - cm["start"]           # sparse 列表存的是偏移
            if off in cm["ulist"]:
                return cm["gid0"] + cm["ulist"].index(off)
        elif cm["start"] <= c < cm["start"] + cm["len"]:
            return cm["gid0"] + (c - cm["start"])
    return None


def width(adv, cmaps, s: str) -> tuple[float, str]:
    total, missing = 0.0, []
    for ch in s:
        g = gid_of(cmaps, ch)
        if g is None or g >= len(adv):
            missing.append(ch)
            continue
        total += adv[g]
    return total, "".join(missing)


def measure(asset: str, s: str) -> tuple[float, str]:
    adv, cmaps = font_of(asset)
    return width(adv, cmaps, s)


# 内置清单：只放"最坏情况"样本（真实文案可能更长，所以取边界值）
CASES = [
    ("font.numL", 112, "100", "P0 主数值 3 位数"),
    ("font.numM", 112, "7d 16h", "P0 运行时长"),
    ("font.numM", 166, "118.00 MB/s", "P2 双向合计（1GbE 满速，最坏）"),
    ("font.numS", 80, "100%", "P1 列表百分比"),
    ("font.cjkMeta", 248, "已用 1023 GB · 可用 1234.5 TB", "P0 存储行（最坏）"),
    ("font.cjkMeta", 100, "峰 100%", "趋势峰值行"),
    ("font.cjkMeta", 138, "负载 1.27/0.62/0.49", "P0 副行"),
    ("font.cjkMeta", 56, "固件内存", "P3 KV 标签"),
    ("font.cjkTitle", 260, "系统状态", "P0 健康卡标题"),
    ("font.cjkTitle", 448, "阵列状态", "P1 阵列标题"),
]


def layout_report() -> int:
    lay = json.load(open(os.path.join(UI, "layout.json"), encoding="utf-8"))
    bind = json.load(open(os.path.join(UI, "bindings.json"), encoding="utf-8"))
    strs = json.load(open(os.path.join(UI, "strings.json"), encoding="utf-8"))
    field = {f["id"]: f for f in bind["mvvm"]["fields"]}
    bound = {}
    for b in bind["bindings"]:
        if b.get("property", "text") == "text":
            bound[b["controlId"]] = b["fieldId"]
    bad, checked = [], 0

    def walk(n):
        nonlocal checked
        t = n.get("text")
        if n.get("type") == "Text" and t and t.get("fontAsset") in FONT_FILE:
            box_w = n["rect"]["size"][0]
            samples = []
            if t.get("locKey"):
                samples = list(strs["strings"].get(t["locKey"], {}).items())
            elif n["id"] in bound:
                samples = [("default", str(field[bound[n["id"]]].get("default") or ""))]
            for culture, s in samples:
                if not s:
                    continue
                checked += 1
                w, miss = measure(t["fontAsset"], s)
                if miss:
                    bad.append(f"[缺字形] {n['id']} ({culture}) 字库无 {'/'.join(miss)}：{s!r}")
                if w > box_w:
                    bad.append(f"[超框] {n['id']} ({culture}) {w:.1f}px > 框宽 {box_w}px：{s!r}")
        for c in n.get("children", []):
            walk(c)

    walk(lay["root"])
    print(f"layout 核对：{checked} 条文案，{len(bad)} 个问题")
    for b in bad:
        print("  ·", b)
    return 1 if bad else 0


def main() -> int:
    if "--layout" in sys.argv:
        return layout_report()
    if len(sys.argv) > 2:
        asset = sys.argv[1]
        if not asset.startswith("font."):
            asset = next((k for k, v in FONT_FILE.items() if v == sys.argv[1]), None)
            assert asset, f"未知字库：{sys.argv[1]}"
        rc = 0
        for s in sys.argv[2:]:
            w, miss = measure(asset, s)
            print(f"{w:7.1f}px  {s!r}" + (f"   缺字形: {miss}" if miss else ""))
            rc |= 1 if miss else 0
        return rc
    bad = 0
    for asset, box, s, note in CASES:
        w, miss = measure(asset, s)
        ok = w <= box and not miss
        bad += 0 if ok else 1
        print(f"{'OK ' if ok else '超!'} {w:7.1f}px / 框 {box:4d}px  {asset[5:]:8s} {s!r:26s} {note}"
              + (f"   缺字形: {miss}" if miss else ""))
    print(f"\n超框或缺字形：{bad} 项")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
