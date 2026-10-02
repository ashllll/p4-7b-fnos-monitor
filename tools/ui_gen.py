#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ui_gen.py — KK_UI_UMG 风格的 Manifest → LVGL C 代码生成器（构建期运行）。

源头：components/fnos_monitor/ui/Source/<PackageId>/*.json（人和 AI 维护）
产物：components/fnos_monitor/ui/Generated/<PackageId>/*.generated.c/h（可删除重建）

对应 KK_UI_UMG 的 Editor/Generators：本工具只消费 Source JSON，不读生成物。
用法：
    python3 tools/ui_gen.py [--source <SourcePackageDir>] [--check]

--check 只校验生成结果是否与磁盘一致（CI/自检用），不写文件。
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = REPO / "components/fnos_monitor/ui/Source/FnosDashboard"

SCHEMA_VERSION = "1.0"

KNOWN_CONTROLS = [
    "Panel", "Text", "Image", "Button", "Divider",
    "Bar", "Arc", "Trend", "Chip", "Icon", "SignalBars",
]

# 控件 → 绑定属性允许表（LVGL 扩展见 docs/ui-kk.md §4）
BINDABLE = {
    "Panel": {"color": "string", "alpha": "float", "x": "int", "width": "int"},
    "Image": {"color": "string", "alpha": "float", "x": "int", "width": "int"},
    "Text": {"text": "string", "color": "string", "alpha": "float"},
    "Button": {"color": "string", "alpha": "float", "interactable": "bool"},
    "Bar": {"value": "float"},
    "Arc": {"value": "float"},
    "Trend": {"points": "series", "top": "float"},
    "Chip": {"text": "string", "color": "string"},
    "SignalBars": {"value": "int"},
}

EVENT_ALIASES = {
    "onClick": "LV_EVENT_CLICKED",
    "onSwipeLeft": "kk:swipe-left",
    "onSwipeRight": "kk:swipe-right",
}

FIELD_CTYPES = {
    "string": "char",
    "int": "int32_t",
    "float": "float",
    "bool": "bool",
    "series": "kk_series_t",
}


class GenError(Exception):
    pass


# ─────────────────────────────── helpers ───────────────────────────────

def snake(name: str) -> str:
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()
    return re.sub(r"_+", "_", s)


def cstr(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def parse_color(value: str):
    if not isinstance(value, str) or not re.fullmatch(r"#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?", value):
        raise GenError(f"bad color {value!r}")
    hexpart = value[1:]
    rgb = int(hexpart[:6], 16)
    alpha = int(hexpart[6:8], 16) if len(hexpart) == 8 else 0xFF
    return rgb, alpha


def color_lit(value: str) -> str:
    rgb, _ = parse_color(value)
    return f"kk_c(0x{rgb:06X})"


def opa_lit(value: str) -> str:
    _, alpha = parse_color(value)
    return f"kk_opa(0x{alpha:02X})"


def load_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:  # noqa: BLE001
        raise GenError(f"{path}: {exc}") from exc


def fnum(x) -> float:
    if isinstance(x, bool) or not isinstance(x, (int, float)):
        raise GenError(f"bad number {x!r}")
    return float(x)


# ─────────────────────────────── model ───────────────────────────────

class Node:
    def __init__(self, raw: dict, parent: "Node | None", ctx: "Ctx"):
        self.raw = raw
        self.parent = parent
        self.ctx = ctx
        self.type = raw.get("type")
        self.id = raw.get("id")
        if self.type not in KNOWN_CONTROLS:
            raise GenError(f"node {self.id}: unsupported type {self.type!r}")
        if not self.id or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", self.id):
            raise GenError(f"node id {self.id!r} must be PascalCase/C identifier")
        if self.id in ctx.nodes:
            raise GenError(f"duplicate node id {self.id}")
        ctx.nodes[self.id] = self
        self.rect = raw.get("rect") or {}
        self.children = [Node(c, self, ctx) for c in (raw.get("children") or [])]

    # member name in generated view struct
    @property
    def member(self) -> str:
        return snake(self.id)

    def spec(self, key: str) -> dict:
        return self.raw.get(key) or {}


class Ctx:
    def __init__(self, source_dir: Path):
        self.source_dir = source_dir
        self.pkg = load_json(source_dir / "package.json")
        self.layout = load_json(source_dir / "layout.json")
        self.bindings = load_json(source_dir / "bindings.json")
        self.codegen = load_json(source_dir / "codegen.json")
        self.strings = load_json(source_dir / "strings.json")
        self.assets = load_json(source_dir / "assets.json")
        self.nodes: dict[str, Node] = {}
        self.root = Node(self.layout["root"], None, self)
        self.resolve_rects()
        self.pages = self.layout.get("pages") or []
        self.fields = self.bindings.get("mvvm", {}).get("fields", [])
        self.bound = self.bindings.get("bindings", [])
        self.events = self.bindings.get("events", [])
        self.prefix = self.codegen.get("prefix", "fnos_dash")
        self.controller = self.codegen.get("controller", {}).get("className", "FnosDashboardController")
        self.default_culture = self.strings.get("defaultCulture", "zh-Hans")

    def resolve_rects(self) -> None:
        """把 anchor/position/size 解析成确定像素。
        v4 修正：运行期 lv_obj_get_content_width() 在对象刚创建、坐标未刷新时返回 0/1，
        拉伸/右对齐 rect 全算废（top_bar w=1、hd_clock x=-120 实测）。"""
        def walk(n: Node, pw: int, ph: int) -> None:
            r = n.rect
            if r:
                amin, amax, pos, size = (r["anchorMin"], r["anchorMax"],
                                         r["position"], r["size"])
                w = round(pw * (amax[0] - amin[0]) + size[0])
                h = round(ph * (amax[1] - amin[1]) + size[1])
                x = round(pw * amin[0]) + pos[0]
                y = round(ph * amin[1]) + pos[1]
            else:
                x, y, w, h = 0, 0, pw, ph
            n.px = (x, y, max(1, w), max(1, h))
            for c in n.children:
                walk(c, n.px[2], n.px[3])
        walk(self.root, self.pkg["designResolution"]["width"],
             self.pkg["designResolution"]["height"])

    def node(self, node_id: str) -> Node:
        try:
            return self.nodes[node_id]
        except KeyError:
            raise GenError(f"unknown node id {node_id!r}") from None

    def field(self, field_id: str) -> dict:
        for f in self.fields:
            if f.get("id") == field_id:
                return f
        raise GenError(f"unknown field id {field_id!r}")

    def loc(self, key: str) -> str:
        entry = self.strings.get("strings", {}).get(key)
        if not entry:
            raise GenError(f"unknown locKey {key!r}")
        if self.default_culture not in entry:
            raise GenError(f"locKey {key!r} missing culture {self.default_culture}")
        return entry[self.default_culture]

    def font_symbol(self, asset_id: str) -> str:
        for a in self.assets.get("assets", []):
            if a.get("id") == asset_id:
                sym = a.get("symbol")
                if not sym:
                    raise GenError(f"asset {asset_id!r} has no symbol")
                return sym
        raise GenError(f"unknown font asset {asset_id!r}")


# ─────────────────────────── validation ───────────────────────────

def validate(ctx: Ctx) -> list[str]:
    errors: list[str] = []

    def err(msg: str):
        errors.append(msg)

    if ctx.pkg.get("schemaVersion") != SCHEMA_VERSION:
        err(f"package.json schemaVersion must be {SCHEMA_VERSION}")
    if ctx.pkg.get("packageId") != ctx.source_dir.name:
        err(f"packageId {ctx.pkg.get('packageId')!r} != folder name {ctx.source_dir.name!r}")

    controls = set((ctx.pkg.get("v1") or {}).get("controls", []))
    for n in ctx.nodes.values():
        if n.type not in controls:
            err(f"node {n.id}: type {n.type} not declared in package.json v1.controls")

    # rect sanity
    for n in ctx.nodes.values():
        r = n.rect
        if n.parent is not None:
            if set(r) < {"anchorMin", "anchorMax", "position", "size"}:
                err(f"node {n.id}: rect must define anchorMin/anchorMax/position/size")
            for k in ("anchorMin", "anchorMax", "position", "size"):
                v = r.get(k)
                if not isinstance(v, list) or len(v) != 2:
                    err(f"node {n.id}: rect.{k} must be [x, y]")

    # text rules (KK: locKey XOR dynamic text binding)
    bound_text = {(b["controlId"], b.get("property")) for b in ctx.bound}
    for n in ctx.nodes.values():
        if n.type != "Text":
            continue
        t = n.spec("text")
        has_loc = "locKey" in t
        has_dyn = (n.id, "text") in bound_text
        if not has_loc and not has_dyn:
            err(f"Text {n.id}: needs locKey or a text binding")
        if has_loc and has_dyn:
            err(f"Text {n.id}: must not have both locKey and text binding (KK TXT003)")
        if has_loc:
            try:
                ctx.loc(t["locKey"])
            except GenError as exc:
                err(f"Text {n.id}: {exc}")

    # fields
    for f in ctx.fields:
        if f.get("type") == "series" and not isinstance(f.get("capacity"), int):
            err(f"field {f.get('id')}: series field needs capacity (C port)")
    seen_fields = set()
    for f in ctx.fields:
        fid = f.get("id")
        if fid in seen_fields:
            err(f"duplicate field id {fid}")
        seen_fields.add(fid)
        if f.get("type") not in FIELD_CTYPES:
            err(f"field {fid}: unknown type {f.get('type')!r}")
        if f.get("type") == "string" and not isinstance(f.get("maxLen"), int):
            err(f"field {fid}: string field needs maxLen (C port)")

    # bindings
    for b in ctx.bound:
        cid, fid, prop = b.get("controlId"), b.get("fieldId"), b.get("property")
        try:
            node = ctx.node(cid)
        except GenError as exc:
            err(f"binding: {exc}")
            continue
        try:
            field = ctx.field(fid)
        except GenError as exc:
            err(f"binding {cid}: {exc}")
            continue
        allowed = BINDABLE.get(node.type, {})
        if prop not in allowed:
            err(f"binding {cid}.{prop}: not bindable on {node.type}")
            continue
        want = allowed[prop]
        have = field.get("type")
        ok = (have == want) or (want == "float" and have == "int") or (want == "text" and False)
        if prop in ("text",) and have in ("string", "int", "float", "bool"):
            ok = True
        if prop in ("value", "top", "alpha", "x", "width") and have in ("float", "int"):
            ok = True
        if prop == "color" and have == "string":
            ok = True
        if prop == "points" and have == "series":
            ok = True
        if prop == "interactable" and have == "bool":
            ok = True
        if not ok:
            err(f"binding {cid}.{prop}: field {fid} type {have} incompatible (want {want})")
        if prop == "text" and node.spec("text").get("locKey"):
            err(f"binding {cid}.text: node has locKey (KK TXT003)")

    # events
    seen_handlers = set()
    for e in ctx.events:
        cid, ev, handler = e.get("controlId"), e.get("event"), e.get("handler")
        try:
            node = ctx.node(cid)
        except GenError as exc:
            err(f"event: {exc}")
            continue
        if ev not in EVENT_ALIASES:
            err(f"event {cid}.{ev}: unsupported event")
        if node.type != "Button" and ev == "onClick":
            err(f"event {cid}.onClick: only Button supports onClick")
        if not handler or not re.fullmatch(r"On[A-Za-z0-9]+", handler):
            err(f"event {cid}: handler {handler!r} must be OnXxx style (KK event rules)")
        if handler in seen_handlers:
            err(f"event {cid}: duplicate handler {handler}")
        seen_handlers.add(handler)

    # pages
    if not ctx.pages:
        err("layout.json must declare pages[] (id + index)")
    for p in ctx.pages:
        try:
            node = ctx.node(p["id"])
        except GenError as exc:
            err(f"pages: {exc}")
            continue
        if node.type != "Panel":
            err(f"pages: {p['id']} must be a Panel")

    return errors


# ─────────────────────────── code emitters ───────────────────────────

def emit_strings_h(ctx: Ctx) -> str:
    guard = f"{ctx.prefix.upper()}_STRINGS_GENERATED_H"
    out = [
        "/* 由 tools/ui_gen.py 生成，请勿手改。源头：Source/%s/strings.json */" % ctx.pkg["packageId"],
        "#pragma once",
        "",
        f"/* 文化：{ctx.default_culture}（静态文案在生成期烘焙，运行时不做本地化解析） */",
        "",
    ]
    for key in ctx.strings.get("strings", {}):
        macro = "FNOS_STR_" + key.replace(".", "_").replace("-", "_").upper()
        out.append(f"#define {macro} {cstr(ctx.loc(key))}")
    out.append("")
    return "\n".join(out)


def emit_store_h(ctx: Ctx) -> str:
    p = ctx.prefix
    guard = f"{p.upper()}_STORE_GENERATED_H"
    out = [
        "/* 由 tools/ui_gen.py 生成，请勿手改。源头：Source/%s/bindings.json */" % ctx.pkg["packageId"],
        "#pragma once",
        "#include <stdbool.h>",
        "#include <stdint.h>",
        '#include "kk_widgets.h"',
        "",
        "typedef enum {",
    ]
    for i, f in enumerate(ctx.fields):
        out.append(f"    {p.upper()}_FIELD_{f['id']} = {i},")
    out += [
        f"    {p.upper()}_FIELD_COUNT",
        f"}} {p}_field_id_t;",
        "",
    ]
    for f in ctx.fields:
        if f["type"] == "series":
            out.append(f"#define {p.upper()}_SERIES_{f['id'].upper()}_CAP {f['capacity']}")
    out += [
        f"#define {p.upper()}_DIRTY_WORDS (({p.upper()}_FIELD_COUNT + 31) / 32)",
        "",
        f"typedef struct {p}_store {{",
    ]
    for f in ctx.fields:
        t = f["type"]
        if t == "string":
            out.append(f"    char {snake(f['id'])}[{f['maxLen']}];")
        elif t == "series":
            out.append(f"    kk_series_t {snake(f['id'])};")
        else:
            out.append(f"    {FIELD_CTYPES[t]} {snake(f['id'])};")
    out += [
        f"    uint32_t dirty[{p.upper()}_DIRTY_WORDS];",
        f"}} {p}_store_t;",
        "",
        f"static inline bool {p}_field_dirty(const {p}_store_t *s, unsigned f)",
        "{",
        "    return (s->dirty[f >> 5] >> (f & 31)) & 1u;",
        "}",
        "",
        f"void {p}_store_init({p}_store_t *s);",
        f"bool {p}_store_update_str({p}_store_t *s, {p}_field_id_t f, const char *v);",
        f"bool {p}_store_update_int({p}_store_t *s, {p}_field_id_t f, int32_t v);",
        f"bool {p}_store_update_float({p}_store_t *s, {p}_field_id_t f, float v);",
        f"bool {p}_store_update_bool({p}_store_t *s, {p}_field_id_t f, bool v);",
        f"bool {p}_store_series_push({p}_store_t *s, {p}_field_id_t f, float v);",
        "",
    ]
    return "\n".join(out)


def emit_store_c(ctx: Ctx) -> str:
    p = ctx.prefix
    st = f"{p}_store_t"
    out = [
        "/* 由 tools/ui_gen.py 生成，请勿手改。 */",
        f'#include "{p}_store.generated.h"',
        "#include <stdio.h>",
        "#include <string.h>",
        "",
    ]
    for f in ctx.fields:
        if f["type"] == "series":
            out.append(f"static float {p}_{snake(f['id'])}_buf[{p.upper()}_SERIES_{f['id'].upper()}_CAP];")
    out += [
        "",
        f"void {p}_store_init({st} *s)",
        "{",
        "    memset(s, 0, sizeof(*s));",
    ]
    for f in ctx.fields:
        t, fid = f["type"], snake(f["id"])
        default = f.get("default")
        if t == "string":
            out.append(f"    snprintf(s->{fid}, sizeof(s->{fid}), \"%s\", {cstr(default or '')});")
        elif t == "series":
            out.append(f"    kk_series_init(&s->{fid}, {p}_{fid}_buf, {p.upper()}_SERIES_{f['id'].upper()}_CAP);")
        elif t == "bool":
            out.append(f"    s->{fid} = {'true' if default else 'false'};")
        elif t in ("int", "float"):
            out.append(f"    s->{fid} = {float(default or 0)!r}f;" if t == "float" else f"    s->{fid} = {int(default or 0)};")
    out += [
        "    memset(s->dirty, 0, sizeof(s->dirty));",
        "}",
        "",
    ]

    def setter(name, ctype, assign, fmt=None):
        out.append(f"bool {p}_store_update_{name}({st} *s, {p}_field_id_t f, {ctype} v)")
        out.append("{")
        out.append("    if ((unsigned)f >= %s_FIELD_COUNT) return false;" % p.upper())
        out.append("    switch (f) {")
        for fld in ctx.fields:
            if fmt is None:
                continue
            if fld["type"] not in fmt:
                continue
            out.append(f"    case {p.upper()}_FIELD_{fld['id']}:")
            out.append("        " + assign.replace("{F}", snake(fld["id"])).replace("{NEQ}", "return true;"))
            out.append("        break;")
        out.append("    default:")
        out.append("        return false;")
        out.append("    }")
        out.append("    s->dirty[(unsigned)f >> 5] |= 1u << ((unsigned)f & 31);")
        out.append("    return true;")
        out.append("}")
        out.append("")

    setter("str", "const char *",
           'if (strcmp(s->{F}, v ? v : "") == 0) {NEQ}\n        snprintf(s->{F}, sizeof(s->{F}), "%s", v ? v : "");',
           fmt={"string"})
    setter("int", "int32_t", "if (s->{F} == v) {NEQ}\n        s->{F} = v;", fmt={"int"})
    setter("float", "float", "if (s->{F} == v) {NEQ}\n        s->{F} = v;", fmt={"float"})
    setter("bool", "bool", "if (s->{F} == v) {NEQ}\n        s->{F} = v;", fmt={"bool"})

    out.append(f"bool {p}_store_series_push({st} *s, {p}_field_id_t f, float v)")
    out.append("{")
    out.append("    switch (f) {")
    for fld in ctx.fields:
        if fld["type"] != "series":
            continue
        out.append(f"    case {p.upper()}_FIELD_{fld['id']}:")
        out.append(f"        kk_series_push(&s->{snake(fld['id'])}, v);")
        out.append("        break;")
    out.append("    default:")
    out.append("        return false;")
    out.append("    }")
    out.append("    s->dirty[(unsigned)f >> 5] |= 1u << ((unsigned)f & 31);")
    out.append("    return true;")
    out.append("}")
    out.append("")
    return "\n".join(out)


def node_member_decl(n: Node) -> list[str]:
    if n.type == "Bar":
        return [f"    kk_bar_t {n.member};"]
    if n.type == "Trend":
        return [f"    kk_trend_t {n.member};"]
    if n.type == "SignalBars":
        return [f"    lv_obj_t *{n.member}[4];"]
    return [f"    lv_obj_t *{n.member};"]


def emit_view_h(ctx: Ctx) -> str:
    p = ctx.prefix
    out = [
        "/* 由 tools/ui_gen.py 生成，请勿手改。源头：Source/%s/layout.json */" % ctx.pkg["packageId"],
        "#pragma once",
        '#include "lvgl.h"',
        '#include "kk_widgets.h"',
        "",
        f"#define {p.upper()}_PAGE_COUNT {len(ctx.pages)}",
        "",
        f"typedef struct {p}_view {{",
        "    lv_obj_t *root;",
        f"    lv_obj_t *page[{p.upper()}_PAGE_COUNT];",
    ]
    for n in ctx.nodes.values():
        if n.parent is None:
            continue            # 根节点就是结构体里的 root，别再生成同名成员
        out += node_member_decl(n)
    out += [
        f"}} {p}_view_t;",
        "",
        f"void {p}_view_build({p}_view_t *v, lv_obj_t *parent);",
        "",
        "/* Controller 事件入口（手写 FnosDashboardController.c 实现） */",
    ]
    for e in ctx.events:
        out.append(f"void {ctx.controller}_{e['handler']}(void);")
    out.append("")
    return "\n".join(out)


def rect_args(n: Node) -> str:
    # 生成期已解析为确定像素（见 Ctx.resolve_rects），运行时零算术
    x, y, w, h = n.px
    return f"kk_rect(0, 0, 0, 0, {x}, {y}, {w}, {h})"


def style_lines(n: Node) -> list[str]:
    out = []
    m = f"v->{n.member}"
    if n.type in ("Panel", "Image", "Button"):
        img = n.spec("image")
        if img.get("color"):
            out.append(f"    kk_style_bg({m}, {color_lit(img['color'])}, {opa_lit(img['color'])});")
        if img.get("radius") is not None:
            out.append(f"    lv_obj_set_style_radius({m}, {int(img['radius'])}, 0);")
        if img.get("borderWidth"):
            out.append(f"    lv_obj_set_style_border_width({m}, {int(img['borderWidth'])}, 0);")
            out.append(f"    lv_obj_set_style_border_color({m}, {color_lit(img.get('borderColor', '#334052FF'))}, 0);")
            out.append(f"    lv_obj_set_style_border_opa({m}, {opa_lit(img.get('borderColor', '#334052FF'))}, 0);")
    if n.type == "Divider":
        d = n.spec("divider")
        out.append(f"    kk_style_bg({m}, {color_lit(d.get('color', '#334052FF'))}, {opa_lit(d.get('color', '#334052FF'))});")
    return out


def emit_node_fn(ctx: Ctx, n: Node) -> list[str]:
    """每节点一个 static 构建函数（后序发射）。
    v4 修正：整棵树内联进一个函数会让 fnos_dash_view_build 栈帧爆到 11 KB
    （main 任务栈只有 4 KB），递归拆分后峰值栈 = 树深 × 小帧。"""
    p = ctx.prefix
    out: list[str] = []
    m = n.member
    rect = rect_args(n)
    # 先发射子节点的构建函数（后序：调用者在后面定义也合法）
    child_fns: list[str] = []
    for c in n.children:
        child_fns += emit_node_fn(ctx, c)
    out += child_fns
    # noinline：否则 -O2 会把单用静态函数内联回父函数，栈帧重新叠起来（v4 实测 2.4 KB）
    out.append(f"static void __attribute__((noinline)) build_{m}(fnos_dash_view_t *v, lv_obj_t *parent)")
    out.append("{")

    if n.type == "Bar":
        b = n.spec("bar")
        out.append(f"    kk_bar_create(&v->{m}, parent, {rect}, "
                   f"{color_lit(b.get('color', '#2F80EDFF'))}, {int(b.get('radius', 2))});")
    elif n.type == "Trend":
        t = n.spec("trend")
        auto = 0 if any(b["controlId"] == n.id and b["property"] == "top" for b in ctx.bound) else 1
        out.append(f"    kk_trend_create(&v->{m}, parent, {rect}, "
                   f"{color_lit(t.get('color', '#2F80EDFF'))}, {int(t.get('points', 60))}, "
                   f"{int(t.get('areaOpacity', 128))}, {int(t.get('gridLines', 0))}, {auto});")
    elif n.type == "SignalBars":
        out.append(f"    kk_signal_bars(parent, {rect}, v->{m});")
    else:
        ctor = {
            "Panel": "kk_panel_create",
            "Image": "kk_panel_create",
            "Button": "kk_button_create",
            "Text": "kk_label_create",
            "Divider": "kk_panel_create",
            "Chip": "kk_chip_create",
            "Arc": "kk_arc_create",
            "Icon": "kk_icon_create",
        }[n.type]
        extra = ""
        if n.type == "Icon":
            kind = n.spec("icon").get("kind", "overview")
            extra = f", KK_ICON_{kind.upper()}, {color_lit(n.spec('icon').get('color', '#BFC9D7FF'))}"
        elif n.type == "Arc":
            extra = f", {color_lit(n.spec('arc').get('color', '#2F80EDFF'))}"
        elif n.type == "Chip":
            extra = f", {color_lit(n.spec('chip').get('color', '#2F80EDFF'))}"
        out.append(f"    v->{m} = {ctor}(parent, {rect}{extra});")
        out += style_lines(n)

    # 装饰对象清 CLICKABLE（LVGL 9.5 默认可点，会吞掉点击）；Button/滑动页保留
    # Bar/Trend/SignalBars 的成员不是 lv_obj_t*，标志在各自构造器里清
    swipe_node = any(e["controlId"] == n.id and e["event"].startswith("onSwipe")
                     for e in ctx.events)
    if n.type in ("Bar", "Trend", "SignalBars"):
        pass
    elif swipe_node:
        out.append(f"    lv_obj_add_flag(v->{m}, LV_OBJ_FLAG_CLICKABLE);")
        out.append(f"    lv_obj_remove_flag(v->{m}, LV_OBJ_FLAG_SCROLLABLE);")
    elif n.type != "Button":
        out.append(f"    lv_obj_remove_flag(v->{m}, LV_OBJ_FLAG_CLICKABLE);")
        out.append(f"    lv_obj_remove_flag(v->{m}, LV_OBJ_FLAG_SCROLLABLE);")

    if n.type == "Text":
        t = n.spec("text")
        font = t.get("fontAsset")
        if font:
            out.append(f"    lv_obj_set_style_text_font(v->{m}, &{ctx.font_symbol(font)}, 0);")
        if t.get("color"):
            out.append(f"    lv_obj_set_style_text_color(v->{m}, {color_lit(t['color'])}, 0);")
            out.append(f"    lv_obj_set_style_text_opa(v->{m}, {opa_lit(t['color'])}, 0);")
        if t.get("align"):
            align = {"left": "LV_TEXT_ALIGN_LEFT", "right": "LV_TEXT_ALIGN_RIGHT",
                     "center": "LV_TEXT_ALIGN_CENTER"}[t["align"]]
            out.append(f"    lv_obj_set_style_text_align(v->{m}, {align}, 0);")
        if t.get("letterSpace"):
            out.append(f"    lv_obj_set_style_text_letter_space(v->{m}, {int(t['letterSpace'])}, 0);")
        if t.get("lineSpace"):
            out.append(f"    lv_obj_set_style_text_line_space(v->{m}, {int(t['lineSpace'])}, 0);")
        if t.get("locKey"):
            out.append(f"    lv_label_set_text(v->{m}, FNOS_STR_{t['locKey'].replace('.', '_').replace('-', '_').upper()});")
        else:
            out.append(f"    lv_label_set_text(v->{m}, \"\");")
        out.append(f"    kk_label_vcenter(v->{m}, {n.px[3]});")

    for c in n.children:
        out.append(f"    build_{c.member}(v, v->{m});")
    out.append("}")
    out.append("")
    return out


def emit_view_c(ctx: Ctx) -> str:
    p = ctx.prefix
    out = [
        "/* 由 tools/ui_gen.py 生成，请勿手改。 */",
        f'#include "{p}_view.generated.h"',
        f'#include "{p}_strings.generated.h"',
        '#include "kk_rect.h"',
        '#include "kk_theme.h"',
        '#include "fnos_fonts.h"',
        "",
    ]
    # 事件回调：View 只转发
    for e in ctx.events:
        cid, ev = e["controlId"], e["event"]
        if ev.startswith("onSwipe"):
            out.append(f"static void ev_{snake(cid)}_{snake(ev)}(void)")
        else:
            out.append(f"static void ev_{snake(cid)}_{snake(ev)}(lv_event_t *e)")
        out.append("{")
        if not ev.startswith("onSwipe"):
            out.append("    (void)e;")
        out.append(f"    {ctx.controller}_{e['handler']}();")
        out.append("}")
        out.append("")

    # 每节点一个 static 构建函数：必须定义在 view_build 之前（C 不允许嵌套定义）
    node_fns: list[str] = []
    for c in ctx.root.children:
        node_fns += emit_node_fn(ctx, c)
    out += node_fns

    out += [
        f"void {p}_view_build({p}_view_t *v, lv_obj_t *parent)",
        "{",
        f"    lv_obj_t *scr = parent ? parent : lv_screen_active();",
        f"    v->root = kk_panel_create(scr, kk_rect(0, 0, 0, 0, 0, 0, {ctx.pkg['designResolution']['width']}, {ctx.pkg['designResolution']['height']}));",
        f"    lv_obj_remove_flag(v->root, LV_OBJ_FLAG_CLICKABLE);",
        f"    lv_obj_remove_flag(v->root, LV_OBJ_FLAG_SCROLLABLE);",
        f"    kk_style_bg(v->root, {color_lit('#080B10FF')}, kk_opa(0xFF));",
    ]
    root = ctx.root
    out.append("    /* 构建整棵树（每节点一个函数，控制栈帧：v4 栈溢出修正） */")
    for c in root.children:
        out.append(f"    build_{c.member}(v, v->root);")

    # pages[]
    for i, pg in enumerate(ctx.pages):
        out.append(f"    v->page[{i}] = v->{snake(ctx.node(pg['id']).id)};")
    out.append("")
    # 注册事件（同一节点的左右滑只 attach 一次）
    swipes: dict[str, dict[str, str]] = {}
    for e in ctx.events:
        cid, ev = e["controlId"], e["event"]
        node = ctx.node(cid)
        if ev == "onClick":
            out.append(f"    lv_obj_add_event_cb(v->{node.member}, ev_{snake(cid)}_{snake(ev)}, LV_EVENT_CLICKED, NULL);")
        elif ev in ("onSwipeLeft", "onSwipeRight"):
            swipes.setdefault(node.member, {})[ev] = f"ev_{snake(cid)}_{snake(ev)}"
    for member, cbs in swipes.items():
        left = cbs.get("onSwipeLeft", "NULL")
        right = cbs.get("onSwipeRight", "NULL")
        out.append(f"    kk_swipe_attach(v->{member}, {left}, {right});")
    out.append("}")
    out.append("")
    return "\n".join(out)


def emit_binder_h(ctx: Ctx) -> str:
    p = ctx.prefix
    out = [
        "/* 由 tools/ui_gen.py 生成，请勿手改。 */",
        "#pragma once",
        f'#include "{p}_view.generated.h"',
        f'#include "{p}_store.generated.h"',
        "",
        f"void {p}_binder_flush({p}_view_t *v, {p}_store_t *s);",
        "",
    ]
    return "\n".join(out)


def emit_binder_c(ctx: Ctx) -> str:
    p = ctx.prefix
    out = [
        "/* 由 tools/ui_gen.py 生成，请勿手改。Binder：唯一允许把 Store 写回 LVGL 的地方。 */",
        f'#include "{p}_view.generated.h"',
        f'#include "{p}_store.generated.h"',
        '#include "kk_theme.h"',
        "#include <stdio.h>",
        "#include <string.h>",
        "",
        f"void {p}_binder_flush({p}_view_t *v, {p}_store_t *s)",
        "{",
        "    (void)v;",
    ]
    for b in ctx.bound:
        cid, fid, prop = b["controlId"], b["fieldId"], b["property"]
        node = ctx.node(cid)
        field = ctx.field(fid)
        member = node.member
        fs = snake(fid)
        field_ref = f"s->{fs}"
        bit = f"{p.upper()}_FIELD_{fid}"
        fmt = b.get("format")
        out.append(f"    if ({p}_field_dirty(s, {bit})) {{")
        if prop == "text":
            if node.type == "Chip":
                out.append(f"        kk_chip_set_text(v->{member}, {field_ref});")
            elif field["type"] == "string":
                out.append(f"        lv_label_set_text(v->{member}, {field_ref});")
            else:
                fmtv = fmt or ("%d" if field["type"] == "int" else ("%g" if field["type"] == "float" else "%d"))
                if field["type"] == "bool":
                    out.append(f"        lv_label_set_text(v->{member}, {field_ref} ? \"1\" : \"0\");")
                else:
                    out.append(f"        char buf[48]; snprintf(buf, sizeof(buf), \"{fmtv}\", (double){field_ref});")
                    out.append(f"        lv_label_set_text(v->{member}, buf);")
        elif prop == "color":
            if node.type == "Text":
                out.append(f"        lv_obj_set_style_text_color(v->{member}, kk_color_parse({field_ref}), 0);")
                out.append(f"        lv_obj_set_style_text_opa(v->{member}, kk_opa_parse({field_ref}), 0);")
            elif node.type == "Chip":
                out.append(f"        kk_chip_set_color(v->{member}, kk_color_parse({field_ref}));")
            else:
                out.append(f"        kk_style_bg(v->{member}, kk_color_parse({field_ref}), kk_opa_parse({field_ref}));")
        elif prop == "alpha":
            out.append(f"        lv_obj_set_style_opa(v->{member}, kk_opa_pct({field_ref}), 0);")
        elif prop == "x":
            out.append(f"        lv_obj_set_x(v->{member}, (int32_t){field_ref});")
        elif prop == "width":
            out.append(f"        lv_obj_set_width(v->{member}, (int32_t){field_ref});")
        elif prop == "interactable":
            out.append(f"        if ({field_ref}) lv_obj_add_flag(v->{member}, LV_OBJ_FLAG_CLICKABLE);")
            out.append(f"        else lv_obj_remove_flag(v->{member}, LV_OBJ_FLAG_CLICKABLE);")
        elif prop == "value":
            if node.type == "Bar":
                out.append(f"        kk_bar_set(&v->{member}, {field_ref});")
            elif node.type == "Arc":
                out.append(f"        kk_arc_set(v->{member}, (int){field_ref});")
            elif node.type == "SignalBars":
                out.append(f"        kk_signal_set(v->{member}, (int8_t){field_ref});")
        elif prop == "points":
            out.append(f"        kk_trend_sync_series(&v->{member}, &{field_ref});")
        elif prop == "top":
            out.append(f"        kk_trend_range(&v->{member}, {field_ref});")
        out.append("    }")
    out += [
        "    memset(s->dirty, 0, sizeof(s->dirty));",
        "}",
        "",
    ]
    return "\n".join(out)


# ─────────────────────────────── main ───────────────────────────────

def generate(ctx: Ctx) -> dict[str, str]:
    return {
        f"{ctx.prefix}_strings.generated.h": emit_strings_h(ctx),
        f"{ctx.prefix}_store.generated.h": emit_store_h(ctx),
        f"{ctx.prefix}_store.generated.c": emit_store_c(ctx),
        f"{ctx.prefix}_view.generated.h": emit_view_h(ctx),
        f"{ctx.prefix}_view.generated.c": emit_view_c(ctx),
        f"{ctx.prefix}_binder.generated.h": emit_binder_h(ctx),
        f"{ctx.prefix}_binder.generated.c": emit_binder_c(ctx),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default=str(DEFAULT_SOURCE))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    source_dir = Path(args.source).resolve()
    if not source_dir.is_dir():
        print(f"error: source package not found: {source_dir}", file=sys.stderr)
        return 2

    try:
        ctx = Ctx(source_dir)
        errors = validate(ctx)
    except GenError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if errors:
        print(f"validate: {len(errors)} error(s)", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    out_dir = (source_dir.parent.parent / "Generated" / ctx.pkg["packageId"]).resolve()
    files = generate(ctx)

    stale = []
    for name, content in files.items():
        target = out_dir / name
        if args.check:
            if not target.exists() or target.read_text(encoding="utf-8") != content:
                stale.append(name)
        else:
            out_dir.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

    if args.check:
        if stale:
            print("generate: stale " + ", ".join(stale), file=sys.stderr)
            return 1
        print(f"generate: up to date ({len(files)} files)")
        return 0

    print(f"generate: wrote {len(files)} files to {out_dir}")
    for name in files:
        print(f"  - {name}")
    return 0


# ─────────────────────── lint（非致命：版面预算 / 越界） ───────────────────────
# 实机照片踩过的两类问题（副行 138px 装不下格式串、文本框越出卡片）在这里提前拦下。
# 字宽粗估：CJK/全角 ≈ 1.0em，ASCII ≈ 0.55em；只作预算校验，不做精确排版。

def _font_em_size(symbol: str) -> int:
    m = re.search(r"_(\d+)$", symbol or "")
    return int(m.group(1)) if m else 16


def _text_px(s: str, em: int) -> float:
    return sum(em if ord(ch) > 0x2E80 else em * 0.55 for ch in s)


def lint(ctx: Ctx) -> list[str]:
    warns: list[str] = []

    def walk(n: Node) -> None:
        if n.parent and getattr(n, "px", None) and getattr(n.parent, "px", None):
            px, py, pw, ph = n.px
            _, _, ppw, pph = n.parent.px
            if px + pw > ppw + 1:
                warns.append(f"[越界] {n.parent.id} → {n.id}: x={px} w={pw} 超出父宽 {ppw}")
            if py + ph > pph + 1:
                warns.append(f"[越界] {n.parent.id} → {n.id}: y={py} h={ph} 超出父高 {pph}")
        if n.type == "Text":
            t = n.spec("text")
            em = _font_em_size(ctx.font_symbol(t["fontAsset"])) if t.get("fontAsset") else 16
            box_w = n.px[2] if getattr(n, "px", None) else 0
            samples: list[tuple[str, str]] = []
            if t.get("locKey"):
                entry = ctx.strings.get("strings", {}).get(t["locKey"], {})
                samples = [(c, s) for c, s in entry.items()]
            else:
                for b in ctx.bound:
                    if b.get("controlId") == n.id and b.get("property") == "text":
                        f = ctx.field(b["fieldId"])
                        samples = [("default", str(f.get("default") or ""))]
            for culture, s in samples:
                if not s:
                    continue
                w = _text_px(s, em)
                if box_w and w > box_w:
                    warns.append(f"[文本预算] {n.id} ({culture}) 估算 {w:.0f}px > 框宽 {box_w}px：{s[:20]!r}")
        for c in n.children:
            walk(c)

    walk(ctx.root)
    return warns


if __name__ == "__main__":
    raise SystemExit(main())
