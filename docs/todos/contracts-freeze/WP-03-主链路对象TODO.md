# WP-03 主链路对象 TODO

最近更新时间：2026-03-17（T018 完成，WP-03 全部完成）

## 1. 工作包目标

从 WP-03 开始，所有主链路任务按 Design + Build 双轨交付执行，避免只产出语义文档而遗漏代码落地。

## 2. 研读资料基线

1. 架构资料：docs/architecture/DASSALL_Agent_architecture.md、docs/architecture/DASALL_Engineering_Blueprint.md。
2. 方案资料：docs/plans/DASALL_contracts冻结实施计划.md、docs/plans/DASALL_工程落地实现步骤指引.md。
3. 决策资料：docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md、docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md、docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md。
4. 前置冻结包：WP-01、WP-02 交付物与 review checklist。

## 3. 完成标准

1. 每个 WP03-Txxx 都拆分为 WP03-Txxx-D（Design）和 WP03-Txxx-B（Build）两个子任务。
2. 每个 Build 子任务均具备代码目标、测试目标、验收命令三件套。
3. AgentRequest -> GoalContract -> ContextPacket -> Observation -> ObservationDigest -> BeliefState -> Checkpoint -> AgentResult 可形成可编译、可测试闭环。
4. ContextPacket、Observation、Checkpoint 边界不与 Prompt、Recovery、Worker 子域混淆。

## 4. Design->Build 拆分总表

| 主任务 | Design 子任务（文档交付） | Build 子任务（代码交付） | 输入依据 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|---|
| WP03-T001 | WP03-T001-D：主链路对象依赖顺序说明 | WP03-T001-B：新增 MainFlowContracts 聚合入口 | WP-01/02 冻结包 | contracts/include/agent/MainFlowContracts.h | tests/contract/smoke/MainFlowContractsSmokeTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MainFlowContractsSmokeTest --output-on-failure |
| WP03-T002 | WP03-T002-D：AgentRequest 语义边界说明 | WP03-T002-B：新增 AgentRequest 最小契约对象与边界守卫 | 架构入口链路、ADR-008 | contracts/include/agent/AgentRequest.h；contracts/include/agent/AgentRequestGuards.h | tests/contract/agent/AgentRequestContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R AgentRequestContractTest --output-on-failure |
| WP03-T003 | WP03-T003-D：AgentRequest 字段必选/可选表 | WP03-T003-B：补齐 AgentRequest 字段合法性校验 | T002-D、WP-02 规则 | contracts/include/agent/AgentRequestGuards.h | tests/contract/agent/AgentRequestFieldContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R AgentRequestFieldContractTest --output-on-failure |
| WP03-T004 | WP03-T004-D：GoalContract 职责边界说明 | WP03-T004-B：新增 GoalContract 契约对象与守卫 | 架构文档、计划文档 | contracts/include/agent/GoalContract.h；contracts/include/agent/GoalContractGuards.h | tests/contract/agent/GoalContractContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R GoalContractContractTest --output-on-failure |
| WP03-T005 | WP03-T005-D：GoalContract 字段表与约束规则 | WP03-T005-B：补齐 GoalContract 字段一致性校验 | T004-D | contracts/include/agent/GoalContractGuards.h | tests/contract/agent/GoalContractFieldContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R GoalContractFieldContractTest --output-on-failure |
| WP03-T006 | WP03-T006-D：Observation 统一折叠语义说明 | WP03-T006-B：新增 Observation 契约对象 | 架构观测链路 | contracts/include/observation/Observation.h | tests/contract/observation/ObservationContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ObservationContractTest --output-on-failure |
| WP03-T007 | WP03-T007-D：Observation 来源分类与引用规则 | WP03-T007-B：新增 ObservationSource 与引用校验器 | T006-D、WP-02 ErrorSource | contracts/include/observation/ObservationSource.h；contracts/include/observation/ObservationSourceGuards.h | tests/contract/observation/ObservationSourceContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ObservationSourceContractTest --output-on-failure |
| WP03-T008 | WP03-T008-D：ObservationDigest 分层边界说明 | WP03-T008-B：新增 ObservationDigest 契约对象与分层守卫 | T006-D | contracts/include/observation/ObservationDigest.h；contracts/include/observation/ObservationDigestGuards.h | tests/contract/observation/ObservationDigestBoundaryContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ObservationDigestBoundaryContractTest --output-on-failure |
| WP03-T009 | WP03-T009-D：BeliefState 主链路定位说明 | WP03-T009-B：新增 BeliefState 契约对象与禁入守卫 | 架构认知链路 | contracts/include/agent/BeliefState.h；contracts/include/agent/BeliefStateGuards.h | tests/contract/agent/BeliefStateContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R BeliefStateContractTest --output-on-failure |
| WP03-T010 | WP03-T010-D：ContextPacket 语义组成说明 | WP03-T010-B：收敛 ContextPacket 主链路契约对象 | ADR-006、WP-01 核对单 | contracts/include/context/ContextPacket.h | tests/contract/context/ContextPacketMainFlowContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ContextPacketMainFlowContractTest --output-on-failure |
| WP03-T011 | WP03-T011-D：ContextPacket 必选块字段表 | WP03-T011-B：补齐 ContextPacket 组成块校验器 | T010-D | contracts/include/context/ContextPacketGuards.h | tests/contract/context/ContextPacketFieldContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ContextPacketFieldContractTest --output-on-failure |
| WP03-T012 | WP03-T012-D：Checkpoint 最小恢复语义说明 | WP03-T012-B：新增 Checkpoint 契约对象与恢复边界守卫 | 架构恢复链路、ADR-007 | contracts/include/checkpoint/Checkpoint.h；contracts/include/checkpoint/CheckpointGuards.h | tests/contract/checkpoint/CheckpointContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CheckpointContractTest --output-on-failure |
| WP03-T013 | WP03-T013-D：Checkpoint 恢复必需字段表 | WP03-T013-B：补齐 Checkpoint 字段完整性校验 | T012-D | contracts/include/checkpoint/CheckpointGuards.h | tests/contract/checkpoint/CheckpointFieldContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CheckpointFieldContractTest --output-on-failure |
| WP03-T014 | WP03-T014-D：AgentResult 最小输出语义说明 | WP03-T014-B：新增 AgentResult 契约对象与输出守卫 | 架构输出链路 | contracts/include/agent/AgentResult.h；contracts/include/agent/AgentResultGuards.h | tests/contract/agent/AgentResultContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R AgentResultContractTest --output-on-failure |
| WP03-T015 | WP03-T015-D：单 Agent 主链路对象流图 | WP03-T015-B：新增主链路端到端契约冒烟测试 | T002-D 至 T014-D | tests/contract/e2e/MainFlowContractE2ETest.cpp | tests/contract/e2e/MainFlowContractE2ETest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MainFlowContractE2ETest --output-on-failure |
| WP03-T016 | WP03-T016-D：职责重叠检查单 | WP03-T016-B：新增主链路重叠自动检查守卫 | T002-D 至 T015-D | contracts/include/boundary/MainFlowOverlapGuards.h | tests/contract/smoke/MainFlowOverlapContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MainFlowOverlapContractTest --output-on-failure |
| WP03-T017 | WP03-T017-D：主链路对象评审纪要 | WP03-T017-B：将评审结论固化为 WP03 gate 检查项 | T015-D、T016-D | contracts/include/boundary/M3ChecklistGuards.h | tests/contract/smoke/M3ChecklistContractTest.cpp | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R M3ChecklistContractTest --output-on-failure |
| WP03-T018 | WP03-T018-D：M3 冻结包文档 | WP03-T018-B：新增 WP03 CI 门禁脚本并接入流水线 | T017-D、T017-B | scripts/ci/wp03_contract_gate.sh；tests/CMakeLists.txt（接入） | 复用 WP03 contract tests 全集 | bash scripts/ci/wp03_contract_gate.sh |

## 5. 原子任务状态清单（按子任务）

| 子任务 ID | 状态 | 任务描述 | 交付物 | 完成判定 |
|---|---|---|---|---|
| WP03-T001-D | Done | 明确主链路对象全集与依赖顺序 | deliverables/WP03-T001-主链路对象依赖表.md（含本地+外部证据链、D Gate） | 顺序覆盖 AgentRequest 到 AgentResult |
| WP03-T001-B | Done | 新增主链路对象聚合入口头文件 | contracts/include/agent/MainFlowContracts.h + tests/contract/smoke/MainFlowContractsSmokeTest.cpp | include 可编译且测试通过 |
| WP03-T002-D | Done | 定义 AgentRequest 最小语义边界 | deliverables/WP03-T002-AgentRequest语义说明.md（含研究证据链、D Gate） | 不夹带 runtime/provider 私有状态 |
| WP03-T002-B | Done | 新增 AgentRequest 契约对象与边界守卫 | AgentRequest.h + AgentRequestGuards.h + AgentRequestContractTest.cpp | 越权字段被拦截，22/22 contract tests passed |
| WP03-T003-D | Done | 列出 AgentRequest 字段必选/可选规则 | deliverables/WP03-T003-AgentRequest字段表.md（含字段分组、必选/可选规则、禁止字段、D Gate） | 字段规则可执行判定 |
| WP03-T003-B | Done | 实现 AgentRequest 字段级校验器 | AgentRequestGuards.h（validate_agent_request_field_rules）+ AgentRequestFieldContractTest.cpp（4正例+17负例） | 必填缺失与非法组合可被拦截，23/23 contract tests passed |
| WP03-T004-D | Done | 定义 GoalContract 职责边界 | deliverables/WP03-T004-GoalContract语义说明.md（含五职责语义、必填/可选字段、枚举定义、禁止字段、D Gate） | 成功判据/约束/预算/审批策略齐全 |
| WP03-T004-B | Done | 新增 GoalContract 契约对象与守卫 | GoalContract.h + GoalContractGuards.h + GoalContractContractTest.cpp（4正例+14负例） | 边界语义可测试，24/24 contract tests passed |
| WP03-T005-D | Done | 列出 GoalContract 必填字段与约束表达 | deliverables/WP03-T005-GoalContract字段表.md（含字段分组、三层校验设计、budget 维度规则、D Gate） | 不依赖自然语言补充解释 |
| WP03-T005-B | Done | 实现 GoalContract 字段一致性校验器 | GoalContractGuards.h（validate_goal_contract_field_rules）+ GoalContractFieldContractTest.cpp（4正例+10负例） | 目标约束可程序化校验，25/25 contract tests passed |
| WP03-T006-D | Done | 定义 Observation 统一折叠语义 | deliverables/WP03-T006-Observation语义说明.md（含六职责语义、5必填+8可选字段、ObservationSource枚举、禁止字段、D Gate） | 统一承载四类来源输出 |
| WP03-T006-B | Done | 新增 Observation 契约对象 | Observation.h + ObservationGuards.h + ObservationContractTest.cpp（4正例+14负例） | 结构可编译且样例通过，26/26 contract tests passed |
| WP03-T007-D | Done | 列出 Observation 来源分类与引用规则 | deliverables/WP03-T007-Observation分类表.md（含枚举冻结决策、source→correlation映射规则7条、ErrorSourceRef对齐、Layer 3守卫规则、D Gate） | 来源类型可直接消费 |
| WP03-T007-B | Done | 实现 ObservationSource 与引用校验器 | ObservationSource.h + ObservationSourceGuards.h + ObservationSourceContractTest.cpp（5正例+14负例） | 来源引用规则可自动验证，27/27 contract tests passed |
| WP03-T008-D | Done | 定义 ObservationDigest 与 Observation 分层边界 | deliverables/WP03-T008-ObservationDigest边界说明.md（含5必填+4可选字段、互斥字段表11项、分层对称性、消费者清单、7条守卫规则、D Gate） | Digest/Observation 分层清晰 |
| WP03-T008-B | Done | 新增 ObservationDigest 分层守卫 | ObservationDigest.h + ObservationDigestGuards.h + ObservationDigestBoundaryContractTest.cpp（4正例+14负例） | 推理层与执行层字段隔离，28/28 contract tests passed |
| WP03-T009-D | Done | 定义 BeliefState 在主链路位置 | deliverables/WP03-T009-BeliefState语义说明.md（含6必填+3可选字段、10类禁止字段、8消费者/下游、8条守卫规则、D Gate） | 明确非入口/非恢复快照 |
| WP03-T009-B | Done | 新增 BeliefState 契约对象与禁入守卫 | BeliefState.h + BeliefStateGuards.h + BeliefStateContractTest.cpp（4正例+14负例） | 越界字段被拒绝，29/29 contract tests passed |
| WP03-T010-D | Done | 定义 ContextPacket 语义组成 | deliverables/WP03-T010-ContextPacket语义说明.md（含4必填+9可选字段、ADR-006 §6.1全10槽位覆盖、24类禁止字段、1生产者+8消费者、D Gate） | 不含消息渲染内容 |
| WP03-T010-B | Done | 新增 ContextPacket 主链路契约对象 | ContextPacket.h + ContextPacketMainFlowContractTest.cpp | 与 ADR-006 边界一致 |
| WP03-T011-D | Done | 列出 ContextPacket 必选组成块 | deliverables/WP03-T011-ContextPacket字段表.md（含4必填+9可选字段分组、三层堆叠设计、L1/L2/L3校验规则、D Gate） | 目标/摘要/记忆知识预算覆盖 |
| WP03-T011-B | Done | 实现 ContextPacket 组成块校验器 | ContextPacketGuards.h + ContextPacketFieldContractTest.cpp（4正例+14负例） | 必选块缺失可检测，31/31 contract tests passed |
| WP03-T012-D | Done | 定义 Checkpoint 最小恢复语义 | deliverables/WP03-T012-Checkpoint语义说明.md（含5必填+6可选字段、CheckpointState枚举、9类禁止字段、4消费者、2层守卫设计、D Gate） | 最小恢复状态闭合 |
| WP03-T012-B | Done | 新增 Checkpoint 契约对象与恢复边界守卫 | Checkpoint.h + CheckpointGuards.h + CheckpointContractTest.cpp（4正例+14负例） | 不退化为无限快照，32/32 contract tests passed |
| WP03-T013-D | Done | 列出 Checkpoint 恢复必需字段 | deliverables/WP03-T013-Checkpoint字段表.md（含5R+6O字段分组、三层堆叠L3设计、state→pending_action一致性规则、D Gate） | 状态引用/进度/预算/子域入口齐全 |
| WP03-T013-B | Done | 实现 Checkpoint 字段完整性校验 | CheckpointGuards.h（validate_checkpoint_field_rules）+ CheckpointFieldContractTest.cpp（4正例+10负例） | 恢复必需字段缺失可阻断，33/33 contract tests passed |
| WP03-T014-D | Done | 定义 AgentResult 最小输出语义 | deliverables/WP03-T014-AgentResult语义说明.md（含6R+7O字段、AgentResultStatus枚举6值、架构§5.1全映射、禁止字段11类、两层守卫、D Gate） | 输出语义不暴露内部细节 |
| WP03-T014-B | Done | 新增 AgentResult 契约对象与守卫 | AgentResult.h + AgentResultGuards.h + AgentResultContractTest.cpp（4正例+14负例） | 最终结果/状态/摘要/引用可校验，34/34 contract tests passed |
| WP03-T015-D | Done | 绘制单 Agent 主链路对象流转图 | deliverables/WP03-T015-主流程对象流图.md（含8节点流转总图、关联字段8边详表、生产者/消费者映射、端到端示例、Design→Build映射、D Gate） | 从入口到结果闭环 |
| WP03-T015-B | Done | 新增主链路端到端契约冒烟测试 | MainFlowContractE2ETest.cpp（4正例+5负例，覆盖全链路Guard、关联一致性、断裂检测） | 主链路样例端到端通过 |
| WP03-T016-D | Done | 进行主链路对象职责重叠检查 | deliverables/WP03-T016-职责重叠检查单.md（含8×5互斥矩阵、4组对称互斥、域归属唯一性、D Gate） | 无对象跨 Prompt/Recovery/Worker 职责 |
| WP03-T016-B | Done | 新增主链路重叠自动检查守卫 | MainFlowOverlapGuards.h + MainFlowOverlapContractTest.cpp（4正例+5负例） | 职责重叠可自动阻断，36/36 contract tests passed |
| WP03-T017-D | Done | 组织主链路对象评审 | deliverables/WP03-T017-评审纪要.md（含8对象评审、3高扇出ADR对齐、10 Gate清单、Design→Build映射、D Gate） | 高扇出对象结论闭合 |
| WP03-T017-B | Done | 固化评审结论为 M3 checklist 守卫 | M3ChecklistGuards.h + M3ChecklistContractTest.cpp（4正例+6负例，10/10 sub-tests） | 评审结论可程序化执行 |
| WP03-T018-D | Done | 发布主链路对象 M3 冻结包 | deliverables/WP03-T018-M3冻结包.md（含冻结资产清单17D+21H+17T、M3 Gate 10项全Pass、门禁规则、变更管理、Design→Build映射、D Gate） | 可作为后续详细设计基线 |
| WP03-T018-B | Done | 新增并接入 WP03 CI 门禁脚本 | scripts/ci/wp03_contract_gate.sh（17 required tests、注册校验、全量 contract suite） | 本地/CI gate 返回码符合预期，37/37 passed |

## 6. 推荐执行顺序

1. 先执行 D 链路：T001-D 至 T014-D，先冻结语义边界和字段约束。
2. 紧跟执行 B 链路：每完成一个 D 子任务，立即执行对应 B 子任务，禁止集中到最后补做。
3. 最后执行闭环与门禁：T015-D/B -> T016-D/B -> T017-D/B -> T018-D/B。

## 7. 阻塞项与解阻条件

1. BLK-01：仅有 Design 文档，缺失 Build 对象头文件与 contract tests。  
解阻条件：任一 Txxx-D 进入 In Review 前，Txxx-B 至少完成代码骨架与测试骨架提交。
2. BLK-02：CMake/CTest 无法执行新增 WP03 contract tests。  
解阻条件：cmake -S . -B build-ci -G Ninja 成功，且 ctest --test-dir build-ci -L contract 可执行。
3. BLK-03：主链路对象边界与 ADR-006/007/008 冲突。  
解阻条件：先修订 D 子任务交付物并完成评审纪要，再推进对应 B 子任务。

## 8. 依赖与风险

1. 若 GoalContract 未冻结成功判据，Planner 与 ResponseBuilder 将持续返工。
2. 若 Observation 与 ObservationDigest 不分层，memory、cognition、audit 会被迫耦合到同一对象。
3. 若 Checkpoint 过载，runtime 与 multi_agent 将失去恢复边界。
4. 若 T015/T016 缺少 Build 测试守卫，T017/T018 的评审与冻结将不可验证。

## 9. WP03-T001 执行证据

1. D 交付物：deliverables/WP03-T001-主链路对象依赖表.md
2. B 代码交付：contracts/include/agent/MainFlowContracts.h
3. B 测试交付：tests/contract/smoke/MainFlowContractsSmokeTest.cpp
4. 测试接线：tests/contract/CMakeLists.txt 新增 MainFlowContractsSmokeTest 注册
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 21/21 contract tests passed
	- ctest --test-dir build-ci -R MainFlowContractsSmokeTest --output-on-failure -> 1/1 passed

## 10. WP03-T002 执行证据

1. D 交付物：deliverables/WP03-T002-AgentRequest语义说明.md（含 Phase 0 研究证据链 + D Gate 结果）
2. B 代码交付：
	- contracts/include/agent/AgentRequest.h（最小契约对象，6 必填 + 11 可选字段，含 RequestChannel 枚举）
	- contracts/include/agent/AgentRequestGuards.h（必填校验 + 边界守卫）
3. B 测试交付：tests/contract/agent/AgentRequestContractTest.cpp（4 正例 + 13 负例）
4. 测试接线：tests/contract/CMakeLists.txt 新增 AgentRequestContractTest 注册（agent 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 22/22 contract tests passed
	- ctest --test-dir build-ci -R AgentRequestContractTest --output-on-failure -> 1/1 passed

## 11. WP03-T003 执行证据

1. D 交付物：deliverables/WP03-T003-AgentRequest字段表.md（含字段分组、必选/可选规则、禁止字段、D Gate）
2. B 代码交付：
	- contracts/include/agent/AgentRequestGuards.h（新增 validate_agent_request_field_rules 字段级校验器，覆盖可选字符串非空、数值正值、tags 合法性、RuntimeBudget 维度校验）
3. B 测试交付：tests/contract/agent/AgentRequestFieldContractTest.cpp（4 正例 + 17 负例）
4. 测试接线：tests/contract/CMakeLists.txt 新增 AgentRequestFieldContractTest 注册（agent 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 23/23 contract tests passed
	- ctest --test-dir build-ci -R AgentRequestFieldContractTest --output-on-failure -> 1/1 passed

## 12. WP03-T004 执行证据

1. D 交付物：deliverables/WP03-T004-GoalContract语义说明.md（含五职责语义、必填/可选字段、枚举定义、禁止字段、上下游关系、D Gate）
2. B 代码交付：
	- contracts/include/agent/GoalContract.h（契约对象，6 必填 + 7 可选字段，含 ApprovalPolicy/GoalStatus 枚举）
	- contracts/include/agent/GoalContractGuards.h（必填校验 + 边界守卫，2 层验证）
3. B 测试交付：tests/contract/agent/GoalContractContractTest.cpp（4 正例 + 14 负例）
4. 测试接线：tests/contract/CMakeLists.txt 新增 GoalContractContractTest 注册（agent 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 24/24 contract tests passed
	- ctest --test-dir build-ci -R GoalContractContractTest --output-on-failure -> 1/1 passed

## 13. WP03-T005 执行证据

1. D 交付物：deliverables/WP03-T005-GoalContract字段表.md（含字段分组、三层校验堆叠设计、budget 维度约束规则、D Gate）
2. B 代码交付：
	- contracts/include/agent/GoalContractGuards.h（新增 validate_goal_contract_field_rules 字段级校验器，覆盖可选字符串非空、tags 合法性、budget_override 5 维度正值）
3. B 测试交付：tests/contract/agent/GoalContractFieldContractTest.cpp（4 正例 + 10 负例）
4. 测试接线：tests/contract/CMakeLists.txt 新增 GoalContractFieldContractTest 注册（agent 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 25/25 contract tests passed
	- ctest --test-dir build-ci -R GoalContractFieldContractTest --output-on-failure -> 1/1 passed

## 14. WP03-T006 执行证据

1. D 交付物：deliverables/WP03-T006-Observation语义说明.md（含六职责语义、5必填+8可选字段、ObservationSource枚举、禁止字段6类、上下游关系、D Gate）
2. B 代码交付：
	- contracts/include/observation/Observation.h（统一折叠契约对象，5 必填 + 8 可选字段，含 ObservationSource 枚举）
	- contracts/include/observation/ObservationGuards.h（必填校验 + 边界守卫 + success/error 一致性校验，2 层验证）
3. B 测试交付：tests/contract/observation/ObservationContractTest.cpp（4 正例 + 14 负例，含 success/error 一致性校验）
4. 测试接线：tests/contract/CMakeLists.txt 新增 ObservationContractTest 注册（observation 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 26/26 contract tests passed
	- ctest --test-dir build-ci -R ObservationContractTest --output-on-failure -> 1/1 passed

## 15. WP03-T007 执行证据

1. D 交付物：deliverables/WP03-T007-Observation分类表.md（含枚举冻结决策4类+Unspecified、source→correlation映射规则7条、ErrorSourceRef对齐4行、Layer 3守卫规则、D Gate）
2. B 代码交付：
	- contracts/include/observation/ObservationSource.h（独立枚举头文件，含 to_string_view/is_known_observation_source/source_to_error_ref_type 工具函数）
	- contracts/include/observation/ObservationSourceGuards.h（Layer 3 source→correlation 一致性守卫，7 条校验规则）
	- contracts/include/observation/Observation.h（移除内联枚举，改为 include ObservationSource.h）
3. B 测试交付：tests/contract/observation/ObservationSourceContractTest.cpp（5 正例 + 14 负例 + 3 工具函数测试）
4. 测试接线：tests/contract/CMakeLists.txt 新增 ObservationSourceContractTest 注册（observation 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 27/27 contract tests passed
	- ctest --test-dir build-ci -R ObservationSourceContractTest --output-on-failure -> 1/1 passed

## 16. WP03-T008 执行证据

1. D 交付物：deliverables/WP03-T008-ObservationDigest边界说明.md（含5必填+4可选字段、互斥字段表11项、分层对称性、消费者清单、7条守卫规则、D Gate）
2. B 代码交付：
	- contracts/include/observation/ObservationDigest.h（推理友好投影契约对象，5 必填 + 4 可选字段，含 confidence [0.0,1.0] 约束）
	- contracts/include/observation/ObservationDigestGuards.h（Layer 1 必填校验 + Layer 2 分层边界校验，7 条规则）
3. B 测试交付：tests/contract/observation/ObservationDigestBoundaryContractTest.cpp（4 正例 + 14 负例，含 confidence 边界测试、可选字段约束测试）
4. 测试接线：tests/contract/CMakeLists.txt 新增 ObservationDigestBoundaryContractTest 注册（observation 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 28/28 contract tests passed
	- ctest --test-dir build-ci -R ObservationDigestBoundaryContractTest --output-on-failure -> 1/1 passed

## 17. WP03-T009 执行证据

1. D 交付物：deliverables/WP03-T009-BeliefState语义说明.md（含6必填+3可选字段、10类禁止字段、分层对称表、8消费者/下游、8条守卫规则、D Gate）
2. B 代码交付：
	- contracts/include/agent/BeliefState.h（认知状态契约对象，6 必填 + 3 可选字段，含 confidence [0.0,1.0] 约束）
	- contracts/include/agent/BeliefStateGuards.h（Layer 1 必填校验 + Layer 2 禁入边界校验，8 条规则）
	- contracts/include/agent/BeliefStateTag.h（空标签类型，供 MainFlowContracts.h 链路占位）
3. B 测试交付：tests/contract/agent/BeliefStateContractTest.cpp（4 正例 + 14 负例，含 confidence 边界测试、可选字段约束测试）
4. 测试接线：tests/contract/CMakeLists.txt 新增 BeliefStateContractTest 注册（agent 分组）
5. MainFlowContracts.h 更新：BeliefStateEntry 从内联占位改为 BeliefStateTag 别名
6. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 29/29 contract tests passed
	- ctest --test-dir build-ci -R BeliefStateContractTest --output-on-failure -> 1/1 passed

## 18. WP03-T010 执行证据

1. D 交付物：deliverables/WP03-T010-ContextPacket语义说明.md（含4必填+9可选字段、ADR-006 §6.1 全10槽位覆盖、24类禁止字段、1生产者+8消费者、D Gate）
2. B 代码交付：
	- contracts/include/context/ContextPacket.h（语义上下文契约对象，4 必填 + 9 可选字段，覆盖 ADR-006 §6.1 全 10 类槽位）
3. B 测试交付：tests/contract/context/ContextPacketMainFlowContractTest.cpp（4 正例 + 14 负例，含首轮空历史、元数据、必填缺失、可选字段边界测试）
4. 测试接线：tests/contract/CMakeLists.txt 新增 ContextPacketMainFlowContractTest 注册（context 分组）
5. MainFlowContracts.h 更新：ContextPacketEntry 从 ContextPacketTag 改为 ContextPacket 别名
6. MainFlowContractsSmokeTest.cpp 更新：ContextPacketEntry 的 is_empty_v 断言改为 !is_empty_v（反映已升级为实体结构）
7. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 30/30 contract tests passed
	- ctest --test-dir build-ci -R ContextPacketMainFlowContractTest --output-on-failure -> 1/1 passed

## 19. WP03-T011 执行证据

1. D 交付物：deliverables/WP03-T011-ContextPacket字段表.md（含4必填+9可选字段分组、三层堆叠校验设计L1/L2/L3、校验规则表、Design→Build映射、D Gate）
2. B 代码交付：
	- contracts/include/context/ContextPacketGuards.h（三层堆叠校验器：L1 必填4字段存在性 + L2 边界约束 created_at + L3 字段规则：可选 string 非空、向量非空且元素非空、tags 合法性）
3. B 测试交付：tests/contract/context/ContextPacketFieldContractTest.cpp（4 正例 + 14 负例，覆盖必填缺失、空字符串、边界违规、向量校验、tags 非法）
4. 测试接线：tests/contract/CMakeLists.txt 新增 ContextPacketFieldContractTest 注册（context 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 31/31 contract tests passed
	- ctest --test-dir build-ci -R ContextPacketFieldContractTest --output-on-failure -> 1/1 passed

## 20. WP03-T012 执行证据

1. D 交付物：deliverables/WP03-T012-Checkpoint语义说明.md（含5必填+6可选字段、CheckpointState枚举7值、9类禁止字段、1生产者+4消费者、两层守卫设计、D Gate）
2. B 代码交付：
	- contracts/include/checkpoint/Checkpoint.h（最小恢复状态契约对象，5 必填 + 6 可选字段，含 CheckpointState 枚举，对齐架构 §3.8.3）
	- contracts/include/checkpoint/CheckpointGuards.h（L1 必填5字段存在性 + L2 边界约束：枚举范围、可选 string 非空、created_at 正值）
3. B 测试交付：tests/contract/checkpoint/CheckpointContractTest.cpp（4 正例 + 14 负例，覆盖必填缺失、Unspecified 状态、空字符串、枚举遍历、可选边界）
4. 测试接线：tests/contract/CMakeLists.txt 新增 CheckpointContractTest 注册（checkpoint 分组）
5. MainFlowContracts.h 更新：CheckpointEntry 从 CheckpointTag 改为 Checkpoint 别名
6. MainFlowContractsSmokeTest.cpp 更新：CheckpointEntry 的 is_empty_v 断言改为 !is_empty_v（反映已升级为实体结构）
7. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 32/32 contract tests passed
	- ctest --test-dir build-ci -R CheckpointContractTest --output-on-failure -> 1/1 passed

## 21. WP03-T013 执行证据

1. D 交付物：deliverables/WP03-T013-Checkpoint字段表.md（含5R+6O字段分组、三层堆叠L3设计、tags统一校验、state→pending_action一致性规则、D Gate）
2. B 代码交付：
	- contracts/include/checkpoint/CheckpointGuards.h（新增 validate_checkpoint_field_rules：L3 继承 L2 + tags 非空向量无空串 + 等待状态 pending_action 非空一致性）
3. B 测试交付：tests/contract/checkpoint/CheckpointFieldContractTest.cpp（4 正例 + 10 负例，覆盖 tags 违规、Paused/WaitingConfirm/WaitingTool 一致性违规、L2 回归）
4. 测试接线：tests/contract/CMakeLists.txt 新增 CheckpointFieldContractTest 注册（checkpoint 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 33/33 contract tests passed
	- ctest --test-dir build-ci -R CheckpointFieldContractTest --output-on-failure -> 1/1 passed

## 22. WP03-T014 执行证据

1. D 交付物：deliverables/WP03-T014-AgentResult语义说明.md（含6R+7O字段、AgentResultStatus枚举6值、架构§5.1全5字段映射、§3.8.1审计引用、禁止字段11类、两层守卫设计、D Gate）
2. B 代码交付：
	- contracts/include/agent/AgentResult.h（最终输出契约对象，6 必填 + 7 可选字段，含 AgentResultStatus 枚举，复用 WP-02 ResultCode/ErrorInfo）
	- contracts/include/agent/AgentResultGuards.h（L1 必填6字段存在性 + L2 边界约束：枚举范围、result_code WP-02范围、可选 string 非空、tags 统一校验）
3. B 测试交付：tests/contract/agent/AgentResultContractTest.cpp（4 正例 + 14 负例，覆盖必填缺失、Unspecified 状态、result_code 范围、空字符串、tags 违规）
4. 测试接线：tests/contract/CMakeLists.txt 新增 AgentResultContractTest 注册（agent 分组）
5. MainFlowContracts.h 更新：AgentResultEntry 从 AgentResultTag 改为 AgentResult 别名
6. MainFlowContractsSmokeTest.cpp 更新：AgentResultEntry 的 is_empty_v 断言改为 !is_empty_v（反映已升级为实体结构）
7. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 34/34 contract tests passed
	- ctest --test-dir build-ci -R AgentResultContractTest --output-on-failure -> 1/1 passed

## 23. WP03-T015 执行证据

1. D 交付物：deliverables/WP03-T015-主流程对象流图.md（含8节点流转总图、节点详表8行、关联字段8边详表、生产者/消费者映射8阶段、端到端示意、Design→Build映射4行、D Gate）
2. B 代码交付：
	- tests/contract/e2e/MainFlowContractE2ETest.cpp（端到端契约冒烟测试，覆盖全8节点Guard验证、关联字段一致性、kCanonicalOrder完整性、关联断裂检测）
3. B 测试交付：tests/contract/e2e/MainFlowContractE2ETest.cpp（4 正例 + 5 负例，共 9 个子测试）
4. 测试接线：tests/contract/CMakeLists.txt 新增 MainFlowContractE2ETest 注册（e2e 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 35/35 contract tests passed
	- ctest --test-dir build-ci -R MainFlowContractE2ETest --output-on-failure -> 1/1 passed（9/9 sub-tests passed）

## 24. WP03-T016 执行证据

1. D 交付物：deliverables/WP03-T016-职责重叠检查单.md（含8×5互斥矩阵、4组相邻对象对称互斥表、域归属唯一性规则、Design→Build映射、D Gate）
2. B 代码交付：
	- contracts/include/boundary/MainFlowOverlapGuards.h（5域禁止字段数组、evaluate_main_flow_overlap统一检查函数、4组对称互斥函数、域归属常量）
3. B 测试交付：tests/contract/smoke/MainFlowOverlapContractTest.cpp（4 正例 + 5 负例，共 9 个子测试）
4. 测试接线：tests/contract/CMakeLists.txt 新增 MainFlowOverlapContractTest 注册（smoke 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 36/36 contract tests passed
	- ctest --test-dir build-ci -R MainFlowOverlapContractTest --output-on-failure -> 1/1 passed（9/9 sub-tests passed）

## 25. WP03-T017 执行证据

1. D 交付物：deliverables/WP03-T017-评审纪要.md（含8对象评审清单、3高扇出对象×ADR一致性Go/No-Go、跨对象一致性评审、10 Gate M3冻结条件清单、Design→Build映射、D Gate）
2. B 代码交付：
	- contracts/include/boundary/M3ChecklistGuards.h（M3ChecklistInputs 10 gate struct、M3ChecklistResult、validate_m3_checklist() 验证函数、kM3GateNames/kM3GateDescriptions 审计数组、m3_count_passed_gates() 辅助函数）
3. B 测试交付：tests/contract/smoke/M3ChecklistContractTest.cpp（4 正例 + 6 负例，共 10 个子测试）
4. 测试接线：tests/contract/CMakeLists.txt 新增 M3ChecklistContractTest 注册（smoke 分组）
5. 验收命令与结果：
	- cmake --build build-ci --target dasall_contract_tests -> 37/37 contract tests passed
	- ctest --test-dir build-ci -R M3ChecklistContractTest --output-on-failure -> 1/1 passed（10/10 sub-tests passed）

## 26. WP03-T018 执行证据

1. D 交付物：deliverables/WP03-T018-M3冻结包.md（含冻结资产清单17D+21H+17T、里程碑依赖链M1→M2→M3、语义主结论5节、M3 Gate 10项全Pass、生效门禁3条准入+4条阻断、变更管理4规则、测试证据汇总、Design→Build映射、D Gate）
2. B 代码交付：
	- scripts/ci/wp03_contract_gate.sh（WP-03 CI 门禁脚本，17 required tests 注册校验 + 全量 contract suite 执行，模式对齐 wp01/wp02_contract_gate.sh）
3. B 测试交付：复用 WP03 contract tests 全集（37/37 passed）
4. 验收命令与结果：
	- bash scripts/ci/wp03_contract_gate.sh -> [WP03-GATE] WP03 contract gate passed（37/37 contract tests passed, 0 failed）
