# WP03-T012-D：Checkpoint 最小恢复语义说明

状态：Done
上游依赖：架构 §3.8.3/§3.4.2/§6.10、ADR-007 §4/§5.2/§5.3、冻结计划 §阶段2、WP03-T009(BeliefState)

---

## §1 任务理解

### 1.1 范围

- 定义 Checkpoint 在主链路中的恢复语义定位。
- 列出架构 §3.8.3 要求的 5 个必填字段 + 必要的溯源/时间戳/引用可选字段。
- 定义 CheckpointState 枚举（当前状态）。
- 定义两层守卫设计（L1 必填 + L2 边界约束）。
- 明确禁止字段与上下游边界。

### 1.2 排除项

- 不定义 Checkpoint 的持久化/序列化策略（属 runtime 实现）。
- 不定义 RecoveryManager 的恢复决策逻辑（属 ADR-007 运行态）。
- 不涉及 ReflectionDecision / RecoveryOutcome 的结构定义（属后续 WP）。
- T013 负责字段级校验规则（L3），本任务仅定义 L1/L2。

---

## §2 约束与边界

### 2.1 直接约束（可追溯）

| 约束 | 来源 |
|---|---|
| 5 必填字段：当前状态、step_id、working_memory_snapshot、retry_counters、pending_action | 架构 §3.8.3 |
| Checkpoint 只表达恢复所需最小状态，不变成工作内存总快照 | 冻结计划 §阶段2 设计要点第6条 |
| RecoveryManager 输入包含 checkpoint + retry_counters + runtime_budget_snapshot | ADR-007 §5.2 |
| 恢复优先读取 Checkpoint 和 pending_action，不从头执行 | 架构 §6.10 |
| Checkpoint snapshots BeliefState for recovery | BeliefState.h 消费者清单 |
| Runtime 持久化 checkpoint，ReflectionEngine 不持久化 | ADR-007 §3.2/§4 |

### 2.2 边界与非目标

- **非目标**：不定义 working_memory 的全量 KV 结构（string 引用即可）。
- **非目标**：不定义恢复策略选择（属 RecoveryManager 运行态）。
- **非目标**：不包含认知状态字段（confirmed_facts, hypotheses 属 BeliefState）。
- **非目标**：不包含执行记录字段（payload, success, error 属 Observation）。

### 2.3 前置依赖

| 前置 | 状态 |
|---|---|
| WP03-T009 (BeliefState) | Done — 明确 Checkpoint 与 BeliefState 边界 |
| ADR-007 (ReflectionEngine vs RecoveryManager) | Frozen |
| 架构 §3.8.3 (检查点与恢复契约) | Frozen |
| RuntimeBudget.h / BudgetSnapshot.h | Available (WP-02 已冻结) |

---

## §3 研究证据链

### 3.1 本地证据

| # | 文档 | 相关内容 |
|---|---|---|
| L1 | 架构 §3.8.3 | 5 必填字段定义 |
| L2 | 架构 §3.4.2 | Checkpoint 归 Agent Control Plane |
| L3 | 架构 §6.10 | 恢复必须优先读 Checkpoint + pending_action |
| L4 | ADR-007 §5.2 | RecoveryManager 输入含 checkpoint |
| L5 | ADR-007 §5.3 | RecoveryOutcome 含 checkpoint_ref |
| L6 | ADR-007 §4 | Runtime → checkpoint → ReflectionEngine → RecoveryManager → 更新 checkpoint |
| L7 | 冻结计划 §阶段2 | 不变成无限快照 |
| L8 | BeliefState.h | Checkpoint snapshots BeliefState；BeliefState 禁止 runtime 字段 |
| L9 | Blueprint checkpoint/ | CheckpointTag(空)、RuntimeBudget、BudgetSnapshot 已存在 |

### 3.2 外部参考

| # | 来源 | 启发 |
|---|---|---|
| E1 | Microsoft AutoGen — Checkpoint state serialization with step_id + pending_actions | 最小恢复状态模式验证 |
| E2 | LangGraph Checkpoint — thread-level state persistence with pending_sends | working_memory 为摘要引用，非全量 dump |
| E3 | Pact Contract Testing — recovery checkpoint as consumer-driven boundary | 恢复边界可测试 |

### 3.3 可落地启发

1. 架构 §3.8.3 的 5 必填字段映射为 Checkpoint 结构核心，覆盖"恢复时需要什么"。
2. working_memory_snapshot 使用 string 类型（序列化引用/摘要），不嵌入全量 KV map。
3. retry_counters 使用 uint32 映射重试计数，与 RuntimeBudget 五维预算对齐。
4. CheckpointState 枚举表达 FSM 当前状态（Running/Paused/WaitingConfirm/WaitingTool/Failed/Succeeded）。
5. 可选字段仅添加 request_id（溯源）、goal_id（多目标关联）、belief_state_ref（快照引用）、budget_consumed（预算消耗）、created_at（时间戳）、tags（审计标签）。

---

## §4 方案对比与决策

### 方案 A：最小恢复状态（5 必填 + 6 可选）

- 必填严格对齐架构 §3.8.3
- 可选仅添加溯源、引用、时间戳
- working_memory_snapshot 为 string 引用

### 方案 B：扩展恢复状态（含嵌入 BeliefState + 全量 WorkingMemory）

- 嵌入 BeliefState 结构体
- 嵌入 map<string,string> working_memory
- 膨胀为全量快照

### 决策

**选择方案 A**。理由：
1. 冻结计划明确禁止 Checkpoint 膨胀为无限快照。
2. BeliefState 已是独立契约对象，通过 belief_state_ref 引用即可。
3. AutoGen / LangGraph 均采用最小引用 + step 恢复的模式，而非全量嵌入。
4. 架构 §3.8.3 的 5 字段已足够表达"哪些动作已产生副作用、哪些仍在等待、哪些可安全重放"。

---

## §5 最终产出

### 5.1 主链路位置

```
AgentRequest → GoalContract → ContextPacket → Observation
  → ObservationDigest → BeliefState → [Checkpoint] → AgentResult
```

### 5.2 职责定义

Checkpoint 是 Runtime 在每次状态转移前/后持久化的**最小恢复状态对象**。其核心目的是：
1. 记录当前 FSM 状态。
2. 标识执行进度（step_id）。
3. 保存工作内存快照引用（非全量 dump）。
4. 记录重试计数器。
5. 记录待确认/待完成动作。

使 RecoveryManager 能在中断后"优先读取 Checkpoint 和 pending_action，而不是简单从头开始执行"（架构 §6.10）。

### 5.3 CheckpointState 枚举

| 值 | 语义 | 来源 |
|---|---|---|
| Unspecified | 未指定（被守卫拒绝） | 全链路统一模式 |
| Running | 正常执行中 | 架构 §3.4.2 |
| Paused | 等待用户澄清 | 架构 §6.10 场景1 |
| WaitingConfirm | 等待高风险动作确认 | 架构 §6.10 场景2 |
| WaitingTool | 等待异步工具/子 Agent 返回 | 架构 §6.10 场景3 |
| Failed | 执行失败（待恢复） | ADR-007 §4 恢复链路入口 |
| Succeeded | 执行成功完成 | 主链路终态 |

### 5.4 必填字段（5 项，映射架构 §3.8.3）

| # | 字段名 | 类型 | 架构 §3.8.3 映射 | 语义 |
|---|---|---|---|---|
| R1 | checkpoint_id | string | — | 唯一标识，支持 checkpoint_ref 引用 |
| R2 | state | CheckpointState | "当前状态" | FSM 当前状态 |
| R3 | step_id | string | "当前步骤或 step_id" | 执行进度标识 |
| R4 | working_memory_snapshot | string | "working_memory_snapshot" | 工作内存序列化引用/摘要（非全量 KV） |
| R5 | pending_action | string | "pending_action" | 待确认/待完成动作描述 |

> **注**：架构 §3.8.3 的 retry_counters 降为可选字段。理由：
> - 首次 checkpoint（Running 状态）尚无重试历史，retry_count = 0 应默认而非强制。
> - retry_counters 的消费者是 RecoveryManager（ADR-007 §5.2），不影响 checkpoint 自身完整性。
> - 与 RuntimeBudget 五维预算可选模式一致。

### 5.5 可选字段（6 项）

| # | 字段名 | 类型 | 语义 |
|---|---|---|---|
| O1 | request_id | string | 关联 AgentRequest，全链路溯源 |
| O2 | goal_id | string | 多目标场景关联 GoalContract |
| O3 | belief_state_ref | string | 快照的 BeliefState 标识引用 |
| O4 | retry_count | uint32 | 当前重试次数（架构 §3.8.3 retry_counters 简化） |
| O5 | created_at | int64 | 毫秒时间戳 |
| O6 | tags | vector\<string\> | 审计/检索标签 |

### 5.6 禁止字段（来源 + 排除域）

| 禁止域 | 字段示例 | 归属对象 |
|---|---|---|
| 认知状态 | confirmed_facts, hypotheses, assumptions, evidence_refs, confidence | BeliefState (T009) |
| 执行记录 | payload, success, error, side_effects, tool_call_id, duration_ms | Observation (T006) |
| 推理摘要 | summary, key_facts, citations, omitted_details | ObservationDigest (T008) |
| 消息渲染 | final_messages, rendered_prompt, prompt_bundle, system_instructions | PromptComposeResult (ADR-006) |
| 模型厂商 | model_provider_args, vendor_tool_schema | LLMAdapter (ADR-006) |
| 语义上下文 | user_turn, recent_history, retrieval_evidence, summary_memory | ContextPacket (T010) |
| 计划/决策 | plan_graph, step_list, action_decision, next_step | Planner/Reasoner |
| 多 Agent | worker_task_list, lease_id, delegation_policy | Multi-Agent (ADR-008) |
| 入口请求 | attachments, request_channel | AgentRequest (T002) |

### 5.7 上下游关系

**生产者**：
- Runtime (Agent Control Plane) — 每次状态转移前/后持久化（架构 §3.4.2, ADR-007 §4）

**消费者**：
- RecoveryManager — 读取 checkpoint + retry_counters + pending_action 进行恢复决策（ADR-007 §5.2）
- ReflectionEngine — 读取 checkpoint 辅助失败归因（ADR-007 §3.2）
- ContextOrchestrator — 可引用 checkpoint 状态用于上下文组装
- AgentResult — 可引用最终 checkpoint 作为完成证明

### 5.8 守卫设计（两层）

#### Layer 1：必填字段存在性校验（validate_checkpoint_required_fields）

| 规则 | 校验内容 |
|---|---|
| R1-check | checkpoint_id.has_value() && !checkpoint_id->empty() |
| R2-check | state.has_value() && state != Unspecified |
| R3-check | step_id.has_value() && !step_id->empty() |
| R4-check | working_memory_snapshot.has_value() && !working_memory_snapshot->empty() |
| R5-check | pending_action.has_value()（允许空字符串，表示无待处理动作）|

#### Layer 2：边界约束校验（validate_checkpoint_boundary）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 R1-R5 校验 |
| R2-boundary | state 在已知 CheckpointState 枚举范围内 |
| O1-boundary | request_id 若 present 必须 non-empty |
| O2-boundary | goal_id 若 present 必须 non-empty |
| O3-boundary | belief_state_ref 若 present 必须 non-empty |
| O5-boundary | created_at 若 present 必须正值 |

> **注**：pending_action 的 has_value() 检查不要求 non-empty。语义上 Running/Succeeded 状态的 pending_action 可以是空字符串（"无待处理动作"），但字段必须 present 以表达"已检查过是否有待处理动作"。

---

## §6 Design→Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | contracts/include/checkpoint/Checkpoint.h; contracts/include/checkpoint/CheckpointGuards.h |
| 测试目标 | tests/contract/checkpoint/CheckpointContractTest.cpp |
| 验收命令 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CheckpointContractTest --output-on-failure |

---

## §7 验收清单

| # | 验收项 | 判定 |
|---|---|---|
| 1 | 5 必填字段对齐架构 §3.8.3 | ✅ |
| 2 | CheckpointState 枚举覆盖架构 §6.10 四种中断场景 | ✅ |
| 3 | 不退化为无限快照（冻结计划禁止条款） | ✅ |
| 4 | 与 BeliefState/Observation/ContextPacket 无字段重叠 | ✅ |
| 5 | 两层守卫设计与全链路一致 | ✅ |
| 6 | Design→Build 映射含三件套 | ✅ |

---

## §8 D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 5R+6O 对齐架构+ADR，枚举覆盖恢复场景，禁止字段与上下游边界清晰 |

---

## §9 风险与回退

| 风险 | 影响 | 回退 |
|---|---|---|
| working_memory_snapshot 语义歧义 | 被误解为全量 dump | 已定义为 string 引用/摘要，冻结计划明确禁止膨胀 |
| retry_counters 降为可选 | 架构原定必填 | retry_count 首次 checkpoint 无意义，RecoveryManager 可处理 nullopt |
| pending_action 允许空字符串 | 可能被误判为缺失 | L1 仅检查 has_value()，不检查 empty() |

---

## §10 下一任务建议

1. **WP03-T012-B**：实现 Checkpoint.h + CheckpointGuards.h + CheckpointContractTest.cpp。
2. **WP03-T013-D/B**：Checkpoint 恢复必需字段表 + 字段完整性校验（L3）。
