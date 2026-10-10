# UI 第二轮迭代（ui-skills 设计审计 → 落地 → 预览回归 → 实机验证）

日期：2026-10-08　范围：设备端 LVGL 五页界面（components/fnos_monitor/fnos_ui.c + ui_kit/）
方法：ui-skills 路由（superfuture/design-review + 本地 interface-design 视角）先出审计清单，再改代码，
用主机预览四条审计 + 每张稳态图对比度做回归，最后 ./idf.sh build → 烧录 → page_shot 实机取证。

## 1. 审计发现与处置

| # | 现象（改前） | 处置 | 落点 |
|---|---|---|---|
| 1 | 每页右缘一条 438px 高的灰竖条（页面对象自己的滚动条，内容偶尔超十几像素就出现） | 页面保留可滚动（不丢数据的保险），但关掉滚动条绘制 | fnos_ui.c ui_page `lv_obj_set_scrollbar_mode(page, OFF)`；外壳 content 同样处理（uk.c） |
| 2 | 配对页原生键盘 3 列 × 4 行 ≈200px，整卡比 grid 视口高，最后一行（清除/0/确认）被底栏截断 | 键盘改 4 列 × 3 行、行距 UK_S1 ⇒ 140px；配对码卡最小宽改按实际列数算 | fnos_ui.c `PAIR_PAD_COLS 4`、PAD 按列重排、pair_layout 的 keys_w |
| 3 | 池/清单底行被容器下沿拦腰截断（总览卷清单、存储三卡、诊断十行） | 新增 `uk_viewport_snap()`：只调行距把"能整行放下的行"铺满视口，下一行整条落到视口外；行高不变、数据不藏、幂等、失败回滚 | ui_kit/uk.c `uk_viewport_snap()`；调用点：uk_pool_relayout 末尾、总览卷清单、告警列 |
| 4 | 温度页展开态 12/48 路通道挤在一列里（池按"一台设备一个条目"，多列逻辑用不上） | 设备块内新增通道网格（ROW_WRAP + 150px 最小格宽、最多 4 列），折叠/展开状态都跟着块宽重算 | fnos_ui.c `TEMP_GRID_CELL_MIN/TEMP_GRID_MAX_COLS` + `temp_grid_relayout()` + temp_blk_build/page_layout(4) |
| 5 | 长列表装得下也不换列，尾巴一律靠滚动 | `uk_pool_relayout` 在"一屏装得下"前提下逐列 +1（受 min_col_w 与 min(n, 列上限) 约束），装不下仍保留滚动 | ui_kit/uk.c |
| 6 | 顶栏常驻「Wi-Fi 未配置」「采集诊断」是实心描边按钮，比状态胶囊还抢眼 | 新增幽灵档 BTN_GHOST（无底色/无描边、按下才有反馈），两个常驻按钮降到这一档，顶栏唯一高饱和元素只剩状态胶囊 | fnos_ui.c 按钮 enum、btn_skin、flex_btn、build_header |
| 7 | 采集诊断十行等权无分组，关键项埋在中段 | 按连接/采集/运行三组，组间发丝线 + CJK12 组名 | fnos_ui.c `AGENT_GROUP[]` + 诊断构建 |
| 8 | 系统页「告警与事件」只有 1~4 条却占满整段高度，健康态大片留白 | 告警卡改按内容定高（上限 150px，超出卡内滚动），富余高度还给容器服务/硬件温度两卡；它跟着 p3 覆盖层一起收 | fnos_ui.c `UI_ALERT_PANEL_MAX_H`、build_p3、system_layout、p3_overlay_sync |
| 9 | 总览「网络吞吐」卡底部注释「N 次采集 · 0-x MB/s」被卡片下沿切一半 | 注释改为在图表**之前**创建：卡片被行高挤压时先让可伸缩的图表少几像素，而不是切半行文字 | fnos_ui.c overview_card 构建顺序 |

## 2. 证据

- 预览回归：`bash tools/preview/run.sh tools/preview/out/iter2-after4`（改前基线 iter2-before）——75 张 1024×600、四条审计全 PASS（阅读顺序 / wifi 卡在 p3 隐藏且顶栏可开 / 最长清单滚动+字形覆盖+诊断切换+告警清除+healthy·waiting·lock-timeout 迁移 / 11 条连接失败原因 / 配对面板 / 温度全通道展开），每张稳态图正文对比度最低 4.64:1（目标 ≥4.5:1），进程退出码 0。
- 像素级复核：右缘竖条（x=1006..1009 各 438 行、RGB(82,80,90)）在 before 的 02-live-p0 / 04-healthy-p0 / 03-offline-p0 存在，after 全部为 0；温度展开态通道行 x 起点 21/263/505…（格宽 242px）⇒ 4 列铺满卡片；总览网络卡注释 12 行完整落在卡内（y=447..458）。
- 栈深：`python3 tools/stack_check.py --elf build/fnos_monitor.elf --objdump $HOME/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-objdump` —— 3526 个函数，lvgl_worker 2672+4400=7072/12288=58% ✓、app_main 4640/16384=28% ✓、poll_task 41% ✓、pair_task 43% ✓。
- 构建：`./idf.sh build` 退出码 0，fnos_monitor.bin 0x6bb0e0 字节（app 分区 0x900000，余 25%）。
- 烧录：`./idf.sh -p /dev/tty.usbmodem5CF71088571 -b 230400 flash` —— 7 057 632 字节写入 0x10000，hash verified，RTS 复位，退出码 0。
- 串口 35 s：无 panic/assert；Wi-Fi 拿到 <BOARD_IP>；"history backfilled: 300 samples"。
- 实机取景（`tools/page_shot.py --page N`，手机相机截图，图在 ~/Documents/ChatGPT/board-camera/）：五页逐页看过——顶栏两个幽灵按钮 + 状态胶囊层级成立；总览网络卡注释完整出现在图表上方；存储三卡每行完整；系统页告警卡收成一条、容器服务/硬件温度两卡拿回高度；温度页 3 列设备网格正常。

## 3. 本轮改动文件与哈希

```
bb7f53c6db70b5ac2082c6966cc50bb5dc96e14a1d9e7e3041e3707772f3afca  components/fnos_monitor/fnos_ui.c
114d470a739fd3ecf15a1a75dc93c58e298340a60b86a9c7e34dd315e7dd0444  components/fnos_monitor/ui_kit/uk.c
95173cd31d8eba88e619ff7ee7ee07ca1e526ecd503963971900210d2a8c47f1  components/fnos_monitor/ui_kit/uk.h
```

## 4. 未做 / 已知项

- 网络页（第 2 页）底部还有第二张「网卡接口」卡，标题只露上半行：页内容比视口高约一张卡。**没有**把 uk_viewport_snap 用到页面级——它的"整行铺满视口"策略对高度差大的页面级卡片会把行距撑出大空档（推演 gap 会涨到 ~116px），比露半截更糟；页面本身仍可拖动滚动，数据可达。
- 温度页展开态在实机上需要手指点一下（GT911 触摸无法自动化），实机只验收了折叠态；展开态证据来自主机预览（48 路 4 列）。
- 未发布：开发仓禁止发布/推送，release 仓仍停在 1.2.4（HEAD ce67298）。
