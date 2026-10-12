#!/usr/bin/env python3
"""Exercise production startup timer error paths with anonymous SDK/LVGL stubs."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'main/main.cpp').read_text()

def function(name):
    match = re.search(r'^static bool ' + name + r'\([^\n]*\)\s*\{', source, re.M)
    if not match:
        raise AssertionError('missing production function ' + name)
    start = source.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#define CONFIG_FNOS_SOAK_DIAG 1
#define CONFIG_FNOS_AUTO_PAGE_SEC 2
#define ESP_OK 0
#define ESP_LOGE(...) (++errors)
#define ESP_LOGW(...) (++warnings)
#define pdMS_TO_TICKS(x) (x)
using esp_err_t = int;
using esp_timer_handle_t = void *;
struct esp_timer_create_args_t { const char *name; };
static int errors, warnings, create_rc, start_rc, delete_rc;
static int created, started, deleted;
static esp_err_t esp_timer_create(const esp_timer_create_args_t *, void **out) {
    ++created; if (!create_rc) *out = reinterpret_cast<void *>(1); return create_rc;
}
static esp_err_t esp_timer_start_periodic(void *t, uint64_t us) {
    assert(t == reinterpret_cast<void *>(1) && us == 60000000); ++started; return start_rc;
}
static esp_err_t esp_timer_delete(void *t) {
    assert(t == reinterpret_cast<void *>(1)); ++deleted; return delete_rc;
}
static int locks, unlocks, ui_created, lv_created, delays;
static bool lock_ok, lv_ok;
static bool bsp_display_lock(int ms) { assert(ms == 2000); ++locks; return lock_ok; }
static void bsp_display_unlock() { ++unlocks; }
static void vTaskDelay(int ms) { assert(ms == 300); ++delays; }
static void fnos_ui_create() { ++ui_created; }
static bool ready = true;
static bool fnos_ui_ready() { return ready; }
static void diag_timer_cb(void *) {}
static void auto_page_cb(void *) {}
static void *lv_timer_create(void (*)(void *), int, void *) {
    ++lv_created; return lv_ok ? reinterpret_cast<void *>(1) : nullptr;
}
'''
checks = r'''
int main() {
    esp_timer_create_args_t args{"fixture"};
    create_rc = 1;
    assert(!start_periodic_timer(&args, 60000000));
    assert(created == 1 && started == 0 && deleted == 0 && errors == 1);
    create_rc = 0; start_rc = 2;
    assert(!start_periodic_timer(&args, 60000000));
    assert(started == 1 && deleted == 1 && errors == 2);
    delete_rc = 3;
    assert(!start_periodic_timer(&args, 60000000));
    assert(started == 2 && deleted == 2 && errors == 4);
    start_rc = delete_rc = 0;
    assert(start_periodic_timer(&args, 60000000));
    assert(started == 3 && deleted == 2 && errors == 4);
    assert(!ui_build_locked());
    assert(locks == 3 && delays == 3 && ui_created == 0 && unlocks == 0);
    lock_ok = true;
    assert(ui_build_locked()); // optional timer OOM must not recreate UI on retry
    assert(ui_created == 1 && lv_created == 2 && unlocks == 1 && errors == 6);
    ready = false;
    const int before = lv_created;
    assert(!ui_build_locked());
    assert(lv_created == before && unlocks == 4 && ui_created == 4);
    ready = true;
    lv_ok = true;
    assert(ui_build_locked());
    assert(ui_created == 5 && lv_created == 4 && unlocks == 5 && errors == 9);
}
'''
body = function('start_periodic_timer') + function('ui_build_locked')
with tempfile.TemporaryDirectory(prefix='fnos-main-timers-') as directory:
    cpp, binary = Path(directory) / 'main.cpp', Path(directory) / 'main'
    for negative in (False, True):
        variant = body.replace('const esp_err_t cleanup_err = esp_timer_delete(timer);',
                               'const esp_err_t cleanup_err = ESP_OK;') if negative else body
        cpp.write_text(prefix + variant + checks)
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-fsanitize=address,undefined',
                        str(cpp), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True)
        if (result.returncode == 0) == negative:
            raise AssertionError('cleanup negative control survived' if negative else result.stderr.decode())
print('PASS: production create/start/delete failures, optional LVGL OOM, lock exhaustion, cleanup negative control')
