#!/usr/bin/env python3
"""Production history parser boundary checks; locks are host stubs, no devices."""
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
CASES=[
 ('normal', [[1700000000,1,2,3,4],[1700000002,2,3,4,5]],2,1,2,20),
 ('rejected-row-time',[[1700000000,'bad',2,3,4],[1700000010,1,2,3,4],[1700000012,2,3,4,5]],2,1,2,20),
 ('span-overflow',[[1000000001,1,2,3,4],[3999999999,2,3,4,5]],2,0,0,0),
 ('gap-overflow',[[1700000000,1,2,3,4],[2000000000,2,3,4,5]],2,0,0,0),
 ('reversed',[[1700000002,1,2,3,4],[1700000000,2,3,4,5]],2,0,0,0),
 ('duplicate',[[1700000000,1,2,3,4],[1700000000,2,3,4,5]],2,0,0,0),
 ('bad-time',[[1700000000,1,2,3,4],[0,2,3,4,5],[1700000002,3,4,5,6]],3,0,0,0),
 ('float-overflow',[[1700000000,1e100,2,3,4],[1700000002,2,3,4,5]],1,0,0,0),
 ('empty',[],0,0,0,0),
 ('object-rows',{'row':[1700000000,1,2,3,4]},0,0,0,0),
 ('object-row',[{'ts':1700000000,'cpu':1,'mem':2,'rx':3,'tx':4}],0,0,0,0),
 ('short-rows',[[],[1700000000],[1700000000,1],[1700000000,1,2],[1700000000,1,2,3]],0,0,0,0),
 ('invalid-metrics',[[1700000000,None,2,3,4],[1700000000,1,False,3,4],[1700000000,1,2,{},4],[1700000000,1,2,3,[]]],0,0,0,0),
 ('extra-columns',[[1700000000,1,2,3,4,42,'ignored'],[1700000002,2,3,4,5,None,{}]],2,1,2,20),
]
def run():
 with tempfile.TemporaryDirectory(prefix='fnos-history-') as tmp:
  out=Path(tmp)
  subprocess.run([sys.executable,str(ROOT/'tools/parsecheck/extract.py'),str(ROOT/'components/fnos_monitor/fnos_data.c'),str(out/'production.c')],check=True,capture_output=True)
  (out/'main.c').write_text('''#include <assert.h>
#include "production.c"
int main(int argc,char **argv) {
 assert(argc==6);
 bool ok=parse_history(argv[1]);
 int n=atoi(argv[2]);
 assert(ok==(n>0)); assert(s_seq==n);
 assert(s_status.hist_ts_ok==atoi(argv[3]));
 assert(s_status.hist_span_s==atoi(argv[4]));
 assert(s_status.hist_avg_gap_x10==atoi(argv[5]));
 return 0;
}
''')
  cj=ROOT/'managed_components/espressif__cjson/cJSON'
  subprocess.run(['cc','-std=c11','-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize-recover=all','-g','-I'+str(ROOT/'build/config'),'-I'+str(ROOT/'components/fnos_monitor'),'-I'+str(cj),str(out/'main.c'),str(cj/'cJSON.c'),'-o',str(out/'test')],check=True,capture_output=True)
  for name,rows,n,valid,span,gap in CASES:
   result=subprocess.run([str(out/'test'),json.dumps({'rows':rows}),str(n),str(valid),str(span),str(gap)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=10)
   if result.returncode:raise RuntimeError(name+' failed: '+result.stderr)
  utf_source=(ROOT/'tools/parsecheck/main.c').read_text()
  utf=re.search(r'^static bool utf8_ok\(.*?^}',utf_source,re.M|re.S).group(0)
  (out/'utf.c').write_text('#include <stdbool.h>\n#include <assert.h>\n'+utf+r'''
int main(void) {
 assert(utf8_ok("ASCII")); assert(utf8_ok("\xE4\xB8\xAD\xF0\x9F\x98\x80"));
 assert(!utf8_ok("\xC0\x80")); assert(!utf8_ok("\xE0\x80\x80"));
 assert(!utf8_ok("\xED\xA0\x80")); assert(!utf8_ok("\xF4\x90\x80\x80"));
 assert(!utf8_ok("\xF5\x80\x80\x80")); assert(!utf8_ok("\xE2"));
 return 0;
}
''')
  subprocess.run(['cc','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'utf.c'),'-o',str(out/'utf')],check=True,capture_output=True)
  subprocess.run([str(out/'utf')],check=True,capture_output=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=10)
  ui_source=(ROOT/'components/fnos_monitor/fnos_ui.c').read_text()
  span=re.search(r'^static const char \*hist_span_text\(.*?^}',ui_source,re.M|re.S).group(0)
  span_main=r'''
int main(void) {
 fnos_status_t st={0}; char b[128]; st.hist_ts_ok=true; st.hist_span_s=1; st.hist_avg_gap_x10=INT_MAX;
 hist_span_text(b,sizeof b,&st); assert(strstr(b,"小时") && !strchr(b,'-'));
 st.hist_avg_gap_x10=10; hist_span_text(b,sizeof b,&st); assert(strstr(b,"分钟"));
 st.hist_ts_ok=false; hist_span_text(b,sizeof b,&st); assert(strstr(b,"次采集")); return 0;
}
'''
  prefix='#include <stdio.h>\n#include <string.h>\n#include <stdint.h>\n#include <limits.h>\n#include <assert.h>\n#include "fnos_data.h"\n#define UI_HIST 180\n'
  for negative in (False,True):
   candidate=span if not negative else span.replace('int64_t sec =','int sec =')
   (out/'span.c').write_text(prefix+candidate+span_main)
   subprocess.run(['cc','-fsanitize=address,undefined','-I'+str(ROOT/'components/fnos_monitor'),'-I'+str(ROOT/'build/config'),str(out/'span.c'),'-o',str(out/'span')],check=True)
   result=subprocess.run([str(out/'span')],capture_output=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=10)
   if negative: assert result.returncode!=0 and b'strstr(b' in result.stderr,'UI narrowing negative control must reject the wrapped duration'
   else: assert result.returncode==0,result.stderr
  # Remove timestamp order guard; duplicate timestamps with a later valid row
  # would then receive plausible but invalid metadata.
  source=(out/'production.c').read_text()
  old=source.replace('ts<=ts_last','ts<ts_last')
  assert old!=source
  (out/'production.c').write_text(old)
  subprocess.run(['cc','-std=c11','-I'+str(ROOT/'build/config'),'-I'+str(ROOT/'components/fnos_monitor'),'-I'+str(cj),str(out/'main.c'),str(cj/'cJSON.c'),'-o',str(out/'negative')],check=True,capture_output=True)
  rows=[[1700000000,1,2,3,4],[1700000000,2,3,4,5],[1700000002,3,4,5,6]]
  result=subprocess.run([str(out/'negative'),json.dumps({'rows':rows}),'3','0','0','0'],capture_output=True,timeout=10)
  assert result.returncode!=0 and b'hist_ts_ok' in result.stderr,'duplicate-time negative control must hit the metadata assertion'
 print(f'PASS history: {len(CASES)} production cases ASan/UBSan, timestamp-order negative control, UTF8 scalar boundaries, UI span INT_MAX and narrowing negative control')
if __name__=='__main__':run()
