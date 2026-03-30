# WP03-T011-D：ContextPacket 必选组成块字段表

状态：Done
上游依赖：WP03-T010-D/B（ContextPacket 语义组成）、ADR-006 §6.1、架构 §3.8.5/§5.3.3

---

## §1 任务理解

### 1.1 范围

- 列出 ContextPacket 全部 4 必填 + 9 可选字段的分组、校验规则、约束条件。
- 定义三层堆叠校验设计（L1 必填 → L2 边界 → L3 字段规则），映射到 ContextPacketGuards.h。
- 输出 Design→Build 映射。

### 1.2 排除项

- 不修改 ContextPacket.h 结构定义（T010 已冻结）。
- 不引入 ADR-006 §6.1 之外的新槽位。
- 不涉及 PromptComposeResult、Observation、BeliefState 等跨域对象。

---

## §2 约束与边界

### 2.1 直接约束（可追溯）

| 约束 | 来源 |
|---|---|
| 4 必填字段：request_id, user_turn, current_goal_summary, recent_history | ContextPacket.h (T010-B), ADR-006 §6.1 |
| 可选 string 字段若 present 必须 non-empty | T003 §4.3 "携带内容或省略"原则 |
| 向量字段若 present 则元素不得为空字符串 | T007-B ObservationSourceGuards 一致性 |
| created_at 若 present 必须正值 | ContextPacket.h 注释, GoalContract/AgentRequest 一致性 |
| tags 若 present 必须非空向量且无空串 | 全链路 tags 校验统一模式 |
| 三层堆叠校验：L1 必填 → L2 边界 → L3 字段规则 | GoalContractGuards.h (T005-B) 模式 |

### 2.2 边界与非目标

- **非目标**：不修改 ContextPacket 结构，不添加新字段。
- **非目标**：不实现 ContextOrchestrator 的组装逻辑。
- **非目标**：不涉及 token_budget_report 的内容格式校验（该字段内容格式由 ContextOrchestrator 定义）。

### 2.3 前置依赖

| 前置 | 状态 |
|---|---|
| WP03-T010-D/B (ContextPacket 语义组成) | Done |
| ADR-006 (ContextOrchestrator vs PromptComposer) | Frozen |
| GuardCommon.h (has_non_empty_value) | Available |

---

## §3 研究证据链

### 3.1 本地证据

| # | 文档 | 相关内容 |
|---|---|---|
| L1 | ContextPacket.h (T010-B) | 4 必填 + 9 可选字段定义，含注释约束 |
| L2 | T010 交付物 | ADR-006 §6.1 十槽位覆盖确认，24 类禁止字段 |
| L3 | ADR-006 §6.1 | 10 类语义槽定义，前 3 + request_id 必填 |
| L4 | 架构 §5.3.3 | Token 不足优先保留 goal > constraints > latest_observation |
| L5 | GoalContractGuards.h (T005-B) | 三层堆叠模式参考 |
| L6 | GuardCommon.h | has_non_empty_value() 工具函数 |
| L7 | AgentRequestGuards.h (T003-B) | 可选字段非空校验、RuntimeBudget 维度校验模式 |

### 3.2 外部参考

| # | 来源 | 启发 |
|---|---|---|
| E1 | Microsoft Semantic Kernel — ContextVariables | required/optional field separation pattern |
| E2 | Pact Contract Testing | Consumer-driven field presence and type constraint validation |
| E3 | LangChain RunnableConfig | "present-or-omit" pattern for optional metadata fields |

### 3.3 可落地启发

1. 三层堆叠与 T005 GoalContractGuards 完全对齐，保持全链路一致性。
2. "携带内容或省略"原则对所有可选 string 字段统一适用。
3. 向量字段校验需检查两层：向量本身非空 + 元素非空。
4. created_at 正值约束是通用模式（AgentRequest、GoalContract、BeliefState 均已实现）。
5. tags 校验是全链路统一模式，无需额外设计。

---

## §4 方案对比与决策

### 方案 A：三层堆叠（与 T005 一致）

- L1：4 必填字段存在性 + 非空校验
- L2：继承 L1 + created_at 正值 + recent_history 首轮语义
- L3：继承 L2 + 可选 string 非空 + 向量元素非空 + tags 合法性

### 方案 B：两层扁平（L1 必填 + L2 全部规则）

- L1：必填字段存在性
- L2：所有其他规则混合

### 决策

**选择方案 A**。理由：
1. 与 GoalContractGuards (T005)、AgentRequestGuards (T003) 一致，保持全链路三层堆叠统一风格。
2. 消费者可根据需要选择调用层级（L1 快速检查 vs L3 完整校验）。
3. 外部参考 E2 (Pact) 支持分层 contract validation 最佳实践。

---

## §5 最终产出

### 5.1 必填字段分组（4 项）

| # | 字段名 | 类型 | ADR-006 槽位 | 校验规则 | 校验层 |
|---|---|---|---|---|---|
| R1 | request_id | string | 溯源标识 | 必须 present 且 non-empty | L1 |
| R2 | user_turn | string | 槽位 1 | 必须 present 且 non-empty | L1 |
| R3 | current_goal_summary | string | 槽位 2 | 必须 present 且 non-empty | L1 |
| R4 | recent_history | vector\<string\> | 槽位 3 | 必须 present（首轮可为空向量）| L1 |

### 5.2 可选字段分组（9 项）

| # | 字段名 | 类型 | ADR-006 槽位 | 校验规则 | 校验层 |
|---|---|---|---|---|---|
| O1 | summary_memory | string | 槽位 4 | 若 present 必须 non-empty | L3 |
| O2 | retrieval_evidence | vector\<string\> | 槽位 5 | 若 present 必须非空向量且元素 non-empty | L3 |
| O3 | latest_observation_digest_summary | string | 槽位 6 | 若 present 必须 non-empty | L3 |
| O4 | active_tools | vector\<string\> | 槽位 7 | 若 present 必须非空向量且元素 non-empty | L3 |
| O5 | policy_digest | string | 槽位 8 | 若 present 必须 non-empty | L3 |
| O6 | token_budget_report | string | 槽位 9 | 若 present 必须 non-empty | L3 |
| O7 | belief_state_summary | string | 槽位 10 | 若 present 必须 non-empty | L3 |
| O8 | created_at | int64 | 通用元数据 | 若 present 必须 > 0 | L2 |
| O9 | tags | vector\<string\> | 通用元数据 | 若 present 必须非空向量且元素 non-empty | L3 |

### 5.3 三层堆叠校验设计

#### Layer 1：必填字段存在性校验（validate_context_packet_required_fields）

| 规则 | 校验内容 |
|---|---|
| R1-check | request_id.has_value() && !request_id->empty() |
| R2-check | user_turn.has_value() && !user_turn->empty() |
| R3-check | current_goal_summary.has_value() && !current_goal_summary->empty() |
| R4-check | recent_history.has_value()（允许空向量，首轮场景）|

#### Layer 2：边界约束校验（validate_context_packet_boundary）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 R1-R4 校验 |
| O8-boundary | created_at 若 present 必须 > 0 |

#### Layer 3：字段规则校验（validate_context_packet_field_rules）

| 规则 | 校验内容 |
|---|---|
| 继承 L2 | 全部 L1 + L2 校验 |
| O1-rule | summary_memory 若 present 必须 non-empty |
| O2-rule | retrieval_evidence 若 present 必须非空向量且每个元素 non-empty |
| O3-rule | latest_observation_digest_summary 若 present 必须 non-empty |
| O4-rule | active_tools 若 present 必须非空向量且每个元素 non-empty |
| O5-rule | policy_digest 若 present 必须 non-empty |
| O6-rule | token_budget_report 若 present 必须 non-empty |
| O7-rule | belief_state_summary 若 present 必须 non-empty |
| O9-rule | tags 若 present 必须非空向量且每个元素 non-empty |

### 5.4 禁止字段（继承 T010）

继承 T010 交付物的 24 类禁止字段，不在 Guards 中引入。

### 5.5 演进建议

- 若后续需要 token_budget_report 内容格式校验，可在 L3 追加结构化解析规则。
- 若 retrieval_evidence 引入 source_type 标注，可在 L3 追加引用标注校验。

---

## §6 Design→Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | contracts/include/context/ContextPacketGuards.h |
| 测试目标 | tests/contract/context/ContextPacketFieldContractTest.cpp |
| 验收命令 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ContextPacketFieldContractTest --output-on-failure |

---

## §7 验收清单

| # | 验收项 | 判定 |
|---|---|---|
| 1 | 4 必填字段均有明确 L1 校验规则 | ✅ |
| 2 | 9 可选字段均有 L2/L3 校验规则 | ✅ |
| 3 | 三层堆叠设计与 T005 一致 | ✅ |
| 4 | Design→Build 映射含三件套 | ✅ |

---

## §8 D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 字段表完整覆盖 4R + 9O，三层堆叠设计与全链路一致，Build 映射完整 |

---

## §9 风险与回退

| 风险 | 影响 | 回退 |
|---|---|---|
| recent_history 空向量在首轮被误拒 | L1 校验失败 | L1 仅检查 has_value()，不检查 size() |
| token_budget_report 格式校验缺失 | 内容不可解析 | L3 仅检查 non-empty，格式交由 ContextOrchestrator |

---

## §10 下一任务建议

1. **WP03-T011-B**：实现 ContextPacketGuards.h 三层堆叠校验 + ContextPacketFieldContractTest.cpp（4 正 + N 负例）。
2. **WP03-T012-D/B**：Checkpoint 最小恢复语义定义与实现。
