# ACC-TODO-003 override 与 diagnostics 入口 schema 收敛

日期：2026-04-23  
任务：ACC-TODO-003  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 将 ACC-TODO-003 定义为补齐 `OverrideSourceFact`、`DiagnosticsSelectorFact`、artifact size / selector schema，用于解阻 ACC-BLK-003，并作为 ACC-TODO-016 / 035 的前置条件。
2. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 在本轮前只有“override 只允许受控入口”“diagnostics pull 需要 allow proof”的总原则，但没有把 Access 私有事实对象与 infra 侧真实 typed 对象对齐，也仍保留了基于 `trace_id/session_id` 的宽泛 selector 说法。
3. [docs/architecture/DASALL_infra_config模块详细设计方案.md](/home/gangan/DASALL/docs/architecture/DASALL_infra_config模块详细设计方案.md) 已冻结 `ConfigPatch` v1：`patch_id/source_kind/source_id/actor/target_scope/base_version/reason_code/expires_at/patches`，并明确 `runtime_override` 只接受结构化 patch，不接受自由字典。
4. [infra/include/diagnostics/DiagnosticsTypes.h](/home/gangan/DASALL/infra/include/diagnostics/DiagnosticsTypes.h) 与 [infra/include/diagnostics/IDiagnosticsService.h](/home/gangan/DASALL/infra/include/diagnostics/IDiagnosticsService.h) 已落盘 `SnapshotQuery{snapshot_id}`、`SnapshotExportRequest{snapshot_id,target,format,target_ref}` 与 `IDiagnosticsService::get_snapshot/export_snapshot`，说明 diagnostics 侧已经存在稳定 typed 入口，而不是仍停留在 placeholder。
5. [docs/architecture/DASALL_infra_diagnostics模块详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_infra_diagnostics模块详细设计.md) 已冻结 diagnostics v1 的导出矩阵：`LocalFile + Json` 为唯一默认可成功路径，`TextArchive` 必拒绝，`RemoteUpload` 默认关闭且必须 exact-match `allowed_targets`，并由 `infra.diagnostics.max_artifact_bytes` 限制导出产物大小。

## 2. 外部参考

1. OWASP Access Control 强调特权操作必须走集中式授权与最小权限路径；高风险资源不应暴露宽松、可猜测或可拼装的 selector / transport。该原则直接支持本轮把 diagnostics pull 收口为 `snapshot_id` only，并要求 override / diagnostics 继续走显式 allow proof 和 deny-by-default。
   - 参考：<https://owasp.org/www-community/Access_Control>

## 3. 设计结论

1. Access 不再定义第二套 override 输入 schema；`runtime_override` 入口 payload 直接复用 infra/config 已冻结的 `ConfigPatch` v1。
2. Access 私有 `OverrideSourceFact` 只保留 `ConfigPatch` 的稳定元数据和 `key_path/op` 摘要，不把 patch value、自由 JSON、query string、CLI 原始参数或环境变量明文带入 policy / audit。
3. Access v1 `diagnostics.pull` 的唯一公开 selector 固定为 `snapshot_id`；`trace_id`、`session_id`、`request_id` 继续只作为日志/审计关联上下文，不作为 pull selector。
4. diagnostics 导出路径直接对齐 `SnapshotExportRequest`：v1 仅 `LocalFile + Json + local://diagnostics/<artifact_name>.jsonl` 保证可成功；`TextArchive` 必拒绝，`RemoteUpload` 继续默认关闭，只有 infra/diagnostics 明确开启并 exact-match allow-list 时才允许。
5. Access 侧只消费 diagnostics 稳定元数据，如 `snapshot_id/export_id/size_bytes/checksum/created_at`；不把 artifact bytes 内联回普通 entry 响应体，也不发明第二套 transport。

## 4. 边界 / 职责

| 对象 | 边界与职责 | 不允许事项 |
|---|---|---|
| `ConfigPatch` | infra/config 拥有的 typed override 输入对象；负责表达 patch body 与来源元数据 | Access 重定义第二套 override JSON/body；普通业务入口直接拼 patch |
| `OverrideSourceFact` | access private policy/audit 投影；只保存 override 来源、scope、TTL、path/op 摘要 | 保存 patch value 明文；替代 ConfigCenter 做 merge 或白名单判定 |
| `SnapshotQuery` | infra/diagnostics 拥有的 snapshot 查询对象；v1 只有 `snapshot_id` | Access 再发明 `trace_id/session_id` 公开查询字段 |
| `SnapshotExportRequest` | infra/diagnostics 拥有的导出请求对象；表达 `snapshot_id/target/format/target_ref` | Access 自造另一套 artifact export body 或返回 inline bytes |
| `DiagnosticsSelectorFact` | access private selector / gate 投影；表达 `snapshot_id`、request mode 与导出目标事实 | 承载 snapshot 内容；替代 diagnostics 的 export gate |

## 5. 数据 / 接口说明

### 5.1 `OverrideSourceFact`

1. 最小字段面冻结为：`override_id`、`source_kind`、`source_ref`、`actor_ref`、`target_scope`、`base_version`、`reason_code`、`expires_at`、`requested_paths`、`requested_ops`。
2. `requested_paths` 只保留 `ConfigPatch.patches[*].key_path` 摘要；`requested_ops` 只保留 `replace/remove` 摘要；任何 `value` 明文都不进入 Access policy/audit。
3. Access 入口只校验元数据完整性、来源受控性与 allow proof 前置条件；路径白名单、高风险键“只收紧不放宽”和最终 merge 语义继续归 ConfigCenter / profiles validator。

### 5.2 `DiagnosticsSelectorFact`

1. 最小字段面冻结为：`selector_kind`、`selector_value`、`request_mode`、`export_target(optional)`、`export_format(optional)`、`target_ref(optional)`。
2. `selector_kind` 在 v1 固定为 `snapshot_id`；`selector_value` 直接对齐 `SnapshotQuery.snapshot_id`。
3. `request_mode` 只允许 `snapshot_get` 或 `snapshot_export`。`snapshot_export` 时必须附带完整 `SnapshotExportRequest` 四元组，缺失任一字段都按 fail-closed 处理。

### 5.3 artifact size / target / transport

1. `snapshot_get` 只走 `IDiagnosticsService::get_snapshot(const SnapshotQuery&)`；Access 不提供基于 `trace_id/session_id/request_id` 的模糊查询面。
2. `snapshot_export` 只走 `IDiagnosticsService::export_snapshot(const SnapshotExportRequest&)`；v1 唯一默认可成功路径是 `LocalFile + Json + local://diagnostics/<artifact_name>.jsonl`。
3. `RemoteUpload` 是否真正执行由 infra/diagnostics 自身 gate 决定；Access 最多只把 request 事实送入授权链并要求 allow proof，不替代 diagnostics 做 target allow-list 匹配。
4. diagnostics 导出体积上限继续以 `infra.diagnostics.max_artifact_bytes` 为准；Access 不接管 size 判定逻辑，只保证下游结果不会被重新包装成自由字节流响应。

## 6. 流程 / 时序

1. override 路径：入口壳层认证主体 -> 归一化 `ConfigPatch` -> 投影 `OverrideSourceFact` -> 构造 `access.runtime_override.apply` 授权查询 -> 明确 allow 后才调用 ConfigCenter `apply_override`。
2. diagnostics snapshot 读取：入口壳层认证主体 -> 校验 `SnapshotQuery.snapshot_id` -> 投影 `DiagnosticsSelectorFact(request_mode=snapshot_get)` -> 构造 `access.diagnostics.pull` 授权查询 -> allow 后调用 `get_snapshot()`。
3. diagnostics 导出：在 snapshot pull 基础上继续校验 `SnapshotExportRequest` -> 投影 `request_mode=snapshot_export` 与 target/format/target_ref -> allow 后调用 `export_snapshot()` -> 只返回稳定元数据。
4. 任一环节缺失 allow proof、TTL、`snapshot_id`、`target_ref` 或命中未冻结 selector / transport 时，都必须 fail-closed，并写 `access.runtime_override.denied` 或 `access.diagnostics.pull` 审计事件。

## 7. 文件范围

1. 设计真值源更新在 [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 6.7、6.11.4、6.11.5、6.12、11、12。
2. 上游 typed schema 证据来自 [docs/architecture/DASALL_infra_config模块详细设计方案.md](/home/gangan/DASALL/docs/architecture/DASALL_infra_config模块详细设计方案.md)、[docs/architecture/DASALL_infra_diagnostics模块详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_infra_diagnostics模块详细设计.md)、[infra/include/diagnostics/DiagnosticsTypes.h](/home/gangan/DASALL/infra/include/diagnostics/DiagnosticsTypes.h)、[infra/include/diagnostics/IDiagnosticsService.h](/home/gangan/DASALL/infra/include/diagnostics/IDiagnosticsService.h)。
3. 本任务交付物落于 [docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md)。
4. TODO / blocker / 证据回写落于 [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 与 [docs/worklog/DASALL_开发执行记录.md](/home/gangan/DASALL/docs/worklog/DASALL_开发执行记录.md)。

## 8. Design -> Build 映射

| Design 项 | 后续 Build 落点 |
|---|---|
| `OverrideSourceFact` 字段与来源校验规则 | `access/include/AccessTypes.h`、`access/src/AccessPolicyGate.cpp` |
| `DiagnosticsSelectorFact` 与 `snapshot_id` only selector | `access/include/AccessTypes.h`、`access/src/AccessPolicyGate.cpp` |
| diagnostics export gate / metadata only 返回语义 | `access/src/AccessPolicyGate.cpp`、`access/src/AccessObservabilityBridge.cpp`、`tests/unit/access/AccessPolicyOverrideGateTest.cpp`、`tests/integration/access/AccessObservabilityIntegrationTest.cpp` |

## 9. Build 三件套

1. 代码目标：无；本任务只完成 Access 侧 override / diagnostics schema 与 deny-by-default 规则冻结，不修改 access 生产代码。
2. 测试目标：通过文档与已落盘 infra typed 对象检索，确认 `ConfigPatch`、`SnapshotQuery`、`SnapshotExportRequest`、`OverrideSourceFact`、`DiagnosticsSelectorFact`、`snapshot_id` only selector 和 artifact size / target 规则形成唯一口径。
3. 验收命令：
   - `rg -n "ConfigPatch|OverrideSourceFact|DiagnosticsSelectorFact|SnapshotQuery|SnapshotExportRequest|snapshot_id|target_ref|max_artifact_bytes|allow proof" docs/architecture/DASALL_access子系统详细设计.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md infra/include/diagnostics/DiagnosticsTypes.h`

## 10. 风险与回退

1. 如果后续实现重新接受自由 patch body、`trace_id/session_id` 公共 selector、inline artifact bytes 或 remote target 的 prefix/wildcard 匹配，会直接破坏本轮冻结的最小权限边界；必须回退到 `snapshot_id` only + exact-match target + deny-by-default。
2. 本任务只冻结 access 侧 schema 和 gate，不等价于 `AccessPolicyGate`、`AccessObservabilityBridge`、diag export 响应路径已经实现；ACC-TODO-016 与 ACC-TODO-035 仍需用单测 / 集成测试证明“未冻结 selector 被拒绝、审计字段稳定、远程导出继续受双重 gate 约束”。