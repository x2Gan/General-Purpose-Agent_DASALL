# WP04-T022-D：ADR 到字段级约束映射表

> 版本：1.0 | 日期：2026-03-19 | 状态：Done
> 任务编号：WP04-T022-D
> 上游输入：WP04-T001-D 至 WP04-T021-D、ADR-006、ADR-007、ADR-008、docs/architecture/DASSALL_Agent_architecture.md、docs/architecture/DASALL_Engineering_Blueprint.md、docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md

---

## 1. 任务识别

### 1.1 task_id

- WP04-T022-D

### 1.2 来源

- docs/todos/contracts-freeze/WP-04-边界对象TODO.md

### 1.3 范围

- 把 ADR-006、ADR-007、ADR-008 在 WP04 已冻结对象上的字段级约束收敛成一份可机读映射表。
- 映射对象限定为 WP04 已完成的 10 个对象：ContextPacket、PromptComposeRequest、PromptComposeResult、ReflectionDecision、RecoveryRequest、RecoveryOutcome、MultiAgentRequest、MultiAgentResult、WorkerTask、WorkerLease。
- 映射内容限定为两类：对象级映射和禁止字段映射。
- Build 阶段只新增自动检查守卫与 smoke contract test，不重写既有对象定义和字段校验逻辑。

### 1.4 排除项

- 不新增业务对象。
- 不改写 ADR-006/007/008 已冻结结论。
- 不扩张到 runtime、multi_agent、memory、llm 的实现策略。
- 不把 T022 扩张为 M4 checklist；该职责留给 WP04-T023。

---

## 2. Phase 0：研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T022 的直接约束 |
|---|---|---|
| L1 | docs/todos/contracts-freeze/WP-04-边界对象TODO.md | T022 的目标是 ADR 到字段级约束映射表与自动检查守卫，输入基线为 T001-D 至 T021-D |
| L2 | WP04-T001-D | ADR-006 已冻结 3 个 Prompt/Context 对象及 17 个禁止字段 |
| L3 | WP04-T006-D | ADR-007 已冻结 3 个 Recovery 对象影响范围；ReflectionDecision/RecoveryOutcome 禁区明确，RecoveryRequest 有独立 top-level forbidden fields |
| L4 | WP04-T013-D | ADR-008 已冻结 4 个协同子域对象、上游锚点与 request/result/task/lease 的分层关系 |
| L5 | PromptBoundaryContracts.h | ADR-006 的字段禁区已经程序化为 3 组数组和 3 个 evaluate_* guard |
| L6 | RecoveryBoundaryGuards.h、RecoveryRequestGuards.h | ADR-007 的字段禁区分布在共享 Recovery guard 与 RecoveryRequest 自身 guard 中，说明 T022 需要统一映射入口而不是假设单一头文件 |
| L7 | MultiAgentBoundaryGuards.h、WorkerLeaseGuards.h | ADR-008 的字段禁区跨 request/result/task/lease 四类对象，WorkerLease 禁区由对象 guard 维护而不是单独 boundary aggregate |
| L8 | MultiAgentBoundaryContracts.h、RecoveryBoundaryContracts.h | 仓库已经采用“对象 catalog + 守卫重导出 + smoke test”模式，T022 应复用这一模式而不是发明新的运行时机制 |
| L9 | docs/architecture/DASSALL_Agent_architecture.md §0.4/§3.8/§4.8 | 契约优先、控制与认知分离、协同对象与主控对象分层，要求映射表能直接支撑 contract gate |
| L10 | docs/plans/DASALL_contracts冻结实施计划.md §3.2/§6.1/§6.3 | contracts 冻结要求语义先于 schema、兼容优先、分波次冻结，T022 应做“已有约束的映射与审查”而不是新增协议面 |

### 2.2 外部参考清单

| # | 来源 | 对 T022 的映射 |
|---|---|---|
| E1 | Anthropic, Building Effective Agents | 复杂 agent 系统应优先采用简单、可组合、可测试的接口；中间 gate 应尽量显式且可验证，支撑 T022 采用 header-only catalog + smoke gate |
| E2 | Microsoft Azure Architecture Center, AI agent orchestration patterns | 中间输出在传递给下游前应先校验；共享状态要最小化；least-privilege 与 audit trail 应程序化，支撑把 ADR 禁区映射成自动检查条目 |
| E3 | Proto Best Practices | 契约应兼容优先、避免对象膨胀、共享类型/约束应集中治理，支撑 T022 用集中映射表防止字段禁区散落后出现遗漏 |

### 2.3 对本任务的可落地启发

1. T022 不应新增第 11 个边界层，而应把已有 guard 统一编目成可审查的映射总表。
2. “ADR 约束映射”至少要覆盖两个维度：对象级 owner/scope 映射，以及禁止字段到实际 guard 的映射。
3. 自动检查应优先发现三类缺口：对象漏编目、禁止字段漏编目、映射条目存在但没有真实命中既有 guard。
4. RecoveryRequest 与 WorkerLease 证明字段禁区不一定都在 shared boundary 头里，因此 T022 需要允许 object-local guard 作为映射目标。
5. smoke test 应验证 catalog 完整性与 guard 命中，不重复执行各对象的业务语义测试。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 T022 的映射边界与对象全集 | T001-D/T006-D/T013-D、WP04 TODO | 本文件 §4 | 明确只覆盖 10 个对象、3 份 ADR、2 类映射 | 若扩张到 M4 checklist 或 runtime 策略，回退到“对象 + 禁止字段”范围 |
| D2 | 定义对象级映射表结构 | 现有 BoundaryContracts aggregate 模式 | 本文件 §5.1 | 每个对象都绑定 ADR、WP 范围、主字段 guard、边界 guard | 若某对象找不到既有 guard，标记 Blocked 并停止进入 B |
| D3 | 定义禁止字段映射表结构与期望计数 | Prompt/Recovery/MultiAgent/Task 既有 guard 头 | 本文件 §5.2 | 明确 57 个禁止字段映射条目与每个 ADR 的期望计数 | 若计数无法闭合，回退并重新核对前序 deliverable |
| D4 | 定义自动检查守卫的检查项 | Anthropic/Azure/Proto 参考、现有 smoke 模式 | 本文件 §6 | 明确对象覆盖、字段覆盖、guard 命中、重复条目四类检查 | 若检查项需要运行时策略数据，删除并回退为 compile-time/static check |
| D5 | 输出 Design->Build 三件套与 Gate 结论 | WP04 TODO、tests/contract/CMakeLists.txt 模式 | 本文件 §7/§8 | 代码目标、测试目标、验收命令完整闭合，Gate=PASS/Blocked | 若三件套不闭合或守卫目标不唯一，则 Blocked |

---

## 4. T022 设计结论

### 4.1 映射边界

- 映射范围固定为 3 份 ADR、10 个对象、2 类映射：
  - 对象级映射：对象属于哪份 ADR、对应哪个 WP 范围、主字段 guard 是什么、边界 guard 是什么。
  - 禁止字段映射：字段名、归属对象、归属 ADR、实际 guard 入口、拒绝理由。

### 4.2 对象全集

| ADR | 对象 |
|---|---|
| ADR-006 | ContextPacket、PromptComposeRequest、PromptComposeResult |
| ADR-007 | ReflectionDecision、RecoveryRequest、RecoveryOutcome |
| ADR-008 | MultiAgentRequest、MultiAgentResult、WorkerTask、WorkerLease |

### 4.3 期望计数

| ADR | 对象映射条目数 | 禁止字段映射条目数 |
|---|---|---|
| ADR-006 | 3 | 17 |
| ADR-007 | 3 | 24 |
| ADR-008 | 4 | 16 |
| 合计 | 10 | 57 |

说明：

1. ADR-006 禁止字段 = ContextPacket 8 + PromptComposeRequest 4 + PromptComposeResult 5。
2. ADR-007 禁止字段 = ReflectionDecision 5 + RecoveryRequest 15 + RecoveryOutcome 4。
3. ADR-008 禁止字段 = MultiAgentRequest 2 + MultiAgentResult 2 + WorkerTask 3 + WorkerLease 9。

---

## 5. 映射表设计

### 5.1 对象级映射表

| 对象 | ADR | WP 范围 | 主字段 guard | 边界 guard |
|---|---|---|---|---|
| ContextPacket | ADR-006 | WP03-T010/T011 | ContextPacketGuards.h | PromptBoundaryContracts.h |
| PromptComposeRequest | ADR-006 | WP04-T002/T003 | validate_prompt_compose_request_field_rules | evaluate_compose_request_field_boundary |
| PromptComposeResult | ADR-006 | WP04-T004/T005 | validate_prompt_compose_result_field_rules | evaluate_compose_result_field_boundary |
| ReflectionDecision | ADR-007 | WP04-T007/T008 | validate_reflection_decision_field_rules | evaluate_reflection_decision_field_boundary |
| RecoveryRequest | ADR-007 | WP04-T009/T010 | validate_recovery_request_field_rules | evaluate_recovery_request_field_boundary |
| RecoveryOutcome | ADR-007 | WP04-T011/T012 | validate_recovery_outcome_field_rules | evaluate_recovery_outcome_field_boundary |
| MultiAgentRequest | ADR-008 | WP04-T014/T015 | validate_multi_agent_request_field_rules | evaluate_multi_agent_request_field_boundary |
| MultiAgentResult | ADR-008 | WP04-T016/T017 | validate_multi_agent_result_field_rules | evaluate_multi_agent_result_field_boundary |
| WorkerTask | ADR-008 | WP04-T018/T019 | validate_worker_task_field_rules | evaluate_worker_task_field_boundary |
| WorkerLease | ADR-008 | WP04-T020/T021 | validate_worker_lease_field_rules | validate_worker_lease_forbidden_field |

### 5.2 禁止字段映射原则

1. 每个禁止字段都必须唯一映射到一个对象和一个 guard 入口。
2. 每个禁止字段映射必须能被 T022-B 的自动检查真实命中，不能只停留在字符串清单。
3. 相同字段名允许跨对象出现，但必须由不同对象上下文区分，例如 `agent_result` 在 MultiAgentResult/WorkerLease 中都属于越界别名。

### 5.3 自动检查守卫的最小职责

1. 检查对象级 catalog 是否完整覆盖 3/3/4 三个 ADR 波次。
2. 检查禁止字段 catalog 是否满足 17/24/16 计数。
3. 检查每个禁止字段条目是否能通过映射到的 guard 被真实拒绝。
4. 检查是否存在重复条目或空 guard_symbol。

---

## 6. Phase 2：设计交付结果

### 6.1 文档产出

- docs/todos/contracts-freeze/deliverables/WP04-T022-ADR字段映射表.md

### 6.2 状态与证据

- 已明确 10 个对象级映射条目。
- 已明确 57 个禁止字段映射条目的期望总量和按 ADR 分布。
- 已锁定 T022-B 的自动检查范围：对象覆盖、字段覆盖、guard 命中、重复条目。

### 6.3 D Gate

| 项 | 结果 |
|---|---|
| 是否达到进入 -B 条件 | ✅ 是 |
| 阻塞项 | 无 |
| 进入依据 | 对象全集、计数、guard 入口、Build 三件套已闭合，且无跨工作包扩张 |

**Gate 结论：PASS — 可进入 WP04-T022-B**

---

## 7. Design->Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | contracts/include/boundary/ADRFieldMappingGuards.h |
| 测试目标 | tests/contract/smoke/ADRFieldMappingContractTest.cpp |
| 验收命令 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ADRFieldMappingContractTest --output-on-failure && ctest --test-dir build-ci -L contract --output-on-failure |

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T022 目标与三件套 | docs/todos/contracts-freeze/WP-04-边界对象TODO.md | WP04-T022 行 |
| ADR-006 对象与禁止字段 | docs/todos/contracts-freeze/deliverables/WP04-T001-ADR006对象影响清单.md | §2/§3/§4 |
| ADR-007 对象与禁止字段 | docs/todos/contracts-freeze/deliverables/WP04-T006-ADR007对象影响清单.md | §3/§4/§5 |
| ADR-008 对象与禁止字段 | docs/todos/contracts-freeze/deliverables/WP04-T013-ADR008对象影响清单.md | §3 |
| WorkerTask 字段表基线 | docs/todos/contracts-freeze/deliverables/WP04-T019-WorkerTask字段表.md | §4/§5 |
| WorkerLease 字段表基线 | docs/todos/contracts-freeze/deliverables/WP04-T021-WorkerLease字段表.md | §4/§5 |
| Prompt guard 实现基线 | contracts/include/prompt/PromptBoundaryContracts.h | arrays + evaluate_* |
| Recovery guard 实现基线 | contracts/include/boundary/RecoveryBoundaryGuards.h；contracts/include/checkpoint/RecoveryRequestGuards.h | forbidden arrays + evaluate_* |
| Multi-agent/task guard 实现基线 | contracts/include/boundary/MultiAgentBoundaryGuards.h；contracts/include/task/WorkerLeaseGuards.h | forbidden arrays + evaluate_* |
| contracts 冻结原则 | docs/plans/DASALL_contracts冻结实施计划.md | §3.2/§6.1/§6.3 |

### 8.2 外部业界参考

1. Anthropic, Building Effective Agents  
   结论：复杂 agent 系统要保持简单、可组合、可测试，中间 gate 应显式可验证。  
   参考：https://www.anthropic.com/engineering/building-effective-agents

2. Microsoft Azure Architecture Center, AI agent orchestration patterns  
   结论：中间输出在传递给下游前应先校验，状态应最小化并保持可审计。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/ai-ml/guide/ai-agent-design-patterns

3. Proto Best Practices  
   结论：契约应兼容优先、集中治理、避免对象膨胀和约束散落。  
   参考：https://protobuf.dev/best-practices/dos-donts/