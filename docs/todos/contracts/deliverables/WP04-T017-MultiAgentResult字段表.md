# WP04-T017-D：MultiAgentResult 字段表

> 版本：1.0 | 日期：2026-03-19 | 状态：Done
> 任务编号：WP04-T017-D
> 上游输入：WP04-T016-D/B、WP04-T013-D/B、ADR-008 §3.2/§3.3/§4/§5.2、docs/architecture/DASSALL_Agent_architecture.md §4.8/§6.11、docs/architecture/DASALL_Engineering_Blueprint.md §3.10/§7.1、docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md

---

## 1. 任务识别

### 1.1 范围

- 把 T016 已冻结的 MultiAgentResult 对象骨架下沉为字段级清单、字段规则和最小组合约束。
- 定义 MultiAgentResult 的三层校验堆叠：L1 必填、L2 边界、L3 字段规则。
- 锁定 3 个必填字段 + 3 个可选字段，不新增字段、不扩张到 WorkerTask/WorkerLease 或 AgentResult。
- 将 L3 规则映射到 contracts/include/agent/MultiAgentResultGuards.h 的最小增量实现和 tests/contract/agent/MultiAgentResultFieldContractTest.cpp。

### 1.2 排除项

- 不新增 MultiAgentResult 字段。
- 不改写 T016 已冻结的对象定位、最小槽位和顶层禁区结论。
- 不扩张到 ResultMerger 的合并算法、冲突仲裁策略或 Worker 选择逻辑。
- 不改写 ADR-008 关于全局主控权与最终输出权的结论。
- 不把 failure_summary 扩张为顶层 ErrorInfo，也不把 recommended_next_action 扩张为运行时动作 DSL。

---

## 2. Phase 0：研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T017 的直接约束 |
|---|---|---|
| L1 | WP04-T016-D §3/§4/§5 | MultiAgentResult 已冻结为 collaboration-result object，字段集合锁定为 3 必填 + 3 可选；T017 只能补字段规则，不能改对象边界 |
| L2 | ADR-008 §3.2/§3.3/§4 | MultiAgentResult 是 MultiAgentCoordinator 回传给 AgentOrchestrator 的协同结果，不是顶层 AgentResult，也不是 WorkerTask 图 |
| L3 | ADR-008 §5.2 | MultiAgentResult 至少包含 subtask_results、merged_result、conflicts、worker_trace_refs、failure_summary、recommended_next_action |
| L4 | WP04-T013-D §3.2 | 协同结果只表达聚合产物、冲突和下一步建议，不得替代最终 AgentResult |
| L5 | contracts/include/agent/MultiAgentResult.h | 当前对象骨架已经锁定 6 个字段，T017 只允许在现有对象面上补字段级 hygiene 与组合规则 |
| L6 | contracts/include/agent/MultiAgentResultGuards.h | T016 已实现 required/boundary guards；T017 应在其之上新增 field-rules guard，而不是重写 L1/L2 |
| L7 | contracts/include/agent/AgentResult.h | 顶层 AgentResult 已显式排除 subtask_results、merged_result、conflicts 等多 Agent 细节，说明 T017 不能把字段规则回推成最终结果语义 |
| L8 | docs/architecture/DASSALL_Agent_architecture.md §4.8/§6.11 | 多 Agent 协同属于主循环中的受控阶段；协同结果应保持可溯源、可聚合、可折叠，但不承载顶层完成态 |
| L9 | docs/architecture/DASALL_Engineering_Blueprint.md §3.10/§7.1 | contract 对象与 contract tests 继续落在 contracts/agent 与 tests/contract/agent，T017-B 代码目标只应是 Guards 增量和 field contract test |
| L10 | docs/todos/contracts/WP-04-边界对象TODO.md | T017 完成判定要求覆盖 subtask_results/merged_result/conflicts/worker_trace_refs/recommended_next_action，并把字段口径程序化 |

### 2.2 外部参考清单

| # | 来源 | 对 T017 的映射 |
|---|---|---|
| E1 | Anthropic, Building Effective Agents | orchestrator-workers 与 parallelization 都强调聚合结果要保持结构化、透明、可测试，并通过 guardrails/stopping conditions 防止错误级联 |
| E2 | Microsoft Azure Architecture Center, AI agent orchestration patterns | concurrent orchestration 明确要求有 conflict resolution strategy，且建议对中间代理输出做结构化验证，避免坏结果继续级联 |
| E3 | Proto Best Practices | 契约演进应兼容优先；字段应单一职责、避免大对象膨胀，repeated 字段的 repeatedness 不应随意改变，支持 T017 在既有 6 字段上做 just-enough validation |

### 2.3 对本任务的可落地启发

1. T017 的职责不是增加 MultiAgentResult 表达力，而是把 T016 的 6 个槽位收敛为“可自动验证的最小协同结果合同”。
2. subtask_results、conflicts、worker_trace_refs 都是聚合列表字段，应至少保证非空、非空白、可程序化消费，并防止重复项污染聚合结果。
3. merged_result 与 recommended_next_action 分别表达“聚合产物”和“主控建议”，字段层需要阻断两者语义塌缩为同一字符串槽位。
4. failure_summary 只表达协同阶段局部失败摘要，应保持可选且非空白，不得升级为顶层 ErrorInfo 镜像。
5. 字段测试应优先覆盖正例、空白字符串、重复列表项和语义塌缩等消费方真实依赖的失败面，而不是提前进入调度算法验证。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 MultiAgentResult 全字段清单和层次归属 | T016-D、MultiAgentResult.h | 本文件 §4 | 3 必填 + 3 可选字段全部列出，且不新增字段 | 若出现新增字段或跨对象挪用，回退到 T016 已冻结集合 |
| D2 | 明确每个字段的字段级规则与归属层（L1/L2/L3） | ADR-008 §5.2、MultiAgentResultGuards.h、MultiAgentBoundaryGuards.h | 本文件 §5 | 每个字段都映射到明确规则或已有 guard | 若规则与 T016 冲突，优先回退到 T016/L1-L2 已冻结口径 |
| D3 | 定义 T017 专属最小组合规则 | Anthropic/Azure 外部参考、WP04 TODO | 本文件 §5.4 | 至少 2 条可程序化组合规则，且不越界到聚合算法 | 若组合规则依赖运行时策略细节，删除并回退为字段 hygiene |
| D4 | 输出 T017 的 Design→Build 三件套 | WP04 TODO、既有 field-test 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整闭合 | 若需要改动非 T017 文件族，判定越界并回退 |
| D5 | 形成 D Gate 结论 | D1-D4 | 本文件 §7 | Gate 为 PASS/Blocked 二值 | 若字段表未闭合或三件套缺失，则 Blocked |

---

## 4. 字段全集与分组

MultiAgentResult 固定由 3 个必填字段 + 3 个可选字段组成，总数锁定为 6。

### 4.1 必填字段（3 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| subtask_results | std::optional<std::vector<std::string>> | 子任务结果引用或结构化摘要集合 | present；向量非空；元素包含非空白内容；元素唯一 | L1 + L2 + L3 |
| merged_result | std::optional<std::string> | 对子任务结果合并后的协同产出 | present；包含非空白内容 | L1 + L3 |
| recommended_next_action | std::optional<std::string> | 建议给 AgentOrchestrator 的下一步动作 | present；包含非空白内容；不得与 merged_result 归一化后等值 | L1 + L3 |

### 4.2 可选字段（3 项）

| 字段 | 类型 | 语义 | 字段级规则 | 校验层 |
|---|---|---|---|---|
| conflicts | std::optional<std::vector<std::string>> | 冲突、分歧或未决项集合 | present 时：向量非空、元素包含非空白内容、元素唯一 | L2 + L3 |
| worker_trace_refs | std::optional<std::vector<std::string>> | Worker 轨迹或审计引用 | present 时：向量非空、元素包含非空白内容、元素唯一 | L2 + L3 |
| failure_summary | std::optional<std::string> | 协同阶段局部失败摘要 | present 时必须包含非空白内容 | L2 + L3 |

---

## 5. 字段级规则总表

### 5.1 通用规则

| 规则编号 | 规则 | 适用字段 |
|---|---|---|
| R1 | required string 字段必须 present 且包含至少一个非空白字符 | merged_result, recommended_next_action |
| R2 | required vector 字段必须 present 且至少包含一个元素 | subtask_results |
| R3 | vector 字段元素必须包含至少一个非空白字符 | subtask_results, conflicts, worker_trace_refs |
| R4 | 聚合语义向量若 present，则元素必须唯一 | subtask_results, conflicts, worker_trace_refs |
| R5 | optional vector 若 present，则向量本身必须非空 | conflicts, worker_trace_refs |
| R6 | optional string 若 present，则必须包含至少一个非空白字符 | failure_summary |
| R7 | 聚合产物字段与主控建议字段必须保持语义分层 | merged_result, recommended_next_action |

### 5.2 三层堆叠校验设计

#### Layer 1：必填字段存在性（T016-B 已实现）

| 规则 | 校验内容 |
|---|---|
| L1-R1 | subtask_results 必须 present 且非空向量 |
| L1-R2 | merged_result 必须 present 且非空字符串 |
| L1-R3 | recommended_next_action 必须 present 且非空字符串 |

#### Layer 2：边界约束（T016-B 已实现）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 L1 规则 |
| L2-R1 | subtask_results 元素不得为空字符串 |
| L2-R2 | conflicts 若 present，则必须为非空向量，且元素不得为空字符串 |
| L2-R3 | worker_trace_refs 若 present，则必须为非空向量，且元素不得为空字符串 |
| L2-R4 | failure_summary 若 present，则不得为空字符串 |
| L2-R5 | agent_result / final_agent_response 等顶层结果替代别名继续由共享 boundary guard 阻断 |

#### Layer 3：字段规则（T017-B 新增）

| 规则 | 校验内容 | 来源 |
|---|---|---|
| 继承 L2 | 全部 L1 + L2 规则 | 仓内统一 guard 堆叠模式 |
| L3-R1 | subtask_results 元素必须包含至少一个非空白字符 | 聚合结果列表 hygiene |
| L3-R2 | subtask_results 元素必须唯一 | 防止同一子结果重复计入聚合面 |
| L3-R3 | merged_result 必须包含至少一个非空白字符 | 聚合产物不可塌缩为空壳字符串 |
| L3-R4 | recommended_next_action 必须包含至少一个非空白字符 | 主控建议不可塌缩为空壳字符串 |
| L3-R5 | conflicts 若 present，则元素必须包含至少一个非空白字符且唯一 | 冲突集合必须可程序化消费 |
| L3-R6 | worker_trace_refs 若 present，则元素必须包含至少一个非空白字符且唯一 | 轨迹引用必须可追溯且不可重复 |
| L3-R7 | failure_summary 若 present，则必须包含至少一个非空白字符 | 局部失败摘要不可塌缩为空白 |
| L3-R8 | merged_result 与 recommended_next_action 去首尾空白后不得等值 | 聚合产物与主控建议必须保持语义分层 |

### 5.3 字段解释

1. subtask_results 保持字符串列表形态，不在 T017 扩张为嵌套 WorkerResult 对象，因为那会跨到新的 contract object 设计。
2. conflicts 只保证结构有效、去重和可审计，不在 T017 解释冲突优先级、仲裁算法或权重评分。
3. worker_trace_refs 在 contracts 层只保证“像引用一样可消费”，不绑定具体 trace URI scheme 或日志存储格式。
4. failure_summary 继续保持协同阶段局部失败摘要，不上升为顶层 ErrorInfo，也不要求在无失败时必须存在。
5. recommended_next_action 保持字符串建议形态，不在 T017 扩张为新的 enum 或 runtime action object，以避免跨包扩张。

### 5.4 最小组合规则

| 规则编号 | 非法组合 | 判定原因 |
|---|---|---|
| C1 | merged_result 与 recommended_next_action 去首尾空白后等值 | 对象把“协同产物是什么”和“主控下一步该做什么”压塌为同一字符串槽位 |
| C2 | subtask_results present 但包含重复项 | 会把同一子任务结果重复计入协同聚合面，破坏结果可追溯性 |
| C3 | conflicts present 但包含重复或空白项 | 冲突名册不可程序化消费，也无法稳定支撑后续仲裁 |
| C4 | worker_trace_refs present 但包含重复或空白项 | 审计引用会失去唯一性与追溯价值 |

说明：

- T017 不新增 recommended_next_action 与具体 runtime 状态的组合规则，因为那会跨到 AgentOrchestrator/RecoveryManager 的运行时裁定。
- T017 不强制 conflicts 与 failure_summary 同时出现，因为局部失败与结果分歧并不是同一语义层。
- 这符合 consumer-driven / just-enough validation 原则：只验证当前消费者真实依赖、且能稳定自动化的字段规则。

---

## 6. Design->Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | contracts/include/agent/MultiAgentResultGuards.h（新增 validate_multi_agent_result_field_rules 及字段 hygiene 辅助校验） |
| 测试目标 | tests/contract/agent/MultiAgentResultFieldContractTest.cpp |
| 验收命令 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentResultFieldContractTest --output-on-failure && ctest --test-dir build-ci -L contract --output-on-failure |

---

## 7. D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 6 个字段已全覆盖；L1/L2/L3 分层闭合；新增规则限定在 field hygiene、列表唯一性与结果/建议分层，未越界到 ResultMerger 或 AgentOrchestrator 的运行时实现 |

**Gate 结论：PASS — 可进入 WP04-T017-B**

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T017 来源与三件套 | docs/todos/contracts/WP-04-边界对象TODO.md | WP04-T017 行 |
| MultiAgentResult 对象边界 | docs/todos/contracts/deliverables/WP04-T016-MultiAgentResult语义说明.md | §3/§4/§5/§6 |
| ADR-008 责任链与最小结果槽位 | docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | §3.2/§3.3/§4/§5.2 |
| T013 对 T016/T017 的影响结论 | docs/todos/contracts/deliverables/WP04-T013-ADR008对象影响清单.md | §3.2/§5 |
| 多 Agent 架构与主控关系 | docs/architecture/DASSALL_Agent_architecture.md | §4.8/§6.11 |
| multi_agent 模块工程落点 | docs/architecture/DASALL_Engineering_Blueprint.md | §3.10/§7.1 |
| contracts 分波次冻结原则 | docs/plans/DASALL_contracts冻结实施计划.md | §3.2/§6.3/§8 |
| 契约优先与阶段 Gate 纪律 | docs/plans/DASALL_工程落地实现步骤指引.md | 阶段 B / 阶段 L |
| 现有 MultiAgentResult 对象与 guards | contracts/include/agent/MultiAgentResult.h；contracts/include/agent/MultiAgentResultGuards.h | 对象定义与 T016 required/boundary guards |

### 8.2 外部业界参考

1. Anthropic, Building Effective Agents  
   结论：并行化与 orchestrator-workers 模式都强调结构化中间结果、透明聚合和 guardrails，避免坏输出直接级联到下游。  
   参考：https://www.anthropic.com/engineering/building-effective-agents

2. Microsoft Azure Architecture Center, AI agent orchestration patterns  
   结论：multi-agent orchestration 需要显式 conflict resolution strategy，并在中间结果传递前做结构化验证，避免矛盾结果或低质量结果继续传播。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/ai-ml/guide/ai-agent-design-patterns

3. Proto Best Practices  
   结论：契约字段应单一职责、兼容演进优先，repeated 字段不应随意改变 repeatedness，支持在既有对象面上做最小必要的字段校验。  
   参考：https://protobuf.dev/best-practices/dos-donts/