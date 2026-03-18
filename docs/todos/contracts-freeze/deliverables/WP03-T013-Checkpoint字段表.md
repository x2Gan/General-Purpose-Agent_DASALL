# WP03-T013-D：Checkpoint 恢复必需字段表

状态：Done
上游依赖：WP03-T012-D/B（Checkpoint 语义说明 + L1/L2 守卫）、ADR-007 §5.2、架构 §3.8.3/§6.10

---

## §1 任务理解

### 1.1 范围

- 列出 Checkpoint 全部 5 必填 + 6 可选字段的分组、校验规则、约束条件。
- 定义三层堆叠校验设计（L1 必填 → L2 边界 → L3 字段规则），L3 映射到 CheckpointGuards.h 新增函数。
- 明确恢复场景下的字段完整性约束：等待类状态的 pending_action 一致性。
- 输出 Design→Build 映射。

### 1.2 排除项

- 不修改 Checkpoint.h 结构定义（T012 已冻结）。
- 不修改 L1/L2 守卫（T012-B 已冻结）。
- 不引入架构 §3.8.3 之外的新字段。
- 不定义 RecoveryManager 的恢复决策逻辑（属 ADR-007 运行态）。
- 不定义持久化/序列化策略（属 runtime 实现）。

---

## §2 约束与边界

### 2.1 直接约束（可追溯）

| 约束 | 来源 |
|---|---|
| 5 必填字段：checkpoint_id, state, step_id, working_memory_snapshot, pending_action | Checkpoint.h (T012-B), 架构 §3.8.3 |
| 6 可选字段：request_id, goal_id, belief_state_ref, retry_count, created_at, tags | Checkpoint.h (T012-B), T012-D §5.5 |
| L1/L2 守卫已冻结：必填存在性 + 枚举范围 + 可选 string 非空 + created_at 正值 | CheckpointGuards.h (T012-B) |
| 三层堆叠校验设计 | T005 GoalContractGuards、T011 ContextPacketGuards 统一模式 |
| tags 校验统一模式：非空向量 + 无空串 | AgentRequest/GoalContract/BeliefState/ContextPacket 全链路一致 |
| 等待类状态的 pending_action 必须记录等待内容 | 架构 §6.10："恢复时必须优先读取 Checkpoint 和 pending_action" |
| RecoveryManager 输入含 checkpoint + retry_counters | ADR-007 §5.2 |

### 2.2 边界与非目标

- **非目标**：不修改 Checkpoint 结构，不添加新字段。
- **非目标**：不修改 L1/L2 守卫函数。
- **非目标**：不实现 RecoveryManager 的恢复逻辑。
- **非目标**：不定义 working_memory_snapshot 的内容格式校验（该字段内容格式由 Runtime 定义）。

### 2.3 前置依赖

| 前置 | 状态 |
|---|---|
| WP03-T012-D/B (Checkpoint 语义说明 + L1/L2 守卫) | Done |
| ADR-007 (ReflectionEngine vs RecoveryManager) | Frozen |
| 架构 §3.8.3 (检查点与恢复契约) | Frozen |
| GuardCommon.h (has_non_empty_value) | Available |

---

## §3 研究证据链

### 3.1 本地证据

| # | 文档 | 相关内容 |
|---|---|---|
| L1 | 架构 §3.8.3 | 5 必填字段定义，"明确哪些动作已产生副作用、哪些动作仍在等待确认" |
| L2 | 架构 §6.10 | "恢复时必须优先读取 Checkpoint 和 pending_action，不从头执行" |
| L3 | ADR-007 §5.2 | RecoveryManager 输入含 checkpoint + retry_counters + runtime_budget_snapshot |
| L4 | T012-D §1.2 | "T013 负责字段级校验规则（L3），本任务仅定义 L1/L2" |
| L5 | T012-D §5.4/§5.5 | 5R+6O 完整字段清单与类型定义 |
| L6 | T012-D §5.8 | L1/L2 守卫设计规则表 |
| L7 | CheckpointGuards.h | L1 validate_checkpoint_required_fields + L2 validate_checkpoint_boundary 已实现 |
| L8 | T011 ContextPacketGuards.h | L3 模式参考：tags + 可选 string + 可选 vector |
| L9 | Checkpoint.h 注释 | pending_action "empty string allowed when no action is pending" |

### 3.2 外部参考

| # | 来源 | 启发 |
|---|---|---|
| E1 | LangGraph Checkpoint — `pending_sends` mandatory for async states | 等待类状态必须记录等待内容，否则恢复无法确定"在等什么" |
| E2 | Microsoft AutoGen — checkpoint state/pending_actions consistency | FSM 状态与 pending 动作的一致性是恢复可靠性基础 |
| E3 | Pact Contract Testing — layered validation with state-dependent rules | 状态依赖的字段校验是 consumer-driven contract 的自然延伸 |

### 3.3 可落地启发

1. 三层堆叠 L3 与 T011 ContextPacketGuards 完全对齐，保持全链路一致。
2. L2 已覆盖可选 string（request_id/goal_id/belief_state_ref）非空和 created_at 正值，L3 无需重复。
3. tags 校验是全链路统一模式，必须在 L3 补齐。
4. **状态→pending_action 语义一致性**是 Checkpoint 独有的 L3 规则：WaitingConfirm / WaitingTool / Paused 三种等待状态的 pending_action 不得为空字符串。
5. retry_count 为 uint32_t，没有额外 L3 约束（非负由类型保证）。

---

## §4 方案对比与决策

### 方案 A：三层堆叠 + 状态一致性（推荐）

- L3 继承 L2
- 新增 tags 校验（统一模式）
- 新增 state→pending_action 语义一致性校验

### 方案 B：仅 tags 校验（最小添加）

- L3 继承 L2
- 仅新增 tags 校验
- 不验证 state→pending_action 一致性

### 决策

**选择方案 A**。理由：
1. 架构 §6.10 明确要求"恢复时必须优先读取 Checkpoint 和 pending_action"，等待类状态的空 pending_action 将导致恢复丢失关键信息。
2. LangGraph 的 pending_sends 一致性校验验证了此模式的必要性。
3. 规则简单明确：仅 Paused / WaitingConfirm / WaitingTool 三个状态需要非空 pending_action，Running / Failed / Succeeded 不受此约束。
4. 与全链路统一模式一致，消费者可分层调用（L1 快速检查 vs L3 完整校验）。

---

## §5 最终产出

### 5.1 必填字段分组（5 项）

| # | 字段名 | 类型 | 架构 §3.8.3 映射 | 校验规则 | 校验层 |
|---|---|---|---|---|---|
| R1 | checkpoint_id | string | 唯一标识 | 必须 present 且 non-empty | L1 |
| R2 | state | CheckpointState | "当前状态" | 必须 present 且不为 Unspecified | L1 |
| R3 | step_id | string | "当前步骤或 step_id" | 必须 present 且 non-empty | L1 |
| R4 | working_memory_snapshot | string | "working_memory_snapshot" | 必须 present 且 non-empty | L1 |
| R5 | pending_action | string | "pending_action" | 必须 present（允许空字符串表示"无待处理"） | L1 |

### 5.2 可选字段分组（6 项）

| # | 字段名 | 类型 | 校验规则 | 校验层 |
|---|---|---|---|---|
| O1 | request_id | string | 若 present 必须 non-empty | L2 |
| O2 | goal_id | string | 若 present 必须 non-empty | L2 |
| O3 | belief_state_ref | string | 若 present 必须 non-empty | L2 |
| O4 | retry_count | uint32 | 无额外约束（非负由类型保证） | — |
| O5 | created_at | int64 | 若 present 必须 > 0 | L2 |
| O6 | tags | vector\<string\> | 若 present 必须非空向量且元素 non-empty | L3 |

### 5.3 三层堆叠校验设计

#### Layer 1：必填字段存在性校验（validate_checkpoint_required_fields，T012-B 已实现）

| 规则 | 校验内容 |
|---|---|
| R1-check | checkpoint_id.has_value() && !checkpoint_id->empty() |
| R2-check | state.has_value() && state != Unspecified |
| R3-check | step_id.has_value() && !step_id->empty() |
| R4-check | working_memory_snapshot.has_value() && !working_memory_snapshot->empty() |
| R5-check | pending_action.has_value()（允许空字符串，表示无待处理动作）|

#### Layer 2：边界约束校验（validate_checkpoint_boundary，T012-B 已实现）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 R1-R5 校验 |
| R2-boundary | state 在已知 CheckpointState 枚举范围内 [0,6] |
| O1-boundary | request_id 若 present 必须 non-empty |
| O2-boundary | goal_id 若 present 必须 non-empty |
| O3-boundary | belief_state_ref 若 present 必须 non-empty |
| O5-boundary | created_at 若 present 必须正值 |

#### Layer 3：字段规则校验（validate_checkpoint_field_rules，T013-B 新增）

| 规则 | 校验内容 | 来源 |
|---|---|---|
| 继承 L2 | 全部 L1 + L2 校验 | — |
| O6-rule | tags 若 present 必须非空向量且每个元素 non-empty | 全链路 tags 统一模式 |
| R2R5-rule | state 为 Paused / WaitingConfirm / WaitingTool 时，pending_action 必须 non-empty | 架构 §6.10：恢复必须读取 pending_action；等待类状态必须记录"在等什么" |

> **设计说明**：
> - R2R5-rule 是 Checkpoint 独有的 L3 校验。其语义来自架构 §6.10 的三种中断场景：
>   - Paused（等用户澄清）→ 必须记录等待的澄清内容。
>   - WaitingConfirm（等高风险确认）→ 必须记录等待确认的动作。
>   - WaitingTool（等异步工具返回）→ 必须记录等待的工具调用。
> - Running / Failed / Succeeded 状态的 pending_action 允许空字符串。
> - retry_count（uint32）无额外 L3 规则，非负由类型保证。

### 5.4 禁止字段（继承 T012）

继承 T012 交付物的 9 类禁止字段（认知/执行/推理/消息/模型/语义上下文/计划/多 Agent/入口），不在 Guards 中引入。

### 5.5 演进建议

- 若后续 working_memory_snapshot 引入结构化格式（如 JSON 引用），可在 L3 追加格式校验。
- 若 RecoveryManager 需要 checkpoint 间的 diff 校验，可在专用 RecoveryGuards 中实现，不扩展 CheckpointGuards。

---

## §6 Design→Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | contracts/include/checkpoint/CheckpointGuards.h（新增 validate_checkpoint_field_rules） |
| 测试目标 | tests/contract/checkpoint/CheckpointFieldContractTest.cpp |
| 验收命令 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CheckpointFieldContractTest --output-on-failure |

---

## §7 验收清单

| # | 验收项 | 判定 |
|---|---|---|
| 1 | 5 必填字段均有明确 L1 校验规则（T012 已冻结） | ✅ |
| 2 | 6 可选字段均有 L2/L3 校验规则或类型保证 | ✅ |
| 3 | 三层堆叠设计与 T011 一致 | ✅ |
| 4 | state→pending_action 一致性规则覆盖三种等待状态 | ✅ |
| 5 | tags 遵循全链路统一校验模式 | ✅ |
| 6 | Design→Build 映射含三件套 | ✅ |

---

## §8 D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 5R+6O 字段全覆盖，L3 新增 tags + state→pending_action 一致性，与全链路三层堆叠统一 |

---

## §9 风险与回退

| 风险 | 影响 | 回退 |
|---|---|---|
| state→pending_action 一致性过严 | Running 状态有时也有 pending_action | 当前设计仅约束三种等待状态，Running/Failed/Succeeded 不受限，风险可控 |
| tags 校验与其他契约不一致 | 全链路 tags 统一模式破坏 | 完全复用 T011/T005/T003 模式，风险极低 |
| L3 遗漏 retry_count 约束 | 恢复时 retry_count 缺失 | retry_count 由 RecoveryManager 处理 nullopt（= 0），不影响 checkpoint 自身完整性 |

---

## §10 下一任务建议

1. **WP03-T013-B**：实现 validate_checkpoint_field_rules + CheckpointFieldContractTest.cpp。
2. **WP03-T014-D/B**：AgentResult 最小输出语义说明 + 契约对象与输出守卫。
