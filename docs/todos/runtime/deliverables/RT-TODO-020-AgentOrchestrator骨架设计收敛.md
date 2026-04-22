# RT-TODO-020 AgentOrchestrator 骨架设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.4 已把 `AgentOrchestrator` 固定为 runtime 的单次请求主循环持有者，并把内部阶段冻结为 `preflight`、`main_loop`、`tool_round`、`recovery_round`、`terminalize`。
2. 同一文档的 6.8.1 和 6.8.2 已把 unary 主流程与异常恢复时序拆成：`AgentFacade -> AgentOrchestrator -> Session/Checkpoint/Cognition/Tool/Recovery`，说明 020 的目标是先让五段 topology 连通，而不是提前宣称真端口已 ready。
3. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 已把 RT-TODO-020 的完成判定锁定为“使用 stub/null 端口打通五段主循环，且 FSM 推进可追踪”；同时 RT-BLK-05 已由 RT-TODO-003 解阻。
4. `docs/todos/runtime/deliverables/RT-TODO-003-RuntimeDependencySet-seam收敛.md` 已明确：`AgentOrchestrator` 只能消费组合根注入的 seam，不得自行判断并构造 stub/null adapter。020 因此必须采用“注入式 stub round ports”，而不是把 blocker/profile 判断塞进主循环本体。
5. `runtime/src/fsm/AgentFsm.h` 与 `runtime/src/fsm/TransitionGuardTable.cpp` 已冻结真实状态机与守卫表，足以支撑 020 用真实 `AgentFsm` 贯穿 `Idle -> Receiving -> Planning -> ... -> Completed` 的骨架路径。
6. `runtime/src/AgentFacade.cpp` 当前仍返回 “AgentOrchestrator is not wired yet”，说明 runtime 缺的不是 facade surface，而是一个可执行的 orchestrator 内核骨架。

## 设计结论

1. 020 新增 runtime 私有类：
   - `runtime/src/AgentOrchestrator.h`
   - `runtime/src/AgentOrchestrator.cpp`
2. `AgentOrchestrator` 本轮只落地 `run_once(const contracts::AgentRequest&)`，并返回 module-local `OrchestratorRunResult`：
   - 包含最终 `contracts::AgentResult`
   - 包含按固定顺序输出的 `stage_trace`
   - 记录是否走过 `tool_round` 与 `recovery_round`
3. 020 的最小真实控制器只有 `AgentFsm`：
   - preflight 用真实状态迁移 `Idle -> Receiving -> Planning`
   - main loop 用真实状态迁移 `Planning -> Reasoning -> Responding/ToolCalling`
   - tool/recovery/terminal round 继续复用真实状态迁移表
4. 020 不自行构造 stub/null adapter。主循环只消费构造时注入的 `OrchestratorStubPorts`：
   - `reject_preflight`：用于 fail-closed preflight
   - `main_loop_exit`：决定 direct response 还是进入 tool round
   - `recovery_exit`：决定 reflection 后继续响应还是进入 fail-safe
5. `tool_round` 与 `recovery_round` 即便被跳过，也必须在 `stage_trace` 中保留占位，确保五段 topology 可观测、可断言。
6. `terminalize` 固定负责：
   - 必要时把 `FailedSafe` 收敛到 `Responding`
   - 推进 `Responding -> Auditing -> Persisting -> Completed`
   - 输出最终 `AgentResult`
7. 020 只证明 runtime-local orchestration skeleton 成立，不负责：
   - `continue_from_checkpoint(...)`
   - `handle_waiting_state(...)`
   - Session/Budget/Checkpoint/Recovery/Scheduler 真控制器装配
   - 相邻模块真端口联调

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `AgentOrchestrator::run_once` | 驱动五段主循环骨架，输出 stage trace 与最终结果 | 不做 true integration，不接 resume/waiting path |
| `OrchestratorStubPorts` | 由组合根或测试夹具注入 deterministic stub round outcome | 不承担 profile/blocker 判断 |
| `AgentFsm` | 真实执行业务状态迁移与 guard 校验 | 不解释下游错误语义 |
| `OrchestratorRunResult` | 收敛本轮 module-local 执行轨迹 | 不作为 public ABI 暴露 |

## 文件落点

| 设计项 | 文件 |
|---|---|
| 私有类声明 | `runtime/src/AgentOrchestrator.h` |
| 私有类实现 | `runtime/src/AgentOrchestrator.cpp` |
| 骨架行为测试 | `tests/unit/runtime/AgentOrchestratorSkeletonTest.cpp` |
| CMake 接线 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/AgentOrchestrator.h`、`runtime/src/AgentOrchestrator.cpp`
2. 测试目标：`tests/unit/runtime/AgentOrchestratorSkeletonTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_agent_orchestrator_skeleton_unit_test && ctest --test-dir build-ci -R "^AgentOrchestratorSkeletonTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 锁定五段 topology 的最小骨架 | `stage_trace` 固定输出 preflight/main_loop/tool_round/recovery_round/terminalize | PASS |
| D2 | 锁定 020 的 seam 注入策略 | stub round outcome 由外部注入，主循环不自行造 stub/null adapter | PASS |
| D3 | 锁定真实 FSM 接入范围 | 020 至少真实推进 `Idle -> ... -> Completed` 或 fail-safe 路径 | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. RT-BLK-05 已由 003 解阻，但 020 仍保持 runtime-local seam/stub 证明，不外推 true integration ready。
3. Build 三件套已锁定。
4. 范围未越出 020 的骨架任务边界。

结论：D Gate = PASS，可进入 RT-TODO-020 的 Build 阶段。