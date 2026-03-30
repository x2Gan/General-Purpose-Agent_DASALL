# WP04-T012-D：RecoveryOutcome 字段表

> 版本：1.0 | 日期：2026-03-18 | 状态：Done
> 任务编号：WP04-T012-D
> 上游输入：WP04-T011-D/B、ADR-007 §3.4/§4/§5.3、WP04-T006-D、WP01-T010、docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md

---

## 1. 任务识别

### 1.1 范围

- 把 T011 已冻结的 RecoveryOutcome 对象骨架进一步固化为字段级清单、字段 hygiene 规则和最小组合约束。
- 定义 RecoveryOutcome 的三层校验堆叠：L1 必填、L2 边界、L3 字段规则。
- 明确 2 个必填字段 + 5 个可选字段的字段级约束，并把规则映射到 `RecoveryOutcomeGuards.h` 的最小增量实现。
- 为 `RecoveryOutcomeFieldContractTest.cpp` 提供正例/负例的直接判定依据。

### 1.2 排除项

- 不新增 RecoveryOutcome 字段。
- 不改写 T011 已冻结的对象定位、槽位集合和失败归因禁区。
- 不发明新的恢复动作枚举、运行态枚举或 RecoveryManager 策略 DSL。
- 不扩张到 RecoveryRequest、ReflectionDecision 或 Checkpoint 的字段设计。
- 不把失败归因、belief 修补、plan 修补重新带回 RecoveryOutcome。

---

## 2. Phase 0：研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T012 的直接约束 |
|---|---|---|
| L1 | WP04-T011-D §4/§5 | RecoveryOutcome 已冻结为 2 必填 + 5 可选槽位；T012 只能补字段规则，不能改对象边界 |
| L2 | ADR-007 §3.4 / §4 / §5.3 | RecoveryOutcome 只表达 RecoveryManager 的执行结果与控制元数据，不负责失败归因或计划修补 |
| L3 | WP04-T006-D §5 | RecoveryOutcome 允许字段族与 4 个失败归因禁区已冻结，T012 只应在既有允许字段内做 field hygiene |
| L4 | WP01-T010 §3 / §4 / §6 | RecoveryOutcome 可承载 `rejection_reason` / `escalation_reason`，但必须维持“建议权与执行权分层”，并可追溯拒绝原因 |
| L5 | WP04-T011-D §4.2 / §4.3 | `checkpoint_ref`、`compensation_result_ref` 是两类不同控制引用；`rejection_reason`、`escalation_reason` 是审计原因，不应与引用槽位语义塌缩 |
| L6 | docs/plans/DASALL_contracts冻结实施计划.md §3.2 / §6.3 | contracts 冻结遵循“语义先于 schema、兼容优先、consumer-driven just-enough validation” |
| L7 | docs/todos/contracts/WP-04-边界对象TODO.md | T012 完成判定要求覆盖 `executed_action` / `final_runtime_state` / `rejection_reason` / `checkpoint_ref`，并把非法字段混入阻断保持为 gate |

### 2.2 外部参考清单

| # | 来源 | 对 T012 的映射 |
|---|---|---|
| E1 | Microsoft Azure Architecture Center: Best practices for RESTful web API design | 资源表示应围绕业务实体而不是内部实现，新增字段通常比重定义旧字段更安全；支持 T012 只补字段 hygiene，不改对象职责 |
| E2 | Proto Best Practices | 面向长期演进的契约应避免重定义既有字段语义或类型，优先做兼容式增量约束；支持 T012 继续复用 string 槽位并加自动化校验，而不是重构为新类型 |
| E3 | Microsoft Azure Architecture Center: Retry Pattern | 恢复控制需要显式、可审计的状态与标识；支持 T012 强化结果对象中 action/state/reason/reference 的“可读且不含糊”规则 |

### 2.3 对本任务的可落地启发

1. T012 的重点不是再定义 RecoveryOutcome，而是把 T011 的 7 个槽位收敛为可自动执行的字段 hygiene 规则。
2. 由于 T011 没有冻结动作枚举和运行态枚举，T012 不应凭空发明固定取值集合；更稳妥的做法是约束“字符串必须有实际内容”，以及“不同语义槽位不能塌缩成同一引用”。
3. `checkpoint_ref` 与 `compensation_result_ref` 属于不同控制工件引用；如果两个字段复用同一标识，会削弱审计与续跑可读性，应在字段层阻断。
4. `rejection_reason` / `escalation_reason` 已在 T011 冻结为审计原因，T012 应把“meaningful when present”进一步明确为“不能只有空白字符”。
5. T012 仍应继承 T011 的非法字段 gate，保持失败归因字段混入时立即阻断，而不是把这类保护转移到下游实现。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 RecoveryOutcome 全字段清单与层次归属 | T011-D、RecoveryOutcome.h | 本文件 §4 | 2 必填 + 5 可选字段全部列出，且不新增字段 | 若出现新字段，回退到 T011 已冻结集合 |
| D2 | 明确字符串槽位的“有意义内容”规则 | T011-D §5、WP01-T010、Azure API Design | 本文件 §5.1/§5.2 | 必填与可选字符串都具备可程序化 hygiene 规则 | 若演化为动作枚举表，回退到字符串 hygiene 口径 |
| D3 | 定义最小组合规则，避免不同控制引用和审计槽位塌缩 | T011-D §4.2/§4.3、Retry Pattern | 本文件 §5.3/§5.4 | 至少 1 条可程序化组合规则，且不发明新状态机 | 若依赖运行时策略值，删除并回退为字段独立规则 |
| D4 | 输出 T012 的 Design→Build 三件套 | WP04 TODO、现有字段表模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整 | 若需要改动对象结构，判定越界并回退 |
| D5 | 形成 D Gate 结论 | D1-D4 | 本文件 §7 | Gate 为 PASS/Blocked 二值 | 若字段表未闭合或三件套缺失，则 Blocked |

---

## 4. 字段全集与分组

RecoveryOutcome 固定由 2 个必填字段 + 5 个可选字段组成，总数锁定为 7。

### 4.1 必填字段（2 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `executed_action` | `std::optional<std::string>` | RecoveryManager 实际采取的恢复动作名称/类别 | present；至少包含 1 个非空白字符 | L1 + L3 |
| `final_runtime_state` | `std::optional<std::string>` | 恢复动作完成后的运行态结论 | present；至少包含 1 个非空白字符 | L1 + L3 |

### 4.2 可选字段（5 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| `updated_retry_count` | `std::optional<std::uint32_t>` | 恢复后更新的重试计数快照 | present 时无需额外数值约束；`uint32_t` 已天然保证非负 | L2 |
| `checkpoint_ref` | `std::optional<std::string>` | 关联 checkpoint 的标识引用 | present 时至少包含 1 个非空白字符；若与 `compensation_result_ref` 同时 present，则二者不得相同 | L2 + L3 |
| `compensation_result_ref` | `std::optional<std::string>` | 关联补偿结果的标识引用 | present 时至少包含 1 个非空白字符；若与 `checkpoint_ref` 同时 present，则二者不得相同 | L2 + L3 |
| `rejection_reason` | `std::optional<std::string>` | 准入拒绝时的审计原因 | present 时至少包含 1 个非空白字符；与 `escalation_reason` 互斥 | L2 + L3 |
| `escalation_reason` | `std::optional<std::string>` | 升级/人工介入时的审计原因 | present 时至少包含 1 个非空白字符；与 `rejection_reason` 互斥 | L2 + L3 |

---

## 5. 字段级规则总表

### 5.1 通用规则

| 规则编号 | 规则 | 适用字段 |
|---|---|---|
| R1 | required string 字段必须 present 且非空 | `executed_action`, `final_runtime_state` |
| R2 | optional string 字段若 present，则必须 non-empty | `checkpoint_ref`, `compensation_result_ref`, `rejection_reason`, `escalation_reason` |
| R3 | 所有 string 槽位必须至少包含 1 个非空白字符，不能只由空格、制表符或换行组成 | 所有 6 个 string 字段 |
| R4 | 审计原因字段互斥 | `rejection_reason`, `escalation_reason` |
| R5 | 不同控制引用字段同时 present 时必须保持不同标识 | `checkpoint_ref`, `compensation_result_ref` |

### 5.2 三层堆叠校验设计

#### Layer 1：必填字段存在性（T011-B 已实现）

| 规则 | 校验内容 |
|---|---|
| L1-R1 | `executed_action` 必须存在且非空 |
| L1-R2 | `final_runtime_state` 必须存在且非空 |

#### Layer 2：边界约束（T011-B 已实现）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 L1 规则 |
| L2-R1 | `checkpoint_ref` present 时必须 non-empty |
| L2-R2 | `compensation_result_ref` present 时必须 non-empty |
| L2-R3 | `rejection_reason` present 时必须 non-empty |
| L2-R4 | `escalation_reason` present 时必须 non-empty |
| L2-R5 | `rejection_reason` 与 `escalation_reason` 不得同时 present |

#### Layer 3：字段规则（T012-B 新增）

| 规则 | 校验内容 | 来源 |
|---|---|---|
| 继承 L2 | 全部 L1 + L2 规则 | 仓内统一 guard 堆叠模式 |
| L3-R1 | `executed_action` 必须包含至少 1 个非空白字符 | T011-D 对“实际动作名称/类别”的要求 |
| L3-R2 | `final_runtime_state` 必须包含至少 1 个非空白字符 | T011-D 对“运行态结论”的要求 |
| L3-R3 | `checkpoint_ref`、`compensation_result_ref`、`rejection_reason`、`escalation_reason` present 时必须包含至少 1 个非空白字符 | T011-D 对“meaningful when present”的细化 |
| L3-R4 | `checkpoint_ref` 与 `compensation_result_ref` 若同时 present，则不得使用同一标识 | T011-D 中两类控制引用职责不同，不能塌缩为同一工件 |

### 5.3 字段解释

1. T012 不新增动作枚举或运行态枚举，是为了遵守 T011 已冻结的 string 槽位设计，避免跨工作包重定义对象类型。
2. “non-empty” 在 T011 只保证了字符串长度不为 0；T012 把“meaningful when present”进一步细化为“不能只有空白字符”，这样审计、日志和 gate 才能稳定消费。
3. `checkpoint_ref` 与 `compensation_result_ref` 都是引用字段，但它们分别指向恢复锚点和补偿结果；如果两个字段复用同一标识，会让结果对象失去区分不同控制工件的能力。
4. `updated_retry_count` 在 T012 不追加策略规则，因为是否更新、何时更新属于 RecoveryManager 的运行控制，而不是 contracts 的字段表职责。

### 5.4 最小组合规则

| 规则编号 | 非法组合 | 判定原因 |
|---|---|---|
| C1 | `checkpoint_ref` 与 `compensation_result_ref` 同时 present 且值相同 | RecoveryOutcome 无法区分恢复锚点引用和补偿结果引用，审计语义塌缩 |

说明：

- T012 不新增 `executed_action -> final_runtime_state` 的动作/状态映射表，因为仓内尚未冻结统一动作枚举；该类规则属于后续 runtime 语义收敛，而不是当前字段表。
- 这符合 consumer-driven contracts 的 just-enough validation 原则：只校验当前消费者稳定依赖、且可以自动化执行的字段关系。

---

## 6. Design→Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | `contracts/include/checkpoint/RecoveryOutcomeGuards.h`（新增 `validate_recovery_outcome_field_rules` 与字符串 whitespace/ref-distinct 辅助校验） |
| 测试目标 | `tests/contract/checkpoint/RecoveryOutcomeFieldContractTest.cpp` |
| 验收命令 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryOutcomeFieldContractTest --output-on-failure` |

---

## 7. D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 7 个字段已全覆盖；L1/L2/L3 分层闭合；新增规则都限定在字符串 hygiene 和引用去塌缩，未越界到 RecoveryManager 策略或新枚举 |

**Gate 结论：PASS — 可进入 WP04-T012-B**

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T012 来源与三件套 | docs/todos/contracts/WP-04-边界对象TODO.md | WP04-T012 行 |
| RecoveryOutcome 对象边界 | docs/todos/contracts/deliverables/WP04-T011-RecoveryOutcome语义说明.md | §4/§5/§6 |
| RecoveryOutcome 影响项与允许字段族 | docs/todos/contracts/deliverables/WP04-T006-ADR007对象影响清单.md | §5 |
| 建议权/执行权分层与审计原因定位 | docs/todos/contracts/deliverables/WP01-T010-恢复语义核对单.md | §3/§4/§6 |
| ADR-007 对 RecoveryOutcome 的权威约束 | docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md | §3.4/§4/§5.3 |
| contracts 字段冻结纪律 | docs/plans/DASALL_contracts冻结实施计划.md | §3.2/§6.3 |

### 8.2 外部业界参考

1. Microsoft Azure Architecture Center, Best practices for RESTful web API design  
   结论：资源表示应围绕业务实体，已有表示新增字段通常比重定义旧字段更安全；请求/响应中的标识与表示应保持清晰、可演进。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/best-practices/api-design
2. Proto Best Practices  
   结论：长期演进契约应避免重定义字段语义或类型，优先采用兼容式增量演进。  
   参考：https://protobuf.dev/best-practices/dos-donts/
3. Microsoft Azure Architecture Center, Retry Pattern  
   结论：恢复控制需要显式、可审计的状态与标识，避免依赖隐式推断。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/retry