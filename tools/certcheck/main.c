/* 读一个 PEM 文件，用固件那份 pem_fingerprint() 算指纹并打印。
   见 run.sh 的说明：这是为了让板子和 NAS 上那两串指纹"同源"这件事可验证。 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool pem_fingerprint(const char *pem, char *out, size_t out_cap);
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "用法：%s <cert.pem>\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    /* 先看大小再说"读不出来"（审计 M16）：fopen 一个目录照样成功（fread 才失败），
       而超过缓冲的证书会被静默截断到 16 KB，pem_fingerprint 于是返回 false，
       报出来的却是"PEM 读不出来或太长"—— 三种完全不同的原因挤在一句话里。 */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "%s 不是可读的普通文件（目录？）\n", argv[1]);
        return 2;
    }
    long fsz = ftell(f);
    rewind(f);
    if (fsz < 0) { fclose(f); perror("ftell"); return 2; }
    static char buf[16384];
    if ((size_t)fsz >= sizeof buf) {
        fclose(f);
        fprintf(stderr, "输入太大：%ld 字节 ≥ 缓冲 %zu —— 这里只算单张证书的指纹\n",
                fsz, sizeof buf);
        return 2;
    }
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    bool short_read = ferror(f) != 0;
    fclose(f);
    if (short_read || n != (size_t)fsz) {
        fprintf(stderr, "只读到 %zu / %ld 字节（读取失败）\n", n, fsz);
        return 2;
    }
    buf[n] = 0;

    char fp[128];
    if (!pem_fingerprint(buf, fp, sizeof fp)) {
        fprintf(stderr, "pem_fingerprint 返回 false：没有成对有序的 BEGIN/END 证书边界（或指纹放不下）\n");
        return 1;
    }
    puts(fp);
    return 0;
}
