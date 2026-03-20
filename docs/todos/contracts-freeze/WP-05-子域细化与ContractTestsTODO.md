# WP-05 子域细化与 Contract Tests TODO

最近更新时间：2026-03-20

## 1. 工作包目标

延续 WP-03/WP-04 的双轨交付纪律，在子域细化与测试治理阶段强制执行 Design + Build 成对交付，确保 V1 Ready 包可落地、可验证。

## 2. 研读资料基线

1. 架构资料：docs/architecture/DASSALL_Agent_architecture.md、docs/architecture/DASALL_Engineering_Blueprint.md。
2. 方案资料：docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md。
3. 决策资料：docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md、docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md、docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md。
4. 前置冻结包：WP-01 至 WP-04 冻结包与 checklist。

## 3. 完成标准

1. 每个 WP05-Txxx 都拆分为 WP05-Txxx-D（Design）和 WP05-Txxx-B（Build）两个子任务。
2. 每个 Build 子任务均具备代码目标、测试目标、验收命令三件套。
3. 子域对象可回溯到 WP-01 至 WP-04 骨架对象，不越权承担主链路职责。
4. Contract Tests 覆盖序列化稳定性、错误码/枚举兼容性、事件封套、ADR 边界字段。

## 4. Design->Build 拆分总表

| 主任务 | Design 子任务（文档交付） | Build 子任务（代码交付） | 输入依据 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|---|
| WP05-T001 | WP05-T001-D：子域推进顺序与并行边界 | WP05-T001-B：新增 DomainRolloutGuards 执行顺序守卫 | 计划文档第 8 阶段 4/5 | contracts/include/boundary/DomainRolloutGuards.h | tests/contract/smoke/DomainRolloutContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R DomainRolloutContractTest --output-on-failure |
| WP05-T002 | WP05-T002-D：ToolRequest 职责边界 | WP05-T002-B：新增 ToolRequest 契约对象与守卫 | WP-03、WP-04 冻结包 | contracts/include/tool/ToolRequest.h；contracts/include/tool/ToolRequestGuards.h | tests/contract/tool/ToolRequestContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolRequestContractTest --output-on-failure |
| WP05-T003 | WP05-T003-D：ToolResult 职责边界 | WP05-T003-B：新增 ToolResult 契约对象与守卫 | T002-D | contracts/include/tool/ToolResult.h；contracts/include/tool/ToolResultGuards.h | tests/contract/tool/ToolResultContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolResultContractTest --output-on-failure |
| WP05-T004 | WP05-T004-D：ToolDescriptor/ToolIR 分层说明 | WP05-T004-B：新增 ToolDescriptor 与 ToolIR 契约对象 | 架构 tools 章节 | contracts/include/tool/ToolDescriptor.h；contracts/include/tool/ToolIR.h | tests/contract/tool/ToolDescriptorIRContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolDescriptorIRContractTest --output-on-failure |
| WP05-T005 | WP05-T005-D：PromptSpec/PromptRelease 定义 | WP05-T005-B：新增 PromptSpec/PromptRelease 契约对象与守卫 | WP-04 Prompt 边界 | contracts/include/prompt/PromptSpec.h；contracts/include/prompt/PromptRelease.h；contracts/include/prompt/PromptReleaseGuards.h | tests/contract/prompt/PromptSpecReleaseContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R PromptSpecReleaseContractTest --output-on-failure |
| WP05-T006 | WP05-T006-D：Turn/Session/SummaryMemory 定义 | WP05-T006-B：新增对应 memory 契约对象与守卫 | WP-03 ContextPacket/Observation | contracts/include/memory/Turn.h；contracts/include/memory/Session.h；contracts/include/memory/SummaryMemory.h | tests/contract/memory/TurnSessionSummaryMemoryContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R TurnSessionSummaryMemoryContractTest --output-on-failure |
| WP05-T007 | WP05-T007-D：MemoryFact/ExperienceMemory 定义 | WP05-T007-B：新增对应 memory 契约对象与守卫 | 架构 memory 章节 | contracts/include/memory/MemoryFact.h；contracts/include/memory/ExperienceMemory.h | tests/contract/memory/MemoryFactExperienceContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MemoryFactExperienceContractTest --output-on-failure |
| WP05-T008 | WP05-T008-D：task 子域其余对象定义 | WP05-T008-B：新增 task 子域对象与越权守卫 | WP-04 多 Agent 边界 | contracts/include/task/TaskDomainContracts.h；contracts/include/task/TaskDomainGuards.h | tests/contract/task/TaskDomainContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R TaskDomainContractTest --output-on-failure |
| WP05-T009 | WP05-T009-D：event 子域事件类型设计 | WP05-T009-B：新增 EventType/EventPayload 契约对象与守卫 | WP-02 EventEnvelope | contracts/include/event/EventType.h；contracts/include/event/EventPayload.h；contracts/include/event/EventPayloadGuards.h | tests/contract/event/EventTypePayloadContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R EventTypePayloadContractTest --output-on-failure |
| WP05-T010 | WP05-T010-D：LLMRequest/LLMResponse 职责边界 | WP05-T010-B：新增 LLMRequest/LLMResponse 契约对象与守卫 | 架构 llm 章节、WP-04 Prompt 边界 | contracts/include/llm/LLMRequest.h；contracts/include/llm/LLMResponse.h；contracts/include/llm/LLMBoundaryGuards.h | tests/contract/llm/LLMRequestResponseContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R LLMRequestResponseContractTest --output-on-failure |
| WP05-T011 | WP05-T011-D：跨模块接口候选清单 | WP05-T011-B：新增 InterfaceCatalog 契约目录 | 计划文档阶段 5 | contracts/include/boundary/InterfaceCatalog.h | tests/contract/smoke/InterfaceCatalogContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R InterfaceCatalogContractTest --output-on-failure |
| WP05-T012 | WP05-T012-D：接口准入评估单 | WP05-T012-B：新增 InterfaceAdmissionGuards 准入守卫 | T011-D | contracts/include/boundary/InterfaceAdmissionGuards.h | tests/contract/smoke/InterfaceAdmissionContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R InterfaceAdmissionContractTest --output-on-failure |
| WP05-T013 | WP05-T013-D：序列化稳定性测试矩阵 | WP05-T013-B：实现序列化兼容 contract tests | 计划文档阶段 5 | tests/contract/serialization/SerializationCompatibilityContractTest.cpp | tests/contract/serialization/SerializationCompatibilityContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R SerializationCompatibilityContractTest --output-on-failure |
| WP05-T014 | WP05-T014-D：错误码/枚举兼容测试矩阵 | WP05-T014-B：实现错误码/枚举兼容 contract tests | WP-02 冻结包 | tests/contract/error/ErrorCodeEnumCompatibilityContractTest.cpp | tests/contract/error/ErrorCodeEnumCompatibilityContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ErrorCodeEnumCompatibilityContractTest --output-on-failure |
| WP05-T015 | WP05-T015-D：EventEnvelope 兼容测试矩阵 | WP05-T015-B：实现 EventEnvelope 兼容 contract tests | WP-02、event 子域设计 | tests/contract/event/EventEnvelopeCompatibilityContractTest.cpp | tests/contract/event/EventEnvelopeCompatibilityContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R EventEnvelopeCompatibilityContractTest --output-on-failure |
| WP05-T016 | WP05-T016-D：ADR 边界测试矩阵 | WP05-T016-B：实现 ADR 边界回归 contract tests | WP-04 冻结包 | tests/contract/smoke/ADRBoundaryRegressionContractTest.cpp | tests/contract/smoke/ADRBoundaryRegressionContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ADRBoundaryRegressionContractTest --output-on-failure |
| WP05-T017 | WP05-T017-D：Contract Tests 覆盖矩阵 | WP05-T017-B：新增 CoverageMatrixGuards 自动检查 | T013-D 至 T016-D | contracts/include/boundary/CoverageMatrixGuards.h | tests/contract/smoke/CoverageMatrixContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CoverageMatrixContractTest --output-on-failure |
| WP05-T018 | WP05-T018-D：版本变更模板 | WP05-T018-B：新增 VersionChangeSchema 与校验器 | 计划文档第 10 节 | contracts/include/boundary/VersionChangeSchema.h；contracts/include/boundary/VersionChangeGuards.h | tests/contract/smoke/VersionChangeSchemaContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R VersionChangeSchemaContractTest --output-on-failure |
| WP05-T019 | WP05-T019-D：contracts 变更评审流程清单 | WP05-T019-B：新增 BreakingReviewGuards 门禁守卫 | T018-D | contracts/include/boundary/BreakingReviewGuards.h | tests/contract/smoke/BreakingReviewContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R BreakingReviewContractTest --output-on-failure |
| WP05-T020 | WP05-T020-D：子域对象与测试评审纪要 | WP05-T020-B：固化评审结论为 V1ReadyChecklistGuards | T001-D 至 T019-D | contracts/include/boundary/V1ReadyChecklistGuards.h | tests/contract/smoke/V1ReadyChecklistContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R V1ReadyChecklistContractTest --output-on-failure |
| WP05-T021 | WP05-T021-D：M5/V1 Ready 冻结包文档 | WP05-T021-B：新增 WP05 CI 门禁脚本并接入 | T020-D、T020-B | scripts/ci/wp05_contract_gate.sh；tests/CMakeLists.txt（接入） | 复用 WP05 contract tests 全集 | bash scripts/ci/wp05_contract_gate.sh |

## 5. 原子任务状态清单（按子任务）

| 子任务 ID | 状态 | 任务描述 | 交付物 | 完成判定 |
|---|---|---|---|---|
| WP05-T001-D | Done | 制定子域推进顺序和并行边界 | deliverables/WP05-T001-子域推进顺序表.md | ✅ 新增四波 rollout 方案：Wave1 tool；Wave2 prompt+memory；Wave3 task+event；Wave4 llm；明确允许并行、禁止并行和越权禁区，D Gate=PASS |
| WP05-T001-B | Done | 新增子域推进顺序守卫 | DomainRolloutGuards.h + contract test | ✅ 新增 DomainRolloutGuards.h 与 DomainRolloutContractTest；61/61 contract tests passed（含 1/1 DomainRolloutContractTest，2026-03-19）；验证命令：cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R DomainRolloutContractTest --output-on-failure |
| WP05-T002-D | Done | 细化 ToolRequest 职责边界 | deliverables/WP05-T002-ToolRequest语义说明.md | ✅ 新增 ToolRequest 语义说明，冻结执行意图/约束复用/追溯锚点边界；明确禁止 result、error、budget snapshot、Prompt/provider、ToolDescriptor/ToolIR 语义进入请求对象；D Gate=PASS |
| WP05-T002-B | Done | 新增 ToolRequest 契约对象与守卫 | ToolRequest.h/Guards + contract test | ✅ 新增 contracts/include/tool/ToolRequest.h、contracts/include/tool/ToolRequestGuards.h、tests/contract/tool/ToolRequestContractTest.cpp，并接入 tests/contract/CMakeLists.txt；验证证据：ctest --test-dir build-ci -N -R ToolRequestContractTest 发现 1 个测试；cmake --build build-ci --target dasall_contract_tests 通过且 62/62 contract tests passed；ctest --test-dir build-ci -R ToolRequestContractTest --output-on-failure 1/1 通过（2026-03-20） |
| WP05-T003-D | Done | 细化 ToolResult 职责边界 | deliverables/WP05-T003-ToolResult语义说明.md | ✅ 新增 ToolResult 语义说明，冻结执行输出/Observation 折叠/恢复控制禁区边界；明确禁止 Observation ownership、runtime accounting、Prompt/provider、ToolDescriptor/ToolIR、recovery control 语义进入结果对象；D Gate=PASS |
| WP05-T003-B | Done | 新增 ToolResult 契约对象与守卫 | ToolResult.h/Guards + contract test | ✅ 新增 contracts/include/tool/ToolResult.h、contracts/include/tool/ToolResultGuards.h、tests/contract/tool/ToolResultContractTest.cpp，并接入 tests/contract/CMakeLists.txt；验证证据：ctest --test-dir build-ci -N -R ToolResultContractTest 发现 1 个测试；cmake --build build-ci --target dasall_contract_tests 通过且 63/63 contract tests passed；ctest --test-dir build-ci -R ToolResultContractTest --output-on-failure 1/1 通过（2026-03-20） |
| WP05-T004-D | Done | 细化 ToolDescriptor 与 ToolIR 分层 | deliverables/WP05-T004-ToolDescriptor-ToolIR说明.md | ✅ 新增 ToolDescriptor/ToolIR 分层说明，冻结注册能力面与归一化执行面边界；显式输出 Design->Build 映射与 B 三件套，D Gate=PASS |
| WP05-T004-B | Done | 新增 ToolDescriptor/ToolIR 契约对象 | ToolDescriptor.h/ToolIR.h + contract test | ✅ 新增 contracts/include/tool/ToolDescriptor.h、contracts/include/tool/ToolIR.h、tests/contract/tool/ToolDescriptorIRContractTest.cpp，并接入 tests/contract/CMakeLists.txt；验证证据：ctest --test-dir build-ci -N -R ToolDescriptorIRContractTest 发现 1 个测试；cmake --build build-ci --target dasall_contract_tests 通过且 64/64 contract tests passed；ctest --test-dir build-ci -R ToolDescriptorIRContractTest --output-on-failure 1/1 通过（2026-03-20） |
| WP05-T005-D | Done | 细化 PromptSpec 与 PromptRelease 对象 | deliverables/WP05-T005-PromptSpec-PromptRelease定义.md | ✅ 新增 PromptSpec/PromptRelease 定义文档，冻结稳定选择面与版本化发布面边界；显式给出 Design->Build 映射、Build 三件套与 D Gate=PASS；不回改 PromptComposeRequest/Result |
| WP05-T005-B | Done | 新增 PromptSpec/PromptRelease 契约对象与守卫 | PromptSpec.h/PromptRelease.h/Guards + contract test | ✅ 新增 contracts/include/prompt/PromptSpec.h、contracts/include/prompt/PromptRelease.h、contracts/include/prompt/PromptReleaseGuards.h、tests/contract/prompt/PromptSpecReleaseContractTest.cpp，并接入 tests/contract/CMakeLists.txt；验证证据：首次 `ctest --test-dir build-ci -N -R PromptSpecReleaseContractTest` 在构建前返回 0，构建后复核发现 1 个测试；`cmake --build build-ci --target dasall_contract_tests` 通过且 65/65 contract tests passed；`ctest --test-dir build-ci -R PromptSpecReleaseContractTest --output-on-failure` 1/1 通过（2026-03-20） |
| WP05-T006-D | Done | 细化 Turn/Session/SummaryMemory 对象 | deliverables/WP05-T006-Memory对象定义.md | ✅ 新增 memory 对象定义文档，冻结 Turn 单轮记录面、Session 会话索引面、SummaryMemory 结构化摘要沉淀面；明确与 SessionContext/ContextPacket/Checkpoint 分层、Design->Build 映射、Build 三件套与 D Gate=PASS |
| WP05-T006-B | Done | 新增 Turn/Session/SummaryMemory 契约对象与守卫 | Turn.h/Session.h/SummaryMemory.h + contract test | ✅ 新增 contracts/include/memory/Turn.h、contracts/include/memory/Session.h、contracts/include/memory/SummaryMemory.h、tests/contract/memory/TurnSessionSummaryMemoryContractTest.cpp，并接入 tests/contract/CMakeLists.txt；验证证据：构建前 `ctest --test-dir build-ci -N -R TurnSessionSummaryMemoryContractTest` 返回 Total Tests: 0；`cmake --build build-ci --target dasall_contract_tests` 通过且 66/66 contract tests passed；构建后 `ctest --test-dir build-ci -N -R TurnSessionSummaryMemoryContractTest` 发现 1 个测试；`ctest --test-dir build-ci -R TurnSessionSummaryMemoryContractTest --output-on-failure` 1/1 通过（2026-03-20） |
| WP05-T007-D | Done | 细化 MemoryFact/ExperienceMemory 对象 | deliverables/WP05-T007-记忆事实对象定义.md | ✅ 新增 MemoryFact/ExperienceMemory 设计文档，冻结事实沉淀面与经验写回面边界；显式给出 Design->Build 映射、Build 三件套与 D Gate=PASS；明确阻断 SessionContext/runtime/provider/checkpoint 越权语义 |
| WP05-T007-B | Done | 新增 MemoryFact/ExperienceMemory 契约对象与守卫 | MemoryFact.h/ExperienceMemory.h + contract test | ✅ 新增 contracts/include/memory/MemoryFact.h、contracts/include/memory/ExperienceMemory.h、tests/contract/memory/MemoryFactExperienceContractTest.cpp，并接入 tests/contract/CMakeLists.txt；验证证据：ctest --test-dir build-ci -N -R MemoryFactExperienceContractTest 发现 1 个测试；cmake --build build-ci --target dasall_contract_tests 通过且 67/67 contract tests passed；ctest --test-dir build-ci -R MemoryFactExperienceContractTest --output-on-failure 1/1 通过（2026-03-20） |
| WP05-T008-D | Done | 细化 task 子域其余对象 | deliverables/WP05-T008-任务子域对象定义.md | ✅ 新增任务子域对象定义文档，冻结 WorkerTask/WorkerLease/SubTaskGraph 的 task 子域目录与越权禁区；显式给出 Design->Build 映射、Build 三件套与 D Gate=PASS（2026-03-20） |
| WP05-T008-B | Done | 新增 task 子域对象与越权守卫 | TaskDomainContracts.h/Guards + contract test | ✅ 新增 contracts/include/task/TaskDomainContracts.h、contracts/include/task/TaskDomainGuards.h、tests/contract/task/TaskDomainContractTest.cpp，并接入 tests/contract/CMakeLists.txt；验证证据：ctest --test-dir build-ci -N -R TaskDomainContractTest 发现 1 个测试；cmake --build build-ci --target dasall_contract_tests 通过且 68/68 contract tests passed；ctest --test-dir build-ci -R TaskDomainContractTest --output-on-failure 1/1 通过（2026-03-20） |
| WP05-T009-D | Done | 细化 event 子域事件类型 | deliverables/WP05-T009-EventType-EventPayload设计稿.md | ✅ 新增 EventType/EventPayload 设计稿，冻结事件类型识别面与 payload 承载面边界；显式给出 Design->Build 映射、Build 三件套与 D Gate=PASS（2026-03-20） |
| WP05-T009-B | Done | 新增 EventType/EventPayload 契约对象与守卫 | EventType.h/EventPayload.h/Guards + contract test | ✅ 新增 contracts/include/event/EventType.h、contracts/include/event/EventPayload.h、contracts/include/event/EventPayloadGuards.h、tests/contract/event/EventTypePayloadContractTest.cpp，并接入 tests/contract/CMakeLists.txt；验证证据：ctest --test-dir build-ci -N -R EventTypePayloadContractTest 发现 1 个测试；cmake --build build-ci --target dasall_contract_tests 通过且 69/69 contract tests passed；ctest --test-dir build-ci -R EventTypePayloadContractTest --output-on-failure 1/1 通过（2026-03-20） |
| WP05-T010-D | Not Started | 细化 LLMRequest/LLMResponse 职责边界 | deliverables/WP05-T010-LLMRequest-LLMResponse说明.md | 不泄漏 provider 私有字段到主链路 |
| WP05-T010-B | Not Started | 新增 LLMRequest/LLMResponse 契约对象与守卫 | LLMRequest.h/LLMResponse.h/LLMBoundaryGuards.h + contract test | 共享对象污染可阻断 |
| WP05-T011-D | Not Started | 识别进入 contracts 的接口候选 | deliverables/WP05-T011-接口候选清单.md | 仅保留稳定依赖面 |
| WP05-T011-B | Not Started | 新增 InterfaceCatalog 契约目录 | InterfaceCatalog.h + contract test | 候选目录可程序化审查 |
| WP05-T012-D | Not Started | 评估接口候选必要性和边界 | deliverables/WP05-T012-接口准入评估单.md | 保留/推迟/退回结论明确 |
| WP05-T012-B | Not Started | 新增 InterfaceAdmissionGuards 准入守卫 | InterfaceAdmissionGuards.h + contract test | 准入规则可自动执行 |
| WP05-T013-D | Not Started | 定义序列化稳定性测试矩阵 | deliverables/WP05-T013-序列化测试矩阵.md | 覆盖核心对象序列化兼容 |
| WP05-T013-B | Not Started | 实现序列化兼容 contract tests | SerializationCompatibilityContractTest.cpp | round-trip 与兼容断言通过 |
| WP05-T014-D | Not Started | 定义错误码与枚举兼容测试矩阵 | deliverables/WP05-T014-错误码枚举测试矩阵.md | 可捕获 breaking 枚举变更 |
| WP05-T014-B | Not Started | 实现错误码/枚举兼容 contract tests | ErrorCodeEnumCompatibilityContractTest.cpp | 错误语义漂移可检测 |
| WP05-T015-D | Not Started | 定义 EventEnvelope 兼容测试矩阵 | deliverables/WP05-T015-事件测试矩阵.md | 覆盖头部稳定与 payload 扩展约束 |
| WP05-T015-B | Not Started | 实现 EventEnvelope 兼容 contract tests | EventEnvelopeCompatibilityContractTest.cpp | 兼容违规可自动检测 |
| WP05-T016-D | Not Started | 定义 ADR 约束对象边界测试矩阵 | deliverables/WP05-T016-ADR边界测试矩阵.md | 可捕获 ContextPacket/ReflectionDecision/MultiAgentResult 越界 |
| WP05-T016-B | Not Started | 实现 ADR 边界回归 contract tests | ADRBoundaryRegressionContractTest.cpp | ADR 边界回退可阻断 |
| WP05-T017-D | Not Started | 汇总 tests/contract 覆盖矩阵 | deliverables/WP05-T017-ContractTests覆盖矩阵.md | 高风险对象至少一种契约测试 |
| WP05-T017-B | Not Started | 新增 CoverageMatrixGuards 自动检查 | CoverageMatrixGuards.h + contract test | 覆盖缺口可自动发现 |
| WP05-T018-D | Not Started | 建立版本变更记录模板 | deliverables/WP05-T018-版本变更模板.md | breaking 与 non-breaking 区分明确 |
| WP05-T018-B | Not Started | 新增 VersionChangeSchema 与校验器 | VersionChangeSchema.h/Guards + contract test | 模板可程序化校验 |
| WP05-T019-D | Not Started | 建立 contracts 变更评审流程清单 | deliverables/WP05-T019-变更流程清单.md | breaking change 必须触发专门评审 |
| WP05-T019-B | Not Started | 新增 BreakingReviewGuards 门禁守卫 | BreakingReviewGuards.h + contract test | 评审门禁可自动执行 |
| WP05-T020-D | Not Started | 组织子域对象与测试评审 | deliverables/WP05-T020-评审纪要.md | 细化不回改 WP01-WP04 结论 |
| WP05-T020-B | Not Started | 固化评审结论为 V1 Ready checklist 守卫 | V1ReadyChecklistGuards.h + contract test | 评审结论可程序化执行 |
| WP05-T021-D | Not Started | 发布 contracts V1 Ready 冻结包 | deliverables/WP05-T021-M5冻结包.md | 允许后续模块以 V1 contracts 为基线 |
| WP05-T021-B | Not Started | 新增并接入 WP05 CI 门禁脚本 | scripts/ci/wp05_contract_gate.sh | gate 返回码符合预期 |

## 6. 推荐执行顺序

1. 先做子域对象链：T001-D/B -> T010-D/B。
2. 再做接口准入链：T011-D/B -> T012-D/B。
3. 再做测试治理链：T013-D/B -> T019-D/B。
4. 最后做评审与发布链：T020-D/B -> T021-D/B。

## 7. 阻塞项与解阻条件

1. BLK-01：只完成子域设计说明，未同步落盘对象与 contract tests。  
解阻条件：每个 Txxx-D 进入 In Review 前，Txxx-B 至少完成代码骨架和测试骨架提交。
2. BLK-02：WP04 边界对象未冻结导致子域细化口径不稳定。  
解阻条件：WP04-T024-D/B 完成并发布 M4 冻结包。
3. BLK-03：序列化与兼容测试矩阵未转为自动化 tests。  
解阻条件：T013-B 至 T016-B 至少完成首版 contract tests 并接入 ctest。
4. BLK-04：CMake/CTest 无法执行新增 WP05 tests。  
解阻条件：cmake -S . -B build-ci -G Ninja 成功，且 ctest --test-dir build-ci -L contract 可执行。

## 8. 依赖与风险

1. 若子域对象细化阶段回改主链路或边界对象，应退回对应前序工作包处理。
2. 若 Contract Tests 不围绕真实消费方依赖设计，会导致字段刚性耦合和演进停滞。
3. 若过早把 provider 或序列化技术细节写入共享对象，contracts 稳定性会显著下降。
4. 若 T017/T019 缺少 Build 门禁守卫，V1 Ready 评审无法形成可执行规则。
