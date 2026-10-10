# tools/patches —— 一键套用的补丁包（卡片自适应 + 触控/渲染性能）

把 `perf/eval-20261010` 分支相对公共祖先 **`1b8a239`** 的全部改动导出成 24 份补丁：
23 个提交（12 个 UI/卡片自适应 + 11 个 perf/bench）+ 1 份生成字体的二进制补丁。
套完得到的树与本机分支 tip `7dc1993` **逐字节相同**（见下方"验证证据"），不依赖拉分支、不依赖网络。

```sh
git checkout 1b8a239                  # 基线 = 本分支与 origin/main 的公共祖先
bash tools/patches/apply.sh --check   # 先判定：待套用 / 已就位 / 冲突（不动任何文件）
bash tools/patches/apply.sh           # 套用；已经就位就直接退出 0（幂等）
./idf.sh build
```

退出码：`0` = 已就位（含本次套用成功）；`1` = 树与基线不一致（**没有改任何文件**，并打印自查命令）。

判定不是"一份份 `git apply --check`"：补丁之间有先后依赖（后面的补丁改前面的补丁写下的代码），
而 `--check` 不写索引、不跨补丁保持状态。脚本改成往一个**临时索引**（`GIT_INDEX_FILE`）里正序真套一遍、
失败再倒序真撤一遍，才敢说"待套用/已就位"，工作树和真索引全程不动。
套用时**必须逐份** `git apply`（`git apply tools/patches/*.patch` 会因为同样的原因在第二份就失败）。

## 基线

* 基线提交：`1b8a239`（"板上配网卡：刷完固件自己提示连 Wi-Fi"），也是 `perf/eval-20261010` 与
  `origin/main`(`d84039f`) 的公共祖先；本分支落后 `origin/main` 3 个提交（`d84039f`、`314304b`、`ce67298`）。
* **如果你的树已经含远端 `d84039f`**（"按页刷新"那笔），`0015-perf-page-refresh` 会冲突：本地的 0015
  就是那笔改动的移植 + 实测。这种情况从 `1b8a239` 起套，或只挑你缺的那几份。
* 生成字体走的是二进制补丁（`0024`），已在干净 worktree 上验证可套。

## 验证证据

在 `1b8a239` 的干净 worktree 上逐份套 24 份：

```
apply.sh --check            → "待套用：24 份补丁能从当前树上一路套到底"，exit 0
apply.sh                    → 24 份全部套用，exit 0
apply.sh（再跑一次）        → "已就位：24 份都能反向撤销"，exit 0
git add -A + git write-tree → 1baf1bd0e2cfba15171923be6a47f3a2e7b81ace
git rev-parse 7dc1993^{tree}→ 1baf1bd0e2cfba15171923be6a47f3a2e7b81ace   （一致）
git diff --cached 7dc1993   → 空
```

另外两种状态也在真实 worktree 上验过：只套了一半的树报"冲突"（exit 1），基线换到 `d84039f` 也报"冲突"。

## 清单（按编号顺序套用）

| 补丁 | 内容 |
| --- | --- |
| `0001-ui-skeleton-round2` | UI 第二轮：六页骨架重排、首页/网络页纵向预算修复、告警页体检块 |
| `0002-preview-audit-tools` | 预览/审计工具链（`tools/preview/`）：格式残渣、越界、清单下限、对比度审计 + 卡片表/带宽探针 |
| `0003-docs-ui-records` | 文档：UI 四轮记录、设计合同、实机证据索引、审计规则（含 18 段二进制截图） |
| `0004-nas-app-collector` | 飞牛应用包与采集器：温度全量自适应、docker 采集与生命周期检查 |
| `0005-chore-idf-sh` | chore：`idf.sh` 构建包装、依赖锁、忽略规则 |
| `0006-ui-number-transition` | UI 数字过渡动效：读数变化逐字符滑动/柔化，全页接入 |
| `0007-docs-readme` | 文档：README 重写、采集器与应用包说明、1.2.4 发布记录 |
| `0008-ui-storage-row-anim` | 存储页行读数动效：第二行不再挡住行身份 |
| `0009-ui-list-two-rows` | 清单容器保证完整露出两行，值条补间对齐数字过渡 |
| `0010-ui-list-readability-floor` | 全页清单可读性下限：池/滚动列自动成清单，审计覆盖每一页 |
| `0011-verify-list-floor` | 实机烧录验证：清单下限在全页成立（10 PASS / 0 FAIL） |
| `0012-ui-card-content-height` | **卡片边界按内容定高**：全页卡片随内容/字符串伸缩，超出交给页级滚动（10 PASS） |
| `0013-perf-bench-instrument` | 台架仪表：串口 `bench` 命令 + 逐帧计数器（默认关，`CONFIG_FNOS_PERF_BENCH`） |
| `0014-fix-card-height-hysteresis` | 修卡片定高测量的两处滞回（chrome 手算 + 池行距复位） |
| `0015-perf-page-refresh` | 按页刷新（移植远端 `d84039f` 的刷新路径；`ui_tick` p50 75.6 → 35.4 ms，−53%） |
| `0016-docs-perf-baseline` | 触控/渲染性能基线文档（N=20 实机）+ `tools/perf_table.py` |
| `0017-perf-gesture-first-batch` | 手势与稳态第一批：BSP 绘制缓冲 120 行、整数弹簧、空闲暂停、采样 8 ms |
| `0018-perf-refresh-soon` | 按下/拖动/切页不再等刷新周期（`refresh_soon`） |
| `0019-fix-motion-stats-ifdef` | 修生产配置编不过（手势轮询暂停不能放进 `MOTION_STATS` 的 `#ifdef`） |
| `0020-perf-drag-floor` | 拖动帧间隔压到显示流水线地板（−27.9% / −29.2%），并记录三条死路 |
| `0021-perf-ui-levers` | 把 UI 侧最后的杠杆测干净（12 条负结果），UI 代码零改动 |
| `0022-perf-round3-attribution` | 归因：BSP 绘制缓冲独占帧工作 −29.6%；补两条负结果 + 台架兜底上报 |
| `0023-perf-round4-fixed-cost` | round4：适配器整帧 M2C 移除＝回归（+1.0~1.5 ms，已还原）；绘制缓冲 120 行是上限 |
| `0024-generated-fonts` | 生成字体 `components/fnos_monitor/fonts/ui_font_*.c`（`GIT binary patch`，10 MB，5 段） |

## 实测（同一台 P4 板、同一台架，每组 3 次 `swipe:3:0:3`，单位 ms）

| 配置 | 按下→首帧 `iv` | 拖动帧间隔 `rs2rr` | 松手→静止 `rr2rs` |
| --- | --- | --- | --- |
| 真基线（BSP 12 切片） | 38.6 | 31.0 | 7.3 |
| 仅绘制缓冲 120 行（4 切片） | 29.2 | 22.0 | 7.1 |
| 本补丁包全量 | **26.9** | **22.4** | **4.6** |

帧工作（`rs2rr`）−29.6%、`iv` −30.3%、`rr2rs` −37%；端到端"按下→首帧"只到 **−8.2%**——
地板是 4.6 ms 输入注入量化 + 8.35 ms 半个显示周期（详见 `docs/perf-report-2026-10-10.md`）。

## 生成字体（`0024`）两条路

* **直接套**：`0024` 是 `git apply` 能吃的二进制补丁，跟着脚本一起套即可。
* **自己生成**：跳过 `0024`，套完 `0001`..`0023` 后跑 `bash tools/gen_fonts.sh`。需要 node/npm
  （`npx --yes lv_font_conv@1.5.2`）和 `tools/fonts/*.ttf`（不入库，得自备）。
  `--no-compress` 不能去掉：LVGL 9.5 的 RLE 解压器是全局状态，而 `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`
  会两线程并行解压 ⇒ 字形碎裂/闪烁。

## 前置与验收

* 需要本地真实配置（`components/fnos_monitor/fnos_config.h`、`sdkconfig`，两者都在 `.gitignore` 里，补丁不含），
  首次 `./idf.sh build` 会拉 `managed_components/`。
* 验收：`./idf.sh build` → `bash tools/verify_all.sh --flash --port <PORT>`（本机 10 PASS / 0 FAIL）。
* 补丁**不含**被实测否掉的两条路——适配器整帧 M2C 移除（回归 +1.0~1.5 ms，已还原）、
  绘制缓冲 300/600 行（系统起不来）、帧缓冲 3→4（要改 IDF `mipi_dsi_priv.h` 与适配器
  `adapter_internal.h` 的上限宏，仓库外源码）——它们只作为负结果留在报告与 `0023` 的提交信息里。
