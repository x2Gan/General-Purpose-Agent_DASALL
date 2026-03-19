# WP04-T016-D：MultiAgentResult 语义说明

> 版本：1.0 | 日期：2026-03-19 | 状态：Done
> 任务编号：WP04-T016-D
> 上游输入：ADR-008 §3.2/§3.3/§4/§5.2、WP04-T013-D §3.2、WP03-T014 AgentResult 语义说明、docs/architecture/DASSALL_Agent_architecture.md §3.8/§6.11、docs/architecture/DASALL_Engineering_Blueprint.md §3.10/§7.1、docs/plans/DASALL_contracts冻结实施计划.md

---

## 1. 研究结论摘要

### 1.1 本地证据清单

| 编号 | 证据 | 对 T016 的直接约束 |
|---|---|---|
| L1 | ADR-008 §3.2 / §3.3 / §4 | AgentOrchestrator 拥有全局请求生命周期与最终输出裁定权；MultiAgentCoordinator 只负责协同子图编排与结果回传，因此 MultiAgentResult 只能表达协同结果 |
| L2 | ADR-008 §5.2 | MultiAgentResult 不直接等于 AgentResult，至少覆盖 `subtask_results`、`merged_result`、`conflicts`、`worker_trace_refs`、`failure_summary`、`recommended_next_action` |
| L3 | docs/todos/contracts-freeze/deliverables/WP04-T013-ADR008对象影响清单.md §3.2 | MultiAgentResult 是协同回传值，不是最终用户面结果；`agent_result` / `final_agent_response` 属于显式禁区 |
| L4 | contracts/include/boundary/MultiAgentBoundaryGuards.h | 共享边界守卫已经冻结 MultiAgentResult 的两类顶层越权别名：`agent_result`、`final_agent_response` |
| L5 | contracts/include/agent/AgentResult.h | AgentResult 明确声明 `subtask_results`、`merged_result`、`conflicts` 等多 Agent 细节不属于顶层统一输出对象，可作为 T016 的反向边界证据 |
| L6 | docs/architecture/DASSALL_Agent_architecture.md §3.8 / §6.11 | 多 Agent 协同属于主循环中的受控阶段；主控层拥有最终 AgentResult，协同层只回传局部结构化结果 |
| L7 | docs/architecture/DASALL_Engineering_Blueprint.md §3.10 / §7.1 | MultiAgentCoordinator 位于 multi_agent 模块，但 contracts 对象与 tests/contract/agent 的验证入口应位于 contracts/agent 与 tests/contract/agent |
| L8 | docs/plans/DASALL_contracts冻结实施计划.md §2 / §7 / §8 | contracts 必须先冻结职责边界，再落对象骨架与 contract tests；T016 只做对象与 required/boundary guards，字段细则留给 T017 |
| L9 | docs/todos/contracts-freeze/WP-04-边界对象TODO.md | T016-B 的代码目标是 `MultiAgentResult.h` / `MultiAgentResultGuards.h` + contract test，完成判定是“终态语义越权被阻断” |

### 1.2 外部参考清单

| 编号 | 参考 | 与 T016 的映射 |
|---|---|---|
| E1 | Anthropic: Building Effective Agents | orchestrator-workers 模式下由 central orchestrator 负责综合 worker 输出，worker 或协同子域结果不应直接替代顶层最终结果 |
| E2 | Microsoft Azure Architecture Center: AI agent orchestration patterns | concurrent / group chat / handoff 等多 Agent 模式都需要显式结果聚合与冲突处理，建议把中间结果保持为结构化 aggregation object，而不是直接暴露 final response |
| E3 | Proto Best Practices | 契约演进应保持对象边界稳定、字段职责单一，支持把 MultiAgentResult 与 AgentResult 分离为不同消息类型而非复用一个顶层结果对象 |

### 1.3 对本任务的可落地启发

1. MultiAgentResult 必须是 AgentOrchestrator 可消费的协同汇总对象，只表达子任务结果、合并结果、冲突、轨迹引用、失败摘要和下一步建议。
2. T016 只冻结对象职责边界、对象骨架和 required/boundary guards；字段 hygiene、去重、组合规则留给 T017。
3. MultiAgentResult 需要通过两层手段阻断顶层终态越权：对象骨架不暴露 AgentResult 顶层字段，边界守卫复用 `MultiAgentBoundaryGuards.h` 拦截 `agent_result` / `final_agent_response`。
4. `recommended_next_action` 只表达对 AgentOrchestrator 的建议，不表达已经提交给用户的最终结果，更不能替代 AgentResult 的 `result_code`、`response_text`、`task_completed` 和 `error_info`。
5. contract test 应优先证明“MultiAgentResult 不是第二个 AgentResult”，而不是提前完成 T017 的字段表细则。

---

## 2. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 MultiAgentResult 的对象定位 | ADR-008 §3.2/§3.3/§4/§5.2、WP04-T013-D §3.2 | 本文件 §3 | 明确其为 collaboration-result object，不是最终 AgentResult | 若出现用户面最终输出语义，回退为“协同回传值” |
| D2 | 冻结最小职责槽位 | ADR-008 §5.2、WP04-T013-D §3.2 | 本文件 §4 | 3 个必填 + 3 个可选槽位明确，覆盖 ADR-008 最小结果面 | 若出现顶层 result_code / response_text 等字段，按边界回退 |
| D3 | 冻结顶层禁区 | AgentResult.h、MultiAgentBoundaryGuards.h、WP04 TODO | 本文件 §5 | 明确不承担 AgentResult 终态语义，也不承载全局 request lifecycle 控制权 | 若对象膨胀为 AgentResult 镜像，则直接阻断 |
| D4 | 输出 Design->Build 三件套 | WP04 TODO、既有 agent contract 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整闭合 | 若需要新增运行时调度算法或字段表规则，判定越界并回退 |
| D5 | 形成 D Gate 结果 | D1-D4 | 本文件 §7 | PASS/Blocked 二值明确 | 若职责边界或三件套未闭合，则 Blocked |

---

## 3. 对象定位与责任链

### 3.1 核心职责

MultiAgentResult 是 MultiAgentCoordinator 回传给 AgentOrchestrator 的协同结果对象。

它只负责承载三类信息：

1. 子任务级执行产出与合并产物。
2. 协同过程中的冲突、轨迹引用和局部失败摘要。
3. 给 AgentOrchestrator 的下一步建议。

它不是：

1. 第二个 AgentResult，不负责提交最终 `result_code`、`response_text`、`task_completed`、`error_info`。
2. 顶层请求生命周期状态对象，不负责主状态机、总预算、顶层 checkpoint 或用户面终态发布。
3. WorkerTask / WorkerLease 图本身，不负责承载任务派发或租约控制细节。

### 3.2 调用链位置

```text
AgentRequest
  -> AgentOrchestrator decides multi-agent mode
  -> MultiAgentRequest
  -> MultiAgentCoordinator builds WorkerTask graph
  -> MultiAgentResult
  -> AgentOrchestrator folds collaboration result into final AgentResult
```

### 3.3 生产者与消费者

| 角色 | 与 MultiAgentResult 的关系 |
|---|---|
| MultiAgentCoordinator / ResultMerger | 唯一生产者；负责汇总 worker 子任务结果并给出协同层建议 |
| AgentOrchestrator | 直接消费者；决定继续推理、replan、降级、升级人工或折叠为最终 AgentResult |
| Contract tests | 验证对象未回退为 AgentResult 镜像，并验证 ADR-008 顶层禁区仍被阻断 |

---

## 4. 最小职责槽位

MultiAgentResult 在 T016 冻结为 3 个必填槽位 + 3 个可选槽位。

### 4.1 必填槽位（3 项）

| 字段 | 类型 | 语义 | 冻结原因 |
|---|---|---|---|
| `subtask_results` | `std::vector<std::string>` | 子任务结果引用或结构化摘要集合 | ADR-008 §5.2 明示协同结果必须包含子任务结果集合；没有它就无法证明是聚合结果 |
| `merged_result` | `std::string` | 对子任务结果合并后的协同产出 | MultiAgentResult 的核心职责就是提供合并结果，而不是只回传松散子结果 |
| `recommended_next_action` | `std::string` | 给 AgentOrchestrator 的下一步建议 | 协同层只能给出建议，不能直接提交最终结果；该字段固化“建议而非终裁” |

### 4.2 可选槽位（3 项）

| 字段 | 类型 | 语义 | 说明 |
|---|---|---|---|
| `conflicts` | `std::vector<std::string>` | 冲突、分歧或未决项集合 | 只有出现冲突时才需要返回 |
| `worker_trace_refs` | `std::vector<std::string>` | worker 轨迹或审计引用 | 用于上游溯源，不承担最终审计总入口职责 |
| `failure_summary` | `std::string` | 局部失败摘要 | 只描述协同阶段失败摘要，不替代顶层 `ErrorInfo` |

说明：ADR-008 在影响清单中还列出了 `failure_summary`；T016 把它保留在对象面，字段级 hygiene 与组合规则留给 T017。

---

## 5. 顶层禁区与边界声明

### 5.1 不得替代顶层 AgentResult

| 禁止字段 | 禁止原因 |
|---|---|
| `agent_result` / `final_agent_response` | 会把协同结果对象退化为最终结果包装器 |
| `result_code` | 顶层终态码由 AgentOrchestrator 提交 |
| `response_text` | 用户面文本回复属于最终 AgentResult |
| `task_completed` | 是否完成整个请求属于全局主控裁定 |
| `error_info` | 顶层标准化失败对象属于 AgentResult，而不是协同结果 |

### 5.2 不得承载全局主控语义

| 禁止字段 | 禁止原因 |
|---|---|
| `session_id` / `checkpoint_ref` | 顶层会话与 checkpoint 入口由 AgentOrchestrator 持有 |
| `global_fsm_state` / `runtime_state` | MultiAgentResult 不是主状态机容器 |
| `runtime_budget_consumed` | 总预算归 runtime / AgentOrchestrator 管控 |

### 5.3 不得回退为 Worker 执行对象

| 禁止字段 | 禁止原因 |
|---|---|
| `lease_id` / `worker_type` / `allowed_tools` | 这些语义属于 WorkerTask / WorkerLease，而不是协同汇总结果 |

---

## 6. Design->Build 映射

| 设计结论 | Build 落地点 | 说明 |
|---|---|---|
| 冻结 MultiAgentResult 对象骨架 | `contracts/include/agent/MultiAgentResult.h` | 只定义职责边界对应的最小对象，不进入 T017 字段细则 |
| 冻结 required/boundary guards 并复用 ADR-008 禁区守卫 | `contracts/include/agent/MultiAgentResultGuards.h` | required guard 验证最小槽位；boundary guard 复用 `MultiAgentBoundaryGuards.h` 阻断顶层结果替代别名 |
| 新增 T016 contract gate | `tests/contract/agent/MultiAgentResultContractTest.cpp` | 至少覆盖 1 个正例 + 1 个负例，并补充 compile-time 结构禁区断言 |
| 接入合同测试注册 | `tests/contract/CMakeLists.txt` | 新增 MultiAgentResultContractTest |

代码目标：`contracts/include/agent/MultiAgentResult.h`、`contracts/include/agent/MultiAgentResultGuards.h`

测试目标：`tests/contract/agent/MultiAgentResultContractTest.cpp`

验收命令：`cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentResultContractTest --output-on-failure && ctest --test-dir build-ci -L contract --output-on-failure`

---

## 7. D Gate 结果

| D 原子项 | 状态 | 说明 |
|---|---|---|
| D1：对象定位 | ✅ Done | 已明确其为协同回传结果，而不是最终 AgentResult |
| D2：最小槽位 | ✅ Done | 3 必填 + 3 可选槽位冻结，并覆盖 ADR-008 最小结果面 |
| D3：顶层禁区 | ✅ Done | AgentResult 终态语义、全局主控语义和 Worker 执行语义均已显式阻断 |
| D4：Design->Build 三件套 | ✅ Done | 代码目标、测试目标、验收命令已锁定 |
| D5：进入 B 判定 | ✅ Done | 无阻塞项 |

**Gate 结论：PASS — 可进入 WP04-T016-B**

进入 B 的条件：

1. ✅ MultiAgentResult 已限定为 collaboration-result object。
2. ✅ 已明确复用 `MultiAgentBoundaryGuards.h`，避免重复实现顶层结果替代禁区。
3. ✅ 已明确 AgentResult 顶层终态字段不得下沉到协同结果对象。
4. ✅ 代码、测试、验收命令三件套已锁定。

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| MultiAgentCoordinator 受主控约束 | docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | §3.2 / §3.3 / §4 |
| MultiAgentResult 最小结果槽位 | docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | §5.2 |
| T013 对 T016 的对象影响结论 | docs/todos/contracts-freeze/deliverables/WP04-T013-ADR008对象影响清单.md | §3.2 |
| 现有协同结果禁区守卫 | contracts/include/boundary/MultiAgentBoundaryGuards.h | result boundary guards |
| AgentResult 顶层结果基线与反向禁区 | contracts/include/agent/AgentResult.h | `What AgentResult is NOT` 与 forbidden fields |
| contracts 分波次冻结原则 | docs/plans/DASALL_contracts冻结实施计划.md | §2 / §7 / §8 |
| 多 Agent 架构与主控关系 | docs/architecture/DASSALL_Agent_architecture.md | §3.8 / §6.11 |
| multi_agent 模块工程落点 | docs/architecture/DASALL_Engineering_Blueprint.md | §3.10 / §7.1 |
| T016 来源与完成判定 | docs/todos/contracts-freeze/WP-04-边界对象TODO.md | WP04-T016 行 |

### 8.2 外部业界参考

1. Anthropic, Building Effective Agents  
   结论：orchestrator-workers 模式要求由 central orchestrator 负责综合 worker 输出并控制流程，局部协同结果不应直接替代顶层最终结果。  
   参考：https://www.anthropic.com/engineering/building-effective-agents

2. Microsoft Azure Architecture Center, AI agent orchestration patterns  
   结论：multi-agent orchestration 需要显式的结果聚合、冲突处理和结构化中间结果，最终输出仍由上层 orchestrator 统一裁定。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/ai-ml/guide/ai-agent-design-patterns

3. Proto Best Practices  
   结论：应使用不同消息类型承载不同生命周期职责，避免把长期演进的聚合结果对象与顶层 API/存储结果对象混用。  
   参考：https://protobuf.dev/best-practices/dos-donts/