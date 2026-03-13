# ADR-007 ReflectionEngine 与 RecoveryManager 职责划分

## 0. 文档信息

### 0.1 文档定位

本文档用于裁定 DASALL Agent 中 ReflectionEngine 与 RecoveryManager 的职责边界、输入输出契约和调用关系。

该决策属于架构级边界决策，会直接影响以下内容：

1. ReflectionDecision 的语义范围。
2. runtime 与 cognition 的依赖方向。
3. retry、replan、degrade、abort_safe、checkpoint、compensation 的归属。
4. 后续 contracts、runtime、cognition、tools 模块的接口冻结方式。

### 0.2 背景

当前设计文档已经分别定义了两类能力：

1. Cognition 子系统中的 ReflectionEngine，负责分析失败原因、偏航风险和后续动作建议。
2. Runtime 子系统中的 RecoveryManager，负责超时、重试、熔断、降级和恢复策略执行。

但现有材料里仍然存在一处典型边界模糊：

1. 架构文档一方面写明 ReflectionEngine 输出 retry_step、replan、abort_safe 等反思结论。
2. 另一方面又写明 Runtime 负责重试、超时、checkpoint、恢复和失败收敛。
3. Tool/Workflow 章节还写到步骤失败后由 Runtime 判断局部重试、全局重规划还是失败收敛。

如果不先裁定这条边界，后续很容易出现两个问题：

1. cognition 为了“更智能地处理失败”开始侵入 retry budget、超时控制、熔断状态和补偿执行。
2. runtime 为了“更统一地处理故障”开始自己承担失败归因、计划修补和语义级重规划判断。

---

## 1. 问题陈述

需要解决的问题不是“两个组件都参与失败处理是否可以”，而是以下三个更根本的问题：

1. 谁拥有失败语义解释权。
2. 谁拥有恢复动作执行权。
3. 当反思建议与预算、幂等性、副作用、熔断状态冲突时，谁拥有最终裁定权。

这三个问题如果没有清晰答案，后续 contracts 和 runtime/cognition 详细设计一定会产生职责串扰。

---

## 2. 现有设计理解

### 2.1 当前仓库内的设计信号

基于现有架构文档与工程蓝图，可以得出以下共识：

1. Agent Control Plane 负责超时、重试、熔断、降级、checkpoint 和恢复。
2. Cognition Layer 负责失败反思和重规划，并显式输出 retry_step、replan、abort_safe 等控制信号。
3. 工程蓝图明确写明 ReflectionEngine 的职责是“分析失败并给出重试/重规划/终止决策”。
4. 工程蓝图同时明确写明 cognition 不承担线程调度、超时控制和重试，这些由 runtime 负责。
5. Runtime 子系统关键组件中已经列出 RecoveryManager，其职责是“超时、重试、熔断、降级和恢复策略执行”。
6. Tool 与 Workflow 章节明确要求单步失败后，交由 Runtime 判断局部重试、全局重规划还是失败收敛。

这说明当前总体方向已经隐含了一个正确趋势：

1. ReflectionEngine 更接近“失败分析器和策略建议者”。
2. RecoveryManager 更接近“恢复策略执行器和运行控制者”。

问题在于这一点还没有被显式冻结，尤其是“谁来最终决定是否真的执行 retry/replan”仍有重复解释空间。

### 2.2 业界常见做法与可借鉴结论

结合当前主流 Agent 系统设计资料与可靠性架构模式，可以提炼出四条稳定结论：

1. Anthropic 在 2024 年底总结的 agent 实践里，把 evaluator-optimizer 明确定义为“生成与评估的认知闭环”，而不是运行时重试框架；同时强调 agent 应基于环境反馈工作，但必须配合显式 stopping conditions 与 guardrails。
2. OpenAI 当前 Agents 指南把 agent 拆成 models、tools、knowledge、guardrails、logic 和 evals，强调控制流与优化是独立原语，而不是把所有故障处理都塞进模型推理本身。
3. Azure Retry Pattern 明确要求 retry logic 只应放在“真正理解失败操作完整上下文”的控制层，并警告避免多层嵌套 retry；幂等性、退避、熔断和失败升级都属于可靠性控制问题，而不是业务认知问题。
4. Azure Compensating Transaction Pattern 明确要求补偿和回滚由 workflow/orchestration 记录并执行，要求可恢复、可续跑、幂等和可审计，这天然属于 runtime control plane，而不是反思模块。

这些结论与 DASALL 当前的七层分层和 runtime/cognition 分模块设计是一致的。

---

## 3. 决策

### 3.1 总体决策

ReflectionEngine 与 RecoveryManager 必须严格分层，分别承担“失败语义分析与策略建议”和“恢复策略裁定与执行控制”两类职责，不能合并为单一组件，也不能互相侵入对方的主职责域。

### 3.2 ReflectionEngine 的职责边界

ReflectionEngine 归属 cognition 子系统，负责理解失败、归因失败并给出语义级下一步建议，而不是直接驱动恢复动作执行。

它的职责固定为：

1. 读取 Observation、ErrorInfo、当前 PlanGraph、BeliefState、GoalContract 与最近执行轨迹。
2. 判断当前失败是局部执行偏差、计划缺陷、上下文不足、工具选择错误、环境阻塞还是不可恢复失败。
3. 生成 ReflectionDecision，统一表达 continue、retry_step、replan、abort_safe 等反思结论。
4. 对 retry_step 或 replan 给出语义理由，例如参数错误、证据不足、假设失真、步骤顺序错误、目标约束变化。
5. 在需要时输出 plan_patch_hint、reason_code、confidence、clarification_needed、skill_switch_hint 等认知级附加信号。

ReflectionEngine 明确不负责：

1. 维护 retry counters、backoff deadline、lease、circuit breaker 和超时计时器。
2. 判断某个副作用操作在当前幂等性条件下是否允许真实重放。
3. 直接触发 tool 重试、workflow 重放、checkpoint resume 或 compensation 执行。
4. 自己持久化 checkpoint 或修改 runtime 状态机。
5. 代替 runtime 做 budget、profile、safe mode、degraded mode 的最终裁定。

### 3.3 RecoveryManager 的职责边界

RecoveryManager 归属 runtime 子系统，负责把失败处理建议与运行时约束结合起来，形成可执行、可审计、可恢复的控制动作。

它的职责固定为：

1. 接收 Observation、ErrorInfo、Checkpoint、RuntimeBudget、retry_counters、当前 FSM 状态以及 ReflectionDecision。
2. 基于幂等性、副作用、预算、熔断状态、profile 策略和审批要求，裁定是否允许执行 retry_step、replan、abort_safe 或 degrade。
3. 执行具体恢复策略，包括重试调度、退避、熔断、降级、恢复点回放、转入 SafeMode 或 FailedSafe。
4. 在需要补偿时协调 Tool System 中的 CompensationManager，驱动补偿动作注册、执行与结果审计。
5. 在需要重规划时，由 runtime 驱动 Planner 或上层 Orchestrator 进入 replan 路径，而不是由 ReflectionEngine 直接越级调用执行链路。
6. 记录 RecoveryOutcome、更新 checkpoint、写入审计事件，并保证恢复流程可中断、可续跑、可观测。

RecoveryManager 明确不负责：

1. 独立完成失败归因、计划质量评估或 belief 修正。
2. 绕过 ReflectionEngine 直接根据错误码推导复杂语义级 replan 结论。
3. 直接生成新的 PlanGraph、Prompt 或最终回复。
4. 把简单瞬时错误自动升级为全局语义问题并替代 cognition 做判断。

### 3.4 最终裁定权

为避免 Runtime 与 Cognition 双重拍板，必须同时明确最终裁定权：

1. ReflectionEngine 拥有失败语义解释权和建议权。
2. RecoveryManager 拥有恢复动作执行权和运行时准入裁定权。
3. 当 ReflectionDecision 与运行时约束冲突时，以 RecoveryManager 的准入裁定为准，但必须保留可审计的拒绝原因。
4. 当 RecoveryManager 因预算、幂等性、熔断或风险策略拒绝执行 retry_step 时，可以升级为 replan 或 abort_safe，但不得静默篡改认知结论。

---

## 4. 调用顺序与责任链

本决策固定以下失败处理责任链：

1. Tool、Workflow、MCP 或外部执行结果统一映射为 Observation 与 ErrorInfo。
2. Runtime 持久化必要 checkpoint，并收集当前 retry counters、budget、side effects 与状态机上下文。
3. ReflectionEngine 读取失败上下文，输出 ReflectionDecision 与认知级理由。
4. RecoveryManager 结合 ReflectionDecision 与运行时约束做准入裁定。
5. 若准入通过，则执行具体恢复动作：retry、resume、replan 路由、degrade、abort_safe、compensation。
6. Runtime 更新 FSM、checkpoint、audit 与 Experience Memory 写回。

这条责任链意味着：

1. 先分析失败含义，再决定是否允许执行恢复动作。
2. retry_step 是语义建议，不等于已经真实发起重试。
3. replan 是控制路径切换，不等于 ReflectionEngine 直接拥有计划执行权。
4. compensation 是运行补偿动作，不属于 ReflectionEngine 的职责域。

---

## 5. 契约层面的直接影响

本决策会直接影响 contracts 的定义，需同步冻结以下约束：

### 5.1 ReflectionDecision 只表达语义判断，不表达调度细节

ReflectionDecision 不得包含以下运行时控制字段：

1. retry_after_ms
2. backoff_strategy
3. lease_extension
4. checkpoint_blob
5. circuit_breaker_transition

ReflectionDecision 应包含的内容是：

1. decision_kind
2. rationale
3. confidence
4. relevant_observation_refs
5. plan_patch_hint 或 replan_hint
6. clarification_hint 或 skill_switch_hint

### 5.2 RecoveryManager 消费 ReflectionDecision，但不替代其定义

RecoveryManager 的输入应是一个恢复请求对象，而不是第二个反思对象。它至少包含：

1. reflection_decision
2. error_info
3. latest_observation
4. checkpoint
5. retry_counters
6. runtime_budget_snapshot
7. idempotency_and_side_effect_report

### 5.3 RecoveryOutcome 表达执行结果与控制元数据

RecoveryOutcome 应表达：

1. executed_action
2. final_runtime_state
3. updated_retry_counters
4. checkpoint_ref
5. compensation_result_ref
6. rejection_reason 或 escalation_reason

它不应反向承担失败归因或计划语义修补职责。

---

## 6. 备选方案与取舍

### 方案 A：由 ReflectionEngine 同时负责失败分析与恢复执行

不采纳。

原因：

1. 会让 cognition 侵入 timeout、retry、熔断、补偿和 checkpoint 语义。
2. 会让模型推理结果直接驱动副作用重放，破坏控制与认知分离。
3. 会让恢复链路难以做到幂等、审计和 profile 化裁剪。

### 方案 B：由 RecoveryManager 同时负责错误分析与恢复执行

不采纳。

原因：

1. 会让 runtime 为了统一而承担语义归因和计划判断，最终侵入 cognition。
2. 会把复杂失败场景退化成错误码分支，损失反思与重规划能力。
3. 难以在未来支持更复杂的自评、重规划和 Skill 切换策略。

### 方案 C：ReflectionEngine 负责语义判断，RecoveryManager 负责恢复执行

采纳。

原因：

1. 符合 DASALL 当前分层原则：认知负责判断，主控负责执行。
2. 符合主流 Agent 实践中 evaluator/optimizer 与 orchestration/guardrails 分离的趋势。
3. 符合可靠性架构里 retry、compensation、resume 由 control plane 持有的工程规律。

---

## 7. 影响与后续动作

### 7.1 直接影响

1. contracts 需要冻结 ReflectionDecision 与 RecoveryOutcome 的分层字段。
2. runtime 详细设计需要把 RetryPolicy、RecoveryPolicy、CheckpointPolicy、CompensationPolicy 放入 RecoveryManager 责任域。
3. cognition 详细设计需要把 ReflectionEngine 限定为“分析与建议”，不直接控制线程、队列、超时和真实执行。
4. tools 详细设计需要明确 CompensationManager 由 runtime 驱动，而不是由 reflection 直接调用。

### 7.2 实施约束

后续实现必须满足以下约束：

1. 任何真实 retry 都必须经过 RecoveryManager 的准入判断。
2. 任何全局 replan 都必须由 runtime 驱动 Planner 路径切换，并受 MAX_REPLAN_COUNT 约束。
3. 任何补偿与回滚都必须记录到审计链路，并支持中断后续跑。
4. ReflectionEngine 给出的建议若被拒绝，必须有 rejection_reason 可追踪。

### 7.3 后续文档建议

建议紧接本 ADR 继续补充以下内容：

1. contracts 详细定义 ErrorInfo、ReflectionDecision、RecoveryOutcome、Checkpoint。
2. runtime 详细设计中的失败状态机、恢复状态迁移和补偿时序。
3. AgentOrchestrator 与 MultiAgentCoordinator 职责划分 ADR。

---

## 8. 最终裁定

本轮正式裁定如下：

1. ReflectionEngine 只负责失败语义分析与建议，不负责恢复执行。
2. RecoveryManager 只负责恢复准入与执行控制，不负责复杂失败归因。
3. retry、replan、abort_safe 在认知层是建议信号，在 runtime 层才变成受约束的真实控制动作。
4. 该边界应在 contracts、runtime、cognition 详细设计之前冻结。

Status：Accepted