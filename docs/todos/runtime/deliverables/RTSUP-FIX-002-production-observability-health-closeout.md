# RTSUP-FIX-002 production observability / health closeout

来源任务：RTSUP-FIX-002
完成日期：2026-05-27
关联缺口：RTSUP-GAP-002
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/todos/runtime/DASALL_runtime_support组件专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`

## 1. 任务边界

1. 本轮只收口 `apps/runtime_support` shared helper 的 production observability / health sink 接线与总账 traceability，不扩张到 qemu、installed-package 或 release-runner 证据。
2. authoritative 实现来源固定为 `RTSUP-TODO-006` 已落盘代码与 focused tests；当前 round 的目标是把系统总记录中的旧 `Todo` 状态升级为与专项 TODO 一致的完成态。
3. owner 边界不变：app binary `main.cpp` 仍然持有 runtime dependency owner，`apps/runtime_support` 只负责组合 observability providers、tool/services probes 与 runtime health aggregate。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| infra public seam | `infra/include/ObservabilityLiveComposition.h` 与 `infra/src/ObservabilityLiveComposition.cpp` 已提供 `compose_live_observability()` | shared helper 不再需要穿透 infra internal provider，production logger/audit/metrics/trace/health provider 已有 public composition seam |
| app live composition | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已存在 `RuntimeObservabilityBundle`、`compose_runtime_observability_bundle()` 与 `:production-observability-health` marker | daemon/gateway shared helper 会先组合 shared observability bundle，再把 providers 注入 tools、services 与 runtime health monitor |
| focused tests | `tests/integration/tools/ToolProductionObservabilityIntegrationTest.cpp`、`tests/integration/access/RuntimeProductionHealthCompositionTest.cpp`、`tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp` 已在当前树存在 | 当前树已经具备 direct tool/services sink emit、runtime health aggregate 与 app composition retention 的 focused 验证出口 |
| authoritative traceability | `docs/todos/runtime/DASALL_runtime_support组件专项TODO.md` 的 `RTSUP-TODO-006` 与 `docs/worklog/DASALL_开发执行记录.md` 的记录 #647 已将该实现面记为 Done | 本轮不重复发明新实现，而是把系统级总账与专项 TODO / worklog 对齐 |

## 3. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| 通过 public seam 组合 shared observability providers | `infra/include/ObservabilityLiveComposition.h`、`infra/src/ObservabilityLiveComposition.cpp` |
| 在 runtime_support helper 注入 audit / metrics / trace / health provider | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| 把 tool/services probes 保留到 shared health monitor | `services/include/ServiceLiveComposition.h`、`services/src/ServiceLiveComposition.cpp`、`runtime/include/RuntimeDependencySet.h` |
| 锁定 direct sink emit、health aggregate 与 app retention | `ToolProductionObservabilityIntegrationTest`、`RuntimeProductionHealthCompositionTest`、`DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` |

## 4. 验证

1. `RunCtest_CMakeTools(tests=["ToolObservabilityIntegrationTest","ToolProductionObservabilityIntegrationTest","RuntimeProductionHealthCompositionTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])`
	- 结果：继续命中仓库已知泛化 `生成失败`，未返回 test-level 诊断；不将其判定为功能红灯。
2. `./build/vscode-linux-ninja/tests/integration/tools/dasall_tool_observability_integration_test && ./build/vscode-linux-ninja/tests/integration/tools/dasall_tool_production_observability_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_access_runtime_production_health_composition_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test && ./build/vscode-linux-ninja/tests/integration/access/dasall_access_gateway_runtime_live_dependency_composition_integration_test && printf '%s\n' PASS`
	- 结果：`PASS`。

## 5. 完成判定

满足以下条件时，`RTSUP-FIX-002` 可在系统总记录中标记为 Done：

1. 当前树仍能发现并执行 observability / health focused tests。
2. 总账行项与 runtime_support 专项 TODO 的 `RTSUP-TODO-006` 状态保持一致。
3. 结论保持在 build-tree focused evidence，不越级外推为 installed / qemu / release-ready。

本轮结论：`RTSUP-FIX-002` 可在系统总记录中升级为 Done。shared helper 的 production observability / health 接线已由专项 TODO、记录 #647 与本轮 direct-binary focused 复验共同闭合；当前剩余工作已转向 knowledge 语义与 release/qemu 证据，而不是 shared sink 尚未接入。