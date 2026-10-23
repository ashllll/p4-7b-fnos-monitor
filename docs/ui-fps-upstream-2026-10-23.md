# 2026-10-23 官方显示栈调查与设备对照

> 本文保留开发、试验及回退阶段的时间顺序；文中“未提交／未操作发布目录”描述当时状态。随后按用户授权将通过验证的修复与这三份记录一并归档到 `main`，发布验证入口见 [本轮验证汇总](ui-validation-2026-10-23.md)。

用户要求以成熟方案继续优化实际交互帧率。本轮复用 Espressif adapter 的显示模式、回调和 PPA/DMA2D 路径，不增加自写渲染器。基线是本轮已修复居中和刷新重排的开发版本；首页持续刷新平均约 54 FPS，拖动的帧间隔 P50 为 39–49 ms，尚未达到高帧率目标。60 Hz 面板刷新、趋势的 60 FPS 上限和实际绘制帧率是不同指标；60 FPS 的帧预算约为 16.67 ms。

## 官方依据与候选顺序

1. [adapter 0.7.3 上游变更记录](https://github.com/espressif/esp-iot-solution/blob/1d9a68a6989f85e278958b48fb6c50f350c72f65/components/display/tools/esp_lvgl_adapter/CHANGELOG.md)修正防撕裂提交竞态、序列回绕及提前完成通知；不是性能倍增承诺。其[组件声明](https://github.com/espressif/esp-iot-solution/blob/1d9a68a6989f85e278958b48fb6c50f350c72f65/components/display/tools/esp_lvgl_adapter/idf_component.yml)要求 IDF >=5.5.5，故组合验证官方 IDF 5.5.5 与 adapter 0.7.3，LVGL 保持 9.5.0。
2. 对照 [IDF 5.5.3](https://github.com/espressif/esp-idf/blob/v5.5.3/components/esp_lcd/dsi/include/esp_lcd_mipi_dsi.h) 与 [5.5.5 MIPI DSI 头文件](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_lcd/dsi/include/esp_lcd_mipi_dsi.h)，后者提供 `on_frame_buf_complete`。旧工具链没有该符号，adapter 使用兼容回调并给出警告。旧回调路径是此前 DIRECT 首帧卡住的候选原因；本轮只验证 SDK 与 adapter 组合使启动成功，未把原因独立归到某一个修复。
3. [官方显示模式文档](https://components.espressif.com/components/espressif/esp_lvgl_adapter/versions/0.7.3/readme)支持 DIRECT 用于局部更新，FULL 用于大面积更新；180° 旋转需要三个面板缓冲。先只升级栈、保留 TRIPLE_PARTIAL，再分别比较 DOUBLE_DIRECT 与 TRIPLE_FULL，保持数据、曲线质量、两绘制线程和手势脚本一致。
4. [LVGL PPA 文档](https://lvgl.io/docs/open/9.4/details/integration/chip_vendors/espressif/hardware_accelerator_ppa)仍把原生 PPA 绘制单元标为实验性，并不保证整页收益；当前负载的 fill-only 路径已在前轮否决。adapter 自带的 custom blend 路径要求一个软件绘制线程，不能与双线程配置直接混用。[IDF PPA 性能说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32p4/api-reference/peripherals/ppa.html#performance-overview)说明 PSRAM 并发读写会影响加速效果。
5. [LVGL DMA2D 说明](https://lvgl.io/docs/open/9.4/details/integration/chip_vendors/espressif/hardware_accelerator_dma2d.html)中的 ESP32 路径属于显示传输。当前 EK79007 默认配置已经启用 `use_dma2d`，adapter 也已经注册旋转 PPA；不能将这两项描述为本轮新增加速。编译优化、400 MHz CPU、200 MHz PSRAM、256 KiB L2/128 B cache line 和双核软件绘制也已启用。

## 隔离与验证

新 IDF 独立安装，构建目录和 sdkconfig 在本地排除目录，原正常 ELF/BIN 和主 sdkconfig 保留用于回退。仅在开发目录修改依赖声明；不操作发布目录、不提交推送。升级构建确认启用了新 framebuffer-complete 分支，静态任务栈检查通过；这些不等于实屏或高 FPS 验收。

为 DSH 准备了脱敏源码副本和只读调查合同；headless 返回缺少 API 凭据，已有账号通道返回 ACCOUNT_SIGN_IN_REQUIRED，两次启动均未产生工作报告。输入 SHA256 校验无修改。随技能安装的 `verify_workspace.sh` 实际缺失，本轮使用独立哈希核对；不把 DSH 失败当作已完成独立评审。

## 新栈 + 原 PARTIAL 模式首轮

保留 TRIPLE_PARTIAL 的一次同场景短测：首页/网络稳态帧间隔 P50/P95 为 19.660/28.360、21.851/50.762 ms；首页/网络/系统拖动为 45.842/53.933、46.125/53.936、38.047/44.374 ms。六次脚本拖动无漏计，未见崩溃，观察期采集累计 390 成功/2 失败。两张新鲜首页/网络页照片已查看，可见区域的圆环和单位对齐正常；取景倾斜、右侧仍有裁切。

这是一次测试，不与旧栈三次分位数中位数混为同精度结论。单独升级没有表现出明确帧率收益，继续测试官方 DIRECT/FULL 路径。原始串口、个人固件及照片只留本地。


## 新栈 DIRECT 对照（淘汰）

新 IDF 与 adapter 组合下，DOUBLE_DIRECT 不再卡首帧，UI 创建成功。它却更慢：首页/网络稳态 P50 约 33.1 ms；首页/网络/系统拖动 P50/P95 分别为 82.842/99.364、82.574/83.239、66.457/83.060 ms。六次脚本拖动无漏计，两页新鲜照片已查看，朝向及可见对齐正常。该路径不保留，不能用“能够启动”替代性能验收。

TRIPLE_FULL 候选已编译，未烧录、未测量。根据 DIRECT 对照，优先转向官方热代码驻 IRAM 的 CPU 优化，仍使用原生 PARTIAL。新构建的 `.iram0.text` 为 207134 B，未启用该选项的新栈候选为 94440 B，增加约 110 KiB；64 行内部绘制缓冲占 128 KiB，替代原 120 行的 240 KiB。测试必须确认没有分配失败或丢掉防撕裂的回退。该组合收益不能单独归因于 IRAM。

热代码配置依据：[LVGL 官方 Espressif 性能配置](https://lvgl.io/docs/open/9.4/details/integration/chip_vendors/espressif/tips_and_tricks.html#improving-lvgl-speed-of-execution)，使用现成 `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y`；不手写 SIMD、链接器分区或绘制内核。


## IRAM 组合对照（淘汰）

候选确认热函数进入 IRAM、三缓冲 PARTIAL 初始化成功，无分配失败/防撕裂回退。启动内部空闲堆约 222 KiB，显示初始化后约 69 KiB，与原配置的显示后约 67 KiB 相近。

首页/网络稳态帧间隔 P50/P95 为 20.386/29.212、25.309/54.496 ms；首页/网络/系统拖动为 57.032/70.335、54.325/63.388、47.525/54.691 ms。六次手势无漏计，未见崩溃，两页新鲜照片已实际查看。性能不如原基线，故不保留。结果仅适用于当前界面、板卡和该配置组合，不推断 IRAM 在其他负载没有用途。

## 官方 Snapshot + Image 集成试验（主机门禁失败，未烧录）

依据 [LVGL Snapshot 文档](https://lvgl.io/docs/open/9.4/details/auxiliary-modules/snapshot.html)，用现成 `lv_snapshot_take` 与 `lv_image_create` 组合，未新增自写绘制单元。第一次隐藏页面根节点导致原生页面身份审计失败；保留页面根节点与手势控制器、只临时隐藏已捕获的子项后，390 帧跟手/取消/反向/响应和 379 帧网络往返通过。

完整状态回归与首次入页仍失败：原来通过真实可见控件验证的内容在缓存显示期间被隐藏，状态检查找不到卷读数，首次入页统计从 2 个可见节点变为落位后 182 个。缓存图像能否完整替代其可见内容语义尚未被验证。本轮不改弱检查来通过门禁，候选未上板，已逐字恢复 UI 源码。这是当前集成不完整，不能据此断言 LVGL Snapshot 本身有问题。

## 最终保留与后续准入

本轮依赖、BSP、入口源码与 sdkconfig 试验均恢复；LVGL 9.5.0 / adapter 0.6.4 / IDF 5.5.3、双软件线程、120 行 PARTIAL 保持原基线。前一轮的圆环居中、单位基线、网络重排缓存、平滑曲线和原生折线范围修正继续保留。发布目录未操作，没有提交或推送。

恢复后的主机 131 张完整状态与硬件组合/分配失败恢复门禁通过；163 帧首次系统入页恢复通过；静态任务栈检查通过。正常应用 SHA256 与本轮开始前正常固件一致。本次三组官方路径的设备结果不支持“仅改配置就稳定 60 FPS”。设备渲染完成计数、面板扫描 60 Hz 和实际可见新画面是不同指标；单张照片只能检查静态外观，不能测量真实呈现 FPS。

后续方案仍优先复用原生 Snapshot/Image 或上游提供的图层缓存能力，目标是在运动前准备可复用像素，运动期只合成图像。当前有两个明确待解决项：不能在首触回调里同步生成整页快照而造成百毫秒阻塞；缓存呈现必须保留首次入页、反向、动态清单、滚动、分配失败和数据可信度合同。以真实设备帧间隔和首次响应同时验收，不只看动画中段平均值。这套缓存集成尚未完成，当前不声称已达到稳定 60 FPS。

## 恢复烧录与最终短测

正常固件的 bootloader、应用和分区表均烧录并校验通过。五个相同用例的一次恢复短测：首页/网络稳态窗口平均为 52.1/38.3 FPS，帧间隔 P50/P95 为 18.440/26.948、21.969/50.141 ms；首页/网络/系统拖动为 47.159/59.130、42.317/54.012、38.256/48.188 ms（P50 倒数分别约 21.2、23.6、26.1 FPS；窗口平均另为 24.1、25.1、35.6 FPS）。不能将不同统计口径混用。

六次拖动无漏计，无崩溃、缓冲回退或找不到 Wi-Fi 标记；观察期采集累计 390 成功/2 失败。新鲜网络页和首页照片已查看，圆环 10/43 数字与可见单位位置正常，设备留在首页。取景仍有倾斜和右侧裁切；这是短时恢复确认，不是长测或真实呈现 60 FPS 验收。

当前高 FPS 目标仍未达到。本轮没有新增可保留的帧率提升；保留前轮已通过的 UI 修复，并保存官方方案对照和未完成缓存合同。
