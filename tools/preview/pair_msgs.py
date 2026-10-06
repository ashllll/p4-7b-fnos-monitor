#!/usr/bin/env python3
"""把设备端**可能显示给用户的每一条配对消息**抠出来，生成一副 C 数组。

为什么：`fnos_pair.c` 里 `set_view(FNOS_PAIR_*, "…")` 的文案就是配对卡上那行字。
可预览的桩件只会走到 UNPROVISIONED / CONFIRM / PROVISIONED 三个状态——
**FAILED 那条分支（七条文案，全是坏事发生时用户看到的话）从来没被渲染过**，
包装标签会不会截断、会不会压到别的构件，谁也不知道。

顺带记一条同源的教训（真机上已经吃过一次）：**主机预览的桩件必须与设备行为一致，
否则预览的绿灯是假的**。当时桩件把 `v.msg` 固定成空串，而设备 `fnos_pair_init()`
会写「未配对：用编译期默认地址」，于是"状态说明压在三步指引上"这个重叠 bug
只在真机照片上才暴露。

用法：python3 tools/preview/pair_msgs.py <fnos_pair.c> <输出 .h>
"""
import io
import re
import sys

STATES = ("UNPROVISIONED", "FETCHING", "CONFIRM", "PAIRING", "PROVISIONED", "FAILED")


def main():
    src_path, out_path = sys.argv[1], sys.argv[2]
    src = io.open(src_path, encoding="utf-8").read()
    pairs = re.findall(r'set_view\(\s*FNOS_PAIR_(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"', src)
    seen, rows = set(), []
    for st, msg in pairs:
        if st not in STATES:
            print(f"✗ 出现了没见过的状态 FNOS_PAIR_{st}，pair_msgs.py 要跟着改")
            return 1
        if (st, msg) in seen:
            continue
        seen.add((st, msg))
        rows.append((st, msg))
    if not rows:
        print("✗ 一条 set_view 消息都没抠到 —— 提取规则和源码对不上了")
        return 1
    body = "\n".join('    {FNOS_PAIR_%s, "%s"},' % (s, m.replace('"', '\\"'))
                     for s, m in rows)
    io.open(out_path, "w", encoding="utf-8").write(
        "/* 由 tools/preview/pair_msgs.py 从 components/fnos_monitor/fnos_pair.c 生成，不要手改。\n"
        " * 设备端每一条会显示给用户的配对消息（%d 条）。 */\n"
        "#define PAIR_MSG_COUNT %d\n"
        "static const struct { int state; const char *msg; } PAIR_MSGS[PAIR_MSG_COUNT] = {\n%s\n};\n"
        % (len(rows), len(rows), body))
    by_state = {}
    for s, _ in rows:
        by_state[s] = by_state.get(s, 0) + 1
    print("抠出 %d 条配对消息：%s" % (len(rows),
          "、".join("%s×%d" % (k, v) for k, v in sorted(by_state.items()))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
