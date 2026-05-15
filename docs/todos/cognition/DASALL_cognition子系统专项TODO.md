# DASALL cognition 子系统专项 TODO

最近更新时间：2026-05-15
阶段：Detailed Design -> Special TODO
适用范围：cognition/
当前结论：认知详设已经具备 L3/L2 混合粒度拆分条件；COG-TODO-001 已把 `ICognitionEngine` 公共口径收敛为 `decide()` / `reflect()` / `IResponseBuilder::build()` 三入口，COG-BLK-001 已解阻；COG-TODO-002 已把 cognition↔llm stage taxonomy 收敛为 `planning/execution/reflection/response` canonical key 与 StageModelHint 映射表，COG-BLK-002 已解阻；COG-TODO-003 已把 runtime↔cognition caller fixture 与 ActionDecision→FSM 第一跳口径收敛到 Runtime 真实 FSM 状态 / guard table，COG-BLK-003 已解阻；COG-TODO-004 已冻结 `MockLLMManager`、`MockCognitionFixture`、failure/profile smoke fixture 的测试支撑口径，前置补设计 / 评审门禁已完成；COG-TODO-024 已新增真实 `MockLLMManager` / `MockCognitionFixture` header 与 narrow surface test，COG-BLK-004 已关闭；COG-TODO-020 已新增 `CognitionLlmBridge` 私有 owner 与 focused bridge tests；COG-TODO-021 已新增 `StageOutputValidator` 私有 owner、schema / graph / response focused tests，并把阶段输出合法性收口为 fail-closed 校验；COG-TODO-022 已新增 `CognitionTelemetry` 私有 owner、统一 stage fields、redaction 和 sink failure fail-open tests；COG-TODO-023 已把 `CognitionFacade` 从 stub 收敛为实际 orchestration owner，并以 flow / degraded / invalid focused tests 验证三入口闭环；cognition integration discoverability 继续由 COG-TODO-025 收口。

评审后补充结论（2026-04-27）：COG-TODO-001 ~ 030 构成当前可执行基线，但代码评审发现 bridge 主链接入、profile 投影闭环、Runtime 语义信号消费、结构化 schema 校验和 integration 证据可信度仍存在生产可用性缺口；本次新增 COG-TODO-031 ~ 038 作为后续补强任务，不改变 001 ~ 030 的历史完成状态。

复验后补充结论（2026-04-28）：对 COG-TODO-031 ~ 038 的 Build 评审复验发现，统一验收命令当前仍不可通过，主要阻断包括 `RuntimeCognitionLoopSmokeTest` 未被 `dasall_unit_tests` 聚合构建、runtime 最小 init fixture 缺少 cognition canonical stage routes、runtime+cognition response bridge / template fallback 口径不一致，以及 `ReflectionRequest.active_plan` 设计语义未进入生产 façade 路径。本次新增 COG-TODO-039 ~ 044 作为 Gate-COG-12 复验修复任务；它们不回写否定 001 ~ 038 的历史执行记录，但在 039 ~ 044 完成前，不应宣称 cognition 具备可合并的完整验收证据。

方案 A 回写补充结论（2026-05-15）：`COG-GAP-004` 的 structured projection authoritative consumption 方案 A 已由 `COG-FIX-004A-DOC-001` 冻结到认知详设 6.13.4 / 13.2 与本专项 TODO；本次新增 `COG-FIX-004A-BLD-001` ~ `012` 和 `Gate-COG-FIX004A-01` ~ `05` 作为后续 build / evidence 任务，不回写否定 `COG-TODO-020` / `021` / `023` / `031` / `035` 的历史完成状态，而是在其基础上把 bridge structured payload 推进为 `PlanGraph` / `ActionDecision` 的主链对象来源。

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_cognition子系统详细设计.md
2. docs/architecture/DASALL_Agent_architecture.md
3. docs/architecture/DASALL_Engineering_Blueprint.md
4. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
5. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
6. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
7. docs/ssot/InfraConcurrencyPolicy.md
8. docs/ssot/InfraIntegrationTopology.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. docs/todos/contracts/deliverables/WP05-T011-接口候选清单.md
12. docs/todos/contracts/deliverables/WP05-T012-接口准入评估单.md
13. docs/todos/llm/deliverables/LLM-TODO-035-profile-diff-integration设计收敛.md
14. docs/todos/tools/DASALL_tools子系统专项TODO.md
15. docs/todos/memory/DASALL_memory子系统专项TODO.md
16. docs/todos/profiles/DASALL_profiles子系统专项TODO.md
17. 当前代码与测试现状：cognition/CMakeLists.txt、cognition/src/placeholder.cpp、tests/unit/cognition/CMakeLists.txt、tests/integration/CMakeLists.txt、tests/unit/runtime/RuntimeSmokeTest.cpp、tests/mocks/include/MockLLMAdapter.h、tests/mocks/include/MockMemoryStore.h、tests/mocks/include/MockTool.h、contracts/include/boundary/InterfaceCatalog.h、profiles/include/RuntimePolicySnapshot.h

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不越过 cognition 子系统边界扩张到无关模块。
3. 不把讨论类、评审类事项伪装成 Done-ready Build 任务。
4. 每项任务必须同时包含代码目标、测试目标、验收命令。
5. 设计证据不足处先列补设计或评审前置项，不伪造函数级实现任务。
6. 不提前把 PlanGraph、ReplanResult、ActionDecision、BeliefUpdateHint 推入 shared contracts。

## 2. 子系统目标与范围

### 2.1 子系统目标

1. 将 cognition 从占位静态库收敛为 Layer 5 Cognition Layer 的稳定语义决策面，保持 Runtime 主控权、Memory 上下文权、LLM Prompt 治理权和 RecoveryManager 恢复准入权不变。
2. 建立 Perception、Planner、Reasoner、Reflection、Response Builder 五段认知链路，并通过 CognitionFacade 对 Runtime 暴露稳定的决策、反思与终态构造入口。
3. 复用既有 GoalContract、BeliefState、ContextPacket、Observation、ReflectionDecision、AgentResult、ErrorInfo / ResultCode 等 frozen contracts，在 cognition 内以模块公共类型承接 PlanGraph、ActionDecision、BeliefUpdateHint、StageModelHint 等未冻结 supporting objects。
4. 为 cognition/include、cognition/src、tests/unit/cognition、tests/integration/cognition、tests/mocks/include、CMake 接线和 Gate 收口提供可执行工程计划，确保后续可以按最小原子任务直接推进。

### 2.2 范围边界

纳入本专项 TODO 的对象：

1. cognition 公共接口面、模块公共类型与配置投影。
2. CognitionFacade、StagePolicyResolver、CognitionConfigProjector、InputBoundaryValidator。
3. PerceptionEngine、Planner、Reasoner、ReflectionEngine、BeliefUpdateSynthesizer、ResponseBuilder。
4. CognitionLlmBridge、StageOutputValidator、CognitionTelemetry。
5. tests/unit/cognition、tests/integration/cognition、tests/mocks/include 的认知专项支撑、discoverability 和 Gate。
6. Runtime↔Cognition、Cognition↔LLM 的交互口径收敛与认知专项交付证据回写。

不纳入本专项 TODO 的对象：

1. Runtime 主状态机、RecoveryManager、CheckpointManager、AgentOrchestrator 的实现。
2. Memory 的 ContextOrchestrator、摘要压缩、写回事务实现。
3. LLM 的 PromptRegistry、PromptComposer、PromptPolicy、Provider Adapter 实现与 provider catalog。
4. ToolManager、ExecutionService、KnowledgeService、platform / infra 的生产实现。
5. shared contracts 的新 admission，除非已有单独 contracts 评审结论。
6. Response streaming 首版落地；当前仅允许单次响应 + 模板降级路径。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| COG-TC001 | 认知详设 1.1、2.1；架构 4.3、5.8 | Must | cognition 必须保持为语义决策面，只输出“该做什么”和“为什么”，不拥有控制平面 | 任务不得把线程、重试、超时、补偿或结果提交写进 cognition |
| COG-TC002 | 认知详设 1.2、6.2；架构 4.3、5.8；蓝图 3.4 | Must | Perception、Planner、Reasoner、Reflection、Response Builder 五段链路必须全部有工程落点 | 任务拆分必须覆盖五段主链与门面，不得只做单体引擎 |
| COG-TC003 | ADR-006 3.2、3.3；认知详设 2.1、6.1 | Must | ContextPacket 语义装配权仍归 memory，Prompt 选择与消息装配仍归 llm | 认知任务不得引入 MemoryStore retrieve 或 PromptComposer 类实现 |
| COG-TC004 | ADR-007 3.2、3.4；认知详设 2.1、6.9.1 | Must | ReflectionEngine 只输出 suggestion-only ReflectionDecision，恢复准入与执行归 Runtime | 反思任务不得添加 retry counter、backoff、checkpoint 或补偿执行字段 |
| COG-TC005 | 架构 4.5、5.2；工具详设 2.1；认知详设 6.1 | Must | cognition 不得直调 tools / services / knowledge 实现，只能经 Runtime 间接生效 | Reasoner 只能输出 ActionDecision / tool_intent_hint，不得生成 ToolRequest |
| COG-TC006 | profiles 详设 6.9；认知详设 2.1、6.10 | Must | cognition 五档 profile 必开，差异只能通过配置投影与 DI 落地 | 必须拆出 CognitionConfigProjector / StagePolicyResolver，禁止主流程散落 profile 分支 |
| COG-TC007 | 蓝图 3.4、4.2；认知详设 1.2、6.1 | Must | cognition 依赖方向只能指向 contracts、llm 公共接口和 infra 观测抽象 | 代码目标限定在 cognition/tests/docs，不得 include tools/memory/platform 实现头 |
| COG-TC008 | InterfaceCatalog.h；WP05-T011；WP05-T012；认知详设 2.2、6.5.2 | Must | `IPlanner` 仍处于 AwaitingSupportingContracts，PlanGraph / ReplanResult / ActionDecision 等 supporting types 继续保持模块公共而非 shared contracts | 不生成 contracts admission 任务；所有实现保持在 cognition/include 与 cognition/src |
| COG-TC009 | ADR-008；认知详设 2.3、6.12、6.13 | Must-Not | cognition 不得形成第二套 orchestrator 或 workflow engine | CognitionFacade 只做阶段协调，不做总调度或多 Agent 主控 |
| COG-TC010 | 认知详设 6.15 | Must | cognition 采用请求级无状态 + 单请求串行阶段模型 | 任务不得在门面中引入跨请求可变共享状态或阶段并行 |
| COG-TC011 | 认知详设 6.14.3、6.15.3；ADR-007 | Must | cognition 失败必须 fail-fast 返回 ErrorInfo，不自建本地 retry/circuit breaker | CognitionLlmBridge / CognitionFacade 任务必须以错误映射、阶段超时隔离为验收基线 |
| COG-TC012 | 认知详设 6.14.2；llm deliverable 035；RuntimePolicySnapshot.h | Must | cognition↔llm 的阶段命名与 StageModelHint 投影必须和 llm 真实 stage key 统一，不能在测试里偷偷做第二套映射 | LLM bridge、profile compatibility、StageModelHint 相关任务必须先补设计对齐 |
| COG-TC013 | llm 详设 13.2、13.5、15.6；认知详设 2.3、6.11 | Must | provider-private 字段如 `reasoning_content` 不得穿透到 cognition 公共接口、日志、长期历史或 shared contracts | Bridge、Validator、Telemetry 任务必须覆盖 redaction 与 fail-closed |
| COG-TC014 | 工程规范 3.6、3.7 | Must | 模块边界错误必须显式可观测；新增公共接口至少补 1 个 unit 或 contract 测试 | 每个任务都必须绑定明确测试目标与命令 |
| COG-TC015 | 工程落地指引 阶段 I；SSOT `InfraIntegrationTopology` | Must | cognition 进入核心链路后必须补至少 1 条 integration smoke，并保证 `ctest -N` 可发现 | tests/integration/cognition 拓扑与 smoke gate 必须显式成任务 |
| COG-TC016 | 当前代码现状；认知详设 3.1、11.2 | Must | 当前 cognition 仍是 placeholder，tests/unit/cognition 空白，runtime smoke 仍绕过 cognition | 首批任务必须先替换骨架与测试入口，不能直接宣称主链集成 ready |
| COG-TC017 | 当前代码现状；认知详设 11.2 COG-B02 / COG-B03 | Must | runtime/include 已具备 RuntimeDependencySet / FSM 等公共面；RuntimeCognitionLoopSmoke caller fixture 已由 COG-TODO-003 收敛，MockLLMManager / MockCognitionFixture 设计口径已由 COG-TODO-004 收敛，真实 header 仍待落盘 | Runtime 交互与故障注入任务必须先清 caller fixture 与 mock 支撑缺口 |
| COG-TC018 | 认知详设 6.14.4、6.16.3；ADR-006 | Must-Not | BeliefUpdateHint 写回和上下文重装配由 Runtime / Memory 裁定，cognition 只能发信号 | 不生成 cognition 直接写 memory 或直接 reload context 的任务 |

### 3.2 当前代码与测试现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| cognition/CMakeLists.txt | `dasall_cognition` 当前编译 `src/CognitionFacade.cpp`，并通过 public header file set 登记 cognition 公共头 | cognition 已退出 placeholder-only 状态；unit discoverability 仍由 COG-TODO-006 接线 |
| cognition/src/placeholder.cpp | 已由 COG-TODO-005 删除，CMake 源列表不再引用 placeholder | 后续不得用 placeholder 冒充生产骨架 |
| cognition/include | 已落盘 `ICognitionEngine.h`、`IResponseBuilder.h`、`IPlanner.h`、`IReasoner.h`、`IReflectionEngine.h`、`CognitionConfig.h`、`CognitionTypes.h` 等承载头 | 公共 include 根已建立；字段/接口签名冻结继续由 COG-TODO-007 ~ 010 推进 |
| tests/unit/cognition/CMakeLists.txt | 仅有 placeholder 注释 | cognition 单测拓扑尚未接线 |
| tests/integration/CMakeLists.txt | 当前只接入 infra / profiles / platform / services / tools / llm | cognition integration 拓扑完全缺失 |
| tests/unit/runtime/RuntimeSmokeTest.cpp | 当前只通过 `MockLLMAdapter`、`MockMemoryStore`、`MockTool` 验证 smoke，不经过 cognition | runtime 当前仍处于“绕过 cognition 的冒烟模式” |
| tests/mocks/include | 已有 `MockLLMAdapter.h`、`MockLLMManager.h`、`MockCognitionFixture.h`、`MockMemoryStore.h`、`MockTool.h`、`CapabilityServicesLoopbackFixture.h`、`MCPLoopbackServerFixture.h` | 通用脚手架与 cognition-specific mock seam 已落盘；integration discoverability 仍待 COG-TODO-025 / 026 / 028 / 029 消费 |
| runtime/include | 已存在 RuntimeDependencySet、FSM、recovery、budget、session 等公共头；但 cognition caller fixture 尚未成为专用测试门 | runtime↔cognition 具备接线基础，仍需 COG-TODO-026/027 用专用 smoke / contract 验证真实消费 |
| contracts/include/boundary/InterfaceCatalog.h | `IPlanner` 被标记为 `AwaitingSupportingContracts` | 不应在本专项内推动 planner / action / plan supporting types 进入 shared contracts |
| profiles/include/RuntimePolicySnapshot.h | 已冻结 `model_profile.stage_routes`、`token_budget_policy`、`prompt_policy`、`timeout_policy`、`degrade_policy` 等策略快照输入面 | CognitionConfigProjector 可直接以 RuntimePolicySnapshot 作为唯一外部投影视图 |
| tests/contract/CMakeLists.txt | 已注册 `GoalContractFieldContractTest`、`BeliefStateContractTest`、`ObservationContractTest`、`ContextPacketFieldContractTest`、`ReflectionDecisionContractTest`、`AgentResultContractTest`、`MainFlowContractE2ETest` | cognition 可直接复用现有 shared contracts gate，无需另建 shared contract 测试机制 |

## 4. 粒度可行性评估

### 4.1 总体结论

结论：当前可直接生成 L3 / L2 混合专项 TODO，不能整体按纯 L3 推进。

当前最细可安全落盘粒度：

1. L3：CognitionStepRequest / CognitionDecisionResult / ReflectionRequest / CognitionReflectionResult / ResponseBuildRequest / ResponseBuildResult、PlanGraph / PlanNode / ReplanResult、ActionDecision、BeliefUpdateHint、BudgetContext、ContextSufficiencySignal、StageModelHint，以及 `ICognitionEngine` / `IResponseBuilder` / `IPlanner` / `IReasoner` / `IReflectionEngine` 的接口定义任务。
2. L2：CognitionConfig / StageExecutionHints / StageExecutionPlan、CognitionConfigProjector、StagePolicyResolver、InputBoundaryValidator、五段阶段组件、CognitionLlmBridge、StageOutputValidator、CognitionTelemetry、CognitionFacade。
3. L0：`ICognitionEngine` 公开口径与架构草图差异、cognition↔llm stage taxonomy 对齐、runtime↔cognition caller fixture、cognition-specific mock seam。

判断依据：

1. 认知详设已给出核心接口签名、请求/结果对象字段、主流程、异常流程、目录建议、测试出口和 Design -> Build 映射。
2. PlanGraph、ActionDecision、BeliefUpdateHint、StageModelHint、BudgetContext 等关键对象已经具备可追溯字段定义，满足数据结构 / 接口级拆分条件。
3. 五段组件卡片已经明确职责、非职责边界、核心数据、关键执行流、失败语义与测试出口，可以支撑组件级原子任务。
4. 当前真正阻碍函数 / 接口级拆分的，不是主链职责不清，而是三条跨文档和跨模块接缝仍未统一：公共接口口径、llm stage taxonomy、runtime caller seam。
5. 因此专项 TODO 可以直接进入执行，但必须先完成 001~003 的补设计 / 评审门禁，再推进跨模块接线与 bridge/profile/integration 任务。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| `CognitionStepRequest` / `CognitionDecisionResult` / `ReflectionRequest` / `CognitionReflectionResult` / `ResponseBuildRequest` / `ResponseBuildResult` | 6.6.1、6.6.2 | L3 | 字段、结果语义、错误出口、目录建议完整；COG-TODO-001 已统一 Runtime-facing 三入口口径 | 无 001 相关缺口；后续仍需按 module-public 落盘字段 | 可直接拆对象定义任务 |
| `PlanGraph` / `PlanNode` / `ReplanResult` | 6.5.3、6.13.2 | L3 | 字段、DAG 约束、revision 规则、测试出口明确 | shared admission 暂不成熟，但不阻断 module-local 落盘 | 直接拆 module-public 类型任务，不推动 contracts |
| `ActionDecision` / `BeliefUpdateHint` | 6.5.3、6.13.2、6.13.3、6.14.4 | L3 | 字段、rationale/confidence、merge mode、写回协议已明确；COG-TODO-003 已冻结 ActionDecision→FSM 第一跳映射与 caller fixture | 无 003 相关缺口 | 继续保持 module-local，并由后续 runtime smoke / interaction contract 验证 |
| `StageModelHint` / `BudgetContext` / `ContextSufficiencySignal` | 6.14.2、6.14.5、6.16 | L3 | 字段与约束明确；COG-TODO-002 已冻结 `planning/execution/reflection/response` canonical stage key 与 cognition 组件映射表 | 无 002 相关缺口 | 可直接定义对象 |
| `ICognitionEngine` / `IResponseBuilder` / `IPlanner` / `IReasoner` / `IReflectionEngine` | 6.6.1、6.6.3 | L3 | 接口名、方法名、输入输出、模块归属明确；COG-TODO-001 已确认 `ICognitionEngine::decide()` / `reflect()` 与 `IResponseBuilder::build()` 为唯一可执行口径 | 无 001 相关缺口 | 可在 COG-TODO-010 冻结接口 |
| `CognitionConfig` / `StageExecutionHints` / `StageExecutionPlan` | 6.10、6.13.1、6.15、6.16 | L2 | 配置键、默认值、阶段策略来源与超时/预算规则明确 | 结构字段未完全成表 | 以配置投影和阶段策略组件任务落地，不强行细化到字段级 |
| `CognitionConfigProjector` | 6.13.1、8.1、8.2 | L2 | 依赖 `RuntimePolicySnapshot`、配置投影职责和测试出口明确；stage taxonomy 已由 COG-TODO-002 统一 | `StageExecutionPlan` supporting fields 未完全成表 | 可拆实现，并在 projector 边界处理 legacy profile-source alias |
| `StagePolicyResolver` | 6.13.1、6.15.3、6.16 | L2 | 启停规则、deadline、plan cap、clarification threshold 已明确 | `StageExecutionPlan` supporting fields 未完全成表 | 以组件级任务推进，不继续拆私有 helper |
| `InputBoundaryValidator` | 6.7、6.8、6.9、7.1 COG-D03 | L2 | invalid input、missing belief state、schema violation 等边界已有明确定义 | 具体 helper 集合未成表 | 直接按组件收口输入边界 |
| `PerceptionEngine` | 6.13.2 | L2 | 核心数据、关键执行流、规则降级和测试出口明确 | `PerceptionRequest` 等内部 helper 未单独落盘 | 直接按组件任务推进 |
| `Planner` | 6.13.2、6.16.2 | L2 | build_plan / replan 语义、budget 收紧规则、PlanGraph 约束明确 | 内部 request helper 未单独成表 | 直接按组件任务推进，并绑定 plan/budget 测试 |
| `Reasoner` | 6.13.2、6.14.1 | L2 | candidate scoring、clarification threshold、response outline 已明确；runtime FSM 消费接缝已由 COG-TODO-003 收敛 | delegate hint 首版仍关闭，后续如启用需另走交互契约扩展 | 组件实现与 runtime 交互契约分成两类任务 |
| `ReflectionEngine` | 6.13.3、6.9.1 | L2 | 失败分类、goal gap、belief invalidation、suggestion-only 约束明确 | 无额外设计缺口 | 直接按组件任务推进 |
| `BeliefUpdateSynthesizer` | 6.13.3、6.14.4 | L2 | delta 分类、evidence refs、merge mode、best-effort 写回协议明确 | runtime 写回 fixture 尚未冻结 | 组件实现先行，写回时序在交互契约任务收口 |
| `ResponseBuilder` | 6.13.3、10.2 | L2 | llm/build/template fallback、AgentResult 映射、redaction 出口明确 | streaming 不在当前范围 | 直接按单次响应 + 模板降级路径推进 |
| `CognitionLlmBridge` | 6.13.4、6.14.2、6.14.3 | L2 | 调用映射、失败投影、budget hint、test outlet 明确；canonical stage key 已由 COG-TODO-002 统一；MockLLMManager 设计口径与真实 header 已由 COG-TODO-004、024 收口 | 无新增前置阻断；后续 021 / 023 继续消费 bridge supporting structs | 组件 owner 已落盘，可直接推进 validator / façade 串联 |
| `StageOutputValidator` | 6.13.4 | L2 | required fields、enum、numeric bounds、plan graph / response invariants 明确；bridge supporting structs 已可直接复用 | 无新增前置阻断；后续 façade 只消费 validator 结果，不回退为隐式通过 | 组件 owner 已落盘，可直接推进 telemetry / façade 串联 |
| `CognitionTelemetry` | 6.11、6.13.4 | L2 | 日志/指标/trace/audit 字段、redaction 和 fail-open 规则明确；统一 stage fields 已落盘 | 无新增前置阻断；HealthProbe 仍按 OQ05 延后 | 组件 owner 已落盘，可直接推进 façade 串联 |
| `CognitionFacade` | 6.13.1、6.7、6.8 | L2 | 三入口流程、错误收口、降级语义、测试出口明确；bridge / validator / telemetry supporting owner 已可直接消费 | 无新增前置阻断；response public surface 继续由独立 `IResponseBuilder` 持有 | façade owner 已落盘，可直接进入 runtime smoke / interaction contract |
| `RuntimeCognitionLoopSmoke` / `CognitionRuntimeInteractionContract` | 7.1 COG-D09 / D12、8.2~8.3、6.14 | L0 | 目标测试名、消费场景和检查点明确；COG-TODO-003 已冻结 caller fixture 与 FSM 第一跳口径；COG-TODO-004 已冻结 mock seam 设计口径 | cognition-specific mock header 与 integration topology 尚缺 | 先做 024、025，再推进 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| 公共接口口径、stage taxonomy、runtime caller seam 收敛 | 6.6、6.14、11.2 | 补设计 / 评审门禁 | COG-TODO-001 ~ 004 | 先消除跨文档与跨模块接缝冲突，再推进 Build |
| cognition include 布局、请求/结果对象、计划/决策对象 | 7.1 COG-D01、8.1 | 目录 / 数据结构 / 接口定义 | COG-TODO-005 ~ 010 | 先形成 module public surface，再进入阶段实现 |
| 配置投影、阶段启停、输入边界 | 6.10、6.13.1、6.9 | 配置 / 生命周期 / 错误处理 | COG-TODO-011 ~ 013 | 把 profile 差异、deadline、invalid input 统一收敛 |
| 五段阶段组件主链 | 6.13.2、6.13.3；7.1 COG-D03 ~ D07 | 阶段实现 | COG-TODO-014 ~ 019 | 依次完成感知、规划、推理、反思、写回提示、终态构造 |
| llm bridge、schema 校验、语义观测、门面收口 | 6.13.4；7.1 COG-D08、D09、D11 | 适配器 / 校验器 / 观测 / 生命周期 | COG-TODO-020 ~ 023 | 建立 decide / reflect / build_response 的完整受控闭环 |
| mocks 支撑、integration 拓扑、runtime happy path | 8.1、8.2、9.1；11.2 | 测试支撑 / topology / smoke | COG-TODO-024 ~ 026 | 先把测试脚手架和 discoverability 接入，再做主成功链 |
| 交互契约、失败注入、profile 兼容 | 6.14、6.15、6.16；7.1 COG-D10、D12 | 集成 / 失败 / profile gate | COG-TODO-027 ~ 029 | 验证 runtime↔cognition↔llm 三方接缝、故障和 profile 差异 |
| Gate 与交付证据回写 | 9.4、11.2、12.2 | 文档 / 交付证据 | COG-TODO-030 | 收敛命令证据、阻塞变化、风险残留 |
| 评审后生产闭环补强 | 6.10、6.13.4、6.14、6.15、6.16；9.1、9.4 | 接线 / 集成 / 负例 / 证据补强 | COG-TODO-031 ~ 038 | 把 bridge 主链接入、profile 生产投影、Runtime 语义信号消费、结构化 schema 校验和真实 gate evidence 收敛为后续补强闭环 |
| structured projection authoritative consumption | 6.13.4、13.2；COG-GAP-004 / COG-FIX-004 | 文档冻结 / schema / projection / authoritative consumption / evidence | COG-FIX-004A-DOC-001、COG-FIX-004A-BLD-001 ~ 012 | 把 bridge structured payload 从 validator-focused 能力推进为 `PlanGraph` / `ActionDecision` 主链对象来源，并冻结 Gate-COG-FIX004A-* |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 接口定义类任务 | 是 | COG-TODO-007 ~ 010 |
| 数据结构定义类任务 | 是 | COG-TODO-007 ~ 009 |
| 生命周期与初始化类任务 | 是 | COG-TODO-005、011、012、023 |
| 适配器 / 桥接类任务 | 是 | COG-TODO-020、022、024、031 |
| 异常与错误处理类任务 | 是 | COG-TODO-013、017、021、028、034、035 |
| 配置与 Profile 裁剪类任务 | 是 | COG-TODO-001、002、011、012、029、032 |
| 测试与门禁类任务 | 是 | COG-TODO-006、024 ~ 030、036、037 |
| 文档 / 交付证据回写类任务 | 是 | COG-TODO-001 ~ 004、030、038 |

## 6. 原子任务清单

说明：除文档一致性任务使用 `rg` 检索外，其余任务统一沿用仓库现有命令基线 `cmake -S . -B build-ci -G "Unix Makefiles" && ...`。表内“后续 gate 复验”表示该任务通过后会在 COG-TODO-030 中再跑一次全量门禁，不影响当前任务的即时完成判定。

### 6.1 前置补设计 / 评审门禁任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COG-TODO-001 | Done | 补齐 ICognitionEngine 公共接口口径评审 | 架构 5.8.4；蓝图 3.4 / 7；认知详设 6.6.1 | 6.6.1 `decide/reflect/build_response`；5.8.4 `step()` | L0 | 更新认知详设、架构/蓝图引用说明与本专项 TODO | `ICognitionEngine`、`IResponseBuilder`、`CognitionStepRequest` / `CognitionDecisionResult` 公开消费口径 | 文档一致性：`step()` 与三入口语义只能保留一套可执行口径 | `rg -n "ICognitionEngine|step\(|decide\(|reflect\(|build\(" docs/architecture/DASALL_Agent_architecture.md docs/architecture/DASALL_Engineering_Blueprint.md docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md` | 无 | 已解阻：COG-BLK-001 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-001-ICognitionEngine公共接口口径收敛.md；更新后的认知详设 / 专项 TODO；验收命令已通过，cognition 可执行入口统一为 `decide()` / `reflect()` / `IResponseBuilder::build()` | Runtime 对 cognition 的公开入口不再同时存在互相冲突的 `step()` 与三入口描述；runtime 自身 FSM `step()` 不属于本冲突 |
| COG-TODO-002 | Done | 收敛 cognition↔llm stage taxonomy 与 StageModelHint 映射表 | 认知详设 6.14.2；llm deliverable 035；RuntimePolicySnapshot.h | 6.14.2 `StageModelHint`；llm 真实 stage key `planning/execution/reflection/response` | L0 | 更新认知详设与本专项 TODO 的 stage 命名 / 投影约束 | `StageModelHint`、`stage_name`、`task_type`、`ModelProfile.stage_routes`、llm stage key | 文档一致性：认知阶段名、llm stage key、profile stage_routes 的唯一映射可检索 | `rg -n "planning|execution|reflection|response|perception|reasoning|StageModelHint|stage_routes" docs/architecture/DASALL_cognition子系统详细设计.md docs/architecture/DASALL_llm子系统详细设计.md docs/todos/llm/deliverables/LLM-TODO-035-profile-diff-integration设计收敛.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md` | 无 | 已解阻：COG-BLK-002 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-002-stage-taxonomy与StageModelHint映射收敛.md；更新后的认知详设 / 专项 TODO；LLM-TODO-035 追认说明；验收命令已通过，canonical key 集合固定为 `planning/execution/reflection/response` | bridge / projector / profile gate 消费的 stage taxonomy 唯一，legacy alias 只能在 profile provider / projector 边界归一化，测试不得私有映射 |
| COG-TODO-003 | Done | 收敛 runtime↔cognition caller fixture 与 ActionDecision→FSM 口径 | 认知详设 6.14.1、8.2、11.2；当前 runtime 现状 | 6.14.1 ActionDecision→FSM；COG-B02 | L0 | 更新认知详设与本专项 TODO，冻结 tests/design gate 最小 caller fixture 形状 | `ActionDecision.decision_kind`、`CognitionStepRequest`、`ReflectionRequest`、`RuntimeCognitionLoopSmoke`、`caller_domain` | 文档一致性：caller_domain、goal/context/belief/observation handoff、FSM 转移口径可检索 | `rg -n "ActionDecision\.decision_kind|CognitionStepRequest|ReflectionRequest|RuntimeCognitionLoopSmoke|caller_domain|FSM" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md` | 无 | 已解阻：COG-BLK-003 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-003-runtime-caller-fixture与FSM口径收敛.md；更新后的认知详设 / 专项 TODO；验收命令已通过，ActionDecision→FSM 第一跳映射对齐 Runtime 真实状态 / guard table | runtime 消费 cognition 的最小 fixture 已明确；legacy `MockLLMAdapter + MockTool` smoke 不再作为 COG-TODO-026/027 的验收口径 |
| COG-TODO-004 | Done | 补齐 cognition 测试 fixture 设计口径 | 认知详设 8.1、11.2；当前 tests/mocks 现状 | 8.1 `tests/mocks/include/MockLLMManager.h`、`MockCognitionFixture.h` | L0 | 更新认知详设与本专项 TODO 的 mock seam 设计说明 | `MockLLMManager`、`MockCognitionFixture`、failure/profile smoke fixture 角色 | 文档一致性：cognition-specific mock seam 及其作用域可检索 | `rg -n "MockLLMManager|MockCognitionFixture|tests/mocks/include" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md` | COG-TODO-001 ~ 003 | 设计侧已解阻：COG-BLK-004；实现侧仍由 COG-TODO-024 关闭 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-004-cognition测试fixture口径收敛.md；更新后的认知详设 / 专项 TODO；验收命令已通过，mock seam 作用域已固定为 `MockLLMManager`、`MockCognitionFixture` 与 failure/profile scenario helper | unit / integration / failure / profile gate 所需 mock seam 已明确；当前粗粒度 `MockLLMAdapter` 路径不再作为 cognition gate 口径 |

### 6.2 Build-ready 对象、接口与骨架任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COG-TODO-005 | Done | 新增 cognition 公共 include 布局与 CMake 骨架 | 认知详设 7.1 COG-D01、8.1、8.2；当前代码现状 | 8.1 目录建议 | L2 | 新增 `cognition/include/`、更新 `cognition/CMakeLists.txt`、替换 `src/placeholder.cpp` 唯一源列表 | `ICognitionEngine.h`、`IResponseBuilder.h`、`IPlanner.h`、`IReasoner.h`、`IReflectionEngine.h`、`CognitionConfig.h`、`CognitionTypes.h` 的落盘承载面 | build：`dasall_cognition` 不再仅依赖 placeholder；后续 gate 可承载 unit / integration 文件 | `cmake -S . -B build-ci-cog005 -G "Unix Makefiles" && cmake --build build-ci-cog005 --target dasall_cognition dasall_unit_tests`；`cmake --build build-ci --target dasall_cognition dasall_unit_tests`；`test ! -e cognition/src/placeholder.cpp && ! rg -n "placeholder.cpp|keep_library_non_empty" cognition/CMakeLists.txt cognition/src cognition/include` | 无 | 无 | 无 | docs/todos/cognition/deliverables/COG-TODO-005-cognition公共include布局与CMake骨架.md；cognition/CMakeLists.txt；cognition/include/；验收命令已通过，public headers 已登记到 `dasall_cognition` file set | cognition 已具备真实 include 根，并可在不依赖单个 placeholder 源文件的前提下编译；`CognitionInterfaceSurfaceTest` discoverability 仍由 COG-TODO-006 完成 |
| COG-TODO-006 | Done | 接线 tests/unit/cognition 与 CognitionInterfaceSurfaceTest | 认知详设 7.1 COG-D01、8.1、9.1；工程规范 3.7 | 8.1 tests/unit/cognition；9.1 unit matrix | L2 | 更新 `tests/unit/cognition/CMakeLists.txt` 与 `tests/unit/CMakeLists.txt`，新增 `tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp` | `CognitionInterfaceSurfaceTest` | unit：`ctest -N` 能发现 cognition unit 入口；`dasall_unit_tests` 聚合包含 1 个 `cognition` 标签测试 | `cmake -S . -B build-ci-cog006 -G "Unix Makefiles" && cmake --build build-ci-cog006 --target dasall_unit_tests`；`ctest --test-dir build-ci-cog006 -N | rg "CognitionInterfaceSurfaceTest"`；`ctest --test-dir build-ci-cog006 -R "CognitionInterfaceSurfaceTest" --output-on-failure`；`cmake -S . -B build-ci && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -N | rg "CognitionInterfaceSurfaceTest"` | COG-TODO-005 | 无 | 无 | docs/todos/cognition/deliverables/COG-TODO-006-cognition-unit测试入口接线.md；tests/unit/cognition/CMakeLists.txt；tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp；tests/unit/CMakeLists.txt；验收命令已通过，464 个 unit 测试全绿 | cognition unit 测试目录已退出 placeholder 状态；`CognitionInterfaceSurfaceTest` 在 `build-ci-cog006` 与既有 `build-ci` 均可被 `ctest -N` 稳定发现并可运行 |
| COG-TODO-007 | Done | 定义 CognitionConfig 与请求/结果对象族 | 认知详设 6.6.2、6.10、8.1；工程规范 3.2 | 6.6.2 request/result structs；8.1 `CognitionConfig.h`、`CognitionTypes.h`、`response/*` | L3 | 更新 `cognition/include/CognitionConfig.h`、`cognition/include/CognitionTypes.h`，新增 `cognition/include/response/ResponseBuildRequest.h`、`cognition/include/response/ResponseBuildResult.h`、`cognition/include/perception/PerceptionResult.h`，并适配直接消费者 | `CognitionStepRequest`、`CognitionDecisionResult`、`ReflectionRequest`、`CognitionReflectionResult`、`ResponseBuildRequest`、`ResponseBuildResult`、`CognitionConfig` | unit：`CognitionInterfaceSurfaceTest`；contract 回归：Goal/Belief/Context/Observation 边界不回退 | `cmake -S . -B build-ci-cog007 -G "Unix Makefiles" && cmake --build build-ci-cog007 --target dasall_cognition dasall_unit_tests dasall_contract_tests`；`ctest --test-dir build-ci-cog007 -R "CognitionInterfaceSurfaceTest|GoalContractFieldContractTest|BeliefStateContractTest|ContextPacketFieldContractTest|ObservationContractTest" --output-on-failure`；`cmake --build build-ci --target dasall_cognition dasall_unit_tests dasall_contract_tests` | COG-TODO-001、005、006 | 已解阻：COG-BLK-001 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-007-CognitionConfig与请求结果对象收敛.md；`cognition/include/CognitionConfig.h`；`cognition/include/CognitionTypes.h`；`cognition/include/response/ResponseBuildRequest.h`；`cognition/include/response/ResponseBuildResult.h`；`cognition/include/perception/PerceptionResult.h`；`tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp` | 已完成：请求/结果对象字段与详设一致；surface test 覆盖字段正例与越界字段负例；未把运行控制、provider payload 或恢复执行字段混入 cognition 公共对象 |
| COG-TODO-008 | Done | 定义 PlanGraph / PlanNode / ReplanResult 模块公共类型 | 认知详设 6.5.3、6.13.2、8.1；WP05-T011 / 012 | 6.5.3 PlanGraph / PlanNode / ReplanResult | L3 | 新增 `cognition/include/plan/PlanGraph.h`、`cognition/include/plan/ReplanResult.h`，并登记 `cognition/CMakeLists.txt` public header file set | `PlanGraph`、`PlanNode`、`PlanEdge`、`PlanOpenQuestion`、`ReplanResult` | unit：`CognitionInterfaceSurfaceTest`；后续 gate 复验：`PlannerPlanGraphTest` / `PlannerReplanTest` | `cmake -S . -B build-ci-cog008 -G "Unix Makefiles" && cmake --build build-ci-cog008 --target dasall_cognition dasall_unit_tests`；`ctest --test-dir build-ci-cog008 -R "CognitionInterfaceSurfaceTest" --output-on-failure`；`cmake --build build-ci --target dasall_cognition dasall_unit_tests`；`ctest --test-dir build-ci -R "CognitionInterfaceSurfaceTest" --output-on-failure` | COG-TODO-005、006 | 无 | 无 | docs/todos/cognition/deliverables/COG-TODO-008-PlanGraph与ReplanResult对象收敛.md；`cognition/include/plan/PlanGraph.h`；`cognition/include/plan/ReplanResult.h`；`tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp` | 已完成：DAG 基本字段、revision、open questions 与 success_signal 承载面稳定落盘；surface test 覆盖 runtime/recovery/provider 越界字段负例；未推动 shared admission |
| COG-TODO-009 | Done | 定义 ActionDecision / BeliefUpdateHint / StageModelHint / BudgetContext / ContextSufficiencySignal | 认知详设 6.5.3、6.14.2、6.14.5、6.16、8.1 | 6.5.3 ActionDecision / BeliefUpdateHint；6.14.2 StageModelHint；6.16 BudgetContext | L3 | 更新 `cognition/include/decision/ActionDecision.h`、`cognition/include/belief/BeliefUpdateHint.h`、`cognition/include/CognitionTypes.h`、`cognition/src/CognitionFacade.cpp`、`tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp` | 上述 module-public supporting types 与最小 façade 直接消费者 | unit：`CognitionInterfaceSurfaceTest`；后续 gate 复验：`StageModelHintProjectionTest`、`BudgetAwareDecisionTest` | `cmake --build build-ci --target dasall_cognition_interface_surface_unit_test && ctest --test-dir build-ci -R "^CognitionInterfaceSurfaceTest$" --output-on-failure` | COG-TODO-002、005、006 | 已解阻：COG-BLK-002 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-009-ActionDecision与BudgetContext对象收敛.md；对应 include 头文件、`CognitionFacade.cpp`、`CognitionInterfaceSurfaceTest.cpp`；Build_CMakeTools 通过、RunCtest_CMakeTools 因已知工具问题回退到显式 ctest | 已完成：决策、写回提示、预算与上下文充分性信号全部保持 module-local；StageModelHint 采用 canonical stage key；旧 tool payload 字段已退出 ActionDecision public surface |
| COG-TODO-010 | Done | 定义 ICognitionEngine / IResponseBuilder / IPlanner / IReasoner / IReflectionEngine 接口 | 认知详设 6.6.1、6.6.3、8.1；InterfaceCatalog.h | 6.6.1 / 6.6.3 接口语义 | L3 | 更新 `cognition/include/ICognitionEngine.h`、`IResponseBuilder.h`、`IPlanner.h`、`IReasoner.h`、`IReflectionEngine.h`，补齐阶段 request structs 并同步 `tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp` | `ICognitionEngine::decide()`、`ICognitionEngine::reflect()`、`IResponseBuilder::build()`、`IPlanner::build_plan()`、`IPlanner::replan()`、`IReasoner::decide()`、`IReflectionEngine::analyze()`、`PlanningRequest`、`ReplanRequest`、`ReasoningRequest`、`ReflectionAnalysisRequest` | unit：`CognitionInterfaceSurfaceTest`；contract 回归：`IPlanner` 仍保持 postponed admission | `cmake --build build-ci --target dasall_cognition_interface_surface_unit_test dasall_contract_interface_admission_test && ./build-ci/tests/unit/cognition/dasall_cognition_interface_surface_unit_test && ./build-ci/tests/contract/dasall_contract_interface_admission_test` | COG-TODO-001、005、006、007、008、009 | 已解阻：COG-BLK-001 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-010-cognition公共接口面收敛.md；对应 include 头文件、`CognitionInterfaceSurfaceTest.cpp`、更新后的认知详设 / 开发记录 | 已完成：runtime-facing 公开入口保持 `decide()` / `reflect()` / `build()`；阶段接口已具备稳定签名与 supporting request structs；`IPlanner` 未被误推进 shared contracts |

### 6.3 Build-ready 组件实现任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COG-TODO-011 | Done | 实现 CognitionConfigProjector | 认知详设 6.13.1、7.1 COG-D02；RuntimePolicySnapshot.h | 6.13.1 `project_config()`；6.10 配置表 | L2 | 新增 `cognition/src/config/CognitionConfigProjector.cpp`、`cognition/src/config/CognitionConfigProjector.h`，并更新 cognition / unit CMake 接线 | `project_config()`、`merge_profile_defaults()`、`derive_stage_model_hint()` | unit：`CognitionConfigProjectionTest` | `Build_CMakeTools(buildTargets=["dasall_cognition_config_projection_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_config_projection_unit_test` | COG-TODO-002、007、010 | 已解阻：COG-BLK-002 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-011-CognitionConfigProjector收敛.md；`cognition/src/config/CognitionConfigProjector.cpp`；`cognition/src/config/CognitionConfigProjector.h`；`tests/unit/cognition/CognitionConfigProjectionTest.cpp`；相关 CMake 接线 | 已完成：cognition 配置直接来自 `RuntimePolicySnapshot`；stage hint 继续使用 canonical stage key；缺失 canonical route 与 legacy alias 走 fail-closed |
| COG-TODO-012 | Done | 实现 StagePolicyResolver | 认知详设 6.13.1、6.15.3、6.16.2；7.1 COG-D02 | 6.13.1 `resolve_decide_plan()` 等 | L2 | 新增 `cognition/src/StagePolicyResolver.cpp`、`cognition/src/StagePolicyResolver.h`，并更新 cognition / unit CMake 接线 | `resolve_decide_plan()`、`resolve_reflection_plan()`、`resolve_response_plan()`、`derive_stage_model_hint()`、`StageExecutionPlan` | unit：`StagePolicyResolverTest`、`StagePolicyResolverProfileDiffTest`、`BudgetAwareDecisionTest` | `Build_CMakeTools(buildTargets=["dasall_stage_policy_resolver_unit_test","dasall_stage_policy_resolver_profile_diff_unit_test","dasall_budget_aware_decision_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_policy_resolver_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_policy_resolver_profile_diff_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_budget_aware_decision_unit_test` | COG-TODO-002、007、011 | 已解阻：COG-BLK-002 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-012-StagePolicyResolver收敛.md；`cognition/src/StagePolicyResolver.cpp`；`cognition/src/StagePolicyResolver.h`；对应 resolver unit tests 与 CMake 接线 | 已完成：阶段启停、deadline、budget-aware plan cap 和 profile 差异均可自动化验证，且 resolver 未扩张为 public/shared contract |
| COG-TODO-013 | Done | 实现 InputBoundaryValidator | 认知详设 6.9、7.1 COG-D03 | 8.1 `cognition/src/validation/InputBoundaryValidator.cpp` | L2 | 新增 `cognition/src/validation/InputBoundaryValidator.cpp`、`cognition/src/validation/InputBoundaryValidator.h`，并更新 `CognitionFacade.cpp` / cognition unit CMake 接线 | `validate_decide_request()`、`validate_reflection_request()`、`validate_response_request()` | unit：`PerceptionBoundaryValidationTest`、`CognitionFacadeInvalidInputTest` | `Build_CMakeTools(buildTargets=["dasall_perception_boundary_validation_unit_test","dasall_cognition_facade_invalid_input_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_boundary_validation_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_invalid_input_unit_test` | COG-TODO-007、010 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-013-InputBoundaryValidator收敛.md；`cognition/src/validation/InputBoundaryValidator.cpp`；`cognition/src/validation/InputBoundaryValidator.h`；`tests/unit/cognition/PerceptionBoundaryValidationTest.cpp`；`tests/unit/cognition/CognitionFacadeInvalidInputTest.cpp`；相关 CMake 接线 | 已完成：invalid input 已统一映射为显式 `ErrorInfo`，且 decide / reflect / build 三入口不再静默降级为 recent-history only |
| COG-TODO-014 | Done | 实现 PerceptionEngine | 认知详设 6.13.2；7.1 COG-D03 | 6.13.2 `PerceptionEngine` 卡片 | L2 | 新增 `cognition/src/perception/PerceptionEngine.cpp`、`cognition/src/perception/PerceptionEngine.h`，并更新 cognition / unit CMake 接线 | `perceive()`、`extract_entities()`、`detect_ambiguities()`、`derive_clarification_questions()`、`run_rule_fallback()`、`validate_perception_output()` | unit：`PerceptionEngineTest`、`PerceptionBoundaryValidationTest`、`PerceptionClarificationRuleTest` | `Build_CMakeTools(buildTargets=["dasall_perception_engine_unit_test","dasall_perception_clarification_rule_unit_test","dasall_perception_boundary_validation_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_engine_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_clarification_rule_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_boundary_validation_unit_test` | COG-TODO-007、010、011、012、013 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-014-PerceptionEngine收敛.md；`cognition/src/perception/PerceptionEngine.cpp`；`cognition/src/perception/PerceptionEngine.h`；`tests/unit/cognition/PerceptionEngineTest.cpp`；`tests/unit/cognition/PerceptionClarificationRuleTest.cpp`；相关 CMake 接线 | 已完成：实体抽取、歧义检测、规则降级与澄清输出均可二值断言，且无效输入继续复用统一 boundary validator |
| COG-TODO-015 | Done | 实现 Planner 与 PlanGraphBuilder | 认知详设 6.13.2、6.16.2；7.1 COG-D04 | 6.13.2 `Planner` 卡片 | L2 | 新增 `cognition/src/planning/Planner.cpp`、`cognition/src/planning/Planner.h`、`cognition/src/planning/PlanGraphBuilder.cpp`、`cognition/src/planning/PlanGraphBuilder.h`，并更新 cognition / unit CMake 接线 | `build_plan()`、`replan()`、`expand_goal_into_nodes()`、`validate_plan_graph()`、`compress_plan_when_budget_tight()` | unit：`PlannerPlanGraphTest`、`PlannerReplanTest`、`PlannerNodeBudgetTest` | `Build_CMakeTools(buildTargets=["dasall_planner_plan_graph_unit_test","dasall_planner_replan_unit_test","dasall_planner_node_budget_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_plan_graph_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_replan_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_node_budget_unit_test` | COG-TODO-007、008、010、011、012、013 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-015-Planner与PlanGraphBuilder收敛.md；`cognition/src/planning/Planner.cpp`；`cognition/src/planning/Planner.h`；`cognition/src/planning/PlanGraphBuilder.cpp`；`cognition/src/planning/PlanGraphBuilder.h`；`tests/unit/cognition/PlannerPlanGraphTest.cpp`；`tests/unit/cognition/PlannerReplanTest.cpp`；`tests/unit/cognition/PlannerNodeBudgetTest.cpp`；相关 CMake 接线 | 已完成：DAG 构建、`revision` 递增、budget 收紧和 open question 路径均可自动化验证；budget tight 时 plan node 数收缩行为已由 `PlannerNodeBudgetTest` 断言 |
| COG-TODO-016 | Done | 实现 Reasoner 与 DecisionProjector | 认知详设 6.13.2、6.14.1；7.1 COG-D05 | 6.13.2 `Reasoner` 卡片 | L2 | 新增 `cognition/src/reasoning/Reasoner.cpp`、`cognition/src/reasoning/Reasoner.h`、`cognition/src/reasoning/DecisionProjector.cpp`、`cognition/src/reasoning/DecisionProjector.h`，并更新 cognition / unit CMake 接线 | `decide()`、`score_candidates()`、`evaluate_clarification_need()`、`project_response_outline()`、`validate_decision_thresholds()` | unit：`ReasonerActionDecisionTest`、`ReasonerClarificationThresholdTest`、`ReasonerConflictResolutionTest` | `Build_CMakeTools(buildTargets=["dasall_reasoner_action_decision_unit_test","dasall_reasoner_clarification_threshold_unit_test","dasall_reasoner_conflict_resolution_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_action_decision_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_clarification_threshold_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_conflict_resolution_unit_test` | COG-TODO-007、008、009、010、012、013、015 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-016-Reasoner与DecisionProjector收敛.md；`cognition/src/reasoning/Reasoner.cpp`；`cognition/src/reasoning/Reasoner.h`；`cognition/src/reasoning/DecisionProjector.cpp`；`cognition/src/reasoning/DecisionProjector.h`；`tests/unit/cognition/ReasonerActionDecisionTest.cpp`；`tests/unit/cognition/ReasonerClarificationThresholdTest.cpp`；`tests/unit/cognition/ReasonerConflictResolutionTest.cpp`；相关 CMake 接线 | 已完成：执行、澄清、收敛、冲突处理四类候选均可稳定投影为 `ActionDecision`，且 terminal direct-response 计划不会再误判为执行路径 |
| COG-TODO-017 | Done | 实现 ReflectionEngine | 认知详设 6.13.3；7.1 COG-D06 | 6.13.3 `ReflectionEngine` 卡片 | L2 | 新增 `cognition/src/reflection/ReflectionEngine.cpp`、`cognition/src/reflection/ReflectionEngine.h`，并更新 cognition / unit CMake 接线 | `analyze()`、`classify_failure_source()`、`evaluate_goal_gap()`、`detect_assumption_invalidations()`、`project_reflection_decision()`、`validate_reflection_contract()` | unit：`ReflectionEngineDecisionTest`、`ReflectionEngineBeliefInvalidationTest`、`ReflectionEngineConservativeAbortTest` | `Build_CMakeTools(buildTargets=["dasall_reflection_engine_decision_unit_test","dasall_reflection_engine_belief_invalidation_unit_test","dasall_reflection_engine_conservative_abort_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_decision_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_belief_invalidation_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_conservative_abort_unit_test` | COG-TODO-007、008、009、010、012、013 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-017-ReflectionEngine收敛.md；`cognition/src/reflection/ReflectionEngine.cpp`；`cognition/src/reflection/ReflectionEngine.h`；`tests/unit/cognition/ReflectionEngineDecisionTest.cpp`；`tests/unit/cognition/ReflectionEngineBeliefInvalidationTest.cpp`；`tests/unit/cognition/ReflectionEngineConservativeAbortTest.cpp`；相关 CMake 接线 | 已完成：`retry_step` / `replan` / `abort_safe` 仍保持 suggestion-only，且高风险或证据不足时会保守收敛到 `AbortSafe` |
| COG-TODO-018 | Done | 实现 BeliefUpdateSynthesizer | 认知详设 6.13.3、6.14.4；7.1 COG-D06 | 6.13.3 `BeliefUpdateSynthesizer` 卡片 | L2 | 新增 `cognition/src/belief/BeliefUpdateSynthesizer.cpp`、`cognition/src/belief/BeliefUpdateSynthesizer.h`，并更新 cognition / unit CMake 接线 | `synthesize_from_decide()`、`synthesize_from_reflection()`、`merge_deltas()`、`normalize_evidence_refs()` | unit：`BeliefUpdateSynthesizerTest`、`BeliefUpdateMergeModeTest`、`BeliefUpdateEvidenceDedupTest` | `Build_CMakeTools(buildTargets=["dasall_belief_update_synthesizer_unit_test","dasall_belief_update_merge_mode_unit_test","dasall_belief_update_evidence_dedup_unit_test"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_synthesizer_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_merge_mode_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_evidence_dedup_unit_test` | COG-TODO-008、009、010、017 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-018-BeliefUpdateSynthesizer收敛.md；`cognition/src/belief/BeliefUpdateSynthesizer.cpp`；`cognition/src/belief/BeliefUpdateSynthesizer.h`；`tests/unit/cognition/BeliefUpdateSynthesizerTest.cpp`；`tests/unit/cognition/BeliefUpdateMergeModeTest.cpp`；`tests/unit/cognition/BeliefUpdateEvidenceDedupTest.cpp`；相关 CMake 接线 | 已完成：delta 分类、evidence 去重和 merge mode 输出正确，且组件只生成 best-effort hint，不直接触发 memory 写入 |
| COG-TODO-019 | Done | 实现 ResponseBuilder | 认知详设 6.13.3、10.2；7.1 COG-D07 | 6.13.3 `ResponseBuilder` 卡片 | L2 | 新增 `cognition/src/response/ResponseBuilder.cpp`，并更新 `cognition/src/CognitionFacade.cpp`、`cognition/CMakeLists.txt`、`tests/unit/cognition/CMakeLists.txt` | `build()`、`select_response_mode()`、`build_with_llm()`、`build_with_template()`、`redact_unsafe_fields()`、`clamp_output_size()` | unit：`ResponseBuilderAgentResultMappingTest`、`ResponseBuilderTemplateFallbackTest`、`ResponseBuilderRedactionTest` | `Build_CMakeTools(buildTargets=["dasall_cognition","dasall_response_builder_agent_result_mapping_unit_test"])`；`RunCtest_CMakeTools(tests=["ResponseBuilderAgentResultMappingTest"])`；`cmake --build build/vscode-linux-ninja --target dasall_response_builder_agent_result_mapping_unit_test dasall_response_builder_template_fallback_unit_test dasall_response_builder_redaction_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_agent_result_mapping_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_template_fallback_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_response_builder_redaction_unit_test` | COG-TODO-007、009、010、012、013 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-019-ResponseBuilder收敛.md；`cognition/src/response/ResponseBuilder.cpp`；`cognition/src/CognitionFacade.cpp`；`cognition/CMakeLists.txt`；`tests/unit/cognition/CMakeLists.txt`；`tests/unit/cognition/ResponseBuilderAgentResultMappingTest.cpp`；`tests/unit/cognition/ResponseBuilderTemplateFallbackTest.cpp`；`tests/unit/cognition/ResponseBuilderRedactionTest.cpp` | 已完成：独立 ResponseBuilder owner 已落盘；completed llm projection、template fallback、fallback-disabled error 与 redaction on/off 全部可验证，且未引入 streaming 额外职责；`RunCtest_CMakeTools` 仍存在已知工具态 `生成失败`，已按仓库基线回退显式二进制执行 |
| COG-TODO-020 | Done | 实现 CognitionLlmBridge | 认知详设 6.13.4、6.14.2、6.14.3；7.1 COG-D08、D11 | 6.13.4 `CognitionLlmBridge` 卡片 | L2 | 新增 `cognition/src/llm/CognitionLlmBridge.h`、`cognition/src/llm/CognitionLlmBridge.cpp`，并更新 `cognition/CMakeLists.txt`、`tests/unit/cognition/CMakeLists.txt` | `invoke_stage()`、`build_llm_request()`、`derive_budget_hint()`、`normalize_llm_response()`、`project_llm_failure()` | unit：`CognitionLlmBridgeProjectionTest`、`CognitionLlmBridgeErrorMappingTest`、`StageModelHintProjectionTest` | `Build_CMakeTools(buildTargets=["dasall_cognition_llm_bridge_projection_unit_test","dasall_cognition_llm_bridge_error_mapping_unit_test","dasall_stage_model_hint_projection_unit_test"])`；`RunCtest_CMakeTools(tests=["CognitionLlmBridgeProjectionTest","CognitionLlmBridgeErrorMappingTest","StageModelHintProjectionTest"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_llm_bridge_projection_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_llm_bridge_error_mapping_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_model_hint_projection_unit_test` | COG-TODO-002、004、007、009、010、024 | 已解阻：COG-BLK-002、COG-BLK-004 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-020-CognitionLlmBridge收敛.md；`cognition/src/llm/CognitionLlmBridge.h`；`cognition/src/llm/CognitionLlmBridge.cpp`；`cognition/CMakeLists.txt`；`tests/unit/cognition/CMakeLists.txt`；`tests/unit/cognition/CognitionLlmBridgeProjectionTest.cpp`；`tests/unit/cognition/CognitionLlmBridgeErrorMappingTest.cpp`；`tests/unit/cognition/StageModelHintProjectionTest.cpp` | 已完成：stage hint 投影、budget hint、错误映射和 provider-private 字段剥离全部通过 focused tests；`RunCtest_CMakeTools` 仍存在已知工具态 `生成失败`，已按仓库基线回退显式二进制执行；bridge 保持 fail-fast，不自建 retry/breaker |
| COG-TODO-021 | Done | 实现 StageOutputValidator | 认知详设 6.13.4；7.1 COG-D08 | 6.13.4 `StageOutputValidator` 卡片 | L2 | 新增 `cognition/src/validation/StageOutputValidator.cpp`、`cognition/src/validation/StageOutputValidator.h`，并更新 cognition / unit CMake 接线 | `validate_stage_output()`、`validate_plan_graph_invariants()`、`validate_action_decision_invariants()`、`validate_response_envelope()` | unit：`StageOutputValidatorSchemaTest`、`StageOutputValidatorPlanGraphInvariantTest`、`StageOutputValidatorResponseEnvelopeTest` | `Build_CMakeTools(buildTargets=["dasall_stage_output_validator_schema_unit_test","dasall_stage_output_validator_plan_graph_invariant_unit_test","dasall_stage_output_validator_response_envelope_unit_test"])`；`RunCtest_CMakeTools(tests=["StageOutputValidatorSchemaTest","StageOutputValidatorPlanGraphInvariantTest","StageOutputValidatorResponseEnvelopeTest"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_schema_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_plan_graph_invariant_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_response_envelope_unit_test` | COG-TODO-002、008、009、010、020 | 已解阻：COG-BLK-002 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-021-StageOutputValidator收敛.md；`cognition/src/validation/StageOutputValidator.cpp`；`cognition/src/validation/StageOutputValidator.h`；`tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`；`tests/unit/cognition/StageOutputValidatorPlanGraphInvariantTest.cpp`；`tests/unit/cognition/StageOutputValidatorResponseEnvelopeTest.cpp` | 已完成：required fields、graph invariants、decision / response envelope 约束全部以 fail-closed 方式生效；`RunCtest_CMakeTools` 仍存在已知工具态 `生成失败`，已按仓库基线回退显式二进制执行 |
| COG-TODO-022 | Done | 实现 CognitionTelemetry | 认知详设 6.11、6.13.4；7.1 COG-D09 | 6.13.4 `CognitionTelemetry` 卡片 | L2 | 新增 `cognition/src/observability/CognitionTelemetry.cpp`、`cognition/src/observability/CognitionTelemetry.h`、`tests/mocks/include/MockCognitionTelemetrySink.h`，并更新 cognition / unit CMake 接线 | `emit_stage_started()`、`emit_stage_completed()`、`emit_stage_failed()`、`emit_clarification_requested()`、`emit_response_degraded()` | unit：`CognitionTelemetryFieldsTest`、`CognitionTelemetryRedactionTest`、`CognitionTelemetryFailureIsolationTest` | `Build_CMakeTools(buildTargets=["dasall_cognition_telemetry_fields_unit_test","dasall_cognition_telemetry_redaction_unit_test","dasall_cognition_telemetry_failure_isolation_unit_test"])`；`RunCtest_CMakeTools(tests=["CognitionTelemetryFieldsTest","CognitionTelemetryRedactionTest","CognitionTelemetryFailureIsolationTest"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_fields_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_redaction_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_failure_isolation_unit_test` | COG-TODO-004、007、009、010 | 已解阻：COG-BLK-004 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-022-CognitionTelemetry收敛.md；`cognition/src/observability/CognitionTelemetry.cpp`；`cognition/src/observability/CognitionTelemetry.h`；`tests/mocks/include/MockCognitionTelemetrySink.h`；`tests/unit/cognition/CognitionTelemetryFieldsTest.cpp`；`tests/unit/cognition/CognitionTelemetryRedactionTest.cpp`；`tests/unit/cognition/CognitionTelemetryFailureIsolationTest.cpp` | 已完成：语义级字段完整、provider-private 内容被裁剪且 sink 故障 fail-open；`RunCtest_CMakeTools` 仍存在已知工具态 `生成失败`，已按仓库基线回退显式二进制执行 |
| COG-TODO-023 | Done | 实现 CognitionFacade | 认知详设 6.13.1、6.7、6.8；7.1 COG-D08 | 6.13.1 `CognitionFacade` 卡片 | L2 | 更新 `cognition/src/CognitionFacade.cpp`、`tests/unit/cognition/CMakeLists.txt`，新增 `tests/unit/cognition/CognitionFacadeFlowTest.cpp`、`tests/unit/cognition/CognitionFacadeDegradedModeTest.cpp` | `decide()`、`reflect()`、`run_decision_pipeline()`、`run_reflection_pipeline()`；模块级 response 闭环通过 `create_response_builder()` 组合验证 | unit：`CognitionFacadeFlowTest`、`CognitionFacadeDegradedModeTest`、`CognitionFacadeInvalidInputTest` | `Build_CMakeTools(buildTargets=["dasall_cognition_facade_invalid_input_unit_test","dasall_cognition_facade_flow_unit_test","dasall_cognition_facade_degraded_mode_unit_test"])`；`RunCtest_CMakeTools(tests=["CognitionFacadeInvalidInputTest","CognitionFacadeFlowTest","CognitionFacadeDegradedModeTest"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_invalid_input_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_flow_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_degraded_mode_unit_test` | COG-TODO-001、002、007 ~ 022 | COG-BLK-001、COG-BLK-002、COG-BLK-004 | 完成 COG-TODO-001、002、024 | docs/todos/cognition/deliverables/COG-TODO-023-CognitionFacade收敛.md；`cognition/src/CognitionFacade.cpp`；`tests/unit/cognition/CognitionFacadeFlowTest.cpp`；`tests/unit/cognition/CognitionFacadeDegradedModeTest.cpp`；`tests/unit/cognition/CMakeLists.txt` | 已完成：façade 不再是 stub；decide / reflect 主链、invalid-input 显式错误出口和 clarification degrade path 已通过 focused tests，且未越权触发外部执行与恢复 |

### 6.4 测试支撑、集成与 Gate 收口任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COG-TODO-024 | Done | 新增 MockLLMManager 与 MockCognitionFixture | 认知详设 8.1、11.2；当前 tests/mocks 现状 | 8.1 `tests/mocks/include/MockLLMManager.h`、`MockCognitionFixture.h` | L2 | 新增 `tests/mocks/include/MockLLMManager.h`、`tests/mocks/include/MockCognitionFixture.h`，并更新 `tests/unit/cognition/CMakeLists.txt`、新增 `tests/unit/cognition/MockCognitionFixtureSurfaceTest.cpp` | `MockLLMManager` scripted stage result / failure helper、`MockCognitionFixture` runtime caller request builders、shared cognition owner helper | unit：`MockCognitionFixtureSurfaceTest` | `Build_CMakeTools(buildTargets=["dasall_mock_cognition_fixture_surface_unit_test"])`；`RunCtest_CMakeTools(tests=["MockCognitionFixtureSurfaceTest"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_mock_cognition_fixture_surface_unit_test` | COG-TODO-001 ~ 004、006、010 | 已解阻：COG-BLK-004 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-024-cognition测试fixture实现收敛.md；`tests/mocks/include/MockLLMManager.h`；`tests/mocks/include/MockCognitionFixture.h`；`tests/unit/cognition/CMakeLists.txt`；`tests/unit/cognition/MockCognitionFixtureSurfaceTest.cpp` | 已完成：cognition-specific mock seam 真正落盘，bridge / telemetry / facade / integration 后续不再依赖当前粗粒度 `MockLLMAdapter`；`RunCtest_CMakeTools` 仍存在已知工具态 `生成失败`，已按仓库基线回退显式二进制执行 |
| COG-TODO-025 | Done | 注册 tests/integration/cognition 拓扑 | 认知详设 7.1 COG-D10、8.1、9.1；SSOT `InfraIntegrationTopology` | 8.1 `tests/integration/cognition/`；9.1 integration matrix | L2 | 更新 `tests/integration/CMakeLists.txt`，新增 `tests/integration/cognition/CMakeLists.txt` | cognition integration discoverability | integration：`ctest -N` 可发现 cognition integration 用例 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -N | rg "Cognition(Runtime|FailureInjection|ProfileCompatibility)Test|CognitionRuntimeInteractionContractTest"` | COG-TODO-005、024 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-025-tests_integration_cognition拓扑收敛.md；`tests/integration/CMakeLists.txt`；`tests/integration/cognition/CMakeLists.txt` | 已完成：cognition integration 拓扑已被顶层聚合；discoverability 当前以 `CognitionRuntimeIntegrationTest`、`CognitionRuntimeInteractionContractTest`、`CognitionFailureInjectionIntegrationTest`、`CognitionProfileCompatibilityTest` 与后续 `CognitionReviewRegressionTest` 为准；`dasall_integration_tests` 构建当前受 runtime 既有编译错误阻塞，已在交付物记录为非本任务回归 |
| COG-TODO-026 | Done | 验证 CognitionRuntimeIntegration 主成功链 | 认知详设 7.1 COG-D09 / D10、8.2、9.1；当前 runtime smoke 现状 | 8.2 COG-M4 / M5；`RuntimeCognitionLoopSmokeTest`、`CognitionRuntimeIntegrationTest` | L2 | 新增 `tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp`、`tests/integration/cognition/CognitionRuntimeIntegrationTest.cpp`，并让 runtime smoke 不再绕过 cognition | runtime↔cognition happy path | unit + integration：从 Runtime handoff 到 cognition decide / response 主链可执行 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeCognitionLoopSmokeTest|CognitionRuntimeIntegrationTest" --output-on-failure` | COG-TODO-003、023、024、025 | COG-BLK-003、COG-BLK-004 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-026-CognitionRuntimeIntegration主链收敛.md；`tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h`；`tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp`；`tests/integration/cognition/CognitionRuntimeIntegrationTest.cpp`；`tests/unit/runtime/CMakeLists.txt`；`tests/integration/cognition/CMakeLists.txt`；`runtime/src/AgentOrchestrator.cpp` | 已完成：runtime 不再绕过 cognition 主链，`RuntimeCognitionLoopSmokeTest` 与 `CognitionRuntimeIntegrationTest` 均通过；`build-ci` 目录沿用 Ninja 生成器进行等效验证 |
| COG-TODO-027 | Done | 验证 CognitionRuntimeInteractionContract | 认知详设 6.14、7.1 COG-D12、9.2；contracts 基线 | 6.14.1 ~ 6.14.5；`CognitionRuntimeInteractionContractTest` | L2 | 新增 `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` | ActionDecision→FSM、错误回流、BeliefUpdateHint 写回时序、ContextSufficiencySignal | integration + contract：交互契约与既有 shared contract tests 同时通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests dasall_contract_tests && ctest --test-dir build-ci -R "CognitionRuntimeInteractionContractTest|ContextPacketFieldContractTest|ReflectionDecisionContractTest|AgentResultContractTest|MainFlowContractE2ETest" --output-on-failure` | COG-TODO-003、018、023、024、025 | COG-BLK-003、COG-BLK-004 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-027-CognitionRuntimeInteractionContract收敛.md；`tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp`；`tests/integration/cognition/CMakeLists.txt` | 已完成：交互契约矩阵命令通过，`CognitionRuntimeInteractionContractTest` 与 `ContextPacketFieldContractTest`、`ReflectionDecisionContractTest`、`AgentResultContractTest`、`MainFlowContractE2ETest` 全部通过 |
| COG-TODO-028 | Done | 验证 CognitionFailureInjectionIntegration | 认知详设 9.1、9.3；7.1 COG-D10 | `CognitionFailureInjectionIntegrationTest` | L2 | 新增 `tests/integration/cognition/CognitionFailureInjectionIntegrationTest.cpp` | llm unavailable、schema violation、missing belief state、contradictory observation、response fallback | integration：故障路径显式返回 ErrorInfo / 降级结果，且无静默吞错 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "CognitionFailureInjectionIntegrationTest" --output-on-failure` | COG-TODO-020、021、023、024、025 | COG-BLK-004 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-028-CognitionFailureInjectionIntegration收敛.md；`tests/integration/cognition/CognitionFailureInjectionIntegrationTest.cpp`；`tests/integration/cognition/CMakeLists.txt` | 已完成：`CognitionFailureInjectionIntegrationTest` 通过并覆盖五类故障/降级路径，结果以显式失败或 PartiallyCompleted 返回，无静默吞错 |
| COG-TODO-029 | Done | 验证 CognitionProfileCompatibility | 认知详设 6.10.2、9.1、9.4；llm deliverable 035；profiles 详设 | `CognitionProfileCompatibilityTest`；五档 profile 策略 | L2 | 新增 `tests/integration/cognition/CognitionProfileCompatibilityTest.cpp` | desktop_full / cloud_full / edge_balanced / edge_minimal / factory_test 下的阶段启停、plan cap、fallback 与 route 策略 | integration：五档 profile 都有明确通过或拒绝结论 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "CognitionProfileCompatibilityTest" --output-on-failure` | COG-TODO-002、011、012、020、023、024、025 | COG-BLK-002、COG-BLK-004 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-029-CognitionProfileCompatibility收敛.md；`tests/integration/cognition/CognitionProfileCompatibilityTest.cpp`；`tests/integration/cognition/CMakeLists.txt` | 已完成：`CognitionProfileCompatibilityTest` 通过，五档 profile 的策略差异通过统一投影视图收敛且给出明确通过/拒绝结论 |
| COG-TODO-030 | Done | 回写 cognition 专项 Gate 与交付证据 | 认知详设 9.4、11.2、12.2；文档治理基线 | Gate-COG-*；COG-BLK-* | L2 | 更新 `docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` | Gate 结论、阻塞变化、风险残留、命令证据 | process：全部 gate 命令、通过/残余结论、后续动作回写齐备 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_cognition dasall_unit_tests dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -R "Cognition|RuntimeCognitionLoopSmoke|GoalContractFieldContractTest|BeliefStateContractTest|ContextPacketFieldContractTest|ObservationContractTest|ReflectionDecisionContractTest|AgentResultContractTest|MainFlowContractE2ETest"` | COG-TODO-026 ~ 029 | 无 | 已完成 | 更新后的专项 TODO、对应 deliverables、docs/worklog/DASALL_开发执行记录.md；`docs/todos/cognition/deliverables/COG-TODO-030-cognition专项Gate与交付证据回写收敛.md` | 已完成：`ctest -N` 发现 733 tests；聚焦 gate regex `25/25` 通过；全量 integration 聚合存在 infra/plugin 既有残余，已在交付物与 worklog 回写风险及后续动作 |

### 6.5 评审后补强任务（COG-TODO-031 ~ 038）

说明：本节来源于 2026-04-27 对 `docs/todos/cognition/deliverables/`、`cognition/`、单元测试与集成测试的专项评审。任务目标是把“已可编译、可聚焦测试”的 cognition 基线继续推进到更可信的生产闭环；新增任务不得回改 COG-TODO-001 ~ 030 的 Done 结论，只补齐后续缺口和证据。

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COG-TODO-031 | Done | 串联 CognitionLlmBridge 到 cognition 主链 | 评审发现：`CognitionLlmBridge` 已实现但 `CognitionFacade` / `ResponseBuilder` 生产路径未注入 `ILLMManager`，当前 bridge tests 只证明独立投影 | 6.13.4、6.14.2；COG-TODO-020、023 | L2 | 已新增 `CognitionRuntimeDependencies` 和非破坏性 factory overload，`CognitionFacade` 经 bridge 触达 `planning` / `execution` / `reflection`，`ResponseBuilder` 经 bridge 触达 `response`；无 bridge 时保留 deterministic observation projection / template fallback | `create_cognition_engine(config, dependencies)`、`create_response_builder(config, dependencies)`、`CognitionFacade::consume_decision_bridge_stage()`、`ResponseBuilder::build()`、`MockLLMManager::generate_requests()` | unit + integration：mock llm manager 记录 canonical stage 调用；bridge failure 触发显式模板降级；无 bridge 的 response observation projection 不再标为 LLM projection | `cmake --build build-ci --target dasall_cognition dasall_cognition_facade_flow_unit_test dasall_response_builder_template_fallback_unit_test dasall_cognition_llm_bridge_projection_unit_test && ctest --test-dir build-ci -R "CognitionFacadeFlowTest|ResponseBuilder.*|CognitionLlmBridgeProjectionTest" --output-on-failure`；`cmake --build build-ci --target dasall_cognition_runtime_integration_test && ctest --test-dir build-ci -R "CognitionRuntimeIntegrationTest" --output-on-failure` | COG-TODO-020、023、024 | 已确认：`runtime/include/RuntimeDependencySet.h` 已有 `llm_manager` 字段；本轮以 cognition-side dependencies 与 runtime test fixture 闭环 | 已完成：不扩大 shared contracts；runtime production profile 投影留给 COG-TODO-032 | docs/todos/cognition/deliverables/COG-TODO-031-CognitionLlmBridge主链接入收敛.md；`cognition/include/CognitionDependencies.h`；`cognition/src/CognitionFacade.cpp`；`cognition/src/response/ResponseBuilder.cpp`；相关 mock / unit / integration fixture 更新 | 已完成：focused ctest `5/5` 通过，`CognitionRuntimeIntegrationTest` 通过；真实主链 test 证明 façade / response builder 通过 canonical stage 调用 bridge，无 bridge 时仍 deterministic 且带 diagnostics |
| COG-TODO-032 | Done | 收敛 Runtime profile 到 CognitionConfig 的生产注入闭环 | 评审发现：profile compatibility 集成测试主要切换 `profile_id`，未断言 `CognitionConfigProjector` 投影结果被 runtime 初始化路径消费 | 6.10.2、6.13.1、6.15.3；COG-TODO-011、012、029 | L2 | 已新增 snapshot-aware cognition factory overload，并在 runtime init 缺失 live ports 时显式完成 `RuntimePolicySnapshot -> CognitionConfig` 投影；`CognitionFacade` / `ResponseBuilder` 真实消费 `StagePolicyResolver` 输出，bridge request 可观察 route / deadline / output budget | `AgentFacade::init()`、`create_cognition_engine(snapshot, dependencies)`、`create_response_builder(snapshot, dependencies)`、`CognitionConfigProjector`、`StagePolicyResolver`、`RuntimePolicySnapshot` | unit + integration：public surface、projector、resolver profile diff 与 provider-driven runtime projection gate 验证真实 profile asset 的 route / deadline / output budget 注入；缺失 canonical route 继续 fail-closed | `Build_CMakeTools()`；`RunCtest_CMakeTools(tests=["ProfileRuntimePolicySchemaContractTest","ProfileOverlayComposerTest","ProfilesBuildRuntimeIntegrationTest","RuntimeProfileCompatibilityTest","CapabilityServicesProfileIntegrationTest","CognitionRuntimePolicyProjectionIntegrationTest"])` | COG-TODO-011、012、029 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-032-RuntimeProfile到CognitionConfig注入收敛.md；`cognition/include/CognitionDependencies.h`；`cognition/include/ICognitionEngine.h`；`cognition/include/IResponseBuilder.h`；`cognition/src/CognitionFacade.cpp`；`cognition/src/response/ResponseBuilder.cpp`；`runtime/src/AgentFacade.cpp`；`tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h`；`tests/integration/cognition/CognitionProfileCompatibilityTest.cpp`；`tests/integration/cognition/CognitionRuntimePolicyProjectionIntegrationTest.cpp`；`tests/integration/cognition/CMakeLists.txt`；相关 unit tests | 已完成：032 的权威 integration evidence 已切换到 `CognitionRuntimePolicyProjectionIntegrationTest`；真实 profile asset -> provider -> runtime init -> planning/execution/reflection stage request 已自动化覆盖；更宽的 response bridge 语义继续留给 COG-TODO-040 |
| COG-TODO-033 | Done | Runtime 消费 BeliefUpdateHint 与 ContextSufficiencySignal | 评审发现：`CognitionDecisionResult` 已携带 belief / context 信号，但 `AgentOrchestrator` 主链未证明写回与 context reload recommendation 被消费 | 6.14.4、6.14.5、6.16；COG-TODO-018、027 | L2 | 已在 Runtime 侧增加 bounded best-effort 处理：`BeliefUpdateHint` 经 Memory seam 投影为最小 turn/summary/facts；`recommend_context_reload` 触发一次受 replan budget 约束的上下文刷新，失败后复用 waiting-clarify degrade | `AgentOrchestrator::run_once()`、runtime cognition handoff helper、`BeliefUpdateHint`、`ContextSufficiencySignal`、memory mock / true integration fixture | unit + integration：可观察到 memory writeback 调用或 sqlite facts 落库；reload recommendation 触发恰好一次受限 refresh；显式 cognition error 仍 hard fail；写回失败不覆盖 completed 结果 | `Build_CMakeTools(buildTargets=["dasall_runtime_cognition_loop_smoke_unit_test","dasall_cognition_runtime_interaction_contract_integration_test","dasall_cognition_failure_injection_integration_test"])`；`RunCtest_CMakeTools(tests=["RuntimeCognitionLoopSmokeTest","CognitionRuntimeInteractionContractTest","CognitionFailureInjectionTest"])` | COG-TODO-018、026、027 | 无 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-033-Runtime消费Belief与Context信号收敛.md；`runtime/src/AgentOrchestrator.cpp`；`tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp`；`tests/unit/runtime/CMakeLists.txt`；`tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` | 已完成：interaction contract 和 smoke 不再只证明 runtime 能跑通，而能证明 belief/context 信号被 Runtime 真正消费；writeback failure 与 explicit cognition error 都有防回归证据 |
| COG-TODO-034 | Done | Runtime 消费 ReflectionDecision 并统一 cognition 错误面 | 评审发现：`AgentOrchestrator` 当前调用 `reflect()` 后丢弃结果并固定继续；同时 decision result 若同时含 action 与 error 缺少 fail-closed 断言 | 6.14.1、6.14.3、6.15；ADR-007；COG-TODO-017、027、028 | L2 | 已在 Runtime 显式解释 `CognitionReflectionResult.reflection_decision`，将 Continue / RetryStep / Replan / AbortSafe 映射到现有 FSM 与 RecoveryManager 准入；同时校验 cognition result 中 `error_info` 与可执行 action 的互斥规则 | `AgentOrchestrator::run_once()`、`make_reflection_request()`、`CognitionDecisionResult`、`CognitionReflectionResult`、`ReflectionDecision` | unit + integration：reflection abort 停止主链；retry / replan 进入既有恢复或规划路径；error+execute 冲突 fail-closed；cognition error 优先级可追溯 | `Build_CMakeTools(buildTargets=["dasall_cognition_runtime_interaction_contract_integration_test","dasall_cognition_failure_injection_integration_test"])`；`RunCtest_CMakeTools(tests=["CognitionRuntimeInteractionContractTest","CognitionFailureInjectionIntegrationTest","ReflectionDecisionContractTest"])` | COG-TODO-017、026、027、028 | 无 | 已完成：RecoveryManager 保持唯一恢复准入 owner，runtime 只负责把 suggestion 投影为合规 `RecoveryRequest` | docs/todos/cognition/deliverables/COG-TODO-034-Runtime反思决策与错误面收敛.md；`runtime/src/AgentOrchestrator.cpp`；`tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp`；`tests/integration/cognition/CognitionFailureInjectionIntegrationTest.cpp` | 已完成：`reflect()` 结果不再被丢弃；Continue / RetryStep / Replan / AbortSafe 全部有 deterministic runtime behavior；error+execute conflict 与 reflection error priority 均有自动化覆盖 |
| COG-TODO-035 | Done | 将 StageOutputValidator 升级为结构化 JSON / schema 校验 | 评审发现：当前 validator 对 llm payload 的 required field / enum / numeric / list 检查依赖字符串扫描，无法可靠处理空白、转义、嵌套和伪字段 | 6.13.4、6.15.1；COG-TODO-021 | L2 | 在 `StageOutputValidator` private namespace 内新增窄 JSON token/object parser，把 `validate_stage_output()` 从 substring 判定升级为结构化字段遍历；schema spec 保持 module-private | `StageOutputValidator`、`StageSchemaSpec`、`ValidationIssueSet`、`StageLlmCallResult.response.content_payload` | unit：覆盖合法 JSON 空白 / 顺序变化、字符串转义伪字段、嵌套数组、字段类型错误、malformed JSON fail-closed | `Build_CMakeTools(buildTargets=["dasall_stage_output_validator_schema_unit_test","dasall_stage_output_validator_plan_graph_invariant_unit_test","dasall_stage_output_validator_response_envelope_unit_test"])`；`RunCtest_CMakeTools(tests=["StageOutputValidatorSchemaTest","StageOutputValidatorPlanGraphInvariantTest","StageOutputValidatorResponseEnvelopeTest"])` | COG-TODO-021、028 | 无 | 已完成：仓库无正式 JSON 依赖，本轮仅在 cognition private validator 内补窄 parser，不暴露 shared utility | docs/todos/cognition/deliverables/COG-TODO-035-StageOutputValidator结构化校验收敛.md；`cognition/src/validation/StageOutputValidator.cpp`；`cognition/src/validation/StageOutputValidator.h`；`tests/unit/cognition/StageOutputValidatorSchemaTest.cpp` | 已完成：stage payload 改为结构化 token/object 校验，escaped pseudo-field、nested array、字段类型错误与 malformed JSON 全部有 focused unit 证据，且既有 plan graph / response envelope tests 保持通过 |
| COG-TODO-036 | Done | 移除 cognition integration placeholder alias 并重建证据口径 | 评审发现：profile compatibility 历史空跑别名会被计入 Gate-COG-10 聚焦统计，导致通过率被高估 | 8.1、9.1、9.4；COG-TODO-025、030 | L2 | 已删除 profile compatibility 历史空跑别名；`DASALL_COGNITION_INTEGRATION_TEST_EXECUTABLE_TARGETS` 现仅保留真实 executable target；Gate-COG-10 证据口径回落到真实 `CognitionProfileCompatibilityTest` | `tests/integration/cognition/CMakeLists.txt`、`CognitionProfileCompatibilityTest`、Gate-COG-10 evidence | process + integration：`ctest -N` 中 cognition gate 名称均可回溯到真实 executable；无历史空跑 gate | `cmake -S . -B build-ci && ctest --test-dir build-ci -N | rg "Cognition" && rg -n "CognitionProfileCompatibilityTest|CognitionReviewRegressionTest" tests/integration/cognition/CMakeLists.txt docs/todos/cognition/deliverables/COG-TODO-030-cognition专项Gate与交付证据回写收敛.md` | COG-TODO-025、029、030 | 无 | 已完成；仓库内未发现 CI/脚本对旧别名的保留需求，故不保留该别名 | docs/todos/cognition/deliverables/COG-TODO-036-cognitionIntegration证据口径收敛.md；`tests/integration/cognition/CMakeLists.txt`；更新后的 Gate-COG-10 证据文档 | 已完成：cognition 聚焦 gate 的 test count 不再包含空跑测试；所有通过项均可追溯到真实 executable 或真实测试命令 |
| COG-TODO-037 | Done | 补齐评审缺口的负例回归矩阵 | 评审发现：现有 focused tests 多验证 happy path 和独立组件，缺少能防止 bridge 未接入、profile 未投影、runtime 丢弃语义信号、schema 字符串绕过的回归矩阵 | 9.1、9.2、9.4；COG-TODO-031 ~ 036 | L2 | 已新增 `CognitionReviewRegressionTest`，并把 bridge 调用回归、missing canonical route fail-closed 与 placeholder alias absence 收口为专用 gate；其余负例继续复用 runtime/schema/response focused tests，形成完整 review matrix | `tests/unit/cognition/*`、`tests/integration/cognition/*`、`tests/unit/runtime/*`、`tests/fixtures/runtime/*` | unit + integration：bridge not invoked、missing canonical route fail-closed、belief/context signal observed、reflection abort observed、schema pseudo-field rejected、placeholder alias absent 均有明确测试入口 | `Build_CMakeTools(buildTargets=["dasall_cognition_review_regression_integration_test","dasall_cognition_runtime_interaction_contract_integration_test","dasall_stage_output_validator_schema_unit_test","dasall_cognition_facade_flow_unit_test","dasall_response_builder_template_fallback_unit_test"])`；`RunCtest_CMakeTools(tests=["CognitionReviewRegressionTest","CognitionFacadeFlowTest","CognitionRuntimeInteractionContractTest","StageOutputValidatorSchemaTest","ResponseBuilderTemplateFallbackTest"])` | COG-TODO-031 ~ 036 | 无 | 已完成；response stage bridge 继续由 `CognitionFacadeFlowTest` 与 `ResponseBuilderTemplateFallbackTest` 守护，review regression 不再误伤允许的模板回退 | docs/todos/cognition/deliverables/COG-TODO-037-cognition评审负例矩阵收敛.md；`tests/integration/cognition/CognitionReviewRegressionTest.cpp`；`tests/integration/cognition/CMakeLists.txt` | 已完成：任一关键评审缺口回归时，focused gate 或 review regression 会明确失败；负例归因已绑定到具体 owner/test，而非人工日志判断 |
| COG-TODO-038 | Done | 回写评审后 Gate、worklog 与残余风险 | 评审发现：COG-TODO-030 已记录聚焦 gate 25/25 和全量 integration 残余，但未包含后续补强任务的 Gate-COG-11 结论、风险状态和证据修正 | 9.4、10.1、11.2、12.2 | L2 | 已更新专项 TODO、对应 deliverables、`docs/worklog/DASALL_开发执行记录.md`，补齐 Gate-COG-11 结论、COG-R13 ~ R18 状态、全量 integration 残余与后续动作 | Gate-COG-11、COG-R13 ~ R18、COG-TODO-031 ~ 037 evidence | process：文档能追溯每个评审发现的任务、代码证据、测试命令和剩余风险 | `rg -n "Gate-COG-11|COG-R1[3-8]|COG-TODO-03[6-8]" docs/todos/cognition docs/worklog/DASALL_开发执行记录.md && rg -n "CognitionReviewRegressionTest|CognitionProfileCompatibilityTest" tests/integration/cognition/CMakeLists.txt docs/todos/cognition` | COG-TODO-031 ~ 037 | 无 | 已完成；前序补强任务已有明确通过结论，repo-wide integration 残余继续显式保留 | docs/todos/cognition/deliverables/COG-TODO-038-cognition评审后Gate与风险回写收敛.md；专项 TODO 与 worklog 更新 | 已完成：文档不再混淆 001 ~ 030 基线、031 ~ 037 补强和 repo-wide integration 残余；Gate-COG-11 与 COG-R13 ~ R18 具备可追溯状态 |

### 6.6 复验后修复与生产证据收敛任务（COG-TODO-039 ~ 044）

说明：本节来源于 2026-04-28 对方案、设计收敛、`cognition/`、runtime cognition handoff、unit / contract / integration gate 的复验。任务目标是修复当前统一验收命令中的可复现失败，并把 response 语义、reflection active plan、文档证据和 warning hygiene 收敛到可合并状态；新增任务不得越权改写 ADR 或把 PlanGraph / ActionDecision / BeliefUpdateHint 推入 shared contracts。

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COG-TODO-039 | Todo | 修复统一验收构建聚合与 runtime canonical init fixture | 2026-04-28 Build 评审：`dasall_unit_tests` 下 `RuntimeCognitionLoopSmokeTest` 已注册但未被聚合构建；`RuntimeControlPlaneSurfaceTest` 的最小 init fixture 只提供 `main` route，无法通过 cognition canonical route 投影 | 9.1 discoverability；9.2 统一验收命令；COG-TC012、COG-TC015；COG-TODO-032 / 033 | L2 | 更新 `tests/unit/CMakeLists.txt`，把 `dasall_runtime_cognition_loop_smoke_unit_test` 纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`；更新 `tests/fixtures/runtime/RuntimeUnaryFixture.h`，使 `make_policy_snapshot()` 提供 `planning/execution/reflection/response` canonical stage routes，或在纯 runtime surface test 中显式注入 mock cognition / response builder ports | `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`、`RuntimeCognitionLoopSmokeTest`、`make_policy_snapshot()`、`AgentFacade::init()` | unit + discoverability：`RuntimeControlPlaneSurfaceTest` 通过；`dasall_unit_tests` 构建会先产出 runtime cognition smoke executable；`ctest -N` 不再出现该 test missing executable | `cmake -S . -B build-ci-cog039 -G "Unix Makefiles" && cmake --build build-ci-cog039 --target dasall_unit_tests && ctest --test-dir build-ci-cog039 --output-on-failure -R "RuntimeControlPlaneSurfaceTest|RuntimeCognitionLoopSmokeTest"`；`ctest --test-dir build-ci-cog039 -N | rg "RuntimeCognitionLoopSmokeTest"` | COG-TODO-032、033 | 无 | 仅修测试聚合与 fixture 合法性，不改变 cognition 生产语义；若 runtime surface test 选择 mock ports，必须显式证明该测试不依赖 cognition policy projector | docs/todos/cognition/deliverables/COG-TODO-039-统一验收构建聚合与runtime-fixture收敛.md；`tests/unit/CMakeLists.txt`；`tests/fixtures/runtime/RuntimeUnaryFixture.h`；必要时更新 `RuntimeControlPlaneSurfaceTest.cpp` | `dasall_unit_tests` 不再因 missing executable 或最小 init fixture route 缺失失败；runtime smoke 在统一验收构建目标下可发现、可执行 |
| COG-TODO-040 | Todo | 收敛 runtime+cognition response bridge 与 template fallback 契约 | 2026-04-28 Build 评审：`RuntimeCognitionLoopSmokeTest` / `CognitionRuntimeIntegrationTest` 期望 runtime+cognition completion message，`CognitionProfileCompatibilityTest` 期望非 factory profile 发出 response bridge request；但 COG-TODO-037 文档允许 review regression 不强制 response bridge，测试与设计口径冲突 | 6.13.4 ResponseBuilder；6.14.2 stage route；9.1 integration matrix；COG-TODO-031、032、037 | L2 | 先在交付物中冻结 response 行为口径：非 factory profile 是否必须走 response bridge，factory_test 是否模板优先；随后按冻结口径更新 `runtime/src/AgentOrchestrator.cpp` 的 `make_response_build_request()` / observation payload 投影、`cognition/src/response/ResponseBuilder.cpp` 的 mode selection 或对应 integration tests | `ResponseBuildRequest.latest_observation`、`ResponseBuilder::select_response_mode()`、`StagePolicyResolver::resolve_response_plan()`、`MockLLMManager::generate_requests()` | unit + integration：非 factory profile 的 response stage 行为与设计一致；factory_test 模板优先路径保持明确；response bridge failure 仍可模板降级；completion message / response request 断言不互相矛盾 | `cmake --build build-ci-cog040 --target dasall_cognition dasall_runtime_cognition_loop_smoke_unit_test dasall_cognition_runtime_integration_test dasall_cognition_profile_compatibility_integration_test dasall_cognition_review_regression_integration_test dasall_response_builder_template_fallback_unit_test && ctest --test-dir build-ci-cog040 --output-on-failure -R "RuntimeCognitionLoopSmokeTest|CognitionRuntimeIntegrationTest|CognitionProfileCompatibilityTest|CognitionReviewRegressionTest|ResponseBuilderTemplateFallbackTest"` | COG-TODO-031、032、037、039 | response bridge 与模板回退的产品语义需先冻结，避免只放宽测试或只硬接 bridge | 交付物必须明确二值判定：哪些 profile 必须调用 response bridge，哪些 profile 允许 template preferred；实现与测试按同一判定收敛 | docs/todos/cognition/deliverables/COG-TODO-040-response-bridge与fallback契约收敛.md；`runtime/src/AgentOrchestrator.cpp`；`cognition/src/response/ResponseBuilder.cpp`；`tests/integration/cognition/CognitionRuntimeIntegrationTest.cpp`；`tests/integration/cognition/CognitionProfileCompatibilityTest.cpp`；`tests/integration/cognition/CognitionReviewRegressionTest.cpp` | 聚焦 ctest 不再出现 response completion message 与 response bridge request 断言冲突；review regression 与 profile compatibility 对 response 行为口径一致 |
| COG-TODO-041 | Todo | 打通 Reflection active PlanGraph 生产路径 | 2026-04-28 Build 评审：详设要求 ReflectionEngine 分析 `PlanGraph` / 当前计划节点 `success_signal`，内部 `ReflectionAnalysisRequest` 与 `ReflectionEngine` 已支持 active_plan，但 runtime-facing `ReflectionRequest` 只有 `active_plan_ref` 且 `CognitionFacade::reflect()` 固定传 `std::nullopt` | 6.6.2 `ReflectionRequest`；6.13.2 PlanGraph；6.13.3 ReflectionEngine；COG-TC004、COG-TC008 | L3 / L2 | 非破坏性更新 `cognition/include/CognitionTypes.h`，为 `ReflectionRequest` 增加 `std::optional<plan::PlanGraph> active_plan` 并保留 `active_plan_ref` 兼容；更新 `CognitionFacade::reflect()` 映射 `analysis_request.active_plan = request.active_plan`；如 Runtime 可获得当前计划图，则更新 `make_reflection_request()` 传入 active plan，否则补显式 TODO / diagnostic 不伪造 plan | `ReflectionRequest`、`ReflectionAnalysisRequest`、`CognitionFacade::reflect()`、`ReflectionEngine::analyze()`、`PlanGraph` | unit + integration：ReflectionEngine 直接单测继续通过；新增 façade-level test 证明 active node id / success_signal 影响 reflection rationale 或 decision；runtime reflection request 至少不再静默丢弃已提供 active plan | `cmake --build build-ci-cog041 --target dasall_cognition dasall_cognition_interface_surface_unit_test dasall_reflection_engine_decision_unit_test dasall_cognition_facade_flow_unit_test dasall_cognition_runtime_interaction_contract_integration_test && ctest --test-dir build-ci-cog041 --output-on-failure -R "CognitionInterfaceSurfaceTest|ReflectionEngineDecisionTest|CognitionFacadeFlowTest|CognitionRuntimeInteractionContractTest"` | COG-TODO-008、010、017、023、034 | 不得把 `PlanGraph` 推入 shared contracts；不得在 ReflectionEngine 内执行恢复动作 | 采用 optional additive 字段降低破坏性；若需要删除 `active_plan_ref`，必须另走接口 breaking change 评审 | docs/todos/cognition/deliverables/COG-TODO-041-ReflectionActivePlan生产路径收敛.md；`cognition/include/CognitionTypes.h`；`cognition/src/CognitionFacade.cpp`；相关 reflection / facade / runtime tests | 已提供 active plan 时，生产 façade 路径会传给 ReflectionEngine；测试能捕获再次把 active plan 置空的回归 |
| COG-TODO-042 | Todo | 复验 Gate-COG-12 并回写统一验收证据 | 2026-04-28 Build 评审：TODO 中 Gate-COG-11 标为 Pass，但复验统一命令失败；需要在 039 ~ 041 修复后重新跑完整验收并把 Gate、风险、worklog 分层回写 | 9.2 统一验收命令；9.4 Gate；10.1 风险；12.2 交付证据 | L2 | 更新 `docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` 与本任务 deliverable；必要时补充 `docs/todos/cognition/deliverables/COG-TODO-030` / 038 的“历史证据已被 Gate-COG-12 复验覆盖”说明，不改写 ADR | Gate-COG-12、COG-R19 ~ R23、统一验收命令、ctest discoverability | process + build：干净 Unix Makefiles 目录可配置、构建、发现 733+ tests；聚焦 regex 全绿；若 repo-wide 残余仍存在，必须明确非 cognition owner 与可执行解阻条件 | `cmake -S . -B build-ci-cog042 -G "Unix Makefiles" && cmake --build build-ci-cog042 --target dasall_cognition dasall_unit_tests dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci-cog042 -N && ctest --test-dir build-ci-cog042 --output-on-failure -R "Cognition|RuntimeCognitionLoopSmoke|GoalContractFieldContractTest|BeliefStateContractTest|ContextPacketFieldContractTest|ObservationContractTest|ReflectionDecisionContractTest|AgentResultContractTest|MainFlowContractE2ETest"` | COG-TODO-039 ~ 041 | 039 ~ 041 任一验收未通过时不得把 Gate-COG-12 标为 Pass | 若命令受非 cognition 残余阻断，必须在交付物中列出失败测试、owner、最小解阻条件和 cognition scope 结论 | docs/todos/cognition/deliverables/COG-TODO-042-Gate-COG-12复验证据回写.md；专项 TODO；worklog | Gate-COG-12 状态、命令结果、残余风险和回退路径全部可追溯；不再只依赖历史 Gate-COG-11 Pass 结论 |
| COG-TODO-043 | Todo | 同步 cognition 详设当前状态与追溯证据 | 2026-04-28 方案评审：详设仍描述 `cognition/src/placeholder.cpp`、空 unit 目录与 runtime smoke 绕过 cognition，已与当前代码和 TODO 状态冲突 | 1.2 模块概览；3.1 现状-目标差距；9.1 测试矩阵；COG-TC014 | L2 | 更新 `docs/architecture/DASALL_cognition子系统详细设计.md`，把 placeholder-only / 空测试目录描述标记为历史 baseline 或改为当前状态；补充 031 ~ 042 的证据链入口，不改变 ADR 结论和 module-public 边界 | 详设当前状态、缺口表、Gate 映射、测试矩阵 | docs：`rg` 不再把 placeholder-only 描述作为当前事实；仍保留历史 baseline 可追溯；详设、专项 TODO、worklog 对 Gate-COG-12 口径一致 | `rg -n "placeholder.cpp|空测试目录|绕过 cognition|Gate-COG-12|COG-TODO-04[0-3]" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md` | COG-TODO-039 ~ 042 | 无 | 不修改 ADR-006/007/008 结论；仅修正文档当前状态和追溯证据 | docs/todos/cognition/deliverables/COG-TODO-043-cognition详设当前状态追溯收敛.md；`docs/architecture/DASALL_cognition子系统详细设计.md`；专项 TODO / worklog 引用 | 新读者不会从详设误判 cognition 仍是 placeholder-only；当前代码、测试与 Gate 状态可跨文档一致追溯 |
| COG-TODO-044 | Todo | 清理 cognition 测试初始化 warning 并固化 warning hygiene | 2026-04-28 Build 评审：`MockCognitionFixture.h` 与 `ResponseBuilderTemplateFallbackTest.cpp` 存在 `CognitionRuntimeDependencies::policy_snapshot` missing initializer warning；虽不阻断当前构建，但会降低后续 `-Werror` 兼容性 | 工程规范 3.6；COG-TC014；COG-TODO-031、032 | L3 | 更新 `tests/mocks/include/MockCognitionFixture.h`、`tests/unit/cognition/ResponseBuilderTemplateFallbackTest.cpp` 等 cognition 测试初始化点，显式设置 `.policy_snapshot = nullptr` 或提供最小 helper，避免聚合初始化遗漏 | `CognitionRuntimeDependencies`、`MockCognitionFixture::make_engine()`、`MockCognitionFixture::make_response_builder()`、response fallback tests | unit + build hygiene：相关 cognition unit 构建不再产生 missing-field initializer warning；测试行为不变 | `cmake --build build-ci-cog044 --target dasall_cognition_facade_flow_unit_test dasall_response_builder_template_fallback_unit_test dasall_mock_cognition_fixture_surface_unit_test && ctest --test-dir build-ci-cog044 --output-on-failure -R "CognitionFacadeFlowTest|ResponseBuilderTemplateFallbackTest|MockCognitionFixtureSurfaceTest"` | COG-TODO-031、032 | 无 | 仅清理 cognition 责任域 warning，不扩大到 tools / infra / services 的既有 warning | docs/todos/cognition/deliverables/COG-TODO-044-cognition测试初始化warning收敛.md；相关 test / mock fixture 更新 | cognition 相关 warning hygiene 收敛，后续提升 warning gate 时不因本子系统测试初始化遗漏阻断 |

### 6.7 Structured projection authoritative consumption 任务（COG-FIX-004A）

说明：本节承接 `COG-GAP-004` / `COG-FIX-004`。它不回写否定 `COG-TODO-020` / `021` / `023` / `031` / `035` 的既有完成状态，而是在已具备 bridge、validator、façade 和结构化 schema 校验基线的前提下，把 `planning` / `execution` 的 structured payload 升级为 `PlanGraph` / `ActionDecision` 主链对象来源。设计 owner、schema baseline、fallback policy、diagnostics 与边界约束以认知详设 6.13.4 / 13.2 和总账附录 A 为准。

| Design ID | 产物 | 内容 | 通过条件 |
|---|---|---|---|
| COG-FIX-004A-D01 | schema baseline | 冻结 `cognition.plan.v1`、`cognition.reasoning.v1` 字段表、unknown field 策略、version 策略 | 认知详设 / 本专项 TODO / 总账附录 A 三处引用一致 |
| COG-FIX-004A-D02 | projection contract | 冻结 projector 输入输出、error model、fallback policy、diagnostics | 后续 Build 任务都能指向明确 owner / target / test |
| COG-FIX-004A-D03 | runtime boundary review | 复核 Runtime / ToolManager / RecoveryManager 边界未被 structured output 越权 | ADR-006 / ADR-007 / ADR-008 guardrail 可检索，且无新增 shared contracts admission |
| COG-FIX-004A-D04 | gate plan | 冻结 `Gate-COG-FIX004A-01` ~ `05` 与统一验收命令 | 后续执行不再临时改成功判定 |

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COG-FIX-004A-DOC-001 | Done | 回写方案 A 设计到 cognition 详设与专项 TODO | 总账附录 A；认知详设 6.13.4 / 13.2；COG-GAP-004 | 文档设计冻结 | L2 | 更新 `docs/architecture/DASALL_cognition子系统详细设计.md`、`docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、`docs/todos/DASALL_子系统查漏补缺专项记录.md` | `COG-FIX-004`、`Gate-COG-FIX004A-*`、schema baseline | docs consistency | `rg -n "COG-FIX-004A" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md && rg -n "cognition.plan.v1" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md && rg -n "cognition.reasoning.v1" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md` | 无 | 无 | 无 | 更新后的详设 / 专项 TODO / 总账附录 A | 方案 A 不再只存在于总账附录；设计锚点、任务 ID、Gate 和命令跨文档一致 |
| COG-FIX-004A-BLD-001 | Done | 增加 StageSchemaRegistry 与 structured schema specs | 附录 A.5；COG-TODO-021 / 035 | `planning/plan`、`execution/action_decision` schema owner | L2 | 新增 `cognition/src/validation/StageSchemaRegistry.h`、`cognition/src/validation/StageSchemaRegistry.cpp`，更新 `cognition/CMakeLists.txt`、`cognition/src/validation/StageOutputValidator.h`、`tests/unit/cognition/CMakeLists.txt`、`tests/unit/cognition/StageOutputValidatorSchemaTest.cpp` | `schema_for_planning_plan()`、`schema_for_execution_action_decision()`、`StageSchemaSpec` | `StageOutputValidatorSchemaTest`、`StageSchemaRegistryTest` | `cmake --build build-ci --target dasall_stage_schema_registry_unit_test dasall_stage_output_validator_schema_unit_test && ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest" && ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest"` | COG-FIX-004A-DOC-001 | 无 | 无 | schema registry 源码、unit tests、CMake 接线、对齐 registry baseline 的 schema validator test | planning / execution schema 不再散落在 Facade 或 tests；schema version、required fields、enum、numeric、list、unknown field 策略可二值断言 |
| COG-FIX-004A-BLD-002 | Done | 提取可复用 structured payload token view | 附录 A.3；COG-TODO-035 | validator private parser -> projector 可消费 token view | L2 | 新增 `cognition/src/validation/StructuredPayloadView.h`，更新 `cognition/src/validation/StageOutputValidator.cpp` / `.h`、`cognition/src/validation/StageSchemaRegistry.h` / `.cpp`、`tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`、`tests/unit/cognition/StageSchemaRegistryTest.cpp` | `parse_structured_payload()`、field token accessor、type-safe string / number / bool / list / object readers | `StageOutputValidatorSchemaTest`、`StageSchemaRegistryTest` | `cmake --build build-ci --target dasall_stage_schema_registry_unit_test dasall_stage_output_validator_schema_unit_test && ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest" && ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest"` | COG-FIX-004A-BLD-001 | 无 | parser 不扩成 shared utility；只对 cognition private projection 暴露 | token view header、validator 更新、schema unknown-field focused tests | projector 不再二次手写字符串扫描；malformed、escaped pseudo-field、nested array、type mismatch 与 unknown top-level field 均 fail-closed |
| COG-FIX-004A-BLD-003 | Done | 实现 PlanGraphStructuredProjector | 附录 A.4 / A.5.1；COG-TODO-008 / 015 / 021 | planning payload -> `PlanGraph` | L2 | 新增 `cognition/src/projection/PlanGraphStructuredProjector.h`、`cognition/src/projection/PlanGraphStructuredProjector.cpp`，更新 `cognition/CMakeLists.txt`、`tests/unit/cognition/CMakeLists.txt` | `project_plan_graph()`、`PlanGraphProjectionResult` | `PlanGraphStructuredProjectionTest` | `cmake --build build-ci --target dasall_plan_graph_structured_projection_unit_test && ctest --test-dir build-ci --output-on-failure -R "PlanGraphStructuredProjectionTest"` | COG-FIX-004A-BLD-002 | 无 | 无 | projector 源码、unit tests、CMake 接线、deliverable | valid `cognition.plan.v1` payload 可投影为合法 `PlanGraph`；duplicate node、unknown edge、cycle、over cap、missing success signal 均失败且不静默修复 |
| COG-FIX-004A-BLD-004 | Done | 实现 ActionDecisionStructuredProjector | 附录 A.4 / A.5.2；COG-TODO-009 / 016 / 021 | execution payload -> `ActionDecision` | L2 | 新增 `cognition/src/projection/ActionDecisionStructuredProjector.h`、`cognition/src/projection/ActionDecisionStructuredProjector.cpp`，更新 `cognition/CMakeLists.txt`、`tests/unit/cognition/CMakeLists.txt`、`cognition/src/validation/StageOutputValidator.cpp` | `project_action_decision()`、`ActionDecisionProjectionResult`、`validate_action_decision_invariants()` | `ActionDecisionStructuredProjectionTest` | `cmake --build build-ci --target dasall_action_decision_structured_projection_unit_test && ctest --test-dir build-ci --output-on-failure -R "ActionDecisionStructuredProjectionTest"` | COG-FIX-004A-BLD-003 | 无 | 无 | projector 源码、unit tests、CMake 接线、invariant 收紧、deliverable | valid `cognition.reasoning.v1` payload 可投影为合法 `ActionDecision`；invalid enum、missing selected node、tool intent on response、clarification conflict 均 fail-closed |
| COG-FIX-004A-BLD-005 | Done | 将 Facade planning path 切到 structured projection authoritative consumption | 附录 A.4 / A.6；COG-TODO-023 / 031 / 032 / COG-FIX-003 | `CognitionFacade::run_decision_pipeline()` planning segment | L2 | 更新 `cognition/src/CognitionFacade.cpp`、`cognition/src/validation/StageOutputValidator.cpp`、`tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`、`tests/unit/cognition/CognitionFacadeStructuredPlanOutputTest.cpp`、`tests/unit/cognition/CMakeLists.txt` | `consume_decision_bridge_stage()`、planning projection flow、fallback policy、array-path schema gate | `StageOutputValidatorSchemaTest`、`CognitionFacadeStructuredPlanOutputTest` | `cmake --build build-ci --target dasall_stage_output_validator_schema_unit_test dasall_cognition_facade_structured_plan_output_unit_test && ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredPlanOutputTest"` | COG-FIX-004A-BLD-003 | 无 | stage policy 未冻结则回到 DOC-001 | Facade 变更、validator array-path 修正、unit tests | schema-valid planning bridge payload 成为 active `PlanGraph`；本地 Planner 只在显式 fallback 条件下使用；invalid payload 不会悄悄进入 plan graph |
| COG-FIX-004A-BLD-006 | Done | 将 Facade execution path 切到 structured projection authoritative consumption | 附录 A.4 / A.6；COG-TODO-023 / 031 / 033 / 034 | `CognitionFacade::run_decision_pipeline()` execution segment | L2 | 更新 `cognition/src/CognitionFacade.cpp`、`tests/unit/cognition/CognitionFacadeStructuredActionOutputTest.cpp`、`tests/unit/cognition/CMakeLists.txt` | execution projection flow、action invariant validation、belief synthesis handoff | `CognitionFacadeStructuredActionOutputTest` | `cmake --build build-ci --target dasall_cognition_facade_structured_action_output_unit_test && ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredActionOutputTest"` | COG-FIX-004A-BLD-004、COG-FIX-004A-BLD-005 | 无 | 无 | Facade 变更、unit tests、deliverable | schema-valid execution bridge payload 成为 `ActionDecision`；Runtime-facing result 保持 existing shape；invalid action payload fail-closed 或显式 fallback |
| COG-FIX-004A-BLD-007 | Done | 扩展 MockLLMManager / MockCognitionFixture 支持 structured payload scenarios | 附录 A.9；COG-TODO-004 / 024 / 037 | tests support | L3 | 更新 `tests/mocks/include/MockLLMManager.h`、`tests/mocks/include/MockCognitionFixture.h` | planning / action JSON fixture builders、malformed / schema-invalid / projection-invalid staged results | `MockCognitionFixtureSurfaceTest`、projection / facade tests | `cmake --build build-ci --target dasall_mock_cognition_fixture_surface_unit_test && ctest --test-dir build-ci --output-on-failure -R "MockCognitionFixtureSurfaceTest"` | COG-FIX-004A-BLD-001 | 无 | 无 | mock fixture 更新 | tests 可稳定注入 valid / invalid structured payload，不维护第二套 stage mapping 或 provider-private payload |
| COG-FIX-004A-BLD-008 | Done | 增加 cognition structured-output integration regression | 附录 A.3 / A.6 / A.8；COG-TODO-027 / 037 | cognition integration | L2 | 新增 `tests/integration/cognition/CognitionStructuredOutputIntegrationTest.cpp`，更新 `tests/integration/cognition/CMakeLists.txt` | end-to-end `decide()` through bridge projection | `CognitionStructuredOutputIntegrationTest` | `cmake --build build-ci --target dasall_cognition_structured_output_integration_test && ctest --test-dir build-ci --output-on-failure -R "CognitionStructuredOutputIntegrationTest"` | COG-FIX-004A-BLD-006、COG-FIX-004A-BLD-007 | 无 | 无 | integration test、CMake 接线 | bridge valid JSON 驱动 `PlanGraph` 与 `ActionDecision` 的主链路径可复验；malformed / invalid payload regression 明确失败或 fallback |
| COG-FIX-004A-BLD-009 | Todo | 补齐 Runtime interaction contract 证明 projected ActionDecision 被消费 | 附录 A.4 / A.8；COG-TODO-027 / COG-FIX-001 | Runtime -> Cognition -> Runtime interaction | L2 | 扩展 `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` 或新增 structured fixture | projected `ActionDecision` -> Runtime first hop / response builder / tool intent handoff | `CognitionRuntimeInteractionContractTest` structured cases | `cmake --build build-ci --target dasall_cognition_runtime_interaction_contract_integration_test && ctest --test-dir build-ci --output-on-failure -R "CognitionRuntimeInteractionContractTest"` | COG-FIX-004A-BLD-008 | 无 | Runtime 不得消费 provider JSON；只消费 projected `ActionDecision` | runtime interaction tests | Runtime 证明消费的是 projection 后的 module-public `ActionDecision`，不是 raw bridge payload；`ExecuteAction` / `DirectResponse` / `ConvergeSafe` 至少各有一条 structured case |
| COG-FIX-004A-BLD-010 | Todo | 接入 diagnostics / telemetry structured projection fields | 附录 A.7；COG-TODO-022 / COG-FIX-005 | telemetry / diagnostics | L2 | 更新 `cognition/src/observability/CognitionTelemetry.*`、`cognition/src/CognitionFacade.cpp`、相关 tests | projection source、schema version、failure code、projected counts | `CognitionTelemetryFieldsTest`、Facade structured tests | `cmake --build build-ci --target dasall_cognition_telemetry_fields_unit_test && ctest --test-dir build-ci --output-on-failure -R "CognitionTelemetryFieldsTest"` | COG-FIX-004A-BLD-006 | COG-FIX-005 production sink 未完成不阻断 focused fields | production sink 另由 COG-FIX-005 收口；本任务只冻结字段与 redaction | telemetry code / tests | structured projection success/failure 均有可观测字段；不记录 raw payload 或 provider-private fields |
| COG-FIX-004A-BLD-011 | Todo | 建立 schema drift / safety negative matrix | 附录 A.5 / A.8；COG-TODO-035 / 037 | negative regression | L2 | 扩展 `tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`、projection tests、integration regression | schema version mismatch、unknown field、raw provider payload leakage、tool arg overreach、delegate_hint disabled | `StageOutputValidatorSchemaTest`、`PlanGraphStructuredProjectionTest`、`ActionDecisionStructuredProjectionTest`、`CognitionStructuredOutputIntegrationTest` | `ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest" && ctest --test-dir build-ci --output-on-failure -R "StructuredProjectionTest" && ctest --test-dir build-ci --output-on-failure -R "CognitionStructuredOutputIntegrationTest"` | COG-FIX-004A-BLD-008 | 无 | 无 | negative tests | schema drift、unknown fields、redaction bypass、tool execution overreach 均可被自动化拦截 |
| COG-FIX-004A-BLD-012 | Todo | 回写 COG-FIX-004 完成证据与 Gate 结果 | 附录 A.11 / A.12；DASALL 开发执行规范 | docs / worklog / SSOT evidence | L2 | 更新总账附录 A、`docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`，必要时更新 cognition deliverable | `COG-FIX-004` status、`Gate-COG-FIX004A-*`、validation commands | docs consistency + final focused ctest | `rg -n "COG-FIX-004" docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md && rg -n "Gate-COG-FIX004A" docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md` | COG-FIX-004A-BLD-001 ~ 011 | 任一 Build task 未通过不得 Done | 全部 Build tasks 通过后回写 | docs / worklog / deliverable | `COG-FIX-004` 可从 Todo 改为 Done；证据包含命令、结果、风险和不可外推范围 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 补设计 / 评审解阻 | COG-TODO-001 ~ 004 | 001~003 可并行，004 在其后收口 | 先消除接口、taxonomy、caller seam 和 mock seam 缺口，避免后续 bridge / integration 返工 |
| B 骨架与公共接口面 | COG-TODO-005 ~ 010 | 005 串行起步；006 紧随；007~009 可并行；010 收口 | 先建立 cognition include 根、类型与接口，再进入实现 |
| C 配置 / 策略 / 输入边界 | COG-TODO-011 ~ 013 | 011 与 013 可并行；012 依赖 011 | 先把投影、阶段策略和 invalid input 统一收敛 |
| D 五段主链实现 | COG-TODO-014 ~ 019 | 014、015、017、019 可并行；016 依赖 015；018 依赖 017 | 先完成感知、规划、反思、终态构造，再收口决策与写回提示 |
| E 桥接 / 校验 / 观测 / 门面 | COG-TODO-020 ~ 023 | 020、021、022 分段并行；023 最后串联 | stage taxonomy 清理后再实现 bridge / validator / telemetry，并最终由 facade 收口 |
| F 测试支撑与拓扑 | COG-TODO-024 ~ 025 | 024 先行；025 紧随 | 没有认知专用 mocks 和 integration discoverability，就不推进 happy path / failure / profile gate |
| G 运行链路与质量门 | COG-TODO-026 ~ 029 | 026 先 happy path；027 再交互契约；028 / 029 可并行 | 先打通主成功链，再收口失败注入与 profile 兼容 |
| H 交付证据 | COG-TODO-030 | 串行 | 所有 Gate 通过 / 残余风险与 blocker 状态集中回写 |
| I 评审后补强闭环 | COG-TODO-031 ~ 038 | 031 / 032 先补生产接线；033 / 034 串联 Runtime 语义消费；035 / 036 可并行；037 统一负例矩阵；038 收口证据 | 把“可执行基线”推进为更可信的生产可用闭环，避免 bridge、profile、runtime signal、schema 和 gate evidence 只停留在局部证明 |
| J 复验后验收修复 | COG-TODO-039 ~ 044 | 039 先修 build 聚合与 runtime fixture；040 / 041 可并行收敛 response 与 reflection；042 在 039 ~ 041 后统一复验；043 / 044 可在 042 前后并行收口文档和 warning | 把 2026-04-28 Build 复验发现的可复现失败转为最小修复任务，并用 Gate-COG-12 覆盖当前可合并结论 |
| K structured projection authoritative consumption | COG-FIX-004A-DOC-001、COG-FIX-004A-BLD-001 ~ 012 | DOC-001 先冻结 schema / projection / fallback / diagnostics；001 / 002 串行建立 registry 与 token view；003 / 004 projector 可串并结合；005 / 006 收 Facade authoritative consumption；007 / 008 / 009 / 010 / 011 收 tests、runtime interaction、telemetry 与负例；012 最后回写证据 | 把方案 A 从总账附录推进为 cognition 自身可执行任务簇；在 Gate-COG-FIX004A-05 通过前，不得把 `COG-FIX-004` 标为 Done |

### 7.2 必过门禁表

| Gate ID | 对应设计 Gate | 门禁名称 | 触发时机 | 通过条件 | 未通过后动作 |
|---|---|---|---|---|---|
| Gate-COG-01 | TODO 新增 | 接缝统一门 | 进入 Build 前 | COG-TODO-001 ~ 004 完成，接口口径、stage taxonomy、runtime caller seam、cognition-specific mock seam 唯一 | 停止 bridge / facade / integration 任务，先回到补设计 |
| Gate-COG-02 | 详设 Gate-01 | 骨架替换门 | COG-TODO-005 ~ 006 后 | `dasall_cognition` 不再是 placeholder-only，`CognitionInterfaceSurfaceTest` discoverable | 修正 CMake / test topology |
| Gate-COG-03 | 详设 Gate-02/03 | 公共接口冻结门 | COG-TODO-007 ~ 010 后 | request/result、plan/action/belief hint、公共接口全部可编译，且 contracts 不回退 | 回退到对象 / 接口定义任务 |
| Gate-COG-04 | TODO 新增 | 策略与边界门 | COG-TODO-011 ~ 013 后 | projector / resolver / invalid input 路径全部可验证 | 停止阶段实现，先修配置投影与边界校验 |
| Gate-COG-05 | 详设 Gate-02/03/08 | 五段主链单测门 | COG-TODO-014 ~ 019 后 | 五段组件单测全绿，BudgetAware / TemplateFallback / BeliefUpdate 路径可二值断言 | 逐组件回退，不进入 facade 集成 |
| Gate-COG-06 | 详设 Gate-07/10 | Bridge / Validator / Telemetry 门 | COG-TODO-020 ~ 023 后 | StageModelHintProjection、schema invariant、telemetry redaction、facade flow 全部通过 | 停止 runtime happy path，先修 bridge / validator / telemetry |
| Gate-COG-07 | TODO 新增 | Integration discoverability 门 | COG-TODO-024 ~ 025 后 | `ctest -N` 可发现 cognition integration 用例 | 修复 tests/integration/cognition 拓扑 |
| Gate-COG-08 | 详设 Gate-04 | Runtime happy path 门 | COG-TODO-026 后 | `RuntimeCognitionLoopSmokeTest` 与 `CognitionRuntimeIntegrationTest` 通过 | 不宣称主链 ready，回退到 facade / fixture / topology |
| Gate-COG-09 | 详设 Gate-05/09 | 交互契约与失败门 | COG-TODO-027 ~ 028 后 | 交互契约、错误回流、failure injection 全部通过且 shared contracts 不回退 | 回退到 runtime seam / schema validator / bridge |
| Gate-COG-10 | 详设 Gate-06 | Profile 兼容与证据门 | COG-TODO-029 ~ 030 后 | 五档 profile 结论明确，Gate 证据与残余风险全部回写 | 保持专项为“部分可执行”，不得宣称全量 ready |
| Gate-COG-11 | 评审后新增 | 生产闭环补强门 | COG-TODO-031 ~ 038 后 | bridge 主链接入、profile 生产投影、Runtime 语义信号消费、structured schema、真实 integration evidence 与负例矩阵全部通过 | 保持专项为“可执行基线 + 待补强”，不得宣称 production ready |
| Gate-COG-FIX004A-01 | TODO 新增 | Schema / projection design gate | COG-FIX-004A-DOC-001 后 | schema baseline、projection contract、fallback policy、diagnostics 均冻结并跨文档一致 | 回到 DOC-001，不进入 Build |
| Gate-COG-FIX004A-02 | TODO 新增 | Projector unit gate | COG-FIX-004A-BLD-003 / 004 后 | PlanGraph / ActionDecision projector 正负例全绿，且 object invariant validator 继续通过 | 回退 projector，不改 Facade |
| Gate-COG-FIX004A-03 | TODO 新增 | Facade authoritative consumption gate | COG-FIX-004A-BLD-005 / 006 后 | structured payload 成为 Facade 主链对象来源；invalid payload fail-closed 或显式 fallback | 回退 Facade 接线，保留 projector focused tests |
| Gate-COG-FIX004A-04 | TODO 新增 | Runtime interaction and negative matrix gate | COG-FIX-004A-BLD-008 ~ 011 后 | runtime interaction、structured integration、schema drift、安全负例、telemetry fields 全绿 | 回退对应 owner，不回写 Done |
| Gate-COG-FIX004A-05 | TODO 新增 | Evidence writeback gate | COG-FIX-004A-BLD-012 后 | TODO、worklog、deliverable 与命令结果一致，且不可外推范围明确 | `COG-FIX-004` 保持 Todo / Blocked |

## 8. 阻塞项与解阻条件

| Blocker ID | 对应设计 Blocker | 阻塞项 | 当前影响 | 解阻条件 | 回退策略 |
|---|---|---|---|---|---|
| COG-BLK-001 | TODO 新增 | 已解阻：`ICognitionEngine` 公开口径已从旧 `step()` 草图收敛为 `decide()` / `reflect()` / `IResponseBuilder::build()` | COG-TODO-007 / 010 / 023 可按三入口推进，不再因公共口径冲突返工 | 已完成 COG-TODO-001，交付物：docs/todos/cognition/deliverables/COG-TODO-001-ICognitionEngine公共接口口径收敛.md | 保持接口与 supporting types module-local，不推进跨模块正式接线 |
| COG-BLK-002 | TODO 新增 | 已解阻：cognition 与 llm 的 stage taxonomy 已统一为 `planning/execution/reflection/response` canonical key，并补齐 StageModelHint 映射表 | COG-TODO-009 / 011 / 012 / 020 / 029 可按 canonical key 推进，不再需要测试私有映射 | 已完成 COG-TODO-002，交付物：docs/todos/cognition/deliverables/COG-TODO-002-stage-taxonomy与StageModelHint映射收敛.md | 若旧 profile-source 仍有 `planner/responder`，归一化只能发生在 profile provider / projector 边界 |
| COG-BLK-003 | 详设 B02 | 已解阻：Runtime caller fixture 与 ActionDecision→FSM 第一跳口径已冻结；legacy runtime smoke 仍可保留为旧路径但不再作为 cognition gate | Runtime happy path、交互契约、写回时序已有设计验收口径，生产测试仍由 COG-TODO-026/027 落地 | 已完成 COG-TODO-003，交付物：docs/todos/cognition/deliverables/COG-TODO-003-runtime-caller-fixture与FSM口径收敛.md | 在 COG-TODO-026 前仍不宣称主链 ready，只允许用 fixture 口径推进 mock / topology |
| COG-BLK-004 | 详设 B03 | 已解阻：`MockLLMManager`、`MockCognitionFixture` 与 failure/profile scenario helper 的职责边界已冻结且真实 header 已落盘 | COG-TODO-020 / 022 / 023 / 026 ~ 029 现在可以统一复用 cognition-specific mock seam，不再借用 `MockLLMAdapter` 旧路径伪装 cognition gate | 已完成 COG-TODO-004、024；交付物：docs/todos/cognition/deliverables/COG-TODO-004-cognition测试fixture口径收敛.md、docs/todos/cognition/deliverables/COG-TODO-024-cognition测试fixture实现收敛.md | integration discoverability 仍由 COG-TODO-025 收口，024 不提前宣称 integration ready |
| COG-BLK-005 | 详设 B01 | `IPlanner`、`PlanGraph`、`ActionDecision` supporting contracts 未冻结 | 任何 shared admission 或 breaking change 评审都不具备条件 | 继续沿用 module public surface；如需 admission，另起 contracts 评审 | 本专项内不推进 contracts 扩张，只保留 module-local / module-public 形态 |
| COG-BLK-006 | 详设 B04 | 已解阻：profile→cognition 配置投影已由 `CognitionConfigProjector` 提供，后续可在 profile gate 中复用同一投影视图 | COG-TODO-012 / 029 可直接消费 projector，不再缺失 profile→cognition 配置链路 | 已完成 COG-TODO-011（CognitionConfigProjector）并具备 unit 验证与交付物证据 | 在 COG-TODO-029 前仍不提前宣称五档 profile integration ready |

## 9. 验收与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| 单元测试 | 请求/结果对象、配置投影、阶段策略、输入边界、五段组件、bridge、validator、telemetry、facade | InterfaceSurface、BudgetAware、TemplateFallback、SchemaInvariant、TelemetryRedaction | 每类组件至少 1 条成功路径和 1 条失败 / 降级路径 |
| 契约回归 | GoalContract、BeliefState、ContextPacket、Observation、ReflectionDecision、AgentResult、MainFlowContractE2E | cognition 依赖这些 contracts 时不引入边界漂移 | 相关 contract tests 全绿 |
| 集成测试 | runtime + cognition happy path、交互契约、failure injection、profile compatibility | RuntimeCognitionLoopSmoke、CognitionRuntimeIntegration、InteractionContract、FailureInjection、ProfileCompatibility | 主成功链、失败链和五档 profile 均有明确结论 |
| discoverability | tests/unit/cognition、tests/integration/cognition | `ctest -N` 可发现 cognition unit / integration 测试 | 不允许 hidden test target |
| 交付证据 | 专项 TODO 与 worklog 回写 | Gate 结论、命令、残余风险、后继动作 | 每个 Gate 均有可回溯证据 |

### 9.2 统一验收命令建议

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_cognition dasall_unit_tests dasall_contract_tests dasall_integration_tests && \
ctest --test-dir build-ci -N && \
ctest --test-dir build-ci --output-on-failure -R "Cognition|RuntimeCognitionLoopSmoke|GoalContractFieldContractTest|BeliefStateContractTest|ContextPacketFieldContractTest|ObservationContractTest|ReflectionDecisionContractTest|AgentResultContractTest|MainFlowContractE2ETest"
```

### 9.3 质量门清单

| Gate | 对应设计 Gate | 通过条件 | 对应任务 |
|---|---|---|---|
| Gate-COG-01 | TODO 新增 | 公开接口口径、stage taxonomy、caller seam、mock seam 已统一 | COG-TODO-001 ~ 004 |
| Gate-COG-02 | 详设 Gate-01 | `dasall_cognition` 已退出 placeholder-only 状态 | COG-TODO-005 ~ 006 |
| Gate-COG-03 | 详设 Gate-02/03 | 请求/结果对象、PlanGraph / ActionDecision / BeliefUpdateHint、公共接口冻结 | COG-TODO-007 ~ 010 |
| Gate-COG-04 | TODO 新增 | projector / resolver / invalid input 路径全绿 | COG-TODO-011 ~ 013 |
| Gate-COG-05 | 详设 Gate-02/03/08 | 五段主链单测全绿 | COG-TODO-014 ~ 019 |
| Gate-COG-06 | 详设 Gate-07/10 | bridge / validator / telemetry / facade 全绿 | COG-TODO-020 ~ 023 |
| Gate-COG-07 | TODO 新增 | cognition integration discoverable | COG-TODO-024 ~ 025 |
| Gate-COG-08 | 详设 Gate-04 | runtime happy path 通过 | COG-TODO-026 |
| Gate-COG-09 | 详设 Gate-05/09 | 交互契约与 failure injection 通过 | COG-TODO-027 ~ 028 |
| Gate-COG-10 | 详设 Gate-06 | profile compatibility 与交付证据回写完成 | COG-TODO-029 ~ 030 |
| Gate-COG-11 | 评审后新增 | bridge / profile / runtime signals / reflection / schema / evidence 的生产闭环补强通过 | COG-TODO-031 ~ 038 |
| Gate-COG-12 | 复验后新增 | 统一验收构建聚合、runtime fixture、response bridge/fallback、reflection active plan、文档证据与 warning hygiene 复验通过 | COG-TODO-039 ~ 044 |
| Gate-COG-FIX004A-01 | TODO 新增 | schema baseline、projection contract、fallback policy、diagnostics 跨文档冻结一致 | COG-FIX-004A-DOC-001 |
| Gate-COG-FIX004A-02 | TODO 新增 | projector unit 正负例与 invariant validator 全绿 | COG-FIX-004A-BLD-003 ~ 004 |
| Gate-COG-FIX004A-03 | TODO 新增 | structured payload 成为 Facade 主链对象来源，且 invalid payload fail-closed / 显式 fallback | COG-FIX-004A-BLD-005 ~ 006 |
| Gate-COG-FIX004A-04 | TODO 新增 | runtime interaction、structured integration、schema drift、安全负例、telemetry fields 全绿 | COG-FIX-004A-BLD-008 ~ 011 |
| Gate-COG-FIX004A-05 | TODO 新增 | `COG-FIX-004` 证据、Gate 与不可外推范围回写一致 | COG-FIX-004A-BLD-012 |

### 9.4 当前 Gate 状态（2026-04-28）

| Gate | 当前状态 | 证据 | 备注 |
|---|---|---|---|
| Gate-COG-10 | Pass（已更正口径） | COG-TODO-029、030、036 | profile compatibility 证据已回落到真实 executable；repo-wide integration 残余不归 cognition owner |
| Gate-COG-11 | 历史记录：Pass（cognition scope）；复验后需由 Gate-COG-12 覆盖 | COG-TODO-031 ~ 038；2026-04-28 Build 复验发现统一验收仍有失败 | 保留 031 ~ 038 的历史完成证据，但不再单独作为 production-ready 合并结论 |
| Gate-COG-12 | Changes Requested / Pending | COG-TODO-039 ~ 044；2026-04-28 复验失败命令 | 039 ~ 044 完成并跑通统一验收前，不应宣称 cognition 具备完整可合并验收证据 |
| Gate-COG-FIX004A-01 | Pass | COG-FIX-004A-DOC-001；认知详设 6.13.4 / 13.2；本专项 TODO 6.7 | schema baseline、projection contract、fallback policy、diagnostics 与 Gate 已跨文档冻结 |
| Gate-COG-FIX004A-02 | Pass | COG-FIX-004A-BLD-003 / 004；`PlanGraphStructuredProjectionTest`、`ActionDecisionStructuredProjectionTest` | PlanGraph / ActionDecision projector 正负例已具 focused evidence，且 invariant validator 继续通过 |
| Gate-COG-FIX004A-03 | Pass | COG-FIX-004A-BLD-005 / 006；`CognitionFacadeStructuredPlanOutputTest`、`CognitionFacadeStructuredActionOutputTest` | structured payload 已成为 Facade 主链对象来源；invalid payload 具 fail-closed / explicit fallback focused evidence |
| Gate-COG-FIX004A-04 | Pending | 待 COG-FIX-004A-BLD-008 ~ 011 | runtime interaction、integration regression 与 safety negative matrix 仍待 build 证据 |
| Gate-COG-FIX004A-05 | Pending | 待 COG-FIX-004A-BLD-012 | `COG-FIX-004` 完成证据、worklog 与风险收口仍未回写 |

## 10. 风险与回退策略

### 10.1 风险表

说明：COG-R01 ~ R08 与认知详设 §11.1 保持 ID 一致，确保跨文档可追溯。COG-R09 ~ R12 为本专项 TODO 新增的工程执行风险；COG-R13 ~ R18 为 2026-04-27 评审后新增的生产闭环补强风险；COG-R19 ~ R23 为 2026-04-28 复验后新增的可合并验收风险。

| Risk ID | 对应设计 Risk | 风险 | 等级 | 影响 | 缓解动作 |
|---|---|---|---|---|---|
| COG-R01 | 详设 R01 | cognition 侵入 prompt/message 组装，破坏 ADR-006 边界 | High | ContextPacket/Prompt 层级混淆，memory/llm 权责失守 | 强制经 CognitionLlmBridge 只传 stage hints，不持有 PromptRegistry/Composer；COG-TC003 约束 + 020/022 redaction 验证 |
| COG-R02 | 详设 R02 | Reflection 与 Recovery 重新混淆，破坏 ADR-007 边界 | High | 测试口径失真，cognition 侵入恢复执行 | 反思接口只返回 suggestion-only ReflectionDecision，不返回 retry_after/backoff；COG-TC004 约束 + 017 单测验证 |
| COG-R03 | 详设 R03 | PlanGraph / ActionDecision / BeliefUpdateHint 被过早推进 shared contracts | High | contracts 返工，跨模块耦合扩大 | 明确保持 module-public，相关任务不出 contracts |
| COG-R04 | 详设 R04 | runtime 继续绕过 cognition，导致后续主链 smoke 虚假通过 | High | 无法证明 cognition 真正进入主循环 | 003、026 必须替换当前 `RuntimeSmokeTest` 旁路路径 |
| COG-R05 | 详设 R05 | edge_minimal 下五段逻辑实现成本过高，测试要求与 profile 策略不一致 | Medium | profile gate 不稳 | 通过 011 / 012 / 029 验证"逻辑保留、实现降级"而非关闭 cognition；edge_minimal 下 Planner/Reasoner 可降级为规则路径 |
| COG-R06 | 详设 R06 | 跨子系统交互契约歧义，Runtime/Cognition/LLM 三方集成时错误处理链路踢皮球 | High | 集成困难，错误回流路径不收敛 | 依据 6.14 精化交互契约编写集成用例；错误回流统一终止于 Runtime；027 交互契约验证必须覆盖三方 |
| COG-R07 | 详设 R07 | 单阶段超时未隔离，某一阶段卡死拖垮主链 | Medium | runtime 无响应，failure gate 虚化 | 012 / 023 按 deadline_ms 隔离阶段 |
| COG-R08 | 详设 R08 | BudgetContext / ContextSufficiencySignal 虽有设计但未进入实际行为 | Medium | 基于不完整上下文做错误决策 | 012 / 015 / 029 必须覆盖 budget-aware 与 context reload recommendation |
| COG-R09 | TODO 新增 | 测试支撑长期依赖 `MockLLMAdapter` / `MockTool` 旧路径，无法准确覆盖认知边界 | Medium | unit / integration 误判 | 004 已冻结 cognition-specific fixture 口径；024 已完成真实 header 落盘，025 / 026 / 028 / 029 继续把后续 smoke / failure / profile gate 迁移到新 seam |
| COG-R10 | TODO 新增 | 公共接口口径继续漂移，导致 runtime 消费反复返工 | High | interface / types / facade 全链返工 | 001 先行，任何实现前先统一 `step()` vs 三入口口径 |
| COG-R11 | TODO 新增 | cognition↔llm stage taxonomy 不一致，桥接和 profile 测试引入隐藏映射 | High | bridge / projector / profile gate 不可信 | 002 先行，StageModelHint 与 `stage_routes` 使用同一 taxonomy |
| COG-R12 | TODO 新增 | telemetry 泄漏 provider-private 字段或原始上下文 | High | 违反 llm / ADR 边界，形成合规与调试风险 | 020 / 021 / 022 必须覆盖 redaction 和 fail-open |
| COG-R13 | 评审后新增 | `CognitionLlmBridge` 只在 focused tests 中存在，主链未真实调用 | High | llm bridge、schema 与 profile route 证据失真 | COG-TODO-031 通过 dependency injection 和 mock recording 证明主链调用 |
| COG-R14 | 评审后新增 | profile compatibility 只验证 terminal status，未证明 profile→cognition 投影被消费 | High | edge_minimal / factory_test 等策略差异可能虚假通过 | COG-TODO-032 强制断言 `CognitionConfigProjector` 与 `StagePolicyResolver` 的生产注入结果 |
| COG-R15 | 评审后新增 | Runtime 丢弃 `BeliefUpdateHint`、`ContextSufficiencySignal` 或 `ReflectionDecision` | High | cognition 返回的语义信号无法影响 memory、context refresh、retry / abort 行为 | COG-TODO-033 / 034 补 runtime 消费路径和交互契约断言 |
| COG-R16 | 评审后新增 | `StageOutputValidator` 依赖字符串扫描，schema 可被伪字段或 malformed JSON 绕过 | High | LLM 输出校验不可信，failure gate 可能漏判 | COG-TODO-035 升级结构化 JSON / schema 校验并补负例 |
| COG-R17 | 评审后新增 | integration placeholder alias 被计入 cognition gate 通过率 | Medium | 交付证据膨胀，真实测试覆盖被高估 | COG-TODO-036 移除或转换为真实 executable alias，重建 test count |
| COG-R18 | 评审后新增 | COG-TODO-030 证据与后续缺口边界不清，容易误读为 production ready | Medium | 后续补强优先级丢失，全量 integration 残余被弱化 | COG-TODO-038 回写 Gate-COG-11、风险状态和 worklog |
| COG-R19 | 复验后新增 | 统一验收构建目标未聚合 runtime cognition smoke，导致 `ctest -N` 可发现但运行时 missing executable | High | gate discoverability 与 build target 依赖不一致，CI 可重复失败 | COG-TODO-039 把 runtime cognition smoke 纳入 `dasall_unit_tests` 聚合并复跑 discoverability |
| COG-R20 | 复验后新增 | runtime 最小 init fixture 缺少 cognition canonical stage routes | High | `AgentFacade::init()` 在真实 cognition composition 下 fail-closed，runtime surface test 与生产约束冲突 | COG-TODO-039 统一 runtime fixture 的 `planning/execution/reflection/response` route 或显式 mock cognition ports |
| COG-R21 | 复验后新增 | response bridge 与 template fallback 契约未收敛，profile compatibility / runtime integration / review regression 断言互相冲突 | High | 非 factory profile 是否必须调用 response bridge 无法二值判定，集成测试不可信 | COG-TODO-040 先冻结 response 行为口径，再同步生产逻辑、fixture 与测试断言 |
| COG-R22 | 复验后新增 | `ReflectionRequest.active_plan` 设计语义未进入生产 façade 路径 | High | ReflectionEngine 无法基于当前计划节点 success_signal 做偏差分析，详设能力被静默降级 | COG-TODO-041 以 additive optional 字段打通 active PlanGraph，并补 façade/runtime 级回归 |
| COG-R23 | 复验后新增 | 详设当前状态与代码现实不一致，且 cognition 测试存在初始化 warning | Medium | 追溯证据误导后续任务；未来 warning gate 提升时可能被非语义问题阻断 | COG-TODO-043 同步详设状态；COG-TODO-044 清理 cognition 测试初始化 warning |

### 10.1.1 评审后补强风险状态（2026-04-28）

| Risk ID | 当前状态 | 关闭/缓解证据 | 残余说明 |
|---|---|---|---|
| COG-R13 | Closed | COG-TODO-031 + `CognitionFacadeFlowTest` | 主链 bridge 调用已具 focused 证据 |
| COG-R14 | Closed | COG-TODO-032 + `CognitionRuntimePolicyProjectionIntegrationTest` | runtime profile→cognition 投影已在生产初始化路径被消费 |
| COG-R15 | Closed | COG-TODO-033、034 + `CognitionRuntimeInteractionContractTest` | belief/context/reflection 信号均有 runtime owner 消费证据 |
| COG-R16 | Closed | COG-TODO-035 + `StageOutputValidatorSchemaTest` | schema 伪字段与 malformed JSON 绕过已被 fail-closed 阻断 |
| COG-R17 | Closed | COG-TODO-036 + `CognitionReviewRegressionTest` | cognition gate 已不再统计历史空跑别名 |
| COG-R18 | Mitigated | COG-TODO-038 | 001 ~ 030 基线、031 ~ 037 补强与 repo-wide integration 残余已分层记录；infra/plugin 聚合残余仍保留 |

### 10.1.2 复验后风险状态（2026-04-28）

| Risk ID | 当前状态 | 关闭/缓解证据 | 残余说明 |
|---|---|---|---|
| COG-R19 | Open | 待 COG-TODO-039 | `RuntimeCognitionLoopSmokeTest` 已注册但需确保统一 build target 会产出 executable |
| COG-R20 | Open | 待 COG-TODO-039 | runtime 最小 init fixture 需与 cognition canonical route 约束一致 |
| COG-R21 | Open | 待 COG-TODO-040 | response bridge / template fallback 需要先冻结二值判定，再修生产和测试 |
| COG-R22 | Open | 待 COG-TODO-041 | active PlanGraph 不能继续只停留在内部 `ReflectionAnalysisRequest` 与 unit test |
| COG-R23 | Open | 待 COG-TODO-043、044 | 文档状态与 warning hygiene 可在主验收修复后收口 |

### 10.2 回退策略

| 场景 | 回退策略 |
|---|---|
| 公开接口评审迟迟未收敛 | 保持 request / result / interfaces 为 module-local / module-public，暂停 runtime 正式接线 |
| llm stage taxonomy 仍未统一 | 只推进不依赖真实 route key 的本地阶段实现，不宣称 bridge / profile gate ready |
| runtime integration 被阻塞 | 先完成阶段单测、facade fake 流程与 topology 接线，不提前宣称主链闭环 |
| response llm 路径不稳定 | ResponseBuilder 保持单次响应 + 模板降级，不引入 streaming |
| shared admission 被要求提前推进 | 明确退回 contracts 评审流程，本专项继续仅维护 cognition module public surface |
| edge_minimal Planner/Reasoner 降级路径缺失 | Planner/Reasoner 可降级为 rule-based 固定路径（skip plan / hardcoded decide），通过 012 / 029 验证降级行为而非关闭 cognition |
| 评审后补强任务影响既有 Done 记录 | 保留 COG-TODO-001 ~ 030 的完成证据不回写为失败；新增缺口统一进入 COG-TODO-031 ~ 038 和 Gate-COG-11 |
| 复验后发现历史 Gate 结论与当前命令结果冲突 | 保留历史执行记录，但新增 Gate-COG-12 与 COG-TODO-039 ~ 044 覆盖当前可合并验收结论；不得直接把旧 Pass 当作当前 production-ready 证据 |

## 11. 可行性结论

1. 当前专项 TODO 可以直接进入执行，但执行顺序必须严格遵守“先解阻、再骨架、再主链、再 bridge / facade、最后 integration / gate”的顺序。
2. 当前可直接落到的最细粒度是 L3 / L2 混合：请求/结果对象、PlanGraph / ActionDecision / BeliefUpdateHint、公共接口已能落到接口 / 数据结构级；StagePolicyResolver、五段组件、CognitionLlmBridge、StageOutputValidator、CognitionTelemetry、CognitionFacade 仍以组件级最稳妥。
3. 当前前置补设计 / 评审门禁已经全部收敛；剩余阻断转为实现侧 mock header 与 integration discoverability：
   - cognition-specific 测试 fixture 设计口径与真实 `MockLLMManager` / `MockCognitionFixture` header 已落盘；剩余阻断收敛到 integration discoverability（COG-TODO-025）与后续 happy-path / failure / profile gate。
4. 因此，本专项的建议执行策略是：
   - COG-TODO-001 ~ 004 已完成前置补设计 / 评审门禁。
   - 然后并行推进 COG-TODO-005 ~ 019 的 module-local / module-public Build 任务。
   - 再收口 COG-TODO-020 ~ 029 的跨模块接缝与 Gate。
5. 001 ~ 004 与 024 已完成前置解阻，本专项可按当前文档进入 Build 实施；在 COG-TODO-025 完成前，不应把 runtime happy path、profile gate 或 llm integration discoverability 宣称为 ready。
6. 评审后新增的 COG-TODO-031 ~ 038 是 001 ~ 030 之后的生产闭环补强：它们不否定当前可执行基线，但在完成 Gate-COG-11 前，不应把 cognition 宣称为已具备完整 production-ready 证据。
7. 复验后新增的 COG-TODO-039 ~ 044 是当前可合并验收修复：它们覆盖 2026-04-28 统一验收失败、response 语义冲突、Reflection active plan 丢失、详设追溯过期与 warning hygiene；在 Gate-COG-12 通过前，Gate-COG-11 只能作为历史补强记录，不再单独作为 production-ready 结论。

## 12. 未决问题处置表

说明：以下未决问题来源于认知详设 §12，需在对应任务执行中明确处置。

| OQ ID | 来源 | 问题摘要 | 处置方式 | 关联 TODO | 说明 |
|---|---|---|---|---|---|
| COG-OQ01 | 详设 §12 | ContextPacket 中 BeliefState 应否必填 | 采纳：首版 BeliefState 设为必填，default 为空 snapshot | COG-TODO-007 | 避免下游组件对可空字段做防御式编程 |
| COG-OQ03 | 详设 §12 | IPlanner 是否需要支持 delegate hint | 延后：首版关闭 delegate hint，IPlanner 只接受 Goal→PlanGraph | COG-TODO-016 | 待多代理子系统就绪后再开放 delegation 扩展 |
| COG-OQ05 | 详设 §12 | CognitionTelemetry 的 HealthProbe 如何实现 | 设计收口：首版继续用 metrics 汇聚（last_latency / error_count）作为被动 health probe 输入，不引入独立 probe endpoint、独立 cadence 或独立 event publish 通道；系统口径由 INT-TODO-026 / `HealthCadenceAndEventBoundary` 冻结 | COG-TODO-022 | 复杂度低优先；后续若扩展为主动探针，必须先回到 system cadence SSOT |
| COG-OQ06 | 详设 §12 | BudgetContext 首版是否 optional | 采纳：首版 BudgetContext 设为 optional，缺省时使用 profile 默认 budget | COG-TODO-009 | 降低首版集成门槛，但要求 015 验证 budget-tight 收缩行为 |
| COG-OQ08 | 详设 §12 | StageModelHint 中 preferred_provider 如何处理 | 采纳：preferred_provider 字段保留，默认值为空字符串（llm 自行选择）| COG-TODO-009 | 不强制指定 provider，保持 llm 路由自主权 |
| COG-OQ09 | 详设 §12 | StageOutputValidator 的 schema 版本化策略 | 延后：首版 schema 使用 header comment 标记版本，不引入独立 schema registry | COG-TODO-008、009 | 首版保持简单；schema registry 作为后续演进点 |

## 13. 执行记录

### 13.1 COG-TODO-001：ICognitionEngine 公共接口口径收敛（2026-04-24）

1. 任务选择：COG-TODO-001 无前置依赖，是 COG-BLK-001 的最小解阻任务。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-001-ICognitionEngine公共接口口径收敛.md。
3. 架构回写：
   - `docs/architecture/DASALL_Agent_architecture.md` §5.8.4 已把 `ICognitionEngine::step()` 草图替换为三入口公共口径。
   - `docs/architecture/DASALL_Engineering_Blueprint.md` §3.4 已回链三入口为 Build / fixture 的唯一可执行口径。
   - `docs/architecture/DASALL_cognition子系统详细设计.md` §6.6.1 已补 COG-TODO-001 评审结论。
4. 验收命令：
   - `rg -n "ICognitionEngine|step\(|decide\(|reflect\(|build\(" docs/architecture/DASALL_Agent_architecture.md docs/architecture/DASALL_Engineering_Blueprint.md docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md`
5. 验收结论：PASS；检索仍可命中 runtime 自身 `IRuntimeEngine::step()` 和历史说明，但 cognition Build-ready 公共接口只保留 `decide()` / `reflect()` / `IResponseBuilder::build()`。
6. Blocker：COG-BLK-001 已解阻；COG-TODO-007 / 010 / 023 可按三入口继续推进。

### 13.2 COG-TODO-002：stage taxonomy 与 StageModelHint 映射收敛（2026-04-24）

1. 任务选择：COG-TODO-002 无前置依赖，是 COG-BLK-002 的最小解阻任务。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-002-stage-taxonomy与StageModelHint映射收敛.md。
3. 架构回写：
   - `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.2 已冻结 `planning/execution/reflection/response` canonical key 与 cognition 组件映射表。
   - `docs/architecture/DASALL_llm子系统详细设计.md` §6.10.3 已补充 stage route key 约束。
   - `docs/todos/llm/deliverables/LLM-TODO-035-profile-diff-integration设计收敛.md` 已追认 COG-TODO-002 收敛结果。
4. 验收命令：
   - `rg -n "planning|execution|reflection|response|perception|reasoning|StageModelHint|stage_routes" docs/architecture/DASALL_cognition子系统详细设计.md docs/architecture/DASALL_llm子系统详细设计.md docs/todos/llm/deliverables/LLM-TODO-035-profile-diff-integration设计收敛.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md`
5. 验收结论：PASS；canonical stage key 集合、StageModelHint 映射表、legacy alias 禁入边界与后续 Build 映射均可检索。
6. Blocker：COG-BLK-002 已解阻；COG-TODO-009 / 011 / 012 / 020 / 029 可按 canonical key 继续推进。

### 13.3 COG-TODO-003：runtime caller fixture 与 FSM 口径收敛（2026-04-24）

1. 任务选择：COG-TODO-003 无前置依赖，是 COG-BLK-003 的最小解阻任务。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-003-runtime-caller-fixture与FSM口径收敛.md。
3. 架构回写：
   - `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.1 已把 ActionDecision→FSM 映射对齐 Runtime 真实 `RuntimeState` / `TransitionGuardFact`。
   - `CognitionStepRequest` / `ReflectionRequest` / `ResponseBuildRequest` 设计示例已补 `caller_domain`、request / trace / profile 字段。
   - RuntimeCognitionLoopSmokeTest 口径已明确：不得再以 `MockLLMAdapter + MockTool` 旁路串接作为 cognition gate。
4. 验收命令：
   - `rg -n "ActionDecision\.decision_kind|CognitionStepRequest|ReflectionRequest|RuntimeCognitionLoopSmoke|caller_domain|FSM" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md`
5. 验收结论：PASS；真实 Runtime FSM 状态映射、caller fixture 字段、RuntimeCognitionLoopSmoke 断言与 COG-BLK-003 解阻记录均可检索。
6. Blocker：COG-BLK-003 已解阻；COG-TODO-026 / 027 后续可按此 caller fixture 口径落地 production smoke / interaction contract。

### 13.4 COG-TODO-004：cognition 测试 fixture 口径收敛（2026-04-24）

1. 任务选择：COG-TODO-004 依赖 COG-TODO-001 ~ 003，本轮在接口、stage taxonomy 与 runtime caller seam 均已冻结后收敛 testing seam。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-004-cognition测试fixture口径收敛.md。
3. 架构回写：
   - `docs/architecture/DASALL_cognition子系统详细设计.md` §9.3.1 已冻结 `MockLLMManager`、`MockCognitionFixture` 与 FailureProfileScenario 的职责、非职责边界和消费测试。
   - COG-D09 的测试支撑目标已从旧 `MockResponseBuilderSupport.h` 收敛为 `MockLLMManager.h` + `MockCognitionFixture.h`。
   - COG-B03 / COG-BLK-004 已区分设计侧解阻与实现侧落盘：真实 header 与 discoverability 仍由 COG-TODO-024 / 025 关闭。
4. 验收命令：
   - `rg -n "MockLLMManager|MockCognitionFixture|tests/mocks/include" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md`
5. 验收结论：PASS；MockLLMManager、MockCognitionFixture、tests/mocks/include 目录目标、COG-TODO-004 Done 状态与 COG-BLK-004 设计侧解阻记录均可检索。
6. Blocker：COG-BLK-004 已完成设计侧解阻；COG-TODO-020 / 022 / 023 / 026 ~ 029 仍需等待 COG-TODO-024 把 mock header 真正落盘。

### 13.5 COG-TODO-005：cognition 公共 include 布局与 CMake 骨架（2026-04-25）

1. 任务选择：COG-TODO-005 无前置依赖；COG-TODO-001 ~ 004 已完成并解除了接口、stage taxonomy、runtime caller seam 与测试 seam 的设计侧阻塞。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-005-cognition公共include布局与CMake骨架.md。
3. 代码落点：
   - `cognition/CMakeLists.txt` 已登记 `dasall_cognition` public header file set，并继续公开 `cognition/include`。
   - 新增 `cognition/include/IPlanner.h`、`IReasoner.h`、`IReflectionEngine.h` 作为后续 COG-TODO-010 的阶段接口承载头。
   - 删除 `cognition/src/placeholder.cpp`，`dasall_cognition` 不再依赖 placeholder-only 源。
4. 验收命令：
   - `cmake -S . -B build-ci-cog005 -G "Unix Makefiles" && cmake --build build-ci-cog005 --target dasall_cognition dasall_unit_tests`
   - `cmake --build build-ci --target dasall_cognition dasall_unit_tests`
   - `test ! -e cognition/src/placeholder.cpp && ! rg -n "placeholder.cpp|keep_library_non_empty" cognition/CMakeLists.txt cognition/src cognition/include`
5. 验收结论：PASS；Unix Makefiles 干净目录与现有 Ninja `build-ci` 均通过，`dasall_unit_tests` 共 463 个 unit 测试全绿，placeholder 残留负例检索为零。
6. 后续边界：COG-TODO-006 继续接线 `CognitionInterfaceSurfaceTest` discoverability；COG-TODO-007 ~ 010 继续字段与接口签名冻结，不在本轮提前完成。

### 13.6 COG-TODO-006：cognition unit 测试入口接线（2026-04-25）

1. 任务选择：COG-TODO-006 依赖 COG-TODO-005；公共 include 与 `dasall_cognition` 骨架已完成，且本任务无未解 blocker。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-006-cognition-unit测试入口接线.md。
3. 代码落点：
   - `tests/unit/cognition/CMakeLists.txt` 已从 placeholder 注释替换为真实 unit target 注册，生成 `dasall_cognition_interface_surface_unit_test`，并注册 CTest 名称 `CognitionInterfaceSurfaceTest`。
   - `tests/unit/CMakeLists.txt` 已将 `${DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS}` 纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`，确保 `dasall_unit_tests` 聚合先构建 cognition unit executable。
   - 新增 `tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp`，覆盖三入口签名、factory 链接、承载头可 include、legacy `step()` 不存在负例。
4. 验收命令：
   - `cmake -S . -B build-ci-cog006 -G "Unix Makefiles"`
   - `cmake --build build-ci-cog006 --target dasall_unit_tests`
   - `ctest --test-dir build-ci-cog006 -N | rg "CognitionInterfaceSurfaceTest"`
   - `ctest --test-dir build-ci-cog006 -R "CognitionInterfaceSurfaceTest" --output-on-failure`
   - `cmake -S . -B build-ci && cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci -N | rg "CognitionInterfaceSurfaceTest"`
5. 验收结论：PASS；`build-ci-cog006` 与既有 `build-ci` 均通过 `dasall_unit_tests`，464 个 unit 测试全绿，label summary 包含 `cognition = 1 test`；`ctest -N` 在两个构建目录均发现 `Test #21: CognitionInterfaceSurfaceTest`。
6. 后续边界：COG-TODO-007 可复用 `CognitionInterfaceSurfaceTest` 继续收敛 CognitionConfig 与请求/结果对象字段；本轮未提前冻结 COG-TODO-007 ~ 010 的后续字段和阶段接口。

### 13.7 COG-TODO-009：ActionDecision 与 BudgetContext 对象收敛（2026-04-27）

1. 任务选择：COG-TODO-009 依赖 COG-TODO-002、005、006；三项均已完成，COG-BLK-002 已解阻，因此本轮直接进入 Build-ready supporting types 收口。
2. 交付物：docs/todos/cognition/deliverables/COG-TODO-009-ActionDecision与BudgetContext对象收敛.md。
3. 代码落点：
   - `cognition/include/decision/ActionDecision.h` 已从旧 `tool_name` / `tool_arguments_payload` / `response_text` string 字段切换到 `tool_intent_hint`、`response_outline`、`candidate_scores` 等语义对象。
   - `cognition/include/belief/BeliefUpdateHint.h` 已改为 delta-oriented 结构，显式区分 facts / hypotheses / assumptions / evidence deltas、`missing_evidence_refs`、`confidence_hint` 与 `merge_mode`。
   - `cognition/include/CognitionTypes.h` 已新增 `ModelCapabilityTier` 与 `StageModelHint`，并保留 `BudgetContext`、`ContextSufficiencySignal` 为 module-public 信号对象。
   - `cognition/src/CognitionFacade.cpp` 已切换到新 supporting types；`tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp` 已补充 009 字段正例与旧字段负例。
4. 验收命令：
   - `cmake --build build-ci --target dasall_cognition`
   - `cmake --build build-ci --target dasall_cognition_interface_surface_unit_test && ctest --test-dir build-ci -R "^CognitionInterfaceSurfaceTest$" --output-on-failure`
5. 验收结论：PASS；`dasall_cognition` 构建通过，`CognitionInterfaceSurfaceTest` 1/1 passed。`RunCtest_CMakeTools` 在本轮返回通用错误“生成失败”，已按仓库已知 fallback 路径回退到显式 ctest，不判为代码回归。
6. 后续边界：COG-TODO-010 可直接基于已冻结的 `ActionDecision` / `BeliefUpdateHint` / `StageModelHint` 继续收敛 `IPlanner`、`IReasoner`、`IReflectionEngine` 等接口签名；本轮不推进 shared admission。

### 13.8 COG-TODO-011：CognitionConfigProjector 收敛（2026-04-27）

1. 任务选择：COG-TODO-011 依赖 COG-TODO-002、007、010；三项均已完成，且 COG-BLK-006 的唯一解阻动作就是补齐 profile→cognition 配置投影视图。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-011-CognitionConfigProjector收敛.md。
3. 代码落点：
   - 新增 `cognition/src/config/CognitionConfigProjector.h`、`cognition/src/config/CognitionConfigProjector.cpp`，从 `RuntimePolicySnapshot` 投影 `CognitionConfig` 与 `StageModelHint`。
   - 更新 `cognition/CMakeLists.txt`，登记 projector 源文件、增加 `cognition/src` 私有 include，并把 `dasall_profiles` 作为私有依赖接入。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_cognition_config_projection_unit_test` 并为该测试补 `dasall_profiles` 链接和 `cognition/src` 私有 include。
   - 新增 `tests/unit/cognition/CognitionConfigProjectionTest.cpp`，覆盖 profile 默认投影、stage hint 正例、缺失 route 和 legacy alias 负例。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_cognition_config_projection_unit_test"])`
   - `RunCtest_CMakeTools(tests=["CognitionConfigProjectionTest"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_config_projection_unit_test`
5. 验收结论：PASS；首次 Build_CMakeTools 失败并精确暴露 `cognition/src` 私有 include 缺口，补齐同一 slice 后复跑通过。`RunCtest_CMakeTools` 再次返回仓库已知通用错误“生成失败”，因此按既有 fallback 直接执行测试二进制并通过。
6. Blocker：COG-BLK-006 已由本任务关闭；COG-TODO-012 可直接在 projector 之上实现 `StagePolicyResolver`，不再缺 profile→cognition 投影链路。

### 13.9 COG-TODO-012：StagePolicyResolver 收敛（2026-04-27）

1. 任务选择：COG-TODO-012 依赖 COG-TODO-002、007、011；三项均已完成，因此本轮直接进入 resolver 私有组件与 focused unit tests 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-012-StagePolicyResolver收敛.md。
3. 代码落点：
   - 新增 `cognition/src/StagePolicyResolver.h`、`cognition/src/StagePolicyResolver.cpp`，定义私有 `StageExecutionPlan` 与 `StageFallbackMode`，并实现 `resolve_decide_plan()`、`resolve_reflection_plan()`、`resolve_response_plan()`、`derive_stage_model_hint()`。
   - 更新 `cognition/CMakeLists.txt`，把 resolver 纳入 `dasall_cognition` 源列表。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_stage_policy_resolver_unit_test`、`dasall_stage_policy_resolver_profile_diff_unit_test`、`dasall_budget_aware_decision_unit_test`，并为它们补 `dasall_profiles` 与 `cognition/src` 私有 include。
   - 新增 `tests/unit/cognition/StagePolicyResolverTest.cpp`、`StagePolicyResolverProfileDiffTest.cpp`、`BudgetAwareDecisionTest.cpp`，分别覆盖默认策略、profile 差异和 budget-aware 规则。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_stage_policy_resolver_unit_test","dasall_stage_policy_resolver_profile_diff_unit_test","dasall_budget_aware_decision_unit_test"])`
   - `RunCtest_CMakeTools(tests=["StagePolicyResolverTest","StagePolicyResolverProfileDiffTest","BudgetAwareDecisionTest"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_policy_resolver_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_policy_resolver_profile_diff_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_budget_aware_decision_unit_test`
5. 验收结论：PASS；三项 resolver 相关 build target 均通过，`RunCtest_CMakeTools` 继续返回仓库已知通用错误“生成失败”，因此沿用显式二进制 fallback，三项 focused unit tests 全部通过。
6. 后续边界：COG-TODO-013 可在 resolver 已统一收敛的 request-level plan 之上补齐 `InputBoundaryValidator`，不再在 façade 或阶段组件里散落输入校验逻辑。

### 13.10 COG-TODO-013：InputBoundaryValidator 收敛（2026-04-27）

1. 任务选择：COG-TODO-013 依赖 COG-TODO-007、010；两项均已完成，且 011 / 012 已补齐 profile 投影与 request-level stage policy，因此本轮直接进入输入边界 fail-fast 语义收敛。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-013-InputBoundaryValidator收敛.md。
3. 代码落点：
   - 新增 `cognition/src/validation/InputBoundaryValidator.h`、`cognition/src/validation/InputBoundaryValidator.cpp`，统一实现 decide / reflect / response 三类 request 的 required-field 校验与 `ErrorInfo` 生成。
   - 更新 `cognition/src/CognitionFacade.cpp`，将 `decide()`、`reflect()` 与 `ResponseBuilder::build()` 接入统一 validator，invalid input 直接 fail-fast。
   - 更新 `cognition/CMakeLists.txt` 与 `tests/unit/cognition/CMakeLists.txt`，接入 validator 源文件，并新增 `dasall_perception_boundary_validation_unit_test`、`dasall_cognition_facade_invalid_input_unit_test`。
   - 新增 `tests/unit/cognition/PerceptionBoundaryValidationTest.cpp`、`tests/unit/cognition/CognitionFacadeInvalidInputTest.cpp`，分别覆盖 validator 负例和 façade / response builder 的显式错误出口。
4. 验收命令：
   - `ListBuildTargets_CMakeTools()`
   - `Build_CMakeTools(buildTargets=["dasall_perception_boundary_validation_unit_test","dasall_cognition_facade_invalid_input_unit_test"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_boundary_validation_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_invalid_input_unit_test`
5. 验收结论：PASS；两个 focused build target 均通过，显式二进制执行零输出退出；invalid input 已统一映射为 `ValidationFieldMissing` + stage-specific `ErrorInfo`。
6. 后续边界：COG-TODO-014 可直接复用 `InputBoundaryValidator` 作为感知阶段入口边界，不再在 `PerceptionEngine` 内部复制 required-field 检查。

### 13.11 COG-TODO-014：PerceptionEngine 收敛（2026-04-27）

1. 任务选择：COG-TODO-014 依赖 COG-TODO-007、010、011、012、013；五项均已完成，因此本轮直接进入感知阶段私有组件与 focused unit tests 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-014-PerceptionEngine收敛.md。
3. 代码落点：
   - 新增 `cognition/src/perception/PerceptionEngine.h`、`cognition/src/perception/PerceptionEngine.cpp`，实现 `perceive()`、实体抽取、约束投影、歧义检测、澄清问题推导、规则降级与输出校验。
   - 更新 `cognition/CMakeLists.txt`，将 `src/perception/PerceptionEngine.cpp` 纳入 `dasall_cognition`。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_perception_engine_unit_test` 与 `dasall_perception_clarification_rule_unit_test`，并为它们补 `cognition/src` 私有 include。
   - 新增 `tests/unit/cognition/PerceptionEngineTest.cpp`、`tests/unit/cognition/PerceptionClarificationRuleTest.cpp`，并复用既有 `PerceptionBoundaryValidationTest` 作为边界复验。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_perception_engine_unit_test","dasall_perception_clarification_rule_unit_test","dasall_perception_boundary_validation_unit_test"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_engine_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_clarification_rule_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_perception_boundary_validation_unit_test`
5. 验收结论：PASS；首次 focused run 暴露歧义路径置信度未压到澄清阈值以下的局部行为偏差，同一 slice 修补后复跑通过，三项感知相关 unit tests 全部零输出退出。
6. 后续边界：COG-TODO-015 可直接消费 `PerceptionResult` 进入 `Planner` / `PlanGraphBuilder`，不必在计划阶段再重复做感知层的缺槽与澄清判断。

### 13.12 COG-TODO-015：Planner 与 PlanGraphBuilder 收敛（2026-04-27）

1. 任务选择：COG-TODO-015 依赖 COG-TODO-007、008、010、011、012、013；六项均已完成，且 014 已提供可消费的 `PerceptionResult`，因此本轮直接进入 planning 私有组件与 focused unit tests 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-015-Planner与PlanGraphBuilder收敛.md。
3. 代码落点：
   - 新增 `cognition/src/planning/Planner.h`、`cognition/src/planning/Planner.cpp`，以私有 `Planner` 实现 `IPlanner::build_plan()` / `replan()`。
   - 新增 `cognition/src/planning/PlanGraphBuilder.h`、`cognition/src/planning/PlanGraphBuilder.cpp`，实现 open question 路径、DAG 展开、budget 压缩、replan graph 修补与图校验。
   - 更新 `cognition/CMakeLists.txt`，将 planning sources 纳入 `dasall_cognition`。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_planner_plan_graph_unit_test`、`dasall_planner_replan_unit_test`、`dasall_planner_node_budget_unit_test` 并为三者补 `cognition/src` 私有 include。
   - 新增 `tests/unit/cognition/PlannerPlanGraphTest.cpp`、`tests/unit/cognition/PlannerReplanTest.cpp`、`tests/unit/cognition/PlannerNodeBudgetTest.cpp`，分别覆盖 DAG 构建与 open question、replan revision、budget 压缩。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_planner_plan_graph_unit_test","dasall_planner_replan_unit_test","dasall_planner_node_budget_unit_test"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_plan_graph_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_replan_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_planner_node_budget_unit_test`
5. 验收结论：PASS；首次构建暴露 `PlannerReplanTest.cpp` 使用不存在的错误分类枚举与局部 `CognitionConfig` 聚合初始化告警，同一 slice 修补后复跑通过，三条 planner-focused unit tests 全部零输出退出。
6. 后续边界：COG-TODO-016 可直接在 `PlanGraph` 与 `ReplanResult` 的稳定输出之上实现 `Reasoner` / `DecisionProjector`，不必在决策阶段再次回补 planning 图结构与 budget 压缩逻辑。

### 13.13 COG-TODO-016：Reasoner 与 DecisionProjector 收敛（2026-04-27）

1. 任务选择：COG-TODO-016 依赖 COG-TODO-007、008、009、010、012、013、015；七项均已完成，因此本轮直接进入 reasoning 私有组件与 focused unit tests 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-016-Reasoner与DecisionProjector收敛.md。
3. 代码落点：
   - 新增 `cognition/src/reasoning/Reasoner.h`、`cognition/src/reasoning/Reasoner.cpp`，以私有 `Reasoner` 实现 `IReasoner::decide()`，并落规则式 candidate scoring、clarification gate、budget-safe converge 与阈值校验。
   - 新增 `cognition/src/reasoning/DecisionProjector.h`、`cognition/src/reasoning/DecisionProjector.cpp`，统一投影 `tool_intent_hint`、`response_outline` 与 `candidate_scores`。
   - 更新 `cognition/CMakeLists.txt`，将 reasoning sources 纳入 `dasall_cognition`。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_reasoner_action_decision_unit_test`、`dasall_reasoner_clarification_threshold_unit_test`、`dasall_reasoner_conflict_resolution_unit_test` 并为三者补 `cognition/src` 私有 include。
   - 新增 `tests/unit/cognition/ReasonerActionDecisionTest.cpp`、`tests/unit/cognition/ReasonerClarificationThresholdTest.cpp`、`tests/unit/cognition/ReasonerConflictResolutionTest.cpp`，分别覆盖执行 / 直答、澄清阈值、冲突与安全收敛路径。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_reasoner_action_decision_unit_test","dasall_reasoner_clarification_threshold_unit_test","dasall_reasoner_conflict_resolution_unit_test"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_action_decision_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_clarification_threshold_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reasoner_conflict_resolution_unit_test`
5. 验收结论：PASS；构建一次通过，首次合并执行暴露 terminal direct-response plan 被误判为 `ExecuteAction` 的局部行为偏差，同一 slice 修补 active node 解析与 direct-response 分值后复跑通过，三条 reasoner-focused unit tests 全部零输出退出。
6. 后续边界：COG-TODO-017 可直接围绕 `latest_observation`、`PlanGraph` 与 `BeliefState` 的偏差分析实现 `ReflectionEngine`，不必再回补 `ActionDecision` 的 candidate scoring 与 runtime 第一跳映射。

### 13.14 COG-TODO-017：ReflectionEngine 收敛（2026-04-27）

1. 任务选择：COG-TODO-017 依赖 COG-TODO-007、008、009、010、012、013；六项均已完成，因此本轮直接进入 reflection 私有组件与 focused unit tests 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-017-ReflectionEngine收敛.md。
3. 代码落点：
   - 新增 `cognition/src/reflection/ReflectionEngine.h`、`cognition/src/reflection/ReflectionEngine.cpp`，以私有 `ReflectionEngine` 实现 `IReflectionEngine::analyze()`，并落 failure classification、goal-gap 评估、belief invalidation 检测、decision projection 与 contract validation。
   - 更新 `cognition/CMakeLists.txt`，将 reflection source 纳入 `dasall_cognition`。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_reflection_engine_decision_unit_test`、`dasall_reflection_engine_belief_invalidation_unit_test`、`dasall_reflection_engine_conservative_abort_unit_test` 并补 `cognition/src` 私有 include。
   - 新增 `tests/unit/cognition/ReflectionEngineDecisionTest.cpp`、`tests/unit/cognition/ReflectionEngineBeliefInvalidationTest.cpp`、`tests/unit/cognition/ReflectionEngineConservativeAbortTest.cpp`，分别覆盖 retry/continue、belief invalidation replan 与高风险 abort-safe。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_reflection_engine_decision_unit_test","dasall_reflection_engine_belief_invalidation_unit_test","dasall_reflection_engine_conservative_abort_unit_test"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_decision_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_belief_invalidation_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_conservative_abort_unit_test`
5. 验收结论：PASS；首轮构建暴露 `ReflectionEngine::analyze()` 签名误加 `const` 导致 override 失败，同一 slice 修正后复跑通过，三条 reflection-focused unit tests 全部零输出退出。
6. 后续边界：COG-TODO-018 可直接围绕 `ReflectionDecision` 与 `BeliefUpdateHint` 关系实现 `BeliefUpdateSynthesizer`，不必再回补 reflection failure attribution 或 shared `ReflectionDecision` 契约边界。

### 13.15 COG-TODO-018：BeliefUpdateSynthesizer 收敛（2026-04-27）

1. 任务选择：COG-TODO-018 依赖 COG-TODO-008、009、010、017；四项均已完成，因此本轮直接进入 belief writeback hint 私有组件与 focused unit tests 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-018-BeliefUpdateSynthesizer收敛.md。
3. 代码落点：
   - 新增 `cognition/src/belief/BeliefUpdateSynthesizer.h`、`cognition/src/belief/BeliefUpdateSynthesizer.cpp`，实现 decide / reflection 路径的 delta 折叠、`merge_deltas()`、`normalize_evidence_refs()` 与无证据 delta 丢弃。
   - 更新 `cognition/CMakeLists.txt`，将 belief synthesizer source 纳入 `dasall_cognition`。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_belief_update_synthesizer_unit_test`、`dasall_belief_update_merge_mode_unit_test`、`dasall_belief_update_evidence_dedup_unit_test` 并补 `cognition/src` 私有 include。
   - 新增 `tests/unit/cognition/BeliefUpdateSynthesizerTest.cpp`、`tests/unit/cognition/BeliefUpdateMergeModeTest.cpp`、`tests/unit/cognition/BeliefUpdateEvidenceDedupTest.cpp`，分别覆盖 decide-path delta 折叠、reflection replace merge mode 与 evidence 去重。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_belief_update_synthesizer_unit_test","dasall_belief_update_merge_mode_unit_test","dasall_belief_update_evidence_dedup_unit_test"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_synthesizer_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_merge_mode_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_evidence_dedup_unit_test`
5. 验收结论：PASS；首轮构建暴露 `append_unique_delta()` 模板分支误访问 `evidence_ref` 字段，同一 slice 修正为真正的 `else if constexpr` 链后复跑通过，三条 belief-update-focused unit tests 全部零输出退出。
6. 后续边界：COG-TODO-019 可直接围绕 `ActionDecision.response_outline`、`latest_observation` 与 template fallback 实现 `ResponseBuilder`，不必再回补 belief hint 的 merge / dedup / best-effort 写回边界。

### 13.16 COG-TODO-024：cognition 测试 fixture 实现收敛（2026-04-27）

1. 任务选择：COG-TODO-020 / 022 / 023 与 026 ~ 029 仍受 COG-BLK-004 约束；在设计侧 COG-TODO-004 已冻结 mock seam 职责边界后，本轮先完成实现侧最小解阻任务 COG-TODO-024。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-024-cognition测试fixture实现收敛.md。
3. 代码落点：
   - 新增 `tests/mocks/include/MockLLMManager.h`，提供基于 `llm::ILLMManager` 的 scripted stage result / failure helper、generate handler 与 request recording。
   - 新增 `tests/mocks/include/MockCognitionFixture.h`，固定 `runtime.agent_orchestrator` caller shape，并生成 `CognitionStepRequest`、`ReflectionRequest`、`ResponseBuildRequest` 与最小合法 contracts 对象。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_mock_cognition_fixture_surface_unit_test` 并补 `cognition/src` include。
   - 新增 `tests/unit/cognition/MockCognitionFixtureSurfaceTest.cpp`，覆盖 llm manager scripted behavior、runtime caller request helper 与 shared cognition owner surface。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_mock_cognition_fixture_surface_unit_test"])`
   - `RunCtest_CMakeTools(tests=["MockCognitionFixtureSurfaceTest"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_mock_cognition_fixture_surface_unit_test`
5. 验收结论：PASS；首轮构建暴露 `ModelSelectionHint` include path 漏接，同一 slice 修正后目标成功编译链接；`RunCtest_CMakeTools` 继续触发仓库既有工具态 `生成失败`，按仓库基线回退显式二进制执行后零输出退出，证明 mock seam 已可被当前 public surface 直接消费。
6. 后续边界：COG-BLK-004 已关闭，COG-TODO-020 / 022 / 023 可直接复用新 mock seam 推进 bridge / telemetry / facade；integration discoverability 仍由 COG-TODO-025 收口，不在本轮提前宣称 ready。

### 13.17 COG-TODO-020：CognitionLlmBridge 收敛（2026-04-27）

1. 任务选择：COG-TODO-024 已关闭 COG-BLK-004，且 COG-TODO-002、007、009、010 已冻结 canonical stage key、输入对象与 `StageModelHint`，因此本轮直接进入 llm bridge private owner 与 focused tests 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-020-CognitionLlmBridge收敛.md。
3. 代码落点：
   - 新增 `cognition/src/llm/CognitionLlmBridge.h`、`cognition/src/llm/CognitionLlmBridge.cpp`，实现 `StageLlmCallRequest` / `StageSchemaSpec` / `StageBudgetHint` / `StageLlmCallResult` / `LlmFailureProjection` 及 bridge 五个核心方法。
   - 更新 `cognition/CMakeLists.txt`，把 bridge source 纳入 `dasall_cognition`，并补上 `dasall_llm` 私有依赖。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_cognition_llm_bridge_projection_unit_test`、`dasall_cognition_llm_bridge_error_mapping_unit_test`、`dasall_stage_model_hint_projection_unit_test`，并为它们补 `cognition/src` include 与 `dasall_llm` 链接。
   - 新增 `tests/unit/cognition/CognitionLlmBridgeProjectionTest.cpp`、`CognitionLlmBridgeErrorMappingTest.cpp`、`StageModelHintProjectionTest.cpp`，分别覆盖 stage/schema/budget 投影、llm failure 映射与 canonical stage hint 投影。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_cognition_llm_bridge_projection_unit_test","dasall_cognition_llm_bridge_error_mapping_unit_test","dasall_stage_model_hint_projection_unit_test"])`
   - `RunCtest_CMakeTools(tests=["CognitionLlmBridgeProjectionTest","CognitionLlmBridgeErrorMappingTest","StageModelHintProjectionTest"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_llm_bridge_projection_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_llm_bridge_error_mapping_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_model_hint_projection_unit_test`
5. 验收结论：PASS；首轮构建暴露 llm public include 路径误写，第二轮构建暴露测试侧 `ModelSelectionHint` 只有前置声明；同一 slice 修补 include 和测试显式依赖后复跑通过。`RunCtest_CMakeTools` 继续命中仓库既有工具态 `生成失败`，回退显式二进制执行后三条桥接测试全部零输出退出。
6. 后续边界：COG-TODO-021 可直接消费 bridge supporting structs 和错误 surface 收口 schema / invariant 校验；COG-TODO-022 / 023 可复用 bridge 的 canonical stage、diagnostics 与 redaction 结果，但本轮不提前串联 façade 主链。

### 13.18 COG-TODO-021：StageOutputValidator 收敛（2026-04-27）

1. 任务选择：COG-TODO-020 已冻结 `StageLlmCallResult` 与 canonical stage hint，COG-TODO-008、009、019 已冻结 PlanGraph、ActionDecision、ResponseBuildResult supporting types，因此本轮直接进入 validator private owner 与 focused tests 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-021-StageOutputValidator收敛.md。
3. 代码落点：
   - 新增 `cognition/src/validation/StageOutputValidator.h`、`cognition/src/validation/StageOutputValidator.cpp`，实现 `StageSchemaSpec`、`ValidationIssue` / `ValidationIssueSet` / `ValidationResult` 以及 `validate_stage_output()`、`validate_plan_graph_invariants()`、`validate_action_decision_invariants()`、`validate_response_envelope()`。
   - 更新 `cognition/CMakeLists.txt`，把 validator source 纳入 `dasall_cognition`。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，新增 `dasall_stage_output_validator_schema_unit_test`、`dasall_stage_output_validator_plan_graph_invariant_unit_test`、`dasall_stage_output_validator_response_envelope_unit_test`，并补 `cognition/src` include 与 `dasall_llm` 链接。
   - 新增 `tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`、`StageOutputValidatorPlanGraphInvariantTest.cpp`、`StageOutputValidatorResponseEnvelopeTest.cpp`，分别覆盖 schema fail-closed、plan graph depth cap / DAG invariants 与 response fallback envelope 一致性。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_stage_output_validator_schema_unit_test","dasall_stage_output_validator_plan_graph_invariant_unit_test","dasall_stage_output_validator_response_envelope_unit_test"])`
   - `RunCtest_CMakeTools(tests=["StageOutputValidatorSchemaTest","StageOutputValidatorPlanGraphInvariantTest","StageOutputValidatorResponseEnvelopeTest"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_schema_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_plan_graph_invariant_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_stage_output_validator_response_envelope_unit_test`
5. 验收结论：PASS；首轮构建暴露 plan / response 两个 validator tests 未链接 `dasall_llm`，同一 slice 补齐依赖后复跑通过。`RunCtest_CMakeTools` 继续命中仓库既有工具态 `生成失败`，回退显式二进制执行后三条 validator tests 全部零输出退出。
6. 后续边界：COG-TODO-022 可直接消费 validator diagnostics 和 fail-closed 结果做语义观测；COG-TODO-023 只消费 validator 结论编排 façade 主链，不回退为隐式放行或自修复。

### 13.19 COG-TODO-022：CognitionTelemetry 收敛（2026-04-27）

1. 任务选择：COG-TODO-024 已关闭 fixture blocker，且 COG-TODO-020、021 已分别冻结 bridge diagnostics 与 validator result surface，因此本轮直接进入 telemetry private owner 与 focused tests 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-022-CognitionTelemetry收敛.md。
3. 代码落点：
   - 新增 `cognition/src/observability/CognitionTelemetry.h`、`cognition/src/observability/CognitionTelemetry.cpp`，实现 `StageTelemetryContext`、`DecisionTelemetryRecord`、`DegradeTelemetryRecord`、`TelemetryEvent`、`TelemetryMetric`、`TelemetryEmitResult` 与五个 telemetry emit 方法。
   - 新增 `tests/mocks/include/MockCognitionTelemetrySink.h`，提供 recording/failure-injection sink，支撑 fail-open focused tests。
   - 更新 `cognition/CMakeLists.txt` 与 `tests/unit/cognition/CMakeLists.txt`，把 telemetry source、三条 telemetry tests 和必要 include 接线纳入构建。
   - 新增 `tests/unit/cognition/CognitionTelemetryFieldsTest.cpp`、`CognitionTelemetryRedactionTest.cpp`、`CognitionTelemetryFailureIsolationTest.cpp`，分别覆盖字段口径、redaction 与 sink failure isolation。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_cognition_telemetry_fields_unit_test","dasall_cognition_telemetry_redaction_unit_test","dasall_cognition_telemetry_failure_isolation_unit_test"])`
   - `RunCtest_CMakeTools(tests=["CognitionTelemetryFieldsTest","CognitionTelemetryRedactionTest","CognitionTelemetryFailureIsolationTest"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_fields_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_redaction_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_telemetry_failure_isolation_unit_test`
5. 验收结论：PASS；首轮构建暴露 `ErrorInfo` optional fields 与 failure type 假设不匹配，同一 slice 修正后复跑通过。`RunCtest_CMakeTools` 继续命中仓库既有工具态 `生成失败`，回退显式二进制执行后三条 telemetry tests 全部零输出退出。
6. 后续边界：COG-TODO-023 直接消费 bridge、validator、telemetry 三个 private owner 串联 façade 主链；022 不提前绑定具体 infra provider，也不提前接 runtime smoke。

### 13.20 COG-TODO-023：CognitionFacade 收敛（2026-04-27）

1. 任务选择：COG-TODO-020、021、022 已分别冻结 bridge supporting surface、validator fail-closed result surface 和 telemetry stage field owner，因此本轮直接进入 façade orchestration owner 的实现闭环。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-023-CognitionFacade收敛.md。
3. 代码落点：
   - 更新 `cognition/src/CognitionFacade.cpp`，将 façade 从 stub 改为可执行 orchestration owner：`decide()` 串联 `PerceptionEngine -> Planner -> StageOutputValidator(plan) -> Reasoner -> StageOutputValidator(action)`，`reflect()` 串联 `ReflectionEngine -> BeliefUpdateSynthesizer`，并统一消费 telemetry。
   - 更新 `tests/unit/cognition/CMakeLists.txt`，注册 `dasall_cognition_facade_flow_unit_test`、`dasall_cognition_facade_degraded_mode_unit_test` 并补 mock fixture 依赖。
   - 新增 `tests/unit/cognition/CognitionFacadeFlowTest.cpp` 与 `tests/unit/cognition/CognitionFacadeDegradedModeTest.cpp`，分别覆盖模块级三入口闭环与 perception routing gap 下的 clarification degrade path。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_cognition_facade_invalid_input_unit_test","dasall_cognition_facade_flow_unit_test","dasall_cognition_facade_degraded_mode_unit_test"])`
   - `RunCtest_CMakeTools(tests=["CognitionFacadeInvalidInputTest","CognitionFacadeFlowTest","CognitionFacadeDegradedModeTest"])`
   - `./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_invalid_input_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_flow_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_cognition_facade_degraded_mode_unit_test`
5. 验收结论：PASS；首轮构建暴露的都是 023 本地接口对齐问题（optional surface、`ErrorInfo.source_ref`、`ValidationResult.ok` 字段访问、telemetry `[[nodiscard]]` 处理），同一 slice 修正后编译通过。`RunCtest_CMakeTools` 继续命中仓库既有工具态 `生成失败`，回退显式二进制执行时又暴露 invalid-input missing field projection 回归；回补 `missing_evidence_hints` 后复跑，三条 façade tests 全部零输出退出。
6. 后续边界：023 只把 façade 收口为 orchestration owner，没有回卷 `IResponseBuilder` public surface，也没有越权进入 runtime 恢复或最终结果提交；后续直接进入 runtime smoke / interaction contract 任务。

### 13.21 COG-TODO-032：Runtime profile 到 CognitionConfig 注入收敛（2026-04-27）

1. 任务选择：COG-TODO-011、012、029 已分别冻结 projector、resolver 与 profile compatibility 基线；本轮最小缺口是 runtime true integration fixture 仍预装 `CognitionConfig{}`，以及 façade / response builder 没有真实消费 runtime policy snapshot。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-032-RuntimeProfile到CognitionConfig注入收敛.md。
3. 代码落点：
   - 更新 `cognition/include/CognitionDependencies.h`、`ICognitionEngine.h`、`IResponseBuilder.h`，新增 snapshot-aware factory overload，并把 runtime policy snapshot 作为可选 composition dependency 传入 cognition。
   - 更新 `cognition/src/config/CognitionConfigProjector.cpp` 与 `cognition/src/llm/CognitionLlmBridge.cpp`，使 `StageModelHint.preferred_provider` 真实携带 canonical route，并把 route 写入 `LLMRequest.model_route`。
   - 更新 `cognition/src/CognitionFacade.cpp`、`cognition/src/response/ResponseBuilder.cpp`，在存在 runtime policy snapshot 时真实消费 `StagePolicyResolver` 输出，route/deadline/output budget 与 `factory_test` 模板优先不再依赖硬编码默认值。
   - 更新 `runtime/src/AgentFacade.cpp`，仅在缺失 live cognition ports 时用 policy snapshot 组合 `cognition_engine` / `response_builder`，保持故障注入与契约测试中显式装配的自定义 ports 不被覆写。
   - 更新 `tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h`、`tests/integration/cognition/CognitionProfileCompatibilityTest.cpp`、`tests/integration/cognition/CognitionRuntimePolicyProjectionIntegrationTest.cpp` 以及相关 unit tests，保留对 broader response 语义的隔离，同时新增真实 provider-driven projection gate。
4. 验收命令：
   - `Build_CMakeTools()`
   - `RunCtest_CMakeTools(tests=["ProfileRuntimePolicySchemaContractTest","ProfileOverlayComposerTest","ProfilesBuildRuntimeIntegrationTest","RuntimeProfileCompatibilityTest","CapabilityServicesProfileIntegrationTest","CognitionRuntimePolicyProjectionIntegrationTest"])`
5. 验收结论：PASS；当前权威聚焦 gate 已切换为 `CognitionRuntimePolicyProjectionIntegrationTest`，并由 schema / overlay / runtime profile / capability integration focused suite 共同托底。该 gate 直接证明真实 profile asset 经 `RuntimePolicyProvider` 与 `AgentFacade::init()` 进入 planning/execution/reflection stage request，route / deadline / output budget 都来自真实 snapshot；更宽的 response bridge 语义继续留给 COG-TODO-040。
6. 后续边界：032 只收敛 runtime profile 到 cognition 的生产注入，不进入 memory writeback、context reload 或 reflection recovery 语义消费；这些由 COG-TODO-033 / 034 继续推进。

### 13.22 COG-TODO-033：Runtime 消费 Belief 与 Context 信号收敛（2026-04-27）

1. 任务选择：COG-TODO-018、026、027 已冻结 `BeliefUpdateHint` public surface 与 runtime unary integration baseline；本轮最小缺口是 `AgentOrchestrator` live unary path 只读 `action_decision`，没有消费 cognition 返回的 belief/context signals。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-033-Runtime消费Belief与Context信号收敛.md。
3. 代码落点：
   - 更新 `runtime/src/AgentOrchestrator.cpp`，新增 cognition belief writeback helper，把 `BeliefUpdateHint` 投影成最小 `MemoryWritebackRequest`，并以 best-effort 方式写回 memory。
   - 同一文件新增一次 replan-budgeted context refresh：在无显式 cognition error 且 `recommend_context_reload=true` 时，runtime 额外执行一次 `prepare_context()` 后重试 `decide()`。
   - 同一文件在 refresh 失败或重试后仍无 executable action 时，复用既有 waiting-clarify checkpoint/session binding/resume plan 语义，不再把这类 context-insufficient case 一律 hard fail。
   - 更新 `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp`，新增 counting memory manager 断言 writeback、bounded refresh 和 writeback failure best-effort。
   - 更新 `tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp` 与 `tests/unit/runtime/CMakeLists.txt`，把 smoke 证据从 response payload 切到 sqlite memory 落库，并补 `memory/src` include。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_runtime_cognition_loop_smoke_unit_test","dasall_cognition_runtime_interaction_contract_integration_test","dasall_cognition_failure_injection_integration_test"])`
   - `RunCtest_CMakeTools(tests=["RuntimeCognitionLoopSmokeTest","CognitionRuntimeInteractionContractTest","CognitionFailureInjectionTest"])`
5. 验收结论：PASS；首轮构建只暴露 smoke test 私有 include 路径和 contract test 错误枚举这两个本地接线问题，同一 slice 修正后通过。三条聚焦测试证明 runtime 会真实消费 belief/context signals，explicit cognition error 仍保持 fail-closed。
6. 后续边界：033 只收 decision path 的 belief/context 语义，不在本轮提前解释 `reflect()` 返回的 `ReflectionDecision`；反思分支和错误面统一由 COG-TODO-034 继续推进。

### 13.23 COG-TODO-034：Runtime 反思决策与错误面收敛（2026-04-27）

1. 任务选择：COG-TODO-017、026、027、028 已分别冻结 `ReflectionDecision`、runtime unary happy-path、interaction contract 与 failure injection baseline；本轮最小缺口是 `AgentOrchestrator` live unary tool round 会调用 `reflect()` 但直接丢弃结果，同时没有显式拒绝 executable action 与 cognition error 并存的冲突结果。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-034-Runtime反思决策与错误面收敛.md。
3. 代码落点：
   - 更新 `runtime/src/AgentOrchestrator.cpp`，新增 decision conflict fail-closed guard，并把 `CognitionReflectionResult.reflection_decision` 投影为合规 `RecoveryRequest` 后交由 `RecoveryManager::evaluate/execute/apply` 解释。
   - 同一文件新增 synthetic reflection failure observation / idempotency report helper，确保 `RecoveryRequest` 满足 observation/source-correlation 与 retry idempotency guard；同时补 `continue_from_checkpoint()` 的 `Planning` 分支，收敛 replan path。
   - 更新 `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp`，新增 Continue / RetryStep / Replan / AbortSafe 四类 reflection decision 的 runtime 行为验证。
   - 更新 `tests/integration/cognition/CognitionFailureInjectionIntegrationTest.cpp`，新增 executable+error conflict fail-closed 与 reflection error priority 场景。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_cognition_runtime_interaction_contract_integration_test","dasall_cognition_failure_injection_integration_test"])`
   - `RunCtest_CMakeTools(tests=["CognitionRuntimeInteractionContractTest","CognitionFailureInjectionIntegrationTest","ReflectionDecisionContractTest"])`
   - `get_errors(filePaths=[runtime/src/AgentOrchestrator.cpp, tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp, tests/integration/cognition/CognitionFailureInjectionIntegrationTest.cpp])`
5. 验收结论：PASS；首轮聚焦验证暴露的都是 034 本地接线问题，包括 safe-path 字段误用、Continue/RetryStep 的 retry idempotency evidence 缺口，以及 synthetic failure observation 未满足 Observation guards。逐一修补后，三条聚焦测试全部通过，且编辑器诊断无新增错误。
6. 后续边界：034 只收 runtime 对 reflection suggestion 与 cognition error surface 的解释，不提前进入 COG-TODO-035 的 structured schema 校验，也不处理 COG-TODO-036 的 placeholder alias 证据口径。

### 13.24 COG-TODO-035：StageOutputValidator 结构化 JSON/schema 校验收敛（2026-04-27）

1. 任务选择：COG-TODO-021 已冻结 validator public surface，028 已证明 schema failure integration baseline；本轮最小缺口是 `validate_stage_output()` 仍依赖 substring/字符扫描，无法可靠处理 whitespace、转义伪字段、nested array 和 malformed JSON。
2. 设计交付物：docs/todos/cognition/deliverables/COG-TODO-035-StageOutputValidator结构化校验收敛.md。
3. 代码落点：
   - 重写 `cognition/src/validation/StageOutputValidator.cpp` 的 private helper 区，在 cognition private namespace 内新增窄 JSON string/value/object tokenizer，并把 required / enum / numeric / list 提取全部切到结构化 token 遍历。
   - 更新 `cognition/src/validation/StageOutputValidator.h`，新增 `ValidationIssueCode::MalformedJson`，用于 malformed payload fail-closed。
   - 更新 `tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`，补空白/顺序变化、escaped pseudo-field、nested array、malformed JSON 与字段类型错误回归场景。
4. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_stage_output_validator_schema_unit_test","dasall_stage_output_validator_plan_graph_invariant_unit_test","dasall_stage_output_validator_response_envelope_unit_test"])`
   - `RunCtest_CMakeTools(tests=["StageOutputValidatorSchemaTest","StageOutputValidatorPlanGraphInvariantTest","StageOutputValidatorResponseEnvelopeTest"])`
   - `get_errors(filePaths=[cognition/src/validation/StageOutputValidator.cpp, cognition/src/validation/StageOutputValidator.h, tests/unit/cognition/StageOutputValidatorSchemaTest.cpp])`
5. 验收结论：PASS；首轮 schema test 只暴露 parser 对 array item 结束条件的本地误判，同一 slice 修正后复跑 `3/3` tests passed，编辑器诊断无新增错误。`RunCtest_CMakeTools` 仍打印已知 `DartConfiguration.tcl` stderr，但 result code=0 且 stdout 明确全部通过，按仓库基线计为有效证据。
6. 后续边界：035 只收 stage payload 的结构化 schema 校验，不提前处理 COG-TODO-036 的 placeholder alias 证据口径，也不把 private parser 扩成 shared JSON utility。

### 13.25 COG-FIX-004A-DOC-001：structured projection 方案 A 文档冻结（2026-05-15）

1. 任务选择：`COG-GAP-004` 的方案 A 已在总账附录 A 完成设计，但 cognition 详设与专项 TODO 仍缺少同等级锚点；本轮最小可执行任务是把 schema baseline、projection contract、fallback policy、diagnostics、Gate 和 Build 任务映射回 cognition 自身文档。
2. 设计交付物：更新认知详设 6.13.4 / 13.2，新增本专项 TODO 6.7 structured projection 任务簇，并同步总账附录 A 中 `COG-FIX-004A-DOC-001` 的完成状态。
3. 回写内容：
   - 在认知详设中冻结 `COG-FIX-004A-D01` ~ `D04` 的 owner、`cognition.plan.v1` / `cognition.reasoning.v1` schema baseline、profile fallback policy、diagnostics / telemetry 字段和安全边界。
   - 在本专项 TODO 中新增 `COG-FIX-004A-BLD-001` ~ `012`、`Gate-COG-FIX004A-01` ~ `05`、执行顺序与当前 Gate 状态，避免方案 A 只存在于总账附录。
   - 保持 `COG-TODO-020` / `021` / `023` / `031` / `035` 的历史完成状态不被回写否定，仅把 structured projection authoritative consumption 作为后续 build 任务簇继续推进。
4. 验收命令：
   - `rg -n "COG-FIX-004A" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md`
   - `rg -n "cognition.plan.v1" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md`
   - `rg -n "cognition.reasoning.v1" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md`
5. 验收结论：PASS；三条 `rg` 命令均可在认知详设、本专项 TODO 与总账附录 A 中命中相同 task / schema / gate 锚点；`Gate-COG-FIX004A-01` 现可判定为通过。
6. 后续边界：DOC-001 只冻结方案 A 设计与任务映射，不宣称 projector、Facade authoritative consumption、runtime interaction 或 negative matrix 已完成；这些继续由 `COG-FIX-004A-BLD-001` ~ `012` 和 `Gate-COG-FIX004A-02` ~ `05` 收口。

### 13.26 COG-FIX-004A-BLD-001：StageSchemaRegistry 与 structured schema specs 收敛（2026-05-15）

1. 任务选择：方案 A 的第一条 Build 任务需要先把 `planning/plan` 与 `execution/action_decision` schema owner 从 validator / tests 的分散定义收敛为单点 registry，避免后续 projector / Facade 继续消费手写 spec。
2. 代码交付物：新增 `cognition/src/validation/StageSchemaRegistry.h`、`cognition/src/validation/StageSchemaRegistry.cpp`，冻结 `cognition.plan.v1` / `cognition.reasoning.v1` 的 `StageSchemaSpec`；同时让 `StageOutputValidator` 改为 include registry 定义，并新增 `tests/unit/cognition/StageSchemaRegistryTest.cpp` 与相应 CMake 接线。
3. 测试对齐：`tests/unit/cognition/StageOutputValidatorSchemaTest.cpp` 不再本地 `make_schema_spec()`，而是直接消费 `schema_for_execution_action_decision()`；首轮聚焦测试暴露最小正例 payload 缺少 registry 新冻结的 required fields，同一原子任务内补齐 payload 后复跑恢复全绿。
4. 验收命令：
   - `cmake --build build-ci --target dasall_stage_schema_registry_unit_test dasall_stage_output_validator_schema_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest"`
5. 验收结论：PASS；`StageSchemaRegistryTest` 与 `StageOutputValidatorSchemaTest` 均通过，schema registry owner、schema validator baseline 与 focused test discoverability 已闭合。
6. 后续边界：BLD-001 只解决 schema owner 收敛与 structured schema spec baseline，不提前宣称 token view、typed projector、Facade authoritative consumption 或 runtime structured interaction 已完成；这些继续由 `COG-FIX-004A-BLD-002` ~ `012` 收口。

### 13.27 COG-FIX-004A-BLD-002：structured payload token view 收敛（2026-05-15）

1. 任务选择：BLD-003 / BLD-004 需要一个可复用的 payload token view 才能避免 projector 再次手写字符串扫描；因此本轮只把 validator 内部 parser 抽为 cognition validation 私有 view，并补齐最小 unknown-field 策略。
2. 代码交付物：新增 `cognition/src/validation/StructuredPayloadView.h`，提供 `parse_structured_payload()`、field token accessor、type-safe string / number / bool / list / object readers；`StageOutputValidator` 改为直接消费 token view；`StageSchemaSpec` 增补 `known_top_level_fields`，将 unknown field fail-closed 收口到 registry baseline。
3. 测试对齐：`StageOutputValidatorSchemaTest` 新增 registered `x_` extension 正例与 unknown top-level field 负例；`StageSchemaRegistryTest` 新增 `known_top_level_fields` 冻结断言，确保 unknown-field 策略不再散落在 validator 私有实现。
4. 验收命令：
   - `cmake --build build-ci --target dasall_stage_schema_registry_unit_test dasall_stage_output_validator_schema_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "StageSchemaRegistryTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest"`
5. 验收结论：PASS；两个 target 构建通过，`StageSchemaRegistryTest` 与 `StageOutputValidatorSchemaTest` 均通过，token view 抽取与 top-level unknown field 策略已具 focused evidence。
6. 后续边界：BLD-002 只提供 validator / projector 可复用的 token view 和 top-level unknown-field baseline，不提前落地 typed projection、Facade authoritative consumption 或 deeper nested drift matrix；这些继续由 `COG-FIX-004A-BLD-003` ~ `011` 收口。

### 13.28 COG-FIX-004A-BLD-003：PlanGraphStructuredProjector 收敛（2026-05-15）

1. 任务选择：在 BLD-002 已抽出 token view 之后，最小下一跳是把 planning payload 的 typed projection owner 从未来的 Facade / tests 中分离出来，单点收敛到 `PlanGraphStructuredProjector`，为 BLD-005 的 authoritative consumption 做准备。
2. 代码交付物：新增 `cognition/src/projection/PlanGraphStructuredProjector.h` 与 `.cpp`，实现 `project_plan_graph()`，把 structured planning payload 投影为 `plan::PlanGraph`；projector 只负责 typed construction 与字段类型边界，不承担 Runtime/Facade 编排职责。
3. 边界收紧：`PlanGraphStructuredProjector` 对 `plan_id`、`revision`、`estimated_complexity`、`nodes`、`edges`、`open_questions` 做 typed projection；对 `success_signal` 缺失/空串等 projection 级问题 fail-closed；duplicate node、unknown edge、cycle、over cap 继续由既有 `StageOutputValidator::validate_plan_graph_invariants()` 收口，不在 projector 内静默修复。
4. 测试对齐：新增 `tests/unit/cognition/PlanGraphStructuredProjectionTest.cpp`，覆盖 valid payload 正例，以及 missing success signal、duplicate node、unknown edge、cycle、over cap 等负例；并通过 `tests/unit/cognition/CMakeLists.txt` 注册 `dasall_plan_graph_structured_projection_unit_test`。
5. 验收命令：
   - `cmake --build build-ci --target dasall_plan_graph_structured_projection_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "PlanGraphStructuredProjectionTest"`
6. 验收结论：PASS；新 target 构建通过，`PlanGraphStructuredProjectionTest` 通过，planning payload -> `PlanGraph` 的 typed projection owner 已具 focused evidence。
7. 后续边界：BLD-003 只收 planning payload projection owner 与其 focused invariant gate，不提前修改 Facade planning path、Runtime interaction 或 `ActionDecision` projection；这些继续由 `COG-FIX-004A-BLD-004` ~ `009` 收口。

### 13.29 COG-FIX-004A-BLD-004：ActionDecisionStructuredProjector 收敛（2026-05-15）

1. 任务选择：在 BLD-003 已把 planning payload projection owner 独立出来后，最小对称下一跳是把 execution payload 的 typed projection owner 从未来的 Facade / tests 中分离出来，单点收敛到 `ActionDecisionStructuredProjector`，为 BLD-006 的 authoritative consumption 做准备。
2. 代码交付物：新增 `cognition/src/projection/ActionDecisionStructuredProjector.h` 与 `.cpp`，实现 `project_action_decision()`，把 structured execution payload 投影为 `decision::ActionDecision`；projector 负责 `decision_kind`、`confidence`、`clarification_needed`、`tool_intent_hint`、`response_outline`、`candidate_scores` 的 typed construction，并对 enum / nested object / candidate score 类型错配 fail-closed。
3. 边界收紧：`ActionDecisionStructuredProjector` 只收 execution payload 的 typed projection 与 nested object shape，不承担 Facade / Runtime 编排；`selected_node_id` 缺失、response 上携带 tool intent、clarification 冲突等 decision-level 互斥关系继续由既有 `StageOutputValidator::validate_action_decision_invariants()` 收口，本轮同步补齐 response/tool 与 clarification conflict 的 invariant gate。
4. 测试对齐：新增 `tests/unit/cognition/ActionDecisionStructuredProjectionTest.cpp`，覆盖 valid payload 正例，以及 invalid enum、missing selected node、tool intent on response、clarification conflict 负例；并通过 `tests/unit/cognition/CMakeLists.txt` 注册 `dasall_action_decision_structured_projection_unit_test`。
5. 验收命令：
   - `cmake --build build-ci --target dasall_action_decision_structured_projection_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "ActionDecisionStructuredProjectionTest"`
6. 验收结论：PASS；新 target 构建通过，`ActionDecisionStructuredProjectionTest` 通过，execution payload -> `ActionDecision` 的 typed projection owner 已具 focused evidence；`Gate-COG-FIX004A-02` 随之转为 Pass。
7. 后续边界：BLD-004 只收 execution payload projection owner 与 focused invariant gate，不提前切换 Facade execution 主链、Runtime interaction 或 telemetry 字段；这些继续由 `COG-FIX-004A-BLD-005` ~ `010` 收口。

### 13.30 COG-FIX-004A-BLD-005：Facade planning authoritative consumption 收敛（2026-05-15）

1. 任务选择：在 BLD-003 已把 `PlanGraphStructuredProjector` 落成 typed projection owner 后，最小下一跳是把 `CognitionFacade::run_decision_pipeline()` planning segment 切到“bridge payload 先过 schema / projection / invariant，再决定是否进入 local planner fallback”的 authoritative consumption 主链。
2. 代码交付物：更新 `cognition/src/CognitionFacade.cpp`，使 planning bridge payload 成功时先经 `schema_for_planning_plan()`、`PlanGraphStructuredProjector` 与 `validate_plan_graph_invariants()`，再把 projected `PlanGraph` 作为 active plan 输入 reasoning；只有 bridge/schema/projection/invariant 失败且 degraded path 明确允许时，才回到 local planner。
3. 前置修正：更新 `cognition/src/validation/StageOutputValidator.cpp` 与 `tests/unit/cognition/StageOutputValidatorSchemaTest.cpp`，补齐 `nodes.node_id`、`nodes.action_kind_hint` 等 array-in-path schema gate，使 planning payload 能在进入 projector 前正确通过 registry baseline，而不是被 dotted-path 误判为缺字段。
4. 测试对齐：新增 `tests/unit/cognition/CognitionFacadeStructuredPlanOutputTest.cpp`，覆盖 projected plan 成为 local reasoner 输入的正例，以及 invalid planning payload 仅在显式 degraded path 下回到 local planner 的负例；并通过 `tests/unit/cognition/CMakeLists.txt` 注册 `dasall_cognition_facade_structured_plan_output_unit_test`。
5. 验收命令：
   - `cmake --build build-ci --target dasall_stage_output_validator_schema_unit_test dasall_cognition_facade_structured_plan_output_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "StageOutputValidatorSchemaTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredPlanOutputTest"`
6. 验收结论：PASS；planning array-path schema gate 已具 focused evidence，`CognitionFacadeStructuredPlanOutputTest` 通过，schema-valid planning bridge payload 已成为 active `PlanGraph`，本地 Planner 只在显式 fallback 条件下使用。
7. 后续边界：BLD-005 只收 planning authoritative consumption 与 array-path schema gate，不提前切 execution authoritative consumption、runtime interaction 或 telemetry fields；这些继续由 `COG-FIX-004A-BLD-006` ~ `010` 收口。

### 13.31 COG-FIX-004A-BLD-006：Facade execution authoritative consumption 收敛（2026-05-15）

1. 任务选择：在 BLD-005 已让 projected `PlanGraph` 成为 planning 主链对象来源后，最小对称下一跳是把 execution segment 也切到“bridge payload 先过 schema / projection / invariant，再决定是否回到 local reasoner”的 authoritative consumption 主链。
2. 代码交付物：更新 `cognition/src/CognitionFacade.cpp`，使 execution bridge payload 成功时先经 `schema_for_execution_action_decision()`、`ActionDecisionStructuredProjector` 与 `validate_action_decision_invariants()`，再把 projected `ActionDecision` 直接作为 `CognitionDecisionResult.action_decision`；invalid execution payload 在 degraded path 禁用时 fail-closed，在允许时才回到 local reasoner / bounded fallback。
3. 测试对齐：新增 `tests/unit/cognition/CognitionFacadeStructuredActionOutputTest.cpp`，覆盖 projected `ActionDecision` 成为 Facade authoritative result 的正例，以及 invalid execution payload 在无 degraded path 时 fail-closed 的负例；同时复跑 `CognitionFacadeFlowTest` 与 `CognitionFacadeDegradedModeTest`，确认旧的 bridge invocation / degraded semantics 未回退。
4. 验收命令：
   - `cmake --build build-ci --target dasall_cognition_facade_structured_action_output_unit_test dasall_cognition_facade_flow_unit_test dasall_cognition_facade_degraded_mode_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeStructuredActionOutputTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeFlowTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "CognitionFacadeDegradedModeTest"`
5. 验收结论：PASS；`CognitionFacadeStructuredActionOutputTest`、`CognitionFacadeFlowTest` 与 `CognitionFacadeDegradedModeTest` 均通过，execution bridge payload 已成为 `ActionDecision` authoritative source；`Gate-COG-FIX004A-03` 随之转为 Pass。
6. 后续边界：BLD-006 只收 execution authoritative consumption，不提前接入 Runtime interaction contract、structured integration regression 或 telemetry structured fields；这些继续由 `COG-FIX-004A-BLD-008` ~ `010` 收口。

### 13.32 COG-FIX-004A-BLD-007：structured payload test fixture 收敛（2026-05-15）

1. 任务选择：在 BLD-006 已让 structured planning / execution payload 成为 Facade 主链对象来源后，最小下一跳是把 tests support seam 收敛到统一的 structured payload scenario API，避免下游测试继续手写 JSON、重复 stage route 映射或依赖 provider-private payload 形态。
2. 代码交付物：更新 `tests/mocks/include/MockLLMManager.h`，新增 `make_structured_stage_result()` 与 `set_structured_stage_payload()`，使 staged success result 可以按 stage 自动生成 route；更新 `tests/mocks/include/MockCognitionFixture.h`，新增 planning / execution structured payload builders，以及 valid、malformed、schema-invalid、projection-invalid scenario 与对应 stage helper。
3. 测试对齐：更新 `tests/unit/cognition/MockCognitionFixtureSurfaceTest.cpp`，显式覆盖 valid / malformed / schema-invalid / projection-invalid payload 的 parseability 与 staging surface；同时更新 `tests/unit/cognition/CognitionFacadeStructuredPlanOutputTest.cpp` 与 `tests/unit/cognition/CognitionFacadeStructuredActionOutputTest.cpp`，改为直接复用 fixture helper，不再手写 structured stage payload。
4. 验收命令：
   - `cmake --build build-ci --target dasall_mock_cognition_fixture_surface_unit_test dasall_plan_graph_structured_projection_unit_test dasall_action_decision_structured_projection_unit_test dasall_cognition_facade_structured_plan_output_unit_test dasall_cognition_facade_structured_action_output_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R "MockCognitionFixtureSurfaceTest|PlanGraphStructuredProjectionTest|ActionDecisionStructuredProjectionTest|CognitionFacadeStructuredPlanOutputTest|CognitionFacadeStructuredActionOutputTest"`
5. 验收结论：PASS；`MockCognitionFixtureSurfaceTest`、`PlanGraphStructuredProjectionTest`、`ActionDecisionStructuredProjectionTest`、`CognitionFacadeStructuredPlanOutputTest` 与 `CognitionFacadeStructuredActionOutputTest` 全部通过，structured payload tests support seam 已可稳定注入 valid / invalid payload，而不再维护第二套 stage mapping。
6. 后续边界：BLD-007 只收 tests support seam，不提前接入 structured-output integration regression、Runtime interaction contract 或 telemetry structured fields；这些继续由 `COG-FIX-004A-BLD-008` ~ `010` 收口。

### 13.33 COG-FIX-004A-BLD-008：structured-output integration regression 收敛（2026-05-15）

1. 任务选择：在 BLD-006 已让 structured planning / execution payload 成为 `CognitionFacade::run_decision_pipeline()` 的 authoritative source、BLD-007 已把 tests support seam 收敛成统一 scenario helper 后，最小下一跳是补一条 integration 级 `decide()` 回归，证明 bridge valid JSON 能贯穿 snapshot-backed cognition engine 主链，同时 malformed / invalid payload 的 fallback / fail-fast 语义在 integration 层仍然可复验。
2. 代码交付物：新增 `tests/integration/cognition/CognitionStructuredOutputIntegrationTest.cpp`，使用 `create_cognition_engine(snapshot, dependencies)`、`MockCognitionFixture` 与 `make_true_integration_policy_snapshot()` 组装 snapshot-backed integration path；覆盖 valid structured planning + execution direct-response 正例、planning malformed 显式 fallback 负例、execution invariant invalid 且降级关闭时 fail-fast 负例。更新 `tests/integration/cognition/CMakeLists.txt`，注册 `dasall_cognition_structured_output_integration_test`。
3. 测试对齐：新 integration regression 直接复用 BLD-007 的 structured payload scenario helper，不再在 integration 层手写 planning / execution JSON；同时断言 `structured_projection.projected_plan_graph`、`structured_projection.projected_action_decision`、`structured_projection.local_fallback:planning`、`structured_projection.invariant_failed:execution` 等 diagnostics，确保 integration 证据与设计冻结的 failure taxonomy 一致。
4. 验收命令：
   - `cmake --build build-ci --target dasall_cognition_structured_output_integration_test`
   - `ctest --test-dir build-ci --output-on-failure -R "CognitionStructuredOutputIntegrationTest"`
   - `ctest --test-dir build-ci -N | rg "CognitionStructuredOutputIntegrationTest"`
5. 验收结论：PASS；`CognitionStructuredOutputIntegrationTest` 通过，且 `ctest -N` 已发现该目标，证明 valid bridge payload 可驱动 `PlanGraph` / `ActionDecision` integration 主链，malformed planning payload 会显式记录 fallback，invalid execution payload 在禁用降级时会 fail-fast。
6. 后续边界：BLD-008 只收 cognition integration regression，不提前证明 Runtime first hop / response builder 消费 projected `ActionDecision`，也不提前接入 telemetry structured fields 或 schema drift matrix；这些继续由 `COG-FIX-004A-BLD-009` ~ `011` 收口。
