# 图表样式

沿用用户已确定的 CUKTECH 仪表视觉，不修改全局技能的默认皮肤或其他项目的品牌。颜色来自当前 [P_CUKTECH](../../components/fnos_monitor/ui_kit/uk_theme.c)；图表仅将它们映射为 diagram-design 的语义角色。

| 语义角色 | 值 | 来源／用途 |
| --- | --- | --- |
| paper | `#000000` | 背景 |
| paper-2 | `#080D12` | 节点表面 |
| ink | `#F5F7FA` | 主文字 |
| ink-strong | `#080D12` | 亮底反色文字，本组图未使用 |
| muted | `#D4DFE8` | 默认连线、技术标签 |
| soft | `#A5B4C2` | 次级文字、节点边框 |
| rule | `#25313E` | 装饰区域边界与分隔线 |
| rule-solid | `#A5B4C2` | 必要边框 |
| accent | `#FFC800` | 每图至多两个焦点；不表示健康 |
| accent-tint | `#24200A` | 焦点表面 |
| link | `#37C4D6` | HTTPS 请求与响应 |

标题、节点和中文说明使用无衬线字体栈 `Noto Sans SC, PingFang SC, Microsoft YaHei, system-ui, sans-serif`；技术标识使用 `ui-monospace, SFMono-Regular, Consolas, monospace`。字体来源为系统，保留各平台回退；没有嵌入固件字库，也不宣称与开发板字体完全一致。为适应 GitHub README 正文栏缩放，覆盖技能标准字号：标题 28 px、节点 20 px、中文说明与分支标签 14 px、技术标签 12 px、英文眉题 8 px。主节点缩短文案，尺寸不够时拆成主文字与辅助说明；两张 SVG 声明原生宽高，点击可查看完整矢量图。

节点结构沿 4 px 栅格，连线使用直线或半径 8 px 的正交转角。判断分支以文字标注，箭头标签留独立背景及 8 px 间距；成功与停止依靠形状及文案识别，黄色只突出关键关口与最终仪表。
