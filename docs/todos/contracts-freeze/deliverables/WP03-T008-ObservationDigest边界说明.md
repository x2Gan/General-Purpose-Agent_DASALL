# WP03-T008 ObservationDigest 分层边界说明

最近更新时间：2026-03-17
任务状态：Done
任务编号：WP03-T008
上游输入：架构 5.2.6、Blueprint §3.6、ADR-006 §6.1、冻结计划 §7/§8、T006-D（Observation 语义说明）、T007-D（来源分类表）

## 1. 任务理解

本任务只处理 WP03-T008：定义 ObservationDigest 与 Observation 的分层边界。

具体交付物：
1. ObservationDigest 字段语义与必填/可选分组。
2. ObservationDigest 与 Observation 互斥字段清单（分层边界）。
3. ObservationDigest 的上下游关系与消费者。
4. 守卫校验规则清单（2 层）。

本任务不处理：
1. Observation 对象级结构或字段增删（T006 已冻结）。
2. ObservationSource 枚举扩展（T007 已冻结）。
3. BeliefState / ContextPacket / Checkpoint（T009 及后续）。
4. DigestBuilder 实现细节（归 tools/cognition 实现层）。
5. Digest 序列化格式或持久化策略。

## 2. 约束与边界

### 2.1 直接约束（可追溯）

1. 架构 5.2.6：ObservationDigest 必须包含 summary、key_facts、citations、omitted_details、confidence。
2. 架构 5.2.6：Observation Digest 是 ContextOrchestrator 标准输入之一，不应让 Reasoner 直接处理未经治理的大块原始工具输出。
3. ADR-006 §6.1：ContextPacket 第 6 项应含 latest_observation_digest。
4. 冻结计划 §7：观测链路：ToolResult→Observation→ObservationDigest。
5. 冻结计划 §8 阶段2 第4条：ObservationDigest 必须与 Observation 分层，前者面向推理消费，后者保留更完整执行语义。
6. 架构主循环伪代码：observation_digest_.build(tool_result) 生成 Digest，memory.write_digest(session_id, digest) 持久化。
7. 架构 5.3.7：ContextOrchestrator 将本轮 Observation Digest 回写 Memory。
8. 架构 6.2 步骤9：Tool/MCP 结果被标准化为 Observation Digest 写回 Memory。
9. T006-D §5.3 禁止字段：summary、key_facts、confidence 明确归 ObservationDigest，禁止出现在 Observation。
10. WP02-T012：所有枚举必须含 Unspecified 哨兵值。
11. WP02-T010：时间字段为毫秒时间戳。
12. WP02-T009：标识字段需入口生成并全链路透传。

### 2.2 边界与非目标

边界：
1. 仅定义 ObservationDigest 对象级字段语义与分层边界。
2. 仅冻结 Digest 与 Observation 的互斥关系。
3. 仅定义守卫校验规则清单。

非目标：
1. 不实现 DigestBuilder 的压缩算法。
2. 不定义 Digest 的持久化 schema。
3. 不引入 Observation 执行语义字段（payload、error、side_effects、source）。
4. 不引入运行态字段（fsm_state、retry_counters、backoff_ms）。
5. 不引入消息渲染字段（final_messages、rendered_prompt）。

### 2.3 前置依赖检查

1. T006-D/B 已 Done：Observation 统一折叠语义冻结，禁止字段清单含 summary/key_facts/confidence。
2. T007-D/B 已 Done：ObservationSource 枚举与引用规则冻结。
3. WP02 冻结包 Done：枚举规则、时间语义、标识规则已冻结。
4. WP01 ObservationDigestTag.h 存在：Stable 类型占位符。
5. 当前无阻塞项。

## 3. 研究证据链

### 3.1 本地证据清单

| 编号 | 证据来源 | 关键结论 |
|---|---|---|
| L1 | 架构 5.2.6 | Digest 必须含 summary/key_facts/citations/omitted_details/confidence 五字段 |
| L2 | 架构 5.2.6 | Digest 是 ContextOrchestrator 标准输入，不让 Reasoner 直接处理原始输出 |
| L3 | ADR-006 §6.1 | ContextPacket 含 latest_observation_digest |
| L4 | 冻结计划 §8 阶段2 | ObservationDigest 必须与 Observation 分层 |
| L5 | 冻结计划 §7 | Observation→ObservationDigest 是观测链路标准流转 |
| L6 | 架构 11.1 伪代码 | observation_digest_.build(tool_result)；memory.write_digest(session_id, digest) |
| L7 | T006-D §5.3 | summary/key_facts/confidence 归 Digest，禁止进入 Observation |
| L8 | Blueprint §3.6 | struct ObservationDigest { summary, key_facts, citations, omitted_details, confidence } |
| L9 | 架构 5.3.7 | ContextOrchestrator 将 Observation Digest 回写 Memory |
| L10 | WP01 ObservationDigestTag.h | Stable 类型占位符已存在 |

### 3.2 外部参考清单

| 编号 | 参考来源 | 关键结论 |
|---|---|---|
| E1 | LangChain ToolMessage + runnable.summarize | 原始输出与压缩摘要分层；摘要独立于原始数据流 |
| E2 | OpenAI Assistants API retrieval | 检索结果经 summarizer 压缩后进入 thread context，原始文件独立保留 |
| E3 | Martin Fowler — Consumer-Driven Contracts | 消费者驱动 Digest 字段选择；原始数据由生产者保留 |
| E4 | Google DeepMind — Tool Response Summarization | 工具返回需 observation summarization 阶段，提取 key findings、citations、confidence |

### 3.3 对本任务的可落地启发

1. ObservationDigest 是 Observation 的推理友好投影，只承载 Reasoner/ContextOrchestrator 需要的压缩信息。
2. 五字段结构已由架构 5.2.6 冻结，不可增删核心字段。
3. Digest 必须通过 observation_id 关联源 Observation，不内嵌原始 payload。
4. 分层守卫应验证"Digest 不含执行语义字段"——与 T006 禁止字段对称。
5. confidence 应为 [0.0, 1.0] 浮点值，明确上下界约束。

## 4. ObservationDigest 字段语义定义

### 4.1 必填字段（5 项）

| 字段名 | 类型语义 | 语义说明 | 主要依据 |
|---|---|---|---|
| observation_id | string | 本 Digest 对应的源 Observation 的唯一标识。用于建立 Digest→Observation 追溯关系。不可为空。 | WP02-T009 标识规则；架构 11.1 write_digest 需要关联 |
| summary | string | 压缩后的短摘要，面向 Reasoner 和 ContextOrchestrator 消费。不含原始 payload 全文。 | 架构 5.2.6 第 1 项 |
| key_facts | vector\<string\> | 从原始 Observation payload 中提取的高价值事实列表。每条事实应自包含、可独立引用。 | 架构 5.2.6 第 2 项 |
| citations | vector\<string\> | 指向原始证据或结果引用的列表。支持审计追溯，不含完整原文。 | 架构 5.2.6 第 3 项 |
| confidence | float | 摘要可信度标注，取值范围 [0.0, 1.0]。0.0 表示完全不可信，1.0 表示完全可信。 | 架构 5.2.6 第 5 项 |

### 4.2 可选字段（4 项）

| 字段名 | 类型语义 | 语义说明 | 约束 |
|---|---|---|---|
| omitted_details | vector\<string\> | 被裁剪的内容提示列表。标注哪些原始细节未纳入 summary/key_facts。 | 架构 5.2.6 第 4 项。可为空表示无裁剪 |
| source | ObservationSource 枚举 | 源 Observation 的来源通道。冗余复制自 Observation.source，便于消费者不回溯原始 Observation 即可获知来源类型。 | 若存在则不为 Unspecified |
| created_at | int64 | Digest 产生时间戳（毫秒）。区别于源 Observation.created_at。 | WP02-T010 时间语义。若存在则 > 0 |
| tags | vector\<string\> | 检索/审计标签。不承载执行控制信号。 | 与 Observation.tags 语义一致 |

### 4.3 设计理由

1. observation_id 为必填：Digest 必须可追溯到源 Observation（架构 11.1 write_digest 关联 session.id + latest）。
2. summary 为必填：Digest 的核心存在价值是提供压缩摘要（架构 5.2.6 第一条）。
3. key_facts 为必填：高价值事实是 Reasoner 决策的关键输入（架构 5.2.6 第二条）。
4. citations 为必填：审计追溯要求（架构 5.2.6 第三条）。
5. confidence 为必填：推理消费者需要据此判断可信度（架构 5.2.6 第五条）。
6. omitted_details 为可选：无裁剪时可不提供（架构 5.2.6 第四条允许为空）。
7. source 为可选：冗余字段，便于消费者快速分类。
8. created_at 为可选：Digest 产生时间可通过其他途径获取。
9. tags 为可选：与 Observation 模式一致。

## 5. ObservationDigest 与 Observation 分层边界

### 5.1 互斥字段表

| 字段类别 | Observation 持有 | ObservationDigest 持有 | 分层理由 |
|---|---|---|---|
| 执行结果 payload | ✅ payload（原始结构化数据） | ❌ 禁止 | Digest 只承载压缩摘要，不复制原始输出 |
| 执行成功状态 | ✅ success | ❌ 禁止 | success/failure 是执行语义，Digest 通过 confidence 表达可信度 |
| 结构化错误信息 | ✅ error（ErrorInfo） | ❌ 禁止 | 错误诊断归 ReflectionEngine/RecoveryManager，不回灌推理 |
| 副作用声明 | ✅ side_effects | ❌ 禁止 | 副作用归补偿/审计消费者，不进入推理上下文 |
| 执行关联字段 | ✅ tool_call_id、worker_task_id | ❌ 禁止 | 执行追溯归 Observation，Digest 通过 observation_id 间接关联 |
| 执行耗时 | ✅ duration_ms | ❌ 禁止 | 执行性能指标不进入推理消费 |
| 压缩摘要 | ❌ 禁止（T006 §5.3） | ✅ summary | 摘要是推理消费接口 |
| 高价值事实 | ❌ 禁止（T006 §5.3） | ✅ key_facts | 事实提取面向推理决策 |
| 摘要可信度 | ❌ 禁止（T006 §5.3） | ✅ confidence | 可信度面向推理判断 |
| 证据引用 | ❌ 禁止 | ✅ citations | 审计引用面向推理追溯 |
| 裁剪提示 | ❌ 禁止 | ✅ omitted_details | 裁剪提示面向推理完整性判断 |

### 5.2 共享字段表

| 字段名 | Observation | ObservationDigest | 共享理由 |
|---|---|---|---|
| observation_id | ✅ 必填（Observation 唯一标识） | ✅ 必填（关联到源 Observation） | Digest→Observation 追溯键 |
| source | ✅ 必填 | ✅ 可选（冗余复制） | 便于消费者不回溯即可获知来源 |
| created_at | ✅ 必填（Observation产生时间） | ✅ 可选（Digest产生时间） | 两者时间基准不同 |
| tags | ✅ 可选 | ✅ 可选 | 检索/审计标签通用 |

### 5.3 分层对称性

| 层面 | Observation 禁止 | ObservationDigest 禁止 |
|---|---|---|
| 推理摘要 | summary、key_facts、confidence、citations、omitted_details | — |
| 执行结果 | — | payload、success、error、side_effects、duration_ms |
| 执行关联 | — | tool_call_id、worker_task_id |
| 计划/决策 | plan_graph、step_list、action_decision、next_step | plan_graph、step_list、action_decision、next_step |
| Runtime 内部 | fsm_state、retry_counters、backoff_ms | fsm_state、retry_counters、backoff_ms |
| 消息渲染 | final_messages、rendered_prompt、prompt_bundle | final_messages、rendered_prompt、prompt_bundle |
| Provider 私有 | model_provider_args、vendor_tool_schema | model_provider_args、vendor_tool_schema |

## 6. 上下游关系与消费者

| 上游/下游 | 关系 | 数据流向 | 边界约束 |
|---|---|---|---|
| Observation → ObservationDigest | 派生 | DigestBuilder 从 Observation 提取摘要 | Digest 不修改原始 Observation |
| ObservationDigest → ContextOrchestrator | 消费 | latest_observation_digest 进入 ContextPacket | ADR-006 §6.1 第 6 项 |
| ObservationDigest → Memory | 写入 | memory.write_digest(session_id, digest) | 架构 11.1 伪代码 |
| ObservationDigest → Reasoner | 间接消费 | 经 ContextPacket 中的 latest_observation_digest | Reasoner 不直接处理原始 Observation |
| ObservationDigest → Planner | 间接消费 | 经 ContextPacket 为 replan 提供依据 | 规划决策基于摘要而非原始输出 |
| DigestBuilder → ObservationDigest | 生产 | observation_digest_.build(tool_result) | 架构 11.1 伪代码 |

## 7. 守卫校验规则清单

### 7.1 Layer 1：必填字段校验

| 规则编号 | 规则描述 | 通过条件 | 失败消息 |
|---|---|---|---|
| R1 | observation_id 必须存在且非空 | has_non_empty_value(observation_id) | "observation_id is required and must be non-empty" |
| R2 | summary 必须存在且非空 | has_non_empty_value(summary) | "summary is required and must be non-empty" |
| R3 | key_facts 必须存在 | key_facts.has_value() | "key_facts is required" |
| R4 | citations 必须存在 | citations.has_value() | "citations is required" |
| R5 | confidence 必须存在且在 [0.0, 1.0] 范围 | confidence.has_value() && *confidence >= 0.0f && *confidence <= 1.0f | "confidence is required and must be in [0.0, 1.0]" |

### 7.2 Layer 2：分层边界校验

| 规则编号 | 规则描述 | 通过条件 | 失败消息 |
|---|---|---|---|
| R6 | source 若存在则不为 Unspecified | 不存在或不为 Unspecified | "source must not be Unspecified when present" |
| R7 | created_at 若存在则 > 0 | 不存在或 > 0 | "created_at must be positive when present" |

### 7.3 守卫层叠关系

| 层级 | 守卫 | 已有/新增 | 来源 |
|---|---|---|---|
| Layer 1 | validate_observation_digest_required_fields | **新增** | T008-B |
| Layer 2 | validate_observation_digest_boundary | **新增** | T008-B |

Layer 2 在 Layer 1 通过后执行。

## 8. 验收清单

1. 已定义 ObservationDigest 5 必填 + 4 可选字段语义。
2. 已定义与 Observation 的互斥字段表（11 项互斥、4 项共享）。
3. 已定义分层对称性（Observation 禁摘要字段、Digest 禁执行字段）。
4. 已定义上下游关系（1 生产者 + 5 消费者/下游）。
5. 已定义 7 条守卫校验规则（5 必填 + 2 边界）。
6. 所有结论可追溯到架构 5.2.6、ADR-006 §6.1、冻结计划 §7/§8、T006-D §5.3。

## 9. D Gate 结果

| 检查项 | 结果 |
|---|---|
| 五字段是否覆盖 summary/key_facts/citations/omitted_details/confidence | ✅ |
| 与 Observation 分层是否互斥明确 | ✅ 11 项互斥 |
| observation_id 是否建立 Digest→Observation 追溯 | ✅ 必填 |
| confidence 范围是否冻结为 [0.0, 1.0] | ✅ |
| 消费者清单是否覆盖 ContextOrchestrator/Memory/Reasoner/Planner | ✅ |
| 守卫规则是否可直接翻译为代码 | ✅ 7 条 |
| 是否达到进入 -B 条件 | ✅ 通过 |

## 10. 风险与回退

1. 风险：DigestBuilder 压缩质量低导致 key_facts 为空向量。
   回退：守卫层允许空向量（key_facts 必须 has_value 但可为空列表）；质量由 confidence 值体现。
2. 风险：confidence 值的语义在不同来源间不一致。
   回退：confidence 为摘要层可信度，不是执行成功概率。来源差异由消费者结合 source 枚举判断。
3. 风险：omitted_details 过长导致 ContextPacket token 预算膨胀。
   回退：omitted_details 为可选字段，ContextOrchestrator 可裁剪。
4. 风险：Digest 产生延迟导致 ContextOrchestrator 拿到陈旧 Digest。
   回退：created_at 可选字段标注 Digest 产生时间，消费者可判断新鲜度。

## 11. 下一任务建议

1. WP03-T008-B：新增 ObservationDigest.h + ObservationDigestGuards.h + 契约测试。
2. WP03-T009-D：BeliefState 主链路定位说明。
