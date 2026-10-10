# 触控与渲染性能基线（2026-10-09，实机）

本文件记录「30% 触控与性能提升」这条目标的**起点数字**：口径、测量设施、基线值，以及哪些指标能判、
哪些不能判。后续每个优化提交都要拿这里的表对比，数字一律来自实机计数器。

## 口径

- **基线固件** = `6f94878`（当时代码 + 纯台架仪表，零优化）。仪表默认关，测量时才在本地 sdkconfig 打开：

  | 选项 | 值 | 作用 |
  |---|---|---|
  | `CONFIG_FNOS_UI_PERF_BENCH` | y | 串口 `bench` 命令（合成指针 + 逐帧计数器） |
  | `CONFIG_FNOS_UI_MOTION_STATS` | y | 切页 `motion … prepare_us/frames/render_avg_us` 日志 |
  | `CONFIG_ESP_LVGL_ADAPTER_ENABLE_FPS_STATS` | y | adapter 侧帧率交叉验证 |

- **基线数字只能来自实机**。宿主预览的 `PREVIEW_MOTION_RESPONSIVENESS`（32ms 响应 / 180ms 完成）走虚拟时间，
  只当"手感没坏"的回归门禁，不当帧率证据。
- 目标（P）= 按下→首帧、拖动帧间隔的 **p50 与 p95 都改善 ≥30%**；辅助指标（`ui_tick`、`cpu_lvgl`、
  FPS、`misses`）回退不得超过 5%。
- 噪声控制：同一块板、同一份 sdkconfig（除被测项）、固定页面（系统页 3）、`--repeat 20 --warmup 1`，
  每个指标先取单次重复的值、再对 20 次重复取中位数。

## 测量设施

- 固件侧 `components/fnos_monitor/fnos_perf.c`：串口 `bench tap <page> [cycles]` / `bench swipe <page> [px] [cycles]`
  （px 0 = 半屏宽）/ `bench idle <page> [seconds]` / `bench help`。合成指针状态机由 8ms `lv_timer` 在
  **LVGL 任务里**推进；真实面板读取保留在链路里（先调 adapter 的 read_cb，再覆盖 `point/state`），
  结束时换回，所以被测的不是另一条输入路径。
- 采集侧 `tools/perf_bench.py`：握住串口发命令、解析 `[bench]` 行、写 CSV。两个坑写在脚本里：
  开端口会复位板子；**开端口后第一次写会带一个杂散字节**，必须先发空行冲掉。
- 汇总 `python3 tools/perf_table.py <baseline.csv> <candidate.csv>`：同用例的 N 次重复取中位数，给 Δ%。

复现（macOS，板子在 `/dev/cu.usbmodem5CF71088571`）：

```
export FNOS_IDF_ENV=/Users/llll/code/esp/use-esp-idf-5.5.3.sh
python3 tools/perf_bench.py --port /dev/cu.usbmodem5CF71088571 --label base \
  --cases tap:3:3,swipe:3:0:3,idle:3:5 --repeat 20 --warmup 1 --boot 25 \
  --out /tmp/fnos-perf/base.csv
```

## 基线值（N=20，系统页）

| 指标 | tap:3:3 | swipe:3:0:3 | idle:3:5 |
|---|---|---|---|
| 按下→首帧 p50 | 11.6 ms | 66.7 ms | — |
| 按下→首帧 p95 | 11.6 ms | 66.7 ms | — |
| 松手→首帧 p50 | 119.9 ms | 45.9 ms | — |
| 帧间隔 p50 | 38.7 ms | 37.6 ms | — |
| 帧间隔 p95 | 38.7 ms | 49.1 ms | — |
| ui_tick p50 | 75.2 ms | 75.5 ms | 75.6 ms |
| ui_tick p95 | 91.6 ms | 94.3 ms | 106.3 ms |
| FPS | 16.9 | 18.4 | 16.0 |
| LVGL 任务 CPU% | 14.4 | 10.4 | 6.3 |
| misses | 0 | 0 | 0 |

（原始 CSV：`/tmp/fnos-perf/base.csv`，60 行。）

## 判读规则（重要）

- **`iv_n`（帧间隔样本数）先看**：`tap` 用例每次只按一下，`iv_n` 中位数 3（2~6），它的帧间隔 p50/p95
  在小样本上会乱跳（基线 16.6 / 181.0 / 50.0 ms 之间来回），**不可用于判定**；`swipe` 用例 `iv_n`
  中位数 16（10~19），可用于判定 P2。
- **`ui_tick` 是最稳的信号**：每次重复都落在 75±1 ms，任何改动只要动到刷新路径都会立刻反映出来。
- `idle` 用例没有按/拖，`p2f`/`iv` 列为空（0），只用来测稳态帧率、`ui_tick` 与 CPU 占用。
- 30% 的目标线（按基线换算）：swipe 帧间隔 **p50 ≤ 26.3 ms、p95 ≤ 34.4 ms**；tap 按下→首帧
  **p50 ≤ 8.1 ms**（它已经被 indev 15ms 采样周期量化，必须同时缩短采样周期才可能达标）。

## 基线结论

1. 每 500ms 的 `ui_tick` 要 **75 ms**，且跑在 LVGL 任务里持显示锁 —— 这是拖动帧间隔 p95 49 ms、
   切页慢的主嫌。
2. 拖动时帧间隔 p50 37.6 ms（≈27 fps），远低于 `LV_DEF_REFR_PERIOD=15ms` 的能力；成本在
   "整视口重绘 + flush 切片"，不在刷新路径。
3. 触摸按下到首帧 p50 只有 11.6 ms，但它由 indev 采样周期（15ms）量化，改善空间来自采样周期本身。
