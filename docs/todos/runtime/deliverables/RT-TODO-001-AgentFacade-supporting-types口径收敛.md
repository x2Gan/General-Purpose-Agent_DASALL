# RT-TODO-001 AgentFacade supporting types 口径收敛

日期：2026-04-22  
任务：RT-TODO-001  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/runtime/DASALL_runtime子系统专项TODO.md](/home/gangan/DASALL/docs/todos/runtime/DASALL_runtime子系统专项TODO.md) 将 RT-TODO-001 定义为补齐 `AgentInitRequest`、`AgentInitResult`、`HandleOptions`、`ResumeHandleRequest` 的职责、字段概要和落盘位置，用于解阻 RT-BLK-04。
2. [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 的 6.24.3 只列出 AgentFacade 的 module-local 对象名和建议接口，尚未把 supporting types 的输入边界、最小字段和未来文件落点成表。
3. 6.24.4 与 6.24.5 已明确 `AgentOrchestrator`、`SessionManager`、`CheckpointManager` 的分工，因此 AgentFacade supporting types 的目标不是承载下游细节，而是稳定 runtime public surface 的入口语义。

## 2. 外部参考

1. Temporal Workflow 文档强调，单次执行由“定义 + 执行请求”共同确定，恢复依赖既有执行实例而不是重新提交全部业务事实；这支持将 init/handle/resume 输入拆为不同的 module-local supporting types，而不是复用同一大对象。
2. Temporal 还要求 durable execution 保持 deterministic replay 约束，这意味着 `ResumeHandleRequest` 只能引用 checkpoint/session 锚点和恢复意图，不能在恢复入口再混入一份新的完整请求体。

## 3. 设计结论

1. `AgentInitRequest` 与 `AgentInitResult` 固定为 boot-time 对象，只服务 runtime 初始化与 readiness 判定。
2. `HandleOptions` 固定为 handle-scoped 元数据容器，只补充 request/session/trace/timeout 等控制平面事实，不复制 `contracts::AgentRequest` 负载。
3. `ResumeHandleRequest` 固定为“引用既有执行实例”的恢复输入，通过 `checkpoint_ref + session_id + resume_token` 绑定既有执行上下文，再由 `SessionManager` 和 `CheckpointManager` 还原最小恢复种子。
4. 这四个对象全部维持 module-local public type 身份，后续只落在 runtime/include，不进入 contracts 共享层。

## 4. supporting types 明细

| 对象 | 边界与职责 | 数据/接口说明 |
|---|---|---|
| `AgentInitRequest` | Runtime 组合根初始化输入；仅在 `AgentFacade::init()` 可见 | 包含 `runtime_instance_id`、`profile_id`、`policy_snapshot`、`dependency_set`、`boot_reason`、`cold_start`；不携带下游实现对象的可变运行态 |
| `AgentInitResult` | Runtime readiness 输出；供 apps/access 判断是否可接单 | 包含 `accepted`、`runtime_instance_id`、`resolved_profile_id`、`degraded`、`health_summary`、`error_code`、`diagnostics`；`accepted=false` 时必须 fail-closed |
| `HandleOptions` | 单次 handle 的附加控制面输入；与 `contracts::AgentRequest` 配对使用 | 包含 `request_id`、`session_id`、`caller_id`、`entrypoint`、`checkpoint_ref`、`timeout_override_ms`、`trace_context`；禁止复制业务载荷和 provider payload |
| `ResumeHandleRequest` | 单次 resume 的最小恢复输入；供 `AgentFacade::resume()` 使用 | 包含 `request_id`、`session_id`、`checkpoint_ref`、`resume_reason`、`resume_token`、`trace_context`、`override_options`；不重新声明完整请求体 |

## 5. 流程 / 时序

1. 初始化路径：`AgentInitRequest -> AgentFacade::init() -> RuntimeDependencySet ready check -> AgentInitResult`。
2. 正常请求路径：`contracts::AgentRequest + HandleOptions -> AgentFacade::handle() -> AgentOrchestrator::run_once()`。
3. 恢复路径：`ResumeHandleRequest -> AgentFacade::resume() -> SessionManager::load_session() -> CheckpointManager::make_resume_plan() -> AgentOrchestrator::continue_from_checkpoint()`。
4. 失败收敛：初始化失败返回 `AgentInitResult(accepted=false)`；handle/resume 失败折叠为标准 `AgentResult`，而不是把异常透传给 apps。

## 6. 文件范围

1. 设计真值源更新在 [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 的 6.24.3.1。
2. 本任务交付文档落于 [docs/todos/runtime/deliverables/RT-TODO-001-AgentFacade-supporting-types口径收敛.md](/home/gangan/DASALL/docs/todos/runtime/deliverables/RT-TODO-001-AgentFacade-supporting-types口径收敛.md)。
3. 后续 Build 落盘目标预留为 `runtime/include/AgentTypes.h`、`runtime/include/IAgent.h`、`runtime/src/AgentFacade.cpp`。

## 7. Design -> Build 映射

| Design 项 | 后续 Build 落点 |
|---|---|
| `AgentInitRequest` / `AgentInitResult` boot-time 口径 | `runtime/include/AgentTypes.h`、`runtime/src/AgentFacade.cpp` |
| `HandleOptions` handle-scoped 元数据边界 | `runtime/include/AgentTypes.h`、`runtime/include/IAgent.h` |
| `ResumeHandleRequest` 恢复入口口径 | `runtime/include/AgentTypes.h`、`runtime/src/AgentFacade.cpp`、后续 `SessionManager`/`CheckpointManager` 调用点 |

## 8. Build 三件套

1. 代码目标：无；本任务只收敛设计与 supporting type 口径，不修改 runtime 生产代码。
2. 测试目标：通过文档检索确认四个 supporting types 已在 architecture/TODO 范围内形成唯一口径。
3. 验收命令：
   - `rg -n "AgentInitRequest|AgentInitResult|HandleOptions|ResumeHandleRequest" docs/architecture/DASALL_runtime子系统详细设计.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md docs/todos/runtime/deliverables/RT-TODO-001-AgentFacade-supporting-types口径收敛.md`

## 9. 风险与回退

1. 如果后续 RT-TODO-005 在 `IAgent::handle()` 直接把 `HandleOptions` 扩成第二份业务请求体，应回退到“只承载控制平面元数据”的边界。
2. 如果后续 resume 实现要求 `ResumeHandleRequest` 携带完整 `AgentRequest`，则表示 checkpoint/session 恢复种子设计不足，应在 `SessionManager` / `CheckpointManager` 内补最小恢复对象，而不是回灌入口类型。
3. 本任务只完成 AgentFacade supporting types 的口径冻结，RT-BLK-04 仍需 RT-TODO-002 完成后才能整体解除。