# COG-TODO-033 Runtime 消费 Belief 与 Context 信号收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Design + Build runtime 语义消费补强

## 1. 本地证据

1. `runtime/src/AgentOrchestrator.cpp` 的 live unary path 在调用 `cognition_engine->decide()` 后，只检查 `action_decision` 是否为 `ExecuteAction`；`belief_update_hint` 与 `context_sufficiency` 没有任何消费逻辑。
2. 同一文件的 tool round 在 `reflect()` 后直接丢弃返回值，只把控制流固定推回 `Reasoning -> Responding`，因此当前 runtime 无法证明 cognition 返回的 belief/context 语义进入了 runtime owner。
3. `memory/include/IMemoryManager.h` 已提供 `write_back()` 与 `prepare_context()` 两个稳定 seam，033 不需要新增 memory public API。
4. `memory/include/writeback/MemoryWritebackRequest.h` 已经支持 `Turn`、`SummaryMemory`、`FactCandidate`、`ExperienceCandidate`，足以承载 `BeliefUpdateHint` 的 bounded writeback 投影。
5. `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` 当前会构造含 `BeliefUpdateHint` 与 `ContextSufficiencySignal` 的 cognition 结果，但断言只验证 executable / non-executable contract，没有证明 runtime 真正消费这些字段。
6. `runtime/src/AgentOrchestrator.cpp` 已有完整的 waiting-clarify state、checkpoint、resume plan 生成路径，因此 033 不应新增新的 terminal semantics，而应在 context reload 失败后复用现有 waiting-clarify 出口。

## 2. 边界与职责收敛

1. `BeliefUpdateHint` 继续由 cognition 生成，但只允许 runtime 经 `IMemoryManager::write_back()` 投影到 memory；cognition 不直接依赖 memory。
2. `ContextSufficiencySignal.recommend_context_reload` 继续只是 suggestion；真正是否 refresh、何时 refresh、refresh 后如何 degrade，继续由 runtime owner 决定。
3. refresh 预算与重试次数继续受 runtime budget controller 管理；033 最多允许一次 replan-budgeted context refresh，不引入无限重试。
4. waiting-clarify 继续由 runtime checkpoint/session machinery 承载；033 不在 cognition contract 中新增等待态对象。
5. belief writeback 采用 best-effort：写回失败可被记录，但不能覆盖主决策错误或把成功路径整体改为失败。

## 3. 数据与接口说明

### 3.1 输入与输出

| 接口 / 数据 | 方向 | 本轮约束 |
|---|---|---|
| `cognition::CognitionDecisionResult.belief_update_hint` | cognition -> runtime | 仅由 runtime 投影成 `MemoryWritebackRequest`，不直接跨边界暴露 memory 对象 |
| `cognition::ContextSufficiencySignal` | cognition -> runtime | `recommend_context_reload=true` 时至多触发一次 replan-budgeted `prepare_context()` 刷新 |
| `memory::MemoryWritebackRequest` | runtime -> memory | 只写最小 turn/summary/facts，不嵌入 checkpoint 或 raw execution payload |
| waiting clarify state | runtime terminalization | refresh 仍不能拿到 executable action 时，复用既有 waiting checkpoint + resume plan 语义 |

### 3.2 目标文件范围

1. `runtime/src/AgentOrchestrator.cpp`
2. `tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp`
3. `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp`
4. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md`
5. `docs/worklog/DASALL_开发执行记录.md`

## 4. 流程与时序

### 4.1 decide 后的 belief writeback

1. runtime 在 live unary path 取得 `CognitionDecisionResult` 后，先读取 `belief_update_hint`。
2. 若存在 hint，runtime 构造最小 `MemoryWritebackRequest`：
   - `Turn` 记录当前 request/session/user_input；
   - `SummaryMemory` 记录本轮 belief 摘要；
   - `FactCandidate` 记录 `confirmed_facts_delta` 和必要 evidence anchor；
   - 不让 writeback failure 覆盖主决策错误。
3. writeback 成功时，后续 integration test 必须能在 memory snapshot 或 sqlite store 中观察到新事实。

### 4.2 context reload 与 clarification degrade

1. 如果 cognition 没给出 `ExecuteAction`，且 `context_sufficiency.recommend_context_reload=true`，runtime 先检查 `can_replan()`。
2. 预算允许时，runtime 消耗一次 replan budget，调用 `prepare_context()` 刷新上下文，并带上 `missing_evidence_hints` 形成 reload reason。
3. runtime 用刷新后的 context 再调用一次 `decide()`。
4. 若第二次仍没有 executable action，则复用 waiting-clarify 出口，生成 waiting checkpoint / session binding / resume plan，并把 cognition 的 clarification question 或 missing evidence hints 体现在 waiting reason 中。
5. 若 budget 不允许 refresh，则直接进入 clarification degrade，不隐式重试。

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| belief hint 只能经 memory seam 写回 | `AgentOrchestrator.cpp` writeback helper | integration 可观察到事实落库或 working memory 更新 |
| context reload 只允许一次且受 runtime budget 约束 | `AgentOrchestrator.cpp` refresh helper | unit/integration 可观察到 `prepare_context()` 被 bounded 地调用 |
| refresh 失败后复用 waiting-clarify | `AgentOrchestrator.cpp` waiting branch | runtime 不再把 context-insufficient case 一律当作 hard failure |
| writeback failure 不覆盖主决策 | runtime tests | cognition success path 在 writeback fail 时仍保持原 terminal status |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 在 `AgentOrchestrator` 增加 belief writeback helper | contract test 能观察 writeback 被调用或 facts 落库 | `Build_CMakeTools(buildTargets=["dasall_runtime_cognition_loop_smoke_unit_test","dasall_cognition_runtime_interaction_contract_integration_test"])` | 若 writeback mapping 过大，只先落 confirmed facts 与 summary，不提前扩 experience |
| B2 | 在 main loop 增加一次 replan-budgeted context refresh | non-executable + recommend reload case 触发一次 refresh | `RunCtest_CMakeTools(tests=["RuntimeCognitionLoopSmokeTest","CognitionRuntimeInteractionContractTest"])` | 若 refresh 后仍非 executable，复用 waiting-clarify，而不是引入新的失败模式 |

## 7. D Gate

Gate = PASS。

进入 Build 的依据已经充分：控制点已定位在 `AgentOrchestrator` live unary path；memory seam 与 waiting-clarify machinery 都已存在；本轮不扩 shared contracts，不改 cognition owner，只补 runtime 消费逻辑和最小测试证据。

## 8. Build 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_runtime_cognition_loop_smoke_unit_test","dasall_cognition_runtime_interaction_contract_integration_test"])`
   - 结果：首轮失败暴露两个局部接线问题，同一 slice 内修复后通过。
   - 修复点：
     - `RuntimeCognitionLoopSmokeTest.cpp` 需要读取 sqlite store 私有头，补充 `tests/unit/runtime/CMakeLists.txt` 的 `memory/src` include 目录。
     - `CognitionRuntimeInteractionContractTest.cpp` 的 writeback failure probe 误用了不存在的 `ResultCode` 枚举，改为现有的 runtime failure code。
2. `RunCtest_CMakeTools(tests=["RuntimeCognitionLoopSmokeTest","CognitionRuntimeInteractionContractTest"])`
   - 结果：通过，2 条聚焦测试全部通过。
   - 关键覆盖点：
     - `RuntimeCognitionLoopSmokeTest` 证明 executable cognition path 会把 belief hint 真实写回 sqlite memory，并更新 working memory 的 `latest_turn_id`。
     - `CognitionRuntimeInteractionContractTest` 证明 executable path 发生一次 belief writeback；`recommend_context_reload=true` 的 non-executable path 触发恰好一次额外 `prepare_context()`，随后进入 waiting clarify；writeback failure 仍不覆盖 completed 结果。
3. `Build_CMakeTools(buildTargets=["dasall_cognition_failure_injection_integration_test"])` + `RunCtest_CMakeTools(tests=["CognitionFailureInjectionTest"])`
   - 结果：通过。
   - 补充覆盖点：显式 `error_info` / `result_code` 的 cognition failure 仍走 hard failure surface，没有被新的 clarification degrade 路径吞掉。
4. `get_errors(filePaths=[runtime/src/AgentOrchestrator.cpp, tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp, tests/unit/runtime/RuntimeCognitionLoopSmokeTest.cpp, tests/unit/runtime/CMakeLists.txt])`
   - 结果：无新增编辑器错误。

## 9. 完成判定

COG-TODO-033 已完成。

1. Runtime 现在会在 live unary path 中消费 `CognitionDecisionResult.belief_update_hint`，并通过 `IMemoryManager::write_back()` 以 best-effort 方式投影最小 turn/summary/facts。
2. 当 cognition 在无显式错误的前提下返回 `recommend_context_reload=true` 时，runtime 会在 replan budget 允许的情况下额外执行一次 `prepare_context()`，并用刷新后的 context 重新做一次 `decide()`。
3. refresh 后仍没有 executable action，或者 replan budget / refreshed context 不可用时，runtime 会复用既有 waiting-clarify checkpoint/session binding/resume plan 语义，而不是把这类 context-insufficient case 一律硬失败。
4. 显式 cognition error surface 仍保持 fail-closed；belief writeback failure 不覆盖主决策终态。