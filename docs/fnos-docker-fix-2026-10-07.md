# 容器采集修复与验收

## 已确认原因

调查时运行的伴侣插件关闭了容器采集，包用户也没有 Docker Socket 权限。FPK 采集模块调用了未定义的 `_http_unix`，异常被吞掉，空列表或旧列表被标成新的健康快照。固件系统页把所有空列表统一显示为“未采集到容器”。

## 修改

- 新增固定 Docker GET 传输，成功才更新缓存；保留真实读取时间和截断前总数。首次失败区分 denied/missing/error；有效快照后失败标 stale，恢复自动更新。关闭和失败不会阻止其他模块的告警计算。
- `cmd/docker_launch.py` 使用系统 Python 的隔离模式。root 子进程只接受继承 socketpair 的读取指令，只调用 `GET /containers/json?all=1`，只返回名称、运行标志、状态描述、总数和读取时间。有 3 秒总截止及响应/帧字节上限。
- 生命周期需要 root 启动辅助进程。管理服务永久降权后才打开包日志和执行可写 target 代码；配置、证书、PID 和停止信号均使用包用户身份。辅助进程关闭/退出后指定 PID 被回收，不改变 TLS 子进程的 SIGCHLD 行为。
- 系统页分别呈现未启用、权限不足、来源不存在、读取失败、暂无容器；旧数据灰色显示“上次”，模块失败不会显示健康的零计数。
- 契约检查跟随 Docker 字段的真实生产模块；修复自适应预览尺寸提取、生命周期第 12 段端口以及通过数冒充执行数的检查误报。

## 本机验证

- `nas/fpk/docker_check.py`：固定 Unix HTTP GET、chunked HTTP、IPC 分帧、关闭不访问、权限失败、缓存总数、陈旧时间戳、恢复、空列表、帧损坏后关闭和 helper 退出通过。
- `nas/fpk/collector_check.py`、`perm_check.py`、`contract_check.py` 通过；52 个协议字段和容量匹配。
- `nas/fpk/test_lifecycle.sh 18971`：142 项通过，0 项失败。该测试在 macOS 跑，不覆盖真实 Linux root 降权。
- 原生 LVGL 预览：75 张图及容器状态断言通过，已检查未启用和旧快照图片；应用 interface-design 的状态与空态规则，保持既有布局。
- ESP-IDF 构建和 230400 波特率烧录成功。实机系统页明确显示“容器未启用”，其他指标持续更新；运行 ELF 前缀与本次产物一致，串口连续观测约 3 分钟，150 次轮询成功、0 次失败；后续相机复核运行约 7 分钟，实机计数 396 次成功、0 次失败，无崩溃/看门狗记录。
- 独立评审修复了启动模块路径和 helper 僵尸进程问题，后续 IPC/截止/回收复核通过。

## 产物

- 插件：1.2.0，80,213 字节。SHA-256 `847b36dc5e9818d2c0cbb0c284e9df80fad9958dddc48524d4be8a5e83763c49`。
- 固件 BIN：1,749,616 字节。SHA-256 `ade07feacf9126f0e06d92f5758ec44b95016127fe87b26308394ab46d31363a`。
- ELF SHA-256 `874f934716e127bcf7b7db8ff92b77bea58bb8c4a636adeefd248a99fde79ba6`。

## 后续现场验收

用户随后手动安装了 1.2.0，出现服务立即退出；调查确认 root 模式升级会重置日志和证书 ownership，1.2.0 缺少权限迁移。最终修复和部署证据见 [1.2.3 启动与升级修复](fnos-startup-fix-2026-10-07.md)。

1.2.3 已在 NAS 运行。实际管理服务为普通包用户，辅助进程为 root；Docker 模块 ok，4 个容器全部运行中，与飞牛 Docker 界面和开发板系统页对应。升级保留容器开关、配置和 TLS 文件，旧版进程均已退出。143 项生命周期、10 项权限和 9 项配置持久化回归通过；图像和私人日志仅保存在本机。

权限依据：[飞牛应用权限文档](https://developer.fnnas.com/docs/core-concepts/privilege/)、[Docker Linux 权限说明](https://docs.docker.com/engine/install/linux-postinstall/)。
