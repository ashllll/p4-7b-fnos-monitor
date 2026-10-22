#include "uk.h"
#include "uk_spring.h"
#include "src/widgets/bar/lv_bar_private.h"

/* Draw on LVGL's actual indicator geometry, including padding and animation.
   No child object or independent position can drift away from the bar. */
static void meter_cursor_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    const lv_bar_t *bar = (const lv_bar_t *)obj;
    lv_area_t track;
    lv_obj_get_content_coords(obj, &track);
    lv_area_t cursor = bar->indic_area;
    bool horizontal = lv_area_get_width(&track) >= lv_area_get_height(&track);
    int32_t thickness = LV_MAX(2, (horizontal ? lv_area_get_height(&track) :
                                  lv_area_get_width(&track)) / 3);
    bool reversed = bar->val_reversed ^
        (horizontal && lv_obj_get_style_base_dir(obj, LV_PART_MAIN) == LV_BASE_DIR_RTL);
    if (horizontal) {
        int32_t x = reversed ? cursor.x1 : cursor.x2;
        cursor.x1 = LV_CLAMP(track.x1, x - thickness / 2, track.x2 - thickness + 1);
        cursor.x2 = cursor.x1 + thickness - 1;
        cursor.y1 = track.y1;
        cursor.y2 = track.y2;
    } else {
        int32_t y = reversed ? cursor.y2 : cursor.y1;
        cursor.y1 = LV_CLAMP(track.y1, y - thickness / 2, track.y2 - thickness + 1);
        cursor.y2 = cursor.y1 + thickness - 1;
        cursor.x1 = track.x1;
        cursor.x2 = track.x2;
    }
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    lv_color_t fill = lv_obj_get_style_bg_color(obj, LV_PART_INDICATOR);
    dsc.bg_color = lv_color_eq(fill, uk_c(UK_OFF)) ? uk_c(UK_OFF) : uk_c(UK_T1);
    dsc.bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_INDICATOR);
    dsc.radius = thickness / 2;
    lv_draw_rect(lv_event_get_layer(e), &dsc, &cursor);
}

static void meter_cursor_attach(lv_obj_t *obj)
{
    if (!lv_obj_check_type(obj, &lv_bar_class)) return;
    for (uint32_t i = 0; i < lv_obj_get_event_count(obj); i++)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(obj, i)) == meter_cursor_draw) return;
    lv_obj_add_event_cb(obj, meter_cursor_draw, LV_EVENT_DRAW_MAIN_END, NULL);
}

typedef struct {
    lv_obj_t *obj;
    float value, velocity, from, from_velocity;
    int32_t target;
    uint32_t started;
    bool arc, active;
} meter_t;
static bool motion_enabled = true;
static void meter_step(void *var, int32_t elapsed);

static void meter_write(meter_t *m, int32_t value)
{
    if (m->arc) lv_arc_set_value(m->obj, value);
    else lv_bar_set_value(m->obj, value, LV_ANIM_OFF);
}
static void meter_snap(meter_t *m)
{
    lv_anim_delete(m, meter_step);
    m->active = false;
    m->value = m->target;
    m->velocity = 0;
    meter_write(m, m->target);
}
static void meter_event(lv_event_t *e)
{
    meter_t *m = lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        lv_anim_delete(m, meter_step);
        lv_free(m);
    }
}
static meter_t *meter_find(lv_obj_t *obj)
{
    for (uint32_t i = 0; i < lv_obj_get_event_count(obj); i++) {
        lv_event_dsc_t *d = lv_obj_get_event_dsc(obj, i);
        if (lv_event_dsc_get_cb(d) == meter_event) return lv_event_dsc_get_user_data(d);
    }
    return NULL;
}
static void meter_sample(meter_t *m, uint32_t elapsed)
{
    m->value = uk_spring_value(m->from, m->from_velocity, m->target,
                               uk_spring_response(elapsed, UK_NUMBER_MS), &m->velocity);
    const int32_t lo = m->arc ? lv_arc_get_min_value(m->obj) : lv_bar_get_min_value(m->obj);
    const int32_t hi = m->arc ? lv_arc_get_max_value(m->obj) : lv_bar_get_max_value(m->obj);
    /* Physical inertia may briefly continue after a reversal, but never beyond
       the instrument's legal range. No interpolation modifies source data. */
    if (m->value < lo || m->value > hi) {
        m->value = LV_CLAMP(lo, m->value, hi);
        m->velocity = 0;
    }
    meter_write(m, (int32_t)lroundf(m->value));
}
static void meter_step(void *var, int32_t elapsed)
{
    meter_t *m = var;
    if (!motion_enabled || uk_motion_reduced() || !lv_obj_is_visible(m->obj) ||
        (uint32_t)elapsed >= 2*UK_NUMBER_MS) {
        meter_snap(m);
        return;
    }
    meter_sample(m, (uint32_t)elapsed);
    /* Keep the tiny tail until sub-tick position AND speed are at rest.
       The 2x response deadline is a bounded emergency finish, not a queue. */
    if (fabsf(m->value-m->target)<1.f && fabsf(m->velocity)<.01f) meter_snap(m);
}
void uk_meter_motion_enable(bool enabled) { motion_enabled = enabled; }
void uk_meter_settle(lv_obj_t *obj)
{
    meter_t *m = meter_find(obj);
    if (m && m->active) meter_snap(m);
}
void uk_meter_set(lv_obj_t *obj, int32_t value, bool animate)
{
    if (!obj) return;
    meter_cursor_attach(obj);
    const bool arc = lv_obj_check_type(obj, &lv_arc_class);
    const int32_t lo = arc ? lv_arc_get_min_value(obj) : lv_bar_get_min_value(obj);
    const int32_t hi = arc ? lv_arc_get_max_value(obj) : lv_bar_get_max_value(obj);
    value = LV_CLAMP(lo, value, hi);
    meter_t *m = meter_find(obj);
    bool enabled = animate && motion_enabled && !UK_NUMBER_REDUCED &&
        !uk_motion_reduced() && UK_NUMBER_MS > 0 && lv_obj_is_visible(obj);
    if (!m && enabled) {
        m = uk_alloc(sizeof *m);
        if (m) {
            *m = (meter_t){.obj=obj, .value=value, .target=value, .arc=arc};
            if (!lv_obj_add_event_cb(obj, meter_event, LV_EVENT_DELETE, m)) { lv_free(m); m=NULL; }
            /* First valid/visible reading is immediate; don't sweep from fake zero. */
            enabled = false;
        }
    }
    if (!m) {
        if (arc) lv_arc_set_value(obj, value);
        else lv_bar_set_value(obj, value, LV_ANIM_OFF);
        return;
    }
    if (!enabled) { m->target=value; meter_snap(m); return; }
    if (m->target == value) return; /* Repeated samples never restart the tail. */
    if (m->active) meter_sample(m, lv_tick_elaps(m->started));
    lv_anim_delete(m, meter_step);
    m->from=m->value; m->from_velocity=m->velocity;
    m->target=value; m->started=lv_tick_get(); m->active=true;
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, m);
    lv_anim_set_values(&a, 0, 2*UK_NUMBER_MS);
    lv_anim_set_duration(&a, 2*UK_NUMBER_MS);
    lv_anim_set_exec_cb(&a, meter_step);
    lv_anim_set_path_cb(&a, lv_anim_path_linear); /* Clock, not the visible curve. */
    if (!lv_anim_start(&a)) meter_snap(m);
}
int32_t uk_anim_path_damped(const lv_anim_t *a)
{
    uk_spring_response_t r=a->act_time>=a->duration ? (uk_spring_response_t){0} :
        uk_spring_response_for(a->act_time < 0 ? 0 : (uint32_t)a->act_time,
                               a->duration, UK_TELEMETRY_DAMPING, 1.f);
    float velocity;
    return (int32_t)lroundf(uk_spring_value(a->start_value, 0, a->end_value, r, &velocity));
}
#ifdef FNOS_UI_TESTING
#include <assert.h>
void uk_meter_selfcheck(lv_obj_t *parent)
{
    bool saved_motion=motion_enabled;
    motion_enabled=true;
    float v, x=uk_spring_value(0,0,1,uk_spring_response(0,450),&v);
    assert(x==0 && v==0);
    /* Critical damping must remain monotone over every supported duration. */
    for (uint32_t duration=1;duration<=1000;duration++) {
        float previous=0;
        for (uint32_t t=0;t<=duration;t++) {
            x=uk_spring_value(0,0,1,
                uk_spring_response_for(t,duration,UK_TELEMETRY_DAMPING,1),&v);
            assert(x+1e-6f>=previous && x<=1 && v>=0);
            previous=x;
        }
    }
    assert(x>.95f && x<1 && v>0);
    /* Exact integration must not depend on how a frame interval was split. */
    float full_v, full=uk_spring_value(.3f,.002f,.8f,uk_spring_response(600,450),&full_v);
    x=uk_spring_value(.3f,.002f,.8f,uk_spring_response(200,450),&v);
    x=uk_spring_value(x,v,.8f,uk_spring_response(400,450),&v);
    assert(fabsf(x-full)<.00001f && fabsf(v-full_v)<.00001f);
    /* Library branches, reversal energy and dropped frames: partitioning time
       must not change the result, including either side of critical damping. */
    const float ratios[] = {.8f, .85f, .999f, 1.f, 1.001f, 1.2f};
    for (unsigned i=0;i<sizeof ratios/sizeof *ratios;i++) {
        full=uk_spring_value(.3f,-.002f,.8f,
            uk_spring_response_for(470,300,8,ratios[i]),&full_v);
        x=uk_spring_value(.3f,-.002f,.8f,
            uk_spring_response_for(17,300,8,ratios[i]),&v);
        x=uk_spring_value(x,v,.8f,
            uk_spring_response_for(453,300,8,ratios[i]),&v);
        assert(isfinite(x) && isfinite(v));
        assert(fabsf(x-full)<.00001f && fabsf(v-full_v)<.00001f);
    }
    float peak=0;
    for (uint32_t t=0;t<=600;t++) {
        x=uk_spring_value(0,0,1,uk_spring_response_for(t,300,8,.85f),&v);
        peak=LV_MAX(peak,x);
    }
    assert(peak>1.005f && peak<1.007f); /* Restrained ammeter rebound. */
    x=uk_spring_value(.3f,.002f,0,uk_spring_response_for(1,300,8,.85f),&v);
    assert(x>.3f && v>0); /* Reversal preserves momentum, not an instant stop. */
    lv_obj_t *bar=lv_bar_create(parent);
    lv_bar_set_range(bar,0,100*UK_METER_SCALE);
    lv_obj_update_layout(parent);
    uk_meter_set(bar,0,true);
    meter_t *m=meter_find(bar);
    if (UK_NUMBER_REDUCED || UK_NUMBER_MS==0) {
        assert(!m);
        uk_meter_set(bar,8000,true); assert(lv_bar_get_value(bar)==8000);
        lv_obj_delete(bar);
        motion_enabled=saved_motion;
        return;
    }
    assert(m && !m->active);
    uk_meter_set(bar,8000,true); assert(m->active);
    meter_sample(m,UK_NUMBER_MS/3); m->started=lv_tick_get()-UK_NUMBER_MS/3;
    float position=m->value, velocity=m->velocity;
    uk_meter_set(bar,2000,true);
    assert(fabsf(m->value-position)<.001f && fabsf(m->velocity-velocity)<.001f);
    uint32_t began=m->started;
    uk_meter_set(bar,2000,true); assert(m->started==began);
    uk_meter_settle(bar); assert(lv_bar_get_value(bar)==2000 && !m->active);
    uk_meter_set(bar,9000,true); lv_obj_add_flag(bar,LV_OBJ_FLAG_HIDDEN);
    meter_step(m,10); assert(!m->active && lv_bar_get_value(bar)==9000);
    lv_obj_remove_flag(bar,LV_OBJ_FLAG_HIDDEN);
    uk_meter_set(bar,4000,true); assert(m->active);
    uk_meter_motion_enable(false); meter_step(m,10);
    assert(!m->active && lv_bar_get_value(bar)==4000);
    uk_meter_motion_enable(true);
    uk_meter_set(bar,9000,true);
    uk_motion_set_reduced(true);
    meter_step(m,10);
    assert(!m->active && lv_bar_get_value(bar)==9000);
    uk_motion_set_reduced(false);
    uk_meter_set(bar,1000,true); assert(m->active);
    lv_obj_delete(bar); /* In-flight state and callback must be released together. */
    lv_obj_t *arc=lv_arc_create(parent);
    lv_arc_set_range(arc,0,100*UK_METER_SCALE);
    lv_obj_update_layout(parent);
    uk_test_alloc_fail_after(0); uk_meter_set(arc,3700,true);
    assert(uk_test_alloc_was_triggered() && !meter_find(arc) && lv_arc_get_value(arc)==3700);
    uk_test_alloc_fail_after(-1); uk_alloc_reset();
    uk_meter_set(arc,3700,true); assert(meter_find(arc));
    uk_meter_set(arc,100000,true); uk_meter_settle(arc);
    assert(lv_arc_get_value(arc)==100*UK_METER_SCALE);
    uk_meter_set(arc,1700,false); assert(lv_arc_get_value(arc)==1700);
    lv_obj_delete(arc);
    motion_enabled=saved_motion;
}
#endif
