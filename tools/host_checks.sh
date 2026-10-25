#!/usr/bin/env bash
# Host-only checks. Callback receives a description followed by an argv vector.
fnos_host_checks() {
  export PYTHONDONTWRITEBYTECODE=1
  "$@" "字段契约（板子读的字段 NAS 是否都在）" python3 nas/fpk/contract_check.py
  "$@" "向导表单 schema" python3 nas/fpk/wizard_check.py nas/fpk/nasscreencompanion
  "$@" "证书指纹三方一致（固件 / 服务端 / openssl）" bash tools/certcheck/run.sh
  "$@" "连接失败原因标签跨文件契约" python3 tools/reason_check.py
  "$@" "管理页 ↔ 服务端接口字段名" python3 nas/fpk/api_check.py
  "$@" "权限分类器（矩阵的每一格都来自它）" python3 nas/fpk/perm_check.py
  "$@" "主机预览（四条版面审计 + 配对流程断言）" bash tools/preview/run.sh "${FNOS_PREVIEW_OUT:-out}"
  "$@" "滚动条与卡片间距" node tools/preview/scroll_gap.js "tools/preview/${FNOS_PREVIEW_OUT:-out}"
  "$@" "Python 开发模式零警告（未关闭的 socket/文件）" python3 nas/fpk/devmode_check.py 30
  "$@" "截断真的发生了（六段各喂 50 条）" python3 nas/fpk/collector_check.py
  "$@" "满载状态帧能被固件的 parse_status() 吃下" bash tools/parsecheck/run.sh
  "$@" "停止失败时不删记录、不写配置（M21/M25/M26）" bash nas/fpk/test_process_lifecycle.sh
  "$@" "配网提交代次、迟到事件与 RPC 失败" python3 tools/test_wifi_submission.py
  "$@" "报告入口错误传播与未完成状态（纯桩）" python3 -W error::ResourceWarning tools/test_report_entry.py
  "$@" "空原因集合与缺预览配置负控" python3 tools/test_host_gate_failures.py
  "$@" "发布配置恢复与工具失败边界（无设备）" python3 -W error::ResourceWarning tools/test_gate_failures.py
  "$@" "Manifest 包身份与字段形状负控" python3 -W error::ResourceWarning tools/test_manifest_failures.py
  "$@" "PID 写失败与卸载收尾（无 NAS）" python3 nas/fpk/test_lifecycle_failures.py
  "$@" "API 与向导规则负控（无 NAS）" python3 nas/fpk/test_validation_gates.py
  "$@" "检查器故障收尾（无 NAS）" python3 -W error::ResourceWarning nas/fpk/test_tool_cleanup.py
  "$@" "配对状态边界（无网络）" python3 tools/test_pair_state.py
  "$@" "配对存储读取故障（无设备）" python3 tools/test_pair_nvs.py
  "$@" "配对存储屏障与模拟重启（无设备）" python3 tools/test_pair_nvs_transaction.py
  "$@" "串口采集复位与收尾（模拟串口）" python3 tools/test_serial_capture.py
  "$@" "SSH 与远程安装脚本（纯桩）" python3 -W error::ResourceWarning nas/fpk/test_ssh_scripts.py
  "$@" "配对持久化与锁失败（无设备）" python3 tools/test_pair_commit.py
  "$@" "TLS 探针重入与诊断缓冲（无网络）" python3 tools/test_tls_probe_guard.py
  "$@" "UI 原子请求与刷新定时器重试（无设备）" python3 tools/test_ui_requests.py
  "$@" "启动定时器失败与资源回收（无设备）" python3 tools/test_main_timers.py
  "$@" "照片解码预算与坏图拒绝（匿名PNG）" python3 -W error::ResourceWarning tools/test_photo_check.py
  "$@" "新鲜度证据缺失拒绝（匿名临时文件）" python3 tools/test_evidence_gates.py
  "$@" "证书辅助信息真实 mbedTLS 主机解析（无 NAS）" python3 tools/test_cert_info.py
  "$@" "Wi-Fi 存储屏障、读取拒绝与模拟重启（无设备）" python3 tools/test_wifi_store.py
  "$@" "串口命令边界、数字溢出与口令行清理（无设备）" python3 tools/test_wifi_cli.py
  "$@" "配对诊断缓冲任务边界（无设备）" python3 tools/test_pair_diag.py
  "$@" "配对配置快照与锁失败（无设备）" python3 tools/test_pair_snapshot.py
  "$@" "升级回退点与恢复标志（无 NAS）" python3 nas/fpk/test_upgrade_safety.py
  "$@" "全地址监听预检与未知拒绝（无 NAS）" python3 nas/fpk/test_port_preflight.py
  "$@" "历史与实时合并、游标失效及端点重置（无设备）" python3 tools/test_history_merge.py
  "$@" "历史回填时间边界（无设备）" python3 tools/test_history.py
  "$@" "性能样本完整性（无设备）" python3 tools/test_perf_samples.py
  "$@" "大小栈帧与未知调整负控" python3 tools/test_stack_frames.py
  "$@" "拍屏错误处理（模拟，无设备）" python3 tools/test_page_shot.py
  "$@" "TLS 故障拒绝降级、令牌并发与安全日志" python3 nas/fpk/test_server_safety.py
  "$@" "遥测发布所有权、编码复用与停止屏障（无 NAS）" python3 nas/fpk/test_telemetry_publication.py
  "$@" "令牌 fail-closed 安装/升级/故障路径（M31）" python3 nas/fpk/test_install_token.py
  "$@" "Release 断言与坏夹具拒绝（无设备）" bash tools/preview/test_release_asserts.sh
  "$@" "预览输出目录守卫 + 分支变量按阶段隔离（M36/M57）" bash tools/preview/test_run_guard.sh
  "$@" "MPT 温度超时与只读协议" python3 nas/fpk/test_mpt_temperature.py
  "$@" "磁盘与容器指标质量" python3 nas/fpk/test_metrics.py
  "$@" "TLS 接收超时隔离" python3 nas/fpk/tls_accept_check.py
  "$@" "UI 数据读取锁竞争与恢复" python3 tools/test_data_lock.py
  "$@" "安装包元数据负控" python3 nas/fpk/test_release_metadata.py
  "$@" "安装包、manifest 与 README 一致" python3 nas/fpk/release_metadata.py
}
