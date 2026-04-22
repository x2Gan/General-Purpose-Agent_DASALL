# RT-TODO-015 BudgetController 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.7 已明确 `BudgetController` 的职责是：管理 `RuntimeBudget` 五维上限、维护 `BudgetSnapshot`，并输出预算准入与超限原因。
2. 同一节把最少扣减点固定为：
   - 每轮 reasoning 完成后扣减 `max_turns`
   - 每次工具调用前预扣 `max_tool_calls`
   - 每次进入 replan 路径前扣减 `max_replan_count`
   - 每轮使用 wall-clock 更新 `max_latency_ms` 快照
3. `runtime/include/budget/BudgetDecision.h` 已冻结 `BudgetInitializeRequest`、`BudgetConsumeRequest`、`BudgetDecision`、`BudgetViolationClass`，说明 015 应只实现控制器，不扩 public 类型。
4. `contracts/include/checkpoint/RuntimeBudgetGuards.h` 已提供 `validate_runtime_budget(...)`，说明 015 的初始化校验应直接复用现有 contract guard，而不是自行复制字段校验。
5. `tests/unit/runtime/BudgetControllerTest.cpp` 目前仍是 008 的 fake 行为样例；015 需要把它升级为真实 `BudgetController` 行为测试，覆盖五维预算与 snapshot 一致性。

## 设计结论

1. `BudgetController` 作为 runtime 私有控制器落在：
   - `runtime/src/budget/BudgetController.h`
   - `runtime/src/budget/BudgetController.cpp`
2. 控制器内部只维护三类状态，并统一由 `mutable std::mutex budget_mutex_` 保护：
   - `initialized_`
   - `runtime_budget_`
   - `snapshot_`
   - `started_at_ms_`
3. 初始化规则固定为：
   - `initialize(...)` 先调用 `validate_runtime_budget(...)`
   - 校验通过后构建固定顺序的五维 snapshot：`Token / Turn / ToolCall / Latency / Replan`
   - 每个 entry 的 `current=0`、`remaining=max-current`
4. `consume(...)` 的维度语义固定为：
   - `Token` / `Turn` / `ToolCall` / `Replan`：`current += amount`
   - `Latency`：`current = observed_at_ms - started_at_ms_`，忽略 `amount` 的累加语义
   - 每次 consume 后都刷新 `snapshot_at_ms`
5. `can_*` 系列接口共享同一份 snapshot，但检查不同维度：
   - `can_continue()`：检查 `Token + Turn + Latency`
   - `can_replan()`：先满足 `can_continue()`，再检查 `Replan`
   - `can_call_tool()`：先满足 `can_continue()`，再检查 `ToolCall`
6. 预算拒绝策略固定为：
   - 未初始化：`SnapshotUnavailable`
   - 初始化配置非法：`ConfigurationInvalid`
   - 维度超限：返回对应 `*Exhausted` violation 和 `RT_E_3xx` 映射
   - `overall_reject_reason` 记录最近一次生效的拒绝原因
7. 015 不负责：
   - 执行降级或恢复动作
   - 解释 profile 热更新中途是否切换预算
   - 产生 `TransitionRejectionReason`
   这些职责仍留给 orchestrator / recovery / safe mode 路径。

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `BudgetController::initialize` | 校验 `RuntimeBudget` 并构建五维 snapshot | 不读取 profile 热更新 |
| `BudgetController::consume` | 更新指定预算维度与 snapshot | 不触发降级或恢复动作 |
| `BudgetController::snapshot` | 返回当前预算快照副本 | 不暴露可变内部引用 |
| `BudgetController::can_continue/can_replan/can_call_tool` | 输出准入或拒绝决定 | 不改变预算状态 |

## 文件落点

| 设计项 | 文件 |
|---|---|
| 私有类声明 | `runtime/src/budget/BudgetController.h` |
| 私有类实现 | `runtime/src/budget/BudgetController.cpp` |
| 行为测试 | `tests/unit/runtime/BudgetControllerTest.cpp` |
| CMake 接线 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/budget/BudgetController.h`、`runtime/src/budget/BudgetController.cpp`
2. 测试目标：`tests/unit/runtime/BudgetControllerTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_budget_controller_unit_test && ctest --test-dir build-ci -R "^BudgetControllerTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 锁定五维 snapshot 内部状态 | 不扩 public type | PASS |
| D2 | 锁定 latency 的 wall-clock 语义 | `max_latency_ms` 不被误实现为加法计数器 | PASS |
| D3 | 锁定 `can_*` 的维度子集 | 工具调用与 replan 准入不重复抄逻辑 | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. 五维预算语义与 `can_*` 判定已明确。
3. Build 三件套已锁定。
4. 范围未越出 015 控制器边界。

结论：D Gate = PASS，可进入 RT-TODO-015 的 Build 阶段。