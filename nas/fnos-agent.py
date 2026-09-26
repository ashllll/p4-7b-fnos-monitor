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
  FNAS_TOKEN   非空时要求 ?token= 或 X-Token 头匹配（默认不校验）
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
        self.hostname = socket.gethostname()

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
            return {"pct": 0.0, "cores": os.cpu_count() or 1, "load1": 0.0, "load5": 0.0,
                    "load15": 0.0, "temp_c": None, "procs": 0}
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
        cur = self._net_counters()
        rx = tx = 0
        if iface in cur:
            rx, tx = cur[iface]
        rx_kbs = tx_kbs = 0.0
        prev = self._prev_net
        if prev and prev[0] == iface:
            # 单调钟：墙钟回拨会让 dt 变负、速率变成天文数字并写进曲线
            dt = max(1e-6, time.monotonic() - prev[3])
            rx_kbs = max(0.0, (rx - prev[1]) / dt / 1024.0)
            tx_kbs = max(0.0, (tx - prev[2]) / dt / 1024.0)
        self._prev_net = (iface, rx, tx, time.monotonic())
        gb = 1024.0 ** 3
        return {
            "if": iface,
            "rx_kbs": round(rx_kbs, 1), "tx_kbs": round(tx_kbs, 1),
            "rx_total_gb": round(rx / gb, 2), "tx_total_gb": round(tx / gb, 2),
        }

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
        return vols

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
        return out

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
        return rates[:10]

    # 标签保持 ASCII：板子端 LVGL 用的 Latin 字体，没有 CJK 字形
    TEMP_LABEL = {"coretemp": "CPU", "enp1s0": "NIC", "i915": "iGPU", "nvme": "NVMe"}

    CPU_HWMON = ("coretemp", "k10temp", "zenpower", "cpu_thermal", "acpitz", "cpu")

    def _temps(self):
        out = []
        self._cpu_temp = None
        base = "/sys/class/hwmon"
        try:
            entries = sorted(os.listdir(base))
        except OSError:
            return out
        for e in entries:
            hp = os.path.join(base, e)
            name = read_text(os.path.join(hp, "name")).strip()
            if not name:
                continue
            # NVMe 用设备名（nvme0..3）区分，其余用 hwmon 名
            label = self.TEMP_LABEL.get(name, name)
            if name == "nvme":
                try:
                    dev = os.path.basename(os.path.realpath(os.path.join(hp, "device")))
                    label = dev.upper() if dev.startswith("nvme") else "NVMe"
                except OSError:
                    label = "NVMe"
            try:
                idxs = sorted(int(re.match(r"temp(\d+)_input", f).group(1))
                              for f in os.listdir(hp) if re.match(r"temp\d+_input$", f))
            except (OSError, AttributeError):
                continue
            if not idxs:
                continue
            c = read_int(os.path.join(hp, "temp%d_input" % idxs[0]), -1) / 1000.0
            if c <= 0:
                continue
            if name in self.CPU_HWMON and self._cpu_temp is None:
                self._cpu_temp = round(c, 1)      # 供 cpu.temp_c 使用，不依赖显示标签
                label = "CPU"
            out.append({"n": label, "c": round(c, 1)})
        out.sort(key=lambda t: t["c"], reverse=True)
        return out

    def _docker(self):
        now = time.time()
        if now - self._docker_cache[0] < 5.0:
            return self._docker_cache[1]
        prev = self._docker_cache[1]
        result = prev
        try:
            status, body = _http_unix("/var/run/docker.sock",
                                      "/containers/json?all=1", timeout=4.0)
            if status != 200:
                raise RuntimeError("docker http %d" % status)
            rows = json.loads(body.decode("utf-8", "replace"))
            result = []
            for c in rows:
                names = c.get("Names") or [c.get("Id", "?")[:12]]
                result.append({
                    # 板子端 char n[24]/s[40]：超长会在那里被截断，这里先截好，
                    # 顺便把整帧大小压住（payload 超过板子 8 KB 缓冲会整帧作废）
                    "n": names[0].lstrip("/")[:23],
                    "up": c.get("State") == "running",
                    "s": (c.get("Status") or "")[:39],
                })
            result.sort(key=lambda x: (not x["up"], x["n"]))
            result = result[:16]
            self._docker_seen_up.update(c["n"] for c in result if c["up"])
        except Exception:
            result = prev          # 任何失败都保留上一份，绝不用空表覆盖（否则容器与告警一起消失）
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
        for c in snap.get("docker", []):
            # 只报"本来在跑、现在掉了"的容器；开机就停着的（如已弃用的 exporter）不刷告警
            if not c["up"] and c["n"] in self._docker_seen_up:
                out.append({"lv": "warn", "m": "container %s down" % c["n"]})
        return out[:8]

    def sample(self):
        # 逐段隔离：某一段抛异常时保留上一份的值并记录到 errors，
        # 而不是整帧变成 ready:false（板子对 ready:false 的处理是"保留上一帧"，
        # 结果是面板永久冻结在最后一帧，而 /health 还一直是 ok）。
        snap = {"v": 1, "host": self.hostname, "ts": int(time.time()), "ready": True}
        errors = []
        with self.lock:
            prev = dict(self.snapshot) if self.snapshot.get("ready") else {}
        for key, fn in (("cpu", self._cpu), ("mem", self._mem), ("net", self._net),
                        ("vols", self._volumes), ("raid", self._raid),
                        ("disks", self._disk_io), ("temps", self._temps),
                        ("docker", self._docker), ("zfs", self._zfs)):
            try:
                snap[key] = fn()
            except Exception as exc:                  # noqa: BLE001 - 采集段不能互相拖死
                errors.append(key)
                snap[key] = prev.get(key) if key in prev else None
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

def _http_unix(sock_path, path, timeout=3.0, method="GET"):
    """通过 unix socket 发一个极简 HTTP/1.0 请求（不依赖 requests / docker 包）。"""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect(sock_path)
        req = ("%s %s HTTP/1.0\r\nHost: docker\r\nAccept: application/json\r\n"
               "Connection: close\r\n\r\n" % (method, path)).encode("ascii")
        s.sendall(req)
        buf = bytearray()
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
            if len(buf) > 4 * 1024 * 1024:
                break
    finally:
        s.close()
    head, _, body = bytes(buf).partition(b"\r\n\r\n")
    lines = head.decode("latin1").split("\r\n")
    status = int(lines[0].split()[1]) if lines and len(lines[0].split()) > 1 else 0
    hdrs = {}
    for h in lines[1:]:
        k, _, v = h.partition(":")
        hdrs[k.strip().lower()] = v.strip()
    if hdrs.get("transfer-encoding", "").lower() == "chunked":
        body = _dechunk(body)
    return status, body


def _dechunk(data):
    out = bytearray()
    while True:
        pos = data.find(b"\r\n")
        if pos < 0:
            break
        try:
            size = int(data[:pos].split(b";")[0], 16)
        except ValueError:
            break
        if size == 0:
            break
        out += data[pos + 2:pos + 2 + size]
        data = data[pos + 2 + size + 2:]
    return bytes(out)


# ---------------------------------------------------------------- HTTP 服务

INDEX_HTML = """<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<title>fnos-agent</title>
<style>body{background:#0b0f14;color:#d8e2ee;font:14px/1.5 ui-monospace,Menlo,monospace;margin:0;padding:24px}
h1{font-size:16px;color:#6ee7ff;margin:0 0 12px}pre{white-space:pre-wrap;word-break:break-all}
.b{color:#8ef0a8}</style>
<h1>fnos-agent · <span class="b" id="h">…</span></h1><pre id="j">loading…</pre>
<script>
async function tick(){try{const r=await fetch('/api/v1/status');const j=await r.json();
document.getElementById('h').textContent=j.host+'  '+new Date().toLocaleTimeString();
document.getElementById('j').textContent=JSON.stringify(j,null,1);}catch(e){
document.getElementById('j').textContent='ERROR '+e;}}
tick();setInterval(tick,1000);
</script></html>"""


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
            return True
        if hmac.compare_digest(self.headers.get("X-Token", ""), tok):
            return True
        q = self.path.split("?", 1)[1] if "?" in self.path else ""
        for kv in q.split("&"):
            k, _, v = kv.partition("=")
            if k == "token" and hmac.compare_digest(v, tok):
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
            with col.lock:
                hist = list(col.history)
            return self._send(200, json.dumps({"v": 1, "cols": ["ts", "cpu", "mem", "rx_kbs", "tx_kbs"],
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
    ap.add_argument("--selftest", action="store_true",
                    help="采集一次并打印 JSON（出错时逐段定位），不启动服务")
    args = ap.parse_args()

    netif = os.environ.get("FNAS_NETIF") or None
    vols = [v.strip() for v in (os.environ.get("FNAS_VOLUMES") or "").split(",") if v.strip()]
    col = Collector(interval=args.interval, netif=netif, volumes=vols, hist_len=args.hist)

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

    # 先绑定端口再等第一次采样：systemd 报 active 的时刻端口就应该能连上
    # （否则重启后的头两秒是"服务 active 但连接被拒"）。
    srv = Server((args.bind, args.port), Handler)
    srv.collector = col
    srv.token = os.environ.get("FNAS_TOKEN", "")

    col.start()
    time.sleep(min(2.0, args.interval * 2))                        # 等第一次采样完成
    sys.stderr.write("fnos-agent listening on %s:%d (interval=%.2fs, pid=%d)\n"
                     % (args.bind, args.port, args.interval, os.getpid()))
    sys.stderr.flush()
    try:
        srv.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        col.stop()
        srv.server_close()


if __name__ == "__main__":
    sys.exit(main() or 0)
