#!/usr/bin/env python3
"""生成一份**满载**的 /api/v1/status，喂给从固件里抠出来的 `parse_status()`。

"满载"的界定变过一次，这里记清楚，免得下次又照着已经消失的常量算：
  · 早先固件按段写死 `FNOS_MAX_VOLS` 之类的上限，NAS 侧另有 `DEFAULT_LIMITS`，两边取小。
  · 现在两样都没了：固件改成**动态快照**（条数由 payload 里的实际条数决定，
    contract_check 会打印"数量不由 FNOS_MAX 常量截断"），NAS 侧 `DEFAULT_LIMITS = {}`
    表示默认不截断。唯一还在的硬约束是板子的字符串 arena 预算
    `CONFIG_FNOS_SNAPSHOT_MAX_BYTES`（默认 1 MiB）。
  · 所以满载 = 让各段的字节数加起来逼近这个预算。每项的字节数沿用
    `nas/fpk/contract_check.py` 的 `PER_ENTRY`（那份是量出来的，不是拍的），
    段内条数 = 该段分到的预算 ÷ 每项字节数。

**别把"取不到上限"退化成一个大数**：旧版在两边都是空字典时落到 `10**9`，
于是 `range(10**9)` 把检查跑成"永久挂起"（92% CPU 五分钟）。取不到就报错退出。
"""
import importlib.util
import io
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SEGMENTS = ("vols", "raid", "disks", "temps", "docker", "alerts")
# 用掉预算的多大一块。默认 0.8；run.sh 里的容量探针会从高到低改这个值（PARSE_FILL），
# 量出"板子到底能解析多大的帧"—— payload 字节与 arena 不是 1:1（arena 还要装结构体
# 向量和每串的账头），写死一档要么红得没道理，要么松得测不出回归。
FILL = float(os.environ.get("PARSE_FILL") or 0.8)
CAP_LIMIT = 20000                   # 单段条数上限；超过说明公式算错了，别硬造
DEFAULT_BUDGET = 1024 * 1024        # 与 components/fnos_monitor/Kconfig 的默认值一致


def per_entry():
    """每项字节数从 contract_check 现读 —— 两边不各说各话。"""
    p = os.path.join(REPO, "nas/fpk/contract_check.py")
    spec = importlib.util.spec_from_file_location("contract_check", p)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.PER_ENTRY


def arena_budget():
    """板子的字符串 arena 预算；两个来源都没有就用 Kconfig 的默认值并说清楚。"""
    for rel in ("build/config/sdkconfig.h", "sdkconfig"):
        p = os.path.join(REPO, rel)
        if not os.path.exists(p):
            continue
        s = io.open(p, encoding="utf-8").read()
        m = re.search(r"CONFIG_FNOS_SNAPSHOT_MAX_BYTES\s*=?\s*(\d+)", s)
        if m:
            return int(m.group(1)), rel
    return DEFAULT_BUDGET, "Kconfig 默认值（没找到 sdkconfig：先跑一次 ./idf.sh build）"


def main():
    cost = per_entry()
    budget, where = arena_budget()
    share = budget * FILL / len(SEGMENTS)
    n = {}
    for k in SEGMENTS:
        c = cost.get(k)
        if not c or c <= 0:
            sys.exit("✗ nas/fpk/contract_check.py 的 PER_ENTRY 里没有 %s 的每项字节数" % k)
        n[k] = int(share) // c
        if not 1 <= n[k] <= CAP_LIMIT:
            sys.exit("✗ %s 算出来 %d 条（超出 1..%d）：预算 %d 字节 ÷ %d 段 ÷ 每项 %d 字节"
                     " —— 公式不对就改公式，别硬造一个数出来"
                     % (k, n[k], CAP_LIMIT, budget, len(SEGMENTS), c))
    # stdout 是 payload 本身（run.sh 直接重定向成 full.json），口径说明走 stderr
    print("满载口径：arena 预算 %d 字节（%s）× %.0f%% ÷ %d 段 ⇒ %s"
          % (budget, where, FILL * 100, len(SEGMENTS), n), file=sys.stderr)

    d = {
        "v": 1, "proto": 2, "seq": 999999, "ts": 1791202179, "ts_ms": 1791202179000,
        "mono_ms": 123456789, "host": "fnos-nas-with-a-long-name", "ready": True,
        "age_s": 0.4, "uptime_s": 1234567, "app": "nasscreencompanion", "app_ver": "1.0.0",
        "cpu": {"pct": 100.0, "cores": 32, "load1": 12.34, "load5": 12.34, "load15": 12.34,
                "temp_c": 85.0, "procs": 4096, "runq": 8},
        "mem": {"total_mb": 262144.0, "used_mb": 262144.0, "avail_mb": 262144.0, "pct": 100.0,
                "swap_total_mb": 65536.0, "swap_used_mb": 65536.0},
        "net": {"if": "enp5s0", "rx_kbs": 1234567.8, "tx_kbs": 1234567.8,
                "rx_total_gb": 99999.9, "tx_total_gb": 99999.9},
        "vols": [{"mnt": "/vol%d-longname" % i, "fs": "btrfs", "total_gb": 99999.9,
                  "used_gb": 99999.9, "free_gb": 99999.9, "pct": 100.0} for i in range(n["vols"])],
        "raid": [{"dev": "md%d" % i, "lvl": "raid10", "state": "active", "health": "degraded",
                  "what": "recovery", "ok": False, "have": 7, "want": 8,
                  "sync_pct": 47.3} for i in range(n["raid"])],
        "disks": [{"dev": "nvme%dn1" % i, "rd_kbs": 999999.9, "wr_kbs": 999999.9}
                  for i in range(n["disks"])],
        # 温度：形状照采集端的真实输出 —— 每路一个通道，带 dev/ch（内核短名 + 通道名）
        # 与 dn（人读设备名，28 字符上限）。
        # **最后一路故意只给 n**：这是还没升级的老采集端形状，用来验"先取 dev、
        # 取不到才回退 n"这条路径（不造这个形状，"回退"就永远没被执行过）。
        "temps": [({"n": "enp1s0 PHY", "c": 125.0, "dev": "enp1s0-name-20char",
                    "ch": "PHY-temp", "dn": "Marvell AQC113 10GbE-28char!"}
                   if i < n["temps"] - 1 else
                   {"n": "old-collector-package-id-%d" % i, "c": 125.0})
                  for i in range(n["temps"])],
        "docker": [{"n": "container-name-23char%02d" % i, "up": True,
                    "s": ("Up 12 days (healthy) with a very long status"[:39])}
                   for i in range(n["docker"])],
        "alerts": [{"lv": "critical",
                    "m": "一个很长的告警消息，中文按三字节算，凑到实际会出现的长度上限。" * 2}
                   for _ in range(n["alerts"])],
        "zfs": {"arc_gb": 999.9, "hit_pct": 100.0},
        "modules": {k: {"status": "ok", "ts": 1791202179, "error": None} for k in
                    ("cpu", "mem", "net", "vols", "raid", "disks", "temps", "docker", "zfs")},
        "caps": ["cpu", "mem", "net", "vols", "raid", "disks", "temps", "docker", "zfs"],
        "trunc": {"limits": n, "totals": n, "dropped": {k: 0 for k in n}},
    }
    sys.stdout.write(json.dumps(d, ensure_ascii=False, separators=(",", ":")))


if __name__ == "__main__":
    main()
