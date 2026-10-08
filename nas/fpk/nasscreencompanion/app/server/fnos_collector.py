#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fnos_collector —— 飞牛 fnOS (Debian) 只读状态采集模块（NAS 屏幕伴侣的采集层）。

来源：本文件第 1~445 行是 nas/fnos-agent.py 的原样拷贝（开发版采集器，
SSH + systemd 部署、监听 8799），行为与字段契约保持不变；第 446 行以下是
应用中心版（nasscreencompanion）追加的伴侣层，只新增字段、不改已有字段。

设计目标（给 ESP32-P4 等 MCU 屏幕用）：
  * 一次 HTTP GET 拿到全部指标，payload 约 1~2 KB，纯 JSON；
  * 依赖为零（只用 Python 3 标准库，Python>=3.8 均可）；
  * 严格只读：只读 /proc、/sys、statvfs、/proc/mdstat、docker.sock，不写任何文件；
  * 采样与请求解耦：后台线程按固定间隔采样，HTTP 请求直接返回快照（请求耗时常数级）。

采集层字段（Collector）：cpu{pct,cores,load1,load5,load15,temp_c,procs}、
  mem{total,used,avail,pct,swap}、net{if,rx_kbs,tx_kbs,累计}、vols[]、raid[]、
  disks[]（按繁忙度前 10）、temps[]、docker[]（前 16）、zfs{arc_gb,hit_pct}、
  uptime_s、alerts[]（前 8）、顶层 {v:1, host, ts, ready}。

伴侣层新增（CompanionCollector）：proto=2、seq（采样序号）、ts_ms/mono_ms、
  modules{每段状态 + 该段自己的时间戳 + 错误}、caps（本机可用段）、
  trunc{limits,totals,dropped}——截断前总量，用来区分"只有 10 块盘"和"被截到 10 块"。
  docker 默认关闭：Docker Socket 不是只读权限。

P0「来源/权限矩阵」的可执行版本：
  python3 fnos_collector.py --probe     # 按包用户视角逐项判定可读性
  python3 fnos_collector.py --sample    # 采样一次并打印快照
可选环境变量：
  FNAS_TOKEN   非空时要求 ?token= 或 X-Token 头匹配（HTTP 层用，见 nas_companion_server.py）
  FNAS_NETIF   指定统计的网卡（默认自动：优先默认路由网卡）
  FNAS_VOLUMES 逗号分隔的挂载点白名单（默认自动发现）
"""

import argparse
import hmac
import json
import os
import re
import socket
import socketserver
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

# ---------------------------------------------------------------- 采样工具

def read_text(path, default=""):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return default


def read_int(path, default=0):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return default


# 采集根：默认 /sys。测试用 FNAS_SYSFS 指到一棵假的 sysfs 树 —— 温度的设备名要靠
# realpath 的父子关系推导，只有真树和假树同构才测得出来（这条链路上 macOS 没有 /sys，
# 不搭假树就只剩"看着像对"）。
SYSFS = os.environ.get("FNAS_SYSFS", "/sys")
# /proc 单独一个根：CPU 型号名要从 /proc/cpuinfo 读，测试时两棵树各自可注入。
PROC = os.environ.get("FNAS_PROC", "/proc")


def sysfs(*parts):
    return os.path.join(SYSFS, *parts)


# ---------------------------------------------------------------- 采集器

SKIP_FSTYPES = {
    "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "cgroup", "cgroup2", "overlay",
    "nsfs", "rpc_pipefs", "autofs", "mqueue", "hugetlbfs", "debugfs", "tracefs",
    "securityfs", "pstore", "bpf", "configfs", "fusectl", "squashfs", "ramfs",
    "binfmt_misc", "efivarfs",
    # 网络/融合文件系统：对端一挂，statvfs 会进不可中断的 D 态，采样线程再也回不来
    # （端点还会一直 200 + ready:true，返回冻结的旧数据 —— 监控器最不能犯的错）
    "nfs", "nfs4", "cifs", "smb3", "smbfs", "fuse", "fuseblk", "fuse.sshfs",
    "fuse.mergerfs", "fuse.rclone", "glusterfs", "ceph", "9p", "afs", "ncpfs",
}
# bind mount / 伪目录：按挂载点前缀过滤（不要按设备去重，那会把 btrfs 子卷、
# 同一池的多个数据集、同一 NFS 导出的两个挂载点一起吞掉）
SKIP_MNT_PREFIX = ("/run", "/sys", "/dev", "/proc", "/var/lib/docker", "/var/lib/containerd",
                   "/tmp", "/var/tmp", "/var/lock", "/var/run")


class Collector:
    def _trim(self, name, seq):
        """Full inventories by default; explicit installation policy stays visible."""
        self._totals[name] = len(seq)
        limit = getattr(self, "limits", DEFAULT_LIMITS).get(name)
        return seq[:max(0, int(limit))] if limit is not None else seq

    def __init__(self, interval=1.0, netif=None, volumes=None, hist_len=300):
        self.interval = float(interval)
        self.netif_forced = netif
        self.vol_whitelist = [v for v in (volumes or []) if v]
        self.hist_len = int(hist_len)
        self.lock = threading.Lock()
        self.snapshot = {"v": 1, "host": socket.gethostname(), "ts": 0, "ready": False}
        self.history = []              # [[ts, cpu, mem, rx_kbs, tx_kbs], ...]
        self._stop = threading.Event()
        self._prev_cpu = None
        self._prev_net = None
        self._prev_disk = None
        self._prev_zfs = None
        self._docker_cache = (0.0, [])  # (last_poll, result) —— 注意别叫 _docker，会遮蔽同名方法
        self._docker_seen_up = set()    # 曾经运行过的容器名（用于"掉线"告警）
        self._dev_cache = None          # 温度的设备名索引：(时间, [(realpath, 名字, 类别)])
        self._pci_cache = {}            # (vid,did) → (厂商名, 型号名)，pci.ids 查一次记住
        self._cpu_name = None           # /proc/cpuinfo 的型号名（只读一次）
        self.hostname = socket.gethostname()
        # 截断前总数：板子端数组有固定上限，必须能区分"只有 10 块盘"和"被截到 10 块"
        self._totals = {}

    def set_hist_len(self, hist_len):
        length = int(hist_len)
        if length < 1:
            raise ValueError("history length must be positive")
        with self.lock:
            self.hist_len = length
            if len(self.history) > length:
                del self.history[:-length]

    # ---- 启动/停止
    def start(self):
        t = threading.Thread(target=self._loop, name="sampler", daemon=True)
        t.start()
        return t

    def stop(self):
        self._stop.set()

    def _loop(self):
        while not self._stop.is_set():
            t0 = time.time()
            try:
                snap = self.sample()
                with self.lock:
                    self.snapshot = snap
                    self.history.append([snap["ts"], snap["cpu"]["pct"], snap["mem"]["pct"],
                                         snap["net"]["rx_kbs"], snap["net"]["tx_kbs"]])
                    if len(self.history) > self.hist_len:
                        del self.history[:len(self.history) - self.hist_len]
            except Exception as exc:                      # 采集失败不能让服务挂掉
                with self.lock:
                    self.snapshot = {"v": 1, "host": self.hostname, "ts": int(time.time()),
                                     "ready": False, "error": "%s: %s" % (type(exc).__name__, exc)}
            dt = self.interval - (time.time() - t0)
            if dt > 0:
                self._stop.wait(dt)

    # ---- 单项采集
    def _cpu(self):
        parts = read_text("/proc/stat").split("\n", 1)[0].split()
        if len(parts) < 8 or parts[0] != "cpu":
            # runq 也要给：字段形状在两条路径上必须一致，少一个键会让读它的
            # 客户端安静地显示 0（而不是报错），排查起来非常费劲。
            return {"pct": 0.0, "cores": os.cpu_count() or 1, "load1": 0.0, "load5": 0.0,
                    "load15": 0.0, "temp_c": None, "runq": 0, "procs": 0}
        # parts[1:9] = user..steal；guest/guest_nice 已经被算进 user/nice，
        # 取到第 11 项会把它们重复计入 total，导致 CPU% 系统性偏低
        vals = [int(x) for x in parts[1:9]]
        idle = vals[3] + vals[4]                                  # idle + iowait
        total = sum(vals)
        pct = 0.0
        if self._prev_cpu:
            p_idle, p_total = self._prev_cpu
            dt = total - p_total
            if dt > 0:
                pct = max(0.0, min(100.0, 100.0 * (1.0 - (idle - p_idle) / dt)))
        self._prev_cpu = (idle, total)
        load = read_text("/proc/loadavg").split()
        runq = procs = 0
        m = re.search(r"(\d+)\s*/\s*(\d+)", read_text("/proc/loadavg"))
        if m:                       # "2/1928" = 可运行 2 个 / 总任务 1928 个
            runq = int(m.group(1))
            procs = int(m.group(2))
        return {
            "pct": round(pct, 1),
            "cores": os.cpu_count() or 1,
            "load1": float(load[0]) if len(load) > 0 else 0.0,
            "load5": float(load[1]) if len(load) > 1 else 0.0,
            "load15": float(load[2]) if len(load) > 2 else 0.0,
            "temp_c": None,
            "runq": runq,
            "procs": procs,
        }

    def _mem(self):
        info = {}
        for line in read_text("/proc/meminfo").splitlines():
            k, _, v = line.partition(":")
            info[k.strip()] = int(v.split()[0]) if v.split() else 0
        total = info.get("MemTotal", 0)
        avail = info.get("MemAvailable", info.get("MemFree", 0))
        used = max(0, total - avail)
        swap_total = info.get("SwapTotal", 0)
        swap_used = max(0, swap_total - info.get("SwapFree", 0))
        mb = lambda kb: round(kb / 1024.0, 1)
        return {
            "total_mb": mb(total), "used_mb": mb(used), "avail_mb": mb(avail),
            "pct": round(100.0 * used / total, 1) if total else 0.0,
            "swap_total_mb": mb(swap_total), "swap_used_mb": mb(swap_used),
        }

    def _pick_netif(self):
        if self.netif_forced:
            return self.netif_forced
        best = None
        for line in read_text("/proc/net/route").splitlines()[1:]:
            f = line.split()
            if len(f) >= 2 and f[1] == "00000000":                # default route
                best = f[0]
                break
        if best:
            return best
        cur = self._net_counters()
        if not cur:
            return "lo"
        return max(cur.items(), key=lambda kv: kv[1][0])[0]

    def _net_counters(self):
        out = {}
        for line in read_text("/proc/net/dev").splitlines()[2:]:
            name, _, rest = line.partition(":")
            f = rest.split()
            if len(f) >= 9:
                out[name.strip()] = (int(f[0]), int(f[8]))
        return out

    def _net(self):
        iface = self._pick_netif()
        current = self._net_counters()
        now = time.monotonic()
        previous = getattr(self, "_prev_interfaces", {})
        interfaces = []
        for name in sorted(current):
            if name == "lo":
                continue
            rx, tx = current[name]
            old = previous.get(name)
            rx_rate = tx_rate = 0.0
            if old:
                dt = max(1e-6, now - old[2])
                rx_rate = max(0.0, (rx - old[0]) / dt / 1024.0)
                tx_rate = max(0.0, (tx - old[1]) / dt / 1024.0)
            interfaces.append({
                "if": name, "rx_kbs": round(rx_rate, 1), "tx_kbs": round(tx_rate, 1),
                "rx_total_gb": round(rx / (1024.0 ** 3), 2),
                "tx_total_gb": round(tx / (1024.0 ** 3), 2),
                "state": read_text(sysfs("class", "net", name, "operstate"), "unknown").strip(),
                "speed_mbps": max(0, read_int(sysfs("class", "net", name, "speed"), 0)),
                "physical": os.path.exists(sysfs("class", "net", name, "device")),
            })
        self._prev_interfaces = {name: (rx, tx, now) for name, (rx, tx) in current.items()}
        selected = next((row for row in interfaces if row["if"] == iface), None)
        summary = dict(selected) if selected else {"if": iface, "rx_kbs": 0.0, "tx_kbs": 0.0,
                                                   "rx_total_gb": 0.0, "tx_total_gb": 0.0}
        summary["interfaces"] = self._trim("interfaces", interfaces)
        return summary

    def _volumes(self):
        vols = []
        seen = set()
        for line in read_text("/proc/mounts").splitlines():
            f = line.split()
            if len(f) < 3:
                continue
            src, mnt, fstype = f[0], f[1], f[2]
            if fstype in SKIP_FSTYPES or mnt in seen:
                continue
            if mnt.startswith(SKIP_MNT_PREFIX) and mnt != "/":
                continue
            if self.vol_whitelist and mnt not in self.vol_whitelist:
                continue
            try:
                st = os.statvfs(mnt)
            except OSError:
                continue
            total = st.f_blocks * st.f_frsize
            free = st.f_bavail * st.f_frsize
            used = total - st.f_bfree * st.f_frsize
            if total <= 0:
                continue
            seen.add(mnt)
            gb = 1024.0 ** 3
            vols.append({
                "mnt": mnt, "fs": fstype,
                "total_gb": round(total / gb, 1), "used_gb": round(used / gb, 1),
                "free_gb": round(free / gb, 1),
                "pct": round(100.0 * (total - free) / total, 1),
            })
        vols.sort(key=lambda v: (v["mnt"] != "/", v["mnt"]))
        return self._trim("vols", vols)

    def _raid(self):
        txt = read_text("/proc/mdstat")
        out = []
        cur = None
        for line in txt.splitlines():
            if not line.strip():
                continue
            # 形如 "md1 : active raid1 sdc1[1] sdd1[0]"、"md127 : inactive sdb1[1](S)"、
            # 也可能只有 "md127 : inactive"（无成员）。级别必须单独判定，
            # 不能直接取第二个字段——那会把设备名当成 RAID 级别显示在面板上。
            head = re.match(r"^(md\d+)\s*:\s*(\S+)\s*(.*)$", line)
            if head:
                state, rest = head.group(2), head.group(3)
                m2 = re.match(r"^(raid\d+|linear|multipath|faulty|container)\b\s*(.*)$", rest)
                lvl, memtxt = (m2.group(1), m2.group(2)) if m2 else ("", rest)
                cur = {
                    "dev": head.group(1), "state": state, "lvl": lvl,
                    "members": ["%s%s" % (n, "(%s)" % fl if fl else "") for n, _i, fl in
                                re.findall(r"([a-z0-9]+)\[(\d+)\](?:\(([A-Z])\))?", memtxt)],
                    "have": 0, "want": 0, "ok": False, "sync_pct": -1.0, "what": "",
                }
                out.append(cur)
                continue
            if cur is None:
                continue
            m = re.search(r"\[(\d+)/(\d+)\]\s*\[([U_]+)\]", line)
            if m:
                cur["want"] = int(m.group(1))
                cur["have"] = int(m.group(2))
                flags = m.group(3)
                cur["ok"] = (cur["have"] == cur["want"]) and ("_" not in flags) and \
                            ("active" in cur["state"])
                continue
            m = re.search(r"\[([=>.]+)\]\s*(\w+)\s*=\s*([\d.]+)%", line)
            if m:
                # resync/recovery/check/repair 都只是进度。ok 只由 [n/m] 与 [U_] 决定：
                # 健康阵列每月做 check 的数小时里不该显示成异常。
                cur["sync_pct"] = float(m.group(3))
                cur["what"] = m.group(2)
        # mdstat 的顺序是组装顺序，面板上按设备名排更易读
        out.sort(key=lambda r: r["dev"])
        return self._trim("raid", out)

    def _disk_io(self):
        """md 阵列 + 物理盘的读写速率（KB/s），用于判断"NAS 正在干活吗"。"""
        cur = {}
        for line in read_text("/proc/diskstats").splitlines():
            f = line.split()
            if len(f) < 14:
                continue
            name = f[2]
            if not (name.startswith("md") or re.match(r"^(sd[a-z]+|nvme\d+n\d+)$", name)
                    or name.startswith("dm-")):
                continue
            cur[name] = (int(f[5]) * 512, int(f[9]) * 512)          # sectors read/written
        now = time.monotonic()
        rates = []
        if self._prev_disk:
            prev, pt = self._prev_disk
            dt = max(1e-6, now - pt)
            for name, (r, w) in cur.items():
                pr, pw = prev.get(name, (r, w))
                rd = max(0.0, (r - pr) / dt / 1024.0)
                wr = max(0.0, (w - pw) / dt / 1024.0)
                rates.append({"dev": name, "rd_kbs": round(rd, 1), "wr_kbs": round(wr, 1)})
        self._prev_disk = (cur, now)
        rates.sort(key=lambda d: (d["rd_kbs"] + d["wr_kbs"]), reverse=True)
        return self._trim("disks", rates)

    # ── 温度：把 /sys/class/hwmon 下**每一个**可读通道都报出来（板端逐路显示）。
    #
    #    设备名不查对照表，全部按系统事实推导：哪块 block / 哪张网卡 / 哪个 DRM
    #    控制器和这个 hwmon 指向同一个底层设备，就用它的名字（nvme0n1 / enp1s0 /
    #    i915）；推导不出来时才退回 hwmon 自己的名字（coretemp、acpitz……）。
    #    换机器、插新卡、加硬盘，标签自动跟着变，不需要改代码。
    # 通道语义像 CPU 的标签：Intel coretemp 的 "Package id 0"/"Core 3"、
    # AMD k10temp 的 "Tctl"/"Tdie" 都命中。用它而不是"驱动名对照表"来认 CPU。
    CPU_LABEL_RE = re.compile(r"(?i)\b(package|tctl|tdie|tccd|cpu|core\s*\d+)\b")

    @staticmethod
    def _ls(path):
        try:
            return sorted(os.listdir(path))
        except OSError:
            return []

    @staticmethod
    def _dev_link(path):
        """path/device 的 realpath；该目录没有 device 链接就返回空串。"""
        p = os.path.join(path, "device")
        try:
            if not os.path.exists(p):
                return ""
            return os.path.realpath(p)
        except OSError:
            return ""

    def _dev_map(self):
        """[(底层设备 realpath, 设备短名, 类别)]，30 秒缓存（设备拓扑不会每秒变）。

        类别（block / net / drm）决定"人读的设备名"从哪儿取：磁盘取型号、网卡与显卡取
        PCI 厂商+型号，取不到就退回"驱动名 + 链路速率"。"""
        now = time.time()
        if self._dev_cache and now - self._dev_cache[0] < 30.0:
            return self._dev_cache[1]
        cand = []
        for nm in self._ls(sysfs("class", "block")):
            # 分区（带 partition 文件）不是独立设备，用整盘名
            if os.path.exists(sysfs("class", "block", nm, "partition")):
                continue
            rp = self._dev_link(sysfs("class", "block", nm))
            if rp:
                cand.append((rp, nm, "block"))
        for nm in self._ls(sysfs("class", "net")):
            rp = self._dev_link(sysfs("class", "net", nm))
            if rp:
                cand.append((rp, nm, "net"))
        for nm in self._ls(sysfs("class", "drm")):
            if not nm.startswith("card") or "-" in nm:   # card0 / card1（连接器名跳过）
                continue
            rp = self._dev_link(sysfs("class", "drm", nm))
            if not rp:
                continue
            try:
                drv = os.path.basename(os.path.realpath(os.path.join(rp, "driver")))
            except OSError:
                drv = ""
            if drv:
                cand.append((rp, drv, "drm"))
        self._dev_cache = (now, cand)
        return cand

    @staticmethod
    def _pick_device(rp, cand):
        """hwmon 的底层设备路径 → (设备短名, 类别)，最贴近它的那个（含父子关系）。

        NVMe 的 hwmon 挂在 PCI 控制器上，而 block 设备的 device 在它下面一层
        （…/0000:03:00.0/nvme/nvme0），所以不能只比相等，要认前缀包含。"""
        best, best_kind, best_len = "", "", -1
        if not rp:
            return best, best_kind
        for crp, nm, kind in cand:
            if rp == crp or rp.startswith(crp + os.sep) or crp.startswith(rp + os.sep):
                if len(crp) > best_len:
                    best, best_kind, best_len = nm, kind, len(crp)
        return best, best_kind

    # PCI 厂商号 → 厂商名。**只兜底到厂商这一级**：型号名优先从系统自带的 pci.ids 里查，
    # 查不到就退回"驱动名 + 链路速率"。表里也没有就原样写 PCI 号 —— 宁可难看，也不要
    # 编一个"看起来很像"的型号：认错设备比不认识更糟（用户会照着它去拆机箱/加风扇）。
    PCI_VENDOR = {"8086": "Intel", "10ec": "Realtek", "14e4": "Broadcom",
                  "1d6a": "Marvell", "1b4b": "Marvell", "15b3": "Mellanox",
                  "1022": "AMD", "1002": "AMD", "10de": "NVIDIA", "1a03": "ASPEED",
                  "144d": "Samsung", "1344": "Micron", "1987": "Phison",
                  "1c5c": "SK hynix", "1e0f": "KIOXIA", "1cc1": "ADATA", "2646": "Kingston"}

    def _pci_name(self, vid, did):
        """(厂商名, 型号名)；vid/did 形如 "0x1d6a"。系统里没有 pci.ids 就返回空串。"""
        if not vid.startswith("0x"):
            return "", ""
        v = vid[2:].lower()
        d = did[2:].lower() if did.startswith("0x") else ""
        hit = self._pci_cache.get((v, d))
        if hit is not None:
            return hit
        vend, chip = "", ""
        # 第一项是测试注入点：pci.ids 在 /usr/share 下，假 sysfs 树测不到它，
        # 而"厂商 + 型号"这条路径恰恰是最需要被测的（型号认错比认不出更糟）。
        # 另外把 Debian 常见的 .gz 也认了（有的机器只装压缩版）。
        extra = [x for x in os.environ.get("FNAS_PCI_IDS", "").split(":") if x]
        for p in extra + ["/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids",
                          "/usr/share/pci.ids", "/usr/share/hwdata/pci.ids.gz",
                          "/usr/share/misc/pci.ids.gz"]:
            try:
                if p.endswith(".gz"):
                    import gzip
                    fh = gzip.open(p, "rt", encoding="utf-8", errors="replace")
                else:
                    fh = open(p, "r", encoding="utf-8", errors="replace")
            except OSError:
                continue
            cur = ""
            with fh:
                for line in fh:
                    if not line.strip() or line.startswith("#"):
                        continue
                    if line[0] not in ("\t", " "):          # 厂商行："1d6a  Aquantia Corp."
                        cur = line[:4].lower()
                        if cur == v:
                            vend = line[4:].strip()
                            if not d:
                                break
                    elif cur == v and line.startswith("\t") and not line.startswith("\t\t"):
                        if line[1:5].lower() == d:          # 型号行："\t0001  AQC107 ..."
                            chip = line[5:].strip()
                            break
            if vend:
                break
        self._pci_cache[(v, d)] = (vend, chip)
        return vend, chip

    def _cpu_model(self):
        """CPU 型号名（"Intel N100" / "AMD Ryzen 5 5600"）；取不到返回空串。"""
        if self._cpu_name is not None:
            return self._cpu_name
        name = ""
        for line in read_text(os.path.join(PROC, "cpuinfo")).splitlines():
            low = line.lower()
            if (low.startswith("model name") or low.startswith("hardware")) and ":" in line:
                name = line.split(":", 1)[1].strip()      # x86 用 model name，ARM 常用 Hardware
                break
        name = re.sub(r"\((?:R|TM|r|tm)\)", "", name)
        name = re.sub(r"\s+(?:CPU|Processor)\b.*$", "", name)   # 去掉 "CPU @ 2.00GHz"
        name = re.sub(r"\s{2,}", " ", name).strip()
        self._cpu_name = name
        return name

    # 链路速率 → 人读写法（单位换算，不是设备身份）
    LINK_RATE = {100000: "100GbE", 40000: "40GbE", 25000: "25GbE", 10000: "10GbE",
                 5000: "5GbE", 2500: "2.5GbE", 1000: "1GbE", 100: "100MbE"}

    # pci.ids 的厂商名常常很长（"Advanced Micro Devices, Inc. [AMD/ATI]"），而板端
    # 一行只有 28 个字符：先砍掉 Corp./Inc./Ltd./Semiconductor… 这类尾巴；还是太长
    # 就用 PCI_VENDOR 表里的短名。**砍的是厂商名，不是型号名** —— 型号才是认设备的。
    VENDOR_TAIL = re.compile(r"(?i)\b(corp|corporation|inc|incorporated|ltd|limited|co|company|"
                             r"technologies|technology|semiconductor|electronics|microsystems|"
                             r"gmbh|llc|plc|ag|bv|oy|ab)\b.*$")

    # 型号名的压缩规则见下
    # pci.ids 的型号名常带方括号别名，且括号里往往是**用户认识的那个名字**：
    # "CoffeeLake-S GT2 [UHD Graphics 630]" → 取括号内容；再砍掉 "… Controller" 尾巴。
    # 板端一行只有 28 字符，不处理就会被截成 "Intel CoffeeLake-S GT2 [UHD "。
    CHIP_TAIL = re.compile(r"(?i)\s+(controller|ethernet controller|series)$")

    def _chip_short(self, chip):
        """把型号名压到能塞进一行的长度（取方括号别名，砍通用尾巴）。"""
        m = re.search(r"\[([^\]]{3,})\]", chip)
        if m:
            chip = m.group(1)
        return self.CHIP_TAIL.sub("", chip).strip()

    def _vendor_short(self, vid, vend):
        """把厂商名压到能塞进一行的长度；压不动就退回短名表。"""
        short = self.VENDOR_TAIL.sub("", vend).strip(" ,.;-") if vend else ""
        if not short or len(short) > 14:
            key = vid[2:].lower() if vid.startswith("0x") else ""
            short = self.PCI_VENDOR.get(key, "")
        if not short and vid.startswith("0x"):
            short = "PCI %s" % vid[2:]
        return short

    def _dev_name(self, kind, dev, rp, hw, cpuish):
        """人读的设备名 —— 用户要拿它决定"给谁降温"，所以不能是 NIC 这种缩写，
        也不能只是 enp1s0 这种内核 id。一律从系统事实里读，读不到就退回更朴素的那个。"""
        if kind == "block":
            m = read_text(sysfs("class", "block", dev, "device", "model")).strip()
            return m or dev
        if kind in ("net", "drm"):
            vid = read_text(os.path.join(rp, "vendor")).strip()
            did = read_text(os.path.join(rp, "device")).strip()
            vend, chip = self._pci_name(vid, did)
            vend = self._vendor_short(vid, vend)
            chip = self._chip_short(chip)
            try:
                drv = os.path.basename(os.path.realpath(os.path.join(rp, "driver")))
            except OSError:
                drv = ""
            head = " ".join(x for x in (vend, chip or drv) if x)
            if kind == "net":
                rate = self.LINK_RATE.get(read_int(sysfs("class", "net", dev, "speed"), 0), "")
                # 速率只在"加了还塞得下、且名字里本来没有速率"时才补：pci.ids 的型号名
                # 有的自带 "10GbE Controller"，无脑再补一遍会变成 "… 10GbE 10Gb"。
                if rate and "GbE" not in head and "MbE" not in head:
                    head = "%s %s" % (head, rate)
            return head or dev
        if cpuish:
            return self._cpu_model() or hw
        return hw

    def _temps(self):
        """每一个能读到温度的通道一路。

        每路给：dev（内核短名，如 enp1s0）、ch（通道名，如 PHY）、dn（**人读的设备名**，
        如 "Marvell AQC113 10GbE" / "Samsung SSD 990 PRO 2TB" / "Intel N100"）、c。
        dev 保证是稳定的 id（界面上要能一行一行认住），dn 才是"这是什么设备"。"""
        out = []
        self._cpu_temp = None
        fallback_cpu = None
        cand = self._dev_map()
        for e in self._ls(sysfs("class", "hwmon")):
            hp = sysfs("class", "hwmon", e)
            name = read_text(os.path.join(hp, "name")).strip()
            if not name:
                continue
            rp = self._dev_link(hp)          # hp 本身是符号链接，设备路径要取它的 device
            chans = []                       # [(通道序号, 通道名, 摄氏度)]
            for f in self._ls(hp):
                m = re.match(r"temp(\d+)_input$", f)
                if not m:
                    continue
                c = read_int(os.path.join(hp, f), -1) / 1000.0
                if c <= 0:                   # 读不到 / 该通道没接传感器
                    continue
                ch = read_text(os.path.join(hp, "temp%s_label" % m.group(1))).strip()
                ch = re.sub(r"(?i)\s+temperature$", "", ch) or ("temp%s" % m.group(1))
                chans.append((int(m.group(1)), ch, c))
            if not chans:
                continue
            chans.sort()
            dev, kind = self._pick_device(rp, cand)
            cpuish = any(self.CPU_LABEL_RE.search(ch) for _, ch, _ in chans)
            if not dev:
                dev = os.path.basename(rp.rstrip(os.sep)) or name
            dn = self._dev_name(kind, dev, rp, name, cpuish)
            if fallback_cpu is None and rp.startswith(sysfs("devices", "platform")):
                fallback_cpu = round(chans[0][2], 1)
            for _, ch, c in chans:
                if self._cpu_temp is None and self.CPU_LABEL_RE.search(ch):
                    self._cpu_temp = round(c, 1)
                out.append({"n": "%s %s" % (dev, ch), "c": round(c, 1),
                            "dev": dev, "ch": ch,
                            "dn": dn})
        if self._cpu_temp is None:
            self._cpu_temp = fallback_cpu
        out.sort(key=lambda t: t["c"], reverse=True)
        return self._trim("temps", out)

    def _docker(self):
        import docker_api
        now = time.monotonic()
        if self._docker_cache[0] and now - self._docker_cache[0] < 5.0:
            self._totals['docker'] = self._docker_total
            return self._docker_cache[1]
        frame = docker_api.containers()
        result = self._trim("docker", frame['rows'])
        self._docker_total = frame['total']
        self._docker_ts = frame['ts']
        self._totals['docker'] = self._docker_total
        self._docker_seen_up.update(c['n'] for c in result if c['up'])
        self._docker_cache = (now, result)
        return result

    def _zfs(self):
        txt = read_text("/proc/spl/kstat/zfs/arcstats")
        if not txt:
            return None
        vals = {}
        for line in txt.splitlines():
            f = line.split()
            if len(f) == 3 and f[1] == "4":
                try:
                    vals[f[0]] = int(f[2])
                except ValueError:
                    pass
        if "size" not in vals:
            return None
        hit_pct = None
        if self._prev_zfs:
            dh = vals.get("hits", 0) - self._prev_zfs[0]
            dm = vals.get("misses", 0) - self._prev_zfs[1]
            if dh + dm > 0:
                hit_pct = round(100.0 * dh / (dh + dm), 1)
        self._prev_zfs = (vals.get("hits", 0), vals.get("misses", 0))
        if hit_pct is None:
            h, m = vals.get("hits", 0), vals.get("misses", 0)
            hit_pct = round(100.0 * h / (h + m), 1) if h + m else None
        return {"arc_gb": round(vals["size"] / 1024.0 ** 3, 2), "hit_pct": hit_pct}

    def _alerts(self, snap):
        out = []
        for r in snap.get("raid", []):
            if not r["ok"]:
                if r["sync_pct"] >= 0:
                    out.append({"lv": "info", "m": "%s %s %.1f%%" % (r["dev"], r["what"] or "resync", r["sync_pct"])})
                else:
                    out.append({"lv": "crit", "m": "RAID %s degraded (%s)" % (r["dev"], r["state"])})
        for v in snap.get("vols", []):
            if v["pct"] >= 90:
                out.append({"lv": "crit", "m": "%s free %.1f%% left" % (v["mnt"], 100 - v["pct"])})
            elif v["pct"] >= 80:
                out.append({"lv": "warn", "m": "%s used %.0f%%" % (v["mnt"], v["pct"])})
        cpu = snap.get("cpu", {})
        if cpu.get("temp_c") and cpu["temp_c"] >= 80:
            out.append({"lv": "warn", "m": "CPU temp %.0fC" % cpu["temp_c"]})
        if snap.get("mem", {}).get("pct", 0) >= 90:
            out.append({"lv": "warn", "m": "MEM used %.0f%%" % snap["mem"]["pct"]})
        if cpu.get("cores") and cpu.get("load5", 0) > cpu["cores"]:
            out.append({"lv": "warn", "m": "LOAD high %.2f" % cpu["load5"]})
        for c in (snap.get("docker") or []):
            # 只报"本来在跑、现在掉了"的容器；开机就停着的（如已弃用的 exporter）不刷告警
            if (snap.get("modules", {}).get("docker", {}).get("status", "ok") == "ok"
                    and not c["up"] and c["n"] in self._docker_seen_up):
                out.append({"lv": "warn", "m": "container %s down" % c["n"]})
        return self._trim("alerts", out)



# ================================================================ 伴侣应用扩展
#
# 下面这一层是给飞牛应用中心版（nasscreencompanion）补的，原 fnos-agent 的
# Collector 保持可用、契约不变。新增能力都走新字段，不改动已有字段名/类型。

PROTO_VERSION = 2          # 伴侣应用协议版本；snapshot["v"] 保持 1 不动（旧板兼容）

# 各模块依赖的系统来源：用于"包用户下逐项读取验证"（P0 验收）与运行期能力上报
MODULE_SOURCES = (
    ("cpu", "/proc/stat"),
    ("cpu", "/proc/loadavg"),
    ("mem", "/proc/meminfo"),
    ("net", "/proc/net/dev"),
    ("net", "/proc/net/route"),
    ("vols", "/proc/mounts"),
    ("raid", "/proc/mdstat"),
    ("disks", "/proc/diskstats"),
    ("temps", "/sys/class/hwmon"),
    ("zfs", "/proc/spl/kstat/zfs/arcstats"),
    ("docker", "/var/run/docker.sock"),
)

MODULE_ORDER = ("cpu", "mem", "net", "vols", "raid", "disks", "temps", "docker", "zfs")

# No hardware-count defaults. An installation may explicitly configure a limit;
# trunc reports that policy and every omitted item to older consumers as well.
DEFAULT_LIMITS = {}

ST_OK = "ok"                # 该段本次采样成功
ST_STALE = "stale"          # 本次失败，沿用上一份旧值（带旧时间戳）
ST_MISSING = "missing"      # 来源不存在（驱动缺失 / 功能未启用 / 无该硬件）
ST_DENIED = "denied"        # 权限不足（包用户下最容易踩的一类）
ST_ERROR = "error"          # 读取到了但解析失败
ST_DISABLED = "disabled"    # 被配置显式关闭（如 docker）


def classify_read(path):
    """按包用户视角判定一个路径的可读性，返回 (status, detail)。"""
    try:
        with open(path, "rb") as f:
            f.read(1)
        return ST_OK, None
    except PermissionError as exc:
        return ST_DENIED, str(exc)
    except FileNotFoundError:
        return ST_MISSING, None
    except OSError as exc:
        return ST_ERROR, "%s: %s" % (type(exc).__name__, exc)


def classify_statvfs(path):
    """卷容量走 statvfs，权限模型与 open() 不同，单独判。"""
    try:
        os.statvfs(path)
        return ST_OK, None
    except PermissionError as exc:
        return ST_DENIED, str(exc)
    except OSError as exc:
        return ST_MISSING, "%s: %s" % (type(exc).__name__, exc)


def probe_capabilities(include_docker=True):
    """逐项探测本进程（应当就是包用户）能读到什么。

    这是 P0「来源/权限矩阵」的可执行版本：把应用装到 NAS 上跑一次
    `python3 fnos_collector.py --probe`，输出就是矩阵的实测列。
    """
    sources = []
    by_module = {}

    def record(module, path, status, detail=None):
        sources.append({"module": module, "path": path, "status": status, "detail": detail})
        cur = by_module.setdefault(module, {"ok": 0, "denied": 0, "missing": 0, "error": 0, "disabled": 0})
        cur[status if status in cur else "error"] += 1

    for module, path in MODULE_SOURCES:
        if module == "docker" and not include_docker:
            record(module, path, ST_DISABLED, "容器采集默认关闭")
            continue
        if module == "docker":
            import docker_api
            try:
                docker_api.containers()
                record(module, path, ST_OK, "固定 GET 容器状态读取成功")
            except PermissionError as exc:
                record(module, path, ST_DENIED, str(exc))
            except FileNotFoundError as exc:
                record(module, path, ST_MISSING, str(exc))
            except Exception as exc:
                record(module, path, ST_ERROR, '%s: %s' % (type(exc).__name__, exc))
            continue
        if module == "temps":
            # hwmon 是目录：逐个 temp*_input 试读，才能反映"部分传感器可读"
            if not os.path.isdir(path):
                record(module, path, ST_MISSING, None)
                continue
            try:
                chips = sorted(os.listdir(path))
            except OSError as exc:
                record(module, path, ST_DENIED, str(exc))
                continue
            record(module, path, ST_OK, "%d 个 hwmon 芯片" % len(chips))
            for chip in chips:
                cdir = os.path.join(path, chip)
                try:
                    names = [n for n in sorted(os.listdir(cdir)) if re.match(r"temp\d+_input$", n)]
                except OSError as exc:
                    record(module, os.path.join(cdir, "temp*_input"), ST_DENIED, str(exc))
                    continue
                for name in names:
                    record(module, os.path.join(cdir, name), *classify_read(os.path.join(cdir, name)))
            continue
        # /proc/net/route 在部分内核上名字不同，缺失不算故障
        record(module, path, *classify_read(path))

    modules = {}
    for module in MODULE_ORDER:
        c = by_module.get(module)
        if c is None:
            modules[module] = ST_MISSING
        elif c['disabled']:
            modules[module] = ST_DISABLED
        elif c["ok"] and not (c["denied"] or c["error"] or c["missing"]):
            modules[module] = ST_OK
        elif c["ok"]:
            modules[module] = "partial"
        elif c["denied"]:
            modules[module] = ST_DENIED
        elif c["error"]:
            modules[module] = ST_ERROR
        else:
            modules[module] = ST_MISSING

    return {
        "ts": int(time.time()),
        "app": "nasscreencompanion",
        "proto": PROTO_VERSION,
        "python": sys.version.split()[0],
        "user": _current_user(),
        "uid": os.getuid(),
        "gid": os.getgid(),
        "groups": sorted(os.getgroups()),
        "modules": modules,
        "sources": sources,
    }


def _current_user():
    try:
        import pwd
        return pwd.getpwuid(os.getuid()).pw_name
    except Exception:                                  # noqa: BLE001
        return os.environ.get("USER") or str(os.getuid())


class CompanionCollector(Collector):
    """在 fnos-agent 的 Collector 之上补齐伴侣应用要的契约。

      * 每段独立状态 + 该段自己的采样时间：某段失败保留旧值时，消费者能一眼看出"这是旧的"；
      * seq 采样序号：板子用来判断"我到底拿到新帧没有"；
      * trunc 截断信息：区分"真的只有 10 块盘"和"被截到 10 块"；
      * docker 默认关闭：Docker Socket 不是只读权限，必须显式开启。
    """

    # 这几种返回值"空"就等于不可用，而不是"正常的空列表"
    EMPTY_MEANS_MISSING = ("zfs", "temps", "disks")

    def __init__(self, interval=1.0, netif=None, volumes=None, hist_len=300,
                 docker_enabled=False, limits=None):
        Collector.__init__(self, interval=interval, netif=netif,
                           volumes=volumes, hist_len=hist_len)
        self.docker_enabled = bool(docker_enabled)
        self.limits = dict(DEFAULT_LIMITS)
        if limits:
            self.limits.update(limits)
        self._seq = 0
        self._module_meta = {}

    # ---- 采集
    def sample(self):
        self._seq += 1
        self._totals = {}
        snap = {"v": 1, "host": self.hostname, "ts": int(time.time()), "ready": True}
        with self.lock:
            prev = dict(self.snapshot) if self.snapshot.get("ready") else {}
            prev_meta = dict(self._module_meta)

        meta = {}
        errors = []
        for key, fn in (("cpu", self._cpu), ("mem", self._mem), ("net", self._net),
                        ("vols", self._volumes), ("raid", self._raid),
                        ("disks", self._disk_io), ("temps", self._temps),
                        ("docker", self._docker), ("zfs", self._zfs)):
            if key == "docker" and not self.docker_enabled:
                snap[key] = []
                meta[key] = {"status": ST_DISABLED, "ts": snap["ts"], "error": None}
                continue
            try:
                value = fn()
                if value is None or (key in self.EMPTY_MEANS_MISSING and not value):
                    snap[key] = value if value is not None else (prev.get(key) if key in prev else None)
                    meta[key] = {"status": ST_MISSING, "ts": snap["ts"], "error": None}
                else:
                    snap[key] = value
                    ts = self._docker_ts if key == 'docker' else snap['ts']
                    meta[key] = {"status": ST_OK, "ts": ts, "error": None}
            except Exception as exc:                   # noqa: BLE001 - 采集段不能互相拖死
                errors.append(key)
                detail = "%s: %s" % (type(exc).__name__, exc)
                old = prev_meta.get(key) or {}
                if key in prev and old.get('status') in (ST_OK, ST_STALE, 'partial'):
                    snap[key] = prev[key]              # 保留旧值，但把"旧"如实标出来
                    meta[key] = {"status": ST_STALE, "ts": old.get("ts", 0), "error": detail}
                    self._totals[key] = prev.get('trunc', {}).get('totals', {}).get(key, 0)
                    if key == "net":
                        self._totals["interfaces"] = prev.get('trunc', {}).get('totals', {}).get(
                            "interfaces", len((prev.get("net") or {}).get("interfaces") or []))
                else:
                    snap[key] = None
                    state = ST_DENIED if isinstance(exc, PermissionError) else (ST_MISSING if isinstance(exc, FileNotFoundError) else ST_ERROR)
                    meta[key] = {"status": state, "ts": 0, "error": detail}

        try:
            snap["uptime_s"] = int(float(read_text("/proc/uptime", "0").split()[0] or 0))
        except (ValueError, IndexError):
            snap["uptime_s"] = prev.get("uptime_s", 0)
        if isinstance(snap.get("cpu"), dict):
            snap["cpu"]["temp_c"] = getattr(self, "_cpu_temp", None)
        if errors:
            snap["errors"] = errors

        snap["modules"] = meta
        try:
            snap["alerts"] = self._alerts(snap)
        except Exception:                              # noqa: BLE001
            snap["alerts"] = prev.get("alerts", [])

        # ---- 新增字段（旧板子忽略未知键即可，不改变任何已有字段的类型）
        snap["proto"] = PROTO_VERSION
        snap["seq"] = self._seq
        snap["ts_ms"] = int(time.time() * 1000)
        snap["mono_ms"] = int(time.monotonic() * 1000)
        snap["modules"] = meta
        snap["caps"] = [k for k in MODULE_ORDER
                        if meta.get(k, {}).get("status") in (ST_OK, ST_STALE)]
        # Explicit inventory policies retain original totals, including nested NIC rows.
        inventories = {k: snap.get(k) or []
                       for k in ("docker", "disks", "alerts", "vols", "raid", "temps")}
        inventories["interfaces"] = (snap.get("net") or {}).get("interfaces") or []
        tracked = tuple(inventories)
        snap["trunc"] = {
            "limits": {k: self.limits[k] for k in tracked if k in self.limits},
            "totals": {k: self._totals.get(k, len(inventories[k])) for k in tracked},
            "dropped": {k: max(0, self._totals.get(k, len(inventories[k])) - len(inventories[k]))
                        for k in tracked},
        }
        with self.lock:
            self._module_meta = meta
        return snap


if __name__ == "__main__":
    # python3 fnos_collector.py --probe      → 打印权限/能力矩阵（P0 取证）
    ap = argparse.ArgumentParser(description="fnOS 屏幕伴侣采集模块自检")
    ap.add_argument("--probe", action="store_true", help="打印来源可读性矩阵后退出")
    ap.add_argument("--docker", action="store_true", help="探测时包含 Docker Socket")
    ap.add_argument("--sample", action="store_true", help="采集一次并打印快照")
    args = ap.parse_args()
    if args.probe:
        print(json.dumps(probe_capabilities(include_docker=args.docker),
                         ensure_ascii=False, indent=2))
    elif args.sample:
        c = CompanionCollector(docker_enabled=args.docker)
        c._cpu()
        c._net()
        c._disk_io()
        time.sleep(1.05)                               # 速率类需要两次差值
        print(json.dumps(c.sample(), ensure_ascii=False, indent=2))
    else:
        ap.print_help()
