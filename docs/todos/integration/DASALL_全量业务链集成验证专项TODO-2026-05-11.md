# DASALL 全量业务链集成验证专项 TODO

最近更新时间：2026-05-11  
阶段：System Integration -> Full Business Chain Verification  
适用范围：`contracts/`、`profiles/`、`infra/`、`platform/`、`services/`、`tools/`、`knowledge/`、`memory/`、`llm/`、`cognition/`、`runtime/`、`multi_agent/`、`access/`、`apps/cli`、`apps/daemon`、`apps/gateway`、`debian/`、`scripts/packaging/`、`tests/`

当前结论：DASALL 已经不是空架子。`Gate-INT-03~09` 已闭合 focused / true integration 主线，`Gate-INT-10` 与 `dasall_packaging_preflight_tests` 已闭合 build-tree release-preflight / app-binary smoke，Ubuntu DPKG v1 package gate 文档记录了 local lifecycle、lintian 与 qemu `autopkgtest` 证据，2026-05-11 installed-package `dasall run` 已通过真实 DeepSeek-compatible LLM 路径并断言 `llm.origin=deepseek-prod/deepseek-reasoner`。但这些证据仍不能外推为“全量业务链 production ready”：Knowledge 安装态独立 retrieve/refresh/health 正向入口、daemon/gateway `default-ready` / `degraded-ready` 对外投影、multi_agent profile enablement、gateway shutdown exit-code 断言、release runner 复验与长稳态 soak 仍需按业务链逐项验证和补齐。

本文档作为 2026-05-06 系统集成专项与 2026-05-09 系统集成修复专项之后的新一轮增量规划，不重开已完成的 `INT-TODO-*` / `INTFIX-TODO-*` 历史任务，只把现有架构、流程图、时序图、Gate 证据和剩余缺口转换为可逐条执行的全业务链集成验证任务。

## 1. 文档头

本文档基于以下输入生成：

1. `docs/ssot/SystemIntegrationGateMatrix.md`
2. `docs/worklog/DASALL_开发执行记录.md`，重点采用记录 #620、#619、#618~#615 的当前基线。
3. `docs/todos/integration/DASALL_系统集成专项TODO.md`
4. `docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md`
5. `docs/todos/integration/deliverables/INT-TODO-024-系统集成Gate与交付证据回写收口.md`
6. `docs/todos/integration/deliverables/INTFIX-TODO-012-系统集成修复Gate与证据收口.md`
7. `docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md`
8. `docs/todos/access/DASALL_access子系统专项TODO.md`
9. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md`
10. `docs/todos/memory/DASALL_memory子系统专项TODO.md`
11. `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`
12. `docs/todos/llm/DASALL_llm子系统专项TODO.md`
13. `docs/todos/tools/DASALL_tools子系统专项TODO.md`
14. `docs/todos/services/DASALL_capability_services子系统专项TODO.md`
15. `docs/todos/profiles/DASALL_profiles子系统专项TODO.md`
16. `docs/architecture/DASALL_runtime子系统详细设计.md`
17. `docs/architecture/DASALL_access子系统详细设计.md`
18. `docs/architecture/DASALL_cognition子系统详细设计.md`
19. `docs/architecture/DASALL_memory子系统详细设计.md`
20. `docs/architecture/DASALL_knowledge子系统详细设计.md`
21. `docs/architecture/DASALL_llm子系统详细设计.md`
22. `docs/architecture/DASALL_tools子系统详细设计.md`
23. `docs/architecture/DASALL_capability_services子系统详细设计.md`
24. `docs/architecture/DASALL_profiles模块详细设计.md`
25. `docs/architecture/DASALL_infrastructure子系统详细设计.md`
26. `docs/architecture/DASALL_multi_agent子系统详细设计.md`
27. `tests/CMakeLists.txt` 与 `tests/integration/**/CMakeLists.txt`
28. `scripts/packaging/validate_gate_int_10_installed_package_qemu.sh`

编制原则：

1. 不使用 subsystem smoke、fixture gate、ping/liveness 或旧历史绿灯替代 true integration / app-binary / installed-package 证据。
2. 不把 build-tree `release-preflight` 外推为 installed-package / qemu / production ready。
3. 不把 `agent.dataset` 当作 installed-package `run` 成功语义；LLM 主链必须断言 provider / model origin。
4. 不泄露、记录或提交 DeepSeek secret 值；secret 只允许以 secret URI / testbed 注入状态进入证据。
5. 每个后续任务必须包含代码目标、测试目标、验收命令三件套。
6. `ADR-006`、`ADR-007`、`ADR-008` 边界必须继续成立：Memory 掌上下文，Runtime 掌恢复准入和全局主控，MultiAgent 不能形成第二主循环。

## 2. 当前集成基线

| 层级 | 当前证据 | 当前结论 | 不允许外推 |
|---|---|---|---|
| focused / true integration | `Gate-INT-03~09` 已 Pass；`dasall_gate_int_09` 作为 one-shot acceptance；`dasall_gate_int_08` 作为 Access focused gate | default unary、structured evidence、diagnostics snapshot、degraded semantics、tools/services 语义、Access v1 focused ingress 已有正式 Gate | 不代表 app-binary、installed-package、qemu 或 production release 完整 ready |
| app-binary / build-tree release-preflight | `dasall_gate_int_10` 与 `dasall_packaging_preflight_tests` 已通过；daemon/gateway binary smoke 与 startup diagnostics 已接入 | build-tree 下真实 app 二进制入口、release-preflight 标签和 discoverability 已闭合 | 不代表 installed-package qemu、长稳态 soak、default-ready 健康语义已闭合 |
| installed-package local smoke | `pkg_smoke_install.sh --explicit-start-check` 已通过，`dasall run` 真实 DeepSeek LLM path 返回 completed / `llm.origin=deepseek-prod/deepseek-reasoner` | package smoke 已从生命周期可用升级到控制面 + 主功能语义可用 | 不代表 Knowledge installed-package ready；不代表外部 LLM 抖动已被 CI 策略吸收 |
| packaging release evidence | Packaging TODO 记录 PKG-TODO-018 已有 lintian `RC=0` 与 qemu `autopkgtest RC=0` 历史证据；#619 新增 Gate-INT-10 -> qemu 串联脚本 | package v1 gate 有历史收口证据，且仓库内已有正式串联入口 | 若发布环境、镜像、secret、runner 变化，仍需在 release runner 复跑并归档 |
| 子系统专项 | memory、tools、services、profiles、llm 等专项基本闭合；knowledge 已具 query / evidence / index / health 任务链和 build 基线 | 多数子系统已具模块内 unit / integration / profile / observability 证据 | 子系统闭合不自动等于跨入口、安装态、真实依赖全链闭合 |
| multi_agent | 详设有主流程、状态机和投影规则；profile 中已有 enabled 声明风险 | 设计链路清楚，但无专项 TODO，且实现仍有 placeholder / enablement 落差 | 不得宣称 GA runtime-ready 或 profile-enabled production path |

## 3. 全量业务链清单

| 链路 ID | 业务链 | 入口 | 核心路径 | 设计依据 | 当前证据 | 剩余缺口 | 后续任务 |
|---|---|---|---|---|---|---|---|
| BC-01 | CLI / daemon 本地控制面 | `dasall ping/readiness/run/status/cancel/diag` | CLI parser -> IPC client -> daemon listener -> Access/Runtime bridge -> Result | Access 主流程、packaging TODO、worklog #620 | packaging local smoke 覆盖 ping/readiness/run/status/cancel/diag；DaemonPing / CliDaemonSocketPath / DaemonBinaryUnary smoke 已接入 release-preflight | 需要把 degraded/default-ready 语义投影到对外 readiness；status/cancel 缺失 receipt 仍应作为语义断言保留 | FULLINT-TODO-006、007、013 |
| BC-02 | HTTP gateway unary ingress | HTTP submit | HttpProtocolAdapter -> AccessGateway -> RuntimeBridge -> Runtime -> ResultPublisher -> HTTP envelope | Access 主流程时序、Gate-INT-08、Gate-INT-10 | HttpGatewaySubmitIntegrationTest、GatewayBinaryUnarySmokeTest、GatewayMissingBackend regression 已通过历史 Gate | shutdown exit-code 未显式断言；release-preflight 与 packaging_preflight 证据容易误读 | FULLINT-TODO-007、010、012 |
| BC-03 | Access admission / policy / idempotency | CLI / HTTP / daemon submit | ProtocolAdapter -> SubjectResolver -> AuthenticatorChain -> AccessPolicyGate -> RateLimit/Idempotency -> RequestNormalizer | Access §6.7、§6.14、§6.20 | Gate-INT-08 覆盖 production ingress、policy backend unavailable、health readiness、profile/contracts guard | 更广安全治理、release hardening、streaming / diagnostics pull 仍非 v1 ready | FULLINT-TODO-006、013 |
| BC-04 | Async receipt / query / cancel / replay | AcceptedAsync receipt / status / cancel | RuntimeBridge AcceptedAsync -> AsyncTaskRegistry -> query/cancel -> ResultReplayCache -> Publisher | Access AsyncTaskRegistry、RuntimeBridge 局部时序 | Gate-INT-08 覆盖 async receipt/query/cancel focused path；package smoke 覆盖 missing receipt status/cancel | 需要把 trace cause chain、receipt ownership 与 installed-package 行为整理成可复验矩阵 | FULLINT-TODO-011、013 |
| BC-05 | Runtime single-agent unary 主链 | AgentRequest / RuntimeDispatchRequest | AgentFacade -> AgentOrchestrator -> Session/FSM/Budget/Checkpoint -> Memory -> Cognition -> optional Tool -> Response -> AgentResult | Runtime §6.7、FSM、单 Agent unary sequence | Gate-INT-03、RuntimeUnaryIntegrationTest、CognitionRuntimeIntegrationTest、MainFlowContractE2ETest | app-binary smoke 需拒绝 stub-ready；optional knowledge/llm 缺失时 readiness 语义需对外明确 | FULLINT-TODO-003、007、013 |
| BC-06 | Cognition decision / reflection / response | Runtime step request | Context/Belief -> Perception/Planner/Reasoner -> LLM stage -> ActionDecision -> reflect -> ResponseBuilder | Cognition §6.7、§6.8、§6.14 | Gate-INT-03 与 installed-package direct response LLM path 已覆盖主线重要切片 | BeliefUpdateHint 写回时序、阶段级失败回流仍需作为全链验证断言项 | FULLINT-TODO-003、012 |
| BC-07 | LLM production generation | Runtime / Cognition / ResponseBuilder LLM request | PromptRegistry/Pipeline -> PromptComposer -> PromptPolicy -> LLMManager -> ModelRouter -> Adapter -> Observability | LLM §6.7、§6.8；worklog #620 | installed-package `dasall run` 已真实调用 DeepSeek-compatible provider，断言 `llm.origin` | CI / release runner 需要 secret 注入和外部抖动策略；streaming 后置 | FULLINT-TODO-012、013、019 |
| BC-08 | Knowledge retrieve / evidence projection | Runtime KnowledgeQuery | KnowledgeServiceFacade -> QueryNormalizer -> CorpusRouter -> Recall -> Reranker -> EvidenceAssembler -> Runtime -> Memory ContextOrchestrator | Knowledge §6.7、§6.8、§6.9 | Gate-INT-04 已锁 RuntimeEvidenceProjection + KnowledgeEvidencePreservation；knowledge 模块有 lexical / evidence / quality gates | installed-package 缺独立 retrieve/refresh/health 正向入口；不能仅靠 runtime evidence gate 宣称 package-ready | FULLINT-TODO-014 |
| BC-09 | Memory context assembly | Runtime prepare_context | MemoryManager -> ContextOrchestrator -> CandidateCollector -> stores/vector -> BudgetAllocator -> ContextPacket | Memory §6.8.1 | Memory 专项已闭合；MemoryContextAssembleIntegrationTest 与 profile/failure/writeback gates 存在 | 需要在全链中断言 Knowledge external_evidence、LLM over-budget reassemble 与 degraded warning 不丢失 | FULLINT-TODO-003、012 |
| BC-10 | Memory writeback / compression / maintenance | Runtime write_back / maintenance | WritebackCoordinator -> store transaction -> fact/experience -> vector sidecar -> maintenance/checkpoint | Memory §6.8.2、§6.9 | MemoryWritebackIntegrationTest、MemoryMaintenanceIntegrationTest、MemoryFailureInjectionTest 已落盘 | 需要和 runtime checkpoint/resume、tools observation、multi_agent observation 折叠共同验证 | FULLINT-TODO-011、015 |
| BC-11 | Tools governed execution | Runtime ToolRequest | ToolManager -> Registry -> Validator -> PolicyGate -> RouteSelector -> Executor -> ResultProjector -> ObservationDigest | Tools §6.7、§6.8 | ToolServices / Observability / WorkflowFailure / MCPFallback / SkillRuntime / ProfileIntegration 已闭合；Gate-INT-07 守语义 | generic MCP 只到 loopback / plugin-stdio hybrid；runtime production caller adapter 是跨模块后续事项 | FULLINT-TODO-012、016 |
| BC-12 | Capability Services execution/data/system | Tools service request | IExecutionService / IDataService -> ServiceFacade -> Lane -> AdapterRouter -> AdapterBridge -> ResultMapper -> Observability | Services §6.7、§6.8 | Services smoke/failure/profile integration 全绿；高风险 action fail-closed | 高风险确认闭环与真实 platform/remote adapter 非 v1 完整范围 | FULLINT-TODO-012、016 |
| BC-13 | Infra config / policy / secret / plugin / diagnostics / health | App startup / diagnostics | InfraFacade -> ConfigCenter -> Policy -> Logging/Audit/Trace/Metrics -> Secret -> Plugin -> Diagnostics -> Health/Watchdog | Infra §6.7、§6.8、§6.10 | Gate-INT-05、startup diagnostics tests、package smoke secret onboarding | app-binary failure stage 覆盖仍需扩到 runtime composition/init、AccessGateway init、listen/bind | FULLINT-TODO-009、017 |
| BC-14 | Profiles build/runtime policy activation | CMake profile / app profile_id / override | ProfileCatalog -> Resolver/Provider -> OverlayComposer -> Validator -> LKG -> RuntimePolicySnapshot | Profiles §6.7、§6.8 | profiles unit/integration/contract 已闭合；runtime policy schema gate 存在 | multi_agent enabled_modules 与 runtime snapshot / coordinator 装配仍有声明落差 | FULLINT-TODO-004、018 |
| BC-15 | Recovery / safe-mode / resume | Runtime failure / checkpoint | ReflectionDecision -> RecoveryManager -> retry/replan/abort/degrade -> checkpoint/resume/safe-mode | Runtime 异常恢复时序、ADR-007 | Runtime gate 已覆盖部分 degraded/recovery semantics；runtime TODO 要求 replay / health / maintenance 一等化 | 需要全链验证 retry token、checkpoint causality、writeback/resume/audit continuity | FULLINT-TODO-011、018 |
| BC-16 | Packaging / installed-package / release handoff | `dpkg-buildpackage` / smoke / qemu | Gate-INT-10 -> packaging preflight -> build package -> metadata -> installed-package smoke -> qemu autopkgtest -> lintian | Packaging TODO、SystemIntegrationGateMatrix、worklog #619/#620 | v1 package gate closeout 与 local package smoke 均有证据；串联脚本已落盘 | release runner 必须复跑并归档；qemu image/secret/network 是环境前提 | FULLINT-TODO-002、013、019 |
| BC-17 | Multi-agent coordination | Runtime MultiAgentRequest | Runtime -> MultiAgentCoordinator -> Planner -> Registry/Lease/Scheduler -> WorkerBridge -> Merger -> Runtime fold Observation | MultiAgent §6.8、§6.9 | 详设清晰；profile 声明风险已被集成评审识别 | 无专项 TODO；实现与 profile enablement 未闭合；需要 Null/Real coordinator 与禁用态 Gate | FULLINT-TODO-004、018 |

## 4. 业务链验证分层

| 验证层 | 目标 | 推荐入口 | 必须说明 |
|---|---|---|---|
| L1 focused / fixture | 定位单链实现回归 | 具体 `ctest -R` 或模块 custom target | 只能证明局部切片，不能替代系统 Gate |
| L2 true integration | 跨模块 shared contract / runtime-facing boundary | `dasall_gate_int_03~08`、相关 integration tests | 必须覆盖真实调用链或共享对象，不用旧 fixture 冒充 |
| L3 app-binary / build-tree release-preflight | 真实 daemon/gateway/CLI 二进制入口 | `dasall_gate_int_10`、`dasall_packaging_preflight_tests`、startup diagnostics targets | 不代表 installed-package qemu / production ready |
| L4 installed-package local | 安装后生命周期和主功能语义 | `pkg_smoke_install.sh --explicit-start-check`、`sudo dasall run ... --json` | 必须断言 LLM origin，不能采信 `agent.dataset` |
| L5 release runner / qemu | 权威 installed-package gate | `validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>`、lintian、归档日志 | 依赖 runner、image、secret、网络；必须记录环境与证据路径 |
| L6 soak / chaos / production confidence | 长稳态、外部依赖抖动、恢复策略 | longer-running binary soak、provider retry budget、network failure tests | 属 release confidence 扩展，不替代前置 Gate |

## 5. 阻塞项与风险

| 阻塞项 ID | 阻塞描述 | 影响业务链 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| FULLINT-BLK-001 | release runner / qemu image / virt-server / secret 注入未固定 | BC-16、BC-07 | 在 CI 或 release runner 固化 image、virt args、secret injection，并归档 `autopkgtest` 日志 | 本地只宣称 build-tree / local installed smoke，不宣称 production release-ready |
| FULLINT-BLK-002 | Knowledge 安装态缺 retrieve/refresh/health 独立正向入口 | BC-08、BC-16 | 新增 CLI 或 daemon API 入口，或让 daemon live composition 接入真实 `IKnowledgeService` 并补 package smoke | 保留 Gate-INT-04 作为 build-tree evidence gate，不宣称 knowledge installed-package ready |
| FULLINT-BLK-003 | daemon/gateway readiness 对外语义仍可能把 accepted/degraded 当 ready | BC-01、BC-02、BC-05、BC-13 | 将 `AgentInitReadinessLevel` 投影到 health/readiness，对外区分 default-ready、degraded-ready、stub-ready | 对 release 文案只写 degraded candidate，不写 production default-ready |
| FULLINT-BLK-004 | multi_agent profile enablement 与实现落差 | BC-14、BC-17 | 新开 multi_agent/profile 任务，落 NullCoordinator / RealCoordinator 装配和禁用态 Gate | profile 声明中避免把 multi_agent 外推为 GA runtime-ready |
| FULLINT-BLK-005 | gateway shutdown exit-code 与 startup failure stage 覆盖不足 | BC-02、BC-13 | 补 gateway exit-code 断言和 runtime composition/init/listen/bind failure diagnostics binary regressions | release candidate 可继续编译，但 release confidence 降级 |

## 6. 专项 TODO

### 6.1 前置补设计 / 证据矩阵冻结任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FULLINT-TODO-001 | Done | 冻结全量业务链与证据矩阵 | 本文档 §3；SystemIntegrationGateMatrix；各子系统详设 | 17 条业务链、验证层级、证据命名与不外推规则 | L2 | `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md`；`docs/ssot/BusinessChainIntegrationMatrix.md` | `BC-*` 链路 ID、验证层级、Gate 映射、installed-package 本轮运行快照 | process：文档一致性与 Gate 映射检查；actual：`command -v dasall`、`dpkg-query`、`systemctl is-active/is-enabled`、`sudo -n dasall ping/readiness --json` | `rg -n "BC-0|Gate-INT-|FULLINT-TODO|FULLINT-BLK" docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md docs/ssot/SystemIntegrationGateMatrix.md docs/ssot/BusinessChainIntegrationMatrix.md` | 无 | 无 | 业务链编号、证据层级和旧 Gate 关系唯一；普通用户 socket 权限限制已显式记录 | `docs/ssot/BusinessChainIntegrationMatrix.md` | BC-01~BC-17 均已在 SSOT 中记录入口、当前最高实证层、代码/运行证据、冻结结论、缺口和后继任务；不再散落在各子系统 TODO 中 |
| FULLINT-TODO-002 | NotStarted | 冻结 installed-package 与 build-tree 证据分层复核表 | §2；packaging TODO；worklog #619/#620 | local package、qemu、release-preflight、LLM origin、knowledge gap | L2 | `docs/todos/integration/deliverables/FULLINT-TODO-002-package证据分层复核.md` | `PackageEvidenceLayer` 文档表 | process：命令与 owner 一致性 | `rg -n "dasall_gate_int_10|dasall_packaging_preflight_tests|pkg_smoke_install|validate_gate_int_10_installed_package_qemu|autopkgtest|llm.origin" docs/todos/packaging docs/worklog/DASALL_开发执行记录.md scripts/packaging` | 001 | 无 | 无 | package evidence layer deliverable | local smoke、qemu runner、DeepSeek LLM 主链、knowledge installed-package gap 口径一致 |
| FULLINT-TODO-003 | NotStarted | 建立 runtime/cognition/memory/llm 主链证据包 | Runtime §6.7；Cognition §6.7；Memory §6.8；LLM §6.7；Gate-INT-03 | default unary、context assemble、stage LLM、response build、writeback 关键断言 | L2 | `tests/integration/agent_loop/`；`tests/integration/cognition/`；必要时新增 deliverable | `AgentResult`、`ContextPacket`、`ActionDecision`、`LLMManagerResult`、`WritebackResult` | `RuntimeUnaryIntegrationTest`、`CognitionRuntimeIntegrationTest`、Memory context/writeback focused tests 组合执行 | `Build_CMakeTools(buildTargets=["dasall_gate_int_03","dasall_gate_int_06"])` + `ctest --test-dir build/vscode-linux-ninja -R "RuntimeUnaryIntegrationTest|CognitionRuntimeIntegrationTest|MemoryContextAssembleIntegrationTest|MemoryWritebackIntegrationTest" --output-on-failure` | 001 | 无 | 主链 focused tests 当前可发现且断言项覆盖上下文、LLM、终态输出 | 主链证据包 deliverable | 能证明 unary 主链不是空响应，不把 cognition/llm/memory 任一段静默降级为成功 |
| FULLINT-TODO-004 | NotStarted | 校准 profile enablement 与 multi_agent 声明边界 | MultiAgent §6.8/§6.9；Profiles §6.7；集成修复专项 §13.2 | `desktop_full` / `cloud_full` 的 `enabled_modules.multi_agent`、RuntimePolicySnapshot 投影、Null/Real coordinator owner | L2 | `profiles/*/runtime_policy.yaml`；`profiles/include/RuntimePolicySnapshot.h`；`multi_agent/include/`；`runtime/src/AgentOrchestrator.cpp` | `enabled_modules.multi_agent`、`NullMultiAgentCoordinator`、`MultiAgentCoordinator` 装配策略 | unit / integration：profile matrix、禁用态 Gate、runtime 装配不误报 | `Build_CMakeTools(buildTargets=["dasall_profile_matrix_consistency_unit_test","dasall_runtime_profile_compatibility_integration_test"])`，新增后补 multi_agent focused target | 001 | FULLINT-BLK-004 | profile 声明、runtime snapshot 和 coordinator 装配策略一致 | profile/multi_agent 校准补丁或专项拆分文档 | 在未实现 RealCoordinator 前，禁用态或 NullCoordinator 有明确 Gate，文档不再把 multi_agent 写成 GA ready |

### 6.2 骨架接口 / 验证入口补齐任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FULLINT-TODO-005 | NotStarted | 新增全量业务链 discoverability verifier 清单 | `tests/CMakeLists.txt`；SystemIntegrationGateMatrix | Gate discoverability 与 business-chain labels | L2 | `tests/VerifySystemGateDiscoverability.cmake`；`tests/CMakeLists.txt`；可选 `tests/FullBusinessChainGate.cmake` | `DASALL_FULL_BUSINESS_CHAIN_EXPECTED_TESTS` | ctest discoverability：Gate-INT-08/09/10、knowledge/memory/tools/services/llm/profile 代表测试 | `Build_CMakeTools(buildTargets=["dasall_gate_int_08","dasall_gate_int_09","dasall_gate_int_10","dasall_packaging_preflight_tests"]) && ctest --test-dir build/vscode-linux-ninja -N` | 001、002 | 无 | 无 | full business chain discoverability target 草案 | 每条业务链至少有一个可发现的 focused test 或显式标记为 missing gate |
| FULLINT-TODO-006 | NotStarted | 扩展 Access ingress 业务链验证矩阵 | Access §6.7、§6.14、§6.20；Gate-INT-08 | CLI/daemon、HTTP/gateway、policy fail-closed、health readiness、profile/contracts guard | L2 | `tests/integration/access/`；`docs/todos/access/DASALL_access子系统专项TODO.md` 回链 | `AccessGatewayPipeline`、`RuntimeBridge`、`AsyncTaskRegistry`、`ResultPublisher` | `CliDaemonSubmitIntegrationTest`、`HttpGatewaySubmitIntegrationTest`、`AccessAsyncReceiptQueryCancelIntegrationTest`、policy/readiness tests | `Build_CMakeTools(buildTargets=["dasall_gate_int_08"])` | 001 | 无 | Access v1 focused gate 可稳定复跑 | Access ingress 证据矩阵 | Access 入口链按 CLI/HTTP/async/security 拆分记录，mock pipeline、ping liveness 不再混入 release 证据 |
| FULLINT-TODO-007 | NotStarted | 加严 daemon/gateway readiness 投影与 app-binary no-stub 断言 | BinaryEntrypointReadinessV1；RuntimeAppCompositionV1；Gate-INT-10 | `accepted`、`stub-ready`、`degraded-ready`、`default-ready` 到 health/readiness/binary smoke 的投影 | L2 | `apps/daemon/src/main.cpp`；`apps/gateway/src/main.cpp`；`runtime/include/AgentTypes.h`；`tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`；`GatewayBinaryUnarySmokeTest.cpp` | `AgentInitResult::readiness_level()`、health/readiness payload、`runtime readiness=` | daemon/gateway health readiness integration、binary smoke no-stub assertion | `Build_CMakeTools(buildTargets=["dasall_runtime_agent_init_result_readiness_unit_test","dasall_gate_int_06","dasall_gate_int_10"])` | 001 | FULLINT-BLK-003 | health/readiness owner 语义冻结 | readiness 投影补丁和测试 | 对外 READY 不再由 `accepted` 偷换；stub-ready 必须 fail，degraded-ready 必须显式标识 |
| FULLINT-TODO-008 | NotStarted | 补 gateway shutdown exit-code 断言 | 集成修复专项 §13.4 | GatewayBinaryUnarySmokeTest 读取 exit code 但未断言为 0 | L3 | `tests/integration/access/GatewayBinaryUnarySmokeTest.cpp` | `gateway_exit_code` assertion | GatewayBinaryUnarySmokeTest | `Build_CMakeTools(buildTargets=["dasall_access_gateway_binary_unary_smoke_integration_test","dasall_gate_int_10"])` | 007 | FULLINT-BLK-005 | gateway smoke 可稳定通过 | test 补丁 | submit 成功但 gateway shutdown 非 0 时，测试必须失败并输出 artifact |
| FULLINT-TODO-009 | NotStarted | 扩展 startup diagnostics failure stage 覆盖 | Infra §6.7/§6.8；集成修复专项 §13.4 | runtime dependency composition、runtime init、AccessGateway init、listen/bind 失败分支 | L2 | `apps/daemon/src/main.cpp`；`apps/gateway/src/main.cpp`；`tests/integration/access/*StartupDiagnosticsTest.cpp` | startup failure reporter、`stage`、`error_code`、`trace_id`、路径字段 | daemon/gateway startup diagnostics focused tests | `Build_CMakeTools(buildTargets=["dasall_access_daemon_startup_diagnostics_test","dasall_access_gateway_startup_diagnostics_test"])` | 001 | FULLINT-BLK-005 | 新 failure fixture 能稳定触发目标 stage | diagnostics tests 与 artifact | app-binary 失败日志覆盖真实 composition/init/listen/bind，不只覆盖 config/profile load |
| FULLINT-TODO-010 | NotStarted | 梳理 packaging preflight 与 Gate-INT-10 命令关系 | INTFIX §13.4；SystemIntegrationGateMatrix | release-preflight dual target | L3 | `docs/ssot/SystemIntegrationGateMatrix.md`、`scripts/packaging/README.md`、本专项 deliverable | `dasall_gate_int_10`、`dasall_packaging_preflight_tests` | process：命令说明一致性 | `rg -n "dasall_gate_int_10|dasall_packaging_preflight_tests|gateway binary|daemon/CLI|release-preflight" docs/ssot/SystemIntegrationGateMatrix.md scripts/packaging/README.md docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md` | 002 | 无 | 无 | 命令关系说明补丁 | 单跑 `dasall_packaging_preflight_tests` 不再被误读为 gateway binary ready |

### 6.3 配置策略实现 / 业务链执行验证任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FULLINT-TODO-011 | NotStarted | 验证 async / cancel / replay / recovery 因果链 | Access async 时序；Runtime recovery/resume；Memory writeback | async receipt ownership、cancel 转发、replay cache、checkpoint/resume/writeback continuity | L2 | `tests/integration/access/`；`tests/integration/agent_loop/`；`tests/integration/memory/` | `AsyncTaskReceipt`、`CancellationToken`、`Checkpoint`、`WritebackResult` | Access async tests + runtime resume/failure + memory writeback | `Build_CMakeTools(buildTargets=["dasall_gate_int_08"])` + `ctest --test-dir build/vscode-linux-ninja -R "AccessAsyncReceiptQueryCancelIntegrationTest|MemoryWritebackIntegrationTest|MemoryFailureInjectionTest" --output-on-failure` | 006 | 无 | async focused tests 与 recovery/memory tests 可组合复跑 | async/recovery 证据包 | receipt/cancel/replay 不只返回 envelope，还保留 ownership、trace/ref、失败语义和写回连续性 |
| FULLINT-TODO-012 | NotStarted | 执行知识/记忆/LLM/工具服务跨链回归 | Knowledge/Memory/LLM/Tools/Services 详设；Gate-INT-04/06/07 | evidence -> context -> prompt -> provider -> tool/service -> observation digest | L2 | `tests/integration/knowledge/`、`memory/`、`llm/`、`tools/`、`services/`；必要时新增聚合脚本 | `EvidenceBundle`、`ContextPacket`、`PromptComposeResult`、`ToolInvocationEnvelope`、`ObservationDigest` | Gate-INT-04、06、07 + subsystem integration focused matrix | `Build_CMakeTools(buildTargets=["dasall_gate_int_04","dasall_gate_int_06","dasall_gate_int_07"])` + focused `ctest` matrix | 003 | 无 | 子系统 focused gates 当前绿态 | cross-chain 回归矩阵 | 能证明知识证据、上下文、LLM、工具服务语义没有在跨链投影中丢失或被错误降级 |
| FULLINT-TODO-013 | NotStarted | 执行 installed-package 控制面 + 主功能矩阵 | Packaging TODO；worklog #620 | fresh install、explicit start、ping/readiness、run、status/cancel、diag、LLM origin、knowledge/tools 缺口断言 | L4 | `scripts/packaging/pkg_smoke_install.sh`；`debian/tests/pkg-smoke-local-control-plane` | package smoke assertions | installed-package local smoke | `dpkg-buildpackage -us -uc -b && bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`；人工生产 LLM 验证：`sudo dasall run '{"prompt":"请用LLM回答：1+1等于几？只给出简短答案。"}' --json --timeout-ms 120000` | 002、007 | FULLINT-BLK-001、002、003 | 本地 rootful 条件、secret URI、外部网络可用 | package smoke 日志与矩阵 | `run` 必须返回 completed 与 `llm.origin`；`agent.dataset` 为失败信号；knowledge 缺口必须显式记录 |
| FULLINT-TODO-014 | NotStarted | 新增 knowledge installed-package 正向入口验证方案 | worklog #620；Knowledge TODO | retrieve/refresh/health installed surface | L2 | 候选：`apps/cli` knowledge subcommand、daemon diag route、或 package smoke hook；`scripts/packaging/pkg_smoke_install.sh` | `IKnowledgeService::retrieve()`、`request_refresh()`、`get_health()` 或等价 CLI/daemon facade | package smoke：knowledge retrieve/refresh/health positive path | `Build_CMakeTools(buildTargets=["dasall_gate_int_04"]) && dpkg-buildpackage -us -uc -b && bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | 002、013 | FULLINT-BLK-002 | 冻结安装态 knowledge 正向入口 owner；或 daemon live composition 接入真实 `IKnowledgeService` | knowledge installed-package 方案与实现 | 安装态能独立证明 knowledge retrieve/refresh/health，不再只靠 build-tree evidence gate |
| FULLINT-TODO-015 | NotStarted | 复验 memory context/writeback installed-package 持久化风险 | Memory TODO；package smoke | memory store / writeback / maintenance | L2 | `scripts/packaging/pkg_smoke_install.sh`；可选 daemon diag memory route | `MemoryContextAssembleIntegrationTest`、`MemoryWritebackIntegrationTest`、installed state dir | build-tree memory gates + package lifecycle smoke | `Build_CMakeTools(buildTargets=["dasall_memory_context_assemble_integration_test","dasall_memory_writeback_integration_test"]) && dpkg-buildpackage -us -uc -b && bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | 013 | 无 | package smoke 支持 memory state path 检查或明确保持 build-tree only | memory installed-package 风险记录 | memory writeback 不被假定为 package-ready；若只验证 build-tree，文档必须清楚标注 |
| FULLINT-TODO-016 | NotStarted | 收敛 tools/services runtime production caller 边界 | Tools / Services TODO | ToolManager -> Services -> Observation | L2 | `runtime/src/AgentOrchestrator.cpp`、`tools/src/`、`services/src/`、`tests/integration/tools` | runtime production caller adapter、`ToolInvocationEnvelope` | `ToolServicesSmokeIntegrationTest`、runtime unary with tool path fixture | `Build_CMakeTools(buildTargets=["dasall_gate_int_07","dasall_runtime_unary_integration_test"]) && ctest --test-dir build/vscode-linux-ninja -R "ToolServicesSmokeIntegrationTest|RuntimeUnaryIntegrationTest" --output-on-failure` | 012 | 无 | runtime tool path fixture 可用 | tools/services runtime caller verification | runtime tool path 仍经 registry/validator/policy/route/services/projection，不绕过治理 |
| FULLINT-TODO-017 | NotStarted | 扩展 Access / Infra release hardening 负路径 | Access security；Infra diagnostics；startup diagnostics | policy backend unavailable、diagnostics denied、audit required、listen/bind fail-closed | L2 | `tests/integration/access/`；`tests/integration/infra/` | `AccessPolicyGate`、`DiagnosticsService`、audit/error fields | security/diagnostics focused tests | `Build_CMakeTools(buildTargets=["dasall_gate_int_05","dasall_gate_int_08"])` | 006、009 | 无 | 负路径 fixture 可稳定复现 | hardening 证据包 | fail-closed 与 diagnostics retained snapshot 同时可验证，不用 liveness 替代安全结论 |
| FULLINT-TODO-018 | NotStarted | 建立 multi_agent Null/Real coordinator 与禁用态 Gate 路线 | MultiAgent §6.8/6.9；Tools §6.8；Runtime recovery | MultiAgent Null/Real coordinator、Observation 折叠、Tool compensation hints、RecoveryManager 裁定 | L2 | `multi_agent/`、`runtime/`、`tools/`、`tests/integration/`、新增专项 TODO | `MultiAgentExecutionReport`、`Observation` 折叠、`compensation_hints`、`RecoveryOutcome` | 新增 multi_agent focused tests 与 recovery/tool failure tests | 设计阶段：`rg -n "MultiAgentExecutionReport|NullCoordinator|RecoveryOutcome|compensation_hints" multi_agent runtime tools tests docs`；实现后补 CMake target | 004、011、012 | FULLINT-BLK-004 | multi_agent owner 与最小 Gate 冻结 | multi_agent/recovery 增量专项 | 在声明 profile-ready 前，有禁用态 Gate、Null/Real 装配证据和 Observation 折叠验证 |

### 6.4 测试集成门禁 / 证据收口任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FULLINT-TODO-019 | NotStarted | 在 release runner 执行 installed-package qemu / lintian / LLM 串联 gate | worklog #619/#620；packaging TODO | Gate-INT-10 -> package build -> metadata -> qemu autopkgtest -> lintian -> installed LLM smoke | L5 | `scripts/packaging/validate_gate_int_10_installed_package_qemu.sh`；release CI 配置；packaging docs | `.changes` artifact、autopkgtest logs、lintian logs、secret injection record | release runner end-to-end gate | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>`；`lintian ../dasall_0.1.0-1_amd64.changes`；installed `dasall run` LLM origin smoke | 013 | FULLINT-BLK-001 | release runner、qemu image、virt args、secret/network 均具备 | release evidence bundle | 只有该任务通过后，才允许把当前版本升级为 production installed-package release-ready 候选 |
| FULLINT-TODO-020 | NotStarted | 固化全量业务链 one-shot build-tree 验收入口 | 本文 §4；TODO 格式标准 | full business-chain gate | L2 | `tests/CMakeLists.txt`、可选 `scripts/ci/validate_full_business_chains.sh` | `dasall_full_business_chain_preflight` target 或脚本 | focused gates + app-binary + package metadata discoverability | `Build_CMakeTools(buildTargets=["dasall_gate_int_08","dasall_gate_int_09","dasall_gate_int_10","dasall_packaging_preflight_tests"]) && python3 scripts/packaging/validate_autopkgtest_metadata.py` | 005、012 | 无 | 无 | one-shot preflight target / script | 一条命令能验证 build-tree 全业务链预检，不吞并 installed-package qemu owner |
| FULLINT-TODO-021 | NotStarted | 回写全量业务链验证结果与下一轮调试清单 | TODO 格式标准；worklog 规则；SystemIntegrationGateMatrix | 本专项 TODO、deliverable、worklog、Gate matrix 后继关系 | L2 | `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md`；`docs/todos/integration/deliverables/`；`docs/worklog/DASALL_开发执行记录.md` | business-chain evidence table、blocker status、residual risk | process：文档一致性检查 | `rg -n "FULLINT-TODO|FULLINT-BLK|BC-0|记录 #" docs/todos/integration docs/worklog/DASALL_开发执行记录.md docs/ssot/SystemIntegrationGateMatrix.md` | 003~020 | 无 | 每个业务链都有最新执行结论 | deliverable + worklog 记录 | 已执行链路、未执行阻塞、失败根因、下一轮调试任务全部有证据回写 |
| FULLINT-TODO-022 | NotStarted | 形成全量 DASALL 功能集成验证报告 | 用户请求；本专项总收口 | 业务链逐条 Pass / Partial / Blocked | L2 | `docs/todos/integration/deliverables/FULLINT-TODO-022-全量DASALL功能集成验证报告.md` | `BusinessChainVerificationStatus` 文档表 | process + 当轮 gate evidence | `rg -n "BC-01|BC-17|Pass|Partial|Blocked|FULLINT-TODO-022" docs/todos/integration/deliverables/FULLINT-TODO-022-全量DASALL功能集成验证报告.md` | 021 | 无 | 无 | 全量验证报告 | 用户可按报告逐条业务链执行/复验；每条链的状态、证据和阻塞项都能二值追踪 |

## 7. 验收命令矩阵

### 7.1 build-tree / focused integration

```text
Build_CMakeTools(buildTargets=["dasall_gate_int_03","dasall_gate_int_04","dasall_gate_int_05","dasall_gate_int_06","dasall_gate_int_07","dasall_gate_int_08","dasall_gate_int_09"])
```

### 7.2 app-binary / release-preflight

```text
Build_CMakeTools(buildTargets=["dasall_gate_int_10","dasall_packaging_preflight_tests"])
Build_CMakeTools(buildTargets=["dasall_access_daemon_startup_diagnostics_test","dasall_access_gateway_startup_diagnostics_test"])
```

### 7.3 installed-package local smoke

```bash
dpkg-buildpackage -us -uc -b
bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check
sudo dasall run '{"prompt":"请用LLM回答：1+1等于几？只给出简短答案。"}' --json --timeout-ms 120000
```

通过条件：`disposition=completed`、`task_completed=true`、`llm.origin=deepseek-prod/deepseek-reasoner` 或等价 provider/model origin；不得出现把 `agent.dataset` 当作 `run` 成功语义。

### 7.4 release runner qemu / autopkgtest

```bash
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>
lintian ../dasall_0.1.0-1_amd64.changes
```

说明：`<image-or-config>` 必须由 release runner 提供；本仓库脚本不下载、不创建、不缓存 testbed image。

### 7.5 文档一致性

```bash
rg -n "BC-0|FULLINT-TODO|FULLINT-BLK|Gate-INT-|release-preflight|installed-package" docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md docs/ssot/SystemIntegrationGateMatrix.md docs/worklog/DASALL_开发执行记录.md
```

## 8. 质量门与完成判定

| Gate | 完成标准 | 证据位置 |
|---|---|---|
| FULLINT-GATE-01 业务链矩阵门 | BC-01~BC-17 均完成证据矩阵冻结 | 本文件 §3、FULLINT-TODO-001 deliverable |
| FULLINT-GATE-02 build-tree 全链预检门 | Gate-INT-08/09/10、packaging preflight、startup diagnostics 当轮通过 | worklog + FULLINT-TODO-011/012/020 deliverables |
| FULLINT-GATE-03 installed-package local 主功能门 | dpkg build + package smoke + LLM origin 当轮通过 | worklog + FULLINT-TODO-013 deliverable |
| FULLINT-GATE-04 缺口不外推门 | knowledge/memory/multi_agent/qemu/readiness 缺口均有 owner 和阻塞项 | 本文件 §5、FULLINT-TODO-014~019 deliverables |
| FULLINT-GATE-05 最终报告门 | 每条业务链状态为 Pass / Partial / Blocked，且带命令证据 | FULLINT-TODO-022 deliverable |

当且仅当以下条件全部满足，才允许把本专项写为“全量业务链集成验证闭环”：

1. BC-01 ~ BC-17 每条业务链都有最新执行结论、证据层级和 artifact 路径。
2. `Gate-INT-03~10`、package local smoke、qemu `autopkgtest`、lintian 与 installed LLM smoke 的结果分层记录清楚。
3. daemon/gateway readiness 不再把 `accepted` 或 `stub-ready` 外推为 `default-ready`。
4. installed-package `run` 成功语义以 LLM provider/model origin 为准，不以 dataset fallback 为准。
5. Knowledge installed-package 缺口要么被正向入口补齐，要么在 release 结论中显式标为 not-ready。
6. multi_agent 若仍未实现 Null/Real coordinator 与禁用态 Gate，则不能被 profile 文案宣称为 GA runtime-ready。
7. 所有新发现失败都进入 worklog 和下一轮原子任务，不以“局部绿”覆盖“系统红”。

## 9. 后续执行建议

1. 第一轮执行：FULLINT-TODO-003、006、007、012，先证明 build-tree 主链和入口链没有回退。
2. 第二轮执行：FULLINT-TODO-005、013、014，补 installed-package 下 Knowledge 与 LLM 主功能矩阵。
3. 第三轮执行：FULLINT-TODO-008、009、017，强化 app-binary diagnostics 与 gateway release confidence。
4. 第四轮执行：FULLINT-TODO-004、018，收敛 multi_agent/profile enablement 与恢复/side-effect 链路。
5. Release 前执行：FULLINT-TODO-019、021、022，在 release runner 归档 qemu / lintian / LLM smoke / worklog 证据。

## 10. 当前本轮交付说明

本轮交付内容为全量业务链验证规划与专项 TODO 文档，不修改产品代码、不执行 Gate，也不将任何未执行任务标记为 Done。下一轮可从 FULLINT-TODO-003 或 FULLINT-TODO-007 开始进入 Build / Debug / Gate 实施。
