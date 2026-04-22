# RT-TODO-016 CheckpointManager 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.9 已把 `CheckpointManager` 的职责固定为：构建、校验、保存、读取 `Checkpoint`，并生成 `ResumePlan`。
2. 同一文档与 6.5.1 / 6.20 一起约束了 016 的三个硬要求：
   - waiting checkpoints 必须带 `pending_action`
   - checkpoint 必须写入 `rt.schema_version`、`rt.fsm_state_enum_version`、`rt.budget_schema_version`
   - load / resume 遇到不兼容版本必须显式 reject
3. `runtime/include/checkpoint/CheckpointBuildTypes.h` 已冻结 build/persist/load/validate supporting types、version tag helpers 与 `CheckpointConsistencyIssue`，说明 016 应只实现控制器，不再造 supporting type。
4. `runtime/src/checkpoint/CheckpointStateMapper.cpp` 已在 012 落地 `RuntimeState -> CheckpointState` 的唯一折叠规则；016 应直接复用，而不是复制映射逻辑。
5. `tests/unit/runtime/CheckpointManagerTest.cpp` 当前 fake 已覆盖 build/save/load/resume 的最小正反例，适合直接提升为真实 `CheckpointManager` 行为测试。

## 设计结论

1. `CheckpointManager` 作为 runtime 私有控制器落在：
   - `runtime/src/checkpoint/CheckpointManager.h`
   - `runtime/src/checkpoint/CheckpointManager.cpp`
2. 控制器内部只维护一个最小内存存储：`std::optional<contracts::Checkpoint> stored_checkpoint_`，并由 `mutable std::mutex ckpt_mutex_` 保护，满足 6.14.2 的 L4 锁序约束。
3. `build_checkpoint(...)` 的固定流程为：
   - 先拒绝未 accepted 的 `StateTransitionOutcome`
   - 再通过 `CheckpointStateMapper::to_checkpoint_state(resolved_state)` 折叠运行态 checkpoint state，并与 hint 做一致性校验
   - 将 request 字段投影到 `contracts::Checkpoint`
   - 合并 runtime version tags
   - 最后调用 `validate(...)`，只有 consistent 才返回 checkpoint
4. `validate(...)` 的固定检查顺序为：
   - `checkpoint_id` / `step_id` / `working_memory_snapshot`
   - 具体 `CheckpointState`
   - waiting states 的 `pending_action` 非空约束
   - 三个 runtime version tag 存在且版本兼容
5. `save(...)` / `load(...)` 的固定语义为：
   - `save(...)` 先 validate，失败返回 `RT_E_411_CHECKPOINT_SAVE_FAILED`
   - `load(...)` 先按 `checkpoint_ref` 命中存储，再 validate；不兼容或不存在时返回 reject
6. `make_resume_plan(...)` 的固定语义为：
   - 先 validate checkpoint
   - 再复用 `resume_target_state(...)`
   - terminal / unspecified checkpoint state 拒绝 resume
   - `WaitingConfirm` 生成 `requires_operator_intervention=true`
7. 016 不负责：
   - 外部持久化 backend 接线
   - SessionManager 的 checkpoint anchor 绑定
   - RecoveryManager 的恢复准入判断

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `CheckpointManager::build_checkpoint` | 从 transition outcome 构建最小 checkpoint | 不推进状态机 |
| `CheckpointManager::validate` | 校验字段、状态与版本兼容性 | 不生成恢复策略 |
| `CheckpointManager::save/load` | 提供最小内存存储语义 | 不接真持久化 backend |
| `CheckpointManager::make_resume_plan` | 生成 resumable/rejected 决定 | 不执行恢复 |

## 文件落点

| 设计项 | 文件 |
|---|---|
| 私有类声明 | `runtime/src/checkpoint/CheckpointManager.h` |
| 私有类实现 | `runtime/src/checkpoint/CheckpointManager.cpp` |
| 行为测试 | `tests/unit/runtime/CheckpointManagerTest.cpp` |
| CMake 接线 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/checkpoint/CheckpointManager.h`、`runtime/src/checkpoint/CheckpointManager.cpp`
2. 测试目标：`tests/unit/runtime/CheckpointManagerTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_checkpoint_manager_unit_test && ctest --test-dir build-ci -R "^CheckpointManagerTest$|^CheckpointStateMapperTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 锁定 build/validate/save/load/resume 最小闭环 | 不越权依赖 session/recovery 实现 | PASS |
| D2 | 锁定 pending_action 与 version tag 规则 | waiting / version mismatch 可自动拒绝 | PASS |
| D3 | 锁定 ResumePlan 生成边界 | terminal checkpoint 不可恢复 | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. 012/009/016 的职责边界已串清。
3. Build 三件套已锁定。
4. 范围未越出 016 控制器边界。

结论：D Gate = PASS，可进入 RT-TODO-016 的 Build 阶段。