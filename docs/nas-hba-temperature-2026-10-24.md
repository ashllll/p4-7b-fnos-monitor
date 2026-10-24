# NAS 控制器温度采集与 1.2.8 部署验证

## 问题与实现

此前采集器只读取 hwmon。现场 MPT 控制器已由内核识别，但没有 hwmon 温度接口，
因此仅修复控制器与下挂磁盘的设备归属并不足以显示它的温度。

1.2.8 在既有 root 辅助进程中增加固定的温度读取操作 `T`。
主服务继续降权到专用包用户；父进程不能传入 ioctl、设备路径或固件命令。
辅助进程按本机 SCSI host 的驱动、控制器 ID、MPI 版本和 PCI 路径枚举 MPT 控制器，
仅发送 MPI CONFIG 的 IO Unit page 7 header/read-current，请求没有写入数据，
不发送阵列配置、Flash 或复位命令。已有 hwmon 的控制器继续由原来源负责，避免重复。

接口布局依据 Linux 的 [mpt3sas ioctl 定义](https://github.com/torvalds/linux/blob/v6.12/drivers/scsi/mpt3sas/mpt3sas_ctl.h)
及 [IO Unit page 7 定义](https://github.com/torvalds/linux/blob/v6.12/drivers/scsi/mpt3sas/mpi/mpi2_cnfg.h)。
先查询固件返回的页版本与长度，再读取当前页；IOC/Board 按各自单位字段判断存在性，
支持摄氏与华氏转换，不为固件标记缺失的通道制造读数。

读取结果缓存 5 秒，避免随高频 HTTP 轮询反复访问控制器。传输复用既有继承 socketpair
和串行锁，温度请求使用独立的有界等待。普通 hwmon 与控制器通道合并后统一排序、计数；
控制器读取失败时保留可用 hwmon，并将温度段明确标为 partial/error，携带原因。

## 检查

- `python3 nas/fpk/test_mpt_temperature.py`：5 项通过，覆盖固定只读 ABI、错误响应、单位与缺失通道、独立设备身份、采集合并、IPC 操作白名单和缓存。
- `python3 nas/fpk/temps_check.py`：104 项通过，控制器与下挂磁盘各自保留温度身份。
- `python3 nas/fpk/test_metrics.py`：4 项通过；Docker 既有 IPC 与指标采集检查通过。
- `python3 nas/fpk/startup_permission_check.py`：10 项通过。
- `python3 nas/fpk/test_upgrade_safety.py`：10 项通过。
- `python3 nas/fpk/test_server_safety.py`：67 项通过。
- `bash nas/fpk/build.sh` 与 manifest 检查通过，包内无本机字节码或缓存。

## 部署与现场验证

保留旧版本包与安装代码的本地回退副本，保存配置及证书比较依据。
CLI 安装预检要求向导和依赖复核，因此转到已登录的 fnOS 应用中心手动更新至 1.2.8。
应用中心确认了现有 Python 3.12 依赖；未安装新的依赖应用。

升级后的在线快照出现独立 HBA IOC 通道，温度采集段为 ok；原有有效 hwmon 通道保留。
现场内核另有五个非正数温度输入，沿用原采集器的无效/未连接通道规则，未将它们伪装为有效读数。
控制器未返回可用的 Board 温度，没有新增虚假 Board 通道。

重新拍摄并人工查看开发板温度页：第一项已显示 HBA 身份及 IOC 读数。
照片有反光和屏边遮挡，仅确认该条目显示，不代表完整触摸、光学或长期运行验收。
拍摄结束后恢复开发板正常运行；本次没有重新烧录固件。

服务设置、配对设备和 TLS 证书均与升级前一致，网页服务保持普通包身份；
安装的 collector、IPC transport、root reader 三份源码哈希与修复包一致。
私有快照、配置备份、安装日志及照片留在本地排除路径，没有加入源码发布。
上述部署验证在源码发布前完成。
