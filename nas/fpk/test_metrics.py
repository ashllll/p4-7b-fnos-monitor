#!/usr/bin/env python3
"""Anonymous telemetry fixtures; no device, Docker socket or SMART execution."""
import importlib.util
import json
import sys
import tempfile
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
spec=importlib.util.spec_from_file_location('legacy_metrics',BASE.parent.parent/'fnos-agent.py')
legacy=importlib.util.module_from_spec(spec);spec.loader.exec_module(legacy)
spec=importlib.util.spec_from_file_location('sync_metrics',BASE.parent.parent/'sync_docker_metrics.py')
sync_metrics=importlib.util.module_from_spec(spec);spec.loader.exec_module(sync_metrics)

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
        for module in (docker_api,launch,legacy):
            raw=[{'Id':'a'*64,'State':'running'},{'Id':'b'*64,'State':'exited'}]
            cpu=[100,1000]
            failed=False
            def get(identifier,suffix):
                if failed:raise PermissionError()
                if suffix=='json':return {'RestartCount':2}
                return {'cpu_stats':{'cpu_usage':{'total_usage':cpu[0]},'system_cpu_usage':cpu[1],'online_cpus':4},'memory_stats':{'usage':256*1024**2,'stats':{'inactive_file':64*1024**2}}}
            def frame():
                rows=[{},{}]
                metrics.enrich(raw,rows)
                return rows
            metrics=module.DockerMetrics(get,clock=lambda:1700000000)
            first=frame();self.assertIsNone(first[0]['cpu_pct']);self.assertEqual(first[0]['mem_mb'],192)
            second=frame();self.assertEqual(second[1]['restarts'],2);self.assertIsNone(second[1]['mem_mb'])
            cpu[:]=[150,1200]
            third=frame();self.assertEqual(third[0]['cpu_pct'],100)
            self.assertEqual(third[0]['id'],'a'*64)
            self.assertEqual(third[0]['metrics_ts'],1700000000)
            # A new module instance cannot inherit another collector's CPU baseline.
            isolated=module.DockerMetrics(get)
            separate=[{},{}];isolated.enrich(raw,separate)
            self.assertIsNone(separate[0]['cpu_pct'])
            # Removal clears cache AND baseline, observed through the same interface.
            metrics.enrich([],[])
            readded=frame();self.assertIsNone(readded[0]['cpu_pct'])
            failed=True
            frame();bad=frame();self.assertIsNone(bad[0]['cpu_pct']);self.assertEqual(bad[0]['metrics_ts'],0)
            failed=False
            frame();recovered=frame();self.assertIsNone(recovered[0]['cpu_pct'])
            with self.assertRaises(ValueError):module._metrics_get('../unsafe','json')
            with self.assertRaises(ValueError):module._metrics_get('a'*64,'start')

    def test_zfs_distinguishes_absent_from_unreadable(self):
        c=collector.Collector()
        with patch.object(collector,'read_text_strict',side_effect=FileNotFoundError()):
            self.assertIsNone(c._zfs())
        with patch.object(collector,'read_text_strict',side_effect=PermissionError('denied')):
            with self.assertRaises(PermissionError):
                c._zfs()

    def test_volume_percent_counts_only_usable_capacity(self):
        st=SimpleNamespace(f_blocks=100,f_frsize=1024**3,f_bfree=30,f_bavail=20)
        # 伴侣采集器与 fnos-agent.py 各自维护一份 _volumes；口径必须一致，
        # 否则同一块盘在 8799 和伴侣端口上显示不同的 Use%。
        for name,module in (('companion',collector),('legacy',legacy)):
            with self.subTest(collector=name):
                c=module.Collector()
                with patch.object(module,'read_text',return_value='/dev/sda /data ext4 rw 0 0\n'), \
                     patch.object(module.os,'statvfs',return_value=st):
                    rows=c._volumes()
                self.assertEqual(len(rows),1)
                self.assertEqual((rows[0]['total_gb'],rows[0]['used_gb'],rows[0]['free_gb']),
                                 (100.0,70.0,20.0))
                self.assertEqual(rows[0]['pct'],77.8)

    def test_cpu_reads_loadavg_once(self):
        stat='cpu 1 2 3 4 5 6 7 8\n'
        load='0.10 0.20 0.30 1/100 12345\n'
        # 负载与 runq/procs 必须来自同一次读取：两次读之间可能变化，
        # 第二次失败还会把整段 CPU 采集打成异常。
        for name,module in (('companion',collector),('legacy',legacy)):
            with self.subTest(collector=name):
                c=module.Collector()
                with patch.object(module,'read_text',side_effect=[stat,load]) as read:
                    cpu=c._cpu()
                self.assertEqual(cpu['load1'],0.1)
                self.assertEqual((cpu['runq'],cpu['procs']),(1,100))
                self.assertEqual(read.call_count,2)

    def test_publication_rejects_non_finite_json_numbers(self):
        with self.assertRaises(ValueError):
            collector.PublishedSnapshot({'v':1,'host':'fixture','ts':0,'ready':True,
                                         'value':float('nan')})

    def test_generated_metric_copies_reject_drift_and_preserve_entry_code(self):
        self.assertEqual(sync_metrics.sync(),[])
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            source=(sync_metrics.BASE/'docker_metrics.py.inc').read_text()
            (base/'docker_metrics.py.inc').write_text(source)
            for name in sync_metrics.TARGETS:
                p=base/name;p.parent.mkdir(parents=True,exist_ok=True)
                p.write_text('# keep trusted entry\n'+sync_metrics.START+source+sync_metrics.END+'# keep privilege drop\n')
            target=base/sync_metrics.TARGETS[0]
            target.write_text(target.read_text().replace('self._cursor+=1','self._cursor+=2'))
            self.assertEqual(sync_metrics.sync(base),[sync_metrics.TARGETS[0]])
            sync_metrics.sync(base,write=True)
            self.assertEqual(sync_metrics.sync(base),[])
            self.assertTrue(target.read_text().endswith('# keep privilege drop\n'))
            target.write_text('no generated markers\n')
            with self.assertRaises(ValueError):sync_metrics.sync(base)

if __name__=='__main__':unittest.main()
