# UniFi 风格的信息与入口增强

2026-10-12，在原生 LVGL 六页内实现；沿用 `unifi-ui-design` 的 Network 10.6.106 深色参考与本项目触控适配。主机预览是匿名样例，不代表 NAS 已部署或开发板已烧录。

## 界面

- 窄屏：当按比例分配后的资源卡宽度不足配置的可读宽度时，CPU 与内存上下排列，保留字号与页面滚动。
- 总览：CPU、内存、网络、存储、温度标题带详情入口与按下反馈；资源卡提供独立的数据质量标记。内存显示可用容量、总量与交换区用量。
- 存储：每卷显示剩余容量；磁盘行追加读、写各自的平均完成延迟和 SMART 状态。SMART 异常进入采集告警。无 I/O 或计数器刚初始化/重置时延迟为 `--`，不伪造零。
- 网络：物理网口在链路 up 且协商速率有效时显示收、发各自的链路占用。全双工两个方向独立计算，KiB/s × 1024 × 8 转为 bit/s 后除以协商 Mbps；不把双向合计当利用率，也不拿虚拟网卡猜物理链路。
- 网络采集延迟卡：当前请求耗时与最近至多 64 次请求的 P95；含失败请求。无诊断样本时明确等待。
- 系统：容器保留完整名称和状态，增加 CPU、内存、重启次数、资源采样年龄。多核 CPU 允许超过 100%；内存扣除 inactive file cache。停止状态不显示旧 CPU/内存为当前读数。
- 温度：每次采集最高通道温度的历史曲线。它是所有已上报通道的聚合，最高通道身份可以变化，不声称是一台设备的温度轨迹。设备块明确标出展开的通道数。
- 清单标题保留各自采集段状态，缺失字段不当成健康值。数量、换行与滚动仍由实际清单和可用空间决定。

## 协议与采集

保持 proto=2，追加可选字段；旧端点、旧屏端保留兼容。

| 对象 | 追加字段 |
| --- | --- |
| disks[] | read_ms、write_ms（可 null）、smart、smart_ts |
| docker[] | id（完整 Docker ID）、cpu_pct、mem_mb、restarts（可 null）、metrics_ts |
| history.rows[] | 第六列 temp_max（可 null），前五列与含义保持不变 |

温度历史为单条聚合序列，屏端环形缓冲新增 600 个 float，使用已有历史游标与重置规则。旧版五列历史的温度为空；不是 0°C。历史接收字节预算由 32 KiB 调整至 64 KiB，仍不是任意长度 JSON 的保证。

SMART 使用可选的 `smartctl -j -H -n standby,3 /dev/<枚举的物理盘>`；不安装工具、不改变设备权限、不启动自检。每轮至多探测一个到期盘，单次超时 0.4 秒，默认每盘 60 秒缓存（`FNAS_SMART_INTERVAL` 可调）。显示通过、异常、休眠跳过、缺少工具、无权限、超时、未采集；md/dm 逻辑设备标为不适用。包用户无设备读取权限时会明确不可用，这次没有扩展 root 的 SMART 权限。盘从清单消失后清理缓存。

Docker 的只读 reader 仍只接受 R，容器 ID 只能来自 Docker 自身枚举且必须为完整十六进制 ID。增加两个固定 GET：inspect 的 RestartCount、stats 的单次 CPU/内存数据；不接收任意路径或操作命令。每次库存刷新轮转一个容器，额外两次请求各至多 0.3 秒 socket 超时，root reader 保留原整体 3 秒期限。全量容器不会截断，资源年龄随轮转显示；CPU 需要同一 ID 的两次有效累计样本，容器多时更新周期相应增长。查询失败清除该实体的旧资源数据，移除实体清理缓存。Web 服务继续降权运行。

计算依据：[Linux diskstats](https://www.kernel.org/doc/html/latest/admin-guide/iostats.html)、[Docker Engine API](https://docs.docker.com/reference/api/engine/version/v1.51/)、[smartctl 手册源码](https://github.com/smartmontools/smartmontools/blob/main/src/smartctl.8.in)。SMART 通过只代表其健康自检状态，界面不将其等同于完整的数据安全证明。

## 验证

```sh
python3 nas/fpk/test_metrics.py
python3 nas/fpk/collector_check.py
python3 nas/fpk/docker_check.py
python3 nas/fpk/contract_check.py
python3 tools/test_history.py
python3 tools/test_history_merge.py
bash tools/gen_fonts.sh
bash tools/preview/run.sh out-enhanced
./idf.sh build
python3 tools/stack_check.py
```

预览覆盖当前 1024×600 与 480×480、800×480、1280×800；验证标题点击入口、空/旧清单、完整设备名称、动态清单、缺失字段、对比度、越界、点击重叠与内存失败恢复。ASan/UBSan 检查新增可选值的 null、真实零、负数、溢出、非整数重启次数以及历史合并。NAS/设备运行和物理触摸仍需部署后的独立验收。

本轮实际通过：94 帧默认预览、四档尺寸、全部五个详情标题的指针点击、四组硬件清单与分配失败恢复、43 处滚动条间距检查；采集指标用例、Docker IPC 回归、75 字段契约、92 项温度采集检查、10 个历史合并场景和 41 个验证门禁测试。ESP-IDF 5.5.3 / esp32p4 rev3 构建通过；7 个栈入口预算通过（静态下界，未包含全部中断与动态调用）。
