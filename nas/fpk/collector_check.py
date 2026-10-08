#!/usr/bin/env python3
"""Complete inventories, explicit policy and independent interface rates."""
import importlib.util
import json
from pathlib import Path
from unittest.mock import patch
COLLECTOR = Path(__file__).parent / "nasscreencompanion/app/server/fnos_collector.py"

def main():
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
    assert len(third["interfaces"]) == 1
    assert third["interfaces"][0]["rx_kbs"] == 0
    obj.limits = {"interfaces": 0}
    assert collect(16)["interfaces"] == [] and obj._totals["interfaces"] == 1
    del obj.limits
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
    with patch.object(legacy, "_http_unix", return_value=(200, body)), patch.object(legacy.time, "monotonic", return_value=10):
        containers = agent._docker()
    assert {x["n"] for x in containers} == {prefix + "alpha", prefix + "beta", "abcdef0123456789" * 4}
    assert all(x["s"] == status for x in containers)
    assert agent._docker_seen_up == {prefix + "alpha", prefix + "beta"}
    print("PASS: full inventories, NIC isolation, hotplug, reset and complete Docker identities")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
