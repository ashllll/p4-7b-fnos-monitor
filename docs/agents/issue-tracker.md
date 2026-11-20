# 任务入口：本地 Markdown

在用户未选择外部任务平台时，skills 默认采用本地 Markdown，沿用项目已存在的本地记录方式。GitHub remote 的存在不代表已选择 GitHub Issues，也不自动授权创建 Issue、标签或评论。

- 既有跨版本验收清单继续引用 [未闭环事项](../open-items-2026-10-11.md)，保留原审计编号及历史边界，不批量迁移或改写状态。
- 本轮 UI 已完成修复及残余验收范围以 [并行缺陷复核](../ui-post-motion-audit-2026-10-14.md) 为准；主机或短时设备检查通过不自动关闭长期验收项。
- 后续确需规格或拆票时，按需建立 `.scratch/<feature>/spec.md` 和 `.scratch/<feature>/issues/<NN>-<slug>.md`，每票一个文件、从 `01` 顺序编号；不为已完成工作补建空票。
- 分诊角色在票据顶部的 `Status:` 字段记录，词汇见 [triage-labels.md](triage-labels.md)。执行进展、验证证据、未完成项和讨论追加到对应票据，保留历史。

skills 要求“发布规格／票据”时，在上述本地目录创建文件；要求“获取票据”时，读取用户提供的路径或对应编号。引用代码评审基线时固定到实际提交或任务开始快照，不能用移动中的分支代替固定证据。

## Wayfinding operations（本地 Markdown）

一个 effort 一个地图，决策票与实施票分开：

- 地图：`.scratch/<effort>/map.md`，`Kind: wayfinder:map`，`Status: open`；正文固定 `Destination`、`Notes`、`Decisions so far`、`Not yet specified`、`Out of scope`。地图只索引已关闭决策票的答案，不列 open 票。
- 子票：`.scratch/<effort>/issues/<NN>-<slug>.md`，`Kind: wayfinder:<research|prototype|grilling|task>`，`Parent:` 指向地图；`Status: open|closed`；`Assignee:` 为空表示未认领，开始处理前必须写入当前处理者，未写不算 claim；`Blocked by:` 用相对链接和可读标题写出真正阻塞它的票。
- Wayfinding 票的 `Status` 只表示决策票是否关闭，和分诊角色分开；不使用 `needs-triage`、`ready-for-agent` 等分诊标签。
- 问题保留在 `## Question`；答案写入独立 `## Resolution`，再把状态改为 `closed`。`grilling`、`prototype` 或明确标 HITL 的票必须与用户交流确认；`research`、`task` 等 AFK 票按事实或任务证据解决，不强行等待用户。
- frontier：`Status: open`、`Assignee:` 为空、且所有 `Blocked by:` 票已关闭。用手工 `rg`/`find` 查询；不增加管理脚本或依赖。
- 实施、设备执行和提交推送不因创建决策票而获得授权，也不写成已完成。

当前不启用外部 PR 作为自动分诊来源。后续用户明确选择 GitHub、GitLab、Jira 或其他平台时，再修改本入口；平台操作、提交及推送继续按当前授权执行。
