# BLK-INF-LOG-007 audit route classifier / payload boundary 冻结

关联任务：BLK-INF-LOG-007  
日期：2026-05-27  
结论级别：L1 design / SSOT freeze

## 1. 收口结论

1. `docs/ssot/LoggingProductionAcceptanceMatrix.md` 与 `docs/architecture/DASALL_infra_logging模块详细设计.md` 现已共同冻结 `INF-LOG-FIX-008` 的 v1 high-risk classifier：`LogEvent.category() == "audit"`、`LogLevel::Fatal`，以及 attrs 显式声明 `event_kind=high_risk`。普通 `LogLevel::Error` 若没有这个 marker，继续留在 ordinary log route。
2. `AuditLinkAdapter` 的 ordinary-log audit ref schema 已固定为 `audit_ref_pending`、`evidence_ref`、`evidence_kind`、`audit_trace_id`、`audit_task_id` 五个 attrs；高风险事件若缺少完整 `AuditRef`，必须 fail-closed 返回 `ValidationFieldMissing`，且不允许留下部分 `audit_*` attrs。
3. privacy split 已冻结：ordinary log 只保留 redacted message 与 audit correlation anchors；`actor`、`action`、`target`、`outcome`、`side_effects` 等完整 audit payload 继续由 audit owner persistence 持有，`IAuditLogger::write_audit()` 仍是唯一 handoff seam。

## 2. 本地证据与业界对齐

1. 本地证据：`AuditLinkAdapter.cpp` / `SinkDispatcher.cpp` 已存在 attrs-based route skeleton，但 blocker 前缺少正式 SSOT 来说明哪些事件算 high-risk、哪些字段允许留在 ordinary log、哪些 audit payload 必须留在 audit owner。
2. OWASP Logging Cheat Sheet 明确建议把 audit trail 与一般 security/event logging 分层处理，并在 `Data to exclude` 中列出 session identification values、access tokens、passwords、file paths 等不应直接写入日志的高敏感数据；这与本轮把完整 audit payload 保留在 audit owner persistence、只在 ordinary log 中保留最小 correlation anchor 的结论一致。
3. OpenTelemetry Logs Data Model 将稳定公共语义保留在 top-level fields，把可变事件上下文放在 attributes 中，并强调 `TraceId` / `Attributes` 的相关性承载。这与本轮“只保留最小 audit correlation attrs，不新增新的 top-level log contract”保持一致。

## 3. 回写清单

1. `docs/ssot/LoggingProductionAcceptanceMatrix.md`
2. `docs/architecture/DASALL_infra_logging模块详细设计.md`
3. `docs/todos/DASALL_子系统查漏补缺专项记录.md`
4. `tests/contract/smoke/LoggingProductionAcceptanceContractTest.cpp`

## 4. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_logging_production_acceptance_contract_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["LoggingProductionAcceptanceContractTest"])`
   - 结果：命中仓库既有泛化 `生成失败`。
3. fallback 直接执行：
   - `./build/vscode-linux-ninja/tests/contract/dasall_logging_production_acceptance_contract_test`
   - 结果：通过。

## 5. 非外推边界

1. 本轮只闭合 `INF-LOG-FIX-008` 的 design / SSOT blocker，不代表 audit route、audit owner persistence handoff 或 correlation contract 的生产实现已经完成。
2. 本轮继续不把 qemu / kvm 作为 owner 验收口径；后续实现与验收仍以 build-tree focused evidence 和 installed authoritative evidence 的既有分层为准。