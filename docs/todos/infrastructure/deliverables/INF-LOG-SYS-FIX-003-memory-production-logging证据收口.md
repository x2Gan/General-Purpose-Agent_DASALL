# INF-LOG-SYS-FIX-003 memory production logging 证据收口

日期：2026-05-28  
关联任务：INF-LOG-SYS-FIX-003 / INF-LOG-FIX-010 / INF-LOG-SYS-GATE-002  
关联冻结：`docs/ssot/KeySubsystemLoggingFieldMatrix.md`、`docs/architecture/DASALL_infra_logging模块详细设计.md` 6.10.13 / 6.10.15

## 1. 目标与约束

1. 沿 `KeySubsystemLoggingFieldMatrix` 收紧 memory ordinary log attrs，不再允许 `make_log_attrs()` 把未知 field 直接透传进 persisted runtime.log。
2. 建立 build-tree focused evidence，证明 memory `writeback` / `context` / `maintenance` 事件既能落到 shared runtime.log，也能通过 `LogQueryService` 用 `session_id` 查询出 redacted artifact。
3. raw context / retrieval / writeback payload 不得进入 memory ordinary log 或 query artifact；当前只闭合 build-tree focused evidence，不外推到 installed/package authoritative evidence。

## 2. Design -> Build 映射

| Design 结论 | Build 落点 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| memory ordinary log 只能输出 owner-safe attrs | `MemoryObservability::make_log_attrs()` 改为 allowlist 过滤，而不是透传任意 field | `memory/src/observability/MemoryObservability.cpp` | `MemoryObservabilityBridgeTest` | `cmake --build build/vscode-linux-ninja --target dasall_memory_observability_bridge_integration_test` |
| writeback/context/maintenance 事件必须能落盘并保留可查询 session correlation | 在 temp state root 下写入 runtime.log，并通过 `FileLogReader + LogQueryService` 查询 session-scoped artifact | `tests/integration/memory/MemoryProductionLoggingIntegrationTest.cpp` | `MemoryProductionLoggingIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_memory_production_logging_integration_test` |
| raw context/retrieval/writeback payload 不得进入 runtime.log 或 query artifact | 对 runtime.log / query artifact 同时做 forbidden payload grep | `tests/integration/memory/MemoryProductionLoggingIntegrationTest.cpp`、`tests/integration/memory/MemoryObservabilityBridgeTest.cpp` | `MemoryProductionLoggingIntegrationTest`、`MemoryObservabilityBridgeTest` | `./build/vscode-linux-ninja/tests/integration/memory/dasall_memory_production_logging_integration_test && ./build/vscode-linux-ninja/tests/integration/memory/dasall_memory_observability_bridge_integration_test` |

## 3. 改动事实

1. `memory/src/observability/MemoryObservability.cpp` 现为 ordinary log attrs 建立 allowlist，只保留 `warning_count`、`warning`、`degraded`、`result_code`、`fact_count`、`experience_count`、`conflict_count`、`partial`、`retryable`、`turn_id`、`summary_id`、冲突摘要字段，以及 maintenance 的请求/结果计数等 owner-safe 摘要字段。
2. raw `summary_text`、`goal_summary`、`latest_observation_digest_summary`、`agent_response`、`fact_text`、retrieval evidence 正文与其他未知 field 现不会进入 memory ordinary log attrs。
3. 新增 `MemoryProductionLoggingIntegrationTest`，在 temp `state_root/logging/runtime.log` 上触发 `writeback`、`context`、`maintenance` 三条路径，并使用 `FileLogReader + LogQueryService` 按 `session_id` 读取 query artifact，验证至少命中 `writeback` 与 `context` 事件。
4. `MemoryObservabilityBridgeTest` 现同时检查 `LoggingFacade::last_dispatched_event()` 不含 raw context / writeback payload 字段，防止 build-tree in-memory dispatch 回归成 payload passthrough。

## 4. 验证

1. `cmake --build build/vscode-linux-ninja --target dasall_memory_production_logging_integration_test dasall_memory_observability_bridge_integration_test`
   - 结果：通过。
2. `./build/vscode-linux-ninja/tests/integration/memory/dasall_memory_production_logging_integration_test`
   - 结果：通过。
3. `./build/vscode-linux-ninja/tests/integration/memory/dasall_memory_observability_bridge_integration_test`
   - 结果：通过。

## 5. 非外推边界

1. 本任务只闭合 memory owner 的 allowlist 与 build-tree persisted/query 证据；跨子系统 e2e、installed/package authoritative evidence 继续留给 `INF-LOG-SYS-FIX-007` / `INF-LOG-FIX-011`。
2. 当前 query 只证明 session-scoped `writeback` / `context` 记录可从 runtime.log materialize 出 redacted artifact；不把 diagnostics artifact 直接外推为 package 或 release 结论。
3. 本轮不使用 qemu / kvm。