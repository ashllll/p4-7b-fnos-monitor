# 数字读数过渡

目标工程是本目录的 C/LVGL 固件；`esp32-p4-wifi6-touch-lcd-7b` 是资料仓。此次只修改开发工程，没有同步发布目录、提交、推送或烧录。

## 表现与数据约束

- 使用 Apple Core Animation `easeOut` 对应的 `cubic-bezier(0,0,0.58,1)`；计算时先求解曲线的 x，再取 y。
- 只为变化的字符添加纵向位移、透明度及轻微柔化。位移按字体行高计算，柔化为五点加权字形采样，不是高斯模糊。
- 不变的字符保留；进位增加字符时从原画面位置过渡。更新中再次收到读数时从当前可见字形继续，不排队播放历史读数。
- label 的真实文本始终是最新读数，没有数值插值，也不制造采样或历史数据。
- 断线、缺失值、单位/状态/身份变化立即显示。多行、溢出、字体切换、隐藏对象及字符数量减少且内容尺寸遮罩无法保留原字形时使用原生静态文本；避免裁字或覆盖相邻单位。
- 继承原生 label 的布局、背景、字体及颜色；动画只替换文字绘制。每个 label 只有一个动画回调；结束、取消、删除都会释放临时字形。

## 覆盖

总览：CPU、内存、负载、运行时钟、网络速率、容量和温度。

存储：容量汇总与卷/阵列/磁盘行读数；网络：速率、峰值、量程、接口上下行；系统：资源 KPI、副读数、负载和交换数据；温度：主读数、设备峰值、通道与汇总；诊断：耗时、计数、快照年龄、内存和 ZFS 数值。

告警文字、严重程度、连接原因、配对输入、设备名称和地址保持直接显示。状态变化不等待动画。

## 参数

参数集中在组件 Kconfig，主机预览从设备的公开 UI 配置导入。无需在业务页写像素常数。

| 配置 | 默认值 |
| --- | --- |
| `FNOS_UI_NUMBER_MS` | 250 ms；0 关闭 |
| `FNOS_UI_NUMBER_STAGGER_MS` | 50 ms |
| `FNOS_UI_NUMBER_MAX_STAGGER_MS` | 100 ms |
| `FNOS_UI_NUMBER_TRAVEL_PCT` | 字体行高的 25% |
| `FNOS_UI_NUMBER_BLUR_PCT` | 字体行高的 6%；0 关闭 |
| `FNOS_UI_NUMBER_BUDGET_BYTES` | 临时字形总预算 128 KiB |
| `FNOS_UI_REDUCED_MOTION` | 启用后数值立即更新 |

预算不足或分配失败时回退最新静态读数。预算只约束可选动画，不限制传感器、磁盘、接口或其他数据条目数量。

## 验证入口

先生成设备配置，再使用 `tools/preview/run.sh` 或现有主机 CMake 构建。以下命令不连接 NAS 或开发板，使用匿名桩数据。

```sh
PREVIEW_NUMBERS=1 tools/preview/build/preview /tmp/fnos-number-motion
PREVIEW_NUMBERS=1 PREVIEW_PARTIAL=1 tools/preview/build/preview /tmp/fnos-number-partial
PREVIEW_DATA_MOTION=1 tools/preview/build/preview /tmp/fnos-data-motion
tools/preview/build/preview /tmp/fnos-number-static
PREVIEW_SIZE=1280x800 tools/preview/build/preview /tmp/fnos-number-1280
PREVIEW_SIZE=800x480 PREVIEW_DATA_MOTION=1 tools/preview/build/preview /tmp/fnos-data-number-800
python3 tools/preview/ppm2png.py /tmp/fnos-data-motion
python3 tools/preview/motion_view.py /tmp/fnos-data-motion
```

数值夹具包含小数、进位、运行时间、单位切换、更新中改目标、重复目标、缺失、内存分配失败和对象删除。测试通过原生 LVGL 时钟取 22 帧；六页数据过渡取 102 帧，并运行现有边界、字形与对比度审计。`99 → 100` 首帧应与旧画面逐像素一致，最终帧应与原生静态目标一致；全帧与部分缓冲渲染应逐帧一致。

Sanitizer 构建使用 `-DFNOS_PREVIEW_SANITIZE=ON`，分别运行数字夹具与六页动画。此预览保留全局页面对象，使用 `ASAN_OPTIONS=detect_leaks=0`；动画自身的取消/析构释放由自检直接检查，不据此声称整个应用没有泄漏。

设备端经 ESP-IDF 5.5.3 构建，`tools/stack_check.py` 增补三个数字绘制/动画/析构回调。栈检查是静态估算，不能代替设备运行观测。

## 审查与限制

本次修改的 8 个代码/配置文件按 OCR 解析规则由 Codex 审查；另人工审查两个构建定义和本说明，合计 11 项。工作区原有 6 项文档修改不属于本次变更。DSH Desktop 已安装，但机器锁屏阻止专用会话启动，因此没有派发 DSH 工作，也没有独立 DSH 审查结论。

1024×600 与 1280×800 完整状态集各 84 帧。800×480 的六页数字过渡可验证，但完整状态集仍在连接失败原因可见性检查处失败，不能报告小尺寸全量验收通过。此次通过 HEAD 独立副本确认了小尺寸启动卡片溢出，并复用内容测量逻辑修复；长连接错误说明的可见性问题仍待单独处理。

离线播放器是实际 LVGL 帧序列，可调播放速度及逐帧查看。

## 2026-10-09 烧录实机复核

用户授权烧录验证后，以 `flash-idf` 写入 ESP32-P4 v3.2，串口波特率 230400，烧录返回成功。写入前备份覆盖区域的 7,102,464 字节并校验 SHA-256；构建文件在烧录前后与备份清单记录的哈希一致。备份只保存在本机 `Documents/ChatGPT/board-backups/number-motion-20261009-112940/`，含私有设备数据，不进入版本库。

以 `serial-monitor` 的日志读取与分析函数观察 220.2 秒，115200 波特率、单一串口持有者。六页切换均得到设备确认，逐页通过 `capture-board` 获取并实际检查照片；总览在启动后约 48 秒和 190 秒分别复查，时钟、采集计数与网络读数继续变化，未停在旧画面。

最后一条周期采集日志为 `ok=180 fail=0`；串口关闭后另拍总览，屏幕显示 `ok=285 fail=0` 并持续在线。内部空闲堆约 214 KiB、DMA 堆约 176 KiB。日志未见崩溃、断言、看门狗或分配失败。技能的宽泛错误关键词把六条正常 `poll ... fail=0` 信息误报为错误，已逐条核对排除，原始报告保留。

六页照片和 8 秒总览相机预览录像保存在本机 `Documents/ChatGPT/board-camera/number-motion-verify-20261009-113931/`，未复制到版本库。录像抽取逐帧检查，确认读数持续变化；相机反光、摩尔纹和采样限制使其不能充当精确帧率或面板撕裂测量。此次通过串口切页，未验证手指触摸、每个滚动列表的全部末项或全部故障态。

交付配置仍是关闭自动轮播、保留数字动画；没有写入临时轮播固件，也未同步发布目录或推送。
