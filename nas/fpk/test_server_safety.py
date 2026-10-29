#!/usr/bin/env python3
"""Host-only regressions for TLS fail-closed, token serialization and shutdown."""
import copy
import importlib.util
import inspect
import textwrap
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

path = Path(__file__).parent / 'nasscreencompanion/app/server/nas_companion_server.py'
spec = importlib.util.spec_from_file_location('safety_server', path)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Safety(unittest.TestCase):
    def setUp(self):
        self.cfg = copy.deepcopy(m.DEFAULT_CONFIG)
        self.cfg['devices'] = [{'name': 'test', 'last4': 'last',
                                'sha256': m._token_hash('test-only-token')}]
        self.state = m.State(self.cfg)
        self.saver = patch.object(m, 'save_config')
        self.saver.start()
        self.logger = patch.object(m, 'log')
        self.logger.start()
        self.addCleanup(self.saver.stop)
        self.addCleanup(self.logger.stop)

    def handler(self):
        h = object.__new__(m.Handler)
        h.server = type('Server', (), {'state': self.state})()
        return h

    def test_tls_identity_snapshot_matches_active_handshake_until_restart(self):
        import tempfile, ssl, http.client, hashlib, json
        with tempfile.TemporaryDirectory() as tmp,patch.object(m,'tls_dir',return_value=tmp),patch.object(m,'_local_ips',return_value=[]):
            cert=Path(tmp)/'server.crt';key=Path(tmp)/'server.key'
            with patch.object(m,'tls_cert_path',return_value=str(cert)),patch.object(m,'tls_key_path',return_value=str(key)):
                cfg=dict(self.cfg,bind='127.0.0.1',port=0)
                def start():
                    state=m.State(cfg);servers,problems=m.make_servers(state,cfg,bind_socket=False)
                    self.assertFalse(problems);srv=servers[0]
                    worker=threading.Thread(target=srv.serve_forever,kwargs={'poll_interval':.01});worker.start()
                    return srv,worker
                def identity(srv,tls=False):
                    if tls:
                        ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);ctx.check_hostname=False;ctx.verify_mode=ssl.CERT_NONE
                        client=http.client.HTTPSConnection(*srv.server_address,timeout=2,context=ctx)
                    else:client=http.client.HTTPConnection(*srv.server_address,timeout=2)
                    try:
                        client.connect();der=client.sock.getpeercert(binary_form=True) if tls else None
                        client.request('GET','/api/v1/identity');response=client.getresponse()
                        self.assertEqual(response.status,200)
                        return json.loads(response.read()),der
                    finally:client.close()
                def fingerprint(pem):return ':'.join('%02X'%v for v in hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).digest())
                srv,worker=start()
                try:
                    old,peer=identity(srv,True)
                    self.assertEqual(ssl.PEM_cert_to_DER_cert(old['cert_pem']),peer)
                    self.assertEqual(fingerprint(old['cert_pem']),old['tls_fingerprint'])
                    self.assertTrue(m.generate_self_signed()[0]);new_pem=cert.read_text()
                    self.assertNotEqual(new_pem,old['cert_pem'])
                    for tls in (False,True):
                        current,peer=identity(srv,tls)
                        self.assertEqual(current['cert_pem'],old['cert_pem'])
                        self.assertEqual(current['tls_fingerprint'],old['tls_fingerprint'])
                        if tls:self.assertEqual(ssl.PEM_cert_to_DER_cert(current['cert_pem']),peer)
                    source=textwrap.dedent(inspect.getsource(m.Handler._route_get))
                    needle='            cert_pem = tls.get("cert_pem") if tls.get("enabled") else None'
                    # inspect.dedent removes one class indentation level.
                    needle=needle[4:]
                    self.assertIn(needle,source)
                    source=source.replace(needle,needle+'\n        if path == "/api/v1/identity" and tls.get("enabled"):\n            with open(tls["cert"]) as fp: cert_pem = fp.read()',1)
                    namespace=dict(m.__dict__);exec(source,namespace)
                    with patch.object(m.Handler,'_route_get',namespace['_route_get']):
                        current,_=identity(srv)
                        with self.assertRaises(AssertionError):self.assertEqual(fingerprint(current['cert_pem']),current['tls_fingerprint'])
                finally:srv.shutdown();srv.server_close();worker.join(2)
                srv,worker=start()
                try:
                    current,peer=identity(srv,True)
                    self.assertEqual(current['cert_pem'],new_pem)
                    self.assertEqual(ssl.PEM_cert_to_DER_cert(new_pem),peer)
                    self.assertEqual(fingerprint(new_pem),current['tls_fingerprint'])
                finally:srv.shutdown();srv.server_close();worker.join(2)

    def test_active_tls_capture_excludes_cooperating_regeneration(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp,patch.object(m,'tls_dir',return_value=tmp),patch.object(m,'_local_ips',return_value=[]):
            cert=Path(tmp)/'server.crt';key=Path(tmp)/'server.key'
            with patch.object(m,'tls_cert_path',return_value=str(cert)),patch.object(m,'tls_key_path',return_value=str(key)):
                info=m.prepare_tls();self.assertTrue(info['enabled']);old=cert.read_text()
                attempted=threading.Event();done=threading.Event();result=[]
                flock=m.fcntl.flock;loader=m.load_ssl_context
                def observe_lock(fd,mode):
                    if mode==m.fcntl.LOCK_EX:attempted.set()
                    return flock(fd,mode)
                def regenerate():
                    try:result.append(m.generate_self_signed())
                    finally:done.set()
                writer=threading.Thread(target=regenerate)
                def load(value):
                    if value is info:
                        writer.start();self.assertTrue(attempted.wait(2));self.assertFalse(done.is_set())
                    return loader(value)
                with patch.object(m.fcntl,'flock',side_effect=observe_lock),patch.object(m,'load_ssl_context',side_effect=load):
                    context,error=m.load_active_tls(info)
                    self.assertIsNotNone(context,error)
                    writer.join(10);self.assertFalse(writer.is_alive());self.assertTrue(result[0][0],result)
                self.assertEqual(info['cert_pem'],old)
                self.assertNotEqual(cert.read_text(),old)
                # Read failures fail closed and don't publish partial identity.
                import builtins
                real_open=builtins.open
                def fail_cert(path,*args,**kwargs):
                    if str(path)==str(cert) and args and args[0]=='r':raise OSError('fixture')
                    return real_open(path,*args,**kwargs)
                bad=dict(info);bad.pop('cert_pem')
                with patch('builtins.open',side_effect=fail_cert):
                    context,error=m.load_active_tls(bad);self.assertIsNone(context);self.assertTrue(error)
                self.assertNotIn('cert_pem',bad)

    def test_tls_regeneration_preserves_old_pair_and_validates_candidate(self):
        import os, tempfile, subprocess, ssl
        with tempfile.TemporaryDirectory() as tmp, patch.object(m, 'tls_dir', return_value=tmp), patch.object(m, '_local_ips', return_value=[]):
            cert=Path(tmp)/'server.crt';key=Path(tmp)/'server.key'
            with patch.object(m,'tls_cert_path',return_value=str(cert)),patch.object(m,'tls_key_path',return_value=str(key)):
                ok,message=m.generate_self_signed();self.assertTrue(ok,message)
                before=(cert.read_bytes(),key.read_bytes())
                self.assertEqual(key.stat().st_mode & 0o777,0o600)
                self.assertTrue(m.prepare_tls()['enabled'])
                # A generator failure must never remove or modify old material.
                with patch.object(m,'generate_self_signed',return_value=(False,'fixture')):
                    self.assertFalse(m.prepare_tls(regen=True)['enabled'])
                self.assertEqual((cert.read_bytes(),key.read_bytes()),before)
                with patch.object(m.subprocess,'run',return_value=subprocess.CompletedProcess([],1,'','')):
                    self.assertFalse(m.generate_self_signed()[0])
                self.assertEqual((cert.read_bytes(),key.read_bytes()),before)
                # A claimed openssl success with invalid output is also rejected.
                def invalid(cmd,**kwargs):
                    Path(cmd[cmd.index('-keyout')+1]).write_text('anonymous invalid key')
                    Path(cmd[cmd.index('-out')+1]).write_text('anonymous invalid cert')
                    return subprocess.CompletedProcess(cmd,0,'','')
                with patch.object(m.subprocess,'run',side_effect=invalid):
                    self.assertFalse(m.generate_self_signed()[0])
                self.assertEqual((cert.read_bytes(),key.read_bytes()),before)
                replace=os.replace
                def fail_second(source,target):
                    if target==str(key) and Path(source).name=='server.key':raise OSError('fixture')
                    return replace(source,target)
                with patch.object(m.os,'replace',side_effect=fail_second):
                    self.assertFalse(m.generate_self_signed()[0])
                self.assertEqual((cert.read_bytes(),key.read_bytes()),before)
                chmod=os.chmod
                def fail_permission(target,mode):
                    if Path(target).name=='server.key' and '.candidate-' in str(target):raise OSError('fixture')
                    return chmod(target,mode)
                with patch.object(m.os,'chmod',side_effect=fail_permission):
                    self.assertFalse(m.generate_self_signed()[0])
                self.assertEqual((cert.read_bytes(),key.read_bytes()),before)
                self.assertFalse(list(Path(tmp).glob('.candidate-*')))
                # Valid but unrelated key is not accepted by prepare_tls/status.
                with tempfile.TemporaryDirectory() as other:
                    other_key=Path(other)/'key';other_cert=Path(other)/'cert'
                    subprocess.run([m._openssl_bin(),'req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(other_key),'-out',str(other_cert),'-days','1','-subj','/CN=anonymous-fixture'],check=True,capture_output=True)
                    key.write_bytes(other_key.read_bytes())
                    self.assertFalse(m.prepare_tls()['enabled'])
                key.write_bytes(before[1])
                self.assertTrue(m.prepare_tls(regen=True)['enabled'])
                self.assertNotEqual(cert.read_bytes(),before[0])
                # Restore the old pre-delete order in the production function.
                source=textwrap.dedent(inspect.getsource(m.prepare_tls))
                source=source.replace('    missing = not ', '    if regen:\n        os.unlink(tls_cert_path())\n        os.unlink(tls_key_path())\n    missing = not ',1)
                namespace=dict(m.__dict__);exec(source,namespace)
                old_before=(cert.read_bytes(),key.read_bytes())
                namespace['generate_self_signed']=lambda:(False,'fixture')
                self.assertFalse(namespace['prepare_tls'](regen=True)['enabled'])
                with self.assertRaises(FileNotFoundError):
                    self.assertEqual((cert.read_bytes(),key.read_bytes()),old_before)

    def test_tls_regeneration_failed_rollback_retains_private_backup(self):
        import tempfile, os
        with tempfile.TemporaryDirectory() as tmp,patch.object(m,'tls_dir',return_value=tmp),patch.object(m,'_local_ips',return_value=[]):
            cert=Path(tmp)/'server.crt';key=Path(tmp)/'server.key'
            with patch.object(m,'tls_cert_path',return_value=str(cert)),patch.object(m,'tls_key_path',return_value=str(key)):
                self.assertTrue(m.generate_self_signed()[0]);before=cert.read_bytes()
                replace=os.replace
                def fail(source,target):
                    if Path(source).name in ('server.key','backup-0'):raise OSError('fixture')
                    return replace(source,target)
                with patch.object(m.os,'replace',side_effect=fail):
                    ok,message=m.generate_self_signed();self.assertFalse(ok);self.assertIn('恢复失败',message)
                retained=list(Path(tmp).glob('.candidate-*'))
                self.assertEqual(len(retained),1)
                self.assertEqual((retained[0]/'backup-0').read_bytes(),before)
                self.assertEqual(retained[0].stat().st_mode & 0o777,0o700)

    def test_tls_generation_propagates_exception_when_cleanup_also_fails(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp, patch.object(m, 'tls_dir', return_value=tmp), \
                patch.object(m, '_local_ips', return_value=[]):
            cert = Path(tmp) / 'server.crt'
            key = Path(tmp) / 'server.key'
            with patch.object(m, 'tls_cert_path', return_value=str(cert)), \
                    patch.object(m, 'tls_key_path', return_value=str(key)):
                with patch.object(m.subprocess, 'run', side_effect=KeyboardInterrupt):
                    with patch.object(m.shutil, 'rmtree', side_effect=OSError('fixture')):
                        with self.assertRaises(KeyboardInterrupt):
                            m.generate_self_signed()

    def test_startup_config_rejects_invalid_auth_tls_and_numbers(self):
        import os,json,tempfile
        envs=('TRIM_SERVICE_PORT','NSC_BIND','NSC_INTERVAL','NSC_HIST','NSC_AUTH','NSC_DOCKER','NSC_TLS')
        clean={k:v for k,v in os.environ.items() if k not in envs}
        with tempfile.TemporaryDirectory() as tmp,patch.dict(os.environ,clean,clear=True),patch.object(m,'cfg_path',return_value=str(Path(tmp)/'config.json')):
            config_path=Path(tmp)/'config.json'
            for bad in ('invalid-fixture','','nan','2'):
                with patch.dict(os.environ,{'NSC_TLS':bad}),self.assertRaises(m.ConfigError):m.load_config()
            for yes in ('1','true','yes','on',' TRUE '):
                with patch.dict(os.environ,{'NSC_TLS':yes,'NSC_DOCKER':yes}):
                    cfg=m.load_config();self.assertTrue(cfg['tls_enabled']);self.assertTrue(cfg['docker_enabled'])
            for no in ('0','false','no','off',' FALSE '):
                with patch.dict(os.environ,{'NSC_TLS':no,'NSC_DOCKER':no}):
                    cfg=m.load_config();self.assertFalse(cfg['tls_enabled']);self.assertFalse(cfg['docker_enabled'])
            with patch.dict(os.environ,{'NSC_INTERVAL':'0.05'}):
                self.assertEqual(m.load_config()['interval'],0.05)
            for key,value in [('NSC_INTERVAL','nan'),('NSC_INTERVAL','inf'),('NSC_INTERVAL','0'),('NSC_INTERVAL','0.049'),('NSC_HIST','29'),('NSC_HIST','30.5'),('TRIM_SERVICE_PORT','0'),('TRIM_SERVICE_PORT','65536'),('NSC_AUTH','invalid'),('NSC_BIND',''),('NSC_DOCKER','invalid')]:
                with patch.dict(os.environ,{key:value}),self.assertRaises(m.ConfigError):m.load_config()
            for key,value in [('tls_enabled','invalid'),('tls_enabled',None),('docker_enabled',{}),('interval',True),('interval',float('nan')),('hist_len',30.5),('port',False),('port',65536),('auth_mode','invalid'),('bind',None),('devices',{}),('devices',[{}]),('devices',[None]),('devices',[{'sha256':'é'*64}])]:
                stored=dict(m.DEFAULT_CONFIG);stored[key]=value
                config_path.write_text(json.dumps(stored));before=config_path.read_bytes()
                with self.assertRaises(m.ConfigError):m.load_config()
                self.assertEqual(config_path.read_bytes(),before)
            config_path.write_text(json.dumps({'tls_enabled':False,'docker_enabled':False,'port':12345,'devices':[{'sha256':'a'*64,'name':'fixture'}]}))
            cfg=m.load_config();self.assertFalse(cfg['tls_enabled']);self.assertEqual(cfg['port'],12345)
            with patch.dict(os.environ,{'NSC_TLS':'true','TRIM_SERVICE_PORT':'23456'}):
                cfg=m.load_config();self.assertTrue(cfg['tls_enabled']);self.assertEqual(cfg['port'],23456)
            config_path.unlink();cfg=m.load_config();cfg['devices'].append({});self.assertEqual(m.DEFAULT_CONFIG['devices'],[])
            # Invalid startup is rejected before collectors/listeners/run are created.
            with patch.dict(os.environ,{'NSC_TLS':'invalid-fixture'}),patch.object(m,'run') as run,patch.object(m,'State') as state:
                with self.assertRaises(m.ConfigError):m.main(['--serve'])
                run.assert_not_called();state.assert_not_called()
            # Restore the old production positive-list rule: malformed TLS now
            # reaches the actual load_config/main path and runs in plaintext.
            old_bool=lambda value,key: str(value).strip().lower() in ('1','true','yes','on')
            with patch.dict(os.environ,{'NSC_TLS':'invalid-fixture'}),patch.object(m,'_config_bool',side_effect=old_bool),patch.object(m,'run',return_value=0) as run:
                self.assertFalse(m.load_config()['tls_enabled'])
                self.assertEqual(m.main(['--serve']),0)
                self.assertFalse(run.call_args[0][0]['tls_enabled'])
            config_path.write_text('{"secret-fixture":invalid}')
            with self.assertRaises(m.ConfigError) as error:m.load_config()
            self.assertNotIn('secret-fixture',str(error.exception))
            for value in ('0','-1','65536'):
                with patch.object(m,'run') as run,patch.object(m,'load_config') as load,patch.object(m,'_port_busy') as busy,patch.object(m.sys,'stderr'):
                    with self.assertRaises(SystemExit) as error:m.main(['--check-port',value])
                    self.assertEqual(error.exception.code,2);run.assert_not_called();load.assert_not_called();busy.assert_not_called()

            # Restoring the old truthy option guard makes port zero start run().
            source=textwrap.dedent(inspect.getsource(m.main)).replace('if args.check_port is not None:', 'if args.check_port:')
            namespace=dict(m.__dict__);exec(source,namespace)
            with patch.object(m,'run',return_value=0) as run,patch.object(m,'load_config',return_value=copy.deepcopy(m.DEFAULT_CONFIG)):
                namespace['run']=run;namespace['load_config']=m.load_config
                self.assertEqual(namespace['main'](['--check-port','0']),0)
                run.assert_called_once()

    def test_companion_absolute_connection_budget_and_timer_failure(self):
        import socket,time
        self.assertEqual(m.ThreadedHTTPServer.connection_timeout_s,30.0)
        class Quiet(m.BaseHTTPRequestHandler):
            timeout=1.0
            def log_message(self,*args):pass
        srv=m.ThreadedHTTPServer(('127.0.0.1',0),Quiet);srv.connection_timeout_s=.2
        a,b=socket.socketpair();b.settimeout(1)
        try:
            srv.process_request(a,('anonymous',0))
            until=time.monotonic()+.65
            while time.monotonic()<until:
                try:b.sendall(b'G')
                except OSError:break
                time.sleep(.03)
            with srv._requests_done:self.assertTrue(srv._requests_done.wait_for(lambda:not srv._requests,timeout=1))
            self.assertEqual(b.recv(1),b'')
        finally:srv.server_close();a.close();b.close()
        # Removing the watchdog keeps a header alive despite the absolute budget.
        srv=m.ThreadedHTTPServer(('127.0.0.1',0),Quiet);srv.connection_timeout_s=.05
        a,b=socket.socketpair()
        try:
            with patch.object(m.threading.Timer,'start',return_value=None):
                srv.process_request(a,('anonymous',0))
                for _ in range(5):b.sendall(b'G');time.sleep(.03)
                with srv._requests_done:self.assertTrue(srv._requests)
                srv.server_close()
        finally:srv.server_close();a.close();b.close()
        # Allocation/start errors go to the server error channel, returning slots.
        for failure in ('create','start'):
            srv=m.ThreadedHTTPServer(('127.0.0.1',0),Quiet);a,b=socket.socketpair()
            target=patch.object(m.threading,'Timer',side_effect=MemoryError('fixture')) if failure=='create' else patch.object(m.threading.Timer,'start',side_effect=RuntimeError('fixture'))
            try:
                with target,patch.object(srv,'handle_error') as error,patch.object(m.threading,'excepthook') as unhandled:
                    srv.process_request(a,('anonymous',0))
                    with srv._requests_done:self.assertTrue(srv._requests_done.wait_for(lambda:not srv._requests,timeout=1))
                error.assert_called_once();unhandled.assert_not_called();self.assertEqual(a.fileno(),-1)
                for _ in range(srv.probe_limit):self.assertTrue(srv._probe_slots.acquire(False))
                self.assertFalse(srv._probe_slots.acquire(False))
            finally:srv.server_close();a.close();b.close()

    def test_companion_interrupt_after_spawn_preserves_slot(self):
        import socket,socketserver
        entered,release=threading.Event(),threading.Event()
        class Blocked(socketserver.BaseRequestHandler):
            def handle(self):entered.set();release.wait(3)
        original=socketserver.ThreadingMixIn.process_request
        def interrupted(server,request,address):
            original(server,request,address);raise KeyboardInterrupt
        srv=m.ThreadedHTTPServer(('127.0.0.1',0),Blocked);a,b=socket.socketpair()
        try:
            with patch.object(socketserver.ThreadingMixIn,'process_request',interrupted),patch.object(m.threading,'excepthook') as error:
                with self.assertRaises(KeyboardInterrupt):srv.process_request(a,('anonymous',0))
                self.assertTrue(entered.wait(1));self.assertEqual(srv._requests,{a:a})
                release.set();srv.server_close();error.assert_not_called()
            for _ in range(srv.probe_limit):self.assertTrue(srv._probe_slots.acquire(False))
            self.assertFalse(srv._probe_slots.acquire(False))
        finally:release.set();srv.server_close();a.close();b.close()

    def test_companion_deadline_targets_wrapped_tls_socket(self):
        import socket,ssl,subprocess,tempfile,time
        with tempfile.TemporaryDirectory() as tmp:
            cert,key=str(Path(tmp)/'cert.pem'),str(Path(tmp)/'key.pem')
            subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',key,'-out',cert,'-days','1','-subj','/CN=localhost'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15)
            ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(cert,key)
            srv=m.ThreadedHTTPServer(('127.0.0.1',0),m.Handler);srv.ssl_context=ctx;srv.connection_timeout_s=.3
            a,b=socket.socketpair();b.settimeout(2)
            try:
                b.sendall(bytes([22,3,1,0]));started=time.monotonic()
                srv.process_request(a,('anonymous',0))
                with srv._requests_done:self.assertTrue(srv._requests_done.wait_for(lambda:not srv._requests,timeout=2))
                self.assertLess(time.monotonic()-started,2)
                self.assertEqual(b.recv(1),b'')
            finally:srv.server_close();a.close();b.close()
            # A non-OSError after wrap still owns the detached SSLSocket.
            srv=m.ThreadedHTTPServer(('127.0.0.1',0),m.Handler);srv.ssl_context=ctx
            a,b=socket.socketpair();b.settimeout(2);wrapped=[]
            def failed_handshake(conn):
                wrapped.append(conn);raise RuntimeError('fixture after wrap')
            try:
                b.sendall(bytes([22,3,1,0]))
                with patch.object(ssl.SSLSocket,'do_handshake',failed_handshake),patch.object(srv,'handle_error') as error,patch.object(m.threading,'excepthook') as unhandled:
                    srv.process_request(a,('anonymous',0))
                    with srv._requests_done:self.assertTrue(srv._requests_done.wait_for(lambda:not srv._requests,timeout=2))
                self.assertEqual(len(wrapped),1);self.assertEqual(wrapped[0].fileno(),-1)
                error.assert_called_once();unhandled.assert_not_called();self.assertEqual(b.recv(1),b'')
            finally:srv.server_close();a.close();b.close()
            # Successful TLS then partial HTTP also resolves the detached live fd.
            srv=m.ThreadedHTTPServer(('127.0.0.1',0),m.Handler);srv.ssl_context=ctx;srv.connection_timeout_s=.5
            serve=threading.Thread(target=srv.serve_forever,kwargs={'poll_interval':.01});serve.start()
            raw=socket.create_connection(srv.server_address,timeout=2);client=None
            try:
                client_ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);client_ctx.check_hostname=False;client_ctx.verify_mode=ssl.CERT_NONE
                client=client_ctx.wrap_socket(raw,server_hostname='localhost');client.settimeout(2)
                client.sendall(b'GET / HTTP/1.1'+bytes([13,10])+b'Host:')
                with srv._requests_done:self.assertTrue(srv._requests_done.wait_for(lambda:not srv._requests,timeout=2))
                self.assertEqual(client.recv(1),b'')
            finally:
                if client is not None:client.close()
                raw.close();srv.shutdown();serve.join(2);srv.server_close()
            self.assertFalse(serve.is_alive())

    def test_management_page_requests_and_state(self):
        import re, shutil, subprocess, json
        html=(path.parent.parent/'ui/www/index.html').read_text()
        script=re.search(r'<script>(.*?)</script>',html,re.S).group(1)
        node=shutil.which('node');self.assertIsNotNone(node)
        harness=r'''
const vm=require('node:vm'),assert=require('node:assert/strict');
const source=JSON.parse(process.argv[1]);
function setup(code=source){
 const nodes={},events={},timers=new Map(),intervals=new Map(),requests=[];let id=0;
 function node(name){return nodes[name]||(nodes[name]={value:'',textContent:'',innerHTML:'',style:{},dataset:{},disabled:false,
 matches:()=>false,querySelectorAll:()=>[],classList:{toggle(){}}});}
 const ctx={document:{getElementById:node,querySelectorAll:()=>[],querySelector:()=>node('note')},
 window:{addEventListener:(n,f)=>events[n]=f},location:{pathname:'/app/nasscreencompanion',hostname:'anonymous-fixture'},
 AbortController,Date,Error,JSON,console,confirm:()=>true,
 setTimeout(f,ms){let n=++id;timers.set(n,{f,ms});return n;},clearTimeout:n=>timers.delete(n),
 setInterval(f,ms){let n=++id;intervals.set(n,{f,ms});return n;},clearInterval:n=>intervals.delete(n),
 fetch(url,options){return new Promise((resolve,reject)=>requests.push({url,options,resolve,reject}));}};
 vm.createContext(ctx);
 // Keep production page handlers, but explicitly drive the initial refresh.
 code=code.replace(/\nrefresh\(\);\nrefreshTimer = setInterval\(refresh, 2000\);/,'\nrefreshTimer = setInterval(refresh, 2000);');
 vm.runInContext(code,ctx);
 return {ctx,nodes,events,timers,intervals,requests,node};
}
const flush=async()=>{for(let i=0;i<20;i++)await Promise.resolve();};
function respond(r,value,status=200){r.resolve({ok:status===200,status,text:async()=>typeof value==='string'?value:JSON.stringify(value)});}
async function shapes(code=source){
 const h=setup(code);
 for(const bad of ['null','[]','42','"text"','','<private-login>']){
  const task=h.ctx.get('/api/state');respond(h.requests.at(-1),bad);
  const result=await task;assert.ok(result&&typeof result.error==='string','bad shapes must be errors');
  assert.ok(!result.error.includes('private-login'));
 }
 let task=h.ctx.post('/api/pairing/new',{ttl:300});respond(h.requests.at(-1),{});
 assert.ok((await task).error,'missing success fields rejected');
 task=h.ctx.post('/api/pairing/new',{ttl:300});respond(h.requests.at(-1),{ok:true,code:'123456',ttl:300});
 assert.equal((await task).code,'123456');
 let r=h.requests.at(-1);assert.equal(r.options.method,'POST');assert.equal(r.options.redirect,'error');
 assert.equal(r.options.body,'{"ttl":300}');assert.equal(r.url,'/app/nasscreencompanion/api/pairing/new');
 for(const status of [401,403,500]){
  task=h.ctx.get('/api/state');respond(h.requests.at(-1),'private-login-token',status);
  assert.equal((await task).error,'HTTP '+status);
 }
 task=h.ctx.get('/api/state');h.requests.at(-1).reject(new Error('private-token-url'));
 assert.ok(!(await task).error.includes('private-token-url'));
 assert.equal(h.timers.size,0);
 let cancelled=false;task=h.ctx.get('/api/state');r=h.requests.at(-1);
 r.resolve({ok:false,status:500,body:{cancel:async()=>{cancelled=true;}}});
 assert.equal((await task).error,'HTTP 500');assert.ok(cancelled);assert.equal(r.options.signal.aborted,true);
 const click=h.node('gencode').onclick();respond(h.requests.at(-1),'null');await click;
 assert.ok(h.node('codehint').textContent);assert.notEqual(h.node('codewrap').style.display,'');
}
async function timeout(code=source){
 const h=setup(code);const task=h.ctx.get('/api/state');const r=h.requests.at(-1);
 const timer=[...h.timers.values()].find(t=>t.ms===10000);assert.ok(timer,'finite production deadline');timer.f();
 const result=await task;assert.ok(result.error);assert.equal(r.options.signal.aborted,true);
 respond(r,{config:{},collector:{},devices:[],tls:{}});await flush();assert.equal(h.timers.size,0);
 const second=h.ctx.get('/api/state');h.requests.at(-1).resolve({ok:true,status:200,text:()=>new Promise(()=>{})});await flush();
 [...h.timers.values()].find(t=>t.ms===10000).f();assert.ok((await second).error,'body read has same deadline');
}
async function lifecycle(code=source){
 const h=setup(code);h.node('hhint').textContent='sentinel';
 const task=vm.runInContext('refresh()',h.ctx);const r=h.requests.at(-1);
 h.events.pagehide();assert.equal(r.options.signal.aborted,true);assert.equal(h.intervals.size,0);
 respond(r,{app:'late',config:{},collector:{},devices:[],tls:{}});await task;
 assert.equal(h.node('hhint').textContent,'sentinel','hidden late response cannot render');
 assert.equal(h.requests.length,1);assert.equal(h.timers.size,0);
 h.events.pageshow();assert.equal(h.intervals.size,1);assert.equal(h.requests.length,2);
 respond(h.requests.at(-1),'null');await flush();
 const x=setup(code);x.node('hhint').textContent='save-sentinel';
 const pending=vm.runInContext('refresh()',x.ctx);
 vm.runInContext('configSaving=true;++refreshSerial',x.ctx);
 respond(x.requests.at(-1),{app:'old',config:{},collector:{},devices:[],tls:{}});await pending;
 assert.equal(x.node('hhint').textContent,'save-sentinel','pre-save refresh must not overwrite');
}
function rendering(code=source){
 const h=setup(code);
 h.ctx.renderTls({tls:{configured:true,enabled:false,error:'fixture'}});
 assert.ok(h.node('tlsline').innerHTML.includes('停止监听'));
 assert.ok(!h.node('tlsline').innerHTML.includes('明文 HTTP'));
 h.ctx.renderDiag({error:'<anonymous>'});assert.ok(h.node('diagrows').innerHTML.includes('&lt;anonymous&gt;'));
 const vols=Array.from({length:73},(_,i)=>({mnt:'unique-volume-'+i,pct:10}));
 const raid=Array.from({length:73},(_,i)=>({dev:'unique-array-'+i,ok:true}));
 h.ctx.renderCards({snapshot:{vols,raid}});
 for(let i=0;i<73;i++){assert.ok(h.node('cards').innerHTML.includes('unique-volume-'+i+'<'));assert.ok(h.node('cards').innerHTML.includes('unique-array-'+i+'<'));}
}
async function partialSave(){
 const h=setup();for(const [id,value] of Object.entries({'c-port':'8798','c-bind':'0.0.0.0','c-interval':'1','c-hist':'600','c-auth':'pairing','c-tls':'1','c-docker':'1'}))h.node(id).value=value;
 vm.runInContext('configDirty=true',h.ctx);const task=h.node('savecfg').onclick();
 respond(h.requests.at(-1),{ok:true,changed:['port'],restart_required:['port']});await flush();
 assert.ok(h.requests.at(-1).url.endsWith('/api/collector/docker'));
 respond(h.requests.at(-1),'anonymous failure',500);await task;
 assert.ok(h.node('cfghint').textContent.includes('服务配置已保存'));
 assert.ok(h.node('cfghint').textContent.includes('未确认'));
 assert.equal(vm.runInContext('configDirty',h.ctx),true);assert.equal(h.node('savecfg').disabled,false);
}
(async()=>{
 await shapes();await timeout();await lifecycle();rendering();await partialSave();
 let failed=false;
 try{await shapes(source.replace("if(!value || typeof value !== 'object' || Array.isArray(value))",'if(!value) return value;\n      if(false)'));}catch(e){assert.equal(e.code,'ERR_ASSERTION');failed=true;}
 assert.ok(failed,'null-shape negative must fail');
 failed=false;try{rendering(source.replace('vols.map','vols.slice(0,4).map'));}catch(e){assert.equal(e.code,'ERR_ASSERTION');failed=true;}
 assert.ok(failed,'four-item negative must fail');
 failed=false;try{await lifecycle(source.replace('    if(!current()) return;','    // removed freshness check'));}catch(e){assert.equal(e.code,'ERR_ASSERTION');failed=true;}
 assert.ok(failed,'late-response negative must fail');
 console.log('production admin request/render/lifecycle/partial-save and three negatives PASS');
})().catch(e=>{console.error(e);process.exitCode=1;});
'''
        result=subprocess.run([node,'-e',harness,json.dumps(script)],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('three negatives PASS',result.stdout)

    def test_legacy_diagnostic_page_session(self):
        import re
        import shutil
        import subprocess
        import json
        legacy_path = Path(__file__).resolve().parents[1] / 'fnos-agent.py'
        spec = importlib.util.spec_from_file_location('legacy_page_fixture', legacy_path)
        legacy = importlib.util.module_from_spec(spec); spec.loader.exec_module(legacy)
        html = legacy.INDEX_HTML
        script = re.search(r'<script>(.*?)</script>', html, re.S).group(1)
        self.assertIn('type="password"', html)
        self.assertIn('明文传输', html)
        self.assertNotIn('localStorage', script)
        self.assertNotIn('sessionStorage', script)
        self.assertNotIn('console.', script)
        node = shutil.which('node')
        self.assertIsNotNone(node, 'Node required for production page regression')
        harness = r'''
const vm=require('node:vm'), assert=require('node:assert/strict');
const source=JSON.parse(process.argv[1]);
async function run(code, negative=false){
 const elements=Object.fromEntries(['auth','token','status','j','stop'].map(id=>[id,{value:'',textContent:'',disabled:true,events:{},addEventListener(n,f){this.events[n]=f;}}]));
 const timers=new Map(), requests=[], events={};let sequence=0;
 const context={document:{getElementById:id=>elements[id]},window:{addEventListener(n,f){events[n]=f;}},AbortController,Date,Error,JSON,
 setTimeout(f,ms){const id=++sequence;timers.set(id,{f,ms});return id;},clearTimeout(id){timers.delete(id);},
 fetch(url,options){return new Promise((resolve,reject)=>requests.push({url,options,resolve,reject}));}};
 vm.createContext(context);vm.runInContext(code,context);
 const flush=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};
 const submit=value=>{elements.token.value=value;elements.auth.events.submit({preventDefault(){}});};
 const response=(request,status,data)=>request.resolve({status,ok:status===200,json:async()=>data});
 assert.equal(requests.length,0,'no unauthenticated automatic poll');
 submit('fixture-secret');assert.equal(elements.token.value,'');
 assert.equal(requests[0].url,'/api/v1/status');assert.equal(requests[0].options.headers['X-Token'],'fixture-secret');
 assert.equal(requests[0].options.redirect,'error');assert.equal(requests[0].options.credentials,'omit');
 submit('replacement');assert.equal(requests[0].options.signal.aborted,true);
 response(requests[0],200,{host:'old-host'});await flush();assert.ok(!elements.status.textContent.includes('old-host'),'late session cannot publish');
 response(requests[1],200,{host:'current-host',note:'<script>fixture</script>'});await flush();
 assert.ok(elements.status.textContent.includes('current-host'));assert.ok(elements.j.textContent.includes('<script>fixture</script>'));
 assert.equal([...timers.values()].filter(t=>t.ms===1000).length,1);
 elements.stop.events.click();assert.equal(elements.j.textContent,'连接后显示采集状态。');assert.equal(timers.size,0);
 assert.equal(vm.runInContext('token',context),'');assert.equal(elements.stop.disabled,true);
 submit('bad-secret');response(requests[2],401,{error:'bad-secret'});await flush();
 assert.ok(elements.status.textContent.includes('鉴权失败'));assert.equal(timers.size,0);assert.equal(vm.runInContext('token',context),'');
 assert.ok(!elements.j.textContent.includes('bad-secret'));
 submit('valid');response(requests[3],200,[]);await flush();assert.ok(elements.status.textContent.includes('读取失败'));assert.ok(!elements.j.textContent.includes('current-host'));
 elements.stop.events.click();submit('valid');requests[4].reject(new Error('private-url-token'));await flush();assert.ok(!elements.status.textContent.includes('private-url-token'));
 const retry=[...timers.entries()].find(([id,t])=>t.ms===1000);timers.delete(retry[0]);retry[1].f();assert.equal(requests.length,6);
 const timeout=[...timers.values()].find(t=>t.ms===10000);timeout.f();assert.equal(requests[5].options.signal.aborted,true);
 requests[5].reject(new Error('abort'));await flush();
 events.pagehide();assert.equal(vm.runInContext('token',context),'');assert.equal(timers.size,0);
 submit('');assert.equal(Object.keys(requests[6].options.headers).length,0);elements.stop.events.click();
}
(async()=>{await run(source);let failed=false;try{await run(source.replace("token='';input.value='';", "input.value='';"));}catch(e){assert.ok(String(e).includes('fixture-secret')||e.code==='ERR_ASSERTION');failed=true;}assert.ok(failed,'no-token-clear negative must fail');console.log('production page session checks and clear negative PASS');})().catch(e=>{console.error(e);process.exitCode=1;});
'''
        result = subprocess.run([node, '-e', harness, json.dumps(script)],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('negative PASS', result.stdout)

    def test_pair_code_invalid_shapes_preserve_pending(self):
        self.state.new_pair_code()
        before = copy.deepcopy(self.state.pending_pair)
        for code in ('é', '１２３４５６', '12345', '1234567', 123456, None, {}, []):
            token, error = self.state.try_pair(code, 'fixture', 'fixture')
            self.assertIsNone(token)
            self.assertTrue(error)
            self.assertEqual(self.state.pending_pair, before)
            m.save_config.assert_not_called()
        source = textwrap.dedent(inspect.getsource(m.State.try_pair))
        start = source.index('        if (not isinstance')
        end = source.index('\n            return None, "配对码不正确"', start)
        source = source[:start] + '        if not hmac.compare_digest(str(code), pend["code"]):' + source[end:]
        namespace = dict(m.__dict__); exec(source, namespace)
        with self.assertRaises(TypeError):
            namespace['try_pair'](self.state, 'é', 'fixture', 'fixture')

    def test_pair_rejects_non_string_device_name(self):
        self.state.new_pair_code()
        code = self.state.pending_pair['code']
        before = copy.deepcopy(self.state.pending_pair)
        for name in (123, True, {}, []):
            token, error = self.state.try_pair(code, name, 'fixture')
            self.assertIsNone(token)
            self.assertIn('设备名', error)
            self.assertEqual(self.state.pending_pair, before)
            m.save_config.assert_not_called()

    def test_legacy_unicode_token_and_invalid_startup(self):
        import sys
        legacy_path = Path(__file__).resolve().parents[1] / 'fnos-agent.py'
        spec = importlib.util.spec_from_file_location('legacy_auth_fixture', legacy_path)
        legacy = importlib.util.module_from_spec(spec); spec.loader.exec_module(legacy)
        h = object.__new__(legacy.Handler)
        h.server = type('Server', (), {'token':'fixture-token'})()
        for header, query, expected in (('é','','False'), ('fixture-token','','True'),
                                        ('','é','False'), ('','fixture-token','True')):
            h.headers = {'X-Token':header}; h.path = '/status?token=' + query
            self.assertEqual(h._authorized(), expected == 'True')
        h.server.token = 'é'; h.headers = {'X-Token':'é'}
        self.assertTrue(h._authorized())
        source = textwrap.dedent(inspect.getsource(legacy.Handler._authorized)).replace('.encode("utf-8")', '')
        namespace = dict(legacy.__dict__); exec(source, namespace)
        h.server.token = 'fixture-token'
        with self.assertRaises(TypeError): namespace['_authorized'](h)
        with patch.object(legacy, 'Server') as server, patch.object(legacy, 'Collector') as collector:
            for args in (['--interval', 'nan'], ['--interval', 'inf'], ['--interval', '0'],
                         ['--interval', '-1'], ['--port', '65536']):
                with patch.object(sys, 'argv', ['agent'] + args), self.assertRaises(SystemExit) as caught:
                    legacy.main()
                self.assertEqual(caught.exception.code, 2)
            server.assert_not_called(); collector.assert_not_called()

    def legacy_module(self):
        legacy_path = Path(__file__).resolve().parents[1] / 'fnos-agent.py'
        spec=importlib.util.spec_from_file_location('legacy_lifecycle_fixture',legacy_path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        return module

    def test_legacy_sampler_lifecycle(self):
        legacy=self.legacy_module();col=legacy.Collector(interval=0.01)
        entered,release=threading.Event(),threading.Event()
        def blocked_sample():
            entered.set();release.wait(3)
            return {'ts':1,'cpu':{'pct':1},'mem':{'pct':1},'net':{'rx_kbs':1,'tx_kbs':1}}
        col.sample=blocked_sample
        try:
            worker=col.start();self.assertTrue(entered.wait(1))
            self.assertIs(col.start(),worker)
            self.assertFalse(col.stop(timeout=0.01))
            self.assertIs(col._thread,worker)
            with self.assertRaises(RuntimeError):col.start()
            release.set();self.assertTrue(col.stop(timeout=1))
            self.assertIsNone(col._thread)
            self.assertEqual(col.history,[])  # Late sample after stop cannot publish.
            next_worker=col.start();self.assertIsNot(next_worker,worker)
        finally:
            release.set();self.assertTrue(col.stop(timeout=2))
        with patch.object(legacy.threading,'Thread') as factory:
            factory.return_value.start.side_effect=RuntimeError('anonymous start failure')
            with self.assertRaises(RuntimeError):col.start()
            self.assertIsNone(col._thread)
        source=textwrap.dedent(inspect.getsource(legacy.Collector.stop))
        namespace=dict(legacy.__dict__);exec('def stop(self, timeout=5):\n    self._stop.set()\n',namespace)
        self.assertNotEqual(source,'')
        self.assertIsNone(namespace['stop'](col))  # Old flag-only stop lacks completion receipt.

    def test_legacy_startup_cleanup_and_empty_auth(self):
        import os,sys,signal
        legacy=self.legacy_module()
        h=object.__new__(legacy.Handler);h.server=type('S',(),{'token':''})();h.headers={};h.path='/status'
        self.assertFalse(h._authorized());h.server.insecure_no_auth=True;self.assertTrue(h._authorized())
        previous=signal.getsignal(signal.SIGTERM)
        for fault,want in [('start',1),('warmup',0),('serve',1),('close',1),('stop',1),('normal',0)]:
            with self.subTest(fault=fault),patch.dict(os.environ,{'FNAS_TOKEN':'anonymous-test'}),patch.object(sys,'argv',['agent','--port','0','--bind','127.0.0.1']),patch.object(legacy,'Server') as factory,patch.object(legacy,'Collector') as constructor,patch.object(legacy.time,'sleep') as sleep:
                srv=factory.return_value;col=constructor.return_value;col.stop.return_value=True
                if fault=='start':col.start.side_effect=RuntimeError('fault')
                if fault=='warmup':sleep.side_effect=KeyboardInterrupt
                if fault=='serve':srv.serve_forever.side_effect=RuntimeError('fault')
                if fault=='close':srv.server_close.side_effect=OSError('fault')
                if fault=='stop':col.stop.return_value=False
                self.assertEqual(legacy.main(),want)
                srv.server_close.assert_called_once();col.stop.assert_called_once()
                self.assertIs(signal.getsignal(signal.SIGTERM),previous)

    def test_legacy_sigterm_during_warmup(self):
        import os,sys,subprocess,select,signal,socket
        legacy_path=Path(__file__).resolve().parents[1]/'fnos-agent.py'
        code=r"""
import importlib.util,sys,os,time
spec=importlib.util.spec_from_file_location('legacy_process_fixture',sys.argv[1])
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.Collector.sample=lambda self:{'ts':1,'ready':True,'cpu':{'pct':1},'mem':{'pct':1},'net':{'rx_kbs':1,'tx_kbs':1}}
real_server=m.Server
port=[]
def make_server(*args):
    srv=real_server(*args);port.append(srv.server_address[1]);return srv
m.Server=make_server
real_sleep=time.sleep
def warmup(seconds):
    print('READY',port[0],flush=True);real_sleep(10)
m.time.sleep=warmup
os.environ['FNAS_TOKEN']='anonymous-fixture'
sys.argv=['agent','--bind','127.0.0.1','--port','0']
raise SystemExit(m.main())
"""
        with subprocess.Popen([sys.executable,'-c',code,str(legacy_path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True) as child:
            try:
                self.assertTrue(select.select([child.stdout],[],[],10)[0])
                line=child.stdout.readline().strip();self.assertTrue(line.startswith('READY '),line)
                port=int(line.split()[1]);child.send_signal(signal.SIGTERM)
                out,err=child.communicate(timeout=10)
                self.assertEqual(child.returncode,0,err)
                with socket.socket() as probe:
                    probe.bind(('127.0.0.1',port))
            finally:
                if child.poll() is None:child.kill();child.wait(timeout=5)

    def test_legacy_sample_keeps_published_cpu_immutable(self):
        legacy=self.legacy_module();col=legacy.Collector()
        old={'ready':True,'cpu':{'pct':5,'temp_c':10}}
        col.snapshot=old;col._cpu_temp=99
        with patch.object(col,'_cpu',side_effect=OSError('fault')):
            with patch.object(col,'_mem',return_value={}),patch.object(col,'_net',return_value={}),patch.object(col,'_volumes',return_value=[]),patch.object(col,'_raid',return_value=[]),patch.object(col,'_disk_io',return_value=[]),patch.object(col,'_temps',return_value=[]),patch.object(col,'_docker',return_value=[]),patch.object(col,'_zfs',return_value={}),patch.object(col,'_alerts',return_value=[]):
                result=col.sample()
        self.assertEqual(old['cpu']['temp_c'],10)
        self.assertEqual(result['cpu']['temp_c'],99)
        self.assertIsNot(result['cpu'],old['cpu'])

    def test_legacy_raid_health_and_maintenance(self):
        legacy=self.legacy_module()
        spec=importlib.util.spec_from_file_location('raid_companion_fixture',Path(__file__).parent/'nasscreencompanion/app/server/fnos_collector.py')
        companion=importlib.util.module_from_spec(spec);spec.loader.exec_module(companion)
        healthy="md0 : active raid1 sda1[0] sdb1[1]\n  100 blocks [2/2] [UU]\n"
        degraded="md0 : active raid1 sda1[0]\n  100 blocks [2/1] [U_]\n"
        fixtures=[(healthy,'ok',None),(degraded,'degraded','crit'),
            ('md0 : inactive sda1[0](S)\n','inactive','crit'),
            ('md0 : active raid0 sda1[0] sdb1[1]\n','ok',None),
            ('md0 : active linear sda1[0]\n','ok',None),
            ('md0 : active raid5 sda1[0]\n','unknown','info'),
            ('md0 : active container imsm metadata\n','container','info'),
            (healthy.replace('active raid1','active (read-only) raid1'),'readonly','warn')]
        for action in ('check','resync','repair','recovery'):
            progress='  [=>........] %s = 25.0%% (1/4)\n'%action
            fixtures.extend([(healthy+progress,'ok','info'),(degraded+progress,'degraded','crit')])
        col=legacy.Collector();other=companion.CompanionCollector()
        for raw,health,level in fixtures:
            with self.subTest(health=health,raw=raw),patch.object(legacy,'read_text_strict',return_value=raw),patch.object(companion,'read_text_strict',return_value=raw):
                rows=col._raid();self.assertEqual(rows,other._raid());self.assertEqual(rows[0]['health'],health)
                self.assertEqual(rows[0]['ok'],health=='ok')
                if health=='readonly':self.assertEqual(rows[0]['lvl'],'raid1')
                alerts=col._alerts({'raid':rows,'cpu':None,'mem':None,'vols':None,'docker':None})
                self.assertEqual([a['lv'] for a in alerts],[] if level is None else [level])
                if '25.0%' in raw:self.assertIn('25.0%',alerts[0]['m'])
        with patch.object(legacy,'read_text_strict',return_value=healthy+'  [=>.] check = 120.0%\n'):
            self.assertEqual(col._raid()[0]['sync_pct'],-1)
        dense=''.join('md%d : active raid1 sda1[0] sdb1[1]\n  100 blocks [2/2] [UU]\n'%i for i in range(73))
        with patch.object(legacy,'read_text_strict',return_value=dense):
            rows=col._raid();self.assertEqual(len(rows),73);self.assertTrue(all(r['health']=='ok' for r in rows))

    def test_legacy_raid_read_failure_keeps_shape_and_previous(self):
        legacy=self.legacy_module();col=legacy.Collector()
        for name,value in [('_cpu',{}),('_mem',{}),('_net',{}),('_volumes',[]),('_disk_io',[]),('_temps',[]),('_docker',[]),('_zfs',{})]:
            active=patch.object(col,name,return_value=value);active.start();self.addCleanup(active.stop)
        with patch.object(legacy,'read_text_strict',side_effect=PermissionError('anonymous denied')):
            with self.assertRaises(PermissionError):col._raid()
            first=col.sample();self.assertEqual(first['raid'],[]);self.assertIn('raid',first['errors'])
            previous=[{'dev':'md0','health':'degraded','ok':False,'state':'active','sync_pct':-1}]
            col.snapshot={'ready':True,'raid':previous}
            current=col.sample();self.assertEqual(current['raid'],previous);self.assertIsNot(current['raid'],previous)
            self.assertEqual(current['alerts'][0]['lv'],'crit')
        with patch.object(legacy,'read_text_strict',return_value='Personalities : [raid1]\nunused devices: <none>\n'):
            self.assertEqual(col._raid(),[])

    def test_legacy_raid_old_alert_and_read_negatives(self):
        legacy=self.legacy_module();col=legacy.Collector()
        row={'dev':'md0','health':'degraded','ok':False,'state':'active','sync_pct':25.0,'what':'recovery'}
        source=textwrap.dedent(inspect.getsource(legacy.Collector._alerts))
        namespace=dict(legacy.__dict__)
        old=source.replace('if health == "degraded":','if health == "degraded" and not prog:')
        self.assertNotEqual(source,old);exec(old,namespace)
        self.assertEqual(col._alerts({'raid':[row]})[0]['lv'],'crit')
        self.assertEqual(namespace['_alerts'](col,{'raid':[row]})[0]['lv'],'info')
        source=textwrap.dedent(inspect.getsource(legacy.Collector._raid))
        old=source.replace('read_text_strict("/proc/mdstat")','read_text("/proc/mdstat")')
        self.assertNotEqual(source,old);namespace=dict(legacy.__dict__);namespace['read_text']=lambda path:''
        exec(old,namespace)
        with patch.object(legacy,'read_text_strict',side_effect=PermissionError):
            with self.assertRaises(PermissionError):col._raid()
            self.assertEqual(namespace['_raid'](col),[])

    def test_legacy_connection_quota_drain_and_start_failure(self):
        import socket,socketserver,time
        legacy=self.legacy_module();release=threading.Event();entered=threading.Condition();count=[0]
        class Blocked(socketserver.BaseRequestHandler):
            def handle(self):
                with entered:count[0]+=1;entered.notify_all()
                release.wait(3)
        with patch.object(legacy.Server,'connection_limit',2):srv=legacy.Server(('127.0.0.1',0),Blocked)
        pairs=[]
        try:
            for _ in range(2):
                a,b=socket.socketpair();pairs.append((a,b));srv.process_request(a,('anonymous',0))
            with entered:self.assertTrue(entered.wait_for(lambda:count[0]==2,timeout=1))
            a,b=socket.socketpair();pairs.append((a,b));b.settimeout(1)
            srv.process_request(a,('anonymous',0));self.assertEqual(b.recv(1),b'')
            self.assertEqual(srv._connection_rejected,1);self.assertEqual(len(srv._requests),2)
            srv.drain_timeout_s=0.02
            with self.assertRaises(TimeoutError):srv.server_close()
            self.assertTrue(srv._closing);self.assertEqual(len(srv._requests),2)
            a,b=socket.socketpair();pairs.append((a,b));b.settimeout(1)
            srv.process_request(a,('anonymous',0));self.assertEqual(b.recv(1),b'')
            release.set();srv.drain_timeout_s=1;srv.server_close();self.assertFalse(srv._requests)
        finally:
            release.set();srv.drain_timeout_s=2;srv.server_close()
            for a,b in pairs:a.close();b.close()
        srv=legacy.Server(('127.0.0.1',0),Blocked)
        a,b=socket.socketpair()
        try:
            with patch.object(legacy.threading.Thread,'start',side_effect=RuntimeError('anonymous create failure')):
                with self.assertRaises(RuntimeError):srv.process_request(a,('anonymous',0))
            self.assertEqual(srv._requests,set());self.assertEqual(a.fileno(),-1)
            # Every slot is returned even when spawning fails.
            for _ in range(srv.connection_limit):self.assertTrue(srv._connection_slots.acquire(False))
            self.assertFalse(srv._connection_slots.acquire(False))
        finally:srv.server_close();a.close();b.close()

    def test_legacy_partial_http_close_and_missing_drain_negative(self):
        import socket,socketserver,time
        legacy=self.legacy_module();srv=legacy.Server(('127.0.0.1',0),legacy.Handler)
        a,b=socket.socketpair();b.settimeout(1)
        try:
            srv.process_request(a,('anonymous',0));b.sendall(b'GET /health HTTP/1.1\r\nHost: fixture\r\n')
            srv.server_close();self.assertFalse(srv._requests);self.assertEqual(b.recv(1),b'')
        finally:srv.server_close();a.close();b.close()
        source=textwrap.dedent(inspect.getsource(legacy.Server.server_close))
        old=source.replace('while self._requests:','while False:')
        self.assertNotEqual(source,old)
        # __class__ cell belongs to Server; keep super() valid in this extracted method.
        old=old.replace('super().server_close()','socketserver.ThreadingMixIn.server_close(self)')
        namespace=dict(legacy.__dict__);exec(old,namespace)
        release,entered=threading.Event(),threading.Event()
        class Blocked(socketserver.BaseRequestHandler):
            def handle(self):entered.set();release.wait(3)
        srv=legacy.Server(('127.0.0.1',0),Blocked);a,b=socket.socketpair()
        try:
            srv.process_request(a,('anonymous',0));self.assertTrue(entered.wait(1))
            namespace['server_close'](srv)
            self.assertTrue(srv._requests)  # Old no-drain close reports success while worker remains.
            srv.drain_timeout_s=0.01
            with self.assertRaises(TimeoutError):srv.server_close()
        finally:
            release.set();srv.drain_timeout_s=2;srv.server_close();a.close();b.close()

    def test_legacy_absolute_connection_budget_and_timer_failure(self):
        import socket,time
        legacy=self.legacy_module()
        self.assertEqual(legacy.Server.connection_limit,32)
        self.assertEqual(legacy.Server.connection_timeout_s,30.0)
        class Quiet(legacy.Handler):timeout=1.0
        srv=legacy.Server(('127.0.0.1',0),Quiet);srv.connection_timeout_s=0.2
        a,b=socket.socketpair();b.settimeout(1)
        try:
            srv.process_request(a,('anonymous',0))
            until=time.monotonic()+0.7
            while time.monotonic()<until:
                try:b.sendall(b'G')
                except OSError:break
                time.sleep(0.03)  # Frequent bytes stay below the idle timeout.
            with srv._requests_done:self.assertTrue(srv._requests_done.wait_for(lambda:not srv._requests,timeout=1))
            self.assertEqual(b.recv(1),b'')
        finally:srv.server_close();a.close();b.close()
        # Negative: never starting the production watchdog keeps a trickled header live.
        srv=legacy.Server(('127.0.0.1',0),Quiet);srv.connection_timeout_s=0.05
        a,b=socket.socketpair()
        try:
            with patch.object(legacy.threading.Timer,'start',return_value=None):
                srv.process_request(a,('anonymous',0))
                for _ in range(5):b.sendall(b'G');time.sleep(0.03)
                self.assertTrue(srv._requests)
                srv.server_close()
        finally:srv.server_close();a.close();b.close()
        # Watchdog allocation/start failure must return both request and slot ownership.
        srv=legacy.Server(('127.0.0.1',0),Quiet);a,b=socket.socketpair()
        try:
            with patch.object(legacy.threading.Timer,'start',side_effect=RuntimeError('anonymous timer failure')),patch.object(srv,'handle_error') as error,patch.object(legacy.threading,'excepthook') as unhandled:
                srv.process_request(a,('anonymous',0))
                with srv._requests_done:self.assertTrue(srv._requests_done.wait_for(lambda:not srv._requests,timeout=1))
            error.assert_called_once();unhandled.assert_not_called()
            self.assertEqual(a.fileno(),-1)
            for _ in range(srv.connection_limit):self.assertTrue(srv._connection_slots.acquire(False))
        finally:srv.server_close();a.close();b.close()

    def test_legacy_real_http_after_connection_guards(self):
        import http.client,json,time
        legacy=self.legacy_module();srv=legacy.Server(('127.0.0.1',0),legacy.Handler)
        srv.token='anonymous-fixture';srv.collector=type('C',(),{
            'lock':threading.Lock(),'snapshot':{'ready':True,'ts':int(time.time()),'host':'fixture'},
            'history':[[1,2,3,4,5],[2,3,4,5,6]]})()
        worker=threading.Thread(target=srv.serve_forever,kwargs={'poll_interval':0.01},daemon=True)
        worker.start()
        conn=http.client.HTTPConnection(*srv.server_address,timeout=2)
        try:
            for path,headers,want in [('/health',{},200),('/status',{},401),('/status',{'X-Token':srv.token},200),('/history?n=1',{'X-Token':srv.token},200)]:
                conn.request('GET',path,headers=headers);response=conn.getresponse();body=json.loads(response.read())
                self.assertEqual(response.status,want)
                if path.startswith('/history'):self.assertEqual(body['rows'],[[2,3,4,5,6]])
                if path=='/health':self.assertTrue(body['ok'])
        finally:
            conn.close();srv.shutdown();worker.join(timeout=2);srv.server_close()
        self.assertFalse(worker.is_alive());self.assertEqual(srv._requests,set())

    def test_legacy_interrupt_after_worker_start_keeps_ownership(self):
        import socket,socketserver
        legacy=self.legacy_module();entered,release=threading.Event(),threading.Event()
        class Blocked(socketserver.BaseRequestHandler):
            def handle(self):entered.set();release.wait(3)
        original=socketserver.ThreadingMixIn.process_request
        def interrupted(server,request,address):
            original(server,request,address)
            raise KeyboardInterrupt
        srv=legacy.Server(('127.0.0.1',0),Blocked);a,b=socket.socketpair()
        try:
            with patch.object(socketserver.ThreadingMixIn,'process_request',interrupted),patch.object(legacy.threading,'excepthook') as error:
                with self.assertRaises(KeyboardInterrupt):srv.process_request(a,('anonymous',0))
                self.assertTrue(entered.wait(1));self.assertEqual(srv._requests,{a})
                release.set();srv.server_close();error.assert_not_called()
            for _ in range(srv.connection_limit):self.assertTrue(srv._connection_slots.acquire(False))
            self.assertFalse(srv._connection_slots.acquire(False))
        finally:release.set();srv.server_close();a.close();b.close()

    def test_legacy_docker_http_framing_and_budget(self):
        legacy=self.legacy_module()
        class FakeSocket:
            def __init__(self,raw,fail=None):self.raw=raw;self.closed=False;self.fail=fail;self.timeouts=[]
            def settimeout(self,value):self.timeouts.append(value)
            def connect(self,path):
                if self.fail=='connect':raise OSError('anonymous connect failure')
            def sendall(self,data):
                if self.fail=='send':raise OSError('anonymous send failure')
            def recv(self,n):
                if self.fail=='recv':raise OSError('anonymous read failure')
                part=self.raw[:min(n,7)];self.raw=self.raw[len(part):];return part
            def close(self):self.closed=True
        def request(raw,fail=None,limit=None):
            sock=FakeSocket(raw,fail)
            try:
                with patch.object(legacy.socket,'socket',return_value=sock),patch.object(legacy,'DOCKER_HTTP_MAX_BYTES',limit or legacy.DOCKER_HTTP_MAX_BYTES):
                    return legacy._http_unix('/anonymous/socket','/fixture')
            finally:self.assertTrue(sock.closed);self.assertTrue(all(t>0 for t in sock.timeouts))
        valid=[b'HTTP/1.0 200 OK\r\n\r\n{}',b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}',
               b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2;fixture=yes\r\n{}\r\n0\r\nX-Fixture: ok\r\n\r\n']
        for raw in valid:self.assertEqual(request(raw),(200,b'{}'))
        bad=[b'',b'HTTP/1.1 200 OK\r\n',b'bad\r\n\r\n{}',
             b'HTTP/1.1 200 OK\r\nBadHeader\r\n\r\n{}',
             b'HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n{}',
             b'HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n{}',
             b'HTTP/1.1 200 OK\r\nContent-Length: +2\r\n\r\n{}',
             b'HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\n{}',
             b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}',
             b'HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n{}',
             b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 2\r\n\r\n{}']
        for raw in bad:
            with self.subTest(raw=raw),self.assertRaises(ValueError):request(raw)
        with self.assertRaises(ValueError):request(b'HTTP/1.0 200 OK\r\n\r\n'+b'x'*70,limit=64)
        for fail in ('connect','send','recv'):
            with self.assertRaises(OSError):request(b'',fail)
        sock=FakeSocket(b'HTTP/1.0 200 OK\r\n\r\n{}')
        with patch.object(legacy.socket,'socket',return_value=sock),patch.object(legacy.time,'monotonic',side_effect=[0,0.1,4]):
            with self.assertRaises(TimeoutError):legacy._http_unix('/anonymous/socket','/fixture',timeout=3)
        self.assertTrue(sock.closed)

    def test_legacy_chunk_exactness_and_old_partial_negative(self):
        legacy=self.legacy_module()
        self.assertEqual(legacy._dechunk(b'1\r\n{\r\n1\r\n}\r\n0\r\n\r\n'),b'{}')
        malformed=[b'2\r\n{}\r\n',b'2\r\n{}xx0\r\n\r\n',b'2\r\n{}\r\n0\r\n\r\nJUNK',
                   b'-1\r\n{}\r\n0\r\n\r\n',b'+2\r\n{}\r\n0\r\n\r\n',b'3\r\n{}\r\n0\r\n\r\n',
                   b'2\r\n{}\r\n0\r\nBadTrailer\r\n\r\n',b'2\r\n{}\r\n0\r\nContent-Length: 2\r\n\r\n',
                   b'ffffffffffffffffffffffff\r\n',b'0\r\n']
        for raw in malformed:
            with self.subTest(raw=raw),self.assertRaises(ValueError):legacy._dechunk(raw)
        with patch.object(legacy,'DOCKER_HTTP_MAX_BYTES',2),self.assertRaises(ValueError):legacy._dechunk(b'2\r\n{}\r\n0\r\n\r\n')
        # Exact old production algorithm: missing zero chunk used to produce valid JSON.
        def old_dechunk(data):
            out=bytearray()
            while True:
                pos=data.find(b'\r\n')
                if pos<0:break
                try:size=int(data[:pos].split(b';')[0],16)
                except ValueError:break
                if size==0:break
                out+=data[pos+2:pos+2+size];data=data[pos+2+size+2:]
            return bytes(out)
        for raw in malformed[:3]:self.assertEqual(old_dechunk(raw),b'{}')

    def test_legacy_docker_failure_cache_remains_observable(self):
        import json
        legacy=self.legacy_module();col=legacy.Collector()
        previous=[{'n':'fixture','up':True,'s':'Up'}];col._docker_cache=(0,previous)
        with patch.object(legacy.time,'time',return_value=10),patch.object(legacy,'_http_unix',side_effect=ValueError('anonymous bad framing')) as request:
            with self.assertRaises(RuntimeError):col._docker()
            with self.assertRaises(RuntimeError):col._docker()
            self.assertEqual(request.call_count,1);self.assertIs(col._docker_cache[1],previous)
        bad_rows=[{},[None],[{'Names':'/wrong','State':'running'}],
                  [{'Names':['/'],'State':'running'}],[{'Names':['/ok']}],
                  [{'Names':['/ok'],'State':'running','Status':42}]]
        for i,rows in enumerate(bad_rows):
            with patch.object(legacy.time,'time',return_value=20+i*6),patch.object(legacy,'_http_unix',return_value=(200,json.dumps(rows).encode())):
                with self.assertRaises(RuntimeError):col._docker()
                self.assertIs(col._docker_cache[1],previous)
        with patch.object(legacy.time,'time',return_value=100),patch.object(legacy,'_http_unix',return_value=(200,b'[]')):
            self.assertEqual(col._docker(),[]);self.assertFalse(col._docker_failed)
        col.snapshot={'ready':True,'docker':previous};col._docker_cache=(0,previous)
        for name,value in [('_cpu',{}),('_mem',{}),('_net',{}),('_volumes',[]),('_raid',[]),('_disk_io',[]),('_temps',[]),('_zfs',{})]:
            active=patch.object(col,name,return_value=value);active.start();self.addCleanup(active.stop)
        with patch.object(legacy.time,'time',return_value=200),patch.object(legacy,'_http_unix',side_effect=ValueError):
            snap=col.sample();self.assertEqual(snap['docker'],previous);self.assertIn('docker',snap['errors'])

    def test_legacy_http_actual_anonymous_unix_socket(self):
        import socket,tempfile
        legacy=self.legacy_module()
        for chunked in (False,True):
            with self.subTest(chunked=chunked),tempfile.TemporaryDirectory(prefix='legacy-http-') as root:
                address=str(Path(root)/'fixture.sock');errors=[]
                with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as listener:
                    listener.settimeout(2);listener.bind(address);listener.listen(1)
                    def respond():
                        try:
                            connection,_=listener.accept()
                            with connection:
                                connection.settimeout(2);request=b''
                                while not request.endswith(bytes([13,10,13,10])) and len(request)<8192:
                                    piece=connection.recv(1024)
                                    if not piece:break
                                    request+=piece
                                self.assertIn(b'GET /fixture HTTP/1.0',request)
                                crlf=bytes([13,10])
                                lines=[b'HTTP/1.0 200 OK',b'Transfer-Encoding: chunked' if chunked else b'Content-Length: 2',b'',b'']
                                raw=crlf.join(lines)+(crlf.join([b'2',b'{}',b'']) if chunked else b'{}')
                                connection.sendall(raw)
                        except Exception as exc:errors.append(type(exc).__name__)
                    worker=threading.Thread(target=respond,daemon=True);worker.start()
                    try:
                        if chunked:
                            with self.assertRaises(ValueError):legacy._http_unix(address,'/fixture',timeout=1)
                        else:self.assertEqual(legacy._http_unix(address,'/fixture',timeout=1),(200,b'{}'))
                    finally:worker.join(timeout=3)
                    self.assertFalse(worker.is_alive());self.assertEqual(errors,[])

    def test_invalid_late_field_does_not_partially_update(self):
        before = copy.deepcopy(self.cfg)
        with self.assertRaises(m.ConfigError):
            self.handler()._apply_config({'interval': 2.0, 'bind': 'invalid'})
        self.assertEqual(self.cfg, before)
        self.assertEqual(self.state.collector.interval, before['interval'])
        m.save_config.assert_not_called()

    def test_invalid_tls_values_never_disable_tls(self):
        for value in ('typo', None, [], {}, float('nan')):
            with self.assertRaises(m.ConfigError):
                self.handler()._apply_config({'tls_enabled': value})
            self.assertTrue(self.cfg['tls_enabled'])
        changed, restart = self.handler()._apply_config({'tls_enabled': 'false'})
        self.assertEqual(changed, ['tls_enabled'])
        self.assertEqual(restart, ['tls_enabled'])
        self.assertFalse(self.cfg['tls_enabled'])

    def test_runtime_config_rejects_values_startup_rejects(self):
        before = copy.deepcopy(self.cfg)
        for body in ({'interval': True}, {'interval': float('nan')},
                     {'port': 8798.9}, {'hist_len': 30.5}):
            with self.assertRaises(m.ConfigError):
                self.handler()._apply_config(body)
            self.assertEqual(self.cfg, before)
            m.save_config.assert_not_called()
        changed, restart = self.handler()._apply_config({'interval': '2.5'})
        self.assertEqual(changed, ['interval'])
        self.assertEqual(restart, [])
        self.assertEqual(self.cfg['interval'], 2.5)

    def test_persistence_failure_keeps_memory_and_pair_code(self):
        self.state.new_pair_code()
        code = self.state.pending_pair['code']
        before = copy.deepcopy(self.cfg)
        with patch.object(m, 'save_config', side_effect=OSError('anonymous fault')):
            for op in (lambda: self.handler()._apply_config({'interval': 2.0}),
                       lambda: self.state.revoke('last'),
                       lambda: self.state.try_pair(code, 'fixture', 'fixture')):
                with self.assertRaises(OSError):op()
                self.assertEqual(self.cfg, before)
            self.assertEqual(self.state.pending_pair['code'], code)
        token, error = self.state.try_pair(code, 'fixture', 'fixture')
        self.assertTrue(token);self.assertIsNone(error)
        self.assertIsNone(self.state.pending_pair)

    def test_config_writer_blocks_revoke_until_persisted(self):
        saving, release, done = threading.Event(), threading.Event(), threading.Event()
        calls, errors = [], []
        def save(cfg):
            calls.append(copy.deepcopy(cfg))
            if len(calls) == 1:
                saving.set()
                if not release.wait(2):raise AssertionError('fixture timeout')
        def writer():
            try:self.handler()._apply_config({'interval': 2.0})
            except Exception as error:errors.append(error)
        def revoke():
            try:self.state.revoke('last')
            except Exception as error:errors.append(error)
            finally:done.set()
        with patch.object(m, 'save_config', side_effect=save):
            a = threading.Thread(target=writer);b = threading.Thread(target=revoke)
            a.start()
            try:
                self.assertTrue(saving.wait(1));b.start()
                self.assertFalse(done.wait(.05))
            finally:
                release.set();a.join(2)
                if b.ident is not None:b.join(2)
        self.assertFalse(errors)
        self.assertEqual(self.cfg['interval'], 2.0)
        self.assertEqual(self.cfg['devices'], [])
        self.assertEqual(calls[-1]['devices'], [])
        self.assertEqual(calls[-1]['interval'], 2.0)

    def test_docker_bad_number_or_persist_failure_keeps_state(self):
        h = self.handler()
        for value in (float('nan'), float('inf'), 2, -1):
            with patch.object(h, '_require_admin', return_value={}), \
                 patch.object(h, '_read_json', return_value={'enabled': value}), \
                 patch.object(h, '_err') as error:
                h._route_post('/api/collector/docker', {})
                self.assertEqual(error.call_args.args[0], 400)
                self.assertFalse(self.cfg['docker_enabled'])
        with patch.object(h, '_require_admin', return_value={}), \
             patch.object(h, '_read_json', return_value={'enabled': True}), \
             patch.object(m, 'save_config', side_effect=OSError('anonymous fault')):
            with self.assertRaises(OSError):h._route_post('/api/collector/docker', {})
        self.assertFalse(self.cfg['docker_enabled'])
        self.assertFalse(self.state.collector.docker_enabled)

    def test_http_framing_rejects_ambiguous_lengths_and_trailing_requests(self):
        import socket, io
        from email.message import Message
        h=self.handler()
        for headers in ([('Content-Length','+2')],[('Content-Length','1_0')],
                        [('Content-Length','-1')],[('Content-Length','²')],
                        [('Content-Length','2'),('Content-Length','2')],
                        [('Content-Length','2, 2')],[('Transfer-Encoding','chunked')],
                        [('Transfer-Encoding','identity'),('Content-Length','2')],
                        [('Content-Length','9'*1000)]):
            h.headers=Message()
            for key,value in headers:h.headers[key]=value
            h.rfile=io.BytesIO(b'{}')
            with self.assertRaises(m.BadRequest):h._read_json(allow_empty=True)
        h.headers=Message();h.headers['Content-Length']='0002';h.rfile=io.BytesIO(b'{}')
        self.assertEqual(h._read_json(),{})
        srv=m.ThreadedHTTPServer(('127.0.0.1',0),m.Handler)
        srv.state=self.state;srv.ssl_context=None
        worker=threading.Thread(target=srv.serve_forever,kwargs={'poll_interval':.01});worker.start()
        crlf=bytes([13,10])
        health=crlf.join([b'GET /api/v1/health HTTP/1.1',b'Host: localhost',b'Connection: close',b'',b''])
        def request(method,path,headers,body=b''):
            return crlf.join([method+b' '+path+b' HTTP/1.1',b'Host: localhost']+headers+[b'',b''])+body
        def exchange(payload):
            with socket.create_connection(srv.server_address,timeout=2) as client:
                client.sendall(payload);client.shutdown(socket.SHUT_WR)
                response=b''
                while True:
                    chunk=client.recv(65536)
                    if not chunk:break
                    response+=chunk
                return response
        try:
            cases=[([b'Content-Length: 65537'],b'',400),
                   ([b'Content-Length: +2'],b'{}',400),
                   ([b'Content-Length: 2',b'Content-Length: 3'],b'{}',400),
                   ([b'Transfer-Encoding: chunked'],b'0'+crlf+crlf,400),
                   ([b'Content-Length: 2'],b'[]',400),
                   ([b'Content-Length: 2'],b'{!',400),
                   ([b'Content-Length: 2'],b'{}',403)]
            for headers,body,status in cases:
                response=exchange(request(b'POST',b'/api/v1/pair',headers,body)+health)
                self.assertEqual(response.count(b'HTTP/1.1 '),1)
                self.assertIn(b'HTTP/1.1 '+str(status).encode(),response)
                self.assertIn(b'Connection: close',response)
            for path,status in [(b'/missing',404),(b'/api/config',403)]:
                response=exchange(request(b'POST',path,[b'Content-Length: 2'],b'{}')+health)
                self.assertEqual(response.count(b'HTTP/1.1 '),1);self.assertIn(str(status).encode(),response.split(crlf)[0])
            self.assertIn(b'HTTP/1.1 400',exchange(request(b'POST',b'/api/v1/pair',[b'Content-Length: 10'],b'{}')))
            for headers in ([b'Content-Length: 1'],[b'Transfer-Encoding: chunked'],[b'Content-Length: 0',b'Content-Length: 0']):
                response=exchange(request(b'GET',b'/api/v1/health',headers)+health)
                self.assertEqual(response.count(b'HTTP/1.1 '),1);self.assertIn(b'HTTP/1.1 400',response)
            # Ordinary telemetry still supports two requests on one connection.
            response=exchange(request(b'GET',b'/api/v1/health',[])+health)
            self.assertEqual(response.count(b'HTTP/1.1 200'),2)
            source=textwrap.dedent(inspect.getsource(m.Handler.do_POST))
            start=source.index('    # Auth/unknown/')
            end=source.index('    path, q = self._normalize()')
            source=source[:start]+source[end:]
            namespace=dict(m.__dict__);exec(source,namespace)
            with patch.object(m.Handler,'do_POST',namespace['do_POST']):
                response=exchange(request(b'POST',b'/api/v1/pair',[b'Content-Length: 65537'])+health)
                with self.assertRaises(AssertionError):self.assertEqual(response.count(b'HTTP/1.1 '),1)
                self.assertIn(b'HTTP/1.1 400',response);self.assertIn(b'HTTP/1.1 200',response)
        finally:
            srv.shutdown();srv.server_close();worker.join(2)
            self.assertFalse(worker.is_alive())

    def test_optional_body_does_not_accept_negative_length(self):
        h = self.handler()
        from email.message import Message
        h.headers = Message();h.headers['Content-Length'] = '-1'
        for optional in (True, False):
            with self.assertRaises(m.BadRequest):h._read_json(allow_empty=optional)

    def test_management_route_consumes_locked_snapshot(self):
        h = self.handler()
        with patch.object(h, '_require_admin', return_value='fixture'), \
             patch.object(h, '_send') as send:
            h._route_get('/api/state', {})
        code, payload = send.call_args.args
        self.assertEqual(code, 200)
        self.assertEqual(payload['config']['port'], self.cfg['port'])
        self.assertEqual(payload['devices'][0]['name'], 'test')
        self.assertIsNone(payload['pairing'])
        self.assertNotIn('sha256', payload['devices'][0])

    def test_management_snapshot_is_detached_and_waits_for_lock(self):
        self.state.new_pair_code()
        self.cfg['devices'][0]['_dirty'] = True
        cfg, devices, pending = self.state.management_snapshot()
        self.assertNotIn('devices', cfg)
        self.assertNotIn('sha256', devices[0]);self.assertNotIn('_dirty', devices[0])
        self.state.cancel_pair_code();self.state.note_seen(self.cfg['devices'][0], 'new')
        self.assertIsNotNone(pending);self.assertNotIn('last_ip', devices[0])
        entered, done = threading.Event(), threading.Event()
        def read():entered.set();self.state.management_snapshot();done.set()
        self.state._lock.acquire();thread = threading.Thread(target=read)
        try:
            thread.start();self.assertTrue(entered.wait(1));self.assertFalse(done.wait(.05))
        finally:self.state._lock.release();thread.join(2)
        self.assertTrue(done.is_set())

    def test_flush_skips_write_when_no_device_is_dirty(self):
        m.save_config.reset_mock()
        self.assertTrue(self.state.flush())
        m.save_config.assert_not_called()
        self.cfg['devices'][0]['_dirty'] = True
        self.assertTrue(self.state.flush())
        m.save_config.assert_called_once()

    def test_flush_failure_retains_dirty_for_retry(self):
        device = self.cfg['devices'][0];device['_dirty'] = True
        with patch.object(m, 'save_config', side_effect=OSError('anonymous fault')):
            self.assertFalse(self.state.flush())
        self.assertTrue(device['_dirty'])
        self.assertTrue(self.state.flush());self.assertNotIn('_dirty', device)

    def test_run_cleanup_errors_return_failure_including_signal(self):
        from unittest.mock import Mock
        for fault in ('flush', 'socket', 'listener', 'collector', 'signal-flush', 'drain'):
            state = self.state
            state.collector.snapshot['ready'] = True;state.stop_requested.clear()
            srv = Mock(is_unix=False)
            if fault == 'listener':srv.shutdown.side_effect = OSError('fixture')
            if fault == 'drain':srv.server_close.side_effect = TimeoutError('fixture')
            servers = [srv] + ([Mock(is_unix=True)] if fault == 'socket' else [])
            if fault == 'socket':servers[-1].server_close.side_effect = PermissionError('fixture')
            with patch.object(m, 'State', return_value=state), \
                 patch.object(m, 'make_servers', return_value=(servers, [])), \
                 patch.object(m.signal, 'signal'), \
                 patch.object(state.collector, 'start'), \
                 patch.object(state.collector, 'stop', side_effect=OSError('fixture') if fault == 'collector' else None), \
                 patch.object(state, 'flush', return_value=fault not in ('flush', 'signal-flush')) as flush, \
                 patch.object(m.os, 'unlink', side_effect=PermissionError('fixture') if fault == 'socket' else None), \
                 patch.object(state.stop_requested, 'wait', side_effect=SystemExit(0) if fault == 'signal-flush' else None, return_value=True):
                self.assertEqual(m.run(self.cfg), 1, fault)
                if fault in ('drain','collector'):flush.assert_not_called()

    def test_startup_failures_and_signal_close_only_started_listeners(self):
        from unittest.mock import Mock
        for fault in ('sampler-start', 'second-thread', 'early-signal', 'no-tcp'):
            state=self.state;state.stop_requested.clear()
            state.collector.snapshot['ready'] = fault != 'early-signal'
            servers=[Mock(is_unix=False),Mock(is_unix=True)]
            if fault=='no-tcp':servers=servers[1:]
            handlers=[];created=[]
            def signal(sig, handler):handlers.append(handler)
            def wait(timeout):
                if fault=='early-signal':handlers[0](m.signal.SIGTERM,None)
                return True
            def make_thread(**kwargs):
                t=Mock()
                if fault=='second-thread' and created:t.start.side_effect=RuntimeError('fixture')
                created.append(t);return t
            with patch.object(m,'State',return_value=state), \
                 patch.object(m,'make_servers',return_value=(servers,[])), \
                 patch.object(m.signal,'signal',side_effect=signal), \
                 patch.object(m.threading,'Thread',side_effect=make_thread), \
                 patch.object(state.stop_requested,'wait',side_effect=wait), \
                 patch.object(state.collector,'start',side_effect=RuntimeError('fixture') if fault=='sampler-start' else None), \
                 patch.object(state.collector,'stop') as stop, \
                 patch.object(state,'flush',return_value=True), \
                 patch.object(m.os,'unlink') as unlink:
                rc=m.run(self.cfg)
                self.assertEqual(rc,3 if fault=='no-tcp' else 0 if fault=='early-signal' else 1,fault)
                stop.assert_called_once();unlink.assert_not_called()
                for srv in servers:srv.server_close.assert_called_once()
                if fault=='second-thread':
                    servers[0].shutdown.assert_called_once();servers[1].shutdown.assert_not_called()
                else:
                    for srv in servers:srv.shutdown.assert_not_called()
                self.assertEqual(len(handlers),2) # original handler restored

    def test_make_servers_unexpected_failure_unwinds_owned_unix_listener(self):
        import tempfile
        real=m.UnixHTTPServer
        for fault in ('prepare','tcp'):
            with tempfile.TemporaryDirectory() as tmp:
                sockpath=str(Path(tmp)/'bound.sock');owned=[]
                cfg=copy.deepcopy(self.cfg);cfg['tls_enabled']=fault=='prepare'
                def bind(*args,**kwargs):
                    srv=real(*args,**kwargs);owned.append(srv);return srv
                with patch.object(m,'socket_path',return_value=sockpath), \
                     patch.object(m,'UnixHTTPServer',side_effect=bind), \
                     patch.object(m,'prepare_tls',side_effect=RuntimeError('fixture')), \
                     patch.object(m,'ThreadedHTTPServer',side_effect=RuntimeError('fixture')):
                    with self.assertRaises(RuntimeError):m.make_servers(self.state,cfg)
                self.assertEqual(len(owned),1);self.assertEqual(owned[0].socket.fileno(),-1)
                self.assertFalse(Path(sockpath).exists())

    def test_shared_drain_deadline_leaves_only_remaining_sampler_budget(self):
        from unittest.mock import Mock
        import re
        text=(Path(__file__).parent/'nasscreencompanion/cmd/main').read_text()
        stop=text.split('stop_process() {',1)[1].split('# ---------------------------------------------------------------- 入口',1)[0]
        count=int(re.search(r'for i in \$\(seq 1 (\d+)\)',stop).group(1))
        period=float(re.search(r'sleep (0\.\d+)',stop).group(1))
        self.assertLess(m.SHUTDOWN_DRAIN_BUDGET_S,count*period)
        state=self.state;state.stop_requested.clear();state.collector.snapshot['ready']=True
        clock=[0.0];servers=[]
        for _ in range(2):
            srv=Mock(is_unix=False)
            srv.server_close.side_effect=lambda:clock.__setitem__(0,clock[0]+3)
            servers.append(srv)
        with patch.object(m,'State',return_value=state), \
             patch.object(m,'make_servers',return_value=(servers,[])), \
             patch.object(m.signal,'signal'),patch.object(state.stop_requested,'wait',return_value=True), \
             patch.object(m.time,'monotonic',side_effect=lambda:clock[0]), \
             patch.object(state.collector,'start'),patch.object(state.collector,'stop') as stop_sampler, \
             patch.object(state,'flush',return_value=True):
            self.assertEqual(m.run(self.cfg),0)
        self.assertEqual(servers[0].shutdown_deadline,servers[1].shutdown_deadline)
        stop_sampler.assert_called_once_with(timeout=m.SHUTDOWN_DRAIN_BUDGET_S-6)

    def test_actual_sigterm_during_first_frame_wait_cleans_bound_sockets(self):
        import os, select, signal, socket, subprocess, sys, tempfile
        with tempfile.TemporaryDirectory() as tmp:
            sockpath=str(Path(tmp)/'startup.sock')
            script=r"""
import importlib.util,sys
from unittest.mock import patch
sp=importlib.util.spec_from_file_location('startup_server',sys.argv[1]);m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
cfg=dict(m.DEFAULT_CONFIG);st=m.State(cfg)
class Sampler:
    lock=m.threading.Lock()
    snapshot={'ready':False}
    def start(self):print('WAITING',flush=True)
    def stop(self, timeout=10):print('STOPPED',flush=True)
st.collector=Sampler()
servers=[m.ThreadedHTTPServer(('127.0.0.1',0),m.Handler),m.UnixHTTPServer(sys.argv[2],m.Handler)]
with patch.object(m,'State',return_value=st),patch.object(m,'make_servers',return_value=(servers,[])),patch.object(m,'socket_path',return_value=sys.argv[2]),patch.object(st,'flush',return_value=True),patch.object(m,'log'):
    rc=m.run(cfg)
    assert all(s.socket.fileno()==-1 for s in servers)
    print('EXIT',rc,flush=True)
    sys.exit(rc)
"""
            proc=subprocess.Popen([sys.executable,'-c',script,str(path.resolve()),sockpath],
                                  stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                self.assertTrue(select.select([proc.stdout],[],[],3)[0])
                self.assertEqual(proc.stdout.readline().strip(),'WAITING')
                proc.send_signal(signal.SIGTERM)
                out,err=proc.communicate(timeout=4)
                self.assertEqual(proc.returncode,0,err);self.assertIn('STOPPED',out);self.assertIn('EXIT 0',out)
                self.assertFalse(Path(sockpath).exists())
            finally:
                if proc.poll() is None:proc.kill()
                proc.communicate(timeout=3)

    def test_collector_stop_joins_and_reports_unfinished_sample(self):
        import time
        coll=m.CompanionCollector(interval=1,hist_len=2,docker_enabled=False)
        entered,release=threading.Event(),threading.Event()
        def sample():
            entered.set();release.wait(3)
            return {'ts':1,'cpu':{'pct':0},'mem':{'pct':0},'net':{'rx_kbs':0,'tx_kbs':0}}
        with patch.object(coll,'sample',side_effect=sample):
            thread=coll.start()
            try:
                self.assertTrue(entered.wait(2));self.assertIs(coll.start(),thread)
                with self.assertRaises(TimeoutError):coll.stop(timeout=.02)
                with self.assertRaises(RuntimeError):coll.start()
                self.assertTrue(thread.is_alive())
                release.set();coll.stop(timeout=2)
                self.assertFalse(thread.is_alive());self.assertIsNone(coll._thread)
                restarted=coll.start();self.assertIsNot(restarted,thread);coll.stop(timeout=2)
            finally:release.set();coll.stop(timeout=2)
        with patch.object(m.threading.Thread,'start',side_effect=RuntimeError('fixture')):
            with self.assertRaises(RuntimeError):coll.start()
        self.assertIsNone(coll._thread);coll.stop()

    def test_second_unix_listener_preserves_existing_socket_and_regular_file(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = str(Path(tmp)/'fixture.sock')
            with m.UnixHTTPServer(path, m.Handler):
                inode = Path(path).stat().st_ino
                with self.assertRaises(OSError):m.UnixHTTPServer(path, m.Handler)
                self.assertEqual(Path(path).stat().st_ino, inode)
            Path(path).write_text('anonymous fixture')
            with self.assertRaises(OSError):m.UnixHTTPServer(path, m.Handler)
            self.assertEqual(Path(path).read_text(), 'anonymous fixture')

    def test_unix_cleanup_retains_replaced_file_socket_and_symlink(self):
        import tempfile, socket, os
        for kind in ('file', 'socket', 'symlink'):
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp)/'fixture.sock'
                srv = m.UnixHTTPServer(str(path), m.Handler)
                path.unlink()
                replacement = None
                try:
                    if kind == 'file':path.write_text('anonymous fixture')
                    elif kind == 'socket':
                        replacement = socket.socket(socket.AF_UNIX)
                        replacement.bind(str(path))
                    else:
                        target = Path(tmp)/'target';target.write_text('anonymous fixture')
                        path.symlink_to(target)
                    identity = os.lstat(path)
                    srv.server_close();srv.server_close()
                    self.assertTrue(os.path.lexists(path))
                    after = os.lstat(path)
                    self.assertEqual((after.st_dev, after.st_ino), (identity.st_dev, identity.st_ino))
                    if kind == 'file':self.assertEqual(path.read_text(), 'anonymous fixture')
                    if kind == 'symlink':self.assertTrue(path.is_symlink())
                finally:
                    srv.server_close()
                    if replacement is not None:replacement.close()

    def test_unix_cleanup_failure_is_retryable_and_activation_failure_unwinds(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = str(Path(tmp)/'fixture.sock')
            srv = m.UnixHTTPServer(path, m.Handler)
            with patch.object(m.os, 'unlink', side_effect=PermissionError('fixture')):
                with self.assertRaises(PermissionError):srv.server_close()
            self.assertTrue(Path(path).exists())
            srv.server_close();self.assertFalse(Path(path).exists())
            with patch.object(m.socketserver.TCPServer, 'server_activate', side_effect=OSError('fixture')):
                with self.assertRaises(OSError):m.UnixHTTPServer(path, m.Handler)
            self.assertFalse(Path(path).exists())

    def test_unix_cleanup_identity_guard_negative_control(self):
        import tempfile
        source = textwrap.dedent(inspect.getsource(m.UnixHTTPServer._cleanup_unix_path))
        old = 'if not stat.S_ISSOCK(info.st_mode) or (info.st_dev, info.st_ino) != identity:'
        self.assertIn(old, source)
        namespace = dict(m.__dict__)
        exec(source.replace(old, 'if False:'), namespace)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'fixture.sock'
            srv = m.UnixHTTPServer(str(path), m.Handler)
            path.unlink();path.write_text('anonymous fixture')
            try:
                with patch.object(m.UnixHTTPServer, '_cleanup_unix_path', namespace['_cleanup_unix_path']):
                    srv.server_close()
                with self.assertRaises(AssertionError):
                    self.assertTrue(path.exists(), 'replacement was removed')
            finally:srv.server_close()

    def test_unix_permission_failure_never_listens(self):
        import tempfile, socket
        with tempfile.TemporaryDirectory() as tmp:
            path = str(Path(tmp)/'fixture.sock')
            with patch.object(m.os, 'chmod', side_effect=PermissionError('fixture')), \
                 patch.object(m.socketserver.TCPServer, 'server_activate') as activate:
                with self.assertRaises(PermissionError):m.UnixHTTPServer(path, m.Handler)
                activate.assert_not_called()
            self.assertFalse(Path(path).exists())
            with m.UnixHTTPServer(path, m.Handler) as srv:
                self.assertEqual(Path(path).stat().st_mode & 0o777, 0o600)

    def test_slot_is_reserved_before_thread_creation_and_restored_on_failure(self):
        from unittest.mock import Mock
        srv = object.__new__(m.ThreadedHTTPServer)
        srv._requests_done = threading.Condition();srv._requests = {};srv._closing = False
        srv._probe_slots = threading.BoundedSemaphore(1)
        srv._probe_rejected = 0;srv._probe_last_log = 0
        srv.probe_limit = 1;srv.shutdown_request = Mock()
        request = Mock()
        srv._probe_slots.acquire()
        with patch.object(m.socketserver.ThreadingMixIn, 'process_request') as create:
            srv.process_request(request, ('fixture', 0));create.assert_not_called()
            srv.shutdown_request.assert_called_once_with(request)
        srv._probe_slots.release()
        with patch.object(m.socketserver.ThreadingMixIn, 'process_request', side_effect=RuntimeError('fixture')):
            with self.assertRaises(RuntimeError):srv.process_request(request, ('fixture', 0))
        self.assertTrue(srv._probe_slots.acquire(blocking=False));srv._probe_slots.release()

    def test_close_interrupts_partial_request_and_rejects_new_admission(self):
        import socket, time
        from unittest.mock import Mock
        class Partial(m.BaseHTTPRequestHandler):
            def do_GET(self):pass
            def log_message(self, *args):pass
        srv = m.ThreadedHTTPServer(('127.0.0.1', 0), Partial)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .01})
        thread.start()
        client = socket.create_connection(srv.server_address, timeout=2)
        try:
            client.sendall(b'GET /api/v1/identity HTTP/1.1\r\nHost:')
            deadline = time.monotonic()+2
            while not srv._requests and time.monotonic()<deadline:time.sleep(.01)
            self.assertTrue(srv._requests)
            srv.shutdown();srv.server_close();thread.join(2)
            self.assertFalse(srv._requests)
            rejected = Mock()
            with patch.object(srv, 'shutdown_request') as close, \
                 patch.object(m.socketserver.ThreadingMixIn, 'process_request') as create:
                srv.process_request(rejected, ('fixture',0))
                close.assert_called_once_with(rejected);create.assert_not_called()
        finally:
            client.close();srv.shutdown();srv.server_close();thread.join(2)

    def test_close_interrupts_registered_tls_handshake(self):
        import socket, ssl, subprocess, tempfile, time
        with tempfile.TemporaryDirectory() as tmp:
            cert, key = str(Path(tmp)/'cert.pem'), str(Path(tmp)/'key.pem')
            subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes',
                            '-keyout',key,'-out',cert,'-days','1','-subj','/CN=localhost'],
                           check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15)
            ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(cert,key)
            srv=m.ThreadedHTTPServer(('127.0.0.1',0),m.Handler);srv.ssl_context=ctx
            serve=threading.Thread(target=srv.serve_forever,kwargs={'poll_interval':.01})
            serve.start();client=socket.create_connection(srv.server_address,timeout=2)
            try:
                client.sendall(b'\x16\x03\x01\x00')
                deadline=time.monotonic()+2
                while time.monotonic()<deadline:
                    with srv._requests_done:
                        if any(isinstance(v,ssl.SSLSocket) for v in srv._requests.values()):break
                    time.sleep(.01)
                with srv._requests_done:
                    self.assertTrue(any(isinstance(v,ssl.SSLSocket) for v in srv._requests.values()))
                srv.shutdown();started=time.monotonic();srv.server_close()
                self.assertLess(time.monotonic()-started,2)
                self.assertFalse(srv._requests)
            finally:
                client.close();srv.shutdown();srv.server_close();serve.join(2)

    def test_close_waits_for_writer_and_timeout_is_explicit(self):
        import socket, time
        entered, release, wrote = threading.Event(), threading.Event(), threading.Event()
        class Writer(m.BaseHTTPRequestHandler):
            def do_GET(self):
                entered.set();release.wait(3);wrote.set()
            def log_message(self, *args):pass
        for timeout in (False, True):
            entered.clear();release.clear();wrote.clear()
            srv = m.ThreadedHTTPServer(('127.0.0.1',0), Writer)
            if timeout:srv.drain_timeout_s=.05
            serve = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval':.01})
            serve.start();client = socket.create_connection(srv.server_address,timeout=2)
            done=threading.Event();errors=[]
            def close():
                try:srv.server_close()
                except Exception as exc:errors.append(exc)
                finally:done.set()
            closer=None
            try:
                client.sendall(b'GET / HTTP/1.0\r\n\r\n');self.assertTrue(entered.wait(2))
                srv.shutdown();closer=threading.Thread(target=close);closer.start()
                if timeout:
                    self.assertTrue(done.wait(1));self.assertIsInstance(errors[0],TimeoutError)
                    self.assertFalse(wrote.is_set())
                else:
                    self.assertFalse(done.wait(.05));release.set()
                    self.assertTrue(done.wait(2));self.assertTrue(wrote.is_set());self.assertFalse(errors)
            finally:
                release.set();client.close();serve.join(2)
                if closer:closer.join(2)
                srv.drain_timeout_s=2;srv.server_close()

    def test_plain_http_partial_request_uses_slot_and_idle_timeout(self):
        import socket, time
        from contextlib import ExitStack
        class FixtureHandler(m.BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200);self.send_header('Content-Length', '2')
                self.end_headers();self.wfile.write(b'ok')
            def log_message(self, *args):pass
        class Bounded(m.ThreadedHTTPServer):probe_limit = 2
        with Bounded(('127.0.0.1', 0), FixtureHandler) as srv:
            thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .05})
            thread.start()
            try:
                with ExitStack() as stack:
                    clients = [stack.enter_context(socket.create_connection(srv.server_address, timeout=2)) for _ in range(2)]
                    for client in clients:client.sendall(b'GET / HTTP/1.1\r\nHost: fixture\r\n')
                    deadline = time.monotonic()+2
                    while srv._probe_slots._value and time.monotonic()<deadline:time.sleep(.01)
                    self.assertEqual(srv._probe_slots._value, 0)
                    with socket.create_connection(srv.server_address, timeout=2) as extra:
                        self.assertEqual(extra.recv(1), b'')
                    # 生产15秒超时，不缩短常量后冒充真实超时证明。
                    for client in clients:
                        client.settimeout(m.HTTP_IDLE_TIMEOUT_S+3)
                        self.assertEqual(client.recv(1), b'')
                    deadline = time.monotonic()+2
                    while srv._probe_slots._value != 2 and time.monotonic()<deadline:time.sleep(.01)
                    self.assertEqual(srv._probe_slots._value, 2)
                    with socket.create_connection(srv.server_address, timeout=2) as healthy:
                        healthy.sendall(b'GET / HTTP/1.0\r\n\r\n')
                        self.assertIn(b'200', healthy.recv(1024))
            finally:srv.shutdown();thread.join(2)

    def test_tls_failure_never_opens_plain_listener(self):
        for info in ({'configured': True, 'enabled': False, 'error': 'test'},
                     {'configured': True, 'enabled': True, 'cert': 'bad', 'key': 'bad'}):
            with patch.object(m, 'prepare_tls', return_value=info), \
                 patch.object(m, 'load_ssl_context', return_value=(None, 'test')), \
                 patch.object(m, 'ThreadedHTTPServer') as listener:
                servers, problems = m.make_servers(self.state, self.cfg, bind_socket=False)
                self.assertEqual(servers, [])
                self.assertTrue(problems)
                listener.assert_not_called()

    def test_explicit_plain_configuration_still_works(self):
        self.cfg['tls_enabled'] = False
        with patch.object(m, 'ThreadedHTTPServer') as listener:
            servers, problems = m.make_servers(self.state, self.cfg, bind_socket=False)
            self.assertEqual(servers, [listener.return_value])
            self.assertEqual(problems, [])

    def test_revoked_record_cannot_be_updated_or_authenticated(self):
        ok, device = self.state.check_token('test-only-token')
        self.assertTrue(ok)
        self.assertEqual(self.state.revoke('last'), 1)
        self.state.note_seen(device, 'test-address')
        self.assertNotIn('last_ip', device)
        self.assertEqual(self.state.check_token('test-only-token'), (False, None))

    def test_device_operations_wait_for_shared_lock(self):
        for op in (lambda: self.state.check_token('test-only-token'),
                   lambda: self.state.note_seen(self.cfg['devices'][0], 'test'),
                   self.state.flush, lambda: self.state.revoke('missing')):
            entered, done = threading.Event(), threading.Event()
            def worker():
                entered.set()
                op()
                done.set()
            self.state._lock.acquire()
            t = threading.Thread(target=worker)
            try:
                t.start()
                self.assertTrue(entered.wait(1))
                self.assertFalse(done.wait(.05))
            finally:
                self.state._lock.release()
                t.join(2)
            self.assertTrue(done.is_set())

    def test_run_stops_both_listeners_and_collector(self):
        state = self.state
        class Listener:
            is_unix = False
            def __init__(self):
                self.closed = False
                self.stopped = threading.Event()
            def serve_forever(self, **kwargs):
                self.stopped.wait(2)
            def shutdown(self):
                self.stopped.set()
            def server_close(self):
                self.closed = True
        sequence = []
        servers = [Listener(), Listener()]
        for n, listener in enumerate(servers):
            old_stop, old_close = listener.shutdown, listener.server_close
            def stop(n=n, old=old_stop):sequence.append('stop'+str(n));old()
            def close(n=n, old=old_close):sequence.append('close'+str(n));old()
            listener.shutdown, listener.server_close = stop, close
        state.stop_requested.clear()
        with patch.object(state.stop_requested, 'wait', return_value=True), \
             patch.object(m, 'State', return_value=state), \
             patch.object(m, 'make_servers', return_value=(servers, [])), \
             patch.object(m.signal, 'signal'), patch.object(m.os, 'unlink') as unlink, \
             patch.object(state, 'flush', side_effect=lambda:sequence.append('flush') or True), \
             patch.object(state.collector, 'start'), patch.object(state.collector, 'stop') as stop:
            state.collector.snapshot['ready'] = True
            self.assertEqual(m.run(self.cfg), 0)
            stop.assert_called_once();unlink.assert_not_called()
            self.assertEqual(sequence,['stop0','stop1','close0','close1','flush'])
        self.assertTrue(all(s.closed and s.stopped.is_set() for s in servers))

    def test_access_log_never_contains_query_token(self):
        h = object.__new__(m.Handler)
        h.path = '/api/v1/identity?token=secret-test-value'
        h.command = 'GET'
        h.client_address = ('127.0.0.1', 1)
        with patch.object(m, 'log') as log:
            h.log_message('"%s" %s %s', h.path, '200', '-')
            self.assertNotIn('secret-test-value', str(log.call_args))
            self.assertNotIn('?token', str(log.call_args))
        h.path = '/api/v1/status'
        with patch.object(m, 'log') as log:
            h.log_message('"%s" %s %s', h.path, '200', '-')
            log.assert_not_called()

    def test_shutdown_requests_process_stop(self):
        h = object.__new__(m.Handler)
        h.server = type('Server', (), {'state': self.state})()
        h.path = '/api/shutdown'
        h.headers = {}
        with patch.object(h, '_require_admin', return_value={'uid': 'test'}), \
             patch.object(h, '_read_json', return_value={}), patch.object(h, '_send'):
            h._route_post('/api/shutdown', {})
        self.assertTrue(self.state.stop_requested.is_set())



class ConfigWriteSafety(unittest.TestCase):
    def test_private_candidates_concurrent_and_failure(self):
        import tempfile, json, os, stat
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/'config.json';victim=Path(tmp)/'anonymous-sentinel'
            victim.write_text('preserve');legacy=Path(str(target)+'.tmp');legacy.symlink_to(victim)
            with patch.object(m,'cfg_path',return_value=str(target)):
                initial={'writer':'initial'};m.save_config(initial)
                self.assertEqual(victim.read_text(),'preserve')
                self.assertTrue(legacy.is_symlink())
                self.assertEqual(stat.S_IMODE(target.stat().st_mode),0o600)
                barrier=threading.Barrier(2);real_dump=m.json.dump;errors=[]
                def dump(value,stream,**kw):
                    real_dump(value,stream,**kw)
                def save(value):
                    try:
                        barrier.wait(timeout=3)
                        m.save_config({'writer':value,'payload':value*10000,'_revision':initial['_revision']})
                    except Exception as exc:errors.append(exc)
                with patch.object(m.json,'dump',side_effect=dump):
                    threads=[threading.Thread(target=save,args=(v,)) for v in ('a','b')]
                    for t in threads:t.start()
                    for t in threads:t.join(timeout=5);self.assertFalse(t.is_alive())
                self.assertEqual(len(errors),1)
                self.assertIsInstance(errors[0],m.ConfigError)
                final=json.loads(target.read_text());self.assertEqual(final['payload'],final['writer']*10000)
                self.assertFalse(list(Path(tmp).glob('.config-*.tmp')))
                before=target.read_bytes()
                def broken(value,stream,**kw):stream.write('{');raise OSError('anonymous injected write failure')
                for where in ('dump','fsync','replace'):
                    owner,attr=(m.json,'dump') if where=='dump' else (m.os,where)
                    with patch.object(owner,attr,side_effect=broken if where=='dump' else OSError('anonymous fault')):
                        with self.assertRaises(OSError):m.save_config({'writer':'failed','_revision':final['_revision']})
                    self.assertEqual(target.read_bytes(),before)
                    self.assertFalse(list(Path(tmp).glob('.config-*.tmp')))

    def test_callback_candidates_preserve_legacy_symlink(self):
        import tempfile, json, sys, contextlib, io
        cmd=path.parents[2]/'cmd'
        for name in ('_common.sh','upgrade_callback'):
            raw=(cmd/name).read_text()
            if name=='_common.sh':raw=raw[raw.index('apply_config() {'):]
            script=raw.split("<<'PYEOF'",1)[1].split('\nPYEOF',1)[0]
            with tempfile.TemporaryDirectory() as tmp:
                target=Path(tmp)/'config.json';target.write_text('{}')
                victim=Path(tmp)/'anonymous-sentinel';victim.write_text('preserve')
                Path(str(target)+'.tmp').symlink_to(victim)
                argv=['fixture',str(target),'8798','127.0.0.1','pairing','false','1','true']
                with patch.object(sys,'argv',argv),contextlib.redirect_stdout(io.StringIO()):
                    exec(compile(script,name,'exec'),{})
                self.assertEqual(victim.read_text(),'preserve')
                self.assertIsInstance(json.loads(target.read_text()),dict)
                self.assertFalse(list(Path(tmp).glob('.config-*.tmp')))


    def test_old_fixed_candidate_negative(self):
        import tempfile, inspect, textwrap
        source=textwrap.dedent(inspect.getsource(m.save_config))
        line='fd, tmp = tempfile.mkstemp(prefix=".config-", suffix=".tmp", dir=os.path.dirname(path))'
        self.assertIn(line,source)
        source=source.replace(line,'tmp = path + ".tmp"\n        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)')
        ns=dict(m.__dict__);exec(compile(source,'old-fixed-candidate','exec'),ns)
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/'config.json';victim=Path(tmp)/'anonymous-sentinel';victim.write_text('preserve')
            Path(str(target)+'.tmp').symlink_to(victim)
            ns['cfg_path']=lambda:str(target)
            ns['save_config']({'writer':'anonymous'})
            self.assertNotEqual(victim.read_text(),'preserve','old fixed candidate must corrupt sentinel')


    def test_revision_conflict_and_callback_interoperation(self):
        import tempfile,json,contextlib,io,sys
        with tempfile.TemporaryDirectory() as tmp,patch.object(m,'cfg_path',return_value=str(Path(tmp)/'config.json')):
            first=copy.deepcopy(m.DEFAULT_CONFIG);m.save_config(first)
            stale=dict(first);current=dict(first,port=12345);m.save_config(current)
            before=Path(m.cfg_path()).read_bytes()
            with self.assertRaises(m.ConfigError):m.save_config(stale)
            self.assertEqual(Path(m.cfg_path()).read_bytes(),before)
            self.assertEqual(stale['_revision'],first['_revision'])
            for invalid in (True, -1, '1'):
                Path(m.cfg_path()).write_text(json.dumps(dict(current,_revision=invalid)))
                with self.assertRaises(m.ConfigError):m.load_config()
            Path(m.cfg_path()).write_bytes(before)
            loaded=m.load_config();self.assertEqual(loaded['_revision'],current['_revision'])
            raw=(path.parents[2]/'cmd/_common.sh').read_text();raw=raw[raw.index('apply_config() {'):]
            script=raw.split("<<'PYEOF'",1)[1].split('\nPYEOF',1)[0]
            with patch.object(sys,'argv',['fixture',m.cfg_path(),'12346','127.0.0.1','pairing','false','1','true']),contextlib.redirect_stdout(io.StringIO()):
                exec(compile(script,'apply_config','exec'),{})
            after=json.loads(Path(m.cfg_path()).read_text());self.assertEqual(after['_revision'],current['_revision']+1)
            with self.assertRaises(m.ConfigError):m.save_config(loaded)
            self.assertEqual(json.loads(Path(m.cfg_path()).read_text())['port'],12346)
            lock=Path(m.cfg_path()+'.lock');lock.unlink();victim=Path(tmp)/'sentinel';victim.write_text('preserve');lock.symlink_to(victim)
            with self.assertRaises(OSError):m.save_config(after)
            self.assertEqual(victim.read_text(),'preserve')


    def test_process_revision_race_and_removed_cas_negative(self):
        import tempfile,subprocess,sys,json,time,inspect,textwrap,os
        with tempfile.TemporaryDirectory() as tmp,patch.object(m,'cfg_path',return_value=str(Path(tmp)/'config.json')):
            initial={'writer':'initial'};m.save_config(initial)
            code=r"""import importlib.util,sys,time
from pathlib import Path
spec=importlib.util.spec_from_file_location('fixture_server',sys.argv[1]);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
p=Path(sys.argv[2]);name=sys.argv[3];m.cfg_path=lambda:str(p/'config.json')
cfg={'writer':name,'_revision':int(sys.argv[4])};(p/('ready-'+name)).touch();deadline=time.monotonic()+5
while not (p/'go').exists():
 if time.monotonic()>deadline:raise RuntimeError('fixture synchronization timeout')
 time.sleep(.005)
try:m.save_config(cfg);print('SAVED')
except m.ConfigError:print('CONFLICT')
"""
            workers=[]
            try:
                for name in ('a','b'):
                    workers.append(subprocess.Popen([sys.executable,'-c',code,str(path),tmp,name,str(initial['_revision'])],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,TRIM_PKGVAR=tmp,TRIM_PKGETC=tmp,TRIM_APPDEST=tmp)))
                deadline=time.monotonic()+5
                while not all((Path(tmp)/('ready-'+n)).exists() for n in ('a','b')):
                    self.assertLess(time.monotonic(),deadline);time.sleep(.005)
                (Path(tmp)/'go').touch()
                results=[]
                for worker in workers:
                    out,err=worker.communicate(timeout=8);self.assertEqual(worker.returncode,0,err);results.append(out.strip())
                self.assertEqual(sorted(results),['CONFLICT','SAVED'])
                self.assertEqual(json.loads(Path(m.cfg_path()).read_text())['_revision'],initial['_revision']+1)
            finally:
                for worker in workers:
                    if worker.poll() is None:worker.kill()
                    worker.communicate(timeout=3)
            source=textwrap.dedent(inspect.getsource(m.save_config));self.assertIn('if revision != expected:',source)
            source=source.replace('if revision != expected:','if False:')
            ns=dict(m.__dict__);ns['cfg_path']=m.cfg_path;exec(compile(source,'removed-cas','exec'),ns)
            ns['save_config'](initial)
            self.assertEqual(json.loads(Path(m.cfg_path()).read_text())['writer'],'initial','removed CAS must overwrite newer writer')


class ExitReceiptSafety(unittest.TestCase):
    def test_entry_receipt_success_error_and_exception(self):
        import tempfile,json,os,stat
        with tempfile.TemporaryDirectory() as tmp,patch.object(m,'cfg_path',return_value=str(Path(tmp)/'config.json')):
            receipt=Path(tmp)/'service-exit.json'
            for code in (0,1,3):
                with patch.object(m,'main',return_value=code):self.assertEqual(m.main_with_exit_receipt(['--serve']),code)
                self.assertEqual(json.loads(receipt.read_text()),{'pid':os.getpid(),'code':code})
                self.assertEqual(stat.S_IMODE(receipt.stat().st_mode),0o600)
            with patch.object(m,'main',side_effect=RuntimeError('anonymous fault')):
                with self.assertRaises(RuntimeError):m.main_with_exit_receipt(['--serve'])
            self.assertEqual(json.loads(receipt.read_text())['code'],1)
            receipt.unlink()
            with patch.object(m,'main',return_value=0),patch.object(m.os,'fsync',side_effect=OSError('anonymous fault')):
                with self.assertRaises(OSError):m.main_with_exit_receipt(['--serve'])
            self.assertFalse(receipt.exists());self.assertFalse(list(Path(tmp).glob('.service-exit-*')))
            with patch.object(m,'main',return_value=0):self.assertEqual(m.main_with_exit_receipt(['--check-port','12345']),0)
            self.assertFalse(receipt.exists())

if __name__ == '__main__':
    unittest.main()
