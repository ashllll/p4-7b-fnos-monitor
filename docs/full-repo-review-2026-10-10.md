# 全仓代码审核报告

- 仓库：`/Users/llll/code/esp/p4-7b-fnos-monitor`（main，HEAD `f3b298a`「merge: 把 v1.2.5 候选整改线并入 main」，审核期间工作区干净）
- 审核日期：2026-10-10
- 审核工具：`opencodereview` (`ocr`) v1.12.13，LLM = `mimo-v2.6-pro`（provider `mimo-tokenplan`），`--audience agent`
- 审核方式：无 diff 全文件扫描（`ocr scan`，等价 "review the whole repo"），**只读**，未修改任何被审代码
- 复核方式：Lead 一手读码 + 16 个只读对抗式复核子代理（逐条 CONFIRMED / PARTIAL / REFUTED，必须给出触发条件与后果；范围见附录 F），关键结论用 ASan/UBSan 与主机实跑复现

---

## 0. 结论摘要

`ocr` 对本仓可扫子集的 106 个目标文件中的 **93 个**做了完整扫描（含 0 条评论者；未完成的 13 个见 §6 与附录 D，**已全部由人工对抗式复核覆盖**），共产生 **371 条原始评论**（critical 4 / high 69 / medium 168 / low 130）。逐条对抗式复核后的结论是：**扫描器的高严重度里有大量误报，真正需要修的是下面 22 条 high + 60 条 medium**。其中 `tools/preview/preview.c:3235`、`tools/certcheck/run.sh:71-73`、`tools/preview/scroll_gap.js`、`tools/preview/preview.c:2276-2292` 等 **11 条 high 是审核清单之外、由复核过程新发现**的高危项（H13–H22 里有 10 条属于"验收门给出错误安全信号"一类，另 1 条 H21 是实机显示缺陷）。

最需要立刻处理的 6 条：

| # | 位置 | 问题 | 证据强度 |
|---|------|------|---------|
| 1 | `components/fnos_monitor/fnos_ui.c:3717` | 上电首次刷新读 `NAV_TXT[-1]`（全局数组越界 8 字节） | **ASan+UBSan 实跑 abort，附运行时栈** |
| 2 | `components/fnos_monitor/fnos_data.c:373-374` | `hist_ts_ok/hist_span_s/hist_avg_gap_x10` 被整结构体覆盖 → 历史跨度/断流提示永久失效 | 一手读码 + grep 唯一写入者 |
| 3 | `nas/fpk/nasscreencompanion/app/server/fnos_collector.py:1007-1009` | FPK 采集器把 `zfs` 对象强制成 `[]` → 固件 `cJSON_IsObject` 恒失败，ZFS 页永远"未采集到"（1.2.5 已发出） | 一手读码 + 契约/固件/UI 三处交叉验证 |
| 4 | `components/fnos_monitor/ui_kit/uk.c:743-744` | `lv_malloc` 未判空即 `memset` → OOM 必崩，且绕过内存不足提示 | 一手读码 + LVGL `lv_malloc` 语义 |
| 5 | `components/fnos_monitor/fnos_pair.c:120-121` | PEM 解析在无换行装甲行上越过 `'\0'` → 堆越界读 | **ASan `heap-buffer-overflow` 复现** |
| 6 | `tools/preview/preview.c:3235` | 硬件清单审计的类检查写错（`lv_obj_check_type` 应为 `lv_obj_has_class`）→ 夹具矩阵 abort、两条反回归断言恒真 | 对照实验：改一个词后 3 个夹具全 PASS |
| 7 | `tools/parsecheck/main.c:117-120` | 合法"空硬件清单"帧 SIGSEGV（验收门自身崩溃） | 实跑 `rc=139` |

三条"验收工具假绿"同样重要（会给出错误的安全信号）：`tools/audit125/soak72.py:834` 零样本仍判 PASS、`soak72.py:470,473` 堆底线用瞬时值而非历史最小值、`tools/audit125/test_diag.py:196` 固件 SHA 断言按 64 字符写而死也过不了真机 8 字符。本轮复核又新增 10 条同类的"门假绿/门恒红"（H13–H20 + H22），另加 1 条实机显示缺陷（H21）：`tools/certcheck/run.sh` 在没有任何证书时三方指纹照样"一致 ✓"、`nas/fpk/board_sim.py` 在 TLS 关闭时仍报"TLS 固定…都通了"、`nas/fpk/mem_check.py` 把"未判定"返回 0、`nas/fpk/page_check.js` 打了 ✗ 却 exit 0、`tools/preview/scroll_gap.js` 对全部基线恒假红（16/16）且在"什么都没查"时 exit 0、`tools/stack_check.py` 在 objdump 读不了 ELF 或入口符号缺失时照样打印"全部入口在预算内"并退 0（`tools/verify_all.sh:71` 正是 grep 这句话判 PASS）、`tools/test_report.sh:34-42` 在生命周期套件报失败时不影响自身退出码（报告照发"M 项失败"却 exit 0）、`nas/fpk/manifest_check.py` 对非默认包与空字段照打"四处常量对得上 ✓"、`nas/fpk/nasscreencompanion/app/server/docker_api.py:89-90` 把容器名截断到 23 字符后**两个容器同名**（屏上无法区分、`dock_live` 身份比对失效）。

---

## 1. 审核范围与方法

### 1.1 覆盖矩阵

`ocr scan --preview` 在 HEAD `f3b298a`（`git ls-files` = **255** 个 tracked 文件）上的判定：

| 分类 | 数量 | 处置 |
|------|------|------|
| Will review | 106 | `ocr` 扫描（分批，见附录 A） |
| Excluded: too_large | 2 | 用 `--max-tokens 220000` 单独扫（**含旗舰文件 `fnos_ui.c`**） |
| Excluded: default_path | 4 | `tools/audit125/test_*.py` → 子代理人工审 |
| Excluded: unsupported_ext | 44 | 配置/构建文件 → 子代理人工审；`.md`（33 份）→ 子代理人工审 |
| Excluded: user_exclude | 69 | `tools/patches`(24)、`docs/archive`(20)、厂商 BSP(14)、生成字体(8)、LICENSE/.gitignore/verification.txt(3) |
| Excluded: binary | 25 | 图片/二进制（`docs/archive` 18、`nas/fpk` 7）→ 无代码可审 |

Too-large 只有 2 个文件，而且恰好是本项目最核心的两个：`components/fnos_monitor/fnos_ui.c`（264,647 B / 5174 行）和 `tools/preview/preview.c`（182,152 B）。它们用 `--max-tokens 220000` 单独成批扫完（session `e9d0ac69`，30 分 09 秒，15 条评论）。

### 1.2 补审（不适用 `ocr` 的文件）

| 子代理 | 范围 | 产出 |
|--------|------|------|
| `b182f026` | `tools/audit125/test_diag.py`/`test_pair_nvs.py`/`test_raid.py`/`test_soak72.py` | 6 条发现（3 high 假绿） |
| `c3d1727f` | 根/各组件 `CMakeLists.txt`、`sdkconfig.defaults`、`partitions.csv`、`dependencies.lock`、`nas/fnos-agent.service`、`components/fnos_monitor/Kconfig` | 3 条发现 |
| `3b2036bb` | 33 份 `.md` 文档（排除 BSP 与 archive） | 3 high + 6 medium + 行号漂移 |
| `6397ae21` | `tools/**`（32 条原始评论） | CONFIRMED 19 / PARTIAL 11 / REFUTED 3，新增 high 1 |
| `d7c04bd2` | `fnos_wifi_cli.c`/`fnos_wifi_store.c`/`fnos_net.h`（14 条） | 确认 11 / 降级 2 / 撤回 1，新增 medium 1 |
| `be0f140a` | `fnos_pair.c`/`fnos_pair.h`（12 条） | 确认 6 / 降级 2 / 撤回 2，ASan 复现 1 |
| `68e768f3` | `fnos_data.c`/`fnos_snapshot.c`/`fnos_perf.c`（22 条） | 7 CONFIRMED / 11 PARTIAL / 4 REFUTED |
| `cc983774` | `ui_kit/**`（16 条） | 真实严重度只有 1 high + 1 medium，5 条纯死代码 |
| `32314f25` | 头文件 + `main/main.cpp`（28 条） | 1 条真实功能缺陷（TOCTOU），3 条 REFUTED |

复核对每条评论要求：可复现的触发条件、明确后果、修复建议、以及**判定依据**（文件:行 + 上游语义）。凡"不可达 / 已被上游防护 / 误解 LVGL/IDF/cJSON 语义"的一律撤回，避免修不存在的 bug。

### 1.3 有效性审计（评论是否对当前 HEAD 有效）

- 本轮各批次（`59ee939a` 等）评论：把每条评论的 `existing_code` 做空白归一化后回查当前文件，**115/115 命中**（`/tmp/ocr_stale_check.py`）→ 无陈旧评论。
- 历史会话（`af46fdfe`/`b409c87b`，10-02）：29+9 条里 19 条指向已被删除的文件（`components/fnos_monitor/kk_ui/*`、`ui/*`）+ 6 条片段已不存在 → **降为附录，不进结论**（旧 JSON/MVVM 管线已按 AGENTS.md 移除）。
- 注入 `\0` 截断的蜜罐校验：`fnos_ui.c:3717` 段落被独立复现证实，非幻觉。

### 1.4 局限性（必须说明）

1. **实机未验**：全部结论基于源码 + 主机复现（`tools/preview` 主机渲染、ASan/UBSan、`tools/parsecheck` 夹具）。AGENTS.md 明确"主机预览/构建通过 ≠ 实机验收"，本报告不替代 `capture-board` 实测。
2. 厂商 vendored BSP（`components/esp32_p4_wifi6_touch_lcd_7b/**`）已扫 8 个文件，但它的 4 条 high 经复核全部属于"无固件调用者的 latent 缺陷"，降级处理。
3. 生成物（`components/fnos_monitor/fonts/**`）、历史补丁（`tools/patches/**`）、归档文档（`docs/archive/**`）与二进制资产不在审核范围，理由见 §1.1。
4. 部分文件受 LLM 超时影响需要重跑（见 §6 覆盖缺口），未覆盖到的文件在附录 A 列出。

---

## 2. 必须修（high，22 条）

### H1. `components/fnos_monitor/fnos_ui.c:3717` 上电首次刷新 `NAV_TXT[-1]` 全局越界读 —— CONFIRMED（ASan 实跑）

**现象**：`fnos_ui_create()` 先把 `s_page = -1`（`fnos_ui.c:4651`），紧接着调 `fnos_ui_set_page(0)`（`:4652`）。`fnos_ui_set_page` 的 `if (s_page < 0 || motion_reduced())` 分支（`:5023`）先 `refresh_page(idx)`（`:3841`）→ `refresh()`（`:3850`）→ `header_refresh()`（`:3713`）→ `NAV_TXT[s_page]`（`:3717-3719`）——此时 `s_page` 仍是 `-1`。写 `s_page` 的 `nav_select(0)`（`:3007-3013`）在这之后才发生。

**触发**：每次上电/每次 `fnos_ui_create`。**无条件**。

**后果（复核后修正，避免夸大）**：读 `NAV_TXT` 前 8 字节（相邻全局变量/链接填充），把该地址当 `const char*` 交给 `set_txt(..., "%s · 等待采集端", …)` 的 `vsnprintf` 解引用。**复核补充**：`nav_select(0)`（`:3007-3013`）在**同一次 `fnos_ui_set_page` 调用内**又调一次 `header_refresh()`，把标签文本覆写成 `"总览 · 等待采集端"`；`fnos_ui.c` 里没有 `lv_refr_now`，所以**主机上没有任何一帧被渲染出来**（lldb 实测：命中 `:3717` 时 `s_page = -1`，`NAV_TXT[-1]` 求值得到相邻的可读空串指针 `0x…a2ae ""`）。真正的风险取决于真机链接布局：那 8 字节若为 `NULL` → newlib `%s` 打 `(null)`；若为任意非地址数据 → `set_text_v`（`fnos_ui.c:315`）里 `vsnprintf(NULL,0,fmt,ap)` 解引用野指针 → LoadProhibited；若能对上相邻字符串指针 → 文本串位（同一调用内被覆写）。**因此定级 high 依据的是"每次上电一次真实全局越界读 + 设备侧可能 panic + sanitizer 门必然 abort"，而"屏上长期显示垃圾文本"这一说法不成立。**

**证据（Lead 亲跑 ASan/UBSan）**：

```
palette: device
/Users/llll/code/esp/p4-7b-fnos-monitor/components/fnos_monitor/fnos_ui.c:3717:67: runtime error:
    index -1 out of bounds for type 'const char *const[6]'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior .../fnos_ui.c:3717:67
==14071==ERROR: AddressSanitizer: global-buffer-overflow on address 0x000102b24cd8 ... READ of size 8
    #0 header_refresh+0x2e8
    #1 refresh+0x318
    #2 fnos_ui_set_page+0x1ec
    #3 fnos_ui_create+0x19b0
    #4 main+0x10b4
0x000102b24cd8 is located 8 bytes before global variable 'NAV_TXT' defined in
    '.../components/fnos_monitor/fnos_ui.c' (0x000102b24ce0) of size 48
SUMMARY: AddressSanitizer: global-buffer-overflow in header_refresh+0x2e8
```

复现命令（**不要**加 `ASAN_OPTIONS=detect_leaks=1`，macOS 不支持该选项）：

```bash
cmake -S tools/preview -B tools/preview/build-sanitize -DFNOS_PREVIEW_SANITIZE=ON
cmake --build tools/preview/build-sanitize --target preview -j8
PREVIEW_SIZE=1024x600 tools/preview/build-sanitize/preview /tmp/asan_out; echo "exit=$?"   # 134 (SIGABRT)
```

**附带结论**：README 文档化的 `FNOS_PREVIEW_SANITIZE=ON` 门现在**一跑即 abort**——即"sanitizer 门"实际处于失效状态。

**修法**（任一）：`header_refresh()` 里钳位（`int p = s_page < 0 ? 0 : s_page;`）；或在 `fnos_ui_set_page()` 进入 `s_page < 0` 分支前先写 `s_page = idx;`。

### H2. `components/fnos_monitor/fnos_data.c:373-374` 历史跨度字段被整结构体覆盖 —— CONFIRMED（功能静默失效）

**现象**：`fnos_data_commit_status()` 在 `s_lock` 内调 `fnos_status_copy(dst, st)`（`:374`），把 `s_status` **整结构体**覆盖成轮询暂存 `tmp`。而 `hist_ts_ok / hist_span_s / hist_avg_gap_x10` 只有 `fnos_data_parse_history()` 写（`:162-168`），`fnos_snapshot.c`（含 `fnos_status_parse`）从不写这三个字段；轮询循环是"先 `commit_status(&tmp,...)`（`:447`）→ 再 `parse_history()` 回填（`:450-457`）"，且 `hist_done` 置位后不再回填。

**触发**：第一次成功轮询（首次回填历史）之后的**下一次成功轮询**，`commit_status` 用 `hist_*=0` 的 `tmp` 覆盖 `s_status`。

**后果**：`fnos_ui.c:3057-3072` 的 `hist_span_text()`（调用点 `fnos_ui.c:4017`）因 `!hist_ts_ok` 恒走 `:3059` 分支 → 屏幕**永久**显示「最近 600 次采集」，秒/分钟/小时换算与「· 有断流」提示全部失效。这是本轮唯一"功能静默失效、界面永远错"的缺陷。

**测试盲区**：`tools/parsecheck/main.c:143-159` 在 `parse_history` 后立刻读字段；`tools/preview/preview.c:170-172`、`:433-435` 给夹具写死 `hist_*` → 两条主机路径都掩盖了该缺陷。

**修法**：`commit_status` 在 `fnos_status_copy` 之后恢复 `hist_*` 三字段（或让 `parse_history` 只写 `s_status` 而不经覆盖路径）。

### H3. `components/fnos_monitor/ui_kit/uk.c:743-744` OOM 时 `memset(NULL)` —— CONFIRMED

**现象**：`uk_kpi_create()` 里 `void *k = lv_malloc(sizeof *k);`（绕过 `uk_alloc`）后**未判空**就 `memset(k, 0, sizeof *k)`。LVGL `lv_malloc` 失败只 `LV_LOG_ERROR` 并返回 `NULL`（`managed_components/lvgl__lvgl/src/stdlib/lv_mem.c`）→ `memset(NULL,…)` 必然 StoreProhibited/复位。

**触发**：KPI 卡片分配失败（PSRAM 紧张时；首页 4 个 KPI 循环调用点 `fnos_ui.c:1350`，另有 `ui_kit/uk_number.c:322`）。

**后果**：崩溃；并且因为绕开 `uk_alloc`（`uk.c:32-47`，失败会置 `s_alloc_failed`），AGENTS.md 硬约束③要求的"界面内存不足"提示**不会出现**——失败表现为随机复位。

**修法**：改用 `uk_alloc` 并在用前判空（失败时返回占位卡并置 `s_alloc_failed`）。

### H4. `components/fnos_monitor/fnos_pair.c:120-121` PEM 指纹解析越界读 —— CONFIRMED（ASan 复现）

**现象**：`pem_fingerprint()` 的内层 `while (*p && *p != '\n') p++;` 在"装甲行是串尾且无换行"时停在 `'\0'`，随后 `continue` 让外层 `for` 的 `p++` **越过终止符**，`:128-131` 把堆上后续字节当 base64 收进 `s_b64` → 指纹错误或越界崩溃。

**触发**：`cert_pem` 的 `-----BEGIN CERTIFICATE-----` 行没有换行就结束（服务端畸形/截断响应，或 `fnos_pair.c:271` 的探针 `snprintf` 截断）。放行者是 `has_cert`（`fnos_pair.c:605` 调用）：它只查有没有 BEGIN 行、不查 END 行。

**证据**：复核子代理在 ASan 下复现 `heap-buffer-overflow READ of size 1 in pem_fingerprint`（脚本 `/tmp/pair_review/`）。

**修法**：内层循环后补 `if (*p == '\0') break;`（**不要写 `p += 1`**，那会跳过 BEGIN 行后的换行）；`has_cert` 增加 END 行校验；`tools/certcheck/extract.py` 抠的是同一份代码，需同步修改。

### H5. `nas/fpk/nasscreencompanion/app/server/fnos_collector.py:1007-1009` 把 `zfs` 对象强制成 `[]` —— CONFIRMED（功能静默失效，1.2.5 已发布的 FPK 包）

**现象**：采集快照收尾的"契约面"修正把 `zfs` 和列表型模块混在同一组：

```python
for k in ("vols", "raid", "disks", "temps", "docker", "zfs"):
    if not isinstance(snap.get(k), list):
        snap[k] = []
```

但 `zfs` 的契约是**对象**：`fnos_collector.py:703-727` 的 `_zfs()` 返回 `{"arc_gb": …, "hit_pct": …}`，`nas/fpk/contract_check.py:66` 的 `OBJ_METHOD = {"cpu","mem","net","zfs"}` 也把 zfs 归为对象型。

**触发**：任何一次 ZFS 采集成功（`_zfs()` 返回 dict）→ 同一帧内被覆盖成 `[]`。**每一帧都发生**。

**后果**：固件 `components/fnos_monitor/fnos_snapshot.c:208` `s.has_zfs = cJSON_IsObject(o)` 恒为 false → `fnos_ui.c:4375` 的 Agent 详情行永久显示「未采集到 ZFS 数据」（ARC 容量与命中率永不显示）。注意影响面：只走 FPK 伴侣（8798）；独立采集器 `nas/fnos-agent.py` 没有这段类型修正，8799 路径不受影响。

**归因**：该块是今天（2026-10-10）commit `9932675`「fix(1.2.5): 修掉自己引入的 payload 类型回归；bump 到 1.2.5 并重打包」引入的——为修 `test_lifecycle.sh` 7b 抓到的"raid 变 null"而加的兜底，顺手把 zfs 也扫了进去，属于**修复引入的新回归**，且 1.2.5 包已带此缺陷。

**修法**：把 zfs 从 list 组里移出，单独按"对象或 null"处理：

```python
for k in ("vols", "raid", "disks", "temps", "docker"):
    if not isinstance(snap.get(k), list):
        snap[k] = []
if not isinstance(snap.get("zfs"), (dict, type(None))):
    snap["zfs"] = None
```

**回归**：`bash nas/fpk/test_lifecycle.sh`（若有 zfs 断言）+ 主机上 `contract_check.py` 的 OBJ_METHOD 检查应能覆盖此类型；建议补一条"zfs 成功采集时必须是 dict"的用例。

### H6. `tools/preview/preview.c:3235` `hardware_contains()` 用错类检查 —— CONFIRMED（复核新增，**不在扫描器清单里**）

**现象**：`:3232-3240` 用 `lv_obj_check_type(object, &lv_label_class)` 做**精确类指针比较**找标签，但本仓 UI 的标签**全部**是 `number_class` 实例（`components/fnos_monitor/ui_kit/uk_number.c:29-30` `.base_class = &lv_label_class`、`:90-95` `lv_obj_class_create_obj(&number_class, parent)`，经 `ui_kit/uk.c:66-74 mk_label_raw` 创建）；`grep -rn "lv_label_create(" components/fnos_monitor` **0 命中** ⇒ `hardware_contains()` 恒返回 `NULL`。

**后果（两层）**：

1. **硬件清单审计跑不起来**：`PREVIEW_HARDWARE_FIXTURE=…/dense.json PREVIEW_SIZE=1024x600 ./build/preview out` → `Assertion failed: (label), function hardware_reachable, file preview.c, line 3245.` → exit 134（`compact`/`mixed` 同样；只有 `empty` 通过）。
2. **两条反回归断言恒真**：`:3379 assert(!hardware_contains(lv_screen_active(),"网口与接口"))`、`:3387 if (old) assert(!hardware_contains(lv_screen_active(), old));` —— "denied 必须藏起网口卡""清空后旧卷不得复现"这两条检查**从未真正生效**。这正是 AGENTS.md 硬约束③（硬件清单必须自适应、不得裁剪、超预算必须显式提示）对应的门。

**对照实验（决定性）**：仅把 `:3235` 改成 `lv_obj_has_class`，`compact`/`mixed`/`dense` 三个夹具全部 exit 0，并输出 `PASS: all hardware identities and final entries reachable, channel expansion and inventory shrink`；`MEASURE: tap_max=44 tap_dropped=0 seen=3045 seen_dropped=0`。

**修法**：`lv_obj_check_type` → `lv_obj_has_class`（一个词），并把硬件夹具矩阵纳入 `tools/preview/run.sh` 常规回归；修复后确认 `:3379`/`:3387` 两条负向断言仍然通过。

**回归命令**：

```bash
for f in empty compact mixed dense failure; do
  PREVIEW_HARDWARE_FIXTURE=/tmp/part3_verify/fixtures/$f.json PREVIEW_SIZE=1024x600 \
    tools/preview/build/preview /tmp/out-$f; echo "$f=$?"
done   # 期望 5/5 exit 0；修复前 dense/compact/mixed=134
```

### H7. `tools/parsecheck/main.c:117-120` 空硬件清单帧 SIGSEGV —— CONFIRMED（实跑 `rc=139`）

**现象**：`fields[]` 无条件取 `st.vols[0]`/`st.nets[0]`/`st.temps[0]`…，而 `fnos_snapshot.c:74-108` 在计数为 0 时向量指针保持 `NULL`。

**触发**：合法输入 `{"ready":true,"host":"h","cpu":{…},"net":{…}}`（无卷/无盘/无温度）→ `./ps empty.json` 实测 `rc=139`。

**后果**：AGENTS.md 硬约束③明确要求"硬件清单必须自适应、不得按型号/数量裁剪"，而验收门自己遇到空清单就崩；`tools/preview/make_payload.py:59-72` 强制每段 ≥1 条 ⇒ **这条路径在门里永远踩不到**。

**修法**：`st.nvols ? st.vols[0].xxx : ""`（或按段遍历）+ 增加"空清单"fixture 到 run.sh。

### H8. `tools/serial_capture.py:56-61` 收尾复位是死代码 —— CONFIRMED

**现象**：全文件只定义 `main`；`:57` 调用的 `hard_reset` **没有定义**，抛 NameError 后被 `:58 except Exception` 静默吞掉；`:16` 的 `--reset` 从未被引用；实际只剩 `:60 ser.close()`，而它在 `:21-24`/`:51-55` 自述会把板子留在 `waiting for download`（本项目曾因此误判"固件没跑"）。`docs/verification.md:614` 还写着"捕获后自动复位回运行模式"。

**同文件第三条（`:25`）**：`:21-24` 的长注释声称"打开端口时就把 DTR/RTS 钉在正常启动电平"，但 `:25` 只是 `serial.Serial(args.port, args.baud, timeout=0.2)`，全仓搜不到 `setDTR/setRTS/ser.dtr` 任何写法 → **注释描述的缓解措施根本没有实现**（pyserial 默认 `dtr=True/rts=True`，对 USB-Serial-JTAG 恰是 IO0=低+EN=低）。维护者会误以为副作用已处理。修法：要么显式置安全电平并保持到 close，要么把注释改成如实描述。

**修法**：真正调用同文件 `:48-49` 已写好的 `python3 -m esptool --after hard-reset chip-id`（或删除该段并改文档）。

### H9. `tools/audit125/` 三处"假绿" —— CONFIRMED

| 位置 | 问题 | 后果 |
|------|------|------|
| `tools/audit125/soak72.py:834` | 采集零样本仍 `state["complete"] = True` → `return EXIT_PASS`（`:855`），无 `sample_count` 守卫；`test_soak72.py:631` 的 FakePort 恒有数据 → 零样本分支无用例 | 采不到数据也报"72h 通过" |
| `tools/audit125/soak72.py:470,473` | 堆底线取 PSRAM **瞬时** `pfree`（`min(r["mem"]["pfree"] …)`），而非 README §3 要求的**历史最小值** `pmin`（`test_soak72.py:239` 反倒设了 `pmin`） | 缓慢泄漏会被瞬时值掩盖 |
| `tools/audit125/test_diag.py:196` | `HEX64 = re.compile(r"^[0-9a-f]{64}$")` + `:236 HEX64.match(rec["fw"])`；真机 `fw` 只有 **8** 字符（`CONFIG_APP_RETRIEVE_LEN_ELF_SHA=9`，sdkconfig:687；IDF `esp_app_desc.c:113` `n = MIN(size, sizeof(app_elf_sha256_str))`），`main/main.cpp:123-124` 用 `char fw[80]` | 断言永不成立（替身硬编 64 字符，`soak72` 侧按 `ELF_SHA[:9]` 前缀比） |

**修法**：加 `sample_count == 0 → error, exit 1`；堆底线改用 `pmin`；SHA 断言放宽为前缀或把 CONFIG 提到 65。

### H10. `README.md:7,11,13,76` 对外发布信息错误 —— CONFIRMED

**现象**：README 写"当前开发源码 `cc2da56…` 配套应用 **1.2.4**"、`:76` 包 SHA-256 = `cc10d47f…`；实测 `nas/fpk/nasscreencompanion/manifest:2 version = 1.2.5`，`nas/fpk/nasscreencompanion.fpk` = `afed3abd9d910b13302b29a0d058b504ee5c279155f2c4b43b587193456bcc35`、83,741 B。

**后果**：首页校验值对不上任何现存产物（用户按 README 校验必然失败）。

**修法**：更新为 1.2.5 / `afed3abd…` / 83741 B。

### H11. `docs/review-v1.2.5-audit-2026-10-10.md:26-31` 与 `tools/audit125/README.md:4-5` 的"无法复核"段落 —— CONFIRMED

**现象**：两份文档称 `v1.2.5-candidate.patch`、`tools/audit125/*`"在目标仓库、`~/code/esp`、`~/Downloads`、`~/Desktop`、`/tmp` 中均不存在"；实测根目录有 `v1.2.5-candidate.patch`(182,537 B) 与 `v1.2.5-candidate-on-perf.patch`(210,070 B) 且被 git 跟踪，`tools/audit125/` 及 5 个脚本也在树内。

**后果**：后续复核者会跳过本可复核的对象，或以为仓库缺件。

**修法**：改写为"审计对象已在 main 树内，可复核"。

---

### H12. `nas/fpk/nasscreencompanion/app/server/nas_companion_server.py:1079-1110` TLS 探测放在 accept 线程且没有超时 —— CONFIRMED（远程未认证即可让整条遥测瘫掉）

**现象**：`get_request()` 里对刚 accept 的连接做 `head = conn.recv(1, socket.MSG_PEEK)`（`:1096`）并在同一函数里 `wrap_socket(..., server_side=True)`（`:1101`）做 TLS 握手。stdlib 的 `socketserver._handle_request_noblock` 是在 `serve_forever` 线程里**先**调 `get_request()`，之后 `ThreadingMixIn` 才为连接起线程；而 `BaseHTTPRequestHandler`/`StreamRequestHandler` 的 `timeout` 是 `None`，全文件没有任何 `settimeout`。

**触发**：从局域网连上 8798 后**一个字节都不发**（或发一半 TLS 握手中途停住）。peek 与握手都会永久阻塞。

**后果**：accept 循环卡在这一个 socket 上 → listen backlog（`request_queue_size` 默认 5）被占满 → 之后所有连接被挂起/丢弃：开发板 1 Hz 遥测与配对全部停摆，**不写任何日志**。远程、未认证、单条 socket 即可。管理面（AF_UNIX）不受影响。

**修法**：把 peek + TLS 探测搬到每连接线程里（覆写 `process_request`，或在 `handle()` 顶部做 TLS 判定）；最低限度在 `accept()` 之后立刻 `conn.settimeout(10)`。

---

### H13. `tools/certcheck/run.sh:16,34-36,50-52,71-73,83-92` 三方指纹对照在"什么都没跑起来"时全打 ✓ —— CONFIRMED（实跑复现 `exit 0`）

**现象**：`:16` 只有 `set -u`（无 `-e`、无 `pipefail`）。`:34` 的 `openssl req` 兜底失败或 `:30-32` 的 `cp` 失败时 `$PEM` 根本不存在，于是三条对照的取值全部为空：`FIRMWARE=$("$OUT/fp" "$PEM")` → `tools/certcheck/main.c:15-19` 只 `perror` 到 stderr、stdout **什么都没有**；`OPENSSL` → `openssl x509` 失败 → `""`；`PY` → python `FileNotFoundError` → `""`。`:71/:72/:73` 比较的是 `"" = ""` → **三条全部打印 ✓、`rc` 保持 0、脚本 `exit 0`**（`:84/:88/:90` 对 `$CRLF`/`$CHAIN` 同理）。

**触发**：`PATH` 里没有 `openssl`（或 openssl 被策略限制）、`/tmp/nsc-soak/var/tls/server.crt` 不可读。

**后果**：固件 ↔ 服务端指纹等价性是配对模型**唯一的手工信任锚**（三方交叉验证），却在没有任何证书被编译、解码、比较的情况下报告"三方一致"。实跑复现：把一个立刻失败的假 `openssl` 放到 `PATH` 首位 → `EXIT=0`，日志依次打印「固件 pem_fingerprint() : 」（空）、「服务端 Python 那套 : 」（空）、「openssl 官方说法 : 」（空），然后「✓ 固件、服务端、openssl 三方一致…」。

**修法**：`:16` 改 `set -euo pipefail`；`:37` 之后加 `[ -s "$PEM" ] || { echo "✗ 没有可用证书（openssl 失败？）" >&2; exit 1; }`；`:71` 之前加 `[ -n "$FIRMWARE" ] && [ -n "$OPENSSL" ] && [ -n "$PY" ] || { echo "✗ 有值为空，对照根本没跑起来" >&2; exit 1; }`。

---

### H14. `tools/preview/scroll_gap.js` 滚动条净距看门狗：全仓基线恒假红，且"什么都没查"也报成功 —— CONFIRMED（实跑 `16/16` + `4/4` 假红）

**(a) `:97-109,:126` 把"整行铺满"的背景带当成内容右缘**：逐行从滚动条右侧开始找第一个非底色像素当"内容左缘"、再找最右非底色像素当"内容右缘"，于是卡内任何**横贯整卡的背景带**（实测 `tools/preview/out` 某卡 `y=104` 的橙色带 `x=8..1015`）被判成内容右缘 → `gap = 2` 恒判不合格；而该卡滚动条左 24 px 内近白**文字**像素为 **0**。实跑：`node tools/preview/scroll_gap.js tools/preview/out` → `exit 1`，基线 **16/16 全假红**；`node … tools/preview/palette-out/graphite` → `exit 1`，**4/4 全假红**。因为 `tools/preview/palettes.sh:26` 直接拿它的退出码当门，三套配色的看门狗**实质全红** → 真回归会被淹没（文件头自述 2026-10-06 已发生过同类"15 处假不合格(内容→条=2px)"）。

**(b) `:153,:125,:142` 空目录/无 PNG/所有 list 卡 `!track` → `checked=0 bad=0` → `exit 0`**，仍打印「不合格 0 处」：把滚动条整个删掉（合成 C）也报成功。`:126-129` 另有一条：`cr === -1`（除滚动条外无任何非底色像素）时 `gap = x0 + 1 = 281px` 判 OK（合成 A：只有滚动条、零内容，`exit 0`）。

**修法**：①先按行剔除"铺满卡宽的连通带"，只统计文字色/非整行连通的像素列；②`checked === 0` 时 `exit 2`；`track` 缺失单独列出并计入失败；③`cr < 0` 报"未检测到内容"失败。修好之前不要动 UI 基线。

---

### H15. `nas/fpk/page_check.js:613,205` 异步路径抛异常后打了 ✗ 却 `exit 0` —— CONFIRMED（实跑复现）

**现象**：检查体是一个 async IIFE，`:205` 的汇总与 `process.exit(failures ? 1 : 0)` 在 IIFE 末尾；`:613` 的 `unhandledRejection` 处理器只打印 ✗，**不改退出码**。

**触发**：IIFE 在任一 `await` 之后抛异常（实测把 `/api/logs` 的 `lines` 由字符串换成对象）。

**后果**：输出 `✗ 异步路径抛出未捕获异常：firstLog.slice is not a function`，**没有汇总行、`exit=0`** → `nas/fpk/test_lifecycle.sh:389` 按退出码判定 → 假绿。同文件另有两处断言强度不足：`:328` 概览断言是对整块 `#cards` innerHTML 的子串搜索——把页面所有 CPU 数字改成 `0.0 %` 仍 `exit 0`（needle `"2.2"` 命中的是进度条内联 `style="width:2.2%"`）；`:230-244` 点击表硬编码 5 个 id，而垫片的 `querySelectorAll('[data-revoke]')` 返回 `[]` → `index.html:324-327` 的设备"撤销"处理器**从未被执行**却全绿。

**修法**：处理器里补 `process.exitCode = 1`，汇总/`exit` 移入 `finally`；概览断言按每个 `.card` 切片；点击目标从 HTML 机械扫出（照同文件 `:586` 的对账方式）。

---

### H16. `nas/fpk/board_sim.py:163-167,178,181,292-294` TLS 关闭时"TLS 固定"整块跳过，仍报"都通了" —— CONFIRMED（实跑复现）

**触发**：NAS 侧关掉 TLS（`tls_enabled=false`，向导可关）。

**后果**：③「固定证书 / 固定别的证书 / 对照」三条断言整块不跑，脚本仍 `exit 0`，并在 `:292-294` 打印「配对链路、**TLS 固定**、字段契约都通了」——**未验证的安全属性被报成已验证**（已端到端复现：对 `/tmp` 自建 TLS-off 假 NAS 运行 → 0 且打印上述总结句）。另两处虚增：`:170-176` 的 `:172` 无条件 `ok("② 自算指纹…")` 恒真，真比对 `:174` 静默不跑；`:242-244` 的 `except ssl.SSLError: pass` 把"错误配对码被拒"整条吞掉。

**缓解（定级说明）**：`nas/fpk/test_lifecycle.sh:544-547` 在 7d 阶段用 `grep -q '固定一张别的证书时握手失败' boardsim.log || bad …` 兜住了这个静默跳过，且该套件 TLS 默认开（`CACERT="$STAGE/var/tls/server.crt"`）→ 套件路径与 `docs/fnos-companion-test-report.md:52` 的"通过"不受影响。受害者是按 `docs/fnos-companion-app-plan.md:453-455`「装完包之后最值得跑的那条命令」手动跑 `board_sim` 的人。

**修法**：TLS 未开时总结句改为"未验证 TLS 固定"且不计通过；`:172` 改前置检查；`:243` 改 `bad(...)`。

---

### H17. `nas/fpk/mem_check.py:79-81` 采样不足时"未判定"返回 0，与通过同码 —— CONFIRMED

**触发**：采样 < 6 条（`argv` 传 0、请求 < 3000、连接失败任一）。

**后果**：打印「样本太少，不做判定」却 `return 0`，退出码与真正通过完全相同；`tools/test_report.sh:67-79` **只看退出码** → "没测"被记成"通过"。

**修法**：未判定返回独立非 0 码（如 `3`），调用方按码区分"通过 / 未判定 / 失败"。

---

### H18. `tools/stack_check.py:95-96,171-173,191` objdump 读不了 ELF 时照样打印"全部入口在预算内"并退 0 —— CONFIRMED（实测复现）

**触发**：objdump 能跑但读不出这个 ELF（`--elf` 指错、被 strip、架构不符）→ `:95-96` 的 `subprocess.run` 返回非 0 且 stdout 为空。

**后果**：脚本先打印 `0 个函数`、再对四个入口各打一行 `！在 ELF 里找不到这个入口`，最后**照样**打印 `全部入口在预算内`（`:191`）并 `exit 0`。`tools/verify_all.sh:71` 正是 `grep` 这句话来判 `PASS 栈深在预算内` → 静态栈预算这道 AGENTS.md 强制门在"根本没测"时给出通过。实跑复现：`python3 tools/stack_check.py --elf tools/preview/build/preview`（工具链在 PATH）→ `rc=0` + 上述输出。

**修法**：`:95-96` 之后加 `if p.returncode or not p.stdout.strip(): print("objdump 读不了这个 ELF…"); return 2`（fail closed），并把"找不到入口"计入失败计数。

---

### H19. `tools/stack_check.py:131-134,171-173` 入口符号缺失时该任务栈深不检查也不计失败 —— CONFIRMED

**触发**：某个入口函数被改名/删除（或上面的"ELF 读不出来"，此时四个入口同时触发）。

**后果**：`worst()` 返回 `None` → `:131-134` 直接 `continue`，**没有 `over += 1`** → 那个任务的预算从不被检查，汇总里仍是"全部入口在预算内"、退 0。真实入口改名/合并时会静默丢掉一整条调用链的栈深检查。

**修法**：把"找不到入口"当成失败（`over += 1` 或独立的 `missing` 计数器强制退 1）。

---

### H20. `tools/test_report.sh:34-42` 生命周期套件报失败时脚本自身仍退 0 —— CONFIRMED

**触发**：`nas/fpk/test_lifecycle.sh` 报告 ≥1 项失败，或在打印结果前死掉。

**后果**：`:34` 起把套件输出重定向进日志，然后只用 `if ! grep -q '0 项失败'` 判断，**该分支只写日志、不动 `HARD_FAIL`** → 脚本退出 0，报告正文照常写一行"N 项通过，M 项失败"，与正常结果不可区分；只有人工读那行才会发现。这直接违反同文件 `:58-61` 自己写的规则（"不只打印，还要…影响退出码"）。配合 §3.50 的 `tools/test_report.sh:119-124`（栈深门跳过只打控制台），"报告看起来完整"这件事本身不可信。

**修法**：该分支里加 `HARD_FAIL=1`；并单独检查 `nas/fpk/test_lifecycle.sh` 的退出码（`grep` 失败与"进程非 0 退出"是两种失败）。

---

### H21. `nas/fpk/nasscreencompanion/app/server/docker_api.py:89-90` 容器名截断到 23 字符后两容器**同名**，屏上无法区分 —— CONFIRMED（实测复现）

**触发**：两个容器名共享前 23 个字符（实测 `very-long-service-name-01` / `-02`）。

**后果**：截断后**同名**，屏上两条标签完全一样；若 `-01` 运行中而 `-02` 已退出，用户看到的是两个相同名字、状态不同的条目，无法判断是哪一个；`dock_live` 的身份比对（用名字匹配）也随之失效。这是本轮唯一**直接打在真机屏幕上**的新缺陷，且与 AGENTS.md「硬件清单必须自适应、不得裁剪」的精神冲突（这里裁的是名字的可区分性）。

**修法**：保留可区分的尾部（如 `前 16 字符…后 6 字符`）或追加容器 Id 前 6 位；截断时显式标注为截断形式。

---

### H22. `nas/fpk/manifest_check.py:77,203,260` 非默认包与空字段照样"四处常量对得上 ✓" —— CONFIRMED（实测 T1–T7）

**触发**：传入非默认包（实测 T3：`appname=otherpkg, service_port=9999`）、`display_name`/`desktop_applaunchname` 留空（T4/T6）、`config` 顶层是数组（T2）。

**后果**：T3 打印「四处常量对得上 ✓」并 `exit 0` —— 这是**上架前的自检背书**，但核的是硬编码的默认包路径，换包即失效；T4/T6 空字段静默退 0（空 `display_name` 在应用中心就是空白条目）；T2 的 `data.values()` `AttributeError` 崩在 `:260` 而不是 `:87`，把"配置格式不对"报成脚本自身崩溃（归因错）。

**修法**：wiring 接收 `pkg` 参数而不是硬编码默认包；`REQUIRED` 字段增非空校验；解析后先 `isinstance(data, dict)` 再取 `.values()`。

---

## 3. 必修（medium，60 条）

1. **`components/fnos_monitor/fnos_data.c:194-197` TOCTOU —— 以为配好仍在明文轮询**：`s_conn_gen = fnos_pair_generation();`（`fnos_pair.c:872-888` 无锁）与 `fnos_pair_active(&s_conn);` 是两次独立读取；配对若恰在两者之间完成，`s_conn` 保存旧（明文/无 token）端点而 `s_conn_gen` 已是新一代 → `fnos_data.c:433` 的代次比较永不触发，设备静默继续轮询旧端点，UI 却显示"已配对"。修法：`fnos_pair_active` 在同锁内返回代数（`uint32_t fnos_pair_active(fnos_pair_cfg_t *out, uint32_t *gen);`），`fnos_data.c:196` 改用它。
2. **`components/fnos_monitor/fnos_snapshot.c:161-163` net 摘要漏字段**：摘要分支只解析 `if/rx_kbs/tx_kbs/rx_total_gb/tx_total_gb`，漏 `state/speed_mbps/physical`，而 `:202-207` 的 interfaces 循环解析了 → `net.state` 恒 `""`、`speed_mbps` 恒 0；采集端 `nas/fnos-agent.py:214-244`（`summary = dict(selected)`）确认三键已下发。修法：摘要复用数组循环的解析。
3. **`components/fnos_monitor/fnos_snapshot.c:150-151` `number()` 无 clamp**：`double` 直接赋给 `uint32_t/int64_t/int`，越界/NaN/±inf 是 UB，实机显示垃圾数（不崩）。
4. **`components/fnos_monitor/fnos_wifi_store.c` 凭据写入/擦除失败被吞**：`:72-75` `save()` 失败仍打印"凭据已保存"；`:82-86` `clear()` 丢弃 `nvs_open/erase/commit` 全部返回值且声明为 `void`（`fnos_wifi_store.h:27`）仍打印"已清除"；调用方 `:63` 直接 `{ clear(); return true; }` → CLI 无从感知"以为删了其实没删"（含安全语义）。
5. **`components/fnos_monitor/fnos_net.h:9` "幂等，非阻塞"误导 + LVGL 任务内同步 Wi-Fi RPC**：`fnos_ui.c:2523`（`wifi_pick`）、`fnos_ui.c:2667`（`wifi_connect_cb`）在 `lv_timer` 回调链（`ui_tick` 注册于 `fnos_ui.c:4656`）里调 `fnos_net_set_credentials` → `fnos_net.c:262` NVS 提交 + `:272-273` `esp_wifi_disconnect/set_config/connect`，最坏阻塞数秒（`fnos_net.h:36-37` 自认 5 s），直接顶住 LVGL 帧。修法：照 `fnos_net.c:68-71 s_connect_wanted` 范式改成置标志由 service 任务执行。
6. **`components/fnos_monitor/fnos_wifi_cli.c:110-111` 前缀匹配**：`!strncmp(sub,"set",3)` / `!strncmp(sub,"clear",5)` → `wifi clearing` 直接清 NVS、`wifi setfoo bar` 写 SSID=foo。修法：token 后 `strcmp` 精确匹配。
7. **`components/fnos_monitor/fnos_wifi_cli.c:127-128` 超长行被拆成多条命令**：`CLI_LINE_MAX=192`（`:22`）下 >191 B 的行被 `fgets` 截断成两条命令依次执行。修法：丢弃超长行并报错。
8. **`components/fnos_monitor/fnos_wifi_cli.c:99-101` `tls` 命令把日志级别提到 DEBUG 后从不恢复**（探针是 detached 任务 `fnos_pair.c:317-323`，`probe_task` 结束只 `vTaskDelete`）→ 之后串口永久刷 DEBUG 日志。
9. **`components/fnos_monitor/fnos_net.h:43` 扫描缓存无锁协议**：`fnos_net.c:341-357` 先 `s_ap_n = 0` 再回填同一数组；读侧 `fnos_net.c:369-374` 先读条数再逐条拷（`fnos_ui.c:2477` 每 tick、`:2530` 点击时）→ 同一帧内 SSID 撕裂（字符串始终有 NUL，无越界）。另 `fnos_net.c:228 static char buf[160]`、`:282 static char buf[16]` 跨任务共用。
10. **`components/fnos_monitor/fnos_net.c:113` `reason_text()` 不认 211/212** → 配对/连接失败时屏上显示"原因码 211"而非可执行提示。
11. **`main/main.cpp:210-224` `bsp_display_lock` 失败静默**：只打一条 `ESP_LOGE(TAG,"display lock failed")`，之后永久无 UI、无重试、无外显状态（`:232-235` 夜间定时器创建失败、`:236-242` HEAP_DEBUG 分支同样静默）。修法：有限重试 + 降级标志 + 屏上提示。
12. **printf 格式属性缺失（AGENTS.md 硬约束⑤相关）**：`fnos_data.h:95` 缺 `__attribute__((format(printf,3,4)))`，`ui_kit/uk.h:86` 缺 `format(printf,2,3)`。今天调用点都是字面量 → 无实例缺陷，但本仓历史上漏过格式符，屏上残留 `%d/%s/%u` 正是同一类问题。
13. **`sdkconfig.defaults:30` `CONFIG_LV_USE_CLIB_MALLOC=y` 与 `:85` `CONFIG_LV_USE_CUSTOM_MALLOC=y` 互斥**（LVGL Kconfig:45-63 同一 choice，后写者胜；生成物 `sdkconfig:2893/2896` 证实 CUSTOM 生效）：删/移 `:85` → `components/lvgl_mem_psram` 的 `lv_*_core` 全部成为死代码、LVGL 对象回落内部 RAM（≈361 KB）→ OOM，而且**没有任何编译期报错**。修法：删 `:30` 或移到 `:85` 之后。
14. **`main/CMakeLists.txt:4,8` 缺 `esp_timer` 依赖**：`main.cpp:17/:100/:232` 使用 `esp_timer`，而 main 的 `REQUIRES`（fnos_monitor、BSP、lvgl_mem_psram、nvs_flash）+ `PRIV_REQUIRES esp_app_format` 都没有它；`esp_timer` 不属 IDF 公共依赖（IDF `build.cmake:275`），BSP 只 public `esp_driver_*`+fatfs，LVGL 用不传播的 `PRIV_REQUIRES ${IDF_COMPONENTS}` → 当前唯一供给是 `fnos_monitor:44` 的 public `esp_timer`（一旦上游收紧即断）。修法：`main/CMakeLists.txt:8` 显式加 `esp_timer`。
15. **`tools/preview/CMakeLists.txt:15` 导出正则漏 `CONFIG_FNOS_SNAPSHOT_MAX_BYTES`**：而 `preview.c:44` 编了 `fnos_snapshot.c`（`:15-16` `#ifndef` 兜底 `(1024*1024)`，`:37-38` 当快照池预算）；今天默认值（`sdkconfig:2551 =1048576`）与兜底相等无差，改成 Kconfig 允许的 65536 就会出现"真机丢弃 >64 KB 快照、主机门仍按 1 MB 通过"。
16. **`tools/**` 验收门可信度**（逐条见附录 B）：
    - `tools/parsecheck/main.c:120` UTF-8 扫描漏 `temps[].dn`、`mods[].name/status`、`raid[].health/what`、`disks[].dev`，而 `docs/fnos-companion-test-report.md:66` 声称全字段已验；
    - `main.c:139-140` `hbuf[512*1024]` 静默截断报成 `history parse=FAIL bytes=524287`；
    - `main.c:166,171` 守卫 `argc >= 7` 却读 `atof(argv[7])`（`run.sh:50-52` 恒传 8 参，门路径不踩）；
    - `tools/preview/snapshot_check.c:52-54` 断言全写在 `assert()`：`CMakeLists.txt:83` 的 `-UNDEBUG` 与 sanitize 目标挡住了 NDEBUG，但 `run.sh:59/62` **只编译并只运行 `preview`，从不执行 `snapshot_check`**（唯一执行处是 `tools/preview/README.md:74` 的手工命令）→ ownership/并发/预算三项验收无自动化证据。修法：`run.sh` 执行它并把结果纳入报告，关键断言改显式 `if` + 退出码；
    - `tools/certcheck/main.c:26-29` 目录输入误报"PEM 读不出来或太长"；`:20-23` 读满不报"输入太大"；
    - `tools/photo_check.py:14/:20-22/:66-69/:74`（assert 魔数、未校验 `bit_depth/interlace/ctype`、忽略 alpha、`tot==0` 除零）；
    - `tools/serial_capture.py:26-29` `time.time()` 非单调（应 `monotonic`）、`:33-34` `buf` 无上限、`:41-42` 残留缓冲无时间戳；
    - `tools/preview/snapshot_check.c:50` `cJSON_PrintUnformatted` 未判空；
    - `tools/preview/stub/esp_heap_caps.h:5-6/:12-13` 常量与 IDF 5.5.3 不符（真值：DMA `1<<3`、SPIRAM `1<<10`、INTERNAL `1<<11`、DEFAULT `1<<12`）；`stub/sdkconfig.h:4` 与 `ui_config_host.h` 重定义冲突（实测设备值 360 胜出，该行纯死，可删）。
17. **`tools/preview/preview.c` 主机门可信度**（part3，9 条原始评论；复核后 **1 条升 high（H5）、其余全部降级或撤回**，见 §4）：
    - `:158` `assert(fnos_status_create(s,&counts))`、`:3282/:3285` `assert(fread(...))` + `assert(fnos_status_parse(...))`、`:2672` `assert(fnos_data_get(&s_motion_fixture))` 把**有副作用**的调用写进 `assert`——副作用属实，但"NDEBUG 下被擦除"在本仓**所有常规构建路径都不成立**：`tools/preview/run.sh:57-58` 传 `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O1 -g"`（覆掉 CMake 默认的 `-O2 -g -DNDEBUG`；实测 `flags.make` 里 `C_FLAGS = -O1 -g -std=gnu11 -arch arm64 …` 无 `-DNDEBUG`），sanitize（`CMakeLists.txt:70-74`）与 `snapshot_check`（`:83`）还显式 `-UNDEBUG`，且仓库无 CI。**唯一危险路径**是手工 `cmake -S tools/preview -B build`（`CMakeLists.txt:6-8` 默认 Release）：create 被擦除而 `:159 s->ever_ok = true` 仍在 ⇒ 旧快照被当"在线"；`:3283 static fnos_status_t fixture` 若全零，则 `:3311-3387` 整个硬件矩阵**空跑并报 PASS**（最危险的假绿形态）。→ **medium（潜伏）/ medium-latent**。修法：改成真 `if (!…) abort();`，并给 preview target 无条件加 `-UNDEBUG`；负向对照用 `-DCMAKE_BUILD_TYPE=Release` 在 `/tmp` 副本构一次。
    - `:1345-1346`（`verify_container_order` 的 `fnos_status_t st = {0}; fnos_data_get(&st);` 从不 `fnos_status_release`；同模式还有 `verify_overview_charts`/`verify_ui_regressions`/`verify_docker_module_states`/`verify_temperature_protocol_limit`；调用点 `:1346` 漏、`:2672` 漏、`:3294-3299` 正确、`:3389` 正常路径清理）、`:3337-3338`（`PREVIEW_ALLOC_FAIL_AT` 分支提前 `return` 跳过 `:3389`）→ 机制 CONFIRMED，影响 low（host 侧 `fnos_snapshot.c:28 return malloc(n)`；macOS ASan 无 LeakSanitizer，泄漏量不可实测）。
    - `:804` `SEEN_MAX(4096)` 溢出后静默丢弃 → 复核为 **low**：实测默认 `seen=1685 seen_dropped=0`、dense 夹具 `seen=3045 seen_dropped=0`（占 74%，余量 1.35×）；真溢出会抬高 `s_never_visible` 并在 `:3606-3610 abort()`（响亮但归因误导）；夹具模式下该审计在 `:3338` 早退不跑。
    - **已撤回**：`:694` `MAX_TAPPABLE(256)`（实测最大 **57**，余量 4.5×）、`:516-519` `flush_cb` 越界（`s_frame[SCR_MAX_W*SCR_MAX_H]` `:508-511` + 入口 `:3490-3495` 钳 `w<=1280/h<=800`，display 按 `s_scr_w/h` 建 `:3497`；`PREVIEW_PARTIAL` 是 opt-in `:3500-3502`；像素 memcmp 断言 `:2911/:2981/:2989/:3009/:3019/:3108/:3129/:3157/:3185/:3218` 都在默认 FULL 下跑）、`:1271-1273` `state_name()` 漏 `ST_LOCKBUSY`（唯一调用点 `:1313/:1320` 在 `run_state`；7 次调用 `:3552-3555/:3595/:3599` 都不会遇到该状态）→ 移入 §4。

18. **`run-as` 文档措辞与 `config/privilege` 不一致（原报告定为 high，**复核后降级**）**：`docs/fnos-companion-app-plan.md:67` 写"`config/privilege` 用 `run-as=package`"、`nas/fpk/README.md:172` 写"运行身份是专用包用户（`run-as: package`），不是 root"，而实测 `nas/fpk/nasscreencompanion/config/privilege:4` = `"run-as": "root"`。**但实际设计是对的**：`config/privilege` 为 root 是生命周期脚本（写 `/var/apps`、读 root 态 `docker.sock`）所需，服务本体由 `cmd/main:39-47` 在 root 下启动 `/usr/bin/python3 -I cmd/docker_launch.py --user "$package" …`，而 `cmd/docker_launch.py:220-222` 在 exec 前 `setgroups([])/setgid()/setuid()` **永久降权**，`cmd/_common.sh:49-58` 的 `as_package()` 也用 `runuser` 操作包内数据，`cmd/install_init:12` 的表述（"管理服务使用非 root 包用户；生命周期使用 root"）与实现一致。→ 安全承诺不成立的部分只是"`run-as: package`"这个**文件级措辞**（README.md:71 的"管理服务以普通包用户运行；独立 root 辅助进程仅提供固定的容器状态读取"与实现相符）。**修法**：把两处"`run-as: package`"改为"privilege 声明 `run-as: root`，服务进程启动时降权为包用户"。
19. **`components/fnos_monitor/fnos_ui.c` part3 条目（原 2 medium + 3 low，复核后全部降为 low，另有 1 条撤回）**：
    - `:3668-3669` `overview_refresh()` 卷扩容失败直接 `return` → 本拍半更新（容量数字已写、卷行与 `storage_note` 停旧值）。复核：`uk_realloc`（`ui_kit/uk.c:39-44`）走 `lv_realloc`，失败时**原块保留**且置 `s_alloc_failed=true` ⇒ 无 UAF，只是少了一拍更新（跳过 `:3583` 起的后续、卷段从 `:3675+`），下一拍自愈 → **low**。修法：`count = LV_MIN(count, capacity)` 后继续走完。
    - `:2615-2616` Wi-Fi 口令 `s_wifi_pw` 在 `wifi_close_cb`/`wifi_result_cb` ONLINE 分支后不清零、`s_wifi_reveal` 不复位 → 明文驻留 RAM。复核：`:2674` 成功后确有 `s_wifi_pw[0] = 0`（仅首字节），`:2610-2617` 关窗路径不擦；明文显示时 LVGL 标签另存堆副本；无日志引用该缓冲 ⇒ 需要物理接触/root 才能读 → **low**（机制成立，影响面小于原描述）。修法：close 时 `memset` + 复位 reveal。
    - `:864` 顶栏 Wi-Fi 按钮宽度用 `UK_FONT_CJK_16` 量（`:860 lv_text_get_size(&wifi_size,"Wi-Fi 未配置",UK_FONT_CJK_16,…)`）、标签却用 `UK_FONT_CJK_12` 渲染 → 宽出约 20px，从顶栏 hostcol（`:849-856`）抢宽，长主机名更早截断；同模式还在配对（`:875-876`）与设置（`:886`）按钮 → **low（恒触发、纯显示）**。修法：测量与渲染用同一字号。
    - `:4717-4719` `dump_card_map` 注释称"页号从祖先链读"，实际只比直接父对象（`lv_obj_get_parent(c) == s_ui.page[k]`）→ 挂在容器里的卡片全打 `p=-1`（`main_row :1281/:1292/:1305`、`s_ui.p3_cols :1471/:1483`、`grid :1881/:1908/:1915/:1959`），直接挂页的正常（`:1004/:1017/:1252/:1361/:1449/:1496/:1540/:1588/:1608/:1791/:1801/:1862/:2228`）；仅在 `PREVIEW_CARDMAP`（`preview.c:1195` → `:3527`）门控下调用 → **low（诊断专用）**。
    - **已撤回**：`:961-962` `chart_sample_count` 未判空（`r->chart = uk_trend_create(body, UI_HIST, 1)`（`:1039`）恒有 1 个序列）→ 移入 §4。
20. **`nas/fpk/nasscreencompanion/app/server/fnos_collector.py:382-383` mdstat `[m/n]` 的 have/want 赋值互换 —— CONFIRMED（降级数组显示反向数字）**：Linux mdstat 的 `[m/n]` 是"m 个工作成员 / 共 n 个"，而代码写 `cur["want"] = int(m.group(1)); cur["have"] = int(m.group(2))`。降级阵列 `[1/2] [U_]` 会产出 `have=2, want=1`，固件 `fnos_snapshot.c:176-177` 原样下发、`fnos_ui.c:4056` 打成 `"%s · %d/%d …"` → 屏上显示「2/1」。健康判定不受影响（`fnos_collector.py:131-132` 用 `have == want and "_" not in flags`，对称），所以只是数值撒谎。**修法**：交换两行赋值，并补一条 `[1/2] [U_]` 的解析用例。
21. **`nas/fpk/nasscreencompanion/cmd/main` 生命周期两条 —— CONFIRMED（复核后由 high 降 medium）**：
    - `:115` 在 `stop_process()` 里引用未定义的 `${port}`（该变量只在 `start_process`（`:19`，`local`）与 `status` 分支（`:139`）赋值），而 `_common.sh:10` 是 `set -u` → 走进这条路径（SIGKILL 后进程仍活：D 状态或权限）bash 立即以 "unbound variable" 中止，两条 `warn` 打不出来，且 `:116 as_package rm -f "$PID_FILE"`、`:117 cleanup_socket` 都不执行 → 残留 PID 文件与 socket，后续 `status/start` 判定失真。**修法**：函数内自行取端口或 `${port:-未知}`，并把清理动作置于消息之前（或用 `|| true` 保护）。
    - `:64-67` 就绪探测超时路径只 `fail + return 1`，不杀掉刚启动的进程、不删 PID 文件 → 服务活着占着端口，而应用中心显示"启动失败"，再点 start 会走 `is_running` 分支报"已在运行"并返回 0。**修法**：该路径执行与启动失败一致的清理（kill 本次 pid + 删 PID_FILE + cleanup_socket），或在文案中明确"进程仍在运行，请先 stop"。
22. **`nas/fpk/build.sh:27` 打包后唯一一道内容校验会假绿 —— CONFIRMED**：`JUNK=$(tar tzf "$FPK"; tar xzOf "$FPK" app.tgz | tar tz) || true`。两条命令用 `;` 连接，`|| true` 覆盖整条；`fnpack build` 之后若包损坏或布局变化（`app.tgz` 不存在），两条 tar 都失败 → `JUNK` 空 → `BAD` 空 → `:38` 照样打印「✓ 包内没有字节码缓存 / .DS_Store / ._ 资源分叉」并 `exit 0`。这是分发前唯一一道包内容检查（注释自称"这个检查就是为它准备的"）。**修法**：分开取退出码，非 0 或列表为空即硬失败，例如 `if ! JUNK=$(tar tzf "$FPK" 2>/dev/null); then echo "✗ 打不开包，无法校验" >&2; exit 1; fi`，内层 `tar xzOf … | tar tz` 用 `PIPESTATUS[0]` 判第一段。
23. **`nas/fpk/contract_check.py:387-390` "满载状态帧截断"检查已死但仍打印 ✓ —— CONFIRMED**：固件早已没有 `RX_BUF_SIZE`（现为 `components/fnos_monitor/fnos_data.c:56 HIST_BUF_SIZE (32*1024)`），`:388` 的正则永不命中 → `rx=0` → `:389/:391` 两个分支都不进；而 `main()` 在 `:320` 仍无条件打印「判定：板子读的每个字段，NAS 侧都在；容量上限也不丢数据 ✓」。注意这不是"整段被跳过"：`limits`（来自 `fnos_data.h` 的解析，`:351-354`）通常是有的，死的只是这一条检查 → NAS 侧上限调大后板子缓冲不够时门禁仍全绿。**修法**：改用现宏名（`HIST_BUF_SIZE`），或删掉该项并从 `:320` 的结论里排除它（§5「同名正则已静默空转」一句与本条合并）。
24. **`nas/fpk/nasscreencompanion/app/ui/www/index.html:363-366` `refresh()` 没有 `catch` —— CONFIRMED**：`async function refresh(){ … try{ … } finally { refreshing = false; } }`，`get('/api/state')` 在网络中断/服务重启时 reject → unhandled rejection；`$('hdot')` 停留在上一次状态、界面不显示任何错误，而 `setInterval` 每 2 s 再触发一次，控制台持续刷 rejection。`loadLogs()`、`gencode/cancelcode/rundiag` 与撤销按钮同样只有成功路径 → 点了没反应。**修法**：`catch(e){ $('hdot').className='dot err'; $('hhint').textContent='连接失败'; }`，操作类按钮在 catch 里给失败提示。
25. **`nas/fpk/nasscreencompanion/cmd/config_callback:9` 与运行中的服务并发改写 `config.json` —— CONFIRMED（窗口毫秒级，后果是丢配置）**：`apply_config`（`cmd/_common.sh:111-141`）是"读（`:118`）→ 合并（`:125-131`，特意保留 `devices`）→ 临时文件 + `os.replace`（`:133-137`）"的整文件重写；服务端 `nas_companion_server.py:407-424 save_config()` 也整文件重写同一路径（`os.replace` 在 `:424`），配对/令牌写回就走它 → 谁后写谁赢：服务在读到写之间完成配对，则新设备被旧副本覆盖；反之新的 `port/bind` 被旧副本覆盖。`config_callback:13` 才去停服务，所以窗口真实存在，而脚本仍打印「配置已更新（已配对设备保留）」。**修法**：先 `service_stop` 再写配置（或让 `apply_config` 经服务 API 提交），并在 `save_config` 侧加文件锁。
26. **`nas/fpk/nasscreencompanion/cmd/uninstall_callback:8` 无条件宣称已释放端口 —— CONFIRMED**：`:5 as_package rm -f "$PID_FILE"` 先删 PID 文件，`:6 cleanup_socket`，`:7` 直接 `say "已释放端口与 Socket。…"`，全程没有停服务或确认进程已退出（`uninstall_init` 的 `service_stop` 失败只 `warn`）。进程若仍在（SIGKILL 未生效、D 状态、权限），端口继续被占但 PID 文件已不存在 → `is_running` 永远为假，重装/重启报「端口被占用」且无迹可查。**修法**：先停并确认（含 `kill -0` 复核），失败就保留 PID 文件，文案改成"进程仍在运行（pid …）"。
27. **`nas/fpk/nasscreencompanion/wizard/config:16` 端口校验过宽 —— CONFIRMED（low）**：`{ "pattern": "^[0-9]{2,5}$" }` 接受 `00`/`00000`/`99999`（>65535），却拒绝 `80` 这类 1 位合法端口；向导通过后 `apply_config` 原样写进 `config.json`，要到服务启动绑定失败才暴露。**修法**：pattern 换成 `^([1-9][0-9]{0,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5])$`，或在 `apply_config` 里补 `1 <= int(port) <= 65535` 校验。
28. **`tools/preview/palettes.sh:14-15` 未校验的 `KK_PALETTES` 直接进 `rm -rf` —— CONFIRMED（low，主机侧开发工具）**：`PALETTES="${KK_PALETTES:-graphite abyss phosphor}"`（`:11`）→ `for p in $PALETTES; do rm -rf "$OUT/$p"`；未加引号的展开既做词分割又做 glob（`KK_PALETTES='*'` 会删掉 `palette-out/` 下所有目录），`KK_PALETTES='../../components'` 之类可越出 `palette-out/`。**修法**：`case "$p" in ''|*[!A-Za-z0-9_-]*) echo "非法配色名：$p" >&2; exit 2;; esac` + `rm -rf "${OUT:?}/${p}"`，或改数组逐项校验并 `set -f`。
29. **`tools/verify_all.sh:54` 几何审计的 grep 与实际输出对不上（并暴露 `PREVIEW_AUDIT_ALL` 计数是死变量）—— CONFIRMED（复核修正：不是"漏成 PASS"，而是"错报成字形问题"+ 探针模式静默）**：模式是 `grep -qiE "跑出父对象|压住|overlap|溢出|missing glyph"`，而 `preview.c` 实际打印的是英文 `child out of parent:`（`:607`）、`inline metric overflow %d > %d`（`:860`）、`text overflow …`（`:881`）、`missing glyph U+%04X in %s`（`:873`）、`text overlap: …`（`:978`）——中文词只存在于注释里，`overflow` 也不等于 `overlap`，因此 `child out of parent`、`inline metric overflow`、`text overflow` 这三类**永远匹配不到**（对照 `tools/preview/palettes.sh:21` 对同一份日志用的就是正确的英文模式）。**但"越界会漏成 PASS"这条不成立**：这些违规在 `preview.c` 里默认直接 `abort()`（`:612`/`:668`/`:860`/`:873`/`:881`/`:978`），preview 退出码非 0 → `verify_all.sh:46` 会走 `else` 分支并**误报**成「渲染失败（多为字形缺口，跑 `bash tools/gen_tools.sh`…）」——真问题是**归因错误**，不是放行。真正会静默的是文档化的探针模式 `PREVIEW_AUDIT_ALL=1`（`docs/ui-refactoring-ui-2026-10-08.md:215` 记录用过）：该模式下违规只做 `s_audit_fail++`（`:612`/`:668`），而 `s_audit_fail` **全文件只被写、从不被读**（`grep -n s_audit_fail` 只有 `:529/:612/:668`）→ preview 退出 0、不打印汇总，此时 `verify_all.sh:54` 的 grep 又匹配不到那三类文本 → 真的会显示「几何审计（无越界/压叠/字形缺口）」并 PASS。**修法**：①模式改成与 `preview.c` 一致（`child out of parent|inline metric overflow|text overflow|missing glyph|text overlap|list too short`）；②`verify_all.sh:49` 的失败文案按日志内容分流（越界/溢出 vs 字形）；③`preview.c` 在 `PREVIEW_AUDIT_ALL` 模式下末尾打印违规计数并以非 0 退出。
30. **`tools/verify_all.sh:77` 把"栈深超预算"误诊成"环境没激活" —— CONFIRMED**：`if python3 tools/stack_check.py >"$OUT/stack.log" 2>&1; then … else bad "栈深检查跑不起来（先激活 IDF 环境或设置 FNOS_IDF_ENV）"; tail -5`。而 `tools/stack_check.py:187-190` 在超预算时打印「N 个入口超出预算：…」并 `return 1`（正常时打印「全部入口在预算内」`return 0`，见 `:191-193`）→ 任一入口超预算都会落进 `else`，把维护者引去折腾 IDF 环境；`then` 分支里那个 `bad "栈深超预算："` 反而几乎不可达（要求退出码 0 且输出里没有"全部入口在预算内"）。主任务栈只有 16384 B（硬约束），这条误诊会直接耽误真问题。**修法**：先按日志内容判定（`grep -q "超出预算" "$OUT/stack.log"` → `bad "栈深超预算"` + 打印该行），再兜底报环境问题。
31. **`nas/fnos-agent.py:812-814` + `:917`（含 `nas/fnos-agent.service:12`、`nas/install.sh`）默认安装下 8799 无认证对外 —— CONFIRMED**：`tok = getattr(self.server, "token", "")`，`if not tok: return True`（空 token = 鉴权关闭，fail-open）；`srv.token = os.environ.get("FNAS_TOKEN", "")`，而随包发布的 unit 是 `--bind 0.0.0.0 --port 8799` 且没有 `Environment=`，`nas/install.sh` 全仓零 `FNAS_TOKEN` 引用 → **默认安装就是"局域网任意主机可读"**：`GET /api/v1/status`、`/api/v1/history` 会给出挂载路径、卷名/容量、容器名与镜像、网卡 MAC/IP、温度、主机名与 300 条历史（只读，无 `do_POST` → 501；隔壁 FPK 伴侣默认是配对 + sha256，口径不一致）。**修法**：`install.sh` 生成随机 token 写进 0600 的 `EnvironmentFile=`；`main()` 在 token 为空时拒绝启动，除非显式 `--insecure-no-auth`（fail closed）。
32. **`nas/fnos-agent.py:833-839` 忽略 `?n=` → 历史回填静默永久失败 —— CONFIRMED**：`hist = list(col.history)` 返回整条缓冲，`--hist`/`FNAS_HIST`（`:871`）没有上界；而开发板恒定请求 `?n=600`（`components/fnos_monitor/fnos_data.c:205`），agent 完全不看这个参数。行 ≈ 35 B + 55 B 头，一旦缓冲 > ~930 行，响应体就超过开发板的 `HIST_BUF_SIZE (32*1024)`（`fnos_data.c:56`）→ 被截断 → cJSON 解析失败 → 回填永远不成功、每秒重试、界面无错误提示（曲线一直空白）。同端点的 FPK 伴侣则正确按 `n` 截断（`nas_companion_server.py:799-803 rows = rows[-want:]`）。**修法**：照伴侣实现，`want = int(n or 0) or len(hist); rows = hist[-min(want, len(hist)):]`。
33. **`nas_companion_server.py:624-634` `_read_json` 把坏请求体当 `{}` → 两处"报成功但没做" —— CONFIRMED**：请求体缺失/超过 64 KB/chunked/非 JSON 对象时返回 `{}`。`/api/config`（`:884-892`）于是 `_apply_config({})` → 回 `{"ok":true,"changed":[]}` 且管理页打印"已保存"，实际一条都没改；`/api/collector/docker`（`:917-925`）`on = bool({}.get("enabled"))` = False → **把容器采集静默关掉并持久化**，同样回 ok。管理面（AF_UNIX + 网关头）门控，属健壮性而非鉴权绕过，但"失败报成功"会让人查错方向。**修法**：Content-Length 缺失/超限/体不是 JSON 对象时一律 400；docker 端点要求 `enabled` 必须存在。
34. **`nas_companion_server.py:1248-1250` 只捕 `KeyboardInterrupt` → `finally: state.flush()` 在每次正常停止时都不执行 —— CONFIRMED**：`cmd/main:99` 用 `as_package kill -TERM "$pid"` 停服务，SIGTERM 的默认动作直接终止解释器、不走 `finally`（`:1189-1266`）→ `state.flush()`（`:542`）从不落盘，`last_ip`/`_dirty`（"退出时统一落盘"）每次升级/重启都丢；顺带留下陈旧的 af_unix socket 文件（下次启动 `:1126` 会 unlink，无害）。**修法**：进入等待循环前 `signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))`（SystemExit 仍会执行 `finally`）。
35. **`tools/preview/hardware_fixtures.py:26` 无法表达"采集端省略" → 唯一一条运行期 `%d` 警告带从未被任何审计面渲染 —— CONFIRMED**：`"trunc":{"limits":{},"dropped":{}}` 是硬编码空字典，`tools/preview/preview.c` 全文没有 `"trunc"`/`source_dropped`，也没有任何 preview 状态去设置它 → `components/fnos_monitor/fnos_snapshot.c:210-214` 的 `source_dropped` 恒 0；而 `components/fnos_monitor/fnos_ui.c:3897` 的 `set_txt(s_ui.foot_txt,"采集端省略 %d 项 · 请检查采集限制",st->source_dropped)` 是全工程**唯一一条运行期 `%d` 提示**（非 KPI），它在字形/标签溢出/格式残留/点压四个审计面上**一次都没被渲染过**。AGENTS.md 把"硬件清单不得裁剪、超预算必须显式提示"定为硬约束，但"密集清单 + 采集端丢弃 N 项"这一组合在夹具里根本不可表达。**修法**：给夹具加 `dropped` 旋钮并在 `mixed`/`dense` 用上，例如 `"trunc":{"limits":{"vols":24},"dropped":{"vols":2,"disks":19}}`，另加一个 `source_dropped>0` 的状态跑门。
36. **`tools/preview/run.sh:62`（配合 `:8`）不清理环境也不断言帧数 → 门可能只跑了个子集仍退 0 —— CONFIRMED**：`tools/preview/preview.c:3520/3522/3526/3535` 在 `PREVIEW_MOTION`/`PREVIEW_NUMBERS`/`PREVIEW_CARDMAP`/`PREVIEW_PAIR_FIRST_ENTRY` 任一被设置时渲染**子集**后 `return 0`；`run.sh` 既不 `env -u` 也不断言产物数量。于是调用方 shell 里恰好残留任一 `PREVIEW_*`（例如 `PREVIEW_MOTION=0,1,0 bash tools/preview/run.sh`）时，这条"一条命令的交付前门"只渲 7 或 ~30 帧就 `exit 0`，六页的图像/溢出/残留/点压四项审计**一个都没跑**，且没有任何输出说明（`:2` 还自称"4 页 × 3 状态"）。**修法**：`env -u PREVIEW_MOTION -u PREVIEW_NUMBERS -u PREVIEW_CARDMAP -u PREVIEW_PAIR_FIRST_ENTRY -u PREVIEW_DATA_MOTION ./build/preview "$OUT"`，并在 `:63` 之前断言 `[ "$(find "$OUT" -name '*.ppm' | wc -l)" -ge 84 ] || { echo "✗ 帧数不足：审计没跑全"; exit 1; }`。
37. **`tools/preview/pair_msgs.py:36-38` 只挡"整体抠取失败"，少一个状态会静默缩小审计面 —— CONFIRMED**：唯一守卫是 `if not rows`（全都没抠到）。若某个状态的文案不再匹配 `set_view(FNOS_PAIR_X, "字面量")`——例如移进表/数组、先格式化进缓冲——抽取会**静默少一行**，`PAIR_MSG_COUNT` 随之变小，而 `preview.c:2276-2292` 对 `PAIR_MSGS[PAIR_MSG_COUNT-1].msg` 的断言**照样通过** → 配对卡审计在一个子集上变绿，正是这个工具存在的意义（其 docstring `:4-7`：7 条 FAILED 文案"从来没被渲染过"）。**修法**：`:35` 之后加 `missing = sorted(set(STATES) - {s for s, _ in rows}); if missing: print(f"✗ 这些状态一条都没抠到：{missing}"); return 1`。
38. **`nas/fpk/page_check.js:328` 概览断言是整块 innerHTML 子串搜索 → 值全错也能过 —— CONFIRMED**：needle `"2.2"` 命中的是进度条内联 `style="width:2.2%"` 而不是卡片文本，把页面所有 CPU 数字改成 `0.0`（渲染 `0.0 %`）实测仍 `exit 0`。**修法**：按每个 `.card` 切片后再断言"标签 + 数值"。
39. **`nas/fpk/page_check.js:230-244` 点击表硬编码 5 个 id → 新控件永远不会被点到 —— CONFIRMED**：垫片的 `querySelectorAll('[data-revoke]')` 返回 `[]`，`index.html:324-327` 的设备"撤销"处理器从未被执行，检查却全绿。**修法**：从 HTML 扫出所有 `$('x').onclick=` 目标（照同文件 `:586` 的机械对账）。
40. **`nas/fpk/api_check.py:121-123` 未登记端点直接 `continue`，仍打印"字段名服务端也都认 ✓" —— CONFIRMED（当前是潜在）**：`if ok is None: continue` 使请求体字段名**一条都不核**；当前 5 条路径恰好与 `accepted_keys()` 全覆盖，所以今天不触发（用假键复现：`/tmp/expA` → `exit 0`）。下次服务端加 POST 端点即无声逃检，而本脚本的唯一职责就是核字段名。**修法**：未登记端点显式报"未覆盖"并非 0。
41. **`nas/fpk/board_sim.py:156,234` 服务端返回非 JSON 时裸 traceback 崩掉整个脚本 —— CONFIRMED**：`identity`/`status` 返回 500 HTML（或反代页面、应用崩）时 `json.loads` 抛 `JSONDecodeError: Expecting value: line 1 column 1 (char 0)`，④⑤⑥ 三段全不跑、无诊断（`exit 1`，**不是假绿**）。**修法**：`json.loads` 包 try，报"不是 JSON：<前 80 字节>"。
42. **`nas/fpk/wizard_check.py:159-161` 无法求值的规则被当成"满足" —— CONFIRMED**：`except (TypeError, ValueError): return True` → 规则写坏（如 `{"min":"abc"}`）时判通过，脚本仍 `exit 0` 且打印"校验规则…都对得上 ✓"（`/tmp/expD2` 复现）。另 `:151-156` 对"数值 min/max + 字符串 initValue"用 `len(str(value))` 当长度比 → 误报「initValue='8798' 通不过这条规则」（`/tmp/expD` 复现 `exit 1`，低危）。**修法**：无法求值的规则判 FAIL 并报出该规则；数值比较按数值走，类型不符显式报错。
43. **`nas/fpk/nasscreencompanion/cmd/config_init:8-16` 端口预检在"我们不拥有该端口"时也判正常 —— CONFIRMED**：`port_busy` 为真时用 `is_running`（`cmd/_common.sh:91-103`）判断"是不是我们的服务在跑"，但 `is_running` 只证明**我们自己的进程活着**，从不检查该进程是否**持有被探测的那个端口**；于是"服务跑在 8080、用户提交已被别的进程占用的 8081"这一组合会走「正常」分支并 `rc=0`。而此时 `config_callback:19` 已经停掉旧实例、`apply_config` 把占用端口写进 `config.json`，`cmd/main:22-25` 随后拒绝启动 → 用户看到「配置已保存，但服务重启失败」：**应用下线且配置指向一个永远绑不上的端口**。**修法**：要求"拥有权"——`is_running && [ "$W_PORT" = "$(effective_port "$(resolve_python)")" ]`，否则失败；`effective_port`（`cmd/_common.sh:184`）已存在、`main` 在用，但这里从没用过。
44. **`nas/fpk/nasscreencompanion/cmd/_common.sh:173-174`（配合 `wizard/install:65`，消费端 `app/server/fnos_collector.py:200-202`）`interval=0` 未被拦，采集循环空转烧一个核 —— CONFIRMED**：字符过滤 `''|*[!0-9.]*` 只允许数字与点，`0` 通过；向导 pattern `^[0-9]+(\.[0-9]+)?$` 也接受 `0` → `apply_config` 写 `"interval": 0.0`；`load_config`（`nas_companion_server.py:370-407`）不做范围校验 → `CompanionCollector(interval=0.0)`（`:1276`）→ `Collector._loop` 的 `dt = self.interval - elapsed; if dt > 0: wait` **永不等待** → 采样循环死转 `/proc`、`/sys`，顶满一个核。管理 API 自己是有界的（`nas_companion_server.py:946` 强制 0.2–60），这条路径绕过了应用自己的策略。附带：`1.2.3` 能过同一个过滤但在 `float(interval)` 处炸，`config_callback:10` 会把它误报成「写权限」问题。**修法**：`norm_wizard` 里拒绝第二个小数点并做 0.2–60 范围校验（bash `case` + `awk`），与 API 同界。
45. **`nas/install.sh:53,56,68,74` 远端 `$STAGE` 未加引号即进入 root 的 `rm -rf`/`mv` —— CONFIRMED（PARTIAL，low-medium）**：`STAGE` 来自 ssh 命令的标准输出，唯一守卫是 `[ -n "$STAGE" ]`（`:54`）；登录横幅/rc 噪声会让它变成多行或非路径，而 `run_root "rm -rf $STAGE"`（`:68/:74`）与 `mv $STAGE/...`（`:56`）都是未加引号的插值 → 多余行会变成 **root** `rm -rf` 的**额外操作数**，路径含空格也会被拆开。**修法**：先校验形状（`case "$STAGE" in /tmp/tmp.*) ;; *) exit 1;; esac`）并把每处都写成 `rm -rf -- "$STAGE"`。
46. **`nas/fpk/test_lifecycle.sh:25`（跳过 `:52`、`:356`）断言下限与跳过路径不符 → 假红 —— CONFIRMED（low-medium）**：在 fnpack 缺席的环境（**文档记载的正常情况**：`docs/fnos-companion-app-plan.md:48` 装到 `$HOME/.local/bin`，而套件从不把它加进 `PATH`，只有 `nas/fpk/build.sh:14` 加了）跑，143 条断言（`docs/fnos-companion-test-report.md:35`）减去 `[ -n "$FPK" ]` 块里的 10 条 = 133 < `EXPECTED_MIN=142` → 抛「✗ 只跑了 133 条断言…」`FAIL=1`、`exit 1`，`tools/test_report.sh:36-45` 据此记一套红；没有 node 时再少 2 条（`:391/:392`）= 141 < 142，同样假红。而且这条路径上整包基线（缺文件、`__pycache__`、`.DS_Store`、密钥扫描）**一条也不跑**。**修法**：让下限随跳过感知（`[ -z "$FPK" ] && EXPECTED_MIN=$((EXPECTED_MIN-10))`；`command -v node >/dev/null || EXPECTED_MIN=$((EXPECTED_MIN-2))`），或像 `build.sh` 那样导出 `PATH`。
47. **`tools/verify_all.sh:51` `ppm2png.py` 失败时既不记 pass 也不记 fail —— CONFIRMED**：`python3 tools/preview/ppm2png.py "$OUT/png" >"$OUT/ppm2png.log" 2>&1 && pass "PPM→PNG"` 只有 `&&` 分支 → 转换失败时报告里既没有这一行 PASS 也没有 FAIL，整条门仍然全绿，PNG 证据缺失却看不出来。**修法**：改成 `if …; then pass …; else bad "PPM→PNG 转换失败（见 $OUT/ppm2png.log）"; fi`。
48. **`tools/plan_check.py:97,105,117,129-131` 计划/报告对账在四种情况下静默消失 —— CONFIRMED**：`:97` ROOTS 是相对路径而脚本已定义 `REPO` → 非仓库根目录运行时每个反引号路径都误判失败（或匹配到不相干的树，修法 `os.path.join(REPO, r, p)`）；`:105/:117` `board_fields.json`/`.fpk` 不存在时字段数与包体积检查直接消失（`if os.path.exists` 无 `else`）；`:129-131` 报告文件缺失抛 traceback 被上层吞成"没这条"，报告措辞漂移时**断言条数核对与唯一的"套件失败交叉验证"**（`if failed: problems.append`）双双静默，`rc=0`。**修法**：每处不存在/不匹配都 `problems.append`。
49. **`tools/freshness_check.py:75-80` 构建产物陈旧检查的输入白名单漏掉构建文件 —— CONFIRMED**：允许的后缀只有 `.c/.h/.sh/.py` 加 `manifest/config/privilege/resource`；实测被它当"看不见"的构建输入有 `main/CMakeLists.txt`、`main/idf_component.yml`、`components/fnos_monitor/{CMakeLists.txt,Kconfig,idf_component.yml}`、`components/lvgl_mem_psram/CMakeLists.txt`、`components/esp32_p4_wifi6_touch_lcd_7b/{CMakeLists.txt,Kconfig,idf_component.yml}` → 改了这些之后 `build/fnos_monitor.bin` 仍报"不比源码旧"。**修法**：白名单补 `*.txt`、`*.yml/.yaml`、`Kconfig`、`*.csv`、`*.ld`，或改为显式列出构建输入。
50. **`tools/soak_check.py:70-71`（配合 `:87-90,:100-108`）staged 副本缺失时"跑的是不是当前代码"检查静默失效 —— CONFIRMED（low-medium）**：`if not os.path.exists(sp) or not os.path.exists(src): continue` → `/tmp` 被清、`--stage` 指错、目录结构变化时该检查直接跳过，脚本最后仍打印「长测在跑、跑的是当前代码、两面都健康 ✓」并退 0——正是这个文件当初被写出来要抓的"第 18 轮"失效；同理 fd 采样 <3 条、rss 采样 <10 条时句柄/内存检查静默 no-op，汇总仍报健康。**修法**：staged 文件缺失记 problem，采样不足打印"窗口不足，未判定"。
51. **`tools/test_report.sh:119-124`（配合 `:91` 与 `:246`）强制门被跳过只打控制台，且对账用的是上一次的报告 —— CONFIRMED（low-medium）**：`:119-124` 在 `$HOME/.platformio/…/riscv32-esp-elf-objdump` 不存在时**只打印一行提示**就跳过 AGENTS.md 要求的静态栈预算门（报告里没有这一行、不置 `HARD_FAIL`）→ 一份看起来完整的报告里有一道门从未运行，且这正是 H18 那条"objdump 读不了"的入口；另外 `:91` 的 `plan_check.py` 在 `:246` 重写报告**之前**运行 → 每次核对的都是上一轮的 `docs/plan.md`/报告（滞后一轮，也让 H18/H19 类问题晚一轮才被发现）。**修法**：跳过时写一行 `静态栈预算 | 跳过` 并（非交互时）失败；把 `plan_check` 移到报告重写之后。
52. **`tools/perf_table.py:48-49`（配合 `:32`）缺列/缺参时抛异常而非按"无数据"处理 —— CONFIRMED（low-medium）**：`f"{med(d.get(case, []), 'iv_n'):.0f}"` 在两个 CSV 的 `--cases` 不一致或缺 `iv_n` 列时抛 `TypeError: unsupported format string passed to NoneType.__format__`（上面 METRICS 循环对 `None` 是打 `--` 的，这一行漏了）；无参数调用时 `files[0]` 抛 `IndexError`。**修法**：复用 `--` 守卫并校验 argv。
53. **`nas/fpk/nasscreencompanion/cmd/install_callback:14-19`（配合 `cmd/_common.sh:174`）表单校验失败被误诊成"写权限" —— CONFIRMED**：向导填 `interval=1.2.3` 时 `norm_wizard` 的 `*[!0-9.]*` 过滤放行（只挡非数字非点字符），随后 `float()` 抛 `ValueError`，`install_callback` 却把失败原因报成「请检查写权限」——**根因与提示相反**，用户会去改一个无关的权限问题（与 §3.44 的 `interval=0` 是同一处过滤器的两个出口）。**修法**：校验失败时把 `apply_config` 的真实输出透出，并在 `norm_wizard` 里做形状（单个小数点）+ 范围（0.2–60）校验。
54. **`nas/fpk/nasscreencompanion/cmd/install_init:19-21`（配合 `cmd/_common.sh:147-150`、`wizard/install:14`）向导端口 8799 被放行，随后顶掉独立采集器 —— CONFIRMED（实测退 0）**：向导只把 8799 当提示文案（`wizard/install:14`「请勿填 8799」），`norm_wizard:147-150` 只校验 1..65535，`install_init` 打印「计划监听 0.0.0.0:8799」+「检查通过」并退 0；若独立采集器（`nas/fnos-agent.service:12 --bind 0.0.0.0 --port 8799 --port 8799`）未在跑就没人拦，伴侣应用随后绑定该端口，采集器起来时 `Restart=always` 反复 crash-loop，项目自己的规则（`cmd/_common.sh:7`「不触碰 8799」）被绕过。**修法**：`install_init`/`config_init` 显式拒绝 `W_PORT == DEV_AGENT_PORT`，或在 `norm_wizard` 里钳掉。
55. **`nas/fpk/devmode_check.py:66-67` `install_callback` 失败时丢弃 returncode/stderr → 装失败被误诊成"服务端中途退出" —— CONFIRMED**：子进程的 `returncode` 与捕获到的 `stderr` 全丢，只按"服务没起来"推断原因，维护者会去查服务端日志而不是安装输出。**修法**：非 0 时打印捕获的 stderr 与退出码。
56. **`nas/fpk/perm_check.py:76-77` 断言与 `:69` 完全重复，标称验证的"目录"场景从未跑过 —— CONFIRMED（实测 `classify_read(真目录)=error`，IsADirectoryError）**：该行传的是**普通文件**（与 `:69` 同一用例），却挂着"listdir 应当成功"的意图；真正的目录用例会因 `IsADirectoryError` 失败，所以这条覆盖率是虚的。**修法**：删掉重复行，改成真目录用例或按函数契约显式断言。

57. **`tools/preview/run.sh:13,61` 输出目录来自位置参数却被直接 `rm -rf` —— CONFIRMED（数据丢失）**：`OUT=${1:-out}`（`:13`）后先 `cd "$(dirname "$0")"`（`:8`），`:61` 是 `rm -rf "$OUT"`，全程不校验取值。因此 `bash tools/preview/run.sh ..` 会删掉整个 `tools/`，`bash tools/preview/run.sh ../..` 会删掉仓库根——而这正是 README/文件头（`:4`「用法：`tools/preview/run.sh [输出目录]`」）鼓励的调用方式。与 §3.28 的 `tools/preview/palettes.sh:14` 属同一类缺守卫。**修法**：拒绝绝对路径与含 `..` 的取值（`case "$OUT" in /*|*..*) die;; esac`），或改成脚本自己在 `mktemp -d` 下建子目录、只允许删自己建的那个。
58. **`nas/fnos-agent.py:70-78,253` `fstype` 精确匹配漏掉未列名的 FUSE 子类型 —— CONFIRMED**：`SKIP_FSTYPES` 显式列了 `"fuse"`、`"fuseblk"`、`"fuse.sshfs"`、`"fuse.mergerfs"`、`"fuse.rclone"`，而判定是 `if fstype in SKIP_FSTYPES`（`:253`）→ **`fuse.davfs2`、`fuse.gcsfuse`、`fuse.s3fs`、`fuse.cryfs`、`fuse.bindfs`、`fuse.encfs` 等一律漏过**，直接走到 `os.statvfs(mnt)`（`:260`）。而 `:75` 的注释正是为这类文件系统写的：「网络/融合文件系统：对端一挂，`statvfs` 会进不可中断的 D 态，采样线程再也回不来」——即实现没有兜住自己声明的风险（NAS 上 WebDAV/对象存储/加密盘挂载很常见）。**修法**：改成前缀判定 `fstype == "fuse" or fstype.startswith("fuse.") or fstype in SKIP_FSTYPES`（或把 `statvfs` 挪到带超时的子进程里）。
59. **`tools/release_check.sh:20-24,44-46` 真实配置挪不动时，发布前的"秘密扫描"会打绿灯 —— CONFIRMED（假绿）**：脚本只有 `set -u`（`:15`），`mv -f "$CFG" "$STASH"`（`:24`）的失败无人检查；而内嵌 Python 对"没有真实配置可对照"的处理是 `print('（本机没有真实配置可对照，只报大小）', …); raise SystemExit(0)`（`:44-46`）→ 于是当 `mv` 因目录只读、文件带 `chflags uchg`（把真实配置钉住是常见做法，AGENTS.md 也要求它留本地）等原因失败时：真实配置仍留在 `$CFG`、`build-release/` **确实编进了真实 Wi-Fi 口令/令牌/NAS 地址**，而这道专门用来抓它们的门**打印一句安抚话后退 0**。**修法**：`mv` 后校验 `[ -f "$STASH" ]` 否则 `exit 1`；把"没有真实配置"与"挪不动配置"区分开，后者必须失败。
60. **`nas/fpk/mem_check.py:53` 默认鉴权配置下有一半请求根本没进被测代码 —— CONFIRMED（实测 401）**：脚本用 `cfg.update(port=0, bind="127.0.0.1", tls_enabled=False)`，未改 `auth_mode`（`nas_companion_server.py:85` 默认 `"pairing"`）且未登记任何设备；按同样方式实跑（端口 0、无 TLS、空 devices）四个 `PATHS` 的响应是 `/api/v1/status → 401`、`/api/v1/health → 200`、`/api/v1/identity → 200`、`/api/v1/history → 401`（原文「未配对：请在 NAS 管理页生成配对码…」）→ 3000 次请求里有一半在鉴权层就被打回，**遥测载荷构造与历史查询这两条最可能泄漏的路径从未被压到**，却照样给出"内存不涨 ✓"的结论。**修法**：给 `state` 预置一个已配对设备（或把 `auth_mode` 设为 `lan`）并断言四个端点都回 200；与 H17（样本不足仍退 0）一起修，才是可信的取证。


---

## 4. 撤回与降级（REFUTED / PARTIAL，不要按原报告修）

这些条目曾以 high/critical 出现在扫描器输出里，复核判定**不可达、已被上游防护或误解上游语义**，按原样修改反而会引入 bug：

- `components/lvgl_mem_psram/lvgl_mem_psram.c:56-57` 原 **critical**「`realloc(p,0)` 后二次 realloc → double free」：LVGL 调用契约下 `new_size == 0` 不可达 → 降 low（真问题只是"primary 失败后换 caps 再 realloc 同一指针"的语义模糊 + `lv_mem_monitor_core` 只统计 SPIRAM、`free_cnt` 恒 1）。
- `fnos_pair.c:347-351`「`*out_len` 未初始化即读是 UB」：`http_oneshot` 的 5 个调用点（`fnos_pair.c:243/:275/:288/:548/:671`）**全部**先 `int status = 0, len = 0;` → 无 UB，只剩"错误出口契约不一致"（low）。
- `fnos_pair.c:216` `fnos_pair_last_http_diag` 零调用者属实，但 `s_http_diag` 本身被 `:252/:255/:281/:300/:566/:694/:718` 使用；真实风险只是 `pair_task` 与 `probe_task` 并发写同一缓冲（诊断文本撕裂，low）。
- `fnos_pair.c:885-887`/`:898-900` 取锁失败回退无锁整拷贝：`portMAX_DELAY` 实际不会失败 → 可达性极低（契约加固，low）。
- `fnos_pair.c:813-815` FETCHING 期间 `confirm(true)` 越过指纹核对：`fnos_ui.c:2755-2756` 把 `FNOS_PAIR_FETCHING` 映射成 `PST_BUSY`、`:2889-2890` 仅 `PST_CONFIRM` 才显示"接受并配对" → UI 挡住（残余：派发前不校验 `s_view.state`，纵深防御建议补一句）。
- `fnos_pair.h:24` `PEM_MAX` 装不下 4096 位证书：`fnos_pair.c:613-617` 已显式失败"证书太长，装不下"。
- `fnos_snapshot.c:137-138`（cJSON 已判 NULL：`cJSON.c:1131-1143`）、`:65-66`（引用计数：非测试调用点都在锁内或私有句柄）、`:25-26`（无 SPIRAM 的板子上 total/free 为 0，但不崩）、`:132-133`（排序非全序，仅行序抖动）、`:200`（格式串与实参数量对得上，未违反硬约束⑤）。
- `fnos_data.c:490-491`（`cJSON_InitHooks` 全仓唯一）、`:541-544`（该 API 除 `tools/preview/preview.c:501` 宿主桩外无调用者）、`:454-455`、`:164`。
- `fnos_perf.c:486`（`strtok` 唯一调用点单实例顺序执行）、`:43-45`（profiler 跨任务调用，但 `CONFIG_LV_USE_PROFILER=n` → 出厂不编译）、`:144-145`/`:328-330`/`:427`（只在 `CONFIG_FNOS_UI_PERF_BENCH=y` 的台架固件生效）。
- `fnos_wifi_store.c:56-57` 静默截断：内部副本 `:39-40 char s[64]/char p[96]` + 调用方全 64/96 → 不可达。
- `fnos_net.c:204/:271` `threshold.authmode=WPA2_PSK` 不构成"开放 AP 连不上"（该阈值只作用于连接期扫描过滤）。
- `ui_kit`：`uk_theme.c:76-77`（唯一调用点在 UI 创建之前）、`uk.c:896`（死 API，全仓无调用者）、`uk.c:504`、`uk.c:1009-1012`、`uk.c:684-685`（LVGL 支持负索引且越界返回 NULL：`lv_obj_tree.c:335-349`）、`uk.h:127`（分母恒 ≥1）、`uk.h:47-48`（`lv_display_get_vertical_resolution(NULL)` 走默认显示，不解引用）。
- `uk.h:154-155` `LV_OBJ_FLAG_USER_1` 三义：消费者不会误判（`preview.c:625` 遇第一个 USER_1 即停；`uk_list_promise` 只在 `uk_list_is(UK_LIST_FLAG)` 为真时调用）→ 可维护性。
- `tools/parsecheck/main.c:194`（值域 `[0,max]`，`<0` 是死代码）、`tools/preview/stub/esp_log.h:9`（`##__VA_ARGS__`，`-std=gnu11` 无 `-Wpedantic`）、`:10`（`"E %s: " fmt` 拼接反而更严）。
- `tools/preview/preview.c:694` `MAX_TAPPABLE(256)` 静默停收：复核实测最大并发可点控件 **57**（余量 4.5×）→ 不可达。
- `tools/preview/preview.c:516-519` `flush_cb` 不校验 area：`s_frame[SCR_MAX_W*SCR_MAX_H]`（`:508-511`）+ 入口 `:3490-3495` 钳 `w<=1280/h<=800`，display 按 `s_scr_w/h` 建（`:3497`）⇒ 越界不可达；`PREVIEW_PARTIAL` 为 opt-in（`:3500-3502`），默认 FULL 下像素断言齐全。
- `tools/preview/preview.c:1271-1273` `state_name()` 漏 `ST_LOCKBUSY`：唯一调用点 `:1313/:1320` 在 `run_state`，7 次调用（`:3552-3555/:3595/:3599`）都不会遇到该状态。
- `components/fnos_monitor/fnos_ui.c:961-962` `chart_sample_count` 未判空：`r->chart = uk_trend_create(body, UI_HIST, 1)`（`:1039`）保证恒有 1 个序列。
- 厂商 BSP 两条原 high（`esp32_p4_wifi6_touch_lcd_7b.c:233-245` i2s 句柄失败后不置 NULL 导致重入误判；`:401-410` 未初始化 `bsp_lcd_handles_t handles` 在失败路径被写出参）：全仓**无** `bsp_audio_init`/`bsp_display_new` 固件调用者 → latent，降 low（但若将来启用音频/BSP 显示初始化需先修）。
- 厂商 BSP 另外三条原 high，同属 **latent（无固件调用者）**：①`components/esp32_p4_wifi6_touch_lcd_7b/Kconfig:66-68` `BSP_SPIFFS_PARTITION_LABEL default "storage"`，而 `partitions.csv` 里没有 SPIFFS 分区 → 一旦调用 `bsp_spiffs_mount()` 就是 `ESP_ERR_NOT_FOUND` 并被 `BSP_ERROR_CHECK` 升级为 abort；②`include/bsp/display.h:34-35` `BSP_LCD_BITS_PER_PIXEL (16)` 与 RGB888 模式下实现里的 `bits_per_pixel=24`（`esp32_p4_wifi6_touch_lcd_7b.c:463-468`）矛盾（配置门控、无消费者）；③`priv_include/bsp_err_check.h:21-22` `BSP_NULL_CHECK(x,ret)` 用 `assert(x)` → NDEBUG 构建下检查与实参副作用一起消失，且未显式 `#include <assert.h>`（IDF 是否定义 NDEBUG 未在本仓验证）→ 契约问题。这三条都应在"启用 BSP 音频/显示/SPIFFS 之前"顺手修掉，不属于本次必修。
- `docs/verification.md` §13.3/§17 提到的 `ui_gen.py`/`audit_fonts.py`/`ui/`：那两节本身在记录**删除动作**，不算"把已删模块当现存"。
- **两套 NAS 服务端经复核后判定"不是缺陷"的项（不要按扫描器/直觉去改）**：历史时间戳单位（两边都发秒级 `ts`，伴侣多发的 `ts_ms` 被忽略，落在固件 `1e9..4e9` 窗口内）；路径穿越/任意文件读（全是固定路径、没有文件服务）；`shell=True`/`os.system`/`eval`/`pickle`（两个服务端与 `app/server/*.py` 里都没有，openssl 走 list 形式 `subprocess.run`）；令牌比较（`hmac.compare_digest`，`fnos-agent.py:815-819`、`nas_companion_server.py:529-531`）；坏 JSON → 500（不会，`_read_json` 给 `{}`，真问题见 §3.33）；证书 `not_after` 时区（`parsedate_to_datetime("… GMT").timestamp()` 新旧 Python 都正确）；NFS/CIFS 卡死 `statvfs`（两个采集器都用 `SKIP_FSTYPES` 排除）；`/api/v1/status` 在锁外读 `coll.snapshot`（CPython 重绑定原子、发布后不再改，无可证竞态）；`nas/fnos-agent.service:11 User=root`（**不是漏洞**：unit 已开 `ProtectSystem=strict`/`ProtectHome`/`NoNewPrivileges`/`CapabilityBoundingSet=CAP_NET_BIND_SERVICE`/`RestrictAddressFamilies`，agent 不写盘、不执行客户端输入，root 是读 `docker.sock` 与 `/sys` 所需；建议只在文档里写明理由）。
- **`nas/fpk` 生命周期/打包脚本复核后判定"不是缺陷"的项（`shellcheck -S warning` 只报 SC2034，七个脚本 `bash -n` 全过）**：`config/privilege` 的 `run-as: root` 不是漏洞（`cmd/main:38-50` 走该分支后 `cmd/docker_launch.py:220-224` 做 `setgroups([])/setgid/setuid`，`:225-226` 复核 `os.getuid()/getgid() != 0`，日志在降权之后才打开 `:227`；错的只是文档）；`docker_launch.py:74-75` 帧超限的 `return` 是**有出口的失败**（`app/server/docker_api.py:42-43` 抛 `ConnectionError('container reader closed')`、`:69` 置 `_broken`、`:53-54` 提示"重启应用"），`--limit` 是 `cmd/main:38-50` 有意省略（1.2.4 全量清单）且与客户端预算检查（`:62-63`）一致；`docker_launch.py` 的所有权恢复（`:94-127` 拒符号/硬链接/非常规项、只接受 uid 0 或包 uid、遍历时把目录封到 0700、`finally` 恢复 uid/gid）、`prepare_data`（`:145-183` 在 fork 子进程里复核包用户访问）、`installed_app_dir`（`:130-142` 的 appname 取自 root 所属 manifest）都干净；无错误 CWD（`install.sh:13`、`preview-naming.sh:15`、`test_lifecycle.sh:16` 从 `$0`/`BASH_SOURCE` 解析，`docker_launch.py:130-133` 从 `realpath(__file__)`）；前导零端口在 bash 里按十进制解析（`[ "08080" -ge 1 ]`），`norm_wizard:148-150` 不会静默重置；无断言落进子 shell（没有 `… | while` 或 `$( … ok/bad … )`）；`SC2140`（`:621/:710/:870`）是中文串里的内层 ASCII 引号，纯外观。`cmd/upgrade_callback`、`config/resource`、`cmd/docker_launch.py`、`wizard/install` 四个文件 **0 发现**。

- **`tools/**` 验收门复核后判定"不是缺陷 / 不回改"的项（附证据）**：`tools/stack_check.py` 的静态栈深模型**没有**漏掉 `fnos_pair` 的 `nvs_load`（独立从 ELF 重算，`app_main → fnos_data_start → fnos_pair_init → nvs_load → snprintf → _svfprintf_r → _dtoa_r → __pow5mult → __multadd → _Balloc` = 4096 B，与工具输出 4096/2512/3088 完全一致；`nvs_load` 的 2032 B 帧跑在**主任务**上，不在 pair_task）；`tools/reason_check.py:64-66` 只收 `s_conn_err` 是**有意**的（通用规则会把 `fnos_data_start()` 写的非失败占位 `"starting"`（`fnos_data.c:505`）误报；残留 low：`link_reason()`（`fnos_ui.c:3078`）没有 `"starting"` 分支，那一段启动窗口落回"离线，显示最后一次采集快照"）；`tools/gen_fonts.sh` 的 CJK 扫描清单**不会**造成 tofu（`-r` 覆盖 0x4E00-0x9FFF，且"未被扫描的设备源码里用到、又不被任何范围覆盖"的 CJK 集合为空；唯一含屏上候选 CJK 的未扫文件 `fnos_net.c reason_text()` 根本没到 UI——`s_reason` 只当标志用）；`make_payload/extract` 的 1 MiB 兜底与 Kconfig 一致（`components/fnos_monitor/Kconfig:51-54` 默认 1048576 == `DEFAULT_BUDGET`，`sdkconfig:2551` 与 `build/config/sdkconfig.h:1183` 同值）；`diag_timer_cb` 不在 ELF 里是**文档化**的（`tools/stack_check.py:53-54`）；`gen_fonts.sh` 不会"失败留下旧文件还继续"（`set -euo pipefail`）；`tools/certcheck/extract.py` 不会抠错函数体（定义正则要求 `)\n`，原型与 K&R 风格都不匹配 → 响亮报"找不到定义"）。**`tools/perf_trace.py` 0 发现**（纯报告工具、非门；两条提示：假定 FTrace 小数位固定 6 位；`frame` 跨度数为 0 时 `nframes = max(len(frames),1)` 会把整段报成一帧）。
- - **`nas/fpk/*_check.py` 复核后撤回的三条（不要改）**：`collector_check.py:58` 的 (1)(2)"从仓库根调用会 IndexError/FileNotFoundError"——实测 `cd nas/fpk && python3 collector_check.py` 与从仓库根调用**都 PASS 退 0**（Python 3.14 下 `__file__` 强制绝对化，`Path(__file__).parent` 至少有 5 层 parents）；`manifest_check.py:87` 的"崩溃"路径——`main` 在 `:266-268` 已 try/except 并返回 1，真实崩溃点是 `:260`（即 H22 的 T2）；`collector_check.py:74` 的"假绿"——`-01` 现在被截成合法 int。附：该批 14 个文件全部存在且都有评论，实跑 `collector_check.py`/`docker_check.py`/`devmode_check.py`（40 轮）/`manifest_check.py`/`perm_check.py`/`startup_permission_check.py`（10 tests）均 `rc 0`；**UNVERIFIABLE**：真 IDF 上 `ESP_IDF_VERSION` 错值的实际报错、`capture-board` 的失败分支（本机 PATH 里有 `capture-board`）、fnOS 卸载是否清 `TRIM_PKGVAR`、root 权限用例（无 sudo 环境）、devmode 的 `HTTPException` 路径（需故障注入）。
- **`tools/reason_check.py:63`「`produced()` 漏掉 `parse_status()` 这条产出通道」—— REFUTED（实跑反驳）**：`parse_status()`（`components/fnos_monitor/fnos_data.c:110-116`）确实用 `snprintf(s_conn_err, sizeof s_conn_err, "%s", reason)` 把 `fnos_status_parse` 的 `reason`（`fnos_snapshot.c:137 "bad payload"`、`:147 "data capacity"`）转抄进 `s_conn_err`，但这两个标签在别处有字面量调用点（`fnos_data.c:304` 的 `"data capacity"`、`:473` 的 `"bad payload"`），已被 `produced()` 的另外两条规则收进集合；实跑 `python3 tools/reason_check.py` → 「`fnos_data.c` 会写出的原因标签（12 个）」= 「`fnos_ui.c` `link_reason` 认得（12 个）」，`bad payload`/`data capacity` 都在其中，退 0。所以这个"漏通道"不构成当前覆盖缺口（`tools/reason_check.py:56-78` 的零覆盖率风险另见 §5 的 low 条目）。
**运维提示（不是缺陷，但影响引用）**：`tools/freshness_check.py` 在当前树上退 1 是**真阳性**——`nas/fpk` 包与 `tools/preview/build/preview` 分别比 `tools/preview/preview.c`、`components/fnos_monitor/ui_kit/uk_number.c` 旧；引用任何产物哈希（如 `README.md:76` 的包 SHA-256）之前先重建。

---

## 5. 死代码与文档漂移（low，批量清理）

**ui_kit 死 API（全仓零调用点）**：`uk_numpad_create`（`uk.c:872` / `uk.h:180`）、`uk_anim_enter`（`uk.c:940` / `uk.h:189`）、`UK_COL_FLAG`（`uk.c:496`）、27 个 `UK_HEX_*`（`uk_theme.h:16-50`，且与运行时 `g_uk_pal` 双重真值源、已漂移：宏抄的是 `P_GRAPHITE`，运行时默认 `uk_theme.c:68 g_uk_pal = P_DEVICE`）、`UK_HAIRLINE_HEX`（`uk_theme.h:57`，`"#RRGGBBAA"` 字符串形式，`lv_color_hex` 只收 `uint32_t`）。

**头文件契约与实现不符（最误导维护者）**：`uk.h:119-127`（列数公式/`data_fit` 语义已被 `uk.c:660-700` 取代）、`uk_theme.h:145`（只列 3 个调色板而 `PRESETS` 有 4 个）、`uk_theme.h:91-97`（单一 `#ifndef` 守卫覆盖 5 个配置）、`fnos_ui.h:16-17`（注释里的 `icons[]` 已不存在）、`:25`（`set_page` 缺线程约束注释）、`:38-40`（`temp_expand` 只展开不切页）、`fnos_data.h:123`（`source_ts=0` 两义）、`:110`、`fnos_config.example.h:12`（未配对是明文）、`:14`（8799 独立 agent 与 8798 FPK 未区分）、`fnos_perf.h:10`（漏 `prof` 子命令）、`:1-3`（缺 `extern "C"`）。

**其他 low**：`fnos_perf.c:484-486` 就地把调用方 buffer 交给 `strtok`（调用方 `fnos_wifi_cli.c:116-117` 只打印首词）；`fnos_ui.c:246-248` `s_night_req` 是普通 `bool`（C11 数据竞争）；`fnos_ui.c:864`、`:4717-4719`（见 §3.19）；`fnos_data.c:338` `adopt_nas_time` 只判下界缺上界；`fnos_data.c:447-450` 历史回填 append 在实时样本之后而 `:558` 按 seq 取最近 600 → 回填满时实时样本被挤出时间轴；`fnos_theme`/文档页数快照（`docs/ui-*-2026-10-08.md` 的"五页"，现六页）。

**FPK 服务端/采集端低危（对抗式复核确认，机制成立但影响面小）**：`nas_companion_server.py:932-938` `/api/shutdown` 回 `{"ok":true}` 却只用 `threading.Thread(target=self.server.shutdown)` 关掉**收到请求的那个监听器**，进程、TCP 遥测面与采集器继续运行（包内暂无调用者：`cmd/main:99` 用 `kill -TERM`，`test_lifecycle.sh:428` 只断言 403）——"失败动作报成功"的现成陷阱；`:568-570` `log_message` 把每个 1 Hz 轮询都写进 `var/service.log` + stderr（与自身注释相反，且管理页只看 400 行内存尾，真实事件几分钟就被挤掉；对照 `nas/fnos-agent.py:792` 是静默的）；`:666-669` 支持 `?token=` 且 `:568-570` 会原样记进日志与 stderr → 手工/curl 路径的长效令牌明文落盘（板子走 `X-Token`，`fnos_data.c:271-272`，集群路径不受影响）；`:514-533` `revoke()`/`check_token()`/`note_seen()` 改 `self.cfg["devices"]` 时**不持 `_lock`**（`pair_allowed`/`try_pair` `:478-512` 持锁）→ 配对与撤销并发时要么刚配好的设备被覆盖、要么撤销被回写（PARTIAL，未做压力测试）；`:1167-1178` `tls_enabled=true` 但证书生成/加载失败时遥测面**以明文起来而配置仍写 TLS 开**（板子会响亮失败"握手失败"、原因在 `state.tls["error"]` 与日志里，不是静默，但对运维是误导）；`nas/fnos-agent.py:240-241` 与 `fnos_collector.py:309-310` 的 net 摘要回退字典只有 5 键，丢掉 `state`/`speed_mbps`/`physical`（与 §3.2 同一契约问题，发生在默认路由接口缺失的那一拍；逐接口行仍是对的，固件也确实不读摘要三键）。

**复核新增的验收工具 low（机制成立，影响面小）**：`tools/preview/hardware_fixtures.py:18` `dense` 用例的 12 个 RAID 阵列只写 `"state":"clean","ok":true` 而没有 `health`/`what`，按解析器自身注释（`components/fnos_monitor/fnos_snapshot.c:174-175`）缺键成 `""` → UI 对 12 个阵列全显示旧文案"状态未知"，"12 行 RAID 布局"从未用正常态文案被审计（修法：补 `"health":"healthy","what":""`，并加一个带维护动作的变体）；`components/fnos_monitor/ui_kit/uk_number.c:172` 内容宽度守卫用的是**字符数**而不是 `:168-169` 已测得的宽度，而字体是比例字体（`components/fnos_monitor/fonts/ui_font_num_32.c` 的 `adv_w`：`'1'=217` vs `'8'/'9'=328`；`num_44`：`'1'=298` vs `'8'=451`）→ `"8.8 TB"`→`"1.1 TB"` 这类"位数相同但更窄"的更新被判为无需重建：`:220` 发布了更短文本、`:221` 收缩了遮罩，而旧层保持旧 x（`:198 g->to_x = g->x`），`:240-243` 的裁剪会把退场的尾位切掉约 100–200 ms（自愈；修法：改用测量宽度判据）；`uk_number.c:124` 预算耗尽（`UK_NUMBER_BUDGET_BYTES`=`CONFIG_FNOS_UI_NUMBER_BUDGET_BYTES`=131072、`sizeof(glyph_layer_t)`=56 B ⇒ ≈1170 个动画数字）时 `add_layer` 返回 NULL，所有调用者（`:141/:184`、`:209/:210`）静默回落原生文本，既无计数也无标志（真分配路径会置 `s_alloc_failed`，`uk.c:35`）→"数字不再动画"与 `UK_NUMBER_REDUCED` 不可区分（修法：置计数器并在 `UK_POOL_TRACE` 处暴露）；`tools/preview/ppm2png.py:27,:52-61` `read_ppm` 不校验长度、`write_png:33-35` 照抄 → 被截断的 `.ppm` 会变成缺行的 PNG 而 `main` 仍打印 `ppm2png: 1 file(s)` 退 0，空目录打印 `0 file(s)` 也退 0（`PARTIAL`）；`tools/preview/motion_view.py:109-112` `check_png` 只比 8 字节签名，仅头的文件算有效帧（`PARTIAL`）；`tools/preview/scroll_gap.js:10` 文件头写"条→内缘 ≥8px"而代码与汇总行是 ≥4，阈值被静默放宽且文档自相矛盾；`nas/fpk/page_check.js:402` 样本缺 `seq` 或 `seq=0` 时 needle 退化成 `"seq 0"`、与页面 `||0` 自证，`hseq` 渲染坏了也不红（`PARTIAL`）；`nas/fpk/config_persistence_check.py:143,156,209` `assertFalse(self.started.exists())` 恒真——`install_callback` 全文 22 行从不调用 `service_start`（仅 `cmd/_common.sh:180` 定义，被 `config_callback:21`、`upgrade_callback:46` 调用）→ 该断言无信息量（修法：改成断言 `config.json` 内容）。



**复核新增的验收门 low（工具侧，机制成立）**：`tools/stack_check.py:106` 只认 `addi sp,sp,-N` 形式的栈帧——RISC-V 无法单指令编码 ≥2048 B 的帧（GCC 改发 `lui/addi` + `sub sp,sp,a5`），此时该帧**不可见**→ 低估，正是文件自己 `:165-166` 称为"最危险的失效方式"的那一种；今天最大帧恰好是 `nvs_load` 的 **2032 B**（余量 16 B），且当前 ELF 里 `sub sp,sp,`/`add sp,sp,` 指令数为 **0**，所以是潜伏项（修法：同时匹配 `lui/addi` + `sub|add sp,sp,a5`）；`tools/verify_all.sh:70` 不解析工具链路径（`test_report.sh:120` 会前置 `PATH`）→ 直接 `FileNotFoundError` 报"栈深检查跑不起来"，也是 H18 那条假绿的入口；`tools/test_report.sh:52` `grep -cF … || echo 0` 在"一个 ✓ 都没有"时同时输出 `0` **并**返回 1 → `SUITE_TICKS="0\n0"` 触发假的 ✓-数量不符/HARD_FAIL（同文件 `:50-51` 注释正好警告这一类）；`tools/release_check.sh:52` 的 `len(v) > 3` 使 ≤3 字符的密钥值**从不被扫描**，且 `val()` 取不到时对四个键都打"未出现"并退 0（今天 `fnos_config.h:5,6,8,10` 四键齐备且带引号）；`tools/perf_bench.py:105-108` 把读取窗口关闭时"还没收到换行"的 `[bench]` 行当成一次有效重复（中位数静默少几个字段）；`tools/parsecheck/run.sh:33`（注释 `:28-29`）把期望环长 600 硬编码在 Python 里而文件自述"期望必须与实现同源"（`components/fnos_monitor/fnos_data.h:26 FNOS_HIST_MAX = 600`），注释还写着"300 行"而代码造 900 行（方向安全：变了会响亮失败）；`tools/reason_check.py:56-78` 两个抽取器同时失配（改名）时 `prod==tran==set()` → 打印"每个失败标签都有对应的人话…✓"并退 0，把零覆盖率报成成功。

**`nas/fpk` 检查脚本与打包脚本的其余 low（复核确认，机制成立）**：`collector_check.py:69` mock 打在 `time.monotonic` 上而 `_docker` 用 `time.time()` → 实测两次调用只有 1 次 HTTP，**mock 全程无效**，现在能过只因缓存必然失效（修法：patch `time.time`）；`cmd/upgrade_init:3` 注释称"先备份再停"而代码 `:9-12` 先 stop、`:17-21` 才备份，**与注释相反**；`upgrade_init:18` 同版本重复升级时 `cp -f` 覆盖 `pre-old` 回退点、`:5` 的 `mkdir`/`touch FLAG` 失败静默（升级后服务不恢复且无提示）；`wizard/uninstall:6-7`（PARTIAL）实测 `cmd/main` 只有 `start|stop|status`、`uninstall_callback` 只删 PID/Socket/FLAG，**从不删 `config.json` 与其中保存的配对令牌**，而文案宣称"令牌已删除"（是否由平台清 `TRIM_PKGVAR` = UNVERIFIABLE，需真机）；`cmd/install_init:7,31` 的 `A && B || C` 让 `say` 的退出状态参与判定、可**反转结论**（改 if/else），`:14-17` `port_busy` 只探 `127.0.0.1` → 占用者绑在网卡 IP 上会漏检（`W_BIND=0.0.0.0` 时应补探测）；`tools/page_shot.py:73,:75` 不查 `capture-board` 的 returncode、不校验 `info["image"]`，`IMAGE=None` 仍退 0（假成功），纯文本末行或 JSON 非 dict 时直接 traceback（`:33` 的 `main(void=None)` 是死参数）；`tools/preview/lv_conf.h:9-11,7` 依赖 `CONFIG_LV_CONF_SKIP` 缺失（实测 `build/config/sdkconfig.h:1311` 存在=1，机制当前成立）但没有 fail-loud 兜底，退化时预览会静默用 LVGL 默认值（注：`tools/` 非固件，不受"只有 LVGL 任务可调 LVGL API"约束）；`docker_check.py:138-139,96` 的 `finally: os._exit(0)` 让异常路径也退 0 → `waitpid` 断言**恒真**、`b'X'`/`b''` 两个用例无法区分"崩溃"，非 daemon 且无超时的 `accept()` 有挂死风险；`devmode_check.py:96-97,120,115-116,55`（`HTTPException` 不是 `OSError` → 压测探针会被一次协议错崩掉；staging 树泄漏；`kill()` 后不 `wait()`；端口竞态）；`startup_permission_check.py:4-8` docstring 指向不存在的 `/tmp/fnos-startup-permission-tests.py`；`idf.sh:15,13-14,21-23`（PARTIAL：`ESP_IDF_VERSION` 错值会静默回落——`Kconfig.idf_v$ESP_IDF_VERSION.in` 实存于 `managed_components/espressif__esp_wifi_remote/Kconfig:8`；`[ -x ]` 未校验解释器；未尊重 `xcode-select -p`）。

**`ocr` 末批新增、Lead 一手复核的低危项**：`tools/preview/ppm2png.py:20-22` 解析 PPM 头时 `while not data[j:j+1].isspace(): j += 1` 在"最后一个字段后没有空白就 EOF"的截断文件上**永不终止**（`b"".isspace()` 恒 False）→ 100% CPU 空转、脚本挂死，而截断的 `.ppm` 是常规产物（预览进程在审计失败路径上会 `abort()`，写了一半的文件留在输出目录；同类还见 `:52-61` 缺行 PNG）；**修法**：循环加 `j < len(data)` 边界并抛错。`tools/certcheck/extract.py:91,101-106` 生成的 `heap_caps_calloc` 桩件是**死代码 + 假注释**：注释称"让 PSRAM 那次直接失败，走它自己的 calloc 回退分支，顺带也验了 cert_info"，但同文件 `:26` 明说"**只验 `pem_fingerprint`**：`cert_info` 依赖 mbedtls 的 x509 整套"——被抠出来的函数集里没有 `cert_info`，全仓（`tools/certcheck/**`）也只有这一处提到它；**修法**：删掉桩件与那句注释，或把 `cert_info` 真抠出来。`nas/preview-naming.sh:17-18` 的 `SSH_OPTS` 关掉主机密钥校验并把 `known_hosts` 指向 `/dev/null`（`-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null`），而该脚本会把 `fnos-agent` 副本上传到 NAS 的 `/tmp` 再执行、且支持 `SSHPASS`（`:22-24` 用 `sshpass -e`）→ 中间人既能替换被执行的载荷、也能拿到口令（4bc24436 已把"`/tmp` 里可预测文件名"判为 low，这条是同一脚本的信任边界问题）；**修法**：至少改成"仅限可信局域网"的显式提示 + 首次连接打印指纹让用户确认，`SSHPASS` 路径建议改用密钥。

**文档漂移（medium，逐条有据）**：`tools/audit125/README.md:21`（`PASS 24 项` → 实测 29）、`docs/fnos-companion-app-plan.md:204-215`（仍以已不存在的 `RX_BUF_SIZE` 8256/24576 为叙述对象，现 `fnos_data.c:56 HIST_BUF_SIZE (32*1024)`；`nas/fpk/contract_check.py:387` 的同名正则已静默空转）、`docs/fnos-market-submission.md:16`（75,932 B → 实测 83,741 B，与 `docs/fnos-companion-test-report.md:32` 自相矛盾）、`tools/patches/README.md:60`（`CONFIG_FNOS_PERF_BENCH` → 实际 `CONFIG_FNOS_UI_PERF_BENCH`，`components/fnos_monitor/Kconfig:115`）、`docs/verification.md:316`（`fnos_view_set_night()` → 实际 `fnos_ui.c:5159 fnos_ui_set_night()`）、`tools/preview/run.sh:2`（"4 页 × 3 状态" → 实测 84 张、六页，`fnos_ui.h:18 FNOS_UI_PAGE_COUNT 6`）。

---

## 6. 覆盖缺口与建议的后续动作

0. **覆盖率与剩余缺口**：`ocr` 的 106 个目标文件里 **93 个**拿到了完整结论（含 0 条评论者），其余 **13 个**（`nas/fpk/{api_check,board_sim,config_persistence_check}.py`、`nas/fpk/nasscreencompanion/cmd/docker_launch.py`、`nas/fpk/page_check.js`、`nas/fpk/test_lifecycle.sh`、`nas/install.sh`、`tools/audit125/soak72.py`、`tools/gen_fonts.sh`、`tools/perf_trace.py`、`tools/preview/motion_view.py`、`tools/stack_check.py`、`tools/test_report.sh`）因 LLM 反复超时未出结论，**已全部由人工对抗式复核覆盖**（见附录 F）；`ocr` 因扩展名/路径规则排除的 144 个对象（`.md`、`CMakeLists.txt`、`sdkconfig.defaults`、`partitions.csv`、`test_*.py` 等）同样如此。生成字体、`docs/archive`、`tools/patches`、二进制资产按 §1.1 说明不在审核范围。
1. **实机验收未做**：H1/H2/H3/H4 都应在板子上按 `AGENTS.md` 的 `capture-board` 流程复验一次（尤其 H1 的 `NAV_TXT[-1]` 在不同链接布局下可能表现为复位而非垃圾文本）。
2. **修复后必须跑的门**：`bash tools/gen_fonts.sh`、`bash tools/preview/run.sh`、`python3 tools/stack_check.py`、`./idf.sh build`；建议把 `snapshot_check` 与 sanitizer 构建纳入 `run.sh`（见 §3.16/§3.17）。
3. **回归夹具建议**：空硬件清单帧（H7）、无换行 PEM 装甲行（H4）、历史回填后二次 commit（H2）、KPI 分配失败注入（H3）、`s_page=-1` 首次刷新（H1）——各加一条最小用例，这些正是现有夹具掩盖掉的路径。
4. 本报告的原始扫描输出（每条评论含 `existing_code`/`suggestion_code`）保存在 `/tmp/ocr_all.json`，逐文件分布见附录 B；`ocr` 各批次 session 位于 `~/.opencodereview/sessions/Users-llll-code-esp-p4-7b-fnos-monitor/`。

---

## 附录 A　扫描批次与 session

| 批次 | session | 说明 |
|------|---------|------|
| part1 主扫描（无 diff 全文件，按语言分批） | `59ee939a` | `--concurrency 6 --timeout 300`（95 分钟后因吞吐过低被终止，已完成结果全部保留） |
| part4 厂商 BSP | `ed913b71` | `--concurrency 4 --timeout 300` |
| part3 too_large 旗舰文件 | `e9d0ac69` | `--max-tokens 220000 --concurrency 1 --timeout 600`，30m09s |
| part2 补扫（77 文件） | `c78337cd` | `--concurrency 12`（因吞吐过低被终止） |
| g/h/i/h6b 分组补扫 | `（多 session）` | `--no-plan --batch none --concurrency 4 --timeout 120`（`/tmp/ocr_g*.txt`、`h*.txt`、`i*.txt`、`h6b.txt`） |
| 旧会话（2026-10-02，仅作附录，结论不进正文） | `af46fdfe`、`b409c87b` | 19 条指向已删除的旧 `kk_ui/`、`ui/` 管线 |

本轮扫描（不含旧会话）原始评论 **371** 条（critical 4 / high 69 / medium 168 / low 130）；含旧会话共 400 条（critical 4 / high 76 / medium 183 / low 137，旧会话结论不进正文）。

各 session 的产出（`done` = 完成审查的文件数，`req` = LLM 请求数）：

| session | done | failed | req |
|---------|------|--------|-----|
| `59ee939a` | 29 | 13 | 285 |
| `0a9fad66` | 9 | 3 | 65 |
| `ae05907c` | 9 | 2 | 49 |
| `43fc1aa9` | 9 | 2 | 58 |
| `d660153a` | 9 | 2 | 38 |
| `5251b90b` | 8 | 3 | 44 |
| `ed913b71` | 8 | 0 | 50 |
| `af46fdfe`（旧会话） | 7 | 0 | 50 |
| `241f8f89` | 6 | 1 | 27 |
| `7a063ad3` | 4 | 0 | 28 |
| `1925326b` | 4 | 0 | 46 |
| `3ecefc94` | 3 | 1 | 11 |
| `0e66bc30` | 3 | 1 | 8 |
| `536c7f28` | 3 | 1 | 12 |
| `f9a553da` | 3 | 0 | 17 |
| `e9d0ac69` | 2 | 0 | 13 |
| `b409c87b`（旧会话） | 2 | 1 | 14 |
| `3f43ca00` | 2 | 0 | 6 |
| `c1003ed7` | 2 | 2 | 15 |
| `465e7bd7` | 0 | 0 | 13 |
| `c78337cd` | 0 | 0 | 10 |
| `df7a1a5a` | 0 | 0 | 3 |
| `5370e8dc` | 0 | 0 | 6 |
| `a741d30f` | 0 | 0 | 6 |
| `695a3bb2` | 0 | 0 | 4 |
| `1454a2f6` | 0 | 1 | 6 |
| `0d47823e` | 0 | 1 | 4 |

## 附录 B　逐文件扫描结果（原始评论数，未含复核判定）

复核后的真实定级见正文 §2–§4；此表的 high 数**不等于**必须修的数量（多数为误报）。

| 文件 | critical | high | medium | low | session |
|------|---------|------|--------|-----|---------|
| `nas/fpk/nasscreencompanion/app/server/fnos_collector.py` | 1 | 1 | 1 | 4 | 43fc1aa9 |
| `tools/serial_capture.py` | 1 | 2 | 1 | 3 | c1003ed7 |
| `tools/verify_all.sh` | 1 | 1 | 1 | 3 | 7a063ad3 |
| `components/lvgl_mem_psram/lvgl_mem_psram.c` | 1 | 0 | 2 | 1 | 59ee939a |
| `components/esp32_p4_wifi6_touch_lcd_7b/esp32_p4_wifi6_touch_lcd_7b.c` | 0 | 4 | 4 | 4 | ed913b71 |
| `nas/fnos-agent.py` | 0 | 1 | 6 | 3 | f9a553da |
| `nas/fpk/nasscreencompanion/app/server/nas_companion_server.py` | 0 | 2 | 3 | 4 | f9a553da |
| `tools/preview/preview.c` | 0 | 2 | 6 | 1 | e9d0ac69 |
| `components/fnos_monitor/fnos_data.c` | 0 | 1 | 3 | 4 | 59ee939a |
| `components/fnos_monitor/fnos_pair.c` | 0 | 1 | 6 | 1 | 59ee939a |
| `components/fnos_monitor/fnos_snapshot.c` | 0 | 2 | 3 | 3 | 59ee939a |
| `tools/parsecheck/main.c` | 0 | 2 | 2 | 4 | 59ee939a |
| `nas/fpk/nasscreencompanion/cmd/main` | 0 | 2 | 3 | 2 | 43fc1aa9 |
| `components/fnos_monitor/fnos_net.c` | 0 | 1 | 1 | 4 | 7a063ad3 |
| `components/fnos_monitor/fnos_perf.c` | 0 | 4 | 2 | 0 | 59ee939a |
| `components/fnos_monitor/fnos_wifi_cli.c` | 0 | 2 | 2 | 2 | 59ee939a |
| `components/fnos_monitor/ui_kit/uk.c` | 0 | 4 | 2 | 0 | 59ee939a |
| `nas/fpk/contract_check.py` | 0 | 1 | 2 | 3 | 5251b90b |
| `nas/fpk/nasscreencompanion/app/ui/www/index.html` | 0 | 1 | 3 | 2 | 3ecefc94 |
| `components/fnos_monitor/fnos_wifi_store.c` | 0 | 2 | 3 | 0 | 59ee939a |
| `nas/fpk/nasscreencompanion/cmd/config_init` | 0 | 2 | 2 | 1 | 0a9fad66 |
| `tools/perf_table.py` | 0 | 1 | 3 | 1 | 0a9fad66 |
| `tools/preview/scroll_gap.js` | 0 | 1 | 2 | 2 | d660153a |
| `components/esp32_p4_wifi6_touch_lcd_7b/priv_include/bsp_err_check.h` | 0 | 1 | 1 | 2 | ed913b71 |
| `components/fnos_monitor/ui_kit/uk.h` | 0 | 2 | 2 | 0 | 59ee939a |
| `nas/preview-naming.sh` | 0 | 1 | 1 | 2 | ae05907c |
| `tools/certcheck/extract.py` | 0 | 1 | 2 | 1 | 5251b90b |
| `tools/freshness_check.py` | 0 | 1 | 2 | 1 | ae05907c |
| `tools/perf_bench.py` | 0 | 1 | 2 | 1 | ae05907c |
| `tools/plan_check.py` | 0 | 2 | 1 | 1 | 43fc1aa9 |
| `tools/preview/palettes.sh` | 0 | 1 | 2 | 1 | 3ecefc94 |
| `tools/preview/ppm2png.py` | 0 | 2 | 1 | 1 | d660153a |
| `tools/release_check.sh` | 0 | 1 | 2 | 1 | d660153a |
| `components/esp32_p4_wifi6_touch_lcd_7b/Kconfig` | 0 | 1 | 1 | 1 | ed913b71 |
| `components/esp32_p4_wifi6_touch_lcd_7b/include/bsp/display.h` | 0 | 1 | 1 | 1 | ed913b71 |
| `components/fnos_monitor/fnos_net.h` | 0 | 2 | 1 | 0 | 59ee939a |
| `nas/fpk/mem_check.py` | 0 | 1 | 0 | 2 | 0a9fad66 |
| `nas/fpk/nasscreencompanion/cmd/config_callback` | 0 | 1 | 1 | 1 | 241f8f89 |
| `tools/certcheck/main.c` | 0 | 2 | 1 | 0 | 59ee939a |
| `tools/preview/run.sh` | 0 | 1 | 1 | 1 | d660153a |
| `tools/reason_check.py` | 0 | 1 | 0 | 2 | ae05907c |
| `nas/fpk/build.sh` | 0 | 1 | 1 | 0 | 3f43ca00 |
| `nas/fpk/nasscreencompanion/cmd/uninstall_callback` | 0 | 1 | 1 | 0 | d660153a |
| `nas/fpk/nasscreencompanion/wizard/config` | 0 | 1 | 1 | 0 | 5251b90b |
| `nas/fpk/nasscreencompanion/wizard/install` | 0 | 1 | 1 | 0 | 0a9fad66 |
| `tools/preview/snapshot_check.c` | 0 | 1 | 1 | 0 | 59ee939a |
| `nas/fpk/nasscreencompanion/config/privilege` | 0 | 1 | 0 | 0 | 241f8f89 |
| `components/esp32_p4_wifi6_touch_lcd_7b/include/bsp/esp32_p4_wifi6_touch_lcd_7b.h` | 0 | 0 | 5 | 1 | ed913b71 |
| `components/fnos_monitor/fnos_data.h` | 0 | 0 | 4 | 2 | 59ee939a |
| `components/fnos_monitor/fnos_ui.h` | 0 | 0 | 4 | 2 | 59ee939a |
| `components/fnos_monitor/fnos_config.example.h` | 0 | 0 | 2 | 3 | 59ee939a |
| `nas/fpk/devmode_check.py` | 0 | 0 | 3 | 2 | 536c7f28 |
| `nas/fpk/nasscreencompanion/cmd/_common.sh` | 0 | 0 | 3 | 2 | 0a9fad66 |
| `nas/fpk/wizard_check.py` | 0 | 0 | 5 | 0 | d660153a |
| `tools/certcheck/run.sh` | 0 | 0 | 3 | 2 | 43fc1aa9 |
| `tools/photo_check.py` | 0 | 0 | 3 | 2 | 5251b90b |
| `tools/soak_check.py` | 0 | 0 | 1 | 4 | d660153a |
| `components/fnos_monitor/fnos_pair.h` | 0 | 0 | 3 | 1 | 59ee939a |
| `components/fnos_monitor/fnos_perf.h` | 0 | 0 | 2 | 2 | 59ee939a |
| `components/fnos_monitor/ui_kit/uk_theme.h` | 0 | 0 | 2 | 2 | 59ee939a |
| `nas/fpk/manifest_check.py` | 0 | 0 | 2 | 2 | ae05907c |
| `nas/fpk/nasscreencompanion/cmd/install_init` | 0 | 0 | 1 | 3 | 5251b90b |
| `nas/fpk/nasscreencompanion/cmd/upgrade_callback` | 0 | 0 | 2 | 2 | 43fc1aa9 |
| `nas/install-remote.sh` | 0 | 0 | 2 | 2 | 43fc1aa9 |
| `tools/parsecheck/run.sh` | 0 | 0 | 3 | 1 | 0a9fad66 |
| `idf.sh` | 0 | 0 | 1 | 2 | 3f43ca00 |
| `nas/fpk/nasscreencompanion/cmd/upgrade_init` | 0 | 0 | 3 | 0 | d660153a |
| `nas/fpk/temps_check.py` | 0 | 0 | 1 | 2 | 43fc1aa9 |
| `tools/page_shot.py` | 0 | 0 | 2 | 1 | 3ecefc94 |
| `tools/preview/pair_msgs.py` | 0 | 0 | 2 | 1 | ae05907c |
| `components/esp32_p4_wifi6_touch_lcd_7b/include/bsp/touch.h` | 0 | 0 | 1 | 1 | ed913b71 |
| `components/fnos_monitor/Kconfig` | 0 | 0 | 2 | 0 | 0e66bc30 |
| `components/fnos_monitor/ui_kit/uk_theme.c` | 0 | 0 | 2 | 0 | 59ee939a |
| `nas/fpk/collector_check.py` | 0 | 0 | 1 | 1 | 7a063ad3 |
| `nas/fpk/docker_check.py` | 0 | 0 | 2 | 0 | 5251b90b |
| `nas/fpk/nasscreencompanion/app/server/docker_api.py` | 0 | 0 | 1 | 1 | ae05907c |
| `nas/fpk/nasscreencompanion/cmd/install_callback` | 0 | 0 | 2 | 0 | ae05907c |
| `nas/fpk/perm_check.py` | 0 | 0 | 2 | 0 | d660153a |
| `tools/parsecheck/extract.py` | 0 | 0 | 1 | 1 | 0a9fad66 |
| `tools/parsecheck/make_payload.py` | 0 | 0 | 1 | 1 | 5251b90b |
| `tools/preview/lv_conf.h` | 0 | 0 | 1 | 1 | 59ee939a |
| `tools/preview/stub/esp_heap_caps.h` | 0 | 0 | 1 | 1 | 59ee939a |
| `tools/preview/stub/sdkconfig.h` | 0 | 0 | 1 | 1 | 59ee939a |
| `nas/fpk/nasscreencompanion/wizard/uninstall` | 0 | 0 | 1 | 0 | 5251b90b |
| `tools/preview/stub/esp_log.h` | 0 | 0 | 0 | 4 | 59ee939a |
| `components/fnos_monitor/ui_kit/uk_number.c` | 0 | 0 | 0 | 2 | f9a553da |
| `components/esp32_p4_wifi6_touch_lcd_7b/include/bsp/config.h` | 0 | 0 | 0 | 1 | ed913b71 |
| `components/fnos_monitor/fnos_wifi_store.h` | 0 | 0 | 0 | 1 | 0a9fad66 |
| `nas/fpk/startup_permission_check.py` | 0 | 0 | 0 | 1 | 241f8f89 |
| `tools/preview/hardware_fixtures.py` | 0 | 0 | 0 | 1 | 43fc1aa9 |

## 附录 C　ASan / UBSan 原始输出（`fnos_ui.c:3717`，正文 H1）

```
palette: device
/Users/llll/code/esp/p4-7b-fnos-monitor/components/fnos_monitor/fnos_ui.c:3717:67: runtime error: index -1 out of bounds for type 'const char *const[6]'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /Users/llll/code/esp/p4-7b-fnos-monitor/components/fnos_monitor/fnos_ui.c:3717:67 
=================================================================
==14071==ERROR: AddressSanitizer: global-buffer-overflow on address 0x000102b24cd8 at pc 0x0001023e59b8 bp 0x00016da91b50 sp 0x00016da91b48
READ of size 8 at 0x000102b24cd8 thread T0
    #0 0x0001023e59b4 in header_refresh+0x2e8 (preview:arm64+0x1000799b4)
    #1 0x0001023cb8d8 in refresh+0x318 (preview:arm64+0x10005f8d8)
    #2 0x0001023bbb98 in fnos_ui_set_page+0x1ec (preview:arm64+0x10004fb98)
    #3 0x0001023b75b4 in fnos_ui_create+0x19b0 (preview:arm64+0x10004b5b4)
    #4 0x000102378538 in main+0x10b4 (preview:arm64+0x10000c538)
    #5 0x000196467e7c in start+0x1a1c (dyld:arm64e+0x31e7c)

0x000102b24cd8 is located 8 bytes before global variable 'NAV_TXT' defined in '/Users/llll/code/esp/p4-7b-fnos-monitor/components/fnos_monitor/fnos_ui.c' (0x000102b24ce0) of size 48
0x000102b24cd8 is located 40 bytes after global variable 'switch.table.run_state.780' defined in '/Users/llll/code/esp/p4-7b-fnos-monitor/tools/preview/preview.c' (0x000102b24c80) of size 48
SUMMARY: AddressSanitizer: global-buffer-overflow (preview:arm64+0x1000799b4) in header_refresh+0x2e8
Shadow bytes around the buggy address:
  0x000102b24a00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x000102b24a80: 00 00 00 00 00 00 f9 f9 f9 f9 f9 f9 f9 f9 f9 f9
  0x000102b24b00: 00 00 00 00 00 00 00 00 00 00 f9 f9 f9 f9 f9 f9
  0x000102b24b80: 00 00 00 f9 f9 f9 f9 f9 00 00 00 00 00 00 00 f9
  0x000102b24c00: f9 f9 f9 f9 00 00 00 00 00 00 f9 f9 f9 f9 f9 f9
=>0x000102b24c80: 00 00 00 00 00 00 f9 f9 f9 f9 f9[f9]00 00 00 00
  0x000102b24d00: 00 00 f9 f9 f9 f9 f9 f9 00 00 00 00 00 00 00 00
  0x000102b24d80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x000102b24e00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x000102b24e80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x000102b24f00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07 
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
==14071==ABORTING
```

## 附录 D　覆盖缺口

- 目标文件（`ocr scan --preview` 的 Will review 列表）：**106** 个
- `ocr` 完成审查的文件（任意批次写过 `review_item_done`，含 0 条评论者）：**93** 个
- `ocr` 未完成（多次 LLM 超时 / 批次被终止 / 仍在重试）：**13** 个

下列文件没有拿到 `ocr` 的完整结论，**已全部改由附录 F 的人工对抗式复核覆盖**（结论并入正文；`ocr` 因 `unsupported_ext`/`default_path`/`too_large` 排除的对象同样如此）：

- `nas/fpk/api_check.py`
- `nas/fpk/board_sim.py`
- `nas/fpk/config_persistence_check.py`
- `nas/fpk/nasscreencompanion/cmd/docker_launch.py`
- `nas/fpk/page_check.js`
- `nas/fpk/test_lifecycle.sh`
- `nas/install.sh`
- `tools/audit125/soak72.py`
- `tools/gen_fonts.sh`
- `tools/perf_trace.py`
- `tools/preview/motion_view.py`
- `tools/stack_check.py`
- `tools/test_report.sh`

#### 记录过 `review_item_failed` 的目标文件（多为一次超时后由重跑或人工复核补齐）：

- `components/fnos_monitor/fnos_net.c`
- `components/fnos_monitor/fnos_wifi_store.h`
- `components/fnos_monitor/ui_kit/uk_number.c`
- `nas/fnos-agent.py`
- `nas/fpk/api_check.py`
- `nas/fpk/board_sim.py`
- `nas/fpk/collector_check.py`
- `nas/fpk/config_persistence_check.py`
- `nas/fpk/contract_check.py`
- `nas/fpk/devmode_check.py`
- `nas/fpk/nasscreencompanion/app/server/docker_api.py`
- `nas/fpk/nasscreencompanion/app/server/nas_companion_server.py`
- `nas/fpk/nasscreencompanion/app/ui/www/index.html`
- `nas/fpk/nasscreencompanion/cmd/docker_launch.py`
- `nas/fpk/page_check.js`
- `nas/fpk/test_lifecycle.sh`
- `nas/install.sh`
- `tools/audit125/soak72.py`
- `tools/gen_fonts.sh`
- `tools/perf_trace.py`
- `tools/preview/motion_view.py`
- `tools/preview/scroll_gap.js`
- `tools/stack_check.py`
- `tools/test_report.sh`

## 附录 E　复现与回归命令

```bash
# 1) 主机 sanitizer 门（正文 H1；不要加 ASAN_OPTIONS=detect_leaks=1，macOS 不支持）
cmake -S tools/preview -B tools/preview/build-sanitize -DFNOS_PREVIEW_SANITIZE=ON
cmake --build tools/preview/build-sanitize --target preview -j8
PREVIEW_SIZE=1024x600 tools/preview/build-sanitize/preview /tmp/asan_out; echo "exit=$?"   # 期望：修复后 0，当前 134

# 2) 空硬件清单帧（正文 H7）
cd tools/parsecheck && ./ps /tmp/empty.json; echo "exit=$?"   # 期望：修复后 0，当前 139

# 3) 交付前门（AGENTS.md）
bash tools/gen_fonts.sh
bash tools/preview/run.sh
python3 tools/stack_check.py
./idf.sh build
```

重新采集原始评论：`python3 /tmp/ocr_collect.py --out /tmp/ocr_all.json --status`
（本报告由 `docs/full-repo-review-2026-10-10.md` 生成脚本汇总；扫描器各批次 session 见附录 A。）

## 附录 F　人工复核覆盖（`ocr` 不适用或反复超时的文件）

`ocr` 的排除项（`user_exclude`/`unsupported_ext`/`default_path`）与反复 LLM 超时的文件，
由只读对抗式复核子代理逐个人工审核（不修改任何被审代码）：

| 复核者 | 覆盖范围 |
|--------|----------|
| `b182f026` | `tools/audit125/test_diag.py`、`test_pair_nvs.py`、`test_raid.py`、`test_soak72.py` |
| `c3d1727f` | 根/各组件 `CMakeLists.txt`、`sdkconfig.defaults`、`partitions.csv`、`dependencies.lock`、`nas/fnos-agent.service`、`components/fnos_monitor/Kconfig` |
| `3b2036bb` | 33 份 `.md` 文档（排除 BSP 与 `docs/archive`） |
| `6397ae21` | `tools/**` 的 32 条扫描评论逐条复核 |
| `d7c04bd2` | `fnos_wifi_cli.c`、`fnos_wifi_store.c`、`fnos_net.h`（14 条） |
| `be0f140a` | `fnos_pair.c`、`fnos_pair.h`（12 条，含 ASan 复现） |
| `68e768f3` | `fnos_data.c`、`fnos_snapshot.c`、`fnos_perf.c`（22 条） |
| `cc983774` | `ui_kit/**`（16 条） |
| `32314f25` | 各头文件 + `main/main.cpp`（28 条） |
| `89173e5f` | `fnos_ui.c` + `tools/preview/preview.c`（part3 的 15 条，含对照实验） |
| `cdb5b464` | （中途失败，无结论）`nas/fpk/{api,board_sim,config_persistence,mem,temps,wizard}_check.py`、`page_check.js`、`scroll_gap.js` |
| `235157cc` | 接替 `cdb5b464`：`nas/fpk/{api,board_sim,config_persistence,mem,temps,wizard}_check.py` |
| `10c2f5e2` | 接替 `cdb5b464`：`nas/fpk/page_check.js`、`nas/fpk/scroll_gap.js` |
| `437f224e` | `idf.sh`、`nas/fpk/{collector,docker,devmode,manifest,perm,startup_permission}_check.py`、`app/server/docker_api.py`、`tools/page_shot.py`、`tools/preview/lv_conf.h`、`cmd/{install_init,upgrade_init,install_callback}`、`wizard/uninstall` |
| `4bc24436` | `nas/fpk/test_lifecycle.sh`、`cmd/{_common.sh,config_init,upgrade_callback,docker_launch.py}`、`config/resource`、`wizard/install`、`nas/install*.sh`、`nas/preview-naming.sh` |
| `61124156` | `tools/` 其余 15 个脚本（`freshness_check.py`、`gen_fonts.sh`、`parsecheck/*`、`perf_*`、`plan_check.py`、`reason_check.py`、`release_check.sh`、`soak_check.py`、`stack_check.py`、`test_report.sh`、`verify_all.sh`） |
| `a43e5a02` | `tools/preview/{hardware_fixtures,motion_view,pair_msgs,ppm2png}.py`、`tools/preview/run.sh`、`tools/certcheck/*`、`components/fnos_monitor/ui_kit/uk_number.c` |
| `04ef5869` | `nas/fnos-agent.py`、`nas/fpk/nasscreencompanion/app/server/nas_companion_server.py` |

生成物（`components/fnos_monitor/fonts/**`，由 `tools/gen_fonts.sh` 生成）、历史补丁（`tools/patches/**`）、
归档文档（`docs/archive/**`）与二进制资产（图片）不在代码审核范围，理由见正文 §1.1。
