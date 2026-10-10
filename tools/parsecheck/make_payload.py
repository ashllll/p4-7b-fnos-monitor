#!/usr/bin/env python3
"""生成一份**满载**的 /api/v1/status：各段都取两边声明上限里更小的那个。

每项的字段是照着 `nas/fpk/nasscreencompanion/app/server/fnos_collector.py` 的真实
输出形状写的，值取实际会出现的长度上限（长主机名、39 字的容器状态、中文告警…）。
`run.sh` 把它喂给从固件里抠出来的 `parse_status()`。

上限从两边现读：NAS 的 `DEFAULT_LIMITS` 与固件的 `FNOS_MAX_*`，取较小者——
这样"板子能不能吃下自己声明能吃下的量"才是个真问题。
"""
import io
import json
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def nas_limits():
    p = os.path.join(REPO, "nas/fpk/nasscreencompanion/app/server/fnos_collector.py")
    s = io.open(p, encoding="utf-8").read()
    m = re.search(r"DEFAULT_LIMITS\s*=\s*\{(.*?)\}", s, re.S)
    return {k: int(v) for k, v in re.findall(r'"(\w+)":\s*(\d+)', m.group(1))}

def fw_caps():
    p = os.path.join(REPO, "components/fnos_monitor/fnos_data.h")
    s = io.open(p, encoding="utf-8").read()
    return {m.group(1).lower(): int(m.group(2))
            for m in re.finditer(r"#define\s+FNOS_MAX_(\w+)\s+(\d+)", s)}

def main():
    lim, cap = nas_limits(), fw_caps()
    n = {k: min(lim.get(k, 10 ** 9), cap.get(k, 10 ** 9))
         for k in ("vols", "raid", "disks", "temps", "docker", "alerts")}
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
    import sys
    main()
