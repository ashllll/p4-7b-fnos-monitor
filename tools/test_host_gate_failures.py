#!/usr/bin/env python3
"""空提取与配置缺失负控；只操作临时文件，不移动真实配置。"""
import contextlib
import json
import os
import struct
import zlib
import sys
import serial_check
import io
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import reason_check
from parsecheck.extract import grab

ROOT = Path(__file__).resolve().parents[1]

class GateFailures(unittest.TestCase):
    def reason(self, produced, translated):
        with tempfile.TemporaryDirectory() as tmp:
            data, ui = Path(tmp) / 'data.c', Path(tmp) / 'ui.c'
            data.write_text(produced)
            ui.write_text(translated)
            with patch.object(reason_check, 'DATA_C', str(data)), patch.object(reason_check, 'UI_C', str(ui)), contextlib.redirect_stdout(io.StringIO()):
                return reason_check.main()

    def test_preview_evidence_failures(self):
        source=(ROOT/'tools/preview/preview.c').read_text()
        helpers='\n'.join(grab(source, name) for name in ('evidence_flush','evidence_close','preview_finish'))
        main=source[source.index('int main(int argc, char **argv)'):]
        # Every successful mode must consume the shared receipt. The finalizer alone
        # cannot prove that an early branch actually calls it.
        self.assertNotIn('return 0;',main)
        for call in ['fclose(csv);','fclose(record.timeline);','fclose(timeline);','fflush(s_barmap);']:
            self.assertNotIn(call,source)
        prelude=r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *s_barmap; static int s_audit_fail;
static int sticky,flush_bad,close_bad,closed;
static int test_ferror(FILE *f){(void)f;return sticky;}
static int test_fflush(FILE *f){(void)f;return flush_bad?-1:0;}
static int test_fclose(FILE *f){(void)f;closed++;return close_bad?-1:0;}
#define ferror test_ferror
#define fflush test_fflush
#define fclose test_fclose
'''
        driver=r'''int main(int argc,char **argv){
 const char *m=argc>1?argv[1]:"ok";
 if(!strcmp(m,"audit"))s_audit_fail=1;
 if(!strcmp(m,"sticky"))sticky=1;
 if(!strcmp(m,"flush"))flush_bad=1;
 if(!strcmp(m,"close")){close_bad=1;s_barmap=stdout;}
 if(!strcmp(m,"barmap"))s_barmap=stdout;
 int rc=preview_finish();
 if(!strcmp(m,"barmap") && (s_barmap || closed!=1))return 9;
 return rc;
}'''
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); c=root/'receipt.c'; exe=root/'receipt'
            def build(text):
                c.write_text(prelude+text+driver)
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True,capture_output=True,timeout=30)
            build(helpers)
            for mode in ['ok','barmap','audit','sticky','flush','close']:
                result=subprocess.run([str(exe),mode],capture_output=True,timeout=5)
                self.assertEqual(result.returncode,0 if mode in ['ok','barmap'] else 1,mode)
                if result.returncode:self.assertIn(b'FAIL:',result.stderr)
            build(helpers.replace('if (s_audit_fail) {','if (0) {'))
            self.assertEqual(subprocess.run([str(exe),'audit'],capture_output=True,timeout=5).returncode,0)
            build(helpers.replace('if (fclose(f) != 0) failed = 1;','(void)fclose(f);'))
            self.assertEqual(subprocess.run([str(exe),'close'],capture_output=True,timeout=5).returncode,0)
        # This rejects the old early-mode return even with a correct finalizer.
        mutant=main.replace('record_motion(touch);\n        return preview_finish();','record_motion(touch);\n        return 0;')
        self.assertNotEqual(mutant,main)
        self.assertIn('return 0;',mutant)

    def test_lifecycle_entry_failures(self):
        source = (ROOT/'nas/fpk/test_lifecycle.sh').read_text()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); pkg=root/'nasscreencompanion'
            (pkg/'cmd').mkdir(parents=True); (pkg/'app').mkdir()
            (pkg/'cmd/example').write_text('fixture')
            bindir=root/'bin'; bindir.mkdir()
            (bindir/'fnpack').write_text('#!/bin/bash\nexit 7\n')
            (bindir/'fnpack').chmod(0o755)
            entry=root/'test_lifecycle.sh'
            prefix=source[:source.index('step "0b.')]
            env=dict(os.environ,PATH=str(bindir)+os.pathsep+os.environ['PATH'],TMPDIR=str(root))
            entry.write_text(prefix)
            result=subprocess.run(['bash',str(entry),'18797'],env=env,capture_output=True,timeout=10)
            self.assertEqual(result.returncode,1)
            self.assertIn(b'fnpack build',result.stderr)
            self.assertNotIn('fnpack build 成功'.encode(), result.stdout)
            for port in ['bad','0','65516','9999999999999999999']:
                result=subprocess.run(['bash',str(entry),port],env=env,capture_output=True,timeout=10)
                self.assertEqual(result.returncode,2)
            contract=source[source.index('CONTRACT_RC=$?'):source.index("python3 -c 'import json,sys;d=json.load", source.index('CONTRACT_RC=$?'))]
            setup='PASS=0; FAIL=0; ok(){ PASS=$((PASS+1)); }; bad(){ FAIL=$((FAIL+1)); }; CONTRACT=""; (exit 7)\n'
            result=subprocess.run(['bash','-c',setup+contract+'\ntest "$FAIL" = 1 && test "$PASS" = 0'],capture_output=True,timeout=5)
            self.assertEqual(result.returncode,0)
            mutant=contract.replace('[ "$CONTRACT_RC" = 0 ] && ','')
            result=subprocess.run(['bash','-c',setup+mutant+'\ntest "$FAIL" = 1 && test "$PASS" = 0'],capture_output=True,timeout=5)
            self.assertNotEqual(result.returncode,0)
            mb=source[source.index('MB_RC=$?'):source.index('\ngrep -qE "sed -i',source.index('MB_RC=$?'))]
            result=subprocess.run(['bash','-c',setup.replace('CONTRACT=""','MB=""')+mb+'\ntest "$FAIL" = 1 && test "$PASS" = 0'],capture_output=True,timeout=5)
            self.assertEqual(result.returncode,0)
            scanner=source[source.index('grep -qE "sed -i'):source.index('step "2.')]
            result=subprocess.run(['bash','-c','PASS=0; FAIL=0; ok(){ PASS=$((PASS+1)); }; bad(){ FAIL=$((FAIL+1)); }; CMD=/definitely/missing;\n'+scanner+'\ntest "$FAIL" = 1 && test "$PASS" = 0'],capture_output=True,timeout=5)
            self.assertEqual(result.returncode,0)
            helper=source[source.index('must() {'):source.index('\nstep "0.')]
            result=subprocess.run(['bash','-c',helper+'\nmust bash -c "exit 7"; echo UNREACHED'],capture_output=True,timeout=5)
            self.assertEqual(result.returncode,1)
            self.assertNotIn(b'UNREACHED',result.stdout)
            page=source[source.index('admin "http://x/app/nasscreencompanion/" >'):source.index('check "$(admin -o',source.index('admin "http://x/app/nasscreencompanion/" >'))]
            result=subprocess.run(['bash','-c','set -o pipefail; PASS=0; FAIL=0; WORK="$1"; ok(){ PASS=$((PASS+1)); }; bad(){ FAIL=$((FAIL+1)); }; admin(){ printf "<meta charset"; };\n'+page+'\ntest "$PASS" = 1 && test "$FAIL" = 0','fixture',str(root)],capture_output=True,timeout=5)
            self.assertEqual(result.returncode,0)
            tail=source[source.index('[ "$FAIL" = 0 ] || exit 1'):]
            for fail,skip,want in [(0,0,0),(0,13,2),(1,13,1)]:
                result=subprocess.run(['bash','-c',f'FAIL={fail}; SKIPPED={skip};\n'+tail],capture_output=True,timeout=5)
                self.assertEqual(result.returncode,want)

    def test_lifecycle_cleanup_ownership(self):
        source=(ROOT/'nas/fpk/test_lifecycle.sh').read_text()
        helpers=source[source.index('DIRECT_CHILDREN=""'):source.index('\nCACERT=')]
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            wrapper=source[source.index("import os, pathlib, sys\nif ")+0:source.index("\nPYOWNED")]
            real=base/'nas_companion_server_real.py'
            real.write_text('_openssl_bin=lambda: "real"\ndef probe():return _openssl_bin()\n')
            namespace={'__file__':str(base/'nas_companion_server.py'),'__name__':'fixture'}
            exec(wrapper,namespace)
            self.assertEqual(namespace['probe'](),'real')
            namespace['_openssl_bin']=lambda:None
            self.assertIsNone(namespace['probe']())  # Injection must affect actual production globals.
            with subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL) as victim:
                try:
                    for mode in ['foreign','stop-failed','direct-child']:
                        work=base/mode;stage=work/'apps/nasscreencompanion';cmd=stage/'cmd';target=stage/'target'
                        cmd.mkdir(parents=True);target.mkdir()
                        (stage/'var').mkdir()
                        main=cmd/'main'; main.write_text('#!/bin/bash\necho called > "'+str(work/'called')+'"\nexit 7\n');main.chmod(0o700)
                        pre='WORK="$1"; STAGE="$2"; CMD="$3"; TARGET="$4"; FAIL=0;\n'
                        if mode=='foreign':
                            (stage/'var/app.pid').write_text(str(victim.pid))
                            body='printf "%s" "$5" > "$SERVICE_RECEIPT"; exit 0'
                        elif mode=='stop-failed':body='exit 0'
                        else:
                            main.write_text('#!/bin/bash\nexit 0\n')
                            body='sleep 60 & own=$!; DIRECT_CHILDREN="$own"; printf "%s" "$own" > "$WORK/child.pid"; FAIL=1; exit 0'
                        result=subprocess.run(['bash','-c',pre+helpers+'\n'+body,'fixture',str(work),str(stage),str(cmd),str(target),str(victim.pid)],capture_output=True,timeout=10)
                        self.assertEqual(result.returncode,1,result.stderr)
                        self.assertTrue(work.exists())
                        if mode=='foreign':
                            self.assertFalse((work/'called').exists())
                            mutant=helpers.replace('service_pid_owned "$pid" || is_direct_child "$pid" || {','true || {',1)
                            self.assertNotEqual(mutant,helpers)
                            negative=subprocess.run(['bash','-c',pre+mutant+'\n'+body,'fixture',str(work),str(stage),str(cmd),str(target),str(victim.pid)],capture_output=True,timeout=10)
                            self.assertEqual(negative.returncode,1)
                            self.assertTrue((work/'called').exists())
                        if mode=='direct-child':
                            pid=int((work/'child.pid').read_text())
                            with self.assertRaises(ProcessLookupError):os.kill(pid,0)
                        self.assertIsNone(victim.poll())
                    assert_fragment=source[source.index('assert_absent() {'):source.index('must() {')]
                    setup='PASS=0; FAIL=0; ok(){ PASS=$((PASS+1)); }; bad(){ FAIL=$((FAIL+1)); };\n'
                    result=subprocess.run(['bash','-c',setup+assert_fragment+'\nassert_absent token /definitely/missing safe; test "$FAIL" = 1 && test "$PASS" = 0; require_fingerprint "" && exit 9; require_uint "" && exit 9; exit 0'],capture_output=True,timeout=5)
                    self.assertEqual(result.returncode,0,result.stderr)
                finally:
                    victim.terminate();victim.wait(timeout=5)

    def test_empty_extraction_rejected(self):
        data = 'static const char *classify_conn_err(int n) { return "alpha"; }'
        ui = 'static const char *link_reason(int n) { if (strcmp(st->last_err, "alpha")) return "message"; }'
        self.assertEqual(self.reason(data, ui), 0)
        for left, right in (('', ''), (data, ''), ('', ui)):
            with self.subTest(left=left, right=right):
                self.assertEqual(self.reason(left, right), 1)

    def test_seen_capacity_exhaustion_rejected(self):
        source = (ROOT / 'tools/preview/preview.c').read_text()
        start = source.index('#define SEEN_MAX ')
        end = source.index('\n/*', start)
        production = source[start:end]
        program = '#include <stdbool.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <assert.h>\n' + production + r'''
static int objects[SEEN_MAX+1];
int main(int argc,char **argv) {
    (void)argv;
    for(int i=0;i<SEEN_MAX;i++) seen_add(&objects[i]);
    assert(s_seen_n==SEEN_MAX);
    for(int i=0;i<SEEN_MAX;i++) seen_add(&objects[i]);
    assert(s_seen_n==SEEN_MAX);
    if(argc>1)seen_add(&objects[SEEN_MAX]);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source_file, binary = Path(tmp)/'seen.c', Path(tmp)/'seen'
            command = ['cc','-std=c11','-UNDEBUG','-fsanitize=address,undefined',str(source_file),'-o',str(binary)]
            source_file.write_text(program)
            subprocess.run(command,check=True,capture_output=True,timeout=30)
            subprocess.run([str(binary)],check=True,capture_output=True,cwd=tmp,timeout=10)
            result = subprocess.run([str(binary),'overflow'],capture_output=True,cwd=tmp,timeout=10)
            self.assertNotEqual(result.returncode,0)
            self.assertIn(b'coverage is incomplete',result.stderr)
            mutant = program.replace('        abort();','        return;',1)
            self.assertNotEqual(mutant,program)
            source_file.write_text(mutant)
            subprocess.run(command,check=True,capture_output=True,timeout=30)
            result = subprocess.run([str(binary),'overflow'],capture_output=True,cwd=tmp,timeout=10)
            self.assertEqual(result.returncode,0)  # The old fail-open mutation is distinguishable.

    def test_serial_availability_gate(self):
        good = 'I (1) fnos_ui: ui created (pages=6)\nI (2) main: boot complete (ui=created)\n'
        self.assertTrue(serial_check.check(good)[0])
        cases = ['', 'boot complete', good.replace('created)', 'degraded)'),
                 good.replace('ui=created', 'ui=ok'), good + good,
                 good.replace('I (1) fnos_ui: ui created (pages=6)\n', ''),
                 good + 'Guru Meditation Error', good + 'Backtrace: 0x0000',
                 good + 'E (3) main: night timer start failed: failure',
                 good + 'E (3) fnos_ui: UI refresh timer allocation failed: display is stale',
                 good + 'E (3) main: display start failed', good + '\ufffd']
        for text in cases:
            with self.subTest(text=text): self.assertFalse(serial_check.check(text)[0])
        self.assertIn('boot complete', cases[2])  # old grep would falsely accept it
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory)/'serial.log'
            command = [sys.executable, str(ROOT/'tools/serial_check.py'), str(log)]
            self.assertEqual(subprocess.run(command,capture_output=True).returncode, 2)
            log.write_text(good)
            self.assertEqual(subprocess.run(command,capture_output=True).returncode, 0)
            log.write_text(cases[2])
            self.assertEqual(subprocess.run(command,capture_output=True).returncode, 1)
            log.write_bytes(b'\xff')
            self.assertEqual(subprocess.run(command,capture_output=True).returncode, 2)

    def shell_helper(self, name):
        source = (ROOT/'tools/verify_all.sh').read_text()
        start = source.index(name + '() {')
        end = source.index('\n}', start) + 2
        return source[start:end]

    def test_output_never_deletes_existing_paths(self):
        helper = self.shell_helper('prepare_output')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); sentinel = root/'keep'; sentinel.write_text('preserve')
            command = ['bash','-c',helper + '\nprepare_output "$1"','fixture',str(root)]
            one = subprocess.run(command,capture_output=True,text=True,check=True).stdout.strip()
            two = subprocess.run(command,capture_output=True,text=True,check=True).stdout.strip()
            self.assertNotEqual(one,two)
            self.assertEqual(Path(one).parent,root); self.assertTrue(Path(two).is_dir())
            self.assertEqual(sentinel.read_text(),'preserve')
            denied = subprocess.run(command[:-1]+[str(sentinel)],capture_output=True)
            self.assertNotEqual(denied.returncode,0)
            self.assertEqual(sentinel.read_text(),'preserve')

    def test_l3_capture_failure_cannot_use_partial_log(self):
        source = (ROOT/'tools/verify_all.sh').read_text()
        start = source.index('if [ "$FLASH" = 1 ]; then\n  step "L3')
        end = source.index('\nif [ "$PHOTO"',start)
        block = source[start:end]
        good = 'I (1) fnos_ui: ui created (pages=6)\nI (2) main: boot complete (ui=created)\n'
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); out=root/'out'; out.mkdir()
            (root/'idf.sh').write_text('#!/bin/sh\nexit 0\n'); (root/'idf.sh').chmod(0o700)
            fake=root/'python3'
            fake.write_text('''#!/bin/sh
case "$1" in
 tools/serial_capture.py) printf '%s' "$FIXTURE_LOG" > "$OUT/serial.log"; exit "$FIXTURE_RC" ;;
 tools/serial_check.py) touch "$OUT/check-called"; exec "$REAL_PYTHON" "$REAL_CHECK" "$2" ;;
 *) exit 99 ;;
esac
'''); fake.chmod(0o700)
            pre='fail=0; step() { :; }; pass() { :; }; bad() { fail=$((fail+1)); }; FLASH=1; PORT=anonymous\n'
            env=dict(os.environ,PATH=str(root)+os.pathsep+os.environ['PATH'],OUT=str(out),FIXTURE_LOG=good,
                     FIXTURE_RC='1',REAL_PYTHON=sys.executable,REAL_CHECK=str(ROOT/'tools/serial_check.py'))
            command=['bash','-c',pre+block+'\nexit "$fail"']
            self.assertEqual(subprocess.run(command,cwd=root,env=env,capture_output=True).returncode,1)
            self.assertFalse((out/'check-called').exists())
            # Ignore capture failure while retaining its plausible partial log: old false-green mechanism.
            mutant=block.replace('>"$OUT/serial_capture.log" 2>&1; then','>"$OUT/serial_capture.log" 2>&1; true; then')
            self.assertNotEqual(mutant,block)
            self.assertEqual(subprocess.run(['bash','-c',pre+mutant+'\nexit "$fail"'],cwd=root,env=env,capture_output=True).returncode,0)
            self.assertTrue((out/'check-called').exists())
            self.assertEqual(subprocess.run(command,cwd=root,env=dict(env,FIXTURE_RC='0'),capture_output=True).returncode,0)
            self.assertEqual(subprocess.run(command,cwd=root,env=dict(env,FIXTURE_RC='0',FIXTURE_LOG=good.replace('ui=created','ui=degraded')),capture_output=True).returncode,1)

    def test_photo_file_decode_and_failures(self):
        helper = self.shell_helper('capture_board_photo')
        def chunk(kind, data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); out = root/'out'; out.mkdir()
            image = root/'fixture.png'
            stub = root/'capture'; stub.write_text('#!/bin/sh\nprintf "%s\\n" "$FIXTURE_JSON"\nexit "${FIXTURE_RC:-0}"\n'); stub.chmod(0o700)
            env = dict(os.environ,FNOS_CAPTURE_BOARD=str(stub),FIXTURE_JSON=json.dumps({'image':str(image)}))
            command = ['bash','-c',helper+'\ncapture_board_photo "$1"','fixture',str(out)]
            image.write_bytes(b'\x89PNG\r\n\x1a\n')
            self.assertNotEqual(subprocess.run(command,env=env,cwd=ROOT,capture_output=True).returncode,0)
            image.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0\0\0\0'))+chunk(b'IEND',b''))
            self.assertEqual(subprocess.run(command,env=env,cwd=ROOT,capture_output=True).returncode,0)
            for rc, body in (('1',env['FIXTURE_JSON']),('0','broken'),('0','{}')):
                with self.subTest(rc=rc,body=body):
                    result = subprocess.run(command,env=dict(env,FIXTURE_RC=rc,FIXTURE_JSON=body),cwd=ROOT,capture_output=True)
                    self.assertNotEqual(result.returncode,0)

    def test_missing_preview_config_rejected(self):
        # 不碰仓库 sdkconfig 或真实配置。复制构建定义，逐一缺失输入。
        for device, host in ((False, False), (True, False), (False, True)):
            with self.subTest(device=device, host=host), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                source = root / 'tools/preview'
                source.mkdir(parents=True)
                shutil.copyfile(ROOT / 'tools/preview/CMakeLists.txt', source / 'CMakeLists.txt')
                if device:
                    config = root / 'build/config/sdkconfig.h'
                    config.parent.mkdir(parents=True)
                    config.write_text('/* anonymous fixture */\n')
                if host:
                    (source / 'lv_kconfig_host.h').write_text('/* anonymous fixture */\n')
                result = subprocess.run(['cmake', '-S', str(source), '-B', str(root / 'cmake-out')], capture_output=True, text=True, timeout=30)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Missing device/host LVGL config', result.stderr)

    def test_wifi_reason_empty_success_is_rejected(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('wifi_reason_fixture', ROOT/'tools/wifi_reason_check.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        def empty(command, **kwargs):
            return subprocess.CompletedProcess(command, 0, b'')
        with patch.object(module, 'source_at', return_value=None), patch.object(module.subprocess, 'run', side_effect=empty), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(module.main(), 1)
        # Old rc-only behavior accepts an executable that ran no assertions.
        source = (ROOT/'tools/wifi_reason_check.py').read_text()
        source = source.replace(' or "ALL OK" not in out', '')
        namespace = dict(module.__dict__); exec(compile(source, str(module.__file__), 'exec'), namespace)
        with patch.object(module.subprocess, 'run', side_effect=empty), contextlib.redirect_stdout(io.StringIO()):
            namespace['source_at'] = lambda *args: None
            self.assertEqual(namespace['main'](), 0)

    def test_preview_guard_preserves_preexisting_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); preview=root/'tools/preview';preview.mkdir(parents=True);(preview/'build').mkdir()
            for name in ('test_run_guard.sh','run.sh'):
                shutil.copyfile(ROOT/'tools/preview'/name,preview/name)
            (preview/'preview.c').write_text('getenv("PREVIEW_MOTION");\n')
            sentinels=['guard-link','guard-dirty/keep','.run-guard-probe.sh','build/guard-preview']
            for name in sentinels:
                target=preview/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text('preserve')
            fake=root/'fake';fake.mkdir();git=fake/'git';git.write_text('#!/bin/sh\nexit 0\n');git.chmod(0o700)
            env=dict(os.environ,GUARD_QUICK='1',PATH=str(fake)+os.pathsep+os.environ['PATH'])
            command=['bash',str(preview/'test_run_guard.sh')]
            result=subprocess.run(command,env=env,capture_output=True,text=True,errors="replace",timeout=30)
            self.assertEqual(result.returncode,2,result.stdout+result.stderr)
            self.assertIn("INCOMPLETE",result.stdout)
            for name in sentinels:self.assertEqual((preview/name).read_text(),'preserve')
            self.assertFalse(list(preview.glob('.guard-work.*')))
            # Historical cleanup is reproduced only in this anonymous tree.
            script=(preview/'test_run_guard.sh').read_text()
            mutant=script.replace('  rm -rf -- "$WORK" || rc=1','  rm -f "$PREVIEW_DIR/guard-link" "$PREVIEW_DIR/.run-guard-probe.sh" "$PREVIEW_DIR/build/guard-preview"\n  rm -rf "$PREVIEW_DIR/guard-dirty"\n  rm -rf -- "$WORK" || rc=1')
            self.assertNotEqual(mutant,script);(preview/'test_run_guard.sh').write_text(mutant)
            subprocess.run(command,env=env,capture_output=True,timeout=30)
            self.assertFalse((preview/'guard-dirty/keep').exists())

    def test_release_assert_gate_scan_failure_and_missing_binary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);preview=root/'tools/preview';preview.mkdir(parents=True);(preview/'build').mkdir()
            script=preview/'test_release_asserts.sh';shutil.copyfile(ROOT/'tools/preview/test_release_asserts.sh',script)
            (preview/'preview.c').write_text('int anonymous;\n')
            fake=root/'fake';fake.mkdir()
            programs={
             'cmake': '#!/bin/sh\nwhile [ "$#" -gt 0 ]; do if [ "$1" = -B ]; then shift; dest=$1; fi; shift; done\nmkdir -p "$dest/CMakeFiles/preview.dir"\nprintf "C_FLAGS = -DNDEBUG -UNDEBUG\\n" > "$dest/CMakeFiles/preview.dir/flags.make"\n',
             'nm': '#!/bin/sh\necho __assert_rtn\n',
             'python3': '#!/bin/sh\nif [ "$2" = tools/preview/preview.c ] && [ "${FAIL_SIDE:-0}" = 1 ]; then exit 1; fi\nexec "$REAL_PYTHON" "$@"\n'}
            for name,body in programs.items():
                f=fake/name;f.write_text(body);f.chmod(0o700)
            binary=preview/'build/preview';binary.write_text('#!/bin/sh\ncase "$PREVIEW_HARDWARE_FIXTURE" in *empty*) echo "FAIL: 硬件夹具是空的" ;; *) echo "FAIL: 硬件夹具" ;; esac\nexit 1\n');binary.chmod(0o700)
            env=dict(os.environ,PATH=str(fake)+os.pathsep+os.environ['PATH'],REAL_PYTHON=sys.executable)
            command=['bash',str(script)]
            good=subprocess.run(command,env=env,capture_output=True,text=True,errors="replace",timeout=30)
            self.assertEqual(good.returncode,0,good.stdout+good.stderr)
            bad=subprocess.run(command,env=dict(env,FAIL_SIDE='1'),capture_output=True,text=True,errors="replace",timeout=30)
            self.assertEqual(bad.returncode,1,bad.stdout+bad.stderr)
            self.assertIn('assert 扫描失败',bad.stdout,bad.stdout+bad.stderr)
            production=script.read_text();a=production.index('if [ "$side_rc" -ne 0 ]; then');b=production.index('\nfi',a)+3
            script.write_text(production[:a]+production[b:])
            old=subprocess.run(command,env=dict(env,FAIL_SIDE='1'),capture_output=True,text=True,errors="replace",timeout=30)
            self.assertEqual(old.returncode,0,old.stdout+old.stderr)
            script.write_text(production);binary.unlink()
            missing=subprocess.run(command,env=env,capture_output=True,text=True,errors="replace",timeout=30)
            self.assertEqual(missing.returncode,2,missing.stdout+missing.stderr)
            self.assertIn('INCOMPLETE',missing.stdout)

    def test_process_harness_setup_failure_and_cleanup_ownership(self):
        source=(ROOT/'nas/fpk/test_process_lifecycle.sh').read_text()
        prefix=source.split('# ---------------------------------------------------------------- S1 停不下来',1)[0]
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);folder=root/'nas/fpk';folder.mkdir(parents=True)
            (folder/'nasscreencompanion/cmd').mkdir(parents=True)
            script=folder/'test_process_lifecycle.sh'
            fake=root/'fake';fake.mkdir();cp=fake/'cp'
            cp.write_text('#!/bin/sh\nexit 7\n');cp.chmod(0o700)
            env=dict(os.environ,PATH=str(fake)+os.pathsep+os.environ['PATH'])
            script.write_text(prefix+'\nif setup_pkg "$WORK/setup"; then exit 0; else exit 7; fi\n')
            command=['bash',str(script)]
            result=subprocess.run(command,env=env,capture_output=True,timeout=10)
            self.assertEqual(result.returncode,7,result.stdout+result.stderr)
            mutant=prefix.replace('cp -R "$PKG/cmd" "$dir/cmd" || return 1',
                                  'cp -R "$PKG/cmd" "$dir/cmd"')
            self.assertNotEqual(mutant,prefix)
            script.write_text(mutant+'\nif setup_pkg "$WORK/setup"; then exit 0; else exit 7; fi\n')
            old=subprocess.run(command,env=env,capture_output=True,timeout=10)
            self.assertEqual(old.returncode,0,old.stdout+old.stderr)
            # PID file content is deliberately another child owned by this Python test,
            # never a user process. The harness must not adopt it as its own victim.
            child=subprocess.Popen(['sleep','60'])
            try:
                script.write_text(prefix+'\nmkdir -p "$WORK/rogue/var"\nprintf "%s\\n" "$FOREIGN_FIXTURE_PID" > "$WORK/rogue/var/app.pid"\nexit 0\n')
                result=subprocess.run(command,env=dict(os.environ,FOREIGN_FIXTURE_PID=str(child.pid)),
                                      capture_output=True,timeout=10)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIsNone(child.poll())
            finally:
                if child.poll() is None:child.kill()
                child.wait(timeout=5)

    def test_tls_accept_harness_failures_and_wrapped_socket_ownership(self):
        import importlib.util, inspect, textwrap
        from unittest.mock import Mock
        spec=importlib.util.spec_from_file_location('tls_gate_fixture',ROOT/'nas/fpk/tls_accept_check.py')
        gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
        raw,wrapped,ctx=Mock(),Mock(),Mock()
        wrapped.recv.return_value=b'HTTP/1.0 200 OK\r\n\r\n'
        ctx.wrap_socket.return_value=wrapped
        with patch.object(gate,'connect',return_value=raw):
            self.assertTrue(gate.http_get('fixture',0,True,ctx)[0])
        wrapped.close.assert_called_once();raw.close.assert_called_once()
        source=textwrap.dedent(inspect.getsource(gate.http_get))
        old=source.replace('for owned in ((s,) if s is raw else (s, raw)):', 'for owned in (raw,):')
        self.assertNotEqual(old,source)
        ns=dict(gate.__dict__);exec(old,ns)
        wrapped.close.reset_mock()
        with patch.dict(ns,connect=Mock(return_value=raw)):
            self.assertTrue(ns['http_get']('fixture',0,True,ctx)[0])
        with self.assertRaises(AssertionError):wrapped.close.assert_called_once()
        for error in (FileNotFoundError(),subprocess.TimeoutExpired('openssl',20)):
            with patch.object(gate.subprocess,'run',side_effect=error):
                self.assertEqual(gate.make_cert('/unused-fixture'),(None,None))
        srv=Mock();srv._accept_test_thread=None
        srv.server_close.side_effect=OSError('fixture close')
        with self.assertRaises(OSError):gate.stop_server(srv)
        srv.server_close.side_effect=None;srv.shutdown.side_effect=OSError('fixture shutdown')
        with self.assertRaises(OSError):gate.stop_server(srv)
        module=Mock();module.ThreadedHTTPServer.return_value=Mock()
        with patch.object(gate.threading,'Thread',side_effect=RuntimeError('fixture start')):
            with self.assertRaises(RuntimeError):gate.start_server(module,ctx)
        module.ThreadedHTTPServer.return_value.server_close.assert_called_once()
        wrapped.close.side_effect=OSError('fixture close')
        raw.close.reset_mock()
        with patch.object(gate,'connect',return_value=raw):
            self.assertFalse(gate.http_get('fixture',0,True,ctx)[0])
        raw.close.assert_called_once();wrapped.close.side_effect=None
        # Empty/incorrect status lines cannot borrow a 200 substring as success.
        for status in (b'HTTP/1.0 2009 Nope\r\n\r\n',b'garbage 200\r\n\r\n'):
            raw.recv.return_value=status
            with patch.object(gate,'connect',return_value=raw):
                self.assertFalse(gate.http_get('fixture',0,False)[0])

    def test_preview_output_cannot_delete_sources(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);preview=root/'tools/preview';preview.mkdir(parents=True)
            script=preview/'run.sh';shutil.copyfile(ROOT/'tools/preview/run.sh',script)
            source=preview/'preview.c';source.write_text('source sentinel')
            folder=preview/'source-dir';folder.mkdir();(folder/'keep').write_text('keep')
            for output in ('preview.c','source-dir'):
                result=subprocess.run(['bash',str(script),output],capture_output=True,timeout=10)
                self.assertNotEqual(result.returncode,0)
                self.assertTrue(source.is_file());self.assertEqual((folder/'keep').read_text(),'keep')
                expected='输出目标是现有文件' if output=='preview.c' else '输出目录非空'
                self.assertIn(expected.encode(),result.stderr) # refusal must come from the ownership gate
            production=script.read_text();start=production.index('python3 - "$OUT_REAL"');end=production.index("\nPYOUT",start)+len('\nPYOUT')
            script.write_text(production[:start]+production[end:])
            old=subprocess.run(['bash',str(script),'preview.c'],capture_output=True,timeout=10)
            self.assertNotIn('输出目标是现有文件'.encode(),old.stderr)
            script.write_text(production)
            # An owned output may proceed to the independent config/build gates.
            owned=preview/'owned';owned.mkdir();(owned/'.fnos-preview-output').write_text('fnos-preview-output-v1\n')
            (owned/'image').write_text('old result')
            # Evaluate only the actual ownership preflight, avoiding a fake build.
            text=script.read_text();start=text.index('python3 - "$OUT_REAL"');end=text.index("\nPYOUT",start)+len('\nPYOUT')
            check=text[start:end]
            import shlex
            good=subprocess.run(['bash','-c','OUT_REAL='+shlex.quote(str(owned))+'\n'+check],capture_output=True,timeout=10)
            self.assertEqual(good.returncode,0,good.stderr)
            (owned/'.fnos-preview-output').unlink();(owned/'.fnos-preview-output').symlink_to(source)
            bad=subprocess.run(['bash','-c','OUT_REAL='+shlex.quote(str(owned))+'\n'+check],capture_output=True,timeout=10)
            self.assertNotEqual(bad.returncode,0)

    def test_scroll_png_validation_and_inflate_budget(self):
        script=ROOT/'tools/preview/scroll_gap.js'
        import binascii
        def chunk(kind,data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',binascii.crc32(kind+data)&0xffffffff)
        signature=b'\x89PNG\r\n\x1a\n'
        def png(raw=b'\0\x20\x30\x40',extra=b'',header=None):
            ihdr=header or struct.pack('>IIBBBBB',1,1,8,2,0,0,0)
            return signature+chunk(b'IHDR',ihdr)+chunk(b'IDAT',zlib.compress(raw)+extra)+chunk(b'IEND',b'')
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);image=root/'fixture.png'
            js='require(process.argv[1]).readPNG(process.argv[2]);'
            def read(data,tool=script,code=js):
                image.write_bytes(data)
                return subprocess.run(['node','-e',code,str(tool),str(image)],capture_output=True,timeout=10)
            self.assertEqual(read(png()).returncode,0)
            bads=[b'bad-sign'+png()[8:],png()[:-1],png()+b'extra',png(raw=b'\0\x20'),
                  png(raw=b'\5\x20\x30\x40'),png(extra=b'extra'),png(raw=b'x'*10000),
                  png(header=struct.pack('>IIBBBBB',100000,100000,8,2,0,0,0))]
            corrupt=bytearray(png());corrupt[-1]^=1;bads.append(bytes(corrupt))
            for data in bads:self.assertNotEqual(read(data).returncode,0)
            # Verify the bound reaches zlib, instead of relying only on a late length check.
            guard='const z=require("zlib"),real=z.inflateSync;z.inflateSync=(b,o)=>{if(!o||o.maxOutputLength!==5)throw Error("missing inflate budget");return real(b,o)};'+js
            self.assertEqual(read(png(),code=guard).returncode,0)
            text=script.read_text();old=text.replace('maxOutputLength: expected + 1, info: true','info: true')
            self.assertNotEqual(old,text);mutant=root/'mutant.js';mutant.write_text(old)
            result=read(png(),tool=mutant,code=guard)
            self.assertNotEqual(result.returncode,0);self.assertIn(b'missing inflate budget',result.stderr)

class PageGateRegression(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='fnos-page-gate-')
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)

    def run_cmd(self,*args):
        return subprocess.run(args,cwd=self.root,text=True,stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT,errors='replace',timeout=30)

    def test_page_visible_cpu_and_revoke(self):
        state = dict(ver='fixture-version', uid=1000,
            config=dict(port=8798, interval=3, auth_mode='pairing', bind='127.0.0.1',
                        hist_len=600, docker_enabled=False, tls_enabled=True),
            tls=dict(configured=True, enabled=True, fingerprint='AA:BB:CC:DD', subject='fixture'),
            devices=[], listen=dict(tcp='fixture:8798', socket='/fixture/app.sock'),
            collector=dict(ready=True, age_s=1, seq=3),
            paths=dict(appdest='/fixture/app', pkgetc='/fixture/etc', pkgvar='/fixture/var'))
        snapshot = dict(cpu=dict(pct=2.2, cores=4), mem=dict(total_mb=8192),
            net={'if': 'fixture0'}, host='fixture-host', modules={'cpu': dict(status='ok')})
        responses = {'GET /api/state': state, 'GET /api/status-now': snapshot,
            'GET /api/probe': dict(uid=1000, sources=[dict(path='/fixture/source', status='ok')],
                                  modules={'cpu': dict(status='ok')}),
            'GET /api/diagnostics': dict(checks=[dict(name='fixture-check', status='ok', detail='fixture-detail')]),
            'GET /api/logs': dict(lines=['fixture-log']),
            'POST /api/pairing/new': dict(ok=True, code='123456', ttl=3600),
            'POST /api/pairing/cancel': dict(ok=True, cancelled=True),
            'POST /api/config': dict(ok=True, changed=[], restart_required=[]),
            'POST /api/collector/docker': dict(ok=True, docker_enabled=False),
            'POST /api/devices/revoke': dict(ok=True, removed=0)}
        fixture = self.root / 'responses.json'
        fixture.write_text(json.dumps(responses))
        html = (ROOT / 'nas/fpk/nasscreencompanion/app/ui/www/index.html').read_text()
        target = self.root / 'index.html'
        args = ('node', str(ROOT / 'nas/fpk/page_check.js'), str(target), str(fixture))
        target.write_text(html)
        baseline = self.run_cmd(*args)
        self.assertEqual(baseline.returncode, 0, baseline.stdout)
        for broken, message in (
                (html.replace("num(cpu.pct,1)+'<span", "'0.0'+'<span", 1), 'CPU 占用'),
                (html.replace('{id: b.dataset.revoke}', "{id: 'wrong'}", 1), '设备 ID'),
                (html.replace("s.id==='s-'+b.dataset.t", 'false', 1), '目标页面未唯一激活'),
                (html.replace("'seq ' + (c.seq||0)", "'seq 0'", 1), '采样序号')):
            self.assertNotEqual(broken, html)
            target.write_text(broken)
            result = self.run_cmd(*args)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(message, result.stdout)


        target.write_text(html)
        for sequence in [None, 0, -1, 1.5, True, '3']:
            responses['GET /api/state']['collector']['seq'] = sequence
            fixture.write_text(json.dumps(responses))
            result = self.run_cmd(*args)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn('样本须含正整数 seq', result.stdout)

class MotionPngGateRegression(unittest.TestCase):
    def test_motion_png_decode(self):
        with tempfile.TemporaryDirectory(prefix='fnos-motion-png-') as directory:
            root = Path(directory)
            frame = root/'frame.png'
            (root/'motion.csv').write_text('file,time_ms,page,phase\nframe.png,0,0,idle\n')
            def chunk(kind, body):
                return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
            valid = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0\x10\x20\x30'))+chunk(b'IEND',b'')
            command = [sys.executable, str(ROOT/'tools/preview/motion_view.py'), str(root)]
            frame.write_bytes(valid)
            result = subprocess.run(command,capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)
            receipt=(root/'index.html').read_bytes()
            for bad in [valid[:8], valid[:-5], valid[:-1]+b'x', valid+b'tail']:
                frame.write_bytes(bad)
                result=subprocess.run(command,capture_output=True,timeout=30)
                self.assertNotEqual(result.returncode,0,result.stdout)
                self.assertEqual((root/'index.html').read_bytes(),receipt)
            # Exact old signature-only helper accepts the truncated frame.
            source=(ROOT/'tools/preview/motion_view.py').read_text()
            start=source.index('def check_png('); end=source.index('\n\ndef main(',start)
            old=source[:start]+"def check_png(path):\n    assert path.read_bytes()[:8] == b'\\x89PNG\\r\\n\\x1a\\n'\n"+source[end:]
            mutant=root/'old.py'
            # Preserve the production import root for this temporary old-helper control.
            old=old.replace('str(Path(__file__).resolve().parents[1])',repr(str(ROOT/'tools')))
            mutant.write_text(old)
            frame.write_bytes(valid[:8])
            result=subprocess.run([sys.executable,str(mutant),str(root)],capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)


if __name__ == '__main__':
    unittest.main()
