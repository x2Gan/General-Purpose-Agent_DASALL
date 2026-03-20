# WP05-T010 LLMRequest / LLMResponse 职责边界说明

最近更新时间：2026-03-20
任务状态：Done
任务编号：WP05-T010
上游输入：架构文档 5.4 LLM 子系统、ADR-006 Prompt 链责任链、WP05-T005 PromptRelease/PromptComposeResult 冻结对象

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. 架构文档 5.4.1 已冻结 `ILLMAdapter::generate(const LLMRequest&) -> LLMResponse` 的统一接口，说明 llm 子域需要稳定的请求/响应契约面。
2. 架构文档 5.4.3 已冻结 LLM 输出语义只允许 `Direct Response`、`Tool Call Intent`、`Clarification Request`、`Replan Suggestion` 四类意图，且“输出只是意图，不是可直接执行的命令”。
3. 架构文档 5.4.4/5.4.5 已冻结调用链审计锚点至少包含 `model_name`、`prompt_id`、`prompt_version`，但这些审计字段不等于 provider 私有协议字段。
4. ADR-006 已冻结调用顺序 `ContextOrchestrator -> PromptRegistry -> PromptComposer -> PromptPolicy -> LLMManager`，因此 LLMRequest 只能消费 provider-neutral 的 prompt 结果，不得反向拥有 ContextPacket、PromptSpec 或 PromptRelease 资产字段。
5. `PromptComposeResult` 已冻结 `messages`、`selected_prompt_id`、`selected_version`、`estimated_tokens`，说明 LLMRequest 的输入 handoff 应是“已装配好的 provider-neutral messages + 审计锚点”，而不是再次携带 prompt 模板资产。
6. `contracts/include/llm/` 当前为空目录，说明本任务可以在不回改既有 llm 契约的前提下新增最小稳定对象。

### 外部参考清单

1. Protocol Buffers Language Guide, Updating A Message Type：新增字段属于 wire-safe 变更；字段编号和语义不应被复用，契约演进应优先新增而非覆写旧字段。
2. OpenAI Structured Outputs Guide：结构化输出应以显式 schema 约束为边界，响应面只暴露可验证的结构化结果和 refusal/usage 等可编程消费元信息，而不要求共享对象暴露 provider 私有协议细节。

### 对本任务的可落地启发

1. LLMRequest 必须只承载 provider-neutral 调用面：消息载荷、路由标识、输出约束和预算提示，而不是 PromptRelease 原始资产或厂商私有参数。
2. LLMResponse 必须只承载“模型语义结果 + 审计元信息 + usage/refusal 元信息”，不得直接承载可执行控制命令或 provider raw payload。
3. 守卫应至少覆盖 required、field hygiene、forbidden field categories 三层，并输出稳定的 decision/reason 以支撑 contract tests。

## 1. 任务理解

本任务只处理 WP05-T010：

1. 新增 `contracts/include/llm/LLMRequest.h`，冻结 LLM 请求对象边界。
2. 新增 `contracts/include/llm/LLMResponse.h`，冻结 LLM 响应对象边界。
3. 新增 `contracts/include/llm/LLMBoundaryGuards.h`，实现请求/响应 required、field rules 与 forbidden field guards。
4. 新增并注册 `tests/contract/llm/LLMRequestResponseContractTest.cpp`。

本任务不处理：

1. `ModelRoute` 独立对象设计与实现。
2. provider SDK、传输协议、stream handle 的实现细节。
3. PromptRelease、PromptComposeResult、ErrorInfo 等既有冻结对象回改。

## 2. 约束与边界

### 2.1 LLMRequest 允许承载

1. 调用身份：`request_id`、`llm_call_id`。
2. provider-neutral 调用面：`model_route`、`request_mode`、`messages`。
3. prompt/输出审计锚点：`prompt_id`、`prompt_version`、`output_schema_ref`、`response_format`。
4. 预算与时限提示：`runtime_budget`、`max_output_tokens`、`timeout_ms`。
5. 时间与审计：`created_at`、`tags`。

### 2.2 LLMRequest 禁止承载

1. Context/Memory 所有权字段：`context_packet`、`summary_memory`、`retrieval_candidates`。
2. Prompt 资产字段：`system_instructions`、`task_template`、`few_shot_refs`、`policy_notes`。
3. provider 私有字段：`provider_payload`、`model_provider_args`、`vendor_request`。
4. runtime 控制字段：`retry_count`、`checkpoint_ref`、`fsm_state`。

### 2.3 LLMResponse 允许承载

1. 调用身份：`request_id`、`llm_call_id`。
2. 语义输出：`response_kind`、`content_payload`。
3. 审计元信息：`completed_at`、`model_name`、`prompt_id`、`prompt_version`、`finish_reason`。
4. usage 元信息：`input_tokens`、`output_tokens`、`total_tokens`。
5. refusal 与审计标签：`refusal_reason`、`tags`。

### 2.4 LLMResponse 禁止承载

1. provider raw payload：`raw_provider_response`、`logprobs`、`reasoning_trace`、`vendor_response`。
2. prompt/context ownership：`messages`、`system_instructions`、`context_packet`。
3. 可执行控制字段：`shell_command`、`tool_request`、`worker_dispatch`、`retry_plan`。
4. 错误所有权字段：`error_info`、`result_code`、`failure_type`。

## 3. Design 原子清单

1. D1：冻结 LLMRequest 的最小 provider-neutral 请求面。
- 输入依据：架构文档 5.4.1、5.4.4、5.4.6；ADR-006 调用顺序。
- 产出路径：`contracts/include/llm/LLMRequest.h`。
- 完成判定：required 字段和禁止域可守卫校验，不回收 PromptRelease/ContextPacket 资产字段。
- 风险与回退：若对象重新携带 prompt/context/provider 私有字段，则回退到 provider-neutral handoff 面。

2. D2：冻结 LLMResponse 的最小语义输出面。
- 输入依据：架构文档 5.4.3、5.4.5。
- 产出路径：`contracts/include/llm/LLMResponse.h`。
- 完成判定：response_kind 可表达稳定意图分类，usage/refusal 元信息与 provider raw payload 分层清晰。
- 风险与回退：若响应对象开始承担执行控制或错误对象所有权，则回退为语义结果对象。

3. D3：新增请求/响应边界守卫与 contract tests。
- 输入依据：WP05 contract task 三件套、上游 Prompt handoff 边界。
- 产出路径：`contracts/include/llm/LLMBoundaryGuards.h`、`tests/contract/llm/LLMRequestResponseContractTest.cpp`、`tests/contract/CMakeLists.txt`。
- 完成判定：至少 1 个正例 + 1 个负例；stable decision/reason 可断言；测试可被 ctest 发现。
- 风险与回退：若只有 happy path，则补齐 provider/private、prompt asset、execution control 等负例。

## 4. Design -> Build 映射

| D 原子项 | 设计结论 | 对应 Build 动作 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| D1 | LLMRequest 只消费 provider-neutral handoff 数据 | 新增请求对象与 required/field-rules/boundary 守卫 | contracts/include/llm/LLMRequest.h；contracts/include/llm/LLMBoundaryGuards.h | 缺失 required、prompt 审计锚点不成对、provider 私有字段负例断言 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R LLMRequestResponseContractTest --output-on-failure |
| D2 | LLMResponse 只承载语义结果与审计元信息 | 新增响应对象与 refusal/usage 一致性守卫 | contracts/include/llm/LLMResponse.h；contracts/include/llm/LLMBoundaryGuards.h | refusal_reason 缺失、usage 不一致、execution control 字段负例断言 | 同上 |
| D3 | llm 子域必须能自动阻断共享对象污染 | 新增 forbidden field decision 守卫 + contract test + CMake 注册 | contracts/include/llm/LLMBoundaryGuards.h；tests/contract/llm/LLMRequestResponseContractTest.cpp；tests/contract/CMakeLists.txt | decision/reason 稳定断言与测试发现性验证 | 同上 |

## 5. D Gate 结果

1. D 文档已落盘。
2. Design 原子清单具备二值完成判定。
3. Build 三件套已锁定：
- 代码目标：`contracts/include/llm/LLMRequest.h`、`contracts/include/llm/LLMResponse.h`、`contracts/include/llm/LLMBoundaryGuards.h`
- 测试目标：`tests/contract/llm/LLMRequestResponseContractTest.cpp`
- 验收命令：`cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R LLMRequestResponseContractTest --output-on-failure`
4. 范围未越界，满足进入 Build 条件。

Gate 结论：PASS。

## 6. Build 执行清单

1. B1：新增 LLMRequest 契约对象与 provider-neutral handoff 守卫。
2. B2：新增 LLMResponse 契约对象与语义输出守卫。
3. B3：新增 forbidden field decision 守卫，阻断 context/prompt/provider/runtime/execution control 污染。
4. B4：新增并注册 LLMRequestResponseContractTest（正例 + 负例 + decision/reason 断言）。

## 7. Build 合规复核

1. 新增代码已补充对象级、字段级与守卫级注释。
2. 测试覆盖正例与负例，并断言 stable decision/reason。
3. 测试已注册到 contract tests，具备可发现性。
4. TODO 已回写状态与验收证据。

## 8. Blocker 状态

当前无 blocker。