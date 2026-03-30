# WP04-T007-D：ReflectionDecision 职责边界语义说明

> 版本：1.0 | 日期：2026-03-17 | 状态：Done
> 任务编号：WP04-T007-D
> 上游输入：ADR-007 §3.2/§3.4/§5.1、WP04-T006-D §3、WP01-T010-恢复语义核对单、WP03-T012-Checkpoint 语义说明、WP02-T009/T010/T012 横切规范

---

## 1. 研究结论摘要

### 1.1 本地证据清单

| 编号 | 证据 | 对 T007 的直接约束 |
|---|---|---|
| L1 | ADR-007 §3.2 / §5.1 | ReflectionDecision 只表达失败语义判断与建议，不得携带 retry_after_ms、backoff_strategy、lease_extension、checkpoint_blob、circuit_breaker_transition 等运行时调度字段 |
| L2 | ADR-007 §3.4 | ReflectionDecision 先拥有建议权，RecoveryManager 再拥有恢复准入裁定权；因此对象只能表示 suggestion，不得等价为已执行动作 |
| L3 | WP04-T006-D §3 | T006 已冻结 ReflectionDecision 的允许语义类别：decision_kind、rationale、confidence、relevant_observation_refs 与 hint 类建议；T007 只能在该边界内定义对象 |
| L4 | WP01-T010 §3/§6 | 恢复语义核对单已确认 ReflectionDecision 的职责是建议权，不承载执行调度细节；后续对象必须可被字段守卫阻断越界 |
| L5 | Observation.h 注释 | ReflectionDecision.relevant_observation_refs 是 Observation 的合法消费者引用，说明对象需要显式保留观测证据引用能力 |
| L6 | contracts 冻结实施计划 §6.3 | contracts 设计必须先冻结语义，再冻结 schema；T007 只做职责边界与对象骨架，字段细则和组合规则留给 T008 |
| L7 | WP04 TODO T007/T008 | T007-B 目标是“新增 ReflectionDecision 契约对象与守卫”；T008 再做字段表与字段校验器，说明本任务不应提前实现字段级组合规则全集 |

### 1.2 外部参考清单

| 编号 | 参考 | 与 T007 的映射 |
|---|---|---|
| E1 | Microsoft Azure Architecture Center: Retry Pattern | 重试逻辑只能放在理解完整失败上下文的控制层；支撑 ReflectionDecision 只保留语义建议而不携带调度参数 |
| E2 | Anthropic: Building Effective Agents | 成熟 agent 设计应保持简单、可组合，并用显式 guardrails 和测试隔离复杂控制流；支撑 ReflectionDecision 维持 lean contract + contract test gate |
| E3 | Martin Fowler / Ian Robinson: Consumer-Driven Contracts | 契约应围绕消费者真实需求保持“just enough validation”，避免对象膨胀；支撑 T007 只冻结最小建议语义槽位，不把恢复执行细节并入对象 |

### 1.3 对本任务的可落地启发

1. ReflectionDecision 必须被实现为认知层 suggestion object，而不是 runtime control object；对象本体只表达“怎么理解失败、建议下一步做什么”。
2. 为避免跨包扩张，T007 只冻结最小对象骨架与 required/boundary guards，字段 hygiene、组合规则和可选字段细表延后到 T008。
3. 与 ADR-007 明示的多类 hint 保持兼容的最小方式，是先冻结一个 `hint_ref` 引用槽位，而不是在 T007 直接展开多个 hint payload 字段族。
4. 调度越界不能靠文档约束，必须通过现有 `RecoveryBoundaryGuards` 在 T007-B 的 contract test 中显式验证 `retry_after_ms` 之类字段被拒绝。
5. 观测证据引用应保留为 `relevant_observation_refs`，因为 ReflectionDecision 的建议需要能追溯回 Observation，而不是只存自由文本理由。

---

## 2. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 ReflectionDecision 在失败责任链中的对象定位 | ADR-007 §3.2/§3.4、WP04-T006-D §2 | 本文件 §3 | 明确生产者/消费者/调用顺位，且未侵入 RecoveryManager 执行域 | 若出现执行控制描述，则回退到“建议权对象”口径 |
| D2 | 冻结 ReflectionDecision 的最小语义槽位 | ADR-007 §5.1、Observation.h、WP02-T009/T010 | 本文件 §4 | Required/Optional 槽位明确，且不提前展开 T008 字段规则全集 | 若字段膨胀，回退到最小槽位集合 |
| D3 | 冻结 ReflectionDecision 的调度禁区 | ADR-007 §5.1、WP01-T010、WP04-T006-D §3.4 | 本文件 §5 | 5 个调度禁区与守卫映射清晰可追溯 | 若混入 runtime 字段，按 ADR-007 直接阻断 |
| D4 | 输出 T007 的 D→B 映射三件套 | WP04 TODO、既有 Prompt/Checkpoint 对象模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整 | 若需要新增非 T007 对象，判定跨包扩张并回退 |
| D5 | 形成 D Gate 结论并判定是否可进入 B | D1-D4 结果 | 本文件 §7 | Gate 为二值结论，阻塞项可执行 | 若对象边界或三件套未闭合，则判定 Blocked |

---

## 3. 对象定位与调用链

### 3.1 核心职责

ReflectionDecision 是 ReflectionEngine 的失败语义判断对象。

它只负责回答两类问题：

1. 当前失败在语义上属于什么类型。
2. 认知层建议的下一步方向是什么。

ReflectionDecision 不是：

1. RecoveryManager 的执行请求对象。
2. 已经批准执行的 retry/replan 控制动作。
3. checkpoint、backoff、lease、熔断状态的承载体。
4. runtime state machine 的状态快照。

### 3.2 调用链位置

```text
Observation + ErrorInfo + BeliefState + Checkpoint context
  -> ReflectionEngine
  -> ReflectionDecision
  -> RecoveryManager(ReflectionDecision + Checkpoint + Budget + Idempotency)
  -> RecoveryOutcome
```

### 3.3 生产者与消费者

| 角色 | 与 ReflectionDecision 的关系 |
|---|---|
| ReflectionEngine | 唯一生产者 |
| RecoveryManager | 直接消费者；读取建议，不直接接受执行调度参数 |
| Planner / Runtime Orchestrator | 间接消费者；在 replan/abort_safe 路径中按 runtime 准入规则消费 |
| 审计/测试链路 | 消费 decision_kind、rationale、hint_ref、observation refs 做可追溯验证 |

---

## 4. 最小语义槽位

### 4.1 决策枚举

ReflectionDecision 的决策类型冻结为认知层建议枚举，而不是 runtime action：

| 枚举值 | 语义 |
|---|---|
| `Unspecified` | 未指定哨兵值，守卫必须拒绝 |
| `Continue` | 失败不构成恢复分支，允许主链继续 |
| `RetryStep` | 建议在运行时准入通过后重试当前步骤 |
| `Replan` | 建议切换到重规划路径 |
| `AbortSafe` | 建议进入安全终止/失败收敛 |

### 4.2 Required 槽位（T007 冻结）

| 字段 | 类型 | 语义 | 冻结原因 |
|---|---|---|---|
| `request_id` | `string` | 追溯到本轮 AgentRequest | 横切标识规范要求认知对象可溯源 |
| `decision_kind` | `ReflectionDecisionKind` | 当前失败语义建议类型 | ADR-007 §5.1 明确要求 |
| `rationale` | `string` | 失败语义解释与建议原因 | 保证建议不是黑箱布尔值 |

### 4.3 Optional 槽位（T007 冻结）

| 字段 | 类型 | 语义 | 说明 |
|---|---|---|---|
| `goal_id` | `string` | 多目标关联引用 | 仅做追溯，不增加执行语义 |
| `confidence` | `float` | 建议可信度，范围为 $[0, 1]$ | 数值规则留到 T007-B/T008 验证 |
| `relevant_observation_refs` | `vector<string>` | 支撑建议的 Observation 引用 | 保持建议与证据的可追溯关系 |
| `hint_ref` | `string` | 指向 plan/replan/clarification/skill-switch 提示资产的统一引用槽位 | 用单引用槽位承接多类 hint，避免 T007 对 hint payload 过度展开 |
| `created_at` | `int64` | 生成时间戳 | 审计与时间排序元数据 |
| `tags` | `vector<string>` | 审计/检索标签 | 不承载执行控制信号 |

### 4.4 边界解释

1. `decision_kind` 与 `rationale` 是 T007 的对象语义核心，必须在 B 阶段具备 required guards。
2. `confidence`、`hint_ref`、`relevant_observation_refs` 在 T007 只冻结为合法槽位；更细的 hygiene 和组合规则留给 T008。
3. `hint_ref` 采用“引用而非嵌入”策略，避免 ReflectionDecision 演化为 plan patch payload 或 runtime command 容器。

---

## 5. 调度禁区与守卫映射

ReflectionDecision 不得承载以下运行时调度字段：

| 禁止字段 | 禁止原因 | 来源 |
|---|---|---|
| `retry_after_ms` | 重试时延属于 RecoveryManager 调度控制 | ADR-007 §5.1 |
| `backoff_strategy` | 退避策略属于 runtime 恢复执行层 | ADR-007 §5.1 |
| `lease_extension` | 租约延长属于 runtime/协同控制 | ADR-007 §5.1 |
| `checkpoint_blob` | checkpoint 持久化属于 runtime | ADR-007 §5.1 |
| `circuit_breaker_transition` | 熔断迁移属于恢复准入裁定 | ADR-007 §5.1 |

对应守卫复用：

1. `kReflectionSchedulingForbiddenFields`
2. `evaluate_reflection_decision_field_boundary()`

T007-B 不重新定义该禁区规则，只在 ReflectionDecision 的 contract test 中复用并验证。

---

## 6. Design→Build 映射

| 设计结论 | Build 落地点 | 说明 |
|---|---|---|
| 冻结 ReflectionDecision 最小对象骨架 | `contracts/include/checkpoint/ReflectionDecision.h` | 仅定义对象与决策枚举，不实现 T008 级字段规则 |
| 冻结 required/boundary 守卫并接入调度禁区包装 | `contracts/include/checkpoint/ReflectionDecisionGuards.h` | 复用 T006 RecoveryBoundaryGuards，阻断调度字段越界 |
| 提供 T007 contract gate | `tests/contract/checkpoint/ReflectionDecisionContractTest.cpp` | 至少覆盖 1 个正例 + 1 个负例 |
| 接入合同测试注册 | `tests/contract/CMakeLists.txt` | 新增 ReflectionDecisionContractTest |

代码目标：`contracts/include/checkpoint/ReflectionDecision.h`、`contracts/include/checkpoint/ReflectionDecisionGuards.h`

测试目标：`tests/contract/checkpoint/ReflectionDecisionContractTest.cpp`

验收命令：`cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ReflectionDecisionContractTest --output-on-failure`

---

## 7. D Gate 结果

| D 原子项 | 状态 | 说明 |
|---|---|---|
| D1：对象定位 | ✅ Done | ReflectionDecision 已明确为 ReflectionEngine 的建议对象 |
| D2：最小槽位 | ✅ Done | 3 必填 + 6 可选槽位冻结，未越界到 T008 字段细则 |
| D3：调度禁区 | ✅ Done | 5 个运行时调度禁区与既有守卫映射明确 |
| D4：D→B 三件套 | ✅ Done | 代码目标、测试目标、验收命令已锁定 |
| D5：进入 B 判定 | ✅ Done | 无阻塞项 |

**Gate 结论：PASS — 可进入 WP04-T007-B**

进入 B 的条件：

1. ✅ 对象边界已明确为 suggestion-only，不包含 runtime schedule。
2. ✅ 最小对象槽位已冻结，可直接落头文件与 required/boundary guards。
3. ✅ 调度禁区已有复用守卫，无需重复实现。
4. ✅ 代码、测试、验收命令三件套已锁定。

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| ReflectionDecision suggestion-only 边界 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §3.2 / §5.1 |
| 最终裁定权分层 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §3.4 |
| Recovery 责任链 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §4 |
| T006 允许语义与禁区 | docs/todos/contracts/deliverables/WP04-T006-ADR007对象影响清单.md | §3 |
| 建议权/执行权核对 | docs/todos/contracts/deliverables/WP01-T010-恢复语义核对单.md | §3 / §4 / §6 |
| Observation 对 relevant_observation_refs 的消费者提示 | contracts/include/observation/Observation.h | 注释段 |
| contracts 分波次冻结原则 | docs/plans/DASALL_contracts冻结实施计划.md | §6.2 / §6.3 |
| T007/T008 边界划分 | docs/todos/contracts/WP-04-边界对象TODO.md | §4 / §5 |

### 8.2 外部业界参考

1. Microsoft Azure Architecture Center, Retry Pattern  
   结论：retry logic 只能放在真正理解失败上下文的控制层，避免多层 retry 叠加。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/retry
2. Anthropic, Building Effective Agents  
   结论：复杂 agent 应保持简单、可组合，并依赖显式 guardrails 与测试。  
   参考：https://www.anthropic.com/engineering/building-effective-agents
3. Martin Fowler / Ian Robinson, Consumer-Driven Contracts  
   结论：契约应围绕消费者真实需求做“just enough validation”，避免对象膨胀。  
   参考：https://martinfowler.com/articles/consumerDrivenContracts.html