#!/usr/bin/env python3
"""Pairing commit tail and real forget function under host lock/NVS fault injection."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT/'components/fnos_monitor/fnos_pair.c').read_text()

def function(name):
    match = re.search(r'^static void '+name+r'\([^;]*?\)\s*\{', SOURCE, re.M)
    if not match:
        raise RuntimeError('production function missing: '+name)
    start = SOURCE.index('{', match.start())
    depth, end = 1, start+1
    while depth:
        depth += (SOURCE[end]=='{') - (SOURCE[end]=='}')
        end += 1
    return SOURCE[match.start():end]

pair = function('do_pair')
start = pair.index('    /* 先取得发布锁')
# Execute production persistence/publication tail. HTTP/JSON/token acceptance is a separate contract.
commit = 'static void commit_pair(fnos_pair_cfg_t *next) {\n'+pair[start:]
http_start = pair.index('    int status = 0, len = 0;')
http_end = pair.index('    /* 服务端在非 200 时也会给', http_start)
exchange = 'static void exchange_pair(void) { char *rx=malloc(2048); char url[128]="fixture", body[128]="fixture";\n'+pair[http_start:http_end]+'    processed++; free(rx);\n}'
forget = function('do_forget')
prefix = pair[:pair.index('    char *rx =')].replace('static void do_pair(void)', 'static void pair_request_body(char *out, size_t cap)')
prefix += '    snprintf(out, cap, "%s", body);\n}'
assert 'snprintf(s_code,' not in function('do_fetch')
assert function('do_fetch').count('snprintf(s_pend_code,') == 2
PREAMBLE = r'''
#include "fnos_pair.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdTRUE 1
#define portMAX_DELAY 0
#define FNOS_HOST "fixture.invalid"
#define FNOS_PORT 24680
#define FNOS_TOKEN ""
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t *s_lock = &mutex;
static atomic_bool fail_take, block_save, save_entered, release_save;
static bool fail_save, fail_wipe, held;
static int saves, wipes;
static fnos_pair_cfg_t s_cfg, persisted;
static uint32_t s_generation;
static bool s_provisioned, s_cancel_requested;
static char s_pend_pem[64], s_pend_fp[64], s_pend_cn[64], s_pend_until[64];
static int s_pend_port;
static bool s_pend_tls;
static char s_code[16], s_pend_code[16];
#define PAIR_PATH "/api/v1/pair"
#define FNOS_PAIR_DEVICE_NAME "anonymous-fixture"
static int requests, processed;
static bool cancel_after_request;
static char s_http_cert[64], s_http_diag[96];
static bool pair_cancelled(void) { return s_cancel_requested; }
static bool http_oneshot(bool post,const char *url,const char *body,const char *cert,char *rx,int cap,int *status,int *len,char *diag,size_t diag_cap) {
    (void)url; (void)body; (void)cert; (void)rx; (void)cap; (void)diag; (void)diag_cap;
    assert(post); requests++; *status=200; *len=2;
    if (cancel_after_request) s_cancel_requested=true;
    return true;
}
static fnos_pair_state_t view;
static int xSemaphoreTake(pthread_mutex_t *lock, int timeout) {
    (void)timeout;
    if (fail_take) return 0;
    assert(!pthread_mutex_lock(lock)); held=true; return pdTRUE;
}
static void xSemaphoreGive(pthread_mutex_t *lock) {
    assert(held); held=false; assert(!pthread_mutex_unlock(lock));
}
static bool nvs_save(const fnos_pair_cfg_t *next) {
    assert(held); saves++;
    atomic_store(&save_entered, true);
    while (block_save && !release_save) { }
    if (fail_save) return false;
    persisted=*next; return true;
}
static bool nvs_wipe(void) {
    assert(held); wipes++;
    if (fail_wipe) return false;
    memset(&persisted, 0, sizeof persisted); return true;
}
static void set_view(fnos_pair_state_t state, const char *msg) { (void)msg; view=state; }
'''
MAIN = r'''
static void reset(void) {
    memset(&s_cfg, 0, sizeof s_cfg); strcpy(s_cfg.host, "old.invalid");
    strcpy(s_cfg.token, "anonymous-old"); persisted=s_cfg;
    s_generation=10; s_provisioned=true; s_cancel_requested=false; view=FNOS_PAIR_PAIRING;
    fail_take=false; fail_save=false; fail_wipe=false; held=false; saves=wipes=0;
    strcpy(s_pend_pem, "old cert"); strcpy(s_pend_fp, "old fp"); strcpy(s_pend_cn, "old cn");
    strcpy(s_pend_until, "old date"); s_pend_port=1234; s_pend_tls=true;
}
static void unchanged(void) {
    assert(!strcmp(s_cfg.host, "old.invalid") && !strcmp(persisted.host, "old.invalid"));
    assert(s_generation == 10 && s_provisioned && !held && view == FNOS_PAIR_FAILED);
}
static void *writer(void *arg) { commit_pair(arg); return NULL; }
int main(void) {
    fnos_pair_cfg_t next={0}; strcpy(next.host, "new.invalid"); strcpy(next.token, "anonymous-new"); next.port=24680;
    requests=processed=0; s_cancel_requested=true; exchange_pair(); assert(!requests && !processed);
    s_cancel_requested=false; cancel_after_request=true; exchange_pair(); assert(requests==1 && !processed);
    s_cancel_requested=false; cancel_after_request=false; exchange_pair(); assert(requests==2 && processed==1);
    char request_body[128];
    strcpy(s_pend_code, "123456"); strcpy(s_code, "654321");
    pair_request_body(request_body, sizeof request_body);
    assert(strstr(request_body, "123456") && !strstr(request_body, "654321"));
    reset(); s_cancel_requested=true; commit_pair(&next);
    assert(!saves && !wipes && s_generation==10 && s_provisioned && !held && view==FNOS_PAIR_PAIRING);
    reset(); fail_take=true; commit_pair(&next); unchanged(); assert(!saves && !wipes);
    reset(); s_lock=NULL; commit_pair(&next); unchanged(); assert(!saves); s_lock=&mutex;
    reset(); fail_save=true; commit_pair(&next); unchanged(); assert(saves == 1);
    reset(); fail_take=true; do_forget(); unchanged(); assert(!wipes);
    reset(); s_lock=NULL; do_forget(); unchanged(); assert(!wipes); s_lock=&mutex;
    reset(); fail_wipe=true; do_forget(); unchanged(); assert(wipes == 1 && s_pend_port == 1234);
    reset(); commit_pair(&next);
    assert(!strcmp(s_cfg.host, "new.invalid") && !memcmp(&s_cfg, &persisted, sizeof s_cfg));
    assert(s_generation == 11 && s_provisioned && view == FNOS_PAIR_PROVISIONED && !held);
    reset(); s_generation=UINT32_MAX; commit_pair(&next); assert(s_generation == 1);
    reset(); s_generation=UINT32_MAX; do_forget();
    assert(s_generation == 1 && !s_provisioned && view == FNOS_PAIR_UNPROVISIONED && !held);
    assert(!strcmp(s_cfg.host, FNOS_HOST) && s_cfg.port == FNOS_PORT && !s_cfg.token[0] && !s_cfg.tls);
    assert(!persisted.host[0] && !s_pend_pem[0] && !s_pend_fp[0] && !s_pend_cn[0] && !s_pend_until[0]);
    assert(!s_pend_port && !s_pend_tls);
    reset(); fail_save=true; commit_pair(&next); fail_save=false; commit_pair(&next);
    assert(view == FNOS_PAIR_PROVISIONED && s_generation == 11);
    reset(); block_save=true; release_save=false; save_entered=false;
    pthread_t t; assert(!pthread_create(&t, NULL, writer, &next));
    while (!save_entered) { }
    assert(pthread_mutex_trylock(&mutex) != 0); // no snapshot crosses persistence/publication window
    release_save=true; assert(!pthread_join(t, NULL)); block_save=false;
    assert(!memcmp(&persisted, &s_cfg, sizeof s_cfg) && s_generation == 11);
    puts("Pair commit/forget lock-before-NVS, storage failure, retry, generation, publication window PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='fnos-pair-commit-') as tmp:
    path=Path(tmp)/'test.c'; binary=Path(tmp)/'test'
    for variant in ("current", "old_order", "mailbox"):
        negative = variant != "current"
        tested=commit
        if variant == "old_order":
            # Restore the old persistence-before-lock order; NVS must detect missing ownership.
            tested=tested.replace('    /* 先取得发布锁', '    nvs_save(next);\n    /* 先取得发布锁', 1)
        tested_prefix = prefix.replace("s_pend_code", "s_code") if variant == "mailbox" else prefix
        path.write_text(PREAMBLE+exchange+tested+forget+tested_prefix+MAIN)
        subprocess.run(['cc','-std=c11','-pthread','-UNDEBUG','-fsanitize=address,undefined','-g',
                        '-I',str(ROOT/'components/fnos_monitor'),str(path),'-o',str(binary)],check=True)
        run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
        if negative:
            assert run.returncode != 0, variant + ' negative control passed'
        elif run.returncode:
            raise RuntimeError(run.stdout+run.stderr)
        else:
            print(run.stdout.strip())
print('Pair commit old-order and mailbox-reuse negative controls rejected')
