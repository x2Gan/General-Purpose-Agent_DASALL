# DASALL Access 子系统交付评审报告

评审日期：2026-04-24
评审范围：`docs/todos/access/deliverables/*`、`access/*`、`tests/`（重点为 `tests/unit/access` 与 `tests/integration/access`）
评审角色：C++ / Agent 子系统交付评审
评审结论版本：v1.0

## 1. 执行摘要

### 1.1 总体结论

Access 子系统的架构方向是正确的：`access/` 作为共享 Access Core，`apps/*` 作为入口壳层，Runtime 继续持有主控权，`AgentRequest` / `AgentResult` 作为 shared 主链对象，主体、授权证明、异步回执和发布上下文保持 module-local sidecar。这一方向与总架构和 Access 详设一致。

但当前交付代码尚不能判定为 v1 可交付或 Build-Ready。核心原因不是缺少零散类，而是“类已存在，主链未装配”：`AccessGateway` 当前只是一个可注入函数 facade，默认构造后没有 resolver/auth/policy/admission/validator/normalizer/runtime/publisher 主链；daemon 和 gateway 组合根也以空 pipeline 构造 `AccessGateway`。因此 unary submit、async receipt、query/cancel、publish failure fallback 这些 TODO 目标尚未形成可运行闭环。

### 1.2 评分

| 维度 | 评分 | 结论 |
|---|---:|---|
| 架构与边界方向 | 8.5 / 10 | 设计原则清晰，Access / Runtime / Contracts / Platform 边界基本正确 |
| 设计收敛文档 | 7.0 / 10 | 001~030 多数有交付物，但 034~036 仍 Blocked，部分 Done 结论与代码现实不一致 |
| `access/` 代码结构 | 6.0 / 10 | 组件文件齐备，接口和单组件逻辑可读，但缺 production pipeline 和真实 config/policy/runtime 接线 |
| 安全与治理 | 5.0 / 10 | fail-closed 思路存在，但 HMAC、策略后端、审计、override/diagnostics 仍未工程闭环 |
| 测试有效性 | 4.0 / 10 | 单测数量多，但 integration smoke 弱且 clean rebuild 存在失败；缺端到端主链 Gate |
| 发布就绪度 | 3.0 / 10 | 不建议作为 Access v1 release baseline，需要先完成 P0 修复 |

最终判定：**Design Direction Ready，Implementation Not Ready，Release Gate Not Closed**。

## 2. 评审依据

### 2.1 本地依据

1. `docs/architecture/DASALL_Agent_architecture.md`
2. `docs/architecture/DASALL_access子系统详细设计.md`
3. `docs/todos/access/DASALL_access子系统专项TODO.md`
4. `docs/todos/access/deliverables/*`
5. `access/include/*`、`access/src/*`
6. `tests/unit/access/*`、`tests/integration/access/*`
7. 相关入口壳层代码：`apps/cli`、`apps/daemon`、`apps/gateway`、`apps/simulator`

### 2.2 行业最佳实践参照

1. 接入层采用清晰 pipeline：decode -> identity -> authn -> authz -> admission -> validation -> normalization -> runtime bridge -> publish。
2. 授权与高风险入口遵循 deny-by-default、per-request authorization、fail-closed、最小权限和可审计。
3. 异步 receipt ownership token 应使用稳定、可轮换、抗碰撞的 MAC/HMAC，不使用 `std::hash` 或隐式默认 secret。
4. 网关解析与编码应优先使用结构化 parser，避免自写 JSON 扫描导致转义、嵌套和注入问题。
5. 健康检查 readiness 应反映真实依赖状态，而不是硬编码 ready。
6. 集成门应在 clean build 后可重复通过，避免依赖 stale binary。

## 3. 正向发现

1. Access 的系统级定位正确。总架构明确 Product & Access Layer 负责协议转换、认证鉴权、输入规范化并形成 `AgentRequest` 交给 Runtime；Access 详设也明确 `access/ + apps/*` 双层落点。
2. `access/include` 已经补齐主要 public surface：`IAccessGateway`、`IAccessRuntimeBridge`、`IAdmissionController`、`IProtocolAdapter`、`AccessTypes`、`AccessErrors`。
3. 关键组件已形成代码文件和基础单测：`SubjectResolver`、`AuthenticatorChain`、`AccessPolicyGate`、`AdmissionController`、`RequestValidator`、`RequestNormalizer`、`RuntimeBridge`、`ResultPublisher`、`AsyncTaskRegistry`、`ResultReplayCache`、`AccessObservabilityBridge`。
4. streaming 延后策略是正确的。当前没有把 WS/MQTT/StreamGateway 伪装成 v1 ready，这一点符合 Access 详设中的 `ACC-GATE-11` 思路。
5. 单组件层面已有不少 fail-closed 测试，例如 policy backend unavailable、payload limit、header injection、ownership mismatch、receipt expiry。

## 4. P0 阻断问题

### P0-1：AccessGateway 未装配设计主链，当前默认 submit 只会走空 pipeline

证据：

1. `access/src/AccessGateway.h` 只保存 `SubmitPipeline` 和 `PublishBackend` 两个函数对象，没有持有 resolver/auth/policy/admission/normalizer/runtime/publisher 等依赖。
2. `access/src/AccessGateway.cpp` 的 `submit()` 只检查状态、创建 inflight guard，然后调用 `run_submit_pipeline()`。
3. `run_submit_pipeline()` 在 pipeline 为空时返回 `submit_pipeline_not_configured`。
4. `apps/daemon/src/main.cpp` 与 `apps/gateway/src/main.cpp` 都使用默认构造 `std::make_shared<dasall::access::AccessGateway>()`，没有注入任何主链。

影响：

1. 设计要求的 Admission pipeline 没有在 production path 串起来。
2. daemon/gateway 的业务 submit 默认会被拒绝。
3. 单测可以通过函数注入模拟成功，但不能证明真实 Access 主链 ready。

建议：

1. 新增明确的 `AccessPipeline` 或 `AccessGatewayDependencies`，包含 registry、resolver、authenticator、policy_gate、admission、validator、normalizer、runtime_bridge、publisher、async_registry、replay_cache、observability、config views。
2. `AccessGateway::init()` 必须验证依赖完整性，缺任一 P0 依赖时 fail-closed，而不是进入 Ready。
3. `submit()` 内部按详设顺序执行主链，并统一映射各阶段错误。
4. `apps/daemon` / `apps/gateway` 组合根必须构造真实 pipeline 或显式测试 profile mock pipeline。

### P0-2：集成门未闭合，当前 integration smoke 不是端到端 Access 主链验证

证据：

1. `docs/todos/access/deliverables/ACC-TODO-034-CLI-daemon-unary-smoke+async-receipt集成门计划.md` 状态仍是 `Blocked`。
2. `ACC-TODO-035`、`ACC-TODO-036` 也仍为 `Blocked`。
3. `tests/integration/access/AccessGatewaySmokeIntegrationTest.cpp` 只检查 `IAccessGateway` 是 abstract 和 `PublishEnvelope` 字段，没有启动 gateway、daemon、pipeline 或 RuntimeBridge。
4. `tests/integration/access/CliDaemonPingIntegrationTest.cpp` 只调用 `CliIpcClient::ping_daemon()`，未证明 daemon 进程、AccessGateway 主链、admission 或 runtime dispatch。
5. clean rebuild 全量 Access 测试目标时，`AccessGatewaySmokeIntegrationTest.cpp` 因 C++20 designated initializer 字段顺序不匹配而编译失败；此前 `ctest` 通过的是已有 stale binary，不能作为交付证据。

影响：

1. `ACC-G4` “至少 1 个 access integration 用例被 `ctest -N` 发现并通过”未形成可靠证据。
2. `CLI -> daemon -> access -> runtime -> publish` 和 `HTTP -> gateway -> access` 均没有真实闭环验证。

建议：

1. 先修复 `AccessGatewaySmokeIntegrationTest.cpp` 的字段初始化顺序，保证 clean rebuild 可通过。
2. 将 smoke 改为真实构造 `AccessGateway` + mock RuntimeBridge + mock Publisher，提交一个 `InboundPacket`，断言 `AgentRequest` 生成、RuntimeBridge 被调用、PublishEnvelope 返回。
3. `CliDaemonPingIntegrationTest` 不应以绕过 AccessGateway 的 ping 作为 Access integration gate；应增加 submit/query/cancel 端到端用例。
4. 建议新增 aggregate target：`dasall_access_tests`，避免每次手工列 60+ 个 target。

### P0-3：RuntimeDispatchRequest 与 RuntimeBridge 没有真正承载 `AgentRequest`

证据：

1. Access 详设要求 Access 基于 contracts 生成 `AgentRequest` 并提交给 Runtime。
2. `RequestNormalizer::project_agent_request()` 确实生成了 `dasall::contracts::AgentRequest`。
3. 但 `access/include/AccessTypes.h` 中 `RuntimeDispatchRequest` 只包含 `InboundPacket`、`SubjectIdentity`、`AccessDecisionProof` 和 context map，没有 `AgentRequest` 字段。
4. `RuntimeBridge::dispatch()` 接收的也是 `RuntimeDispatchRequest`，无法保证真实 runtime-facing seam 得到 shared `AgentRequest`。

影响：

1. normalizer 生成的 `AgentRequest` 没有进入 RuntimeBridge public seam，存在“测试里生成了，但主链上没传”的断裂。
2. RuntimeBridge 目前更像 access request stub，不是 access -> runtime contract handoff。

建议：

1. 二选一收敛：要么 `RuntimeDispatchRequest` 内显式包含 `dasall::contracts::AgentRequest agent_request`，要么 RuntimeBridge 接口改为 `dispatch(const AgentRequest&, const RuntimeInvokeContext&)`。
2. `RequestNormalizationOutput` 的 `agent_request` 必须被 AccessGateway 主链消费并传入 RuntimeBridge。
3. 增加 contract compatibility 集成测试：验证 RuntimeBridge mock 收到的 `AgentRequest.request_id/session_id/trace_id/user_input/request_channel` 与 normalizer 输出一致。

### P0-4：async receipt query / cancel 未接入 gateway/daemon，cancel 没有转发 RuntimeBridge

证据：

1. `TaskQueryHandler` 存在，但 `apps/gateway/src/main.cpp` 只注册 `/health/*`、OPTIONS 和 `POST /v1/submit`。
2. `TaskQueryHandler::handle_cancel()` 只调用 `registry_.mark_completed(id, "cancelled")`，注释也写明 RuntimeBridge cancel 转发在 Phase A4 补充。
3. daemon 入口也没有 query/cancel route。

影响：

1. TODO 中要求的 async receipt/query/replay/受控 cancel 不是入口可用能力。
2. cancel 语义停留在本地 registry 状态修改，没有把取消事实交给 Runtime。

建议：

1. gateway 增加 `/v1/receipt/{id}` 与 `/v1/cancel/{id}`，daemon 增加对应 IPC operation。
2. query/cancel 入口仍需过 actor extraction + policy gate；owner token 只解决 receipt possession，不等于授权。
3. `TaskQueryHandler::handle_cancel()` 应调用 `IAccessRuntimeBridge::cancel(request_id, actor_ref)`，registry 状态更新以 runtime cancel 结果为准。

### P0-5：HTTP adapter 首版解码不满足主链输入要求

证据：

1. `HttpProtocolAdapter::decode()` 设置了 `packet_id`、`entry_type`、`peer_ref`、`payload`，但没有设置 `protocol_kind`。
2. `RequestNormalizer` 和 `RequestValidator` 都要求 `packet.protocol_kind` 非空。
3. JSON 解析为朴素字符串扫描，不处理转义、嵌套、数组或恶意输入。

影响：

1. 通过 HTTP adapter 进入主链的请求会缺失 `protocol_kind`，在 validator/normalizer 阶段被拒绝。
2. 自写 JSON 扫描会给 payload、header、idempotency 等字段引入解析不一致和注入风险。

建议：

1. HTTP decode 固定设置 `entry_type="gateway"`、`protocol_kind="http_unary"`，不要信任 body 内自报 entry。
2. 使用项目已有结构化序列化/JSON 工具；若项目暂无，至少为首版引入小范围 parser wrapper，不在 adapter 内写 ad hoc scanner。
3. POST `/v1/submit` 应校验 method/path/content-type/body-size/idempotency-key，并把 header 白名单投影进 `request_context`。

## 5. P1 高优先级问题

### P1-1：AccessConfigAdapter 仍是占位，bootstrap/profile/policy 投影没有代码实现

`access/src/AccessConfigAdapter.cpp` 只有预留注释。设计要求 `AccessBootstrapConfig` 和运行期 profile/policy snapshot 生成 immutable governance views，但当前没有 parser、projection、hot update、last-known-good 或 fail-closed 初始化逻辑。

建议补一个真实 `AccessConfigAdapter.{h,cpp}`，至少支持：

1. bootstrap config -> `AccessAuthView` / `AccessAdmissionView` / `AccessPublishView`。
2. profile/policy snapshot -> `AccessRuntimeGovernanceView`。
3. fingerprint 变更检测。
4. schema 缺失或非法时 fail init，而不是使用散落默认值。

### P1-2：ownership token 使用 `std::hash` + 静态默认 secret，不满足 HMAC 设计要求

`AsyncTaskRegistry::build_ownership_token()` 注释写明“后续可替换为正式 HMAC”，当前实现是 `std::hash<std::string>`。这不是稳定、抗碰撞、跨进程可移植的 MAC；默认 secret 为空时还会落到 `"access-static-secret-v1"`。

建议：

1. 使用 infra/secret 提供的 deployment secret，缺失则禁用 async receipt 或 init fail-closed。
2. token 格式包含 `key_id`、`issued_at`、`expires_at`、`receipt_id`、`actor_ref`、`request_id`。
3. 使用 HMAC-SHA256 或项目安全库封装，输出 base64url。
4. 支持 secret rotation：验证当前 key 和 previous key，签发只用 current key。

### P1-3：策略门仍是 `PolicyBackendSnapshot` stub，没有接 infra/policy

`AccessPolicyGate` 的策略后端是本地 bool snapshot：`allow_submit`、`allow_task_query`、`allow_override`。这有利于单测，但不等同于真实 policy evaluation。

建议：

1. 定义 `IAccessPolicyEvaluator` seam，production 实现调用 infra/policy，单测实现用 snapshot。
2. policy input 明确包含 subject/channel/environment/operation/target/fingerprint。
3. policy backend unavailable 必须映射为 fail-closed，并发出 audit + metric。

### P1-4：observability bridge 未接入主链

`AccessObservabilityBridge` 已能构造事件，但 `AccessGateway` submit/publish/shutdown 路径没有调用它。当前单测只能证明事件对象字段，不证明真实失败路径可观测。

建议：

1. 在 request received、auth failed、policy denied、admission rejected、runtime rejected、publish failed、shutdown abandoned 等路径发事件。
2. 同步接 logging/metrics/tracing/audit bridge，至少以接口注入方式实现。
3. 所有 reject result 携带 request_id/trace_id，避免拒绝路径不可追踪。

### P1-5：health readiness 被硬编码 ready，不能反映真实依赖

`apps/gateway/src/main.cpp` 初始化 `HealthProbeHandler` 后直接 `set_started(true)` 和 `set_ready(true)`。这与详设中 readiness 需要 `AccessGatewayState == Ready`、adapter 已注册、RuntimeBridge 可达的定义不一致。

建议：

1. readiness 从 `AccessGateway::is_ready()`、registry binding 数、RuntimeBridge health、policy/config health 聚合而来。
2. init 缺 pipeline 时 readiness 必须 false。
3. health integration test 应启动真实 handler，模拟 runtime unavailable / policy unavailable。

### P1-6：daemon ping 绕过 AccessGateway，不能作为本地控制面主链证据

`DaemonBootstrap::handle_connection()` 对 `packet_id == "ping"` 直接构造 pong 并发送，绕过 AccessGateway。这个可以作为 process liveness，但不能作为 Access 链路 smoke。

建议：

1. 保留 `/ping` 或 IPC ping 作为 liveness，但单独标注“不证明 access admission”。
2. 新增 `submit` IPC operation，经 `DaemonProtocolAdapter -> AccessGateway -> RuntimeBridge mock -> ResultPublisher` 完整闭环。

### P1-7：deliverables 状态与实际交付证据不一致

`ACC-TODO-031/032/033` 文档均声明大量单元门 Done；`ACC-TODO-036` 又明确 6 个集成门 Pending，且自身 Blocked。当前已有 `docs/todos/access/Access子系统交付评估报告.md` 给出“代码落地质量优秀、已达到 Build-Ready”的结论，与本次代码和测试验证不一致。

建议：

1. 将现有 `Access子系统交付评估报告.md` 标注为旧稿或替换为本报告结论。
2. TODO 状态按真实门禁拆分：unit component Done、composition Not Ready、integration Blocked、release Not Ready。
3. 每个 Done deliverable 重新补“最新复验日期、clean build 命令、结果摘要”。

## 6. P2 中优先级问题

1. `access/CMakeLists.txt` 仍编译 `src/placeholder.cpp`，应在静态库不再为空后删除。
2. `AccessTypes.h` 中较多字段使用自由字符串表达枚举语义，例如 `decision`、`subject_type`、`auth_method`、`trust_level`、`protocol_status_hint`。建议为内部强语义字段引入 enum class 或集中常量，避免拼写漂移。
3. `AccessGateway::shutdown()` 当前只等待 inflight，并未写 abandoned audit、未处理 receipt expiry、未关闭 adapter registry/cache。
4. `ResultPublisher::map_agent_result_to_protocol()` 用 `IdempotencyReplayHit` 作为 completed success 的 code，语义不够干净。建议增加 `AccessErrorCode::Ok` 或让 success mapping 与 error mapping 分离。
5. `RequestNormalizer::generate_stable_id()` 包含 process-local counter，不适合跨进程重放稳定性；建议接入统一 ID generator。
6. `ProtocolAdapterRegistry` registry 本身可用，但 AccessGateway 未使用 registry 进行 decode/encode，registry 价值尚停留在单测层。
7. `apps/gateway/src/main.cpp` 直接 include `AccessGateway.h` internal header，组合根可以这样做，但应明确这是 app-private composition 权限，不能让业务 handler 直接依赖 internal 组件。

## 7. 测试评估

### 7.1 已执行验证

| 命令 | 结果 | 评审解释 |
|---|---|---|
| `cmake --build build-ci --target dasall_access` | 通过 | access 静态库本身可编译 |
| `cmake --build build-ci --target <全部 Access 测试目标>` | 失败 | clean rebuild 在 `AccessGatewaySmokeIntegrationTest.cpp` designated initializer 处失败 |
| `ctest --test-dir build-ci -L access --output-on-failure` | 62 个测试可发现；在未完整构建时 16 个 Not Run | discoverability 存在，但测试运行依赖预先构建，且不是 clean gate |
| `ctest --test-dir build-ci -R "AccessGatewaySmokeIntegrationTest|CliDaemonPingIntegrationTest"` | 通过 | 该通过依赖既有二进制，不能抵消 clean rebuild 失败；测试内容也不是端到端主链 |

### 7.2 测试覆盖结论

| 测试层 | 当前状态 | 结论 |
|---|---|---|
| 单组件单测 | 覆盖较多 | 可以作为组件局部行为证据 |
| AccessGateway facade 单测 | 主要测试函数注入和生命周期 | 不能证明真实 pipeline |
| Adapter 单测 | HTTP/daemon/simulator 均有 | 解析与主链输入仍有缺口 |
| Integration | 仅骨架级 | 不能证明 v1 交付链路 |
| Contract regression | 未作为 Access gate 汇聚 | 需要补 Access 相关 contract guard 命令 |
| Failure/observability/profile | 计划中 | 尚未交付 |

## 8. 设计收敛复核

### 8.1 已收敛且方向正确

1. Access Channel owner：正确。
2. Runtime 不被 Access 替代：正确。
3. `SubjectIdentity` / `AccessDecisionProof` / receipt 保持 module-local：正确。
4. Streaming 延后 Gate：正确。
5. HTTP-only 首版边界：正确。
6. CLI 纯客户端、daemon 本地 owner：方向正确。

### 8.2 需要重新收敛

1. RuntimeBridge handoff object：当前 `RuntimeDispatchRequest` 没有携带 `AgentRequest`，需重新冻结。
2. AccessGateway production dependencies：需要从函数注入测试 facade 收敛到真实 pipeline composition。
3. Config projection：`AccessConfigAdapter` 需要真实落地，不应继续停在 translation unit 占位。
4. Async ownership：HMAC、secret source、rotation、multi-instance 行为需要冻结。
5. Integration Gate：034~036 应从 Blocked 文档变成可执行测试，而不是只做计划。

## 9. 建议修复路线

### R0：先闭合可重复构建门

1. 修复 `AccessGatewaySmokeIntegrationTest.cpp` 初始化顺序。
2. 新增 `dasall_access_tests` aggregate target。
3. 在 CI / 本地统一使用 clean build + `ctest -L access`，避免 stale binary。
4. 回写 TODO：明确当前 release gate Not Ready。

### R1：实现真实 AccessGateway 主链

1. 定义 `AccessGatewayDependencies` 或 `AccessPipeline`。
2. 将 resolver/auth/policy/admission/validator/normalizer/runtime/publisher/async/obs/config 注入 AccessGateway。
3. `init()` 验证依赖和 config，缺失即 fail-closed。
4. `submit()` 串联完整 pipeline，并补 reject/publish/receipt 映射。
5. apps/daemon 和 apps/gateway 使用真实 composition root，不再默认空 gateway。

### R2：打通 Runtime 和 async receipt

1. 让 RuntimeBridge 接收 `AgentRequest`。
2. mock RuntimeBridge 返回 Completed / AcceptedAsync / Rejected 三类结果。
3. AccessGateway 在 AcceptedAsync 时注册 `AsyncTaskReceipt`，publish/query 路径可查。
4. cancel 经 ownership + policy 后转发 RuntimeBridge。

### R3：补安全治理

1. ownership token 改为正式 HMAC。
2. 策略门接 `IAccessPolicyEvaluator`，production 接 infra/policy。
3. override/diagnostics 按 schema 和 allow proof 实现 deny-by-default。
4. 观测事件接 logging/metrics/tracing/audit。

### R4：补入口与集成测试

1. HTTP adapter 使用结构化 parser，设置固定 protocol_kind。
2. gateway 增加 receipt query/cancel route。
3. daemon submit/query/cancel 走 AccessGateway。
4. 集成测试覆盖 CLI -> daemon、HTTP -> gateway、async receipt query/cancel、publish failure、policy backend unavailable。

### R5：更新交付文档

1. 修正 `Access子系统交付评估报告.md` 的 Build-Ready 结论。
2. 034/035/036 从计划文档推进到测试证据文档。
3. 所有 Done deliverable 增加最新复验结果。

## 10. 建议验收清单

### 必须通过后才可称为 Access v1 可交付

1. `cmake --build build-ci --target dasall_access`
2. `cmake --build build-ci --target dasall_access_tests`
3. `ctest --test-dir build-ci -L access --output-on-failure`
4. `ctest --test-dir build-ci -R "AgentRequestContractTest|AgentResultContractTest|IdentityMetadataContractTest" --output-on-failure`
5. 新增并通过：`AccessGatewayPipelineIntegrationTest`
6. 新增并通过：`CliDaemonSubmitIntegrationTest`
7. 新增并通过：`AccessAsyncReceiptQueryCancelIntegrationTest`
8. 新增并通过：`AccessObservabilityIntegrationTest`
9. 新增并通过：`AccessHealthReadinessIntegrationTest`
10. 新增并通过：`AccessPolicyBackendUnavailableIntegrationTest`

### 交付判定

当前不建议将 Access 子系统标注为“已达到 Build-Ready”。更准确的状态应为：

| 子域 | 当前状态 |
|---|---|
| 设计方向 | Ready |
| public ABI / supporting types | Mostly Ready |
| 单组件实现 | Partially Ready |
| production pipeline | Not Ready |
| daemon/gateway 主链组合根 | Not Ready |
| async receipt query/cancel E2E | Not Ready |
| observability / profile / policy 集成 | Not Ready |
| release gate | Blocked |

## 11. 关键行动项

| 优先级 | 行动项 | 负责人建议 | 完成标准 |
|---|---|---|---|
| P0 | 修复 integration clean rebuild 失败 | Access 测试 owner | Access 测试目标 clean build 通过 |
| P0 | 实现 AccessGateway production pipeline | Access core owner | submit 真实串联 8+ 阶段并有集成测试 |
| P0 | RuntimeBridge handoff 携带 AgentRequest | Access + Runtime owner | mock runtime 收到完整 AgentRequest |
| P0 | 接通 async receipt query/cancel | Access + gateway/daemon owner | receipt query/cancel 集成测试通过 |
| P1 | ownership token 改正式 HMAC | Access + infra/secret owner | token 支持 key rotation，默认 secret 禁止 |
| P1 | AccessConfigAdapter 真实现 | Access + profiles/config owner | config projection 单测 + profile 集成通过 |
| P1 | Observability 接入主链 | Access + infra owner | 三类关键事件在集成测试中可断言 |
| P1 | 更新 deliverables 状态 | Access 文档 owner | TODO 与报告不再宣称 Build-Ready |

---

评审签署：当前 Access 子系统具备继续迭代的良好结构基础，但交付代码尚未完成主链闭环。建议先集中处理 P0，再进入安全治理和集成门收口。
