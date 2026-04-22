# RT-TODO-010 ISessionManager 与 SessionTypes 设计收敛

## 1. 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.5 已将 SessionManager 收敛为“管理 Session、Turn、Checkpoint 锚点与等待态元信息，向主循环提供可恢复的 SessionSnapshot”的组件，建议方法集合包含 `load_session(...)`、`prepare_turn(...)`、`persist_turn(...)`、`bind_checkpoint_ref(...)`、`build_resume_seed(...)`。
2. 同一文档的 6.24.5.1 已冻结 `SessionSnapshot`、`PendingInteractionState`、`TurnPersistPlan`、`ResumeSeed` 的职责与字段概要，并明确 `TurnPersistPlan` 是 module-local 写回计划，不应直接进入 public include 面。
3. `docs/todos/runtime/deliverables/RT-TODO-002-SessionManager-supporting-types口径收敛.md` 已把 `SessionSnapshot` 定位为 read-mostly preflight snapshot，把 `ResumeSeed` 定位为交给 checkpoint 路径继续构造 `ResumePlan` 的最小恢复种子，禁止越权承担 schema/version 判定。
4. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.8、6.24.9 已分别把 RecoveryManager 与 CheckpointManager 的边界固定下来，因此 010 的 session public surface 不能吞并恢复裁定或 checkpoint compatibility 语义。
5. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 RT-TODO-010 限定为 `runtime/include/session/ISessionManager.h`、`runtime/include/session/SessionTypes.h` 的 include 面收敛，验收出口为 `SessionTypeSurfaceTest`。

## 2. 外部参考

1. Temporal Workflow 文档强调 durable execution 依赖对既有执行实例的稳定重建，而不是把恢复路径当成新请求重跑；这支持 DASALL 将 `SessionSnapshot` 与 `ResumeSeed` 拆成不同生命周期对象，并把等待态事实保留在最小可恢复集合中。

## 3. 设计结论

1. `SessionTypes.h` 固定公开三类核心 supporting types：
   - `PendingInteractionState`：等待态最小恢复对象；
   - `SessionSnapshot`：preflight 读取态快照；
   - `ResumeSeed`：交给 checkpoint 路径继续生成 `ResumePlan` 的恢复种子。
2. `PendingInteractionState` 的等待态分类固定为枚举，而不是散落字符串，至少覆盖：
   - `None`
   - `Clarify`
   - `Confirm`
   - `WaitExternal`
   这样 010/018/028 可以共享同一套 waiting vocabulary，不再各自拼接渠道名或 blocking reason。
3. `SessionSnapshot` 维持 read-mostly 边界，只承载：`session_id`、`request_id`、`turn_index`、`active_checkpoint_ref`、`fsm_state`、`budget_snapshot_ref`、`pending_interaction`、`last_result_summary`。它不暴露底层存储句柄，也不直接承载写回计划字段。
4. `ResumeSeed` 固定为 runtime-owned 恢复种子，只承载：`session_id`、`request_id`、`checkpoint_ref`、`fsm_state`、`pending_interaction`、`policy_snapshot_ref`、`resume_reason`。它不直接生成 `ResumePlan`，也不越权处理 checkpoint version/schema compatibility。
5. 为了让 `ISessionManager` 的 5 个方法具备稳定、可测试的编译面，010 额外冻结少量 request/result seam types：
   - `SessionLoadRequest` / `SessionLoadResult`
   - `PrepareTurnRequest` / `PrepareTurnResult`
   - `SessionPersistRequest` / `SessionPersistResult`
   - `BindCheckpointRefRequest`
   - `BuildResumeSeedRequest` / `ResumeSeedResult`
   这些对象只表达 public seam 输入/输出，不替代 002 已冻结的 `TurnPersistPlan`。
6. `TurnPersistPlan` 保持 module-local，不进入 include 面：
   - `persist_turn(...)` 的 public seam 只接受 `SessionPersistRequest` 并返回 `SessionPersistResult`
   - 真正的写回计划细节继续留给 018 的 `SessionManager.cpp` 内部消费
7. `ISessionManager` 的最小 public contract 本轮固定为：
   - `load_session(const SessionLoadRequest&)`
   - `prepare_turn(const PrepareTurnRequest&)`
   - `persist_turn(const SessionPersistRequest&)`
   - `bind_checkpoint_ref(const BindCheckpointRefRequest&)`
   - `build_resume_seed(const BuildResumeSeedRequest&)`
   其中 session 侧只输出 session-owned snapshot / persist result / resume seed，不裁定 recovery admit/reject。

## 4. 边界与职责

| 对象 | 本轮职责 | 明确不做 |
|---|---|---|
| `PendingInteractionState` | 固定 waiting/confirm/clarify/external 的最小恢复对象 | 不携带完整输入历史或 message transcript |
| `SessionSnapshot` | 暴露编排前的 read-mostly 会话真值视图 | 不承担写回计划或恢复裁定 |
| `ResumeSeed` | 暴露交给 checkpoint 路径的最小恢复种子 | 不替代 `ResumePlan`、不做 version/schema 校验 |
| request/result seam types | 为 `ISessionManager` 提供稳定编译面 | 不暴露底层存储句柄 |
| `ISessionManager` | 定义 session public contract | 不拥有 ContextPacket 装配权，不拥有 FSM/Recovery 裁定权 |
| `TurnPersistPlan` | 保持 module-local 写回计划 | 不升格为 public include type |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 |
|---|---|
| `PendingInteractionState` / `SessionSnapshot` / `ResumeSeed` 与 request/result seam types | `runtime/include/session/SessionTypes.h` |
| `ISessionManager` 最小 public interface | `runtime/include/session/ISessionManager.h` |
| 正例：load/prepare/resume-seed 可组装；负例：checkpoint 绑定或 resume-seed 最小字段缺失时显式拒绝 | `tests/unit/runtime/SessionTypeSurfaceTest.cpp` |
| 单测 discoverability 接线 | `tests/unit/runtime/CMakeLists.txt`、`tests/unit/CMakeLists.txt` |

## 6. Build 三件套

1. 代码目标：`runtime/include/session/ISessionManager.h`、`runtime/include/session/SessionTypes.h`
2. 测试目标：`tests/unit/runtime/SessionTypeSurfaceTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_session_type_surface_unit_test && ctest --test-dir build-ci -R SessionTypeSurfaceTest --output-on-failure`

## 7. D 原子项完成情况

| ID | 内容 | 结果 |
|---|---|---|
| D1 | 锁定 `SessionSnapshot` / `PendingInteractionState` / `ResumeSeed` 的 public include 边界 | PASS |
| D2 | 锁定 request/result seam types 与 `ISessionManager` 最小方法面 | PASS |
| D3 | 保持 `TurnPersistPlan` module-local，并产出 Build 三件套 | PASS |

## 8. D Gate

- Gate: PASS
- 进入 B 的条件：已满足
- 说明：本轮只冻结 session public surface，不越权实现真实存储、checkpoint 兼容性或 recovery 策略。