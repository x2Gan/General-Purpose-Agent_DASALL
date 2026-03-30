# WP05-T002 ToolRequest 语义说明

最近更新时间：2026-03-20
任务状态：Done
任务编号：WP05-T002
上游输入：WP03-T002 AgentRequest 语义说明、WP03-T006 Observation 语义冻结、WP04-T002 PromptComposeRequest 边界、架构 Tool System 章节、DASALL contracts 冻结实施计划

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. 架构文档 4.5 Tool System：Tool System 接收 Cognition 产生的动作意图，经 Validator -> Policy Gate -> Executor 治理后执行，并将结果归一化为 Observation 回传。
2. 架构文档 5.2.2：ITool.validate、ITool.execute、ITool.compensate 全部消费 ToolRequest，说明 ToolRequest 是工具子系统面向执行的稳定输入契约。
3. 架构文档 5.2.5：Tool Schema、Function Calling、Tool IR 必须显式分层；Function Calling 不是权限系统，Tool IR 是 Runtime/Validator/Executor/Audit 的内部统一表示。
4. 架构文档 5.2.6：原始工具结果应治理后回灌，不应让 Reasoner 直接处理未经治理的原始输出；结果侧应进入 Observation/ObservationDigest，而不是回流到请求对象。
5. DASALL contracts 冻结实施计划 7/8：执行链路为 ActionDecision -> ToolRequest / WorkerTask / External Action，观测链路为 ToolResult -> Observation -> ObservationDigest；Tool 子域提前冻结是因为它直接影响 Observation 治理链路。
6. WP03-T002：AgentRequest 作为统一入口对象，不得混入 provider 私有字段与 runtime 内部状态；ToolRequest 不能回退成第二个入口请求对象。
7. WP04-T002：PromptComposeRequest 只消费 ContextPacket 引用而不拥有上下文数据；ToolRequest 同样应只表达工具执行意图，不成为上下文/Prompt 的第二拥有者。
8. WP03-T006 Observation：错误、payload、side_effects、tool_call_id 等执行结果语义最终归 Observation，说明 ToolRequest 不应提前承载结果和错误语义。
9. WP02-T007 RuntimeBudget：预算语义已冻结为共享横切对象，ToolRequest 只能复用 RuntimeBudget，不能再发明工具专用预算模型。
10. WP04-T018 WorkerTask：子任务对象只保留执行单元最小语义，不混入顶层状态；ToolRequest 应遵循同样的最小执行面原则。

### 外部参考清单

1. Consumer-Driven Contracts Pattern：契约应围绕真实消费方的最小必要期待设计，接收方做 just-enough validation，避免把 provider 完整内部 schema 暴露给所有消费者。
2. Proto Best Practices：长期演进的消息对象应保持单一职责；不同边界的消息不要强行复用一个大对象，避免请求对象膨胀并导致后续 breaking change 风险。

### 对本任务的可落地启发

1. ToolRequest 必须是精简的执行意图对象，只覆盖工具执行所需的稳定输入，不打包结果、错误、Prompt 或上下文内部态。
2. 预算只能复用已冻结的 RuntimeBudget，不能新增 ToolBudget、SpentBudget 等平行预算语义。
3. ToolRequest 应与 ToolDescriptor、ToolIR 保持分层：前者是注册/声明，后者是内部统一执行表示，ToolRequest 只表达执行请求面。
4. 边界守卫应显式拒绝 observation、error、rendered_prompt、tool_schema 等越界字段，避免工具请求对象退化为“万能包”。
5. contract test 应聚焦真实消费方依赖：ITool.validate/execute 需要的最小请求字段，以及跨域字段的自动阻断。

## 1. 任务理解

本任务只处理 WP05-T002：冻结 ToolRequest 的职责边界，并同步落盘 ToolRequest 契约对象、守卫与 contract test。

本任务不处理：

1. ToolResult 对象及其与 Observation 的折叠细节，归 WP05-T003。
2. ToolDescriptor / ToolIR 的注册与内部统一表示分层，归 WP05-T004。
3. Prompt、Memory、LLM 等其他子域对象的冻结。

## 2. 约束与边界

### 2.1 直接约束

1. 来源 WP05-T002 Done Criteria：ToolRequest 不重复定义 error/budget/observation。
2. 来源架构 Tool System：ToolRequest 是 Tool.validate/execute 的执行输入，不是模型 Function Calling 原文，也不是 ToolResult。
3. 来源实施计划执行链/观测链：ToolRequest 位于执行链路，Observation 位于观测链路，二者必须分层。
4. 来源 WP03/WP04 冻结对象：AgentRequest 拥有入口语义，PromptComposeRequest 拥有消息装配语义，ToolRequest 不得重复承载两者主责。
5. 来源 WP02 横切冻结：错误与预算为共享横切基础语义，ToolRequest 只能复用，不得平行复制。

### 2.2 边界与非目标

ToolRequest 允许承载的语义：

1. 执行标识：request_id、tool_call_id、tool_name。
2. 调用意图：invocation_kind、arguments_payload。
3. 调用时间基线：created_at。
4. 关联锚点：goal_id、worker_task_id。
5. 约束复用：runtime_budget、timeout_ms、idempotency_key、tags。

ToolRequest 明确禁止承载的语义：

1. 执行结果语义：error、result_payload、observation、observation_digest、side_effects。
2. 预算状态快照：budget_snapshot、remaining_budget、spent_tokens 等已执行态预算信息。
3. Prompt/Provider 语义：rendered_prompt、provider_payload、final_messages。
4. 注册与内部表示语义：tool_schema、tool_descriptor、tool_ir。
5. 上下文拥有权语义：memory_snapshot、retrieval_candidates、context_packet_internal。

### 2.3 前置依赖检查

1. WP05-T001-D/B 已完成，Wave1 tool 子域可以继续推进。
2. WP03/WP04 冻结包已提供 AgentRequest、Observation、PromptComposeRequest、WorkerTask 等相邻对象作为稳定输入。
3. build-ci 已存在，当前工作树干净，可直接进入实现与验证。

结论：本任务可执行，无 blocker。

## 3. 方案对比与决策

### 3.1 方案 A：最小执行意图对象（采纳）

定义方式：

1. ToolRequest 仅描述“调用哪个工具、以何种调用类型、带什么参数、在什么预算/超时约束下执行”。
2. 结果、错误、Observation、Tool Schema、Prompt 渲染信息全部留在相邻对象承接。

优点：

1. 与架构 Tool.validate/execute 的真实消费面一致。
2. 与 ToolResult、Observation、ToolDescriptor、ToolIR 形成清晰分层。
3. 默认向后兼容，后续新增约束字段时不必回改请求对象核心职责。

缺点：

1. 需要调用方在上游先完成上下文解析和意图归一化，不能把所有便利字段直接塞进请求对象。

### 3.2 方案 B：请求 + 结果/Schema 混合对象（不采纳）

定义方式：

1. 在 ToolRequest 中直接携带 error、result_payload、tool_schema、rendered_prompt、budget_snapshot 等扩展信息。
2. 试图让 Tool.validate/execute 通过单对象拿到全部上下文与结果态。

缺点：

1. 直接违反 WP05-T002 完成判定“不重复定义 error/budget/observation”。
2. 会破坏 T003/T004 后续对象分层，导致 ToolRequest 退化为万能传输包。
3. 会让 ToolRequest 同时承担执行输入、注册描述和结果回传三类职责，演进风险高。

### 3.3 决策

采用方案 A。

## 4. 最终语义冻结

### 4.1 ToolRequest 最小语义范围

ToolRequest 只表达以下五类语义：

1. 工具调用标识：request_id、tool_call_id、tool_name。
2. 工具调用分类：invocation_kind，用于表达读查询/动作/工作流/代理协作/诊断等稳定类别。
3. 执行参数载荷：arguments_payload，承载 provider-neutral 的结构化参数文本。
4. 执行约束复用：runtime_budget、timeout_ms、idempotency_key。
5. 追溯关联：created_at、goal_id、worker_task_id、tags。

### 4.2 明确排除语义

1. ToolRequest 不是 ToolResult，不承载 result_payload、error、side_effects、observation。
2. ToolRequest 不是 Prompt 请求对象，不承载 rendered_prompt、provider_payload、final_messages。
3. ToolRequest 不是工具注册说明，不承载 tool_schema、tool_descriptor。
4. ToolRequest 不是内部统一执行表示，不承载 tool_ir。
5. ToolRequest 不是预算快照对象，不承载 budget_snapshot、remaining_budget、spent_tokens。

### 4.3 与相邻对象的边界

| 对象 | 主责 | ToolRequest 与其关系 | 不可混入字段 |
|---|---|---|---|
| AgentRequest | 顶层入口请求 | ToolRequest 继承 request_id 追溯链，但不重复入口上下文拥有权 | user_input、session_id、trace_id 全量入口语义 |
| PromptComposeRequest | Prompt 装配请求 | ToolRequest 只消费已决策好的执行意图，不承载 Prompt 装配字段 | rendered_prompt、provider_payload、visible_tools |
| ToolResult | 工具执行结果 | ToolRequest 是输入面，ToolResult 是输出面 | error、result_payload、side_effects |
| Observation | 统一观测折叠 | ToolResult/失败结果最终折叠到 Observation，ToolRequest 不提前携带观测语义 | observation、observation_digest |
| ToolDescriptor / ToolIR | 注册描述 / 内部表示 | ToolRequest 只描述执行请求，不拥有注册元数据或内部 IR | tool_schema、tool_descriptor、tool_ir |

## 5. Design -> Build 映射

| D 原子项 | 设计结论 | 对应 Build 动作 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| D1 | 冻结 ToolRequest 只承载执行意图与追溯锚点 | 定义 ToolInvocationKind 与 ToolRequest struct | contracts/include/tool/ToolRequest.h | ToolRequestContractTest 正例覆盖最小/完整请求 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolRequestContractTest --output-on-failure |
| D2 | 明确禁止结果/预算快照/Prompt/Descriptor 语义进入 ToolRequest | 在 ToolRequestGuards.h 中实现 required/boundary/field/forbidden-field 守卫 | contracts/include/tool/ToolRequestGuards.h | ToolRequestContractTest 负例覆盖 observation/error/rendered_prompt/tool_schema 等越界字段 | 同上 |
| D3 | 锁定 Build 三件套并验证测试发现性 | 新增 tool 组 contract test 并接入 tests/contract/CMakeLists.txt | tests/contract/tool/ToolRequestContractTest.cpp；tests/contract/CMakeLists.txt | ctest 可发现 ToolRequestContractTest，正负例可二值判定 | ctest --test-dir build-ci -N -R ToolRequestContractTest |

## 6. D Gate 结果

1. D 文档已落盘。
2. ToolRequest 最小语义范围已冻结。
3. Build 三件套已锁定：
   - 代码目标：ToolRequest.h、ToolRequestGuards.h
   - 测试目标：ToolRequestContractTest.cpp + CMake 注册
   - 验收命令：cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolRequestContractTest --output-on-failure
4. 范围未越界，可进入 -B。

Gate 结论：PASS。

## 7. Build 执行清单

### B1 代码目标

1. 新增 ToolInvocationKind 与 ToolRequest 契约对象。
2. 新增 ToolRequest 三层守卫：required、boundary、field rules。
3. 新增 field-name forbidden guard，阻断结果/Prompt/Descriptor 越界字段。

### B2 测试目标

1. 至少 1 个正例：最小合法 ToolRequest 通过校验。
2. 至少 1 个负例：越界字段和非法可选字段被阻断。
3. 触及 CMake 注册时，额外验证测试发现性。

### B3 验收命令

1. ctest --test-dir build-ci -N -R ToolRequestContractTest
2. cmake --build build-ci --target dasall_contract_tests
3. ctest --test-dir build-ci -R ToolRequestContractTest --output-on-failure

## 8. 风险与回退

1. 风险：后续为图省事把 ToolResult/error 直接塞回 ToolRequest。
   回退：继续通过 forbidden-field 守卫和 contract test 阻断。
2. 风险：把 ToolDescriptor/ToolIR 提前内嵌进 ToolRequest，导致 T004 无法分层。
   回退：保持 ToolRequest 仅保留 tool_name + invocation_kind + arguments_payload 的执行面。
3. 风险：为工具执行单独再发明预算结构。
   回退：统一复用 RuntimeBudget，新增预算需求先回到横切基础对象评审。

## 9. Blocker 状态

当前无 blocker。

若后续出现阻塞，最小解阻路径为：

1. 若 build-ci 无法重新发现新增测试，先修复 tests/contract/CMakeLists.txt 注册入口。
2. 若 ToolRequest 字段设计与 T003/T004 冲突，回退到本文件 4.2 的禁止项，以边界优先而非实现便利优先。