# WP04-T011-D：RecoveryOutcome 语义说明

> 版本：1.0 | 日期：2026-03-18 | 状态：Done
> 任务编号：WP04-T011-D
> 上游输入：ADR-007 §3.4/§4/§5.3、WP04-T006-D §5、WP01-T010 恢复语义核对单、WP04-T009/T010 RecoveryRequest 冻结基线、WP03-T012/013 Checkpoint 冻结基线

---

## 1. 研究结论摘要

### 1.1 本地证据清单

| 编号 | 证据 | 对 T011 的直接约束 |
|---|---|---|
| L1 | ADR-007 §3.4 / §5.3 | RecoveryOutcome 只表达 RecoveryManager 实际执行后的结果与控制元数据，允许承载 executed_action、final_runtime_state、updated_retry_counters、checkpoint_ref、compensation_result_ref、rejection_reason 或 escalation_reason |
| L2 | ADR-007 §4 | RecoveryOutcome 位于 ReflectionDecision 与 RecoveryManager 准入裁定之后，是恢复责任链的终端结果对象，不是失败分析输入 |
| L3 | docs/architecture/DASSALL_Agent_architecture.md ADR-007 摘要 | 当前架构明确“ReflectionEngine 负责失败语义判断，RecoveryManager 负责恢复执行”，并指出需要补充 ReflectionDecision 与 RecoveryOutcome 的分层契约 |
| L4 | docs/plans/DASALL_contracts冻结实施计划.md §7/§8 阶段 3 | Recovery 链路固定为 Observation + ErrorInfo + Checkpoint -> ReflectionDecision -> RecoveryOutcome；RecoveryOutcome 只表达执行结果与控制元数据，不回写失败归因语义 |
| L5 | WP04-T006-D §5 | RecoveryOutcome 允许字段族与 4 个失败归因禁区已在影响清单阶段冻结，T011 需把它们下沉为对象骨架和对象级守卫 |
| L6 | WP01-T010 §3/§4/§6 | 恢复语义核对单已确认“建议权与执行权分层”：RecoveryOutcome 可承载 rejection_reason/escalation_reason，但不得承载 failure_root_cause、root_cause_analysis、belief_patch、plan_patch_hint |
| L7 | WP04-T009-D §3/§5 | RecoveryRequest 已冻结为 runtime admission input，并显式禁止预先写入 executed_action/final_runtime_state/checkpoint_ref/rejection_reason；这些语义因此只能落在 RecoveryOutcome |
| L8 | WP03-T012 / WP03-T013 | Checkpoint 已冻结为最小恢复状态对象，结果对象应通过 checkpoint_ref 引用，而不是重复嵌入 checkpoint 快照 |
| L9 | docs/todos/contracts/WP-04-边界对象TODO.md | T011-B 目标是 RecoveryOutcome.h/Guards + contract test，完成判定是“不混入失败归因语义” |

### 1.2 外部参考清单

| 编号 | 参考 | 与 T011 的映射 |
|---|---|---|
| E1 | Microsoft Azure Architecture Center: Retry Pattern | retry/重试控制必须在理解完整失败上下文的控制层实现，并显式考虑 idempotency；支撑 RecoveryOutcome 只记录执行结果，不在结果对象中重做失败归因 |
| E2 | Microsoft Azure Architecture Center: Compensating Transaction Pattern | 恢复/补偿流程需要 durable state、可续跑记录、补偿结果引用和可恢复的审计信息；支撑 RecoveryOutcome 保留 checkpoint_ref、compensation_result_ref、审计原因等控制元数据 |
| E3 | Ian Robinson / Martin Fowler: Consumer-Driven Contracts | provider 应围绕真实消费者依赖做 just-enough validation；支撑 T011 只冻结 RecoveryManager 输出真正需要的最小结果槽位和边界守卫 |
| E4 | Anthropic: Building Effective Agents | 复杂 agent 系统应保持简单、透明、可测试，并以显式 gate/guardrails 控制复杂性；支撑 T011 继续采用 lean object + layered guards + contract tests 的落地方式 |

### 1.3 对本任务的可落地启发

1. RecoveryOutcome 的核心职责是把 RecoveryManager 的“执行结果”与“控制元数据”做成单一稳定输出，而不是把失败分析再表达一遍。
2. 为保持与 WP04-T009/T010 的边界闭环，RecoveryOutcome 必须承接被 RecoveryRequest 明确禁止的结果层字段，但只冻结最小槽位，不提前实现 T012 的字段细则。
3. `checkpoint_ref` 应继续采用“引用既有冻结对象”策略，而不是嵌入第二份 Checkpoint；这能保持对象职责单一并减少兼容性风险。
4. `rejection_reason` / `escalation_reason` 属于 RecoveryManager 准入裁定后的审计原因，允许进入结果对象，但必须与失败归因字段严格区分。
5. T011 的自动化守卫应优先覆盖对象级不变量和归因禁区，字段 hygiene 与组合约束留给 T012，保持工作包范围收敛。

---

## 2. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 RecoveryOutcome 在恢复责任链中的对象定位 | ADR-007 §3.4/§4/§5.3、WP04-T006-D §5 | 本文件 §3 | 明确生产者/消费者/调用顺位，且未侵入 ReflectionDecision/RecoveryRequest 主职责 | 若出现失败归因或准入输入表述，回退到 runtime-owned execution result 口径 |
| D2 | 冻结 RecoveryOutcome 的最小槽位集合 | ADR-007 §5.3、WP01-T010、WP03-T012 | 本文件 §4 | 2 个必填 + 5 个可选槽位明确，且复用 checkpoint_ref 引用模式 | 若出现嵌入 Checkpoint 或新增失败分析字段，回退到既有冻结对象 |
| D3 | 冻结 RecoveryOutcome 顶层禁区 | ADR-007 §5.3、WP04-T006-D、WP01-T010 | 本文件 §5 | 4 个失败归因禁区明确，且不扩张到 RecoveryManager 策略实现 | 若出现 belief/plan patch 类字段，按 ADR-007 直接阻断 |
| D4 | 输出 T011 的 Design→Build 三件套 | WP04 TODO、现有 checkpoint contract 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整 | 若需要跨包引入新对象族，判定越界并回退 |
| D5 | 形成 D Gate 结果 | D1-D4 | 本文件 §7 | Gate 为 PASS/Blocked 二值 | 若对象定位、槽位或三件套未闭合，则 Blocked |

---

## 3. 对象定位与责任链

### 3.1 核心职责

RecoveryOutcome 是 RecoveryManager 的 runtime-owned execution result object。

它负责把以下两类信息收敛到同一个 contracts 边界里：

1. RecoveryManager 最终实际采取了什么恢复动作。
2. 该动作对应的运行态结果、计数快照和审计引用/原因。

RecoveryOutcome 不是：

1. 第二个 ReflectionDecision，不负责表达 failure_root_cause、root_cause_analysis、plan_patch_hint 等失败归因或计划修补语义。
2. 第二个 RecoveryRequest，不承接 reflection_decision、error_info、latest_observation、checkpoint 等准入输入证据。
3. 完整执行日志，不内嵌 Observation/Checkpoint 全量内容，只暴露结果所需的最小控制元数据。

### 3.2 调用链位置

```text
Observation + ErrorInfo + Checkpoint
  -> ReflectionDecision
  -> RecoveryRequest
  -> RecoveryManager admission + execution
  -> RecoveryOutcome
```

### 3.3 生产者与消费者

| 角色 | 与 RecoveryOutcome 的关系 |
|---|---|
| RecoveryManager | 唯一生产者；在准入裁定和恢复执行后生成对象 |
| Runtime 审计链路 | 消费执行动作、终态、checkpoint_ref 和 rejection/escalation 原因 |
| Contract tests / CI gate | 消费对象结构与 guard 结果，验证执行结果语义未回退为失败归因对象 |

---

## 4. 最小语义槽位

RecoveryOutcome 固定冻结为 2 个必填槽位 + 5 个可选槽位。

### 4.1 必填槽位（2 项）

| 字段 | 类型 | 语义 | 冻结原因 |
|---|---|---|---|
| `executed_action` | `string` | RecoveryManager 实际采取的恢复动作名称/类别 | ADR-007 §5.3 明示 RecoveryOutcome 必须表达 executed_action |
| `final_runtime_state` | `string` | 恢复动作完成后的运行态结论 | ADR-007 §5.3 明示 RecoveryOutcome 必须表达 final_runtime_state |

### 4.2 可选槽位（5 项）

| 字段 | 类型 | 语义 | 说明 |
|---|---|---|---|
| `updated_retry_count` | `uint32` | 恢复后更新的重试计数快照 | 复用仓内 `retry_counters -> retry_count` 简化口径 |
| `checkpoint_ref` | `string` | 关联的 checkpoint 标识引用 | 复用 WP03 Checkpoint 引用模式，不嵌入对象本体 |
| `compensation_result_ref` | `string` | 补偿动作或补偿记录引用 | 承接 ADR-007 的控制元数据槽位 |
| `rejection_reason` | `string` | 准入被拒绝时的审计原因 | 允许进入结果对象，但只表达裁定原因，不表达失败归因 |
| `escalation_reason` | `string` | 升级/人工介入时的审计原因 | 与 rejection_reason 同层，二者不能同时为非空 |

### 4.3 最小职责声明

RecoveryOutcome 只回答结果层的五个问题：

1. RecoveryManager 最终执行了什么动作。
2. 该动作结束后 runtime 处于什么结果状态。
3. 若有重试计数变化，新的快照是多少。
4. 若生成或推进了 checkpoint/补偿记录，对应引用是什么。
5. 若未执行或升级，审计原因是什么。

它不回答：

1. 为什么产生该失败根因。
2. 认知层应如何修补 plan/belief。
3. 失败输入证据的完整原文或完整恢复策略细节。

---

## 5. 顶层禁区与边界声明

RecoveryOutcome 顶层不得承载以下四类失败归因字段：

| 禁止字段 | 禁止原因 |
|---|---|
| `failure_root_cause` | 失败归因属于 ReflectionDecision |
| `root_cause_analysis` | 根因分析属于 cognition 层 |
| `belief_patch` | belief 修订不属于恢复结果对象 |
| `plan_patch_hint` | 计划语义修补属于 ReflectionDecision |

对象级边界还需满足以下两条：

1. `rejection_reason` 与 `escalation_reason` 不能同时为非空，否则结果对象会混入双重裁定语义。
2. optional string 槽位若 present，则必须为 non-empty；空字符串不构成稳定契约语义。

---

## 6. Design→Build 映射

| 设计结论 | Build 落地点 | 说明 |
|---|---|---|
| 冻结 RecoveryOutcome 对象骨架 | `contracts/include/checkpoint/RecoveryOutcome.h` | 只定义结果对象和最小槽位，不展开 T012 字段细则 |
| 冻结 required/boundary guards 与归因禁区包装 | `contracts/include/checkpoint/RecoveryOutcomeGuards.h` | required/boundary 规则在本文件实现；失败归因禁区复用 `boundary/RecoveryBoundaryGuards.h` |
| 提供 T011 contract gate | `tests/contract/checkpoint/RecoveryOutcomeContractTest.cpp` | 覆盖正例与失败归因/双审计原因负例 |
| 接入合同测试注册 | `tests/contract/CMakeLists.txt` | 新增 RecoveryOutcomeContractTest |

代码目标：`contracts/include/checkpoint/RecoveryOutcome.h`、`contracts/include/checkpoint/RecoveryOutcomeGuards.h`

测试目标：`tests/contract/checkpoint/RecoveryOutcomeContractTest.cpp`

验收命令：`cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryOutcomeContractTest --output-on-failure`

---

## 7. D Gate 结果

| D 原子项 | 状态 | 说明 |
|---|---|---|
| D1：对象定位 | ✅ Done | RecoveryOutcome 已明确为 runtime-owned execution result object，不是建议对象也不是准入输入 |
| D2：最小槽位 | ✅ Done | 2 必填 + 5 可选槽位冻结，并沿用 checkpoint_ref 引用模式 |
| D3：顶层禁区 | ✅ Done | 4 个失败归因禁区与对象级不变量已明确 |
| D4：D→B 三件套 | ✅ Done | 代码目标、测试目标、验收命令已锁定 |
| D5：进入 B 判定 | ✅ Done | 无阻塞项 |

**Gate 结论：PASS — 可进入 WP04-T011-B**

进入 B 的条件：

1. ✅ RecoveryOutcome 已限定为执行结果与控制元数据对象。
2. ✅ 已明确复用 RecoveryBoundaryGuards 的失败归因禁区目录。
3. ✅ 已明确 checkpoint 采用引用而非嵌入模式。
4. ✅ 代码、测试、验收命令三件套已锁定。

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| RecoveryOutcome 允许字段族 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §5.3 |
| 建议权/执行权分层 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §3.4 / §4 |
| 当前架构对 ADR-007 的摘要结论 | docs/architecture/DASSALL_Agent_architecture.md | ADR-007 摘要 |
| Recovery 链路对象地图与阶段 3 约束 | docs/plans/DASALL_contracts冻结实施计划.md | §7 / §8 阶段 3 |
| RecoveryOutcome 影响项前置结论 | docs/todos/contracts/deliverables/WP04-T006-ADR007对象影响清单.md | §5 |
| 恢复语义核对基线 | docs/todos/contracts/deliverables/WP01-T010-恢复语义核对单.md | §3 / §4 / §6 |
| RecoveryRequest 结果层禁区 | docs/todos/contracts/deliverables/WP04-T009-RecoveryRequest语义说明.md | §3 / §5 |
| Checkpoint 引用模式 | docs/todos/contracts/deliverables/WP03-T012-Checkpoint语义说明.md | §2 / §5 |
| T011 来源与完成判定 | docs/todos/contracts/WP-04-边界对象TODO.md | WP04-T011 行 |

### 8.2 外部业界参考

1. Microsoft Azure Architecture Center, Retry Pattern  
   结论：retry 逻辑应放在理解完整失败上下文的控制层，并显式考虑 idempotency 与事务一致性。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/retry
2. Microsoft Azure Architecture Center, Compensating Transaction Pattern  
   结论：恢复/补偿需要 durable state、补偿引用和可恢复的审计记录，补偿步骤应支持 idempotent commands。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/compensating-transaction
3. Ian Robinson / Martin Fowler, Consumer-Driven Contracts  
   结论：提供方应围绕消费者真实依赖实施 just-enough validation，并以自动化测试维持契约稳定。  
   参考：https://martinfowler.com/articles/consumerDrivenContracts.html
4. Anthropic, Building Effective Agents  
   结论：复杂 agent 系统应保持简单、透明、可测试，并通过显式 guardrails 约束复杂控制流。  
   参考：https://www.anthropic.com/engineering/building-effective-agents