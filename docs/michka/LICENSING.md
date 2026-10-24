# 上游协议继承与发行政策

日期：2026-10-22 · 状态：accepted（用户明确要求）。
决策：[ADR-0011](../adr/0011-michka-upstream-license-inheritance.md)。

> **本迁移继承 michka 上游 CC BY-NC 4.0。michka 原始代码、资源以及对它们的衍生修改保留此许可、原作者署名和修改记录；不得重新包装为本项目自有的 Apache-2.0／MIT 代码。**

本文件是项目的许可处理与发行要求，不替代完整许可原文。具体许可授权以权利人的声明与相应条款为准。

## 1. 上游身份与固定来源

| 项目 | 已核对内容 |
| --- | --- |
| 名称 | Michka — Cross-platform Hardware Monitor Dashboard |
| 来源 | [michaeldallariva/michka](https://github.com/michaeldallariva/michka) |
| 固定提交 | `3213285d6c22e786c0d4349ee3c0813cc98ab16d` |
| 原作者 | Michael DALLA RIVA / Patrick DUQUESNOY |
| 原版权声明 | Copyright (c) 2025–2026 michka.org |
| 上游许可 | Creative Commons Attribution-NonCommercial 4.0 International，CC BY-NC 4.0 |
| 许可原文 | [上游LICENSE](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/LICENSE)、[CC完整条款](https://creativecommons.org/licenses/by-nc/4.0/legalcode.en) |
| 本地原样副本 | [UPSTREAM-LICENSE.txt](UPSTREAM-LICENSE.txt)；Git blob SHA `e569daa9d5911276acde62a387a09a06d57e2d20` |

上游LICENSE不是CC完整法律文本，只是项目声明和第三方列表，因此同时保留完整条款链接。上游升级时重新读取目标提交的LICENSE并比较，不凭旧表格断言新版本许可相同。

## 2. “继承上游协议”的明确含义

1. **原始上游材料**：保留CC BY-NC 4.0及来源、版权、作者、许可和免责等原有通知。
2. **上游衍生修改**：本项目按用户选择继续采用CC BY-NC 4.0，保留原作者并标注本地修改者、修改日期和内容。
3. **新迁移专用模块**：在作者有权授权的范围内，计划采用CC BY-NC 4.0与迁移项目一致；在首次加入源码时明确文件／目录范围。引入第三方代码时按其原许可处理，不能仅凭目录位置统一换许可。
4. **现有独立Apache-2.0代码**：原固件、采集器和飞牛应用已有授权继续保留；本轮不替换根LICENSE，不追溯改变历史发行物。
5. **第三方依赖**：保留MIT、Apache-2.0、zlib-style、OFL等各自条款；它们不会因为被打包就全部变成CC BY-NC。
6. **组合发行物**：列明整体迁移政策和各部分许可，不宣传“整个包仅Apache-2.0”或“全部允许自由商用”。

CC BY-NC 4.0没有ShareAlike（SA）条款。**继续以相同协议许可本地衍生修改，是本项目主动确定的政策；不能写成CC BY-NC本身具有SA强制继承机制。** 依据为CC条款中对Adapter's License及下游限制的规定。[完整条款](https://creativecommons.org/licenses/by-nc/4.0/legalcode.en)

## 3. 非商业、署名与修改标注

上游许可限定非商业使用。免费分发并不自动说明用途符合NonCommercial定义；涉及商业用途时不能只删除版权、把修改代码重命名或改根LICENSE来绕开。若需要超出既有授权的用途，应先取得相关权利人适用的单独许可。[CC条款第1、2节](https://creativecommons.org/licenses/by-nc/4.0/legalcode.en)

分享时按适用条件保留作者／版权／许可通知，提供来源及许可链接，标明修改并保留已有修改说明；不能暗示上游作者为本迁移背书，也不能施加妨碍接收者行使许可权利的额外法律或技术限制。[CC条款第2、3节](https://creativecommons.org/licenses/by-nc/4.0/legalcode.en)

项目介绍应准确写“源码公开，遵循CC BY-NC 4.0非商业许可”，不要写“无商业限制的开源软件”。如需要对具体分发方式作判断，以实际材料、使用目的及权利人授权为依据。

## 4. 按来源管理，避免许可证覆盖错误

| 材料 | 许可处理 | 记录位置 |
| --- | --- | --- |
| 原版michka源码／网页资源 | 原样保留上游声明 | 上游LICENSE、来源锁定记录 |
| 修改的michka布局／后端／资源 | 继承CC BY-NC 4.0，记录差异 | 修改清单、文件说明、发行说明 |
| 原创网关／适配器／新客户端代码 | 本项目计划采用CC BY-NC 4.0，核对作者授权 | 新模块LICENSE／SPDX及来源表 |
| 复用当前项目Apache代码 | 保留Apache要求和原版权，不能仅改SPDX | 原文件通知、目录说明、依赖清单 |
| 原第三方库／字体／运行时 | 保留各自许可及必要通知 | THIRD_PARTY_NOTICES或相同用途文件 |
| 混合来源文件／无法确认归属 | 先查来源和许可兼容，未明确前不发行 | 发行阻塞项与处理证据 |

“独立模块”不能仅根据放在不同目录判断。若复制、翻译或改编上游实现，要记录真实来源，不将其标成完全原创。

## 5. 署名与修改记录模板

下面是未来迁移发行物的模板。未实现的功能不得填成已完成。

```text
Based on Michka — Cross-platform Hardware Monitor Dashboard
Upstream: https://github.com/michaeldallariva/michka
Upstream revision: <实际使用的提交>
Copyright (c) 2025–2026 michka.org
Authors: Michael DALLA RIVA / Patrick DUQUESNOY

Michka and our adaptations of its materials are distributed under
CC BY-NC 4.0:
https://creativecommons.org/licenses/by-nc/4.0/

Local modifications by: <实际修改者>
Modification date/version: <日期与版本>
Changes: <实际修改摘要，或指向随包提供的修改清单>

Original third-party and independently licensed components retain
their respective licenses; see the included license inventory.
This adaptation is not represented as endorsed by the upstream authors.
```

至少放在迁移项目README及随包版权说明中，并在界面About／关于页提供可达入口。源码档、二进制包、镜像均须带适用许可和通知；只在GitHub网页写一句署名不足以覆盖离线交付。

修改清单字段：上游路径／本地路径、上游提交、修改者、日期、变更摘要、适用许可、来源证据。原创文件可使用 `SPDX-License-Identifier: CC-BY-NC-4.0`；不要给未经审查的混合来源文件机械替换SPDX。

## 6. 第三方清单

上游LICENSE列出了.NET、Microsoft库、SQLitePCLRaw、ECharts及SQLite等来源。该列表作为起点，**不是本项目最终完整依赖清单**。实际发行还可能包含Chromium、TLS／WebSocket库、ESP-IDF、BSP、LVGL、JPEG接口、字体及其传递依赖，应按实际打包方式补齐版本与通知。

每项记录：名称、版本、来源URL／提交、许可证、是否修改、是否随包分发、许可证文件位置。操作系统提供的组件与随包组件分别记录，避免声称打包了未打包的软件或漏掉实际依赖。

## 7. 发行检查清单

- [ ] 锁定实际michka提交，原LICENSE与版权完整保留。
- [ ] michka及衍生修改明确继承CC BY-NC 4.0，不出现Apache-only／MIT-only误导。
- [ ] 所有本地改动有修改者、日期、范围、来源；介绍没有暗示上游背书。
- [ ] 旧Apache代码、第三方库和字体许可分别保留，混合来源疑点已解决。
- [ ] 所有新增迁移模块的作者授权和许可范围明确。
- [ ] README、About、发布说明和离线包署名相互一致。
- [ ] 源码、二进制、镜像均包含适用通知；完整条款链接有效。
- [ ] 发行说明明确非商业限制，不宣称无条件商用。
- [ ] 上游升级后的许可变化已核对；无来源未明或许可未决材料。
- [ ] 凭据、私有配置、真实接口响应、个人固件／截图／日志未混入发行物。

由 AT-16 记录实际发行检查证据。初稿阶段只制定政策、保存上游许可副本；本次仅发布规划、来源和许可副本，不发行 michka 应用或远程客户端，也不重新许可整个现有仓库。
