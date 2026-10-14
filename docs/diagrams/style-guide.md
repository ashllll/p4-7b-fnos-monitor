# 图表样式

沿用用户已确定的 CUKTECH 仪表视觉，不修改全局技能的默认皮肤或其他项目的品牌。颜色来自当前 [P_CUKTECH_CONSOLE](../../components/fnos_monitor/ui_kit/uk_theme.c)；图表仅将它们映射为 diagram-design 的语义角色。

| 语义角色 | 值 | 来源／用途 |
| --- | --- | --- |
| paper | `#000000` | 背景 |
| paper-2 | `#101010` | 节点表面 |
| ink | `#F8F8F8` | 主文字 |
| ink-strong | `#101010` | 亮底反色文字，本组图未使用 |
| muted | `#E0E0E0` | 默认连线、技术标签 |
| soft | `#B8B8B8` | 次级文字、节点边框 |
| rule | `#282828` | 装饰区域边界与分隔线 |
| rule-solid | `#B8B8B8` | 必要边框 |
| accent | `#75BFFF` | 每图至多两个焦点；不表示健康 |
| accent-tint | `#181818` | 焦点表面 |
| link | `#8ED7E5` | HTTPS 请求与响应 |

标题、节点和中文说明使用无衬线字体栈 `Noto Sans SC, PingFang SC, Microsoft YaHei, system-ui, sans-serif`；技术标识使用 `ui-monospace, SFMono-Regular, Consolas, monospace`。字体来源为系统，保留各平台回退；没有嵌入固件字库，也不宣称与开发板字体完全一致。为适应 GitHub README 正文栏缩放，覆盖技能标准字号：标题 28 px、节点 20 px、中文说明与分支标签 14 px、技术标签 12 px、英文眉题 8 px。主节点缩短文案，尺寸不够时拆成主文字与辅助说明；两张 SVG 声明原生宽高，点击可查看完整矢量图。

节点结构沿 4 px 栅格，连线使用直线或半径 8 px 的正交转角。判断分支以文字标注，箭头标签留独立背景及 8 px 间距；成功与停止依靠形状及文案识别，蓝色只突出关键关口与最终仪表。
