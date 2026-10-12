#!/usr/bin/env python3
"""本地长测（soak）日志样本及源码副本核对。

为什么需要它：方案文档的状态行里写着「P3 的本地可做部分……服务侧长跑……已完成」，
而在此之前，"长跑是好的"这件事一直靠我每轮**肉眼看一眼日志尾部**。第 18 轮就吃过
一次亏：那个跑了十几个小时的长测实例，跑的其实是一份**被破坏过的副本**（做证伪时
临时把引导守卫改成 `return False`，还原了源码，但 `/tmp/nsc-soak/` 里的拷贝没还原）。
日志每一行都漂漂亮亮地写着「管理面=200 TLS=200 明文引导=200」——**它只是一直在测
旧的、坏的那份代码**。

所以这个脚本查四件事：
  1. 日志是否存在且时间新鲜（缺失、过期或未来时间均未判定）
  2. **跑的是不是当前代码**（暂存副本与源码逐字节比对；不一致就是第 18 轮那个坑）
  3. 最近的采样里两面是否都还答得上来
  4. 句柄有没有涨、内存有没有爬（判据与 mem_check.py 同源）

用法：python3 tools/soak_check.py [--log /tmp/nsc-soak.log] [--stage /tmp/nsc-soak]
退出码：0=新鲜日志样本及源码副本一致，1=证据有问题，2=缺失/过期/未来日志未判定。
日志mtime不能证明进程仍在运行，也不证明72小时或设备验收。
"""

import io
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_LOG = "/tmp/nsc-soak.log"
DEFAULT_STAGE = "/tmp/nsc-soak"

# 采样每60秒一条；mtime只用于拒绝过期证据，不能证明进程仍在运行。
# 超过这个时长不再接受日志。**以前只判断"日志文件在不在"**：本机那份 5 天前的旧日志
# 会被当成"长测在跑"，于是脚本拿一份早就停了的采样去证明"当前代码的健康"——
# 与第 18 轮那个坑同一类（看着像证据，其实证明的是别的东西）。
FRESH_S = 900

# 核对服务端全部Python模块，避免只核对入口而漏掉docker_api等实际导入模块。
SERVER_SRC = "nas/fpk/nasscreencompanion/app/server"

def tracked_sources(stage):
    current = os.path.join(REPO, SERVER_SRC)
    staged = os.path.join(stage, "target/server")
    names = set()
    for base in (current, staged):
        if not os.path.isdir(base):
            raise FileNotFoundError("缺少服务端源码目录")
        def walk_error(error):
            raise error
        for directory, dirs, files in os.walk(base, onerror=walk_error):
            dirs[:] = [d for d in dirs if d != "__pycache__"]
            names.update(os.path.relpath(os.path.join(directory, f), base)
                         for f in files if f.endswith(".py"))
    if not names:
        raise ValueError("没有服务端Python模块，代码身份未判定")
    return [(os.path.join("target/server", name), os.path.join(SERVER_SRC, name))
            for name in sorted(names)]

SAMPLE = re.compile(
    r"#(\d+)\s+rss=(\d+)KB\s+fd=(\d+).*?管理面=(\d+)\s+TLS=(\d+)\s+明文引导=(\d+)")


def main():
    args = sys.argv[1:]
    log = args[args.index("--log") + 1] if "--log" in args else DEFAULT_LOG
    stage = args[args.index("--stage") + 1] if "--stage" in args else DEFAULT_STAGE

    if not os.path.exists(log):
        print(f"长测日志缺失（没有 {log}）——这是**未判定**，不是失败项，"
              f"但它意味着「服务侧长跑」这一条现在没有证据支撑")
        return 2

    rows = []
    with io.open(log, encoding="utf-8", errors="replace") as stream:
        for line in stream:
            m = SAMPLE.search(line)
            if m:
                rows.append(tuple(int(x) for x in m.groups()))

    # 日志已过期，不能据此判断当前运行状态。
    # 这时既不能打 ✓（没证据），也不该算 ✗（它不是本轮缺陷）——退出码 2 让上层
    # 写成"未判定"，并且**不**把本轮报告染红。
    age_s = time.time() - os.path.getmtime(log)
    if age_s > FRESH_S:
        print(f"长测日志过期（{log} 最后写入于 {age_s / 60:.0f} 分钟前，"
              f"超过 {FRESH_S // 60} 分钟没动静）——**未判定**，不算失败。")
        print(f"  这份日志里有 {len(rows)} 条采样，但不能确认它们属于本次候选，"
              f"不能当「当前服务侧长跑健康」的证据。")
        print("  要判定就重启长测（bash /tmp/soak.sh），采样起来后再跑本脚本。")
        return 2

    if age_s < 0:
        print("日志mtime在未来，采样新鲜度未判定")
        return 2

    if not rows:
        print(f"✗ {log} 是新的、但里面没有一条完整采样（服务从来没起来过？）")
        return 1

    print(f"采样 {rows[-1][0]} 次，最新一次 #{rows[-1][0]}："
          f"rss={rows[-1][1]}KB fd={rows[-1][2]}")
    problems = []

    # ── 1. 跑的是不是当前代码
    try:
        tracked = tracked_sources(stage)
    except (OSError, ValueError) as error:
        print(f"源码身份未判定：{error}")
        return 1
    for rel_stage, rel_src in tracked:
        sp = os.path.join(stage, rel_stage)
        src = os.path.join(REPO, rel_src)
        if not os.path.exists(sp) or not os.path.exists(src):
            # 以前这里 continue：文件缺了就当"这项没事"，可缺的正是"跑的是不是当前
            # 代码"的证据本身（审计 M50）。缺文件 = 无法证明，不能算过。
            problems.append(f"缺文件：暂存副本 {rel_stage} 或源码 {rel_src} 不存在"
                            f"—— 这条采样跑的是哪份代码无从证明")
            continue
        with io.open(sp, "rb") as stream:
            a = stream.read()
        with io.open(src, "rb") as stream:
            b = stream.read()
        if a != b:
            print(f"  ✗ {rel_stage} 与源码**不一致**（{len(a)} vs {len(b)} 字节）"
                  f"—— 这些采样测的是旧代码")
            problems.append("stale")

    if any(row[0] <= 0 or row[1] <= 0 or row[2] <= 0 for row in rows) or \
            any(a[0] >= b[0] for a, b in zip(rows, rows[1:])):
        problems.append("采样编号非递增/重复，或资源计数无效")

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
    else:
        # 窗口不够时以前是**静默跳过**，末尾照样打印"长测健康 ✓"（审计 M50）
        problems.append(f"句柄样本只有 {len(fds)} 次（<3）：句柄趋势未判定")

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
    else:
        problems.append(f"内存样本只有 {len(rows)} 次（<10）：内存趋势未判定")

    if problems:
        print("\n长测有问题：")
        for p in problems:
            print("  ✗", p)
        if "stale" in problems:
            print("    暂存副本是旧的——重启长测（先 pkill -f soak.sh，再跑 "
                  "bash /tmp/soak.sh）后，之前的采样才作数")
        return 1
    print("\n新鲜日志的源码副本一致、样本健康 ✓（未证明进程仍在运行或72小时完成）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
