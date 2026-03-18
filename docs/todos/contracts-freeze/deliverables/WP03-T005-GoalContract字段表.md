# WP03-T005 GoalContract 字段表与约束规则

最近更新时间：2026-03-17
任务状态：Done
任务编号：WP03-T005
上游输入：WP03-T004 GoalContract 语义说明（T004-D/B 已 Done）

## 1. 任务理解

本任务只处理 WP03-T005：列出 GoalContract 必填字段与约束表达，交付"GoalContract 字段表"。

本任务不处理：

1. GoalContract 职责边界定义（WP03-T004，已 Done）。
2. Observation / ObservationDigest 字段（WP03-T006/T007/T008）。
3. ContextPacket / Checkpoint 字段（WP03-T010 及后续）。
4. 多 Agent 子目标分解实现（MultiAgentCoordinator 子域）。

## 2. 约束与边界

### 2.1 直接约束（可追溯）

1. T004-D §3：GoalContract 仅承载五职责——目标描述、成功判据、约束集合、预算声明、审批策略。
2. T004-D §4.1：必填字段 6 项（goal_id/request_id/goal_description/success_criteria/status/created_at）。
3. T004-D §4.2：可选字段 7 项（constraints/approval_policy/budget_override/priority/parent_goal_id/deadline_at/tags）。
4. T004-D §4.3：禁止字段 6 类（Plan/Runtime/Provider/结果/多 Agent/消息装配）。
5. T004-D §4.4：ApprovalPolicy 枚举含 Unspecified/Auto/RequireConfirm；GoalStatus 枚举含 Unspecified/Active/Achieved/Failed/Cancelled。
6. WP02-T007：RuntimeBudget 5 维度已冻结（max_tokens/max_turns/max_tool_calls/max_latency_ms/max_replan_count），不扩展。
7. WP02-T010：时间字段为毫秒时间戳，deadline_at 为执行判定主语义。
8. WP02-T012：所有枚举必须包含显式 Unspecified 哨兵值。
9. WP02-T002/T003：新增优于修改，可选改必填为高风险；字段若存在则必须承载有意义内容。
10. T003-D §4.3 规则模式：可选字段若存在必须非空（"carry meaningful content or omit"）。
11. T004-B 已冻结 Layer 1（必填校验）+ Layer 2（边界守卫），T005-B 在此基础上补齐 Layer 3。

### 2.2 边界与非目标

边界：

1. 仅定义 GoalContract 字段级约束规则，不修改已冻结的必填/可选分组。
2. 仅冻结字段合法性规则，不冻结序列化格式。
3. 仅覆盖单 Agent 场景字段约束，多 Agent 级联规则不在此冻结。

非目标：

1. 不修改 GoalContract.h 结构体定义（已在 T004-B 冻结）。
2. 不引入新字段或重新分组。
3. 不定义 Plan/Runtime/Provider 字段。

### 2.3 前置依赖检查

1. T004-D/B 已 Done：GoalContract 语义边界冻结，GoalContract.h + GoalContractGuards.h 已交付。
2. T003-D/B 已 Done：AgentRequest 字段规则模式已建立，可作为 T005 参考模板。
3. WP02 冻结包 Done：RuntimeBudget、枚举规则、时间语义已冻结。
4. 当前无阻塞项。

## 3. 研究证据链

### 3.1 本地证据清单

| 编号 | 证据来源 | 关键结论 |
|---|---|---|
| L1 | T004-D §4.1-4.2 | GoalContract 6 必填 + 7 可选字段定义 |
| L2 | T004-D §4.3 | 6 类禁止字段清单 |
| L3 | T004-D §4.4 | ApprovalPolicy 和 GoalStatus 枚举值域 |
| L4 | T003-D §4.3 | "carry meaningful content or omit"规则模式 |
| L5 | T003-B Guards | validate_agent_request_field_rules 三层堆叠模式 |
| L6 | WP02-T007 | RuntimeBudget 5 维度冻结，各维度 > 0 |
| L7 | WP02-T010 | 时间字段毫秒语义 |
| L8 | WP02-T012 | 枚举 Unspecified 哨兵规则 |

### 3.2 外部参考清单

| 编号 | 参考来源 | 关键结论 |
|---|---|---|
| E1 | Martin Fowler — Contract Test (2011/2018) | 契约测试验证格式而非数据，防止契约漂移 |
| E2 | Pact.io — Consumer-Driven Contract Testing | 消费者驱动的契约测试隔离验证 |
| E3 | Bertrand Meyer — Design by Contract | 前置条件/后置条件/不变量三级守卫 |

### 3.3 对本任务的可落地启发

1. GoalContract 字段规则应复用 T003 模式：可选字段若存在则非空、数值若存在则为正。
2. budget_override 复用 RuntimeBudget 5 维度检查，逻辑与 AgentRequest.runtime_budget 一致。
3. tags 合法性规则与 AgentRequest.tags 一致：非空向量 + 无空字符串。
4. 字段校验器应以三层堆叠（required → boundary → field rules）保证向后兼容。
5. 契约测试覆盖"格式正确性"而非"数据正确性"，每条规则需正/负双向覆盖。

## 4. 方案对比与决策

### 4.1 方案 A：三层堆叠（Layer 3 字段规则）——推荐

设计方式：在已冻结的 Layer 1（必填校验）+ Layer 2（边界守卫）之上新增 Layer 3（validate_goal_contract_field_rules），继承前两层并追加可选字段合法性检查。

优点：

1. 与 T003-B AgentRequest 字段规则模式完全一致。
2. 向后兼容：不修改已冻结的 Layer 1/2 函数签名。
3. 消费者可按需选择调用层级（Layer 1/2/3）。

缺点：

1. 三层调用链在极端场景下有微小性能开销（可忽略）。

### 4.2 方案 B：合并为单一校验函数

设计方式：把所有字段规则合并到 validate_goal_contract_boundary 中。

优点：调用路径短。

缺点：破坏已冻结的 Layer 2 函数签名和行为契约，属于 breaking change。

### 4.3 决策

采用方案 A。取舍理由：方案 A 与 T003 模式一致，且不修改已冻结代码。

## 5. 最终产出

### 5.1 GoalContract 字段分组

1. 目标语义字段组（goal semantics）：goal_id, request_id, goal_description, success_criteria, status, created_at。
2. 约束字段组（constraints）：constraints, approval_policy, priority。
3. 预算字段组（budget）：budget_override, deadline_at。
4. 关联字段组（correlation）：parent_goal_id, tags。

### 5.2 GoalContract 必填字段约束规则表

| 字段名 | 分组 | 约束规则 | 校验层 | 违规时行为 |
|---|---|---|---|---|
| goal_id | 目标语义 | 必须存在且非空字符串 | Layer 1 | 拒绝，返回明确错误 |
| request_id | 目标语义 | 必须存在且非空字符串 | Layer 1 | 拒绝，返回明确错误 |
| goal_description | 目标语义 | 必须存在且非空字符串 | Layer 1 | 拒绝，返回明确错误 |
| success_criteria | 目标语义 | 必须存在且非空字符串 | Layer 1 | 拒绝，返回明确错误 |
| status | 目标语义 | 必须存在且不为 Unspecified；值域 [Active, Achieved, Failed, Cancelled] | Layer 1 + Layer 2 | 拒绝，返回明确错误 |
| created_at | 目标语义 | 必须存在且为正整数（毫秒时间戳） | Layer 1 | 拒绝，返回明确错误 |

### 5.3 GoalContract 可选字段约束规则表

| 字段名 | 分组 | 约束规则 | 校验层 | 违规时行为 |
|---|---|---|---|---|
| constraints | 约束 | 若存在则必须非空字符串（"carry content or omit"） | Layer 3 | 拒绝，返回明确错误 |
| approval_policy | 约束 | 若存在则值域 [Unspecified, Auto, RequireConfirm]；Unspecified 表示"未设置"允许通过 | Layer 2 | 拒绝，返回明确错误 |
| budget_override | 预算 | 若存在则各已设维度必须 > 0（max_tokens/max_turns/max_tool_calls/max_latency_ms/max_replan_count） | Layer 3 | 拒绝，返回明确错误 |
| priority | 约束 | 若存在则必须 > 0 | Layer 2 | 拒绝，返回明确错误 |
| parent_goal_id | 关联 | 若存在则必须非空字符串 | Layer 3 | 拒绝，返回明确错误 |
| deadline_at | 预算 | 若存在则必须为正整数且 >= created_at | Layer 2 | 拒绝，返回明确错误 |
| tags | 关联 | 若存在则：(1) 向量非空；(2) 不含空字符串 | Layer 3 | 拒绝，返回明确错误 |

### 5.4 三层校验堆叠设计

| 校验层 | 函数名 | 来源 | 覆盖范围 |
|---|---|---|---|
| Layer 1 | validate_goal_contract_required_fields | T004-B 已冻结 | 6 必填字段存在性 + 非空 |
| Layer 2 | validate_goal_contract_boundary | T004-B 已冻结 | Layer 1 + 枚举值域 + deadline_at/priority 边界 |
| Layer 3 | validate_goal_contract_field_rules | T005-B 新增 | Layer 2 + 可选字符串非空 + tags 合法性 + budget 维度正值 |

### 5.5 Budget Override 维度约束规则

复用 WP02-T007 RuntimeBudget 5 维度，与 AgentRequest.runtime_budget（T003-B）规则一致：

| 维度 | 约束 | 依据 |
|---|---|---|
| max_tokens | 若存在则 > 0 | WP02-T007 |
| max_turns | 若存在则 > 0 | WP02-T007 |
| max_tool_calls | 若存在则 > 0 | WP02-T007 |
| max_latency_ms | 若存在则 > 0 | WP02-T007 |
| max_replan_count | 若存在则 > 0 | WP02-T007 |

### 5.6 明确禁止字段（复用 T004-D）

以下字段或同类语义不得进入 GoalContract（与 T004-D §4.3 一致，此处确认无变更）：

1. Plan/执行步骤字段：plan_graph、step_list、action_sequence、tool_sequence。
2. Runtime 内部状态：fsm_state、retry_counters、backoff_ms、circuit_state、checkpoint_ref。
3. Provider 私有字段：rendered_prompt、model_provider_args、vendor_tool_schema。
4. 执行结果字段：observation、observation_digest、belief_state、agent_result。
5. 多 Agent 子域字段：worker_task_list、lease_id、multi_agent_request。
6. 消息装配字段：final_messages、prompt_bundle、token_usage。

### 5.7 字段演进建议（兼容性）

1. 新增字段默认走可选，避免将可选字段直接升级为必填。
2. 不修改既有字段语义；若语义变化，按 breaking 候选进入专门评审。
3. budget_override 维度扩展需回到 WP02-T007 评审流程。

### 5.8 与后续任务映射

| 后续任务 | 消费内容 |
|---|---|
| WP03-T006/T007 | Observation 消费 GoalContract 字段规则验证模式 |
| WP03-T010/T011 | ContextPacket 消费 GoalContract 摘要字段规则 |
| WP03-T015 | 端到端冒烟测试消费 GoalContract 全三层校验 |

## 6. Design→Build 映射

### 6.1 代码目标

contracts/include/agent/GoalContractGuards.h：新增 validate_goal_contract_field_rules() 函数（Layer 3）。

### 6.2 测试目标

tests/contract/agent/GoalContractFieldContractTest.cpp：覆盖 Layer 3 字段规则的正例 + 负例。

### 6.3 验收命令

```bash
cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R GoalContractFieldContractTest --output-on-failure
```

## 7. 验收清单

1. 已产出 GoalContract 字段表，含必填与可选字段约束规则。
2. 三层校验堆叠设计与 T003 模式一致。
3. budget_override 5 维度约束规则与 RuntimeBudget 冻结一致。
4. 禁止字段复用 T004-D 清单，无遗漏。
5. 结论可追溯到 T004-D/T003-D/WP02。

## 8. D Gate 结果

| 检查项 | 结果 |
|---|---|
| 字段分组是否与 T004-D 一致 | ✅ 4 组 |
| 必填字段约束规则是否覆盖 6 项 | ✅ |
| 可选字段约束规则是否覆盖 7 项 | ✅ |
| budget_override 维度规则是否与 WP02-T007 一致 | ✅ 5 维 |
| 三层堆叠设计是否兼容已冻结 Layer 1/2 | ✅ |
| 是否达到进入 -B 条件 | ✅ 通过 |

## 9. 风险与回退

1. 风险：可选字段合法性校验与上游 AgentRequest 不一致。
回退：GoalContract 字段规则严格复用 T003 模式，保持一致性。
2. 风险：budget_override 维度扩展导致 T005 字段表过时。
回退：维度扩展走 WP02-T007 评审流程，T005 字段表随之更新。
3. 风险：tags 规则过严导致空 tags 场景无法通过。
回退：tags 为 nullopt 时视为未提供（通过），仅 present-but-invalid 时拒绝。

## 10. 下一任务建议

1. WP03-T006-D：定义 Observation 统一折叠语义。
2. WP03-T006-B：新增 Observation 契约对象。
