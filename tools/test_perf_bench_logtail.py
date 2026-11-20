#!/usr/bin/env python3
"""perf_bench 串口行清洗回归：日志尾巴不得污染记录，前缀日志不得吞掉记录。"""
import unittest

import perf_bench as bench

VALID = "[bench] " + " ".join(f"{k}=1" for k in bench.FIELDS if k != "kind") + " kind=tap"


class CleanLine(unittest.TestCase):
    def test_suffix_log_is_stripped(self):
        dirty = VALID + "W (57753) esp-tls: read timeout"
        self.assertEqual(bench.clean_line(dirty), VALID)
        self.assertEqual(bench.parse_bench(bench.clean_line(dirty))["kind"], "tap")

    def test_prefix_log_keeps_record(self):
        dirty = "W (57753) esp-tls: boot noise " + VALID
        self.assertEqual(bench.clean_line(dirty), VALID)
        self.assertEqual(bench.parse_bench(bench.clean_line(dirty))["page"], 1)

    def test_both_ends_cut(self):
        dirty = "I (12) tag: noise " + VALID + " W (99) tag: tail"
        self.assertEqual(bench.clean_line(dirty), VALID)
        self.assertEqual(bench.parse_bench(bench.clean_line(dirty))["cycles"], 1)

    def test_truncated_record_is_not_a_sample(self):
        broken = VALID.rsplit(" ", 3)[0] + " tick_max_us="
        with self.assertRaises(ValueError):
            row = bench.parse_bench(bench.clean_line(broken))
            if row is None:
                raise ValueError("truncated record parsed as empty")

    def test_prefix_only_line_is_not_a_sample(self):
        self.assertEqual(bench.clean_line("W (5) esp-tls: only log"), "")
        self.assertIsNone(bench.parse_bench(""))


if __name__ == "__main__":
    unittest.main()
