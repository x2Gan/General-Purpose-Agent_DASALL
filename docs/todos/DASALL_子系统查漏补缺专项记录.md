# DASALL 子系统查漏补缺专项记录

最近更新时间：2026-05-13  
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
| llm | 已记录 | D1-D9 主体实现 L2 基本闭合；production LLM generation 有 L4 local 证据 | streaming 生命周期、production adapter family 覆盖、production observability/audit sink 接入、L5 qemu / release runner / soak 证据、源码边界回归防线 |
| memory | 已记录 | 主体实现 L2 基本闭合；Runtime / app 链路已接入；installed writeback 有 L4 local 证据 | sqlite-vss concrete backend、SQLite 3.51.3 基线、observability / audit / metrics / trace sink、L5 qemu / release runner / soak 证据、并发与边界回归防线 |
| knowledge | 已记录 | 主体实现 L2 基本闭合；Runtime -> Memory evidence 读链路已打通；本轮 9 个聚焦测试通过、1 个 installed asset probe 失败 | refresh 异步/首启 build、concrete vector backend、lane timeout/parallel recall、首批 corpus baseline、持久化 ledger/catalog/migration、production telemetry sink、installed/qemu/soak 证据 |
| tools | 已记录 | 主体治理链、MCP/Skill/Workflow/Projection 已达到 L2 fixture / integration 可信；Runtime/app 可调用 IToolManager | production live services 后端仍落默认实现、plugin 自动接入未闭合、MCP 仅 stdio concrete、compensate 入口未实现、production observability 与 installed/qemu/soak 证据不足 |
| capability services | 已记录 | 主体服务门面、execution/data/system lanes、adapter/router/bridge、observability 与健康探针达到 L2 fixture / integration 可信 | runtime production 未装配真实 services facade/backend、data cache 命中结果 triad 缺陷、subscription trace 链不完整、adapter concrete backend / production sinks / installed-qemu-soak 证据不足 |
| runtime | 已记录 | 控制面主体已落地，source evidence 覆盖 AgentFacade / AgentOrchestrator / FSM / Budget / Checkpoint / Recovery / Session / Scheduler / SafeMode；true integration 仅覆盖部分 live unary 路径 | production direct LLM path 绕过完整 cognition/tools/recovery 主链、Session/Checkpoint in-memory、resume synthetic、timeout/cancel/budget/telemetry/health/scheduler/production evidence 未全闭合 |

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
| app live composition -> cognition | 已装配但不保证必经 | L0 / L4 boundary | `RuntimeLiveDependencyComposition` 创建 cognition ports，同时添加 `required-live-baseline` 触发 production direct LLM path |

### 4.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| COG-GAP-001 | High | Runtime live cognition path 未实现 `DirectResponse` / `ConvergeSafe -> Responding + IResponseBuilder.build()` 第一跳 | `AgentOrchestrator` live path 只允许 `ExecuteAction` 继续，非可执行决策只在澄清 degrade 条件成立时进入 `WaitingClarify`，否则失败 | 详设 6.14.1 的 ActionDecision→FSM 映射不完整；terminal cognition decision 会被误判为失败 | 在 Runtime 增加 terminal decision 分支与 interaction contract tests |
| COG-GAP-002 | High | installed production `dasall run` 不证明 cognition `decide()` / `reflect()` 必经 | `required-live-baseline` 触发 production LLM direct path；FULLINT-TODO-003 已记录 cognition L4 未证明 | 不能宣称 cognition 与 installed 主链完全打通 | 增加 cognition-positive installed / app-binary evidence，或明确提供 cognition-first mode / gate |
| COG-GAP-003 | Medium | Facade 未主动执行 stage timeout isolation | `deadline_ms` 已投影，但 Facade 同步调用阶段和 bridge，未见超时裁定与 late result discard | 单阶段卡死可能拖垮整条 cognition 链，详设 6.15.3 未闭合 | 增加 stage runner / timeout wrapper / timeout tests |
| COG-GAP-004 | Medium | LLM structured output validator 未成为主链对象生成裁决入口 | `StageOutputValidator` 独立可用；Facade bridge result 成功后主要记录 diagnostics，本地 Planner / Reasoner 仍生成语义对象 | schema validation 能力存在，但不能证明 LLM JSON 输出真正驱动 PlanGraph / ActionDecision | 增加 structured output parse -> validate -> project pipeline，或明确 v1 只用 bridge as advisory |
| COG-GAP-005 | Medium | CognitionTelemetry production sink 接入不足 | `CognitionRuntimeDependencies` 仅含 `llm_manager` 和 `policy_snapshot`；默认 no-op sink | 语义观测能力在模块内可测，但 production trace / metric / audit 链路未证明 | 扩展窄依赖面或在 runtime composition 注入 infra sink |
| COG-GAP-006 | Medium | ResponseBuilder degraded telemetry 未与 production response path 明确打通 | Telemetry 有 `emit_response_degraded()`，但 ResponseBuilder 未明显持有 telemetry | response fallback / degraded path 生产观测可能缺字段 | 给 ResponseBuilder 注入 telemetry 或在 Runtime terminalize 层统一 emit |
| COG-GAP-007 | Low | 详设 / TODO 历史状态容易与当前代码状态混淆 | cognition 专项 TODO 中有历史 Done 与后续 Todo 混合，详设仍有历史 baseline 段落 | 后续评审容易把历史 Gate 当当前可合并状态 | 用本文件和后续 Gate-COG-12 复验结果统一当前口径 |

### 4.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| COG-FIX-001 | Todo | 补齐 terminal ActionDecision 到 Runtime Responding 映射 | 更新 `runtime/src/AgentOrchestrator.cpp`，在 live cognition path 中处理 `DirectResponse` / `ConvergeSafe`，进入 `RuntimeState::Responding` 并调用 `IResponseBuilder.build()`；保留 `NoDecision` fail-fast | 扩展 `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp`，新增 DirectResponse、ConvergeSafe、NoDecision 三类 fixture | `cmake --build build-ci --target dasall_cognition_runtime_interaction_contract_integration_test && ctest --test-dir build-ci -R "CognitionRuntimeInteractionContractTest" --output-on-failure` | 详设 6.14.1 五类 decision kind 第一跳均有自动化断言 |
| COG-FIX-002 | Todo | 建立 cognition-positive installed / app-binary 证据 | 为 installed 或 app-binary 增加可控路径，使一次 run 明确执行 cognition `decide()` / `reflect()` / `ResponseBuilder`，或增加 diagnostics 证明 direct path 与 cognition path 的选择原因 | 新增或扩展 integration / package smoke，断言 cognition stage trace / diagnostics / telemetry marker 出现在结果或日志中 | `cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "CognitionRuntimeIntegrationTest|CognitionRuntimeInteractionContractTest" --output-on-failure`；installed 命令需在任务中补齐 | 不再只能用 source evidence 说明 cognition 存在；至少有 L3 或 L4 cognition-positive 证据 |
| COG-FIX-003 | Todo | 实现 Facade 阶段超时隔离 | 在 `cognition/src/CognitionFacade.cpp` 增加阶段执行 helper，按 `StageExecutionPlan.deadline_ms` / `StageModelHint.deadline_ms` 对 Perception / Planner / Reasoner / Reflection / bridge 调用做超时裁定，并返回 `cognition.stage_timeout` ErrorInfo | 新增 `CognitionFacadeStageTimeoutTest` 或扩展 failure injection，模拟慢 bridge / 慢阶段，断言 late result 不影响当前请求 | `cmake --build build-ci --target dasall_cognition_facade_stage_timeout_unit_test && ctest --test-dir build-ci -R "CognitionFacadeStageTimeoutTest" --output-on-failure` | 超时阶段 fail-fast，错误包含 stage、request_id / trace_id，且不自建 retry |
| COG-FIX-004 | Todo | 明确 LLM structured output 在主链中的角色 | 二选一：A. 实现 bridge JSON payload -> `StageOutputValidator` -> PlanGraph / ActionDecision projection；B. 在详设与代码 diagnostics 中明确 v1 bridge 只作为 advisory，不驱动主链对象 | 若选 A，新增 schema valid / invalid projection tests；若选 B，新增 regression test 证明 malformed bridge payload 不会被误当主链对象 | A: `ctest --test-dir build-ci -R "StageOutputValidatorSchemaTest|CognitionFacadeStructuredOutputTest" --output-on-failure`；B: `ctest --test-dir build-ci -R "CognitionFacadeFlowTest|CognitionLlmBridgeErrorMappingTest" --output-on-failure` | 文档、代码和 tests 对 bridge output 角色一致，不再半明半暗 |
| COG-FIX-005 | Todo | 接入 production CognitionTelemetry sink | 扩展 `CognitionRuntimeDependencies` 或 Runtime composition，使 cognition 能获得 infra telemetry sink；ResponseBuilder degraded path 也要有明确 emit 口径 | 新增 telemetry integration / mock sink test，断言 stage completed / failed / response degraded 被 emit 且 redaction 生效 | `cmake --build build-ci --target dasall_cognition_telemetry_fields_unit_test dasall_cognition_telemetry_redaction_unit_test && ctest --test-dir build-ci -R "CognitionTelemetry" --output-on-failure` | production composition 不再只能使用 no-op sink；degraded response 有可观测证据 |
| COG-FIX-006 | Todo | 回写 Gate-COG-12 后的当前状态 | 更新 `docs/architecture/DASALL_cognition子系统详细设计.md`、`docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` 和本文档 cognition 章节 | 文档检索能区分历史 baseline、当前 L2 完成、L4 未证明和后续 Todo | `rg -n "COG-GAP|COG-FIX|Gate-COG-12|cognition L4 未证明|DirectResponse|ConvergeSafe" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition docs/worklog/DASALL_开发执行记录.md docs/todos/DASALL_子系统查漏补缺专项记录.md` | cognition 当前状态跨文档一致，后续不再误读为 placeholder-only 或 production-ready |

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

cognition 子系统的实现覆盖度已经很高，当前主要问题不是“缺少 cognition 模块本体”，而是“跨 runtime 的全部 decision kind 消费、installed production 必经证据、阶段超时隔离、LLM structured output 主链角色和 production telemetry 接入”尚未全部闭合。

因此本章冻结口径为：

1. cognition 主体：基本完成，L1 / L2 证据充分。
2. runtime 非 direct cognition path：基本打通，但 ActionDecision 映射不完整。
3. installed production 主链：不能宣称 cognition 必经；当前只可记录为 source / integration evidence，L4 positive evidence 待补。
4. 查漏补缺优先级：先修 `COG-GAP-001` 和 `COG-GAP-002`，再处理 timeout、structured output 和 telemetry。

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

1. LLM 模块内部主体实现达到 L1 / L2 可信度，本轮 46 个 focused / integration / contract 测试通过。
2. Runtime production direct LLM path 已接入 `ILLMManager`，`RuntimeLiveDependencyComposition` 会创建 production LLM manager，`AgentOrchestrator` 已有 `llm.origin=` 输出和禁用 `agent.dataset` fallback 的生产直连路径。
3. `docs/ssot/BusinessChainIntegrationMatrix.md` 记录 BC-07 LLM production generation 已有 installed-package local L4 证据：`sudo -n dasall run ...` 返回 DeepSeek `llm.origin` 且未出现 `agent.dataset`。
4. D10 streaming 仍明确未实现：`LLMManager::stream_generate()` fail-closed，OpenAI / Ollama / Local adapter streaming 均为 placeholder session ref。
5. production factory 当前只自动注册 `openai_compatible` provider family；LAN / Local family 在 fixture 与 adapter skeleton 中存在，但生产注册链路未证明全量闭合。
6. metrics / trace / audit bridge 能在 fixture 中工作，但 production factory 构造 `LLMManager` 时未注入 metrics / trace bridge，audit bridge 也未进入 manager hot path；不能宣称 production observability / audit sink 完整闭合。

### 5.3 设计覆盖矩阵

| 设计域 | 当前覆盖判断 | 主要证据 | 缺口 / 边界 |
|---|---|---|---|
| D1 公共接口与 shared contract | 已覆盖 | `ILLMManager`、`ILLMAdapter`、`ILLMTransport`、`LLMRequestResponseContractTest`、`LLMInterfaceSurfaceTest` | streaming 形状存在但不代表 stream-ready |
| D2 profile -> LLM config 投影 | 已覆盖 | `project_llm_subsystem_config()`、`LLMSubsystemConfigProjectionTest`、`ProviderConfigProjectionTest`、`LLMProfileIntegrationTest` | production route family 覆盖仍取决于 factory 注册能力 |
| D3 ModelRouter | 已覆盖 | `ModelRouterPolicyTest`、`ModelRouterFallbackTest`、`ModelRouterReasoningModeSelectionTest`、`DeepSeekDualModeSelectionIntegrationTest` | canonical stage key 与部分历史 alias 需持续收敛，但当前不构成核心缺口 |
| D4 Prompt / Provider asset | 已覆盖 | `PromptAssetRepository`、`ProviderCatalogRepository`、baseline / deployment / snapshot overlay、schema / version / content hash、`LLMProviderAssetOnboardingIntegrationTest` | asset-only onboarding 已证明 openai-compatible family；非 openai family production onboarding 未证明 |
| D5 PromptComposer | 已覆盖 | template renderer、slot mapping、budget clamp、prompt identity stamping；`PromptComposerSlotMappingTest`、`PromptComposerOverBudgetTest`、smoke integration | 不负责 memory retrieve，符合 ADR-006 |
| D6 PromptPolicy | 已覆盖 | trusted source、allowlist、tool visibility、redaction、render budget；`PromptPolicyAllowlistTest`、`PromptPolicyToolVisibilityTest`、`LLMGovernanceFailureIntegrationTest` | policy sink / audit 生产热路径仍需接线证明 |
| D7 LLMManager unary 主链 | 已覆盖 | `LLMManager::generate()` 编排 PromptPipeline -> ModelRouter -> executor / fallback -> normalizer -> usage / tags；manager success/failure/timeout/retry/concurrency tests | `stream_generate()` 未实现；production observability 注入不足 |
| D8 adapters / transport | 部分覆盖 | `OpenAICompatibleAdapter` + `CurlCommandLLMTransport`；adapter health / protocol mapping tests；Ollama / Local adapter skeleton | production factory 只自动注册 `openai_compatible`；Ollama / Local production route 未证明 |
| D9 observability / integration smoke | 部分覆盖 | `LLMMetricsBridge`、`LLMTraceBridge`、`LLMAuditBridge`、`LLMSubsystemSmokeIntegrationTest`、`LLMObservabilityFieldCompletenessTest`、`LLMAuditEventCoverageTest` | fixture 可观测不等于 production sink；audit bridge 未接入 manager/factory hot path |
| D10 streaming 后置 | 未完成 / fail-closed | manager 和 adapters 均返回 not implemented / placeholder session；cognition bridge streaming preference 会进入 manager unimplemented path | 需要独立 streaming lifecycle 设计、实现和 integration gate |
| installed / production evidence | 部分覆盖 | `LLMProductionFactory`、runtime direct path、BC-07 L4 installed local `llm.origin` 证据 | L5 qemu / release runner、外部 provider 长稳态、soak / chaos 未证明 |

### 5.4 关联模块链路判断

| 链路 | 是否打通 | 当前可信层级 | 判断 |
|---|---|---:|---|
| Profiles -> LLM config | 基本打通 | L2 | typed projection、route / allowlist / timeout / fallback 在 unit 与 profile integration 中验证 |
| LLM assets -> PromptPipeline / ProviderCatalog | 基本打通 | L2 | baseline / deployment / snapshot source switch、persona selection、provider onboarding 均有 integration 证据 |
| Runtime live composition -> LLM | 已打通但 production family 部分 | L0 / L4 partial | production factory 创建 `ILLMManager` 并注入 runtime；当前自动注册 `openai_compatible` routes |
| Runtime -> LLM production generate | 已打通 | L4 local | `AgentOrchestrator` direct path 调 `llm_manager->generate()`；BC-07 记录 installed local `llm.origin` 正向证据 |
| Cognition -> LLM | 基本打通 | L1 / L2 | `CognitionLlmBridge` 可调用 `generate()` / `stream_generate()`；unary 可用，streaming 会落入未实现路径 |
| LLM -> Infra secret | 部分打通 | L0 / L4 partial | production factory 使用 `FileSecretBackend` + curl transport；secret/qemu/network/release runner 证据仍归 L5 gate |
| LLM -> Infra metrics / trace / audit | 部分打通 | L1 / L2 | bridge 类与 fixture smoke 已通过；production factory 未注入 metrics/trace，audit bridge 未进 manager hot path |
| LLM -> Memory / Tools / Recovery | 边界正确 | L0 / L2 | 未发现 LLM 直接拥有 memory context、tools execution 或 recovery decision；符合 ADR-006/007/008 |
| Access / apps -> Runtime -> LLM | 部分打通 | L3 / L4 partial | app runtime_support composition 和 installed `dasall run` 可证明 LLM direct generation；HTTP/gateway/qemu/soak 不由该证据外推 |

### 5.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| LLM-GAP-001 | High | D10 streaming lifecycle 未实现 | `LLMManager::stream_generate()` 返回 `llm manager stream_generate is not implemented in unary phase`；OpenAI / Ollama / Local adapter streaming 均返回 placeholder session id | cognition / future TUI 若设置 streaming preference 会进入 fail-closed；不能宣称 stream-ready | 增加 stream session registry、transport SSE / delta merge、adapter stream、manager lifecycle、cognition negative/positive tests |
| LLM-GAP-002 | Medium | production factory 自动注册 provider family 只覆盖 `openai_compatible` | `LLMProductionFactory` 对非 `openai_compatible` provider 直接 `continue`；fixture 中 LAN / Local adapters 已存在但 production route 未证明 | Cloud / LAN / Local fallback 的生产 family 全量闭合不能成立 | 为 Ollama / Local family 增加 production adapter factory / route registration / provider assets / fallback tests |
| LLM-GAP-003 | Medium | production metrics / trace / audit sink 接入不完整 | `LLMProductionFactory` 构造 `LLMManager` 时未传入 `LLMMetricsBridge` / `LLMTraceBridge`；`LLMAuditBridge` 主要由 tests 手动调用 | fixture observability 通过不能外推 production sink；production issue 排查缺可观测闭环 | 扩展 factory options 或 runtime composition 注入 infra logger / meter / tracer / audit logger，并增加 production factory tests |
| LLM-GAP-004 | Medium | L5 qemu / release runner / external provider 长稳态未证明 | BC-07 只有 installed-package local L4；BC-16 明确 qemu / lintian / release runner 仍需复跑 | 不能把本机 installed DeepSeek 正向 run 外推为 release-ready 或 soak-ready | 用 packaging qemu gate、external provider secret/network gate、长稳态/chaos evidence 收口 |
| LLM-GAP-005 | Low / Medium | LLM 源码边界缺少自动化回归防线 | 静态检查未发现越界，但当前缺少明确 test/script 防止 `llm/` 未来 include memory/tools/apps/runtime 私有实现 | 后续改动可能破坏 ADR-006/007/008 边界 | 增加 boundary compliance test 或脚本，锁定 LLM 不直拉 Memory ContextOrchestrator、不直调 Tools、不触碰 RecoveryManager |

### 5.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| LLM-FIX-001 | Todo | 实现 streaming 生命周期 fail-closed -> 可控可测 | 更新 `llm/src/LLMManager.cpp`、`llm/src/adapters/OpenAICompatibleAdapter.cpp`、必要时新增 `llm/src/stream/StreamSessionRegistry.*`；明确 cancellation、observer error、final usage、fallback 与 timeout 语义 | 新增 `StreamSessionLifecycleTest`、`LLMStreamingIntegrationTest`，并扩展 `CognitionLlmBridgeErrorMappingTest` 覆盖 streaming preference | `RunCtest_CMakeTools(tests=["StreamSessionLifecycleTest","LLMStreamingIntegrationTest","CognitionLlmBridgeErrorMappingTest"])` | manager/adapters 不再返回 placeholder；streaming success、provider error、cancel、timeout 均有二值测试 |
| LLM-FIX-002 | Todo | 补齐 production provider family 注册 | 更新 `LLMProductionFactory`，把 provider `adapter_family` 映射到 OpenAI-compatible / Ollama / Local 等 adapter factory；必要时补 provider asset baseline | 新增或扩展 `LLMProductionFactoryTest`、`LLMProviderAssetOnboardingIntegrationTest`、`LLMFallbackIntegrationTest` 覆盖 openai + ollama + local production registration | `RunCtest_CMakeTools(tests=["LLMProviderAssetOnboardingIntegrationTest","LLMFallbackIntegrationTest","LLMProductionFactoryTest"])` | production factory 能按 provider catalog 注册多 family routes，并证明 fallback chain 不只停留在 fixture 手工注册 |
| LLM-FIX-003 | Todo | 接入 production observability / audit sink | 扩展 `LLMProductionFactoryOptions` 或 runtime composition，将 infra logger / metrics provider / tracer / audit logger 注入 `LLMManager` 与 response normalization audit path | 扩展 `LLMSubsystemSmokeIntegrationTest` 或新增 `LLMProductionObservabilityIntegrationTest`，断言 production-composed manager 产生日志、指标、trace 和 reasoning strip audit event | `RunCtest_CMakeTools(tests=["LLMObservabilityFieldCompletenessTest","LLMAuditEventCoverageTest","LLMProductionObservabilityIntegrationTest"])` | fixture bridge 与 production factory 路径一致可观测；audit event 不再只能由 test 手动调用 |
| LLM-FIX-004 | Todo | 收口 L5 / release runner / provider 长稳态证据 | 不一定改产品代码；更新 packaging scripts、secret/network preflight、docs evidence；必要时补 installed LLM smoke 脚本 | packaging qemu / autopkgtest / installed `dasall run` / external provider failure-injection evidence | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>`；installed `sudo -n dasall run ... --json --timeout-ms 120000` | BC-07 从 L4 local 可升级到 L5 release runner / qemu；外部 provider 抖动和 secret/network failure 有记录 |
| LLM-FIX-005 | Todo | 建立 LLM 边界回归防线 | 新增 test 或 script 检查 `llm/` 不 include / link memory、tools、apps、runtime 私有实现；PromptPipeline / PromptComposer 不做 memory retrieval | `LLMBoundaryGuardComplianceTest` 或 boundary script；保留 `LLMInterfaceSurfaceTest` / `LLMRequestResponseContractTest` | `RunCtest_CMakeTools(tests=["LLMBoundaryGuardComplianceTest","LLMInterfaceSurfaceTest","LLMRequestResponseContractTest"])` | ADR-006/007/008 边界有自动化守护，后续改动不能悄悄越界 |
| LLM-FIX-006 | Todo | 回写 LLM 当前状态与历史 baseline 差异 | 更新 `docs/architecture/DASALL_llm子系统详细设计.md`、`docs/todos/llm/*`、`docs/worklog/DASALL_开发执行记录.md` 与本文档 | 文档一致性检索，区分 D1-D9 当前闭合、D10 streaming 未闭合、L4/L5 证据边界 | `rg -n "LLM-GAP|LLM-FIX|D10 streaming|openai_compatible|BC-07|llm.origin" docs/architecture/DASALL_llm子系统详细设计.md docs/todos/llm docs/worklog/DASALL_开发执行记录.md docs/todos/DASALL_子系统查漏补缺专项记录.md` | 后续评审不再把旧 placeholder baseline 当当前结论，也不把 L4 local 证据外推为 L5 / production soak |

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

LLM 子系统的实现覆盖度已经很高，当前主要问题不是“缺少 LLM 模块本体”，而是“streaming 后置能力、production family 覆盖、production observability/audit sink、release-runner 证据和边界回归防线”尚未全部闭合。

因此本章冻结口径为：

1. LLM D1-D9 主体：基本完成，L1 / L2 证据充分，本轮 46 个聚焦测试通过。
2. Runtime production LLM generation：有 L4 local installed evidence，但不外推为 L5 qemu / release runner 或 L6 soak。
3. D10 streaming：未完成，当前是明确 fail-closed / placeholder 状态。
4. production completeness：OpenAI-compatible 路径可用；LAN / Local production family、metrics / trace / audit sink 与 external provider 长稳态仍需补证据。
5. 查漏补缺优先级：先修 `LLM-GAP-001`，再收口 `LLM-GAP-002` / `LLM-GAP-003`，最后用 `LLM-GAP-004` / `LLM-GAP-005` 提升 release 与边界可信度。

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
4. VectorMemory 的 concrete `sqlite-vss` backend 未实现 / 未接线；当前只有 unavailable / no-op fallback，且 `MEM-TODO-035` 仍为 Not Started。
5. SQLite 版本基线与详设要求不一致：当前 CMake 拉取 SQLite `3460100`，而详设 MEM-C022 / config 表要求 WAL 并发场景至少 3.51.3 或 backport，并缺少运行时 version gate。
6. Observability / audit / metrics / trace 目前主要停留在错误 metadata、warnings、reports 和测试可见字段，未接入 infra production sinks。

### 6.3 设计覆盖矩阵

| 设计域 | 当前覆盖判断 | 主要证据 | 缺口 / 边界 |
|---|---|---|---|
| Public facade 与支撑类型 | 已覆盖 | `IMemoryManager`、`IContextOrchestrator`、`IMemoryStore` ISP surfaces、`MemoryConfig`、context/writeback/export/maintenance types 已落盘 | facade 不直接暴露 prompt/provider payload，符合 ADR-006 |
| WorkingMemory | 已覆盖 | `WorkingMemoryBoard`、snapshot export、writeback 更新、并发相关测试存在 | installed 长稳态 board checkpoint 不属于 memory owner，仍归 runtime checkpoint 证据 |
| Short-term Session / Turn / Summary | 已覆盖 | SQLite session / turn / summary CRUD、summary upsert、context candidate collection、contract tests | installed 多轮 reuse 可继续补正向证据 |
| Long-term Fact / Experience | 已覆盖 | fact / experience insert/query/supersede、conflict resolver、retention、writeback integration tests | 语义抽取质量仍依赖上游 runtime / cognition / LLM 输入，不由 memory 自行推理 |
| ContextOrchestrator | 已覆盖 | `CandidateCollector`、`BudgetAllocator`、`CompressionCoordinator`、`ContextPacketGuards`、slot projection、dropped section report | Memory 只产 `ContextPacket`，不渲染 Prompt；该边界需持续守住 |
| WritebackCoordinator | 已覆盖 | transactional core persist、derived data persist、working board update、conflict handling、quarantine path | vector sidecar 目前因 adapter 为空而无法形成 real index |
| SQLite store / schema / WAL | 基本覆盖 | writer connection、reader pool、`PRAGMA journal_mode` / `synchronous` / `foreign_keys` / `wal_autocheckpoint`、migrations install | SQLite version gate 缺失；reader pool 并发防护未显式证明 |
| VectorMemory | 部分覆盖 | `VectorConfig`、profile projection、unavailable adapter、vector-disabled / unavailable tests | concrete `SqliteVssVectorBackend` 未实现；factory 未接线；`sqlite-vss` third-party / packaging 未冻结 |
| Maintenance | 基本覆盖 | background worker、manual execute、WAL checkpoint、turn/fact/experience retention、quarantine cleanup、vector rebuild hook | vector rebuild 因 concrete backend 缺失只能停在 hook / unavailable 语义 |
| Profile projection | 基本覆盖 | `MemoryConfigProjector` 投影 recent turns、WAL、reader pool、maintenance、vector enabled / backend | 会投影 `SqliteVss`，但 concrete backend 不存在，需 fail-closed 或补实现 |
| Observability / audit / metrics / trace | 部分覆盖 | `MemoryError` audit metadata、assembly/writeback warnings、maintenance report、tests 断言字段 | 未见 infra logger / metric / trace / audit sink 注入和 production emit |
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
| Infra observability -> Memory | 部分打通 | L1 / L2 | Memory 内部 report / warning 可测；production sink 接入不足 |
| Packaging / installed -> Memory | 部分打通 | L4 local | installed memory DB / WAL / rows 有本机证据；qemu / release runner / soak 仍待补 |

### 6.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| MEM-GAP-001 | High | concrete `sqlite-vss` vector backend 未实现 / 未接线 | 当前仅见 `UnavailableVectorMemoryIndexAdapter`；未见 `SqliteVssVectorBackend`；`MemoryManagerFactory` 对 vector adapter 传 `nullptr`；`MEM-TODO-035` 仍 Not Started | VectorMemory 只能证明 disabled / unavailable fallback，不能证明详设中的语义向量召回、写回 sidecar 和 rebuild | 实现并接线 sqlite-vss backend，或正式冻结 v1 `VectorBackend::None` 为唯一 supported runtime path 并更新设计/TODO |
| MEM-GAP-002 | High | SQLite 版本基线与详设不一致，且缺运行时 version gate | `memory/CMakeLists.txt` 使用 `DASALL_SQLITE_AUTOCONF_VERSION 3460100`；未见 `sqlite3_libversion_number()` / `sqlite_min_version` 配置检查；详设要求 WAL 并发至少 3.51.3 或 backport | 并发 / WAL 行为的 design assumption 无法由当前依赖版本证明；installed 环境若换库也缺 fail-closed | 升级 pinned SQLite、补 `MemoryConfig.storage.sqlite_min_version` 和 store open-time version check |
| MEM-GAP-003 | Medium / High | Observability / audit / metrics / trace 未接 infra production sinks | `MemoryError` 有 audit metadata，context/writeback/maintenance 有 warnings/report；未见 Memory factory 注入 logger / meter / tracer / audit sink | production 排障和审计证据不足，不能把测试字段外推为生产观测闭环 | 增加 Memory observability bridge / narrow sink interface，并在 manager / orchestrator / writeback / maintenance emit |
| MEM-GAP-004 | Medium | L5 qemu / release runner / soak 证据缺失 | 既有 SSOT 只记录 installed local L4；qemu / release runner / soak 仍未证明 | 不能宣称 Memory production-ready 或 release-ready；只能说本机安装态写回成立 | 增加 installed package qemu gate、release preflight、WAL / migration / writeback / context reuse 证据 |
| MEM-GAP-005 | Medium | SQLite reader pool 并发防护未显式闭合 | `SqliteMemoryStore::select_reader_connection() const` 会更新 mutable `next_reader_index_`；当前未见 mutex / atomic / connection lease 保护，也未见并发压力测试覆盖该 store path | 多线程 `prepare_context()` 或并发查询可能产生 C++ data race 或共享 connection 竞争，影响详设 WAL 并发可信度 | 加锁 / atomic round-robin / per-call lease，并补 store concurrency stress test；必要时用 TSAN gate |
| MEM-GAP-006 | Low / Medium | Memory 边界回归自动化不足 | 手工检查未发现 Memory 侵入 PromptComposer / RecoveryManager / AgentOrchestrator owner，但缺专门 boundary test/script | 后续改动可能破坏 ADR-006/007/008，尤其是 context / recovery / prompt owner 边界 | 增加 `MemoryBoundaryGuardComplianceTest` 或脚本，锁定 include/link 和关键 symbol 禁区 |
| MEM-GAP-007 | Low / Medium | installed 多轮 context reuse / maintenance 正向证据可加强 | 当前 L4 证据主要证明 DB 写回、WAL 和 rows；未证明 installed 环境跨多轮 context reuse、retention / checkpoint / quarantine cleanup 的正向路径 | installed 证据对“写入后被下一轮上下文消费”和“维护任务生产运行”覆盖不足 | 增加 installed multi-turn smoke 与 maintenance smoke，记录 ContextPacket 命中、summary reuse、checkpoint / retention 结果 |

### 6.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| MEM-FIX-001 | Todo | 实现 / 冻结 vector backend 生产口径 | 若实现：新增 `memory/src/vector/SqliteVssVectorBackend.*`、更新 `memory/src/MemoryManagerFactory.cpp`、`memory/CMakeLists.txt`、third-party / packaging；若冻结：更新 `MemoryConfigProjector`、设计和 TODO，使 sqlite-vss 不再作为默认可用承诺 | `SqliteVssVectorBackendTest`、`VectorMemoryAdapterTest`、`MemoryWritebackIntegrationTest`、`MemoryProfileCompatibilityTest` | `RunCtest_CMakeTools(tests=["SqliteVssVectorBackendTest","VectorMemoryAdapterTest","MemoryWritebackIntegrationTest","MemoryProfileCompatibilityTest"])` | vector enabled 时要么 concrete backend 可用，要么明确 fail-closed 且文档不再承诺未实现能力 |
| MEM-FIX-002 | Todo | 对齐 SQLite 最低版本与运行时 gate | 更新 `memory/CMakeLists.txt` pinned version；在 `memory/include/config/MemoryConfig.h` 增加 `sqlite_min_version`；在 `memory/src/store/sqlite/SqliteMemoryStore.cpp` open 阶段检查 `sqlite3_libversion_number()` 并返回稳定错误 | `SqliteVersionGateTest`、`SqliteMemoryStoreTest`、`MemoryFailureInjectionTest` | `RunCtest_CMakeTools(tests=["SqliteVersionGateTest","SqliteMemoryStoreTest","MemoryFailureInjectionTest"])` | 低版本 SQLite fail-closed，高版本继续通过 WAL / reader pool tests，文档中的 3.51.3 / backport 口径与代码一致 |
| MEM-FIX-003 | Todo | 接入 Memory production observability sinks | 新增 `memory/include/observability/*` 或窄接口；在 `MemoryManagerFactory` / `MemoryManager` / `ContextOrchestrator` / `WritebackCoordinator` / `MemoryMaintenanceWorker` 接入 logger / metrics / trace / audit emit | `MemoryObservabilityBridgeTest`、`MemoryWritebackIntegrationTest`、`MemoryMaintenanceIntegrationTest` | `RunCtest_CMakeTools(tests=["MemoryObservabilityBridgeTest","MemoryWritebackIntegrationTest","MemoryMaintenanceIntegrationTest"])` | context assembly、writeback、conflict、compression、maintenance 的 success/failure/degraded 事件可由 production composition 注入 sink 捕获 |
| MEM-FIX-004 | Todo | 补齐 SQLite reader pool 并发防护 | 更新 `SqliteMemoryStore` reader selection / connection usage，使用 mutex、atomic round-robin 或 lease；必要时把读连接绑定到 request scope | `SqliteMemoryStoreConcurrencyTest`、`MemoryContextIntegrationTest`、TSAN 可选 gate | `RunCtest_CMakeTools(tests=["SqliteMemoryStoreConcurrencyTest","MemoryContextIntegrationTest"])` | 并发 `load_session_bundle` / fact / experience / summary 查询无 data race、无共享连接竞争导致的 flaky failure |
| MEM-FIX-005 | Todo | 增加 Memory 边界回归防线 | 新增 `tests/contract/memory/MemoryBoundaryGuardComplianceTest.cpp` 或 boundary script，检查 `memory/` 不 include / link LLM PromptComposer、Tools executor、Runtime RecoveryManager / AgentOrchestrator 私有实现 | `MemoryBoundaryGuardComplianceTest`、`ContextPacketFieldContractTest` | `RunCtest_CMakeTools(tests=["MemoryBoundaryGuardComplianceTest","ContextPacketFieldContractTest"])` | ADR-006/007/008 关键边界有自动化证据，后续改动不能悄悄越界 |
| MEM-FIX-006 | Todo | 收口 installed / qemu memory release evidence | 更新 packaging smoke / release runner / qemu 脚本与证据文档，覆盖 installed `dasall run` 写回、WAL、migration、context reuse、summary reuse | installed package smoke、qemu / autopkgtest gate、BusinessChain matrix 回写 | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>`；另补 installed memory DB 查询命令 | BC-09 / BC-10 / BC-16 中 Memory 证据可从 L4 local 升级到 L5 qemu / release runner |
| MEM-FIX-007 | Todo | 增加 installed multi-turn context / maintenance 正向证据 | 不一定改产品代码；可增加 installed smoke fixture、maintenance CLI / diag hook 或 test helper，记录多轮后 ContextPacket 命中与 maintenance report | `MemoryInstalledMultiTurnSmokeTest`、`MemoryMaintenanceInstalledSmokeTest` 或等价 package smoke | `RunCtest_CMakeTools(tests=["MemoryContextIntegrationTest","MemoryMaintenanceIntegrationTest"])`；installed 命令在任务 deliverable 中固定 | 能证明写入内容被下一轮 context 使用，并证明 checkpoint / retention / quarantine cleanup 在安装态可观测 |

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

Memory 子系统的实现覆盖度已经很高，当前主要问题不是“缺少 Memory 模块本体”，而是“concrete vector backend、SQLite version baseline、production observability sink、release-runner 证据、并发防护和边界回归自动化”尚未全部闭合。

因此本章冻结口径为：

1. Memory 主体：基本完成，L1 / L2 证据充分，本轮 37 个聚焦测试通过。
2. Runtime / app 链路：已接入 `prepare_context()` / `write_back()`；installed writeback 有 L4 local 证据，但不外推为 L5 qemu / release runner 或 L6 soak。
3. VectorMemory：当前只能证明 disabled / unavailable fallback；不能宣称 concrete sqlite-vss backend 已完成。
4. SQLite / observability / concurrency：仍需补版本 gate、production sink 和 reader pool 并发防护。
5. 查漏补缺优先级：先修 `MEM-GAP-001` / `MEM-GAP-002`，再收口 `MEM-GAP-003` / `MEM-GAP-005`，最后通过 `MEM-GAP-004` / `MEM-GAP-006` / `MEM-GAP-007` 提升 release 与边界可信度。

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
| Vector / hybrid retrieval | 部分覆盖 | `VectorRetrieverBridge`、`IQueryEncoder`、`IVectorRecallStore`、`RecallCoordinatorDenseBridgeTest` 存在；dense failure/degrade 语义可测 | no concrete production backend；installed factory `vector_enabled=false`；memory `MEM-TODO-035` 仍显示 sqlite-vss backend 未实现 |
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
| KNO-GAP-003 | High | concrete vector / hybrid production 链未闭合 | Knowledge 只有 `IQueryEncoder` / `IVectorRecallStore` / bridge；factory `vector_enabled=false`；Memory `MEM-TODO-035` 显示 sqlite-vss backend 未实现 | 详设中的 hybrid/dense semantic recall 不能宣称完成；profile vector_enabled 只能落到 degrade/fallback | 实现 Memory/Knowledge 间 concrete vector recall store 与 embedding/upsert lifecycle，或冻结 v1 lexical-only production 口径 |
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

本轮检查目标：核对 `runtime/*` 是否覆盖 `docs/architecture/DASALL_runtime子系统详细设计.md` 中规定的 Runtime 控制平面、显式 FSM、预算、Checkpoint、Recovery、Session、Scheduler、SafeMode、可观测、后台维护、跨模块依赖与 app production composition，并判断关联模块链路是否已经全部打通。

检查范围：

1. Runtime 源码与公共接口：`runtime/include/*`、`runtime/src/*`、`runtime/CMakeLists.txt`。
2. App production composition：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`。
3. Runtime integration / fixture 拓扑：`tests/integration/agent_loop/*`、`tests/fixtures/runtime/*`、`tests/integration/agent_loop/CMakeLists.txt`。
4. 相邻模块真实调用点：memory、knowledge、llm、cognition、tools、services、multi_agent 只按 runtime 源码里的实际 include、link、dependency set 字段和 call site 判断，不以历史 TODO Done 状态替代当前代码事实。

本轮执行验证：未重新运行 CMake / CTest。以下结论基于当前源码、CMake 注册和测试代码静态检查；测试文件只作为“证据形态与覆盖意图”的辅助，不把测试存在或历史通过状态外推为生产完成。

证据口径：

1. L0 source evidence 可证明 Runtime 组件与调用点是否真实存在。
2. L1 / L2 只能在看到 fixture / integration 的代码结构后说明“有可回归目标”，本轮未重新确认运行结果。
3. L3 / L4 / L5 / L6 若无当前 app-binary、installed package、qemu/release runner 或 soak 命令证据，不在本章外推。

### 10.2 总体结论

结论：`runtime/*` 已经不再是详设早期描述的 placeholder-only 状态。当前代码已经落地 `AgentFacade`、`AgentOrchestrator`、`RuntimeDependencySet`、显式 FSM / TransitionGuardTable、`BudgetController`、`CheckpointManager`、`RecoveryManager`、`SessionManager`、`Scheduler`、`SafeModeController`、`RuntimeTelemetryBridge`、`RuntimeEventBus`、`RuntimeHealthProbe` 与 `BackgroundMaintenanceHooks`，并通过 app runtime_support 在 daemon / gateway 中装配 Runtime facade。

但当前不能判定为“runtime 详设所有功能全部完成、所有关联链路全部 production-ready”。更准确的状态是：

1. Runtime 控制面主体达到较强 L0 source coverage；核心 P0 组件均有真实代码落点。
2. 非 production direct 的 live unary path 已能从 memory context、knowledge evidence、cognition decide、tools invoke、reflection、RecoveryManager、ResponseBuilder 走到 AgentResult，但该路径主要由 build-tree integration / fixture 证明，不等于 app production 默认路径。
3. daemon / gateway 的 app composition 会创建 live dependency set，但 `RuntimeLiveDependencyComposition` 写入 `required-live-baseline` evidence，`AgentOrchestrator` 因此优先进入 production direct LLM path：memory context -> `ILLMManager::generate()` -> memory writeback -> checkpoint/session；该路径明确跳过 tools、cognition reflection 和 RecoveryManager 成功链路。
4. Session 与 Checkpoint 当前是 runtime 进程内 in-memory store；resume / replay 路径虽然有 token、checkpoint schema/version 校验和 waiting state，但 `continue_from_checkpoint()` 主要合成 synthetic request 并走 direct response terminalize，不是真实重放外部等待动作或重新进入完整 cognition/tools 主循环。
5. 预算、超时、取消、调度、可观测、健康和后台维护组件都已存在，但主链接线不完整：Turn / Token / Latency 没有系统扣减，`CancellationToken` 没有 deadline 绑定到 worker/LLM/tool，Telemetry/EventBus/Health/Maintenance 没有接入 `AgentOrchestrator` 或 app production health hot path。
6. 因此 runtime 当前最高可信结论应写成“控制面主体和部分 true integration path 已实现”，不能写成“详设全部功能和生产链路已完全闭合”。

### 10.3 设计覆盖矩阵

| 设计域 | 当前覆盖判断 | 主要代码证据 | 缺口 / 边界 |
|---|---|---|---|
| 构建入口与依赖方向 | 基本覆盖 | `runtime/CMakeLists.txt` 构建真实 `dasall_runtime` sources，public link `contracts` / `profiles`，private link cognition/llm/memory/tools | `AgentOrchestrator.cpp` 直接 include 多个相邻模块 public headers，属于 source-level 编译耦合；未见 platform/backend/provider 实现直连 |
| AgentFacade public surface | 基本覆盖 | `AgentFacade::init/handle/resume/stop` 已实现；init 可 fail-closed、degraded-ready、stub-ready；resume 校验 active waiting session、checkpoint 与 token | `stop()` 仅清空 root，没有 drain / join / telemetry flush / checkpoint persist / background stop |
| RuntimeDependencySet | 基本覆盖 | 持有 memory、cognition、response_builder、tools、multi_agent、knowledge、llm；`describe_readiness()` 区分 required 与 optional ports | `multi_agent_coordinator` 字段存在但 runtime 主链未消费；services limited self-check / diagnose seam 未落入 dependency set |
| AgentOrchestrator 主控 | 部分覆盖 | `run_once()`、`continue_from_checkpoint()`、`handle_waiting_state()` 已实现；有 preflight、main loop、tool round、recovery round、terminalize traces | 同时存在 production direct LLM path、live cognition/tools path、runtime-local stub path；三者证据不能互相外推 |
| Production direct LLM path | 部分覆盖 / app 默认路径 | `has_production_llm_direct_path()` 识别 `required-live-baseline`，随后调用 memory `prepare_context()`、`llm_manager->generate()`、memory `write_back()`、checkpoint/session terminalize | 成功时跳过 cognition/tools/recovery；LLM failure 直接 Failed，没有进入 RecoveryManager / SafeModeController |
| Live cognition/tools unary path | 部分覆盖 | memory context -> optional knowledge retrieve -> cognition `decide()` -> tool manager `invoke()` -> cognition `reflect()` -> RecoveryManager / ResponseBuilder | 只把 `ExecuteAction` 作为主成功动作；`DirectResponse` / `ConvergeSafe` 等 terminal cognition decision 未按详设完整映射 |
| FSM / transition guards | 基本覆盖 | `RuntimeState` 17 状态、`TransitionGuardTable` 合法边和 checkpoint hint 表已实现；非法转移结构化拒绝 | `AgentFsm::is_terminal()` 只把 `SafeMode` 当 terminal；Completed / Failed / FailedSafe / Degraded 终态/半终态语义仍需按详设复核 |
| BudgetController | 部分覆盖 | 五维 snapshot、initialize/restore/consume/can_continue/can_replan/can_call_tool 已实现 | 主链只系统消费 ToolCall 和 context reload Replan；Turn / Token / Latency 没有系统扣减或 watchdog 绑定 |
| CheckpointManager | 部分覆盖 | build/save/load/validate/resume plan、CheckpointState mapper、`rt.schema_version` / `rt.fsm_state_enum_version` / `rt.budget_schema_version` 校验已实现 | store 是 in-memory map；未接 memory persistence backend；resume 不是 durable replay |
| SessionManager | 部分覆盖 | load/prepare/persist/bind/build_resume_seed 已实现，pending interaction 与 active checkpoint 校验存在 | 单一 `stored_snapshot_` in-memory；未接真正 session persistence；turn_index / multi-session 语义偏简化 |
| RecoveryManager / SafeMode | 部分覆盖 | evaluate/execute/apply 支持 AbortSafe、Replan、Continue、RetryStep、budget exhausted degrade；SafeModeController 使用 profile degrade policy | RecoveryManager 计划/返回 outcome，但 retry/replan 最终走 `continue_from_checkpoint()` synthetic path，未真实重新执行 tool 或重进完整主循环 |
| Scheduler / 并发模型 | 部分覆盖 | foreground/recovery/maintenance queue、worker ticket、backpressure state、drop-oldest / failed-safe recommend 已实现 | 不是真实 thread pool；`AgentOrchestrator` 同步调用 tool；无 Recovery Handler Thread、Event Dispatch Thread、Checkpoint Persist Thread |
| Timeout / CancellationToken | 部分覆盖 | `CancellationToken` 支持 cancel、deadline、atomic state | `AgentOrchestrator` enqueue 传 `CancellationToken{}` 且不绑定 deadline；LLM/tool 调用前未见 token 检查；session/step watchdog 未落主链 |
| Observability / health / maintenance | 组件存在但主链未接 | `RuntimeTelemetryBridge`、`RuntimeEventBus`、`RuntimeHealthProbe`、`BackgroundMaintenanceHooks` 可独立 publish/sample/aggregate | `AgentOrchestrator` / `AgentFacade` / daemon / gateway 未实例化这些组件或 emit transition/budget/recovery/safe-mode 事件；health 只在 access/gateway 层用 runtime readiness 字符串 |
| App production composition | 部分覆盖 | daemon / gateway 创建 `AgentFacade`，调用 `build_*_agent_init_request()`，通过 runtime_support 组合 memory/sqlite、llm、cognition、tools、multi_agent、knowledge | app production 默认选择 direct LLM path；无 app integration tests 覆盖 daemon/gateway runtime full loop；runtime_support tool manager 的 services backend 仍落 default/fallback 问题归 tools/services 章节 |
| Integration evidence | 部分覆盖 | `RuntimeUnaryIntegrationTest` 绑定 sqlite memory、cognition、tools、IDataService fake；`RuntimeResumeIntegrationTest`、`RuntimeCheckpointReplayRegressionTest`、`RuntimeSafeModeIntegrationTest`、`RuntimeHealthMaintenanceIntegrationTest` 存在 | 这些多为 build-tree / fixture / fake service 证据；本轮未运行；不能外推 L3/L4/L5/L6 |

### 10.4 关联模块链路判断

| 链路 | 是否打通 | 当前可信层级 | 判断 |
|---|---|---:|---|
| apps daemon/gateway -> Runtime | 部分打通 | L0 / L3 source boundary | main.cpp 创建 `AgentFacade`，init 成功后把 access runtime dispatch backend 指向 `runtime_facade->handle()`；但未见当前 app-binary 运行证据 |
| runtime_support -> RuntimeDependencySet | 基本打通 | L0 | live composition 创建 SQLite memory、production LLM manager、cognition engine、response builder、ToolManager、MultiAgent coordinator、installed asset knowledge service |
| Runtime -> Memory context/writeback | 基本打通 | L0 / L2 partial | `AgentOrchestrator` 多处调用 `prepare_context()` / `write_back()`；direct LLM 与 live cognition path 均有 memory 调用点 |
| Runtime -> Knowledge evidence | 部分打通 | L0 / L2 partial | `make_memory_context_request()` 在 knowledge_service 存在时调用 `retrieve()`，把 projection/ref 传入 MemoryContextRequest；installed probe 状态归 knowledge 章节，不在本章外推 |
| Runtime -> LLM | 已打通 production direct | L0 / L4 boundary | direct path 调 `llm_manager->generate()` 并在 response_text 中保留 `llm.origin`；但该路径不是完整 cognition/tools/recovery 主链 |
| Runtime -> Cognition decide/reflect/response | 部分打通 | L0 / L2 partial | live path 调 `decide()`、`reflect()`、`response_builder->build()`；但 production direct path 默认绕过，terminal decision 映射不完整 |
| Runtime -> Tools | 部分打通 | L0 / L2 partial | live path 构造 ToolRequest 并调用 `tool_manager->invoke()`；integration fixture 可走 IDataService fake | production direct LLM path 不走 tools；app production services backend 缺口归 tools/services 章节并影响 runtime 生产完整度 |
| Runtime -> RecoveryManager | 部分打通 | L0 / L2 partial | tool reflection 非 Continue 会进入 RecoveryManager evaluate/execute/apply；abort/degrade 接 SafeModeController | direct LLM failure 不走 RecoveryManager；retry/replan 执行仍 synthetic |
| Runtime -> Services | 未直接闭合 | L0 | 详设允许 limited self-check / diagnose seam；当前 `RuntimeDependencySet` 无 services seam，runtime 主要经 tools 间接触达 services | 不能宣称 runtime 自检/诊断 services 链路已落地 |
| Runtime -> MultiAgent | 仅 seam 存在 | L0 | dependency set 持有 `multi_agent_coordinator`，runtime_support 创建 coordinator | `AgentOrchestrator` 未消费该端口；阶段 J 可后置，但不能宣称 multi-agent runtime 链路完成 |
| Runtime -> Infra observability / health | 组件存在，production 未接 | L0 / L1 | runtime-private telemetry/event/health/maintenance 类存在 | 未接 app / orchestrator hot path，不能宣称 production observability 完整 |
| Runtime -> installed / qemu / soak | 证据不足 | L0 / L3 partial | 有 source-level app composition | 本章未发现当前 L4/L5/L6 full runtime design evidence |

### 10.5 缺口清单

| Gap ID | 严重级别 | 缺口 | 代码证据 | 影响 | 补缺方向 |
|---|---|---|---|---|---|
| RT-GAP-001 | High | app production 默认 direct LLM path 绕过完整 cognition/tools/recovery 主链 | `RuntimeLiveDependencyComposition` 添加 `required-live-baseline`；`AgentOrchestrator::run_once()` 优先进入 `has_production_llm_direct_path()` 分支，并记录 tool/recovery skipped | daemon/gateway 或 installed direct run 成功不能证明详设中的完整 runtime loop 已 production-ready | 明确 direct LLM path 与 default full unary path 的模式边界，或让 production gate 可选择 cognition/tools/recovery-positive path |
| RT-GAP-002 | High | live cognition path 对 terminal decision kind 映射不完整 | live path 要求 `ActionDecisionKind::ExecuteAction`，否则只有 missing context/clarification 可进 `WaitingClarify`，其余返回 “requires cognition to select an executable action” | `DirectResponse` / `ConvergeSafe` 等详设要求的 Reasoning -> Responding 第一跳会误判失败 | 补齐 terminal decision -> `IResponseBuilder.build()` -> Responding 的分支与 interaction contract tests |
| RT-GAP-003 | High | Session / Checkpoint 持久化仍为 in-memory，resume/replay 不是真实 durable replay | `SessionManager` 只保存单个 `stored_snapshot_`；`CheckpointManager` 使用 `stored_checkpoints_` map；`continue_from_checkpoint()` 构造 synthetic request 并走 direct response | 进程重启后 waiting / checkpoint / budget 恢复不可靠；无法满足 durable checkpoint / resume 设计目标 | 接入 durable memory/session/checkpoint persistence 或明确 runtime v1 in-memory 限制，并补真实 restart/resume gate |
| RT-GAP-004 | High | timeout / cancellation / budget 五维执行未全闭合 | `CancellationToken` 可绑定 deadline，但 scheduler enqueue 使用默认 token；主链只消费 ToolCall 和 Replan；Latency current 只在 consume 时更新 | STEP_TIMEOUT / SESSION_TIMEOUT / max_latency / max_turns / max_tokens 不能作为主链硬防护 | 为每次 request / worker ticket 绑定 token 和 deadline，系统扣减 Turn/Latency/Token，并把超时折叠为 RT_E_600/601/602/603 |
| RT-GAP-005 | High | RuntimeTelemetryBridge / EventBus / Health / BackgroundMaintenance 未接入主链和 app production | `AgentOrchestrator.cpp` 未引用 telemetry/event/health/maintenance；daemon/gateway health 只用 `runtime_init_result.readiness_label()` | 状态迁移、预算拒绝、恢复拒绝、safe mode、maintenance backlog 不能形成 production observability / health 证据 | 在 AgentFacade/AgentOrchestrator composition 中注入 event bus/telemetry/health provider，并注册 app health / diagnostics sink |
| RT-GAP-006 | Medium / High | Scheduler 只是同步 ticket / backpressure gate，不是详设中的执行平面弹性线程池 | `Scheduler` 只 enqueue/acquire/release；`AgentOrchestrator` 获取 worker 后同步调用 `tool_manager->invoke()` | Tool/knowledge/maintenance 阻塞仍可能卡主链；Recovery Handler / Checkpoint Persist / Event Dispatch 线程模型未实现 | 增加 worker pool / async handoff / persist queue / maintenance queue，并实现 stop drain/join |
| RT-GAP-007 | Medium / High | WaitingConfirm、高风险确认与 SkillRouter 交互点未进入 orchestrator 主链 | FSM guard table 有 WaitingConfirm 边；`AgentOrchestrator` 未见 high-risk confirmation 分支或 SkillRouter 调用；RuntimeDependencySet 无 SkillRouter seam | 详设 6.7 / 6.22 的用户确认和 skill route 只能算状态机预留，不能算功能完成 | 在 cognition action -> tool 前增加 high-risk confirmation gate 与 tools skill route seam，补 waiting confirm resume tests |
| RT-GAP-008 | Medium | RuntimeErrorCode 已定义但 AgentResult/result_code 主链仍大量使用 raw `5002..5009` 常量 | `AgentOrchestrator.cpp` 顶部定义 `kRuntimeOrchestratorSkeleton*` raw codes；`make_runtime_error()` 接收 int code | RT_E_* 域无法统一覆盖 Facade/AgentResult/Telemetry；可观测和错误分类不一致 | 用 `RuntimeErrorCode` / stable mapping 替换 raw constants，保留外部 result_code compatibility 时也要写明映射 |
| RT-GAP-009 | Medium | services limited self-check / diagnose seam 缺失 | `RuntimeDependencySet` 无 services field；runtime_support 只通过 tools `ToolServiceBridge` 间接触达 services 且 production backend 缺口另见 tools/services | 详设 6.13 的 services limited use 未落地；runtime health/diagnose 无法直接验证 services readiness | 增加受限 services diagnose/self-check seam，或在文档中正式取消 Runtime 直连 services 设计并全部归 tools/access |
| RT-GAP-010 | Medium | app-binary / installed / qemu / soak 证据不足 | app main 有 source composition；integration/apps 当前主要是 cli config tests；runtime tests 位于 build-tree agent_loop | 不能把 L0 source 和 L2 fixture 外推为 L3/L4/L5/L6 production confidence | 增加 daemon/gateway runtime-positive app tests、installed package local smoke、qemu/release runner、long-run failure injection |
| RT-GAP-011 | Medium | direct LLM path failure 不进入 RecoveryManager / SafeMode 收敛 | `llm_result` 缺 response 时直接返回 Failed；没有 reflection / recovery / safe-mode 评估 | 生产 LLM 故障不经过详设的恢复准入与降级链，和完整 runtime 控制面目标不一致 | 为 LLM direct path 增加 recovery/degrade/safe-mode 评估，或把 direct path 标为 minimal production path 不承担 full recovery claim |
| RT-GAP-012 | Low / Medium | `AgentFsm::is_terminal()` 只把 SafeMode 当 terminal，终态语义和 FSM 表存在歧义 | `Completed -> Idle` 合法，`Failed -> Degraded` / `Degraded -> SafeMode` 合法；但 `FailedSafe`、`Completed`、`Failed` 不算 terminal | 终态、半终态、可恢复态的 runtime API 解释可能不一致，影响 telemetry / checkpoint / external readiness | 明确 terminal / resumable / safe terminal 三类语义，调整 `is_terminal()` 或重命名为 `is_non_exit_safe_mode()` |
| RT-GAP-013 | Low / Medium | multi_agent seam 仅装配未消费 | runtime_support 创建 coordinator，`RuntimeDependencySet` 持字段；runtime 主链未使用 | 阶段 J 可后置，但不能在 runtime coverage 中宣称 multi_agent 链路打通 | 保持阶段 L 后置口径，补 boundary guard 防止把 field existence 写成功能完成 |

### 10.6 补缺任务建议

| Task ID | 状态 | 任务标题 | 代码目标 | 测试目标 | 建议验收命令 | 完成判定 |
|---|---|---|---|---|---|---|
| RT-FIX-001 | Todo | 区分并收口 production direct path 与 full unary path | 更新 `runtime/src/AgentOrchestrator.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`，明确 direct LLM mode / full cognition-tools mode 的 profile 或 evidence 开关；direct path 不再被当作 full runtime gate | 新增 `RuntimeProductionPathSelectionTest`，覆盖 direct LLM、full cognition-tools、missing evidence fail-closed | `RunCtest_CMakeTools(tests=["RuntimeUnaryIntegrationTest","RuntimeProductionPathSelectionTest","DaemonRuntimeLiveDependencyCompositionTest"])` | app/default gate 能明确证明选择哪条路径；direct path 成功不再被外推为 full loop 成功 |
| RT-FIX-002 | Todo | 补齐 cognition terminal decision 与 WaitingConfirm | 更新 live cognition path，支持 DirectResponse / ConvergeSafe -> ResponseBuilder -> Responding；对 high-risk tool action 进入 WaitingConfirm 并支持 resume | 扩展 `CognitionRuntimeInteractionContractTest`、新增 `RuntimeWaitingConfirmIntegrationTest` | `RunCtest_CMakeTools(tests=["CognitionRuntimeInteractionContractTest","RuntimeWaitingConfirmIntegrationTest","RuntimeUnaryIntegrationTest"])` | Reasoning -> Responding、Reasoning -> WaitingConfirm -> ToolCalling 均有自动化证据 |
| RT-FIX-003 | Todo | 实现 durable session/checkpoint persistence 与真实 resume | 接入 memory/session/checkpoint persistence seam 或 runtime-owned durable store；改造 `continue_from_checkpoint()` 重新进入必要的 cognition/tool/recovery 步骤，而不是只 synthetic direct response | `RuntimeRestartResumeIntegrationTest`、`RuntimeCheckpointReplayRegressionTest`、`RuntimeResumeIntegrationTest` | `RunCtest_CMakeTools(tests=["RuntimeResumeIntegrationTest","RuntimeCheckpointReplayRegressionTest","RuntimeRestartResumeIntegrationTest"])` | 进程重启后可 load waiting checkpoint，并按 checkpoint target state 真实恢复或明确 fail-closed |
| RT-FIX-004 | Todo | 打通 timeout / cancellation / five-dimensional budget | 在 `AgentOrchestrator` 创建 request token，绑定 session/step deadlines，传入 scheduler/LLM/tool；系统扣减 Turn/Latency/Token；补 RT_E_600~603 映射 | `CancellationTokenTest`、`BudgetControllerTest`、新增 `RuntimeTimeoutCancellationIntegrationTest` | `RunCtest_CMakeTools(tests=["CancellationTokenTest","BudgetControllerTest","RuntimeTimeoutCancellationIntegrationTest"])` | tool/LLM/memory/knowledge timeout 可被取消并映射到 RT_E_*；Turn/Latency/Token 超限能阻断主链 |
| RT-FIX-005 | Todo | 接入 runtime telemetry/event/health/maintenance hot path | 扩展 AgentFacade / AgentOrchestrator composition，创建 `RuntimeEventBus`、`RuntimeTelemetryBridge`、health signal provider、maintenance hooks；状态迁移/预算拒绝/恢复拒绝/safe-mode 都 emit | `RuntimeTelemetryBridgeTest`、`RuntimeEventBusTest`、`RuntimeHealthMaintenanceIntegrationTest`、新增 `RuntimeMainLoopTelemetryIntegrationTest` | `RunCtest_CMakeTools(tests=["RuntimeTelemetryBridgeTest","RuntimeEventBusTest","RuntimeHealthMaintenanceIntegrationTest","RuntimeMainLoopTelemetryIntegrationTest"])` | 主链不再只有 trace vector；production-composed runtime 可观测事件和 health snapshot 可捕获 |
| RT-FIX-006 | Todo | 实现 scheduler worker pool 与 graceful stop | 改造 `Scheduler` / `AgentFacade::stop()` / checkpoint persist path，增加 worker pool、recovery queue、maintenance queue、event dispatch、checkpoint persist drain/join | `SchedulerTest`、新增 `RuntimeSchedulerConcurrencyIntegrationTest`、`RuntimeGracefulStopTest` | `RunCtest_CMakeTools(tests=["SchedulerTest","RuntimeSchedulerConcurrencyIntegrationTest","RuntimeGracefulStopTest"])` | stop(timeout) 会 drain/join/flush；foreground/recovery/maintenance backpressure 有真实并发证据 |
| RT-FIX-007 | Todo | 统一 RuntimeErrorCode 与 AgentResult/result_code 映射 | 替换 raw 5002~5009 或建立 `RuntimeResultCodeMapper`；所有 runtime error_info 记录 RT_E_*，外部 compatibility code 单独映射 | `RuntimeErrorCodeTest`、新增 `RuntimeErrorMappingIntegrationTest` | `RunCtest_CMakeTools(tests=["RuntimeErrorCodeTest","RuntimeErrorMappingIntegrationTest","RuntimeTelemetryBridgeTest"])` | AgentResult、RecoveryApplyResult、Telemetry 里的 runtime 错误码域一致可分类 |
| RT-FIX-008 | Todo | 明确并实现 services limited diagnose seam | 二选一：A. 在 `RuntimeDependencySet` 增加 services diagnose/self-check seam；B. 更新详设和 runtime_support，明确 runtime 不直连 services，全部经 tools/access | A 新增 `RuntimeServicesDiagnoseIntegrationTest`；B 新增 boundary guard / docs check | A: `RunCtest_CMakeTools(tests=["RuntimeServicesDiagnoseIntegrationTest","CapabilityServicesHealthIntegrationTest"])`；B: `rg -n "services limited|services diagnose|RuntimeDependencySet" docs runtime apps` | services 关系不再处于“详设有、代码无”的状态 |
| RT-FIX-009 | Todo | 建立 app / installed / qemu runtime-positive evidence | 新增 daemon/gateway app runtime smoke、installed local smoke、qemu/release runner gate，分别覆盖 direct path 与 full unary path | `DaemonRuntimeFullUnarySmokeTest`、`GatewayRuntimeFullUnarySmokeTest`、installed smoke script | `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","DaemonRuntimeFullUnarySmokeTest","GatewayRuntimeFullUnarySmokeTest"])`；`sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | runtime 证据能从 L2 升级到 L3/L4/L5；direct/full path 分账记录 |
| RT-FIX-010 | Todo | 固定 multi_agent stage-L 边界与 guard | 保持 `RuntimeDependencySet` multi_agent seam，但在 readiness / telemetry / docs 中明确阶段 J 不宣称 multi_agent ready；阶段 L 再接 orchestrator path | `RuntimeDependencySetReadinessTest`、新增 boundary wording guard | `RunCtest_CMakeTools(tests=["RuntimeDependencySetReadinessTest"])`；`rg -n "multi_agent.*ready|multi-agent.*enabled" docs runtime tests` | field existence 不再被误读为 runtime multi-agent 链路完成 |

### 10.7 非缺口但需守住的边界

以下事项当前不应被当成缺口，后续实现时必须继续守住：

1. Runtime 继续拥有全局主控、FSM、RecoveryManager、CheckpointManager、SafeModeController 的执行控制权，不能把恢复准入回推给 cognition 或 tools。
2. Runtime 不拥有 ContextOrchestrator；memory `prepare_context()` / `write_back()` 仍是上下文装配与写回的 owner。
3. Runtime 不拥有 PromptRegistry / PromptComposer / provider payload；direct LLM path 也必须只经 `ILLMManager`，不得直连 provider adapter。
4. Runtime 不绕过 `IToolManager` / PolicyGate 直接执行服务或平台动作；高风险 action 仍要经 tools/services 治理。
5. ReflectionDecision 继续是 cognition suggestion-only；Runtime / RecoveryManager 组合预算、checkpoint、幂等和 side-effect 事实后才裁定恢复。
6. runtime-local stub / fixture path 可以用于控制面回归，但不能作为 true integration、app production 或 installed evidence。
7. multi_agent 在阶段 J 保持可选 seam / disabled adapter 口径；不因 dependency set 字段存在就宣称协同闭环完成。

### 10.8 建议复验命令

本节命令用于后续把本章结论升级或关闭缺口。若使用 VS Code CMake Tools，应优先选择 `ListTests_CMakeTools` / `RunCtest_CMakeTools` 执行；以下 shell 形式仅作为文档化验收口径。

Runtime 控制面聚焦回归：

```bash
ctest --test-dir build/vscode-linux-ninja --output-on-failure -R "RuntimeControlPlaneSurfaceTest|RuntimeErrorCodeTest|CancellationTokenTest|AgentFsmTest|BudgetControllerTest|CheckpointManagerTest|RecoveryManagerTest|SchedulerTest|TransitionGuardTableTest|CheckpointStateMapperTest|SessionTypeSurfaceTest|SessionManagerTest|SafeModeControllerTest|AgentOrchestratorSkeletonTest|AgentOrchestratorControllerAssemblyTest|RuntimeTelemetryBridgeTest|RuntimeEventBusTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest"
```

Runtime agent loop / integration 复核：

```bash
ctest --test-dir build/vscode-linux-ninja --output-on-failure -R "RuntimeUnaryFixtureIntegrationTest|RuntimeUnaryIntegrationTest|RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest|RuntimeCheckpointReplayCompatibilityTest|RuntimeProfileCompatibilityTest|RuntimeSafeModeIntegrationTest|RuntimeHealthMaintenanceIntegrationTest|RuntimeRequiredOptionalPortsIntegrationTest|RuntimeEvidenceProjectionIntegrationTest|RuntimePolicyConsumerIntegrationTest|RuntimeRecoveryContextIntegrationTest"
```

源码链路与缺口复核：

```bash
rg -n "required-live-baseline|has_production_llm_direct_path|llm_manager->generate|cognition_engine->decide|tool_manager->invoke|response_builder->build|recovery_manager_\.evaluate|CancellationToken\{\}|RuntimeTelemetryBridge|RuntimeHealthProbe|BackgroundMaintenanceHooks|stored_snapshot_|stored_checkpoints_" \
  runtime/include \
  runtime/src \
  apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp \
  apps/daemon/src/main.cpp \
  apps/gateway/src/main.cpp
```

app / installed / release 证据口径：

```bash
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>
```

### 10.9 当前章节结论

Runtime 子系统的当前实现覆盖度已经明显高于早期详设的 placeholder 口径，控制面主体不是主要缺口。当前主要问题是“多条路径的证据边界、production 默认路径、持久化恢复、超时取消、调度线程模型、可观测健康接线和 release evidence”尚未全部闭合。

因此本章冻结口径为：

1. Runtime 主体：AgentFacade、AgentOrchestrator、FSM、Budget、Checkpoint、Recovery、Session、Scheduler、SafeMode 等已具备真实代码落点，L0 source coverage 高。
2. 关联模块链路：memory、knowledge、llm、cognition、tools 均有 source-level call site；其中 full cognition/tools/recovery 链主要停留在 build-tree live path / integration evidence，不是 daemon/gateway 默认 production direct path。
3. Production / installed：daemon/gateway 已装配 Runtime facade 与 live dependencies，但默认 direct LLM path 只能证明 minimal production answer path，不能证明完整 runtime design path。
4. 恢复可靠性：Checkpoint/Session/Resume 语义存在，但 in-memory 与 synthetic resume 限制使其还不能承担 durable recovery claim。
5. 查漏补缺优先级：先修 `RT-GAP-001`、`RT-GAP-003`、`RT-GAP-004`、`RT-GAP-005`，再收口 `RT-GAP-002`、`RT-GAP-006`、`RT-GAP-007`、`RT-GAP-008`，最后通过 `RT-GAP-009`、`RT-GAP-010`、`RT-GAP-011`、`RT-GAP-012`、`RT-GAP-013` 提升 production、release 与边界可信度。
