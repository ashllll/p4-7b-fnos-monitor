# 飞牛监控伴侣：实现合同

本文件描述当前源码的部署、协议与安全边界。安装操作见 [应用说明](../nas/fpk/README.md)，验证入口见 [验证指南](verification.md)，剩余验收见 [未闭环事项](open-items-2026-10-11.md)。仓库内 1.2.5 FPK 是历史包，未包含后续源码修复。

## 部署与运行身份

- 包源码位于 `nas/fpk/nasscreencompanion`，依赖声明为 `python312`。
- fnpack 将包内 app 内容解到安装 target。**TRIM_APPDEST 指向 target 本身**：安装后服务位于 `${TRIM_APPDEST}/server/`，页面在 `${TRIM_APPDEST}/ui/www/`，网关 Socket 在 `${TRIM_APPDEST}/app.sock`。源码树中的 app 层不能再加到安装路径上。
- 配置保存在 TRIM_PKGETC，运行状态保存在 TRIM_PKGVAR；生命周期入口为 `nas/fpk/nasscreencompanion/cmd/main`。
- 生命周期需要 root 完成私有目录准备及容器辅助进程管理；管理／采集服务由 docker_launch 降权到包用户后 exec。读取失败按来源报告，不递归放宽权限。
- 管理页走应用网关与 Unix Socket；开发板读取独立的 HTTPS 遥测端口，默认 8798。旧 systemd 采集器默认 8799，单独部署，不自动覆盖。

## 数据与协议

| 数据 | 来源与边界 |
| --- | --- |
| CPU、内存、负载、运行时间 | Linux proc；差值按真实采样时间计算 |
| 网络 | proc 网络计数与路由；接口独立计算，区分热插拔和计数复位 |
| 卷与磁盘 | 本地挂载容量、statvfs、diskstats；逻辑设备不等同物理硬盘数量 |
| RAID | mdstat；健康严重度与维护进度分开，读取失败不能伪装空清单 |
| 温度、可选 ZFS | hwmon／可读 ZFS 来源；缺失或权限错误单独显示 |
| 容器 | 显式启用的受限辅助进程，仅固定 Docker 只读请求 |

不调用未公开的飞牛遥测 API，不承诺 SMART 或所有厂商传感器兼容。
动态清单保留身份、名称与通道；若显式配置数量策略，必须提供遗漏元数据。
固件接收及快照有可配置字节预算，超限应报错并保留最后有效数据，不能静默裁剪。

设备消费的状态／历史字段以 `nas/fpk/board_fields.json`、采集器和固件解析器为准。
修改协议时同时跑字段契约、采集回归与实际解析器；专用 fixture 不等于通用 JSON 验证器。
历史 reset／回填必须保留实时样本，并避免将其他端点的历史混入当前端点。

## 鉴权与 TLS

1. 管理操作验证应用网关身份；TCP 请求中的 X-Trim Header 不授予管理权限。Unix Socket 0600 允许 root 与所属用户访问，不能声称仅网关可访问。
2. 默认启用 HTTPS 和配对令牌。开发板首次通过受限明文端点取得公有证书，用户核对 SHA-256 指纹后确认；此引导通道不代替信任确认。
3. 设备保存自己的端点、令牌与受信证书；NAS 保存令牌哈希。管理页可撤销单个设备。
4. 已配置证书损坏、读取失败或生成失败必须明确失败，不能自动退回明文。明文／免令牌模式只由用户显式启用。
5. 重签先生成私有候选、校验证书对／有效期／私钥权限，再备份并替换；失败尝试恢复。两文件替换不是掉电原子事务。
6. 活跃 SSLContext 与公有证书／指纹在同一协作锁内装载；identity 使用内存快照。重签后的新身份直到重启才和新握手一起生效。

管理页请求有有限超时和响应形状检查；迟到刷新不能覆盖新状态。两阶段保存可能只完成第一阶段，必须显示部分保存／结果未确认并保留待保存状态。中止 fetch 不撤销已经到达后端的 POST。

## 并发与生命周期

- 采集只保留一个工作线程；失败保留旧快照副本并报告来源错误，停止后的迟到采样不得发布。
- 在创建请求线程前限制整连接名额，连接绝对期限覆盖探测、TLS 与 HTTP，字节到达不重置期限。
- POST 单请求关闭；拒绝冲突或不合法的长度／传输编码。正常只读 GET 可复用连接。
- 配置 writer 使用同一 config.json.lock 与内部 revision 比较；旧快照冲突明确失败。候选使用独立 0600 临时文件、flush／fsync／replace；只清理本次候选。
- 非协作手工修改和不递增 revision 的旧程序不受 CAS 保证。锁 inode 被同 UID 替换、锁等待与阻塞 IO 仍有边界。
- 关闭时拒绝新请求、中断登记的 live socket 并有界等待工作线程。Unix 路径仅在 dev／inode 匹配本次绑定身份时删除。
- 服务 CLI 写私有 PID／退出码收据；stop 只有确认进程结束且读到匹配 PID 的成功收据才返回成功。强杀后无收据属于收尾失败。
- PID／命令路径与 Unix inode 检查不构成进程出生身份或同 UID 隔离。共享排空预算不保证不可中断 IO 的硬退出时间。

## 安装、升级与卸载

安装先校验向导字段、目录与端口，再启动；配置错误保留原文件，不静默重置默认值。
升级先验证并备份旧配置，保留设置、配对设备和证书；仅原来运行时恢复启动。迁移和应用设置使用相同的协作锁／revision 规则。
卸载按用户选择处理应用数据；平台保留数据时，不能承诺令牌已删除或复装一定要求重新配对。
旧 systemd 服务的端口、代码与信任材料需要单独确认，不由 FPK 安装隐式接管。

## 验证与发布

```bash
python3 nas/fpk/manifest_check.py --strict
python3 nas/fpk/api_check.py
python3 nas/fpk/contract_check.py
python3 nas/fpk/collector_check.py
python3 -W error::ResourceWarning nas/fpk/test_server_safety.py
python3 nas/fpk/config_persistence_check.py
bash nas/fpk/test_lifecycle.sh
python3 tools/plan_check.py
```

生命周期脚本使用匿名临时布局；缺少工具的未完成状态与硬失败必须区分。
`tools/test_report.sh` 生成报告并检查错误传播；报告不得手写“本轮通过”。
打包入口为 `nas/fpk/build.sh`，检查包内容和缓存；不直接把开发机文件整目录交给 fnpack。

源码发布、FPK 打包、真实 fnOS 安装、开发板配对和物理屏幕／长期运行是独立阶段。
正式包需要在声明支持的环境验证运行身份、权限、网关、升级／卸载、证书信任及故障恢复。
容量、性能、真实掉电／回退、浏览器视觉和 72 小时证据的缺口见未闭环事项，不随源码推送自动关闭。
