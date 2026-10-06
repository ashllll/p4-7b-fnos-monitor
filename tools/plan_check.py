#!/usr/bin/env python3
"""核对 docs/fnos-companion-app-plan.md 里的**事实性声明**是否与仓库现状一致。

为什么值得单独一个脚本：那份文档既是要给人读的方案，也是 P4 上架材料的一部分。
它会随着实现不断变长（现在快 400 行），而**过期的数字比没有数字更糟**——读者不会
去核对"48 个字段"到底是不是 49，只会照着它做判断。这个脚本把三类容易过期的声明
变成可执行检查：

  1. 文档里反引号引到的**路径**必须真的存在（包内相对路径、仓库相对路径都认）；
  2. 提到的**字段数**必须等于 `nas/fpk/board_fields.json` 的实际条目数；
  3. 提到的**包大小**必须等于 `.fpk` 的实际字节数。

用法：python3 tools/plan_check.py
退出码非 0 表示文档里有对不上的地方。
"""

import glob
import io
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# 一起核的文档：方案文档 + 给用户看的 README。两份都会被人当真话读，
# 所以都按同一套规则查（哪份没写某项声明，那项自然就跳过）。
DOCS = [os.path.join(REPO, "docs", "fnos-companion-app-plan.md"),
        os.path.join(REPO, "nas/fpk/README.md")]
FIELD_MANIFEST = os.path.join(REPO, "nas/fpk/board_fields.json")
FPK = os.path.join(REPO, "nas/fpk/nasscreencompanion.fpk")

# 文档里的路径可能相对于这几个根写
ROOTS = [".", "nas/fpk/nasscreencompanion", "nas/fpk/nasscreencompanion/app",
         "nas/fpk", "components/fnos_monitor", "tools", "tools/preview"]
# 不是路径、或本来就是模板/清单写法，跳过
NOT_A_PATH = {
    "connect/timeout", "jal/call", "required/min/max/len/pattern", "{uidir}/config",
    "gatewayPrefix=/app/nasscreencompanion",
    "dirname/rm/kill/echo/mkdir/cat/printf/tr/sleep/python3/touch/nohup/id/grep/cp/command",
}


def brace_glob(pat, root):
    """把 a_{1,2}.png 展开成 a_1.png / a_2.png 再 glob。"""
    m = re.search(r"\{([^{}]+)\}", pat)
    if not m:
        return glob.glob(os.path.join(root, pat))
    out = []
    for alt in m.group(1).split(","):
        out += brace_glob(pat[:m.start()] + alt + pat[m.end():], root)
    return out


def check_doc(doc, problems):
    """核一份文档；problems 是累加的。返回统计用的字符串列表。"""
    notes = []
    if not os.path.exists(doc):
        problems.append(f"找不到文档 {os.path.relpath(doc, REPO)}")
        return notes
    text = io.open(doc, encoding="utf-8").read()
    name = os.path.relpath(doc, REPO)

    # ── 1. 路径
    cands = set()
    for m in re.finditer(r"`([^`\n]+)`", text):
        t = m.group(1).strip()
        if " " in t or t.startswith(("http", "-", "$")) or "/" not in t:
            continue
        if t in NOT_A_PATH or t.endswith("/"):
            continue
        cands.add(t.split()[0].rstrip("：:，,"))
    checked = 0
    for p in sorted(cands):
        if p.startswith("/") or p in NOT_A_PATH:
            continue
        # 文档里常写成 `path:41` 或 `path:12-18`（行号引用）——核对前把行号去掉，
        # 否则每加一处行号引用就会报一次"路径不存在"
        p = re.sub(r":\d+(-\d+)?$", "", p)
        # 反引号里还会写**代码片段**（`get('/api/…')`、`path == "/api/x"`）。
        # 真路径里不会出现引号、括号、省略号，见到这些就当片段、不当路径——
        # 否则每次在文档里举例都会报一次假阳性。
        # 反引号里还会写**代码片段**（`get('/api/…')`）和**占位写法**（`<安装卷>/…`）：
        # 真路径里不会出现引号、括号、省略号、尖括号。
        if any(ch in p for ch in "'\"()…=<>"):
            continue
        # 还有一类是**字段路径**（`trunc.limits/totals/dropped` 这种），不是文件。
        # 判据：最后一段没有点（不是扩展名）**且**整串不以仓库顶层目录开头。
        # 这样 `nas/fpk/build_pkg`（真的写错了）照样会被抓，而字段路径不会误报。
        top = ("nas/", "tools/", "components/", "main/", "docs/", "managed_components/")
        if "." not in p.rsplit("/", 1)[-1] and not p.startswith(top):
            continue
        # 还有一类是**跑在 NAS 上的运行时路径**（`@appdata/info.log`、`target/app.sock`
        # 这种），它们在这份仓库里当然不存在，但写出来是对的。显式列出来，别靠猜。
        if p.startswith(("@appdata/", "@appcenter/", "@appconf/", "target/", "etc/", "var/")):
            continue
        checked += 1
        if any(os.path.exists(os.path.join(r, p)) for r in ROOTS):
            continue
        if any(ch in p for ch in "*{") and any(brace_glob(p, r) for r in ROOTS):
            continue
        problems.append(f"文档引用的路径不存在：{p}")
    notes.append(f"路径：{name} 检查 {checked} 个片段")

    # ── 2. 字段数
    if os.path.exists(FIELD_MANIFEST):
        data = json.load(io.open(FIELD_MANIFEST, encoding="utf-8"))
        # board_fields.json 是 {"note":…, "source":…, "fields":[…]}，不是裸数组
        fields = data.get("fields", data) if isinstance(data, dict) else data
        n = len(fields)
        claimed = {int(x) for x in re.findall(r"(\d+)\s*个字段", text)}
        for c in sorted(claimed):
            if c != n:
                problems.append(f"文档写「{c} 个字段」，board_fields.json 实际是 {n}")
        notes.append(f"字段数：{name} 提到 {sorted(claimed) or '（没提）'}，实际 {n}")

    # ── 3. 包大小
    if os.path.exists(FPK):
        size = os.path.getsize(FPK)
        claimed = {int(x) for x in re.findall(r"（(\d{5,7})\s*字节）", text)}
        for c in sorted(claimed):
            if c != size:
                problems.append(f"文档写「{c} 字节」，.fpk 实际是 {size} 字节")
        notes.append(f"包大小：{name} 提到 {sorted(claimed) or '（没提）'}，实际 {size}")

    # ── 4. 断言条数必须和测试报告里那次真实运行一致
    #      （报告是 tools/test_report.sh 跑出来的，里面是当次的真实结果）
    report = os.path.join(REPO, "docs", "fnos-companion-test-report.md")
    if True:
        rtext = io.open(report, encoding="utf-8").read()
        m = re.search(r"(\d+)\s*项通过，\s*(\d+)\s*项失败", rtext)
        if m:
            real, failed = int(m.group(1)), int(m.group(2))
            claimed = set()
            for pat in (r"本地端到端测试\s*(\d+)\s*项", r"走完整生命周期（(\d+)\s*项）",
                        r"本地端到端断言\s*(\d+)\s*项"):
                claimed |= {int(x) for x in re.findall(pat, text)}
            for c in sorted(claimed):
                if c != real:
                    problems.append(f"文档写「{c} 项」断言，测试报告里当次实际是 {real} 项")
            notes.append(f"断言条数：{name} 提到 {sorted(claimed) or '（没提）'}，报告 {real} 项通过/{failed} 项失败")
            if failed:
                problems.append(f"测试报告显示有 {failed} 项失败——先把它跑绿再谈文档")

    # ── 4b. 目录里的链接必须真的有对应标题（目录最容易悄悄烂掉）
    heads = re.findall(r"^#{2,3}\s+(.+?)\s*$", text, re.M)
    def slug(t):
        a = t.lower()
        a = re.sub(r'[`*：:，,。.（）()“”"\'？?！!、/]', '', a)
        return a.replace(' ', '-')
    have = {slug(h) for h in heads}
    for label, target in re.findall(r"\[([^\]]+)\]\(#([^)]+)\)", text):
        if target not in have:
            problems.append(f"目录里的链接 [{label}](#{target}) 没有对应的标题"
                            f"—— 标题改了名、目录没跟着改")
    n_toc = len(re.findall(r"\[[^\]]+\]\(#[^)]+\)", text))
    if n_toc:
        notes.append(f"目录链接：{n_toc} 条")

    # ── 4c. 说明书里「引号引起来的报错原文」，必须真的能在代码里找到
    # 这一节是**用户唯一的查找键**：他是在看不到管理页的时候来翻它的。
    # 报错文案一改、说明书没跟着改，这一节就正好在最需要的时候失效。
    # 代码里的文案带变量插值（$DEV_AGENT_PORT / ${INFO_LOG} / $port），所以按
    # 「去掉数字与省略号后的片段」比对，而不是整串字面量。
    if os.path.basename(doc) == "README.md":
        srcs = []
        for root in ("nas/fpk/nasscreencompanion/cmd", "nas/fpk/nasscreencompanion/app/server"):
            for dp, _, fns in os.walk(os.path.join(REPO, root)):
                for fn in fns:
                    if fn.endswith((".sh", ".py")) or "." not in fn:
                        srcs.append(io.open(os.path.join(dp, fn), encoding="utf-8",
                                            errors="replace").read())
        blob = "\n".join(srcs)
        blob_norm = re.sub(r"[0-9]+", "", blob)
        quoted = re.findall(r"「([^」]{8,})」", text)
        checked = 0
        for q in quoted:
            plain = re.sub(r"[*`]", "", q)
            # 按数字/省略号切成片段，每段都要在同一份源码里出现
            parts = [x for x in re.split(r"[0-9]+|…|\.\.\.", plain) if len(x.strip()) >= 6]
            if not parts:
                continue
            checked += 1
            for part in parts:
                if re.sub(r"[0-9]+", "", part) not in blob_norm:
                    problems.append(f"说明书引用的报错原文对不上代码：「{part.strip()}」"
                                    f"（完整引用：{plain[:40]}…）—— 用户会拿这句话去查，"
                                    f"文案改了说明书就得跟着改")
                    break
        if checked:
            notes.append(f"说明书引用的报错原文：{checked} 条")

    # ── 5. 提到的工具脚本必须存在
    #      \b 不能省：少了它 `board_fields.json` 会被 `js` 这个分支截成 `board_fields.js`
    tool_pat = re.compile(r"((?:tools|nas/fpk)/[\w./-]+\.(?:py|sh|js)\b)")
    for tool in sorted(set(tool_pat.findall(text))):
        if not os.path.exists(os.path.join(REPO, tool)):
            problems.append(f"文档提到的工具不存在：{tool}")

    return notes


def main():
    problems = []
    notes = []
    for doc in DOCS:
        notes += check_doc(doc, problems)
    for n in notes:
        print(n)

    if problems:
        print("\n对不上的地方：")
        for p in problems:
            print("  ✗", p)
        return 1
    print("\n被核的文档里，路径、字段数、包大小、断言条数都与仓库现状一致 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
