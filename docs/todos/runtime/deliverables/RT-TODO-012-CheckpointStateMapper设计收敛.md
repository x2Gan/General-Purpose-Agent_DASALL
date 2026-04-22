# RT-TODO-012 CheckpointStateMapper 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.5.1 已冻结 `CheckpointState` 与 17 态 `RuntimeState` 的折叠映射：
   - `Running` <- `Receiving/Planning/Reasoning/ToolCalling/Reflecting/Responding/Auditing/Persisting`
   - `Paused` <- `WaitingClarify`
   - `WaitingConfirm` <- `WaitingConfirm`
   - `WaitingTool` <- `WaitingExternal`
   - `Failed` <- `Failed/FailedSafe/Degraded/SafeMode`
   - `Succeeded` <- `Completed`
   - `Unspecified` <- `Idle`
2. 同一节明确约束：CheckpointManager 构建 checkpoint 时必须复用此映射表；恢复时只允许 `Paused/WaitingConfirm/WaitingTool/Running` 进入 resume，`Failed/Succeeded` 不允许 resume。
3. `runtime/include/fsm/StateTransitionTypes.h` 已冻结 17 态 `RuntimeState`，并在 `StateTransitionCheckpointHint` 中直接消费 `contracts::CheckpointState`，说明 runtime 侧不能再引入第二套 checkpoint 状态枚举。
4. `runtime/include/recovery/ResumePlan.h` 已存在 `resume_target_state(...)` 的 public seam，但 012 的 TODO 范围只要求实现 `CheckpointStateMapper::to_checkpoint_state/can_resume_from`；因此 mapper 应作为 runtime 内部规则表，而不是新的 public include。
5. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 012 的代码目标限定为 `runtime/src/checkpoint/CheckpointStateMapper.cpp`，测试出口限定为 `CheckpointStateMapperTest`。

## 外部参考

1. Temporal `Workflow Execution` 文档强调 durable execution 依赖显式的持久化状态与 replay 恢复边界：执行恢复总是从最近的已记录事件继续，而不是任意从内部中间态重启。这支持 DASALL 先把更丰富的 FSM 状态折叠成有限的 checkpoint 状态集合，再基于该集合决定是否允许 resume。
2. 同一文档将 workflow status 区分为可继续推进的 open 状态与不可继续推进的 closed 状态。这个口径与 DASALL 将 `Running/Paused/Waiting*` 视为可 resume，而将 `Failed/Succeeded` 视为不可 resume 的规则一致。

## 设计结论

1. `CheckpointStateMapper` 作为 runtime 内部规则表实现，不进入 `runtime/include`，而是落在：
   - `runtime/src/checkpoint/CheckpointStateMapper.h`
   - `runtime/src/checkpoint/CheckpointStateMapper.cpp`
2. `to_checkpoint_state(...)` 返回 `std::optional<contracts::CheckpointState>`：
   - 对 17 个已知 `RuntimeState` 给出显式折叠结果；
   - 对非法枚举值返回 `std::nullopt`，让后续 `CheckpointManager` 能把这种情况显式升级为 reject，而不是静默回退。
3. `can_resume_from(...)` 只负责回答 checkpoint 状态是否允许 resume：
   - `Running/Paused/WaitingConfirm/WaitingTool` -> `true`
   - `Failed/Succeeded/Unspecified` -> `false`
4. 012 不负责：
   - 生成 `ResumePlan`
   - 校验 checkpoint version tag
   - 写入 `pending_action`
   - 解释恢复拒绝原因到 `RT_E_*`
   这些职责仍留给 016/017 和已冻结的 supporting types。
5. 为了让 016 可复用、而 012 本身又保持 module-local，测试 `CheckpointStateMapperTest` 将直接包含 runtime 的私有头，不新增 public ABI。

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `CheckpointStateMapper::to_checkpoint_state` | 将 17 态 `RuntimeState` 折叠为合法 `CheckpointState` | 不生成 checkpoint 对象，不写 version tag |
| `CheckpointStateMapper::can_resume_from` | 判断 checkpoint 状态是否允许恢复 | 不决定恢复目标 `RuntimeState` |
| `CheckpointStateMapperTest` | 覆盖 6.5.1 全映射与 resume admit/reject 规则 | 不替代 016 的 CheckpointManager 一致性测试 |

## 文件落点

| 设计项 | 文件 |
|---|---|
| mapper 私有声明 | `runtime/src/checkpoint/CheckpointStateMapper.h` |
| mapper 实现 | `runtime/src/checkpoint/CheckpointStateMapper.cpp` |
| 单测 | `tests/unit/runtime/CheckpointStateMapperTest.cpp` |
| CMake 注册 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt`、`tests/unit/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/checkpoint/CheckpointStateMapper.h`、`runtime/src/checkpoint/CheckpointStateMapper.cpp`
2. 测试目标：`tests/unit/runtime/CheckpointStateMapperTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_checkpoint_state_mapper_unit_test && ctest --test-dir build-ci -R "^CheckpointStateMapperTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 固定 17 态到 7 态的唯一折叠表 | 每个 `RuntimeState` 都有唯一映射或显式 reject | PASS |
| D2 | 固定 checkpoint resume admit/reject 规则 | `Failed/Succeeded/Unspecified` 明确不可 resume | PASS |
| D3 | 固定 mapper 的 module-local 落点 | 不新增 public include | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. Design -> Build 映射完整。
3. Build 三件套已锁定。
4. 范围未越过 012 的规则表任务边界。

结论：D Gate = PASS，可进入 RT-TODO-012 的 Build 阶段。