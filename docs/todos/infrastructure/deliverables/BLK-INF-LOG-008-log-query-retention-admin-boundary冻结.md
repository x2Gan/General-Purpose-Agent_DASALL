# BLK-INF-LOG-008 log query retention / admin boundary 冻结

关联任务：BLK-INF-LOG-008  
日期：2026-05-27  
结论级别：L1 design / SSOT freeze

## 1. 收口结论

1. `docs/ssot/LoggingProductionAcceptanceMatrix.md` 与 `docs/architecture/DASALL_infra_logging模块详细设计.md` 现已共同冻结 `INF-LOG-FIX-009` 的 query artifact surface 为 default-disabled/admin-only：daemon / CLI / installed command surface 默认不得开放 log query artifact；只有 `infra.logging.export.enable_diag_pull == true` 且调用方携带 `PolicyDecision::Allow` 的完整 allow proof 时，`LogQueryService` 才允许 materialize 本地 artifact。
2. `diag://infra/logging/query/<query_id>` 继续只表示 diagnostics local artifact 引用；persisted artifact 与 index 只能落在本地 diagnostics artifact root，不得变成 primary runtime log owner、remote export target 或任意全文搜索入口。index 只允许保存 `artifact_ref/query_id/selector_kind/selector_value/checksum/match_count/truncated/created_at` 这类 owner-safe metadata。
3. redaction-at-query 与 retention cleanup 已冻结：query artifact materialization / index write 必须对 matched record 再执行一次 redaction；cleanup 只允许清理 query artifact / index 自身，不得删除、截断或重写 primary runtime log、rotation family、audit owner persistence 或 diagnostics retained snapshot artifact。

## 2. 本地证据与边界对齐

1. 本地证据：`LogQueryService` 当前只有 `ILogQueryRecordReader` 注入骨架，仍没有 persisted reader、artifact materializer 或 retention policy；blocker 前缺少正式 SSOT 来说明 query artifact 默认是否开放、artifact/index 能保存什么、cleanup 可以清理什么。
2. `INF-FIX-002` 已把 diagnostics retained snapshot 与 daemon `diag_disabled` admin boundary 的 owner 分层冻结，因此本轮可直接复用“default-disabled/admin-only 属于 command surface boundary，而不是 retained snapshot/query service failure”的既有结论。
3. `LoggingProductionAcceptanceMatrix` 已把 `diag://infra/logging/query/<query_id>` 固定为 diagnostics local artifact 命名空间；本轮继续保持 query artifact 与 primary runtime log / audit persistence 分层，不把 logging query 扩成 remote export 或全文检索入口。

## 3. 回写清单

1. `docs/ssot/LoggingProductionAcceptanceMatrix.md`
2. `docs/architecture/DASALL_infra_logging模块详细设计.md`
3. `docs/todos/DASALL_子系统查漏补缺专项记录.md`
4. `tests/contract/smoke/LoggingProductionAcceptanceContractTest.cpp`

## 4. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_logging_production_acceptance_contract_test"])`
   - 结果：待本轮 blocker 修改后执行。
2. `RunCtest_CMakeTools(tests=["LoggingProductionAcceptanceContractTest"])`
   - 结果：预期继续命中仓库既有泛化 `生成失败`。
3. fallback authoritative evidence：
   - `./build/vscode-linux-ninja/tests/contract/dasall_logging_production_acceptance_contract_test`

## 5. 非外推边界

1. 本轮只闭合 `INF-LOG-FIX-009` 的 design / SSOT blocker，不代表 persisted reader、artifact materialization、metadata index 或 retention cleanup 的生产实现已经完成。
2. 本轮继续不把 qemu / kvm 作为 owner 验收口径；后续实现与验收仍以 build-tree focused evidence 和 installed authoritative evidence 的既有分层为准。