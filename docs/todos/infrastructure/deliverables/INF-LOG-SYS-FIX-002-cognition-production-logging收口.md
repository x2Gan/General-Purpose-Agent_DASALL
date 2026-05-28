# INF-LOG-SYS-FIX-002 cognition production logging 收口

日期：2026-05-28  
关联任务：INF-LOG-SYS-FIX-002 / INF-LOG-FIX-010 / INF-LOG-SYS-GATE-002  
关联冻结：`docs/ssot/KeySubsystemLoggingFieldMatrix.md`、`docs/architecture/DASALL_infra_logging模块详细设计.md` 6.10.13 / 6.10.14

## 1. 目标与约束

1. 按 `KeySubsystemLoggingFieldMatrix` 为 cognition 建立正式 logger seam，不再让 `InfraTelemetrySink::emit_log()` 停留在空实现。
2. `TelemetryEvent` 只能投影 owner-safe attrs；`raw prompt`、token、`payload_excerpt`、`response_summary`、`clarification_question`、`candidate_scores` 不得进入 ordinary log。
3. logger seam 不能改变既有 cognition factory overload，也不能把 audit owner persistence、metrics bridge、trace bridge 混进 logging payload。
4. 当前结论只到 L2 build-tree focused evidence；installed/package authoritative evidence 继续留给 `INF-LOG-SYS-FIX-007` / `INF-LOG-FIX-011`，本轮不使用 qemu / kvm。

## 2. Design -> Build 映射

| Design 结论 | Build 落点 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| cognition runtime dependency 必须显式持有 logger seam | 在 `CognitionRuntimeDependencies`、`RuntimeDependencySet` 与 runtime_support composition 间透传 live logger | `cognition/include/CognitionDependencies.h`、`runtime/include/RuntimeDependencySet.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`runtime/src/AgentFacade.cpp` | `CognitionProductionTelemetryIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_cognition_production_telemetry_integration_test` |
| cognition telemetry log sink 只能投影 owner-safe attrs | `InfraTelemetrySink::emit_log()` 把 redacted `TelemetryEvent` 转为 module=`cognition` 的 `LogEvent`，同时过滤 forbidden attrs | `cognition/src/observability/CognitionTelemetry.cpp` | `CognitionProductionLoggingIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_cognition_production_logging_integration_test` |
| live composition 必须证明 completed / failed / degraded 三类事件可落盘且不泄漏敏感字段 | 直接读取 runtime.log，并同步检查 `LoggingFacade::last_dispatched_event()` 的 allowlist 结果 | `tests/integration/cognition/CognitionProductionLoggingIntegrationTest.cpp`、`tests/integration/cognition/CognitionProductionTelemetryIntegrationTest.cpp` | `CognitionProductionLoggingIntegrationTest`、`CognitionProductionTelemetryIntegrationTest` | `./build/vscode-linux-ninja/tests/integration/cognition/dasall_cognition_production_logging_integration_test && ./build/vscode-linux-ninja/tests/integration/cognition/dasall_cognition_production_telemetry_integration_test` |

## 3. 改动事实

1. `CognitionRuntimeDependencies` 新增 `logger`，并由 `make_live_telemetry_sink()` 统一把 logger/audit/metrics/trace 组装进同一个 cognition telemetry sink。
2. `InfraTelemetrySink::emit_log()` 现会把 redacted `TelemetryEvent` 写入 `infra::logging::ILogger`。event message 固定为 `cognition <event_name>`，log attrs 仅保留 `request_id`、`goal_id`、`profile_id`、`stage`、`trace_id`、`model_hint_tier`、decision/error/fallback 等 owner-safe 摘要字段。
3. `payload_excerpt`、`response_summary`、`clarification_question`、`candidate_scores` 与 raw prompt/token 不再进入 cognition ordinary log；若 event 同时具备 audit refs，只保留 `audit_ref_pending`、`audit_trace_id`、`audit_task_id`、`evidence_ref`、`evidence_kind` 这组 correlation anchor。
4. `RuntimeDependencySet` 现同步持有 `logger`，`RuntimeLiveDependencyComposition` 与 `AgentFacade` 不再只把 audit/metrics/trace 下发给 cognition，避免 runtime live unary 路径漏接 cognition logging。
5. 新增 `CognitionProductionLoggingIntegrationTest`，在 temp runtime.log 上验证 `stage.completed`、`stage.failed`、`response.degraded` 三类事件同时进入 shared logging sink，并且 runtime.log 中不出现 `top-secret`、`secret-token`、`payload_excerpt`、`response_summary`、`clarification_question`、`candidate_scores`。

## 4. 验证

1. `cmake --build build/vscode-linux-ninja --target dasall_cognition_production_logging_integration_test dasall_cognition_production_telemetry_integration_test`
   - 结果：通过。
2. `./build/vscode-linux-ninja/tests/integration/cognition/dasall_cognition_production_logging_integration_test`
   - 结果：通过。
3. `./build/vscode-linux-ninja/tests/integration/cognition/dasall_cognition_production_telemetry_integration_test`
   - 结果：通过。

## 5. 非外推边界

1. 本任务只闭合 cognition owner 的 logger seam、allowlist 和 build-tree runtime.log 证据；不宣称 query artifact、cross-subsystem e2e 或 installed/package proof 已完成。
2. runtime 侧本轮只补 `RuntimeDependencySet::logger` carrier，不扩写 runtime event bus -> logging bridge；该缺口继续由 `INF-LOG-SYS-FIX-005` 持有。
3. authoritative owner evidence 继续以 local installed authoritative evidence 为上限，本轮不使用 qemu / kvm。