# WP04-T019-D：WorkerTask 字段表

> 版本：1.0 | 日期：2026-03-19 | 状态：Done
> 任务编号：WP04-T019-D
> 上游输入：WP04-T018-D/B、WP04-T013-D/B、ADR-008 §3.2/§3.3/§4/§5.3、docs/architecture/DASSALL_Agent_architecture.md §4.8/§6.11、docs/architecture/DASALL_Engineering_Blueprint.md §3.1/§3.10/§7.1、docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md

---

## 1. 任务识别

### 1.1 范围

- 把 T018 已冻结的 WorkerTask 对象骨架下沉为字段级清单、字段规则和最小组合约束。
- 定义 WorkerTask 的三层校验堆叠：L1 必填、L2 边界、L3 字段规则。
- 锁定 6 个必填字段 + 1 个可选字段，不新增字段、不扩张到 WorkerLease、MultiAgentRequest、MultiAgentResult。
- 将 L3 规则映射到 contracts/include/task/WorkerTaskGuards.h 的最小增量实现和 tests/contract/task/WorkerTaskFieldContractTest.cpp。

### 1.2 排除项

- 不新增 WorkerTask 字段。
- 不改写 T018 已冻结的对象定位、最小槽位和顶层禁区结论。
- 不扩张到 AgentRegistry 匹配算法、WorkerLease 租约管理、ResultMerger 聚合或 runtime 调度策略。
- 不为 WorkerTask 引入第二套 timeout/deadline 口径，也不扩张为权限 DSL 或工具描述对象。
- 不改写 ADR-008 关于全局主控权、最终输出权和顶层 checkpoint 分层的结论。

---

## 2. Phase 0：研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T019 的直接约束 |
|---|---|---|
| L1 | WP04-T018-D §3/§4/§5 | WorkerTask 已冻结为 execution-unit object，字段集合锁定为 6 必填 + 1 可选；T019 只能补字段规则，不能改对象边界 |
| L2 | ADR-008 §3.2/§3.3/§4 | WorkerTask 是 MultiAgentCoordinator 调度的子任务执行单元，不是第二个 AgentRequest、AgentResult 或顶层 checkpoint 入口 |
| L3 | ADR-008 §5.3 | WorkerTask 至少应明确 task_id、parent_task_id、lease_id、worker_type、allowed_tools、timeout、idempotency_key |
| L4 | WP04-T013-D §3.3 | WorkerTask 归属于局部执行对象，锚点来自 MultiAgentRequest / 顶层任务图；不得反向携带全局 Session/FSM/checkpoint 主控语义 |
| L5 | WP01-T011 §3.1/§4.3/§6 | WorkerTask 仅承载执行单元约束（task/lease/timeout/tools/idempotency），不得承载全局 Session/FSM 语义 |
| L6 | contracts/include/task/WorkerTask.h | 当前对象骨架已经锁定 7 个字段，T019 只允许在现有对象面上补字段级 hygiene 与组合规则 |
| L7 | contracts/include/task/WorkerTaskGuards.h | T018 已实现 required/boundary guards；T019 应在其之上新增 field-rules guard，而不是重写 L1/L2 |
| L8 | docs/todos/contracts/WP-04-边界对象TODO.md | T019 完成判定要求覆盖 task_id/parent_task_id/lease_id/worker_type/allowed_tools/timeout/idempotency_key，并将字段规则程序化 |
| L9 | docs/architecture/DASSALL_Agent_architecture.md §2.4/§3.0 | contracts 需要契约优先、治理优先和资源预算化表达；字段规则应服务于可恢复、可审计的执行单元对象 |
| L10 | docs/architecture/DASALL_Engineering_Blueprint.md §3.1/§3.10/§7.1 | WorkerTask contract object 继续落在 contracts/task，与 tests/contract/task 对应；T019-B 只应是 guards 增量与 field contract test |

### 2.2 外部参考清单

| # | 来源 | 对 T019 的映射 |
|---|---|---|
| E1 | Anthropic, Building Effective Agents | 最有效的 agent 系统使用简单、可组合、可测试的接口；orchestrator-workers 模式应保持子任务接口透明，避免把执行单元膨胀为复杂隐式协议 |
| E2 | Microsoft Azure Architecture Center, AI agent orchestration patterns | multi-agent orchestration 中间输出必须先验证再下传；应控制上下文膨胀、保持最小必要状态、对 handoff/worker 边界做可靠性和 least-privilege 治理 |
| E3 | Proto Best Practices | 契约演进应兼容优先，避免对象膨胀；共享字段应维持单一职责，repeated 语义应稳定且可程序化验证 |

### 2.3 对本任务的可落地启发

1. T019 的职责不是增加 WorkerTask 的表达力，而是把 T018 的 7 个槽位收敛为“可自动验证的最小执行单元合同”。
2. `task_id`、`parent_task_id`、`lease_id`、`worker_type` 应补足非空白 hygiene，避免运行时被 whitespace-only 值绕过。
3. `allowed_tools` 是执行单元最关键的约束面，应至少保证元素非空白、去重、可程序化消费，以避免权限边界看似存在但实际塌缩。
4. `task_id` 与 `parent_task_id` 在 T018 已要求分层，T019 应把“去空白后仍需分层”固化为字段规则，阻断锚点靠空白字符伪装为不同值。
5. `idempotency_key` 继续保持可选，但一旦提供就必须具备可审计价值，至少不能是空白壳值。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 WorkerTask 全字段清单和层次归属 | T018-D、WorkerTask.h | 本文件 §4 | 6 必填 + 1 可选字段全部列出，且不新增字段 | 若出现新增字段或跨对象挪用，回退到 T018 已冻结集合 |
| D2 | 明确每个字段的字段级规则与归属层（L1/L2/L3） | ADR-008 §5.3、WorkerTaskGuards.h、WP01-T011 | 本文件 §5 | 每个字段都映射到明确规则或已有 guard | 若规则与 T018 冲突，优先回退到 T018/L1-L2 已冻结口径 |
| D3 | 定义 T019 专属最小组合规则 | Anthropic/Azure 外部参考、WP04 TODO | 本文件 §5.4 | 至少 2 条可程序化组合规则，且不越界到租约或调度算法 | 若组合规则依赖运行时策略细节，删除并回退为字段 hygiene |
| D4 | 输出 T019 的 Design→Build 三件套 | WP04 TODO、既有 field-test 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整闭合 | 若需要改动非 T019 文件族，判定越界并回退 |
| D5 | 形成 D Gate 结论 | D1-D4 | 本文件 §7 | Gate 为 PASS/Blocked 二值 | 若字段表未闭合或三件套缺失，则 Blocked |

---

## 4. 字段全集与分组

WorkerTask 固定由 6 个必填字段 + 1 个可选字段组成，总数锁定为 7。

### 4.1 必填字段（6 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `task_id` | `std::optional<std::string>` | 当前执行单元唯一锚点 | present；包含非空白内容；去空白后不得与 `parent_task_id` 等值 | L1 + L2 + L3 |
| `parent_task_id` | `std::optional<std::string>` | 指向上层任务图的父任务锚点 | present；包含非空白内容；去空白后不得与 `task_id` 等值 | L1 + L2 + L3 |
| `lease_id` | `std::optional<std::string>` | 当前执行单元关联的租约引用 | present；包含非空白内容 | L1 + L3 |
| `worker_type` | `std::optional<std::string>` | 当前执行单元的 worker 能力或角色 | present；包含非空白内容 | L1 + L3 |
| `allowed_tools` | `std::optional<std::vector<std::string>>` | 当前执行单元允许调用的工具名册 | present；向量非空；元素包含非空白内容；元素唯一 | L1 + L2 + L3 |
| `timeout_ms` | `std::optional<std::uint32_t>` | 当前执行单元局部执行超时 | present；值大于 0 | L1 |

### 4.2 可选字段（1 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `idempotency_key` | `std::optional<std::string>` | 重放/补偿锚点 | present 时必须包含非空白内容 | L2 + L3 |

---

## 5. 字段级规则总表

### 5.1 通用规则

| 规则编号 | 规则 | 适用字段 |
|---|---|---|
| R1 | required string 字段必须 present 且包含至少一个非空白字符 | `task_id`, `parent_task_id`, `lease_id`, `worker_type` |
| R2 | required vector 字段必须 present 且至少包含一个元素 | `allowed_tools` |
| R3 | vector 字段元素必须包含至少一个非空白字符 | `allowed_tools` |
| R4 | 工具名册若 present，则元素必须唯一 | `allowed_tools` |
| R5 | optional string 若 present，则必须包含至少一个非空白字符 | `idempotency_key` |
| R6 | 执行单元锚点与父任务锚点必须保持分层 | `task_id`, `parent_task_id` |

### 5.2 三层堆叠校验设计

#### Layer 1：必填字段存在性（T018-B 已实现）

| 规则 | 校验内容 |
|---|---|
| L1-R1 | `task_id` / `parent_task_id` / `lease_id` / `worker_type` 必须 present 且非空字符串 |
| L1-R2 | `allowed_tools` 必须 present 且非空向量 |
| L1-R3 | `timeout_ms` 必须 present 且大于 0 |

#### Layer 2：边界约束（T018-B 已实现）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 L1 规则 |
| L2-R1 | `task_id` 不得等于 `parent_task_id` |
| L2-R2 | `allowed_tools` 元素不得为空字符串 |
| L2-R3 | `idempotency_key` 若 present，则不得为空字符串 |
| L2-R4 | `global_session_state` / `global_fsm_state` / `session_fsm_state` 等顶层别名继续由共享 boundary guard 阻断 |

#### Layer 3：字段规则（T019-B 新增）

| 规则 | 校验内容 | 来源 |
|---|---|---|
| 继承 L2 | 全部 L1 + L2 规则 | 仓内统一 guard 堆叠模式 |
| L3-R1 | `task_id` 必须包含至少一个非空白字符 | 执行单元锚点 hygiene |
| L3-R2 | `parent_task_id` 必须包含至少一个非空白字符 | 上层任务锚点 hygiene |
| L3-R3 | `lease_id` 必须包含至少一个非空白字符 | 租约引用 hygiene |
| L3-R4 | `worker_type` 必须包含至少一个非空白字符 | 路由角色 hygiene |
| L3-R5 | `allowed_tools` 元素不得为空白字符串 | least-privilege 工具名册 hygiene |
| L3-R6 | `allowed_tools` 元素必须唯一 | 工具许可名册不可重复计数同一工具 |
| L3-R7 | `idempotency_key` 若 present，则必须包含至少一个非空白字符 | 重放锚点必须可审计 |
| L3-R8 | `task_id` 与 `parent_task_id` 去首尾空白后不得等值 | 执行单元锚点与父任务锚点必须在 whitespace 层面也保持分层 |

### 5.3 字段解释

1. `timeout_ms` 在 T019 不追加上限、deadline 或 backoff 规则，因为这些属于 runtime/WorkerLease 策略，不属于 WorkerTask 字段表的普适口径。
2. `allowed_tools` 继续保持字符串名册，不在 T019 扩张为 ToolDescriptor、权限矩阵或复杂 ACL 对象。
3. `worker_type` 保持字符串角色标识，不在 T019 扩张为枚举全集或 registry capability schema，以避免跨到 AgentRegistry 设计。
4. `idempotency_key` 只保证“有值即有效”，不在 T019 强制其出现，因为是否需要幂等锚点取决于副作用和补偿语义，属于更上层策略。
5. `task_id` 与 `parent_task_id` 的 trimmed distinctness 是对 T018 边界规则的加固，而不是新增对象职责。

### 5.4 最小组合规则

| 规则编号 | 非法组合 | 判定原因 |
|---|---|---|
| C1 | `task_id` 与 `parent_task_id` 去首尾空白后等值 | 执行单元锚点与父任务锚点在 whitespace 层面塌缩，破坏任务图分层 |
| C2 | `allowed_tools` present 且包含重复项 | 工具许可名册会重复计数同一工具，影响 least-privilege 边界和可审计性 |
| C3 | `allowed_tools` present 但包含空白项 | 工具边界看似存在但无法被程序稳定消费 |

说明：

- T019 不新增 `worker_type -> allowed_tools` 的条件矩阵规则，因为那会跨到 AgentRegistry 或权限系统的运行时策略。
- T019 不要求 `idempotency_key` 必须与 `task_id` 或 `lease_id` 不同，因为是否共用标识属于实现策略，不是 contracts 的通用字段约束。
- 这符合 consumer-driven / just-enough validation 原则：只验证当前消费者真实依赖、且能稳定自动化的字段规则。

---

## 6. Design->Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | contracts/include/task/WorkerTaskGuards.h（新增 `validate_worker_task_field_rules` 及字段 hygiene 辅助校验） |
| 测试目标 | tests/contract/task/WorkerTaskFieldContractTest.cpp |
| 验收命令 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R WorkerTaskFieldContractTest --output-on-failure && ctest --test-dir build-ci -L contract --output-on-failure |

---

## 7. D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 7 个字段已全覆盖；L1/L2/L3 分层闭合；新增规则限定在 field hygiene、工具名册唯一性和锚点分层加固，未越界到 WorkerLease、AgentRegistry 或 runtime 调度实现 |

**Gate 结论：PASS — 可进入 WP04-T019-B**

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T019 来源与三件套 | docs/todos/contracts/WP-04-边界对象TODO.md | WP04-T019 行 |
| WorkerTask 对象边界 | docs/todos/contracts/deliverables/WP04-T018-WorkerTask语义说明.md | §3/§4/§5/§6 |
| ADR-008 责任链与最小执行槽位 | docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | §3.2/§3.3/§4/§5.3 |
| T013 对 T018/T019 的影响结论 | docs/todos/contracts/deliverables/WP04-T013-ADR008对象影响清单.md | §2.1/§3.3 |
| 协同语义核对单 | docs/todos/contracts/deliverables/WP01-T011-协同语义核对单.md | §3.1/§4.3/§6 |
| 多 Agent 架构与主控关系 | docs/architecture/DASSALL_Agent_architecture.md | §2.4/§3.0/§4.8/§6.11 |
| contracts/task 与 tests/task 工程落点 | docs/architecture/DASALL_Engineering_Blueprint.md | §3.1/§3.10/§7.1 |
| contracts 分波次冻结原则 | docs/plans/DASALL_contracts冻结实施计划.md | §3.2/§6.2/§6.3/§8 |
| 契约优先与阶段 Gate 纪律 | docs/plans/DASALL_工程落地实现步骤指引.md | 阶段 B / 阶段 L |
| 现有 WorkerTask 对象与 guards | contracts/include/task/WorkerTask.h；contracts/include/task/WorkerTaskGuards.h | 对象定义与 T018 required/boundary guards |

### 8.2 外部业界参考

1. Anthropic, Building Effective Agents  
   结论：agent 接口应保持简单、可组合、可测试；orchestrator-workers 模式下，子任务接口需要透明并具备明确 guardrails。  
   参考：https://www.anthropic.com/engineering/building-effective-agents

2. Microsoft Azure Architecture Center, AI agent orchestration patterns  
   结论：中间 agent 输出必须先验证再下传；multi-agent 需要最小必要上下文、least-privilege 边界和可靠性控制。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/ai-ml/guide/ai-agent-design-patterns

3. Proto Best Practices  
   结论：契约应兼容优先、避免对象膨胀，repeated 语义应稳定并做 just-enough validation。  
   参考：https://protobuf.dev/best-practices/dos-donts/