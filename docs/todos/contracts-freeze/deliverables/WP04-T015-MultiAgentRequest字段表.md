# WP04-T015-D：MultiAgentRequest 字段表

> 版本：1.0 | 日期：2026-03-19 | 状态：Done
> 任务编号：WP04-T015-D
> 上游输入：WP04-T014-D/B、WP04-T013-D/B、ADR-008 §3.2/§3.3/§4/§5.1、docs/architecture/DASSALL_Agent_architecture.md §4.8/§6.11、docs/architecture/DASALL_Engineering_Blueprint.md §3.10/§7.1、docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md

---

## 1. 任务识别

### 1.1 范围

- 把 T014 已冻结的 MultiAgentRequest 对象骨架下沉为字段级清单、字段规则和最小组合约束。
- 定义 MultiAgentRequest 的三层校验堆叠：L1 必填、L2 边界、L3 字段规则。
- 锁定 5 个必填字段 + 3 个可选字段，不新增字段、不扩张到 MultiAgentResult/WorkerTask/WorkerLease。
- 将 L3 规则映射到 contracts/include/agent/MultiAgentRequestGuards.h 的最小增量实现和 tests/contract/agent/MultiAgentRequestFieldContractTest.cpp。

### 1.2 排除项

- 不新增 MultiAgentRequest 字段。
- 不改写 T014 已冻结的对象定位、最小槽位和顶层禁区结论。
- 不扩张到 MultiAgentCoordinator 的调度算法、Worker 选择、租约分配或结果合并策略。
- 不改写 ADR-008 关于全局主控权与最终输出权的结论。
- 不引入第二套预算对象、权限 DSL 或协同状态机对象。

---

## 2. Phase 0：研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T015 的直接约束 |
|---|---|---|
| L1 | WP04-T014-D §3/§4/§5 | MultiAgentRequest 已冻结为 collaboration-subdomain request，且字段集合锁定为 5 必填 + 3 可选；T015 只能补字段规则，不能改对象边界 |
| L2 | ADR-008 §3.2/§3.3/§4 | MultiAgentRequest 是 AgentOrchestrator 发往 MultiAgentCoordinator 的协同子域输入，不是第二个 AgentRequest，也不是 WorkerTask 图 |
| L3 | ADR-008 §5.1 | MultiAgentRequest 至少包含 parent_request_id、parent_task_id、goal_fragment、plan_fragment、collaboration_mode、worker_budget_guard、permission_guard、stop_conditions |
| L4 | WP04-T013-D §3.1 | 协同请求只消费已裁定的局部目标、局部计划和约束条件；不复用 AgentRequest 顶层入口语义 |
| L5 | docs/architecture/DASSALL_Agent_architecture.md §4.8/§6.11/ADR-008 摘要 | 多 Agent 协同属于主循环中的受控阶段；主控层拥有总预算、最终结果和状态机，协同请求只承载子域最小必要上下文 |
| L6 | docs/architecture/DASALL_Engineering_Blueprint.md §3.10/§7.1 | MultiAgentCoordinator 位于 multi_agent 模块，但 contract 对象与 contract tests 应落在 contracts/agent 与 tests/contract/agent |
| L7 | contracts/include/boundary/MultiAgentBoundaryGuards.h | request 级禁区已冻结为 `agent_request` / `agent_request_payload`，T015 应复用既有 boundary guard，只补字段 hygiene 与组合规则 |
| L8 | contracts/include/checkpoint/RuntimeBudget.h 与 RuntimeBudgetGuards.h | worker_budget_guard 复用 WP02-T007 五维预算对象；若 present，则应服从既有正值与完整性守卫 |
| L9 | docs/todos/contracts-freeze/WP-04-边界对象TODO.md | T015 完成判定要求覆盖 parent_request_id/goal_fragment/plan_fragment/guards/stop_conditions，并把协同字段约束程序化 |

### 2.2 外部参考清单

| # | 来源 | 对 T015 的映射 |
|---|---|---|
| E1 | Anthropic, Building Effective Agents | orchestrator-workers 模式要求 central orchestrator 动态拆解任务并委派 worker；代理输入应保持最小、透明、可测试，并通过 stopping conditions 与 guardrails 保持控制 |
| E2 | Microsoft Azure Architecture Center, AI agent orchestration patterns | multi-agent orchestration 会放大上下文、协调与失败传播成本，应只传递最小必要上下文、在代理交接前验证结构化输出，并为 handoff/迭代路径设计明确 stopping conditions |
| E3 | Proto Best Practices | 契约演进应兼容优先；枚举应保留 `Unspecified` 哨兵值，共享通用类型优于重复发明，支持 T015 继续复用 RuntimeBudget 并把 CollaborationMode 的 `Unspecified` 作为非法运行值 |

### 2.3 对本任务的可落地启发

1. T015 的职责不是增加 MultiAgentRequest 表达力，而是把 T014 的 8 个槽位收敛为“可自动验证的最小协同输入合同”。
2. `worker_budget_guard` 继续复用 RuntimeBudget，并在字段层把“有值即完整合法”程序化，避免协同子域另起一套预算口径。
3. `permission_guard` 与 `stop_conditions` 是协同阶段最小治理面，应做非空、非塌缩、去重校验，避免把空约束伪装成已治理输入。
4. handoff 模式比 sequential/concurrent 更容易出现动态转移和循环漂移，因此需要在字段层要求显式的权限边界和退出条件。
5. 合同测试应优先覆盖正例、空约束、重复 stop_conditions、无效 budget guard 与 handoff 缺少治理字段等真实消费方关心的失败面。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 MultiAgentRequest 全字段清单和层次归属 | T014-D、MultiAgentRequest.h | 本文件 §4 | 5 必填 + 3 可选字段全部列出，且不新增字段 | 若出现新增字段或跨对象挪用，回退到 T014 已冻结集合 |
| D2 | 明确每个字段的字段级规则与归属层（L1/L2/L3） | ADR-008 §5.1、RuntimeBudgetGuards、MultiAgentBoundaryGuards | 本文件 §5 | 每个字段都映射到明确规则或已有 nested guard | 若规则与 T014 冲突，优先回退到 T014/L1-L2 已冻结口径 |
| D3 | 定义 T015 专属最小组合规则 | Anthropic/Azure 外部参考、WP04 TODO | 本文件 §5.4 | 至少 2 条可程序化组合规则，且不越界到调度算法 | 若组合规则依赖运行时策略细节，删除并回退为字段 hygiene |
| D4 | 输出 T015 的 Design→Build 三件套 | WP04 TODO、既有 field-test 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整闭合 | 若需要改动非 T015 文件族，判定越界并回退 |
| D5 | 形成 D Gate 结论 | D1-D4 | 本文件 §7 | Gate 为 PASS/Blocked 二值 | 若字段表未闭合或三件套缺失，则 Blocked |

---

## 4. 字段全集与分组

MultiAgentRequest 固定由 5 个必填字段 + 3 个可选字段组成，总数锁定为 8。

### 4.1 必填字段（5 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `parent_request_id` | `std::optional<std::string>` | 顶层 AgentRequest 锚点 | present，且包含非空白内容 | L1 + L3 |
| `parent_task_id` | `std::optional<std::string>` | 顶层任务图或协同根任务锚点 | present，且包含非空白内容；不得与 `parent_request_id` 塌缩为同值 | L1 + L2 + L3 |
| `goal_fragment` | `std::optional<std::string>` | 协同子域目标片段 | present，且包含非空白内容 | L1 + L3 |
| `plan_fragment` | `std::optional<std::string>` | 协同子域计划片段 | present，且包含非空白内容；不得与 `goal_fragment` 归一化后等值 | L1 + L3 |
| `collaboration_mode` | `std::optional<CollaborationMode>` | 协同模式 | present；非 `Unspecified`；值在已知枚举范围内 | L1 + L2 |

### 4.2 可选字段（3 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `worker_budget_guard` | `std::optional<RuntimeBudget>` | worker 级预算上界 | present 时必须通过 `validate_runtime_budget` | L3 |
| `permission_guard` | `std::optional<std::string>` | 当前协同阶段的权限边界摘要 | present 时必须包含非空白内容；在 `Handoff` 模式下必须 present | L3 |
| `stop_conditions` | `std::optional<std::vector<std::string>>` | 当前协同阶段退出条件集合 | present 时：向量非空、元素包含非空白内容、元素唯一；在 `Handoff` 模式下必须 present | L3 |

---

## 5. 字段级规则总表

### 5.1 通用规则

| 规则编号 | 规则 | 适用字段 |
|---|---|---|
| R1 | required string 字段必须 present 且包含至少一个非空白字符 | `parent_request_id`, `parent_task_id`, `goal_fragment`, `plan_fragment` |
| R2 | required enum 字段必须 present，且不得为 `Unspecified` | `collaboration_mode` |
| R3 | enum 字段必须落在已知枚举范围内 | `collaboration_mode` |
| R4 | optional string 若 present，则必须包含至少一个非空白字符 | `permission_guard` |
| R5 | optional vector 若 present，则向量本身必须非空 | `stop_conditions` |
| R6 | optional vector 若 present，则元素必须包含至少一个非空白字符 | `stop_conditions` |
| R7 | 具有条件集合语义的 vector 若 present，则元素必须唯一 | `stop_conditions` |
| R8 | 复用既有冻结对象时，必须通过其已有 guard | `worker_budget_guard` |

### 5.2 三层堆叠校验设计

#### Layer 1：必填字段存在性（T014-B 已实现）

| 规则 | 校验内容 |
|---|---|
| L1-R1 | `parent_request_id` / `parent_task_id` / `goal_fragment` / `plan_fragment` 必须 present 且非空字符串 |
| L1-R2 | `collaboration_mode` 必须 present 且不为 `Unspecified` |

#### Layer 2：边界约束（T014-B 已实现）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 L1 规则 |
| L2-R1 | `collaboration_mode` 位于已知枚举范围 |
| L2-R2 | `parent_request_id` 不得等于 `parent_task_id` |
| L2-R3 | `agent_request` / `agent_request_payload` 等 AgentRequest 包装别名继续由共享 boundary guard 阻断 |

#### Layer 3：字段规则（T015-B 新增）

| 规则 | 校验内容 | 来源 |
|---|---|---|
| 继承 L2 | 全部 L1 + L2 规则 | 仓内统一 guard 堆叠模式 |
| L3-R1 | 4 个 required string 字段都必须包含至少一个非空白字符，拒绝 whitespace-only 输入 | 最小必要上下文 hygiene |
| L3-R2 | `worker_budget_guard` 若 present，则必须通过 `validate_runtime_budget` | 复用 WP02-T007 预算口径 |
| L3-R3 | `permission_guard` 若 present，则必须包含至少一个非空白字符 | least-privilege guard 不可塌缩 |
| L3-R4 | `stop_conditions` 若 present，则必须为非空向量 | 停止条件集合必须有实际内容 |
| L3-R5 | `stop_conditions` 元素必须包含至少一个非空白字符 | 停止条件名册不可包含空壳条件 |
| L3-R6 | `stop_conditions` 元素必须唯一 | 条件集合不可重复计数同一退出条件 |
| L3-R7 | `goal_fragment` 与 `plan_fragment` 去首尾空白后不得等值 | 目标与计划层次必须分离 |
| L3-R8 | `collaboration_mode == Handoff` 时，`permission_guard` 必须 present 且合法 | handoff 模式需要显式权限边界 |
| L3-R9 | `collaboration_mode == Handoff` 时，`stop_conditions` 必须 present 且合法 | handoff 模式需要显式 stopping conditions |

### 5.3 字段解释

1. `worker_budget_guard` 继续复用 RuntimeBudget，而不是发明 WorkerBudget；T015 只把“有值即完整有效”固化为程序化校验。
2. `permission_guard` 保持字符串摘要形态，不在 T015 扩张为权限策略 DSL 或工具白名单对象。
3. `stop_conditions` 在 contracts 层只保证结构有效和可审计，不解释具体调度算法如何消费这些条件。
4. `goal_fragment` 与 `plan_fragment` 的分离规则用于防止把“目标是什么”和“怎么做”压塌成同一字符串槽位，回退为单层 request hint。
5. handoff 模式的附加要求来自外部参考对“动态转移与循环漂移风险”的共同结论，目的是让协同转移路径具备最小治理信息，而不是把所有模式都过度收紧。

### 5.4 最小组合规则

| 规则编号 | 非法组合 | 判定原因 |
|---|---|---|
| C1 | `goal_fragment` 与 `plan_fragment` 去首尾空白后等值 | 协同请求退化为单层自然语言提示，丢失目标/计划分层 |
| C2 | `collaboration_mode == Handoff` 且缺少 `permission_guard` | 动态交接缺少显式权限边界，无法证明受控交接 |
| C3 | `collaboration_mode == Handoff` 且缺少 `stop_conditions` | 动态交接缺少退出条件，容易形成无界 handoff 漂移 |
| C4 | `stop_conditions` present 但包含重复或空白条件 | 退出条件名册不可程序化消费 |

说明：

- T015 不增加 `collaboration_mode -> worker_budget_guard` 的条件规则，因为预算是否存在属于 orchestrator 策略决定，不是协同输入对象的普适字段约束。
- T015 不把 `permission_guard` 展开为工具级权限矩阵，因为那会跨到 WorkerTask 或运行时权限系统。
- 这符合 consumer-driven / just-enough validation 的原则：只验证当前消费者真实依赖、且能稳定自动化的字段规则。

---

## 6. Design->Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | `contracts/include/agent/MultiAgentRequestGuards.h`（新增 `validate_multi_agent_request_field_rules` 及字段 hygiene 辅助校验） |
| 测试目标 | `tests/contract/agent/MultiAgentRequestFieldContractTest.cpp` |
| 验收命令 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentRequestFieldContractTest --output-on-failure && ctest --test-dir build-ci -L contract --output-on-failure` |

---

## 7. D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 8 个字段已全覆盖；L1/L2/L3 分层闭合；新增规则限定在 field hygiene、handoff 最小治理与 stop_conditions 程序化消费，未越界到 MultiAgentCoordinator 调度实现 |

**Gate 结论：PASS — 可进入 WP04-T015-B**

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T015 来源与三件套 | docs/todos/contracts-freeze/WP-04-边界对象TODO.md | WP04-T015 行 |
| MultiAgentRequest 对象边界 | docs/todos/contracts-freeze/deliverables/WP04-T014-MultiAgentRequest语义说明.md | §3/§4/§5/§6 |
| ADR-008 责任链与最小输入槽位 | docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | §3.2/§3.3/§4/§5.1 |
| T013 对 T014/T015 的影响结论 | docs/todos/contracts-freeze/deliverables/WP04-T013-ADR008对象影响清单.md | §3.1/§5 |
| 多 Agent 架构与主控关系 | docs/architecture/DASSALL_Agent_architecture.md | §4.8/§6.11/ADR-008 摘要 |
| multi_agent 模块工程落点 | docs/architecture/DASALL_Engineering_Blueprint.md | §3.10/§7.1 |
| contracts 分波次冻结原则 | docs/plans/DASALL_contracts冻结实施计划.md | §3.2/§6.3/§8 |
| 契约优先与阶段 Gate 纪律 | docs/plans/DASALL_工程落地实现步骤指引.md | 阶段 B / 阶段 L |
| request 级共享禁区守卫 | contracts/include/boundary/MultiAgentBoundaryGuards.h | request boundary guards |
| RuntimeBudget 复用基线 | contracts/include/checkpoint/RuntimeBudget.h；contracts/include/checkpoint/RuntimeBudgetGuards.h | 对象定义与守卫 |

### 8.2 外部业界参考

1. Anthropic, Building Effective Agents  
   结论：orchestrator-workers 模式要求 central orchestrator 负责动态拆解与委派；有效 agent 设计应保持输入最小、透明、可测试，并通过 stopping conditions 与 guardrails 保持控制。  
   参考：https://www.anthropic.com/engineering/building-effective-agents

2. Microsoft Azure Architecture Center, AI agent orchestration patterns  
   结论：multi-agent orchestration 需要最小必要上下文、交接前结构化验证，并对 handoff/迭代路径设置 stopping conditions 与可靠性控制。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/ai-ml/guide/ai-agent-design-patterns

3. Proto Best Practices  
   结论：契约演进应兼容优先；枚举保留 `Unspecified` 哨兵值；优先复用共享通用类型而不是重复发明。  
   参考：https://protobuf.dev/best-practices/dos-donts/