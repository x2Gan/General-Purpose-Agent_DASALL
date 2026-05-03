# DASALL daemon 本地控制面专项 TODO

最近更新时间：2026-05-03
阶段：Detailed Design -> Special TODO
适用范围：`apps/daemon/`、`apps/cli/` daemon 客户端面、`access/` daemon entry seam、`platform/IIPC` peer identity 与 loopback 消费面、daemon 部署契约、相关测试拓扑
当前结论：daemon 本地控制面专项 `DMD-TODO-001` ~ `DMD-TODO-040` 已全部完成。`main.cpp` 已接入真实 `AgentFacade::init` 启动路径，`Gate-DMD-04` 已补入 `DaemonBinaryUnarySmokeTest`，`Gate-DMD-07` 已收敛为仅 `daemon.diag_enabled` 可热更且完成 focused 复验，`Gate-DMD-10` 继续保持 PASS。当前无新增外部 blocker；本轮可恢复为 close-ready 结论，后续仅保留仓库级工具链噪声与 v2 范围外能力作为残余风险。

## 1. 文档头

本文档严格基于以下输入生成：

1. `docs/architecture/DASALL_daemon本地控制面详细设计.md`
2. `docs/architecture/DASALL_Agent_architecture.md`
3. `docs/architecture/DASALL_Engineering_Blueprint.md`
4. `docs/adr/ADR-005-architecture-review-baseline.md`
5. `docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md`
6. `docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md`
7. `docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md`
8. `docs/ssot/CrossModuleDataProjectionMatrix.md`
9. `docs/ssot/InfraConcurrencyPolicy.md`
10. `docs/ssot/InfraIntegrationTopology.md`
11. `docs/plans/DASALL_工程落地实现步骤指引.md`
12. `docs/development/DASALL_工程协作与编码规范.md`
13. 现有交付记录：`docs/todos/access/deliverables`、`docs/todos/runtime/deliverables`、`docs/todos/platform/deliverables`、`docs/todos/infrastructure/deliverables`、`docs/todos/profiles`
14. 当前代码与测试现状：`apps/daemon/src/main.cpp`、`apps/daemon/src/DaemonBootstrap.*`、`apps/daemon/CMakeLists.txt`、`access/include/*`、`access/include/daemon/DaemonProtocolAdapter.h`、`access/src/*`、`access/src/daemon/DaemonProtocolAdapter.cpp`、`platform/include/IIPC.h`、`platform/src/linux/UnixIpcProvider.cpp`、`tests/unit/access/*`、`tests/unit/platform/linux/*`、`tests/integration/access/*`

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不把 daemon 扩张成 Runtime 主控、gateway 中心代理或远程公共 API。
3. 不把 socket activation、streaming attach、多实例隔离写成 v1 Done-ready 任务。
4. 不把已存在的 `IIPC::describe_peer`、`DaemonProtocolAdapter`、`AsyncTaskRegistry` 当作完全缺失对象重复规划；只规划补齐、接线和验证差距。
5. 每个任务必须同时给出代码目标、测试目标和验收命令。
6. 当前无法安全细化到函数级的 supervisor/watchdog、runtime status query、真实 OS UDS 行为，必须标明阻塞项或评审门禁。

### 1.1 专家评审补充结论

评审结论：当前 TODO 的职责边界、阶段门禁和阻塞显性化做得比较扎实，可以作为 daemon v1 的执行基线；但若直接按现表推进，仍存在 7 个交付风险：

1. `DMD-BLK-004` 已识别真实 IIPC 双向 request/response 缺口，但原计划没有给出专门 owner 任务，容易长期卡住 ping/unary/async 集成门。
2. `DaemonProtocolAdapter` 当前仍以字符串扫描解析 JSON，TODO 需要明确 frame codec、schema_version、escaping、payload 上限与错误映射的硬化任务。
3. 当前 `ping` 在 `DaemonBootstrap` 内特判并绕过 Access command/policy 路径，TODO 需要约束 ping/readiness 也走统一 daemon command router，只允许无特权轻量处理，不允许形成旁路。
4. CLI 侧现在只验证 `send()` 成功，没有读取 daemon 响应；TODO 需要补齐 CLI-daemon v1 wire contract 和响应解析验收。
5. UDS 的 socket_path、权限、owner/group、stale socket 清理策略尚未单列任务；这是本地控制面安全交付的必备项。
6. SIGHUP/hot-reload 在详设中有矩阵，但原子任务只记录 signal intent，缺少 allowlist、不可热更键拒绝和配置快照切换任务。
7. 长期运行 daemon 需要并发、背压、draining 与 soak 组合门；原计划有单点单测，但缺少集成压力与资源泄漏验收。

因此本次补强新增 `DMD-TODO-029` ~ `DMD-TODO-035`，并将其纳入执行顺序、阻塞解法、验收矩阵与最终 Gate。新增项不改变 v1 范围：仍然只交付本地 direct-bind UDS、unary + accepted_async、ping/status/cancel/只读 diag、readiness 和 graceful shutdown；socket activation、远程控制面、streaming attach 继续作为 v2 演进。

行业实践校验：

1. Docker daemon 文档明确强调持久配置优先使用配置文件，命令行 flags 与配置文件重复配置会导致 daemon 启动失败；本 TODO 继续保留 validate-only 与冲突显式失败要求。
2. systemd daemon 指南建议新式守护进程将监听 socket 与 supervisor 激活能力解耦；v1 不强行实现 socket activation，但必须保持 `DaemonSupervisorAdapter` 和 listener abstraction 的演进空间。
3. systemd `sd_notify` 语义提供 READY、STOPPING、WATCHDOG 等进程状态通知；本 TODO 将其抽象为可 no-op 的通知面，避免 v1 与具体 service manager 强绑定。
4. OWASP 授权基线要求默认拒绝并逐请求校验权限；本 TODO 将本地 UDS 请求、diag、override、status/cancel 全部纳入 command taxonomy 与 PolicyGate。
5. OpenTelemetry semantic conventions 强调 trace、metric、log 采用统一命名以提升跨平台可观测性；本 TODO 要求 daemon 日志、指标、审计至少携带 request_id/session_id/trace_id/daemon_state/connection_ref/receipt_ref。

## 2. 子系统目标与范围

### 2.1 子系统目标

1. 将 daemon 收敛为 DASALL 本地控制面的常驻服务进程，作为 CLI 进入 `Access -> Runtime` 主链的本地服务入口。
2. 在 `apps/daemon` 中补齐进程生命周期、配置校验、UDS listener、signal、readiness、draining shutdown 与 supervisor/watchdog 预留。
3. 通过共享 `access/` core 完成本地主体识别、认证授权、Admission、归一化、Runtime bridge、结果发布、receipt/status/cancel 与观测审计。
4. 继续保持 `runtime/AgentFacade` 和 `AgentOrchestrator` 的全局主控权，daemon 不持有业务状态机、预算裁定、恢复执行权或调度权。
5. v1 只承诺本地 direct-bind UDS、unary + accepted_async、ping/status/cancel/只读 diag、readiness 与 graceful shutdown。

### 2.2 范围边界

纳入本专项 TODO 的对象：

1. `apps/daemon/src/main.cpp`、`DaemonBootstrap.*`、新增 daemon config / lifecycle / listener / signal / supervisor 组件。
2. `access/include/daemon` 与 `access/src/daemon` 的 daemon frame、adapter、health、receipt/status/cancel 命令面。
3. `access/src` 既有 `SubjectResolver`、`AuthenticatorChain`、`AccessPolicyGate`、`AdmissionController`、`RequestNormalizer`、`RuntimeBridge`、`ResultPublisher`、`AsyncTaskRegistry` 在 daemon 主链中的接线与验证。
4. `platform/include/IIPC.h` 与 `platform/src/linux/UnixIpcProvider.cpp` 的 peer identity 消费验证和真实 UDS 行为阻塞跟踪。
5. `apps/cli/src` 的 daemon v1 command/response contract、响应解析和输出格式化。
6. `docs/deploy/daemon` 或等价位置的 direct-bind v1 部署契约、配置样例和运维验收说明。
7. `tests/unit/apps/daemon`、`tests/unit/access`、`tests/integration/access` 的 daemon 专项测试拓扑和 gate。
8. 交付证据回写到本 TODO、`docs/todos/daemon/deliverables` 与 `docs/worklog`。

不纳入本专项 TODO 的对象：

1. 远程 HTTP/gRPC/TCP 控制面。
2. 完整 streaming attach / subscribe 协议。
3. systemd fd inheritance 式 socket activation 正式交付。
4. Runtime 主状态机、RecoveryManager、CheckpointManager、Scheduler 的内部实现。
5. cognition、llm、tools、memory、knowledge 的生产实现。
6. shared contracts 新字段 admission；daemon 私有 IPC 对象不得进入 `contracts/`。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| DMD-TC001 | daemon 详设 1.1、1.3、6.1；蓝图 3.2 | Must | daemon 是本地 Access owner，但 `apps/daemon` 只负责进程壳层、监听绑定、signal 与 adapter 装配 | 任务限定在入口壳层和共享 access core 接线，不复制 access 主链 |
| DMD-TC002 | daemon 详设 1.1、2、6.3；SSOT Access sidecar | Must | Access 是认证、授权、Admission、归一化、发布和观测投影 owner | daemon 请求处理必须经 `IAccessGateway` / access core，不能绕过 PolicyGate |
| DMD-TC003 | ADR-008；daemon 详设 1.1、6.1、6.12 | Must-Not | Runtime 仍是全局主控，daemon 不得形成第二主循环、第二调度中心或第二恢复裁定点 | receipt/status/cancel 只能保存映射和转发控制，不实现业务任务系统 |
| DMD-TC004 | ADR-006、ADR-007；daemon 详设 1.1、6.1 | Must-Not | daemon 不拥有 ContextPacket 装配、Prompt 渲染、失败语义判断或恢复执行 | 不生成 daemon 直接调用 cognition/llm/tools/memory 的任务 |
| DMD-TC005 | daemon 详设 6.4.3、6.7；当前 `IIPC.h` | Must | 本地主体可信事实必须来自 platform peer identity；缺失时 fail-closed | 保留 `IIPC::describe_peer`，补齐 privileged command 缺失 peer identity 的拒绝测试 |
| DMD-TC006 | daemon 详设 6.4.1、6.4.2；SSOT Access sidecar | Must | `UdsRequestFrame`、`UdsResponseFrame`、`PeerIdentitySnapshot`、`SubjectIdentity` 等为 module-local 或 module public，不进入 contracts | 任务只新增 access/platform/apps 公共面，不修改 `contracts/` |
| DMD-TC007 | daemon 详设 6.10；profiles 详设约束 | Must | daemon 配置服从 ConfigCenter 四层模型和 Profile 治理，不自建旁路配置体系 | 必须拆出 `DaemonBootstrapConfig`、配置投影、冲突校验和 validate-only 任务 |
| DMD-TC008 | daemon 详设 6.10.3；dockerd 实践已由详设吸收 | Should | flags 与配置文件同键冲突必须拒绝启动，支持 validate-only | 配置任务必须先于 listener bind 和业务请求任务 |
| DMD-TC009 | daemon 详设 6.7；OWASP 授权基线已由详设吸收 | Must | 本地 socket 不等于无限可信；默认拒绝、逐请求授权、diag/override 独立守门 | `ping/status/cancel/diag/override` command taxonomy 必须明确 |
| DMD-TC010 | daemon 详设 6.8；当前 `AsyncTaskRegistry` | Must | accepted_async 只能形成 receipt 映射、owner 校验和 TTL 清理，不得保存业务主状态机 | 复用/扩展 `AsyncTaskRegistry`，不新增第二套任务系统 |
| DMD-TC011 | daemon 详设 6.9、6.11；infra health/logging/audit 设计 | Must | daemon 必须区分 ping、readiness、diagnostics，并输出最小结构化观测字段 | 拆出 health service、readiness command、observability/audit 任务 |
| DMD-TC012 | daemon 详设 6.12；SSOT 并发策略 | Must | shutdown 进入 Draining，拒绝新请求，排空 inflight；不得持锁执行 socket write、JSON 序列化或审计导出 | lifecycle、listener、receipt、publish 任务必须显式验证锁顺序和排空 |
| DMD-TC013 | 蓝图 4.1、4.2、4.3 | Must | `apps/daemon` 依赖 `access/runtime/contracts/infra/platform/profiles`，不得依赖 cognition/llm/tools/memory/knowledge 实现；跨模块优先走接口 | CMake 与 include 任务必须检查依赖方向，涉及具体实现 include 需评审 |
| DMD-TC014 | 工程规范 3.1~3.7 | Must | C++20、头文件在 include、实现放 src、边界错误可观测、新公共接口同步测试 | 每项 Build 任务必须绑定 unit/integration/contract 或 discoverability 验收 |
| DMD-TC015 | 当前代码现状 | Evidence | `IIPC::describe_peer`、`PeerIdentitySnapshot`、`DaemonProtocolAdapter`、`DaemonBootstrap`、`AsyncTaskRegistry` 已存在 | TODO 以补齐字段、接线、真实集成验证和 gate 收口为主 |
| DMD-TC016 | 当前测试现状 | Evidence | 已有 `DaemonProtocolAdapterTest`、`DaemonProtocolAdapterLocalTrustedTest`、`UnixIpcProviderPeerIdentityTest`、`DaemonLoopbackFixtureTest`、`DaemonPingIntegrationTest`；`tests/unit/apps/daemon` 已注册，CLI 侧仍未消费 daemon response | 真正 daemon ping gate 已具备，后续重点转到 unary / async 集成与 CLI wire contract |
| DMD-TC017 | 当前 IIPC 现状；daemon 详设 6.6.2、9 | Must | daemon E2E 必须证明 client send 可被 listener receive，server response 可被 client receive；只验证 `send()` 成功不能算 ping 集成 | 必须新增 IIPC loopback/真实 UDS 解阻任务，并把 `DMD-BLK-004` 从“风险提示”变成可执行前置项 |
| DMD-TC018 | daemon 详设 6.4.1、6.6.2；安全评审 | Must | frame codec 不得依赖安全敏感字段的临时字符串扫描；必须校验 schema_version、command、payload 边界、escaping 与 malformed error mapping | `DaemonProtocolAdapter` 任务必须补安全硬化和畸形帧回归 |
| DMD-TC019 | 当前 `DaemonBootstrap::handle_connection()` | Must | ping/readiness 不能在 Bootstrap 内形成永久旁路；允许无特权轻量命令，但仍需走统一 command router、frame decode、peer fact 和审计字段投影 | `DMD-TODO-019` 必须删除静态 ping 特判或将其降级为 router 内部 handler |
| DMD-TC020 | CLI 详设与当前 `CliIpcClient` | Must | CLI-daemon 协议必须读取并解析 daemon 响应，区分 pong、accepted、rejected、receipt、not_ready；不能只以 send 成功作为业务成功 | 新增 CLI wire contract 与 response parsing 任务 |
| DMD-TC021 | daemon 详设 6.7.3、6.12 | Must | UDS socket_path、owner/group、mode、stale socket 检测与清理需要显式策略；不得删除不属于当前 daemon 的活动 socket | 新增 UDS endpoint 安全策略任务，并纳入 bind conflict/failure gate |
| DMD-TC022 | daemon 详设 6.10.4 | Should | hot-reload 只能作用于 allowlist 键，listener/socket_path/startup_mode/dispatch_workers 等 v1 不可热更键必须拒绝并可观测 | 新增 SIGHUP reload allowlist 与配置快照切换任务 |
| DMD-TC023 | daemon 详设 6.5.2、6.11、6.12；长期运行守护进程实践 | Should | daemon 交付必须有并发、背压、draining、receipt TTL 清理和资源释放的组合门；单点单测不足以证明长期驻留能力 | 新增 concurrency/backpressure/soak gate |

### 3.2 当前代码与测试现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| `apps/daemon/src/main.cpp` | 构造 `UnixIpcProvider` 与 concrete `AccessGateway`，硬编码 `/tmp/dasall-daemon-control.sock`，注册 SIGTERM/SIGINT，调用 `DaemonBootstrap::run()` | 已有最小组合根，但缺配置模型、validate-only、readiness、supervisor adapter、draining 状态机 |
| `apps/daemon/src/DaemonBootstrap.*` | 提供 `run(endpoint)`、`stop()`、`handle_connection()`；单线程同步 accept；ping 直接绕过 `IAccessGateway` 回 JSON | 可作为 M1 骨架，但需拆分 lifecycle/listener/signal，接入配置和共享 access 主链 |
| `access/include/daemon/DaemonProtocolAdapter.h` | 已定义 `can_handle`、`decode`、`encode`、`set_active_channel`、`describe_local_peer_uid_fact` | adapter 接缝存在，但缺正式 `UdsRequestFrame/UdsResponseFrame` 类型与完整字段解析 |
| `access/src/daemon/DaemonProtocolAdapter.cpp` | 采用最小字符串扫描解析 JSON，支持 `op=ping`、`packet_id`、`payload`、`peer_ref`、`async_preferred` | 需替换为结构化 frame 解码/编码与 malformed frame 错误映射 |
| `platform/include/IIPC.h` | 已新增 `PeerIdentitySnapshot` 和 `IIPC::describe_peer(handle)` | platform public seam 已存在；后续重点是 fail-closed 测试与真实 UDS/loopback 行为验证 |
| `platform/src/linux/UnixIpcProvider.cpp` | loopback provider 已形成 listener/client 双向 request/response，`accept/connect/receive` 不再伪造成功 | 真实 daemon ping 集成的平台层阻塞已清除，可继续作为 unary/async/failure 集成夹具底座 |
| `access/CMakeLists.txt` | `dasall_access` 已 PUBLIC 链接 `dasall_platform` 并编译 daemon adapter 与 access core 文件 | `dasall_access` 依赖 platform 的历史阻塞已解，后续做接线和 gate |
| `tests/unit/access/CMakeLists.txt` | 已注册 daemon adapter、local trusted、async registry、task query、runtime bridge、publisher 等测试 | access 层基础单测较完整，但 daemon command 集成仍缺 |
| `tests/unit/platform/linux/CMakeLists.txt` | 已注册 `UnixIpcProviderPeerIdentityTest` | peer identity 接口面已有自动化入口 |
| `tests/integration/access/DaemonPingIntegrationTest.cpp` | 启动 in-process daemon fixture，发送 v1 ping frame，并读取 completed response 与 readiness 摘要 | 当前 ping 测试已是 daemon 服务集成，不再是 IPC client smoke |
| `apps/cli/src/CliIpcClient.cpp` | ping/submit 当前以 `connect/send/close` 成功作为返回依据，未消费 `receive()` 响应 | CLI-daemon wire contract 尚未闭环，status/cancel/readiness/diag 的客户端语义不能自然验收 |
| `platform/src/linux/UnixIpcProvider.cpp` | `send()` 只返回 bytes_sent，`receive()` 对普通 channel 返回空 payload；accepted channel 与 connected channel 未形成双向队列 | 必须补 loopback fixture 或真实 UDS 行为，才能避免 daemon 集成测试虚绿 |
| `DaemonProtocolAdapter.cpp` | 通过 `extract_json_string` / `extract_json_bool` 做简化扫描，响应 JSON 直接字符串拼接 | 需要 frame codec 安全硬化，至少覆盖 escaping、schema_version、未知字段、malformed payload 与错误映射 |
| UDS socket 权限与 stale socket | 当前 `main.cpp` 硬编码 `/tmp/dasall-daemon-control.sock`，listener skeleton 通过字符串包含 `in-use` 模拟地址冲突 | 缺少 socket_path 受管配置、父目录权限、mode/owner/group、stale socket 安全清理和 bind conflict 真实策略 |

## 4. 粒度可行性评估

### 4.1 总体结论

结论：当前可直接生成 L3/L2 混合专项 TODO，不能整体按纯 L3 推进。

当前最细可安全落盘粒度：

1. L3：`DaemonLifecycleController::start()`、`DaemonLifecycleController::shutdown(timeout)`、`DaemonListenerHost::bind()`、`DaemonListenerHost::accept_loop()`、`DaemonProtocolAdapter::decode()`、`DaemonProtocolAdapter::encode()`、`IIPC::describe_peer()` fail-closed 消费、`AsyncTaskRegistry::query_receipt()` / `validate_ownership()` / `mark_completed()` 在 daemon command 中的调用路径。
2. L2：`DaemonBootstrapConfig`、`DaemonProcessContext`、`DaemonReadinessSnapshot`、`UdsRequestFrame`、`UdsResponseFrame`、daemon command taxonomy、health/readiness/diag 服务、observability/audit bridge、profile 配置投影。
3. L1：`DaemonSupervisorAdapter` 对外 supervisor/watchdog 通知面、真实 OS UDS 行为、Runtime status query surface；详设明确方向但当前代码/接口证据不足以安全拆到函数级。
4. L0：socket activation、远程控制面、streaming attach、多 daemon 实例隔离；均为 v2 或后续演进，不进入 v1 Build-ready 清单。

判断依据：

1. daemon 详设已给出核心对象字段、核心接口名、主流程、异常流程、目录建议、测试目标和 Design -> Build 映射，满足 L2/L3 拆分多数条件。
2. 当前代码已具备部分接口和测试基础，能够把 TODO 从“从零补口”校准为“补齐字段、拆分职责、接线主链、扩展 gate”。
3. 阻碍纯 L3 的缺口不是 daemon 边界不清，而是部分跨模块实现面仍不足：真实 IIPC 双向传输、runtime status query、supervisor/watchdog 通知 surface、profile daemon 键资产。
4. 因此本专项 TODO 可以进入执行，但 DMD-TODO-001、002、023、024 必须优先完成，且 DMD-TODO-008、016、024 标记阻塞/评审门禁。
5. 本次评审将 `真实 IIPC 双向传输` 从单纯 blocker 升级为 `DMD-TODO-029`，并新增 frame codec、CLI response、UDS endpoint、安全热重载、并发 soak、部署契约等收口任务；这些任务不扩大 daemon 业务范围，但会显著降低交付虚绿和运维不可控风险。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| `DaemonBootstrapConfig` | 详设 6.4.1、6.10 | L2 | 字段、默认值、覆盖层级、冲突规则明确 | profile 资产内 daemon 键是否完整未验证 | 拆为配置类型、投影/校验、validate-only 三个任务 |
| `DaemonProcessContext` | 详设 6.4.1、6.6.1 | L2 | 字段和 build 成功后只读约束明确 | 具体 dependency factory 口径未冻结 | 通过 `DaemonBootstrap::build(config)` 组合根任务收敛 |
| `DaemonLifecycleController` | 详设 6.4.2、6.5.1、6.12 | L3 | 状态、转移、错误语义、shutdown 流程明确 | 当前代码尚未拆出类 | 直接拆 start/shutdown/state 单元任务 |
| `DaemonListenerHost` | 详设 6.2、6.4.2、6.6.2 | L3 | bind/accept_loop/close 语义和错误映射明确 | 当前 `DaemonBootstrap::run()` 混合 listener 与 dispatch | 拆出 direct-bind listener host，并保留 socket activation 为 v2 |
| `DaemonSignalHandler` | 详设 6.2、6.5 | L3 | SIGTERM/SIGINT/SIGHUP 受控响应职责明确 | 当前只有 main.cpp 全局 handler | 拆出独立组件并测 stop/reload 行为 |
| `DaemonSupervisorAdapter` | 详设 6.2、6.9.3 | L1 | READY/STOPPING/WATCHDOG 通知面方向明确 | 具体 external supervisor / infra watchdog 接口选择未冻结 | 先做 v1 no-op/infra event adapter，标评审门禁 |
| `UdsRequestFrame` / `UdsResponseFrame` | 详设 6.4.1、6.6.2 | L2 | 字段、schema_version、request_id、trace_id、command、async_preference 已明确 | 当前没有正式类型，JSON 解析为字符串扫描 | 先定义 frame 类型，再改 adapter |
| `DaemonProtocolAdapter` | 详设 6.2、6.3、6.4.2 | L3 | `decode/encode/describe_local_peer_uid_fact` 已存在 | malformed frame、字段完整性、error mapping 不足 | 以补齐 schema 与错误路径为主 |
| `PeerIdentitySnapshot` / `IIPC::describe_peer` | 详设 6.4.3、6.7；当前 `IIPC.h` | L3 | 接口与 platform 单测已存在 | 真实 UDS peer credential 行为未闭环 | daemon 侧只做消费和 fail-closed；真实 OS 行为作为平台阻塞项 |
| `SubjectResolver` / `AuthenticatorChain` / `AccessPolicyGate` | 详设 6.6.2、6.7；当前 access 实现 | L3 | 类与多组单测已存在 | daemon command taxonomy 与 diag/override 分类需接线 | 拆入 daemon 主链接线和 privileged command gate |
| `AdmissionController` / `RequestNormalizer` / `RuntimeBridge` / `ResultPublisher` | 详设 6.6.2；当前 access 实现 | L3 | 类、接口和单测已存在 | apps/daemon 当前使用空 `AccessGateway` pipeline | 拆出 daemon composition root 接线与 unary integration |
| receipt/status/cancel | 详设 6.8；当前 `AsyncTaskRegistry` / gateway `TaskQueryHandler` | L2 | receipt 字段、owner 校验、TTL、cancel registry 行为存在 | daemon command routing 缺失；runtime status query surface 不足 | 复用 `AsyncTaskRegistry`，status 首版 registry-only；runtime query 标阻塞 |
| ping/readiness/health | 详设 6.9 | L2 | ping/readiness 区分、健康维度和输出字段明确 | 当前 ping 绕过 health service，readiness snapshot 缺失 | 拆类型、service、integration |
| diagnostics | 详设 6.9、6.10、6.11；infra diagnostics 设计 | L2 | 只读诊断、policy gate、profile 开关约束明确 | daemon diag command shape 与 infra facade 绑定未落盘 | 先做只读命令族和 deny gate，远程导出排除 |
| observability/audit | 详设 6.11；SSOT Access sidecar | L2 | 日志字段、指标、审计事件清单明确 | daemon access bridge 的字段投影未接线 | 拆出字段集单测和 failure/audit integration |
| tests/CMake | 详设 6.13、9；当前 tests | L2 | 现有 access/platform 测试宏和集成拓扑可复用 | `tests/unit/apps/daemon` 缺失；daemon E2E 缺失 | 先注册拓扑，再跑 ping/unary/async/shutdown/profile gate |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| daemon v1 命令面与 UDS frame schema | 6.4.1、6.6.2、6.7、6.8、6.9 | 接口定义 / 数据结构 | DMD-TODO-001、011 | 先冻结 module-local frame 和 command taxonomy，再补 adapter |
| daemon 配置、Profile 与启动校验 | 6.4.1、6.10 | 配置 / Profile 裁剪 / 生命周期 | DMD-TODO-002、003、004、009 | 消除 hardcoded socket path 和半启动风险 |
| 进程生命周期、listener、signal、supervisor | 6.2、6.5、6.6.1、6.9.3、6.12 | 生命周期 / 初始化 / 适配器 | DMD-TODO-005、006、007、008、022 | 把当前 `DaemonBootstrap` 单体拆成可测组件 |
| peer identity 与默认拒绝 | 6.4.3、6.7 | 安全 / 错误处理 | DMD-TODO-012 | 使用已存在 `IIPC::describe_peer`，补 fail-closed gate |
| Access 主链接线与 Runtime bridge | 6.3、6.6.2 | 适配器 / 桥接 / 主流程 | DMD-TODO-013、014、025 | 从空 pipeline 升级为共享 access core -> runtime unary |
| accepted_async、status、cancel | 6.8 | 数据结构 / 错误处理 / owner 校验 | DMD-TODO-015、016、017、026 | 复用 `AsyncTaskRegistry`，补 daemon command routing |
| ping、readiness、diagnostics、watchdog | 6.9、6.10、6.11 | 健康 / 诊断 / 运维入口 | DMD-TODO-018、019、020、008 | 区分 ping 和正式 diag，watchdog 先最小预留 |
| 可观测性、审计与 failure path | 6.11、6.12 | 异常 / 审计 / 质量门 | DMD-TODO-021、022、027 | 结构化字段、fail-closed、draining abandoned 可验证 |
| IIPC 双向通信与 daemon fixture | 6.6.2、9 | 平台补口 / 测试夹具 / 集成解阻 | DMD-TODO-029 | 把 `DMD-BLK-004` 从长期阻塞转为可验收解阻任务 |
| frame codec 安全硬化 | 6.4.1、6.6.2、6.12 | 协议安全 / 错误映射 / 回归测试 | DMD-TODO-030 | 替换临时字符串扫描的安全敏感路径，补 schema/version/escaping/malformed 测试 |
| CLI-daemon wire contract | CLI 详设、daemon 详设 6.9 | 客户端协议 / 响应解析 / 用户入口 | DMD-TODO-031 | 让 CLI 验收从 `send()` 成功升级为读取稳定响应 |
| UDS endpoint 权限与 stale socket | 6.7.3、6.12 | 本地安全 / 部署策略 / bind preflight | DMD-TODO-032 | 明确 socket_path、mode、owner/group、stale 清理和冲突拒绝 |
| SIGHUP reload allowlist | 6.10.4 | 配置热重载 / 生命周期 / 审计 | DMD-TODO-033 | 只允许安全键热更，不可热更键必须拒绝并保留当前快照 |
| 并发背压与长期运行 soak | 6.5.2、6.11、6.12 | 并发 / 可靠性 / 资源治理 | DMD-TODO-034 | 验证 worker、Admission、draining、receipt TTL、资源释放组合行为 |
| 部署与 supervisor 交付契约 | 6.9.3、10、11 | 部署文档 / service manager / 运维验收 | DMD-TODO-035 | v1 交付 direct-bind systemd/服务样例和最小操作手册，socket activation 仅预留 |
| daemon/CLI 默认 endpoint 与入口契约一致性 | 6.7.3、6.10、6.13 | 配置 / CLI 契约 / 部署复验 | DMD-TODO-036 | 统一 daemon 与 CLI 默认 socket_path，并补 CLI 显式覆盖与正向 smoke |
| 真实 UDS socket mode 与 stale restart 收敛 | 6.7.3、6.12 | UDS 安全 / restart 恢复 / failure gate | DMD-TODO-037 | 让 daemon 实际创建的 socket 权限与 cleanup policy 对齐，并补重启回归 |
| daemon 入口 profile/config snapshot 真接线 | 6.6.1、6.10.1、6.10.3 | 组合根 / 配置来源 / profile 投影 | DMD-TODO-038 | 把 profile / deployment snapshot 从测试 helper 接到真实 main/bootstrap 入口 |
| hot-reload 真实快照源与正向复验 | 6.10.4、6.11 | 配置热重载 / 审计 / 生命周期 | DMD-TODO-039 | 让 SIGHUP 读取新快照而不是重复 apply 初始 config，并补正向 gate |
| 文档、陈旧测试与专项 Gate 复验回写 | 7、9、11 | 文档 / 证据 / 质量门复验 | DMD-TODO-040 | 清理过期口径并重写 Gate-DMD-05/07/09/10 的权威证据 |
| CMake/test topology 与交付证据 | 6.13、7、8、9 | 测试 / 门禁 / 文档回写 | DMD-TODO-023、024、025、026、027、028 | 先解决 discoverability，再收敛 gate evidence |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 接口定义类任务 | 是 | DMD-TODO-001、011、018 |
| 数据结构定义类任务 | 是 | DMD-TODO-001、002、018 |
| 生命周期与初始化类任务 | 是 | DMD-TODO-004、005、006、007、009、022 |
| 适配器 / 桥接类任务 | 是 | DMD-TODO-008、011、013、014、020 |
| 异常与错误处理类任务 | 是 | DMD-TODO-012、016、017、020、022、027 |
| 配置与 Profile 裁剪类任务 | 是 | DMD-TODO-002、003、004、019、027 |
| 测试与门禁类任务 | 是 | DMD-TODO-023、024、025、026、027、029、034 |
| 文档 / 交付证据回写类任务 | 是 | DMD-TODO-028、035 |


## 6. 原子任务清单

> **DMD-TODO-032 已完成，交付物见 deliverables/DMD-TODO-032-UDS-endpoint安全策略收敛.md。**

说明：表内验收命令表示任务完成后必须存在并通过的命令。当前尚不存在的测试文件或测试名，必须由对应任务同步新增和注册，不能只写代码不接入 `ctest -N`。

### 6.1 前置 schema、配置与组件拆分任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMD-TODO-001 | Done | 定义 daemon v1 命令 taxonomy 与 UDS frame 类型 | daemon 详设 6.4.1、6.6.2、6.7、6.8、6.9；当前 `DaemonProtocolAdapter` | 6.4.1 `UdsRequestFrame` / `UdsResponseFrame` | L2 | 新增 `access/include/daemon/DaemonProtocolTypes.h`，更新 `access/CMakeLists.txt` public headers | `DaemonCommandKind`、`UdsRequestFrame`、`UdsResponseFrame`、`DaemonFrameDecodeError` | unit：`DaemonProtocolTypesTest` 验证字段默认值、schema_version、command 分类、async preference | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonProtocolTypesTest" --output-on-failure` | 无 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-001-daemon命令taxonomy与frame类型收敛.md`；新增头文件和测试 | 仅当 ping/run/status/cancel/diag/unknown 命令分类可二值断言，且类型未进入 `contracts/` 时完成 |
| DMD-TODO-002 | Done | 定义 DaemonBootstrapConfig 与 DaemonProcessContext | daemon 详设 6.4.1、6.10；当前 main 硬编码 socket path | 6.4.1 `DaemonBootstrapConfig` / `DaemonProcessContext` | L2 | 新增 `apps/daemon/src/DaemonConfig.h`，更新 `apps/daemon/CMakeLists.txt` | `DaemonBootstrapConfig`、`DaemonProcessContext`、`DaemonStartupMode`、`DaemonConfigConflict` | unit：`DaemonBootstrapConfigTest` 验证默认值、非法 socket_path、worker 数、TTL、grace window | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonBootstrapConfigTest" --output-on-failure` | 无 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-002-DaemonBootstrapConfig收敛.md`；新增配置头与测试 | 仅当详设 6.10.2 中所有 v1 配置键都有类型承载和非法值断言时完成 |
| DMD-TODO-003 | Done | 收敛 daemon Profile 配置投影来源 | daemon 详设 6.10；profiles 详设；当前 profiles 资产 | 6.10.1 ConfigCenter 四层模型 | L2 | 更新 `profiles/*` daemon 键资产或新增 `profiles/include` 投影 helper；不得在 `apps/daemon` 写旁路配置表 | `daemon.socket_path`、`daemon.listen_backlog`、`daemon.dispatch_timeout_ms`、`daemon.diag.enabled`、`daemon.watchdog.enabled` | unit/integration：`DaemonProfileProjectionTest` 验证五档 profile 下 daemon 键存在且默认安全 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "DaemonProfileProjectionTest|ProfileMatrixConsistencyTest" --output-on-failure` | DMD-TODO-002 | DMD-BLK-001 | profiles daemon 键落盘并经 profile 负责人评审 | `docs/todos/daemon/deliverables/DMD-TODO-003-daemon-profile配置投影收敛.md`；profile 资产或投影 helper | 仅当 daemon 配置完全来自 ConfigCenter/Profile/部署覆盖，不依赖 main.cpp 常量时完成 |
| DMD-TODO-004 | Done | 实现 DaemonConfigValidator 与 validate-only 路径 | daemon 详设 6.10.3、6.12；工程规范 3.6 | 6.10.3 配置校验与冲突规则 | L3 | 新增 `apps/daemon/src/DaemonConfigValidator.{h,cpp}`，更新 main 参数解析最小入口 | `validate_config()`、`validate_conflicts()`、`validate_only()` | unit：`DaemonConfigValidatorTest` 验证 flags/config 冲突、socket_path 空、payload 上限、hot-reload 禁止键 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_daemon dasall_unit_tests && ctest --test-dir build-ci -R "DaemonConfigValidatorTest" --output-on-failure` | DMD-TODO-002 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-004-DaemonConfigValidator收敛.md`；新增 validator | 仅当配置非法在 listener bind 前失败，且 validate-only 不创建 listener 时完成 |
| DMD-TODO-005 | Done | 实现 DaemonLifecycleController 状态机 | daemon 详设 6.4.2、6.5.1、6.12；当前 `DaemonBootstrap::run/stop` | 6.5.1 Bootstrapping -> Binding -> Ready -> Draining -> Stopped | L3 | 新增 `apps/daemon/src/DaemonLifecycleController.{h,cpp}`，从 `DaemonBootstrap` 迁移状态推进 | `start()`、`mark_binding()`、`mark_ready()`、`shutdown(timeout)`、`state()` | unit：`DaemonLifecycleControllerTest` 验证合法转移、非法转移、draining 拒绝新请求、drain timeout | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonLifecycleControllerTest" --output-on-failure` | DMD-TODO-002、004 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-005-DaemonLifecycleController收敛.md`；新增 lifecycle 组件 | 仅当状态表中的每个 v1 状态都有确定的新请求行为和观测语义断言时完成 |
| DMD-TODO-006 | Done | 实现 DaemonListenerHost direct-bind 监听层 | daemon 详设 6.2、6.4.2、6.6.2；当前 `DaemonBootstrap::run` | 6.6.2 listener accept 主流程 | L3 | 新增 `apps/daemon/src/DaemonListenerHost.{h,cpp}`，从 `DaemonBootstrap::run()` 迁出 listener/accept/close | `bind(endpoint)`、`accept_loop()`、`close()`、`set_connection_handler()` | unit：`DaemonListenerHostTest` 验证 bind 参数、accept timeout、close 后拒绝、listener error mapping | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonListenerHostTest" --output-on-failure` | DMD-TODO-002、005 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-006-DaemonListenerHost收敛.md`；新增 listener host | 仅当 listener host 不再混合 decode/submit/publish 逻辑，且 direct-bind 路径可单测时完成 |
| DMD-TODO-007 | Done | 实现 DaemonSignalHandler 受控 signal 响应 | daemon 详设 6.2、6.5；当前 main 全局 signal handler | 6.5 生命周期控制 | L3 | 新增 `apps/daemon/src/DaemonSignalHandler.{h,cpp}`，替换 main.cpp 全局裸指针处理 | `install_handlers()`、`request_shutdown()`、`request_reload()`、`last_signal()` | unit：`DaemonSignalHandlerTest` 验证 SIGTERM/SIGINT 触发 shutdown、SIGHUP 只触发 reload intent | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonSignalHandlerTest" --output-on-failure` | DMD-TODO-005 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-007-DaemonSignalHandler收敛.md`；新增 signal 组件 | 仅当 main 不再持有裸全局 bootstrap 指针，且 signal 行为可测试时完成 |
| DMD-TODO-008 | Done | 实现 DaemonSupervisorAdapter 最小通知面 | daemon 详设 6.2、6.9.3；infra watchdog 设计 | 6.9.3 READY/STOPPING/WATCHDOG 通知 | L1 | 新增 `apps/daemon/src/DaemonSupervisorAdapter.{h,cpp}`；v1 支持 no-op 与 infra watchdog event 两种注入实现 | `notify_ready()`、`notify_stopping()`、`tick_watchdog()` | unit：`DaemonSupervisorAdapterTest` 验证无 supervisor 时 no-op 成功、watchdog disabled 不影响主流程 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonSupervisorAdapterTest|WatchdogServiceInterfaceTest" --output-on-failure` | DMD-TODO-002、005 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-008-DaemonSupervisorAdapter收敛.md`；新增 supervisor adapter | 仅当 v1 no-op 路径不阻塞 daemon 启动，且 watchdog 失败不会由 daemon 自行恢复裁定时完成 |
| DMD-TODO-009 | Done | 接线 DaemonBootstrap::build 组合根 | daemon 详设 6.4.2、6.6.1；当前 `DaemonBootstrap` 构造器 | 6.6.1 启动主流程 | L3 | 重构 `apps/daemon/src/DaemonBootstrap.{h,cpp}`，引入 config、context、lifecycle、listener、supervisor 依赖 | `DaemonBootstrap::build(config)`、`DaemonBootstrap::run(context)`、`DaemonBootstrap::stop()` | unit：`DaemonBootstrapTest` 验证 config invalid 不 bind、gateway not ready 不接受请求、build 失败不返回半初始化 context | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_daemon dasall_unit_tests && ctest --test-dir build-ci -R "DaemonBootstrapTest" --output-on-failure` | DMD-TODO-002、004、005、006、007 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-009-DaemonBootstrap组合根收敛.md`；更新 bootstrap | 仅当 `DaemonBootstrap` 不再同时承担 config 校验、listener、decode、submit、publish 五类职责时完成 |

### 6.2 Access 主链、协议、安全与 Runtime 接线任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMD-TODO-010 | Done | 收敛 daemon composition root 的跨模块 include 边界 | 蓝图 4.3；工程规范 3.3；当前 main include `access/src/AccessGateway.h` | 6.3 依赖方向冻结 | L2 | 在 `access/include` 暴露受控 factory，更新 `apps/daemon/CMakeLists.txt` 与 `apps/daemon/src/main.cpp`，移除 daemon 对 `access/src` 的 include 依赖 | `create_access_gateway()` | unit/build：`AccessInterfaceSurfaceTest`、`dasall_daemon` 构建验证依赖方向 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_daemon dasall_unit_tests && ctest --test-dir build-ci -R "AccessInterfaceSurfaceTest" --output-on-failure` | DMD-TODO-009 | 无 | 已满足：daemon composition root 已改走 access public factory；`apps/gateway` 的同口径对齐不属于本行范围 | `docs/todos/daemon/deliverables/DMD-TODO-010-daemon-composition-root-include边界收敛.md`；`access/include/AccessGatewayFactory.h`；`access/src/AccessGatewayFactory.cpp` | 仅当 apps/daemon 不再 include `access/src/AccessGateway.h`，且不扩大到 cognition/llm/tools 实现时完成 |
| DMD-TODO-011 | Done | 补齐 DaemonProtocolAdapter frame decode/encode | daemon 详设 6.4.1、6.4.2、6.6.2；当前 adapter | 6.4.2 `DaemonProtocolAdapter::decode/encode` | L3 | 更新 `access/src/daemon/DaemonProtocolAdapter.cpp` 与 `access/include/daemon/DaemonProtocolAdapter.h`，优先委托 `DaemonFrameCodec`，不得继续在 adapter 内复制字符串扫描规则 | `decode()`、`encode()`、`parse_uds_request_frame()`、`build_uds_response_frame()` | unit：`DaemonProtocolAdapterTest` 扩展 request_id/trace_id/session_hint/idempotency_key/command/args/payload/malformed frame | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonProtocolAdapterTest" --output-on-failure` | DMD-TODO-001、030 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-011-DaemonProtocolAdapter-frame收敛.md`；更新 adapter | 仅当 malformed frame 不进入 Runtime，且 response frame 能稳定承载 disposition、receipt_ref、agent_result/error_ref，并复用安全 frame codec 时完成；2026-04-28 已完成 parse/build 收敛并通过 `DaemonProtocolAdapterTest` + `DaemonProtocolAdapterLocalTrustedTest` |
| DMD-TODO-012 | Done | 校验 peer identity 缺失 fail-closed 路径 | daemon 详设 6.4.3、6.7；当前 `IIPC::describe_peer` 与 adapter | 6.7 本地主体识别 | L3 | 更新 `access/src/daemon/DaemonProtocolAdapter.cpp`、`access/src/SubjectResolver.cpp`、`access/src/AccessPolicyGate.cpp` 的 daemon 本地事实消费 | `describe_local_peer_uid_fact()`、`SubjectResolver::resolve()`、privileged command deny path | unit：`DaemonProtocolAdapterLocalTrustedTest`、`SubjectResolverLocalTrustedTest`、新增 `DaemonPeerIdentityFailClosedTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonProtocolAdapterLocalTrustedTest|SubjectResolverLocalTrustedTest|DaemonPeerIdentityFailClosedTest|UnixIpcProviderPeerIdentityTest" --output-on-failure` | DMD-TODO-001、011 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-012-peer-identity-fail-closed收敛.md` | 仅当 local trusted、remote/untrusted、describe_peer failure 三类路径都有明确 Allow/Deny 结果，且 privileged command 缺 peer identity 必拒绝时完成；2026-04-28 已新增 daemon fail-closed gate 与 `DaemonPeerIdentityFailClosedTest`，并通过 4 项定向测试 |
| DMD-TODO-013 | Done | 接线 daemon AccessGateway 完整 submit pipeline | daemon 详设 6.6.2；当前 `AccessGateway` 默认空 pipeline | 6.6.2 Admission -> Normalize -> RuntimeBridge -> Publish | L2 | 更新 `apps/daemon/src/DaemonBootstrap.cpp` 或新增 `apps/daemon/src/DaemonAccessPipelineFactory.cpp`，组合 `RequestValidator`、`SubjectResolver`、`AuthenticatorChain`、`AccessPolicyGate`、`AdmissionController`、`RequestNormalizer`、`RuntimeBridge`、`ResultPublisher` | `build_daemon_submit_pipeline()`、`IAccessGateway::submit()` | unit：`DaemonAccessPipelineFactoryTest`；access 单测回归 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_daemon dasall_unit_tests && ctest --test-dir build-ci -R "DaemonAccessPipelineFactoryTest|AccessGatewayFacadeTest|AdmissionControllerTest|RequestNormalizerTest|ResultPublisherTest" --output-on-failure` | DMD-TODO-010、011、012 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-013-daemon-AccessGateway-pipeline收敛.md`；pipeline factory | 仅当 daemon 请求不再走空 submit pipeline，且未知命令、auth deny、payload too large 不进入 Runtime 时完成 |
| DMD-TODO-014 | Done | 接线 DaemonRuntimeBridge 到 runtime AgentFacade unary | daemon 详设 6.3、6.6.2；runtime `AgentFacade` | 6.6.2 RuntimeBridge 调用 Runtime | L3 | 更新 `access/src/RuntimeBridge.cpp` 或 daemon pipeline factory 中的 DispatchBackend 注入；不直接调用 runtime 内部 orchestrator | `RuntimeBridge::dispatch()`、`AgentFacade::handle()` adapter lambda | unit/integration：`RuntimeBridgeTest`、`RuntimeBridgeRejectMappingTest`、新增 `DaemonUnaryRuntimeBridgeTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeBridgeTest|RuntimeBridgeRejectMappingTest|DaemonUnaryRuntimeBridgeTest" --output-on-failure` | DMD-TODO-013 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-014-DaemonRuntimeBridge-unary收敛.md` | 仅当 daemon unary happy path 通过 `AgentFacade::handle()`，且 Runtime 未初始化时返回 bridge unavailable / fail-closed 时完成；2026-04-28 已在 daemon 组合根接入 `AgentFacade::handle()` adapter lambda，并新增 `DaemonUnaryRuntimeBridgeTest`，定向回归 `RuntimeBridgeTest|RuntimeBridgeRejectMappingTest|DaemonUnaryRuntimeBridgeTest` 通过 |
| DMD-TODO-015 | Done | 接线 accepted_async 到 AsyncTaskRegistry | daemon 详设 6.8；当前 `AsyncTaskRegistry` | 6.8.1 accepted_async receipt | L3 | 更新 daemon pipeline / response path，复用 `access/src/AsyncTaskRegistry.*`，不新增 `ReceiptStore` 平行实现 | `AsyncTaskRegistry::register_async_accept()`、daemon accepted response builder | unit：`AsyncTaskRegistryTest`、`AsyncTaskRegistryOwnershipTest`、新增 `DaemonAcceptedAsyncReceiptTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AsyncTaskRegistry(Test|OwnershipTest|ExpiryTest)|DaemonAcceptedAsyncReceiptTest" --output-on-failure` | DMD-TODO-013、014 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-015-daemon-accepted-async-receipt收敛.md` | 仅当 accepted_async 响应带 receipt_ref、ownership_token/owner 事实和 TTL，且未创建第二任务系统时完成；2026-04-29 已完成 receipt 接线与回执字段投影，新增 `DaemonAcceptedAsyncReceiptTest` 并通过定向验证。 |
| DMD-TODO-016 | Done | 实现 daemon status 命令 routing | daemon 详设 6.8；当前 `AsyncTaskRegistry` / gateway `TaskQueryHandler` | 6.8 status 查询 | L2 | 新增 `access/src/daemon/DaemonTaskQueryHandler.{h,cpp}` 并在 daemon access submit pipeline 接入 status taxonomy 路由；复用 `AsyncTaskRegistry` registry-only 查询 | `handle_status(receipt_ref, owner)`、`AsyncTaskRegistry::query_receipt()` | unit：`DaemonTaskQueryHandlerTest` 验证 found/not_found/expired/owner mismatch；回归 `AccessTaskQueryHandlerTest` | `Build_CMakeTools + RunCtest_CMakeTools(tests=["DaemonTaskQueryHandlerTest","AccessTaskQueryHandlerTest"])` | DMD-TODO-001、015 | 无 | 2026-04-29 已按 registry-only scope 完成 status 路由，不引入 runtime live status/query 依赖 | `docs/todos/daemon/deliverables/DMD-TODO-016-daemon-status-routing收敛.md` | 仅当 status 命令对 active/expired/missing/mismatch 都有稳定响应，且不会暴露其他 owner 信息时完成；本轮已通过 |
| DMD-TODO-017 | Done | 实现 daemon cancel 命令 routing | daemon 详设 6.8；`IAccessRuntimeBridge::cancel`；当前 `AsyncTaskRegistry` | 6.8 cancel owner 校验 | L3 | 更新 `DaemonTaskQueryHandler` 与 daemon pipeline：owner 验证通过后调用 `RuntimeBridge::cancel()`，成功后标记 `AsyncTaskRegistry::mark_completed("cancelled")` | `handle_cancel(receipt_ref, owner)`、`RuntimeBridge::cancel()`、`AsyncTaskRegistry::mark_completed()` | unit：`DaemonCancelCommandTest`、`RuntimeBridgeRejectMappingTest`；回归 `AccessTaskQueryHandlerTest`，并验证 owner mismatch 不调用 cancel backend | `Build_CMakeTools + RunCtest_CMakeTools(tests=["DaemonCancelCommandTest","RuntimeBridgeRejectMappingTest","AccessTaskQueryHandlerTest"])` | DMD-TODO-015、016 | 无 | 2026-04-29 已完成 owner-matched cancel forward 与 fail-closed 拒绝路径，不绕过 PolicyGate | `docs/todos/daemon/deliverables/DMD-TODO-017-daemon-cancel-routing收敛.md` | 仅当 owner 匹配才转发 cancel，owner mismatch/expired/not found 全部 fail-closed，且 cancel 不绕过 PolicyGate 时完成；本轮已通过 |

### 6.3 健康、诊断、观测与关闭任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMD-TODO-018 | Done | 定义 DaemonHealthTypes 与 DaemonHealthService | daemon 详设 6.4.1、6.9；infra health 设计 | 6.9 ping/readiness/health | L2 | 新增 `access/include/daemon/DaemonHealthTypes.h`、`access/src/daemon/DaemonHealthService.{h,cpp}`，更新 `access/CMakeLists.txt` | `DaemonReadinessSnapshot`、`DaemonPingSummary`、`DaemonHealthService::snapshot()` | unit：`DaemonHealthServiceTest` 验证 lifecycle/listener/gateway/bridge/diag/degraded_reasons 聚合 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonHealthServiceTest" --output-on-failure` | DMD-TODO-005 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-018-DaemonHealthService收敛.md`；新增 health 类型/服务 | 仅当 ping 与 readiness 输出语义分离，且不暴露敏感内部字段时完成；2026-04-29 已新增 `DaemonHealthTypes`/`DaemonHealthService` 并通过 `DaemonHealthServiceTest`。 |
| DMD-TODO-019 | Done | 实现 ping 与 readiness 命令响应 | daemon 详设 6.9.1；当前 ping 直接返回静态 JSON | 6.9.1 ping/readiness | L3 | 更新 daemon command handler，使 ping/readiness 使用 `DaemonHealthService`；删除或降级 `DaemonBootstrap` 内的静态 ping 特判，统一走 frame decode、peer fact、command router 与响应编码 | `handle_ping()`、`handle_readiness()`、`DaemonHealthService::snapshot()` | unit/integration：`DaemonPingCommandTest`、`DaemonReadinessCommandTest`；补 `DaemonPingDoesNotBypassRouterTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "DaemonPingCommandTest|DaemonReadinessCommandTest|DaemonPingDoesNotBypassRouterTest" --output-on-failure` | DMD-TODO-018、023、030 | DMD-BLK-004 | IIPC loopback/daemon fixture 可传递 request 与 response | `docs/todos/daemon/deliverables/DMD-TODO-019-daemon-ping-readiness收敛.md` | 仅当 ping 返回 version/schema/profile/readiness 摘要，readiness 在 bridge unavailable 时返回 NOT_READY/DEGRADED，且 ping 不再绕过统一 daemon command router 时完成；2026-04-29 已移除 `DaemonBootstrap` 静态 ping 特判，并通过 `DaemonPingCommandTest`、`DaemonReadinessCommandTest`、`DaemonPingDoesNotBypassRouterTest`。 |
| DMD-TODO-020 | Done | 实现 daemon 只读 diagnostics command gate | daemon 详设 6.9、6.10、6.11；infra diagnostics 详设 | 6.9 diagnostics；6.10 diag.enabled | L2 | 新增 `access/src/daemon/DaemonDiagnosticsHandler.{h,cpp}`，接入 `AccessPolicyGate` 与 infra diagnostics facade | `handle_diag(command)`、`is_read_only_diag_command()`、`diag_enabled` gate | unit：`DaemonDiagnosticsHandlerTest`、`AccessPolicyOverrideGateTest`；integration：`DaemonDiagDenyIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "DaemonDiagnosticsHandlerTest|DaemonDiagDenyIntegrationTest|DiagnosticsCommandPolicyTest" --output-on-failure` | DMD-TODO-001、003、013 | DMD-BLK-006 | infra diagnostics facade 与 daemon 只读命令白名单冻结 | `docs/todos/daemon/deliverables/DMD-TODO-020-daemon-diagnostics-gate收敛.md` | 仅当 diag 默认关闭、未授权拒绝、只读白名单外拒绝、远程导出/写操作不可达时完成；2026-04-29 已冻结 `IDiagnosticsService` seam 与 infra 只读白名单，并通过 `DaemonDiagnosticsHandlerTest`、`DiagnosticsCommandPolicyTest`、`DaemonDiagDenyIntegrationTest`。 |
| DMD-TODO-021 | Done | 接线 daemon 结构化日志、指标与审计字段 | daemon 详设 6.11；SSOT Access sidecar；infra logging/audit/metrics | 6.11 可观测性与审计 | L2 | 更新 `access/src/AccessObservabilityBridge.cpp`、`ResultPublisher.cpp`、daemon command handlers | `emit_daemon_request_fact()`、`emit_receipt_event()`、`emit_peer_identity_denied()`、`emit_shutdown_abandoned()` | unit：`DaemonObservabilityFieldSetTest`；现有 `AccessObservabilityFieldSetTest` 回归 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonObservabilityFieldSetTest|AccessObservabilityFieldSetTest|AccessObservabilityBridgeTest" --output-on-failure` | DMD-TODO-012、013、015 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-021-daemon-observability-audit收敛.md` | 仅当 request_id/session_id/trace_id/daemon_state/connection_ref/receipt_ref 在对应路径可断言，且认证秘密不进入日志时完成；2026-04-29 已补齐 daemon request/receipt/peer deny/shutdown abandoned 事件字段，并通过 `DaemonObservabilityFieldSetTest`、`AccessObservabilityFieldSetTest`、`AccessObservabilityBridgeTest`。 |
| DMD-TODO-022 | Done | 实现 graceful shutdown 排空与 abandoned 审计 | daemon 详设 6.5、6.12；`IAccessGateway::shutdown` | 6.12 异常与恢复时序 | L3 | 更新 `DaemonLifecycleController`、`DaemonListenerHost`、`DaemonBootstrap` 与 access publish 生命周期 | `shutdown(timeout)`、`reject_new_request_when_draining()`、`wait_inflight_or_timeout()` | unit/integration：`DaemonGracefulShutdownTest`、`AccessGatewayLifecycleTest`、`DaemonShutdownAbandonedAuditTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "DaemonGracefulShutdownTest|AccessGatewayLifecycleTest|DaemonShutdownAbandonedAuditTest" --output-on-failure` | DMD-TODO-005、006、013、021 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-022-daemon-graceful-shutdown收敛.md` | 仅当 Draining 拒绝新请求、inflight 在窗口内排空、超时 abandoned 有审计事实，且 daemon 不做业务恢复裁定时完成；2026-04-30 已通过 `DaemonGracefulShutdownTest`、`AccessGatewayLifecycleTest`、`DaemonShutdownAbandonedAuditTest`。 |

### 6.4 测试拓扑、集成门禁与交付证据任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMD-TODO-023 | Done | 注册 tests/unit/apps/daemon 拓扑 | daemon 详设 6.13、9；当前 tests 现状 | 9 测试与质量门 | L2 | 新增 `tests/unit/apps/daemon/CMakeLists.txt` 与顶层 `tests/unit/CMakeLists.txt` 接线 | daemon unit test discoverability | `ctest -N` 可发现 `DaemonBootstrapTest`、`DaemonLifecycleControllerTest`、`DaemonListenerHostTest`、`DaemonConfigValidatorTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_daemon_bootstrap_unit_test dasall_daemon_lifecycle_controller_unit_test dasall_daemon_listener_host_unit_test dasall_daemon_config_validator_unit_test && ctest --test-dir build-ci -N | rg "Daemon(Bootstrap|LifecycleController|ListenerHost|ConfigValidator)Test"` | DMD-TODO-002、004、005、006 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-023-tests_unit_apps_daemon拓扑收敛.md`；新增 CMake 拓扑 | 仅当 apps/daemon 单测被统一聚合目标发现，且无本地手工命令依赖时完成；2026-05-01 已确认 `tests/unit/apps/daemon/CMakeLists.txt` 与顶层 `tests/unit/CMakeLists.txt` 完成接线，focused build + `ctest -N` 可稳定发现 4 个锚点测试；原 `dasall_unit_tests` 聚合目标会执行全仓 unit tests 并受仓库既有 runtime failure 污染，不再作为该拓扑任务的主验收信号。 |
| DMD-TODO-024 | Done | 验证真实 daemon ping 集成 | daemon 详设 8.1、9；旧 legacy send-only ping smoke | Gate-DMD-01 ping | L2 | 新增 `tests/integration/access/DaemonPingIntegrationTest.cpp`，并在必要处收紧 loopback provider 的 accept/connect/receive 语义 | daemon process/in-process fixture、response receive path | integration：启动 in-process daemon fixture，发送 ping，读取 completed response 与 readiness 摘要 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_daemon dasall_access_daemon_ping_integration_test && ctest --test-dir build-ci -R "DaemonPingIntegrationTest" --output-on-failure` | DMD-TODO-006、009、019、023、029、030 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-024-daemon-ping-integration收敛.md`；集成测试 | 仅当测试不只是 client send 成功，而是能验证 daemon 处理并返回 pong/readiness 时完成；CLI 真实 ping 验收归 DMD-TODO-031；2026-05-01 已新增 `DaemonPingIntegrationTest`、替换旧 legacy smoke 注册，并通过 `DaemonPingIntegrationTest` 与 loopback/provider focused 回归；仓库聚合目标 `dasall_integration_tests` 会执行全仓 integration tests 并受既有 runtime/infra failure 污染，不再作为该任务主验收信号。 |
| DMD-TODO-025 | Done | 验证 daemon unary 主链与拒绝路径 | daemon 详设 6.6.2、9；Access core 当前实现 | Gate-DMD-03 request flow | L2 | 新增 `tests/integration/access/DaemonUnaryIntegrationTest.cpp`、`DaemonRejectPathIntegrationTest.cpp`、`DaemonBinaryUnarySmokeTest.cpp` 与 `DaemonIntegrationHarness.h` | daemon request -> AccessGateway -> RuntimeBridge -> ResultPublisher；built daemon/cli unary smoke | integration：happy path、real daemon binary unary smoke、unknown command、auth deny、validation reject、runtime bridge unavailable | `Build_CMakeTools(buildTargets=["dasall_daemon","dasall_cli","dasall_access_daemon_unary_integration_test","dasall_access_daemon_reject_path_integration_test","dasall_access_daemon_binary_unary_smoke_integration_test"])`；`RunCtest_CMakeTools(tests=["DaemonUnaryIntegrationTest","DaemonRejectPathIntegrationTest","DaemonBinaryUnarySmokeTest"])` | DMD-TODO-013、014、021、024 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-025-daemon-unary-reject集成收敛.md` | 仅当 happy path 和三类拒绝路径都经共享 access core，且 built `dasall_daemon` + built `dasall_cli run` 能在真实 `main.cpp` 路径上返回 completed 时完成；2026-05-03 已补齐 `DaemonBinaryUnarySmokeTest`，并通过 `DaemonUnaryIntegrationTest`、`DaemonRejectPathIntegrationTest`、`DaemonBinaryUnarySmokeTest` 覆盖 helper-level 与 real-binary 两层 unary 主链证据；`RunCtest_CMakeTools` stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库当前基线计为有效证据。 |
| DMD-TODO-026 | Done | 验证 daemon async/status/cancel 闭环 | daemon 详设 6.8、9 | Gate-DMD-04 async/status/cancel | L2 | 新增 `tests/integration/access/DaemonReceiptFlowIntegrationTest.cpp`，并在 `AccessGatewayFactory` 注入 `AsyncTaskRegistry` seam + status/cancel wire 投影 | accepted_async、status、cancel、owner mismatch、TTL expired | integration：accepted_async 返回 receipt；owner 查询 pending/completed/cancelled；非 owner cancel 拒绝 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_access_daemon_receipt_flow_integration_test dasall_access_daemon_task_query_handler_unit_test dasall_access_daemon_cancel_command_unit_test && ctest --test-dir build-ci -R "DaemonReceiptFlowIntegrationTest|DaemonTaskQueryHandlerTest|DaemonCancelCommandTest" --output-on-failure` | DMD-TODO-015、016、017、024 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-026-daemon-async-status-cancel收敛.md` | 仅当 receipt 不可被跨主体滥用，cancel 不绕过 owner/PolicyGate，TTL 过期语义可见时完成；2026-05-02 已通过 `DaemonReceiptFlowIntegrationTest` 覆盖 accepted_async、owner status active/completed/cancelled、status owner mismatch、cancel owner mismatch、TTL expired，并回归 `DaemonTaskQueryHandlerTest`、`DaemonCancelCommandTest`；`RunCtest_CMakeTools` stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库当前基线计为有效证据。 |
| DMD-TODO-027 | Done | 验证 daemon failure、shutdown 与 profile 兼容门 | daemon 详设 6.10、6.12、9、11；SSOT `InfraIntegrationTopology` | Gate-DMD-02/05/06 | L2 | 新增 `tests/integration/access/DaemonFailureInjectionIntegrationTest.cpp`、`DaemonProfileCompatibilityTest.cpp` | bind conflict、peer identity unsupported、runtime timeout、shutdown draining、profile diag disabled | integration：failure injection、graceful shutdown、profile compatibility | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_access_daemon_failure_injection_integration_test dasall_access_daemon_profile_compatibility_integration_test dasall_daemon_graceful_shutdown_unit_test && ctest --test-dir build-ci -R "Daemon(FailureInjection|GracefulShutdown|ProfileCompatibility)Test" --output-on-failure` | DMD-TODO-003、012、020、022、024 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-027-daemon-failure-profile-gate收敛.md` | 仅当失败路径都有明确错误/审计/health 事实，且 profile 不通过代码分叉改变主流程时完成；2026-05-02 已通过 `DaemonFailureInjectionTest` 覆盖 bind conflict、peer identity unsupported、runtime timeout 注入，通过 `DaemonGracefulShutdownTest` 覆盖 draining stop，通过 `DaemonProfileCompatibilityTest` 覆盖五档 baseline profile 下 unary 主链一致且 diag 默认关闭；`RunCtest_CMakeTools` stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库当前基线计为有效证据。 |
| DMD-TODO-028 | Done | 回写 daemon 专项 Gate 与交付证据 | daemon 详设 7、8、9、11；工程协作规范 | Gate-DMD-* | L2 | 更新 `docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md`、`docs/todos/daemon/deliverables/`、`docs/worklog/DASALL_开发执行记录.md` | gate 状态、阻塞变化、命令证据、风险残留 | process：所有 gate 命令、`ctest -N` discoverability、残余 blocker 状态回写齐备 | `rg -n "DMD-TODO-028|Gate-DMD-0[1-9]|DMD-BLK-00[1-8]|当前残余风险" docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md docs/todos/daemon/deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md docs/worklog/DASALL_开发执行记录.md && rg -n "DMD-TODO-02(4|5|6|7|8)|DMD-TODO-03(1|4|5)" docs/worklog/DASALL_开发执行记录.md docs/todos/daemon/deliverables` | DMD-TODO-024、025、026、027、031、034、035 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md`；更新后的专项 TODO 与 worklog | 仅当每个 Gate 都有可追溯命令证据、`DMD-BLK-001` ~ `DMD-BLK-008` 状态更新完毕、当前残余风险与回退路径均回写时完成；2026-05-02 已完成 9.4 Gate 执行证据、9.5 阻塞变化与回退记录、残余风险快照及 worklog #522。 |

### 6.5 评审补强与回归整改任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| DMD-TODO-029 | Done | 补齐 IIPC 双向 request/response loopback 解阻门 | 评审发现 DMD-BLK-004；daemon 详设 6.6.2、9；当前 `UnixIpcProvider` | 6.6.2 accept/receive/send 主流程 | L2 | 更新 `platform/src/linux/UnixIpcProvider.cpp` 的测试可用双向通道，或新增 `tests/mocks/include/LoopbackIpcProvider.h` 并接入 daemon integration fixture；不得修改 `IIPC` 公共语义为 daemon 特例 | client channel/server channel 绑定关系、per-channel inbound queue、peer identity snapshot、close propagation | unit：`UnixIpcProviderLoopbackTest` 验证 client->server、server->client、peer closed、payload too large；fixture：`DaemonLoopbackFixtureTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "UnixIpcProviderLoopbackTest|DaemonLoopbackFixtureTest|UnixIpcProviderPeerIdentityTest" --output-on-failure` | 无 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-029-IIPC-loopback解阻收敛.md`；platform 测试或 daemon fixture | 仅当集成测试能证明请求 payload 被 daemon listener 消费且响应 payload 被 CLI/client 读取时完成；完成后更新 `DMD-BLK-004` 状态 |
| DMD-TODO-030 | Done | 硬化 daemon frame codec、schema_version 与错误映射 | 评审发现当前 adapter 字符串扫描；daemon 详设 6.4.1、6.6.2、6.12 | 6.4.1 UDS frame；6.12 协议错误尽早拒绝 | L3 | 新增 `access/include/daemon/DaemonFrameCodec.h` 与 `access/src/daemon/DaemonFrameCodec.cpp`，更新 `access/CMakeLists.txt` public headers/source，由 `DaemonProtocolAdapter` 复用；若暂不引入第三方 JSON 库，必须把最小 JSON 规则集中到 codec 并显式覆盖 escaping/未知字段 | `decode_request_frame()`、`encode_response_frame()`、`DaemonFrameDecodeError`、`map_frame_error_to_publish_envelope()` | unit：`DaemonFrameCodecTest`、`DaemonFrameCodecMalformedTest` 验证 schema 缺失、版本不兼容、未知 command、escaped quotes、payload 过大、非 UTF-8/截断 payload | `Build_CMakeTools(buildTargets=["dasall_access_daemon_frame_codec_unit_test","dasall_access_daemon_frame_codec_malformed_unit_test","dasall_access_daemon_protocol_adapter_unit_test"])`；`RunCtest_CMakeTools(tests=["DaemonFrameCodecTest","DaemonFrameCodecMalformedTest","DaemonProtocolAdapterTest"])` | DMD-TODO-001 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-030-daemon-frame-codec安全硬化.md`；新增 codec 与测试 | 仅当安全敏感字段不再由散落的字符串扫描解析，malformed frame 不进入 Access/Runtime，响应编码能正确 escape 用户 payload 时完成；2026-04-28 已实现 codec 收口、adapter 复用与 3 条 focused unit tests 通过 |
| DMD-TODO-031 | Done | 收敛 CLI-daemon v1 命令与响应解析契约 | 评审发现当前 CLI 只验证 send 成功；CLI 详设；daemon 详设 6.9 | CLI -> daemon 本地控制面 | L2 | 更新 `apps/cli/src/CliIpcClient.*`、`CliCommandParser.*`、`CliOutputFormatter.*`，让 ping/run/status/cancel/readiness/diag 读取并解析 `UdsResponseFrame`；共享 `DaemonFrameCodec` 新增 request encode/response decode 以避免 CLI 手写 wire parser；diag 命令默认隐藏 | `DaemonClientResponse`、`ping_daemon()`、`submit()`、`query_status()`、`cancel()`、`read_readiness()` | unit：`CliIpcClientTest`、`CliIpcClientResponseTest`、`CliIpcClientUnavailableTest`、`CliDaemonCommandParserTest`、`CliDaemonOutputFormatterTest`；integration：CLI ping 读取 pong/readiness | `Build_CMakeTools(buildTargets=["dasall_cli","dasall_access_cli_ipc_client_unit_test","dasall_access_cli_ipc_client_response_unit_test","dasall_access_cli_ipc_client_unavailable_unit_test","dasall_access_cli_command_parser_unit_test","dasall_access_cli_output_formatter_unit_test","dasall_access_daemon_ping_integration_test"])`；`RunCtest_CMakeTools(tests=["CliIpcClientTest","CliIpcClientResponseTest","CliIpcClientUnavailableTest","CliDaemonCommandParserTest","CliDaemonOutputFormatterTest","DaemonPingIntegrationTest"])` | DMD-TODO-001、019、029、030 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-031-CLI-daemon-wire-contract收敛.md` | 仅当 CLI 能区分 accepted/rejected/not_ready/receipt，并且 ping 集成断言的是 pong/readiness 内容而非 send 成功时完成；2026-05-02 已补齐结构化 `DaemonClientResponse`、CLI 命令解析/输出格式化与 `DaemonPingIntegrationTest` 中的 CLI pong 断言，并通过 6 条 focused tests。 |
| DMD-TODO-032 | Done | 收敛 UDS endpoint 权限、stale socket 与 bind 安全策略 | daemon 详设 6.7.3、6.10.2、6.12；评审发现 socket 安全缺任务 | 6.7.3 socket 权限面；6.12 socket 地址冲突 | L2 | 更新 `DaemonConfigValidator`、`DaemonListenerHost` 与必要的 platform preflight helper；socket_path 必须来自受管配置，校验父目录、相对路径、路径穿越、mode/owner/group、stale socket 安全判定 | `DaemonSocketPolicy`、`validate_socket_path()`、`preflight_bind_endpoint()`、`try_cleanup_stale_socket()` | unit：`DaemonSocketPolicyTest`、`DaemonListenerHostBindConflictTest`；failure：活动 socket 不被删除、stale socket 只在 owner/mode 匹配时清理 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "DaemonSocketPolicyTest|DaemonListenerHostBindConflictTest|DaemonConfigValidatorTest" --output-on-failure` | DMD-TODO-002、004、006 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-032-UDS-endpoint安全策略收敛.md` | 仅当 daemon 不再依赖 hardcoded `/tmp` socket，且 bind conflict/stale cleanup/permission denied 都有稳定错误与审计事实时完成 |
| DMD-TODO-033 | Done | 实现 SIGHUP hot-reload allowlist 与配置快照切换 | daemon 详设 6.10.4；当前 `DaemonSignalHandler` 仅 reload intent | 6.10.4 热重载矩阵 | L2 | 新增 `DaemonConfigReloader` 或扩展 `DaemonBootstrap`，当前只允许 `daemon.diag_enabled` 受控热更；`log_level`/`log_format`/`watchdog_enabled`/`receipt_ttl_sec`/`override_enabled` 与其它 daemon 键统一拒绝并保持旧快照 | `reload_allowed_keys()`、`apply_reload_snapshot()`、`reject_reload_for_restart_only_keys()` | unit：`DaemonConfigReloadTest` 验证仅 `daemon.diag_enabled` 生效、`log_format`/`watchdog_enabled`/`receipt_ttl_sec`/`override_enabled`/`socket_path` 均拒绝、失败保留 last-known-good config；audit：reload denied 记录事实 | `Build_CMakeTools(buildTargets=["dasall_daemon_config_reload_unit_test","dasall_daemon_signal_handler_unit_test","dasall_access_daemon_observability_field_set_unit_test"])`；`RunCtest_CMakeTools(tests=["DaemonConfigReloadTest","DaemonSignalHandlerTest","DaemonObservabilityFieldSetTest"])` | DMD-TODO-003、004、007、021 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-033-daemon-hot-reload-allowlist收敛.md` | 仅当 SIGHUP 不重启 listener、不改 socket_path，且除 `daemon.diag_enabled` 外的 daemon 键都不会被误判为运行期可热更时完成；2026-05-03 已将 allowlist 收窄为 `daemon.diag_enabled` 单键，并通过 `DaemonConfigReloadTest`、`DaemonSignalHandlerTest`、`DaemonObservabilityFieldSetTest` 完成 focused 复验。 |
| DMD-TODO-034 | Done | 验证 daemon 并发背压与长期运行 soak gate | daemon 详设 6.5.2、6.11、6.12；评审发现长期运行门缺失 | 6.5.2 worker/backpressure；6.12 draining | L2 | 新增 deterministic soak/integration fixture；覆盖 dispatch_workers、Admission 并发上限、payload 上限、receipt TTL 清理、draining、publish failure、资源释放 | `DaemonLoadScenario`、`DaemonInProcessFixture`、`active_connection_count()`、`receipt_active_count()` | integration：`DaemonBackpressureIntegrationTest`、`DaemonSoakIntegrationTest`、`DaemonReceiptTtlCleanupIntegrationTest`；要求无 unbounded growth 断言 | `Build_CMakeTools(buildTargets=["dasall_access_daemon_backpressure_integration_test","dasall_access_daemon_soak_integration_test","dasall_access_daemon_receipt_ttl_cleanup_integration_test"])`；直接执行 `build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_backpressure_integration_test`、`dasall_access_daemon_soak_integration_test`、`dasall_access_daemon_receipt_ttl_cleanup_integration_test` | DMD-TODO-013、015、022、024、029 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-034-daemon-concurrency-soak-gate.md` | 仅当高并发拒绝走 Admission、draining 不接新请求、TTL 清理不会持锁执行 publish/审计、资源计数回落到基线时完成；2026-05-02 已新增 `DaemonBackpressureIntegrationTest`、`DaemonSoakIntegrationTest`、`DaemonReceiptTtlCleanupIntegrationTest` 及配套 fixture/load helper，并通过 `Build_CMakeTools` 构建三个 focused targets；由于当前环境下 `RunCtest_CMakeTools` 会卡住，最终改为直接执行三条已构建测试二进制验证，`backpressure`、`soak`、`ttl cleanup` 均返回退出码 0。 |
| DMD-TODO-035 | Done | 收敛 daemon 部署与 supervisor 交付契约 | daemon 详设 6.9.3、10、11；systemd/dockerd 实践 | 6.9.3 supervisor；10 演进评估 | L2 | 收敛 `docs/deploy/daemon/` 部署说明，并把 `UnixIpcProvider` 修到真实 OS 级 UDS，使 direct-bind v1 service 示例、配置样例、socket 权限说明、validate-only、ping/readiness、graceful stop 可按文档复现；不交付 socket activation fd import | `dasall-daemon.service` 示例、`daemon.example.yaml/json`、运维验收清单 | docs/process：`DaemonDeploymentContractReview`；smoke：validate-only、daemon unavailable、start、ping/readiness、graceful stop 可复现 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_daemon dasall_daemon_config_validator_unit_test dasall_daemon_graceful_shutdown_unit_test dasall_access_daemon_ping_integration_test && ctest --test-dir build-ci -R "DaemonConfigValidatorTest|DaemonGracefulShutdownTest|DaemonPingIntegrationTest" --output-on-failure` | DMD-TODO-004、008、019、022、032 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-035-daemon部署与supervisor交付契约.md`；部署样例与验收清单 | 仅当本地运维者能按文档完成 validate、start、ping/readiness、stop，并明确 socket activation 为 v2 非交付项时完成；2026-05-02 已补齐 `docs/deploy/daemon/` 交付目录、将 `UnixIpcProvider` 收敛为真实 `AF_UNIX`/`SOCK_SEQPACKET` transport，并通过 `DaemonConfigValidatorTest`、`DaemonGracefulShutdownTest`、`DaemonPingIntegrationTest` 与本地 smoke。 |
| DMD-TODO-036 | Done | 收敛 daemon/CLI 默认 endpoint 与 CLI socket-path 覆盖 | 2026-05-02 评审发现 daemon 默认 path、CLI 默认 path 与 daemon socket policy 三者不一致；daemon 详设 6.7.3、6.10；DMD-TODO-031/035 follow-up | 6.7.3 socket_path；6.10 配置来源与 CLI 入口契约 | L2 | 抽取 daemon/CLI 共用 endpoint 默认值来源，更新 `apps/cli/src/main.cpp`、`apps/cli/src/CliCommandParser.*` 与必要的 shared endpoint defaults header；若保留 CLI 显式覆盖，必须支持 `--socket-path` 或等价受控参数，并同步 `docs/deploy/daemon/README.md` / `ACCEPTANCE_CHECKLIST.md` | `kDefaultDaemonSocketPath`、CLI endpoint override parsing、`CliCommandParser::parse()`、`main()` endpoint resolution | unit：扩展 `CliDaemonCommandParserTest`；integration：新增 `CliDaemonSocketPathIntegrationTest` 或等价 real smoke，验证 CLI 在默认值与显式 socket-path 两条路径下都能正向连到 daemon | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_cli dasall_daemon dasall_access_cli_command_parser_unit_test dasall_access_cli_ipc_client_unit_test dasall_access_cli_ipc_client_response_unit_test dasall_access_cli_ipc_client_unavailable_unit_test dasall_access_daemon_ping_integration_test dasall_access_cli_daemon_socket_path_integration_test && ctest --test-dir build-ci -R "CliDaemonCommandParserTest|CliIpcClientTest|CliIpcClientResponseTest|CliIpcClientUnavailableTest|CliDaemonSocketPathIntegrationTest|DaemonPingIntegrationTest" --output-on-failure` | DMD-TODO-031、035 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-036-daemon-cli-endpoint统一与覆盖收敛.md`；共享 endpoint defaults、CLI 参数与部署文档更新 | 仅当 CLI 与 daemon 默认 socket_path 来自同一处定义，CLI 可通过默认值和显式 socket-path 两条路径正向连通 daemon，且部署文档不再依赖“CLI 固定连接旧路径”的补充说明时完成；2026-05-02 已新增 `DaemonEndpointDefaults.h`、CLI `--socket-path` 解析和 `CliDaemonSocketPathIntegrationTest`，并通过 6 项 focused CLI/daemon tests。 |
| DMD-TODO-037 | Done | 收敛真实 UDS socket mode 与 stale restart 安全回归 | 2026-05-02 评审发现真实 daemon 创建的 socket mode 与 cleanup policy 不一致；daemon 详设 6.7.3、6.12；DMD-TODO-032/035 follow-up | 6.7.3 socket 文件权限；6.12 socket 地址冲突与 stale cleanup | L2 | 更新 `platform/src/linux/UnixIpcProvider.cpp`、`apps/daemon/src/DaemonSocketPolicy.*` 与必要的 `DaemonListenerHost` bind/close 路径，让真实 bind 后的 socket mode 收敛到 policy 要求，并补齐 daemon 创建 socket 的 stale cleanup / restart-safe 行为 | `UnixIpcProvider::listen()`、socket mode apply helper、`try_cleanup_stale_socket()`、restart-safe bind preflight | unit：扩展 `DaemonSocketPolicyTest`、`UnixIpcProviderTest`；integration：新增 `DaemonSocketModeIntegrationTest`、`DaemonStaleSocketRecoveryIntegrationTest` 或等价 restart smoke | `cmake --build build-ci --target dasall_daemon dasall_daemon_socket_policy_unit_test dasall_unix_ipc_provider_unit_test dasall_access_daemon_ping_integration_test dasall_access_daemon_socket_mode_integration_test dasall_access_daemon_stale_socket_recovery_integration_test && ctest --test-dir build-ci -R "DaemonSocketPolicyTest|UnixIpcProviderTest|DaemonSocketModeIntegrationTest|DaemonStaleSocketRecoveryIntegrationTest|DaemonPingIntegrationTest" --output-on-failure` | DMD-TODO-032、035 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-037-UDS权限与stale-restart收敛.md`；socket mode / restart 回归代码与测试 | 仅当 daemon 实际创建的 socket mode 与 cleanup policy 对齐、daemon 异常退出后的 stale socket 可被安全回收、并且 restart smoke 能稳定通过时完成；2026-05-02 已在 `UnixIpcProvider::listen()` 收敛真实 socket mode=0600，并新增 `DaemonSocketModeIntegrationTest` / `DaemonStaleSocketRecoveryIntegrationTest`，focused gate 5/5 通过。 |
| DMD-TODO-038 | Done | 将 profile/config snapshot 真正接入 daemon 入口组合根 | 2026-05-02 评审发现 profile projection 仍主要停留在 helper / harness，main 仍硬编码 profile_id 与健康元数据；daemon 详设 6.6.1、6.10.1、6.10.3；DMD-TODO-003/004/009 follow-up | 6.6.1 启动主流程；6.10 ConfigCenter 四层模型 | L2 | 新增 `apps/daemon/src/DaemonEntryConfigLoader.{h,cpp}` 或等价 loader，更新 `apps/daemon/src/main.cpp` 与 `apps/daemon/CMakeLists.txt`，将 `profiles/include/DaemonProfileProjection.h` 与 deployment snapshot 接到真实入口；移除 `main.cpp` 中写死的 `daemon_profile_id` / `effective_profile_id` / 健康布尔常量 | `DaemonEntryConfigLoader::load()`、`resolve_bootstrap_config_from_profile()`、entry bootstrap wiring | unit：新增 `DaemonEntryConfigProjectionTest`；integration：扩展 `DaemonProfileCompatibilityTest` 或新增 entry-level profile smoke，验证真实入口能消费 profile 产物 | `cmake --build build-ci --target dasall_daemon dasall_daemon_entry_config_projection_unit_test dasall_daemon_profile_projection_unit_test dasall_access_daemon_profile_compatibility_integration_test && ctest --test-dir build-ci -R "DaemonEntryConfigProjectionTest|DaemonProfileProjectionTest|DaemonProfileCompatibilityTest|DaemonBootstrapTest" --output-on-failure` | DMD-TODO-003、004、009、035 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-038-daemon入口profile-config接线收敛.md`；入口 loader、focused tests 与文档回写 | 仅当 daemon 二进制入口不再依赖 `main.cpp` 中写死的 profile_id / readiness 常量，且 profile / deployment snapshot 能通过真实 main/bootstrap 路径进入 process context 时完成；2026-05-02 已新增 `DaemonEntryConfigLoader`、`DaemonEntryConfigProjectionTest`，并将 `DaemonProfileCompatibilityTest` 改为通过 entry loader 装配 baseline profiles，focused gate 4/4 通过。 |
| DMD-TODO-039 | Done | 收敛 hot-reload 真实快照源与正向复验门 | 2026-05-02 评审发现 SIGHUP 目前主要重复 apply 初始 `parsed.config`，缺少 fresh snapshot source；daemon 详设 6.10.4、6.11；DMD-TODO-033 follow-up | 6.10.4 热重载矩阵；6.11 reload denied / applied 审计 | L2 | 在 `apps/daemon/src/DaemonConfigReloader.*` 与 `main.cpp` 上新增真实 reload candidate source，复用 `DMD-TODO-038` 的 loader / snapshot 解析路径；当前仅 `daemon.diag_enabled` 允许被进程实际观察到，`daemon.log_format`、`daemon.socket_path` 等非 allowlisted key 继续拒绝并审计 | `load_reload_candidate()`、`DaemonConfigReloader::apply_reload_snapshot()`、reload audit projection | unit：扩展 `DaemonConfigReloadTest`；integration：新增 `DaemonHotReloadIntegrationTest`，验证 `diag_enabled` 正向生效、`daemon.log_format`/`daemon.socket_path` 拒绝、审计字段稳定 | `Build_CMakeTools(buildTargets=["dasall_daemon","dasall_daemon_signal_handler_unit_test","dasall_daemon_config_reload_unit_test","dasall_access_daemon_hot_reload_integration_test","dasall_access_daemon_observability_field_set_unit_test"])`；`RunCtest_CMakeTools(tests=["DaemonConfigReloadTest","DaemonHotReloadIntegrationTest","DaemonSignalHandlerTest","DaemonObservabilityFieldSetTest"])` | DMD-TODO-033、038 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-039-daemon热重载真实快照收敛.md`；reload source、integration test 与审计回写 | 仅当 SIGHUP 会读取 fresh snapshot 而不是重复初始 config，且 `daemon.diag_enabled` 的运行效果可观测、`daemon.log_format`/`daemon.socket_path` 等非 allowlisted key 继续拒绝且有稳定审计事实时完成；2026-05-03 已让 `DaemonHotReloadIntegrationTest` 同时证明 `diag_enabled` 热更可观察、`daemon.log_format` 与 `daemon.socket_path` 都会被拒绝，focused gate 4/4 通过。 |
| DMD-TODO-040 | Done | 清理过期文档、陈旧测试并回写专项复验证据 | 2026-05-02 评审发现部署文档仍残留 readiness 消费缺口口径，且存在陈旧 legacy send-only ping smoke；DMD-TODO-028/035 follow-up | 7 Gate / 9 验收 / 11 结论回写 | L2 | 更新 `docs/deploy/daemon/README.md`、`docs/deploy/daemon/ACCEPTANCE_CHECKLIST.md`、`docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md`、相关 deliverables / worklog；删除、重写或正式注册 legacy send-only ping smoke，避免陈旧测试拓扑残留；补齐 `tests/unit/apps/daemon/CMakeLists.txt` 对共享 daemon public 头的 include 可见性 | `Gate-DMD-05/07/09/10` 复验证据、陈旧 smoke 清理、文档一致性检查 | process + focused validation：负向检查过期口径已清除；回归 `DaemonPingIntegrationTest`、`CliDaemonCommandParserTest`、`DaemonConfigReloadTest`、`DaemonProfileCompatibilityTest` | `rg -n "当前 CLI 尚未消费 readiness 响[应]|CliDaemonPingIntegrationTe(st)|专项状态从 fully-close[d]" docs/deploy/daemon docs/todos/daemon tests/integration/access`；`RunCtest_CMakeTools(tests=["DaemonPingIntegrationTest","CliDaemonCommandParserTest","DaemonConfigReloadTest","DaemonProfileCompatibilityTest"])` | DMD-TODO-036、037、038、039 | 无 | 无 | `docs/todos/daemon/deliverables/DMD-TODO-040-daemon文档与证据复验收敛.md`；清理后的文档 / 测试 / worklog | 仅当部署文档、专项 TODO、deliverables 与测试拓扑不再残留过期口径，历史 send-only smoke 已被删除或改写为有效 gate，且 Gate-DMD-05 / 07 / 09 / 10 的复验证据已回写时完成；2026-05-02 已删除 legacy send-only ping smoke 文件、清理专项文档中的过期口径，补齐 `apps/daemon` unit-test 拓扑对共享 daemon public 头的 include 可见性，并完成 Gate-DMD-05 / 07 / 09 / 10 focused 复验回写。 |

## 7. 执行顺序建议

### 7.1 串并行编排（含 2026-05-02 回归整改）

说明：当前时点优先执行 `DMD-TODO-036` ~ `DMD-TODO-040`；A ~ I 为 `DMD-TODO-001` ~ `035` 的首轮收敛顺序，保留用于追溯。

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| R1 运行面一致性整改 | DMD-TODO-036、037 | 036/037 可并行；037 依赖 032/035 基线 | 先统一 daemon/CLI 默认 endpoint 与 CLI 覆盖，再修真实 socket mode 与 stale restart，避免后续 profile/reload 复验落在不稳定运行面上 |
| R2 入口配置与热更真接线 | DMD-TODO-038、039 | 038 先行；039 依赖 038 | 先把 main/bootstrap 接到真实 profile/config snapshot，再验证 SIGHUP 会读取新快照而不是重复旧值 |
| R3 文档、测试与证据复验 | DMD-TODO-040 | 串行收口 | 清理过期文档与陈旧 smoke，并重写 Gate-DMD-05/07/09/10 的权威证据 |
| A schema 与配置基线 | DMD-TODO-001、002、003、004 | 001/002 可并行；003 需评审；004 依赖 002 | 先固定 frame、command、config，避免后续 adapter 和 listener 返工 |
| B daemon 壳层拆分 | DMD-TODO-005、006、007、008、009、010 | 005 先行；006/007 可并行；008 阻塞但可先 no-op 设计；009 收口；010 评审 | 将当前 `DaemonBootstrap` 单体拆成 lifecycle/listener/signal/bootstrap |
| B2 IIPC 与 UDS 安全解阻 | DMD-TODO-029、032 | 029 应尽早执行以解 DMD-BLK-004；032 依赖配置和 listener | 先证明 request/response 可真实闭环，再收敛 socket_path 权限与 stale socket |
| C protocol 与安全 | DMD-TODO-030、011、012 | 030 先行；011 复用 codec；012 紧随 | 正式解析 frame，并验证 peer identity 缺失 fail-closed |
| D Access/Runtime 主链 | DMD-TODO-013、014 | 串行 | 先接完整 access pipeline，再接 Runtime unary bridge |
| E async/status/cancel | DMD-TODO-015、016、017 | 015 先行；016 阻塞于 status scope；017 依赖 016 | accepted_async 先可用，status/cancel 再接 owner 校验 |
| F 运维能力 | DMD-TODO-018、019、020、021、022、033、035 | 018/021 可并行；019 依赖 018/030；020 阻塞；022 依赖 lifecycle/listener；033/035 评审门 | health/readiness、diag、observability、draining、hot-reload、部署契约形成长期运行能力 |
| G 测试拓扑与 CLI | DMD-TODO-023、024、031 | 023 先行；024 依赖 029；031 依赖 019/029/030 | 先保证 daemon 单测 discoverable，再验证真实 ping 和 CLI 响应契约 |
| H 集成与质量门 | DMD-TODO-025、026、027、034 | 025 在 024 后；026/027 依赖各自 blocker；034 在主链和 shutdown 后 | 主链、async、failure、profile、并发/soak 分门别类验证 |
| I 证据回写 | DMD-TODO-028 | 串行收口 | 全部 gate 证据、阻塞变化和残余风险回写 |

### 7.2 必过门禁表

| Gate ID | 门禁名称 | 触发时机 | 通过条件 | 未通过后动作 |
|---|---|---|---|---|
| Gate-DMD-01 | Schema 与配置冻结门 | DMD-TODO-001~004 后 | frame 类型、command taxonomy、config 默认值、冲突校验全部有单测 | 停止 adapter/pipeline 任务，先修 schema/config |
| Gate-DMD-02 | 进程壳层拆分门 | DMD-TODO-005~010 后 | lifecycle/listener/signal/bootstrap 单测可发现，main 不再硬编码关键运行配置 | 回退到 apps/daemon 组件拆分 |
| Gate-DMD-03 | IPC/UDS 与 Peer identity 安全门 | DMD-TODO-029、030、011、012、032 后 | request/response loopback、frame codec、local trusted、untrusted、describe_peer failure、privileged command deny、socket path 安全全部通过 | 禁止推进 diag/override/status/cancel 和 daemon E2E |
| Gate-DMD-04 | Access/Runtime 主链门 | DMD-TODO-013~014 后 | daemon 请求经共享 access core，unary happy/reject 路径可验证 | 停止 async/status/cancel，先修 pipeline |
| Gate-DMD-05 | Health/Readiness/CLI 门 | DMD-TODO-018~019、031 后 | ping 与 readiness 分离，bridge unavailable 可表现 NOT_READY/DEGRADED，CLI 能读取稳定响应 | 修 health service、command handler 和 CLI response parser |
| Gate-DMD-06 | Async/Receipt 门 | DMD-TODO-015~017、026 后 | receipt owner/TTL/status/cancel 全部 fail-closed 可验证 | status/cancel 不对外开放 |
| Gate-DMD-07 | Shutdown/Observability/Reload 门 | DMD-TODO-021~022、027、033 后 | Draining、abandoned audit、结构化字段、failure injection、hot-reload allowlist 通过 | 修 lifecycle/listener/observability/reloader |
| Gate-DMD-08 | 并发与长期运行门 | DMD-TODO-034 后 | backpressure、soak、receipt TTL 清理、资源计数回落全部可验证 | 不开放长期驻留交付声明 |
| Gate-DMD-09 | 集成与交付证据门 | DMD-TODO-024~028、031、034、035 后 | daemon ping/unary/async/failure/profile/CLI/soak/deploy gate 有命令证据，阻塞项状态回写 | 不标记专项完成 |
| Gate-DMD-10 | 评审回归整改复验门 | DMD-TODO-036~040 后 | daemon/CLI 默认 endpoint 一致、真实 socket mode = 0600 且 stale restart 可恢复、main 接入真实 profile / reload snapshot、部署与 CLI 文档及 focused evidence 同步 | 不恢复专项 fully-closed 结论，继续整改并重写 Gate 证据 |

## 8. 阻塞项与解阻条件

| 阻塞 ID | 阻塞项 | 影响任务 | 证据 | 解阻条件 | 回退策略 |
|---|---|---|---|---|---|
| DMD-BLK-001 | daemon profile 键资产未完成一致性验证 | DMD-TODO-003、027 | 已由 DMD-TODO-003 清除：五档 profile 均已补 `daemon.*` 关键键，并由 `DaemonProfileProjectionTest` / `ProfileMatrixConsistencyTest` 覆盖 | 五档 profile 均有 daemon 运行键或明确安全默认投影 | 默认 diag/override/watchdog 关闭，socket_path 使用部署层保守默认 |
| DMD-BLK-002 | 已由 DMD-TODO-008 清除：supervisor/watchdog 对外通知 surface 已冻结为 no-op + `IWatchdogService` bridge | DMD-TODO-035 | `DaemonSupervisorAdapter` 已固定 `notify_ready()` / `notify_stopping()` / `tick_watchdog()` 三个入口；`DaemonSupervisorAdapterTest` 已验证 no-op path、watchdog bridge 与 failure surfacing，`WatchdogServiceInterfaceTest` 回归通过 | 已满足：v1 只交付 no-op + watchdog service bridge，systemd 专用 fd/notify 保持 v2 范围外 | 若后续需要 systemd notify/socket activation，作为 DMD-TODO-035 或 v2 扩展单独收敛，不回退 v1 no-op 契约 |
| DMD-BLK-003 | 已由 DMD-TODO-010 清除：daemon composition root 已改走 access public factory | DMD-TODO-010 | `access/include/AccessGatewayFactory.h` 已作为 public seam 导出；`apps/daemon/src/main.cpp` 与 `apps/daemon/CMakeLists.txt` 已移除对 `access/src` 的直接依赖；`AccessInterfaceSurfaceTest` 与 `dasall_daemon` 验证通过 | 已满足：daemon 侧 concrete include 边界已冻结为 public factory 方案 | 若后续要让 `apps/gateway` 同步收敛，可独立复用该 public factory，不回退 daemon 方案 |
| DMD-BLK-004 | 已由 DMD-TODO-029 清除：`UnixIpcProvider` 已形成测试可用双向 request/response loopback | DMD-TODO-019、024、025、026、027、031、034 | `UnixIpcProviderLoopbackTest` 已验证 client->server、server->client、peer_closed 与 payload budget；`DaemonLoopbackFixtureTest` 已验证 daemon listener 消费 ping 并向 client 返回 response | 已满足：platform loopback fixture 可传递 request/response，006/009 可继续拆分 listener/bootstrap，后续 daemon integration 可基于该闭环继续推进 | 如后续真实 UDS 行为与 loopback fixture 不一致，新增 platform gate 收敛，不回退到 fake IIPC 冒充 E2E |
| DMD-BLK-005 | 已由 DMD-TODO-016 解组：status 首版 scope 冻结为 `AsyncTaskRegistry` registry-only | DMD-TODO-026 | `IAccessRuntimeBridge` 已有 `cancel`，但没有 status/query 方法；本轮不引入 runtime live status 依赖 | 已满足：DMD-TODO-016 采用 registry-only 查询，不等待 Runtime query surface | 若后续需要 live status，新增 Runtime query seam 并升级 status 路由 |
| DMD-BLK-006 | 已解阻：daemon diag 命令白名单与 infra diagnostics facade seam 已冻结 | DMD-TODO-020、027 | 详设要求只读 diag，当前已通过 `IDiagnosticsService` + `kReadOnlyCommandWhitelist` 收敛 daemon handler | 后续仅需在 composition root 增补 concrete service 装配，不再阻塞 gate 设计 | v1 默认关闭 diag，不阻塞 ping/unary/async |
| DMD-BLK-007 | 已由 DMD-TODO-031 清除：CLI-daemon response schema 已被客户端消费 | DMD-TODO-025、026、031 | `CliIpcClient` 现在会通过共享 `DaemonFrameCodec` 编码请求、读取并解析 `UdsResponseFrame`；`CliIpcClientResponseTest` 覆盖 accepted_async/not_ready/rejected，`DaemonPingIntegrationTest` 覆盖 CLI 读 pong/readiness | 已满足：CLI/client 已能区分 accepted/rejected/not_ready/receipt，不再以 `send()` 成功冒充业务成功 | 后续 unary/status/cancel E2E 继续沿用当前 `DaemonClientResponse`，不回退到 send-only smoke |
| DMD-BLK-008 | 已由 DMD-TODO-035 清除：部署目录、service 样例与 supervisor 最小契约已冻结 | DMD-TODO-035 | `docs/deploy/daemon/README.md`、`ACCEPTANCE_CHECKLIST.md`、`dasall-daemon.service`、`daemon.example.yaml/json` 已落盘；real smoke 已验证 validate/start/ping/readiness/stop；socket activation 明确保留为 v2 | 已满足：docs/deploy/daemon 成为 v1 交付基线，no-op/watchdog bridge 与 direct-bind service 语义已定稿 | 若后续要扩展 `Type=notify` 或 `.socket` 单元，单独作为 v2 任务推进，不回退当前 v1 契约 |

## 9. 验收与质量门

### 9.1 验收矩阵

| 验收项 | 覆盖任务 | 必须通过的证据 |
|---|---|---|
| Schema/config 可执行 | DMD-TODO-001~004 | `DaemonProtocolTypesTest`、`DaemonBootstrapConfigTest`、`DaemonConfigValidatorTest` |
| 进程生命周期可控 | DMD-TODO-005~009、022 | `DaemonLifecycleControllerTest`、`DaemonListenerHostTest`、`DaemonBootstrapTest`、`DaemonGracefulShutdownTest` |
| IIPC / frame / UDS 安全 | DMD-TODO-011~012、029、030、032 | `UnixIpcProviderLoopbackTest`、`DaemonFrameCodecMalformedTest`、`DaemonProtocolAdapterLocalTrustedTest`、`DaemonPeerIdentityFailClosedTest`、`UnixIpcProviderPeerIdentityTest`、`DaemonSocketPolicyTest` |
| Access/Runtime 主链 | DMD-TODO-013~014、025 | `DaemonAccessPipelineFactoryTest`、`DaemonUnaryRuntimeBridgeTest`、`DaemonUnaryIntegrationTest`、`DaemonBinaryUnarySmokeTest` |
| Async/status/cancel | DMD-TODO-015~017、026 | `DaemonAcceptedAsyncReceiptTest`、`DaemonTaskQueryHandlerTest`、`DaemonReceiptFlowIntegrationTest` |
| Health/diag/observability/reload | DMD-TODO-018~021、027、033、039 | `DaemonHealthServiceTest`、`DaemonReadinessCommandTest`、`DaemonDiagnosticsHandlerTest`、`DaemonObservabilityFieldSetTest`、`DaemonConfigReloadTest`、`DaemonHotReloadIntegrationTest` |
| CLI 与真实 daemon E2E | DMD-TODO-024、031 | `CliIpcClientTest`、`CliIpcClientResponseTest`、`CliIpcClientUnavailableTest`、`CliDaemonCommandParserTest`、`CliDaemonOutputFormatterTest`、`DaemonPingIntegrationTest` |
| 运行面一致性与入口配置回归 | DMD-TODO-036~040 | `CliDaemonSocketPathIntegrationTest`、`DaemonSocketModeIntegrationTest`、`DaemonStaleSocketRecoveryIntegrationTest`、`DaemonEntryConfigProjectionTest`、`DaemonHotReloadIntegrationTest`、文档一致性 `rg` 检查 |
| 并发/soak/长期运行 | DMD-TODO-022、034 | `DaemonBackpressureIntegrationTest`、`DaemonSoakIntegrationTest`、`DaemonReceiptTtlCleanupIntegrationTest` |
| 集成与交付证据 | DMD-TODO-023~028、035 | `ctest -N` discoverability、daemon ping/unary/async/failure/profile/deploy gate 命令证据 |

### 9.2 统一验收命令建议

```bash
ctest --test-dir build-ci -N | rg "Daemon(Bootstrap|LifecycleController|ListenerHost|ConfigValidator)Test|DaemonPingIntegrationTest|DaemonUnaryIntegrationTest|DaemonBinaryUnarySmokeTest|DaemonRejectPathIntegrationTest|DaemonReceiptFlowIntegrationTest|DaemonFailureInjectionTest|DaemonProfileCompatibilityTest|DaemonHotReloadIntegrationTest"
ctest --test-dir build-ci --output-on-failure -R "DaemonProtocolTypesTest|DaemonBootstrapConfigTest|DaemonConfigValidatorTest|DaemonLifecycleControllerTest|DaemonListenerHostTest|DaemonBootstrapTest|UnixIpcProviderLoopbackTest|UnixIpcProviderPeerIdentityTest|DaemonFrameCodecTest|DaemonFrameCodecMalformedTest|DaemonPeerIdentityFailClosedTest|DaemonSocketPolicyTest"
ctest --test-dir build-ci --output-on-failure -R "Daemon(PingIntegration|UnaryIntegration|BinaryUnarySmoke|RejectPathIntegration|ReceiptFlow|FailureInjection|GracefulShutdown|ProfileCompatibility|HotReloadIntegration)Test|Cli(IpcClient(Test|ResponseTest|UnavailableTest)|DaemonCommandParserTest|DaemonOutputFormatterTest)"
build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_backpressure_integration_test
build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_soak_integration_test
build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_receipt_ttl_cleanup_integration_test
```

说明：`dasall_unit_tests`、`dasall_integration_tests` 与 `RunCtest_CMakeTools` 在当前仓库仍可能受到既有 runtime/infra 失败、`DartConfiguration.tcl` 噪声和 034 soak 路径卡住现象影响；本专项最终以 9.4 的 focused gate matrix 作为主判定信号。

### 9.3 质量 Gate 清单

| Gate | 二值通过条件 |
|---|---|
| Gate-DMD-01 | frame/config 类型与校验测试全绿，配置非法不会 bind listener |
| Gate-DMD-02 | daemon 壳层组件单测可发现，main 不再硬编码关键运行配置 |
| Gate-DMD-03 | IIPC 双向 loopback、frame codec、peer identity fail-closed、UDS endpoint 安全同时通过 |
| Gate-DMD-04 | unary happy path、auth deny、validation reject、runtime unavailable 全部经 access core，且 built daemon + built cli unary smoke 通过真实 `main.cpp` 路径 |
| Gate-DMD-05 | ping/readiness/diag 三者语义分离，diag 默认关闭，CLI 读取响应内容而非只断言 send 成功 |
| Gate-DMD-06 | receipt owner/TTL/status/cancel fail-closed 全部可验证 |
| Gate-DMD-07 | graceful shutdown 排空或 abandoned 审计、`daemon.diag_enabled` 单键 hot-reload 可验证，非 allowlisted key 稳定拒绝 |
| Gate-DMD-08 | backpressure、soak、receipt TTL 清理和资源回落可验证 |
| Gate-DMD-09 | 所有通过/未通过 gate 均回写命令证据与残余 blocker，部署契约已评审 |
| Gate-DMD-10 | 默认 endpoint、真实 socket mode/stale restart、profile/reload 真接线与文档/证据复验同时完成 |

### 9.4 Gate 执行证据（2026-05-02）

说明：`Gate-DMD-01` ~ `Gate-DMD-09` 为首轮收敛基线；`DMD-TODO-036` ~ `DMD-TODO-040` 完成后，`Gate-DMD-05` / `Gate-DMD-07` / `Gate-DMD-09` 已完成 focused 复验，`Gate-DMD-10` 已补齐为当前 close-ready 主判定。

| Gate ID | 结论 | 命令证据 | 结果摘要 |
|---|---|---|---|
| Gate-DMD-01 | PASS | `ctest --test-dir build-ci --output-on-failure -R "DaemonProtocolTypesTest|DaemonBootstrapConfigTest|DaemonConfigValidatorTest"` | `DMD-TODO-001`、`002`、`004` 已冻结 command taxonomy、config 投影与 validate-only 路径；非法配置在 bind 前失败。 |
| Gate-DMD-02 | PASS | `cmake --build build-ci --target dasall_daemon_bootstrap_unit_test dasall_daemon_lifecycle_controller_unit_test dasall_daemon_listener_host_unit_test dasall_daemon_config_validator_unit_test`；`ctest --test-dir build-ci -N | rg "Daemon(Bootstrap|LifecycleController|ListenerHost|ConfigValidator)Test"` | `DMD-TODO-005` ~ `010` 的进程壳层拆分已由 `DMD-TODO-023` discoverability 收口；`DaemonBootstrapTest`、`DaemonLifecycleControllerTest`、`DaemonListenerHostTest`、`DaemonConfigValidatorTest` 均可稳定发现。 |
| Gate-DMD-03 | PASS | `RunCtest_CMakeTools(tests=["UnixIpcProviderLoopbackTest","UnixIpcProviderPeerIdentityTest","DaemonFrameCodecTest","DaemonFrameCodecMalformedTest","DaemonProtocolAdapterTest","DaemonProtocolAdapterLocalTrustedTest","DaemonPeerIdentityFailClosedTest","DaemonSocketPolicyTest","DaemonListenerHostBindConflictTest","DaemonConfigValidatorTest"])` | `DMD-TODO-029`、`030`、`011`、`012`、`032` 已证明 request/response loopback、frame codec、peer identity fail-closed 与 UDS endpoint 安全同时成立。 |
| Gate-DMD-04 | PASS | `RunCtest_CMakeTools(tests=["DaemonAccessPipelineFactoryTest","RuntimeBridgeRejectMappingTest","DaemonUnaryRuntimeBridgeTest","DaemonUnaryIntegrationTest","DaemonRejectPathIntegrationTest","DaemonBinaryUnarySmokeTest"])` | `DMD-TODO-013`、`014`、`025` 已证明 unary happy path、unknown command、auth deny、validation reject 与 runtime unavailable 全部经共享 access core 收敛；新增 `DaemonBinaryUnarySmokeTest` 后，built `dasall_daemon` + built `dasall_cli run` 的真实 binary unary 主链也已打通。 |
| Gate-DMD-05 | PASS | `RunCtest_CMakeTools(tests=["DaemonPingCommandTest","DaemonReadinessCommandTest","DaemonPingDoesNotBypassRouterTest","DaemonDiagnosticsHandlerTest","DaemonDiagDenyIntegrationTest","CliIpcClientTest","CliIpcClientResponseTest","CliIpcClientUnavailableTest","CliDaemonCommandParserTest","CliDaemonOutputFormatterTest","DaemonPingIntegrationTest"])` | `DMD-TODO-019`、`020`、`031` 已证明 ping/readiness/diag 三者分离、diag 默认关闭，CLI 已能读取结构化 daemon response 而非只断言 `send()` 成功。 |
| Gate-DMD-06 | PASS | `RunCtest_CMakeTools(tests=["DaemonAcceptedAsyncReceiptTest","DaemonTaskQueryHandlerTest","DaemonCancelCommandTest","DaemonReceiptFlowIntegrationTest"])` | `DMD-TODO-015`、`016`、`017`、`026` 已证明 receipt owner、TTL、status、cancel 全链路 fail-closed。 |
| Gate-DMD-07 | PASS | `RunCtest_CMakeTools(tests=["DaemonObservabilityFieldSetTest","AccessGatewayLifecycleTest","DaemonGracefulShutdownTest","DaemonShutdownAbandonedAuditTest","DaemonFailureInjectionTest","DaemonProfileCompatibilityTest","DaemonConfigReloadTest","DaemonSignalHandlerTest","DaemonHotReloadIntegrationTest"])` | `DMD-TODO-021`、`022`、`027`、`033`、`039` 已证明 graceful shutdown、abandoned audit、failure injection、profile compatibility 与 `daemon.diag_enabled` 单键 hot-reload 全部收敛；`daemon.log_format`、`daemon.socket_path` 等非 allowlisted 键继续拒绝。 |
| Gate-DMD-08 | PASS | `Build_CMakeTools(buildTargets=["dasall_access_daemon_backpressure_integration_test","dasall_access_daemon_soak_integration_test","dasall_access_daemon_receipt_ttl_cleanup_integration_test"])`；直接执行 `build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_backpressure_integration_test`、`dasall_access_daemon_soak_integration_test`、`dasall_access_daemon_receipt_ttl_cleanup_integration_test` | `DMD-TODO-034` 已证明 backpressure、soak、receipt TTL cleanup 与资源计数回落；由于当前环境下 `RunCtest_CMakeTools` 会卡住，该 gate 以 direct-binary focused evidence 为权威结果。 |
| Gate-DMD-09 | PASS | `rg -n "DMD-TODO-028|Gate-DMD-0[1-9]|DMD-BLK-00[1-8]|当前残余风险" docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md docs/todos/daemon/deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md docs/worklog/DASALL_开发执行记录.md` | `DMD-TODO-028` 已将 Gate-DMD-01 ~ 09、`DMD-BLK-001` ~ `008`、当前残余风险、回退路径与交付物回链统一落盘，专项已具备收口证据。 |
| Gate-DMD-10 | PASS | `rg -n "当前 CLI 尚未消费 readiness 响[应]|CliDaemonPingIntegrationTe(st)|专项状态从 fully-close[d]" docs/deploy/daemon docs/todos/daemon tests/integration/access`；`RunCtest_CMakeTools(tests=["DaemonPingIntegrationTest","CliDaemonCommandParserTest","DaemonConfigReloadTest","DaemonProfileCompatibilityTest"])` | `DMD-TODO-036` ~ `040` 已完成默认 endpoint、真实 socket mode/stale restart、entry/reload 真接线和 docs/test evidence cleanup；最终 focused build 同时补齐了 `apps/daemon` unit-test 拓扑对共享 daemon public 头的 include 可见性，`Gate-DMD-05` / `Gate-DMD-07` / `Gate-DMD-09` 已按整改后 focused evidence 复验。 |

### 9.5 阻塞变化与回退记录

1. `DMD-BLK-001` ~ `DMD-BLK-008` 已全部清除，当前 daemon 专项范围内无 open blocker；blocker 状态以 §8 和本节证据表为准。
2. 本专项的权威验证路径已收敛为任务级 focused matrix，不再把 `dasall_unit_tests` / `dasall_integration_tests` 聚合结果直接记为 daemon gate 结论；聚合噪声由对应 runtime/infra owner 继续处理。
3. `RunCtest_CMakeTools` 在当前环境下仍可能打印仓库既有 `DartConfiguration.tcl` 噪声，且 034 soak gate 会卡住；本轮已按 `Build_CMakeTools + direct binary execution` 回退验证，不改写任何 daemon 产品语义。
4. `DMD-TODO-028` 未触发代码回退，仅完成 evidence writeback；后续若需复验，优先复用 §9.4 的 focused gate 命令，而不是扩大诊断范围或回退到 send-only smoke。

## 10. 残余风险与回退策略

| 残余风险 | 当前状态 | 影响 | 收敛 / 回退策略 |
|---|---|---|---|
| 全仓聚合 build / ctest 仍受 runtime、infra 既有失败污染 | Open（仓库级环境噪声） | `dasall_unit_tests` / `dasall_integration_tests` 不能直接作为 daemon Gate PASS / FAIL 的主判定 | 持续以 §9.4 focused gate matrix 为权威证据；跨模块失败由对应 owner 子系统单独解阻 |
| `RunCtest_CMakeTools` 对 034 soak 路径仍可能卡住，并继续打印 `DartConfiguration.tcl` 噪声 | Open（工具链噪声） | CMake Tools 结果不能单独代表 034 长期运行 gate 结论 | 沿用 `Build_CMakeTools + direct binary execution`；若后续工具链稳定，再补一条非阻断增强证据 |
| `status` 当前仍为 registry-only，而非 runtime live status | Scoped limitation | v1 status 语义只覆盖 receipt registry 生命周期，不覆盖 Runtime 内部实时调度状态 | 保持当前边界，不把 registry-only 结果外推为 live runtime query；若要扩展，新增 runtime query seam 任务 |
| v1 部署边界未覆盖 `Type=notify`、socket activation、remote control plane、streaming attach、多 daemon 隔离 | Scoped limitation | 当前 direct-bind v1 gate 不能外推到更宽的 supervisor / remote / multi-instance 场景 | 继续保持 v2 范围外，任何边界扩展必须单独立题，不复用 035/028 结论 |
| 长期运行声明当前只覆盖 deterministic in-process backpressure / soak / TTL gate 与单机 direct-bind smoke | Limited evidence | 尚未证明更长时长、多节点或跨主机部署下的资源曲线 | 当前以 `DMD-TODO-034` 为 v1 release boundary；若要扩大长期运行声明，新增更长时段 soak / multi-host gate |

## 11. 可行性结论

### 11.1 当前执行结论

1. `DMD-TODO-001` ~ `DMD-TODO-040` 已全部完成；`Gate-DMD-01` ~ `Gate-DMD-09` 的历史 focused evidence 保持可追溯，`Gate-DMD-10` 已在整改完成后补齐并 PASS。
2. 当前可直接复用的稳定能力已覆盖 local direct-bind UDS、real daemon/CLI unary smoke、accepted_async、ping/status/cancel/只读 diag、readiness、CLI wire contract、并发/soak、部署契约，以及默认 endpoint、一致 socket mode/stale restart、entry/reload 真接线与文档/测试证据的最终收口状态。
3. 权威证据位于本文件 §9.4/§9.5、`docs/todos/daemon/deliverables/` 各交付物与 `docs/worklog/DASALL_开发执行记录.md`；当前专项已恢复为 close-ready 结论，不再保留 reopen hardening 状态。

### 11.2 当前最小可宣称边界

1. `status` 继续冻结为 registry-only 语义，不外推为 Runtime live status query。
2. supervisor 契约继续冻结为 no-op + watchdog bridge seam；`Type=notify` 与 socket activation 仍属于 v2。
3. diagnostics 继续保持默认关闭、只读白名单与显式授权；`SIGHUP` 当前只允许 `daemon.diag_enabled` 单键热更，不扩张为写操作或远程控制面。
4. 长期运行质量声明只覆盖 `DMD-TODO-034` 的 deterministic in-process backpressure / soak / TTL matrix 与 `DMD-TODO-035` 的单机 direct-bind smoke。

### 11.3 后续建议

1. 若需要更强的仓库级通过信号，优先治理 `dasall_unit_tests` / `dasall_integration_tests` 的跨子系统噪声，再考虑把聚合命令恢复为 daemon gate 辅助证据。
2. 若需要扩大部署或控制面边界，按 v2 新任务推进 `Type=notify`、socket activation、remote control plane、streaming attach、多 daemon 隔离，不复用当前 v1 Gate 结论外推。
3. 若需要提升长期运行声明，新增更长时段、多主机或外部 client mix 的 soak harness，而不是放宽 `DMD-TODO-034` 现有完成判定。
