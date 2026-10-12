#!/usr/bin/env python3
"""Production UI mailboxes and tick creation; no device or network access."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT / 'components/fnos_monitor/fnos_ui.c').read_text()
def fn(name):
    m = re.search(r'^(?:static )?(?:void|bool) ' + name + r'\([^\n]*\)\s*\{', s, re.M)
    if not m: raise RuntimeError(name)
    a = s.index('{', m.start()); b = a + 1; depth = 1
    while depth:
        depth += (s[b] == '{') - (s[b] == '}'); b += 1
    return s[m.start():b]
page = re.search(r'int want = ([^;]+);', fn('ui_tick')).group(1)
temp = re.search(r'int expand = ([^;]+);', fn('ui_tick')).group(1)
night = re.search(r'const bool night = ([^;]+);', fn('ui_tick')).group(1)
declarations = '\n'.join(re.findall(r'^static atomic_(?:bool|int)\s+s_(?:night_req|page_req|temp_expand_req)[^;]*;', s, re.M))
if len(declarations.splitlines()) != 3: raise RuntimeError('mailboxes must be atomic')
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#define FNOS_UI_PAGE_COUNT 6
#define ESP_LOGE(...) (++errors)
#define ESP_LOGI(...) ((void)0)
typedef int lv_timer_t;
typedef int lv_obj_t;
static bool s_created;
static lv_timer_t *s_ui_tick, *s_motion_poll;
static int errors, attempts, trees, pages, starts, s_page;
static bool fail_timer;
static lv_timer_t timer;
static lv_obj_t object;
static int s_st;
static struct { lv_obj_t *screen, *viewport, *page[6]; } s_ui;
static void ui_tick(lv_timer_t *t) { (void)t; }
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), int period, void *arg) {
 assert(cb == ui_tick && period == 500 && !arg); ++attempts;
 return fail_timer ? NULL : &timer;
}
static void fnos_status_release(int *p) { assert(p == &s_st); }
static lv_obj_t *uk_screen(void) { ++trees; return &object; }
static lv_obj_t *uk_shell(lv_obj_t *scr, lv_obj_t **rail, lv_obj_t **head, lv_obj_t **foot) {
 assert(scr == &object); *rail = *head = *foot = &object; return &object;
}
static void build_rail(lv_obj_t *p) { assert(p); }
static void build_header(lv_obj_t *p) { assert(p); }
static void build_foot(lv_obj_t *p) { assert(p); }
static lv_obj_t *build_page(lv_obj_t *p, int i) { assert(p && i >= 0 && i < 6); ++pages; return p; }
static void show(lv_obj_t *p, bool on) { assert(p && !on); }
static void fnos_ui_set_page(int p) { s_page = p; }
static void overview_layout(void) {}
static void night_apply(void) {}
static void motion_poll_start(void) { s_motion_poll = &timer; }
static void motion_init(void) { ++starts; motion_poll_start(); }
static void fnos_perf_init(void) {}
'''
body = declarations + '\n' + '\n'.join(fn(n) for n in ('fnos_ui_set_night','fnos_ui_request_page','fnos_ui_request_temp_expand','ui_tick_start','fnos_ui_ready','fnos_ui_create'))
body += '\nstatic int take_page(void) { return '+page+'; }\nstatic int take_temp(void) { return '+temp+'; }\nstatic bool read_night(void) { return '+night+'; }\n'
checks = r'''
static atomic_int stage;
static bool test_temp;
static void *writer(void *unused) {
 (void)unused;
 while (atomic_load(&stage) != 1) sched_yield();
 if (test_temp) fnos_ui_request_temp_expand(2); else fnos_ui_request_page(2);
 atomic_store(&stage, 2); return NULL;
}
static void interleave(bool temp) {
 test_temp = temp; atomic_store(&stage, 0);
 if (temp) fnos_ui_request_temp_expand(1); else fnos_ui_request_page(1);
 pthread_t thread; assert(!pthread_create(&thread, NULL, writer, NULL));
 assert((temp ? take_temp() : take_page()) == 1);
 if (atomic_load(&stage) == 0) atomic_store(&stage, 1);
 assert(!pthread_join(thread, NULL));
 assert((temp ? take_temp() : take_page()) == 2);
 assert((temp ? take_temp() : take_page()) == -1);
}
int main(void) {
 assert(take_page() == -1 && take_temp() == -1 && !read_night());
 fnos_ui_request_page(-1); fnos_ui_request_page(FNOS_UI_PAGE_COUNT);
 fnos_ui_request_temp_expand(-1);
 assert(take_page() == -1 && take_temp() == -1);
 fnos_ui_request_page(1); fnos_ui_request_page(3); assert(take_page() == 3);
 fnos_ui_request_temp_expand(1); fnos_ui_request_temp_expand(7); assert(take_temp() == 7);
 fnos_ui_set_night(true); assert(read_night()); fnos_ui_set_night(false); assert(!read_night());
 interleave(false); interleave(true);
 assert(!fnos_ui_ready()); fail_timer = true;
 fnos_ui_create(); assert(!fnos_ui_ready() && trees == 1 && pages == 6 && starts == 1 && errors == 1);
 fnos_ui_create(); assert(!fnos_ui_ready() && trees == 1 && attempts == 2 && errors == 2);
 fail_timer = false; fnos_ui_create(); assert(fnos_ui_ready() && attempts == 3 && trees == 1 && starts == 1);
 fnos_ui_create(); assert(fnos_ui_ready() && attempts == 3 && trees == 1);
}
'''
negative = r'''
static int lost_exchange(atomic_int *p) {
 int old = atomic_load(p);
 if (old == 1) {
  atomic_store(&stage, 1);
  while (atomic_load(&stage) != 2) sched_yield();
 }
 atomic_store(p, -1); return old;
}
'''
with tempfile.TemporaryDirectory(prefix='fnos-ui-requests-') as d:
    c, exe = Path(d)/'ui.c', Path(d)/'ui'
    for bad in (False, True):
        variant = body
        if bad:
            for mailbox in ('s_page_req', 's_temp_expand_req'):
                variant = variant.replace(f'atomic_exchange_explicit(&{mailbox}, -1, memory_order_relaxed)', f'lost_exchange(&{mailbox})')
        c.write_text(prefix + '\nstatic atomic_int stage;\n' + (negative if bad else '') + variant + checks.replace('static atomic_int stage;', ''))
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread',str(c),'-o',str(exe)],check=True)
        result = subprocess.run([str(exe)],capture_output=True,timeout=15)
        if bad and result.returncode != 0 and b'take_temp() : take_page()) == 2' not in result.stderr:
            raise AssertionError('negative failed for an unrelated reason: ' + result.stderr.decode())
        if (result.returncode == 0) == bad:
            raise AssertionError('lost update negative survived' if bad else result.stderr.decode())
print('PASS: latest mailbox values, validation, deterministic concurrent post/consume, lost-update negative, timer OOM/retry without tree duplication')

# Page transition must not retain an active transition after allocation fails.
motion_prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#define FNOS_UI_PAGE_COUNT 6
#define UK_MOTION_MS 240
#define UK_MOTION_MIN_MS 90
#define UK_MOTION_DAMPING 10
#define ESP_LOGE(...) ((void)0)
typedef int lv_obj_t;
typedef struct {int unused;} lv_anim_t;
static struct {int from,to,direction;int32_t offset,start,end,width,velocity,spring_b;bool active,tracking,dragging;uint32_t duration;float phase_velocity;} s_motion;
static struct {lv_obj_t *page[6],*viewport,*diagnostics_button;} s_ui;
static bool s_refresh_pending, fail_anim, reduced;
static int s_page, selected, refreshes, pairs, attempts;
static lv_anim_t animation;
static void show(lv_obj_t *o,bool on){(void)o;(void)on;}
static void lv_obj_set_style_translate_x(lv_obj_t *o,int x,int part){(void)o;assert(!x && !part);}
static void lv_obj_invalidate(lv_obj_t *o){(void)o;}
static void refresh_soon(void){}
static void refresh(void){refreshes++;}
static void pair_refresh(void){pairs++;}
static void nav_select(int p){selected=p;}
static bool motion_reduced(void){return reduced;}
static void motion_exec(void *p,int32_t v){(void)p;(void)v;}
static void motion_done(lv_anim_t *a){(void)a;}
static void lv_anim_init(lv_anim_t *a){(void)a;}
static void lv_anim_set_var(lv_anim_t *a,void *p){(void)a;assert(p==&s_motion);}
static void lv_anim_set_values(lv_anim_t *a,int x,int y){(void)a;assert(x==0&&y==1024);}
static void lv_anim_set_duration(lv_anim_t *a,int v){(void)a;assert(v>=90&&v<=240);}
static void lv_anim_set_exec_cb(lv_anim_t *a,void (*cb)(void*,int32_t)){(void)a;assert(cb==motion_exec);}
static void lv_anim_path_linear(void){}
static void lv_anim_set_path_cb(lv_anim_t *a,void (*cb)(void)){(void)a;assert(cb==lv_anim_path_linear);}
static void lv_anim_set_completed_cb(lv_anim_t *a,void (*cb)(lv_anim_t*)){(void)a;assert(cb==motion_done);}
static lv_anim_t *lv_anim_start(lv_anim_t *a){(void)a;attempts++;return fail_anim?NULL:&animation;}
"""
motion_body=fn('motion_finish')+'\n'+fn('motion_snap')
motion_checks=r"""
static void scenario(bool commit){
 s_motion.from=0;s_motion.to=1;s_motion.direction=1;s_motion.width=1024;s_motion.offset=-128;
 s_motion.active=true;s_motion.velocity=0;s_refresh_pending=true;
 int before=refreshes;motion_snap(commit);
 assert(!s_motion.active && !s_refresh_pending);
 assert(s_page==(commit?1:0)&&selected==s_page);
 assert(refreshes==before+1 && pairs==refreshes);
}
int main(void){
 fail_anim=true;scenario(true);scenario(false);assert(attempts==2);
 reduced=true;scenario(true);assert(attempts==2);reduced=false;
 fail_anim=false;s_motion.offset=-128;s_motion.active=true;s_refresh_pending=true;
 motion_snap(false);assert(s_motion.active && s_refresh_pending && attempts==3);
 motion_finish();assert(!s_motion.active&&!s_refresh_pending);return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='fnos-motion-oom-') as d:
    c,exe=Path(d)/'motion.c',Path(d)/'motion'
    flags=['clang','-std=c11','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-lm','-o',str(exe)]
    c.write_text(motion_prefix+motion_body+motion_checks)
    subprocess.run(flags,check=True,capture_output=True)
    r=subprocess.run([str(exe)],capture_output=True,timeout=15)
    assert r.returncode==0,r.stderr.decode(errors='replace')
    if 'if (!lv_anim_start(&a))' in motion_body:
        a=motion_body.index('    if (!lv_anim_start(&a))');b=motion_body.index('\n    }',a)+6
        bad=motion_body[:a]+'    lv_anim_start(&a);'+motion_body[b:]
        c.write_text(motion_prefix+bad+motion_checks);subprocess.run(flags,check=True,capture_output=True)
        r=subprocess.run([str(exe)],capture_output=True,timeout=15)
        assert r.returncode and b'!s_motion.active && !s_refresh_pending' in r.stderr,r.stderr
print('PASS: production page motion allocation failure commits/cancels immediately, flushes pending refresh; normal/reduced paths and old unchecked-start negative')

# Missing gesture timer retries independently of event registration and tree creation.
poll_prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define UK_GESTURE_POLL_MS 8
#define ESP_LOGE(...) (++errors)
typedef int lv_timer_t;
static lv_timer_t *s_motion_poll, timer;
static struct {bool tracking;} s_motion;
static int attempts, errors, pauses, resumes, readies;
static bool fail;
static void motion_tick(lv_timer_t *t) {(void)t;}
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *),int ms,void *arg) {
 assert(cb==motion_tick && ms==8 && !arg);attempts++;return fail?NULL:&timer;
}
static void lv_timer_pause(lv_timer_t *t){assert(t==&timer);pauses++;}
static void lv_timer_resume(lv_timer_t *t){assert(t==&timer);resumes++;}
static void lv_timer_ready(lv_timer_t *t){assert(t==&timer);readies++;}
'''
poll_main = r'''
int main(void) {
 fail=true;motion_poll_start();assert(!s_motion_poll && errors==1 && attempts==1);
 motion_poll_start();assert(!s_motion_poll && errors==2 && attempts==2);
 fail=false;motion_poll_start();assert(s_motion_poll && pauses==1 && attempts==3);
 motion_poll_start();assert(pauses==1 && attempts==3);
 s_motion_poll=NULL;s_motion.tracking=true;motion_poll_start();
 assert(s_motion_poll && resumes==1 && readies==1 && attempts==4);
 return 0;
}
'''
poll_body = fn('motion_poll_set')+'\n'+fn('motion_poll_start')
assert 'motion_poll_start();' in fn('fnos_ui_create').split('return;',1)[0]
assert 's_motion_poll != NULL' in fn('fnos_ui_ready')
with tempfile.TemporaryDirectory(prefix='fnos-gesture-timer-') as d:
 c,exe=Path(d)/'test.c',Path(d)/'test'
 for old in (False,True):
  body=poll_body.replace('if (s_motion_poll) return;','') if old else poll_body
  if old:assert body!=poll_body
  c.write_text(poll_prefix+body+poll_main)
  subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True,capture_output=True)
  r=subprocess.run([str(exe)],capture_output=True,timeout=15)
  if old:assert r.returncode and b'pauses==1 && attempts==3' in r.stderr,r.stderr
  else:assert not r.returncode,r.stderr
print('PASS production gesture timer failure/retry, idle pause, active resume, idempotency and duplicate-allocation negative; readiness/retry wiring')
