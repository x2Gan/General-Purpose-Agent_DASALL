# RT-TODO-014 AgentFsm 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.6 已把 `AgentFSM` 的核心职责限定为：维护显式状态机、守卫校验、输出 `StateTransitionOutcome` / `TransitionRejectionReason` / checkpoint hint，且不得持有 memory/cognition/tools 等业务依赖。
2. 同一文档的 6.7.3 和 6.7.4 已冻结主状态迁移图和 guard/checkpoint 表，说明 014 的实现必须消费 013 的 `TransitionGuardTable`，而不是再写第二份状态表。
3. 同一文档的 6.14.2 明确声明锁顺序中 `L1: AgentFSM::state_mutex` 排第一，说明 014 必须把当前状态保护在独立互斥量后面，以满足后续 orchestrator / session / checkpoint 组合时的锁序约束。
4. `tests/unit/runtime/AgentFsmTest.cpp` 目前仍是 007 的 fake surface test，仅证明接口形状存在；014 需要把它替换为真实 `AgentFsm` 行为测试。
5. `runtime/src/fsm/TransitionGuardTable.h` / `.cpp` 已在 013 落地合法边、guard 规则和 checkpoint strategy，014 已具备直接复用的 module-local 前置依赖。

## 外部参考

1. XState/Stately 的 guards 实践强调：状态机实现应先解析显式 guard 规则，再给出确定性的 transition / rejection 结果。对应到 DASALL，`AgentFsm` 应把“是否合法边”和“缺哪个 guard”从状态突变逻辑中分离出来，先查询规则表，再决定是否推进状态。

## 设计结论

1. `AgentFsm` 作为 runtime 私有实现落在：
   - `runtime/src/fsm/AgentFsm.h`
   - `runtime/src/fsm/AgentFsm.cpp`
   它实现 public interface `IAgentFsm`，但自身不进入 `runtime/include`。
2. `AgentFsm` 的唯一内部状态是 `current_state_`，并由 `mutable std::mutex state_mutex_` 保护：
   - `current_state()` 走 copy-on-read；
   - `can_enter(...)` 在持锁读取当前状态后做纯判定；
   - `transition(...)` 在同一把锁下完成校验、状态推进和 outcome 组装。
3. 014 的判定顺序固定为：
   - 先校验 `request.from_state == current_state_`；不满足则返回 `SourceStateMismatch`
   - 再校验当前状态是否为终态且请求试图离开；若是则返回 `TerminalStateExit`
   - 再查询 `TransitionGuardTable::is_legal(...)`；非法边返回 `IllegalTransition`
   - 再查询 `get_guard(...)` 并检查 guard 是否满足；缺 guard 返回 `MissingGuardFact`
   - 最后查询 `get_checkpoint_strategy(...)`，推进状态并返回 accepted outcome
4. `is_terminal(state)` 在 014 的真实规则中只将 `SafeMode` 视为终态：
   - `Completed` 不是终态，因为 6.7.4 显式存在 `Completed -> Idle`
   - `Failed` / `Degraded` 不是终态，因为仍有恢复或降级收敛路径
5. rejection detail 采用最小结构化文案策略：
   - `SourceStateMismatch`：报告 request.from 与当前状态不一致
   - `IllegalTransition`：报告 from->to 不在 6.7.4
   - `MissingGuardFact`：报告缺失的 guard fact，并回填 `violated_guard`
   - `TerminalStateExit`：报告终态不可离开
6. 014 不新增 public type，不修改 `IAgentFsm` surface；只把 007/013 已冻结的 seam 串成真实实现。

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `AgentFsm::current_state` | 返回受互斥量保护的当前状态快照 | 不暴露内部锁或可变引用 |
| `AgentFsm::can_enter` | 做无副作用的准入判断 | 不写状态、不产出完整 rejection 对象 |
| `AgentFsm::transition` | 组装 accepted/rejected outcome 并在成功时推进状态 | 不直接发遥测、不调用其他控制器 |
| `AgentFsm::is_terminal` | 定义真实终态判定 | 不替代 orchestrator 的生命周期裁定 |

## 文件落点

| 设计项 | 文件 |
|---|---|
| 私有类声明 | `runtime/src/fsm/AgentFsm.h` |
| 私有类实现 | `runtime/src/fsm/AgentFsm.cpp` |
| 行为测试 | `tests/unit/runtime/AgentFsmTest.cpp` |
| CMake 接线 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/fsm/AgentFsm.h`、`runtime/src/fsm/AgentFsm.cpp`
2. 测试目标：`tests/unit/runtime/AgentFsmTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_agent_fsm_unit_test && ctest --test-dir build-ci -R "^AgentFsmTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 锁定 AgentFsm 私有承载面 | 不扩 public ABI | PASS |
| D2 | 锁定状态推进与 rejection 判定顺序 | 014 不重复解释 6.7.4 | PASS |
| D3 | 锁定终态集合 | `Completed` 非终态、`SafeMode` 终态 | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. 014 与 013 的职责边界已分清。
3. 锁顺序与终态判定已明确。
4. Build 三件套已锁定。

结论：D Gate = PASS，可进入 RT-TODO-014 的 Build 阶段。