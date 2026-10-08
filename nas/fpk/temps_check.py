#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""温度全量枚举：在一棵**假 sysfs 树**上跑真的采集代码。

为什么非要搭假树：这段逻辑的价值全在"谁和谁是同一个设备"——
  * NVMe 的 hwmon 挂在 PCI 控制器上，而 block 设备的 device 在它下面一层
    （…/0000:03:00.0/nvme/nvme0），所以匹配必须认前缀，不能只比相等；
  * 网卡 hwmon 的名字就是 netdev 名（enp1s0），要用 /sys/class/net 反查；
  * 分区（nvme0n1p1）不是独立设备，必须跳过，否则整盘名会被分区名顶掉；
  * CPU 温标没有任何 block/net/drm 认领，只能靠通道标签（Package / Core N / Tctl）认。

macOS 上没有 /sys，而这台开发机又进不去 NAS（SSH 要密码）——不搭假树，就只能
"看着像对"。所以这里用 FNAS_SYSFS 把真树的形状搬到临时目录：PCI → nvme/nvme0 →
nvme0n1（带分区）、PCI → net/enp1s0、PCI → drm/card0、platform/coretemp.0、
platform/acpitz.0，再把断言写在"应该报出哪几路、叫什么、谁最热"上。

证伪方式（改代码后必须能把这些断言弄红）：
  * 把 `_pick_device()` 的前缀包含改成相等比较 → nvme0n1 那条立刻红；
  * 删掉 `_dev_map()` 里的 partition 跳过 → 候选里出现分区名，红；
  * 把"每设备只取 temp1"的老逻辑拿回来 → PHY/MAC、Core N 全部消失，红。

用法：python3 nas/fpk/temps_check.py
退出码非 0 表示自适应命名或通道枚举不符合预期。
"""

import importlib.util
import io
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
AGENT = os.path.join(REPO, "nas", "fnos-agent.py")
COLLECTOR = os.path.join(HERE, "nasscreencompanion", "app", "server", "fnos_collector.py")

FAILS = []
PASSES = [0]


def check(cond, what):
    if cond:
        PASSES[0] += 1
    else:
        FAILS.append(what)
    print(("  ✓ " if cond else "  ✗ ") + what)


# ------------------------------------------------------------------ 假树工具
# 链接目标一律写成相对路径（和真 /sys 一样）：相对前缀按"链接所在目录"算，
# 从 <root>/class/<类>/ 出发到设备是 ../../devices/... —— 写错一层就会静默指到
# 根本不存在的路径，而 realpath 不会报错，只会让匹配失败（这正是要测的东西）。

def mkdir(root, *parts):
    p = os.path.join(root, *parts)
    os.makedirs(p, exist_ok=True)
    return p


def write(root, text, *parts):
    p = os.path.join(root, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with io.open(p, "w", encoding="utf-8") as f:
        f.write(text)
    return p


def link(root, target, *parts):
    p = os.path.join(root, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    if os.path.islink(p) or os.path.exists(p):
        os.remove(p)
    os.symlink(target, p)
    return p


def hwmon(root, dev_dir, idx, name, chans):
    """在 devices/<dev_dir> 下建 hwmon<idx> 并挂到 <root>/class/hwmon。

    chans = [(通道序号, 标签或 None, 毫摄氏度)]；标签为 None 就不写 tempN_label。
    """
    hp = "devices/%s/hwmon/hwmon%d" % (dev_dir, idx)
    mkdir(root, hp)
    link(root, "../..", hp, "device")                     # hwmonN/device → 所属设备
    write(root, name, hp, "name")
    for n, label, milli in chans:
        write(root, str(milli), hp, "temp%d_input" % n)
        if label:
            write(root, label, hp, "temp%d_label" % n)
    link(root, "../../" + hp, "class/hwmon", "hwmon%d" % idx)


def build_nas_tree(root):
    """照真机（飞牛 NAS）的形状搭：CPU + 网卡 + iGPU + 2 块 NVMe + 一个 acpitz。"""
    # ── CPU：平台设备，没有 block/net/drm 认领，只有通道标签能认出来
    mkdir(root, "devices/platform/coretemp.0")
    hwmon(root, "platform/coretemp.0", 0, "coretemp",
          [(1, "Package id 0", 34000)] + [(2 + i, "Core %d" % i, 33000 + i * 100) for i in range(6)])

    # ── 网卡：PCI 0000:01:00.0，hwmon 名就是 netdev 名
    nic = "pci0000:00/0000:00:1c.0/0000:01:00.0"
    mkdir(root, "devices", nic, "net/enp1s0")
    link(root, "../..", "devices", nic, "net/enp1s0", "device")       # → PCI 设备
    mkdir(root, "bus/pci/drivers/atlantic")
    link(root, "../../../../bus/pci/drivers/atlantic", "devices", nic, "driver")
    link(root, "../../devices/" + nic + "/net/enp1s0", "class/net", "enp1s0")
    # 人读设备名要的那几件事实：PCI 厂商/型号号 + 链路速率
    write(root, "0x1d6a", "devices", nic, "vendor")
    write(root, "0x0001", "devices", nic, "device")
    write(root, "10000", "class/net", "enp1s0", "speed")
    hwmon(root, nic, 2, "enp1s0", [(1, "PHY Temperature", 74200), (2, "MAC Temperature", 74200)])

    # ── iGPU：DRM 控制器，设备名取驱动名
    gpu = "pci0000:00/0000:00:02.0"
    mkdir(root, "devices", gpu, "drm/card0")
    link(root, "../..", "devices", gpu, "drm/card0", "device")
    mkdir(root, "bus/pci/drivers/i915")
    link(root, "../../../../bus/pci/drivers/i915", "devices", gpu, "driver")
    link(root, "../../devices/" + gpu + "/drm/card0", "class/drm", "card0")
    write(root, "0x8086", "devices", gpu, "vendor")
    write(root, "0x46d0", "devices", gpu, "device")
    hwmon(root, gpu, 4, "i915", [(1, None, 35000)])

    # ── 两块 NVMe：hwmon 在 PCI 上，block 设备在它下面一层；每块带一个分区
    for k, (slot, blk, hw) in enumerate((("0000:03:00.0", "nvme0n1", 3),
                                         ("0000:08:00.0", "nvme1n1", 5))):
        ctrl = blk[:-1]                                   # nvme0
        pci = "pci0000:00/0000:00:1d.%d/%s" % (k, slot)
        ns = "%s/nvme/%s/%s" % (pci, ctrl, blk)           # …/nvme0/nvme0n1
        mkdir(root, "devices", ns)
        link(root, "..", "devices", ns, "device")          # 命名空间 → 控制器
        part = ns + "p1"
        mkdir(root, "devices", part)
        write(root, "1", "devices", part, "partition")     # 分区标记：必须被跳过
        link(root, "..", "devices", part, "device")
        link(root, "../../devices/" + ns, "class/block", blk)
        link(root, "../../devices/" + part, "class/block", blk + "p1")
        # 盘型号：真实机器上在控制器目录里（/sys/class/block/nvme0n1/device/model）
        write(root, "Samsung SSD 990 PRO 2TB" if k == 0 else "WD Black SN850X 2TB",
              "devices", pci, "nvme", ctrl, "model")
        hwmon(root, pci, hw, "nvme",
              [(1, "Composite", 43900 - k * 1000),
               (2, "Sensor 1", 41900 - k * 1000),
               (3, "Sensor 2", 40900 - k * 1000)])

    # ── 主板热区：没有标签、也没有 block/net/drm 认领 → 用 hwmon 名；temp2 读不到
    mkdir(root, "devices/platform/acpitz.0")
    hwmon(root, "platform/acpitz.0", 6, "acpitz", [(1, None, 28000), (2, None, 0)])


def build_cap_tree(root):
    """只有一个 60 通道的芯片：验证低温与高温通道都不会因设备数量被省略。"""
    mkdir(root, "devices/platform/ipmi.0")
    hwmon(root, "platform/ipmi.0", 0, "ipmi",
          [(n, "temp%d" % n, 1000 * n) for n in range(1, 61)])


# ------------------------------------------------------------------ 加载模块

def load(path, name):
    sys.dont_write_bytecode = True          # 别在包目录里留 __pycache__
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def temps_of(module, root, proc):
    """按真实调用路径跑一次 _temps()（不走 __init__，避免起采样线程）。"""
    module.SYSFS = root
    module.PROC = proc
    obj = module.Collector.__new__(module.Collector)
    obj._dev_cache = None
    obj._pci_cache = {}          # 这两个缓存平时在 __init__ 里建；绕过它就得自己给
    obj._cpu_name = None
    obj._totals = {}
    return obj, obj._temps()


def run_phase(label, module, root, expect, proc, env=None):
    saved = {k: os.environ.get(k) for k in (env or {})}
    os.environ.update(env or {})
    try:
        obj, out = temps_of(module, root, proc)
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    pairs = [(t["dev"], t["ch"]) for t in out]
    print("\n[%s] %s：%d 路" % (label, os.path.basename(root), len(out)))
    for want in expect["have"]:
        check(want in pairs, "%s → %s 在列表里" % (want[0], want[1]))
    for bad in expect.get("absent", []):
        check(bad not in pairs, "%s → %s 不应出现" % (bad[0], bad[1]))
    if expect.get("dn"):
        got = {(t["dev"], t["ch"]): t["dn"] for t in out}
        for key, want in expect["dn"].items():
            check(got.get(key) == want,
                  "%s → %s 的设备名 = 「%s」（实际「%s」）" % (key[0], key[1], want, got.get(key)))
    if expect.get("first"):
        got = " → ".join(pairs[0]) if pairs else "空"
        check(pairs[:1] == [expect["first"]],
              "最热的一路是 %s → %s（实际 %s）" % (expect["first"][0], expect["first"][1], got))
    if expect.get("ntotal"):
        check(len(out) == expect["ntotal"], "通道数 = %d（实际 %d）" % (expect["ntotal"], len(out)))
    if expect.get("cpu_temp") is not None:
        check(obj._cpu_temp == expect["cpu_temp"],
              "cpu.temp_c = %s（实际 %s）" % (expect["cpu_temp"], obj._cpu_temp))
    if expect.get("sorted"):
        vals = [t["c"] for t in out]
        check(vals == sorted(vals, reverse=True), "按温度降序")
    if expect.get("cap") is not None:
        check(len(out) == expect["cap"], "完整保留 %d 路（实际 %d）" % (expect["cap"], len(out)))
    if expect.get("at_most") is not None:
        check(len(out) <= expect["at_most"],
              "不超过 %d 路（实际 %d）" % (expect["at_most"], len(out)))
    check(len(set(pairs)) == len(pairs), "没有重复的 设备+通道")
    check(all(t["n"] == ("%s %s" % (t["dev"], t["ch"]))[:32] for t in out),
          "兼容字段 n = \"设备 通道\"")
    check(not any(nm.endswith("p1") for _, nm, _kind in obj._dev_map()), "候选设备里没有分区名")
    return out


def dump_payload():
    """把假树上的 temps 段原样打出来：这就是板子会收到的线上格式。

    板子的接收缓冲是 24 KB（fnos_data.c 的 RX_BUF_SIZE），所以这里同时报字节数 ——
    采集端每加一个字段都要能一眼看出"payload 还装不装得下"。
    """
    import json
    root = tempfile.mkdtemp(prefix="temps-dump-")
    proc = tempfile.mkdtemp(prefix="temps-dump-proc-")
    try:
        build_nas_tree(root)
        write(proc, "model name\t: Intel(R) N100\n", "cpuinfo")
        m = load(COLLECTOR, "nsc_dump")
        _, out = temps_of(m, root, proc)
        body = json.dumps(out, ensure_ascii=False, separators=(",", ":"))
        print(body)
        print("\n%d 路 · temps 段 %d 字节 · 单路平均 %.0f 字节"
              % (len(out), len(body.encode("utf-8")), len(body.encode("utf-8")) / max(1, len(out))))
        return 0
    finally:
        shutil.rmtree(root, ignore_errors=True)
        shutil.rmtree(proc, ignore_errors=True)


def main():
    if "--dump" in sys.argv:
        return dump_payload()
    root = tempfile.mkdtemp(prefix="temps-check-")
    cap_root = tempfile.mkdtemp(prefix="temps-cap-")
    proc = tempfile.mkdtemp(prefix="temps-proc-")
    try:
        build_nas_tree(root)
        build_cap_tree(cap_root)
        # 假的 /proc：CPU 型号名是"人读设备名"里 CPU 那一路的唯一来源
        write(proc, "processor\t: 0\nmodel name\t: Intel(R) N100\n", "cpuinfo")
        # 迷你 pci.ids：验"厂商 + 型号"这条路径 —— 真机上就是靠它把 1d6a:0001 认成
        # AQC113CS；没有它就只能退回厂商表 + 驱动名（上面 full 那一组测的就是退路）。
        pci_ids = os.path.join(proc, "pci.ids")
        io.open(pci_ids, "w", encoding="utf-8").write(
            "1d6a  Aquantia Corp.\n\t0001  AQC113CS\n"
            "8086  Intel Corporation\n\t46d0  CoffeeLake-S GT2 [UHD Graphics 630]\n")
        # 17 路 = acpitz 1 + 网卡 2 + iGPU 1 + 两块 NVMe 各 3 + coretemp 7
        full = {
            "have": [("enp1s0", "PHY"), ("enp1s0", "MAC"),
                     ("coretemp.0", "Package id 0"), ("coretemp.0", "Core 0"), ("coretemp.0", "Core 5"),
                     ("i915", "temp1"),
                     ("nvme0n1", "Composite"), ("nvme0n1", "Sensor 1"), ("nvme0n1", "Sensor 2"),
                     ("nvme1n1", "Composite"), ("nvme1n1", "Sensor 2"),
                     ("acpitz.0", "temp1")],
            "absent": [("acpitz.0", "temp2"), ("nvme0n1p1", "Composite"), ("NIC", "PHY")],
            # 人读设备名：磁盘取型号、网卡取"厂商 + 驱动 + 速率"（假树里没有 pci.ids，
            # 所以厂商表兜底到 Marvell，型号名用驱动名 atlantic）、CPU 取 /proc/cpuinfo、
            # 显卡取"厂商 + 驱动"、认不出的热区退回 hwmon 名。
            "dn": {
                ("nvme0n1", "Composite"): "Samsung SSD 990 PRO 2TB",
                ("nvme1n1", "Sensor 2"): "WD Black SN850X 2TB",
                ("enp1s0", "PHY"): "Marvell atlantic 10GbE",
                ("coretemp.0", "Core 3"): "Intel N100",
                ("i915", "temp1"): "Intel i915",
                ("acpitz.0", "temp1"): "acpitz",
            },
            "first": ("enp1s0", "PHY"),
            "ntotal": 17,
            "cpu_temp": 34.0,
            "sorted": True,
            "at_most": 48,
        }
        capped = {
            "have": [("ipmi.0", "temp60"), ("ipmi.0", "temp1")],
            "absent": [],
            "first": ("ipmi.0", "temp60"),
            "cap": 60, "sorted": True,
        }
        # 有 pci.ids 时：型号名来自文件（AQC113CS / UHD Graphics），不再用驱动名兜底
        withpci = {
            "have": [("enp1s0", "PHY"), ("i915", "temp1")],
            "dn": {("enp1s0", "PHY"): "Aquantia AQC113CS 10GbE",
                   ("i915", "temp1"): "Intel UHD Graphics 630"},
            "at_most": 48, "sorted": True,
        }
        for label, path, name in (("agent", AGENT, "fnas_agent"),
                                  ("collector", COLLECTOR, "nsc_temps")):
            if not os.path.exists(path):
                print("找不到 %s" % path)
                return 2
            m = load(path, name)
            run_phase(label, m, root, full, proc)
            run_phase(label + "/pci", m, root, withpci, proc, env={"FNAS_PCI_IDS": pci_ids})
            run_phase(label + "/cap", m, cap_root, capped, proc)
    finally:
        shutil.rmtree(root, ignore_errors=True)
        shutil.rmtree(cap_root, ignore_errors=True)
        shutil.rmtree(proc, ignore_errors=True)

    print("\n结果：%d 项通过，%d 项失败" % (PASSES[0], len(FAILS)))
    for f in FAILS:
        print("  ✗ %s" % f)
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
