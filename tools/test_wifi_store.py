#!/usr/bin/env python3
"""Whole production Wi-Fi NVS module with immediate-write faults/reboot and task races.
NVS and FreeRTOS are host models; no flash power-cut or device timing claim.
"""
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'components/fnos_monitor/fnos_wifi_store.c').read_text()
PRE=r'''
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "nvs.h"
typedef struct { bool exists[3]; char ssid[128],pass[128];int32_t txn; } Store;
static Store disk;
static int step, fault, after, reboot_mode, read_error=-1, open_error;
static jmp_buf reboot;
static int key(const char *k) { if(!strcmp(k,"ssid"))return 0;if(!strcmp(k,"pass"))return 1;assert(!strcmp(k,"txn_v1"));return 2; }
static int write_begin(void) {step++;if(step==fault&&!after){if(reboot_mode)longjmp(reboot,1);return 99;}return 0;}
static int write_end(void) {if(step==fault&&after){if(reboot_mode)longjmp(reboot,1);return 99;}return 0;}
static int nvs_open(const char *name,int mode,nvs_handle_t *h) {(void)mode;assert(!strcmp(name,"wifi"));if(open_error)return open_error;*h=17;return 0;}
static void nvs_close(nvs_handle_t h) {assert(h==17);}
static int nvs_get_str(nvs_handle_t h,const char *k,char *out,size_t *size) {
 assert(h==17);int i=key(k);assert(i<2);if(read_error==i)return 99;if(!disk.exists[i])return ESP_ERR_NVS_NOT_FOUND;
 const char *s=i?disk.pass:disk.ssid;size_t n=strlen(s)+1;if(*size<n)return 99;memcpy(out,s,n);*size=n;return 0;
}
static int nvs_get_i32(nvs_handle_t h,const char *k,int32_t *out) {assert(h==17&&key(k)==2);if(read_error==2)return 99;if(!disk.exists[2])return ESP_ERR_NVS_NOT_FOUND;*out=disk.txn;return 0;}
static int nvs_set_str(nvs_handle_t h,const char *k,const char *s) {
 assert(h==17);int i=key(k);assert(i<2);if(write_begin())return 99;
 snprintf(i?disk.pass:disk.ssid,128,"%s",s);disk.exists[i]=true;return write_end();
}
static int nvs_set_i32(nvs_handle_t h,const char *k,int32_t x) {assert(h==17&&key(k)==2);if(write_begin())return 99;disk.exists[2]=true;disk.txn=x;return write_end();}
static int nvs_erase_key(nvs_handle_t h,const char *k) {assert(h==17);int i=key(k);if(write_begin())return 99;bool had=disk.exists[i];disk.exists[i]=false;int e=write_end();return e?e:(had?0:ESP_ERR_NVS_NOT_FOUND);}
static int nvs_commit(nvs_handle_t h) {assert(h==17);if(write_begin())return 99;return write_end();}
'''
MAIN=r'''
static void reset_fault(void) { fault=step=after=reboot_mode=open_error=0;read_error=-1;atomic_flag_clear(&s_store_guard); }
static void check_load(bool expected,const char *ss,const char *pw) {
 char s[64],p[96];memset(s,'x',sizeof s);memset(p,'x',sizeof p);
 assert(fnos_wifi_store_load(s,sizeof s,p,sizeof p)==expected);
 if(expected)assert(!strcmp(s,ss)&&!strcmp(p,pw));else assert(!s[0]&&!p[0]);
}
static void setup(int mode) {
 memset(&disk,0,sizeof disk);reset_fault();
 if(mode<3){disk.exists[0]=disk.exists[1]=true;strcpy(disk.ssid,"old-network");strcpy(disk.pass,mode==2?"":"old-password");}
 if(mode==0){disk.exists[2]=true;disk.txn=WIFI_STORE_READY;}
}
static void transitions(void) {
 int cases=0;
 for(int mode=0;mode<4;mode++)for(int clear=0;clear<2;clear++)for(int open=0;open<2;open++) {
  setup(mode);Store initial=disk;bool result=clear?fnos_wifi_store_clear():fnos_wifi_store_save("new-network",open?"":"new-password");assert(result);int count=step;assert(count==7);
  for(int n=1;n<=count;n++)for(int post=0;post<2;post++)for(int boot=0;boot<2;boot++) {
   disk=initial;reset_fault();fault=n;after=post;reboot_mode=boot;
   if(!setjmp(reboot)) {bool ok=clear?fnos_wifi_store_clear():fnos_wifi_store_save("new-network",open?"":"new-password");assert(!ok);}
   reset_fault();char s[64],p[96];bool ok=fnos_wifi_store_load(s,sizeof s,p,sizeof p);
   if(ok) {
    bool old=mode<3&&!strcmp(s,"old-network")&&!strcmp(p,mode==2?"":"old-password");
    bool def=!strcmp(s,"default-network")&&!strcmp(p,"default-password");
    bool next=!clear&&!strcmp(s,"new-network")&&!strcmp(p,open?"":"new-password");
    assert(old||def||next);
   } else assert(!s[0]&&!p[0]);
   assert(clear?fnos_wifi_store_clear():fnos_wifi_store_save("new-network",open?"":"new-password"));
   check_load(true,clear?"default-network":"new-network",clear?"default-password":open?"":"new-password");cases++;
  }
 }
 printf("fault/reboot transition cases: %d\n",cases);
}
static void reads(void) {
 setup(0);for(int i=0;i<3;i++){read_error=i;check_load(false,"","");}read_error=-1;
 for(int i=0;i<2;i++){setup(0);disk.exists[i]=false;check_load(false,"","");}
 setup(0);disk.txn=WIFI_STORE_DIRTY;check_load(false,"","");disk.txn=99;check_load(false,"","");
 disk.txn=WIFI_STORE_EMPTY;check_load(false,"","");
 setup(3);disk.exists[2]=true;disk.txn=WIFI_STORE_READY;check_load(false,"","");
 setup(1);disk.exists[1]=false;check_load(false,"","");
 setup(2);check_load(true,"old-network","");
 setup(0);memset(disk.pass,'p',100);disk.pass[100]=0;check_load(false,"","");
 setup(0);open_error=99;check_load(false,"","");open_error=ESP_ERR_NVS_NOT_FOUND;check_load(true,"default-network","default-password");
 setup(0);char s[2],p[2];assert(!fnos_wifi_store_load(s,sizeof s,p,sizeof p)&&!s[0]&&!p[0]);
 assert(!fnos_wifi_store_load(s,0,NULL,0));assert(!fnos_wifi_store_load(NULL,0,p,0));
 assert(fnos_wifi_store_load(NULL,0,NULL,0));
 char long_name[34];memset(long_name,'a',33);long_name[33]=0;int old_step=step;assert(!fnos_wifi_store_save(long_name,"x")&&step==old_step);
}
static atomic_int finished;
static void *writer(void *arg) {const char *text=arg;for(int i=0;i<100;i++)assert(fnos_wifi_store_save(text,text));atomic_fetch_add(&finished,1);return NULL;}
static void *reader(void *unused) {(void)unused;while(atomic_load(&finished)<2){char s[64],p[96];assert(fnos_wifi_store_load(s,sizeof s,p,sizeof p));assert(!strcmp(s,p));}return NULL;}
int main(void) {
 transitions();reads();reset_fault();assert(fnos_wifi_store_save("seed","seed"));
 pthread_t a,b,c;assert(!pthread_create(&a,NULL,writer,"writer-one"));assert(!pthread_create(&b,NULL,writer,"writer-two"));assert(!pthread_create(&c,NULL,reader,NULL));
 assert(!pthread_join(a,NULL)&&!pthread_join(b,NULL)&&!pthread_join(c,NULL));
 puts("PASS reads/capacities/open-network/invalid-states/two-writers+reader");return 0;
}
'''
HEADERS={
'nvs.h':'''#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define NVS_READONLY 0
#define NVS_READWRITE 1
''',
'esp_log.h':'''#define ESP_LOGE(...) ((void)TAG)
#define ESP_LOGI(...) ((void)TAG)
''',
'fnos_config.h':'#define APP_WIFI_SSID "default-network"\n#define APP_WIFI_PASS "default-password"\n',
'freertos/FreeRTOS.h':'#pragma once\n',
'freertos/task.h':'static void vTaskDelay(int ticks) {(void)ticks;sched_yield();}\n'}
with tempfile.TemporaryDirectory(prefix='fnos-wifi-store-') as directory:
 d=Path(directory)
 for name,body in HEADERS.items():
  p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body)
 for negative in (False,True):
  candidate=SOURCE.replace('err = store_barrier(h, WIFI_STORE_DIRTY);','err = ESP_OK;') if negative else SOURCE
  if negative and candidate==SOURCE:raise AssertionError('barrier negative control did not change source')
  c=d/'wifi.c';c.write_text(PRE+candidate+MAIN);binary=d/'wifi'
  subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function',
                  '-pthread','-fsanitize=address,undefined',
                  '-I',str(d),'-I',str(ROOT/'components/fnos_monitor'),str(c),'-o',str(binary)],check=True)
  result=subprocess.run([str(binary)],capture_output=True)
  if negative==(result.returncode==0):raise AssertionError('barrier negative control survived' if negative else result.stderr.decode())
  if not negative:print(result.stdout.decode(),end='')
print('PASS: removed DIRTY barrier negative control rejected; host NVS/RTOS model only')
