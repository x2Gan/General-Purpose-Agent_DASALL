# RT-TODO-008 IBudgetController 与 BudgetDecision 设计收敛

## 1. 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.10 明确 Runtime 只消费 `RuntimePolicySnapshot.runtime_budget`，不新增 profile schema，也不重新解释 `RuntimeBudget` 五维语义。
2. 同一文档的 6.24.7 已将 BudgetController 收敛为“只做预算事实判断”的组件，输入固定为 `RuntimeBudget`、`BudgetConsumeRequest`、当前 `BudgetSnapshot`，输出固定为 `BudgetDecision` 与 `BudgetSnapshot`。
3. `contracts/include/checkpoint/RuntimeBudget.h` 已冻结 token/turn/tool_call/latency/replan 五维预算；`contracts/include/checkpoint/BudgetSnapshot.h` 已冻结唯一的预算快照表达，runtime 不应再引入第二套 snapshot 类型。
4. `docs/todos/contracts/deliverables/WP02-T007-RuntimeBudget字段清单.md` 与 WP02-T008 的 BudgetSnapshot 规则已经明确：`RuntimeBudget` 只表达上限，`BudgetSnapshot` 只表达 current/max/remaining/reject_reason，后续子域必须复用而不是扩展顶层预算维度。
5. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 RT-TODO-008 限定为 `runtime/include/budget/IBudgetController.h` 与 `runtime/include/budget/BudgetDecision.h` 的 public surface 收敛，验收出口为 `BudgetControllerTest`。

## 2. 外部参考

1. AWS Builders' Library《Timeouts, retries, and backoff with jitter》指出 timeout 和 retry 预算本质上是防止资源被长时间占用、避免放大下游过载的保护机制，重试必须限额且最好只在单层做控制；这支持 Runtime 将预算控制收敛为独立控制器而不是分散在调用链各层。
2. gRPC Deadlines 指南强调 deadline 应显式设置并向下游传播，超时后调用方与服务端都应停止继续占用资源；这支持 BudgetController 对 latency 维度只输出事实判断，而把实际取消与恢复留给其他控制器。

## 3. 设计结论

1. `BudgetDecision.h` 直接复用 contracts 的 `RuntimeBudget`、`BudgetSnapshot` 与 `BudgetType`，不自造第二套 budget dimension 或 snapshot schema。
2. `BudgetInitializeRequest` 只承载运行期 budget 基线和初始化时间戳；它不复制 `RuntimePolicySnapshot` 全对象，避免 008 越权进入 profile 投影逻辑。
3. `BudgetConsumeRequest` 固定以 `BudgetType + amount + observed_at_ms + detail` 表达一次预算扣减事实：
   - `amount` 对 turn/tool_call/replan/token 表示增量数量
   - 对 latency 表示当前观测到的 wall-clock 使用量或推进量
4. `BudgetViolationClass` 固定覆盖五维超限和两类系统性拒绝：
   - `ConfigurationInvalid`
   - `SnapshotUnavailable`
   - `TokenExhausted / TurnExhausted / ToolCallExhausted / LatencyExhausted / ReplanExhausted`
5. `BudgetDecision` 固定同时暴露：是否允许、当前评估的 `BudgetType`、`BudgetViolationClass`、对齐 006 的 `RuntimeErrorCode`、以及结构化 detail。这样 015/017/023 后续可直接消费统一拒绝面。
6. `IBudgetController` 保持纯预算事实边界：
   - `initialize(...)` 建立初始预算快照
   - `consume(...)` 记录预算消耗并输出本次判断
   - `snapshot()` 返回 frozen `BudgetSnapshot`
   - `can_continue()`、`can_replan()`、`can_call_tool()` 只输出预算准入结论，不直接执行取消、恢复、降级
7. Token 维度不额外发明 `can_consume_tokens()`；统一通过 `consume()` 与 `snapshot()` 表达，避免 008 提前扩张出 015 之外的新接口面。

## 4. 边界与职责

| 对象 | 本轮职责 | 明确不做 |
|---|---|---|
| `BudgetInitializeRequest` | 固定初始化输入 | 不复制完整 profile snapshot |
| `BudgetConsumeRequest` | 固定扣减事实表达 | 不承担恢复或取消动作 |
| `BudgetDecision` | 暴露预算准入和拒绝原因 | 不执行降级/重试/恢复 |
| `BudgetViolationClass` | 固定预算拒绝分类 | 不替代 telemetry/event bridge |
| `IBudgetController` | 定义预算控制 public contract | 不实现真正扣减逻辑，不持有 orchestrator 依赖 |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 |
|---|---|
| request/decision/violation supporting types 与 error code 映射 | `runtime/include/budget/BudgetDecision.h` |
| `IBudgetController` 最小 public interface | `runtime/include/budget/IBudgetController.h` |
| 正例：预算初始化/准入；负例：tool-call 超限 rejection | `tests/unit/runtime/BudgetControllerTest.cpp` |
| 单测 discoverability 接线 | `tests/unit/runtime/CMakeLists.txt`、`tests/unit/CMakeLists.txt` |

## 6. Build 三件套

1. 代码目标：`runtime/include/budget/IBudgetController.h`、`runtime/include/budget/BudgetDecision.h`
2. 测试目标：`tests/unit/runtime/BudgetControllerTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_budget_controller_unit_test && ctest --test-dir build-ci -R BudgetControllerTest --output-on-failure`

## 7. D 原子项完成情况

| ID | 内容 | 结果 |
|---|---|---|
| D1 | 锁定 RuntimeBudget/BudgetSnapshot 复用边界 | PASS |
| D2 | 锁定 `IBudgetController` 最小方法面 | PASS |
| D3 | 锁定超限分类、错误码映射和 Build 三件套 | PASS |

## 8. D Gate

- Gate: PASS
- 进入 B 的条件：已满足
- 说明：本轮只冻结预算 public surface，不越权实现预算扣减算法或恢复策略。