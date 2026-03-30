# WP04-T008-D：ReflectionDecision 字段表

> 版本：1.0 | 日期：2026-03-18 | 状态：Done
> 任务编号：WP04-T008-D
> 上游输入：WP04-T007-D/B、ADR-007 §5.1、docs/architecture/DASSALL_Agent_architecture.md §3.8/§6.10、WP02-T009/T010/T012、WP03-T013 字段表模式

---

## 1. 任务识别

### 1.1 范围

- 把 T007 已冻结的 ReflectionDecision 对象骨架下沉为字段级清单、字段规则和最小组合约束。
- 定义 ReflectionDecision 的三层校验堆叠：L1 必填、L2 边界、L3 字段规则。
- 将 L3 映射到 ReflectionDecisionGuards.h 的最小增量实现和 contract test。

### 1.2 排除项

- 不新增 ReflectionDecision 字段。
- 不改写 T007 已冻结的对象定位、决策枚举和调度禁区结论。
- 不扩张到 RecoveryRequest/RecoveryOutcome。
- 不重新实现 ADR-007 的调度字段禁区目录；继续复用 RecoveryBoundaryGuards。
- 不引入运行时执行策略，例如 retry policy、backoff policy、circuit breaker policy。

---

## 2. 研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T008 的直接约束 |
|---|---|---|
| L1 | WP04-T007-D §4 | ReflectionDecision 已冻结为 3 必填 + 6 可选槽位，T008 只能补字段规则，不能改对象边界 |
| L2 | ADR-007 §5.1 | ReflectionDecision 应包含 decision_kind、rationale、confidence、relevant_observation_refs 与 hint 类认知信号，不得混入调度细节 |
| L3 | docs/architecture/DASSALL_Agent_architecture.md §3.8 / Reflection Engine 表 | ReflectionDecision 是 ReflectionEngine 的输出对象，承担 continue、retry_step、replan、abort_safe 等反思结论 |
| L4 | docs/architecture/DASSALL_Agent_architecture.md §11.1 附近 | 反思链路输出显式自评信号（confidence、clarification_needed、replan_hint），说明 confidence / hint 类字段属于合法认知元数据 |
| L5 | contracts/include/observation/Observation.h 注释 | Observation.observation_id 是 ReflectionDecision.relevant_observation_refs 的合法引用目标，说明引用字段应保持可追溯且非空 |
| L6 | contracts/include/checkpoint/ReflectionDecisionGuards.h | T007 已冻结 L1/L2：request_id、decision_kind、rationale 必填；goal_id/hint_ref 非空；confidence 范围；created_at 正值 |
| L7 | WP03-T013-D §5.3 与 WP04-T003-D | 仓内字段表模式统一采用“L3 继承 L2 + tags/vector hygiene + 最小组合规则”，T008 应沿用同一 guard 风格 |
| L8 | docs/plans/DASALL_contracts冻结实施计划.md | contracts 设计遵循“先语义、后字段、再 guard/test”，并要求 ReflectionDecision 不混入执行控制字段 |
| L9 | docs/plans/DASALL_工程落地实现步骤指引.md | 关键结构体字段必须评审并锁定版本，统一追踪字段必须完整 |

### 2.2 外部参考清单

| # | 来源 | 对 T008 的映射 |
|---|---|---|
| E1 | Microsoft Azure Retry Pattern | retry 只应在理解完整失败上下文的高层控制处实现，支持 T008 继续把调度字段排除在 ReflectionDecision 之外 |
| E2 | Ian Robinson / Martin Fowler, Consumer-Driven Contracts | 接收侧应做 just-enough validation，围绕消费者真实依赖字段做有界校验，支持 T008 只实现最小必要字段规则 |
| E3 | Anthropic, Building Effective Agents | 有效 agent 倾向于简单、可组合、可测试的 guardrail 设计，支持 T008 用最小 L3 校验而不是把对象做成复杂状态机 |

### 2.3 对本任务的可落地启发

1. T008 的职责不是再讨论 ReflectionDecision 是什么，而是把已经冻结的 9 个字段变成可执行的字段规则表。
2. L3 应只补充 T007 没有覆盖的字段 hygiene：vector 非空、元素非空、必要时唯一，不重复实现 L1/L2。
3. `confidence` 的字段规则不能只做区间检查，还应显式拒绝非有限值，避免 NaN 逃过范围比较。
4. `relevant_observation_refs` 承担证据引用角色，应保持“有意义的引用集合”语义：present 时非空、元素非空、不可重复。
5. 调度字段拦截仍属于 ADR-007 边界守卫，T008-B 只需在字段级 contract test 中确认该禁区继续生效。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 ReflectionDecision 全字段清单和层次归属 | T007-D、ReflectionDecision.h | 本文件 §4 | 3 必填 + 6 可选全部列出，且不新增字段 | 若出现新字段，回退到 T007 已冻结集合 |
| D2 | 明确每个字段的字段级规则与归属层（L1/L2/L3） | ADR-007 §5.1、ReflectionDecisionGuards.h、Observation.h | 本文件 §5 | 每个字段都映射到明确规则或类型保证 | 若规则与 T007 冲突，优先回退到 T007/L1-L2 已冻结口径 |
| D3 | 定义 T008 专属最小组合规则 | ADR-007 §5.1、CDC just-enough validation、仓内字段表模式 | 本文件 §5.4 | 至少 1 条可程序化组合规则，且不跨包扩张 | 若组合规则依赖运行时语义，删除并回退为字段 hygiene |
| D4 | 输出 T008 的 Design→Build 三件套 | WP04 TODO、现有 field-test 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整 | 若需要新增非 T008 文件族，判定越界并回退 |
| D5 | 形成 D Gate 结论 | D1-D4 | 本文件 §7 | Gate 为 PASS/Blocked 二值 | 若字段表未闭合或三件套缺失，则 Blocked |

---

## 4. 字段全集与分组

ReflectionDecision 固定由 3 个必填字段 + 6 个可选字段组成，总数锁定为 9。

### 4.1 必填字段（3 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `request_id` | `std::optional<std::string>` | 对本轮请求的追溯 ID | present 且 non-empty | L1 |
| `decision_kind` | `std::optional<ReflectionDecisionKind>` | 反思建议类型 | present；非 `Unspecified`；值在已知枚举范围内 | L1 + L2 |
| `rationale` | `std::optional<std::string>` | 失败语义解释与建议理由 | present 且 non-empty | L1 |

### 4.2 可选字段（6 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `goal_id` | `std::optional<std::string>` | 多目标追溯引用 | present 时 non-empty | L2 |
| `confidence` | `std::optional<float>` | 建议可信度 | present 时必须为有限值，且在 $[0,1]$ 内 | L2 + L3 |
| `relevant_observation_refs` | `std::optional<std::vector<std::string>>` | 支撑该建议的 Observation 引用集合 | present 时：向量非空、元素 non-empty、元素唯一 | L3 |
| `hint_ref` | `std::optional<std::string>` | 认知提示资产引用 | present 时 non-empty | L2 |
| `created_at` | `std::optional<std::int64_t>` | 生成时间戳 | present 时必须为正值 | L2 |
| `tags` | `std::optional<std::vector<std::string>>` | 审计与检索标签 | present 时：向量非空、元素 non-empty | L3 |

---

## 5. 字段级规则总表

### 5.1 通用规则

| 规则编号 | 规则 | 适用字段 |
|---|---|---|
| R1 | required string 字段必须 present 且 non-empty | `request_id`, `rationale` |
| R2 | required enum 字段必须 present，且不得为 `Unspecified` | `decision_kind` |
| R3 | enum 字段必须落在已知枚举范围内 | `decision_kind` |
| R4 | optional string 字段若 present，则必须 non-empty | `goal_id`, `hint_ref` |
| R5 | optional timestamp 若 present，则必须为正值 | `created_at` |
| R6 | optional confidence 若 present，则必须为有限值且满足 $0.0 \le x \le 1.0$ | `confidence` |
| R7 | optional vector 若 present，则向量本身必须非空 | `relevant_observation_refs`, `tags` |
| R8 | optional vector 若 present，则元素必须 non-empty | `relevant_observation_refs`, `tags` |
| R9 | 具有引用集合语义的 vector 若 present，则元素必须唯一 | `relevant_observation_refs` |

### 5.2 三层堆叠校验设计

#### Layer 1：必填字段存在性（T007-B 已实现）

| 规则 | 校验内容 |
|---|---|
| L1-R1 | `request_id.has_value() && !request_id->empty()` |
| L1-R2 | `decision_kind.has_value() && decision_kind != Unspecified` |
| L1-R3 | `rationale.has_value() && !rationale->empty()` |

#### Layer 2：边界约束（T007-B 已实现）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 L1 规则 |
| L2-R1 | `decision_kind` 位于已知枚举范围 |
| L2-R2 | `goal_id` 若 present 则 non-empty |
| L2-R3 | `confidence` 若 present 则位于 $[0,1]$ |
| L2-R4 | `hint_ref` 若 present 则 non-empty |
| L2-R5 | `created_at` 若 present 则为正值 |

#### Layer 3：字段规则（T008-B 新增）

| 规则 | 校验内容 | 来源 |
|---|---|---|
| 继承 L2 | 全部 L1 + L2 规则 | 仓内统一 guard 堆叠模式 |
| L3-R1 | `confidence` 若 present，则必须是有限值；NaN/Inf 一律拒绝 | 浮点 hygiene，避免范围比较漏检 |
| L3-R2 | `relevant_observation_refs` 若 present，则必须为非空向量 | Observation 引用集合需要“有意义内容” |
| L3-R3 | `relevant_observation_refs` 每个元素必须 non-empty | 证据引用不可为空 |
| L3-R4 | `relevant_observation_refs` 元素必须唯一 | 引用集合语义，不应重复计数同一 Observation |
| L3-R5 | `tags` 若 present，则必须为非空向量且元素 non-empty | 仓内统一 tags 模式 |

### 5.3 字段解释

1. `confidence` 的区间规则在 T007 已有基础，但 T008 负责把“有限值”补齐为字段级 hygiene，避免 `NaN` 漏过简单比较。
2. `relevant_observation_refs` 不要求在 contracts 层验证引用对象是否真实存在；T008 只保证其结构合法且适合作为证据引用集合。
3. `hint_ref` 继续保持 opaque reference，不在 T008 展开为 plan patch / clarification payload 结构。

### 5.4 最小组合规则

| 规则编号 | 非法组合 | 判定原因 |
|---|---|---|
| C1 | `relevant_observation_refs` present 且包含重复引用 | 同一 Observation 被重复计入会扭曲证据集合的审计语义 |

说明：

- T008 不增加 `decision_kind -> hint_ref`、`decision_kind -> confidence` 等运行时组合约束，因为 ADR-007 只冻结建议语义，不冻结恢复执行策略。
- 这符合 consumer-driven contracts 的 just-enough validation 原则：只验证当前消费者真实依赖、且能稳定自动化的字段规则。

---

## 6. Design→Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | `contracts/include/checkpoint/ReflectionDecisionGuards.h`（新增 `validate_reflection_decision_field_rules`） |
| 测试目标 | `tests/contract/checkpoint/ReflectionDecisionFieldContractTest.cpp` |
| 验收命令 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ReflectionDecisionFieldContractTest --output-on-failure` |

---

## 7. D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 9 个字段已全覆盖，L1/L2/L3 分层闭合，T008 专属规则限定在字段 hygiene 与最小组合约束内，未越界到 T009/T010 |

**Gate 结论：PASS — 可进入 WP04-T008-B**

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T008 来源与三件套 | docs/todos/contracts/WP-04-边界对象TODO.md | WP04-T008 行 |
| ReflectionDecision 对象边界 | docs/todos/contracts/deliverables/WP04-T007-ReflectionDecision语义说明.md | §4/§5 |
| ReflectionDecision 调度禁区 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §5.1 |
| ReflectionEngine 输出角色 | docs/architecture/DASSALL_Agent_architecture.md | Reflection Engine 表、反思链路章节 |
| ReflectionDecision 工程归属 | docs/architecture/DASALL_Engineering_Blueprint.md | `policy/` 与 ReflectionEngine 组件表 |
| 字段冻结纪律 | docs/plans/DASALL_contracts冻结实施计划.md | ReflectionDecision 相关约束、语义先于字段 |
| 统一追踪字段要求 | docs/plans/DASALL_工程落地实现步骤指引.md | 字段锁定与追踪字段要求 |
| Observation 引用语义 | contracts/include/observation/Observation.h | `observation_id` 注释 |
| 仓内字段表模式 | docs/todos/contracts/deliverables/WP03-T013-Checkpoint字段表.md | §5.3 |

### 8.2 外部业界参考

1. Microsoft Azure Architecture Center, Retry Pattern  
   结论：retry logic 只应在理解完整失败上下文的控制层实现，避免多层重试与职责混淆。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/retry
2. Ian Robinson / Martin Fowler, Consumer-Driven Contracts  
   结论：接收侧应做 just-enough validation，围绕真实消费需求做有界校验，而不是全量过度约束。  
   参考：https://martinfowler.com/articles/consumerDrivenContracts.html
3. Anthropic, Building Effective Agents  
   结论：有效 agent 设计应保持简单、可组合，并通过显式 guardrails 和测试保持可靠性。  
   参考：https://www.anthropic.com/engineering/building-effective-agents