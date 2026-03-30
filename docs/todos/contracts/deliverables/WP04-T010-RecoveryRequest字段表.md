# WP04-T010-D：RecoveryRequest 字段表

> 版本：1.0 | 日期：2026-03-18 | 状态：Done
> 任务编号：WP04-T010-D
> 上游输入：WP04-T009-D/B、ADR-007 §3.3/§3.4/§4/§5.2、docs/architecture/DASSALL_Agent_architecture.md §3.8.3/§6.10、WP03-T007-D/B、WP03-T013-D/B、WP02-T008、docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md

---

## 1. 任务识别

### 1.1 范围

- 把 T009 已冻结的 RecoveryRequest 对象骨架下沉为字段级清单、字段规则和最小组合约束。
- 定义 RecoveryRequest 的三层校验堆叠：L1 必填、L2 边界、L3 字段规则。
- 明确顶层 5 必填 + 2 可选字段，以及嵌套 `IdempotencyAndSideEffectReport` 的 2 必填 + 2 条件字段规则。
- 将 L3 映射到 RecoveryRequestGuards.h 的最小增量实现和 contract test。

### 1.2 排除项

- 不新增 RecoveryRequest 字段。
- 不改写 T009 已冻结的对象定位、最小槽位和顶层禁区结论。
- 不扩张到 RecoveryOutcome 或 RecoveryManager 的执行/调度策略。
- 不改写 ReflectionDecision、Observation、Checkpoint、BudgetSnapshot 既有冻结规则，只复用其已存在守卫。
- 不引入 backoff、circuit breaker、compensation plan 等运行时策略字段。

---

## 2. Phase 0：研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T010 的直接约束 |
|---|---|---|
| L1 | WP04-T009-D §4/§5 | RecoveryRequest 已冻结为 5 必填 + 2 可选槽位；T010 只能补字段规则，不能改对象边界 |
| L2 | ADR-007 §3.3/§3.4/§4/§5.2 | RecoveryManager 结合 ReflectionDecision、checkpoint、retry counters、budget、idempotency evidence 做准入裁定；因此 T010 应强化“准入证据包一致性”，而不是定义执行策略 |
| L3 | docs/architecture/DASSALL_Agent_architecture.md §3.8.3/§6.10/ADR-007 摘要 | 恢复链路必须围绕 Checkpoint、pending_action、预算和错误证据工作，不能发明第二份恢复状态 |
| L4 | WP03-T007-D §5/§6/§7 | Observation 的 source、tool_call_id、worker_task_id、request_id、goal_id 以及 ErrorSourceRef 对齐关系已冻结，可作为 RecoveryRequest 错误证据一致性的直接输入 |
| L5 | WP03-T013-D §5.3 | Checkpoint 已采用 L1/L2/L3 三层堆叠模式；RecoveryRequest 字段表应复用同一 guard 叠层方式 |
| L6 | WP02-T008 §4.2/§4.4 | BudgetSnapshot 已冻结为状态表达，`remaining=max-current` 且 reject_reason 只在超限时出现；T010 只能复用其守卫，不新增预算判定字段 |
| L7 | docs/plans/DASALL_contracts冻结实施计划.md §3.2/§6.3 | 契约演进遵循兼容优先、语义先于字段、consumer-driven just-enough validation |
| L8 | docs/plans/DASALL_工程落地实现步骤指引.md 阶段 K/L Gate | 请求追踪字段必须完整，统一链路追踪字段不能漂移；RecoveryRequest 作为恢复准入对象，嵌套 request_id/goal_id 应保持同链路一致 |

### 2.2 外部参考清单

| # | 来源 | 对 T010 的映射 |
|---|---|---|
| E1 | Microsoft Azure Architecture Center: Retry Pattern | retry 只应在理解完整失败上下文的控制层实现，且应考虑 idempotency；支持 T010 把字段规则聚焦为“准入证据包完整性” |
| E2 | Microsoft Azure Architecture Center: Compensating Transaction Pattern | 恢复/补偿依赖 durable state、可续跑信息和 idempotent commands；支持 T010 强化 checkpoint/idempotency evidence 的结构化一致性 |
| E3 | Ian Robinson / Martin Fowler: Consumer-Driven Contracts | 接收方应做 just-enough validation，并围绕真实消费者依赖构建自动化断言；支持 T010 只做 RecoveryManager 真正依赖的字段一致性校验 |
| E4 | Anthropic: Building Effective Agents | 有效 agent 应保持简单、透明、可测试；支持 T010 继续采用 lean object + layered guards + contract tests 的实现方式 |

### 2.3 对本任务的可落地启发

1. T010 的职责不是重新定义 RecoveryRequest，而是把 T009 的 7 个槽位收敛为可自动执行的字段级规则。
2. 字段规则的价值点在“证据包一致性”，包括 request/goal 追踪对齐、ErrorInfo 与 Observation 的关联对齐，以及 idempotency 证据闭合，而不是恢复策略本身。
3. RecoveryRequest 作为 RecoveryManager 的消费者契约，应只校验对准入有直接价值的字段关系，避免把运行时动作决策写进 contracts。
4. 如果 Observation 已声明存在副作用但仍允许安全重放，必须有幂等键支撑；否则 RecoveryManager 无法做出可靠准入判断。
5. 嵌套对象优先复用既有 L3 守卫，RecoveryRequest 只补“跨对象组合规则”，这样能保持兼容性并避免跨工作包扩张。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 RecoveryRequest 全字段清单和层次归属 | T009-D、RecoveryRequest.h | 本文件 §4 | 5 必填 + 2 可选字段全部列出，且不新增字段 | 若出现新字段，回退到 T009 已冻结集合 |
| D2 | 明确 `IdempotencyAndSideEffectReport` 的字段规则 | ADR-007 §5.2、Azure Retry/Compensating Transaction | 本文件 §5.2/§5.3 | 4 个字段的 required/conditional 规则闭合 | 若演变为策略 DSL，回退到 admission evidence 口径 |
| D3 | 定义 RecoveryRequest 专属最小组合规则 | WP03-T007、T009-D、工程落地追踪字段要求 | 本文件 §5.4 | 至少 3 条可程序化组合规则，且不越界到 RecoveryOutcome | 若依赖运行时策略值，删除并回退为字段一致性 |
| D4 | 输出 T010 的 Design→Build 三件套 | WP04 TODO、现有 checkpoint field-test 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整 | 若需要改动非 T010 文件族，判定越界并回退 |
| D5 | 形成 D Gate 结论 | D1-D4 | 本文件 §7 | Gate 为 PASS/Blocked 二值 | 若字段表未闭合或三件套缺失，则 Blocked |

---

## 4. 字段全集与分组

RecoveryRequest 固定由 5 个必填字段 + 2 个可选字段组成，总数锁定为 7。

### 4.1 必填字段（5 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `reflection_decision` | `std::optional<ReflectionDecision>` | ReflectionEngine 的建议结果 | present，且通过 `validate_reflection_decision_field_rules` | L1 + L3 |
| `error_info` | `std::optional<ErrorInfo>` | 本次失败的结构化错误语义 | present，且通过 `validate_error_info_required_fields` | L1 |
| `latest_observation` | `std::optional<Observation>` | 最近一次失败执行的统一观测 | present，且通过 `validate_observation_boundary` + `validate_observation_source_correlation` | L1 + L2 + L3 |
| `checkpoint` | `std::optional<Checkpoint>` | 恢复锚点与待恢复状态 | present，且通过 `validate_checkpoint_field_rules` | L1 + L3 |
| `idempotency_and_side_effect_report` | `std::optional<IdempotencyAndSideEffectReport>` | 准入所需的幂等/副作用证据 | present，且通过 `validate_idempotency_and_side_effect_report_field_rules` | L1 + L3 |

### 4.2 可选字段（2 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `retry_count` | `std::optional<std::uint32_t>` | 当前重试次数快照 | present 时与 `checkpoint.retry_count` 保持一致 | L2 |
| `runtime_budget_snapshot` | `std::optional<BudgetSnapshot>` | 当前预算快照 | present 时通过 `validate_budget_snapshot` | L2 |

### 4.3 嵌套报告字段（4 项）

`IdempotencyAndSideEffectReport` 固定由 2 个必填字段 + 2 个条件字段组成。

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `replay_safe` | `std::optional<bool>` | 当前失败场景是否允许真实重放 | 必填 | L1 |
| `side_effects_present` | `std::optional<bool>` | 当前观测是否已出现副作用 | 必填 | L1 |
| `idempotency_key` | `std::optional<std::string>` | 幂等键引用 | present 时 non-empty；若 `replay_safe=true` 且 `side_effects_present=true`，则必须 present 且 non-empty | L1 + L3 |
| `non_replayable_reason` | `std::optional<std::string>` | 不允许重放时的拒绝原因 | `replay_safe=false` 时必填且 non-empty；`replay_safe=true` 时必须 absent 或 empty | L1 |

---

## 5. 字段级规则总表

### 5.1 通用规则

| 规则编号 | 规则 | 适用字段 |
|---|---|---|
| R1 | required object 字段必须 present | 5 个顶层必填字段 |
| R2 | 复用既有冻结对象时，必须通过其已有 guard | `reflection_decision`, `latest_observation`, `checkpoint`, `runtime_budget_snapshot` |
| R3 | optional string 若 present 则必须 non-empty | `idempotency_key` |
| R4 | 条件字段必须受 `replay_safe` 驱动，不允许双向漂移 | `non_replayable_reason` |
| R5 | 可选快照值若 present，则必须与其锚点对象一致 | `retry_count` |

### 5.2 三层堆叠校验设计

#### Layer 1：必填字段存在性（T009-B 已实现）

| 规则 | 校验内容 |
|---|---|
| L1-R1 | `reflection_decision` / `error_info` / `latest_observation` / `checkpoint` / `idempotency_and_side_effect_report` 必须存在 |
| L1-R2 | `reflection_decision` 通过 `validate_reflection_decision_field_rules` |
| L1-R3 | `error_info` 通过 `validate_error_info_required_fields` |
| L1-R4 | `latest_observation` 通过 `validate_observation_boundary` |
| L1-R5 | `checkpoint` 通过 `validate_checkpoint_field_rules` |
| L1-R6 | `replay_safe`、`side_effects_present` 必填；`idempotency_key` present 时 non-empty |
| L1-R7 | `non_replayable_reason` 在 `replay_safe=false` 时必填，在 `replay_safe=true` 时必须为空 |

#### Layer 2：边界约束（T009-B 已实现）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 L1 规则 |
| L2-R1 | `latest_observation.success` 必须为 `false` |
| L2-R2 | `latest_observation.error` 必须存在 |
| L2-R3 | `checkpoint.state` 不得为 `Succeeded` |
| L2-R4 | `retry_count` present 时必须等于 `checkpoint.retry_count` |
| L2-R5 | `runtime_budget_snapshot` present 时必须通过 `validate_budget_snapshot` |

#### Layer 3：字段规则（T010-B 新增）

| 规则 | 校验内容 | 来源 |
|---|---|---|
| 继承 L2 | 全部 L1 + L2 规则 | 仓内统一 guard 堆叠模式 |
| L3-R1 | `latest_observation` 必须通过 `validate_observation_source_correlation` | WP03-T007 Source→Correlation 规则 |
| L3-R2 | `reflection_decision.request_id`、`latest_observation.request_id`、`checkpoint.request_id` 若存在，则三者必须同值 | 工程落地追踪字段完整性 |
| L3-R3 | `reflection_decision.goal_id`、`latest_observation.goal_id`、`checkpoint.goal_id` 若存在，则三者必须同值 | 同链路 goal 追踪一致性 |
| L3-R4 | `error_info` 必须镜像 `latest_observation.error` 的核心错误语义（failure_type/retryable/safe_to_replan/details/source_ref） | RecoveryRequest 表示同一失败证据包 |
| L3-R5 | `error_info.source_ref` 必须与 `latest_observation.source` 的关联字段一致：ToolExecution→`tool_call`/`tool_call_id`，WorkerAgent→`worker_task`/`worker_task_id`，Retrieval/HumanFeedback→`observation`/`observation_id` | WP03-T007 §6 |
| L3-R6 | 当 `replay_safe=true` 且 `side_effects_present=true` 时，`idempotency_key` 必须存在且 non-empty | Azure Retry / Compensating Transaction 的 idempotency 准则 |

### 5.3 字段解释

1. `error_info` 与 `latest_observation.error` 同时存在不是重复设计失误，而是分别服务于 RecoveryManager 的“快捷准入读取”和 Observation 的“完整执行记录”；T010 的职责是确保二者不漂移。
2. `request_id` / `goal_id` 一致性不是新增语义，而是把现有追踪字段纪律程序化，避免 RecoveryRequest 混入跨请求或跨目标证据。
3. `error_info.source_ref` 的对齐规则直接复用 WP03-T007 对 Observation source/correlation 的冻结结论，确保 RecoveryRequest 引用的就是同一条失败执行链路。
4. `replay_safe=true` 且 `side_effects_present=true` 时要求 `idempotency_key`，目的是确保“可安全重放”具备最小可审计证据，而不是靠运行时猜测。

### 5.4 最小组合规则

| 规则编号 | 非法组合 | 判定原因 |
|---|---|---|
| C1 | `request_id` 在 `reflection_decision/latest_observation/checkpoint` 间不一致 | RecoveryRequest 只能绑定单一请求的恢复证据 |
| C2 | `goal_id` 在 `reflection_decision/latest_observation/checkpoint` 间不一致 | RecoveryRequest 不能混入跨目标恢复证据 |
| C3 | `error_info` 与 `latest_observation.error` 核心字段不一致 | RecoveryManager 无法确定到底以哪份失败语义做准入 |
| C4 | `replay_safe=true` 且 `side_effects_present=true`，但缺少 `idempotency_key` | 存在副作用时无法证明安全重放 |

说明：

- T010 不新增 `decision_kind -> retry_count`、`budget -> decision_kind`、`checkpoint.state -> decision_kind` 之类策略规则，因为这些属于 RecoveryManager 的运行控制，不属于 contracts 字段表。
- 这符合 consumer-driven contracts 的 just-enough validation 原则：只验证当前消费者真实依赖、且能稳定自动化的字段关系。

---

## 6. Design→Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | `contracts/include/checkpoint/RecoveryRequestGuards.h`（新增 `validate_idempotency_and_side_effect_report_field_rules`、`validate_recovery_request_field_rules` 及跨对象一致性辅助校验） |
| 测试目标 | `tests/contract/checkpoint/RecoveryRequestFieldContractTest.cpp` |
| 验收命令 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryRequestFieldContractTest --output-on-failure` |

---

## 7. D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 7 个字段和嵌套报告字段已全覆盖；L1/L2/L3 分层闭合；新增规则均限定在证据包一致性和 idempotency hygiene，未越界到 RecoveryManager 执行策略 |

**Gate 结论：PASS — 可进入 WP04-T010-B**

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T010 来源与三件套 | docs/todos/contracts/WP-04-边界对象TODO.md | WP04-T010 行 |
| RecoveryRequest 对象边界 | docs/todos/contracts/deliverables/WP04-T009-RecoveryRequest语义说明.md | §4/§5/§6 |
| RecoveryManager 输入槽位 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §3.3/§4/§5.2 |
| Recovery 链路恢复锚点要求 | docs/architecture/DASSALL_Agent_architecture.md | §3.8.3/§6.10 |
| Observation source/correlation 与 ErrorSourceRef 对齐 | docs/todos/contracts/deliverables/WP03-T007-Observation分类表.md | §5/§6/§7 |
| Checkpoint 三层字段守卫模式 | docs/todos/contracts/deliverables/WP03-T013-Checkpoint字段表.md | §5.3 |
| BudgetSnapshot 规则 | docs/todos/contracts/deliverables/WP02-T008-BudgetSnapshot规则.md | §4.2/§4.4 |
| 字段冻结与兼容纪律 | docs/plans/DASALL_contracts冻结实施计划.md | §3.2/§6.3 |
| 链路追踪字段完整性 | docs/plans/DASALL_工程落地实现步骤指引.md | 阶段 K/L Gate |

### 8.2 外部业界参考

1. Microsoft Azure Architecture Center, Retry Pattern  
   结论：retry 应只在理解完整失败上下文的控制层实现，且需要充分考虑 idempotency。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/retry
2. Microsoft Azure Architecture Center, Compensating Transaction Pattern  
   结论：恢复/补偿依赖 durable state、可续跑步骤和 idempotent commands。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/compensating-transaction
3. Ian Robinson / Martin Fowler, Consumer-Driven Contracts  
   结论：消费者应做 just-enough validation，并用自动化断言维持演进边界。  
   参考：https://martinfowler.com/articles/consumerDrivenContracts.html
4. Anthropic, Building Effective Agents  
   结论：复杂 agent 应保持简单、透明、可测试，并通过显式 guardrails 强化可靠性。  
   参考：https://www.anthropic.com/engineering/building-effective-agents