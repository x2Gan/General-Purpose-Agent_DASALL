# COG-TODO-034 Runtime 反思决策与错误面收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Design + Build runtime 反思/错误面补强

## 1. 本地证据

1. `runtime/src/AgentOrchestrator.cpp` 的 live unary tool round 在拿到 tool observation 后会调用 `cognition_engine->reflect(...)`，但当前返回值被 `(void)` 直接丢弃。
2. 同一文件后续 recovery 分支只消费 `stub_ports.recovery_exit` 生成的本地 `RecoveryRequest`，没有把真实 `CognitionReflectionResult.reflection_decision` 接到 `RecoveryManager`。
3. `runtime/src/recovery/RecoveryManager.cpp` 已经能解释 `Continue`、`RetryStep`、`Replan`、`AbortSafe`，并产出 `resume_plan`、`Planning` state 或 safe-mode/degrade escalation，因此 034 的缺口不是 recovery owner 本身，而是 runtime 没把 cognition 结果接入。
4. `contracts/include/checkpoint/RecoveryRequestGuards.h` 明确要求 recovery admission 必须基于失败 observation，且当 `latest_observation.source=ToolExecution` 时，`error_info.source_ref.ref_type` 必须是 `tool_call` 且 `ref_id` 必须等于 `tool_call_id`。
5. `runtime/src/AgentOrchestrator.cpp` 当前只校验 `action_decision` 是否为 `ExecuteAction`，没有显式拒绝 “`error_info/result_code` 与 executable action 并存” 的 cognition 结果。
6. `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp` 目前只覆盖 executable / non-executable contract 和 033 的 belief/context consumption，还没有验证 reflection decision 或 decision error/action conflict。

## 2. 边界与职责收敛

1. `ReflectionDecision` 继续是 cognition suggestion-only object；runtime 不在 034 中新增 reflection public surface，只把它映射到现有 `RecoveryRequest` admission。
2. `RecoveryManager` 继续拥有 recovery admission/execute/apply 权；`AgentOrchestrator` 只负责把 cognition suggestion 转成合规 `RecoveryRequest`，再解释 outcome 如何进入现有 FSM。
3. `Continue/RetryStep` 继续通过 checkpoint/resume machinery 重走已有恢复入口，不在 runtime 主链里手写第二套 replay 逻辑。
4. `Replan` 继续表示“回到 planning 再继续完成本轮”，034 只允许 runtime 借助现有 `continue_from_checkpoint()` 补一个 `Planning` 分支，不引入新的 orchestrator entrypoint。
5. cognition 错误面遵循 fail-closed：`error_info` 或 `result_code` 一旦与 executable action / reflection suggestion 并存，runtime 必须优先拒绝，不得继续执行 tool round 或 recovery。

## 3. 数据与接口说明

### 3.1 输入与输出

| 接口 / 数据 | 方向 | 本轮约束 |
|---|---|---|
| `cognition::CognitionDecisionResult` | cognition -> runtime | executable action 与 `error_info/result_code` 互斥；冲突时 fail-closed |
| `cognition::CognitionReflectionResult` | cognition -> runtime | `reflection_decision` 经 runtime 投影为 `RecoveryRequest`；反思错误优先于 suggestion |
| `contracts::RecoveryRequest` | runtime -> recovery | 必须使用失败 observation + matching `error_info.source_ref`，不能直接复用成功 tool observation |
| `RecoveryOutcome` / `ResumePlan` | recovery -> runtime | `Continue/RetryStep` 进入已有 resume path；`Replan` 进入 planning path；`AbortSafe` 进入 safe/degraded terminal path |

### 3.2 目标文件范围

1. `runtime/src/AgentOrchestrator.cpp`
2. `tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp`
3. `tests/integration/cognition/CognitionFailureInjectionIntegrationTest.cpp`
4. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md`
5. `docs/worklog/DASALL_开发执行记录.md`

## 4. 流程与时序

### 4.1 decision error/action 冲突

1. runtime 在 `decide()` 后首先校验 cognition result。
2. 如果 `error_info` 或 `result_code` 存在，同时 `action_decision` 是 `ExecuteAction`，runtime 立即 fail-closed，拒绝继续 tool round。
3. 错误必须定位到 `main_loop`，保证追踪上能区分“cognition fail-closed”与“tool round 失败”。

### 4.2 reflection decision -> recovery admission

1. tool round 成功得到 observation 后，runtime 调用 `reflect()` 并捕获 `CognitionReflectionResult`。
2. 若 reflection result 自带 `error_info/result_code`，runtime 直接在 `recovery_round` fail-closed，不再解释 suggestion。
3. 若存在 `reflection_decision`，runtime 生成合规 `RecoveryRequest`：
   - `latest_observation` 改为 runtime 合成的失败 observation；
   - `error_info` 与该失败 observation 保持镜像一致；
   - `idempotency_and_side_effect_report` 来自当前 tool round 的 idempotency key / side-effect evidence。
4. runtime 调用 `RecoveryManager::evaluate/execute/apply`，再根据 executed action 进入现有分支：
   - `Continue/RetryStep`：沿 `continue_from_checkpoint()` 重走 resume path；
   - `Replan`：经 `continue_from_checkpoint()` 的 `Planning -> Reasoning -> Responding` 分支继续；
   - `AbortSafe` / degrade：沿现有 safe terminal branch 进入 `FailedSafe/Degraded/SafeMode`。

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| reflection result 不能再被丢弃 | `AgentOrchestrator.cpp` tool round | integration 可观察到 abort/retry/replan 行为差异 |
| recovery request 必须满足 tool-call source_ref guard | reflection recovery helper | `RecoveryManager` admission 不因 source_ref 不匹配被拒绝 |
| executable action 与 cognition error 互斥 | main loop conflict guard | error+execute 冲突 fail-closed |
| replan 不新增新入口 | `continue_from_checkpoint()` 规划分支 | `ReflectionDecisionKind::Replan` 能进入 planning path 并完成本轮 |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 在 `AgentOrchestrator` 增加 decision/result conflict guard 与 reflection recovery helper | interaction / failure injection 覆盖 error+execute 与 reflection error priority | `Build_CMakeTools(buildTargets=["dasall_cognition_runtime_interaction_contract_integration_test","dasall_cognition_failure_injection_integration_test"])` | 若 reflection helper 不满足 guard，优先修正 synthetic error/observation，不绕过 RecoveryManager |
| B2 | 将 `Continue/RetryStep/Replan/AbortSafe` 映射到现有 recovery/continue path | interaction contract 覆盖 abort/retry/replan deterministic terminal behavior | `RunCtest_CMakeTools(tests=["CognitionRuntimeInteractionContractTest","CognitionFailureInjectionTest","ReflectionDecisionContractTest"])` | 若 replan 缺恢复入口，仅补 `continue_from_checkpoint()` 的 `Planning` 分支，不新增 orchestrator API |

## 7. D Gate

Gate = PASS。

进入 Build 的依据已经充分：owner 已锁定在 `AgentOrchestrator`；`RecoveryManager` 与 checkpoint/resume path 已存在；关键 guard 也已明确。034 只补接线与 fail-closed 冲突校验，不扩 shared contracts。

## 8. Build 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_cognition_runtime_interaction_contract_integration_test","dasall_cognition_failure_injection_integration_test"])`
   - 结果：首轮失败只暴露 034 本地 safe-path 接线字段错误；修正后通过。
2. `RunCtest_CMakeTools(tests=["CognitionRuntimeInteractionContractTest","CognitionFailureInjectionIntegrationTest","ReflectionDecisionContractTest"])`
   - 结果：通过；`CognitionRuntimeInteractionContractTest` 额外验证 Continue / RetryStep / Replan / AbortSafe 四类 reflection decision，`CognitionFailureInjectionIntegrationTest` 验证 executable+error conflict 与 reflection error priority。
3. `get_errors(filePaths=[runtime/src/AgentOrchestrator.cpp, tests/integration/cognition/CognitionRuntimeInteractionContractTest.cpp, tests/integration/cognition/CognitionFailureInjectionIntegrationTest.cpp])`
   - 结果：无新增编辑器错误。
4. 关键实现校准：为满足 `RecoveryRequest` guards，runtime 合成的 reflection failure observation 额外归一化了 `payload`、`duration_ms`、`worker_task_id`，并在 Continue / RetryStep 路径上把 retry idempotency evidence 回填为 `idempotency_key ?? tool_call_id`，避免 recovery admission 伪失败。

## 9. 完成判定

1. 已完成：`AgentOrchestrator` 不再丢弃 `reflect()` 结果；真实 `reflection_decision` 现在会经 `RecoveryRequest -> RecoveryManager::evaluate/execute/apply` 进入 runtime recovery owner。
2. 已完成：`Continue` / `RetryStep` 会沿已有 resume path 继续，`Replan` 会经 `continue_from_checkpoint()` 的 `Planning -> Reasoning -> Responding` 分支回到 planning path，`AbortSafe` 会进入既有 safe terminal path。
3. 已完成：decision path 上 executable action 与 `error_info/result_code` 并存时立即 fail-closed；reflection path 上若 cognition 自带 error surface，则优先失败，不再解释 suggestion。
4. 已完成：interaction / failure injection / contract 三类聚焦验证全部通过，说明 034 的 reflection 分支、冲突 fail-closed 与 error priority 都已有自动化证据。