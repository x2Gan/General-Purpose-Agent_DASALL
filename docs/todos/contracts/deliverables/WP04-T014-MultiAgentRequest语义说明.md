# WP04-T014-D：MultiAgentRequest 语义说明

> 版本：1.0 | 日期：2026-03-19 | 状态：Done
> 任务编号：WP04-T014-D
> 上游输入：ADR-008 §3.2/§3.3/§4/§5.1、WP04-T013-D §3.1、WP02-T007 RuntimeBudget 冻结基线、WP02-T009 标识传播规则、WP03-T002 AgentRequest 语义说明、WP03-T018-M3 冻结包

---

## 1. 研究结论摘要

### 1.1 本地证据清单

| 编号 | 证据 | 对 T014 的直接约束 |
|---|---|---|
| L1 | ADR-008 §3.2 / §3.3 / §4 | AgentOrchestrator 拥有全局请求生命周期控制权；MultiAgentCoordinator 只消费局部协同输入并回传协同结果，因此 MultiAgentRequest 只能表达子域请求 |
| L2 | ADR-008 §5.1 | MultiAgentRequest 不复用 AgentRequest，至少包含 parent_request_id、parent_task_id、goal_fragment、plan_fragment、collaboration_mode、worker_budget_guard、permission_guard、stop_conditions |
| L3 | docs/todos/contracts/deliverables/WP04-T013-ADR008对象影响清单.md §3.1 | MultiAgentRequest 是协同入口，不是第二个 AgentRequest；只消费已裁定的目标片段、计划片段和约束条件 |
| L4 | docs/plans/DASALL_contracts冻结实施计划.md §2 / §7 / §8 | contracts 必须先冻结语义边界，再落对象骨架与 contract tests；协同链路固定为 Goal Fragment + Plan Fragment -> MultiAgentRequest -> WorkerTask -> MultiAgentResult |
| L5 | docs/architecture/DASSALL_Agent_architecture.md §3.8 / §6.11 | 多 Agent 协同属于主循环中的受控阶段；主控层拥有总预算和最终结果，协同请求只携带子域执行所需最小输入 |
| L6 | docs/architecture/DASALL_Engineering_Blueprint.md §3.10 / §7.1 | MultiAgentCoordinator 位于 multi_agent 模块，但调度指令由 runtime 发出；对象契约应放在 contracts/agent 并供 tests/contract/agent 直接验证 |
| L7 | contracts/include/boundary/MultiAgentBoundaryGuards.h | 已有边界守卫明确阻断 `agent_request` / `agent_request_payload` 两类 AgentRequest 复用别名，可复用为 T014 顶层禁区守卫 |
| L8 | contracts/include/agent/AgentRequest.h / AgentRequestGuards.h | AgentRequest 是单 Agent 统一入口对象，包含 user_input、request_channel、domain_context、runtime_budget 等顶层入口语义；这些语义不能下沉到协同子域请求 |
| L9 | docs/todos/contracts/WP-04-边界对象TODO.md | T014-B 代码目标是 MultiAgentRequest.h/Guards + contract test，完成判定是“复用 AgentRequest 的越界场景被阻断” |

### 1.2 外部参考清单

| 编号 | 参考 | 与 T014 的映射 |
|---|---|---|
| E1 | Anthropic: Building Effective Agents | orchestrator-workers 模式要求由 central orchestrator 动态拆解任务并委派 worker；worker 输入应是局部子任务上下文，而不是顶层用户请求对象 |
| E2 | Microsoft Azure Architecture Center: AI agent orchestration patterns | 多 Agent orchestration 会引入额外协调开销、状态管理和结果聚合责任，因此每个子代理输入应收敛为最小必要上下文，并在交接前验证结构化输出 |

### 1.3 对本任务的可落地启发

1. MultiAgentRequest 必须是 orchestrator 发给协同子域的最小输入对象，只描述“已经裁定好的局部目标、计划和约束”，不能重新承载顶层 user_input 或总预算。
2. T014 只冻结对象职责边界、对象骨架和 required/boundary guards；字段 hygiene、组合规则和可选槽位细则留给 T015。
3. worker_budget_guard 应复用既有 RuntimeBudget，避免为协同预算重新发明第二套预算对象。
4. 复用 AgentRequest 的越界场景需要同时从两层阻断：一层通过对象骨架不暴露顶层入口字段，另一层通过复用 MultiAgentBoundaryGuards 阻断 `agent_request` 类别名。
5. contract test 应优先验证“对象不是第二个 AgentRequest”，而不是提前验证协同字段的完整 field table。

---

## 2. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 MultiAgentRequest 的对象定位 | ADR-008 §3.2/§3.3/§4/§5.1、WP04-T013-D §3.1 | 本文件 §3 | 明确其为 collaboration-subdomain request，不是第二个 AgentRequest | 若出现 user-facing 或 final-output 语义，回退为“主控裁定后的局部协同输入” |
| D2 | 冻结最小职责槽位 | ADR-008 §5.1、WP02-T007、WP02-T009 | 本文件 §4 | 5 个必填 + 3 个可选槽位明确，且复用 RuntimeBudget | 若新增顶层 Session/Checkpoint/FinalResult 语义，则回退到 ADR-008 最小槽位 |
| D3 | 冻结顶层禁区 | ADR-008 §5.1、WP04 TODO、AgentRequest 基线 | 本文件 §5 | 明确不复用 AgentRequest、用户入口语义和全局预算/状态机字段 | 若对象膨胀为总请求镜像，则按 ADR-008 直接阻断 |
| D4 | 输出 Design->Build 三件套 | WP04 TODO、现有 agent contract 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整闭合 | 若需要字段级规则或新对象族才能落地，则判定越界并回退 |
| D5 | 形成 D Gate 结果 | D1-D4 | 本文件 §7 | PASS/Blocked 二值明确 | 若职责边界或三件套未闭合，则 Blocked |

---

## 3. 对象定位与责任链

### 3.1 核心职责

MultiAgentRequest 是 AgentOrchestrator 发给 MultiAgentCoordinator 的协同子域输入对象。

它只负责承载三类信息：

1. 顶层请求在进入协同模式后的父级引用。
2. 已裁定的局部目标与计划片段。
3. 约束当前协同阶段的模式、预算、权限和退出条件。

它不是：

1. 第二个 AgentRequest，不负责重复表达 `user_input`、`request_channel`、`domain_context` 等入口语义。
2. 顶层 Runtime 状态对象，不负责主状态机、总 checkpoint、总预算或最终结果提交。
3. WorkerTask 图本身，不负责描述租约分配、具体 worker 选择或执行结果。

### 3.2 调用链位置

```text
AgentRequest
  -> AgentOrchestrator decides multi-agent mode
  -> MultiAgentRequest
  -> MultiAgentCoordinator creates WorkerTask graph
  -> MultiAgentResult
  -> AgentOrchestrator folds result into final AgentResult
```

### 3.3 生产者与消费者

| 角色 | 与 MultiAgentRequest 的关系 |
|---|---|
| AgentOrchestrator 上游装配逻辑 | 唯一生产者；在进入协同子域前装配对象 |
| MultiAgentCoordinator | 直接消费者；基于该对象生成 WorkerTask 图和协同策略执行 |
| Contract tests | 验证对象未回退为 AgentRequest 镜像，并验证 ADR-008 顶层禁区仍被阻断 |

---

## 4. 最小职责槽位

MultiAgentRequest 在 T014 冻结为 5 个必填槽位 + 3 个可选槽位。

### 4.1 必填槽位（5 项）

| 字段 | 类型 | 语义 | 冻结原因 |
|---|---|---|---|
| `parent_request_id` | `string` | 关联顶层 AgentRequest | ADR-008 §5.1 明示协同请求必须保留顶层请求锚点 |
| `parent_task_id` | `string` | 关联顶层任务图或协同根任务 | 协同阶段必须位于主控任务图之下，而非自成新入口 |
| `goal_fragment` | `string` | 协同子域要完成的局部目标 | 协同请求只消费被裁定后的局部目标，不承载完整用户输入 |
| `plan_fragment` | `string` | 已裁定的局部计划片段 | MultiAgentCoordinator 应消费计划片段而不是重做主控计划 |
| `collaboration_mode` | `enum` | 指示当前协同模式 | ADR-008 明示协同请求包含 collaboration_mode，且模式必须由主控显式裁定 |

### 4.2 可选槽位（3 项）

| 字段 | 类型 | 语义 | 说明 |
|---|---|---|---|
| `worker_budget_guard` | `RuntimeBudget` | 当前协同阶段可消耗的 worker 级预算上界 | 复用 WP02-T007 预算对象，不扩展第二预算 schema |
| `permission_guard` | `string` | 当前协同阶段的权限边界摘要 | 只表达约束声明，不携带具体工具执行结果 |
| `stop_conditions` | `vector<string>` | 当前协同阶段的退出条件集合 | 只表达退出条件名册，不承担恢复或结果语义 |

---

## 5. 顶层禁区与边界声明

### 5.1 不得复用 AgentRequest 入口语义

| 禁止字段 | 禁止原因 |
|---|---|
| `agent_request` / `agent_request_payload` | 会把协同子域请求退化为顶层请求包装器 |
| `user_input` | 顶层用户原始输入属于 AgentRequest |
| `request_channel` | 入口来源属于主控入口而非协同子域 |
| `domain_context` / `constraint_set` | 顶层上下文摘要与总约束不应原样下沉为协同请求 |
| `approval_policy_hint` | 审批提示属于顶层入口策略，不属于局部协同请求 |

### 5.2 不得承载全局主控语义

| 禁止字段 | 禁止原因 |
|---|---|
| `session_id` / `checkpoint_ref` | 顶层会话与恢复锚点由 AgentOrchestrator 持有 |
| `final_agent_response` / `agent_result` | 最终输出权只属于 AgentOrchestrator |
| `global_fsm_state` / `runtime_state` | 协同请求不是主状态机容器 |

### 5.3 不得越级承担 WorkerTask / WorkerLease 语义

| 禁止字段 | 禁止原因 |
|---|---|
| `worker_type` / `allowed_tools` | 这是 WorkerTask 的执行单元语义 |
| `lease_id` / `deadline_at` | 这是 WorkerLease 或 WorkerTask 层的执行租约语义 |

---

## 6. Design->Build 映射

| 设计结论 | Build 落地点 | 说明 |
|---|---|---|
| 冻结 MultiAgentRequest 对象骨架 | `contracts/include/agent/MultiAgentRequest.h` | 只定义职责边界对应的最小对象，不进入 T015 字段细则 |
| 冻结 required/boundary guards 并复用 ADR-008 禁区守卫 | `contracts/include/agent/MultiAgentRequestGuards.h` | required guard 验证对象最小槽位；boundary guard 复用 `MultiAgentBoundaryGuards.h` 阻断 AgentRequest 复用别名 |
| 新增 T014 contract gate | `tests/contract/agent/MultiAgentRequestContractTest.cpp` | 至少覆盖 1 个正例 + 1 个负例，并补充 compile-time 结构禁区断言 |
| 接入合同测试注册 | `tests/contract/CMakeLists.txt` | 新增 MultiAgentRequestContractTest |

代码目标：`contracts/include/agent/MultiAgentRequest.h`、`contracts/include/agent/MultiAgentRequestGuards.h`

测试目标：`tests/contract/agent/MultiAgentRequestContractTest.cpp`

验收命令：`cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentRequestContractTest --output-on-failure && ctest --test-dir build-ci -L contract --output-on-failure`

---

## 7. D Gate 结果

| D 原子项 | 状态 | 说明 |
|---|---|---|
| D1：对象定位 | ✅ Done | 已明确其为协同子域输入，而不是第二个 AgentRequest |
| D2：最小槽位 | ✅ Done | 5 必填 + 3 可选槽位冻结，并复用 RuntimeBudget |
| D3：顶层禁区 | ✅ Done | AgentRequest 入口语义、全局主控语义、Worker 执行语义均已显式阻断 |
| D4：Design->Build 三件套 | ✅ Done | 代码目标、测试目标、验收命令已锁定 |
| D5：进入 B 判定 | ✅ Done | 无阻塞项 |

**Gate 结论：PASS — 可进入 WP04-T014-B**

进入 B 的条件：

1. ✅ MultiAgentRequest 已限定为 collaboration-subdomain request。
2. ✅ 已明确复用 RuntimeBudget 与 MultiAgentBoundaryGuards，避免跨包发明新基础对象。
3. ✅ 已明确复用 AgentRequest 的越界场景需要由对象骨架 + boundary guard 双重阻断。
4. ✅ 代码、测试、验收命令三件套已锁定。

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| MultiAgentCoordinator 受主控约束 | docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | §3.2 / §3.3 / §4 |
| MultiAgentRequest 最小输入槽位 | docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md | §5.1 |
| T013 对 T014 的对象影响结论 | docs/todos/contracts/deliverables/WP04-T013-ADR008对象影响清单.md | §3.1 |
| contracts 分波次冻结原则 | docs/plans/DASALL_contracts冻结实施计划.md | §2 / §7 / §8 |
| Multi-agent 架构与主控关系 | docs/architecture/DASSALL_Agent_architecture.md | §3.8 / §6.11 |
| multi_agent 模块工程落点 | docs/architecture/DASALL_Engineering_Blueprint.md | §3.10 / §7.1 |
| 现有协同边界守卫 | contracts/include/boundary/MultiAgentBoundaryGuards.h | request boundary guards |
| AgentRequest 顶层入口基线 | contracts/include/agent/AgentRequest.h；contracts/include/agent/AgentRequestGuards.h | 对象定义与 required/boundary guards |
| T014 来源与完成判定 | docs/todos/contracts/WP-04-边界对象TODO.md | WP04-T014 行 |

### 8.2 外部业界参考

1. Anthropic, Building Effective Agents  
   结论：orchestrator-workers 模式中，central orchestrator 动态拆解任务并把局部子任务委派给 worker，因此 worker 输入应是局部子任务上下文，而不是顶层用户请求镜像。  
   参考：https://www.anthropic.com/engineering/building-effective-agents

2. Microsoft Azure Architecture Center, AI agent orchestration patterns  
   结论：multi-agent orchestration 需要显式协调状态、上下文与结果聚合，并建议在代理交接前使用结构化输入输出与验证来降低级联错误。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/ai-ml/guide/ai-agent-design-patterns