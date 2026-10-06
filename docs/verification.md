# 验证记录（p4-7b-fnos-monitor）

环境：ESP32-P4-WIFI6-Touch-LCD-7B（实测 rev v3.2，MAC `e8:f6:0a:e8:ba:68`），
调试口 `/dev/tty.usbmodem5CF71088571`（CH343P），ESP-IDF 5.5.3（PlatformIO 包 + `use-esp-idf-5.5.3.sh`）。
NAS：飞牛 fnOS（Debian 12，内核 6.18.18.c1032-trim），12 核 / 31.8 GB，6 个存储空间，4 组 md RAID。

## 1. NAS 侧采集器

| 项目 | 结果 |
| --- | --- |
| 安装 | `nas/install.sh` → `/usr/local/bin/fnos-agent.py` + `/etc/systemd/system/fnos-agent.service`，enabled + active |
| 常驻 | `MemoryCurrent≈11.9 MB`，`NRestarts=0`（连续运行 >40 分钟无重启） |
| 响应 | `/api/v1/status` 2334~2336 B，响应 0.6~16 ms（NAS 本机 0.6 ms，局域网内首次 16 ms） |
| 历史 | `/api/v1/history` 稳定 300 行；板子开机后 "history backfilled: 300 samples" |
| 只读 | systemd 加固 `ProtectSystem=strict / ProtectHome / PrivateTmp / NoNewPrivileges / RestrictAddressFamilies`；代码无写路径 |
| 内容核对 | `vols` 6 项（/、/fs、/vol2、/vol3、/vol4、/vol5，已按设备去掉 /tmp、/var/tmp 这两个 bind mount）；`raid` md0/md1/md2 OK + md127 broken；`temps` 7 项；`docker` 4 项（netdata/qbittorrentee/qdrant 运行中、fnos-exporter 已退出）；`zfs` ARC 与命中率；`alerts` 命中 md127 degraded |

## 2. 编译与烧录

* `./idf.sh build`：`fnos_monitor.bin` 0x18d640 B（1.63 MB），9 MB app 分区剩余 83%。
  首次配置解析 23 个依赖（lvgl 9.5、esp_lvgl_adapter 0.6.4、esp_wifi_remote 1.2.5、esp_hosted 1.4.7、cjson 1.7.19 等）。
* 烧录：`./idf.sh -p /dev/tty.usbmodem5CF71088571 -b 230400 flash`（230400 稳定；460800/921600 在本板会
  `Invalid head of packet`）。1.63 MB 用时约 40 s，Hash 校验通过。

## 3. 串口启动日志（`logs/boot-verify.log`）

```
I (2477) transport: Slave chip Id[12]                 ← 板载 C6（ESP-Hosted SDIO）
I (3518) fnos_net: wifi station started; awaiting ip
I (3518) esp_lvgl:adapter: LVGL adapter initialized successfully
I (3537) ek79007: version: 2.0.2                       ← 7 寸 MIPI-DSI 面板
I (3741) ESP32_P4_EV: Touch 0x5d found                 ← GT911 在 0x5D
I (3769) esp_lvgl:touch: Touch input device registered successfully
I (3782) ESP32_P4_EV: Setting LCD backlight: 45%
I (3791) main: heap display        internal=244KB largest=204KB dma=206KB psram=27623KB
I (3895) fnos_view: dashboard created (4 pages, 180 s window)
I (3897) fnos_data: polling http://192.168.0.119:8799/api/v1/status every 1000 ms
I (3917) main: backlight 45% (day)
I (7523) fnos_net: got ip 192.168.0.214
I (8167) fnos_data: history backfilled: 300 samples
I (37748) fnos_data: poll ok=30 fail=0 19ms cpu=3.0% mem=62.0% rx=39.6 tx=1104.0 KB/s alerts=1
```

* 内部 RAM 稳态 **244 KB 空闲（最大块 204 KB）**、DMA 206 KB、PSRAM 27.5 MB —— 比 Brookesia 版
  （211 KB）还宽裕：没有系统外壳，而且这条链路不跑 TLS。
* 唯一警告是 BSP 的 `ledc: GPIO 32 is not usable`（与背光 PWM 复用有关，实测背光正常）。

## 4. 视觉验收（手机摄像头 + `read_image`）

逐页拍照核对（`CONFIG_FNOS_AUTO_PAGE_SEC=9` 临时打开自动轮播，验收后已改回 0 重新烧录）：

| 页 | 核对到的内容 |
| --- | --- |
| OVERVIEW | CPU 2%(load 0.44 12 cores) / MEMORY 59%(18.4/31 GB) / HOTTEST 74C(cpu 32C) / UPTIME 9d 08h；CPU-MEM 双曲线在动；温度榜 NIC 74C、NVME0 44C、NVME3 38C、iGPU 33C、CPU 32C；6 个存储占用条 34/59/65/5/0/10%；底部红色 `RAID md127 degraded (broken)` |
| STORAGE | `6 volumes 26.7 TB total 14.1 TB used`；6 行 `挂载点/文件系统/占用条/百分比/已用-总量`（18 GB/63 GB、7.0 TB/11.9 TB、6.9 TB/10.6 TB、18 GB/383 GB、0 GB/2.7 TB、92 GB/949 GB）；RAID ARRAYS：md1 OK 2/2、md0 OK 1/1、md127 **DEGRADED broken**（红）；DISK ACTIVITY：sde/sdb/sdg 各约 R 344 KB/s；ZFS ARC 13 GB hit 95% |
| NETWORK | DOWNLOAD 6.6 KB/s（total received 350 GB）、UPLOAD 96.5 KB/s（total sent 145 GB）；双曲线 + 自适应纵轴刻度 200/100/0.0 KB/s；`interface enp1s0-ovs poll 1 s agent 7 ms samples 318` |
| SYSTEM | CONTAINERS：netdata/qbittorrentee/qdrant 绿点 up、fnos-exporter 红点 `Exited (255) 11 days ago`；TEMPERATURES 8 项；AGENT/LINK：host nas、endpoint 192.168.0.119:8799、http 10 ms (status 200)、poll 29 ok / 0 err、last error none、data age 0 s、wifi 192.168.0.214 -5x dBm |

同一页两张相隔 ~10 s 的照片里曲线形状、CPU/内存数值、温度都在变 → 界面确实在持续刷新，不是冻结画面。

**触摸（用户实机确认）**：点左侧四个导航项能切页、高亮跟随；在内容区左右滑动能翻页。
摄像头没法替人确认"手指点下去对不对"，这一项由用户亲自核对（对应的坐标标定沿用了同板同参数的
`touch_flags` 全 0 结论，见 p4-7b-unifi-app README）。

## 5. 离线/恢复路径（实测）


做法：在板子正常运行时，把 NAS 上的 `fnos-agent` 停掉 20 秒再启动，前后各拍一张照片。

| 阶段 | 面板上看到的东西 |
| --- | --- |
| 采集中断 20 s | 状态丸变红 **OFFLINE**；底部红字 **`AGENT UNREACHABLE: connect/timeout`**；`poll … ok 42 err 8 api 0 ms retrying`；`age 28s`；数值保持在最后一帧（CPU 2%、MEM 60%、温度榜），配合 OFFLINE/数据年龄明确表示"这是旧数据" |
| 采集恢复 | 状态丸回绿 **ONLINE**；底部恢复为 NAS 自身告警 `RAID md127 degraded (broken)`；`poll … ok 68 err 8 api 21 ms live`、`age 0s`；数值立刻刷新（CPU 3%、load 0.33） |

结论：断链有明确指示、不会假装数据是新的；恢复后无需重启板子，自动继续轮询。

## 6. 长跑（13 分钟串口，`logs/soak.log`）

| 指标 | 实测 |
| --- | --- |
| 时长 / 轮询 | 774 s，720 次成功轮询（1 Hz，无堆积） |
| 失败次数 | 9 次，全部来自第 5 节那次刻意停机（`connect/timeout`），停机结束后自动恢复 |
| 轮询耗时 | 9~32 ms（HTTP 请求 + cJSON 解析） |
| 崩溃/断言/看门狗 | 0（`panic|assert|abort|watchdog|Guru` 全为 0） |
| 界面刷新 | 相隔 10 s 的两张照片里曲线、CPU/内存、温度均在变化 |
| 内部 RAM | 开机 244 KB → 稳态 234~244 KB（第二段长跑用 `CONFIG_FNOS_HEAP_DEBUG=y` 每 10 秒记录，见 `logs/soak2.log`） |

## 7. 代码评审与修复（两轮独立只读评审）

写完第一版后，分别派了两个只读评审：一个看固件 C 代码，一个看 NAS 采集器 Python + 部署脚本。
下面记录**已修**的真实缺陷（评审里判定为"风格问题"的没有采纳）。

### 固件侧

| # | 缺陷 | 后果 | 修法 |
| --- | --- | --- | --- |
| 1 | LVGL 定时器里调 `fnos_net_rssi()`，本板 Wi-Fi 在 C6 上，`esp_wifi_sta_get_ap_info()` 是 ESP-Hosted **同步 RPC**（超时 5 s） | 界面与触摸（同任务）最多卡 5 秒 | RSSI 改为缓存：`fnos_net_poll_rssi()` 在轮询任务里刷（5 s TTL），UI 只读内存 |
| 2 | 重连定时器回调（esp_timer 任务，LVGL tick 也在其中）里直接 `esp_wifi_connect()` | 同一套同步 RPC，重连时界面整体停住 | 回调只置标志，`fnos_net_service()` 在轮询任务里调 |
| 3 | 图表纵轴峰值扫描把 `LV_CHART_POINT_NONE`(=INT32_MAX) 当数据（`lv_chart_add_series` 会这样初始化整条序列） | 曲线没填满前纵轴刻度出现 "1953125 MB/s" 这类值，曲线被压成一条线 | 扫描时跳过哨兵 |
| 4 | `http_get()` 不校验 `fetch_headers` 返回值与响应完整性 | keep-alive 连接里残留 body 会污染后面若干轮轮询（静默、粘滞） | 头失败/未收全 → `http_drop_client()` 重建 |
| 5 | `parse_history()` 写环形缓冲没拿锁，且 seq 从 0 重排 | 会冲掉刚写入的实时样本、UI 可能读到新旧混合 | 加锁 + 接在当前 seq 之后 |
| 6 | SYSTEM 页 AGENT/LINK 面板高度算错（`CONT_H-280=198` 装 10 行） | 最后两行（psram/charts）被父对象裁掉，永远看不见 | 温度面板 205 + 端点面板 `CONT_H-205=273`，行距 20 |
| 7 | STORAGE 页下半区只剩 112 px（6 行 × 52） | RAID 第 4 行（md2）与磁盘第 4/5 行被裁 | 行高 44、固定 6 行、RAID/磁盘行距 21，面板按实际行数定位 |
| 8 | 历史回填失败后 `hist_done` 已置位 | 一次失败就永不重试（静默） | 只有解析成功才置位 |
| 9 | 锁超时与"没有数据"共用一个返回值 | 锁竞争瞬间整屏数值变 `--`、徽标变 NO DATA | 单独的 `s_valid` 标志 |
| 10 | `s_ip` 由 Wi-Fi 事件任务写、LVGL 任务读 | 可能显示错 IP | `portMUX` 保护 + 读侧拷到静态缓冲 |
| 11 | 概览页存储数值与进度条重叠 3 px | 数字底部被压 | 数值 y 62→56、进度条下移 4 px |
| 12 | 轮询任务缓冲分配失败自删但 `s_task` 未清 | 之后永远无法重启轮询 | 自删前置 `s_task = NULL` |
| 13 | `fnos_net_start()` 失败无重试路径 | 起站失败即整机无数据直到重新上电 | `fnos_net_service()` 里周期重试 |

### 采集器侧

| # | 缺陷 | 后果 | 修法 |
| --- | --- | --- | --- |
| 14 | HTTP/1.1 keep-alive 没设空闲超时 | 板子掉线留下的半开连接永久占线程+fd，累积后端点彻底不可用 | `Handler.timeout = 15`，`request_queue_size = 64`，`handle_error` 静音 |
| 15 | `statvfs` 会作用在 NFS/SMB/fuse 挂载上 | 对端一挂就进 D 态，采样线程永久冻结、端点还返回 ready:true 的旧数据 | 跳过网络/融合文件系统 |
| 16 | 用墙钟算速率差值 | NTP 回拨时速率变成天文数字并写进曲线 | 差值改用 `time.monotonic()` |
| 17 | docker 非 200 时用空表覆盖缓存 | 容器列表与"掉线"告警静默消失 | 失败保留上一份 |
| 18 | docker 数组无上限（板子接收缓冲 8 KB） | 容器多时整帧超限被丢，面板永久停最后一帧 | 上限 16 条 + 名称/状态截断 |
| 19 | 启动时 `HTTPServer.server_bind()` 做反向 DNS | DNS 不可达时阻塞几十秒，systemd 已 active 但端口没监听 | 覆写 `server_bind` 跳过 getfqdn |
| 20 | `sample()` 无逐段隔离 | 一段异常整帧 `ready:false`，面板冻结而 /health 仍 ok | 逐段 try/except 保留上一份 + `errors` 字段 |
| 21 | `/proc/stat` 取到 guest/guest_nice | 它们已计入 user/nice，total 重复计入 → CPU% 偏低 | 只取 user..steal |
| 22 | 按设备去重卷 | 同一设备上的 btrfs 子卷/多数据集会被合并消失 | 改为按挂载点前缀过滤 |
| 23 | mdstat 解析把第二个字段当级别、`(F)/(S)` 标记丢失、check/scrub 被算成故障 | 面板显示垃圾级别、分不出坏盘、健康阵列校验期间显示异常 | 级别单独正则 + 保留成员标记 + ok 只由 `[n/m]`/`[U_]` 决定 |
| 24 | install.sh 用空数组展开 | macOS 自带 bash 3.2 + `set -u` 下"公钥免密"路径 100% 失败 | 改成 `run_ssh()/run_scp()` 函数 |
| 25 | 待安装文件经世界可写的 `/tmp` 中转 | 本地任意用户可在 sudo 前替换，等于 root 任意代码执行 | 改用 0700 的 `mktemp -d` 中转目录，用完删除 |
| 26 | token 常量时间比较、`Access-Control-Allow-Origin: *`、CPU 温度只认 Intel `coretemp` | 见评审原文 | 用 `hmac.compare_digest`、删 CORS 头、按 hwmon 名字判 CPU |

修完后的复核（真机）：

* NAS 采集器重新部署后 `/api/v1/status` 2352 B / 16 ms，`/api/v1/health` 现在返回
  `{"ok":true,"ts":…,"age_s":1,"host":"nas"}`；`raid` 四条阵列都带正确级别与成员（md2 之前解析不出来）；
  `cpu` 里 `runq=1 procs=1932` 语义正确；`errors: null`（无失败段）。
* 端口现在**先绑定再等首帧**：systemd 报 active 的瞬间端口就可连（之前会有约 2 秒"active 但连接被拒"）。
* 固件侧：STORAGE 页四行 RAID（含 md2）与五行磁盘活动全部可见；SYSTEM 页端点信息 10 行全部可见；
  开机后曲线未填满期间的纵轴不再出现荒诞刻度。

## 8. 界面改版 v2（2026-09-26，含实机逐页验收）

设计合同见 [`ui-redesign.md`](ui-redesign.md)。这一版解决的是"信息层级 / 状态编码 / 实机可读性"，
不是加装饰。改动与证据：

| 项 | v1 | v2（实测照片核对） |
| --- | --- | --- |
| 字体 | LVGL 内置 Montserrat（ASCII） | IBM Plex Sans/Mono + Noto Sans SC 子集；数值用等宽 tabular（`tools/gen_fonts.sh` 可复现，中文字形从源码字符串自动提取） |
| 标签语言 | 全英文 | 中文标签 + 拉丁术语（CPU / KB/s / RAID / HTTP） |
| 总览 | 四块数字 + 曲线 | **健康结论 Hero**（正常/注意/危险 + "6 项检查 · 2 条告警" + 原因行）→ 四块遥测 → 存储一览 → CPU/内存双轨趋势 |
| 存储 | 6 行用量条 | 容量 Hero（14.1 TB 已用）+ **容量堆叠条**（按卷总量分段着色）→ 6 个卷卡片（含 80%/90% 阈值刻度）→ RAID 状态词 + 磁盘活动 |
| 网络 | 单页双曲线 | DOWN/UP 双主值 + 累计 → 双轨趋势（面积纹理 + 亮线 + 右轴刻度）→ 累计/延迟/采样四格 |
| 系统 | 容器 + 温度 + 键值 | 容器（状态点 + 状态词）→ 7 路温度（热条 + 阈值色）→ 采集端点自检 8 行 |
| 状态编码 | 单一颜色 + 文案 | 身份色 / 严重度色 / **可信度**三层分离：在线 / 陈旧 Ns / 采集端离线 / 等待数据 四种胶囊状态 |
| 导航 | 纯文字 | 几何图标 + 中文标签 + 选中态（左侧 3px 条 + 底色） |
| 顶栏 | 主机 + 胶囊 | 主机 + 端点 + 胶囊 + **Wi-Fi 信号条** + 时钟 |
| 底栏 | 轮询统计 + 告警文字 | 轮询统计 + 告警带（严重度色点 + 一行原因） |

修掉的真实缺陷（这一轮实机照片发现的）：

1. **页面坐标错位**：`apply_page()` 里把页面位置归零，而页面原本是挂在屏幕上的 (124,80) 偏移处 ——
   结果每次切页都把整页内容挪到屏幕左上角，压住顶栏和导航。修法：加一个内容容器，页面挂在容器里，
   归零 = 回到内容区原点。
2. **趋势糊成实心色板**：180 个 1 Hz 采样点 1:1 画成柱 + 30% 不透明度 → 密集柱连成一块实心色带。
   修法：隔点入图（2 秒一个点，窗口仍是 3 分钟）+ 柱间 2px 缝 + 面积层降到 20%。
3. **动画/切页残留**：v2 明确采用原子换页（先隐藏旧页并归零偏移，再显示目标页，最后整屏失效），
   不在 partial buffer 上做双页位移动画。

验收证据（手机固定机位逐页拍照 + 肉眼判读，`logs/ui-v2-boot.log` 为对应串口记录）：

* 顶栏/导航/底栏在四页上位置正确，无内容压住 chrome；
* 总览：`危险` + `6 项检查 · 2 条告警` + 红色原因行 `RAID md127 degraded (broken)`；
  四块遥测 2% / 55% / 76C / 1d 15h 与采集端数值一致；
* 存储：14.1 TB Hero + 堆叠条 + 6 个卷卡片（含阈值刻度）+ RAID 四行（md127 红字"降级 broken"）；
* 网络：DOWN 2.2 KB/s / UP 8.x KB/s + 双轨趋势（刻度 100 / 50.0 / 0.0）+ 四格上下文；
* 系统：5 个容器（mcpcat-app / netdata / qdrant 运行中，fnos-exporter 与 qbittorrentee 已退出，红点 +
  容器自带状态文本）、**7 路温度全部可见不裁切**（NIC 75°C 红、NVMe 33–45°C、iGPU/CPU 35°C）、
  端点自检 8 行（主机 / 端点 / HTTP 6 ms 状态 200 / 轮询 27 正常 0 失败 / 最近错误无 / 数据年龄 0s /
  Wi-Fi 192.168.0.214 -37 dBm / 内部内存 235 KB）；
* 串口：`dashboard v2 created (4 pages, 180 s window)`、`history backfilled: 300 samples`、
  `poll ok=30 fail=0 17ms`、内部 RAM 稳态 236 KB（v1 是 239 KB，多出来的是字体与控件）。

仍未做（留给下一轮）：滑动切页过渡动画、告警历史页、趋势柱再稀一档（现在 2 s/点在 7 寸上仍偏"条形码"）、
夜间配色（现在只降背光不换配色）。

> 复核路径说明：v4.2 那轮四页版面是用 `CONFIG_FNOS_AUTO_PAGE_SEC=12` 的临时构建逐页拍照核对的，
> 交付固件把该值改回 0（唯一差别是那个自动轮播定时器）。2026-10-02 的交付复核轮又完整走了一遍
> "关掉轮播 → 重建 → 烧录 → 串口 + 实机复拍"（数据见 §13.2）。

## 9. "字体花屏闪烁"的两个真因（2026-09-26，用户实机报告后定位）

用户报"字体花屏闪烁"。逐项取证后确认是**两个独立缺陷**叠加，都已修复并复验。

### 9.1 RLE 压缩字体的解码器是全局状态，和 2 个并行绘制线程打架 → 字形碎裂

* 现象：数字/字母随机碎裂、每次重绘碎的位置还不一样（所以看起来在闪）。
* 证据链：
  1. 我生成的字库描述符是 `.bitmap_format = 1`（RLE 压缩），而 LVGL 内置 Montserrat 是 `0`（未压缩）
     —— 这也解释了为什么 v1（用内置字体）干净、换成自定义字体后才出问题。
  2. LVGL 9.5 的解压器跑在**一份全局状态**上：`src/core/lv_global.h` 里的
     `LV_GLOBAL_DEFAULT()->font_fmt_rle`，`rle_init()/rle_next()` 都读写它，
     `font/fmt_txt/lv_font_fmt_txt.c` 里**没有任何锁**。
  3. 本工程 `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`，`lv_display.c` 会把一帧拆成两个 tile
     交给两个线程并行绘制；两个线程同时解压不同字形就会互相踩状态。
* 修法：`tools/gen_fonts.sh` 里所有 `lv_font_conv` 调用加 `--no-compress`（描述符变回 `bitmap_format = 0`，
  不再走那条全局解压路径）。代价：11 个字库从约 1.2 MB 源文件涨到 1.9 MB，固件从 1.58 MB 涨到 1.87 MB
  （app 分区仍剩 82%）。脚本头部写了完整的踩坑说明，避免下次重新生成时又踩回去。

### 9.2 中文写进了"只有拉丁字形"的字体 → 豆腐块

* 现象：`NAS □□□□`、`正常 □ / □□ 0`、`TB / □□` 这类方框。
* 原因：界面混排两种字体（Plex 拉丁 / Noto 中文），有几处标签建的时候用了拉丁字体，
  后来 `ck_set()` 写进去的却是中文。
* 修法：先把 4 处改对（概览瓦片副标题、底栏轮询行、存储 Hero 单位行、网络页累计行），
  再补了一个**静态审计脚本**（扫"拉丁字体 + 字符串含中文"的调用点，含动态 `ck_set` 目标），
  当前结果 0 处。

### 9.3 复验

`CONFIG_FNOS_AUTO_PAGE_SEC=12` 临时构建下逐页拍照（每张间隔 ~9 s）：

* 总览：`NAS 持续运行`、`负载 … 核`、底栏 `采集 … 正常 68 / 失败 0 · 7 ms` 全部正常显示，无方框；
* 存储：`TB / 总量`、`可用 12.5 TB`、RAID `正常 2/2` / `降级 broken`、磁盘活动 4 行全部正常；
* 网络：`累计接收 54 GB` / `累计发送 309 GB` / `采集延迟 7 ms` / `曲线采样 368` 全部正常；
* 四页在两张相隔数十秒的照片里同一段文字像素一致（不再有随机碎裂）。

交付固件已把自动轮播关掉重新烧录，并再抓一次串口稳态确认（2026-10-02 再次确认：见 §13.2）。

## 10. 布局异常修复轮（2026-09-26 晚，用户报告"存储/总览界面布局存在异常"）

> 方法说明：这一轮主模型换成不带图像能力的 mimo-v2.6-pro，`read_image` 被准入判断拒绝
> （`inputModalities` 缺失）。照片判读改由图像能力子代理（deepseek-flash）完成：
> 它对照片做 90° 正转 + deskew 后逐块放大判读，并用像素剖面量化间距/重叠量。
> 结论仍需按"实机肉眼 + 固定机位照片"的证据等级理解。

### 10.1 根因：底栏文字画到了屏幕顶部

`build_bottom()` 把底栏文字挂在**屏幕**上、y 写 11/12/16 —— 而底栏面板本身在 y=560。
结果整行"采集 … / 告警"画在**屏幕顶部**，压住顶栏：两个视觉评审都独立报到
"红色告警压住在线胶囊"、"小字采集与 nas 重叠"。修法：文字挂到底栏面板对象上，y 相对面板。

### 10.2 同轮修复的其它问题

| # | 问题 | 修法 |
| --- | --- | --- |
| 1 | 系统页「采集端点」8 行 22px 行距把最后一行顶到面板底边（0 下边距） | 行距 20 + 起点 36；并改成两列（标签左、值右对齐），顺带解决右半部 45% 空白 |
| 2 | 系统页「容器」空槽留孤立圆点 | 空槽整个隐藏（圆点透明度归零） |
| 3 | 总览/网络页数值与单位贴脸（2px）或单位飘在瓦片最右端 | 新增 `place_unit()`：按数值实际宽度把单位贴到右侧 10px；网络页单位下移 8px |
| 4 | 存储页容量堆叠条被 `update_storage` 挪回旧坐标 y=92，压住单位标签 | 与创建坐标统一为 y=128 |
| 5 | 堆叠条总宽没减段间缝（5×3px）→ 溢出面板；最小段 6px 像杂点 | 总宽 840→825；最小段 8px |
| 6 | RAID/磁盘面板第 4 行被下边裁掉 | 面板上移（344→336）、行距 21→20 |
| 7 | 磁盘活动数值小数位不统一（176 / 72.0） | 固定 `R %.0f W %.0f` |
| 8 | RAID 行序是组装顺序（md1/md0/md127/md2） | 采集端按设备名排序 |
| 9 | 导航选中态太弱（照片上不可辨） | 选中底色提亮一档（CK_NAV_SEL）+ 指示条 3→4px |
| 10 | 系统页容器面板下方 34% 空白 | 加"告警"分隔线 + 告警列表（把危险/注意的原因逐条列出来） |

### 10.2b 终审回报后的二次修复

终审确认 A/B/F 三项已修复（底栏归位、胶囊不再被压、堆叠条不越界），但报出新问题：

| # | 问题 | 修法 |
| --- | --- | --- |
| 1 | 总览页指标块的单位完全没画出来（裸数字 6 / 52 / 74） | 不再用 `lv_obj_get_self_width` 测量定位，改成固定偏移 x=85（mono34 字宽 21px、值最多 3 字符）——测量路径实测不可靠，宁可固定 |
| 2 | 温度文案 `NIC 74C` 缺度数符 | 副标签与系统页温度值补 `°`（cjk15 子集含 U+00B0） |
| 3 | 总览页写"2 条告警"却只列 1 条 | 原因行补 `(+N)` 后缀；完整列表在系统页告警面板 |
| 4 | 网络页"累计收发"上下两处重复 | 下方四格改为 峰值下行 / 峰值上行 / 采集延迟 / 曲线采样 |
| 5 | 柱状趋势"从顶边向下长" | 核对 LVGL 源码（`draw_series_bar` 以 `obj->coords.y2` 为基线向上画）→ 判为照片倾斜判读误差，不改 |

### 10.2c 单位"消失"的根因（串口取实证，查了三轮）

单位标签始终不显示，两轮视觉复核都说"数字右侧是纯底色"。静态读代码读不出问题，
最后**在串口里打印标签的真实坐标**才定位：

```
uidbg: unit0 x=278 y=53 w=14 h=16 hidden=0 text='%' par_w=209
```

* 标签确实被创建、文字是"%"、没有隐藏 —— 但 **x=278**，而它的父瓦片只有 209 宽：
  被推到面板外 69px，LVGL 裁掉 → 肉眼看就是"单位没了"。
* 根因：`ck_label_r()`（内部调 `lv_obj_align(TOP_RIGHT)`）创建的标签，之后
  `lv_obj_set_pos(85, 53)` **只对 y 生效、x 不生效**（实测 y=53 与调用一致、x=278 与调用不符）。
  对照组：顶栏端点用普通 `ck_label` 创建、之后 `place_unit` 移动 —— 移动正常。
* 修法：单位标签改用普通 `ck_label` 直接放在 (85, 53)，不再走 align 路径。
  串口复测 `unit0 x=85 y=53 w=14 style_x=85 par_w=209` ✓ 在面板内。

> 经验：**"看不见"的问题先在串口里打印对象的真实几何**，比对着照片猜快一个数量级。
> `lv_obj_get_x` 返回的是相对父对象的坐标（`coords.x1 - parent->coords.x1`），与 `style_x` 对照
> 就能判断是"样式没生效"还是"坐标被算错"。另外 LVGL 9 里 `lv_obj_t` 是不透明类型，
> 调试打印不能访问 `obj->coords`，只能用 `lv_obj_get_x/get_width` 这类公开 API。

### 10.2d 系统页三项修复的复核 + 温度单位缺字形

系统页复核（照片由视觉子代理判读）：**C/D/E 三项全部已修复**——
「采集端点」两列 8 行值右对齐、末行距面板底边 15px 未裁切；容器列表末尾孤立圆点消失
（连通域扫描确认）；分隔线 + "告警" + 2 条告警（红 RAID md127 / 黄 qbittorrentee）完整。
整体判定"布局已修复可用"。

但复核报出一个新的**缺字形**问题：温度单位 ° 显示成空心方框（「75.0▯C」，系统页 7 行）。
根因查字体 cmap：`ui_font_mono_20` 只覆盖 `0x20-0x7E`（ASCII），**没有 U+00B0**；
而 cjk_15 的稀疏表从 0xB0 起（含 °），所以总览页副标签的 "NIC 75°C" 一直是好的
（此前复核已确认 ° 渲染清晰），只有走等宽字体的系统页温度值会缺。
修法：`tools/gen_fonts.sh` 给 mono 52/34/20 三个字库补 `-r 0xB0 -r 0xB7`，
重新生成后 mono_20 的 cmap 出现第二段 `range_start=176, range_length=8` ✓ 覆盖 ° 与 ·。

### 10.3 视觉终审

（报告见提交记录；覆盖：顶栏/底栏不再重叠、端点面板两列 8 行完整、空槽消失、告警列表出现、
堆叠条与数字有间距且不溢出、单位紧跟数值。）

## 11. 体验打磨（第三轮）

在"布局已修复可用"之后做的一轮抛光：

| 项 | 做法 |
| --- | --- |
| 网络瓦片右侧 65% 空白 | 瓦片内加**迷你趋势**（203×96，与下方 3 分钟详图同刻度）：即时"在涨还是在跌"的纹理 |
| 趋势序列身份不可区分 | 两个趋势面板标题行加**图例**（色块 + 序列名：CPU/MEM、DOWN/UP） |
| 告警列表行距偏紧 | 28 → 30px（末行 438..456，面板 464） |
| 夜间只降背光不换配色 | `fnos_view_set_night()`：纯黑底（#000000）+ 面板更暗（#0B0E12）+ 趋势纹理透明度 20%→10%，配合背光 12%。**不做整屏 alpha 合成**（opa_layered 每帧走全屏图层，这板子绘制预算有限） |
| 切页无过渡 | **导航指示条 180ms ease-out 滑动**到新项；页面本体仍原子换页。不做整页 translate/淡入——全屏页 + partial buffer 上双页合成会留残影（技能文档的已验证故障模式） |

## 12. 已知问题与不做的事

* **qBittorrent 任务状态未接 —— 根因是容器自己在崩溃重启，不是采集端的问题**：
  `docker logs qbittorrentee` 在刷屏 `QtLockedFile::lock(): file is not opened`，
  `ps` 里 `qbittorrent-nox --webui-port=8080 --profile=/config` 一闪就没（s6 反复拉起），
  容器内 `/proc/net/tcp` 只有 BT 端口在听、8080 从未 LISTEN，所以在 NAS 上和容器内部
  `curl :8080/api/v2/app/version` 都是"连接被重置"。
  可见的权限问题：`/config/qBittorrent/config` 目录属主是 `root`，而进程以 `abc` 运行（`/config/qBittorrent` 本身是 `abc`）。
  修法（需要停容器 + `chown -R abc:abc`，属于改用户的容器，未执行）：修好后 WebUI 才能接进来。
  仪表盘目前只显示该容器在不在跑（`Up 9 days`，因为 s6 一直在重启子进程）。
* **机械盘温度拿不到**：NAS 未安装 `smartctl`，sda–sdh 也没有 `drivetemp` hwmon；温度页只有
  4 块 NVMe + CPU + 网卡 + 核显。
* **界面文字为 ASCII**：LVGL 内置 Montserrat 只有 ASCII 字形（见 README 说明）。
* **md127 是 NAS 的真实状态**（broken raid1，只挂着一个成员），不是固件误报。

## 13. 交付复核轮（2026-10-02 下午：代码复审 + 字体审计工具 + 关掉轮播）

### 13.1 复审范围与结论

逐个文件人工细读（配合 `ocr scan` 整文件扫描）：`fnos_ui.c`、`ui/FnosDashboardController.c`、
`ui/FnosDashboardViewAnim.c`、`kk_ui/kk_widgets.c`、`kk_ui/kk_theme.h`、`kk_ui/kk_rect.c`、`main/main.cpp`、
`tools/ui_gen.py`、`tools/ui_validate.py`、`fnos_data.c`、`fnos_net.c`。

* **数据层无实质问题**：`fnos_data.c` 的 `http_get` 三条错误路径都会 `http_drop_client()`（不残留 keep-alive
  连接污染后续轮询）、数组解析全部带 `FNOS_MAX_*` 边界、cJSON hooks 走 PSRAM 且失败回退内部 RAM；
  `fnos_net.c` 的 esp_timer 回调只置 `s_connect_wanted`，真正的 `esp_wifi_connect()` 由轮询任务发出
  （不阻塞 LVGL 的 esp_timer 任务），`s_rssi` 是 `volatile int8_t`。
* **界面层**：`kk_widgets.c` 里 `t->bar` / `t->line` 的每次解引用都有判空（313-316、336-337、352-353、358）；
  `kk_swipe_attach` 的 `lv_malloc` 只在视图构造期发生 4 次（`fnos_dash_view.generated.c:4440-4443`），长跑无泄漏。

本轮修掉的 3 个真问题：

| # | 问题 | 修法 |
| --- | --- | --- |
| 1 | `fnos_ui.c:129` 还留着 6 秒一次性调试探针 `probe_cb`（v4.2.2 冻结事故的主角），交付固件里照跑 | 删除函数与 `lv_timer_create(probe_cb, …)` |
| 2 | `tools/gen_fonts.sh` 扫中文字形时漏了 `bindings.json`（5 个字段默认值含中文：等待数据 / 无告警 / 等待 / 等待首个数据帧 / TB / 总量） | 清单补上 bindings.json、`kk_widgets.c`、`main.cpp` |
| 3 | v3 有的"拉丁字体写中文"审计在 v4 丢了（v4.2 的"前"字豆腐块就是这么漏出去的） | 新增 `tools/audit_fonts.py`（见 §13.3） |

顺手清理：删除 9 个僵尸字库（≈1 MB，不在 `CMakeLists.txt` 的 `PROJ_SRCS` 里，却被 `fnos_fonts.h` 的 glob
声明着），`gen_fonts.sh` 增加"生成后清理非本次产出"步骤；字库从 11 个降到 10 个（cjk_13/17/24/40、
num_17/28/44/56、txt_13/15）。

### 13.2 关掉轮播（交付态）

* `sdkconfig:2547`：`CONFIG_FNOS_AUTO_PAGE_SEC` **12 → 0**。该宏只有两处使用：`main/main.cpp:61-68`
  的 `auto_page_cb` 与 `main/main.cpp:120-123` 的 `lv_timer_create(...)`，整块都在
  `#if CONFIG_FNOS_AUTO_PAGE_SEC > 0` 里 —— 关掉后连定时器都不存在（Kconfig 里这本来就是"仅视觉验收"的开关）。
* 重建：`./idf.sh build` → EXIT=0，`build/fnos_monitor.bin` 1816544 B；烧录：
  `./idf.sh -p /dev/cu.usbmodem5CF71088571 -b 230400 flash` → FLASH_EXIT=0。
* 串口 50 s（`logs/delivery-nocarousel.log`）：`VERIFY MODE` **0 次**、`task_wdt`/`Guru Meditation`/断言 **0 次**、
  `poll ok=30 fail=0 12ms`、内部 RAM 稳态 217 KB（启动首帧 230 → 稳定 217）、PSRAM 27314 KB 无下降。
* 实机复拍（手机相机预览截图，两张相隔 36 s）：时钟 15:39 → 15:40、CPU 2% → 8%、
  负载 1.27/0.62/0.49 → 2.06/0.90/0.58、曲线出现新峰 —— **UI 活性确认**；
  页面仍停在总览、左侧导航指示条不动 —— **轮播确已关闭**。

### 13.3 `tools/audit_fonts.py`：审计内容与自身的三个坑

审计项：A 字库 `.bitmap_format` 必须为 0（RLE 会花屏）；B Text 节点的实际文案（locKey 各语言 + 字段默认值，
字体按父链继承）必须被其字库覆盖；C Controller `SETS/SETS_F` 格式串里的中文必须被目标字段节点的字库覆盖；
D 全仓 UI 源码的中文字形至少被某个"参与构建"的字库覆盖；E 僵尸字库；F 字段缓冲（`maxLen`）预算。

调试这个脚本时它自己踩了三个坑，都是"静默失效 → 假绿"，已全部修正并加了自检：

1. `strip_comments_keep_literals()` 只保留字面量，`SETS(...)` 的调用语法一起被丢掉 → 正则永远匹配不上
   （检查 C/F 一直是死代码）→ 新增 `strip_comments_keep_code()`（注释换空格、代码原样保留）。
2. 正则 `SETS_F?\(` 里的下划线是硬字符（只有 `F` 可选）→ 只匹配 `SETS_F`，20 多处 `SETS(FtPoll, …)` 被整片漏掉
   → 改为 `SETS(?:_F)?\(`。
3. `field_macro_table()` 把捕获组（已经是去掉 `FNOS_DASH_FIELD_` 前缀的字段名）又切了一次前缀 → 展开成空串，
   存储/RAID/告警整组行漏检 → 直接使用捕获组。
4. 收尾自检：一条 `SETS/SETS_F` 都没解析到就直接报错（"别信这个 PASS"），避免以后再次静默失效。

F 检查据此发现 3 处字段缓冲偏紧并已放宽：`FtPoll` 80 → 96、`SyCnt` 24 → 40、`SyKvVal2` 24 → 40
（生成物 `fnos_dash_store.generated.h:302/435/499`）。

### 13.4 OCR 第二轮评审未完成（环境原因，如实记录）

* 本机 `git` 当前不可用：`git --version` 打印 "You have not agreed to the Xcode license agreements.
  Please run 'sudo xcodebuild -license'..."（exit 69）→ `ocr review`（diff 模式）直接报
  "not a git repository"。构建同理受影响，须 `export DEVELOPER_DIR=/Library/Developer/CommandLineTools`
  才能跑 `./idf.sh build`。
* 改用 `ocr scan`（整文件、不需要 git）跑三路（固件 9 文件 / NAS 5 文件 / 数据层 4 文件）：
  三个进程均 0% CPU 长时间挂起（16 分钟后仍无输出文件），判定为 provider 端无响应，已终止。
  本轮复审以人工逐文件细读 + 自建审计工具完成。

## 14. 界面 v5 迭代（2026-10-02 晚：密度优先 + 大数字 + fnOS 术语对齐）

**范围**：在 v4 语法不变的前提下做六组改动（设计推导与逐项落点见 `docs/ui-kk-iteration-v5.md`）：
V5.0-A P0 三张 KPI 主值 28→44px、V5.0-B 存储行补 `已用 … · 可用 …`、V5.0-C P2 五块瓦片数值 17→28px、
V5.0-D 弱色正文与"数据 idle 色"分离（`KK_TEXT5 0x7B8BA1`，3.07:1 → 4.83:1）、V5.1 趋势卡峰值行、
V5.2 四条 fnOS 术语（系统状态 / 阵列状态 / 硬盘活动 / 固件内存）。

**静态验证链**（全绿）：`tools/text_width.py --layout`（0 问题）→ `bash tools/gen_fonts.sh`（10 字库 1.7 MB，
cjk_13 补 硬/固/件）→ `python3 tools/ui_gen.py` → `python3 tools/ui_validate.py`（nodes 384→393、fields 275→284、
bindings 279→288、lint 0 warning）→ `python3 tools/audit_fonts.py`（0 error / 0 warning）→ `./idf.sh build`（EXIT=0）。

**manifest 完整性复核**（对 HEAD 做节点级 diff）：新增 9 个节点（`OvVolUse0..5`、`OvPeakCpu/OvPeakMem/OvPeakTemp`）、
零删除；改动集中在 4 张 KPI 卡内部几何 + 图例三行行距 30→46 + P2 五块瓦片 + 17 个弱色文本节点，
其余 300+ 节点逐字未动。

**实机验证**：交付固件 `build/fnos_monitor.bin` 1823696 B（19:27，`CONFIG_FNOS_AUTO_PAGE_SEC=0`），
烧录后串口 45 s `logs/v5-final-nocarousel.log`（`poll ok=30 fail=0 45ms cpu=2.2%`、internal 221KB、
psram 27304KB、无 watchdog/断言）；**复位重启捕获** `logs/v5-final-boot.log`（142 行，从
`[4.08] fnos_ui: ui created (pages=4)` 起完整启动链，`VERIFY MODE` 0 次 = 该固件未建自动翻页定时器）。
逐页拍照用临时轮播构建（8 s，原 sdkconfig 备份后恢复）取证：P0 两帧相隔 38 s 同页同钟点而轮询计数在走、
P1/P2/P3 全部无越界/截断/豆腐块/空槽（P2 实测 `双向合计 157 KB/s`、`曲线采样 288`、`采集延迟 9 ms`；
P3 温度栏 7 条全满、`固件内存 221 KB`）。结论与台账见
`components/fnos_monitor/ui/Source/FnosDashboard/validation.md`（Runtime=Verified）。

**新增工具**：`tools/text_width.py`（读生成字库 `adv_w` 的真实字宽核对，`--layout` 遍历全部 Text 节点）。
它的第一件功劳就否掉了 lint 的乐观估宽：`NwTotalVal` 原 158px 框放不下 `118.00 MB/s`（162.3px）。

## 15. v5.1 修复（2026-10-05：删掉系统页温度卡两条全高竖刻度）

**用户报障**（原文）："系统界面 温度模块有两条竖线bug 需要处理解决 一直在显示着"。

**定位**：P3 温度卡（`SyTempPanel` 320×432）内只有两个 1px `Divider`：`SyTTick60`/`SyTTick75`，
尺寸 **[1, 378]**、位置 [172,44]/[183,44]。它们是 v4.2 B7"温度栏 60/75 档刻度"的遗留物，
x 坐标正确（温度条卡内 x=126..202、76px 宽 ⇒ 60% = 171.6、75% = 183），**长度是错的**：
378px 恰好等于 10 行行高之和，于是 1px 灰线（Divider 默认 `#334052`）从 y=44 一直拉到 y=422，
纵穿每一行的行底与条轨；右对齐数值文字框卡内 x=148..228（`73.0°C` 实占 ~183..228）紧贴刻度线，
线又静止不动 ⇒ 实机读作"两条一直在的竖线 / 渲染残迹"，而不是刻度。
分级信息本来就有两条通道：数值文本（`73.0°C`）与条的分级色（`KK_BAR_WARM/FULL`，实测 71°C 走黄、31°C 走青）。

**修法**：删除这两个节点（脚本 `/tmp/patch_ui_v51.py`：先断言父节点、几何、无子节点、四类源文件无引用，
再写盘；改动前 `layout.json` 备份 `/tmp/v51_backup/layout.json`）。`layout.json` −1456 B / −50 行；
生成物 `fnos_dash_view.generated.c` −18 行、`fnos_dash_view.generated.h` −2 行。
**容量页不动**：`StVolTick60*/85*` 是每行 1×18 的短刻度，被约束在行内、读作刻度（见 §7 B7 注记）。

**静态链**（全绿）：`python3 tools/ui_gen.py` → `python3 tools/ui_validate.py`（nodes 393 → **391**，
fields 284 / bindings 288 / lint 0 warning 不变）→ `python3 tools/audit_fonts.py`（0 error / 0 warning）
→ `python3 tools/text_width.py --layout`（111 条文案 0 问题）→ `./idf.sh build`（EXIT=0）。
台账 `validation.md` 在拍照前先置 Runtime=Pending，实机确认后回写 Runtime=Verified。

**实机复验**：仍按 §14 的取证法——临时把 `CONFIG_FNOS_AUTO_PAGE_SEC` 改 8（原 `sdkconfig` 备份
`/tmp/sdkconfig.v51_off.bak`，diff 只有这一行）→ 构建 1823488 B → 烧录 → 唤醒手机相机（`adb shell input tap`）
→ 每 3–4 s 抓帧（`/tmp/v51p_1..26.png`）。P3 命中 `/tmp/v51p_21.png`（03:15；`轮询 118 · 失败 0 · 24 ms · 数据 0s 前`）：
温度栏 7 条（NIC 71.0°C、NVME2 41.9°C、NVME3 37.9°C、iGPU 31.0°C、CPU 31.0°C、NVME1 29.9°C、NVME0 27.9°C）
的条与数值干净，放大裁切 `/tmp/v51_temp_zoom.png` **确认无任何纵向穿行线**；对照修复前 `/tmp/v5p_5.png`
与 `/tmp/p3_temp_zoom.png` 可见同位置灰线。同页其余元素（采集端点卡 12 行含 `固件内存 221 KB`、容器 4/4、
`无告警`）无回归。改回 `0` 后重新构建 + 烧录：**交付固件 `build/fnos_monitor.bin` 1823440 B（03:17，
`CONFIG_FNOS_AUTO_PAGE_SEC=0`）**；串口 `logs/v51-final-nocarousel.log`（143 行）从
`[1.23] main_task: Calling app_main()` 到 `[4.09] fnos_ui: ui created (pages=4)` 完整启动链，
**`VERIFY MODE` 0 次**、无 `task_wdt`/断言/Guru，稳态 `poll ok=30 fail=0 11ms cpu=1.6% mem=41.2%`、
`internal=225KB largest=184KB dma=187KB psram=27304KB`。

## 16. 主机预览（2026-10-05 下午：把"改 JSON→烧录→拍照"换成 8 秒离线渲染）

**动机**：v4.2/v5 每一轮视觉验收都是"改 manifest → `ui_gen.py` → `./idf.sh build` → 烧录 → 唤醒手机相机 → 抓帧"，
一轮十几分钟，且受拍照角度、反光、相机画幅漂移影响（§14/§15 的证据都要先判"板子在不在框里"）。
`tools/preview/` 把**设备端真实的那套 UI 代码**（同一份 LVGL 9.5 + 同一份生成字库 + `kk_widgets.c`/`kk_rect.c` +
View/Store/Binder/Controller）编译到 macOS，只替身板级接口（`stub/` 里的 `esp_timer.h`/`esp_heap_caps.h`/`esp_log.h`/
`sdkconfig.h`/`fnos_config.h`），fixture 直接填 `fnos_status_t` ⇒ 渲染出的就是会烧进板子的像素，
`bash tools/preview/run.sh` 一次出 4 页 × 3 状态（live/offline/warming）共 12 张 1024×600 PNG。

**入库前的自洽性验证**：`git worktree add --detach /tmp/prev-clean HEAD`（= `a45c394`）+ 软链 `managed_components` +
拷 `build/config/sdkconfig.h` ⇒ `run.sh` 构建成功并产出 **12/12 PNG**（`preview: 4 page(s)/state` + `ppm2png: 12 file(s)`），
证明入库状态不依赖任何本地残留；增量重跑 **8 s wall**（渲染本身 0.45 s CPU），`tools/preview/build` 49 MB（`build/` 已在 `.gitignore`）。

**离线交叉验证 §15 的修复**：`01-live-p3.png` 里温度卡 10 行**无任何纵向穿行线**，而修复前同一位置可复现灰线 ⇒
报障修复在离线渲染上也可见；`01-live-p0.png`（系统状态/告警卡、三张 KPI 卡、存储 `已用 … · 可用 …` 六行、
趋势卡、导航与底栏）与 §14/§15 的实机照片逐项一致。

**已知边界**（详见 `tools/preview/README.md`）：① 只认生成物，改 manifest 必须先 `ui_gen.py`；② 字库是真子集，
改文案要重跑 `gen_fonts.sh`；③ fixture 里手写的告警文案是假造中文，可能落在子集外显示方块——真实采集器
`nas/fnos-agent.py:420-444` 的告警**全是 ASCII 英文**（`"%s %s %.1f%%"`、`"RAID %s degraded (%s)"`、
`"%s free %.1f%% left"`、`"%s used %.0f%%"`、`"CPU temp %.0fC"`、`"MEM used %.0f%%"`、`"LOAD high %.2f"`、
`"container %s down"`），不要把 fixture 的方块当固件豆腐块 bug；④ 主机三处 Kconfig 必须偏离设备
（`LV_USE_OS 0`、libc malloc、`LV_DRAW_SW_DRAW_UNIT_CNT 1`，语义等价替身），且断言改成 stderr + `abort()`
（设备端默认 `LV_ASSERT_HANDLER` 是 `while(1);`，断言失败表现为无声 100% CPU 死循环——当初 `./build/preview`
"卡死"就是它）；⑤ 不能替代实机：触摸/手势、刷新率、PSRAM/DMA 采样路径、真实 Wi-Fi 时序仍要烧录验证。

**流程落点**：`tools/preview/`（含 `README.md`）入库；`docs/ui-kk-authoring.md` §H 增第 7 条（改完版面先看预览再烧录）；
`docs/ui-kk.md` §2 工具树补 `preview/`；`.gitignore` 补 `tools/preview/out/` 与 `*.ppm`。
完整验收链变为：`ui_gen.py` → `ui_validate.py` → `audit_fonts.py` → `text_width.py --layout` → **`preview/run.sh` 看图** → 烧录 + 实机取证。

---

## 17. 删除 ui/ 生成框架，界面改为手写 C + kk_ui（2026-10-05）

**用户指令（原文）**："只需要保留 kkui 其余的 ui 框架全部删除"；追问后的选择：**现在就物理删除**，
并且**要我按 kk_ui 写骨架**（保证固件能编译、屏幕有画面、preview 能跑）。

### 17.1 删除范围与备份

| 删除对象 | 规模 |
| --- | --- |
| `components/fnos_monitor/ui/`（Source JSON + Generated + Controller/ViewAnim） | 23 个跟踪文件 / 30,635 行 |
| `tools/ui_gen.py` `tools/ui_validate.py` `tools/text_width.py` `tools/audit_fonts.py` | 4 个脚本 |

删除前整份备份到 `/tmp/v6_backup/`：`wip_full.patch`（`git diff --binary HEAD`，含另一会话全部未提交改动）、
`ui_snapshot/`（23 文件）、`fnos_monitor_snapshot/`（2.4 MB 组件快照）、`README.txt`（还原命令）。
还原：`cp -a /tmp/v6_backup/ui_snapshot/<相对路径> …` 或 `git apply /tmp/v6_backup/wip_full.patch`。
**保留**：`components/fnos_monitor/kk_ui/`（`kk_theme.h` / `kk_rect.c/h` / `kk_widgets.c/h`）、`fonts/`、
`tools/gen_fonts.sh`（改文案来源）、`tools/preview/`（改编译清单）、`fnos_data.c` / `fnos_net.c`。

### 17.2 骨架（`components/fnos_monitor/fnos_ui.c`，手写）

四页 = 左 rail（`KK_RAIL_W`）+ 顶栏（`KK_HEAD_H`：主机 / `采集 <ip>:<port> · 本机 <ip>` / 信号条 / 状态胶囊三态
"等待数据 / 在线 / 离线"）+ 内容区；P0 总览（四张 KPI 卡 + 存储六行分级条 + CPU/MEM 双线趋势）、
P1 存储（卷 + 阵列状态 + ZFS + 硬盘活动）、P2 网络（上/下行/合计/延迟 + 流量双线 + 容器）、
P3 系统（温度十行分级条 + 采集端点七行 + 告警）。刷新 `ui_tick` 500 ms：`fnos_data_get()` 快照 +
`fnos_data_hist_read()` 灌 `kk_series_t` → `refresh()` 刷全部页；`fnos_ui_set_page()` 仍是原子换页。
`fnos_ui.h` 增 `FNOS_UI_PAGE_COUNT 4`（`main/main.cpp` 的三处调用无需改动）。

### 17.3 见证与踩坑

- **字库来源改写**：`tools/gen_fonts.sh` 的中文字形清单从"strings.json + bindings.json + Controller + ViewAnim +
  生成头"改为**只扫界面源码的字符串字面量**（`fnos_ui.c/h`、`kk_ui/kk_widgets.c`、`main/main.cpp`），重建 10 个字库（1.2 MB）。
- **字体规则（新）**：`ui_font_num_*` / `ui_font_txt_*` **只含 ASCII**，含中文的标签必须用 `ui_font_cjk_*`。
  据此修 8 处（顶栏端点行、KPI 副行、四条趋势图例值、P2 大卡副行），阵列行的"正常/降级"画在百分比位
  ⇒ 运行时对该 label 覆盖 `ui_font_cjk_13`。`—`(U+2014) 不在任何子集里（显示方块），6 处占位符改回 ASCII `-`。
- **行宽两级**：普通行 = 名字 130 + 详情 + 百分比 70；长文案行（硬盘"读 x · 写 y"、容器状态）用
  `row_full_detail()` 铺满并隐藏百分比位（否则被 `LV_LABEL_LONG_DOT` 截成 `写 1...`）。
  踩坑：该助手最初写 `lv_obj_set_pos(detail, x, lv_obj_get_y(detail))`——`lv_obj_get_y()` 是**绝对坐标**，
  导致详情行整体下移错位；改成只设 x 与宽度。
- **真 bug：实机内容区一片黑（预览看不见）**。首版 `fnos_ui_create()` 把 `s_page = -1; fnos_ui_set_page(0);`
  放在 `s_created = true;` **之前**，而 `fnos_ui_set_page()` 见到 `!s_created` 直接 return ⇒ 四页全部停在
  `LV_OBJ_FLAG_HIDDEN`，实机只有左导航与顶栏，内容区全黑；**预览掩盖了它**，因为 `run_state()` 会逐页
  `fnos_ui_set_page(p)` 再截图。修法：先置 `s_created = true` 再激活首页（并给预览加了"create 后、
  任何 set_page 之前"的初始帧快照 `01-live-init.png`，专门盯这类只在设备上暴露的问题）。
- **离线证据**：`bash tools/preview/run.sh /tmp/prev_skel5` → **12/12 PNG**（4 页 × 活/离线/预热），
  逐页判读无豆腐块、无截断、无重叠；离线态状态胶囊为琥珀"离线"，活态为绿"在线"。
- **构建**：`./idf.sh build` EXIT=0，`build/fnos_monitor.bin` **1556384 B**（较 v5.1 的 1823440 B 减少 ~267 KB，
  即被删框架的静态体积）。
- **提交内的字库兼容窗口**：本次提交把字库阶梯换成了 v6 的一套（`cjk_{13,15,20,30}` / `num_{16,22,30,44}` /
  `txt_{12,14}`），但**已提交的 v5 版 `kk_ui/kk_widgets.c` 仍引用 `ui_font_txt_13`**（v6 版才改引用 `txt_12`，
  那份改动还没提交）。为了让 HEAD 单独可构建、又不动别人的在写文件，`tools/gen_fonts.sh` 的 `EXPECT` 与
  `components/fnos_monitor/CMakeLists.txt` 里**同时保留 `ui_font_txt_13`**（只有 ASCII 字形），并在两处写明
  "v6 落地后删"。**自洽性证据**：`git worktree add --detach /tmp/skel-clean3 HEAD` + 软链 `managed_components`
  + 拷 `build/config/sdkconfig.h` + `bash tools/preview/run.sh` ⇒ **13/13 PNG**（不依赖工作树里任何未提交文件）。
- **兼容垫片**：同一次提交里 `fnos_ui.c` 顶部对 kk_ui v6 的令牌（`KK_S1`/`KK_T1`…/`KK_RAIL_W`/`KK_HEAD_H`）
  加了 `#ifndef` 兜底，值同 v6；kk_ui 侧定义齐全时自动失效。踩坑：垫片注释里写 `KK_T*`，其中的 `*/`
  提前结束块注释，编译直接炸——注释里不要出现 `*/`。
- **文档**：`docs/ui-kk.md` 增 §8（手写页面约定）并改写"结构语言/工具树/验收合同"；
  `docs/ui-kk-authoring.md`、`docs/ui-kk-composition.md`、`docs/ui-kk-iteration-v5.md` 标为历史文档（只留设计史与判据）。

### 17.4 交付态与实机证据

- 固件：`build/fnos_monitor.bin` **1556384 B**（14:20 构建，`CONFIG_FNOS_AUTO_PAGE_SEC=0` 未改动），已烧录运行。
- 串口 `logs/skeleton-boot.log`（143 行，复位后捕获）：`[3.92] fnos_ui: ui created (pages=4)` →
  `[7.70] fnos_net: got ip 192.168.0.214` → `[8.11] fnos_data: history backfilled: 300 samples`，
  无 watchdog / 断言 / `VERIFY MODE`，堆稳态 `internal=230KB dma=192KB psram=27602KB`。
- 实机（`adb` 唤醒相机后间隔 10 s 两帧，md5 不同 ⇒ 界面活着）：P0 全量显示——顶栏 `采集 192.168.0.119:8799 · 本机 192.168.0.214`
  + 绿色"在线"胶囊、四张 KPI（`CPU 2` / `内存 51` / `最高温度 33` / `运行时长 10d 15h`，
  副行 `负载 0.21/0.57/0.75`、`15.9/31.0 GB · 余 15.2`、`CPU 33°C · 12 核`、`进程 2012 · runq 1`）、
  存储六行 `已用 … · 可用 …` + 分级条（36% / 60% / 66% / 6% / 0% / 10%，60% 与 66% 转橙）、
  趋势 CPU 蓝线 + MEM 青线与图例 `23% · 峰 69%` / `51% · 峰 51%`。
- 说明：本次工作树里 `components/fnos_monitor/kk_ui/*` 仍是**另一会话（UniFi v6 重构）未提交的改动**，
  本提交不含这些文件；固件是用工作树现状构建的。

## 18. 温度全量自适应显示（2026-10-06：新增第 5 页「温度」）

起因：用户报障"最高 75 度的数据是哪里来的，不应该出现这个数据才对"——溯源是**网卡 PHY 结温**
（NAS 上 netdata 图名 `sensors.temperature_enp1s0-pci-0100_temp1_PHY_Temperature_input`），
而界面上只有一块"最高温度"KPI，孤零零一个数字很容易被读成整机温度。用户要求：
"每一个能读到温度的传感器都显示出来，并自适应标记是什么设备……还需要显示设备名称"，
并明确"需要自适应，不要写死"。设计与踩坑见 `docs/ui-redesign-v9.md` 第十三节。

### 18.1 采集端（NAS）

- `nas/fnos-agent.py` 与 `nas/fpk/nasscreencompanion/app/server/fnos_collector.py` 的 `_temps()`
  改为枚举 `/sys/class/hwmon/*/temp*_input` 的**每一个可读通道**；设备名按系统事实推导
  （`/sys/class/{block,net,drm}` 反查 realpath + **前缀包含**匹配，NVMe 的 hwmon 挂在 PCI
  控制器上而 block 设备在它下一层；跳过带 `partition` 文件的分区；DRM 用驱动名），
  通道名取 `tempN_label`（去掉尾部 " Temperature"），CPU 靠标签语义（`Package`/`Core N`/
  `Tctl`/`Tdie`）识别 —— **不再有 `TEMP_LABEL` 这类写死的名字表**。
- 输出 `{"n","c","dev","ch"}`（`n` 保留 = 老固件兼容）；`TEMP_MAX=48` 与
  `DEFAULT_LIMITS["temps"]=48` 与板端 `FNOS_MAX_TEMPS` 对齐。
- 本机实测 24 路：coretemp `Package id 0` + `Core 0–5`、enp1s0 `PHY`+`MAC`、i915 `temp1`、
  4×NVMe `Composite`/`Sensor 1..3`；`temps` 段 24 路约 1.4 KB（板端接收缓冲 24 KB）。
- 本地验证：`python3 nas/fpk/temps_check.py` —— 在一棵**假 sysfs 树**上跑两个采集器的真代码
  （62 项断言全绿：前缀匹配、跳分区、CPU 标签识别、`dev/ch` 组装、48 路截断丢最冷）。
  两次证伪：把前缀包含改成相等比较 → 24 条断言变红；删掉分区跳过 → 4 条变红。

### 18.2 板端

- 第 5 个导航页「温度」（新图标 `KK_ICON_THERMO`）；导航几何**重解**而不是硬塞：
  `KK_NAV_Y0 72 / KK_NAV_H 60 / KK_NAV_PITCH 68` ⇒ 末项下沿 404，离 432 的分隔线还有 28px。
- 版面：摘要条 44（`KKM.h_note`）两行 + `温度传感器 · 全部通道` 卡，三列并排、每列一池 16 行，
  行高 `KK_TROW_H`(44)、条高 4、右对齐数值列 76px；摊派 `per = ceil(ntemps/3)`，
  每列只用前 `per` 行。颜色只表状态并有文字冗余（摘要行写"危险 N 路 / 注意 M 路"）。
- 兼容：`fnos_data.c` 先读 `dev`/`ch`，取不到才回退到老的 `n`；系统页温度卡与总览页
  「最高温度」KPI 的副标签也改成"设备名 · 通道名"。

### 18.3 证据

- **主机预览**：`cd tools/preview && bash run.sh out` ⇒ `preview: 5 page(s)/state`、46 张 PNG；
  几何越界、字形覆盖、"从未露面构件"三项审计全过（基线 56 → **193**，构成逐项写在
  `tools/preview/preview.c` 的注释里：96 系统页温度行池 + 96 温度页三列行池 + 1 空态标签）。
- **固件解析器**：`bash tools/parsecheck/run.sh` ⇒ `✓ 满载帧（10491 字节）能被 parse_status() 解析`；
  `temps[0]` 的 `dev`/`ch` 正确，且**最后一路故意只给 `n`** 用来验老采集端回退
  （`temp_last dev=[old-collector-packa] ch=[]`）。
- **契约**：`python3 nas/fpk/contract_check.py --emit` ⇒ `nas/fpk/board_fields.json` 51 字段，
  含 `['temps','dev'] ['temps','ch'] ['temps','n'] ['temps','c']`。
- **构建**：`./idf.sh build` EXIT=0，`build/fnos_monitor.bin` **0x1a9ef0 B**（1.74 MB），app 分区余 82%。
- **串口**（`tools/serial_capture.py --seconds 15`，捕获后自动复位回运行模式）：
  `[0.82] fnos_ui: ui created (pages=5)` → `[0.82] main: boot complete` →
  `[3.68] fnos_net: got ip 192.168.0.214` → `[4.08] fnos_data: history backfilled: 300 samples`；
  堆稳态 `internal=217KB largest=172KB dma=179KB psram=27240KB`；**无** watchdog / 断言 /
  `Guru Meditation` / `VERIFY MODE`。
- **实机照片**（手机相机预览截图，屏幕在画面里转 90°）：
  - `android-preview-20261006-131005-539116.png`：存储页 + 左侧**五项导航**（总览/存储/网络/系统/温度，
    温度计图标可见，与"配对"入口无重叠）；
  - `android-preview-20261006-131023-847289.png`：网络页（上行 186 KB/s、下行 252 KB/s、双曲线在动、
    `enp1s0-ovs`、采集成功率 100%）；
  - `android-preview-20261006-131041-454442.png`：**温度页** —— 摘要 `7 路传感器 · 最热 NIC 76.0°C` /
    `1 路 ≥75°C 危险 · 0 路 ≥60°C 注意 · 逐路写明设备与通道`，列表 `NIC 76.0°C`（红）、
    `NVME2 43.9`、`NVME3 39.9`、`iGPU 36.0`、`CPU 36.0`、`NVME1 34.9`、`NVME0 33.9`。
- **当次验收用的轮播开关**：为逐页拍照把 `CONFIG_FNOS_AUTO_PAGE_SEC` 临时设为 12 构建并烧录，
  验收后**已改回 0** 并重新构建烧录（串口日志里没有 `VERIFY MODE` 即为交付态）。

### 18.4 已知状态与后续

- 面板上现在是**7 路、名字是 `NIC`/`NVME2`**：因为 NAS 上的采集端**尚未部署**（部署要 SSH/sudo
  凭据，本会话拿不到）。这同时是"向后兼容回退路径"的实机证据 —— 老采集端只发 `n`，新固件把它
  整条放进 `dev`，页面照常工作。
- 把 `nas/install.sh` 部署到 NAS 之后（`SSHPASS=… NAS_SUDO_PASS=… ./install.sh`），同一页应显示
  24 路「`enp1s0 · PHY`」「`nvme0n1 · Composite`」「`CPU · Core 3`」这类"设备名 · 通道名"。
  这一步不需要重新烧录固件。
- 并发说明：本次改动落地时该工作树同时被另一会话改写（v9/v10 版面重构），因此落地一律用
  `patch -p0 --forward`（只加本改动的 hunk），没有整文件覆盖。

### 18.5 温度行的固定次序（2026-10-06 补，用户反馈后）

用户看到面板后给的要求是："每个通道应该单独显示并显示设备名称，**而不是直接切换显示**"。
按温度降序排的榜正是"切换显示"的来源：温度一升一降，行就互换位置，设备名跟着一起挪，
人认不住哪一行是哪台设备。

- 改法：`components/fnos_monitor/fnos_data.c` 新增 `temp_cmp()`（设备名 → 通道名 的字节序），
  `parse_status()` 解析完温度后 `qsort` 一次 —— **同一路传感器永远在同一行**；
  "最热的是谁"改由摘要行点名（`fnos_ui.c` 的 P4 摘要自己扫 max，不再假设 `temps[0]` 是最热的）。
- 连带同步：`tools/preview/preview.c` 的桩件用同规则排序（桩与设备行为必须一致，否则预览是假绿灯）；
  `tools/parsecheck/extract.py` 的 `FUNCS` 加上 `temp_cmp`（它是从 `fnos_data.c` 里按白名单抠函数的），
  `main.c` 的老采集端回退断言改成**按名字找**那一行（排序后它不再固定在最后）。
- 实机证据：`android-preview-20261006-132143-604816.png`（系统页「硬件温度」卡）——
  `CPU 35.0 / NIC 76.0 / NVME0 30.9 / NVME1 34.9 / NVME2 44.9 / NVME3 39.9 / iGPU 36.0`：
  最热的 `NIC` 稳在第 2 行（不再跑到最上面），行序不再随温度变。
- 交付态：`CONFIG_FNOS_AUTO_PAGE_SEC=0`（验收期间临时开过 12 用于逐页拍照，已复原并重新构建烧录；
  串口无 `VERIFY MODE`）。

### 18.6 设备名 `dn`（2026-10-06 再补，用户第二轮反馈后）

用户第二条反馈："能显示温度的设备名吗？而不是缩写。像 NIC 单独显示一个缩写，我根本不知道
它是什么设备，也无法对它进行降温。" —— 要求"名字要能支持一个动作"，所以内核 id（`enp1s0`）
也不合格。

- 采集端（`nas/fnos-agent.py` + fpk 里的同源文件）：每路多带一个 `dn`，从系统事实取：
  磁盘 → `device/model`；网卡/显卡 → PCI `vendor`/`device` 经 `pci.ids`（没有 pci.ids 就
  用厂商表 + 驱动名）+ `/sys/class/net/<if>/speed`；CPU → `/proc/cpuinfo` 的 `model name`；
  认不出的退回 hwmon 名。**不猜型号**（查不到就写 `PCI 1d6a` 或退回驱动名 + 速率）。
  新增模块级 `PROC = os.environ.get("FNAS_PROC", "/proc")` 便于假树测试。
- 板端：`fnos_temp_t` 加 `char dn[28]`，解析 `dn`；温度行改成**两行**（第一行设备名，
  第二行 通道名 + 数值，去掉进度条），系统页摘要卡改成 `通道名 · 设备名`（截断先丢设备名），
  摘要行用完整 `dn`。设计说明见 `docs/ui-redesign-v9.md` 第十三节。
- 证据：
  - `python3 nas/fpk/temps_check.py` → **74 项通过 0 项失败**，其中设备名断言：
    `nvme0n1 · Composite = Samsung SSD 990 PRO 2TB`、`nvme1n1 · Sensor 2 = WD Black SN850X 2TB`、
    `enp1s0 · PHY = Marvell atlantic 10GbE`（假树无 pci.ids，走厂商表+驱动+速率）、
    `CPU · Core 3 = Intel N100`（假 `/proc/cpuinfo`）、`i915 · temp1 = Intel i915`、
    `acpitz · temp1 = acpitz`（认不出时的退路）。
  - 主机预览 `05-limits-p4.png`：24 行两行式行，第一行全是人读设备名
    （`Marvell AQC113 10GbE` / `Samsung SSD 990 PRO 2TB` / `Intel N100` / `Intel UHD Graphics` …），
    第二行是通道名与数值；摘要 `24 路传感器 · 最热 Marvell AQC113 10GbE · PHY 74.2°C`。
  - `tools/parsecheck`：满载帧涨到 **12183 字节**仍能被 `parse_status()` 解析，
    新增断言 `dn` 落进结构体（`BADTEMP dn 没解析出来` → return 8），
    且最后一路仍只给 `n`（老采集端回退路径）。
  - `tools/preview` 的"从未露面构件"基线从 193 收到 **169**（行从 4 构件变 3 构件，
    注释里逐项重算过）；同时新增 **`ST_LEGACY` 状态**（预览出图 `09-legacy-p*.png`）：
    桩件模拟"老采集端只发 `n`"的 7 路数据，把**回退路径**变成可回归的图 ——
    面板上现在跑的正是这一屏（`7 路传感器 · 最热 NIC 76.0°C`，每行第一行 `NIC`/`NVME2`…、
    第二行只有数值），不再只靠设备照片当基线。
  - 固件 `./idf.sh build` + 烧录成功（13:31:38），串口 `got ip 192.168.0.214`、
    `history backfilled: 300 samples`、无崩溃、无 `VERIFY MODE`。
- **面板上要真正看到这些名字，还差一步**：把采集端部署到 NAS
  （`cd nas && SSHPASS=… NAS_SUDO_PASS=… ./install.sh`）。当前面板显示 `NIC`/`NVME2`
  是**老采集端**的数据（它只发 `n`），新固件按设计回退到那个字段 —— 固件不用重烧。

## 19. 温度页排版返工（2026-10-06 第三轮：用户报"温度界面排版完全错乱了"）

**根因**（两条叠在一起，都在"行内层级"上）：

1. 固定两行制（行 1 设备名 / 行 2 通道名 + 数值）在**老采集端数据**下退化成"名字一行、
   数值孤零零飘在右下角"—— 老端没有 `ch`，第二行左边是空的，看着就像排版错位。
   面板上当时跑的正是这份数据（NAS 采集端未升级）。
2. 反过来把数值提到行 1 与名字同行，则设备名只剩 ~185px，被截成
   `Samsung SSD 990 PRO …`、`KIOXIA EXCERIA PLUS …` —— 而这一页唯一要回答的就是
   "是谁在热"，名字截断等于没写。

**设计合同**（按 `cuktech-screen-ui` + `interface-design` 两套 skill 落）：

| 项 | 取值 |
| --- | --- |
| 设备 | 7" 1024×600 MIPI-DSI、内容区 948×544、观看距离 ~40cm、1 s 刷新、触摸 |
| 页面问题 | **哪一路传感器现在多少度、它属于哪台设备**（只回答这一个） |
| 数据合同 | `dn` 人读设备名 / `dev` 内核名 / `ch` 通道名 / `c` °C（0.1 精度）；阈值 60 注意、75 危险；缺 `ch` = 老采集端；离线 = 保留旧数据 |
| 对象顺序 | 稳定的"设备名 → 通道名"字典序（`CPU` → `enp1s0` → `i915` → `nvme0n1…` → 认不出的平台热区），恰好是物理拓扑；**不随温度变**（用户要求"不要切换显示"） |
| 信息层级 | 设备名（cjk_16, T2）→ 数值（cjk_16, 阈值色）→ 通道名（cjk_12, T3） |
| 状态编码 | 色 + 文案冗余：危险/注意=数值色，页头给"x 路 ≥75°C 危险 · y 路 ≥60°C 注意"；离线=数值与名字降 `KK_T3`，页头"保留旧数据 · 年龄"说明原因（`KK_T4` 按版面合同不得用于 <20px 文字） |

**结构（自适应行形态）**：

```
① 有 ch（新采集端）：行 1 = 设备名（占满列宽 265px，约 26 西文字符）
                     行 2 = 通道名（cjk_12, T3） + 数值（cjk_16, 阈值色，右对齐）
② 无 ch（老采集端）：整行 = 设备名 + 数值（名字让出 72px 数值列），不留半行空白
```

行高仍是 44（`2 + 19 + 2 + 19 + 2`，度量表 `KKM.trow_h/trow_name_y/trow_sub_y`）；
数值列宽 `KK_TROW_VAL_W` 72px；行内不做进度条（数值自带阈值色，条会把两行字挤碎）。

**证据**：

- 主机预览：`tools/preview/out/02-live-p4.png`（24 路两行制，全部设备名完整不截断）、
  `09-legacy-p4.png`（7 路一行制，数值紧挨名字）、`03-offline-p4.png`（数值灰化 +
  页头"保留旧数据"）。三项审计通过：几何越界 0、字形缺字 0、"从未露面构件"
  169 = 基线（与改造前一致）。
- 真机：`android-preview-20261006-163754-445064.png`（`/Users/llll/Documents/ChatGPT/board-camera/`）
  —— 为拍这一页临时把开机页设成第 4 页（`fnos_ui_set_page(FNOS_TEMP_START_PAGE)`，
  拍完已改回 `fnos_ui_set_page(0)` 并重建烧录）。照片里三列分别是
  `CPU 36.0 / NIC 76.0(红) / NVME0 31.9`、`NVME1 34.9 / NVME2 43.9 / NVME3 39.9`、`iGPU 37.0`，
  **每行都是"名字 + 数值"同一行**，与主机预览 `09-legacy-p4.png` 一致 —— 用户报的
  "值飘在右下角"消失。页头 `7 路传感器 · 最热 NIC 76.0°C` + `1 路 ≥75°C 危险 · 0 路 ≥60°C 注意` 正常。
- 交付态复核：`CONFIG_FNOS_AUTO_PAGE_SEC=0`、开机第 0 页（16:39:16 烧录，串口
  `sta ip: 192.168.0.214`、`history backfilled: 300 samples`、无崩溃、无 `VERIFY MODE`）。


## 20. 真机部署记录：NAS 采集端换成"温度全量自适应"版（2026-10-06 16:44）

**这一步是用户明确授权后由代理直接做的**（此前几轮一直卡在"需要 SSH 凭据"）。

- 连接：`ssh llll@192.168.0.119`（fnOS/Debian 12，内核 `6.18.18.c1107-trim`，主机名 `nas`）。
  顺带把本机 `~/.ssh/id_ed25519.pub` 装进 `/home/llll/.ssh/authorized_keys`
  （该用户家目录原先不存在 —— 登录时一直报 `Could not chdir to home directory /home/llll` ——
  已创建并 chown 到 `llll:1001`）；**sudo 仍需密码**，不落盘、不进记忆。
- 部署前先跑**只读**试算（`nas/preview-naming.sh` → 远端 `fnos-agent.py --temps`）：
  真机 **24 路**，命名如下（摘）：

  | 设备名(dn) | 通道(ch) | °C | 内核名(dev) |
  | --- | --- | --- | --- |
  | Aquantia atlantic 10GbE | PHY / MAC | 76.0 | enp1s0 |
  | ZHITAI TiPlus7100 1TB | Composite / Sensor 1–3 | 43.9…31.9 | nvme2n1 / nvme3n1 |
  | PCIe-8 SSD 512GB | Composite / Sensor 1–3 | 35.9…27.9 | nvme0n1 / nvme1n1 |
  | Intel UHD Graphics 630 | temp1 | 36.0 | i915 |
  | Intel Core i7-8700 | Package id 0 / Core 0–5 | 35.0…33.0 | CPU |

  ⇒ 24 = CPU 7 + 网卡 2 + 核显 1 + 4×NVMe×4。**"75°C 是谁"这个原始问题就此闭环：是 Aquantia 10G 网卡的 PHY 结温。**
- 顺带修掉一个真机才暴露的瑕疵：iGPU 型号名在 pci.ids 里是
  `CoffeeLake-S GT2 [UHD Graphics 630]`，28 字符上限把它截成 `Intel CoffeeLake-S GT2 [UHD `。
  新增 `_chip_short()`：**有方括号别名就取括号内容**（那通常是用户认识的名字），再砍
  `… Controller` 尾巴 → `Intel UHD Graphics 630`。`nas/fpk/temps_check.py` 的假 pci.ids
  已换成真机这条，断言同步 → **92 项通过 0 失败**。
- 部署：`cd nas && SSHPASS=… NAS_SUDO_PASS=… ./install.sh` —— 装到
  `/usr/local/bin/fnos-agent.py` + systemd 单元（部署前该文件是 Sep 20 的旧版），
  本机复核输出 `temps: enp1s0 PHY 75.0, enp1s0 MAC 75.0, nvme2n1 Composite 43.9, …`（24 条）。
- 线上契约复核：`/api/v1/status` 的 `temps[]` 字段 = `['c','ch','dev','dn','n']`，
  例 `{"n":"enp1s0 PHY","c":76.0,"dev":"enp1s0","ch":"PHY","dn":"Aquantia atlantic 10GbE"}` ——
  `n`/`c` 保留（老固件可用），新增三个字段。
- **面板侧（固件未重烧）**：照片 `android-preview-20261006-164706-861674.png`
  （`/Users/llll/Documents/ChatGPT/board-camera/`）显示「温度」页已是 24 路两行制：
  第一行设备名（`Intel Core i7-8700`、`Aquantia atlantic 10GbE`、`ZHITAI TiPlus7100 1TB`、
  `PCIe-8 SSD 512GB`、`Intel UHD Graphics 630`），第二行通道名 + 数值，76.0°C 走危险色；
  页头 `24 路传感器 · 最热 Aquantia atlantic 10GbE · MAC 76.0°C` +
  `2 路 ≥75°C 危险 · 0 路 ≥60°C 注意`。行序按设备名稳定（CPU → enp1s0 → i915 → nvme0…3）。

## 21. 配对 POST 卡死：请求体根本没发出去（2026-10-06）

**症状**：板子明文取 `identity` ✓、证书指纹算得出 ✓，但 `POST /api/v1/pair` 6 秒超时
（串口 `fnos_pair: http fetch_headers failed: errno=0 internal_heap=210288` +
`HTTP_CLIENT: Connection timed out before data was ready!`），同时 NAS 应用日志出现
`SSLEOFError: EOF occurred in violation of protocol` 与 403/500。

**定位手法（关键）**：给配对流加了一条**只读自检** —— `fnos_pair_tls_probe()`
（`components/fnos_monitor/fnos_pair.c`）+ 串口命令 `tls`（`fnos_wifi_cli.c`）：
明文取 identity → 用那张证书 `GET /api/v1/health` → 再 `POST /api/v1/pair` 一个假码。
第一次跑就分流了问题：**GET 一直 `ok=1 status=200`** ⇒ 证书 / 时钟 / TLS 版本 / 应用侧
TLS 服务 / 6 秒超时全部排除，问题只能在 POST 这一段。

**真因**：`http_oneshot()` 里 `esp_http_client_open(c, strlen(body))` 只负责**连接 +
记住 Content-Length**，请求头与请求体是在第一次 `write`/`fetch_headers` 时才发的。
原代码没写 body 就 `esp_http_client_fetch_headers()` ⇒ 服务端等 body、板子等响应，
双向死等到超时；板子一断开，应用侧就报 `SSLEOFError`。GET 没有 body，所以从来没暴露。

**修**：open 之后补 `esp_http_client_write(c, body, blen)` 并校验返回值，失败时把
`写请求体失败 n/m（errno N）` 写上屏。

**证据**（修复后同一条 `tls` 命令，真机串口）：

```
探针：identity 来自 192.168.0.119:8798，证书 1184 字节
[tls] HTTPS 通过 https://192.168.0.119:8798/api/v1/health（ok=1 status=200 len=106）
      ｜ POST /api/v1/pair ok=1 status=403：配对码已过期，请重新生成
```

POST 拿到 403 且**服务端的错误文案能上屏** —— 说明请求体发出去了、响应也收全了
（假码 `000000` 必然 403，这正是探针的预期结果）。

**顺带**：`tls` 命令临时把 `esp-tls` / `mbedtls` / `HTTP_CLIENT` 抬到 DEBUG，握手细节直接
进串口；探针跑在独立的 PSRAM 栈任务里（mbedTLS 握手吃栈，串口命令任务的 4 KB 不够）。
