# 飞牛监控 FPK 维护者交接（2026-10-23）

这是一份内部维护与候选包交接记录，不是应用市场提交材料，也不代表已向市场提交。当前包由应用中心手动安装；仓库 README 仍标明未提交官方应用库。本文只记录仓库当前包合同、可复现的本地验证方式与尚未完成的验收，不推断市场方现行要求。

## 当前包身份

| 项目 | 当前值 |
| --- | --- |
| 应用名 / 包名 | 飞牛监控 / `nasscreencompanion` |
| manifest 版本 | `1.2.7` |
| 平台 / 最低 fnOS | `x86` / `1.2.0701` |
| manifest 来源 | `thirdparty` |
| maintainer / distributor | `ashllll` / `ashllll` |
| maintainer URL / distributor URL | <https://github.com/ashllll/p4-7b-fnos-monitor>（两者相同，已填写） |
| 应用依赖 | `python312` |
| 遥测服务端口 | 默认 `8798`；安装时可选其他空闲端口 |
| 安装方式 | fnOS 应用中心手动上传 FPK；尚无官方应用库提交记录 |

权威包字段在 [`nas/fpk/nasscreencompanion/manifest`](../nas/fpk/nasscreencompanion/manifest)，安装、配置与运维说明在 [`nas/fpk/README.md`](../nas/fpk/README.md)。本地较早的 [`docs/fnos-market-submission.md`](fnos-market-submission.md) 是 2026-10-06 的历史清单：其中“两个 URL 待填写”等状态已经过时，不能作为当前市场要求或提交状态的依据。本文没有查验外部市场的现行流程；真正提交前由维护者按届时官方渠道确认。

## 本地构建和验证

从仓库根目录，在干净、已审阅的候选源码上依序执行：

```sh
python3 nas/fpk/manifest_check.py --strict
python3 nas/fpk/collector_check.py
python3 nas/fpk/docker_check.py
python3 nas/fpk/startup_permission_check.py
python3 nas/fpk/config_persistence_check.py
bash nas/fpk/test_lifecycle.sh
bash nas/fpk/build.sh
```

需要 Python 3、Bash、`tar` 和 PATH 上的 `fnpack`。始终通过 `bash nas/fpk/build.sh` 打包；它会同步 Docker 指标代码、清理包树中的 Python 字节码和 macOS 杂项、调用 `fnpack build`，然后检查外层 FPK 和内部 `app.tgz` 清单。不要直接运行 `fnpack build`，否则开发机的 `__pycache__` 可能进入交付包。

各主机检查分别覆盖采集器、可选 Docker 读取器、合成目录权限迁移、配置保留，以及临时布局中的安装/升级/停止/卸载生命周期。它们不替代 fnOS Linux 上的用户身份、目录权限、应用中心升级、设备连接或长时间运行验收。`bash tools/test_report.sh` 会覆盖 [`docs/fnos-companion-test-report.md`](fnos-companion-test-report.md)；运行前应先审阅该脚本的环境要求并保护既有报告。

2026-10-23 首次以源码-only archive stage 检查时，`collector_check.py` 和 `config_persistence_check.py` 出现了测试夹具缺陷。前者曾用 `Collector.__new__` 绕过生产构造器，随后调用依赖构造器创建的 `_docker_metrics`；后者只 AST 加载 loader/validator，却漏掉校验器实际引用的 `INTERVAL_MIN_S` 和 `INTERVAL_MAX_S`。当时红色运行输出保存在忽略目录 `logs/collector-red-20261023.txt` 和 `logs/config-persistence-red-20261023.txt`。

只改了这两个 host fixture，没有改 collector、配置校验器或 FPK 运行时代码：采集器夹具改为走生产 `Collector()` 构造，并在构造时把 Docker metric 请求替换为返回空对象的本地 stub；配置夹具的 AST 选择器增加了生产定义的两个 interval bound。修复后两项测试在开发树和新 stage 都通过。对于 Docker metrics 和配置边界的邻近合同，`test_metrics.py`（4 项）及 `Safety.test_startup_config_rejects_invalid_auth_tls_and_numbers`（1 项）也通过。

最终候选在新建的 `.local-artifacts/todo-20261023/fpk-handoff/stage-fixture-overlay.lkeMAa/` 中生成：stage 来自精确 HEAD `b64be2158e2076571305353a71d8a2560030dce7` 的 `git archive`，只显式覆盖两个已审阅的 fixture 文件。源覆盖补丁 SHA-256 为 `a6d3caecab8f16524ed25793c4d3912995432cce0e378597d4f6f1210e0010a0`；各文件哈希记录在候选 `release-manifest.json`。这两个测试文件不在 FPK 内容中。stage 的严格 manifest 检查通过，但因 `/tmp/fnos-docs` 快照缺失而跳过布局原文核对；collector、Docker、10 项启动权限、9 项配置保留、4 项 metrics、1 项启动配置、临时布局生命周期（143 项通过、0 失败）和 `build.sh` 均通过。生命周期测试只使用本机临时布局和本地测试服务，不是 NAS 安装验收。

候选 FPK 为 **105,369 字节**，SHA-256 `fddc91952fe931f83dc8d9787167748fe829947a7dcf0e1d5a4427f5ed5a2687`。其 `SHA256SUMS` 独立校验通过；外层 23 项、内层 app.tgz 17 项，安全归档检查未发现路径穿越、字节码/缓存、运行时配置路径或夹具测试文件；`gitleaks` 分别扫描解包的外层和 app 内容均为 0 findings。此前源码-only 构建的 105,371 字节 FPK（`d451a59ef4ff6aff0cbf17b1985a730f0ef3b23b1b84f99036af9da59ed5422c`）是另一份本地候选：两次构建的 app.tgz 内普通文件内容哈希相同，但外层 manifest 与压缩 app.tgz 字节不同，因此每份工件只能使用自己的精确 SHA-256。新 FPK、`release-manifest.json`、`SHA256SUMS` 和所有输出日志都只保存在忽略目录 `.local-artifacts/todo-20261023/fpk-handoff/`，不是已发布工件。检查日志路径由 sidecar 列出。以上均为本地主机结果，不代表 fnOS、物理设备或市场验收。

[`docs/fpk-performance-2026-10-12.md`](fpk-performance-2026-10-12.md) 记有 1.2.4 → 1.2.6 → 1.2.7 的真实 fnOS 应用中心升级，以及当时检查的包用户、配置和 TLS 文件保留。这是该日期候选的历史实机证据；不能继承为当前本地 FPK 的校验和、当前源码对应关系、24/72 小时稳定性或市场接受记录。

## 运行、权限和数据保留合同

首次安装向导当前默认值如下，用户可在设置中更改：

| 设置 | 默认值 | 需要维护者说明的影响 |
| --- | --- | --- |
| 端口 | `8798` | 冲突时换用空闲端口；应用只检测并报错，不结束占用端口的其他进程。`8799` 留给独立开发版采集器。 |
| 监听 | 内网网卡（`0.0.0.0`） | `127.0.0.1` 只供本机管理，开发板无法远程读取。 |
| 鉴权 | 配对令牌 | 可选 LAN 免令牌会允许内网设备读取；只用于受控排障。令牌以 SHA-256 保存，配对时明文只显示一次。 |
| TLS | HTTPS 开 | 首次运行生成自签证书；开发板需要人工核对证书 SHA-256 指纹并固定证书。关闭 TLS 会令遥测变为明文。 |
| Docker 采集 | 关闭 | Docker Socket 具有广泛控制能力。启用前要单独评估；管理服务不加入 `docker` 组，读取由固定请求的辅助进程完成。 |
| 采样间隔 | `1.0` 秒 | 向导接受 `0.05`–`60` 秒；默认并非长时间资源验收结果。 |

`config/privilege` 声明 fnOS 生命周期以 root 调用，但日常管理服务以专用非 root 包用户运行。root 仅用于生命周期的数据权限准备，以及可选 Docker 读取辅助进程；不要把这解释成整个应用以 root 身份采集。`config/resource` 当前没有声明共享目录。桌面入口限制为非全员可见，并声明只读访问；这些是当前包配置，不是外部市场审核结论。

### 安装、升级与恢复

- 首次安装依赖 `python312`。默认遥测为 HTTPS、配对令牌、Docker 关闭。检查端口冲突失败或无法判定时会中止，不会杀掉其他服务。
- 升级前，`upgrade_init` 在私有数据目录创建 `config.json.pre-<旧版本>`；已有回退文件不会覆盖。备份失败时升级在停服前中止。服务升级前处于运行状态，升级后才尝试恢复运行；原先停止的服务保持停止。
- 升级会补齐新字段并保留现有设置、已配对设备和 TLS 证书。配置不可读、损坏或不是 JSON 对象时应停止处理并保留原文件；先核对独立备份，再决定如何恢复。
- 包内回退点只备份 `config.json`，不是应用数据快照，也不是 FPK 版本回滚。升级前由维护者在应用外安全保存已核实的恢复材料和上一份可用 FPK；涉及 TLS 时还要把应用私有数据纳入受控备份。不要把备份文件、证书、配对记录或配置值提交到仓库。
- 卸载回调尝试停止本应用并清理 PID、Socket 和升级标志。fnOS 决定卸载时是否保留应用数据；如果用户选择删除数据，NAS 侧配置、配对记录和证书会丢失，开发板需要重新配对。卸载失败或进程未停净时，应保留 PID 证据并按应用中心报告处理，不要假报端口已释放。
- 本应用不管理独立的 8799 采集器、其他服务或开发板 NVS。恢复前后应单独确认目标应用版本、配置、服务状态和 TLS 指纹；证书变化需要重新人工核对并重新配对。

### fnOS 上的候选验收

在专用测试 NAS 上，由维护者手动通过应用中心操作，并先准备应用外备份。逐项记录候选 FPK 的 SHA-256、fnOS 版本、安装前后版本和结果：

1. 首次安装后确认应用中心状态、端口监听、管理入口、专用包用户、配置默认值和 TLS 指纹；从可信管理页人工核对指纹后再配对测试设备。
2. 从仍在支持范围的旧包升级，分别验证升级前运行和停止两种状态；检查服务设置、配对记录、TLS 证书和启动恢复行为。
3. 在合成配置故障下验证原文件保留和人工回退步骤；另行验证 OOM、突然断电、TLS 证书失败/轮换和卸载后的数据保留边界。主机回归不能替代这些真实 fnOS 故障注入。
4. 进行真实设备连接与 24/72 小时资源和稳定性观察，记录实际起止时间、峰值资源、重启/错误和数据新鲜度。不要把短时请求测量推导成高频采样或长期稳定结论。
5. 如果使用 `nas/fpk/board_sim.py`，它会消费配对码并创建测试设备记录；测试完须在管理页撤销记录。运行前先由可信管理页人工核对 TLS 指纹。

以上真实 NAS 安装/升级/配置回退、故障恢复、TLS、卸载和长测目前仍是独立验收门；本文和主机测试没有替它们判定通过。板端的全屏触摸、未裁切画面、数据新鲜度与 UI 帧率属于另一组物理设备验收，不能记作 NAS 安装测试，也不能用 FPK/主机结果代替。

## 候选 FPK 与校验和交接

不要把仓库中已有的文件哈希当成新候选的发布校验值：开发树现存的 `nas/fpk/nasscreencompanion.fpk` 为 104,796 字节、SHA-256 `6feefe9dcbcfc9f4a902b3474188df1978d2693b2fd9ade2d22c84b366395f9d`；2026-10-12 的 [`docs/fpk-performance-2026-10-12.md`](fpk-performance-2026-10-12.md) 记录的 1.2.7 包则为 104,759 字节、SHA-256 `e727f32cb50ac1504380a5a6ffe30fe63bf865fc071933b997447d1b0a15f079`。两份记录对应历史工件，不能据此证明彼此或现存二进制与某一源码修订的对应关系。隔离重建的 `b64be2158e2076571305353a71d8a2560030dce7` 候选为 105,371 字节，SHA-256 `d451a59ef4ff6aff0cbf17b1985a730f0ef3b23b1b84f99036af9da59ed5422c`；其 sidecar 与验证日志仅在开发树忽略目录 `.local-artifacts/todo-20261023/fpk-handoff/candidates/b64be2158e2076571305353a71d8a2560030dce7/` 和同目录上层 `logs/`。这是新的本地候选，不证明任何旧二进制身份，也不是市场发布校验值。

下面生成的是维护者内部交接侧车，不是官方市场格式。只在已授权的干净候选源码 checkout 中运行；命令把 FPK 与 JSON/SHA256SUMS 放在系统临时目录，不添加到仓库。先按上一节运行检查，并记录实际通过项；侧车默认将 NAS/市场验收标成未验证，维护者只能依据新证据更新状态。

```sh
# 在隔离的、干净且已审阅的候选构建 checkout 的仓库根目录运行；
# build.sh 会在 checkout 内生成 FPK，因此不要在含个人本地状态的开发树运行。
set -euo pipefail
python3 nas/sync_docker_metrics.py
test -z "$(git status --porcelain)"
SOURCE_REV="$(git rev-parse --verify HEAD)"
bash nas/fpk/build.sh
OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nasscreencompanion-handoff.XXXXXX")"
python3 - nas/fpk/nasscreencompanion/manifest nas/fpk/nasscreencompanion.fpk "$OUT_DIR" "$SOURCE_REV" <<'PY'
import hashlib, json, pathlib, shutil, sys

manifest_path, package_path, out_dir = map(pathlib.Path, sys.argv[1:4])
revision = sys.argv[4]
fields = {}
for line in manifest_path.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line and not line.startswith("#") and "=" in line:
        key, value = line.split("=", 1)
        fields[key.strip()] = value.strip()

name = package_path.name
digest = hashlib.sha256(package_path.read_bytes()).hexdigest()
size = package_path.stat().st_size
out_dir.mkdir(parents=True, exist_ok=True)
shutil.copy2(package_path, out_dir / name)
record = {
    "record_type": "local-fpk-handoff-v1",
    "source_commit": revision,
    "artifact": {
        "filename": name,
        "sha256": digest,
        "size_bytes": size,
        "appname": fields["appname"],
        "version": fields["version"],
        "platform": fields["platform"],
        "os_min_version": fields["os_min_version"],
        "install_dep_apps": fields.get("install_dep_apps", ""),
        "maintainer_url": fields.get("maintainer_url", ""),
        "distributor_url": fields.get("distributor_url", ""),
    },
    "acceptance": {
        "manifest_check_strict": "RECORD_ACTUAL_RESULT",
        "host_validation": "RECORD_ACTUAL_RESULT",
        "fnos_install_upgrade": "NOT_VERIFIED",
        "soak_and_fault_recovery": "NOT_VERIFIED",
        "market_submission": "NOT_SUBMITTED",
    },
}
(out_dir / "release-manifest.json").write_text(
    json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
(out_dir / "SHA256SUMS").write_text(
    f"{digest}  {name}\n", encoding="ascii")
print(f"Candidate sidecars: {out_dir}")
print(f"{size} bytes  SHA-256 {digest}")
PY
```

Before handoff, independently verify `sha256sum -c SHA256SUMS` on Linux or `shasum -a 256 -c SHA256SUMS` on macOS from `OUT_DIR`, compare the JSON to the FPK manifest, and attach only the approved artifact plus its sidecars through the separately authorized distribution channel. The `release-manifest.json` schema above is project-local provenance; it is not asserted to satisfy a market's submission rules. This task does not authorize uploading, publishing or submitting the FPK.

## License and source attribution

- Project firmware, NAS collector and FPK application source use the repository's [Apache License 2.0](../LICENSE). The FPK build selects `nas/fpk/nasscreencompanion`; it does not bundle the ESP32 firmware image or `components/fnos_monitor/assets/`.
- The firmware has separately scoped third-party material: LVGL/cJSON MIT, fonts under SIL OFL 1.1, the damped-spring adaptation under its retained zlib-style notice, and d3-shape basis code under ISC. Preserve each notice when distributing the firmware; see [`components/fnos_monitor/ui_kit/third_party/README.md`](../components/fnos_monitor/ui_kit/third_party/README.md) and the font license files.
- [`components/fnos_monitor/assets/michka_host_status/`](../components/fnos_monitor/assets/michka_host_status/) contains Michka-derived host-status glyphs under CC BY-NC 4.0 with attribution and modification notes in that directory's `LICENSE` and `README.md`. This directory-level license does not change the surrounding Apache-2.0 project license. A distributed derivative containing those glyphs remains subject to the noncommercial condition for that artwork; commercial redistribution needs separate permission or replacement artwork. Keep that asset and its attribution/license separate from the FPK source and any Apache-only description.
- The FPK manifest declares `python312` as an fnOS application dependency. The package source tree has no Python `requirements.txt`/`pyproject.toml` dependency lock; do not imply that the Python runtime is vendored into the FPK. `fnpack` is a build-time tool, not an installed-app dependency.

## 仍需维护者关闭的门

- 以上是隔离主机 stage 的结果；严格 manifest 检查仍缺 `/tmp/fnos-docs` 布局原文快照。不要把这项跳过写成完整的布局原文核验。
- 在真实 NAS 上完成应用中心安装/升级/配置回退、OOM/断电、TLS 和卸载数据保留测试；完成有实际起止证据的 24/72 小时稳定性与资源观察。
- 独立完成板端的全屏未裁切显示、完整触摸交互、数据新鲜度和 UI 帧率验收；此项不属于 NAS 应用生命周期验收。
- 在提交前核对届时官方应用市场的流程和材料要求。当前记录仍是“未提交”；不得从旧清单推断已满足现行要求。
- 发布包含 Michka 派生字形的固件前，单独确认 CC BY-NC 4.0 的分发范围和商业许可；Apache-2.0 项目许可不会覆盖该限制。

本次截至 2026-10-23 仅在隔离的本地 archive stage 构建并检查候选；没有进行真实 NAS 安装/升级/卸载、板端或网络验收、市场联系、上传或发布。
