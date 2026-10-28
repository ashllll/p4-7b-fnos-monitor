#!/usr/bin/env python3
"""Compile production Wi-Fi submission functions with deterministic host event stubs."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'components/fnos_monitor/fnos_net.c').read_text()

def function(name):
    match = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('missing production function: ' + name)
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

preamble = r'''
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_OK 0
#define WIFI_IF_STA 0
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
#define FNOS_WIFI_SUBMIT_IDLE 0
#define FNOS_WIFI_SUBMIT_PENDING 1
#define FNOS_WIFI_SUBMIT_FAILED 2
#define WIFI_SUBMIT_TIMEOUT_US 30000000LL
#define WIFI_REASON_TEXT_MAX 192
#define WIFI_REASON_ASSOC_LEAVE 8
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_SCAN_DONE 3
#define WIFI_EVENT_STA_CONNECTED 4
#define IP_EVENT_STA_GOT_IP 1
#define IP_EVENT_STA_LOST_IP 2
#define BIT_IP 1
#define TAG "test"
typedef int esp_event_base_t;
typedef int esp_err_t;
typedef int wifi_auth_mode_t;
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct { uint8_t ssid[32]; uint8_t ssid_len; } wifi_event_sta_connected_t;
typedef struct { struct { struct { uint32_t addr; } ip; } ip_info; } ip_event_got_ip_t;
typedef struct { struct { uint8_t ssid[32], password[64]; struct { int authmode; } threshold; } sta; } wifi_config_t;
static int s_cfg_mux, s_ip_mux, s_submit_state;
static uint32_t s_submit_generation, s_submit_applied_generation, s_submit_associated_generation;
static char s_pend_ssid[64], s_pend_pass[96], s_ssid[64], s_reason[128], s_submit_reason[128], s_ip[16];
static bool s_cred_want, s_wifi_inited;
static atomic_bool s_online, s_configured, s_scan_done, s_scan_want, s_scan_busy, s_connect_wanted, s_reassociate;
static bool s_started = true;
static _Atomic uint8_t s_wifi_retry_count;
static void *s_eg, *s_wifi_retry_timer;
static int64_t s_submit_started_us, s_retry_due_us, now_us;
static bool save_ok = true, resubmit_during_save;
static int config_rc, connect_rc, connect_calls, disconnect_calls;
static int64_t esp_timer_get_time(void) { return now_us; }
static int esp_wifi_connect(void) { connect_calls++; return connect_rc; }
static int esp_wifi_disconnect(void) { disconnect_calls++; return 0; }
static wifi_config_t captured;
static int esp_wifi_set_config(int i, wifi_config_t *c) { (void)i; captured=*c; return config_rc; }
static void wifi_schedule_retry(void);
static void mark_offline(void) { s_online = false; }
static const char *reason_text(uint8_t r) { (void)r; return "disconnect"; }
static void esp_ip4addr_ntoa(void *a, char *s, size_t n) { (void)a; snprintf(s,n,"127.0.0.1"); }
static void esp_timer_stop(void *t) { (void)t; }
static void xEventGroupSetBits(void *g, int b) { (void)g; (void)b; }
static void fnos_data_network_changed(void) {}
static void sntp_start_once(void) {}
static void scan_begin(void) {}
static void scan_collect(void) {}
static void fnos_net_start(void) {}
void fnos_net_set_credentials(const char *, const char *);
static bool fnos_wifi_store_save(const char *s, const char *p) {
    (void)s; (void)p;
    if (resubmit_during_save) {
        resubmit_during_save = false;
        fnos_net_set_credentials("new-network", "new-pass");
    }
    return save_ok;
}
'''
checks = r'''
static void ip(void) {
    ip_event_got_ip_t e = {.ip_info.ip.addr = 1};
    on_net_event(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &e);
}
static void associate(const char *ssid) {
    wifi_event_sta_connected_t e = {0};
    e.ssid_len = strlen(ssid); memcpy(e.ssid, ssid, e.ssid_len);
    on_net_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &e);
}
int main(void) {
    assert(wifi_authmode_for_password("") == WIFI_AUTH_OPEN);
    assert(wifi_authmode_for_password("pass") == WIFI_AUTH_WPA2_PSK);
    fnos_net_set_credentials("test-network", "pass");
    ip(); assert(s_submit_state == FNOS_WIFI_SUBMIT_PENDING); /* stale old IP */
    save_ok = false;
    fnos_net_service(); assert(s_submit_state == FNOS_WIFI_SUBMIT_FAILED);
    ip(); assert(s_submit_state == FNOS_WIFI_SUBMIT_FAILED); /* NVS failure persists */
    assert(strstr(s_submit_reason, "NVS"));
    save_ok = true;
    fnos_net_set_credentials("test-network", "pass");
    fnos_net_service();
    wifi_event_sta_disconnected_t leave = {.reason = WIFI_REASON_ASSOC_LEAVE};
    on_net_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &leave);
    assert(s_submit_state == FNOS_WIFI_SUBMIT_PENDING); /* intentional old disconnect */
    ip(); assert(s_submit_state == FNOS_WIFI_SUBMIT_PENDING); /* not associated yet */
    associate("wrong-network"); ip(); assert(s_submit_state == FNOS_WIFI_SUBMIT_PENDING);
    associate("test-network");
    on_net_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &leave);
    ip(); assert(s_submit_state == FNOS_WIFI_SUBMIT_PENDING); /* association was lost */
    associate("test-network"); ip(); assert(s_submit_state == FNOS_WIFI_SUBMIT_IDLE);
    fnos_net_set_credentials("test-network", "pass");
    save_ok = false; resubmit_during_save = true;
    fnos_net_service();
    assert(s_submit_state == FNOS_WIFI_SUBMIT_PENDING && s_cred_want);
    assert(!strcmp(s_pend_ssid, "new-network")); /* older save failure cannot clobber */
    save_ok = true; fnos_net_service();
    associate("new-network"); ip(); assert(s_submit_state == FNOS_WIFI_SUBMIT_IDLE);
    config_rc = -1;
    fnos_net_set_credentials("test-network", "pass"); fnos_net_service();
    assert(s_submit_state == FNOS_WIFI_SUBMIT_FAILED);
    config_rc = 0; connect_rc = -1;
    fnos_net_set_credentials("test-network", "pass"); fnos_net_service();
    assert(s_submit_state == FNOS_WIFI_SUBMIT_FAILED);
    connect_rc = 0;
    fnos_net_set_credentials("test-network", "pass"); fnos_net_service();
    now_us += WIFI_SUBMIT_TIMEOUT_US + 1; fnos_net_service();
    assert(s_submit_state == FNOS_WIFI_SUBMIT_FAILED);
    ip(); assert(s_submit_state == FNOS_WIFI_SUBMIT_FAILED);
    char full_ssid[33], full_pass[65];
    memset(full_ssid,'S',32); full_ssid[32]=0;
    memset(full_pass,'a',64); full_pass[64]=0;
    fnos_net_set_credentials(full_ssid,full_pass); fnos_net_service();
    assert(memcmp(captured.sta.ssid,full_ssid,32)==0);
    assert(memcmp(captured.sta.password,full_pass,64)==0);
    s_online=false; s_configured=true; s_connect_wanted=false;
    s_wifi_retry_count=0; now_us=100000000; wifi_schedule_retry();
    assert(s_retry_due_us==101000000);
    int before=connect_calls;
    now_us=100999999; fnos_net_service(); assert(connect_calls==before);
    now_us=101000000; connect_rc=1; fnos_net_service();
    assert(connect_calls==before+1); assert(s_retry_due_us==102000000);
    now_us=102000000; connect_rc=0; fnos_net_service();
    assert(connect_calls==before+2); assert(s_retry_due_us==0);
    s_wifi_retry_count=15; wifi_schedule_retry();
    assert(s_retry_due_us==now_us+30000000);
    ip(); assert(s_retry_due_us==0);
    s_online=false; s_configured=false; wifi_schedule_retry(); assert(s_retry_due_us==0);
    fnos_net_set_credentials("open-network", ""); fnos_net_service();
    assert(captured.sta.threshold.authmode == WIFI_AUTH_OPEN);
    /* LOST_IP must ask the service task to force a fresh association; the event
       callback itself must not issue the ESP-Hosted RPC. */
    s_online=true; s_configured=true; s_connect_wanted=false;
    disconnect_calls=0;
    on_net_event(NULL, IP_EVENT, IP_EVENT_STA_LOST_IP, NULL);
    assert(!s_online); assert(atomic_load(&s_reassociate)); assert(disconnect_calls==0);
    fnos_net_service();
    assert(disconnect_calls==1); assert(!atomic_load(&s_reassociate));
    puts("PASS: stale IP / NVS failure / association / resubmission / RPC failures / timeout / open authmode / lost-IP reassociation");
}
'''
names = ['wifi_authmode_for_password', 'wifi_schedule_retry', 'fnos_utf8_copy',
         'fnos_net_set_credentials', 'submit_failure', 'apply_pending_credentials',
         'on_net_event', 'fnos_net_service']
with tempfile.TemporaryDirectory(prefix='fnos-wifi-submit-') as tmp:
    c = Path(tmp) / 'submission.c'
    c.write_text(preamble + '\n'.join(function(n) for n in names) + checks)
    binary = Path(tmp) / 'submission'
    subprocess.run(['cc', '-std=gnu11', '-UNDEBUG', '-fsanitize=address,undefined',
                    str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, cwd=tmp)
    # Reintroduce the reviewed bug: any old IP event clears the latest result.
    guarded = """if (s_submit_state == FNOS_WIFI_SUBMIT_PENDING &&
            s_submit_applied_generation == s_submit_generation &&
            s_submit_associated_generation == s_submit_generation)"""
    mutant = c.read_text().replace(guarded, "if (s_submit_state != FNOS_WIFI_SUBMIT_IDLE)")
    if mutant == c.read_text():
        raise RuntimeError('negative control no longer matches IP event branch')
    c.write_text(mutant)
    subprocess.run(['cc', '-std=gnu11', '-UNDEBUG', str(c), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], cwd=tmp, capture_output=True)
    if result.returncode == 0:
        raise RuntimeError('negative control passed: stale IP regression was not detected')
    print('PASS: negative control rejects old-IP state overwrite')

with tempfile.TemporaryDirectory(prefix='fnos-net-width-') as tmp:
    original=preamble+'\n'.join(function(n) for n in names)+checks
    for field in ('ssid','password'):
        srcname='ssid' if field=='ssid' else 'pass'
        old='memcpy(cfg.sta.'+field+', '+srcname+', strnlen('+srcname+', sizeof cfg.sta.'+field+'));'
        replacement='snprintf((char *)cfg.sta.'+field+', sizeof(cfg.sta.'+field+'), "%s", '+srcname+');'
        mutant=original.replace(old,replacement)
        if mutant==original:raise RuntimeError('missing full-width config copy')
        c=Path(tmp)/'width.c';binary=Path(tmp)/'width';c.write_text(mutant)
        subprocess.run(['cc','-std=gnu11','-UNDEBUG',str(c),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],cwd=tmp,capture_output=True,timeout=20)
        if result.returncode==0 or ('captured.sta.'+field).encode() not in result.stderr:
            raise RuntimeError('old width negative failed to hit exact assertion')
    print('PASS: full-width SSID/password truncation negatives rejected')

with tempfile.TemporaryDirectory(prefix='fnos-net-deadline-') as tmp:
    program=preamble+'\n'.join(function(n) for n in names)+checks
    needle='bool retry_due = s_retry_due_us && now_us >= s_retry_due_us;'
    mutant=program.replace(needle,'bool retry_due = false;')
    if mutant==program:raise RuntimeError('missing retry due consumption')
    c=Path(tmp)/'deadline.c';binary=Path(tmp)/'deadline';c.write_text(mutant)
    subprocess.run(['cc','-std=gnu11','-UNDEBUG',str(c),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],cwd=tmp,capture_output=True,timeout=20)
    if result.returncode==0 or b'connect_calls==before+1' not in result.stderr:
        raise RuntimeError('missing retry fallback negative missed exact assertion')
    print('PASS: absent retry deadline consumption negative rejected')

# Production service/getters with real pthread locks and a deterministic producer
# arriving after consumption. This is host synchronization, not FreeRTOS timing.
thread_preamble = preamble.replace('#include <assert.h>', '#include <assert.h>\n#include <pthread.h>\n#include <sched.h>')
thread_preamble = thread_preamble.replace('#define portENTER_CRITICAL(x) ((void)0)', '#define portENTER_CRITICAL(x) pthread_mutex_lock(x)').replace('#define portEXIT_CRITICAL(x) ((void)0)', '#define portEXIT_CRITICAL(x) pthread_mutex_unlock(x)')
thread_preamble = thread_preamble.replace('static int s_cfg_mux, s_ip_mux, s_submit_state;', 'static pthread_mutex_t s_cfg_mux=PTHREAD_MUTEX_INITIALIZER, s_ip_mux=PTHREAD_MUTEX_INITIALIZER;\nstatic int s_submit_state;')
thread_preamble += r'''
#define WIFI_REASON_CAP 128
#define WIFI_STATE_STR_MAX 256
typedef int fnos_wifi_submit_t;
typedef int fnos_net_state_t;
#define FNOS_NET_CONNECTING 1
#define FNOS_NET_FAILED 3
#define FNOS_NET_UNCONFIGURED 0
#define FNOS_NET_ONLINE 2
void fnos_net_ssid_r(char *,size_t);
static atomic_bool rendezvous, producer_done;
static bool hook;
static atomic_bool *target;
static void after_take(atomic_bool *flag) {
    if (hook && flag==target) {
        atomic_store(&rendezvous,true);
        while(!atomic_load(&producer_done)) sched_yield();
    }
}
static bool take_request(atomic_bool *flag,bool value) {
    bool old=atomic_exchange(flag,value);
    after_take(flag); return old;
}
static bool old_take_request(atomic_bool *flag,bool value) {
    bool old=atomic_load(flag); after_take(flag);
    atomic_store(flag,value); return old;
}
#undef atomic_exchange
#define atomic_exchange(flag,value) take_request(flag,value)
'''
thread_checks = r'''
static void *producer(void *unused) {
    (void)unused;
    while(!atomic_load(&rendezvous)) sched_yield();
    atomic_store(target,true); atomic_store(&producer_done,true);
    return NULL;
}
static void *writer(void *unused) {
    (void)unused;
    for(int i=0;i<2000;i++) {
        portENTER_CRITICAL(&s_cfg_mux);
        memset(s_ssid,i%2?'A':'B',sizeof s_ssid-1); s_ssid[sizeof s_ssid-1]=0;
        s_submit_state=i%3;
        portEXIT_CRITICAL(&s_cfg_mux);
    }
    return NULL;
}
int main(void) {
    pthread_t p;
    atomic_bool *flags[]={&s_connect_wanted,&s_scan_want,&s_scan_done};
    s_wifi_inited=true;
    for(int i=0;i<3;i++) {
        target=flags[i]; hook=true; atomic_store(target,true);
        rendezvous=false; producer_done=false;
        assert(!pthread_create(&p,NULL,producer,NULL));
        fnos_net_service(); assert(!pthread_join(p,NULL));
        assert(atomic_load(target)); /* late producer is not erased */
        hook=false; fnos_net_service(); assert(!atomic_load(target));
    }
    strcpy(s_ssid,"snapshot"); const char *borrow=fnos_net_ssid();
    assert(borrow!=s_ssid); strcpy(s_ssid,"replacement"); assert(!strcmp(borrow,"snapshot"));
    memset(s_ssid,'A',sizeof s_ssid-1); s_ssid[sizeof s_ssid-1]=0;
    assert(!pthread_create(&p,NULL,writer,NULL));
    for(int i=0;i<2000;i++) {
        char out[64]; fnos_net_ssid_r(out,sizeof out);
        assert(strlen(out)==63);
        for(int j=1;j<63;j++) assert(out[j]==out[0]);
        assert(fnos_net_submit_state()>=0); (void)fnos_net_state();
    }
    assert(!pthread_join(p,NULL));
    puts("PASS: atomic consume/late producer, locked snapshots, legacy SSID copy");
}
'''
thread_names = ['wifi_authmode_for_password','wifi_schedule_retry', 'fnos_utf8_copy','fnos_net_set_credentials','submit_failure',
                'apply_pending_credentials','fnos_net_service','fnos_net_ssid_r',
                'fnos_net_ssid','fnos_net_submit_state','fnos_net_state']
with tempfile.TemporaryDirectory(prefix='fnos-net-threads-') as tmp:
    c=Path(tmp)/'threads.c'; binary=Path(tmp)/'threads'
    program=thread_preamble+'\n'.join(function(n) for n in thread_names)+thread_checks
    c.write_text(program)
    subprocess.run(['cc','-std=gnu11','-pthread','-UNDEBUG','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,cwd=tmp,timeout=20)
    mutant=program.replace('if (atomic_exchange(&s_connect_wanted, false) && !s_online && s_configured)',
                           'if (old_take_request(&s_connect_wanted, false) && !s_online && s_configured)')
    if mutant==program:raise RuntimeError('missing atomic consumption production expression')
    c.write_text(mutant)
    subprocess.run(['cc','-std=gnu11','-pthread','-UNDEBUG',str(c),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],cwd=tmp,capture_output=True,timeout=20)
    if result.returncode==0 or b'atomic_load(target)' not in result.stderr:
        raise RuntimeError('late request negative did not hit exact assertion')
    print('PASS: old read/clear loses late request negative rejected')

scan_preamble = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define ESP_OK 0
#define WIFI_AUTH_OPEN 0
#define FNOS_AP_MAX 24
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define TAG "fixture"
typedef int esp_err_t;
typedef struct {unsigned char ssid[33]; int8_t rssi; int authmode;} wifi_ap_record_t;
typedef struct {char ssid[33]; int8_t rssi; bool secure;} fnos_ap_t;
static fnos_ap_t s_aps[FNOS_AP_MAX];
static int s_ap_n, s_ap_mux, num_rc, records_rc;
static bool s_scan_busy,s_scan_failed,oom;
static uint16_t count;
static int esp_wifi_scan_get_ap_num(uint16_t *num) {*num=count;return num_rc;}
static int esp_wifi_scan_get_ap_records(uint16_t *got,wifi_ap_record_t *out) {
    for(int i=0;i<*got;i++) {strcpy((char*)out[i].ssid,"same");out[i].rssi=-60+i;out[i].authmode=1;}
    return records_rc;
}
static void *fixture_alloc(size_t n,size_t size) {return oom?NULL:calloc(n,size);}
#define calloc fixture_alloc
'''
scan_checks = r'''
int main(void) {
    s_ap_n=1;count=0;num_rc=1;s_scan_busy=true;
    scan_collect();assert(s_scan_failed);assert(!s_scan_busy);assert(s_ap_n==0);
    num_rc=0;scan_collect();assert(!s_scan_failed);assert(s_ap_n==0);
    count=2;oom=true;s_scan_busy=true;scan_collect();assert(s_scan_failed);assert(!s_scan_busy);
    oom=false;records_rc=1;s_scan_busy=true;scan_collect();assert(s_scan_failed);assert(!s_scan_busy);
    records_rc=0;s_scan_failed=false;s_scan_busy=true;scan_collect();
    assert(!s_scan_busy);assert(!s_scan_failed);assert(s_ap_n==1);assert(s_aps[0].rssi==-59);
    puts("PASS: scan error/empty/OOM/records error/dedup strongest");
}
'''
with tempfile.TemporaryDirectory(prefix='fnos-net-scan-') as tmp:
    c=Path(tmp)/'scan.c';binary=Path(tmp)/'scan'
    program=scan_preamble+function('ap_cmp_rssi')+function('scan_collect')+scan_checks
    c.write_text(program)
    subprocess.run(['cc','-std=gnu11','-UNDEBUG','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,cwd=tmp,timeout=20)
    mutant=program.replace('s_scan_failed = err != ESP_OK;', 's_scan_failed = false;')
    if mutant==program:raise RuntimeError('missing scan error branch')
    c.write_text(mutant)
    subprocess.run(['cc','-std=gnu11','-UNDEBUG',str(c),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],cwd=tmp,capture_output=True,timeout=20)
    if result.returncode==0 or b's_scan_failed' not in result.stderr:
        raise RuntimeError('scan error negative missed exact assertion')
    print('PASS: scan error-as-empty negative rejected')
