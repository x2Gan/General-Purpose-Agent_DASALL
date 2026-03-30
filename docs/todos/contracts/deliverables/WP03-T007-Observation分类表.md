# WP03-T007 Observation 来源分类与引用规则

最近更新时间：2026-03-17
任务状态：Done
任务编号：WP03-T007
上游输入：T006-D（Observation 语义说明）、WP02-T006（ErrorSource 引用约定）、架构 3.8.2、ADR-007/ADR-008、冻结计划 §7

## 1. 任务理解

本任务只处理 WP03-T007：列出 Observation 来源分类与引用规则。

具体交付物：
1. ObservationSource 枚举冻结分类表（决策是否追加扩展值）。
2. 每个 source 值的关联字段引用规则（source→correlation 映射）。
3. ObservationSource 与 ErrorSourceRef.ref_type 的对齐规则。
4. 守卫校验规则清单（source→correlation 一致性校验）。

本任务不处理：
1. Observation 对象级结构或字段增删（已由 T006 冻结）。
2. ObservationDigest 分层边界（归 T008）。
3. 来源通道的具体执行实现（归 runtime/tools/multi_agent）。
4. 新增 Observation 字段。

## 2. 约束与边界

### 2.1 直接约束（可追溯）

1. 架构 3.8.2：四类来源统一折叠为 Observation。source 字段标识来源通道。
2. 冻结计划 §7：ToolResult / WorkerResult / RetrievalResult / HumanInput → Observation。
3. ADR-007 §4：Tool、Workflow、MCP 或外部执行结果统一映射为 Observation + ErrorInfo。
4. ADR-008 §3.2.5：AgentOrchestrator 把协同结果统一折叠为 Observation。Worker 回传结构化结果或失败 Observation。
5. T006-D §5.4：ObservationSource 已定义 4 值 + Unspecified。McpRemote/WorkflowStep 扩展留待 T007 决策。
6. T006-D §5.2：tool_call_id（source=ToolExecution）、worker_task_id（source=WorkerAgent）已定义为关联字段。
7. WP02-T006：ErrorSourceRefMinimal.ref_type 仅允许 observation/tool_call/worker_task/checkpoint。
8. WP02-T012：所有枚举必须包含 Unspecified 哨兵值。
9. WP02-T009：标识字段需入口生成并全链路透传。

### 2.2 边界与非目标

边界：
1. 仅定义 ObservationSource 枚举值的冻结分类与扩展决策。
2. 仅定义 source→correlation 字段映射规则。
3. 仅定义与 ErrorSourceRef.ref_type 的对齐规则。
4. 仅定义守卫校验规则清单（可直接翻译为代码）。

非目标：
1. 不新增或修改 Observation struct 字段（T006 已冻结）。
2. 不定义 ObservationDigest 语义（归 T008）。
3. 不实现具体的来源通道归一化逻辑（归 runtime 实现）。
4. 不修改 ErrorSourceRefMinimal 结构（WP02 已冻结）。

### 2.3 前置依赖检查

1. T006-D/B 已 Done：Observation 统一折叠语义冻结，ObservationSource 枚举已定义。
2. WP02-T006 已 In Review：ErrorSource 引用约定已交付。
3. WP02-T012 已 Done：枚举生命周期规则已冻结。
4. 当前无阻塞项。

## 3. 研究证据链

### 3.1 本地证据清单

| 编号 | 证据来源 | 关键结论 |
|---|---|---|
| L1 | 架构 3.8.2 | 四类来源：工具结果、检索结果、人工反馈、子 Agent 输出统一折叠 |
| L2 | 冻结计划 §7 观测链路 | ToolResult / WorkerResult / RetrievalResult / HumanInput → Observation |
| L3 | ADR-007 §4.1 | "Tool、Workflow、MCP 或外部执行结果统一映射为 Observation 与 ErrorInfo" |
| L4 | ADR-008 §4.4 | Worker 回传结构化结果或失败 Observation |
| L5 | T006-D §5.4 | 四来源枚举已定义；McpRemote/WorkflowStep 留待 T007 |
| L6 | Observation.h 注释 | "Extended source types (McpRemote, WorkflowStep) are deferred to T007" |
| L7 | 架构 4.5 | Tool System 将结果归一化为 Observation 回传 |
| L8 | WP02-T006 §4.2 | ref_type 仅 observation/tool_call/worker_task/checkpoint |
| L9 | Observation.h tool_call_id 注释 | "Relevant when source=ToolExecution or McpRemote" |
| L10 | 架构 3.8.4 | "Multi-Agent 输出和 Tool 输出必须最终汇聚成统一 Observation" |

### 3.2 外部参考清单

| 编号 | 参考来源 | 关键结论 |
|---|---|---|
| E1 | LangChain ToolMessage | tool_call_id 作为执行关联键，一对一映射到具体工具调用 |
| E2 | OpenAI Function Calling Response | 工具返回统一 {tool_call_id, role, content}，来源分类通过 role 区分 |
| E3 | Martin Fowler — Consumer-Driven Contracts | 来源分类枚举应保守冻结，消费者驱动扩展 |
| E4 | Azure Compensating Transaction Pattern | 来源引用需可追溯到执行上下文，支持补偿与审计 |

### 3.3 对本任务的可落地启发

1. ObservationSource 枚举应冻结为当前 4 类 + Unspecified，不追加 McpRemote/WorkflowStep。理由：MCP 执行通过 Tool System 归一化（架构 4.5），最终仍是 ToolExecution 通道；Workflow 步骤不是独立观测来源。
2. 每个 source 值必须映射到明确的关联字段：ToolExecution → tool_call_id 必存；WorkerAgent → worker_task_id 必存；其余来源无专属关联字段要求。
3. ObservationSource 枚举值应与 ErrorSourceRef.ref_type 形成可校验的映射关系。
4. 守卫校验应作为 Observation 的 Layer 3（在已有 Layer 1 必填 + Layer 2 边界之上），专门校验 source→correlation 一致性。
5. ObservationSource 枚举应从 Observation.h 提取到 ObservationSource.h 独立头文件，Observation.h 改为 include 引用。

## 4. ObservationSource 枚举冻结分类表

### 4.1 冻结决策

**决策：保持当前 4 类 + Unspecified，不追加 McpRemote/WorkflowStep。**

| 候选值 | 决策 | 理由 |
|---|---|---|
| Unspecified (0) | 保留 | WP02-T012 哨兵值，枚举生命周期必备 |
| ToolExecution (1) | 保留 | 架构 3.8.2 + 4.5：本地工具执行结果归一化通道 |
| WorkerAgent (2) | 保留 | ADR-008：Worker 子任务回传通道 |
| Retrieval (3) | 保留 | 冻结计划 §7：知识/记忆检索结果通道 |
| HumanFeedback (4) | 保留 | 冻结计划 §7：人在回路反馈通道 |
| McpRemote | **不追加** | MCP 远程工具执行通过 Tool System 归一化（架构 4.5），最终映射为 ToolExecution。MCP 来源可通过 tags 标注而非枚举区分 |
| WorkflowStep | **不追加** | Workflow 步骤不是独立观测来源；步骤执行走 ToolExecution 或 WorkerAgent 通道 |

### 4.2 分类语义表

| 枚举值 | 生产者 | 语义 | 典型 payload 内容 | 上游原始类型 |
|---|---|---|---|---|
| ToolExecution | Tool System（含 MCP 工具） | 本地或远程工具执行结果 | 工具输出 JSON | ToolResult |
| WorkerAgent | MultiAgentCoordinator | Worker 子任务执行结果 | 子任务结构化返回 | WorkerResult (SubTaskResult) |
| Retrieval | Knowledge/Memory Manager | 知识检索或记忆检索结果 | 检索结果摘要 | RetrievalResult |
| HumanFeedback | 用户端 / 澄清通道 | 人工反馈、澄清、确认 | 用户输入文本 | HumanInput |

### 4.3 枚举扩展策略

1. 当且仅当出现无法映射到现有四类的全新观测生产通道时，才追加枚举值。
2. 追加时必须同步更新 ObservationSourceGuards 的枚举范围校验。
3. 追加必须包含 T007 格式的分类表行、关联字段规则和守卫校验规则。
4. 不允许删除或重解释已冻结的枚举值语义。

## 5. Source→Correlation 关联字段引用规则

### 5.1 映射规则表

| source 值 | 必须存在的关联字段 | 可选关联字段 | 禁止存在的关联字段 | 规则理由 |
|---|---|---|---|---|
| ToolExecution | tool_call_id | request_id, goal_id | worker_task_id | ToolExecution 产出来自工具调用，必须可追溯到 tool_call_id；worker_task_id 属于 WorkerAgent 通道 |
| WorkerAgent | worker_task_id | request_id, goal_id | tool_call_id | WorkerAgent 产出来自子任务，必须可追溯到 worker_task_id；tool_call_id 属于 ToolExecution 通道 |
| Retrieval | （无专属必须字段） | request_id, goal_id, tool_call_id | worker_task_id | 检索结果不源于工具调用或子任务；若由工具驱动检索，tool_call_id 可选存在 |
| HumanFeedback | （无专属必须字段） | request_id, goal_id | tool_call_id, worker_task_id | 人工反馈不关联工具或子任务执行 |

### 5.2 通用关联规则

1. request_id：所有来源均可选存在，关联入口 AgentRequest.request_id。
2. goal_id：所有来源均可选存在，关联当前 GoalContract.goal_id。
3. tool_call_id 与 worker_task_id 互斥：同一个 Observation 不应同时存在两者（来源分类本身互斥）。
4. 关联字段缺失时不等于无效 Observation，仅表示追溯链路不完整（守卫可降级为 warning 或升级为 error）。

### 5.3 引用值约束

1. 所有关联字段（tool_call_id、worker_task_id、request_id、goal_id）若存在则必须非空。
2. 关联字段的值应符合 WP02-T009 标识规则（入口生成、全链路不可变）。
3. 关联字段的值不承载业务逻辑，仅用于追溯、审计和引用。

## 6. ObservationSource 与 ErrorSourceRef.ref_type 对齐规则

### 6.1 映射表

| ObservationSource | 对应 ErrorSourceRef.ref_type | 映射关系 | 说明 |
|---|---|---|---|
| ToolExecution | "tool_call" | source→ref_type：当 Observation 失败且 source=ToolExecution 时，ErrorInfo.source_ref.ref_type 应为 "tool_call" | ref_id = tool_call_id |
| WorkerAgent | "worker_task" | source→ref_type：当 Observation 失败且 source=WorkerAgent 时，ErrorInfo.source_ref.ref_type 应为 "worker_task" | ref_id = worker_task_id |
| Retrieval | "observation" | source→ref_type：检索失败时 ErrorInfo.source_ref.ref_type 回退为 "observation" | ref_id = observation_id（无专属通道标识） |
| HumanFeedback | "observation" | source→ref_type：人工反馈异常时 ErrorInfo.source_ref.ref_type 回退为 "observation" | ref_id = observation_id |

### 6.2 对齐校验规则

1. 当 Observation.success=false 且 error 存在时，error.source_ref.ref_type 应与 source 枚举值的映射一致。
2. ToolExecution 的 error.source_ref.ref_id 应等于 tool_call_id（若存在）。
3. WorkerAgent 的 error.source_ref.ref_id 应等于 worker_task_id（若存在）。
4. 此对齐校验为推荐规则，守卫层当前以 source→correlation 一致性为优先校验目标，error.source_ref 对齐校验可在后续守卫演进中追加。

## 7. 守卫校验规则清单（Layer 3：source→correlation 一致性）

### 7.1 校验规则表

| 规则编号 | 规则描述 | 触发条件 | 通过条件 | 失败消息 |
|---|---|---|---|---|
| R1 | ToolExecution 必须有 tool_call_id | source=ToolExecution | tool_call_id 存在且非空 | "tool_call_id is required when source is ToolExecution" |
| R2 | WorkerAgent 必须有 worker_task_id | source=WorkerAgent | worker_task_id 存在且非空 | "worker_task_id is required when source is WorkerAgent" |
| R3 | ToolExecution 不应有 worker_task_id | source=ToolExecution | worker_task_id 不存在 | "worker_task_id must not be present when source is ToolExecution" |
| R4 | WorkerAgent 不应有 tool_call_id | source=WorkerAgent | tool_call_id 不存在 | "tool_call_id must not be present when source is WorkerAgent" |
| R5 | HumanFeedback 不应有 tool_call_id | source=HumanFeedback | tool_call_id 不存在 | "tool_call_id must not be present when source is HumanFeedback" |
| R6 | HumanFeedback 不应有 worker_task_id | source=HumanFeedback | worker_task_id 不存在 | "worker_task_id must not be present when source is HumanFeedback" |
| R7 | 关联字段若存在则非空 | tool_call_id 或 worker_task_id 存在 | 值非空 | "correlation field must be non-empty when present" |

### 7.2 守卫层叠关系

| 层级 | 守卫 | 已有/新增 | 来源 |
|---|---|---|---|
| Layer 1 | validate_observation_required_fields | 已有 | T006-B |
| Layer 2 | validate_observation_boundary | 已有 | T006-B |
| Layer 3 | validate_observation_source_correlation | **新增** | T007-B |

Layer 3 在 Layer 2 通过后执行。Layer 3 专门校验 source 枚举值与关联字段（tool_call_id、worker_task_id）的一致性。

## 8. 与上下游对象的边界关系

| 关系 | 说明 |
|---|---|
| ObservationSource ← Observation.source | Observation.source 字段的类型定义，T007 将枚举从 Observation.h 提取到独立头文件 |
| ObservationSource → ObservationSourceGuards | 守卫消费枚举值进行 source→correlation 校验 |
| ObservationSource → ErrorSourceRefMinimal.ref_type | 失败场景下 source 枚举值映射到 ref_type 字符串 |
| T007 守卫 → T006 守卫 | Layer 3 依赖 Layer 1+2 通过后执行 |

## 9. 验收清单

1. 已决策 ObservationSource 枚举冻结为 4 类 + Unspecified，不追加 McpRemote/WorkflowStep。
2. 已定义每个 source 值的分类语义表。
3. 已定义 source→correlation 关联字段引用规则（必须/可选/禁止）。
4. 已定义 ObservationSource 与 ErrorSourceRef.ref_type 的对齐规则。
5. 已定义 Layer 3 守卫校验规则清单（7 条规则）。
6. 所有结论可追溯到架构 3.8.2、ADR-007/008、冻结计划 §7、T006-D、WP02-T006。

## 10. D Gate 结果

| 检查项 | 结果 |
|---|---|
| 枚举分类是否冻结为 4 类 + Unspecified | ✅ |
| 是否决策不追加 McpRemote/WorkflowStep | ✅ 有明确理由 |
| source→correlation 映射是否无歧义 | ✅ 7 条规则 |
| 与 ErrorSourceRef.ref_type 是否对齐 | ✅ 4 行映射 |
| 守卫层叠关系是否清晰（Layer 1→2→3） | ✅ |
| 是否达到进入 -B 条件 | ✅ 通过 |

## 11. 风险与回退

1. 风险：MCP 远程工具未来需独立来源标识，但当前冻结为 ToolExecution。
   回退：MCP 来源可通过 tags 标注（如 "mcp:server-name"），若后续确认需独立枚举值则追加，不破坏现有语义。
2. 风险：ToolExecution 通道禁止 worker_task_id 可能过严（如工具由 Worker 驱动执行的场景）。
   回退：工具由 Worker 驱动时，Tool 执行产生的 Observation 仅记 tool_call_id，Worker 层级的 Observation 记 worker_task_id，两者为不同 Observation 实例。
3. 风险：Layer 3 守卫对 Retrieval/HumanFeedback 缺少专属必须字段校验。
   回退：Retrieval 和 HumanFeedback 无专属关联标识（无 retrieval_call_id 或 feedback_id），通过 observation_id 追溯即可。若后续需要可追加。

## 12. 下一任务建议

1. WP03-T007-B：新增 ObservationSource 独立头文件 + ObservationSourceGuards + 契约测试。
2. WP03-T008-D：ObservationDigest 分层边界说明。
