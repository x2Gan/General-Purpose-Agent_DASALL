# CAP-TODO-031 Capability Services failure integration 设计收敛

日期：2026-04-09
任务：CAP-TODO-031
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 的 9.1 和 9.3 已冻结 services failure integration 的最小口径：`CapabilityServicesFailureIntegrationTest` 必须以 `integration;failure` 标签覆盖 `adapter timeout`、`partial side effect`、`subscription overflow` 与 `circuit breaker open` 四类关键注入点。
2. [tests/mocks/include/CapabilityServicesLoopbackFixture.h](../../../../tests/mocks/include/CapabilityServicesLoopbackFixture.h) 在 030 完成后只接了 execute/query/catalog 与 audit/trace 注入，尚未暴露 subscription hub、metrics bridge 和 compensation hint lookup，因此 031 若不补 tests-side fixture，就无法在不复制 production 装配的前提下稳定覆盖 overflow 与 partial side effect 语义。
3. [services/src/adapters/RemoteServiceAdapter.cpp](../../../../services/src/adapters/RemoteServiceAdapter.cpp) 已冻结 timeout 语义：当 `timeout_on_invoke=true` 时直接返回 `adapter_unavailable` receipt，不会进入 `invoke_remote` callback。因此 remote timeout 的集成证据必须锚定 adapter receipt 与 metrics stage，而不是误把 callback ledger 当作唯一真相。
4. [services/src/mapping/ResultMapper.cpp](../../../../services/src/mapping/ResultMapper.cpp) 明确规定 `partial_side_effect` 只有在同时携带 `side_effects`、`evidence_refs` 和 `compensation_hints` 时才成立，否则要 fail-close 回退为 `invalid_request`。这决定了 031 必须为 fixture 增加 compensation hint lookup 注入点，而不是放宽 ResultMapper 契约。
5. [services/src/execution/ExecutionSubscriptionHub.cpp](../../../../services/src/execution/ExecutionSubscriptionHub.cpp) 已冻结 overflow 语义：队列溢出时返回 `resync_required=true`、`dropped_count>0`，并通过 `ServiceMetricsBridge::record_subscription_result(...)` 发射 overflow metric。031 只需要把 hub 接回 loopback facade 即可稳定复现该路径。
6. [services/src/execution/ExecutionCommandLane.cpp](../../../../services/src/execution/ExecutionCommandLane.cpp) 已冻结 route failure 观测：`route_unavailable` 会触发 `ServiceMetricsBridge::record_execution_circuit_open(...)`，高风险动作才会发 execution audit。因此 031 的 circuit-open 验证应聚焦 route-unavailable fast-fail 与 circuit metric，不额外发明不存在的审计事件。

## 2. 外部参考

1. CTest 官方手册说明 `ctest -N` 用于 discoverability，`ctest -L <label>` 用于标签过滤执行；这支持 031 同时验证 `integration;failure` 的发现性与执行稳定性，而不把 failure 用例从既有 integration 拓扑中拆出去。参考：https://cmake.org/cmake/help/latest/manual/ctest.1.html

## 3. Design 结论

1. 继续保持 services failure integration 为 tests-side 收敛，不修改任何 services public ABI；仅扩展 `CapabilityServicesLoopbackFixture`，增加 `metrics_bridge`、`lookup_compensation_hints`、`ExecutionSubscriptionHub` 接线与 `publish/subscribe` helper，让测试复用 production `ServiceFacade -> ExecutionCommandLane/DataQueryLane/ExecutionSubscriptionHub -> AdapterBridge` 主链。
2. 新增 `CapabilityServicesFailureIntegrationTest`，把四类 failure injection 分解为四个稳定断言：
   - remote timeout：验证 `AdapterUnavailable -> ProviderTimeout` 映射、retryable/safe_to_replan 语义，以及 remote adapter stage 的 degraded metrics
   - partial side effect：验证 `side_effects` / `compensation_hints` / `evidence_ref` 保留，并通过 high-risk `toggle` 命中 execution requested/completed audit 和 compensation hint metrics
   - subscription overflow：验证 `resync_required` / `dropped_count` / `next_cursor` 不被 facade 吞掉，并稳定发射 `services_subscription_overflow_total`
   - circuit open：验证 route-unavailable fast-fail 不进入后端，并发射 `services_execution_circuit_open_total`
3. remote timeout 场景不依赖 loopback remote request ledger 计数，因为 `RemoteServiceAdapter` timeout 分支在 production 语义上会先短路；本轮把 adapter receipt source ref 与 metrics stage 作为稳定证据，避免把测试绑死在 callback 细节上。
4. 031 完成后，services 集成矩阵已经覆盖 smoke + failure + audit + metrics + trace + health 六类入口，下一直接执行入口收敛到 CAP-TODO-032 的 profile 差异验证。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 扩展 loopback fixture 的 metrics / compensation / subscription 注入点 | tests/mocks/include/CapabilityServicesLoopbackFixture.h |
| 为 services integration 增加 failure 标签入口 | tests/integration/services/CMakeLists.txt |
| 覆盖 timeout / partial / overflow / circuit 四类 failure injection | tests/integration/services/CapabilityServicesFailureIntegrationTest.cpp |
| 回写 031 状态、032 执行入口与验证证据 | docs/todos/services/DASALL_capability_services子系统专项TODO.md、docs/worklog/DASALL_开发执行记录.md |

## 5. Build 三件套

1. 代码目标：扩展 `CapabilityServicesLoopbackFixture` 以承接 `ServiceMetricsBridge`、subscription overflow 和 partial-side-effect compensation hint 注入，并新增 `CapabilityServicesFailureIntegrationTest` 与 `integration;failure` 注册入口。
2. 测试目标：`CapabilityServicesFailureIntegrationTest` 至少覆盖一条 remote timeout、一条 partial side effect、一条 subscription overflow、一条 circuit-open route-unavailable 路径，并保证 failure/integration 聚合链不回退。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_services_failure_integration_test`
   - `ctest --test-dir build-ci --output-on-failure -R CapabilityServicesFailureIntegrationTest`
   - `cmake --build build-ci --target dasall_integration_tests`
   - `ctest --test-dir build-ci -N -L failure`
   - `ctest --test-dir build-ci --output-on-failure -L failure`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
   - `ctest --test-dir build-ci -N`

## 6. 风险与回退

1. 当前 services 的 “circuit open” 仍由 `route_unavailable + services_execution_circuit_open_total` 这条最小代理语义承载，而不是独立 breaker 状态机；若未来 runtime/infra 引入显式 breaker owner，031 的集成断言应切换到新的稳定信号源，而不是继续把 route failure 当唯一来源。
2. partial side effect 的成功判据依赖 `compensation_hints` fail-close 约束；若后续扩展补偿目录或 receipt 字段，必须先更新 ResultMapper/设计文档，再调整 fixture lookup，而不能在测试中绕过这一契约。
3. remote timeout 不以 loopback callback 是否执行作为判据；若后续 adapter 实现改为真正调用 remote stub 再超时，也应以 receipt/result/metric 是否保持冻结语义为回归标准，而不是回退到 callback 计数。