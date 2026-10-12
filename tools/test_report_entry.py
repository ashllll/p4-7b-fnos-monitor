#!/usr/bin/env python3
"""Exercise the production report entry with anonymous command stubs, never real suites."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
BASH = shutil.which('bash')

class ReportEntry(unittest.TestCase):
    def run_report(self, args=('--full',), **options):
        with tempfile.TemporaryDirectory(prefix='fnos-report-entry-') as directory:
            root=Path(directory)
            for name in ('tools','docs','nas/fpk/nasscreencompanion/cmd','build','bin'):
                (root/name).mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/'tools/test_report.sh',root/'tools/test_report.sh')
            if options.pop('unchecked_patch',False):
                script=root/'tools/test_report.sh'
                script.write_text(script.read_text().replace('set -euo pipefail','set -uo pipefail',1))
            suite=root/'nas/fpk/test_lifecycle.sh'
            suite.write_text('#!/bin/sh\nprintf "  \\033[32m✓ fixture\\n结果：1 项通过，0 项失败\\n"\nexit "${FIXTURE_SUITE_RC:-0}"\n')
            if options.pop('empty_suite',False):
                suite.write_text('#!/bin/sh\nprintf "结果：0 项通过，0 项失败\\n"\n')
            suite.chmod(0o700)
            for name, data in {
                'bash':'#!/bin/sh\nexit 0\n',
                'python3':'''#!/bin/sh
case "$1" in
 -V) echo "Python fixture"; exit 0 ;;
 -) [ "${PATCH_FAIL:-0}" = 0 ] || exit 7; exec "$REAL_PYTHON" "$@" ;;
 tools/soak_check.py) exit "${FIXTURE_SOAK_RC:-0}" ;;
 tools/plan_check.py) exit "${PLAN_RC:-0}" ;;
 *) exit "${CHECK_RC:-0}" ;;
esac
''',
                'fnpack':'#!/bin/sh\necho "Version 1.0"\n',
                'node':'#!/bin/sh\necho "fixture"\n',
                'riscv32-esp-elf-objdump':'#!/bin/sh\nexit 0\n',
            }.items():
                file=root/'bin'/name; file.write_text(data); file.chmod(0o700)
            if options.pop('temp_fail',False):
                file=root/'bin/mktemp';file.write_text('#!/bin/sh\nexit 1\n');file.chmod(0o700)
            if options.pop('write_fail',False):
                (root/'docs/fnos-companion-test-report.md').mkdir()
            env=dict(os.environ,PATH=str(root/'bin')+os.pathsep+os.environ['PATH'],REAL_PYTHON=sys.executable,TMPDIR=str(root))
            env.update({('FIXTURE_'+key if key in ('SUITE_RC','SOAK_RC') else key):str(value) for key,value in options.items()})
            result=subprocess.run([BASH,str(root/'tools/test_report.sh'),*args],env=env,capture_output=True,text=True,timeout=30)
            report=root/'docs/fnos-companion-test-report.md'
            text=report.read_text() if report.is_file() else ''
            return result, text

    def test_success_has_reproduction_commands(self):
        result,text=self.run_report()
        self.assertEqual(result.returncode,0,result.stderr+result.stdout)
        self.assertNotIn('__PLAN_PENDING__',text)
        self.assertIn('python3 tools/test_ui_requests.py',text)
        self.assertNotIn('| — |',text)

    def test_incomplete_is_distinct(self):
        for args, options in (((),{}),(('--quick','--full'),{}),(('--full',),{'SOAK_RC':2}),(('--full',),{'SUITE_RC':2})):
            with self.subTest(args=args,options=options):
                result,text=self.run_report(args,**options)
                self.assertEqual(result.returncode,2,result.stderr+result.stdout)
                self.assertIn('本轮证据未完成',result.stdout)
                self.assertTrue(text)

    def test_failures_override_incomplete(self):
        for options in ({'SUITE_RC':7},{'CHECK_RC':1},{'PLAN_RC':1},{'empty_suite':True}):
            with self.subTest(options=options):
                result,text=self.run_report((),**options)
                self.assertEqual(result.returncode,1,result.stderr+result.stdout)
                self.assertTrue(text)

    def test_workspace_write_and_patch_errors_propagate(self):
        for options in ({'temp_fail':True},{'write_fail':True},{'PATCH_FAIL':1}):
            with self.subTest(options=options):
                result,_=self.run_report(**options)
                self.assertNotEqual(result.returncode,0)
                self.assertNotIn('回填 plan_check 结论：',result.stdout)

    def test_unchecked_patch_negative_control(self):
        result,text=self.run_report(PATCH_FAIL=1,unchecked_patch=True)
        self.assertEqual(result.returncode,0)
        self.assertIn('__PLAN_PENDING__',text)
        self.assertIn('回填 plan_check 结论：',result.stdout)

    def test_unknown_option_rejected_before_checks(self):
        result,text=self.run_report(('--typo',))
        self.assertEqual(result.returncode,2)
        self.assertEqual(text,'')
        self.assertNotIn('跑 NAS',result.stdout)

if __name__ == '__main__': unittest.main()
