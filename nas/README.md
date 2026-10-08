# NAS 侧采集：飞牛应用与独立 Linux 服务

日常在飞牛上使用，推荐 [NAS 屏幕伴侣 FPK](fpk/README.md)：应用中心安装、飞牛桌面管理、默认 HTTPS + 设备配对，遥测默认端口 `8798`。本目录的 `fnos-agent.py` 是保留的独立 Python / systemd 方案，默认 HTTP `8799`。**`install.sh` 安装的是独立采集器，不是飞牛应用包。**

| 项目 | 飞牛应用 `nasscreencompanion` | 独立 `fnos-agent` |
| --- | --- | --- |
| 部署 | 飞牛应用中心手动安装 FPK | SSH + sudo 安装 systemd 单元 |
| 管理入口 | 飞牛桌面内的管理页 | 命令行、journal、简易 HTTP 页面 |
| 默认遥测 | HTTPS / `8798` / 配对令牌 | HTTP / `8799` / 无令牌 |
| 服务身份 | 普通包用户；可选受限 root 容器读取辅助进程 | 示例 systemd 单元为 root，附带只读加固 |
| 配置 | 安装向导与服务设置 | CLI 参数、环境变量与本地单元覆盖 |
| 并存 | 安装、升级、卸载不接管旧服务 | 由自身安装 / 卸载脚本管理 |

下面说明**独立采集器**；飞牛应用安装与配对请使用 [fpk/README.md](fpk/README.md)。

## 部署独立采集器

采集器只使用 Python 3 标准库。NAS 需要 Python 3、systemd、SSH，部署账号需要 sudo 权限。以下命令从仓库根目录执行，替换示例地址与用户名：

```bash
# 可先查看设备 / 温度命名：只运行一次，不启动监听服务。
bash nas/preview-naming.sh your-user@nas.example.test

# 安装并启动；SSH 默认使用现有公钥或连接，sudo 默认使用非交互模式。
NAS_HOST=nas.example.test NAS_USER=your-user bash nas/install.sh
# 卸载本采集器。
NAS_HOST=nas.example.test NAS_USER=your-user bash nas/install.sh uninstall
```

`install.sh` 将文件投送到 NAS 的私有 `0700` 临时目录，以 sudo 安装到 `/usr/local/bin/fnos-agent.py` 和 systemd 单元路径，再启动服务。需要密码认证时，脚本支持 `SSHPASS`（本机需 `sshpass`）和 `NAS_SUDO_PASS`；在自己的本地环境提供，不把真实值写进命令示例、仓库或日志。现有脚本不保存 SSH 主机密钥，自动部署前应通过自己的可信连接核实 NAS 身份。

在 NAS 上进行只读检查：

```bash
curl --fail --silent http://127.0.0.1:8799/api/v1/health
python3 /usr/local/bin/fnos-agent.py --selftest
python3 /usr/local/bin/fnos-agent.py --temps
journalctl -u fnos-agent -n 50 --no-pager
```

日志访问可能需要 root 或日志组权限。启用 `FNAS_TOKEN` 后，遥测请求需带对应令牌。完整 JSON 和诊断输出可能包含设备、挂载与网络信息，应仅保存在自己的本地验证目录。

## 数据与接口

| 字段 | 主要来源 |
| --- | --- |
| `cpu` | `/proc/stat` 差值、`/proc/loadavg`、可读 CPU hwmon |
| `mem` | `/proc/meminfo` |
| `net` | `/proc/net/dev` 差值；默认接口摘要与完整非 loopback 接口清单 |
| `vols` | `/proc/mounts` 过滤 + `statvfs`，按设备去重 |
| `raid` | `/proc/mdstat`，阵列成员及同步进度 |
| `disks` | `/proc/diskstats` 差值，包含系统提供的逻辑块设备 |
| `temps` | `/sys/class/hwmon/*/temp*_input`，设备与通道身份 |
| `docker` | Unix Socket 上固定 `GET /containers/json`，不依赖 Docker Python 包 |
| `zfs` | 存在时读取 `/proc/spl/kstat/zfs/arcstats` |
| `alerts` | 根据采集状态派生容量、阵列、温度、内存、负载及容器告警 |

| 路径 | 作用 |
| --- | --- |
| `GET /api/v1/status` | 当前完整状态；某一来源失败时保留其上一份数据并列出错误 |
| `GET /api/v1/history` | CPU / 内存 / 默认网口收发历史，默认保留 300 个样本 |
| `GET /api/v1/health` | 存活与快照新鲜度探针 |
| `GET /` | 简易实时页面，供排查使用 |

完整清单的 JSON 大小随实际硬件和名称变化，没有固定“小帧”保证。速率使用单调时间；网络接口分别处理热插拔和计数器复位。跳过会阻塞 `statvfs` 的 NFS / SMB / fuse 挂载；每段采集独立处理错误。固件超出整帧字节预算时保留最后有效快照并报错，不静默裁掉设备。

## 配置

| 入口 | 默认 / 用途 |
| --- | --- |
| `--bind` | `0.0.0.0`，按需改为指定地址 |
| `--port` | `8799` |
| `--interval` | `1.0` 秒 |
| `--hist` | `300` 个样本 |
| `--selftest` / `--temps` | 单次来源自检 / 温度表，不启动服务 |
| `FNAS_TOKEN` | 非空时要求查询令牌或 `X-Token` |
| `FNAS_NETIF` / `FNAS_VOLUMES` | 选择摘要网口 / 存储卷 |
| `FNAS_SYSFS` / `FNAS_PROC` | 测试用假树根路径 |

这套独立端点没有飞牛应用的 HTTPS 证书配对流程。开发板使用回退端点时，在本地配置里设置实际 `FNOS_HOST`、`FNOS_PORT` 和可选 `FNOS_TOKEN`；不要把旧 HTTP 端口输入需要 HTTPS 的配对流程。默认监听所有内网地址且无令牌，适用范围由自己的网络与部署策略确定；需要设备配对时使用 FPK 方案。

## 运行边界与排障

采集逻辑读取本机状态，不提供磁盘、阵列或容器控制接口。随附 systemd 单元使用 `ProtectSystem=strict`、`ProtectHome`、`PrivateTmp`、`NoNewPrivileges` 及能力、地址族、CPU、内存限制；**它的服务身份仍是 root**，与 FPK 的包用户管理服务不同。安装与卸载脚本会修改本采集器的程序和服务文件，不能将部署操作也称为只读。

| 现象 | 检查 |
| --- | --- |
| SSH / sudo 安装失败 | NAS 地址、SSH 认证、sudo 非交互权限、本机 `sshpass` 是否可用 |
| 端口无法访问 | `journalctl`、实际 `--bind` / `--port`、本机防火墙与路由 |
| 容器或温度缺失 | Docker Socket / hwmon 来源及服务身份的读取权限 |
| 健康探针可达但值不更新 | 查看数据年龄和逐段错误；端口监听不等于采集正常 |
| 固件仅显示旧清单 | 确认部署的是当前采集源码，检查响应与固件字节预算 |

固件上手、动态清单与许可证见 [项目 README](../README.md)。飞牛日常使用、升级保留、证书固定与容器辅助进程见 [应用 README](fpk/README.md)。
