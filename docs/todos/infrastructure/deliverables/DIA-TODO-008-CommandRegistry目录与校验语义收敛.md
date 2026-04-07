# DIA-TODO-008 CommandRegistry 目录与校验语义收敛

日期：2026-04-07  
任务：DIA-TODO-008  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_infra_diagnostics模块详细设计.md 6.2、6.3、6.6 已明确 CommandRegistry 负责白名单与参数 schema，但 `list_commands()` / `validate()` 的对象级边界尚未展开。
2. docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md 将 DIA-TODO-008 标为 `Blocked`，并把 `CommandCatalog`、`ValidationResult` 与“参数 schema 返回语义”列为最小解阻动作。
3. infra/include/diagnostics/DiagnosticsTypes.h 已冻结 DiagnosticsCommand、SnapshotQuery、SnapshotExportRequest、DiagnosticsSnapshotResult 与只读命令白名单，因此本轮只应补 diagnostics 私有 registry 对象，不得扩写 contracts。
4. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md 中 INF-BLK-08 已解阻，diagnostics 当前剩余阻塞应收敛到 registry 返回边界、完整 allowed_commands 参数 schema、脱敏矩阵、导出细则与桥接接口。

## 2. 外部参考

1. JSON Schema Validation Draft 2020-12 §3、§6.5.3、§9.1-§9.5 将 `required`、`enum` 等视为 assertion，把 `title`、`description`、`examples` 视为 annotation；本轮据此把 diagnostics 的参数 schema 暴露收敛为 `arg_schema_ref` + `arg_schema_summary`，避免把完整 schema 直接塞进返回对象。
   - https://json-schema.org/draft/2020-12/json-schema-validation
2. OpenAPI Specification 3.1.1 的 Schema / Parameter / Example Object 约束强调“目录发现面返回 schema 引用和摘要，例子与描述服从权威 schema”；本轮据此把 `list_commands()` 设计为 catalog discoverability 接口，而不是执行期 schema transport 通道。
   - https://swagger.io/specification/

## 3. Blocker 修复与设计结论

阻塞分类：

1. DIA-BLK-002 属于 context blocker：接口方法名与职责已知，但 registry 的返回对象与校验结果仍是未冻结边界。

最小 blocker-fix：

1. 在 diagnostics 详细设计中补齐 `CommandCatalog` 与 `ValidationResult` 的字段表。
2. 冻结 `CommandCatalog.entries` 的最小公开字段，只暴露 `arg_schema_ref` 与 `arg_schema_summary`，不直接内联完整参数 schema。
3. 把完整 `allowed_commands` 参数 schema 内容继续保留在 DIA-BLK-003，不在本轮越界补齐。

设计结论：

1. `CommandCatalog` 表示“当前 profile 下生效的只读诊断命令目录”，不是历史快照或审计日志。
2. `CommandCatalog.entries` 的最小字段固定为 `command_name`、`request_scope`、`arg_schema_ref`、`arg_schema_summary`、`read_only`。
3. `ValidationResult` 成功路径固定返回 `accepted`、`catalog_ref`、`matched_command_ref`、`schema_ref`、`normalized_command`；失败路径固定返回 `blocking_errors`、`field_paths` 与 `result_code=INF_E_DIAG_COMMAND_INVALID`。
4. `validate()` 只做白名单命中与参数 schema 静态校验，不承担 PolicyGuard、AuditBridge 或执行器副作用。
5. `field_paths` 必须使用稳定定位符，例如 `command_name`、`request_scope`、`timeout_ms`、`args[0]`，以便后续单测/contract 测试冻结负例。
6. diagnostics 继续只映射 contracts::ResultCode/ErrorInfo，不新增共享 validation 对象，不把 registry 细节推进到 contracts。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 registry 目录对象 | 后续在 infra/include/diagnostics/IDiagnosticsCommandRegistry.h 落盘 `list_commands() -> CommandCatalog` |
| 冻结 validate 成功/失败边界 | 后续在 infra/include/diagnostics/IDiagnosticsCommandRegistry.h 落盘 `validate(const DiagnosticsCommand&) -> ValidationResult` |
| 冻结 catalog discoverability 语义 | 后续新增 DiagnosticsCommandRegistryTest，覆盖只读命令目录与 schema ref/summary discoverability |
| 冻结 field_paths / blocking_errors 负例定位 | 后续新增 DiagnosticsCommandRegistryTest 与边界 contract，用稳定字段路径冻结非法参数断言 |
| 将完整 allowed_commands 参数 schema 保留为独立 blocker | DIA-TODO-013 继续受 DIA-BLK-003 约束，不在本轮提前补实现细节 |

## 5. Build 三件套

1. 代码目标：更新 docs/architecture/DASALL_infra_diagnostics模块详细设计.md，并把 DIA-TODO-008 的完成证据回写到 diagnostics TODO / infrastructure 总 TODO / worklog。
2. 测试目标：
   - 本轮 process 验证：确认 architecture doc 已具备 `CommandCatalog`、`ValidationResult`、`arg_schema_ref`、`field_paths` 锚点。
   - 后续接口验证：DIA-TODO-011 落盘后执行 DiagnosticsServiceInterfaceTest 与 DiagnosticsCommandRegistryTest。
3. 验收命令：
   - rg -n "CommandCatalog|ValidationResult|arg_schema_ref|field_paths" docs/architecture/DASALL_infra_diagnostics模块详细设计.md
   - rg -n "DIA-TODO-008|DIA-BLK-002|DIA-TODO-011" docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md

## 6. 风险与回退

1. 若后续实现把完整参数 schema 直接内联进 `CommandCatalog` 或 `ValidationResult`，会隐式越过 DIA-BLK-003，并放大 profile/config 资产的 breaking 风险。
2. 若 `ValidationResult` 吸收 policy/audit 判定字段，会越过 ADR-007 与 diagnostics / policy guard 的职责边界。
3. 若 `field_paths` 使用不稳定自然语言而非固定定位符，后续测试无法可靠冻结负例。
4. 回退策略：若后续评审认为 registry 返回边界过宽，应回退到 `arg_schema_ref` + `arg_schema_summary` 模式，而不是继续把 schema 内容扩张到接口层。