# WP-RT-GAP-001 Audit / Metrics 强制 sink 收口

日期：2026-07-16
任务：WP-RT-GAP-001
对应缺口：GAP-P0-A
状态：Done

## 1. 执行前提

1. [docs/deliverables/RT-EVAL-2026-05-31-runtime子系统落地评估与生产级缺口治理任务规划.md](../../deliverables/RT-EVAL-2026-05-31-runtime%E5%AD%90%E7%B3%BB%E7%BB%9F%E8%90%BD%E5%9C%B0%E8%AF%84%E4%BC%B0%E4%B8%8E%E7%94%9F%E4%BA%A7%E7%BA%A7%E7%BC%BA%E5%8F%A3%E6%B2%BB%E7%90%86%E4%BB%BB%E5%8A%A1%E8%A7%84%E5%88%92.md) 已把本任务固定为“在 AgentFacade.init 挂 audit/metrics sink subscriber，并由 RuntimePolicySnapshot 投影 fail-closed / fail-open 语义”。
2. `infra::audit::IAuditLogger` 与 `infra::metrics::IMetricsProvider` public interface 已冻结，无需另拆前置 BLOCK 解组任务。
3. 现有 runtime production composition 已提供 `RuntimeEventBus` / `RuntimeTelemetryBridge` / `audit_logger` / `metrics_provider`，缺口集中在 default sink forwarding 与 readiness fail-closed，而不是 observability owner 不存在。

## 2. 外部参考

1. OpenTelemetry Metrics API 说明 `Counter` 适用于“count the number of requests completed / errors / checkpoints run” 这类单调事件计数；本轮据此把 runtime control-plane 关键事件投影为 `runtime_control_plane_event_total` 计数器，而不是误用 gauge 或 histogram。

## 3. 改动

1. 更新 `profiles/include/RuntimePolicySnapshot.h` 与 `profiles/src/RuntimePolicyProvider.cpp`，新增 `RuntimeSinkPolicy{ fail_closed_on_audit_failure, drop_on_metrics_failure }`，并从 `ops_policy.runtime_sink.*` 投影到运行时快照。
2. 更新 `profiles/desktop_full/runtime_policy.yaml`、`profiles/edge_balanced/runtime_policy.yaml`、`profiles/edge_minimal/runtime_policy.yaml`、`profiles/cloud_full/runtime_policy.yaml`、`profiles/factory_test/runtime_policy.yaml`，显式区分 production-like profile 的 fail-closed 行为与 `edge_minimal` 的 metrics best-effort 行为。
3. 更新 `runtime/src/AgentFacade.cpp`，在 `init()` 阶段按 `RuntimeSinkPolicy` 扩展 readiness：当 audit/metrics/event-bus 在当前策略下属于 required 时，缺失即 fail-closed；初始化成功后自动为关键 runtime 事件安装 audit sink subscriber 与 metrics sink subscriber，并在 `stop()` 解除订阅。
4. 更新 `tests/unit/runtime/RuntimeSinkForwardingTest.cpp` 与 `tests/unit/runtime/CMakeLists.txt`，新增 `RuntimeAuditSinkForwardingTest`、`RuntimeMetricsSinkForwardingTest`、`RuntimeSinkFailClosedTest`。
5. 更新 `tests/integration/agent_loop/RuntimeProductionLoggingIntegrationTest.cpp`，先经 `AgentFacade.init()` 安装 default sink subscribers，再断言 audit-marked runtime events 已进入 live audit sink。
6. 更新 `tests/unit/profiles/RuntimePolicySnapshotTest.cpp` 与 `tests/unit/profiles/RuntimePolicyProviderTest.cpp`，锁定 sink policy 默认值与 YAML 投影不回退。
7. 更新 [docs/architecture/DASALL_runtime子系统详细设计.md](../../architecture/DASALL_runtime%E5%AD%90%E7%B3%BB%E7%BB%9F%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md)，把 `RuntimeSinkPolicy`、AgentFacade init default sink subscriber、required/optional observability port 语义回写到详设。

## 4. 验证

1. `cmake --build build-ci --target dasall_runtime_sink_forwarding_unit_test`
   - 结果：通过。
2. `ctest --test-dir build-ci -R "RuntimeAuditSink|RuntimeMetricsSink|RuntimeSinkFailClosed" --output-on-failure`
   - 结果：通过，3/3。
3. `ctest --test-dir build-ci -R "RuntimePolicySnapshot|RuntimePolicyProvider" --output-on-failure`
   - 结果：通过，2/2。
4. `cmake --build build-ci --target dasall_runtime_production_logging_integration_test && ctest --test-dir build-ci -R "RuntimeProductionLogging" --output-on-failure`
   - 结果：通过，1/1。
5. `ctest --test-dir build-ci -R "RuntimeAuditSink|RuntimeMetricsSink|RuntimeSinkFailClosed|RuntimeProductionLogging" --output-on-failure`
   - 结果：通过，4/4；与任务规划文档中的验收正则一致。

## 5. 结果

1. `WP-RT-GAP-001 / GAP-P0-A` 已闭合：runtime 不再只把关键 control-plane 事件留在 module-local telemetry record / event queue，audit/metrics sink 现已具备默认强制 forwarding。
2. `RuntimeSinkPolicy` 已成为 profile 投影的一部分，runtime init 现在能把 observability sink 缺口按 required/optional 语义显式折叠为 fail-closed 或 degraded。
3. build-tree focused evidence 已覆盖 unit forwarding、init fail-closed 与 live production composition audit persistence；更高层 installed/qemu/soak 任务继续留在后续 WP-RT-GAP-005 / 019 等工作包收口。