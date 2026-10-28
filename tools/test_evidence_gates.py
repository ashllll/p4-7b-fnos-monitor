#!/usr/bin/env python3
"""Missing freshness evidence must fail; temporary files only, no real artifacts."""
import contextlib
import importlib.util
import io
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
def _enter(case, cm):
 # TestCase.enterContext 是 3.11+；本套门禁还要能在 3.9 的 CI 上跑。
 value=cm.__enter__();case.addCleanup(cm.__exit__,None,None,None);return value
spec=importlib.util.spec_from_file_location('freshness',ROOT/'tools/freshness_check.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Freshness(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
  (self.root/'source').mkdir();(self.root/'source/main.c').write_text('fixture')
  (self.root/'artifact').write_text('fixture')
  os.utime(self.root/'source/main.c',(100,100));os.utime(self.root/'artifact',(200,200))
  _enter(self, patch.object(m,'REPO',str(self.root)))
  self.spec={'name':'fixture','artifact':'artifact','roots':['source'],'extra':[],'how':'fixture-only'}
  _enter(self, patch.object(m,'ARTIFACTS',[self.spec]))
 def run_gate(self):
  with contextlib.redirect_stdout(io.StringIO()):return m.main()
 def test_normal_and_stale(self):
  self.assertEqual(self.run_gate(),0);os.utime(self.root/'source/main.c',(300,300));self.assertEqual(self.run_gate(),1)
 def test_missing_root(self):
  self.spec['roots']=['missing'];self.assertEqual(self.run_gate(),1)
 def test_missing_extra(self):
  self.spec['extra']=['missing.h'];self.assertEqual(self.run_gate(),1)
 def test_empty_inputs(self):
  (self.root/'source/main.c').unlink();self.assertEqual(self.run_gate(),1)
 def test_stat_failure(self):
  real=m.os.path.getmtime
  def fail(path):
   if str(path).endswith('main.c'):raise PermissionError('anonymous fixture')
   return real(path)
  with patch.object(m.os.path,'getmtime',side_effect=fail):self.assertEqual(self.run_gate(),1)
 def test_walk_failure(self):
  with patch.object(m.os,'walk',side_effect=PermissionError('anonymous fixture')):self.assertEqual(self.run_gate(),1)
 def test_artifact_directory(self):
  (self.root/'artifact').unlink();(self.root/'artifact').mkdir();self.assertEqual(self.run_gate(),1)
 def test_package_extensionless_and_data(self):
  self.spec['all_files']=True
  for name in ('main','install_callback','resource.css','icon.png'):
   p=self.root/'source'/name;p.write_bytes(b'anonymous');os.utime(p,(300,300));self.assertEqual(self.run_gate(),1);p.unlink()
 def test_kconfig_and_generated_preview(self):
  p=self.root/'source/Kconfig.projbuild';p.write_text('fixture');os.utime(p,(300,300));self.assertEqual(self.run_gate(),1);p.unlink()
  d=self.root/'source/out-fixture';d.mkdir();(d/'metadata.json').write_text('fixture');self.assertEqual(self.run_gate(),0)
 def test_negative_control(self):
  self.spec['roots']=['missing']
  with patch.object(m,'newest_sources',return_value=(0,[])):
   self.assertEqual(self.run_gate(),0,'old fail-open behavior must be distinguished')
spec=importlib.util.spec_from_file_location('soak',ROOT/'tools/soak_check.py')
soak=importlib.util.module_from_spec(spec);spec.loader.exec_module(soak)
class Soak(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
  self.stage=self.root/'stage';self.current=self.root/soak.SERVER_SRC;self.staged=self.stage/'target/server'
  self.current.mkdir(parents=True);self.staged.mkdir(parents=True)
  for name in ('nas_companion_server.py','fnos_collector.py','docker_api.py'):
   (self.current/name).write_text('anonymous fixture');(self.staged/name).write_text('anonymous fixture')
  self.log=self.root/'soak.log';self.write_rows(range(1,11))
  _enter(self, patch.object(soak,'REPO',str(self.root)))
  _enter(self, patch.object(soak.sys,'argv',['soak','--log',str(self.log),'--stage',str(self.stage)]))
 def write_rows(self,ids):
  self.log.write_text(''.join(f'#{n} rss=1000KB fd=5 管理面=200 TLS=200 明文引导=200\n' for n in ids))
 def run_gate(self):
  with contextlib.redirect_stdout(io.StringIO()):return soak.main()
 def test_normal(self):self.assertEqual(self.run_gate(),0)
 def test_changed_import(self):
  (self.staged/'docker_api.py').write_text('old fixture');self.assertEqual(self.run_gate(),1)
 def test_missing_import(self):
  (self.staged/'docker_api.py').unlink();self.assertEqual(self.run_gate(),1)
 def test_extra_stale_module(self):
  (self.staged/'old.py').write_text('old fixture');self.assertEqual(self.run_gate(),1)
 def test_duplicate_or_reversed_ids(self):
  for ids in ([1]*10,list(range(10,0,-1))):self.write_rows(ids);self.assertEqual(self.run_gate(),1)
 def test_future_log(self):
  import time
  future=time.time()+100;os.utime(self.log,(future,future));self.assertEqual(self.run_gate(),2)
 def test_missing_or_stale_log(self):
  os.utime(self.log,(100,100));self.assertEqual(self.run_gate(),2);self.log.unlink();self.assertEqual(self.run_gate(),2)
 def test_insufficient_rows(self):
  self.write_rows(range(1,4));self.assertEqual(self.run_gate(),1)
 def test_missing_or_empty_source(self):
  for p in self.current.iterdir():p.unlink()
  for p in self.staged.iterdir():p.unlink()
  self.assertEqual(self.run_gate(),1);self.current.rmdir();self.assertEqual(self.run_gate(),1)
 def test_negative_control(self):
  (self.staged/'docker_api.py').write_text('old fixture')
  old=[('target/server/'+name,soak.SERVER_SRC+'/'+name) for name in ('nas_companion_server.py','fnos_collector.py')]
  with patch.object(soak,'tracked_sources',return_value=old):self.assertEqual(self.run_gate(),0)
spec=importlib.util.spec_from_file_location('plan',ROOT/'tools/plan_check.py')
plan=importlib.util.module_from_spec(spec);spec.loader.exec_module(plan)
class Plan(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
  (self.root/'docs').mkdir();(self.root/'source').mkdir()
  (self.root/'source/input.h').write_text('fixture')
  self.doc=self.root/'docs/plan.md';self.doc.write_text('`source/input.h` 1 个字段 本地端到端测试 1 项')
  self.fields=self.root/'fields.json';self.fields.write_text('[["root","host"]]')
  self.package=self.root/'package.fpk';self.package.write_text('fixture')
  self.report=self.root/'docs/fnos-companion-test-report.md';self.report.write_text('1 项通过，0 项失败')
  for name,value in [('REPO',str(self.root)),('DOCS',[str(self.doc)]),('FIELD_MANIFEST',str(self.fields)),('FPK',str(self.package))]:
   _enter(self, patch.object(plan,name,value))
 def run_gate(self):
  with contextlib.redirect_stdout(io.StringIO()):return plan.main()
 def test_cwd_independent(self):
  old=os.getcwd()
  try:
   os.chdir('/');self.assertEqual(self.run_gate(),0)
  finally:os.chdir(old)
 def test_missing_evidence(self):
  for path in (self.doc,self.fields,self.package,self.report):
   original=path.read_bytes();path.unlink();self.assertEqual(self.run_gate(),1);path.write_bytes(original)
 def test_malformed_manifest(self):
  for text in ('{}','[]','["host"]','[["root", "host"],["root", "host"]]','{"fields":null}','bad json'):
   self.fields.write_text(text);self.assertEqual(self.run_gate(),1)
 def test_summary_zero_missing_conflicting(self):
  for text in ('0 项通过，0 项失败','no receipt','1 项通过，0 项失败 1 项通过，1 项失败'):
   self.report.write_text(text);self.assertEqual(self.run_gate(),1)
  self.report.write_text('1 项通过，0 项失败 1 项通过，0 项失败');self.assertEqual(self.run_gate(),0)
 def test_unreadable_input(self):
  real=plan.io.open
  def fail(path,*a,**kw):
   if str(path)==str(self.fields):raise PermissionError('fixture')
   return real(path,*a,**kw)
  with patch.object(plan.io,'open',side_effect=fail):self.assertEqual(self.run_gate(),1)
 def test_old_first_summary_negative_control(self):
  source=(ROOT/'tools/plan_check.py').read_text()
  original=source
  source=source.replace('r"(\\d+)\\s*项通过，\\s*(\\d+)\\s*项失败", rtext)}','r"(\\d+)\\s*项通过，\\s*(\\d+)\\s*项失败", rtext)[:1]}')
  self.assertNotEqual(source,original)
  namespace={'__file__':str(ROOT/'tools/plan_check.py'),'__name__':'fixture'}
  exec(compile(source,'anonymous-plan-negative','exec'),namespace)
  for name in ('REPO','DOCS','FIELD_MANIFEST','FPK'):namespace[name]=getattr(plan,name)
  self.report.write_text('1 项通过，0 项失败 1 项通过，1 项失败')
  with contextlib.redirect_stdout(io.StringIO()):self.assertEqual(namespace['main'](),0)
class PaletteSafety(unittest.TestCase):
 def test_invalid_names_and_linked_output(self):
  import subprocess
  import shutil
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);script=root/'palettes.sh'
   script.write_bytes((ROOT/'tools/preview/palettes.sh').read_bytes())
   target=root/'target';(target/'graphite').mkdir(parents=True)
   sentinel=target/'graphite/sentinel';sentinel.write_text('preserve')
   (root/'palette-out').symlink_to(target,target_is_directory=True)
   for name in ('graphite','*','../target','  '):
    result=subprocess.run([shutil.which('bash'),str(script)],env=dict(os.environ,KK_PALETTES=name),capture_output=True,timeout=10)
    self.assertEqual(result.returncode,2);self.assertEqual(sentinel.read_text(),'preserve')
   source=script.read_text().replace('if [ -L "$OUT" ]; then','if false; then')
   script.write_text(source)
   result=subprocess.run([shutil.which('bash'),str(script)],env=dict(os.environ,KK_PALETTES='graphite'),capture_output=True,timeout=10)
   self.assertNotEqual(result.returncode,0);self.assertFalse(sentinel.exists(),'old output symlink behavior deletes sentinel before missing binary failure')
if __name__=='__main__':unittest.main()
