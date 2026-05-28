# INF-LOG-SYS-FIX-004 knowledge production logging 收口

日期：2026-05-28  
关联任务：INF-LOG-SYS-FIX-004 / INF-LOG-FIX-010 / INF-LOG-SYS-GATE-002  
关联冻结：`docs/ssot/KeySubsystemLoggingFieldMatrix.md`、`docs/architecture/DASALL_infra_logging模块详细设计.md` 6.10.13 / 6.10.16

## 1. 目标与约束

1. 让 knowledge primary / fallback telemetry event 在 live composition 下进入 shared runtime.log，并保留 `telemetry_path` 区分 primary 与 fallback。
2. 给 knowledge ordinary log 增加 build-tree 可查询 selector，使 `FileLogReader + LogQueryService` 能按 `session_id` materialize diagnostics artifact。
3. raw retrieve query/body、corpus document text 与 secret-bearing detail 不得进入 runtime.log 或 diagnostics artifact；本轮只闭合 build-tree focused evidence，不外推到 installed/package authoritative evidence。

## 2. Design -> Build 映射

| Design 结论 | Build 落点 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| knowledge log 需要 session-scoped selector 才能支撑 diagnostics artifact | `KnowledgeQuery.session_id -> KnowledgeTelemetryEvent.session_id -> make_knowledge_log_event()` | `knowledge/include/health/KnowledgeTelemetry.h`、`knowledge/src/facade/KnowledgeService.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | `KnowledgeProductionLoggingIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_production_logging_integration_test dasall_knowledge_production_telemetry_integration_test` |
| invalid query text 与 invalid telemetry payload 必须分别落为 primary failure / fallback invalid-payload 事件 | live composition 下分别触发空 query_text 与空 request_id 输入，检查 runtime.log 的 `telemetry_path=primary/fallback` | `tests/integration/knowledge/KnowledgeProductionLoggingIntegrationTest.cpp` | `KnowledgeProductionLoggingIntegrationTest` | `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_production_logging_integration_test` |
| sink failure / invalid payload contract 继续由 knowledge owner 近端测试守住 | 保持 `KnowledgeTelemetryFieldSetTest` 与 `KnowledgeTelemetryDegradeEventTest` 通过，证明 fallback 与 fail-open 没被 session selector 改坏 | `tests/unit/knowledge/KnowledgeTelemetryFieldSetTest.cpp`、`tests/unit/knowledge/KnowledgeTelemetryDegradeEventTest.cpp`、`tests/unit/knowledge/KnowledgeTelemetryTest.cpp` | `KnowledgeTelemetryFieldSetTest`、`KnowledgeTelemetryDegradeEventTest`、`KnowledgeTelemetryTest` | `./build/vscode-linux-ninja/tests/unit/knowledge/dasall_knowledge_telemetry_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_knowledge_observability_fields_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_knowledge_telemetry_degrade_event_unit_test` |

## 3. 改动事实

1. `KnowledgeTelemetryEvent` 新增 `session_id`，`KnowledgeServiceFacade::retrieve()` / `fail_closed()` 现会把 `KnowledgeQuery.session_id` 注入 telemetry event。
2. `make_knowledge_log_event()` 现把 `session_id` 写入 ordinary log attrs；`request_id` 仍是 knowledge 主 correlation key，`session_id` 只是 build-tree diagnostics/query selector 增强，不替换现有主键语义。
3. 新增 `KnowledgeProductionLoggingIntegrationTest`，在 live runtime composition 下同时触发 success、primary failure、fallback invalid-payload 三条路径，校验 runtime.log 含 `knowledge.retrieve.completed` / `knowledge.retrieve.failed` / `knowledge.retrieve.invalid_payload` 与 `telemetry_path=primary/fallback`。
4. 同一 test 现使用 `FileLogReader + LogQueryService` 按 `session_id` materialize diagnostics artifact，证明 knowledge persisted runtime.log 已可 query/index；artifact 与 runtime.log 都不含 raw retrieve query/body。
5. `KnowledgeTelemetryFieldSetTest`、`KnowledgeTelemetryDegradeEventTest` 与 `KnowledgeTelemetryTest` 仅补齐 aggregate fixture 字段，不改变原有 invalid-payload fallback 和 sink-failure fail-open contract。

## 4. 验证

1. `cmake --build build/vscode-linux-ninja --target dasall_knowledge_production_logging_integration_test dasall_knowledge_production_telemetry_integration_test`
   - 结果：通过。
2. `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_production_logging_integration_test`
   - 结果：通过。
3. `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_production_telemetry_integration_test`
   - 结果：通过。
4. `cmake --build build/vscode-linux-ninja --target dasall_knowledge_telemetry_unit_test dasall_knowledge_observability_fields_unit_test dasall_knowledge_telemetry_degrade_event_unit_test`
   - 结果：通过。
5. `./build/vscode-linux-ninja/tests/unit/knowledge/dasall_knowledge_telemetry_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_knowledge_observability_fields_unit_test && ./build/vscode-linux-ninja/tests/unit/knowledge/dasall_knowledge_telemetry_degrade_event_unit_test`
   - 结果：通过。

## 5. 非外推边界

1. 本任务只闭合 knowledge owner 的 build-tree runtime.log / diagnostics artifact / fallback-path 证据；installed/package authoritative evidence 与跨子系统 e2e 继续留给 `INF-LOG-SYS-FIX-007` / `INF-LOG-FIX-011`。
2. `session_id` 在 knowledge ordinary log 中只作为 DASALL 内部 diagnostics selector，不表示外部认证 session，也不把 knowledge 提升为 trace owner。
3. 本轮不使用 qemu / kvm。