# 全 UI 仪表惯性动效：可复用弹簧库研究

日期：2026-10-20。范围：外部一手源码与许可调查；本文提出选型和验收要求，不代表实现、构建或实机验收已通过。

选型已落地为 C99 适配，实际文件、参数与已完成的检查见 [实现与验证记录](ui-ammeter-cursors-2026-10-20.md)；下文保留研究阶段的候选比较与验收建议。

## 选型结论

建议复用 Ryan Juckett 的 **Damped Springs** 官方参考实现，以最小 C99 语法适配接入当前 C/LVGL 工程。它直接维护位置与速度，覆盖欠阻尼、临界和过阻尼；计算系数与更新状态分离，适合同帧大量数字、环表、线条进度共用参数。重新设置目标时保留位置和速度，即可形成旧式指针表的惯性跟随。上游代码、许可及完整推导均在[作者原文](https://www.ryanjuckett.com/damped-springs/)。

这是选型判断，并非声称存在一个可直接安装的官方 ESP-IDF/LVGL 插件。需要的自有代码仅是生命周期、时间单位、显示范围与控件绑定；不要再实现自己的微分方程、衰减查表或另一套弹簧公式。

## 一手候选比较

| 候选 | 已核实能力与边界 | 本项目判断 |
| --- | --- | --- |
| Ryan Juckett Damped Springs | C++ 两函数、4 个 float 系数；`CalcDampedSpringMotionParams` 接受步长、角频率、阻尼比；`UpdateDampedSpringMotion` 更新位置和速度。无框架/堆分配依赖，标准数学函数即可。[原文与源码](https://www.ryanjuckett.com/damped-springs/) | 优先；C99 适配量最小，可共享系数。 |
| Charmbracelet Harmonica v0.2.0 | 官方明确是 Juckett C++ 实现的 Go 移植；`NewSpring` 与 `Update` 分离，MIT 许可。[项目](https://github.com/charmbracelet/harmonica/tree/v0.2.0)、[求值实现](https://github.com/charmbracelet/harmonica/blob/v0.2.0/spring.go) | 证明该算法有实际库复用；不为嵌入式引入 Go，也不绕道翻译回 C。 |
| Daniel Holden Spring-It-On | MIT；`common.h` 包含完整阻尼弹簧和半衰期接口，但同时含 raylib/raygui 依赖；`fast_negexp`、`fast_atan` 是近似计算。[固定源码](https://github.com/orangeduck/Spring-It-On/blob/4d97b497a78e40c7b47d49e8a9da27e4aa5616d6/common.h)、[许可](https://github.com/orangeduck/Spring-It-On/blob/4d97b497a78e40c7b47d49e8a9da27e4aa5616d6/LICENSE) | 可用备用；拆分依赖和验证近似误差的成本高于 Juckett。函数名含 exact 不意味着其快速数学近似没有误差。 |
| Flutter third_party/spring_animation | MIT，来自 React Native；实现为 Objective-C/Foundation，仅暴露按时间求位置；源码把所有 `zeta >= 1` 走临界分支。[来源说明](https://github.com/flutter/flutter/blob/master/engine/src/flutter/third_party/spring_animation/README.md)、[源码](https://github.com/flutter/flutter/blob/master/engine/src/flutter/third_party/spring_animation/spring_animation.mm)、[许可](https://github.com/flutter/flutter/blob/master/engine/src/flutter/third_party/spring_animation/LICENSE) | 当前文件不适合直接 vendor 给 ESP32；速度连续重定向还需额外接口。 |
| LVGL 9.5 内置 lv_anim | 提供线性、ease、overshoot、bounce 等定时路径；由起止值、持续时间和 path 回调求值。[v9.5.0 头文件](https://github.com/lvgl/lvgl/blob/v9.5.0/src/misc/lv_anim.h)、[实现](https://github.com/lvgl/lvgl/blob/v9.5.0/src/misc/lv_anim.c) | 保留其调度与属性写入能力；默认路径不是带持续速度状态的二阶物理弹簧。换成 bounce 不能自动解决遥测频繁改目标的速度连续性。 |

## 可追溯 vendor 清单

Juckett 原文发表于 2012-07-20，页尾标记更新于 2020-11-29；页面没有 Git tag 或正式发布包。因此版本标识应写“官方网页源码快照 2026-10-20”，不要编造语义版本。

本轮从原站 HTML 的两个 `<pre>` 块提取完整许可与源代码，保存临时交接文件 `/tmp/fnos-juckett-source.cpp`。按两个解码后的代码块以两个换行连接、末尾加一个换行：5812 字节，SHA-256：

```text
6f1f3c6ebdd7a8d804f9273be8d2e4efb335410b6e7ac74467afa0b0022df392
```

建议仓库只 vendor 这个算法块及完整原始许可，配 `UPSTREAM.md` 记录来源、快照日期、上游提取哈希与本地适配项。可以分成 `.c` 和 `.h`，将 C++ 引用改成指针、补 C typedef/header guard/math include、加符号前缀；保留算法表达式和分支。适配后文件应明确标记修改，不能沿用原始快照哈希当成改后文件哈希。

**许可是 zlib-style，不是 MIT。** 原文明确允许商业使用和修改再分发，要求不歪曲来源、明确标记修改版本、保留完整声明。不要用 Harmonica 的 MIT 许可替换 Juckett 原代码许可。[许可原文](https://www.ryanjuckett.com/damped-springs/)

Harmonica v0.2.0 解析到提交 `5ae57d3f7a21232590cc173f92e2f27eeab6ec43`；Spring-It-On 本轮 main 解析到 `4d97b497a78e40c7b47d49e8a9da27e4aa5616d6`。两者仅作候选比较，本轮未引入项目依赖。

## 接入建议与边界

下列为本项目工程建议，不是上游库保证：

- 时间统一为秒，角频率使用 rad/s，速度使用 value/s。原作 `epsilon = 0.0001f` 还用于零角频率判断；若把角频率换成 rad/ms 却沿用 epsilon，会把低频范围错误当成静止。UI 的毫秒时钟在适配层转换一次。
- 每个正在运动的量保留 `position / velocity / target`。新样本只改 target，不能将 velocity 清零、从上次原始数据重启动画，或每 500 ms 从头播放同一条曲线。相同目标重复提交应为无操作。
- 使用真实 elapsed dt；相同 dt、角频率、阻尼比的量共享系数，避免每个控件重复 exp/sin/cos。后台页面停止刷新后再次进入的时间策略需明确，不能把暂停误当每帧固定 16 ms。
- 接近临界阻尼适合作为稳定仪表默认观感；轻微欠阻尼可以提供小幅回摆，但须通过实屏比较选定参数。不要把所有内容都配置成可见反弹。阻尼/响应速度归入统一 motion 配置，按数值、表针、页面位移等角色读取。
- 环表指针角、弧形填充、条形填充、数字和趋势新端点可用同一求值器。真值、告警状态、时间戳和无效数据判定保持来自数据层；不能让运动中的显示值推迟告警。历史采样点不能因为动效而被改写成虚构测量。
- 运动状态与绘制裁剪分离：绘制必须遵守 0–100%/合法尺寸，不能让回摆把线条画出父对象。若边界处理改动 velocity 或 position，必须把它作为明确且可测试的交互规则，避免无意破坏连续性。
- 缺失/断连/首次样本、列表重排及页面销毁单独处理。动态设备以稳定实体标识绑定动效状态，不能按数组下标让一块磁盘继承另一块磁盘的指针惯性。

## 技术验收建议

1. **上游等价性**：C99 适配结果对照原 C++，覆盖三类阻尼、零频、零 dt、不同初始速度、反向运动及大 dt；明确 float 容差。
2. **可变帧长**：固定目标下，对比同总时长的 30/60 Hz、非均匀 dt、单步采样；检查位置和速度误差。适配层若截断长 dt，要明确这不再等价于真实时间。
3. **连续重定向**：运动中更换目标、反向目标和重复目标；目标修改瞬间位置与速度不变，随后连续趋向新目标。
4. **显示收敛**：按显示分辨率定义位置和速度双阈值；阈值满足才结束，否则最终像素可能在一两个值之间抖动。不可仅按位置阈值提前清速度。
5. **全 UI 覆盖**：逐页列出环表/针、直线表、数字、实时趋势端点、详情、诊断与页面位移的接入表；检查未接入项的合理性，不能凭首页演示声明全覆盖。
6. **资源与生命周期**：动态清单增减、长名称重排、删除动画对象、快速换页、低内存、后台暂停恢复；确认无悬空指针、重复计时器、每帧堆分配或不必要的离屏刷新。
7. **本项目验收**：运行实际预览、PREVIEW_MOTION、首次入页/重复目标回归、布局/字形/格式残留审计、固件构建和栈检查；主机结果与物理屏验收分列。获得对应设备操作授权后，按项目规则采集实屏图像和运行证据，不能以单张静态照片证明动效时序。
