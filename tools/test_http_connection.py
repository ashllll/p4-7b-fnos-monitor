#!/usr/bin/env python3
"""Run production HTTP reader against scripted complete/partial response lifetimes."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'components/fnos_monitor/fnos_data.c').read_text()

def function(name):
    match = re.search(r'^static [\w *]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError(name)
    start = source.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define POLL_TIMEOUT_MS 100
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
typedef int esp_err_t;
typedef void *esp_http_client_handle_t;
typedef struct { const char *url, *cert_pem; int timeout_ms, buffer_size, buffer_size_tx; bool keep_alive_enable, disable_auto_redirect, skip_cert_common_name_check; } esp_http_client_config_t;
static esp_http_client_handle_t s_client;
static bool s_conn_usable=true, s_conn_tls=true;
static unsigned s_conn_gen=1;
static char s_conn_err[24], s_token[10]="token", s_cert_pem[10]="cert";
static bool persistent=true, partial, complete, fail_headers;
static int creates, cleanups, reads;
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) { CHECK(c->cert_pem==s_cert_pem); creates++; return (void *)1; }
static void esp_http_client_cleanup(esp_http_client_handle_t c) { CHECK(c); CHECK(complete || partial || fail_headers); cleanups++; }
static void esp_http_client_set_url(esp_http_client_handle_t c, const char *s) { (void)c;(void)s; }
static void esp_http_client_set_header(esp_http_client_handle_t c,const char *k,const char *v) { (void)c;(void)k;CHECK(!strcmp(v,"token")); }
static int64_t esp_timer_get_time(void) { return 0; }
static const char *classify_conn_err(int e) { (void)e; return NULL; }
static int esp_http_client_open(esp_http_client_handle_t c,int n) { CHECK(c);(void)n;complete=false;return 0; }
static int esp_http_client_fetch_headers(esp_http_client_handle_t c) { CHECK(c);return fail_headers ? -1 : 2; }
static int esp_http_client_get_status_code(esp_http_client_handle_t c) { CHECK(c);return 200; }
static bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { CHECK(c);return complete; }
static bool esp_http_client_is_persistent_connection(esp_http_client_handle_t c) { CHECK(c);CHECK(complete);return persistent; }
static void *heap_caps_realloc(void *p,size_t n,int caps) { (void)caps;return realloc(p,n); }
static int esp_http_client_read(esp_http_client_handle_t c,char *b,int n) { CHECK(c);CHECK(n>=2);reads++; if(partial)return 0;memcpy(b,"{}",2);complete=true;return 2; }
'''
main = r'''
int main(void) {
 char *body=NULL;size_t cap=0;int status=0,len=0,ms=0;
 for(int i=0;i<2;i++) { CHECK(http_get("https://test",&body,&cap,4096,&status,&len,&ms));CHECK(!strcmp(body,"{}")); }
 CHECK(creates==1 && cleanups==0 && reads==2);
 persistent=false;
 CHECK(http_get("https://test",&body,&cap,4096,&status,&len,&ms));
 CHECK(status==200 && len==2 && !strcmp(body,"{}") && s_client==NULL && cleanups==1);
 persistent=true;
 CHECK(http_get("https://test",&body,&cap,4096,&status,&len,&ms));CHECK(creates==2 && cleanups==1);
 partial=true;
 CHECK(!http_get("https://test",&body,&cap,4096,&status,&len,&ms));CHECK(s_client==NULL && cleanups==2);
 partial=false;fail_headers=true;
 CHECK(!http_get("https://test",&body,&cap,4096,&status,&len,&ms));CHECK(s_client==NULL && cleanups==3);
 fail_headers=false;
 CHECK(http_get("https://test",&body,&cap,4096,&status,&len,&ms));CHECK(creates==4 && cleanups==3);
 free(body);puts("PASS: keepalive reuse, complete close response, reconnect, truncated body, header failure");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='fnos-http-test-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(harness+'\n'+function('http_drop_client')+'\n'+function('http_get')+'\n'+main)
    for flags in [[], ['-DNDEBUG']]:
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2',*flags,str(p/'test.c'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
