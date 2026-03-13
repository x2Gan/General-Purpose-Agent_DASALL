# ADR-006 ContextOrchestrator 与 PromptComposer 职责划分

## 0. 文档信息

### 0.1 文档定位

本文档用于裁定 DASALL Agent 中 ContextOrchestrator 与 PromptComposer 的职责边界、输入输出契约和调用关系。

该决策属于架构级边界决策，会直接影响以下内容：

1. ContextPacket 的职责边界。
2. PromptComposeRequest 和 PromptComposeResult 的定义。
3. memory 与 llm 子系统之间的依赖关系。
4. token 预算、裁剪策略和 Prompt 治理的归属。
5. 后续 contracts、runtime、memory、llm 模块的 API 冻结方式。

### 0.2 背景

当前设计文档已经分别定义了两类能力：

1. Memory 子系统中的 Context Orchestrator，负责检索、筛选、压缩、预算裁剪和写回闭环。
2. LLM 子系统中的 Prompt Registry、Prompt Composer、Prompt Policy，负责 Prompt 资产选择、消息装配和发送前治理。

但现有文档中仍存在边界重叠风险：

1. Context Orchestrator 被描述为负责按 stage、task_type、available_tools 决定上下文装配策略。
2. Prompt Composer 被描述为按 stage、task_type、context_packet、tool_visibility 组装最终 messages。
3. 某些学习材料中把 final_messages 放入 ContextPacket，这会把语义上下文对象与模型请求对象混在一起。

如果不先裁定这条边界，后续很容易出现两个问题：

1. memory 为了“更完整地装配上下文”开始侵入 prompt 与模型适配细节。
2. llm 为了“更灵活地组织消息”开始自己做检索、压缩和历史筛选，形成双上下文主控点。

---

## 1. 问题陈述

需要解决的问题不是“两个组件都做一点上下文相关工作是否可以”，而是以下三个更根本的问题：

1. 谁拥有语义上下文的组装权。
2. 谁拥有模型请求消息的装配权。
3. 当预算不足、内容过多、模型能力受限时，谁拥有最终裁剪的决策权。

这三个问题如果没有清晰答案，后续 contracts 设计一定会产生职责串扰。

---

## 2. 现有设计理解

### 2.1 当前仓库内的设计信号

基于现有架构文档，可以得出以下共识：

1. Cognition 层只消费 ContextPacket 与 Observation，不直接访问 memory、knowledge、tools 的实现细节。
2. Memory 子系统负责向 Cognition 提供已组装好的 ContextPacket，并负责写回和压缩闭环。
3. Prompt Registry 负责选择 Prompt 资产，Prompt Composer 负责把 Prompt 资产和上下文装配成最终 messages，Prompt Policy 负责发送前治理。
4. Runtime 主循环中，Context Orchestrator 在前，Prompt Registry、Prompt Composer、Prompt Policy 在后。

这说明当前总体方向已经隐含了一个正确趋势：

1. ContextOrchestrator 更接近“语义上下文供应方”。
2. PromptComposer 更接近“模型输入消息构造方”。

问题在于这一点还没有被显式冻结。

### 2.2 业界常见做法与可借鉴结论

结合通用 ADR 实践和当前主流 Agent 系统设计资料，可以提炼出三条稳定结论：

1. ADR 应优先记录这种会影响多个模块、多个接口和后续演进路径的架构级决策，而不是等 API 全部冻结后再补录。
2. 主流 Agent 实践会把 orchestration、state 或 memory、tools、instructions 分开处理，不鼓励一个组件同时承担检索、消息拼装、工具可见性和模型适配四类职责。
3. 在成熟实现中，“上下文准备”通常是对信息源做筛选、排序、压缩和预算分配，而“prompt 装配”通常是把已准备好的上下文映射成模型可消费的 messages、instructions、tool definitions 和结构化输出约束。

这与当前 DASALL 的七层分层和 memory、llm 分模块设计是一致的。

---

## 3. 决策

### 3.1 总体决策

ContextOrchestrator 与 PromptComposer 必须严格分层，分别承担“语义上下文编排”和“模型消息装配”两类职责，不能合并为单一组件，也不能互相侵入对方的主职责域。

### 3.2 ContextOrchestrator 的职责边界

ContextOrchestrator 归属 memory 子系统，负责产出面向认知层与 LLM 输入层共享消费的语义上下文对象，而不是直接产出最终模型消息。

它的职责固定为：

1. 根据 stage、goal_contract、session_state、available_tools、runtime_budget 决定本轮上下文装配策略。
2. 从 Working Memory、Summary Memory、Long-Term Memory、Experience Memory、Knowledge Service 拉取候选信息。
3. 对候选信息执行相关性排序、冲突处理、压缩和预算分配。
4. 生成 ContextPacket，并显式记录 dropped_items、compression_notes、budget_report 等元信息。
5. 在需要时触发摘要沉淀与写回闭环，但写回动作由 memory 内部策略执行，不向 llm 暴露内部实现细节。

ContextOrchestrator 明确不负责：

1. 选择 PromptSpec 或 PromptRelease。
2. 生成 provider-specific messages。
3. 注入 system instructions、few-shots、output schema 或 tool schemas。
4. 处理模型厂商格式差异。
5. 在最终发送前执行 Prompt 安全治理。

### 3.3 PromptComposer 的职责边界

PromptComposer 归属 llm 子系统，负责将已经确定的 Prompt 资产和语义上下文映射为模型可消费的消息结构。

它的职责固定为：

1. 接收 PromptRegistry 选出的 PromptRelease。
2. 接收 ContextPacket、visible_tools、model_route、task_type、stage、output_schema 等输入。
3. 将 ContextPacket 中的语义槽位映射到 Prompt 模板变量与消息结构中。
4. 生成规范化的 messages 或等价模型请求载荷，并产出 PromptComposeResult。
5. 记录 selected_prompt_id、selected_version、pruned_sections、estimated_tokens 等装配结果信息。

PromptComposer 明确不负责：

1. 主动拉取 memory 或 knowledge 的原始候选片段。
2. 重新决定长期记忆、历史、证据的保留优先级。
3. 直接修改 Working Memory、Summary Memory、Experience Memory。
4. 决定某个工具是否在权限上可执行。
5. 把未治理的原始工具结果直接塞入最终请求。

### 3.4 PromptPolicy 的边界补充

为避免 ContextOrchestrator 与 PromptComposer 的边界再次模糊，必须同时明确 PromptPolicy 的位置：

1. PromptPolicy 位于 PromptComposer 之后。
2. PromptPolicy 负责发送前的合法性与治理校验，而不是语义上下文构建。
3. PromptPolicy 可以对消息做 redaction、tool visibility patch、长度限制和来源过滤。
4. PromptPolicy 不得回头替代 ContextOrchestrator 进行检索、压缩或重排。

---

## 4. 调用顺序与责任链

本决策固定以下调用顺序：

1. Runtime 解析当前 stage、goal、session 和 runtime budget。
2. ContextOrchestrator 组装 ContextPacket。
3. PromptRegistry 选择 PromptRelease。
4. PromptComposer 根据 ContextPacket 和 PromptRelease 生成 PromptComposeResult。
5. PromptPolicy 对 PromptComposeResult 做发送前治理。
6. LLMManager 将治理后的请求下发给具体模型适配器。

这条责任链意味着：

1. 语义信息先确定，再做消息渲染。
2. Prompt 是对上下文的消费方，不是上下文的拥有方。
3. LLM 子系统可以感知 ContextPacket，但不能成为 ContextPacket 的生产者。

---

## 5. Token 预算与裁剪权的裁定

### 5.1 总体原则

必须把 token 预算拆成两层，而不是由单一组件一次性拍板：

1. 语义预算。
2. 渲染预算。

### 5.2 语义预算归属 ContextOrchestrator

ContextOrchestrator 拥有语义预算裁剪权，负责决定：

1. 哪类信息源进入本轮上下文。
2. 当前目标、约束、未完成动作、历史、证据分别分配多少预算。
3. 哪些内容被摘要沉淀，哪些内容被延迟、裁剪或丢弃。

这是因为这些决策依赖 memory、knowledge、goal 和 stage 语义，属于信息治理问题，而不是消息格式问题。

### 5.3 渲染预算归属 PromptComposer 与 PromptPolicy

PromptComposer 与 PromptPolicy 只拥有渲染层面的预算处理权，负责：

1. 把 ContextPacket 中的内容映射到不同 role 的消息中。
2. 在不改变语义优先级的前提下做格式压缩、字段省略或模板降级。
3. 发现 provider 限制无法承载时，返回 over_budget 或 composition_warning，而不是自行重做语义裁剪。

如果渲染后仍然超预算，正确流程不是 PromptComposer 自行继续删减历史，而是返回上游，由 Runtime 触发 ContextOrchestrator 重新装配或切换模型路线。

---

## 6. 契约层面的直接影响

本决策会直接影响 contracts 的定义，需同步冻结以下约束：

### 6.1 ContextPacket 只承载语义上下文，不承载最终消息

ContextPacket 不得包含 final_messages、provider_payload、rendered_prompt 这类消息层字段。

ContextPacket 应包含的内容是：

1. user_turn
2. current_goal 或 goal_contract 摘要
3. recent_history
4. summary_memory
5. retrieval_evidence
6. latest_observation_digest
7. active_tools 或 visible_capabilities 摘要
8. policy_digest
9. token_budget_report
10. belief_state 或等价事实视图

### 6.2 PromptComposeRequest 消费 ContextPacket，而不替代其定义

PromptComposeRequest 应是一个装配请求对象，而不是第二个上下文对象。它至少包含：

1. stage
2. task_type
3. context_packet
4. prompt_release
5. visible_tools
6. model_route
7. output_schema_ref 或 response_format

### 6.3 PromptComposeResult 只承载装配产物与装配元数据

PromptComposeResult 应只表达：

1. messages 或 provider-neutral prompt payload
2. selected_prompt_id
3. selected_version
4. pruned_sections
5. estimated_tokens
6. composition_warnings

它不应反向承担 memory 写回、候选信息检索或知识召回语义。

---

## 7. 备选方案与取舍

### 方案 A：由 ContextOrchestrator 同时负责上下文编排和最终消息装配

不采纳。

原因：

1. 会让 memory 子系统侵入 prompt 资产和模型适配层。
2. 会让 ContextPacket 退化为 provider-specific 请求结构。
3. 不利于 Prompt 版本治理、灰度发布和模型替换。

### 方案 B：由 PromptComposer 同时负责上下文拉取、排序和消息装配

不采纳。

原因：

1. 会让 llm 子系统侵入 memory 和 knowledge 的核心职责。
2. 会形成第二个上下文主控点。
3. token 裁剪会演化成“谁离模型近谁说了算”，最终破坏分层。

### 方案 C：ContextOrchestrator 负责语义上下文，PromptComposer 负责消息装配

采纳。

原因：

1. 与当前七层分层和模块职责最一致。
2. 更利于 contracts 分层冻结。
3. 更利于测试隔离、模型切换和 Prompt 治理。
4. 能清晰区分语义预算和渲染预算。

---

## 8. 后果

### 正面影响

1. memory 与 llm 的主职责边界清晰，降低后续 API 返工风险。
2. ContextPacket 可以稳定成为 cognition、runtime、llm 之间的共享语义对象。
3. Prompt 资产治理、灰度、回滚和审计更容易独立建设。
4. 模型切换和 provider 适配不会反向污染 memory 设计。

### 负面影响

1. 会引入两层预算控制，需要设计好 over_budget 回传机制。
2. 在实现上会多一个中间对象层次，初期感觉比“直接拼 prompt”更复杂。
3. 需要更严格的 contracts 设计，避免 ContextPacket 和 PromptComposeRequest 再次重叠。

### 风险控制要求

1. 不允许在 ContextPacket 中加入 final_messages 之类字段。
2. 不允许 PromptComposer 直接调用 memory 或 knowledge 的实现。
3. 如果渲染超预算，必须通过显式信号回到 Runtime，而不是组件内偷偷裁剪语义内容。
4. 所有裁剪都必须可审计，可追踪 dropped_items、pruned_sections 与 policy_patch。

---

## 9. 实施要求

基于本 ADR，下一步必须落实以下动作：

1. 在 contracts 详细设计中，去除 ContextPacket 中可能混入的消息层字段。
2. 在 llm 详细设计中，把 PromptRegistry、PromptComposer、PromptPolicy 的接口拆清楚。
3. 在 runtime 详细设计中，明确 over_budget 的回流路径和重装配策略。
4. 在 memory 详细设计中，补齐 ContextAssembleRequest、ContextAssembleResult、CompressionRequest、CompressionResult。
5. 在测试设计中，分别提供 MockContextOrchestrator 与 MockPromptComposer，避免两者被同一 Mock 混淆。

Status：Accepted
