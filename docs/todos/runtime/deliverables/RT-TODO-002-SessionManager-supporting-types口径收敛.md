# RT-TODO-002 SessionManager supporting types 口径收敛

日期：2026-04-22  
任务：RT-TODO-002  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/runtime/DASALL_runtime子系统专项TODO.md](/home/gangan/DASALL/docs/todos/runtime/DASALL_runtime子系统专项TODO.md) 将 RT-TODO-002 定义为补齐 `SessionSnapshot`、`PendingInteractionState`、`TurnPersistPlan`、`ResumeSeed` 的输入/输出语义，用于继续解阻 RT-BLK-04。
2. [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 的 6.24.5 目前只有 SessionManager 的类级职责、接口和执行流说明，原表甚至仍以 `SessionConsistencyReport` 代替 `ResumeSeed`，不足以支撑后续 `ISessionManager` public surface 设计。
3. 6.24.4、6.24.8、6.24.9 已把 `AgentOrchestrator`、`RecoveryManager`、`CheckpointManager` 的边界写清，因此 SessionManager supporting types 必须只承载 session-owned 快照、等待态和恢复种子，而不能吞并 checkpoint/recovery 的裁定语义。

## 2. 外部参考

1. Temporal Workflow 文档强调 durable execution 依赖对“既有执行实例”的稳定重建，而不是把每次恢复都当作新的业务请求；这支持把 `SessionSnapshot` 和 `ResumeSeed` 分离成不同生命周期对象。
2. Temporal 的 deterministic replay 约束同样说明，恢复入口应引用先前持久化的最小事实集合，因此 `PendingInteractionState` 和 `ResumeSeed` 都应保持窄接口，不重复携带完整请求载荷。

## 3. 设计结论

1. `SessionSnapshot` 固定为 preflight 阶段的 read-mostly 会话快照，供主循环读取当前 session、checkpoint、FSM 和等待态事实。
2. `PendingInteractionState` 固定为 waiting/confirm/clarify 的最小恢复对象，只保存等待态恢复所需的事实，不承担完整对话历史归档。
3. `TurnPersistPlan` 固定为 turn 结束后的持久化计划对象，与 `SessionSnapshot` 分离，避免把读取态和写回态混成同一 supporting type。
4. `ResumeSeed` 固定为 SessionManager 输出的 runtime-owned 恢复种子，用于交给 checkpoint 路径组装 `ResumePlan`，但不越权替代 checkpoint version/schema 校验。

## 4. supporting types 明细

| 对象 | 边界与职责 | 数据/接口说明 |
|---|---|---|
| `SessionSnapshot` | `load_session()` 输出；只读供主循环消费 | 包含 `session_id`、`request_id`、`turn_index`、`active_checkpoint_ref`、`fsm_state`、`budget_snapshot_ref`、`pending_interaction`、`last_result_summary`；不暴露底层存储句柄 |
| `PendingInteractionState` | waiting/checkpoint 的最小恢复对象；挂在 snapshot 与 waiting checkpoint 上 | 包含 `interaction_kind`、`prompt_token`、`deadline_ms`、`blocking_reason`、`resume_channel`、`input_schema_hint`；允许为空表示当前无等待态 |
| `TurnPersistPlan` | `persist_turn()` 的 module-local 写回计划；描述 turn 收敛后的写回动作 | 包含 `session_id`、`request_id`、`turn_index`、`terminal_state`、`checkpoint_ref`、`writeback_mode`、`audit_summary`、`next_resume_seed_ref`；不直接等价于存储结果 |
| `ResumeSeed` | `build_resume_seed()` 的最小恢复种子；供 checkpoint 路径继续构造 `ResumePlan` | 包含 `session_id`、`request_id`、`checkpoint_ref`、`fsm_state`、`pending_interaction`、`policy_snapshot_ref`、`resume_reason`；禁止复制完整 `AgentRequest` |

## 5. 流程 / 时序

1. preflight：`load_session() -> SessionSnapshot`，为 `AgentOrchestrator` 提供进入本轮编排前的会话真值视图。
2. waiting 处理：`SessionSnapshot.pending_interaction -> PendingInteractionState`，驱动 waiting/clarify/confirm 的最小恢复入口。
3. turn 收敛：`persist_turn(SessionSnapshot, AgentResult draft) -> TurnPersistPlan`，由 SessionManager 内部执行持久化写回和 checkpoint anchor 更新。
4. resume 路径：`build_resume_seed() -> ResumeSeed -> CheckpointManager::make_resume_plan()`，SessionManager 只提供恢复事实，版本兼容仍由 checkpoint 侧判断。

## 6. 文件范围

1. 设计真值源更新在 [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 的 6.24.5 与 6.24.5.1。
2. 本任务交付文档落于 [docs/todos/runtime/deliverables/RT-TODO-002-SessionManager-supporting-types口径收敛.md](/home/gangan/DASALL/docs/todos/runtime/deliverables/RT-TODO-002-SessionManager-supporting-types口径收敛.md)。
3. 后续 Build 落盘目标预留为 `runtime/include/session/SessionTypes.h`、`runtime/include/session/ISessionManager.h`、`runtime/src/session/SessionManager.cpp`。

## 7. Design -> Build 映射

| Design 项 | 后续 Build 落点 |
|---|---|
| `SessionSnapshot` / `PendingInteractionState` 会话快照与等待态口径 | `runtime/include/session/SessionTypes.h`、`runtime/include/session/ISessionManager.h` |
| `TurnPersistPlan` 写回计划边界 | `runtime/include/session/SessionTypes.h`、`runtime/src/session/SessionManager.cpp` |
| `ResumeSeed` 最小恢复种子链 | `runtime/include/session/SessionTypes.h`、`runtime/src/session/SessionManager.cpp`、后续 checkpoint 调用点 |

## 8. Build 三件套

1. 代码目标：无；本任务只收敛 SessionManager supporting types 设计，不修改 runtime 生产代码。
2. 测试目标：通过文档检索确认四个 supporting types 已在 architecture/TODO/deliverable 三处形成一致口径。
3. 验收命令：
   - `rg -n "SessionSnapshot|PendingInteractionState|TurnPersistPlan|ResumeSeed" docs/architecture/DASALL_runtime子系统详细设计.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md docs/todos/runtime/deliverables/RT-TODO-002-SessionManager-supporting-types口径收敛.md`

## 9. 风险与回退

1. 如果后续 RT-TODO-010 把 `SessionSnapshot` 扩成可变持久化对象，会破坏 snapshot 与 persist plan 分层，应回退到“读取态 / 写回态分离”的口径。
2. 如果后续实现要求 `ResumeSeed` 直接承担 schema/version 兼容判断，则说明 SessionManager 越权侵入 checkpoint 责任，应把该判定回退到 `CheckpointManager::make_resume_plan()`。
3. 本任务完成后，RT-BLK-04 的 supporting types 部分可视为完成解阻；但真正解除对 RT-TODO-010 的影响，还需后续 `ISessionManager` public surface 在 Build 轮次中按本口径落盘。