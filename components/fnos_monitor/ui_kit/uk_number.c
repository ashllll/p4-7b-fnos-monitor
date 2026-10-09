/* Numeric transitions inherit lv_label: native text/layout/audits stay authoritative. */
#include "uk.h"
#include "src/widgets/label/lv_label_private.h"
#include "src/core/lv_obj_class_private.h"
#include "src/misc/lv_text_private.h"
#include "src/misc/lv_area_private.h"
#include <string.h>
#include <math.h>

typedef struct glyph_layer {
    struct glyph_layer *next;
    uint32_t cp, delay;
    int key;
    bool presented;
    float x, y, alpha, from_x, from_y, from_alpha, to_x, to_y, to_alpha;
} glyph_layer_t;
typedef struct {
    lv_label_t label;
    glyph_layer_t *layers;
    const lv_font_t *font;
    uint32_t started, span;
    bool active;
} number_label_t;
static bool motion_enabled = true;
static size_t layer_bytes;
static void number_event(const lv_obj_class_t *cls, lv_event_t *e);
static void number_destroy(const lv_obj_class_t *cls, lv_obj_t *obj);
static void number_step(void *obj, int32_t elapsed);
static const lv_obj_class_t number_class = {
    .base_class=&lv_label_class, .event_cb=number_event, .destructor_cb=number_destroy,
    .instance_size=sizeof(number_label_t), .width_def=LV_SIZE_CONTENT,
    .height_def=LV_SIZE_CONTENT, .name="uk_number_label"
};

/* Invert x(t), then evaluate y(t). Apple easeOut is (0,0,.58,1),
   not y(progress) and not LVGL's unrelated built-in ease-out polynomial. */
static float apple_ease(float progress)
{
    if (progress<=0) return 0;
    if (progress>=1) return 1;
    float lo=0, hi=1, t=0;
    for (int i=0;i<16;i++) {
        t=(lo+hi)*.5f;
        float x=3*(1-t)*t*t*UK_NUMBER_BEZIER_X2+t*t*t;
        if (x<progress) lo=t; else hi=t;
    }
    return 3*(1-t)*t*t+t*t*t;
}
static bool has_reading(const char *text)
{
    bool digit=false;
    for (const unsigned char *s=(const unsigned char *)text;*s;s++) {
        if (*s=='\n' || *s=='\r') return false;
        if (*s>='0' && *s<='9') digit=true;
    }
    return digit;
}
/* A unit, sign, device name, or status change is semantic: publish it immediately. */
static bool same_numeric_structure(const char *a, const char *b)
{
    for (;;) {
        while (*a>='0' && *a<='9') a++;
        while (*b>='0' && *b<='9') b++;
        if (*a!=*b) return false;
        if (!*a) return true;
        a++; b++;
    }
}
static void clear_layers(lv_obj_t *obj)
{
    number_label_t *n=(number_label_t *)obj;
    lv_anim_delete(obj,number_step);
    while (n->layers) {
        glyph_layer_t *next=n->layers->next;
        layer_bytes-=sizeof *n->layers;
        lv_free(n->layers); n->layers=next;
    }
    n->active=false;
    lv_obj_invalidate(obj);
}
void uk_number_settle(lv_obj_t *obj)
{
    if (obj && lv_obj_get_class(obj)==&number_class) clear_layers(obj);
}
void uk_number_motion_enable(bool enabled) { motion_enabled=enabled; }
lv_obj_t *uk_number_label_create(lv_obj_t *parent)
{
    lv_obj_t *obj=lv_obj_class_create_obj(&number_class,parent);
    lv_obj_class_init_obj(obj);
    return obj;
}
static void number_destroy(const lv_obj_class_t *cls, lv_obj_t *obj)
{
    (void)cls; clear_layers(obj);
}
static void sample_layers(number_label_t *n, uint32_t elapsed)
{
    for (glyph_layer_t *g=n->layers;g;g=g->next) {
        float p=elapsed<=g->delay ? 0 : apple_ease((float)(elapsed-g->delay)/(UK_NUMBER_MS ? UK_NUMBER_MS : 1));
        g->x=g->from_x+(g->to_x-g->from_x)*p;
        g->y=g->from_y+(g->to_y-g->from_y)*p;
        g->alpha=g->from_alpha+(g->to_alpha-g->from_alpha)*p;
    }
}
static void number_step(void *obj, int32_t elapsed)
{
    number_label_t *n=obj;
    sample_layers(n,(uint32_t)elapsed);
    if ((uint32_t)elapsed>=n->span) clear_layers(obj);
    else lv_obj_invalidate(obj);
}
static int char_count(const char *text)
{
    int count=0; uint32_t i=0;
    while (text[i]) { lv_text_encoded_next(text,&i); count++; }
    return count;
}
static glyph_layer_t *add_layer(number_label_t *n, int key, uint32_t cp, float x, float y, float alpha)
{
    if (layer_bytes+sizeof(glyph_layer_t)>UK_NUMBER_BUDGET_BYTES) return NULL;
    glyph_layer_t *g=uk_alloc(sizeof *g);
    if (!g) return NULL;
    layer_bytes+=sizeof *g;
    *g=(glyph_layer_t){.next=n->layers,.key=key,.cp=cp,
        .x=x,.y=y,.alpha=alpha,.to_x=x,.to_y=y,.to_alpha=alpha};
    n->layers=g;
    return g;
}
static bool seed_layers(lv_obj_t *obj, const char *text)
{
    number_label_t *n=(number_label_t *)obj;
    int key=-char_count(text); uint32_t i=0; float x=0;
    int32_t spacing=lv_obj_get_style_text_letter_space(obj,0);
    while (text[i]) {
        uint32_t cp=lv_text_encoded_next(text,&i), j=i;
        uint32_t next=text[j] ? lv_text_encoded_next(text,&j) : 0;
        if (!add_layer(n,key++,cp,x,0,1)) return false;
        x+=lv_font_get_glyph_width(n->font,cp,next)+spacing;
    }
    return true;
}
void uk_number_set_text(lv_obj_t *obj, const char *text, bool animate)
{
    if (!obj) return;
    if (!text) text="";
    if (lv_obj_get_class(obj)!=&number_class) { lv_label_set_text(obj,text); return; }
    number_label_t *n=(number_label_t *)obj;
    const char *old=lv_label_get_text(obj);
    const lv_font_t *font=lv_obj_get_style_text_font(obj,0);
    bool eligible=animate && motion_enabled && !UK_NUMBER_REDUCED && UK_NUMBER_MS>0 &&
        lv_obj_is_visible(obj) && has_reading(old) && has_reading(text) &&
        same_numeric_structure(old,text) &&
        (!n->active || n->font==font);
    if (!eligible) {
        clear_layers(obj);
        if (strcmp(old,text)) lv_label_set_text(obj,text);
        return;
    }
    if (!strcmp(old,text)) return; /* Same target never restarts a running transition. */
    n->font=font;
    lv_obj_update_layout(obj);
    lv_point_t old_size,new_size;
    int32_t spacing=lv_obj_get_style_text_letter_space(obj,0);
    lv_text_get_size(&old_size,old,font,spacing,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    lv_text_get_size(&new_size,text,font,spacing,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    /* Wrapped/ellipsized text and shrinking content-sized masks use native text.
       A smaller mask cannot retain all outgoing digits without painting over units. */
    if ((lv_obj_get_style_width(obj,0)==LV_SIZE_CONTENT && char_count(text)<char_count(old)) ||
        old_size.x>lv_obj_get_content_width(obj) || old_size.y>lv_font_get_line_height(font) ||
        (lv_obj_get_style_width(obj,0)!=LV_SIZE_CONTENT && !obj->w_layout &&
         new_size.x>lv_obj_get_content_width(obj))) {
        clear_layers(obj); lv_label_set_text(obj,text); return;
    }
    lv_area_t old_content; lv_obj_get_content_coords(obj,&old_content);
    lv_text_align_t alignment=lv_obj_get_style_text_align(obj,0);
    int32_t old_origin=old_content.x1+(alignment==LV_TEXT_ALIGN_RIGHT ?
        lv_area_get_width(&old_content)-old_size.x : alignment==LV_TEXT_ALIGN_CENTER ?
        (lv_area_get_width(&old_content)-old_size.x)/2 : 0);
    if (n->active) sample_layers(n,lv_tick_elaps(n->started));
    else if (!seed_layers(obj,old)) { clear_layers(obj); lv_label_set_text(obj,text); return; }
    lv_anim_delete(obj,number_step);
    glyph_layer_t **slot=&n->layers;
    while (*slot) {
        glyph_layer_t *g=*slot;
        if (g->alpha<=0 && g->to_alpha==0) { *slot=g->next; layer_bytes-=sizeof *g; lv_free(g); }
        else slot=&g->next;
    }
    float distance=(float)lv_font_get_line_height(font)*UK_NUMBER_TRAVEL_PCT/100;
    /* Preserve every presented layer during retargeting, including a partially
       faded incoming glyph. No queue of logical values and no restart jump. */
    for (glyph_layer_t *g=n->layers;g;g=g->next) {
        g->presented=true;
        g->from_x=g->x; g->from_y=g->y; g->from_alpha=g->alpha;
        g->to_x=g->x; g->to_y=distance; g->to_alpha=0; g->delay=0;
    }
    uint32_t i=0, changed=0, max_delay=0; int key=-char_count(text); float x=0;
    while (text[i]) {
        uint32_t cp=lv_text_encoded_next(text,&i), j=i;
        uint32_t next=text[j] ? lv_text_encoded_next(text,&j) : 0;
        glyph_layer_t *found=NULL;
        for (glyph_layer_t *g=n->layers;g;g=g->next)
            if (g->key==key && g->cp==cp && (!found || g->alpha>found->alpha)) found=g;
        bool changing=!found || found->alpha<.999f || fabsf(found->y)>.01f;
        uint32_t delay=changing ? LV_MIN(changed++*UK_NUMBER_STAGGER_MS,UK_NUMBER_MAX_STAGGER_MS) : 0;
        if (!found) found=add_layer(n,key,cp,x,-distance,0);
        if (!found) { clear_layers(obj); lv_label_set_text(obj,text); return; }
        found->from_x=found->x; found->from_y=found->y; found->from_alpha=found->alpha;
        found->to_x=x; found->to_y=0; found->to_alpha=1; found->delay=delay;
        /* Pair the outgoing digit with its incoming digit's delay. */
        for (glyph_layer_t *g=n->layers;g;g=g->next)
            if (g->key==key && g!=found) g->delay=delay;
        max_delay=LV_MAX(max_delay,delay);
        key++; x+=lv_font_get_glyph_width(font,cp,next)+spacing;
    }
    n->span=UK_NUMBER_MS+max_delay; n->started=lv_tick_get(); n->active=true;
    lv_label_set_text(obj,text); /* Public text is always the actual latest reading. */
    lv_obj_update_layout(obj);
    lv_area_t new_content; lv_obj_get_content_coords(obj,&new_content);
    int32_t new_origin=new_content.x1+(alignment==LV_TEXT_ALIGN_RIGHT ?
        lv_area_get_width(&new_content)-new_size.x : alignment==LV_TEXT_ALIGN_CENTER ?
        (lv_area_get_width(&new_content)-new_size.x)/2 : 0);
    for (glyph_layer_t *g=n->layers;g;g=g->next) if (g->presented) {
        g->from_x+=old_origin-new_origin;
        g->x=g->from_x;
    }
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a,obj);
    lv_anim_set_values(&a,0,(int32_t)n->span); lv_anim_set_duration(&a,n->span);
    lv_anim_set_exec_cb(&a,number_step); lv_anim_set_path_cb(&a,lv_anim_path_linear);
    lv_anim_start(&a);
}
static void number_draw(lv_event_t *e)
{
    lv_obj_t *obj=lv_event_get_current_target(e);
    number_label_t *n=(number_label_t *)obj;
    lv_layer_t *layer=lv_event_get_layer(e);
    lv_area_t content; lv_obj_get_content_coords(obj,&content);
    lv_area_t saved=layer->_clip_area, clip;
    if (!lv_area_intersect(&clip,&saved,&content)) return;
    layer->_clip_area=clip;
    lv_draw_label_dsc_t label; lv_draw_label_dsc_init(&label);
    lv_obj_init_draw_label_dsc(obj,0,&label);
    lv_point_t size;
    lv_text_get_size(&size,lv_label_get_text(obj),label.font,label.letter_space,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    int32_t shift=label.align==LV_TEXT_ALIGN_RIGHT ? lv_area_get_width(&content)-size.x :
        label.align==LV_TEXT_ALIGN_CENTER ? (lv_area_get_width(&content)-size.x)/2 : 0;
    float radius=(float)lv_font_get_line_height(label.font)*UK_NUMBER_BLUR_PCT/100;
    for (glyph_layer_t *g=n->layers;g;g=g->next) {
        if (g->alpha<=0) continue;
        lv_draw_label_dsc_t d=label;
        d.align=LV_TEXT_ALIGN_LEFT;
        d.opa=(lv_opa_t)(label.opa*LV_MIN(g->alpha,1.f));
        lv_point_t pos={content.x1+shift+(int32_t)lroundf(g->x),
                       content.y1+(int32_t)lroundf(g->y)};
        int32_t blur=(int32_t)lroundf(radius*(1-g->alpha));
        /* Five weighted glyph samples approximate a tiny blur without offscreen
           surfaces. All remain clipped to this label, never to adjacent units. */
        if (blur>0) {
            lv_opa_t full=d.opa; d.opa=full/8;
            const lv_point_t taps[]={{-blur,0},{blur,0},{0,-blur},{0,blur}};
            for (unsigned i=0;i<sizeof taps/sizeof taps[0];i++) {
                lv_point_t p={pos.x+taps[i].x,pos.y+taps[i].y}; lv_draw_character(layer,&d,&p,g->cp);
            }
            d.opa=full/2;
        }
        lv_draw_character(layer,&d,&pos,g->cp);
    }
    layer->_clip_area=saved;
}
static void number_event(const lv_obj_class_t *cls, lv_event_t *e)
{
    lv_obj_t *obj=lv_event_get_current_target(e);
    number_label_t *n=(number_label_t *)obj;
    if (lv_event_get_code(e)==LV_EVENT_DRAW_MAIN && n->active) {
        if (n->font==lv_obj_get_style_text_font(obj,0)) {
            /* Keep the native object background/border; replace only label text. */
            if (lv_obj_event_base(&lv_label_class,e)!=LV_RESULT_OK) return;
            number_draw(e); return;
        }
        clear_layers(obj);
    }
    lv_obj_event_base(cls,e);
}
#ifdef FNOS_UI_TESTING
#include <assert.h>
void uk_number_selfcheck(lv_obj_t *parent)
{
    assert(apple_ease(0)==0 && apple_ease(1)==1);
    assert(fabsf(apple_ease(.5f)-.6846432f)<.0001f);
    if (UK_NUMBER_REDUCED || UK_NUMBER_MS==0) return;
    lv_obj_t *o=uk_number_label_create(parent);
    lv_obj_set_style_text_font(o,UK_FONT_NUM_32,0);
    uk_number_set_text(o,"65%",false); lv_obj_update_layout(o);
    uk_number_set_text(o,"66%",true);
    number_label_t *n=(number_label_t *)o;
    assert(n->active);
    sample_layers(n,UK_NUMBER_MS/3);
    n->started=lv_tick_get()-UK_NUMBER_MS/3;
    glyph_layer_t *before=n->layers;
    float y=before->y,alpha=before->alpha;
    uk_number_set_text(o,"67%",true);
    assert(fabsf(before->y-y)<.001f && fabsf(before->alpha-alpha)<.001f);
    uint32_t began=n->started; uk_number_set_text(o,"67%",true); assert(n->started==began);
    uk_number_set_text(o,"--",false); assert(!n->active && !n->layers);
    uk_number_set_text(o,"99",false); lv_obj_update_layout(o);
    uk_number_set_text(o,"100",true); assert(n->active);
    uk_number_settle(o); assert(!n->active && !strcmp(lv_label_get_text(o),"100"));
    uk_number_set_text(o,"99",true); assert(!n->active && !strcmp(lv_label_get_text(o),"99"));
    uk_number_set_text(o,"100",false);
    uk_test_alloc_fail_after(0); uk_number_set_text(o,"101",true);
    assert(uk_test_alloc_was_triggered() && !n->active && !strcmp(lv_label_get_text(o),"101"));
    uk_test_alloc_fail_after(-1); uk_alloc_reset();
    uk_number_set_text(o,"102",true); assert(n->active);
    uk_number_set_text(o,"1 GB",true); assert(!n->active);
    uk_number_motion_enable(false); uk_number_set_text(o,"2 GB",true); assert(!n->active);
    uk_number_motion_enable(true); uk_number_set_text(o,"3 GB",true); assert(n->active);
    size_t used=layer_bytes;
    lv_obj_delete(o); assert(layer_bytes<used); /* Destructor must remove in-flight callbacks and layers. */
    uk_kpi_t *k=uk_kpi_create(parent,"rate");
    lv_obj_set_width(k->box,LV_PCT(100)); lv_obj_set_height(k->box,LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(k->box,0);
    uk_kpi_set(k,"9.9","MB/s","",-1); lv_obj_update_layout(parent);
    uk_kpi_set(k,"10.0","MB/s","",-1);
    assert(((number_label_t *)k->val)->active);
    uk_kpi_set(k,"1.0","GB/s","",-1); assert(!((number_label_t *)k->val)->active);
    lv_obj_delete(k->box); lv_free(k);
    /* 行内读数（存储页的卷/阵列/磁盘形态）：第二行是每秒都在变的**数据**，
       它既不能挡住 val 的过渡，自己也要能滑（见 uk_row_set 的 same_reading）。 */
    uk_row_t *r=uk_row_create(parent,false,true);
    uk_row_set(r,"/vol1","已用 5.4 TB / 总计 7.3 TB · btrfs","75%",NULL,75,0);
    lv_obj_update_layout(parent);
    uk_row_set(r,"/vol1","已用 5.5 TB / 总计 7.3 TB · btrfs","76%",NULL,76,0);
    assert(((number_label_t *)r->val)->active);
    assert(((number_label_t *)r->name2)->active);
    lv_obj_delete(r->row); lv_free(r);
}
#endif
