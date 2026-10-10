# tools/audit125 —— v1.2.5 候选专项审计的回归工具

本目录是《v1.2.5 候选专项审计与整改》报告里那套补丁与测试的**本机可复现对应物**。
报告 §3 声称的 `v1.2.5-candidate.patch` 与 `tools/audit125/*` 在公开仓库、本机任何位置都
取不到（见 `docs/review-v1.2.5-audit-2026-10-10.md` §0），所以这里把它们按同一条目重新做出来：
每个脚本要么驱动**真实生产文件**里抽出来的函数，要么对着固定判据做离线评价，不复制实现。

对应关系：

| 脚本 | 覆盖的审计条目 | 抽取/驱动的真实代码 |
|---|---|---|
| `test_raid.py` | R1 降级被进度掩盖、R2 读不到 mdstat 被当空清单、R3 `inactive` 命中 `active`、R4 其它段为 None 时告警整体丢失、R5 RAID0/linear 与容器误报降级 | `nas/fpk/nasscreencompanion/app/server/fnos_collector.py` 整个模块（只读导入） |
| `test_pair_nvs.py` | N1 明文配对残留旧证书、N2 wipe 失败谎报已清除、N3 tls 类型/非 200/超长 token | `components/fnos_monitor/fnos_pair.c` 按函数名抽取 `has_cert`/`nvs_save`/`nvs_wipe`/`pair_response_accept` 与 `do_fetch` 的 tls 判定片段 |
| `test_diag.py` | S1 板端诊断证据（心跳字段/单位/异常标记） | `main/main.cpp` 里真实的 `diag_timer_cb` 函数体 |
| `soak72.py` | S1 72 小时只读采集 + 离线三态评价 | 无（只解析诊断行；判据按报告 §4） |
| `test_soak72.py` | 上面那个评价器自身的回归 | 合成 `metrics.jsonl`，不接串口、不联网 |

## 1. 主机侧（不需要板子，也不需要 IDF 工具链）

```bash
python3 tools/audit125/test_raid.py          # PASS 24 项，退出码 0
python3 nas/fpk/collector_check.py           # 既有伴侣层检查
python3 nas/fpk/contract_check.py            # 既有字段契约检查
python3 tools/audit125/test_soak72.py        # 评价器自测（合成记录）
```

需要 C/C++ 编译器的两个（`test_pair_nvs.py` 用 `cc`，`test_diag.py` 用 `c++`）：

```bash
python3 tools/audit125/test_pair_nvs.py      # PASS 37 项
python3 tools/audit125/test_diag.py          # PASS 136 项
CC=gcc  python3 tools/audit125/test_pair_nvs.py     # 指定 C 编译器
CXX=g++ python3 tools/audit125/test_diag.py         # 指定 C++ 编译器
```

Windows PowerShell 同上，用 `python` 代替 `python3`；没有 `cc`/`c++` 时用 `CC`/`CXX`
指向本机编译器（例如 `$env:CC = "gcc"`）。这两个脚本会在系统临时目录里拼翻译单元，
只用标准库 + 生产文件 + `managed_components/espressif__cjson`。

## 2. 诊断固件（板端证据的来源，默认关闭）

诊断心跳在 `CONFIG_FNOS_SOAK_DIAG=y` 时由 LVGL 事件循环每 10 秒打一行，默认 `n`。
**必须走工程自己的入口构建**，否则 `ESP_IDF_VERSION` 没导出，kconfig 重配会丢掉
`CONFIG_SLAVE_IDF_TARGET_*` 一族符号，`esp_hosted` 直接报 `#error "Unknown Slave Target"`：

```bash
FNOS_IDF_ENV=/Users/llll/code/esp/use-esp-idf-5.5.3.sh ./idf.sh build          # 默认版
FNOS_IDF_ENV=/Users/llll/code/esp/use-esp-idf-5.5.3.sh ./idf.sh \
    -B build_diag -D SDKCONFIG=$PWD/build_diag/sdkconfig build                 # 诊断版
# 诊断版的 sdkconfig 是主 sdkconfig 的副本 + CONFIG_FNOS_SOAK_DIAG=y，不要手改主 sdkconfig
python3 tools/stack_check.py --elf build_diag/fnos_monitor.elf
```

诊断行的契约（采集脚本按它解析，改字段等于改接口）：

```text
I (123456) main: FNOS_DIAG {"n":1234,"up":1234567,"hz":10,"fw":"<64位小写hex ELF SHA-256>",
 "rst":4,"ev":["panic"],"io":{"ok":4200,"fail":1,"age":3,"p95":180,"src":1697000000},
 "ui":{"age":2},"mem":{"ifree":180000,"imin":150000,"imax":65536,"dfree":90000,"dmin":80000,
 "pfree":3000000,"pmin":2500000},"stk":{"lvgl":3000,"poll":2100,"pair":1800}}
```

| 字段 | 单位/含义 |
|---|---|
| `n` / `up` | 诊断序号 / 开机毫秒 |
| `hz` | 标称周期秒（本构建 10） |
| `fw` | 64 位小写 hex，等于**本次 ELF 的 SHA-256**（`esp_app_get_elf_sha256`） |
| `rst` / `ev` | 复位原因编号 / 异常名（4 PANIC、5 INT_WDT、6 TASK_WDT、7 WDT、9 BROWNOUT、15 CPU_LOCKUP） |
| `io.ok` / `io.fail` | 成功/失败接收计数 |
| `io.age` | 距最近一次成功接收的秒数（从未成功 = -1） |
| `io.p95` | 最近 64 次请求耗时的 P95（毫秒） |
| `io.src` | 最近成功帧的 NAS `source_ts`（epoch 秒，无 = -1） |
| `ui.age` | 两次诊断回调之间的秒数（**不代表帧耗时或触摸延迟**） |
| `mem.*` | 字节；`imin/dmin/pmin` 是 heap_caps 历史最小，`imax` 是内部最大连续块。DMA 与内部 RAM 可能覆盖同一段堆，**不能相加** |
| `stk.*` | `uxTaskGetStackHighWaterMark` 的**字节**高水位（本工程用 IDF FreeRTOS，不乘 4）；0 = 该任务没起来/测不到 |

## 3. 72 小时实机长测

串口只读采集，脚本不发任何数据、不管理 NAS/路由器，掉线只记一条并收工（不自动重开串口
掩盖掉线）。pyserial 在 `open()` 前把 `dtr/rts/dsrdtr/rtscts` 全部置 false，避免 USB-Serial-JTAG
把 DTR/RTS 当 BOOT/EN 造成"看起来板子自己重启"。

Windows PowerShell（端口只在本机输入，不进任何文件）：

```powershell
python -m pip install pyserial
$env:FNOS_SOAK_PORT = Read-Host '板端串口，仅在本机输入'
python tools/audit125/soak72.py capture --hours 72 --elf build/fnos_monitor.elf --output soak72-results
python tools/audit125/soak72.py analyze --input soak72-results/metrics.jsonl --hours 72 --elf build/fnos_monitor.elf --output soak72-review
```

故障窗口文件只含数字（`--fault-windows` 只影响评价、不产生故障；两次命令要传同一个文件）：

```json
[{"start_s":86400,"end_s":86460},{"start_s":129600,"end_s":129720}]
```

产物：`metrics.jsonl`（每行 `{"t":<接收时刻>,"rec":{…}}`，只有数值）与 `summary.json`
（唯一的下游接口，含 verdict 与原因）。判据要点（报告 §4）：心跳覆盖 ≥99%、任意样本间隔
≤30s、60s 无心跳 FAIL、栈高水位 ≥1024B、内部历史最小空闲 ≥32KiB/最大连续块 ≥16KiB、
DMA ≥8KiB、PSRAM 历史最小空闲 ≥1MiB、前 2h 预热不计趋势、HTTP P95 ≤1000ms、
板端最新成功接收年龄 ≤15s、`source_ts` 持续不变 >30s 或缺失 FAIL、登记故障窗口结束
60s 内必须恢复。

## 4. 退出码

| 码 | 含义 |
|---|---|
| 0 | PASS：判据全部满足 |
| 1 | FAIL：能证明违反了判据（含异常复位、心跳中断、栈/堆破线） |
| 2 | INCOMPLETE：**证据不足**，不能判 PASS（缺结束标记、起止心跳、采样不够、主机时钟回拨等） |
| 3 | INPUT_ERROR：输入本身不可用（文件空/坏、参数错、缺编译器、抽取失败） |

`test_pair_nvs.py` 的 2/3 分别是"生产文件抽取失败"与"环境不可用"；`test_raid.py`、
`test_diag.py`、`test_soak72.py` 只用 0/1。`soak72.py` 任何输入都会落一份 `summary.json`。

## 5. 证据边界（这些脚本不给你盖什么章）

* 主机测试里的 NVS、ESP-IDF、FreeRTOS、LVGL、cJSON 全是替身或真实库的主机编译：
  它们证明**判断逻辑**，不证明 Flash 行为、真实 TLS 握手、真实 mdstat、真实触摸。
* `test_diag.py` 证明字段契约，**不证明板上 LVGL 每 10 秒真的跑到**；那要靠 72h 的
  心跳覆盖，而心跳也不等于画面流畅（不测 FPS、触摸 P95/P99）。
* 诊断日志本身有观察开销，生产版关掉诊断后仍需一次短回归。
* 磁盘/阵列结论只覆盖本次 mdstat 可见的成员状态，不等于 SMART、文件系统或其它池健康。
* 72h 未跑完之前，本目录的任何绿灯都不能当作发布门槛已过；报告 §7 的缺口清单仍然有效。
