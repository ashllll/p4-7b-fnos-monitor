# p4-7b-fnos-monitor

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
