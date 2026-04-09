# CAP-TODO-027 ServiceHealthProbe 健康探针设计收敛

日期：2026-04-09
任务：CAP-TODO-027
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.3 与 6.10 已冻结 `ServiceHealthProbe` 的输入输出边界：输入只能收敛 `circuit state / adapter readiness / queue stats`，输出为供 infra/health 聚合使用的 `HealthSnapshot`，且组件必须保持 internal-only。
2. 同一设计文档在 6.4、6.9 与 9.1 明确 services 的健康事实只能来自既有 execution/data/system/observability 子域，而不能扩张出新的公共 ABI 或恢复裁定逻辑，因此 027 只能实现事实汇聚与 `IHealthProbe` 兼容输出，不能代替 runtime/infra 做恢复决策。
3. [infra/include/health/IHealthProbe.h](../../../../infra/include/health/IHealthProbe.h)、[infra/include/health/ProbeTypes.h](../../../../infra/include/health/ProbeTypes.h) 与 [infra/include/health/HealthStateTypes.h](../../../../infra/include/health/HealthStateTypes.h) 已冻结 readiness probe 与 `HealthSnapshot` 抽象边界，因此 services health 必须同时兼容 `ProbeResult` 输出和本地 `HealthSnapshot` 事实快照，而不是自定义平行 health schema。
4. [services/src/system/SystemSnapshotLane.h](../../../../services/src/system/SystemSnapshotLane.h)、[services/src/execution/ExecutionSubscriptionHub.h](../../../../services/src/execution/ExecutionSubscriptionHub.h) 与 [services/src/ops/ServiceConfigAdapter.h](../../../../services/src/ops/ServiceConfigAdapter.h) 已分别提供 system snapshot、queue overflow 事实和固定 policy 基线，因此 027 可以基于这些既有内部事实定义统一 `ServiceHealthSample`，不新增 `services.*` 顶层 schema。
5. [tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) 与 [tests/integration/CMakeLists.txt](../../../../tests/integration/CMakeLists.txt) 在本轮前没有把 services trace/health 测试完整纳入顶层聚合 target；027 同步补齐这些 target 注册，确保 TODO 验收命令里的 `dasall_unit_tests` / `dasall_integration_tests` 真正覆盖 services observability 增量。

## 2. 外部参考

1. Kubernetes 对 liveness/readiness probe 的区分说明，服务可以保持存活但暂时不 ready 接流量；这直接支持 027 把 circuit open、adapter down、queue overflow 映射为 `liveness=true`、`readiness=false`、`degraded=true`，而不是一律标成进程级失败。参考：https://kubernetes.io/docs/concepts/configuration/liveness-readiness-startup-probes/
2. Azure Architecture Center 的 Circuit Breaker 模式说明，后端不可用或错误率过高时应短路请求并向上游暴露非 ready 事实；这支持 027 把 `route_unavailable` 收敛为 services 的 circuit-open 健康事实，而不是在 health 路径继续尝试后端。参考：https://learn.microsoft.com/azure/architecture/patterns/circuit-breaker
3. Azure Architecture Center 的 Queue-Based Load Leveling 模式强调队列饱和和溢出是典型的过载信号，应显式暴露 backpressure 状态；这支持 027 把 command/subscription queue 的 high-watermark、overflow 和 `resync_required` 统一纳入 degraded/readiness 输出。参考：https://learn.microsoft.com/azure/architecture/patterns/queue-based-load-leveling

## 3. Design 结论

1. 新增 internal `ServiceHealthProbe`，实现 `infra::IHealthProbe`，并通过 internal `IServiceHealthSignalProvider` 消费 services 已存在的健康事实，不引入新的公共 ABI 或跨模块 contracts。
2. `ServiceHealthProbe` 同时产出两类结果：对 infra/health 暴露 `ProbeResult`，对 services 内部保留最近一次 versioned `HealthSnapshot`。这样既能接入 health monitor，也能在 services 内保留统一的 readiness/degraded/circuit 事实快照。
3. readiness block 条件固定为：`circuit_state=open/unknown`、`adapter_readiness=unavailable/unknown`、command/subscription queue 触达 high-watermark 或出现 overflow/`resync_required`。这些场景保持 `liveness=true`，但输出 `readiness=false`、`degraded=true`。
4. warning-only 条件固定为：`circuit_state=half_open`、`adapter_readiness=degraded`、`system_snapshot_degraded=true`、`audit/metrics/trace bridge degraded=true`。这些场景保持 `liveness=true`、`readiness=true`、`degraded=true`，只表达性能或观测退化，不越权裁定服务完全不可用。
5. `system_snapshot_ready=false` 被视为 health sampling 本身无法形成可信服务事实，故输出 `liveness=false`、`readiness=false` 的 unhealthy snapshot；provider 缺失或 sample 非法则通过 `ValidationFieldMissing` 和 `ProbeStatus::Unknown` fail-closed。
6. 本轮同步补齐 services trace/health 测试到顶层 unit/integration 聚合 target，修复“测试可 discover，但 `dasall_unit_tests` / `dasall_integration_tests` 不一定先编译对应可执行文件”的构建偏差。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal health probe、sample/provider 和 snapshot 映射 | services/src/ops/ServiceHealthProbe.h、services/src/ops/ServiceHealthProbe.cpp |
| 将 health probe 接入 services 构建图 | services/CMakeLists.txt |
| 覆盖 circuit open、adapter down、queue overflow 单测 | tests/unit/services/ops/ServiceHealthProbeTest.cpp、tests/unit/services/ops/CMakeLists.txt |
| 覆盖 command route-unavailable + subscription overflow 的 services 级健康收敛 | tests/integration/services/CapabilityServicesHealthIntegrationTest.cpp、tests/integration/services/CMakeLists.txt |
| 补齐顶层 unit/integration 聚合 target 对 services trace/health 的编译依赖 | tests/unit/CMakeLists.txt、tests/integration/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/ops/ServiceHealthProbe.h/.cpp`，固定 `ServiceHealthSample`、queue pressure 判定、circuit/adapter readiness 映射、`ProbeResult` 输出与 versioned `HealthSnapshot` 收敛逻辑。
2. 测试目标：新增 `tests/unit/services/ops/ServiceHealthProbeTest.cpp` 与 `tests/integration/services/CapabilityServicesHealthIntegrationTest.cpp`，分别验证 `circuit open`、`adapter down`、`queue overflow` 场景，以及 command/subscription 事实到 health snapshot 的整合。
3. 验收命令：
   - `cmake --build build-ci --target dasall_services dasall_unit_tests dasall_integration_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L integration`

## 6. 风险与回退

1. 当前 `ServiceHealthProbe` 仍通过 signal provider 接收 circuit/adapter/queue/observability 事实，还没有在 services 运行时完成全自动接线；后续若需要长期驻留采样，必须在不破坏 internal-only 边界的前提下，把 provider 与现有 lanes/bridges 做组合根接入。
2. `route_unavailable` 目前仍是 services 本地“circuit open”最小代理事实，并不等价于 runtime 级独立 circuit breaker；若后续需要更严格的 circuit 状态机，应由 runtime/infra 统一设计后再接回 services health。
3. health monitor 当前仍是占位式聚合实现，027 只保证 `ServiceHealthProbe` 自身的 probe/snapshot 语义稳定；若要让 infra/health 真正消费 services snapshot 并驱动系统级 transition，还需要后续健康聚合链路继续收口。