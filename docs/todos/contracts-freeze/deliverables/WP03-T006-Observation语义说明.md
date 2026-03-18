# WP03-T006 Observation 语义说明

最近更新时间：2026-03-17
任务状态：Done
任务编号：WP03-T006
上游输入：架构 3.8.2 / 4.5 / 5.2.6、ADR-006/007/008、冻结计划 §7/§8、WP03-T004/T005 已冻结交付物

## 1. 任务理解

本任务只处理 WP03-T006：定义 Observation 的统一折叠语义边界。

本任务不处理：

1. Observation 来源分类与引用规则（WP03-T007）。
2. ObservationDigest 分层边界与字段（WP03-T008）。
3. BeliefState / ContextPacket / Checkpoint / AgentResult（WP03-T009 及后续）。
4. 工具治理流程与执行细节（tools 子域实现）。
5. MCP 远程能力实现细节（multi_agent 子域）。

## 2. 约束与边界

### 2.1 直接约束（可追溯）

1. 架构 3.8.2：系统必须把工具结果、检索结果、人工反馈、子 Agent 输出统一折叠为 Observation，并定义 ErrorInfo。
2. 架构 3.8.2：Observation 至少包含 source、success、payload、error、side_effects。
3. 架构 4.5：Tool System 将结果归一化为 Observation 回传。
4. 架构 3.8.4：Multi-Agent 输出和 Tool 输出必须最终汇聚成统一 Observation。
5. 架构 5.8.4：CognitionStepRequest 包含 latest_observation。
6. 架构 5.2.6：工具和工作流原始结果适合程序消费，不适合直接回灌推理。Observation Digest 是推理消费接口。
7. ADR-006：ContextOrchestrator 消费 latest_observation_digest，不直接消费原始 Observation。
8. ADR-007：Tool、Workflow、MCP 或外部执行结果统一映射为 Observation 与 ErrorInfo。反思/恢复细节归 ReflectionEngine/RecoveryManager。
9. ADR-008：子 Agent 输出若不符合约定格式，统一视为失败 Observation。AgentOrchestrator 折叠协同结果为 Observation。
10. 冻结计划 §7：ToolResult / WorkerResult / RetrievalResult / HumanInput → Observation → ObservationDigest。
11. 冻结计划 §8 阶段2 第4条：ObservationDigest 必须与 Observation 分层。
12. WP02-T012：所有枚举必须包含显式 Unspecified 哨兵值。
13. WP02-T010：时间字段为毫秒时间戳。
14. WP02-T009：标识字段需入口生成并全链路透传。
15. WP01 ObjectBoundaryCatalog：Observation = Stable 类型。

### 2.2 边界与非目标

边界：

1. 仅定义 Observation 对象级职责边界与最小字段语义。
2. 仅定义统一折叠语义（四类来源归一化到同一结构），不定义具体来源分类枚举（归 T007）。
3. 仅冻结字段语义与分组，不冻结序列化格式。

非目标：

1. 不定义 ObservationDigest 的摘要字段（归 T008）。
2. 不引入运行态字段：retry_counters、backoff_ms、fsm_state、circuit_state。
3. 不引入消息渲染字段：final_messages、rendered_prompt。
4. 不引入计划/决策字段：plan_graph、step_list、action_decision。
5. 不引入 Checkpoint 反向引用：checkpoint_ref。

### 2.3 前置依赖检查

1. T004-D/B 已 Done：GoalContract 语义边界冻结。
2. T005-D/B 已 Done：GoalContract 字段规则冻结。
3. WP02 冻结包 Done：ErrorInfo、ErrorSourceRef、RuntimeBudget、枚举规则已冻结。
4. WP01 ObservationTag.h 存在：Stable 类型占位符。
5. 当前无阻塞项。

## 3. 研究证据链

### 3.1 本地证据清单

| 编号 | 证据来源 | 关键结论 |
|---|---|---|
| L1 | 架构 3.8.2 | Observation 统一折叠四类来源，至少含 source/success/payload/error/side_effects |
| L2 | 架构 4.5 | Tool System 归一化结果为 Observation |
| L3 | 架构 3.8.4 | Multi-Agent 输出汇聚为统一 Observation |
| L4 | 架构 5.8.4 | CognitionStepRequest 含 latest_observation |
| L5 | 架构 5.2.6 | 原始结果不适合直接回灌推理，需 Digest 中间层 |
| L6 | ADR-006 §6.1 | ContextOrchestrator 消费 observation_digest，不直接消费原始 Observation |
| L7 | ADR-007 §4 | Tool/Workflow/MCP/外部结果统一映射为 Observation + ErrorInfo |
| L8 | ADR-008 §3.2/4 | AgentOrchestrator 折叠协同结果为 Observation；失败格式视为失败 Observation |
| L9 | 冻结计划 §7 | 四来源通道：ToolResult / WorkerResult / RetrievalResult / HumanInput |
| L10 | WP01 GuardCommon.h | is_supported_error_source_ref_type 已接受 "observation" |
| L11 | 架构主循环伪代码 | observation_builder_.from_tool_result / memory.write_observation |

### 3.2 外部参考清单

| 编号 | 参考来源 | 关键结论 |
|---|---|---|
| E1 | Martin Fowler — Contract Test (2011/2018) | 契约测试验证格式稳定性，不绑定数据内容 |
| E2 | LangChain ToolMessage / AgentAction | 工具执行结果归一化为统一消息类型，含 tool_call_id + content + artifact |
| E3 | OpenAI Function Calling Response | 工具返回统一结构 {tool_call_id, role, content}，不含执行细节 |
| E4 | AutoGPT Observation Pattern | Agent 循环中 Observation 为"执行后观测"，与 Action 对称 |

### 3.3 对本任务的可落地启发

1. Observation 应是纯数据折叠容器：只表达"发生了什么"，不表达"下一步做什么"。
2. 四类来源统一到一个 struct，通过 source 枚举区分，而非继承层次——与冻结计划一致。
3. error 字段应直接关联已冻结的 ErrorInfo 对象，不重复定义错误结构。
4. side_effects 为声明式列表（字符串向量），用于幂等/补偿判断，不承载执行逻辑。
5. observation_id 为全链路透传标识，是 ReflectionDecision.relevant_observation_refs 和 Memory 写入的引用键。

## 4. Observation 统一折叠语义定义

基于架构 3.8.2 冻结要求，Observation 承载以下职责且仅以下职责：

| 职责 | 语义说明 | 消费者 | 不承载 |
|---|---|---|---|
| 来源标识 | 标记此观测来自哪类执行通道 | Reasoner、ReflectionEngine | 不含执行流程细节 |
| 成功状态 | 标记执行是否成功 | Reasoner、RecoveryManager | 不含重试策略 |
| 结果载荷 | 执行产出的原始数据（结构化字符串） | Reasoner、Memory、DigestBuilder | 不含消息渲染 |
| 错误信息 | 执行失败时的结构化错误描述 | ReflectionEngine、RecoveryManager | 不含恢复执行细节 |
| 副作用声明 | 执行引起的不可逆变更列表 | 补偿策略、审计 | 不含补偿执行逻辑 |
| 关联追踪 | 关联执行请求的标识（tool_call_id 等） | 审计、Memory 引用 | 不含 Checkpoint 快照 |

## 5. 最终产出

### 5.1 Observation 必填字段

| 字段名 | 类型语义 | 语义说明 | 主要依据 |
|---|---|---|---|
| observation_id | string | 观测级唯一标识，全链路透传用于引用 | WP02-T009 标识规则、ADR-007 relevant_observation_refs |
| source | ObservationSource 枚举 | 来源通道类型（T007 定义枚举值） | 架构 3.8.2 source 字段 |
| success | bool | 执行是否成功 | 架构 3.8.2 success 字段 |
| payload | string | 执行结果载荷（结构化字符串） | 架构 3.8.2 payload 字段 |
| created_at | int64 | 观测产生时间戳（毫秒） | WP02-T010 时间语义 |

### 5.2 Observation 可选字段

| 字段名 | 类型语义 | 语义说明 | 约束 |
|---|---|---|---|
| error | ErrorInfo | 执行失败时的结构化错误信息 | 复用 WP02 已冻结 ErrorInfo 对象 |
| side_effects | vector\<string\> | 执行引起的副作用声明列表 | 声明式描述，不含执行逻辑 |
| tool_call_id | string | 关联的工具调用标识 | source=ToolExecution/McpRemote 时使用 |
| worker_task_id | string | 关联的 Worker 子任务标识 | source=WorkerAgent 时使用 |
| request_id | string | 关联的入口请求标识 | 从 AgentRequest.request_id 透传 |
| goal_id | string | 关联的目标标识 | 从 GoalContract.goal_id 透传 |
| duration_ms | int64 | 执行耗时（毫秒） | 若存在则 > 0 |
| tags | vector\<string\> | 检索/审计标签 | 不承载执行控制信号 |

### 5.3 Observation 禁止字段（守卫边界）

以下字段或同类语义不得进入 Observation：

1. 计划/决策字段：plan_graph、step_list、action_sequence、action_decision、next_step。
2. Runtime 内部状态：fsm_state、retry_counters、backoff_ms、circuit_state。
3. 恢复控制字段：checkpoint_ref、recovery_action、replan_trigger。
4. 消息渲染字段：final_messages、rendered_prompt、prompt_bundle、token_usage。
5. Provider 私有字段：model_provider_args、vendor_tool_schema。
6. 推理摘要字段：summary、key_facts、confidence（归 ObservationDigest）。

### 5.4 ObservationSource 枚举（预定义，T007 细化）

| 值 | 语义 | 说明 |
|---|---|---|
| Unspecified (0) | 未指定 | WP02-T012 哨兵值 |
| ToolExecution (1) | 本地工具执行结果 | Tool System 输出 |
| WorkerAgent (2) | Worker Agent 子任务返回 | ADR-008 Multi-Agent 场景 |
| Retrieval (3) | 知识检索结果 | Memory/Knowledge 检索 |
| HumanFeedback (4) | 人工反馈/澄清/确认 | 人在回路场景 |

> 注：T006 仅定义最小四来源枚举（与冻结计划 §7 直接对应）。McpRemote/WorkflowStep 等扩展值留待 T007 决策是否追加。

## 6. 与上下游对象的边界关系

| 上游/下游 | 关系 | 数据流向 | 边界约束 |
|---|---|---|---|
| ToolExecution → Observation | 生产 | tool_result 归一化为 Observation | Tool 不设置 Observation 的推理字段 |
| WorkerAgent → Observation | 生产 | worker_result 折叠为 Observation | 失败格式统一视为 success=false |
| Retrieval → Observation | 生产 | retrieval_result 归一化为 Observation | payload 为检索结果摘要 |
| HumanFeedback → Observation | 生产 | human_input 归一化为 Observation | 人工反馈不含执行副作用 |
| Observation → Reasoner | 消费 | latest_observation 作为决策输入 | Reasoner 只读，不修改 Observation |
| Observation → ReflectionEngine | 消费 | 失败 Observation 触发归因分析 | 反思结论不回写 Observation |
| Observation → ObservationDigest | 派生 | DigestBuilder 从 Observation 提取摘要 | Digest 不修改原始 Observation |
| Observation → Memory | 写入 | write_observation(session_id, latest) | Memory 持久化，不修改 Observation |
| Observation → Checkpoint | 快照 | Checkpoint 记录 Observation 引用 | Observation 不含 checkpoint_ref |
| Observation → ContextPacket | 间接 | 经 Digest 进入 ContextPacket | ContextPacket 不直接包含原始 Observation |

## 7. 方案对比与决策

### 7.1 方案 A：统一 struct + source 枚举区分（推荐）

设计方式：所有来源输出折叠到一个 Observation struct，通过 ObservationSource 枚举区分来源类型。

优点：
1. 与架构 3.8.2 统一折叠要求完全一致。
2. 消费者（Reasoner/Memory/Digest）无需 switch 不同类型。
3. 与已冻结 ErrorInfo 对象直接组合。

缺点：
1. 不同来源的 payload 结构差异需在运行期约定。

### 7.2 方案 B：继承层次 + 多态

设计方式：定义 ObservationBase，各来源派生子类。

优点：编译期区分来源类型。

缺点：
1. 与架构"统一折叠"要求冲突。
2. 消费者需处理多态分发，增加耦合。
3. 序列化/contract testing 复杂度高。

### 7.3 决策

采用方案 A。取舍理由：方案 A 与架构 3.8.2 统一折叠要求一致，且与已有 GoalContract/AgentRequest 等 struct 模式统一。

## 8. 验收清单

1. 已定义 Observation 统一折叠语义（四类来源归一化）。
2. 已列出必填字段（5 项）与可选字段（8 项）。
3. 已定义 ObservationSource 枚举（4 类 + Unspecified）。
4. 已定义禁止字段清单（6 类禁止）。
5. 已定义与上下游对象的边界关系（4 生产者 + 6 消费者/下游）。
6. 结论可追溯到架构 3.8.2、ADR-006/007/008、冻结计划 §7。

## 9. D Gate 结果

| 检查项 | 结果 |
|---|---|
| 统一折叠语义是否与架构 3.8.2 对齐 | ✅ |
| 最小字段是否覆盖 source/success/payload/error/side_effects | ✅ |
| ObservationSource 枚举是否含 Unspecified | ✅ WP02-T012 |
| 禁止字段是否阻断 Plan/Runtime/Digest 越界 | ✅ 6 类 |
| 与 ErrorInfo 关联是否复用已冻结结构 | ✅ |
| 与 ObservationDigest 分层是否清晰 | ✅ 归 T008 |
| 是否达到进入 -B 条件 | ✅ 通过 |

## 10. 风险与回退

1. 风险：payload 结构在不同来源间差异过大，导致消费者需自行解析。
回退：payload 为结构化字符串（JSON），消费者按 source 类型解析。后续可考虑 typed_payload 扩展。
2. 风险：error 字段与 success=true 同时存在导致语义歧义。
回退：守卫层校验 success=false 时 error 应存在，success=true 时 error 应为 nullopt。
3. 风险：side_effects 列表无法被程序化消费。
回退：side_effects 为声明式字符串列表，补偿逻辑不在 Observation 层实现。
4. 风险：observation_id 生成策略未冻结导致引用断裂。
回退：observation_id 必须在生产端生成，全链路不可变。

## 11. 下一任务建议

1. WP03-T007-D：列出 Observation 来源分类与引用规则。
2. WP03-T007-B：新增 ObservationSource 枚举与引用校验器。
