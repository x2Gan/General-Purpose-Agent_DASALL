# INF-LOG-FIX-008 audit route / owner handoff 收口

关联任务：INF-LOG-FIX-008  
日期：2026-05-27  
结论级别：L2 build-tree audit route / owner handoff / correlation evidence

## 1. 收口结论

1. `LoggingFacade` 现已在 ordinary log 完成 `enrich -> redact -> format` 后、dispatch 前，对 high-risk event fail-closed 执行 `IAuditLogger::write_audit()` handoff；缺完整 audit anchor attrs 或缺 attached audit logger 时，普通日志不会继续 dispatch。
2. `AuditLinkAdapter` 与 `SinkDispatcher` 现已共同守住 `BLK-INF-LOG-007` 冻结的边界：high-risk classifier 只允许 `category==audit`、`Fatal` 或 `event_kind=high_risk`，ordinary log 只保留 `audit_ref_pending/evidence_ref/evidence_kind/audit_trace_id/audit_task_id` 五个 correlation attrs。
3. `compose_live_observability()` 现已在 concrete audit logger 启动成功后 attach 回 `LoggingFacade`，使 build-tree live composition 下的 ordinary log route、audit owner persistence、trace/task correlation 与 privacy split 同时成立。
4. `LoggingAuditRouteIntegrationTest`、`AuditLinkAdapterPersistenceTest` 与 `AuditLogCorrelationContractTest` 现已分别覆盖 route、owner handoff/persistence 与 correlation/privacy split；敏感 payload 不会重复泄漏到 ordinary log record、audit target 或 audit side effects。

## 2. 代码回写

1. `infra/src/logging/AuditLinkAdapter.cpp`
2. `infra/src/logging/SinkDispatcher.cpp`
3. `infra/src/logging/LoggingFacade.h`
4. `infra/src/logging/LoggingFacade.cpp`
5. `infra/src/ObservabilityLiveComposition.cpp`
6. `tests/integration/infra/logging/LoggingAuditLinkIntegrationTest.cpp`
7. `tests/integration/infra/logging/CMakeLists.txt`
8. `tests/integration/CMakeLists.txt`
9. `tests/unit/infra/logging/AuditLinkAdapterPersistenceTest.cpp`
10. `tests/unit/infra/CMakeLists.txt`
11. `tests/unit/CMakeLists.txt`
12. `tests/contract/smoke/AuditLogCorrelationContractTest.cpp`
13. `tests/contract/CMakeLists.txt`
14. `docs/architecture/DASALL_infra_logging模块详细设计.md`
15. `docs/ssot/LoggingProductionAcceptanceMatrix.md`
16. `docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md`
17. `docs/todos/DASALL_子系统查漏补缺专项记录.md`

## 3. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_logging_audit_link_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["LoggingAuditLinkIntegrationTest"])`
   - 结果：命中仓库既有泛化 `生成失败`。
3. fallback 直接执行：
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_audit_link_integration_test`
   - 结果：通过。
4. `Build_CMakeTools(buildTargets=["dasall_logging_audit_route_integration_test","dasall_audit_link_adapter_persistence_unit_test","dasall_audit_log_correlation_contract_test"])`
   - 结果：通过。
5. `RunCtest_CMakeTools(tests=["LoggingAuditRouteIntegrationTest","AuditLinkAdapterPersistenceTest","AuditLogCorrelationContractTest"])`
   - 结果：命中仓库既有泛化 `生成失败`。
6. fallback 直接执行：
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_audit_route_integration_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_audit_link_adapter_persistence_unit_test`
   - `./build/vscode-linux-ninja/tests/contract/dasall_audit_log_correlation_contract_test`
   - 结果：三项均通过。

## 4. 非外推边界

1. 本轮只闭合 L2 build-tree 下的 audit route、owner handoff、correlation 与 privacy split，不把结论外推到 installed package、release runner、qemu 或 soak。
2. `LogQueryService` 的 persisted reader/index/retention/cleanup 仍留给 `INF-LOG-FIX-009`；当前普通日志与 audit owner persistence 的 join/export 旁路继续禁止。
3. owner 验收口径继续以 installed authoritative evidence 为后继目标，不回退到 qemu / kvm 口径。