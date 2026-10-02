# FnosDashboard — fnOS NAS 四页仪表盘（KK_UI_UMG Source package）

本目录是 **UI 的唯一源头**（KK_UI_UMG 的 Source package，LVGL 移植版）。
设计语言与移植口径见 `docs/ui-kk.md`；编写规则见 `docs/ui-kk-authoring.md`；
v3 现状盘点（几何/文案事实来源）见 `docs/ui-audit-v3.md`。

| 项 | 值 |
| --- | --- |
| packageId | `FnosDashboard` |
| namespace | `Fnos.Ui.Dashboard` |
| 平台 | ESP32-P4 + LVGL 9.5，1024×600 横屏（ROTATE_180） |
| 生成物 | `components/fnos_monitor/ui/Generated/FnosDashboard/`（可删除重建，禁止手改） |
| 手写 partial | `ui/FnosDashboardController.c`（业务映射，唯一 Store 写入者）、`ui/FnosDashboardViewAnim.c`（纯视觉动效） |
| 运行时入口 | `fnos_ui_create()`（= KK 的 `UIManager.OpenAsync`），500 ms tick 跑 `FnosDashboardController_Tick()` |

## 版面（4 页 + chrome）

- `TopBar`：主机名 / 端点 / 可信度胶囊 / 时钟 / Wi-Fi 信号条
- `Rail`：4 个导航按钮（总览/存储/网络/系统）+ 指示条
- `Bottom`：轮询统计 + 告警带
- `P0Overview` 总览：健康结论 + 存储空间 + 4 张遥测卡 + CPU/内存趋势
- `P1Storage` 存储：容量 Hero + 容量堆叠条 + 6 个卷格 + RAID + 磁盘活动
- `P2Network` 网络：DOWN/UP 双主值 + 吞吐趋势 + 4 张上下文瓦片
- `P3System` 系统：容器 + 温度 + 采集端点自检

每页回答的问题、数据合同、状态三层编码见 `docs/ui-redesign.md`（v2 数据合同，仍然有效）。

## 文件

```text
package.json    schema / 设计分辨率 / 控件词汇
layout.json     控件树（rect + 样式 + locKey）—— 版面合同
bindings.json   mvvm.fields + 绑定 + 事件 —— 动态数据合同
codegen.json    生成目标（prefix=fnos_dash）、requiredServices
strings.json    静态文案（zh-Hans / en-US）
assets.json     字体资产（LvglFont → fnos_fonts.h 符号）
validation.md   交付台账（ui-pipeline:validation-ledger）
```

## 生成与校验

```bash
python3 tools/ui_validate.py     # 校验 manifest + 写台账
python3 tools/ui_gen.py          # 生成 ui/Generated/FnosDashboard/*
./idf.sh build                   # 构建
```

## 事件契约

| 事件 | handler（`FnosDashboardController.c` 实现） |
| --- | --- |
| 导航点击 | `OnNavOverviewRequested` / `OnNavStorageRequested` / `OnNavNetworkRequested` / `OnNavSystemRequested` |
| 页面左右滑 | `On<Page>NextRequested` / `On<Page>PrevRequested`（Page ∈ Overview/Storage/Network/System） |

## 交付状态

见 `validation.md` 台账。Runtime 行只有 `Pending / Verified`；只有真机逐页目视验收后才允许写 `Verified`。
