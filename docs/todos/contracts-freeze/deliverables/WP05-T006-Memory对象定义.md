# WP05-T006 Memory 对象定义

最近更新时间：2026-03-20
任务状态：Done
任务编号：WP05-T006
上游输入：WP03 ContextPacket/Observation/Checkpoint 冻结结果、ADR-006 ContextOrchestrator 与 PromptComposer 边界、DASSALL Agent 架构文档 3.8.5/4.2/5.3.6、DASALL Engineering Blueprint 3.7、DASALL contracts 冻结实施计划

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. 架构文档 3.8.5 明确建议在 contracts 层继续冻结 `Session` 与 `Turn`，用于承载跨轮会话记录、工具轨迹和最终回复。
2. 架构文档 4.2 说明 `Session Manager` 输出的是 `SessionContext`，这是 runtime/memory 内部运行态对象，不应直接暴露为稳定共享契约。
3. 架构文档 5.3.6 与 Engineering Blueprint 3.7 明确 memory 子系统管理会话级短期记忆、摘要沉淀和经验写回，并要求 `SummaryMemory` 至少覆盖 `decisions_made`、`confirmed_facts`、`tool_outcomes`，而不仅是聊天压缩。
4. ADR-006 §3.2/§6.1 冻结了 `ContextPacket` 只承载语义上下文槽位，其中 `summary_memory` 只是供认知层和 PromptComposer 消费的摘要投影，不是 SummaryMemory 对象本体。
5. ADR-006 §3.2 明确 `ContextOrchestrator` 从 Working Memory、Summary Memory、Long-Term Memory、Experience Memory 拉取候选信息，再生成 `ContextPacket` 并在需要时触发写回闭环，这要求 T006 对象只表达稳定存储面，不能反向承担装配策略。
6. WP03-T012/T013 已冻结 `Checkpoint` 作为最小恢复状态对象，负责 `state`、`step_id`、`working_memory_snapshot`、`pending_action` 等恢复必需字段；T006 必须避免让 memory 记录对象吸收这些 runtime-owned 恢复字段。
7. 实施计划说明 memory 子域放在 prompt 之后，是因为 `Turn`、`Session`、`SummaryMemory` 需要依赖已经稳定的 `Observation`、`ContextPacket` 和 `Checkpoint`，且每个子域对象不得重复定义横切基础字段、不得逆向修改主链路边界。

### 外部参考清单

1. Protobuf Language Guide: Updating A Message Type：新增字段通常安全，但修改既有字段编号或语义会引入兼容风险；稳定契约应优先使用显式 presence 和最小必填集合，为后续演进保留兼容空间。
2. Consumer-Driven Contracts Pattern：provider contract 应只暴露真实消费方所需的最小必要元素，消费者应执行 just-enough validation，而不是把 provider 的全部内部状态投影到共享契约中。

### 对本任务的可落地启发

1. `Turn` 应表达“单轮会话记录”，保存用户输入、工具/观察引用和最终回复，但不能演化为 `Observation`、`ToolResult` 或 `Checkpoint` 的容器。
2. `Session` 应表达“稳定会话索引面”，只保存会话标识、turn 索引、摘要引用和轻量元数据，而不是 `SessionContext` 或顶层 FSM 的运行态快照。
3. `SummaryMemory` 应表达“可沉淀的结构化摘要”，保留 summary 文本以及决策/事实/工具结果等高价值摘要槽位，但不回流为 `ContextPacket` 或 `BeliefState` 的完整替代。
4. 为保持后续兼容性，T006 对象应继续沿用 `std::optional<T>` 和显式 field guards，让“缺失”“为空”“越界”三类状态可二值判定。
5. Contract tests 应围绕真实消费面设计：验证合法最小对象可通过，同时阻断 `SessionContext`、`Checkpoint` 和执行结果原始载荷越权进入 memory 契约对象。

## 1. 任务理解

本任务只处理 WP05-T006：冻结 `Turn`、`Session`、`SummaryMemory` 的稳定契约边界，并落盘 memory 子域的 contracts 对象、守卫和 contract test。

本任务不处理：

1. `SessionContext`、`ContextAssembleRequest/Result`、`CompressionRequest/Result` 的运行态详细设计。
2. `Checkpoint`、`Observation`、`BeliefState`、`MemoryFact`、`ExperienceMemory` 的字段回改。
3. memory 后端的 SQLite/FAISS/JSON/Proto 序列化策略，只定义共享 contracts 面。

## 2. 约束与边界

### 2.1 直接约束

1. `Turn`、`Session`、`SummaryMemory` 必须服务 memory 子系统的稳定读写面，不得把 runtime-owned 的恢复状态、装配策略或 provider-specific 细节写入对象。
2. 这些对象只能复用已冻结的横切标识（如 `session_id`），不能重新定义 `trace_id`、`request_channel`、`fsm_state` 等无关顶层字段。
3. `SummaryMemory` 与 `ContextPacket.summary_memory` 必须分层：前者是结构化沉淀对象，后者只是面向认知消费的摘要投影。
4. `Session` 与 `SessionContext` 必须分层：前者是共享契约索引面，后者是 SessionManager/runtime 内部运行态对象。
5. 默认向后兼容，保持最小必填字段集合，并以 guard 阻断越权字段语义进入 memory 子域。

### 2.2 Turn 允许与禁止

Turn 允许承载：

1. 稳定标识：`turn_id`、`session_id`。
2. 轮次输入：`user_input`。
3. 轮次输出：`agent_response`。
4. 轨迹引用：`tool_call_refs`、`observation_refs`、`summary_memory_ref`。
5. 时间与审计：`created_at`、`tags`。

Turn 明确禁止承载：

1. Checkpoint 恢复字段：`checkpoint_id`、`state`、`pending_action`、`working_memory_snapshot`、`retry_count`。
2. SessionContext/ContextPacket 装配字段：`current_goal_summary`、`recent_history`、`policy_digest`、`token_budget_report`、`belief_state_summary`。
3. Observation/ToolResult 原始载荷：`payload`、`error`、`side_effects`、`provider_payload`。

### 2.3 Session 允许与禁止

Session 允许承载：

1. 稳定标识：`session_id`。
2. 会话索引：`turn_ids`。
3. 轻量元数据：`user_id`、`metadata_digest`。
4. 摘要锚点：`latest_summary_memory_ref`。
5. 生命周期：`created_at`、`last_active_at`、`tags`。

Session 明确禁止承载：

1. SessionContext 内部运行态：`active_goal`、`skill_profile`、`planner_state`、`visible_tools`、`policy_digest`。
2. 顶层 Session/FSM 控制字段：`fsm_state`、`retry_after_ms`、`scheduler_slot`、`recovery_action`。
3. Checkpoint 恢复字段：`checkpoint_id`、`pending_action`、`working_memory_snapshot`、`retry_count`。

### 2.4 SummaryMemory 允许与禁止

SummaryMemory 允许承载：

1. 稳定标识：`summary_id`、`session_id`。
2. 摘要正文：`summary_text`。
3. 结构化沉淀槽位：`source_turn_ids`、`decisions_made`、`confirmed_facts`、`tool_outcomes`。
4. 生命周期：`created_at`、`tags`。

SummaryMemory 明确禁止承载：

1. ContextPacket/SessionContext 装配字段：`current_goal_summary`、`recent_history`、`policy_digest`、`token_budget_report`、`belief_state_summary`。
2. Checkpoint 恢复字段：`checkpoint_id`、`state`、`pending_action`、`working_memory_snapshot`、`retry_count`。
3. 原始执行记录字段：`payload`、`error`、`side_effects`、`provider_payload`。

## 3. Design 原子清单

1. D1：冻结 Turn 的单轮记录边界和字段最小集。
- 输入依据：架构文档 3.8.5、WP03 Observation/ContextPacket 冻结结果。
- 产出：`contracts/include/memory/Turn.h`。
- 完成判定：Turn 可表达用户输入、工具/观察引用和最终回复，且不含 Checkpoint 或 ContextPacket 装配字段。
- 风险回退：若字段更像恢复状态或上下文装配状态，则迁回 Checkpoint/SessionContext。

2. D2：冻结 Session 的稳定会话索引面。
- 输入依据：架构文档 4.2 Session Manager 边界、实施计划阶段 8。
- 产出：`contracts/include/memory/Session.h`。
- 完成判定：Session 只保留会话标识、turn 索引、摘要引用与轻量元数据，不暴露 SessionContext/FSM 细节。
- 风险回退：若字段要求实时执行控制或恢复语义，则迁回 runtime-owned SessionContext/Checkpoint。

3. D3：冻结 SummaryMemory 的结构化摘要沉淀面。
- 输入依据：Engineering Blueprint 3.7、ADR-006 §3.2/§6.1。
- 产出：`contracts/include/memory/SummaryMemory.h`。
- 完成判定：SummaryMemory 可表达摘要正文、决策、事实、工具结果沉淀，且不替代 ContextPacket 或 BeliefState。
- 风险回退：若字段开始承载装配策略或认知状态，则迁回 ContextOrchestrator/BeliefState。

4. D4：设计 memory 子域边界可验证测试矩阵。
- 输入依据：WP03/WP05 现有 contract tests 风格。
- 产出：`tests/contract/memory/TurnSessionSummaryMemoryContractTest.cpp` + CMake 注册。
- 完成判定：至少 1 个正例 + 1 个负例，并显式验证 Turn/Session/SummaryMemory 对 `SessionContext`、`Checkpoint`、执行记录字段的拒绝。
- 风险回退：若测试只覆盖 happy path，则补充字段缺失、时间逆序、重复引用和越界字段断言。

## 4. Design -> Build 映射

| D 原子项 | 设计结论 | 对应 Build 动作 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| D1 | Turn 只表达单轮记录面 | 新增 Turn 契约与内联守卫 | `contracts/include/memory/Turn.h` | 正例：最小合法 Turn 通过；负例：缺失 `turn_id`、重复 `tool_call_refs`、Checkpoint 字段边界拒绝 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R TurnSessionSummaryMemoryContractTest --output-on-failure` |
| D2 | Session 只表达会话索引面 | 新增 Session 契约与内联守卫 | `contracts/include/memory/Session.h` | 正例：合法 Session 通过；负例：缺失 `turn_ids`、`last_active_at` 早于 `created_at`、SessionContext/Checkpoint 字段边界拒绝 | 同上 |
| D3 | SummaryMemory 只表达结构化摘要沉淀 | 新增 SummaryMemory 契约与内联守卫 | `contracts/include/memory/SummaryMemory.h` | 正例：合法 SummaryMemory 通过；负例：缺失 `summary_text`、重复 `confirmed_facts`、Context/Checkpoint/执行记录字段边界拒绝 | 同上 |
| D4 | memory 子域边界需可自动验证 | 新增 TurnSessionSummaryMemoryContractTest 并接入 CMake | `tests/contract/memory/TurnSessionSummaryMemoryContractTest.cpp`；`tests/contract/CMakeLists.txt` | 覆盖 Turn/Session/SummaryMemory 的 required-field、field-rule、boundary 决策 | 同上 |

## 5. D Gate 结果

1. D 文档已落盘。
2. Design 原子清单已冻结，且每项具备二值完成判定。
3. Build 三件套已锁定：
- 代码目标：`contracts/include/memory/Turn.h`、`contracts/include/memory/Session.h`、`contracts/include/memory/SummaryMemory.h`
- 测试目标：`tests/contract/memory/TurnSessionSummaryMemoryContractTest.cpp` + `tests/contract/CMakeLists.txt`
- 验收命令：`cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R TurnSessionSummaryMemoryContractTest --output-on-failure`
4. 范围未越界，不回改 ContextPacket、Checkpoint、Observation，可进入 B。

Gate 结论：PASS。

## 6. Build 执行清单

1. B1：新增 Turn 契约头文件与内联 required/field/boundary guards。
2. B2：新增 Session 契约头文件与内联 required/field/boundary guards。
3. B3：新增 SummaryMemory 契约头文件与内联 required/field/boundary guards。
4. B4：新增 contract test，覆盖正例、负例和越界字段拒绝断言，并接入 `tests/contract/CMakeLists.txt`。

## 7. 风险与回退

1. 风险：把 SessionContext 内部状态直接写入 Session，导致 runtime 与共享契约混层。
- 回退：Session 只保留 turn 索引、摘要引用和轻量元数据。
2. 风险：把 Checkpoint 恢复状态塞入 Turn 或 SummaryMemory，导致 memory 对象成为恢复快照副本。
- 回退：恢复状态只允许保留在 Checkpoint 及其下游恢复对象中。
3. 风险：把 Observation/ToolResult 原始载荷写入 Turn 或 SummaryMemory，导致对象膨胀并污染后续序列化兼容性。
- 回退：只保留 ref/digest/summary，不嵌入执行原始记录。
4. 风险：测试只验证 happy path，后续无法自动发现边界回退。
- 回退：固定保留 required-field、时间边界、重复引用和 forbidden-field 回归断言。

## 8. Blocker 状态

当前无 blocker。