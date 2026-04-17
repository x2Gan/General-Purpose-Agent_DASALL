# DASALL runtime 子系统专项 TODO

最近更新时间：2026-04-17
阶段：Detailed Design -> Special TODO
适用范围：runtime/
当前结论：runtime 详设已具备执行基线，但原专项 TODO 在 gate 分层、checkpoint replay、health/maintenance、smoke 证据语义上仍存在执行硬度缺口。本轮补强后将计划收敛为 31 项任务、12 道质量门，并显式区分 runtime-local fixture gate 与 true cross-module integration gate。

## 1. 文档头

输入依据：

1. docs/architecture/DASALL_runtime子系统详细设计.md
2. docs/architecture/DASALL_Agent_architecture.md
3. docs/architecture/DASALL_Engineering_Blueprint.md
4. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
5. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
6. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
7. docs/ssot/CrossModuleDataProjectionMatrix.md
8. docs/ssot/InfraConcurrencyPolicy.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. docs/todos/contracts/deliverables/WP02-T007-RuntimeBudget字段清单.md
12. docs/todos/contracts/deliverables/WP04-T008-ReflectionDecision字段表.md
13. docs/todos/contracts/deliverables/WP04-T010-RecoveryRequest字段表.md
14. docs/todos/contracts/deliverables/WP04-T012-RecoveryOutcome字段表.md
15. docs/todos/contracts/deliverables/WP04-T015-MultiAgentRequest字段表.md
16. docs/todos/contracts/deliverables/WP04-T019-WorkerTask字段表.md
17. runtime/CMakeLists.txt
18. runtime/src/placeholder.cpp
19. tests/unit/runtime/CMakeLists.txt
20. tests/unit/runtime/RuntimeSmokeTest.cpp
21. tests/integration/CMakeLists.txt
22. profiles/include/RuntimePolicySnapshot.h
23. llm/include/ILLMManager.h
24. contracts/include/checkpoint/Checkpoint.h
25. docs/todos/llm/DASALL_llm子系统专项TODO.md
26. docs/todos/tools/DASALL_tools子系统专项TODO.md
27. docs/todos/services/DASALL_capability_services子系统专项TODO.md
28. docs/worklog/DASALL_开发执行记录.md

行业补强参照：

1. Temporal / Cadence：durable execution、workflow versioning、replay-safe deployment。
2. Stripe：idempotency key 复用与首结果固定语义。
3. OpenTelemetry：trace/span hierarchy、span link、审计字段口径。
4. LangGraph：checkpoint persistence、thread-scoped replay、pending writes、versioned resume。

编制原则：

1. 不改写已冻结 ADR、SSOT、contracts 结论。
2. 不把 streaming、多 Agent、shared supporting-object admission 写成阶段 J 的前置 Done 项。
3. 证据不足处先补设计，不伪造实现任务。
4. 每个 TODO 必须同时给出代码目标、测试目标、验收命令。
5. 任务保持单主目标，避免 God Task。
6. 必须区分 runtime-local fixture 验证与 true cross-module integration 验证，不允许用 stub 或 fixture 冒充真联调完成。
7. checkpoint 相关任务必须同时覆盖字段守卫、version tag 校验和 deterministic replay 或 golden fixture regression。
8. health signal、watchdog、background maintenance 是一等 Build 对象，不得隐藏在 telemetry 说明里。
9. 现有 RuntimeSmokeTest 只可作为构建存活信号，不可作为 runtime 主控闭环证据。

## 2. 子系统目标与范围

子系统目标：

1. 把 runtime 从 placeholder-only 静态库收敛为 Layer 6 唯一全局主控平面。
2. 落实 `AgentFacade -> AgentOrchestrator -> Session/FSM/Budget/Checkpoint/Recovery/Scheduler/SafeMode` 的工程骨架。
3. 复用 frozen contracts，并在 runtime 内承接 supporting types、规则表和控制器实现。
4. 让 unary 主路径、resume 路径、checkpoint replay 路径、safe mode 路径与 health/background maintenance 路径都具备可验证的自动化门禁。

纳入范围：

1. runtime/include、runtime/src、tests/unit/runtime、tests/integration/agent_loop、CMake 接线。
2. `AgentFacade`、`AgentOrchestrator`、`SessionManager`、`AgentFsm`、`Scheduler`、`BudgetController`、`CheckpointManager`、`RecoveryManager`、`SafeModeController`、`RuntimeTelemetryBridge`、`RuntimeEventBus`、`RuntimeHealthProbe`。
3. `CheckpointState`↔FSM 映射、状态守卫表、`RT_E_*` 错误码域、取消传播、并发背压、watchdog 与 background maintenance hooks。

不纳入范围：

1. memory 的上下文装配实现。
2. llm 的 prompt/provider 实现。
3. tools 的执行治理实现。
4. streaming 主链与多 Agent 正式闭环。
5. 任意新的 shared contracts admission。

## 3. 输入依据与约束清单

### 3.1 约束清单

| ID | 来源 | 约束 | 对 TODO 的直接影响 |
|---|---|---|---|
| RT-TC001 | runtime 详设 1.1、2.1；架构 4.4、5.1 | runtime 必须保持唯一全局主控 | 不拆出第二调度中心、第二上下文中心、第二最终裁定者 |
| RT-TC002 | ADR-007；runtime 详设 6.11 | Reflection 只给建议，Recovery 才执行恢复 | 恢复链路只能由 `RecoveryManager` 落盘 |
| RT-TC003 | ADR-006；runtime 详设 6.13 | runtime 只能消费 `ContextPacket`，不能做 Prompt/Provider 实现 | 不生成上下文装配和 Prompt 任务 |
| RT-TC004 | runtime 详设 1.3、2.1；蓝图 4.2 | runtime 只能依赖 frozen contracts 与相邻模块 public interface | 不允许直连下游实现 |
| RT-TC005 | runtime 详设 6.10、6.16；RuntimePolicySnapshot.h | 预算、超时、取消传播是硬约束 | `BudgetController`、`CancellationToken`、safe mode gate 必须单列 |
| RT-TC006 | runtime 详设 6.5.1、6.20 | checkpoint/save/load/resume 必须可验证 | `CheckpointStateMapper`、`CheckpointManager`、resume integration 必须入表 |
| RT-TC007 | runtime 详设 6.14；InfraConcurrencyPolicy | 队列、锁顺序、backpressure 必须显式定义 | `Scheduler`、`RuntimeEventBus` 必须带并发门禁 |
| RT-TC008 | runtime 详设 6.12、6.15 | 状态迁移、预算拒绝、恢复拒绝、safe mode 必须可观测并归入 `RT_E_*` | telemetry/error domain 不是可选项 |
| RT-TC009 | CrossModuleDataProjectionMatrix | runtime 只能消费 projection，不得回灌 raw payload | 集成测试不能伪造 raw payload 直通 |
| RT-TC010 | runtime 详设 3.1、8.3；当前仓库现状 | 当前 runtime 仍是 placeholder-only，tests 只有 smoke | 必须先做骨架、control-plane surface test 和测试拓扑，不得直接宣称主链 ready |
| RT-TC011 | runtime 详设 8.3 RT-BLK-01；当前仓库现状 | memory/knowledge/tools runtime-facing public interface 尚未全部落位 | 集成任务必须带 blocker 或 fail-closed stub 策略 |
| RT-TC012 | runtime 详设 6.24、9.2 | 任务拆分优先落到显式接口、规则表、控制器和单一测试组 | 使用"补设计 / 接口 / 控制器 / 集成门禁"四段拆分 |
| RT-TC013 | runtime 详设 6.16.2；RT-C019 | 每个 Worker Ticket 必须绑定 CancellationToken，支持 step-level 超时取消传播 | `CancellationToken` 必须作为独立对象落盘，Scheduler 与 Worker 接口必须接受 token |
| RT-TC014 | ADR-007 3.3；runtime 详设 6.17.2；RT-C020 | 工具重试必须复用原 `retry_idempotency_token`，replan 必须生成新 token | RecoveryManager 的 retry admission 必须验证幂等令牌复用；完成判定须包含令牌断言 |
| RT-TC015 | runtime 详设 6.14.2；RT-C021 | 组件间获取多把锁时必须遵从声明的全局锁顺序（L1→L6），Recovery Handler Thread 仅持有只读快照 | Scheduler、AgentOrchestrator 实现任务的完成判定须声明锁顺序验证 |
| RT-TC016 | runtime 详设 6.20；RT-C022 | Checkpoint 必须携带版本元数据（`rt.schema_version` / `rt.fsm_state_enum_version`），resume 时做兼容性校验 | CheckpointManager 实现须包含 version tag 写入与 reject 路径 |
| RT-TC017 | runtime 详设 6.15；RT-C023 | Runtime 必须定义独立错误码域 `RT_E_*`，所有组件错误输出归入此域 | RuntimeErrorCode 定义与 TelemetryBridge 接线须保证码段不重叠且可结构化输出 |
| RT-TC018 | runtime 详设附录 A；行业实践（Temporal / LangGraph） | checkpoint 兼容 gate 不能只验证 version tag，还必须验证 deterministic replay 或 golden checkpoint fixture | 必须新增 replay compatibility 测试与独立 Gate |
| RT-TC019 | runtime 详设 6.23、8.2 J4；行业实践（Kubernetes / LLM health probe） | health signal、watchdog、后台维护任务必须显式落盘并有单测或集成验证 | health/probe/maintenance 不能并入 telemetry 任务说明后省略 |
| RT-TC020 | runtime 详设 6.12、6.18；行业实践（OpenTelemetry） | 追踪必须保留 request、session、turn、checkpoint 因果链，resume 与 retry 需要可关联的 trace 或 span 信息 | Telemetry task 必须验证 span 字段、resume/retry causal link 和 audit continuity |
| RT-TC021 | 已交付子系统 TODO 与 worklog 基线 | subsystem-local fixture gate 与 true cross-module integration gate 必须分开记录、分开验收 | unary gate 必须拆分为 fixture gate 和真集成 gate，证据回写不能混写 |
| RT-TC022 | 当前仓库现状；RuntimeSmokeTest.cpp | 现有 mock smoke 只能证明 build liveness，不能证明 runtime control-plane ready | 必须引入 runtime-local control-plane surface test 或 fixture integration test 替代 gate 语义 |
| RT-TC023 | runtime 详设 9.1 compatibility、10.3 灰度策略 | 至少 desktop_full、edge_balanced、edge_minimal 三档 profile 需要显式验证 runtime budget、degrade、enablement 差异 | 必须新增 RuntimeProfileCompatibilityTest 或等价 gate |

### 3.2 当前代码与测试现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| runtime/CMakeLists.txt | 仅编译 `src/placeholder.cpp` | runtime 已进构建图，但仍是 placeholder-only |
| runtime/src/placeholder.cpp | 只有占位函数 | 生产控制平面尚未开始 |
| runtime/include | 当前不存在 | 公共 ABI 和 supporting types 尚未落盘 |
| tests/unit/runtime/CMakeLists.txt | 只注册 `dasall_runtime_smoke_test` | runtime unit 拓扑尚未组件化 |
| tests/unit/runtime/RuntimeSmokeTest.cpp | 只串联 mock，不经过真实 control-plane | 不能作为 runtime 主控闭环证据 |
| tests/integration/CMakeLists.txt | 未接入 runtime / agent_loop | runtime integration discoverability 为空 |
| tests/CMakeLists.txt | 已有 unit/contract/integration 聚合目标 | runtime 可以直接复用现有测试聚合 |
| RuntimePolicySnapshot.h | 已有 runtime_budget、timeout、degrade、execution、ops 等策略域 | runtime 可直接消费现有 profile projection |
| ILLMManager.h | 已有 `init/generate/stream_generate/health_check` | llm 依赖面明确 |
| Checkpoint.h + contracts deliverables | `Checkpoint`、`RuntimeBudget`、`ReflectionDecision`、`RecoveryRequest`、`RecoveryOutcome` 等字段已冻结 | checkpoint/recovery/budget 相关任务有可靠真值 |

## 4. 粒度可行性评估

### 4.1 总体结论

1. 可直接做到 L3 的对象：`RuntimeErrorCode`、`CancellationToken`、`IAgentFsm`、`StateTransitionTypes`、`IBudgetController`、`CheckpointStateMapper`、`TransitionGuardTable`，以及 frozen contracts 的消费口径。
2. 可安全做到 L2 的对象：`ICheckpointManager`、`IRecoveryManager`、`ISessionManager`、`IScheduler`、`AgentFacade`、`AgentOrchestrator`、`SessionManager`、`Scheduler`、`SafeModeController`、`RuntimeHealthProbe`、`RuntimeProfileCompatibilityTest` 对应的 fixture 组合层。
3. 只能停在 L1 的对象：`RuntimeTelemetryBridge`、`RuntimeEventBus`、`BackgroundMaintenanceHooks`、`RuntimeDependencySet` 私有 helper 细节。
4. 仍然 blocked 的对象：true cross-module unary integration 与真端口持久化 round-trip，原因是相邻模块 runtime-facing public interface 未全部落位。

### 4.2 评估表

| 设计对象 | 锚点 | 粒度 | 结论 |
|---|---|---|---|
| `RuntimeErrorCode` / `CancellationToken` | 6.15、6.16、8.1、9.2 | L3 | 直接拆对象定义与单测 |
| `IAgentFsm` / `StateTransitionTypes` | 6.6、6.7.4、6.24.6 | L3 | 先接口，再守卫表，再实现 |
| `IBudgetController` / `BudgetDecision` | 6.10、6.24.7 | L3 | 直接拆接口与实现 |
| `CheckpointState`↔FSM / `TransitionGuardTable` | 6.5.1、6.7.4、9.2 | L3 | 适合独立规则表任务 |
| `ICheckpointManager` / `IRecoveryManager` | 6.20、6.24.8、6.24.9 | L2 | 方法集明确，但 supporting fields 未全成表 |
| `ISessionManager` / `AgentFacade` / `AgentOrchestrator` | 6.24.3、6.24.4、6.24.5 | L2 | 先补 supporting types 和 seam，再推进 |
| `IScheduler` / `SchedulerTicket` | 6.14.4、6.24.10 | L2 | 接口与策略明确，可先公共面后实现 |
| `RuntimeTelemetryBridge` / `RuntimeEventBus` / `RuntimeHealthProbe` / `BackgroundMaintenanceHooks` / `RuntimeDependencySet` | 6.12、6.18、6.23、6.24.12 | L1/L2 | Telemetry、EventBus、maintenance 保持轻量，health probe 可独立收敛并测试 |
| `RuntimeUnaryFixtureIntegration` | 7、8.3、9.4 | L2 | 先做 topology/fixture/stub，作为 runtime-local gate |
| `RuntimeUnaryIntegration` | 7、8.3、9.4 | Blocked | 只有相邻模块 public interface 落位后才能作为真集成 gate |

## 5. Design -> TODO 映射表

| Design 项 | 设计锚点 | 对应任务 | 说明 |
|---|---|---|---|
| supporting types、port seam、runtime-local fixture matrix、测试拓扑缺口 | 6.24.3、6.24.5、6.24.12、8.3 | RT-TODO-001 ~ 004 | 先消除证据缺口，再允许 Build-ready 任务进入执行 |
| runtime include 根、`IAgent`、control-plane surface smoke | 7、8.1、8.2 J0 | RT-TODO-005、025 | 先把 runtime 从 placeholder-only 变成可承载公共 ABI 且具备正确 smoke 语义的模块 |
| error/cancel、fsm、budget、checkpoint/recovery、session、scheduler 接口面 | 6.6、6.10、6.15、6.16、6.20、6.24 | RT-TODO-006 ~ 011 | 把显式接口和 supporting types 落到 runtime/include |
| CheckpointStateMapper、TransitionGuardTable | 6.5.1、6.7.4 | RT-TODO-012、013 | 规则表单独可测 |
| AgentFsm、BudgetController、CheckpointManager、RecoveryManager、SessionManager、Scheduler | 6.14、6.16、6.20、6.24 | RT-TODO-014 ~ 019 | P0 控制器逐个落盘 |
| AgentOrchestrator runtime-local 骨架与全控制器集成 | 6.8、6.9、6.24.4 | RT-TODO-020、021 | 先验证主循环 topology，再接入全部控制器 |
| SafeModeController、Telemetry、EventBus、Health、BackgroundMaintenance | 6.11、6.12、6.21、6.23、6.24 | RT-TODO-022、023、030 | 收口安全失败、可观测与后台健康维护 |
| contracts 字段级对齐、checkpoint replay compatibility、golden fixtures | 6.5、6.20、9.3、附录 A | RT-TODO-024、028 | 防止只验证 version tag 而不验证 replay 语义 |
| runtime-local unary fixture gate | 8.1、9.1、9.4、9.6 | RT-TODO-025、026 | 先证明 runtime 自身控制平面闭环成立 |
| true cross-module unary gate | 8.2 J3、9.4、9.6 | RT-TODO-027 | 仅在相邻模块 public interface 落位后才可宣称联调 ready |
| profile compatibility 与灰度差异验证 | 9.1 compatibility、10.3 | RT-TODO-029 | 把 profile 差异从隐含假设变成显式 Gate |
| Gate 证据与 blocker 回写 | 9.6、11、12 | RT-TODO-031 | fixture gate、真集成 gate、残余 blocker 必须分开回写 |

### 5.2 评审修正覆盖表

| 评审发现 | 补强动作 | 对应任务 | 收口方式 |
|---|---|---|---|
| stub gate 与真集成 gate 混用，容易产生假阳性 | 把 unary gate 拆成 runtime-local fixture gate 与 true cross-module integration gate | RT-TODO-025 ~ 027、RT-TODO-031 | Gate-RT-07 与 Gate-RT-11 分开验收、分开回写 |
| checkpoint 只校验 version tag，缺少 replay regression | 增加 golden checkpoint fixtures、replay compatibility contract 和 replay regression integration | RT-TODO-024、RT-TODO-028 | Gate-RT-08 单独阻断 |
| health、watchdog、background maintenance 未显式落到 TODO | 把 health probe 与 maintenance hooks 升格为显式任务与测试 | RT-TODO-023、RT-TODO-030 | Gate-RT-06、Gate-RT-10 收口 |
| 旧 RuntimeSmokeTest 容易被误当作主链证据 | 增加 RuntimeControlPlaneSurfaceTest，并显式降级旧 smoke 语义 | RT-TODO-025、RT-R08 | Gate-RT-02、Gate-RT-11 写死规则 |
| 顶层 pass criteria 弱于详设的 replay、compatibility、concurrency 要求 | 扩展测试矩阵、质量门和 profile gate | RT-TODO-024、RT-TODO-028 ~ 031 | 12 道质量门覆盖 replay、profile、health、evidence |
| subsystem-local 与 cross-module 证据回写不分层 | 新增 blocker 校准表与 Gate 执行证据表 | RT-TODO-031 | 专项 TODO 与 worklog 双写回 |

## 6. 原子任务清单

### 6.1 前置补设计 / 接缝收敛任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RT-TODO-001 | NotStarted | 补齐 AgentFacade supporting types 口径 | runtime 详设 6.24.3、8.1 | 6.24.3 `AgentInitRequest`/`AgentInitResult`/`HandleOptions`/`ResumeHandleRequest` 口径 | L0 | runtime 详设；本 TODO | `AgentInitRequest`、`AgentInitResult`、`HandleOptions`、`ResumeHandleRequest` | 文档一致性 | `rg -n "AgentInitRequest\|AgentInitResult\|HandleOptions\|ResumeHandleRequest" docs/architecture/DASALL_runtime子系统详细设计.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md` | 无 | RT-BLK-04 | 完成本任务 | docs/todos/runtime/deliverables/RT-TODO-001-AgentFacade-supporting-types口径收敛.md；更新后的 runtime 详设 | types 有职责、字段概要、落盘位置，可被后续 Build 任务引用 |
| RT-TODO-002 | NotStarted | 补齐 SessionManager supporting types 口径 | runtime 详设 6.24.5、8.1 | 6.24.5 `SessionSnapshot`/`PendingInteractionState`/`TurnPersistPlan`/`ResumeSeed` 口径 | L0 | runtime 详设；本 TODO | `SessionSnapshot`、`PendingInteractionState`、`TurnPersistPlan`、`ResumeSeed` | 文档一致性 | `rg -n "SessionSnapshot\|PendingInteractionState\|TurnPersistPlan\|ResumeSeed" docs/architecture/DASALL_runtime子系统详细设计.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md` | 无 | RT-BLK-04 | 完成本任务 | docs/todos/runtime/deliverables/RT-TODO-002-SessionManager-supporting-types口径收敛.md；更新后的 runtime 详设 | session supporting types 输入/输出语义明确，可被后续 Build 任务引用 |
| RT-TODO-003 | NotStarted | 收敛 RuntimeDependencySet 与相邻模块 seam | runtime 详设 6.2、6.13、6.24.12、8.3 | 6.24.12 `RuntimeDependencySet` 组合根；6.13 相邻模块调用面 | L0 | runtime 详设；本 TODO | `RuntimeDependencySet`、fail-closed stub、null adapter | 文档一致性 | `rg -n "RuntimeDependencySet\|fail-closed stub\|null adapter\|public interface" docs/architecture/DASALL_runtime子系统详细设计.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md` | 无 | RT-BLK-05 | 完成本任务 | docs/todos/runtime/deliverables/RT-TODO-003-RuntimeDependencySet-seam收敛.md；更新后的 runtime 详设 | seam 口径唯一，不再假设真实端口已 ready |
| RT-TODO-004 | NotStarted | 收敛 runtime 测试拓扑与 caller fixture | runtime 详设 7、8.1、9.1、9.4 | 8.1 目录布局；9.1 测试矩阵；9.4 集成测试路径 | L0 | runtime 详设；本 TODO | `tests/unit/runtime/`、`tests/integration/agent_loop/`、caller fixture | 文档一致性 | `rg -n "tests/unit/runtime\|tests/integration/agent_loop\|RuntimeUnaryIntegrationTest\|RuntimeResumeIntegrationTest\|RuntimeSafeModeIntegrationTest" docs/architecture/DASALL_runtime子系统详细设计.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md` | 无 | RT-BLK-06 | 完成本任务 | docs/todos/runtime/deliverables/RT-TODO-004-runtime测试拓扑与fixture收敛.md；更新后的 runtime 详设 | discoverability 和 fixture 边界稳定 |

### 6.2 Build-ready 接口与骨架任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RT-TODO-005 | NotStarted | 新增 runtime include 布局与 CMake 骨架 | runtime 详设 7、8.1、8.2 J0 | 8.1 目录布局；8.2 J0 public surface 建立 | L2 | `runtime/include/IAgent.h`、`runtime/include/AgentTypes.h`、`runtime/CMakeLists.txt`、`runtime/src/AgentFacade.cpp` | `IAgent::init/handle/resume/stop`、`AgentFacade`、组合根 | build 通过，且不再依赖旧 smoke 语义 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime` | 001、003 | 无 | — | `runtime/include/IAgent.h`、`runtime/include/AgentTypes.h`、更新后的 `runtime/CMakeLists.txt`、`runtime/src/AgentFacade.cpp` | runtime 不再只靠 placeholder 维持空库，且 RT-TODO-025 之前不再把旧 smoke 当作交付依据 |
| RT-TODO-006 | NotStarted | 定义 RuntimeErrorCode 与 CancellationToken | runtime 详设 6.15、6.16、8.1；RT-TC013、RT-TC017 | 6.15.1 错误码分类表；6.16.2 取消令牌语义 | L3 | `runtime/include/RuntimeErrorCode.h`、`runtime/include/CancellationToken.h` | `RT_E_1xx`~`RT_E_6xx` 全码段枚举、`CancellationToken::cancel/is_cancelled/bind_deadline` | `RuntimeErrorCodeTest`、`CancellationTokenTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "RuntimeErrorCodeTest\|CancellationTokenTest" --output-on-failure` | 005 | 无 | — | `runtime/include/RuntimeErrorCode.h`、`runtime/include/CancellationToken.h`、`tests/unit/runtime/RuntimeErrorCodeTest.cpp`、`tests/unit/runtime/CancellationTokenTest.cpp` | error/cancel 公共面可编译且可测；码段不重叠；cancel 跨线程可见 |
| RT-TODO-007 | NotStarted | 定义 IAgentFsm 与 StateTransitionTypes | runtime 详设 6.6、6.7.4、6.24.6 | 6.6 接口表；6.7.1 运行状态枚举；6.7.4 守卫表列定义 | L3 | `runtime/include/fsm/IAgentFsm.h`、`runtime/include/fsm/StateTransitionTypes.h` | `IAgentFsm::current_state/can_enter/transition/is_terminal`、`RuntimeState`(17态)、`StateTransitionRequest`、`StateTransitionOutcome`、`TransitionRejectionReason` | `AgentFsmTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R AgentFsmTest --output-on-failure` | 005 | 无 | — | `runtime/include/fsm/IAgentFsm.h`、`runtime/include/fsm/StateTransitionTypes.h` | 显式状态机输入/输出类型稳定；17 态枚举与详设 6.7.1 一致 |
| RT-TODO-008 | NotStarted | 定义 IBudgetController 与 BudgetDecision | runtime 详设 6.10、6.24.7；RT-TC005 | 6.24.7 接口表；6.10 配置映射表 | L3 | `runtime/include/budget/IBudgetController.h`、`runtime/include/budget/BudgetDecision.h` | `IBudgetController::initialize/consume/snapshot/can_continue/can_replan/can_call_tool`、`BudgetDecision`、`BudgetViolationClass` | `BudgetControllerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R BudgetControllerTest --output-on-failure` | 005、006 | 无 | — | `runtime/include/budget/IBudgetController.h`、`runtime/include/budget/BudgetDecision.h` | 预算公共面覆盖 turn/tool/replan/latency 五维；消费 RuntimeBudget 不重解释语义 |
| RT-TODO-009 | NotStarted | 定义 checkpoint/recovery include 面 | runtime 详设 6.20、6.24.8、6.24.9；RT-TC006、RT-TC016 | 6.24.9 CheckpointManager 接口表；6.24.8 RecoveryManager 接口表；6.20 版本兼容方案 | L2 | `runtime/include/checkpoint/ICheckpointManager.h`、`runtime/include/checkpoint/CheckpointBuildTypes.h`、`runtime/include/recovery/IRecoveryManager.h`、`runtime/include/recovery/ResumePlan.h` | `ICheckpointManager::build_checkpoint/save/load/validate/make_resume_plan`、`IRecoveryManager::evaluate/execute/apply`、`ResumePlan`、`CheckpointBuildRequest` | `CheckpointManagerTest`、`RecoveryManagerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "CheckpointManagerTest\|RecoveryManagerTest" --output-on-failure` | 005 | 无 | — | `runtime/include/checkpoint/ICheckpointManager.h`、`runtime/include/checkpoint/CheckpointBuildTypes.h`、`runtime/include/recovery/IRecoveryManager.h`、`runtime/include/recovery/ResumePlan.h` | checkpoint/recovery 公共面与详设一致；含 version tag key 定义 |
| RT-TODO-010 | NotStarted | 定义 ISessionManager 与 session public types | runtime 详设 6.24.5、8.1 | 6.24.5 SessionManager 接口表；6.6 `ISessionManager` 方法建议 | L2 | `runtime/include/session/ISessionManager.h`、`runtime/include/session/SessionTypes.h` | `ISessionManager::load_session/prepare_turn/persist_turn/bind_checkpoint_ref/build_resume_seed`、`SessionSnapshot`、`PendingInteractionState`、`ResumeSeed` | `SessionTypeSurfaceTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R SessionTypeSurfaceTest --output-on-failure` | 002、005 | RT-BLK-04 | 完成 002 | `runtime/include/session/ISessionManager.h`、`runtime/include/session/SessionTypes.h`、`tests/unit/runtime/SessionTypeSurfaceTest.cpp` | session public surface 有独立接口面测试且可直接支撑后续 resume 任务 |
| RT-TODO-011 | NotStarted | 定义 IScheduler 与 SchedulerTicket | runtime 详设 6.14.4、6.24.10；RT-TC007、RT-TC013 | 6.24.10 Scheduler 接口表；6.14.4 队列背压表 | L2 | `runtime/include/scheduling/IScheduler.h`、`runtime/include/scheduling/SchedulerTicket.h` | `IScheduler::enqueue/acquire_worker/release_worker/backpressure_state`、`SchedulerTicket`、`SchedulerTicketRequest` | `SchedulerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R SchedulerTest --output-on-failure` | 005、006 | 无 | — | `runtime/include/scheduling/IScheduler.h`、`runtime/include/scheduling/SchedulerTicket.h` | 调度公共面包含队列与背压输出口；Ticket 接口含 CancellationToken 绑定点 |

### 6.3 控制器与控制平面实现任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RT-TODO-012 | NotStarted | 实现 CheckpointStateMapper | runtime 详设 6.5.1、6.20、9.2；RT-TC016 | 6.5.1 CheckpointState↔FSM 映射表全量 | L3 | `runtime/src/checkpoint/CheckpointStateMapper.cpp` | `CheckpointStateMapper::to_checkpoint_state/can_resume_from` | `CheckpointStateMapperTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R CheckpointStateMapperTest --output-on-failure` | 007、009 | 无 | — | `runtime/src/checkpoint/CheckpointStateMapper.cpp`、`tests/unit/runtime/CheckpointStateMapperTest.cpp` | §6.5.1 全部映射规则自动覆盖；Failed/Succeeded resume 拒绝可断言 |
| RT-TODO-013 | NotStarted | 实现 TransitionGuardTable | runtime 详设 6.7.4、9.2 | 6.7.4 状态转移守卫表全量 | L3 | `runtime/src/fsm/TransitionGuardTable.cpp` | `TransitionGuardTable::is_legal/get_guard/get_checkpoint_strategy` | `TransitionGuardTableTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R TransitionGuardTableTest --output-on-failure` | 007 | 无 | — | `runtime/src/fsm/TransitionGuardTable.cpp`、`tests/unit/runtime/TransitionGuardTableTest.cpp` | §6.7.4 全表合法/非法转移可二值断言；非法转移必须输出 TransitionRejectionReason |
| RT-TODO-014 | NotStarted | 实现 AgentFsm | runtime 详设 6.7、6.24.6 | 6.24.6 AgentFSM 接口表；6.7.3 主状态迁移图 | L2 | `runtime/src/fsm/AgentFsm.cpp` | `AgentFsm::current_state/transition/is_terminal/can_enter` | `AgentFsmTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R AgentFsmTest --output-on-failure` | 007、013 | 无 | — | `runtime/src/fsm/AgentFsm.cpp`、`tests/unit/runtime/AgentFsmTest.cpp` | Waiting/FailedSafe/Completed 等 17 态显式可访问；非法转移拒绝有 rejection reason |
| RT-TODO-015 | NotStarted | 实现 BudgetController | runtime 详设 6.10、6.16、6.24.7；RT-TC005 | 6.24.7 BudgetController 接口表；6.16.1 超时维度表 | L2 | `runtime/src/budget/BudgetController.cpp` | `BudgetController::initialize/consume/snapshot/can_continue/can_replan/can_call_tool` | `BudgetControllerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R BudgetControllerTest --output-on-failure` | 006、008 | 无 | — | `runtime/src/budget/BudgetController.cpp`、`tests/unit/runtime/BudgetControllerTest.cpp` | 五维预算扣减和拒绝理由稳定；扣减点覆盖 turn/tool/replan/latency |
| RT-TODO-016 | NotStarted | 实现 CheckpointManager | runtime 详设 6.20、6.24.9；RT-TC006、RT-TC016 | 6.24.9 CheckpointManager 接口表；6.20 版本校验方案 | L2 | `runtime/src/checkpoint/CheckpointManager.cpp` | `CheckpointManager::build_checkpoint/save/load/validate/make_resume_plan` | `CheckpointManagerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "CheckpointManagerTest\|CheckpointStateMapperTest" --output-on-failure` | 009、012 | 无 | — | `runtime/src/checkpoint/CheckpointManager.cpp`、`tests/unit/runtime/CheckpointManagerTest.cpp` | waiting state 含 pending_action；schema version reject 可测；`rt.schema_version`/`rt.fsm_state_enum_version` tag 写入验证 |
| RT-TODO-017 | NotStarted | 实现 RecoveryManager | runtime 详设 6.11、6.17、6.24.8；RT-TC002、RT-TC014 | 6.24.8 RecoveryManager 接口表；6.17.2 retry_idempotency_token；6.11.1 Recovery Context 可见性边界表 | L2 | `runtime/src/recovery/RecoveryManager.cpp` | `RecoveryManager::evaluate/execute/apply` | `RecoveryManagerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R RecoveryManagerTest --output-on-failure` | 006、009、015、016 | 无 | — | `runtime/src/recovery/RecoveryManager.cpp`、`tests/unit/runtime/RecoveryManagerTest.cpp` | retry/replan/abort_safe/degrade admission 明确；retry 必须复用 `retry_idempotency_token`，replan 必须生成新 token；rejection_reason 引用 RT_E_* 码 |
| RT-TODO-018 | NotStarted | 实现 SessionManager | runtime 详设 6.24.5、9.4 | 6.24.5 SessionManager 接口表；9.4 集成测试路径 #3 resume | L2 | `runtime/src/session/SessionManager.cpp` | `SessionManager::load_session/prepare_turn/persist_turn/bind_checkpoint_ref/build_resume_seed` | `SessionManagerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R SessionManagerTest --output-on-failure` | 002、010、016 | RT-BLK-01 | 完成 003 或相邻模块提供 stub | `runtime/src/session/SessionManager.cpp`、更新后的 `tests/unit/runtime/SessionManagerTest.cpp` | waiting state 与 checkpoint anchor 路径可二值验证；fail-closed stub 下可测；resume 集成语义在 RT-TODO-028 单独收口 |
| RT-TODO-019 | NotStarted | 实现 Scheduler | runtime 详设 6.14.4、6.24.10；RT-TC007、RT-TC013、RT-TC015 | 6.14.4 队列背压与溢出策略表；6.14.2 锁顺序表 | L2 | `runtime/src/scheduling/Scheduler.cpp` | `Scheduler::enqueue/acquire_worker/release_worker/backpressure_state` | `SchedulerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R SchedulerTest --output-on-failure` | 006、011 | 无 | — | `runtime/src/scheduling/Scheduler.cpp`、`tests/unit/runtime/SchedulerTest.cpp` | 队列深度、overflow、背压语义稳定；Worker Ticket 绑定 CancellationToken；符合 §6.14.2 锁顺序（L5:queue_mutex） |
| RT-TODO-020 | NotStarted | 实现 AgentOrchestrator 骨架与 stub 主循环 | runtime 详设 6.8、6.9、6.24.4 | 6.24.4 AgentOrchestrator 核心内部阶段：preflight/main_loop/tool_round/recovery_round/terminalize | L2 | `runtime/src/AgentOrchestrator.cpp` | `AgentOrchestrator::run_once`（stub 下游端口） | `AgentOrchestratorSkeletonTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R AgentOrchestratorSkeletonTest --output-on-failure` | 003、014 | RT-BLK-05 | 完成 003 | `runtime/src/AgentOrchestrator.cpp`、`tests/unit/runtime/AgentOrchestratorSkeletonTest.cpp` | 五段主循环 topology 连通（使用 stub/null 端口）；FSM 推进可追踪 |
| RT-TODO-021 | NotStarted | AgentOrchestrator 全控制器集成 | runtime 详设 6.8、6.9、6.24.4、7；RT-TC015 | 6.24.4 全量执行流；6.14.2 锁顺序表 | L2 | `runtime/src/AgentOrchestrator.cpp` | `AgentOrchestrator::run_once/continue_from_checkpoint/handle_waiting_state`（接入全部控制器，仍使用 seam/stub 证明 runtime-local 闭环） | `AgentOrchestratorControllerAssemblyTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R AgentOrchestratorControllerAssemblyTest --output-on-failure` | 015、016、017、018、019、020 | 无 | — | 更新后的 `runtime/src/AgentOrchestrator.cpp`、`tests/unit/runtime/AgentOrchestratorControllerAssemblyTest.cpp` | preflight/decision/execution/recovery/terminal 五段主循环连通；符合 §6.14.2 锁顺序声明；本任务只证明 runtime-local 控制器装配成立，不外推真端口联调 ready |
| RT-TODO-022 | NotStarted | 实现 SafeModeController | runtime 详设 6.21、6.24.11 | 6.21.1 降级触发分类表；6.21.2 声明式降级链 | L2 | `runtime/src/safety/SafeModeController.cpp` | `SafeModeController::evaluate_entry/evaluate_exit/current_mode` | `SafeModeControllerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R SafeModeControllerTest --output-on-failure` | 006、017、021 | 无 | — | `runtime/src/safety/SafeModeController.cpp`、`tests/unit/runtime/SafeModeControllerTest.cpp` | timeout、budget、dependency unavailable 的降级分类可单元验证；safe mode 集成语义在 RT-TODO-030 单独收口 |
| RT-TODO-023 | NotStarted | 实现 RuntimeTelemetryBridge、RuntimeEventBus、RuntimeHealthProbe 与 BackgroundMaintenance hooks | runtime 详设 6.12、6.18、6.21、6.23、6.24.12；RT-TC008、RT-TC019、RT-TC020、RT-TC021 | 6.12 可观测性设计表；6.18 关联 ID 所有权与传播；6.23 后台维护任务表；8.2 J4 health 与 Gate 收口 | L1/L2 | `runtime/src/telemetry/RuntimeTelemetryBridge.cpp`、`runtime/src/telemetry/RuntimeEventBus.cpp`、`runtime/src/health/RuntimeHealthProbe.cpp`、`runtime/src/maintenance/BackgroundMaintenanceHooks.cpp` | `RuntimeTelemetryBridge::emit_transition/emit_budget_reject/emit_recovery_reject/emit_safe_mode`、`RuntimeEventBus::publish/subscribe`、`RuntimeHealthProbe::collect_snapshot`、`BackgroundMaintenanceHooks::publish_idle_tick` | `RuntimeTelemetryBridgeTest`、`RuntimeEventBusTest`、`RuntimeHealthProbeTest`、`RuntimeBackgroundMaintenanceHookTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "Runtime(TelemetryBridge|EventBus|HealthProbe|BackgroundMaintenanceHook)Test" --output-on-failure` | 006、013、017、021、022 | 无 | — | `runtime/src/telemetry/RuntimeTelemetryBridge.cpp`、`runtime/src/telemetry/RuntimeEventBus.cpp`、`runtime/src/health/RuntimeHealthProbe.cpp`、`runtime/src/maintenance/BackgroundMaintenanceHooks.cpp`、对应测试文件 | 结构化字段统一且引用 RT_E_*；request_id、session_id、trace_id、turn_id、checkpoint_id 可贯通；resume 与 retry 因果链可追踪；health、watchdog、background maintenance 不阻塞 Main Loop；EventBus 背压策略与 §6.14.4 一致 |

### 6.4 测试支撑、集成、兼容与 Gate 收口任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| RT-TODO-024 | NotStarted | Contracts 字段级与 checkpoint replay 兼容验证 | runtime 详设 6.5、6.20、9.3、附录 A；RT-TC006、RT-TC016、RT-TC018 | 6.5 核心对象与 contracts 对齐关系表；6.20 版本兼容方案；附录 A durable execution 改进项 | L2 | `tests/contract/runtime/`、`tests/fixtures/runtime/checkpoints/`、`tests/integration/agent_loop/RuntimeCheckpointReplayCompatibilityTest.cpp` | `RuntimeBudget`、`Checkpoint`、`ReflectionDecision`、`RecoveryRequest`、`RecoveryOutcome`、golden checkpoint fixtures、`ResumePlan` 稳定性 | `RuntimeBudgetContractTest`、`CheckpointFieldContractTest`、`ReflectionDecisionContractTest`、`RecoveryRequestContractTest`、`RecoveryOutcomeContractTest`、`RuntimeCheckpointReplayCompatibilityTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -R "(RuntimeBudgetContractTest|CheckpointFieldContractTest|ReflectionDecisionContractTest|RecoveryRequestContractTest|RecoveryOutcomeContractTest|RuntimeCheckpointReplayCompatibilityTest)" --output-on-failure` | 009、016、017 | 无 | — | `tests/contract/runtime/`、`tests/fixtures/runtime/checkpoints/`、`tests/integration/agent_loop/RuntimeCheckpointReplayCompatibilityTest.cpp` | shared contracts 字段无回退；同一 golden checkpoint fixture 反复加载时得到稳定的 `ResumePlan`、状态映射与 reject 语义；version mismatch、pending_action 缺失和非法终态 resume 都能显式拒绝 |
| RT-TODO-025 | NotStarted | 接线 runtime unit、integration、fixture 测试拓扑并替换旧 smoke 语义 | runtime 详设 8.1、9.1、9.4；RT-TC010、RT-TC021、RT-TC022 | 8.1 目录布局；9.1 测试矩阵；9.4 集成测试路径 | L2 | `tests/unit/runtime/CMakeLists.txt`、`tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt`、`tests/integration/agent_loop/CMakeLists.txt`、`tests/fixtures/runtime/`、`tests/unit/runtime/RuntimeControlPlaneSurfaceTest.cpp` | discoverability topology、control-plane surface smoke、fixture roots | `ctest -N`、`RuntimeControlPlaneSurfaceTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci -R RuntimeControlPlaneSurfaceTest --output-on-failure` | 004、005 | RT-BLK-06 | 完成 004 | 更新后的 CMakeLists.txt 文件集、`tests/fixtures/runtime/`、`tests/unit/runtime/RuntimeControlPlaneSurfaceTest.cpp` | runtime unit、integration、fixture 用例都能被顶层 `ctest -N` 发现；现有 `RuntimeSmokeTest` 被显式降级为 build-liveness-only 或被替换，不再作为 Gate 证据 |
| RT-TODO-026 | NotStarted | 验证 RuntimeUnaryFixtureIntegration 主成功链 | runtime 详设 7、8.2 J1~J3、9.4、9.6；RT-TC021 | 9.4 集成测试路径 #1 的 runtime-local 变体；9.6 unary Gate 的 subsystem-local 层 | L2 | `tests/integration/agent_loop/RuntimeUnaryFixtureIntegrationTest.cpp` | unary runtime-local fixture chain：`AgentRequest -> Session/FSM/Budget/Checkpoint/Recovery -> AgentResult` | `RuntimeUnaryFixtureIntegrationTest`、`AgentOrchestratorSkeletonTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests dasall_unit_tests && ctest --test-dir build-ci -R "(RuntimeUnaryFixtureIntegrationTest|AgentOrchestratorSkeletonTest)" --output-on-failure` | 021、023、024、025 | 无 | — | `tests/integration/agent_loop/RuntimeUnaryFixtureIntegrationTest.cpp` | 主链真实经过 `AgentFacade` 与 `AgentOrchestrator`，并使用 RT-TODO-003 约定的 fail-closed stub 或 null adapter 完成 runtime-local 闭环；该任务只证明 subsystem-local ready，不外推 true integration ready |
| RT-TODO-027 | NotStarted | 验证 RuntimeUnaryIntegration 真端口成功链 | runtime 详设 7、8.2 J3、9.4、9.6；RT-TC011、RT-TC021 | 9.4 集成测试路径 #1 正常链路；9.6 RT-GATE-04 的 true integration 层 | L2 | `tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp` | unary 真端口成功链：`AgentRequest -> ContextPacket -> cognition -> tools -> AgentResult` | `RuntimeUnaryIntegrationTest`、`MainFlowContractE2ETest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests dasall_contract_tests && ctest --test-dir build-ci -R "(RuntimeUnaryIntegrationTest|MainFlowContractE2ETest)" --output-on-failure` | 026、023、024、025 | RT-BLK-01 | 完成 RT-BLK-01 解阻，且 026 已通过 | `tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp` | 主链真实经过 runtime 与相邻模块 public interface，且 fixture gate 与 true integration gate 的结果会在 RT-TODO-031 中分开回写 |
| RT-TODO-028 | NotStarted | 验证 RuntimeResumeIntegration 与 checkpoint replay regression | runtime 详设 7、8.2 J2/J3、9.4、9.5、9.6；RT-TC016、RT-TC018 | 9.4 集成测试路径 #3 Resume；9.5 failure injection；附录 A replay-safe 改进项 | L2 | `tests/integration/agent_loop/RuntimeResumeIntegrationTest.cpp`、`tests/integration/agent_loop/RuntimeCheckpointReplayRegressionTest.cpp` | waiting-state resume、incompatible schema reject、golden checkpoint replay regression | `RuntimeResumeIntegrationTest`、`RuntimeCheckpointReplayRegressionTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "(RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest)" --output-on-failure` | 018、021、024、025、026 | 无 | — | `tests/integration/agent_loop/RuntimeResumeIntegrationTest.cpp`、`tests/integration/agent_loop/RuntimeCheckpointReplayRegressionTest.cpp` | waiting resume、incompatible schema reject 与 replay regression 都可断言；若真端口持久化仍未 ready，本任务只能宣称 runtime-owned resume 语义已验证，不能替代 RT-TODO-027 |
| RT-TODO-029 | NotStarted | 验证 RuntimeProfileCompatibility | runtime 详设 9.1 compatibility、10.3 灰度策略；RT-TC023 | 9.1 compatibility 行；10.3 按 profile 灰度策略 | L2 | `tests/integration/agent_loop/RuntimeProfileCompatibilityTest.cpp` | `desktop_full`、`edge_balanced`、`edge_minimal` 至少三档 profile 下的 runtime_budget、degrade、enablement 差异 | `RuntimeProfileCompatibilityTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R RuntimeProfileCompatibilityTest --output-on-failure` | 015、021、023、025、026 | 无 | — | `tests/integration/agent_loop/RuntimeProfileCompatibilityTest.cpp` | profile 差异全部由 `RuntimePolicySnapshot` 投影视图驱动；`desktop_full`、`edge_balanced`、`edge_minimal` 的通过、降级、拒绝结论可二值判定，且不依赖测试私有映射 |
| RT-TODO-030 | NotStarted | 验证 RuntimeSafeMode、Health、Cancellation 与 Concurrency Gate | runtime 详设 6.14、6.16、6.21、6.23、9.1、9.6；RT-TC007、RT-TC013、RT-TC015、RT-TC019、RT-TC020 | 9.5 失败注入测试点；9.6 RT-GATE-02 / RT-GATE-08；8.2 J4 health 收口 | L2 | `tests/integration/agent_loop/RuntimeSafeModeIntegrationTest.cpp`、`tests/integration/agent_loop/RuntimeHealthMaintenanceIntegrationTest.cpp`、`tests/unit/runtime/` | safe mode、health degrade、idle maintenance、cancel、backpressure、lock-order stress | `RuntimeSafeModeIntegrationTest`、`RuntimeHealthMaintenanceIntegrationTest`、`CancellationTokenTest`、`SchedulerTest`、`RuntimeHealthProbeTest`、`RuntimeBackgroundMaintenanceHookTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "(RuntimeSafeModeIntegrationTest|RuntimeHealthMaintenanceIntegrationTest|CancellationTokenTest|SchedulerTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest)" --output-on-failure` | 019、022、023、025、026 | 无 | — | `tests/integration/agent_loop/RuntimeSafeModeIntegrationTest.cpp`、`tests/integration/agent_loop/RuntimeHealthMaintenanceIntegrationTest.cpp`、对应 unit tests | timeout、budget overrun、dependency unavailable、health degrade、queue overflow 都能进入可审计终态；§6.14.2 锁顺序无死锁；后台维护任务不会阻塞 Main Loop |
| RT-TODO-031 | NotStarted | 回写 runtime 专项 Gate 与交付证据 | runtime 详设 9.6、11、12；成熟子系统 TODO 与 worklog 基线；RT-TC021、RT-TC022 | 9.6 Gate 建议清单；§11 回退策略；已交付子系统 blocker-first 回写风格 | L2 | `docs/todos/runtime/DASALL_runtime子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` | Gate 结论、blocker 状态、runtime-local 与 true integration 分层证据、残余风险、命令证据 | 全量门禁复验与证据双写回 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime dasall_unit_tests dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci -R "(RuntimeControlPlaneSurfaceTest|RuntimeErrorCodeTest|CancellationTokenTest|AgentFsmTest|BudgetControllerTest|CheckpointManagerTest|RecoveryManagerTest|SchedulerTest|TransitionGuardTableTest|CheckpointStateMapperTest|SessionTypeSurfaceTest|SessionManagerTest|SafeModeControllerTest|AgentOrchestratorSkeletonTest|AgentOrchestratorControllerAssemblyTest|RuntimeTelemetryBridgeTest|RuntimeEventBusTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest|RuntimeBudgetContractTest|CheckpointFieldContractTest|ReflectionDecisionContractTest|RecoveryRequestContractTest|RecoveryOutcomeContractTest|RuntimeCheckpointReplayCompatibilityTest|RuntimeUnaryFixtureIntegrationTest|RuntimeUnaryIntegrationTest|RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest|RuntimeProfileCompatibilityTest|RuntimeSafeModeIntegrationTest|RuntimeHealthMaintenanceIntegrationTest|MainFlowContractE2ETest)" --output-on-failure` | 027 ~ 030 | 无 | — | 更新后的 TODO、worklog、deliverables 回链记录 | 每个 Gate 都有命令证据和状态回写；runtime-local fixture gate 与 true integration gate 的结论分开记录；残余 blocker 与风险终态明确 |

## 7. 执行顺序建议

### 7.1 串并行编排

| 阶段 | 任务 | 编排建议 | 说明 |
|---|---|---|---|
| A 补设计与接缝收敛 | 001 ~ 004 | 001/002 并行，003 在其后，004 再收口 | 先冻结 supporting types、seam、runtime-local fixture matrix 与测试拓扑 |
| B Public surface 与骨架 | 005 ~ 011 | 005 串行起步，006 ~ 011 按域并行 | 先建 runtime/include 根和接口面，并让模块摆脱 placeholder-only |
| C 规则表与控制器核心 | 012 ~ 019 | 012/013/015/019 并行；014 依赖 013；016 依赖 012；017 依赖 015/016；018 依赖 016 | 先状态规则，再 checkpoint、recovery、session、scheduler |
| D Orchestrator 两阶段 | 020、021 | 020 先（骨架+stub），021 后（全控制器集成，但仍只证明 runtime-local 闭环） | 先验证主循环 topology，再接入全量控制器 |
| E 安全、观测与 replay 基础 | 022、023、024 | 022 先，023 与 024 可并行 | 收口 safe mode、telemetry、health/maintenance 与 checkpoint compatibility |
| F runtime-local gates 与兼容性 | 025、026、028、029、030 | 025 先；026、028、029 可并行；030 在 022/023 后 | runtime-local fixture gate、resume/replay、profile、safe mode/health/concurrency 都先于真集成 gate |
| G true cross-module integration | 027 | 仅在 RT-BLK-01 解阻后执行 | 027 阻塞时，专项状态只能写为 subsystem-local ready，不能外推全链 ready |
| H 证据与 blocker 收口 | 031 | 串行 | 所有 Gate、blocker、残余风险、下一步动作集中回写 |

### 7.2 必过门禁表

| Gate ID | 对应设计或补强点 | 通过条件 | 关联任务 |
|---|---|---|---|
| Gate-RT-01 | TODO 补强 | supporting types、seam、topology 口径统一 | 001 ~ 004 |
| Gate-RT-02 | TODO 补强 | runtime public surface 摆脱 placeholder-only，且旧 smoke 不再承担 Gate 语义 | 005、025 |
| Gate-RT-03 | RT-GATE-06 / RT-GATE-07 | FSM 守卫表与 CheckpointState 映射一致 | 012 ~ 014 |
| Gate-RT-04 | RT-GATE-03 | budget、checkpoint、recovery、session primitive 全绿 | 015 ~ 018、024 |
| Gate-RT-05 | RT-GATE-08 | scheduler、cancel、backpressure、lock-order 语义可自动验证 | 006、011、019、030 |
| Gate-RT-06 | 8.2 J4 health 收口 | telemetry、health、background maintenance 字段与行为可验证 | 022、023、030 |
| Gate-RT-07 | TODO 补强 | runtime-local unary fixture loop 通过 | 020、021、025、026 |
| Gate-RT-08 | TODO 补强 | checkpoint replay 与 resume compatibility 通过 | 024、028 |
| Gate-RT-09 | 9.1 compatibility | profile compatibility 通过 | 029 |
| Gate-RT-10 | RT-GATE-02 | timeout、budget、dependency unavailable、health degrade 可安全收敛 | 022、023、030 |
| Gate-RT-11 | TODO 补强 | true cross-module unary integration 通过，或在 RT-BLK-01 未解时明确保持 Blocked | 027 |
| Gate-RT-12 | 成熟子系统证据基线 | runtime-local gate、true integration gate、blocker 状态、残余风险均已分开回写 | 031 |

## 8. 阻塞项与解阻条件

| Blocker ID | 对应设计 Blocker | 阻塞项 | 当前影响 | 解阻条件 | 回退策略 |
|---|---|---|---|---|---|
| RT-BLK-01 | RT-BLK-001 | memory、tools、knowledge runtime-facing public interface 未全部落位 | true unary integration、true-port session persist、dependency unavailable 路径不能直接用真实端口验证 | 完成 RT-TODO-003，或相邻模块提供 public interface 与 fail-closed stub | 解阻前只验证 runtime 自身逻辑，不宣称真端口 ready |
| RT-BLK-02 | RT-BLK-002 | llm streaming supporting objects 未冻结 | streaming 正式接线不可作为阶段 J 门禁 | 由 contracts owner 完成 supporting-object 收口 | 明确排除 streaming |
| RT-BLK-03 | RT-BLK-003 | multi_agent 阶段顺序未开始 | 多 Agent 协同闭环不能作为前置完成条件 | 阶段 L 再进入 | 本专项只保留扩展点 |
| RT-BLK-04 | 详设 8.3 隐含 | `AgentFacade` / `SessionManager` supporting types 未成表 | `IAgent`、`ISessionManager` 只能停在类级 | 完成 RT-TODO-001、002 | 解阻前只做 L2 级 public surface |
| RT-BLK-05 | 详设 8.3 隐含 | `RuntimeDependencySet` 与邻接端口 seam 不唯一 | orchestrator 与 fixture gate 容易出现假 ready | 完成 RT-TODO-003 | 解阻前不做 runtime-local gate |
| RT-BLK-06 | RT-BLK-004 | runtime integration topology 与 fixture 仍为空 | `ctest -N` 找不到 runtime integration 或 fixture tests，且旧 smoke 容易被误用 | 完成 RT-TODO-004、025 | 解阻前保留旧 smoke 仅作为 build-liveness，不得作为 Gate 证据 |

### 8.1 Blocker 校准记录

| Blocker ID | 校准时间 | 校准结果 | 剩余阻塞范围 | 备注 |
|---|---|---|---|---|
| RT-BLK-01 | — | — | — | — |
| RT-BLK-02 | — | — | — | — |
| RT-BLK-03 | — | — | — | — |
| RT-BLK-04 | — | — | — | — |
| RT-BLK-05 | — | — | — | — |
| RT-BLK-06 | — | — | — | — |

## 9. 验收与质量门

### 9.1 测试矩阵

| 测试层 | 关键用例 | 通过标准 |
|---|---|---|
| 单元测试 | `RuntimeControlPlaneSurfaceTest`、`RuntimeErrorCodeTest`、`CancellationTokenTest`、`AgentFsmTest`、`BudgetControllerTest`、`CheckpointManagerTest`、`RecoveryManagerTest`、`SchedulerTest`、`TransitionGuardTableTest`、`CheckpointStateMapperTest`、`SessionTypeSurfaceTest`、`SessionManagerTest`、`SafeModeControllerTest`、`AgentOrchestratorSkeletonTest`、`AgentOrchestratorControllerAssemblyTest`、`RuntimeTelemetryBridgeTest`、`RuntimeEventBusTest`、`RuntimeHealthProbeTest`、`RuntimeBackgroundMaintenanceHookTest` | 每个控制器至少有 1 条成功路径和 1 条拒绝、异常或降级路径 |
| 契约回归 | `RuntimeBudgetContractTest`、`CheckpointFieldContractTest`、`ReflectionDecisionContractTest`、`RecoveryRequestContractTest`、`RecoveryOutcomeContractTest`、`RuntimeCheckpointReplayCompatibilityTest` | runtime 新实现不破坏 shared contracts，checkpoint fixture 兼容规则可验证 |
| runtime-local fixture integration | `RuntimeUnaryFixtureIntegrationTest` | 证明 runtime 自身控制平面闭环成立，不外推真端口联调 ready |
| true cross-module integration | `RuntimeUnaryIntegrationTest`、`MainFlowContractE2ETest` | 仅在相邻模块 public interface 落位后通过；未解阻时必须显式保持 Blocked |
| replay 与 resume compatibility | `RuntimeResumeIntegrationTest`、`RuntimeCheckpointReplayRegressionTest` | waiting resume、schema reject、replay regression 都可二值判定 |
| failure injection / safe mode / health maintenance | `RuntimeSafeModeIntegrationTest`、`RuntimeHealthMaintenanceIntegrationTest` | timeout、budget、dependency unavailable、health degrade、maintenance hook 都能进入可审计终态 |
| concurrency / cancellation | `CancellationTokenTest`、`SchedulerTest`、`RuntimeEventBusTest` | cancel、queue overflow、lock-order stress 无死锁、无卡死 |
| profile compatibility | `RuntimeProfileCompatibilityTest` | 至少三档 profile 的 budget、degrade、enablement 差异明确 |
| discoverability | `ctest -N` | runtime unit、integration、fixture 用例都能被顶层发现 |
| 证据回写 | 专项 TODO、deliverables、worklog | 每个 Gate 都有命令证据、subsystem-local 结论和 cross-module blocker 状态 |

### 9.2 统一验收命令建议

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_runtime dasall_unit_tests dasall_contract_tests dasall_integration_tests && \
ctest --test-dir build-ci -N && \
ctest --test-dir build-ci -R "(RuntimeControlPlaneSurfaceTest|RuntimeErrorCodeTest|CancellationTokenTest|AgentFsmTest|BudgetControllerTest|CheckpointManagerTest|RecoveryManagerTest|SchedulerTest|TransitionGuardTableTest|CheckpointStateMapperTest|SessionTypeSurfaceTest|SessionManagerTest|SafeModeControllerTest|AgentOrchestratorSkeletonTest|AgentOrchestratorControllerAssemblyTest|RuntimeTelemetryBridgeTest|RuntimeEventBusTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest|RuntimeBudgetContractTest|CheckpointFieldContractTest|ReflectionDecisionContractTest|RecoveryRequestContractTest|RecoveryOutcomeContractTest|RuntimeCheckpointReplayCompatibilityTest|RuntimeUnaryFixtureIntegrationTest|RuntimeUnaryIntegrationTest|RuntimeResumeIntegrationTest|RuntimeCheckpointReplayRegressionTest|RuntimeProfileCompatibilityTest|RuntimeSafeModeIntegrationTest|RuntimeHealthMaintenanceIntegrationTest|MainFlowContractE2ETest)" --output-on-failure
```

### 9.3 质量门清单

| Gate | 通过条件 |
|---|---|
| Gate-RT-01 | supporting types、seam、topology 口径统一 |
| Gate-RT-02 | runtime public surface 摆脱 placeholder-only，且旧 smoke 不再承担 Gate 语义 |
| Gate-RT-03 | FSM 守卫表与 checkpoint state 映射一致 |
| Gate-RT-04 | budget、checkpoint、recovery、session primitive 全绿 |
| Gate-RT-05 | cancel、backpressure、lock-order 语义全绿 |
| Gate-RT-06 | telemetry、health、background maintenance 可验证 |
| Gate-RT-07 | runtime-local unary fixture gate 通过 |
| Gate-RT-08 | replay 与 resume compatibility 通过 |
| Gate-RT-09 | profile compatibility 通过 |
| Gate-RT-10 | safe mode 与 failure injection gate 通过 |
| Gate-RT-11 | true cross-module unary gate 通过，或显式保持 Blocked |
| Gate-RT-12 | 证据与 blocker 分层回写齐备 |

### 9.4 Gate 执行证据表

| Gate ID | 执行时间 | 执行命令 | 执行结果 | subsystem-local 结论 | cross-module blocker | 后继动作 |
|---|---|---|---|---|---|---|
| Gate-RT-01 | — | — | — | — | — | — |
| Gate-RT-02 | — | — | — | — | — | — |
| Gate-RT-03 | — | — | — | — | — | — |
| Gate-RT-04 | — | — | — | — | — | — |
| Gate-RT-05 | — | — | — | — | — | — |
| Gate-RT-06 | — | — | — | — | — | — |
| Gate-RT-07 | — | — | — | — | — | — |
| Gate-RT-08 | — | — | — | — | — | — |
| Gate-RT-09 | — | — | — | — | — | — |
| Gate-RT-10 | — | — | — | — | — | — |
| Gate-RT-11 | — | — | — | — | — | — |
| Gate-RT-12 | — | — | — | — | — | — |

### 9.5 Gate 写回规则

1. Gate-RT-07 通过只能证明 runtime-local fixture 闭环成立，不能替代 Gate-RT-11。
2. 若 RT-BLK-01 未解，Gate-RT-11 的状态必须写为 Blocked，不得写为 Pass。
3. `dasall_runtime_smoke_test` 与当前 `RuntimeSmokeTest.cpp` 只能出现在 build-liveness 记录里，不能写入任何 Gate 通过证据。

## 10. 风险与回退策略

### 10.1 风险表

| Risk ID | 风险 | 影响 | 缓解动作 |
|---|---|---|---|
| RT-R01 | runtime 被写成 God Object | 测试不可拆、边界漂移 | 严格按 014 ~ 023 的组件粒度推进；020/021 先骨架后集成 |
| RT-R02 | runtime 直连下游实现 | profile 裁剪与 mock 失效 | 所有跨模块任务先过 RT-TODO-003 |
| RT-R03 | 过早推动 streaming 或 supporting objects admission | shared ABI 返工 | 本专项显式排除 streaming |
| RT-R04 | checkpoint 语义与实现脱节 | resume 不可靠 | 先过 012、013、016、024、028 |
| RT-R05 | 锁顺序或背压策略未按设计落地 | 死锁、主循环卡死 | 019、030 必须覆盖并发路径；RT-TC015 锁顺序声明 |
| RT-R06 | incompatible schema 被静默 resume | 错误状态回放 | 016、024、028 强制验证 reject 路径；RT-TC016 版本标签写入 |
| RT-R07 | 重试未复用幂等令牌 | 重复副作用 | 017 强制把 `retry_idempotency_token` 入 admission；RT-TC014 |
| RT-R08 | 把旧 RuntimeSmokeTest 误当主控闭环证据 | 提前宣布 Stage J ready | 025、031 显式标注旧 smoke 非 Gate 证据 |
| RT-R09 | 相邻模块 public interface 延迟落位 | true integration readiness 被高估 | 保留 RT-BLK-01，先用 stub 或 fixture 验证 runtime 自身逻辑 |
| RT-R10 | 编排器前置依赖过宽导致串行过长 | 延迟 unary gate | 拆分为 020（骨架）和 021（runtime-local 全集成），再单独推进 027 |
| RT-R11 | Telemetry sink 故障吞没主路径错误 | 主循环失败被静默 | 023 要求 TelemetryBridge、EventBus、HealthProbe 有独立单测 |
| RT-R12 | runtime-local fixture gate 与 true integration gate 混写 | 形成假阳性与错误里程碑判断 | 026、027、031 必须分层设计、分层记录 |
| RT-R13 | health、watchdog、background maintenance 未独立建模 | 依赖退化时无提前预警，safe mode 触发滞后 | 023、030 把 health 与 maintenance 升格为显式任务和 Gate |
| RT-R14 | replay gate 只看 version tag 不看 replay regression | 跨版本或重构后出现静默语义漂移 | 024、028 增加 golden checkpoint fixture 与 replay regression |
| RT-R15 | profile 差异未被显式验证 | budget、degrade、enablement 在不同 profile 下漂移 | 029 单列 RuntimeProfileCompatibilityTest |

### 10.2 回退策略

1. 相邻模块 public interface 未按期稳定时，优先使用 fail-closed stub、null adapter、fixture，不直连实现。
2. checkpoint 或 resume 一轮内无法闭合时，先保证 `FailedSafe + audit + persist` 成立，再补 `load()` 与 `make_resume_plan()`。
3. supporting object admission 引发 contracts 不确定时，回退为 module-local handoff，不推动 shared ABI。
4. integration gate 长时间被 topology 或 fixture 阻塞时，保留旧 smoke 仅作为构建存活信号，但不能作为 Stage J Gate 证据。
5. true integration 长时间被 RT-BLK-01 阻塞时，专项状态冻结在 subsystem-local ready，显式保留 Gate-RT-11 为 Blocked。
6. health 或 background maintenance 路径不稳定时，先关闭 idle maintenance 自动触发，只保留 on-demand health snapshot 与 safe mode 入口，不关闭审计与告警字段。

## 11. 可行性结论

### 11.1 是否可直接进入执行

可以，但不能跳过 RT-TODO-001 ~ 004，也不能用 RT-TODO-026 代替 RT-TODO-027。

### 11.2 当前可落到的最细粒度

1. L3：`RuntimeErrorCode`、`CancellationToken`、`IAgentFsm`、`StateTransitionTypes`、`IBudgetController`、`CheckpointStateMapper`、`TransitionGuardTable`，以及 frozen contracts 的消费口径。
2. L2：`AgentFacade`、`ISessionManager` / `SessionManager`、`ICheckpointManager` / `CheckpointManager`、`IRecoveryManager` / `RecoveryManager`、`IScheduler` / `Scheduler`、`AgentOrchestrator`（骨架 + runtime-local 全集成）、`SafeModeController`、`RuntimeHealthProbe`、`RuntimeProfileCompatibilityTest` 对应的 fixture 组合层。
3. L1 / Blocked：`RuntimeTelemetryBridge`、`RuntimeEventBus`、`BackgroundMaintenanceHooks`、`RuntimeDependencySet` 的私有 helper 细节，以及依赖相邻模块 public interface 的 true integration 路径。

### 11.3 后续建议

1. 先执行 RT-TODO-001 ~ 004，冻结 supporting types、port seam、runtime-local fixture matrix 与 test topology。
2. 再执行 RT-TODO-005 ~ 024，把 runtime 从 placeholder-only 收敛为可编译、可测、具备 replay compatibility 约束的控制平面骨架与控制器集合。
3. 执行 RT-TODO-025、026、028、029、030，先把 runtime-local fixture gate、resume/replay、profile、safe mode 与 health gate 跑通。
4. 仅在 RT-BLK-01 解阻后执行 RT-TODO-027；027 未通过前，不得把专项状态写成全链 ready。
5. 最后执行 RT-TODO-031，分开回写 runtime-local 结论、true integration 结论、残余 blocker 与后续动作。

## 12. 未决问题处置表

> 来源：runtime 详细设计 §12 未决问题清单。对每个未决问题给出明确处置方式与关联任务。

| OQ ID | 来源 | 问题摘要 | 处置方式 | 关联 TODO | 说明 |
|---|---|---|---|---|---|
| RT-OQ-01 | 详设 §12 OQ-1 | multi_agent 编排协议尚未稳定 | 本专项排除 | 无 | RT-BLK-03 覆盖；阶段 L 再进入 |
| RT-OQ-02 | 详设 §12 OQ-2 | streaming supporting objects（ChunkEnvelope 等）冻结时间不确定 | 本专项排除 | 无 | RT-BLK-02 覆盖；Stage J 不含 streaming Gate |
| RT-OQ-03 | 详设 §12 OQ-3 | checkpoint schema 版本升级策略未最终确定 | 本专项 partial 覆盖 | RT-TODO-016、024、028 | version tag 写入、compatibility contract 与 replay regression 已纳入；跨大版本迁移留后续 |
| RT-OQ-04 | 详设 §12 OQ-4 | 相邻模块 runtime-facing public interface 落位时间不确定 | 本专项 mitigate | RT-TODO-003、026、027、031 | fail-closed stub + seam 唯一原则；RT-BLK-01 持续追踪；fixture gate 与 true integration gate 分开记录 |
| RT-OQ-05 | 详设 §12 OQ-5 | recovery retry 超限后的人工干预接口未定义 | 本专项 partial 覆盖 | RT-TODO-017 | retry admission 含 max_retry 上限；超限后进入 FailedSafe -> audit；人工干预 API 留后续 |
| RT-OQ-06 | 详设 6.23 / 8.2 J4 | health probe 与 background maintenance 的默认 cadence 尚未冻结 | 本专项 partial 覆盖 | RT-TODO-023、030 | 首版先固化字段、non-blocking 行为与 Gate；具体 cadence 后续由 profile 与 ops_policy 收口 |
