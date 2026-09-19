# nas/ —— NAS 侧只读采集器

`fnos-agent.py` 是给这块板子（也给任何想要一份紧凑 NAS 状态的脚本）用的采集端点：
单文件、只用 Python3 标准库、只读、常驻约 11 MB。

## 安装 / 卸载 / 排错

```bash
cd nas
# 安装（会 scp 到 /tmp，再用 sudo 安装到 /usr/local/bin + /etc/systemd/system 并启动）
SSHPASS='<ssh 口令>' NAS_SUDO_PASS='<sudo 口令>' ./install.sh
# 卸载
SSHPASS='...' NAS_SUDO_PASS='...' ./install.sh uninstall

# NAS 上直接验证
curl -s http://127.0.0.1:8799/api/v1/status | python3 -m json.tool | head -40
python3 /usr/local/bin/fnos-agent.py --selftest     # 逐段自检，哪一段抛异常会指出来
journalctl -u fnos-agent -n 50 --no-pager           # 需要 root 或 adm 组
```

## 采集内容

| 字段 | 来源 |
| --- | --- |
| `cpu`（pct/load1-15/cores/temp） | `/proc/stat` 差值、`/proc/loadavg`、hwmon `coretemp` |
| `mem`（total/used/avail/pct/swap） | `/proc/meminfo` |
| `net`（if/rx_kbs/tx_kbs/累计） | `/proc/net/dev` 差值；网卡自动取默认路由那个（本机是 `enp1s0-ovs`） |
| `vols`（各存储池） | `/proc/mounts` 过滤 + `statvfs`；按设备去重（`/tmp`、`/var/tmp` 是 `/` 的 bind mount） |
| `raid`（md 阵列、成员数、同步进度） | `/proc/mdstat` |
| `disks`（读写 KB/s，按繁忙度取前 10） | `/proc/diskstats` 差值 |
| `temps` | `/sys/class/hwmon/*/temp*_input`（NVMe×4、CPU、网卡、核显） |
| `docker`（名称/是否运行/状态） | 直接对 `/var/run/docker.sock` 发 HTTP/1.0 请求（不依赖 docker 包） |
| `zfs`（ARC 大小、命中率） | `/proc/spl/kstat/zfs/arcstats` 差值 |
| `alerts` | 由上面几项派生：阵列异常/同步、空间 ≥80%/≥90%、CPU ≥80°C、内存 ≥90%、负载 > 核数、曾运行的容器掉线 |

## 端点

| 路径 | 说明 |
| --- | --- |
| `GET /api/v1/status` | 完整状态（约 2.35 KB，实测 16 ms）；某一段采集失败时该段保留上一份值并列出 `errors` |
| `GET /api/v1/history` | `{"cols":["ts","cpu","mem","rx_kbs","tx_kbs"],"rows":[[...]]}`，最近 300 个采样 |
| `GET /api/v1/health` | `{"ok":true,"ts":…,"age_s":1,"host":"nas"}` —— 带快照新鲜度，便于外部探测"采集是否还活着" |
| `GET /` | 浏览器里看的简易实时页面（排查用） |

健壮性相关的几处刻意设计（都是评审提出后补的）：

* 每个连接 15 秒空闲超时 + accept 队列 64：板子掉线留下的半开连接不会永久占线程/fd。
* 跳过 NFS/SMB/fuse 挂载：`statvfs` 在硬挂载的网络文件系统上会进 D 态，采样线程再也回不来，
  而端点还会一直返回 `ready:true` 的旧数据。
* 速率差值一律用 `time.monotonic()`：墙钟被 NTP 回拨时不会把速率算成天文数字。
* 逐段 try/except：某一段（比如 docker）失败只让那一段沿用上一份值，不会整帧作废。
* docker 列表上限 16 条、名称/状态截断：payload 不会涨过板子 8 KB 的接收缓冲。
* 先绑定端口再等第一帧采样：systemd 报 active 的时刻端口就能连上。
* `server_bind` 跳过 Python 默认的反向 DNS（解析不可达时会阻塞几十秒才监听）。

参数：`--bind`（默认 0.0.0.0）、`--port`（8799）、`--interval`（1.0 s）、`--hist`（300）、`--selftest`。
环境变量：`FNAS_TOKEN`（非空则要求 token）、`FNAS_NETIF`、`FNAS_VOLUMES`。

## 只读边界

* 代码里没有任何写文件、发命令、改配置的路径；docker 只调 `GET /containers/json`。
* systemd 单元：`ProtectSystem=strict`、`ProtectHome=true`、`PrivateTmp=true`、
  `NoNewPrivileges=true`、`ProtectKernelTunables/Modules/ControlGroups=true`、
  `RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6`、`CapabilityBoundingSet=CAP_NET_BIND_SERVICE`、
  `CPUQuota=25%`、`MemoryMax=192M`、`Nice=5`。
* 端口监听在局域网内、无认证（默认）。同一网段的人都能读到 NAS 的负载与容量。
  要收紧就设 `FNAS_TOKEN`（板子端在 `fnos_config.h` 填同一个值），或把 `--bind` 改成具体地址。
