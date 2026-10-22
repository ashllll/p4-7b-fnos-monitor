# 当前界面样张 · 2026-10-21

使用发布目录的原生 LVGL 9.5 渲染，尺寸为 1024×600，主题为 `cuktech-console`。覆盖六页、配网与配对、详情、诊断、夜间和三档占用率，共 21 张图片。

图片来自源码提交 [`885264e`](https://github.com/ashllll/p4-7b-fnos-monitor/commit/885264e5a521620a5e99d9db5b8fac3ce0500985)，其固件源码与已烧录的 `d453cac` 一致。首页布局、圆环／条形／曲线附着游标、告警事件流与数值刷新均由当前 UI 代码绘制。

数据使用匿名主机夹具，板级接口由测试替身提供；没有连接个人 NAS 或读取个人配置。图片是动画落定后的稳态帧，不能用于判断实机帧率、动画手感或触摸效果。

## 复现与校验

先按[项目说明](../../../README.md#快速开始)准备 ESP-IDF 并完成一次构建，然后从仓库根目录执行：

```bash
env -u KK_PALETTE bash tools/preview/run.sh out-readme-current-20261021
```

[manifest.json](manifest.json)记录源码与夹具哈希、实际 UI 配置、原始输出文件和 PNG SHA-256。复现时使用相同提交与配置；截图仅采用主机替身，真实配置留在本地。完整预览通过 126 张状态图与 11 张刷新回归图，以及字形、格式残留、溢出、对象边界、触控重叠、对比度、硬件清单与分配失败恢复检查。

[返回项目首页](../../../README.md) · [主机预览说明](../../../tools/preview/README.md) · [2026-10-14 历史样张](../ui-cuktech-ultra-2026-10-14/README.md)

## 总览

![总览](overview.png)

## 存储

![存储](storage.png)

## 网络

![网络](network.png)

## 系统

![系统](system.png)

## 温度

![温度](temperature.png)

## 告警

![告警](alerts.png)

## Wi-Fi 扫描

![Wi-Fi 扫描](wifi-scan.png)

## Wi-Fi 键盘

![Wi-Fi 键盘](wifi-keyboard.png)

## 配对码

![配对码](pairing.png)

## 证书核对

![证书核对](pairing-certificate.png)

## 卷详情

![卷详情](volume-detail.png)

## 容器详情

![容器详情](container-detail.png)

## 设置与诊断

![设置与诊断](diagnostics.png)

## 夜间总览

![夜间总览](night.png)

## 无告警体检

![无告警体检](health-summary.png)

## 低占用 · 总览

![低占用 · 总览](usage-low.png)

## 低占用 · 存储

![低占用 · 存储](usage-low-storage.png)

## 提醒阈值 · 总览

![提醒阈值 · 总览](usage-warn.png)

## 提醒阈值 · 存储

![提醒阈值 · 存储](usage-warn-storage.png)

## 接近上限 · 总览

![接近上限 · 总览](usage-high.png)

## 接近上限 · 存储

![接近上限 · 存储](usage-high-storage.png)
