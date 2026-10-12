#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""配网原因串的容量与 UTF-8 截断回归（审计 M10）。

背景（复核意见原文）：`reason_text()` 新增的两条原因分别占 76 / 96 个 UTF-8 字节，
而接收缓存 `s_reason` 只有 72 字节 —— 按 `snprintf` 的 71 字节有效容量，两条都截断在
多字节字符中间，产出**非法 UTF-8**（字库拿不到完整码点，那一行整条不显示），
同时把解释内容丢掉一半。修法是"放大容量 + 只在字符边界截断"，本脚本把三件事钉住：

1. 容量：`reason_text()` 里**每一条**字面量都得装进 `WIFI_REASON_CAP - 1`；
2. 行为：把 `fnos_net.c` 里标记出的 `fnos_utf8_copy()` 原样抠出来编到主机上，
   验证边界/截断/空串/NULL 永远给出合法 UTF-8 与 NUL 结尾；
3. 反证：同一组输入喂给"按字节截断"的老做法必须产出非法 UTF-8 —— 否则这条
   检查根本区分不出好坏（老基线 `53e1bf8` 的 72 字节缓存就是当年那个做法）。

只读源码 + 在临时目录里编译一个主机小程序，不碰设备、不写仓库。
"""

import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NET_C = os.path.join(REPO, "components", "fnos_monitor", "fnos_net.c")
UI_C = os.path.join(REPO, "components", "fnos_monitor", "fnos_ui.c")
BASELINE = "53e1bf8"

fails = []
oks = []


def ok(msg):
    oks.append(msg)
    print("  ✓ %s" % msg)


def bad(msg):
    fails.append(msg)
    print("  ✗ %s" % msg)


def read(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def source_at(rev, path):
    """取某个历史版本的源码（负数控制用）；取不到就返回 None。"""
    try:
        out = subprocess.run(["git", "show", "%s:%s" % (rev, path)],
                             cwd=REPO, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, check=True)
        return out.stdout.decode("utf-8")
    except Exception:
        return None


def extract_block(src, begin, end):
    """抠出 begin 标记整行起、到 end 标记之前的源码。

    切片必须从 begin 标记的注释开头算起：标记本身就是 `/* … */` 注释，
    若把 `/*` 吃掉，剩下的中文会变成非法 C 记号（第一版就是这样编译失败的）。
    """
    i = src.find(begin)
    j = src.find(end)
    if i < 0 or j < 0 or j <= i:
        return None
    return src[i:j]


REASON_DEF = "static const char *reason_text(uint8_t r)"


def reason_literals(src):
    """reason_text() 里 `return "…";` 的全部字面量。

    必须锚在**函数定义**上：`reason_text(` 第一次出现是在文件靠上的注释里
    （"见 reason_text() 里最长的一句"），从那里往下的切片会一直吃到下一个
    顶层 `}`，一个字面量都取不到（第一版就栽在这儿）。
    """
    i = src.find(REASON_DEF)
    if i < 0:
        return []
    # 函数体到下一个顶层 } 为止（够用：里面没有嵌套函数）
    j = src.find("\n}\n", i)
    body = src[i:j if j > 0 else len(src)]
    return re.findall(r'return\s+"([^"]*)"', body)


def cap_of(src):
    m = re.search(r"#define\s+WIFI_REASON_CAP\s+(\d+)", src)
    return int(m.group(1)) if m else None


def main():
    fails.clear()
    oks.clear()
    net = read(NET_C)
    ui = read(UI_C)

    # ── 1. 容量：每条原因都必须装得下 ─────────────────────────────────
    cap = cap_of(net)
    if cap is None:
        bad("fnos_net.c 里找不到 #define WIFI_REASON_CAP")
        return 1
    lits = reason_literals(net)
    if len(lits) < 3:
        bad("reason_text() 里只解析到 %d 条字面量，解析规则可能过期了" % len(lits))
        return 1
    longest = max(lits, key=lambda s: len(s.encode("utf-8")))
    lmax = len(longest.encode("utf-8"))
    if lmax + 1 <= cap:
        ok("WIFI_REASON_CAP=%d 装得下最长的原因（%d 字节）：%s" % (cap, lmax, longest))
    else:
        bad("最长的原因 %d 字节 + NUL 超过 WIFI_REASON_CAP=%d：%s" % (lmax, cap, longest))
    per = [s for s in lits if len(s.encode("utf-8")) + 1 > cap]
    if per:
        bad("有 %d 条原因装不进缓存：%s" % (len(per), per))
    else:
        ok("全部 %d 条原因（含新增的加密方式/信号两条）都在容量内" % len(lits))

    # ── 2. 显示链路上游的缓冲不能比原因缓存更小 ───────────────────────
    for name, pat in (("fnos_ui.c 的状态串缓冲 st", r"char\s+st\[(\d+)\]"),
                      ("fnos_ui.c 的提交原因缓冲 sr", r"char\s+sr\[(\d+)\]")):
        m = re.search(pat, ui)
        if not m:
            bad("%s 找不到（界面改动后请同步本检查）" % name)
        elif int(m.group(1)) >= cap:
            ok("%s = %s 字节 ≥ WIFI_REASON_CAP" % (name, m.group(1)))
        else:
            bad("%s = %s 字节 < WIFI_REASON_CAP=%d —— 成型好的原因又会被界面截断"
                % (name, m.group(1), cap))
    m = re.search(r"#define\s+WIFI_STATE_STR_MAX\s+(\d+)", net)
    if m and int(m.group(1)) > cap:
        ok("WIFI_STATE_STR_MAX=%s > WIFI_REASON_CAP（整串成型缓冲留了余量）" % m.group(1))
    else:
        bad("WIFI_STATE_STR_MAX 缺失或不大于 WIFI_REASON_CAP（整串会被硬截断）")

    # ── 3. 行为：把被标记的 fnos_utf8_copy() 抠出来编到主机上 ─────────
    block = extract_block(net, "/* WIFI_REASON_UTF8_BEGIN", "/* WIFI_REASON_UTF8_END")
    if not block or "fnos_utf8_copy" not in block:
        bad("fnos_net.c 里找不到 WIFI_REASON_UTF8_BEGIN/END 标记块")
        return 1
    ok("抠出 fnos_utf8_copy() 实现（%d 行）" % len(block.strip().splitlines()))

    harness = r'''
#include <stdio.h>
#include <string.h>
#include <stddef.h>
/*@@BLOCK@@*/

static int utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n;
        if (*p < 0x80) n = 1;
        else if ((*p & 0xE0) == 0xC0) n = 2;
        else if ((*p & 0xF0) == 0xE0) n = 3;
        else if ((*p & 0xF8) == 0xF0) n = 4;
        else return 0;
        if (n == 1) { p++; continue; }
        for (int i = 1; i < n; i++) {
            if ((p[i] & 0xC0) != 0x80) return 0;   /* 缺续字节 = 截断在字符中间 */
        }
        p += n;
    }
    return 1;
}

static int fails;

/* 老做法（负数控制）：按字节截断，和当年 snprintf 到 72 字节缓存等价 */
static void naive_copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void check(int cond, const char *what)
{
    if (!cond) { printf("FAIL %s\n", what); fails++; }
    else       printf("ok   %s\n", what);
}

int main(void)
{
    /* 复核意见里点名的两条真实原因文本（与 fnos_net.c 保持一致） */
    const char *a = "找到了网络但加密方式不合适（路由器是混合/旧加密？）";
    const char *b = "这个网络的加密方式不被接受（当前只收 WPA2 及以上，开放网络连不上）";

    char small[72], big[128], tiny[1], one[8];

    /* 老做法在 72 字节缓存上必须产出非法 UTF-8 —— 证明这条检查抓得住回归 */
    naive_copy(small, sizeof small, a);
    check(!utf8_valid(small), "负向控制：按字节截断 72 字节缓存确实产出非法 UTF-8（旧行为）");
    naive_copy(small, sizeof small, b);
    check(!utf8_valid(small), "负向控制：更长的原因在 72 字节缓存上同样非法");

    /* 新做法：任何容量下都必须是合法 UTF-8 且 NUL 结尾 */
    memset(big, 0x7f, sizeof big);
    fnos_utf8_copy(big, sizeof big, b);
    check(utf8_valid(big), "整串装得下时原样拷贝且合法");
    check(strcmp(big, b) == 0, "cap 足够时不丢内容");

    memset(small, 0x7f, sizeof small);
    fnos_utf8_copy(small, sizeof small, b);
    check(utf8_valid(small), "72 字节容量下截断仍合法（不会切在汉字中间）");
    check(strlen(small) + 3 > 0 && strlen(small) <= 71, "截断长度不超过 cap-1");
    /* 截断结果必须是原串的前缀：只能"少拷贝尾巴"，不能丢中间或改写内容。
       注意最后一个字节**允许**是续字节 —— 只要那个字完整落进来了就合法，
       真正的判据是上面的 utf8_valid（老做法正是在这里失败）。 */
    check(strncmp(small, b, strlen(small)) == 0, "截断结果仍是原串的前缀（没丢中间）");

    memset(one, 0x7f, sizeof one);
    fnos_utf8_copy(one, 1, b);
    check(one[0] == 0, "cap=1 时只写 NUL（不越界）");

    memset(tiny, 0x7f, sizeof tiny);
    fnos_utf8_copy(tiny, sizeof tiny, a);
    check(tiny[0] == 0, "1 字节缓冲也不越界");

    fnos_utf8_copy(big, sizeof big, NULL);
    check(big[0] == 0, "NULL 源得到空串");

    fnos_utf8_copy(big, sizeof big, "");
    check(big[0] == 0, "空串得到空串");

    fnos_utf8_copy(NULL, 8, a);
    check(1, "NULL 目标不崩（只返回 0）");

    /* 逐字节容量扫描：任何容量都不能产出非法 UTF-8 */
    int all_ok = 1;
    for (size_t cap = 1; cap <= sizeof big; cap++) {
        char buf[128];
        memset(buf, 0x7f, sizeof buf);
        fnos_utf8_copy(buf, cap, b);
        if (!utf8_valid(buf) || strlen(buf) > cap - 1) { all_ok = 0; printf("  cap=%zu 失败\n", cap); }
    }
    check(all_ok, "1..128 每一种容量都合法且不越界");

    /* 返回值 = 实际写入字节数 */
    check(fnos_utf8_copy(big, sizeof big, "abc") == 3, "返回值是写入字节数");

    if (fails) { printf("FAILS %d\n", fails); return 1; }
    printf("ALL OK\n");
    return 0;
}
'''
    harness = harness.replace("/*@@BLOCK@@*/", block)

    with tempfile.TemporaryDirectory(prefix="wifi-reason-") as tmp:
        csrc = os.path.join(tmp, "t.c")
        with open(csrc, "w", encoding="utf-8") as f:
            f.write(harness)
        exe = os.path.join(tmp, "t")
        cc = os.environ.get("CC", "cc")
        built = subprocess.run([cc, "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter",
                                "-o", exe, csrc],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        if built.returncode != 0:
            bad("主机编译 fnos_utf8_copy 失败：%s" % built.stdout.decode("utf-8", "replace")[-800:])
            return 1
        ok("主机编译通过（cc=%s）" % cc)
        run = subprocess.run([exe], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        out = run.stdout.decode("utf-8", "replace")
        for line in out.splitlines():
            if line.startswith("ok "):
                ok(line[3:])
            elif line.startswith("FAIL "):
                bad(line[5:])
        if run.returncode != 0 or "FAIL" in out or "ALL OK" not in out:
            bad("主机行为用例有失败项")

    # ── 4. 历史对照：老容量装不下本轮新增的原因 ───────────────────────
    # 说明：基线 53e1bf8 的 s_reason 就是 72 字节，但那时 reason_text() 最长只有
    # 57 字节，所以基线本身不截断 —— 截断是"本轮往 72 字节缓存里加了两条 76/96
    # 字节的原因"引入的。真正该钉住的是：**当前内容**装不进**老容量**，且老代码
    # 没有按字符边界截断的能力。行为层面的负数控制由上面 C 用例里的 naive_copy 给出。
    base = source_at(BASELINE, "components/fnos_monitor/fnos_net.c")
    if base is None:
        print("  ! 取不到基线 %s 的源码（浅克隆？）—— 跳过历史对照" % BASELINE)
    else:
        m = re.search(r"static\s+char\s+s_reason\[(\d+)\]", base)
        bc = int(m.group(1)) if m else None
        blits = reason_literals(base)
        blong = max((len(s.encode("utf-8")) for s in blits), default=0)
        if bc == 72:
            ok("历史对照：基线 %s 的原因缓存就是 s_reason[72]" % BASELINE)
        else:
            bad("历史对照对不上：基线 s_reason=%s（预期 72）" % bc)
        if bc and lmax + 1 > bc:
            ok("历史对照：当前最长原因 %d 字节 + NUL > 老容量 %d → 沿用 72 字节必然截断"
               % (lmax, bc))
        else:
            bad("历史对照失效：当前最长原因 %d 字节居然装得进老容量 %s" % (lmax, bc))
        print("  · 基线 reason_text() 最长字面量 %d 字节（%d 条）—— 基线自身不截断，"
              "截断是本轮新增那两条时引入的" % (blong, len(blits)))
        if "fnos_utf8_copy" not in base:
            ok("历史对照：基线还没有 fnos_utf8_copy（按字符边界截断是本轮的修法）")
        else:
            bad("基线里已经有 fnos_utf8_copy 了 —— 负数控制的假设不成立")

    print("\n%s" % ("PASS: 配网原因串容量足够，UTF-8 截断只在字符边界发生"
                    if not fails else "FAIL: 配网原因串容量/UTF-8 截断仍有问题"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
