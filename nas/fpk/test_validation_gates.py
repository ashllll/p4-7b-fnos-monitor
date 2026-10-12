#!/usr/bin/env python3
"""Anonymous API/wizard false-green regressions; no services or network."""
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('wizard_check',HERE/'wizard_check.py')
wizard=importlib.util.module_from_spec(spec); spec.loader.exec_module(wizard)
SERVER='''class Handler:
    def _route_get(self, path, q):
        if path == "/api/state":
            return 0
    def _route_post(self, path, q):
        if path == "/api/pairing/new":
            return body.get("ttl")
        if path == "/api/devices/revoke":
            return body.get("id")
        if path == "/api/pairing/cancel":
            return 0
'''
class GateTests(unittest.TestCase):
    def api(self,html,expected,script=None,server=SERVER):
        with tempfile.TemporaryDirectory() as tmp:
            pkg=Path(tmp); ui=pkg/'app/ui/www'; srv=pkg/'app/server'
            ui.mkdir(parents=True); srv.mkdir(parents=True)
            (ui/'index.html').write_text(html); (srv/'nas_companion_server.py').write_text(server)
            result=subprocess.run([sys.executable,str(script or HERE/'api_check.py'),str(pkg)],capture_output=True,text=True)
            self.assertEqual(result.returncode,expected,result.stdout+result.stderr)
    def test_locked_config_helper(self):
        source = SERVER.replace('    def _route_post(self, path, q):',
            '    def _route_post(self, path, q):\n        if path == "/api/config": return self._apply_config(body)')
        source += '\n    def _apply_config(self, body):\n        return self._apply_config_locked(body)\n    def _apply_config_locked(self, body):\n        if "tls_enabled" in body: return True\n'
        self.api("post('/api/config',{tls_enabled:true});",0,server=source)
        self.api("post('/api/config',{typo:true});",1,server=source)
        # 未调用的helper不能为入口背书。
        self.api("post('/api/config',{tls_enabled:true});",1,
                 server=source.replace('return self._apply_config_locked(body)', 'return 0'))

    def test_api_valid(self):
        self.api("get('/api/state'); post('/api/pairing/new',{ttl:300}); post('/api/pairing/cancel',{});",0)
    def test_duplicate_endpoint_typo(self):
        self.api("post('/api/pairing/new',{ttl:300}); post('/api/pairing/new',{typo:300});",1)
    def test_empty_page(self): self.api('<html></html>',2)
    def test_wrong_method(self): self.api("post('/api/state',{});",1)
    def test_neighbor_key_is_not_accepted(self): self.api("post('/api/pairing/new',{id:300});",1)
    def test_unresolved_body(self): self.api("post('/api/pairing/new',missing);",2)
    def test_nested_body(self): self.api("post('/api/pairing/new',{ttl:{value:300}});",2)
    def test_shorthand_not_silently_skipped(self): self.api("post('/api/pairing/new',{ttl:300,typo});",2)
    def test_unknown_expression(self): self.api("post('/api/pairing/new',makeBody());",2)
    def test_body_variable(self): self.api("const body = {ttl:300}; post('/api/pairing/new',body);",0)
    def test_duplicate_variables_unresolved(self): self.api("const body = {ttl:300}; const body = {id:300}; post('/api/pairing/new',body);",2)
    def test_nonfinite_value(self):
        for value in ('nan','NaN','inf','-Infinity',float('nan'),float('inf')):
            with self.subTest(value=value): self.assertFalse(wizard._rule_ok({'min':1,'max':65535},value))
    def test_numeric_rules(self):
        self.assertTrue(wizard._rule_ok({'min':1,'max':65535},'8798'))
        self.assertFalse(wizard._rule_ok({'min':1,'max':65535},'0'))
        self.assertTrue(wizard._rule_ok({'min':2,'max':5},'abcd'))
        self.assertTrue(wizard._rule_ok({'required':True,'len':3},'nan'))
        for rule in ({'min':'nan'},{'max':float('inf')},{'min':3,'max':2},{'len':1.5},{'len':-1},{'min':True},{'max':None}):
            self.assertFalse(wizard._rule_ok(rule,'3'),rule)
    def test_bad_bounds_without_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'install'
            path.write_text(json.dumps([{'stepTitle':'fixture','items':[{'type':'text','field':'wizard_port','label':'Port','rules':[{'min':'nan','message':'bad'}]}]}]))
            problems=[]; wizard.check_file(str(path),problems)
            self.assertTrue(any('数值规则' in p for p in problems),problems)
    def test_empty_wizard_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp)/'cmd').mkdir()
            result=subprocess.run([sys.executable,str(HERE/'wizard_check.py'),tmp],capture_output=True)
            self.assertNotEqual(result.returncode,0)
    def test_empty_steps(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'install'; path.write_text('[]'); problems=[]
            wizard.check_file(str(path),problems); self.assertTrue(problems)
    def test_unreadable_script_not_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            cmd=Path(tmp)/'cmd'; cmd.mkdir(); (cmd/'main').write_text('fixture')
            with mock.patch('builtins.open',side_effect=OSError('fixture denied')):
                with self.assertRaises(OSError): wizard.script_vars(tmp)
    def test_mutated_body(self):
        self.api("const body = {ttl:300}; body.typo=1; post('/api/pairing/new',body);",2)
    def test_negative_control_duplicate_skip(self):
        source=(HERE/'api_check.py').read_text()
        marker='    for method, path, keys in calls:\n'
        self.assertIn(marker,source)
        mutant=source.replace(marker,marker+'        if (method, path) in seen:\n            continue\n',1)
        with tempfile.TemporaryDirectory() as tmp:
            script=Path(tmp)/'mutant.py'; script.write_text(mutant)
            # Restoring the old defect produces a false success for the exact failing fixture.
            self.api("post('/api/pairing/new',{ttl:300}); post('/api/pairing/new',{typo:300});",0,script)

# These fixtures import production gates, with all transport operations mocked.
import contextlib
import io
import ssl

def load_gate(name):
    spec=importlib.util.spec_from_file_location(name,HERE/(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
contract=load_gate('contract_check')
board=load_gate('board_sim')
class ContractBoardTests(unittest.TestCase):
    def test_typed_root_fields(self):
        reads=contract.firmware_reads(contract.DEFAULT_FIRMWARE)
        for key in ('ts','uptime_s','proto','ready'): self.assertIn(('root',key),reads)
    def test_negative_control_root_type_omission(self):
        source=(HERE/'contract_check.py').read_text()
        mutant=source.replace("r'(?:number|inum|unum|lnum|fnum)\\(root,", "r'number\\(root,")
        self.assertNotEqual(source,mutant)
        ns={'__file__':str(HERE/'contract_check.py'),'__name__':'negative_fixture'}
        exec(compile(mutant,'negative_fixture','exec'),ns)
        self.assertNotIn(('root','ts'),ns['firmware_reads'](contract.DEFAULT_FIRMWARE))
    def test_negative_control_body_echo(self):
        source=(HERE/'board_sim.py').read_text()
        old=source.replace('bad(f"{what} 的响应不是有效 JSON 对象")','bad(f"{what}: {data!r}")')
        self.assertNotEqual(source,old)
        ns={'__name__':'negative_fixture'};exec(compile(old,'negative_fixture','exec'),ns)
        output=io.StringIO()
        with contextlib.redirect_stdout(output):ns['jload'](b'broken-anonymous-token-marker','fixture')
        self.assertIn('anonymous-token-marker',output.getvalue())
    def test_empty_fields_fail_before_sampling(self):
        with mock.patch.object(contract,'load_reads',return_value=([],'fixture')),mock.patch.object(contract,'live_status') as sample,contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(contract.main(),1);sample.assert_not_called()
    def contract_rows(self,st):
        with mock.patch.object(contract,'load_reads',return_value=([('vols','mnt')],'fixture')),mock.patch.object(contract,'live_status',return_value=st),mock.patch.object(contract,'all_source_keys',return_value={'mnt'}),mock.patch.object(contract,'limits_problems',return_value=[]),mock.patch.object(contract,'preview_size_problems',return_value=[]),mock.patch.object(sys,'argv',['check']),contextlib.redirect_stdout(io.StringIO()):
            return contract.main()
    def test_all_rows_checked(self):
        self.assertEqual(self.contract_rows({'vols':[{'mnt':'fixture'},{'oops':1}]}),1)
        self.assertEqual(self.contract_rows({'vols':[{'mnt':'fixture'},{'mnt':'other'}]}),0)
    def test_nonobject_status(self):self.assertEqual(self.contract_rows([]),1)
    def test_method_does_not_include_neighbor(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'source.py';p.write_text('def one():\n return {"ok":1}\ndef two():\n return {"typo":2}\n')
            self.assertNotIn('typo',contract.method_body(p,'one'))
    def test_collector_exit_not_ignored(self):
        with mock.patch.object(contract.subprocess,'run',side_effect=subprocess.CalledProcessError(1,'fixture')):
            with self.assertRaises(subprocess.CalledProcessError):contract.live_status()
    def test_json_errors_do_not_echo_body(self):
        secret='anonymous-sensitive-marker'
        for data in (b'',b'[]',b'null',('broken'+secret).encode()):
            out=io.StringIO()
            with contextlib.redirect_stdout(out):self.assertIsNone(board.jload(data,'fixture'))
            self.assertNotIn(secret,out.getvalue())
        self.assertEqual(board.jload(b'{"ok":true}','fixture'),{'ok':True})
    def test_request_failure_closes_connection(self):
        conn=mock.Mock();conn.request.side_effect=OSError('fixture')
        with mock.patch.object(board.http.client,'HTTPConnection',return_value=conn):
            with self.assertRaises(OSError):board.req('fixture',1,'/')
        conn.close.assert_called_once()
    def test_oversize_response_closes(self):
        conn=mock.Mock();conn.getresponse.return_value.read.return_value=b'x'*(16*1024*1024+1)
        with mock.patch.object(board.http.client,'HTTPConnection',return_value=conn):
            with self.assertRaises(ValueError):board.req('fixture',1,'/')
        conn.close.assert_called_once()
    def simulator(self,pair_status=200,contract_status=200,missing_fp=False,checker_failure=False):
        board.PASS=board.FAIL=0;calls=[];paths=[];token='anonymous-token-marker';ctx=object();wrong=object()
        def request(host,port,path,ctx=None,token=None,method='GET',body=None,**kw):
            calls.append((path,method,body))
            if ctx is wrong:raise ssl.SSLError('wrong fixture certificate')
            if path=='/api/v1/identity':return 200,json.dumps({'tls':True,'cert_pem':'fixture','tls_fingerprint':'' if missing_fp else 'AA'}).encode()
            if path=='/api/v1/pair':
                value=json.loads(body)['code']
                if value=='invalid':return 400,b'{}'
                return pair_status,json.dumps({'token':'anonymous-token-marker'}).encode()
            if path=='/api/v1/history':return 200,b'{}'
            if ctx is None:return 403,b'{}'
            if token is None:return 401,b'{}'
            count=sum(p=='/api/v1/status' and m=='GET' for p,m,_ in calls)
            if count>=4:return contract_status,b'{"ready":true}'
            return 200,b'{"ready":true}'
        def checker(args,**kw):
            paths.append(Path(args[-1]))
            if checker_failure:raise OSError('fixture')
            return subprocess.CompletedProcess(args,0,stdout='判定：字段通过',stderr='')
        output=io.StringIO()
        with mock.patch.object(sys,'argv',['board','--host','fixture','--port','1','--pair-code','123456']),mock.patch.object(board,'req',side_effect=request),mock.patch.object(board,'pem_fingerprint',return_value='AA'),mock.patch.object(board,'pinned_ctx',side_effect=lambda pem:wrong if pem=='wrong' else ctx),mock.patch.object(board,'make_impostor_cert',return_value='wrong'),mock.patch.object(board.subprocess,'run',side_effect=checker),contextlib.redirect_stdout(output):
            result=board.main()
        self.assertNotIn(token,output.getvalue());self.assertNotIn('123456',output.getvalue())
        for p in paths:self.assertFalse(p.exists());self.assertFalse(p.parent.exists())
        return result,calls
    def test_pair_error_does_not_echo_token(self):self.assertEqual(self.simulator(pair_status=400)[0],1)
    def test_missing_fingerprint_stops_before_pair(self):
        result,calls=self.simulator(missing_fp=True);self.assertEqual(result,1);self.assertFalse(any(p=='/api/v1/pair' for p,_,_ in calls))
    def test_contract_transport_failure_not_skipped(self):self.assertEqual(self.simulator(contract_status=500)[0],1)
    def test_contract_temp_cleanup_on_exception(self):self.assertEqual(self.simulator(checker_failure=True)[0],1)
    def test_simulator_success_and_invalid_code(self):
        result,calls=self.simulator();self.assertEqual(result,0);self.assertTrue(any(body and 'invalid' in body for _,_,body in calls))

class BuildArchiveTests(unittest.TestCase):
    def build_fixture(self,mode,grep_failure=False,find_failure=False):
        import os
        with tempfile.TemporaryDirectory(prefix='fnos-package-check-') as tmp:
            root=Path(tmp);(root/'nasscreencompanion').mkdir();binpath=root/'bin';binpath.mkdir()
            fixture=root/'pack.py'
            fixture.write_text("import io,tarfile\nfrom pathlib import Path\nmode="+repr(mode)+"\nwith tarfile.open('nasscreencompanion.fpk','w:gz') as outer:\n if mode!='missing':\n  data=b'broken'\n  if mode!='corrupt':\n   inner=io.BytesIO()\n   with tarfile.open(fileobj=inner,mode='w:gz') as app:\n    info=tarfile.TarInfo('nested/._resource' if mode=='junk' else 'fixture.txt');info.size=1;app.addfile(info,io.BytesIO(b'x'))\n   data=inner.getvalue()\n  info=tarfile.TarInfo('app.tgz');info.size=len(data);outer.addfile(info,io.BytesIO(data))\n")
            script=(HERE/'build.sh').read_text().replace('fnpack build --directory "$PKG"',sys.executable+' '+str(fixture))
            # Keep the actual shell checks; tool faults are isolated to this fixture.
            if grep_failure:script=script.replace('grep -E',str(binpath/'grep')+' -E');(binpath/'grep').write_text('#!/bin/sh\nexit 2\n');(binpath/'grep').chmod(0o700)
            if find_failure:script=script.replace('find "$PKG"',str(binpath/'find')+' "$PKG"');(binpath/'find').write_text('#!/bin/sh\nexit 1\n');(binpath/'find').chmod(0o700)
            (root/'build.sh').write_text(script)
            return subprocess.run(['bash',str(root/'build.sh')],capture_output=True,text=True,timeout=15)
    def test_valid(self):self.assertEqual(self.build_fixture('valid').returncode,0)
    def test_missing_corrupt_inner_archive(self):
        for mode in ('missing','corrupt'):self.assertNotEqual(self.build_fixture(mode).returncode,0)
    def test_nested_resource_fork(self):self.assertNotEqual(self.build_fixture('junk').returncode,0)
    def test_grep_tool_error(self):self.assertNotEqual(self.build_fixture('valid',grep_failure=True).returncode,0)
    def test_cleanup_tool_error(self):self.assertNotEqual(self.build_fixture('valid',find_failure=True).returncode,0)

if __name__=='__main__': unittest.main()
