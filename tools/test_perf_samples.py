#!/usr/bin/env python3
"""性能采样的分片、损坏记录与旧 CSV 兼容回归；不打开串口。"""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import perf_bench as bench
import perf_table as table


class Samples(unittest.TestCase):
    def record(self):
        values = {"kind": "tap", "cycles": 3, "p2f_n": 3, "r2f_n": 3, "misses": 0}
        return '[bench] ' + ' '.join(f'{k}={values.get(k, 1)}' for k in bench.FIELDS)

    def test_record_validation(self):
        self.assertEqual(bench.parse_bench(self.record())['frames'], 1)
        for line in ('[bench] kind=tap', self.record().replace('frames=1', 'frames=nan'),
                     self.record().replace('frames=1', 'frames=1.5'), '[bench] err=no-pointer-indev', self.record() + ' frames=2'):
            with self.subTest(line=line), self.assertRaises(ValueError):
                bench.parse_bench(line)

    def test_fragmented_line(self):
        record = self.record()
        class Serial:
            def __init__(self):
                self.chunks = iter([record[:30].encode(), record[30:].encode(), b'\n'])
                self.reads = 0
            def write(self, _): pass
            def flush(self): pass
            def read(self, _):
                self.reads += 1
                return next(self.chunks)
        serial = Serial()
        with patch.object(bench, 'drain'):
            row, _ = bench.run_case(serial, 'tap:3', 1)
        self.assertEqual(serial.reads, 3)
        self.assertEqual(row['cpu_idle'], 1)

    def test_incomplete_cycles_are_rejected(self):
        for field in ('cycles', 'p2f_n', 'r2f_n', 'misses'):
            old, new = (0, 1) if field == 'misses' else (3, 1)
            record = self.record().replace(f'{field}={old}', f'{field}={new}')
            class Serial:
                def write(self, _): pass
                def flush(self): pass
                def read(self, _): return (record + '\n').encode()
            with self.subTest(field=field), patch.object(bench, 'drain'), self.assertRaises(ValueError):
                bench.run_case(Serial(), 'tap:3:3', 1)

    def test_missing_newline_times_out(self):
        class Serial:
            def write(self, _): pass
            def flush(self): pass
            def read(self, _): return b'[bench] kind=tap'
        with patch.object(bench, 'drain'), self.assertRaises(TimeoutError):
            bench.run_case(Serial(), 'tap:3', .01)

    def test_csv_validation_and_legacy_columns(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'samples.csv'
            path.write_text('case,frames\ntap:3,12\n')
            rows = table.load(path)['tap:3']
            self.assertEqual(table.med(rows, 'frames'), 12)
            self.assertIsNone(table.med(rows, 'iv_n'))
            for data in ('', 'case,page\ntap:3,3\n', 'case,frames\n', 'case,frames\ntap:3,\n',
                         'case,frames\ntap:3,nan\n', 'case,frames\ntap:3,inf\n',
                         'case,frames\n,1\n', 'case,frames\ntap:3,1,2\n',
                         'case,frames,frames\ntap:3,1,1\n',
                         'case,frames,page\ntap:3,1,\n', 'case,frames\ntap:3,1.5\n'):
                path.write_text(data)
                with self.subTest(data=data), self.assertRaises(ValueError):
                    table.load(path)

if __name__ == '__main__':
    unittest.main()
