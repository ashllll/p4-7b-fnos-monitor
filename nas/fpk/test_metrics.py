#!/usr/bin/env python3
"""Anonymous telemetry fixtures; no device, Docker socket or SMART execution."""
import importlib.util
import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
BASE=Path(__file__).parent/'nasscreencompanion'
sys.path.insert(0,str(BASE/'app/server'))
import fnos_collector as collector
import docker_api
spec=importlib.util.spec_from_file_location('launch_metrics',BASE/'cmd/docker_launch.py')
launch=importlib.util.module_from_spec(spec);spec.loader.exec_module(launch)

class MetricsTests(unittest.TestCase):
    def test_disk_delta_idle_hotplug_reset(self):
        c=collector.Collector()
        def line(dev,read,write,r_ops,w_ops,r_ms,w_ms):
            return f'8 0 {dev} {r_ops} 0 {read} {r_ms} {w_ops} 0 {write} {w_ms} 0 0 0'
        def sample(at,text):
            with patch.object(collector,'read_text',return_value=text),patch.object(collector.time,'monotonic',return_value=at),patch.object(c,'_smart_enrich'):
                return c._disk_io()
        a=line('sda',100,200,10,20,100,200)
        first=sample(1,a);self.assertIsNone(first[0]['read_ms'])
        b=line('sda',120,240,12,24,106,220)
        second=sample(3,b)[0]
        self.assertEqual((second['rd_kbs'],second['wr_kbs']),(5,10))
        self.assertEqual((second['read_ms'],second['write_ms']),(3,5))
        self.assertIsNone(sample(4,b)[0]['read_ms'])
        reset=sample(5,line('sda',1,1,1,1,1,1))[0]
        self.assertIsNone(reset['read_ms']);self.assertEqual(reset['rd_kbs'],0)
        new=sample(6,line('nvme1n1',900,900,900,900,900,900))[0]
        self.assertIsNone(new['write_ms'])

    def test_smart_failed_standby_missing_rotation(self):
        import shutil,subprocess
        c=collector.Collector()
        rows=[{'dev':'sda'},{'dev':'sdb'},{'dev':'md0'}]
        result=SimpleNamespace(stdout=json.dumps({'smart_status':{'passed':False}}).encode(),returncode=8)
        with patch.object(shutil,'which',return_value='/fixture/smartctl'),patch.object(subprocess,'run',return_value=result) as run,patch.object(collector.time,'monotonic',return_value=100):
            c._smart_enrich(rows)
            self.assertEqual(rows[0]['smart'],'failed');self.assertEqual(rows[1]['smart'],'pending')
            self.assertEqual(rows[2]['smart'],'unsupported')
            self.assertEqual(run.call_args.args[0],['/fixture/smartctl','-j','-H','-n','standby,3','/dev/sda'])
            self.assertLess(run.call_args.kwargs['timeout'],1)
        with patch.object(shutil,'which',return_value='/fixture/smartctl'),patch.object(subprocess,'run',return_value=SimpleNamespace(stdout=b'{}',returncode=3)),patch.object(collector.time,'monotonic',return_value=101):
            c._smart_enrich(rows);self.assertEqual(rows[1]['smart'],'standby')
        with patch.object(shutil,'which',return_value=None),patch.object(collector.time,'monotonic',return_value=200):
            c._smart_enrich(rows);self.assertEqual(rows[0]['smart'],'missing')

    def test_docker_metrics_both_transports(self):
        for module in (docker_api,launch):
            module._metrics_cache={};module._metrics_previous={};module._metrics_cursor=0
            raw=[{'Id':'a'*64,'State':'running'},{'Id':'b'*64,'State':'exited'}]
            cpu=[100,1000]
            def get(identifier,suffix):
                if suffix=='json':return {'RestartCount':2}
                return {'cpu_stats':{'cpu_usage':{'total_usage':cpu[0]},'system_cpu_usage':cpu[1],'online_cpus':4},'memory_stats':{'usage':256*1024**2,'stats':{'inactive_file':64*1024**2}}}
            def frame():
                rows=[{},{}]
                module._enrich_metrics(raw,rows)
                return rows
            with patch.object(module,'_metrics_get',side_effect=get),patch.object(module.time,'time',return_value=1700000000):
                first=frame();self.assertIsNone(first[0]['cpu_pct']);self.assertEqual(first[0]['mem_mb'],192)
                second=frame();self.assertEqual(second[1]['restarts'],2);self.assertIsNone(second[1]['mem_mb'])
                cpu[:]=[150,1200]
                third=frame();self.assertEqual(third[0]['cpu_pct'],100)
                self.assertEqual(third[0]['id'],'a'*64)
            with patch.object(module,'_metrics_get',side_effect=PermissionError()):
                frame();bad=frame();self.assertIsNone(bad[0]['cpu_pct']);self.assertEqual(bad[0]['metrics_ts'],0)
            with self.assertRaises(ValueError):module._metrics_get('../unsafe','json')
            with self.assertRaises(ValueError):module._metrics_get('a'*64,'start')
            # Removal clears both cached resources and CPU baselines.
            module._enrich_metrics([],[]);self.assertFalse(module._metrics_cache)

if __name__=='__main__':unittest.main()
