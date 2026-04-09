# CAP-TODO-030 Capability Services smoke integration 设计收敛

日期：2026-04-09
任务：CAP-TODO-030
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 的 6.10、7.1、8.3、9.1 已冻结 Phase 5 smoke 口径：必须覆盖 `Tool -> IExecutionService / IDataService -> CapabilityServicesLoopbackFixture -> LocalServiceAdapter loopback -> result` 的最小闭环，并让结构化 observability 字段可被二值断言。
2. [tests/integration/services/CapabilityServicesSmokeIntegrationTest.cpp](../../../../tests/integration/services/CapabilityServicesSmokeIntegrationTest.cpp) 在本轮前只验证 execute/query/catalog 的 loopback round-trip 与默认 local route，并未覆盖 request ledger、audit context、trace span 的字段可观测性，因此 029 只关闭了 discoverability，未关闭 smoke 语义验收。
3. [tests/mocks/include/CapabilityServicesLoopbackFixture.h](../../../../tests/mocks/include/CapabilityServicesLoopbackFixture.h) 已经复用 production `ServiceFacade`、`ExecutionCommandLane`、`DataQueryLane`、`AdapterBridge` 与 `LocalServiceAdapter` / `RemoteServiceAdapter`，但在本轮前没有暴露 audit/trace 注入点，因此 smoke 无法在不复制组装代码的前提下证明 observability 链条。
4. [services/src/bridges/ServiceAuditBridge.cpp](../../../../services/src/bridges/ServiceAuditBridge.cpp) 明确冻结了 services audit 字段口径：`request_id`、`trace_id`、`worker_type` 位于 `AuditContext`，`request_id` side effect 只出现在 `service.execution.requested`，而不是 completed 事件；smoke 断言必须服从这一语义，而不能反向修改 production 行为。
5. [services/src/bridges/ServiceTraceBridge.cpp](../../../../services/src/bridges/ServiceTraceBridge.cpp) 已冻结 facade/lane/adapter/external 的 span 命名、`services.request_id` / `services.tool_call_id` / `services.capability_id` / `services.target_id` 属性和父子 span 关系；smoke 只需要把桥挂入 loopback fixture，就能验证 trace root 与 nested span 结构。
6. [services/src/execution/ExecutionCommandLane.cpp](../../../../services/src/execution/ExecutionCommandLane.cpp) 明确规定：high-risk/critical action 必须携带 `idempotency_key`，且 execution audit 只对 high-risk 路径发射。因此 030 的 observability smoke 必须显式选择 high-risk `toggle` + idempotency key，而不是弱化 lane 约束。

## 2. 外部参考

1. CTest 官方手册说明 `ctest -N` 只列出将运行的测试而不执行，`-L` 基于 label 过滤测试集合；这支持 030 在保持 029 discoverability 不回退的前提下，用 `ctest -N` 和 `ctest -L integration` 同时证明 smoke 可发现性与执行稳定性。参考：https://cmake.org/cmake/help/latest/manual/ctest.1.html
2. OpenTelemetry trace 概念文档明确指出：同一 trace 内的 spans 共享 `trace_id`，子 span 通过 `parent_id` 形成层级；span attributes 用于携带结构化上下文字段，span 本身可视为带上下文的 structured logs。这支持 030 在 smoke 中把 `request_id` / `tool_call_id` / `capability_id` / `target_id` 作为 trace attributes 验证，并要求 facade/lane/adapter/external 保持严格父子链。参考：https://opentelemetry.io/docs/concepts/signals/traces/

## 3. Design 结论

1. `CapabilityServicesLoopbackFixture` 继续保持 tests-side header-only 支撑，但新增可选 `audit_bridge` / `trace_bridge` 注入点，以及 `high_risk_actions` / `critical_actions` 控制项。这样 smoke 可以直接复用 production facade/lane/adapter 主链，不需要在测试里复制 services 组装逻辑。
2. `CapabilityServicesSmokeIntegrationTest` 保持两段式验收：
   - 第一段继续验证 execute/query/catalog 的最小 loopback round-trip，并把 local request ledger 作为当前 V1 的结构化日志字段证据，显式断言 `request_id`、`capability_id`、`target_id`、`operation_name`。
   - 第二段挂入真实 `ServiceAuditBridge` 与 `ServiceTraceBridge`，通过 high-risk `toggle` + `idempotency_key` 穿过 production 审计门控，验证 audit event family、audit context 字段、trace scope、span 属性与 parent-child 关系。
3. smoke observability 不额外发明 production 日志桥。当前仓库没有单独的 services logging bridge TODO，因此 030 用 loopback request ledger 证明结构化 request fields 可观测，用 audit/trace 证明跨链上下文字段和层级可观测，满足 Phase 5 对 smoke 可观测性的最小验收口径。
4. 030 完成后，CAP-TODO-031 与 CAP-TODO-032 不再受 smoke blocker 约束，可以分别进入 failure injection 与 profile 差异的 direct build 轮次。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| loopback fixture 挂接 audit/trace 与 action policy 注入点 | tests/mocks/include/CapabilityServicesLoopbackFixture.h |
| smoke 最小闭环继续验证 request ledger 字段 | tests/integration/services/CapabilityServicesSmokeIntegrationTest.cpp |
| smoke observability 子场景验证 high-risk audit + facade/lane/adapter/external trace 链 | tests/integration/services/CapabilityServicesSmokeIntegrationTest.cpp |
| 回写 030 状态、031/032 解阻结果与执行证据 | docs/todos/services/DASALL_capability_services子系统专项TODO.md、docs/worklog/DASALL_开发执行记录.md |

## 5. Build 三件套

1. 代码目标：扩展 `CapabilityServicesLoopbackFixture` 以承接 `ServiceAuditBridge` / `ServiceTraceBridge` 和 action policy 选项，并把 `CapabilityServicesSmokeIntegrationTest` 从“只看回路成功”升级为“回路成功 + request ledger + audit + trace 字段可观测”。
2. 测试目标：`CapabilityServicesSmokeIntegrationTest` 至少覆盖一条 execute/query/catalog 最小闭环正例，以及一条 high-risk execute + live query 的 observability 正例；同时保持 `ctest -N` 与 `ctest -L integration` 的 discoverability 不回退。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_services_smoke_integration_test`
   - `ctest --test-dir build-ci --output-on-failure -R CapabilityServicesSmokeIntegrationTest`
   - `cmake --build build-ci --target dasall_integration_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L integration`

## 6. 风险与回退

1. smoke observability 当前通过 high-risk `toggle` + `idempotency_key` 命中 production 审计路径；若后续 action taxonomy 调整，应更新 smoke 测试输入或 fixture options，而不是放宽 `ExecutionCommandLane` 对 high-risk/critical action 的既有约束。
2. 当前“日志字段可观测”由 loopback request ledger 充当结构化日志替身，因为仓库里尚无单独的 services logging bridge；若未来新增 services logging sink，应把 smoke 断言迁移到正式日志出口，而不是继续扩 request ledger 语义。
3. 本轮不触碰 services integration 注册宏和顶层 target 列表；若未来 smoke 变更再次破坏 `ctest -N` 或 `integration` 标签 discoverability，应回退到 029 的稳定注册基线，而不是在 030 里继续引入新的 CMake 变体。