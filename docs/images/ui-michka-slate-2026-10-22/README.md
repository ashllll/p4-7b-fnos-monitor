# michka 石板灰复刻版 · 2026-10-22

近黑画布、灰色气泡卡片、居中的方块帽标题、Inter 等宽数字；青绿=正常、蓝=内存/主型号、冰青=下行/温度、橙=上行/告警。15 张原生 LVGL 1024×600 图采用匿名夹具，含六页、输入与配对、详情、诊断、夜间。这些图是主机渲染，不代表实屏验收。

当前默认主题 `michka`；改动与验证记录见[本轮归档](../../ui-michka-slate-2026-10-22.md)。

复现：`KK_PALETTE=michka bash tools/preview/run.sh out/michka-18`。

[设计与验证](../../ui-michka-slate-2026-10-22.md) · [源码/图片指纹](manifest.json)

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

## 首次连接与配对

| ![Wi-Fi 扫描](wifi-scan.png) | ![屏上键盘](wifi-keyboard.png) |
| --- | --- |
| ![配对码输入](pairing.png) | ![证书指纹确认](pairing-certificate.png) |

## 详情与诊断

| ![存储卷详情](volume-detail.png) | ![容器详情](container-detail.png) |
| --- | --- |

![设置与诊断](diagnostics.png)

## 无告警与夜间

| ![体检摘要](health-summary.png) | ![夜间总览](night.png) |
| --- | --- |
