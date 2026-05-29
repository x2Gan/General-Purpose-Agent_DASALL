# KeySubsystemLoggingFieldMatrix

关联任务：INF-LOG-SYS-FIX-001 / BLK-INF-LOG-009 / INF-LOG-FIX-010  
最近更新时间：2026-05-29
适用阶段：key subsystem production logging field freeze

## 1. 目标

本文件冻结 DASALL 当前 key subsystem production logging 字段矩阵，统一以下口径：

1. cognition、memory、knowledge、runtime、services 五个 owner 子系统允许进入 ordinary log 的 owner-safe attrs。
2. 各子系统跨链路 correlation 字段、禁止字段、redaction rule 与 audit split。
3. `logging-installed-proof.json.subsystems` 与 `logging-runtime-proof.json.subsystems` 在后续 installed/package 任务中的最小字段要求。
4. `INF-LOG-SYS-GATE-001~005` 在后续 `INF-LOG-SYS-FIX-002~007` 与 `INF-LOG-FIX-010~011` 中分别绑定哪些 build / installed 验证出口。

本文件是 `INF-LOG-FIX-010` 的 business chain logging matrix。当前只冻结 key owner 子系统；`llm`、`profiles`、`tools` 继续复用 `docs/ssot/LoggingProductionAcceptanceMatrix.md` 的 shared logging chain 与 correlation 规则。本轮 owner 验收只接受 local installed authoritative evidence，不使用 qemu / kvm。

## 2. 输入证据

### 2.1 本地代码事实

1. cognition：`cognition/src/observability/CognitionTelemetry.cpp` 当前会为 `stage.completed`、`stage.failed`、`response.degraded` 组装 `TelemetryEvent`，但 `InfraTelemetrySink::emit_log()` 仍为空；现有 context/field 面包括 `request_id`、`goal_id`、`profile_id`、`stage`、`trace_id`、`model_hint_tier`、`decision_kind`、`confidence`、`selected_node_id`、`error_code`、`fallback_mode`、`payload_excerpt` 等。
2. memory：`memory/src/observability/MemoryObservability.cpp` 当前已通过 `ILogger` 发射 module=`memory` 的 `LogEvent`，且 `make_log_attrs()` 已形成 owner-safe allowlist；`MemoryManager` 会发射 lifecycle telemetry，`WritebackCoordinator` 会对 turn validation fail-fast 发射 `writeback.failed`。
3. knowledge：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 当前的 `make_knowledge_log_event()` 已固定 module=`knowledge` 的 primary/fallback logging attrs，并新增 `session_id` 作为 diagnostics selector；`KnowledgeProductionLoggingIntegrationTest` 已证明 success / failure / invalid-payload fallback 可落到 runtime.log，并可按 `session_id` materialize query artifact。
4. runtime：`RuntimeTelemetryBridge` 继续向 `RuntimeEventBus` 发射 `RuntimeEventEnvelope`，而 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 现已通过 `RuntimeLoggingBridge` 把 `transition` / `budget.reject` / `recovery.reject` / `safe_mode` envelope 投影为 module=`runtime` 的 `LogEvent`；`audit=true` 事件在 ordinary log 中只保留 `audit_ref_pending` marker，不回写完整 audit payload，`RuntimeLoggingBridgeTest` 与 `RuntimeProductionLoggingIntegrationTest` 已证明 runtime.log 落盘，`RuntimeHealthMaintenanceIntegrationTest` 守住 event-bus/backpressure 相邻回归。
5. services：`services/include/ServiceLiveComposition.h` 现已新增 `logger` seam，`ServiceLiveComposition` 会在 logger 存在时创建 `ServiceLoggingBridge` 并下发到 execution/data lane；`ServiceLoggingBridgeTest`、`CapabilityServicesLoggingIntegrationTest` 与迁移后的 `CapabilityServicesSmokeIntegrationTest` 已证明 execute/query/catalog route attrs 进入 module=`services` 的 structured logging sink，而 `request ledger` 只保留 fixture / fallback discoverability 语义。

### 2.2 外部参考

1. OpenTelemetry Logs Data Model：log record 需要区分 top-level trace context 与 per-event `Attributes`；`TraceId`/`SpanId`/`Resource` 作为稳定 correlation/source 字段，event-specific request context 放入 attributes。对应到 DASALL，本轮固定 `trace_id` 优先于其他 correlation anchors，模块/route/stage 继续停留在 attrs，而不是扩写新的全局 owner 字段。
2. OWASP Logging Cheat Sheet：日志应覆盖 when/where/who/what，但不得直接记录 session identification values、access tokens、passwords、secrets、connection strings、file paths 等高敏感原文。对应到 DASALL，本矩阵只允许 owner-safe correlation 与枚举/计数/布尔摘要字段进入 ordinary log，`query/body`、`payload_excerpt`、`payload_json` 等原文必须脱敏或禁止落盘。

## 3. 全局冻结规则

| 维度 | 冻结结论 | 非外推 / owner 约束 |
|---|---|---|
| correlation 优先级 | shared correlation anchor 固定为 `trace_id -> request_id -> session_id -> task_id/turn_id/checkpoint_id -> unknown`；`stage`、`component`、`route_kind`、`operation_name` 等继续停留在 attrs | `session_id` 在本矩阵中只表示 DASALL 内部会话相关键，不代表 HTTP cookie、socket token 或其他认证 session identification values |
| shared allowlist 原则 | ordinary log 只允许写入 `module`、`event_name`、`profile_id`、`outcome`、枚举/布尔/计数摘要字段，以及上表 correlation anchors | 不允许把 subsystem 私有对象、完整 payload、全文摘要或高基数字段直接透传到 attrs |
| shared forbidden 集合 | raw prompt、`clarification_question`、`response_summary`、`payload_excerpt`、context body、`query/body`、`payload_json`、password、token、secret、connection string、raw file path、audit actor/action/target/outcome/side_effects | 命中 allowlist 的字段值若出现 secret/token/password/auth 文本模式，仍必须按 `RedactionFilter` 继续 redacted |
| audit split | `audit=true` 事件与 high-risk action 继续由 audit owner 持有 persistence；ordinary log 只允许保留 `audit_ref_pending`、`evidence_ref`、`evidence_kind`、`audit_trace_id`、`audit_task_id` 这组 frozen anchor attrs | logging 不能把 audit payload join 回普通日志，也不能把 route hint 误写成审计持久化已完成 |
| installed artifact summary | 后续 installed/package 任务必须在 `logging-installed-proof.json.subsystems` 与 `logging-runtime-proof.json.subsystems` 下输出每个子系统的 `record_count`、`event_names`、`correlation_fields_present`、`redaction_proof`、`query_proof_ref`、`flush_observed`、`evidence_level` | 当前 `INF-LOG-SYS-FIX-001` 只冻结字段，不宣称这些 artifact 已产生；真正 L4 owner 证据留给 `INF-LOG-FIX-011` / `INF-LOG-SYS-FIX-007` |
| 证据边界 | 本矩阵提供 L1/L2 field freeze 与 docs contract；同一 live composition 落盘/查询属于 L3，fresh installed package proof 属于 L4 | local installed authoritative evidence 是当前 owner 验收上限；qemu / kvm / autopkgtest / soak 继续留给更高层 handoff |

## 4. 关键子系统字段矩阵

| 子系统 | ordinary log allowlist | 必须脱敏或禁止写入 | correlation anchors | build / installed gate |
|---|---|---|---|---|
| cognition | `event_name`、`profile_id`、`stage`、`goal_id`、`trace_id`、`request_id`、`model_hint_tier`、`decision_kind`、`confidence`、`selected_node_id`、`clarification_needed`、`result_code`、`error_code`、`error_stage`、`retryable`、`safe_to_replan`、`fallback_mode` | `clarification_question`、`response_summary`、raw `candidate_scores`、raw prompt、token、raw `payload_excerpt`、完整 `error_message`、raw `audit_refs.artifact_refs` | `trace_id`、`request_id`、`goal_id`、`stage` | `INF-LOG-SYS-GATE-002`；`CognitionProductionLoggingIntegrationTest`；installed summary 额外要求 `stage_set` 与 `degraded_event_total` |
| memory | `event_name`、`request_id`、`session_id`、`trace_id`、`stage`、`profile_id`，以及 `warning_count`、`warning`、`warning_codes`、`result_code`、`failure_reason`、`fact_count`、`experience_count`、`conflict_count`、`partial`、`retryable`、`turn_id`、`summary_id`、`storage_backend`、`vector_enabled`、`auto_schedule`、`lifecycle_state`、maintenance 请求/执行/计数字段、conflict 摘要字段 | raw context body、retrieval payload、summary text、embedding/vector payload、secret refs、store path、未经过 allowlist 的任意 field passthrough；`failure_reason` 只允许来自 bounded guard/config reason，不承载 raw payload | `trace_id`、`request_id`、`session_id`、`stage` | `INF-LOG-SYS-GATE-002`；`MemoryProductionLoggingIntegrationTest`、`MemoryObservabilityBridgeTest`；installed summary 额外要求 `write_back_event_total`、`maintenance_event_total`、`lifecycle_event_total`、`failed_event_total` 与 `query_proof_ref` |
| knowledge | `request_id`、`session_id`、`component`、`snapshot_id`、`profile_id`、`query_kind`、`retrieval_mode`、`outcome`、`vector_backend_ready`、`warning_count`、`warning_summary`、`selected_corpora`、`sparse_hit_count`、`dense_hit_count`、`reason_codes`、`error_category`、`telemetry_path` | raw retrieve query、raw ingest body、corpus document text、未摘要的 warning payload、secret-bearing reason detail、fallback path 泄漏的原始 `query/body` | `request_id`、`session_id`、`snapshot_id`、`profile_id`；若后续 runtime 注入 `trace_id`，只能作为增强 correlation，不能替换当前主键 | `INF-LOG-SYS-GATE-002`；`KnowledgeProductionLoggingIntegrationTest`、`KnowledgeProductionTelemetryIntegrationTest`；installed summary 额外要求 `telemetry_path_set`、`fallback_event_total` 与 `query_proof_ref` |
| runtime | `event_name`、`category`、`severity`、`request_id`、`session_id`、`trace_id`、`turn_id`、`checkpoint_id`、`runtime_instance_id`、`from_state`、`to_state`、`violation`、`budget_type`、`executed_action`、`final_runtime_state`、`previous_mode`、`target_mode`、`action`、`selected_fallback`、`error_code`、`audit_ref_pending` | raw recovery payload、checkpoint body、prompt/context data、task output body、完整 audit payload；`audit=true` 事件不得把 actor/action/target/outcome/side_effects 写回 ordinary log | `trace_id`、`request_id`、`session_id`、`turn_id`、`checkpoint_id`、`runtime_instance_id` | `INF-LOG-SYS-GATE-003`；`RuntimeLoggingBridgeTest`、`RuntimeProductionLoggingIntegrationTest`、`RuntimeHealthMaintenanceIntegrationTest`；installed summary 额外要求 `transition_event_total`、`audit_pending_event_total` 与 `flush_observed` |
| services | `request_id`、`capability_id`、`target_id`、`request_kind`、`operation_name`、`route_kind`、`adapter_id`、`trust_class`、`availability_state`、`transport_outcome`、`provider_status_code`、`latency_ms`、`side_effect_count`、`evidence_ref_count` | raw `payload_json`、catalog/result body、adapter endpoint secrets、auth headers、未脱敏 side effect 原文；`request ledger` 不得再充当 production logging 证据 | `request_id`、`capability_id`、`target_id`，以及 trace bridge 可选提供的 trace context | `INF-LOG-SYS-GATE-004`；`ServiceLoggingBridgeTest`、`CapabilityServicesLoggingIntegrationTest`、`CapabilityServicesSmokeIntegrationTest`；installed summary 额外要求 `execution_event_total`、`query_event_total`、`catalog_event_total` 与 `request_ledger_replaced=true` |

## 5. Installed artifact 字段冻结

### 5.1 `logging-installed-proof.json.subsystems`

每个子系统节点至少包含：

1. `record_count`
2. `event_names`
3. `correlation_fields_present`
4. `redaction_proof`
5. `query_proof_ref`
6. `flush_observed`
7. `evidence_level`

### 5.2 `logging-runtime-proof.json.subsystems`

每个子系统节点至少包含：

1. `record_count`
2. `event_names`
3. `correlation_fields_present`
4. `redaction_proof`
5. `query_proof_ref`
6. `flush_observed`
7. `evidence_level`
8. 子系统特定摘要字段：
   - cognition：`stage_set`、`degraded_event_total`
   - memory：`write_back_event_total`、`maintenance_event_total`、`lifecycle_event_total`、`failed_event_total`
   - knowledge：`telemetry_path_set`、`fallback_event_total`
   - runtime：`transition_event_total`、`audit_pending_event_total`
   - services：`execution_event_total`、`query_event_total`、`catalog_event_total`、`request_ledger_replaced`

## 6. Gate 回链

| Gate ID | 冻结要求 | 绑定任务 |
|---|---|---|
| `INF-LOG-SYS-GATE-001` | 字段矩阵已列出 allowlist、forbidden fields、correlation、audit split 与 installed artifact 字段 | `INF-LOG-SYS-FIX-001` |
| `INF-LOG-SYS-GATE-002` | cognition / memory / knowledge 的事件必须先过子系统 allowlist 与 `RedactionFilter`，success/failure/degraded/fallback 全部有 focused 覆盖 | `INF-LOG-SYS-FIX-002`、`INF-LOG-SYS-FIX-003`、`INF-LOG-SYS-FIX-004` |
| `INF-LOG-SYS-GATE-003` | runtime logging bridge 只投影 `RuntimeEventEnvelope` 的 operational attrs，不阻塞 event bus，不改写 `audit=true` owner | `INF-LOG-SYS-FIX-005` |
| `INF-LOG-SYS-GATE-004` | services 正式日志字段来自 `ServiceLoggingBridge`；`request ledger` 只保留 fixture / fallback discoverability 语义 | `INF-LOG-SYS-FIX-006` |
| `INF-LOG-SYS-GATE-005` | 五个 key subsystem 必须在同一 live composition 下落到 shared sink、可 query、可 flush，并产出 `logging-installed-proof.json.subsystems` / `logging-runtime-proof.json.subsystems` | `INF-LOG-SYS-FIX-007`、`INF-LOG-FIX-010`、`INF-LOG-FIX-011` |

## 7. 完成判定

当且仅当以下条件成立时，才允许将 `INF-LOG-SYS-FIX-001` 视为完成：

1. 本文件已明确五个 key subsystem 的 ordinary log allowlist、forbidden fields、correlation anchors、audit split 与 installed artifact 字段。
2. `docs/architecture/DASALL_infra_logging模块详细设计.md` 已回链本文件，并明确 cognition / memory / knowledge / runtime / services 的 owner boundary。
3. `docs/todos/DASALL_子系统查漏补缺专项记录.md` 已将 `INF-LOG-SYS-FIX-001` 标记为 Done，并将 `BLK-INF-LOG-009` 转为 Closed。
4. `KeySubsystemLoggingFieldMatrixContractTest` 已能同时扫描本文件、logging 详设、系统总账与工作日志，并给出可复验的二值结果。