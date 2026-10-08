# 验证索引

本发布副本保留可复现的源码、匿名测试和聚合验证结果。
原始串口日志、手机相机预览截图及真实 NAS 响应只保存在开发目录。
旧版逐设备读数和网络信息不随源码发布。

## 原生界面与硬件自适应

最终 UI 源码基线见 [硬件自适应记录](ui-hardware-adaptive-audit-2026-10-08.md)。
已完成 8 个尺寸的状态渲染、16 个匿名硬件组合、导航/配对交互、动态快照 ASan/UBSan
和 UI 分配失败注入。测试数据中的数量用于回归，不是生产硬件上限。

这轮固件曾在 ESP32-P4 rev3.2 实机烧录并观察五页及刷新，详情和证据边界见上述记录。
构建成功或主机预览不等同于物理验收；单张手机截图不能证明连续帧率、无闪烁或色彩准确度。
发布构建使用模板配置，与带本地配置的实机固件分别记录，不上传个人固件。

## 配套应用

源码与安装包为 1.2.4。此前 1.2.3 有 NAS 安装/升级记录；本发布的 1.2.4 不据此宣称
已经完成真实 NAS 安装或官方应用库审核。默认全量容器链路的启动和名称修复见
[1.2.4 发布验证](release-1.2.4.md)。

重跑入口：`nas/fpk/test_lifecycle.sh`、`docker_check.py`、`collector_check.py`、
`startup_permission_check.py`、`config_persistence_check.py`、`contract_check.py`。
这些检查使用匿名临时数据或开发机 fallback，不冒充 Linux 包用户权限的实测结果。

历史技术记录：

- [容器权限与固定 GET 链路](fnos-docker-fix-2026-10-07.md)
- [启动、升级与配置持久化](fnos-startup-fix-2026-10-07.md)
- [卡片自适应](ui-card-adaptive-audit-2026-10-08.md)
- [原生 UI 实现](ui-v12-lvgl-native.md)
