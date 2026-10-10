/* 把一份 /api/v1/status 的 JSON 喂给固件里那个 parse_status()，把解析结果打出来。
   见 run.sh：这一层的意义是"板子到底能不能吃下满载的那一帧"。 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fnos_data.h"

bool parse_status(const char *json, fnos_status_t *st);
bool parse_history(const char *js);
int fnos_data_hist_read(int64_t since_seq, fnos_sample_t *out, int max, int64_t *next_seq);

/* 抠出来的代码里那两个文件级静态（见 extract.py 的替身段） */
extern fnos_status_t s_status;
extern int64_t s_seq;

/* 简易 UTF-8 校验：拒绝孤立续接字节、截断的序列、以及 5/6 字节的非法前导。 */
static bool utf8_ok(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int need;
        if (*p < 0x80)                       { p++; continue; }
        else if ((*p & 0xE0) == 0xC0) need = 1;
        else if ((*p & 0xF0) == 0xE0) need = 2;
        else if ((*p & 0xF8) == 0xF0) need = 3;
        else return false;
        p++;
        while (need--) {
            if ((*p & 0xC0) != 0x80) return false;
            p++;
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "用法：%s <status.json>\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static char buf[64 * 1024];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;

    static fnos_status_t st;                 /* 与固件一样放静态区，别压栈 */
    memset(&st, 0, sizeof st);
    bool ok = parse_status(buf, &st);
    printf("parse=%s bytes=%zu\n", ok ? "ok" : "FAIL", n);
    if (!ok) return 1;

    printf("host=%s uptime=%u proto=%d\n", st.host, (unsigned)st.uptime_s, st.proto);
    printf("cpu=%.1f cores=%d load1=%.2f runq=%d temp=%.1f procs=%d\n",
           st.cpu.pct, st.cpu.cores, st.cpu.load1, st.cpu.runq, st.cpu.temp_c, st.cpu.procs);
    printf("mem=%.1f/%.1f pct=%.1f swap=%.1f/%.1f\n", st.mem.used_mb, st.mem.total_mb,
           st.mem.pct, st.mem.swap_used_mb, st.mem.swap_total_mb);
    printf("net=%s rx=%.1f tx=%.1f\n", st.net.ifname, st.net.rx_kbs, st.net.tx_kbs);
    printf("counts vols=%d raid=%d disks=%d temps=%d docker=%d alerts=%d mods=%d\n",
           st.nvols, st.nraid, st.ndisks, st.ntemps, st.ndocker, st.nalerts, st.nmods);
    if (st.nvols)    printf("vol0=%s pct=%.1f\n", st.vols[0].mnt, st.vols[0].pct);
    if (st.nraid)    printf("raid0=%s state=%s have=%d/%d health=%s what=%s\n", st.raid[0].dev,
                            st.raid[0].state, st.raid[0].have, st.raid[0].want,
                            st.raid[0].health, st.raid[0].what);
    if (st.ndisks)   printf("disk0=%s rd=%.1f\n", st.disks[0].dev, st.disks[0].rd_kbs);
    if (st.ntemps)   printf("temp0=%s · %s c=%.1f dn=[%s]\n",
                            st.temps[0].dev, st.temps[0].ch, st.temps[0].c, st.temps[0].dn);
    /* dn（人读设备名）必须落进结构体：它是"这到底是什么设备"的唯一来源，
       解析丢了界面上就退回 enp1s0 这种内核 id（用户报障的正是"只看到 NIC 这种缩写"）。 */
    if (st.ntemps && !st.temps[0].dn[0]) {
        printf("BADTEMP dn 没解析出来（dev=[%s]）\n", st.temps[0].dev);
        return 8;
    }
    /* 老采集端（还没升级的 NAS）只发拼好的 n：解析层要把它整条放进 dev，ch 留空。
       生成器把**最后一路**造成这种形状，这里断言回退真的生效 —— 否则新固件遇到旧
       采集端会显示一整列空白设备名，而"名字为空"在界面上看不出是解析问题。 */
    if (st.ntemps >= 2) {
        /* 温度表现在按"设备名+通道名"固定排序（fnos_data.c 的 temp_cmp），
           所以那一路**不再固定在最后一行** —— 按名字找它，别按位置找。 */
        const fnos_temp_t *legacy = NULL;
        for (int i = 0; i < st.ntemps; i++) {
            if (strncmp(st.temps[i].dev, "old-collector", 13) == 0) { legacy = &st.temps[i]; break; }
        }
        printf("temp_legacy dev=[%s] ch=[%s]\n",
               legacy ? legacy->dev : "(没找到只给 n 的那一路)", legacy ? legacy->ch : "");
        if (!legacy || !legacy->dev[0] || legacy->ch[0]) {
            printf("BADTEMP 老采集端回退失效：dev=[%s] ch=[%s]\n",
                   legacy ? legacy->dev : "-", legacy ? legacy->ch : "-");
            return 7;
        }
    }
    if (st.ndocker)  printf("dock0=%s up=%d\n", st.docker[0].n, (int)st.docker[0].up);
    if (st.nalerts)  printf("alert0=%s: %s\n", st.alerts[0].lv, st.alerts[0].m);
    if (st.nmods)    printf("mod0=%s/%s\n", st.mods[0].name, st.mods[0].status);

    /* 截断过的字符串必须是**合法 UTF-8**：半截字符在屏幕上是豆腐块或乱码，
       而且看不出是"被截断"还是"数据本身坏了"。这里逐字段验一遍。 */
    const char *fields[] = { st.host, st.net.ifname, st.last_err,
                             st.vols[0].mnt, st.raid[0].dev, st.raid[0].state,
                             st.docker[0].n, st.docker[0].s, st.temps[0].dev, st.temps[0].ch,
                             st.alerts[0].lv, st.alerts[0].m };
    int bad = 0;
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        if (!utf8_ok(fields[i])) {
            printf("BADUTF8 field#%zu: ", i);
            for (const unsigned char *p = (const unsigned char *)fields[i]; *p; p++)
                printf("%02X ", *p);
            printf("\n");
            bad++;
        }
    }
    printf("utf8=%s\n", bad ? "FAIL" : "ok");

    /* 历史回填：同一份解析器在真机上只在开机那一次跑，之前从没在设备外执行过。
       断言的是"解析成功 + 时间语义算对了"——曲线横轴靠 hist_span_s 换算成分钟，
       算错的话界面上写的就是错的时长。 */
    if (argc >= 3) {
        FILE *hf = fopen(argv[2], "rb");
        if (!hf) { perror(argv[2]); return 2; }
        static char hbuf[512 * 1024];
        size_t hn = fread(hbuf, 1, sizeof hbuf - 1, hf);
        fclose(hf);
        hbuf[hn] = 0;
        bool hok = parse_history(hbuf);
        printf("history parse=%s bytes=%zu n_seq=%lld ts_ok=%d span_s=%d gap_x10=%d\n",
               hok ? "ok" : "FAIL", hn, (long long)s_seq, (int)s_status.hist_ts_ok,
               s_status.hist_span_s, s_status.hist_avg_gap_x10);
        if (!hok) return 4;
        /* **必须比精确值**，不能只断言"大于 0"：把 `ts_last - ts_first` 写成 `ts_last`
           时 span 会变成一个一亿多的数，而 `> 0` 照样通过（证伪时就是这么漏掉的）。
           期望值由 run.sh 从同一份生成器算出来传进来。 */
        int want_span = (argc >= 4) ? atoi(argv[3]) : -1;
        int want_gap  = (argc >= 5) ? atoi(argv[4]) : -1;
        if (!s_status.hist_ts_ok) {
            printf("BADHIST hist_ts_ok=0 —— 有可用 ts 却没标出来\n");
            return 5;
        }
        if (s_status.hist_span_s != want_span || s_status.hist_avg_gap_x10 != want_gap) {
            printf("BADHIST span_s=%d（期望 %d）gap_x10=%d（期望 %d）\n",
                   s_status.hist_span_s, want_span, s_status.hist_avg_gap_x10, want_gap);
            return 5;
        }
        /* ── 再把环形缓冲**读回来**对一遍 ──
           parse_history 往 s_h_* 里写，fnos_data_hist_read 把它们读出来画曲线。
           这两头从来没在设备外跑过，也从来没一起跑过：写进去的值顺序对不对、
           窗口落后太多时会不会给错段，只有读回来才知道。 */
        if (argc >= 7) {
            fnos_sample_t *sm = calloc(4096, sizeof *sm);
            int64_t next = 0;
            int got = fnos_data_hist_read(0, sm, 4096, &next);
            int want_n = atoi(argv[5]);
            double w_cpu0 = atof(argv[6]), w_rx0 = atof(argv[7]);
            printf("history read=%d 行（写完应当是 %d），next_seq=%lld\n",
                   got, want_n, (long long)next);
            if (got != want_n) { printf("BADHISTREAD 读回来的行数是 %d，应当是 %d\n", got, want_n); return 6; }
            /* 生成器写的是 cpu = 30.0 + i%50、rx = 1000.0 + i，所以第 0 行与
               最后一行是可以算出来的 —— 用生成器传进来的期望值比，别在这儿重算 */
            if (sm[0].cpu < w_cpu0 - 0.01 || sm[0].cpu > w_cpu0 + 0.01 ||
                sm[0].rx_kbs < w_rx0 - 0.01 || sm[0].rx_kbs > w_rx0 + 0.01) {
                printf("BADHISTREAD 第 0 行对不上：cpu=%.2f（期望 %.2f）rx=%.2f（期望 %.2f）\n",
                       sm[0].cpu, w_cpu0, sm[0].rx_kbs, w_rx0);
                return 6;
            }
            /* 相邻两行必须是"下一行"：顺序错了曲线就反着画 */
            for (int i = 1; i < got; i++) {
                if (sm[i].rx_kbs < sm[i - 1].rx_kbs - 0.01) {
                    printf("BADHISTREAD 第 %d 行比上一行小（rx %.2f < %.2f）—— 顺序错了\n",
                           i, sm[i].rx_kbs, sm[i - 1].rx_kbs);
                    return 6;
                }
            }
            /* 落后太多（since 远早于窗口）时只该给最后一窗，不该越界 */
            int64_t nx2 = 0;
            int got2 = fnos_data_hist_read(next - 100000, sm, 4096, &nx2);
            if (got2 > FNOS_HIST_MAX) {
                printf("BADHISTREAD 窗口外的请求要回了 %d 行，超过环形缓冲 %d\n", got2, FNOS_HIST_MAX);
                return 6;
            }
            printf("history 回读：顺序正确、窗口夹取正确（落后 100000 时给 %d 行）\n", got2);
            free(sm);
        }
    }
    return bad ? 3 : 0;
}
