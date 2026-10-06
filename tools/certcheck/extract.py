#!/usr/bin/env python3
"""从 components/fnos_monitor/fnos_pair.c 里**原样**抠出 pem_fingerprint() 并补上它的两个
文件级缓冲，供主机侧编译运行。

为什么抠而不是抄一份：抄一份就等于"测的是我手抄的那份"，改了固件里的真函数测试也不会
知道（这正是本项目反复强调的那类假绿灯）。这里把抠出来的原文写进生成文件，并在旁边
留一条 sha256 注释；下次函数一改，`run.sh` 打印的日志里就能看出是同一份还是变了。

用法：python3 tools/certcheck/extract.py <fnos_pair.c> <输出 .c>
"""
import hashlib
import io
import os
import re
import sys


def main():
    src_path, out_path = sys.argv[1], sys.argv[2]
    src = io.open(src_path, encoding="utf-8").read()

    # 两个函数都要：指纹（用户逐段比对的依据）+ CN/到期（同一屏上显示给用户看的）
    fns = []
    for name, sig in (("pem_fingerprint",
                       r"^static bool pem_fingerprint\(const char \*pem, char \*out, size_t out_cap\)\n"),
                      # 只验 pem_fingerprint：cert_info 依赖 mbedtls 的 x509 整套
                      # （不是 base64/sha256 两个文件），为它把整个 library/ 编到
                      # 主机不值当——它做的只是把 mbedtls 解出的 subject 与
                      # valid_to 格式化一下，风险在编译期（mday→day 已经踩过）。
                      ):
        if name != "pem_fingerprint":
            continue
        # **按花括号配对切，别用非贪婪正则**：`cert_info()` 里面有嵌套的
        # `if (!crt) { … }`，`\{.*?\n\}` 会停在那个内层块的收尾行上，
        # 抠出来的东西少一半（报 `use of undeclared identifier 'crt'`）。
        m = re.search(sig, src, re.M)
        if not m:
            print(f"✗ 在 {src_path} 里找不到 {name}() 的定义"
                  f"（改了签名？那就同步改 tools/certcheck/extract.py）")
            return 1
        i = src.index("{", m.end() - 1)
        depth = 0
        for j in range(i, len(src)):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    break
        fns.append(src[m.start():j + 1])
    fn = "\n\n".join(fns)
    digest = hashlib.sha256(fn.encode("utf-8")).hexdigest()

    # 两个文件级缓冲的尺寸也照抄，别写死数字
    sizes = {}
    for name in ("s_b64", "s_der"):
        mm = re.search(r"^static .*?\b%s\[([^\]]+)\];" % name, src, re.M)
        if not mm:
            print(f"✗ 找不到 {name} 的声明（改了名字？同步改 extract.py）")
            return 1
        sizes[name] = mm.group(1)

    # 尺寸可能写成宏（FNOS_PAIR_PEM_MAX）——去头文件里把数值查出来，
    # 别把宏名原样抄进生成文件（那会变成自引用的 #define，编译不过）
    def resolve(tok, where):
        if re.fullmatch(r"\d+", tok):
            return tok
        for f in (where, src_path):
            d = os.path.dirname(f)
            for cand in (os.path.join(d, "fnos_pair.h"), f):
                if not os.path.exists(cand):
                    continue
                mm = re.search(r"^#define\s+%s\s+(\d+)" % re.escape(tok),
                               io.open(cand, encoding="utf-8").read(), re.M)
                if mm:
                    return mm.group(1)
        print(f"✗ {tok} 不是数字，也没在头文件里找到它的 #define —— 请手动确认它的值")
        return None

    for k in list(sizes):
        v = resolve(sizes[k], src_path)
        if v is None:
            return 1
        sizes[k] = v

    header = f'''/* 本文件由 tools/certcheck/extract.py 从 {src_path} 自动生成，**不要手改**。
 * 里面是固件那个 pem_fingerprint() 的原文（sha256 {digest}）。
 * 它只依赖 mbedtls 与下面两个缓冲，所以能直接编到主机上跑。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>   /* cert_info 的 PSRAM 回退分支用 calloc */
#include <stdio.h>
#include <string.h>
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

#define EXT_RAM_BSS_ATTR
static char s_b64[{sizes['s_b64']}];
static unsigned char s_der[{sizes['s_der']}];

/* 主机上没有 ESP 的堆分级 API：cert_info 先试 PSRAM、失败再退回 calloc，
   这里让 PSRAM 那次直接失败，走它自己的 calloc 回退分支——**那条分支在真机上
   也可能走到（PSRAM 分配失败时），所以顺带也验了它**。 */
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_8BIT   2
static void *heap_caps_calloc(size_t n, size_t sz, int caps)
{{                                     /* f-string 里的字面花括号要写两遍 */
    (void)n; (void)sz; (void)caps;
    return NULL;                      /* 强制走 calloc 回退 */
}}

/* 抠出来的函数原文里带了 static，主机这边要能从 main.c 调到 */
#define static
{fn}
#undef static
'''
    io.open(out_path, "w", encoding="utf-8").write(header)
    print(f"抠出 pem_fingerprint()：{len(fn)} 字节，sha256 {digest[:16]}…")
    return 0


if __name__ == "__main__":
    sys.exit(main())
