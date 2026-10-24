# 架构决策记录

ADR-0001～0007 记录现有原生固件的选择与后续演进；ADR-0008 起记录 2026-10-22 的 michka 迁移方向、候选方案及许可政策。状态以各记录为准，accepted 表示决策接受，不自动表示已实现或实机通过。术语见 [GLOSSARY](../../GLOSSARY.md)。

工程 skills 的领域文档消费规则见 [domain.md](../agents/domain.md)，任务入口见 [issue-tracker.md](../agents/issue-tracker.md)，分诊角色见 [triage-labels.md](../agents/triage-labels.md)。

当前默认主题与显示栈见 [michka 石板灰](../ui-michka-slate-2026-10-22.md)和[原生裁剪揭示验证](../ui-drag-lvgl96-2026-10-24.md)；温度页后续调整见[温度页记录](../ui-temperature-compact-2026-10-24.md)。[ULTRA 归档](../ui-ultra-release-2026-10-14.md)和[Skills 全流程复核](../skills-workflow-2026-10-14.md)保留各自历史阶段。

| ADR | 决策 |
| --- | --- |
| [0001](0001-native-instrument-ui.md) | 通过共享原生 LVGL 构件统一横屏仪表界面 |
| [0002](0002-continuous-telemetry-motion.md) | 连续遥测保留位置与速度（求解器及参数由 0005 替代） |
| [0003](0003-module-freshness-and-reading-presence.md) | 读数存在性、模块新鲜度和统计新鲜度分别判断 |
| [0004](0004-identity-and-status-colors.md) | 指标身份色与当前状态色分离，默认 ULTRA 主题统一 |
| [0005](0005-library-springs-and-attached-cursors.md) | 复用开源阻尼求解器，圆环、条形和曲线游标直接附着图形 |
| [0006](0006-network-flow-curve.md) | D3 B 样条与原生绘线；网络独占范围及单次平均由 0007 替代 |
| [0007](0007-all-trend-flow-smoothing.md) | 全部遥测趋势使用双重移动平均，保留原始测量与状态语义 |
| [0008](0008-michka-remote-display-direction.md) | accepted：原版 michka 外部运行，P4 保留为远程触控终端；完整迁移未交付 |
| [0009](0009-jpeg-remote-display-poc.md) | proposed：JPEG 全帧与浏览器触摸注入，先做性能去留验证 |
| [0010](0010-fnos-michka-data-adapter.md) | proposed：保留 fnOS 采集器和状态语义，以适配器／扩展接入 |
| [0011](0011-michka-upstream-license-inheritance.md) | accepted：继承上游 CC BY-NC 4.0，保留独立代码和第三方原许可 |

michka 的 [PRD、架构、迁移与验收文档](../michka/README.md) 是新路线的维护入口；它们不改变当前固件已验证范围。

CUKTECH 阶段设计合同见 [全界面 CUKTECH 仪表语言](../ui-cuktech-2026-10-14.md)，当时参数与取舍见 [电流表式动效](../ui-ammeter-motion-2026-10-14.md)，该阶段缺陷回归及烧录证据见 [并行缺陷复核](../ui-post-motion-audit-2026-10-14.md)。历史文档中的窄屏、字体及阶段产物记录不替代当前源码与验收记录。

后续决策顺序编号；改变既有决定时新增 ADR 并标记被替代项，保留原有理由。只有影响跨页架构、数据语义或设备边界的取舍进入 ADR，普通样式调整和缺陷修复留在对应设计或验证记录。
