# michka 远程触控迁移文档集

初稿：2026-10-22；发布复核：2026-10-25。文档版本：0.2。阶段：迁移规划，完整远程客户端尚未交付和验收。

本次发布整理规划与来源，不交付远程显示实现。开发侧分段 PoC 记录不等于完整链路验收；本目录未将任何 AT 项标为通过。当前公开固件仍使用原生 LVGL，版本基线见 [SOURCES](SOURCES.md)。

**产品方向已确认：保留 ESP32-P4-WIFI6-Touch-LCD-7B、1024×600 横屏；原版 michka 在 NAS／电脑运行，开发板接收画面并回传触摸；先验证流畅度。**

**许可政策已确认：michka 原始代码及其衍生修改继承上游 CC BY-NC 4.0，保留署名、许可和修改记录；不得把它们重新标成 Apache-2.0／MIT。独立的既有代码和第三方依赖保留各自原许可。** 详见 [许可政策](LICENSING.md) 与 [ADR-0011](../adr/0011-michka-upstream-license-inheritance.md)。

## 阅读入口

| 文档 | 解决的问题 |
| --- | --- |
| [PRD](PRD.md) | 为谁做、必须保留什么、用户操作与产品验收 |
| [架构与接口](ARCHITECTURE.md) | 渲染／传输／显示职责、协议草案、状态机、资源和数据映射 |
| [迁移执行计划](MIGRATION.md) | 阶段依赖、实施落点、交付物、失败退出与回退 |
| [验收与证据](ACCEPTANCE.md) | 流畅度、画质、异常、语义和长测如何证明 |
| [上游协议继承与发行](LICENSING.md) | 许可范围、署名模板、第三方清单、发布门槛 |
| [来源与基线](SOURCES.md) | 固定提交、当前资产、外部依据与未验证项 |

## 架构决策

- [ADR-0008](../adr/0008-michka-remote-display-direction.md)：原版 michka + P4 远程触控终端。accepted，方向确认，完整迁移未交付。
- [ADR-0009](../adr/0009-jpeg-remote-display-poc.md)：JPEG 全帧 + WebSocket + 浏览器输入注入。proposed，以 PoC 为准。
- [ADR-0010](../adr/0010-fnos-michka-data-adapter.md)：保留 fnOS 采集与状态语义，采用适配器和扩展。proposed。
- [ADR-0011](../adr/0011-michka-upstream-license-inheritance.md)：继承上游许可并按来源划分范围。accepted，用户已要求。

accepted 表示决策被接受，不等于代码已实现或性能已通过。ADR-0001～0007 继续描述现有固件；迁移方向不直接撤销现有实现合同。

## 文档约定

- 产品需求编号 FR；性能与可用性目标编号 NFR；测试编号 AT；工作包编号 WP。未勾选清单都表示尚未完成。
- 需求以 PRD 为准；测量方法和门槛以 ACCEPTANCE 为准；技术候选以 ARCHITECTURE 和 ADR 为准；许可以 LICENSING 及所链接原文为准。
- 阈值是当前计划目标，必须通过实测。不能把目标改小后沿用旧“通过”结论。
- 初始会话规格留在本地排除目录，不是公开文档依赖；本目录是后续维护入口。跨文档改动必须同步需求、测试及 ADR 的关联。
- 发布这些文档不表示计划步骤已执行，也不扩大后续设备操作或服务部署的授权。

使用技能：interface-design、codebase-design、domain-modeling、to-spec；外部依据直接核对原始资料。
