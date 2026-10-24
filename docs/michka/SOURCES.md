# 来源、当前基线与不确定性

初稿：2026-10-22；发布复核：2026-10-25。区分当前源码、历史验证记录、外部事实和待验证方案。

## 1. 发布复核基线（2026-10-25）

| 项目 | 核对结果 |
| --- | --- |
| 发布目录 | `<workspace>/p4-7b-fnos-monitor-release`，独立发布副本 |
| 复核前发布 HEAD | `fd7573ab8d836976260dd6faa290a306e9afaf76`，main；本次仅整理文档 |
| 开发目录 | `<workspace>/p4-7b-fnos-monitor`；私有配置、个人固件及未发布试验留在开发环境 |
| remote | https://github.com/ashllll/p4-7b-fnos-monitor.git |
| 板卡目标 | Waveshare ESP32-P4-WIFI6-Touch-LCD-7B，1024×600横屏 |
| 当前发布栈 | ESP-IDF 5.5.5、LVGL 9.6.0、esp_lvgl_adapter 0.7.3、原生 UI、ESP-Hosted |
| NAS应用版本 | nasscreencompanion 1.2.8；部署证据见[控制器温度记录](../nas-hba-temperature-2026-10-24.md) |

初稿调查的发布基线为 `e0805d98366daa99a7705b72844e1d056c4e255c`，不能当作本次发布版本。当前依赖以 [dependencies.lock](../../dependencies.lock) 为准，显示栈验证见[原生裁剪揭示记录](../ui-drag-lvgl96-2026-10-24.md)。

源码读取确认：已有六页、来源状态、动态实体、历史与动效；当前首页布局不是历史 ADR 所记等高九宫格。[2026-10-21 匿名渲染](../images/ui-current-2026-10-21/README.md)仅代表当时样例；温度页当前已移除趋势卡，见[温度页记录](../ui-temperature-compact-2026-10-24.md)。

[ADR-0001](../adr/0001-native-instrument-ui.md)保留历史理由；[ADR-0003](../adr/0003-module-freshness-and-reading-presence.md)的数据语义继续适用。迁移未完成前，不把旧固件验证结论转移给新方案。

## 2. 上游资料

上游固定提交：`3213285d6c22e786c0d4349ee3c0813cc98ab16d`。

该提交是规划调查来源，未因本次文档发布而更新；后续 PoC 必须记录其实际使用的上游版本。发布复核已重新读取该提交的 README、部署说明、指标模型、API 和 LICENSE；本地许可副本与上游逐字节一致。

| 依据 | 支持的事实 |
| --- | --- |
| [README](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/README.md) | .NET 10、浏览器、SSE、SQLite、Hub／Agent |
| [部署说明](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/deploy/README.md) | 自包含服务、Linux部署、kiosk、模板与widget；并非已验证兼容fnOS |
| [MetricSnapshot](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/src/Monitor/Metrics/MetricSnapshot.cs) | 基础指标、容器扩展、模型字段及缺失语义局限 |
| [LinuxMetricsCollector](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/src/Monitor/Metrics/LinuxMetricsCollector.cs) | Linux读取来源和thermal-zone温度路径 |
| [Endpoints](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/src/Monitor/Api/Endpoints.cs) | ingest、时间在线判断、服务／系统相关接口 |
| [app.js](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/src/Monitor/wwwroot/js/app.js) | 标准卡片单行重排、模板加载与原版交互 |
| [widget合同](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/src/Monitor/widgets/README.txt) | render／mount／unmount及同源数据入口 |
| [LICENSE](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/LICENSE) | CC BY-NC 4.0、作者、版权、第三方声明 |

## 3. 平台与协议依据

- [Waveshare官方板卡资料](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B)：处理器、内存、面板、触摸与接口；不意味着可运行桌面浏览器。
- [ESP-IDF 5.5.5 JPEG](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/jpeg.html)：与当前 IDF 版本对应的硬件解码能力、输出格式和单模块测试；不证明端到端帧率。
- [CDP Page](https://chromedevtools.github.io/devtools-protocol/tot/Page/)及[Input](https://chromedevtools.github.io/devtools-protocol/tot/Input/)：抓帧及输入注入候选；固定浏览器后必须重验，接口文档不是PoC结果。
- [CC BY-NC 4.0完整条款](https://creativecommons.org/licenses/by-nc/4.0/legalcode.en)：非商业、署名、修改标注与其他条件；本项目继承政策见LICENSING。

## 4. 待验证事项

| 未知项 | 验证工作 |
| --- | --- |
| PC／NAS依赖与资源 | WP-02、WP-09 |
| 浏览器供帧与JPEG质量 | WP-03、WP-05 |
| P4解码兼容、旋转、DMA／缓冲 | WP-04、WP-05 |
| 无线带宽和触摸反馈 | WP-05 |
| 旧值／缺失映射与历史 | WP-06、WP-07 |
| 模板扩展是否需核心补丁 | WP-08 |
| 实际商业用途或新增第三方许可 | 发生对应使用／引入前核对，AT-16记录 |

所有性能数字是候选门槛或预算算例。开发侧已有未发布的分段 PoC 记录，本次未复核其原始运行证据，也未将其复制到本发布集；不据此认定任何 AT 项通过。当前发布基线不含可用远程客户端，不能将原生固件验收移作远程链路验收。
