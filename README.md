# p4-7b-fnos-monitor

当前界面为手写 C + KKUI 构件的五页监控界面（总览 / 存储 / 网络 / 系统 / 温度），最新布局与验证边界见 [v9 设计与版面合同](docs/ui-redesign-v9.md)，温度页那一轮的起因、采集端契约与踩坑见该文档第十三节。下方标为 v2 的内容是历史记录。

把 **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B**（7 英寸 1024×600 MIPI-DSI + GT911 触摸）做成
**飞牛 fnOS NAS 的状态监视器**：上电直接进仪表盘，4 页数据、触摸导航。

数据来源不是把 NAS 的账号密码塞进固件，而是在 NAS 上跑一个**单文件只读采集器**
（`nas/fnos-agent.py`，Python3 标准库 + systemd），它把 NAS 的现状压成约 2.3 KB 的 JSON，
板子每秒 HTTP 取一次。

```
┌──────────────────────── 飞牛 NAS 192.168.0.119 ────────────────────────┐
│  /proc /sys statvfs /proc/mdstat docker.sock /proc/spl/kstat/zfs      │
│                     │  只读读取，1 Hz 采样                            │
│              fnos-agent.py (systemd, root, 只读加固)                   │
│                     │  http://192.168.0.119:8799/api/v1/status        │
└─────────────────────┼─────────────────────────────────────────────────┘
                      │  纯 HTTP + 小 JSON（不用 TLS：本板内部 RAM 只有 ~361 KB）
┌─────────────────────┼─────────────────────────────────────────────────┐
│ ESP32-P4 7B          ▼                                                │
│  fnos_net.c   Wi-Fi STA（板载 C6 / ESP-Hosted）+ SNTP                  │
│  fnos_data.c  esp_http_client + cJSON → 快照 + 曲线环形缓冲（PSRAM）    │
│  fnos_view.c  LVGL 9.5 四页仪表盘 + 触摸导航/翻页                      │
└───────────────────────────────────────────────────────────────────────┘
```

## 面板上的四页（界面 v2）

设计合同与令牌见 [`docs/ui-redesign.md`](docs/ui-redesign.md)，实机验收记录见
[`docs/verification.md`](docs/verification.md) 第 8 节。要点：

* **字体**：IBM Plex Mono（数值，等宽 tabular，刷新不跳动）+ IBM Plex Sans（拉丁标签）
  + Noto Sans SC 子集（中文标签）。`tools/gen_fonts.sh` 一键重现，中文字形从源码字符串自动提取。
* **状态三层**：身份色（CPU 蓝 / 内存青 / 温度橙 / 网络下行青、上行蓝）、严重度色（正常绿 / 注意黄 /
  危险红）、可信度（在线 / 陈旧 Ns / 采集端离线 / 等待数据）。三者互不覆盖。

| 页 | 第一眼 | 第二眼 | 第三眼 |
| --- | --- | --- | --- |
| 总览 | 健康结论（正常/注意/危险 + 检查数 + **原因行**） | CPU / 内存 / 最高温度 / 运行时长四块遥测（含迷你条） | 6 个存储空间占用一览 + CPU·内存双轨趋势 |
| 存储 | 已用容量 Hero（14.1 TB）+ **容量堆叠条**（按卷总量分段着色） | 6 个卷卡片：用量条 + 80%/90% 阈值刻度 + 已用/总量 | RAID 状态词（正常 n/m、同步 %、降级）+ 磁盘读写活动 |
| 网络 | 下行 / 上行双主值（单位档位带迟滞） | 双轨吞吐趋势（面积纹理 + 亮线 + 右轴刻度） | 累计收发 / 采集延迟 / 曲线采样数 |
| 系统 | 容器清单（状态点 + 状态词 + 容器自带状态） | 7 路温度（热条 + 阈值色：≥60 黄、≥75 红） | **告警列表**（危险/注意逐条列出）+ 采集端点自检 8 行（两列：标签 / 右对齐值） |

顶栏：主机名 + 端点 + 可信度胶囊 + **Wi-Fi 信号条** + 时钟。
底栏：轮询统计 + **告警带**（严重度色点 + 一行原因）。
导航：左侧四项（几何图标 + 中文标签 + 选中态），点选切页；内容区左右滑动翻页（阈值 70 px，原子换页）。
夜间 23:00–07:00 背光降到 12%。

## 板子侧工程结构

```
main/main.cpp                    上电流程：NVS → Wi-Fi → BSP 显示 → UI → 轮询任务 → 夜间背光
components/fnos_monitor/
├── fnos_net.c/.h                Wi-Fi STA（ESP-Hosted）+ SNTP + 断线指数退避重连
├── fnos_data.c/.h               HTTP 轮询、cJSON 解析、快照与曲线环形缓冲
├── fnos_view.c/.h               LVGL 仪表盘（4 页 + 顶部/底部栏 + 触摸）
├── fnos_config.h(.example.h)    Wi-Fi / NAS 地址端口 / token / 时区 / 背光 —— 真实文件不入库
├── Kconfig                      FNOS_HEAP_DEBUG、FNOS_CHART_WINDOW
└── idf_component.yml            cjson + esp_wifi_remote(==1.2.5) + esp_hosted(1.4.*)
components/esp32_p4_wifi6_touch_lcd_7b/   工程内 BSP 副本（唯一改动：LVGL 绘制缓冲放 PSRAM）
components/lvgl_mem_psram/                LVGL 对象/样式的自定义分配器（PSRAM 优先）
```

### 编译 / 烧录

```bash
cd /Users/llll/code/esp/p4-7b-fnos-monitor
cp components/fnos_monitor/fnos_config.example.h components/fnos_monitor/fnos_config.h   # 填 Wi-Fi 与 NAS 地址
./idf.sh build
./idf.sh -p /dev/tty.usbmodem5CF71088571 flash monitor    # 本板串口（CH343P，230400 更稳）
```

`idf.sh` 里 `. /Users/llll/code/esp/use-esp-idf-5.5.3.sh` 之后**必须**补
`export ESP_IDF_VERSION=5.5`：本机 IDF 来自 PlatformIO 包，没有这个变量时
`esp_wifi_remote` 的 Kconfig 被静默跳过，ESP-Hosted 会退回 SPI（本板 C6 是 SDIO），Wi-Fi 起不来。

## NAS 侧：fnos-agent

细节见 [`nas/README.md`](nas/README.md)。一句话版本：

```bash
cd nas
SSHPASS='<ssh 口令>' NAS_SUDO_PASS='<sudo 口令>' ./install.sh      # 安装并启动
SSHPASS='...' NAS_SUDO_PASS='...' ./install.sh uninstall           # 卸载
```

* 只读：只读 `/proc`、`/sys`、`statvfs`、`/proc/mdstat`、`docker.sock`、`/proc/spl/kstat/zfs/arcstats`，
  不写任何文件；systemd 单元带 `ProtectSystem=strict / ProtectHome / PrivateTmp / NoNewPrivileges`
  等只读加固，实测常驻内存 ~11 MB。
* 端点：`/api/v1/status`（约 2.35 KB）、`/api/v1/history`（最近 300 个采样，供板子重启后回填曲线）、
  `/api/v1/health`（带 `ts`/`age_s`，可判断"采集是否还活着"）、`/`（浏览器可直接看的实时页面）。
* 采样与请求解耦：后台线程 1 Hz 采样，HTTP 只返回快照，实测响应 ~16 ms。
* 可选 `FNAS_TOKEN`：设置后要求 `?token=` 或 `X-Token` 头匹配。

## 已知边界（都不是 bug）

* **界面文字是 ASCII**：LVGL 内置 Montserrat 只有 ASCII 字形，装一份中文字体要多几 MB 且要生成子集。
  采集器输出的告警文案因此也统一成英文（`RAID md127 degraded`）。
* **拿不到 SATA 机械盘温度**：NAS 上没装 `smartctl`，且 sda–sdh 没有 `drivetemp` hwmon 节点；
  温度页显示的是 4 块 NVMe、CPU、网卡、核显。要机械盘温度得在 NAS 装 smartmontools。
* **没有用 netdata**：NAS 上 netdata(:19999) 正在跑，但它的 `disk_space` 只看得到 `/` 与 `/config`，
  各存储池剩余、RAID、Docker 都没有；而且 `allmetrics` 一次 319 KB。所以自己采一份小的。
* **fnOS 自身的私有 API 没有使用**：走的是本机 `/proc`/`statvfs`/`docker.sock`，不依赖 fnOS 版本，
  也不需要把 NAS 账号交给任何容器。
* **md127 目前是 degraded**（`broken raid1`，只剩 nvme1n1p1）——这是 NAS 的真实状态，
  仪表盘底部会一直挂着红色告警；镜像成员是否要重建由你决定。

## 沿用 p4-7b-unifi-app 的板级结论

这块板子的显示/触摸/内存坑在资料仓 `../../esp32-p4-wifi6-touch-lcd-7b/README.md` 与
`../p4-7b-unifi-app/README.md` 有完整记录，本工程直接沿用：

1. **`touch_flags` 必须全 0**（`swap_xy=0, mirror_x=0, mirror_y=0`）：adapter 只旋转 framebuffer，
   输入路径没有任何坐标变换，官方示例那组镜像在本板 + `ROTATE_180` 下等于多做一次 180° 翻转。
2. **撕裂规避用 `TRIPLE_PARTIAL`**（`ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI`），
   配 `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=3`；官方 brookesia 示例的 `DOUBLE_DIRECT` + 2 个 buffer 起不来。
3. **内部 RAM 只有 ~361 KB**：LVGL 绘制缓冲（工程内 BSP 一行补丁 `.use_psram = true`）、
   LVGL 对象（`lvgl_mem_psram`）、LVGL/轮询任务栈全部搬 PSRAM；大数组用 `EXT_RAM_BSS_ATTR`
   （需要 `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`，否则该宏静默失效）。
4. **数据链路走纯 HTTP**：上游 UniFi 版是 HTTPS，mbedTLS 缓冲默认吃内部 RAM，实测跑 ~170 秒后
   内部分配打光 → 界面冻结 + 触摸失效。本工程不碰 TLS，从根上避开；
   `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC/DYNAMIC_BUFFER` 也照旧留着以防其它组件用到 TLS。
5. **改 `sdkconfig.defaults` 不会覆盖已生成的 `sdkconfig`**：改完要 `rm sdkconfig` 重新生成。
6. **自定义字体必须 `--no-compress`**（`tools/gen_fonts.sh` 已固定）：lv_font_conv 默认输出 RLE 压缩字体，
   而 LVGL 9.5 的解压器用的是**一份全局状态**（`LV_GLOBAL_DEFAULT()->font_fmt_rle`，无锁），
   本工程又开着 `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`（两个 tile 两个线程并行绘制）——
   两个线程同时解压字形就会互相踩状态，表现是**字体随机碎裂 + 闪烁**。LVGL 内置 Montserrat 未压缩，
   所以只有换成自定义字体后才会暴露（详见 `docs/verification.md` 第 9 节）。
7. **中文标签必须用带中文的字库**：界面混排 Plex（拉丁）与 Noto（中文），
   把中文写进只有拉丁字形的标签会画成豆腐块。改完跑一遍审计脚本
   （扫"拉丁字体 + 字符串含中文"的调用点，含动态 `ck_set` 目标）应输出 0。

## 当前交付状态（2026-09-20）

* NAS 上 `fnos-agent.service` 已安装并 enabled，端口 8799，重启 NAS 后自动起来。
* 板子上已烧录本工程固件，开机直进仪表盘，实测：
  * 13 分钟长跑 720 次轮询、0 崩溃/断言/看门狗，内部 RAM 稳态 234~244 KB；
  * 四页版式用手机摄像头逐页核对通过，触摸导航/滑动翻页由用户实机确认（`docs/verification.md` 第 4 节）；
  * 把采集器停掉 20 秒 → 面板立刻转红 `OFFLINE` + `AGENT UNREACHABLE`，重启后自动恢复。
* 当前烧录的版本把 `CONFIG_FNOS_HEAP_DEBUG` 开着（串口每 10 秒一条堆余量 + 每 30 秒一条轮询统计），
  长期摆放观察时很有用；不想要就把它改成 n 重新编译烧录。

## 验证记录

见 `docs/verification.md`（编译、烧录、串口、视觉验收、离线恢复与长跑数据）。


## 开发板 ↔ 飞牛应用：通信与配对（一步步）

> **先纠正一个最容易误解的点**：配对码是 **NAS 的应用管理页生成**的，开发板只负责**输入**这 6 位数字。
> 板子这一侧没有任何东西需要"填回"应用包里 —— 板子不进固件、不进 fpk，参数是运行时存进它自己的 NVS。

### 1. 两条链路（板子只做一件事：每秒 HTTP GET 一个 JSON）

```
未配对（装上就能用）      板子 ──HTTP 明文、无令牌──▶  http://<NAS>:8799/api/v1/status
                          开发版采集器（systemd + /usr/local/bin/fnos-agent.py）

已配对（更严的那条路）    板子 ──HTTPS + Bearer 令牌──▶  https://<NAS>:8798/api/v1/status
                          飞牛应用「飞牛监控」（包用户身份运行，只读采集）
```

两条线**并存、互不干扰**：应用不监听 8799、不改动 systemd 单元；开发版也不认令牌。
板子选哪条，就看它 NVS 里有没有配对记录。

| | 未配对 | 已配对 |
| --- | --- | --- |
| 地址来源 | 编译期 `components/fnos_monitor/fnos_config.h`（`FNOS_HOST`/`FNOS_PORT`，本机是 `192.168.0.119:8799`） | NVS（`fnos_pair` 保存的 host/port/tls/token/证书） |
| 传输 | 明文 HTTP | HTTPS（固定板子自己取回的那张证书） |
| 认证 | 无 | `Authorization: Bearer <令牌>`（NAS 侧只存 `sha256("nsc:"+token)`） |
| 左栏「配对」入口 | 橙色（未配对） | 正常色（已配对） |

### 2. 烧录之后的初始状态

1. 首次上电 `fnos_pair_init()` 读 NVS —— 空的，于是**退回编译期默认参数**（明文、无令牌），
   行为跟以前完全一样，面板直接出数据；
2. 想改默认地址（比如让新板子默认就打应用）：`cp components/fnos_monitor/fnos_config.example.h
   components/fnos_monitor/fnos_config.h` 改 `FNOS_HOST`/`FNOS_PORT`，再 `./idf.sh build` + 烧录。
   注意 `fnos_config.h` 在 `.gitignore` 里（它要放 Wi-Fi 口令），别提交。

### 3. 配对六步（左边是你在哪儿操作）

| # | 在哪 | 做什么 | 实际发生的事 |
| --- | --- | --- | --- |
| 1 | **NAS 桌面** | 打开「飞牛监控」→「**设备配对**」→ 生成配对码 | `POST /api/pairing/new` 生成 **6 位码，5 分钟有效**（`ttl=300`），同一时刻只允许一个码 |
| 2 | **开发板** | 左栏点「**配对**」→ 数字键盘输入这 6 位 → 按「**确认**」 | `fnos_pair_begin(code)`：先在**明文**连接上 `GET http://<NAS>:8798/api/v1/identity`（NAS 只在这一个接口接受明文），取回证书 PEM |
| 3 | 开发板 | （自动）算指纹 | 板子**自己**解 base64 + SHA-256（`pem_fingerprint()`），不信服务器 JSON 里给的指纹字段 |
| 4 | 开发板 | 屏幕显示指纹 **4 行 × 8 字节**、证书 CN 与到期日 | 状态 `FNOS_PAIR_CONFIRM`，等你表决 |
| 5 | **你** | 把板子屏幕上的指纹与 NAS 管理页「**传输加密**」里的指纹**逐段比对**；一致点「**确认**」，不一致点「**关闭**」 | 点关闭 = `fnos_pair_confirm(false)`，**不会发出任何带令牌的请求** |
| 6 | 开发板 | （自动）换令牌并保存 | `POST https://<NAS>:8798/api/v1/pair {"code":"…","name":"p4-7b-lcd"}` → 拿到令牌 → 写 NVS → `fnos_pair_generation()+1`，数据层重建客户端，此后每秒走 HTTPS |

配对码错/过期时，板子会显示 NAS 返回的原因（例如 `没有正在进行的配对，请先在 NAS 管理页生成配对码`），
回第 1 步重新生成即可。

### 4. 为什么指纹非要人工比对一次

板子固定的是"**这一张**证书"，而"这一张是不是你那台 NAS"只能当面确认：中间人可以同时伪造证书和
JSON 里的指纹字段，但伪造不了你眼睛看到的 NAS 管理页。所以指纹由板子自己算、由你比对，比对通过前
不发任何带令牌的请求 —— 这是一次性的信任建立，之后板子只认这枚证书（不做公共 CA 链校验）。

### 5. 解除配对 / 换 NAS / 回默认

- **开发板**：「配对」页 → 「**解除配对**」**按两次**才生效（防误触）→ 清 NVS，立刻退回默认链路；
- **NAS**：管理页把该设备从列表里删掉 → 它的令牌立即失效（两边都做才算干净）；
- 换 NAS：先在旧 NAS 上删设备，再按上面六步重新配对即可。

### 6. 排错

| 现象 | 多半是 | 怎么办 |
| --- | --- | --- |
| 板子显示"没有正在进行的配对" | 码没生成 / 超过 5 分钟 / 已被用过一次 | 管理页重新生成，立刻在板子上输入 |
| 板子一直转圈 / 提示"配对请求没连上" | 请求没走通（证书、端口、请求体、服务端 5xx 都可能） | 串口敲 `tls`：它明文取证书 → `GET /health` → `POST` 一个假码，把失败的**那一层**指出来（2026-10-06 靠它定位到 POST 没发请求体） |
| 板子上「确认」后停在取证书 | 板子连不到 `8798`，或 NAS 上应用没启动 | 先看应用中心里应用是否运行；`curl http://<NAS>:8798/api/v1/identity` 有 JSON 才算通 |
| 指纹两处不一致 | 板子拿到的是别人的证书（或 NAS 重装过应用、证书重生成了） | 点「关闭」，核对 `curl -s http://<NAS>:8798/api/v1/identity` 里的 `tls_fingerprint` |
| 配对成功但数据不变 | 板子还在读 8799（那也正常，两条线都在跑） | 「系统」页或「温度」页页头会显示数据源；想只走应用就在应用侧停掉开发版采集器 |
| 想彻底回退 | —— | 板子「解除配对」+ NAS 管理页删设备；或重烧固件（NVS 会被清） |

### 7. 相关代码

| 位置 | 作用 |
| --- | --- |
| `components/fnos_monitor/fnos_pair.c` / `.h` | 状态机（未配对/取证书/等确认/换令牌/已配对/失败）、指纹计算、NVS 读写、`fnos_pair_forget()` |
| `components/fnos_monitor/fnos_ui.c` | 「配对」卡片：数字键盘（`删除`/`确认`）、指纹四行显示、`关闭`/`解除配对`/`确认` 三个按钮 |
| `components/fnos_monitor/fnos_data.c` | 按 `fnos_pair_active()` 拼 URL、按 `fnos_pair_generation()` 重建 HTTP 客户端 |
| `nas/fpk/nasscreencompanion/app/server/nas_companion_server.py` | `new_pair_code(ttl=300)`、`try_pair()`、`/api/v1/identity`（明文引导）、`/api/v1/pair` |
| `docs/fnos-companion-app-plan.md` | 这套配对协议当初的设计与信任模型推演 |


## 下载 / 安装

**飞牛应用包**（装到 NAS 上的那个，给开发板/副屏提供只读状态与逐路温度）：

- 稳定下载地址（GitHub Release，永远指向最新版）：
  <https://github.com/ashllll/p4-7b-fnos-monitor/releases/latest/download/nasscreencompanion-1.1.0.fpk>
- 仓库里也带一份：`nas/fpk/nasscreencompanion.fpk`
- 安装：飞牛桌面 → **应用中心 → 手动安装 → 上传该 fpk**；或在 NAS 上
  `sudo appcenter-cli install-fpk nasscreencompanion-1.1.0.fpk`（依赖 `python312`）
- 装完在飞牛桌面打开「飞牛监控」→ 管理页；开发板配对流程见
  [issues 与 docs](docs/fnos-market-submission.md#5-与开发版并存的关系评审会被问到的点)

**开发板固件**：`./idf.sh build` 后 `idf.py -p <串口> flash`（本机为 ESP32-P4 rev v3.2）。


## 开源协议

**Apache License 2.0** —— 继承自同作者的原始项目 `T-Display-S3-fnos-monitor`
（同一个"飞牛监控"产品的上一代硬件版本），全文见 [LICENSE](LICENSE)。

仓库内第三方组件各自的协议：

| 组件 | 协议 |
| --- | --- |
| `components/esp32_p4_wifi6_touch_lcd_7b/`（Espressif / Waveshare BSP） | Apache-2.0 |
| LVGL（`managed_components/`，由组件管理器拉取，不入库） | MIT |
| cJSON（ESP-IDF 内置） | MIT |
| ESP-IDF | Apache-2.0 |
| 界面字体（`tools/fonts/` 下的 Noto Sans SC、Inter） | 字体原文件不入库，只分发生成后的 C 字体文件 |

`nas/fnos-agent.py`（NAS 只读采集端）与 `nas/fpk/`（飞牛应用包工程）同样适用 Apache-2.0。
