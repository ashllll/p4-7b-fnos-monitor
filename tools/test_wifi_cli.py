#!/usr/bin/env python3
"""Production CLI parsing and line clearing; anonymous input, no serial device."""
import re
import subprocess
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT/'components/fnos_monitor/fnos_wifi_cli.c').read_text()
def function(name, source=SRC):
    m = re.search(r'^(?:static )?(?:void|bool|int|unsigned) '+name+r'\([^;]*?\)\s*\{', source, re.M)
    if not m:
        raise RuntimeError('missing production function: '+name)
    a = source.index('{', m.start()); b = a+1; depth = 1
    while depth:
        depth += (source[b] == '{')-(source[b] == '}'); b += 1
    return source[m.start():b]
PRE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <setjmp.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { uint32_t major1,minor1,patch1; } esp_hosted_coprocessor_fwver_t;
static unsigned fw_queries;
static bool online=true;
static esp_err_t fw_error;
bool fnos_net_online(void) { return online; }
const char *esp_err_to_name(esp_err_t error) { (void)error;return "TEST_RPC_FAIL"; }
esp_err_t esp_hosted_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t *version) {
    assert(!version->major1 && !version->minor1 && !version->patch1);
    fw_queries++; *version=(esp_hosted_coprocessor_fwver_t){7,8,9}; return fw_error;
}
static atomic_uint s_poll_interval_ms=1000;
#define FNOS_POLL_INTERVAL_MIN_MS 50U
#define FNOS_POLL_INTERVAL_MAX_MS 60000U
#define FNOS_UI_PAGE_COUNT 6
static int clears, sets, page=-1, expand=-1;
static char ssid[64], pass[96];
static void usage(void) {}
static void cmd_show(void) {}
static void fnos_net_set_credentials(const char *a,const char *b) { sets++; snprintf(ssid,sizeof ssid,"%s",a); snprintf(pass,sizeof pass,"%s",b); }
static void fnos_ui_request_page(int n) { page=n; }
static void fnos_ui_request_temp_expand(int n) { expand=n; }
static void fnos_pair_tls_probe_async(void) {}
static bool fnos_wifi_store_clear(void) { clears++;return true; }
static int benches;
static bool fnos_perf_cli(const char *x) {
    if (!strncmp(x,"bench",5)) {
        assert(!strcmp(x,"bench swipe 0 450 2")); benches++; return true;
    }
    return false;
}
static unsigned fnos_ui_refresh_period_ms(void) {
    unsigned period=atomic_load_explicit(&s_poll_interval_ms,memory_order_relaxed);
    return period < 100 ? 100 : period;
}
#define CLI_LINE_MAX 192
#define pdMS_TO_TICKS(x) (x)
#define vTaskDelay(x) ((void)(x))
static jmp_buf input_done;
static const char *chunks[10];
static unsigned chunk_index, chunk_offset;
static char *fragment_fgets(char *line, int cap, FILE *stream) {
    (void)stream;
    const char *chunk=chunks[chunk_index];
    if (!chunk) longjmp(input_done,1);
    if (!*chunk) { chunk_index++; return NULL; }
    size_t count=strlen(chunk+chunk_offset);
    if (count >= (size_t)cap) count=cap-1;
    memcpy(line,chunk+chunk_offset,count); line[count]=0;
    chunk_offset+=count;
    if (!chunk[chunk_offset]) {chunk_index++; chunk_offset=0;}
    return line;
}
#define fgets fragment_fgets
static void handle(char *line);
static void run(const char *input) { char line[192];snprintf(line,sizeof line,"%s",input); handle(line); }
'''
MAIN = r'''
int main(void) {
    run("wifi version"); assert(fw_queries==1);
    fw_error=-1; run("wifi version"); assert(fw_queries==2);
    online=false; run("wifi version"); assert(fw_queries==2);
    online=true; fw_error=ESP_OK;
    run("wifi version extra"); run("wifi versioning"); assert(fw_queries==2);
    const char *bad_poll[]={"poll", "poll 49", "poll 60001", "poll -1", "poll 50 extra", "poll 50x", "polling 50", "poll 999999999999999999999999"};
    for(unsigned i=0;i<sizeof bad_poll/sizeof bad_poll[0];i++) { run(bad_poll[i]);assert(fnos_data_poll_interval_ms()==1000); }
    run("poll 50");assert(fnos_data_poll_interval_ms()==50);
    run("poll 60000");assert(fnos_data_poll_interval_ms()==60000);
    run("poll\t1000");assert(fnos_data_poll_interval_ms()==1000);
    run("wificlear"); run("wifi clearing"); run("wifi clear extra");
    assert(!clears);
    run("wifi\tclear"); assert(clears==1);
    run("wifi setfoo name password"); assert(!sets);
    run("wifi\tset\tfixture\t\"\""); assert(sets==1 && !strcmp(ssid,"fixture") && !pass[0]);
    run("wifi set fixture a b c"); assert(sets==2 && !strcmp(pass,"a b c"));
    const char *bad[] = {"temp 2147483648", "temp 99999999999999999999999999999999", "temp -1", "temp 1x", "temperature 1", "page 12", "page 1x", "page1"};
    for (unsigned i=0;i<sizeof bad/sizeof bad[0];i++) { page=expand=-1;run(bad[i]);assert(page==-1 && expand==-1); }
    run("page\t5"); assert(page==5);
    run("temp\t2147483647"); assert(page==4 && expand==INT_MAX);
    run("anonymous-marker-invalid-command");
    char line[192]; memset(line,'x',sizeof line); clear_line(line,sizeof line);
    for (unsigned i=0;i<sizeof line;i++) assert(line[i]==0);
    char overflow[192]; memset(overflow,'x',sizeof overflow-1);overflow[sizeof overflow-1]=0;
    chunks[0]="bench sw";chunks[1]="";chunks[2]="ipe 0 450 2\n";
    chunks[3]=overflow;chunks[4]="";chunks[5]="wifi clear\n";
    chunks[6]="page ";chunks[7]="";chunks[8]="4\n";
    if (!setjmp(input_done)) cli_task(NULL);
    assert(benches==1 && clears==1 && page==4);
    return 0;
}
'''
data_source=(ROOT/'components/fnos_monitor/fnos_data.c').read_text()
body = '\n'.join(function(n,data_source) for n in ('fnos_data_poll_interval_ms','fnos_data_set_poll_interval_ms'))+'\n'
body += '\n'.join(function(n) for n in ('cmd_version','cmd_set','command_is','command_index','clear_line','handle','cli_task'))
with tempfile.TemporaryDirectory(prefix='fnos-cli-') as directory:
    c=Path(directory)/'cli.c'; binary=Path(directory)/'cli'
    for negative in (False, True):
        candidate = body.replace('(!line[n] || line[n] ==', '(true || line[n] ==') if negative else body
        if negative and candidate == body:
            raise AssertionError('negative control did not change source')
        c.write_text(PRE+candidate+MAIN)
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                        '-fno-sanitize-recover=undefined',str(c),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True)
        if negative == (result.returncode==0):
            raise AssertionError('prefix negative control survived' if negative else result.stderr.decode())
        if not negative and b'anonymous-marker-invalid-command' in result.stdout:
            raise AssertionError('unknown command echoed input')
        if not negative and (result.stdout.count(b'coprocessor firmware=7.8.9')!=1 or
                             b'coprocessor firmware query failed: TEST_RPC_FAIL' not in result.stdout or
                             b'coprocessor firmware query skipped' not in result.stdout):
            raise AssertionError('coprocessor version success/failure/offline result mismatch')
print('PASS: coprocessor version success/RPC failure/offline/extra-token rejection; complete tokens, tabs/open network, numeric bounds/trailing input, no unknown-input echo, whole-line clearing, fragmented reads/EOF/overflow-tail recovery, prefix negative control')
