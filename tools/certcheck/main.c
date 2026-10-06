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
    static char buf[16384];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;

    char fp[128];
    if (!pem_fingerprint(buf, fp, sizeof fp)) {
        fprintf(stderr, "pem_fingerprint 返回 false（PEM 读不出来或太长）\n");
        return 1;
    }
    puts(fp);
    return 0;
}
