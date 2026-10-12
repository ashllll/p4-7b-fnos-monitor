#!/usr/bin/env python3
"""Legacy diagnostic ABI: borrowing is confined to its writer task; no device I/O."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'components/fnos_monitor/fnos_pair.c').read_text()
match = re.search(r'^const char \*fnos_pair_last_http_diag\(void\)\s*\{', SOURCE, re.M)
if not match:
    raise RuntimeError('production diagnostic function missing')
start = SOURCE.index('{', match.start())
end, depth = start + 1, 1
while depth:
    depth += (SOURCE[end] == '{') - (SOURCE[end] == '}')
    end += 1
function = SOURCE[match.start():end]
preamble = r'''
#include "fnos_pair.h"
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
typedef void *TaskHandle_t;
static _Atomic(TaskHandle_t) s_task;
static TaskHandle_t current;
static char s_http_diag[96] = "fixture failure";
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current; }
'''
main = r'''
int main(void) {
    current = (void *)1;
    assert(!strcmp(fnos_pair_last_http_diag(), ""));
    s_task = (void *)2;
    const char *other = fnos_pair_last_http_diag();
    assert(other != s_http_diag && !strcmp(other, ""));
    strcpy(s_http_diag, "next request");
    assert(!strcmp(other, ""));
    current = (void *)2;
    assert(fnos_pair_last_http_diag() == s_http_diag);
    assert(!strcmp(fnos_pair_last_http_diag(), "next request"));
    s_task = NULL;
    assert(!strcmp(fnos_pair_last_http_diag(), ""));
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='fnos-pair-diag-') as directory:
    source = Path(directory) / 'diag.c'
    binary = Path(directory) / 'diag'
    for negative in (False, True):
        body = 'const char *fnos_pair_last_http_diag(void) { return s_http_diag; }' if negative else function
        source.write_text(preamble + body + main)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-fsanitize=address,undefined',
                        '-I', str(ROOT / 'components/fnos_monitor'), str(source), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True)
        if negative == (result.returncode == 0):
            raise AssertionError('legacy bare-pointer negative control survived' if negative else result.stderr.decode())
print('PASS: unpublished/other/owner task borrowing, stable empty result, bare-pointer negative control')
