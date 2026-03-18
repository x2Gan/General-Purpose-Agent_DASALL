# WP03-T002 AgentRequest 语义说明

最近更新时间：2026-03-17
任务状态：Done
任务编号：WP03-T002
上游输入：架构文档入口链路、WP01-T013 M1 冻结包、WP02-T015 M2 冻结包

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. 架构文档 §3.8.1：AgentRequest 被定义为"统一入口，请求、会话、用户、trace 和附件信息都从这里进入"。
2. 架构文档 §6.1-6.2：AgentRequest 由 Access Gateway 创建，传入 Runtime 处理。
3. ADR-008 §5.1：明确 MultiAgentRequest 不复用 AgentRequest，两者层级分离。
4. 工程蓝图 §3.1：contracts/include/agent/ 承载 AgentRequest, AgentResult, GoalContract。
5. WP02-T009：request_id/session_id/trace_id 横切标识规范已冻结。
6. WP02-T007：RuntimeBudget 五维预算语义已冻结。
7. WP02-T010：时间语义（created_at/deadline_at/timeout_ms）已冻结。
8. WP03-T001：主链路 8 对象依赖链已冻结，AgentRequest 位于链首。

### 外部参考清单

1. Anthropic "Building Effective Agents"（2024-12）：orchestrator-workers 模式中 central orchestrator 接收任务请求并拥有生命周期，worker 不拥有入口请求。AgentRequest 作为统一入口与此一致——入口对象不混入执行态。
2. Microsoft Scheduler-Agent-Supervisor Pattern：Scheduler 在 state store 中记录任务初始状态，submission process 创建 task state。AgentRequest 对应 task submission boundary，只承载请求初始信息，不携带运行中间态。

### 对本任务的可落地启发

1. AgentRequest 作为纯入口契约对象，不承载 runtime 运行态——与 Anthropic 入口请求和 Microsoft 初始提交一致。
2. 验证守卫应遵循 Poka-yoke 原则（Anthropic），让误用契约变得更困难。
3. 字段验证在契约边界执行（Consumer-Driven Contracts），必填完整性 + 禁止字段渗透。
4. 预算与超时复用已冻结 WP-02 横切基础（RuntimeBudget, TimeDeadline），不重复发明。
5. RequestChannel 枚举遵循 WP-02 枚举生命周期规则（Unspecified 哨兵值）。

## D Gate 结果

- 是否达到进入 -B 条件：**是**
- 已定义 AgentRequest 最小语义范围（入口意图/约束/预算/元数据）
- 已明确禁止夹带 runtime 内部状态和 provider 私有字段
- 研究证据链完整：本地 8 条 + 外部 2 条
- 后续 T003 字段表已同步完成，可直接输入 B 阶段

## 1. 任务理解

本任务只处理 WP03-T002：定义 AgentRequest 的最小语义范围，交付“AgentRequest 语义说明”。

本任务不展开字段清单（WP03-T003）、不展开 GoalContract 语义（WP03-T004）、不进入 Context/Observation/Checkpoint 细化。

## 2. 约束与边界

### 2.1 直接约束（可追溯）

1. 来源 WP-03 TODO：T002 完成判定为“AgentRequest 不夹带 runtime 内部状态和 provider 私有字段”。
2. 来源总体计划阶段 2：AgentRequest 必须只表达统一入口请求语义，不夹带 runtime 内部状态。
3. 来源工程蓝图入口链路：AgentRequest 由 apps 层 Request Normalizer 构造并传入 runtime，属于跨层统一入口对象。
4. 来源总体计划对象分层顺序：llm/provider 适配应位于后续层，前置共享语义对象应避免被 provider 细节污染。
5. 来源 ADR-006：消息渲染与 provider payload 属于 Prompt/LLM 侧，不属于共享语义上下文对象；同理不得上浮污染入口请求对象。
6. 来源 M2 冻结包：横切 id/time/budget/error 已冻结为统一基础语义，AgentRequest 只能消费这些横切语义，不得再发明同类内部态字段。

### 2.2 边界与非目标

边界：

1. 只冻结 AgentRequest 的“语义边界”，不冻结具体字段名与类型。
2. 只定义“应承载什么/不应承载什么”，不定义序列化实现与 provider 协议映射。
3. 只处理单 Agent 入口语义，不覆盖 MultiAgentRequest 子域请求语义。

非目标：

1. 不定义 request_id/session_id/trace_id 的字段细则（由 WP-02 规则承接，WP03-T003 落盘）。
2. 不定义运行时 FSM、retry counters、checkpoint 引用等恢复执行态字段。
3. 不定义 provider_model、provider_payload、tool schema 渲染结构。

### 2.3 前置依赖检查

1. M2 前置依赖满足：WP02-T015 当前为 Done，B1/B2 为 Closed，可进入 WP-03 对象语义任务。
2. 输入一致性风险：工作日志 #007 仍记录历史“B2 待验证”状态，与 M2 冻结包最新状态不一致。
3. 处置：本任务以最新冻结包为准继续推进；日志口径差异登记为文档同步风险，不作为阻塞项。

## 3. 方案对比与决策

### 3.1 方案 A：入口最小载荷语义（推荐）

定义方式：

1. AgentRequest 仅承载“外部请求意图 + 约束边界 + 预算边界 + 请求元数据”。
2. 运行态与供应商态均不进入该对象。

优点：

1. 与“统一入口请求语义”要求完全一致。
2. 避免 apps/runtime/llm 间边界漂移。
3. 可直接被 T003 字段任务消费。

缺点：

1. 对实现方约束更严格，需要将内部态下沉到 runtime 内部结构。

### 3.2 方案 B：入口+预热执行态混合语义

定义方式：

1. 在 AgentRequest 中附带部分 runtime 预判状态（如初始执行阶段、预估重试上限）。
2. 允许透传少量 provider 配置快捷字段。

优点：

1. 短期实现路径看似更快。
2. 可减少部分中间转换。

缺点：

1. 违反 T002 完成判定与计划约束。
2. 容易把入口契约固化为实现细节，形成后续 breaking 变更风险。
3. 会与 ADR-006 的分层理念冲突（语义对象与渲染对象混层）。

### 3.3 决策

采用方案 A。

取舍理由：方案 A 与 WP03-T002 完成判定、阶段 2 计划约束和蓝图入口链路完全一致，且能最小化跨模块耦合。

## 4. 最终产出

### 4.1 AgentRequest 最小语义范围（冻结）

AgentRequest 只表达四类语义：

1. 入口意图语义：用户任务意图、目标描述输入、调用入口上下文。
2. 约束语义：安全约束、策略约束、能力可见性约束（声明式而非执行态）。
3. 预算语义：对本次请求生效的预算边界声明（消费 WP-02 冻结语义）。
4. 请求元数据语义：请求关联标识与追踪元数据（消费 WP-02 冻结语义）。

### 4.2 明确排除语义（禁止夹带）

AgentRequest 不得承载以下语义：

1. runtime 内部状态：FSM 状态、调度队列位点、retry counters、backoff、熔断态、checkpoint 快照引用细节。
2. provider 私有字段：provider_payload、rendered_prompt、vendor-specific message 参数、provider 私有路由细节。
3. 执行结果态：Observation/ObservationDigest/BeliefState/RecoveryOutcome 任一结果对象内容。
4. 多 Agent 子域态：WorkerTask/MultiAgentRequest/MultiAgentResult 的局部协同内部字段。

### 4.3 语义边界说明（用于评审）

1. AgentRequest 是“入口契约”，不是“运行快照”。
2. AgentRequest 是“语义请求”，不是“模型请求体”。
3. AgentRequest 是“跨模块稳定对象”，不是“某一 provider 的配置对象”。
4. 任何实现便利性诉求都不得突破上述边界，必要信息应通过后续对象链分层承接。

### 4.4 与后续任务映射

1. 可直接输入 WP03-T003：在本语义边界内拆解必填/可选字段。
2. 可直接输入 WP03-T004/T005：将目标契约表达下沉到 GoalContract，避免挤入 AgentRequest。
3. 可直接输入 WP03-T010：ContextPacket 承接共享上下文语义，不回灌入口对象。

### 4.5 Build 模式补充

本任务为 Design 优先任务，当前仅进行文档落盘，不进行 contracts 代码实现。

代码变更清单：

1. 更新 WP-03 TODO 中 T002 状态和交付物链接。
2. 新增本交付文档。

关键接口（后续实现占位，不在本任务落盘）：

1. contracts/agent/AgentRequest.h：仅可声明入口语义相关字段分组。
2. apps/*/RequestNormalizer：仅负责外部协议到 AgentRequest 的归一化，不注入 runtime/provider 私有态。

测试清单（后续可执行）：

1. contract test：校验 AgentRequest schema 不包含 runtime 内部状态字段。
2. contract test：校验 AgentRequest schema 不包含 provider 私有字段。
3. review checklist：命中“入口对象混入执行态/供应商态”即拒绝。

验证步骤（至少一组）：

1. 契约校验：在 contract tests 中新增 AgentRequest 边界断言并执行。
2. 评审校验：按 WP02-T013 checklist + 本文 4.2 禁止项逐条核对。
3. 构建校验：新增测试后执行 contracts + tests 构建，确保门禁可执行。

## 5. 验收清单

1. 已定义 AgentRequest 的最小语义范围（入口意图/约束/预算/元数据）。
2. 已明确“禁止夹带 runtime 内部状态和 provider 私有字段”。
3. 结论可追溯到 WP-03 TODO、实施计划、蓝图入口链路、ADR-006 和 M2 冻结包。
4. 产出可直接被 WP03-T003 消费。

Quality Gate 回答：

1. 当前任务是否达成 Done Criteria：是，语义边界已满足“不夹带 runtime 内部状态和 provider 私有字段”。
2. 产出是否可被下一任务直接消费：是，WP03-T003 可直接据此拆字段。
3. 是否引入 breaking change 风险：否，本任务未改动代码与既有契约实现。
4. 是否需要触发 ADR 或版本变更流程：否，本任务未改写 ADR 结论，仅做语义冻结落盘。

## 6. 风险与回退

1. 风险：实现阶段为图方便把 provider 参数塞进 AgentRequest。
回退：将 provider 参数下沉到 PromptComposeRequest/LLMRequest，并在契约测试中禁止回流。
2. 风险：runtime 为减少转换成本把执行态字段前移到入口对象。
回退：由 runtime 内部状态容器承接，AgentRequest 保持只读入口语义。
3. 风险：文档口径漂移（M2 与工作日志状态不一致）导致评审误判。
回退：以最新冻结包为准，补充日志同步条目并在评审纪要中记录口径来源。

## 7. 下一任务建议（仅直接后继）

1. WP03-T003 列出 AgentRequest 必填字段和可选字段。
