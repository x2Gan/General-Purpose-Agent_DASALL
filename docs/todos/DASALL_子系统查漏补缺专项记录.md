# DASALL 子系统查漏补缺专项记录

最近更新时间：2026-05-18  
阶段：System Review -> Subsystem Gap Closure  
适用范围：按子系统逐项检查详细设计、当前代码、测试证据、跨模块链路与 installed / production 证据边界  
记录口径：本文件用于集中记录“设计已要求但实现、测试、证据或生产链路仍未完全闭合”的缺口；不替代各子系统专项 TODO、worklog 或 SSOT 矩阵。

## 1. 文档用途

本文件用于承接逐个子系统排查后的查漏补缺结论，形成一个可追加、可复验、可转 TODO 的总账。

记录原则：

1. 每个子系统单独成章，先给出结论，再列覆盖矩阵、缺口清单和补缺任务建议。
2. 明确区分源码存在、focused / fixture 通过、true integration 通过、installed-package 运行通过和 production-ready 结论，不用低层证据外推高层结论。
3. 缺口必须绑定代码落点、测试目标和验收命令；若暂时只是证据不足，也要写清楚需要补哪类证据。
4. 不把历史 TODO 的 Done 状态直接当作当前实现完成结论；以当前工作树和可复验链路为准。
5. 不改写 ADR-006 / ADR-007 / ADR-008 的边界：Memory 掌 Context，LLM 掌 Prompt，Runtime 掌 Recovery 与全局主控。

## 2. 证据层级

| 层级 | 名称 | 可采信结论 | 不可外推 |
|---|---|---|---|
| L0 | source-code evidence | 当前代码中存在或缺失入口、类型、调用点、CMake / test 注册 | 不代表可运行 |
| L1 | focused / fixture evidence | 局部组件、mock、fixture 路径可回归 | 不代表真实跨模块集成 |
| L2 | true integration evidence | 跨模块 runtime-facing 行为在 build tree 中成立 | 不代表 app binary 或 installed package |
| L3 | app-binary / release-preflight evidence | build tree 下 CLI / daemon / gateway 或 release-preflight 可执行 | 不代表 installed package 或 qemu |
| L4 | installed-package local evidence | 当前机器安装态生命周期或主功能可执行 | 不代表 release runner、qemu、长期 production |
| L5 | release runner / qemu evidence | qemu / autopkgtest / release runner 中 installed package gate 可复验 | 不代表 soak / chaos |
| L6 | soak / chaos / production confidence | 长稳态、外部依赖抖动、恢复策略可观测 | 不替代 L2-L5 前置 gate |

## 3. 子系统索引

| 子系统 | 当前章节状态 | 最高可信结论 | 主要残余缺口 |
|---|---|---|---|
| cognition | 已记录 | 主体实现 L2 基本闭合；installed cognition 必经链路未证明 | Runtime 对部分 `ActionDecision` 第一跳映射、production direct LLM bypass、stage timeout、LLM structured output 主链消费、production telemetry sink |
| llm | 已记录 | D1-D9 主体实现 L2 基本闭合；production LLM generation 有 L4 local 证据；source boundary 自动化守护已接线 | L5 qemu / release runner / soak 证据、external provider 长稳态 |
| memory | 已记录 | 主体实现 L2 基本闭合；Runtime / app 链路与 production observability 已接入；sqlite-vss 本机 installed-package authoritative 证据已闭合 | L5 qemu / release runner / soak 证据、边界回归防线、installed 多轮 context / maintenance 正向证据 |
| knowledge | 已记录 | 主体实现 L2 基本闭合；Runtime -> Memory evidence 读链路已打通；本轮 9 个聚焦测试通过、1 个 installed asset probe 失败 | refresh 异步/首启 build、concrete vector backend、lane timeout/parallel recall、首批 corpus baseline、持久化 ledger/catalog/migration、production telemetry sink、installed/qemu/soak 证据 |
| tools | 已记录 | 主体治理链、MCP/Skill/Workflow/Projection 已达到 L2 fixture / integration 可信；Runtime/app 可调用 IToolManager | production live services 后端仍落默认实现、plugin 自动接入未闭合、MCP 仅 stdio concrete、compensate 入口未实现、production observability 与 installed/qemu/soak 证据不足 |
| capability services | 已记录 | 主体服务门面、execution/data/system lanes、adapter/router/bridge、observability 与健康探针达到 L2 fixture / integration 可信 | runtime production 未装配真实 services facade/backend、data cache 命中结果 triad 缺陷、subscription trace 链不完整、adapter concrete backend / production sinks / installed-qemu-soak 证据不足 |
| runtime | 已记录 | 控制面主体已脱离 placeholder；AgentFacade / AgentOrchestrator / FSM / Budget / Checkpoint / Recovery / Session / Scheduler / SafeMode 均有真实落点；installed `run` 有 L4 production direct LLM 主功能证据 | production direct LLM path 与 cognition/tools/recovery full path 证据分层、terminal cognition decision 映射、durable checkpoint/resume、deadline/cancellation 主链、runtime telemetry/health/maintenance hot path、L5/L6 证据 |
| runtime_support / app live composition | 已记录 | daemon/gateway 共享 app-level runtime 组合根已落盘；required live baseline 有 L2 focused / L3 partial 证据 | services backend 注入、observability/health sinks、knowledge optional degraded semantics、owner/fail-closed regression matrix、installed/qemu/release 证据 |
| access / apps ingress | 已记录 | Access v1 unary focused ingress 已通过 Gate-INT-08；CLI/daemon/gateway app-binary 与 installed local control-plane 已有 L3/L4 partial 证据 | installed HTTP gateway 正向、真实 async receipt package path、streaming lifecycle、multi-instance receipt authority、release/qemu/security hardening 证据 |
| multi_agent | 已记录 | IMultiAgentCoordinator、Null/Real coordinator、Runtime fold helper 与 build-tree sidecar 协同已有 L2 证据；installed profiles 默认 disabled | profile-enabled installed 正向路径、CLI/API surface、role/capability/routing 生产策略、observability/audit、qemu/soak 证据 |
| infrastructure | 已记录 | logging/audit/config/secret/policy/diagnostics/health/plugin 等主体接口与多数组件已落盘；diagnostics/readiness 有 L3/L4 partial 证据 | production sinks across subsystems、diagnostics retained snapshot 分层、SecretManager/ISecretManager app live composition、plugin-to-tools 自动接线、watchdog/event publish、KMS/OTLP 等 optional backend、release runner / soak |
| profiles / platform | 已记录 | profiles Build/Runtime 双平面与 platform/linux provider 主体任务多为 Done；multi_agent disabled policy 和 linux bootstrap 已有 focused/integration 证据 | consumer matrix live enforcement、installed policy mutation validation、profile enablement 与 runtime wiring 一致性、platform qemu/autopkgtest bootstrap、ARM/HAL 真实驱动边界 |
| contracts / packaging / release | 已记录 | contracts 主链对象与多条 boundary regression 已建立；local installed package L4 主功能通过 | WP-04 supporting object 清点、shared admission 防线、qemu/autopkgtest L5、lintian/artifact SOP、package evidence matrix 与 worklog 自动回写 |

## 4. cognition 子系统查漏补缺

### 4.1 检查范围与依据

本轮检查目标：核对 `cognition/*` 是否覆盖 `docs/architecture/DASALL_cognition子系统详细设计.md` 中规定的功能，并判断关联模块链路是否打通。

检查范围：

1. cognition 源码与公共接口：`cognition/include/*`、`cognition/src/*`、`cognition/CMakeLists.txt`。
2. cognition 单元与集成测试：`tests/unit/cognition/*`、`tests/integration/cognition/*`。
3. Runtime 接入链路：`runtime/src/AgentFacade.cpp`、`runtime/src/AgentOrchestrator.cpp`、`runtime/include/fsm/StateTransitionTypes.h`。
4. app live composition：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`。
5. 证据边界：`docs/worklog/DASALL_开发执行记录.md`、`docs/todos/integration/deliverables/FULLINT-TODO-003-runtime-cognition-memory-llm主链证据包.md`、`docs/ssot/BusinessChainIntegrationMatrix.md`。

未执行项：本轮未重新运行 CMake / CTest；以下结论基于当前源码、测试拓扑和既有证据文档静态检查。需要把状态升级为验收结论时，必须执行 4.8 中的聚焦命令。

### 4.2 总体结论

结论：`cognition/*` 已经基本覆盖 cognition 详设的主体功能，不能再视为 placeholder。公共接口、五段认知链路、profile 投影、预算与上下文信号、LLM bridge、Reflection、BeliefUpdateHint、ResponseBuilder、validators、telemetry 和测试拓扑均有工程落点。

但当前不能判定为“所有功能完全完成、所有关联链路完全打通”。更准确的状态是：

1. cognition 模块内部主体实现达到 L1 / L2 可信度，核心组件和 integration tests 已存在。
2. Runtime 非 direct live cognition path 已打通 `ExecuteAction -> ToolCalling -> WaitingExternal -> Reflecting -> ResponseBuilder` 主链。
3. installed production `dasall run` 当前主要证明 runtime -> memory context -> production LLM direct response -> persistence 链路，不证明 cognition `decide()` / `reflect()` 在 installed L4 主链必经。
4. 详设中要求的部分交互契约仍未完全闭合，尤其是 `DirectResponse` / `ConvergeSafe` 到 Runtime `Responding` 的第一跳映射。

### 4.3 设计覆盖矩阵

| 设计域 | 当前覆盖判断 | 主要证据 | 缺口 / 边界 |
|---|---|---|---|
| 公共接口 `ICognitionEngine` / `IResponseBuilder` | 已覆盖 | `decide()`、`reflect()`、`build()` 三入口已落盘；factory overload 支持 config、dependencies、policy snapshot | 无明显接口缺口；仍需避免重新引入旧 `step()` 混合职责 |
| 支撑类型 | 已覆盖 | `CognitionTypes`、`ActionDecision`、`PlanGraph`、`ReplanResult`、`BeliefUpdateHint`、`BudgetContext`、`ContextSufficiencySignal`、`StageModelHint` 已存在 | supporting types 继续保持 module-local / module-public，不进入 shared contracts |
| Perception | 已覆盖 | `PerceptionEngine` 支持目标、上下文、观察、歧义、澄清问题与 diagnostics | 当前以规则感知为主，LLM structured perception 未成为主输出来源 |
| Planner | 已覆盖 | `PlanGraphBuilder` 支持 plan graph、replan、budget compression、invariant validation | Reflection active plan 生产路径在专项 TODO 中仍有待补强记录 |
| Reasoner | 已覆盖 | 候选评分、clarification、conflict、budget、direct response / converge safe 决策类型已实现 | Runtime 尚未完整消费所有 terminal decision kind |
| Reflection | 已覆盖 | `ReflectionEngine` 输出 shared `ReflectionDecision`，保持 suggestion-only | Runtime 对反思建议已有 RecoveryManager 接入，但 installed L4 仍未证明 cognition reflection 必经 |
| BeliefUpdateHint | 已覆盖 | decide / reflect 路径均可合成 hint；Runtime 有 best-effort writeback 证据 | installed direct path 不证明 cognition belief writeback 已执行 |
| ResponseBuilder | 已覆盖 | 支持 LLM bridge、observation projection、template fallback、redaction、clamp | Runtime 对 cognition terminal decision 的 response-first 路径未完整闭合 |
| StagePolicyResolver / profile projection | 基本覆盖 | canonical stage key 为 `planning` / `execution` / `reflection` / `response`；budget-aware plan cap 存在 | 需要持续防止 legacy stage alias 在 bridge / tests 中私有散落 |
| CognitionLlmBridge | 基本覆盖 | StageModelHint 投影到 LLM request，错误归一化，provider-private redaction | bridge 成功/失败进入 diagnostics，但 LLM structured output 未驱动主链对象生成 |
| StageOutputValidator | 部分覆盖 | JSON / schema / PlanGraph / ActionDecision / response envelope validator 已实现并有 tests | validator 未完全成为 Facade 解析 LLM structured output 的主链裁决入口 |
| CognitionTelemetry | 部分覆盖 | stage started/completed/failed、clarification、response degraded、redaction、sink failure isolation 已实现 | production infra sink 未通过 `CognitionRuntimeDependencies` 注入，ResponseBuilder 未明显接 telemetry |
| 阶段超时隔离 | 部分覆盖 | `deadline_ms` 可从 policy / hint 投影到 bridge request | `CognitionFacade` 未见主动按 `deadline_ms` 中断等待并返回 `cognition.stage_timeout` 的门面级隔离 |
| Runtime integration | 部分覆盖 | live cognition path 可执行 `ExecuteAction` 工具链、反思、终态 response；interaction contract tests 存在 | `DirectResponse` / `ConvergeSafe` 第一跳缺口；installed production direct path bypass cognition |
| Memory / Tools / Recovery 边界 | 基本正确 | cognition 不直拉 Memory、不直调 Tools、不执行 Recovery；Runtime 负责写回、ToolRequest、RecoveryManager | installed direct path 仍只证明非 cognition 主链；需分别补 cognition-positive installed evidence |

### 4.4 关联模块链路判断

| 链路 | 是否打通 | 当前可信层级 | 判断 |
|---|---|---:|---|
| Runtime -> Cognition decide | 部分打通 | L2 | live integration path 存在且 `ExecuteAction` 可进入工具链；production direct LLM path 可绕过 cognition |
| Runtime -> Cognition reflect | 部分打通 | L2 | Tool observation 后可调用 `reflect()` 并把非 Continue 建议交给 RecoveryManager；installed L4 未证明必经 |
| Runtime -> ResponseBuilder | 部分打通 | L2 | tool/reflection terminal path 可调用 builder；`DirectResponse` / `ConvergeSafe` terminal path 未按详设第一跳闭合 |
| Cognition -> LLM | 基本打通 | L1 / L2 | Facade / ResponseBuilder 可经 `CognitionLlmBridge` 触达 canonical stages | 
| Cognition -> Memory | 间接打通 | L2 | 通过 Runtime 消费 `BeliefUpdateHint` 写回；cognition 不直写，符合 ADR-006 |
| Cognition -> Tools | 间接打通 | L2 | Reasoner 只产 `tool_intent_hint`；Runtime 构造 `ToolRequest` 并调用 `ToolManager` |
| Cognition -> Recovery | 间接打通 | L2 | Reflection 只产 suggestion；Runtime / RecoveryManager 裁定，符合 ADR-007 |
| Cognition -> Knowledge | 间接 / 证据不足 | L0 / L2 partial | knowledge evidence 可由 Runtime / ContextPacket 进入 cognition；未证明 cognition installed positive path |
| app live composition -> cognition | 已装配，支持受控 cognition-first | L0 / L3-L4 stratified | `RuntimeLiveDependencyComposition` 默认保留 `required-live-baseline` 触发 production direct LLM path；设置 `DASALL_RUNTIME_COGNITION_FIRST=1` 时改写为 `cognition-first-forced`，`DaemonBinaryUnarySmokeTest` 已证明 app-binary cognition-positive run |

### 4.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| COG-GAP-001 | 已闭合 | Runtime live cognition path 现已闭合 `DirectResponse` / `ConvergeSafe -> Responding + IResponseBuilder.build()` 第一跳 | 2026-05-14 已更新 `AgentOrchestrator` live path：`DirectResponse` / `ConvergeSafe` 进入 `Responding` 并调用 `IResponseBuilder.build()`，`NoDecision` 保持 fail-fast；`CognitionRuntimeInteractionContractTest` 已补齐三类 fixture | 详设 6.14.1 的 terminal ActionDecision→FSM 第一跳不再缺失 | 2026-05-14 由 `COG-FIX-001` 完成；后续只保留 installed cognition-positive 证据与其他非本缺口项 |
| COG-GAP-002 | 已闭合 | app-binary cognition-positive evidence 已建立，installed 仍保持 direct path 分层 | 2026-05-14 已在 `RuntimeLiveDependencyComposition` 增加 `DASALL_RUNTIME_COGNITION_FIRST=1` gate；`DaemonRuntimeLiveDependencyCompositionTest` 断言 `cognition-first-forced` marker，`DaemonBinaryUnarySmokeTest` 断言无 `llm.origin=` 且返回 `runtime unary integration completed:` | app-binary 不再只能证明 direct LLM path；installed 与 cognition-first 证据口径可分层陈述 | 2026-05-14 由 `COG-FIX-002` 完成；installed L4 仍保持 direct-path 口径，后续仅在需要时追加 qemu/package 证据 |
| COG-GAP-003 | 已闭合 | Facade stage timeout isolation 已落地 | 2026-05-14 已在 `CognitionFacade` 引入阶段 deadline runner：Perception / Planner / Reasoner / Reflection 与 bridge 均按 `StageExecutionPlan.deadline_ms` / `StageModelHint.deadline_ms` fail-fast；`CognitionFacadeStageTimeoutTest` 证明 planning/reflection 慢 bridge 返回 `cognition.stage_timeout` 且 late result 不污染下一次请求 | 单阶段卡死不再拖垮整条 cognition 链，详设 6.15.3 具备 focused unit evidence | 2026-05-14 由 `COG-FIX-003` 完成；后续只保留 structured output / telemetry 等非超时缺口 |
| COG-GAP-004 | 已闭合 | LLM structured output validator 已成为主链对象生成裁决入口 | 2026-05-15 `COG-FIX-004` 已完成：`StageOutputValidator`、typed projector、Facade authoritative consumption、runtime interaction、integration regression 与 telemetry fields 均已形成 focused evidence，`Gate-COG-FIX004A-05` 已转 Pass | LLM JSON 输出现已受 schema / invariant / fail-closed 策略治理并驱动 `PlanGraph` / `ActionDecision` 主链 | 2026-05-15 由 `COG-FIX-004` 完成；后续仅保留 installed / qemu / soak 等更高层证据 |
| COG-GAP-005 | 已闭合 | CognitionTelemetry production sink 已接入 live audit / metrics / tracing provider | 2026-05-16 已扩展 `CognitionRuntimeDependencies`，`CognitionTelemetry` 在内部适配 infra provider，`runtime_support` / `AgentFacade` 透传 live observability bundle；`CognitionProductionTelemetryIntegrationTest` 已证明 `stage.completed` / `stage.failed` / `response.degraded` 会落到 concrete provider | cognition 语义观测不再只停留在 module-local no-op sink | 2026-05-16 由 `COG-FIX-005` 完成；installed / qemu / soak 仍需独立证据 |
| COG-GAP-006 | 已闭合 | ResponseBuilder degraded telemetry 已与 production response path 打通 | 2026-05-16 `ResponseBuilder` 已持有 live telemetry owner，并在 invalid / failure / template fallback 路径显式发射 response stage failure 与 `response.degraded`；integration test 同时覆盖 redaction | response fallback / degraded path 现在具备 production-style 可观测证据 | 2026-05-16 由 `COG-FIX-005` 完成；后续只需维持字段与 redaction 一致性 |
| COG-GAP-007 | 已闭合 | 详设 / TODO / worklog 的当前状态口径已统一 | 2026-05-16 已同步回写 `docs/architecture/DASALL_cognition子系统详细设计.md`、`docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` 与本文件，明确 Gate-COG-12 当前仍受 repo-wide non-cognition blocker 阻断 | 后续评审不再需要在历史 baseline 与当前状态之间人工对齐 | 2026-05-16 由 `COG-FIX-006` 完成；后续仅维护新增证据的同步回写 |

### 4.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| COG-FIX-001 | Done | 补齐 terminal ActionDecision 到 Runtime Responding 映射 | 已更新 `runtime/src/AgentOrchestrator.cpp`，在 live cognition path 中处理 `DirectResponse` / `ConvergeSafe`，进入 `RuntimeState::Responding` 并调用 `IResponseBuilder.build()`；保留 `NoDecision` fail-fast | 已扩展 `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp`，新增 DirectResponse、ConvergeSafe、NoDecision 三类 fixture 与 response builder probe | `cmake --build build-ci --target dasall_cognition_runtime_interaction_contract_integration_test && ctest --test-dir build-ci -R "CognitionRuntimeInteractionContractTest" --output-on-failure` | 2026-05-14 已验收通过：详设 6.14.1 五类 decision kind 第一跳均有自动化断言 |
| COG-FIX-002 | Done | 建立 cognition-positive installed / app-binary 证据 | 已更新 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`，增加 `DASALL_RUNTIME_COGNITION_FIRST=1` 受控 gate：证据模式下写入 `:cognition-first-forced` marker，而不再注入 `:required-live-baseline`；同时更新 `runtime/src/AgentOrchestrator.cpp`，让 live cognition path 复用 direct path 的 tolerated context warning 口径 | 已扩展 `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` 与 `tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`，断言 marker 分层、app-binary human/JSON run 无 `llm.origin=` 且返回 `runtime unary integration completed:`；`CognitionRuntimeIntegrationTest` 回归通过 | `Build_CMakeTools(buildTargets=["dasall_access_daemon_runtime_live_dependency_composition_integration_test","dasall_access_daemon_binary_unary_smoke_integration_test","dasall_cognition_runtime_integration_test"])`；`RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","DaemonBinaryUnarySmokeTest","CognitionRuntimeIntegrationTest"])` | 2026-05-14 已验收通过：L3 app-binary cognition-positive evidence 已建立，direct path 与 cognition-first path 不再混写 |
| COG-FIX-003 | Done | 实现 Facade 阶段超时隔离 | 已更新 `cognition/src/CognitionFacade.cpp`：新增阶段 deadline runner，对 Perception / Planner / Reasoner / Reflection 与 bridge 调用按 `StageExecutionPlan.deadline_ms` / `StageModelHint.deadline_ms` 做超时裁定；超时统一返回带 `cognition.stage_timeout` message 的 `ErrorInfo`，并包含 stage、request_id、trace_id | 已新增 `tests/unit/cognition/CognitionFacadeStageTimeoutTest.cpp` 并更新 `tests/unit/cognition/CMakeLists.txt`：模拟 planning/reflection 慢 bridge，断言 fail-fast、late result 不污染后续请求；`CognitionFacadeFlowTest` / `CognitionFacadeDegradedModeTest` 回归通过 | `cmake --build build-ci --target dasall_cognition_facade_stage_timeout_unit_test && ctest --test-dir build-ci -R "CognitionFacadeStageTimeoutTest" --output-on-failure` | 2026-05-14 已验收通过：超时阶段 fail-fast，错误包含 stage、request_id / trace_id，且不自建 retry |
| COG-FIX-004 | Done | 落地 LLM structured output 主链投影方案 A | 已选定方案 A；完整设计、风险控制、执行门禁与专项 TODO 见附录 A：bridge JSON payload 经 `StageOutputValidator` 校验后投影为 `PlanGraph` / `ActionDecision`，并由 Facade 主链消费 | 新增 schema valid / invalid、typed projection、facade structured-output、runtime interaction、telemetry 与负例回归测试，并通过 docs consistency 与统一 focused ctest 证明 malformed / invalid / drifted payload fail-closed，valid payload 可驱动主链对象 | `rg -n "COG-FIX-004" docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md && rg -n "Gate-COG-FIX004A" docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md && ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest|StageOutputValidatorSchemaTest|PlanGraphStructuredProjectionTest|ActionDecisionStructuredProjectionTest|CognitionFacadeStructuredPlanOutputTest|CognitionFacadeStructuredActionOutputTest|CognitionStructuredOutputIntegrationTest|CognitionRuntimeInteractionContractTest|CognitionTelemetryFieldsTest"` | 文档、代码、tests 与 diagnostics 均能证明 bridge output 从 advisory 证据升级为受治理的主链对象来源，且 `Gate-COG-FIX004A-05` 已通过 |
| COG-FIX-005 | Done | 接入 production CognitionTelemetry sink | 已扩展 `CognitionRuntimeDependencies` 并在 `CognitionTelemetry` 内部适配 live audit / metrics / tracing provider；`CognitionFacade` 与 `ResponseBuilder` 均改用 live telemetry owner，`runtime/src/AgentFacade.cpp` 与 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 负责透传 provider | 已新增 `tests/integration/cognition/CognitionProductionTelemetryIntegrationTest.cpp`，并结合 `CognitionTelemetryFieldsTest`、`CognitionTelemetryRedactionTest`、`CognitionTelemetryFailureIsolationTest` 断言 stage completed / failed / response degraded 已被 emit 且 redaction 生效 | `cmake -S . -B build && cmake --build build --target dasall_apps_runtime_support dasall_cognition_telemetry_fields_unit_test dasall_cognition_telemetry_redaction_unit_test dasall_cognition_telemetry_failure_isolation_unit_test dasall_cognition_production_telemetry_integration_test && ctest --test-dir build --output-on-failure -R "^(CognitionTelemetryFieldsTest|CognitionTelemetryRedactionTest|CognitionTelemetryFailureIsolationTest|CognitionProductionTelemetryIntegrationTest)$"` | production composition 不再只能使用 no-op sink；degraded response 已有 production-style 可观测证据 |
| COG-FIX-006 | Done | 回写 Gate-COG-12 后的当前状态 | 已更新 `docs/architecture/DASALL_cognition子系统详细设计.md`、`docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` 和本文档 cognition 章节，统一 Gate-COG-12、L2 完成、L4 未证明与 production telemetry sink 已完成的口径 | 文档检索现可区分历史 baseline、当前 L2 完成、L4 未证明和后续 repo-wide blocker | `rg -n "COG-GAP|COG-FIX|Gate-COG-12|cognition L4 未证明|DirectResponse|ConvergeSafe" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition docs/worklog/DASALL_开发执行记录.md docs/todos/DASALL_子系统查漏补缺专项记录.md` | cognition 当前状态跨文档一致，后续不再误读为 placeholder-only 或 production-ready |

#### COG-FIX-001 完成证据（2026-05-14）

1. `runtime/src/AgentOrchestrator.cpp` 已补齐 live cognition path 的 terminal decision 分支：`DirectResponse` / `ConvergeSafe` 现在从 `Reasoning` 第一跳进入 `Responding`，并在无 tool observation 时调用 `IResponseBuilder.build()`；`NoDecision` 继续 fail-fast。
2. `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` 已新增 DirectResponse、ConvergeSafe、NoDecision 三类 fixture，并使用 response builder probe 断言 Runtime 会把 terminal decision 原样交给 `IResponseBuilder`。
3. 验收命令 `cmake --build build-ci --target dasall_cognition_runtime_interaction_contract_integration_test && ctest --test-dir build-ci -R "CognitionRuntimeInteractionContractTest" --output-on-failure` 已通过。

#### COG-FIX-002 完成证据（2026-05-14）

1. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已新增 `DASALL_RUNTIME_COGNITION_FIRST=1` 受控 gate；开启后 app live composition 会写入 `runtime:<owner>:cognition-first-forced` marker，并停止注入 `required-live-baseline`，从而让 app-binary run 不再被 production direct LLM path 抢占。
2. `runtime/src/AgentOrchestrator.cpp` 的 live cognition path 现已复用 `live_context_degradation_is_fatal()`，与 direct path 对齐 tolerable context warning 语义，不再因首轮 `goal_summary` fallback 等允许降级而提前 fail-fast。
3. `tests/integration/access/DaemonBinaryUnarySmokeTest.cpp` 现在在 cognition-first gate 下拉起真实 `dasall-daemon` + `dasall-cli run`，human/JSON 输出都断言无 `llm.origin=` 且包含 `runtime unary integration completed:`；同轮 `DaemonRuntimeLiveDependencyCompositionTest` 与 `CognitionRuntimeIntegrationTest` 已通过。

#### COG-FIX-003 完成证据（2026-05-14）

1. `cognition/src/CognitionFacade.cpp` 已新增阶段 deadline runner：Perception / Planner / Reasoner / Reflection 与 bridge 调用现在都会在门面层按 `StageExecutionPlan.deadline_ms` / `StageModelHint.deadline_ms` 做超时裁定；一旦超时即 fail-fast，并返回带 `cognition.stage_timeout` message、stage、request_id、trace_id 的 `ErrorInfo`，不在 cognition 内部自建 retry。
2. `tests/unit/cognition/CognitionFacadeStageTimeoutTest.cpp` 已新增 planning/reflection 慢 bridge 场景，断言 Facade 会在 deadline 内返回超时结果，并在首轮超时后允许下一次请求走通，证明 late result 不污染后续请求；`tests/unit/cognition/CMakeLists.txt` 已同步接入新 target。
3. 验收命令 `cmake --build build-ci --target dasall_cognition_facade_stage_timeout_unit_test && ctest --test-dir build-ci -R "CognitionFacadeStageTimeoutTest" --output-on-failure` 已通过；相邻回归 `cmake --build build-ci --target dasall_cognition_facade_flow_unit_test dasall_cognition_facade_degraded_mode_unit_test dasall_cognition_facade_stage_timeout_unit_test && ctest --test-dir build-ci -R "CognitionFacade(Flow|DegradedMode|StageTimeout)Test" --output-on-failure` 也已通过。

#### COG-FIX-005 完成证据（2026-05-16）

1. `cognition/include/CognitionDependencies.h` 已扩展 live audit / metrics / tracing provider 依赖，`cognition/src/observability/CognitionTelemetry.cpp` 内新增 infra-backed sink adapter，`cognition/src/CognitionFacade.cpp` 与 `cognition/src/response/ResponseBuilder.cpp` 现都会以 live provider 优先构造 telemetry owner；`ResponseBuilder` 的 invalid / failure / template fallback 路径也会显式发射 `response.degraded`。
2. `runtime/src/AgentFacade.cpp` 与 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 现会在 runtime 组合 cognition engine / response builder 时透传 shared observability providers，避免 production composition 仍然落到 no-op sink；`tests/unit/runtime/RuntimeSmokeTest.cpp` 也已改成真实 runtime smoke，保证相关目标能构建执行。
3. 验收命令 `cmake -S . -B build && cmake --build build --target dasall_apps_runtime_support dasall_runtime_smoke_test dasall_runtime_cognition_loop_smoke_unit_test dasall_cognition_telemetry_fields_unit_test dasall_cognition_telemetry_redaction_unit_test dasall_cognition_telemetry_failure_isolation_unit_test dasall_cognition_production_telemetry_integration_test && ctest --test-dir build --output-on-failure -R "^(RuntimeBuildLivenessSmokeTest|RuntimeCognitionLoopSmokeTest|CognitionTelemetryFieldsTest|CognitionTelemetryRedactionTest|CognitionTelemetryFailureIsolationTest|CognitionProductionTelemetryIntegrationTest)$"` 已通过。

#### COG-FIX-006 完成证据（2026-05-16）

1. `docs/architecture/DASALL_cognition子系统详细设计.md` 已补齐当前观测状态：`CognitionTelemetry` 的 live provider 接线、`response.degraded` production emit 与 `CognitionProductionTelemetryIntegrationTest` 验收出口均已写入当前设计口径。
2. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 与 `docs/worklog/DASALL_开发执行记录.md` 已移除“production sink 未完成”的当前态表述，并新增 2026-05-16 的完成回写，明确 production telemetry sink 已完成，但 installed / qemu / soak 的 L4 证据仍未证明。
3. 验收命令 `rg -n "COG-GAP|COG-FIX|Gate-COG-12|cognition L4 未证明|DirectResponse|ConvergeSafe" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition docs/worklog/DASALL_开发执行记录.md docs/todos/DASALL_子系统查漏补缺专项记录.md` 应返回一致的当前态语义，不再把 placeholder baseline 或 production sink pending 误读为现状。

### 4.7 非缺口但需守住的边界

以下事项当前不是缺口，后续修改时应继续守住：

1. `PlanGraph`、`ActionDecision`、`BeliefUpdateHint`、`StageModelHint` 继续保持 cognition module-local / module-public，不应为了补 runtime 映射而推入 shared contracts。
2. cognition 不直接 retrieve Memory、不直接写 Memory；只通过 `ContextSufficiencySignal` 和 `BeliefUpdateHint` 通知 Runtime。
3. cognition 不直接构造 `ToolRequest`，只输出 `tool_intent_hint`；真实工具调用仍归 Runtime / ToolManager。
4. ReflectionEngine 不执行 retry / replan / abort，只输出 `ReflectionDecision`；恢复准入和执行仍归 Runtime / RecoveryManager。
5. LLM PromptRegistry / PromptComposer 仍归 llm 子系统；cognition 只传 stage hints、task_type、schema / budget 约束。
6. production direct LLM path 如果继续保留，必须在证据文档中明确它与 cognition path 的区别，避免把 direct path 成功外推为 cognition 成功。

### 4.8 建议复验命令

本节命令用于后续把本章结论升级或关闭缺口。若使用 VS Code CMake Tools，应优先选择对应 build target / CTest target 执行；以下 shell 形式仅作为文档化验收口径。

聚焦 cognition 单元回归：

```bash
cmake --build build-ci --target dasall_cognition dasall_unit_tests && \
ctest --test-dir build-ci --output-on-failure -R "Cognition(Facade|LlmBridge|StageOutputValidator|Telemetry|Interface|Config|Policy|Perception|Planner|Reasoner|Reflection|Belief|Response)"
```

Runtime / cognition 交互契约：

```bash
cmake --build build-ci --target dasall_integration_tests && \
ctest --test-dir build-ci --output-on-failure -R "RuntimeCognitionLoopSmokeTest|CognitionRuntimeIntegrationTest|CognitionRuntimeInteractionContractTest|CognitionFailureInjectionIntegrationTest|CognitionProfileCompatibilityTest|CognitionReviewRegressionTest"
```

共享契约回归：

```bash
cmake --build build-ci --target dasall_contract_tests && \
ctest --test-dir build-ci --output-on-failure -R "GoalContract|BeliefState|ContextPacket|Observation|ReflectionDecision|AgentResult|MainFlowContractE2E"
```

installed 证据边界复核：

```bash
rg -n "has_production_llm_direct_path|required-live-baseline|cognition_engine->decide|response_builder->build|llm.origin|cognition L4 未证明" \
  runtime/src/AgentOrchestrator.cpp \
  apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp \
  docs/todos/integration/deliverables/FULLINT-TODO-003-runtime-cognition-memory-llm主链证据包.md \
  docs/ssot/BusinessChainIntegrationMatrix.md
```

### 4.9 当前章节结论

cognition 子系统的实现覆盖度已经很高，当前主要问题不再是 cognition 本体、terminal decision 消费、阶段超时隔离、structured output 主链角色或 production telemetry 接入，而是更高层的 installed / qemu / soak 证据与 Gate-COG-12 所依赖的 repo-wide non-cognition blocker 仍未清零。

因此本章冻结口径为：

1. cognition 主体：已完成 L1 / L2 自有代码、focused tests 与当前态文档回写，当前不再存在本章内的 self-owned 结构性缺口。
2. runtime 非 direct cognition path：`DirectResponse` / `ConvergeSafe` 第一跳映射、timeout isolation、structured output authoritative consumption 与 production telemetry sink 已闭合；这些项后续只需维持回归，不再作为新增补缺任务。
3. installed production 主链：仍不能宣称 cognition 必经；当前只可记录为 source / integration evidence，L4 installed / qemu / soak positive evidence 仍待独立补证。
4. 后续查漏补缺优先级：从 cognition owner 内部缺口转向 Gate-COG-12 关联的 repo-wide non-cognition blocker，以及更高层 installed/package 证据链闭合。

## 5. LLM 子系统查漏补缺

### 5.1 检查范围与依据

本轮检查目标：核对 `llm/*` 是否覆盖 `docs/architecture/DASALL_llm子系统详细设计.md` 中规定的功能，并判断与 Runtime、Cognition、Profiles、Infra、Access / apps、installed package 证据链路是否打通。

检查范围：

1. LLM 源码与公共接口：`llm/include/*`、`llm/src/*`、`llm/CMakeLists.txt`、`llm/assets/prompts/*`、`llm/assets/providers/*`。
2. LLM 单元与集成测试：`tests/unit/llm/*`、`tests/integration/llm/*`、`tests/contracts/LLMRequestResponseContractTest.cpp`。
3. 生产装配与调用链路：`llm/src/LLMProductionFactory.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`runtime/src/AgentOrchestrator.cpp`、`cognition/src/llm/CognitionLlmBridge.cpp`。
4. 证据边界：`docs/ssot/BusinessChainIntegrationMatrix.md`、`docs/ssot/SystemIntegrationGateMatrix.md`、`docs/todos/llm/*`、`docs/worklog/DASALL_开发执行记录.md`。

本轮执行验证：使用 VS Code CMake Tools 执行 LLM 聚焦 CTest，覆盖 46 个 LLM / Prompt / Provider / Router / Manager / Normalizer / Observability / Audit / contract / integration 测试，结果为 `100% tests passed, 0 tests failed`。其中包括 `LLMSubsystemSmokeIntegrationTest`、`DeepSeekDualModeSelectionIntegrationTest`、`LLMFallbackIntegrationTest`、`LLMPromptSourceSwitchIntegrationTest`、`LLMPersonaSelectionIntegrationTest`、`LLMGovernanceFailureIntegrationTest`、`LLMProfileIntegrationTest`、`LLMProviderAssetOnboardingIntegrationTest` 和 `LLMRequestResponseContractTest`。

### 5.2 总体结论

结论：`llm/*` 已经基本覆盖 LLM 详设 D1-D9 的主体功能，不能再沿用详设早期“placeholder-only”的历史状态口径。当前 LLM 子系统已具备公共接口、profile 投影、Prompt asset / Provider asset loader、PromptPipeline、ModelRouter、LLMManager unary 主链、adapter registry、OpenAI-compatible adapter、usage / response normalization、observability bridge 和 integration smoke。

但当前不能判定为“所有功能完全完成、所有关联模块链路全部打通”。更准确的状态是：

1. LLM 模块内部主体实现达到 L1 / L2 可信度；既有 46 个 focused / integration / contract 测试与新增 `LLMProductionObservabilityIntegrationTest` focused evidence 已通过。
2. Runtime production direct LLM path 已接入 `ILLMManager`，`RuntimeLiveDependencyComposition` 会创建 production LLM manager，`AgentOrchestrator` 已有 `llm.origin=` 输出和禁用 `agent.dataset` fallback 的生产直连路径。
3. `docs/ssot/BusinessChainIntegrationMatrix.md` 记录 BC-07 LLM production generation 已有 installed-package local L4 证据：`sudo -n dasall run ...` 返回 DeepSeek `llm.origin` 且未出现 `agent.dataset`。
4. D10 streaming 已由 `LLM-FIX-001` 在 llm owner 内部收口：`LLMManager::stream_generate()`、`OpenAICompatibleAdapter::stream_generate()` 与 `StreamSessionRegistry` 已形成 module-local streaming 闭环；shared `StreamHandle` admission 仍保持 deferred。
5. `LLMProductionFactory` 已按 provider catalog 自动注册 `openai_compatible`、`ollama_native` 与 `local_runtime` provider family；Cloud / LAN / Local unary 与 fallback focused evidence 已闭合，但这不外推为所有 family 的 streaming 已完成。
6. production factory + runtime composition 现已把 logger / metrics / trace / audit sink 接入 `LLMManager` hot path；`LLMProductionObservabilityIntegrationTest` 已证明 production-composed manager 会自动发出 structured log、metrics、trace 与 reasoning strip audit event。

### 5.3 设计覆盖矩阵

| 设计域 | 当前覆盖判断 | 主要证据 | 缺口 / 边界 |
|---|---|---|---|
| D1 公共接口与 shared contract | 已覆盖 | `ILLMManager`、`ILLMAdapter`、`ILLMTransport`、`LLMRequestResponseContractTest`、`LLMInterfaceSurfaceTest` | streaming 形状存在但不代表 stream-ready |
| D2 profile -> LLM config 投影 | 已覆盖 | `project_llm_subsystem_config()`、`LLMSubsystemConfigProjectionTest`、`ProviderConfigProjectionTest`、`LLMProfileIntegrationTest` | production route family 覆盖仍取决于 factory 注册能力 |
| D3 ModelRouter | 已覆盖 | `ModelRouterPolicyTest`、`ModelRouterFallbackTest`、`ModelRouterReasoningModeSelectionTest`、`DeepSeekDualModeSelectionIntegrationTest` | canonical stage key 与部分历史 alias 需持续收敛，但当前不构成核心缺口 |
| D4 Prompt / Provider asset | 已覆盖 | `PromptAssetRepository`、`ProviderCatalogRepository`、baseline / deployment / snapshot overlay、schema / version / content hash、`LLMProviderAssetOnboardingIntegrationTest` | asset-only onboarding 已证明 deployment overlay OpenAI-compatible provider instance，以及 baseline LAN / Local admitted family projection；真实 endpoint / secret / header 注入链仍未证明 |
| D5 PromptComposer | 已覆盖 | template renderer、slot mapping、budget clamp、prompt identity stamping；`PromptComposerSlotMappingTest`、`PromptComposerOverBudgetTest`、smoke integration | 不负责 memory retrieve，符合 ADR-006 |
| D6 PromptPolicy | 已覆盖 | trusted source、allowlist、tool visibility、redaction、render budget；`PromptPolicyAllowlistTest`、`PromptPolicyToolVisibilityTest`、`LLMGovernanceFailureIntegrationTest` | policy sink / audit 生产热路径仍需接线证明 |
| D7 LLMManager 主链 | 已覆盖 | `LLMManager::generate()` 与 `LLMManager::stream_generate()` 已分别覆盖 unary 与 llm internal streaming 编排；manager success/failure/timeout/retry/concurrency 与 streaming integration tests 已落地 | shared stream handle 仍未 admission |
| D8 adapters / transport | 已覆盖 | `OpenAICompatibleAdapter`、`OllamaAdapter`、`LocalLLMAdapter` + `CurlCommandLLMTransport`；adapter health / protocol mapping tests；`LLMProductionFactoryTest`、`LLMFallbackIntegrationTest`、`LLMProviderAssetOnboardingIntegrationTest` | Ollama / Local family 的 streaming 仍保持 placeholder；真实 endpoint / secret / release evidence 仍未证明 |
| D9 observability / integration smoke | 已覆盖 | `LLMMetricsBridge`、`LLMTraceBridge`、`LLMAuditBridge`、`LLMSubsystemSmokeIntegrationTest`、`LLMObservabilityFieldCompletenessTest`、`LLMAuditEventCoverageTest`、`LLMProductionObservabilityIntegrationTest` | production-composed observability sink 已接线；installed / qemu / soak 证据仍需独立补齐 |
| D10 streaming 生命周期 | 已由 LLM-FIX-001 收口 | `StreamSessionRegistry`、`LLMManager::stream_generate()`、`OpenAICompatibleAdapter::stream_generate()` 与三条 focused tests 已落地；cognition streaming preference 不再必然落入 unimplemented path | shared `StreamHandle` admission 仍 deferred；Ollama / Local streaming 仍 placeholder，production sink 仍待补齐 |
| installed / production evidence | 已由 LLM-FIX-004 / 007 收口到 L5 / L6 partial | `LLMProductionFactory`、runtime direct path、PKG-TODO-018 qemu authoritative `pkg-smoke-local-control-plane PASS`、BC-07 `llm.origin` 断言、`LLM-FIX-004` 交付文档、release-runner workflow/script contract 与 `LLM-FIX-007` soak plan | 当前已具历史 L5 qemu / release evidence 与仓库级 rerun contract；`FULLINT-TODO-019` owner 当前 release candidate rerun / artifact archive，007 已把 external provider 长稳态 / L6 soak 拆成可执行 slices |

### 5.4 关联模块链路判断

| 链路 | 是否打通 | 当前可信层级 | 判断 |
|---|---|---:|---|
| Profiles -> LLM config | 基本打通 | L2 | typed projection、route / allowlist / timeout / fallback 在 unit 与 profile integration 中验证 |
| LLM assets -> PromptPipeline / ProviderCatalog | 基本打通 | L2 | baseline / deployment / snapshot source switch、persona selection、provider onboarding 均有 integration 证据 |
| Runtime live composition -> LLM | 已打通 | L2 / L4 partial | production factory 创建 `ILLMManager` 并注入 runtime；runtime composition 现已先组合 observability bundle，再把 logger / metrics / tracer / audit sink 与 OpenAI / Ollama / Local routes 一并接入 production manager |
| Runtime -> LLM production generate | 已打通 | L5 / L4 rerun | `AgentOrchestrator` direct path 调 `llm_manager->generate()`；BC-07 已记录 installed local `llm.origin` 正向证据，PKG-TODO-018 qemu authoritative package smoke 又在 machine-isolation testbed 复用了同一 installed LLM 断言 |
| Cognition -> LLM | 基本打通 | L1 / L2 | `CognitionLlmBridge` 可调用 `generate()` / `stream_generate()`；unary 可用，streaming 会落入未实现路径 |
| LLM -> Infra secret | 部分打通 | L0 / L5 partial | production factory 使用 `FileSecretBackend` + curl transport；qemu authoritative package smoke 已证明 installed secret import + provider call 正向路径，release-runner workflow/script 现已固定 secret/network preflight contract |
| LLM -> Infra metrics / trace / audit | 基本打通 | L1 / L2 | bridge 类、fixture smoke 与 production observability integration 已通过；production factory / runtime composition 现已注入 logger、metrics provider、tracer provider 与 audit logger |
| LLM -> Memory / Tools / Recovery | 边界正确 | L0 / L2 | 未发现 LLM 直接拥有 memory context、tools execution 或 recovery decision；符合 ADR-006/007/008 |
| Access / apps -> Runtime -> LLM | 部分打通 | L3 / L5 partial | app runtime_support composition、installed `dasall run` 与 qemu authoritative package smoke 可共同证明 CLI -> daemon -> runtime -> LLM direct generation；HTTP/gateway 与 L6 soak 仍不由该证据外推 |

### 5.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| LLM-GAP-001 | High | D10 streaming lifecycle 已由 LLM-FIX-001 收口 | `llm/src/stream/StreamSessionRegistry.*`、`LLMManager::stream_generate()`、`OpenAICompatibleAdapter::stream_generate()` 已落地 module-local 生命周期 owner、SSE/delta merge 与终态收口；`StreamSessionLifecycleTest` / `LLMStreamingIntegrationTest` / `CognitionLlmBridgeErrorMappingTest` 通过 | cognition streaming preference 不再必然 fail-closed；shared StreamHandle admission 仍保持 deferred，不外推为 shared stream-ready | 继续保持 shared `StreamHandle` / contracts admission 后置，仅在 llm 内部扩展 provider family 与 production sink |
| LLM-GAP-002 | Medium | production factory 多 family 注册已由 LLM-FIX-002 收口 | `LLMProductionFactory` 已按 `adapter_family` 注册 OpenAI-compatible / Ollama / Local routes；baseline provider catalog 已补齐 `ollama_lan` / `local_runtime`；`LLMProductionFactoryTest`、`LLMProviderAssetOnboardingIntegrationTest`、`LLMFallbackIntegrationTest` 通过 | Cloud / LAN / Local production unary / fallback 不再停留在 fixture 手工注册 | 继续保持 provider assets 与 `adapter_family` 映射一致；下一步转向 production observability / audit sink |
| LLM-GAP-003 | Medium | production metrics / trace / audit sink 已由 LLM-FIX-003 收口 | `LLMProductionFactoryOptions` 现已接入 infra logger / metrics provider / tracer provider / audit logger，`RuntimeLiveDependencyComposition` 也已先组合 observability bundle 再装配 production manager；`LLMProductionObservabilityIntegrationTest` 通过 | production-composed manager 不再只在 fixture 中可观测，reasoning strip audit 已进入 manager hot path | 后续转向 L5 release / qemu 证据与 external provider 长稳态 |
| LLM-GAP-004 | Medium | 当前 release candidate rerun 与 L6 长稳态仍未证明 | `LLM-FIX-004` 已把 BC-07 / BC-16 收口为“历史 authoritative qemu L5 + 当前 local L4 rerun + release-runner contract fixed”；`LLM-FIX-007` 又已把 provider jitter、network loss、secret rotate、retry budget exhaustion 与 observability trend 冻结为可执行验收项；但 `FULLINT-TODO-019` 在当前主机仍缺 runner-local `qemu_image` 与 `DASALL_DEEPSEEK_API_KEY_FILE`，而 007 的 soak slices 也尚未真正执行 | 若误把现有 focused failure-handling、计划文档或历史 qemu PASS 当成当前 release confidence，会高估 production ready 程度 | 通过 `FULLINT-TODO-019` 复跑当前 release candidate；按 `LLM-FIX-007` deliverable 执行 provider soak / chaos / failure trend evidence |
| LLM-GAP-005 | Low / Medium | LLM 源码边界缺少自动化回归防线 | 静态检查未发现越界，但当前缺少明确 test/script 防止 `llm/` 未来 include memory/tools/apps/runtime 私有实现 | 后续改动可能破坏 ADR-006/007/008 边界 | 增加 boundary compliance test 或脚本，锁定 LLM 不直拉 Memory ContextOrchestrator、不直调 Tools、不触碰 RecoveryManager |

### 5.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| LLM-FIX-001 | Done | 实现 streaming 生命周期 fail-closed -> 可控可测 | 已更新 `llm/src/LLMManager.cpp`、`llm/src/adapters/OpenAICompatibleAdapter.cpp`、`llm/src/stream/IStreamObserver.h`、`llm/src/stream/StreamSessionRegistry.*`，落地 module-local lifecycle owner、observer 回调、SSE / delta merge、终态 usage 收口与 streaming route 执行 | 已重写 `StreamSessionLifecycleTest`、新增 `LLMStreamingIntegrationTest`，并扩展 `CognitionLlmBridgeErrorMappingTest` 覆盖 streaming preference | `Build_CMakeTools(buildTargets=["dasall_stream_session_lifecycle_unit_test","dasall_cognition_llm_bridge_error_mapping_unit_test","dasall_llm_streaming_integration_test"])`；`RunCtest_CMakeTools(tests=["StreamSessionLifecycleTest","LLMStreamingIntegrationTest","CognitionLlmBridgeErrorMappingTest"])` | manager/adapters 不再返回 placeholder；registry cancel/overflow、OpenAI-compatible SSE success/observer rejection、manager streaming success 与 cognition streaming failure projection 均有 focused tests |
| LLM-FIX-002 | Done | 补齐 production provider family 注册 | 已更新 `LLMProductionFactory` / `LLMProductionFactoryOptions`，把 provider `adapter_family` 映射到 OpenAI-compatible / Ollama / Local adapter factory，并为 source-tree 验证补齐 provider catalog baseline override seam；baseline provider catalog 已补 `ollama_lan` / `local_runtime` | 已新增或扩展 `LLMProductionFactoryTest`、`LLMProviderAssetOnboardingIntegrationTest`、`LLMFallbackIntegrationTest`，覆盖 openai + ollama + local production registration 与 fallback | `RunCtest_CMakeTools(tests=["LLMProviderAssetOnboardingIntegrationTest","LLMFallbackIntegrationTest","LLMProductionFactoryTest"])` | production factory 已能按 provider catalog 注册多 family routes，并证明 fallback chain 不只停留在 fixture 手工注册 |
| LLM-FIX-003 | Done | 接入 production observability / audit sink | 已扩展 `LLMProductionFactoryOptions`、`RuntimeLiveDependencyComposition` 与 `LLMManager`：runtime composition 先组合 live observability bundle，再把 infra logger / metrics provider / tracer provider / audit logger 注入 production-composed manager；reasoning strip audit event 现由 manager hot path 自动发出 | 已新增 `LLMProductionObservabilityIntegrationTest` 并更新 llm integration 注册；focused validation 同时构建 `dasall_apps_runtime_support`，断言 production-composed manager 产生日志、指标、trace 和 reasoning strip audit event | `cmake --build build-ci --target dasall_llm_production_observability_integration_test dasall_apps_runtime_support -j4 && ctest --test-dir build-ci --output-on-failure -R '^LLMProductionObservabilityIntegrationTest$'` | production factory 与 runtime composition 路径现已具备一致的 observability / audit sink 接线，audit event 不再只能由 test 手动调用 |
| LLM-FIX-004 | Done | 收口 L5 / release runner / provider 长稳态证据 | 已新增 `docs/todos/llm/deliverables/LLM-FIX-004-L5-release-runner-provider长稳态证据收口.md`，并同步 packaging workflow/script、SSOT、integration TODO 与 worklog；本轮不新增 llm 产品代码 | authoritative qemu 历史证据、当前 installed local rerun、release-runner contract 与 provider failure-handling basis 已统一回写 | `rg -n "LLM-FIX-004|BC-07|BC-16|FULLINT-BLK-001|DASALL-Release-Package-Gate|llm.origin=deepseek-prod" docs/todos docs/ssot docs/worklog scripts/packaging .github/workflows`；`ctest --test-dir build-ci --output-on-failure -R "(LLMFallbackIntegration|LLMProfileIntegration|DeepSeekDualModeSelectionIntegration|LLMProductionObservabilityIntegration|LLMManager(TimeoutPolicy|RetryBudget|ConcurrencyGuard)|ModelRouterStability)Test"` | BC-07 / BC-16 已统一记录为“历史 authoritative qemu L5 + 当前 local rerun + release-runner contract fixed”；provider timeout/retry/fallback/route stability 有明确 failure-handling basis；同时显式保留 FULLINT-TODO-019 当前 release candidate 复跑与 L6 soak 未完成的边界 |
| LLM-FIX-005 | Done | 建立 LLM 边界回归防线 | 已新增 `tests/unit/llm/LLMBoundaryGuardComplianceTest.cpp` 与 `tests/unit/llm/CMakeLists.txt` 接线，自动扫描 `llm/` source/include、`llm/CMakeLists.txt` 以及 `PromptPipeline` / `PromptComposer` retrieval token，拒绝 include/link memory、tools、apps、runtime 私有实现 | `LLMBoundaryGuardComplianceTest`、`LLMInterfaceSurfaceTest`、`LLMRequestResponseContractTest` | `cmake -S . -B build-ci && cmake --build build-ci --target dasall_llm_boundary_guard_compliance_unit_test dasall_llm_interface_surface_unit_test dasall_contract_tests -j4 && ctest --test-dir build-ci --output-on-failure -R '(LLMBoundaryGuardCompliance|LLMInterfaceSurface|LLMRequestResponseContract)Test'` | ADR-006/007/008 边界已有自动化守护，后续改动不能悄悄越界 |
| LLM-FIX-006 | Done | 回写 LLM 当前状态与历史 baseline 差异 | 已新增 `docs/todos/llm/deliverables/LLM-FIX-006-llm详设当前状态追溯收敛.md`，并同步 `docs/architecture/DASALL_llm子系统详细设计.md`、`docs/todos/llm/DASALL_llm子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` 与本文档 | 文档一致性检索已区分 D1-D9 当前闭合、D10 已在 module-local 边界收口、shared admission No-Go 与 historical L5 / current rerun 证据边界 | `rg -n '当前只编译 src/placeholder.cpp|tests/unit/llm/CMakeLists.txt 仍为占位|tests/integration/llm 目录尚不存在|当前没有 LLMManager、ModelRouter、Prompt 三段或 adapter 实现' docs/architecture/DASALL_llm子系统详细设计.md; test $? -eq 1`；`rg -n 'LLM-FIX-006|历史 baseline|shared admission No-Go|历史 authoritative qemu L5|current rerun|边界回归防线' docs/architecture/DASALL_llm子系统详细设计.md docs/todos/llm docs/worklog/DASALL_开发执行记录.md docs/todos/DASALL_子系统查漏补缺专项记录.md` | 后续评审不再把旧 placeholder baseline 当当前结论，也不把 llm internal streaming closure 误写成 shared stream-ready；当前 source boundary guard 已由 `LLM-FIX-005` 收口，current rerun 与 L6 soak 则继续留在 `FULLINT-TODO-019` 与 `LLM-FIX-007` |
| LLM-FIX-007 | Done | 固化 external provider 长稳态与 L6 soak 执行计划 | 已新增 `docs/todos/llm/deliverables/LLM-FIX-007-external-provider长稳态与L6-soak执行计划.md`，把 provider jitter、network loss、secret rotate、retry budget exhaustion 与 observability trend 拆成可执行验收项，并同步详设 / TODO / worklog 口径；本轮不新增 llm 产品代码 | external provider soak / chaos / failure trend 的执行矩阵、命令模板、artifact 口径与通过判定已冻结 | `rg -n 'LLM-FIX-007|provider jitter|network loss|secret rotate|retry budget exhaustion|observability trend|SOAK-0' docs/todos/llm/DASALL_llm子系统专项TODO.md docs/todos/llm/deliverables/LLM-FIX-007-external-provider长稳态与L6-soak执行计划.md docs/todos/DASALL_子系统查漏补缺专项记录.md docs/architecture/DASALL_llm子系统详细设计.md docs/worklog/DASALL_开发执行记录.md`；`rg -n 'DASALL_AUTOPKGTEST_SETUP_COMMANDS_BOOT|DASALL_DEEPSEEK_API_KEY_FILE|config apply --from-file|LlmSecretPageTest|LLMManagerRetryBudgetTest|LLMProductionObservabilityIntegrationTest' docs/todos/llm/deliverables/LLM-FIX-007-external-provider长稳态与L6-soak执行计划.md scripts/packaging/validate_gate_int_10_installed_package_qemu.sh scripts/packaging/pkg_smoke_install.sh tests/unit/apps/cli/LlmSecretPageTest.cpp tests/unit/llm/LLMManagerRetryBudgetTest.cpp tests/integration/llm/LLMProductionObservabilityIntegrationTest.cpp .github/workflows/release-package-gate.yml` | current release candidate rerun 与 external provider soak 执行矩阵不再混写；007 完成的是计划冻结，不是宣称 L6 evidence 已执行 |

### 5.7 非缺口但需守住的边界

以下事项当前不是缺口，后续修改时应继续守住：

1. LLM 掌 Prompt / Provider / ModelRouter / Adapter 调用，不掌 ContextOrchestrator；Memory context 仍由 memory / runtime owner 负责。
2. PromptPipeline / PromptComposer 不主动 retrieve memory，也不直接拼装 runtime context；只消费 request 中已经归一化的 prompt 输入。
3. ModelRouter 只做基于 profile、provider catalog、health、budget、latency、reasoning / tools 约束的 deterministic selection，不引入额外 LLM 推理。
4. LLM 不直接调用 ToolManager，不拥有工具执行权限；tool visibility 只在 PromptPolicy 中治理提示词可见性。
5. LLM 不裁定 recovery；provider failure、fallback exhausted、timeout 只返回 ErrorInfo / failure category，由 Runtime / RecoveryManager 决定系统恢复动作。
6. `agent.dataset` fallback 不能再作为 installed `run` 成功语义；Runtime production LLM path 已明确禁用该 fallback，应继续保持。
7. DeepSeek dual mode、persona selection、prompt source switch、asset-only onboarding 当前已有 integration 证据，不应重复列为缺口；后续只需守住 regression gate。

### 5.8 建议复验命令

本节命令用于后续把本章结论升级或关闭缺口。若使用 VS Code CMake Tools，应优先选择 `ListTests_CMakeTools` / `RunCtest_CMakeTools` 执行；以下 shell 形式仅作为文档化验收口径。

本轮已通过的 LLM 聚焦测试等价命令口径：

```bash
ctest --test-dir build/vscode-linux-ninja --output-on-failure -R "LLM|Prompt|Provider|ModelRouter|Adapter|ResponseNormalizer|TokenEstimator|TemplateRenderer"
```

production / installed 证据边界复核：

```bash
rg -n "create_production_llm_manager|openai_compatible|LLMMetricsBridge|LLMTraceBridge|LLMAuditBridge|stream_generate|llm.origin|agent.dataset fallback is disabled" \
  llm/src/LLMProductionFactory.cpp \
  llm/src/LLMManager.cpp \
  llm/src/adapters \
  apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp \
  runtime/src/AgentOrchestrator.cpp \
  docs/ssot/BusinessChainIntegrationMatrix.md
```

release / qemu gate 口径：

```bash
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>
```

边界回归口径：

```bash
rg -n "ContextOrchestrator|IMemoryManager|ToolManager|RecoveryManager|AgentOrchestrator|apps/" llm/include llm/src
```

### 5.9 当前章节结论

LLM 子系统的实现覆盖度已经很高，当前主要问题不是“缺少 LLM 模块本体”或“源码边界无人守护”，而是“release-runner / qemu 证据与 external provider 长稳态”尚未全部闭合。

因此本章冻结口径为：

1. LLM D1-D9 主体：基本完成，L1 / L2 证据充分，本轮 46 个聚焦测试通过。
2. Runtime production LLM generation：有 L4 local installed evidence，但不外推为 L5 qemu / release runner 或 L6 soak。
3. D10 streaming：已由 `LLM-FIX-001` 在 llm owner 内部完成 module-local 实现与 focused tests，但不能外推为 shared stream-ready。
4. production completeness：OpenAI-compatible、LAN / Local production family 与 metrics / trace / audit sink 已接入；当前 release candidate rerun / artifact archive 仍由 `FULLINT-TODO-019` owner，external provider 长稳态 / L6 soak 的执行矩阵则已由 `LLM-FIX-007` deliverable 固化。
5. 查漏补缺优先级：`LLM-GAP-001`、`LLM-GAP-002`、`LLM-GAP-003` 已分别由 `LLM-FIX-001`、`LLM-FIX-002`、`LLM-FIX-003` 收口；下一步继续通过 `FULLINT-TODO-019` 执行 current release candidate rerun，并按 `LLM-FIX-007` deliverable 逐条执行 external provider soak slices。

## 6. Memory 子系统查漏补缺

### 6.1 检查范围与依据

本轮检查目标：核对 `memory/*` 是否覆盖 `docs/architecture/DASALL_memory子系统详细设计.md` 中规定的五层记忆、上下文编排、写回、SQLite 存储、维护、配置、观测和跨模块链路，并判断当前实现是否仍存在功能、证据或生产化缺口。

检查范围：

1. Memory 源码与公共接口：`memory/include/*`、`memory/src/*`、`memory/CMakeLists.txt`、`sql/memory/*`。
2. Memory 单元、集成与 contract 测试：`tests/unit/memory/*`、`tests/integration/memory/*`、`tests/contracts/*Memory*`、`tests/contracts/ContextPacketFieldContractTest.cpp`。
3. Runtime / apps 接入链路：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`runtime/src/AgentOrchestrator.cpp`。
4. 证据边界：`docs/todos/memory/DASALL_memory子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`、`docs/ssot/BusinessChainIntegrationMatrix.md`、`docs/todos/integration/*`。

本轮执行验证：使用 VS Code CMake Tools 执行 37 个 Memory 聚焦 CTest，覆盖 context assembly、writeback、working memory、SQLite store、maintenance、profile projection、failure injection、integration 与 memory/context contract 测试，结果为 `100% tests passed, 0 tests failed`。

证据边界说明：本轮没有重新执行 installed package、qemu、release runner 或 soak；installed memory DB writeback 的 L4 local 结论来自既有 worklog / SSOT 记录，本章不把该证据外推为 L5 / L6。

### 6.2 总体结论

结论：`memory/*` 已经基本覆盖 Memory 详设主体功能，不能再沿用详设早期“placeholder-only”的历史状态口径。当前 Memory 子系统已具备 `IMemoryManager` facade、`ContextOrchestrator`、`CandidateCollector`、预算分配、压缩协调、`WritebackCoordinator`、冲突处理、WorkingMemoryBoard、SQLite store、WAL / reader pool、maintenance worker、profile projection 和聚焦测试矩阵。

但当前不能判定为“所有功能完全完成、所有关联链路全部达到 production-ready”。更准确的状态是：

1. Memory 模块内部主体实现达到 L1 / L2 可信度，本轮 37 个聚焦测试通过。
2. Runtime 已在 production direct LLM path、live cognition path、resume / context reload / belief writeback 等路径调用 `prepare_context()` 和 `write_back()`；apps runtime support 会创建并初始化 SQLite-backed memory manager。
3. `docs/worklog/DASALL_开发执行记录.md` 与 `docs/ssot/BusinessChainIntegrationMatrix.md` 已记录 installed local L4 memory writeback 证据，包括 `/var/lib/dasall/memory/memory.db`、`journal_mode=wal`、core tables 与 turn / summary / llm origin rows。
4. VectorMemory 的 `sqlite-vss` internal driver seam 已由 `MEM-TODO-035` 补齐 focused coverage；但真实 sqlite-vss loadable extension / third_party package / production factory wiring / installed-qemu 证据仍未闭合。
5. SQLite 版本基线与运行时 gate 已由 `MEM-FIX-002` 对齐：`memory/CMakeLists.txt` 已升级到 SQLite 3.51.3 对应 pin，`StorageConfig.sqlite_min_version` 与 `SqliteMemoryStore::open()` fail-closed gate 已落地，并由 `SqliteVersionGateTest`、`SqliteMemoryStoreTest`、`MemoryFailureInjectionTest` 验证。
6. Observability / audit / metrics / trace 已由 `MEM-FIX-003` 接入 production sinks：`MemoryRuntimeDependencies` 作为窄注入面进入 `IMemoryManager` factory，`memory/src/observability/MemoryObservability.*` 在 module-local bridge 中统一适配 logger / audit / metrics / tracing provider，`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已透传 live observability bundle；`MemoryObservabilityBridgeTest` 与 `RuntimeProductionHealthCompositionTest` 已通过。

### 6.3 设计覆盖矩阵

| 设计域 | 当前覆盖判断 | 主要证据 | 缺口 / 边界 |
|---|---|---|---|
| Public facade 与支撑类型 | 已覆盖 | `IMemoryManager`、`IContextOrchestrator`、`IMemoryStore` ISP surfaces、`MemoryConfig`、context/writeback/export/maintenance types 已落盘 | facade 不直接暴露 prompt/provider payload，符合 ADR-006 |
| WorkingMemory | 已覆盖 | `WorkingMemoryBoard`、snapshot export、writeback 更新、并发相关测试存在 | installed 长稳态 board checkpoint 不属于 memory owner，仍归 runtime checkpoint 证据 |
| Short-term Session / Turn / Summary | 已覆盖 | SQLite session / turn / summary CRUD、summary upsert、context candidate collection、contract tests | installed 多轮 reuse 可继续补正向证据 |
| Long-term Fact / Experience | 已覆盖 | fact / experience insert/query/supersede、conflict resolver、retention、writeback integration tests | 语义抽取质量仍依赖上游 runtime / cognition / LLM 输入，不由 memory 自行推理 |
| ContextOrchestrator | 已覆盖 | `CandidateCollector`、`BudgetAllocator`、`CompressionCoordinator`、`ContextPacketGuards`、slot projection、dropped section report | Memory 只产 `ContextPacket`，不渲染 Prompt；该边界需持续守住 |
| WritebackCoordinator | 已覆盖 | transactional core persist、derived data persist、working board update、conflict handling、quarantine path | vector sidecar 仍取决于 production factory / sqlite-vss extension 是否接线，不能由 focused seam 外推 real index |
| SQLite store / schema / WAL | 已覆盖 | writer connection、reader pool、`PRAGMA journal_mode` / `synchronous` / `foreign_keys` / `wal_autocheckpoint`、migrations install、SQLite 3.51.3 pin、open-time version gate、`SqliteVersionGateTest` / `SqliteMemoryStoreTest` / `MemoryFailureInjectionTest`、`SqliteMemoryStoreConcurrencyTest`、`MemoryContextIntegrationTest` | reader pool 借用防护已显式闭合；TSAN 仅保留为可选增强 |
| VectorMemory | 部分覆盖 | `VectorConfig`、profile projection、unavailable adapter、vector-disabled / unavailable tests；`SqliteVssVectorBackend` internal driver seam 与 focused unit tests 已落盘 | 真实 sqlite-vss 扩展未进入 third_party / packaging；production factory 与 installed/qemu 正向仍未证明 |
| Maintenance | 基本覆盖 | background worker、manual execute、WAL checkpoint、turn/fact/experience retention、quarantine cleanup、vector rebuild hook | vector rebuild 仍需 production sqlite-vss extension / factory / package 证据；当前不能从 internal seam 外推 installed rebuild |
| Profile projection | 基本覆盖 | `MemoryConfigProjector` 投影 recent turns、WAL、reader pool、maintenance、vector enabled / backend | 会投影 `SqliteVss`，但 production extension / factory availability 需 fail-closed 或补证据 |
| Observability / audit / metrics / trace | 已覆盖 | `MemoryRuntimeDependencies`、`memory/src/observability/MemoryObservability.*`、`MemoryObservabilityBridgeTest`；manager / context / writeback / maintenance emit 已接 live logger / metric / trace / audit | installed / qemu / soak 证据仍需独立补齐 |
| Runtime / app 链路 | 基本打通 | `RuntimeLiveDependencyComposition` 创建 memory manager；`AgentOrchestrator` 多路径调用 prepare/write_back；SSOT 有 L4 writeback 证据 | L5 qemu / release runner / soak 未证明；installed multi-turn context reuse 证据可加强 |
| ADR 边界 | 基本正确 | memory 拥有 ContextOrchestrator；runtime 拥有 RecoveryManager / AgentOrchestrator；LLM 拥有 PromptComposer | 需要自动化 boundary guard 防止未来回归 |

### 6.4 关联模块链路判断

| 链路 | 是否打通 | 当前可信层级 | 判断 |
|---|---|---:|---|
| Runtime -> Memory `prepare_context` | 已打通 | L2 / L4 partial | runtime production direct path 和 live cognition path 均有调用点；installed L4 已证明至少一次 memory DB writeback，但不证明所有上下文分支 |
| Runtime -> Memory `write_back` | 已打通 | L2 / L4 partial | LLM response、belief hint、resume / context reload 相关路径存在；installed local DB rows 提供 L4 writeback 证据 |
| apps runtime_support -> Memory | 已打通 | L0 / L4 partial | live dependency composition 创建 SQLite-backed manager，并按 installed layout 提供 DB path / migration dir |
| Memory -> LLM | 间接打通 | L2 | Memory 产出 `ContextPacket`；LLM / PromptComposer 消费 runtime 已归一化 prompt 输入，Memory 不拼 prompt，符合 ADR-006 |
| Cognition -> Runtime -> Memory | 基本打通 | L2 | cognition 产 `BeliefUpdateHint`，Runtime best-effort 写回 Memory；cognition 不直写 Memory |
| Knowledge -> Runtime / Memory | 部分打通 | L2 partial | knowledge evidence 可经 runtime / context request / installed package evidence 进入链路；Memory 不拥有 Knowledge indexing owner |
| Tools -> Runtime -> Memory | 间接打通 | L2 partial | tools observation / result 由 runtime 主链决定是否写回；tools 不直写 Memory，边界正确 |
| Recovery -> Memory | 间接打通 | L3 partial | FULLINT-011 / recovery causality 证据支持恢复链路可沉淀写回；恢复准入仍归 runtime/RecoveryManager |
| Profiles -> MemoryConfig | 基本打通 | L2 | profile snapshot 可投影 storage / context / vector / maintenance 配置；vector backend 投影与实际实现不匹配 |
| Infra observability -> Memory | 已打通 | L2 | Memory factory 现可注入 logger / audit / metrics / tracing provider，runtime_support live composition 已在 `create_memory_manager()` 前组合 observability bundle；`MemoryObservabilityBridgeTest` 与 `RuntimeProductionHealthCompositionTest` 已通过 |
| Packaging / installed -> Memory | 部分打通 | L4 local | installed memory DB / WAL / rows 有本机证据；qemu / release runner / soak 仍待补 |

### 6.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| MEM-GAP-001 | High | `sqlite-vss` production 接线与本机 installed-package authoritative 能力证据已闭合；release-runner / qemu 隔离证据另归 MEM-FIX-006 | `memory/src/vector/SqliteVssVectorBackend.*` 已改为 loadable extension driver，`MemoryManagerFactory` 已补 sqlite writer pre-open lifecycle，`SqliteVssVectorBackend::search()` 已覆盖 fresh empty real index no-op；`MemoryContextAssembleIntegrationTest` 已覆盖安装态首轮 context warnings 不再出现 `vector_query_unavailable`；2026-05-18 当前树 Debian packages 通过本机 `pkg_smoke_install.sh --explicit-start-check`，安装态资产 `/usr/lib/dasall/sqlite-vss/{vector0.so,vss0.so}`、`/usr/share/dasall/sql/memory/V002__vector_sidecar.sql`、WAL、turn / summary writeback、`memory_vector_documents` sidecar 与 sqlite-vss real search hit 均有证据 | 本机 L4 installed-package 能力证据可作为 MEM-FIX-001 Done；不能由此宣称 release-runner / qemu / soak ready | 后续只在 MEM-FIX-006 / release gate 中补 qemu、multi-turn context reuse 与 soak；不要再把 qemu guest-side evidence 作为 MEM-FIX-001 能力 blocker |
| MEM-GAP-002 | 已闭合 | SQLite 版本基线与运行时 version gate 已对齐 | 2026-05-18 已将内置 SQLite pin 升至 `sqlite-autoconf-3510300`（SQLite 3.51.3），新增 `StorageConfig.sqlite_min_version` / `encode_sqlite_version_number()`，在 `SqliteMemoryStore::open()` 增加 `sqlite3_libversion_number()` fail-closed gate，并补齐 `SqliteVersionGateTest`、`SqliteMemoryStoreTest`、`MemoryFailureInjectionTest`；详设已补运行时编码 3051003 口径 | WAL / 并发基线不再依赖口头假设，低版本或错误替换库会稳定 fail-closed | 2026-05-18 由 `MEM-FIX-002` 完成；后续只保留 reader pool 并发防护与 observability 等其他缺口 |
| MEM-GAP-003 | 已闭合 | Observability / audit / metrics / trace 已接 infra production sinks | 2026-05-18 已新增 `MemoryRuntimeDependencies` public seam 与 module-local `MemoryObservability` bridge；`MemoryManager` / `ContextOrchestrator` / `WritebackCoordinator` / `MemoryMaintenanceWorker` 已发出 context / writeback / conflict / compression / maintenance success/degraded/failure 事件；`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已先组合 live observability bundle 再创建 memory manager | production composition 不再停留在 error metadata / warnings / report 字段，Memory 语义事件现可落 concrete log / metrics / trace / audit provider | 2026-05-18 由 `MEM-FIX-003` 完成；后续只保留 installed / qemu / soak 与并发 / 边界类缺口 |
| MEM-GAP-004 | Medium | release runner / qemu / soak 证据缺失 | 既有 SSOT 只记录 installed local L4；qemu / release runner / soak 仍未证明 | 不能宣称 Memory release-runner ready 或长期 production-ready；但不阻塞 MEM-FIX-001 用本机 installed-package gate 收敛 DASALL 能力证据 | 增加 release-runner qemu gate、WAL / migration / writeback / context reuse 证据；本机安装态能力证据归 MEM-FIX-001，qemu 隔离证据归 MEM-FIX-006 / release gate |
| MEM-GAP-005 | 已闭合 | SQLite reader pool 并发防护已显式闭合 | 2026-05-18 已将 `SqliteMemoryStore::select_reader_connection() const` 收口为 atomic round-robin + per-slot lease mutex，`load_session_bundle` / `load_latest_summary` / `query_facts` / `query_experiences` / `count_turns` 在整个查询期持有 reader lease；新增 `SqliteMemoryStoreConcurrencyTest` 与 `MemoryContextIntegrationTest` 覆盖 store 与 `prepare_context()` 并发读取 | 多线程 `prepare_context()` 与并发查询不再裸共享 `sqlite3*`，WAL reader pool 可信度与详设口径一致 | 2026-05-18 由 `MEM-FIX-004` 完成；TSAN gate 保持可选增强 |
| MEM-GAP-006 | Low / Medium | Memory 边界回归自动化不足 | 手工检查未发现 Memory 侵入 PromptComposer / RecoveryManager / AgentOrchestrator owner，但缺专门 boundary test/script | 后续改动可能破坏 ADR-006/007/008，尤其是 context / recovery / prompt owner 边界 | 增加 `MemoryBoundaryGuardComplianceTest` 或脚本，锁定 include/link 和关键 symbol 禁区 |
| MEM-GAP-007 | Low / Medium | installed 多轮 context reuse / maintenance 正向证据可加强 | 当前 L4 证据主要证明 DB 写回、WAL 和 rows；未证明 installed 环境跨多轮 context reuse、retention / checkpoint / quarantine cleanup 的正向路径 | installed 证据对“写入后被下一轮上下文消费”和“维护任务生产运行”覆盖不足 | 增加 installed multi-turn smoke 与 maintenance smoke，记录 ContextPacket 命中、summary reuse、checkpoint / retention 结果 |

### 6.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| MEM-FIX-001 | Done | 收口 sqlite-vss production 接线与本机安装态证据 | 已完成真实 sqlite-vss loadable extension / third_party 依赖接入、`MemoryManagerFactory` production wiring、sqlite writer pre-open lifecycle、fresh empty real index search no-op、`SimpleLocalEmbeddingAdapter`、packaging asset / install layout、`V002__vector_sidecar.sql` migration 与 profile fail-closed 策略；2026-05-18 final qemu attempt 证明阻塞在 WSL2 host loopback / autopkgtest 启动链，不指向 DASALL memory / sqlite-vss 功能失败；本任务以本机实际 installed-package gate 作为 authoritative 能力证据 | 已通过 `SimpleLocalEmbeddingAdapterTest`、`VectorMemoryAdapterTest`、`SqliteVssVectorBackendTest`、`SchemaMigrationTest`、`MemoryWritebackIntegrationTest`、`MemoryProfileCompatibilityTest`、`MemoryContextAssembleIntegrationTest`、`GatewayRuntimeLiveDependencyCompositionTest`、`RuntimeProductionHealthCompositionTest`；`dpkg-buildpackage -us -uc -b` 生成 2026-05-18 12:20 Debian packages；`pkg_smoke_install.sh --explicit-start-check` 通过；安装态 DB 证明 `journal_mode=wal`、turn / summary 含 `llm.origin=deepseek-prod/`、`memory_vector_documents` 有 smoke 写回行，sqlite-vss virtual table real search hit 返回 `cli-run-llm-response` | `RunCtest_CMakeTools(tests=["SimpleLocalEmbeddingAdapterTest","VectorMemoryAdapterTest","SqliteVssVectorBackendTest","SchemaMigrationTest","MemoryWritebackIntegrationTest","MemoryProfileCompatibilityTest","MemoryContextAssembleIntegrationTest","GatewayRuntimeLiveDependencyCompositionTest","RuntimeProductionHealthCompositionTest"])`；`dpkg-buildpackage -us -uc -b`；`DASALL_DEEPSEEK_API_KEY_FILE="$HOME/.local/share/dasall/secrets/deepseek-prod.secret" bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`；Python sqlite3 查询 `/var/lib/dasall/memory/memory.db` 与 load_extension `/usr/lib/dasall/sqlite-vss/{vector0.so,vss0.so}` 验证 sidecar / search hit | MEM-FIX-001 以本机 installed-package authoritative Done 收敛；qemu guest-side 证据不再阻塞本任务，后续只归 MEM-FIX-006 / release-runner gate |
| MEM-FIX-002 | Done | 对齐 SQLite 最低版本与运行时 gate | 已升级 `memory/CMakeLists.txt` 到 `sqlite-autoconf-3510300`（SQLite 3.51.3），新增 `StorageConfig.sqlite_min_version` / `encode_sqlite_version_number()`，在 `SqliteMemoryStore::open()` 增加 `sqlite3_libversion_number()` fail-closed gate，并把 pin 注入 `dasall_sqlite3` 编译命令以确保版本升级触发重编译 | 新增 `SqliteVersionGateTest` 覆盖默认值、backport floor 与 fail-closed；扩展 `SqliteMemoryStoreTest`、`MemoryFailureInjectionTest` 覆盖 open/init 负路径 | `RunCtest_CMakeTools(tests=["SqliteVersionGateTest","SqliteMemoryStoreTest","MemoryFailureInjectionTest"])` 当前会返回泛化 `生成失败`，已回退 `cmake --build build-ci --target dasall_memory_sqlite_version_gate_unit_test dasall_memory_sqlite_store_unit_test dasall_memory_failure_injection_integration_test`、`ctest --test-dir build-ci -R '^(SqliteVersionGateTest|SqliteMemoryStoreTest|MemoryFailureInjectionTest)$' --output-on-failure`，并用 `gdb -batch -ex "break main" -ex "run" -ex "print (int)sqlite3_libversion_number()" -ex "quit" build-ci/tests/unit/memory/dasall_memory_sqlite_version_gate_unit_test` 复核运行时版本 | 运行时 `sqlite3_libversion_number()` 为 3051003，低版本 gate fail-closed，高版本 WAL/store/init tests 通过，文档中的 3.51.3 / backport 口径与代码一致 |
| MEM-FIX-003 | Done | 接入 Memory production observability sinks | 已新增 public `MemoryRuntimeDependencies` 窄注入面与 module-local `memory/src/observability/MemoryObservability.*` bridge；`MemoryManagerFactory` / `MemoryManager` / `ContextOrchestrator` / `WritebackCoordinator` / `MemoryMaintenanceWorker` 均已接入 logger / metrics / trace / audit emit；`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 现会先组合 live observability bundle 再创建 memory manager | 已新增 `MemoryObservabilityBridgeTest`，并与 `MemoryWritebackIntegrationTest`、`MemoryMaintenanceIntegrationTest`、`RuntimeProductionHealthCompositionTest` 一起证明 bridge emit 与 real composition wiring | `cmake --build build-ci --target dasall_memory_observability_bridge_integration_test dasall_memory_writeback_integration_test dasall_memory_maintenance_integration_test -j4 && ctest --test-dir build-ci --output-on-failure -R '^(MemoryObservabilityBridgeTest|MemoryWritebackIntegrationTest|MemoryMaintenanceIntegrationTest)$' && cmake --build build-ci --target dasall_access_runtime_production_health_composition_integration_test -j4 && ctest --test-dir build-ci --output-on-failure -R '^RuntimeProductionHealthCompositionTest$'` | production-composed Memory path 现可发出 context assembly、writeback、conflict、compression、maintenance 的 success/failure/degraded 事件，且 runtime_support 不再把 Memory 落到 no-op sink |

#### MEM-FIX-003 完成证据（2026-05-18）

1. `memory/include/MemoryDependencies.h` 已新增 `MemoryRuntimeDependencies` 作为 runtime-facing 窄注入面；`memory/src/observability/MemoryObservability.*` 在 module-local bridge 中统一把 Memory 事件适配到 live logger / audit / metrics / trace provider，避免 public ABI 直接暴露 infra concrete 类型。
2. `memory/src/MemoryManager.cpp`、`memory/src/context/ContextOrchestrator.cpp`、`memory/src/writeback/WritebackCoordinator.cpp`、`memory/src/maintenance/MemoryMaintenanceWorker.cpp` 现已覆盖前门失败、context assemble / degraded、compression、writeback / conflict、maintenance success / degraded / failure emit；`tests/integration/memory/MemoryObservabilityBridgeTest.cpp` 已证明这些事件会落到 concrete provider。
3. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 现已先组合 live observability bundle，再调用 `create_memory_manager()` 透传 logger / audit / metrics / tracing provider；2026-05-18 已通过 `cmake --build build-ci --target dasall_memory_observability_bridge_integration_test dasall_memory_writeback_integration_test dasall_memory_maintenance_integration_test -j4`、`ctest --test-dir build-ci --output-on-failure -R '^(MemoryObservabilityBridgeTest|MemoryWritebackIntegrationTest|MemoryMaintenanceIntegrationTest)$'`、`cmake --build build-ci --target dasall_access_runtime_production_health_composition_integration_test -j4` 与 `ctest --test-dir build-ci --output-on-failure -R '^RuntimeProductionHealthCompositionTest$'`，且未使用 qemu / kvm 证据。
| MEM-FIX-004 | Done | 补齐 SQLite reader pool 并发防护 | 已将 `SqliteMemoryStore` reader path 收口为 atomic round-robin + per-slot lease；`load_session_bundle` / `load_latest_summary` / `query_facts` / `query_experiences` / `count_turns` 改为在整个查询期间持有 lease，避免裸连接跨线程复用 | 已新增 `SqliteMemoryStoreConcurrencyTest` 与 `MemoryContextIntegrationTest`；TSAN 保持可选 gate | `cmake -S . -B build-ci && cmake --build build-ci --target dasall_memory_sqlite_store_concurrency_unit_test dasall_memory_context_integration_test -j4 && ctest --test-dir build-ci --output-on-failure -R '^(SqliteMemoryStoreConcurrencyTest|MemoryContextIntegrationTest)$'` | 并发 `load_session_bundle` / fact / experience / summary 查询与 `prepare_context()` 在本机 `build-ci` 上稳定通过，无共享连接竞争导致的 flaky failure |
| MEM-FIX-005 | Todo | 增加 Memory 边界回归防线 | 新增 `tests/contract/memory/MemoryBoundaryGuardComplianceTest.cpp` 或 boundary script，检查 `memory/` 不 include / link LLM PromptComposer、Tools executor、Runtime RecoveryManager / AgentOrchestrator 私有实现 | `MemoryBoundaryGuardComplianceTest`、`ContextPacketFieldContractTest` | `RunCtest_CMakeTools(tests=["MemoryBoundaryGuardComplianceTest","ContextPacketFieldContractTest"])` | ADR-006/007/008 关键边界有自动化证据，后续改动不能悄悄越界 |
| MEM-FIX-006 | Todo | 收口 release-runner / qemu memory evidence | 更新 release runner / qemu 脚本与证据文档，覆盖安装包在隔离 testbed 中的 `dasall run` 写回、WAL、migration、context reuse、summary reuse | qemu / autopkgtest gate、BusinessChain matrix 回写 | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>`；另引用 MEM-FIX-001 的本机 installed memory DB 查询证据作为前置能力证据 | BC-09 / BC-10 / BC-16 中 Memory 证据可从本机 installed-package Done 补强到 release-runner / qemu ready；该任务不再阻塞 MEM-FIX-001 |
| MEM-FIX-007 | Todo | 增加 installed multi-turn context / maintenance 正向证据 | 不一定改产品代码；可增加 installed smoke fixture、maintenance CLI / diag hook 或 test helper，记录多轮后 ContextPacket 命中与 maintenance report | `MemoryInstalledMultiTurnSmokeTest`、`MemoryMaintenanceInstalledSmokeTest` 或等价 package smoke | `RunCtest_CMakeTools(tests=["MemoryContextIntegrationTest","MemoryMaintenanceIntegrationTest"])`；installed 命令在任务 deliverable 中固定 | 能证明写入内容被下一轮 context 使用，并证明 checkpoint / retention / quarantine cleanup 在安装态可观测 |

#### MEM-FIX-004 完成证据（2026-05-18）

1. `memory/src/store/sqlite/SqliteMemoryStore.h/.cpp` 已把 reader pool 从“裸指针轮询”收口为 atomic round-robin 选 slot + per-slot lease mutex，并让 `load_session_bundle`、`load_latest_summary`、`query_facts`、`query_experiences`、`count_turns` 在整个查询期持有 reader lease，避免同一 `sqlite3*` 被并发请求直接共享。
2. `tests/unit/memory/SqliteMemoryStoreConcurrencyTest.cpp` 已显式证明单 reader pool 下第二个借用者会等待第一个 lease 释放，并补齐公开只读 API 的并发稳定性覆盖；`tests/unit/memory/CMakeLists.txt` 已注册 `SqliteMemoryStoreConcurrencyTest`。
3. `tests/integration/memory/MemoryContextIntegrationTest.cpp` 已新增 manager 级并发门，证明 `prepare_context()` 在 `reader_pool_size=1` 的 build-tree 配置下可被多线程稳定调用；2026-05-18 已通过 `cmake -S . -B build-ci`、`cmake --build build-ci --target dasall_memory_sqlite_store_concurrency_unit_test dasall_memory_context_integration_test -j4` 与 `ctest --test-dir build-ci --output-on-failure -R '^(SqliteMemoryStoreConcurrencyTest|MemoryContextIntegrationTest)$'`，且未使用 qemu / kvm 证据。

#### MEM-FIX-001 host qemu 启动链补记（2026-05-17）

1. 本轮对 host 上的 `autopkgtest-virt-qemu` 启动链做了单独探针，解释了为什么早前日志只停在 `autopkgtest` 启动头：当只看到 `autopkgtest [..]: version` / `host ... command line`，但没有 `find_free_port`、`qemu-img info`、`full qemu command-line`、guest boot 或 testbed capabilities 时，qemu/testbed 尚未进入稳定可观测阶段。
2. 当前 WSL2 host 的第一层 trap 是 loopback route：`ip route get 127.0.0.1` 命中 `via ... dev loopback0 table 127`，未监听的 `127.0.0.1:10022` 会停在 `SYN-SENT`，导致 Ubuntu 24.04 `autopkgtest-virt-qemu` 5.47 卡在无 timeout 的 `find_free_port: trying 10022`。临时 `sudo ip rule add pref 0 to 127.0.0.0/8 lookup local` 可让空端口快速 `ECONNREFUSED`；该规则已写入 `scripts/packaging/README.md` 作为可固化建议。
3. 第二层 trap 是 KVM 假阳性：`/dev/kvm` 可写不等于 QEMU `-enable-kvm` 可用；当前 host 上 QEMU 报 `Could not access KVM kernel module: No such device`。`AUTOPKGTEST_QEMU_DISABLE_KVM=1` 对该版本 `autopkgtest-virt-qemu` 不足以去掉自动追加的 `-enable-kvm`，后续应使用 `--qemu-command=<no-kvm-wrapper>` 或在脚本中增加真实 KVM probe。
4. 临时端口规避 + no-KVM qemu wrapper 下，最小 autopkgtest probe 已进入 Ubuntu 24.04 guest，拿到 testbed capabilities，并在 guest 中输出 `DASALL_AUTOPKGTEST_QEMU_PROBE_OK` / `smoke PASS`。这只证明 host `autopkgtest -> autopkgtest-virt-qemu -> qemu -> testbed` 启动链可达，不把 `MEM-FIX-001` 升级为 Done。
5. 因此 qemu 链路的下一次正式推进应归入 `MEM-FIX-006` / release-runner gate：先固化 loopback route rule 与 no-KVM wrapper / KVM real probe，再执行 `validate_gate_int_10_installed_package_qemu.sh`，最后回写隔离 testbed 中的 asset、migration、profile fail-closed 与 real search hit evidence。`MEM-FIX-001` 本身改走本机实际 installed-package gate。

#### MEM-FIX-001 final qemu attempt 补记（2026-05-18）

1. 本轮按“最后尝试一次 qemu”的约束执行 `scripts/packaging/run_local_qemu_gate.sh`，使用稳定 qemu image、稳定 DeepSeek secret、DNS repair setup script 与 no-KVM qemu wrapper；artifact 根路径为 `/tmp/dasall-mem-fix-001-final-20260518-105155.log` 与 `$HOME/.cache/dasall/qemu/autopkgtest-output/mem-fix-001-final-20260518-105155`。
2. final run 已通过 build-tree `dasall_gate_int_10` 与 `dasall_packaging_preflight_tests`，并完成 `dpkg-buildpackage`。构包安装阶段真实带出 `/usr/lib/dasall/sqlite-vss/vector0.so`、`/usr/lib/dasall/sqlite-vss/vss0.so`、`/usr/share/dasall/sql/memory/V002__vector_sidecar.sql`，且生成 `dasall`、`dasall-cli`、`dasall-daemon`、`dasall-common` Debian artifacts。
3. qemu 前置也已到达正式 autopkgtest 启动点：`debian/tests/control` metadata validated；命令行包含 `--setup-commands $HOME/.local/share/dasall/qemu/setup-commands.sh`、`--output-dir .../mem-fix-001-final-20260518-105155`、DeepSeek secret copy / env 注入，以及 `--qemu-command=$HOME/.cache/dasall/qemu/qemu-system-x86_64-no-kvm`。
4. 该 final run 未进入 guest。`autopkgtest` output log 只写出启动头，summary 为空；进程侧证据显示 `autopkgtest-virt-qemu` 未派生 `qemu-system-x86_64` 子进程，fd 对应 `/proc/net/tcp` 中 `127.0.0.1 -> 127.0.0.1:10026` 的 `SYN-SENT`，即仍卡在 Ubuntu 24.04 `autopkgtest-virt-qemu` 的 host-side `find_free_port()` / WSL2 loopback route trap。
5. 本轮已按长阻塞处理并终止进程组；该结论不指向 DASALL memory / sqlite-vss 功能失败，也不提供 guest-side PASS / FAIL。2026-05-18 已明确改用本机实际 installed-package gate 作为 `MEM-FIX-001` 的 authoritative 能力证据，qemu 专属要求从本任务移出；后续不应继续在 `MEM-FIX-001` 内追加 qemu workaround，除非目标切换为 `MEM-FIX-006` / release-runner gate。

### 6.7 非缺口但需守住的边界

以下事项当前不是缺口，后续修改时应继续守住：

1. Memory 拥有 ContextOrchestrator 和 `ContextPacket` assembly，不拥有 PromptComposer、PromptRegistry、provider payload 或 prompt rendering。
2. Memory 不裁定 retry / replan / abort；恢复准入和执行仍归 Runtime / RecoveryManager，Memory 只沉淀事实、经验和恢复结果证据。
3. Memory 不成为第二个 AgentOrchestrator；调用时机、任务主循环和全局状态机仍由 Runtime owner 决定。
4. Cognition / Tools / LLM / Access 不应绕过 Runtime 直接写 Memory；跨模块写回应经 Runtime 或明确的 facade seam。
5. Memory supporting types 保持在 `memory/include`，不为方便跨模块调用而随意推入 `contracts`。
6. WorkingMemory 是进程内短时板，持久 checkpoint / resume 的 owner 边界必须继续与 Runtime checkpoint 语义区分。
7. Vector backend 未完成时，不应把 unavailable fallback 伪装成语义向量召回能力。

### 6.8 建议复验命令

本节命令用于后续把本章结论升级或关闭缺口。若使用 VS Code CMake Tools，应优先选择 `ListTests_CMakeTools` / `RunCtest_CMakeTools` 执行；以下 shell 形式仅作为文档化验收口径。

本轮已通过的 Memory 聚焦测试等价命令口径：

```bash
ctest --test-dir build/vscode-linux-ninja --output-on-failure -R "Memory|TurnSessionSummaryMemoryContractTest|MemoryFactExperienceContractTest|ContextPacketFieldContractTest"
```

vector / SQLite / observability 缺口复核：

```bash
rg -n "SqliteVssVectorBackend|UnavailableVectorMemoryIndexAdapter|VectorMemoryIndexAdapter|DASALL_SQLITE_AUTOCONF_VERSION|sqlite3_libversion|sqlite_min_version|MemoryObservability|metrics|trace|audit" \
  memory/include \
  memory/src \
  memory/CMakeLists.txt \
  docs/todos/memory/DASALL_memory子系统专项TODO.md
```

Runtime / app 链路复核：

```bash
rg -n "create_memory_manager|memory_manager|prepare_context|write_back|export_working_memory_snapshot|run_maintenance|memory.db|journal_mode=wal|turns_total|summaries_total" \
  apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp \
  runtime/src/AgentOrchestrator.cpp \
  docs/worklog/DASALL_开发执行记录.md \
  docs/ssot/BusinessChainIntegrationMatrix.md
```

边界回归口径：

```bash
rg -n "PromptComposer|PromptRegistry|ToolManager|RecoveryManager|AgentOrchestrator|apps/|llm/src|tools/src|runtime/src" memory/include memory/src
```

release / qemu gate 口径：

```bash
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>
```

### 6.9 当前章节结论

Memory 子系统的实现覆盖度已经很高，当前主要问题不是“缺少 Memory 模块本体”，而是“release-runner 证据、边界回归自动化和 installed 多轮 context / maintenance 正向证据”尚未全部闭合。

因此本章冻结口径为：

1. Memory 主体：基本完成，L1 / L2 证据充分；本轮 `MEM-FIX-003` focused validation 已证明 production observability sink 与 runtime_support composition wiring。
2. Runtime / app 链路：已接入 `prepare_context()` / `write_back()`；installed writeback 有 L4 local 证据，但不外推为 L5 qemu / release runner 或 L6 soak。
3. VectorMemory / sqlite-vss：本机 installed-package authoritative 证据已闭合，但不外推为 L5 qemu / release runner 或 L6 soak。
4. SQLite / observability / concurrency：版本 gate、production sink 与 reader pool 借用防护均已收口；后续只保留 TSAN 这类可选增强。
5. 查漏补缺优先级：转向 `MEM-GAP-004` / `MEM-GAP-006` / `MEM-GAP-007`，继续提升 release、边界与 installed 多轮可信度。

## 7. Knowledge 子系统查漏补缺

### 7.1 检查范围与依据

本轮检查目标：核对 `knowledge/*` 是否覆盖 `docs/architecture/DASALL_knowledge子系统详细设计.md` 中规定的检索、索引、ingest、freshness、配置、观测、并发和跨模块链路，并判断 Runtime / Memory / Profiles / apps / installed asset 链路是否已经全部闭合。

检查范围：

1. Knowledge 源码与公共接口：`knowledge/include/*`、`knowledge/src/*`、`knowledge/CMakeLists.txt`。
2. Knowledge 单元与集成测试：`tests/unit/knowledge/*`、`tests/integration/knowledge/*`、`tests/integration/full_business_chain/*Knowledge*`。
3. Runtime / Memory / apps 接入链路：`runtime/src/AgentOrchestrator.cpp`、`runtime/include/RuntimeDependencySet.h`、`memory/include/context/MemoryContextRequest.h`、`memory/src/context/ContextOrchestrator.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`。
4. 证据边界：`docs/todos/knowledge/*`、`docs/todos/memory/DASALL_memory子系统专项TODO.md`、`docs/ssot/*`、`docs/worklog/DASALL_开发执行记录.md`。

本轮执行验证：使用 VS Code CMake Tools 在既有 `build/vscode-linux-ninja` 构建目录执行 Knowledge 聚焦 CTest。通过的测试包括 `KnowledgeProfileCompatibilityTest`、`RuntimeKnowledgeEvidenceIntegrationTest`、`KnowledgeRefreshLoopTest`、`RetrievalQualityRegressionTest`、`dasall_knowledge_retrieval_smoke_integration_test`、`KnowledgeHealthProbeTest`、`KnowledgeServiceFacadeRealRefreshTest`、`KnowledgeTelemetryTest`、`RecallCoordinatorDenseBridgeTest`。失败测试为 `KnowledgeInstalledAssetProbeIntegrationTest`，失败信息为 `installed asset knowledge factory failed: installed asset knowledge service options are inconsistent`。

证据边界说明：本轮没有重新执行真实 installed package、qemu、release runner 或 soak。`KnowledgeInstalledAssetProbeIntegrationTest` 当前失败说明 build-tree installed asset gate 自身也未绿；因此本章不把 apps live composition 的 source evidence 外推为 L4 / L5。

### 7.2 总体结论

结论：`knowledge/*` 已经不是 placeholder。当前代码已经具备 `IKnowledgeService` facade、`KnowledgeTypes` 支撑类型、Query Plane、Index Plane、SQLite FTS5 lexical index、source scan / canonicalize / chunk / ingest、routing、sparse recall、rerank、evidence assembly、freshness/health、profile projection、installed asset factory 和 runtime-facing evidence projection。

但当前不能判定为“详设所有功能全部完成、所有关联模块链路全部 production-ready”。更准确的状态是：

1. Knowledge 模块内部 lexical retrieval / ingest / refresh 主体实现达到 L1 / L2 可信度，本轮 9 个聚焦测试通过。
2. Runtime `AgentOrchestrator` 已能调用 `IKnowledgeService::retrieve()`，并把 `EvidenceBundle.context_projection` 与 `retrieval_evidence_refs` 传给 Memory `ContextOrchestrator`；读链路在 build-tree integration 中成立。
3. SQLite FTS5 index writer/reader、manifest sidecar、checksum、active snapshot swap、rollback/LKG 基线真实存在，但 `VersionLedger` 与 `CorpusCatalog` 主要是内存状态，未形成详设要求的完整持久 ledger/catalog/migration/startup recovery。
4. Hybrid / dense 检索只完成端口、bridge、degrade 语义和 dense bridge focused tests；production concrete vector backend、embedding/upsert/index lifecycle 没有闭合。
5. `request_refresh()` 当前是同步执行的 facade 调用，不符合详设中异步 refresh / in-flight 观测口径；`init()` 也不触发首启 active snapshot build。
6. installed asset factory 已接入 apps live composition，但 probe 测试当前失败；且 factory 实际只扫描 profiles 与 LLM provider YAML，不覆盖详设首批 architecture / ADR / SSOT / profile policy normative corpus baseline。
7. telemetry bridge 有测试，但 production sink 注入不足；retrieve event 还存在 `profile_id` 必填而 facade 未填的有效载荷缺口。

### 7.3 设计覆盖矩阵

| 设计域 | 当前覆盖判断 | 主要证据 | 缺口 / 边界 |
|---|---|---|---|
| Public facade / supporting types | 已覆盖 | `IKnowledgeService` 暴露 `init`、`retrieve`、`health_snapshot`、`request_refresh`；`KnowledgeTypes` 覆盖 query、evidence、config、health、refresh、corpus descriptor、`RetrievalEvidenceRef` projection | 支撑类型保持 knowledge module-local / module-public，不应为便利跨模块调用而上升 contracts |
| Config / profile projection | 基本覆盖 | `KnowledgeConfigProjector` 消费真实 `RuntimePolicySnapshot` 与 `BuildProfileManifest`，投影 knowledge/vector 开关、deadline、budget、lane timeout、profile mode | 投影出 lane timeout / parallel recall 不代表 coordinator 已执行该策略 |
| Query normalize / corpus routing | 已覆盖 | `QueryNormalizer`、`CorpusRouter` 已实现 trusted corpus、allowed corpora、domain tags、authority、freshness、lexical/hybrid/dense mode fallback | installed factory 提供的 corpus baseline 太窄，限制了路由真实覆盖面 |
| Sparse lexical retrieve | 已覆盖 | `SparseRetriever` 构造 FTS 表达式、filter corpus/tags/language/authority、生成 snippet/window/citation；integration retrieval smoke 通过 | 当前可证明的是 lexical / FTS5 主链，不等于 semantic vector recall |
| Rerank / EvidenceAssembler | 基本覆盖 | `Reranker`、`EvidenceAssembler` 可输出 `EvidenceBundle`、context projection、warnings/degraded | evidence quality gate 有 `RetrievalQualityRegressionTest`，但 production installed gate 未绿 |
| Runtime -> Memory evidence handoff | 基本打通 | `RuntimeKnowledgeEvidenceIntegrationTest` 通过；Runtime 将 context projection / refs 写入 `MemoryContextRequest`，Memory 投影到 `ContextPacket` | 这是 L2 build-tree 读链路，不证明 installed/qemu/release runner |
| Ingest / scanner / canonicalizer / chunker | 基本覆盖 | `SourceScanner` 文件扫描 / trust / diff / quarantine，`Canonicalizer` Markdown/YAML 规范化，`Chunker` 稳定 chunk/citation，`IngestionCoordinator` 组 batch | vector embedding/upsert 未接入；首启 build 与 Runtime write-side trigger 未证明 |
| Index writer / reader | 基本覆盖 | `IndexWriter` 写 SQLite FTS5 shadow DB、manifest sidecar、checksum、active snapshot swap；`IndexReader` atomic active snapshot 读 | ledger/catalog 持久化、format migration、startup recovery 不完整 |
| Refresh lifecycle | 部分覆盖 | `KnowledgeRefreshLoopTest`、`KnowledgeServiceFacadeRealRefreshTest` 通过；`refresh_in_flight_` busy guard 存在 | `request_refresh()` 当前同步执行；`init()` 不自动首建 active snapshot；Runtime 写侧触发链路未证明 |
| Vector / hybrid retrieval | 部分覆盖 | `VectorRetrieverBridge`、`IQueryEncoder`、`IVectorRecallStore`、`RecallCoordinatorDenseBridgeTest` 存在；dense failure/degrade 语义可测 | no concrete production backend；installed factory `vector_enabled=false`；Memory sqlite-vss internal seam 已有，但真实 extension / package / production recall store 未闭合 |
| Deadline / concurrency / backpressure | 部分覆盖 | Facade 有 coarse `ensure_deadline_not_expired()` 与 `compute_stage_budget()`；config 有 `max_parallel_recall` 和 lane timeout | `RecallCoordinator` 串行执行 sparse/dense lane，未执行 lane timeout、parallel recall、cancel/late result discard |
| Health / freshness | 基本覆盖 | `FreshnessController`、`KnowledgeHealthProbe`、`KnowledgeHealthProbeTest` 存在；health snapshot 暴露 freshness、reason codes、vector backend 状态 | installed probe 当前失败，生产状态证据不足 |
| Telemetry / audit / metrics / trace | 部分覆盖 | `KnowledgeTelemetry` 支持 log/metrics/trace/audit/fallback sink、invalid payload / sink failure 计数；`KnowledgeTelemetryTest` 通过 | production factory 使用空 sink；retrieve event 缺 `profile_id` 会被 `has_required_fields(Retrieve)` 判 invalid payload |
| Installed asset factory | 部分覆盖 | apps runtime support 调用 `create_installed_asset_knowledge_service` 并传 `service_instance_id`；factory 创建 lexical-only service | `KnowledgeInstalledAssetProbeIntegrationTest` 当前失败；factory corpus 只含 profiles / LLM providers；无自动 refresh |
| ADR 边界 | 基本正确 | Knowledge 不生成 `ContextPacket` / Prompt，不裁定 Recovery，不拥有 AgentOrchestrator；CMake link 仅 public `contracts`、private `profiles` / sqlite | 仍需 boundary guard 防止未来引入 llm/cognition/apps/runtime 私有依赖 |

### 7.4 关联模块链路判断

| 链路 | 是否打通 | 当前可信层级 | 判断 |
|---|---|---:|---|
| Runtime -> Knowledge retrieve | 已打通 | L2 | `AgentOrchestrator` 调用 `knowledge_service->retrieve()`；`RuntimeKnowledgeEvidenceIntegrationTest` 通过 |
| Knowledge -> Runtime -> Memory ContextPacket | 已打通 | L2 | context projection 和 `retrieval_evidence_refs` 经 `MemoryContextRequest` 进入 `ContextPacket`；Memory 不拥有 indexing owner |
| apps runtime_support -> Knowledge factory | 部分打通 | L0 / L2 red | production composition 传入 `service_instance_id` 并可注入 dependency set；但 `KnowledgeInstalledAssetProbeIntegrationTest` 当前失败，不能外推 installed OK |
| Profiles -> KnowledgeConfig | 基本打通 | L2 | `KnowledgeProfileCompatibilityTest` 走真实 profiles catalog/resolver/provider/projector 并通过 |
| Knowledge -> SQLite FTS5 | 基本打通 | L1 / L2 | writer/reader/refresh/retrieval smoke 均有通过测试；lexical DB 主链成立 |
| Knowledge -> Memory vector backend | 未闭合 | L0 / L1 partial | Knowledge dense bridge 接口存在，Memory concrete sqlite-vss backend未实现，installed factory vector disabled |
| Runtime / write-side -> Knowledge refresh trigger | 未证明 | L0 partial | 未见 runtime 非测试路径触发 `request_refresh()`；factory/init 也不自动首建 |
| Knowledge -> Infra observability | 部分打通 | L1 | telemetry bridge focused test 通过；production sink 注入和 retrieve payload completeness 未闭合 |
| Packaging / installed -> Knowledge | 证据不足 | L2 red / L4 missing | build-tree installed asset probe 当前失败；未执行真实 package/qemu/release runner |

### 7.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| KNO-GAP-001 | High | `request_refresh()` 与详设异步 refresh 语义不一致 | `KnowledgeServiceFacade::request_refresh()` 直接同步调用 `deps_.request_refresh` 或 `run_real_refresh()`，完成后立刻清除 `refresh_in_flight_` | 长 refresh 期间 Runtime / health / caller 难以观测 accepted/in-flight/progress；Repeated refresh 测试也只证明同步完成后可再次 accepted | 增加 refresh job runner、queue/in-flight state、progress/terminal status，并更新 tests 覆盖 Busy / Accepted / Completed / Failed |
| KNO-GAP-002 | High | `init()` 不触发首启 active snapshot build | `KnowledgeServiceFacade::init()` 主要校验配置并进入 Running；无 active snapshot 时 `IndexReader` 会返回 unavailable；installed probe 需手工 `request_refresh({})` | 首次 retrieve 可能因未建索引失败；installed/runtime 启动后不能保证 Knowledge ready | 在 init/factory/composition 中定义首建策略：同步 prewarm、后台 refresh 或 fail-closed ready state，并补首启 retrieve/health tests |
| KNO-GAP-003 | High | concrete vector / hybrid production 链未闭合 | Knowledge 只有 `IQueryEncoder` / `IVectorRecallStore` / bridge；factory `vector_enabled=false`；Memory sqlite-vss focused seam 已完成但真实 extension / package / production recall store 未接线 | 详设中的 hybrid/dense semantic recall 不能宣称完成；profile vector_enabled 只能落到 degrade/fallback | 实现 Memory/Knowledge 间 concrete vector recall store 与 embedding/upsert lifecycle，或冻结 v1 lexical-only production 口径 |
| KNO-GAP-004 | Medium / High | lane-level timeout、parallel recall、cancel 未实现 | `RecallCoordinatorPolicy` 有 `max_parallel_recall`、`sparse_lane_timeout_ms`、`dense_lane_timeout_ms`，但 `RecallCoordinator::recall()` 串行调用 lanes，未计时/取消 | 复杂查询下 deadline 策略只剩 facade coarse checkpoint；dense/sparse 慢 lane 会拖累全链 | 为 recall coordinator 增加 parallel execution / timeout wrapper / late result discard，补 sparse slow、dense slow、partial degraded tests |
| KNO-GAP-005 | Medium / High | installed 首批 corpus baseline 与详设不一致 | `make_installed_asset_descriptors()` 仅创建 `dasall_profiles` 与 `dasall_llm_providers`；缺 architecture / ADR / SSOT / profile policy normative corpora | Runtime 查询无法覆盖详设要求的架构、ADR、SSOT 证据面；installed retrieve 质量被限制 | 扩展 installed corpus descriptors 与 asset staging，补 routing/retrieval tests 覆盖 normative corpora |
| KNO-GAP-006 | Medium | VersionLedger / CorpusCatalog 持久化、migration、startup recovery 不完整 | `VersionLedger` 使用内存 `std::vector<VersionLedgerEntry>`；`CorpusCatalog` 使用内存 snapshot；`IndexWriter` 有 manifest sidecar 但未见完整 migration manager / catalog reload | 进程重启后 ledger/catalog 状态不可完整恢复，rollback/LKG 和 format migration 不满足详设持久化口径 | 增加 JSONL/SQLite ledger、catalog sidecar/reload、format version migrator、startup LKG recovery tests |
| KNO-GAP-007 | Medium | Retrieve telemetry payload 与 production sink 未闭合 | `KnowledgeTelemetry::has_required_fields(Retrieve)` 要求 `profile_id`；`KnowledgeServiceFacade` success/fail retrieve event 未设置；installed factory 使用 `TelemetrySinks{}` | retrieve event 可能被判 invalid payload 并 dropped；生产日志/metric/trace/audit 无闭环 | 在 query/config/facade 中传入 profile_id，factory/runtime 注入 production sinks，并补 `KnowledgeRetrieveTelemetryFieldsTest` |
| KNO-GAP-008 | Medium | Runtime write-side refresh trigger 未证明 | 源码证据显示 Runtime retrieve 读链路存在；未见生产路径在 corpus/source 变化时调用 `IKnowledgeService::request_refresh()` | Index 更新依赖外部/测试显式触发，无法证明 live corpus 变化后自动刷新 | 定义 Runtime/apps/daemon refresh trigger seam，或明确 Knowledge v1 只支持 manual refresh，并补 integration test |
| KNO-GAP-009 | Medium | installed asset probe 当前失败 | `KnowledgeInstalledAssetProbeIntegrationTest` 构造 `InstalledAssetKnowledgeServiceOptions` 未填 `service_instance_id`，而 options 校验要求非空，CTest 当前失败 | build-tree installed asset gate 不绿，阻断把 installed factory 作为已验证链路证据 | 修复 probe fixture 或兼容 options 默认值，并补 factory positive / negative tests |
| KNO-GAP-010 | Medium | release runner / qemu / soak 证据不足 | 本轮只跑 build-tree CTest；未执行 package installed lifecycle、qemu、release preflight、long-run freshness/refresh soak | 不能宣称 Knowledge release-ready 或 production-ready | 增加 installed package smoke、qemu gate、refresh/retrieve long-run、disk/corrupt snapshot failure injection |
| KNO-GAP-011 | Low / Medium | 边界回归自动化不足 | 当前源码/CMake 未见越过 LLM/Memory/Runtime owner 的明显问题，但缺专门 include/link/symbol guard | 后续改动可能破坏 ADR-006/007/008，特别是直接生成 ContextPacket 或直接依赖 LLM provider | 增加 `KnowledgeBoundaryGuardComplianceTest` 或脚本，锁定 include/link 与 forbidden symbols |

### 7.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| KNO-FIX-001 | Todo | 实现异步 refresh job runner | 更新 `KnowledgeServiceFacade` / 新增 refresh worker，拆分 Accepted / Busy / Completed / Failed 状态，保留 synchronous test helper | `KnowledgeRefreshAsyncLifecycleTest`、`KnowledgeRefreshLoopTest`、`KnowledgeServiceFacadeRealRefreshTest` | `RunCtest_CMakeTools(tests=["KnowledgeRefreshAsyncLifecycleTest","KnowledgeRefreshLoopTest","KnowledgeServiceFacadeRealRefreshTest"])` | 长 refresh 可被观察为 in-flight；并发 refresh 返回 Busy；完成后 health/freshness/snapshot 状态可查询 |
| KNO-FIX-002 | Todo | 补齐首启 index build / prewarm 策略 | 更新 `KnowledgeServiceFacade::init()`、installed factory 或 runtime composition，明确首启是否后台 refresh、同步 prewarm 或 fail-closed | `KnowledgeInitPrewarmTest`、`KnowledgeInstalledAssetProbeIntegrationTest`、`dasall_knowledge_retrieval_smoke_integration_test` | `RunCtest_CMakeTools(tests=["KnowledgeInitPrewarmTest","KnowledgeInstalledAssetProbeIntegrationTest","dasall_knowledge_retrieval_smoke_integration_test"])` | 初始化后 health 状态与 retrieve 行为一致；无 active snapshot 时不会误报 ready |
| KNO-FIX-003 | Todo | 收口 vector/hybrid production 口径 | 若实现：接入 concrete vector store/encoder、embedding/upsert、dense recall；若冻结：将 production config/factory/profile 明确限制为 lexical-only | `VectorRetrieverBridgeTest`、`RecallCoordinatorDenseBridgeTest`、新增 `KnowledgeHybridRetrievalIntegrationTest` | `RunCtest_CMakeTools(tests=["VectorRetrieverBridgeTest","RecallCoordinatorDenseBridgeTest","KnowledgeHybridRetrievalIntegrationTest"])` | vector_enabled profile 要么真实可用，要么 fail-closed/lexical fallback 口径被设计和代码共同固定 |
| KNO-FIX-004 | Todo | 实现 recall lane timeout 与 parallel policy | 更新 `RecallCoordinator`，执行 `max_parallel_recall`、sparse/dense timeout、cancel/late discard 和 partial degrade | `RecallCoordinatorTimeoutTest`、`RecallCoordinatorDenseBridgeTest`、`RetrievalQualityRegressionTest` | `RunCtest_CMakeTools(tests=["RecallCoordinatorTimeoutTest","RecallCoordinatorDenseBridgeTest","RetrievalQualityRegressionTest"])` | 慢 lane 不拖垮整体 deadline；hybrid partial result 有明确 degraded reason |
| KNO-FIX-005 | Todo | 扩展 installed normative corpus baseline | 更新 `KnowledgeServiceFactory`、asset staging / packaging，新增 architecture / ADR / SSOT / profile policy descriptors | `KnowledgeInstalledAssetProbeIntegrationTest`、`RetrievalQualityRegressionTest`、新增 normative corpus routing test | `RunCtest_CMakeTools(tests=["KnowledgeInstalledAssetProbeIntegrationTest","RetrievalQualityRegressionTest","KnowledgeInstalledNormativeCorpusTest"])` | installed factory 能索引并检索详设定义的首批 normative corpora |
| KNO-FIX-006 | Todo | 持久化 ledger/catalog 与 startup recovery | 新增 ledger/catalog persistence、manifest/catalog reload、format migration、LKG recovery；扩展 `IndexWriter` / `IndexReader` startup path | `VersionLedgerPersistenceTest`、`CorpusCatalogPersistenceTest`、`IndexStartupRecoveryTest` | `RunCtest_CMakeTools(tests=["VersionLedgerPersistenceTest","CorpusCatalogPersistenceTest","IndexStartupRecoveryTest"])` | 进程重启后能恢复 active/LKG snapshot 和 corpus catalog；format version mismatch 可 fail-closed 或 migrate |
| KNO-FIX-007 | Todo | 补齐 telemetry fields 与 production sinks | 在 config/query/facade 中填充 `profile_id`；apps/runtime composition 注入 log/metrics/trace/audit sinks；补 fallback invalid payload 测试 | `KnowledgeTelemetryTest`、`KnowledgeRetrieveTelemetryFieldsTest`、`KnowledgeProductionTelemetryIntegrationTest` | `RunCtest_CMakeTools(tests=["KnowledgeTelemetryTest","KnowledgeRetrieveTelemetryFieldsTest","KnowledgeProductionTelemetryIntegrationTest"])` | retrieve event 不再被 invalid payload 丢弃，production-composed service 有可观测 sink 证据 |
| KNO-FIX-008 | Todo | 定义并实现 refresh trigger seam | 在 runtime/apps/daemon 或 knowledge admin seam 中定义 source/corpus changed -> `request_refresh()` 触发路径；或正式记录 v1 manual-only | `KnowledgeRuntimeRefreshTriggerIntegrationTest`、`KnowledgeRefreshLoopTest` | `RunCtest_CMakeTools(tests=["KnowledgeRuntimeRefreshTriggerIntegrationTest","KnowledgeRefreshLoopTest"])` | corpus 变化后 refresh 触发路径可复验，或文档明确 manual-only 且 UI/daemon 不误承诺自动刷新 |
| KNO-FIX-009 | Todo | 修复 installed asset probe 红灯 | 更新 probe fixture 补 `service_instance_id`，或调整 options 默认策略；确保重复 refresh / snapshot DB / DeepSeek provider query 仍断言 | `KnowledgeInstalledAssetProbeIntegrationTest` | `RunCtest_CMakeTools(tests=["KnowledgeInstalledAssetProbeIntegrationTest"])` | 当前失败测试转绿，且 failure path 仍能捕获 options inconsistent |
| KNO-FIX-010 | Todo | 建立 release / qemu / soak 证据 | 更新 packaging smoke、qemu/release runner、refresh/retrieve long-run 脚本和 SSOT evidence | installed package smoke、qemu gate、Knowledge refresh/retrieve soak | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | Knowledge 证据可从 L2/L3 升级到 L5；长稳态 refresh/retrieve/freshness 有记录 |
| KNO-FIX-011 | Todo | 增加 Knowledge 边界回归防线 | 新增 boundary test/script，检查 `knowledge/` 不 include/link LLM provider、Cognition implementation、Memory ContextOrchestrator implementation、Runtime AgentOrchestrator 私有实现 | `KnowledgeBoundaryGuardComplianceTest`、`RuntimeKnowledgeEvidenceIntegrationTest` | `RunCtest_CMakeTools(tests=["KnowledgeBoundaryGuardComplianceTest","RuntimeKnowledgeEvidenceIntegrationTest"])` | ADR-006/007/008 边界有自动化守护，Knowledge 继续只产 evidence，不产 ContextPacket/Prompt/Recovery 决策 |

### 7.7 非缺口但需守住的边界

以下事项当前不是缺口，后续修改时应继续守住：

1. Knowledge 只产 `EvidenceBundle`、`context_projection` 和 `RetrievalEvidenceRef`，不生成 `ContextPacket`，Context owner 仍归 Memory。
2. Knowledge 不渲染 Prompt，不调用 LLM provider，不拥有 PromptComposer / ModelRouter。
3. Knowledge 不裁定 retry / replan / abort；检索失败、stale、index unavailable 只能返回错误或 degraded evidence，恢复准入归 Runtime / RecoveryManager。
4. Runtime / AgentOrchestrator 继续拥有调用时机和全局主控；Knowledge 不自启任务主循环。
5. Lexical-only fallback 是可接受的 v1 退化能力，但不能被写成 semantic vector recall 已完成。
6. `KnowledgeTypes` 继续保持 module-local / module-public，不因 Runtime/Memory 使用 projection 就随意搬入 `contracts`。
7. Refresh/write path 与 retrieve/read path 应继续分离：retrieve 保持只读，refresh / ingest / index writer 才拥有写 snapshot 权限。

### 7.8 建议复验命令

本节命令用于后续把本章结论升级或关闭缺口。若使用 VS Code CMake Tools，应优先选择 `ListTests_CMakeTools` / `RunCtest_CMakeTools` 执行；以下 shell 形式仅作为文档化验收口径。

本轮通过的 Knowledge 聚焦测试等价命令口径：

```bash
ctest --test-dir build/vscode-linux-ninja --output-on-failure -R "KnowledgeProfileCompatibilityTest|RuntimeKnowledgeEvidenceIntegrationTest|KnowledgeRefreshLoopTest|RetrievalQualityRegressionTest|dasall_knowledge_retrieval_smoke_integration_test|KnowledgeHealthProbeTest|KnowledgeServiceFacadeRealRefreshTest|KnowledgeTelemetryTest|RecallCoordinatorDenseBridgeTest"
```

当前红灯复验口径：

```bash
ctest --test-dir build/vscode-linux-ninja --output-on-failure -R "KnowledgeInstalledAssetProbeIntegrationTest"
```

源码缺口复核：

```bash
rg -n "request_refresh|refresh_in_flight|service_instance_id|profile_id|VersionLedger|CorpusCatalog|max_parallel_recall|sparse_lane_timeout|dense_lane_timeout|vector_enabled|IVectorRecallStore|SqliteVssVectorBackend" \
  knowledge/include \
  knowledge/src \
  memory/include \
  memory/src \
  runtime/src/AgentOrchestrator.cpp \
  apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp \
  docs/todos/memory/DASALL_memory子系统专项TODO.md
```

Runtime / Memory evidence handoff 复核：

```bash
rg -n "knowledge_service->retrieve|make_knowledge_query|context_projection|retrieval_evidence_refs|external_evidence|ContextPacket" \
  runtime/src/AgentOrchestrator.cpp \
  memory/include/context/MemoryContextRequest.h \
  memory/src/context/ContextOrchestrator.cpp \
  tests/integration/knowledge \
  tests/integration/full_business_chain
```

边界回归口径：

```bash
rg -n "PromptComposer|PromptRegistry|ModelRouter|RecoveryManager|AgentOrchestrator|ContextOrchestrator|llm/src|cognition/src|memory/src|apps/" knowledge/include knowledge/src
```

release / qemu gate 口径：

```bash
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>
```

### 7.9 当前章节结论

Knowledge 子系统的实现覆盖度已经很高，当前主要问题不是“缺少 Knowledge 模块本体”，而是“refresh lifecycle、首启 build、vector/hybrid concrete backend、lane timeout/parallel recall、installed corpus baseline、ledger/catalog 持久化、production telemetry 与 installed/release 证据”尚未全部闭合。

因此本章冻结口径为：

1. Knowledge 主体：lexical retrieve、ingest、FTS5 index、evidence projection、profile compatibility 和 Runtime -> Memory handoff 已基本达到 L1 / L2。
2. 本轮复验：9 个 Knowledge 聚焦测试通过，`KnowledgeInstalledAssetProbeIntegrationTest` 失败，不能宣称 installed asset gate 绿色。
3. Runtime / Memory 读链路：已打通到 `ContextPacket` 证据投影，但 refresh/write trigger 与 installed/qemu/release runner 未证明。
4. Vector / hybrid：当前只能证明接口、bridge 和 fallback/degrade，不能宣称 production semantic vector recall 完成。
5. 查漏补缺优先级：先修 `KNO-GAP-001` / `KNO-GAP-002` / `KNO-GAP-009`，再收口 `KNO-GAP-003` / `KNO-GAP-004` / `KNO-GAP-005` / `KNO-GAP-007`，最后通过 `KNO-GAP-006` / `KNO-GAP-008` / `KNO-GAP-010` / `KNO-GAP-011` 提升 release 与边界可信度。

## 8. Tools 子系统查漏补缺

### 8.1 检查范围与依据

本轮检查目标：核对 `tools/*` 是否覆盖 `docs/architecture/DASALL_tools子系统详细设计.md` 中规定的功能，并判断与 Runtime、Services、Profiles、Infra / Plugin、MCP、Skill、Knowledge 及 app live composition 的链路是否打通。

检查范围：

1. Tools 源码与公共接口：`tools/include/*`、`tools/src/*`、`tools/CMakeLists.txt`。
2. Tools 单元与集成测试：`tests/unit/tools/*`、`tests/integration/tools/*`。
3. Runtime / app 接入链路：`runtime/include/RuntimeDependencySet.h`、`runtime/src/AgentOrchestrator.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`。
4. Services / plugin / MCP / Skill 相邻边界：`services/include/IExecutionService.h`、`services/include/IDataService.h`、`infra/include/plugin/IPluginManager.h`、`tools/include/plugin/IToolPluginProvider.h`、`tools/include/mcp/*`。
5. 用户括号中提到的 `knowledge/*`：只作为相邻子系统核对是否承载 Tool 详设实现。静态检查结果显示 `knowledge/*` 不调用 `IToolManager`，也不是 Tool 详设实现落点；只有 tests / profile 配置中出现 `agent.dataset`、`max_tool_calls`、`tool_visibility_rules`、`allowed_tool_domains` 等交叉字段。

本轮执行验证：使用 VS Code CMake Tools 在 `build/vscode-linux-ninja` 中执行 11 个聚焦测试，结果全部通过：`ToolInterfaceSurfaceTest`、`ToolManagerPipelineTest`、`BuiltinExecutorLaneTest`、`ToolServicesSmokeIntegrationTest`、`ToolMCPFallbackIntegrationTest`、`ToolPluginStdioMCPIntegrationTest`、`ToolPluginSkillBundleIntegrationTest`、`ToolObservabilityIntegrationTest`、`RuntimeUnaryIntegrationTest`、`DaemonRuntimeLiveDependencyCompositionTest`、`FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest`。

未执行项：本轮未执行 installed package、qemu、release runner、long-running soak 或真实外部 MCP server / plugin load gate；这些不得由 L1/L2 fixture 结果外推。

### 8.2 总体结论

结论：`tools/*` 已经不再是详设早期记录中的 placeholder-only 状态。当前代码已经落地 `IToolManager`、`ToolInvocationContext`、`ToolInvocationEnvelope`、Registry、Validator、PolicyGate、RouteSelector、BuiltinExecutorLane、ToolServiceBridge、WorkflowEngine、MCP Adapter / Lane / Cache / Discovery / stdio transport、PluginExtensionBridge、SkillRegistry / Runtime / Importers、ResultProjector、CompensationLedger、ToolConfigAdapter 以及 audit / metrics / trace / health 桥。

但当前不能判定为“DASALL_tools 子系统详设所有功能全部完成、所有关联链路完全打通”。更准确的状态是：

1. Tools 模块内部治理链和主要 module-local 子域达到 L1 / L2 可信度，本轮聚焦测试全绿。
2. Runtime 已通过 `RuntimeDependencySet::tool_manager` 和 `AgentOrchestrator` 调用 `IToolManager::invoke()`；app live composition 也会注入一个 concrete `ToolManager`。
3. `BuiltinExecutorLane` 在代码上可调用注入的 `IExecutionService` / `IDataService`，并且 full-business-chain fixture 已证明 ToolManager -> services loopback 可成功；但 `apps/runtime_support` 生产装配给 builtin lane 的 `execution_service` / `data_service` 仍为空，实际落到 tools 内部默认服务，不能宣称 installed production tools -> concrete services 后端完全闭合。
4. MCP 的 stdio concrete 路径、fallback 和 plugin-delivered stdio 样本已可测；SSE / streamable-HTTP 只是接口枚举，没有 concrete transport；generic MCP rollout 仍只能记为 implemented-under-evaluation。
5. PluginExtensionBridge 能把插件导出的 builtin provider、stdio MCP launch spec、skill bundle 归一化为 snapshot，但未看到 tools 生产路径主动订阅 `IPluginManager`、扫描 active set，或自动把 snapshot 注入 ToolRegistry / CapabilityDiscovery / SkillRegistry。
6. CompensationLedger 与 side-effect hints 已存在，但 `ToolManager::compensate()` 当前固定返回 `tool.manager.compensation_unconfigured`，受控补偿入口未实现。
7. `knowledge/*` 不是 tools 子系统实现目录；Knowledge 与 Tools 当前主要通过 Runtime / profile / evidence 侧间接协作，不能把 Knowledge 的完成度当作 Tools 的完成度。

### 8.3 设计覆盖矩阵

| 设计域 | 当前覆盖判断 | 主要证据 | 缺口 / 边界 |
|---|---|---|---|
| 公共 include 与 CMake | 已覆盖 | `tools/CMakeLists.txt` 构建 `dasall_tools` 并列出真实 sources / public headers；`tools/include/IToolManager.h`、`ITool.h`、`IPolicyGate.h`、`ICapabilityCache.h`、`ToolInvocationContext.h`、`ToolInvocationEnvelope.h` 已存在 | CMake public link 只暴露 `dasall_contracts`，符合最小共享面；生产装配仍需注入真实后端 |
| ToolManager 总入口 | 基本覆盖 | `tools/src/ToolManager.cpp` 串联 registry -> validator -> config -> policy -> route -> execute -> projector -> metrics/trace/audit；`invoke_batch()` 已存在 | `invoke_batch()` 首版串行，符合预留接口但未实现并行优化；`compensate()` 未实现 |
| Registry / Builtin catalog | 基本覆盖 | `ToolRegistry`、`BuiltinCatalog`、`MCPBindingRegistry` 存在；builtin catalog 定义 `agent.terminal`、`agent.dataset` | 生产 live composition 只注册 `agent.dataset`；无 `tools/src/builtin/` 具体工具包装目录，builtin 语义集中在 descriptor + lane dispatch |
| Validator / ToolIR | 已覆盖 | `tools/src/validation/ToolValidator.cpp`；`ToolValidator*Test` 覆盖 defaulting、dry-run、boundary | 未发现越权升格 supporting object 到 contracts |
| Profile / PolicyGate | 已覆盖 | `ToolConfigAdapter` 消费 `RuntimePolicySnapshot` 中 `max_tool_calls`、tool/mcp/workflow timeout、capability cache、allowed domains、visibility；`ToolPolicyGate` fail-closed | domain 口径来自 ToolManager 派生的 builtin/workflow/mcp，不直接使用 caller domain；后续需继续防止 profile 键重解释 |
| RouteSelector / lane | 部分覆盖 | `ToolRouteSelector` 按 builtin / workflow / mcp 评分，`ExecutorLanePool` 输出 lane key 和 concurrency budget | 当前 lane pool 只是 reservation / availability 判断，没有真实队列、并发窗口扣减、overflow/backpressure/lock-order 执行机制 |
| BuiltinExecutorLane / ToolServiceBridge | 部分覆盖 | `BuiltinExecutorLane::dispatch_action/query/diagnose()` 调用注入的 `IExecutionService::execute()`、`IDataService::query()`；`ToolServiceBridge` 构造 services request；`BuiltinExecutorLaneTest` 证明 fake service 被调用 | 默认依赖与 app live composition 仍使用内部 default service；installed production concrete services 后端未证明 |
| WorkflowEngine | 基本覆盖 | `WorkflowEngine.cpp`、`WorkflowEngineTest`、`WorkflowCyclicRejectionTest`、`ToolWorkflowFailureIntegrationTest` | DAG-only / failure stop 已有证据；runtime-facing workflow plan 生产来源与 AgentDelegation sidecar 消费仍需更高层证据 |
| MCP runtime | 基本覆盖 | `MCPAdapter`、`MCPLane`、`CapabilityCache`、`CapabilityDiscovery`、`StdioMCPTransport`、`StdioMCPServerLauncher`；MCP fallback / plugin stdio integration 通过 | concrete transport 仅 stdio；SSE / streamable-HTTP 未实现；generic MCP rollout、外部 server 兼容矩阵、性能和长期 session 证据不足 |
| Plugin extension bridge | 部分覆盖 | `PluginExtensionBridge::on_plugin_loaded()` / `on_plugin_unloaded()` 使用 snapshot-and-swap；并发单测存在 | 未接 `IPluginManager::list_active()` / load-unload event；未自动推送到 ToolRegistry / CapabilityDiscovery / SkillRegistry；生产 live composition 未启用 plugin extension bridge |
| Skill runtime/importer | 基本覆盖 | `SkillRegistry`、`SkillRuntime`、`ExternalSkillImporter`、`PluginSkillBundleImporter` 与 skill integration tests 存在；external importer 默认受 `external_skill_import_enabled` 保护 | SkillRuntime 尚未证明进入 Runtime production 主链；external dialect rollout 仍需 profile/discoverability gate 证据 |
| ResultProjector / ObservationDigest | 已覆盖 | `ResultProjector.cpp` 规则化投影，不调用 LLM；投影测试与 tools services smoke 验证 Observation / ObservationDigest 同步产出 | 仍缺真实生产 payload 样本回归，不能证明所有工具结果摘要质量 |
| CompensationLedger / compensation | 部分覆盖 | `CompensationLedger.cpp` 与测试可记录 side_effects、LIFO hints、irreversible evidence；ToolManager 可根据 side_effects 生成 hint | `ToolManager::compensate()` 未接 ledger/services compensation，当前返回 unconfigured failure |
| Observability / health | 部分覆盖 | `ToolAuditBridge`、`ToolMetricsBridge`、`ToolTraceBridge`、`ToolHealthProbe` 与 `ToolObservabilityIntegrationTest` 存在 | ToolManager 默认 metrics/trace disabled；app live composition 未注入 infra metrics/tracing/audit sink；production 可观测链路不能外推 |
| Knowledge 关联 | 非 tools 实现域 | `knowledge/*` 无 `tools::` / `IToolManager` 生产调用；Runtime 分别持有 `knowledge_service` 与 `tool_manager` | Knowledge 完成度不等于 Tools 完成度；二者链路主要通过 Runtime / evidence / profile 间接汇合 |

### 8.4 关联模块链路判断

| 链路 | 是否打通 | 当前可信层级 | 判断 |
|---|---|---:|---|
| Runtime -> IToolManager | 基本打通 | L2 | `RuntimeDependencySet` 持有 `std::shared_ptr<tools::IToolManager>`；`AgentOrchestrator` 构造 `ToolInvocationContext` / `ToolRequest` 并调用 `tool_manager->invoke()`；`RuntimeUnaryIntegrationTest` 通过 |
| app live composition -> ToolManager | 部分打通 | L2 / L3 boundary | `RuntimeLiveDependencyComposition` 创建 concrete `ToolManager`，`DaemonRuntimeLiveDependencyCompositionTest` 通过；但只注册 `agent.dataset` 且后端为 default service |
| Tools -> Services | 部分打通 | L1 / L2 | lane 代码和单测证明可调用注入的 `IExecutionService` / `IDataService`；full-business-chain fixture 证明 ToolManager -> services loopback | production live composition 未注入 concrete services facade，installed 链路不闭合 |
| Tools -> Profiles | 基本打通 | L1 / L2 | `ToolConfigAdapter` 直接消费 `RuntimePolicySnapshot`；profile integration 测试存在 |
| Tools -> Infra observability | 部分打通 | L1 / L2 | bridge 类与 integration test 存在；默认生产装配未启用 metrics/trace provider，也未证明 audit sink 落 infra hot path |
| Tools -> Infra/plugin | 部分打通 | L1 / L2 fixture | tools-local `IToolPluginProvider` 与 `PluginExtensionBridge` 存在；plugin stdio / skill bundle integration 通过 | 未接 `IPluginManager` active set / lifecycle event；生产自动接线不足 |
| Tools -> MCP server | 部分打通 | L2 fixture | stdio launch、handshake、capability cache、fallback integration 通过 | 仅 stdio concrete；外部真实 server、SSE / streamable-HTTP、long session / rollout gate 未证明 |
| Tools -> Knowledge | 间接 / 非依赖 | L0 / L2 partial | Runtime 分别调用 Knowledge 和 Tools；`knowledge/*` 不承载 tools 实现 | 不能把 `knowledge/*` 当 tools 代码目录；如需 `knowledge.search` tool，需要新增 builtin/MCP/skill binding |
| Tools -> cognition / llm / platform / runtime 实现 | 边界基本正确 | L0 | 未发现 tools 生产源码直接 include cognition/llm/platform/runtime 私有实现；Runtime 作为上游调用者存在 | 后续需用 boundary guard 防止回归 |
| Tools -> installed / package | 证据不足 | L0 / L3 partial | build tree 和 app composition 有证据 | 未执行 installed package、qemu、release runner、soak；不能宣称 production-ready |

### 8.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 代码证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| TOOL-GAP-001 | High | production live composition 未注入真实 `IExecutionService` / `IDataService`，builtin 调用落到 tools 内部 default service | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 构造 `BuiltinExecutorLaneDependencies{ .execution_service = nullptr, .data_service = nullptr }`；`BuiltinExecutorLane::default_dependencies()` 内置 `DefaultExecutionService` / `DefaultDataService` | `DaemonRuntimeLiveDependencyCompositionTest` 成功只能证明 default service path，不证明 installed tools -> concrete services / platform 后端 | 在 runtime_support 组合真实 services facade，或显式把 default service 标成 fixture-only / preview fallback，并补 production bridge test |
| TOOL-GAP-002 | High | `ToolManager::compensate()` 未实现受控补偿入口 | `ToolManager::run_compensation_pipeline()` 固定返回 `tool.manager.compensation_unconfigured`；CompensationLedger 未被 compensate hot path 消费 | 详设中的 compensate 公共入口存在但不可用；副作用链路只能给 runtime hints，不能执行受控补偿 | 接入 CompensationLedger + services compensation request，保留 Runtime 恢复准入权 |
| TOOL-GAP-003 | High | plugin extension bridge 未与 infra/plugin lifecycle 自动接线 | tools 生产源码未使用 `IPluginManager` / `ActivePluginSet`；PluginExtensionBridge 只有 `on_plugin_loaded()` / `on_plugin_unloaded()` 和 snapshot | 插件 load/unload 不会自动更新 tools registry、MCP discovery 或 skill registry；部署期扩展闭环不完整 | 增加 PluginExtensionBridge lifecycle adapter / active set importer，并补 unload invalidation integration |
| TOOL-GAP-004 | High | plugin 导出的 builtin provider / MCP launch spec / skill bundle 未自动进入 ToolRegistry / CapabilityDiscovery / SkillRegistry | `PluginExtensionBridge` 仅生成 snapshot；integration tests 手动读取 bridge snapshot 后组装 MCP/Skill 子域 | plugin 激活不等于 capability 可见这一点守住了，但缺少详设要求的二次解析自动接线 | 实现 extension catalog consumer，将 snapshot delta 分发到 registry、discovery、skill importer |
| TOOL-GAP-005 | Medium / High | lane/bulkhead 隔离仍偏逻辑判定，缺少真实并发窗口、overflow_policy、backpressure 与 lock-order 证据 | `ExecutorLanePool` 只返回 `concurrency_budget`，未见队列、令牌扣减或超限拒绝；现有并发测试偏 registry/bridge snapshot | 高频工具调用或 MCP 阻塞时仍可能共享执行资源，详设 TOOL-C016 / TOOL-C021 未完全闭合 | 增加 lane scheduler / concurrency guard，定义 overflow/backpressure，并补压力测试 |
| TOOL-GAP-006 | Medium | MCP concrete transport 只有 stdio，generic MCP rollout 仍不应宣称完成 | `IMCPTransport` 枚举含 `sse` / `streamable_http`，但实际文件只有 `StdioMCPTransport.*`；`MCPAdapter::default_dependencies()` 非 stdio 返回 nullptr | 只能宣称 stdio MCP 路径实现；不能宣称 generic MCP / remote socket family 全面可用 | 补 SSE / streamable-HTTP adapter 或明确 v1 rollout 只支持 stdio，并建立 compatibility matrix |
| TOOL-GAP-007 | Medium | production observability / audit sink 未接入 app live composition | `ToolManager::default_dependencies()` 创建 disabled metrics / trace bridge；`RuntimeLiveDependencyComposition` 未传 infra provider / tracer / audit logger | fixture 观测通过不能外推 production trace/metrics/audit hot path | 在 runtime_support 注入 infra sinks，扩展 `ToolObservabilityIntegrationTest` 或新增 production composition test |
| TOOL-GAP-008 | Medium | builtin tool 的具体包装层目录和扩展模式缺失 | `tools/src/registry/BuiltinCatalog.cpp` 有 `agent.terminal` / `agent.dataset` descriptor，但无 `tools/src/builtin/` 目录；生产 composition 仅注册 `agent.dataset` | 后续新增 agent terminal / dataset 的参数校验、schema、服务请求映射容易继续散落 | 建立 `tools/src/builtin/terminal`、`tools/src/builtin/dataset` 或等价 internal wrapper，注册逻辑仍收敛到 BuiltinCatalog |
| TOOL-GAP-009 | Medium | Runtime production 可见工具面过窄且与 builtin catalog 不一致 | builtin catalog 有 `agent.terminal` 与 `agent.dataset`；`compose_runtime_tool_manager()` 只注册 `agent.dataset` | installed/app 只能证明 dataset query；不能证明 high-risk action / terminal / diagnostic builtin 路径 | 明确 runtime profile 下默认可见工具，补 `agent.terminal` action gate、confirmation 和 services backend tests |
| TOOL-GAP-010 | Medium | Skill runtime 仍未进入 Runtime production 主链 | `SkillRegistry` / `SkillRuntime` / importers 与 integration tests 存在；`RuntimeLiveDependencyComposition` 和 `ToolManager` hot path 未装配 SkillRuntime | Skill 能力是 module subdomain 可用，不代表 runtime 自动发现 / instantiate / workflow route 可用 | 增加 SkillRuntime -> WorkflowEngine -> ToolManager runtime-facing integration 或明确保持离线/导入阶段能力 |
| TOOL-GAP-011 | Low / Medium | ResultProjector 缺少生产 payload golden regression | `ResultProjector` 单元与 smoke 验证存在，但样本主要为 fixture payload | 规则化摘要在真实复杂 payload 下可能丢关键字段或过度截断 | 收集真实 ToolResult payload golden set，补 projector regression gate |
| TOOL-GAP-012 | Low / Medium | installed package / qemu / release runner / soak 证据缺失 | 本轮只运行 build-tree CTest；未执行 package 或 installed 命令 | 不能从源码和 L2 fixture 外推 production-ready | 增加 installed `dasall run` tools-positive evidence、qemu/autopkgtest gate、long-session MCP/tool soak |

### 8.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| TOOL-FIX-001 | Todo | 接入 runtime production services 后端 | 更新 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`，为 `BuiltinExecutorLane` 注入 concrete `IExecutionService` / `IDataService` 或 services facade composition；default service 仅保留 fixture / preview fallback | 新增 `ToolServicesProductionBridgeIntegrationTest` 或扩展 `DaemonRuntimeLiveDependencyCompositionTest`，断言 payload 来自 services concrete / loopback backend 而非 tools default service | `RunCtest_CMakeTools(tests=["ToolServicesSmokeIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest","FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest"])` | app live composition 的 `agent.dataset` / action tool 不再依赖 tools 内部 default service |
| TOOL-FIX-002 | Todo | 实现 `ToolManager::compensate()` 受控补偿入口 | 更新 `tools/src/ToolManager.cpp` 与 `BuiltinExecutorLane` / `ToolServiceBridge`，把 `CompensationRequest` 映射到 services compensation；保留 Runtime 负责是否触发补偿 | 新增 `ToolManagerCompensationPipelineTest`、`ToolCompensationServicesIntegrationTest`，覆盖 success、unconfigured、irreversible、policy denied | `RunCtest_CMakeTools(tests=["CompensationLedgerTest","ToolManagerCompensationPipelineTest","ToolCompensationServicesIntegrationTest"])` | `IToolManager::compensate()` 不再固定 unconfigured；side_effect evidence 与 compensation_hints 可回传 runtime |
| TOOL-FIX-003 | Todo | 打通 infra/plugin -> tools extension 自动接线 | 新增 lifecycle adapter，消费 `IPluginManager::list_active()` 或 load/unload event，把 `ToolPluginExtensionCatalog` 送入 `PluginExtensionBridge` | 扩展 `PluginExtensionBridgeTest`，新增 `ToolPluginLifecycleBridgeIntegrationTest`，验证 load/unload 后 descriptor、MCP launch spec、skill asset 可见性变化 | `RunCtest_CMakeTools(tests=["PluginExtensionBridgeTest","PluginExtensionBridgeConcurrencyTest","ToolPluginLifecycleBridgeIntegrationTest"])` | plugin 激活后仍经 ToolRegistry / CapabilityDiscovery / SkillRegistry 二次治理，但不再需要 tests 手工调用 bridge |
| TOOL-FIX-004 | Todo | 将 plugin snapshot 分发到 registry / discovery / skill 子域 | 实现 extension catalog consumer：builtin provider -> ToolRegistry，stdio MCP -> CapabilityDiscovery / MCPBindingRegistry，skill bundle -> PluginSkillBundleImporter / SkillRegistry；卸载时撤销 source | 新增 `ToolPluginExtensionEndToEndIntegrationTest` 覆盖 builtin provider、stdio MCP、skill bundle 三类 source revoke | `RunCtest_CMakeTools(tests=["ToolPluginStdioMCPIntegrationTest","ToolPluginSkillBundleIntegrationTest","ToolPluginExtensionEndToEndIntegrationTest"])` | plugin capability 从 active set 到 tools 可见性形成自动闭环，unload 后调用 fail-closed |
| TOOL-FIX-005 | Todo | 补齐 lane bulkhead 执行语义 | 更新 `ExecutorLanePool` 或新增 lane scheduler，显式实现并发窗口扣减、overflow_policy、backpressure reason code 与 lock-order 文档 | 新增 `ExecutorLanePoolConcurrencyTest`、`ToolLaneBackpressureIntegrationTest`、MCP 阻塞不影响 builtin 的测试 | `RunCtest_CMakeTools(tests=["ToolRouteSelectorTest","ToolLaneBackpressureIntegrationTest","ExecutorLanePoolConcurrencyTest"])` | builtin / workflow / MCP 故障域有真实资源隔离证据，不只是 lane key |
| TOOL-FIX-006 | Todo | 明确 MCP v1 transport 支持范围 | 二选一：A. 实现 SSE / streamable-HTTP transport；B. 在设计与配置中明确 v1 仅支持 stdio，并禁止 generic-ready 表述 | A 新增 transport / adapter tests；B 新增 rollout checklist 和 static wording guard | A: `RunCtest_CMakeTools(tests=["MCPAdapterTransportSwitchTest","MCPHttpTransportIntegrationTest"])`；B: `rg -n "generic MCP ready|MCP.*production-ready" docs tools tests` | MCP 兼容声明与 concrete transport 能力一致 |
| TOOL-FIX-007 | Todo | 接入 production tools observability sink | 扩展 `ToolManagerDependencies` / runtime_support composition，注入 infra metrics provider、tracer、audit logger；保留 bridge failure fail-open | 扩展 `ToolObservabilityIntegrationTest` 或新增 `ToolProductionObservabilityIntegrationTest`，断言 production-composed manager 发出 audit / metric / trace event | `RunCtest_CMakeTools(tests=["ToolObservabilityIntegrationTest","ToolProductionObservabilityIntegrationTest"])` | production live path 不再只使用 disabled metrics/trace bridge |
| TOOL-FIX-008 | Todo | 建立 concrete builtin wrapper 布局 | 新增 `tools/src/builtin/terminal`、`tools/src/builtin/dataset` 或等价 internal wrapper；把 descriptor、参数约束、services request mapping 模式固化 | 新增 `AgentDatasetToolTest`、`AgentTerminalToolPolicyTest`，覆盖 read-only query、高风险 action confirmation、schema / argument mapping | `RunCtest_CMakeTools(tests=["AgentDatasetToolTest","AgentTerminalToolPolicyTest","BuiltinExecutorLaneTest"])` | builtin 扩展不再只靠 catalog descriptor + generic service bridge |
| TOOL-FIX-009 | Todo | 建立 tools installed / release 证据 | 不一定改产品代码；补 installed `dasall run` tools-positive probe、package install layout 检查、qemu / release runner gate | 新增或扩展 package smoke，断言 installed app 能走 `IToolManager` governed route 并产生 observation digest | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>`；installed `sudo -n dasall run ... --json --timeout-ms 120000` | tools 从 L2 fixture 提升到 L4/L5 evidence，不再只停留在 build tree |
| TOOL-FIX-010 | Todo | 收口 `knowledge.search` / Knowledge 与 Tools 的关系 | 若需要将 Knowledge 暴露为 tool，新增 `knowledge.search` builtin/MCP/skill binding，通过 ToolRegistry/PolicyGate/RouteSelector 调用 Runtime/Services 稳定门面；若不需要，则在 docs 明确 `knowledge/*` 非 tools 实现域 | 新增 `ToolKnowledgeSearchIntegrationTest` 或文档 boundary guard，防止误把 Knowledge 子系统实现当 Tools 子系统 | `RunCtest_CMakeTools(tests=["RuntimeKnowledgeEvidenceIntegrationTest","ToolKnowledgeSearchIntegrationTest"])` 或 `rg -n "knowledge.search|IToolManager|tools::" knowledge tools tests` | Knowledge 与 Tools 的相邻关系被显式化，不再产生目录口径混淆 |

### 8.7 非缺口但需守住的边界

以下事项当前不应被当成缺口，后续实现时必须继续守住：

1. `ToolAdmissionDecision`、`ToolRouteDecision`、`CapabilitySnapshot`、`SkillSpecAsset`、`WorkflowPlan` 等 supporting object 继续保持 tools module-local / module-public，不应为了补链路而提前写入 `contracts/include`。
2. Tools 不直接调用 cognition / llm，也不拥有 prompt、reasoning 或 recovery decision；ResultProjector 继续采用规则化投影，不引入 LLM provider。
3. Tools 不直接拥有 Memory ContextPacket 装配权；Observation / ObservationDigest 由 Runtime / Memory 后续消费。
4. AgentDelegation 仍必须经 Runtime / AgentOrchestrator 裁定，不应在 WorkflowEngine 内直接拉起 MultiAgentCoordinator。
5. Plugin signature、ABI、load/unload、safe mode 决策继续归 infra/plugin；Tools 只消费已激活插件导出的工具域资产并做二次治理。
6. `knowledge/*` 是 Knowledge 子系统，不是 Tools 子系统源码替代目录；二者通过 Runtime / profile / evidence 间接协作即可。

### 8.8 本轮复验命令与结果

本轮通过 VS Code CMake Tools 执行以下聚焦 CTest，全部通过：

```text
ToolInterfaceSurfaceTest
ToolManagerPipelineTest
BuiltinExecutorLaneTest
ToolServicesSmokeIntegrationTest
ToolMCPFallbackIntegrationTest
ToolPluginStdioMCPIntegrationTest
ToolPluginSkillBundleIntegrationTest
ToolObservabilityIntegrationTest
RuntimeUnaryIntegrationTest
DaemonRuntimeLiveDependencyCompositionTest
FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest
```

该结果可采信为：

1. Tools public surface、ToolManager pipeline、BuiltinExecutorLane 注入式 services 调用、MCP fallback、plugin stdio MCP fixture、plugin skill bundle fixture、observability fixture、Runtime/app build-tree 调用链均有 L1 / L2 正向证据。
2. 不能外推为：production services 后端、真实 installed package、qemu / release runner、真实外部 MCP server、真实 plugin lifecycle、long-running soak 已完成。

建议后续复验命令：

```text
RunCtest_CMakeTools(tests=[
  "ToolInterfaceSurfaceTest",
  "ToolManagerPipelineTest",
  "BuiltinExecutorLaneTest",
  "ToolServicesSmokeIntegrationTest",
  "ToolMCPFallbackIntegrationTest",
  "ToolPluginStdioMCPIntegrationTest",
  "ToolPluginSkillBundleIntegrationTest",
  "ToolObservabilityIntegrationTest",
  "RuntimeUnaryIntegrationTest",
  "DaemonRuntimeLiveDependencyCompositionTest",
  "FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest"
])
```

installed / release 证据仍需单独执行：

```bash
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>
```

### 8.9 当前章节结论

Tools 子系统的当前实现覆盖度已经较高，不能再按详设早期“仅 placeholder”口径判断。当前更准确的冻结口径为：

1. Tools 主体：公共接口、治理链、路由、投影、Workflow、MCP stdio、Skill、observability 和测试拓扑基本达到 L1 / L2。
2. Runtime/app 接入：build-tree 路径已可调用 `IToolManager`，但 production live composition 的 services 后端仍是 default fallback，不能宣称真实 installed tools -> services -> platform 链路闭合。
3. Plugin / MCP / Skill 扩展：fixture 和 module subdomain 可用，但 plugin lifecycle 自动接线、generic MCP rollout、Runtime production skill 消费仍待补。
4. Compensation / bulkhead / observability：存在支撑组件，但受控补偿入口、真实并发窗口和 production sink 仍未完全闭合。
5. Knowledge 关系：`knowledge/*` 不是 tools 详设实现落点；若未来需要 `knowledge.search` tool，应作为明确的 ToolRegistry / PolicyGate / RouteSelector 绑定任务单独落地。

查漏补缺优先级：先修 `TOOL-GAP-001` / `TOOL-GAP-002` / `TOOL-GAP-003` / `TOOL-GAP-004`，再收口 `TOOL-GAP-005` / `TOOL-GAP-006` / `TOOL-GAP-007` / `TOOL-GAP-008` / `TOOL-GAP-009`，最后通过 `TOOL-GAP-010` / `TOOL-GAP-011` / `TOOL-GAP-012` 提升 runtime production、installed 和 release 可信度。

## 9. Capability Services 子系统查漏补缺

### 9.1 检查范围与依据

本轮检查目标：核对 `services/*` 是否覆盖 `docs/architecture/DASALL_capability_services子系统详细设计.md` 中规定的功能，并判断与 Tools、Runtime / apps、Profiles、Infra observability / health、Platform / remote adapter 以及测试链路是否打通。

检查范围：

1. Services 公共 ABI 与源码：`services/include/*`、`services/src/*`、`services/CMakeLists.txt`。
2. Services 单元与集成测试：`tests/unit/services/*`、`tests/integration/services/*`、`tests/mocks/include/CapabilityServicesLoopbackFixture.h`。
3. Tools / Runtime / app 接入链路：`tools/src/execution/BuiltinExecutorLane.cpp`、`tools/src/bridge/ToolServiceBridge.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`apps/runtime_support/CMakeLists.txt`。
4. Profiles / Infra 相邻边界：`profiles/include/RuntimePolicySnapshot.h`、`profiles/include/BuildProfileManifest.h`、`infra/include/audit/*`、`infra/include/metrics/*`、`infra/include/tracing/*`、`infra/include/health/*`。
5. 证据边界：本章优先采用当前源码和 CTest 结果；历史 TODO / worklog 仅作为辅助线索，不直接等同当前完成结论。

本轮执行验证：使用 VS Code CMake Tools 在 `build/vscode-linux-ninja` 中执行 31 个 services / services-adjacent 聚焦测试，结果全部通过：`ServiceHeaderLayoutTest`、`ServiceContextBuilderTest`、`ServiceFacadeTest`、`AdapterRouterTest`、`AdapterBridgeTest`、`LocalPlatformAdapterTest`、`LocalServiceAdapterTest`、`RemoteServiceAdapterTest`、`ExecutionCommandLaneTest`、`ExecutionQueryLaneTest`、`ExecutionSubscriptionHubTest`、`ExecutionDiagnoseServiceTest`、`DataProjectionCacheTest`、`DataQueryLaneTest`、`ResultMapperTest`、`ServiceAuditBridgeTest`、`ServiceMetricsBridgeTest`、`ServiceTraceBridgeTest`、`ServiceConfigAdapterTest`、`ServiceHealthProbeTest`、`SystemSnapshotLaneTest`、`CapabilityServicesSmokeIntegrationTest`、`CapabilityServicesFailureIntegrationTest`、`CapabilityServicesProfileIntegrationTest`、`CapabilityServicesAuditIntegrationTest`、`CapabilityServicesMetricsIntegrationTest`、`CapabilityServicesTraceIntegrationTest`、`CapabilityServicesHealthIntegrationTest`、`DaemonRuntimeLiveDependencyCompositionTest`、`BuiltinExecutorLaneResultCodeTest`、`ServiceResultSemanticsContractTest`。

未执行项：本轮未执行 installed package、qemu、release runner、long-running soak、真实 platform HAL handler、真实 remote service endpoint 或 production infra sink gate；这些不得由 L1/L2 测试结果外推。

### 9.2 总体结论

结论：`services/*` 已经基本覆盖 Capability Services 详设中的主体功能，不能再按 placeholder 或 skeleton 口径判断。当前代码已经落地 public ABI 三件套、`ServiceFacade`、`ServiceContextBuilder`、Execution Command / Query / Subscription / Diagnose、Data Query / Projection Cache / Catalog、internal SystemSnapshotLane、AdapterRouter / AdapterBridge / LocalPlatformAdapter / LocalServiceAdapter / RemoteServiceAdapter、ResultMapper、CompensationCatalog、ServiceConfigAdapter、ServiceHealthProbe、ServiceAuditBridge、ServiceMetricsBridge、ServiceTraceBridge，并且 services focused / integration 测试拓扑可发现且本轮全绿。

但当前不能判定为“所有功能完全完成、所有关联链路完全打通”。更准确的状态是：

1. Services 模块内部主体实现达到 L1 / L2 可信度，public ABI、核心 lanes、adapter 路由、结果映射、配置派生、健康与可观测桥都有真实源码与测试证据。
2. Tools 代码可以通过注入的 `IExecutionService` / `IDataService` 调用 services，full-business-chain fixture 也能证明 ToolManager -> services loopback 正向链路。
3. Runtime / app production composition 仍未真正实例化并注入 `ServiceFacade` 或 concrete services backend；`compose_runtime_tool_manager()` 给 `BuiltinExecutorLane` 传入空 `execution_service` / `data_service`，实际由 `BuiltinExecutorLane::default_dependencies()` 回落到 tools 内部 `DefaultExecutionService` / `DefaultDataService`。
4. Adapter 层是 callback/seam 形式，测试 fixture 通过注入 loopback handler 形成闭环；真实 platform HAL、local service endpoint、remote service endpoint 的生产 handler / registry / health-probe 接线尚未证明。
5. 当前存在至少两个源码级缺口：`DataQueryLane` cache hit 返回 `code=ToolExecutionFailed` 且 `error=nullopt`，与 services result triad 约定不一致；`ServiceFacade::subscribe()` 未像其他 public 方法一样创建 facade trace span，subscription public ABI 的 trace 链不完整。

### 9.3 设计覆盖矩阵

| 设计域 | 当前覆盖判断 | 主要证据 | 缺口 / 边界 |
|---|---|---|---|
| public ABI 三件套 | 已覆盖 | `services/include/ServiceTypes.h`、`IExecutionService.h`、`IDataService.h`；`services/CMakeLists.txt` public FILE_SET 仅暴露这三份头 | 当前不应迁入 `contracts/include`；后续需继续守住最小共享面 |
| `ServiceFacade` 统一入口 | 基本覆盖 | `ServiceFacade final : IExecutionService, IDataService`，实现 execute / compensate / query_state / subscribe / diagnose / query / list_capabilities | `subscribe()` 缺 facade trace span；production app composition 未实例化 facade |
| `ServiceContextBuilder` | 已覆盖 | normalize context、request/session/trace/tool/goal/budget/deadline 校验与默认化有 unit test | `ServiceCallContext` 不含 caller_domain，caller domain 仍由 Tools PolicyGate 承担 |
| Execution Command Lane | 已覆盖 | validation、idempotency cache、serialization key、critical/high-risk/safe-mode gate、router/bridge、compensation hints、audit/metrics/trace 已实现 | high-risk gate 默认 fail-closed；真实 backend handler 未证明 |
| Execution Query Lane | 已覆盖 | read-only query route、side-effect rejection、stale cache fallback、metrics/trace 已实现 | cached snapshot provider 仍是注入 seam，production state backend 未证明 |
| Execution Subscription | 基本覆盖 | `ExecutionSubscriptionHub` 支持 publish/subscribe、cursor、drop-oldest overflow、resync_required、metrics；failure integration 覆盖 overflow | hub 无 trace_bridge，facade subscribe 也无 trace span；state stream 的 production publisher 未证明 |
| Execution Diagnose | 基本覆盖 | `ExecutionDiagnoseService` 存在并有 focused test；facade diagnose 可 trace | runtime/app production 默认 diagnose 仍可能落 tools default service |
| Data Query / Catalog | 部分覆盖 | `DataQueryLane` 路由 dataset/projection、cache lookup/store、side-effect rejection、catalog.list，`DataProjectionCache` 有 TTL/stale 语义 | cache hit result triad 缺陷；单 lane dependency 只有一个 `CapabilitySnapshotView`，多 dataset / dynamic snapshot production registry 未证明 |
| System Snapshot | 基本覆盖 | `SystemSnapshotLane` internal-only 聚合 infra health、platform snapshot、resource summary、service instances | 只通过 callback seam；runtime/tools/apps 非测试消费者未证明，继续不进入 public ABI |
| AdapterRouter | 已覆盖 | 支持 capability snapshot、operation support、route class、preferred locality、fallback envelope、trust / availability / local platform enable fail-closed | `caller_domain_allowlist` 未在 router 内消费，当前依赖上游 Tool PolicyGate；动态 adapter probe registry 未证明 |
| AdapterBridge / adapters | 基本覆盖 | bridge 按 `adapter_id` 调 invoker，处理 missing invoker、route mismatch、exception；local_platform/local_service/remote_service adapters 有 options 与 tests | adapters 本身是 callback wrappers，缺真实 platform HAL / remote endpoint production 接线证据 |
| ResultMapper / triad | 基本覆盖 | `ResultMapper` 统一 provider status -> `ResultCode` / `ErrorInfo`；success 默认 `code=nullopt,error=nullopt`；contract tests 通过 | `DataQueryLane` cache hit 绕过 mapper 并写入伪失败 code，形成源码级不一致 |
| CompensationCatalog | 基本覆盖 | 默认 `toggle -> switch.disable`、`safe_mode.enter -> safe_mode.exit`；lookup / flatten tests 存在 | production compensation 执行由 Runtime/Recovery 裁定，services 只提供执行面与 hint |
| ServiceConfigAdapter | 已覆盖 | 从 `RuntimePolicySnapshot` + `BuildProfileManifest` 派生 `ServicePolicyView`，不新增 services 顶层 runtime policy key | 新 services 参数需先改 profiles contract，不应私自扩 schema |
| ServiceHealthProbe | 已覆盖 | 实现 `infra::IHealthProbe`，汇总 circuit、adapter readiness、queue、system snapshot、audit/metrics/trace degraded | app live composition 未注册 services health probe 到 production health hot path |
| Observability bridges | 基本覆盖 | Audit、Metrics、Trace bridge 均存在；audit/metrics/trace integration tests 全绿；trace 支持 facade/lane/adapter/external span | production infra sinks 未接入；subscription trace 缺口；installed trace/metrics/audit 未证明 |
| 测试与 discoverability | 已覆盖到 L2 | unit / integration CMake 注册、top-level integration 聚合、CMake Tools 测试全绿 | L3 app-binary 只证明 runtime composition 可调用 tools default path；L4-L6 未证明 |

### 9.4 关联模块链路判断

| 链路 | 是否打通 | 当前可信层级 | 判断 |
|---|---|---:|---|
| Tools -> Services public ABI | 部分打通 | L1 / L2 | `BuiltinExecutorLane` 可调用注入的 `IExecutionService` / `IDataService`；`ToolServiceBridge` 可构造 service request；full-business-chain fixture 通过 loopback services |
| Runtime / app -> Services backend | 未完全打通 | L0 / L2 boundary | `apps/runtime_support` 链接 `dasall_services`，但 `compose_runtime_tool_manager()` 未实例化 `ServiceFacade`，传空 services 依赖后回落 tools default service |
| Services -> Profiles | 基本打通 | L1 / L2 | `ServiceConfigAdapter` 消费 `RuntimePolicySnapshot` / `BuildProfileManifest`；profile integration test 覆盖 desktop / edge policy projection |
| Services -> Infra health | 模块内打通，production 未证明 | L1 / L2 | `ServiceHealthProbe` 输出 `infra::HealthSnapshot`；health integration 通过 | production composition 未注册 probe |
| Services -> Infra audit / metrics / trace | 模块内打通，production 未证明 | L1 / L2 | bridges 与 integration tests 证明可写 audit、metric、trace span | runtime_support 未注入 production provider/logger/tracer |
| Services -> Platform HAL | seam 可用，production 未证明 | L1 | `LocalPlatformAdapter` 需要 `platform_hal_enabled` 和 `invoke_platform` callback | 未看到 production handler 接 platform HAL concrete provider |
| Services -> Local / Remote service endpoint | seam 可用，production 未证明 | L1 / L2 fixture | `LocalServiceAdapter` / `RemoteServiceAdapter` 可通过 loopback handler、timeout、availability tests | 真实 endpoint discovery、connection、auth、retry / timeout budget 未证明 |
| Services -> Runtime Recovery | 边界正确 | L0 / L1 | Services 只执行 `compensate()` 与产出 compensation hints，不决定是否恢复 | 符合 ADR-007；后续不应把 recovery admission 放入 services |
| Services -> Memory / Knowledge / LLM | 无直接生产依赖 | L0 | 未发现 services 直接拉 Memory / Knowledge / LLM 私有实现 | 相邻链路应继续经 Runtime / Tools / contracts supporting objects |
| installed / package / qemu | 证据不足 | L0 / L3 partial | build tree tests 和 daemon composition 通过 | 未证明 installed tools -> concrete services -> external backend 正向链路 |

### 9.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 代码证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| CAPSRV-GAP-001 | High | runtime production live composition 未注入真实 `ServiceFacade` / `IExecutionService` / `IDataService` 后端 | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 构造 `BuiltinExecutorLaneDependencies{ .execution_service = nullptr, .data_service = nullptr }`；`tools/src/execution/BuiltinExecutorLane.cpp` 在依赖为空时使用 `DefaultExecutionService` / `DefaultDataService` | `DaemonRuntimeLiveDependencyCompositionTest` 通过只能证明 app-composed ToolManager 可调用 default data service，不能证明 production tools -> services facade -> adapter/backend 链路闭合 | 在 runtime_support 增加 services composition root / factory，或显式标记 default service 只作 preview fallback，并补 production bridge test |
| CAPSRV-GAP-002 | High | `DataQueryLane` cache hit result triad 与公共结果语义不一致 | `DataQueryLane::query()` cache hit 分支返回 `.code = contracts::ResultCode::ToolExecutionFailed` 且 `.error = std::nullopt`；`ServiceTypes` success/consistency 约定要求 success 为 `code=nullopt,error=nullopt` | cached data query 会被 `succeeded()` / `has_consistent_values()` 判为失败或不一致，进而污染 ToolResult 投影、metrics outcome 和 contract gate | cache hit 应返回 `code=std::nullopt,error=std::nullopt`，并新增 cache-hit triad regression |
| CAPSRV-GAP-003 | Medium / High | subscription public ABI trace 链不完整 | `ServiceFacade::subscribe()` 直接调用 `subscribe_execution_state`，没有 `start_facade_span()`；`ExecutionSubscriptionHub` 只有 metrics_bridge，没有 trace_bridge | 详设要求 `ServiceFacade -> lane -> adapter/external` 追踪链；状态订阅 public 方法在 trace integration 中没有同等可观测性 | 给 subscription 增加 facade span、结果 complete 语义和必要的 hub/lane trace 点 |
| CAPSRV-GAP-004 | Medium / High | adapter concrete backend 仍是 callback seam，生产 endpoint / HAL 接线未证明 | `LocalPlatformAdapter` 无 `invoke_platform` 时返回 `platform_hal_unbound`；`LocalServiceAdapter` 无 handler 返回 `local_service_unbound`；`RemoteServiceAdapter` 无 handler 返回 `remote_service_stub` | services lane / router / bridge 可运行，但真实系统能力执行仍取决于外部 handler 注入；不能宣称 platform/remote 后端完成 | 建立 production adapter registry、platform HAL handler、local/remote endpoint resolver 和 health probe 回写 |
| CAPSRV-GAP-005 | Medium | capability snapshot / adapter candidate 仍偏静态依赖，production dynamic registry 未闭合 | `ExecutionCommandLaneDependencies` / `DataQueryLaneDependencies` 均持有单个 `CapabilitySnapshotView` 和 candidate 列表；Data 路由用 `request.dataset` 对比该 snapshot | 多 capability / 多 dataset / adapter hot update 场景需要动态 snapshot registry；当前 fixture 容易掩盖生产 catalog 变化 | 引入 services-side snapshot / candidate provider 或在 runtime composition 明确生成动态视图，并补多 capability tests |
| CAPSRV-GAP-006 | Medium | production observability / health sinks 未接入 app live composition | Audit/Metrics/Trace/Health bridge 代码与测试存在；`RuntimeLiveDependencyComposition` 未实例化 services bridges、metrics provider、tracer、audit logger 或 health probe registration | L1/L2 可观测测试不能外推为 installed production audit/metrics/trace/health hot path | 在 services factory / runtime_support 注入 infra sinks，并补 production observability / health composition test |
| CAPSRV-GAP-007 | Medium | caller domain allowlist 与 trust 约束在 services router 内未独立生效 | `ServicePolicyView` 有 allowed domains / trust 相关派生；`AdapterRouter::select_adapter()` 输入没有 caller domain，`ServiceCallContext` 也没有 caller_domain | 当前安全边界依赖 Tools PolicyGate；如果未来出现非 Tools caller，services 无法按 caller domain 二次 fail-closed | 明确 caller-domain owner：继续由 Tools/Access 负责并加 boundary guard，或扩展 ServiceCallContext / router policy 后补兼容测试 |
| CAPSRV-GAP-008 | Low / Medium | installed / qemu / release runner / soak 证据缺失 | 本轮只执行 build-tree CMake Tools CTest；未执行 installed package、qemu、真实 external backend、long-running subscription/adapter soak | 不能从 services L2 fixture 全绿外推为生产可用性、长期稳定性或 release runner 通过 | 增加 installed tools->services positive probe、qemu/autopkgtest、adapter timeout/overflow soak 与 chaos gate |

### 9.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| CAPSRV-FIX-001 | Todo | 接入 runtime production services backend | 新增 services composition root / factory，或在 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 实例化 `ServiceFacade`、lanes、router、bridge、adapters 并注入 `BuiltinExecutorLane` | 新增 `ToolServicesProductionBridgeIntegrationTest` 或扩展 `DaemonRuntimeLiveDependencyCompositionTest`，断言 payload / trace / evidence 来自 services concrete path 而非 tools default service | `RunCtest_CMakeTools(tests=["ToolServicesSmokeIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest","FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest"])` | app live composition 的 `agent.dataset` / action / diagnose 不再回落 tools default services |
| CAPSRV-FIX-002 | Todo | 修复 DataQuery cache hit result triad | 更新 `services/src/data/DataQueryLane.cpp` cache hit 分支，把成功 cache hit 返回为 `code=nullopt,error=nullopt`，保留 `from_cache=true` 与 rows_json | 扩展 `DataQueryLaneTest`，新增 cache hit `has_consistent_values()` / `succeeded()` 断言；保留 `ServiceResultSemanticsContractTest` 和 `BuiltinExecutorLaneResultCodeTest` | `RunCtest_CMakeTools(tests=["DataQueryLaneTest","ServiceResultSemanticsContractTest","BuiltinExecutorLaneResultCodeTest"])` | cache 命中不会被误投影为失败或 triad inconsistent |
| CAPSRV-FIX-003 | Todo | 补齐 subscription trace 链 | 更新 `ServiceFacade::subscribe()` 使用 `ServiceTraceBridge::start_facade_span()`；给 `ServiceTraceBridge` 增加 `ExecutionSubscriptionResult` complete overload，必要时给 `ExecutionSubscriptionHub` 增加 trace 注入 | 扩展 `ServiceFacadeTest`、`ServiceTraceBridgeTest`、`CapabilityServicesTraceIntegrationTest`，新增 subscribe span 和 overflow/error span 断言 | `RunCtest_CMakeTools(tests=["ServiceFacadeTest","ServiceTraceBridgeTest","CapabilityServicesTraceIntegrationTest","CapabilityServicesFailureIntegrationTest"])` | execute/query/catalog/subscribe public ABI 都有一致 facade trace 证据 |
| CAPSRV-FIX-004 | Todo | 建立 production adapter registry / backend handlers | 接入 platform HAL handler、local service endpoint handler、remote service endpoint handler；把 adapter availability / trust / route classes 与 health probe / profile 派生闭合 | 新增 `CapabilityServicesProductionAdapterIntegrationTest`，覆盖 local_platform enabled/disabled、local_service unavailable、remote timeout、fallback forbidden | `RunCtest_CMakeTools(tests=["AdapterRouterTest","LocalPlatformAdapterTest","LocalServiceAdapterTest","RemoteServiceAdapterTest","CapabilityServicesProfileIntegrationTest","CapabilityServicesProductionAdapterIntegrationTest"])` | adapter route 不再只靠 loopback fixture，可证明生产 handler 注入和 fail-closed |
| CAPSRV-FIX-005 | Todo | 收敛动态 capability snapshot / candidate provider | 为 execution/data lanes 引入 snapshot/candidate provider 或在 services factory 中按 registry 动态生成视图，避免单 snapshot 固化多 dataset 场景 | 新增多 capability / 多 dataset route tests，覆盖 snapshot mismatch、hot update、availability unknown fail-closed | `RunCtest_CMakeTools(tests=["AdapterRouterTest","DataQueryLaneTest","ExecutionCommandLaneTest","CapabilityServicesProfileIntegrationTest"])` | 多 capability / dataset 下不需要复制 lane 或依赖单 fixture snapshot |
| CAPSRV-FIX-006 | Todo | 接入 production observability 与 health sinks | 在 services composition root / runtime_support 注入 `ServiceAuditBridge`、`ServiceMetricsBridge`、`ServiceTraceBridge`、`ServiceHealthProbe` 所需 infra logger/provider/tracer/signal provider | 新增 `CapabilityServicesProductionObservabilityIntegrationTest` 或扩展 audit/metrics/trace/health integration，断言 production-composed path 发出真实 sink event | `RunCtest_CMakeTools(tests=["CapabilityServicesAuditIntegrationTest","CapabilityServicesMetricsIntegrationTest","CapabilityServicesTraceIntegrationTest","CapabilityServicesHealthIntegrationTest","CapabilityServicesProductionObservabilityIntegrationTest"])` | services production path 有 audit/metric/trace/health 可观测证据，而非仅 fixture |
| CAPSRV-FIX-007 | Todo | 明确 caller-domain trust owner | 二选一：A. 扩展 `ServiceCallContext` / router 以消费 caller domain allowlist；B. 明确 caller-domain owner 固定在 Tools / Access PolicyGate，并删除或降级 services 内未消费字段 | A 新增 services router caller domain tests；B 新增 boundary guard，证明非 Tools caller 必须先过上游 policy | A: `RunCtest_CMakeTools(tests=["AdapterRouterTest","ToolPolicyGateTest","ToolServicesSmokeIntegrationTest"])`；B: `rg -n "caller_domain_allowlist|allowed_tool_domains|ServiceCallContext" services tools docs` | caller domain 约束不会停留在“字段存在但无 owner”的状态 |
| CAPSRV-FIX-008 | Todo | 建立 installed / release services 证据 | 增加 installed `dasall run` / tool prompt positive path，显式证明 tools -> services -> adapter/backend；增加 qemu/autopkgtest 与 subscription/adapter soak gate | package smoke 或 qemu gate 记录 service trace / audit / payload evidence；long-running subscription overflow / remote timeout 稳定可复验 | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>`；installed services-positive 命令需在任务中固化 | services 从 L2 fixture 提升到 L4/L5/L6 evidence，不再仅停留在 build tree |

### 9.7 非缺口但需守住的边界

以下事项当前不应被当成缺口，后续实现时必须继续守住：

1. `IExecutionService`、`IDataService`、`ServiceTypes` 当前 canonical public include 根仍是 `services/include`，不应为了补 production composition 而提前迁入 `contracts/include`。
2. `ServiceFacade` 是 services module-internal composition root；tools 只能依赖 `IExecutionService` / `IDataService` 和 supporting objects，不应直接 include `ServiceFacade`。
3. `SystemSnapshotLane`、`ExecutionSubscriptionHub`、`ServiceHealthProbe` 继续 internal-only；健康跨模块 ABI 继续使用 `infra::IHealthProbe` / `infra::HealthSnapshot`。
4. `ServiceConfigAdapter` 只消费 `RuntimePolicySnapshot` 与 `BuildProfileManifest` 派生 internal `ServicePolicyView`；新增 services 配置必须先改 profiles contract。
5. Services 不拥有 Recovery admission；`compensate()` 是执行面，是否触发补偿仍由 Runtime / RecoveryManager 裁定，符合 ADR-007。
6. Services 不直接拥有 Memory / Knowledge / LLM；数据、执行和外部能力访问仍应通过 Tools / Runtime / contracts supporting objects 串联。
7. Adapter loopback fixture 继续只存在于 `tests/mocks/include`，不应变成 production-only loopback adapter。

### 9.8 本轮复验命令与结果

本轮通过 VS Code CMake Tools 执行以下聚焦 CTest，全部通过：

```text
ServiceHeaderLayoutTest
ServiceContextBuilderTest
ServiceFacadeTest
AdapterRouterTest
AdapterBridgeTest
LocalPlatformAdapterTest
LocalServiceAdapterTest
RemoteServiceAdapterTest
ExecutionCommandLaneTest
ExecutionQueryLaneTest
ExecutionSubscriptionHubTest
ExecutionDiagnoseServiceTest
DataProjectionCacheTest
DataQueryLaneTest
ResultMapperTest
ServiceAuditBridgeTest
ServiceMetricsBridgeTest
ServiceTraceBridgeTest
ServiceConfigAdapterTest
ServiceHealthProbeTest
SystemSnapshotLaneTest
CapabilityServicesSmokeIntegrationTest
CapabilityServicesFailureIntegrationTest
CapabilityServicesProfileIntegrationTest
CapabilityServicesAuditIntegrationTest
CapabilityServicesMetricsIntegrationTest
CapabilityServicesTraceIntegrationTest
CapabilityServicesHealthIntegrationTest
DaemonRuntimeLiveDependencyCompositionTest
BuiltinExecutorLaneResultCodeTest
ServiceResultSemanticsContractTest
```

该结果可采信为：

1. Services public ABI、facade、context builder、router/bridge/adapters、execution/data/system lanes、result mapping、config/health、audit/metrics/trace 和 loopback integration 具有 L1 / L2 正向证据。
2. `DaemonRuntimeLiveDependencyCompositionTest` 证明 app-composed `ToolManager` 可调用 `agent.dataset` 并产出 ToolResult / Observation / ObservationDigest；但结合源码可知该路径仍可由 tools default data service 完成，不能外推为 concrete services backend 已注入。
3. `ServiceResultSemanticsContractTest` 与 `BuiltinExecutorLaneResultCodeTest` 证明公共 triad gate 存在；但 `DataQueryLane` cache hit 分支没有被现有测试捕获，仍需单独补 regression。

建议后续复验命令：

```text
RunCtest_CMakeTools(tests=[
  "ServiceHeaderLayoutTest",
  "ServiceContextBuilderTest",
  "ServiceFacadeTest",
  "AdapterRouterTest",
  "AdapterBridgeTest",
  "LocalPlatformAdapterTest",
  "LocalServiceAdapterTest",
  "RemoteServiceAdapterTest",
  "ExecutionCommandLaneTest",
  "ExecutionQueryLaneTest",
  "ExecutionSubscriptionHubTest",
  "ExecutionDiagnoseServiceTest",
  "DataProjectionCacheTest",
  "DataQueryLaneTest",
  "ResultMapperTest",
  "ServiceAuditBridgeTest",
  "ServiceMetricsBridgeTest",
  "ServiceTraceBridgeTest",
  "ServiceConfigAdapterTest",
  "ServiceHealthProbeTest",
  "SystemSnapshotLaneTest",
  "CapabilityServicesSmokeIntegrationTest",
  "CapabilityServicesFailureIntegrationTest",
  "CapabilityServicesProfileIntegrationTest",
  "CapabilityServicesAuditIntegrationTest",
  "CapabilityServicesMetricsIntegrationTest",
  "CapabilityServicesTraceIntegrationTest",
  "CapabilityServicesHealthIntegrationTest",
  "DaemonRuntimeLiveDependencyCompositionTest",
  "BuiltinExecutorLaneResultCodeTest",
  "ServiceResultSemanticsContractTest"
])
```

production / installed / release 证据仍需单独执行并记录：

```bash
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>
```

### 9.9 当前章节结论

Capability Services 子系统的当前实现覆盖度已经很高，主体功能不再是缺口。当前更准确的冻结口径为：

1. Services 主体：public ABI、`ServiceFacade`、execution/data/system lanes、adapter/router/bridge、mapping、compensation、config、health、audit/metrics/trace 与测试拓扑基本达到 L1 / L2。
2. Tools 链路：注入式 Tools -> Services loopback 已证明；但 runtime production live composition 仍未注入真实 services backend，不能宣称 installed production tools -> services -> adapter/backend 链路完全闭合。
3. 源码缺陷：`DataQueryLane` cache hit result triad 与公共成功约定不一致；subscription public ABI trace 覆盖不完整。
4. Backend / production：adapters 是可注入 seam，真实 platform/local/remote endpoint、dynamic registry、production observability/health sinks 与 installed/qemu/soak 证据仍需补。
5. 查漏补缺优先级：先修 `CAPSRV-GAP-001` / `CAPSRV-GAP-002` / `CAPSRV-GAP-003`，再收口 `CAPSRV-GAP-004` / `CAPSRV-GAP-005` / `CAPSRV-GAP-006` / `CAPSRV-GAP-007`，最后通过 `CAPSRV-GAP-008` 提升 installed、release 与长稳态可信度。

## 10. Runtime 子系统查漏补缺

### 10.1 检查范围与依据

检查范围：`runtime/include/*`、`runtime/src/*`、`runtime/CMakeLists.txt`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、`tests/unit/runtime/*`、`tests/integration/agent_loop/*`、`docs/todos/runtime/DASALL_runtime子系统专项TODO.md`、`docs/ssot/BusinessChainIntegrationMatrix.md`、`docs/ssot/SystemIntegrationGateMatrix.md`。

本章口径：本轮未重新执行 Runtime CTest；结论基于当前源码、专项 TODO、SSOT 与 worklog 静态核对。`Runtime` 详设中的 implementation-not-ready 是历史 baseline；当前实现已明显推进，但证据层级仍必须拆开记录。

### 10.2 总体结论

结论：`runtime/*` 已经不是 placeholder-only。当前代码已有 `AgentFacade`、`AgentOrchestrator`、`RuntimeDependencySet`、FSM / TransitionGuardTable、`BudgetController`、`CheckpointManager`、`RecoveryManager`、`SessionManager`、`Scheduler`、`SafeModeController`、runtime telemetry / event / health / maintenance 组件，并由 daemon / gateway production composition 创建 live dependency set。

当前不能写成“Runtime 详设全部 production-ready”。更准确状态是：

1. 控制面主体与多条 build-tree integration path 已有 L0 / L1 / L2 证据。
2. installed `dasall run` 当前主要证明 memory context -> production LLM direct path -> memory writeback / checkpoint / session 的 L4 local 主功能，不证明 cognition/tools/recovery full path 是 installed 必经。
3. Checkpoint / Session / Resume 的对象与校验存在，但 store 仍偏进程内，resume 多处走 synthetic request，不能外推 durable replay。
4. timeout / cancellation / scheduler / telemetry / health / maintenance 组件存在，但主链扣减、deadline 传播、worker 取消、production sink 与 health hot path 接线仍不足。

### 10.3 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| RT-GAP-001 | High | production direct LLM path 与 cognition/tools/recovery full path 证据分层不清 | `AgentOrchestrator` 有 `has_production_llm_direct_path()` 与 `required-live-baseline` 判定；BusinessChain SSOT 将 BC-05 写为 L2/L4 partial | installed 主功能容易被误写成完整 runtime 设计链路已闭合 | 将 direct path、cognition-first path、tool path、recovery path 的 gate / diagnostics marker 分开 |
| RT-GAP-002 | 已闭合 | terminal cognition decision 到 Runtime Responding 映射已闭合 | 2026-05-14 已更新 `AgentOrchestrator` live cognition path：`DirectResponse` / `ConvergeSafe` 会进入 `Responding` 并调用 `IResponseBuilder.build()`；`NoDecision` 保持 fail-fast；`CognitionRuntimeInteractionContractTest` 已补齐三类 fixture | terminal cognition decision 不再被误判为失败或降级 | 2026-05-14 已由 `COG-FIX-001` / `RT-FIX-002` 完成，后续只保留 runtime 证据分层与 durable/resume 等剩余议题 |
| RT-GAP-003 | High | durable checkpoint / resume / replay 证据不足 | CheckpointManager / SessionManager 存在，但 in-memory store 与 synthetic resume 语义仍是边界 | 不能宣称 durable recovery 或 workflow replay-safe | 接入持久 store、golden replay、waiting external action replay、installed resume evidence |
| RT-GAP-004 | Medium / High | deadline / cancellation 未贯通 LLM / tool / worker hot path | `CancellationToken` 存在；orchestrator 调用链中 deadline 传播和 worker check 不充分 | 超时或 cancel 可能无法及时停止外部调用 | 将 runtime budget/deadline 绑定 LLM request、ToolInvocationContext、SchedulerTicket 和 waiting state |
| RT-GAP-005 | Medium | Runtime telemetry/event/health/maintenance 未进入 production hot path | 独立组件存在；daemon/readiness 主要消费 runtime readiness 字符串 | 可观测与健康证据不能支撑生产排障 | 在 `AgentFacade` / `AgentOrchestrator` / app composition 注入 runtime sinks 与 health probe |
| RT-GAP-006 | Medium | Knowledge unavailable / optional degraded semantics 仍需 runtime owner 统一输出 | Gate-INT-06 要求 required/optional ports；Knowledge installed asset probe 另有边界 | optional backend 缺失可能被误判为 runtime fatal 或误报 ready | 固化 degraded reason、readiness 字段、AgentResult evidence 与 regression tests |
| RT-GAP-007 | Medium | Scheduler / background worker 仍偏同步模型 | Scheduler 对象存在，但主链调用 tool/LLM 多为同步 | 并发、backpressure、maintenance 与 recovery thread 设计无法由当前证据证明 | 建立最小 worker/bulkhead 执行模型或明确 v1 synchronous runtime 口径 |
| RT-GAP-008 | Medium | app-binary / installed / qemu Runtime full path 证据不足 | L4 local run 证明 direct LLM；Gate-INT-10 与 packaging gate 另分层 | 不能外推 release runner 或 qemu | 增加 app-binary cognition-positive / tool-positive / recovery-positive probes |

### 10.4 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| RT-FIX-001 | Todo | 固化 runtime path evidence 分层 | 更新 `AgentOrchestrator` diagnostics / metadata，区分 `direct_llm`、`cognition_first`、`tool_positive`、`recovery_positive` path；更新 SSOT | 扩展 `RuntimeUnaryIntegrationTest`、`FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest`，断言 path marker 不混写 | `RunCtest_CMakeTools(tests=["RuntimeUnaryIntegrationTest","FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest"])` | direct path 成功不再被文档或测试外推为 cognition/tools/recovery full path |
| RT-FIX-002 | Done | 补齐 terminal cognition decision 映射 | 已更新 `runtime/src/AgentOrchestrator.cpp`，处理 `DirectResponse` / `ConvergeSafe` / `NoDecision`，调用 `IResponseBuilder.build()` 并保持 recovery owner 边界 | 已扩展 `CognitionRuntimeInteractionContractTest`，补齐 DirectResponse、ConvergeSafe、NoDecision fixtures 与 response builder probe | `cmake --build build-ci --target dasall_cognition_runtime_interaction_contract_integration_test && ctest --test-dir build-ci -R "CognitionRuntimeInteractionContractTest" --output-on-failure` | 2026-05-14 已验收通过：五类 decision kind 均有第一跳断言，失败原因可审计 |
| RT-FIX-003 | Todo | 建立 durable checkpoint / resume gate | 为 checkpoint/session 接入持久 store seam；`continue_from_checkpoint()` 支持 replay waiting external action / tool observation；保留 schema/version guard | 新增 `RuntimeDurableResumeIntegrationTest`、扩展 `RuntimeCheckpointReplayRegressionTest` | `RunCtest_CMakeTools(tests=["RuntimeResumeIntegrationTest","RuntimeCheckpointReplayRegressionTest","RuntimeDurableResumeIntegrationTest"])` | 进程重启后可恢复 waiting checkpoint，schema/version mismatch fail-closed |
| RT-FIX-004 | Todo | 贯通 deadline / cancellation | 将 `CancellationToken` 绑定 BudgetController deadline、LLM request、ToolInvocationContext、SchedulerTicket；调用前后检查取消 | `CancellationTokenTest`、`RuntimeCancellationPropagationIntegrationTest`、tool/LLM timeout fixture | `RunCtest_CMakeTools(tests=["CancellationTokenTest","RuntimeCancellationPropagationIntegrationTest","RuntimeRequiredOptionalPortsIntegrationTest"])` | cancel/timeout 能终止或标记外部调用，late result 不污染 session |
| RT-FIX-005 | Todo | 接入 runtime production observability / health | 在 `RuntimeLiveDependencyComposition` / daemon / gateway 注入 telemetry/event/health/maintenance sinks；AgentOrchestrator 发 transition/budget/recovery/safe-mode events | `RuntimeHealthMaintenanceIntegrationTest`、`RuntimeTelemetryBridgeTest`、daemon readiness integration | `RunCtest_CMakeTools(tests=["RuntimeTelemetryBridgeTest","RuntimeHealthMaintenanceIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest"])` | runtime production path 有可观测 sink 和 health snapshot，不只依赖 readiness 字符串 |
| RT-FIX-006 | Todo | 建立 runtime L3/L4/L5 full-path evidence | 增加 app-binary / installed probes，分别覆盖 direct LLM、cognition-positive、tool-positive、recovery-negative/positive | Gate-INT-10 app-binary smoke、package smoke、BusinessChain matrix 回写 | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | BC-05 / BC-06 / BC-11 / BC-15 的证据层级可分开提升，不再互相冒充 |

### 10.5 建议复验命令

```bash
ctest --test-dir build-ci --output-on-failure -R "Runtime(ControlPlane|Unary|Resume|Checkpoint|Profile|SafeMode|Health|RequiredOptional|Evidence|Recovery|Cancellation)"
```

```bash
rg -n "required-live-baseline|has_production_llm_direct_path|llm_manager->generate|cognition_engine->decide|tool_manager->invoke|response_builder->build|RecoveryManager|CancellationToken|RuntimeTelemetryBridge|RuntimeHealthProbe" runtime apps/runtime_support apps/daemon apps/gateway docs/ssot
```

### 10.6 当前章节结论

Runtime 当前缺的不是控制面骨架，而是生产路径、完整设计路径、恢复可靠性、取消超时、可观测健康和 release evidence 的分层闭合。优先顺序为 `RT-GAP-001` / `RT-GAP-002` / `RT-GAP-003` / `RT-GAP-004`，再推进 `RT-GAP-005` / `RT-GAP-006` / `RT-GAP-007` / `RT-GAP-008`。

### 10.7 runtime_support / app live composition 共享组合根补充

#### 10.7.1 检查范围与依据

检查范围：`apps/runtime_support/include/RuntimeLiveDependencyComposition.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`apps/runtime_support/CMakeLists.txt`、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、`tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/multi_agent/*`、`docs/todos/runtime/DASALL_runtime_support组件专项TODO.md`。

本节口径：`apps/runtime_support` 不是独立业务子系统，而是 daemon / gateway 共用的 app-level runtime live composition root。结论必须同时守住三条边界：一，owner 仍在 app binary `main.cpp`；二，helper 只负责依赖装配与 seam 选择，不是第二个 orchestrator / scheduler / recovery owner；三，build-tree focused 绿灯不能外推 installed / qemu / release-ready。

#### 10.7.2 总体结论

结论：`compose_minimal_live_dependency_set()` 已把 daemon / gateway 的 runtime dependency owner 收敛为共享 helper，不能再把 `apps/runtime_support` 写成“尚未存在”或“只靠空 `RuntimeDependencySet` 占位”的历史口径。

当前更准确的状态是：

1. `apps/runtime_support` 已提供 public helper、options、fail-closed result surface，并被 daemon / gateway entry 真实调用。
2. helper 已装配 SQLite memory baseline、production LLM manager、cognition engine、response builder、minimal `ToolManager`、typed multi-agent seam，以及 optional knowledge service seam。
3. `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` 与 multi-agent focused tests 证明 build-tree 下的 owner、install-layout usage 和 required live baseline 已落盘。
4. 当前不能宣称 production composition 已全部闭合：tool path 仍回落 default service，observability / health sinks 未接入，knowledge installed positive path 未闭合，installed / qemu / release evidence 仍不足。

#### 10.7.3 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| RTSUP-GAP-001 | High | production tool path 仍未注入真实 services backend | `RuntimeLiveDependencyComposition` 仍向 `BuiltinExecutorLaneDependencies` 传 `execution_service = nullptr`、`data_service = nullptr`；`DaemonRuntimeLiveDependencyCompositionTest` 只能证明 default service path | 不能宣称 app live composition 已闭合 tools -> services -> adapter/backend 主链 | 在 composition root 中实例化真实 `ServiceFacade` / execution/data lanes，或明确 default service 只作 preview fallback |
| RTSUP-GAP-002 | High | production observability / health sinks 未注入 shared helper | helper 当前未统一注入 runtime/tools/services 所需 infra logger、audit、metrics、trace、health provider | build-tree 局部 observability 测试不能外推 production hot path | 增加 runtime_support 专用 observability/health composition layer，并扩展 focused production composition tests |
| RTSUP-GAP-003 | Medium / High | knowledge optional degraded semantics 与 installed positive evidence 仍未闭合 | helper 已调用 installed asset knowledge factory，但既有 probe 仍有 red path；当前更多是 degrade marker 而非 installed positive proof | optional port 容易被误写成 ready 或 fatal，readiness / evidence 分层不稳定 | 固化 knowledge unavailable reason、ready/degraded marker、positive probe 与 package smoke 口径 |
| RTSUP-GAP-004 | Medium | owner/fail-closed/evidence marker regression matrix 不完整 | 当前 focused tests 证明 positive path，但 required port 缺失、daemon/gateway 对称性、evidence marker 分层尚未系统锁定 | 后续修改 helper 容易把 owner 边界、optional/required 口径或 diagnostics marker 写回历史状态 | 为 shared helper 增加 required-missing、gateway symmetry、marker stratification regression tests |
| RTSUP-GAP-005 | Medium | installed / qemu / release-preflight 证据不足 | 当前主要是 build-tree focused tests 与部分 installed local evidence；未见单独 composition gate | 不能从 app-binary / local package 绿灯外推 release-runner 或 qemu ready | 建立 package / qemu / release-preflight composition matrix，并把结果回写专项 TODO / 总记录 / SSOT |

#### 10.7.4 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| RTSUP-FIX-001 | Todo | 接入 runtime production services backend | 更新 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`，为 `BuiltinExecutorLane` 注入 concrete `IExecutionService` / `IDataService` 或 services facade composition | 新增 `ToolServicesProductionBridgeIntegrationTest` 或扩展 daemon/gateway composition tests，断言 payload/evidence 来自 services concrete path | `RunCtest_CMakeTools(tests=["ToolServicesSmokeIntegrationTest","ToolServicesProductionBridgeIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])` | helper 不再依赖 tools 内部 default service 冒充 production backend |
| RTSUP-FIX-002 | Todo | 接入 production observability 与 health sinks | 在 runtime_support 统一注入 runtime/tools/services 所需 logger、audit、metrics、trace、health provider；保留 fail-open / fail-closed 口径测试 | 新增 `ToolProductionObservabilityIntegrationTest`、`RuntimeProductionHealthCompositionTest` 或等价 composition tests | `RunCtest_CMakeTools(tests=["ToolObservabilityIntegrationTest","ToolProductionObservabilityIntegrationTest","RuntimeHealthMaintenanceIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest"])` | shared helper 组合出的 live path 可发出真实 sink event 与 health snapshot |
| RTSUP-FIX-003 | Todo | 收口 knowledge optional degraded semantics 与 installed positive probe | 补齐 helper 对 knowledge unavailable / degraded / positive 的 marker 和 readiness 语义；必要时扩展 installed asset composition path | 扩展 `KnowledgeInstalledAssetProbeIntegrationTest` 或新增 daemon/gateway installed knowledge composition positive test | `RunCtest_CMakeTools(tests=["KnowledgeInstalledAssetProbeIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])` | knowledge optional port 不再在 fatal / ready / degraded 三种语义之间漂移 |
| RTSUP-FIX-004 | Todo | 建立 owner / fail-closed / marker regression matrix | 为 helper 增加 required ports missing、daemon/gateway symmetry、multi-agent enablement marker、knowledge unavailable marker 的 focused tests | 新增 `RuntimeLiveCompositionFailureMatrixTest`、扩展 daemon/gateway composition tests | `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest","MultiAgentDisabledByProfileIntegrationTest","RuntimeLiveCompositionFailureMatrixTest"])` | 后续回归不会再把 helper 写回空 composition、owner 不清或 marker 混写 |
| RTSUP-FIX-005 | Todo | 建立 installed / qemu / release-preflight composition gate | 将 daemon / gateway 的 shared helper 结果纳入 package smoke / qemu / release-preflight matrix，并回写到专项 TODO / 总记录 / SSOT | package / qemu composition probe、release-preflight matrix | `Build_CMakeTools(target=dasall_gate_int_10)`；`sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | shared helper 的证据层级能从 L2/L3 partial 推进到 L4/L5 候选，而不是长期停留在 focused path |

#### 10.7.5 完整专项 TODO

`apps/runtime_support` 的完整组件级专项 TODO 已单列为：`docs/todos/runtime/DASALL_runtime_support组件专项TODO.md`。

该文档给出：

1. owner / boundary / install layout / required-vs-optional ports 的完整约束清单；
2. 已完成基线与剩余缺口的四分段任务表；
3. 与 `runtime`、`access`、`tools`、`capability services`、`infrastructure`、`packaging` 的 blocker / Gate 映射；
4. `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest`、services/observability/knowledge 相关 gate 的统一验收命令。

#### 10.7.6 当前章节补充结论

`apps/runtime_support` 当前已经解决“谁来在 daemon / gateway 装配 runtime live dependencies”这个 owner 问题；接下来的工程重点不再是继续证明 helper 存在，而是把 `RTSUP-GAP-001` / `RTSUP-GAP-002` / `RTSUP-GAP-003` 这三条 production completeness 缺口真正闭合，再通过 `RTSUP-GAP-004` / `RTSUP-GAP-005` 建立稳定的 regression 与 installed/release 证据。

## 11. Access / apps ingress 查漏补缺

### 11.1 检查范围与依据

检查范围：`access/include/*`、`access/src/*`、`apps/cli/src/*`、`apps/daemon/src/*`、`apps/gateway/src/*`、`tests/unit/access/*`、`tests/integration/access/*`、`docs/todos/access/DASALL_access子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`、`docs/ssot/BusinessChainIntegrationMatrix.md`。

### 11.2 总体结论

Access 不能再沿用详设中的 placeholder-only 历史口径。当前 Access v1 unary focused ingress 已通过 Gate-INT-08；最近 worklog 记录 `ACC-TODO-045~051` 已补齐 production profile/config projection、HMAC ownership token / rotation、policy backend、observability bridge 与 release polish。Access 的剩余缺口集中在 release / installed / multi-instance / streaming / security hardening，而不是基础 core 缺失。

### 11.3 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| ACC-GAP-001 | High | installed package 真实 async receipt 正向路径不足 | BusinessChain 记录 installed status/cancel 只有 missing receipt reject；BC-04 为 L3/L4 partial | 不能宣称 package async ready | 增加 installed submit -> receipt -> status -> cancel/replay 正向 probe |
| ACC-GAP-002 | High | HTTP gateway installed 正向 ingress 证据不足 | GatewayBinaryUnarySmokeTest 是 build-tree app-binary；本轮未见 installed HTTP request | BC-02 只能写 L3，不能外推 L4/L5 | 建立 installed gateway socket/port/security/profile matrix 与 positive/negative tests |
| ACC-GAP-003 | Medium / High | multi-instance receipt / replay authority 未闭合 | `AsyncTaskRegistry` 当前更偏单进程/本地 registry；Access 专项仍把更广 release 作为 residual | 多 daemon / gateway / restart 后 receipt 状态可能不具权威一致性 | 引入 receipt store / authority seam、TTL cleanup、idempotency replay cross-process tests |
| ACC-GAP-004 | Medium | streaming lifecycle 与 shared admission 延后 | Access 详设明确 streaming/WS/MQTT 在 shared lifecycle 未冻结前延后 | 不能把 unary Gate-INT-08 写成 streaming ready | 保持 feature flag / blocked 口径，单列 StreamGateway 设计与 gate |
| ACC-GAP-005 | Medium | release security matrix 仍需 L5/qemu 证据 | 本机 L4 control-plane 可用；qemu/autopkgtest 未执行 | 安全负路径与 package hardening 不能外推 release-runner | 增加 qemu rootless/rootful、socket mode、policy deny、diag disabled、ownership mismatch matrix |

### 11.4 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| ACC-FIX-001 | Todo | 建立 installed async receipt 正向 gate | 为 CLI/daemon package smoke 增加真实 async submit receipt，保留 HMAC ownership token 与 replay cache；必要时补 daemon test hook | installed status/cancel/replay positive + mismatch negative；`DaemonReceiptFlowIntegrationTest` 不回退 | `RunCtest_CMakeTools(tests=["DaemonReceiptFlowIntegrationTest","AccessAsyncReceiptQueryCancelIntegrationTest","FullIntAsyncRecoveryCausalityTest"])`；installed 命令在 deliverable 中固化 | BC-04 从 missing-reject partial 升级为 package positive evidence |
| ACC-FIX-002 | Todo | 补齐 installed HTTP gateway unary evidence | 明确 gateway installed config、listen policy、auth/policy profile；增加 package smoke 中 HTTP positive / backend missing negative | `GatewayBinaryUnarySmokeTest`、installed HTTP smoke、policy deny / readiness negative | `Build_CMakeTools(target=dasall_gate_int_10)`；`sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | HTTP gateway 不再只停留在 build-tree app-binary 证据 |
| ACC-FIX-003 | Todo | 收口 receipt authority / multi-instance 口径 | 新增 receipt store/provider seam 或明确 v1 single-daemon authority；将 TTL cleanup、idempotency、ownership validation 绑定持久/共享策略 | `DaemonReceiptTtlCleanupIntegrationTest`、新增 multi-instance/restart replay test | `RunCtest_CMakeTools(tests=["DaemonReceiptTtlCleanupIntegrationTest","DaemonReceiptFlowIntegrationTest","AccessReceiptAuthorityIntegrationTest"])` | 重启或多实例边界被明确，不能产生“本地 registry 当全局事实”的歧义 |
| ACC-FIX-004 | Todo | 固化 streaming 延后与 feature gate | 更新 Access TODO / SystemIntegrationGateMatrix，明确 unary v1 与 StreamGateway 的 shared lifecycle owner；stream flag 默认关闭 | static wording guard + stream disabled regression | `rg -n "StreamGateway|streaming|WS|MQTT|shared lifecycle|Gate-INT-08" docs/todos/access docs/ssot access apps` | Gate-INT-08 不再被误读为 streaming-ready |
| ACC-FIX-005 | Todo | 建立 access release security matrix | 扩展 packaging/qemu gate，覆盖 socket mode、policy backend unavailable、ownership mismatch、diag deny、default diag disabled、reload denied | access release-preflight tests + qemu/autopkgtest evidence | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | Access v1 security hardening 有 L5 证据，不依赖本机 focused 绿灯外推 |

### 11.5 当前章节结论

Access / apps ingress 当前主体已具备 focused integration 可信度，剩余重点是把 Gate-INT-08 的 build-tree / focused 结论升级为 installed / release-runner 证据，并把 streaming、multi-instance 和 release hardening 继续分账。

## 12. Multi-Agent 子系统查漏补缺

### 12.1 检查范围与依据

检查范围：`multi_agent/include/*`、`multi_agent/src/*`、`runtime/include/RuntimeDependencySet.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`profiles/include/RuntimePolicySnapshot.h`、`profiles/src/RuntimePolicyProvider.cpp`、`tests/integration/multi_agent/*`、`docs/architecture/DASALL_multi_agent子系统详细设计.md`、`docs/ssot/BusinessChainIntegrationMatrix.md`。

### 12.2 总体结论

Multi-Agent 已不是详设早期的 placeholder-only。当前已有 `IMultiAgentCoordinator`、`MultiAgentTypes`、Null / Real coordinator、`MultiAgentRuntimeFold`、runtime dependency slot、profile typed `multi_agent_enabled()`，并有 `MultiAgentDisabledByProfileIntegrationTest`、`MultiAgentCoordinatorPipelineTest`、`MultiAgentRecoveryFoldIntegrationTest` 等 build-tree 证据。

但 SSOT 明确：installed desktop / cloud profiles 仍为 `multi_agent: false`，CLI 没有 multi-agent 独立 surface，当前不能宣称 package-ready 或 profile-enabled runtime-ready。

### 12.3 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| MA-GAP-001 | High | installed profile-enabled positive path 缺失 | BusinessChain BC-17 为 L2/L4 asset partial；installed profiles 禁用 multi_agent | 不能宣称 multi_agent GA / package-ready | 新增受控 profile、feature flag、CLI/API 或 diag surface，建立 installed positive gate |
| MA-GAP-002 | High | Runtime 主链消费 coordinator / fold 的 owner 边界仍需加强 | Runtime live composition 注入 coordinator；fold helper 已有；但 default run 不进入 enabled sidecar | enabled sidecar 可能只在 synthetic tests 成立 | 明确 AgentOrchestrator 调用时机、fallback、recovery fold 与 memory writeback 边界 |
| MA-GAP-003 | Medium | role / worker capability / lease / routing 策略仍是最小实现 | 当前 real coordinator 能产 structured report，但不等于完整 worker orchestration | 复杂 agent 协同能力不能从最小 sidecar 外推 | 分阶段实现 role catalog、worker lease、subtask graph lifecycle、capability routing |
| MA-GAP-004 | Medium | observability / audit / recovery causality 证据不足 | tests 覆盖 recovery fold；production sink / audit chain 不足 | 多 Agent side effect 与 recovery 证据难审计 | 接 runtime / infra sinks，记录 subtask、lease、fold、recovery owner facts |
| MA-GAP-005 | Medium | qemu / soak / chaos 证据缺失 | 现有为 build-tree integration 与 installed disabled asset | 长稳态、worker failure、partial completion 未证明 | 增加 enabled sidecar soak / fault injection gate |

### 12.4 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| MA-FIX-001 | Todo | 设计并落地 multi_agent enabled profile gate | 增加 experimental profile 或 overlay，保持 desktop/cloud default disabled；RuntimePolicyProvider 对 enablement fail-closed | `MultiAgentDisabledByProfileIntegrationTest`、新增 enabled profile validation / installed asset test | `RunCtest_CMakeTools(tests=["MultiAgentDisabledByProfileIntegrationTest","MultiAgentCoordinatorPipelineTest","MultiAgentEnabledProfileIntegrationTest"])` | 默认禁用不回退，受控启用有可复验证据 |
| MA-FIX-002 | Todo | 接入 Runtime enabled sidecar 主链 | 在 Runtime 明确 multi_agent coordinator 调用点、fold-to-runtime-result、recovery owner 和 memory writeback 投影；禁用态继续 Null | `MultiAgentCoordinatorPipelineTest`、`MultiAgentRecoveryFoldIntegrationTest`、runtime unary regression | `RunCtest_CMakeTools(tests=["MultiAgentCoordinatorPipelineTest","MultiAgentRecoveryFoldIntegrationTest","RuntimeUnaryIntegrationTest"])` | enabled sidecar 不绕过 Runtime/RecoveryManager，disabled profile 行为不变 |
| MA-FIX-003 | Todo | 建立 role/capability/lease v1 | 扩展 `MultiAgentTypes` 与 coordinator 内部策略，加入 worker capability、lease timeout、subtask lifecycle | 新增 role routing、lease expiry、partial failure tests | `RunCtest_CMakeTools(tests=["MultiAgentCoordinatorPipelineTest","MultiAgentRoleLeaseIntegrationTest"])` | multi_agent 不再只有 loopback sidecar，具备最小 worker governance |
| MA-FIX-004 | Todo | 接入 multi_agent observability / audit | 增加 telemetry/audit bridge 或复用 runtime event bus，记录 subtask graph、worker report、fold result、recovery request | `MultiAgentObservabilityIntegrationTest`、`MultiAgentRecoveryFoldIntegrationTest` | `RunCtest_CMakeTools(tests=["MultiAgentObservabilityIntegrationTest","MultiAgentRecoveryFoldIntegrationTest"])` | sidecar 协同与恢复因果链可观测，可被 SSOT 归档 |
| MA-FIX-005 | Todo | 建立 installed / qemu enabled evidence | 增加 package profile overlay、CLI/diag surface 或 controlled run prompt，验证 enabled positive path 与 disabled default | package smoke / qemu / fault injection | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | BC-17 可从 L2/L4 asset partial 升级为受控 enabled package evidence |

### 12.5 当前章节结论

Multi-Agent 当前是“build-tree sidecar 协同路径已有、installed 默认禁用且 package-ready 未证明”。后续应先守住 disabled default 和 Runtime owner，再推进 enabled profile、主链消费、角色/租约、观测和 release 证据。

## 13. Infrastructure 子系统查漏补缺

### 13.1 检查范围与依据

检查范围：`infra/include/*`、`infra/src/*`、`docs/todos/infrastructure/*`、`docs/architecture/DASALL_infrastructure子系统详细设计.md`、`docs/architecture/DASALL_infra_secret模块详细设计.md`、`access/include/AccessGatewayFactory.h`、`access/src/AccessGatewayFactory.cpp`、`runtime/include/RuntimeDependencySet.h`、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`tests/unit/infra/*`、`tests/integration/infra/*`、`tests/unit/access/*`、`docs/ssot/SystemIntegrationGateMatrix.md`。

### 13.2 总体结论

Infrastructure 已完成大量接口与组件骨架：logging / audit / tracing / metrics / config / secret / policy / diagnostics / health / watchdog / OTA / plugin 均有专项任务与代码证据。当前缺口不在“有没有 infra 目录”，而在 production sinks across subsystems、diagnostics retained snapshot 证据分层、plugin-to-tools 自动接线、watchdog/event publish 以及 release-runner / soak 证据。

按架构设计，SecretManager / ISecretManager 理论上应是 infra 子系统持有的基础能力：由 InfraServiceFacade 统一初始化和生命周期管理，向上只暴露 consumer-facing 的 `get_secret` / `materialize` / `release` / `rotate` / `revoke` / `inspect` 读、物化、轮换与吊销入口；bootstrap 初始写入仍应停留在 `SecretBootstrapWriter` internal seam，不并入 ISecretManager 公共 ABI。上游 daemon / gateway / runtime_support 不应自建第二条 secret 通道，而应从 app live composition 或 InfraServiceFacade 取得 `std::shared_ptr<ISecretManager>` 后注入 Access、LLM provider auth_ref 解析、OTA / Plugin trust anchor 等真实消费者。

当前事实缺口是：ISecretManager 与 SecretManagerFacade / FileSecretBackend 读链已经存在，但它尚未成为 daemon / gateway 的 live-composed dependency。`RuntimeDependencySet` 没有 secret manager 字段，`compose_minimal_live_dependency_set()` 只装配 memory / LLM / cognition / response_builder / tools / knowledge；daemon / gateway main 也没有给 `DaemonAccessPipelineOptions` / `GatewayAccessPipelineOptions::ownership_secret_manager` 赋值。因此 Access 的 accepted_async receipt ownership secret 只能在测试或显式工厂调用中可用，尚不能宣称 app live composition 已获得 ISecretManager。

### 13.3 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| INF-GAP-001 | High | production observability sinks 未贯通到 Runtime/Cognition/LLM/Tools/Services/Memory/Knowledge/Access | 多子系统有 local bridge / no-op sink；runtime_support 未统一注入 | 局部 telemetry 测试不能外推生产观测 | 建立 InfraObservabilityCompositionV1 与子系统 sink adapter matrix |
| INF-GAP-002 | High | diagnostics retained snapshot 与 default diag disabled 边界需持续分层 | Gate-INT-05 要求 retained snapshot；installed `diag health` default 返回 `diag_disabled` | default disabled 容易被误读为 diagnostics failed 或 ready | 明确 product default、admin enablement、retained snapshot gate 与 installed evidence |
| INF-GAP-003 | Medium / High | plugin lifecycle 未自动驱动 tools extension | Infra plugin manager 与 tools PluginExtensionBridge 各自存在，但自动接线不足 | plugin active 不等于 tool capability visible | 建立 plugin active set -> tools extension bridge adapter |
| INF-GAP-004 | Medium | health/watchdog cadence 与 event publish 最小接口未完全收口 | SystemIntegrationGateMatrix 将 health cadence 作为 post-unary / blocker | health transitions 与 watchdog actions 难以进入统一 evidence | 冻结 health event publish interface，接 Runtime/Access/Services probes |
| INF-GAP-005 | Medium | secret KMS、metrics OTLP、tracing exporter 等 optional backend 未冻结 | 当前更多是 interface / local/fallback 能力 | 不能宣称 production external backend ready | 将 optional backend 降级为 profile-gated tasks，建立 fail-closed 与 package asset gate |
| INF-GAP-006 | Medium | L5 release runner / L6 soak 不足 | 本机 L4 installed 信号存在；qemu/soak 未覆盖 infra failure modes | release hardening 不充分 | 增加 qemu diagnostics/health/plugin/secret/policy smoke 与 soak/chaos |
| INF-GAP-007 | High | SecretManager / ISecretManager 未进入 daemon / gateway app live composition | `ISecretManager`、`SecretManagerFacade`、`FileSecretBackend` 已存在；Access 工厂有 `ownership_secret_manager` seam；但 `RuntimeDependencySet` 无 secret manager 字段，`compose_minimal_live_dependency_set()` 不产出 secret manager，daemon / gateway main 未向 Access options 赋值 | accepted_async ownership secret、LLM provider `secret://` runtime verification、OTA / Plugin trust anchor 等 consumer 不能依赖生产组合根；secret 读链证据停留在 focused / explicit wiring，不能外推为 app live ready | 冻结 SecretManager live composition seam，接入 daemon / gateway Access options，并建立 secret consumer matrix 与 installed/package evidence |

### 13.4 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| INF-FIX-001 | Todo | 建立 production observability composition | 新增 infra composition helper，把 logger/audit/meter/tracer sinks 注入 runtime_support、access、services、tools、llm、memory、knowledge、cognition | 各子系统 production observability integration；sink failure fail-open/fail-closed 口径测试 | `RunCtest_CMakeTools(tests=["ToolObservabilityIntegrationTest","CapabilityServicesTraceIntegrationTest","KnowledgeTelemetryTest","LLMObservabilityBridgeTest"])` | production-composed path 能捕获跨子系统 audit/metric/trace event |
| INF-FIX-002 | Todo | 固化 diagnostics retained snapshot gate | 修正/扩展 `DiagnosticsServiceFacade` retained snapshot round-trip、export/import、default disabled admin boundary | `InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`、daemon diag deny/default disabled tests | `RunCtest_CMakeTools(tests=["InfraDiagnosticsSmokeTest","InfraDiagnosticsIntegrationTest","DaemonDiagDenyIntegrationTest"])` | Gate-INT-05 与 installed default diag disabled 不再混写 |
| INF-FIX-003 | Todo | 接 plugin manager 到 tools extension bridge | 新增 infra/plugin -> tools adapter，消费 active plugin set 与 load/unload event，分发 builtin/MCP/skill assets | `PluginExtensionBridgeTest`、`ToolPluginLifecycleBridgeIntegrationTest`、plugin unload invalidation | `RunCtest_CMakeTools(tests=["PluginExtensionBridgeTest","ToolPluginLifecycleBridgeIntegrationTest"])` | plugin active 后仍经 tools 二次治理，但自动可见性链路闭合 |
| INF-FIX-004 | Todo | 收口 health/watchdog event publish | 冻结最小 `HealthEventPublisher` 或等价接口；Access/Runtime/Services health probes 发 transition event；watchdog 只执行 policy-defined actions | `RuntimeHealthMaintenanceIntegrationTest`、infra health/watchdog tests、daemon readiness regression | `RunCtest_CMakeTools(tests=["RuntimeHealthMaintenanceIntegrationTest","HealthSnapshotTest","DaemonRuntimeLiveDependencyCompositionTest"])` | health cadence 有 owner、有 event、有 backpressure/timeout 语义 |
| INF-FIX-005 | Todo | optional backend profile-gated 接入 | 为 KMS/OTLP/exporter 等建立 profile flag、dependency availability、package asset 与 unavailable error | secret / metrics / tracing backend unavailable and positive tests | `RunCtest_CMakeTools(tests=["SecretTypesTest","ServiceMetricsBridgeTest","ServiceTraceBridgeTest"])` | optional backend 不再以 interface 存在冒充 production backend ready |
| INF-FIX-006 | Todo | 建立 infra release / soak gate | 更新 qemu/autopkgtest 和 soak scripts，覆盖 diagnostics、health/readiness、secret unavailable、plugin safe unload、observability sink failure | qemu package smoke、long-running diagnostics/health soak | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | infra 证据可从 L3/L4 partial 推进到 L5/L6 candidate |
| INF-FIX-007 | Todo | 冻结 SecretManager live composition seam | 在 infra 或 apps/runtime_support 增加最小 production builder，基于 install layout / profile policy 创建 `FileSecretBackend + SecretManagerFacade` 并返回 `std::shared_ptr<ISecretManager>`；保持 ISecretManager ABI 不新增 create/set，bootstrap 写入继续只走 `SecretBootstrapWriter` | `SecretManagerFacadeTest`、`FileSecretBackendTest`、新增 `SecretManagerLiveCompositionTest` 或等价 runtime_support composition test，覆盖 missing root / backend unavailable / positive file-backed manager | `cmake --build build-ci --target dasall_secret_manager_facade_unit_test dasall_file_secret_backend_unit_test dasall_secret_manager_live_composition_unit_test && ctest --test-dir build-ci -R "^(SecretManagerFacadeTest|FileSecretBackendTest|SecretManagerLiveCompositionTest)$" --output-on-failure` | app live composition 能产出可用 ISecretManager；不可用时返回明确 unavailable error，不 silent fallback 成 ready |
| INF-FIX-008 | Todo | 将 ISecretManager 注入 daemon / gateway Access ownership seam | 更新 `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` 或共享 composition result，把 live secret manager 赋给 `DaemonAccessPipelineOptions::ownership_secret_manager` 与 `GatewayAccessPipelineOptions::ownership_secret_manager`；仅当 bootstrap/profile 配置声明 `ownership_token_hmac_secret_ref` 时启用 accepted_async ownership HMAC | 扩展 `AsyncTaskRegistryMissingSecretFailClosedTest`，新增 daemon/gateway composition positive 与 missing-secret fail-closed 回归，覆盖 ownership secret 可物化时 receipt 正常生成 | `cmake --build build-ci --target dasall_access_async_task_registry_missing_secret_fail_closed_unit_test dasall_daemon_access_secret_composition_unit_test dasall_gateway_access_secret_composition_unit_test && ctest --test-dir build-ci -R "^(AsyncTaskRegistryMissingSecretFailClosedTest|DaemonAccessSecretCompositionTest|GatewayAccessSecretCompositionTest)$" --output-on-failure` | daemon/gateway 生产工厂不再只能靠测试手动注入 secret manager；缺 secret 时仍 fail-closed 为 `ownership_secret_unavailable` |
| INF-FIX-009 | Todo | 建立 secret consumer matrix 与 package 证据 | 新增或扩展 SSOT，列明 Access ownership HMAC、LLM provider `auth_ref`、OTA/Plugin trust anchor、Config bootstrap writer 四类 consumer 的 owner、读写路径、profile flag、package asset 与不可外推范围；增加 guard，防止把 bootstrap create/set 并入 ISecretManager | secret consumer matrix doc/test、boundary contract test、installed config secret smoke 与 package asset probe | `rg -n "ISecretManager|SecretBootstrapWriter|ownership_secret_manager|secret://|trust anchor" docs/ssot docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/infrastructure && ctest --test-dir build-ci -R "Secret.*Boundary|Secret.*Package|AsyncTaskRegistryMissingSecretFailClosedTest" --output-on-failure` | 后续评审能区分 bootstrap 写入、consumer 读取、app live composition 与 installed/package evidence，不再把 focused secret 读链外推为 production ready |

### 13.5 当前章节结论

Infrastructure 当前主体接口和多数组件已完成，剩余关键是把 infra 能力从“局部组件可测”推进到“生产组合根可注入、跨子系统可观测、release runner 可复验”。其中 SecretManager / ISecretManager 必须优先按 live composition seam 闭合，避免 Access、LLM、OTA、Plugin 等消费者继续依赖 focused fixture 或手动 wiring 证据。

## 14. Profiles / Platform Linux 查漏补缺

### 14.1 检查范围与依据

检查范围：`profiles/*`、`profiles/include/*`、`profiles/src/*`、`platform/include/*`、`platform/src/linux/*`、`platform/src/arm/hal/*`、`docs/todos/profiles/DASALL_profiles子系统专项TODO.md`、`docs/todos/platform/DASALL_platform_linux组件专项TODO.md`、`tests/unit/profiles/*`、`tests/unit/platform/linux/*`、`tests/integration/platform/linux/*`。

### 14.2 总体结论

Profiles 与 Platform Linux 都已明显超过占位阶段。Profiles `PRF-TODO-001~022` 多数已 Done，Build / Runtime 双平面、ProfileCatalog、BuildProfileResolver、RuntimePolicyProvider、OverlayComposer、LKG、Telemetry、五档 runtime_policy 已落地。Platform Linux `PLAT-LNX-TODO-001~026` 记录为 Done，线程、定时器、队列、文件、网络、IPC、HAL stub、dynamic library loader 与 bootstrap integration 已有证据。

剩余缺口主要在跨子系统 consumer consistency、installed policy mutation、profile enablement 与 runtime wiring 一致性、platform qemu/autopkgtest bootstrap、真实 ARM/HAL driver 边界。

### 14.3 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| PRF-GAP-001 | High | RuntimePolicySnapshot consumer matrix 未形成持续 gate | 多子系统消费 profile：runtime/llm/tools/memory/knowledge/services/access/multi_agent | 新增 profile key 容易只被部分 consumer 采纳 | 建立 profile consumer matrix contract 与 per-subsystem projection regression |
| PRF-GAP-002 | High | profile enablement 与 runtime actual wiring 一致性仍需强约束 | multi_agent profile disabled 有证据；enabled synthetic 与 installed disabled 分层 | enabled_modules 可能被误解为能力已可用 | 对每个 enabled module 建立 “asset -> provider -> runtime injection -> gate” 链路检查 |
| PRF-GAP-003 | Medium | installed runtime_policy mutation / LKG rollback 证据不足 | Profiles 有 LKG / OverlayComposer；installed package smoke 更关注默认资产 | 现场配置损坏或热更新无法证明稳定回退 | 增加 installed config mutation、schema mismatch、LKG rollback smoke |
| PLAT-GAP-001 | Medium / High | platform qemu/autopkgtest bootstrap 证据不足 | Linux bootstrap integration 为 build-tree；L5 未跑 | 不能宣称 platform release-runner ready | 加入 qemu platform bootstrap、socket/file/network/IPC smoke |
| PLAT-GAP-002 | Medium | ARM/HAL 真实驱动边界仍是 stub / availability probe | Platform TODO 明确 HalStubOnly；真实驱动不在本轮 | edge profile 不能宣称真实 HAL ready | 冻结 ARM HAL driver admission 条件、profile flag、package asset 与 fail-closed |
| PLAT-GAP-003 | Low / Medium | platform provider 长稳态与 resource leak soak 不足 | unit/integration 覆盖正负路径；长时间 timer/thread/socket soak 未见 | edge/daemon 长跑稳定性证据不足 | 增加 thread/timer/socket/IPC soak 与 resource cleanup gate |

### 14.4 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| PRF-FIX-001 | Todo | 建立 RuntimePolicy consumer matrix gate | 新增 `docs/ssot/RuntimePolicyConsumerMatrix.md` 或扩展现有 SSOT；每个 profile key 绑定 consumer、fallback、test | `ProfileRuntimePolicySchemaContractTest`、各子系统 profile compatibility tests | `RunCtest_CMakeTools(tests=["ProfileRuntimePolicySchemaContractTest","RuntimeProfileCompatibilityTest","ToolProfileIntegrationTest","MemoryProfileCompatibilityTest","KnowledgeProfileCompatibilityTest","CapabilityServicesProfileIntegrationTest"])` | 新增/变更 policy key 必须同时更新 consumer 与测试 |
| PRF-FIX-002 | Todo | 校准 enabled_modules 与实际 runtime wiring | 新增 consistency checker，验证 enabled module 有 provider factory、dependency injection、readiness/gate；disabled 有 Null/fail-closed | multi_agent、tools、knowledge、memory vector 等 profile enablement regression | `RunCtest_CMakeTools(tests=["MultiAgentDisabledByProfileIntegrationTest","RuntimeRequiredOptionalPortsIntegrationTest","CapabilityServicesProfileIntegrationTest"])` | profile enablement 不再只是 YAML 资产声明 |
| PRF-FIX-003 | Todo | 增加 installed profile mutation / LKG rollback gate | package smoke 中修改 runtime_policy 副本或使用 test overlay，验证 schema reject、LKG fallback、telemetry event | installed config mutation smoke、unit LKG tests | `sh scripts/packaging/pkg_smoke_install.sh --explicit-start-check`；具体 mutation 命令在 deliverable 固化 | 安装态 profile 损坏不会导致 silent unsafe ready |
| PLAT-FIX-001 | Todo | 建立 platform qemu/autopkgtest bootstrap | 将 LinuxPlatformBootstrapIntegrationTest、Unix IPC/socket mode、file/network smoke 加入 qemu/autopkgtest package gate | qemu platform bootstrap / IPC / file/network negative tests | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | platform bootstrap 从 L2 提升到 L5 evidence |
| PLAT-FIX-002 | Todo | 冻结 ARM/HAL driver admission | 更新 platform/design/profile docs，规定真实 HAL driver 接入的 interface、package asset、fallback、safety policy；HalStub 继续默认 | HalAvailabilityBridgeTest、edge profile HAL unavailable/available matrix | `RunCtest_CMakeTools(tests=["HalAvailabilityBridgeTest","LinuxPlatformBootstrapIntegrationTest"])` | edge profile 不再把 HalStubOnly 写成真实驱动 ready |
| PLAT-FIX-003 | Todo | 增加 platform resource soak | 增加 timer/thread/socket/IPC close/reopen soak，记录 fd/thread leak 与 deadline drift | platform soak / stress tests | `ctest --test-dir build-ci --output-on-failure -R "Platform.*Soak|UnixIpcProvider|PosixTimerProvider|PosixThreadProvider"` | 长稳态基础资源行为有可复验证据 |

### 14.5 当前章节结论

Profiles / Platform Linux 当前主要缺口不是实现骨架，而是跨子系统一致性和 release-runner 证据。优先推进 profiles consumer matrix 与 enabled_modules wiring checker，再补 installed mutation、qemu bootstrap 和 HAL admission。

## 15. Contracts / Packaging / Release Handoff 查漏补缺

### 15.1 检查范围与依据

检查范围：`contracts/include/*`、`tests/contract/*`、`docs/todos/contracts/*`、`docs/ssot/*`、`debian/*`、`scripts/packaging/*`、`docs/todos/integration/*`、`docs/worklog/DASALL_开发执行记录.md`、`tests/integration/full_business_chain/*`。

### 15.2 总体结论

Contracts 已承担主链对象冻结与边界回归，Packaging 已有 local installed L4 证据：重新构包、fresh reinstall、daemon active/enabled、CLI ping/readiness/run、LLM origin、default diag disabled 等均在 BusinessChain SSOT 中记录。剩余风险集中在 supporting object admission、WP-04 边界对象透明度、qemu/autopkgtest L5、lintian/artifact SOP、evidence matrix 自动回写与 release/installed 分层不要混写。

### 15.3 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| CONT-GAP-001 | High | WP-04 supporting object 状态需重新清点 | 多个子系统仍有 module-local supporting types；contracts deliverables 分散 | 容易把 module-local 对象过早提升 shared ABI | 清点 WP-04 / contract tests，冻结 admission / rejection / deferred list |
| CONT-GAP-002 | Medium / High | cross-module boundary guard 仍需统一 | cognition/memory/knowledge/tools/services 各自有边界要求，但 guard 分散 | 后续改动可能破坏 ADR-006/007/008 | 建立统一 boundary guard target 或 per-subsystem scripts |
| PKG-GAP-001 | High | qemu/autopkgtest L5 未完成 | `validate_gate_int_10_installed_package_qemu.sh` 存在；SSOT 记录 L4 local，不外推 L5 | 不能宣称 release runner ready | 准备 qemu image/virt-server、执行 autopkgtest、归档 artifact |
| PKG-GAP-002 | Medium / High | packaging evidence matrix 与 worklog 自动回写不足 | 当前证据依赖手工 SSOT/worklog 更新 | release 结论易漂移 | 建立 package smoke -> evidence JSON -> SSOT/worklog template |
| PKG-GAP-003 | Medium | package asset layout 对 knowledge/vector/profile/plugin 等 optional assets 的 negative/positive 证据不足 | installed assets 已有部分断言；sqlite-vss/knowledge normative/plugin extension 等仍缺 | 安装包可能缺资产但 focused tests 不暴露 | 扩展 packaging preflight asset matrix 与 profile-specific asset checks |
| APP-GAP-001 | Medium | CLI / daemon / gateway user-facing surface 与 full business chain matrix 未完全对应 | `dasall --help` 无 multi-agent surface，knowledge refresh/health 等也非完整 installed surface | 用户可用入口与内部能力不一致 | 建立 app command surface matrix：supported / hidden / disabled / admin-only |

### 15.4 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| CONT-FIX-001 | Todo | 清点 WP-04 supporting object admission | 更新 contracts WP-04 TODO / deliverables，列出 ReflectionDecision、RecoveryRequest/Outcome、MultiAgentRequest/WorkerTask 等 shared vs module-local 状态 | `InterfaceCatalogContractTest`、boundary contract tests、static rg guard | `RunCtest_CMakeTools(tests=["InterfaceCatalogContractTest","ADRBoundaryRegressionContractTest","RecoveryBoundaryContractsSmokeTest"])` | supporting object admission 有明确 accepted/deferred/rejected 清单 |
| CONT-FIX-002 | Todo | 建立统一 boundary guard | 新增或聚合 boundary tests，检查 memory 不拥有 prompt/recovery、knowledge 不生成 ContextPacket、tools 不直调 LLM/runtime、multi_agent 不主控 | per-subsystem boundary guard + existing contract smoke | `ctest --test-dir build-ci --output-on-failure -R "Boundary|Contract|InterfaceCatalog"` | ADR-006/007/008 回归不靠人工 rg |
| PKG-FIX-001 | Todo | 执行 qemu/autopkgtest L5 package gate | 准备 qemu/autopkgtest 配置；串联 Gate-INT-10、packaging preflight、dpkg-buildpackage、autopkgtest | qemu installed control-plane、LLM run、diag default、asset matrix | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | BC-16 可记录 L5 release runner / qemu 证据 |
| PKG-FIX-002 | Todo | 自动化 package evidence matrix 回写 | packaging scripts 输出 evidence JSON/Markdown；生成 SSOT/worklog snippet，记录 command、exit、artifact、package versions | validate evidence schema test、worklog snippet dry-run | `RunCtest_CMakeTools(tests=["PackagingPreflightEvidenceTest"] )` 或等价 script test | package 证据不再靠手工散写 |
| PKG-FIX-003 | Todo | 扩展 package asset matrix | 检查 profiles、LLM provider configs、knowledge normative corpus、plugin descriptors、memory migration/sqlite-vss assets、systemd/socket files | packaging preflight tests + installed asset probes | `Build_CMakeTools(target=dasall_packaging_preflight_tests)` | 资产缺失能在 release-preflight 暴露 |
| APP-FIX-001 | Todo | 建立 app command surface matrix | 更新 CLI/daemon/gateway docs/tests，列出 run/readiness/ping/status/cancel/diag/knowledge/multi_agent/admin-only 的支持状态 | CLI parser tests、daemon/gateway help surface tests、BusinessChain matrix consistency | `RunCtest_CMakeTools(tests=["CliCommandParserTest","DaemonBinaryUnarySmokeTest","GatewayBinaryUnarySmokeTest"])` | 用户入口、profile enablement、BusinessChain SSOT 三者一致 |

### 15.5 当前章节结论

Contracts / Packaging 当前已有较强基础和 L4 local package 证据；下一阶段核心是守住 shared ABI admission 边界，并把 L4 local 推进到 L5 qemu/autopkgtest，同时自动化 evidence matrix 回写，防止 release 结论漂移。

## 16. 全局执行顺序与统一验收口径

### 16.1 优先级排序

| 优先级 | 范围 | 原因 | 首批任务 |
|---|---|---|---|
| P0 | Memory 口径同步、Runtime path 分层、Access installed async / gateway、Capability Services triad / production backend | 直接影响总账事实正确性与默认主链证据边界 | `MEM-FIX-001`、`RT-FIX-001`、`ACC-FIX-001`、`CAPSRV-FIX-002` |
| P1 | Multi-Agent enabled gate、Infra observability/diagnostics/SecretManager live composition、Profiles consumer matrix | 防止 profile/feature enablement 与实际 wiring 漂移 | `MA-FIX-001`、`INF-FIX-001`、`INF-FIX-002`、`INF-FIX-007`、`INF-FIX-008`、`PRF-FIX-001` |
| P2 | Platform qemu、Contracts boundary guard、Packaging L5、app surface matrix | 提升 release-runner 与长期治理可信度 | `PLAT-FIX-001`、`CONT-FIX-002`、`PKG-FIX-001`、`APP-FIX-001` |
| P3 | soak / chaos / optional backend / streaming | 不阻断当前 unary 主线，但必须持续跟踪 | `INF-FIX-006`、`MA-FIX-005`、`ACC-FIX-004`、`PLAT-FIX-003` |

### 16.2 统一验收命令口径

聚焦回归优先使用每项任务表内的 `RunCtest_CMakeTools`。需要 shell 固化时，使用以下分层命令，不跨层外推：

```bash
ctest --test-dir build-ci --output-on-failure -R "Runtime|Cognition|Memory|Knowledge|Tool|Service|Access|MultiAgent|Profile|Platform|Infra|Contract"
```

```bash
Build_CMakeTools(target=dasall_gate_int_10)
Build_CMakeTools(target=dasall_packaging_preflight_tests)
```

```bash
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>
```

### 16.3 总账冻结结论

本文件当前总账口径为：DASALL 多数核心子系统已经脱离 placeholder，L1/L2 证据覆盖面较宽，且已有 installed local L4 主功能证据；但 runtime full path、Access installed positive ingress、multi_agent enabled package path、infra production sinks、profiles consumer consistency、platform qemu、contracts supporting object admission 与 packaging L5/L6 仍需按任务表逐项闭合。后续任何“production-ready / release-ready / package-ready”结论，都必须同时标明对应 L 层级、命令、worklog/SSOT 回写位置和不可外推范围。

## 附录 A：COG-FIX-004 方案 A 完整设计与专项 TODO

本附录用于承接 `COG-GAP-004` / `COG-FIX-004`：将 LLM structured output 从“bridge 成功后主要记录 diagnostics”的半明状态，推进为“bridge JSON payload 经 schema 校验、typed projection 和对象不变量校验后，成为 `PlanGraph` / `ActionDecision` 主链对象来源”的完整方案 A。

本附录是 COG-FIX-004 的执行依据。后续若同步拆入 `docs/todos/cognition/DASALL_cognition子系统专项TODO.md`，应保持本附录中的边界、任务 ID、验收命令与完成判定一致。

### A.1 决策结论

结论：COG-FIX-004 选定方案 A，并按“schema-first、validator-owned、typed projection、Facade authoritative consumption、Runtime boundary preserved”的方式实施。

目标状态：

1. `planning` bridge 返回的 JSON payload 经 `StageOutputValidator::validate_stage_output()` 校验后，投影为 `plan::PlanGraph`。
2. `execution` bridge 返回的 JSON payload 经 `StageOutputValidator::validate_stage_output()` 校验后，投影为 `decision::ActionDecision`。
3. 投影后的对象必须继续通过 `validate_plan_graph_invariants()` / `validate_action_decision_invariants()`，才能进入后续主链。
4. schema 违例、malformed JSON、字段类型漂移、非法 enum、非法边引用、非法 action decision 组合均 fail-closed，并在允许降级时走显式 fallback，而不是静默回退。
5. Runtime 仍只消费 cognition 输出的 module-public `ActionDecision`，不直接消费 provider JSON；真实 `ToolRequest`、工具执行、Recovery 准入仍归 Runtime / ToolManager / RecoveryManager。

非目标状态：

1. 不把 `PlanGraph`、`ActionDecision`、projection draft 或 schema registry 推入 shared contracts。
2. 不在 cognition 中实现 PromptComposer、PromptRegistry、provider adapter 或 retry manager。
3. 不允许 LLM JSON 直接生成 `ToolRequest`、`RecoveryRequest`、checkpoint 或 Runtime FSM transition。
4. 不以“JSON mode 能返回合法 JSON”替代应用侧 schema、typed projection 和对象不变量校验。
5. 不把 provider-private fields、reasoning trace、raw prompt 或 raw provider payload 暴露给 cognition public result、Runtime diagnostics 或长期历史。

### A.2 行业实践归纳

| 行业实践 | 可采纳结论 | 在方案 A 中的落点 |
|---|---|---|
| OpenAI Structured Outputs / JSON Schema | Structured Outputs 比 JSON mode 更可靠；JSON mode 只保证合法 JSON，不保证 schema adherence；应用仍要处理 refusal、incomplete、schema drift 和不相关输入导致的幻觉填充 | `CognitionLlmBridge` 继续声明 `response_format=json_schema/json_object` 与 `output_schema_ref`；`StageOutputValidator` 做二次 schema 校验；投影失败不能进入主链 |
| LangChain structured response | structured output 应作为 agent state 中的 typed / validated value 暴露，而不是让业务代码解析自然语言；validation error 可触发 retry 或上抛 | DASALL 不在 cognition 内自建 retry，但把 validation result 显式交给 Facade / Runtime policy；typed projection 只产 module-public object |
| Tool use / function calling schema | 当输出要驱动系统能力、工具或数据访问时，必须通过受控函数参数 schema、工具权限和执行治理分层 | `ActionDecision` 只携带 `tool_intent_hint`；Runtime 后续仍经 ToolRoute / Validator / PolicyGate / Executor / Audit |
| Schema evolution / OpenAPI / gRPC | 结构化输出 schema 需要版本、兼容策略和类型漂移检测；新增字段应兼容，删除字段要走 deprecation | 引入 `cognition.plan.v1`、`cognition.reasoning.v1` projection baseline；required / optional / unknown field 策略固定到 registry 和测试 |
| Guardrails / constrained decoding | 模型输出可被约束，但不能省略 fail-closed、edge case handling、observability 和回退策略 | schema 校验、typed projection、不变量校验、diagnostics、telemetry 与 fallback policy 全链路闭环 |

### A.3 设计原则

1. 单一入口：所有 LLM structured payload 进入主链前必须先经过 `StageOutputValidator`，不能由 `Planner`、`Reasoner`、`CognitionFacade` 或测试 fixture 私自解析。
2. 双层校验：先校验 raw JSON payload schema，再校验投影后的 module-public object invariant。
3. Typed projection owner 独立：新增 projection 组件负责 `JSON token -> draft -> PlanGraph / ActionDecision`，避免把投影逻辑散落到 Facade 或 validator。
4. 主链事实唯一：当 structured projection 模式启用且 bridge 返回 valid payload 时，`PlanGraph` / `ActionDecision` 的 source of truth 是投影对象；本地 `PlanGraphBuilder` / `Reasoner` 只能作为显式 fallback 或 comparison diagnostics。
5. 降级显式：provider failure、schema violation、projection violation、invariant violation、timeout、empty payload 必须有不同 diagnostic 和 `ErrorInfo` stage，不得混成普通 `llm_bridge.failed`。
6. Runtime 边界不变：Runtime 接收的是 cognition module-public result；Runtime 不感知 provider schema，也不绕过 cognition validator。
7. Profile 可治理：是否要求 structured projection、是否允许 local fallback、fallback 后是否可继续执行，必须来自 `CognitionConfig` / `StageExecutionPlan`，不能靠测试私有开关。
8. 证据分层：focused projection test 只证明投影；Facade structured-output test 证明 cognition 主链；Runtime interaction test 证明 Runtime 消费；installed / production 证据另行分层，不用 L1/L2 外推 L4。

### A.4 目标架构

目标数据流：

```text
CognitionFacade.decide()
  -> PerceptionEngine.perceive()
  -> CognitionLlmBridge.invoke_stage(planning/plan)
  -> StageOutputValidator.validate_stage_output(raw planning payload, plan schema)
  -> PlanGraphStructuredProjector.project(payload)
  -> StageOutputValidator.validate_plan_graph_invariants(projected_plan)
  -> CognitionLlmBridge.invoke_stage(execution/action_decision)
  -> StageOutputValidator.validate_stage_output(raw execution payload, action schema)
  -> ActionDecisionStructuredProjector.project(payload, projected_plan)
  -> StageOutputValidator.validate_action_decision_invariants(projected_action)
  -> BeliefUpdateSynthesizer.synthesize_from_decide()
  -> CognitionDecisionResult(action_decision, belief_update_hint, diagnostics)
```

组件职责：

| 组件 | 新职责 | 明确非职责 |
|---|---|---|
| `CognitionLlmBridge` | 继续投影 stage / task / schema / budget 到 LLM 公共接口；归一化 response / error；redact provider-private fields | 不解析 `PlanGraph` / `ActionDecision`；不做 retry；不做 prompt/provider 私有逻辑 |
| `StageOutputValidator` | 校验 raw payload schema；校验投影后的 object invariants；生成 `ValidationResult` / `ErrorInfo` / diagnostics | 不做 typed object construction；不静默补默认值；不直接裁定 Runtime 恢复 |
| `StageSchemaRegistry`（新增） | 维护 `planning/plan` 与 `execution/action_decision` 的 required fields、enum、numeric、list、unknown field 策略、schema version | 不持有 provider schema 私有格式；不替代 llm schema asset owner |
| `PlanGraphStructuredProjector`（新增） | 将 schema-valid planning payload 投影为 `plan::PlanGraph`，并标记 source/version/diagnostics | 不调用 Planner；不修复图结构；不把 graph 写入 contracts |
| `ActionDecisionStructuredProjector`（新增） | 将 schema-valid execution payload 投影为 `decision::ActionDecision`，并校验与 active plan 的引用关系 | 不生成 ToolRequest；不执行工具；不改变 Runtime FSM |
| `CognitionFacade` | 编排 bridge -> validator -> projector -> invariant validator；处理 fallback policy；发射 diagnostics / telemetry | 不直接解析 JSON 字段；不自建 retry/circuit breaker |
| `Planner` / `Reasoner` | 保留 deterministic fallback 和 comparison baseline | 不再在 structured projection success path 上覆盖投影对象 |

### A.5 Schema baseline

首版只覆盖 `planning/plan` 与 `execution/action_decision`，不把 `perception`、`reflection`、`response` 一次性纳入同一变更，降低主链 blast radius。

#### A.5.1 `cognition.plan.v1`

必填字段：

| 字段 | 类型 | 约束 | 投影目标 |
|---|---|---|---|
| `schema_version` | string | 固定 `cognition.plan.v1` | projection version guard |
| `plan_id` | string | 非空，建议以 request/goal 派生 | `PlanGraph.plan_id` |
| `revision` | number/integer | `>= 0` | `PlanGraph.revision` |
| `nodes` | array<object> | `1..max_plan_nodes` | `PlanGraph.nodes` |
| `nodes[].node_id` | string | 非空，唯一 | `PlanNode.node_id` |
| `nodes[].objective` | string | 非空 | `PlanNode.objective` |
| `nodes[].success_signal` | string | 非空 | `PlanNode.success_signal` |
| `nodes[].action_kind_hint` | string | enum: `tool_action` / `direct_response` / `validation` / `clarification` | `PlanNode.action_kind_hint` |
| `nodes[].depends_on` | array<string> | 只能引用已知 node id | `PlanNode.depends_on` 与 edges cross-check |
| `nodes[].evidence_refs` | array<string> | 可空；不得包含 raw provider payload | `PlanNode.evidence_refs` |
| `edges` | array<object> | edge endpoints 必须引用 known nodes；不得成环 | `PlanGraph.edges` |
| `open_questions` | array<object> | 可空；blocking 问题需携带 reason | `PlanGraph.open_questions` |
| `plan_rationale` | string | 可空但字段必须存在 | `PlanGraph.plan_rationale` |
| `estimated_complexity` | number/integer | `0..max_plan_nodes` 或 profile cap | `PlanGraph.estimated_complexity` |

Unknown field 策略：首版 fail-closed，除非字段名以 `x_` 开头且 registry 明确允许。该规则避免模型幻觉字段被误认为系统语义。

#### A.5.2 `cognition.reasoning.v1`

必填字段：

| 字段 | 类型 | 约束 | 投影目标 |
|---|---|---|---|
| `schema_version` | string | 固定 `cognition.reasoning.v1` | projection version guard |
| `decision_kind` | string | enum: `ExecuteAction` / `DirectResponse` / `AskClarification` / `ConvergeSafe` / `NoDecision` | `ActionDecision.decision_kind` |
| `confidence` | number | `0.0..1.0` | `ActionDecision.confidence` |
| `rationale` | string | 可空但字段必须存在 | `ActionDecision.rationale` |
| `selected_node_id` | string/null | `ExecuteAction` 时必须引用 active plan node；其他 kind 必须 null 或空 | `ActionDecision.selected_node_id` |
| `tool_intent_hint` | object/null | `ExecuteAction` 时必须存在且 `tool_name` 非空 | `ActionDecision.tool_intent_hint` |
| `clarification_needed` | boolean | `AskClarification` 时必须 true | `ActionDecision.clarification_needed` |
| `clarification_question` | string/null | `AskClarification` 时非空 | `ActionDecision.clarification_question` |
| `response_outline` | object/null | `DirectResponse` / `ConvergeSafe` 时 summary 非空 | `ActionDecision.response_outline` |
| `candidate_scores` | array<object> | `1..4`；每项 score `0.0..1.0` | `ActionDecision.candidate_scores` |

互斥规则：

1. `ExecuteAction` 必须有 `selected_node_id` 与 `tool_intent_hint.tool_name`，不得同时要求 clarification。
2. `AskClarification` 必须有 `clarification_question`，不得有 `tool_intent_hint`。
3. `DirectResponse` / `ConvergeSafe` 必须有 `response_outline.summary`，不得有 executable tool intent。
4. `NoDecision` 只能在 fail-fast / explicit uncertainty 场景出现；Facade 不得把 `NoDecision` 当 successful action。

### A.6 Error model 与 fallback 语义

| 失败点 | Result / diagnostic | 是否允许 fallback | 处理规则 |
|---|---|---:|---|
| bridge unavailable / provider failure | `decision_pipeline.llm_bridge_failed:<stage>` | 取决于 `rule_fallback_enabled` | 允许时走本地 Planner / Reasoner；不允许时 fail-fast |
| timeout | `decision_pipeline.stage_timeout:<stage>` | 否，除非 StageExecutionPlan 明确允许 timeout fallback | 保持 COG-FIX-003 fail-fast 语义，避免 late result 污染 |
| empty payload | `structured_output.empty_payload:<stage>` | 取决于 `structured_projection_required` | required 时 fail-fast；非 required 且 degraded allowed 时 fallback |
| malformed JSON | `structured_output.malformed_json:<stage>` | 取决于 policy | 不能进入 projector；测试必须证明不被误当主链对象 |
| schema violation | `structured_output.schema_violation:<stage>` | 取决于 policy | `StageOutputValidator` 返回 field path / issue code |
| projection violation | `structured_output.projection_failed:<stage>` | 取决于 policy | typed projector 返回 detail，不静默补字段 |
| object invariant violation | `structured_output.invariant_failed:<stage>` | 取决于 policy | 继续使用 existing object invariant validator |
| projected action 与 Runtime policy 冲突 | Runtime existing policy diagnostic | 不由 cognition 裁定 | Runtime / ToolManager 后续 gate 保持 owner |

Policy 默认建议：

1. `desktop_full`、`cloud_full`：structured projection required；schema / projection / invariant failure fail-fast 或进入 Runtime recovery，不静默本地覆盖。
2. `edge_balanced`：structured projection preferred；provider failure 可 local fallback；schema-valid projection 一旦成功即为 source of truth。
3. `edge_minimal`、`factory_test`：可保留 local fallback / template preferred，但 tests 必须能显式开启 required mode 验证方案 A 主链。

### A.7 Diagnostics 与 telemetry

新增 diagnostics 建议：

| Diagnostic | 触发条件 |
|---|---|
| `structured_projection.required:<stage>` | 当前 stage policy 要求 structured projection 成为主链来源 |
| `structured_projection.bridge_payload_valid:<stage>` | raw payload 通过 `validate_stage_output()` |
| `structured_projection.projected_plan_graph` | planning payload 成功投影为 `PlanGraph` |
| `structured_projection.projected_action_decision` | execution payload 成功投影为 `ActionDecision` |
| `structured_projection.local_fallback:<stage>` | policy 允许且已走本地 fallback |
| `structured_projection.schema_violation:<stage>` | raw payload schema 校验失败 |
| `structured_projection.projection_failed:<stage>` | typed projection 失败 |
| `structured_projection.invariant_failed:<stage>` | typed object invariant 校验失败 |

Telemetry 字段建议：

1. `structured_projection_enabled`：当前 stage 是否启用 structured projection。
2. `structured_projection_required`：失败是否允许 fallback。
3. `structured_schema_version`：`cognition.plan.v1` / `cognition.reasoning.v1`。
4. `structured_projection_source`：`llm_bridge` / `local_fallback`。
5. `structured_projection_failure_code`：schema / projection / invariant / timeout / provider。
6. `projected_node_count`、`projected_candidate_count`：只记录计数，不记录 raw payload。

### A.8 安全与边界要求

1. Redaction 在 bridge 层保留；projector 不能重新暴露 redacted 前 payload。
2. `evidence_refs` 可携带引用 ID，但不得携带 raw prompt、provider payload、secret、authorization、reasoning trace。
3. `tool_intent_hint.argument_hints` 只能是意图级 hint，不能成为工具参数 authority；Runtime 后续仍需 ToolRequest validator。
4. `delegate_hint` 首版不作为 structured projection required 字段；后续启用 multi-agent 时单独走交互契约扩展。
5. `ReflectionEngine` 不因方案 A 获得恢复执行权；structured `ActionDecision` 不改变 ADR-007。
6. `CognitionFacade` 不得并行触发本地 Reasoner 与 LLM action projection 后择优；如需 comparison，只能记录 shadow diagnostics，不能绕过 source-of-truth policy。

### A.9 设计阶段拆分

| Design ID | 产物 | 内容 | 通过条件 |
|---|---|---|---|
| COG-FIX-004A-D01 | schema baseline | 冻结 `cognition.plan.v1`、`cognition.reasoning.v1` 字段表、unknown field 策略、version 策略 | 附录 A 与 cognition 详设 / cognition 专项 TODO 引用一致 |
| COG-FIX-004A-D02 | projection contract | 冻结 projector 输入输出、error model、fallback policy、diagnostics | 所有 Build 任务可指向明确 owner / target / test |
| COG-FIX-004A-D03 | runtime boundary review | 复核 Runtime / ToolManager / RecoveryManager 边界未被 structured output 越权 | ADR-006 / ADR-007 / ADR-008 guardrail 可检索且无新增 shared contracts admission |
| COG-FIX-004A-D04 | gate plan | 冻结 Gate-COG-FIX004A-01 ~ 04 与统一验收命令 | 后续执行不再临时改成功判定 |

### A.10 Build 阶段专项 TODO

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COG-FIX-004A-DOC-001 | Done | 回写方案 A 设计到 cognition 详设与专项 TODO | 附录 A；认知详设 6.13.4 / 13.2；COG-GAP-004 | 文档设计冻结 | L2 | `docs/architecture/DASALL_cognition子系统详细设计.md`、`docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、本文档 | `COG-FIX-004`、`Gate-COG-FIX004A-*`、schema baseline | docs consistency | `rg -n "COG-FIX-004A" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md && rg -n "cognition.plan.v1" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md && rg -n "cognition.reasoning.v1" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md` | 本附录 | 无 | 无 | 更新后的详设 / TODO / 本文档 | 方案 A 不再只存在于总账附录；设计锚点、任务 ID、Gate 和命令跨文档一致 |
| COG-FIX-004A-BLD-001 | Done | 增加 StageSchemaRegistry 与 structured schema specs | 附录 A.5；COG-TODO-021 / 035 | `planning/plan`、`execution/action_decision` schema owner | L2 | 新增 `cognition/src/validation/StageSchemaRegistry.h`、`cognition/src/validation/StageSchemaRegistry.cpp`，更新 `cognition/CMakeLists.txt`、`cognition/src/validation/StageOutputValidator.h`、`tests/unit/cognition/CMakeLists.txt`、`tests/unit/cognition/StageOutputValidatorSchemaTest.cpp` | `schema_for_planning_plan()`、`schema_for_execution_action_decision()`、`StageSchemaSpec` | `StageOutputValidatorSchemaTest`、新增 `StageSchemaRegistryTest` | `cmake --build build-ci --target dasall_stage_schema_registry_unit_test dasall_stage_output_validator_schema_unit_test && ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest" && ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest"` | COG-FIX-004A-DOC-001 | 无 | 无 | schema registry 源码、unit tests、CMake 接线、对齐 registry baseline 的 schema validator test | planning / execution schema 不再散落在 Facade 或 tests；schema version、required fields、enum、numeric、list、unknown field 策略可二值断言 |
| COG-FIX-004A-BLD-002 | Done | 提取可复用 structured payload token view | 附录 A.3；COG-TODO-035 | validator private parser -> projector 可消费 token view | L2 | 新增 `cognition/src/validation/StructuredPayloadView.h`，更新 `cognition/src/validation/StageOutputValidator.cpp` / `.h`、`cognition/src/validation/StageSchemaRegistry.h` / `.cpp` | `parse_structured_payload()`、field token accessor、type-safe string/number/bool/list/object readers | `StageOutputValidatorSchemaTest`、`StageSchemaRegistryTest`、新增 malformed / unknown field cases | `cmake --build build-ci --target dasall_stage_schema_registry_unit_test dasall_stage_output_validator_schema_unit_test && ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest" && ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest"` | COG-FIX-004A-BLD-001 | 无 | parser 不扩成 shared utility；只对 cognition private projection 暴露 | token view header、validator parser 更新、focused tests | projector 不再二次手写字符串扫描；malformed、escaped pseudo-field、nested array、type mismatch 与 unknown top-level field 均 fail-closed |
| COG-FIX-004A-BLD-003 | Done | 实现 PlanGraphStructuredProjector | 附录 A.4 / A.5.1；COG-TODO-008 / 015 / 021 | planning payload -> `PlanGraph` | L2 | 新增 `cognition/src/projection/PlanGraphStructuredProjector.h`、`cognition/src/projection/PlanGraphStructuredProjector.cpp`，更新 `cognition/CMakeLists.txt`、`tests/unit/cognition/CMakeLists.txt` | `project_plan_graph()`、`PlanGraphProjectionResult` | 新增 `PlanGraphStructuredProjectionTest` | `cmake --build build-ci --target dasall_plan_graph_structured_projection_unit_test && ctest --test-dir build-ci --output-on-failure -R "PlanGraphStructuredProjectionTest"` | COG-FIX-004A-BLD-002 | 无 | 无 | projector 源码、unit tests、CMake 接线、deliverable | valid `cognition.plan.v1` payload 可投影为合法 `PlanGraph`；duplicate node、unknown edge、cycle、over cap、missing success signal 均失败且不静默修复 |
| COG-FIX-004A-BLD-004 | Done | 实现 ActionDecisionStructuredProjector | 附录 A.4 / A.5.2；COG-TODO-009 / 016 / 021 | execution payload -> `ActionDecision` | L2 | 新增 `cognition/src/projection/ActionDecisionStructuredProjector.h`、`cognition/src/projection/ActionDecisionStructuredProjector.cpp`，更新 `cognition/CMakeLists.txt`、`tests/unit/cognition/CMakeLists.txt`、`cognition/src/validation/StageOutputValidator.cpp` | `project_action_decision()`、`ActionDecisionProjectionResult`、`validate_action_decision_invariants()` | 新增 `ActionDecisionStructuredProjectionTest` | `cmake --build build-ci --target dasall_action_decision_structured_projection_unit_test && ctest --test-dir build-ci --output-on-failure -R "ActionDecisionStructuredProjectionTest"` | COG-FIX-004A-BLD-003 | 无 | 无 | projector 源码、unit tests、CMake 接线、invariant 收紧、deliverable | valid `cognition.reasoning.v1` payload 可投影为合法 `ActionDecision`；invalid enum、missing selected node、tool intent on response、clarification conflict 均 fail-closed |
| COG-FIX-004A-BLD-005 | Done | 将 Facade planning path 切到 structured projection authoritative consumption | 附录 A.4 / A.6；COG-TODO-023 / 031 / 032 / COG-FIX-003 | `CognitionFacade::run_decision_pipeline()` planning segment | L2 | 更新 `cognition/src/CognitionFacade.cpp`、`cognition/src/validation/StageOutputValidator.cpp`、`tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`、`tests/unit/cognition/CognitionFacadeStructuredPlanOutputTest.cpp`、`tests/unit/cognition/CMakeLists.txt` | `consume_decision_bridge_stage()`、planning projection flow、fallback policy、array-path schema gate | 新增 `CognitionFacadeStructuredPlanOutputTest` | `cmake --build build-ci --target dasall_stage_output_validator_schema_unit_test dasall_cognition_facade_structured_plan_output_unit_test && ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredPlanOutputTest"` | COG-FIX-004A-BLD-003 | 无 | stage policy 未冻结则回到 DOC-001 | Facade 变更、validator array-path 修正、unit tests | schema-valid planning bridge payload 成为 active `PlanGraph`；本地 Planner 只在显式 fallback 条件下使用；invalid payload 不会悄悄进入 plan graph |
| COG-FIX-004A-BLD-006 | Done | 将 Facade execution path 切到 structured projection authoritative consumption | 附录 A.4 / A.6；COG-TODO-023 / 031 / 033 / 034 | `CognitionFacade::run_decision_pipeline()` execution segment | L2 | 更新 `cognition/src/CognitionFacade.cpp`、`tests/unit/cognition/CognitionFacadeStructuredActionOutputTest.cpp`、`tests/unit/cognition/CMakeLists.txt` | execution projection flow、action invariant validation、belief synthesis handoff | 新增 `CognitionFacadeStructuredActionOutputTest` | `cmake --build build-ci --target dasall_cognition_facade_structured_action_output_unit_test && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredActionOutputTest"` | COG-FIX-004A-BLD-004、COG-FIX-004A-BLD-005 | 无 | 无 | Facade 变更、unit tests、deliverable | schema-valid execution bridge payload 成为 `ActionDecision`；Runtime-facing result 保持 existing shape；invalid action payload fail-closed 或显式 fallback |
| COG-FIX-004A-BLD-007 | Done | 扩展 MockLLMManager / MockCognitionFixture 支持 structured payload scenarios | 附录 A.9；COG-TODO-004 / 024 / 037 | tests support | L3 | 更新 `tests/mocks/include/MockLLMManager.h`、`tests/mocks/include/MockCognitionFixture.h` | planning/action JSON fixture builders、malformed / schema-invalid / projection-invalid staged results | `MockCognitionFixtureSurfaceTest`、projection/facade tests | `cmake --build build-ci --target dasall_mock_cognition_fixture_surface_unit_test && ctest --test-dir build-ci --output-on-failure -R "MockCognitionFixtureSurfaceTest"` | COG-FIX-004A-BLD-001 | 无 | 无 | mock fixture 更新 | tests 可稳定注入 valid / invalid structured payload，不维护第二套 stage mapping 或 provider-private payload |
| COG-FIX-004A-BLD-008 | Done | 增加 cognition structured-output integration regression | 附录 A.3 / A.6 / A.8；COG-TODO-027 / 037 | cognition integration | L2 | 新增 `tests/integration/cognition/CognitionStructuredOutputIntegrationTest.cpp`，更新 `tests/integration/cognition/CMakeLists.txt` | end-to-end `decide()` through bridge projection | `CognitionStructuredOutputIntegrationTest` | `cmake --build build-ci --target dasall_cognition_structured_output_integration_test && ctest --test-dir build-ci --output-on-failure -R "CognitionStructuredOutputIntegrationTest"` | COG-FIX-004A-BLD-006、COG-FIX-004A-BLD-007 | 无 | 无 | integration test、CMake 接线 | bridge valid JSON 驱动 `PlanGraph` 与 `ActionDecision` 的主链路径可复验；malformed / invalid payload regression 明确失败或 fallback |
| COG-FIX-004A-BLD-009 | Done | 补齐 Runtime interaction contract 证明 projected ActionDecision 被消费 | 附录 A.4 / A.8；COG-TODO-027 / COG-FIX-001 | Runtime -> Cognition -> Runtime interaction | L2 | 扩展 `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` 或新增 structured fixture | projected `ActionDecision` -> Runtime first hop / response builder / tool intent handoff | `CognitionRuntimeInteractionContractTest` structured cases | `cmake --build build-ci --target dasall_cognition_runtime_interaction_contract_integration_test && ctest --test-dir build-ci --output-on-failure -R "CognitionRuntimeInteractionContractTest"` | COG-FIX-004A-BLD-008 | 无 | Runtime 不得消费 provider JSON；只消费 projected `ActionDecision` | runtime interaction tests | Runtime 证明消费的是 projection 后的 module-public `ActionDecision`，不是 raw bridge payload；`ExecuteAction` / `DirectResponse` / `ConvergeSafe` 至少各有一条 structured case |
| COG-FIX-004A-BLD-010 | Done | 接入 diagnostics / telemetry structured projection fields | 附录 A.7；COG-TODO-022 / COG-FIX-005 | telemetry / diagnostics | L2 | 更新 `cognition/src/observability/CognitionTelemetry.*`、`cognition/src/CognitionFacade.cpp`、相关 tests | projection source、schema version、failure code、projected counts | `CognitionTelemetryFieldsTest`、Facade structured tests | `cmake --build build-ci --target dasall_cognition_telemetry_fields_unit_test dasall_cognition_facade_structured_plan_output_unit_test dasall_cognition_facade_structured_action_output_unit_test && ctest --test-dir build-ci --output-on-failure -R "CognitionTelemetryFieldsTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredPlanOutputTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredActionOutputTest"` | COG-FIX-004A-BLD-006 | COG-FIX-005 production sink 未完成不阻断 focused fields | production sink 另由 COG-FIX-005 收口；本任务只冻结字段与 redaction | telemetry code/tests | structured projection success/failure 均有可观测字段；不记录 raw payload 或 provider-private fields |
| COG-FIX-004A-BLD-011 | Done | 建立 schema drift / safety negative matrix | 附录 A.5 / A.8；COG-TODO-035 / 037 | negative regression | L2 | 扩展 `tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`、projection tests、integration regression | schema version mismatch、unknown field、raw provider payload leakage、tool arg overreach、delegate_hint disabled | `StageOutputValidatorSchemaTest`、`PlanGraphStructuredProjectionTest`、`ActionDecisionStructuredProjectionTest`、`CognitionStructuredOutputIntegrationTest` | `cmake --build build-ci --target dasall_stage_output_validator_schema_unit_test dasall_plan_graph_structured_projection_unit_test dasall_action_decision_structured_projection_unit_test dasall_cognition_structured_output_integration_test && ctest --test-dir build-ci --output-on-failure -R "^StageOutputValidatorSchemaTest$" && ctest --test-dir build-ci --output-on-failure -R "StructuredProjectionTest" && ctest --test-dir build-ci --output-on-failure -R "^CognitionStructuredOutputIntegrationTest$"` | COG-FIX-004A-BLD-008 | 无 | 无 | negative tests | schema drift、unknown fields、provider-private leakage、tool execution overreach 与 delegate drift 均可被自动化拦截 |
| COG-FIX-004A-BLD-012 | Done | 回写 COG-FIX-004 完成证据与 Gate 结果 | 附录 A.11 / A.12；DASALL 开发执行规范 | docs / worklog / SSOT evidence | L2 | 更新本文档、`docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`、必要时更新 cognition deliverable | COG-FIX-004 status、Gate-COG-FIX004A-*、validation commands | docs consistency + final focused ctest | `rg -n "COG-FIX-004" docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md && rg -n "Gate-COG-FIX004A" docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md && ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest|StageOutputValidatorSchemaTest|PlanGraphStructuredProjectionTest|ActionDecisionStructuredProjectionTest|CognitionFacadeStructuredPlanOutputTest|CognitionFacadeStructuredActionOutputTest|CognitionStructuredOutputIntegrationTest|CognitionRuntimeInteractionContractTest|CognitionTelemetryFieldsTest"` | COG-FIX-004A-BLD-001 ~ 011 | 任一 Build task 未通过不得 Done | 全部 Build tasks 通过后回写 | docs/worklog/deliverable | `COG-FIX-004` 已从 Todo 改为 Done；证据包含命令、结果、风险和不可外推范围 |

2026-05-15 回写结果：认知详设已新增 `COG-FIX-004A` structured projection authoritative consumption 设计冻结小节与 13.2 schema 对齐补充；cognition 专项 TODO 已新增 6.7 任务簇、`Gate-COG-FIX004A-01` ~ `05` 与 13.25 文档冻结记录；因此 `COG-FIX-004A-DOC-001` 完成，后续 `BLD-001` ~ `012` 进入 Build 执行序列。

2026-05-15 BLD-001 回写结果：`StageSchemaRegistry` 已落盘到 `cognition/src/validation/StageSchemaRegistry.h` / `.cpp`，`StageSchemaSpec` owner 从 validator / tests 的分散定义收敛到 registry；`StageOutputValidatorSchemaTest` 已改为直接消费 `schema_for_execution_action_decision()`，并新增 `StageSchemaRegistryTest` 与 CMake 接线。验收命令 `cmake --build build-ci --target dasall_stage_schema_registry_unit_test dasall_stage_output_validator_schema_unit_test && ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest" && ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest"` 通过，因此 `COG-FIX-004A-BLD-001` 完成，后续 `BLD-002` ~ `012` 继续保持 Todo。

2026-05-15 BLD-002 回写结果：`StructuredPayloadView` 已落盘到 `cognition/src/validation/StructuredPayloadView.h`，把 validator 私有 parser 提炼为 projector 可复用的 token view；`StageOutputValidator` 已切到 view 访问器，并按 registry 的 `known_top_level_fields` 与 `allowed_extension_prefixes` 执行 unknown field fail-closed；`StageOutputValidatorSchemaTest` 新增 registered `x_` extension 正例与 unknown top-level field 负例，`StageSchemaRegistryTest` 新增 unknown-field baseline 断言。验收命令 `cmake --build build-ci --target dasall_stage_schema_registry_unit_test dasall_stage_output_validator_schema_unit_test && ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest" && ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest"` 通过，因此 `COG-FIX-004A-BLD-002` 完成，后续 `BLD-003` ~ `012` 继续保持 Todo。

2026-05-15 BLD-003 回写结果：`PlanGraphStructuredProjector` 已落盘到 `cognition/src/projection/PlanGraphStructuredProjector.h` / `.cpp`，将 planning payload 的 typed projection owner 从未来的 Facade / tests 中分离出来；projector 负责 `plan_id`、`revision`、`estimated_complexity`、`nodes`、`edges`、`open_questions` 的 typed construction，并对 projection 级字段错误 fail-closed。`tests/unit/cognition/PlanGraphStructuredProjectionTest.cpp` 已新增 valid payload 正例，以及 missing success signal、duplicate node、unknown edge、cycle、over cap 负例；验收命令 `cmake --build build-ci --target dasall_plan_graph_structured_projection_unit_test && ctest --test-dir build-ci --output-on-failure -R "PlanGraphStructuredProjectionTest"` 通过，因此 `COG-FIX-004A-BLD-003` 完成，后续 `BLD-004` ~ `012` 继续保持 Todo。

2026-05-15 BLD-004 回写结果：`ActionDecisionStructuredProjector` 已落盘到 `cognition/src/projection/ActionDecisionStructuredProjector.h` / `.cpp`，将 execution payload 的 typed projection owner 从未来的 Facade / tests 中分离出来；projector 负责 `decision_kind`、`confidence`、`clarification_needed`、`tool_intent_hint`、`response_outline`、`candidate_scores` 的 typed construction，并对 enum / nested object / candidate score 类型错配 fail-closed。`tests/unit/cognition/ActionDecisionStructuredProjectionTest.cpp` 已新增 valid payload 正例，以及 invalid enum、missing selected node、tool intent on response、clarification conflict 负例；同时 `StageOutputValidator::validate_action_decision_invariants()` 已补齐 response/tool 与 clarification conflict gate。验收命令 `cmake --build build-ci --target dasall_action_decision_structured_projection_unit_test && ctest --test-dir build-ci --output-on-failure -R "ActionDecisionStructuredProjectionTest"` 通过，因此 `COG-FIX-004A-BLD-004` 完成，`Gate-COG-FIX004A-02` 转为 Pass，后续 `BLD-005` ~ `012` 继续保持 Todo。

2026-05-15 BLD-005 回写结果：`CognitionFacade::run_decision_pipeline()` planning segment 已切到 structured projection authoritative consumption；planning bridge payload 现在先经 `schema_for_planning_plan()`、`PlanGraphStructuredProjector` 与 `validate_plan_graph_invariants()`，成功后直接成为 active `PlanGraph`，只有 bridge/schema/projection/invariant 失败且 degraded path 明确允许时才回到 local planner。为支撑该主链，本轮同时修正了 `StageOutputValidator` 对 `nodes.node_id`、`nodes.action_kind_hint` 等 array-in-path 字段的 schema gate，并在 `StageOutputValidatorSchemaTest` 中补齐 planning nested-field 正负例。验收命令 `cmake --build build-ci --target dasall_stage_output_validator_schema_unit_test dasall_cognition_facade_structured_plan_output_unit_test && ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredPlanOutputTest"` 通过，因此 `COG-FIX-004A-BLD-005` 完成。

2026-05-15 BLD-006 回写结果：`CognitionFacade::run_decision_pipeline()` execution segment 已切到 structured projection authoritative consumption；execution bridge payload 现在先经 `schema_for_execution_action_decision()`、`ActionDecisionStructuredProjector` 与 `validate_action_decision_invariants()`，成功后直接成为 `CognitionDecisionResult.action_decision`，invalid action payload 在 degraded path 禁用时 fail-closed，在允许时才回到 local reasoner / bounded fallback。`tests/unit/cognition/CognitionFacadeStructuredActionOutputTest.cpp` 已新增 authoritative direct-response 正例与 invalid execution payload fail-closed 负例；同时复跑 `CognitionFacadeFlowTest` 与 `CognitionFacadeDegradedModeTest`，确认旧的 flow / degraded 语义未回退。验收命令 `cmake --build build-ci --target dasall_cognition_facade_structured_action_output_unit_test dasall_cognition_facade_flow_unit_test dasall_cognition_facade_degraded_mode_unit_test && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredActionOutputTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeFlowTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeDegradedModeTest"` 通过，因此 `COG-FIX-004A-BLD-006` 完成，`Gate-COG-FIX004A-03` 转为 Pass，后续 `BLD-007` ~ `012` 继续保持 Todo。

2026-05-15 BLD-007 回写结果：`tests/mocks/include/MockLLMManager.h` 已新增 `make_structured_stage_result()` 与 `set_structured_stage_payload()`，把 staged structured payload 注入收口到统一 stage route helper；`tests/mocks/include/MockCognitionFixture.h` 已新增 planning / execution structured payload builders，以及 valid、malformed、schema-invalid、projection-invalid scenario 与对应 stage helper。`tests/unit/cognition/MockCognitionFixtureSurfaceTest.cpp` 已补齐 payload parseability 与 staged result surface 断言，`tests/unit/cognition/CognitionFacadeStructuredPlanOutputTest.cpp`、`tests/unit/cognition/CognitionFacadeStructuredActionOutputTest.cpp` 已改为直接复用 fixture helper，不再手写 structured stage payload。验收命令 `cmake --build build-ci --target dasall_mock_cognition_fixture_surface_unit_test dasall_plan_graph_structured_projection_unit_test dasall_action_decision_structured_projection_unit_test dasall_cognition_facade_structured_plan_output_unit_test dasall_cognition_facade_structured_action_output_unit_test && ctest --test-dir build-ci --output-on-failure -R "MockCognitionFixtureSurfaceTest|PlanGraphStructuredProjectionTest|ActionDecisionStructuredProjectionTest|CognitionFacadeStructuredPlanOutputTest|CognitionFacadeStructuredActionOutputTest"` 通过，因此 `COG-FIX-004A-BLD-007` 完成，后续 `BLD-008` ~ `012` 继续保持 Todo。

2026-05-15 BLD-008 回写结果：`tests/integration/cognition/CognitionStructuredOutputIntegrationTest.cpp` 已新增 snapshot-backed cognition integration regression，直接通过 `create_cognition_engine(snapshot, dependencies)`、`MockCognitionFixture` 与 `make_true_integration_policy_snapshot()` 复验 structured planning / execution payload 的 integration 主链。该用例覆盖 valid bridge payload 正例、planning malformed 显式 fallback 负例，以及 execution invariant invalid 且降级关闭时的 fail-fast 负例；`tests/integration/cognition/CMakeLists.txt` 已完成 `dasall_cognition_structured_output_integration_test` 接线。验收命令 `cmake --build build-ci --target dasall_cognition_structured_output_integration_test && ctest --test-dir build-ci --output-on-failure -R "CognitionStructuredOutputIntegrationTest"` 通过，且 `ctest --test-dir build-ci -N | rg "CognitionStructuredOutputIntegrationTest"` 已确认 discoverability，因此 `COG-FIX-004A-BLD-008` 完成，后续 `BLD-009` ~ `012` 继续保持 Todo。

2026-05-15 BLD-009 回写结果：`tests/mocks/include/MockCognitionFixture.h` 已补充 `StructuredExecutionPayloadScenario::ValidConvergeSafe`，避免 Runtime structured contract case 回退到手写 execution JSON；`tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` 已新增一个只代理 real structured `decide()` 的 cognition wrapper 与 `ContractProbeToolManager`，把 structured ExecuteAction / DirectResponse / ConvergeSafe 三类 case 接入现有 Runtime interaction contract。新增 structured ExecuteAction case 已证明 Runtime 会把 projected `tool_intent_hint.tool_name` / `argument_hints` 映射为 `ToolRequest.tool_name` / `arguments_payload`，并把 projected `selected_node_id` 与 tool-round `latest_observation` 传入 response builder；structured DirectResponse / ConvergeSafe cases 已证明 Runtime 直接消费 projected terminal `ActionDecision` 与 `response_outline.summary`，而不是消费 raw provider payload。验收命令 `cmake --build build-ci --target dasall_cognition_runtime_interaction_contract_integration_test && ctest --test-dir build-ci --output-on-failure -R "CognitionRuntimeInteractionContractTest"` 通过，因此 `COG-FIX-004A-BLD-009` 完成，后续 `BLD-010` ~ `012` 继续保持 Todo。

2026-05-15 BLD-010 回写结果：`cognition/src/observability/CognitionTelemetry.h` / `.cpp` 已为 `StageTelemetryContext` 新增 `StructuredProjectionTelemetry` 载体，并把 `structured_projection_enabled`、`structured_projection_required`、`structured_schema_version`、`structured_projection_source`、`structured_projection_failure_code`、`projected_node_count`、`projected_candidate_count` 接入 telemetry 事件字段；`cognition/src/CognitionFacade.cpp` 已为 planning / execution structured bridge path补齐同源 diagnostics，并在 `decide()` 出口把这些 diagnostics 汇总回 telemetry context，确保 projection source、schema version、failure code 与 projected counts 在 owner telemetry 与 façade diagnostics 两侧同步可观测。`tests/unit/cognition/CognitionTelemetryFieldsTest.cpp`、`tests/unit/cognition/CognitionFacadeStructuredPlanOutputTest.cpp`、`tests/unit/cognition/CognitionFacadeStructuredActionOutputTest.cpp` 已分别补齐 telemetry field、planning fallback / success、execution fail-closed / success 断言。验收命令 `cmake --build build-ci --target dasall_cognition_telemetry_fields_unit_test dasall_cognition_facade_structured_plan_output_unit_test dasall_cognition_facade_structured_action_output_unit_test && ctest --test-dir build-ci --output-on-failure -R "CognitionTelemetryFieldsTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredPlanOutputTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredActionOutputTest"` 通过，因此 `COG-FIX-004A-BLD-010` 完成；production sink 继续由 `COG-FIX-005` 收口，`Gate-COG-FIX004A-04` 仍待 `BLD-011` 后统一转 Pass。

2026-05-15 BLD-011 回写结果：`cognition/src/projection/PlanGraphStructuredProjector.cpp` 与 `cognition/src/projection/ActionDecisionStructuredProjector.cpp` 已补齐 `schema_version` 校验以及 top-level / nested unexpected-field fail-closed guard，避免 `provider_payload`、`delegate_hint`、`tool_intent_hint.arguments_payload` 这类 schema drift / safety overreach 被静默吞掉；`tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`、`tests/unit/cognition/PlanGraphStructuredProjectionTest.cpp`、`tests/unit/cognition/ActionDecisionStructuredProjectionTest.cpp` 与 `tests/integration/cognition/CognitionStructuredOutputIntegrationTest.cpp` 已分别覆盖 schema version mismatch、provider-private leakage、tool arg overreach、delegate hint disabled，以及 planning fallback / execution fail-fast 的 integration regression。验收命令 `cmake --build build-ci --target dasall_stage_output_validator_schema_unit_test dasall_plan_graph_structured_projection_unit_test dasall_action_decision_structured_projection_unit_test dasall_cognition_structured_output_integration_test && ctest --test-dir build-ci --output-on-failure -R "^StageOutputValidatorSchemaTest$" && ctest --test-dir build-ci --output-on-failure -R "StructuredProjectionTest" && ctest --test-dir build-ci --output-on-failure -R "^CognitionStructuredOutputIntegrationTest$"` 通过，因此 `COG-FIX-004A-BLD-011` 完成，`Gate-COG-FIX004A-04` 转为 Pass；`COG-FIX-004` 总完成证据与 Gate-05 仍待 `COG-FIX-004A-BLD-012` 统一回写。

2026-05-15 BLD-012 回写结果：本文档中的 `COG-FIX-004` 总任务已改为 Done，附录 A 中 `COG-FIX-004A-BLD-012` 已改为 Done，`Gate-COG-FIX004A-05` 已由 Pending 转为 Pass；`docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 与 `docs/worklog/DASALL_开发执行记录.md` 已同步回写最终状态、统一验收命令与不可外推范围。docs consistency 命令 `rg -n "COG-FIX-004" docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md && rg -n "Gate-COG-FIX004A" docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md` 与 final focused ctest 命令 `ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest|StageOutputValidatorSchemaTest|PlanGraphStructuredProjectionTest|ActionDecisionStructuredProjectionTest|CognitionFacadeStructuredPlanOutputTest|CognitionFacadeStructuredActionOutputTest|CognitionStructuredOutputIntegrationTest|CognitionRuntimeInteractionContractTest|CognitionTelemetryFieldsTest"` 通过，因此 `COG-FIX-004` 的 A.13 完成判定 1 ~ 8 已全部满足；本次完成结论仍只提升 cognition 的 L1 / L2 主链证据，不把 installed / qemu / soak 或 production telemetry sink 外推为已完成。

2026-05-15 Gate-05 widened acceptance 补证：`cognition/src/StagePolicyResolver.cpp` 已把 decision fallback 收口为 profile-aware matrix，`cognition/src/CognitionFacade.cpp` 保留 config-only degraded path 的 request-owned fail-open 语义，`tests/unit/cognition/CognitionFacadeStageTimeoutTest.cpp` 与 `tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h` 已改用 structured planning / execution payload，避免 authoritative structured path 下继续使用纯文本 bridge success fixture。`tests/integration/cognition/CognitionProfileCompatibilityTest.cpp` 与 `CognitionRuntimePolicyProjectionIntegrationTest.cpp` 则补齐 runtime-init diagnostics 与 `edge_minimal` boundary 对齐：`edge_minimal` 目前只冻结“显式终态必须存在；若发出 reflection bridge，则 route / timeout 必须来自真实 snapshot 投影”，不提前冻结其 terminal status / reflection emission 宽语义。随后通过 `RunCtest_CMakeTools` 复核 `CognitionFacadeStageTimeoutTest`、`CognitionFacadeDegradedModeTest`、`CognitionProfileCompatibilityTest`、`CognitionRuntimePolicyProjectionIntegrationTest`、`StagePolicyResolverTest`、`StagePolicyResolverProfileDiffTest` 六个 blocker / owner tests，并把当前 49 个 cognition unit / integration tests 在 `build/vscode-linux-ninja` 上全量复跑为绿；因此 `Gate-COG-FIX004A-05` 现已具 focused + wider acceptance 双层证据，但 widening 结论仍只提升 cognition L1 / L2 主链可信度，不外推到 installed / qemu / soak 或 production telemetry sink。

### A.11 Gate 与统一验收命令

| Gate ID | 名称 | 触发时机 | 通过条件 | 回退动作 |
|---|---|---|---|---|
| Gate-COG-FIX004A-01 | Schema / projection design gate | BLD-001 前 | schema baseline、projection contract、fallback policy、diagnostics 均冻结并跨文档一致 | 回到 DOC-001，不进入 Build |
| Gate-COG-FIX004A-02 | Projector unit gate | BLD-003 / BLD-004 后 | PlanGraph / ActionDecision projector 正负例全绿；不变量 validator 继续通过 | 回退 projector，不改 Facade |
| Gate-COG-FIX004A-03 | Facade authoritative consumption gate | BLD-005 / BLD-006 后 | structured payload 成为 Facade 主链对象来源；invalid payload fail-closed 或显式 fallback | 回退 Facade 接线，保留 projector focused tests |
| Gate-COG-FIX004A-04 | Runtime interaction and negative matrix gate | BLD-008 ~ BLD-011 后 | runtime interaction、structured integration、schema drift、安全负例、telemetry fields 全绿 | 回退对应 owner，不回写 Done |
| Gate-COG-FIX004A-05 | Evidence writeback gate | BLD-012 后 | TODO、worklog、deliverable 与命令结果一致；不可外推范围明确 | COG-FIX-004 保持 Todo / Blocked |

当前状态（2026-05-15）：`Gate-COG-FIX004A-01` = Pass（DOC-001 已完成并跨文档一致）；`Gate-COG-FIX004A-02` = Pass（BLD-003 / BLD-004 projector focused tests 已全绿）；`Gate-COG-FIX004A-03` = Pass（Facade planning / execution authoritative consumption 已具 focused evidence）；`Gate-COG-FIX004A-04` = Pass（BLD-008 ~ 011 已具 focused evidence，schema drift 与 safety negative matrix 已补齐）；`Gate-COG-FIX004A-05` = Pass（最终证据、统一验收命令与不可外推范围已跨文档一致回写，且 `build/vscode-linux-ninja` 上当前 49 个 cognition tests widened acceptance 全绿）。

阶段性聚焦命令：

```bash
ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest|StageOutputValidatorSchemaTest"
```

```bash
ctest --test-dir build-ci --output-on-failure -R "PlanGraphStructuredProjectionTest|ActionDecisionStructuredProjectionTest"
```

```bash
ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredPlanOutputTest|CognitionFacadeStructuredActionOutputTest"
```

```bash
ctest --test-dir build-ci --output-on-failure -R "CognitionStructuredOutputIntegrationTest|CognitionRuntimeInteractionContractTest|CognitionTelemetryFieldsTest"
```

统一验收命令：

```bash
cmake --build build-ci --target dasall_cognition dasall_unit_tests dasall_integration_tests && \
ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest|StageOutputValidatorSchemaTest|PlanGraphStructuredProjectionTest|ActionDecisionStructuredProjectionTest|CognitionFacadeStructuredPlanOutputTest|CognitionFacadeStructuredActionOutputTest|CognitionStructuredOutputIntegrationTest|CognitionRuntimeInteractionContractTest|CognitionTelemetryFieldsTest"
```

### A.12 风险与回退策略

| Risk ID | 风险 | 影响 | 缓解 / 回退 |
|---|---|---|---|
| COG-FIX004A-R01 | schema 与 C++ 类型漂移 | 投影成功但对象语义错位 | registry + projector unit + invariant validator 三层 gate；字段改动必须同步测试 |
| COG-FIX004A-R02 | LLM payload 幻觉出可执行工具意图 | 可能推动错误 Tool route | `ActionDecision` 仍只给 `tool_intent_hint`；Runtime ToolManager / policy gate 继续裁定 |
| COG-FIX004A-R03 | invalid payload 被 local fallback 掩盖 | 测试误以为 structured 主链成立 | diagnostics 必须标记 `local_fallback`；required mode tests 禁止 fallback |
| COG-FIX004A-R04 | Facade 复杂度膨胀 | cognition 主链难以维护 | projection owner 独立；Facade 只编排结果，不直接解析字段 |
| COG-FIX004A-R05 | profile 策略不一致 | edge / factory tests 与 production path 语义冲突 | structured projection policy 必须来自 `StageExecutionPlan`，并在 profile compatibility 中断言 |
| COG-FIX004A-R06 | provider-private 字段泄漏 | 安全与合规风险 | bridge redaction + projector denylist + negative tests；telemetry 只记 schema version/count/source |

回退原则：

1. BLD-001 / BLD-002 失败：只回退 schema registry / parser，不影响现有 Facade 主链。
2. BLD-003 / BLD-004 失败：保留 raw schema validator，projector 不接入 Facade。
3. BLD-005 / BLD-006 失败：Facade 回退到当前 local Planner / Reasoner 主链，structured projection 保持 focused-only，不宣称 COG-FIX-004 完成。
4. BLD-008 / BLD-009 失败：不升级证据层级，不把 unit projector 通过外推为 Runtime interaction 成立。
5. 任何安全负例失败：停止回写 Done，优先修 negative matrix。

### A.13 完成判定

COG-FIX-004 仅当以下条件全部满足时可标记 Done：

1. `planning/plan` 与 `execution/action_decision` 的 schema baseline 已冻结并跨文档一致。
2. `PlanGraphStructuredProjector` 与 `ActionDecisionStructuredProjector` 有独立正负例单测。
3. `CognitionFacade` 在 structured projection required mode 下，使用 bridge valid payload 生成主链 `PlanGraph` / `ActionDecision`。
4. malformed JSON、schema violation、projection violation、object invariant violation 均 fail-closed 或按 policy 显式 fallback，且 diagnostics 可区分。
5. Runtime interaction contract 证明 projected `ActionDecision` 被 Runtime 作为 module-public cognition result 消费，而不是消费 raw JSON。
6. Telemetry / diagnostics 记录 projection source、schema version、failure code，且不泄漏 raw provider payload。
7. 统一 focused 验收命令通过，worklog / TODO / deliverable 完成证据回写，且当前 49 个 cognition unit / integration tests 的 widened acceptance 不再暴露 authority/fallback 回归。
8. COG-FIX-004 的完成结论只提升 cognition L1/L2 主链证据；installed / qemu / soak 证据仍按总账 L 层级另行声明，不外推。
