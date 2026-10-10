#!/usr/bin/env python3
"""RAID 采集回归：把审计 R1–R5 钉成可执行的断言（本机可复现）。

为什么需要它：这几条问题的共同点是"代码看上去对、跑起来也不报错，只是结论反了"。
比如读不到 /proc/mdstat 时返回空数组，板子上就显示"未发现阵列"——比报错更糟，
因为错误看起来像事实。这类东西只能拿真实 mdstat 形态的 fixture 断言输出。

审计报告自述的"RAID 20 项回归"在仓库里并不存在（见
docs/review-v1.2.5-audit-2026-10-10.md §0），这份是本机可复现的对应物。

用法：
    python3 tools/audit125/test_raid.py          # 退出码 0 = 全过
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
COLLECTOR = os.path.join(REPO, "nas", "fpk", "nasscreencompanion", "app", "server",
                         "fnos_collector.py")


def _load_collector():
    spec = importlib.util.spec_from_file_location("fnos_collector", COLLECTOR)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MOD = _load_collector()

# ---------------------------------------------------------------- fixture
# 全部按内核 mdstat 的真实排版：首行是阵列头，后面的行缩进且属于上一个阵列。
HEALTHY_RAID1 = """\
Personalities : [raid1] [raid6] [raid5] [raid4]
md0 : active raid1 sda1[0] sdb1[1]
      1953382464 blocks super 1.2 [2/2] [UU]

unused devices: <none>
"""

# 审计 R1 的现场：降级（[2/1] [U_]）且正在 recovery
DEGRADED_RECOVERING = """\
md0 : active raid1 sda1[0]
      1953382464 blocks super 1.2 [2/1] [U_]
      [>....................]  recovery = 20.5% (200000000/976691200) finish=120.0min speed=100000K/sec
"""

DEGRADED_IDLE = """\
md0 : active raid1 sda1[0]
      1953382464 blocks super 1.2 [2/1] [U_]
"""

# 审计 R3：inactive 里含 "active" 子串
INACTIVE = """\
md127 : inactive sdb1[1](S)
      1953382464 blocks super 1.2
"""

# 审计 R5：RAID0/linear 没有冗余，内核不打 [n/m] [UU] 汇总行
RAID0 = """\
md1 : active raid0 sdc1[0] sdd1[1]
      3906764928 blocks super 1.2 512k chunks
"""

LINEAR = """\
md2 : active linear sde1[0]
      3906764928 blocks super 1.2
"""

# 冗余阵列缺汇总行：不能当成健康，也不能当成降级
RAID5_NO_SUMMARY = """\
md3 : active raid5 sdf1[0] sdg1[1] sdh1[2]
      3906764928 blocks super 1.2 level 5, 512k chunk
"""

CONTAINER = """\
md127 : active container imsm 00000000:00000000
"""

READONLY = """\
md0 : active (read-only) raid1 sda1[0] sdb1[1]
      1953382464 blocks super 1.2 [2/2] [UU]
"""

NO_ARRAY = """\
Personalities : [raid1]
unused devices: <none>
"""

BAD_PCT = """\
md0 : active raid1 sda1[0] sdb1[1]
      1953382464 blocks super 1.2 [2/2] [UU]
      [====>...............]  check = 120.0% (1/2) finish=1.0min speed=1K/sec
"""


def collector(mdstat=None, exc=None, limits=None):
    """一个只接 fixture 的采集器：read_text_strict 是 _raid 唯一的读取入口。"""
    c = MOD.CompanionCollector(limits=limits)
    c._totals = {}

    def fake_read(path):
        if exc is not None:
            raise exc
        return mdstat

    MOD.read_text_strict = fake_read
    return c


def raid(mdstat, **kw):
    rows = collector(mdstat, **kw)._raid()
    return rows


def one(mdstat, **kw):
    rows = raid(mdstat, **kw)
    assert len(rows) == 1, "期望 1 个阵列，得到 %d 个" % len(rows)
    return rows[0]


def alerts(mdstat=None, snap_extra=None, limits=None, **kw):
    c = collector(mdstat, limits=limits, **kw)
    snap = {"raid": c._raid() if mdstat is not None else None}
    snap.update(snap_extra or {})
    return c._alerts(snap)


# ---------------------------------------------------------------- 断言
FAILED = []
COUNT = 0


def check(name, got, want):
    global COUNT
    COUNT += 1
    if got != want:
        FAILED.append("%s\n     实际: %r\n     期望: %r" % (name, got, want))


def lv(alerts_):
    return [a["lv"] for a in alerts_]


# R3 + 基本分类
check("健康 raid1 → health=ok", one(HEALTHY_RAID1)["health"], "ok")
check("健康 raid1 → ok=True", one(HEALTHY_RAID1)["ok"], True)
check("健康 raid1 → 无告警", alerts(HEALTHY_RAID1), [])

# R3：inactive 不能被 "active" 子串命中
check("inactive → health=inactive", one(INACTIVE)["health"], "inactive")
check("inactive → ok=False", one(INACTIVE)["ok"], False)
check("inactive → crit", lv(alerts(INACTIVE)), ["crit"])

# R1：降级 + 正在 recovery 仍然是 crit，且把进度写在同一条里
r1 = alerts(DEGRADED_RECOVERING)
check("降级恢复中 → crit（不是 info）", lv(r1), ["crit"])
check("降级恢复中 → 进度不丢", "20.5%" in r1[0]["m"], True)
check("降级恢复中 → health=degraded", one(DEGRADED_RECOVERING)["health"], "degraded")
check("降级无进度 → crit", lv(alerts(DEGRADED_IDLE)), ["crit"])

# R5：没有冗余的阵列不打汇总行，这是正常形态
check("raid0 无汇总行 → ok", one(RAID0)["ok"], True)
check("raid0 → 无告警", alerts(RAID0), [])
check("linear 无汇总行 → ok", one(LINEAR)["ok"], True)
check("raid5 缺汇总行 → unknown（不猜）", one(RAID5_NO_SUMMARY)["health"], "unknown")
check("raid5 缺汇总行 → info（不是 crit）", lv(alerts(RAID5_NO_SUMMARY)), ["info"])
check("container → info", lv(alerts(CONTAINER)), ["info"])
check("read-only → warn", lv(alerts(READONLY)), ["warn"])

# 进度范围检查
check("进度 120% → 不采信", one(BAD_PCT)["sync_pct"], -1.0)

# R2：读不到就抛，不能退化成"没有阵列"
try:
    raid(None, exc=PermissionError(13, "Permission denied"))
    check("mdstat 无权限 → 必须抛", "没抛", "PermissionError")
except PermissionError:
    check("mdstat 无权限 → 抛 PermissionError", True, True)
except Exception as exc:                                   # noqa: BLE001
    check("mdstat 无权限 → 抛", type(exc).__name__, "PermissionError")

# 真的没有阵列时，空列表是正常结果（不能因为"空"就抛）
check("无阵列 → 空列表不报错", raid(NO_ARRAY), [])

# R4：段为 None 时整张告警表不能消失
check("段全为 None → 空告警，不抛", alerts(None, snap_extra={"cpu": None, "mem": None,
                                                             "vols": None, "docker": None}), [])
r4 = alerts(DEGRADED_IDLE, snap_extra={"cpu": None, "mem": None, "vols": None})
check("cpu/mem 为 None 时阵列告警仍在", lv(r4), ["crit"])

# R1 的顺序：按严重度排序后再套 limits，截断不能先砍掉 crit
many = alerts(DEGRADED_IDLE,
              snap_extra={"vols": [{"mnt": "/", "pct": 85}], "mem": {"pct": 95}},
              limits={"alerts": 1})
check("alerts 限 1 条 → 留下 crit", lv(many), ["crit"])
ordered = alerts(DEGRADED_IDLE, snap_extra={"vols": [{"mnt": "/", "pct": 85}],
                                            "mem": {"pct": 95}})
check("告警按严重度排序", lv(ordered), ["crit", "warn", "warn"])

# R2 的"形状"面：采集失败也不能把 payload 的类型改坏。板子是按类型解析的，
# 首帧失败时曾经写出 "raid": null —— 包生命周期测试 7b 抓到的就是这个。
c0 = collector(None, exc=FileNotFoundError(2, "No such file or directory"))
snap0 = c0.sample()
check("首帧 mdstat 失败 → raid 仍是数组", isinstance(snap0.get("raid"), list), True)
check("首帧 mdstat 失败 → modules.raid 不是 ok",
      snap0["modules"]["raid"]["status"] != MOD.ST_OK, True)
check("首帧 mdstat 失败 → errors 点名 raid", "raid" in (snap0.get("errors") or []), True)
check("首帧失败 → 其它段形状不变",
      (isinstance(snap0.get("vols"), list), isinstance(snap0.get("cpu"), dict)), (True, True))
check("首帧失败 → 仍然 ready（失败如实写在 modules，不伪装成没数据）",
      snap0.get("ready"), True)

# ---------------------------------------------------------------- 汇总
if FAILED:
    print("FAIL %d/%d" % (len(FAILED), COUNT))
    for f in FAILED:
        print("  ✗ " + f)
    sys.exit(1)
print("PASS %d 项 RAID 回归全部通过" % COUNT)
