# README 产品流程图

使用 [diagram-design](https://github.com/cathrynlavery/diagram-design) 2.6 重绘，技能基线 `11b08eade8049c441ed3df1895e3b527c26c0fbe`，MIT。HTML 是可编辑源文件，SVG 由技能自带的导出器生成，README 直接显示 SVG。

| 图 | 源文件 | README 图片 | 范围 |
| --- | --- | --- | --- |
| 首次配对 | [HTML](first-connection.html) | [SVG](first-connection.svg) | 默认 HTTPS 的准备、人工指纹核对、配对结果与失败出口 |
| 日常数据流 | [HTML](runtime-data-flow.html) | [SVG](runtime-data-flow.svg) | NAS 本机来源、可选 Docker、管理网关、HTTPS 请求／响应、快照与 LVGL |

设计选择：`fit` 画布（960×720、960×680）、`balanced` 细节、面向安装与开发读者；静态、无脚本、无远程字体或其他外部资源。配色和字体回退见[图表样式](style-guide.md)。这两张图是文档示意，不是实机截图，也不增加设备验收结论。

重绘时先核对 `fnos_pair.c` 的 `do_fetch`／`do_pair`、`fnos_data.c` 的 `poll_task`／`note_failure` 及 NAS 应用的路由、采集实现。首次公开证书引导不能替代人工核对指纹；日常 HTTPS 请求由开发板发起，UI 不直接访问网络。

从仓库根目录执行以下命令；需要已安装 `diagram-design` 和 Python 3.10+，不需要 Playwright：

```bash
python3 "$HOME/.agents/skills/diagram-design/scripts/self_check.py" --offline docs/diagrams/first-connection.html
python3 "$HOME/.agents/skills/diagram-design/scripts/self_check.py" --offline docs/diagrams/runtime-data-flow.html
python3 "$HOME/.agents/skills/diagram-design/scripts/export_svg.py" docs/diagrams/first-connection.html --system-fonts
python3 "$HOME/.agents/skills/diagram-design/scripts/export_svg.py" docs/diagrams/runtime-data-flow.html --system-fonts
```

导出后用浏览器检查两张 SVG 的文本、分支、箭头方向和间距，再提交 HTML 与 SVG。自动 self-check 检查无障碍名称、单文件安全与画布边界；不能替代字体和连线路由的目视检查。

导入保真记录：配对草稿 10 个节点／9 条边合并为 9 个节点／8 条边，仅将「安装 NAS 应用」与「生成配对码」合并为一个准备步骤；指纹及配对结果的所有出口保留。数据流 7 个节点／7 条边全部保留，新增两个区域框用于区分 NAS 和开发板；区域框不代表防火墙或额外服务。丢弃 Mermaid 的自动布局和样式。
