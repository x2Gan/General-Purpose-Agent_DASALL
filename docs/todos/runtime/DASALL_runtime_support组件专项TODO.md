# DASALL runtime_support 组件专项 TODO

最近更新时间：2026-05-14
阶段：Detailed Design -> Special TODO
适用范围：`apps/runtime_support/` 共享 app-level runtime live composition helper、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、相关 focused composition tests，以及与 runtime/tools/services/knowledge/infra 的组合根边界
当前结论：`apps/runtime_support::compose_minimal_live_dependency_set()` 已把 daemon / gateway 的 runtime dependency owner 收敛为共享 helper，并已完成 install layout + SQLite memory baseline、production LLM manager、cognition/response ports、minimal ToolManager、typed multi-agent seam、runtime production services backend 注入、production observability/health sinks，以及 knowledge ready / degraded / unavailable 语义与 installed positive probe 的最小 live baseline。当前剩余缺口集中在 owner / fail-closed / marker regression matrix，以及 installed / qemu / release 层级验证。

## 1. 文档头

### 1.1 输入依据

本文档严格基于以下输入生成：

1. `docs/architecture/DASALL_runtime子系统详细设计.md`
2. `docs/architecture/DASALL_access子系统详细设计.md`
3. `docs/ssot/RuntimeAppCompositionV1.md`
4. `docs/ssot/BusinessChainIntegrationMatrix.md`
5. `docs/ssot/SystemIntegrationGateMatrix.md`
6. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md`
7. `docs/todos/access/DASALL_access子系统专项TODO.md`
8. `docs/todos/tools/DASALL_tools子系统专项TODO.md`
9. `docs/todos/services/DASALL_capability_services子系统专项TODO.md`
10. `docs/todos/DASALL_子系统查漏补缺专项记录.md`
11. `docs/worklog/DASALL_开发执行记录.md`
12. `apps/runtime_support/include/RuntimeLiveDependencyComposition.h`
13. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
14. `apps/runtime_support/CMakeLists.txt`
15. `apps/daemon/src/main.cpp`
16. `apps/gateway/src/main.cpp`
17. `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`
18. `tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp`
19. `tests/integration/multi_agent/MultiAgentDisabledByProfileIntegrationTest.cpp`
20. `tests/integration/multi_agent/MultiAgentCoordinatorPipelineTest.cpp`
21. `tests/integration/multi_agent/MultiAgentRecoveryFoldIntegrationTest.cpp`

### 1.2 生成原则

1. 不改写已冻结 ADR / SSOT 结论，不把 `apps/runtime_support` 扩张成 Runtime 第二主控或 Access 第二核心。
2. owner 必须固定在 app binary `main.cpp`；shared helper 只负责依赖装配与 seam 选择。
3. required ports 必须 fail-closed；optional ports 只能以显式 degraded / unavailable 语义暴露，不得隐式漂移。
4. 不把 build-tree focused evidence 外推为 installed / qemu / release-ready；所有层级必须分开记账。
5. 每个任务都保留代码目标、测试目标和验收命令三件套。
6. 不再把“helper 是否存在、daemon/gateway 是否仍用空 `RuntimeDependencySet`”当作当前主问题；主问题已经转向 downstream production completeness。

## 2. 目标与范围

### 2.1 组件目标

1. 将 daemon / gateway 的 runtime dependency owner 固定为共享的 app-level composition root，避免在两个 app binary 重复拼接 runtime internals。
2. 在 install layout、state owner、policy snapshot 和测试 override 之间提供单一、可测试、可 fail-closed 的 live composition 入口。
3. 为 runtime、tools、services、knowledge、multi_agent 等相邻子系统提供明确的 required / optional 端口组合边界。
4. 形成可评审、可回归、可分层记账的 focused / app-binary / installed / qemu 证据链，而不是继续把 helper 视作隐式 wiring。

### 2.2 纳入范围

1. `apps/runtime_support/include/RuntimeLiveDependencyComposition.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`apps/runtime_support/CMakeLists.txt`。
2. `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` 中对 shared helper 的调用与 fail-closed 行为。
3. `tests/integration/access/*RuntimeLiveDependencyCompositionTest.cpp` 与 multi-agent focused tests 中对 helper 的 owner / baseline 断言。
4. helper 与 runtime/tools/services/knowledge/infra 的组合边界、evidence marker、fail-closed / degrade-ready 语义。
5. 与本组件直接相关的专项 TODO、系统总记录和 Gate / SSOT 回写策略。

### 2.3 不纳入范围

1. Runtime FSM、Budget、Checkpoint、Recovery、Scheduler、SafeMode 的内部实现。
2. Access Admission / RuntimeBridge / ResultPublisher 的内部实现。
3. 任何 shared contracts admission 扩张。
4. streaming lifecycle、remote gateway transport、multi-agent 完整 worker orchestration。
5. `services/`、`infra/`、`knowledge/` 的完整子系统专项实现；本组件只负责消费其 public seam。

## 3. 当前状态与约束

### 3.1 约束清单

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| RTSUP-TC001 | `RuntimeAppCompositionV1`、runtime 详设 6.24.12.3 | Must | daemon / gateway 的 runtime production composition owner 在 app binary `main.cpp`，helper 只是共享组合根 | 不允许再回到空 `RuntimeDependencySet`、隐藏 fallback 或 access entry 自行 DI |
| RTSUP-TC002 | ADR-008、runtime 详设 6.24.12 | Must-Not | helper 不是第二个 orchestrator、scheduler、recovery owner 或 access pipeline | 任务只能围绕依赖装配、seam 选择、marker 与证据，不新增控制面职责 |
| RTSUP-TC003 | runtime 详设 6.24.12.3 | Must | required live baseline 缺失必须 fail-closed | memory / llm / cognition / response / tools / multi-agent 的缺失不能降级成静默 fallback |
| RTSUP-TC004 | runtime / access 详设；Gate-INT-06 | Must | optional port 必须显式 degraded / unavailable，不能在 ready / fatal 之间漂移 | knowledge 相关任务必须同时写清 marker、readiness 和 installed evidence |
| RTSUP-TC005 | install layout 与 FULLINT-TODO-015 证据 | Must | 生产默认走 install layout 和 state owner；测试只可通过 options 覆盖 readonly assets root / state root | helper 不能在 app main 内部被临时路径、硬编码 `/tmp` 或空路径替代 |
| RTSUP-TC006 | tools / services 专项 TODO | Must | production tool path 不能长期依赖 tools 内部 default execution/data service | 必须补 services facade / concrete backend 注入任务和 gate |
| RTSUP-TC007 | infrastructure / runtime / tools 专项 TODO | Must | production observability / health sinks 必须由组合根显式注入 | 不能把 module-local bridge green 当作 production sink ready |
| RTSUP-TC008 | 系统证据层级规则 | Must | L2/L3 focused 结果不能外推 installed / qemu / release-ready | 专项 TODO 必须单列 installed / qemu gate 与证据回写任务 |
| RTSUP-TC009 | access 详设 1.2、3.1 | Must-Not | Access entry 不得重新拼装 cognition / llm / memory / tools / services 等 runtime internals | 若 wiring 回流到 `apps/daemon` / `apps/gateway`，视为越权依赖回归 |
| RTSUP-TC010 | runtime / access / tools / services / infra 专项 TODO | Should | shared helper 应保持 minimal live baseline，再按 production adapters 渐进补齐 | 任务优先级先服务 backend，再 observability，再 installed evidence |

### 3.2 当前代码与测试证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| `RuntimeLiveDependencyComposition.h` | 已定义 result、options 与 public helper surface | shared helper 有稳定 public seam，可被 daemon/gateway/tests 共同引用 |
| `RuntimeLiveDependencyComposition.cpp` | 已解析 install layout，创建 SQLite memory manager、production LLM manager、cognition engine、response builder、minimal `ToolManager`、multi-agent coordinator，并尝试接入 installed asset knowledge service | minimal live baseline 已存在；owner 问题已解决 |
| `apps/runtime_support/CMakeLists.txt` | 已定义 `dasall_apps_runtime_support`，并链接 runtime/cognition/infra/knowledge/llm/memory/multi_agent/services/tools | helper 已成为显式 build target，而非 app 内匿名 wiring |
| `apps/daemon/src/main.cpp` | 已在构造 `AgentInitRequest` 前调用 shared helper | daemon entry 已不再依赖空 `RuntimeDependencySet` |
| `apps/gateway/src/main.cpp` | 已在 gateway unary entry 中调用 shared helper | gateway entry 与 daemon 已共享同一 composition owner 规则 |
| `DaemonRuntimeLiveDependencyCompositionTest` | 已证明 daemon live composition 可创建 helper 结果，并覆盖部分 positive path | focused evidence 证明 owner 与 baseline，不证明 services backend / installed gate |
| `GatewayRuntimeLiveDependencyCompositionTest` | 已证明 gateway live composition 可创建 helper 结果，并覆盖 memory state root override | gateway 与 daemon 对称性已有 focused baseline |
| `MultiAgent*IntegrationTest` | 已通过 shared helper 消费 typed `multi_agent_enabled()` | helper 已进入 multi-agent enabled / disabled seam |
| tool path | 已通过 `services::compose_live_services()` 与 `ServiceLiveComposition` public seam 向 `BuiltinExecutorLane` 注入 concrete `IExecutionService` / `IDataService` | `agent.dataset` / `agent.terminal` 已不再回落 tools default service，并有 direct + app composition focused evidence |
| observability / health | helper 已通过 `infra::compose_live_observability()` 统一提供 audit / metrics / trace sinks，并注册 tools/services probes 到 health monitor | production observability / health hot path 已有 direct tools focused evidence 与 app composition health aggregate evidence |
| knowledge seam | helper 现在以 installed positive probe 决定 ready/degraded，并在 daemon/gateway composition 中记录 `knowledge-installed-assets-ready` 或 `knowledge-degraded:*` marker | build-tree focused evidence 已固定 knowledge ready / degraded / unavailable 语义与 installed positive path |

## 4. Design Track 映射

| 设计结论 | 设计锚点 | 当前状态 | 对应任务 |
|---|---|---|---|
| owner 固定在 app binary，helper 为共享组合根 | runtime 详设 6.24.12.3、`RuntimeAppCompositionV1` | 已完成并回写 | RTSUP-TODO-001、003 |
| install layout / state owner / test override 语义固定 | FULLINT-TODO-015、runtime_support public options | 已完成 | RTSUP-TODO-002 |
| minimal live baseline 包含 memory/llm/cognition/response/tools/multi-agent，knowledge 为 optional | runtime / access / Gate-INT-06 | 已完成并补齐 installed positive probe 与 degraded marker 语义 | RTSUP-TODO-002、004、007 |
| tool path 需要真实 services backend，而非 default service | tools / capability services 专项 TODO | 已完成，helper 通过 services public live composition seam 注入 production backend | RTSUP-TODO-005 |
| production observability / health sinks 需要 shared helper 注入 | infra / runtime / tools / services 专项 TODO | 已完成，shared helper 已注入 concrete sinks 并保活 health monitor/probes | RTSUP-TODO-006 |
| evidence marker 与 owner / fail-closed regression 需要单独 gate | SystemIntegrationGateMatrix、BusinessChainIntegrationMatrix | 未完成 | RTSUP-TODO-008 |
| installed / qemu / release-preflight 需要单列证据 | Gate-INT-10、packaging / release 规则 | 未完成 | RTSUP-TODO-009 |

## 5. Build Track 映射

| Build 目标 | 代码落点 | 测试落点 | 说明 |
|---|---|---|---|
| 文档与 owner 边界回写 | runtime/access 详设与专项 TODO、系统总记录 | 文档一致性检查 | 只收敛边界，不新增运行行为 |
| install layout + SQLite baseline | `apps/runtime_support/*`、daemon/gateway entry | daemon/gateway composition tests | 确保 memory state owner 与 required ports baseline 稳定 |
| minimal tool surface + multi-agent seam | `RuntimeLiveDependencyComposition.cpp` | daemon/gateway + multi-agent focused tests | 当前只证明 baseline，不证明 production services backend |
| production services backend | `RuntimeLiveDependencyComposition.cpp`、`services/include/ServiceLiveComposition.h`、`services/src/ServiceLiveComposition.cpp` | `ToolServicesProductionBridgeIntegrationTest`、daemon/gateway composition tests | 已完成，tool path 已通过 live services backend 收口 |
| production observability / health sinks | helper + infra public observability composition helper | `ToolProductionObservabilityIntegrationTest`、`RuntimeProductionHealthCompositionTest`、扩展 daemon/gateway composition tests | 已完成，shared sinks 与 health aggregate 已进入 focused regression 面 |
| knowledge optional degraded semantics | helper + knowledge installed seam | `KnowledgeInstalledAssetProbeIntegrationTest`、扩展 daemon/gateway composition tests | 已完成，knowledge ready / degraded / unavailable 语义与 installed positive path 已固定 |
| installed / qemu / release-preflight matrix | packaging scripts、package smoke、system gate docs | Gate-INT-10 / qemu probes | 负责把 L2/L3 partial 提升到 L4/L5 候选 |

## 6. 原子任务清单

### 6.1 补设计 / 评审解阻任务

| Task ID | 状态 | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RTSUP-TODO-001 | Done | 收敛 owner、边界与证据分层口径 | runtime 详设 6.24.12.3、access 详设 1.2、`RuntimeAppCompositionV1` | owner 在 `main.cpp`、helper 只做装配、evidence 不越级 | L2 | `docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/architecture/DASALL_access子系统详细设计.md`、`docs/todos/runtime/DASALL_runtime子系统专项TODO.md`、`docs/todos/access/DASALL_access子系统专项TODO.md` | `apps/runtime_support` owner / boundary wording | 文档一致性检查 | `rg -n "runtime_support|compose_minimal_live_dependency_set|app-level composition root|owner" docs/architecture/DASALL_runtime子系统详细设计.md docs/architecture/DASALL_access子系统详细设计.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md docs/todos/access/DASALL_access子系统专项TODO.md` | 无 | 无 | 已完成本轮文档回写 | 更新后的 design/TODO 文档 | 只有当 runtime / access 不再把 helper 写成空 composition、access core 或隐藏 fallback 时完成 |
| RTSUP-TODO-002 | Done | 固定 install layout、SQLite state owner 与 required/optional 基线 | FULLINT-TODO-015、Gate-INT-06、runtime_support public options | readonly assets root、state root、SQLite memory baseline、knowledge optional seam | L2 | `apps/runtime_support/include/RuntimeLiveDependencyComposition.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | `RuntimeLiveDependencyCompositionOptions`、SQLite memory config、knowledge optional seam | `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` | `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])` | 无 | 无 | 已由既有实现落盘 | helper public options、runtime_support source、focused tests | 生产默认必须走 install layout，测试只能通过 options override，且 required/optional ports 口径不漂移 |

### 6.2 骨架与公共接口面任务

| Task ID | 状态 | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RTSUP-TODO-003 | Done | 建立 shared helper target 与 daemon/gateway 统一 owner | `RuntimeAppCompositionV1`、runtime/access 详设、daemon/gateway entry 现状 | `dasall_apps_runtime_support` target、daemon/gateway 调用点、error surface | L2 | `apps/runtime_support/CMakeLists.txt`、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` | `compose_minimal_live_dependency_set()`、`RuntimeDependencyCompositionResult` | `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` | `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])` | RTSUP-TODO-001、002 | 无 | 已由 shared helper 和 app entry 落盘 | helper target、daemon/gateway main、focused tests | daemon / gateway 不再复制 DI 逻辑，也不再依赖空 `RuntimeDependencySet` |
| RTSUP-TODO-004 | Done | 接入 minimal ToolManager、typed multi-agent seam 与 optional knowledge seam | tools / multi_agent / knowledge 专项 TODO 当前设计边界 | `agent.dataset` tool surface、Null/Real coordinator、optional knowledge factory | L2 | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | `compose_runtime_tool_manager()`、`visible_tools`、`multi_agent_enabled()`、knowledge service injection | `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest`、`MultiAgentDisabledByProfileIntegrationTest`、`MultiAgentCoordinatorPipelineTest` | `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest","MultiAgentDisabledByProfileIntegrationTest","MultiAgentCoordinatorPipelineTest"])` | RTSUP-TODO-003 | 无 | 既有 build-tree baseline 已存在 | helper source、focused tests | minimal live baseline 的 tool/multi-agent/knowledge seams 已进入 shared helper，不再散落在 app entry |

### 6.3 组件实现任务

| Task ID | 状态 | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RTSUP-TODO-005 | Done | 接入 runtime production services backend | tools / capability services 专项 TODO；`TOOL-GAP-001`、`CAPSRV-GAP-001` | `BuiltinExecutorLaneDependencies` 不再为空服务；tool path 走 concrete services backend | L2 | `services/include/ServiceLiveComposition.h`、`services/src/ServiceLiveComposition.cpp`、`services/CMakeLists.txt`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`tests/integration/tools/ToolServicesProductionBridgeIntegrationTest.cpp`、`tests/integration/tools/CMakeLists.txt`、`tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/CMakeLists.txt` | `compose_live_services()`、`compose_runtime_tool_manager()`、`IExecutionService`、`IDataService`、`ServiceFacade` | `ToolServicesProductionBridgeIntegrationTest`、扩展 daemon/gateway composition tests | `RunCtest_CMakeTools(tests=["ToolServicesProductionBridgeIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])`；回退：`cmake -S . -B build-rtsup005 -G "Unix Makefiles" && cmake --build build-rtsup005 --target dasall_tool_services_production_bridge_integration_test dasall_access_daemon_runtime_live_dependency_composition_integration_test dasall_access_gateway_runtime_live_dependency_composition_integration_test -j2 && ctest --test-dir build-rtsup005 -R '^(ToolServicesProductionBridgeIntegrationTest|DaemonRuntimeLiveDependencyCompositionTest|GatewayRuntimeLiveDependencyCompositionTest)$' --output-on-failure` | RTSUP-TODO-004 | RTSUP-BLK-001 | 已通过 services public live composition seam 解阻 | 更新后的 services factory、helper source 与 focused tests | app live composition 的 tool path 不再回落 tools default service，且 direct tools->services 与 daemon/gateway composition focused tests 全部通过 |
| RTSUP-TODO-006 | Done | 接入 production observability 与 health sinks | infra / runtime / tools / capability services 专项 TODO；`TOOL-GAP-007`、`CAPSRV-GAP-006`、`INF-GAP-001` | logger / audit / metrics / trace / health provider 注入 shared helper | L2 | `infra/include/ObservabilityLiveComposition.h`、`infra/src/ObservabilityLiveComposition.cpp`、`infra/CMakeLists.txt`、`services/include/ServiceLiveComposition.h`、`services/src/ServiceLiveComposition.cpp`、`runtime/include/RuntimeDependencySet.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`tests/integration/tools/ToolProductionObservabilityIntegrationTest.cpp`、`tests/integration/tools/CMakeLists.txt`、`tests/integration/access/RuntimeProductionHealthCompositionTest.cpp`、`tests/integration/access/CMakeLists.txt`、扩展 daemon/gateway composition tests | `compose_live_observability()`、runtime_support observability bundle、service observability bridge injection、tool/services probe registration | `ToolProductionObservabilityIntegrationTest`、`RuntimeProductionHealthCompositionTest`、扩展 daemon/gateway composition tests | `RunCtest_CMakeTools(tests=["ToolProductionObservabilityIntegrationTest","RuntimeProductionHealthCompositionTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])`；回退：`cmake -S . -B build-rtsup005 -G "Unix Makefiles" && cmake --build build-rtsup005 --target dasall_tool_production_observability_integration_test dasall_access_runtime_production_health_composition_integration_test dasall_access_daemon_runtime_live_dependency_composition_integration_test dasall_access_gateway_runtime_live_dependency_composition_integration_test -j2 && ctest --test-dir build-rtsup005 -R '^(ToolProductionObservabilityIntegrationTest|RuntimeProductionHealthCompositionTest|DaemonRuntimeLiveDependencyCompositionTest|GatewayRuntimeLiveDependencyCompositionTest)$' --output-on-failure` | RTSUP-TODO-005 | RTSUP-BLK-002 | 已通过 infra public observability composition seam 与 health monitor/probe registration 解阻 | 更新后的 infra/services/helper source 与 observability/health focused tests | shared helper 组合出的 live path 能发出真实 sink event，并把 health hot path 纳入 app composition，且 direct tools 与 daemon/gateway/runtime health tests 全部通过 |
| RTSUP-TODO-007 | Done | 收口 knowledge optional degraded semantics 与 installed positive probe | knowledge 专项 TODO；`KnowledgeInstalledAssetProbeIntegrationTest` 当前 red 边界 | helper 的 knowledge ready / degraded / unavailable marker、installed positive evidence | L2 | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`tests/integration/knowledge/KnowledgeInstalledAssetProbeIntegrationTest.cpp`、`tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp` | installed positive probe、knowledge degraded marker、ready/degraded readiness semantics | `KnowledgeInstalledAssetProbeIntegrationTest`、扩展 daemon/gateway composition tests | `RunCtest_CMakeTools(tests=["KnowledgeInstalledAssetProbeIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])`；回退：`cmake -S . -B build-rtsup005 -G "Unix Makefiles" && cmake --build build-rtsup005 --target dasall_knowledge_installed_asset_probe_integration_test dasall_access_daemon_runtime_live_dependency_composition_integration_test dasall_access_gateway_runtime_live_dependency_composition_integration_test -j2 && ctest --test-dir build-rtsup005 -R '^(KnowledgeInstalledAssetProbeIntegrationTest|DaemonRuntimeLiveDependencyCompositionTest|GatewayRuntimeLiveDependencyCompositionTest)$' --output-on-failure` | RTSUP-TODO-004 | RTSUP-BLK-003 | 已通过 installed positive probe 与 daemon/gateway degraded marker 回归收缩到 package / qemu gate | 更新后的 helper source、knowledge probe tests | knowledge optional port 的语义稳定，不再在 fatal / ready / degraded 之间漂移，且 installed positive probe 与 daemon/gateway composition focused tests 全部通过 |

### 6.4 测试支撑 / 集成 / 门禁任务

| Task ID | 状态 | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RTSUP-TODO-008 | NotStarted | 建立 owner / fail-closed / marker regression matrix | SystemIntegrationGateMatrix、BusinessChainIntegrationMatrix、runtime_support focused evidence | required-missing、daemon/gateway symmetry、multi-agent enablement、knowledge unavailable、marker stratification | L2 | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp`、新增 `tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp` | required ports missing、marker assertions、gateway symmetry | `RuntimeLiveCompositionFailureMatrixTest`、扩展 daemon/gateway composition tests | `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest","MultiAgentDisabledByProfileIntegrationTest","RuntimeLiveCompositionFailureMatrixTest"])` | RTSUP-TODO-005、006、007 | RTSUP-BLK-001、002 | shared helper 的 downstream seams 至少有稳定的 failure / marker contract 可测 | 更新后的 focused tests | 修改 helper 后，不会再出现 owner 不清、fail-closed 丢失或 evidence marker 混写 |
| RTSUP-TODO-009 | NotStarted | 建立 installed / qemu / release-preflight composition gate | Gate-INT-10、packaging / release 规则、系统总记录证据分层 | daemon/gateway shared helper 的 package / qemu / release-preflight matrix | L2 | `scripts/packaging/*`、必要时 package smoke harness 与相关文档回写 | package / qemu composition probe、release-preflight matrix | `Build_CMakeTools(target=dasall_gate_int_10)`、qemu / package smoke | `Build_CMakeTools(target=dasall_gate_int_10)`；`sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | RTSUP-TODO-005、006、007、008 | RTSUP-BLK-003 | packaging / qemu 环境可执行，shared helper 的 installed probes 已纳入正式 gate | package smoke / gate 文档 / SSOT 回写 | `apps/runtime_support` 的证据层级可从 L2/L3 partial 提升到 L4/L5 候选 |
| RTSUP-TODO-010 | Done | 回写专项 TODO 与系统查漏补缺总账 | 当前任务需求；总记录与 design/TODO 边界修正 | runtime_support 组件专项 TODO、系统总记录补充章节 | L2 | `docs/todos/runtime/DASALL_runtime_support组件专项TODO.md`、`docs/todos/DASALL_子系统查漏补缺专项记录.md` | 文档一致性检查 | `rg -n "runtime_support 组件专项 TODO|runtime_support / app live composition|RTSUP-GAP|RTSUP-FIX|RTSUP-TODO" docs/todos/runtime/DASALL_runtime_support组件专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md` | RTSUP-TODO-001 | 无 | 已完成本轮回写 | 新增专项 TODO 与更新后的系统总记录 | `apps/runtime_support` 的 owner、当前缺口与后续任务不再散落在 runtime/access/tools/services 多份文档里 |

## 7. 执行顺序建议

### 7.1 串并行编排

| 阶段 | 任务 | 编排建议 | 说明 |
|---|---|---|---|
| A 边界与基线冻结 | 001 ~ 004、010 | 已完成 | owner、install layout、shared helper、minimal baseline 与总记录回写已经到位 |
| B production backend 收口 | 005、006 | 005 先，006 可并行准备 | 先解决 tool path default service，再补 observability / health sinks |
| C optional knowledge 与 regression | 007、008 | 007 已完成，008 下一步 | knowledge degraded 语义已稳定，下一步建立完整 regression matrix |
| D installed / qemu / release 证据 | 009 | 最后串行 | 只有在 build-tree production completeness 稳定后，installed / qemu gate 才有意义 |

### 7.2 必过门禁表

| Gate ID | 对应设计或约束 | 通过条件 | 关联任务 |
|---|---|---|---|
| Gate-RTSUP-01 | owner / boundary gate | daemon / gateway 继续通过 shared helper 装配 runtime deps；Access entry 不回流 runtime internals | 001、003、010 |
| Gate-RTSUP-02 | required baseline fail-closed gate | required ports 缺失时 helper 显式失败，不出现空 fallback / 静默 ready | 002、008 |
| Gate-RTSUP-03 | services backend gate | tool path 不再回落 default service，而是通过 concrete services backend | 005、008 |
| Gate-RTSUP-04 | observability / health gate | shared helper 组合的 live path 可发出真实 sink event 与 health snapshot | 006、008 |
| Gate-RTSUP-05 | knowledge optional gate | knowledge ready / degraded / unavailable 语义固定，installed positive probe 可复验 | 007、008 |
| Gate-RTSUP-06 | installed / qemu / release gate | daemon / gateway composition 在 package / qemu / release-preflight 中有正式证据 | 009 |

## 8. 阻塞项与解阻条件

| Blocker ID | 描述 | 影响任务 | 解阻条件 | 未解阻前策略 |
|---|---|---|---|---|
| RTSUP-BLK-001 | 已解阻：runtime_support 已通过 `ServiceLiveComposition` public seam 获得真实 services composition root，tool path 不再回落 default service | 008、009 | services facade / lanes / adapters 在 helper 侧具备最小 production composition 口径 | 保持 direct tools->services 与 daemon/gateway composition focused tests 作为后续 regression baseline |
| RTSUP-BLK-002 | 已解阻：infra provider / tracer / audit logger / health provider 已通过 `infra::compose_live_observability()` 收口，并在 helper 中统一注册 tools/services probes | 008、009 | runtime/tools/services/infra 就 helper 注入口径达成一致，或在 helper 中收口最小 provider seam | 保持 direct tools observability test、runtime health composition test 与 daemon/gateway composition tests 作为后续 regression baseline |
| RTSUP-BLK-003 | 已部分解阻：installed knowledge positive probe 已可复验，但 package / qemu 环境尚未形成正式 gate | 009 | knowledge installed probe 正向可复验，Gate-INT-10 / qemu harness 可运行 | 保持 knowledge focused tests 作为 build-tree baseline，并在 009 单列 package / qemu 证据 |

## 9. 测试矩阵与统一验收命令

### 9.1 测试矩阵

| 测试层级 | 覆盖范围 | 关键用例 | 目标 |
|---|---|---|---|
| focused integration | daemon / gateway shared helper 正向路径、state root override、owner 对称性 | `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` | 证明 owner 与 minimal baseline 已落盘 |
| multi-agent seam | typed enablement、Null / Real coordinator folding | `MultiAgentDisabledByProfileIntegrationTest`、`MultiAgentCoordinatorPipelineTest`、`MultiAgentRecoveryFoldIntegrationTest` | 证明 helper 已进入 multi-agent enablement 边界 |
| services backend | tool path 不再回落 default service | `ToolServicesSmokeIntegrationTest`、`ToolServicesProductionBridgeIntegrationTest` | 证明 composition 后的 tool path 进入真实 services backend |
| observability / health | runtime/tools/services sink injection | `ToolProductionObservabilityIntegrationTest`、`RuntimeProductionHealthCompositionTest`、`RuntimeHealthMaintenanceIntegrationTest` | 证明 production hot path 有真实 sink / health snapshot |
| knowledge optional seam | installed asset knowledge ready / degraded / unavailable | `KnowledgeInstalledAssetProbeIntegrationTest`、扩展 daemon/gateway composition tests | 固定 knowledge optional 语义与 installed positive path |
| installed / qemu / release | package / qemu composition probe | `Gate-INT-10` 相关 package smoke、qemu probes | 把 focused / app-binary 证据提升到 installed / qemu 层 |

### 9.2 统一验收命令建议

1. focused baseline：`RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest","MultiAgentDisabledByProfileIntegrationTest","MultiAgentCoordinatorPipelineTest","MultiAgentRecoveryFoldIntegrationTest"])`
2. production completeness：`RunCtest_CMakeTools(tests=["ToolServicesSmokeIntegrationTest","ToolServicesProductionBridgeIntegrationTest","ToolProductionObservabilityIntegrationTest","RuntimeHealthMaintenanceIntegrationTest","KnowledgeInstalledAssetProbeIntegrationTest"])`
3. installed / qemu：`Build_CMakeTools(target=dasall_gate_int_10)` 与 `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>`

## 10. 风险与回退策略

| 风险 ID | 描述 | 触发条件 | 回退策略 |
|---|---|---|---|
| RTSUP-RISK-001 | helper 膨胀成第二个 runtime control plane | 在 helper 内新增编排、恢复、状态推进逻辑 | 回退到“只做装配与 seam 选择”的边界，所有控制逻辑继续留在 runtime / access |
| RTSUP-RISK-002 | tool path 继续由 default service 假装 production backend | services backend 未接完就把 focused 结果写成 ready | 保持 explicit gap，直到 005 和 Gate-RTSUP-03 通过 |
| RTSUP-RISK-003 | knowledge optional 语义漂移 | unavailable / degraded / ready 在不同文档和测试中口径不一 | 以 007 固定 marker / readiness / installed probe 语义 |
| RTSUP-RISK-004 | build-tree 绿灯被误写成 installed / qemu ready | 未建立 009 就引用 focused composition tests 作为 release 证据 | 严格按证据层级回写，package / qemu 之前保持 L2/L3 partial |
| RTSUP-RISK-005 | daemon / gateway entry 回流 runtime internals wiring | app entry 为了局部修复开始手工拼 cognition / llm / memory / tools / services | 回退到 shared helper，保留 daemon/gateway 只作为 owner + entry bootstrap |

## 11. 可行性结论

### 11.1 是否可直接进入执行

可以，但不应把范围再放大。

当前最小可执行闭环是：在 `RTSUP-TODO-005` / `RTSUP-TODO-006` / `RTSUP-TODO-007` 已完成的前提下，先推进 `RTSUP-TODO-008` 的 regression 收口，再通过 `RTSUP-TODO-009` 把证据提升到 installed / qemu 层。

### 11.2 当前最细可安全落盘粒度

1. L3：`compose_runtime_tool_manager()`、knowledge marker / readiness projection、daemon / gateway composition test assertions。
2. L2：services facade / backend injection、observability / health provider injection、installed / qemu evidence matrix。
3. L1：更宽 release runner / soak / chaos 证据，以及 helper 之外的系统级 release hardening。

## 12. 未决问题处置表

| OQ ID | 问题 | 当前处置 | 后续动作 |
|---|---|---|---|
| RTSUP-OQ-001 | helper 应维持 minimal baseline，还是继续扩成 default-ready 全装配根？ | 采纳“minimal baseline + 渐进式 production adapters” | 先完成 005/006/007，再评估是否需要更多 optional adapters |
| RTSUP-OQ-002 | knowledge 对哪些 profile / package 场景应从 degraded 升级为 required？ | 暂缓，保持默认 optional degrade-ready | 等 knowledge installed positive gate 与 profile matrix 收口后再定 |
| RTSUP-OQ-003 | daemon 与 gateway 的 health / observability 注入是否完全共用一套 provider？ | 采纳“shared helper 注入 provider，entry-specific reporting 继续留在 app entry” | 006 完成后再根据 health / gateway transport 差异做微调 |