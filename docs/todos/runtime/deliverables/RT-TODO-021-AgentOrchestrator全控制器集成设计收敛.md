# RT-TODO-021 AgentOrchestrator 全控制器集成设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.4 已把 `AgentOrchestrator` 固定为 runtime 主循环与最终裁定权的唯一持有者，并把 `run_once(...)`、`continue_from_checkpoint(...)`、`handle_waiting_state(...)` 列为建议接口。
2. 同一文档的 6.8.1、6.8.2 和 6.9 已把 unary success、tool/recovery 与 waiting/resume 三类数据流写成连续链路，说明 021 的关键不是新增控制器，而是把 014~019 的真实控制器按正确顺序装配进现有 orchestrator 骨架。
3. `runtime/src/budget/BudgetController.h`、`runtime/src/session/SessionManager.h`、`runtime/src/checkpoint/CheckpointManager.h`、`runtime/src/recovery/RecoveryManager.h`、`runtime/src/scheduling/Scheduler.h` 已提供真实 runtime-local 控制器；`runtime/src/AgentOrchestrator.cpp` 当前仍只停在 deterministic stub round 决策层。
4. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.14.2 已声明锁顺序 L1->L6；021 因此只能采用“顺序调用控制器、不跨调用持锁”的装配策略，而不能在 orchestrator 中额外拼接复合锁。
5. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 已将 021 的完成判定锁定为：
   - `run_once/continue_from_checkpoint/handle_waiting_state` 接入全部控制器；
   - 五段主循环连通；
   - 只证明 runtime-local 控制器装配成立，不外推真端口联调 ready。
6. RT-TODO-020 已把五段 stage trace 与 runtime-private `OrchestratorRunResult` 落地，说明 021 不应重写主循环壳子，而应把真实控制器嵌入既有 topology。

## 设计结论

1. 021 继续沿用：
   - `runtime/src/AgentOrchestrator.h`
   - `runtime/src/AgentOrchestrator.cpp`
2. `AgentOrchestrator` 本轮新增并真实持有以下 runtime-private 控制器成员：
   - `SessionManager`
   - `BudgetController`
   - `CheckpointManager`
   - `RecoveryManager`
   - `Scheduler`
3. `run_once(...)` 在 021 要支持三条 runtime-local 装配路径：
   - direct success：消费 Session/Budget/FSM/Checkpoint
   - tool -> abort_safe：额外消费 Scheduler/Recovery
   - waiting clarify：生成 waiting checkpoint、session anchor 与 `ResumePlan`
4. `continue_from_checkpoint(...)` 的最小实现采用：
   - `CheckpointManager::load()` 校验 anchor
   - 以 `ResumePlan.target_state` 启动真实 `AgentFsm`
   - 根据 waiting state 走回 `Receiving/Reflecting/ToolCalling`，再收敛到 `Completed`
5. `handle_waiting_state(...)` 在 021 只做最小 dispatcher：
   - `SessionManager::load_session()`
   - `SessionManager::prepare_turn()`
   - `SessionManager::build_resume_seed()`
   - `CheckpointManager::load()/make_resume_plan()`
   - 再委托 `continue_from_checkpoint(...)`
6. 因 `ResumeHandleRequest` 当前没有 typed clarification/confirm/external payload，021 的 `handle_waiting_state(...)` 只证明 dispatcher 与 resume routing 成立，不宣称完整的 waiting-input API 已冻结。
7. 021 仍不接 `AgentFacade`。原因不是 orchestrator 不可用，而是 `RuntimeDependencySet` 只有前向声明、尚无真实定义；本轮继续停在 runtime-local 证明层，避免制造假 ready。

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `AgentOrchestrator::run_once` | 真实装配 Session/Budget/FSM/Checkpoint/Recovery/Scheduler | 不接真端口 facade/runtime dependency graph |
| `AgentOrchestrator::continue_from_checkpoint` | 从 `ResumePlan` 恢复等待态或运行态并重新收敛主链 | 不定义新的 resume contract 对象 |
| `AgentOrchestrator::handle_waiting_state` | 使用 Session/Checkpoint 真控制器构建 resume dispatcher | 不承诺 typed waiting payload 已完备 |
| `OrchestratorRunResult` | 暴露 checkpoint/recovery/scheduler/session 的 module-local 结果槽位 | 不升级为 public ABI |

## 文件落点

| 设计项 | 文件 |
|---|---|
| 私有装配实现 | `runtime/src/AgentOrchestrator.h`、`runtime/src/AgentOrchestrator.cpp` |
| 控制器装配单测 | `tests/unit/runtime/AgentOrchestratorControllerAssemblyTest.cpp` |
| CMake 接线 | `tests/unit/runtime/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/AgentOrchestrator.h`、`runtime/src/AgentOrchestrator.cpp`
2. 测试目标：`tests/unit/runtime/AgentOrchestratorControllerAssemblyTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_agent_orchestrator_controller_assembly_unit_test && ctest --test-dir build-ci -R "^AgentOrchestratorControllerAssemblyTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 锁定 direct success 的最小控制器装配 | Session/Budget/FSM/Checkpoint 真实闭环 | PASS |
| D2 | 锁定 tool -> abort_safe 装配 | Scheduler 与 Recovery 真控制器参与主循环 | PASS |
| D3 | 锁定 waiting -> resume 装配 | `handle_waiting_state + continue_from_checkpoint` 可 runtime-local 验证 | PASS |
| D4 | 锁定锁顺序实现策略 | orchestrator 只做顺序调用，不跨控制器持锁 | PASS |
| D5 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. 021 继续停在 runtime-local seam/stub 证明层，不与 true integration gate 混淆。
3. `continue_from_checkpoint(...)` 与 `handle_waiting_state(...)` 已在设计层明确最小实现边界。
4. Build 三件套已锁定。

结论：D Gate = PASS，可进入 RT-TODO-021 的 Build 阶段。