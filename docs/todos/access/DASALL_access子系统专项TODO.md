# DASALL Access 子系统专项 TODO

最近更新时间：2026-04-23
阶段：Detailed Design -> Special TODO
适用范围：access/、apps/cli、apps/daemon、apps/gateway、apps/simulator、tests/unit/access、tests/integration/access
当前结论：Access 详设已经满足 L3/L2 混合拆分条件。`InboundPacket`、`SubjectIdentity`、`AccessDecisionProof`、`AccessErrorCode`、`RuntimeDispatchRequest`、`PublishEnvelope`、`AsyncTaskReceipt`、`IAccessGateway`、`IProtocolAdapter`、`IAdmissionController`、`IAccessRuntimeBridge`、`AccessBootstrapConfig`、`OverrideSourceFact`、`DiagnosticsSelectorFact`、`HttpProtocolAdapter`、`HealthProbeHandler` 的边界与首版输入输出已经具备接口级或数据结构级拆分证据；streaming 路径也已在 Access 侧收口为延后 Gate + async/poll fallback，剩余外部前置约束转为上游 shared streaming lifecycle 冻结与 IIPC peer identity 接缝。因此本专项 TODO 采用“补设计解阻 -> 公共接口与骨架 -> 主链实现 -> 测试与 Gate 收口”的四段编排，默认以 daemon 常驻本地控制面承载 Access 主链，CLI 作为独立进程通过 IIPC/UDS 接入；gateway 继续仅以 HTTP unary + async receipt 首版边界推进。

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

行业补强参照采用 Access 详设已内化的参考基线，不额外改写边界：

1. Envoy HTTP filters：有序 decoder/encoder pipeline 与 terminal handler 思路。
2. OWASP Authorization Cheat Sheet：deny-by-default、逐请求授权、fail-closed 和审计基线。
3. IETF `Idempotency-Key` header draft：HTTP 幂等键格式与窗口语义。

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
| runtime bridge 真接口未冻结 | RuntimeBridge 与 runtime public seam 返工 | ACC-TODO-001 | Bridge 实现任务不得宣称 Done |
| `AccessBootstrapConfig` 落盘形态未冻结 | 配置与 profile 投影分叉 | ACC-TODO-002 | `AccessConfigAdapter` 与组合根只能停在 Blocked |
| override / diagnostics schema 已冻结（2026-04-23） | `AccessPolicyGate` / observability tests 可直接建模 | ACC-TODO-003 | override / diagnostics 未实现部分继续按 deny-by-default 落测试，不再等待外部 schema |
| gateway transport 选型已冻结（2026-04-23） | HTTP adapter 接口和线程模型不再漂移 | ACC-TODO-004 | HTTP 只按 HTTP-only unary/async + health 独立 listener 推进 |
| IIPC peer identity seam 未冻结 | daemon local trusted 判定缺乏平台事实输入 | ACC-TODO-037 | local trusted 相关命令保持 fail-closed，不写 ready 结论 |
| shared streaming lifecycle 未冻结 | stream / WS / MQTT 语义漂移 | ACC-TODO-005 | 只允许占位 Gate + async/poll fallback，不允许首版实现任务 |

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
| ACC-TODO-031 | NotStarted | 新增 Access interface、lifecycle 与 adapter registry 单元门 | Access 详设 8.1、9.1、9.2 | interface surface / lifecycle / registry discoverability | L2 | `tests/unit/access/CMakeLists.txt`、`AccessInterfaceSurfaceTest.cpp`、`AccessGatewayLifecycleTest.cpp`、`ProtocolAdapterRegistryTest.cpp` | interface surface、lifecycle、registry gate | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AccessInterfaceSurfaceTest|AccessGatewayLifecycleTest|ProtocolAdapterRegistryTest|ProtocolAdapterRegistryConflictTest" --output-on-failure` | 006、007、008、013 | 无 | — | 新增 unit test CMake；相关单测；交付物说明文档 | `access/include` surface 与 registry gate 被 `ctest` 发现且可通过 |
| ACC-TODO-032 | NotStarted | 新增 Admission、Normalizer、Publisher 核心单元门 | Access 详设 6.13、6.16、6.17、9.1 | fail-closed、constraint projection、protocol mapping | L2 | `tests/unit/access/SubjectResolver*.cpp`、`AuthenticatorChain*.cpp`、`AccessPolicyGate*.cpp`、`AdmissionController*.cpp`、`RequestValidator*.cpp`、`RequestNormalizer*.cpp`、`ResultPublisher*.cpp` | Admission / Normalize / Publish core unit gates | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "SubjectResolver|AuthenticatorChain|AccessPolicyGate|AdmissionController|RequestValidator|RequestNormalizer|ResultPublisher|ProtocolErrorMapper" --output-on-failure` | 014、015、016、017、018、019、021 | 无 | — | 核心 unit tests；交付物说明文档 | authentication、authorization、busy/conflict、payload sanitize、`AgentRequest` 投影、publish error mapping 均有自动化断言 |
| ACC-TODO-033 | NotStarted | 新增 async receipt、ownership、cancel 与本地入口单元门 | Access 详设 6.18、6.19、6.21、9.1 | async receipt、ownership、cancel、daemon/simulator local entry | L2 | `tests/unit/access/AsyncTaskRegistry*.cpp`、`AccessTaskQueryHandlerTest.cpp`、`DaemonProtocolAdapter*.cpp`、`SimulatorProtocolAdapter*.cpp` | async/local-entry unit gates | `cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AsyncTaskRegistry|ResultReplayCache|AccessTaskQueryHandlerTest|DaemonProtocolAdapter|SimulatorProtocolAdapter" --output-on-failure` | 022、027、029、030、037 | ACC-BLK-006、ACC-BLK-007 | deployment secret / ownership token 与 peer identity 规则稳定 | async/local-entry unit tests；交付物说明文档 | owner mismatch、expired receipt、cancel not applicable、daemon local trusted、simulator deterministic 行为全部可断言 |
| ACC-TODO-034 | NotStarted | 新增 CLI/daemon unary smoke 与 async receipt 集成门 | Access 详设 8.2、9.1、9.2；CLI/daemon 本地控制面详设 6.6；InfraIntegrationTopology | CLI/daemon unary + async receipt integration | L2 | `tests/integration/access/CMakeLists.txt`、`CliDaemonSmokeIntegrationTest.cpp`、`AccessAsyncReceiptIntegrationTest.cpp` | unary smoke、async receipt integration | `cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci -R "CliDaemonSmokeIntegrationTest|AccessAsyncReceiptIntegrationTest" --output-on-failure` | 022、024、025、029、031、032 | 无 | — | integration CMake、smoke/integration 用例；交付物说明文档 | `ctest -N` 可发现 Access integration；CLI/daemon 至少一条 unary 成功链和一条 async receipt 链通过 |
| ACC-TODO-035 | NotStarted | 新增 failure、observability、health、profile 与 contracts Gate | Access 详设 9.1、9.2、10.1、11；WP03/WP02 contracts deliverables | failure injection、observability completeness、health、profile projection、contracts compatibility | L2 | `tests/integration/access/AccessAdmissionFailureIntegrationTest.cpp`、`AccessObservabilityIntegrationTest.cpp`、`AccessHealthProbeIntegrationTest.cpp`、`AccessProfileCompatibilityTest.cpp` | failure/observability/health/profile/contracts gates | `cmake --build build-ci --target dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -R "AccessAdmissionFailureIntegrationTest|AccessObservabilityIntegrationTest|AccessHealthProbeIntegrationTest|AccessProfileCompatibilityTest|AgentRequestContractTest|AgentRequestFieldContractTest|AgentResultContractTest|IdentityMetadataContractTest" --output-on-failure` | 003、005、012、019、023、028、032、034 | ACC-BLK-003、ACC-BLK-005 | override/diagnostics schema 与 stream 延后边界有正式结论 | failure/observability/health/profile/contracts 用例；交付物说明文档 | Admission fail-closed、三类 observability 事件、health 二值状态、profile 差异投影、contracts 兼容性均通过；stream / override 未冻结部分保持 deny/延后而不是假通过 |
| ACC-TODO-036 | NotStarted | 回写 Access 专项 Gate 与交付证据 | Access 详设 9.2、11、12；成熟子系统 TODO 基线 | Gate、blocker、证据、残余风险回写 | L2 | `docs/todos/access/DASALL_access子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`、`docs/todos/access/deliverables/` | Gate discoverability 与 current-build matrix 复验 | `ctest --test-dir build-ci -N | rg "Access" && rg -n "ACC-GATE|ACC-BLK|CliDaemonSmokeIntegrationTest|AccessAsyncReceiptIntegrationTest|AccessObservabilityIntegrationTest" docs/todos/access/DASALL_access子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md` | 034、035 | 无 | — | 更新后的 TODO、worklog、deliverables 回链记录 | Gate 结论、blocker 状态、残余风险、统一验收命令和证据路径均完成回写，且 runtime-local / integration 证据不混写 |

### 6.5 本地控制面与跨子系统依赖收敛任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ACC-TODO-037 | Done | 补齐 IIPC peer identity 接缝并固化 LocalPeerUidFact 输入 | daemon 本地控制面详设 6.3.3；Access 详设 6.14.9、6.15.3 | `IIPC::describe_peer()`、`PeerIdentitySnapshot`、`LocalPeerUidFact` | L2 | `platform/include/IIPC.h`、`platform/src/linux/UnixIpcProvider.cpp`、`access/src/daemon/DaemonProtocolAdapter.cpp` | `UnixIpcProviderPeerIdentityTest`、`DaemonProtocolAdapterLocalTrustedTest` | `cmake --build build-ci --target dasall_platform dasall_unit_tests && ctest --test-dir build-ci -R "UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest" --output-on-failure` | 006 | ACC-BLK-007 ✅ 已解阻 | — | 更新后的 `platform/include/IIPC.h`、`platform/include/linux/UnixIpcProvider.h`、`platform/src/linux/UnixIpcProvider.cpp`、`access/include/AccessTypes.h`、`access/include/daemon/DaemonProtocolAdapter.h`、`access/src/daemon/DaemonProtocolAdapter.cpp`、相关单测与 CMake；[docs/todos/access/deliverables/ACC-TODO-037-IIPC-peer-identity接缝与LocalPeerUidFact输入收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-037-IIPC-peer-identity接缝与LocalPeerUidFact输入收敛.md) | IIPC peer identity public seam 已冻结并可返回 uid/gid/pid 事实，`UnixIpcProviderPeerIdentityTest` 与 `DaemonProtocolAdapterLocalTrustedTest` 定向通过，daemon 本地 trusted 判定不再依赖隐式假设 |
| ACC-TODO-038 | Done | 纠正 apps/cli 依赖方向并固定 CLI -> IIPC/UDS -> daemon 路径 | CLI 本地控制面详设 5.1、6.2；Access 详设 6.14.8 | `CliIpcClient`、CLI CMake link set、direct runtime link removal | L2 | `apps/cli/CMakeLists.txt`、`apps/cli/src/main.cpp`、`apps/cli/src/CliIpcClient.cpp` | `CliIpcClientTest`、`CliDaemonPingIntegrationTest` | `cmake --build build-ci --target dasall_cli dasall_daemon dasall_unit_tests && ctest --test-dir build-ci -R "CliIpcClientTest|CliDaemonPingIntegrationTest" --output-on-failure` | 006 | 无 | — | 更新后的 `apps/cli/CMakeLists.txt`、`apps/cli/src/CliIpcClient.h`、`apps/cli/src/CliIpcClient.cpp`、`apps/cli/src/main.cpp`、相关 unit/integration 测试与 CMake；[docs/todos/access/deliverables/ACC-TODO-038-CLI依赖方向纠正与UDS路径收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-038-CLI依赖方向纠正与UDS路径收敛.md) | CLI target 已移除 direct runtime link，`CliIpcClientTest` 与 `CliDaemonPingIntegrationTest` 定向通过，CLI -> IIPC/UDS -> daemon 路径骨架完成落盘 |

## 7. 执行顺序建议

### 7.1 串并行编排

| 阶段 | 任务 | 编排建议 | 说明 |
|---|---|---|---|
| A 补设计与解阻 | 001 ~ 005 | 001 / 002 / 003 / 004 已完成；005 仍受外部协同阻塞 | 先冻结 seam、config、override/diagnostics schema、gateway HTTP-only 边界与 v1 范围，再允许接口和组合根落盘 |
| B 公共接口与骨架 | 006 ~ 012、037、038 | 006 串行起步；007~012 可按 surface 域并行；037/038 与骨架并行推进 | 先让 `access/include`、tests topology 与 CLI/daemon 本地接入路径成为稳定入口 |
| C Admission 与 dispatch 主链 | 013 ~ 024 | 013 → 014/015 → 016/017/018 → 019 → 020/021 → 022/023 → 024 | 先 registry 和身份链，再 Admission、Normalizer、Bridge、Publisher、Async、Observability、Gateway |
| D 入口壳层 | 025 ~ 030 | 029 / 025 优先并行；026 / 028 在 004 完成后推进；027 依赖 022 / 026；030 在 CLI/daemon 稳定后并行 | 首版先 CLI/daemon 本地控制面，再补 HTTP/gateway 与 simulator |
| E 测试与 Gate 收口 | 031 ~ 036 | 031 / 032 并行；033 在 022/027/029/030/037 后；034 在 022/024/025/029 后；035 在 023/028/034 后；036 最后 | 先 unit，再 integration，再 Gate 与证据回写 |

### 7.2 必过门禁表

| Gate ID | 对应设计或约束 | 通过条件 | 关联任务 |
|---|---|---|---|
| ACC-GATE-01 | 拓扑门 | `tests/unit/access`、`tests/integration/access` 已接入，`ctest -N` 可发现 Access tests | 006、031、034 |
| ACC-GATE-02 | 边界门 | access 代码不直接依赖 `cognition/llm/tools/services/memory/knowledge` 实现头文件，且不把 sidecar 写入 contracts | 001、019、020、035 |
| ACC-GATE-03 | contracts 一致性门 | `AgentRequest` / `AgentResult` / `IdentityMetadata` 相关 contract tests 全绿 | 019、035 |
| ACC-GATE-04 | Admission fail-closed 门 | auth failed、policy backend failure、queue full、idempotency conflict 四类场景都拒绝且不进入 runtime | 015、016、017、032、035 |
| ACC-GATE-05 | unary smoke 门 | CLI/daemon 或 CLI/HTTP 至少一条同步提交 -> publish 成功链通过 | 024、025、026、029、034 |
| ACC-GATE-06 | async receipt 安全门 | accepted async、ownership validation、expired/not found/mismatch 路径均可自动验证 | 022、027、033、034 |
| ACC-GATE-07 | 可观测完整性门 | `access.auth.failed`、`access.policy.denied`、`access.publish.failed` 三类事件字段稳定且可断言 | 023、035 |
| ACC-GATE-08 | health/readiness 门 | gateway `/health/*` 与 daemon readiness 均二值可测，且健康探针不经过 Admission 主链 | 028、029、035 |
| ACC-GATE-09 | 本地入口 trust 门 | daemon local trusted 和 simulator deterministic subject 受 profile / allowlist 控制，不越权 | 029、030、033 |
| ACC-GATE-10 | override 源安全门 | override / diagnostics schema 未冻结前一律 deny-by-default，冻结后仅受控来源可过 | 003、016、035 |
| ACC-GATE-11 | stream 延后门 | shared streaming lifecycle 未冻结前，WS/MQTT/stream 仅占位、feature flag default-off，并统一回退到 async receipt + poll，不出现在 v1 ready 结论中 | 005、035 |
| ACC-GATE-12 | profile 投影门 | Access 对 `runtime_budget.*`、`timeout_policy.*`、`ops_policy.*`、`infra.security_policy.*` 的投影在至少两档 profile 下行为稳定 | 012、035 |
| ACC-GATE-13 | 本地控制面链路门 | CLI target 不 direct link runtime，CLI 请求经 IIPC/UDS 进入 daemon/access 主链 | 025、029、037、038、034 |

## 8. 阻塞项与解阻条件

| Blocker ID | 对应设计 Blocker | 描述 | 影响任务 | 解阻条件 | 未解阻前策略 |
|---|---|---|---|---|---|
| ACC-BLK-001 | 详设 12.2-1 | `IAccessRuntimeBridge` 与 runtime 真接口的 sidecar handoff 已在 access 侧冻结为 `RuntimeDispatchRequest -> RuntimeInvokeContext` 两段式；剩余风险仅是后续 Build 的 adapter/mock 落盘，不再是设计口径冲突 | 009、011、020 | 已由 ACC-TODO-001 完成解阻；后续只需维持 bridge-local adapter 吸收 runtime public seam 差异 | Bridge 仍不得被误写为 runtime live route ready，但 public seam 已可进入 Build |
| ACC-BLK-002 | 详设 12.2-2 | `AccessBootstrapConfig` 已冻结为 typed bootstrap carrier，热更新仅作用于治理投影；剩余工作是后续 `AccessConfigAdapter` 代码与 tests 落盘，不再是设计口径 blocker | 012、028 | 已由 ACC-TODO-002 完成解阻；后续只需按 fingerprint 规则实现 immutable view 与 hot-update invalidation | `AccessConfigAdapter` 仍需用代码与测试证明 projection 行为，但不再停留在 placeholder 口径 |
| ACC-BLK-003 | 已解阻（2026-04-23）：Access 侧已对齐 infra/config `ConfigPatch` v1 与 diagnostics `SnapshotQuery` / `SnapshotExportRequest`；v1 diagnostics pull 仅接受 `snapshot_id` selector，并冻结 LocalFile + Json + exact-match remote gate | 003、016、035 | 无；后续仅需按 schema 落 `AccessPolicyGate` / observability tests，并保持 deny-by-default 对未冻结 selector 生效 | 证据回链到 Access 详设 6.11.4、6.11.5、6.12 与 [docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-003-override与diagnostics入口schema收敛.md) | 若后续实现重新接受自由 patch、`trace_id/session_id` 公共 selector、inline artifact bytes 或宽松 remote target，则重新转为 Blocked |
| ACC-BLK-004 | 已解阻（2026-04-23）：gateway 首版 transport 已冻结为 `cpp-httplib` HTTP/1.1 unary listener + accepted async receipt + 独立 health listener；WS/MQTT 延后到 Phase A5，不进入 v1 business listener | 004、026、028 | 无；后续仅需按 HTTP-only 边界落 `HttpProtocolAdapter` / `HealthProbeHandler` 与相关单测 | 证据回链到 Access 详设 6.14.8、6.20.2、`apps/gateway/CMakeLists.txt` 与 [docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md) | 若后续实现重新引入 WS/MQTT、streaming route、重量级事件循环依赖或混写 health/business listener，则重新转为 Blocked |
| ACC-BLK-005 | 已收口（2026-04-23）：ACC-TODO-005 已将 Access 侧 streaming 路径固定为 `ACC-GATE-11` 延后 Gate；在 runtime / llm / contracts 冻结 stream attach/reconnect/replay cursor 前，StreamGateway / WS / MQTT 仅允许 feature flag default-off + async receipt/poll fallback | 035、后续 stream tasks | runtime / llm / contracts 对 stream attach/reconnect/replay cursor 达成冻结结论，并形成可执行 listener / replay 测试入口 | StreamGateway / WS / MQTT 不进入 v1 Build-ready 任务；若需要断线恢复，统一回到 receipt/query/poll |
| ACC-BLK-006 | 详设 12.2-6 | ownership token 的 HMAC secret 轮换和多实例同步方案未定义 | 022、027、033 | deployment secret 管理与 constant-time compare 规则固定 | v1 先按单实例 / 部署静态 secret 实现，规模化结论不外推 |
| ACC-BLK-007 | 已解阻（2026-04-23）：platform 已冻结并实现 `IIPC::describe_peer()`，access 已落盘 `LocalPeerUidFact` 与 `DaemonProtocolAdapter` 输入接缝 | 029、033、037 | 无；后续仅需在 029/033 中基于该输入补齐 daemon local trusted 行为测试与入口接线 | 证据回链到 `platform/include/IIPC.h`、`platform/src/linux/UnixIpcProvider.cpp`、`access/src/daemon/DaemonProtocolAdapter.cpp` 与 [docs/todos/access/deliverables/ACC-TODO-037-IIPC-peer-identity接缝与LocalPeerUidFact输入收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-037-IIPC-peer-identity接缝与LocalPeerUidFact输入收敛.md) | 若后续实现绕过 `describe_peer()` 输入或在 peer identity 缺失时默认放行 local trusted，则重新转为 Blocked |

### 8.1 Blocker 校准记录

| Blocker ID | 当前判断 | 是否阻断 v1 unary + async 主链 | 备注 |
|---|---|---|---|
| ACC-BLK-001 | 高风险 | 是 | 不冻结 bridge seam，主链容易返工 |
| ACC-BLK-002 | 中风险 | 是 | config source 不清，组合根与 health/probe 容易分叉 |
| ACC-BLK-003 | 高风险 | 否，但阻断 override/diagnostics gate | 主线继续 deny-by-default |
| ACC-BLK-004 | 中风险 | 否；设计已解阻，剩余是 HTTP-only 实现接线 | gateway 继续按 HTTP-only 边界推进，不外推 WS/MQTT ready |
| ACC-BLK-005 | 中风险 | 否；005 已完成延后 Gate 收口，但仍阻断 stream / WS / MQTT ready 宣称 | 只能保留 feature flag default-off + async/poll fallback 结论 |
| ACC-BLK-006 | 中风险 | 否，但阻断规模化 receipt 结论 | 单实例实现可继续 |
| ACC-BLK-007 | 已解阻 | 否；已从设计阻塞转为实现接线事项 | peer identity seam 已冻结，后续关注 029/033 的行为覆盖完整性 |

## 9. 验收与质量门

### 9.1 测试矩阵

| 测试层级 | 覆盖范围 | 关键用例 | 目标 |
|---|---|---|---|
| unit | public surface、identity/auth/policy/admission、validator、normalizer、bridge、publisher、async registry、replay cache、health、local entry adapters | `AccessInterfaceSurfaceTest`、`AccessGatewayLifecycleTest`、`SubjectResolverTest`、`AccessPolicyGateTest`、`AdmissionControllerTest`、`RequestNormalizerTest`、`RuntimeBridgeTest`、`ResultPublisherTest`、`AsyncTaskRegistryOwnershipTest`、`DaemonProtocolAdapterLocalTrustedTest` | 组件职责、失败原因码、sidecar 边界、fail-closed 行为可二值验证 |
| contract | `AgentRequest`、`AgentResult`、`IdentityMetadata` 不被 Access 污染 | `AgentRequestContractTest`、`AgentRequestFieldContractTest`、`AgentResultContractTest`、`IdentityMetadataContractTest` | 确保 Access 不把主体/认证/回执细节挤入 shared contracts |
| integration | CLI/daemon（默认）与 HTTP（可选）unary、async receipt/query、failure/observability、health/probe、profile | `CliDaemonSmokeIntegrationTest`、`AccessAsyncReceiptIntegrationTest`、`AccessAdmissionFailureIntegrationTest`、`AccessObservabilityIntegrationTest`、`AccessHealthProbeIntegrationTest`、`AccessProfileCompatibilityTest` | 验证 Access 进入核心链路后的最小端到端行为 |
| failure injection | secret backend failure、policy backend failure、queue full、channel unavailable、owner mismatch | `AuthenticatorChainSecretFailureTest`、`AccessPolicyBackendFailureTest`、`AdmissionReplayHitTest`、`ResultPublisherChannelFailureTest`、`AccessAdmissionFailureIntegrationTest` | 验证 fail-closed、fallback 与 evidence gap 可见 |
| 延后 Gate | stream / WS / MQTT | Gate frozen only | 防止未冻结能力被错误标记为 ready |

### 9.2 统一验收命令建议

1. 核心 build + unit：`cmake --build build-ci --target dasall_access dasall_daemon dasall_cli dasall_platform dasall_unit_tests && ctest --test-dir build-ci -R "Access|CliIpcClient|HttpProtocolAdapter|DaemonProtocolAdapter|UnixIpcProviderPeerIdentity|SimulatorProtocolAdapter" --output-on-failure`
2. contracts + integration：`cmake --build build-ci --target dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -R "CliDaemonSmokeIntegrationTest|AccessAsyncReceiptIntegrationTest|AccessAdmissionFailureIntegrationTest|AccessObservabilityIntegrationTest|AccessHealthProbeIntegrationTest|AccessProfileCompatibilityTest|AgentRequestContractTest|AgentResultContractTest|IdentityMetadataContractTest" --output-on-failure`
3. discoverability：`ctest --test-dir build-ci -N | rg "Access"`

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

### 10.2 回退策略

1. 若 runtime seam 迟迟未冻结，回退策略是只保留 `AgentRequest` unary 主链接口占位，不外推 RuntimeBridge live route ready。
2. 若 gateway 选定的 HTTP-only transport 在实现阶段无法稳定接线，回退策略是先交付 CLI/daemon unary + async receipt，并将 HTTP 维持在最小 stub，不前推 WS/MQTT。
3. 若 ownership token 规模化方案未冻结，回退策略是单实例 / 静态 deployment secret 版本，并在 TODO 与 Gate 结论中明确“不外推多实例 ready”。
4. 若 override / diagnostics schema 未冻结，回退策略是入口 deny-by-default + audit，不提供任何灰色开关绕过。
5. 若 stream / WS / MQTT 设计冻结继续延后，回退策略是保留 feature flag default-off 与延后 Gate 状态，统一回落到 async receipt + poll。

## 11. 可行性结论

### 11.1 是否可直接进入执行

可以，但不是无条件全量执行。

当前可直接进入执行的范围是：ACC-TODO-001~005、006~034、037、038。ACC-TODO-035 仍受外部 shared streaming lifecycle 冻结约束，ACC-TODO-036 必须沿用 005 已冻结的延后 Gate 口径，不能把 stream / WS / MQTT 写成 Done-ready Build 结论。默认先落地 CLI -> IIPC/UDS -> daemon -> access 主链，再按已冻结的 HTTP-only 边界并行推进 gateway。

### 11.2 当前可落到的最细粒度

1. L3：public surface、supporting types、错误码、RequestValidator、RequestNormalizer、AdmissionController、AccessPolicyGate、ResultPublisher、AsyncTaskRegistry。
2. L2：ProtocolAdapterRegistry、RuntimeBridge、AccessConfigAdapter、AccessObservabilityBridge、CLI/HTTP/daemon/simulator adapters、AccessGateway。
3. L1 / 延后 Gate：StreamGateway、WS/MQTT adapters。

### 11.3 后续建议

1. 先完成 001 / 002 / 003 / 004 / 005 和 006~012、037、038，保证 Access surface、override/diagnostics gate、gateway HTTP-only 边界、stream 延后 Gate、discoverability、peer identity seam 与 CLI 依赖方向不再漂移。
2. 然后按 `SubjectResolver -> AuthenticatorChain -> AccessPolicyGate -> AdmissionController -> RequestValidator -> RequestNormalizer -> RuntimeBridge -> ResultPublisher -> AsyncTaskRegistry -> AccessGateway` 的顺序推进主链。
3. 先打通 CLI/daemon 本地控制面主链，再按 HTTP-only 边界并行打开 gateway 与 simulator；stream / WS / MQTT 保持延后 Gate + feature flag default-off，直到外部边界冻结。
4. 所有 Gate 结论必须在 ACC-TODO-036 回写到 TODO / deliverables / worklog，严禁用 build liveness 或 placeholder main 冒充 Access ready。
