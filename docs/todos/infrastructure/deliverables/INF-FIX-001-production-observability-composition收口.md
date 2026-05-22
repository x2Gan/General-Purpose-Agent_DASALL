# INF-FIX-001 production observability composition 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `INF-GAP-001` / `INF-FIX-001`。
2. 本轮目标：确认 infra shared observability composition 已经通过 app-level composition root 注入 runtime_support、access、services、tools、llm、memory、knowledge、cognition，并补齐 infrastructure 侧 deliverable / 总账 / worklog 的 traceability 闭环。
3. 完成判定：`infra::compose_live_observability()` 与 `apps/runtime_support::compose_minimal_live_dependency_set()` 已把 shared logger / audit / metrics / trace / health provider 组合并注入下游子系统；focused build-tree tests 能证明跨子系统 audit / metric / trace / health 信号可观测；本轮不使用 qemu / kvm，也不外推为 installed / release / soak 证据。

## 2. 本地证据

1. `docs/architecture/DASALL_infrastructure子系统详细设计.md` 已把 logging / audit / tracing / metrics 定义为 Infra 的统一基础能力，并要求上层消费者通过 application-owned composition 使用这些能力，而不是在各库内部自建第二条组合根。
2. `infra/include/ObservabilityLiveComposition.h` 与 `infra/src/ObservabilityLiveComposition.cpp` 已落盘 `compose_live_observability()`，统一返回 concrete `logger`、`audit_logger`、`metrics_provider`、`tracer_provider` 与 `health_monitor`。
3. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已定义 `RuntimeObservabilityBundle` 与 `compose_runtime_observability_bundle()`，先组合 shared observability bundle，再把同一组 provider 注入 memory、llm、cognition、response builder、services、knowledge，并把 tool / runtime / services probes 注册到 shared `health_monitor`。
4. 同文件中的 `compose_minimal_live_dependency_set()` 已把 `audit_logger`、`metrics_provider`、`tracer_provider`、`health_monitor` 保留在 `RuntimeDependencySet`，并回写 `runtime:<owner>:production-observability-health` 与 runtime control-plane observability markers，说明 access/app owner 证据已保留在 daemon / gateway live composition。
5. focused tests 已覆盖各子系统的 production observability outlet：
   - `tests/integration/tools/ToolProductionObservabilityIntegrationTest.cpp`
   - `tests/integration/services/CapabilityServicesTraceIntegrationTest.cpp`
   - `tests/integration/llm/LLMProductionObservabilityIntegrationTest.cpp`
   - `tests/integration/memory/MemoryObservabilityBridgeTest.cpp`
   - `tests/unit/knowledge/KnowledgeTelemetryTest.cpp`
   - `tests/integration/cognition/CognitionProductionTelemetryIntegrationTest.cpp`
   - `tests/integration/access/RuntimeProductionHealthCompositionTest.cpp`
   - `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`
   - `tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp`
6. `docs/todos/runtime/DASALL_runtime_support组件专项TODO.md` 的 `RTSUP-TODO-006` 已将 `INF-GAP-001` 对应实现面标为 Done，因此当前真实缺口不是“生产接线未落地”，而是 infrastructure 顶层总账仍未把既有实现与 focused evidence 回链闭合。

## 3. 外部参考

1. Mark Seemann 在 Composition Root 一文中明确指出，组合根应尽可能靠近应用入口，且 Composition Root 属于 application infrastructure component。这与 DASALL 当前结构一致：infra 只提供 shared observability builder，真正的跨子系统装配由 `apps/runtime_support` 这个 app-level composition root 持有。
2. OpenTelemetry Signals 文档强调 traces、metrics 与 logs 是观察同一系统活动的互补 signals，应组合起来从不同角度观察同一技术组件。这支持本轮验收口径：INF-FIX-001 不能只看单一 trace 或单一 audit outlet，而要看跨子系统 shared signals 是否一起可观测。

## 4. 设计结论

### 4.1 根因收口

1. `INF-GAP-001` 的根因已不在当前产品代码：shared observability composition helper 与 runtime_support app-level composition root 已落盘，并已把同一组 provider 注入 services / tools / llm / memory / knowledge / cognition，同时保留 access/app owner 证据。
2. 当前真正未闭合的是 infrastructure 视角的 traceability：总账仍把 `INF-GAP-001` / `INF-FIX-001` 标成开放状态，而子系统 TODO、focused tests 与 runtime_support worklog 已把同一实现面记为完成。
3. 因此本轮最小可执行动作不是重复改写 observability 代码，而是新增 infra closeout deliverable，并把总账 / worklog 同步回写为已闭合。

### 4.2 边界与不外推项

1. 本轮 authoritative 边界只到 build-tree focused composition evidence，不外推到 installed package、qemu、kvm、release runner 或 soak。
2. 本轮不改写各子系统既有 fail-open / fail-closed 语义；这些策略继续由 tools / services / llm / memory / cognition / knowledge 各自 owner 的 bridge 与 integration tests 约束。
3. 若后续 focused tests 回退，应优先回到 `compose_live_observability()` 与 `compose_runtime_observability_bundle()` 这两个组合根检查接线，而不是在子系统内重复增加第二条 local wiring。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | infra 必须提供 shared observability composition helper，而不是让各子系统各自建根 | `infra/include/ObservabilityLiveComposition.h`、`infra/src/ObservabilityLiveComposition.cpp` |
| D2 | app-level composition root 必须在同一处把 shared providers 注入 memory / llm / cognition / services / knowledge / tools / access owner | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| D3 | 子系统 focused tests 必须共同证明 shared audit / metric / trace / health signals 可观测 | `ToolProductionObservabilityIntegrationTest`、`CapabilityServicesTraceIntegrationTest`、`LLMProductionObservabilityIntegrationTest`、`MemoryObservabilityBridgeTest`、`KnowledgeTelemetryTest`、`CognitionProductionTelemetryIntegrationTest` |
| D4 | app owner 侧必须保留 runtime/access composition retention 与 evidence marker | `RuntimeProductionHealthCompositionTest`、`DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` |
| D5 | infra 顶层 deliverable、总账与 worklog 必须回链既有实现与 focused evidence | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 6. Build 三件套

1. 代码目标：不重复修改产品代码；以 `compose_live_observability()`、`compose_runtime_observability_bundle()` 与既有 focused tests 为 authoritative source，完成 INF-FIX-001 的 infra 侧 closeout traceability。
2. 测试目标：验证 shared observability providers 继续覆盖 tools、services、llm、memory、knowledge、cognition，以及 runtime/access owner retention。
3. 验收命令：
   - `cmake --build build-ci --target dasall_tool_production_observability_integration_test dasall_services_trace_integration_test dasall_llm_production_observability_integration_test dasall_memory_observability_bridge_integration_test dasall_knowledge_telemetry_unit_test dasall_cognition_production_telemetry_integration_test dasall_access_runtime_production_health_composition_integration_test dasall_access_daemon_runtime_live_dependency_composition_integration_test dasall_access_gateway_runtime_live_dependency_composition_integration_test`
   - `ctest --test-dir build-ci -R '^(ToolProductionObservabilityIntegrationTest|CapabilityServicesTraceIntegrationTest|LLMProductionObservabilityIntegrationTest|MemoryObservabilityBridgeTest|KnowledgeTelemetryTest|CognitionProductionTelemetryIntegrationTest|RuntimeProductionHealthCompositionTest|DaemonRuntimeLiveDependencyCompositionTest|GatewayRuntimeLiveDependencyCompositionTest)$' --output-on-failure`

## 7. Rollout Checklist

1. `INF-GAP-001` 不再保留为开放缺口。
2. `INF-FIX-001` 必须回链到 infra helper、runtime_support composition root 与跨子系统 focused tests，而不是空泛写成“已接入”。
3. deliverable、总账、worklog 三处口径一致：build-tree production composition 已完成，但 installed / qemu / kvm / release / soak 仍未完成。
4. 本轮禁止使用 qemu / kvm；所有验收只依赖本地源码树、focused tests 与文档回写。

## 8. 风险与回退

1. 若把本轮 focused evidence 外推为 installed / release 级就绪，会放大当前证明边界；本轮明确禁止该外推。
2. 若后续只保留子系统 local observability tests、删除 runtime/access owner retention tests，INF-FIX-001 会重新失去 app-level composition 证据；届时必须补等价 owner-level outlet。
3. 若为了 closeout 再次重写 shared observability builder，容易破坏既有各子系统桥接语义；本轮不采用该路线。

## 9. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 infra helper、runtime_support composition root、focused tests 与总账回写。
3. Build 三件套已锁定，且不依赖 qemu / kvm。
4. 范围保持在 `INF-FIX-001` 的 production observability composition closeout，不扩张到 diagnostics、optional backend 或 release runner 证据。

结论：D Gate = PASS；`INF-FIX-001` 可按既有实现与 focused build-tree evidence 收口。