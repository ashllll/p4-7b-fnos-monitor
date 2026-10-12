#!/usr/bin/env python3
"""Extract production cert_info; build the installed IDF mbedTLS X509 on host.
Anonymous temporary certificates only; heap allocation is injected, X509 is real.
"""
import datetime
import os
import re
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MB=Path(os.environ.get('IDF_PATH',str(Path.home()/'.platformio/packages/framework-espidf@3.50503.0')))/'components/mbedtls/mbedtls'
if not (MB/'CMakeLists.txt').is_file():raise RuntimeError('installed IDF mbedTLS source missing')
src=(ROOT/'components/fnos_monitor/fnos_pair.c').read_text()
m=re.search(r'^static void cert_info\([^;]*?\)\s*\{',src,re.M)
if not m:raise RuntimeError('production cert_info missing')
a=src.index('{',m.start());b=a+1;depth=1
while depth:
 depth+=(src[b]=='{')-(src[b]=='}');b+=1
fn=src[m.start():b]
PRE=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/x509_crt.h"
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_8BIT 2
static bool psram_ok, fallback_fail;
static int psram_calls,fallback_calls,frees;
static void *heap_caps_calloc(size_t n,size_t s,int cap) {assert(cap==6);psram_calls++;return psram_ok?calloc(n,s):NULL;}
static void *host_calloc(size_t n,size_t s) {fallback_calls++;return fallback_fail?NULL:calloc(n,s);}
static void host_free(void *p) {frees++;free(p);}
#define calloc host_calloc
#define free host_free
'''
MAIN=r'''
#undef calloc
#undef free
int main(int argc,char **argv) {
 assert(argc==4);FILE *f=fopen(argv[1],"rb");assert(f);char pem[8192];size_t n=fread(pem,1,sizeof pem-1,f);assert(!ferror(f));fclose(f);pem[n]=0;
 char cn[96],until[16];
 for(int psram=0;psram<2;psram++) {
  psram_ok=psram;fallback_fail=false;psram_calls=fallback_calls=frees=0;
  memset(cn,'x',sizeof cn);memset(until,'x',sizeof until);cert_info(pem,cn,sizeof cn,until,sizeof until);
  if(!strcmp(argv[3],"normal"))assert(!strcmp(cn,"CN=cert-info-fixture"));else assert(!cn[0]);
  assert(!strcmp(until,argv[2]));assert(psram_calls==1&&fallback_calls==!psram&&frees==1);
 }
 psram_ok=false;fallback_fail=true;frees=0;
 cert_info(pem,cn,sizeof cn,until,sizeof until);assert(!cn[0]&&!until[0]&&!frees);
 fallback_fail=false;
 cert_info("broken PEM",cn,sizeof cn,until,sizeof until);assert(!cn[0]&&!until[0]);
 cert_info("",cn,sizeof cn,until,sizeof until);assert(!cn[0]&&!until[0]);
 char tiny_cn[1]={'x'},tiny_until[1]={'x'};cert_info(pem,tiny_cn,1,tiny_until,1);assert(!tiny_cn[0]&&!tiny_until[0]);
 puts("PASS real X509 subject/date/PSRAM/fallback/OOM/malformed/tiny buffers");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='fnos-cert-info-') as directory:
 d=Path(directory);build=d/'build'
 log=d/'build.log'
 with log.open('w') as out:
  for args in (
   ['cmake','-S',str(MB),'-B',str(build),'-DENABLE_PROGRAMS=OFF','-DENABLE_TESTING=OFF','-DCMAKE_C_FLAGS=-fsanitize=address,undefined'],
   ['cmake','--build',str(build),'--target','mbedx509','-j','4']):
   result=subprocess.run(args,stdout=out,stderr=subprocess.STDOUT)
   if result.returncode:raise RuntimeError('host mbedTLS build failed: '+log.read_text()[-5000:])
 c=d/'info.c';c.write_text(PRE+fn+MAIN);exe=d/'info'
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                 '-I',str(MB/'include'),str(c),str(build/'library/libmbedx509.a'),str(build/'library/libmbedcrypto.a'),'-o',str(exe)],check=True)
 for kind,subject in (('normal','/CN=cert-info-fixture'),('long','/CN=cert-info-fixture/O='+'x'*64+'/OU='+'y'*64)):
  pem=d/'cert.pem';key=d/'key.pem'
  subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(key),'-out',str(pem),'-days','1','-subj',subject],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  text=subprocess.check_output(['openssl','x509','-in',str(pem),'-noout','-enddate'],text=True).strip().split('=',1)[1]
  expected=datetime.datetime.strptime(text,'%b %d %H:%M:%S %Y %Z').strftime('%Y-%m-%d')
  subprocess.run([str(exe),str(pem),expected,kind],check=True)
print('PASS: production cert_info with actual installed IDF mbedTLS; no real NAS/TLS handshake')
