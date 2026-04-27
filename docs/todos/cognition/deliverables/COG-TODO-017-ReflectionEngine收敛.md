# COG-TODO-017 ReflectionEngine 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready reflection stage implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.3 已冻结 `ReflectionEngine` 职责：基于 `Observation`、`ErrorInfo`、`PlanGraph`、`BeliefState` 与 `GoalContract` 的偏差关系生成 shared `ReflectionDecision`，显式表达 `continue`、`retry_step`、`replan`、`abort_safe` 等 suggestion-only 语义。
2. 同一章节明确非职责边界：`ReflectionEngine` 不持有 retry counter、backoff、checkpoint admission，不直接调用 `Planner` 或 `ToolManager`，也不越级执行恢复动作；恢复准入与执行仍归 Runtime/RecoveryManager。
3. `cognition/include/IReflectionEngine.h` 已把公共接口冻结为 `analyze(const ReflectionAnalysisRequest&) -> contracts::ReflectionDecision`；输入固定包含 `GoalContract`、`BeliefState`、`latest_observation`、可选 `ErrorInfo`、可选 `PlanGraph` 与 `StageExecutionHints`。
4. `contracts/include/checkpoint/ReflectionDecision.h` 与 `ReflectionDecisionGuards.h` 已冻结 shared 输出边界：合法字段仅限 `request_id`、`decision_kind`、`rationale` 及 `goal_id`、`confidence`、`relevant_observation_refs`、`hint_ref`、`created_at`、`tags` 等被动元数据；`retry_after_ms`、`backoff_strategy` 等 runtime scheduling 字段必须被拒绝。
5. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 中的 COG-TC004 与 COG-TODO-017 完成判定一致要求：反思阶段只能输出 suggestion-only `ReflectionDecision`，并且在证据不足或风险过高时保守收敛到 `abort_safe`，不得把恢复权回流到 cognition。

## 2. 外部参考

1. Prompt Engineering Guide 的 Reflexion 文档指出：Reflexion 把环境反馈转成语言化的 self-reflection，帮助 agent 从 trial-and-error 中学习，并把更具体的 verbal feedback 用作后续决策的上下文；它强调显式自评、显式反馈和可解释的改进建议，而不是把反馈压成不可解释的标量控制信号：https://www.promptingguide.ai/techniques/reflexion

本轮借鉴点：`ReflectionEngine` 把失败 observation、error 与 belief assumptions 显式解释为 `RetryStep`、`Replan` 或 `AbortSafe` 等语义建议，同时保留 `rationale`、`relevant_observation_refs` 与 `hint_ref`，让 Runtime 在持有 admission 权的前提下继续裁定是否真正执行恢复动作。

## 3. 主结论

1. 新增私有 `ReflectionEngine`，落在 `cognition/src/reflection/ReflectionEngine.h/.cpp`，以 `IReflectionEngine` 为 owner 实现 `analyze()`。
2. 实现内部规则切分为 `classify_failure_source()`、`evaluate_goal_gap()`、`detect_assumption_invalidations()`、`project_reflection_decision()`、`validate_reflection_contract()`；决策输入覆盖 `latest_observation`、`error_info`、`active_plan`、`belief_state.assumptions` 与 `execution_hints.risk_tolerance`。
3. 当前规则式行为收敛如下：
   - retryable 且局部失败、未触发 goal gap、风险较低时输出 `RetryStep`；
   - 观察结果推翻关键 assumptions 或环境已明显偏移时输出 `Replan`；
   - side effects 已发生、policy 风险高或证据不足以安全判断时输出 `AbortSafe`；
   - success observation 且无 invalidation 时保留 `Continue`。
4. `ReflectionEngine` 在产出 `ReflectionDecision` 后会调用 `validate_reflection_decision_field_rules()` 做契约自检；若内部投影失配，则降级为最小合法 `AbortSafe` suggestion，而不是把 runtime scheduling 字段或无效 shared object 泄漏到外层。
5. 本轮保持了 ADR-007 边界：`ReflectionEngine` 只给语义建议，不生成恢复动作、不生成 runtime policy，也不引入 retry/backoff/checkpoint 等执行字段。

## 4. 边界与职责

| 组件 | 职责 | 非职责 |
|---|---|---|
| `ReflectionEngine` | 分析 observation / error / active plan / belief 偏差并输出 `ReflectionDecision` | 不执行恢复动作；不拥有 admission；不生成 runtime scheduling 字段 |
| `ReflectionDecision` | 表达 continue / retry_step / replan / abort_safe 等语义建议 | 不表达 `retry_after_ms`、`backoff_strategy`、checkpoint 等 runtime 控制 |
| Runtime/RecoveryManager | 消费 `ReflectionDecision` 并结合预算、幂等性、断路器状态做最终恢复裁定 | 不把 failure semantics owner 反向塞回 cognition |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| retryable 局部失败应给出 step-level 建议 | `cognition/src/reflection/ReflectionEngine.cpp` + `ReflectionEngineDecisionTest.cpp` | 失败 observation + retryable error 输出 `RetryStep` |
| assumption invalidation 应切到重规划建议 | `ReflectionEngine.cpp` + `ReflectionEngineBeliefInvalidationTest.cpp` | 观测推翻关键 assumptions 时输出 `Replan` |
| 高风险/证据不足时必须保守收敛 | `ReflectionEngine.cpp` + `ReflectionEngineConservativeAbortTest.cpp` | 高风险 side effect 场景输出 `AbortSafe` |
| 反思输出必须持续满足 shared contract | `ReflectionEngine.cpp` + 三条 tests 中的 guard 断言 | `validate_reflection_decision_field_rules()` 全部通过 |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增私有 `ReflectionEngine` 与 failure / goal-gap / invalidation 规则 | `RetryStep` / `Continue` / `Replan` / `AbortSafe` 可局部断言 | `Build_CMakeTools(buildTargets=["dasall_reflection_engine_decision_unit_test","dasall_reflection_engine_belief_invalidation_unit_test","dasall_reflection_engine_conservative_abort_unit_test"])` | 若决策语义漂移，优先在同一 slice 收紧 failure 分类与风险门槛 |
| B2 | 为输出增加 `ReflectionDecision` guard 自检与 fallback | 产出持续满足 shared contract | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_decision_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_belief_invalidation_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_conservative_abort_unit_test` | 若 guard 失败，统一降级为最小合法 `AbortSafe` suggestion |
| B3 | 注册 reflection sources 与三条 focused unit targets | discoverability 与直接执行成立 | 同上 | 若接线扩大，只保留 cognition reflection slice 内的最小改动 |

## 7. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_reflection_engine_decision_unit_test","dasall_reflection_engine_belief_invalidation_unit_test","dasall_reflection_engine_conservative_abort_unit_test"])`
   - 第一次结果：失败；局部编译错误为 `ReflectionEngine::analyze()` 误加 `const`，未真正 override `IReflectionEngine::analyze()`。
   - 修补同一 slice 后复跑：通过。
2. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_decision_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_belief_invalidation_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_reflection_engine_conservative_abort_unit_test`
   - 结果：通过；三条 reflection-focused unit tests 均零输出退出。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 范围控制 | PASS：只新增 reflection 私有组件、三条 focused tests 与最小 CMake 接线 |
| suggestion-only 边界 | PASS：输出始终为 shared `ReflectionDecision`，未引入 runtime scheduling 字段 |
| 正例覆盖 | PASS：覆盖 `RetryStep`、`Continue`、`Replan` |
| 保守降级 | PASS：覆盖高风险场景下的 `AbortSafe` |
| 契约自检 | PASS：三条 focused tests 均显式校验 `validate_reflection_decision_field_rules()` |
| ADR-007 一致性 | PASS：恢复 admission 与执行权仍保留在 Runtime/RecoveryManager |