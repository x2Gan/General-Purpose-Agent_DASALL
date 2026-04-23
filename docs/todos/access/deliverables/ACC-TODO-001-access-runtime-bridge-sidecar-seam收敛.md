# ACC-TODO-001 access-runtime bridge sidecar seam 收敛

日期：2026-04-23  
任务：ACC-TODO-001  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 将 ACC-TODO-001 定义为补齐 `RuntimeDispatchRequest`、`RuntimeInvokeContext`、`IAccessRuntimeBridge::dispatch/cancel` 的唯一口径，用于解阻 ACC-BLK-001。
2. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 6.6、6.7、6.18、6.21 已分别说明 sidecar、cancel、normalizer 与 bridge 的局部职责，但 12.2-1 仍把“独立 `RuntimeDispatchRequest` 对象”与“由 access 侧 adapter 吸收 sidecar”同时保留为未决方案。
3. [docs/ssot/CrossModuleDataProjectionMatrix.md](/home/gangan/DASALL/docs/ssot/CrossModuleDataProjectionMatrix.md) 的 6.1 与 6.2 已冻结 `RequestNormalizer` 是 runtime bridge sidecar 的唯一 projection owner，并明确 shared 面只能保留 `AgentRequest` / `AgentResult`。
4. [docs/architecture/DASALL_Agent_architecture.md](/home/gangan/DASALL/docs/architecture/DASALL_Agent_architecture.md) 当前 runtime 公开控制面只有 `IAgent::handle(...)` 与架构层 `ITaskManager::cancel(...)` 级别的抽象，没有现成可直接下沉到 `access/include` 的 runtime sidecar ABI。

## 2. 外部参考

1. OpenTelemetry Context Propagation 文档强调，跨服务传播应把 context 作为独立 carrier 传递，并在下游提取后再参与处理；同时敏感 baggage 不应直接混入业务 payload 或外部不可信边界。这与 DASALL access 侧“`AgentRequest` 保持 shared 最小面，主体/授权/发布 sidecar 继续停留 module-local”的边界一致。
   - 参考：<https://opentelemetry.io/docs/concepts/context-propagation/>

## 3. 设计结论

1. `RuntimeDispatchRequest` 冻结为 access module public handoff object，负责承载 `AgentRequest` 与 access 已裁定的最小 sidecar facts；它继续存在于 `access/include`，不再作为临时占位或待定方案。
2. `RuntimeInvokeContext` 冻结为 bridge-local invoke shape，只服务 `RuntimeBridge` 到 runtime adapter 的内部投影；它不进入 `access/include`，也不要求 runtime 立即暴露同名 public ABI。
3. `RequestNormalizer` 是 `RuntimeDispatchRequest` 的唯一 owner，`RuntimeBridge` 是 `RuntimeInvokeContext` 的唯一 owner；任何 apps/daemon/gateway 壳层都不能自行拼装 access sidecar。
4. runtime-facing seam 的 v1 口径固定为“`contracts::AgentRequest` + bridge-local invoke context + 可选 cancel stub”三段式：
   - access 对外只暴露 `IAccessRuntimeBridge::dispatch(const RuntimeDispatchRequest&)` 与 `cancel(request_id, actor_ref)`；
   - bridge 内部负责把 sidecar 投影为 runtime 可消费的调用参数；
   - runtime public surface 继续保持最小稳定面，不把 access 私有对象或 proof 直接拉进 runtime/include。
5. cancel 语义冻结为 access 先完成 ownership / policy gate，再由 `RuntimeBridge` 把 `request_id` 和 `actor_ref` 映射到 runtime cancel seam；cancel 只做转发，不转移最终裁定权。

## 4. 边界 / 职责

| 对象 | 边界与职责 | 不允许事项 |
|---|---|---|
| `RuntimeDispatchRequest` | access -> runtime bridge 的 module public handoff；承载 `AgentRequest`、主体引用、授权证明摘要、publish mode、capability view、dispatch deadline fact | 不进入 contracts；不暴露 runtime 内部类型；不携带 credential 明文、原始 header 或 peer handle |
| `RuntimeInvokeContext` | `RuntimeBridge` 内部投影对象；把 access sidecar 压缩成 runtime adapter 可消费的 invoke facts | 不上浮到 `access/include`；不被 apps 或 tests 作为第二份 shared contract |
| `IAccessRuntimeBridge::dispatch(...)` | access 唯一 runtime 调用面；接收 `RuntimeDispatchRequest`，返回 `RuntimeDispatchResult` | 不直接暴露 runtime orchestrator/session/checkpoint 细节 |
| `IAccessRuntimeBridge::cancel(...)` | access 唯一 cancel 转发面；接收 `request_id + actor_ref`，转发到 runtime cancel seam | 不做 owner 判定外的额外业务裁定；不伪造 cancel 成功 |
| `RequestNormalizer` | 生成 `RuntimeDispatchRequest` 与稳定标识；冻结 shared / module-local 边界 | 不直连 runtime；不在此处生成 runtime 专有对象 |
| `RuntimeBridge` | 生成 `RuntimeInvokeContext`，调用 runtime public seam 或 bridge-local adapter，回收同步/异步/拒绝三类结果 | 不保存长期业务状态；不吞掉 runtime reject 原因 |

## 5. 数据 / 接口说明

### 5.1 `RuntimeDispatchRequest`

1. shared 段固定为 `contracts::AgentRequest agent_request`。
2. sidecar 段固定为 access 已裁定事实：`SubjectIdentity subject_identity`、`AccessDecisionProof decision_proof`、`PublishMode publish_mode`、`ClientCapabilityView client_capability_view`、`DispatchDeadlineView dispatch_deadline`、`AccessRequestContext request_context`。
3. sidecar 只保留 runtime 需要确认、审计关联或高风险动作门禁所需的最小事实；完整 headers、credential refs、peer 原始地址、adapter 句柄继续留在 access 私有域。

### 5.2 `RuntimeInvokeContext`

1. `RuntimeBridge` 把 `RuntimeDispatchRequest` 压缩为 invoke-scoped facts，例如：`request_id`、`session_id`、`trace_id`、`actor_ref`、`operation`、`target_ref`、`decision`、`policy_decision_ref`、`publish_mode`、`dispatch_deadline`、`async_allowed`、`stream_requested`。
2. 该对象只保证 invoke 期可用，不承诺跨 session 持久化 ABI。
3. 后续若 runtime 公开接口扩展，需要优先在 bridge-local adapter 吸收，而不是反向把 `RuntimeInvokeContext` 扩进 contracts 或 runtime/include。

### 5.3 runtime-facing seam

1. 同步 dispatch：`RuntimeBridge` 通过 runtime 已有 public seam 发送 `AgentRequest`，并把 `RuntimeInvokeContext` 注入 invoke-scoped adapter 或 dependency set。
2. 异步受理：runtime 若接受异步，仅返回最小 async accept fact；receipt seed 仍由 access 侧 registry 生成和持有。
3. cancel：`RuntimeBridge::cancel(request_id, actor_ref)` 只映射到 runtime cancel stub；若 runtime 后续固化 `TaskControlRequest` 或等价对象，由 bridge-local adapter 完成对接，不改变 access public ABI。

## 6. 流程 / 时序

1. submit 路径：`InboundPacket -> SubjectResolver / Authenticator / Policy / Admission -> RequestNormalizer -> RuntimeDispatchRequest -> RuntimeBridge -> RuntimeInvokeContext -> runtime public seam`。
2. result 路径：runtime 返回 `Completed / AcceptedAsync / Rejected` 之一，`RuntimeBridge` 将其映射回 `RuntimeDispatchResult`，再交给 `ResultPublisher` 与 `AsyncTaskRegistry`。
3. cancel 路径：`receipt ownership` 与 `access.task.cancel` 授权通过后，`AccessGateway -> IAccessRuntimeBridge::cancel(request_id, actor_ref) -> bridge-local cancel adapter -> runtime cancel seam`。
4. 审计路径：`actor_ref`、`operation`、`target_ref`、`decision`、`policy_decision_ref`、`outcome` 由 `RequestNormalizer` / `RuntimeBridge` / `AccessObservabilityBridge` 分段投影，不从 runtime 结果反推 access 私有事实。

## 7. 文件范围

1. 设计真值源更新在 [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 6.6、6.18、11、12.1、12.2。
2. SSOT 投影补充更新在 [docs/ssot/CrossModuleDataProjectionMatrix.md](/home/gangan/DASALL/docs/ssot/CrossModuleDataProjectionMatrix.md) 的 6.1、6.2。
3. 本任务交付物落于 [docs/todos/access/deliverables/ACC-TODO-001-access-runtime-bridge-sidecar-seam收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-001-access-runtime-bridge-sidecar-seam收敛.md)。
4. TODO / blocker / 证据回写落于 [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 与 [docs/worklog/DASALL_开发执行记录.md](/home/gangan/DASALL/docs/worklog/DASALL_开发执行记录.md)。

## 8. Design -> Build 映射

| Design 项 | 后续 Build 落点 |
|---|---|
| `RuntimeDispatchRequest` 作为 access module public handoff | `access/include/AccessTypes.h`、`tests/unit/access/AccessSupportingTypesTest.cpp` |
| `IAccessRuntimeBridge::dispatch/cancel` 作为 access 唯一 runtime 调用面 | `access/include/IAccessRuntimeBridge.h`、`tests/unit/access/RuntimeBridgeSurfaceTest.cpp` |
| `RuntimeInvokeContext` 作为 bridge-local invoke shape | `access/src/RuntimeBridge.cpp`、runtime-facing adapter / mock |
| cancel 只转发不裁定 | `access/src/RuntimeBridge.cpp`、`tests/unit/access/AccessCancelFlowTest.cpp`、`apps/gateway/src/TaskQueryHandler.cpp` |

## 9. Build 三件套

1. 代码目标：无；本任务只完成 seam 口径冻结与 blocker 解阻，不修改 access/runtime 生产代码。
2. 测试目标：通过文档检索确认 `RuntimeDispatchRequest`、`IAccessRuntimeBridge`、`cancel()` 与 access sidecar owner 在 architecture / ssot / TODO / deliverable 中形成唯一口径。
3. 验收命令：
   - `rg -n "IAccessRuntimeBridge|RuntimeDispatchRequest|RuntimeInvokeContext|Access sidecar|cancel\(" docs/architecture/DASALL_access子系统详细设计.md docs/ssot/CrossModuleDataProjectionMatrix.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-001-access-runtime-bridge-sidecar-seam收敛.md`

## 10. 风险与回退

1. 如果后续实现把 `RuntimeInvokeContext` 直接抬升进 `access/include` 或 runtime shared public headers，会重新引入第二套 ABI，应回退到“bridge-local adapter 吸收差异”的方案。
2. 如果后续 Build 需要把完整 `SubjectIdentity` 或 `AccessDecisionProof` 写进 `AgentResult` / contracts，说明 sidecar owner 已漂移，应回退到 Cross-Module Data Projection Matrix 的 v1 冻结规则。
3. 本任务解决的是 seam 设计冻结，不等价于 runtime live route 已实现；ACC-TODO-020 仍需用 mock/stub 和窄测试证明 dispatch/cancel 行为。