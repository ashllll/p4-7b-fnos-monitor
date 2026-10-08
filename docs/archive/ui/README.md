# 历史 UI 方案（只作参考资料，不再是实现依据）

本目录是**被取代**的界面方案与审计。当前实现依据只有两份：

- `docs/ui-redesign-v11.md` — 视觉与版面合同（令牌、阈值、页面问题、测量口径）
- `docs/ui-v12-lvgl-native.md` — LVGL 原生自适应实现合同（ui_kit、动画、迁移与清理）

这里的历史文件记录"为什么当初那么做/后来为什么不那么做"，
读它可以避免重犯（例如 kk_ui 的绝对坐标推导、UniFi 风格的分区卡墙）。
**不要照着它们写新代码。**

## 归档内容

- `ui-redesign.md` / `ui-unifi*.md` / `ui-audit-v3.md` — 早期视觉方向与审计（已被 v11 合同取代）。
- `ui-kk*.md` — kk_ui 时代的实现与写作规范（kk_ui 已整目录删除，改为 `ui_kit/` + LVGL 原生）。
- `ui-redesign-v7/v8/v9.md` — v11 之前的视觉合同迭代。
- `mockup-v11/` — **HTML/CSS 设计原型**（`render.html` + `css/` + `js/` + `renders/`，曾用 Chrome headless
  出 18 张渲染图给用户确认方向）。视觉合同里的令牌与测量口径就是从它来的，但**实现早已换成设备端
  `components/fnos_monitor/ui_kit/` + LVGL 原生**：原型不再构建、不再测量、不再作为交付物，
  保留它只为"当初为什么定这些数字"的可追溯性。要看图请看 `renders/`，不要再去跑它。
