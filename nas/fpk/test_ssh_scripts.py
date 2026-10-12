#!/usr/bin/env python3
"""NAS script command contracts with local ssh/scp/curl stubs; no network."""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
STUB=r'''#!/usr/bin/env python3
import json,os,subprocess,sys
from pathlib import Path
name=Path(sys.argv[0]).name;args=sys.argv[1:]
with open(os.environ['FIXTURE_LOG'],'a') as log:log.write(json.dumps([name,args])+'\n')
mode=os.environ.get('FIXTURE_MODE','')
if name=='sshpass':sys.exit(subprocess.call(args[1:]))
if name in ('ssh','scp'):
    assert 'StrictHostKeyChecking=yes' in args
    assert 'ControlMaster=no' in args and 'ControlPath=none' in args
    assert 'UserKnownHostsFile='+os.environ['FNOS_SSH_KNOWN_HOSTS'] in args
if name=='ssh':
    cmd=args[-1]
    if mode=='hostfail':sys.exit(255)
    if 'mktemp -d' in cmd:
        if mode=='badstage':print('/tmp/other;injected')
        else:print('/tmp/fnos-preview.Fixture123' if 'fnos-preview.' in cmd else '/tmp/fnos-install.Fixture123')
    elif 'rm -rf' in cmd:
        if mode=='cleanupfail':sys.exit(5)
    elif mode=='execfail' and (cmd.startswith('python3 ') or ('sudo ' in cmd and ' bash ' in cmd)):
        sys.exit(7)
if name=='scp' and mode=='scpfail':sys.exit(6)
if name=='curl':print('401',end='')
'''
class SSHScripts(unittest.TestCase):
    def run_script(self,script,mode='',action=None,password=False):
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp);bin_dir=directory/'bin';bin_dir.mkdir();log=directory/'commands.jsonl'
            for name in ('ssh','scp','sshpass','curl'):
                p=bin_dir/name;p.write_text(STUB);p.chmod(0o755)
            env={k:v for k,v in os.environ.items() if k not in ('SSHPASS','NAS_SUDO_PASS','FNAS_TOKEN')}
            env.update(PATH=str(bin_dir)+os.pathsep+env['PATH'],FIXTURE_LOG=str(log),FIXTURE_MODE=mode,NAS_HOST='fixture.invalid',NAS_USER='fixture',FNOS_SSH_KNOWN_HOSTS=str(directory/'known hosts'))
            if password:env['SSHPASS']='anonymous-fixture-password'
            args=['fixture@fixture.invalid'] if script=='preview-naming.sh' else ([action] if action else [])
            result=subprocess.run(['bash',str(ROOT/'nas'/script),*args],env=env,capture_output=True,text=True,timeout=10)
            commands=[json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
            return result,commands
    def test_verified_host_options_public_key_and_password(self):
        for script in ('install.sh','preview-naming.sh'):
            for password in (False,True):
                with self.subTest(script=script,password=password):
                    result,commands=self.run_script(script,password=password)
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertTrue(any(name=='scp' for name,_ in commands))
                    self.assertTrue(any(name=='ssh' and 'rm -rf' in args[-1] for name,args in commands))
                    self.assertEqual(any(name=='sshpass' for name,_ in commands),password)
    def test_host_verification_failure_stops_upload(self):
        for script in ('install.sh','preview-naming.sh'):
            result,commands=self.run_script(script,'hostfail')
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(any(name in ('scp','curl') for name,_ in commands))
    def test_bad_remote_stage_not_executed_or_removed(self):
        for script in ('install.sh','preview-naming.sh'):
            result,commands=self.run_script(script,'badstage')
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(len(commands),1)
    def test_upload_failure_cleans_own_directory(self):
        for script in ('install.sh','preview-naming.sh'):
            result,commands=self.run_script(script,'scpfail')
            self.assertEqual(result.returncode,6,result.stderr)
            self.assertIn('rm -rf',commands[-1][1][-1])
    def test_remote_execution_failure_cleans_own_directory(self):
        for script in ('install.sh','preview-naming.sh'):
            result,commands=self.run_script(script,'execfail')
            self.assertEqual(result.returncode,7,result.stderr)
            self.assertIn('rm -rf',commands[-1][1][-1])
    def test_cleanup_failure_nonzero(self):
        for script in ('install.sh','preview-naming.sh'):
            result,_=self.run_script(script,'cleanupfail')
            self.assertNotEqual(result.returncode,0)
    def test_verify_never_installs_or_ssh(self):
        result,commands=self.run_script('install.sh',action='verify')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual([name for name,_ in commands],['curl'])
    def test_unknown_action_never_connects(self):
        result,commands=self.run_script('install.sh',action='typo')
        self.assertEqual(result.returncode,2);self.assertEqual(commands,[])
class RemoteInstaller(unittest.TestCase):
    def fixture(self,action):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'root'
            paths=[root/'usr/local/bin/fnos-agent.py',root/'etc/systemd/system/fnos-agent.service',root/'etc/fnos-agent.env']
            for path in paths:path.parent.mkdir(parents=True,exist_ok=True);path.write_text('anonymous preserved fixture')
            ctl=Path(tmp)/'systemctl';ctl.write_text('#!/bin/sh\nexit 7\n');ctl.chmod(0o755)
            env=dict(os.environ,FNAS_INSTALL_ROOT=str(root),SYSTEMCTL=str(ctl))
            result=subprocess.run(['bash',str(ROOT/'nas/install-remote.sh'),action,'--purge-token'],env=env,capture_output=True,text=True,timeout=10)
            self.assertNotEqual(result.returncode,0)
            self.assertTrue(all(path.read_text()=='anonymous preserved fixture' for path in paths))
            return result
    def test_wrong_environment_file_suffix_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'root';source=Path(tmp)/'agent.py';unit=Path(tmp)/'unit'
            source.write_text('print("anonymous fixture")\n')
            unit.write_text('EnvironmentFile=-/etc/fnos-agent.env.wrong\n')
            result=subprocess.run(['bash',str(ROOT/'nas/install-remote.sh'),'install',str(source),str(unit)],env=dict(os.environ,FNAS_INSTALL_ROOT=str(root)),capture_output=True,text=True,timeout=10)
            self.assertNotEqual(result.returncode,0);self.assertIn('EnvironmentFile',result.stderr)
            self.assertFalse((root/'etc/fnos-agent.env').exists())
    def test_failed_stop_preserves_unit_code_and_token(self):
        self.assertIn('停止/禁用服务失败',self.fixture('uninstall').stderr)
    def test_unknown_action_rejected_before_mutation(self):
        self.assertEqual(self.fixture('typo').returncode,2)
if __name__=='__main__':unittest.main()
