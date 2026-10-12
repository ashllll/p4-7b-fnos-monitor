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
// 生效参数每变化一次 +1（配对成功 / 撤销）；跳过 0。
// 0 表示尚未初始化、锁不可用或保存配置读取/校验失败，调用方必须停用当前连接。
// 存储配置失败时允许重新配对/清除，成功后恢复非零代次。
uint32_t fnos_pair_generation(void);
// 复制当前生效参数；不可用时清零 out。需要区分失败的调用方使用 active_gen。
void     fnos_pair_active(fnos_pair_cfg_t *out);
// 同上，但把"这份参数属于哪一代"用**同一次加锁读**一起返回。数据层必须成对地拿
// 参数和代次：分两次读时，若中间刚好完成一次配对，它会拿到新参数却记下旧代次，
// 于是下一轮又把刚建好的 HTTP 客户端拆掉重建（审计 M01）。
// 返回 0 时 out 清零；不可据此回落明文或继续旧客户端。
uint32_t fnos_pair_active_gen(fnos_pair_cfg_t *out);
// 读一次状态快照给 UI 用。
void     fnos_pair_view(fnos_pair_view_t *out);

// 兼容旧诊断符号：仅配对 worker 内可借用，至下一次 HTTP 请求前有效，不得保存或跨任务传递。
// 其他任务及 worker 未发布时返回静态空串；UI/串口应使用 fnos_pair_view 的加锁消息副本。
const char *fnos_pair_last_http_diag(void);

// 开始一次配对：先明文取证书 → 算出指纹 → 进入 FNOS_PAIR_CONFIRM 等用户表决。
// code 是用户在 NAS 管理页生成的 6 位配对码。整条流程在内部任务里跑。
// 非六位 ASCII 数字码或请求槽/worker 忙时忽略新 begin/forget；新 begin 立即失效旧确认。
void fnos_pair_begin(const char *code);
// 用户对当前待确认指纹的表决；仅 CONFIRM 状态且无进行中动作时接受。
// accept=false 也可取消进行中的操作；worker 在网络前/返回后/持久化前检查。
// 已发出的请求无法撤回，NAS 可能已生成设备令牌，需在管理页核对/撤销。
// 取消先于提交锁检查时不保存；已经完成提交的配置保留。其他状态的确认忽略。
void fnos_pair_confirm(bool accept);
// 忘掉本机保存的配对（清 NVS，退回编译期默认）。NAS 侧的设备记录要在管理页撤销。
void fnos_pair_forget(void);

// 串口自检：真跑一次 HTTPS（先明文取 identity，再用那张证书探 /api/v1/health）。
// 仅 GET identity/health，不提交配对码、不写本机 NVS/配对状态。
// identity 来自明文，仅验证所取证书能否握手，不替代用户核对可信指纹。
// 有效输出缓冲必须非空且容量大于 0。
// 并发探针返回 false/busy；配置锁不可用时拒绝访问网络。
// 异步版最多排队一个 PSRAM 栈任务（mbedTLS 握手吃栈）。
bool     fnos_pair_tls_probe(char *out, size_t out_cap);
void     fnos_pair_tls_probe_async(void);

// 只读诊断：配对任务栈的剩余高水位（字节；ESP-IDF 该 API 单位就是字节）。
// 0 = 任务没起来（fnos_pair_init 还没跑或建任务失败）⇒ 长测按"测不到"处理，
// 不能当成"栈只剩 0 字节"。mbedTLS 握手吃栈最深，72h 长测靠它盯这条底线。
uint32_t fnos_pair_stack_min_free(void);

#ifdef __cplusplus
}
#endif
