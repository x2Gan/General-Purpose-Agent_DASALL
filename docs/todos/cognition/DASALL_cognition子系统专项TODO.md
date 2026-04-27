# DASALL cognition 子系统专项 TODO

最近更新时间：2026-04-27
阶段：Detailed Design -> Special TODO
适用范围：cognition/
当前结论：认知详设已经具备 L3/L2 混合粒度拆分条件；COG-TODO-001 已把 `ICognitionEngine` 公共口径收敛为 `decide()` / `reflect()` / `IResponseBuilder::build()` 三入口，COG-BLK-001 已解阻；COG-TODO-002 已把 cognition↔llm stage taxonomy 收敛为 `planning/execution/reflection/response` canonical key 与 StageModelHint 映射表，COG-BLK-002 已解阻；COG-TODO-003 已把 runtime↔cognition caller fixture 与 ActionDecision→FSM 第一跳口径收敛到 Runtime 真实 FSM 状态 / guard table，COG-BLK-003 已解阻；COG-TODO-004 已冻结 `MockLLMManager`、`MockCognitionFixture`、failure/profile smoke fixture 的测试支撑口径，前置补设计 / 评审门禁已完成；COG-TODO-024 已新增真实 `MockLLMManager` / `MockCognitionFixture` header 与 narrow surface test，COG-BLK-004 已关闭；cognition integration discoverability 继续由 COG-TODO-025 收口。

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
| `CognitionLlmBridge` | 6.13.4、6.14.2、6.14.3 | L2 | 调用映射、失败投影、budget hint、test outlet 明确；canonical stage key 已由 COG-TODO-002 统一；MockLLMManager 设计口径已由 COG-TODO-004 冻结 | cognition-specific mock header 尚未落盘 | 先做 024，再推进 bridge |
| `StageOutputValidator` | 6.13.4 | L2 | required fields、enum、numeric bounds、plan graph / response invariants 明确 | `StageSchemaSpec` supporting fields 未完全成表 | 以组件级任务推进，不拆独立 schema admission |
| `CognitionTelemetry` | 6.11、6.13.4 | L2 | 日志/指标/trace/audit 字段、redaction 和 fail-open 规则明确；MockCognitionFixture 设计口径已由 COG-TODO-004 冻结 | cognition-specific fixture header 尚未落盘 | 先补 024，再推进组件落盘 |
| `CognitionFacade` | 6.13.1、6.7、6.8 | L2 | 三入口流程、错误收口、降级语义、测试出口明确 | 接口口径、stage taxonomy、runtime fixture 三条接缝需先统一 | 作为中后期收口任务 |
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

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 接口定义类任务 | 是 | COG-TODO-007 ~ 010 |
| 数据结构定义类任务 | 是 | COG-TODO-007 ~ 009 |
| 生命周期与初始化类任务 | 是 | COG-TODO-005、011、012、023 |
| 适配器 / 桥接类任务 | 是 | COG-TODO-020、022、024 |
| 异常与错误处理类任务 | 是 | COG-TODO-013、017、021、028 |
| 配置与 Profile 裁剪类任务 | 是 | COG-TODO-001、002、011、012、029 |
| 测试与门禁类任务 | 是 | COG-TODO-006、024 ~ 030 |
| 文档 / 交付证据回写类任务 | 是 | COG-TODO-001 ~ 004、030 |

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
| COG-TODO-020 | NotStarted | 实现 CognitionLlmBridge | 认知详设 6.13.4、6.14.2、6.14.3；7.1 COG-D08、D11 | 6.13.4 `CognitionLlmBridge` 卡片 | L2 | 新增 `cognition/src/llm/CognitionLlmBridge.cpp` | `invoke_stage()`、`build_llm_request()`、`derive_budget_hint()`、`normalize_llm_response()`、`project_llm_failure()` | unit：`CognitionLlmBridgeProjectionTest`、`CognitionLlmBridgeErrorMappingTest`、`StageModelHintProjectionTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "CognitionLlmBridge(Projection|ErrorMapping|BudgetHint)|StageModelHintProjectionTest" --output-on-failure` | COG-TODO-002、004、007、009、010、024 | COG-BLK-002、COG-BLK-004 | 完成 COG-TODO-002、024 | docs/todos/cognition/deliverables/COG-TODO-020-CognitionLlmBridge收敛.md；cognition/src/llm/CognitionLlmBridge.cpp | 仅当 stage hint 投影、错误映射和 provider-private 字段剥离全部通过验证，且 bridge 不自建 retry/breaker 时完成 |
| COG-TODO-021 | NotStarted | 实现 StageOutputValidator | 认知详设 6.13.4；7.1 COG-D08 | 6.13.4 `StageOutputValidator` 卡片 | L2 | 新增 `cognition/src/validation/StageOutputValidator.cpp` | `validate_stage_output()`、`validate_plan_graph_invariants()`、`validate_action_decision_invariants()`、`validate_response_envelope()` | unit：`StageOutputValidatorSchemaTest`、`StageOutputValidatorPlanGraphInvariantTest`、`StageOutputValidatorResponseEnvelopeTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "StageOutputValidator(Schema|PlanGraphInvariant|ResponseEnvelope)Test" --output-on-failure` | COG-TODO-002、008、009、010、020 | COG-BLK-002 | 完成 COG-TODO-002 | docs/todos/cognition/deliverables/COG-TODO-021-StageOutputValidator收敛.md；cognition/src/validation/StageOutputValidator.cpp | 仅当 required fields、graph invariants、decision / response envelope 约束全部 fail-closed 生效时完成；首版 schema 使用 header comment 标记格式版本基线（参见 §12 COG-OQ09）|
| COG-TODO-022 | NotStarted | 实现 CognitionTelemetry | 认知详设 6.11、6.13.4；7.1 COG-D09 | 6.13.4 `CognitionTelemetry` 卡片 | L2 | 新增 `cognition/src/observability/CognitionTelemetry.cpp` | `emit_stage_started()`、`emit_stage_completed()`、`emit_stage_failed()`、`emit_clarification_requested()`、`emit_response_degraded()` | unit：`CognitionTelemetryFieldsTest`、`CognitionTelemetryRedactionTest`、`CognitionTelemetryFailureIsolationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "CognitionTelemetry(Fields|Redaction|FailureIsolation)Test" --output-on-failure` | COG-TODO-004、007、009、010 | COG-BLK-004 | 完成 COG-TODO-024 | docs/todos/cognition/deliverables/COG-TODO-022-CognitionTelemetry收敛.md；cognition/src/observability/CognitionTelemetry.cpp | 仅当语义级字段完整、provider-private 内容被裁剪且 sink 故障 fail-open 时完成；推理链 trace（CoT）可按需检索 |
| COG-TODO-023 | NotStarted | 实现 CognitionFacade | 认知详设 6.13.1、6.7、6.8；7.1 COG-D08 | 6.13.1 `CognitionFacade` 卡片 | L2 | 新增 `cognition/src/CognitionFacade.cpp` | `decide()`、`reflect()`、`build_response()`、`run_decision_pipeline()`、`run_reflection_pipeline()`、`run_response_pipeline()` | unit：`CognitionFacadeFlowTest`、`CognitionFacadeDegradedModeTest`、`CognitionFacadeInvalidInputTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "CognitionFacade(Flow|DegradedMode|InvalidInput)Test" --output-on-failure` | COG-TODO-001、002、007 ~ 022 | COG-BLK-001、COG-BLK-002、COG-BLK-004 | 完成 COG-TODO-001、002、024 | docs/todos/cognition/deliverables/COG-TODO-023-CognitionFacade收敛.md；cognition/src/CognitionFacade.cpp | 仅当三入口闭环、错误出口与受控降级路径全部成立，且不越权触发外部执行与恢复时完成 |

### 6.4 测试支撑、集成与 Gate 收口任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| COG-TODO-024 | Done | 新增 MockLLMManager 与 MockCognitionFixture | 认知详设 8.1、11.2；当前 tests/mocks 现状 | 8.1 `tests/mocks/include/MockLLMManager.h`、`MockCognitionFixture.h` | L2 | 新增 `tests/mocks/include/MockLLMManager.h`、`tests/mocks/include/MockCognitionFixture.h`，并更新 `tests/unit/cognition/CMakeLists.txt`、新增 `tests/unit/cognition/MockCognitionFixtureSurfaceTest.cpp` | `MockLLMManager` scripted stage result / failure helper、`MockCognitionFixture` runtime caller request builders、shared cognition owner helper | unit：`MockCognitionFixtureSurfaceTest` | `Build_CMakeTools(buildTargets=["dasall_mock_cognition_fixture_surface_unit_test"])`；`RunCtest_CMakeTools(tests=["MockCognitionFixtureSurfaceTest"])`；`./build/vscode-linux-ninja/tests/unit/cognition/dasall_mock_cognition_fixture_surface_unit_test` | COG-TODO-001 ~ 004、006、010 | 已解阻：COG-BLK-004 | 已完成 | docs/todos/cognition/deliverables/COG-TODO-024-cognition测试fixture实现收敛.md；`tests/mocks/include/MockLLMManager.h`；`tests/mocks/include/MockCognitionFixture.h`；`tests/unit/cognition/CMakeLists.txt`；`tests/unit/cognition/MockCognitionFixtureSurfaceTest.cpp` | 已完成：cognition-specific mock seam 真正落盘，bridge / telemetry / facade / integration 后续不再依赖当前粗粒度 `MockLLMAdapter`；`RunCtest_CMakeTools` 仍存在已知工具态 `生成失败`，已按仓库基线回退显式二进制执行 |
| COG-TODO-025 | NotStarted | 注册 tests/integration/cognition 拓扑 | 认知详设 7.1 COG-D10、8.1、9.1；SSOT `InfraIntegrationTopology` | 8.1 `tests/integration/cognition/`；9.1 integration matrix | L2 | 更新 `tests/integration/CMakeLists.txt`，新增 `tests/integration/cognition/CMakeLists.txt` | cognition integration discoverability | integration：`ctest -N` 可发现 cognition integration 用例 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -N | rg "Cognition(Runtime|FailureInjection|ProfileCompatibility)IntegrationTest|CognitionRuntimeInteractionContractTest"` | COG-TODO-005、024 | 无 | 无 | docs/todos/cognition/deliverables/COG-TODO-025-tests_integration_cognition拓扑收敛.md；tests/integration/cognition/CMakeLists.txt | 仅当 cognition integration 用例被顶层聚合并可被 `ctest -N` 发现时完成 |
| COG-TODO-026 | NotStarted | 验证 CognitionRuntimeIntegration 主成功链 | 认知详设 7.1 COG-D09 / D10、8.2、9.1；当前 runtime smoke 现状 | 8.2 COG-M4 / M5；`RuntimeCognitionLoopSmokeTest`、`CognitionRuntimeIntegrationTest` | L2 | 新增 `tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp`、`tests/integration/cognition/CognitionRuntimeIntegrationTest.cpp`，并让 runtime smoke 不再绕过 cognition | runtime↔cognition happy path | unit + integration：从 Runtime handoff 到 cognition decide / response 主链可执行 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeCognitionLoopSmokeTest|CognitionRuntimeIntegrationTest" --output-on-failure` | COG-TODO-003、023、024、025 | COG-BLK-003、COG-BLK-004 | 完成 COG-TODO-003、024 | docs/todos/cognition/deliverables/COG-TODO-026-CognitionRuntimeIntegration主链收敛.md；对应 smoke / integration 测试文件 | 仅当 runtime 不再绕过 cognition，且主成功链可稳定产出 ActionDecision / AgentResult 时完成 |
| COG-TODO-027 | NotStarted | 验证 CognitionRuntimeInteractionContract | 认知详设 6.14、7.1 COG-D12、9.2；contracts 基线 | 6.14.1 ~ 6.14.5；`CognitionRuntimeInteractionContractTest` | L2 | 新增 `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` | ActionDecision→FSM、错误回流、BeliefUpdateHint 写回时序、ContextSufficiencySignal | integration + contract：交互契约与既有 shared contract tests 同时通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests dasall_contract_tests && ctest --test-dir build-ci -R "CognitionRuntimeInteractionContractTest|ContextPacketFieldContractTest|ReflectionDecisionContractTest|AgentResultContractTest|MainFlowContractE2ETest" --output-on-failure` | COG-TODO-003、018、023、024、025 | COG-BLK-003、COG-BLK-004 | 完成 COG-TODO-003、024 | docs/todos/cognition/deliverables/COG-TODO-027-CognitionRuntimeInteractionContract收敛.md；对应 integration 测试文件 | 仅当 runtime↔cognition 交互契约可自动验证，且不引发 shared contracts 回归时完成 |
| COG-TODO-028 | NotStarted | 验证 CognitionFailureInjectionIntegration | 认知详设 9.1、9.3；7.1 COG-D10 | `CognitionFailureInjectionIntegrationTest` | L2 | 新增 `tests/integration/cognition/CognitionFailureInjectionIntegrationTest.cpp` | llm unavailable、schema violation、missing belief state、contradictory observation、response fallback | integration：故障路径显式返回 ErrorInfo / 降级结果，且无静默吞错 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "CognitionFailureInjectionIntegrationTest" --output-on-failure` | COG-TODO-020、021、023、024、025 | COG-BLK-004 | 完成 COG-TODO-024 | docs/todos/cognition/deliverables/COG-TODO-028-CognitionFailureInjectionIntegration收敛.md；对应 integration 测试文件 | 仅当五类主要失败分支都能产出明确结果与观测证据，而不是由 cognition 内部重试掩盖时完成 |
| COG-TODO-029 | NotStarted | 验证 CognitionProfileCompatibility | 认知详设 6.10.2、9.1、9.4；llm deliverable 035；profiles 详设 | `CognitionProfileCompatibilityTest`；五档 profile 策略 | L2 | 新增 `tests/integration/cognition/CognitionProfileCompatibilityTest.cpp` | desktop_full / cloud_full / edge_balanced / edge_minimal / factory_test 下的阶段启停、plan cap、fallback 与 route 策略 | integration：五档 profile 都有明确通过或拒绝结论 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "CognitionProfileCompatibilityTest" --output-on-failure` | COG-TODO-002、011、012、020、023、024、025 | COG-BLK-002、COG-BLK-004 | 完成 COG-TODO-002、024 | docs/todos/cognition/deliverables/COG-TODO-029-CognitionProfileCompatibility收敛.md；对应 integration 测试文件 | 仅当五档 profile 的认知策略差异均通过统一投影视图驱动，而不是测试私有映射或代码分叉时完成 |
| COG-TODO-030 | NotStarted | 回写 cognition 专项 Gate 与交付证据 | 认知详设 9.4、11.2、12.2；文档治理基线 | Gate-COG-*；COG-BLK-* | L2 | 更新 `docs/todos/cognition/DASALL_cognition子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` | Gate 结论、阻塞变化、风险残留、命令证据 | process：全部 gate 命令、通过/残余结论、后续动作回写齐备 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_cognition dasall_unit_tests dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -R "Cognition|RuntimeCognitionLoopSmoke|GoalContractFieldContractTest|BeliefStateContractTest|ContextPacketFieldContractTest|ObservationContractTest|ReflectionDecisionContractTest|AgentResultContractTest|MainFlowContractE2ETest"` | COG-TODO-026 ~ 029 | 无 | 无 | 更新后的专项 TODO、对应 deliverables、docs/worklog/DASALL_开发执行记录.md | 仅当每个 Gate 都有命令证据、状态变化、风险说明与后续动作回写时完成 |

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

## 10. 风险与回退策略

### 10.1 风险表

说明：COG-R01 ~ R08 与认知详设 §11.1 保持 ID 一致，确保跨文档可追溯。COG-R09 ~ R12 为本专项 TODO 新增的工程执行风险。

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

### 10.2 回退策略

| 场景 | 回退策略 |
|---|---|
| 公开接口评审迟迟未收敛 | 保持 request / result / interfaces 为 module-local / module-public，暂停 runtime 正式接线 |
| llm stage taxonomy 仍未统一 | 只推进不依赖真实 route key 的本地阶段实现，不宣称 bridge / profile gate ready |
| runtime integration 被阻塞 | 先完成阶段单测、facade fake 流程与 topology 接线，不提前宣称主链闭环 |
| response llm 路径不稳定 | ResponseBuilder 保持单次响应 + 模板降级，不引入 streaming |
| shared admission 被要求提前推进 | 明确退回 contracts 评审流程，本专项继续仅维护 cognition module public surface |
| edge_minimal Planner/Reasoner 降级路径缺失 | Planner/Reasoner 可降级为 rule-based 固定路径（skip plan / hardcoded decide），通过 012 / 029 验证降级行为而非关闭 cognition |

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

## 12. 未决问题处置表

说明：以下未决问题来源于认知详设 §12，需在对应任务执行中明确处置。

| OQ ID | 来源 | 问题摘要 | 处置方式 | 关联 TODO | 说明 |
|---|---|---|---|---|---|
| COG-OQ01 | 详设 §12 | ContextPacket 中 BeliefState 应否必填 | 采纳：首版 BeliefState 设为必填，default 为空 snapshot | COG-TODO-007 | 避免下游组件对可空字段做防御式编程 |
| COG-OQ03 | 详设 §12 | IPlanner 是否需要支持 delegate hint | 延后：首版关闭 delegate hint，IPlanner 只接受 Goal→PlanGraph | COG-TODO-016 | 待多代理子系统就绪后再开放 delegation 扩展 |
| COG-OQ05 | 详设 §12 | CognitionTelemetry 的 HealthProbe 如何实现 | 延后：首版 HealthProbe 先用 metrics 汇聚（last_latency / error_count），不引入独立 probe endpoint | COG-TODO-022 | 复杂度低优先；后续可扩展为主动探针 |
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
