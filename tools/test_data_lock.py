#!/usr/bin/env python3
"""Exercise production UI reads against a contended pthread mutex, without hardware."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/parsecheck'))
from extract import grab

SHIM = r'''
#include <pthread.h>
#include <time.h>
#include <assert.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned requested_ms;
static double now_ms(void) {
 struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
 return t.tv_sec*1000.0+t.tv_nsec/1000000.0;
}
static int take(int lock, unsigned ticks) {
 if (!lock) return 0;
 requested_ms += ticks;
 double deadline=now_ms()+ticks;
 do {
  if (!pthread_mutex_trylock(&mutex)) return 1;
  if (!ticks) break;
  struct timespec pause={0,1000000};nanosleep(&pause,NULL);
 } while (now_ms()<deadline);
 return 0;
}
#define xSemaphoreTake(l,t) take(l,t)
'''
MAIN = r'''
static void *reader(void *arg) {
 (void)arg;
 fnos_status_t out={.ever_ok=true,.source_seq=123};
 fnos_sample_t sample={.cpu=37};int64_t next=9;bool reset=true;
 double start=now_ms();
 assert(fnos_data_get(&out) && out.source_seq==123);
 assert(fnos_data_hist_read_ex(9,&sample,1,&next,&reset)==0);
 assert(next==9 && !reset && sample.cpu==37);
 printf("contended reads: %.3f ms, requested wait: %u ms\n",now_ms()-start,requested_ms);
 fflush(stdout);
 assert(requested_ms==0);
 return NULL;
}
int main(void) {
 pthread_t t;assert(!pthread_mutex_lock(&mutex));
 assert(!pthread_create(&t,NULL,reader,NULL));
 assert(!pthread_join(t,NULL));assert(!pthread_mutex_unlock(&mutex));
 fnos_status_t st={.ever_ok=true,.source_seq=456};
 commit_status(&st,1,200);
 fnos_status_t out={0};assert(fnos_data_get(&out) && out.source_seq==456);
 fnos_sample_t sample;int64_t next=0;bool reset;
 assert(fnos_data_hist_read_ex(0,&sample,1,&next,&reset)==1 && next==1);
 fnos_status_release(&out);fnos_status_release(&s_status);
 return 0;
}
'''

def run():
    with tempfile.TemporaryDirectory(prefix='fnos-lock-') as directory:
        p = Path(directory)
        subprocess.run([sys.executable, str(ROOT/'tools/parsecheck/extract.py'),
                        str(ROOT/'components/fnos_monitor/fnos_data.c'), str(p/'production.c')],
                       check=True, capture_output=True)
        source = (p/'production.c').read_text()
        source = source.replace('#define xSemaphoreTake(l, t) ((l) ? pdTRUE : 0)', SHIM)
        source = source.replace('#define xSemaphoreGive(l) ((void)0)',
                                '#define xSemaphoreGive(l) ((void)pthread_mutex_unlock(&mutex))')
        source += '\n' + grab((ROOT/'components/fnos_monitor/fnos_data.c').read_text(), 'fnos_data_get')
        cj = ROOT/'managed_components/espressif__cjson/cJSON'
        for old in (False, True):
            candidate = source
            if old:
                # Reintroduce the old wait only in the two UI read interfaces.
                for name in ('fnos_data_get', 'fnos_data_hist_read_ex'):
                    body = grab(candidate, name)
                    changed = body.replace('xSemaphoreTake(s_lock, 0)',
                                           'xSemaphoreTake(s_lock, pdMS_TO_TICKS(50))')
                    assert changed != body
                    candidate = candidate.replace(body, changed)
            (p/'test.c').write_text(candidate+'\n'+MAIN)
            subprocess.run(['cc','-std=c11','-pthread','-fsanitize=address,undefined',
                            '-I'+str(ROOT/'components/fnos_monitor'), '-I'+str(ROOT/'build/config'),
                            '-I'+str(cj), str(p/'test.c'), str(cj/'cJSON.c'), '-o', str(p/'test')],
                           check=True, capture_output=True)
            result = subprocess.run([str(p/'test')], capture_output=True, text=True, timeout=5,
                                    env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
            print(('old wait: ' if old else 'current: ')+result.stdout.strip())
            if old:
                assert result.returncode and 'requested_ms==0' in result.stderr, result.stderr
            else:
                assert result.returncode == 0, result.stderr
    print('PASS nonblocking reads preserve snapshot, cursor and buffers; next read recovers; old wait rejected')

if __name__ == '__main__':
    run()
