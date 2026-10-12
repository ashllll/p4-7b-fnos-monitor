#!/usr/bin/env python3
"""Production request/dispatch/fetch functions: anonymous host faults, no network/NVS."""
import re
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'components/fnos_monitor/fnos_pair.c').read_text()
def function(name):
    m=re.search(r'^(?:static )?[\w *]+\b'+name+r'\([^;]*?\)\s*\{',SOURCE,re.M)
    if not m: raise RuntimeError('production function missing: '+name)
    start=SOURCE.index('{',m.start()); end=start+1; depth=1
    while depth:
        depth+=(SOURCE[end]=='{')-(SOURCE[end]=='}'); end+=1
    return SOURCE[m.start():end]
PRE=r'''
#include "fnos_pair.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define pdTRUE 1
#define portMAX_DELAY 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define RX_MAX (12*1024)
#define IDENT_PATH "/api/v1/identity"
#define PAIR_APP_PORT 8798
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
enum { ACT_NONE=0, ACT_FETCH, ACT_CONFIRM, ACT_FORGET };
enum { GOOD=0, OOM, NETWORK, JSON_BAD, TLS_BAD, PEM_MISSING, PEM_LONG, FP_BAD, PLAIN };
static int mode, paired, forgotten, notified, calls;
static atomic_bool block_http, entered_http, release_http;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t *s_lock=&mutex;
static void *s_task=(void *)1;
static bool fail_lock, s_request_busy, s_accept, s_cancel_requested, s_provisioned;
static int s_act;
static char s_code[16], s_pend_code[16];
static fnos_pair_cfg_t s_cfg;
static fnos_pair_view_t s_view;
static char s_pend_pem[FNOS_PAIR_PEM_MAX], s_pend_fp[FNOS_PAIR_FP_MAX], s_pend_cn[80], s_pend_until[16];
static char s_http_diag[96];
static int s_pend_port;
static bool s_pend_tls;
static int xSemaphoreTake(pthread_mutex_t *lock,int timeout) {
    (void)timeout; if (fail_lock) return 0; assert(!pthread_mutex_lock(lock)); return pdTRUE;
}
static void xSemaphoreGive(pthread_mutex_t *lock) { assert(!pthread_mutex_unlock(lock)); }
static void xTaskNotifyGive(void *task) { assert(task); notified++; }
static void *fixture_malloc(size_t size) { return mode==OOM ? NULL : malloc(size); }
#define malloc fixture_malloc
static void *heap_caps_malloc(size_t size,int caps) { (void)caps; return fixture_malloc(size); }
typedef struct { char *valuestring; } cJSON;
static cJSON root, tls, pem;
static char big_pem[FNOS_PAIR_PEM_MAX+30];
static cJSON *cJSON_Parse(const char *text) { (void)text; return mode==JSON_BAD ? NULL : &root; }
static void cJSON_Delete(cJSON *node) { assert(node==&root); }
static const cJSON *cJSON_GetObjectItemCaseSensitive(cJSON *node,const char *key) {
    assert(node==&root); return !strcmp(key,"tls") ? &tls : &pem;
}
static bool cJSON_IsBool(const cJSON *node) { assert(node==&tls); return mode!=TLS_BAD; }
static bool cJSON_IsTrue(const cJSON *node) { assert(node==&tls); return mode!=PLAIN; }
static bool cJSON_IsString(const cJSON *node) { assert(node==&pem); return mode!=PEM_MISSING; }
static bool http_oneshot(bool secure,const char *url,const char *cert,const char *body,char *rx,int cap,int *status,int *len,char *diag,size_t diag_cap) {
    (void)url; (void)cert; (void)body; (void)diag; (void)diag_cap;
    assert(!secure && cap>2); calls++;
    entered_http=true;
    while (block_http && !release_http) { }
    strcpy(rx,"{}"); *len=2; *status=mode==NETWORK ? 500 : 200;
    return mode!=NETWORK;
}
static bool pem_fingerprint(const char *cert,char *out,size_t cap) { (void)cert; snprintf(out,cap,"anonymous-fp"); return mode!=FP_BAD; }
static void cert_info(const char *cert,char *cn,size_t cc,char *until,size_t uc) { (void)cert; snprintf(cn,cc,"fixture"); snprintf(until,uc,"2099-01-01"); }
static void do_pair(void);
static void do_forget(void);
'''
functions='\n'.join(function(n) for n in ('has_cert','pair_state_busy','pending_clear','pair_cancelled','set_view_locked','set_view','do_fetch','pair_finish_locked','pair_process_action','post_action','fnos_pair_begin','fnos_pair_confirm','fnos_pair_forget'))
TAIL=r'''
static void do_pair(void) {
    assert(s_request_busy && s_pend_code[0]); paired++;
    set_view(FNOS_PAIR_PROVISIONED,"fixture paired");
}
static void do_forget(void) { assert(s_request_busy); forgotten++; pending_clear(); set_view(FNOS_PAIR_UNPROVISIONED,"fixture forgotten"); }
static void reset(void) {
    mode=GOOD; s_act=ACT_NONE; s_request_busy=false; s_accept=false; s_cancel_requested=false; s_provisioned=false; fail_lock=false;
    paired=forgotten=notified=calls=0; block_http=entered_http=release_http=false;
    pending_clear(); memset(&s_view,0,sizeof s_view); memset(&s_cfg,0,sizeof s_cfg);
    strcpy(s_cfg.host,"fixture.invalid"); s_cfg.port=8798;
    pem.valuestring="-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----";
}
static void offered(void) {
    fnos_pair_begin("123456"); assert(s_request_busy && s_view.state==FNOS_PAIR_FETCHING);
    pair_process_action(); assert(!s_request_busy && s_view.state==FNOS_PAIR_CONFIRM && s_pend_tls);
    assert(!strcmp(s_pend_code,"123456") && !strcmp(s_view.fingerprint,"anonymous-fp"));
}
static void empty_pending(void) {
    assert(!s_pend_code[0] && !s_pend_pem[0] && !s_pend_fp[0] && !s_pend_cn[0] && !s_pend_until[0] && !s_pend_port && !s_pend_tls);
}
static void *dispatch(void *unused) { (void)unused; pair_process_action(); return NULL; }
int main(void) {
    reset(); fnos_pair_begin(NULL); fnos_pair_begin(""); fnos_pair_begin("12345");
    fnos_pair_begin("1234567"); fnos_pair_begin("12\"456"); fnos_pair_begin("abcdef");
    assert(s_act==ACT_NONE && !s_request_busy && !notified);
    reset(); fnos_pair_confirm(true); assert(s_act==ACT_NONE && !notified); pair_process_action(); assert(!paired);
    reset(); offered(); fnos_pair_confirm(true); assert(s_act==ACT_CONFIRM && s_request_busy);
    fnos_pair_confirm(true); fnos_pair_begin("654321"); fnos_pair_forget();
    assert(s_act==ACT_CONFIRM && s_accept && !strcmp(s_pend_code,"123456"));
    pair_process_action(); assert(paired==1 && !s_request_busy);
    fnos_pair_confirm(true); pair_process_action(); assert(paired==1);
    reset(); offered(); fnos_pair_confirm(false); pair_process_action(); empty_pending();
    assert(!paired && s_view.state==FNOS_PAIR_UNPROVISIONED); fnos_pair_confirm(true); pair_process_action(); assert(!paired);
    for (int fault=OOM; fault<=FP_BAD; fault++) {
        reset(); offered(); mode=fault;
        if (fault==PEM_LONG) {
            memset(big_pem,'a',sizeof big_pem); memcpy(big_pem,"-----BEGIN CERTIFICATE-----",27);
            strcpy(big_pem+sizeof big_pem-26,"-----END CERTIFICATE-----"); pem.valuestring=big_pem;
        }
        fnos_pair_begin("654321"); assert(s_view.state==FNOS_PAIR_FETCHING && !s_view.fingerprint[0]);
        fnos_pair_confirm(true); assert(s_act==ACT_FETCH);
        pair_process_action(); assert(s_view.state==FNOS_PAIR_FAILED && !paired && !s_request_busy); empty_pending();
        fnos_pair_confirm(true); pair_process_action(); assert(!paired && s_act==ACT_NONE);
    }
    reset(); offered(); fnos_pair_begin("654321"); pair_process_action();
    assert(!strcmp(s_pend_code,"654321")); fnos_pair_confirm(true); pair_process_action(); assert(paired==1);
    reset(); offered(); fnos_pair_forget(); fnos_pair_confirm(true); pair_process_action();
    assert(forgotten==1 && !paired); empty_pending();
    reset(); mode=PLAIN; fnos_pair_begin("123456"); pair_process_action(); assert(paired==1 && !s_pend_tls);
    reset(); fail_lock=true; fnos_pair_begin("123456"); assert(!s_act && !s_request_busy); fail_lock=false;
    fnos_pair_begin("123456"); block_http=true;
    pthread_t worker; assert(!pthread_create(&worker,NULL,dispatch,NULL)); while(!entered_http) { }
    fnos_pair_begin("654321"); fnos_pair_confirm(true); fnos_pair_forget();
    assert(s_request_busy && s_act==ACT_NONE && !paired);
    release_http=true; assert(!pthread_join(worker,NULL)); assert(s_view.state==FNOS_PAIR_CONFIRM && !s_request_busy);
    fnos_pair_confirm(true); pair_process_action(); assert(paired==1 && !strcmp(s_pend_code,"123456"));
    reset(); fnos_pair_begin("123456"); block_http=true;
    assert(!pthread_create(&worker,NULL,dispatch,NULL)); while(!entered_http) { }
    fnos_pair_confirm(false); assert(s_cancel_requested && s_request_busy && s_act==ACT_NONE);
    release_http=true; assert(!pthread_join(worker,NULL));
    assert(!paired && !s_request_busy && s_view.state==FNOS_PAIR_UNPROVISIONED); empty_pending();
    fnos_pair_confirm(true); pair_process_action(); assert(!paired);
    reset(); fnos_pair_begin("123456"); fnos_pair_confirm(false); pair_process_action();
    assert(!calls && !paired && !s_request_busy); empty_pending();
    reset(); offered();
    // Model cancellation arriving at the worker's final lock boundary.
    s_request_busy=true; s_cancel_requested=true; s_act=ACT_NONE;
    pair_process_action(); assert(!s_request_busy && !s_cancel_requested && s_view.state==FNOS_PAIR_UNPROVISIONED); empty_pending();
    puts("PASS: invalid/duplicate confirm, new fetch invalidation, seven fetch faults, cancellation/forget, plain flow, busy HTTP concurrency, lock failure");
}
'''
program=PRE+'\n'+functions+'\n'+TAIL
with tempfile.TemporaryDirectory() as tmp:
    c=Path(tmp)/'state.c'; binary=Path(tmp)/'state'; c.write_text(program)
    command=['cc','-std=c11','-UNDEBUG','-fsanitize=address,undefined','-pthread','-I',str(ROOT/'components/fnos_monitor'),str(c),'-o',str(binary)]
    subprocess.run(command,check=True); subprocess.run([str(binary)],check=True,cwd=tmp)
    for before,after,label in (
        (' || s_view.state != FNOS_PAIR_CONFIRM','', 'invalid confirmation'),
        ('    s_request_busy = true;','    s_request_busy = false;', 'busy overwrite'),
        ('    if (state == FNOS_PAIR_FAILED) pending_clear();','', 'failure residue'),
    ):
        mutant=program.replace(before,after)
        if mutant==program: raise RuntimeError('negative control missing: '+label)
        c.write_text(mutant); subprocess.run(command,check=True)
        result=subprocess.run([str(binary)],capture_output=True,cwd=tmp)
        if result.returncode==0: raise RuntimeError('negative control accepted: '+label)
        print('PASS: negative control rejected '+label)
