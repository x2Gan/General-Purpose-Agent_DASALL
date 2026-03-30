# WP04-T009-D：RecoveryRequest 语义说明

> 版本：1.0 | 日期：2026-03-18 | 状态：Done
> 任务编号：WP04-T009-D
> 上游输入：ADR-007 §3.3/§4/§5.2、WP04-T006-D §4、WP03-T012/013 Checkpoint 冻结基线、WP04-T007/T008 ReflectionDecision 冻结基线、WP02 RuntimeBudget/BudgetSnapshot 冻结基线

---

## 1. 研究结论摘要

### 1.1 本地证据清单

| 编号 | 证据 | 对 T009 的直接约束 |
|---|---|---|
| L1 | ADR-007 §3.3 / §5.2 | RecoveryManager 必须消费独立的 RecoveryRequest；该对象至少包含 reflection_decision、error_info、latest_observation、checkpoint、retry_counters、runtime_budget_snapshot、idempotency_and_side_effect_report |
| L2 | ADR-007 §3.4 / §4 | ReflectionEngine 先给建议，RecoveryManager 再做准入裁定；因此 RecoveryRequest 不能退化为第二个 ReflectionDecision，也不能直接等于已执行动作 |
| L3 | docs/architecture/DASSALL_Agent_architecture.md §3.8.3 / §6.10 | 恢复链路必须围绕 Checkpoint、retry_counters、pending_action 工作，而不是重新发明第二恢复快照 |
| L4 | WP04-T006-D §4 | RecoveryRequest 是 runtime-owned admission input；T009 需要把 7 个输入槽位落为对象骨架，并明确禁区声明 |
| L5 | WP03-T012 / WP03-T013 | Checkpoint 已冻结为最小恢复状态对象，retry_counters 已在仓内简化为 uint32 retry_count，预算快照复用 BudgetSnapshot |
| L6 | WP04-T007-D / WP04-T008-D | ReflectionDecision 已冻结为 suggestion-only 对象且字段规则已闭合；RecoveryRequest 只能消费该对象，不能把 decision_kind/rationale 等字段重新抬到顶层 |
| L7 | WP02-T007 / WP02-T008 | RuntimeBudget / BudgetSnapshot 已冻结为唯一预算表达，RecoveryRequest 只能复用，不扩展新预算维度 |
| L8 | docs/todos/contracts/WP-04-边界对象TODO.md | T009-B 目标是 RecoveryRequest.h/Guards + contract test，完成判定是不退化为第二反思对象 |

### 1.2 外部参考清单

| 编号 | 参考 | 与 T009 的映射 |
|---|---|---|
| E1 | Microsoft Azure Architecture Center: Retry Pattern | retry logic 只应放在理解完整失败上下文的控制层，支撑 RecoveryRequest 必须携带准入上下文而不是直接携带重试执行参数 |
| E2 | Microsoft Azure Architecture Center: Compensating Transaction Pattern | 补偿与恢复依赖可恢复步骤记录、幂等命令和可续跑控制，支撑 RecoveryRequest 必须显式消费 checkpoint、budget、idempotency/side-effect 证据 |
| E3 | Ian Robinson / Martin Fowler: Consumer-Driven Contracts | 契约应围绕消费者真实依赖做 just-enough validation，支撑 T009 只冻结 RecoveryManager 的最小准入输入，不扩张到恢复策略实现 |
| E4 | Anthropic: Building Effective Agents | 复杂 agent 控制流应保持简单、透明、可测试，支撑 RecoveryRequest 采用 lean object + guard + contract test 的落地方式 |

### 1.3 对本任务的可落地启发

1. RecoveryRequest 的核心不是“再解释一次失败”，而是把 ReflectionDecision 与运行时准入证据绑定成一个 runtime-owned admission object。
2. 为避免跨包扩张，T009 只冻结对象骨架、required/boundary guards 和顶层禁区；字段 hygiene 与更细组合规则留给 T010。
3. `retry_counters` 在仓内已有 Checkpoint 简化先例，因此 T009 直接冻结为 `retry_count`，避免为单值快照额外发明新集合类型。
4. `runtime_budget_snapshot` 必须复用既有 BudgetSnapshot，而不是重新嵌入 RuntimeBudget 或新增预算字段族。
5. `idempotency_and_side_effect_report` 需要保留为结构化准入证据，但只表达“是否可安全重放、幂等键和副作用存在性/原因”，不提前展开为恢复策略 DSL。

---

## 2. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 RecoveryRequest 在失败责任链中的对象定位 | ADR-007 §3.3/§4/§5.2、WP04-T006-D §4 | 本文件 §3 | 明确生产者/消费者/调用顺位，且未侵入 ReflectionDecision/RecoveryOutcome 主职责 | 若出现“已执行动作”或“二次反思”表述，回退到 runtime admission input 口径 |
| D2 | 冻结 RecoveryRequest 的最小槽位集合 | ADR-007 §5.2、WP03-T012/013、WP02-T007/T008 | 本文件 §4 | 5 个必填 + 2 个可选槽位明确，且复用既有 Checkpoint/BudgetSnapshot/ReflectionDecision | 若新增新预算维度或第二恢复快照，回退到既有冻结对象 |
| D3 | 冻结 RecoveryRequest 顶层禁区 | ADR-007 §3.4/§5.2、WP04-T007-D、WP04-T006-D | 本文件 §5 | 明确不得承载 reflection 顶层语义、outcome 结果层字段和执行调度输出 | 若出现跨层字段，按 ADR-007 直接阻断 |
| D4 | 输出 T009 的 Design→Build 三件套 | WP04 TODO、现有 checkpoint contract 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整 | 若需要跨包新增非 T009 对象族，判定越界并回退 |
| D5 | 形成 D Gate 结果 | D1-D4 | 本文件 §7 | Gate 为 PASS/Blocked 二值 | 若对象定位或三件套未闭合，则 Blocked |

---

## 3. 对象定位与责任链

### 3.1 核心职责

RecoveryRequest 是 RecoveryManager 的恢复准入输入对象。

它负责把以下两类信息收敛到同一个 contracts 边界里：

1. ReflectionEngine 给出的失败语义建议。
2. Runtime 做恢复准入时必须读取的运行时证据。

RecoveryRequest 不是：

1. 第二个 ReflectionDecision，不负责再次表达 decision_kind/rationale/confidence 顶层语义。
2. 已执行动作，不直接表达 executed_action、final_runtime_state、checkpoint_ref 等结果层信息。
3. 恢复策略命令，不直接携带 retry_after_ms、backoff_strategy、circuit_breaker_transition 等执行调度输出。

### 3.2 调用链位置

```text
Observation + ErrorInfo + Checkpoint + Budget evidence
  -> ReflectionEngine
  -> ReflectionDecision
  -> RecoveryRequest
  -> RecoveryManager admission decision
  -> RecoveryOutcome
```

### 3.3 生产者与消费者

| 角色 | 与 RecoveryRequest 的关系 |
|---|---|
| Runtime / RecoveryManager 上游装配逻辑 | 唯一生产者；在进入 RecoveryManager 前装配对象 |
| RecoveryManager | 直接消费者；结合幂等性、预算、副作用和 checkpoint 判定是否准入 |
| Contract tests / 审计链路 | 消费对象结构与 guard 结果，验证未回退为二次反思对象 |

---

## 4. 最小语义槽位

RecoveryRequest 固定冻结为 5 个必填槽位 + 2 个可选槽位。

### 4.1 必填槽位（5 项）

| 字段 | 类型 | 语义 | 冻结原因 |
|---|---|---|---|
| `reflection_decision` | `ReflectionDecision` | ReflectionEngine 的建议结果 | ADR-007 §5.2 明示 RecoveryRequest 必须消费它 |
| `error_info` | `ErrorInfo` | 当前失败的结构化错误语义 | RecoveryManager 需要错误类别、是否可重试/可重规划 |
| `latest_observation` | `Observation` | 最近一次失败执行的统一观测 | Recovery 链路必须围绕真实环境反馈而不是自由文本推断 |
| `checkpoint` | `Checkpoint` | 恢复锚点与待恢复状态 | 架构 §3.8.3 / §6.10 冻结为恢复最小状态对象 |
| `idempotency_and_side_effect_report` | `IdempotencyAndSideEffectReport` | 是否允许真实重放的准入证据 | ADR-007 §5.2 明示需要副作用与幂等性依据 |

### 4.2 可选槽位（2 项）

| 字段 | 类型 | 语义 | 说明 |
|---|---|---|---|
| `retry_count` | `uint32` | 当前重试次数快照 | 复用 WP03 Checkpoint 对 retry_counters 的仓内简化方式 |
| `runtime_budget_snapshot` | `BudgetSnapshot` | 当前预算维度使用状态 | 复用 WP02 BudgetSnapshot，不新增预算维度 |

### 4.3 幂等/副作用报告的最小职责

`IdempotencyAndSideEffectReport` 只回答准入问题所需的四件事：

1. 当前失败场景是否可安全重放。
2. 若存在幂等键，当前判定基于哪个幂等键。
3. 当前观测是否已记录副作用。
4. 若不可安全重放，拒绝原因是什么。

它不是：

1. 完整执行记录，不能替代 Observation.payload/error/side_effects。
2. 补偿脚本或策略树，不负责描述如何补偿或如何退避。

---

## 5. 顶层禁区与边界声明

RecoveryRequest 顶层不得承载以下三类字段：

### 5.1 不得退化为第二个 ReflectionDecision

| 禁止字段 | 禁止原因 |
|---|---|
| `decision_kind` | 建议语义属于嵌套的 ReflectionDecision |
| `rationale` | 失败解释权属于 ReflectionDecision |
| `confidence` | 自评信号属于 ReflectionDecision |
| `relevant_observation_refs` | 证据引用属于 ReflectionDecision |
| `hint_ref` | 认知提示引用属于 ReflectionDecision |

### 5.2 不得提前携带 RecoveryOutcome 结果层字段

| 禁止字段 | 禁止原因 |
|---|---|
| `executed_action` | 这是 RecoveryOutcome 的执行结果 |
| `final_runtime_state` | 这是 RecoveryOutcome 的终态元数据 |
| `updated_retry_counters` | 这是执行后的计数，而不是准入输入 |
| `checkpoint_ref` | 这是输出引用，不是输入锚点 |
| `compensation_result_ref` | 这是补偿执行结果 |
| `rejection_reason` / `escalation_reason` | 这是准入后的裁定结果，不是准入输入 |

### 5.3 不得直接携带恢复执行调度输出

| 禁止字段 | 禁止原因 |
|---|---|
| `retry_after_ms` | 真实调度时延属于 RecoveryManager 执行层 |
| `backoff_strategy` | 退避策略属于恢复策略执行 |
| `circuit_breaker_transition` | 熔断迁移属于运行控制结果 |

---

## 6. Design→Build 映射

| 设计结论 | Build 落地点 | 说明 |
|---|---|---|
| 冻结 RecoveryRequest 对象与最小辅助报告结构 | `contracts/include/checkpoint/RecoveryRequest.h` | 只定义对象骨架和准入输入职责，不展开 T010 字段细则 |
| 冻结 required/boundary guards 与顶层禁区目录 | `contracts/include/checkpoint/RecoveryRequestGuards.h` | 复用 ReflectionDecision / ErrorInfo / Observation / Checkpoint / BudgetSnapshot 既有守卫 |
| 提供 T009 contract gate | `tests/contract/checkpoint/RecoveryRequestContractTest.cpp` | 覆盖正例与二次反思/结果层越界负例 |
| 接入合同测试注册 | `tests/contract/CMakeLists.txt` | 新增 RecoveryRequestContractTest |

代码目标：`contracts/include/checkpoint/RecoveryRequest.h`、`contracts/include/checkpoint/RecoveryRequestGuards.h`

测试目标：`tests/contract/checkpoint/RecoveryRequestContractTest.cpp`

验收命令：`cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryRequestContractTest --output-on-failure`

---

## 7. D Gate 结果

| D 原子项 | 状态 | 说明 |
|---|---|---|
| D1：对象定位 | ✅ Done | RecoveryRequest 已明确为 runtime admission input，不是建议对象也不是结果对象 |
| D2：最小槽位 | ✅ Done | 5 必填 + 2 可选槽位冻结，并复用既有冻结对象 |
| D3：顶层禁区 | ✅ Done | reflection 顶层语义、outcome 结果层字段和执行调度输出均被显式阻断 |
| D4：D→B 三件套 | ✅ Done | 代码目标、测试目标、验收命令已锁定 |
| D5：进入 B 判定 | ✅ Done | 无阻塞项 |

**Gate 结论：PASS — 可进入 WP04-T009-B**

进入 B 的条件：

1. ✅ RecoveryRequest 已限定为 runtime-owned admission object。
2. ✅ 已明确复用 ReflectionDecision / Observation / Checkpoint / BudgetSnapshot 既有冻结对象。
3. ✅ 已明确顶层禁区，能直接映射到 guard + contract test。
4. ✅ 代码、测试、验收命令三件套已锁定。

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| RecoveryRequest 最小输入槽位 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §5.2 |
| 建议权/执行权分层 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §3.4 / §4 |
| Checkpoint 恢复基线 | docs/architecture/DASSALL_Agent_architecture.md | §3.8.3 / §6.10 |
| RecoveryRequest 影响项前置结论 | docs/todos/contracts/deliverables/WP04-T006-ADR007对象影响清单.md | §4 |
| ReflectionDecision suggestion-only 基线 | docs/todos/contracts/deliverables/WP04-T007-ReflectionDecision语义说明.md | §3 / §4 / §5 |
| ReflectionDecision 字段冻结基线 | docs/todos/contracts/deliverables/WP04-T008-ReflectionDecision字段表.md | §4 / §5 |
| Checkpoint retry_count 简化口径 | docs/todos/contracts/deliverables/WP03-T012-Checkpoint语义说明.md | §2.1 / §5 |
| RuntimeBudget / BudgetSnapshot 唯一预算表达 | docs/todos/contracts/deliverables/WP02-T007-RuntimeBudget字段清单.md；docs/todos/contracts/deliverables/WP02-T008-BudgetSnapshot规则.md | 预算冻结章节 |
| T009 来源与完成判定 | docs/todos/contracts/WP-04-边界对象TODO.md | WP04-T009 行 |

### 8.2 外部业界参考

1. Microsoft Azure Architecture Center, Retry Pattern  
   结论：retry logic 应放在理解完整失败上下文的控制层，避免多层职责混淆。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/retry
2. Microsoft Azure Architecture Center, Compensating Transaction Pattern  
   结论：补偿与恢复需要 workflow 持有可恢复步骤、幂等和审计信息。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/compensating-transaction
3. Ian Robinson / Martin Fowler, Consumer-Driven Contracts  
   结论：契约应围绕消费者真实依赖做 just-enough validation。  
   参考：https://martinfowler.com/articles/consumerDrivenContracts.html
4. Anthropic, Building Effective Agents  
   结论：复杂 agent 控制流应保持简单、透明、可测试。  
   参考：https://www.anthropic.com/engineering/building-effective-agents