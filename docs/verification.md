# 验证指南

本文件集中说明检查入口与证据边界；当前结果和剩余限制见 [未闭环事项](open-items-2026-10-11.md)。历史逐轮日志可从 Git 历史查阅，不作为当前候选的重新验收。

## 按改动选择检查

| 改动 | 必须检查 |
| --- | --- |
| 固件 C／C++、依赖或 Kconfig | ESP-IDF 实际构建；涉及任务调用链时检查最新 ELF 栈预算 |
| 页面、控件尺寸、文案 | 原生预览；文案／字号先生成字体；查看生成图片 |
| 页面控制器／手势 | 逐帧 motion、首次入页和重复目标回归 |
| 数据、历史、配对或 NVS | 对应生产函数回归；真实持久化／掉电需独立设备证据 |
| NAS 服务、配置或生命周期 | 对应安全／配置／升级测试、匿名生命周期；真实安装另验 |
| 检查脚本 | 正向、故障负控、退出码与资源收尾；不能仅跑语法检查 |
| 文档／清理 | 路径与引用、计划契约、相关测试入口、差异与秘密扫描 |

## 固件构建与栈

从仓库根目录激活 ESP-IDF 5.5.5 环境，使用工程入口：

```bash
./idf.sh build
python3 tools/stack_check.py
```

可通过 FNOS_IDF_ENV 指向自己的 activation script，通过 FNOS_IDF_PYTHON 指定匹配解释器。已有私密配置留在本地；公开构建使用配置模板。构建命令必须实际执行 build，不能把 set-target 或缓存存在当作构建成功。

栈检查读取最新 ELF 的七个任务入口，给出静态深度下界。树外调用、函数指针回调、中断嵌套和运行时高水位仍需独立验证；主机预览的栈大小不能证明设备栈安全。

默认构建不证明 `CONFIG_FNOS_SOAK_DIAG` 分支已编译。诊断版使用独立构建目录／sdkconfig，并保留对应 ELF 身份；不要覆盖个人配置。

## UI 与预览

```bash
# 改文案或字号时才需重新生成；不要手改生成字体。
bash tools/gen_fonts.sh
bash tools/preview/run.sh
# 检查滚动条分类与逐帧依据的自测，不依赖设备。
node tools/preview/scroll_gap.js --self-test
```

完整命令、硬件矩阵、分辨率与动效模式见 [预览说明](../tools/preview/README.md)。输出使用预览目录内的独立相对名称；非空目录必须有脚本的所有权标记。产物不提交。

预览审计覆盖字形、文字溢出、子对象越界、交互重叠、格式说明符残留等。稳态样张、硬件夹具和 motion 各有覆盖范围，图像必须实际查看；样张数量不是生产硬件上限。

主机使用设备 UI 源码，板级接口与部分运行配置是替身。当前设备 LVGL 分配失败会先进入启用的断言；NULL 注入只证明调用者收到 NULL 时的回退，不证明真实 OOM 可恢复。

## 离线回归与 NAS 生命周期

常用的独立检查：

```bash
python3 -W error::ResourceWarning tools/test_gate_failures.py
python3 -W error::ResourceWarning tools/test_host_gate_failures.py
python3 tools/test_manifest_failures.py
python3 nas/fpk/test_validation_gates.py
python3 tools/test_report_entry.py
python3 tools/test_evidence_gates.py
python3 tools/plan_check.py
```

两个入口共用 `tools/host_checks.sh` 的 54 项主机检查清单，包含完整预览、滚动条审计、历史质量、锁竞争、MPT 超时、指标质量和安装包元数据核对。`bash tools/verify_all.sh` 继续执行实际固件构建与栈检查；`bash tools/test_report.sh --full` 另跑匿名 NAS 生命周期、内存检查并生成 [测试报告](fnos-companion-test-report.md)。`--quick` 跳过生命周期，明确返回未完成；`--full` 仍可能因长测证据不足返回 2。缺少工具或证据不能计通过，硬失败优先返回失败。

在干净构建目录首次运行 `bash tools/verify_all.sh` 前，先执行一次 `./idf.sh build`：脚本的主机检查阶段包含预览，需要 `build/config/sdkconfig.h`，而固件构建在后续阶段；缺少该头文件会明确报错，不会静默跳过。

NAS 回归使用与目标 fnOS 一致的 Python 3.12；先让 PATH 中的 `python3` 指向它。ESP-IDF 构建另用其匹配虚拟环境，不要共用解释器。主机预览输出可设 `FNOS_PREVIEW_OUT=out/my-check`，设备配置头可设 `FNOS_UI_DEVICE_CONFIG`。本机 Python 3.15 会在既有 TLS 清理路径报 `return in finally` 警告，尚未声称兼容该版本。

`bash nas/fpk/build.sh` 从实际 FPK 生成 `nas/fpk/release.json` 与 README 校验块；`python3 nas/fpk/release_metadata.py` 只核对。检查会验证包内 manifest、fnpack 的 app.tgz 校验值、整包 SHA-256、大小及 README 一致性。生成元数据不代表已部署或发布。

NAS 侧入口与安装合同见 [伴侣实现合同](fnos-companion-app-plan.md) 和 [应用说明](../nas/fpk/README.md)。生命周期夹具只认领本次启动的进程、失败时保留证据；临时环境通过不等于真实 fnOS 包用户／目录权限、网关、证书或升级已验收。

## 设备与长测

烧录、串口、NAS 安装与长跑按明确任务授权执行。示例中的端口必须换成目标板实际端口：

```bash
export FNOS_SERIAL_PORT='/dev/cu.YOUR_BOARD_PORT'
./idf.sh -p "$FNOS_SERIAL_PORT" -b 230400 flash
./idf.sh -p "$FNOS_SERIAL_PORT" monitor
```

串口打开／关闭可能使板子复位。用 `tools/page_shot.py` 切页取证时，确认固件身份、页面、数据新鲜度与日志；手机拍屏使用 capture-board 并实际查看图片，不上传个人网络或设备清单。

72 小时评价器的用法见 [诊断与长测说明](../tools/audit125/README.md)。合成日志自测、短时观察、无重启计数或一次照片都不能代替候选诊断固件的真实长测。

## 常见故障

- 字形破碎或缺字：核对未压缩字库、字符覆盖和绘制线程配置，修改后重新生成并预览。
- 首帧触摸／回调异常：UI 尚未建树时必须安全返回，不能只依赖后续刷新正常。
- 界面冻结或定时器缺失：检查 LVGL 断言、分配日志和 ready 状态；区分 NULL 回退与真实 allocator 耗尽。
- 网络或 TLS 失败：区分连接、证书与令牌错误；不删除信任材料或自动改成明文。
- 缺失清单：检查来源权限、遗漏元数据和字节预算；旧数据、缺测和真正零值分别处理。

## 发布检查

核对发布目录、分支、remote、明确文件清单、实际暂存差异及将推送的历史。使用 Gitleaks 或等效秘密扫描，并与本地已知真实值无回显匹配；不提交私钥、真实配置、响应、日志、截图、个人固件或构建产物。

源码 push 后回读远端分支与提交。新的 FPK 必须另行构建、扫描、核对包内容并完成安装验收；历史安装包或主机检查不能替代该阶段。
