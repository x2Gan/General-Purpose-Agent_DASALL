# INF-LOG-FIX-009 log query persisted reader / retention 收口

关联任务：INF-LOG-FIX-009  
日期：2026-05-27  
结论级别：L2/L3 build-tree persisted reader / artifact / cleanup evidence

## 1. 收口结论

1. `FileLogReader` 现已按 `runtime.log` + rotation family 读取真实落盘日志，并解析 `dasall.logging.event.v1` JSON-line；selector 继续固定为精确 `trace_id` / `session_id` + ordered time window，不扩全文检索、自由 attr filter 或 remote upload。
2. `LogQueryService::query()` 现会在 `infra.logging.export.enable_diag_pull == true` 且 access context 持有 `PolicyDecision::Allow` proof 时 materialize 本地 artifact JSON，并维护 owner-safe metadata JSONL index；artifact_ref 继续沿用 `diag://infra/logging/query/<query_id>`。
3. redaction-at-query 已进入 build-track 实现：artifact payload 与 index publish 前都会对 matched record 再走一次 `RedactionFilter`，因此不会因为 persisted reader 或 allowlist attrs 再次泄露 secret/token/password/auth value。
4. `LogRetentionPolicy::apply()` 现按 `created_at` retention window 与 max artifact count 清理 query artifact root 中过期/溢出 artifact；cleanup 明确不触碰 primary runtime log、rotation family、audit owner persistence 或 diagnostics retained snapshot store。

## 2. 代码与测试落点

1. 代码目标：
   - `infra/src/logging/FileLogReader.h`
   - `infra/src/logging/FileLogReader.cpp`
   - `infra/src/logging/LogQueryService.h`
   - `infra/src/logging/LogQueryService.cpp`
2. focused tests：
   - `tests/unit/infra/logging/LogQueryServicePersistedReaderTest.cpp`
   - `tests/unit/infra/logging/LogRetentionPolicyTest.cpp`
   - `tests/integration/infra/logging/LoggingDiagnosticsArtifactIntegrationTest.cpp`
3. 邻近回归：
   - `tests/unit/infra/logging/LogQueryServiceTest.cpp`
   - `tests/integration/infra/logging/LogQueryIntegrationTest.cpp`

## 3. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_log_query_service_persisted_reader_unit_test","dasall_log_retention_policy_unit_test","dasall_logging_diagnostics_artifact_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["LogQueryServicePersistedReaderTest","LogRetentionPolicyTest","LoggingDiagnosticsArtifactIntegrationTest"])`
   - 结果：命中仓库既有泛化 `生成失败`。
3. fallback authoritative evidence：
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_log_query_service_persisted_reader_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_log_retention_policy_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_diagnostics_artifact_integration_test`
   - 结果：3/3 通过。
4. 邻近回归：
   - `Build_CMakeTools(buildTargets=["dasall_log_query_service_unit_test","dasall_log_query_integration_test"])` -> 通过
   - `RunCtest_CMakeTools(tests=["LogQueryServiceTest","LogQueryIntegrationTest"])` -> 命中仓库既有泛化 `生成失败`
   - fallback `./build/vscode-linux-ninja/tests/unit/infra/dasall_log_query_service_unit_test && ./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_log_query_integration_test` -> 2/2 通过

## 4. 非外推边界

1. 本轮只闭合 build-tree persisted reader / artifact / cleanup evidence，不代表 daemon / CLI query surface 已默认开启；default-disabled/admin-only 口径继续由 `BLK-INF-LOG-008` 冻结结论约束。
2. 本轮不把 query artifact evidence 外推为 installed/package authoritative evidence；installed proof、package smoke 与 release handoff 继续留给 `INF-LOG-FIX-010` / `INF-LOG-FIX-011`。
3. 本轮继续不使用 qemu / kvm 作为 owner 验收口径。