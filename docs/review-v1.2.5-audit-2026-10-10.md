# 独立复核：v1.2.5 候选专项审计与整改

复核日期：2026-10-10 · 复核对象：`ashllll/p4-7b-fnos-monitor` 公开 main `d84039f6f8538d47c4eacda80f26e1e8ebe8772c` 上的《v1.2.5 候选专项审计与整改》报告
复核范围（按用户指定）：**只做独立复核，不改代码**；聚焦该报告的 **P1 源码修复 + 主机回归** 部分
交付物：本文件。本次未修改任何源码、未提交、未推送、未烧录。

---

## 0. 结论摘要

**成立**（可在本机独立重放，方法与证据见 §2）：

| 编号 | 报告主张 | 复核判定 |
|---|---|---|
| R1 | `_alerts()` 对"降级 + 有进度"只报 info，掩盖降级 | **成立**，实测告警数组恰为 `[{"lv":"info",…}]`，无 crit |
| R2 | mdstat 读取失败被当空数组，伴侣层记为 ok | **成立**，且比报告更严重：会把上一帧还在的阵列**清空**，状态仍 `ok` |
| R3 | `"active" in state` 命中 `inactive` | **代码路径成立**；但触发所需的真实 mdstat 样本未获证实（§3.2） |
| R4 | 段为 None 时 `_alerts()` 抛异常，整组告警退回上一帧 | **成立**，实测同时刻的 RAID 降级 crit 被丢掉 |
| R5 | 健康 RAID0/linear/容器被统一报降级 | **代码后果成立**（前提是这些阵列不打印汇总行，本机未能独立核实） |
| N1 | HTTPS→HTTP 重配对后 NVS 里旧 cert/fp 不删，重启回到 HTTPS | **成立** |
| N2 | `nvs_wipe()` 忽略 erase/commit 失败且仍显示"已清除" | **成立** |
| N3 | tls 类型不校验 / 无 200 门槛 / 超长 token 静默截断 | **三项均成立**，其中 token 截断（`fnos_pair.h:32` `char token[80]`）可达性最高 |
| S1 | 旧 `soak_check.py`、`FNOS_HEAP_DEBUG` 不构成板端证据 | **成立**（旧检查缺日志时 `return 0`；堆调试只打堆） |
| §1 版本事实 | 公开 main 固定于 d84039f、manifest 仍 1.2.4、无 v1.2.5 标签 | **成立**（`git ls-remote` 复核） |
| §7 | 原 `collector_check` / `contract_check` 通过 | **成立**，两项在本机独立跑通 |

**无法复核**（本机与公开树中都找不到产物）：

- `v1.2.5-candidate.patch` 与 `tools/audit125/{soak72,test_raid,test_pair_nvs,test_diag,test_soak72}.py` **在目标仓库、本机 `~/code/esp`、`~/Downloads`、`~/Desktop`、`/tmp` 中均不存在**（公开树亦无 `tools/audit125/`）。
- 因此 §3 的补丁文件清单、§7 的"RAID 20 项 / NVS 主机 C 测试 17 个注入点 / soak 自测 14 项 / 诊断回调 C++ 测试通过"、§4 验收判据的**实现**、§6 中除两项既有检查外的全部命令，**只能视为报告自述，不是本机可复现的绿灯**。S1 的 72h 脚本与实机结果同理。
- 报告 §7 已如实标注"ESP-IDF 构建 / 静态栈检查 / 主机预览 / 字体重建 / 实机 72h 未完成"，这部分与本次观察一致，不构成隐瞒。

**需要修正或补充**：R3 的触发可达性缺证据；R5 与 §4 验收表自相矛盾；N3 三个子问题可达性差异大；R1 的告警字节预算结论应写出来（结论是安全的）；HWM 单位结论应绑定"用的是 IDF FreeRTOS"这一前提（本工程成立）。

---

## 1. 复核环境与方法

- 审计版本整棵树以 `git archive d84039f6f8538d47c4eacda80f26e1e8ebe8772c | tar -x -C /tmp/p4-audit-d84039f` 取出（38 MB）。**未建 worktree、未动当前分支工作树与 `.git`**；当前分支 `perf/eval-20261010` @ d04588f 及其未提交改动（`components/fnos_monitor/fnos_perf.c`、`docs/perf-plan-official-2026-10-10.md`、`docs/perf-report-2026-10-10.md`、未跟踪 `tools/perf_trace.py`）未被触碰。
- 远端事实：`git ls-remote origin` → `d84039f6f8538d47c4eacda80f26e1e8ebe8772c refs/heads/main`（与报告固定 SHA 一致，**未变**）、`e29a311… refs/tags/v1.1.0`、`469140a… refs/tags/v1.1.1`，无 v1.2.5 标签。Release 列表未独立核对（GitHub API 在本机不可达）。
- 报告引用行号抽查：`fnos_collector.py:49 read_text`、`:305 _raid`、`:673 _alerts`；`fnos_pair.c:473 nvs_save`、`:493 nvs_wipe`；`manifest:2 version = 1.2.4` —— 与审计树一致。
- 主机复现脚本：`/tmp/repro_r.py`（本次新建，只读导入审计树模块，monkeypatch 模块级 `read_text` 注入**合成** mdstat fixture）。它是本次唯一新增的代码，不进入目标仓库。
- 两项既有检查在审计树内原样跑通：`python3 nas/fpk/collector_check.py` → `PASS: full inventories, NIC isolation, hotplug, reset and complete Docker identities`（exit 0）；`python3 nas/fpk/contract_check.py` → …`判定：板子读的每个字段，NAS 侧都在；容量上限也不丢数据 ✓`（exit 0）。

---

## 2. 逐条核验

### R1 —— 降级阵列带进度时只有 info（P1）

源码：`nas/fpk/nasscreencompanion/app/server/fnos_collector.py:676-680`

```python
if not r["ok"]:
    if r["sync_pct"] >= 0:
        out.append({"lv": "info", "m": "%s %s %.1f%%" % (r["dev"], r["what"] or "resync", r["sync_pct"])})
    else:
        out.append({"lv": "crit", "m": "RAID %s degraded (%s)" % (r["dev"], r["state"])})
```

注入 fixture（`[2/1] [U_]` + `recovery = 20.5%`）实测：

```
raid   = [{"dev":"md0","state":"active","lvl":"raid1","have":1,"want":2,"ok":false,"sync_pct":20.5,"what":"recovery"}]
alerts = [{"lv":"info","m":"md0 recovery 20.5%"}]        ← 无 crit
```

对照组（同样降级、无进度）→ `alerts = [{"lv":"crit","m":"RAID md0 degraded (active)"}]`。
**判定：成立。** 报告的"只报 info、严重性被掩盖"与判据描述准确；拟修方向（健康与维护各自成条、再按严重度限量）对症。

### R2 —— 读取失败被当作"正常的空清单"（P1）

- `fnos_collector.py:49-54` `read_text()`：`except OSError: return default`（默认 `""`）。
- `fnos_collector.py:305-306` `_raid()` 首两行即 `txt = read_text("/proc/mdstat")`。
- 伴侣层 `CompanionCollector.EMPTY_MEANS_MISSING = ("zfs", "temps", "disks")`（`fnos_collector.py:867`），**不含 raid**；`sample()` 因此走 `meta[key] = {"status": ST_OK, …}` 分支。

实测（mdstat 不可读 → `""`）：

```
sample()['raid']            = []
sample()['modules']['raid'] = {'status': 'ok', 'ts': …, 'error': None}
```

两帧连续采样（第一帧有健康阵列、第二帧读不到）：

```
s1 raid = [{"dev":"md0",…,"ok":true,…}]
s2 raid = []                                    ← 旧值被清空，不是 stale
s2 meta = {'status': 'ok', …}                    ← 仍然报 ok
```

**判定：成立，且报告低估了一处后果。** 现有行为不只是"空数组被记为 ok"，而是**已采到的阵列数据被抹掉**且状态仍为 `ok`。板端后果具体化：`components/fnos_ui.c:3479-3482` 的存储页小结在 `overview_readable/live` 都为真而 `st->nraid == 0` 时显示"未发现阵列"，即把"读不到 mdstat"显示成"没有阵列"。报告拟修（严格读取、错误向伴侣层传播、已有数据时 stale 且不刷新时间戳）正确。

### R3 —— `"active" in state` 误命中 `inactive`（P1）

源码：`fnos_collector.py:335-336`

```python
cur["ok"] = (cur["have"] == cur["want"]) and ("_" not in flags) and \
            ("active" in cur["state"])
```

注入 fixture（`md0 : inactive raid1 sda1[0] sdb1[1]` + `[2/2] [UU]`）：

```
raid   = [{"dev":"md0","state":"inactive","lvl":"raid1","have":2,"want":2,"ok":true,…}]
alerts = []                                      ← 与报告"inactive 被标记 ok=True"一致
```

对照组（真实形态的 inactive，无汇总行 `md0 : inactive sdb1[1](S) sda1[0](S)`）：

```
raid   = [{"dev":"md0","state":"inactive","lvl":"","have":0,"want":0,"ok":false,…}]
alerts = [{"lv":"crit","m":"RAID md0 degraded (inactive)"}]
```

**判定：代码缺陷成立，触发可达性未证实。** `"active" in "inactive"` 是确定的子串误判，但把它变成"假健康"还需要 `/proc/mdstat` 在 `inactive` 头行之下打印 `[n/m] [U…]` 汇总行；报告没给出这样的真实样本，本机也没有 md 环境与可访问的内核文档（`docs.kernel.org`、`man7.org`、`raw.githubusercontent.com` 在本机均不可达），无法独立确认该组合会出现。按源码逻辑，内核只在阵列有 personality 时打印汇总行，而 `inactive` 恰是"没有 personality"的那一类——若如此，R3 的实际后果就不是"假健康"，而是下面的 R5（被报成 degraded）。**建议报告补一条真实 mdstat 证据或改述触发条件。**

### R4 —— 段为 None 时 `_alerts()` 抛异常，整组告警丢帧（P1）

源码：`fnos_collector.py:686-689`（`cpu = snap.get("cpu", {})` 之后直接 `cpu.get(...)`）、兜底在 `:936-937`（`except Exception: snap["alerts"] = prev.get("alerts", [])`）。

实测：`_alerts({"raid": [], "vols": [], "cpu": None, "mem": {}})` → `AttributeError: 'NoneType' object has no attribute 'get'`。
影响实测：`_cpu` 返回 None 且 mdstat 为降级阵列时，`sample()["alerts"] == []` —— **同一帧的 RAID 降级 crit 消失**。

**判定：成立。** 报告的机制描述（`.get()` 抛异常 → 整组退回上一帧 → 丢掉新 RAID 告警）与实测一致。

### R5 —— 健康 RAID0/linear 与元数据容器被报降级（报告定为 P2）

源码：`fnos_collector.py:324`（`"ok": False` 默认）+ `:330-337`（只有匹配到 `[n/m] [U…]` 才更新 `ok`）。

实测：健康 RAID0（`md0 : active raid0 sdb1[1] sda1[0]` + `blocks … 512k chunks`，无汇总行）

```
raid   = [{"dev":"md0","state":"active","lvl":"raid0","have":0,"want":0,"ok":false,…}]
alerts = [{"lv":"crit","m":"RAID md0 degraded (active)"}]
```

外部 imsm 容器（`md127 : inactive sdb1[1](S)` + `super external:imsm`）同样得到 `crit … degraded (inactive)`。

**判定：代码后果成立，前提（这些阵列不打印 `[n/m] [U…]`）本机未能独立核实**——与 R3 同一个未证实点，方向相反。若前提成立，则**每一台健康 RAID0/linear 机器会常驻"DANGER/降级"误报**（板端 `fnos_ui.c:3679-3680` 会显示"降级"并把总览健康度推到 `UK_DANGER`），其影响面不比 R1 小；而报告把它定为 P2（"另补能力"），同时 §4 验收表又写着"RAID0/linear、元数据容器：不无差别产生降级 crit"。**这是报告内部的一处不一致**，建议二选一：升级为发布前项，或明确写成"本轮可不修，但发布说明里承认该误报"。

### N1 —— HTTPS→HTTP 重配对后旧 cert 残留在 NVS（P1）

- `components/fnos_monitor/fnos_pair.c:473-491` `nvs_save()`：只在 `has_cert(c->cert_pem)` 时写 `KEY_CERT`/`KEY_FP`（482-485），**任何路径都不删这两个键**。
- `fnos_pair.c:426-471` `nvs_load()`：有 `KEY_CERT` 就 `s_cfg.tls = true`（456-462），随后以该证书建立信任锚。（报告写"nvs_load 原 453–463 行"，实际指的是这段证书分支；函数体是 426-471。）
- `fnos_pair.c:663-671` `do_pair()`：明文路径下 `memset(next,…)` 后不填 cert，`s_cfg = *next` 清掉 RAM 证书 → NVS 仍留旧证书。

**判定：成立。** 重启后会按旧证书再走 HTTPS，与 NAS 已切明文的状态不符。

### N2 —— `nvs_wipe()` 忽略失败仍宣称已清除（P1）

`fnos_pair.c:493-502`：

```c
nvs_erase_all(h);
nvs_commit(h);
```

两个返回值都没取，函数返回 `void`；`do_forget()`（`:693-712`）无条件 `memset(&s_cfg,…)`、`s_provisioned = false` 并 `set_view(…, "已清除本机配对（NAS 上的设备记录还没撤销）")`。

**判定：成立。** 失败时会显示"已清除"而 Flash 里旧配对仍在。

### N3 —— 配对响应校验（P1）

1. **tls 类型不校验**：`fnos_pair.c:561-566`
   ```c
   bool tls = cJSON_IsTrue(jtls);
   if (!tls) { …明文配对… }
   ```
   字段缺失、`"tls":"true"`、`1`、`null` 全部落进明文分支。**成立**（`do_fetch` 本身要求 HTTP 200，见 `:508` 起的响应校验；这一点报告未夸小）。
2. **无 200 门槛 + 允许"没收完"的响应**：`do_pair()` `:631-642`
   ```c
   bool ok = http_oneshot(true, url, body, …, rx, 2048, &status, &len);
   cJSON *root = (ok || len > 0) ? cJSON_Parse(rx) : NULL;
   …
   if (!token || !token[0]) { …失败分支… }
   ```
   只要解析出非空 `"token"` 字符串就继续；`status` 从不参与判定。并且 `http_oneshot` 在"响应没收完"时 `ok=false` 但仍写 `*out_len = total`（`:410-418`），而 `cJSON_Parse` 允许尾随数据。**成立**：非 200、或未收完的响应，只要含可解析 token 就会被保存。
3. **超长 token 静默截断**：`fnos_pair.c:668` `snprintf(next->token, sizeof(next->token), "%s", token)`，`fnos_pair.h:32` `char token[80]` → ≥79 字符被截断，随后照常 `nvs_save` + `set_view(…, "配对完成")`。**成立**，且这是三项里**可达性最高**的一条：真实 token 超过 79 字符并不罕见，截断后板子会长期用错 token，而屏幕显示"配对完成"。

**判定：三项均成立。** 报告的 79 字符边界与修法（tls 必须 JSON 布尔；只接受传输成功 + HTTP 200 + 非空且长度小于缓冲区的 token）正确。

### S1 —— 旧长测/堆调试不构成板端证据（P1，证据缺口）

- `tools/soak_check.py` 存在，作用于**本机暂存服务**：比对 `/tmp/nsc-soak` 副本与源码（`:66-77`）、看最近 3 次采样的管理面/TLS/明文引导是否 200（`:79-84`）、句柄与 RSS 尾巴（`:86-108`）。
- 缺日志时的行为与报告一致：`:48-51` `if not os.path.exists(log): print("…这不是失败项…"); return 0`。
- `FNOS_HEAP_DEBUG` 只在 `components/fnos_monitor/Kconfig:3` 与 `main/main.cpp:73`、`:146`；回调每 10 s 调一次 `log_heap("periodic")`，只有堆日志，无任务存活/栈/刷新证据。

**判定：成立。** 新脚本与 72h 判据本身不在本机，无法验证（见 §0）。

---

## 3. 需要修正或补充的地方

1. **行号小偏差**（不影响结论）：`nvs_save()` 实为 `fnos_pair.c:473-491`（报告 473-490）；`nvs_load()` 实为 `:426-471`，报告的 453-463 指的是其中的证书分支；R3 的判定语句在 `fnos_collector.py:335-336`（报告写 333-338，覆盖了整段解析，可接受）。
2. **R3 触发可达性缺证据**（§2 R3）：请补一条真实 `/proc/mdstat`（inactive 行 + 汇总行同时出现），否则应把它降级为"防御性修正"，并把"inactive 被误报 degraded"的后果归到 R5。
3. **R5 与 §4 验收表冲突**（§2 R5）：§4 要求"RAID0/linear、元数据容器不无差别产生降级 crit"，但该项被定为 P2。二者需要一致：要么升为发布前项，要么在 §4 里注明本轮允许不通过及其发布说明义务。
4. **§8.2 成立，且后果比报告更具体**：`fnos_ui.c:3678` 的 `bool syncing = r->sync_pct < 100 && (strstr(r->state,"sync") || strstr(r->state,"recover") || strstr(r->state,"reshape"));` —— 动作词在 `what`、`state` 只有首词，因此对真实数据**这条分支永不命中**：进度百分比永远不显示，降级阵列一律走"降级"/`UK_DANGER`。报告只说"未知/容器会按 ok=false 显示降级"，实际连"带进度的降级"也显示不出进度。
5. **N3 应区分可达性**：token 截断（高可达）与"非 200 含 token"（需要非 2xx 的 JSON 体恰好带 `token` 字段，窄）不是同一量级的问题，并列成一条会让人误判优先级。
6. **R1 的告警字节预算应写明结论**：R1 拟修会让"降级 + 有进度"的阵列从 1 条告警变 2 条，但板端接收预算是 `CONFIG_FNOS_STATUS_MAX_BYTES`（`components/fnos_monitor/Kconfig:30-38`，默认 262144，范围 16384–1048576；`fnos_data.c:53-54` 兜底 256 KiB），增量在数十字节级，**不构成风险**；`nas/fpk/contract_check.py:376-393` 的 `PER_ENTRY` 最坏估算只在显式配置 `limits` 时才参与比较。报告没写这句，容易被误读成"改告警条数有预算风险"。
7. **HWM 单位应绑定前提**：报告写"ESP-IDF 此 API 单位为字节，不乘 4"。本工程成立——`sdkconfig.defaults` 未启用 `CONFIG_FREERTOS_SMP`（未出现该项），即使用 IDF FreeRTOS，栈以字节计（同文件 `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384`、`tools/stack_check.py:32` 的 16384 都按字节）。但上游 FreeRTOS 的同名 API 文档写的是"in words"，一旦换成 Amazon SMP 内核就会差 4 倍，建议在 72h 判据里写明"构建使用的内核变体"这一前提。

---

## 4. 未独立核验（明确边界）

- 补丁本体与 `tools/audit125/*`：不存在 → §3 文件清单、§6 命令、§7 测试通过数、§4 验收实现均不可复核。
- ESP-IDF 构建、静态栈检查、主机 UI 预览、字体重建：报告已标注未完成；本机同样缺 `sdkconfig`（仅有 `sdkconfig.defaults`）与 build ELF，无法补做。
- 实机 72 h、真实 NVS Flash 往返、协议切换、屏幕/触摸：报告已标注未执行；本次亦未执行。
- 外部文档原文：本机 `web_fetch` 对 `docs.kernel.org`、`man7.org`、`raw.githubusercontent.com` 等均返回"非公网地址"错误，故 MD mdstat 语义、`nvs_erase_key` 的 `ESP_ERR_NVS_NOT_FOUND` 语义、HWM 单位只能靠代码与工程配置间接佐证（§3.7 已给前提）。
- Release 列表（报告称只有 v1.1.0/v1.1.1）：标签已用 `git ls-remote` 复核，Release 页面未核。

---

## 5. 复核结论

- **就本次指定范围（P1 源码修复 + 主机回归）**：报告在公开 main `d84039f` 上的定位**全部成立**——R1/R2/R4 的基线行为与报告描述逐字复现，R3/R5 的代码缺陷成立（触发可达性待证），N1/N2/N3 的源码依据成立，S1 的"旧工具不构成板端证据"成立；§1 的版本事实、§8 的 ②③④ 三条限制都有直接源码依据；§7 中两项既有检查在本机独立跑通。
- **但报告的交付形态不完整**：所谓"已制作可审阅的局部补丁和主机回归"在本机与公开树中都取不到，报告自述的 20/17/14 项等测试绿灯无法被复核者重跑。**发布决策不应把这部分当作已验证**，除非 `v1.2.5-candidate.patch` 与 `tools/audit125/` 能被独立取得（给出路径/校验和）并按 §6 原样重跑。
- **最小下一步建议**（按最短路径排序）：
  1. 把补丁与 `tools/audit125/` 放到可复核位置（或说明其所在环境），这是本报告其余结论能否升级为"已验证"的唯一前提；
  2. 补 R3 的真实 mdstat 证据、并在 R5 的优先级与 §4 验收表之间做一次一致性修订；
  3. 其余（真实协议往返、ESP-IDF 构建、72 h）按报告 §5/§7 的顺序推进，不需要改变架构或引入新框架。

---

### 附：本次复核的可复现命令

```sh
git archive d84039f6f8538d47c4eacda80f26e1e8ebe8772c | tar -x -C /tmp/p4-audit-d84039f
git -C /Users/llll/code/esp/p4-7b-fnos-monitor ls-remote --tags --heads origin
cd /tmp/p4-audit-d84039f && python3 nas/fpk/collector_check.py && python3 nas/fpk/contract_check.py
python3 /tmp/repro_r.py        # R1/R2/R3/R4/R5 合成 fixture 复现（本次新建，只读导入审计树）
```
