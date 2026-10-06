# p4-7b-fnos-monitor

本目录是 ESP32-P4 7B 的开发工程。继承 `../AGENTS.md` 的嵌入式开发/发布分离规则与用户当前提供的全局协作、技能和设备约束。

- 开发目录：`/Users/llll/code/esp/p4-7b-fnos-monitor`。
- 发布目录：尚未验证/建立；候选 `/Users/llll/code/esp/p4-7b-fnos-monitor-release` 不存在（2026-10-05）。禁止在开发目录执行源码发布、推送或 PR。
- 真实配置 `components/fnos_monitor/fnos_config.h` 留本地，使用 `.example.h`；不发布个人固件、串口日志、截图或凭据。
- UI 唯一源码是 `components/fnos_monitor/fnos_ui.c` + `kk_ui` 构件；旧 JSON/MVVM 管线已移除。当前版面说明见 `docs/ui-redesign-v7.md`。
- 改文案/字号后运行 `bash tools/gen_fonts.sh`，再运行 `bash tools/preview/run.sh`。不要手改生成字体。
- 改版面/控件尺寸后，`tools/preview/run.sh` 的四条审计会替你兜住"用户根本看不见"的错：字形覆盖、标签溢出、**子对象跑出父对象**、**可点击控件互相压住**。后两条是补上来的——配对键盘的四行键原先只给了装三行的盒子，第四行（删除/0/确认）整排掉到卡片外压在"关闭"按钮上，而字形审计与文字断言全都不说话：那三个键"存在"，只是画错了地方。**动盒子高度就必须回头算里面装的东西要多大。**
- **动了 UI 结构（往启动路径或 500 ms tick 里加构件/加大局部变量）之后，先跑 `python3 tools/stack_check.py`**：它从 ELF 静态算各任务调用链的栈深下界，对上预算。本项目被栈溢出烧过两次（主任务栈默认只有 3584 B，已调到 16384；`fnos_ui_create()` 这类"一次性建一大堆构件"的函数单帧能到 11 KB 量级），症状是 `Stack protection fault` + 崩溃 PC 落在堆分配里、设备每几秒重启——**主机预览栈是 MB 级，永远测不出来**。这是静态下界，函数指针回调与中断嵌套不在内。
- `./idf.sh build` 使用已验证的 ESP-IDF 5.5.3 / esp32p4 rev3.x 环境。构建不自动授权烧录/复位、提交、推送或设备外部修改；执行这些阶段需依当前用户授权。
- 只有 LVGL 线程写 UI；数据通过 `fnos_data_get`/历史接口读取，不在 UI 内请求网络。
- 物理屏幕验收按上级约定使用 `capture-board`，不能将主机预览/构建通过等同于实机验收。
