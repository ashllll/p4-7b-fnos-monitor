# README 产品流程图

README 的两张流程图由手写 SVG 作为唯一源文件，动图 GIF 由 SVG 加 motion spec 渲染。GitHub 不在 Markdown 里播放 SVG 内部动画，所以 README 嵌入 GIF，仓库保留 SVG 供编辑与静态引用。

| 图 | 可编辑源 | motion spec | README 动图 | 尺寸 | 范围 |
| --- | --- | --- | --- | --- | --- |
| 首次配对 | [first-connection.svg](first-connection.svg) | [first-connection.motion.json](first-connection.motion.json) | [assets/readme/first-connection.gif](../../assets/readme/first-connection.gif) | 1200×620 · 130 帧 · 5.4 s | 六位配对码、明文只取证书、人工核对指纹、HTTPS 提交、令牌落盘与失败出口 |
| 日常数据流 | [runtime-data-flow.svg](runtime-data-flow.svg) | [runtime-data-flow.motion.json](runtime-data-flow.motion.json) | [assets/readme/runtime-data-flow.gif](../../assets/readme/runtime-data-flow.gif) | 1200×620 · 134 帧 · 5.6 s | NAS 只读采集与快照、可选 Docker、遥测服务两条通路、`fnos_poll` 拉取、环形缓存与 LVGL 显示 |

## 事实依据

两张图只写实现里查得到的东西，改图前先复核这些位置：

- 配对：`components/fnos_monitor/fnos_pair.c` 的 `do_fetch`（明文只请求 `/api/v1/identity`）与 `do_pair`（HTTPS `POST /api/v1/pair`，`pair_response_accept` 要求传输成功、HTTP 200 且令牌非空）；限流常量在 `nas/fpk/nasscreencompanion/app/server/nas_companion_server.py`。
- 数据流：`components/fnos_monitor/fnos_data.c` 的 `fnos_poll`、退避与离线判定，`fnos_data.h` 的环形缓冲深度，`nas/fpk/nasscreencompanion/app/server/fnos_collector.py` 的采样与历史长度，以及 `nas_companion_server.py` 的路由表。

屏幕端只收六位数字配对码，NAS 地址与端口来自配对结果或编译期默认值，不在屏幕上配置；取证书、TLS 探针与配对请求都发生在 `fnos_pair` 任务，遥测请求发生在 `fnos_poll` 任务，UI 只读快照。

## 重新渲染

需要 `rsvg-convert`、`ffmpeg` 和带 Pillow 的 Python（本机用 `/usr/bin/python3`），以及 `beautify-github-readme` 技能自带的 `render_motion_gif.py`：

```bash
SKILL="$HOME/.dsh/skills/beautify-github-readme/scripts/render_motion_gif.py"

rsvg-convert docs/diagrams/first-connection.svg -o /tmp/first-connection.png

/usr/bin/python3 "$SKILL" docs/diagrams/first-connection.svg \
  assets/readme/first-connection.gif --spec docs/diagrams/first-connection.motion.json

/usr/bin/python3 "$SKILL" docs/diagrams/runtime-data-flow.svg \
  assets/readme/runtime-data-flow.gif --spec docs/diagrams/runtime-data-flow.motion.json
```

渲染完检查四件事：`rsvg-convert` 出的静态图没有文字溢出与压线；GIF 尺寸与帧数符合上表；单文件小于 2 MB；首帧与末帧都只剩背景，循环播放不跳变。

## 维护约定

- 动图里每个要移动的元素都要有唯一 `id`，SVG 其余内容构成静态底图；改文案就改 SVG，改节奏就改 motion spec。
- 配色沿用主题令牌（近黑画布、灰色卡片、蓝／绿／橙强调），不引入渐变与外部字体，SVG 内联样式保证离线可渲染。
- 这两张图是文档示意，不是实机截图，也不构成设备验收结论。
