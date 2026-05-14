# ACC-TODO-048 设计收敛文档

## 1. 任务定义

将 `AccessObservabilityBridge` 从“仅能构造事件对象”的局部能力收敛为真实主链可执行的 observability seam：在 request received、auth failed、policy denied、admission rejected、runtime rejected、publish failed、shutdown abandoned 等路径发出稳定事件，并确保 reject result 自带 `request_id/trace_id` 锚点；同时保持 observability sink 失败不得改变业务裁定。

## 2. 本地证据

1. 专项 TODO 将 `ACC-TODO-048` 定义为 P1-4 / R3 安全治理任务，要求 `AccessGateway.*` 与 `AccessObservabilityBridge.*` 不再停留在对象单测，而要进入真实主链失败路径。
2. `access/src/AccessGateway.cpp` 现已新增 reject result 锚点补齐逻辑：无论是 auth/policy/admission/runtime/shutdown 哪类 reject，只要 pipeline 返回结果，`submit()` 都会为 `response_context` 补齐 `request_id/session_id/trace_id`。
3. `access/src/AccessObservabilityBridge.h/.cpp` 现已新增 `emit_admission_rejected(...)`；`access/src/AccessGatewayFactory.cpp` 现已让 gateway/daemon request/auth 路径带上真实 `session_id/trace_id`，并在 admission reject 时发出专用 observability 事件。
4. `access/src/AccessGatewayFactory.cpp` 的 gateway 组合根现已接入 `shutdown_observer`，与 daemon 一样可以在 draining 超时场景发出 `shutdown_abandoned` 事件。
5. `tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp`、`AccessRejectTraceAnchorTest.cpp` 与 `AccessPublishFailureAuditTest.cpp` 已分别证明：主链 success/policy/admission 事件可观测、reject result anchors 稳定且 sink 失败不改业务裁定、runtime rejected + publish failed 事件可同时保留审计锚点。

## 3. 外部参考

1. OWASP Logging Cheat Sheet 强调安全事件应携带可关联上下文，并且日志失败不应改变主业务流程；本任务将该原则具体落为 request/trace anchors 与 sink failure non-blocking：https://cheatsheetseries.owasp.org/cheatsheets/Logging_Cheat_Sheet.html
2. W3C Trace Context 规定跨边界关联至少需要稳定 trace 标识；本任务以 `trace_id` 作为 Access reject/result/observability 的统一锚点：https://www.w3.org/TR/trace-context/
3. OpenTelemetry 对 logs / traces / metrics 的关联实践强调日志和审计事件需要共享 trace correlation 字段；本任务对应做法是让 Access 主链事件统一保留 `request_id/session_id/trace_id`，而不把高基数字段误投到 metric labels：https://opentelemetry.io/docs/concepts/signals/

## 4. 边界与职责

### 4.1 边界

1. 本任务只收敛 Access 侧 observability main-chain seam，不扩写 infra logging / metrics / tracing / audit 公共 ABI。
2. 本任务不把 observability sink 失败升级为业务失败；发送失败只影响可观测性，不改变 Admission / Policy / Runtime 裁定。
3. 本任务不把 release polish、multi-instance authoritative sync 或更广 shutdown audit 结论写成已完成；这些继续留在 051。

### 4.2 职责

| 对象 | 职责 | 非职责 |
|---|---|---|
| `AccessGateway` | 在所有 submit 返回路径上补齐 `request_id/session_id/trace_id` response anchors | 不拥有 observability sink，不直接生成事件对象 |
| `AccessObservabilityBridge` | 组装主链 observability 事件并调用注入的 emit backend | 不反向改变业务判定，不决定 runtime/policy/admission 结果 |
| `AccessGatewayFactory` | 在 gateway/daemon submit path 上触发 request/auth/policy/admission/runtime/publish/shutdown 事件 | 不扩写 apps composition root 之外的基础设施 ABI |

## 5. 数据与接口说明

1. `access/src/AccessGateway.cpp`
   - `submit()` 现在会统一调用 reject/result anchor 补齐逻辑，为返回结果补充 `request_id/session_id/trace_id`。
   - `gateway_not_ready_or_shutting_down` 这类 AccessGateway 自己生成的 reject 结果也不再丢失 trace anchors。
2. `access/src/AccessObservabilityBridge.h/.cpp`
   - 新增 `emit_admission_rejected(...)`，事件名为 `access.admission.rejected`。
   - 事件字段统一遵循 Access 现有 `request_id/session_id/trace_id/entry_type/protocol_kind/actor_ref/reason_code` 口径。
3. `access/src/AccessGatewayFactory.cpp`
   - gateway `emit_request_received()` 和 `emit_auth_failed()` 改为使用真实 `packet.session_hint/trace_id`。
   - daemon `emit_daemon_request_fact()` 与 `emit_peer_identity_denied()` 同样改为带真实 trace/session。
   - admission reject 分支现在会显式调用 `emit_admission_rejected(...)`。
   - `create_gateway_access_gateway(...)` 新增 `shutdown_observer`，与 daemon path 一样在 draining 超时场景发出 `shutdown_abandoned`。
4. `tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp`
   - 成功路径现在会断言 request event 的 `session_id/trace_id`。
   - policy unavailable 路径现在会断言 rejected result 与 policy denied event 的 trace anchors。
   - 新增 admission rejected 路径，证明 request + admission event 都被触发且 runtime 不会被调用。
5. `tests/integration/access/AccessRejectTraceAnchorTest.cpp`
   - 使用返回 `false` 的 observability backend 证明 sink failure 不会改变 auth reject 裁定；同时 rejected result 仍带 request/session/trace anchors。
6. `tests/integration/access/AccessPublishFailureAuditTest.cpp`
   - runtime rejected + publish failed 路径现在额外断言 rejected result 上的 `request_id/trace_id`。

## 6. 流程与时序

1. packet 进入 AccessGateway 后，主链先发 `request_received` / daemon request fact。
2. 若 auth 失败，则事件先发到 observability backend，业务结果仍按原 reject 语义返回；backend 返回 `false` 也不会改变 reject。
3. 若 policy denied，则发 `access.policy.denied`，并返回带 request/trace anchors 的 rejected result。
4. 若 admission reject，则发 `access.admission.rejected`，并返回带 request/trace anchors 的 rejected result。
5. 若 runtime 返回 rejected，则继续保留 `access.runtime.dispatched` 与 `access.publish.failed` 主链证据，且最终 result 仍保留 request/trace anchors。
6. 若 gateway/daemon 在 draining timeout 时仍有 inflight 请求，则通过 `shutdown_observer` 发出 `shutdown_abandoned`。

## 7. Design -> Build 映射

| 设计项 | Build 落点 | 完成判定 |
|---|---|---|
| reject result trace anchors | `access/src/AccessGateway.cpp`、`tests/integration/access/AccessRejectTraceAnchorTest.cpp` | auth/policy/admission/runtime reject 结果不再丢失 `request_id/trace_id` |
| admission rejected 主链事件 | `access/src/AccessObservabilityBridge.*`、`access/src/AccessGatewayFactory.cpp`、`tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp` | admission reject 会发专用事件且 runtime 不会被调用 |
| request/auth 真实 trace/session 字段 | `access/src/AccessGatewayFactory.cpp`、`tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp` | request/auth 事件携带真实 `session_id/trace_id` |
| sink failure non-blocking | `tests/integration/access/AccessRejectTraceAnchorTest.cpp` | observability backend 返回 `false` 不改变 reject 裁定 |
| runtime rejected + publish failed 主链证据 | `tests/integration/access/AccessPublishFailureAuditTest.cpp` | publish failed 审计事件不掩盖 runtime reject，且 reject 结果仍带 anchors |

## 8. 文件范围

1. `access/src/AccessGateway.cpp`
2. `access/src/AccessObservabilityBridge.h`
3. `access/src/AccessObservabilityBridge.cpp`
4. `access/src/AccessGatewayFactory.cpp`
5. `tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp`
6. `tests/integration/access/AccessRejectTraceAnchorTest.cpp`
7. `tests/integration/access/AccessPublishFailureAuditTest.cpp`
8. `tests/integration/access/CMakeLists.txt`
9. `tests/CMakeLists.txt`
10. `docs/todos/access/DASALL_access子系统专项TODO.md`
11. `docs/todos/access/deliverables/ACC-TODO-048-AccessObservabilityBridge-主链与基础设施sink接线.md`
12. `docs/worklog/DASALL_开发执行记录.md`

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|
| B1 | 为所有 reject result 补齐 request/session/trace anchors | `AccessRejectTraceAnchorTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessRejectTraceAnchorTest" --output-on-failure` |
| B2 | 将 request/auth/policy/admission/runtime/publish observability 接入真实主链 | `AccessObservabilityMainChainIntegrationTest`、`AccessPolicyBackendUnavailableIntegrationTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessObservabilityMainChainIntegrationTest|AccessPolicyBackendUnavailableIntegrationTest" --output-on-failure` |
| B3 | 保持 runtime rejected + publish failed 路径的 audit 证据与主结果不互相掩盖 | `AccessPublishFailureAuditTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessPublishFailureAuditTest" --output-on-failure` |

## 10. 验收结果

1. `Build_CMakeTools(buildTargets=["dasall_access_observability_main_chain_integration_test","dasall_access_reject_trace_anchor_integration_test","dasall_access_publish_failure_audit_integration_test"])`
   - 结果：通过。
   - 说明：受影响的 `dasall_access`、observability main-chain integration、reject-anchor integration 与 publish-failure integration 目标均成功重编。
2. `RunCtest_CMakeTools(tests=["AccessObservabilityMainChainIntegrationTest","AccessRejectTraceAnchorTest","AccessPublishFailureAuditTest"])`
   - 结果：通过，3/3 passed。
3. `RunCtest_CMakeTools(tests=["AccessPolicyBackendUnavailableIntegrationTest"])`
   - 结果：通过，1/1 passed。
4. 额外观察
   - `AccessObservabilityMainChainIntegrationTest` 与 `AccessRejectTraceAnchorTest` 现已进入 `gate-int-08 / access-v1-production-gate` 标签族，048 的 focused evidence 已并入现行 gate 口径。
   - gateway shutdown observer 已接线，但更广 shutdown audit / cache-registry release polish 仍留给 051。

## 11. D Gate 结果

Gate = PASS。

1. `AccessObservabilityBridge` 已从对象单测能力收敛为真实主链可执行的 request/auth/policy/admission/runtime/publish/shutdown observability seam。
2. reject result 的 `request_id/trace_id` anchor、sink failure non-blocking、publish failure audit correlation 均已具 focused integration 证据。
3. 本轮未把 release polish、multi-instance authoritative sync 或更广 shutdown audit 写成已完成；这些继续留在 051 风险面。
