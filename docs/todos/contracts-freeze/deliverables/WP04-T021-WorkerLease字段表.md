# WP04-T021-D：WorkerLease 字段表

> 版本：1.0 | 日期：2026-03-19 | 状态：Done
> 任务编号：WP04-T021-D
> 上游输入：WP04-T020-D/B、WP04-T013-D/B、ADR-008 §3.2/§3.3/§4/§5.4/§7、docs/architecture/DASSALL_Agent_architecture.md §3.8/§4.8/§5.11/§6.11/§6.12、docs/architecture/DASALL_Engineering_Blueprint.md §3.1/§3.10/§7.1、docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md、docs/todos/contracts-freeze/deliverables/WP02-T010-TimeDeadline规范.md

---

## 1. 任务识别

### 1.1 范围

- 把 T020 已冻结的 WorkerLease 对象骨架下沉为字段级清单、字段规则和最小组合约束。
- 定义 WorkerLease 的三层校验堆叠：L1 必填、L2 边界、L3 字段规则。
- 锁定 3 个必填字段 + 2 个可选字段，不新增字段、不扩张到 WorkerTask、MultiAgentResult、顶层 checkpoint 或 runtime 租约调度策略。
- 将 L3 规则映射到 contracts/include/task/WorkerLeaseGuards.h 的最小增量实现和 tests/contract/task/WorkerLeaseFieldContractTest.cpp。

### 1.2 排除项

- 不新增 WorkerLease 字段。
- 不改写 T020 已冻结的对象定位、最小槽位和顶层禁区结论。
- 不扩张到 AgentRegistry 匹配算法、租约续约调度器、释放策略、回收策略或 runtime 冲突仲裁。
- 不引入第二套时间表达，不把 deadline 规则回退为 timeout/ttl 主判定。
- 不改写 ADR-008 关于全局主控权、最终输出权和顶层 checkpoint/resume 分层的结论。

---

## 2. Phase 0：研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T021 的直接约束 |
|---|---|---|
| L1 | WP04-T020-D §4/§5 | WorkerLease 已冻结为 lease-metadata object，字段集合锁定为 3 必填 + 2 可选；T021 只能补字段规则，不能改对象边界 |
| L2 | ADR-008 §3.2/§3.3/§4 | WorkerLease 是 MultiAgentCoordinator 维护的局部租约元数据，不是第二个 AgentResult、checkpoint 或 resume 入口 |
| L3 | ADR-008 §5.4 | 顶层 checkpoint 与子任务快照必须分层；子域租约对象不能成为新的恢复入口 |
| L4 | WP04-T013-D §3.4 | WorkerLease 只表达租约元数据、续约窗口和释放摘要，不承载结果汇总或全局状态 |
| L5 | WP04 TODO | T021 完成判定要求覆盖 `lease_id/worker_ref/deadline/renewal/release_reason`，并将字段规则程序化 |
| L6 | WP02-T010 | `deadline_at` 是硬截止时间表达；时间类字段不能混入新的口径或歧义别名 |
| L7 | contracts/include/task/WorkerLease.h | 当前对象骨架已锁定 5 个字段，T021 只允许在现有对象面上补字段级 hygiene 与组合规则 |
| L8 | contracts/include/task/WorkerLeaseGuards.h | T020 已实现 required/boundary guards；T021 应在其之上新增 field-rules guard，而不是重写 L1/L2 |
| L9 | docs/architecture/DASSALL_Agent_architecture.md §3.8/§4.8/§6.11 | contracts 需要支撑多 agent 可恢复、可审计、可验证的协同边界；局部租约对象应保持最小可测试表面 |
| L10 | docs/architecture/DASALL_Engineering_Blueprint.md §3.1/§3.10/§7.1 | WorkerLease contract object 继续落在 contracts/task，与 tests/contract/task 对应；T021-B 只应是 guards 增量与 field contract test |

### 2.2 外部参考清单

| # | 来源 | 对 T021 的映射 |
|---|---|---|
| E1 | Anthropic, Building Effective Agents | 最有效的 agent 系统使用简单、可组合、可测试的接口；复杂协同对象应维持最小稳定表面，而不是把运行时策略隐含进字段协议 |
| E2 | Microsoft Azure Architecture Center, AI agent orchestration patterns | 多 agent 编排应最小化共享状态、把持久状态外置，并在传递中间输出前做验证；局部租约对象应只保留最小必要状态 |
| E3 | Proto Best Practices | 契约演进应兼容优先、避免消息膨胀；共享对象字段应保持单一职责，并优先复用既有时间语义而不是引入新表述 |

### 2.3 对本任务的可落地启发

1. T021 的职责不是增强 WorkerLease 的运行时能力，而是把 T020 的 5 个槽位收敛为可自动验证的最小租约字段合同。
2. `lease_id` 与 `worker_ref` 需要补足非空白 hygiene，避免运行时被 whitespace-only 值绕过。
3. `release_reason` 保持可选，但一旦提供就必须具备审计价值，至少不能是空白壳值。
4. `deadline_at` 与 `renewal_deadline_at` 继续沿用仓内已冻结时间口径，不在 T021 扩张为 ttl、duration 或续约策略对象。
5. field contract test 需要证明三件事：有效字段组合可通过、空白字符串会被拒绝、续约窗口边界仍受 T020 的 deadline 规则约束。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 WorkerLease 全字段清单和层次归属 | T020-D、WorkerLease.h | 本文件 §4 | 3 必填 + 2 可选字段全部列出，且不新增字段 | 若出现新增字段或跨对象挪用，回退到 T020 已冻结集合 |
| D2 | 明确每个字段的字段级规则与归属层（L1/L2/L3） | ADR-008 §5.4、WorkerLeaseGuards.h、WP02-T010 | 本文件 §5 | 每个字段都映射到明确规则或已有 guard | 若规则与 T020 冲突，优先回退到 T020/L1-L2 已冻结口径 |
| D3 | 定义 T021 专属最小组合规则 | Anthropic/Azure 外部参考、WP04 TODO | 本文件 §5.4 | 至少 2 条可程序化组合规则，且不越界到 runtime 调度策略 | 若组合规则依赖运行时策略细节，删除并回退为字段 hygiene |
| D4 | 输出 T021 的 Design->Build 三件套 | WP04 TODO、既有 field-test 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整闭合 | 若需要改动非 T021 文件族，判定越界并回退 |
| D5 | 形成 D Gate 结论 | D1-D4 | 本文件 §7 | Gate 为 PASS/Blocked 二值 | 若字段表未闭合或三件套缺失，则 Blocked |

---

## 4. 字段全集与分组

WorkerLease 固定由 3 个必填字段 + 2 个可选字段组成，总数锁定为 5。

### 4.1 必填字段（3 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `lease_id` | `std::optional<std::string>` | 当前局部租约的稳定标识 | present；包含非空白内容 | L1 + L3 |
| `worker_ref` | `std::optional<std::string>` | 当前租约绑定的 worker 引用 | present；包含非空白内容 | L1 + L3 |
| `deadline_at` | `std::optional<std::int64_t>` | 当前租约的绝对硬截止时间 | present；值大于 0 | L1 |

### 4.2 可选字段（2 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `renewal_deadline_at` | `std::optional<std::int64_t>` | 当前租约允许申请续约的截止时间点 | present 时值大于 0，且不得晚于 `deadline_at` | L2 |
| `release_reason` | `std::optional<std::string>` | 当前租约提前释放时的原因摘要 | present 时必须包含非空白内容 | L2 + L3 |

---

## 5. 字段级规则总表

### 5.1 通用规则

| 规则编号 | 规则 | 适用字段 |
|---|---|---|
| R1 | required string 字段必须 present 且包含至少一个非空白字符 | `lease_id`, `worker_ref` |
| R2 | required 时间字段必须 present 且大于 0 | `deadline_at` |
| R3 | optional 时间字段若 present，则必须大于 0 且不得晚于主 deadline | `renewal_deadline_at` |
| R4 | optional string 若 present，则必须包含至少一个非空白字符 | `release_reason` |

### 5.2 三层堆叠校验设计

#### Layer 1：必填字段存在性（T020-B 已实现）

| 规则 | 校验内容 |
|---|---|
| L1-R1 | `lease_id` / `worker_ref` 必须 present 且非空字符串 |
| L1-R2 | `deadline_at` 必须 present 且大于 0 |

#### Layer 2：边界约束（T020-B 已实现）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 L1 规则 |
| L2-R1 | `renewal_deadline_at` 若 present，则必须大于 0 |
| L2-R2 | `renewal_deadline_at` 若 present，不得晚于 `deadline_at` |
| L2-R3 | `release_reason` 若 present，则不得为空字符串 |
| L2-R4 | `checkpoint_ref` / `resume_token` / `agent_result` / `merged_result` 等越界别名继续由 boundary guard 阻断 |

#### Layer 3：字段规则（T021-B 新增）

| 规则 | 校验内容 | 来源 |
|---|---|---|
| 继承 L2 | 全部 L1 + L2 规则 | 仓内统一 guard 堆叠模式 |
| L3-R1 | `lease_id` 必须包含至少一个非空白字符 | 租约标识 hygiene |
| L3-R2 | `worker_ref` 必须包含至少一个非空白字符 | worker 绑定锚点 hygiene |
| L3-R3 | `release_reason` 若 present，则必须包含至少一个非空白字符 | 释放摘要必须具备审计价值 |

### 5.3 字段解释

1. `deadline_at` 在 T021 不追加上限、时区、回退时间源或续约次数规则，因为这些属于 runtime 或租约调度策略，不属于 WorkerLease 字段表的普适口径。
2. `renewal_deadline_at` 继续保持“续约窗口边界”语义，不在 T021 扩张为新 deadline、ttl 或 interval 对象。
3. `worker_ref` 保持字符串锚点，不在 T021 扩张为 WorkerDescriptor 或 capability schema，以避免跨到 AgentRegistry 设计。
4. `release_reason` 只保证“有值即有效”，不在 T021 强制其出现，因为租约可以自然到期而不需要提前释放原因。

### 5.4 最小组合规则

| 规则编号 | 非法组合 | 判定原因 |
|---|---|---|
| C1 | `renewal_deadline_at` present 且晚于 `deadline_at` | 续约窗口越过硬截止时间，破坏 T020 已冻结的时间边界 |
| C2 | `release_reason` present 但仅由空白字符组成 | 释放摘要看似存在但无法被审计链路稳定消费 |
| C3 | `lease_id` 或 `worker_ref` 仅由空白字符组成 | 局部租约锚点表面存在但实际塌缩，破坏最小可追踪性 |

说明：

- T021 不新增 `release_reason -> renewal_deadline_at` 的条件矩阵，因为那会跨到 runtime 释放/续约策略。
- T021 不要求 `lease_id` 与 `worker_ref` 在字符串值上必须不同，因为两者是否偶然同值不属于 contracts 的通用字段约束。
- 这符合 consumer-driven / just-enough validation 原则：只验证当前消费者真实依赖、且能稳定自动化的字段规则。

---

## 6. Design->Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | contracts/include/task/WorkerLeaseGuards.h（新增 `validate_worker_lease_field_rules` 及字段 hygiene 辅助校验） |
| 测试目标 | tests/contract/task/WorkerLeaseFieldContractTest.cpp |
| 验收命令 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R WorkerLeaseFieldContractTest --output-on-failure && ctest --test-dir build-ci -L contract --output-on-failure |

---

## 7. D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 5 个字段已全覆盖；L1/L2/L3 分层闭合；新增规则限定在 string hygiene 与续约窗口边界复核，未越界到 runtime 续约/回收实现 |

**Gate 结论：PASS — 可进入 WP04-T021-B**

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T021 来源与三件套 | docs/todos/contracts-freeze/WP-04-边界对象TODO.md | WP04-T021 行 |
| WorkerLease 对象边界 | docs/todos/contracts-freeze/deliverables/WP04-T020-WorkerLease语义说明.md | §3/§4/§5/§6 |
| ADR-008 责任链与租约分层 | docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | §3.2/§3.3/§4/§5.4 |
| T013 对 T020/T021 的影响结论 | docs/todos/contracts-freeze/deliverables/WP04-T013-ADR008对象影响清单.md | §2.1/§3.4 |
| 时间/截止时间规范 | docs/todos/contracts-freeze/deliverables/WP02-T010-TimeDeadline规范.md | §3/§4 |
| 多 Agent 架构与主控关系 | docs/architecture/DASSALL_Agent_architecture.md | §3.8/§4.8/§5.11/§6.11/§6.12 |
| contracts/task 与 tests/task 工程落点 | docs/architecture/DASALL_Engineering_Blueprint.md | §3.1/§3.10/§7.1 |
| contracts 分波次冻结原则 | docs/plans/DASALL_contracts冻结实施计划.md | §3.2/§6.2/§6.3/§8 |
| 契约优先与阶段 Gate 纪律 | docs/plans/DASALL_工程落地实现步骤指引.md | 阶段 B / 阶段 L |
| 现有 WorkerLease 对象与 guards | contracts/include/task/WorkerLease.h；contracts/include/task/WorkerLeaseGuards.h | 对象定义与 T020 required/boundary guards |

### 8.2 外部业界参考

1. Anthropic, Building Effective Agents  
   结论：agent 接口应保持简单、可组合、可测试；协同对象应维持最小稳定表面，避免把运行时策略隐含进协议。  
   参考：https://www.anthropic.com/engineering/building-effective-agents

2. Microsoft Azure Architecture Center, AI agent orchestration patterns  
   结论：多 agent 编排应最小化共享状态、把持久状态外置，并在传递中间输出前验证有效性。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/ai-ml/guide/ai-agent-design-patterns

3. Proto Best Practices  
   结论：契约应兼容优先、避免对象膨胀，并优先复用既有共享时间语义而不是引入新的重复表达。  
   参考：https://protobuf.dev/best-practices/dos-donts/