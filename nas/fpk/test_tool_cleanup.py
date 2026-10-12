#!/usr/bin/env python3
"""Local checker fault cleanup: temporary sockets/children/files only."""
import contextlib
import importlib.util
import io
import http.client
import os
import signal
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock
HERE=Path(__file__).resolve().parent
def load(name):
    spec=importlib.util.spec_from_file_location(name,HERE/(name+'.py'))
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module); return module
docker=load('docker_check'); perm=load('perm_check'); devmode=load('devmode_check')
class CleanupTests(unittest.TestCase):
    def reader_fault(self,patch):
        children=[]; sockets=[]; fork=os.fork; socketpair=docker.socket.socketpair
        def record_fork():
            pid=fork()
            if pid: children.append(pid)
            return pid
        def record_sockets():
            pair=socketpair(); sockets.extend(pair); return pair
        with mock.patch.object(docker.os,'fork',side_effect=record_fork), mock.patch.object(docker.socket,'socketpair',side_effect=record_sockets), patch:
            with self.assertRaises((RuntimeError,AssertionError,ConnectionError,EOFError)):
                docker.reader_roundtrip(b'R',{'rows':[]},0)
        self.assertEqual(len(children),1)
        self.assertTrue(all(s.fileno()==-1 for s in sockets))
        with self.assertRaises(ChildProcessError): os.waitpid(children[0],os.WNOHANG)
    def test_parent_protocol_exception(self):
        self.reader_fault(mock.patch.object(docker.api,'_receive',side_effect=RuntimeError('fixture framing failure')))
    def test_child_failure(self):
        self.reader_fault(mock.patch.object(docker.launch,'reader',side_effect=RuntimeError('fixture child failure')))
    def test_reap_deadline(self):
        pid=os.fork()
        if not pid:
            time.sleep(30); os._exit(0)
        try:
            with self.assertRaises(AssertionError): docker.reap_child(pid,timeout=0.02)
            with self.assertRaises(ChildProcessError): os.waitpid(pid,os.WNOHANG)
        finally:
            try:
                found,_=os.waitpid(pid,os.WNOHANG)
                if not found: os.kill(pid,signal.SIGKILL); os.waitpid(pid,0)
            except ChildProcessError: pass
    def test_permission_cleanup_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            tree=Path(tmp)/'fixture'; tree.mkdir(); inner=tree/'denied'; inner.mkdir(); (inner/'data').write_text('x'); inner.chmod(0)
            self.assertTrue(perm.cleanup_tree(str(tree))); self.assertFalse(tree.exists())
    def test_delete_failure_non_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            tree=Path(tmp)/'fixture'; tree.mkdir()
            with mock.patch.object(perm.shutil,'rmtree',side_effect=OSError('fixture delete denied')),contextlib.redirect_stdout(io.StringIO()):
                self.assertFalse(perm.cleanup_tree(str(tree)))
            self.assertTrue(tree.exists())
            self.assertTrue(perm.cleanup_tree(str(tree)))
    def test_chmod_failure_non_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            tree=Path(tmp)/'fixture'; tree.mkdir(); (tree/'inner').mkdir()
            with mock.patch.object(perm.os,'chmod',side_effect=OSError('fixture chmod denied')),contextlib.redirect_stdout(io.StringIO()):
                self.assertFalse(perm.cleanup_tree(str(tree)))
            self.assertFalse(tree.exists())
    def test_main_cleanup_failure_returns_nonzero(self):
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(perm.tempfile,'mkdtemp',return_value=tmp),mock.patch.object(perm,'cleanup_tree',return_value=False),contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(perm.main(),1)
class DevmodeCleanupTests(unittest.TestCase):
    def service_fault(self, unix_error=None, url_error=None, wait_timeout=False):
        process=mock.Mock(); process.poll.return_value=None
        if wait_timeout:
            process.wait.side_effect=[devmode.subprocess.TimeoutExpired('fixture',5),0]
        connection=mock.Mock()
        if unix_error: connection.request.side_effect=unix_error
        output=io.StringIO()
        with tempfile.TemporaryDirectory() as work, contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.object(devmode,'free_port',return_value=24680))
            stack.enter_context(mock.patch.object(devmode.time,'sleep'))
            stack.enter_context(mock.patch.object(devmode.subprocess,'run',return_value=mock.Mock(returncode=0)))
            stack.enter_context(mock.patch.object(devmode.subprocess,'Popen',return_value=process))
            stack.enter_context(mock.patch.object(http.client.HTTPConnection,'__new__',return_value=connection))
            stack.enter_context(mock.patch.object(devmode.urllib.request,'urlopen',side_effect=url_error) if url_error else mock.patch.object(devmode.urllib.request,'urlopen'))
            stack.enter_context(contextlib.redirect_stdout(output))
            result=devmode.check_service(devmode.DEFAULT_PKG,1,work)
        connection.close.assert_called_once(); process.send_signal.assert_called_once_with(signal.SIGTERM)
        if wait_timeout:
            process.kill.assert_called_once(); self.assertEqual(process.wait.call_count,2)
        else:
            process.wait.assert_called_once_with(timeout=5)
        return result,output.getvalue()
    def test_unix_protocol_error_reaped_and_nonzero(self):
        result,output=self.service_fault(unix_error=http.client.BadStatusLine('fixture'))
        self.assertEqual(result,1); self.assertNotIn('✓',output)
    def test_url_protocol_error_reaped_and_nonzero(self):
        result,output=self.service_fault(url_error=http.client.RemoteDisconnected('fixture'))
        self.assertEqual(result,1); self.assertNotIn('✓',output)
    def test_unix_io_error_nonzero(self):
        result,_=self.service_fault(unix_error=OSError('fixture'))
        self.assertEqual(result,1)
    def test_terminate_timeout_kill_wait(self):
        result,_=self.service_fault(wait_timeout=True)
        self.assertEqual(result,0)
    def test_main_staging_removed_on_exception(self):
        created=[]; temporary=devmode.tempfile.TemporaryDirectory
        def record(*args,**kwargs):
            value=temporary(*args,**kwargs); created.append(value.name); return value
        with mock.patch.object(devmode.sys,'argv',['devmode_check.py','1']), mock.patch.object(devmode.tempfile,'TemporaryDirectory',side_effect=record), mock.patch.object(devmode,'check_service',side_effect=RuntimeError('fixture')):
            with self.assertRaises(RuntimeError): devmode.main()
        self.assertEqual(len(created),1); self.assertFalse(Path(created[0]).exists())
    def test_install_failure_reports_code_and_no_child(self):
        with tempfile.TemporaryDirectory() as work, mock.patch.object(devmode,'free_port',return_value=24680),mock.patch.object(devmode.subprocess,'run',return_value=mock.Mock(returncode=7,stdout='',stderr='fixture install failure')),mock.patch.object(devmode.subprocess,'Popen') as popen,contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(devmode.check_service(devmode.DEFAULT_PKG,1,work),1)
            self.assertIn('返回 7',output.getvalue()); self.assertIn('fixture install failure',output.getvalue()); popen.assert_not_called()
if __name__=='__main__': unittest.main()
