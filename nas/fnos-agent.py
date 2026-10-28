#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fnos-agent —— 飞牛 fnOS (Debian) 只读状态采集器。

设计目标（给 ESP32-P4 等 MCU 屏幕用）：
  * 一次 HTTP GET 拿到全部指标，payload 约 1~2 KB，纯 JSON；
  * 依赖为零（只用 Python 3 标准库，Python>=3.8 均可）；
  * 严格只读：只读 /proc、/sys、statvfs、/proc/mdstat、docker.sock，不写任何文件；
  * 采样与请求解耦：后台线程按固定间隔采样，HTTP 请求直接返回快照（请求耗时常数级）。

端点：
  GET /api/v1/status    完整状态 JSON
  GET /api/v1/history   最近 N 个采样的曲线数据（板子重启后回填曲线）
  GET /api/v1/health    存活探测 {"ok":true}
  GET /                 浏览器可看的简易实时页面（排查用）

用法：
  python3 fnos-agent.py --port 8799 --interval 1.0
可选环境变量：
  FNAS_TOKEN   访问令牌（唯一凭据）：请求要带 ?token= 或 X-Token 头。
               为空且没有 --insecure-no-auth 时**拒绝启动**（fail closed）；
               正常安装由 nas/install-remote.sh 写进 /etc/fnos-agent.env（0600）。
  FNAS_NETIF   指定统计的网卡（默认自动：优先默认路由网卡）
  FNAS_VOLUMES 逗号分隔的挂载点白名单（默认自动发现）
"""

import argparse
import copy
import hmac
import json
import math
import http.client
import os
import re
import socket
import socketserver
import signal
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


def read_text_strict(path):
    """读不到就抛。凡"空内容 != 正常"的来源必须走这里。

    `read_text(default="")` 会把权限不足/IO 错误变成"空内容"，而空的 /proc/mdstat
    在语义上是"这台机器没有阵列"——采样层于是拿它覆盖上一帧，板子上从"md0 正常"
    直接跳成"未发现阵列"，全程没有任何一处报错（审计 R2）。
    """
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def raid_health(r):
    """阵列的结构健康度：ok / degraded / inactive / readonly / container / unknown。

    `ok` 原来是"默认 False，等汇总行来翻案"，而 RAID0/linear 本来就没有冗余、
    内核也不给它们打 `[n/m] [UU]` 行 —— 于是这类阵列永远显示"降级"（审计 R5）。
    维护进度（resync/check/repair）**不**进这个字段：健康与维护是两件事（审计 R1），
    降级阵列正在恢复仍然是降级。
    """
    state = r.get("state") or ""
    # 首词精确比较：`"active" in "inactive"` 为真，那是误判的来源（审计 R3）
    first = (state.split() or [""])[0].lower()
    lvl = (r.get("lvl") or "").lower()
    if "read-only" in state.lower() or r.get("ro"):
        return "readonly"
    if lvl == "container":
        return "container"                       # imsm/ddf 外部元数据容器：要在固件里组装
    if first == "inactive":
        return "inactive"
    if first != "active":
        return "unknown"
    if r.get("summary"):
        have, want, flags = int(r.get("have") or 0), int(r.get("want") or 0), r.get("flags") or ""
        return "ok" if (have == want and "_" not in flags) else "degraded"
    if lvl in ("raid0", "linear"):
        return "ok"                              # 没有冗余就没有汇总行，这是正常形态
    return "unknown"                             # 其余情况缺汇总行就不猜：未知不是健康


MAX_RESPONSE=4*1024*1024
DOCKER_SOCKET='/var/run/docker.sock'
# BEGIN GENERATED DOCKER METRICS
# Canonical Docker metric implementation; entry files supply stdlib imports,
# DOCKER_SOCKET and MAX_RESPONSE. Sync with python3 nas/sync_docker_metrics.py --write.
# Embedded at build time: root never imports code from a writable app directory.

def _metrics_get(identifier, suffix):
    if not re.fullmatch(r'[0-9a-f]{64}', identifier) or suffix not in ('json', 'stats?stream=false&one-shot=true'):
        raise ValueError('invalid read-only Docker metric request')
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as raw:
        # One stats read may wait on a busy daemon; the sampler rotates one
        # container per frame, so a full second is still bounded.
        raw.settimeout(1.0)
        raw.connect(DOCKER_SOCKET)
        request = ('GET /containers/%s/%s HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n' % (identifier, suffix)).encode('ascii')
        raw.sendall(request)
        with http.client.HTTPResponse(raw) as response:
            response.begin()
            if response.status != 200:
                raise RuntimeError('Docker metrics unavailable')
            body = response.read(MAX_RESPONSE+1)
    if len(body)>MAX_RESPONSE:
        raise ValueError('Docker metric response exceeds budget')
    result=json.loads(body)
    if not isinstance(result,dict):
        raise ValueError('invalid Docker metrics')
    return result

def _metric_value(value):
    return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value) and value>=0

class DockerMetrics:
    """One collector owns rotation, cached readings and CPU baselines.

    request reads a fixed Docker metric endpoint; clock is an optional wall clock.
    enrich updates panel rows in the same source order as raw_rows, before sorting.
    """
    def __init__(self, request, clock=None):
        self._request = request
        self._clock = clock
        self._cache = {}
        self._previous = {}
        self._cursor = 0

    def enrich(self, raw_rows, rows):
        """Rotate through every entity; bounded work per frame, explicit cache timestamp."""
        identities=[str(c.get('Id','')) for c in raw_rows]
        ids={i for i in identities if re.fullmatch(r'[0-9a-f]{64}',i)}
        self._cache={k:v for k,v in self._cache.items() if k in ids}
        self._previous={k:v for k,v in self._previous.items() if k in ids}
        eligible=sorted(ids)
        if eligible:
            identifier=eligible[self._cursor % len(eligible)]
            self._cursor+=1
            metrics={}
            # A failed inspection cannot publish old restart/resource values as new.
            try:
                info=self._request(identifier,'json')
                count=info.get('RestartCount')
                if isinstance(count,int) and not isinstance(count,bool) and count>=0:
                    metrics['restarts']=count
            except (OSError, ValueError, RuntimeError, http.client.HTTPException):
                pass
            container=next(c for c in raw_rows if c.get('Id')==identifier)
            if container.get('State')=='running':
                try:
                    stats=self._request(identifier,'stats?stream=false&one-shot=true')
                    cpu=stats.get('cpu_stats') or {}
                    usage=(cpu.get('cpu_usage') or {}).get('total_usage')
                    system=cpu.get('system_cpu_usage')
                    cores=cpu.get('online_cpus') or len((cpu.get('cpu_usage') or {}).get('percpu_usage') or [])
                    prior=self._previous.get(identifier)
                    if all(_metric_value(v) for v in (usage,system,cores)):
                        if prior and usage>=prior[0] and system>prior[1] and cores>0:
                            metrics['cpu_pct']=round((usage-prior[0])/(system-prior[1])*cores*100,2)
                        self._previous[identifier]=(usage,system)
                    memory=stats.get('memory_stats') or {}
                    used=memory.get('usage')
                    inactive=(memory.get('stats') or {}).get('total_inactive_file', (memory.get('stats') or {}).get('inactive_file',0))
                    if _metric_value(used) and _metric_value(inactive):
                        metrics['mem_mb']=round(max(0,used-inactive)/1024**2,2)
                except (OSError, ValueError, RuntimeError, http.client.HTTPException):
                    self._previous.pop(identifier,None)
            else:
                self._previous.pop(identifier,None)
            if metrics: metrics['metrics_ts']=int(self._clock() if self._clock else time.time())
            self._cache[identifier]=metrics
        # Rows are still in source order here. Sorting happens after enrichment.
        for original,row in zip(raw_rows,rows):
            identifier=str(original.get('Id',''))
            row.update(id=identifier,cpu_pct=None,mem_mb=None,restarts=None,metrics_ts=0)
            row.update(self._cache.get(identifier,{}))
# END GENERATED DOCKER METRICS

class Collector:
    def __init__(self, interval=1.0, netif=None, volumes=None, hist_len=300):
        self.interval = float(interval)
        self.netif_forced = netif
        self.vol_whitelist = [v for v in (volumes or []) if v]
        self.hist_len = int(hist_len)
        self.lock = threading.Lock()
        self.snapshot = {"v": 1, "host": socket.gethostname(), "ts": 0, "ready": False}
        self.history = []              # [[ts, cpu, mem, rx_kbs, tx_kbs], ...]
        self._stop = threading.Event()
        self._lifecycle_lock = threading.Lock()
        self._thread = None
        self._prev_cpu = None
        self._cpu_temp = None
        self._prev_net = None
        self._prev_disk = None
        self._prev_zfs = None
        self._docker_cache = (0.0, [])  # (last_poll, result) —— 注意别叫 _docker，会遮蔽同名方法
        self._docker_metrics = DockerMetrics(_metrics_get)
        self._docker_failed = False
        self._docker_seen_up = set()    # 曾经运行过的容器名（用于"掉线"告警）
        self._dev_cache = None          # 温度的设备名索引：(时间, [(realpath, 名字, 类别)])
        self._pci_cache = {}            # (vid,did) → (厂商名, 型号名)，pci.ids 查一次记住
        self._cpu_name = None           # /proc/cpuinfo 的型号名（只读一次）
        self.hostname = socket.gethostname()

    # ---- 启动/停止
    def start(self):
        with self._lifecycle_lock:
            if self._thread is not None and self._thread.is_alive():
                if self._stop.is_set():
                    raise RuntimeError("previous sampler has not stopped")
                return self._thread
            self._stop.clear()
            thread = threading.Thread(target=self._loop, name="sampler", daemon=True)
            thread.start()
            self._thread = thread
            return thread

    def stop(self, timeout=5.0):
        with self._lifecycle_lock:
            self._stop.set()
            thread = self._thread
        if thread is None:
            return True
        if thread is threading.current_thread():
            return False
        thread.join(timeout=max(0.0, timeout))
        if thread.is_alive():
            return False
        with self._lifecycle_lock:
            if self._thread is thread:
                self._thread = None
        return True

    def _loop(self):
        while not self._stop.is_set():
            t0 = time.monotonic()
            try:
                snap = self.sample()
                with self.lock:
                    if self._stop.is_set():
                        break
                    self.snapshot = snap
                    self.history.append([snap["ts"], snap["cpu"]["pct"], snap["mem"]["pct"],
                                         snap["net"]["rx_kbs"], snap["net"]["tx_kbs"],
                                         max((t["c"] for t in snap.get("temps", []) if isinstance(t.get("c"), (int,float))), default=None)
                                         if "temps" not in snap.get("errors", []) and snap.get("modules", {}).get("temps", {}).get("status", "ok") == "ok" else None])
                    if len(self.history) > self.hist_len:
                        del self.history[:len(self.history) - self.hist_len]
            except Exception as exc:                      # 采集失败不能让服务挂掉
                with self.lock:
                    if self._stop.is_set():
                        break
                    self.snapshot = {"v": 1, "host": self.hostname, "ts": int(time.time()),
                                     "ready": False, "error": "%s: %s" % (type(exc).__name__, exc)}
            dt = self.interval - (time.monotonic() - t0)
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
        # 键必须齐（审计 M02）：板子按字段名取值，缺 state/speed_mbps/physical 就是
        # 空/0，界面上的链路状态会安静地变成"没数据"。形状固定，值可以是"未知"。
        summary = dict(selected) if selected else {"if": iface, "rx_kbs": 0.0, "tx_kbs": 0.0,
                                                   "rx_total_gb": 0.0, "tx_total_gb": 0.0,
                                                   "state": "", "speed_mbps": 0,
                                                   "physical": False}
        summary["interfaces"] = interfaces
        return summary

    def _volumes(self):
        vols = []
        seen = set()
        for line in read_text("/proc/mounts").splitlines():
            f = line.split()
            if len(f) < 3:
                continue
            src, mnt, fstype = f[0], f[1], f[2]
            # 前缀判定，不是精确匹配（审计 M58）：SKIP_FSTYPES 列了 "fuse"/"fuseblk"/
            # "fuse.sshfs"/"fuse.mergerfs"/"fuse.rclone"，但 fuse.* 的子类型枚举不全 ——
            # fuse.davfs2 / gcsfuse / s3fs / cryfs / encfs 漏判后 os.statvfs() 会冲着
            # 已经挂死的网络/融合文件系统，采样线程卡在 D 态回不来。
            if fstype in SKIP_FSTYPES or fstype.startswith("fuse") or mnt in seen:
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
            # 保留块既不是已用也不是可用；百分比按可用容量算（与 df Use% 一致），
            # 与伴侣采集器的 /api/v1/status 保持同一口径。
            usable = used + free
            seen.add(mnt)
            gb = 1024.0 ** 3
            vols.append({
                "mnt": mnt, "fs": fstype,
                "total_gb": round(total / gb, 1), "used_gb": round(used / gb, 1),
                "free_gb": round(free / gb, 1),
                "pct": round(100.0 * used / usable, 1) if usable > 0 else 0.0,
            })
        vols.sort(key=lambda v: (v["mnt"] != "/", v["mnt"]))
        return vols

    def _raid(self):
        # 严格读：读失败要抛出去，让采样层保留上一帧并标 stale。
        # 退回空串等于宣布"这台机器没有阵列"，比报错更糟（审计 R2）。
        txt = read_text_strict("/proc/mdstat")
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
                # "(read-only)" 跟级别名平级、在 state 之后（"md0 : active (read-only) raid1 …"），
                # 只读阵列能照常读写失败才知道，必须在头行就认出来。
                ro = "(read-only)" in rest
                rest = re.sub(r"^\(read-only\)\s*", "", rest)
                m2 = re.match(r"^(raid\d+|linear|multipath|faulty|container)\b\s*(.*)$", rest)
                lvl, memtxt = (m2.group(1), m2.group(2)) if m2 else ("", rest)
                cur = {
                    "dev": head.group(1), "state": state, "lvl": lvl,
                    "members": ["%s%s" % (n, "(%s)" % fl if fl else "") for n, _i, fl in
                                re.findall(r"([a-z0-9]+)\[(\d+)\](?:\(([A-Z])\))?", memtxt)],
                    "have": 0, "want": 0, "ok": False, "health": "unknown",
                    "sync_pct": -1.0, "what": "",
                    "flags": "", "summary": False, "ro": ro,
                }
                out.append(cur)
                continue
            if cur is None:
                continue
            m = re.search(r"\[(\d+)/(\d+)\]\s*\[([U_]+)\]", line)
            if m:
                cur["want"] = int(m.group(1))
                cur["have"] = int(m.group(2))
                cur["flags"] = m.group(3)
                cur["summary"] = True
                continue
            m = re.search(r"\[([=>.]+)\]\s*(\w+)\s*=\s*([\d.]+)%", line)
            if m:
                # resync/recovery/check/repair 都只是进度。结构健康度只由 [n/m] 与 [U_]
                # 决定：健康阵列每月做 check 的数小时里不该显示成异常。
                pct = float(m.group(3))
                cur["sync_pct"] = pct if 0.0 <= pct <= 100.0 else -1.0   # 进度越界就不认
                cur["what"] = m.group(2)
        # mdstat 的顺序是组装顺序，面板上按设备名排更易读
        out.sort(key=lambda r: r["dev"])
        for r in out:
            r["health"] = raid_health(r)
            r["ok"] = (r["health"] == "ok")         # 未知不是健康
            r.pop("flags", None)
            r.pop("summary", None)
            r.pop("ro", None)
        return out

    def _disk_io(self):
        """KiB/s and read/write mean completion times; no I/O means unknown latency."""
        cur = {}
        for line in read_text("/proc/diskstats").splitlines():
            f = line.split()
            if len(f) < 14 or not re.fullmatch(r"md\d+|sd[a-z]+|nvme\d+n\d+|dm-\d+", f[2]):
                continue
            cur[f[2]] = tuple(int(f[i]) for i in (5, 9, 3, 7, 6, 10))
        now = time.monotonic()
        previous = self._prev_disk
        prev, pt = previous if previous else ({}, now)
        rates = []
        for name, values in sorted(cur.items()):
            row = {"dev": name, "rd_kbs": 0.0, "wr_kbs": 0.0,
                   "read_ms": None, "write_ms": None}
            old = prev.get(name)
            if old and now > pt and all(a >= b for a, b in zip(values, old)):
                d = [a-b for a,b in zip(values, old)]
                row.update(rd_kbs=round(d[0]*512/(now-pt)/1024, 1),
                           wr_kbs=round(d[1]*512/(now-pt)/1024, 1),
                           read_ms=round(d[4]/d[2], 2) if d[2] else None,
                           write_ms=round(d[5]/d[3], 2) if d[3] else None)
            rates.append(row)
        self._prev_disk = (cur, now)
        self._smart_enrich(rates)
        return rates

    def _smart_enrich(self, rows):
        """One due physical disk per tick, bounded read-only probe; never wake standby."""
        import shutil
        import subprocess
        now = time.monotonic()
        cache = getattr(self, '_smart_cache', {})
        physical = [r for r in rows if re.fullmatch(r'sd[a-z]+|nvme\d+n\d+', r['dev'])]
        ids = {r['dev'] for r in physical}
        cache = {k:v for k,v in cache.items() if k in ids}
        period = max(1.0, float(os.environ.get('FNAS_SMART_INTERVAL', '60')))
        due = sorted((r for r in physical if now-cache.get(r['dev'], (-period,))[0] >= period),
                     key=lambda r: cache.get(r['dev'], (-period,))[0])
        if due:
            dev = due[0]['dev']
            state, stamp = 'unavailable', 0
            binary = shutil.which('smartctl')
            if not binary:
                state = 'missing'
            else:
                try:
                    run = subprocess.run([binary, '-j', '-H', '-n', 'standby,3', '/dev/'+dev],
                                         capture_output=True, timeout=0.4, check=False)
                    data = json.loads(run.stdout)
                    if not isinstance(data,dict): raise ValueError('invalid SMART response')
                    passed = data.get('smart_status', {}).get('passed')
                    if isinstance(passed, bool) and not (run.returncode & 7):
                        state, stamp = ('passed' if passed else 'failed'), int(time.time())
                    elif run.returncode == 3:
                        state = 'standby'
                    elif any('permission denied' in str(m.get('string','')).lower() for m in data.get('smartctl',{}).get('messages',[]) if isinstance(m,dict)):
                        state = 'denied'
                except subprocess.TimeoutExpired:
                    state = 'timeout'
                except PermissionError:
                    state = 'denied'
                except (OSError, ValueError, TypeError):
                    pass
            cache[dev] = (now, state, stamp)
        for row in rows:
            item = cache.get(row['dev'])
            row['smart'] = item[1] if item else ('pending' if row['dev'] in ids else 'unsupported')
            row['smart_ts'] = item[2] if item else 0
        self._smart_cache = cache

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
            # Only NVMe namespaces may identify a sensor on their controller;
            # a SAS/SATA child disk must not claim its HBA's temperature.
            nvme_child = kind == "block" and crp.startswith(os.path.join(rp, "nvme") + os.sep)
            if rp == crp or rp.startswith(crp + os.sep) or nvme_child:
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
        return out

    def _docker(self):
        now = time.time()
        if now - self._docker_cache[0] < 5.0:
            if getattr(self, "_docker_failed", False):
                raise RuntimeError("Docker telemetry unavailable")
            return self._docker_cache[1]
        prev = self._docker_cache[1]
        result = prev
        try:
            status, body = _http_unix("/var/run/docker.sock",
                                      "/containers/json?all=1", timeout=4.0)
            if status != 200:
                raise RuntimeError("docker http %d" % status)
            rows = json.loads(body.decode("utf-8"))
            if not isinstance(rows, list):
                raise ValueError("invalid Docker inventory")
            result = []
            for c in rows:
                if not isinstance(c, dict):
                    raise ValueError("invalid Docker container")
                names = c.get("Names") or [c.get("Id")]
                if (not isinstance(names, list) or not names
                        or any(not isinstance(n, str) or not n.lstrip("/") for n in names)
                        or not isinstance(c.get("State"), str) or not c["State"]
                        or not isinstance(c.get("Status", ""), str)):
                    raise ValueError("invalid Docker container fields")
                result.append({
                    "n": names[0].lstrip("/"),
                    "up": c.get("State") == "running",
                    "s": c.get("Status") or "",
                })
            self._docker_metrics.enrich(rows,result)
            result.sort(key=lambda x: (not x["up"], x["n"]))
            self._docker_seen_up.update(c["n"] for c in result if c["up"])
        except Exception:
            self._docker_cache = (now, prev)
            self._docker_failed = True
            raise RuntimeError("Docker telemetry unavailable") from None
        self._docker_failed = False
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
        for r in (snap.get("raid") or []):
            health = r.get("health") or ("ok" if r.get("ok") else "unknown")
            dev = r.get("dev", "?")
            prog = " %s %.1f%%" % (r.get("what") or "resync", r["sync_pct"]) \
                if r.get("sync_pct", -1) >= 0 else ""
            if health == "degraded":
                # 降级就是降级："正在恢复"不改变结构已经缺成员这件事（审计 R1）
                out.append({"lv": "crit",
                            "m": "RAID %s degraded (%s)%s" % (dev, r.get("state") or "?", prog)})
            elif health == "inactive":
                out.append({"lv": "crit", "m": "RAID %s inactive%s" % (dev, prog)})
            elif health == "readonly":
                out.append({"lv": "warn", "m": "RAID %s read-only%s" % (dev, prog)})
            elif health == "container":
                out.append({"lv": "info", "m": "RAID %s external container (%s)"
                            % (dev, r.get("lvl") or "?")})
            elif health == "unknown":
                out.append({"lv": "info", "m": "RAID %s state unknown (%s)"
                            % (dev, r.get("state") or "?")})
            elif prog:
                out.append({"lv": "info", "m": "%s%s" % (dev, prog)})
        for disk in (snap.get("disks") or []):
            if disk.get("smart") == "failed":
                out.append({"lv":"crit", "m":"SMART %s failed" % disk.get("dev","?")})
        for v in (snap.get("vols") or []):
            if v["pct"] >= 90:
                out.append({"lv": "crit", "m": "%s free %.1f%% left" % (v["mnt"], 100 - v["pct"])})
            elif v["pct"] >= 80:
                out.append({"lv": "warn", "m": "%s used %.0f%%" % (v["mnt"], v["pct"])})
        cpu = (snap.get("cpu") or {})
        if cpu.get("temp_c") and cpu["temp_c"] >= 80:
            out.append({"lv": "warn", "m": "CPU temp %.0fC" % cpu["temp_c"]})
        if (snap.get("mem") or {}).get("pct", 0) >= 90:
            out.append({"lv": "warn", "m": "MEM used %.0f%%" % snap["mem"]["pct"]})
        if cpu.get("cores") and cpu.get("load5", 0) > cpu["cores"]:
            out.append({"lv": "warn", "m": "LOAD high %.2f" % cpu["load5"]})
        for c in (snap.get("docker") or []):
            # 只报"本来在跑、现在掉了"的容器；开机就停着的（如已弃用的 exporter）不刷告警
            if not c["up"] and c["n"] in self._docker_seen_up:
                out.append({"lv": "warn", "m": "container %s down" % c["n"]})
        return out

    def sample(self):
        # 逐段隔离：某一段抛异常时保留上一份的值并记录到 errors，
        # 而不是整帧变成 ready:false（板子对 ready:false 的处理是"保留上一帧"，
        # 结果是面板永久冻结在最后一帧，而 /health 还一直是 ok）。
        snap = {"v": 1, "host": self.hostname, "ts": int(time.time()), "ready": True}
        errors = []
        with self.lock:
            # 整帧 deepcopy 是隔离契约：失败段会回落到上一帧的值，调用方也可能
            # 同时持有旧快照；浅共享会让任何原地写入污染已发布的帧。
            # 见 test_server_safety 的 legacy raid 回归（要求新旧对象不同）。
            prev = copy.deepcopy(self.snapshot) if self.snapshot.get("ready") else {}
        for key, fn in (("cpu", self._cpu), ("mem", self._mem), ("net", self._net),
                        ("vols", self._volumes), ("raid", self._raid),
                        ("disks", self._disk_io), ("temps", self._temps),
                        ("docker", self._docker), ("zfs", self._zfs)):
            try:
                snap[key] = fn()
            except Exception as exc:                  # noqa: BLE001 - 采集段不能互相拖死
                errors.append(key)
                snap[key] = prev.get(key)
                if snap[key] is None:
                    snap[key] = [] if key in ("vols", "raid", "disks", "temps", "docker") else {}
        try:
            snap["uptime_s"] = int(float(read_text("/proc/uptime", "0").split()[0] or 0))
        except (ValueError, IndexError):
            snap["uptime_s"] = prev.get("uptime_s", 0)
        if isinstance(snap.get("cpu"), dict):
            snap["cpu"]["temp_c"] = self._cpu_temp
        if errors:
            snap["errors"] = errors
        try:
            snap["alerts"] = self._alerts(snap)
        except Exception:                             # noqa: BLE001
            snap["alerts"] = prev.get("alerts", [])
        return snap


# ---------------------------------------------------------------- docker 只读查询

DOCKER_HTTP_MAX_BYTES = 4 * 1024 * 1024
_HTTP_FIELD = re.compile(r"^[!#$%&'*+.^_`|~0-9A-Za-z-]+$")


def _http_unix(sock_path, path, timeout=3.0, method="GET"):
    """Bounded close-delimited Docker HTTP/1.x; incomplete framing is an error."""
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("invalid Docker HTTP timeout")
    deadline = time.monotonic() + timeout
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.settimeout(timeout)
        s.connect(sock_path)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Docker HTTP deadline exceeded")
        s.settimeout(remaining)
        req = ("%s %s HTTP/1.0\r\nHost: docker\r\nAccept: application/json\r\n"
               "Connection: close\r\n\r\n" % (method, path)).encode("ascii")
        s.sendall(req)
        buf = bytearray()
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Docker HTTP deadline exceeded")
            s.settimeout(remaining)
            chunk = s.recv(min(65536, DOCKER_HTTP_MAX_BYTES + 1 - len(buf)))
            if not chunk:
                break
            buf += chunk
            if len(buf) > DOCKER_HTTP_MAX_BYTES:
                raise ValueError("Docker HTTP response exceeds byte budget")
    finally:
        s.close()
    head, sep, body = bytes(buf).partition(b"\r\n\r\n")
    if not sep:
        raise ValueError("incomplete Docker HTTP header")
    lines = head.decode("latin1").split("\r\n")
    status_line = re.fullmatch(r"HTTP/1\.[01] ([1-5][0-9]{2})(?: [^\r\n]*)?", lines[0])
    if not status_line:
        raise ValueError("invalid Docker HTTP status")
    hdrs = {}
    for h in lines[1:]:
        k, colon, v = h.partition(":")
        if not colon or not _HTTP_FIELD.fullmatch(k):
            raise ValueError("invalid Docker HTTP header")
        k = k.lower()
        if k in ("content-length", "transfer-encoding") and k in hdrs:
            raise ValueError("duplicate Docker HTTP framing")
        hdrs[k] = v.strip()
    transfer = hdrs.get("transfer-encoding")
    length = hdrs.get("content-length")
    if transfer is not None:
        if transfer.lower() != "chunked" or length is not None:
            raise ValueError("unsupported Docker HTTP framing")
        body = _dechunk(body)
    elif length is not None:
        digits = length.lstrip("0") or "0"
        if (not re.fullmatch(r"[0-9]+", length)
                or len(digits) > len(str(DOCKER_HTTP_MAX_BYTES)) or int(digits) != len(body)):
            raise ValueError("Docker HTTP body length mismatch")
    return int(status_line.group(1)), body


def _dechunk(data):
    if len(data) > DOCKER_HTTP_MAX_BYTES:
        raise ValueError("Docker chunked input exceeds byte budget")
    out = bytearray()
    cursor = 0
    while True:
        end = data.find(b"\r\n", cursor)
        if end < 0:
            raise ValueError("missing Docker chunk size")
        number = data[cursor:end].split(b";", 1)[0]
        if not re.fullmatch(rb"[0-9A-Fa-f]+", number):
            raise ValueError("invalid Docker chunk size")
        number = number.lstrip(b"0") or b"0"
        if len(number) > len(format(DOCKER_HTTP_MAX_BYTES, "x")):
            raise ValueError("oversized Docker chunk")
        size = int(number, 16)
        cursor = end + 2
        if size == 0:
            while True:
                end = data.find(b"\r\n", cursor)
                if end < 0:
                    raise ValueError("incomplete Docker chunk trailer")
                line = data[cursor:end]
                cursor = end + 2
                if not line:
                    if cursor != len(data):
                        raise ValueError("trailing Docker chunk bytes")
                    return bytes(out)
                key, colon, value = line.partition(b":")
                if (not colon or not _HTTP_FIELD.fullmatch(key.decode("ascii", "strict"))
                        or key.lower() in (b"content-length", b"transfer-encoding")):
                    raise ValueError("invalid Docker chunk trailer")
        if size > DOCKER_HTTP_MAX_BYTES - len(out) or size > len(data) - cursor - 2:
            raise ValueError("incomplete or oversized Docker chunk")
        if data[cursor + size:cursor + size + 2] != b"\r\n":
            raise ValueError("invalid Docker chunk terminator")
        out.extend(memoryview(data)[cursor:cursor + size])
        cursor += size + 2


# ---------------------------------------------------------------- HTTP 服务

INDEX_HTML = """<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>fnos-agent · 采集诊断</title>
<style>
:root{color-scheme:dark}body{background:#0b0f14;color:#d8e2ee;font:14px/1.5 ui-monospace,Menlo,monospace;margin:0;padding:24px}
main{max-width:960px;margin:auto}h1{font-size:20px;color:#6ee7ff;margin:0 0 8px}p{color:#a9b9ca}form{display:flex;gap:8px;align-items:end;flex-wrap:wrap;margin:20px 0}
label{display:flex;flex-direction:column;gap:6px;flex:1;min-width:160px}input,button{font:inherit;padding:10px;border:1px solid #64768b;border-radius:4px;background:#151f2b;color:inherit}button{cursor:pointer}button:disabled{opacity:.55;cursor:default}input:focus-visible,button:focus-visible{outline:2px solid #6ee7ff;outline-offset:3px}pre{white-space:pre-wrap;overflow-wrap:anywhere;border-top:1px solid #344558;padding-top:16px}#status{min-height:3em;color:#8ef0a8}
</style><main>
<h1>fnos-agent · 采集诊断</h1>
<p>输入采集器令牌以读取状态。令牌仅用于当前页面，停止连接或关闭页面后需重新输入。此 HTTP 服务会明文传输令牌，请只在可信网络使用。</p>
<form id="auth" autocomplete="off"><label for="token">访问令牌<input id="token" type="password" autocomplete="off" spellcheck="false" autocapitalize="off"></label><button type="submit" id="connect">连接 / 更新令牌</button><button type="button" id="stop" disabled>停止并清除</button></form>
<p id="status" role="status" aria-live="polite">尚未连接。显式启用无鉴权模式时可留空连接。</p>
<pre id="j">连接后显示采集状态。</pre>
</main><script>
'use strict';
const form=document.getElementById('auth'), input=document.getElementById('token');
const status=document.getElementById('status'), output=document.getElementById('j');
const stopButton=document.getElementById('stop');
let token='', generation=0, timer=null, pending=null;
function clearSession(){generation++;clearTimeout(timer);timer=null;if(pending)pending.abort();pending=null;token='';input.value='';output.textContent='连接后显示采集状态。';stopButton.disabled=true;}
async function tick(gen){
  if(gen!==generation)return;
  const controller=new AbortController();pending=controller;
  const timeout=setTimeout(()=>controller.abort(),10000);
  try{
    const headers=token?{'X-Token':token}:{};
    const response=await fetch('/api/v1/status',{headers,cache:'no-store',credentials:'omit',redirect:'error',signal:controller.signal});
    if(gen!==generation)return;
    if(response.status===401||response.status===403){clearSession();status.textContent='鉴权失败：请核对令牌后重新连接。';return;}
    if(!response.ok)throw new Error('http');
    const data=await response.json();
    if(!data||typeof data!=='object'||Array.isArray(data)||typeof data.host!=='string')throw new Error('shape');
    if(gen!==generation)return;
    status.textContent=data.host+' · 最近读取 '+new Date().toLocaleTimeString();
    output.textContent=JSON.stringify(data,null,2);
  }catch(error){
    if(gen!==generation)return;
    output.textContent='本次未取得有效状态；上次内容已清除。';
    status.textContent='读取失败或超时。请检查服务与网络；稍后自动重试。';
  }finally{
    clearTimeout(timeout);
    if(gen===generation){pending=null;timer=setTimeout(()=>tick(gen),1000);}
  }
}
form.addEventListener('submit',event=>{event.preventDefault();const next=input.value;clearSession();token=next;stopButton.disabled=false;status.textContent='正在读取…';tick(generation);});
stopButton.addEventListener('click',()=>{clearSession();status.textContent='已停止，当前页面令牌已清除。';});
window.addEventListener('pagehide',clearSession);
</script></html>
"""


class Handler(BaseHTTPRequestHandler):
    server_version = "fnos-agent/1.0"
    protocol_version = "HTTP/1.1"
    # HTTP/1.1 keep-alive 必须配空闲超时：默认 None 时，板子 WiFi 掉线留下的半开
    # 连接会永久阻塞在 readline 上，线程 + fd 只增不减，最终 accept 失败、端点不可用。
    # 15 s 远大于板子最坏退避（5 s），正常 keep-alive 不会被误杀。
    timeout = 15

    def log_message(self, fmt, *args):                            # 静音，避免刷日志
        pass

    def handle_error(self, *args):                                # 客户端中断不刷 traceback
        pass

    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        try:                                                      # 头与体都可能写失败
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except OSError:
            self.close_connection = True

    def _authorized(self):
        tok = getattr(self.server, "token", "")
        if not tok:
            return bool(getattr(self.server, "insecure_no_auth", False))
        if hmac.compare_digest(self.headers.get("X-Token", "").encode("utf-8"), tok.encode("utf-8")):
            return True
        q = self.path.split("?", 1)[1] if "?" in self.path else ""
        for kv in q.split("&"):
            k, _, v = kv.partition("=")
            if k == "token" and hmac.compare_digest(v.encode("utf-8"), tok.encode("utf-8")):
                return True
        return False

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        col = self.server.collector
        if path in ("/api/v1/status", "/status"):
            if not self._authorized():
                return self._send(401, '{"error":"unauthorized"}')
            with col.lock:
                snap = col.snapshot
            return self._send(200, json.dumps(snap, separators=(",", ":"), ensure_ascii=False))
        if path in ("/api/v1/history", "/history"):
            if not self._authorized():
                return self._send(401, '{"error":"unauthorized"}')
            # 按 ?n= 截断（审计 M32）：开发板恒定请求 ?n=600（components/fnos_monitor/
            # fnos_data.c 的历史回填），而这里以前把整条缓冲全发出去。缓冲超过 ~930 行
            # 时响应体就超过开发板 HIST_BUF_SIZE(32 KB) → 被截断 → cJSON 解析失败 →
            # 回填永远不成功（曲线一直空白，每秒重试）。口径与 FPK 伴侣一致：
            # nas_companion_server.py 里是 rows = rows[-want:]。
            want = 0
            q = self.path.split("?", 1)[1] if "?" in self.path else ""
            for kv in q.split("&"):
                k, _, v = kv.partition("=")
                if k == "n":
                    try:
                        want = max(0, int(v))
                    except ValueError:
                        want = 0
            with col.lock:
                hist = list(col.history)
            if want and want < len(hist):
                hist = hist[-want:]
            return self._send(200, json.dumps({"v": 1, "cols": ["ts", "cpu", "mem", "rx_kbs", "tx_kbs", "temp_max"],
                                               "rows": hist}, separators=(",", ":")))
        if path in ("/api/v1/health", "/health"):
            with col.lock:
                snap = col.snapshot
            return self._send(200, json.dumps(
                {"ok": bool(snap.get("ready")), "ts": snap.get("ts", 0),
                 "age_s": max(0, int(time.time()) - int(snap.get("ts", 0) or 0)),
                 "host": snap.get("host", "")}, separators=(",", ":")))
        if path == "/":
            return self._send(200, INDEX_HTML, "text/html; charset=utf-8")
        return self._send(404, '{"error":"not found"}')


class Server(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True
    request_queue_size = 64

    # Reserve the whole connection before creating its worker.
    connection_limit = 32
    connection_timeout_s = 30.0
    drain_timeout_s = 5.0

    def __init__(self, *args, **kwargs):
        self._connection_slots = threading.BoundedSemaphore(self.connection_limit)
        self._requests_done = threading.Condition()
        self._requests = set()
        self._closing = False
        self._connection_rejected = 0
        super().__init__(*args, **kwargs)

    def process_request(self, request, client_address):
        with self._requests_done:
            if self._closing or not self._connection_slots.acquire(blocking=False):
                self._connection_rejected += 1
                self.shutdown_request(request)
                return
            self._requests.add(request)
            try:
                super().process_request(request, client_address)
            except Exception:
                # Interrupts can arrive after the worker starts. Leave its
                # registration for the worker/drain instead of releasing twice.
                self._requests.remove(request)
                self._connection_slots.release()
                self._requests_done.notify_all()
                self.shutdown_request(request)
                raise

    def process_request_thread(self, request, client_address):
        timer = None
        def expire():
            try:
                request.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        try:
            # Absolute connection budget: trickling bytes cannot reset it.
            timer = threading.Timer(self.connection_timeout_s, expire)
            timer.daemon = True
            timer.start()
            super().process_request_thread(request, client_address)
        except Exception:
            self.handle_error(request, client_address)
        finally:
            try:
                if timer is not None:
                    timer.cancel()
                    if timer.ident is not None:
                        timer.join()
            finally:
                try:
                    self.shutdown_request(request)
                finally:
                    with self._requests_done:
                        self._requests.discard(request)
                        self._connection_slots.release()
                        self._requests_done.notify_all()

    def server_close(self):
        with self._requests_done:
            self._closing = True
            for request in self._requests:
                try:
                    request.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        super().server_close()
        deadline = min(time.monotonic() + self.drain_timeout_s,
                       getattr(self, "shutdown_deadline", float("inf")))
        with self._requests_done:
            while self._requests:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("legacy request workers did not stop")
                self._requests_done.wait(remaining)

    def handle_error(self, request, client_address):
        # Expected disconnects have no traceback or request/token logging.
        if not isinstance(sys.exc_info()[1], OSError):
            super().handle_error(request, client_address)

    def server_bind(self):
        # HTTPServer.server_bind() 会做一次 socket.getfqdn() 反向 DNS，解析不可达时
        # 能阻塞几十秒 —— 那段时间监听 socket 还没建立，而 systemd 已认为服务 active。
        # server_name 本服务根本不用，直接跳过。
        socketserver.TCPServer.server_bind(self)
        self.server_name = self.server_address[0]
        self.server_port = self.server_address[1]


def main():
    ap = argparse.ArgumentParser(description="fnOS read-only status agent")
    ap.add_argument("--bind", default=os.environ.get("FNAS_BIND", "0.0.0.0"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("FNAS_PORT", "8799")))
    ap.add_argument("--interval", type=float, default=float(os.environ.get("FNAS_INTERVAL", "1.0")))
    ap.add_argument("--hist", type=int, default=int(os.environ.get("FNAS_HIST", "300")))
    ap.add_argument("--insecure-no-auth", action="store_true",
                    help="明确关闭鉴权（仅本机排障用）。默认没有 FNAS_TOKEN 就拒绝启动")
    ap.add_argument("--selftest", action="store_true",
                    help="采集一次并打印 JSON（出错时逐段定位），不启动服务")
    ap.add_argument("--temps", action="store_true",
                    help="只跑温度枚举并按「设备名 / 通道 / 数值」打表：只读、不启服务、"
                         "不写任何文件。用来在正式部署前先看清这一版会怎么命名")
    args = ap.parse_args()
    if not math.isfinite(args.interval) or args.interval <= 0:
        ap.error("--interval 必须为有限正数")
    if not 0 <= args.port <= 65535:
        ap.error("--port 必须在 0~65535 内")

    netif = os.environ.get("FNAS_NETIF") or None
    vols = [v.strip() for v in (os.environ.get("FNAS_VOLUMES") or "").split(",") if v.strip()]
    # 历史缓冲给个上界（审计 M32）：以前 --hist 想给多大给多大，缓冲会一直涨。
    # 上下限与 FPK 伴侣同一组（nas_companion_server.py 的 hist_len 30~3600）。
    hist_len = max(30, min(3600, args.hist))
    if hist_len != args.hist:
        sys.stderr.write("警告：--hist %d 超出 30~3600，按 %d 处理\n" % (args.hist, hist_len))
    col = Collector(interval=args.interval, netif=netif, volumes=vols, hist_len=hist_len)

    if args.temps:
        rows = col._temps()
        print("%-28s %-12s %7s  %s" % ("设备名(dn)", "通道(ch)", "°C", "内核名(dev)"))
        print("-" * 68)
        for t in rows:
            print("%-28s %-12s %7.1f  %s" % (t.get("dn") or t["dev"], t.get("ch", ""),
                                             t["c"], t["dev"]))
        print("-" * 68)
        print("共 %d 路（>=75°C 危险 %d 路、>=60°C 注意 %d 路）"
              % (len(rows), sum(1 for t in rows if t["c"] >= 75),
                 sum(1 for t in rows if 60 <= t["c"] < 75)))
        return 0

    if args.selftest:
        import traceback
        try:
            print(json.dumps(col.sample(), ensure_ascii=False, indent=1))
            return 0
        except Exception:
            traceback.print_exc()
            print("--- 逐段定位 ---", file=sys.stderr)
            for name in ("cpu", "mem", "net", "volumes", "raid", "disk_io", "temps",
                         "docker", "zfs"):
                fn = getattr(col, "_" + name)
                try:
                    print("  %-8s OK   %s" % (name, str(fn())[:110]), file=sys.stderr)
                except Exception as exc:
                    print("  %-8s FAIL %s: %s" % (name, type(exc).__name__, exc), file=sys.stderr)
            return 1

    # 默认 fail closed（审计 M31）：8799 上是这台 NAS 的只读状态——卷名/容量、容器与
    # 镜像、网卡与 IP、温度、主机名、300 条历史。以前 FNAS_TOKEN 为空就等于**关掉鉴权**
    # （_authorized() 直接 return True），而随包发布的 unit 是 --bind 0.0.0.0 且没有
    # Environment=，装完就是"局域网任意主机可读"。现在没有令牌就拒绝启动。
    token = os.environ.get("FNAS_TOKEN", "").strip()
    if not token and not args.insecure_no_auth:
        sys.stderr.write(
            "拒绝启动：没有可用的访问令牌（FNAS_TOKEN 为空）。\n"
            "  8799 暴露的是整台 NAS 的只读状态，默认必须带令牌才对外服务。\n"
            "  正常安装：nas/install.sh → nas/install-remote.sh 会在 NAS 上生成\n"
            "            /etc/fnos-agent.env（0600，root:root，含 FNAS_TOKEN），\n"
            "            随包 unit 用 EnvironmentFile=-/etc/fnos-agent.env 读它，\n"
            "            由 systemd 启动即可；令牌不会打印，要看用 sudo grep 读文件。\n"
            "  手工启动：FNAS_TOKEN=\"$(python3 -c 'import secrets;print(secrets.token_hex(24))')\" \\\n"
            "            python3 fnos-agent.py --bind 127.0.0.1\n"
            "  确实只在可信网络里排障、且接受任何人可读 8799：加 --insecure-no-auth。\n")
        return 2
    if not token:
        sys.stderr.write("警告：--insecure-no-auth 生效，8799 上的状态对任何能连上的人可读。\n")

    # 先绑定端口再等第一次采样：systemd 报 active 的时刻端口就应该能连上
    # （否则重启后的头两秒是"服务 active 但连接被拒"）。
    srv = None
    result = 0
    previous_term = None
    signal_installed = False
    def terminate(signum, frame):
        raise KeyboardInterrupt
    try:
        if threading.current_thread() is threading.main_thread():
            previous_term = signal.signal(signal.SIGTERM, terminate)
            signal_installed = True
        srv = Server((args.bind, args.port), Handler)
        srv.collector = col
        srv.token = token
        srv.insecure_no_auth = bool(args.insecure_no_auth)
        col.start()
        time.sleep(min(2.0, args.interval * 2))
        sys.stderr.write("fnos-agent listening on %s:%d (interval=%.2fs, pid=%d)\n"
                         % (args.bind, args.port, args.interval, os.getpid()))
        sys.stderr.flush()
        srv.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    except Exception as exc:
        sys.stderr.write("fnos-agent startup/runtime failed: %s\n" % type(exc).__name__)
        result = 1
    finally:
        shutdown_deadline = time.monotonic() + 8.0
        try:
            if srv is not None:
                srv.shutdown_deadline = shutdown_deadline
                srv.server_close()
        except Exception as exc:
            sys.stderr.write("fnos-agent listener close failed: %s\n" % type(exc).__name__)
            result = 1
        try:
            if not col.stop(timeout=max(0.0, shutdown_deadline - time.monotonic())):
                sys.stderr.write("fnos-agent sampler did not stop within deadline\n")
                result = 1
        except Exception as exc:
            sys.stderr.write("fnos-agent sampler stop failed: %s\n" % type(exc).__name__)
            result = 1
        if signal_installed:
            signal.signal(signal.SIGTERM, previous_term)
    return result


if __name__ == "__main__":
    sys.exit(main() or 0)
