#!/usr/bin/env python3
"""Production NVS barrier/save/wipe/load with simulated failures and reboot.
No flash hardware: immediate-write and buffered/partial-commit models only.
"""
import subprocess
import tempfile
from pathlib import Path
from test_pair_nvs import ROOT, PRE, function

prefix = PRE[:PRE.index('static int key_index')]
prefix = prefix.replace('#define NVS_READONLY 0', '#define NVS_READONLY 0\n#define NVS_READWRITE 1')
program = prefix + r'''
#include <setjmp.h>
typedef struct { bool exists[6]; char text[6][FNOS_PAIR_PEM_MAX]; int32_t port, txn; } Store;
static Store disk, cache;
static int buffered, step, fault, after, cut, partial;
static bool rw;
static jmp_buf reboot;
static int index_of(const char *key) {
    const char *keys[]={KEY_HOST,KEY_PORT,KEY_TOKEN,KEY_CERT,KEY_FP,KEY_TXN};
    for(int i=0;i<6;i++) if(!strcmp(key,keys[i])) return i;
    abort();
}
static int nvs_open(const char *name,int mode,nvs_handle_t *out) {
    (void)name; *out=17; rw=mode==NVS_READWRITE; cache=disk; return 0;
}
static void nvs_close(nvs_handle_t h) { assert(h==17); }
static int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *len) {
    assert(h==17); int i=index_of(key); if(!disk.exists[i])return 1;
    size_t n=strlen(disk.text[i])+1; if(n>*len)return 2;
    memcpy(out,disk.text[i],n); *len=n; return 0;
}
static int nvs_get_i32(nvs_handle_t h,const char *key,int32_t *out) {
    assert(h==17); int i=index_of(key); assert(i==1||i==5);
    if(!disk.exists[i]) return 1; *out=i==1?disk.port:disk.txn; return 0;
}
static int interrupted(void) { if(cut)longjmp(reboot,1); return 2; }
static int begin_write(void) { step++; return step==fault&&!after ? interrupted() : 0; }
static int end_write(void) { return step==fault&&after ? interrupted() : 0; }
static Store *target(void) { assert(rw); return buffered?&cache:&disk; }
static int nvs_set_str(nvs_handle_t h,const char *key,const char *value) {
    assert(h==17); if(begin_write())return 2; int i=index_of(key); Store *s=target();
    s->exists[i]=true; snprintf(s->text[i],sizeof(s->text[i]),"%s",value); return end_write();
}
static int nvs_set_i32(nvs_handle_t h,const char *key,int32_t value) {
    assert(h==17); if(begin_write())return 2; int i=index_of(key); Store *s=target();
    s->exists[i]=true; if(i==1)s->port=value;else { assert(i==5);s->txn=value; } return end_write();
}
static int nvs_erase_key(nvs_handle_t h,const char *key) {
    assert(h==17); if(begin_write())return 2; int i=index_of(key); Store *s=target();
    bool existed=s->exists[i]; s->exists[i]=false; int e=end_write(); return e?e:(existed?0:1);
}
static int nvs_commit(nvs_handle_t h) {
    assert(h==17); if(begin_write())return 2;
    if(buffered) {
        /* A failing commit may have persisted any prefix of the staged keys. */
        int limit=step==fault&&after?partial:6;
        for(int i=0;i<limit;i++) {
            disk.exists[i]=cache.exists[i]; memcpy(disk.text[i],cache.text[i],sizeof disk.text[i]);
            if(i==1)disk.port=cache.port; if(i==5)disk.txn=cache.txn;
        }
    }
    return end_write();
}
static fnos_pair_cfg_t s_cfg;
static bool s_provisioned;
''' + '\n'.join(function(n) for n in ('has_cert','stored_cert_valid','nvs_load','nvs_barrier','nvs_save','nvs_wipe')) + r'''
static fnos_pair_cfg_t old_cfg, next_cfg;
static Store initial;
static void set_cfg(fnos_pair_cfg_t *c,const char *host,const char *token,int port,bool tls) {
    memset(c,0,sizeof *c); snprintf(c->host,sizeof c->host,"%s",host);
    snprintf(c->token,sizeof c->token,"%s",token); c->port=port;c->tls=tls;
    if(tls) {
        snprintf(c->cert_pem,sizeof c->cert_pem,"-----BEGIN CERTIFICATE-----\n%s\n-----END CERTIFICATE-----",host);
        snprintf(c->fingerprint,sizeof c->fingerprint,"fp-%s",host);
    }
}
static bool same(const fnos_pair_cfg_t *a,const fnos_pair_cfg_t *b) {
    return !strcmp(a->host,b->host)&&a->port==b->port&&!strcmp(a->token,b->token)&&a->tls==b->tls&&
        !strcmp(a->cert_pem,b->cert_pem)&&!strcmp(a->fingerprint,b->fingerprint);
}
static void check_reboot(bool wipe) {
    fault=0; cache=disk;
    if(!nvs_load()) { assert(!s_provisioned&&!s_cfg.token[0]); return; }
    if(!s_provisioned) { assert(wipe); return; }
    assert(same(&s_cfg,&old_cfg)||(!wipe&&same(&s_cfg,&next_cfg)));
}
static void exercise(bool wipe) {
    disk=initial; fault=0; step=0;
    assert(wipe?nvs_wipe():nvs_save(&next_cfg)); int count=step;
    assert(nvs_load() && (wipe?!s_provisioned:same(&s_cfg,&next_cfg)));
    for(int f=1;f<=count;f++) for(int a=0;a<2;a++) for(int c=0;c<2;c++)
        for(int p=0;p<=6;p++) {
            disk=initial; step=0;fault=f;after=a;cut=c;partial=p;
            if(!setjmp(reboot)) assert(!(wipe?nvs_wipe():nvs_save(&next_cfg)));
            check_reboot(wipe);
            /* Explicit retry repairs a DIRTY store and publishes a complete record. */
            step=0;fault=0; assert(wipe?nvs_wipe():nvs_save(&next_cfg));
            assert(nvs_load()&&(wipe?!s_provisioned:same(&s_cfg,&next_cfg)));
        }
}
int main(void) {
    int cases=0;
    for(buffered=0;buffered<=1;buffered++) for(int legacy=0;legacy<2;legacy++)
        for(int old_tls=0;old_tls<2;old_tls++) for(int new_tls=0;new_tls<2;new_tls++) {
            memset(&disk,0,sizeof disk); fault=0;step=0;
            set_cfg(&old_cfg,"old.invalid","anonymous-old",8798,old_tls);
            set_cfg(&next_cfg,"new.invalid","anonymous-new",24680,new_tls);
            assert(nvs_save(&old_cfg)); if(legacy)disk.exists[5]=false; initial=disk;
            exercise(false); exercise(true); cases+=2;
        }
    printf("PASS: %d transitions, failure before/after every write/erase/commit, reboot, partial commit, retry\n",cases);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c=Path(tmp)/'transaction.c'; binary=Path(tmp)/'transaction'; c.write_text(program)
    cmd=['cc','-std=c11','-UNDEBUG','-fsanitize=address,undefined','-I',str(ROOT/'components/fnos_monitor'),str(c),'-o',str(binary)]
    subprocess.run(cmd,check=True); subprocess.run([str(binary)],check=True,cwd=tmp)
    mutant=program.replace('bool ok = nvs_barrier(h, NVS_TXN_DIRTY) &&', 'bool ok = true &&',1)
    if mutant==program: raise RuntimeError('barrier negative control no longer matches')
    c.write_text(mutant); subprocess.run(cmd,check=True)
    result=subprocess.run([str(binary)],capture_output=True,cwd=tmp)
    if not result.returncode: raise RuntimeError('negative control accepted unguarded mixed-generation writes')
    print('PASS: removing the pre-write barrier is rejected')
