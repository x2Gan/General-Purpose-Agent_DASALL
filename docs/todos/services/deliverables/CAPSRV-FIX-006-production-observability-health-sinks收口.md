# CAPSRV-FIX-006 production observability / health sinks 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `CAPSRV-GAP-006` / `CAPSRV-FIX-006`。
2. 本轮目标：确认当前树已把 Capability Services 的 shared audit / metrics / trace providers 与 `ServiceHealthProbe` 正确接入 production composition hot path，并补齐 services 侧 closeout 交付物、总账与工作日志追溯。
3. 完成判定：`ServiceLiveComposition` 已消费 shared observability providers 并在 live composition 中创建 `ServiceAuditBridge`、`ServiceMetricsBridge`、`ServiceTraceBridge` 与 `ServiceHealthProbe`；`RuntimeLiveDependencyComposition` 已先组合 shared observability bundle，再把 providers 注入 `compose_live_services()` 并把 `services.capability` probe 注册到 runtime health monitor；services-focused integration 与 app-composition focused integration 已给出二值通过证据；本轮不使用 qemu / kvm。

## 2. 本地证据

1. `docs/architecture/DASALL_capability_services子系统详细设计.md` 已冻结 Capability Services 的 observability / health 边界：`ServiceAuditBridge`、`ServiceMetricsBridge`、`ServiceTraceBridge` 与 `ServiceHealthProbe` 都是模块内部组件，`ServiceHealthProbe` 继续通过 `infra::IHealthProbe` / `infra::HealthSnapshot` 进入 infra/health 聚合，而不是升格成新的 services public ABI。
2. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 RuntimeAppCompositionV1 回链已经明确：`apps/runtime_support::compose_minimal_live_dependency_set()` 是 app-level composition root，`visible_tools` / `external_evidence` 只代表 app composition facts，services backend、observability sink 等仍需独立证据闭环。
3. 当前树的 `services/include/ServiceLiveComposition.h` 已暴露 `audit_logger`、`metrics_provider`、`tracer_provider` 与 `health_probe_enabled` 这些 production composition seam；`services/src/ServiceLiveComposition.cpp` 在 `options.observability_enabled` 时创建 `ServiceAuditBridge`、`ServiceMetricsBridge`、`ServiceTraceBridge`，并在 `options.health_probe_enabled` 时创建 `ServiceHealthProbe`。
4. 当前树的 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已先执行 `compose_runtime_observability_bundle()`，随后把 shared providers 注入 `services::compose_live_services()`，并把 `live_services.health_probe` 以 `services.capability` 名称注册到 runtime `health_monitor`；同时写入 `:production-observability-health` evidence marker。
5. 当前树已经有 focused test outlet 覆盖这条热路径：`CapabilityServicesAuditIntegrationTest`、`CapabilityServicesMetricsIntegrationTest`、`CapabilityServicesTraceIntegrationTest`、`CapabilityServicesHealthIntegrationTest` 守住 services 模块内 emit 语义；`ToolProductionObservabilityIntegrationTest`、`RuntimeProductionHealthCompositionTest`、`DaemonRuntimeLiveDependencyCompositionTest` 与 `GatewayRuntimeLiveDependencyCompositionTest` 守住 production-composed shared sink emit、health aggregate 与 app composition retention。
6. `docs/worklog/DASALL_开发执行记录.md` 的记录 #647 已回写过 runtime_support 视角的实现与 focused evidence，但 `docs/todos/DASALL_子系统查漏补缺专项记录.md` 仍将 `CAPSRV-GAP-006` / `CAPSRV-FIX-006` 标记为未完成，services 侧缺少独立 closeout 交付物；当前缺口已收敛为 traceability 未闭环，而不是产品代码未落地。

## 3. 外部参考

1. Mark Seemann 在 Composition Root 总结中指出，对象图应“尽可能靠近应用入口”统一组合，而且 Composition Root 属于应用基础设施组件，而非库内部逻辑。这与 DASALL 当前做法一致：`apps/runtime_support` 负责组合 shared observability bundle，再通过 `ServiceLiveCompositionOptions` 注入 services，而不是让 services/library 代码自己去解析或定位 provider。
2. OpenTelemetry Signals 文档强调 traces、metrics、logs 是观察同一组件运行状态的互补 signals，应组合起来观察系统活动，而不是彼此割裂。这支撑本轮 closeout 的核心判断：Capability Services 的 production evidence 必须同时覆盖 audit / metrics / trace 与 health aggregate，而不是只用单一 fixture 或单条 trace 代表整个 observability hot path。

## 4. 设计结论

### 4.1 根因收口

1. `CAPSRV-GAP-006` 的根因已经不在当前代码树里：shared observability providers 与 `ServiceHealthProbe` 的 production wiring 已经落到 `ServiceLiveComposition` 和 `RuntimeLiveDependencyComposition`，并且 runtime health monitor 已保留 services probe。
2. 当前真正未闭合的是 services 视角的 traceability：总账仍把 `CAPSRV-FIX-006` 标成 Todo，且缺少独立 deliverable 去回链详细设计、runtime_support 组合根与 focused evidence，导致评审者仍会把该缺口误判为产品代码未接线。
3. `CapabilityServicesProductionObservabilityIntegrationTest` 并非唯一可接受出口。只要 services audit/metrics/trace/health integration 与 production-composed tool/runtime/access focused tests 能共同证明 shared sink emit、health aggregate 与 app retention，就可以满足 `CAPSRV-FIX-006` 的完成判定，而无需为了 closeout 再重复造一条功能重叠的新测试。

### 4.2 本轮决定

1. 不重复修改产品代码；以当前树的实现和 focused validation 作为 authoritative source。
2. 新增 services closeout deliverable，并把 `CAPSRV-GAP-006` / `CAPSRV-FIX-006` 在总账与工作日志中同步回写为已闭合。
3. 验收口径保持 build-tree focused evidence，不外推为 installed package、release runner 或 soak 级别已完成；本轮显式绕开 qemu / kvm。
4. `RunCtest_CMakeTools` 在本仓库当前仍对该矩阵返回泛化 `生成失败`，因此 authoritative 结果以 `Build_CMakeTools` 成功构建加 direct binaries `8/8` 通过为准；工具态失败按已知仓库现象记录，不视为代码失败。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | services observability / health 继续保持 internal-only，由 composition root 注入 shared providers | `services/include/ServiceLiveComposition.h`、`services/src/ServiceLiveComposition.cpp` |
| D2 | app-level composition root 先组合 shared observability bundle，再把 providers/probe registration 注入 services | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| D3 | services module 内必须继续守住 audit / metrics / trace / health emit 语义 | `CapabilityServicesAuditIntegrationTest`、`CapabilityServicesMetricsIntegrationTest`、`CapabilityServicesTraceIntegrationTest`、`CapabilityServicesHealthIntegrationTest` |
| D4 | production-composed hot path 必须证明 shared sink emit、health aggregate 与 app retention | `ToolProductionObservabilityIntegrationTest`、`RuntimeProductionHealthCompositionTest`、`DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` |
| D5 | services 侧 closeout 需要回链详细设计、runtime_support 组合根与 focused evidence | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 6. Build 三件套

1. 代码目标：确认当前树中的 `ServiceLiveComposition` / `RuntimeLiveDependencyComposition` 已把 shared audit / metrics / trace providers 和 `ServiceHealthProbe` 接入 production hot path，并保留 services health probe registration。
2. 测试目标：用 services-focused integration 与 app-composition focused integration 共同证明 production-composed services observability / health 行为，而不是只依赖 module-local fixture。
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_services_audit_integration_test","dasall_services_metrics_integration_test","dasall_services_trace_integration_test","dasall_services_health_integration_test","dasall_tool_production_observability_integration_test","dasall_access_runtime_production_health_composition_integration_test","dasall_access_daemon_runtime_live_dependency_composition_integration_test","dasall_access_gateway_runtime_live_dependency_composition_integration_test"])`
   - `RunCtest_CMakeTools(tests=["CapabilityServicesAuditIntegrationTest","CapabilityServicesMetricsIntegrationTest","CapabilityServicesTraceIntegrationTest","CapabilityServicesHealthIntegrationTest","ToolProductionObservabilityIntegrationTest","RuntimeProductionHealthCompositionTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])`：当前仓库仍返回泛化 `生成失败`，authoritative 结果改以同一 build tree 的 direct binaries 为准。
   - `./build/vscode-linux-ninja/tests/integration/services/dasall_services_audit_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/services/dasall_services_metrics_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/services/dasall_services_trace_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/services/dasall_services_health_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/tools/dasall_tool_production_observability_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_runtime_production_health_composition_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_gateway_runtime_live_dependency_composition_integration_test`

## 7. Rollout Checklist

1. `ServiceLiveCompositionOptions` 已保留 shared audit / metrics / trace provider seam 与 `health_probe_enabled` 开关。
2. `ServiceLiveComposition` 已在 production composition 中创建 `ServiceAuditBridge`、`ServiceMetricsBridge`、`ServiceTraceBridge` 与 `ServiceHealthProbe`。
3. `RuntimeLiveDependencyComposition` 已在 shared `health_monitor` 中注册 services probe，并保留 `runtime:<owner>:production-observability-health` evidence marker。
4. services-focused 与 app-composition focused binaries 当前树直接执行 `8/8` 全部通过。
5. 本轮未使用 qemu / kvm，也不把结果外推为 installed / release / soak 证据。
6. 本轮不改产品代码，只补 closeout traceability；后续回归应继续以当前实现文件和 focused test outlet 为 authoritative source。

## 8. 风险与回退

1. 当前闭环只到 build-tree focused evidence，不代表 `CAPSRV-GAP-008` 的 installed package、release runner 或长稳态证据已经完成。
2. `RunCtest_CMakeTools` 对该矩阵的泛化 `生成失败` 仍是仓库工具态问题；如果未来需要 test-level 诊断，需继续保持 direct binary fallback，而不要把工具态误判为功能回退。
3. 本轮没有新增独立 `CapabilityServicesProductionObservabilityIntegrationTest`；后续若现有 tool/runtime/access focused test outlet 被删除或弱化，必须同步补一个等价 production-composed services observability outlet，而不能让 closeout 重新失效。

## 9. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 services composition seam、runtime_support composition root 与 focused evidence outlet。
3. Build 三件套已在本机 build tree 完成，且未使用 qemu / kvm。
4. 范围保持在 `CAPSRV-FIX-006` 的 observability / health closeout，不扩张到 caller-domain owner 或 installed / release 证据。

结论：D Gate = PASS；`CAPSRV-FIX-006` 已按 production observability / health sinks wiring、shared health probe registration 与 focused build-tree evidence 收口。