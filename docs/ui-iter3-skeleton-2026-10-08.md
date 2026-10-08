# UI 第三轮迭代：全 UI 骨架重排（2026-10-08）

用户诉求（原话）："不需要一屏显示所有信息 只需要突出重点信息 突出重点 就算多页也无所谓 …… 得把骨架硬伤解决掉"，
随后批准"所有 ui 页面都需要修改"。本轮把设备端 LVGL 界面从"五页 + 单列堆叠"重排为
**六页 IA（总览 / 存储 / 网络 / 系统 / 温度 / 告警）+ 12 栅格**，并修掉两个骨架硬伤：
**首页纵向预算超 66px（底行被页底静默裁掉）**、**网络页超 86px（"网口与接口"只剩标题上半行）**。

## 1. 骨架合同

| 项 | 内容 |
|---|---|
| 页目录 | 底栏 dock 六槽：总览 / 存储 / 网络 / 系统 / 温度 / 告警；顶栏设备域（Wi-Fi 状态 / 配对 / 设置 / 信号 / 在线胶囊）；下钻一律覆盖层 |
| 一页一个问题 | 总览=现在健康吗；存储=容量与阵列；网络=链路此刻如何；系统=谁在跑、哪块热；温度=哪一路最热；告警=有什么要处理 |
| 优先级阶梯 | 告警 > 温度越限 > 阵列降级 > 网络 > 容器 > 运行时长（磁贴 span、字号、排序都由它推导；运行时长时钟因此 92px → 62px） |
| 一屏装不下就下钻 | 不缩字号、不裁卡、不藏数据；同卡不在两页重复（首页"容器服务"卡与系统页整列容器卡重复 → 已删） |
| 常驻告警带 | 任何页底栏都显示当前最严重一条，点它进告警页 → 多页不会把告警藏起来 |
| 栅格原语 | `UK_GRID_COLS 12` + `uk_grid_row/uk_grid_span/uk_grid_equalize`；`uk_viewport_snap(box, base_gap)` 把池的整行行距吸附到"看得见的整行"，不露半行也不藏数据 |

## 2. 首页（p0）443px 纵向预算

页可视高 = 600 − 顶栏 − 底栏 = **443px**；页可滚但滚动条关闭（`LV_SCROLLBAR_MODE_OFF`）+ `scroll_top=0`，
⇒ 预算超出的部分被**静默裁掉**，屏幕上看不出"下面还有东西"。

- **根因**：结论带 40 + 资源组 300 + 磁贴行 145 + 2×12 间距 = **509**，超 66px；三张磁贴下半截全被页底切断
  （网络图表、存储 /vol1 行与注释、温度传感器名与脚注全部不可见）。
- **9 处改动**（`components/fnos_monitor/fnos_ui.c`）：
  1. `overview_resource_t` 增 `card` 指针（刷新时反查英雄卡量高度）；
  2. 英雄卡两条脚注（CPU 元信息 + 采集信息）并排成一行（`uk_row_box`）→ −21px；
  3. 删首页「容器服务」卡（与系统页重复）→ −104 −12px；
  4. 存储磁贴卷池 min 去掉 `UK_ROW_MIN` 下限（40 → 25）→ −11px；
  5. 最热温度磁贴传感器名内联进数值行（`flex_grow=1` + `min_width=0` + `DOTS`）→ −21px；
  6. `overview_refresh()` 每帧实测英雄卡子对象高度并写回 `min_height`，资源组 min 取
     `max(英雄卡实测, 内存卡 min)`（不再有历史遗留的额外状态行高度）；
  7. 时钟固定 `UK_FONT_DISPLAY_64`（62px 行高），不再按屏幕分辨率升到 92px；
  8. 删掉旧代码里给资源组白送的 `CJK12 + UK_S1`（21px）；
  9. 页 `pad_row` 12 → 8（末行贴齐页底）。
- **结果（几何探针 `PREVIEW_BANDS_AT=02-live-p0`）**：40 + 249 + 130 + 2×12 = **443**，零溢出；
  英雄卡 body 205px、存储池视口 25px（恰好一整行）、磁贴行 130px。实机照见 §4。

## 3. 网络页（p2）86px 溢出

- **根因**：四个顶层子对象 140 + 119 + 122 + 112 + 3×12 = **529**，超 86px ⇒ 最后一张「网口与接口」被页底裁掉。
- **处置**：判定"网卡信息条"（接口名 + 累计收/发 + 采集耗时 + 成功率）与四张速率 KPI 卡**完全重复** ⇒ 删掉整条：
  删 `build_p2` 的 info 卡建卡块（原 `fnos_ui.c:1340-1366`）、`network_layout` 的 info_row 宽度块、
  `refresh` 的 info 填充（原 `:3919-3932`）、离线复位里的 info 复位，以及 `s_ui` 的
  `if_dot/if_name/info_val[4]` 字段。**数据零丢失**：累计流量在 KPI 注脚（"累计 2.2 TB"）、
  接口名在 kpi2[2] 注脚（eth0）、采集延迟就是 kpi2[3] 的值、成功率与 ok/fail 在底栏。
- **结果**：188 + 119 + 112 + 24 = **443**；实机照里「网口与接口」卡完整显示三行接口。

## 4. 四级证据

| 级别 | 证据 | 结果 |
|---|---|---|
| L1 主机预览 | `bash tools/preview/run.sh <绝对目录>` | **84 张 PNG**、9 条 checks 全 PASS、字形/溢出/重叠/子对象越界四条审计通过、正文对比度最低 **4.64:1**（85 条记录）、无 assert |
| L1 几何探针 | `PREVIEW_BANDS_AT=02-live- ./build/preview <dir>` | 六页全部 ≤ 443px：p0 40+249+130+2×12=443；p1 94+337；p2 188+119+112+24=443；p3 108+177+134=419；p4 90+341；p5 45+386 |
| L2 构建 | `./idf.sh build`（IDF 5.5.3 / esp32p4 rev3.x） | EXIT=0，`fnos_monitor.bin` 7 026 464 B，**SHA256 47162c49bf254022ef1972ac3fcfd319e4d640826755a4301f844bdddc480a74** |
| L2 栈深 | `python3 tools/stack_check.py --elf build/fnos_monitor.elf --objdump …` | 最深回调子树 `ui_tick` 4448 B（其次 `pair_btn_cb` 4304、`diagnostics_cb` 4272）；`app_main` 4688/16384=29%、`lvgl_worker` 2672+4448=7120/12288=58%、`poll_task` 2512/6144=41%、`pair_task` 3072/7168=43% |
| L3 烧录 | `./idf.sh -p /dev/tty.usbmodem5CF71088571 -b 230400 flash` | 写入 0x10000、`Hash of data verified`、RTS 硬复位 |
| L3 串口 | `python3 tools/serial_capture.py --seconds 35` | **0 条 panic/assert/Backtrace**；`poll ok=30 fail=0 23ms cpu=3.3% mem=61.5% internal=219KB psram=21967KB` |
| L3 实机六页 | `python3 tools/page_shot.py --page 0..5` + `capture-board` | 六页全部切页成功并逐页看图，见下 |

**实机逐页观感**（照片归档在 `docs/evidence/2026-10-08-round4-p{0..5}.png`，由手机相机截图旋转校正）：

- **p0 总览**：结论带"无采集告警 · 容器 4/4 运行 · 无待处理事件"；英雄卡 23:52（62px）+ 运行 13d 23h + CPU 4 %
  + 脚注"7 核 · 负载 0.76 · 队列 1 / 180 次采集 · 峰 6%" + sparkline；右列内存 62 %；**三张磁贴全部完整可见**
  （网络吞吐 591 KB/s｜203 KB/s + 双线图；存储容量 14.4 / 26.7 TB + 总条 + /vol2 36% + "6 个卷 · 阵列正常 3/3"；
  最热温度 75 °C + 传感器名 + 脚注）—— 443px 预算在真屏上收口。
- **p1 存储**：总容量 26.7 TB / 已用 14.4 TB / 可用 12.2 TB 大条 + 存储卷（/fs 36%、/vol2 67%…）+ 阵列健康
  （md0/md1/md2 全部"正常"）+ 磁盘活动（nvme3n1 / nvme2n1 / md2 读写速率），无半截卡。
- **p2 网络**：整高吞吐图（量程 0–2.6 MB/s，两条线 + 上行/下行峰注释）+ 四张速率 KPI + **「网口与接口」完整显示三行**
  （br-…af234 down、br-…00e4e1 up 10000 Mbps、br-…373df down）→ 86px 溢出已在真屏修掉。
- **p3 系统**：告警卡收成一条（容器 4/4 运行 · 24 路温度 · 严重 0 · 警告 0 + 负载/进程/交换/运行时长）+
  容器服务三列 + 硬件温度三列（Intel Core i7-8700 35.0、Aquantia atlantic 10GbE 75.0、Intel UHD Graphics 630 35.0）+
  告警与事件"暂无告警事件 / 查看全部 →"。
- **p4 温度**：桃色摘要"75.0 °C 最热 Aquantia atlantic 10GbE · MAC / 24 路传感器 · ≥75℃ 关注 2 路 · ≥80℃ 高温 0 路"；
  设备网格 **3 列 × 2 行 6 台设备**，每台"最热通道 + 温度 + N 路 · 区间 · 展开"。
- **p5 告警**：健康系统下的空态——摘要条"严重 0 · 警告 0 · 提示 0" + "暂无告警事件"，页面留白很多（见已知项）。
- 六页底栏常驻状态行（轮询 1s · ok · fail + 最严重一条）与六槽 dock 在每一页都正常。

## 5. 改动文件与哈希

| 文件 | SHA256 | 相对 release 克隆累计差异 |
|---|---|---|
| `components/fnos_monitor/fnos_ui.c` | `435a597a7751fe10fb9c0548699f70fafc683438f50c1971393115e8f9fa80f2`（4757 行） | 756 行 |
| `components/fnos_monitor/ui_kit/uk.c` | `7760cc94ffc6ab8cc13188bf7f53c89e436ae3d6f1547b5b7275792b0678f4d0`（960 行） | 140 行 |
| `components/fnos_monitor/ui_kit/uk.h` | `1a815eef2f6ae9c5cb9c59da466a202d5ee91f4e400ff53b1b809f2435f4edac` | 12 行 |
| `components/fnos_monitor/fnos_ui.h` | — | 8 行 |

（差异基线 = `/Users/llll/code/esp/p4-7b-fnos-monitor-release`，HEAD `ce67298`「完善自适应 NAS 界面并发布配套插件 1.2.4」。开发仓工作树长期脏，`git diff` 不能当本轮 diff。）

## 6. 已知项与边界

1. **告警页空态很空**：健康系统下只有摘要条 + "暂无告警事件"。要在这一页也"突出重点"，需要引入"最近事件/历史"
   数据源（当前只有活动告警），属下一轮候选。
2. **首页存储磁贴只露一整行卷**：池 min 25px 只够一行（露两行要 ~54px ⇒ 页超预算），完整清单在存储页 —— 设计选择。
3. **最热温度磁贴脚注在窄格被 DOTS 截断**（"容器不…"）：脚注优先级低于数值，属设计选择。
4. **温度页展开态（12 路网格）实机未验**：GT911 触摸无法自动化，实机只能验收折叠态；主机预览 fixture 已覆盖展开态
   （48 路 → 4 列 × 12 行）。
5. **照片是手机相机 App 预览截图**（1080×2340，含斜拍、反光、1X 叠层）：可判版面与可读文字，不能作色彩/闪烁证据。
6. **L4 长跑（≥30 min 看内存/重启）未做**。
7. **未提交、未推送**：AGENTS.md 要求提交/推送需用户当下授权；release 仓仍是 1.2.4。

## 7. 复现命令

```bash
# L1：主机预览（84 张）+ 四条审计 + 对比度
bash tools/preview/run.sh /Users/llll/code/esp/p4-7b-fnos-monitor/tools/preview/out/iter3-c
# L1：几何探针（六页纵向预算）
cd tools/preview && PREVIEW_BANDS_AT=02-live- ./build/preview /tmp/bandsprobe
# L2：构建 + 栈深
export IDF_PATH="/Users/llll/.platformio/packages/framework-espidf@3.50503.0"
export ESP_IDF_VERSION=5.5
export FNOS_IDF_PYTHON="$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/python"
./idf.sh build
python3 tools/stack_check.py --elf build/fnos_monitor.elf \
  --objdump "$HOME/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-objdump"
# L3：烧录 + 串口 + 逐页拍照
./idf.sh -p /dev/tty.usbmodem5CF71088571 -b 230400 flash
python3 tools/serial_capture.py --seconds 35 --out /tmp/serial.txt
export FNOS_SERIAL_PORT=/dev/tty.usbmodem5CF71088571
for n in 0 1 2 3 4 5; do python3 tools/page_shot.py --page $n; done
```
