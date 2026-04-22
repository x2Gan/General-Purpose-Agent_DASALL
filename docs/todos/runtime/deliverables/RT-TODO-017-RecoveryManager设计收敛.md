# RT-TODO-017 RecoveryManager 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.8 已把 `RecoveryManager` 固定为“恢复动作准入与执行控制者”，输入必须组合 `ReflectionDecision`、`Observation`、`ErrorInfo`、`Checkpoint`、`BudgetSnapshot` 与幂等性事实。
2. 同文档的 6.17.2 与 RT-C020 进一步约束：
   - retry_step 必须复用原 `retry_idempotency_token`
   - replan 必须生成新 token
3. `runtime/include/recovery/IRecoveryManager.h` 已冻结 `evaluate/execute/apply` 三个 public 方法，以及 `RecoveryExecutionPlan` / `RecoveryApplyResult` supporting types；017 应只实现私有控制器，不再扩 public seam。
4. `tests/unit/runtime/RecoveryManagerTest.cpp` 当前 fake 已覆盖 admit / reject / escalate 三态，是 017 的最小行为蓝图。
5. 015 已落地 `BudgetSnapshot` 真实语义，016 已落地 `CheckpointManager::make_resume_plan()`；017 应直接复用这两个已完成控制器，而不是再写第二套 budget 或 checkpoint compatibility 规则。

## 设计结论

1. `RecoveryManager` 作为 runtime 私有控制器落在：
   - `runtime/src/recovery/RecoveryManager.h`
   - `runtime/src/recovery/RecoveryManager.cpp`
2. 控制器内部维护：
   - `CheckpointManager checkpoint_manager_`，统一消费 016 的 resume compatibility gate
   - `mutable std::mutex recovery_mutex_`，保护最近一次 `RecoveryOutcome` 与 evaluate 产生的最小执行上下文
3. `evaluate(const RecoveryRequest&)` 的固定顺序为：
   - 先调用 `validate_recovery_request_field_rules(...)` 校验嵌套证据
   - 再读取 `ReflectionDecisionKind`
   - 再根据 `BudgetSnapshot` 和 `IdempotencyAndSideEffectReport` 做 admit / reject / escalate 判定
   - retry/continue 路径复用 `CheckpointManager::make_resume_plan()`
   - replan 路径生成新 token，并保留旧 token 只作审计来源
4. admit / reject / escalate 的最小策略为：
   - `RetryStep`：仅在 replay-safe 且 checkpoint 可恢复时 admit
   - `Replan`：仅在 replan budget 未耗尽时 admit，并生成新 token
   - `AbortSafe`：直接 escalate 为 safe-failure hint
   - budget exhaustion：优先 escalate 到 degraded，而不是静默继续 retry/replan
5. `execute(const RecoveryExecutionPlan&)` 的最小动作映射为：
   - `retry_step` -> `RecoveryOutcome.executed_action=retry_step`，最终状态回到 `Reasoning`
   - `replan` -> `executed_action=replan`，最终状态进入 `Planning`
   - safe escalation -> `executed_action=abort_safe`，最终状态进入 `FailedSafe`
   - degraded escalation -> `executed_action=degrade`，最终状态进入 `Degraded`
6. `apply(const RecoveryOutcome&)` 的最小语义为：
   - 先调用 `validate_recovery_outcome_field_rules(...)`
   - 保存最近一次 outcome
   - `retry_step/replan` 视为成功应用
   - `abort_safe` 返回 `RT_E_510_SAFE_MODE_ENTERED`
   - `degrade` 返回 `RT_E_511_DEGRADE_ENTERED`
7. 017 不负责：
   - 真实工具重发、planner 重规划或补偿执行接线
   - Checkpoint 写回 sidecar 更新
   - SafeModeController 最终模式裁定

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `RecoveryManager::evaluate` | 恢复准入与计划生成 | 不直接执行工具或 planner |
| `RecoveryManager::execute` | 把 admit/reject/escalate 计划投影为 `RecoveryOutcome` | 不写审计/telemetry |
| `RecoveryManager::apply` | 吸收 `RecoveryOutcome` 并返回最小应用结果 | 不推进 FSM / SafeModeController |

## 文件落点

| 设计项 | 文件 |
|---|---|
| 私有类声明 | `runtime/src/recovery/RecoveryManager.h` |
| 私有类实现 | `runtime/src/recovery/RecoveryManager.cpp` |
| 行为测试 | `tests/unit/runtime/RecoveryManagerTest.cpp` |
| CMake 接线 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/recovery/RecoveryManager.h`、`runtime/src/recovery/RecoveryManager.cpp`
2. 测试目标：`tests/unit/runtime/RecoveryManagerTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_recovery_manager_unit_test && ctest --test-dir build-ci -R "^RecoveryManagerTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 锁定 admit/reject/escalate 三态 | `ReflectionDecision` 与 runtime constraints 真正汇合 | PASS |
| D2 | 锁定 retry/replan token 语义 | retry 复用旧 token，replan 生成新 token | PASS |
| D3 | 锁定 015/016 复用关系 | budget/ checkpoint 规则不重复实现 | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. 015/016/017 的职责边界已串清。
3. Build 三件套已锁定。
4. 范围未越出 017 控制器边界。

结论：D Gate = PASS，可进入 RT-TODO-017 的 Build 阶段。