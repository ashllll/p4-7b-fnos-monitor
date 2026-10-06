#pragma once
// 配对与信任锚点：把"该信任哪台 NAS、用哪个令牌"从编译期挪到运行期，存 NVS。
//
// 为什么需要它：编译期写死 FNOS_HOST / FNOS_PORT / FNOS_TOKEN 的固件只能给作者
// 自己用；而且 NAS 侧一旦开了 HTTPS（见 docs/fnos-companion-app-plan.md 的 P2 节），
// 板子手里没有证书就连不上——ESP-IDF 的 esp-tls 在 crt_bundle_attach / ca_store /
// cert_pem 三者都为空时不是"先连上再说"，而是直接报错拒连。所以首次必须有一条
// 能取到证书的路：NAS 侧在同一个遥测端口上另收一种**明文**连接，且只放行
// GET /api/v1/identity（见 nas/fpk/nasscreencompanion/app/server/nas_companion_server.py
// 里的 is_plain_bootstrap）。
//
// 信任模型：证书由板子**自己**从服务器取回并自算 SHA-256，指纹显示在屏幕上，
// 由用户与 NAS 管理页显示的指纹逐段比对后确认。确认之前不会发任何带令牌的请求，
// 也不接受服务器在 JSON 里给的指纹字段——否则中间人同时伪造证书和那个字段就能
// 骗过肉眼比对。固定之后板子只认这一枚证书，不做公共 CA 链校验。
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FNOS_PAIR_PEM_MAX 2048      // 单张自签证书的 PEM 约 1.2 KB
#define FNOS_PAIR_FP_MAX  96        // "AA:BB:…" 32 字节的十六进制 = 95 字符

// 当前生效的连接参数（数据层按它拼 URL / 建客户端）
typedef struct {
    char host[32];
    int  port;
    bool tls;                       // true = https（此时 cert_pem 必须非空）
    char token[80];
    char cert_pem[FNOS_PAIR_PEM_MAX];
    char fingerprint[FNOS_PAIR_FP_MAX];
} fnos_pair_cfg_t;

typedef enum {
    FNOS_PAIR_UNPROVISIONED = 0,    // 还没配对：数据层退回编译期默认（明文、无令牌）
    FNOS_PAIR_FETCHING,             // 正在取证书
    FNOS_PAIR_CONFIRM,              // 证书已取到，等用户核对指纹
    FNOS_PAIR_PAIRING,              // 正在用配对码换令牌
    FNOS_PAIR_PROVISIONED,          // 已配对，数据层按保存的参数连接
    FNOS_PAIR_FAILED,               // 上一次尝试失败（msg 里是原因），可以重试
} fnos_pair_state_t;

// UI 侧只读快照（每帧读一次，内部加锁复制）
typedef struct {
    fnos_pair_state_t state;
    char msg[112];                  // 给用户看的一行：进度或失败原因
    char fingerprint[FNOS_PAIR_FP_MAX];
    char subject[80];               // 证书里写的 CN，帮用户认人
    char not_after[16];             // 证书到期日 YYYY-MM-DD
    char host[32];
    int  port;
    bool tls;
} fnos_pair_view_t;

// 幂等。读 NVS；没有保存过的配对就退回编译期默认参数（tls=false、无令牌），
// 也就是"没配对之前行为和以前完全一样"，不会把用户现在能用的链路弄坏。
void fnos_pair_init(void);

bool     fnos_pair_provisioned(void);
// 生效参数每变化一次 +1（配对成功 / 撤销）。数据层用它决定要不要重建 HTTP 客户端。
uint32_t fnos_pair_generation(void);
// 复制当前生效参数。永远成功。
void     fnos_pair_active(fnos_pair_cfg_t *out);
// 读一次状态快照给 UI 用。
void     fnos_pair_view(fnos_pair_view_t *out);

// 开始一次配对：先明文取证书 → 算出指纹 → 进入 FNOS_PAIR_CONFIRM 等用户表决。
// code 是用户在 NAS 管理页生成的 6 位配对码。整条流程在内部任务里跑，不阻塞调用方。
void fnos_pair_begin(const char *code);
// 用户对指纹的表决。accept=false 直接放弃（不会发出配对请求）。
void fnos_pair_confirm(bool accept);
// 忘掉本机保存的配对（清 NVS，退回编译期默认）。NAS 侧的设备记录要在管理页撤销。
void fnos_pair_forget(void);

#ifdef __cplusplus
}
#endif
