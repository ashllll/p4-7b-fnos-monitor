# NAS 屏幕伴侣：本地测试报告

本文件由 `bash tools/test_report.sh` 生成，**不要手改**——改的是检查本身，报告跟着变。
生成时间：2026-10-06 02:25（本机时区）

## 结论一句话

NAS 侧应用包在**开发机上**完成了端到端验证：142 项通过，0 项失败。
安装到真 NAS、以及开发板与 NAS 的真实配对，**尚未进行**（见最后一节）。

## 环境

| 项 | 值 |
| --- | --- |
| 开发机 | macOS 27.0.1 |
| Python | Python 3.14.5 |
| Node | v24.17.0 |
| fnpack | Version 1.2.3 |
| 目标运行时（NAS） | `install_dep_apps = python312` |
| 目标系统（NAS） | 飞牛 fnOS / Debian 12 |

## 产物

| 产物 | 大小 | SHA-256 |
| --- | --- | --- |
| `nas/fpk/nasscreencompanion.fpk` | 69746 字节 | `65ef8ed1ab68f27f542a990deaacfecd179c67d0528a27f40854ee82787a91fa` |
| `build/fnos_monitor.bin`（开发板固件，2026-10-06 01:38） | 1809168 字节 | `645cc61cf433902f79811107754a6e05a62cd6904d62850fc5324cc5c5451a83` |

包内共 `cmd/` 10 个文件（含共用的 `_common.sh`）。

## 检查项与结果

| 检查 | 覆盖什么 | 结果 | 怎么重跑 |
| --- | --- | --- | --- |
| 端到端生命周期套件 | 打包→按真机布局解开→安装/启动/改设置/升级/卸载；权限矩阵；TLS 与指纹；协议字段契约；明文引导通道；端口冲突不越界；向导表单；管理页 JS；平台兼容 | 142 项通过，0 项失败 | `nas/fpk/test_lifecycle.sh` |
| 产物不比源码旧（三套产物） | 见「每一项都在验什么」 | 通过 | `python3 tools/freshness_check.py` |
| 字段契约（板子读的字段 NAS 是否都在） | 见「每一项都在验什么」 | 通过 | `python3 nas/fpk/contract_check.py` |
| 向导表单 schema | 见「每一项都在验什么」 | 通过 | `python3 nas/fpk/wizard_check.py` |
| manifest 取值与包内结构 | 见「每一项都在验什么」 | 通过 | `python3 nas/fpk/manifest_check.py` |
| 每请求内存判定 | 见「每一项都在验什么」 | 通过 | `python3 nas/fpk/mem_check.py 3000` |
| 计划文档与现状一致 | 见「每一项都在验什么」 | 通过 | `python3 tools/plan_check.py` |
| 证书指纹三方一致（固件 / 服务端 / openssl） | 见「每一项都在验什么」 | 通过 | `bash tools/certcheck/run.sh` |
| 连接失败原因标签跨文件契约 | 见「每一项都在验什么」 | 通过 | `python3 tools/reason_check.py` |
| 管理页 ↔ 服务端接口字段名 | 见「每一项都在验什么」 | 通过 | `python3 nas/fpk/api_check.py` |
| 权限分类器（矩阵的每一格都来自它） | 见「每一项都在验什么」 | 通过 | `python3 nas/fpk/perm_check.py` |
| 主机预览（四条版面审计 + 配对流程断言） | 见「每一项都在验什么」 | 通过 | `bash tools/preview/run.sh` |
| 本地长测（在跑、跑的是当前代码、两面健康） | 见「每一项都在验什么」 | 通过 | `python3 tools/soak_check.py` |
| Python 开发模式零警告（未关闭的 socket/文件） | 见「每一项都在验什么」 | 通过 | `python3 nas/fpk/devmode_check.py` |
| 截断真的发生了（六段各喂 50 条） | 见「每一项都在验什么」 | 通过 | `python3 nas/fpk/collector_check.py` |
| 满载状态帧能被固件的 parse_status() 吃下 | 见「每一项都在验什么」 | 通过 | `bash tools/parsecheck/run.sh` |
| 静态栈预算 | 见「每一项都在验什么」 | 通过 | `python3 tools/stack_check.py` |
| 板子动作序列（配对 + 证书固定） | 明文取证书→自算指纹→固定建 TLS→换令牌→带令牌取数；含「固定别的证书必须失败」及其对照 | 通过（已并入套件 7d 阶段） | `python3 nas/fpk/board_sim.py --host <NAS> --port 8798 --pair-code <码>` |
| 通用发布构建 + 秘密扫描 | 不含本地配置也能编出来；产物里搜不到 SSID/口令/令牌/NAS 地址 | **本次未运行**（要两三分钟）——跑 `bash tools/release_check.sh` 或 `bash tools/test_report.sh --full` | `bash tools/release_check.sh` |

## 每一项都在验什么

- **端到端生命周期套件**（`nas/fpk/test_lifecycle.sh`）：先 `fnpack build`，把产出的 `.fpk` 解到临时目录、**复刻 `/var/apps/{appname}/` 的真实布局**（`cmd/` 一套、`target/` 一套、`var/`、`etc/`），再依次跑安装、启动、幂等启动、管理面、配对与令牌、改设置换端口、升级（配置备份 + 令牌保留）、卸载、端口冲突、Socket 建不起来时的降级。里面钉着几条容易漏的：管理页显示的证书指纹必须等于 `openssl x509 -noout -fingerprint -sha256`（板子固定的就是这个值，算错等于信错证书）；关掉 TLS 再打开后指纹必须**完全不变**（否则已配对设备全部失效）；旧固件依赖的 `/api/v1/status`、`history`、`health` 字段名与类型不许变（只能加字段）。
- **字段契约**（`contract_check.py`）：固件用 cJSON 按字段名取值，取不到就是 0——不报错、不打日志。这个脚本从固件源码**现抓**它读了哪些字段（不手抄清单），逐项到 NAS 的产出里核对。
- **manifest 校验**（`manifest_check.py`）：`fnpack build` 只查必要字段在、格式对，不查取值。这里按官方文档核 `platform`/`ctl_stop`/`service_port`/`install_dep_apps` 等，并交叉核对 `desktop_applaunchname` 确实存在于 `ui/config` 的入口 ID 里（对不上＝桌面卡片打不开）。
- **向导 schema**（`wizard_check.py`）：安装向导是用户看到的第一屏。核字段类型、选项、`wizard_` 前缀、校验规则，以及`initValue` 能不能通过自己的 `rules`（不能＝用户什么都不改就被拦下）。
- **管理页 JS**（`page_check.js`）：在 Node + 最小 DOM 垫片里跑真实脚本、喂真实响应、把每个按钮点一遍，并对渲染结果做断言——JS 读不存在的属性只得到 `undefined`，不会报错。
- **每请求内存判定**（`mem_check.py`）：打 3000 个请求，看 RSS 是平台化还是单调上涨（判据取后 1/3 比中间 1/3）。
- **静态栈预算**（`stack_check.py`）：从 ELF 算各任务调用链的栈深下界。本项目被栈溢出烧过两次，而主机预览栈是 MB 级、永远测不出来。
- **产物不比源码旧**（`tools/freshness_check.py`）：这个项目有三套产物、三条编译路径，验证成本差得很远（改文案跑预览几秒，`./idf.sh build` 两分钟起）。便宜的那条会被反复跑、贵的那条容易被跳过，于是出现最坏的情况：**源码里有编不过的东西，而所有检查都是绿的**——因为它们检查的是上一次编出来的二进制。这个脚本只比 mtime，一秒出结果。它第一次跑就抓到三套产物全部过期（其中固件比源码旧 2 分钟、应用包比 `cmd/_common.sh` 旧 3 分钟），也就是说那一轮的全绿里有一部分是对旧代码说的。
- **证书指纹三方一致**（`tools/certcheck/run.sh`）：指纹是整套信任模型里唯一的人工锚点——用户在 NAS 管理页看一串、在板子屏幕看一串，一样才放行，而这两个值由**两段完全独立的代码**算出（服务端纯 Python、板端 C + mbedtls）。这个脚本把固件里那个 `pem_fingerprint()` **原样抠出来**编到主机上跑（不抄一份，抄了就等于测手抄的那份），和 Python 版、和 `openssl x509 -noout -fingerprint -sha256` 三方对照；另外测 CRLF 换行与PEM 里带证书链两个边界。它第一次跑就抓到固件把整条链的 base64 拼起来、链一长就返回 false，而服务端是取第一段——两端会对不上，用户只会看到证书解不开。
- **满载状态帧能被固件的 parse_status() 吃下**（`tools/parsecheck/`）：`contract_check.py` 只核对字段名在不在，**没有任何检查验证板子能不能解析满载的那一帧**——而这是固件里跑得最勤的一段代码（每秒一次），却从来没在设备之外执行过（主机预览把  整个换成了替身）。做法与 certcheck 一致：从 `fnos_data.c` 里**原样抠出** `jstr/jnum/jint/jbool/parse_status` 编译到主机（cJSON 用工程里那一份），喂一份按两边上限生成的**满载**样本（8251 字节），断言解析成功、各段计数与上限一致、**且截断过的每个字符串都是合法 UTF-8**。
- **截断真的发生了**（`nas/fpk/collector_check.py`）：`contract_check.py` 比的是 `DEFAULT_LIMITS` 这个**常量**和板子的数组容量——它保证两边数字一样，但**保证不了代码真的照着这个数字裁**。证伪时把 `vols` 的截断去掉，两个检查**都是绿的**。而这类事在本机测不出来：macOS 上只有一两个卷、零个阵列、零个温度传感器，**数量本来就低于上限**，裁不裁看起来一模一样。所以这里绕开真实数据，直接给 `_trim()` 喂 50 条合成数据（快、确定、不可能是空断言），再**静态核一遍六个调用点都真的走了 `_trim()`**、且没有漏网的硬编码截断。
- **Python 开发模式零警告**（`nas/fpk/devmode_check.py`）：**静态检查和断言都看不见没说出口的问题**。开发模式（`-X dev`）会把 `ResourceWarning`（未关闭的 socket/文件）、弃用调用这些默认被静默吞掉的信号打出来——而这类东西正好是长跑才会发作：单次请求看不出异常，跑一天就是句柄耗尽。这个脚本按真机布局摆好包、用 `python3 -X dev` 起服务、对管理面 + TLS 面 + 明文引导面各打若干次、断言输出里零 `Warning`。证伪：在 `do_GET` 里故意漏一个未关闭的 socket → 报 `ResourceWarning: unclosed <socket.socket fd…>`。
- **本地长测**（`tools/soak_check.py`）：方案文档里写着「服务侧长跑已完成」，而在此之前这件事一直靠**肉眼看日志尾部**。第 18 轮吃过一次亏——那个跑了十几个小时的实例，跑的其实是一份被破坏过的副本，日志每一行都漂漂亮亮地写着「管理面=200 TLS=200 明文引导=200」，**它只是一直在测旧的、坏的那份代码**。所以这个脚本查四件事：在不在跑、**跑的是不是当前代码**（暂存副本与源码逐字节比对）、最近三次两面是否都答得上来、句柄有没有涨 / 内存有没有爬。
- **主机预览**（`tools/preview/run.sh`）：把真实的 `fnos_ui.c` + `ui_kit/` + 字库编到 macOS 上渲染出 57 张样张，四条审计全程盯着——**字形覆盖**（缺字直接 abort）、**标签溢出**（`lv_text_get_size` 比控件高）、**子对象越界**、**可点击控件互相压盖**，另有 11 条连接失败原因的渲染断言与整套配对流程断言。它是用户根本看不见这类问题的唯一拦截点。
- **权限分类器**（`nas/fpk/perm_check.py`）：P0 要交的那张「来源 / 权限矩阵」，每一格都来自 `classify_read()` / `classify_statvfs()`。**判错的代价是方向性的**——把权限不足报成不存在，用户会去查驱动和硬件；反过来用户会去翻权限配置，两种都白折腾。开发机上测不了 NAS 的结论（macOS 没有 /proc、/sys），但分类器是纯逻辑，用临时文件树 + `chmod 000` 造出真 EACCES 就能验。
- **管理页 ↔ 服务端的接口字段名**（`nas/fpk/api_check.py`）：页面发出去的字段名服务端不认识时**不会报错**——`_apply_config()` 是逐键判断的，多一个没人认的键它一声不吭。用户看到的是点了保存、提示已保存、而设置没变。`page_check.js` 抓不到这个（它喂的是预制响应，不看请求体），所以从两边现抓：页面里 `get/post` 的路径与请求体键名，服务端的路由表与各端点认的键，要求页面 ⊆ 服务端。
- **连接失败原因标签的跨文件契约**（`tools/reason_check.py`）：板子连不上的原因差别很大——网线没插和NAS 换过证书要做的处理完全不同。所以 `fnos_data.c` 失败时写一个短标签（`tls handshake`、`no cert`…），`fnos_ui.c` 的 `link_reason()` 把它翻成人话显示在总览页。**两个列表分居两个文件，谁改了另一边不知道**；漂移的后果很隐蔽——不认识的标签让 `link_reason()` 返回 NULL，那行字**整条消失**，界面看上去只是没写原因，用户不知道该干什么。这个脚本从两边现抓集合（不手抄清单）核对产出 ⊆ 已翻译与已翻译 ⊆ 产出。主机预览另有运行时那一半：逐个标签断言翻得出话、每句都不一样、且真的画到了屏幕上。
- **计划文档与现状一致**（`plan_check.py`）：核 `docs/fnos-companion-app-plan.md` 里的事实性声明——引到的路径真的存在、字段数等于 `board_fields.json` 的实际条目数、包大小等于 `.fpk` 的实际字节数、断言条数与本报告一致。过期的数字比没有数字更糟：读者不会去核对它。

## 本地**没有**覆盖到的（都需要真机）

| 项 | 为什么本地测不了 | 怎么验 |
| --- | --- | --- |
| 在干净 NAS 上安装/启动/停止/升级/卸载 | 需要真的 fnOS | 应用中心「手动安装」`nas/fpk/nasscreencompanion.fpk` |
| Linux 包用户身份下的 `/proc`、`/sys`、卷容量可读性 | 本机是 macOS，采集段本来就缺 | 装机后看管理页「能力矩阵」标签页 |
| 统一网关的实际转发与身份注入 | 需要 fnOS 的网关 | 从飞牛桌面打开应用 |
| 开发板与真实证书的握手、配对、断线恢复 | 需要 ESP32-P4 硬件 | 板子上配对 + 拔网线 |
| 24 小时稳定性（含「证书错误」「拔出网线」场景） | 需要真机长跑 | 装机后挂一天 |
| ARM 架构支持 | 未在 ARM 上装过 | 当前 `platform = x86`，验过再放宽 |
| 上架材料里的真实截图 | 需要真机画面 | P4 提交前 |

## 已知的、刻意的未完成项

- `manifest` 里 `maintainer`/`maintainer_url`/`distributor`/`distributor_url` 仍是「待填写」占位。
  `python3 nas/fpk/manifest_check.py --strict` 现在会因此报错——**这是故意的**，提交上架前必须填掉。
- 图标是自绘的占位设计（深色底板 + 蓝色显示器 + 三根负载条），正式提交前应换设计稿。
- 容器（Docker）采集默认关闭，SMART/硬盘健康首版不承诺。
