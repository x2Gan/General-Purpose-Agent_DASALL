# CLCFG-TODO-003 冻结 InstallState、ActionPlan schema 与配置文件事务语义

状态：Done
日期：2026-05-07
来源 TODO：docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md

## 1. 任务边界

1. 本任务只冻结 `InstallState` 的 v1 枚举集合、`ConfigActionPlan` 的 v1 顶层 schema，以及 `ConfigFileWriteTransaction` 的事务写入/回滚规则。
2. 本任务不提前实现 `InstallStateProbe`、`ConfigDiffPlanner`、`DaemonConfigFileStore` 或对应 tests，只为这些 Build 任务提供不可再漂移的设计输入。
3. 本任务必须消费 CLCFG-TODO-002 已冻结的 `0600 root/sudo-only` 口径，因此 action plan 与 manual followups 不再保留 group membership 变更主路径。

## 2. 当前问题与需要收敛的矛盾

1. `docs/architecture/DASALL_cli_config交互式部署配置设计.md` 已有六态 `InstallState` 表，但在本任务开始前尚未明确声明它是闭集，后续实现仍可能再发明 `PartialConfig`、`Unknown` 等并行命名。
2. `ConfigActionPlan` 的文字说明列的是顶层 `service_*` 键，但 JSON 示例仍使用嵌套 `service_actions` 对象，形成两套 schema 并存的直接冲突。
3. 事务写入段落虽然提到了临时文件、`fsync`、`rename`、备份与 rollback，但在本任务开始前没有把“事务单元是整次 apply 批次”“rename 后仍需父目录 `fsync`”“多文件失败要整批回滚”写成固定序列。
4. 外部参考也支持把目录 `fsync` 与原子 rename 写成显式规则：Linux `rename(2)` 保证同文件系统内目标路径原子替换，而 `fsync(2)` 明确指出，仅对文件 `fsync` 并不能保证目录项持久化，目录也需要显式 `fsync`。

## 3. 冻结结论

### 3.1 InstallState v1 是闭集

1. `InstallState` v1 只允许以下六种状态：`FreshInstall`、`BootstrapPending`、`ConfiguredStopped`、`ConfiguredRunning`、`Drifted`、`Unsupported`。
2. 实现期不得再新增 `PartialConfig`、`Unknown`、`NeedsGroupRelogin` 等平行命名；若后续需要扩展，必须重新冻结文档与测试矩阵。
3. `FreshInstall` 表示 canonical 文件缺失或未初始化；`BootstrapPending` 表示文件已出现，但 validate / secret / service 尚未闭环；`ConfiguredStopped` 与 `ConfiguredRunning` 只用于配置有效的稳定部署；`Drifted` 负责承接“可读但不一致/validate 失败”的部署；`Unsupported` 用于 systemd 或 install payload 缺失等环境不满足场景。
4. `state_before` 与 `state_after_expected` 只能从这六态里取值。

### 3.2 ConfigActionPlan v1 顶层 schema

1. `ConfigActionPlan` v1 顶层键固定为：
   - `schema_version`
   - `state_before`
   - `state_after_expected`
   - `file_writes`
   - `secret_writes`
   - `service_validate_requested`
   - `service_reload_required`
   - `service_restart_required`
   - `service_start_requested`
   - `service_enable_requested`
   - `manual_followups`
   - `blocked_actions`
2. `manual_followups` 与 `blocked_actions` 在 v1 固定为 string 数组。
3. `service_actions` 与 `operator_access_actions` 不再作为 v1 key；后续实现只能消费顶层 `service_*`，不能保留第二套嵌套 service schema。
4. `file_writes` 条目至少保留 `path`、`operation`、`requires_root`、`restart_required`、`changed_keys`；`secret_writes` 条目至少保留 `ref`、`operation`、`runtime_verification`。

### 3.3 ConfigFileWriteTransaction v1 事务语义

1. 事务单元是整次 apply 批次，而不是单文件 ad hoc 覆盖。
2. 对每个目标文件都必须走“同目录临时文件 -> 文件 `fsync` -> 父目录 `fsync` -> atomic rename -> 父目录再次 `fsync`”的固定顺序。
3. 所有目标文件替换完成后，统一执行一次 `dasall-daemon --validate-only`；不能一边写一个文件一边立即启动/重载服务。
4. 任一步骤失败时，都必须把本批次已触及的 canonical 文件整批回滚到 `.last-known-good`，并标记 `apply_failed_rolled_back`。
5. 备份只允许保留有限数量的 `.last-known-good` 快照，且不得包含 secret 明文。

## 4. 对后续 Build 的直接约束

1. `InstallStateProbe` 只能输出六态闭集，不再新增第七种本地状态名。
2. `ConfigDiffPlanner` 只能输出顶层 `service_*` 键；不得再序列化 `service_actions` 或 `operator_access_actions`。
3. `DaemonConfigFileStore` 必须把 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` 当作同一 apply 批次处理，失败时整批回滚。
4. 所有 focused tests 都应围绕六态矩阵、plan 顶层键和 rollback/`apply_failed_rolled_back` 事实编写，而不是围绕临时字段名。

## 5. 验证口径

1. 设计验收使用以下命令：

   `rg -n "FreshInstall|BootstrapPending|ConfiguredStopped|ConfiguredRunning|Drifted|Unsupported|file_writes|secret_writes|service_validate_requested|service_reload_required|service_restart_required|service_start_requested|service_enable_requested|manual_followups|blocked_actions|rename|fsync" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/cli/deliverables/CLCFG-TODO-003-state-plan-transaction冻结.md`

2. 通过标准：
   - 六态 `InstallState` 在设计文档与 deliverable 中形成唯一闭集口径。
   - action plan 只保留一套顶层 `service_*` schema，不再出现嵌套 `service_actions`。
   - 事务写入明确包含“同目录临时文件、文件/目录 `fsync`、atomic rename、validate-only、整批 rollback”。