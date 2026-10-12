#!/usr/bin/env python3
"""Production history merge/cursor tests. Anonymous input, actual pthread lock, no device."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
MAIN=r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include "production.c"
static void live(int value,int64_t ts) {
 fnos_status_t st={0}; st.source_ts=ts; st.cpu.pct=st.mem.pct=st.net.rx_kbs=st.net.tx_kbs=value;
 commit_status(&st,1,200);
}
static const char *history="{\"rows\":[[1700000008,80,80,80,80],[1700000009,90,90,90,90],[1700000010,999,999,999,999],[1700000012,999,999,999,999]]}";
static atomic_bool done;
static void *writer(void *arg) {
 (void)arg; for(int i=0;i<2000;i++)live(i,1700000100+i);
 atomic_store(&done,true); return NULL;
}
int main(int argc,char **argv) {
 assert(argc==2); fnos_sample_t out[FNOS_HIST_MAX]; int64_t next=0; bool reset=false;
 if(!strcmp(argv[1],"ui")) {
  live(7,1700000010);ui_cursor=fnos_data_hist_seq()+100;history_reset();
  ui_history_tick();assert(ui_clears==4 && ui_cursor==fnos_data_hist_seq());
  live(100,1700000010);assert(parse_history(history));
  ui_clears=0;ui_history_tick();assert(ui_clears==4 && ui_cursor==fnos_data_hist_seq());
  ui_clears=0;ui_history_tick();assert(ui_clears==0);
 } else if(!strcmp(argv[1],"merge") || !strcmp(argv[1],"oom")) {
  live(100,1700000010);live(101,1700000011);int64_t cursor=fnos_data_hist_seq();assert(cursor==2);
  if(!strcmp(argv[1],"oom")) {
   assert(!parse_history(history));assert(fnos_data_hist_seq()==cursor);
   assert(fnos_data_hist_read_ex(0,out,FNOS_HIST_MAX,&next,&reset)==2 && out[0].cpu==100 && out[1].cpu==101);return 0;
  }
  assert(parse_history(history));assert(fnos_data_hist_seq()==cursor+FNOS_HIST_MAX+4);
  assert(fnos_data_hist_read_ex(cursor,out,2,&next,&reset)==2 && reset);
  assert(out[0].cpu==80 && out[1].cpu==90);
  assert(fnos_data_hist_read_ex(next,out,2,&next,&reset)==2 && !reset);
  assert(out[0].cpu==100 && out[1].cpu==101);
  assert(s_status.hist_ts_ok && s_status.hist_span_s==3 && s_status.hist_avg_gap_x10==10);
  cursor=next;assert(parse_history(history));assert(fnos_data_hist_seq()==cursor);
 } else if(!strcmp(argv[1],"reset")) {
  live(5,1700000010);int64_t cursor=fnos_data_hist_seq();history_reset();
  assert(fnos_data_hist_read_ex(cursor,out,FNOS_HIST_MAX,&next,&reset)==0 && reset && next==cursor);
  live(100,1700000010);assert(parse_history(history));
  assert(fnos_data_hist_read_ex(cursor,out,FNOS_HIST_MAX,&next,&reset)==3 && reset);
  assert(out[0].cpu==80 && out[1].cpu==90 && out[2].cpu==100);
 } else if(!strcmp(argv[1],"full")) {
  for(int i=0;i<FNOS_HIST_MAX;i++)live(i,1700000010+i);
  int64_t cursor=fnos_data_hist_seq();assert(parse_history(history));assert(fnos_data_hist_seq()==cursor);
  assert(fnos_data_hist_read_ex(0,out,FNOS_HIST_MAX,&next,&reset)==FNOS_HIST_MAX && out[0].cpu==0 && out[FNOS_HIST_MAX-1].cpu==FNOS_HIST_MAX-1);
 } else if(!strcmp(argv[1],"unknown-time")) {
  live(7,0);assert(parse_history(history));assert(fnos_data_hist_read_ex(0,out,FNOS_HIST_MAX,&next,&reset)==1 && out[0].cpu==7 && !s_status.hist_ts_ok);
 } else if(!strcmp(argv[1],"bad-order")) {
  live(7,1700000010);assert(!parse_history("{\"rows\":[[1700000009,1,1,1,1],[1700000008,2,2,2,2]]}"));
  assert(fnos_data_hist_seq()==1 && !s_hist_backfilled);
 } else if(!strcmp(argv[1],"capacity")) {
  for(int i=0;i<FNOS_HIST_MAX-2;i++)live(100+i,1700000010+i);
  assert(parse_history("{\"rows\":[[1700000001,1,1,1,1],[1700000002,2,2,2,2],[1700000003,3,3,3,3]]}"));
  assert(fnos_data_hist_read_ex(0,out,FNOS_HIST_MAX,&next,&reset)==FNOS_HIST_MAX && reset);
  assert(out[0].cpu==2 && out[1].cpu==3 && out[2].cpu==100 && out[FNOS_HIST_MAX-1].cpu==100+FNOS_HIST_MAX-3);
 } else if(!strcmp(argv[1],"lock")) {
  s_lock=0;assert(fnos_data_hist_seq()==-1);next=7;reset=true;
  assert(fnos_data_hist_read_ex(7,out,4,&next,&reset)==0 && next==7 && !reset);
 } else if(!strcmp(argv[1],"concurrent")) {
  pthread_t thread;assert(!pthread_create(&thread,NULL,writer,NULL));
  do {
   int n=fnos_data_hist_read_ex(next,out,FNOS_HIST_MAX,&next,&reset);
   for(int i=0;i<n;i++)assert(out[i].cpu==out[i].mem && out[i].cpu==out[i].rx_kbs && out[i].cpu==out[i].tx_kbs);
   assert(fnos_data_hist_seq()>=0);
  } while(!atomic_load(&done));
  assert(!pthread_join(thread,NULL));assert(fnos_data_hist_seq()==2000);
 } else assert(!"unknown fixture");
 return 0;
}
'''
def run():
 with tempfile.TemporaryDirectory(prefix='fnos-history-merge-') as d:
  p=Path(d)
  subprocess.run([sys.executable,str(ROOT/'tools/parsecheck/extract.py'),str(ROOT/'components/fnos_monitor/fnos_data.c'),str(p/'production.c')],check=True,capture_output=True)
  source=(p/'production.c').read_text()
  source=source.replace('#define xSemaphoreTake(l, t) ((l) ? pdTRUE : 0)','#include <pthread.h>\nstatic pthread_mutex_t history_mutex=PTHREAD_MUTEX_INITIALIZER;\n#define xSemaphoreTake(l,t) ((l) ? (pthread_mutex_lock(&history_mutex)==0) : 0)')
  source=source.replace('#define xSemaphoreGive(l) ((void)0)','#define xSemaphoreGive(l) ((void)pthread_mutex_unlock(&history_mutex))')
  (p/'production.c').write_text(source)
  ui=(ROOT/'components/fnos_monitor/fnos_ui.c').read_text()
  start=ui.index('    static fnos_sample_t smp[64];',ui.index('static void ui_tick('))
  end=ui.index('    bool live[]',start)
  block=ui[start:end].replace('s_seq','ui_cursor')
  advance=next(line.strip() for line in ui[end:].splitlines() if 's_seq = next;' in line).replace('s_seq','ui_cursor')
  ui_fixture=r'''
static int64_t ui_cursor;
static int ui_clears;
static struct { bool ever_ok; } s_st={true};
static struct { struct { int chart; } overview_resource[2]; int overview_net,tr_net; } s_ui;
static bool s_overview_gap[3];
static void chart_clear(int chart,int channels) { (void)chart; (void)channels; ui_clears++; }
static void ui_history_tick(void) {
'''+block+advance+'\n}\n'
  main=MAIN.replace('static void live(',ui_fixture+'\nstatic void live(',1)
  (p/'test.c').write_text(main)
  cj=ROOT/'managed_components/espressif__cjson/cJSON'
  cmd=['cc','-std=c11','-pthread','-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize-recover=all','-g','-I'+str(ROOT/'build/config'),'-I'+str(ROOT/'components/fnos_monitor'),'-I'+str(cj),str(p/'test.c'),str(cj/'cJSON.c'),'-o',str(p/'test')]
  subprocess.run(cmd,check=True,capture_output=True)
  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
  for case in ('ui','merge','reset','full','unknown-time','bad-order','capacity','lock','concurrent'):
   result=subprocess.run([str(p/'test'),case],capture_output=True,env=env,timeout=20)
   assert result.returncode==0,(case,result.stderr)
  # Fault only this allocation function; leave parser and snapshot allocations real.
  oom=source.replace('return malloc(n);','return NULL;',1);assert oom!=source
  (p/'production.c').write_text(oom);subprocess.run(cmd,check=True,capture_output=True)
  subprocess.run([str(p/'test'),'oom'],check=True,capture_output=True,env=env,timeout=20)
  # A missing invalidation marker must fail the consumed-cursor replay assertion.
  old=source.replace('s_hist_reset_seq=s_seq;','s_hist_reset_seq=-1;')
  assert old!=source;(p/'production.c').write_text(old);subprocess.run(cmd,check=True,capture_output=True)
  result=subprocess.run([str(p/'test'),'reset'],capture_output=True,env=env,timeout=20)
  assert result.returncode!=0 and b'reset && next==cursor' in result.stderr,'reset negative control must hit empty-batch invalidation'
  (p/'production.c').write_text(source)
  mutant=main.replace('if (n > 0 || history_reset) ui_cursor = next;','if (n > 0) ui_cursor = next;')
  assert mutant!=main;(p/'test.c').write_text(mutant);subprocess.run(cmd,check=True,capture_output=True)
  result=subprocess.run([str(p/'test'),'ui'],capture_output=True,env=env,timeout=20)
  assert result.returncode!=0 and b'ui_cursor==fnos_data_hist_seq()' in result.stderr,'UI empty-batch cursor negative control must fail'
 print('PASS history merge: 9 production scenarios, real pthread mutex, allocation failure, reset and UI empty-batch negative controls; ASan/UBSan')
def chart_test():
 ui=(ROOT/'components/fnos_monitor/fnos_ui.c').read_text()
 start=ui.index('    for (int i = 0; i < n; i++) {',ui.index('static void ui_tick('))
 end=ui.index('    if (n > 0 || history_reset)',start)
 block=ui[start:end]
 helper=''
 if 'static int32_t history_chart_value(' in ui:
  a=ui.index('static int32_t history_chart_value(');b=ui.index('\n}',a)+2;helper=ui[a:b]
 pre=r"""
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#define LV_CHART_POINT_NONE INT32_MAX
typedef int lv_obj_t;
static struct { struct {lv_obj_t *chart;} overview_resource[2]; lv_obj_t *overview_net,*tr_net; } s_ui;
static bool s_overview_gap[3];
static int32_t points[6];static int point_count;
static void uk_trend_push(lv_obj_t *c,int32_t series,int32_t v) {(void)c;(void)series;assert(point_count<6);points[point_count++]=v;}
struct sample {float cpu,mem,rx_kbs,tx_kbs;};
"""
 main=r"""
static void draw(float v) {
 struct sample smp[1]={{v,v,v,v}};int n=1;bool live[3]={true,true,true};point_count=0;
"""+block+r"""
 assert(point_count==6);
}
int main(void) {
 float invalid[]={FLT_MAX,-FLT_MAX,INFINITY,-INFINITY,NAN,0x1p31f,nextafterf(-0x1p31f,-INFINITY)};
 for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++) {
  draw(invalid[i]);for(int j=0;j<6;j++)assert(points[j]==LV_CHART_POINT_NONE);
 }
 float valid[]={0,12.75f,-12.75f,-0x1p31f,nextafterf(0x1p31f,0),FLT_MIN};
 int32_t expected[]={0,12,-12,INT32_MIN,2147483520,0};
 for(unsigned i=0;i<sizeof valid/sizeof valid[0];i++) {
  draw(valid[i]);for(int j=0;j<6;j++)assert(points[j]==expected[i]);
 }
 return 0;
}
"""
 with tempfile.TemporaryDirectory(prefix='fnos-chart-values-') as d:
  p=Path(d);src=p/'test.c';exe=p/'test'
  cmd=['cc','-std=c11','-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize-recover=all',str(src),'-o',str(exe)]
  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
  src.write_text(pre+helper+main);subprocess.run(cmd,check=True,capture_output=True)
  result=subprocess.run([str(exe)],capture_output=True,env=env,timeout=10)
  assert result.returncode==0,result.stderr.decode(errors='replace')
  if helper:
   mutant=main.replace('history_chart_value(', '(int32_t)(')
   assert mutant!=main;src.write_text(pre+helper+mutant);subprocess.run(cmd,check=True,capture_output=True)
   result=subprocess.run([str(exe)],capture_output=True,env=env,timeout=10)
   assert result.returncode!=0 and b'outside the range of representable values' in result.stderr,result.stderr
 print('PASS production ui_tick six chart sinks: finite boundary/nonfinite gaps, fractional truncation; old casts rejected by float-cast-overflow')
def range_test():
 ui=(ROOT/'components/fnos_monitor/fnos_ui.c').read_text()
 def piece(a,b):
  start=ui.index(a);return ui[start:ui.index(b,start)]
 overview=piece('        float top = chart_peak(s_ui.overview_net, 0);','        if (!net_live)') if '        float top = chart_peak(s_ui.overview_net, 0);' in ui else piece('        int32_t ymax = network_chart_max(', '        if (!net_live)')
 network=piece('        int32_t peak_tx = chart_peak(s_ui.tr_net, 0);','        set_num(s_ui.net_axis')
 helper=''
 if 'static int32_t network_chart_max(' in ui:
  a=ui.index('static int32_t network_chart_max(');helper=ui[a:ui.index('\n}',a)+2]
 base=r"""
#include <stdint.h>
#include <float.h>
#include <math.h>
#include <assert.h>
static struct {int overview_net,tr_net;} s_ui;
static struct status {struct {float rx_kbs,tx_kbs;} net;} state,*st=&state;
static int32_t peak,result;
static int32_t chart_peak(int chart,int series){(void)chart;(void)series;return peak;}
static void uk_trend_set_range(int chart,int32_t lo,int32_t hi){(void)chart;assert(lo==0);result=hi;}
"""
 code=base+helper+'\nstatic void overview(void){\n'+overview+'}\nstatic void network(void){\n'+network+'}\n'
 code+=r"""
int main(void){
 state.net.rx_kbs=FLT_MAX;state.net.tx_kbs=0;overview();assert(result==INT32_MAX-1);network();assert(result==INT32_MAX-1);
 state.net.rx_kbs=0;peak=2147483520;overview();assert(result==INT32_MAX-1);network();assert(result==INT32_MAX-1);
 peak=0;overview();assert(result==1);network();assert(result==20);
 state.net.rx_kbs=10.25f;overview();assert(result==13);network();assert(result==20);
 state.net.rx_kbs=NAN;state.net.tx_kbs=INFINITY;overview();assert(result==1);network();assert(result==20);
 return 0;
}
"""
 with tempfile.TemporaryDirectory(prefix='fnos-chart-range-') as d:
  p=Path(d);src=p/'test.c';exe=p/'test';src.write_text(code)
  cmd=['cc','-std=c11','-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize-recover=all',str(src),'-o',str(exe)]
  subprocess.run(cmd,check=True,capture_output=True)
  result=subprocess.run([str(exe)],capture_output=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=10)
  assert result.returncode==0,result.stderr.decode(errors='replace')
  if helper:
   mutant=code.replace('if (top >= (double)(INT32_MAX - 1)) return INT32_MAX - 1;', '')
   assert mutant!=code;src.write_text(mutant);subprocess.run(cmd,check=True,capture_output=True)
   result=subprocess.run([str(exe)],capture_output=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=10)
   assert result.returncode!=0 and b'outside the range of representable values' in result.stderr,result.stderr
 print('PASS production overview/network range: huge values, peak headroom, zero, fractional ceiling, nonfinite input')

def map_test():
 import random
 rng=random.Random(125)
 lo=-2147483648;hi=2147483647
 cases=[(10000000,0,20000000,600,0),(0,lo,hi,lo,hi),(0,hi,lo,hi,lo),(10,1,1,20,30)]
 for _ in range(10000):
  vals=[rng.randint(lo,hi) for _ in range(5)]
  cases.append(tuple(vals))
 def oracle(x,a,b,c,d):
  if a==b:return c
  if b>=a and x>=b:return d
  if b>=a and x<=a:return c
  if b<=a and x<=b:return d
  if b<=a and x>=a:return c
  step=abs(x-a)*abs(d-c)//abs(b-a)
  return c+(-step if d<c else step)
 data=''.join(' '.join(map(str,(*row,oracle(*row))))+'\n' for row in cases)
 main=r"""
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
int32_t fnos_chart_map(int32_t,int32_t,int32_t,int32_t,int32_t);
int main(void){int32_t x,a,b,c,d,want;int n=0;
 while(scanf("%"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32,&x,&a,&b,&c,&d,&want)==6){
  if(fnos_chart_map(x,a,b,c,d)!=want)return 1;n++;
 }return n==10004?0:2;}
"""
 with tempfile.TemporaryDirectory(prefix='fnos-chart-map-') as d:
  p=Path(d);src=p/'test.c';exe=p/'test';src.write_text(main)
  flags=['cc','-std=c11','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  cmd=flags+[str(src),str(ROOT/'components/fnos_monitor/fnos_chart_math.c'),'-o',str(exe)]
  subprocess.run(cmd,check=True,capture_output=True)
  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
  result=subprocess.run([str(exe)],input=data,text=True,capture_output=True,env=env,timeout=15)
  assert result.returncode==0,result.stderr
  lv=(ROOT/'managed_components/lvgl__lvgl/src/misc/lv_math.c').read_text();a=lv.index('int32_t lv_map(');b=lv.index('\n}',a)+2
  old=p/'old.c';old.write_text('#include <stdint.h>\n'+lv[a:b].replace('lv_map(', 'fnos_chart_map(',1))
  subprocess.run(flags+[str(src),str(old),'-o',str(exe)],check=True,capture_output=True)
  result=subprocess.run([str(exe)],input=data,text=True,capture_output=True,env=env,timeout=15)
  assert result.returncode!=0 and 'signed integer overflow' in result.stderr,result.stderr
 with tempfile.TemporaryDirectory(prefix='fnos-chart-cmake-') as d:
  p=Path(d);(p/'src/widgets/chart').mkdir(parents=True)
  (p/'src/widgets/chart/lv_chart.c').write_text('int fixture;\n')
  header='cmake_minimum_required(VERSION 3.18)\nproject(chart_hook C)\n'
  include='include("'+str(ROOT/'components/fnos_monitor/cmake/fnos_chart_math.cmake')+'")\n'
  good=header+'add_library(fixture STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/widgets/chart/lv_chart.c")\n'+include+'fnos_use_safe_chart_map(fixture)\n'
  (p/'CMakeLists.txt').write_text(good)
  r=subprocess.run(['cmake','-S',str(p),'-B',str(p/'build')],capture_output=True,text=True,timeout=30)
  assert r.returncode==0,r.stderr
  (p/'CMakeLists.txt').write_text(header+include+'fnos_use_safe_chart_map(missing_target)\n')
  r=subprocess.run(['cmake','-S',str(p),'-B',str(p/'missing')],capture_output=True,text=True,timeout=30)
  assert r.returncode!=0 and 'Missing LVGL target' in r.stderr,r.stderr
  (p/'other.c').write_text('int other;\n')
  (p/'CMakeLists.txt').write_text(header+'add_library(fixture STATIC other.c)\n'+include+'fnos_use_safe_chart_map(fixture)\n')
  r=subprocess.run(['cmake','-S',str(p),'-B',str(p/'layout')],capture_output=True,text=True,timeout=30)
  assert r.returncode!=0 and 'LVGL chart source layout changed' in r.stderr,r.stderr
 print('PASS chart map: 10004 full-int32/clamp/descending cases against Python integer oracle; production LVGL old math UBSan negative control')

def gauge_test():
 import re
 ui=(ROOT/'components/fnos_monitor/fnos_ui.c').read_text()
 a=ui.index('static int32_t gauge_value(');helper=ui[a:ui.index('\n}',a)+2]
 a=ui.index('static const char *fmt_cap(');formatter=ui[a:ui.index('\n}',a)+2]
 aggregates=re.findall(r'    (?:    )?double used = 0, total = 0, free_gb = 0;.*?for \(int i = 0; i < st->nvols; i\+\+\) \{.*?\n    (?:    )?\}',ui,re.S)
 assert len(aggregates)==2
 # Extract all actual gauge consumers, including color calls, and require safe entry.
 expressions=re.findall(r'gauge_value\((vol->pct|v->pct|g->max_c|used / total \* 100(?:\.0 \+ 0\.5)?)\)',ui)
 assert len(expressions)==7,expressions
 base=r"""
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
struct volume {float used_gb,total_gb,free_gb;};
struct status {int nvols;struct volume vols[2];};
"""
 code=base+helper+'\n'+formatter
 for i,block in enumerate(aggregates):
  code+='\nstatic void aggregate'+str(i)+'(struct status *st){\n'+block+r"""
 assert(isfinite(used)&&isfinite(total)&&isfinite(free_gb));
 assert(used==(double)FLT_MAX*2 && total==(double)FLT_MAX*2);
 assert(gauge_value(used/total*100)==100);
 char b[80];fmt_cap(b,sizeof b,total);assert(!strstr(b,"inf")&&!strstr(b,"nan"));
}
"""
 code+=r"""
int main(void){
 struct status st={2,{{FLT_MAX,FLT_MAX,FLT_MAX},{FLT_MAX,FLT_MAX,FLT_MAX}}};
 aggregate0(&st);aggregate1(&st);
 double input[]={FLT_MAX,-FLT_MAX,INFINITY,-INFINITY,NAN,0,42.75,100,101,-0.5};
 int expected[]={100,-1,-1,-1,-1,0,42,100,100,-1};
 for(unsigned i=0;i<sizeof input/sizeof input[0];i++)assert(gauge_value(input[i])==expected[i]);
 struct {float pct;} vol_obj={FLT_MAX},*vol=&vol_obj,*v=&vol_obj;
 struct {float max_c;} grp={FLT_MAX},*g=&grp;
 double used=(double)FLT_MAX*2,total=(double)FLT_MAX*2;
"""
 for expr in expressions:code+='assert(gauge_value('+expr+')==100);\n'
 code+='assert(vol->pct==FLT_MAX && g->max_c==FLT_MAX);return 0;}\n'
 with tempfile.TemporaryDirectory(prefix='fnos-ui-gauge-') as d:
  p=Path(d);src=p/'test.c';exe=p/'test'
  flags=['cc','-std=c11','-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize-recover=all',str(src),'-o',str(exe)]
  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
  src.write_text(code);subprocess.run(flags,check=True,capture_output=True)
  r=subprocess.run([str(exe)],capture_output=True,env=env,timeout=10)
  assert r.returncode==0,r.stderr.decode(errors='replace')
  for old in [code.replace('gauge_value(vol->pct)','(int32_t)(vol->pct)'),code.replace('gauge_value(g->max_c)','(int32_t)(g->max_c)')]:
   assert old!=code;src.write_text(old);subprocess.run(flags,check=True,capture_output=True)
   r=subprocess.run([str(exe)],capture_output=True,env=env,timeout=10)
   assert r.returncode and b'outside the range' in r.stderr,r.stderr
  old=code.replace('double used = 0, total = 0, free_gb = 0;','float used = 0, total = 0, free_gb = 0;')
  assert old!=code;src.write_text(old);subprocess.run(flags,check=True,capture_output=True)
  r=subprocess.run([str(exe)],capture_output=True,env=env,timeout=10)
  assert r.returncode and b'isfinite(used)' in r.stderr,r.stderr
 print('PASS production gauge consumers and both capacity accumulations: finite extremes, nonfinite, raw preservation, old casts/float sum negative controls')

def snapshot_fallback_test():
 source=(ROOT/'components/fnos_monitor/fnos_data.c').read_text()
 a=source.index('bool fnos_data_get(');b=source.index('\n}',a)+2
 production=source[a:b]
 pre=r"""
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
typedef struct {bool ever_ok; int value;} fnos_status_t;
static fnos_status_t s_status;
static volatile bool s_valid;
static int s_lock=1;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local bool fail_lock;
#define pdTRUE 1
#define pdMS_TO_TICKS(t) (t)
static int xSemaphoreTake(int lock,int ticks) {(void)ticks;return lock && !fail_lock && pthread_mutex_lock(&mutex)==0;}
static void xSemaphoreGive(int lock) {(void)lock;assert(!pthread_mutex_unlock(&mutex));}
static void fnos_status_copy(fnos_status_t *out,const fnos_status_t *in) {*out=*in;}
static atomic_bool done;
static void *writer(void *arg) {(void)arg;for(int i=0;i<10000;i++){assert(!pthread_mutex_lock(&mutex));s_valid=(i&1);s_status=(fnos_status_t){true,i};assert(!pthread_mutex_unlock(&mutex));}atomic_store(&done,true);return NULL;}
"""
 main=r"""
int main(void) {
 fnos_status_t out={0}; s_valid=true; fail_lock=true;
 assert(!fnos_data_get(&out));assert(!out.ever_ok && out.value==0);
 out=(fnos_status_t){true,123};s_valid=false;
 assert(fnos_data_get(&out) && out.value==123);
 s_lock=0;assert(fnos_data_get(&out) && out.value==123);s_lock=1;
 assert(!fnos_data_get(NULL));
 pthread_t t;assert(!pthread_create(&t,NULL,writer,NULL));
 do {assert(fnos_data_get(&out) && out.value==123);}while(!atomic_load(&done));
 assert(!pthread_join(t,NULL)); fail_lock=false;
 assert(fnos_data_get(&out) && out.value==9999);
 return 0;
}
"""
 with tempfile.TemporaryDirectory(prefix='fnos-snapshot-fallback-') as d:
  p=Path(d);src=p/'test.c';exe=p/'test'
  flags=['cc','-std=c11','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(src),'-o',str(exe)]
  for old in (False,True):
   code=production.replace('return out->ever_ok;','return s_valid;') if old else production
   if old:assert code!=production
   src.write_text(pre+code+main);subprocess.run(flags,check=True,capture_output=True)
   r=subprocess.run([str(exe)],capture_output=True,timeout=20)
   if old:assert r.returncode and b'!fnos_data_get(&out)' in r.stderr,r.stderr
   else:assert not r.returncode,r.stderr
 print('PASS production snapshot fallback: missing/failed lock preserves caller-owned validity, concurrent writer, normal copy; old shared-valid negative')

if __name__=='__main__':
 snapshot_fallback_test()
 gauge_test()
 map_test()
 range_test()
 chart_test()
 run()
