#!/usr/bin/env python3
"""Complete inventories, explicit policy and independent interface rates."""
import importlib.util
import json
import sys
from pathlib import Path
from unittest.mock import patch
COLLECTOR = Path(__file__).parent / "nasscreencompanion/app/server/fnos_collector.py"

def main():
    sys.path.insert(0, str(COLLECTOR.parent))
    spec = importlib.util.spec_from_file_location("inventory_collector", COLLECTOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    obj = module.Collector.__new__(module.Collector)
    obj._totals = {}
    rows = list(range(257))
    for name in ("docker", "disks", "alerts", "vols", "raid", "temps", "interfaces"):
        assert obj._trim(name, rows) == rows, name
        assert obj._totals[name] == len(rows)
        obj.limits = {name: 4}
        assert obj._trim(name, rows) == rows[:4], name
        assert obj._totals[name] == len(rows)
        obj.limits[name] = 0
        assert obj._trim(name, rows) == [], name
        del obj.limits
    obj._pick_netif = lambda: "bond0"
    current = {"lo": (100,100), "bond0": (1000,2000), "enp10s0": (3000,4000)}
    obj._net_counters = lambda: current
    def collect(at):
        with patch.object(module.time, "monotonic", return_value=at), patch.object(module, "read_text", return_value="up"), patch.object(module, "read_int", return_value=2500):
            return obj._net()
    first = collect(10)
    assert [x["if"] for x in first["interfaces"]] == ["bond0", "enp10s0"]
    assert first["rx_kbs"] == 0
    current = {"bond0": (3048,6096), "enp10s0": (4024,4512), "vlan-long-identity": (900000,800000)}
    second = collect(12)
    interfaces = {x["if"]: x for x in second["interfaces"]}
    assert second["rx_kbs"] == 1 and second["tx_kbs"] == 2
    assert interfaces["enp10s0"]["rx_kbs"] == .5
    assert interfaces["vlan-long-identity"]["rx_kbs"] == 0
    current = {"enp10s0": (10,20)}
    third = collect(14)
    assert {k: third[k] for k in ("state", "speed_mbps", "physical")} == {
        "state": "", "speed_mbps": 0, "physical": False}
    assert len(third["interfaces"]) == 1
    assert third["interfaces"][0]["rx_kbs"] == 0
    obj.limits = {"interfaces": 0}
    assert collect(16)["interfaces"] == [] and obj._totals["interfaces"] == 1
    del obj.limits
    # 未枚举的FUSE子类型必须在statvfs之前过滤；只接触匿名夹具路径。
    from types import SimpleNamespace
    obj.vol_whitelist = set()
    mounts = "/dev/a /fixture-local ext4 rw 0 0\nremote /fixture-remote fuse.futurefs rw 0 0\n"
    def stat_fixture(path):
        assert path == "/fixture-local", "remote filesystem reached statvfs"
        return SimpleNamespace(f_blocks=100, f_frsize=1024**3, f_bavail=40, f_bfree=50)
    with patch.object(module, "read_text", return_value=mounts), patch.object(module.os, "statvfs", side_effect=stat_fixture) as stat:
        assert [v["mnt"] for v in obj._volumes()] == ["/fixture-local"]
        stat.assert_called_once_with("/fixture-local")
    history = module.Collector(hist_len=100)
    history.history = [[i, 0, 0, 0, 0] for i in range(100)]
    history.set_hist_len(45)
    assert history.hist_len == 45 and history.history == [[i, 0, 0, 0, 0] for i in range(55, 100)]
    history.set_hist_len(80)
    assert history.hist_len == 80 and len(history.history) == 45
    try:
        history.set_hist_len(0)
        raise AssertionError("invalid history policy accepted")
    except ValueError:
        assert history.hist_len == 80
    spec = importlib.util.spec_from_file_location("legacy_inventory_collector", COLLECTOR.parents[4] / "fnos-agent.py")
    legacy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(legacy)
    agent = legacy.Collector.__new__(legacy.Collector)
    agent._docker_cache = (0, [])
    agent._docker_seen_up = set()
    prefix = "shared-application-worker-prefix-"
    status = "Up with a complete health diagnostic " * 4
    body = json.dumps([{"Names": ["/" + prefix + suffix], "State": "running", "Status": status}
                       for suffix in ("alpha", "beta")] +
                      [{"Id": "abcdef0123456789" * 4, "State": "exited", "Status": status}]).encode()
    # Legacy collector uses wall time for this cache; patch that actual clock.
    with patch.object(legacy, "_http_unix", return_value=(200, body)) as request, patch.object(legacy.time, "time", return_value=10):
        containers = agent._docker()
        assert agent._docker_cache[0] == 10 and request.call_count == 1
        assert agent._docker() is containers and request.call_count == 1
        with patch.object(legacy.time, "time", return_value=16):
            assert agent._docker() == containers and request.call_count == 2
    assert {x["n"] for x in containers} == {prefix + "alpha", prefix + "beta", "abcdef0123456789" * 4}
    assert all(x["s"] == status for x in containers)
    assert agent._docker_seen_up == {prefix + "alpha", prefix + "beta"}

    # H5：zfs 是**对象型** —— 采集成功时也不能被强制成 []（以前它和列表型模块同组，
    # 于是屏上的 ZFS 区永远是空的）。列表型/对象型/失败三态一起钉住。
    norm = module.normalize_snapshot_types
    healthy = norm({"zfs": {"arc_gb": 12.5, "hit_pct": 91.0}, "vols": {}, "cpu": [],
                    "mem": None, "net": {}})
    assert healthy["zfs"] == {"arc_gb": 12.5, "hit_pct": 91.0}, healthy["zfs"]
    assert healthy["vols"] == [] and healthy["raid"] == [] and healthy["docker"] == []
    assert healthy["cpu"] == {} and healthy["mem"] == {} and healthy["net"] == {}
    assert norm({"zfs": []})["zfs"] is None      # 误当列表 → 纠正成"没读到"
    assert norm({"zfs": None})["zfs"] is None
    assert norm({})["zfs"] is None and norm({})["docker"] == []

    # CPU采集失败时的旧值不能因温度回填而被原地改写。
    from contextlib import ExitStack
    cc = module.CompanionCollector()
    old_cpu = {"temp_c": 31, "pct": 10}
    cc.snapshot = {"ready": True, "cpu": old_cpu}
    cc._module_meta = {"cpu": {"status": module.ST_OK, "ts": 1}}
    cc._cpu_temp = 42
    with ExitStack() as stack:
        stack.enter_context(patch.object(cc, "_cpu", side_effect=OSError('fixture')))
        for name, value in (("_mem", {}), ("_net", {}), ("_volumes", []), ("_raid", []),
                            ("_disk_io", []), ("_temps", []), ("_zfs", None)):
            stack.enter_context(patch.object(cc, name, return_value=value))
        stack.enter_context(patch.object(module, "read_text", return_value="1"))
        snap = cc.sample()
    assert old_cpu["temp_c"] == 31 and snap["cpu"]["temp_c"] == 42
    assert snap["cpu"] is not old_cpu and snap['modules']['cpu']['status'] == module.ST_STALE

    # H21：容器名是身份，不截断。共享 32 字符前缀的两个容器必须还是两个名字；
    # 57 字符的长名字原样留着（省略显示是渲染端的事，不是采集端把名字砍掉）。
    spec_d = importlib.util.spec_from_file_location("docker_api", COLLECTOR.parent / "docker_api.py")
    dapi = importlib.util.module_from_spec(spec_d)
    spec_d.loader.exec_module(dapi)
    long_name = "application-worker-007-with-a-long-unique-service-name"
    rows = dapi.rows_from([{"Names": ["/" + prefix + s], "Id": s[0] * 64, "State": "running",
                            "Status": "Up 3 days"} for s in ("alpha", "beta")] +
                          [{"Names": ["/" + long_name], "Id": "c" * 64, "State": "exited",
                            "Status": "Exited (137) 3 hours ago"}])
    assert [r["n"] for r in rows] == [prefix + "alpha", prefix + "beta", long_name], rows
    assert len(rows[-1]["n"]) > 23 and rows[-1]["n"] == long_name
    assert rows[0]["id"] == "a" * 12 and rows[-1]["id"] == "c" * 12
    try:
        dapi.rows_from({"not": "a list"})
        raise AssertionError("non-list Docker response accepted")
    except ValueError:
        pass

    print("PASS: full inventories, NIC isolation, hotplug, reset, type contracts and "
          "complete Docker identities")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
