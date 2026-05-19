# TOOL-FIX-007 production tools observability sink 收敛

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-FIX-007`。
2. 本轮目标：以真实目标为准复核 tools production observability sink 是否已进入 app live composition；若实现已存在，则完成 tools 总账、交付物与工作日志的 traceability 闭环，而不是重复扩写已落地代码。
3. 完成判定：`RuntimeLiveDependencyComposition` 已把 shared audit / metrics / trace sinks 注入 tools live path；focused tests 已能证明 production-composed manager 与 daemon / gateway app composition 发出真实 observability evidence；本轮文档回写后 `TOOL-GAP-007` / `TOOL-FIX-007` 可二值闭合。

## 2. 本地证据

1. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已定义 `RuntimeObservabilityBundle`，并通过 `compose_runtime_observability_bundle()` 组合 shared `audit_logger`、`metrics_provider`、`tracer_provider`、`health_monitor` 与 tools/service probes。
2. 同文件中的 `compose_runtime_tool_manager()` 已把 `tool_metrics_bridge`、`tool_trace_bridge` 与 `ToolAuditBridge::bind_hooks(tool_audit_bridge)` 注入 `ToolManagerDependencies`，说明 tools live path 并未停留在 `ToolManager::default_dependencies()` 的 disabled bridge。
3. `compose_minimal_live_dependency_set()` 已将 `compose_runtime_tool_manager(...)` 的结果保存在 `RuntimeDependencySet.tool_manager`，并回写 `:production-observability-health` evidence marker，说明 app live composition 已保留 observability/health owner。
4. `tests/integration/tools/ToolProductionObservabilityIntegrationTest.cpp` 已覆盖 shared audit sink、metrics facade、trace provider、tool/services health probe registration 和 live action success path，断言 tool 与 service audit action 同时进入 shared sink，metrics/trace 也进入 concrete provider。
5. `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` 与 `tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp` 已断言 app live composition 保留 concrete `health_monitor`，并记录 `runtime:*:production-observability-health` evidence marker。
6. `docs/todos/runtime/DASALL_runtime_support组件专项TODO.md` 与 `docs/worklog/DASALL_开发执行记录.md` 现已把同一实现面记为 `RTSUP-TODO-006` 已完成；因此本轮真实缺口不是“未接入”，而是 tools 顶层总账尚未把已有实现和 focused evidence 回链闭合。

## 3. 设计结论

### 3.1 根因重判

1. `TOOL-FIX-007` 的真实缺口已经从“production sink 未接入”收缩为“tools 总账缺少对既有 runtime_support / tools focused evidence 的同步回写”。
2. `ToolManager::default_dependencies()` 中的 disabled metrics / trace bridge 只代表 standalone 默认构造基线，不代表 production-composed manager 的最终 observability 配置。
3. 只要 `RuntimeLiveDependencyComposition` 继续通过 shared observability bundle 显式注入 concrete bridges，并由 focused tests 证明 audit / metrics / trace / health 信号可观测，就不应再把该项维持为 Todo。

### 3.2 边界与不外推项

1. 本轮只闭合 build-tree focused production composition 证据，不外推到 installed package、qemu、kvm、release runner 或 soak。
2. 本轮不新增 transport、builtin wrapper 或 skill runtime 扩张；这些仍属于 `TOOL-FIX-008` / `TOOL-FIX-009` / `TOOL-FIX-010` 的后续范围。
3. observability bridge backend failure 语义继续保持 fail-open；本轮只确认 shared sink 注入与可观测性，不重写既有 fail-open / fail-closed 设计边界。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | 复核 runtime_support 已显式注入 shared observability bundle 到 tools live path | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| D2 | 复核 tools focused production observability tests 已覆盖 shared audit / metrics / trace sink | `tests/integration/tools/ToolProductionObservabilityIntegrationTest.cpp` |
| D3 | 复核 app live composition 已保留 health/marker 证据 | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp` |
| D4 | 将既有实现与 focused evidence 回写到 tools deliverable、总账与工作日志 | `docs/todos/tools/deliverables/TOOL-FIX-007-production-tools-observability-sink收敛.md`、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |
| D5 | 在 tools deliverables index 中补充索引，避免后续评审只能绕到 runtime_support TODO 查询 | `docs/todos/tools/deliverables/DELIVERABLES-INDEX.md` |

## 5. Build 三件套

1. 代码目标：不重复扩写已有 production observability 代码；以 `RuntimeLiveDependencyComposition`、`ToolProductionObservabilityIntegrationTest` 与 daemon/gateway composition tests 的既有实现为 authoritative source，完成 tools 侧 traceability 回写。
2. 测试目标：验证 tools live path 与 app live composition 继续命中 concrete audit / metrics / trace / health sinks；至少覆盖 `ToolObservabilityIntegrationTest` 与 `ToolProductionObservabilityIntegrationTest` 的 observability 断言，并复核 daemon / gateway composition retention。
3. 验收命令：
   - `cmake --build build-ci --target dasall_tool_observability_integration_test dasall_tool_production_observability_integration_test dasall_access_daemon_runtime_live_dependency_composition_integration_test dasall_access_gateway_runtime_live_dependency_composition_integration_test`
   - `ctest --test-dir build-ci -R '^(ToolObservabilityIntegrationTest|ToolProductionObservabilityIntegrationTest|DaemonRuntimeLiveDependencyCompositionTest|GatewayRuntimeLiveDependencyCompositionTest)$' --output-on-failure`

## 6. Rollout Checklist

1. tools 顶层总账不再把 production observability sink 记为未接入。
2. `TOOL-FIX-007` 必须回链到 runtime_support shared observability bundle 与 focused tests，而不是空泛写成“已接入”。
3. deliverable、总账、worklog 三处口径一致：build-tree focused evidence 已完成，但 installed / qemu / release / soak 仍未完成。
4. 本轮禁止使用 qemu / kvm；所有验收只依赖本地源码、focused tests 与文档回写。

## 7. 风险与回退

1. 若忽略现有 runtime_support / tools focused evidence 而重新实现 observability 接线，容易在不必要的重复改动中破坏既有 shared bundle 语义；本轮不采用。
2. 若只更新 tools 总账、不回链 runtime_support 既有证据，后续评审仍会误判为“主总账 Todo，但子系统已 Done”的跨文档不一致；本轮通过 deliverable + worklog 一并收口。
3. 若把本轮 focused evidence 外推成 installed / qemu / soak ready，会错误放大当前证明边界；本轮明确禁止该外推。

## 8. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到代码、测试、总账与交付物索引。
3. Build 三件套已锁定，且不依赖 qemu / kvm。
4. 范围保持在 production tools observability sink 的 tools 侧闭环，不扩张到 builtin wrapper、installed package 或 release gate。

结论：D Gate = PASS，可进入 `TOOL-FIX-007` Build 阶段并按 focused evidence 做 tools 侧收口。