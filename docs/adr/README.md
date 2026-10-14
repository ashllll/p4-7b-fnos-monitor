# 架构决策记录

以下 ADR 于 2026-10-14 补录本次已落地的选择，状态为 accepted；补录没有新增实现或设备操作。术语见 [GLOSSARY](../../GLOSSARY.md)。

工程 skills 的领域文档消费规则见 [domain.md](../agents/domain.md)，任务入口见 [issue-tracker.md](../agents/issue-tracker.md)，分诊角色见 [triage-labels.md](../agents/triage-labels.md)。

最新默认 UI 与发布记录见 [ULTRA 归档](../ui-ultra-release-2026-10-14.md)。前一阶段完整流程、追加修复与验证见 [Skills 全流程复核](../skills-workflow-2026-10-14.md)。

| ADR | 决策 |
| --- | --- |
| [0001](0001-native-instrument-ui.md) | 通过共享原生 LVGL 构件统一横屏仪表界面 |
| [0002](0002-continuous-telemetry-motion.md) | 连续遥测保留位置与速度，使用共享临界阻尼 |
| [0003](0003-module-freshness-and-reading-presence.md) | 读数存在性、模块新鲜度和统计新鲜度分别判断 |
| [0004](0004-identity-and-status-colors.md) | 指标身份色与当前状态色分离，默认 ULTRA 主题统一 |

当前设计合同见 [全界面 CUKTECH 仪表语言](../ui-cuktech-2026-10-14.md)，参数与取舍见 [电流表式动效](../ui-ammeter-motion-2026-10-14.md)，最新缺陷回归及最终烧录证据见 [并行缺陷复核](../ui-post-motion-audit-2026-10-14.md)。历史文档中的窄屏、字体及阶段产物记录不替代这些当前约定。

后续决策顺序编号；改变既有决定时新增 ADR 并标记被替代项，保留原有理由。只有影响跨页架构、数据语义或设备边界的取舍进入 ADR，普通样式调整和缺陷修复留在对应设计或验证记录。
