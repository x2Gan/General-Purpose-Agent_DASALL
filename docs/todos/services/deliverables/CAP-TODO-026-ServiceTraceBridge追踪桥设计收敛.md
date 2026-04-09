# CAP-TODO-026 ServiceTraceBridge 追踪桥设计收敛

日期：2026-04-09
任务：CAP-TODO-026
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.10 与 7.1 已冻结 services trace 输出面：必须覆盖 `ServiceFacade span`、lane span、adapter span、external target span，并保证 `trace_id` / `span_id` / `parent_span_id` 可把 Tool -> Services -> Adapter -> External 串成一条可验证链路。
2. 同一设计文档在 6.3 / 6.7 / 6.8 明确 `ServiceFacade` 只负责统一语义入口，`ExecutionCommandLane` / `ExecutionQueryLane` / `ExecutionDiagnoseService` / `DataQueryLane` 承接 lane 内语义，而 `AdapterBridge` 是唯一真正执行 adapter 调用的边界，因此 026 的最小接入点必须收敛在 facade、lane 与 adapter bridge，而不是把 span 拼装分散到调用方。
3. [infra/include/tracing/ITracerProvider.h](../../../../infra/include/tracing/ITracerProvider.h)、[infra/include/tracing/ITracer.h](../../../../infra/include/tracing/ITracer.h)、[infra/include/tracing/ISpan.h](../../../../infra/include/tracing/ISpan.h) 与 [infra/include/tracing/TraceTypes.h](../../../../infra/include/tracing/TraceTypes.h) 已冻结 tracer provider、tracer、span、trace context 与 attribute 边界，因此 services 只能适配既有 infra tracing 抽象，不能自建平行 tracer/exporter。
4. [services/src/ops/ServiceConfigAdapter.cpp](../../../../services/src/ops/ServiceConfigAdapter.cpp) 与 [services/src/adapters/AdapterRouter.h](../../../../services/src/adapters/AdapterRouter.h) 已在 023 中收口 `effective_profile_id`、`observability_bridge_enabled` 与 `trace_sample_ratio` 的 internal policy 基线，因此 026 直接复用这些内部派生字段控制 trace scope 与采样语义，而不新增 `services.*` 顶层 schema。

## 2. 外部参考

1. OpenTelemetry Traces Concepts 明确一个 trace 由 root span 与层层嵌套的 child spans 组成，父子 span 通过共享 `trace_id` 与 child `parent_id` 建立关系；这直接支持 026 把 `ServiceFacade -> lane -> adapter -> external` 固定为严格的 parent-child 链，而不是仅做并列打点。参考：https://opentelemetry.io/docs/concepts/signals/traces/
2. OpenTelemetry Trace API 说明 `Tracer` 是 span 创建入口，client/server/internal span kind 用于表达边界语义；这支持 026 把 facade 标为 server、lane 标为 internal、adapter/external 标为 client，而不在 services 内发明新的 span kind。参考：https://opentelemetry.io/docs/specs/otel/trace/api/
3. W3C Trace Context 规定远端链路应以 `trace-id` / `parent-id` 形式传播跨进程上下文；这支持 026 在当前 supporting objects 仍只有 `trace_id` / `tool_call_id` / `request_id` 的前提下，把 raw caller fields 规范化为稳定的 lowercase hex remote parent context，而不越权扩张公共请求对象。参考：https://www.w3.org/TR/trace-context/

## 3. Design 结论

1. 新增 internal `ServiceTraceBridge`，唯一职责是把 services execution/data 路径映射为既有 infra tracing span，不引入新的公共 ABI，也不把 tracer/exporter 细节泄漏到 `services/include`。
2. trace scope 固定为 `services` / `v1` / `https://opentelemetry.io/schemas/1.26.0`，并通过 `ServiceTraceBridgeOptions` 保留 `profile_id` 与 `trace_sample_ratio` 这些已冻结 internal policy 字段。
3. `ServiceFacade` 成为 root span 入口，`ExecutionCommandLane`、`ExecutionQueryLane`、`ExecutionDiagnoseService`、`DataQueryLane` 成为 lane span 入口，`AdapterBridge` 成为 adapter 与 external target span 的唯一接入点，保证跨 execution/data 子域仍复用同一 trace bridge。
4. `ServiceTraceBridge` 会把 incoming `trace_id` / `tool_call_id` / `request_id` 规范化为 remote parent context：若来值已经是合法 lowercase hex，则直接复用；否则以稳定哈希映射出最小可验证的 32/16 位 hex trace/span id，保证主链不因上游 trace token 非标准而失去链路关联。
5. `AdapterBridge` 必须在 adapter span active scope 内再启动 external span；本轮 integration test 明确暴露并修复了 external span 原先挂在 lane span 下的问题，最终形成严格的 Tool -> Facade -> Lane -> Adapter -> External 父子链。
6. tracer provider 缺失、start/end 异常等 tracing 基础设施故障只在 `ServiceTraceBridgeStatus` 中标记 degraded，不替换命令/查询主链结果；而业务侧 `partial` / `timeout` / `unreachable` receipt 只会把 span 记为 `Error`，不会误报为 bridge 自身故障。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal services 追踪桥与状态对象 | services/src/bridges/ServiceTraceBridge.h、services/src/bridges/ServiceTraceBridge.cpp |
| 在 facade、lane 与 adapter 边界统一接入 trace | services/src/ServiceFacade.h、services/src/ServiceFacade.cpp、services/src/execution/ExecutionCommandLane.h、services/src/execution/ExecutionCommandLane.cpp、services/src/execution/ExecutionQueryLane.h、services/src/execution/ExecutionQueryLane.cpp、services/src/execution/ExecutionDiagnoseService.h、services/src/execution/ExecutionDiagnoseService.cpp、services/src/data/DataQueryLane.h、services/src/data/DataQueryLane.cpp、services/src/adapters/AdapterBridge.h、services/src/adapters/AdapterBridge.cpp |
| 将 trace bridge 接入 services 构建图 | services/CMakeLists.txt |
| 覆盖 remote parent、from_cache、provider missing 与 adapter receipt error 语义 | tests/unit/services/bridges/ServiceTraceBridgeTest.cpp、tests/unit/services/bridges/CMakeLists.txt |
| 覆盖 Tool -> Facade -> Lane -> Adapter -> External 全链父子关系 | tests/integration/services/CapabilityServicesTraceIntegrationTest.cpp、tests/integration/services/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/bridges/ServiceTraceBridge.h/.cpp`，并把 `ServiceFacade`、execution/data/diagnose lanes 与 `AdapterBridge` 接到统一 trace bridge；同时修正 adapter/external span 的激活顺序，保证 external span 真正挂在 adapter span 下。
2. 测试目标：新增 `tests/unit/services/bridges/ServiceTraceBridgeTest.cpp` 与 `tests/integration/services/CapabilityServicesTraceIntegrationTest.cpp`，分别验证 span 属性/降级语义与真实 services 调用链父子关系。
3. 验收命令：
   - `cmake --build build-ci --target dasall_services dasall_unit_tests dasall_integration_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L integration`

## 6. 风险与回退

1. 当前 remote parent 仍只消费 `trace_id` / `tool_call_id` / `request_id` 这组最小 fields，尚未承接完整 `tracestate` / baggage；如果后续需要跨进程携带更完整 propagation 元数据，应先走 infra tracing / supporting object 评审，而不是在 services bridge 内直接扩字段。
2. 026 只收口 trace span 与 bridge degraded 状态，还没有把 trace exporter 故障聚合进统一 health snapshot；027 仍需把 trace/audit/metrics 的局部 degraded 状态提升为 services 的 readiness/degraded/circuit 事实。
3. 当前 adapter span 名称固定按 `route_kind` 维度编码，不直接编码 action/query kind；若后续观测系统要求更细的 adapter stage taxonomy，应先回写详细设计和 metrics/trace 对齐规则，再统一调整，而不是局部修改命名约定。