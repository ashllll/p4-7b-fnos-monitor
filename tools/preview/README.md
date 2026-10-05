# tools/preview —— 主机预览（不烧录的像素级验收）

把**设备端真实的那套 UI 代码**编译到 macOS 上渲染成 PNG：同一份 LVGL 9.5、
同一份生成字库、同一份 `kk_ui/kk_widgets.c` + `kk_rect.c` + 同一份 `fnos_ui.c`（四页版面）。
只有板级接口是替身（`stub/`：`esp_timer.h` / `esp_heap_caps.h` / `esp_log.h` / `sdkconfig.h` / `fnos_config.h`），
fixture 直接填 `fnos_status_t`，格式化串、阈值配色、可信度降级全部走真实刷新逻辑 ⇒ **看到的就是会烧进板子的像素**。

替代的是"构建 → 烧录 → 手机拍照"那十几分钟一轮：改完 `fnos_ui.c` 跑一次预览即可看图。

## 用法

```bash
bash tools/preview/run.sh [输出目录]     # 默认 out/；内部已 export DEVELOPER_DIR
open out/01-live-p0.png
```

产物：4 页 × 3 状态 = 12 张 1024×600 PNG（`01-live-p0..p3` / `02-offline-*` / `03-warming-*`）。
拆开跑则 `./build/preview <目录>`（写 PPM）+ `python3 tools/preview/ppm2png.py <目录>`。

## 前置

1. 至少跑过一次 `./idf.sh build` —— 需要 `build/config/sdkconfig.h`（主机 LVGL 用设备端真实 `CONFIG_LV_*`）；
2. `managed_components/lvgl__lvgl` 在（`./idf.sh build` 会拉）；
3. macOS 上 `git/make/cc` 需要 `DEVELOPER_DIR=/Library/Developer/CommandLineTools`（`run.sh` 已默认导出）。

## 实测开销

增量一轮 **8 s wall**（其中渲染本身 0.45 s CPU / 12 页），构建产物 `tools/preview/build` 49 MB（`build/` 已被根 `.gitignore` 覆盖）。
干净工作树验证：`git worktree add --detach /tmp/prev-clean HEAD`（= `a45c394`）+ 软链 `managed_components` +
拷 `build/config/sdkconfig.h` ⇒ **12/12 PNG 产出**，说明入库状态自洽、不依赖任何本地残留。

## 边界与坑

- **字库是真子集**：改文案后跑 `bash tools/gen_fonts.sh`（它扫 `fnos_ui.c/h` + `kk_widgets.c` + `main.cpp`
  的字符串字面量），否则新字在预览里就是方块（实机同样方块）。
- **fixture 的告警文案已按 agent 真实格式造**（`preview.c` 里 `"/vol4 used 95%"` / `"container ... down"` 等，
  全 ASCII、级别 `crit`/`warn`/`info`）。若在 fixture 里手写中文，很可能落在字库子集外而显示方块——
  那不是固件豆腐块 bug。
- **主机三处 Kconfig 必须偏离设备**（见 `run.sh` 内注释）：`LV_USE_OS 0`（`LV_OS_NONE`）、libc malloc、
  `LV_DRAW_SW_DRAW_UNIT_CNT 1`。偏离点仅限这三处语义等价替身。
- **断言在主机上改成 stderr + `abort()`**：设备端 LVGL 默认 `LV_ASSERT_HANDLER` 是 `while(1);`，
  断言失败的表现是"无声 100% CPU 死循环"（曾经就是它让 `./build/preview` 看起来像卡死）。看到 `*** LVGL ASSERT ... ***` 就是真 bug。
- **不能替代实机**：触摸/手势、刷新率、PSRAM/DMA 采样路径、真实 Wi-Fi 时序仍要烧录验证。

## 在验收流程里的位置

`bash tools/gen_fonts.sh`（改过文案时）→ **`bash tools/preview/run.sh` 看图** → 烧录 + 实机取证（`docs/verification.md` §14/§15 的取证法）。
预览管"好不好看、对不对齐、有没有方块"，实机管"真的能跑"。
