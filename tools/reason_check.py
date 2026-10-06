#!/usr/bin/env python3
"""连接失败「原因标签」的跨文件契约：产出的每一个都被翻译成人话了吗？

背景：板子连不上 NAS 的原因差别很大——网线没插和"NAS 换过证书"要做的处理完全不同。
所以 `fnos_data.c` 在失败时写一个**短标签**（`tls handshake`、`no cert`…），
`fnos_ui.c` 的 `link_reason()` 再把它翻成用户能照着处理的一句话，显示在总览页健康卡
右侧。**这两个列表分居两个文件，谁改了另一边不知道。**

漂移的后果很隐蔽：`link_reason()` 遇到不认识的标签返回 NULL，于是那行字**整条消失**
——界面看起来只是"没写原因"，用户不知道该干什么，而没有任何断言会红。

检查两件事：
  · 产出 ⊆ 已翻译   —— 否则某个失败场景下用户看不到原因
  · 已翻译 ⊆ 产出   —— 否则有翻译不了的分支（死代码，或者标签拼错了）

做法是从两边**现抓**，不手抄清单（手抄的清单一定会过期）：
  · 产出方（`components/fnos_monitor/fnos_data.c`）：`classify_conn_err()` 的返回值、
    `snprintf(s_conn_err, …, "X")`、`snprintf(s_status.last_err, …, "X")`、
    以及 `poll_task` 里那个 `const char *why = …` 表达式里的字面量。
  · 翻译方（`components/fnos_monitor/fnos_ui.c`）：`link_reason()` 里
    `strcmp(st->last_err, "X")` 的字面量。

用法：python3 tools/reason_check.py
退出码非 0 表示两边对不上。
"""

import io
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_C = os.path.join(REPO, "components/fnos_monitor/fnos_data.c")
UI_C = os.path.join(REPO, "components/fnos_monitor/fnos_ui.c")

# 标签的形状：小写字母开头、可以带一个空格或斜杠（"tls handshake"、"connect/timeout"）
TAG = r'"([a-z][a-z0-9]*(?:[ /][a-z0-9]+)*)"'


def block(text, start_pat):
    """取出从 start_pat 匹配处开始、到配对花括号结束的那一段。"""
    m = re.search(start_pat, text)
    if not m:
        return None
    i = text.index("{", m.end() - 1) if "{" not in m.group(0) else text.index("{", m.start())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return None


def produced(text):
    tags = set()
    fn = block(text, r"static const char \*classify_conn_err\(")
    if fn:
        tags |= set(re.findall(TAG, fn))
    # 连接层的失败通道：http_get() 往 s_conn_err 里写的字面量
    for m in re.finditer(r"snprintf\(\s*s_conn_err\s*,[^;]*?;\s*", text, re.S):
        tags |= set(re.findall(TAG, m.group(0)))
    # "no wifi" 在 mark_link_down() 里直接写进 last_err
    # （**不**用"凡是 snprintf 到 last_err 的都算"这条泛规则：fnos_data_start() 会写一个
    #  非失败的 "starting"，那是启动占位、不是失败标签，泛规则会把它误报成漏翻译）
    fn2 = block(text, r"static void mark_link_down\(void\)")
    if fn2:
        tags |= set(re.findall(TAG, fn2))
    # poll_task 里那个三元表达式
    m = re.search(r"const char \*why\s*=\s*(.*?);\n", text, re.S)
    if m:
        tags |= set(re.findall(TAG, m.group(1)))
    return tags


def translated(text):
    fn = block(text, r"static const char \*link_reason\(")
    return set(re.findall(r'strcmp\(\s*st->last_err\s*,\s*' + TAG + r"\s*\)", fn)) if fn else set()


def main():
    for p in (DATA_C, UI_C):
        if not os.path.exists(p):
            print(f"找不到 {os.path.relpath(p, REPO)}")
            return 2
    prod = produced(io.open(DATA_C, encoding="utf-8").read())
    tran = translated(io.open(UI_C, encoding="utf-8").read())

    print(f"fnos_data.c 会写出的原因标签（{len(prod)} 个）：{sorted(prod)}")
    print(f"fnos_ui.c   link_reason 认得（{len(tran)} 个）：{sorted(tran)}")

    problems = []
    for t in sorted(prod - tran):
        problems.append(f"`{t}` 会被写进 last_err，但 link_reason() 不认得它 —— "
                        f"那种失败下总览页那行字会整个消失，用户看不到原因")
    for t in sorted(tran - prod):
        problems.append(f"`{t}` link_reason() 有翻译，但 fnos_data.c 从不产出它 —— "
                        f"要么是死分支，要么是标签拼错了")

    if problems:
        print("\n两边对不上：")
        for p in problems:
            print("  ✗", p)
        return 1
    print("\n每个失败标签都有对应的人话，每句人话也都有来源 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
