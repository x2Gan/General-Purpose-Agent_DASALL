# WP03-T009 BeliefState 主链路定位说明

最近更新时间：2026-03-17
任务状态：Done
任务编号：WP03-T009
上游输入：架构 3.8.2、架构 4.3、架构 6.2、ADR-006 §6.1、ADR-007 §3.2、冻结计划 §阶段2、LLM Agent学习 §3.4、T008-D（ObservationDigest 边界说明）

## 1. 任务理解

本任务只处理 WP03-T009：定义 BeliefState 在主链路中的定位与禁入边界。

具体交付物：
1. BeliefState 字段语义与必填/可选分组。
2. BeliefState 禁止字段清单（禁入边界）。
3. BeliefState 的上下游关系与消费者。
4. 守卫校验规则清单（2 层）。

本任务不处理：
1. ObservationDigest 对象级字段（T008 已冻结）。
2. Observation 与 ObservationSource（T006/T007 已冻结）。
3. ContextPacket / Checkpoint / AgentResult（T010 及后续）。
4. BeliefUpdater 实现细节（归 cognition 实现层）。
5. BeliefState 持久化格式或序列化策略。
6. 信念更新算法或冲突消解策略。

## 2. 约束与边界

### 2.1 直接约束（可追溯）

1. 架构 3.8.2：BeliefState 必须显式区分 confirmed_facts、hypotheses、assumptions、evidence_refs、confidence。
2. 架构 4.3：Planner 和 Reasoner 必须读取 GoalContract 与 BeliefState，而不是仅靠最近一轮用户输入做决策。
3. 架构 6.2 步骤6：Planner 和 Reasoner 基于 GoalContract、BeliefState 与 LLM 输出生成动作意图。
4. 架构 6.2 补充说明：步骤 4 到步骤 10 间需显式维护 GoalContract、BeliefState 和 Checkpoint，避免把瞬时推测误当稳定事实。
5. ADR-007 §3.2：ReflectionEngine 读取 Observation、ErrorInfo、当前 PlanGraph、BeliefState、GoalContract 与最近执行轨迹。
6. ADR-006 §6.1 item 10：ContextPacket 应含 belief_state 或等价事实视图。
7. LLM Agent学习 §3.4 映射表：BeliefState = { confirmed_facts, hypotheses, assumptions, evidence_refs, confidence }。
8. 冻结计划 §阶段2：BeliefState 为 8 个主链路优先冻结对象之一（observation/BeliefState）。
9. WP02-T012：所有枚举必须含 Unspecified 哨兵值。
10. WP02-T010：时间字段为毫秒时间戳。
11. WP02-T009：标识字段需入口生成并全链路透传。

### 2.2 边界与非目标

边界：
1. 仅定义 BeliefState 对象级字段语义与禁入边界。
2. 仅冻结 BeliefState 与其他主链路对象的互斥关系。
3. 仅定义守卫校验规则清单。

非目标：
1. 不实现信念更新算法或冲突消解策略。
2. 不定义 BeliefState 的持久化 schema。
3. 不引入 Observation 执行语义字段（payload、error、side_effects、success）。
4. 不引入运行态字段（fsm_state、retry_counters、backoff_ms）。
5. 不引入消息渲染字段（final_messages、rendered_prompt、prompt_bundle）。
6. 不引入 Checkpoint 恢复专有字段（working_memory_snapshot、pending_action）。

### 2.3 前置依赖检查

1. T006-D/B 已 Done：Observation 统一折叠语义冻结。
2. T007-D/B 已 Done：ObservationSource 枚举与引用规则冻结。
3. T008-D/B 已 Done：ObservationDigest 分层边界冻结。
4. WP02 冻结包 Done：枚举规则、时间语义、标识规则已冻结。
5. WP01 MainFlowContracts.h 中 BeliefStateEntry 占位已存在。
6. 当前无阻塞项。

## 3. 研究证据链

### 3.1 本地证据清单

| 编号 | 证据来源 | 关键结论 |
|---|---|---|
| L1 | 架构 3.8.2 | BeliefState 必须含 confirmed_facts/hypotheses/assumptions/evidence_refs/confidence 五字段 |
| L2 | 架构 4.3 | Planner/Reasoner 必须读取 BeliefState |
| L3 | 架构 6.2 步骤6 | Planner/Reasoner 基于 GoalContract、BeliefState、LLM 输出生成动作意图 |
| L4 | 架构 6.2 补充 | 步骤 4-10 间需显式维护 BeliefState、GoalContract、Checkpoint |
| L5 | ADR-007 §3.2 | ReflectionEngine 读取 BeliefState 进行失败归因 |
| L6 | ADR-006 §6.1 item 10 | ContextPacket 含 belief_state 或等价事实视图 |
| L7 | LLM Agent学习 §3.4 | confirmed_facts/hypotheses/assumptions/evidence_refs/confidence 五字段映射 |
| L8 | Blueprint §3.1/§6 | BeliefState 在 contracts/include/observation/ 目录（备注）|
| L9 | 冻结计划 §阶段2 | BeliefState 为主链路 8 优先对象之一 |
| L10 | WP01 MainFlowContracts.h | BeliefStateEntry 占位符已存在 |

### 3.2 外部参考清单

| 编号 | 参考来源 | 关键结论 |
|---|---|---|
| E1 | BDI (Belief-Desire-Intention) Agent Architecture | 经典认知架构：Belief 层显式区分已确认事实/假设/前提，与 Desire(目标)/Intention(计划) 分层 |
| E2 | Reflexion (Shinn & Labash 2023, arXiv:2303.11366) | 动态自反思框架将信念状态与执行记忆分层，支持增量更新和经验学习 |
| E3 | Lilian Weng — LLM Powered Autonomous Agents (2023) | 反思机制将观察合成为高层推理，短期+长期记忆分层 |
| E4 | Park et al. — Generative Agents (2023, arXiv:2304.03442) | 反思机制合成高层推理，显式维护信念层次用于引导后续行为 |

### 3.3 对本任务的可落地启发

1. BeliefState 是 Agent 认知循环的中间产物，位于 ObservationDigest 之后、Checkpoint 之前——将观察摘要合成为结构化认知状态。
2. 五字段结构由架构 3.8.2 冻结，不可增删核心字段。
3. BeliefState 明确不是入口（AgentRequest 负责）、不是恢复快照（Checkpoint 负责）、不是执行记录（Observation 负责）。
4. 需要 request_id 关联回 AgentRequest，确保认知状态可追溯到原始请求。
5. confidence 为整体信念可靠度 [0.0, 1.0]，区别于 ObservationDigest.confidence（摘要保真度）。

## 4. BeliefState 字段语义定义

### 4.1 主链路定位

```
AgentRequest → GoalContract → ContextPacket → Observation → ObservationDigest → [BeliefState] → Checkpoint → AgentResult
```

BeliefState 在主链路中的定位：
1. **上游**：接收 ObservationDigest 提供的压缩摘要与事实。
2. **本职**：将观察摘要、先验知识和推理结论合成为结构化认知状态。
3. **下游**：为 Planner/Reasoner/ReflectionEngine/ContextOrchestrator 提供决策基础。
4. **非入口**：不承载用户请求语义（AgentRequest 职责）。
5. **非恢复快照**：不承载运行恢复所需的完整状态快照（Checkpoint 职责）。
6. **非执行记录**：不承载工具/Worker 执行结果的原始数据（Observation 职责）。

### 4.2 必填字段（6 项）

| 字段名 | 类型语义 | 语义说明 | 主要依据 |
|---|---|---|---|
| request_id | string | 关联回发起请求的 AgentRequest.request_id。确保信念状态可追溯到原始请求上下文。不可为空。 | WP02-T009 标识规则 |
| confirmed_facts | vector\<string\> | 已确认为真的事实列表。由 Observation/ObservationDigest 验证、人工确认或推理引擎确定为真的结论。每条事实应自包含。 | 架构 3.8.2 第 1 项 |
| hypotheses | vector\<string\> | 当前假设列表。尚未确认但有证据支持的推测。Planner/Reasoner 据此做条件决策。 | 架构 3.8.2 第 2 项 |
| assumptions | vector\<string\> | 推理前提列表。被当作真但未验证的前置条件。若某 assumption 被推翻，依赖它的决策链需要重评估。 | 架构 3.8.2 第 3 项 |
| evidence_refs | vector\<string\> | 证据引用列表。指向支持当前信念的 Observation、ObservationDigest 或知识条目的标识。支持审计追溯。 | 架构 3.8.2 第 4 项 |
| confidence | float | 整体信念状态可靠度，取值 [0.0, 1.0]。0.0 表示信念状态完全不可靠，1.0 表示完全可靠。此值反映整体认知确定性，非某单条事实的置信度。 | 架构 3.8.2 第 5 项 |

### 4.3 可选字段（3 项）

| 字段名 | 类型语义 | 语义说明 | 约束 |
|---|---|---|---|
| goal_id | string | 关联到当前 GoalContract.goal_id。在多目标场景下标识本 BeliefState 对应的目标。 | 若存在则不为空 |
| created_at | int64 | 信念状态形成时间戳（毫秒）。用于判断信念新鲜度和时序。 | WP02-T010 时间语义。若存在则 > 0 |
| tags | vector\<string\> | 检索/审计标签。不承载执行控制信号。 | 与其他契约对象 tags 语义一致 |

### 4.4 设计理由

1. request_id 为必填：与 GoalContract 模式一致，确保信念状态可追溯到原始请求，支持审计和多 Session 场景。
2. confirmed_facts 为必填：架构 3.8.2 冻结的五核心字段之一，空向量合法（初始状态无确认事实）。
3. hypotheses 为必填：空向量合法；Planner 需显式知道当前无假设或有未确认假设。
4. assumptions 为必填：空向量合法；推理链条的前提需显式声明以支持重评估。
5. evidence_refs 为必填：空向量合法；即使无外部证据，也需显式声明。
6. confidence 为必填：消费者据此判断是否需要进一步求证或降级决策。
7. goal_id 为可选：单目标场景下可通过 request_id 间接关联目标。
8. created_at 为可选：可通过外部时序管理获取。
9. tags 为可选：与其他契约对象模式一致。

## 5. BeliefState 禁止字段表

### 5.1 禁入字段分类

| 字段类别 | 禁止字段 | 禁入理由 |
|---|---|---|
| Observation 执行语义 | payload、success、error（ErrorInfo）、side_effects | 执行结果归 Observation，BeliefState 只消费摘要结论 |
| Observation 关联 | tool_call_id、worker_task_id、duration_ms | 执行追溯归 Observation，BeliefState 通过 evidence_refs 间接引用 |
| ObservationDigest 摘要 | summary、key_facts、citations、omitted_details | 摘要语义归 ObservationDigest，BeliefState 对摘要做进一步合成 |
| 计划/决策 | plan_graph、step_list、action_decision、next_step | 计划语义归 Planner，BeliefState 只提供决策输入不产生决策 |
| Runtime 内部 | fsm_state、retry_counters、backoff_ms | 运行控制归 Runtime，BeliefState 不感知运行机制 |
| 消息渲染 | final_messages、rendered_prompt、prompt_bundle | 消息装配归 PromptComposer，BeliefState 不参与渲染 |
| Provider 私有 | model_provider_args、vendor_tool_schema | 模型/工具实现细节归 LLM Adapter，BeliefState 不绑定供应商 |
| Checkpoint 恢复 | working_memory_snapshot、pending_action、retry_counters | 恢复语义归 Checkpoint，BeliefState 是认知快照不是运行快照 |
| 入口请求 | user_input、request_channel、attachments、constraint_set | 请求语义归 AgentRequest，BeliefState 不重复入口数据 |
| 多 Agent 专有 | worker_task_list、lease_id、delegation_policy | 多 Agent 调度归 MultiAgent Coordinator，BeliefState 不涉及调度 |

### 5.2 与相邻对象的分层对称

| 层面 | BeliefState 持有 | BeliefState 禁止 | 对称对象 |
|---|---|---|---|
| 认知状态 | confirmed_facts、hypotheses、assumptions、evidence_refs、confidence | — | 本对象专有 |
| 执行结果 | — | payload、success、error、side_effects、duration_ms | Observation 持有 |
| 推理摘要 | — | summary、key_facts、citations、omitted_details | ObservationDigest 持有 |
| 恢复快照 | — | working_memory_snapshot、pending_action | Checkpoint 持有 |
| 目标定义 | —（通过 goal_id 关联） | goal_description、success_criteria、constraints | GoalContract 持有 |

## 6. 上下游关系与消费者

| 上游/下游 | 关系 | 数据流向 | 边界约束 |
|---|---|---|---|
| ObservationDigest → BeliefState | 合成输入 | Cognition 层从 Digest 的 key_facts/confidence 推导信念 | BeliefState 不内嵌 Digest 内容，通过 evidence_refs 引用 |
| GoalContract → BeliefState | 目标关联 | GoalContract.goal_id 可选关联到 BeliefState.goal_id | 不复制目标定义字段 |
| BeliefState → Planner | 消费 | Planner 读取 BeliefState 做计划生成 | 架构 4.3、6.2 步骤6 |
| BeliefState → Reasoner | 消费 | Reasoner 读取 BeliefState 做下一步决策 | 架构 4.3、6.2 步骤6 |
| BeliefState → ReflectionEngine | 消费 | ReflectionEngine 读取 BeliefState 做失败归因 | ADR-007 §3.2 |
| BeliefState → ContextOrchestrator | 消费 | 作为 belief_state 进入 ContextPacket | ADR-006 §6.1 item 10 |
| BeliefState → Checkpoint | 快照 | Checkpoint 快照 BeliefState 供恢复 | 架构 6.2 补充说明 |
| Cognition → BeliefState | 生产 | Reasoner/BeliefUpdater 基于 Digest+先验+反思更新 | 生产者归 cognition 实现层 |

## 7. 守卫校验规则清单

### 7.1 Layer 1：必填字段校验

| 规则编号 | 规则描述 | 通过条件 | 失败消息 |
|---|---|---|---|
| R1 | request_id 必须存在且非空 | has_non_empty_value(request_id) | "request_id is required and must be non-empty" |
| R2 | confirmed_facts 必须存在 | confirmed_facts.has_value() | "confirmed_facts is required" |
| R3 | hypotheses 必须存在 | hypotheses.has_value() | "hypotheses is required" |
| R4 | assumptions 必须存在 | assumptions.has_value() | "assumptions is required" |
| R5 | evidence_refs 必须存在 | evidence_refs.has_value() | "evidence_refs is required" |
| R6 | confidence 必须存在且在 [0.0, 1.0] | confidence.has_value() && >= 0.0f && <= 1.0f | "confidence is required and must be in [0.0, 1.0]" |

### 7.2 Layer 2：禁入边界校验

| 规则编号 | 规则描述 | 通过条件 | 失败消息 |
|---|---|---|---|
| R7 | goal_id 若存在则不为空 | 不存在或非空 | "goal_id must be non-empty when present" |
| R8 | created_at 若存在则 > 0 | 不存在或 > 0 | "created_at must be positive when present" |

### 7.3 守卫层叠关系

| 层级 | 守卫 | 已有/新增 | 来源 |
|---|---|---|---|
| Layer 1 | validate_belief_state_required_fields | **新增** | T009-B |
| Layer 2 | validate_belief_state_boundary | **新增** | T009-B |

Layer 2 在 Layer 1 通过后执行。

## 8. 验收清单

1. 已定义 BeliefState 6 必填 + 3 可选字段语义。
2. 已定义禁止字段表（10 类、30+ 具体字段）。
3. 已定义与相邻对象的分层对称关系。
4. 已定义上下游关系（1 生产者 + 7 消费者/下游）。
5. 已定义 8 条守卫校验规则（6 必填 + 2 边界）。
6. 所有结论可追溯到架构 3.8.2、4.3、6.2、ADR-006 §6.1、ADR-007 §3.2。

## 9. D Gate 结果

| 检查项 | 结果 |
|---|---|
| 五核心字段是否覆盖 confirmed_facts/hypotheses/assumptions/evidence_refs/confidence | ✅ |
| 主链路定位是否明确非入口/非恢复快照 | ✅ 明确区分 |
| request_id 是否建立 BeliefState→AgentRequest 追溯 | ✅ 必填 |
| confidence 范围是否冻结为 [0.0, 1.0] | ✅ |
| 消费者清单是否覆盖 Planner/Reasoner/ReflectionEngine/ContextOrchestrator | ✅ |
| 禁止字段是否覆盖执行/摘要/恢复/渲染/调度 | ✅ 10 类 |
| 守卫规则是否可直接翻译为代码 | ✅ 8 条 |
| 是否达到进入 -B 条件 | ✅ 通过 |

## 10. 风险与回退

1. 风险：confirmed_facts 与 SummaryMemory.confirmed_facts 语义重叠。
   回退：BeliefState.confirmed_facts 是当前轮次认知快照，SummaryMemory.confirmed_facts 是跨轮次持久化。两者语义层次不同，不冲突。
2. 风险：confidence 在不同消费者间解读不一致。
   回退：confidence 严格定义为整体信念可靠度，消费者不应将其解读为单条事实的置信度。
3. 风险：假设（assumptions）被推翻后的级联影响未在契约层处理。
   回退：级联重评估是 cognition 实现层职责，契约层只确保 assumptions 可显式声明。
4. 风险：Blueprint 将 BeliefState 归入 observation/，TODO 归入 agent/，存在目录不一致。
   回退：遵循 TODO 工作项定义放入 agent/。若需统一，由后续 T015/T016 评审纪要闭合。

## 11. 下一任务建议

1. WP03-T009-B：新增 BeliefState.h + BeliefStateGuards.h + 契约测试。
2. WP03-T010-D：ContextPacket 语义组成说明。
