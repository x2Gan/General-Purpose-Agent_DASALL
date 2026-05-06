# RecoveryContextBoundary (Single Source of Truth)

关联任务：INT-TODO-007  
关联来源：集成评审 §7.2；ADR-007；runtime 详设 6.11.1

## 1. 目标

本文件冻结 Recovery Context 的系统边界，明确：

1. cognition 可见事实。
2. runtime / RecoveryManager 独占事实。
3. 禁止回流项。
4. `retry budget`、`idempotency`、`circuit`、`deadline`、补偿句柄等执行控制信息的归属。

本文件的核心目的，是在字段级守住 ADR-007 的“建议权与执行权分离”。

## 2. 总原则

1. ReflectionEngine 只拥有失败语义解释权和恢复建议权，不拥有恢复执行权。
2. RecoveryManager / AgentOrchestrator 拥有恢复准入、执行、拒绝、升级与补偿协调权。
3. cognition 只能看见支持语义判断的事实，不能直接消费 retry counter、circuit state、idempotency token、补偿句柄等执行控制信息。
4. 若 runtime 需要把恢复结果让 cognition 感知，必须将其转换为新的 `Observation` / `ErrorInfo` / `GoalContract` 事实，而不是把内部 `RecoveryOutcome` 或执行控制上下文直接回流。

## 3. Recovery Context Boundary Table

| 上下文项 | 生产者 | cognition 可见性 | runtime / recovery 可见性 | 禁止回流 / 约束 |
|---|---|---|---|---|
| `Observation`、`ObservationDigest`、`ErrorInfo`、`GoalContract`、`BeliefState`、plan node status | tools / llm / cognition / runtime | 可见 | 可见 | 作为 ReflectionEngine 的建议输入，也作为 RecoveryManager 的事实输入 |
| `ReflectionDecision.{continue,retry_step,replan,abort_safe}`、`rationale`、`confidence`、`relevant_observation_refs` | cognition / ReflectionEngine | 仅输出 | 可见 | 只表达 suggestion-only 语义；不得被当成已执行动作 |
| `StageBudgetHint`、risk/latency hint、阶段级 `deadline_ms` 提示 | runtime / cognition projector | 可见（投影后） | 可见 | cognition 只看提示视图；不得反推出 runtime 内部 retry budget、circuit state 或剩余恢复次数 |
| `BudgetSnapshot`、`retry budget` 剩余量、`max_replan_count` 剩余量、session / step deadline 剩余量 | BudgetController / Runtime | 不可见原始值；仅允许预算提示投影 | 独占 | 原始 budget / deadline 计数与拒绝原因禁止回流到 cognition |
| retry 次数、backoff 阶段、circuit state、checkpoint 可恢复性 | RecoveryManager / CheckpointManager | 不可见 | 独占 | 这些是恢复执行事实，不属于认知规划语义 |
| `idempotency_and_side_effect_report`、`retry_idempotency_token`、side-effect classification、补偿可用性、`compensation_result_ref` / 补偿句柄 | tools / services / runtime | 不可见 | 独占 | 幂等与补偿上下文只用于恢复准入与执行，不进入 ReflectionDecision |
| `RecoveryOutcome.{admit,reject,escalate}`、`rejection_reason`、`escalation_reason` | RecoveryManager | 不可见 | 输出事实 | 若下一轮需要感知，只能转换为新的 `Observation` / `ErrorInfo` 事实，禁止直接回流 `RecoveryOutcome` |
| policy rule id、secret、provider private payload、raw checkpoint blob、内部审计句柄 | access / infra / llm / checkpoint backend | 不可见 | 受限可见 | 禁止进入 cognition，也不得泄露到非审计字段 |
| `PendingInteractionState.deadline_ms`、`resume_channel`、`input_schema_hint` | runtime waiting state | 仅可见被 runtime 投影后的等待态事实 | 可见 | 不把 waiting checkpoint 原始内部对象直接透传给 cognition |

## 4. 字段级规则

### 4.1 retry budget

1. `retry budget` 的 semantic owner 是 runtime / RecoveryManager，而不是 cognition。
2. cognition 可以收到“预算紧张 / 不建议重试 / 需要降级”的提示，但不能看到原始剩余 retry budget 或拒绝阈值。

### 4.2 idempotency

1. `retry_idempotency_token`、`idempotency_and_side_effect_report`、side-effect classification、补偿句柄都属于 runtime 独占恢复上下文。
2. cognition 不负责生成、复用、校验或解释 idempotency token。
3. 若工具或服务报告 side effect 风险，RecoveryManager 负责准入与补偿协调，cognition 只能接收由 runtime 投影后的语义事实。

### 4.3 circuit

1. circuit state、backoff 阶段、breaker 阈值命中情况由 runtime / RecoveryManager 或下游执行控制面持有。
2. cognition 不建立第二套 circuit breaker，也不消费原始 circuit state。
3. cognition 若需要感知故障域，只能通过 fail-fast 的 `Observation` / `ErrorInfo` 或 profile hint 感知，而不是直接读取 breaker 内部状态。

### 4.4 deadline

1. session-level deadline、worker ticket deadline、checkpoint admission deadline 属于 runtime 执行控制信息。
2. cognition 只消费 stage-level `deadline_ms` 提示，用于阶段隔离和 fail-fast，不拥有 session / recovery deadline 的主控权。
3. hot-reload 或 profile 变更不得 mid-turn 改写已经绑定的 deadline；新的 deadline 只影响后续请求或后续阶段创建。

## 5. 禁止回流清单

以下内容禁止直接从 runtime / RecoveryManager 回流到 cognition：

1. retry counter、retry budget 原始值。
2. circuit state、backoff 阶段、熔断拒绝内部原因码。
3. `RecoveryOutcome`、`rejection_reason`、`escalation_reason` 原对象。
4. `retry_idempotency_token`、side-effect classification、补偿句柄。
5. secret、provider private payload、raw checkpoint blob。

若业务确实需要让 cognition 知道“发生了恢复拒绝/升级/降级”，必须由 runtime 重新投影为 `Observation` / `ErrorInfo` / 受控 `GoalContract` 事实。

## 6. 与相邻 SSOT 的分工

1. 本文件只定义 Recovery Context 的可见性与执行权边界。
2. `RuntimePolicyConsumerMatrix` 负责 profile 键的 consumer / owner / override / hot-reload 语义，不负责恢复执行上下文归属。
3. `UnaryResponseContract`、`SingleAgentRuntimePortMatrix` 等 SSOT 只消费本文件的边界结论，不改写其字段级归属。

## 7. Design -> Build 映射

1. `INT-TODO-017` 必须让 `AgentOrchestrator` 与 `RecoveryManager::evaluate/apply` 的使用点对齐本边界表。
2. 后续 `RuntimeRecoveryContextIntegrationTest` 只允许验证“投影后的 cognition facts”与“runtime 独占事实”分账成立，不能反向打通禁止回流项。

## 8. 完成判定

当且仅当以下条件成立时，才允许将 Recovery Context 视为系统级 SSOT：

1. cognition 可见事实与 runtime 独占事实已按字段级分开。
2. `retry budget`、`idempotency`、`circuit`、`deadline`、补偿句柄的归属已固定。
3. 禁止回流清单明确，且“如需感知必须重新投影为 Observation/ErrorInfo”的规则已固定。
4. runtime 与 cognition 详设对该边界表达一致。