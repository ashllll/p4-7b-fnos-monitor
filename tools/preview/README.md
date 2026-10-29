# tools/preview —— 主机预览（不烧录的像素级验收）

把**设备端真实的那套 UI 代码**编译到 macOS 上渲染成 PNG：同一份 LVGL 9.5、
同一份生成字库、同一份 `ui_kit/uk.c` + `ui_kit/uk_theme.c` + 同一份 `fnos_ui.c`（六页版面）。
只有板级接口是替身（`stub/`：`esp_timer.h` / `esp_heap_caps.h` / `esp_log.h` / `sdkconfig.h` / `fnos_config.h`），
普通 fixture 通过快照 API 构造数据；硬件组合 fixture 使用设备端同一份 `fnos_snapshot.c` 解析匿名 JSON。
格式化、阈值配色、可信度降级和自适应排布都走真实刷新逻辑。

改完 `fnos_ui.c` 可以先跑主机预览检查布局与状态。物理屏幕、触摸、DMA 与 Wi-Fi 时序仍需另外验证。

## 用法

本文命令统一从**仓库根目录**执行。先完成一次 `./idf.sh build`，再使用 `run.sh` 构建主机预览。
默认输出目录是 `tools/preview/out/`；输出名称必须为 `tools/preview/` 内的相对路径。
非空目录须有脚本创建的所有权标记；无标记的旧目录请改用新名称，不要指向源码目录。

```bash
bash tools/preview/run.sh [输出目录]     # 默认 tools/preview/out/；内部已 export DEVELOPER_DIR
open tools/preview/out/02-live-p0.png
```

当前完整流程生成 **126 张 1024×600 状态 PNG，另加 5 张刷新回归 PNG**，覆盖启动/等待/在线告警/离线/正常/满列表/旧版采集端/夜间/配网/配对等状态 × 六页，
外加列表底部、诊断面板与配对全流程。内置断言验证缺字/文本高度、子对象越界、卡片重叠、满列表滚动、
告警消退、诊断按钮、锁超时保留快照、连接失败原因逐个标签可翻译。
行内数值还检查数字与单位的总宽度；配对检查键盘可达及页脚固定，配网检查六阶段按钮宽度、
41 个键的行内边界、滚动后的可见性和固定页脚。
拆开跑则 `tools/preview/build/preview <目录>`（写 PPM）+ `python3 tools/preview/ppm2png.py <目录>`。

默认跟随固件 `CONFIG_FNOS_PALETTE`（当前 `michka`）的仪表主题及底部导航见 [最新设计记录](../../docs/ui-cuktech-2026-10-14.md)。新增分类筛选、恢复全部、跨页保留筛选、详情遮罩阻挡底层输入、返回列表及冻结实体身份的真实指针测试；输出 `13-*` 七帧，包含长名称详情与筛选空结果。默认流程还通过指针点击全部导航项与五个详情入口，并生成长连接失败原因、网口占用、磁盘指标、容器资源和资源未采集样张。信息增强见 [说明](../../docs/ui-telemetry-2026-10-12.md)。

每张快照（连同夜间矩阵与夜间样张）还会扫一遍可见文案里有没有**没被消费掉的格式说明符**
（`%d`/`%s`/`%u`，裸 `%` 与 `%%` 放行）：`set_txt(x, "%s", cond ? "A" : "容器 %d/%d 运行", n, m)`
会把 `%d/%d` 原样印到设备屏上，而这个状态当时可能没有样张覆盖——规则挂在**每个**审计面上才不会漏。

每张稳态快照还检查可见正文的文字/底色对比度，目标为 **4.5:1**。
底色逐层合成透明背景，文字也计入透明度；每张图打印最小值。
此项检查使用原生样式颜色，不能代替实物面板的环境光和色彩测量。
健康样例与满列表样例分别验证 8、16 个容器在反复切页和自适应重排后保持采集器顺序。

### 历史多尺寸工具（已退出当前验收范围）

2026-10-14 用户明确要求后续仅迭代 1024×600 开发板横屏，不再迭代窄屏或竖屏。以下保留为历史工具用法，当前验收不运行多尺寸矩阵。

```bash
PREVIEW_SIZE=800x480  tools/preview/build/preview /tmp/out-800    # 同一份 UI，矮屏一档
PREVIEW_SIZE=1280x800 tools/preview/build/preview /tmp/out-1280   # 大一档
```

面板只有 1024×600 一档，所以**实机验不了别的分辨率**；而"自适应"这句话必须拿别的尺寸真出一张图
才算证明。`PREVIEW_SIZE=WxH`（≤1280×800）把同一份 `fnos_ui.c` 渲到别的分辨率上，各项审计照跑。
实测：1280×800 全绿；800×480 曾抓出两个真缺陷（rail 导航项固定 60px 会把 "NAS" 标签挤出 rail；
顶栏状态牌越出 head）—— 都已按预算自适应修掉（见[硬件自适应记录](../../docs/ui-hardware-adaptive-audit-2026-10-08.md)）。

2026-10-08 的卡片复查通过 480×480、480×800、600×800、640×480、800×480、960×540、
1024×600、1280×800 八档，每档完整生成 75 帧。短屏保留控件尺寸并滚动访问正文；
网络指标按实际数字、单位和标题宽度均衡分行。这里验证的是各尺寸下创建及数据刷新，
不代表验证了运行中修改显示器分辨率。

### 硬件数量与名称自适应

从工程根目录运行：

```bash
python3 tools/preview/hardware_fixtures.py /tmp/fnos-hardware-fixtures
mkdir -p /tmp/fnos-hardware-dense
PREVIEW_SIZE=1024x600 PREVIEW_HARDWARE_FIXTURE=/tmp/fnos-hardware-fixtures/dense.json \
  tools/preview/build/preview /tmp/fnos-hardware-dense
python3 tools/preview/ppm2png.py /tmp/fnos-hardware-dense
```

四组匿名配置覆盖空清单、少量、混合和密集硬件。密集配置包含 24 卷、12 阵列、64 磁盘、
192 温度通道、96 容器和 12 网口，帧大小超过旧 24KB 缓冲。断言逐一检查完整名称与所有设备的
滚动可达性、每组温度展开、网口旧值及权限状态、随后清空时旧条目不会重新出现。
fixture 数量是测试输入，生产代码没有这些数量上限。

解析器 ownership、长路径、并发读者和预算拒绝验证：

```bash
tools/preview/build/snapshot_check
```

UI 内存失败验证可另建 sanitizer 目录：

```bash
cmake -S tools/preview -B tools/preview/build-sanitize -DFNOS_PREVIEW_SANITIZE=ON
cmake --build tools/preview/build-sanitize --target preview -j8
PREVIEW_ALLOC_FAIL_AT=0 PREVIEW_HARDWARE_FIXTURE=/tmp/fnos-hardware-fixtures/mixed.json \
  tools/preview/build-sanitize/preview /tmp/fnos-allocation-check
```

`PREVIEW_ALLOC_FAIL_AT` 仅用于 host，注入一次 UI 自管文本/清单 metadata 分配失败，再验证重试后
各清单恢复完整；也验证温度通道展开时的失败。它不注入 LVGL 框架内部绘制缓冲分配。
设备按可配置字节预算接收数据；超预算明确报错，保留最后有效快照，不静默截掉清单。

### 首次进入配对页

```bash
PREVIEW_SIZE=1024x600 PREVIEW_PAIR_FIRST_ENTRY=1 tools/preview/build/preview /tmp/out-pair-first
```

此模式先配置匿名 Wi-Fi 替身，在 UI 创建后直接进入 NAS 配对，不依赖前面的切页测试初始化网络。
输出 7 帧并检查输入、握手、指纹确认、完成和失败，以及反复进入配对、切换模块和按钮命中。
它与完整状态流程分别验证首次布局和经过其他页面后的布局。

### 动效连续性整改回归

`run.sh` 自动覆盖第三目标接续（含最新目标覆盖与拖到端点）、诊断页真实“精简”开关、
四图持续采样与恢复、温度页无趋势图、四类覆盖层入退场/反向/输入阻挡，以及首次无凭据开机配网。
`PREVIEW_CHART_MAP=1` 另覆盖量程立即扩大、候选稳定 2000ms 后连续收缩、浮点投影和游标附着。
网络备注在收缩开始时静态发布目标范围，不随每个动画帧翻滚数字。
`range` 模式还通过产品网络页的真实采样入口让峰值进窗、离窗，检查备注更新时间；
`capacity` 模式用于逐个主题复核容量阈值与健康异常组合，八套主题均由一键脚本执行。
`temperature` 模式验证温度大读数、设备／通道读数、状态点与温度条共用正常／关注／高温色，
覆盖阈值边界、零和负值、昼夜、旧值降灰和同屏三档；八套主题也由一键脚本执行。

```bash
PREVIEW_DATA_MOTION=overlays tools/preview/build/preview /tmp/fnos-overlays
PREVIEW_DATA_MOTION=boot-wifi tools/preview/build/preview /tmp/fnos-boot-wifi
PREVIEW_DATA_MOTION=reduced tools/preview/build/preview /tmp/fnos-reduced
PREVIEW_DATA_MOTION=range tools/preview/build/preview /tmp/fnos-range
PREVIEW_DATA_MOTION=temperature tools/preview/build/preview /tmp/fnos-temperature
```

覆盖层专用模式保留中间透明度，普通快照仍通过统一落定入口获得稳态。
输出只含匿名 fixture；主机通过不等同于设备触摸或光学流畅度通过。

### 三个只读探针（不改布局，只打印）

```bash
PREVIEW_ENTER_MID=3 tools/preview/build/preview /tmp/out   # 换页入场动画**中途**的帧（3=页号），默认不跑
UK_POOL_TRACE=1     tools/preview/build/preview /tmp/out   # 每张自适应池的解：[pool] n= W= H= cols= shown=
PREVIEW_PROBE=1     tools/preview/build/preview /tmp/out   # 对象树：coords/宽/MAIN padding/SCROLLBAR/滚动余量
```

`snapshot()` 默认会把在飞的页面动画和入场动画落到终值再截图，这样样张是**稳态帧**、
可逐字节比对；想看动画本身才用 `PREVIEW_ENTER_MID`。**不能靠推进虚拟时钟来等动画**——
时钟一推，挂在 500ms 上的 `ui_tick` 也会走一跳，数据年龄与轮询计数跟着漂，快照就不可复现了。
专用 `PREVIEW_MOTION` 模式会冻结 NAS 时间与采集序号，仅推进 LVGL 时间，因此可以确定性录制真实动效。

### 原生跟手滑动与逐帧播放器

以下命令从工程根目录执行；输入走预览的 LVGL pointer driver，与页面控制器共用原生逻辑。

```bash
mkdir -p tools/preview/out/device
PREVIEW_MOTION=0,1,0 PREVIEW_MOTION_SCROLL=1 \
  tools/preview/build/preview tools/preview/out/device/motion
python3 tools/preview/ppm2png.py tools/preview/out/device/motion
python3 tools/preview/motion_view.py tools/preview/out/device/motion

# 两个曾复现的导航边界缺陷：重复当前页与首次系统页入场
PREVIEW_MOTION=0,1,0 PREVIEW_MOTION_INPUT=api PREVIEW_MOTION_REPEAT_TARGET=1 \
  tools/preview/build/preview /tmp/fnos-motion-repeat
PREVIEW_MOTION=0,3,0 PREVIEW_MOTION_INPUT=api PREVIEW_MOTION_FIRST_ENTRY=1 \
  tools/preview/build/preview /tmp/fnos-motion-system

# 自动输入定时器的响应预算；不手工调用 lv_indev_read
mkdir -p /tmp/fnos-motion-response
PREVIEW_MOTION=0,1,0 PREVIEW_MOTION_SCROLL=1 PREVIEW_MOTION_RESPONSIVENESS=1 PREVIEW_PARTIAL=1 \
  tools/preview/build/preview /tmp/fnos-motion-response
```

`motion.csv` 记录时间、页号和阶段；`index.html` 离线逐帧播放并可调速。
帧步长用 `PREVIEW_MOTION_STEP_MS` 配置（默认 40ms，采样频率不代表硬件帧率）。
中间帧允许页面穿过视口边缘，页面内部越界、字形缺失仍然检查；终帧恢复完整越界与交互重叠审计。
还验证短滑取消、纵向滚动隔离、快速反向、终帧无残影和采集历史不被动画时间伪造。
可选同级 `static/` 的健康页 PNG 会显示在播放器下方。

响应探针另写 `response.csv`，覆盖微抖、小横移、慢短划、快速短划和快速重定向。
当前切页使用 LVGL 原生裁剪揭示，内容保持原位；CSV 的 `incoming_visible_width` 记录目标页可见宽度。
探针还检查双页坐标保持、小幅跟手只重绘窄条、裁剪内外的原生命中、反向取消和最终像素一致。
重绘面积断言需加 `PREVIEW_PARTIAL=1`；FULL 模式会按显示配置主动重绘整屏，不能用来验证局部优化。
默认验收 `PREVIEW_MOTION_RESPONSE_MS=32`、`PREVIEW_MOTION_COMPLETE_MS=180`，两个预算独立于生产动画令牌。
响应包含自动输入采样及跟手调度；慢短划从首次采样后的识别过程量起。
输入/动画期间仍检查终帧像素、内容不透明和历史不被伪造。两个预算都设为 0 时仅测量而不执行预算断言。
当前仅在 1024×600 开发板横屏范围验证手势。

稳定数据刷新回归已加入 `run.sh` 的默认验收，也可单独执行：

```bash
PREVIEW_REFRESH_WORK=1 PREVIEW_PARTIAL=1 tools/preview/build/preview /tmp/fnos-refresh-work
```

它检查六页连续刷新不重复改导航样式、存储清单不反复伸缩；同时以数量不变的长卷名和
长容器名验证换行、重排、缩回与完整字符串保留，并覆盖长文本分配失败提示和下一拍恢复。
局部绘制模式还统计实际 flush 像素，要求首页四次稳定刷新总量小于一整帧，防止重复设置样式造成整行重绘。
全帧模式每次必然刷全屏，不能用于这项比较。数字回归保留真实发布节拍，验证 100↔9 的遮罩、
最大运行时长、旧值标识和跨天时标签与读数同步发布；网络图检查零刻度与图表底边对齐。
同一棵 UI 树在 1024×600 横屏中切换“卷 / 全部”筛选并恢复，检查清单列宽随可用区域重新测量。
`CONFIG_FNOS_UI_TEXT_SCRATCH_BYTES` 控制单次格式化的栈缓冲，默认 256 字节、范围 64–1024；
超长内容继续使用精确分配，不能通过截断名称节省开销。调整后须重建设备配置、重跑栈检查。
页面过渡期间数字显示最新读数，数字滚动在过渡结束后恢复；隐藏数字的既有动画会在下一次
动画采样释放图层。这些检查与主机帧采样不代表实机帧率，实机 A/B 必须另行记录。

### 电流表式动效（1024×600 横屏）

当前数字呈现节拍及升级配置见[快速跳变修正](../../docs/ui-numeric-motion-2026-10-24.md)；阻尼库与游标见[库阻尼与附着游标](../../docs/ui-ammeter-cursors-2026-10-20.md)，旧版记录见[电流表式遥测动效](../../docs/ui-ammeter-motion-2026-10-14.md)。
`PREVIEW_NUMBERS=1` 验证第三方求解器的欠阻尼／临界／过阻尼分支、时间步拆分等价性、位置/速度重定向、单位切换立即发布、仪表与数字生命周期；
同时用 200 ms 间隔的 15 个匿名读数检查快速更新：输出实际换值次数和最短间隔，验证配置的共享呈现节拍、取最新目标及缺失值即时发布。`number-stream-*` 为实际控件渲染帧；主机时钟和换值次数不代表开发板呈现 FPS。
`PREVIEW_DATA_MOTION=1` 录制全部六页，含仪表尾段与数字/单位锚点检查。仍使用真实 UI 代码和匿名夹具。
数字、圆环和条形共用呈现门；`PREVIEW_NUMBERS` 同时检查闭门、首次有效值、缺失恢复和设备换名，完整流程另检查真实卷／温度旧值的即时发布。规则、实机结果和边界见[动画技能复核](../../docs/ui-small-screen-anim-2026-10-24.md)。
`PREVIEW_CHART_MAP=1`（已进入默认 `run.sh`）还验证曲线游标随最新样本、滚动与尺寸变化保持附着，隐藏、缺失和清空时消失。

```bash
PREVIEW_NUMBERS=1 tools/preview/build/preview /tmp/fnos-numbers
PREVIEW_DATA_MOTION=1 tools/preview/build/preview /tmp/fnos-data-motion
python3 tools/preview/ppm2png.py /tmp/fnos-data-motion
python3 tools/preview/motion_view.py /tmp/fnos-data-motion
```

播放器帧间隔为 25ms，代表主机采样取证，不代表 P4 实机帧率。只迭代当前横屏；实机视觉验收遵循当前 AGENTS.md 的设备授权和 `capture-board` 流程。

## 前置

1. 至少跑过一次 `./idf.sh build` —— 需要 `build/config/sdkconfig.h`（主机 LVGL 用设备端真实 `CONFIG_LV_*`）；
2. `managed_components/lvgl__lvgl` 在（`./idf.sh build` 会拉）；
3. macOS 上 `git/make/cc` 需要 `DEVELOPER_DIR=/Library/Developer/CommandLineTools`（`run.sh` 已默认导出）。

## 实测开销

构建时间和产物大小取决于主机与依赖缓存，以当前输出为准。完整状态预览包含六页、126 张状态图与 5 张刷新回归图；`16-usage-*` 覆盖低占用、提醒和接近上限的首页/存储页，检查共享阈值、固定历史身份色和过期降灰；`17-overview-*` 覆盖长运行时长及旧值。最新审查及绘制量对照见[2026-10-20 审查](../../docs/ui-review-2026-10-20.md)。历史图片、FPK 和设备证据的边界见[项目 README](../../README.md#最近迭代与版本边界)。

## 边界与坑

生成字体需在 `tools/fonts/` 放置 Inter Regular / Medium / SemiBold 与 Noto Sans SC Regular / Medium 源字体；
`tools/gen_fonts.sh` 会获取 Chakra Petch。已生成的 C 文件可直接构建，SIL OFL 许可随子集保留。

- **动态名称的字库覆盖可配置**：默认包含基本汉字、横排假名、拉丁扩展、希腊和西里尔字符。
  `bash tools/gen_fonts.sh` 还扫描固定界面文案；额外字符区间可由 `FNOS_FONT_CJK_RANGES` 指定。
  CJK 默认 2bpp、未压缩；保持两路设备绘制时避免共享 RLE 解码器的并发问题。
- **快照必须初始化并按 API 管理**：`fnos_status_t state={0}`，用 `fnos_status_create/text/parse/copy/release`。
  跨刷新保留清单时用 `fnos_status_copy`，不能只复制含借用指针的结构体。
- **主机三处 Kconfig 必须偏离设备**（见 `run.sh` 内注释）：`LV_USE_OS 0`（`LV_OS_NONE`）、libc malloc、
  `LV_DRAW_SW_DRAW_UNIT_CNT 1`。偏离点仅限这三处语义等价替身。
- **断言在主机上改成 stderr + `abort()`**：设备端 LVGL 默认 `LV_ASSERT_HANDLER` 是 `while(1);`，
  断言失败的表现是"无声 100% CPU 死循环"（曾经就是它让 `tools/preview/build/preview` 看起来像卡死）。看到 `*** LVGL ASSERT ... ***` 就是真 bug。
- **位移动画需要区分视口裁切与内部越界**：普通快照先落到稳态；专用动效模式从带页面标记的
  页根审计内部盒子，保留视口的预期裁切。不能把所有中间帧的边界审计都关闭。
- **不能替代实机**：触摸/手势、刷新率、PSRAM/DMA 采样路径、真实 Wi-Fi 时序仍要烧录验证；
  逐页实机取证用 `python3 tools/page_shot.py --port "$FNOS_SERIAL_PORT" --page N`（串口切页 + 手机拍照）。

## 看门狗：滚动条贴边

列表卡片右缘原本同时住着"右对齐数值/状态"和 LVGL 纵向滚动条，两者只差 1px，
看起来就是"文字压在竖条上"。修法是 `ui_kit/uk_theme.h` 的 `UK_SCROLL_W / UK_SCROLL_INSET /
UK_SCROLL_GAP` + 收窄内容右界。

```bash
node tools/preview/scroll_gap.js tools/preview/out   # 退出码非 0 = 有贴边
```

它解码预览 PNG，找出每张列表卡片的滚动条列（中性灰、纵向连续 ≥24px），量两个间隙：
**内容右缘→条**（判据 ≥8px）与 **条→卡片内缘**（判据 ≥4px）。滚动条自身及其 1px
抗锯齿边会先整列剔除，避免把条子边缘误判成内容。

## 在验收流程里的位置

`bash tools/gen_fonts.sh`（改过文案时）→ **`bash tools/preview/run.sh` 看图** → `./idf.sh build` →
烧录 + 实机取证（`python3 tools/page_shot.py --port "$FNOS_SERIAL_PORT" --page N` + `read_image` 亲眼看图，见 `docs/verification.md`）。
预览管"好不好看、对不对齐、有没有方块"，实机管"真的能跑、真实数据下版面自适应"。

独立固件构建目录可显式选择对应的公开 UI 配置：

```bash
FNOS_UI_DEVICE_CONFIG="$PWD/path-to-build/config/sdkconfig.h" bash tools/preview/run.sh out/candidate
```

该参数同时用于生成主机 LVGL 配置和 CMake 的 UI 参数，避免意外读取旧 `build/` 配置。
