# WP04-T018-D：WorkerTask 语义说明

> 版本：1.0 | 日期：2026-03-19 | 状态：Done
> 任务编号：WP04-T018-D
> 上游输入：WP04-T013-D/B、WP04-T014-D/B、WP04-T016-D/B、WP04-T017-D/B、ADR-008 §3.2/§3.3/§4/§5.3/§7、docs/architecture/DASSALL_Agent_architecture.md §4.8/§5.11/§6.11/§6.12、docs/architecture/DASALL_Engineering_Blueprint.md §3.1/§3.10/§7.1、docs/plans/DASALL_contracts冻结实施计划.md §2/§7/§8、docs/plans/DASALL_工程落地实现步骤指引.md 阶段 B / 阶段 L

---

## 1. 任务识别

### 1.1 范围

- 定义 WorkerTask 作为 multi-agent 子域中的子任务执行单元对象。
- 冻结 WorkerTask 的最小对象骨架、职责边界与 required/boundary guard 目标。
- 明确 WorkerTask 与 MultiAgentRequest、MultiAgentResult、WorkerLease、顶层 AgentOrchestrator 状态的分层关系。
- 将 T018 的设计结果映射到 `contracts/include/task/WorkerTask.h`、`contracts/include/task/WorkerTaskGuards.h` 和 `tests/contract/task/WorkerTaskContractTest.cpp`。

### 1.2 排除项

- 不进入 T019 字段表级规则，不在本任务冻结列表唯一性、空白字符 hygiene 或 worker_type 枚举全集。
- 不实现 WorkerLease、续约、释放原因或顶层 checkpoint 子域快照；这些属于 T020/T021。
- 不扩张到 AgentRegistry 匹配算法、ResultMerger 合并算法或 runtime 调度策略实现。
- 不让 WorkerTask 承担全局 Session/FSM、最终结果提交或恢复入口职责。

---

## 2. Phase 0：研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T018 的直接约束 |
|---|---|---|
| L1 | ADR-008 §5.3 / §8 | WorkerTask 只描述子任务执行单元；必须与全局主控、顶层 checkpoint/resume 分层 |
| L2 | WP04-T013-D §3.3 | WorkerTask 最小执行槽位锁定为 `task_id`、`parent_task_id`、`lease_id`、`worker_type`、`allowed_tools`、`timeout`、`idempotency_key` |
| L3 | `boundary/MultiAgentBoundaryGuards.h` | 已冻结 WorkerTask 顶层禁区：`global_session_state`、`global_fsm_state`、`session_fsm_state` |
| L4 | `agent/MultiAgentBoundaryContracts.h` | WorkerTask 在 WP04-T018/T019 作为 runtime-controlled multi-agent object 落地，upstream anchor 为 AgentOrchestrator task graph |
| L5 | WP01-T011 §4.3 / §6 | WorkerTask 仅承载执行单元约束（task/lease/timeout/tools/idempotency），不得承载全局 Session/FSM |
| L6 | WP04-T014-D §5.3 | `worker_type`、`allowed_tools`、`lease_id` / `deadline_at` 不属于 MultiAgentRequest，说明 WorkerTask 必须吸收执行单元语义 |
| L7 | WP04-T016-D §5.3 | `lease_id` / `worker_type` / `allowed_tools` 不属于 MultiAgentResult，说明 WorkerTask 不能被结果对象替代 |
| L8 | `contracts/include/task/WorkerTaskTag.h` | 仓库已将 WorkerTask 登记为稳定 contracts 对象，但尚未落实体对象与 guards |
| L9 | `tests/contract/CMakeLists.txt` + 当前仓库状态 | 尚无 `tests/contract/task/` 下的 WorkerTask contract test，T018-B 需要新建 task 测试分组 |
| L10 | `contracts/include/agent/MultiAgentRequest.h` / `MultiAgentResult.h` | 协同请求/结果已显式把 WorkerTask 语义排除出对象边界，T018 需补上缺失的执行单元对象 |

### 2.2 外部参考清单

| # | 来源 | 对 T018 的映射 |
|---|---|---|
| E1 | Anthropic, Building Effective Agents | orchestrator-workers 模式强调中央 orchestrator 负责任务拆解和结果综合，worker 处理受控子任务，适合将 WorkerTask 设计为清晰的执行单元契约 |
| E2 | Microsoft Azure Architecture Center, AI agent orchestration patterns | multi-agent orchestration 要求 orchestrator 负责 work distribution、result aggregation、timeout/retry/checkpoint 等可靠性控制；中间 agent 输出必须先验证再下传 |
| E3 | Proto Best Practices | 契约应保持单一职责、兼容演进优先、避免对象膨胀，支持在 T018 只冻结最小执行单元骨架而不提前扩张到 T019/T020 |

### 2.3 对本任务的可落地启发

1. WorkerTask 应保持“执行单元对象”而不是“局部小型 orchestrator”；它描述谁来做什么、在什么租约和超时内做，而不是全局如何收敛任务。
2. 对象层要优先锁定运行时真实会消费的最小槽位：子任务标识、租约绑定、worker 能力、允许工具、局部超时、幂等键。
3. T018-B 应先实现 required/boundary guards，阻断缺失锚点、空工具列表、非法超时和全局状态越权；更细的 whitespace/去重规则留给 T019。
4. 为了兼容后续租约对象拆分，WorkerTask 当前只持有 `lease_id` 引用，不内嵌租约元数据或续约状态。
5. contract test 应同时证明两件事：正向对象可构造并通过守卫，反向不能携带全局 Session/FSM 或结果层字段。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 WorkerTask 的对象定位与直接消费者 | ADR-008 §5.3、WP04-T013-D | 本文件 §4.1 | 明确其为 execution unit，消费者是 MultiAgentCoordinator / runtime / tools | 若出现第二主控或最终输出职责，回退到 ADR-008 的 execution-unit 定位 |
| D2 | 冻结 WorkerTask 最小槽位与 required/optional 归属 | WP04 TODO、WP04-T013-D、WP01-T011 | 本文件 §4.2 | 最小字段闭合并与 T019 字段表留出边界 | 若字段超过执行单元所需最小集合，回退为最小槽位 |
| D3 | 明确 WorkerTask 禁区与分层关系 | ADR-008 §4/§5.3、`MultiAgentBoundaryGuards.h`、T014/T016 交付物 | 本文件 §5 | 顶层状态、结果对象、租约元数据越权全部显式阻断 | 若出现 WorkerLease 或 AgentResult 语义混入，回退到局部执行边界 |
| D4 | 输出 T018 的 Design->Build 三件套 | WP04 TODO、现有 contract test 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整闭合 | 若需要跨到 T019/T020 文件族，判定越界并回退 |
| D5 | 形成 D Gate 结论 | D1-D4 | 本文件 §7 | PASS/Blocked 二值明确 | 若对象边界或三件套未闭合，则 Blocked |

---

## 4. WorkerTask 语义定义

### 4.1 对象定位

WorkerTask 是由 MultiAgentCoordinator 在协同子域内创建、派发和跟踪的子任务执行单元对象。

它表达的是：

1. 当前执行单元在任务图中的身份。
2. 当前执行单元绑定的租约锚点。
3. 当前执行单元要分配给哪类 worker。
4. 当前执行单元被允许调用的工具边界。
5. 当前执行单元的局部超时约束。
6. 当前执行单元在需要重放或补偿时使用的幂等锚点。

它不表达的是：

1. 顶层 Session 生命周期。
2. 全局 FSM 推进状态。
3. 顶层 checkpoint / resume 入口。
4. 协同结果汇总或最终 AgentResult。
5. WorkerLease 的续约、释放和到期元数据。

### 4.2 最小对象骨架

WorkerTask 固定由 6 个必填字段 + 1 个可选字段组成。

#### 必填字段（6 项）

| 字段 | 类型 | 语义 |
|---|---|---|
| `task_id` | `std::optional<std::string>` | 当前 worker task 的唯一执行单元标识 |
| `parent_task_id` | `std::optional<std::string>` | 指向 orchestrator 或上层协同图中的父任务锚点 |
| `lease_id` | `std::optional<std::string>` | 当前执行单元关联的租约引用 |
| `worker_type` | `std::optional<std::string>` | worker 能力类别或执行角色 |
| `allowed_tools` | `std::optional<std::vector<std::string>>` | 当前执行单元允许调用的工具名册 |
| `timeout_ms` | `std::optional<std::uint32_t>` | 当前执行单元的局部执行超时，单位毫秒 |

#### 可选字段（1 项）

| 字段 | 类型 | 语义 |
|---|---|---|
| `idempotency_key` | `std::optional<std::string>` | 当前执行单元的重放/补偿锚点；无副作用场景可缺省 |

### 4.3 直接消费者与关系

| 消费方 | 关系 |
|---|---|
| `MultiAgentCoordinator` | 直接生产与调度 WorkerTask |
| `AgentRegistry` / worker selection path | 消费 `worker_type` 和 `allowed_tools` 进行执行路由 |
| runtime / audit path | 通过 `task_id`、`parent_task_id`、`lease_id` 关联局部执行轨迹 |
| `WorkerLease`（后续 T020/T021） | 通过 `lease_id` 建立租约元数据关联 |

---

## 5. 边界与禁区

### 5.1 不得承载全局主控语义

| 禁止字段 | 禁止原因 |
|---|---|
| `global_session_state` / `global_fsm_state` / `session_fsm_state` | 这些属于 AgentOrchestrator 顶层状态，`MultiAgentBoundaryGuards` 已冻结为禁区 |
| `session_id` / `runtime_state` / `checkpoint_ref` | 顶层会话、主状态机和恢复入口不属于执行单元对象 |

### 5.2 不得承载结果对象语义

| 禁止字段 | 禁止原因 |
|---|---|
| `agent_result` / `final_agent_response` | 最终输出权只属于 AgentOrchestrator |
| `merged_result` / `conflicts` / `recommended_next_action` | 这些属于 MultiAgentResult 协同汇总对象 |

### 5.3 不得承载租约对象细节

| 禁止字段 | 禁止原因 |
|---|---|
| `renewal_deadline` / `release_reason` / `lease_state` | 这些属于 WorkerLease，不属于 WorkerTask |
| `deadline_at` 全量租约快照 | 顶层租约到期与续约控制在 T020/T021 处理；T018 仅保留局部 `lease_id` 引用 |

### 5.4 Required / Boundary 守卫目标

#### Required 层（T018-B）

1. `task_id`、`parent_task_id`、`lease_id`、`worker_type` 必须 present 且非空。
2. `allowed_tools` 必须 present 且至少包含一个元素。
3. `timeout_ms` 必须 present 且大于 0。

#### Boundary 层（T018-B）

1. `task_id` 不得与 `parent_task_id` 相等，防止父子任务锚点塌缩。
2. `allowed_tools` 元素不得为空字符串。
3. `idempotency_key` 若 present，必须为非空字符串。
4. `evaluate_worker_task_field_boundary()` 继续用于阻断全局 Session/FSM 字段复用。

说明：

- 空白字符 hygiene、`allowed_tools` 唯一性、`worker_type`/`idempotency_key` 的更细规则留给 T019。
- `timeout_ms` 采用仓内已冻结的毫秒单位习惯，与 `AgentRequest.timeout_ms` 和 `RuntimeBudget.max_latency_ms` 一致，避免提前引入 WorkerLease 的 deadline 语义。

---

## 6. Design->Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | `contracts/include/task/WorkerTask.h`、`contracts/include/task/WorkerTaskGuards.h` |
| 测试目标 | `tests/contract/task/WorkerTaskContractTest.cpp` |
| 验收命令 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R WorkerTaskContractTest --output-on-failure && ctest --test-dir build-ci -L contract --output-on-failure` |

---

## 7. D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | WorkerTask 的 execution-unit 定位、最小字段集合、顶层禁区和 Design->Build 三件套均已闭合，且未越界到 T019/T020 |

**Gate 结论：PASS — 可进入 WP04-T018-B**

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T018 来源与三件套 | `docs/todos/contracts-freeze/WP-04-边界对象TODO.md` | WP04-T018 行 |
| WorkerTask 影响项与最小槽位 | `docs/todos/contracts-freeze/deliverables/WP04-T013-ADR008对象影响清单.md` | §3.3 |
| WorkerTask 与 MultiAgentRequest 分层 | `docs/todos/contracts-freeze/deliverables/WP04-T014-MultiAgentRequest语义说明.md` | §5.3 |
| WorkerTask 与 MultiAgentResult 分层 | `docs/todos/contracts-freeze/deliverables/WP04-T016-MultiAgentResult语义说明.md` | §5.3 |
| 协同语义核对单 | `docs/todos/contracts-freeze/deliverables/WP01-T011-协同语义核对单.md` | §4.3 / §6 |
| ADR-008 主控与协同边界 | `docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md` | §3 / §4 / §5.3 / §8 |
| Multi-Agent 架构与 task 子系统 | `docs/architecture/DASSALL_Agent_architecture.md` | §4.8 / §5.11 / §6.11 / §6.12 |
| 工程目录与 tests 落点 | `docs/architecture/DASALL_Engineering_Blueprint.md` | §3.1 / §3.10 / §7.1 |
| contracts 波次冻结原则 | `docs/plans/DASALL_contracts冻结实施计划.md` | §2 / §7 / §8 |
| 契约优先与阶段 Gate 纪律 | `docs/plans/DASALL_工程落地实现步骤指引.md` | 阶段 B / 阶段 L |

### 8.2 外部业界参考

1. Anthropic, Building Effective Agents  
   结论：orchestrator-workers 模式中，中央 orchestrator 负责拆解和综合结果，worker 处理受控子任务；应保持简单、透明和可测试的子任务接口。  
   参考：https://www.anthropic.com/engineering/building-effective-agents

2. Microsoft Azure Architecture Center, AI agent orchestration patterns  
   结论：multi-agent orchestration 中 orchestrator 负责 work distribution、result aggregation、reliability 控制；中间 agent 输出需要验证，timeout/retry/checkpoint 应由 orchestrator 侧控制。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/ai-ml/guide/ai-agent-design-patterns

3. Proto Best Practices  
   结论：契约对象应单一职责、兼容演进优先、避免对象膨胀，支持 T018 先冻结最小执行单元骨架。  
   参考：https://protobuf.dev/best-practices/dos-donts/