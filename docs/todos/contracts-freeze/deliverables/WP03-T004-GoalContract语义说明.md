# WP03-T004 GoalContract 语义说明

最近更新时间：2026-03-17
任务状态：Done
任务编号：WP03-T004
上游输入：WP03-T002 AgentRequest 语义说明、WP03-T003 AgentRequest 字段表、架构文档 3.8.1、ADR-006/007/008

## 1. 任务理解

本任务只处理 WP03-T004：定义 GoalContract 的职责边界。

本任务不处理：

1. GoalContract 字段必填/可选详细表（WP03-T005）。
2. Plan / PlanGraph 结构（Planner 子域）。
3. Observation / ObservationDigest 结构（WP03-T006/T007/T008）。
4. 多 Agent 子目标拆分实现（MultiAgentCoordinator 子域）。

## 2. 约束与边界

### 2.1 直接约束（可追溯）

1. 架构 3.8.1：GoalContract 冻结任务目标、成功判据、约束、预算和审批策略，避免后续模块从自然语言中反复猜测。
2. 架构 5.8.4：CognitionStepRequest 包含 GoalContract，Planner 和 Reasoner 直接消费。
3. 架构 7.3.3：IPlanner::build_plan(const GoalContract& goal, const ContextPacket& context)。
4. 架构 11.1：GoalContract 由 extract_goal(request) 从 AgentRequest 提取。
5. ADR-006：ContextPacket 消费 GoalContract 摘要，GoalContract 不与消息装配混淆。
6. ADR-007：审批策略仅预声明，恢复执行细节归 RecoveryManager。
7. ADR-008：目标分解在单 Agent 范围内由 Planner 处理；多 Agent 分层归 MultiAgentCoordinator。
8. T002-D 冻结：AgentRequest.goal_hint 不替代 GoalContract；AgentRequest.constraint_set 为声明式约束输入。
9. T003-D 冻结：approval_policy_hint 最终策略由后续对象裁定——即 GoalContract。
10. WP02-T007：RuntimeBudget 5 维度已冻结，GoalContract 复用不扩展。
11. WP02-T012：所有枚举必须包含显式 Unspecified 哨兵值。

### 2.2 边界与非目标

边界：

1. 仅定义 GoalContract 对象级职责边界与最小字段语义。
2. 仅定义单 Agent 范围内的目标语义，不引入 MultiAgentRequest/WorkerTask 字段。
3. 仅冻结字段语义与分组，不冻结具体序列化格式。

非目标：

1. 不定义 Plan/PlanGraph 的步骤结构。
2. 不引入运行态字段：fsm_state、retry_counters、checkpoint_ref。
3. 不引入 provider 私有字段。
4. 不定义校验器实现代码（归 T004-B）。

### 2.3 前置依赖检查

1. T002-D/B 已 Done：AgentRequest 语义边界冻结，goal_hint/constraint_set/approval_policy_hint 已定义。
2. T003-D/B 已 Done：AgentRequest 字段合法性校验已实现。
3. WP02 冻结包 Done：RuntimeBudget、枚举规则、时间语义已冻结。
4. 当前无阻塞项。

## 3. GoalContract 五职责语义定义

基于架构 3.8.1 冻结要求，GoalContract 承载以下五项且仅以下五项职责：

| 职责 | 语义说明 | 消费者 | 不承载 |
|---|---|---|---|
| 目标描述 | 从 goal_hint + 上下文中具体化的目标自然语言描述 | Planner、Reasoner | 不含执行步骤 |
| 成功判据 | 可测度的目标完成判定条件 | Reasoner、ReflectionEngine | 不含自然语言段落 |
| 约束集合 | 安全/策略/权限等不可违反的边界条件 | Planner、ToolManager | 不含执行状态 |
| 预算声明 | 目标级预算上限（复用 RuntimeBudget） | RuntimeEngine、ContextOrchestrator | 不扩展新维度 |
| 审批策略 | 任务类决策是否需要人工确认的预声明 | AgentOrchestrator、RecoveryManager | 不含恢复细节 |

## 4. 最终产出

### 4.1 GoalContract 必填字段

| 字段名 | 语义说明 | 主要依据 |
|---|---|---|
| goal_id | 目标级唯一标识，全链路透传 | WP02-T009 标识规则 |
| request_id | 关联入口请求标识 | AgentRequest.request_id |
| goal_description | 具体化的目标描述文本 | 架构 3.8.1 目标职责 |
| success_criteria | 可测度的成功判据（结构化字符串） | 架构 3.8.1 成功判据职责 |
| status | 目标当前生命周期状态 | GoalStatus 枚举 |
| created_at | 目标创建时间戳（毫秒） | WP02-T010 时间语义 |

### 4.2 GoalContract 可选字段

| 字段名 | 语义说明 | 约束 |
|---|---|---|
| constraints | 安全/策略/权限声明式约束集合 | 从 AgentRequest.constraint_set 继承或具体化 |
| approval_policy | 审批策略预声明 | ApprovalPolicy 枚举，不含恢复细节 |
| budget_override | 目标级预算覆盖（RuntimeBudget 类型） | 若存在则覆盖 AgentRequest 层预算 |
| priority | 目标优先级提示 | 从 AgentRequest.priority 继承 |
| parent_goal_id | 父目标标识（多 Agent 场景） | 单 Agent 时为 nullopt |
| deadline_at | 目标级硬截止时间戳（毫秒） | 从 AgentRequest.deadline_at 继承 |
| tags | 检索/审计标签 | 不承载执行控制信号 |

### 4.3 GoalContract 禁止字段（守卫边界）

以下字段或同类语义不得进入 GoalContract：

1. Plan/执行步骤字段：plan_graph、step_list、action_sequence、tool_sequence。
2. Runtime 内部状态：fsm_state、retry_counters、backoff_ms、circuit_state、checkpoint_ref。
3. Provider 私有字段：rendered_prompt、model_provider_args、vendor_tool_schema。
4. 执行结果字段：observation、observation_digest、belief_state、agent_result。
5. 多 Agent 子域字段：worker_task_list、lease_id、multi_agent_request。
6. 消息装配字段：final_messages、prompt_bundle、token_usage。

### 4.4 枚举定义

#### ApprovalPolicy

| 值 | 语义 | 说明 |
|---|---|---|
| Unspecified (0) | 未指定 | WP02-T012 哨兵值 |
| Auto (1) | 自动执行 | 不需要人工确认 |
| RequireConfirm (2) | 需确认 | 关键决策前需人工审批 |

#### GoalStatus

| 值 | 语义 | 说明 |
|---|---|---|
| Unspecified (0) | 未指定 | WP02-T012 哨兵值 |
| Active (1) | 进行中 | 目标正在被处理 |
| Achieved (2) | 已达成 | 成功判据满足 |
| Failed (3) | 已失败 | 明确无法达成 |
| Cancelled (4) | 已取消 | 外部取消或超时 |

## 5. 与上下游对象的边界关系

| 上游/下游 | 关系 | 数据流向 | 边界约束 |
|---|---|---|---|
| AgentRequest → GoalContract | 输入 | goal_hint → goal_description；constraint_set → constraints；approval_policy_hint → approval_policy | GoalContract 具体化，不是复制 |
| GoalContract → ContextPacket | 摘要消费 | GoalContract 摘要进入 ContextPacket.goal_summary | ContextPacket 不回灌 GoalContract |
| GoalContract → Planner | 一级输入 | build_plan(goal, context) | Planner 只读 GoalContract，不修改 |
| GoalContract → Reasoner | 判定输入 | 评估 Observation 是否满足 success_criteria | Reasoner 不变更 GoalContract |
| GoalContract → AgentOrchestrator | 生命周期管理 | status 状态流转 | 仅 Orchestrator 可变更 status |
| GoalContract ↔ Checkpoint | 恢复语义 | Checkpoint 记录 GoalContract 快照 | GoalContract 不承载恢复执行细节 |

## 6. 方案对比与决策

### 6.1 方案 A：最小必填 + 可选扩展（推荐）

设计方式：仅将闭环必需字段设为必填（goal_id/request_id/goal_description/success_criteria/status/created_at），约束/审批/预算走可选。

优点：与 WP02 兼容规则一致，旧系统可平滑接入。

缺点：部分约束需运行期补足。

### 6.2 方案 B：强约束必填化

设计方式：将 constraints/approval_policy/budget_override 统一设为必填。

优点：对象更完整，运行时分支少。

缺点：与"新增优于修改、可选改必填为高风险"原则冲突。

### 6.3 决策

采用方案 A。取舍理由同 T003：方案 A 更符合 WP-02 兼容与演进规则。

## 7. 验收清单

1. 已定义 GoalContract 五职责语义边界。
2. 已列出必填字段（6 项）与可选字段（7 项）。
3. 已定义 ApprovalPolicy 和 GoalStatus 枚举。
4. 已定义禁止字段清单（6 类禁止）。
5. 已定义与 AgentRequest/ContextPacket/Planner/Reasoner/Checkpoint 的边界关系。
6. 结论可追溯到架构 3.8.1、ADR-006/007/008、T002/T003。

## 8. D Gate 结果

| 检查项 | 结果 |
|---|---|
| 五职责语义是否与架构 3.8.1 对齐 | ✅ |
| 必填字段是否覆盖目标闭环所需 | ✅ 6 项 |
| ApprovalPolicy 是否与 ADR-007 分工清晰 | ✅ |
| GoalStatus 枚举是否含 Unspecified | ✅ WP02-T012 |
| 禁止字段是否阻断 Plan/Runtime/Provider 越界 | ✅ 6 类 |
| 与 AgentRequest 映射关系是否清晰 | ✅ |
| 是否达到进入 -B 条件 | ✅ 通过 |

## 9. 风险与回退

1. 风险：success_criteria 过于宽泛导致 Reasoner 无法程序化判定。
回退：要求 success_criteria 为结构化字符串（JSON 或关键指标集），不允许纯自然语言段落。
2. 风险：GoalContract 与 Plan 边界模糊。
回退：GoalContract 定义"要达成什么"（What），Plan 定义"如何达成"（How），禁止在 GoalContract 中出现步骤描述。
3. 风险：多 Agent 场景下 parent_goal_id 引入级联复杂性。
回退：parent_goal_id 为可选字段，单 Agent 不使用，多 Agent 场景在 WP03-T015 后续处理。

## 10. 下一任务建议

1. WP03-T005-D：列出 GoalContract 必填字段与约束表达详细规则。
2. WP03-T005-B：补齐 GoalContract 字段一致性校验器。
