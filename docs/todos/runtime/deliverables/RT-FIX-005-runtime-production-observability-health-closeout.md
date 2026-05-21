# RT-FIX-005 runtime production observability / health closeout

来源任务：RT-FIX-005
完成日期：2026-05-21
关联缺口：RT-GAP-005
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/todos/runtime/deliverables/RT-TODO-003-RuntimeDependencySet-seam收敛.md`、`docs/todos/runtime/deliverables/RT-TODO-023-ObservabilityHealthMaintenance设计收敛.md`、`docs/todos/runtime/deliverables/RT-TODO-030-RuntimeSafeMode与HealthMaintenance设计收敛.md`

## 1. 任务边界

1. 本轮只收口 runtime owner 的 production observability / health hot path，不扩张到 installed package、release runner、qemu 或更高层 L3/L4/L5 证据。
2. authoritative 问题定义固定为：`RuntimeLiveDependencyComposition` 必须把 runtime 自己的 `RuntimeEventBus`、`RuntimeTelemetryBridge`、`RuntimeHealthProbe`、`BackgroundMaintenanceHooks` 接入 live `RuntimeDependencySet` 与 shared health monitor；`AgentOrchestrator` 必须在真实 run / continue / waiting 路径发出 transition、budget reject、recovery reject、safe-mode 事件，而不是只保留 runtime readiness 字符串。
3. 用户已明确禁止使用 qemu / kvm；本轮只使用 build-tree focused build 与 direct CTest 作为权威证据。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| dependency carrier | `runtime/include/RuntimeDependencySet.h` 新增 `runtime_event_bus`、`runtime_telemetry_bridge`、`runtime_health_probe`、`background_maintenance_hooks` 四个 runtime control-plane 字段 | runtime production path 不再只能看到 shared audit/metrics/trace/health monitor，而是能显式持有 runtime 自己的 observability / health 组件 |
| app-level live composition | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 现会实例化 runtime event bus / telemetry bridge / maintenance hooks / health probe，并把 `runtime.control_plane` probe 注册进 shared health monitor；`external_evidence` 新增 `runtime-control-plane-observability-wired` marker | daemon / gateway 的 live dependency set 已把 runtime control-plane observability sink 接入 shared production composition，而不是只给 tools/services 注册 probe |
| runtime control-plane health sample | composition 内新增 `RuntimeControlPlaneHealthSignalProvider`，显式采样 event bus drop、maintenance backlog 与 safe-mode state，并保持 health probe 非阻塞 | runtime health snapshot 现在可以表达 runtime 自身的 control-plane 退化，而不是把 tools/services probe 当作全部 health hot path |
| orchestrator emission | `runtime/src/AgentOrchestrator.cpp` 新增 telemetry context / backfill helper 与 scoped transition emitter；`run_once()`、`continue_from_checkpoint()`、`handle_waiting_state()` 现在会把 stage trace、budget reject、recovery reject、safe-mode 决策发到 `RuntimeTelemetryBridge` | runtime 主循环、resume 与 waiting dispatch 已真正消费 RT-TODO-023 的 runtime-private observability 组件，不再停留在“组件存在但 production hot path 没接” |
| focused regression | `RuntimeProductionHealthCompositionTest`、`DaemonRuntimeLiveDependencyCompositionTest` 已扩展断言 runtime control-plane sinks 与第三个 probe；`RuntimeTelemetryBridgeTest`、`RuntimeHealthMaintenanceIntegrationTest`、`AgentOrchestratorSkeletonTest`、`AgentOrchestratorControllerAssemblyTest` 全部通过 | closeout 证据同时覆盖 composition wiring、runtime health aggregate、telemetry bridge、本地主控骨架和控制器装配回归 |

## 3. 设计结论

1. `apps/runtime_support` 继续只是 app-level composition root，不获取 runtime owner 权限；它的职责是把 runtime-private observability 对象装配进 live dependency set，并注册到 shared health monitor。
2. `AgentOrchestrator` 继续是 runtime transition / budget / recovery / safe-mode 事实的 owner；事件发射通过 `RuntimeTelemetryBridge` 完成，不把 logger / metrics / tracing exporter 语义重新塞回 orchestrator。
3. `RuntimeHealthProbe` 在 production composition 里只采样 runtime control-plane 事实，不重复解释 required/optional port matrix；required/optional readiness 仍由 `RuntimeDependencySet::describe_readiness()` 与 `AgentFacade` entrypoint surface 负责。
4. 本轮结论只证明 build-tree production composition 与 runtime hot path wiring 已闭合，不外推为 installed app-binary、release runner 或 qemu machine-isolated observability evidence。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| 在 live dependency set 中保活 runtime control-plane observability 对象 | `runtime/include/RuntimeDependencySet.h` |
| 在 shared app composition 中实例化 runtime event / telemetry / maintenance / health | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| 把 runtime control-plane probe 注册进 shared health monitor | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`tests/integration/access/RuntimeProductionHealthCompositionTest.cpp` |
| 锁定 daemon live dependency composition 不再只保留 tools/services probes | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` |
| 在 orchestrator run / continue / waiting 路径发 transition、budget、recovery、safe-mode 事件 | `runtime/src/AgentOrchestrator.cpp` |
| 保持 telemetry / health runtime-private 组件原有 focused gate 不回退 | `tests/unit/runtime/RuntimeTelemetryBridgeTest.cpp`、`tests/integration/agent_loop/RuntimeHealthMaintenanceIntegrationTest.cpp`、`tests/unit/runtime/AgentOrchestratorSkeletonTest.cpp`、`tests/unit/runtime/AgentOrchestratorControllerAssemblyTest.cpp` |

## 5. D Gate

1. 范围单一：只处理 `RT-FIX-005` / `RT-GAP-005`。
2. 本轮不扩张到 scheduler 模型重构、knowledge degraded semantics、installed package / release runner / qemu 级证据，也不把当前结果外推为 `RT-GAP-006` ~ `RT-GAP-008` 已关闭。
3. 本轮不使用 qemu / kvm；更高层环境证据继续留给后续 runtime / packaging 任务。

## 6. 验证结果

1. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(DaemonRuntimeLiveDependencyCompositionTest|RuntimeProductionHealthCompositionTest)$'`：通过，2/2 通过。
2. `cmake --build build/vscode-linux-ninja --target dasall_runtime_telemetry_bridge_unit_test dasall_runtime_health_maintenance_integration_test dasall_runtime_agent_orchestrator_skeleton_unit_test dasall_runtime_agent_orchestrator_controller_assembly_unit_test`：通过。
3. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(AgentOrchestratorSkeletonTest|AgentOrchestratorControllerAssemblyTest|RuntimeTelemetryBridgeTest|RuntimeHealthMaintenanceIntegrationTest)$'`：通过，4/4 通过。

## 7. 完成判定

1. `RT-GAP-005` 已在当前树关闭：runtime production composition 现在显式持有 runtime 自己的 observability / health sinks，并把 runtime control-plane probe 纳入 shared health aggregate。
2. `AgentOrchestrator` 现在会在 run、continue 与 waiting 路径发出 transition、budget reject、recovery reject、safe-mode 事件，不再只保留 readiness 字符串或 unit-only telemetry bridge。
3. `RuntimeProductionHealthCompositionTest` 与 `DaemonRuntimeLiveDependencyCompositionTest` 已锁定 runtime control-plane wiring；`RuntimeTelemetryBridgeTest`、`RuntimeHealthMaintenanceIntegrationTest`、`AgentOrchestratorSkeletonTest`、`AgentOrchestratorControllerAssemblyTest` 继续证明 runtime-private telemetry / health 行为未回退。
4. 本轮没有改动 runtime owner 边界之外的控制权，也没有引入 qemu / kvm；runtime 章节的下一优先级收敛为 `RT-GAP-006` optional degraded semantics 与 `RT-GAP-007` / `RT-GAP-008` 更高层证据。