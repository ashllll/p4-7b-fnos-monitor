/* Ownership and parser boundaries, using the production snapshot implementation. */
#include "fnos_data.h"
#include "cJSON.h"
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static fnos_status_t shared;
static void *reader(void *unused)
{
    (void)unused;
    fnos_status_t local={0};
    for (int i=0;i<4000;i++) {
        fnos_status_copy(&local,&shared);
        assert(local.ntemps==193 && !strcmp(local.temps[192].ch,"sensor-192"));
        fnos_status_release(&local);
    }
    return NULL;
}

int main(void)
{
    cJSON *root=cJSON_CreateObject();
    cJSON_AddBoolToObject(root,"ready",true);
    cJSON_AddStringToObject(root,"host","independent-parser-check");
    cJSON *modules=cJSON_AddObjectToObject(root,"modules");
    cJSON *net=cJSON_AddObjectToObject(modules,"net");
    cJSON_AddStringToObject(net,"status","stale");
    cJSON *future=cJSON_AddObjectToObject(modules,"future-module-with-a-complete-long-name");
    cJSON_AddStringToObject(future,"status","ok");
    cJSON *temps=cJSON_AddArrayToObject(root,"temps");
    for (int i=0;i<193;i++) {
        cJSON *t=cJSON_CreateObject(); char channel[32];
        snprintf(channel,sizeof channel,"sensor-%03d",i);
        cJSON_AddStringToObject(t,"dev","a-stable-device-id-longer-than-the-old-buffer");
        cJSON_AddStringToObject(t,"dn","device model with a complete long readable identity");
        cJSON_AddStringToObject(t,"ch",channel);
        cJSON_AddNumberToObject(t,"c",40+i*.1);
        cJSON_AddItemToArray(temps,t);
    }
    cJSON *vols=cJSON_AddArrayToObject(root,"vols");
    char path[3601]; memset(path,'a',sizeof path-1); path[0]='/'; path[sizeof path-1]=0;
    for (int i=0;i<25;i++) {
        cJSON *v=cJSON_CreateObject();
        cJSON_AddStringToObject(v,"mnt",path); cJSON_AddItemToArray(vols,v);
    }
    char *json=cJSON_PrintUnformatted(root);
    /* 分配失败时 json 是 NULL：以前这里直接 strlen(NULL)（审计 M16）。
       这条路径跑在 ASan/UBSan 下，崩溃点会落在 cJSON 里，看着像库坏了。 */
    if (!json) { fprintf(stderr,"cJSON_PrintUnformatted 返回 NULL（内存不够）\n"); return 1; }
    const char *reason=NULL;
    assert(strlen(json)>24*1024);
    assert(fnos_status_parse(json,&shared,&reason));
    assert(shared.ntemps==193 && shared.nvols==25);
    assert(shared.nmods==2 && !strcmp(shared.mods[0].status,"stale"));
    assert(!strcmp(shared.mods[1].name,"future-module-with-a-complete-long-name"));
    assert(!strcmp(shared.vols[24].mnt,path));
    pthread_t workers[4];
    for (int i=0;i<4;i++) assert(!pthread_create(&workers[i],NULL,reader,NULL));
    for (int i=0;i<4;i++) assert(!pthread_join(workers[i],NULL));
    fnos_status_t previous={0};
    fnos_status_copy(&previous,&shared);
    assert(fnos_status_parse("{\"ready\":true,\"host\":\"replacement\",\"temps\":[],\"modules\":[]}",&shared,&reason));
    assert(!shared.ntemps && !shared.nmods);
    assert(previous.ntemps==193 && !strcmp(previous.vols[24].mnt,path));
    assert(!fnos_status_parse("not JSON",&shared,&reason));
    assert(!strcmp(shared.host,"replacement"));
    fnos_counts_t impossible={.temps=INT_MAX};
    assert(!fnos_status_create(&shared,&impossible));
    assert(!strcmp(shared.host,"replacement"));
    /* A syntactically valid frame can exceed the inventory budget through names.
       Reject the whole frame and preserve the previous complete snapshot. */
    size_t cap = CONFIG_FNOS_SNAPSHOT_MAX_BYTES;
    char *oversized = malloc(cap + 64);
    assert(oversized);
    strcpy(oversized, "{\"ready\":true,\"host\":\"");
    size_t prefix = strlen(oversized);
    memset(oversized + prefix, 'a', cap);
    strcpy(oversized + prefix + cap, "\"}");
    assert(!fnos_status_parse(oversized,&shared,&reason));
    assert(reason && !strcmp(reason,"data capacity"));
    assert(!strcmp(shared.host,"replacement"));
    free(oversized);
    fnos_status_release(&shared); fnos_status_release(&previous);
    cJSON_free(json); cJSON_Delete(root);
    puts("PASS: full large frame, long IDs, modules, concurrent readers, replacement, shrink, malformed input, byte budget rejection");

    fnos_status_t metrics={0};
    assert(fnos_status_parse("{\"ready\":true,\"disks\":[{\"dev\":\"sda\",\"read_ms\":0,\"write_ms\":null,\"smart\":\"failed\",\"smart_ts\":1700000000}],\"docker\":[{\"n\":\"worker\",\"id\":\"stable-id\",\"cpu_pct\":250.5,\"mem_mb\":128,\"restarts\":0,\"metrics_ts\":1700000001}]}",&metrics,&reason));
    assert(metrics.disks[0].has_read_ms && metrics.disks[0].read_ms==0 && !metrics.disks[0].has_write_ms);
    assert(!strcmp(metrics.disks[0].smart,"failed") && metrics.disks[0].smart_ts==1700000000);
    assert(metrics.docker[0].has_cpu && metrics.docker[0].cpu_pct==250.5f);
    assert(metrics.docker[0].has_mem && metrics.docker[0].has_restarts && metrics.docker[0].restarts==0);
    assert(!strcmp(metrics.docker[0].id,"stable-id"));
    assert(fnos_status_parse("{\"ready\":true,\"disks\":[{}],\"docker\":[{\"cpu_pct\":-1,\"mem_mb\":1e100,\"restarts\":1.5}]}",&metrics,&reason));
    assert(!metrics.disks[0].has_read_ms && !metrics.disks[0].smart[0]);
    assert(!metrics.docker[0].has_cpu && !metrics.docker[0].has_mem && !metrics.docker[0].has_restarts);
    fnos_status_release(&metrics);
    puts("PASS optional metrics: real zero, null, absent, negative, overflow, fractional restart and stable identity");
    return 0;
}
