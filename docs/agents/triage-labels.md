# 分诊角色

以下默认词汇用于 [本地任务](issue-tracker.md) 的 `Status:` 字段，尚未在 GitHub 等外部平台创建标签。

| skills 角色 | 本地值 | 含义 |
| --- | --- | --- |
| `needs-triage` | `needs-triage` | 尚需核对问题、范围和证据 |
| `needs-info` | `needs-info` | 缺少影响正确性的必要信息 |
| `ready-for-agent` | `ready-for-agent` | 行为、范围和验收已明确，可由代理执行 |
| `ready-for-human` | `ready-for-human` | 需要人工操作或判断 |
| `wontfix` | `wontfix` | 已明确决定不处理，并记录理由 |

这些角色表达分诊结果，不代表实现、发布或实机验收完成。设备操作需要人工参与与是否已获得操作授权是两件事；不能用标签替代授权。特定 workflow 的执行状态按其自身约定记录，与分诊角色区分。
