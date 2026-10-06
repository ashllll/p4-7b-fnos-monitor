#!/usr/bin/env python3
"""本地长测（soak）到底还在不在跑、跑的是不是当前代码。

为什么需要它：方案文档的状态行里写着「P3 的本地可做部分……服务侧长跑……已完成」，
而在此之前，"长跑是好的"这件事一直靠我每轮**肉眼看一眼日志尾部**。第 18 轮就吃过
一次亏：那个跑了十几个小时的长测实例，跑的其实是一份**被破坏过的副本**（做证伪时
临时把引导守卫改成 `return False`，还原了源码，但 `/tmp/nsc-soak/` 里的拷贝没还原）。
日志每一行都漂漂亮亮地写着「管理面=200 TLS=200 明文引导=200」——**它只是一直在测
旧的、坏的那份代码**。

所以这个脚本查四件事：
  1. 长测在不在跑（没跑就如实说"未在运行"，不算失败——它不是交付物）
  2. **跑的是不是当前代码**（暂存副本与源码逐字节比对；不一致就是第 18 轮那个坑）
  3. 最近的采样里两面是否都还答得上来
  4. 句柄有没有涨、内存有没有爬（判据与 mem_check.py 同源）

用法：python3 tools/soak_check.py [--log /tmp/nsc-soak.log] [--stage /tmp/nsc-soak]
退出码非 0 表示长测本身有问题（不是在跑）。
"""

import io
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_LOG = "/tmp/nsc-soak.log"
DEFAULT_STAGE = "/tmp/nsc-soak"

# 暂存副本里这些文件必须与源码一致（只比会真正跑起来的那几个）
TRACKED = [
    ("target/server/nas_companion_server.py",
     "nas/fpk/nasscreencompanion/app/server/nas_companion_server.py"),
    ("target/server/fnos_collector.py",
     "nas/fpk/nasscreencompanion/app/server/fnos_collector.py"),
]

SAMPLE = re.compile(
    r"#(\d+)\s+rss=(\d+)KB\s+fd=(\d+).*?管理面=(\d+)\s+TLS=(\d+)\s+明文引导=(\d+)")


def main():
    args = sys.argv[1:]
    log = args[args.index("--log") + 1] if "--log" in args else DEFAULT_LOG
    stage = args[args.index("--stage") + 1] if "--stage" in args else DEFAULT_STAGE

    if not os.path.exists(log):
        print(f"长测未在运行（没有 {log}）——这不是失败项，"
              f"但它意味着「服务侧长跑」这一条现在没有证据支撑")
        return 0

    rows = []
    for line in io.open(log, encoding="utf-8", errors="replace"):
        m = SAMPLE.search(line)
        if m:
            rows.append(tuple(int(x) for x in m.groups()))
    if not rows:
        print(f"✗ {log} 里没有一条完整采样（服务从来没起来过？）")
        return 1

    print(f"采样 {rows[-1][0]} 次，最新一次 #{rows[-1][0]}："
          f"rss={rows[-1][1]}KB fd={rows[-1][2]}")
    problems = []

    # ── 1. 跑的是不是当前代码
    for rel_stage, rel_src in TRACKED:
        sp = os.path.join(stage, rel_stage)
        src = os.path.join(REPO, rel_src)
        if not os.path.exists(sp) or not os.path.exists(src):
            continue
        a = io.open(sp, "rb").read()
        b = io.open(src, "rb").read()
        if a != b:
            print(f"  ✗ {rel_stage} 与源码**不一致**（{len(a)} vs {len(b)} 字节）"
                  f"—— 这些采样测的是旧代码")
            problems.append("stale")

    # ── 2. 最近三次两面都要答得上来
    for n, rss, fd, mg, tls, plain in rows[-3:]:
        if mg != 200 or tls != 200 or plain != 200:
            problems.append(f"#{n} 管理面={mg} TLS={tls} 明文引导={plain}")
    if not problems:
        print("  ✓ 最近三次采样：管理面 / TLS / 明文引导 都是 200")

    # ── 3. 句柄不许涨
    fds = [r[2] for r in rows[-6:]]
    if len(fds) >= 3 and min(fds) != max(fds):
        problems.append(f"句柄数在动：{fds}")
    elif len(fds) >= 3:
        print(f"  ✓ 句柄稳定在 {fds[0]}")

    # ── 4. 内存要平台化
    # **判据看"尾巴平不平"，不是"前后两段平均值差多少"。**
    # 第一版比的是"中间 1/3 vs 最后 1/3"的平均值，结果服务在某个时刻一次性涨了
    # 约 700KB（换了代码路径），后面 24 次采样**一动不动地停在 37168KB**——
    # 而那两段的平均差是 +516KB，于是被判成"内存还在爬"。
    # **一次性的台阶和缓慢泄漏，隔着老远比平均值是分不出来的**；能分开的是：
    # 泄漏的尾巴会一直往上走，台阶之后是平的。
    if len(rows) >= 10:
        tail = [r[1] for r in rows[-10:]]
        spread = max(tail) - min(tail)
        if spread > 512:                     # KB，和 mem_check 的判据同量级
            problems.append(f"内存尾巴不平：最近 10 次采样在 "
                            f"{min(tail)}~{max(tail)}KB 之间起伏（{spread}KB）")
        else:
            print(f"  ✓ 内存尾巴是平的（最近 10 次 {min(tail)}~{max(tail)}KB，"
                  f"起伏 {spread}KB；全程 {min(r[1] for r in rows)}~{max(r[1] for r in rows)}KB）")

    if problems:
        print("\n长测有问题：")
        for p in problems:
            print("  ✗", p)
        if "stale" in problems:
            print("    暂存副本是旧的——重启长测（先 pkill -f soak.sh，再跑 "
                  "bash /tmp/soak.sh）后，之前的采样才作数")
        return 1
    print("\n长测在跑、跑的是当前代码、两面都健康 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
