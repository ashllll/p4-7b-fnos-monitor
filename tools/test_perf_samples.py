#!/usr/bin/env python3
"""性能采样的分片、损坏记录与旧 CSV 兼容回归；不打开串口。"""
import csv
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import perf_bench as bench
import perf_table as table


class Samples(unittest.TestCase):
    def record(self, **over):
        values = {"kind": "tap", "page": 3, "cycles": 3, "p2f_n": 3, "r2f_n": 3, "misses": 0}
        values.update(over)
        return '[bench] ' + ' '.join(f'{k}={values.get(k, 1)}' for k in bench.FIELDS)

    def aux(self, family, **values):
        return '[bench_%s] kind=tap page=3 ' % family + ' '.join(f'{k}={v}' for k, v in values.items())

    def ui_line(self, **over):
        values = {"frames": 10, "gesture_frames": 4, "gesture_fps": 0, "chart_frames": 2, "draw_stages": 1,
                  "page_paint_us": 1000, "page_paint_n": 10, "page_paint_max_us": 300,
                  "page_layout_us": 400, "page_layout_n": 2, "page_layout_max_us": 250,
                  "chart_draw_us": 2000, "chart_draw_n": 4, "chart_draw_max_us": 900}
        values.update(over)
        return self.aux("ui", **values)

    def tick_line(self):
        return self.aux("tick", motion_n=4, motion_p50_us=100, motion_p95_us=200, motion_max_us=300,
                        full_n=2, full_p50_us=1000, full_p95_us=2000, full_max_us=3000)

    def parts_line(self):
        return self.aux("parts", frames=4, frame_us=9000, flush_us=3000, flush_wait_us=500,
                        ppa_us=800, ppa_n=2, ppa_max_us=500,
                        dma2d_us=0, dma2d_n=0, dma2d_max_us=0,
                        submit_us=200, submit_n=1, submit_max_us=200,
                        freebuf_us=0, freebuf_n=0, freebuf_max_us=0)

    def draw_line(self):
        return self.aux("draw", frames=4, units=10, worker_n=3, cpu_worker=12.5,
                        create_us=100, create_n=2, create_max_us=60,
                        pick_us=0, pick_n=0, pick_max_us=0,
                        blend_us=300, blend_n=3, blend_max_us=120,
                        text_us=700, text_n=2, text_max_us=400)

    def test_record_validation(self):
        self.assertEqual(bench.parse_bench(self.record())['frames'], 1)
        for line in ('[bench] kind=tap', self.record().replace('frames=1', 'frames=nan'),
                     self.record().replace('frames=1', 'frames=1.5'), '[bench] err=no-pointer-indev', self.record() + ' frames=2'):
            with self.subTest(line=line), self.assertRaises(ValueError):
                bench.parse_bench(line)

    def test_wrong_page_is_rejected(self):
        class Serial:
            def __init__(self, payload):
                self.buf = (payload + '\n').encode()
            def write(self, _): pass
            def flush(self): pass
            def read(self, _):
                chunk, self.buf = self.buf, b''
                return chunk
        with patch.object(bench, 'drain'), self.assertRaises(ValueError):
            bench.run_case(Serial(self.record(kind='drag', page=0, cycles=1, p2f_n=1, r2f_n=1)),
                           'drag:2:600:2000', 1)
        with patch.object(bench, 'drain'), self.assertRaises(ValueError):
            bench.run_case(Serial(self.record(kind='tap', page=1)), 'tap:3', 1)

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

    def test_aux_lines_merge_and_derive(self):
        lines = [self.parts_line(), self.draw_line(), self.ui_line(), self.tick_line(), self.record(frames=10)]
        class Serial:
            def __init__(self): self.buf = ('\n'.join(lines) + '\n').encode()
            def write(self, _): pass
            def flush(self): pass
            def read(self, _):
                chunk, self.buf = self.buf, b''
                return chunk
        with patch.object(bench, 'drain'):
            row, _ = bench.run_case(Serial(), 'tap:3', 1)
        self.assertEqual(row['part_flush_us'], 3000)
        self.assertEqual(row['part_ppa_us'], 800)
        self.assertEqual(row['draw_blend_us'], 300)
        self.assertEqual(row['draw_cpu_worker'], 12.5)
        self.assertEqual(row['ui_page_paint_us'], 1000)
        self.assertEqual(row['tick_motion_p50_us'], 100)
        self.assertEqual(row['ui_page_paint_us_pf'], 100.0)
        self.assertEqual(row['ui_page_layout_us_pc'], 200.0)
        self.assertEqual(row['ui_chart_draw_us_pf'], 500.0)
        self.assertEqual(row['ui_chart_draw_us_pf_chart'], 1000.0)
        self.assertEqual(row['ui_chart_less_frames'], 2)
        self.assertEqual(row['part_flush_us_pf'], 750.0)
        self.assertEqual(row['draw_text_us_pf'], 175.0)
        self.assertEqual(row['tick_full_p50_us'], 1000)

    def test_draw_stages_marker_and_missing_row(self):
        # DRAW=n firmware omits [bench_draw]; the marker says so and the
        # per-frame draw columns stay absent instead of becoming 0.
        lines = [self.parts_line(), self.ui_line(draw_stages=0), self.record(frames=10)]
        class Serial:
            def __init__(self): self.buf = ('\n'.join(lines) + '\n').encode()
            def write(self, _): pass
            def flush(self): pass
            def read(self, _):
                chunk, self.buf = self.buf, b''
                return chunk
        with patch.object(bench, 'drain'):
            row, _ = bench.run_case(Serial(), 'tap:3', 1)
        self.assertEqual(row['ui_draw_stages'], 0)
        self.assertIsNone(row.get('draw_text_us'))
        self.assertIsNone(row.get('draw_text_us_pf'))
        self.assertEqual(row['ui_page_paint_us_pf'], 100.0)

    def test_aux_rejects_bad_values(self):
        for line in (self.ui_line(page_paint_us=-1), self.ui_line(page_paint_us=1.5),
                     self.ui_line(page_paint_us='nan'), self.ui_line() + ' page_paint_us=2',
                     self.aux('tick', motion_p50_us=-5), self.draw_line().replace('cpu_worker=12.5', 'cpu_worker=nan'),
                     self.ui_line().replace('kind=tap', 'kind=bogus'),
                     '[bench_ui] kind=tap page=2',
                     '[bench_ui] kind=tap page=2 page_paint_us=17',
                     '[bench_ui] kind=tap page=oops frames=1 page_paint_us=17'):
            with self.subTest(line=line), self.assertRaises(ValueError):
                bench.parse_aux(line)
        self.assertIsNone(bench.parse_aux('[bench] kind=tap'))
        self.assertIsNone(bench.parse_aux('[bench_probe] n=1 ns_per_probe=2'))

    def test_aux_window_bounds(self):
        for lines in ([self.ui_line(draw_stages=0, chart_frames=5)], [self.ui_line(draw_stages=0, gesture_frames=11)]):
            serial = type('Serial', (), {
                'buf': ('\n'.join(lines + [self.record(frames=10)]) + '\n').encode(),
                'write': lambda self, _: None, 'flush': lambda self: None,
                'read': lambda self, _: (self.buf, setattr(self, 'buf', b''))[0]})()
            with self.subTest(lines=lines), patch.object(bench, 'drain'), self.assertRaises(ValueError):
                bench.run_case(serial, 'tap:3', 1)

    def test_summary_skips_absent_aux(self):
        rows = [
            {"ui_page_paint_us_pf": None, "part_flush_us_pf": None, "frames": 3},
            {"ui_page_paint_us_pf": 120.0, "part_flush_us_pf": 40.0, "frames": 3},
            {"ui_page_paint_us_pf": 240.0, "part_flush_us_pf": 80.0, "frames": 3},
        ]
        self.assertEqual(bench.median_positive(rows, "ui_page_paint_us_pf"), 180.0)
        self.assertEqual(bench.median_positive(rows, "tick_full_p50_us"), 0)
        for r in rows:
            r["ui_page_paint_us_pf"] = None
        self.assertEqual(bench.median_positive(rows, "ui_page_paint_us_pf"), 0)

    def test_csv_roundtrip_with_attribution(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'new.csv'
            cols = ['label', 'case', 'repeat'] + bench.FIELDS + bench.AUX_FIELDS + bench.DERIVED_FIELDS
            row = {c: 0 for c in cols}
            row.update({'label': 'x', 'case': 'swipe:2', 'repeat': 0, 'kind': 'swipe', 'frames': 4,
                        'part_frames': 4, 'draw_frames': 4, 'ui_frames': 10, 'ui_gesture_frames': 4,
                        'ui_page_paint_us': 1000, 'ui_chart_draw_us': 2000, 'ui_chart_frames': 2})
            with open(path, 'w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=cols)
                writer.writeheader()
                writer.writerow(row)
            loaded = table.load(path)['swipe:2'][0]
            self.assertEqual(table.med([loaded], 'ui_page_paint_us'), 1000)
            self.assertEqual(table.med([loaded], 'ui_page_paint_us_pf'), 100)
            self.assertEqual(table.med([loaded], 'ui_chart_draw_us_pf'), 500)
            self.assertEqual(table.med([loaded], 'ui_chart_less_frames'), 2)
            self.assertEqual(table.med([loaded], 'ui_gesture_frames'), 4)

    def test_old_firmware_has_no_aux_columns(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'old.csv'
            path.write_text('case,frames\nswipe:2,120\n')
            rows = table.load(path)['swipe:2']
            self.assertIsNone(table.med(rows, 'ui_page_paint_us_pf'))

    def test_old_firmware_new_header_empty_aux(self):
        # Current writer emits every aux column; an old firmware row leaves them
        # empty. Loading must skip the derived math instead of dividing None.
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'old-new-header.csv'
            cols = ['label', 'case', 'repeat'] + bench.FIELDS + bench.AUX_FIELDS + bench.DERIVED_FIELDS
            row = {c: 0 for c in bench.FIELDS}
            row.update({'label': 'old', 'case': 'swipe:2', 'repeat': 0, 'kind': 'swipe', 'frames': 120})
            row.update({c: '' for c in bench.AUX_FIELDS + bench.DERIVED_FIELDS})
            with open(path, 'w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=cols)
                writer.writeheader()
                writer.writerow(row)
            rows = table.load(path)['swipe:2']
            self.assertIsNone(table.med(rows, 'ui_page_paint_us_pf'))
            self.assertIsNone(table.med(rows, 'part_flush_us_pf'))

    def test_old_firmware_new_header_blank_optional_main_fields(self):
        # Current writer adds p99/long at the row end; an old firmware row leaves
        # those two cells empty. They must stay "not measured", not fail the load.
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'old-new-header-optional.csv'
            cols = ['label', 'case', 'repeat'] + bench.FIELDS + bench.AUX_FIELDS + bench.DERIVED_FIELDS
            row = {c: 0 for c in bench.FIELDS}
            row.update({'label': 'old', 'case': 'tap:2', 'repeat': 0, 'kind': 'tap', 'frames': 120,
                        'iv_p99_us': '', 'iv_long_n': ''})
            row.update({c: '' for c in bench.AUX_FIELDS + bench.DERIVED_FIELDS})
            with open(path, 'w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=cols)
                writer.writeheader()
                writer.writerow(row)
            rows = table.load(path)['tap:2']
            self.assertIsNone(table.med(rows, 'iv_p99_us'))
            self.assertIsNone(table.med(rows, 'iv_long_n'))

    def test_short_row_without_case_is_value_error(self):
        # A short CSV row leaves the trailing `case` cell as None; loader must
        # report an invalid sample, not crash on None.strip().
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'short-row.csv'
            path.write_text('frames,case\n1\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                table.load(path)

    def test_draw_guard_rejects_acceptance_row(self):
        with self.assertRaises(RuntimeError):
            bench.require_draw({'ui_draw_stages': 0.0})
        with self.assertRaises(RuntimeError):
            bench.require_draw({'ui_draw_stages': None})
        bench.require_draw({'ui_draw_stages': 1.0})

    def test_cap_guard_rejects_stale_row(self):
        with self.assertRaises(RuntimeError):
            bench.require_cap({'ui_gesture_fps': None}, 30)
        with self.assertRaises(RuntimeError):
            bench.require_cap({'ui_gesture_fps': 0.0}, 30)
        bench.require_cap({'ui_gesture_fps': 30.0}, 30)

    def test_aux_identity_and_frame_bound(self):
        class Serial:
            def __init__(self, payload): self.buf = (payload + '\n').encode()
            def write(self, _): pass
            def flush(self): pass
            def read(self, _):
                chunk, self.buf = self.buf, b''
                return chunk
        wrong_kind = self.ui_line(draw_stages=0).replace('kind=tap', 'kind=swipe') + '\n' + self.record(frames=4)
        too_many = self.ui_line(draw_stages=0, frames=9) + '\n' + self.record(frames=4)
        chart_over = self.ui_line(draw_stages=0, frames=1, chart_frames=2) + '\n' + self.record(frames=4)
        for payload in (wrong_kind, too_many, chart_over):
            with self.subTest(payload=payload), patch.object(bench, 'drain'), self.assertRaises(ValueError):
                bench.run_case(Serial(payload), 'tap:3', 1)

    def test_bench_p99_long_and_legacy_row(self):
        row = bench.parse_bench(self.record(iv_p99_us=40000, iv_long_n=2))
        self.assertEqual(row["iv_p99_us"], 40000)
        self.assertEqual(row["iv_long_n"], 2)
        legacy = 'kind=tap ' + ' '.join(f'{k}={1}' for k in bench.FIELDS
                                       if k not in bench.BENCH_OPTIONAL_FIELDS | {'kind'})
        parsed = bench.parse_bench('[bench] ' + legacy)
        self.assertNotIn("iv_p99_us", parsed)
        self.assertNotIn("iv_long_n", parsed)

    def test_draw_marker_mismatch(self):
        class Serial:
            def __init__(self, payload): self.buf = (payload + '\n').encode()
            def write(self, _): pass
            def flush(self): pass
            def read(self, _):
                chunk, self.buf = self.buf, b''
                return chunk
        stale = self.draw_line() + '\n' + self.ui_line(draw_stages=0) + '\n' + self.record(frames=10)
        lost = self.ui_line(draw_stages=1) + '\n' + self.record(frames=10)
        for payload in (stale, lost):
            with self.subTest(payload=payload), patch.object(bench, 'drain'), self.assertRaises(ValueError):
                bench.run_case(Serial(payload), 'tap:3', 1)

    def test_drag_case_and_runner(self):
        self.assertEqual(bench.case_to_cmd('drag:2:600:2000'), 'bench drag 2 600 2000')
        self.assertEqual(bench.case_to_cmd('drag:2'), 'bench drag 2 600 2000')
        self.assertEqual(bench.parse_bench(self.record(kind='drag', cycles=1, p2f_n=1, r2f_n=1))['kind'], 'drag')

        class Serial:
            def __init__(self, payload):
                self.buf = (payload + '\n').encode()
                self.written = []
            def write(self, data): self.written.append(data)
            def flush(self): pass
            def read(self, _):
                chunk, self.buf = self.buf, b''
                return chunk

        ser = Serial(self.record(kind='drag', page=2, cycles=1, p2f_n=1, r2f_n=1))
        with patch.object(bench, 'drain'):
            row, _ = bench.run_case(ser, 'drag:2:600:2000', 1)
        self.assertEqual(ser.written, [b'bench drag 2 600 2000\n'])
        self.assertEqual(row['kind'], 'drag')
        self.assertEqual(row['cycles'], 1)
        with patch.object(bench, 'drain'), self.assertRaises(ValueError):
            bench.run_case(Serial(self.record(kind='drag', page=2, cycles=3, p2f_n=3, r2f_n=3)),
                           'drag:2:600:2000', 1)


if __name__ == '__main__':
    unittest.main()
