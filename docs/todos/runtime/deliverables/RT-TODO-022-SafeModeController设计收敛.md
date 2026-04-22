# RT-TODO-022 SafeModeController 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 6.21.1 已把触发源固定为 `PolicyForbidden`、`BudgetExhausted`、`RecoveryExhausted`、`DependencyUnavailable`、`WatchdogTimeout` 五类。
2. 同文档 6.21.2 已要求 `SafeModeController` 消费 `degrade_policy.fallback_chain`，按声明式降级链而不是硬编码规则做裁定。
3. 同文档 6.24.11 已把 `SafeModeController` 定位为 runtime-private 控制器：只统一收敛进入/退出条件与模式状态，不持有业务主循环。
4. `profiles/include/RuntimePolicySnapshot.h` 已公开 `degrade_policy.fallback_chain`、`allow_model_failover`、`allow_budget_degrade` 与 `execution_policy.safe_mode_enabled`，足够支撑 022 的最小裁定逻辑。
5. `runtime/include/budget/BudgetDecision.h` 与 `contracts/include/checkpoint/RecoveryOutcome.h` 已分别提供预算拒绝和恢复执行结果事实，022 无需新增 shared contracts。

## 外部参考

1. Resilience4j CircuitBreaker 文档强调：状态切换和事件记录应与业务调用解耦，状态机本身保持轻量且线程安全，慢调用/失败率达到阈值时才切换到拒绝或半开状态。本任务借鉴的是“控制器只做状态裁定，不把业务执行包进临界区”的原则。

## 设计结论

1. 022 只新增 runtime-private 组件：
   - `runtime/src/safety/SafeModeController.h`
   - `runtime/src/safety/SafeModeController.cpp`
2. 不新增 public ABI，不修改 contracts，不让 `SafeModeController` 反向驱动 `AgentOrchestrator` 主循环。
3. `SafeModeController` 的最小职责：
   - 接收 `BudgetDecision`、`RecoveryOutcome`、health/watchdog 事实；
   - 根据 `RuntimePolicySnapshot::degrade_policy.fallback_chain` 决定进入 `FailedSafe`、`Degraded` 或 `SafeMode`；
   - 维护当前安全模式状态，并给出 `evaluate_exit(...)` 的回归 Normal 判定。

## 边界与职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `SafeModeController::evaluate_entry` | 折叠触发源到 `FailedSafe/Degraded/SafeMode` | 不推进 FSM，不落 checkpoint，不发 telemetry |
| `SafeModeController::evaluate_exit` | 基于 operator/health/budget 恢复条件决定是否退回 Normal | 不决定恢复动作，不做 retry/replan |
| `SafeModeController::current_mode` | 输出线程安全的当前模式快照 | 不暴露复杂状态机 DSL |

## 数据与接口说明

1. `SafeModeTriggerKind`：严格对应 6.21.1 的五类触发源。
2. `SafeModeTrigger`：最小接收面，仅复用现有 `BudgetDecision`、`RecoveryOutcome` 与 health/watchdog 事实。
3. `SafeModeDecision`：输出目标 mode、可选的 target runtime state、错误码与选中的 fallback step。
4. `SafeModeExitRequest`：最小退出条件集合，覆盖 dependencies、watchdog、operator clear 和 budget restore 四个维度。

## 流程

1. `PolicyForbidden` / `WatchdogTimeout`：直接进入 `SafeMode`。
2. `BudgetExhausted`：若 `fallback_chain` 允许并启用了 `allow_budget_degrade`，进入 `Degraded`；否则进入 `FailedSafe`。
3. `DependencyUnavailable`：优先尝试 `allow_model_failover` / `allow_tool_skip` 等链上可用 fallback；若只剩 `abort_safe` 或无可用 fallback，则进入 `FailedSafe`。
4. `RecoveryExhausted`：若 `RecoveryOutcome` 已执行 `degrade`，进入 `Degraded`；若是 `abort_safe` 或无法继续，进入 `FailedSafe`。
5. `evaluate_exit(...)`：仅在当前模式非 Normal 且四类恢复条件同时满足时退回 Normal。

## 文件范围

| 设计项 | 文件 |
|---|---|
| SafeModeController 私有实现 | `runtime/src/safety/SafeModeController.h`、`runtime/src/safety/SafeModeController.cpp` |
| 单元测试 | `tests/unit/runtime/SafeModeControllerTest.cpp` |
| 构建接线 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/safety/SafeModeController.h`、`runtime/src/safety/SafeModeController.cpp`
2. 测试目标：`tests/unit/runtime/SafeModeControllerTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_safe_mode_controller_unit_test && ctest --test-dir build-ci -R "^SafeModeControllerTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 固定 5 类触发源与模式输出 | `SafeModeTriggerKind`/`SafeModeDecision` 足以表达进入/退出裁定 | PASS |
| D2 | 锁定声明式降级链行为 | 控制器只从 `fallback_chain` 读取降级顺序 | PASS |
| D3 | 锁定最小退出条件 | `evaluate_exit(...)` 有可二值判定的恢复条件 | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. Build 三件套已锁定。
3. 022 保持 runtime-private，不扩 shared ABI。

结论：D Gate = PASS，可进入 RT-TODO-022 Build。