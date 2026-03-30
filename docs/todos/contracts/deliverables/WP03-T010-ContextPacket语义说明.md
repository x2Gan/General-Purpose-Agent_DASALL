# WP03-T010 ContextPacket 语义组成说明

最近更新时间：2026-03-17
任务状态：Done
任务编号：WP03-T010
上游输入：ADR-006（ContextOrchestrator vs PromptComposer 职责划分）、架构 3.8.5、架构 4.3/5.8.3、架构 6.2、Blueprint §6、WP-01 核对单、T009-D（BeliefState 语义说明）

## 1. 任务理解

本任务只处理 WP03-T010：定义 ContextPacket 的语义组成与主链路定位。

具体交付物：
1. ContextPacket 职责语义与链路定位。
2. ContextPacket 必填/可选字段分组（覆盖 ADR-006 §6.1 全部 10 类槽位）。
3. ContextPacket 禁止字段清单。
4. ContextPacket 上下游关系与消费者。
5. D Gate 结果。

本任务不处理：
1. ContextPacket 字段级校验规则（T011 负责）。
2. ContextAssembleRequest / ContextAssembleResult（归后续 memory 子系统详细设计）。
3. Token 预算裁剪策略实现（归 ContextOrchestrator 实现层）。
4. Prompt 渲染或 PromptComposeRequest 定义（归 llm 子系统）。
5. 已冻结对象 AgentRequest/GoalContract/Observation/ObservationDigest/BeliefState 字段。

## 2. 约束与边界

### 2.1 直接约束（可追溯）

1. **架构 3.8.5**：ContextPacket 承载 user_turn、recent_history、summary_memory、retrieval_evidence、active_tools、policy_digest、token_budget_report（7 项基线）。
2. **ADR-006 §6.1**：ContextPacket 扩展为 10 类语义槽位 = 基线 7 项 + current_goal 摘要 + latest_observation_digest + belief_state。
3. **ADR-006 §6.1 禁令**：ContextPacket 不得包含 final_messages、provider_payload、rendered_prompt。
4. **ADR-006 §3.2**：ContextOrchestrator 归属 memory 子系统，产出 ContextPacket。
5. **ADR-006 §4**：调用顺序固定 ContextOrchestrator → PromptRegistry → PromptComposer → PromptPolicy → LLMManager。
6. **ADR-006 §8 风控**：PromptComposer 不得直接调 memory/knowledge；over_budget 须回传 Runtime。
7. **架构 4.3/5.8.3**：Cognition 层仅消费 ContextPacket 与 Observation，不直接访问底层驱动。
8. **架构 6.2 步骤 4**：Context Orchestrator 拉取 Memory/Knowledge 候选片段，执行预算裁剪，形成 ContextPacket。
9. **架构 5.3.3**：装配优先级：系统策略/安全边界 > 当前目标 > 相关历史/知识 > 一般历史。
10. **WP02-T009**：标识字段需入口生成并全链路透传。
11. **WP02-T010**：时间字段为毫秒时间戳。
12. **WP02-T012**：所有枚举须含 Unspecified 哨兵值。

### 2.2 非目标

1. 不实现 ContextOrchestrator 装配逻辑。
2. 不定义 ContextAssembleRequest / ContextAssembleResult 对象。
3. 不引入消息渲染字段（final_messages、rendered_prompt、prompt_bundle）。
4. 不引入运行态字段（fsm_state、retry_counters、backoff_ms）。
5. 不引入 Checkpoint 恢复专有字段（working_memory_snapshot、pending_action）。
6. 不引入执行结果字段（payload、error、side_effects、success）。
7. 不引入模型厂商特定字段（model_provider_args、vendor_tool_schema）。

### 2.3 前置依赖检查

1. T009-D/B 已 Done：BeliefState 禁入边界冻结。
2. T006-T008 已 Done：Observation/ObservationDigest 分层边界冻结。
3. T002-T005 已 Done：AgentRequest/GoalContract 字段冻结。
4. WP02 冻结包 Done：枚举规则、时间语义、标识规则已冻结。
5. WP01 MainFlowContracts.h 中 ContextPacketEntry 占位已存在。
6. ADR-006 状态为 Accepted。
7. 当前无阻塞项。

## 3. 字段语义与分组

### 3.1 ContextPacket 职责定义

ContextPacket 是 ContextOrchestrator 的产出对象，承载面向 **Cognition 层**与 **LLM 输入层**共享消费的**语义上下文**。

核心职责：
1. 汇聚本轮推理所需的全部语义信息（用户输入、目标摘要、历史、记忆、证据、工具可见性、策略、预算报告、信念状态）。
2. 作为 Cognition 层 (Planner, Reasoner, ReflectionEngine, ResponseBuilder) 与 PromptComposer 的共享输入。
3. 记录被裁剪/压缩/丢弃的元信息，供审计和调试使用。

ContextPacket 不是：
- 模型消息结构（那是 PromptComposeResult）。
- 执行结果记录（那是 Observation）。
- 认知状态快照（那是 BeliefState）。
- 恢复检查点（那是 Checkpoint）。
- 用户入口请求（那是 AgentRequest）。

### 3.2 必填字段（4 项）

| 字段名 | 类型 | 语义 | 来源约束 |
|---|---|---|---|
| request_id | string | 关联到 AgentRequest.request_id，全链路溯源 | WP02-T009 |
| user_turn | string | 当前用户输入/轮次文本，即使系统触发也须有代表性文本 | 架构 3.8.5、ADR-006 §6.1 item 1 |
| current_goal_summary | string | GoalContract 摘要文本（非完整 GoalContract，而是面向上下文消费的 1-2 句压缩）| ADR-006 §6.1 item 2 |
| recent_history | vector\<string\> | 最近对话/动作历史条目（首轮可为空向量但必须 present）| 架构 3.8.5、ADR-006 §6.1 item 3 |

### 3.3 可选字段（9 项）

| 字段名 | 类型 | 语义 | 缺失条件 | 来源约束 |
|---|---|---|---|---|
| summary_memory | string | 长期/摘要记忆内容 | 首轮或无摘要时 | 架构 3.8.5、ADR-006 §6.1 item 4 |
| retrieval_evidence | vector\<string\> | 检索召回的知识/证据条目 | 无知识检索时 | 架构 3.8.5、ADR-006 §6.1 item 5 |
| latest_observation_digest_summary | string | 最新 ObservationDigest 摘要文本 | 首轮无观察时 | ADR-006 §6.1 item 6 |
| active_tools | vector\<string\> | 可见工具/能力标识列表 | 无工具可用时 | 架构 3.8.5、ADR-006 §6.1 item 7 |
| policy_digest | string | 治理策略摘要文本 | 无策略约束时 | 架构 3.8.5、ADR-006 §6.1 item 8 |
| token_budget_report | string | Token 预算分配报告 | 预算跟踪未启用时 | 架构 3.8.5、ADR-006 §6.1 item 9 |
| belief_state_summary | string | BeliefState 或等价事实视图的文本摘要 | 信念状态未形成时 | ADR-006 §6.1 item 10 |
| created_at | int64_t | ContextPacket 装配完成时间戳（毫秒）| —（推荐但非强制） | WP02-T010 |
| tags | vector\<string\> | 检索/审计标签 | 无标签需求时 | 前序对象一致模式 |

### 3.4 ADR-006 §6.1 槽位覆盖对照

| ADR-006 §6.1 item | ContextPacket 字段 | 分类 |
|---|---|---|
| 1. user_turn | user_turn | Required |
| 2. current_goal / goal_contract 摘要 | current_goal_summary | Required |
| 3. recent_history | recent_history | Required |
| 4. summary_memory | summary_memory | Optional |
| 5. retrieval_evidence | retrieval_evidence | Optional |
| 6. latest_observation_digest | latest_observation_digest_summary | Optional |
| 7. active_tools / visible_capabilities 摘要 | active_tools | Optional |
| 8. policy_digest | policy_digest | Optional |
| 9. token_budget_report | token_budget_report | Optional |
| 10. belief_state / 等价事实视图 | belief_state_summary | Optional |

全部 10 类槽位已覆盖。✅

## 4. 禁止字段清单

以下字段**禁止**出现在 ContextPacket 中，违反即为架构越界：

| 分类 | 禁止字段 | 归属对象/子系统 | 禁止依据 |
|---|---|---|---|
| 消息渲染 | final_messages | PromptComposeResult | ADR-006 §6.1 禁令 |
| 消息渲染 | rendered_prompt | PromptComposeResult | ADR-006 §6.1 禁令 |
| 消息渲染 | provider_payload | LLMRequest | ADR-006 §6.1 禁令 |
| 消息渲染 | prompt_bundle | PromptComposeResult | ADR-006 §8 风控 |
| 消息渲染 | system_instructions | PromptRelease | ADR-006 §3.3 |
| Prompt 资产 | prompt_template | PromptRelease | ADR-006 §3.3 |
| Prompt 资产 | few_shots | PromptRelease | ADR-006 §3.3 |
| Prompt 资产 | output_schema | PromptComposeRequest | ADR-006 §3.3 |
| 模型厂商 | model_provider_args | LLMAdapter | ADR-006 §3.2 |
| 模型厂商 | vendor_tool_schema | LLMAdapter | ADR-006 §3.2 |
| 执行结果 | payload | Observation | WP03-T006 |
| 执行结果 | success | Observation | WP03-T006 |
| 执行结果 | error | Observation/ErrorInfo | WP03-T006 |
| 执行结果 | side_effects | Observation | WP03-T006 |
| 运行态 | fsm_state | Runtime | WP03-T009 |
| 运行态 | retry_counters | Runtime | WP03-T009 |
| 运行态 | backoff_ms | Runtime | WP03-T009 |
| 恢复专有 | working_memory_snapshot | Checkpoint | WP03-T009 |
| 恢复专有 | pending_action | Checkpoint | WP03-T009 |
| 计划/决策 | plan_graph | Planner | 架构 4.3 |
| 计划/决策 | step_list | Planner | 架构 4.3 |
| 计划/决策 | action_decision | Reasoner | 架构 4.3 |
| 多 Agent | worker_task_list | MultiAgent | ADR-008 |
| 入口请求 | attachments | AgentRequest | WP03-T002 |

共 24 类禁止字段。✅

## 5. 上下游关系与消费者

### 5.1 生产者

| 生产者 | 职责 | 归属子系统 |
|---|---|---|
| ContextOrchestrator | 检索、筛选、压缩、预算裁剪，产出 ContextPacket | memory 子系统 |

### 5.2 消费者

| 消费者 | 消费方式 | 依据 |
|---|---|---|
| Planner | 读取 goal_summary、recent_history、evidence 生成 PlanGraph | 架构 4.3、6.2 步骤6 |
| Reasoner | 读取全部语义槽位进行下一步决策 | 架构 4.3、6.2 步骤6 |
| ReflectionEngine | 读取 ContextPacket 进行失败归因 | ADR-007 §3.2 |
| ResponseBuilder | 读取 ContextPacket + 执行结果生成 AgentResult | Blueprint §3.1 |
| PerceptionEngine | 读取 user_turn + ContextPacket 提取意图 | Blueprint §3.1 |
| PromptComposer | 将 ContextPacket 槽位映射到 Prompt 模板变量 | ADR-006 §3.3、§4 |
| PromptPolicy | 对 ContextPacket 来源信息做 redaction/过滤 | ADR-006 §3.4 |
| Checkpoint | 快照 ContextPacket 用于恢复 | 架构 6.2 补充 |

共 1 生产者 + 8 消费者。✅

### 5.3 主链路位置

```
AgentRequest → GoalContract → [ContextPacket] → Observation
  → ObservationDigest → BeliefState → Checkpoint → AgentResult
```

ContextPacket 位于 GoalContract 之后、Observation 之前，是主链路中推理前的**语义上下文汇聚点**。

## 6. D Gate 结果

| 检查项 | 结果 |
|---|---|
| 字段覆盖 ADR-006 §6.1 全 10 类槽位 | ✅ 通过（§3.4 对照表） |
| 不含消息渲染内容 | ✅ 通过（§4 禁止字段 24 类） |
| 生产者/消费者可列举 | ✅ 通过（1+8） |
| 与前序冻结对象不重叠 | ✅ 通过（禁止字段覆盖 ObsDigest/BeliefState/Observation/AgentRequest） |
| 与 ADR-006 结论一致 | ✅ 通过 |

**D Gate 判定：通过，可进入 T010-B。**

## 7. 研究证据链

### 7.1 本地证据清单

| 编号 | 证据来源 | 关键结论 |
|---|---|---|
| L1 | 架构 3.8.5 | ContextPacket 7 项基线字段 |
| L2 | ADR-006 §6.1 | 10 类语义槽位 + 禁止 final_messages/provider_payload/rendered_prompt |
| L3 | ADR-006 §3.2/§4 | ContextOrchestrator 产出 ContextPacket，归 memory 子系统 |
| L4 | 架构 4.3/5.8.3 | Cognition 仅消费 ContextPacket + Observation |
| L5 | 架构 6.2 | 步骤 4 形成 ContextPacket，步骤 5 消费 ContextPacket |
| L6 | Blueprint §6 | 接口路径 context/ContextPacket.h |
| L7 | WP02-T009/T010/T012 | 标识、时间、枚举规则 |

### 7.2 外部参考清单

| 编号 | 参考来源 | 关键结论 |
|---|---|---|
| E1 | LangGraph state management | state 与 prompt 分离，state 作为结构化中间对象 |
| E2 | LlamaIndex Workflows | Context 作为独立语义对象在步骤间传递 |
| E3 | LangChain Deep Agents | 上下文压缩/管理独立于模型交互 |
| E4 | ADR 通用实践 | 语义上下文与渲染分层 |
