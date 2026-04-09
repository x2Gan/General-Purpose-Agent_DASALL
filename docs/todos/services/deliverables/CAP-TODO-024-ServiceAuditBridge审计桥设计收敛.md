# CAP-TODO-024 ServiceAuditBridge 审计桥设计收敛

日期：2026-04-09
任务：CAP-TODO-024
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.10 与 9.4 已冻结 services 审计输出面：必须覆盖高风险动作前后、补偿入口与强制 `fallback_blocked`，并与普通日志分离存储。
2. 同一设计文档在 6.3 / 6.6.1 / 6.8.2 明确 `ExecutionCommandLane` 是副作用命令与显式补偿动作的唯一执行面，而 `AdapterRouter` 只负责返回 route 结果，因此审计桥的最小接入点必须在 lane，而不是扩散到 facade、router 与外部调用方各自拼装审计事件。
3. [infra/include/audit/AuditTypes.h](../../../../infra/include/audit/AuditTypes.h) 与 [infra/include/audit/IAuditLogger.h](../../../../infra/include/audit/IAuditLogger.h) 已冻结 `AuditEvent` / `AuditContext` / `AuditWriteOutcome` 的抽象边界，要求事件必须具备 `event_id`、`action`、`actor`、`target`、`outcome`、`evidence_ref` 与 `timestamp`，因此 services 只能适配现有 infra 审计抽象，不能自建平行审计载荷。
4. [services/src/adapters/AdapterRouter.h](../../../../services/src/adapters/AdapterRouter.h) 与 [services/src/ops/ServiceConfigAdapter.cpp](../../../../services/src/ops/ServiceConfigAdapter.cpp) 已在 023 中收口 `audit_level`、`observability_bridge_enabled` 与统一 `ServicePolicyView`，这为 024 提供了稳定的内部 policy 基线，而不需要新增 `services.*` schema。

## 2. 外部参考

1. OWASP Logging Cheat Sheet 强调安全日志与普通业务/运维日志经常服务于不同目的，因此通常应保持分离存储；这支持本轮把高风险动作与补偿链路审计明确收敛到 `infra::audit::IAuditLogger`，而不是混入普通结构化日志。
2. 同一指南要求应用侧日志记录应保持一致的事件属性，并尽可能覆盖 “who / what / where / when”；这对应本轮将 `tool_call_id`、`capability/target`、`execution_id/source_execution_id`、`result/reason` 与 side effects 统一编码进 services audit 事件。
3. 该指南同时要求把日志功能纳入测试，并确保日志失败不会演化为静默丢失；这支持本轮给 `ServiceAuditBridge` 增加本地 emit status、缺 sink 失败可见性，以及 unit/integration 两层验证入口。

## 3. Design 结论

1. 新增 internal `ServiceAuditBridge`，唯一职责是把 services execution/compensation/fallback 事实映射为 `infra::AuditEvent` 并通过 `infra::audit::IAuditLogger` 发射，不引入新的公共 ABI。
2. `ExecutionCommandLane` 成为 024 的唯一集成点：高风险 `execute()` 在 lane 内发射 `service.execution.requested` / `service.execution.completed`，显式 `compensate()` 发射 `service.execution.compensation_requested` / `service.execution.compensation_completed`，route failure 为 `fallback_blocked` 时额外发射 `service.route.fallback_blocked`。
3. 由于 `AuditEvent` 不接受 `Unspecified` outcome，本轮把 request 类事件映射为 `AuditOutcome::Escalated`，把 completion 类事件映射为 `Succeeded / Failed / Rejected`，把 `fallback_blocked` 固定映射为 `Rejected`。
4. 事件相关字段统一按当前冻结 supporting objects 派生：`actor` 来自 `tool_call_id`，`target` 使用 `capability_id:target_id`，`evidence_ref` 分别锚定到 `execution://...`、`compensation://...` 与 `route://...`，而 operation、reason、source_execution_id 与实际 side effects 保留在 `AuditEvent.side_effects` 中。
5. 本轮只把缺 sink / write failure 暴露为 `ServiceAuditBridgeStatus` 与 `ServiceAuditEmitResult` 的本地退化事实，不在 024 内越权改变低风险执行主链的结果语义；后续 metrics/health 任务可继续消费这些退化事实做聚合与告警。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal services 审计桥与发射状态对象 | services/src/bridges/ServiceAuditBridge.h、services/src/bridges/ServiceAuditBridge.cpp |
| 在命令/补偿 lane 内收口高风险、补偿与 fallback_blocked 审计 | services/src/execution/ExecutionCommandLane.h、services/src/execution/ExecutionCommandLane.cpp |
| 将审计桥接入 services 构建图 | services/CMakeLists.txt |
| 覆盖审计桥字段映射、缺 sink 失败可见性 | tests/unit/services/bridges/ServiceAuditBridgeTest.cpp、tests/unit/services/bridges/CMakeLists.txt |
| 覆盖 ServiceFacade -> ExecutionCommandLane -> ServiceAuditBridge 串联 | tests/integration/services/CapabilityServicesAuditIntegrationTest.cpp、tests/integration/services/CMakeLists.txt |
| 接入 unit/integration 聚合目标 | tests/unit/services/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/integration/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/bridges/ServiceAuditBridge.h/.cpp`，并把 `ExecutionCommandLane` 接到统一的 services 审计桥接点。
2. 测试目标：新增 `tests/unit/services/bridges/ServiceAuditBridgeTest.cpp` 与 `tests/integration/services/CapabilityServicesAuditIntegrationTest.cpp`，分别验证审计字段映射与真实 facade/lane 串联。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L integration`

## 6. 风险与回退

1. 当前 `ServiceCallContext` 还没有独立 `decision_ref` 字段，因此 024 只能以 `tool_call_id/request_id/execution_id` 作为当前最小可追溯锚点；若后续 Policy Alignment Gate 要求显式 confirmation proof / decision ref，则必须先通过 supporting object review，而不是在审计桥里自造公共语义。
2. `ServiceAuditBridge` 当前只收口 audit 发射与本地退化状态，不直接做 health 聚合或 exporter recovery；025~027 仍需继续补 metrics/trace/health 才能形成完整 observability 面。
3. 本轮审计桥默认不吞掉低风险主链结果，也不越权改变路由决策；若未来要把 audit sink 缺失升级为高风险动作前置 deny，需要在 health/policy gate 上补充专门的 admission 评审，而不是在 024 内直接改变所有命令行为。