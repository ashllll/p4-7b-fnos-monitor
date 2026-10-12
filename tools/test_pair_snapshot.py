#!/usr/bin/env python3
"""Compile real pairing initialization/snapshot and endpoint guards with host locks."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAIR = (ROOT/'components/fnos_monitor/fnos_pair.c').read_text()
DATA = (ROOT/'components/fnos_monitor/fnos_data.c').read_text()

def function(source, name):
    match = re.search(r'^(?:static )?[\w *]+\b'+name+r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('production function missing: '+name)
    start = source.index('{', match.start())
    depth, end = 1, start+1
    while depth:
        depth += (source[end]=='{') - (source[end]=='}')
        end += 1
    return source[match.start():end]

preamble = r'''
#include "fnos_pair.h"
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0
#define tskNO_AFFINITY 0
#define MALLOC_CAP_SPIRAM 0
#define FNOS_HIST_MAX 600
#define TAG "test"
typedef pthread_mutex_t *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef int BaseType_t;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static _Atomic(SemaphoreHandle_t) s_lock;
static atomic_flag s_init_guard = ATOMIC_FLAG_INIT;
static bool s_loaded;
static _Atomic(TaskHandle_t) s_task;
static fnos_pair_cfg_t s_cfg;
static fnos_pair_view_t s_view;
static uint32_t s_generation;
static bool s_provisioned;
static atomic_bool fail_take;
static bool fail_alloc, fail_task, reenter_init, fail_load;
static int load_count, held, drops;
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return fail_alloc ? NULL : &mutex; }
static int xSemaphoreTake(SemaphoreHandle_t lock, int timeout) {
    (void)timeout;
    if (fail_take) return 0;
    assert(lock);
    int rc = pthread_mutex_lock(lock);
    if (!rc) held++;
    return rc == 0;
}
static void xSemaphoreGive(SemaphoreHandle_t lock) { held--; assert(!pthread_mutex_unlock(lock)); }
static void vSemaphoreDelete(SemaphoreHandle_t lock) { (void)lock; }
static bool nvs_load(void) {
    assert(held == 1);
    load_count++;
    if (fail_load) { memset(&s_cfg,0,sizeof s_cfg); s_provisioned=false; return false; }
    if (reenter_init) fnos_pair_init();
    memset(&s_cfg, 0, sizeof(s_cfg));
    strcpy(s_cfg.host, "fixture.local"); s_cfg.port=24680; s_cfg.tls=true;
    strcpy(s_cfg.cert_pem, "anonymous-certificate"); s_provisioned=true;
    return true;
}
static void pair_task(void *arg) { (void)arg; }
static int create_task(void (*fn)(void *), const char *name, int bytes, void *arg, int priority, TaskHandle_t *out, int core) {
    (void)fn; (void)name; (void)bytes; (void)arg; (void)priority; (void)core;
    if (fail_task) return 0;
    *out=(void *)1; return pdPASS;
}
#define xTaskCreatePinnedToCoreWithCaps(fn,name,bytes,arg,priority,out,core,caps) create_task(fn,name,bytes,arg,priority,out,core)
#define xTaskCreatePinnedToCore create_task
static fnos_pair_cfg_t s_conn;
static uint32_t s_conn_gen;
static bool s_conn_usable, s_conn_tls;
static char s_url_status[128], s_url_hist[128], s_token[80], s_cert_pem[FNOS_PAIR_PEM_MAX], s_conn_err[80];
static void http_drop_client(void) { drops++; }
'''
functions = '\n'.join(function(PAIR, name) for name in (
    'fnos_pair_init','fnos_pair_provisioned','fnos_pair_generation','fnos_pair_active_gen','fnos_pair_active','fnos_pair_view'))
functions += '\n'+function(DATA, 'apply_endpoint')
# Execute the exact production early-rejection block, without mocking an HTTP stack.
http = function(DATA, 'http_get')
http = http[:http.index('    if (!s_client)')] + '    return true;\n}'
functions += '\n'+http
body = r'''
static atomic_bool writer_done;
static void *writer(void *unused) {
    (void)unused;
    for (uint32_t i=2; i<10002; i++) {
        assert(xSemaphoreTake(s_lock,0));
        s_generation=i; s_cfg.port=(int)i;
        xSemaphoreGive(s_lock);
    }
    writer_done=true; return NULL;
}
int main(void) {
    fnos_pair_cfg_t out, zero={0};
    memset(&out,0x5a,sizeof(out));
    assert(fnos_pair_active_gen(&out)==0 && !memcmp(&out,&zero,sizeof(out)));
    fail_alloc=true; fnos_pair_init(); assert(!s_lock && !load_count);
    fail_alloc=false; fail_take=true; fnos_pair_init(); assert(!s_lock && !load_count);
    fail_take=false; fail_task=true; reenter_init=true;
    fnos_pair_init(); assert(s_loaded && load_count==1 && !s_task && s_generation==1);
    assert(fnos_pair_active_gen(&out)==1 && out.port==24680);
    strcpy(s_cfg.host,"retained.local");
    fail_task=false; fnos_pair_init();
    assert(load_count==1 && s_task && !strcmp(s_cfg.host,"retained.local"));
    fnos_pair_init(); assert(load_count==1);
    apply_endpoint(); assert(s_conn_usable && s_conn_gen==1 && s_conn_tls);
    assert(strstr(s_url_status,"https://retained.local:24680/"));
    fail_take=true;
    assert(fnos_pair_generation()==0 && !fnos_pair_provisioned());
    memset(&out,0x5a,sizeof(out));
    assert(fnos_pair_active_gen(&out)==0 && !memcmp(&out,&zero,sizeof(out)));
    fnos_pair_view_t view;
    memset(&view,0x5a,sizeof(view)); fnos_pair_view(&view);
    assert(view.state==FNOS_PAIR_FAILED && !view.host[0] && !view.fingerprint[0]);
    apply_endpoint(); assert(!s_conn_usable && !s_conn_gen && drops==2);
    int status=1,len=1,ms=1; char *buffer=NULL; size_t capacity=0;
    assert(!http_get("ignored",&buffer,&capacity,1,&status,&len,&ms));
    assert(!status && !len && !ms && !strcmp(s_conn_err,"read error"));
    fail_take=false;
    assert(fnos_pair_generation()!=s_conn_gen);
    apply_endpoint(); assert(s_conn_usable && s_conn_gen==1 && drops==3);
    pthread_t thread; assert(!pthread_create(&thread,NULL,writer,NULL));
    while (!writer_done) {
        uint32_t gen=fnos_pair_active_gen(&out);
        assert(gen==1 || gen==(uint32_t)out.port);
    }
    assert(!pthread_join(thread,NULL));
    assert(fnos_pair_active_gen(&out)==10001 && out.port==10001);
    s_task=NULL; s_loaded=false; fail_load=true;
    fnos_pair_init();
    assert(s_task && s_loaded && !s_generation && !fnos_pair_provisioned());
    assert(fnos_pair_active_gen(&out)==0 && !memcmp(&out,&zero,sizeof out));
    fnos_pair_view(&view); assert(view.state==FNOS_PAIR_FAILED);
    apply_endpoint(); assert(!s_conn_usable && !s_conn_gen);
    assert(xSemaphoreTake(s_lock,0));
    s_generation=1; s_cfg.port=24680; s_provisioned=true;
    xSemaphoreGive(s_lock);
    assert(fnos_pair_active_gen(&out)==1 && fnos_pair_provisioned());
    puts("PASS: invalid store disables telemetry while retaining repair worker; explicit recovery");
    puts("PASS: initialization failures/retry, same-lock snapshots, unavailable closes connection, recovery, concurrent writer");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source=Path(tmp)/'snapshot.c'; binary=Path(tmp)/'snapshot'
    source.write_text(preamble+'\n'+functions+'\n'+body)
    command=['cc','-std=c11','-UNDEBUG','-fsanitize=address,undefined','-pthread','-I',str(ROOT/'components/fnos_monitor'),str(source),'-o',str(binary)]
    subprocess.run(command,check=True)
    subprocess.run([str(binary)],check=True,cwd=tmp)
    guarded = '''if (out) memset(out, 0, sizeof(*out));
        return 0;  // 失败不裸读、也不回退编译期 HTTP 参数。'''
    mutant = source.read_text().replace(guarded, 'if (out) *out = s_cfg;\n        return s_generation;')
    if mutant == source.read_text():
        raise RuntimeError('snapshot negative control no longer matches')
    source.write_text(mutant)
    subprocess.run(command,check=True)
    result = subprocess.run([str(binary)],cwd=tmp,capture_output=True)
    if result.returncode == 0:
        raise RuntimeError('negative control accepted unlocked fallback')
    print('PASS: negative control rejects unlocked configuration fallback')
