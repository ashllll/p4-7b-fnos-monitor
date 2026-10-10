#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/audit125/test_pair_nvs.py —— 配对 NVS 保存/擦除 + 配对响应判定的主机单元测试。

为什么是"真实函数提取"而不是"再抄一份实现"：抄一份的话生产改了、测试还在测旧副本，
审计 N1（明文配对残留旧证书）/ N2（wipe 失败谎报已清除）/ N3（非 200 且带 token 也当成功、
超长 token 截断）的回归会静默失效。本脚本按函数名从
components/fnos_monitor/fnos_pair.c 里切出**真实函数体**（字符串/注释感知的括号配平），
拼进临时翻译单元，用内存 NVS 替身 + 命名故障点驱动，再用 CC 编译运行。

真实抽取：has_cert / nvs_save / nvs_wipe / pair_response_accept + do_fetch 的 tls 判定片段。
替身：NVS（内存 KV，commit 为持久化边界）、ESP_LOG、ESP_OK/ESP_ERR_NVS_NOT_FOUND、KEY_* 宏
（宏也从生产文件抽取，避免键名漂移）。tls 类型判定用**真实 cJSON 库**（managed_components）
解析真实 JSON 后驱动抽取到的生产片段。

退出码：0 全过 / 1 有用例失败 / 2 抽取失败 / 3 环境问题（没有编译器、缺文件）。
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = REPO / "components" / "fnos_monitor" / "fnos_pair.c"
HDR_DIR = REPO / "components" / "fnos_monitor"
CJSON_DIR = REPO / "managed_components" / "espressif__cjson" / "cJSON"

EXIT_TEST_FAIL = 1
EXIT_EXTRACT = 2
EXIT_INPUT = 3


class ExtractError(Exception):
    pass


# ────────────────────────────── 抽取

def match_brace(text, i):
    """从 text[i] == '{' 开始做括号配平；跳过注释、字符串、字符常量。"""
    depth = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            q = c
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    i += 1
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            nl = text.find("\n", i)
            if nl < 0:
                break
            i = nl
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            if end < 0:
                raise ExtractError("注释没有收尾（/* 无 */）")
            i = end + 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ExtractError("括号没有配平")


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def extract_function(text, name):
    """切出 `static <type> name(...)  { ... }` 的完整定义（含签名），返回 (代码, 起始行, 结束行)。"""
    pat = re.compile(
        r"^[ \t]*static[ \t]+[\w \t\*]*\b" + re.escape(name) + r"[ \t]*\(", re.M
    )
    m = pat.search(text)
    if not m:
        raise ExtractError("%s：在 %s 里找不到 `static ... %s(` 定义" % (name, SRC, name))
    brace = text.find("{", m.end())
    if brace < 0:
        raise ExtractError("%s：签名后找不到函数体 '{'" % name)
    end = match_brace(text, brace)
    return text[m.start():end + 1], line_of(text, m.start()), line_of(text, end)


def extract_fragment(text, start_marker, end_marker):
    """从 start_marker 所在整行切到 end_marker 所在整行（含），逐字保留。"""
    i = text.find(start_marker)
    if i < 0:
        raise ExtractError("片段起点找不到：%s" % start_marker)
    j = text.find(end_marker, i)
    if j < 0:
        raise ExtractError("片段终点找不到：%s" % end_marker)
    start = text.rfind("\n", 0, i) + 1
    end = text.find("\n", j)
    if end < 0:
        end = len(text)
    return text[start:end], line_of(text, start), line_of(text, end)


def extract_macro(text, name):
    m = re.search(r"^#define[ \t]+" + re.escape(name) + r"[ \t]+(\S+)[ \t]*$", text, re.M)
    if not m:
        raise ExtractError("宏 %s 在 %s 里找不到" % (name, SRC))
    return m.group(1)


# ────────────────────────────── C 翻译单元模板

C_TEMPLATE = textwrap.dedent(r'''
    /* 生成文件：由 tools/audit125/test_pair_nvs.py 拼装。
       "抽取"标记之间的函数体/片段逐字取自 components/fnos_monitor/fnos_pair.c，
       生产改一行这里就测那一行。不要手工编辑。 */
    #include <stdarg.h>
    #include <stdbool.h>
    #include <stddef.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <string.h>

    #include "fnos_pair.h"   /* 真实头文件（-I components/fnos_monitor） */
    #include "cJSON.h"       /* 真实 cJSON 库（managed_components） */

    /* ───── ESP-IDF / NVS 替身：内存 KV + 命名故障点 ───── */
    typedef int esp_err_t;
    #define ESP_OK 0
    #define ESP_FAIL -1
    #define ESP_ERR_NVS_NOT_FOUND 0x1102   /* 真实值 = ESP_ERR_NVS_BASE(0x1100) + 0x02 */

    typedef uint32_t nvs_handle_t;
    enum { NVS_READONLY = 0, NVS_READWRITE = 1 };

    #define TAG "stub"
    #define ESP_LOGE(tag, ...) do { (void)(tag); fprintf(stderr, "  [ESP_LOGE] " __VA_ARGS__); fputc('\n', stderr); } while (0)

    /* 生产宏：也从 fnos_pair.c 抽取，测试里不另写一份键名 */
    #define NVS_NS @NVS_NS@
    #define KEY_HOST @KEY_HOST@
    #define KEY_PORT @KEY_PORT@
    #define KEY_TOKEN @KEY_TOKEN@
    #define KEY_CERT @KEY_CERT@
    #define KEY_FP @KEY_FP@

    #define KV_MAX 8
    #define KV_VAL_MAX (FNOS_PAIR_PEM_MAX + 256)

    typedef struct { char key[16]; char val[KV_VAL_MAX]; bool used; } kv_t;
    typedef struct { kv_t kv[KV_MAX]; int32_t port; bool port_set; } store_t;

    static store_t g_live;          /* 已提交（掉电后还在）*/
    static store_t g_stage;         /* 打开句柄后的暂存 */
    static const char *g_fail_at;   /* 命名故障点，NULL = 不注入 */

    static bool fail_at(const char *point) { return g_fail_at && strcmp(g_fail_at, point) == 0; }

    static kv_t *kv_find(store_t *s, const char *key)
    {
        for (int i = 0; i < KV_MAX; i++) {
            if (s->kv[i].used && strcmp(s->kv[i].key, key) == 0) {
                return &s->kv[i];
            }
        }
        return NULL;
    }

    static const char *set_point(const char *key)
    {
        if (strcmp(key, KEY_HOST) == 0)  return "SET_HOST";
        if (strcmp(key, KEY_PORT) == 0)  return "SET_PORT";
        if (strcmp(key, KEY_TOKEN) == 0) return "SET_TOKEN";
        if (strcmp(key, KEY_CERT) == 0)  return "SET_CERT";
        if (strcmp(key, KEY_FP) == 0)    return "SET_FP";
        return "SET_UNKNOWN";
    }

    static const char *erase_point(const char *key)
    {
        if (strcmp(key, KEY_CERT) == 0) return "ERASE_CERT";
        if (strcmp(key, KEY_FP) == 0)   return "ERASE_FP";
        return "ERASE_UNKNOWN";
    }

    static esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *out)
    {
        (void)ns; (void)mode;
        if (fail_at("OPEN")) {
            return ESP_FAIL;
        }
        g_stage = g_live;
        *out = 1;
        return ESP_OK;
    }

    static void nvs_close(nvs_handle_t h) { (void)h; }

    static esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *val)
    {
        (void)h;
        if (fail_at(set_point(key))) {
            return ESP_FAIL;
        }
        kv_t *e = kv_find(&g_stage, key);
        if (!e) {
            for (int i = 0; i < KV_MAX; i++) {
                if (!g_stage.kv[i].used) { e = &g_stage.kv[i]; break; }
            }
        }
        if (!e) {
            return ESP_FAIL;
        }
        snprintf(e->key, sizeof e->key, "%s", key);
        snprintf(e->val, sizeof e->val, "%s", val);
        e->used = true;
        return ESP_OK;
    }

    static esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t val)
    {
        (void)h;
        if (fail_at(set_point(key))) {
            return ESP_FAIL;
        }
        g_stage.port = val;
        g_stage.port_set = true;
        return ESP_OK;
    }

    static esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len)
    {
        (void)h;
        const kv_t *e = kv_find(&g_stage, key);
        if (!e) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
        size_t need = strlen(e->val) + 1;
        if (!out) { *len = need; return ESP_OK; }
        if (*len < need) { *len = need; return ESP_FAIL; }
        memcpy(out, e->val, need);
        *len = need;
        return ESP_OK;
    }

    static esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *out)
    {
        (void)h; (void)key;
        if (!g_stage.port_set) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
        *out = g_stage.port;
        return ESP_OK;
    }

    static esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
    {
        (void)h;
        if (fail_at(erase_point(key))) {
            return ESP_FAIL;
        }
        kv_t *e = kv_find(&g_stage, key);
        if (!e) {
            return ESP_ERR_NVS_NOT_FOUND;   /* 真实语义：键不存在 */
        }
        memset(e, 0, sizeof *e);
        return ESP_OK;
    }

    static esp_err_t nvs_erase_all(nvs_handle_t h)
    {
        (void)h;
        if (fail_at("ERASE_ALL")) {
            return ESP_FAIL;
        }
        memset(&g_stage, 0, sizeof g_stage);
        return ESP_OK;
    }

    /* 替身模型：commit 是持久化边界，失败即丢弃暂存（真实 NVS 的未提交改动重启后不可见） */
    static esp_err_t nvs_commit(nvs_handle_t h)
    {
        (void)h;
        if (fail_at("COMMIT")) {
            return ESP_FAIL;
        }
        g_live = g_stage;
        return ESP_OK;
    }

    /* ───── 真实抽取 1：has_cert（fnos_pair.c:@L_HAS_CERT@） ───── */
    /* >>> 抽取开始 has_cert >>> */
    @HAS_CERT@
    /* <<< 抽取结束 has_cert <<< */

    /* ───── 真实抽取 2：nvs_save（fnos_pair.c:@L_NVS_SAVE@） ───── */
    /* >>> 抽取开始 nvs_save >>> */
    @NVS_SAVE@
    /* <<< 抽取结束 nvs_save <<< */

    /* ───── 真实抽取 3：nvs_wipe（fnos_pair.c:@L_NVS_WIPE@） ───── */
    /* >>> 抽取开始 nvs_wipe >>> */
    @NVS_WIPE@
    /* <<< 抽取结束 nvs_wipe <<< */

    /* ───── 真实抽取 4：pair_response_accept（fnos_pair.c:@L_PAIR_RESP@） ───── */
    /* >>> 抽取开始 pair_response_accept >>> */
    @PAIR_RESP@
    /* <<< 抽取结束 pair_response_accept <<< */

    /* ───── 真实抽取 5：do_fetch 的 tls 类型判定片段（fnos_pair.c:@L_TLS@） ─────
       生产片段原样贴进 tls_gate()：set_view 替身记录"走了失败分支"，片段正常走完则把
       真实变量 tls 记下来。JSON 由真实 cJSON 库解析。 */
    static char g_view_msg[160];
    static int  g_tls_rejected;   /* 1 = 生产片段调了 set_view（类型不对） */
    static int  g_tls_value;      /* 生产片段算出的真实 tls 布尔；-1 = 没走到 */

    static void set_view(fnos_pair_state_t state, const char *msg)
    {
        (void)state;
        snprintf(g_view_msg, sizeof g_view_msg, "%s", msg);
        g_tls_rejected = 1;
    }

    static void tls_gate(cJSON *root)
    {
        g_view_msg[0] = 0;
        g_tls_rejected = 0;
        g_tls_value = -1;
    /* >>> 抽取开始 tls 片段 >>> */
    @TLS_FRAGMENT@
    /* <<< 抽取结束 tls 片段 <<< */
        (void)jpem;   /* 片段后面的代码才用到它，这里只为避免 -Wunused */
        g_tls_value = tls ? 1 : 0;
    }

    /* ───── 断言 ───── */
    static int g_pass, g_fail;

    static void check(const char *name, bool cond, const char *fmt, ...)
    {
        if (cond) {
            printf("PASS %s\n", name);
            g_pass++;
            return;
        }
        printf("FAIL %s ", name);
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        putchar('\n');
        g_fail++;
    }

    /* ───── 存储观察（走同一套 NVS 替身 API，不绕过） ───── */
    static char g_read[KV_VAL_MAX];
    static char g_rb[4][KV_VAL_MAX];
    static int  g_rbi;

    static bool store_read(const char *key, char *out, size_t cap)
    {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
            return false;
        }
        size_t n = cap;
        esp_err_t e = nvs_get_str(h, key, out, &n);
        nvs_close(h);
        return e == ESP_OK;
    }

    static bool store_has(const char *key) { return store_read(key, g_read, sizeof g_read); }

    static const char *store_str(const char *key)
    {
        g_rbi = (g_rbi + 1) % 4;
        return store_read(key, g_rb[g_rbi], sizeof g_rb[0]) ? g_rb[g_rbi] : "(无)";
    }

    static int store_port(void)
    {
        nvs_handle_t h;
        int32_t p = -1;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
            return -1;
        }
        if (nvs_get_i32(h, KEY_PORT, &p) != ESP_OK) {
            p = -1;
        }
        nvs_close(h);
        return (int)p;
    }

    static void kv_put(store_t *s, const char *key, const char *val)
    {
        for (int i = 0; i < KV_MAX; i++) {
            if (!s->kv[i].used) {
                snprintf(s->kv[i].key, sizeof s->kv[i].key, "%s", key);
                snprintf(s->kv[i].val, sizeof s->kv[i].val, "%s", val);
                s->kv[i].used = true;
                return;
            }
        }
    }

    static void store_reset(void)
    {
        memset(&g_live, 0, sizeof g_live);
        memset(&g_stage, 0, sizeof g_stage);
        g_fail_at = NULL;
    }

    static void seed_plain(void)
    {
        store_reset();
        kv_put(&g_live, KEY_HOST, "oldnas.local");
        g_live.port = 8798;
        g_live.port_set = true;
        kv_put(&g_live, KEY_TOKEN, "oldTokA");
    }

    static void seed_tls_a(void)
    {
        seed_plain();
        kv_put(&g_live, KEY_CERT, PEM_A);
        kv_put(&g_live, KEY_FP, FP_A);
    }

    /* ───── 真正的"生效参数"构造（走真实结构体） ───── */
    static fnos_pair_cfg_t cfg_plain(const char *host, int port, const char *tok)
    {
        fnos_pair_cfg_t c;
        memset(&c, 0, sizeof c);
        snprintf(c.host, sizeof c.host, "%s", host);
        c.port = port;
        c.tls = false;
        snprintf(c.token, sizeof c.token, "%s", tok);
        return c;
    }

    static fnos_pair_cfg_t cfg_tls(const char *host, int port, const char *tok, const char *pem,
                                   const char *fp)
    {
        fnos_pair_cfg_t c = cfg_plain(host, port, tok);
        c.tls = true;
        snprintf(c.cert_pem, sizeof c.cert_pem, "%s", pem);
        snprintf(c.fingerprint, sizeof c.fingerprint, "%s", fp);
        return c;
    }

    /* ───── 注入点：明文保存（基线 = TLS A）失败后旧配对必须原样还在 ───── */
    static void inject_plain_against_tls_a(const char *name, const char *point)
    {
        seed_tls_a();
        fnos_pair_cfg_t c = cfg_plain("newnas.local", 9000, "newTok");
        g_fail_at = point;
        bool r = nvs_save(&c);
        g_fail_at = NULL;
        bool kept = strcmp(store_str(KEY_HOST), "oldnas.local") == 0 && store_port() == 8798 &&
                    strcmp(store_str(KEY_TOKEN), "oldTokA") == 0 && store_has(KEY_CERT) &&
                    store_has(KEY_FP);
        check(name, !r && kept,
              "nvs_save=%d(期望 0) 旧配对仍在=%d host=%s port=%d token=%s cert=%d fp=%d",
              (int)r, (int)kept, store_str(KEY_HOST), store_port(), store_str(KEY_TOKEN),
              (int)store_has(KEY_CERT), (int)store_has(KEY_FP));
    }

    /* ───── 注入点：TLS 保存（基线 = 明文）失败后不得留下 cert/fp，也不得改旧参数 ───── */
    static void inject_tls_against_plain(const char *name, const char *point)
    {
        seed_plain();
        fnos_pair_cfg_t c = cfg_tls("newnas.local", 9000, "newTok", PEM_B, FP_B);
        g_fail_at = point;
        bool r = nvs_save(&c);
        g_fail_at = NULL;
        bool kept = strcmp(store_str(KEY_HOST), "oldnas.local") == 0 && store_port() == 8798 &&
                    strcmp(store_str(KEY_TOKEN), "oldTokA") == 0 && !store_has(KEY_CERT) &&
                    !store_has(KEY_FP);
        check(name, !r && kept,
              "nvs_save=%d(期望 0) 旧明文仍在且没有 cert/fp=%d host=%s token=%s cert=%d fp=%d",
              (int)r, (int)kept, store_str(KEY_HOST), store_str(KEY_TOKEN),
              (int)store_has(KEY_CERT), (int)store_has(KEY_FP));
    }

    static void tls_case(const char *name, const char *json, int want_rejected, int want_tls,
                         const char *msg_sub)
    {
        cJSON *root = cJSON_Parse(json);
        if (!root) {
            check(name, false, "真实 cJSON 解析失败：%s", json);
            return;
        }
        tls_gate(root);
        bool okc = (g_tls_rejected == want_rejected) && (g_tls_value == want_tls) &&
                   (!msg_sub || strstr(g_view_msg, msg_sub) != NULL);
        check(name, okc, "rejected=%d(期望 %d) tls=%d(期望 %d) msg=\"%s\"", g_tls_rejected,
              want_rejected, g_tls_value, want_tls, g_view_msg);
        if (!want_rejected) {
            cJSON_Delete(root);   /* 拒绝路径由生产片段自己 Delete */
        }
    }

    int main(void)
    {
        /* ── 1. 明文正常路径 ── */
        {
            store_reset();
            fnos_pair_cfg_t c = cfg_plain("newnas.local", 8798, "tok-plain-1");
            bool r = nvs_save(&c);
            check("plain_first_save_ok", r, "nvs_save=%d(期望 1)", (int)r);
            check("plain_first_save_contents",
                  strcmp(store_str(KEY_HOST), "newnas.local") == 0 && store_port() == 8798 &&
                      strcmp(store_str(KEY_TOKEN), "tok-plain-1") == 0 && !store_has(KEY_CERT) &&
                      !store_has(KEY_FP),
                  "host=%s port=%d token=%s cert=%d fp=%d", store_str(KEY_HOST), store_port(),
                  store_str(KEY_TOKEN), (int)store_has(KEY_CERT), (int)store_has(KEY_FP));

            bool r2 = nvs_save(&c);
            check("plain_repeat_save_ok_not_found_is_idempotent", r2,
                  "第二次 nvs_save=%d（cert/fp 不存在时 erase 返回 NOT_FOUND 必须当成功）", (int)r2);
        }

        /* ── 2. 往返 1（审计 N1）：TLS A → 明文，必须擦掉 cert/fp ── */
        {
            store_reset();
            fnos_pair_cfg_t a = cfg_tls("nas.local", 8798, "tokA", PEM_A, FP_A);
            bool r1 = nvs_save(&a);
            bool seeded = store_has(KEY_CERT) && store_has(KEY_FP);
            fnos_pair_cfg_t p = cfg_plain("nas.local", 8798, "tokB");
            bool r2 = nvs_save(&p);
            check("roundtrip1_tls_then_plain_clears_cert_fp",
                  r1 && seeded && r2 && !store_has(KEY_CERT) && !store_has(KEY_FP) &&
                      strcmp(store_str(KEY_TOKEN), "tokB") == 0,
                  "saveTLS=%d 存入cert/fp=%d savePlain=%d cert=%d fp=%d token=%s", (int)r1,
                  (int)seeded, (int)r2, (int)store_has(KEY_CERT), (int)store_has(KEY_FP),
                  store_str(KEY_TOKEN));
        }

        /* ── 3. 往返 2：明文 → TLS B，cert/fp 必须是 B，旧 A 不在 ── */
        {
            store_reset();
            fnos_pair_cfg_t p = cfg_plain("nas.local", 8798, "tokP");
            bool r1 = nvs_save(&p);
            fnos_pair_cfg_t b = cfg_tls("nas.local", 8798, "tokB", PEM_B, FP_B);
            bool r2 = nvs_save(&b);
            check("roundtrip2_plain_then_tls_stores_B",
                  r1 && r2 && strcmp(store_str(KEY_CERT), PEM_B) == 0 &&
                      strcmp(store_str(KEY_FP), FP_B) == 0 && strcmp(store_str(KEY_TOKEN), "tokB") == 0,
                  "savePlain=%d saveTLS=%d cert是B=%d fp=%s token=%s", (int)r1, (int)r2,
                  (int)(strcmp(store_str(KEY_CERT), PEM_B) == 0), store_str(KEY_FP),
                  store_str(KEY_TOKEN));
        }

        /* ── 4. 命名故障点注入：nvs_save 必须失败且不得让新参数生效 ── */
        inject_plain_against_tls_a("inject_open_fail", "OPEN");
        inject_plain_against_tls_a("inject_set_host_fail", "SET_HOST");
        inject_plain_against_tls_a("inject_set_port_fail", "SET_PORT");
        inject_plain_against_tls_a("inject_set_token_fail", "SET_TOKEN");
        inject_plain_against_tls_a("inject_erase_cert_fail", "ERASE_CERT");
        inject_plain_against_tls_a("inject_erase_fp_fail", "ERASE_FP");
        inject_plain_against_tls_a("inject_commit_fail", "COMMIT");
        inject_tls_against_plain("inject_set_cert_fail", "SET_CERT");
        inject_tls_against_plain("inject_set_fp_fail", "SET_FP");

        /* ── 5. nvs_wipe（审计 N2）：任一步失败都必须 false 且旧配对还在 ── */
        {
            const char *points[3] = { "OPEN", "ERASE_ALL", "COMMIT" };
            const char *names[3] = { "wipe_open_fail_keeps_pairing",
                                     "wipe_erase_all_fail_keeps_pairing",
                                     "wipe_commit_fail_keeps_pairing" };
            for (int i = 0; i < 3; i++) {
                seed_tls_a();
                g_fail_at = points[i];
                bool r = nvs_wipe();
                g_fail_at = NULL;
                bool kept = store_has(KEY_HOST) && store_has(KEY_CERT) && store_port() == 8798;
                check(names[i], !r && kept, "nvs_wipe=%d(期望 0) 旧配对仍在=%d host=%s port=%d",
                      (int)r, (int)kept, store_str(KEY_HOST), store_port());
            }
            seed_tls_a();
            bool r = nvs_wipe();
            check("wipe_ok_clears_store",
                  r && !store_has(KEY_HOST) && !store_has(KEY_CERT) && !store_has(KEY_FP) &&
                      store_port() == -1,
                  "nvs_wipe=%d(期望 1) host=%d cert=%d fp=%d port=%d", (int)r,
                  (int)store_has(KEY_HOST), (int)store_has(KEY_CERT), (int)store_has(KEY_FP),
                  store_port());
        }

        /* ── 6. pair_response_accept（真实判定函数；cap 与 do_pair 传的 sizeof(s_next.token) 同源） ── */
        {
            char tok79[80], tok80[81];
            memset(tok79, 'x', 79);
            tok79[79] = 0;
            memset(tok80, 'x', 80);
            tok80[80] = 0;
            const size_t cap = sizeof(((fnos_pair_cfg_t *)0)->token);   /* fnos_pair.h: token[80] */
            check("token_field_is_80_bytes", cap == 80, "sizeof(token)=%d(期望 80)", (int)cap);
            check("resp_transport_fail_rejected", !pair_response_accept(false, 0, "tok", cap),
                  "transport_ok=false status=0 token 非空也必须拒绝");
            check("resp_ok_true_status_403_rejected", !pair_response_accept(true, 403, "tok", cap),
                  "ok=true status=403 且响应体带 token（审计 N3）也必须拒绝");
            check("resp_status_0_rejected", !pair_response_accept(true, 0, "tok", cap),
                  "ok=true status=0 必须拒绝");
            check("resp_token_missing_rejected", !pair_response_accept(true, 200, NULL, cap),
                  "status=200 但 token 缺失必须拒绝");
            check("resp_token_empty_rejected", !pair_response_accept(true, 200, "", cap),
                  "status=200 但 token 为空必须拒绝");
            check("resp_token_79_accepted", pair_response_accept(true, 200, tok79, cap),
                  "len=79 cap=%d 必须接受", (int)cap);
            check("resp_token_80_rejected_not_truncated", !pair_response_accept(true, 200, tok80, cap),
                  "len=80 cap=%d 必须拒绝，不得截断后接受", (int)cap);
            check("resp_cap_boundary", pair_response_accept(true, 200, "tok", 4) &&
                                           !pair_response_accept(true, 200, "tok", 3),
                  "len<cap 才接受（cap 边界）");
        }

        /* ── 7. do_fetch 的 tls 类型判定：真实 cJSON 解析 + 真实生产片段（审计 N3） ── */
        tls_case("tls_json_bool_true_accepted", "{\"tls\":true,\"cert_pem\":\"PEM\"}", 0, 1, NULL);
        tls_case("tls_json_bool_false_plain_accepted", "{\"tls\":false}", 0, 0, NULL);
        tls_case("tls_json_string_rejected", "{\"tls\":\"false\"}", 1, -1, "tls");
        tls_case("tls_json_number_rejected", "{\"tls\":0}", 1, -1, "tls");
        tls_case("tls_json_missing_rejected", "{\"cert_pem\":\"PEM\"}", 1, -1, "tls");
        tls_case("tls_json_null_rejected", "{\"tls\":null}", 1, -1, "tls");

        /* ── 8. has_cert（真实抽取，nvs_save 的 TLS 分支就靠它） ── */
        check("has_cert_pem_true", has_cert(PEM_A), "含 BEGIN CERTIFICATE 的 PEM");
        check("has_cert_empty_false", !has_cert(""), "空串");
        check("has_cert_null_false", !has_cert(NULL), "NULL");
        check("has_cert_garbage_false", !has_cert("not a certificate"), "非证书文本");

        printf("PASS %d 项：真实 NVS 函数（nvs_save/nvs_wipe/has_cert）+ 响应判定"
               "（pair_response_accept/do_fetch tls 片段）\n", g_pass);
        if (g_fail) {
            printf("FAIL %d 项\n", g_fail);
            return 1;
        }
        return 0;
    }
''')

PEM_A = "-----BEGIN CERTIFICATE-----\\nTUlJQmZha2VBQ0F3RVFF\\n-----END CERTIFICATE-----\\n"
PEM_B = "-----BEGIN CERTIFICATE-----\\nTUlJQmZha2VCQ0F3RVFF\\n-----END CERTIFICATE-----\\n"
FP_A = "AA:AA:AA:AA"
FP_B = "BB:BB:BB:BB"

PEM_DEFS = (
    'static const char *const PEM_A = "%s";\n'
    'static const char *const PEM_B = "%s";\n'
    'static const char *const FP_A  = "%s";\n'
    'static const char *const FP_B  = "%s";\n'
) % (PEM_A, PEM_B, FP_A, FP_B)

MACRO_NAMES = ["NVS_NS", "KEY_HOST", "KEY_PORT", "KEY_TOKEN", "KEY_CERT", "KEY_FP"]

TLS_FRAG_START = 'const cJSON *jtls = cJSON_GetObjectItemCaseSensitive(root, "tls");'
TLS_FRAG_END = "bool tls = cJSON_IsTrue(jtls);"


def build_unit(src_text):
    funcs = {}
    provenance = []
    for name in ("has_cert", "nvs_save", "nvs_wipe", "pair_response_accept"):
        code, first, last = extract_function(src_text, name)
        funcs[name] = code
        provenance.append((name, first, last, len(code.splitlines())))

    frag, ffirst, flast = extract_fragment(src_text, TLS_FRAG_START, TLS_FRAG_END)
    provenance.append(("tls 判定片段(do_fetch)", ffirst, flast, len(frag.splitlines())))

    tie = "fnos_pair.c:%d-%d" % (ffirst, flast)
    out = C_TEMPLATE
    replacements = {
        "@HAS_CERT@": funcs["has_cert"],
        "@NVS_SAVE@": funcs["nvs_save"],
        "@NVS_WIPE@": funcs["nvs_wipe"],
        "@PAIR_RESP@": funcs["pair_response_accept"],
        "@TLS_FRAGMENT@": frag,
        "@L_HAS_CERT@": "fnos_pair.c:%d-%d" % (provenance[0][1], provenance[0][2]),
        "@L_NVS_SAVE@": "fnos_pair.c:%d-%d" % (provenance[1][1], provenance[1][2]),
        "@L_NVS_WIPE@": "fnos_pair.c:%d-%d" % (provenance[2][1], provenance[2][2]),
        "@L_PAIR_RESP@": "fnos_pair.c:%d-%d" % (provenance[3][1], provenance[3][2]),
        "@L_TLS@": tie,
    }
    for name in MACRO_NAMES:
        replacements["@%s@" % name] = extract_macro(src_text, name)
    for key, val in replacements.items():
        out = out.replace(key, val)

    leftover = re.findall(r"@[A-Z_]+@", out)
    if leftover:
        raise ExtractError("模板占位符没替换完：%s" % ", ".join(sorted(set(leftover))))

    # 测试数据定义放在抽取函数之前（nvs_save 的 TLS 分支只认 has_cert 的真实语义）
    out = out.replace("/* ───── 真实抽取 1", PEM_DEFS + "\n/* ───── 真实抽取 1", 1)

    for name in ("has_cert", "nvs_save", "nvs_wipe", "pair_response_accept"):
        if funcs[name] not in out:
            raise ExtractError("%s 没能拼进翻译单元" % name)
    if frag not in out:
        raise ExtractError("tls 判定片段没能拼进翻译单元")
    return out, provenance


def main():
    if not SRC.is_file():
        print("找不到生产文件：%s" % SRC, file=sys.stderr)
        return EXIT_INPUT
    if not (CJSON_DIR / "cJSON.c").is_file():
        print("找不到真实 cJSON 库：%s" % CJSON_DIR, file=sys.stderr)
        return EXIT_INPUT

    cc = os.environ.get("CC", "cc")
    if shutil.which(cc) is None:
        print("找不到 C 编译器（CC=%s）：装 Xcode 命令行工具或设 CC=<clang 路径>" % cc, file=sys.stderr)
        return EXIT_INPUT

    src_text = SRC.read_text(encoding="utf-8")
    try:
        unit, provenance = build_unit(src_text)
    except ExtractError as exc:
        print("抽取失败：%s" % exc, file=sys.stderr)
        return EXIT_EXTRACT

    for name, first, last, lines in provenance:
        print("[extract] %-24s components/fnos_monitor/fnos_pair.c:%d-%d（%d 行，逐字）"
              % (name, first, last, lines))

    tmp = pathlib.Path(tempfile.mkdtemp(prefix="fnos_pair_nvs_"))
    gen = tmp / "test_pair_nvs_gen.c"
    gen.write_text(unit, encoding="utf-8")

    flags = ["-std=c11", "-Wall", "-Werror", "-I", str(HDR_DIR), "-I", str(CJSON_DIR)]
    cmd = [cc] + flags + ["-o", str(tmp / "t"), str(gen), str(CJSON_DIR / "cJSON.c")]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print("编译失败（%s）：\n%s%s" % (" ".join(cmd), proc.stdout, proc.stderr), file=sys.stderr)
        print("生成的翻译单元保留在：%s" % gen, file=sys.stderr)
        return EXIT_EXTRACT

    run = subprocess.run([str(tmp / "t")], capture_output=True, text=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)

    if run.returncode not in (0, 1):
        print("测试程序异常退出（%d），翻译单元保留在：%s" % (run.returncode, gen), file=sys.stderr)
        return EXIT_TEST_FAIL
    shutil.rmtree(tmp, ignore_errors=True)
    return run.returncode


if __name__ == "__main__":
    sys.exit(main())
