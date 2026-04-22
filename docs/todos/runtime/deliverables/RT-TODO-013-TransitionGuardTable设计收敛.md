# RT-TODO-013 TransitionGuardTable 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.7.4 已给出完整合法转移表，逐条冻结了：`From`、`To`、guard 条件、checkpoint 策略。
2. 同一节明确要求：
   - 任何不在表中的 from->to 组合必须被显式拒绝；
   - 拒绝时必须产出 `TransitionRejectionReason`；
   - 拒绝事件对遥测可见。
3. `runtime/include/fsm/StateTransitionTypes.h` 已冻结 17 态 `RuntimeState`、`TransitionGuardFact`、`StateTransitionCheckpointHint` 和 `StateTransitionRequest/Outcome`，说明 013 应该消费既有 public seam，而不是再造 guard 词汇或 checkpoint hint 类型。
4. `runtime/include/fsm/IAgentFsm.h` 已把 `can_enter(...)` 和 `transition(...)` 的职责留给 014；因此 013 只需要提供纯查询规则，不直接维护状态机实例。
5. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 013 的代码目标限定为 `runtime/src/fsm/TransitionGuardTable.cpp`，测试出口限定为 `TransitionGuardTableTest`。

## 外部参考

1. XState/Stately 的 guards 文档强调：guard 应保持纯、同步、可复用；当一条转移存在多组备选 guard 时，应该显式表达 guard 分支，而不是把条件散落到状态机实现里。这支持 DASALL 把 6.7.4 的表格实现为独立规则表，并为 `Reflecting -> Reasoning`、`Reflecting -> FailedSafe` 这类 OR 守卫提供结构化表达。

## 设计结论

1. `TransitionGuardTable` 作为 runtime 内部规则表实现，不进入 `runtime/include`，而是落在：
   - `runtime/src/fsm/TransitionGuardTable.h`
   - `runtime/src/fsm/TransitionGuardTable.cpp`
2. 规则表使用私有 `TransitionGuard` / `TransitionRule` 结构表达：
   - `all_of`：必须全部满足的 guard facts
   - `any_of`：至少满足一个的 guard facts
   - `checkpoint_strategy`：直接复用 `StateTransitionCheckpointHint`
3. 对 6.7.4 中的 guard 条件，013 采用以下编码规则：
   - `Idle -> Receiving` 使用 `AgentRequestAvailable + FacadeInitialized`
   - `WaitingClarify -> Receiving` 复用 `AgentRequestAvailable`，表示新的用户澄清输入已到达
   - `Reflecting -> Reasoning` 使用 `any_of = {ReflectionContinue, RecoveryRetryAllowed, RecoveryReplanAllowed}`
   - `Reflecting -> FailedSafe` 使用 `any_of = {RecoveryAbortSafe, RecoveryDegrade}`
   - `FailedSafe -> Responding` 视为进入 `FailedSafe` 后的无条件合法边，guard 为空，checkpoint 策略为 `Retain(Failed)`
   - 其余边按 6.7.4 的表格做 `all_of` 编码
4. `TransitionGuardTable` 暴露的最小私有查询面固定为：
   - `is_legal(from_state, to_state)`：是否存在合法边
   - `get_guard(from_state, to_state)`：返回 guard 规则
   - `get_checkpoint_strategy(from_state, to_state)`：返回对应 checkpoint hint
5. 013 不负责：
   - 执行 `StateTransitionRequest` 的 guard 校验
   - 生成 `StateTransitionOutcome`
   - 维护当前状态或终态判断
   这些职责仍留给 014 的 `AgentFsm`。
6. 为避免 public ABI 漂移，013 不新增 `TransitionGuardFact` 枚举项；必要的语义折中通过 module-local 规则解释完成。

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `TransitionGuardTable::is_legal` | 判断 from->to 是否存在于 6.7.4 的合法边集合 | 不检查请求是否已满足 guard |
| `TransitionGuardTable::get_guard` | 返回合法边的 guard 规则 | 不生成拒绝结果 |
| `TransitionGuardTable::get_checkpoint_strategy` | 返回合法边的 checkpoint 策略 | 不实际执行 checkpoint 写入 |
| `TransitionGuardTableTest` | 覆盖全部合法边与非法边二值判断 | 不替代 014 的行为测试 |

## 文件落点

| 设计项 | 文件 |
|---|---|
| 规则表私有声明 | `runtime/src/fsm/TransitionGuardTable.h` |
| 规则表实现 | `runtime/src/fsm/TransitionGuardTable.cpp` |
| 单测 | `tests/unit/runtime/TransitionGuardTableTest.cpp` |
| CMake 注册 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt`、`tests/unit/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/fsm/TransitionGuardTable.h`、`runtime/src/fsm/TransitionGuardTable.cpp`
2. 测试目标：`tests/unit/runtime/TransitionGuardTableTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_transition_guard_table_unit_test && ctest --test-dir build-ci -R "^TransitionGuardTableTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 锁定 6.7.4 全量合法边集合 | 每条合法边都能查到规则 | PASS |
| D2 | 锁定 AND/OR guard 编码方式 | `Reflecting` 相关分支不需要在 014 里重复手写 | PASS |
| D3 | 锁定 checkpoint strategy 查询口 | 每条合法边都能返回稳定 hint | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. Design -> Build 映射完整。
3. Build 三件套已锁定。
4. 范围未越过 013 的规则表任务边界。

结论：D Gate = PASS，可进入 RT-TODO-013 的 Build 阶段。