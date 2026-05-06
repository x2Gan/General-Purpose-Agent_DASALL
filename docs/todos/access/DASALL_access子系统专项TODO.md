# DASALL Access 子系统专项 TODO

最近更新时间：2026-05-06
阶段：Detailed Design -> Special TODO
适用范围：access/、apps/cli、apps/daemon、apps/gateway、apps/simulator、tests/unit/access、tests/integration/access
当前结论：Access 子系统的架构方向与 `access/ + apps/*` 双层工程落点正确；2026-04-24 交付评审给出的 Design Direction Ready / Implementation Not Ready / Release Gate Not Closed 仍保留为历史基线。自 2026-05-06 起，Access v1 unary focused ingress 已通过 `Gate-INT-08` 固化为当前 build 可执行证据，覆盖 CLI->daemon、HTTP->gateway、async receipt、policy backend unavailable、health readiness、profile/contracts guard；后续不得再把 mock pipeline、ping liveness 或局部 envelope 字段写成 release 证据，且更广 release 结论仍需继续受 051 等残余风险约束。

## 1. 文档头

本文档严格基于以下输入生成：

1. `docs/architecture/DASALL_access子系统详细设计.md`
2. `docs/architecture/DASALL_Agent_architecture.md`
3. `docs/architecture/DASALL_Engineering_Blueprint.md`
4. `docs/adr/ADR-005-architecture-review-baseline.md`
5. `docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md`
6. `docs/ssot/CrossModuleDataProjectionMatrix.md`
7. `docs/ssot/InfraConcurrencyPolicy.md`
8. `docs/ssot/InfraIntegrationTopology.md`
9. `docs/plans/DASALL_工程落地实现步骤指引.md`
10. `docs/development/DASALL_工程协作与编码规范.md`
11. `docs/todos/contracts/deliverables/WP02-T009-标识元数据规范.md`
12. `docs/todos/contracts/deliverables/WP03-T002-AgentRequest语义说明.md`
13. `docs/todos/contracts/deliverables/WP03-T014-AgentResult语义说明.md`
14. 现有专项 TODO 基线：`docs/todos/runtime/DASALL_runtime子系统专项TODO.md`、`docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`、`docs/todos/llm/DASALL_llm子系统专项TODO.md`、`docs/todos/services/DASALL_capability_services子系统专项TODO.md`
15. 当前代码与构建现状：`access/CMakeLists.txt`、`access/include/AccessTypes.h`、`access/include/IAccessGateway.h`、`access/include/IAccessRuntimeBridge.h`、`access/include/IProtocolAdapter.h`、`access/src/placeholder.cpp`、`apps/cli/CMakeLists.txt`、`apps/cli/src/main.cpp`、`apps/gateway/CMakeLists.txt`、`apps/gateway/src/main.cpp`、`tests/CMakeLists.txt`、`tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt`
16. 交付评审输入：`docs/todos/access/DASALL_Access子系统交付评审报告.md`，评审结论为 P0/P1/P2 分级整改，重点是“类已存在，主链未装配”。

行业补强参照采用 Access 详设已内化的参考基线，不额外改写边界：

1. Envoy HTTP filters：有序 decoder/encoder pipeline 与 terminal handler 思路。
2. OWASP Authorization Cheat Sheet：deny-by-default、逐请求授权、fail-closed 和审计基线。
3. IETF `Idempotency-Key` header draft：HTTP 幂等键格式与窗口语义。
4. 接入层生产化通用实践：结构化协议解析、真实依赖 readiness、HMAC ownership token、clean rebuild 可重复验证、禁止以 stale binary 或 liveness ping 冒充业务主链证据。

编制原则：

1. 不改写已冻结 ADR、SSOT、contracts 结论。
2. 不把 Access 扩张为 Runtime、LLM、Tools、Services、Memory、Knowledge 的替代实现。
3. 设计证据不足处先补设计或标记 Blocked，不伪造 Done-ready Build 任务。
4. 每项任务都必须具备代码目标、测试目标、验收命令三件套。
5. 每项任务只承载一个主目标，测试类任务允许围绕单一 Gate 打包同类用例。
6. 所有任务均需可追溯到详设章节、现有代码现状、SSOT 或相邻冻结交付物。

## 2. 子系统目标与范围

### 2.1 子系统目标

依据 Access 详设 1.1、架构 3.4.1 / 5.7、蓝图 3.2，Access 子系统的工程目标固定为：

1. 以 `access/ + apps/*` 双层工程落点承接 Layer 7 Access Channel，不把共享接入 core 回塞到 `apps/`。
2. 统一完成入口协议适配、主体识别、认证、授权、限流、幂等、输入归一化与结果发布，而不是让四个入口各自拼装链路。
3. 复用已冻结 `AgentRequest` / `AgentResult` 作为 shared 主链对象，把主体、授权证明、发布上下文和异步状态保持为 module-local sidecar。
4. 打通 unary 同步返回、async receipt/query/replay、受控 cancel、publish failure fallback 的最小可交付主链。
5. 为 `access/include`、`access/src`、`apps/*/src`、`tests/unit/access`、`tests/integration/access`、CMake 和质量门提供可执行落盘计划。

### 2.2 纳入范围

1. `access/include` 公共接口与 supporting types。
2. `access/src` 内的 `ProtocolAdapterRegistry`、`SubjectResolver`、`AuthenticatorChain`、`AccessPolicyGate`、`AdmissionController`、`RequestValidator`、`RequestNormalizer`、`RuntimeBridge`、`ResultPublisher`、`AsyncTaskRegistry`、`ResultReplayCache`、`AccessConfigAdapter`、`AccessObservabilityBridge`、`AccessGateway`。
3. `apps/cli`、`apps/daemon`、`apps/gateway`、`apps/simulator` 的 entry-specific adapter 与组合根。
4. `tests/unit/access` 与 `tests/integration/access` 的 discoverability、smoke、failure、profile、security gates。
5. Access 相关 Gate、blocker、deliverables 与 worklog 回写策略。

### 2.3 不纳入范围

1. Runtime FSM、预算、恢复、调度与最终执行裁定。
2. LLM provider 协议、Prompt 组装、Context 裁剪与 Knowledge 检索实现。
3. Services 内部 RPC、Field Channel/HAL、现场总线驱动与设备总线串口语义。
4. 任何 shared contracts admission 扩张；`SubjectIdentity`、`AccessDecisionProof`、`AsyncTaskReceipt` 继续保持 module-local。
5. 在 shared streaming lifecycle 未冻结前，把 WS/MQTT 全量流式语义作为 v1 硬交付门槛。

## 3. 输入依据与约束清单

### 3.1 约束清单

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| ACC-TC001 | Access 详设 1.1、2.1；架构 5.7 | Must | Access 必须是 Access Channel 的唯一 owner，负责协议适配、认证、授权、准入、归一化和结果发布 | 不允许各 entry 各自持有一套 shared access core |
| ACC-TC002 | 蓝图 3.2 | Must | 工程落点必须是 `access/` 共享 core + `apps/*` 壳层装配 | 任务必须先建 `access/include` / `access/src` / tests 拓扑 |
| ACC-TC003 | ADR-008；Access 详设 1.1、2.1、6.5 | Must | Runtime 保持全局主控，Access 不得形成第二调度中心、恢复中心或预算 owner | RuntimeBridge 只能是 bridge，不得承接 runtime 内部控制逻辑 |
| ACC-TC004 | WP03-T002、WP03-T014、Access 详设 6.6、6.8 | Must | Access 归一化输出必须复用 `AgentRequest`，发布必须以 `AgentResult` 为事实源 | RequestNormalizer / ResultPublisher 必须单列，contracts compatibility 必须设 Gate |
| ACC-TC005 | CrossModuleDataProjectionMatrix 6 | Must | `SubjectIdentity`、`AccessDecisionProof`、publish metadata 只能经 sidecar 和 observability 投影传播，不得塞进 shared contracts | `RuntimeDispatchRequest` 与 `AccessObservabilityBridge` 是唯一投影 owner |
| ACC-TC006 | Access 详设 2.1、6.7.1；OWASP | Must | Admission 必须逐请求执行且 fail-closed；无 allow proof、主体缺失、policy backend 不可用都必须拒绝 | AccessPolicyGate、AuthenticatorChain、AdmissionController 都需要 failure gate |
| ACC-TC007 | Access 详设 2.1、6.11.4 | Must | `runtime_override` 只能来自受控运维命令、诊断入口或已鉴权 ConfigCenter API | override schema 不冻结前，不允许做默认可执行 Build 任务 |
| ACC-TC008 | Access 详设 2.1、6.12 | Must | 认证失败、授权拒绝、override 尝试、publish failure、diagnostics pull 必须可日志、可追踪、可审计 | AccessObservabilityBridge 与 observability integration 不能后置 |
| ACC-TC009 | InfraConcurrencyPolicy；Access 详设 6.13 | Must | 引入 registry / queue / replay cache / stream buffer 时必须显式声明 overflow policy、backpressure 和 lock order；不得持 L2 锁做 I/O | AdmissionController、AsyncTaskRegistry、ResultReplayCache、Publisher 任务必须回链并发 SSOT |
| ACC-TC010 | InfraIntegrationTopology；Access 详设 2.1、9.1 | Must | Access 进入核心链路后必须补至少 1 个 integration smoke，且 `ctest -N` 可发现 | 测试拓扑任务必须前置 |
| ACC-TC011 | 编码规范 3.2 / 3.6 / 3.7 | Must | 新增公共接口进 `include/`，不吞错，新增公共面至少补 unit 或 integration 测试 | public ABI、error mapping、health 等都必须绑定测试 |
| ACC-TC012 | Access 详设 6.7、6.15 | Must | `IAccessGateway` 必须具备 lifecycle、drain、shutdown 拒绝语义 | 当前 `IAccessGateway` 需要补齐 `shutdown/state/is_ready` |
| ACC-TC013 | Access 详设 6.7、6.18.2 | Must | `IAccessRuntimeBridge` 需要 `dispatch/cancel` 双接口；cancel 只做转发，不拥有裁定权 | 当前 `IAccessRuntimeBridge` 需要扩展 surface |
| ACC-TC014 | WP02-T009；Access 详设 6.7、6.19 | Must | Access 需要生成和传播 `request_id/session_id/trace_id`；receipt ownership 校验必须绑定 `actor_ref` + HMAC token | RequestNormalizer、AsyncTaskRegistry、TaskQueryHandler 必须联动 |
| ACC-TC015 | Access 详设 6.11、6.20 | Must | `AccessBootstrapConfig` 只表达启动事实，运行治理优先复用既有 runtime/profile/infra 快照视图 | AccessConfigAdapter 需单列，禁止新增第二套策略体系 |
| ACC-TC016 | Access 详设 6.14.9、10.3、11 | Must | 在 shared streaming lifecycle 未冻结前，stream/WS/MQTT 只能做边界占位和 feature-flag gate，不得宣称首版 ready | StreamGateway、WS/MQTT 只允许 Blocked/延后任务 |
| ACC-TC017 | Access 详设 6.14.8、8.3；CLI/daemon 本地控制面详设 1.2、5.1 | Should | 首版应先交付 CLI 独立进程 + daemon 常驻服务的 unary + accepted_async 本地控制面主链；HTTP/gateway 在 transport 冻结后并行推进 | 执行顺序应优先 CLI/daemon 本地链路，再推进 HTTP/gateway 与 simulator |
| ACC-TC018 | 当前代码现状 + Access 详设 3.1 | Must | 当前 `access/include` 仅有最小 surface，`access/src` 仍是 `placeholder.cpp`，`apps/cli` / `apps/gateway` 仍是 placeholder main，`tests/unit/access` 和 `tests/integration/access` 不存在 | 必须显式承认 implementation not ready，先补骨架、测试拓扑和 surface 再进入主链实现 |

### 3.2 当前代码与测试现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| `access/CMakeLists.txt` | 已定义 `dasall_access`，但只编译 `src/placeholder.cpp`；PUBLIC 仅导出 `include/`，依赖 `dasall_contracts` 和 `dasall_infra` | Access 已有独立模块根，但实现仍停留在占位库 |
| `access/include/AccessTypes.h` | 当前仅定义 `AccessDisposition`、`InboundPacket`、最小 `RuntimeDispatchRequest`、最小 `PublishEnvelope`、最小 `RuntimeDispatchResult` | 支撑类型与详设 6.7 存在明显缺口，尚未收敛到 detail design surface |
| `access/include/IAccessGateway.h` | 当前只有 `init()`、`submit()`、`publish_result()` | 缺少 `shutdown()`、`state()`、`is_ready()`，尚不满足 lifecycle gate |
| `access/include/IAccessRuntimeBridge.h` | 当前只有 `dispatch()` | 缺少 `cancel()` 和 async disposition surface |
| `access/include/IProtocolAdapter.h` | 已有 `can_handle()/decode()/encode()` | 协议 adapter 公共形状存在，可直接延展到 CLI/HTTP/daemon/simulator |
| `apps/cli/CMakeLists.txt`、`apps/gateway/CMakeLists.txt` | `dasall_cli`、`dasall_gateway` 已链接 `dasall_access`、`dasall_runtime`、`dasall_contracts`、`dasall_infra` | entry executable 壳层目标存在，适合接入组合根 |
| `apps/cli/src/main.cpp`、`apps/gateway/src/main.cpp` | 仅输出 placeholder 文本 | 入口壳层尚未接 access 主链 |
| `tests/unit/CMakeLists.txt` | 已接入 runtime / cognition / llm / tools / memory / knowledge / infra / platform / profiles / services，没有 `access` | Access unit discoverability 缺失 |
| `tests/integration/CMakeLists.txt` | 已接入 runtime / infra / profiles / platform / services / tools / memory / knowledge / llm，没有 `access` | Access integration discoverability 缺失 |
| `tests/unit/`、`tests/integration/` 目录 | 当前均无 `access/` 子目录 | 不能宣称 Access 拥有模块级质量门 |

## 4. 粒度可行性评估

### 4.1 总体结论

1. 当前 Access 可直接生成 L3/L2 混合专项 TODO，不属于 L0 或纯组件级文档。
2. 可直接落到 L3 的对象：`AccessErrorCode`、`AccessError`、`AccessGatewayState`、`InboundPacket`、`SubjectIdentity`、`AccessDecisionProof`、`RuntimeDispatchRequest`、`RuntimeDispatchResult`、`PublishEnvelope`、`AsyncTaskReceipt`、`IAccessGateway`、`IAdmissionController`、`IAccessRuntimeBridge`。
3. 可安全落到 L2 的对象：`ProtocolAdapterRegistry`、`SubjectResolver`、`AuthenticatorChain`、`AccessPolicyGate`、`AdmissionController`、`RequestValidator`、`RequestNormalizer`、`RuntimeBridge`、`ResultPublisher`、`AsyncTaskRegistry`、`ResultReplayCache`、`AccessConfigAdapter`、`AccessObservabilityBridge`、CLI/HTTP/daemon/simulator adapters。
4. 只能停在 L1 或 Blocked 的对象：`StreamGateway`、WebSocket/MQTT adapters、`diagnostics.pull` artifact transport、`runtime_override` 受控 patch schema。
5. 结论：本专项 TODO 可以直接进入执行，但必须先完成 001/002/004/006~012 这一组解阻与接口骨架任务；003/005 属于外部协同阻塞，不得被伪装成 Build-ready 完成项。

### 4.2 可落盘对象提取表

| 类别 | 可落盘对象 | 设计锚点 | 建议落位 | 当前状态 |
|---|---|---|---|---|
| Public ABI | `IAccessGateway`、`IProtocolAdapter`、`IAccessRuntimeBridge`、`IAdmissionController`、`AccessTypes`、`AccessErrors` | 6.7、8.1 | `access/include/` | 仅前三者和最小 `AccessTypes` 已存在 |
| 核心数据结构 | `SubjectIdentity`、`AccessDecisionProof`、`RuntimeDispatchRequest`、`RuntimeDispatchResult`、`PublishEnvelope`、`AsyncTaskReceipt`、`AccessBootstrapConfig`、`AccessAdmissionResult` | 6.6、6.7、6.11、6.19 | `access/include/`、`access/src/` | 大部分未落盘 |
| 核心组件 | `ProtocolAdapterRegistry`、`SubjectResolver`、`AuthenticatorChain`、`AccessPolicyGate`、`AdmissionController`、`RequestValidator`、`RequestNormalizer`、`RuntimeBridge`、`ResultPublisher`、`AsyncTaskRegistry`、`ResultReplayCache`、`AccessConfigAdapter`、`AccessObservabilityBridge`、`AccessGateway` | 6.3~6.15、8.1 | `access/src/` | 均未落盘 |
| 入口壳层 | `CliIpcClient`、`HttpProtocolAdapter`、`TaskQueryHandler`、`HealthProbeHandler`、`DaemonProtocolAdapter`、`SimulatorProtocolAdapter` | 6.14.8、6.20、8.1 | `apps/*/src/` | 均未落盘 |
| 错误域 | `AccessErrorCode`、`ProtocolErrorMapper` | 6.7、6.17 | `access/include/AccessErrors.h`、`access/src/ProtocolErrorMapper.cpp` | 未落盘 |
| 配置模型 | `AccessBootstrapConfig`、`AccessAuthView`、`AccessAdmissionView`、`AccessPublishView`、`AccessRuntimeGovernanceView` | 6.11、6.14.8 | `access/include/AccessTypes.h`、`access/src/AccessConfigAdapter.cpp` | 未落盘 |
| 测试面 | interface surface、lifecycle、subject/auth/policy/admission、normalizer、publisher、async receipt、ownership、cancel、health、observability、CLI/daemon smoke（HTTP 可选）、profile gate | 9.1、9.2、8.1 | `tests/unit/access/`、`tests/integration/access/` | 目录不存在 |
| CMake / 注册点 | `access/CMakeLists.txt`、`tests/unit/access/CMakeLists.txt`、`tests/integration/access/CMakeLists.txt`、`tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt` | 8.1、9.2 | 现有 CMake 拓扑 | 仅 `access/CMakeLists.txt` 已存在 |

### 4.3 粒度可行性评估表

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 / 限制 | TODO 拆解策略 |
|---|---|---|---|---|---|
| `IAccessGateway` + lifecycle | 6.7、6.15 | L3 | 方法名、状态机、shutdown 语义明确 | 当前头文件未补齐 | 直接拆 public surface 任务 |
| `AccessErrorCode` / `AccessError` | 6.7、6.17 | L3 | 枚举码段、HTTP/CLI 映射表明确 | 当前头文件缺失 | 直接拆对象定义与测试 |
| `SubjectIdentity` / `AccessDecisionProof` | 6.6、6.7.2 | L3 | 字段和语义明确 | 无实质缺口 | 直接拆 supporting types |
| `RuntimeDispatchRequest` / `RuntimeDispatchResult` | 6.6、6.7、6.8 | L3 | shared / module-local 边界明确 | 与 runtime 真接口接缝未冻结 | 先补 seam，再落对象和 bridge |
| `IAdmissionController` / `AccessAdmissionResult` | 6.7、6.14.8 | L3 | 函数语义、replay/conflict/busy 路径明确 | 当前头文件缺失 | 先 surface，再实现 controller |
| `ProtocolAdapterRegistry` | 6.3、6.14.7 | L2 | 责任、内部接口和测试出口明确 | internal store 细节未成表 | 先 registry，再分别接 entry adapter |
| `SubjectResolver` | 6.7.2、6.14.7 | L3 | 属性最小集、challenge 语义、失败路径明确 | 无实质缺口 | 直接进入 Build |
| `AuthenticatorChain` | 6.3、6.14.7 | L3 | chain 行为、challenge/deny 语义和测试出口明确 | secret backend 适配细节依赖 infra seam | 可直接进入 Build |
| `AccessPolicyGate` | 6.7.1、6.14.7 | L3 | taxonomy、override/diagnostics gate、fail-closed 规则明确 | override/diagnostics schema 已冻结，剩余是实现与测试接线 | 可直接推进 submit/query/cancel 主路径，再补 override/diagnostics gate 测试 |
| `AdmissionController` | 6.13、6.14.7、6.21 | L3 | rate limit、idempotency、replay 规则明确 | 无实质缺口 | 直接进入 Build |
| `RequestValidator` / `RequestNormalizer` | 6.16、6.21.3、6.14.8 | L3 | 字段规则、constraint_set 投影、ID 传播明确 | 无实质缺口 | 直接进入 Build |
| `RuntimeBridge` | 6.7、6.18、6.14.8 | L2 | sync/async/reject 三出口明确 | runtime-facing public seam 未最终冻结 | 前置 ACC-TODO-001 |
| `ResultPublisher` | 6.7、6.10、6.17 | L3 | envelope、error mapping、channel unavailable 语义明确 | stream lifecycle 未冻结 | unary/async publish 可直接 Build；stream path 延后 |
| `AsyncTaskRegistry` / `ResultReplayCache` | 6.10、6.19、6.21 | L3 | ownership proof、TTL、query、replay 规则明确 | HMAC secret rotation 规模化方案未定义 | v1 可先做单实例 / deployment secret 版本 |
| `AccessConfigAdapter` | 6.11、6.14.8 | L2 | snapshot 视图与 immutable view 设计明确 | 落盘形态未冻结 | 前置 ACC-TODO-002 |
| `AccessObservabilityBridge` | 6.12、6.14.8 | L2 | 事件、字段、audit 语义明确 | sink 对接是实现细节 | 直接进入 Build |
| CLI client / HTTP adapters | 6.14.8、6.20 | L2 | 本地控制面与远程入口边界、首版测试出口明确；HTTP-only transport 已冻结 | gateway adapter 与 health handler 仍未落实现 | CLI client 与 HTTP unary/async 可并行进入 Build |
| daemon / simulator adapters | 6.14.9、6.15.3 | L2 | local trusted / deterministic stub 边界明确 | 入口细节未实现 | 在 CLI/daemon 稳定后进入 Build |
| StreamGateway / WS / MQTT | 6.14.9、10.3、11 | L1 / 延后 Gate 已冻结 | 边界、feature flag default-off 和 async/poll fallback 语义明确 | shared lifecycle、attach/reconnect/replay cursor shared contract 未冻结 | 只允许占位 / disabled/not ready，不进入正式 Build |

## 5. Design -> TODO 映射表

### 5.1 映射总表

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| runtime bridge sidecar seam | 6.6、6.7、6.18、12.2-1 | 补设计 / 接缝收敛 | ACC-TODO-001、011、020 | 先冻结 access -> runtime 统一输入与 cancel surface，再允许 bridge 实现 |
| AccessBootstrapConfig 与治理投影 | 6.11、12.2-2 | 补设计 / 配置模型 | ACC-TODO-002、012 | 先定义 source-of-truth，再实现 config projector |
| override / diagnostics 入口 schema | 6.7.1、6.11.4、6.12、12.2-8 | 补设计 / 高风险入口门禁 | ACC-TODO-003、016、035 | 未冻结前只允许 deny-by-default，不允许 Build-ready 任务伪装完成 |
| gateway 首版 transport 边界 | 6.14.8、12.2-3、12.2-5 | 补设计 / 入口范围收敛 | ACC-TODO-004、026、028 | 先冻结 HTTP unary/async 边界，再决定 transport library |
| 本地控制面链路（CLI -> IIPC/UDS -> daemon -> access core） | CLI/daemon 本地控制面详设 1.2、6.2；Access 详设 6.14.9、6.15.3 | 入口链路 / 依赖收敛 | ACC-TODO-025、029、037、038 | 先固化 CLI 纯客户端和 daemon 本地 owner，再扩远程入口 |
| streaming 延后边界 | 6.14.9、10.3、11 | 延后 Gate / feature flag default-off | ACC-TODO-005、035 | stream / WS / MQTT 只保留占位、disabled/not ready 与 async/poll fallback，不进入 v1 主线 |
| 公共接口与 supporting types | 6.7、6.15、8.1 | 接口定义 / 数据结构 | ACC-TODO-006 ~ 012 | 先把 `access/include` 补齐到 detail design surface |
| Admission 主链 | 6.3、6.7.1、6.13、6.14.7 | 组件实现 | ACC-TODO-013 ~ 019、032 | 按 `registry -> resolve -> auth -> policy -> admit -> normalize` 顺序拆分 |
| Runtime bridge 与 publish 主链 | 6.8、6.10、6.17、6.18 | 组件实现 | ACC-TODO-020 ~ 024、032 | sync/async/reject、channel unavailable、observability 分开落盘 |
| 入口壳层与 query / probe path | 6.14.8、6.18.2、6.20、8.1 | adapter / handler / 组合根 | ACC-TODO-025 ~ 030、034、037、038 | CLI/daemon 本地控制面优先，HTTP/gateway 受 transport 门控后推进 |
| 测试、profile、contracts、Gate 回写 | 8.2、9.1、9.2、11 | 测试与证据收口 | ACC-TODO-031 ~ 036 | 不允许用 build liveness 冒充主链 ready |

### 5.2 证据缺口 -> 补设计映射

| 证据缺口 | 直接风险 | 对应补设计任务 | 若未完成的处理规则 |
|---|---|---|---|
| runtime bridge 真接口已冻结（2026-05-06 校准） | 当前剩余风险已从设计冲突转为实现回退防护 | ACC-TODO-001、040 | 保持 `RuntimeBridgeAgentRequestHandoffTest` 与兼容性测试为 focused regression gate，不再把该项写成 blocker |
| `AccessBootstrapConfig` 已冻结为 typed bootstrap carrier | 最小 unary focused gate 不再受配置载体口径冲突阻断；剩余是 045 的 production projection 增量治理 | ACC-TODO-002、045 | `AccessConfigAdapter` 继续按增量任务推进，但不再以“配置载体未冻结”阻断当前 focused gate |
| override / diagnostics schema 已冻结（2026-04-23） | `AccessPolicyGate` / observability tests 可直接建模 | ACC-TODO-003 | override / diagnostics 未实现部分继续按 deny-by-default 落测试，不再等待外部 schema |
| gateway transport 选型已冻结（2026-04-23） | HTTP adapter 接口和线程模型不再漂移 | ACC-TODO-004 | HTTP 只按 HTTP-only unary/async + health 独立 listener 推进 |
| IIPC peer identity seam 已冻结（2026-05-06 校准） | daemon local trusted 已具备平台事实输入；剩余只需防行为回退 | ACC-TODO-037 | 继续以 peer identity focused tests 守住 local trusted，不再把该项写成 ready blocker |
| shared streaming lifecycle 未冻结 | stream / WS / MQTT 语义漂移 | ACC-TODO-005 | 只允许占位 Gate + async/poll fallback，不允许首版实现任务 |

### 5.3 交付评审结果 -> 整改任务映射

| 评审问题 | 优先级 | 整改任务 | 落地说明 |
|---|---|---|---|
| `AccessGateway` 未装配 resolver/auth/policy/admission/validator/normalizer/runtime/publisher 主链 | P0 | ACC-TODO-041、042、049 | 先做 production pipeline，再让 daemon/gateway 组合根使用真实依赖，并以 integration 证据闭环 |
| Access integration smoke 不是端到端主链验证，且 clean rebuild 曾失败 | P0 | ACC-TODO-039、049、050 | 先补 `dasall_access_tests` 和 clean rebuild，再将 034/035/036 从计划推进到可执行证据 |
| `RuntimeDispatchRequest` 未承载 `AgentRequest`，normalizer 与 RuntimeBridge 断裂 | P0 | ACC-TODO-040、041、049 | bridge handoff 必须消费 normalizer 输出，并由 mock runtime 断言 shared contract 字段 |
| async receipt query/cancel 未接 gateway/daemon，cancel 未转发 RuntimeBridge | P0 | ACC-TODO-043、046、049 | 功能路径与 ownership 安全路径分开落地，但最终 Gate 必须同时覆盖 |
| HTTP adapter 未设置固定 `protocol_kind`，且 JSON 解码是 ad hoc scanner | P0 | ACC-TODO-044、049 | 按 HTTP-only 首版边界做结构化解析、header 白名单和输入 fail-closed |
| `AccessConfigAdapter` 仍是占位 | P1 | ACC-TODO-045 | 将 bootstrap/profile/policy snapshot 投影为 immutable governance views，并补非法 schema fail init |
| ownership token 仍使用 `std::hash` 与默认静态 secret | P1 | ACC-TODO-046 | 改 HMAC-SHA256、key rotation、constant-time compare 和 secret missing fail-closed |
| `AccessPolicyGate` 仍是本地 snapshot stub | P1 | ACC-TODO-047 | 引入 `IAccessPolicyEvaluator`，production 接 infra/policy，snapshot 仅作测试 fake |
| observability bridge 未接主链 | P1 | ACC-TODO-048、049 | 真实失败路径必须有 request_id/trace_id、日志、指标、追踪和审计事实 |
| health readiness 硬编码 ready，daemon ping 绕过 AccessGateway | P1 | ACC-TODO-042、049 | readiness 聚合真实依赖；ping 只作 liveness，不作为 Access 主链证据 |
| deliverables 状态与实际交付证据不一致 | P1 | ACC-TODO-050 | 更新旧报告、034/035/036、worklog 与 Gate 证据，明确 Release Gate Not Closed |
| P2 工程硬化项未收敛 | P2 | ACC-TODO-051 | 删除 placeholder、强语义常量/枚举、shutdown audit、ID generator、registry 使用与 include 边界 |

## 6. 原子任务清单

### 6.1 前置补设计 / 评审解阻任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ACC-TODO-001 | Done | 补齐 access-runtime bridge sidecar seam | Access 详设 6.6、6.7、6.18、12.2-1；CrossModuleDataProjectionMatrix 6 | `RuntimeDispatchRequest`、`IAccessRuntimeBridge`、access sidecar 投影 | L2 | Access 详设；本 TODO；必要时 runtime seam 说明文档 | `RuntimeDispatchRequest`、`RuntimeInvokeContext`、`IAccessRuntimeBridge::dispatch/cancel` | 文档一致性 | `rg -n "IAccessRuntimeBridge|RuntimeDispatchRequest|RuntimeInvokeContext|Access sidecar|cancel\(" docs/architecture/DASALL_access子系统详细设计.md docs/ssot/CrossModuleDataProjectionMatrix.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-001-access-runtime-bridge-sidecar-seam收敛.md` | 无 | 已解阻 | runtime-facing public seam 与 access sidecar 投影口径唯一 | 更新后的 Access 详设；本 TODO；[docs/todos/access/deliverables/ACC-TODO-001-access-runtime-bridge-sidecar-seam收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-001-access-runtime-bridge-sidecar-seam收敛.md) | bridge 输入输出、取消路径与 sidecar owner 可被后续 Build 任务直接引用，且不再存在两套 runtime seam 说法 |
| ACC-TODO-002 | Done | 补齐 AccessBootstrapConfig schema 与治理投影源 | Access 详设 6.11、12.2-2；蓝图 3.2；计划文档阶段 K | 6.11 `AccessBootstrapConfig`、runtime governance views | L2 | Access 详设；本 TODO | `AccessBootstrapConfig`、`AccessAuthView`、`AccessAdmissionView`、`AccessPublishView`、`AccessRuntimeGovernanceView` | 文档一致性 | `rg -n "AccessBootstrapConfig|AccessAuthView|AccessAdmissionView|AccessPublishView|AccessRuntimeGovernanceView|SnapshotVersionFingerprint|runtime_budget|timeout_policy|ops_policy|infra.security_policy" docs/architecture/DASALL_access子系统详细设计.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-002-AccessBootstrapConfig与治理投影源收敛.md` | 无 | 已解阻 | 配置 source-of-truth、热更新边界与 immutable view 规则冻结 | 更新后的 Access 详设；本 TODO；[docs/todos/access/deliverables/ACC-TODO-002-AccessBootstrapConfig与治理投影源收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-002-AccessBootstrapConfig与治理投影源收敛.md) | 启动事实配置、运行治理投影和 override 边界均具唯一口径，可直接进入 `AccessConfigAdapter` Build |
| ACC-TODO-003 | Done | 补齐 override 与 diagnostics 入口 schema | Access 详设 6.7.1、6.11.4、6.11.5、6.12；infra/config 与 infra/diagnostics typed 对象 | `access.runtime_override.apply`、`access.diagnostics.pull` | L1 | Access 详设；本 TODO | `OverrideSourceFact`、`DiagnosticsSelectorFact`、artifact size / selector schema | 文档一致性 | `rg -n "ConfigPatch|OverrideSourceFact|DiagnosticsSelectorFact|SnapshotQuery|SnapshotExportRequest|snapshot_id|target_ref|max_artifact_bytes|allow proof" docs/architecture/DASALL_access子系统详细设计.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md infra/include/diagnostics/DiagnosticsTypes.h` | 无 | 已解阻 | override 入口复用 `ConfigPatch` v1；diagnostics 入口复用 `SnapshotQuery` / `SnapshotExportRequest`，v1 selector 收口为 `snapshot_id` only | 更新后的 Access 详设；本 TODO；[docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md) | `OverrideSourceFact` / `DiagnosticsSelectorFact` 与 size / transport 规则形成唯一口径，ACC-TODO-016/035 可直接据此落测试 |
| ACC-TODO-004 | Done | 收敛 gateway 首版 transport 选型与 HTTP-only 边界 | Access 详设 6.14.8、6.20.2、12.2-3、12.2-5；蓝图 3.2；gateway CMake 方案说明 | HTTP unary + async receipt 首版边界 | L1 | Access 详设；本 TODO；`apps/gateway/CMakeLists.txt` 方案说明 | `HttpProtocolAdapter`、`HealthProbeHandler`、HTTP thread model | 文档一致性 | `rg -n "cpp-httplib|HTTP/1.1|accepted async receipt|health listener|WebSocket|MQTT|bounded worker" docs/architecture/DASALL_access子系统详细设计.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md apps/gateway/CMakeLists.txt` | 无 | 已解阻 | gateway 首版固定 `cpp-httplib` HTTP/1.1 unary listener + accepted async receipt + 独立 health listener，WS/MQTT 延后到 Phase A5 | 更新后的 Access 详设；本 TODO；[docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md) | gateway 首版范围、transport 族与线程模型形成唯一口径，ACC-TODO-026/028 可直接按 HTTP-only 边界进入 Build |
| ACC-TODO-005 | Done | 收敛 streaming 延后边界与 async/poll fallback Gate | Access 详设 6.14.9、10.3、11；计划文档阶段 K | `StreamGateway`、WS/MQTT、async receipt + poll fallback | L1 | Access 详设；本 TODO | `StreamGateway` 占位接口、feature flag、fallback policy | 文档一致性 | `rg -n "StreamGateway|feature flag|async receipt|poll fallback|default-off|disabled/not ready|ACC-GATE-11" docs/architecture/DASALL_access子系统详细设计.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-005-streaming延后边界与async-poll-fallback-gate收敛.md` | 无 | 已收口（ACC-BLK-005 继续作为外部冻结项） | Access 侧已固定 `ACC-GATE-11`、feature flag default-off 与 async receipt/poll fallback；若要进入 streaming Build，仍需 runtime/llm/contracts 冻结 attach/reconnect/replay cursor | 更新后的 Access 详设；本 TODO；[docs/todos/access/deliverables/ACC-TODO-005-streaming延后边界与async-poll-fallback-gate收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-005-streaming延后边界与async-poll-fallback-gate收敛.md)；阻塞记录 | stream / WS / MQTT 被明确标注为延后 Gate，默认 disabled/not ready，并统一回退到 async receipt + poll，不再出现在 v1 Done-ready Build 列表 |

### 6.2 骨架与公共接口面任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ACC-TODO-006 | Done | 新增 access include、CMake 与测试拓扑骨架 | Access 详设 8.1、9.1、9.2；当前代码现状 | `access/include`、`tests/unit/access`、`tests/integration/access` discoverability | L2 | `access/CMakeLists.txt`、`tests/unit/access/CMakeLists.txt`、`tests/integration/access/CMakeLists.txt`、`tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt` | `AccessInterfaceSurfaceTest` discoverability、`ctest -N` discoverability | `cmake --build build-ci --target dasall_access && ctest --test-dir build-ci -N` | 无 | 无 | — | 更新后的 CMake、空骨架测试文件；[docs/todos/access/deliverables/ACC-TODO-006-access-include-cmake-测试拓扑骨架收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-006-access-include-cmake-测试拓扑骨架收敛.md) | 已落盘 `access/include/AccessErrors.h`、`access/include/IAdmissionController.h`、`tests/unit/access/AccessInterfaceSurfaceTest.cpp`、`tests/integration/access/AccessGatewaySmokeIntegrationTest.cpp`，且 `ctest -N` 可发现 Access unit/integration 入口 |
| ACC-TODO-007 | Done | 定义 AccessErrorCode 与 AccessError | Access 详设 6.7、6.17 | `AccessErrorCode` 100-999 码段、`AccessError` | L3 | `access/include/AccessErrors.h`、`access/src/ProtocolErrorMapper.cpp` | `AccessErrorCodeTest`、`AccessErrorMappingTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessErrorCodeTest|AccessErrorMappingTest" --output-on-failure` | 006 | 无 | — | 更新后的 `access/include/AccessErrors.h`、`access/src/ProtocolErrorMapper.cpp`、相关单测；[docs/todos/access/deliverables/ACC-TODO-007-AccessErrorCode与AccessError收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-007-AccessErrorCode与AccessError收敛.md) | `AccessErrorCode` / `AccessError` 与协议映射已落盘，`AccessErrorCodeTest`、`AccessErrorMappingTest` 通过；`dasall_unit_tests` 当前仍受既有 `tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp` 编译错误阻塞 |
| ACC-TODO-008 | Done | 定义 AccessGatewayState 并扩展 IAccessGateway 生命周期接口 | Access 详设 6.7、6.15 | `AccessGatewayState`、`shutdown()`、`state()`、`is_ready()` | L3 | `access/include/IAccessGateway.h`、`access/include/AccessTypes.h` | `AccessInterfaceSurfaceTest`、`AccessGatewayLifecycleTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessInterfaceSurfaceTest|AccessGatewayLifecycleTest" --output-on-failure` | 006 | 无 | — | 更新后的 `IAccessGateway.h`（新增 `state()`、`is_ready()`、`shutdown()`）、`AccessTypes.h`（新增 `AccessGatewayState` 枚举）；更新后的 `tests/unit/access/AccessInterfaceSurfaceTest.cpp`（新增枚举验证）、新建 `tests/unit/access/AccessGatewayLifecycleTest.cpp`、更新 `tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-008-AccessGatewayState与IAccessGateway生命周期接口收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-008-AccessGatewayState与IAccessGateway生命周期接口收敛.md) | `AccessGatewayState` 枚举与 `IAccessGateway` 生命周期接口已落盘，`AccessInterfaceSurfaceTest` 与 `AccessGatewayLifecycleTest` 均通过；验收命令结果：100% tests passed (2/2) |
| ACC-TODO-009 | Done | 定义 SubjectIdentity、AccessDecisionProof 与 dispatch/publish/receipt supporting types | Access 详设 6.6、6.7、6.19 | `SubjectIdentity`、`AccessDecisionProof`、`RuntimeDispatchRequest/Result`、`PublishEnvelope`、`AsyncTaskReceipt` | L3 | `access/include/AccessTypes.h` | `AccessSupportingTypesTest`、`PublishEnvelopeTypesTest`、`AsyncTaskReceiptTypesTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessSupportingTypesTest|PublishEnvelopeTypesTest|AsyncTaskReceiptTypesTest" --output-on-failure` | 006 | ACC-BLK-001 ✅ 已解阻 | — | 更新后的 `AccessTypes.h`（新增 6 个 struct/enum）；新增 3 个单元测试文件；更新 `tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-009-SubjectIdentity与AccessDecisionProof等supporting-types收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-009-SubjectIdentity与AccessDecisionProof等supporting-types收敛.md) | 6 个核心 supporting types 已落盘，与详设字段/contracts 边界/ownership 规则一致；3 个单元测试通过 100% (3/3)；验收命令结果完整 |
| ACC-TODO-010 | Done | 定义 IAdmissionController 与 AccessAdmissionResult | Access 详设 6.7、6.14.7 | `IAdmissionController::admit/release_ticket/record_completion` | L3 | `access/include/IAdmissionController.h`、`access/include/AccessTypes.h` | `AdmissionControllerSurfaceTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AdmissionControllerSurfaceTest" --output-on-failure` | 006、009 | 无 | — | 更新后的 `access/include/IAdmissionController.h`（新增 `admit/release_ticket/record_completion`），更新后的 `access/include/AccessTypes.h`（新增 `AccessAdmissionResult`），新增 `tests/unit/access/AdmissionControllerSurfaceTest.cpp`，更新 `tests/unit/access/AccessInterfaceSurfaceTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-010-IAdmissionController与AccessAdmissionResult收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-010-IAdmissionController与AccessAdmissionResult收敛.md) | Admission 公共面与 supporting type 已落盘，覆盖 admit/replay/conflict/release/completion 语义；`AccessInterfaceSurfaceTest` 与 `AdmissionControllerSurfaceTest` 通过，100% tests passed (2/2) |
| ACC-TODO-011 | Done | 对齐 IAccessRuntimeBridge 的 dispatch/cancel 公共面 | Access 详设 6.7、6.18.2 | `IAccessRuntimeBridge::dispatch/cancel` | L3 | `access/include/IAccessRuntimeBridge.h`、`access/include/AccessTypes.h` | `RuntimeBridgeSurfaceTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "RuntimeBridgeSurfaceTest" --output-on-failure` | 001、006、009 | ACC-BLK-001 ✅ 已解阻 | — | 更新后的 `access/include/IAccessRuntimeBridge.h`（新增 `cancel(request_id, actor_ref)`）；新增 `tests/unit/access/RuntimeBridgeSurfaceTest.cpp`；更新 `tests/unit/access/AccessInterfaceSurfaceTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-011-IAccessRuntimeBridge-dispatch-cancel公共面收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-011-IAccessRuntimeBridge-dispatch-cancel公共面收敛.md) | bridge 公共面已对齐 dispatch/cancel 语义，`RuntimeBridgeSurfaceTest` 与 `AccessInterfaceSurfaceTest` 均通过，100% tests passed (2/2) |
| ACC-TODO-012 | Done | 定义 AccessBootstrapConfig 与治理视图 supporting surface | Access 详设 6.11、6.14.8 | `AccessBootstrapConfig`、`Access*View` | L2 | `access/include/AccessTypes.h`、`access/src/AccessConfigAdapter.cpp` 占位 | `AccessConfigProjectionTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessConfigProjectionTest" --output-on-failure` | 002、006 | ACC-BLK-002 ✅ 已解阻 | — | 更新后的 `access/include/AccessTypes.h`（新增 `AccessBootstrapConfig`、`SnapshotVersionFingerprint`、`AccessAuthView`、`AccessAdmissionView`、`AccessPublishView`、`AccessRuntimeGovernanceView`）；新增 `access/src/AccessConfigAdapter.cpp` 占位；更新 `access/CMakeLists.txt`；新增 `tests/unit/access/AccessConfigProjectionTest.cpp`，更新 `tests/unit/access/AccessInterfaceSurfaceTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-012-AccessBootstrapConfig与治理视图supporting-surface收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-012-AccessBootstrapConfig与治理视图supporting-surface收敛.md) | Access 启动事实配置与治理投影视图 supporting surface 已落盘，`AccessConfigProjectionTest` 与 `AccessInterfaceSurfaceTest` 均通过，100% tests passed (2/2) |

### 6.3 配置 / 主链 / 入口实现任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ACC-TODO-013 | Done | 实现 ProtocolAdapterRegistry | Access 详设 6.3、6.14.7 | `register_adapter()`、`resolve_decoder()`、`resolve_encoder()`、`revoke_source()` | L2 | `access/src/ProtocolAdapterRegistry.cpp` | `ProtocolAdapterRegistryTest`、`ProtocolAdapterRegistryConflictTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "ProtocolAdapterRegistryTest|ProtocolAdapterRegistryConflictTest" --output-on-failure` | 006、009 | 无 | — | 更新后的 `access/src/ProtocolAdapterRegistry.h`、`access/src/ProtocolAdapterRegistry.cpp`、`access/CMakeLists.txt`、相关单测与 CMake；[docs/todos/access/deliverables/ACC-TODO-013-ProtocolAdapterRegistry收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-013-ProtocolAdapterRegistry收敛.md) | entry binding 已支持注册/查找/source 撤销与冲突拒绝，`ProtocolAdapterRegistryTest`、`ProtocolAdapterRegistryConflictTest` 定向通过，`ctest -N` 可发现 registry 单测 |
| ACC-TODO-014 | Done | 实现 SubjectResolver | Access 详设 6.7.2、6.14.7 | `resolve()`、`derive_channel_ref()`、`build_challenge_plan()` | L3 | `access/src/SubjectResolver.cpp` | `SubjectResolverTest`、`SubjectResolverLocalTrustedTest`、`SubjectResolverChallengeTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "SubjectResolver(Test|LocalTrustedTest|ChallengeTest)" --output-on-failure` | 009、012、013 | 无 | — | 更新后的 `access/src/SubjectResolver.h`、`access/src/SubjectResolver.cpp`、`access/CMakeLists.txt`、`tests/unit/access/SubjectResolverTest.cpp`、`tests/unit/access/SubjectResolverLocalTrustedTest.cpp`、`tests/unit/access/SubjectResolverChallengeTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-014-SubjectResolver收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-014-SubjectResolver收敛.md) | local trusted、一致身份、remote challenge 与 identity conflict 四类路径均已落盘并定向通过，`ctest -N --test-dir build/vscode-linux-ninja -R "SubjectResolver(Test|LocalTrustedTest|ChallengeTest)"` 可发现 3 个测试 |
| ACC-TODO-015 | Done | 实现 AuthenticatorChain | Access 详设 6.3、6.14.7 | `authenticate()`、`select_chain()`、`map_failure_reason()` | L3 | `access/src/AuthenticatorChain.cpp` | `AuthenticatorChainTest`、`AuthenticatorChainChallengeTest`、`AuthenticatorChainSecretFailureTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AuthenticatorChain(Test|ChallengeTest|SecretFailureTest)" --output-on-failure` | 012、014 | 无 | — | 更新后的 `access/src/AuthenticatorChain.h`、`access/src/AuthenticatorChain.cpp`、`access/CMakeLists.txt`、`tests/unit/access/AuthenticatorChainTest.cpp`、`tests/unit/access/AuthenticatorChainChallengeTest.cpp`、`tests/unit/access/AuthenticatorChainSecretFailureTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-015-AuthenticatorChain收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-015-AuthenticatorChain收敛.md) | trusted success、resolver challenge 透传与 secret backend failure 三类路径均已落盘并定向通过，`ctest -N --test-dir build/vscode-linux-ninja -R "AuthenticatorChain(Test|ChallengeTest|SecretFailureTest)"` 可发现 3 个测试 |
| ACC-TODO-016 | Done | 实现 AccessPolicyGate | Access 详设 6.7.1、6.14.7 | `evaluate_submit()`、`evaluate_task_query()`、`evaluate_override_request()` | L3 | `access/src/AccessPolicyGate.cpp` | `AccessPolicyGateTest`、`AccessPolicyOverrideGateTest`、`AccessPolicyBackendFailureTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessPolicy(GateTest|OverrideGateTest|BackendFailureTest)" --output-on-failure` | 003、009、015 | ACC-BLK-003 | 完成 003 | 更新后的 `access/src/AccessPolicyGate.h`、`access/src/AccessPolicyGate.cpp`、`access/CMakeLists.txt`、`tests/unit/access/AccessPolicyGateTest.cpp`、`tests/unit/access/AccessPolicyOverrideGateTest.cpp`、`tests/unit/access/AccessPolicyBackendFailureTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-016-AccessPolicyGate收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-016-AccessPolicyGate收敛.md) | submit/query/override 三路径均已落盘并定向通过，`ctest -N --test-dir build/vscode-linux-ninja -R "AccessPolicy(GateTest|OverrideGateTest|BackendFailureTest)"` 可发现 3 个测试 |
| ACC-TODO-017 | Done | 实现 AdmissionController | Access 详设 6.13、6.14.7、6.21 | `admit()`、`acquire_inflight_ticket()`、`check_idempotency()`、`release_ticket()`、`record_completion()` | L3 | `access/src/AdmissionController.cpp` | `AdmissionControllerTest`、`RateLimitGateTest`、`IdempotencyGuardTest`、`AdmissionReplayHitTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AdmissionControllerTest|RateLimitGateTest|IdempotencyGuardTest|AdmissionReplayHitTest" --output-on-failure` | 010、012 | 无 | — | 更新后的 `access/src/AdmissionController.h`、`access/src/AdmissionController.cpp`、`access/CMakeLists.txt`、`tests/unit/access/AdmissionControllerTest.cpp`、`tests/unit/access/RateLimitGateTest.cpp`、`tests/unit/access/IdempotencyGuardTest.cpp`、`tests/unit/access/AdmissionReplayHitTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-017-AdmissionController收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-017-AdmissionController收敛.md) | busy/conflict/replay-hit/admit 四类路径均已落盘并定向通过，`ctest -N --test-dir build/vscode-linux-ninja -R "AdmissionControllerTest|RateLimitGateTest|IdempotencyGuardTest|AdmissionReplayHitTest"` 可发现 4 个测试 |
| ACC-TODO-018 | Done | 实现 RequestValidator | Access 详设 6.16、6.17 | `validate_packet()`、`validate_payload_limits()`、`validate_headers()` | L3 | `access/src/RequestValidator.cpp` | `RequestValidatorTest`、`RequestValidatorPayloadLimitTest`、`RequestValidatorInjectionTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "RequestValidator(Test|PayloadLimitTest|InjectionTest)" --output-on-failure` | 009 | 无 | — | 更新后的 `access/src/RequestValidator.h`、`access/src/RequestValidator.cpp`、`access/CMakeLists.txt`、`tests/unit/access/RequestValidatorTest.cpp`、`tests/unit/access/RequestValidatorPayloadLimitTest.cpp`、`tests/unit/access/RequestValidatorInjectionTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-018-RequestValidator收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-018-RequestValidator收敛.md) | validate/payload/header 三段校验链路均已落盘并定向通过，`ctest -N --test-dir build/vscode-linux-ninja -R "RequestValidator(Test|PayloadLimitTest|InjectionTest)"` 可发现 3 个测试 |
| ACC-TODO-019 | Done | 实现 RequestNormalizer | Access 详设 6.8、6.16、6.21.3、6.14.8 | `normalize()`、`ensure_trace_ids()`、`project_agent_request()`、`build_publish_context()` | L3 | `access/src/RequestNormalizer.cpp` | `RequestNormalizerTest`、`RequestNormalizerIdentityProjectionTest`、`RequestNormalizerConstraintProjectionTest`、`RequestNormalizerContractCompatibilityTest` | `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -R "RequestNormalizer(Test|IdentityProjectionTest|ConstraintProjectionTest|ContractCompatibilityTest)|AgentRequestContractTest|AgentResultContractTest" --output-on-failure` | 009、014、017、018 | 无 | — | 更新后的 `access/src/RequestNormalizer.h`、`access/src/RequestNormalizer.cpp`、`access/CMakeLists.txt`、`tests/unit/access/RequestNormalizerTest.cpp`、`tests/unit/access/RequestNormalizerIdentityProjectionTest.cpp`、`tests/unit/access/RequestNormalizerConstraintProjectionTest.cpp`、`tests/unit/access/RequestNormalizerContractCompatibilityTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-019-RequestNormalizer收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-019-RequestNormalizer收敛.md) | `AgentRequest` 投影与 contracts guard 兼容，`request_id/session_id/trace_id` 自动生成/复用与 `constraint_set` 白名单投影均可断言；`ctest -N --test-dir build/vscode-linux-ninja -R "RequestNormalizer(Test|IdentityProjectionTest|ConstraintProjectionTest|ContractCompatibilityTest)"` 可发现 4 个测试 |
| ACC-TODO-020 | Done | 实现 RuntimeBridge | Access 详设 6.7、6.9、6.18、6.14.8 | `dispatch()`、`cancel()`、`map_runtime_result()`、`map_runtime_reject()` | L2 | `access/src/RuntimeBridge.cpp` | `RuntimeBridgeTest`、`RuntimeBridgeAsyncAcceptTest`、`RuntimeBridgeRejectMappingTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "RuntimeBridge(Test|AsyncAcceptTest|RejectMappingTest)" --output-on-failure` | 001、011、019 | ACC-BLK-001 | 完成 001 | 更新后的 `access/src/RuntimeBridge.h`、`access/src/RuntimeBridge.cpp`、`access/CMakeLists.txt`、`tests/unit/access/RuntimeBridgeTest.cpp`、`tests/unit/access/RuntimeBridgeAsyncAcceptTest.cpp`、`tests/unit/access/RuntimeBridgeRejectMappingTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-020-RuntimeBridge收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-020-RuntimeBridge收敛.md) | sync/accepted async/reject 三出口均已落盘并定向通过，`ctest -N --test-dir build/vscode-linux-ninja -R "RuntimeBridge(Test|AsyncAcceptTest|RejectMappingTest)"` 可发现 4 个匹配测试（含既有 `PluginRuntimeBridgeTest`），精确过滤后 `^(RuntimeBridgeTest|RuntimeBridgeAsyncAcceptTest|RuntimeBridgeRejectMappingTest)$` 通过 3/3 |
| ACC-TODO-021 | Done | 实现 ResultPublisher 与 ProtocolErrorMapper | Access 详设 6.10、6.17、6.14.8 | `build_envelope()`、`map_protocol_status()`、`emit_publish()` | L3 | `access/src/ResultPublisher.cpp`、`access/src/ProtocolErrorMapper.cpp` | `ResultPublisherTest`、`ProtocolErrorMapperTest`、`ResultPublisherChannelFailureTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "ResultPublisherTest|ProtocolErrorMapperTest|ResultPublisherChannelFailureTest" --output-on-failure` | 007、009、013、019、020 | 无 | — | 更新后的 `access/include/AccessTypes.h`、`access/src/ProtocolErrorMapper.h`、`access/src/ProtocolErrorMapper.cpp`、`access/src/ResultPublisher.h`、`access/src/ResultPublisher.cpp`、`access/CMakeLists.txt`、`tests/unit/access/ResultPublisherTest.cpp`、`tests/unit/access/ProtocolErrorMapperTest.cpp`、`tests/unit/access/ResultPublisherChannelFailureTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-021-ResultPublisher与ProtocolErrorMapper收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-021-ResultPublisher与ProtocolErrorMapper收敛.md) | `AgentResult` -> protocol response 映射稳定，发布通道失败显式映射 `PublishChannelUnavailable`，`ctest -N --test-dir build/vscode-linux-ninja -R "ResultPublisherTest|ProtocolErrorMapperTest|ResultPublisherChannelFailureTest"` 可发现 3 个测试 |
| ACC-TODO-022 | Done | 实现 AsyncTaskRegistry 与 ResultReplayCache | Access 详设 6.10、6.19、6.21、6.14.8 | `register_async_accept()`、`query_receipt()`、`validate_ownership()`、`put()/lookup()` | L3 | `access/src/AsyncTaskRegistry.cpp`、`access/src/ResultReplayCache.cpp` | `AsyncTaskRegistryTest`、`AsyncTaskRegistryOwnershipTest`、`AsyncTaskRegistryExpiryTest`、`ResultReplayCacheTest`、`ResultReplayCacheEvictionTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AsyncTaskRegistry(Test|OwnershipTest|ExpiryTest)|ResultReplayCache(Test|EvictionTest)" --output-on-failure` | 009、017、020、021 | ACC-BLK-006 | v1 单实例静态 secret + constant-time compare + TTL 规则已落盘；多实例轮换仍待后续任务 | 更新后的 `access/src/AsyncTaskRegistry.h`、`access/src/AsyncTaskRegistry.cpp`、`access/src/ResultReplayCache.h`、`access/src/ResultReplayCache.cpp`、`access/CMakeLists.txt`、`tests/unit/access/AsyncTaskRegistryTest.cpp`、`tests/unit/access/AsyncTaskRegistryOwnershipTest.cpp`、`tests/unit/access/AsyncTaskRegistryExpiryTest.cpp`、`tests/unit/access/ResultReplayCacheTest.cpp`、`tests/unit/access/ResultReplayCacheEvictionTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-022-AsyncTaskRegistry与ResultReplayCache收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-022-AsyncTaskRegistry与ResultReplayCache收敛.md) | accepted async、owner mismatch、TTL expired、replay cache eviction 均可自动断言；`dasall_unit_tests` 聚合构建仍受既有 `FreshnessControllerStalePolicyTest.cpp` 语法错误阻塞，已用 022 定向目标构建+ctest 验证 5/5 通过 |
| ACC-TODO-023 | Done | 实现 AccessObservabilityBridge | Access 详设 6.12、6.14.8 | `emit_request_received()`、`emit_auth_failed()`、`emit_policy_denied()`、`emit_dispatch_result()`、`emit_publish_failed()` | L2 | `access/src/AccessObservabilityBridge.cpp` | `AccessObservabilityBridgeTest`、`AccessObservabilityFieldSetTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessObservabilityBridgeTest|AccessObservabilityFieldSetTest" --output-on-failure` | 012、014、015、016、017、020、021 | 无 | — | 更新后的 `access/src/AccessObservabilityBridge.h`、`access/src/AccessObservabilityBridge.cpp`、`access/CMakeLists.txt`、`tests/unit/access/AccessObservabilityBridgeTest.cpp`、`tests/unit/access/AccessObservabilityFieldSetTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-023-AccessObservabilityBridge收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-023-AccessObservabilityBridge收敛.md) | auth failed / policy denied / publish failed 事件字段齐备，桥接发送失败仅反馈 `false` 且不改变业务判定；定向测试 2/2 通过 |
| ACC-TODO-024 | Done | 实现 AccessGateway facade 与优雅关闭 | Access 详设 6.14.7、6.15 | `submit()`、`publish_result()`、`shutdown()`、`run_submit_pipeline()` | L2 | `access/src/AccessGateway.cpp` | `AccessGatewayFacadeTest`、`AccessGatewayRejectPathTest`、`AccessGatewayAsyncReceiptTest`、`AccessGatewayLifecycleTest` | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessGateway(FacadeTest|RejectPathTest|AsyncReceiptTest|LifecycleTest)" --output-on-failure` | 008、013、014、015、016、017、018、019、020、021、022、023 | 无 | — | 更新后的 `access/src/AccessGateway.h`、`access/src/AccessGateway.cpp`、`access/CMakeLists.txt`、`tests/unit/access/AccessGatewayFacadeTest.cpp`、`tests/unit/access/AccessGatewayRejectPathTest.cpp`、`tests/unit/access/AccessGatewayAsyncReceiptTest.cpp`、`tests/unit/access/AccessGatewayLifecycleTest.cpp`、`tests/unit/access/CMakeLists.txt`；[docs/todos/access/deliverables/ACC-TODO-024-AccessGateway-facade与优雅关闭收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-024-AccessGateway-facade与优雅关闭收敛.md) | submit 主链、reject path、async receipt path、drain/shutdown path 全部可独立验证；定向测试 4/4 通过 |
| ACC-TODO-025 | Done | 实现 CliIpcClient 与 apps/cli 纯客户端组合根 | CLI 本地控制面详设 1.2、6.2、6.4；Access 详设 6.14.8、8.1 | `CliCommandParser`、`CliIpcClient`、`CliOutputFormatter`、CLI composition root | L2 | `apps/cli/src/CliCommandParser.cpp`、`apps/cli/src/CliIpcClient.cpp`、`apps/cli/src/CliOutputFormatter.cpp`、`apps/cli/src/main.cpp`、`apps/cli/CMakeLists.txt` | `CliIpcClientTest`、`CliIpcClientUnavailableTest`、`CliOutputFormatterTest` | `cmake --build build-ci --target dasall_cli dasall_unit_tests && ctest --test-dir build-ci -R "CliIpcClientTest|CliIpcClientUnavailableTest|CliOutputFormatterTest" --output-on-failure` | 013、018、024、038 | 无 | — | 更新后的 `apps/cli` 代码与 CMake、相关单测；交付物说明文档 | CLI 仅作为本地控制面客户端，不 direct link runtime，所有请求经 IIPC/UDS 进入 daemon/access 主链 |
| ACC-TODO-026 | Done | 实现 HttpProtocolAdapter 与 apps/gateway unary/async 组合根 | Access 详设 6.14.8、6.20、8.1 | `decode_http_request()`、`encode_http_response()`、gateway composition root | L2 | `apps/gateway/src/HttpProtocolAdapter.cpp`、`apps/gateway/src/main.cpp` | `HttpProtocolAdapterTest`、`HttpProtocolAdapterErrorMappingTest` | `cmake --build build-ci --target dasall_gateway dasall_unit_tests && ctest --test-dir build-ci -R "HttpProtocolAdapterTest|HttpProtocolAdapterErrorMappingTest" --output-on-failure` | 004、013、018、021、024 | ACC-BLK-004 | 完成 004 | `HttpProtocolAdapter.cpp`、更新后的 `apps/gateway/src/main.cpp`、相关单测；交付物说明文档 | HTTP unary + async receipt 首版主链可用，且不承诺 WS/MQTT |
| ACC-TODO-027 | Done | 实现 TaskQueryHandler 与 ownership validation path | Access 详设 6.18.2、6.19.2、8.1 | `query_receipt()`、`validate_ownership()`、cancel/query path | L2 | `apps/gateway/src/TaskQueryHandler.cpp`、`access/src/AsyncTaskRegistry.cpp` | `AccessTaskQueryHandlerTest`、`AsyncTaskRegistryOwnershipTest` | `cmake --build build-ci --target dasall_gateway dasall_unit_tests && ctest --test-dir build-ci -R "AccessTaskQueryHandlerTest|AsyncTaskRegistryOwnershipTest" --output-on-failure` | 016、020、022、026 | ACC-BLK-006 | deployment secret 与 ownership token 规则稳定 | `TaskQueryHandler.cpp`、相关单测；交付物说明文档 | 非 owner query/cancel 被拒绝，owner query 返回 pending/completed/expired 明确状态 |
| ACC-TODO-028 | Done | 实现 HealthProbeHandler 与 HTTP 安全头/CORS Gate | Access 详设 6.20、6.20.3 | `/health/live`、`/health/ready`、`/health/startup`、安全头/CORS | L2 | `apps/gateway/src/HealthProbeHandler.cpp`、`apps/gateway/src/HttpProtocolAdapter.cpp` | `AccessHealthProbeHandlerTest`、`AccessHttpSecurityHeadersTest` | `cmake --build build-ci --target dasall_gateway dasall_unit_tests && ctest --test-dir build-ci -R "AccessHealthProbeHandlerTest|AccessHttpSecurityHeadersTest" --output-on-failure` | 004、012、026 | ACC-BLK-004 | 完成 004 | `HealthProbeHandler.cpp`、相关单测；交付物说明文档 | health endpoint 二值返回、不经 Admission，且安全头/CORS 行为受配置白名单约束 |
| ACC-TODO-029 | Done | 实现 DaemonProtocolAdapter 与 apps/daemon 本地控制面组合根 | daemon 本地控制面详设 1.2、6.1、6.2；Access 详设 6.14.9、6.15.3、8.1 | `DaemonProtocolAdapter`、`DaemonBootstrap`、local peer uid trusted boundary | L2 | `access/src/daemon/DaemonProtocolAdapter.cpp`、`apps/daemon/src/DaemonBootstrap.cpp`、`apps/daemon/src/main.cpp` | `DaemonProtocolAdapterTest`、`DaemonProtocolAdapterLocalTrustedTest`、`CliDaemonPingIntegrationTest` | `cmake --build build-ci --target dasall_daemon dasall_unit_tests && ctest --test-dir build-ci -R "DaemonProtocolAdapterTest|DaemonProtocolAdapterLocalTrustedTest|CliDaemonPingIntegrationTest" --output-on-failure` | 013、014、015、017、019、024、037 | ACC-BLK-007 | 完成 037 | `DaemonProtocolAdapter.cpp`、`DaemonBootstrap.cpp`、更新后的 `apps/daemon/src/main.cpp`、相关测试；交付物说明文档 | daemon 作为本地 Access owner 可承接 CLI 请求，local trusted 判定基于 peer identity 且不绕过 Admission |
| ACC-TODO-030 | Done | 实现 SimulatorProtocolAdapter 与 apps/simulator 确定性刺激组合根 | Access 详设 6.14.9、6.15.3、8.1 | `SimulatorProtocolAdapter`、deterministic subject stub | L2 | `apps/simulator/src/SimulatorProtocolAdapter.cpp`、`apps/simulator/src/main.cpp` | `SimulatorProtocolAdapterTest`、`SimulatorProtocolAdapterDeterministicTest` | `cmake --build build-ci --target dasall_simulator dasall_unit_tests && ctest --test-dir build-ci -R "SimulatorProtocolAdapterTest|SimulatorProtocolAdapterDeterministicTest" --output-on-failure` | 013、014、017、019、024 | 无 | — | `SimulatorProtocolAdapter.cpp`、更新后的 `apps/simulator/src/main.cpp`、相关单测；交付物说明文档 | simulator 入口只在测试/工厂 profile 下启用，subject stub 行为可重复 |

### 6.4 测试支撑 / 集成 / Gate 收口任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ACC-TODO-031 | Done | 新增 Access interface、lifecycle 与 adapter registry 单元门 | Access 详设 8.1、9.1、9.2 | interface surface / lifecycle / registry discoverability | L2 | `tests/unit/access/CMakeLists.txt`、`AccessInterfaceSurfaceTest.cpp`、`AccessGatewayLifecycleTest.cpp`、`ProtocolAdapterRegistryTest.cpp` | interface surface、lifecycle、registry gate | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessInterfaceSurfaceTest|AccessGatewayLifecycleTest|ProtocolAdapterRegistryTest|ProtocolAdapterRegistryConflictTest" --output-on-failure` | 006、007、008、013 | 无 | — | 新增 unit test CMake；相关单测；交付物说明文档 | `access/include` surface 与 registry gate 被 `ctest` 发现且可通过 |
| ACC-TODO-032 | Done | 新增 Admission、Normalizer、Publisher 核心单元门 | Access 详设 6.13、6.16、6.17、9.1 | fail-closed、constraint projection、protocol mapping | L2 | `tests/unit/access/SubjectResolver*.cpp`、`AuthenticatorChain*.cpp`、`AccessPolicyGate*.cpp`、`AdmissionController*.cpp`、`RequestValidator*.cpp`、`RequestNormalizer*.cpp`、`ResultPublisher*.cpp` | Admission / Normalize / Publish core unit gates | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "SubjectResolver|AuthenticatorChain|AccessPolicyGate|AdmissionController|RequestValidator|RequestNormalizer|ResultPublisher|ProtocolErrorMapper" --output-on-failure` | 014、015、016、017、018、019、021 | 无 | — | 核心 unit tests；交付物说明文档 | authentication、authorization、busy/conflict、payload sanitize、`AgentRequest` 投影、publish error mapping 均有自动化断言 |
| ACC-TODO-033 | Done | 新增 async receipt、ownership、cancel 与本地入口单元门 | Access 详设 6.18、6.19、6.21、9.1 | async receipt、ownership、cancel、daemon/simulator local entry | L2 | `tests/unit/access/AsyncTaskRegistry*.cpp`、`AccessTaskQueryHandlerTest.cpp`、`DaemonProtocolAdapter*.cpp`、`SimulatorProtocolAdapter*.cpp` | async/local-entry unit gates | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AsyncTaskRegistry|ResultReplayCache|AccessTaskQueryHandlerTest|DaemonProtocolAdapter|SimulatorProtocolAdapter" --output-on-failure` | 022、027、029、030、037 | ACC-BLK-006、ACC-BLK-007 | deployment secret / ownership token 与 peer identity 规则稳定 | async/local-entry unit tests；交付物说明文档 | owner mismatch、expired receipt、cancel not applicable、daemon local trusted、simulator deterministic 行为全部可断言 |
| ACC-TODO-034 | Done | 固化 CLI/daemon unary focused ingress 与 async receipt 集成门 | Access 详设 8.2、9.1、9.2；CLI/daemon 本地控制面详设 6.6；InfraIntegrationTopology | CLI/daemon unary submit + async receipt/query/cancel integration | L2 | `tests/integration/access/CMakeLists.txt`、`CliDaemonSubmitIntegrationTest.cpp`、`AccessAsyncReceiptQueryCancelIntegrationTest.cpp`、`DaemonSubmitQueryCancelIntegrationTest.cpp` | unary focused ingress、async receipt/query/cancel integration | `ctest --test-dir build/vscode-linux-ninja -R "CliDaemonSubmitIntegrationTest|AccessAsyncReceiptQueryCancelIntegrationTest|DaemonSubmitQueryCancelIntegrationTest" --output-on-failure` | 022、024、025、029、031、032、042、043 | ACC-BLK-008 ✅ 已解阻 | — | integration CMake、focused ingress / async receipt 用例；`Gate-INT-08` 证据 | 当前 build 可稳定发现并执行 CLI/daemon submit 与 async receipt/query/cancel 主链；不再以 ping liveness 或 mock pipeline 充当集成门证据 |
| ACC-TODO-035 | Done | 固化 failure、observability、health、profile 与 contracts Gate | Access 详设 9.1、9.2、10.1、11；WP03/WP02 contracts deliverables | policy backend unavailable、observability 主链、health readiness、profile projection、contracts guard | L2 | `tests/integration/access/AccessObservabilityMainChainIntegrationTest.cpp`、`AccessPolicyBackendUnavailableIntegrationTest.cpp`、`AccessPublishFailureAuditTest.cpp`、`AccessHealthReadinessIntegrationTest.cpp`、`AccessProfileCompatibilityTest.cpp`、`tests/contract/CMakeLists.txt` | failure / observability / health / profile / contracts focused gates | `AccessObservabilityMainChainIntegrationTest`、`AccessPolicyBackendUnavailableIntegrationTest`、`AccessPublishFailureAuditTest`、`AccessHealthReadinessIntegrationTest`、`AccessProfileCompatibilityTest`、`AgentRequestContractTest`、`AgentResultContractTest`、`IdentityMetadataContractTest` | `Build_CMakeTools(target=dasall_gate_int_08)` | 003、005、012、019、023、028、032、034 | ACC-BLK-003 ✅ 已解阻；ACC-BLK-005 ✅ 已收口 | — | failure/observability/health/profile/contracts focused 用例；`Gate-INT-08` 证据；交付物说明文档 | policy fail-closed、主链 observability、health readiness、profile/contracts guard 已进入 focused gate；stream / WS / MQTT 继续由 `ACC-GATE-11` 保持延后，不再阻断 v1 unary gate |
| ACC-TODO-036 | Done | 回写 Access 专项 Gate 与交付证据 | Access 详设 9.2、11、12；成熟子系统 TODO 基线 | `Gate-INT-08`、blocker、证据、残余风险回写 | L2 | `docs/todos/access/DASALL_access子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`、`docs/todos/access/deliverables/` | Gate discoverability 与 current-build matrix 复验 | `ctest --test-dir build/vscode-linux-ninja -N && rg -n "Gate-INT-08|ACC-TODO-049|ACC-TODO-050|AccessGatewayPipelineIntegrationTest|AccessAsyncReceiptQueryCancelIntegrationTest|AccessHealthReadinessIntegrationTest" docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/integration/DASALL_系统集成专项TODO.md docs/worklog/DASALL_开发执行记录.md` | 034、035、049、050 | 无 | — | 更新后的 TODO、worklog、deliverables 回链记录 | `Gate-INT-08` 结论、blocker 状态、残余风险、统一验收命令和证据路径均完成回写，且 runtime-local / integration 证据不混写 |

### 6.5 本地控制面与跨子系统依赖收敛任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ACC-TODO-037 | Done | 补齐 IIPC peer identity 接缝并固化 LocalPeerUidFact 输入 | daemon 本地控制面详设 6.3.3；Access 详设 6.14.9、6.15.3 | `IIPC::describe_peer()`、`PeerIdentitySnapshot`、`LocalPeerUidFact` | L2 | `platform/include/IIPC.h`、`platform/src/linux/UnixIpcProvider.cpp`、`access/src/daemon/DaemonProtocolAdapter.cpp` | `UnixIpcProviderPeerIdentityTest`、`DaemonProtocolAdapterLocalTrustedTest` | `cmake --build build-ci --target dasall_platform dasall_unit_tests && ctest --test-dir build-ci -R "UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest" --output-on-failure` | 006 | ACC-BLK-007 ✅ 已解阻 | — | 更新后的 `platform/include/IIPC.h`、`platform/include/linux/UnixIpcProvider.h`、`platform/src/linux/UnixIpcProvider.cpp`、`access/include/AccessTypes.h`、`access/include/daemon/DaemonProtocolAdapter.h`、`access/src/daemon/DaemonProtocolAdapter.cpp`、相关单测与 CMake；[docs/todos/access/deliverables/ACC-TODO-037-IIPC-peer-identity接缝与LocalPeerUidFact输入收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-037-IIPC-peer-identity接缝与LocalPeerUidFact输入收敛.md) | IIPC peer identity public seam 已冻结并可返回 uid/gid/pid 事实，`UnixIpcProviderPeerIdentityTest` 与 `DaemonProtocolAdapterLocalTrustedTest` 定向通过，daemon 本地 trusted 判定不再依赖隐式假设 |
| ACC-TODO-038 | Done | 纠正 apps/cli 依赖方向并固定 CLI -> IIPC/UDS -> daemon 路径 | CLI 本地控制面详设 5.1、6.2；Access 详设 6.14.8 | `CliIpcClient`、CLI CMake link set、direct runtime link removal | L2 | `apps/cli/CMakeLists.txt`、`apps/cli/src/main.cpp`、`apps/cli/src/CliIpcClient.cpp` | `CliIpcClientTest`、`CliDaemonPingIntegrationTest` | `cmake --build build-ci --target dasall_cli dasall_daemon dasall_unit_tests && ctest --test-dir build-ci -R "CliIpcClientTest|CliDaemonPingIntegrationTest" --output-on-failure` | 006 | 无 | — | 更新后的 `apps/cli/CMakeLists.txt`、`apps/cli/src/CliIpcClient.h`、`apps/cli/src/CliIpcClient.cpp`、`apps/cli/src/main.cpp`、相关 unit/integration 测试与 CMake；[docs/todos/access/deliverables/ACC-TODO-038-CLI依赖方向纠正与UDS路径收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-038-CLI依赖方向纠正与UDS路径收敛.md) | CLI target 已移除 direct runtime link，`CliIpcClientTest` 与 `CliDaemonPingIntegrationTest` 定向通过，CLI -> IIPC/UDS -> daemon 路径骨架完成落盘 |

### 6.6 2026-04-24 交付评审整改任务

本节用于承接 `DASALL_Access子系统交付评审报告.md` 的 P0/P1/P2 评审结果。001~038 的 Done 仅代表接口、组件或局部单元门已形成基础证据；039 起的任务才是 Access v1 release gate 的实际整改主线。

| ID | 状态 | 优先级 | 任务标题 | 评审来源 | 落地范围 | 代码目标 | 测试目标 | 验收命令 | 前置依赖 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|
| ACC-TODO-039 | Done | P0 | 闭合 Access clean rebuild 与聚合测试门 | P0-2、R0 | CMake、`tests/unit/access`、`tests/integration/access` | 已新增 `dasall_access_unit_tests`、`dasall_access_integration_tests` 与顶层 `dasall_access_tests` 聚合目标；已修复 `AccessGatewaySmokeIntegrationTest.cpp` `PublishEnvelope` designated initializer 字段顺序；Access 测试统一经 `access` label 进入 Gate | `ctest -L access` clean build 后 62/62 通过；`ctest -N -L access` 可发现 62 个 Access unit/integration；全仓 `ctest -N` 退出码为 0 但仍有非 Access 历史测试缺可执行文件提示 | `cmake --build build-ci --target dasall_access_tests && ctest --test-dir build-ci -L access --output-on-failure && ctest --test-dir build-ci -N` | 006、031、032、033 | clean rebuild 不依赖既有二进制；Access 聚合目标和 label 已成为后续所有 Gate 的入口；交付物：[ACC-TODO-039-Access-clean-rebuild与聚合测试门收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-039-Access-clean-rebuild与聚合测试门收敛.md) |
| ACC-TODO-040 | Done | P0 | 修复 RuntimeBridge handoff，使 RuntimeDispatchRequest 承载 AgentRequest | P0-3、R2、INT-TODO-025 / `AccessUnaryProductionPathV1` | `access/include/AccessTypes.h`、`access/include/IAccessRuntimeBridge.h`、`access/src/RuntimeBridge.*`、`access/src/RequestNormalizer.*` | `RuntimeDispatchRequest` 已显式承载 `dasall::contracts::AgentRequest`，`RequestNormalizationOutput.agent_request` 由主链消费，不再只靠 `request_context` 侧带 | `RuntimeBridgeAgentRequestHandoffTest`、`RequestNormalizerRuntimeBridgeCompatibilityTest` | `ctest --test-dir build/vscode-linux-ninja -R "RuntimeBridgeAgentRequestHandoffTest|RequestNormalizerRuntimeBridgeCompatibilityTest|AgentRequestContractTest|AgentResultContractTest" --output-on-failure` | 001、011、019、020、039 | `RuntimeBridge` 收到的 `request_id/session_id/trace_id/user_input/request_channel` 与 normalizer 输出一致，sidecar 不污染 contracts |
| ACC-TODO-041 | Done | P0 | 实现 AccessGateway production pipeline 与依赖完整性校验 | P0-1、R1、INT-TODO-025 / `AccessUnaryProductionPathV1` | `access/src/AccessGateway.*` | resolver、authenticator、policy gate、admission、validator、normalizer、runtime bridge、publisher、async registry、replay cache、observability、config view 已接入 `AccessGateway`；`init()` 对关键依赖缺失 fail-closed；`submit()` 串联真实 pipeline | `AccessGatewayProductionPipelineTest`、`AccessGatewayDependencyValidationTest`、`AccessGatewayPipelineIntegrationTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessGatewayProductionPipelineTest|AccessGatewayDependencyValidationTest|AccessGatewayPipelineIntegrationTest" --output-on-failure` | 013~023、039、040 | 默认构造或依赖缺失不能进入 Ready；focused unit/integration tests 已能稳定断言 production pipeline 与依赖完整性 |
| ACC-TODO-042 | Done | P0 | 接通 daemon/gateway production 组合根与真实 readiness | P0-1、P1-5、P1-6、R1、R4、INT-TODO-025 / `AccessUnaryProductionPathV1` | `apps/daemon/src/*`、`apps/gateway/src/*` | daemon 和 gateway 已不再默认构造空 `AccessGateway`；测试 profile 使用显式 mock pipeline，生产 profile 使用真实 dependencies；readiness 由 gateway state、adapter registry、RuntimeBridge、policy/config health 聚合 | `DaemonAccessSubmitCompositionTest`、`GatewayAccessSubmitCompositionTest`、`AccessHealthReadinessIntegrationTest`、`CliDaemonSubmitIntegrationTest`、`HttpGatewaySubmitIntegrationTest` | `ctest --test-dir build/vscode-linux-ninja -R "DaemonAccessSubmitCompositionTest|GatewayAccessSubmitCompositionTest|AccessHealthReadinessIntegrationTest|CliDaemonSubmitIntegrationTest|HttpGatewaySubmitIntegrationTest" --output-on-failure` | 024、025、026、028、029、041 | daemon/gateway 主业务 submit 已经过 AccessGateway 主链；pipeline 缺失时 readiness=false；ping 不再被写作 Access 主链证据 |
| ACC-TODO-043 | Done | P0 | 打通 async receipt query/cancel 与 RuntimeBridge cancel 转发 | P0-4、R2、R4 | `apps/gateway/src/TaskQueryHandler.*`、daemon IPC handler、`IAccessRuntimeBridge::cancel`、`AsyncTaskRegistry` | gateway 已提供 receipt query/cancel 路径；daemon 已接入 submit/query/cancel IPC operation；query/cancel 统一执行 actor extraction、policy gate 与 ownership validation；cancel 结果以 RuntimeBridge 返回为准 | `AccessAsyncReceiptQueryCancelIntegrationTest`、`AccessCancelForwardingTest`、`DaemonSubmitQueryCancelIntegrationTest` | `ctest --test-dir build/vscode-linux-ninja -R "AccessAsyncReceiptQueryCancelIntegrationTest|AccessCancelForwardingTest|DaemonSubmitQueryCancelIntegrationTest" --output-on-failure` | 016、020、022、027、041、042 | accepted async、owner query、owner cancel、non-owner 拒绝、expired/not found、runtime cancel rejected 路径均可二值断言 |
| ACC-TODO-044 | Pending | P0 | 修复 HTTP adapter 首版 decode/encode 与输入安全 | P0-5、R4 | `apps/gateway/src/HttpProtocolAdapter.*`、gateway submit route | HTTP decode 固定 `entry_type="gateway"`、`protocol_kind="http_unary"`；使用结构化 JSON/parser wrapper；校验 method、path、content-type、body-size、idempotency-key；仅投影白名单 header 到 `request_context` | 新增 `HttpProtocolAdapterStructuredDecodeTest`、`HttpProtocolAdapterSecurityInputTest`、`GatewaySubmitRouteContractTest` | `cmake --build build-ci --target dasall_gateway dasall_access_tests && ctest --test-dir build-ci -R "HttpProtocolAdapterStructuredDecodeTest|HttpProtocolAdapterSecurityInputTest|GatewaySubmitRouteContractTest" --output-on-failure` | 026、039、041 | HTTP 入口生成的 packet 可通过 validator/normalizer；转义、嵌套、超限、非法 content-type 与 header 注入均 fail-closed |
| ACC-TODO-045 | Pending | P1 | 实现 AccessConfigAdapter 生产级 profile/config 投影 | P1-1、R3 | `access/src/AccessConfigAdapter.*`、profiles/config bridge | 将 bootstrap config 与 profile/policy snapshot 投影为 immutable `AccessAuthView`、`AccessAdmissionView`、`AccessPublishView`、`AccessRuntimeGovernanceView`；支持 fingerprint、last-known-good、hot update invalidation；非法 schema fail init | 新增 `AccessConfigAdapterProjectionTest`、`AccessConfigAdapterInvalidSchemaTest`、`AccessProfileCompatibilityTest` | `cmake --build build-ci --target dasall_access_tests && ctest --test-dir build-ci -R "AccessConfigAdapterProjectionTest|AccessConfigAdapterInvalidSchemaTest|AccessProfileCompatibilityTest" --output-on-failure` | 002、012、039 | 至少两档 profile 下投影稳定；配置缺失或非法不会使用散落默认值进入 Ready |
| ACC-TODO-046 | Pending | P1 | 将 async ownership token 改为正式 HMAC 与 secret rotation | P1-2、R3 | `access/src/AsyncTaskRegistry.*`、infra/secret seam | 使用 infra/secret deployment secret；token 包含 `key_id/issued_at/expires_at/receipt_id/actor_ref/request_id`；HMAC-SHA256 + base64url；constant-time compare；支持 current/previous key 验证；缺 secret 时禁用 async 或 init fail-closed | 新增 `AsyncTaskRegistryHmacOwnershipTest`、`AsyncTaskRegistrySecretRotationTest`、`AsyncTaskRegistryMissingSecretFailClosedTest` | `cmake --build build-ci --target dasall_access_tests && ctest --test-dir build-ci -R "AsyncTaskRegistryHmacOwnershipTest|AsyncTaskRegistrySecretRotationTest|AsyncTaskRegistryMissingSecretFailClosedTest" --output-on-failure` | 022、039 | 不再使用 `std::hash` 或静态默认 secret；rotation 与 expired/mismatch 路径可自动验证 |
| ACC-TODO-047 | Pending | P1 | 引入 IAccessPolicyEvaluator 并接入 infra/policy 生产适配 | P1-3、R3 | `access/src/AccessPolicyGate.*`、infra/policy adapter | 定义 `IAccessPolicyEvaluator` seam；production 实现调用 infra/policy；snapshot backend 仅保留为 unit fake；policy input 包含 subject/channel/environment/operation/target/fingerprint；backend unavailable fail-closed | 新增 `AccessPolicyEvaluatorIntegrationTest`、`AccessPolicyBackendUnavailableIntegrationTest`、`AccessPolicyInputAttributeTest` | `cmake --build build-ci --target dasall_access_tests && ctest --test-dir build-ci -R "AccessPolicyEvaluatorIntegrationTest|AccessPolicyBackendUnavailableIntegrationTest|AccessPolicyInputAttributeTest" --output-on-failure` | 016、039、045 | deny-by-default 与 per-request authorization 在生产 seam 中成立；策略失败产生 audit/metric anchor |
| ACC-TODO-048 | Pending | P1 | 将 AccessObservabilityBridge 接入主链与基础设施 sink | P1-4、R3 | `access/src/AccessGateway.*`、`AccessObservabilityBridge.*`、infra logging/metrics/tracing/audit bridge | 在 request received、auth failed、policy denied、admission rejected、runtime rejected、publish failed、shutdown abandoned 路径发事件；reject result 携带 request_id/trace_id；sink 失败不得改变业务裁定 | 新增 `AccessObservabilityMainChainIntegrationTest`、`AccessRejectTraceAnchorTest`、`AccessPublishFailureAuditTest` | `cmake --build build-ci --target dasall_access_tests && ctest --test-dir build-ci -R "AccessObservabilityMainChainIntegrationTest|AccessRejectTraceAnchorTest|AccessPublishFailureAuditTest" --output-on-failure` | 023、041、047 | 三类关键事件不再只停留在对象单测，真实主链失败路径可被日志、指标、追踪、审计断言 |
| ACC-TODO-049 | Done | P0 | 扩展 Access v1 端到端集成 Gate 矩阵 | P0-2、R4、R5 | `tests/integration/access`、contract gate 汇聚 | 以 formal alias + gate label 的方式把当前 focused evidence 收敛为 `Gate-INT-08`；覆盖 CLI->daemon submit、HTTP->gateway submit、async receipt query/cancel、policy backend unavailable、health readiness、profile/contracts guard | `AccessGatewayPipelineIntegrationTest`、`CliDaemonSubmitIntegrationTest`、`HttpGatewaySubmitIntegrationTest`、`AccessPolicyBackendUnavailableIntegrationTest`、`AccessHealthReadinessIntegrationTest`、`AccessProfileCompatibilityTest`、`gate-int-08`、`access-v1-production-gate`、`dasall_gate_int_08` | `Build_CMakeTools(target=dasall_gate_int_08)` | 039~048 | Access v1 unary focused gate 已以端到端主链证据为准，不再以 interface abstract、publish envelope 字段或 ping liveness 作为交付证据 |
| ACC-TODO-050 | Done | P1 | 校准 deliverables 状态、旧报告结论与复验证据 | P1-7、R5 | `docs/todos/access/*`、`docs/worklog/DASALL_开发执行记录.md` | 保留 2026-04-24 评审结论作为历史基线，同时把当前 focused gate 入口校准到 `Gate-INT-08`；034/035/036 不再被写成 mock pipeline / ping 证据；TODO、worklog 与系统 Gate 口径一致 | 文档一致性复验，`rg` 检查 Gate、任务状态、测试名与证据路径 | `rg -n "Gate-INT-08|ACC-TODO-049|ACC-TODO-050|AccessGatewayPipelineIntegrationTest|AccessAsyncReceiptQueryCancelIntegrationTest|AccessHealthReadinessIntegrationTest" docs/todos/access docs/todos/integration docs/worklog/DASALL_开发执行记录.md` | 039、049 | TODO、deliverables、worklog 与交付评审结论一致；不再出现把 mock pipeline、ping liveness 或局部字段写成 release evidence 的表述 |
| ACC-TODO-051 | Pending | P2 | 收敛 P2 工程硬化项与 release polish | P2、R5 | `access/*`、`apps/gateway/*`、tests | 删除不再需要的 `src/placeholder.cpp`；为强语义字符串引入 enum class 或集中常量；shutdown 写 abandoned audit 并关闭 registry/cache；success mapping 与 error mapping 分离；ID 生成接统一 generator；AccessGateway 使用 registry decode/encode；明确 app-private internal include 边界 | 新增或补强 `AccessGatewayShutdownAuditTest`、`AccessStrongEnumContractTest`、`AccessIdGeneratorStabilityTest`、`ProtocolAdapterRegistryGatewayIntegrationTest` | `cmake --build build-ci --target dasall_access_tests && ctest --test-dir build-ci -R "AccessGatewayShutdownAuditTest|AccessStrongEnumContractTest|AccessIdGeneratorStabilityTest|ProtocolAdapterRegistryGatewayIntegrationTest" --output-on-failure` | 039、041、048 | P2 不阻断 P0 主链，但必须在 Access v1 release candidate 前完成或形成显式残余风险记录 |

## 7. 执行顺序建议

### 7.1 串并行编排

| 阶段 | 任务 | 编排建议 | 说明 |
|---|---|---|---|
| A 历史基础校准 | 001 ~ 038 | 001~038 保留为设计、接口、组件和局部单元门基础；不得再以这些 Done 直接宣称 Access v1 Build-Ready | 先承认 2026-04-24 评审结论：当前 release gate 未闭合，034~036 仍要被 039~050 重新驱动 |
| B R0 可重复构建门 | 039 | 039 单独前置，修复 clean rebuild、Access 聚合目标和 label/discoverability | 没有 clean rebuild 证据时，后续任何 ctest 通过都不能作为交付证据 |
| C R1/R2 P0 主链修复 | 040 → 041 → 042 → 043 已完成；044 仍待推进 | `RuntimeBridge` handoff、AccessGateway production pipeline、daemon/gateway 组合根与 query/cancel 已闭合；HTTP structured decode / input security 仍保留在 044 | Access v1 的最短阻断链已通过 focused tests 收口；后续不再把默认空 pipeline 写成 ready |
| D R3 P1 安全治理 | 045 → 046 / 047 → 048 | config 投影先固定治理视图；HMAC ownership 与 policy evaluator 可并行；observability reject trace / 更广主链字段仍待补齐 | 当前 `Gate-INT-08` 只证明最小 focused 安全边界已可执行，不外推为全部安全治理已闭合 |
| E R4 集成矩阵 | 049 | 049 已通过 `dasall_gate_int_08` focused target 固化 CLI/daemon、HTTP/gateway、async query/cancel、policy backend unavailable、health、profile、contracts | 034/035 的 formal test-name 已成为当前 build 可执行 Gate，不再用 ping 或抽象接口 smoke 代替主链 |
| F R5 文档与 P2 硬化 | 050 已完成；051 待推进 | 050 已完成 TODO、worklog 与 focused gate 口径校准；051 在 release candidate 前完成或记录残余风险 | Access v1 unary focused gate 已闭合，但更广 release polish 仍受 P2 残余风险约束 |

### 7.2 必过门禁表

| Gate ID | 对应设计或约束 | 通过条件 | 关联任务 |
|---|---|---|---|
| ACC-GATE-01 | 拓扑门 | `tests/unit/access`、`tests/integration/access` 已接入，`ctest -N` 可发现 Access tests，且 `dasall_access_tests` clean build 可运行 | 006、031、034、039、049 |
| ACC-GATE-02 | 边界门 | access 代码不直接依赖 `cognition/llm/tools/services/memory/knowledge` 实现头文件，且不把 sidecar 写入 contracts | 001、019、020、035、040、041、047 |
| ACC-GATE-03 | contracts 一致性门 | `AgentRequest` / `AgentResult` / `IdentityMetadata` 相关 contract tests 全绿，RuntimeBridge 收到的是 normalizer 投影出的 `AgentRequest` | 019、035、040、049 |
| ACC-GATE-04 | Admission fail-closed 门 | auth failed、policy backend failure、queue full、idempotency conflict 四类场景都拒绝且不进入 runtime | 015、016、017、032、035、041、047、049 |
| ACC-GATE-05 | unary smoke 门 | CLI/daemon 或 CLI/HTTP 至少一条同步提交 -> production pipeline -> runtime mock -> publish 成功链通过 | 024、025、026、029、034、041、042、044、049 |
| ACC-GATE-06 | async receipt 安全门 | accepted async、ownership validation、query、cancel、expired/not found/mismatch 路径均可自动验证 | 022、027、033、034、043、046、049 |
| ACC-GATE-07 | 可观测完整性门 | `access.auth.failed`、`access.policy.denied`、`access.publish.failed` 三类事件在真实主链字段稳定且可断言 | 023、035、048、049 |
| ACC-GATE-08 | health/readiness 门 | gateway `/health/*` 与 daemon readiness 均二值可测，健康探针不经过 Admission 主链，readiness 反映真实依赖 | 028、029、035、042、049 |
| ACC-GATE-09 | 本地入口 trust 门 | daemon local trusted 和 simulator deterministic subject 受 profile / allowlist 控制，不越权且不绕过 Admission | 029、030、033、042、049 |
| ACC-GATE-10 | override 源安全门 | override / diagnostics schema 未冻结前一律 deny-by-default，冻结后仅受控来源可过，并发 audit/metric anchor | 003、016、035、047、048、049 |
| ACC-GATE-11 | stream 延后门 | shared streaming lifecycle 未冻结前，WS/MQTT/stream 仅占位、feature flag default-off，并统一回退到 async receipt + poll，不出现在 v1 ready 结论中 | 005、035、049、050 |
| ACC-GATE-12 | profile 投影门 | Access 对 `runtime_budget.*`、`timeout_policy.*`、`ops_policy.*`、`infra.security_policy.*` 的投影在至少两档 profile 下行为稳定 | 012、035、045、049 |
| ACC-GATE-13 | 本地控制面链路门 | CLI target 不 direct link runtime，CLI 请求经 IIPC/UDS 进入 daemon/access 主链 | 025、029、037、038、034、042、049 |
| ACC-GATE-14 | production pipeline 门 | `AccessGateway::init()` 缺 resolver/auth/policy/admission/normalizer/runtime/publisher 等 P0 依赖时 fail-closed，默认空 pipeline 不能 Ready | 039、041、042、049 |
| ACC-GATE-15 | 交付证据门 | TODO、deliverables、worklog 与最新 clean build / integration / contract 证据一致，旧 Build-Ready 结论已校准 | 039、049、050、051 |

## 8. 阻塞项与解阻条件

| Blocker ID | 对应设计 Blocker | 描述 | 影响任务 | 解阻条件 | 未解阻前策略 |
|---|---|---|---|---|---|
| ACC-BLK-001 | 详设 12.2-1 | `IAccessRuntimeBridge` 与 runtime 真接口的 sidecar handoff 已在 access 侧冻结为 `RuntimeDispatchRequest -> RuntimeInvokeContext` 两段式；剩余风险仅是后续 Build 的 adapter/mock 落盘，不再是设计口径冲突 | 009、011、020 | 已由 ACC-TODO-001 完成解阻；后续只需维持 bridge-local adapter 吸收 runtime public seam 差异 | Bridge 仍不得被误写为 runtime live route ready，但 public seam 已可进入 Build |
| ACC-BLK-002 | 详设 12.2-2 | `AccessBootstrapConfig` 已冻结为 typed bootstrap carrier，热更新仅作用于治理投影；剩余工作是后续 `AccessConfigAdapter` 代码与 tests 落盘，不再是设计口径 blocker | 012、028 | 已由 ACC-TODO-002 完成解阻；后续只需按 fingerprint 规则实现 immutable view 与 hot-update invalidation | `AccessConfigAdapter` 仍需用代码与测试证明 projection 行为，但不再停留在 placeholder 口径 |
| ACC-BLK-003 | 已解阻（2026-04-23）：Access 侧已对齐 infra/config `ConfigPatch` v1 与 diagnostics `SnapshotQuery` / `SnapshotExportRequest`；v1 diagnostics pull 仅接受 `snapshot_id` selector，并冻结 LocalFile + Json + exact-match remote gate | 003、016、035 | 无；后续仅需按 schema 落 `AccessPolicyGate` / observability tests，并保持 deny-by-default 对未冻结 selector 生效 | 证据回链到 Access 详设 6.11.4、6.11.5、6.12 与 [docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md) | 若后续实现重新接受自由 patch、`trace_id/session_id` 公共 selector、inline artifact bytes 或宽松 remote target，则重新转为 Blocked |
| ACC-BLK-004 | 已解阻（2026-04-23）：gateway 首版 transport 已冻结为 `cpp-httplib` HTTP/1.1 unary listener + accepted async receipt + 独立 health listener；WS/MQTT 延后到 Phase A5，不进入 v1 business listener | 004、026、028 | 无；后续仅需按 HTTP-only 边界落 `HttpProtocolAdapter` / `HealthProbeHandler` 与相关单测 | 证据回链到 Access 详设 6.14.8、6.20.2、`apps/gateway/CMakeLists.txt` 与 [docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md) | 若后续实现重新引入 WS/MQTT、streaming route、重量级事件循环依赖或混写 health/business listener，则重新转为 Blocked |
| ACC-BLK-005 | 已收口（2026-04-23）：ACC-TODO-005 已将 Access 侧 streaming 路径固定为 `ACC-GATE-11` 延后 Gate；在 runtime / llm / contracts 冻结 stream attach/reconnect/replay cursor 前，StreamGateway / WS / MQTT 仅允许 feature flag default-off + async receipt/poll fallback | 后续 stream tasks | runtime / llm / contracts 对 stream attach/reconnect/replay cursor 达成冻结结论，并形成可执行 listener / replay 测试入口 | StreamGateway / WS / MQTT 不进入 v1 Build-ready 任务；若需要断线恢复，统一回到 receipt/query/poll |
| ACC-BLK-006 | 详设 12.2-6 | ownership token 的正式 HMAC secret 轮换和多实例同步方案仍未定义；当前代码仅支撑单实例 / 部署静态 secret 基线 | 046、051 | deployment secret 管理、constant-time compare 与 rotation 规则固定 | v1 继续按单实例 / 部署静态 secret 实现，规模化结论不外推 |
| ACC-BLK-007 | 已解阻（2026-04-23）：platform 已冻结并实现 `IIPC::describe_peer()`，access 已落盘 `LocalPeerUidFact` 与 `DaemonProtocolAdapter` 输入接缝 | 029、033、037 | 无；后续仅需在 029/033 中基于该输入补齐 daemon local trusted 行为测试与入口接线 | 证据回链到 `platform/include/IIPC.h`、`platform/src/linux/UnixIpcProvider.cpp`、`access/src/daemon/DaemonProtocolAdapter.cpp` 与 [docs/todos/access/deliverables/ACC-TODO-037-IIPC-peer-identity接缝与LocalPeerUidFact输入收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-037-IIPC-peer-identity接缝与LocalPeerUidFact输入收敛.md) | 若后续实现绕过 `describe_peer()` 输入或在 peer identity 缺失时默认放行 local trusted，则重新转为 Blocked |
| ACC-BLK-008 | 已解阻（2026-05-06 校准）：`docs/ssot/AccessUnaryProductionPathV1.md` 已冻结 production/test profile 边界，040/041/042 已闭合 handoff、production pipeline 与真实 readiness，focused gate 已用 `CliDaemonSubmitIntegrationTest`、`HttpGatewaySubmitIntegrationTest`、`AccessGatewayPipelineIntegrationTest` 与 contracts guard 固化最小 production ingress 证据 | 051、后续 release polish tasks | 无；后续若扩张更广 release 结论，再基于 focused matrix 新开任务 | focused gate 之外，仍禁止把 mock pipeline 或 ping liveness 外推为更广 release 结论 |
| ACC-BLK-009 | 已解阻（2026-04-24，2026-05-06 校准）：Access clean rebuild 与聚合测试门已由 ACC-TODO-039 闭合，当前 unary focused gate 进一步由 `Build_CMakeTools(target=dasall_gate_int_08)` 承接 | 034、035、036、039 | 无；后续 Gate 变更必须同步更新 focused target、Access TODO 与 worklog 证据 | 证据回链到 `tests/CMakeLists.txt`、`tests/unit/access/CMakeLists.txt`、`tests/integration/access/CMakeLists.txt` 与 [docs/todos/access/deliverables/ACC-TODO-039-Access-clean-rebuild与聚合测试门收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-039-Access-clean-rebuild与聚合测试门收敛.md) | 旧二进制证据不再采信；当前 focused release evidence 以 `Gate-INT-08` 为准 |
| ACC-BLK-010 | 交付评审 P1-2/P1-3/P1-4 | ownership token、policy evaluator、observability 仍未完全接到更广生产级安全治理闭环；当前 `Gate-INT-08` 只把 policy backend unavailable 与 focused ingress 证据收敛为最小可执行边界 | 046、047、048、051 | ACC-TODO-046、047、048 完成，并形成更广安全治理矩阵 | async receipt/query/cancel 只能作为 focused functionality / fail-closed 证据，不得外推为全部安全可交付 |

### 8.1 Blocker 校准记录

| Blocker ID | 当前判断 | 是否阻断 v1 unary + async 主链 | 备注 |
|---|---|---|---|
| ACC-BLK-001 | 已解阻 | 否；040 已落完 `AgentRequest` handoff 与 bridge seam 当前实现 | 后续仅需防止 sidecar / mock 回退，不再是 v1 unary 主链阻塞 |
| ACC-BLK-002 | 已解阻（设计冻结完成，045 留作增量治理） | 否；最小 focused gate 不再受 config source 口径冲突阻断 | `AccessConfigAdapter` 的生产级 profile/config 投影仍在 045 继续推进，但不影响当前 unary focused gate |
| ACC-BLK-003 | 高风险 | 否，但阻断 override/diagnostics gate | 主线继续 deny-by-default |
| ACC-BLK-004 | 已解阻（设计冻结完成） | 否；HTTP-only 最小实现已在当前 gateway 主链中可执行 | gateway 继续按 HTTP-only 边界推进，不外推 WS/MQTT ready |
| ACC-BLK-005 | 中风险 | 否；005 已完成延后 Gate 收口，但仍阻断 stream / WS / MQTT ready 宣称 | 只能保留 feature flag default-off + async/poll fallback 结论 |
| ACC-BLK-006 | 中风险 | 否，但阻断规模化 receipt 结论 | 单实例实现可继续 |
| ACC-BLK-007 | 已解阻 | 否；已从设计阻塞转为实现接线事项 | peer identity seam 已冻结，后续关注 029/033 的行为覆盖完整性 |
| ACC-BLK-008 | 已解阻 | 否；`Gate-INT-08` 已闭合最小 production ingress focused gate | 这是 2026-05-06 校准后转出的历史 production pipeline blocker；后续风险转入 051 与更广 release polish |
| ACC-BLK-009 | 已解阻 | 否；clean rebuild 与 Access 聚合测试门已闭合 | 当前 focused release evidence 已收敛到 `Gate-INT-08`；后续风险转入 ACC-TODO-051 与系统侧 `INT-TODO-024` 文档闭环 |
| ACC-BLK-010 | 中风险 | 否，但阻断安全可交付结论 | P0 unary focused gate 已闭合，最终更广安全可交付仍需 046~048 继续收口 |

## 9. 验收与质量门

### 9.1 测试矩阵

| 测试层级 | 覆盖范围 | 关键用例 | 目标 |
|---|---|---|---|
| unit | public surface、identity/auth/policy/admission、validator、normalizer、bridge、publisher、async registry、replay cache、health、local entry adapters、production pipeline dependency validation | `AccessInterfaceSurfaceTest`、`AccessGatewayLifecycleTest`、`SubjectResolverTest`、`AccessPolicyGateTest`、`AdmissionControllerTest`、`RequestNormalizerTest`、`RuntimeBridgeAgentRequestHandoffTest`、`ResultPublisherTest`、`AsyncTaskRegistryHmacOwnershipTest`、`DaemonProtocolAdapterLocalTrustedTest`、`AccessGatewayDependencyValidationTest` | 组件职责、失败原因码、sidecar 边界、fail-closed、HMAC ownership、pipeline 完整性可二值验证 |
| contract | `AgentRequest`、`AgentResult`、`IdentityMetadata` 不被 Access 污染 | `AgentRequestContractTest`、`AgentRequestFieldContractTest`、`AgentResultContractTest`、`IdentityMetadataContractTest` | 确保 Access 不把主体/认证/回执细节挤入 shared contracts |
| integration | CLI/daemon（默认）与 HTTP（可选）unary、async receipt/query/cancel、failure/observability、health/probe、profile、production pipeline | `AccessGatewayPipelineIntegrationTest`、`CliDaemonSubmitIntegrationTest`、`HttpGatewaySubmitIntegrationTest`、`AccessAsyncReceiptQueryCancelIntegrationTest`、`AccessAdmissionFailureIntegrationTest`、`AccessObservabilityMainChainIntegrationTest`、`AccessHealthReadinessIntegrationTest`、`AccessProfileCompatibilityTest` | 验证 Access 进入核心链路后的最小端到端行为 |
| failure injection | secret backend failure、policy backend failure、queue full、channel unavailable、owner mismatch | `AuthenticatorChainSecretFailureTest`、`AccessPolicyBackendFailureTest`、`AdmissionReplayHitTest`、`ResultPublisherChannelFailureTest`、`AccessAdmissionFailureIntegrationTest` | 验证 fail-closed、fallback 与 evidence gap 可见 |
| 延后 Gate | stream / WS / MQTT | Gate frozen only | 防止未冻结能力被错误标记为 ready |

### 9.2 统一验收命令建议

1. 核心 clean build + Access 聚合：`cmake --build build-ci --target dasall_access dasall_access_tests && ctest --test-dir build-ci -L access --output-on-failure`
2. focused release gate：`Build_CMakeTools(target=dasall_gate_int_08)`
3. discoverability：`ctest --test-dir build-ci -N` 后确认 `AccessGatewayPipelineIntegrationTest`、`CliDaemonSubmitIntegrationTest`、`HttpGatewaySubmitIntegrationTest`、`AccessAsyncReceiptQueryCancelIntegrationTest`、`AccessHealthReadinessIntegrationTest`、`AccessProfileCompatibilityTest` 与 `dasall_access_tests` 目标存在。

### 9.3 质量门清单

| Gate ID | 检查项 | 通过标准 | 失败后动作 |
|---|---|---|---|
| ACC-GATE-01 | discoverability | Access unit / integration tests 被 `ctest -N` 发现 | 阻断后续主链任务 |
| ACC-GATE-02 | 边界门 | access 不越权依赖上层实现，且 sidecar 不写入 contracts | 阻断合并并回退到 bridge/normalizer 修正 |
| ACC-GATE-03 | contracts 边界 | contract tests 全绿，`AccessDecisionProof` / `SubjectIdentity` 不进入 shared contracts | 回退到 RequestNormalizer / surface 任务 |
| ACC-GATE-04 | Admission fail-closed | auth failed / policy backend fail / queue full / conflict 全拒绝 | 阻断集成测试 |
| ACC-GATE-05 | unary 主链 | CLI/daemon 或 CLI/HTTP 同步请求至少一条成功链通过 | 阻断 async / local entry 扩展 |
| ACC-GATE-06 | async receipt 安全 | accepted async、query、cancel、owner mismatch、expired 全可测 | 阻断 receipt 相关任务关闭 |
| ACC-GATE-07 | observability | 三类关键事件字段齐备且稳定 | 补 observability bridge，不进入结论 |
| ACC-GATE-08 | health/readiness | `/health/*` 不经 Admission，daemon/readiness 与 gateway/readiness 均二值可测 | 阻断正式可用结论 |
| ACC-GATE-09 | local entry trust | daemon / simulator 不绕过 auth/policy/allowlist | 阻断本地入口 ready 结论 |
| ACC-GATE-10 | override source | override / diagnostics 未冻结前 deny-by-default；冻结后仅受控来源通过 | 高危阻断 |
| ACC-GATE-11 | stream deferral | stream / WS / MQTT 未被冻结时不被写成 ready | 纠正文档 / 回退任务状态 |
| ACC-GATE-12 | profile projection | 两档以上 profile 下 runtime budget / timeout / security 投影行为稳定 | 阻断 profile ready 结论 |
| ACC-GATE-13 | 本地控制面链路 | CLI 不 direct link runtime，请求经 IIPC/UDS 进入 daemon/access 主链 | 阻断 CLI/daemon ready 结论 |
| ACC-GATE-14 | production pipeline | `AccessGateway` 缺 P0 依赖时 fail-closed，生产 submit 串联完整 pipeline | 阻断 Access v1 release gate |
| ACC-GATE-15 | 交付证据 | clean build、unit、integration、contract、deliverables、worklog 证据一致 | 阻断 Build-Ready / Release-Ready 文档结论 |

## 10. 风险与回退策略

### 10.1 风险表

| 风险 ID | 对应设计 Risk | 描述 | 触发条件 | 回退策略 |
|---|---|---|---|---|
| ACC-R01 | 详设 3.3 边界冲突 | Access 直接依赖 tools/services/llm 等实现，绕开 runtime 主控 | 实现期为图省事直接 include 相邻模块实现头 | 回退到 RuntimeBridge 唯一入口，删除越权依赖 |
| ACC-R02 | 详设 3.3 contracts 污染 | `SubjectIdentity`、`AsyncTaskReceipt`、publish metadata 被写入 contracts | 为了减少 sidecar 转换而扩 shared object | 回退到 `RuntimeDispatchRequest` + observability sidecar |
| ACC-R03 | 详设 3.3 授权语义漂移 | 不同 entry 自定义角色与 allow 规则，导致越权 | CLI/daemon/gateway 各自写死 policy | 回退到 `AccessPolicyGate` 单点授权，并补 Gate |
| ACC-R04 | 详设 3.3 发布路径阻塞 | 持锁 publish / replay 导致死锁或主链阻塞 | registry / cache / publisher 持 L2 锁做 I/O | 回退到 snapshot-out-of-lock 设计，强制补 lock-order 测试 |
| ACC-R05 | 详设 3.3 override 越权 | query string / cookie / env var 直达 override | override schema 未冻结却提前开放入口 | 立刻关闭入口、维持 deny-by-default、回写审计 |
| ACC-R06 | 详设 3.3 主体信息缺口 | `actor_ref` / `ownership_token` 设计不稳，导致 query/cancel 泄露 | ownership token 规则或 local trusted 口径漂移 | 回退到 receipt query 只返回 not authorized / not found，阻断 async ready 结论 |
| ACC-R07 | 详设 12.2 transport 选型 | gateway transport 过早绑定，后续线程模型返工 | 先写实现后补选型 | 保持 HTTP-only 最小实现，冻结 transport 后再扩张 |
| ACC-R08 | 详设 10.3 stream 过早推进 | 在 shared lifecycle 未冻结前硬上 stream / WS / MQTT | 把 feature flag 当默认开启 | 回退到 async receipt + poll fallback，不再宣称 stream ready |
| ACC-R09 | 交付评审 P0-1 | `AccessGateway` 仍是函数注入 facade，production pipeline 未装配 | 默认构造即可 Ready，或 daemon/gateway 用空 pipeline | 阻断 release gate，回退到 ACC-TODO-041/042 修复依赖完整性 |
| ACC-R10 | 交付评审 P0-3 | `AgentRequest` 未进入 RuntimeBridge public handoff | normalizer 单测通过但 runtime mock 收不到 shared request | 回退到 ACC-TODO-040，先冻结 handoff 再继续集成 |
| ACC-R11 | 交付评审 P0-2 | stale binary 掩盖 clean rebuild 失败 | 只运行旧 ctest，不重新构建 Access 测试目标 | 回退到 ACC-TODO-039，未闭合前所有 release 证据标记 Pending |
| ACC-R12 | 交付评审 P1-2 | ownership token 使用 `std::hash` 或默认静态 secret | async receipt 开放 query/cancel 前未完成 HMAC | 回退到 fail-closed 或禁用 async receipt，直到 ACC-TODO-046 完成 |

### 10.2 回退策略

1. 若 runtime seam 迟迟未冻结，回退策略是只保留 `AgentRequest` unary 主链接口占位，不外推 RuntimeBridge live route ready。
2. 若 gateway 选定的 HTTP-only transport 在实现阶段无法稳定接线，回退策略是先交付 CLI/daemon unary + async receipt，并将 HTTP 维持在最小 stub，不前推 WS/MQTT。
3. 若 ownership token 规模化方案未冻结，回退策略是单实例 / 静态 deployment secret 版本，并在 TODO 与 Gate 结论中明确“不外推多实例 ready”。
4. 若 override / diagnostics schema 未冻结，回退策略是入口 deny-by-default + audit，不提供任何灰色开关绕过。
5. 若 stream / WS / MQTT 设计冻结继续延后，回退策略是保留 feature flag default-off 与延后 Gate 状态，统一回落到 async receipt + poll。
6. 若 production pipeline 或 `AgentRequest` handoff 未完成，回退策略是保留组件级 Done 证据，但 release gate 保持 Not Ready，不允许 daemon/gateway 标记 ready。
7. 若 clean rebuild 未闭合，回退策略是停止引用旧 ctest 结果，先完成 `dasall_access_tests` 聚合目标与 `ctest -L access`。

## 11. 可行性结论

### 11.1 是否可直接进入执行

可以，但不是无条件全量执行。

当前可直接进入执行的范围是：ACC-TODO-044 继续承担 HTTP structured decode / input security 收口；ACC-TODO-045~048 作为 P1 安全治理与生产化治理并行跟进；ACC-TODO-051 承接剩余 P2 工程硬化。ACC-TODO-034~043、049、050 已完成当前 focused integration gate、最小 production ingress 与证据回写收口。001~038 保留为历史基础和前置依赖，不再作为 Access v1 可交付的最终证据。stream / WS / MQTT 继续沿用 005 已冻结的延后 Gate 口径，不能被写成 Done-ready Build 结论。

### 11.2 当前可落到的最细粒度

1. L3：public surface、supporting types、错误码、RequestValidator、RequestNormalizer、AdmissionController、AccessPolicyGate、ResultPublisher、AsyncTaskRegistry。
2. L2：ProtocolAdapterRegistry、RuntimeBridge、AccessConfigAdapter、AccessObservabilityBridge、CLI/HTTP/daemon/simulator adapters、AccessGateway、AccessGateway production pipeline、Access integration gate matrix。
3. L1 / 延后 Gate：StreamGateway、WS/MQTT adapters。

### 11.3 后续建议

1. 继续按 `HTTP structured decode / input security -> AccessConfigAdapter production projection -> HMAC ownership rotation -> policy evaluator -> observability reject trace / publish evidence` 的顺序推进 ACC-TODO-044~048。
2. `ACC-TODO-040~043` 已以 focused unit/integration tests 落盘；后续只在出现 handoff、pipeline、composition 或 query/cancel 回退时再回开增量任务。
3. ACC-TODO-049/050 已把 focused integration gate 与证据口径收敛到 `Gate-INT-08`；后续若 Gate 名称或矩阵变更，必须同步更新 TODO / worklog / system integration 记录。
4. 最后用 ACC-TODO-051 收口 P2 工程硬化；stream / WS / MQTT 保持延后 Gate + feature flag default-off，直到外部边界冻结。
5. 所有 Gate 结论必须继续回写到 TODO / deliverables / worklog，严禁用 build liveness、placeholder main、abstract interface smoke 或 IPC ping 冒充 Access ready。
