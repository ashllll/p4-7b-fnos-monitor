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

> 复核路径说明：四页的最终版面是用 `CONFIG_FNOS_AUTO_PAGE_SEC=12` 的临时构建逐页拍照核对的，
> 交付固件只把该值改回 0（唯一的差别是那个自动轮播定时器），交付版本另拍了总览页并核对了串口稳态
> （`poll ok=30 fail=0 9ms`、内部 RAM 235 KB）。

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

交付固件已把自动轮播关掉重新烧录，并再抓一次串口稳态确认。

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

## 11. 已知问题与不做的事

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
