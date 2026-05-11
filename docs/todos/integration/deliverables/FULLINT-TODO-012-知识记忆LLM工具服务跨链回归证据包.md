# FULLINT-TODO-012 知识/记忆/LLM/工具服务跨链回归证据包

日期：2026-05-11
任务：FULLINT-TODO-012 执行知识/记忆/LLM/工具服务跨链回归
状态：已完成

## 1. 任务边界

本轮只推进 `FULLINT-TODO-012`，不代替 `FULLINT-TODO-013` 的 installed-package 全矩阵，也不外推 release runner / qemu / lintian 结论。

目标链路为：

1. `KnowledgeServiceFacade` 通过真实 SQLite FTS lexical snapshot 产出 `EvidenceBundle` 与 `RetrievalEvidenceRef`。
2. `MemoryManager` 消费外部证据和 evidence refs，组装 `ContextPacket`，保持 ADR-006 的上下文拥有权边界。
3. `PromptComposer` 只把 `ContextPacket` 的稳定投影映射为 provider-neutral `PromptComposeResult.messages`，不重新检索 knowledge / memory。
4. provider handoff 通过 `ILLMAdapter` 适配器面验证 composed messages 被真实转交，保持 provider-neutral request 语义。
5. `ToolManager -> BuiltinExecutorLane -> IExecutionService` 进入 Capability Services loopback 的真实 lane / adapter / mapper，产出 `ToolInvocationEnvelope`、`Observation` 与 `ObservationDigest`。
6. negative path 必须 fail-closed：过期 knowledge snapshot 或缺失 tool descriptor 不能伪造 evidence、provider call、ObservationDigest。

## 2. Design Gate

| 编号 | 设计判断 | 证据 | Build 落点 |
|---|---|---|---|
| D1 | 不复用既有 focused test 作为完成证据，新增 full-business-chain 独立回归 | 用户要求不得依赖现有单测/集测完成状态；FULLINT-011 已形成新增跨链测试先例 | `tests/integration/full_business_chain/FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest.cpp` |
| D2 | Memory 只消费 evidence projection，不直接拥有 prompt / provider / tool 执行 | ADR-006、memory 详设 §1.1/§1.2 | `MemoryContextRequest.external_evidence`、`retrieval_evidence_refs` -> `ContextPacket` guards |
| D3 | PromptComposer 只负责消息装配，provider request 由 LLM request 面承接 | ADR-006 §3.3/§6.3；`PromptComposeResult` guards | `PromptComposeRequest.tags` 承载 context-derived refs，`PromptComposeResult.messages` 交给 `MockLLMAdapter` |
| D4 | Tool 不直连 platform，必须经 services 稳定门面 | tools 详设 TOOL-C012；services 详设 CAP-C001/C002 | `BuiltinExecutorLaneDependencies.execution_service = CapabilityServicesLoopbackFixture::execution_service()` |
| D5 | Gate-INT-04/06/07 必须直接构建并运行新增跨链测试 | TODO acceptance 写明三个 gate target + focused ctest matrix | CTest label `gate-int-04;gate-int-06;gate-int-07;fullint-012`，并加入三个 gate target 依赖 |
| D6 | installed-package 证据只记录真实本机输出，不伪造 knowledge/tools installed ready | `sudo -n dasall ping/readiness` 已能返回 READY；`run` 后续单独记录 LLM origin | 本证据包安装态章节 |

Design Gate 结论：通过。当前最小实现应新增一条独立 full-business-chain 回归测试，并把它接入 Gate-INT-04/06/07 与 full-business-chain discoverability。

## 3. Build 计划

| 代码目标 | 测试目标 | 验收命令 |
|---|---|---|
| 新增 FULLINT-012 跨链集成测试 | 正例覆盖 evidence -> context -> prompt -> provider -> services -> observation digest；负例覆盖 stale evidence 与 missing descriptor fail-closed | `Build_CMakeTools(buildTargets=["dasall_fullint_012_knowledge_memory_llm_tools_services_cross_chain"])` + `RunCtest_CMakeTools(tests=["FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest"])` |
| 接入 full_business_chain CMake | 新测试可被 CTest 发现并带 `full-business-chain/fullint-012/gate-int-04/06/07` 标签 | `ListTests_CMakeTools` / focused CTest |
| 接入 Gate-INT-04/06/07 与 discoverability | 三个 gate target 不再只覆盖既有 focused slices | `Build_CMakeTools(buildTargets=["dasall_gate_int_04","dasall_gate_int_06","dasall_gate_int_07"])` |
| 记录 installed-package 真实结果 | 本机 `/usr/bin/dasall`、dpkg version、ping/readiness、run output | `sudo -n dasall ping/readiness/run ... --json` |

## 4. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_fullint_012_knowledge_memory_llm_tools_services_cross_chain"])`
	- 结果：通过；新增 full-business-chain test target 构建成功。
2. `RunCtest_CMakeTools(tests=["FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest"])`
	- 结果：通过；1/1 passed。
	- 覆盖：正向链路 `KnowledgeServiceFacade -> MemoryManager -> PromptComposer -> ILLMAdapter -> ToolManager/BuiltinExecutorLane -> CapabilityServicesLoopbackFixture -> ObservationDigest`；负向链路 stale knowledge snapshot 与 missing descriptor 均 fail-closed。
3. `Build_CMakeTools(buildTargets=["dasall_gate_int_04","dasall_gate_int_06","dasall_gate_int_07"])`
	- 结果：通过。
	- `gate-int-04`：`RuntimeEvidenceProjectionIntegrationTest`、`KnowledgeEvidencePreservationTest`、`FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest` 均 passed。
	- `gate-int-06`：`RuntimeRequiredOptionalPortsIntegrationTest`、`RuntimeProfileCompatibilityTest`、`LLMSubsystemSmokeIntegrationTest`、`FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest` 均 passed。
	- `gate-int-07`：`BuiltinExecutorLaneResultCodeTest`、`ServiceResultSemanticsContractTest`、`FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest` 均 passed。
4. `RunCtest_CMakeTools(tests=["FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest","KnowledgeEvidencePreservationTest","MemoryContextAssembleIntegrationTest","LLMSubsystemSmokeIntegrationTest","ToolServicesSmokeIntegrationTest","CapabilityServicesSmokeIntegrationTest"])`
	- 结果：通过；相邻 focused matrix 均 passed。
5. `Build_CMakeTools(buildTargets=["dasall_full_business_chain_discoverability"])`
	- 结果：通过；discoverability 清单已包含 `FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest`。
6. `get_errors` on changed C++ / CMake files
	- 结果：无 VS Code diagnostics。
7. `git diff --check`
	- 结果：通过；无尾随空格或 patch 格式问题。

## 5. 安装态证据

已完成预探针：

1. `command -v dasall`：`/usr/bin/dasall`。
2. `dpkg-query -W -f='${Package} ${Version}\n' dasall`：`dasall 0.1.0-1`。
3. `sudo -n dasall ping --json`：PASS；返回 `readiness=READY`。
4. `sudo -n dasall readiness --json`：PASS；返回 `state=READY`、`lifecycle_ready=true`、`listener_ready=true`、`gateway_ready=true`、`bridge_reachable=true`。
5. `sudo -n dasall run '{"prompt":"FULLINT-012 installed package cross-chain probe: answer with one concise sentence."}' --request-id fullint-012-installed-1778462558 --trace-id trace-fullint-012-installed-1778462558 --json --timeout-ms 120000`
	- 结果：PASS；返回 `disposition=completed`、`task_completed=true`、`exit_code=0`。
	- `response_text` 包含 `llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop`。

安装态边界：本轮 installed-package 只证明控制面 READY 与真实 LLM provider path 可运行；knowledge installed-package 正向 retrieve/refresh/health、runtime production tool caller 与 package 全矩阵仍分别归 `FULLINT-TODO-014`、`FULLINT-TODO-016`、`FULLINT-TODO-013`。

## 6. 当前结论

`FULLINT-TODO-012` 已完成。

Build-tree 证据已经证明：`EvidenceBundle` / `RetrievalEvidenceRef` 通过 `ContextPacket`、`PromptComposeResult`、provider-neutral `LLMRequest/LLMResponse`、`ToolInvocationEnvelope` 到 `ObservationDigest` 的投影没有丢失关键 citation / source / route / digest 语义；stale knowledge snapshot 与 missing tool descriptor 均不会伪造 evidence 或 observation digest。

Installed-package 证据保持 L4 partial：本机安装包可 `ping/readiness`，且 `dasall run` 真实走 DeepSeek-compatible provider 并返回 completed；但本轮不声明 installed knowledge/tools 正向入口 ready。