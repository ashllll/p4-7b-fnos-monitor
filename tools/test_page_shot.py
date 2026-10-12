#!/usr/bin/env python3
"""Capture failure handling, mocked: never opens a serial port or camera."""
import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('page_shot', Path(__file__).with_name('page_shot.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Capture(unittest.TestCase):
    def test_valid_image(self):
        with tempfile.TemporaryDirectory() as d:
            image = Path(d) / 'test.png'
            image.write_bytes(b'\x89PNG\r\n\x1a\n')
            result = subprocess.CompletedProcess([], 0, json.dumps({'image': str(image)}), '')
            with patch.object(m.subprocess, 'run', return_value=result):
                self.assertEqual(m.capture_photo()['image'], str(image))

    def test_invalid_results(self):
        for rc, out in ((1, '{}'), (0, ''), (0, 'broken'), (0, '{}'), (0, 'null'),
                        (0, '{"image":"relative.png"}'),
                        (0, '{"image":"/nonexistent/capture.png"}')):
            with self.subTest(rc=rc, out=out), patch.object(m.subprocess, 'run',
                    return_value=subprocess.CompletedProcess([], rc, out, '')):
                with self.assertRaises(RuntimeError):
                    m.capture_photo()

    def test_timeout_and_missing_command(self):
        for error in (FileNotFoundError(), subprocess.TimeoutExpired('capture', 180)):
            with patch.object(m.subprocess, 'run', side_effect=error):
                with self.assertRaises(RuntimeError):
                    m.capture_photo()


if __name__ == '__main__':
    unittest.main()
