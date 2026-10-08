/* Platform-independent inventory parser; also compiled by the native preview. */
#include "fnos_data.h"
#include "cJSON.h"
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
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
    for (int i=0;i<s.nraid;i++) s.raid[i].dev = s.raid[i].lvl = s.raid[i].state = "";
    for (int i=0;i<s.ndisks;i++) s.disks[i].dev = "";
    for (int i=0;i<s.ntemps;i++) s.temps[i].dev = s.temps[i].ch = s.temps[i].dn = "";
    for (int i=0;i<s.ndocker;i++) s.docker[i].n = s.docker[i].s = "";
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
#define TEXT(field, object, key) do { if (!fnos_status_text(&s, &(field), "%s", string(object,key))) goto fail; } while (0)
    TEXT(s.host,root,"host"); s.uptime_s=number(root,"uptime_s");
    s.source_ts=number(root,"ts"); s.proto=number(root,"proto");
    const cJSON *o=item(root,"cpu");
    s.cpu.pct=number(o,"pct"); s.cpu.load1=number(o,"load1");
    s.cpu.load5=number(o,"load5"); s.cpu.load15=number(o,"load15");
    s.cpu.temp_c=number(o,"temp_c"); s.cpu.cores=number(o,"cores");
    s.cpu.runq=number(o,"runq"); s.cpu.procs=number(o,"procs");
    o=item(root,"mem");
    s.mem.total_mb=number(o,"total_mb"); s.mem.used_mb=number(o,"used_mb");
    s.mem.avail_mb=number(o,"avail_mb"); s.mem.pct=number(o,"pct");
    s.mem.swap_total_mb=number(o,"swap_total_mb"); s.mem.swap_used_mb=number(o,"swap_used_mb");
    o=item(root,"net");
    TEXT(s.net.ifname,o,"if");
    s.net.rx_kbs=number(o,"rx_kbs"); s.net.tx_kbs=number(o,"tx_kbs");
    s.net.rx_total_gb=number(o,"rx_total_gb"); s.net.tx_total_gb=number(o,"tx_total_gb");
    const cJSON *v=NULL;
    int i=0;
    cJSON_ArrayForEach(v,array(root,"vols")) {
        fnos_vol_t *r=&s.vols[i++]; TEXT(r->mnt,v,"mnt"); TEXT(r->fs,v,"fs");
        r->total_gb=number(v,"total_gb"); r->used_gb=number(v,"used_gb");
        r->free_gb=number(v,"free_gb"); r->pct=number(v,"pct");
    }
    i=0; cJSON_ArrayForEach(v,array(root,"raid")) {
        fnos_raid_t *r=&s.raid[i++]; TEXT(r->dev,v,"dev"); TEXT(r->lvl,v,"lvl"); TEXT(r->state,v,"state");
        r->ok=cJSON_IsTrue(item(v,"ok")); r->have=number(v,"have");
        r->want=number(v,"want"); r->sync_pct=number(v,"sync_pct");
    }
    i=0; cJSON_ArrayForEach(v,array(root,"disks")) {
        fnos_disk_t *r=&s.disks[i++]; TEXT(r->dev,v,"dev"); r->rd_kbs=number(v,"rd_kbs"); r->wr_kbs=number(v,"wr_kbs");
    }
    i=0; cJSON_ArrayForEach(v,array(root,"temps")) {
        fnos_temp_t *r=&s.temps[i++]; TEXT(r->dev,v,"dev"); TEXT(r->ch,v,"ch"); TEXT(r->dn,v,"dn");
        if (!r->dev[0]) { TEXT(r->dev,v,"n"); }
        r->c=number(v,"c");
    }
    if (s.ntemps>1) qsort(s.temps,s.ntemps,sizeof *s.temps,temp_cmp);
    i=0; cJSON_ArrayForEach(v,array(root,"docker")) {
        fnos_docker_t *r=&s.docker[i++]; TEXT(r->n,v,"n"); TEXT(r->s,v,"s"); r->up=cJSON_IsTrue(item(v,"up"));
    }
    i=0; cJSON_ArrayForEach(v,array(root,"alerts")) {
        fnos_alert_t *r=&s.alerts[i++]; TEXT(r->lv,v,"lv"); TEXT(r->m,v,"m");
    }
    const cJSON *modules = s.nmods ? item(root,"modules") : NULL;
    i=0; cJSON_ArrayForEach(v,modules) {
        fnos_mod_t *r=&s.mods[i++];
        if (!fnos_status_text(&s,&r->name,"%s",v->string ? v->string : "")) goto fail;
        const cJSON *state=item(v,"status");
        if (cJSON_IsString(state)) TEXT(r->status,v,"status");
        else if (!fnos_status_text(&s,&r->status,cJSON_IsNumber(state) ? "#%.0f" : "unknown",number(v,"status"))) goto fail;
    }
    i=0; cJSON_ArrayForEach(v,interfaces) {
        fnos_netif_t *r=&s.nets[i++]; TEXT(r->ifname,v,"if"); TEXT(r->state,v,"state");
        r->rx_kbs=number(v,"rx_kbs"); r->tx_kbs=number(v,"tx_kbs");
        r->rx_total_gb=number(v,"rx_total_gb"); r->tx_total_gb=number(v,"tx_total_gb");
        r->speed_mbps=number(v,"speed_mbps"); r->physical=cJSON_IsTrue(item(v,"physical"));
    }
    o=item(root,"zfs"); s.has_zfs=cJSON_IsObject(o);
    s.zfs_arc_gb=number(o,"arc_gb"); s.zfs_hit_pct=number(o,"hit_pct");
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
