# INF-LOG-SYS-FIX-003 memory production logging 证据收口

日期：2026-05-29
关联任务：INF-LOG-SYS-FIX-003 / INF-LOG-FIX-010 / INF-LOG-SYS-GATE-002  
关联冻结：`docs/ssot/KeySubsystemLoggingFieldMatrix.md`、`docs/architecture/DASALL_infra_logging模块详细设计.md` 6.10.13 / 6.10.15

## 1. 目标与约束

1. 沿 `KeySubsystemLoggingFieldMatrix` 收紧 memory ordinary log attrs，不再允许 `make_log_attrs()` 把未知 field 直接透传进 persisted runtime.log。
2. 建立 build-tree focused evidence，证明 memory `lifecycle` / `writeback` / `context` / `maintenance` 事件既能落到 shared runtime.log，也能通过 `LogQueryService` 用 `session_id` / `trace_id` / `request_id` 查询出 redacted artifact。
3. raw context / retrieval / writeback payload 不得进入 memory ordinary log 或 query artifact；当前只闭合 build-tree focused evidence，不外推到 installed/package authoritative evidence。
4. fail-fast 静默失败必须有日志证据：writeback turn validation 失败需要输出 `writeback.failed`、`warning_codes` 与 bounded `failure_reason`。

## 2. Design -> Build 映射

| Design 结论 | Build 落点 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| memory ordinary log 只能输出 owner-safe attrs | `MemoryObservability::make_log_attrs()` 改为 allowlist 过滤，而不是透传任意 field | `memory/src/observability/MemoryObservability.cpp` | `MemoryObservabilityBridgeTest` | `cmake --build build/vscode-linux-ninja --target dasall_memory_observability_bridge_integration_test` |
| lifecycle/writeback/context/maintenance 事件必须能落盘并保留可查询 correlation | 在 temp state root 下写入 runtime.log，并通过 `FileLogReader + LogQueryService` 查询 session/trace/request scoped artifact | `tests/integration/memory/MemoryProductionLoggingIntegrationTest.cpp` | `MemoryProductionLoggingIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_memory_production_logging_integration_test` |
| raw context/retrieval/writeback payload 不得进入 runtime.log 或 query artifact | 对 runtime.log / query artifact 同时做 forbidden payload grep | `tests/integration/memory/MemoryProductionLoggingIntegrationTest.cpp`、`tests/integration/memory/MemoryObservabilityBridgeTest.cpp` | `MemoryProductionLoggingIntegrationTest`、`MemoryObservabilityBridgeTest` | `./build/vscode-linux-ninja/tests/integration/memory/dasall_memory_production_logging_integration_test && ./build/vscode-linux-ninja/tests/integration/memory/dasall_memory_observability_bridge_integration_test` |
| lifecycle/fail-fast 事件必须具备生产调试字段 | `MemoryManager` 输出 init/shutdown lifecycle 摘要；`WritebackCoordinator` 对 invalid turn 输出 failed telemetry | `memory/src/MemoryManager.cpp`、`memory/src/writeback/WritebackCoordinator.cpp` | `MemoryObservabilityBridgeTest`、`MemoryProductionLoggingIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_memory_observability_bridge_integration_test dasall_memory_production_logging_integration_test` |
| request-scoped diagnostics 必须可精确回拉 | `LogQueryService` 支持 `RequestId` selector，memory production logging test 同时覆盖 session/trace/request artifact | `infra/src/logging/LogQueryService.{h,cpp}`、`tests/unit/infra/logging/LogQueryServiceTest.cpp`、`tests/integration/memory/MemoryProductionLoggingIntegrationTest.cpp` | `LogQueryServiceTest`、`MemoryProductionLoggingIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_log_query_service_unit_test dasall_memory_production_logging_integration_test` |

## 3. 改动事实

1. `memory/src/observability/MemoryObservability.cpp` 现为 ordinary log attrs 建立 allowlist，只保留 `warning_count`、`warning`、`warning_codes`、`degraded`、`result_code`、`failure_reason`、`fact_count`、`experience_count`、`conflict_count`、`partial`、`retryable`、`turn_id`、`summary_id`、冲突摘要字段、lifecycle 摘要字段，以及 maintenance 的请求/结果计数等 owner-safe 摘要字段。
2. raw `summary_text`、`goal_summary`、`latest_observation_digest_summary`、`agent_response`、`fact_text`、retrieval evidence 正文与其他未知 field 现不会进入 memory ordinary log attrs。
3. `MemoryManager` 现输出 `init.completed`、`init.failed`、`init.skipped`、`shutdown.completed`、`shutdown.skipped`，并携带 `storage_backend`、`vector_enabled`、`auto_schedule`、`lifecycle_state`、`result_code`、`failure_reason`。
4. `WritebackCoordinator` 现对 invalid turn fail-fast 输出 `writeback.failed`，保留 `request_id` / `trace_id` correlation，并输出 `warning_codes=writeback_turn_invalid` 与 contracts guard 的 bounded `failure_reason`。
5. `MemoryProductionLoggingIntegrationTest` 在 temp `state_root/logging/runtime.log` 上触发 `init`、`writeback`、`context`、`maintenance`、`shutdown` 路径，并使用 `FileLogReader + LogQueryService` 分别按 `session_id`、`trace_id`、`request_id` 读取 query artifact。
6. `MemoryObservabilityBridgeTest` 现同时检查 `LoggingFacade::last_dispatched_event()` 不含 raw context / writeback payload 字段，防止 build-tree in-memory dispatch 回归成 payload passthrough；并验证 lifecycle audit action 与 invalid writeback failure telemetry。

## 4. 验证

1. `cmake --build build/vscode-linux-ninja --target dasall_memory_production_logging_integration_test dasall_memory_observability_bridge_integration_test dasall_log_query_service_unit_test dasall_memory_interface_compile_unit_test`
   - 结果：通过。
2. `./build/vscode-linux-ninja/tests/unit/infra/dasall_log_query_service_unit_test`
   - 结果：通过。
3. `./build/vscode-linux-ninja/tests/unit/memory/dasall_memory_interface_compile_unit_test`
   - 结果：通过。
4. `./build/vscode-linux-ninja/tests/integration/memory/dasall_memory_production_logging_integration_test`
   - 结果：通过。
5. `./build/vscode-linux-ninja/tests/integration/memory/dasall_memory_observability_bridge_integration_test`
   - 结果：通过。

## 5. 生产调试操作口径

1. 单请求问题优先按 `request_id` 查询：用于定位某次 runtime writeback、context assembly 或 maintenance 手动触发是否到达 memory owner，以及 fail-fast 是否返回 `writeback.failed`。
2. 跨请求/异步链路优先按 `trace_id` 查询：用于把 runtime caller、memory lifecycle、writeback、context 与 maintenance 事件放到同一 diagnostics artifact 中检查顺序与失败点。
3. 会话级回放优先按 `session_id` 查询：用于定位一个 session 下是否完成 writeback/context 组合，以及 maintenance 是否影响相关记录。
4. lifecycle 排障固定先看 `memory init.*` / `memory shutdown.*`：`storage_backend`、`vector_enabled`、`auto_schedule`、`lifecycle_state`、`result_code`、`failure_reason` 是 owner-safe 判断依据，不需要也不允许回捞 sqlite path、context body 或 raw payload。
5. writeback 失败排障固定先看 `warning_codes` 与 `failure_reason`：`writeback_turn_invalid` 表示 contracts turn guard 拒绝，`failure_reason` 只能是 bounded guard reason；若需要 raw turn 内容，必须回到 memory owner 的受控测试 fixture 或本地调试器，不从 runtime.log/query artifact 取明文。
6. redaction 验收固定同时检查 runtime.log 与 query artifact：两者都不得出现 raw context、retrieval payload、summary text、agent response、fact text、token/secret/password/auth value。

## 6. 非外推边界

1. 本任务只闭合 memory owner 的 allowlist 与 build-tree persisted/query 证据；跨子系统 e2e、installed/package authoritative evidence 继续留给 `INF-LOG-SYS-FIX-007` / `INF-LOG-FIX-011`。
2. 当前 query 只证明 build-tree `session_id` / `trace_id` / `request_id` scoped memory records 可从 runtime.log materialize 出 redacted artifact；不把 diagnostics artifact 直接外推为 package 或 release 结论。
3. 本轮不使用 qemu / kvm。