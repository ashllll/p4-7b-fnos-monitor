#!/usr/bin/env python3
"""管理页 ↔ 服务端：路径和请求体的字段名对不对得上？

为什么值得单独一个检查：**页面发出去的字段名，服务端不认识时不会报错**——
`_apply_config()` 是逐键判断的（`if "key" in body`），多一个没人认的键它一声不吭，
少一个键就是"这个设置项没生效"。用户看到的是"点了保存、提示已保存、但设置没变"，
而 `page_check.js` 也抓不到：它喂的是**预制响应**，页面画得再对，请求体是不是
服务端认的那几个键，它根本不看。

所以这里做静态核对，两边都现抓：
  · 页面（`app/ui/www/index.html`）：`get('/api/…')` 与 `post('/api/…', { … })`
    里的路径，以及 post 请求体对象字面量的键名。
  · 服务端（`app/server/nas_companion_server.py`）：`do_GET`/`do_POST` 里
    `path == "/api/…"` 的路由，以及 `_apply_config()` 认的键（`"k" in body`
    与取值表里的 `("k", cast, lo, hi)`）。

用法：python3 nas/fpk/api_check.py [包根目录]
退出码非 0 表示有对不上的地方。
"""

import ast
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PKG = os.path.join(HERE, "nasscreencompanion")


def object_keys(body):
    # Deliberately bounded grammar: a flat literal with explicit identifier keys.
    # A spread, shorthand, quoted key, nested value or comma expression must be
    # checked by a richer parser rather than silently disappearing from this gate.
    if not body.strip():
        return []
    keys = []
    for field in body.strip().rstrip(",").split(","):
        match = re.match(r"\s*([A-Za-z_]\w*)\s*:\s*\S", field)
        if not match:
            raise ValueError("unsupported POST object field")
        keys.append(match.group(1))
    return keys


def page_calls(html):
    """Extract the supported literal calls, refusing unrecognized POST bodies."""
    calls, handled = [], set()
    literal = r"(['\"])(/api/[^'\"]+)\1"
    for m in re.finditer(r"\bget\(\s*" + literal + r"\s*\)", html):
        calls.append(("GET", m.group(2), []))
        handled.add(m.start())
    for m in re.finditer(r"\bpost\(\s*" + literal + r"\s*,\s*\{([^{}]*)\}\s*\)", html, re.S):
        body = m.group(3)
        keys = object_keys(body)
        if body.strip() and not keys:
            raise ValueError("POST literal body keys unavailable: " + m.group(2))
        calls.append(("POST", m.group(2), keys)); handled.add(m.start())
    for m in re.finditer(r"\bpost\(\s*" + literal + r"\s*,\s*([A-Za-z_]\w*)\s*\)", html):
        var = m.group(3)
        declarations = list(re.finditer(r"\bconst\s+%s\s*=\s*\{([^{}]*)\};" % re.escape(var), html, re.S))
        if len(declarations) != 1:
            raise ValueError("POST body variable not uniquely resolved: " + var)
        if re.search(r"\b%s\s*(?:\.[A-Za-z_]\w*|\[[^\]]*\])\s*=" % re.escape(var), html) or re.search(r"\bObject\.assign\(\s*%s\b" % re.escape(var), html):
            raise ValueError("POST body variable is mutated: " + var)
        body = declarations[0].group(1)
        keys = object_keys(body)
        if body.strip() and not keys:
            raise ValueError("POST variable body keys unavailable: " + var)
        calls.append(("POST", m.group(2), keys)); handled.add(m.start())
    # Function declarations are helpers, not calls. Every other helper invocation
    # must be recognized; expressions/dynamic URLs are explicitly outside this parser.
    for m in re.finditer(r"\b(?:get|post)\s*\(", html):
        if re.search(r"\bfunction\s*$", html[:m.start()]):
            continue
        if m.start() not in handled:
            raise ValueError("unsupported page API call at offset %d" % m.start())
    if not calls:
        raise ValueError("no page API calls found")
    return calls


def route_blocks(src):
    """Return actual method/path branches, bounded by Python AST statements."""
    out = {}
    for fn in ast.walk(ast.parse(src)):
        if not isinstance(fn, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        method = {"do_GET": "GET", "_route_get": "GET",
                  "do_POST": "POST", "_route_post": "POST"}.get(fn.name)
        if method is None:
            continue
        for node in ast.walk(fn):
            if not isinstance(node, ast.If) or not isinstance(node.test, ast.Compare):
                continue
            test = node.test
            if not isinstance(test.left, ast.Name) or test.left.id != "path" or len(test.ops) != 1:
                continue
            rhs = test.comparators[0]
            values = [rhs] if isinstance(test.ops[0], ast.Eq) else (rhs.elts if isinstance(test.ops[0], ast.In) and isinstance(rhs, (ast.Tuple, ast.List)) else [])
            for value in values:
                if isinstance(value, ast.Constant) and isinstance(value.value, str) and value.value.startswith("/api/"):
                    out[(method, value.value)] = node.body
    return out


def server_routes(src):
    return set(route_blocks(src))


def accepted_keys(src):
    out = {}
    tree = ast.parse(src)
    cfg = [n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == "_apply_config"]
    cfg_text = ast.get_source_segment(src, cfg[0]) if len(cfg) == 1 else ""
    # 当前配置入口只负责取锁；仅在入口确实委托body时读取同名唯一校验方法。
    if len(cfg) == 1 and any(isinstance(n, ast.Call) and
            isinstance(n.func, ast.Attribute) and isinstance(n.func.value, ast.Name) and
            n.func.value.id == "self" and n.func.attr == "_apply_config_locked" and
            len(n.args) == 1 and isinstance(n.args[0], ast.Name) and n.args[0].id == "body"
            for n in ast.walk(cfg[0])):
        helper = [n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == "_apply_config_locked"]
        if len(helper) != 1:
            raise ValueError("configuration validator helper is not uniquely resolved")
        cfg_text += "\n" + ast.get_source_segment(src, helper[0])
    for (method, path), statements in route_blocks(src).items():
        if method != "POST":
            continue
        keys = set()
        for statement in statements:
            for node in ast.walk(statement):
                if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and
                    isinstance(node.func.value, ast.Name) and node.func.value.id == "body" and
                    node.func.attr == "get" and node.args and isinstance(node.args[0], ast.Constant) and
                    isinstance(node.args[0].value, str)):
                    keys.add(node.args[0].value)
                if (isinstance(node, ast.Subscript) and isinstance(node.value, ast.Name) and
                    node.value.id == "body" and isinstance(node.slice, ast.Constant) and isinstance(node.slice.value, str)):
                    keys.add(node.slice.value)
        if path == "/api/config":
            if not cfg_text:
                out[path] = None; continue
            keys |= set(re.findall(r'["\'](\w+)["\']\s+in\s+body', cfg_text))
            keys |= set(re.findall(r'\(\s*["\'](\w+)["\']\s*,\s*(?:float|int|str|bool)', cfg_text))
        out[path] = keys
    return out


def main():
    pkg = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PKG
    html_path = os.path.join(pkg, "app", "ui", "www", "index.html")
    srv_path = os.path.join(pkg, "app", "server", "nas_companion_server.py")
    for p in (html_path, srv_path):
        if not os.path.exists(p):
            print(f"找不到 {p}")
            return 2

    try:
        with io.open(html_path, encoding="utf-8") as f:
            calls = page_calls(f.read())
        with io.open(srv_path, encoding="utf-8") as f:
            src = f.read()
        routes = server_routes(src)
        accepts = accepted_keys(src)
    except (OSError, ValueError, SyntaxError) as exc:
        print(f"API 检查未完成：{exc}")
        return 2

    problems = []
    seen = set()
    for method, path, keys in calls:
        seen.add((method, path))
        if (method, path) not in routes:
            problems.append(f"页面调用 {method} {path}，但服务端的 do_GET/do_POST 里没有这个路由"
                            f"（用户会看到一个 404）")
            continue
        if method != "POST" or not keys:
            continue
        ok = accepts.get(path)
        if ok is None:
            # 服务端认哪些键是由 accepted_keys() 从源码抠出来的：抠不到就等于这一项没查
            # （审计 M40）。以前直接 continue，然后照样打印"字段名服务端也都认 ✓"。
            problems.append(f"页面往 {path} 发的请求体有 {sorted(keys)}，但服务端这条路由的"
                            f"请求体键名没抠出来（accepted_keys() 认不出来）—— 字段名这一项等于没核对")
            continue
        for k in keys:
            if k not in ok:
                problems.append(f"页面往 {path} 发的请求体里有 `{k}`，但服务端不认这个键 "
                                f"（认的是 {sorted(ok)}）—— 点了保存会提示成功、而设置没生效")

    print(f"页面调用：{len(seen)} 个端点；服务端路由 {len(routes)} 条")
    for method, path in sorted(seen):                 # seen 里存的是 (方法, 路径)
        keys = sorted({key for m2, p2, kk in calls if (m2, p2) == (method, path) for key in kk})
        extra = f" 请求体 {sorted(keys)}" if keys else ""
        print(f"  {method:4} {path}{extra}")

    # 反向：服务端有、页面从不调用的端点（不是错，但值得知道）
    page_paths = {p for _, p, _ in calls}
    unused = sorted({p for _, p in routes} - page_paths)
    if unused:
        print(f"（服务端还有 {len(unused)} 条页面没用到的路由：{'、'.join(unused)}）")

    if problems:
        print("\n对不上的地方：")
        for p in problems:
            print("  ✗", p)
        return 1
    print("\n页面调用的每个端点都存在，请求体的字段名服务端也都认 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
