# CAP-TODO-025 ServiceMetricsBridge 指标桥设计收敛

日期：2026-04-09
任务：CAP-TODO-025
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.10 与 9.1 已冻结 services 指标面：必须覆盖命令/查询、熔断、缓存、订阅 overflow 与补偿提示，并作为 internal observability bridge 输出到 infra/metrics，而不是自建新的公共 ABI 或 profile schema。
2. 同一设计文档在 6.3 / 6.7 / 6.8 明确 `ExecutionCommandLane`、`ExecutionQueryLane`、`DataQueryLane` 与 `ExecutionSubscriptionHub` 分别承接命令、只读查询、缓存查询和订阅 overflow 事实，因此 025 的最小集成点必须收敛在这些 lane / hub 内，而不是让 facade、router 或外部调用方分别拼装 metrics 事件。
3. [infra/include/metrics/IMetricsProvider.h](../../../../infra/include/metrics/IMetricsProvider.h)、[infra/include/metrics/IMeter.h](../../../../infra/include/metrics/IMeter.h)、[infra/include/metrics/MetricTypes.h](../../../../infra/include/metrics/MetricTypes.h) 与 [infra/include/metrics/MetricsErrors.h](../../../../infra/include/metrics/MetricsErrors.h) 已冻结 `IMetricsProvider` / `IMeter` / `MetricIdentity` / `MetricSample` / `MetricsOperationStatus` 抽象边界，因此 services 只能适配既有 infra metrics 契约，不能自造平行 exporter 或标签模型。
4. [services/src/ops/ServiceConfigAdapter.cpp](../../../../services/src/ops/ServiceConfigAdapter.cpp) 与 [services/src/adapters/AdapterRouter.h](../../../../services/src/adapters/AdapterRouter.h) 已在 023 中收口 `observability_bridge_enabled`、`metrics_granularity` 与 `effective_profile_id` 的 internal `ServicePolicyView`，因此 025 可以直接复用统一 policy 基线控制指标启停和粒度，而不新增 `services.*` 顶层键。

## 2. 外部参考

1. OpenTelemetry Metrics API 明确 `MeterProvider` / `Meter` 是指标创建入口，`Counter` 适合记录非负累计事件，而 `Histogram` 适合记录请求时延这类可用于统计分布和分位数的观测值；这支持 025 把请求量/overflow/补偿提示建模为 counter，把时延建模为 histogram，而不是在 services 内部硬编码 P95/P99 计算。参考：https://opentelemetry.io/docs/specs/otel/metrics/api/
2. Prometheus 关于 histogram 的实践说明指出，时延分布应通过 histogram 暴露 count/sum/buckets，由查询侧再推导平均值与分位数；这支持 025 仅冻结 `services_execution_latency_ms` 这一条时延分布指标，而把 P95/P99 留给下游查询和聚合层处理。参考：https://prometheus.io/docs/practices/histograms/
3. 同一 Prometheus 文档强调 histogram 与 counter 的观测值在聚合后仍可计算全局统计，而预计算 summary quantile 不适合多实例聚合；这进一步支持 025 在 bridge 层只输出可聚合的原始请求数、时延分布和 overflow/circuit/compensation 计数。

## 3. Design 结论

1. 新增 internal `ServiceMetricsBridge`，唯一职责是把 services execution/query/data/subscription 事实映射为既有 infra metrics measurement，不引入新的公共 ABI，也不扩张 `services/include` supporting objects。
2. 指标族冻结为六个 internal family：`services_execution_requests_total`、`services_execution_latency_ms`、`services_execution_circuit_open_total`、`services_data_query_requests_total`、`services_subscription_overflow_total`、`services_compensation_hint_total`。其中请求/overflow/补偿/熔断使用 counter，时延使用 histogram。
3. `ServiceMetricsBridge` 复用 `ServicePolicyView` 中的 `observability_bridge_enabled`、`metrics_granularity` 与 `effective_profile_id` 控制启停、粒度和 profile 维度，不新增新的 schema、profile 读取入口或 exporter 配置面。
4. 025 的集成点收敛在 `ExecutionCommandLane`、`ExecutionQueryLane`、`DataQueryLane` 与 `ExecutionSubscriptionHub`：命令车道负责命令请求、route unavailable 的最小 circuit-open 代理与补偿提示计数；查询/data 车道负责 query/catalog 与 cache hit/miss 事实；订阅 hub 负责 `resync_required` / `dropped_count` 场景下的 overflow 指标。
5. metrics provider / meter / instrument 注册或发射失败只在 `ServiceMetricsBridge` 内部标记 degraded 与 `last_metrics_error_code`，不改变命令、查询、缓存与订阅主链结果；这符合详细设计中“metrics/trace exporter 故障不阻断主链路”的 Phase 4 风险约束。
6. `services_execution_circuit_open_total` 当前以命令车道的 `route_unavailable` 作为最小“熔断已打开”代理事实；真正的独立 circuit state 仍留给后续 health / runtime 聚合任务，不在 025 内越权发明新的共享状态。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal services 指标桥与桥接状态对象 | services/src/bridges/ServiceMetricsBridge.h、services/src/bridges/ServiceMetricsBridge.cpp |
| 在命令/查询/data/订阅路径收口 metrics 发射 | services/src/execution/ExecutionCommandLane.h、services/src/execution/ExecutionCommandLane.cpp、services/src/execution/ExecutionQueryLane.h、services/src/execution/ExecutionQueryLane.cpp、services/src/data/DataQueryLane.h、services/src/data/DataQueryLane.cpp、services/src/execution/ExecutionSubscriptionHub.h、services/src/execution/ExecutionSubscriptionHub.cpp |
| 将指标桥接入 services 构建图 | services/CMakeLists.txt |
| 覆盖指标族注册、降级与禁用 no-op 行为 | tests/unit/services/bridges/ServiceMetricsBridgeTest.cpp、tests/unit/services/bridges/CMakeLists.txt |
| 覆盖 ServiceFacade -> lane/query/cache/subscription -> meter 串联 | tests/integration/services/CapabilityServicesMetricsIntegrationTest.cpp、tests/integration/services/CMakeLists.txt |
| 接入 unit/integration 聚合目标 | tests/unit/CMakeLists.txt、tests/integration/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/bridges/ServiceMetricsBridge.h/.cpp`，并把命令、只读查询、数据查询/目录与订阅 overflow 路径统一接到内部指标桥。
2. 测试目标：新增 `tests/unit/services/bridges/ServiceMetricsBridgeTest.cpp` 与 `tests/integration/services/CapabilityServicesMetricsIntegrationTest.cpp`，分别验证指标族发射/退化行为与 facade/lane/query/cache/subscription 的真实串联可观测性。
3. 验收命令：
   - `cmake --build build-ci --target dasall_services dasall_service_metrics_bridge_unit_test dasall_services_metrics_integration_test`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L integration`

## 6. 风险与回退

1. 当前 infra metrics 标签面受既有 `MetricLabels` 约束，025 只能把 action/query/cache/adapter 等事实压缩进稳定的 stage/result/profile/error 维度；若后续需要新增标签族，必须先走 infra/contracts 评审，而不是在 services bridge 内直接扩标签面。
2. `services_execution_circuit_open_total` 当前依赖 `route_unavailable` 这一最小代理事实，并不等同于 runtime 级真正 circuit breaker 状态；若 027 需要健康快照暴露更严格的 circuit 状态，应由 health/runtime 聚合统一收口。
3. 当前 bridge 只记录本地 degraded 状态与 metrics error code，尚未把 exporter 失败聚合进 health probe；026/027 仍需继续补 trace / health 才能形成完整 observability 面。