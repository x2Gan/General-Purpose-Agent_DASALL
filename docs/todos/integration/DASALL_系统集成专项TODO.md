# DASALL 系统集成专项 TODO

最近更新时间：2026-05-06
阶段：Integration Review -> Special TODO
适用范围：runtime/、cognition/、contracts/、memory/、knowledge/、infra/、tools/、services/、apps/、tests/、docs/ssot/、docs/worklog/
当前结论：DASALL 已具备可运行的入口链、工具链和若干子系统 smoke，但默认 single-agent unary 主链与 Access v1 production ingress 仍未达成 integration ready。本专项 TODO 的目标不是继续扩面，而是围绕默认 unary 主链、Access production path、结构化证据共享、diagnostics retained snapshot、required/optional ports 语义和系统级 Gate 做定点收敛，并把 streaming/shared admission、optional backend、late-scope 运维能力统一归入 blocker/OQ 跟踪。

## 文档头

输入依据：

1. docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md
2. docs/architecture/DASALL_全局子系统详细设计评审报告-2026-04-15.md
3. docs/architecture/DASALL_Agent_architecture.md
4. docs/architecture/DASALL_Engineering_Blueprint.md
5. docs/plans/DASALL_工程落地实现步骤指引.md
6. docs/development/DASALL_工程协作与编码规范.md
7. docs/architecture/DASALL_runtime子系统详细设计.md
8. docs/architecture/DASALL_cognition子系统详细设计.md
9. docs/architecture/DASALL_memory子系统详细设计.md
10. docs/architecture/DASALL_knowledge子系统详细设计.md
11. docs/architecture/DASALL_llm子系统详细设计.md
12. docs/architecture/DASALL_tools子系统详细设计.md
13. docs/architecture/DASALL_capability_services子系统详细设计.md
14. docs/architecture/DASALL_infra_diagnostics模块详细设计.md
15. docs/architecture/DASALL_access子系统详细设计.md
16. docs/architecture/platform_linux_detailed_design.md
17. docs/todos/runtime/DASALL_runtime子系统专项TODO.md
18. docs/todos/cognition/DASALL_cognition子系统专项TODO.md
19. docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md
20. docs/todos/tools/DASALL_tools子系统专项TODO.md
21. docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md
22. docs/todos/profiles/DASALL_profiles子系统专项TODO.md
23. docs/ssot/CrossModuleDataProjectionMatrix.md
24. access/include/AccessTypes.h
25. access/src/RuntimeBridge.cpp
26. apps/daemon/src/main.cpp
27. apps/cli/src/main.cpp
28. runtime/include/RuntimeDependencySet.h
29. runtime/src/AgentFacade.cpp
30. runtime/src/AgentOrchestrator.cpp
31. cognition/src/response/ResponseBuilder.cpp
32. cognition/src/llm/CognitionLlmBridge.cpp
33. memory/include/context/MemoryContextRequest.h
34. memory/src/context/ContextOrchestrator.cpp
35. contracts/include/context/ContextPacket.h
36. knowledge/include/KnowledgeTypes.h
37. infra/src/diagnostics/DiagnosticsServiceFacade.cpp
38. tools/src/execution/BuiltinExecutorLane.cpp
39. tools/src/bridge/ToolServiceBridge.cpp
40. tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp
41. tests/integration/cognition/CognitionRuntimeIntegrationTest.cpp
42. tests/integration/infra/InfraDiagnosticsSmokeTest.cpp
43. tests/integration/knowledge/KnowledgeRetrievalSmokeTest.cpp
44. docs/worklog/DASALL_开发执行记录.md
45. docs/todos/access/DASALL_access子系统专项TODO.md
46. docs/todos/llm/DASALL_llm子系统专项TODO.md
47. docs/todos/memory/DASALL_memory子系统专项TODO.md
48. docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md
49. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md
50. docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md
51. docs/todos/cli/DASALL_cli本地控制面专项TODO.md
52. docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

行业补强参照：

1. Martin Fowler：Consumer-Driven Contracts、Practical Test Pyramid。
2. Temporal / Cadence：durable execution、replay-safe evolution、workflow compatibility gate。
3. Azure Architecture Center：External Configuration Store、Retry、Compensating Transaction。
4. OpenTelemetry：trace/span cause chain、事件结构化字段对齐。
5. 生产级 RAG 实践：evidence provenance、freshness、anchor、citation preservation。

编制原则：

1. 默认采用 Design -> Build 双轨；不跳过设计收敛直接写 Build。
2. 每个原子任务必须同时给出代码目标、测试目标、验收命令。
3. single-agent unary 主链优先，streaming 与 multi_agent 不作为本专项前置完成条件。
4. runtime-local fixture gate、subsystem smoke 与 true cross-module integration gate 必须分层记录、分层验收。
5. shared contracts 的新增面只允许 additive + optional，不允许把 knowledge / tools / access 的内部 supporting objects 整体抬升。
6. 若任务依赖产品语义未冻结、contracts admission 未完成或测试拓扑未稳，必须进入 Blocked 或显式标注解阻条件。
7. 任务完成后必须回写交付物路径、命令证据、结果摘要与后继任务。

## 1. 概述与目标

专项目标：

1. 将 DASALL 从“若干集成切片可运行”推进到“默认 single-agent unary 主链具备系统级 integration ready 证据”。
2. 修复 runtime/cognition 最终响应合同漂移，使主链最终输出可预测、可测试、可审计。
3. 为 knowledge -> runtime -> memory/cognition 补齐最小结构化 evidence projection，避免主链证据语义被过度扁平化。
4. 修复 infra diagnostics retained snapshot round-trip，使系统具备稳定的诊断回读能力。
5. 明确 knowledge / llm 对默认 unary 路径的 required/optional 语义，并把该语义落实到 runtime readiness gate 与 degraded path。
6. 把 tools/services 的状态码语义、系统级 Gate、文档事实与 worklog 证据闭环成一套可持续执行的集成收口机制。
7. 吸收各子系统专项 TODO 中仍具系统集成意义的 Access / health 残项，并把不应进入当前 unary 主线的 deferred 能力显式降级为 blocker / OQ。

纳入范围：

1. runtime、cognition、contracts、memory、knowledge、llm、infra diagnostics、tools、services 的系统集成接缝。
2. access、daemon、cli、platform_linux 已通过的正向链路作为回归保护基线。
3. docs/ssot、docs/todos、docs/worklog 中与系统集成直接相关的设计、门禁、证据资产。
4. tests/integration、tests/contract、tests/unit 中与 unary 主链、structured evidence、diagnostics、result semantics 直接相关的测试拓扑与 Gate。

不纳入范围：

1. multi_agent 正式闭环。
2. streaming 主链与 streaming supporting objects admission。
3. 新的 provider 接入、新的 access 协议扩展、新的 tool lane 功能面扩张。
4. 与本专项无直接关系的仓库级遗留问题，除非其会阻断 targeted acceptance command。
5. `sqlite-vss` concrete backend、真实 KMS backend、OTLP exporter 这类 optional backend 扩张。
6. `diag artifact_ref`、delegate hint、schema registry、checkpoint 大版本迁移、人工干预 API 这类 late-scope 演进项。

## 2. 当前状态

### 3.1 当前代码与测试状态摘要

| 维度 | 当前状态 | 结论 |
|---|---|---|
| 入口链 | `cli -> daemon -> access -> runtime` 已可运行，相关 smoke 为绿 | 可作为系统基座继续复用 |
| access production path | `AccessGateway` 当前允许空 `submit_pipeline_` 进入 `Ready`，`RuntimeDispatchRequest` 也尚未显式承载 `AgentRequest` | Access v1 production path 未 ready，必须进入系统主线 |
| runtime 主链 | `AgentOrchestrator` 已真实调用 memory、cognition、tools，并构造 `AgentResult` | 非 placeholder，但主 Gate 仍为红 |
| knowledge 子系统 | retrieval 主流程与 smoke 为绿，内部具备 richer evidence model | 子系统 ready，主链 projection 不足 |
| llm 子系统 | manager / adapter 主路径与 smoke 为绿 | 子系统 ready，但是否默认进入 unary 主链未收口 |
| streaming / shared admission | `llm/include/stream/StreamSessionRef.h` 仍保持 module-local，`llm/src/stream/` 尚无 concrete lifecycle 实现 | 明确延后，不进入当前 unary Gate |
| infra diagnostics | `execute -> get_snapshot -> export` 路径存在实现 | retained snapshot 回读当前失败 |
| health/watchdog | `platform/include/ITimer.h` 与 `platform/include/linux/PosixTimerProvider.h` 已存在，但 health event publish seam 仍未冻结 | HLT-BLK-001 需重定级，HLT-BLK-002 仍为有效残项 |
| optional backends | memory 仅有 `UnavailableVectorMemoryIndexAdapter`，secret KMS 与 metrics OTLP 仍无冻结依赖链 | 不阻断 unary 主线，但必须在集成 TODO 中显式降级跟踪 |
| tools -> services | 桥接真实存在，smoke 为绿 | 结果码语义仍需统一 |
| 主红灯 | `RuntimeUnaryIntegrationTest`、`CognitionRuntimeIntegrationTest`、`InfraDiagnosticsSmokeTest` | 为本专项最核心收口对象 |

### 3.2 当前系统级判断

1. architecture ready 结论仍成立，但 integration ready 结论尚不能给出。
2. 现在最大的浪费不是“缺少更多设计对象”，而是“主链已接通后仍未优先处理明确红灯”。
3. Access TODO 中 034 ~ 051 与 ACC-BLK-008 / 010 反映的是系统 ingress 主链仍未闭合，必须提升为当前主线任务，而不是继续留在子系统局部清单里。
4. HLT-TODO-009 / 012 / 014 与 RT-OQ-06 仍具系统级意义，但其中 platform timer seam 已在代码中存在，当前更适合重定级为“post-unary 可执行残项”，而不是继续作为纯设计 blocker 挂起。
5. 因此 TODO 应围绕少数关键 Gate 收口，同时把 deferred residual 按“当前主线 / 后置 blocker / late-scope OQ”三类分账，而不是平均分配到所有子系统功能扩张上。

### 3.3 子系统残项吸收结论

1. Access 残项是本轮唯一必须提升为系统主线的专项 TODO 集合：当前代码已验证 `apps/gateway/src/main.cpp` 仍默认构造空 `AccessGateway`，`access/src/AccessGateway.cpp` 也仍允许空 pipeline 进入 `Ready`，不能继续用局部 smoke 代替 production ingress 证据。
2. diagnostics retained snapshot 尽管组件专项 TODO 已收口，但 `tests/integration/infra/InfraDiagnosticsSmokeTest.cpp` 仍红，因此继续保留为系统级 Gate，而不是回退到 infra 局部跟踪。
3. health/watchdog 残项需要重新定级：`ITimer` / `PosixTimerProvider` 已落盘，`HealthConfigPolicy`、`ProbeScheduler` 和 cadence 校准可进入 post-unary 执行队列；真正仍阻塞的是 event publish 最小接口未冻结。
4. LLM streaming/shared admission、Access stream attach/reconnect/replay cursor、CLI diag artifact_ref、metrics OTLP、secret KMS、memory `sqlite-vss` concrete backend、cognition delegate hint / schema registry、runtime checkpoint major upgrade / manual intervention API 不进入当前 unary 验收主线，但必须显式记录在 blocker / OQ 表中，避免被误写成“已完成”或“默认纳入下一轮 Build”。

## 3. 约束条件

### 4.1 约束清单

| ID | 来源 | 约束 | 对 TODO 的直接影响 |
|---|---|---|---|
| INT-TC001 | ADR-006 / ADR-007 / ADR-008 | Memory 保持上下文装配权，Cognition 只提供建议，Runtime 保持唯一主控 | 不新增第二上下文中心、第二恢复执行中心、第二主控平面 |
| INT-TC002 | 集成评审 GINT-01 | 默认 unary 主链未绿前，不扩 streaming / multi_agent / 新 lane 功能 | P0 先收敛主链红灯 |
| INT-TC003 | 集成评审 GINT-02 | knowledge / llm 对默认 unary 路径的 required/optional 语义必须先冻结，再做 Build | 必须先完成 port matrix 与 degraded 语义设计任务 |
| INT-TC004 | 集成评审 GINT-03 | evidence projection 演进只能 additive + optional | contracts 任务不得引入 knowledge 内部对象泄漏 |
| INT-TC005 | 集成评审 GINT-04 | diagnostics retained snapshot round-trip 是系统级能力，不是 infra 私有细节 | 必须单列 contract、实现、Gate 任务 |
| INT-TC006 | 集成评审 GINT-05 | success / error / ResultCode 三元语义必须一致 | tools/services 必须补 contract gate |
| INT-TC007 | 集成评审 §7.1 / §7.2 | RuntimePolicySnapshot consumer matrix 与 Recovery Context 边界须提升为系统 SSOT | 先补系统级设计资产，再落 runtime 使用点 |
| INT-TC008 | TODO 格式标准 | 每个任务必须含代码目标、测试目标、验收命令 | 不允许只有描述性任务 |
| INT-TC009 | 测试治理基线 | fixture gate、subsystem smoke、true integration gate 必须分开 | `RuntimeUnaryFixtureIntegrationTest` 不能冒充系统 gate |
| INT-TC010 | build-validation 记忆 | 优先使用显式 `cmake -S . -B build-ci -G "Unix Makefiles"` + targeted `ctest` 命令 | 统一验收命令避免依赖 IDE 状态 |
| INT-TC011 | Access 代码核对 | mock pipeline、空 `AccessGateway` 和 ping/liveness 证据不能冒充 Access v1 production path ready | Access 残项必须提升为系统级任务与 Gate |
| INT-TC012 | 专项 TODO 残项分账 | streaming/shared admission、optional backend、late-scope 运维能力必须降为 blocker / OQ，不得塞回当前 unary P0 列表 | 集成 TODO 既要完整吸收残项，也要保持主线聚焦 |

### 4.2 当前阻断与可执行性判断

1. 当前并不存在“完全无法推进”的总阻塞；最合理路线是先补 6.1 设计与 SSOT，再推进 6.2 / 6.3 Build。
2. 真正会阻断后续 Build 的，不只是 unary mode、response contract、structured evidence projection 与 diagnostics contract 未冻结，还包括 Access v1 production path 当前仍停留在 mock/empty pipeline 可工作的危险状态。
3. health 残项已具备部分可执行前提：`ITimer` seam 已落盘，`ProbeScheduler` 不应继续无限期停留在文档 blocker；真正需要继续作为 blocker 跟踪的是 event publish 最小接口与 system cadence 口径。

## 4. Design Track 映射

| 评审发现 / 设计缺口 | 设计收口动作 | 对应任务 | 说明 |
|---|---|---|---|
| GINT-01 默认 unary 最终响应合同漂移 | 单列 UnaryResponseContract 与 projection rule | INT-TODO-002、INT-TODO-012、INT-TODO-018 | 先冻结语义，再修主链与 Gate |
| GINT-02 knowledge / llm required vs optional 未收口 | 建立 SingleAgentRuntimePortMatrix | INT-TODO-001、INT-TODO-010、INT-TODO-014、INT-TODO-021 | 先定产品语义，再定 readiness 规则 |
| GINT-03 evidence projection 过薄 | 单列 RetrievalEvidenceProjectionV1 | INT-TODO-003、INT-TODO-008、INT-TODO-009、INT-TODO-013、INT-TODO-019 | 只推进最小 shared projection |
| GINT-04 diagnostics round-trip 回归 | 单列 retained snapshot contract 与 topology | INT-TODO-004、INT-TODO-011、INT-TODO-015、INT-TODO-020 | contract 与 Gate 同时收口 |
| GINT-05 tools/services 结果码歧义 | 建立 status semantic rule | INT-TODO-016、INT-TODO-022 | 先统一语义，再谈更多治理 |
| 配置/恢复系统 SSOT 缺失 | 提升 policy consumer matrix 与 recovery context boundary | INT-TODO-006、INT-TODO-007、INT-TODO-017 | 避免执行期重新发明规则 |
| Gate 与证据分层不足 | 单列 system integration gate matrix | INT-TODO-005、INT-TODO-023、INT-TODO-024 | 把通行条件和证据回写变成一等资产 |
| Access 专项残项进入系统主线 | 单列 AccessUnaryProductionPath contract、生产/测试 profile 边界与安全治理收口 | INT-TODO-025、INT-TODO-027、INT-TODO-028、INT-TODO-030 | 把 ACC-BLK-008 / 010 与 034 ~ 050 从子系统局部提升到系统 ingress 主线 |
| health/watchdog 残项重定级 | 单列 cadence/config/event publish boundary | INT-TODO-026、INT-TODO-029 | timer seam 已存在，需把真正阻塞点收敛到 event publish 与 cadence 口径 |

## 5. Build Track 映射

| Build 目标 | 涉及代码面 | 对应任务 | 交付标准 |
|---|---|---|---|
| default unary 最终输出修复 | runtime/、cognition/、tests/integration/agent_loop/、tests/integration/cognition/ | INT-TODO-012、INT-TODO-018 | unary 真 Gate 重新转绿 |
| structured evidence projection | contracts/、memory/、knowledge/、runtime/、tests/contract/、tests/integration/ | INT-TODO-008、009、013、019 | evidence 在主链上不再只剩字符串 |
| required/optional ports 语义落地 | runtime/、llm/、knowledge/、profiles/、tests/integration/ | INT-TODO-010、014、017、021 | required / optional / degraded 语义可测试 |
| diagnostics retained snapshot 修复 | infra/、tests/integration/infra/、tests/unit/infra/ | INT-TODO-011、015、020 | execute/store/get/export round-trip 稳定 |
| tools/services 语义对齐 | tools/、services/、tests/unit/tools/、tests/integration/tools/、tests/integration/services/ | INT-TODO-016、022 | success/error/code 三元一致 |
| Access v1 production ingress 收口 | access/、apps/daemon/、apps/gateway/、tests/integration/access/、docs/todos/access/ | INT-TODO-027、028、030 | handoff、pipeline、query/cancel、ownership/policy/observability 与 E2E Gate 成套闭合 |
| health cadence/config residual 收口 | infra/src/health/、runtime/、profiles/、tests/unit/infra/、tests/integration/infra/ | INT-TODO-029 | config policy、scheduler baseline 与 fallback 规则稳定，不再继续把 stale blocker 写成前置依赖 |
| system gate 收口 | tests/integration/、docs/ssot/、docs/todos/、docs/worklog/ | INT-TODO-023、024 | discoverability、one-shot 验收与证据闭环完成 |

## 6. 任务表

### 6.1 前置补设计 / 评审解阻任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| INT-TODO-001 | Done | 收敛默认 unary required/optional ports 矩阵 | 集成评审 GINT-02；runtime 详设；ADR-008 | `memory/cognition/tools/knowledge/llm` 在 default unary 中的 `required / optional / fail-closed / degraded` 语义 | L3 | `docs/ssot/SingleAgentRuntimePortMatrix.md`、`docs/architecture/DASALL_runtime子系统详细设计.md` | `SingleAgentRuntimePortMatrix`、`RuntimeDependencySet::has_live_unary_ports` 语义表 | 文档一致性检查 | `rg -n "required|optional|fail-closed|degraded|knowledge_service|llm_manager" docs/ssot/SingleAgentRuntimePortMatrix.md docs/architecture/DASALL_runtime子系统详细设计.md docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md` | 无 | INT-BLK-01 | 已通过 `docs/ssot/SingleAgentRuntimePortMatrix.md` 与 runtime 详设回链完成设计冻结 | `docs/ssot/SingleAgentRuntimePortMatrix.md` | 矩阵明确列出每个 port 的模式、消费者、失败语义和 gate 归属，后续 Build 不再凭口头约定解释 |
| INT-TODO-002 | Done | 收敛 AgentResult 最终响应合同与 projection 规则 | 集成评审 GINT-01；cognition/response 设计 | `response_text`、`status`、`observation projection`、`llm fallback`、`fixture vs true integration` 预期 | L3 | `docs/ssot/UnaryResponseContract.md`、`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/architecture/DASALL_cognition子系统详细设计.md` | `UnaryResponseContract`、`ResponseBuilder` 拼装规则 | 文档一致性检查 | `rg -n "response_text|observation projection|llm fallback|Completed|RuntimeUnaryIntegrationTest|CognitionRuntimeIntegrationTest" docs/ssot/UnaryResponseContract.md docs/architecture/DASALL_runtime子系统详细设计.md docs/architecture/DASALL_cognition子系统详细设计.md` | 无 | INT-BLK-02 | 已通过 `docs/ssot/UnaryResponseContract.md` 与 runtime/cognition 详设回链完成设计冻结 | `docs/ssot/UnaryResponseContract.md` | contract 明确后，任何集成测试断言都能回链到同一份规则文档 |
| INT-TODO-003 | Done | 收敛 RetrievalEvidenceRef 最小结构化共享投影 | 集成评审 GINT-03；CrossModuleDataProjectionMatrix | evidence_ref、source_ref、source_kind、summary_text、trust_level、freshness、anchor_locator 最小字段 | L3 | `docs/ssot/RetrievalEvidenceProjectionV1.md`、`docs/ssot/CrossModuleDataProjectionMatrix.md` | `RetrievalEvidenceRef` 字段表与 projection rule | 文档一致性检查 | `rg -n "evidence_ref|source_ref|source_kind|summary_text|trust_level|freshness|anchor_locator" docs/ssot/RetrievalEvidenceProjectionV1.md docs/ssot/CrossModuleDataProjectionMatrix.md docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md` | 无 | INT-BLK-03 | 已通过 `docs/ssot/RetrievalEvidenceProjectionV1.md` 与 `CrossModuleDataProjectionMatrix` 完成 additive + optional 边界冻结 | `docs/ssot/RetrievalEvidenceProjectionV1.md` | 字段表与 projection 规则冻结，且明确哪些字段禁止进入 shared contracts |
| INT-TODO-004 | Done | 收敛 diagnostics retained snapshot 契约与测试拓扑 | 集成评审 GINT-04；infra diagnostics 详设 | execute/store/get/export round-trip、retention、snapshot id、fixture topology、failure mode | L3 | `docs/ssot/DiagnosticsRetainedSnapshotContract.md`、`docs/architecture/DASALL_infra_diagnostics模块详细设计.md` | `DiagnosticsRetainedSnapshotContract`、`snapshot_id`、retention rule | 文档一致性检查 | `rg -n "retained snapshot|execute|get_snapshot|export|retention|snapshot_id" docs/ssot/DiagnosticsRetainedSnapshotContract.md docs/architecture/DASALL_infra_diagnostics模块详细设计.md docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md` | 无 | INT-BLK-04 | 已通过 `docs/ssot/DiagnosticsRetainedSnapshotContract.md` 与 infra diagnostics 详设回链完成设计冻结 | `docs/ssot/DiagnosticsRetainedSnapshotContract.md` | retained snapshot 的输入、持久化、回读、导出和拒绝语义被同一份契约固定 |
| INT-TODO-005 | Done | 收敛系统级 Gate 矩阵与证据分层规则 | 集成评审 §8、§9；TODO 格式标准 | subsystem smoke、runtime-local fixture gate、true integration gate、one-shot acceptance、worklog 回写规则 | L3 | `docs/ssot/SystemIntegrationGateMatrix.md`、`docs/worklog/DASALL_开发执行记录.md` | `Gate-INT-*`、evidence stratification | 文档一致性检查 | `rg -n "Gate-INT-|fixture gate|true integration|discoverability|worklog" docs/ssot/SystemIntegrationGateMatrix.md docs/todos/integration/DASALL_系统集成专项TODO.md docs/worklog/DASALL_开发执行记录.md` | 无 | INT-BLK-05 | 已通过 `docs/ssot/SystemIntegrationGateMatrix.md` 明确 subsystem smoke / fixture gate / true integration / worklog 分层与命令权威来源 | `docs/ssot/SystemIntegrationGateMatrix.md` | Gate、命令、通过条件、回写位置和回退动作明确，避免再用单点 smoke 冒充系统 ready |
| INT-TODO-006 | Done | 提升 RuntimePolicySnapshot consumer matrix 为系统 SSOT | 集成评审 §7.1；profiles 详设 | runtime、cognition、tools、infra、llm、memory 对共享配置键的 consumer / owner / override / hot-reload 语义 | L3 | `docs/ssot/RuntimePolicyConsumerMatrix.md`、`docs/architecture/DASALL_profiles模块详细设计.md` | `RuntimePolicyConsumerMatrix` | 文档一致性检查 | `rg -n "consumer|owner|override|hot-reload|runtime_budget|timeout|degrade" docs/ssot/RuntimePolicyConsumerMatrix.md docs/architecture/DASALL_profiles模块详细设计.md` | 无 | 无 | 已通过 `docs/ssot/RuntimePolicyConsumerMatrix.md` 与 profiles 详设回链明确 semantic owner、snapshot lifecycle owner 与 override / hot-reload 规则 | `docs/ssot/RuntimePolicyConsumerMatrix.md` | 共享配置键不再由多个模块私自重解释 |
| INT-TODO-007 | Done | 提升 Recovery Context 边界表为系统 SSOT | 集成评审 §7.2；ADR-007 | cognition 可见事实、runtime 独占事实、禁止回流项、幂等性、补偿句柄、circuit/deadline | L3 | `docs/ssot/RecoveryContextBoundary.md`、`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/architecture/DASALL_cognition子系统详细设计.md` | `RecoveryContextBoundary` | 文档一致性检查 | `rg -n "retry budget|idempotency|circuit|deadline|禁止回流|Recovery Context" docs/ssot/RecoveryContextBoundary.md docs/architecture/DASALL_runtime子系统详细设计.md docs/architecture/DASALL_cognition子系统详细设计.md` | 无 | 无 | 已通过 `docs/ssot/RecoveryContextBoundary.md` 与 runtime/cognition 详设回链固定可见事实、独占事实与禁止回流项 | `docs/ssot/RecoveryContextBoundary.md` | 建议权与执行权分离在字段级和测试级都可被引用 |
| INT-TODO-025 | Done | 收敛 AccessUnaryProductionPathV1 与 production/test profile 边界 | Access 残项核对；ACC-TODO-040/041/042；ACC-BLK-008 | `AgentRequest` handoff、production pipeline、readiness、mock pipeline 仅限测试 profile 的边界 | L3 | `docs/ssot/AccessUnaryProductionPathV1.md`、`docs/architecture/DASALL_access子系统详细设计.md`、`docs/todos/access/DASALL_access子系统专项TODO.md` | `AccessUnaryProductionPathV1`、`RuntimeDispatchRequest` handoff 规则、`AccessGateway` readiness contract | 文档一致性检查 | `rg -n "AgentRequest|RuntimeDispatchRequest|submit_pipeline_not_configured|gateway_not_ready_or_shutting_down|readiness|mock pipeline" docs/ssot/AccessUnaryProductionPathV1.md docs/architecture/DASALL_access子系统详细设计.md docs/todos/access/DASALL_access子系统专项TODO.md access/include/AccessTypes.h access/src/AccessGateway.cpp access/src/RuntimeBridge.cpp apps/gateway/src/main.cpp` | 无 | INT-BLK-06 | 已通过 `docs/ssot/AccessUnaryProductionPathV1.md` 与 access 详设 / access TODO 回链完成设计冻结 | `docs/ssot/AccessUnaryProductionPathV1.md` | Access v1 production path 的请求载荷、依赖完整性、readiness 与 mock 使用边界可被同一份规则文档引用 |
| INT-TODO-026 | Done | 收敛 health cadence / config / event publish boundary | HLT-TODO-009/012/014；RT-OQ-06；COG-OQ05 | default cadence、`HealthConfigPolicy`、`ProbeScheduler`、event publish fallback、cognition health probe 关系 | L3 | `docs/ssot/HealthCadenceAndEventBoundary.md`、`docs/architecture/DASALL_infra_health模块详细设计.md`、`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/architecture/DASALL_profiles模块详细设计.md` | `HealthCadenceAndEventBoundary`、`HealthConfigPolicy`、`ProbeScheduler`、event publish fallback rule | 文档一致性检查 | `rg -n "cadence|ProbeScheduler|HealthConfigPolicy|event publish|ITimer|diag_enabled|health probe" docs/ssot/HealthCadenceAndEventBoundary.md docs/architecture/DASALL_infra_health模块详细设计.md docs/architecture/DASALL_runtime子系统详细设计.md docs/architecture/DASALL_profiles模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md platform/include/ITimer.h` | 无 | INT-BLK-07 | 已通过 `docs/ssot/HealthCadenceAndEventBoundary.md` 与 infra/runtime/profiles/TODO 回链统一 default cadence、timer seam、event publish fallback 与 cognition health probe 关系 | `docs/ssot/HealthCadenceAndEventBoundary.md` | health 残项被重新分账为“可执行基线”和“仍待冻结的 bus 扩展”，不再以 stale blocker 挂起 |

### 6.2 骨架与公共接口面任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| INT-TODO-008 | Done | 引入 RetrievalEvidenceRef 与 ContextPacket additive fields | INT-TODO-003；contracts 冻结策略 | additive evidence projection，不破坏现有 `retrieval_evidence: vector<string>` 路径 | L3 | `contracts/include/context/ContextPacket.h`、`contracts/include/context/RetrievalEvidenceRef.h`、`contracts/include/context/ContextPacketGuards.h`、`tests/contract/context/ContextPacketMainFlowContractTest.cpp`、`tests/contract/context/ContextPacketFieldContractTest.cpp`、`tests/contract/context/RetrievalEvidenceRefContractTest.cpp` | `RetrievalEvidenceRef::has_consistent_values()`、`ContextPacket::retrieval_evidence_refs` | `ContextPacketMainFlowContractTest`、`ContextPacketFieldContractTest`、`RetrievalEvidenceRefContractTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "ContextPacketMainFlowContractTest|ContextPacketFieldContractTest|RetrievalEvidenceRefContractTest" --output-on-failure` | 003 | INT-BLK-03 | 已由 003 解阻；008 已完成 additive supporting contract、field guards 与 focused contract tests 落地 | `contracts/include/context/RetrievalEvidenceRef.h`、更新后的 `ContextPacket.h` / `ContextPacketGuards.h`、相关 contract tests | contracts 层新增字段为 additive + optional，现有文本路径与旧用例不回退，三条 focused contract tests 通过 |
| INT-TODO-009 | Done | 扩展 MemoryContextRequest 与 ContextOrchestrator evidence surface | INT-TODO-003；INT-TODO-008；集成评审 GINT-03 | external_evidence 与 structured evidence 共存；ContextPacket 组装不丢 citation/freshness | L2 | `memory/include/context/MemoryContextRequest.h`、`memory/src/context/ContextOrchestrator.cpp`、`memory/src/MemoryManagerFactory.cpp`、`tests/unit/memory/ContextOrchestratorEvidenceProjectionTest.cpp`、`tests/unit/memory/MemoryEvidenceProjectionCompileTest.cpp` | `MemoryContextRequest::retrieval_evidence_refs`、`ContextOrchestrator::assemble()`、bootstrap `IContextOrchestrator::assemble()` | `ContextOrchestratorEvidenceProjectionTest`、`MemoryEvidenceProjectionCompileTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory && ctest --test-dir build-ci -R "ContextOrchestratorEvidenceProjectionTest|MemoryEvidenceProjectionCompileTest" --output-on-failure` | 003、008 | INT-BLK-03 | 008 已落地 contracts additive fields；009 已完成 request -> orchestrator -> ContextPacket 的双轨 evidence projection | 更新后的 memory request/orchestrator/bootstrap surface、focused memory tests 与 CMake 注册 | memory 现可并行消费 `external_evidence` 与 `retrieval_evidence_refs`，且两条 focused tests 通过 |
| INT-TODO-010 | Done | 收敛 RuntimeDependencySet required/optional readiness surface | INT-TODO-001；集成评审 GINT-02 | `has_live_unary_ports`、required/optional ports、degraded marker、health/readiness 输出口 | L2 | `runtime/include/RuntimeDependencySet.h`、`tests/unit/runtime/RuntimeDependencySetReadinessTest.cpp` | `RuntimeDependencySet::has_live_unary_ports()`、`RuntimeDependencySet::describe_readiness()` | `RuntimeDependencySetReadinessTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R RuntimeDependencySetReadinessTest --output-on-failure` | 001 | INT-BLK-01 | 001 已冻结 required/optional matrix；010 已落地 `fail_closed/degraded/ready` readiness surface | 更新后的 runtime header、focused readiness unit test 与 CMake 注册 | runtime 现可显式区分 required live、default-ready 与 degraded-ready，且 `RuntimeDependencySetReadinessTest` 通过 |
| INT-TODO-011 | Done | 补 diagnostics retained snapshot fixture 与 store/get seam | INT-TODO-004；集成评审 GINT-04 | snapshot store/get surface、fixture schema、smoke fixture 与 integration fixture 拓扑 | L2 | `infra/src/diagnostics/DiagnosticsServiceFacade.cpp`、`infra/src/diagnostics/DiagnosticsServiceFacade.h`、`tests/fixtures/infra/DiagnosticsSnapshotFixture.h`、`tests/unit/infra/DiagnosticsSnapshotStoreContractTest.cpp`、`tests/unit/infra/DiagnosticsFixtureSurfaceTest.cpp` | `DiagnosticsServiceFacade::get_snapshot()`、test-time snapshot store clock seam、snapshot fixture surface | `DiagnosticsSnapshotStoreContractTest`、`DiagnosticsFixtureSurfaceTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsSnapshotStoreContractTest|DiagnosticsFixtureSurfaceTest" --output-on-failure` | 004 | INT-BLK-04 | 004 已冻结 retained snapshot contract；011 已落地 fixture schema、store/get seam 与 focused tests | diagnostics fixture、store/get contract test、fixture surface test 与 facade test seam | diagnostics retained snapshot 已具备稳定 fixture 与 contract surface，两条 focused tests 通过 |

### 6.3 配置、策略与组件实现任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| INT-TODO-012 | Done | 修复 ResponseBuilder 与 runtime 最终响应合同 | INT-TODO-002；集成评审 GINT-01 | `response_text`、`status`、`observation projection`、`llm fallback` 组合逻辑 | L2 | `cognition/include/response/ResponseBuildRequest.h`、`cognition/src/response/ResponseBuilder.cpp`、`runtime/src/AgentOrchestrator.cpp`、`tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h`、`tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp`、`tests/integration/cognition/CognitionRuntimeIntegrationTest.cpp` | `ResponseBuildRequest::build_hints`、`ResponseBuilder::build()`、runtime live unary reflection consume / final `AgentResult` publish 路径 | `RuntimeUnaryIntegrationTest`、`CognitionRuntimeIntegrationTest` | `Build_CMakeTools(target=dasall_runtime_unary_integration_test, dasall_cognition_runtime_integration_test) && RunCtest_CMakeTools(tests=RuntimeUnaryIntegrationTest, CognitionRuntimeIntegrationTest)` | 002 | INT-BLK-02 | UnaryResponseContract 已冻结 | 更新后的 response builder/runtime final response path、true integration fixture 与 focused tests | true integration path 不再把 successful reflection `Continue` 误当 recovery/resume；两条当前红灯测试转绿，且 response contract 与 fixture 分层一致 |
| INT-TODO-013 | Done | 打通 knowledge -> runtime -> memory/cognition 的 structured evidence 主链 | INT-TODO-003；INT-TODO-008；INT-TODO-009 | 从 knowledge rich evidence 到 runtime handoff，再到 memory / cognition 消费的主链投影 | L2 | `knowledge/src/facade/KnowledgeService.cpp`、`runtime/src/AgentOrchestrator.cpp`、`memory/src/context/ContextOrchestrator.cpp`、`tests/integration/knowledge/RuntimeKnowledgeEvidenceIntegrationTest.cpp` | `KnowledgeRetrieveResult.evidence`、runtime evidence projection、memory/cognition consume path | `RuntimeKnowledgeEvidenceIntegrationTest`、`KnowledgeRetrievalSmokeTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeKnowledgeEvidenceIntegrationTest|KnowledgeRetrievalSmokeTest" --output-on-failure` | 008、009 | INT-BLK-03 | structured evidence shared surface 已可编译 | 更新后的 knowledge/runtime/memory path 与 integration test | evidence_ref、freshness、citation 至少一条主链可从 knowledge 保留到 runtime/memory/cognition 消费面 |
| INT-TODO-014 | Done | 落实 required/optional ports 与 degraded path 行为 | INT-TODO-001；INT-TODO-010；集成评审 GINT-02 | knowledge / llm 缺失时的 readiness、fallback tag、审计、对外结果语义 | L2 | `runtime/include/RuntimeDependencySet.h`、`runtime/src/AgentFacade.cpp`、`runtime/src/AgentOrchestrator.cpp`、`cognition/src/CognitionFacade.cpp` | `RuntimeDependencySet`、runtime degraded markers、llm/knowledge unavailable behavior | `RuntimeRequiredOptionalPortsIntegrationTest`、`RuntimeProfileCompatibilityTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeRequiredOptionalPortsIntegrationTest|RuntimeProfileCompatibilityTest" --output-on-failure` | 001、010 | INT-BLK-01 | port matrix 与 readiness surface 已冻结 | runtime/cognition integration updates 与 tests | 缺 port 时系统行为与矩阵一致，且不会把 degraded 路径伪装成 default ready |
| INT-TODO-015 | Done | 修复 diagnostics execute/store/get retained snapshot round-trip | INT-TODO-004；INT-TODO-011；集成评审 GINT-04 | execute 后 snapshot 持久化、retention、get_snapshot 映射、export 一致性 | L2 | `infra/src/diagnostics/DiagnosticsServiceFacade.cpp`、相关 snapshot store 源码、`tests/integration/infra/InfraDiagnosticsSmokeTest.cpp` | `DiagnosticsServiceFacade::execute/get_snapshot/export_snapshot` | `InfraDiagnosticsSmokeTest`、`DiagnosticsSnapshotStoreTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "InfraDiagnosticsSmokeTest|DiagnosticsSnapshotStoreTest" --output-on-failure` | 004、011 | INT-BLK-04 | retained snapshot seam 与 fixture 可用 | diagnostics round-trip fix 与 tests | `InfraDiagnosticsSmokeTest` 转绿，且 unit/fixture contract 不回退 |
| INT-TODO-016 | Done | 统一 tools/services success-error-code 三元语义 | 集成评审 GINT-05；tools/services 详设 | `ResultCode`、`error`、`success` 的统一判定规则与默认 service 返回 | L2 | `tools/src/execution/BuiltinExecutorLane.cpp`、`services/include/ServiceTypes.h`、`tests/unit/tools/BuiltinExecutorLaneResultCodeTest.cpp` | `BuiltinExecutorLane::map_service_result()`、default execution/data service result | `BuiltinExecutorLaneResultCodeTest`、`ToolServicesSmokeIntegrationTest`、`CapabilityServicesSmokeIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "BuiltinExecutorLaneResultCodeTest|ToolServicesSmokeIntegrationTest|CapabilityServicesSmokeIntegrationTest" --output-on-failure` | 无 | 无 | 统一语义规则并同步到 tests | 更新后的 tools/services 代码与 tests | payload success 不再携带失败 code，三元语义一致且 smoke 不回退 |
| INT-TODO-017 | Done | 对齐 RuntimePolicySnapshot 与 Recovery Context 在 runtime 执行点的消费 | INT-TODO-006；INT-TODO-007；集成评审 §7.1/§7.2 | runtime 对共享配置键、retry budget、idempotency、circuit/deadline 的使用点与 SSOT 对齐 | L2 | `runtime/src/AgentOrchestrator.cpp`、`runtime/src/recovery/RecoveryManager.cpp`、`tests/integration/agent_loop/RuntimePolicyConsumerIntegrationTest.cpp`、`tests/integration/agent_loop/RuntimeRecoveryContextIntegrationTest.cpp` | `AgentOrchestrator` policy consume points、`RecoveryManager::evaluate/apply` | `RuntimePolicyConsumerIntegrationTest`、`RuntimeRecoveryContextIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "RuntimePolicyConsumerIntegrationTest|RuntimeRecoveryContextIntegrationTest" --output-on-failure` | 006、007 | 无 | system SSOT 已建立 | runtime 代码和 tests | runtime 不再用私有假设解释 shared policy 与 recovery context |
| INT-TODO-027 | Done | 修复 Access AgentRequest handoff、production pipeline 与 readiness | INT-TODO-025；ACC-TODO-040/041/042；Access 代码核对 | `RuntimeDispatchRequest` 与 `AgentRequest` handoff、`AccessGateway` 依赖校验、daemon/gateway production composition root | L2 | `access/include/AccessTypes.h`、`access/include/IAccessRuntimeBridge.h`、`access/src/RuntimeBridge.cpp`、`access/src/AccessGateway.cpp`、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` | `RuntimeDispatchRequest`、`IAccessRuntimeBridge::dispatch()`、`AccessGateway::init/submit()`、gateway/daemon composition | `RuntimeBridgeAgentRequestHandoffTest`、`RequestNormalizerRuntimeBridgeCompatibilityTest`、`AccessGatewayProductionPipelineTest`、`AccessGatewayDependencyValidationTest`、`DaemonAccessSubmitCompositionTest`、`GatewayAccessSubmitCompositionTest`、`AccessHealthReadinessIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_access_tests dasall_daemon dasall_gateway && ctest --test-dir build-ci -R "RuntimeBridgeAgentRequestHandoffTest|RequestNormalizerRuntimeBridgeCompatibilityTest|AccessGatewayProductionPipelineTest|AccessGatewayDependencyValidationTest|DaemonAccessSubmitCompositionTest|GatewayAccessSubmitCompositionTest|AccessHealthReadinessIntegrationTest" --output-on-failure` | 025 | INT-BLK-06 | AccessUnaryProductionPathV1 已冻结 | 更新后的 access/apps 代码与 focused tests | `AgentRequest` handoff 不再靠 `request_context` 侧带，空 pipeline 不得进入 `Ready`，daemon/gateway production path 与测试 mock path 被严格分离 |
| INT-TODO-028 | Done | 打通 Access async ownership / policy / observability 安全治理闭环 | INT-TODO-025；ACC-TODO-043/046/047/048；ACC-BLK-010 | async receipt query/cancel、ownership token、policy evaluator、observability main chain 与 publish failure 审计 | L2 | `access/src/AsyncTaskRegistry.*`、`access/src/AccessPolicyGate.*`、`access/src/AccessObservabilityBridge.*`、`apps/gateway/src/TaskQueryHandler.*`、daemon IPC handler、infra secret/policy/logging/metrics/audit bridge seam | async query/cancel path、HMAC ownership、policy evaluator seam、observability event emission | `AccessAsyncReceiptQueryCancelIntegrationTest`、`AccessCancelForwardingTest`、`DaemonSubmitQueryCancelIntegrationTest`、`AsyncTaskRegistryHmacOwnershipTest`、`AccessPolicyBackendUnavailableIntegrationTest`、`AccessObservabilityMainChainIntegrationTest`、`AccessPublishFailureAuditTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_access_tests && ctest --test-dir build-ci -R "AccessAsyncReceiptQueryCancelIntegrationTest|AccessCancelForwardingTest|DaemonSubmitQueryCancelIntegrationTest|AsyncTaskRegistryHmacOwnershipTest|AccessPolicyBackendUnavailableIntegrationTest|AccessObservabilityMainChainIntegrationTest|AccessPublishFailureAuditTest" --output-on-failure` | 025、027 | INT-BLK-06 | production path 与 subject/policy input 已可稳定注入 | 更新后的 access/infra seams 与 focused tests | owner mismatch、policy backend unavailable、publish failure、cancel reject 等安全治理路径都能以 fail-closed 方式稳定断言 |
| INT-TODO-029 | Done | 落 HealthConfigPolicy / ProbeScheduler baseline 并校准 event publish fallback | INT-TODO-026；HLT-TODO-009/012/014；RT-OQ-06 | `HealthConfigPolicy`、`ProbeScheduler`、runtime/background maintenance cadence consume points、event sink 缺失时 fallback | L2 | `infra/src/health/HealthConfigPolicy.cpp`、`infra/src/health/ProbeScheduler.cpp`、`runtime/src/health/RuntimeHealthProbe.cpp`、`runtime/src/maintenance/BackgroundMaintenanceHooks.cpp`、`tests/unit/infra/health/`、`tests/integration/infra/` | `HealthConfigPolicy::merge/validate_thresholds()`、`ProbeScheduler::start/stop/tick_once()`、runtime cadence projection consume、event fallback rule | `HealthConfigPolicyTest`、`ProbeSchedulerTest`、`InfraHealthCadenceIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "HealthConfigPolicyTest|ProbeSchedulerTest|InfraHealthCadenceIntegrationTest" --output-on-failure` | 026 | INT-BLK-07 | cadence/config/event boundary 已冻结 | health baseline 代码、focused tests 与 blocker 回写 | scheduler 以既有 `ITimer` seam 运转，`HealthConfigPolicy` 不再缺位，runtime 只消费 health cadence 投影，event bus 未冻结时仍能通过日志/指标 fallback 保持系统可观测 |

### 6.4 测试支撑、集成与门禁收口任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| INT-TODO-018 | Done | 修复并固化 default unary 主 Gate | INT-TODO-002；INT-TODO-012；集成评审 GINT-01 | 把 `RuntimeUnaryIntegrationTest` 与 `CognitionRuntimeIntegrationTest` 正式提升为系统通行门 | L2 | `tests/integration/agent_loop/CMakeLists.txt`、`tests/integration/cognition/CMakeLists.txt`、`tests/contract/CMakeLists.txt`、`tests/CMakeLists.txt` | `gate-int-03` label、`default-unary-gate` label、`dasall_gate_int_03` | `RuntimeUnaryIntegrationTest`、`CognitionRuntimeIntegrationTest`、`MainFlowContractE2ETest` | `Build_CMakeTools(target=dasall_gate_int_03)` | 012 | INT-BLK-02 | response contract 已修复 | 更新后的 Gate-INT-03 labels / `dasall_gate_int_03` target 与 focused evidence | default unary 主 Gate 绿色，且 `gate-int-03` / `default-unary-gate` discoverability 稳定 |
| INT-TODO-019 | Done | 新增 structured evidence preservation integration Gate | INT-TODO-003；INT-TODO-013 | knowledge -> runtime -> memory/cognition 的 evidence preservation 主路径与拒绝路径 | L2 | `tests/integration/agent_loop/RuntimeEvidenceProjectionIntegrationTest.cpp`、`tests/integration/knowledge/KnowledgeEvidencePreservationTest.cpp`、`tests/integration/agent_loop/CMakeLists.txt`、`tests/integration/knowledge/CMakeLists.txt`、`tests/CMakeLists.txt` | `RuntimeEvidenceProjectionIntegrationTest`、`KnowledgeEvidencePreservationTest`、`gate-int-04` label、`dasall_gate_int_04` | `RuntimeEvidenceProjectionIntegrationTest`、`KnowledgeEvidencePreservationTest` | `Build_CMakeTools(target=dasall_gate_int_04)` | 013 | INT-BLK-03 | structured evidence 主链已打通 | 新增 Gate-INT-04 tests / labels / `dasall_gate_int_04` target 与 focused evidence | 至少一条主链和一条 reject 路径能验证 evidence 字段保留或 fail-closed，不再依赖 013 的旧测试名 |
| INT-TODO-020 | NotStarted | 修复并固化 diagnostics retained snapshot Gate | INT-TODO-004；INT-TODO-015 | `InfraDiagnosticsSmokeTest`、retained snapshot fixture、integration discoverability | L2 | `tests/integration/infra/InfraDiagnosticsSmokeTest.cpp`、`tests/integration/infra/CMakeLists.txt` | diagnostics round-trip gate | `InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "InfraDiagnosticsSmokeTest|InfraDiagnosticsIntegrationTest" --output-on-failure` | 015 | INT-BLK-04 | diagnostics round-trip 已修复 | 更新后的 infra integration tests 与 gate 记录 | diagnostics retained snapshot 被系统级 Gate 稳定守住，不再只是临时 smoke |
| INT-TODO-021 | NotStarted | 新增 required/optional ports 与 degraded mode integration/profile Gate | INT-TODO-001；INT-TODO-014；INT-TODO-017 | default mode、degraded mode、profile compatibility、readiness tag | L2 | `tests/integration/agent_loop/RuntimeRequiredOptionalPortsIntegrationTest.cpp`、`tests/integration/agent_loop/RuntimeProfileCompatibilityTest.cpp` | runtime port mode integration、profile compatibility | `RuntimeRequiredOptionalPortsIntegrationTest`、`RuntimeProfileCompatibilityTest`、`LLMSubsystemSmokeIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeRequiredOptionalPortsIntegrationTest|RuntimeProfileCompatibilityTest|LLMSubsystemSmokeIntegrationTest" --output-on-failure` | 014、017 | INT-BLK-01 | port mode 语义和 consume points 已落地 | integration tests 与 gate evidence | required / optional / degraded 行为可在三档 profile 下稳定复现 |
| INT-TODO-022 | NotStarted | 新增 tools/services 状态语义 contract Gate | INT-TODO-016 | success/error/code 统一语义，避免结果码漂移重新出现 | L2 | `tests/contract/tools/ServiceResultSemanticsContractTest.cpp`、`tests/unit/tools/BuiltinExecutorLaneResultCodeTest.cpp` | service result semantic contract | `ServiceResultSemanticsContractTest`、`BuiltinExecutorLaneResultCodeTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_tests dasall_unit_tests && ctest --test-dir build-ci -R "ServiceResultSemanticsContractTest|BuiltinExecutorLaneResultCodeTest" --output-on-failure` | 016 | 无 | 统一语义实现完成 | 新增 contract tests | result semantics 发生回退时能在 unit/contract 层被第一时间拦截 |
| INT-TODO-023 | NotStarted | 接线系统级 Gate discoverability 与 one-shot acceptance 命令 | INT-TODO-005；TODO 格式标准 | `ctest -N` discoverability、聚合 target、聚焦 test regex、统一验收命令 | L2 | `tests/integration/CMakeLists.txt`、`tests/contract/CMakeLists.txt`、`docs/todos/integration/DASALL_系统集成专项TODO.md` | system gate labels / discoverability / one-shot command | `ctest -N`、focused integration matrix | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci -R "(RuntimeUnaryIntegrationTest|CognitionRuntimeIntegrationTest|RuntimeEvidenceProjectionIntegrationTest|InfraDiagnosticsSmokeTest|RuntimeRequiredOptionalPortsIntegrationTest|RuntimeProfileCompatibilityTest|ToolServicesSmokeIntegrationTest|CapabilityServicesSmokeIntegrationTest|LLMSubsystemSmokeIntegrationTest|dasall_knowledge_retrieval_smoke_integration_test)" --output-on-failure` | 018、019、020、021、022 | INT-BLK-05 | 关键 Gate 已具 discoverability 与稳定命名 | 更新后的 CMake 注册与 TODO 文档 | `ctest -N` 能发现系统级 Gate；统一命令能覆盖关键红绿灯 |
| INT-TODO-024 | NotStarted | 回写系统集成 Gate、交付物与 worklog 证据 | INT-TODO-005；DASALL 开发执行规范 | 专项 TODO、deliverables、集成评审报告、开发执行记录之间的证据闭环 | L2 | `docs/todos/integration/DASALL_系统集成专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md`、`docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md` | Gate evidence、blocker status、residual risk | 文档一致性与 current-build 复验 | `rg -n "Gate-INT-|INT-TODO-|RuntimeUnaryIntegrationTest|InfraDiagnosticsSmokeTest|RuntimeEvidenceProjectionIntegrationTest" docs/todos/integration/DASALL_系统集成专项TODO.md docs/worklog/DASALL_开发执行记录.md docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md && ctest --test-dir build-ci -N` | 023 | INT-BLK-05 | discoverability 与 focused matrix 已稳定 | 更新后的 TODO / worklog / review doc | 每个 Gate 有命令证据、交付物路径、当前状态、后继任务和残余风险，不再只靠口头结论 |
| INT-TODO-030 | NotStarted | 固化 Access v1 production Gate 与证据分层 | INT-TODO-025；INT-TODO-027；INT-TODO-028；ACC-TODO-034/035/036/049/050 | Access v1 release gate、CLI->daemon、HTTP->gateway、async receipt、health readiness、profile/contracts guard、Access 证据回写 | L2 | `tests/integration/access/`、`tests/contract/`、`docs/todos/access/`、`docs/todos/integration/`、`docs/worklog/DASALL_开发执行记录.md` | `AccessGatewayPipelineIntegrationTest`、`CliDaemonSubmitIntegrationTest`、`HttpGatewaySubmitIntegrationTest`、`AccessAsyncReceiptQueryCancelIntegrationTest`、`AccessPolicyBackendUnavailableIntegrationTest`、`AccessHealthReadinessIntegrationTest`、`AccessProfileCompatibilityTest` | Access v1 E2E / contract gate 与文档回写 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_access_tests dasall_contract_tests && ctest --test-dir build-ci -R "AccessGatewayPipelineIntegrationTest|CliDaemonSubmitIntegrationTest|HttpGatewaySubmitIntegrationTest|AccessAsyncReceiptQueryCancelIntegrationTest|AccessPolicyBackendUnavailableIntegrationTest|AccessHealthReadinessIntegrationTest|AccessProfileCompatibilityTest|AgentRequestContractTest|AgentResultContractTest|IdentityMetadataContractTest" --output-on-failure && rg -n "Build-Ready|AccessGatewayPipelineIntegrationTest|AccessAsyncReceiptQueryCancelIntegrationTest|AccessHealthReadinessIntegrationTest" docs/todos/access docs/todos/integration docs/worklog/DASALL_开发执行记录.md` | 027、028 | INT-BLK-06 | Access 主链与安全治理 focused tests 已稳定 | Access gate 记录、deliverables 与 worklog 回写 | Access v1 不再以 mock pipeline、ping liveness 或局部 envelope 字段作为 release 证据，且系统 TODO / 子系统 TODO / worklog 三处口径一致 |

## 7. 执行顺序建议

### 7.1 串并行编排

| 阶段 | 任务 | 编排建议 | 说明 |
|---|---|---|---|
| A 设计与 SSOT 冻结 | 001 ~ 007、025、026 | 001/002/003/004/025 并行起步，005 在其后，006/007/026 再收口 | 先冻结 unary mode、response contract、evidence projection、diagnostics contract、Access production path、Gate 分层、policy/recovery/health cadence SSOT |
| B 接口与共享投影骨架 | 008 ~ 011 | 008 先，009/010/011 可并行 | 先把 shared surface 和 seam 拉平，避免实现期补丁字段扩散 |
| C 主链实现收口 | 012 ~ 017、027 ~ 029 | 012、015、016、027 可并行起步；013 依赖 008/009；014 依赖 001/010；017 依赖 006/007；028 依赖 027；029 依赖 026 | 把当前红灯、Access ingress 主链与 health post-unary 残项直接落到代码 |
| D 系统 Gate 收口 | 018 ~ 023、030 | 018/020/030 可并行；019 在 013 后；021 在 014/017 后；022 在 016 后；023 在关键 Gate 绿后串行 | 让主链、structured evidence、diagnostics、Access production ingress、required/optional ports、result semantics 全部进入稳定 Gate |
| E 证据与工作记录回写 | 024 | 串行 | 统一回写 TODO / worklog / review 证据，形成后续迭代基座 |

### 7.2 必过门禁表

| Gate ID | 对应设计 Gate | 通过条件 | 关联任务 |
|---|---|---|---|
| Gate-INT-01 | TODO 新增 | single-agent unary required/optional mode、response contract、evidence projection、diagnostics contract 与 system gate matrix 全部冻结 | 001 ~ 007 |
| Gate-INT-02 | TODO 新增 | RetrievalEvidenceRef shared surface 与 memory/runtime seam 编译、contract test 通过 | 008 ~ 011 |
| Gate-INT-03 | 集成评审 GINT-01 | `RuntimeUnaryIntegrationTest`、`CognitionRuntimeIntegrationTest`、`MainFlowContractE2ETest` 全绿 | 012、018 |
| Gate-INT-04 | 集成评审 GINT-03 | structured evidence preservation integration gate 通过 | 013、019 |
| Gate-INT-05 | 集成评审 GINT-04 | diagnostics retained snapshot gate 通过 | 015、020 |
| Gate-INT-06 | 集成评审 GINT-02 | required/optional ports 与 degraded mode gate 通过 | 014、017、021 |
| Gate-INT-07 | 集成评审 GINT-05 | tools/services result semantics gate 通过 | 016、022 |
| Gate-INT-08 | Access 残项吸收 | Access v1 production ingress gate 通过，且 mock/test profile 与 production 证据分层清晰 | 027、028、030 |
| Gate-INT-09 | TODO 新增 | discoverability、one-shot acceptance、worklog 与交付证据闭环完成 | 023、024 |

## 8. 阻塞项与解阻条件

| Blocker ID | 对应设计 Blocker | 阻塞项 | 当前影响 | 解阻条件 | 回退策略 |
|---|---|---|---|---|---|
| INT-BLK-01 | TODO 新增 | default unary required/optional product mode 未冻结 | 已由 001 完成设计冻结解阻；后续转入 010、014、021 的 Build 与 Gate 落地 | 完成 001 并通过相关 owner 评审 | 未冻结前，只允许 fixture / docs 级验证，不宣布 default ready |
| INT-BLK-02 | TODO 新增 | unary response contract 与 fixture 期望未统一 | 已由 002 完成设计冻结解阻；后续转入 012、018 的 Build 与 Gate 固化 | 完成 002 并同步 runtime/cognition/test fixture | 未统一前，不扩大 response builder 分支 |
| INT-BLK-03 | TODO 新增 | structured evidence shared projection 未经 contracts admission | 已由 003 完成设计冻结解阻；后续转入 008、009、013、019 的 contracts / runtime / integration 落地 | 完成 003 并通过 contracts additive 策略确认 | admission 未完成前，保留旧文本路径，禁止私有字段横向扩散 |
| INT-BLK-04 | TODO 新增 | diagnostics retained snapshot contract / fixture 未冻结 | 已由 004 完成设计冻结解阻；后续转入 011、015、020 的 seam、round-trip 与 gate 落地 | 完成 004 并冻结 snapshot contract | 冻结前只做局部调试，不宣称 diagnostics ready |
| INT-BLK-05 | 工具链已知问题 | aggregate build/test 容易被外部或无关问题污染；但 005 已冻结“targeted build/ctest 优先于 aggregate one-shot”的命令权威规则 | 023、024 的 one-shot 验收仍可能受噪音影响 | 使用 targeted build/ctest；必要时单列 residual blocker | 若 aggregate 噪音未清，保留 focused command 作为正式验收依据 |
| INT-BLK-06 | Access 残项吸收 | 已由 025 完成 production/test profile 与 readiness 口径冻结；当前剩余 `AgentRequest` handoff、空 pipeline Ready 与安全治理的代码/ Gate 闭合 | 027、028、030 仍无法宣称 ingress production ready | 完成 025、027、028，并使 focused Access Gate 转绿 | 在解阻前，daemon/gateway 只能显式使用 mock pipeline 作为测试 profile，不得冒充 production 证据 |
| INT-BLK-07 | health/watchdog 重定级 | 已由 026 完成设计冻结；当前剩余是 `HLT-TODO-012` 的 external bus 最小接口与 029 的代码/测试落地，不再是 system cadence 歧义 | 029、HLT-TODO-012、RT-TODO-030 | 完成 029，并在 HLT-TODO-012 中沿已冻结 fallback rule 落 event publish surface | event publish 未冻结前，仅允许同步 evaluate、日志/指标 fallback 与局部缓存，不宣称 bus-ready |
| INT-BLK-08 | deferred streaming | Access stream attach/reconnect/replay cursor 与 LLM shared stream admission 仍未冻结 | 不阻断 001 ~ 030，但阻断后续 streaming 任务与任何 stream-ready 结论 | runtime/llm/contracts 冻结 cancel/ownership/backpressure/shared handle 语义并形成消费者矩阵 | 保持 feature flag default-off + receipt/query/poll fallback，不把 streaming 写入当前 unary gate |
| INT-BLK-09 | deferred optional backend | `sqlite-vss` concrete backend、KMS backend、OTLP exporter 依赖与夹具未冻结 | 不阻断当前 unary Gate，但阻断 optional backend 扩张与相关 release ready 结论 | 明确 third_party / SDK / fixture 策略并补 focused tests | 继续保持 unavailable / noop / file backend baseline，不让 optional backend 反向拖垮主链 |

### 8.1 Blocker 校准记录

| Blocker ID | 校准时间 | 校准结果 | 剩余阻塞范围 | 备注 |
|---|---|---|---|---|
| INT-BLK-01 | 2026-05-06 | 已解阻 | 后续仅剩 010、014、021 的实现与 gate 固化 | 由 `docs/ssot/SingleAgentRuntimePortMatrix.md` 与 runtime 详设回链关闭 |
| INT-BLK-02 | 2026-05-06 | 已解阻 | 后续仅剩 012、018 的实现与主 Gate 固化 | 由 `docs/ssot/UnaryResponseContract.md` 与 runtime/cognition 详设回链关闭 |
| INT-BLK-03 | 2026-05-06 | 已解阻 | 后续仅剩 008、009、013、019 的实现与 gate 固化 | 由 `docs/ssot/RetrievalEvidenceProjectionV1.md` 与总矩阵回链关闭 |
| INT-BLK-04 | 2026-05-06 | 已解阻 | 后续仅剩 011、015、020 的实现与 gate 固化 | 由 `docs/ssot/DiagnosticsRetainedSnapshotContract.md` 与 infra diagnostics 详设回链关闭 |
| INT-BLK-05 | 2026-05-06 | 已知可绕行 | one-shot aggregate 验收层；005 已完成命令权威规则冻结 | 使用 targeted 命令即可推进，待 023/024 再把 discoverability / one-shot 落为可执行 Gate |
| INT-BLK-06 | 2026-05-06 | 未解阻 | 设计冻结已完成；剩余 Access production path、安全治理与 Access Gate 的实现闭合 | 由 `docs/ssot/AccessUnaryProductionPathV1.md`、Access 详设与 Access 专项 TODO 回链确认 025 已完成 |
| INT-BLK-07 | 2026-05-06 | 已解阻（设计冻结完成） | 剩余 health event bus / cadence consume build residual | `HealthCadenceAndEventBoundary` 已冻结 `ITimer` seam、default cadence、event publish fallback 与 cognition health probe 关系；剩余只转入 029 / HLT-TODO-012 / RT-TODO-030 的实现与 Gate |
| INT-BLK-08 | 2026-05-06 | 延后跟踪 | streaming / shared admission late scope | 来源于 ACC-BLK-005、LLM-BLK-005、LLM-BLK-006 |
| INT-BLK-09 | 2026-05-06 | 延后跟踪 | optional backend late scope | 来源于 MEM-TODO-035、SEC-BLK-003、MET-BLK-005 |

## 9. 测试矩阵与统一验收命令

### 9.1 测试矩阵

| 测试层 | 关键用例 | 通过标准 |
|---|---|---|
| 文档 / SSOT 一致性 | 001 ~ 007 的 `rg` 一致性命令 | required/optional、response contract、evidence projection、diagnostics contract、gate matrix、policy/recovery SSOT 可被同一套关键词检索到 |
| contract / compile surface | `ContextPacketMainFlowContractTest`、`ContextPacketFieldContractTest`、`RetrievalEvidenceRefContractTest`、`MemoryEvidenceProjectionCompileTest`、`ServiceResultSemanticsContractTest` | additive fields 不破坏旧路径，semantic contract 可编译可回归 |
| unit | `RuntimeDependencySetReadinessTest`、`BuiltinExecutorLaneResultCodeTest`、`DiagnosticsSnapshotStoreContractTest`、`AccessGatewayDependencyValidationTest`、`HealthConfigPolicyTest`、`ProbeSchedulerTest` | 局部语义和 surface 不出现回退 |
| integration | `RuntimeUnaryIntegrationTest`、`CognitionRuntimeIntegrationTest`、`RuntimeEvidenceProjectionIntegrationTest`、`InfraDiagnosticsSmokeTest`、`RuntimeRequiredOptionalPortsIntegrationTest`、`RuntimeProfileCompatibilityTest`、`AccessGatewayPipelineIntegrationTest`、`CliDaemonSubmitIntegrationTest`、`HttpGatewaySubmitIntegrationTest`、`AccessAsyncReceiptQueryCancelIntegrationTest`、`AccessHealthReadinessIntegrationTest` | 主红灯与新增主链 gate 全绿 |
| regression smoke | `ToolServicesSmokeIntegrationTest`、`CapabilityServicesSmokeIntegrationTest`、`LLMSubsystemSmokeIntegrationTest`、`dasall_knowledge_retrieval_smoke_integration_test`、`DaemonBinaryUnarySmokeTest` | 既有绿色切片不回退 |
| discoverability | `ctest -N` | 系统级 Gate 可被统一发现并可单独执行 |

### 9.2 统一验收命令

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_contract_tests dasall_integration_tests && \
ctest --test-dir build-ci -N && \
ctest --test-dir build-ci -R "(RuntimeUnaryIntegrationTest|CognitionRuntimeIntegrationTest|MainFlowContractE2ETest|RuntimeEvidenceProjectionIntegrationTest|KnowledgeEvidencePreservationTest|InfraDiagnosticsSmokeTest|InfraDiagnosticsIntegrationTest|RuntimeRequiredOptionalPortsIntegrationTest|RuntimeProfileCompatibilityTest|BuiltinExecutorLaneResultCodeTest|ServiceResultSemanticsContractTest|ToolServicesSmokeIntegrationTest|CapabilityServicesSmokeIntegrationTest|LLMSubsystemSmokeIntegrationTest|dasall_knowledge_retrieval_smoke_integration_test|DaemonBinaryUnarySmokeTest|AccessGatewayPipelineIntegrationTest|CliDaemonSubmitIntegrationTest|HttpGatewaySubmitIntegrationTest|AccessAsyncReceiptQueryCancelIntegrationTest|AccessPolicyBackendUnavailableIntegrationTest|AccessHealthReadinessIntegrationTest|AccessProfileCompatibilityTest)" --output-on-failure
```

### 9.3 质量门清单

| Gate | 通过条件 | 对应任务 |
|---|---|---|
| Gate-INT-01 | 设计与 SSOT 冻结完成 | 001 ~ 007 |
| Gate-INT-02 | shared evidence surface 与 memory/runtime seam 绿 | 008 ~ 011 |
| Gate-INT-03 | default unary 主 Gate 绿 | 012、018 |
| Gate-INT-04 | structured evidence preservation 绿 | 013、019 |
| Gate-INT-05 | diagnostics retained snapshot 绿 | 015、020 |
| Gate-INT-06 | required/optional + degraded semantics 绿 | 014、017、021 |
| Gate-INT-07 | result semantics 绿 | 016、022 |
| Gate-INT-08 | Access v1 production ingress gate 绿 | 027、028、030 |
| Gate-INT-09 | discoverability 与证据回写完成 | 023、024 |

### 9.4 Gate 写回规则

1. `RuntimeUnaryFixtureIntegrationTest` 可以继续作为 runtime-local 证据，但绝不能替代 Gate-INT-03。
2. `ToolServicesSmokeIntegrationTest`、`CapabilityServicesSmokeIntegrationTest`、`LLMSubsystemSmokeIntegrationTest`、`dasall_knowledge_retrieval_smoke_integration_test` 属于保护既有绿色切片的 regression smoke，不等于系统主链 ready。
3. 若 aggregate target 受外部噪音影响，则以 targeted build + targeted ctest 命令为正式验收依据，并把 aggregate 问题单列为 residual blocker。

## 10. 风险与回退策略

### 10.1 风险表

| Risk ID | 风险 | 影响 | 缓解动作 |
|---|---|---|---|
| INT-R01 | 在 unary 主 Gate 未绿前继续扩主链功能面 | 红灯面扩大、调试成本飙升 | 严格按 001 ~ 030 顺序推进，先做 P0 收口 |
| INT-R02 | 结构化 evidence 演进过大，反向污染 shared contracts | ABI 返工与跨模块震荡 | 只推进 RetrievalEvidenceRef 最小 additive surface |
| INT-R03 | required/optional port 语义迟迟不冻 | runtime readiness 与 degraded 路径长期口径不一 | 001 作为首批并行任务之一 |
| INT-R04 | diagnostics round-trip 只修实现不修 contract | 测试继续漂移，回归难稳定复现 | 004、011、015、020 成套推进 |
| INT-R05 | 用旧绿色 smoke 掩盖主红灯 | 阶段性判断失真 | Gate-INT-03 / 04 / 05 必须单列 |
| INT-R06 | tools/services 状态码语义继续混杂 | 遥测、审计、故障定位长期失真 | 016、022 必须在系统集成阶段清理 |
| INT-R07 | policy / recovery 仍停留在 subsystem-local 文档 | 实现中再次各写各的规则 | 006、007、017 把 SSOT 与代码消费点打通 |
| INT-R08 | 继续把 Access mock pipeline / ping liveness 写成 production 证据 | ingress 主链状态被误判为 ready | 025、027、030 必须把 production/test profile 与 Gate 分层写死 |
| INT-R09 | 把 streaming、optional backend 或 late-scope 能力误拉回当前 P0 | 主线扩散、验收面失焦 | 用 INT-BLK-08 / 09 与 12.x OQ 表显式分账 |

### 10.2 回退策略

1. RetrievalEvidenceRef 若 admission 未通过，暂不推进 Build，保留旧文本路径并把设计结果单列为 deliverable。
2. default unary 主 Gate 若修复中发现 contract 仍不稳定，先回退到 002 补设计，不在 012 上反复试错。
3. diagnostics 若 round-trip 一轮内无法闭合，先保持 execute/export 可用，但不得再宣布 retained snapshot ready。
4. one-shot aggregate 验收若受外部问题污染，保留 targeted commands 作为正式验收证据，并在 024 中记入 residual blocker。

## 11. 可行性结论

### 11.1 是否可直接进入执行

可以，但不建议直接并行打包多个 Build 行。当前 001、002、003、004、005、006、007、008、009、010、011、012、013、014、015、016、017、018、019、025、026、027、028、029 已完成；当前用户请求的 Gate 固化任务已完成 `INT-TODO-019` 的 structured evidence preservation gate，若继续系统串行推进，下一候选位为 `INT-TODO-020`。

### 11.2 当前最细可执行粒度

1. L3：required/optional port matrix、UnaryResponseContract、RetrievalEvidenceProjectionV1、DiagnosticsRetainedSnapshotContract、RuntimePolicyConsumerMatrix、RecoveryContextBoundary。
2. L2：ResponseBuilder/runtime final response fix、structured evidence mainline、diagnostics round-trip fix、tools/services result semantics、required/optional port runtime behavior。
3. Gate 级：default unary 主 Gate、structured evidence preservation Gate、diagnostics retained snapshot Gate、result semantics Gate、discoverability / evidence writeback Gate。

### 11.3 建议执行策略

1. 先做设计冻结，不要一开始就同时改 runtime、memory、knowledge、infra、access 多条实现线。
2. 第一轮 Build 只聚焦 012、015、016、027 四个最直接对现有红灯、高风险语义与 Access 主链断裂负责的任务。
3. 第二轮 Build 再推进 013、014、017、028、029，把 structured evidence、required/optional semantics、Access 安全治理与 health residual 接入系统主链或 post-unary 队列。
4. 最后用 018 ~ 024、030 统一收口 Gate、discoverability 与 worklog 证据。

## 12. 未决问题处置表

| OQ ID | 来源 | 问题摘要 | 处置方式 | 关联 TODO | 说明 |
|---|---|---|---|---|---|
| INT-OQ-01 | 集成评审 GINT-02 | knowledge / llm 对 default unary 是 required 还是 optional | 必须采纳 | 001、010、014、021 | 不允许跳过 |
| INT-OQ-02 | 集成评审 GINT-01 | 最终 response_text 是否允许继续携带 observation fallback 风格文本 | 必须采纳 | 002、012、018 | 需统一 runtime/cognition/test fixture |
| INT-OQ-03 | 集成评审 GINT-03 | RetrievalEvidenceRef 最小字段边界 | 必须采纳 | 003、008、009、013、019 | 只允许 additive + optional |
| INT-OQ-04 | 集成评审 GINT-04 | retained snapshot 的最小 round-trip 契约和 fixture 结构 | 必须采纳 | 004、011、015、020 | 否则 diagnostics gate 无法稳定 |
| INT-OQ-05 | 集成评审 GINT-05 | ResultCode 是否成为 success 真值还是仅做结构化标签 | 先采纳后验证 | 016、022 | 需通过 unit + contract + smoke 共同验证 |
| INT-OQ-06 | 工程治理 | one-shot acceptance 是否以 build-ci targeted command 为正式基准 | 建议采纳 | 023、024 | 避免依赖 IDE 状态与 preset 漂移 |
| INT-OQ-07 | Access / LLM residual | stream attach/reconnect/replay cursor、shared stream admission 是否进入 shared contracts | 延后并保留为 blocker 跟踪 | INT-BLK-08 | 当前 v1 unary 仅接受 feature flag default-off + receipt/query/poll fallback |
| INT-OQ-08 | health/runtime/cognition residual | default cadence、cognition telemetry health probe、event publish minimal interface 如何统一 | 先采纳设计，后做 post-unary Build | 026、029、INT-BLK-07 | `ITimer` seam 已存在，真正待决的是 cadence 与 event publish 边界 |
| INT-OQ-09 | memory residual | `sqlite-vss` concrete backend 是否需要在当前阶段进入实现与测试 | 延后到 optional backend 扩张 | INT-BLK-09 | 当前仅保留 unavailable baseline，不阻断 unary Gate |
| INT-OQ-10 | metrics / secret residual | OTLP exporter、KMS backend 何时进入真实依赖接入 | 延后到 optional backend 扩张 | INT-BLK-09 | 当前默认保持 `noop/prom_text` 与 file/backend seam，不宣称相关能力 ready |
| INT-OQ-11 | cli / diagnostics residual | `diag artifact_ref` 与更宽 log query/export scope 是否进入当前 CLI/daemon 运维面 | 延后 | 004、015、030 | 当前 CLI/daemon 仅保留 health/queue/threads 三类受控只读方向 |
| INT-OQ-12 | runtime / cognition residual | checkpoint 大版本迁移、人工干预 API、delegate hint、schema registry 是否进入当前里程碑 | 延后到 late-scope 演进 | 017 | 不阻断 unary 主线与 Access ingress 收口，但必须防止被误写为“已完成” |