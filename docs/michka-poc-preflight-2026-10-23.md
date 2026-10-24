# Michka PoC 来源与运行预检（2026-10-23）

**结论：WP-01 来源与备份准备已取证；原版 Linux Hub 已完成一次隔离运行与 CSS 读取，Mac 平台构建的浏览器图表、输入及抓帧已开始验证，详见[浏览器分段记录](michka-browser-poc-2026-10-24.md)；原版长按菜单失败，AT-01 未整体通过。T09 的完整字节一致性实测为 FAIL，两处差异仅在注释；去注释后逐字节相等。** 本记录不改变既有工作包、验收门槛或发布授权。

## 固定来源与本地证据

上游固定为 [`v1.6.0`](https://github.com/michaeldallariva/michka/tree/4d4321983d67e19cf7ca2cf4155217aa8d984e6e)，提交 `4d4321983d67e19cf7ca2cf4155217aa8d984e6e`。研究源码位于 `/Users/llll/Documents/deepseek-harness/default-workspace/michka-ui/src`；其当前 HEAD 为 `3213285d6c22e786c0d4349ee3c0813cc98ab16d`，因此后续复现应使用固定归档，不能把当前 HEAD 当作发行 tag。

本地证据 E1：开发目录下 `.local-artifacts/todo-20261023/michka-preflight/manifest.json`，记录时间 `2026-10-23T17:34:55+08:00`、开发 HEAD `b64be2158e2076571305353a71d8a2560030dce7`。本次重新计算三个归档文件的大小和 SHA-256，均与 E1 一致；manifest 自身 SHA-256 为 `ba3d5e3404d54bfe41e2a944b69606b58502be8480f91b05f2d365d3a341afa2`。

| E1 同目录文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| `hub.deb` | 45,784,748 | `702b1747c2701b44b723f16857a25335204ab368539c76dc17fbbfa35d6b463d` |
| `michka_s` | 54,108,158 | `b8cc666231cc99ecd6334dad17d92e8b270dc3b3e2ab3ebded3585d0f3161359` |
| `michka-v1.6.0-source.tar` | 3,420,160 | `0bb44222ab9dcfd16e2ba8009036ec686ed60a35820b7758b1bc6ce073b3f4f2` |

原下载包与提取文件仍位于 `/private/tmp/michka-pkg/hub.deb` 和 `/private/tmp/michka-pkg/root/opt/michka/michka_s`；包控制信息为 `michka-hub / 1.6.0 / amd64`，可执行文件为 Linux x86-64 ELF。E1 同目录另有 22,882 B 的 `development-diff.patch` 与 `development-untracked/` 备份。它们证明归档时保留了本地差异材料，不证明恢复演练已通过，也不覆盖归档后的并行修改。所有二进制、备份及后续原始运行记录仅留本地，不进入源码发布清单。

归档内 `src/Monitor/wwwroot/css/app.css` 重新计算所得目标哈希为：

```text
1c8b8bdf923fc95030b8008f19e8f1b22433255697a64326352c120924544427
```

这是**源码归档证据**。项目通过 embedded resource 打包网页，Hub 使用 `ManifestEmbeddedFileProvider` 提供静态资源；仍须启动上述实际 ELF 后读取 `/css/app.css`，才能闭环 T09。[项目构建定义](https://github.com/michaeldallariva/michka/blob/v1.6.0/src/Monitor/Monitor.csproj)、[HubHost](https://github.com/michaeldallariva/michka/blob/v1.6.0/src/Monitor/Modes/HubHost.cs)

## 许可与运行合同

上游声明 CC BY-NC 4.0，作者 Michael DALLA RIVA / Patrick DUQUESNOY，版权为 michka.org。后续遵循上游非商业条件，保留原许可、署名与修改记录，不将上游及衍生修改重新标成 MIT / Apache-2.0。发行前按实际携带内容保留第三方许可与适用通知：.NET / ASP.NET Core、Microsoft 库为 MIT，SQLitePCLRaw 与 ECharts 为 Apache-2.0，SQLite 引擎为 Public Domain；浏览器、网关及新增依赖还需单独清点。当前完成许可来源记录，未完成整个 PoC 的最终依赖清单。[固定版本 LICENSE](https://github.com/michaeldallariva/michka/blob/v1.6.0/LICENSE)

Hub 自包含 .NET 10，不要求另装 dotnet，但 Linux 仍需兼容的 libc、libgcc、libstdc++、zlib 等本机库。它会写 SQLite、相邻 `michka.conf`、templates/widgets，并提取原生库；本实验不执行 deb 安装、systemd 注册或 kiosk 安装。[部署说明](https://github.com/michaeldallariva/michka/blob/v1.6.0/deploy/README.md)、[构建定义](https://github.com/michaeldallariva/michka/blob/v1.6.0/src/Monitor/Monitor.csproj)

可用参数为 `--no-local`（关闭本机采集）、`--name`（匿名主机名）、`--db`（隔离数据路径）、`--port`（覆盖保存端口）、`--interval`（至少 200 ms）。`michka_s` 默认 Hub 模式；本次不使用 agent。**没有 `--host` / `--urls` 参数，Hub 明确监听 `0.0.0.0`。** 不能将设置 `ASPNETCORE_URLS` 当作已证明的 localhost 隔离。[Options](https://github.com/michaeldallariva/michka/blob/v1.6.0/src/Monitor/Cli/Options.cs)、[HubHost](https://github.com/michaeldallariva/michka/blob/v1.6.0/src/Monitor/Modes/HubHost.cs)

## 后续运行方法与验收

当前 Mac 为 Darwin arm64，未发现可用 Docker / Podman / Lima / Colima / OrbStack / UTM / QEMU 执行入口；PC 的 Linux / WSL 能力未验证。2026-10-24 的 NAS 只读 SSH 探测确认 x86-64、`unshare --user --map-root-user --net true` 成功，所需 `ip`、`timeout`、`curl`、`sha256sum` 可用；未取得 Docker socket 访问权限。用户随后明确授权按该隔离方案验证，已执行第 1／2 项，并在取证后清理临时目录；实际结果见下节。以下保留 Linux 复验方法；后续 Mac 浏览器使用独立的本地平台构建，不能当作同一发行 ELF 的浏览器证据。

1. 在已有、确认可用的 Linux amd64 隔离环境中只读挂载 E1 的 ELF 至 `/app/michka_s`；为 `/work` 提供独立可写、可执行临时目录。使用非 root 用户、无外部网络的命名空间（容器可用 `--network none`），不挂载宿主服务、个人浏览器目录或 Docker socket。在该命名空间内执行：

   ```bash
   cd /work
   TMPDIR=/work DOTNET_BUNDLE_EXTRACT_BASE_DIR=/work/.bundle \
     /app/michka_s --no-local --name michka-poc --db /work/history.db --port 5000 \
     > /work/hub.private.log 2>&1
   ```

   记录实际 OS / 架构、运行库、ELF 哈希、进程命令与监听状态；日志含启动时生成的 token，原始文件仅留本地，公开结果脱敏。先证明没有对宿主或 LAN 暴露监听，再继续。

2. **T09**：从同一网络命名空间另一个进程直接请求，绕过浏览器缓存：

   ```bash
   curl --fail --silent --show-error --dump-header /work/css.headers \
     --output /work/runtime-app.css http://127.0.0.1:5000/css/app.css
   sha256sum /work/runtime-app.css
   ```

   通过条件：实际进程对应固定 ELF；响应为 HTTP 200、CSS 内容类型而非 HTML fallback / 登录页；保存响应体、时间和头部，并与上述完整目标哈希相等。缺任一证据继续记 `NOT_VERIFIED`，不能以源码／字符串搜索代替。

3. **WP-02 / AT-01**：在同一隔离环境准备独立浏览器 profile，以 1024×600、DPR 1 打开原版页面，记录原版图表及点按、长按、拖动的可观察结果和所有补丁。`--no-local` 的空数据页面只证明服务启动；图表验收还需明确标识的匿名测试输入。浏览器与 Hub 的接入方式另行验证，不默认发布 LAN 端口。此步不包含 P4 抓帧、触摸回传或性能通过结论。

WP-01 的恢复演练与最终依赖清单仍待完成。Hub 启动和 CSS 请求不能替代 WP-02 的浏览器、图表与输入；后续已进入 Chromium 抓帧及 P4 离线 JPEG 探针，见[分段记录](michka-browser-poc-2026-10-24.md)；TLS 图像传输与 P4 前向探针已开始实测，曾发生 TLS NULL 读取异常；稳定性、触摸回传与性能门未通过，不能称为可用远程客户端。

## 2026-10-24 实际隔离运行与 T09 差异

按用户新授权，将固定 ELF 和有限脚本复制到普通 SSH 用户新建的私有临时目录。执行前后均在 NAS 复算 ELF SHA-256，与上表 `b8cc…1359` 一致。进程在独立 user＋network namespace 内运行 `--no-local`，只启用 lo；保存的地址列表只有内部回环，路由表为空。没有安装、自启或改动现有屏幕伴侣。

实际 `/css/app.css` 返回 HTTP 200、`Content-Type: text/css`、50,301 B，实际 SHA-256 为：

```text
fde412cb4ccfc90641aa8afe837b2dd1996b66325f936d8e6cae59fc0f72a182
```

固定 tag 源码为 50,230 B，SHA-256 仍为前述 `1c8b…4427`。完整哈希不等，脚本的严格对比门退出 1，**T09 不判通过**。完整文本差异仅涉及原源码约 689 行、735 行处的两段注释；移除注释后逐字节相同，没有 CSS 规则变化。该结果证明发行文件和 tag 的原始 CSS 字节有区别，不把注释差异扩大为视觉差异，也不静默改弱完整哈希门。

退出清理后复查该运行目录中的进程为 0；取回本地证据后，仅删除本次 mktemp 创建的精确目录，并确认不存在。网络命名空间随进程退出回收。私有运行日志、生成 token、配置与 SQLite 未发布；本地证据在 `.local-artifacts/fps-panel-20261023/michka-netns-result/`。
