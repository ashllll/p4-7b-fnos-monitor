# 第四轮：剩余问题清零（2026-10-08）

上一轮（六页骨架重排，见 `docs/ui-iter3-skeleton-2026-10-08.md`）遗留 7 条已知项，本轮处理其中
能在设备上闭环的 4 条 + 1 条只有真机能抓到的真 bug，并把防再犯的审计挂进快照路径。

| # | 遗留项 | 处置 | 证据 |
|---|--------|------|------|
| 1 | 告警页健康态几乎全空 | 加"当前监控维度"体检块（四行） | 实机 `docs/evidence/2026-10-08-round5-p5-health.png`、样张 `06-alerts-health` |
| 2 | 温度页展开态实机没法验（GT911 触摸不可自动化） | 固件串口 `temp <n>` + `page_shot.py --temp` | 实机 `2026-10-08-round5-p4-temp-expanded.png` |
| 3 | 最热温度磁贴脚注被 DOTS 截断 | 脚注去重（不再抄第二遍传感器名） | 实机 `2026-10-08-round5-p0-band-fixed.png`（"最高 75°C · 容器 4/4"） |
| 4 | 结论带把 `%d/%d` 原样印到屏上（真机抓到，主机 84 张全没抓到） | 修 + 新增两道守护 | 前后对照 `2026-10-08-round5-p0-band-bug.png` / `...-band-fixed.png` |
| 5 | —（顺带） | 全局"格式残渣"审计，挂进每个审计面 | preview.c:762 / :1181 / :2232 / :3436 |

---

## 1 告警页健康态：从"空"到"体检块"

- 结构：`s_ui.alert_health`（fnos_ui.c:180 字段、fnos_ui.c:1801 建树）挂在告警卡 body 里，
  标题 `当前监控维度`（fnos_ui.c:1807），下面四行 = 圆点 + 名字（定宽 68）+ 值（`flex_grow 1` +
  `min_width 0` + `LV_LABEL_LONG_MODE_DOTS`，fnos_ui.c:1811-1817）。
- 行名与文案（fnos_ui.c:3664 `alert_health_sync`，取值在 fnos_ui.c:3748-3749 落屏）：
  `存储容量` `%d 个卷 · 最高 %s %.0f%% · 阵列 %d/%d 正常`、`温度` `%s%d 路 · 最热 %.0f°C · ≥%.0f°C 关注 %d 路`、
  `容器` `%s%d/%d 运行`、`采集` `轮询 %u 次 · 失败 %u 次 · 数据 %s`。
- 只在"体检合格"时露面：`ever_ok && online && nalerts == 0`；露面时空态那句收成一行
  （`lv_obj_set_flex_grow(alert_page_empty, 0)`，fnos_ui.c:4318 一带），单独出现时仍整卡居中。
- 色点规则 `warn[]`：阵列不可读/不健康/卷非 live；温度旧值或有通道 ≥ `UK_TEMP_WARM`；
  容器旧值或 `up < ndocker`；采集 `last_status != 200`。**读不到就写状态词 + 黄点，绝不静默显示 0**——
  0 的含义是"看过、确实是 0"，与"没读到"是两件事。

## 2 温度页展开态：实机取证通道（触摸不可自动化的唯一出路）

- 固件：串口命令 `temp <n>`（components/fnos_monitor/fnos_wifi_cli.c:77-92，用法串 :30、开机帮助 :121）
  → `fnos_ui_request_page(4)` + `fnos_ui_request_temp_expand(n)`；回显 `温度页：展开第 %d 台设备（下一跳生效）`。
  公开 setter fnos_ui.c:4904 只置标志（fnos_ui.c:247 `static volatile int s_temp_expand_req`）。
- **落地顺序**（本轮修的坑）：标志原来在 `ui_tick` 顶部落地，而温度块是 `refresh()` 里建的
  （fnos_ui.c:4138 `temp_blk_build(i)`；`temp_groups()` 在 fnos_ui.c:3086 由 :4092 调用；`refresh()`
  在 ui_tick 末尾 :4409）——开机后 p4 从未布局过时 `temp_blk[n].box` 为 NULL，请求被静默丢弃。
  现在落地排在 `refresh()` **之后**（fnos_ui.c:4423-4430），判据与手指点击 `temp_toggle_cb`
  （fnos_ui.c:3197-3204）完全一致：越界 / 无数据 / 只有一路通道都作废，一次性请求不留悬挂状态。
- 主机：`page_shot.py --temp N`（tools/page_shot.py:39-40 参数、:59-60 发送、:65-66 判据）；
  预览断言在温度协议用例末尾（tools/preview/preview.c:2040 展开、:2044 越界 99 不许崩）。
- 实机结果：`temp 0` 把第一台设备（Intel Core i7-8700）展开成 2 列网格（Core 0-5 + Package id 0），
  块脚注变 `7 路 · 34.0-38.0°C · 收起`，其它设备仍"展开"（照片见上表第 2 行）。

## 3 结论带 `%d/%d` 漏屏：主机 84 张样张为什么没抓到

- 症状（真机照片 `2026-10-08-round5-p0-band-bug.png`）：结论带印出
  `无采集告警　容器 9%d/9%d 运行 · 无待处理事件`。
- 根因：把三元表达式选中的**字面量**当成了 `%s` 的实参——

  ```c
  set_txt(s_ui.home_band_msg, "%s", !st->ever_ok ? "等待采集端首次采集"
          : !st->online ? "采集端离线 · 显示最后一次采集快照"
                        : "容器 %d/%d 运行 · 无待处理事件",
          running, st->ndocker);          /* %s 只印第一个实参，%d 就此留在屏上 */
  ```

- 为什么预览没抓到：健康态那一帧走的是 `st->nalerts > 0` 分支（`set_txt(..., "%s", st->alerts[0].m)`），
  带 `%d` 的那条分支当时没有任何样张覆盖，也没有断言。
- 修复（fnos_ui.c:3825-3842）：拆成 else-if 链，健康态走真正的格式串
  `set_txt(s_ui.home_band_msg, "容器 %d/%d 运行 · 无待处理事件", running, st->ndocker)`，
  并留下注释"别把带 %d 的文案塞进 set_txt 的 %s 里当实参"。全仓扫过一遍：只有这一处这么写。
- 两道守护：
  1. 定点断言（tools/preview/preview.c:1417-1418）：结论带必须含 `运行 · 无待处理事件`，
     且屏上不许出现 `%d`；
  2. 全局审计 `audit_format_residue()`（tools/preview/preview.c:762）：遍历可见 label，
     正文里出现 `%d/%s/%u/%02d` 这类没被消费的格式说明符就直接 abort（`%%` 与裸 `%` 放行）。
     它挂在**三个**审计面上：每张样张（:1181）、夜间适配矩阵（:2232）、夜间样张（:3436）——
     这次的教训正是"没被样张覆盖的状态会活到真机上"，所以规则要跟着**所有**审计面走，而不是补一条样张。

## 4 温度磁贴脚注去重

```c
/* 旧：传感器名与温度数值行里已经有一份，抄第二遍会把"容器 N/N"挤出磁贴（DOTS 截断） */
set_txt(s_ui.overview_note, "%s %.0f°C · %s · %s", old?"温度旧值":"最高", hottest, sensor, containers);
/* 新（fnos_ui.c:3885） */
set_txt(s_ui.overview_note, "%s %.0f°C · %s", old_temp ? "温度旧值" : "最高", hottest, containers);
```

实机复核：`最高 75°C · 容器 4/4` 完整显示（照片第 3 行）。

## 5 顺带：工具与误判复核

- `page_shot.py --temp N`（见 §2）；用法行同步。
- 文档里的样张数 83 → 84（新增 `06-alerts-health`）：`docs/ui-iter3-skeleton-2026-10-08.md`、
  `tools/preview/README.md`、`docs/ui-v12-lvgl-native.md`。
- 误判复核：p5 体检块四行名字在 1:1 下看着像居中缩进（`温度/容器/采集` 比 `存储容量` 右移约 38px）。
  实测 `06-alerts-health.ppm` 像素：四行名字墨迹都从 x=38 起、值都从 x=113 起（圆点 x=21-28）
  ⇒ **本来就是左对齐**，代码未动。目视印象不能当版面证据。

## 证据

- 主机回归 `iter4-e`（`bash tools/preview/run.sh .../tools/preview/out/iter4-e`）：EXIT=0、
  9 条 checks 全 PASS、84 张 PNG、86 条对比度记录最低 4.64:1（`06-alerts-health` 5.73:1）、
  见过的构件 1727 / 从未露面 17（基线噪声）、无 Assertion / 越界 / 格式残渣。
- 栈深 `python3 tools/stack_check.py --elf build/fnos_monitor.elf --objdump "$HOME/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-objdump"`：
  app_main 5296/16384=32%、lvgl_worker 2672+5008=7680/12288=62%、poll_task 2512/6144=41%、pair_task 3072/7168=43%，全部在预算内。
- 构建/烧录：`build/fnos_monitor.bin` 7 029 184 B、SHA256 `37e8df45c92b973848f16f622c04c11a01d58e3aa3ef548e4c5ecdb2ee810a27`；
  `./idf.sh -p /dev/tty.usbmodem5CF71088571 -b 230400 flash` EXIT=0、`Hash of data verified`×3、RTS 硬复位；
  串口 35 s 无 panic（健康样本 `ok=25 fail=0`）。
- 长跑（修复版固件，10 分钟串口）：`tools/serial_capture.py --seconds 600` → 0 条 panic/assert/backtrace、
  19 个健康样本、`poll ok=660 fail=0` 15-30 ms、CPU 峰值 6.8%、**内部堆 215 KB→214 KB、PSRAM 21961 KB→21961 KB 无漂移**、
  全程未重启（无新的 `wifi got ip`）。
- 实机照片（`docs/evidence/`，手机相机 App 截图，已 `sips -r 90` 转正 2340×1080）：
  `2026-10-08-round5-p5-health.png`、`2026-10-08-round5-p4-temp-expanded.png`、
  `2026-10-08-round5-p0-band-bug.png`（修复前）、`2026-10-08-round5-p0-band-fixed.png`（修复后）。

## 文件哈希（本轮结束时）

| 文件 | SHA256 | 备注 |
|------|--------|------|
| components/fnos_monitor/fnos_ui.c | `0dd86576697f9718c0852aa0855c8c3822517df41c3c76353c22848f0fe20b1b` | 4905 行 |
| components/fnos_monitor/ui_kit/uk.c | `7760cc94ffc6ab8cc13188bf7f53c89e436ae3d6f1547b5b7275792b0678f4d0` | 本轮未改 |
| components/fnos_monitor/ui_kit/uk.h | `1a815eef2f6ae9c5cb9c59da466a202d5ee91f4e400ff53b1b809f2435f4edac` | 本轮未改 |
| components/fnos_monitor/fnos_ui.h | `83f792d9076e37661c8d3e462884f4a3c7a3df56f0e5c0318b18c3a6314c12a9` | temp 展开 setter 声明 |
| components/fnos_monitor/fnos_wifi_cli.c | `f7dbabae36ee4ed47989e1446045b337a2d293a22554a0da389ff04da96b1453` | `temp <n>` |
| tools/preview/preview.c | `eab8dbf1ed9310ebbdca5591c24671e1a764a430da37f52f6ed66af1bfe05568` | 3476 行 |
| tools/page_shot.py | `09cf7150e6c2145617b6a7a0a97e2de9698a95d69224890741e1ec73c1fea512` | `--temp` |

相对发布克隆 `/Users/llll/code/esp/p4-7b-fnos-monitor-release`（HEAD `ce67298` = 1.2.4）累计差异行数：
fnos_ui.c 897、uk.c 140、uk.h 12、fnos_ui.h 11、fnos_wifi_cli.c 21、tools/preview/preview.c 123、tools/page_shot.py 14。

## 复现命令

```bash
# 主机回归（84 张 + 9 条 checks + 三条审计面）
bash tools/preview/run.sh /Users/llll/code/esp/p4-7b-fnos-monitor/tools/preview/out/iter4-e
# 构建 / 栈深 / 烧录
export IDF_PATH="/Users/llll/.platformio/packages/framework-espidf@3.50503.0"; export ESP_IDF_VERSION=5.5
export FNOS_IDF_PYTHON="$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/python"; ./idf.sh build
python3 tools/stack_check.py --elf build/fnos_monitor.elf \
  --objdump "$HOME/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-objdump"
./idf.sh -p /dev/tty.usbmodem5CF71088571 -b 230400 flash
# 实机取证（含只能靠串口的温度展开态）
export FNOS_SERIAL_PORT=/dev/tty.usbmodem5CF71088571
python3 tools/page_shot.py --page 5            # 告警页体检块
python3 tools/page_shot.py --page 4 --temp 0   # 温度页第 0 台设备展开
```

## 未做 / 仍开放

1. 首页存储磁贴一屏只露一整行卷 —— 设计选择（`uk_viewport_snap` 不会留半行；要露两行页面预算就破）。
2. 实机照片是手机相机 App 截图（有反光与斜拍），只能当"内容与布局"的证据，不能当色彩证据。
3. 长跑只做了 10 分钟（见下），**数小时级 L4 未做**。
4. 未提交、未推送、未发布（AGENTS.md：发布在 `p4-7b-fnos-monitor-release`，且需用户当下授权）。
