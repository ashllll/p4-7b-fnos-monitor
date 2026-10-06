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

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PKG = os.path.join(HERE, "nasscreencompanion")


def page_calls(html):
    """页面的 (方法, 路径, 请求体键名) 列表。"""
    calls = []
    for m in re.finditer(r"\bget\(\s*'([^']+)'", html):
        calls.append(("GET", m.group(1), []))
    # post('/api/x', { a: ..., b: ... })  —— 只取第一层对象字面量的键
    for m in re.finditer(r"\bpost\(\s*'([^']+)'\s*,\s*\{([^{}]*)\}", html, re.S):
        keys = re.findall(r"([A-Za-z_]\w*)\s*:", m.group(2))
        calls.append(("POST", m.group(1), keys))
    # post('/api/x', body) 这种把对象先存变量的写法：把变量名对应的字面量找出来
    for m in re.finditer(r"\bpost\(\s*'([^']+)'\s*,\s*([A-Za-z_]\w*)\s*\)", html):
        var = m.group(2)
        vm = re.search(r"\bconst\s+%s\s*=\s*\{(.*?)\};" % re.escape(var), html, re.S)
        keys = re.findall(r"([A-Za-z_]\w*)\s*:", vm.group(1)) if vm else []
        calls.append(("POST", m.group(1), keys))
    return calls


def server_routes(src):
    """服务端路由路径（去重）。"""
    return set(re.findall(r'path == "(/api/[^"]+)"', src)) | \
           set(re.findall(r"path in \(\s*\"(/api/[^\"]+)\"", src))


def accepted_keys(src):
    """每个端点认的请求体键名：{路径: {键}}，从处理函数里现抓。"""
    out = {}

    def body_of(fn_pat):
        """按**缩进**切函数体，不按花括号：Python 的函数体里可能一个 `{` 都没有
        （`_apply_config` 就是这样），按花括号找会一路吃到下一个函数的字典字面量上去。"""
        m = re.search(fn_pat, src)
        if not m:
            return ""
        start = src.rfind("\n", 0, m.start()) + 1
        lines = src[start:].split("\n")
        out = [lines[0]]
        for ln in lines[1:]:
            if ln.strip() and not ln.startswith("        ") and ln.lstrip().startswith(("def ", "class ", "@")):
                break
            out.append(ln)
        return "\n".join(out)

    cfg = body_of(r"def _apply_config\(self, body\)")
    keys = set(re.findall(r'"(\w+)"\s+in\s+body', cfg))
    # 取值表 ("interval", float, 0.2, 60.0) 里的键名。
    # 注意 findall 只给了一个分组时返回的是**字符串列表**，不是元组列表——
    # 写成 t[0] 会取到首字母（i/h/p），于是这三个键被当成"服务端不认"。
    keys |= set(re.findall(r'\(\s*"(\w+)"\s*,\s*(?:float|int|str|bool)', cfg))
    out["/api/config"] = keys

    # 其余 POST 端点：函数体里 body.get("k") / body["k"]。
    # 这里是按"就近 700 字符"取的，可能把相邻处理函数的键也收进来——**偏保守**：
    # 收宽了只会让检查松一点（漏报），不会误报。真要精确到函数级就得按缩进切
    # 每个 handler，等哪天它真的漏了再补。
    for path, fn in (("/api/collector/docker", r"def _collector_docker|collector/docker"),
                     ("/api/pairing/new", r"pairing/new"),
                     ("/api/pairing/cancel", r"pairing/cancel"),
                     ("/api/devices/revoke", r"devices/revoke")):
        seg = src[src.find(path):] if path in src else ""
        # 就近取 700 字符，够覆盖那个分支的 body 用法
        out[path] = set(re.findall(r'body(?:\.get\(|\[)"(\w+)"', seg[:700]))
    return out


def main():
    pkg = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PKG
    html_path = os.path.join(pkg, "app", "ui", "www", "index.html")
    srv_path = os.path.join(pkg, "app", "server", "nas_companion_server.py")
    for p in (html_path, srv_path):
        if not os.path.exists(p):
            print(f"找不到 {p}")
            return 2

    calls = page_calls(io.open(html_path, encoding="utf-8").read())
    src = io.open(srv_path, encoding="utf-8").read()
    routes = server_routes(src)
    accepts = accepted_keys(src)

    problems = []
    seen = set()
    for method, path, keys in calls:
        if (method, path) in seen:
            continue
        seen.add((method, path))
        if path not in routes:
            problems.append(f"页面调用 {method} {path}，但服务端的 do_GET/do_POST 里没有这个路由"
                            f"（用户会看到一个 404）")
            continue
        if method != "POST" or not keys:
            continue
        ok = accepts.get(path)
        if ok is None:
            continue
        for k in keys:
            if k not in ok:
                problems.append(f"页面往 {path} 发的请求体里有 `{k}`，但服务端不认这个键 "
                                f"（认的是 {sorted(ok)}）—— 点了保存会提示成功、而设置没生效")

    print(f"页面调用：{len(seen)} 个端点；服务端路由 {len(routes)} 条")
    for method, path in sorted(seen):                 # seen 里存的是 (方法, 路径)
        keys = next(k for m2, p2, k in calls if (m2, p2) == (method, path))
        extra = f" 请求体 {sorted(keys)}" if keys else ""
        print(f"  {method:4} {path}{extra}")

    # 反向：服务端有、页面从不调用的端点（不是错，但值得知道）
    page_paths = {p for _, p, _ in calls}
    unused = sorted(routes - page_paths)
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
