# RT-TODO-007 IAgentFsm 与 StateTransitionTypes 设计收敛

## 1. 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.7.1 已冻结 Runtime FSM 的 17 个显式状态，要求等待态、失败安全态、审计态和持久化态都必须独立建模。
2. 同一文档的 6.7.4 给出了合法转移、guard 条件和 checkpoint 策略，要求非法转移必须返回 `TransitionRejectionReason`，且拒绝事件对遥测可见。
3. 同一文档的 6.24.6 将 `IAgentFsm` 收敛为纯状态事实与转移规则接口，禁止直接依赖 memory、cognition、tools 等业务端口。
4. 同一文档的 6.5.1 已冻结 `CheckpointState` 与 FSM 状态映射，因此 007 的 checkpoint hint 应直接消费 contracts 侧 `CheckpointState`，而不是自造第二套 checkpoint 状态枚举。
5. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 RT-TODO-007 明确限定为 `runtime/include/fsm/IAgentFsm.h` 与 `runtime/include/fsm/StateTransitionTypes.h` 的 public surface 收敛，验收出口为 `AgentFsmTest`。

## 2. 外部参考

1. Stately/XState 的 transitions 文档强调：状态机转移应由显式 event/guard 驱动，只有 guard 满足时才进入目标状态；否则应返回确定性的拒绝结果，而不是静默保持现状。

## 3. 设计结论

1. `RuntimeState` 固定公开 17 态枚举，并提供稳定的状态目录和名称函数，避免后续 `CheckpointStateMapper`、`TransitionGuardTable`、`AgentFsmTest` 各自复制状态表。
2. `StateTransitionRequest` 只承载本轮转移所需的最小事实：`from_state`、`requested_to`、`transition_reason` 和 `guard_facts`。它不直接携带 `SessionSnapshot`、`RecoveryOutcome`、`RuntimeStateSnapshot` 等相邻模块对象，防止 007 越权侵入 009/010/014 的实现面。
3. `TransitionGuardFact` 固定为可枚举的 guard 事实键，而不是散落的字符串常量。这样 013 的 `TransitionGuardTable` 与 014 的 `AgentFsm` 能共享同一套 guard 词汇，不再重复声明。
4. `TransitionRejectionReason` 最小字段固定为 `from_state`、`requested_to`、`violation_type`、`detail`，并允许附带 `violated_guard`，满足 6.7.4 对显式拒绝原因的要求。
5. `StateTransitionCheckpointHint` 采用“动作 + contracts::CheckpointState + pending_action_required”结构：
   - 动作域只表达 `None / Write / Update / Retain / ClearActiveReference`
   - checkpoint 状态直接复用 frozen 的 `contracts::CheckpointState`
   - 是否必须带 `pending_action` 由 hint 显式给出
6. `StateTransitionOutcome` 固定同时承载 accepted/rejected 两条出口：成功时给出 `resolved_state` 与 `checkpoint_hint`，失败时给出 `rejection_reason`，不允许隐式吞掉非法转移。
7. `IAgentFsm` 本轮只冻结接口，不提供实现：
   - `current_state()` 暴露当前显式状态
   - `can_enter(...)` 提供无副作用的快速准入判断
   - `transition(...)` 负责返回完整 `StateTransitionOutcome`
   - `is_terminal(...)` 保留终态判断口，具体规则在 014 落实现时再落到行为测试

## 4. 边界与职责

| 对象 | 本轮职责 | 明确不做 |
|---|---|---|
| `RuntimeState` | 公开 17 态运行状态 | 不承载 checkpoint 映射、不解释预算或恢复策略 |
| `StateTransitionRequest` | 表达一次显式转移请求 | 不直接装配 session/recovery/checkpoint 具体对象 |
| `TransitionRejectionReason` | 表达非法转移的结构化原因 | 不替代遥测桥或错误码域 |
| `StateTransitionCheckpointHint` | 提供 checkpoint 写入/保留/清理提示 | 不实际执行 save/load/validate |
| `IAgentFsm` | 定义状态机 public contract | 不实现 guard table，不持有外部依赖 |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 |
|---|---|
| 17 态 `RuntimeState` 与稳定名称函数 | `runtime/include/fsm/StateTransitionTypes.h` |
| request/outcome/rejection/checkpoint hint supporting types | `runtime/include/fsm/StateTransitionTypes.h` |
| `IAgentFsm` 最小 public interface | `runtime/include/fsm/IAgentFsm.h` |
| 正例：合法转移 surface；负例：非法转移 rejection surface | `tests/unit/runtime/AgentFsmTest.cpp` |
| 单测 discoverability 接线 | `tests/unit/runtime/CMakeLists.txt`、`tests/unit/CMakeLists.txt` |

## 6. Build 三件套

1. 代码目标：`runtime/include/fsm/IAgentFsm.h`、`runtime/include/fsm/StateTransitionTypes.h`
2. 测试目标：`tests/unit/runtime/AgentFsmTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R AgentFsmTest --output-on-failure`

## 7. D 原子项完成情况

| ID | 内容 | 结果 |
|---|---|---|
| D1 | 冻结 17 态枚举与 guard/rejection/checkpoint hint 边界 | PASS |
| D2 | 锁定 `IAgentFsm` 最小方法面 | PASS |
| D3 | 产出 Design -> Build 映射与 Build 三件套 | PASS |

## 8. D Gate

- Gate: PASS
- 进入 B 的条件：已满足
- 说明：本轮 public surface 不越权携带 session/checkpoint/recovery 具体对象，只冻结 014/012/013 所需的共同类型底座。