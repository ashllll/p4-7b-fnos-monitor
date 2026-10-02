#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""audit_fonts.py — 字体与文案一致性审计（豆腐块 / 花屏两类历史真因的静态拦截）。

为什么需要它（都是本工程实机踩过的）：
  1. **豆腐块 A：拉丁字体写中文**。num_*/txt_* 字库只含 ASCII(0x20-0x7E)+°+·，
     中文写进用了这些字体的标签就是方框（v3 §9.2、v4.2 首轮"采集/前"都栽在这）。
  2. **豆腐块 B：字库子集缺字形**。中文字符集由 tools/gen_fonts.sh 从源码字符串字面量
     扫出来，新加了中文却忘了重跑生成脚本 → 那个字没有字形（v4.2 首轮"数据前"缺"前"）。
  3. **花屏：RLE 压缩字体**。lv_font_conv 默认 .bitmap_format=1（RLE），
     LVGL 的解压器跑在全局状态上，两个绘制线程并行解压会互相踩 → 字形随机碎裂。
     本工程所有字体必须 .bitmap_format=0（生成时 --no-compress）。
  4. **未编译的僵尸字库**：fonts/ 里留着上一代的 .c（不在 CMakeLists 里），
     既占仓库体积又让 fnos_fonts.h 声明一堆用不到的符号。

用法：
    python3 tools/audit_fonts.py [--source <SourcePackageDir>]

检查项：
    A. 每个字体 .c 的 .bitmap_format 必须为 0；
    B. 每个 Text 节点的实际文案（locKey 各语言 + 绑定字段默认值）必须被它用的字库覆盖；
    C. Controller 里 SETS/SETS_F 的格式串中文字面量，必须被目标字段所在节点的字库覆盖；
    D. 全仓 UI 源码里出现的中文字形，必须至少存在于某个"参与构建"的字库（否则重跑 gen_fonts.sh）；
    E. fonts/ 下未参与构建的僵尸字库文件（提示清理）。
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ui_gen  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = REPO / "components/fnos_monitor/ui/Source/FnosDashboard"
FONT_DIR = REPO / "components/fnos_monitor/fonts"
COMPONENT_CMAKE = REPO / "components/fnos_monitor/CMakeLists.txt"

# 参与"运行时文案"的 C 源码（Generated/ 是产物不扫；fonts/ 是字库本身不扫）
UI_C_SOURCES = [
    REPO / "components/fnos_monitor/ui/FnosDashboardController.c",
    REPO / "components/fnos_monitor/ui/FnosDashboardViewAnim.c",
    REPO / "components/fnos_monitor/fnos_ui.c",
    REPO / "components/fnos_monitor/fnos_ui.h",
    REPO / "components/fnos_monitor/kk_ui/kk_widgets.c",
    REPO / "main/main.cpp",
]

CJK_RE = re.compile(r"[\u3000-\u303f\u4e00-\u9fff\uff00-\uffef]")
PLACEHOLDER_RE = re.compile(r"%(?:[-+ #0]*\d*(?:\.\d+)?(?:[hljztL])?[diouxXeEfgGaAcspn%])")


def worst_case_text(fmt: str, s_len: int = 12, int_len: int = 10, float_len: int = 8) -> str:
    """把 printf 格式串替换成"最坏情况"字面量，用于估算最长显示宽度与字节数。

    为什么需要：字段默认值（如 "TB / 总量"）比运行时文本短得多，只按默认值估查不出真问题——
    v4.2.2 存储行被 Store 静默截断就是这么漏掉的。参数按用途给两档：
      · 字节预算（字段缓冲）用保守档（默认 12/10/8），宁可高估也不能漏掉真会截断的串；
      · 宽度预算用现实档（6/6/7，约等于"数字+单位"），否则净是误报。
    """
    def sub(pattern: str, repl: str) -> None:
        nonlocal out
        out = re.sub(pattern, repl, out)

    out = fmt
    digits = str(int_len)
    sub(r"%(?:[-+ #0]*\d*(?:\.\d+)?(?:[hljztL])?)[diu]", "9" * int_len)
    sub(r"%(?:[-+ #0]*\d*(?:\.\d+)?(?:[hljztL])?)[xX]", "F" * (int_len // 2 or 1))
    sub(r"%(?:[-+ #0]*\d*(?:\.\d+)?(?:[hljztL])?)[eEfgGaA]", "8" * float_len)
    sub(r"%(?:[-+ #0]*\d*(?:\.\d+)?(?:[hljztL])?)[cs]", "X" * s_len)
    del digits
    return out.replace("%%", "%")


# ────────────────────────────── 字库解析 ──────────────────────────────

def font_coverage(path: Path) -> tuple[set[int], int]:
    """解析 lv_font_conv 生成的 .c：返回（覆盖的码点集合, bitmap_format）。"""
    src = path.read_text(encoding="utf-8", errors="ignore")

    m = re.search(r"\.bitmap_format\s*=\s*(\d+)", src)
    fmt = int(m.group(1)) if m else -1

    lists: dict[str, list[int]] = {}
    for name, body in re.findall(
            r"static const uint16_t (unicode_list_\d+)\[\]\s*=\s*\{(.*?)\};", src, re.S):
        lists[name] = [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", body)]

    cov: set[int] = set()
    for blk in re.findall(r"\{\s*\.range_start\s*=\s*(\d+),\s*\.range_length\s*=\s*(\d+),(.*?)\}",
                          src, re.S):
        start, length, rest = int(blk[0]), int(blk[1]), blk[2]
        ul = re.search(r"\.unicode_list\s*=\s*(\w+)", rest)
        kind = re.search(r"\.type\s*=\s*(LV_FONT_FMT_TXT_CMAP_\w+)", rest)
        sparse = "SPARSE" in (kind.group(1) if kind else "")
        # SPARSE_TINY 的 unicode_list 存的是相对 range_start 的**偏移**，不是码点本身
        if sparse and ul and ul.group(1) != "NULL":
            cov.update(start + off for off in lists.get(ul.group(1), []))
        else:
            cov.update(range(start, start + length))
    return cov, fmt


def load_fonts() -> tuple[dict[Path, tuple[set[int], int]], set[str], list[str]]:
    """返回（按路径的字库信息, 参与构建的字库名, 僵尸字库文件列表）。"""
    cmake = COMPONENT_CMAKE.read_text(encoding="utf-8")
    built = set(re.findall(r"fonts/(\w+)\.c", cmake))

    fonts: dict[Path, tuple[set[int], int]] = {}
    zombies: list[str] = []
    for p in sorted(FONT_DIR.glob("ui_font_*.c")):
        fonts[p] = font_coverage(p)
        if p.stem not in built:
            zombies.append(p.name)
    return fonts, built, zombies


# ────────────────────────────── 源码字面量 ──────────────────────────────

def strip_comments_keep_literals(s: str) -> str:
    """只保留字符串/字符字面量，剥掉注释（与 gen_fonts.sh 的扫描口径一致）。"""
    out: list[str] = []
    i, n = 0, len(s)
    while i < n:
        if s.startswith("/*", i):
            j = s.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = n if j < 0 else j
        elif s[i] in "\"'":
            q = s[i]
            j = i + 1
            while j < n and s[j] != q:
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        else:
            i += 1
    return "".join(out)


def drop_log_calls(s: str) -> str:
    """去掉 ESP_LOGx(...) 整段调用：串口日志的字形不上屏，不需要字库覆盖。
    （否则"日志里写了句中文"会误报成豆腐块风险。）"""
    out: list[str] = []
    i = 0
    while True:
        m = re.search(r"\bESP_LOG\w*\s*\(", s[i:])
        if not m:
            out.append(s[i:])
            break
        start = i + m.start()
        out.append(s[i:start])
        j = i + m.end()
        depth = 1
        while j < len(s) and depth:
            if s[j] == "(":
                depth += 1
            elif s[j] == ")":
                depth -= 1
            j += 1
        i = j
    return "".join(out)


def strip_comments_keep_code(s: str) -> str:
    """注释替换成空格、其余（含字符串字面量）原样保留 —— 用于"按调用点解析源码"。

    为什么不能复用 strip_comments_keep_literals()：那个只留字面量、把 `SETS(FtPoll, "…")`
    的调用语法一起丢掉，正则永远匹配不上（本审计的检查 C/F 曾因此静默失效 = 假绿）。
    """
    out: list[str] = []
    i, n = 0, len(s)
    while i < n:
        if s.startswith("/*", i):
            j = s.find("*/", i + 2)
            end = n if j < 0 else j + 2
            out.append(" " * (end - i))
            i = end
        elif s.startswith("//", i):
            j = s.find("\n", i)
            end = n if j < 0 else j
            out.append(" " * (end - i))
            i = end
        elif s[i] in "\"'":
            q = s[i]
            j = i + 1
            while j < n and s[j] != q:
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def field_macro_table(controller_src: str) -> dict[str, list[str]]:
    """把 #define F_XXX(i) ((fnos_dash_field_id_t[]){ FNOS_DASH_FIELD_A, ... }[(i)]) 展开成字段名表。"""
    table: dict[str, list[str]] = {}
    for name, body in re.findall(
            r"#define\s+(F_\w+)\(i\)\s+\(\(fnos_dash_field_id_t\[\]\)\s*\{(.*?)\}\[\(i\)\]\)",
            controller_src, re.S):
        # 注意：捕获组拿到的已经是"去掉 FNOS_DASH_FIELD_ 前缀"的字段名，
        # 早先这里又切了一次前缀 → 全是空串 → 字段查不到节点 → 整组行静默漏检。
        table[name] = re.findall(r"FNOS_DASH_FIELD_(\w+)", body)
    return table


# ────────────────────────────── 审计主体 ──────────────────────────────

def audit(ctx: ui_gen.Ctx) -> tuple[list[str], list[str]]:
    """返回（错误, 警告）。"""
    errors: list[str] = []
    warns: list[str] = []

    fonts, built, zombies = load_fonts()
    by_symbol = {p.stem: (p, info[0]) for p, info in fonts.items()}
    asset_font: dict[str, tuple[Path, set[int]]] = {}
    for a in ctx.assets.get("assets", []):
        sym = a.get("symbol") or ""
        if sym in by_symbol:
            path, cov = by_symbol[sym]
            asset_font[a["id"]] = (path, cov)

    # A. 压缩格式
    for p, (_, fmt) in fonts.items():
        if p.stem in built and fmt != 0:
            errors.append(f"[花屏风险] {p.name}: .bitmap_format={fmt}（必须 0；生成时加 --no-compress）")

    # E. 僵尸字库
    for z in zombies:
        warns.append(f"[僵尸字库] fonts/{z} 不在 CMakeLists 的 PROJ_SRCS 里（既占体积又让 fnos_fonts.h 多声明）")

    # 字体继承：节点没写 fontAsset 时沿用父节点
    def font_of(n: ui_gen.Node) -> str | None:
        cur: ui_gen.Node | None = n
        while cur is not None:
            fa = cur.spec("text").get("fontAsset")
            if fa:
                return fa
            cur = cur.parent
        return None

    def check_text(where: str, font_id: str | None, text: str, kind: str) -> None:
        if not text:
            return
        if not font_id:
            warns.append(f"[字体缺失] {where}: 文本 {text[:16]!r} 没有 fontAsset（继承链里也没有）")
            return
        entry = asset_font.get(font_id)
        if entry is None:
            errors.append(f"[字库未声明] {where}: fontAsset {font_id!r} 不在 assets.json")
            return
        path, cov = entry
        missing = sorted({ch for ch in text if ord(ch) not in cov}, key=ord)
        if not missing:
            return
        latin_only = "cjk" not in path.stem
        detail = "".join(missing)
        hint = ("该字体是拉丁字库，中文必然画成豆腐块 → 换 font.cjk*"
                if latin_only else "字库子集缺字形 → 重跑 bash tools/gen_fonts.sh")
        errors.append(f"[豆腐块] {where}（{kind}, {path.stem}）缺字形 {detail!r}"
                      f"（{', '.join('U+%04X' % ord(c) for c in missing)}）：{hint}")

    # B. 每个 Text 节点
    bound_text = {(b.get("controlId"), b.get("fieldId"))
                  for b in ctx.bound if b.get("property") == "text"}
    for n in ctx.nodes.values():
        if n.type != "Text":
            continue
        t = n.spec("text")
        fid = font_of(n)
        if t.get("locKey"):
            entry = ctx.strings.get("strings", {}).get(t["locKey"], {})
            for culture, s in entry.items():
                check_text(f"{n.id} ({t['locKey']}/{culture})", fid, s, "locKey")
        else:
            for b in ctx.bound:
                if b.get("controlId") == n.id and b.get("property") == "text":
                    f = ctx.field(b["fieldId"])
                    check_text(f"{n.id} ← {b['fieldId']}.default", fid,
                               str(f.get("default") or ""), "字段默认值")

    # C. Controller 的 SETS/SETS_F 字面量
    controller = REPO / "components/fnos_monitor/ui/FnosDashboardController.c"
    src = controller.read_text(encoding="utf-8", errors="ignore")
    macros = field_macro_table(src)
    calls = strip_comments_keep_code(drop_log_calls(src))
    n_sets = 0

    def nodes_for_field(field_id: str) -> list[ui_gen.Node]:
        out = []
        for cid, fid2 in bound_text:
            if fid2 == field_id and cid in ctx.nodes:
                out.append(ctx.nodes[cid])
        return out

    # 注意正则：`SETS(?:_F)?\(` —— 早先写成 `SETS_F?\(`（下划线是硬字符、F 才可选），
    # 结果只匹配 SETS_F 调用，20 多处 `SETS(FtPoll, …)` 被整片漏掉。
    for m in re.finditer(r"\bSETS(?:_F)?\(\s*([A-Za-z_0-9]+)(\(i\))?\s*,\s*\"((?:[^\"\\]|\\.)*)\"",
                         calls, re.S):
        target, indexed, lit = m.group(1), m.group(2), m.group(3)
        n_sets += 1
        cjk = "".join(sorted(set(CJK_RE.findall(lit))))
        if not cjk:
            continue
        fields = macros.get(target, []) if indexed else [target]
        if not fields:
            errors.append(f"[豆腐块] Controller: SETS_F({target}) 无法展开成字段表（新加的宏要同步本审计）")
            continue
        hit = False
        worst = worst_case_text(lit)
        for field in fields:
            for node in nodes_for_field(field):
                hit = True
                if node.type != "Text":
                    # Chip 等复合控件的文字由 kk_widgets 内部创建、字体在控件实现里定，
                    # manifest 上没有 fontAsset，这里查不到≠有问题（全局覆盖由检查 D 兜底）。
                    continue
                fa = font_of(node)
                check_text(f"{node.id} ← {field}（Controller 格式串）", fa,
                           PLACEHOLDER_RE.sub("", lit), "运行时文案")
                # F. 字段缓冲预算：格式串最坏情况会不会被 Store 静默截断
                #    （宽度不做估算：%s 的实际内容长度无从得知，按最坏档估会次次误报——
                #     实测 14 条"框宽不足"告警对应的真实文案在实机照片里都完整显示。）
                ml = ctx.field(field).get("maxLen")
                if isinstance(ml, int) and ml > 0 and len(worst.encode("utf-8")) > ml:
                    warns.append(f"[缓冲预算] {field}: 格式串最坏 {len(worst.encode('utf-8'))}B（{lit[:24]!r}）"
                                 f" > maxLen {ml}B → Store 会静默截断（v4.2.2 的「余」就是这么被吃掉的）")
        if not hit:
            warns.append(f"[未映射] Controller 里 {target} 的中文格式串 {lit[:20]!r} "
                         f"没有找到绑定文本的节点，只做了全局字形校验")

    # C/F 自检：一条都没解析到就说明正则或源码结构变了（这两个检查会静默失效）
    if n_sets == 0:
        errors.append("[自检] Controller 里没解析到任何 SETS/SETS_F 调用 —— 检查 C/F 已失效，别信这个 PASS")

    # D. 全局：所有 UI 源码里的中文必须存在于某个**参与构建**的字库
    built_cov: set[int] = set()
    for p, (cov, _) in fonts.items():
        if p.stem in built:
            built_cov |= cov
    used: dict[str, set[str]] = {}
    for f in UI_C_SOURCES + [DEFAULT_SOURCE / "strings.json", DEFAULT_SOURCE / "bindings.json"]:
        if not f.exists():
            continue
        body = f.read_text(encoding="utf-8", errors="ignore")
        if f.suffix == ".json":
            found = set(CJK_RE.findall(body))
        else:
            found = set(CJK_RE.findall(strip_comments_keep_literals(drop_log_calls(body))))
        if found:
            used[str(f.relative_to(REPO))] = found
    for path_str, chars in used.items():
        gap = sorted((c for c in chars if ord(c) not in built_cov), key=ord)
        if gap:
            errors.append(f"[字库缺字] {path_str} 用到但没有任何构建内字库覆盖：{''.join(gap)}"
                          f" → 重跑 bash tools/gen_fonts.sh（并确认该文件在它的扫描清单里）")

    return errors, warns


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default=str(DEFAULT_SOURCE))
    args = ap.parse_args()

    source_dir = Path(args.source).resolve()
    try:
        ctx = ui_gen.Ctx(source_dir)
    except ui_gen.GenError as exc:
        print(f"audit: FAIL — {exc}", file=sys.stderr)
        return 1

    errors, warns = audit(ctx)
    for w in warns:
        print(f"  warn: {w}")
    for e in errors:
        print(f"  ERR : {e}", file=sys.stderr)
    if errors:
        print(f"audit: FAIL — {len(errors)} error(s), {len(warns)} warning(s)", file=sys.stderr)
        return 1
    print(f"audit: PASS — 0 error(s), {len(warns)} warning(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
