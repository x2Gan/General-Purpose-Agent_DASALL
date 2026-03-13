# ADR-008 AgentOrchestrator 与 MultiAgentCoordinator 职责划分

## 0. 文档信息

### 0.1 文档定位

本文档用于裁定 DASALL Agent 中 AgentOrchestrator 与 MultiAgentCoordinator 的职责边界、控制权归属、输入输出契约和调用关系。

该决策属于架构级边界决策，会直接影响以下内容：

1. runtime 与 multi_agent 模块的依赖方向。
2. 多 Agent 模式下谁拥有全局任务生命周期与最终输出权。
3. SubTask、WorkerTask、AgentRegistry、ResultMerger、租约与预算约束的归属。
4. 后续 contracts、runtime、multi_agent 模块的接口冻结方式。

### 0.2 背景

当前设计文档已经同时定义了两类能力：

1. Runtime 子系统中的 AgentOrchestrator，负责驱动 Agent 主循环、控制认知/工具/记忆/知识调用顺序，并拥有多 Agent 协调的调度权。
2. Multi-Agent 架构中的 MultiAgentCoordinator，负责子任务拆分、Worker 匹配、结果汇聚、冲突仲裁和失败回收。

但现有材料里仍然存在一处典型边界模糊：

1. 架构文档多次写明 Multi-Agent Coordinator 的调度职责归属 Agent Control Plane。
2. 工程蓝图又把 MultiAgentCoordinator 作为 multi_agent 模块的关键组件列出。
3. 当前描述如果不冻结，后续很容易让 MultiAgentCoordinator 演化成第二个顶层 orchestrator，和 AgentOrchestrator 争夺主循环、预算、状态机与最终输出控制权。

如果不先裁定这条边界，后续很容易出现两个问题：

1. runtime 为了“统一主控”把 Worker 匹配、租约、结果合并、冲突仲裁全部写进 AgentOrchestrator，导致主控过重。
2. multi_agent 为了“更完整协同”把用户交互、全局 checkpoint、主状态机推进和最终回复也纳入 MultiAgentCoordinator，形成双主控点。

---

## 1. 问题陈述

需要解决的问题不是“两个组件都参与调度是否可以”，而是以下四个更根本的问题：

1. 谁拥有整个请求的全局生命周期控制权。
2. 谁拥有多 Agent 子任务图的局部编排权。
3. 谁决定何时进入或退出多 Agent 模式。
4. 当 Worker 协同约束与全局预算、状态机、用户交互发生冲突时，谁拥有最终裁定权。

这四个问题如果没有清晰答案，后续 runtime 与 multi_agent 详细设计一定会出现层次穿透和接口漂移。

---

## 2. 现有设计理解

### 2.1 当前仓库内的设计信号

基于现有架构文档与工程蓝图，可以得出以下共识：

1. Agent Control Plane 负责 Session、Task、状态机、预算和调度，并承载 Multi-Agent Coordinator 的调度职责。
2. Multi-Agent Workers 运行在 Execution & Collaboration Layer，由 Agent Registry 管理，只接受来自 Agent Control Plane 中 Multi-Agent Coordinator 的调度指令。
3. 架构文档明确写明 Multi-Agent Coordinator 的调度逻辑由 Agent Control Plane 驱动，Worker Agent 实例作为能力实体运行在另一层，两者不在同一层。
4. Multi-Agent 架构章节明确写明 MultiAgentCoordinator 运行于 Runtime，负责拆分任务、匹配 Worker、汇聚结果并保证 Worker 不越过主 Runtime 的预算和权限边界。
5. 多 Agent 协同时序图显示 Orchestrator 面向用户、查询 Agent Registry、下发 SubTask、调用 Result Merger，并最终返回 Final Response。
6. 工程蓝图同时把 MultiAgentCoordinator、AgentRegistry、ResultMerger、WorkerAgent 列为 multi_agent 模块关键组件，并注明“调度指令由 runtime 中的 AgentOrchestrator 发出”。

这说明当前总体方向已经隐含了一个正确趋势：

1. AgentOrchestrator 更接近“全局主控与主链路拥有者”。
2. MultiAgentCoordinator 更接近“多 Agent 协同子域控制器”。

问题在于这一点还没有被显式冻结，尤其是“模块实现落点”与“控制权归属”还容易被误读成一回事。

### 2.2 业界常见做法与可借鉴结论

结合当前主流 Agent 系统设计资料与可靠性架构模式，可以提炼出四条稳定结论：

1. Anthropic 在 orchestrator-workers 模式中明确把 central orchestrator 定义为负责动态拆解任务、委派 worker 并综合结果的中心控制点，worker 不是第二个顶层主控，而是执行专门子任务的受控角色。
2. OpenAI 当前 Agents 指南把 logic nodes、tools、knowledge、guardrails 视为可组合原语，强调控制流逻辑与具体 agent/tool 能力分离，说明主流程控制不应被具体能力组件吞并。
3. Azure Scheduler-Agent-Supervisor 模式里，Scheduler 拥有整体工作流状态、顺序和状态持久化；Agent 负责封装步骤执行；Supervisor 负责超时与恢复。它强调“步骤执行者不能拥有全局流程”，这与 DASALL 中 Worker/Coordinator 不能取代全局 Orchestrator 的原则一致。
4. 工业级多 agent 或分布式工作流设计通常把“总任务拥有权”与“局部子图编排权”分开，以避免主控过重或协同子系统越权。

这些结论与 DASALL 当前的七层分层和 runtime/multi_agent 分模块设计是一致的。

---

## 3. 决策

### 3.1 总体决策

AgentOrchestrator 与 MultiAgentCoordinator 必须严格分层，分别承担“全局任务主控”和“多 Agent 协同子域编排”两类职责。MultiAgentCoordinator 不是第二个顶层 orchestrator，而是由 AgentOrchestrator 调用、受其约束的协同控制组件。

### 3.2 AgentOrchestrator 的职责边界

AgentOrchestrator 归属 runtime 子系统，负责整个请求的全局生命周期管理，而不是深入承担每一个 Worker 子任务的局部协同细节。

它的职责固定为：

1. 接收 AgentRequest，拥有从 Receiving 到 Completed 的主状态机推进权。
2. 管理 Session、Checkpoint、RuntimeBudget、Profile、用户澄清/确认等待态和最终 AgentResult 生成时机。
3. 决定本轮任务是走单 Agent 路径，还是进入多 Agent 协同路径。
4. 在进入多 Agent 路径时，向 MultiAgentCoordinator 提供目标片段、计划片段、预算边界、权限边界和退出条件。
5. 接收 MultiAgentCoordinator 返回的协同结果，把它统一折叠为 Observation、Plan 更新或最终回复材料。
6. 在协同失败时，结合 RecoveryManager、Planner、ReflectionEngine 做全局 replan、abort_safe、degrade 或回退到单 Agent 路径的裁定。
7. 保持所有用户面交互唯一出口，不允许 Worker 或 MultiAgentCoordinator 直接对用户形成并提交最终响应。

AgentOrchestrator 明确不负责：

1. 直接维护 AgentRegistry 的匹配策略细节。
2. 直接管理 Worker 租约表、子任务并发窗口、Result Merger 合并算法和等价任务去重细节。
3. 在主循环内部写死具体 worker 路由、能力评分和冲突合并算法。
4. 把自己膨胀成集任务主控、协同引擎、结果合并器于一体的巨型 God Object。

### 3.3 MultiAgentCoordinator 的职责边界

MultiAgentCoordinator 是多 Agent 协同子域控制器。它可以实现于 multi_agent 模块，但其控制权受 runtime 中的 AgentOrchestrator 约束，不拥有顶层主循环。

它的职责固定为：

1. 接收 AgentOrchestrator 提供的 Goal/Plan 片段、协同策略和预算边界，生成或细化可执行的 SubTask 图。
2. 调用 AgentRegistry 进行 Worker 能力匹配，综合 capability、cost_class、max_concurrency、permission_domain 选择候选 Worker。
3. 为 Worker 分配租约、allowed_tools、上下文窗口和超时边界，并发起子任务调度。
4. 维护子任务图状态、Worker 租约状态、等价任务去重和中间结果收集。
5. 调用 ResultMerger 合并 Worker 输出，保留 conflicts、来源可信度和验证意见。
6. 在多 Agent 子域内执行失败回收策略，例如 reschedule、skip、局部失败升级报告。
7. 将协同结果以结构化返回值回传给 AgentOrchestrator，而不是自行决定整个请求完成。

MultiAgentCoordinator 明确不负责：

1. 直接接收用户请求或对外暴露最终 Agent 接口。
2. 拥有全局 Session 生命周期、主 FSM、总 checkpoint、总预算和最终 AgentResult 提交权。
3. 绕过 AgentOrchestrator 直接调用 apps 层、直接与用户澄清或确认。
4. 自行决定整个请求是否进入 SafeMode、FailedSafe、Completed 等顶层运行态。
5. 代替 Planner、Reasoner 或 ReflectionEngine 完成通用认知主链路。

### 3.4 控制权与实现落点的裁定

为避免“模块在哪”与“控制权归谁”再次混淆，必须同时明确以下两点：

1. AgentOrchestrator 拥有全局调度权、模式切换权和最终裁定权。
2. MultiAgentCoordinator 可以作为 multi_agent 模块中的实现组件存在，但它只拥有多 Agent 子任务图的局部编排权。
3. runtime 通过接口依赖协同能力，multi_agent 提供实现与 Worker 能力域，这两者不是冲突关系。
4. 只要存在用户请求、主循环、全局预算、顶层 checkpoint 和最终响应，拥有权始终在 AgentOrchestrator。

---

## 4. 调用顺序与责任链

本决策固定以下多 Agent 责任链：

1. AgentOrchestrator 在主循环中判断当前任务是否需要多 Agent 协同。
2. 若需要，则向 MultiAgentCoordinator 发起协同请求，明确 goal_fragment、plan_fragment、budget_guard、permission_guard、stop_conditions。
3. MultiAgentCoordinator 查询 AgentRegistry，生成 SubTask 图并派发 Worker。
4. Worker 执行子任务并回传结构化结果或失败 Observation。
5. MultiAgentCoordinator 调用 ResultMerger 合并结果，并将 merged_result、conflicts、worker_trace、failure_summary 回传 AgentOrchestrator。
6. AgentOrchestrator 再决定继续 Reasoning、进入 ResponseBuilder、触发 replan、降级或失败收敛。

这条责任链意味着：

1. 多 Agent 协同是主循环中的一个受控阶段，不是独立于主循环的第二套系统入口。
2. MultiAgentCoordinator 的返回值是“协同结果”，不是“最终产品结果”。
3. Worker 的结果必须先经过协同层和主控层折叠，才能进入用户面输出。

---

## 5. 契约层面的直接影响

本决策会直接影响 contracts 的定义，需同步冻结以下约束：

### 5.1 MultiAgentRequest 只表达协同子域请求，不表达全局请求

MultiAgentCoordinator 的输入对象不应复用 AgentRequest。它应是一个协同请求对象，至少包含：

1. parent_request_id
2. parent_task_id
3. goal_fragment 或 subgoal
4. plan_fragment
5. collaboration_mode
6. worker_budget_guard
7. permission_guard
8. stop_conditions

### 5.2 MultiAgentResult 只表达协同结果，不表达最终 AgentResult

MultiAgentCoordinator 的输出对象不应直接等于 AgentResult。它至少包含：

1. subtask_results
2. merged_result
3. conflicts
4. worker_trace_refs
5. failure_summary
6. recommended_next_action

### 5.3 WorkerTask 与全局任务状态必须分层

WorkerTask 只描述子任务执行单元，不得反向承载全局 Session/FSM 语义。至少应明确：

1. task_id 与 parent_task_id
2. lease_id
3. worker_type
4. allowed_tools
5. timeout
6. idempotency_key

### 5.4 顶层 checkpoint 与子任务快照必须分层

1. AgentOrchestrator 拥有顶层 checkpoint 写入和恢复入口。
2. MultiAgentCoordinator 只维护子任务图和 worker 租约的局部快照，并将其纳入顶层 checkpoint 的子域数据。
3. 不允许 MultiAgentCoordinator 自己成为新的 resume 入口。

---

## 6. 备选方案与取舍

### 方案 A：由 AgentOrchestrator 同时负责全局主控和全部多 Agent 协同细节

不采纳。

原因：

1. 会让 AgentOrchestrator 同时承担主循环、会话、预算、协同拆分、租约、合并和冲突仲裁，复杂度过高。
2. 不利于后续替换不同协同模式或能力匹配算法。
3. 运行主控与协同子域无法独立演进和测试。

### 方案 B：由 MultiAgentCoordinator 升级为第二个顶层 Orchestrator

不采纳。

原因：

1. 会形成双主控点，导致主状态机、checkpoint、预算和最终响应权重复。
2. Worker 路径与单 Agent 路径的边界会被打散，用户面交互出口不再唯一。
3. 会让 multi_agent 越权侵入 runtime 的核心职责。

### 方案 C：AgentOrchestrator 负责全局主控，MultiAgentCoordinator 负责协同子域编排

采纳。

原因：

1. 符合 DASALL 现有文档中“调度权归 Layer 6、Worker 实例归 Layer 4”的分层信号。
2. 符合主流 orchestrator-workers 模式中“中心控制点 + 专门 worker”的工程惯例。
3. 能把控制权归属与模块实现落点解耦，减少后续接口返工。

---

## 7. 影响与后续动作

### 7.1 直接影响

1. runtime 详细设计需要冻结 AgentOrchestrator 到 IMultiAgentCoordinator 的调用接口。
2. multi_agent 详细设计需要冻结 AgentRegistry、ResultMerger、WorkerLease、SubTaskGraph 的职责边界。
3. contracts 需要补充 MultiAgentRequest、MultiAgentResult、WorkerTask、WorkerLease 等对象定义。
4. tests 需要覆盖“单 Agent 路径”和“主控触发多 Agent 子域”的双路径行为。

### 7.2 实施约束

后续实现必须满足以下约束：

1. 只有 AgentOrchestrator 可以决定是否进入多 Agent 模式。
2. 只有 AgentOrchestrator 可以提交最终 AgentResult 并控制用户面交互。
3. MultiAgentCoordinator 的所有调度动作都必须受全局预算、权限和 profile 限制。
4. Worker 的任何失败升级到全局 replan 或 abort_safe 时，都必须回到 AgentOrchestrator 做最终裁定。
5. 不允许 Worker 或 MultiAgentCoordinator 绕过主控直接写最终会话结论。

### 7.3 后续文档建议

建议紧接本 ADR 继续补充以下内容：

1. contracts 详细定义 MultiAgentRequest、MultiAgentResult、WorkerTask、WorkerLease。
2. runtime 详细设计中的多 Agent 状态迁移、等待态与 checkpoint 子域结构。
3. multi_agent 详细设计中的 AgentRegistry 匹配规则、ResultMerger 冲突合并规则与租约回收策略。

---

## 8. 最终裁定

本轮正式裁定如下：

1. AgentOrchestrator 是整个请求的全局主控，拥有主循环、主状态机、全局预算、顶层 checkpoint、用户交互和最终输出控制权。
2. MultiAgentCoordinator 是受 AgentOrchestrator 调用的多 Agent 协同子域控制器，拥有子任务图、Worker 匹配、租约、结果合并和局部失败回收的编排权。
3. MultiAgentCoordinator 可以落在 multi_agent 模块实现，但这不改变全局控制权归属于 runtime 的事实。
4. 该边界应在 contracts、runtime、multi_agent 详细设计之前冻结。

Status：Accepted