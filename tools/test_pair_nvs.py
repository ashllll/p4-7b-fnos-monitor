#!/usr/bin/env python3
"""Run production NVS loader against per-key faults; X509 backend is a host stub."""
import re
import subprocess
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'components/fnos_monitor/fnos_pair.c').read_text()
def function(name):
    match = re.search(r'^static (?:bool|void) '+name+r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('missing production function: '+name)
    start=source.index('{',match.start()); end=start+1; depth=1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}'); end+=1
    return source[match.start():end]
PRE = r'''
#include "fnos_pair.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define FNOS_HOST "default.invalid"
#define FNOS_PORT 8798
#define FNOS_TOKEN "anonymous-default-token"
#define ESP_LOGE(...) ((void)0)
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define NVS_READONLY 0
#define NVS_NS "pair"
#define KEY_HOST "host"
#define KEY_PORT "port"
#define KEY_TOKEN "token"
#define KEY_CERT "cert"
#define KEY_FP "fp"
#define KEY_TXN "txn_v1"
enum { NVS_TXN_DIRTY=0, NVS_TXN_READY=1, NVS_TXN_EMPTY=2 };
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define calloc fixture_calloc
static bool fail_alloc;
static void *fixture_calloc(size_t n,size_t size) { return fail_alloc ? NULL : malloc(n*size); }
static void *heap_caps_calloc(size_t n,size_t size,int caps) { (void)caps; return fixture_calloc(n,size); }
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef struct { int unused; } mbedtls_x509_crt;
static int parse_error, parses, closes, opened, code[5];
static const char *values[5];
static int32_t saved_port, saved_txn;
static int txn_error;
static void mbedtls_x509_crt_init(mbedtls_x509_crt *crt) { (void)crt; }
static void mbedtls_x509_crt_free(mbedtls_x509_crt *crt) { (void)crt; }
static int mbedtls_x509_crt_parse(mbedtls_x509_crt *crt,const unsigned char *pem,size_t len) {
    (void)crt; assert(len==strlen((const char *)pem)+1); parses++; return parse_error;
}
static int key_index(const char *key) {
    if (!strcmp(key,KEY_HOST)) return 0;
    if (!strcmp(key,KEY_PORT)) return 1;
    if (!strcmp(key,KEY_TOKEN)) return 2;
    if (!strcmp(key,KEY_CERT)) return 3;
    assert(!strcmp(key,KEY_FP)); return 4;
}
static int nvs_open(const char *name,int mode,nvs_handle_t *out) {
    (void)name; (void)mode; *out=17; return opened;
}
static void nvs_close(nvs_handle_t handle) { assert(handle==17); closes++; }
static int nvs_get_str(nvs_handle_t handle,const char *key,char *out,size_t *size) {
    assert(handle==17); int i=key_index(key);
    if (code[i]) return code[i];
    size_t need=strlen(values[i])+1;
    if (need>*size) { *size=need; return 2; }
    memcpy(out,values[i],need); *size=need; return 0;
}
static int nvs_get_i32(nvs_handle_t handle,const char *key,int32_t *out) {
    if (!strcmp(key,KEY_TXN)) { *out=saved_txn; return txn_error; }
    assert(handle==17 && key_index(key)==1);
    if (code[1]) return code[1];
    *out=saved_port; return 0;
}
static fnos_pair_cfg_t s_cfg;
static bool s_provisioned;
'''
MAIN = r'''
static void reset(void) {
    txn_error=ESP_ERR_NVS_NOT_FOUND; saved_txn=NVS_TXN_DIRTY;
    opened=parse_error=closes=parses=0; fail_alloc=false; saved_port=24680;
    memset(code,0,sizeof code);
    values[0]="fixture.invalid"; values[2]="anonymous-paired-token";
    values[3]="-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----";
    values[4]="fixture-fingerprint";
}
static void rejected(void) { assert(!nvs_load() && !s_provisioned && !s_cfg.token[0]); }
int main(void) {
    reset(); assert(nvs_load() && s_provisioned && s_cfg.tls && closes==1 && parses==1);
    reset(); code[3]=code[4]=1; assert(nvs_load() && s_provisioned && !s_cfg.tls);
    reset(); opened=1; assert(nvs_load() && !s_provisioned && !strcmp(s_cfg.token,FNOS_TOKEN) && !closes);
    reset(); for (int i=0;i<5;i++) code[i]=1;
    assert(nvs_load() && !s_provisioned && !strcmp(s_cfg.host,FNOS_HOST));
    reset(); opened=2; rejected(); assert(!closes);
    for (int i=0;i<5;i++) for (int e=1;e<=3;e++) {
        reset(); code[i]=e; rejected(); assert(closes==1);
    }
    reset(); values[0]=""; rejected();
    reset(); values[2]=""; rejected();
    reset(); saved_port=0; rejected(); reset(); saved_port=65536; rejected();
    reset(); values[3]=""; rejected(); reset(); values[3]="bad certificate"; rejected();
    reset(); values[4]=""; rejected();
    reset(); parse_error=-1; rejected(); assert(parses==1);
    reset(); fail_alloc=true; rejected(); assert(!parses);
    reset(); char oversize[FNOS_PAIR_PEM_MAX+1]; memset(oversize,'a',sizeof oversize); oversize[sizeof oversize-1]=0;
    values[3]=oversize; rejected();
    reset(); assert(nvs_load() && s_cfg.tls); code[3]=2; rejected(); // reboot/read error must drop old credential
    reset(); assert(nvs_load() && s_provisioned); // recovery after explicit repair
    reset(); txn_error=0; saved_txn=NVS_TXN_DIRTY; rejected();
    reset(); txn_error=2; rejected();
    reset(); txn_error=0; saved_txn=99; rejected();
    reset(); txn_error=0; saved_txn=NVS_TXN_EMPTY; rejected();
    reset(); txn_error=0; saved_txn=NVS_TXN_READY; assert(nvs_load() && s_provisioned);
    reset(); txn_error=0; saved_txn=NVS_TXN_READY; for(int i=0;i<5;i++)code[i]=1; rejected();
    reset(); txn_error=0; saved_txn=NVS_TXN_EMPTY; for(int i=0;i<5;i++)code[i]=1;
    assert(nvs_load() && !s_provisioned);
    puts("PASS: stored TLS/no-TLS, empty store, per-key missing/read/type faults, oversize, parse/OOM, invalid fields, reboot recovery");
}
'''
def main():
    program=PRE+'\n'+'\n'.join(function(n) for n in ('has_cert','stored_cert_valid','nvs_load'))+'\n'+MAIN
    with tempfile.TemporaryDirectory() as tmp:
        c=Path(tmp)/'nvs.c'; binary=Path(tmp)/'nvs'; c.write_text(program)
        command=['cc','-std=c11','-UNDEBUG','-fsanitize=address,undefined','-I',str(ROOT/'components/fnos_monitor'),str(c),'-o',str(binary)]
        subprocess.run(command,check=True); subprocess.run([str(binary)],check=True,cwd=tmp)
        mutant=program.replace('ec != ESP_ERR_NVS_NOT_FOUND || ef != ESP_ERR_NVS_NOT_FOUND','false')
        if mutant==program: raise RuntimeError('negative control no longer matches')
        c.write_text(mutant); subprocess.run(command,check=True)
        result=subprocess.run([str(binary)],capture_output=True,cwd=tmp)
        if result.returncode==0: raise RuntimeError('negative control accepted unreadable TLS/orphan fingerprint')
        print('PASS: negative control rejects cert-read-error downgrade')

if __name__ == "__main__":
    main()
