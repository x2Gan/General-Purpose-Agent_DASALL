# RT-TODO-009 ICheckpointManager 与 IRecoveryManager 设计收敛

## 1. 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.9 已将 CheckpointManager 收敛为“构建、校验、保存、读取 Checkpoint，并生成 ResumePlan”的组件，建议接口固定为 `build_checkpoint(...)`、`save(...)`、`load(...)`、`validate(...)`、`make_resume_plan(...)`。
2. 同一文档的 6.24.8 已将 RecoveryManager 收敛为“读取 RecoveryRequest，做恢复准入和执行”的组件，建议接口固定为 `evaluate(const RecoveryRequest&)`、`execute(const RecoveryExecutionPlan&)`、`apply(const RecoveryOutcome&)`。
3. 同一文档的 6.20 明确 Checkpoint 版本兼容性必须通过 `Checkpoint.tags` 写入 `rt.schema_version`、`rt.fsm_state_enum_version`、`rt.budget_schema_version` 三个保留 key，并在 load/resume 时执行 reject gate。
4. `contracts/include/checkpoint/Checkpoint.h` 已冻结 shared Checkpoint 结构，其中 `tags` 是 `std::vector<std::string>` 而非 key-value map；因此 runtime 的 version metadata 只能以稳定字符串编码写入，不能发明新的 shared contracts 字段。
5. `contracts/include/checkpoint/RecoveryRequest.h` 与 `contracts/include/checkpoint/RecoveryOutcome.h` 已分别冻结恢复准入输入和恢复执行结果；runtime 不应在 009 再复制一套 request/result 顶层语义。
6. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.5.1 已冻结 CheckpointState 与 RuntimeState 的映射约束：Paused / WaitingConfirm / WaitingTool / Running 可恢复，Failed / Succeeded 不允许 resume。
7. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 RT-TODO-009 限定为 `runtime/include/checkpoint/ICheckpointManager.h`、`runtime/include/checkpoint/CheckpointBuildTypes.h`、`runtime/include/recovery/IRecoveryManager.h`、`runtime/include/recovery/ResumePlan.h` 的 public surface 收敛，验收出口为 `CheckpointManagerTest` 与 `RecoveryManagerTest`。

## 2. 外部参考

1. Temporal 的 Workflow Execution 文档强调 durable execution 的 replay 必须从持久化状态继续推进，并受 deterministic constraints 约束；这支持 DASALL 把 checkpoint version gate 和 resume admissibility 固定在 CheckpointManager，而不是散落给 orchestrator 或 recovery 路径各自解释。

## 3. 设计结论

1. `CheckpointBuildTypes.h` 负责冻结 checkpoint supporting types，而不是提前实现持久化：
   - `CheckpointBuildRequest` 承载 `StateTransitionOutcome` 与最小 checkpoint 构建事实；
   - `CheckpointBuildResult`、`CheckpointPersistResult`、`CheckpointLoadResult`、`CheckpointConsistencyReport` 负责表达 build/save/load/validate 的结构化结果；
   - 所有失败出口统一挂接 `RuntimeErrorCode`，供 016 直接落实现。
2. 由于 shared `Checkpoint.tags` 是字符串数组，009 固定 version metadata 的写法为 `key=value`，并提供稳定 helper 读取保留 key：
   - `rt.schema_version`
   - `rt.fsm_state_enum_version`
   - `rt.budget_schema_version`
   默认值在本轮冻结为 `"1"`，后续仅允许通过版本常量递增，不允许散落魔法字符串。
3. `CheckpointConsistencyReport` 至少要能表达以下 reject 面：
   - pending action 缺失
   - version tag 缺失
   - schema version 不兼容
   - FSM version 不兼容
   - terminal / unspecified checkpoint 不可 resume
   这样 016/024 可直接消费统一 reject shape，而不是各自产生裸字符串。
4. `ResumePlan` 作为 checkpoint -> runtime 的恢复计划对象，固定只表达 runtime-owned 恢复事实：`checkpoint_ref`、`target_state`、`checkpoint_state`、`resume_reason`、`pending_action`、`policy_snapshot_ref`、`requires_operator_intervention`。它不复制 `RecoveryRequest`、`SessionSnapshot` 或完整 `AgentRequest`。
5. `ResumePlan` 的状态恢复口径本轮冻结为保守映射：
   - `Paused -> WaitingClarify`
   - `WaitingConfirm -> WaitingConfirm`
   - `WaitingTool -> WaitingExternal`
   - `Running -> Reasoning`
   - `Failed / Succeeded / Unspecified -> reject`
   这样 009 不需要等待 010 的 Session types，也不会越权提前发明第二套 resume schema。
6. `ICheckpointManager` 只管理 checkpoint 生命周期：
   - `build_checkpoint(...)` 构建并本地阻断非法 checkpoint
   - `save(...)` 持久化 checkpoint 并返回结构化结果
   - `load(...)` 读取 checkpoint 并执行版本兼容 gate
   - `validate(...)` 输出一致性报告
   - `make_resume_plan(...)` 产出可执行或被拒绝的恢复计划
7. `IRecoveryManager` 不复制 Reflection 或 Checkpoint 的顶层语义，只补足自己的执行 supporting types：
   - `RecoveryExecutionPlan` 表达 admit / reject / escalate 三态、计划动作、可选 `ResumePlan` 与 safe failure hint；
   - `RecoveryApplyResult` 表达 `apply(...)` 的结构化结果；
   - `evaluate(...)` 只做准入与计划生成，`execute(...)` 输出 frozen `RecoveryOutcome`，`apply(...)` 只处理 runtime-local 状态推进。

## 4. 边界与职责

| 对象 | 本轮职责 | 明确不做 |
|---|---|---|
| `CheckpointBuildRequest` | 固定构建 checkpoint 的最小输入 | 不嵌入完整 `SessionSnapshot` 或存储句柄 |
| version tag helpers | 固定 tag key、默认版本值和 `key=value` 编码 | 不实现真实存储后端 |
| `CheckpointConsistencyReport` | 暴露 checkpoint build/load/resume 的结构化 reject 面 | 不替代 replay regression gate |
| `ResumePlan` | 暴露 checkpoint 恢复到 runtime 的最小执行计划 | 不复制 `RecoveryRequest`、`AgentRequest` 或完整会话快照 |
| `ICheckpointManager` | 定义 checkpoint 生命周期 public contract | 不推进状态机，不裁定恢复策略 |
| `RecoveryExecutionPlan` | 暴露 RecoveryManager 的 admit / reject / escalate 计划 | 不重述 ReflectionDecision 顶层字段 |
| `IRecoveryManager` | 定义恢复准入/执行/apply public contract | 不生成新的认知建议，不替代 SafeModeController |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 |
|---|---|
| checkpoint build/load/validate supporting types 与 version tag helpers | `runtime/include/checkpoint/CheckpointBuildTypes.h` |
| `ICheckpointManager` 最小 public interface | `runtime/include/checkpoint/ICheckpointManager.h` |
| `ResumePlan` 与 checkpoint state -> runtime state 恢复口径 | `runtime/include/recovery/ResumePlan.h` |
| `RecoveryExecutionPlan` / `RecoveryApplyResult` 与 `IRecoveryManager` 方法面 | `runtime/include/recovery/IRecoveryManager.h` |
| 正例：version tags + waiting checkpoint 可生成 resume plan；负例：schema version / terminal state reject | `tests/unit/runtime/CheckpointManagerTest.cpp` |
| 正例：evaluate -> execute -> apply 流；负例：unsafe replay reject | `tests/unit/runtime/RecoveryManagerTest.cpp` |
| 单测 discoverability 接线 | `tests/unit/runtime/CMakeLists.txt`、`tests/unit/CMakeLists.txt` |

## 6. Build 三件套

1. 代码目标：`runtime/include/checkpoint/ICheckpointManager.h`、`runtime/include/checkpoint/CheckpointBuildTypes.h`、`runtime/include/recovery/IRecoveryManager.h`、`runtime/include/recovery/ResumePlan.h`
2. 测试目标：`tests/unit/runtime/CheckpointManagerTest.cpp`、`tests/unit/runtime/RecoveryManagerTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_checkpoint_manager_unit_test dasall_runtime_recovery_manager_unit_test && ctest --test-dir build-ci -R "CheckpointManagerTest|RecoveryManagerTest" --output-on-failure`

## 7. D 原子项完成情况

| ID | 内容 | 结果 |
|---|---|---|
| D1 | 锁定 checkpoint version tag 与 consistency/report supporting types | PASS |
| D2 | 锁定 `ResumePlan` 与 `ICheckpointManager` 最小方法面 | PASS |
| D3 | 锁定 `RecoveryExecutionPlan`、`IRecoveryManager` 方法面与 Build 三件套 | PASS |

## 8. D Gate

- Gate: PASS
- 进入 B 的条件：已满足
- 说明：本轮只冻结 checkpoint/recovery include 面，不越权实现持久化、恢复算法或 replay regression gate。