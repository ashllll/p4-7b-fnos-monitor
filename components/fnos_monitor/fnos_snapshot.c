/* Platform-independent inventory parser; also compiled by the native preview. */
#include "fnos_data.h"
#include "cJSON.h"
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <float.h>
#include <math.h>
#include <limits.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#ifndef CONFIG_FNOS_SNAPSHOT_MAX_BYTES
#define CONFIG_FNOS_SNAPSHOT_MAX_BYTES (1024 * 1024)
#endif

typedef struct block { struct block *next; max_align_t alignment; } block_t;
typedef struct { atomic_uint refs; size_t bytes; block_t *blocks; } storage_t;

static void *snapshot_malloc(size_t n)
{
#ifdef ESP_PLATFORM
    /* Inventory must not consume the internal heap needed by display and Wi-Fi. */
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(n);
#endif
}

static void *owned_alloc(fnos_status_t *s, size_t count, size_t size)
{
    storage_t *owner = s->storage;
    if (!owner || size == 0 || count > (SIZE_MAX - sizeof(block_t)) / size) return NULL;
    size_t bytes = count * size + sizeof(block_t);
    if (bytes > CONFIG_FNOS_SNAPSHOT_MAX_BYTES ||
        owner->bytes > CONFIG_FNOS_SNAPSHOT_MAX_BYTES - bytes) return NULL;
    block_t *b = snapshot_malloc(bytes);
    if (!b) return NULL;
    b->next = owner->blocks;
    owner->blocks = b;
    owner->bytes += bytes;
    void *data = b + 1;
    memset(data, 0, count * size);
    return data;
}

static bool owned_text(fnos_status_t *s, const char **field, const char *value)
{
    /* JSON strings are already formatted; empty values need no owned block. */
    if (!value[0]) { *field = ""; return true; }
    size_t n = strlen(value) + 1;
    char *text = owned_alloc(s, n, 1);
    if (!text) return false;
    memcpy(text, value, n);
    *field = text;
    return true;
}

void fnos_status_release(fnos_status_t *s)
{
    if (!s) return;
    storage_t *o = s->storage;
    if (o && atomic_fetch_sub_explicit(&o->refs, 1, memory_order_acq_rel) == 1) {
        block_t *b = o->blocks;
        while (b) { block_t *next = b->next; free(b); b = next; }
        free(o);
    }
    memset(s, 0, sizeof *s);
    s->host = s->net.ifname = s->net.state = "";
}

void fnos_status_copy(fnos_status_t *out, const fnos_status_t *source)
{
    if (!out || !source || out == source) return;
    storage_t *o = source->storage;
    if (o) atomic_fetch_add_explicit(&o->refs, 1, memory_order_relaxed);
    fnos_status_release(out);
    *out = *source;
    if (!out->host) out->host = "";
    if (!out->net.ifname) out->net.ifname = "";
    if (!out->net.state) out->net.state = "";
}

bool fnos_status_create(fnos_status_t *out, const fnos_counts_t *c)
{
    fnos_status_t s = {0};
    s.host = s.net.ifname = s.net.state = "";
    storage_t *o = snapshot_malloc(sizeof *o);
    if (!o) return false;
    memset(o, 0, sizeof *o);
    atomic_init(&o->refs, 1);
    s.storage = o;
#define VECTOR(member, number, count) do { \
    if (c->count < 0) goto fail; \
    s.number = c->count; \
    if (s.number && !(s.member = owned_alloc(&s, s.number, sizeof *s.member))) goto fail; \
} while (0)
    VECTOR(vols, nvols, vols); VECTOR(raid, nraid, raid);
    VECTOR(disks, ndisks, disks); VECTOR(temps, ntemps, temps);
    VECTOR(docker, ndocker, docker); VECTOR(alerts, nalerts, alerts);
    VECTOR(mods, nmods, mods); VECTOR(nets, nnets, nets);
#undef VECTOR
    for (int i=0;i<s.nvols;i++) s.vols[i].mnt = s.vols[i].fs = "";
    for (int i=0;i<s.nraid;i++) s.raid[i].dev = s.raid[i].lvl = s.raid[i].state =
        s.raid[i].health = s.raid[i].what = "";
    for (int i=0;i<s.ndisks;i++) s.disks[i].dev = s.disks[i].smart = "";
    for (int i=0;i<s.ntemps;i++) s.temps[i].dev = s.temps[i].ch = s.temps[i].dn = "";
    for (int i=0;i<s.ndocker;i++) s.docker[i].n = s.docker[i].s = s.docker[i].id = "";
    for (int i=0;i<s.nalerts;i++) s.alerts[i].lv = s.alerts[i].m = "";
    for (int i=0;i<s.nmods;i++) s.mods[i].name = s.mods[i].status = "";
    for (int i=0;i<s.nnets;i++) s.nets[i].ifname = s.nets[i].state = "";
    fnos_status_release(out);
    *out = s;
    return true;
fail:
    fnos_status_release(&s);
    return false;
}

bool fnos_status_text(fnos_status_t *s, const char **field, const char *fmt, ...)
{
    va_list ap, size_ap;
    va_start(ap, fmt); va_copy(size_ap, ap);
    int n = vsnprintf(NULL, 0, fmt, size_ap);
    va_end(size_ap);
    char *text = n >= 0 ? owned_alloc(s, (size_t)n + 1, 1) : NULL;
    if (text) { vsnprintf(text, (size_t)n + 1, fmt, ap); *field = text; }
    va_end(ap);
    return text != NULL;
}

static const cJSON *item(const cJSON *o, const char *key)
{ return cJSON_GetObjectItemCaseSensitive(o, key); }
static double number(const cJSON *o, const char *key)
{ const cJSON *v=item(o,key); return cJSON_IsNumber(v) ? v->valuedouble : 0; }
/* 采集端给的是 JSON number，落到窄类型上要先夹：double→int/uint/float 在越界与
   NaN/±inf 时是 UB（审计 M03）。实机表现是屏上出现垃圾数而不是崩溃，所以没有
   崩溃现场可查——只能在这里一次性收口。9e15 在 double 里精确，且在 int64 范围内。 */
static double clampd(double v, double lo, double hi)
{ if (v != v) return lo; if (v < lo) return lo; if (v > hi) return hi; return v; }
static int      inum(const cJSON *o, const char *k)
{ return (int)clampd(number(o,k), -2147483648.0, 2147483647.0); }
static uint32_t unum(const cJSON *o, const char *k)
{ return (uint32_t)clampd(number(o,k), 0.0, 4294967295.0); }
static int64_t  lnum(const cJSON *o, const char *k)
{ return (int64_t)clampd(number(o,k), -9.0e15, 9.0e15); }
static float    fnum(const cJSON *o, const char *k)
{ return (float)clampd(number(o,k), -FLT_MAX, FLT_MAX); }
/* Optional metrics preserve unknown vs genuine zero, including hostile numbers. */
static bool metric_number(const cJSON *o, const char *key)
{ const cJSON *v=item(o,key); return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble>=0 && v->valuedouble<=FLT_MAX; }
static const char *string(const cJSON *o, const char *key)
{ const cJSON *v=item(o,key); return cJSON_IsString(v) && v->valuestring ? v->valuestring : ""; }
static const cJSON *array(const cJSON *root, const char *key)
{ const cJSON *v=item(root,key); return cJSON_IsArray(v) ? v : NULL; }
static int count(const cJSON *root, const char *key)
{ return cJSON_GetArraySize(array(root,key)); }
static int temp_cmp(const void *a, const void *b)
{ const fnos_temp_t *x=a,*y=b; int n=strcmp(x->dev,y->dev); return n ? n : strcmp(x->ch,y->ch); }

bool fnos_status_parse(const char *json, fnos_status_t *out, const char **reason)
{
    if (reason) *reason = "bad payload";
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsTrue(item(root,"ready"))) { cJSON_Delete(root); return false; }
    const cJSON *net=item(root,"net");
    const cJSON *interfaces=item(net,"interfaces");
    if (!cJSON_IsArray(interfaces)) interfaces=array(root,"netifs");
    fnos_counts_t counts = { count(root,"vols"), count(root,"raid"), count(root,"disks"),
        count(root,"temps"), count(root,"docker"), count(root,"alerts"), cJSON_IsObject(item(root,"modules")) ? cJSON_GetArraySize(item(root,"modules")) : 0,
        cJSON_IsArray(interfaces) ? cJSON_GetArraySize(interfaces) : 0 };
    fnos_status_t s = {0};
    if (reason) *reason = "data capacity";
    if (!fnos_status_create(&s,&counts)) goto fail;
#define TEXT(field, object, key) do { if (!owned_text(&s, &(field), string(object,key))) goto fail; } while (0)
    TEXT(s.host,root,"host"); s.uptime_s=unum(root,"uptime_s");
    s.source_ts=lnum(root,"ts"); s.proto=inum(root,"proto");
    s.source_seq=lnum(root,"seq");
    int64_t source_end=lnum(root,"ts_ms");
    s.source_age_valid=metric_number(root,"age_s") && s.source_ts>0 && source_end>0;
    if (s.source_age_valid) {
        double age=number(root,"age_s")*1000.0-(source_end-s.source_ts*1000.0);
        s.source_age_ms=(int)clampd(age,0.0,INT_MAX);
    }
    const cJSON *o=item(root,"cpu");
    s.cpu.pct=fnum(o,"pct"); s.cpu.load1=fnum(o,"load1");
    s.cpu.load5=fnum(o,"load5"); s.cpu.load15=fnum(o,"load15");
    s.cpu.temp_c=fnum(o,"temp_c"); s.cpu.cores=inum(o,"cores");
    s.cpu.runq=inum(o,"runq"); s.cpu.procs=inum(o,"procs");
    o=item(root,"mem");
    s.mem.total_mb=fnum(o,"total_mb"); s.mem.used_mb=fnum(o,"used_mb");
    s.mem.avail_mb=fnum(o,"avail_mb"); s.mem.pct=fnum(o,"pct");
    s.mem.swap_total_mb=fnum(o,"swap_total_mb"); s.mem.swap_used_mb=fnum(o,"swap_used_mb");
    o=item(root,"net");
    TEXT(s.net.ifname,o,"if");
    s.net.rx_kbs=fnum(o,"rx_kbs"); s.net.tx_kbs=fnum(o,"tx_kbs");
    s.net.rx_total_gb=fnum(o,"rx_total_gb"); s.net.tx_total_gb=fnum(o,"tx_total_gb");
    /* 摘要行就是"选中网卡那一行"（采集端 summary = dict(selected)），里面同样带
       state/speed_mbps/physical；以前只解析了速率，摘要的这三项恒为 ""/0/false，
       凡是读摘要而不是读列表的消费者拿到的都是假值（审计 M02）。 */
    TEXT(s.net.state,o,"state");
    s.net.speed_mbps=inum(o,"speed_mbps"); s.net.physical=cJSON_IsTrue(item(o,"physical"));
    const cJSON *v=NULL;
    int i=0;
    cJSON_ArrayForEach(v,array(root,"vols")) {
        fnos_vol_t *r=&s.vols[i++]; TEXT(r->mnt,v,"mnt"); TEXT(r->fs,v,"fs");
        r->total_gb=fnum(v,"total_gb"); r->used_gb=fnum(v,"used_gb");
        r->free_gb=fnum(v,"free_gb"); r->pct=fnum(v,"pct");
    }
    i=0; cJSON_ArrayForEach(v,array(root,"raid")) {
        fnos_raid_t *r=&s.raid[i++]; TEXT(r->dev,v,"dev"); TEXT(r->lvl,v,"lvl"); TEXT(r->state,v,"state");
        /* 采集端 v1.2.5 起给结构健康度与维护动作；老采集端没有这两个键 ⇒ 空串 ⇒ 界面显示"状态未知"。 */
        TEXT(r->health,v,"health"); TEXT(r->what,v,"what");
        r->ok=cJSON_IsTrue(item(v,"ok")); r->have=inum(v,"have");
        r->want=inum(v,"want"); r->sync_pct=fnum(v,"sync_pct");
    }
    i=0; cJSON_ArrayForEach(v,array(root,"disks")) {
        fnos_disk_t *r=&s.disks[i++]; TEXT(r->dev,v,"dev"); r->rd_kbs=fnum(v,"rd_kbs"); r->wr_kbs=fnum(v,"wr_kbs");
        r->has_read_ms=metric_number(v,"read_ms"); r->read_ms=fnum(v,"read_ms");
        r->has_write_ms=metric_number(v,"write_ms"); r->write_ms=fnum(v,"write_ms");
        TEXT(r->smart,v,"smart"); r->smart_ts=lnum(v,"smart_ts");
    }
    i=0; cJSON_ArrayForEach(v,array(root,"temps")) {
        fnos_temp_t *r=&s.temps[i++]; TEXT(r->dev,v,"dev"); TEXT(r->ch,v,"ch"); TEXT(r->dn,v,"dn");
        if (!r->dev[0]) { TEXT(r->dev,v,"n"); }
        r->c=fnum(v,"c");
    }
    if (s.ntemps>1) qsort(s.temps,s.ntemps,sizeof *s.temps,temp_cmp);
    i=0; cJSON_ArrayForEach(v,array(root,"docker")) {
        fnos_docker_t *r=&s.docker[i++]; TEXT(r->n,v,"n"); TEXT(r->s,v,"s"); r->up=cJSON_IsTrue(item(v,"up"));
        TEXT(r->id,v,"id");
        r->has_cpu=metric_number(v,"cpu_pct"); r->cpu_pct=fnum(v,"cpu_pct");
        r->has_mem=metric_number(v,"mem_mb"); r->mem_mb=fnum(v,"mem_mb");
        r->has_restarts=metric_number(v,"restarts") && number(v,"restarts")<=UINT32_MAX && floor(number(v,"restarts"))==number(v,"restarts");
        r->restarts=unum(v,"restarts"); r->metrics_ts=lnum(v,"metrics_ts");
    }
    i=0; cJSON_ArrayForEach(v,array(root,"alerts")) {
        fnos_alert_t *r=&s.alerts[i++]; TEXT(r->lv,v,"lv"); TEXT(r->m,v,"m");
    }
    const cJSON *modules = s.nmods ? item(root,"modules") : NULL;
    i=0; cJSON_ArrayForEach(v,modules) {
        fnos_mod_t *r=&s.mods[i++];
        if (!owned_text(&s,&r->name,v->string ? v->string : "")) goto fail;
        const cJSON *state=item(v,"status");
        if (cJSON_IsString(state)) TEXT(r->status,v,"status");
        else if (!fnos_status_text(&s,&r->status,cJSON_IsNumber(state) ? "#%.0f" : "unknown",
                                   clampd(number(v,"status"), -9.0e15, 9.0e15))) goto fail;
    }
    i=0; cJSON_ArrayForEach(v,interfaces) {
        fnos_netif_t *r=&s.nets[i++]; TEXT(r->ifname,v,"if"); TEXT(r->state,v,"state");
        r->rx_kbs=fnum(v,"rx_kbs"); r->tx_kbs=fnum(v,"tx_kbs");
        r->rx_total_gb=fnum(v,"rx_total_gb"); r->tx_total_gb=fnum(v,"tx_total_gb");
        r->speed_mbps=inum(v,"speed_mbps"); r->physical=cJSON_IsTrue(item(v,"physical"));
    }
    o=item(root,"zfs"); s.has_zfs=cJSON_IsObject(o);
    s.zfs_arc_gb=fnum(o,"arc_gb"); s.zfs_hit_pct=fnum(o,"hit_pct");
    cJSON_ArrayForEach(v,item(item(root,"trunc"),"dropped")) {
        if (cJSON_IsNumber(v) && v->valuedouble>0) {
            double sum=s.source_dropped+v->valuedouble;
            s.source_dropped=sum>INT_MAX ? INT_MAX : (int)sum;
        }
    }
#undef TEXT
    cJSON_Delete(root);
    fnos_status_copy(out,&s);
    fnos_status_release(&s);
    if (reason) *reason = NULL;
    return true;
fail:
    cJSON_Delete(root);
    fnos_status_release(&s);
    return false;
}
