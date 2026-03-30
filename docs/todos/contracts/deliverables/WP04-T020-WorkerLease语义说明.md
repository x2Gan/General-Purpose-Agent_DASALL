# WP04-T020-D：WorkerLease 语义说明

> 版本：1.0 | 日期：2026-03-19 | 状态：Done
> 任务编号：WP04-T020-D
> 上游输入：WP04-T013-D/B、WP04-T018-D/B、WP04-T019-D/B、ADR-008 §3.2/§3.3/§4/§5.4/§7、docs/architecture/DASSALL_Agent_architecture.md §3.8/§4.8/§5.11/§6.11/§6.12、docs/architecture/DASALL_Engineering_Blueprint.md §3.1/§3.10/§7.1、docs/plans/DASALL_contracts冻结实施计划.md §2/§7/§8、docs/plans/DASALL_工程落地实现步骤指引.md 阶段 B / 阶段 L、docs/todos/contracts/deliverables/WP02-T010-TimeDeadline规范.md

---

## 1. 任务识别

### 1.1 范围

- 定义 WorkerLease 作为 multi-agent 子域中的局部租约元数据对象。
- 冻结 WorkerLease 的最小对象骨架、职责边界与 required/boundary guard 目标。
- 明确 WorkerLease 与 WorkerTask、顶层 checkpoint 子域快照、全局 AgentOrchestrator 状态的分层关系。
- 将 T020 的设计结果映射到 `contracts/include/task/WorkerLease.h`、`contracts/include/task/WorkerLeaseGuards.h` 和 `tests/contract/task/WorkerLeaseContractTest.cpp`。

### 1.2 排除项

- 不进入 T021 字段表级 whitespace hygiene、唯一性或生命周期枚举细化。
- 不把 WorkerLease 扩张为新的 resume 入口、全局 checkpoint 容器或最终结果对象。
- 不实现 AgentRegistry 匹配算法、租约回收策略、续约调度器或 retry/backoff runtime 策略。
- 不为 WorkerLease 引入第二套时间语义；继续复用仓内已冻结的 `deadline_at` 主判定口径。
- 不在本任务冻结 `lease_state` 枚举，避免把运行时生命周期机理提前固化为 contracts 公共枚举。

---

## 2. Phase 0：研究结论摘要

### 2.1 本地证据清单

| # | 来源 | 对 T020 的直接约束 |
|---|---|---|
| L1 | ADR-008 §5.4 / §8 | 顶层 checkpoint 与子任务快照必须分层；MultiAgentCoordinator 不能成为新的 resume 入口 |
| L2 | WP04-T013-D §3.4 | WorkerLease 只表达租约元数据和局部续约/释放语义，不承担结果对象或全局恢复入口职责 |
| L3 | WP04 TODO | T020 仅交付 WorkerLease 职责边界；T021 才细化字段表，且至少覆盖 `lease_id/worker_ref/deadline/renewal/release_reason` |
| L4 | WP04-T018-D §4/§5 | WorkerTask 只保留 `lease_id` 引用，`renewal_deadline`、`release_reason` 等租约细节必须下沉到 WorkerLease |
| L5 | WP04-T019-D §1.2 / §5.3 | WorkerTask 不引入第二套 timeout/deadline 口径；涉及租约 deadline 的控制应归 WorkerLease |
| L6 | WP02-T010 | `deadline_at` 是执行判定主字段，`timeout_ms` 是策略输入；不得混淆 deadline 与 timeout 语义 |
| L7 | WP02-T015-B1-timeout迁移清单 | 若显式提供 `deadline_at`，执行判定必须以 `deadline_at` 为准 |
| L8 | WP01-T011 §3.1 / §6 | 协同对象必须保持全局主控与子域对象分层；worker 侧对象不得承载顶层 Session/FSM / resume 职责 |
| L9 | `contracts/include/agent/MultiAgentBoundaryContracts.h` | WorkerLease 已在 ADR-008 直接影响对象 catalog 中登记，上游锚点是 `WorkerTask / top-level checkpoint subdomain snapshot` |
| L10 | `contracts/include/task/` 当前状态 | 目前仅有 WorkerTask 对象与守卫，尚无 WorkerLease 实体对象与 contract test，T020-B 必须补齐 |

### 2.2 外部参考清单

| # | 来源 | 对 T020 的映射 |
|---|---|---|
| E1 | Anthropic, Building Effective Agents | agent interface 应保持简单、透明、可测试；复杂系统优先采用最小可组合对象而非隐式状态协议 |
| E2 | Microsoft Azure Architecture Center, AI agent orchestration patterns | 多 agent 编排应最小化共享状态、将持久状态外置并校验中间输出，避免错误沿链路传播 |
| E3 | Proto Best Practices | 契约应兼容优先、避免消息膨胀；新增字段要谨慎，公共对象应只携带单一职责的最小语义 |

### 2.3 对本任务的可落地启发

1. WorkerLease 应是“租约元数据对象”，不是“局部调度器”或“第二个 checkpoint”。
2. 最小槽位应只覆盖租约身份、绑定 worker、硬截止时间、续约锚点和释放原因，不提前固化 runtime 生命周期算法。
3. 时间语义必须沿用仓内冻结结论：WorkerLease 使用 `deadline_at` 表达硬边界，不回退为 `timeout_ms` 主判定。
4. T020-B 只需要 required/boundary guards，阻断缺失租约锚点、非法 deadline、越界 resume/result 字段；更细的字段 hygiene 留给 T021。
5. contract test 需要证明三件事：有效租约对象可通过、非法续约边界被拒绝、WorkerLease 不得携带全局主控或最终结果字段。

---

## 3. Phase 1：设计任务清单（D 原子项）

| D 项 | 设计目标 | 输入依据 | 产出文档路径 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定 WorkerLease 的对象定位与直接消费者 | ADR-008 §5.4、WP04-T013-D | 本文件 §4.1 / §4.3 | 明确其为 lease-metadata object，消费者是 MultiAgentCoordinator / runtime snapshot path | 若出现第二主控或 checkpoint 入口语义，回退为局部租约元数据定位 |
| D2 | 冻结 WorkerLease 最小槽位与 required/optional 归属 | WP04 TODO、WP04-T018-D、WP02-T010 | 本文件 §4.2 | 最小字段闭合，且与 T021 字段表预期保持一致 | 若字段超过租约元数据最小集合，回退为最小槽位 |
| D3 | 明确 WorkerLease 禁区与分层关系 | ADR-008 §4/§5.4、WP01-T011、WP04-T013-D | 本文件 §5 | 全局 Session/FSM、resume、结果语义越权全部显式阻断 | 若出现 checkpoint/resume/result 语义混入，回退到子域租约边界 |
| D4 | 输出 T020 的 Design->Build 三件套 | WP04 TODO、现有 contract test 模式 | 本文件 §6 | 代码目标、测试目标、验收命令完整闭合 | 若需要越到 T021 字段表或 runtime 策略实现，判定越界并回退 |
| D5 | 形成 D Gate 结论 | D1-D4 | 本文件 §7 | PASS/Blocked 二值明确 | 若对象边界或三件套未闭合，则 Blocked |

---

## 4. WorkerLease 语义定义

### 4.1 对象定位

WorkerLease 是由 MultiAgentCoordinator 或其下游 runtime 租约管理路径维护的局部租约元数据对象。

它表达的是：

1. 当前租约本身的稳定标识。
2. 当前租约绑定到哪个 worker 实例或 worker 引用。
3. 当前租约的硬截止时间。
4. 当前租约是否存在局部续约窗口。
5. 当前租约被主动释放时的原因摘要。

它不表达的是：

1. 顶层 Session 生命周期。
2. 全局 FSM 推进状态。
3. 顶层 checkpoint / resume 入口。
4. 协同结果汇总或最终 AgentResult。
5. runtime 的租约回收算法、backoff、仲裁或冲突合并策略。

### 4.2 最小对象骨架

WorkerLease 固定由 3 个必填字段 + 2 个可选字段组成。

#### 必填字段（3 项）

| 字段 | 类型 | 语义 |
|---|---|---|
| `lease_id` | `std::optional<std::string>` | 当前局部租约的稳定标识 |
| `worker_ref` | `std::optional<std::string>` | 当前租约绑定的 worker 引用 |
| `deadline_at` | `std::optional<std::int64_t>` | 当前租约的绝对硬截止时间，毫秒时间戳 |

#### 可选字段（2 项）

| 字段 | 类型 | 语义 |
|---|---|---|
| `renewal_deadline_at` | `std::optional<std::int64_t>` | 当前租约允许申请续约的截止时间点 |
| `release_reason` | `std::optional<std::string>` | 当前租约提前释放时的原因摘要 |

说明：

1. `deadline_at` 采用仓内统一的绝对时间语义，避免再发明租约专用 timeout 表达。
2. `renewal_deadline_at` 只表达“续约窗口边界”，不等于租约新 deadline，也不包含续约次数或 backoff 策略。
3. `release_reason` 只表达释放语义摘要，不等于最终任务失败原因或全局恢复决策。
4. `lease_state` 虽属于 WorkerLease 子域概念，但本任务不冻结为公共字段，避免过早把 runtime 生命周期机理写死到 contracts v1。

### 4.3 直接消费者与关系

| 消费方 | 关系 |
|---|---|
| `MultiAgentCoordinator` | 创建、续约、释放 WorkerLease 的局部控制方 |
| runtime / audit path | 通过 `lease_id`、`worker_ref`、`deadline_at` 追踪租约有效性与审计证据 |
| `WorkerTask` | 仅通过 `lease_id` 建立引用，不内嵌租约细节 |
| top-level checkpoint subdomain snapshot | 可纳入顶层 checkpoint 子域快照，但不反向成为新的 resume 入口 |

---

## 5. 边界与禁区

### 5.1 不得承载全局主控语义

| 禁止字段 | 禁止原因 |
|---|---|
| `global_session_state` / `global_fsm_state` / `session_fsm_state` | 这些属于 AgentOrchestrator 顶层状态，不属于局部租约元数据 |
| `session_id` | WorkerLease 不是顶层会话对象 |

### 5.2 不得承载恢复入口语义

| 禁止字段 | 禁止原因 |
|---|---|
| `checkpoint_ref` / `resume_token` | 顶层 checkpoint 与 resume 入口必须仍由 AgentOrchestrator 持有 |

### 5.3 不得承载结果对象语义

| 禁止字段 | 禁止原因 |
|---|---|
| `agent_result` / `final_agent_response` | 最终输出权只属于 AgentOrchestrator |
| `merged_result` | 这是 MultiAgentResult 的协同汇总语义，不属于租约对象 |

### 5.4 Required / Boundary 守卫目标

#### Required 层（T020-B）

1. `lease_id`、`worker_ref` 必须 present 且非空。
2. `deadline_at` 必须 present 且为正整数毫秒时间戳。

#### Boundary 层（T020-B）

1. `renewal_deadline_at` 若 present，必须为正整数毫秒时间戳。
2. `renewal_deadline_at` 若 present，不得晚于 `deadline_at`。
3. `release_reason` 若 present，必须为非空字符串。
4. WorkerLease 顶层字段不得复用全局 Session/FSM、resume 或结果对象别名。

说明：

- whitespace hygiene、`worker_ref` 字段格式、`release_reason` 词汇约束、`renewal_deadline_at` 与后续续约结果的组合规则，留给 T021。
- 本任务不新增 `timeout_ms` 或 `ttl` 字段，以保持 WorkerLease 与 WP02 时间规范一致。

---

## 6. Design->Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | `contracts/include/task/WorkerLease.h`、`contracts/include/task/WorkerLeaseGuards.h` |
| 测试目标 | `tests/contract/task/WorkerLeaseContractTest.cpp` |
| 验收命令 | `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R WorkerLeaseContractTest --output-on-failure && ctest --test-dir build-ci -L contract --output-on-failure` |

---

## 7. D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | WorkerLease 的 lease-metadata 定位、最小字段集合、时间口径和禁区均已闭合，且未越界到 T021 字段表或 runtime 策略实现 |

**Gate 结论：PASS — 可进入 WP04-T020-B**

---

## 8. 可追溯证据

### 8.1 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| T020 来源与三件套 | `docs/todos/contracts/WP-04-边界对象TODO.md` | WP04-T020 行 |
| WorkerLease 影响项与分层定位 | `docs/todos/contracts/deliverables/WP04-T013-ADR008对象影响清单.md` | §3.4 |
| WorkerTask 对租约语义的排除 | `docs/todos/contracts/deliverables/WP04-T018-WorkerTask语义说明.md` | §1.2 / §5.3 |
| WorkerTask 字段表的时间边界排除 | `docs/todos/contracts/deliverables/WP04-T019-WorkerTask字段表.md` | §1.2 / §5.3 |
| 协同语义核对单 | `docs/todos/contracts/deliverables/WP01-T011-协同语义核对单.md` | §3.1 / §6 |
| ADR-008 主控与 WorkerLease 分层 | `docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md` | §4 / §5.4 / §7 / §8 |
| 时间/截止时间规范 | `docs/todos/contracts/deliverables/WP02-T010-TimeDeadline规范.md` | §3 / §4 |
| timeout 迁移与 deadline 优先 | `docs/todos/contracts/deliverables/WP02-T015-B1-timeout迁移清单.md` | §2 / §4 |
| 多 Agent 架构与 task 子系统 | `docs/architecture/DASSALL_Agent_architecture.md` | §3.8 / §4.8 / §5.11 / §6.11 / §6.12 |
| contracts/task 与 tests/task 工程落点 | `docs/architecture/DASALL_Engineering_Blueprint.md` | §3.1 / §3.10 / §7.1 |

### 8.2 外部业界参考

1. Anthropic, Building Effective Agents  
   结论：agent 接口应保持简单、透明、可测试；复杂系统中应优先设计最小、可组合的中间对象，而不是隐藏状态协议。  
   参考：https://www.anthropic.com/engineering/building-effective-agents

2. Microsoft Azure Architecture Center, AI agent orchestration patterns  
   结论：多 agent 编排应最小化共享状态、外置持久状态、校验中间输出，并保持 least-privilege 边界与可靠性控制。  
   参考：https://learn.microsoft.com/en-us/azure/architecture/ai-ml/guide/ai-agent-design-patterns

3. Proto Best Practices  
   结论：契约应兼容优先、避免对象膨胀、保持单一职责；新增字段应谨慎，公共对象应维持最小稳定表面。  
   参考：https://protobuf.dev/best-practices/dos-donts/