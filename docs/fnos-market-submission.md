# 飞牛应用市场上架清单（飞牛监控 / `nasscreencompanion`）

这份文件是**提交给飞牛应用中心之前要过一遍的清单**，对照官方
[上架应用](https://developer.fnnas.com/docs/quick-started/publish-application) 与
[Manifest](https://developer.fnnas.com/docs/core-concepts/manifest) 两页整理。
本地与之配套的校验脚本都在 `nas/fpk/`，最后一步的"提交"只能由开发者本人做。

---

## 1. 现状（2026-10-06）

| 项 | 状态 |
| --- | --- |
| 应用包 | `nas/fpk/nasscreencompanion.fpk`，`bash nas/fpk/build.sh` 产出（当前 75,932 字节），包内无 `__pycache__`/`.DS_Store`/`._` |
| 已装机 | 是：`/var/apps/nasscreencompanion/`，以包用户 `nasscreencompanion`(uid 961) 运行，**非 root** |
| 服务端口 | `8798`（应用）与 `8799`（开发版采集器）**并存**，互不干扰 |
| 当前版本 | 装的是 **1.0.0**（10-05 构建，内置采集器还是老的 `TEMP_LABEL` 版 → 只报 7 路）⇒ 需要把 1.1.0 升级安装上去 |
| 本机校验 | `manifest_check --strict` 只剩 `maintainer_url` / `distributor_url` 两个占位符未填；`api_check` / `contract_check` / `devmode_check` / `mem_check` / `test_lifecycle`（142 断言）全绿 |

---

## 2. 上架材料对照

| 官方要求 | 我们有什么 | 状态 |
| --- | --- | --- |
| `fnpack` 生成的 `.fpk` | `nas/fpk/nasscreencompanion.fpk`（`build.sh` 是唯一入口，别直接 `fnpack build`） | ✅ |
| 应用图标 | `ICON.PNG`、`ICON_256.PNG`，桌面入口图标 `app/ui/images/icon_{32,64,128,256}.png` | ✅ |
| manifest 信息 | 19 字段：`appname/version/display_name/desc/platform/source/maintainer/maintainer_url/distributor/distributor_url/desktop_uidir/desktop_applaunchname/install_dep_apps/service_port/os_min_version/checkport/ctl_stop/disable_authorization_path/changelog` | ⏳ 只差两个 URL |
| 更新说明 | `changelog` 已按 1.1.0 / 1.0.0 写清（**必须单行**：manifest 是 `key = value`，续行会被判非法） | ✅ |
| 支持的系统范围 | `os_min_version = 1.2.0701`（在真机 fnOS 1.2.0701 上实测），`platform = x86` | ✅ |
| 截图（**真实界面，不能用占位图**） | 待补，清单见 §4 | ⏳ |

**提交方式**：开发者后台上线前，走"应用中心开发者先锋交流群"——从飞牛官网进粉丝群 →
联系飞牛社区主理人 → 加入开发者先锋群 → 按工作人员指引提交**应用信息 + 应用包 + 测试材料**。
开发者后台上线后以平台流程为准。

---

## 3. 发布前测试

### 3.1 本机自动化（每次改包都要重跑）

```bash
python3 nas/fpk/manifest_check.py --strict   # manifest 取值 + 包内结构交叉核对
python3 nas/fpk/api_check.py                 # 管理页调的每个端点、每个请求体字段都存在
python3 nas/fpk/contract_check.py            # 板子读的每个字段，NAS 侧都在；容量上限不丢数据
python3 nas/fpk/devmode_check.py             # 开发模式下零警告（socket/文件都关干净）
python3 nas/fpk/mem_check.py                 # 没有每请求泄漏
bash    nas/fpk/test_lifecycle.sh            # 安装→启动→改设置→升级→卸载（142 断言）
bash    nas/fpk/build.sh                     # 重新打包 + 产物自检
```

### 3.2 真机（`192.0.2.10`，fnOS **1.2.0701**，x86）

| 步骤 | 观察点 | 记录 |
| --- | --- | --- |
| 应用中心 → 手动安装 → 选 `.fpk` | 向导四项（端口/监听/鉴权/HTTPS）能填、装完提示"安装完成" | ⏳ |
| 打开桌面入口「飞牛监控」 | 管理页五个标签都能开；「能力矩阵」逐项列出 `/proc`、`/sys/class/hwmon` 的**实测**可读性 | ⏳ |
| 「诊断」 | 服务自检全绿、端口/证书状态可见、日志尾巴有内容 | ⏳ |
| 「设备配对」生成 6 位配对码 → 板子「配对」页输入 | 板子显示证书指纹 4 行，与「传输加密」里的指纹**逐段一致** | ⏳ |
| 板子「温度」页 | 24 路、设备名 + 通道名 + 阈值色；`24 路传感器 · 最热 …` | ⏳ |
| 应用中心 → 升级（换更高版本包） | 走 `upgrade_callback`，配置与配对令牌**不丢** | ⏳ |
| 应用中心 → 卸载 | 服务停止、`app.sock` 清理；**8799 上的开发版采集器与 `/usr/local/bin` 的文件不受影响** | ⏳ |

> 依赖故障也要测（应用依赖 `python312`）：把依赖卸掉再装本应用，安装前检查应给出**写进用户日志**的原因，而不是一句"检查未通过"——这条在 `test_lifecycle.sh` 的 `3b` 阶段已自动化。

---

## 4. 截图清单（真实界面）

1. 管理页「概览」——一眼看到 CPU/内存/网络/温度与数据可信度
2. 管理页「能力矩阵」——权限自检（这是这个应用最有说服力的一屏）
3. 管理页「设备配对」——配对码 + 已配对设备列表
4. 管理页「服务设置」——端口/鉴权/HTTPS/间隔
5. 管理页「诊断」——自检与日志
6. 开发板「温度」页（24 路设备名 + 通道名）
7. 开发板「配对」页（指纹比对）
8. （可选）开发板「总览」页——KPI 副标签直接点名最热设备

---

## 5. 与开发版并存的关系（评审会被问到的点）

- 本应用**不监听 8799**、不改动 systemd 单元与 `/usr/local/bin` 下的开发版采集器；
  安装、升级、卸载都不碰它。
- 开发板未配对时读开发版采集器（`components/fnos_monitor/fnos_config.h` 的
  `192.0.2.10:8799`）；配对后由 `fnos_pair` 切到本应用的 `8798`
  （可开 HTTPS + 配对令牌），连接参数存 NVS。
- 两条线的协议端点一致：`/api/v1/identity`、`/api/v1/pair`、`/api/v1/status`、
  `/api/v1/history`、`/api/v1/health`。

---

## 6. 升级安装（把 1.1.0 装上去）

```bash
# 1) 确保包是最新源码打出来的
bash nas/fpk/build.sh
```

2) 飞牛桌面 → **应用中心 → 手动安装** → 选 `nas/fpk/nasscreencompanion.fpk`
   （同 `appname` 覆盖安装即走升级路径，已装的 1.0.0 → 1.1.0）

3) 装完复核（本机就能跑）：

```bash
curl -s http://192.0.2.10:8798/api/v1/identity    # 应显示 "ver":"1.1.0"
curl -s http://192.0.2.10:8798/api/v1/status | python3 -c \
  "import sys,json; t=json.load(sys.stdin)['temps']; print(len(t),'路'); print(t[0])"
# 期望：24 路，且每条带 dev / ch / dn（人读设备名）
```

> `trim_app_center` 只有 `-h/-i/-t`，**没有命令行安装接口**，所以第 2 步必须在飞牛桌面点。
