# WP01-T008 contracts 边界说明 v1

最近更新时间：2026-03-13
任务状态：In Review
任务编号：WP01-T008
上游输入：WP01-T004 术语消费者矩阵，WP01-T006 稳定对象标注版流图，WP01-T007 内部对象边界清单

## 1. 文档目的

本说明用于把 WP01 已形成的三类材料收束为统一边界规则，明确：

1. 什么对象可以进入 contracts 作为跨模块稳定契约。
2. 什么对象属于模块内部结构或流程中间态，不能外溢到 contracts。
3. 当前阶段不外溢但后续可能对象化的灰度边界如何处理。

## 2. 范围与非目标

### 2.1 本文档覆盖范围

1. 对象级边界判定规则（不是字段级细化规则）。
2. T006 中 Stable/Non-Contract 判定的语义解释。
3. T007 中 Internal-Blocked/Deferred/External-Action 判定的治理落地。

### 2.2 本文档不覆盖范围

1. ADR 字段级核对细项（由 T009、T010、T011 完成）。
2. tool 子域、event 子域、llm 子域后续对象冻结结论（由 WP-05 完成）。
3. 代码实现与序列化选型。

## 3. 边界判定总原则

### 3.1 正向准入原则（进入 contracts）

对象只有同时满足以下条件，才可进入 contracts：

1. 跨模块共享：至少被两个以上模块直接消费，且消费语义稳定。
2. 语义独立：对象代表稳定业务语义，而不是流程中间态、桥接占位或实现步骤。
3. 边界合规：不会暴露下层模块内部结构，不承担其他边界主体职责。
4. 可演进：可在兼容优先前提下扩展，不依赖一次性固化的实现细节。

### 3.2 反向阻断原则（不得进入 contracts）

若对象命中以下任一特征，当前阶段不得进入 contracts：

1. 仅用于图面桥接或过程拼接。
2. 表示外部输入来源或外部动作占位，而非共享语义对象。
3. 表示模块内部提示、策略或执行中间结果。
4. 尚未完成对应子域边界冻结，当前仅为粗粒度占位节点。

## 4. 三层边界模型

基于 T006 与 T007，WP01 采用三层边界模型：

1. Stable（可进入 contracts）：跨模块稳定契约对象。
2. Blocked（禁止外溢）：模块内部结构、桥接节点、原始来源节点、提示信号、外部动作占位。
3. Deferred（阶段性不外溢）：当前不进入 contracts，但允许后续在子域冻结中重新裁定。

该模型用于避免两类误判：

1. 把流程中间态误当共享契约对象。
2. 把“当前未冻结”误判为“永久禁止进入 contracts”。

## 5. 对象边界收束结果（来自 T004/T006/T007）

### 5.1 跨模块稳定契约对象（Stable）

当前进入 contracts 的核心对象包括：

1. AgentRequest
2. GoalContract
3. ContextPacket
4. ActionDecision
5. Observation
6. ObservationDigest
7. ErrorInfo
8. Checkpoint
9. ReflectionDecision
10. RecoveryOutcome
11. AgentResult
12. WorkerTask
13. MultiAgentRequest
14. MultiAgentResult

这些对象满足 T004 的“跨模块稳定对象”判定，并在 T006 流图中已标为 Stable。

### 5.2 禁止外溢对象（Blocked）

当前明确禁止以“对象主名”进入 contracts 的节点包括：

1. SessionContext Summary
2. Memory Evidence
3. Knowledge Evidence
4. Clarification Hint
5. Replan Hint
6. External Action
7. WorkerResult
8. RetrievalResult
9. HumanInput
10. Final Observations
11. Plan Status
12. Goal Fragment
13. Plan Fragment

统一处置规则：以上节点只能作为来源、桥接或内部步骤语义存在；跨模块流通时必须先折叠进 Stable 对象。

### 5.3 阶段性不外溢对象（Deferred）

当前判定为 Deferred 的节点：

1. ToolRequest
2. ToolResult

规则：

1. 在 WP01 阶段不得外溢到 contracts 主清单。
2. 在 WP-05 tool 子域细化后允许复审，决定是否升级为 Stable。

## 6. ADR 对齐边界

### 6.1 ADR-006 对齐

1. ContextPacket 为语义上下文对象，不得承载 final_messages、provider_payload、rendered_prompt。
2. PromptComposeRequest/PromptComposeResult 属于边界对象，但不替代 ContextPacket 语义主权。

### 6.2 ADR-007 对齐

1. ReflectionDecision 只承载失败语义建议，不承载执行调度字段。
2. RecoveryOutcome 只承载恢复执行结果与控制元数据，不回写失败归因语义。

### 6.3 ADR-008 对齐

1. MultiAgentRequest/MultiAgentResult 属于协同子域契约，不等同于全局请求/最终输出。
2. WorkerTask 为子任务执行单元，不承载全局 Session/FSM 语义。

## 7. 边界判定检查单（评审可执行）

任一对象准备进入 contracts 时，按以下顺序检查：

1. 是否已在术语矩阵中被标记为跨模块稳定对象。
2. 是否在对象流图中承担主链路稳定语义，而非桥接或来源占位。
3. 是否命中 T007 的 Blocked 清单特征。
4. 若为 Deferred，是否已有对应子域冻结结论支持升级。
5. 是否与 ADR-006/007/008 的边界裁定冲突。

若任一项不通过，则不得进入 contracts。

## 8. 常见误判与纠偏

1. 误判：把 HumanInput、Memory Evidence 当作共享对象。
   纠偏：它们是来源语义，必须先折叠为 Observation/ContextPacket 等稳定对象。
2. 误判：把 Clarification Hint、Replan Hint 当作顶层共享对象。
   纠偏：它们是内部提示信号，应附着在稳定决策对象语义中。
3. 误判：把 ToolRequest、ToolResult 直接定为永久禁止对象。
   纠偏：它们是 Deferred，不是永久禁止；需等 WP-05 冻结后复审。
4. 误判：把桥接节点（Final Observations、Plan Status）对象化并外溢。
   纠偏：桥接节点可保留在图中，但不能直接成为 contracts 主对象。

## 9. 对后续任务的输入约束

### 9.1 对 T009 的输入

1. 重点核对 ContextPacket 是否存在消息层字段越界。

### 9.2 对 T010 的输入

1. 重点核对 ReflectionDecision 与 RecoveryOutcome 是否发生职责串扰。

### 9.3 对 T011 的输入

1. 重点核对 MultiAgentRequest、MultiAgentResult、WorkerTask 是否越权承载全局语义。

## 10. 风险与回退策略

### 10.1 风险

1. 评审中若忽略 Deferred 语义，可能误伤后续 tool 子域对象冻结。
2. 桥接节点在不同图法中可能被误读为正式对象。
3. 后续子域细化若直接引入新共享对象名，可能绕过本边界规则。

### 10.2 回退策略

1. 若 WP-05 新增稳定对象，采用增量修订本说明，不回写改变 WP01 历史判定依据。
2. 若评审要求更严格主图，可把 Blocked 节点降为图注，不作为节点出现。
3. 若出现边界冲突，优先按 ADR 裁定，再回写到 T009-T011 核对单。

## 11. 交付物映射

1. 本文件即 WP01-T008 交付物“边界说明 v1”。
2. 可直接作为 WP01-T009、WP01-T010、WP01-T011 的统一边界输入。