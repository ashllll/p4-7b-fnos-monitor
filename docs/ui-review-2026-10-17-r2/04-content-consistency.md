# 04 · 文案 / 单位 / 本地化 / 设计系统一致性复核（835de2c，r2）

- 复核对象：`835de2c`（`feat(ui): unify instrument layout and optimize stable refresh`），源码 worktree `/Users/llll/code/esp/p4-7b-fnos-monitor-835de2c`。
- 新帧：`tools/preview/out/ui-review-r2-1024/`，112 张状态帧 + `refresh-work/` 3 张刷新回归帧 = 115 张，1024×600；`docs/ui-review-2026-10-17-r2/preview-run.log`。
- 设计合同：`docs/ui-design-language-2026-10-15.md`（字号/间距/单位角色）、`docs/ui-cuktech-2026-10-14.md`（数据可信度、颜色之外保留事实文字、卡内保留来源名称）、`docs/ui-instrument-2026-10-14.md`（仅 1024×600 横屏）。
- 方法：逐帧读图 + 源码定位；未改源码、未构建、未烧录。上一轮 r1 报告未作为依据，全部在 835de2c 上重新确认。

## 计数

| 级别 | 数量 | 条目 |
|---|---|---|
| P0 | 1 | P0-1 |
| P1 | 2 | P1-1、P1-2 |
| P2 | 8 | P2-1 … P2-8 |
| P3 | 8 | P3-1 … P3-8 |

---

## P0

### P0-1 首采前温度卡自称“离线 · 显示上次快照”，同屏顶栏却是“等待数据 / 等待采集端”

- 源码：`components/fnos_monitor/fnos_ui.c:4379-4385`
  - `4379` `if (!st->online) {` —— 只判在线位，**没有 `st->ever_ok` 守卫**；
  - `4380` `overview_note = why ? why : "离线，显示最后一次采集快照"`；
  - `4381-4383` 温度值置 `"--"`，`home_temp_name = "离线 · 显示上次快照"`；
  - 对照：`fnos_ui.c:4340-4342` 顶栏正确三分支（`!ever_ok → "等待数据"`；`online → "在线"`；否则 `"离线"`），`fnos_ui.c:4188-4191` 副标题 `!ever_ok → "… · 等待采集端"`。
- 帧：`00-startup.png`、`01-warming-p0.png`。
- 原文：“离线 · 显示上次快照” / “离线，显示最后一次采集快照” / 值 `--`；同屏 chip “等待数据”，左上“总览 · 等待采集端”，CPU/内存/存储均为“等待数据/等待采集”。
- 建议：进入分支改为 `if (st->ever_ok && !st->online)`；`!ever_ok` 时温度卡写“等待采集 · 暂无快照”，`overview_note` 用“尚未采到数据”等中性文案。
- 影响：新机或刚重启、从未采样成功时，首屏就声称“显示上次快照”，用户会以为历史数据丢失或正在回放旧值；同屏两种状态语义直接矛盾，是本次最严重的数据可信度问题。

---

## P1

### P1-1 告警页对上游英文消息原样直出，中文界面里出现 4 条英文告警

- 源码：`components/fnos_monitor/fnos_ui.c:4941` `set_txt(l, "%s%s", st->online ? "" : "上次采集：", st->alerts[i].m)` —— `alerts[].m` 不进任何中文映射。
- 桩件/真实格式：`tools/preview/preview.c:376-382`（按 `nas/fnos-agent.py:_alerts()` 的真实格式造，全 ASCII）：
  - `/vol4 used 95%`、`/vol3 free 4.6% left`、`container download-service down`、`md1 resync 47.3%`。
- 帧：`02-live-p5.png`（告警 · 200ms 更新）、`03-offline-p5.png`、`06-offline-tls-p5.png`（均带“上次采集：”前缀）。
- 原文/建议：`/vol3 free 4.6% left` → “卷 /vol3 剩余 4.6%”；`/vol4 used 95%` → “卷 /vol4 已用 95%”；`container download-service down` → “容器 download-service 已停止”；`md1 resync 47.3%` → “阵列 md1 重建 47.3%”。
- 影响：告警页是唯一“出问题看这里”的页面，四条消息全部是英文短语、只有数字和卷名尚可辨认；离线时再叠“上次采集：”英文，等于把未本地化的采集端日志直接端给用户。屏侧可做关键词映射并保留原文兜底。

### P1-2 离线时温度卡仍写“离线 · 显示上次快照”，但读数被固定置 `--`（并不显示上次温度）

- 源码：`components/fnos_monitor/fnos_ui.c:4379-4385`（离线分支无条件 `set_num(home_temp_value, "--")`），`fnos_ui.c:4418` 再强制显示该脚注。
- 帧：`03-offline-p0.png`、`06-offline-tls-p0.png`。
- 原文：“最热温度 -- / 离线 · 显示上次快照 / 连不上 NAS：应用没启动或端口不对 / 传感器 · 最近 3 分钟 旧值”。保留的 `st->temps[]` 实际可用，但卡片选择不显示。
- 建议：要么按旧值显示（“温度旧值 74°C · 显示上次快照”），要么改文案为“离线 · 温度暂不显示”；不要在值为空时声称“显示上次快照”。
- 影响：字段名与行为相反，用户会以为温度采集中断或传感器掉线；06-offline-tls-p0 的链路原因已经写得很好，这张卡却把它抵消掉。

---

## P2

### P2-1 温度通道名直接上屏（MAC / temp1 / Package id 0 / nvme0n1 / Composite）

- 源码：`components/fnos_monitor/fnos_ui.c:4750-4756`（系统页温度摘要行 `ht->ch`/`g0->dn` 原样直写）、`fnos_ui.c:3668`（温度页设备块通道名 `t->ch`）；`fnos_ui.c:4393` 首页来源名同样是 `t->dn · t->ch`；字段定义见 `components/fnos_monitor/fnos_data.h:48`（`dev/ch` 是内核短名与通道名）。
- 帧：`02-live-p3.png`（“最热 Package id 0 · 7 路”“最热 MAC · 2 路”“最热 temp1 · 1 路”“nvme0n1 · 3 路 · Composite”）、`02-live-p4.png`（设备块 “Integrated Graphics · temp1”“NVMe Demo Drive A · nvme0n1”）、`02-live-p0.png`（大字来源 “Ethernet Controller 10GbE · MAC”）。
- 建议：只对 `ch` 做映射：`Package id 0` → “封装温度”、`MAC` → “网卡控制器”、`temp1` → “板载传感器 1”、`Composite` → “综合温度”、`nvme0n1` → “NVMe 0”；设备名（Demo Processor / Ethernet Controller 10GbE 等）按 cuktech 合同“卡内保留来源名称”保留。
- 影响：24 路温度里有 5 类原始内核名，普通用户无法判断哪一路是 CPU、网卡还是硬盘；温度页是本次改造的重点页，这层本地化缺口直接落在主视图。

### P2-2 “容器未启用”与“上次运行中”同屏并存（预览桩与采集端 contract 冲突）

- 源码：`components/fnos_monitor/fnos_ui.c:3937-3945`（`docker_module_note()`：disabled → “未启用”）、`fnos_ui.c:4636-4657`（`dock_summary`：可读且 `ndock>0` 时拼“容器%s · 上次 %d/%d 运行”）、`fnos_ui.c:4709-4724`（行值 `prefix + "运行中/已停止"`，`prefix` 只在实时时为空）。
- 帧：`02-live-p3.png`、`09-legacy-p3.png`：摘要“容器未启用 · 24 路温度 / 7 路温度”，chip“已关闭”，行却全是“上次运行中”。
- 采集端 contract：`nas/fpk/nasscreencompanion/app/server/fnos_collector.py:1121-1123` —— `docker_enabled=false` 时 `snap["docker"]=[]`、status=`disabled`，即真实设备上 disabled 不会带容器数组。
- 预览桩：`tools/preview/preview.c:235` 基础模块表就是 `{ "docker", "disabled" }`，同时 `preview.c:350-367` 保留 8 个容器；项目自己的 `verify_docker_module_states()`（`preview.c:2510-2550`）在 disabled 时断言不得出现容器行。
- 建议：预览桩在 disabled 时清空 `docker[]`（与采集端一致）；屏侧在 `state=disabled` 与固定行之间加一道防御（隐藏行或空态“未启用，不读取容器列表”）。
- 影响：同一张卡里“未启用”和“上次运行中”互相否定，用户无法判断容器是否在跑；`09-legacy-p3` 的 v1 回退路径同样中招，说明不是单帧偶发。

### P2-3 诊断页“轮询 18422 / 7”把成功次数写成了轮询次数

- 源码：`components/fnos_monitor/fnos_ui.c:4876` `set_num(agent_detail[3], "%u / %u", st->ok_count, st->fail_count)`，行标签在 `fnos_ui.c:216-219` 的 `AGENT_KEY[3] = "轮询"`。
- 帧：`06-diagnostics.png` “轮询 18422 / 7”。
- 原文/建议：`轮询 18422 / 7` → “轮询 成功 18422 · 失败 7”（或“成功 18422 / 共 18429”）；是成功/失败计数，不是总轮询次数。
- 影响：18422 会被读成“轮询了 18422 次”，7 会被读成“其中 7 次失败”或“共 7 次”；诊断页最重要的一行失去判断力。

### P2-4 首采前打开“设置与诊断”，“数据年龄”显示开机时长假年龄

- 源码：`components/fnos_monitor/fnos_ui.c:3336-3340` `data_age_ms()` = `esp_timer_get_time()/1000 - st->recv_ms`（首采前 `recv_ms=0`）；`fnos_ui.c:4878` `fmt_age(..., data_age_ms(st))` 无 `ever_ok` 守卫；顶栏 `fnos_ui.c:4188-4191` 有守卫，诊断页没有。
- 帧：无（115 张未包含“暖机 + 诊断页”组合；打开系统页→“设置”即可到达，代码可证）。
- 建议：`!st->ever_ok` 时该行写“从未采集”或“—”，不要走 `fmt_age`。
- 影响：首采前会显示“3 分前”这类开机时长，与同屏“等待数据”矛盾，且把“数据年龄”这个可信度指标变成假信息。

### P2-5 同一容量单位两种写法：`G` 与 `GB`

- 源码：`components/fnos_monitor/fnos_ui.c:4038` 内存“可用 %.1f / %.1fG · 交换 %.1fG”；`fnos_ui.c:4658` 系统页“交换 %.1f/%.1fG”；`fnos_ui.c:4882` ZFS“%.1f GB”；`fnos_ui.c:397-404` `fmt_cap()` 用“GB/TB”。
- 帧：`02-live-p0.png` “可用 5.9 / 16.0G · 交换 0.1G”、`06-diagnostics.png` “ZFS 缓存 12.4 GB · 命中 96.3%”、`04-healthy-p0.png` 同内存行。
- 建议：全站统一 `GB`（紧凑行可保留 `G` 但需在合同里写明），至少同一屏的 ZFS/内存/交换不要混用两种。
- 影响：ZFS 行看起来像另一个单位体系；单位审计时无法用一条规则校验。

### P2-6 已停止容器详情会带出英文 `Exited (…)`（潜在，现有帧未覆盖）

- 源码：`components/fnos_monitor/fnos_ui.c:4710` 行副行先拼 `prefix + d->s`，`fnos_ui.c:4716-4719` 又把同样的原文写进 `name2`；`fnos_ui.c:1265-1270` `entity_open_cb` 把 `row->name2` 原样复制进详情 `detail_meta`（`1268`）。
- 帧：现有 `13-container-detail.png` 打开的是运行中的 media-service，只显示“上次运行中 / CPU …”；没有打开已停止容器（editor-service）的帧，故为代码可证的潜在缺陷。
- 建议：`d->s` 只作为英文兜底，屏侧把 `up=false` 统一成“上次已停止 · 2 小时前”（相对时间可从 `metrics_ts`/采集时点算），或对 `Exited (…)` 做最小映射。
- 影响：点开已停止容器时，中文详情里突现 “上次Exited (0) 2 hours ago”，且前缀与英文直接拼接无空格；这是详情唯一的“原始日志泄漏口”。

### P2-7 顶栏长 SSID 只显示 7 个字符加“…”：按钮宽度按“Wi-Fi 未配置”写死

- 源码：`components/fnos_monitor/fnos_ui.c:948-952` 用 `lv_text_get_size("Wi-Fi 未配置")` 算宽度后建 `wifi_btn`；`fnos_ui.c:2738-2754` 只更新文字，不改宽度。
- 帧：`10-wifi-long-ssid.png` 顶栏显示 `ABCDEFG…`，与“配对”之间约 400px 空白。
- 建议：按钮宽度改为 `LV_SIZE_CONTENT` + 顶栏剩余空间上限（或按最长 24 字符预留），仅在真正放不下时省略；截断时给完整 SSID 的可读入口（配对/设置页已显示当前网络）。
- 影响：用户确认“连的是哪个 AP”时只能看到 7 个字符；多 SSID 前缀相同的现场无法区分。该项与布局审查交叉，文案侧判“截断策略不匹配内容长度”。

### P2-8 离线系统页写“容器离线”，把链路态说成容器态

- 源码：`components/fnos_monitor/fnos_ui.c:4636-4657`：`dock_note` 离线时为“离线”，`dock_summary` 拼成“容器离线 · 上次 8/8 运行”，页头再前置“旧快照 · ”；chip 同时写“离线 · 旧值”。
- 帧：`11-docker-retained-offline.png` “旧快照 · 容器离线 · 上次 8/8 运行 · 24 路温度”。
- 建议：“链路离线 · 容器为旧值（上次 8/8 运行）”或“容器数据：上次 8/8 运行”；不要让“容器”成为“离线”的主语。
- 影响：读成“容器离线/容器挂了”，而实际只是板子连不上 NAS，容器状态未知；对排障方向是误导。

---

## P3

### P3-1 07-pair-confirm 状态卡与指纹卡双标题“核对证书指纹”

- 源码：`components/fnos_monitor/fnos_ui.c:3104`（阶段卡标题切到“核对证书指纹”）与 `fnos_ui.c:2195`（指纹卡标题本身就叫“核对证书指纹”）。
- 帧：`07-pair-confirm.png`（顶部 rail 实为“fnos-nas / 系统 · 200ms 更新”，裁图已核对，重复发生在卡内，不在顶栏）。
- 建议：阶段卡改“配对确认/安全确认”，或指纹卡保留“证书指纹”；一屏只留一个同名标题。
- 影响：两个同名标题相距约 80px，用户找不到“该在哪里确认”，也弱化了“逐段比对 4 行指纹”的指令层级。

### P3-2 实体详情把状态行重复一遍

- 源码：`components/fnos_monitor/fnos_ui.c:1265-1271`：`detail_value = row->val`（如“上次运行中”），`detail_meta = row->name2`（`fnos_ui.c:4716-4719` 里 `name2` 第一行又是同一个“prefix + 运行中”）。
- 帧：`13-container-detail.png`（“上次运行中”×2）、`12-container-metrics.png`（“运行中”×2）。
- 建议：容器 `name2` 不再重复状态行（只留 CPU/内存/重启），或详情页对第一行做去重。
- 影响：详情首屏最显眼的两行是同一句话；在只读面板里浪费一行，看起来像渲染重复。

### P3-3 13-long-detail：超长卷名在详情里换行十余次，把读数压到面板下半部

- 源码：`components/fnos_monitor/fnos_ui.c:1311`（`detail_name` 用 CJK20 且允许换行）。
- 帧：`13-long-detail.png`：`/very-long-volume/完整名称必须可读/…` 重复十余行，`54%` 与“剩余 1.7 TB / 总计 3.6 TB · ext4”被推到面板中部以下。
- 建议：身份名最多 3 行 + 省略号，完整名称放到可展开/可滚动的副区；至少保证读数与容量行始终在首屏。
- 影响：极限长名下“读数值”被名称挤走，长按/点击详情的目的（看读数）失效；真实 NAS 名称通常更短，但合同要求长名可读，这是当前实现的边界行为。

### P3-4 运行时长用“2d 07h”英文单位

- 源码：`components/fnos_monitor/fnos_ui.c:430-437` `fmt_uptime()`（`%ud %02uh` / `%uh %02um` / `%um`）。
- 帧：`02-live-p0.png`、`04-healthy-p0.png`“运行 2d 07h”；`03-offline-p0.png`“旧快照 · 运行 2d 07h”。
- 建议：“2 天 07 小时”或紧凑“2天7小时”；与全站中文单位一致。
- 影响：中文界面里混入 d/h 英文缩写，老年用户/非技术用户不一定认得；与“运行时长 55:41”并排时更显不一致。

### P3-5 容量进位边界：1023.6 GB 会显示成“1024 GB”，下一次才跳“1.0 TB”

- 源码：`components/fnos_monitor/fnos_ui.c:397-404`：`gb >= 1024 → "%.1f TB"`，否则 `gb >= 100 → "%.0f GB"`；1023.6 落在第二档并四舍五入为 1024。
- 帧：无（115 张没有 1023.6 边界样本，代码可证）。
- 建议：按“四舍五入后的值”判断单位，或 1023.5 以上直接进 TB；避免出现“1024 GB”。
- 影响：同一卷相邻两次刷新从“1024 GB”跳到“1.0 TB”，数值先变大后变小，看起来像数据错误。

### P3-6 同一状态/指标多套措辞

- 源码与帧：
  - disabled：chip 用 `mod_status_cn` 的“已关闭”（`fnos_ui.c:236`，`02-live-p3.png` chip），摘要/空态用 `docker_module_note` 的“未启用”（`fnos_ui.c:3943`，同帧摘要、`11-docker-disabled.png`）。
  - denied：`mod_status_cn` “没权限”（`fnos_ui.c:234`，`10-overview-partial.png` 网络卡）、`smart_note` “无权限”（`fnos_ui.c:4328`）、docker 备注“权限不足”（`fnos_ui.c:3944`）。
  - 空容器：实时空列表“暂无容器”（`fnos_ui.c:4652`）与首页“未发现容器”（`fnos_ui.c:4099`）。
  - 温度：首页脚注“最高 74°C”（`fnos_ui.c:4401`）与磁贴/温度页“最热温度”“最热”（`fnos_ui.c:1429`、`4866`）。
  - 同一指标：网络页 KPI“采集延迟”（`fnos_ui.c:1584`）与诊断页“HTTP”（`fnos_ui.c:217`、`4875`）。
- 建议：每个状态/指标定一个 canonical 词，状态词统一走 `mod_status_cn`；指标名在诊断页与网络页二选一。
- 影响：同一事实两种说法，用户会以为是不同条件（“已关闭”是不是比“未启用”更严重？）；跨页阅读需要二次翻译。

### P3-7 Wi-Fi 凭据在“口令”和“密码”之间混用

- 源码：`components/fnos_monitor/fnos_ui.c:2409`/`2410` 标签“口令”、`fnos_ui.c:2665` “输入这个网络的口令”、`fnos_ui.c:2789` “要更换口令，可点「修改密码」”，但按钮 `fnos_ui.c:2772` 写“修改密码”。
- 桩件：`tools/preview/preview.c:3179` 失败原因“密码不对（认证失败）”（固件真实文案见 `components/fnos_monitor/fnos_net.c:159` “认证失败（口令不对）”）。
- 帧：`10-wifi-failed.png`（“密码不对（认证失败）”+“要更换口令，可点「修改密码」”）、`10-wifi-pass.png`（“口令”输入框）。
- 建议：统一为“口令”（与输入标签、固件原因一致），按钮改“修改口令”；预览桩原因改“认证失败（口令不对）”。
- 影响：同一流程里两个词，用户会怀疑“密码”和“口令”是不是两件事。

### P3-8 设计语言里的“页面标题 CJK20”在本版本没有落点（文档角色歧义）

- 合同：`docs/ui-design-language-2026-10-15.md:15-16` 同表一行写“页面标题、详情身份 CJK20”，另一行写“六页标题 CJK16”。
- 实现：`CJK_20` 仅用于详情身份/告警健康值/Wi-Fi 大字（`fnos_ui.c:1311/1313/2079/2571`）；六页识别由底栏页名（CJK16）与顶栏副标题（CJK12）承担，没有 20px 页面标题。
- 帧：`02-live-p0.png`…`02-live-p5.png` 全六页。
- 建议：合同二选一——删掉“页面标题 CJK20”，或明确 20px 只服务“详情身份/单屏大字状态”；当前实现与合同第二行一致，但与第一行冲突。
- 影响：设计系统审计无法判定“页面标题”角色是否缺失；后续若按第一行补 20px 页标题，会与现有六页骨架冲突。

---

## 已核对非缺陷

1. **°C 用法**：所有温度都以 `°C` 结尾，没有裸 `C`/`℉`；读数 1 位小数、摘要取整（74 vs 74.2）属层级压缩，不是单位错误。
2. **MB/s vs Mbps**：网络速率用 `MB/s`（字节），链路速率用 `Mbps`（比特），两者都正确且未混用（`02-live-p2.png`：`12.2 MB/s` 与 `eth0 · up · 2500 Mbps`）。
3. **12px 对比度**：`preview-run.log` 逐帧最低对比度中，12px 文本最差 7.47:1（`01-warming`）、`00-startup` 8.95:1；全帧最差 6.16:1（红色 `#FF5A45` on `#101010`），均超过 WCAG AA，不判缺陷。
4. **“200ms 更新”不是硬编码**：`fnos_ui.c:5227-5232` 取 `max(fnos_data_poll_interval_ms(), CONFIG_FNOS_UI_REFRESH_MS)`；`sdkconfig:2546-2547` 为轮询 200ms、UI 100ms，六页一致。旧文档写“数据 1Hz”是文档过期，屏侧文案与当前配置相符。
5. **07-pair-code “上面的槽位”**：配对码槽位就在数字键盘正上方，左卡也写“输进下方的键盘”，两处相对键盘互相一致，不误指（右卡与槽位同排，读起来略绕，但不改语义）。
6. **10-wifi 成功/失败同构**：两者共用“3/3 · 接入结果”骨架，但标题（连接成功/没连上）、颜色（绿/红）、下一步（完成/修改密码）都不同，可区分，不判缺陷。
7. **实体详情“只读”**：`13-container-detail.png`/`13-volume-detail.png` 都有“实体详情 + 只读”chip 与“打开时快照 · 采集在线/采集不可用，保留上次读数”，只读与时点都写清楚了。
8. **容器行内未混 Exited 英文**：`02-live-p3.png`/`12-container-metrics.png` 行值已本地化为“运行中/已停止”；英文 `Exited (…)` 只存在于被 `show(row->name2,false)` 隐藏的副行，行内不判缺陷（详情路径见 P2-6）。
9. **设计系统无直写**：`fnos_ui.c` 中 `lv_color_hex`/`lv_color_make`/`LV_FONT_*` 均为 0 处，颜色全部走 `uk_c()`（`ui_kit/uk_theme.h:255`）、字体全部走 `UK_FONT_*`；间距基本走 `UK_S1..S6`（`ui_kit/uk.h:39-48`、`UK_CARD_PAD=8`、`UK_CONTENT_GAP=4`、`UK_ROW_H=44`），唯一裸间距是 `fnos_ui.c:885` 的 2px 列间距，量级小、不影响角色统一。
10. **“设置”按钮 → “设置与诊断”卡**：系统页顶栏“设置”（`02-live-p3.png`）打开 `fnos_ui.c:1748` 的“设置与诊断”，卡内含“Wi-Fi 设置”“设备配对”与诊断明细；入口名短、卡名完整，语义包含，不判缺陷。
11. **07-pair-confirm 顶栏并未重复**：顶部 rail 仍是 `fnos-nas / 系统 · 200ms 更新`（裁图 `/tmp/07-pair-confirm-top.png` 已核对）；重复发生在状态卡与左指纹卡之间（P3-1）。
12. **诊断页“采集段”保留英文模块键**：`fnos_ui.c:4891-4904` 用 `%s %s` 输出模块名 + `mod_status_cn` 状态；`cpu/mem/net/...` 是采集端 contract 键，给技术排障用，属可接受的技术标识，不判本地化缺陷。

---

## 归属不确定（采集端 contract vs 屏侧）

1. **英文告警文本**：消息由采集端/`nas/fnos-agent.py` 的 `_alerts()` 产生，屏侧只是 `alerts[].m` 原样显示（`fnos_ui.c:4941`）。归采集端做主本地化最彻底；屏侧可做关键词映射兜底。需要产品确认“告警原文是否要求保真”。
2. **Docker disabled 携带容器数组**：采集端 contract 明确 `disabled → docker=[]`（`fnos_collector.py:1121-1123`），预览桩却保留了 8 个容器（`preview.c:235` + `350-367`），且项目自测断言 disabled 时不得出现行。主责在预览桩；屏侧是否加防御（state=disabled 忽略数组）需 owner 决定。
3. **温度通道名是否翻译**：`dev/ch` 是采集端 hwmon 短名（`fnos_data.h:48`），cuktech 合同又要求“卡内保留来源名称”。设备名保留没有争议；`Package id 0/MAC/temp1/Composite` 这类通道名是否翻译，需要设计 owner 明确边界（本报告按“通道名应本地化、设备名保留”给出建议）。
4. **顶栏长 SSID 宽度**：`fnos_ui.c:948-952` 的固定宽度属布局实现，内容是“SSID 被截到 7 字符”。是否改为自适应需要布局审查确认可用宽度；文案侧只指出截断与空白并存。
5. **温度小数位**：读数 1 位、摘要/体检 0 位（74 vs 74.2）目前视为层级压缩；若设计要求全站统一精度，则应另立格式化规则，不属本 commit 文案错误。
6. **协议 v3 / 板端 known v2**：`06-diagnostics.png` “协议 v3（本机只认到 v2，新字段会忽略）”源自 `FNOS_PROTO_KNOWN 2`（`fnos_data.h:21`）与采集端新版本，属跨组件版本节奏，不是本次屏侧文案缺陷。
