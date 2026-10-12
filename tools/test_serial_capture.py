#!/usr/bin/env python3
"""Serial capture host faults and reset policy; never opens a device."""
import contextlib
import io
import itertools
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock
import serial_capture as target

class Port:
    def __init__(self, **kwargs):
        assert kwargs['port'] is None
        self.events=[]; self.port=None; self.closed=False; self.read_error=None
    def __setattr__(self,name,value):
        if name in ('rts','dtr') and 'events' in self.__dict__: self.events.append((name,value))
        object.__setattr__(self,name,value)
    def open(self): self.events.append(('open',self.port))
    def close(self): self.closed=True
    def read(self,n):
        if self.read_error: raise self.read_error
        return b''

class SerialCapture(unittest.TestCase):
    def run_main(self, reset=False, error=None, reset_error=None, file_error=None):
        port=Port(port=None);port.read_error=error
        def factory(**kwargs):
            self.assertIsNone(kwargs['port']); return port
        with tempfile.TemporaryDirectory() as tmp,contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.dict('sys.modules',{'serial':types.SimpleNamespace(Serial=factory)}))
            stack.enter_context(mock.patch.object(target.sys,'argv',['serial_capture.py','--port','fixture','--seconds','1','--out',str(Path(tmp)/'log')]+(['--reset'] if reset else [])))
            stack.enter_context(mock.patch.object(target.time,'monotonic',side_effect=itertools.count(0,0.2)))
            stack.enter_context(mock.patch.object(target.time,'sleep'))
            reset_mock=stack.enter_context(mock.patch.object(target,'hard_reset',side_effect=reset_error))
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            if file_error: stack.enter_context(mock.patch('builtins.open',side_effect=file_error))
            if error or reset_error or file_error:
                with self.assertRaises(OSError):target.main()
            else:self.assertEqual(target.main(),0)
            self.assertEqual(reset_mock.call_count,int(reset))
        self.assertTrue(port.closed)
        self.assertEqual(port.events[:3],[('rts',False),('dtr',False),('open','fixture')])
    def test_reset_line_sequence(self):
        port=Port(port=None)
        with mock.patch.object(target.time,'sleep'):target.hard_reset(port)
        self.assertEqual(port.events,[('rts',False),('dtr',True),('dtr',False)])
    def test_no_implicit_reset(self): self.run_main()
    def test_explicit_reset_once(self): self.run_main(reset=True)
    def test_read_failure_closes_port(self): self.run_main(error=OSError('fixture read'))
    def test_reset_failure_closes_port(self): self.run_main(reset=True,reset_error=OSError('fixture reset'))
    def test_log_open_failure_closes_port(self): self.run_main(file_error=OSError('fixture write'))
    def test_bad_duration_rejected_before_serial(self):
        for duration in ('nan','inf','-1','0'):
            with mock.patch.object(target.sys,'argv',['serial_capture.py','--port','fixture','--seconds',duration]),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as caught:target.main()
                self.assertEqual(caught.exception.code,2)
    def test_timestamped_tail_and_pending_budget(self):
        for chunks in ([b'half'],[b'a\nb'],[b'123456']):
            stream=io.StringIO();port=mock.Mock(port='fixture')
            port.read.side_effect=list(chunks)+[b'']*10
            with mock.patch.object(target,'MAX_PENDING',4),mock.patch.object(target.time,'monotonic',side_effect=itertools.count(0,0.2)),contextlib.redirect_stdout(io.StringIO()):
                target.capture(port,stream,1,'fixture.log')
            lines=stream.getvalue().splitlines()
            self.assertTrue(lines);self.assertTrue(all(line.startswith('[') for line in lines))
            self.assertIn(chunks[0].decode().replace('\n',''),''.join(line.split('] ',1)[1] for line in lines))
if __name__=='__main__':unittest.main()
