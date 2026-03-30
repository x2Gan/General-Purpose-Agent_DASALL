# WP03-T003 AgentRequest 字段表

最近更新时间：2026-03-17
任务状态：Done
任务编号：WP03-T003
上游输入：WP03-T002 AgentRequest 语义说明、WP02 横切规则（T001/T002/T003/T007/T009/T010/T013）

## 1. 任务理解

本任务只处理 WP03-T003：列出 AgentRequest 必填字段和可选字段，交付“AgentRequest 字段表”。

本任务不处理：

1. GoalContract 字段与语义（WP03-T004/T005）。
2. ContextPacket/Observation/Checkpoint 字段（WP03-T006 及后续）。
3. contracts 代码实现与序列化落盘。

## 2. 约束与边界

### 2.1 直接约束（可追溯）

1. 来源 WP-03 TODO：T003 完成判定要求“字段仅围绕入口请求、约束、预算和请求元数据”。
2. 来源 WP03-T002：AgentRequest 只承载入口意图、约束、预算、请求元数据，不夹带 runtime 内部状态和 provider 私有字段。
3. 来源计划阶段 2：AgentRequest 只表达统一入口请求语义，不夹带 runtime 内部状态。
4. 来源工程蓝图：AgentRequest 由入口 Request Normalizer 构造，且入口链路至少包含 request_id/session_id/trace_id。
5. 来源 WP02-T009：request_id/session_id/trace_id 为入口级主标识，入口生成并透传。
6. 来源 WP02-T007：RuntimeBudget 最小维度覆盖 token/turn/tool_call/latency/replan。
7. 来源 WP02-T010：时间语义应区分 created_at/deadline_at/timeout_ms/ttl，deadline_at 为执行判定主语义。
8. 来源 WP02-T002/T003：新增优于修改、语义变化视为 breaking；字段可选性变化需谨慎。

### 2.2 边界与非目标

边界：

1. 仅定义 AgentRequest 字段级别必填/可选，不定义具体 C++ 类型别名与默认值实现。
2. 仅定义单 Agent 入口字段，不引入 MultiAgentRequest/WorkerTask 字段。
3. 仅冻结字段语义与分组，不冻结具体编码格式和 provider 参数映射。

非目标：

1. 不引入运行态字段：fsm_state、retry_counters、checkpoint_ref 等。
2. 不引入 provider 私有字段：provider_payload、rendered_prompt、vendor model params。
3. 不定义字段校验器实现代码。

### 2.3 前置依赖检查

1. T002 已有交付物且状态 In Review，可作为 T003 输入。
2. M2 冻结包状态 Done，B1/B2 已 Closed，可进入 WP-03 字段冻结任务。
3. 当前无阻塞项，继续推进。

## 3. 方案对比与决策

### 3.1 方案 A：最小必填 + 扩展可选（推荐）

设计方式：

1. 仅将入口闭环必需字段设为必填。
2. 将策略、预算、时间类增强信息设为可选但语义冻结。

优点：

1. 与 WP02 兼容规则一致，旧入口可平滑升级。
2. 保持 AgentRequest 轻量，避免过早绑定策略细节。
3. 直接支撑后续 T004/T005 通过 GoalContract 承接复杂目标约束。

缺点：

1. 部分策略约束需依赖后续对象补足，评审时需明确“可选不等于无效”。

### 3.2 方案 B：强约束必填化

设计方式：

1. 将预算、约束、时间窗口统一设为必填。
2. 强化入口完整性，减少运行期分支。

优点：

1. 入口对象更“完整”。
2. 运行时缺省分支较少。

缺点：

1. 与“新增优于修改、可选改必填为高风险”的兼容原则冲突。
2. 增加接入成本，不利于多入口逐步迁移。

### 3.3 决策

采用方案 A。

取舍理由：方案 A 更符合 WP-02 兼容与演进规则，同时满足 T003“围绕入口请求/约束/预算/元数据”的完成判定。

## 4. 最终产出

### 4.1 AgentRequest 字段分组

1. 入口请求字段组（request payload）。
2. 约束字段组（constraints）。
3. 预算字段组（budget）。
4. 请求元数据字段组（metadata）。

### 4.2 AgentRequest 必填字段表

| 字段名 | 分组 | 必填性 | 语义说明 | 主要依据 |
|---|---|---|---|---|
| request_id | 请求元数据 | 必填 | 请求级唯一标识，入口生成并全链路透传 | WP02-T009、工程蓝图入口链路 |
| session_id | 请求元数据 | 必填 | 会话级关联标识，用于多轮语义连续性 | WP02-T009、工程蓝图入口链路 |
| trace_id | 请求元数据 | 必填 | 追踪链路标识，用于日志/事件/审计对齐 | WP02-T009、工程蓝图入口链路 |
| user_input | 入口请求 | 必填 | 用户本轮请求的原始意图载荷（文本/结构化输入） | WP03-T002 入口意图语义 |
| request_channel | 入口请求 | 必填 | 入口来源通道（cli/gateway/daemon/simulator 等归一化值） | 工程蓝图 apps 入口归一化 |
| created_at | 请求元数据 | 必填 | 请求创建时间，作为时序基线 | WP02-T010 时间语义 |

### 4.3 AgentRequest 可选字段表

| 字段名 | 分组 | 必填性 | 语义说明 | 约束 |
|---|---|---|---|---|
| goal_hint | 入口请求 | 可选 | 对目标的结构化补充提示，供后续 GoalContract 归并 | 不替代 GoalContract |
| domain_context | 入口请求 | 可选 | 入口侧携带的最小业务上下文摘要 | 仅摘要，不含渲染消息 |
| constraint_set | 约束 | 可选 | 安全/策略/权限等声明式约束集合 | 不包含执行态字段 |
| approval_policy_hint | 约束 | 可选 | 需审批动作的入口提示 | 最终策略由后续对象裁定 |
| runtime_budget | 预算 | 可选 | 预算上限声明（token/turn/tool_call/latency/replan） | 维度遵循 WP02-T007 |
| timeout_ms | 预算 | 可选 | 执行时长策略输入，毫秒单位 | 若与 deadline_at 并存，以 deadline_at 为主 |
| deadline_at | 预算 | 可选 | 执行硬截止时间 | 执行判定主字段 |
| priority | 约束 | 可选 | 请求优先级提示 | 仅提示，不直接等于调度状态 |
| idempotency_key | 请求元数据 | 可选 | 幂等请求去重键 | 不等同 request_id |
| locale | 请求元数据 | 可选 | 语言区域偏好 | 不包含 provider 语言参数 |
| client_capabilities | 请求元数据 | 可选 | 客户端可见能力声明（如流式支持） | 仅能力声明，不含 provider 私参 |
| tags | 请求元数据 | 可选 | 检索/审计标签 | 不承载执行控制信号 |

### 4.4 明确禁止字段（T003 守卫）

以下字段或同类语义不得进入 AgentRequest：

1. runtime 内部状态：fsm_state、retry_counters、backoff_ms、circuit_state、checkpoint_ref。
2. provider 私有字段：provider_payload、rendered_prompt、model_provider_args、vendor_tool_schema。
3. 执行结果字段：observation、observation_digest、belief_state、agent_result。
4. 协同子域字段：worker_task、lease_id、parent_task_id、multi_agent_request。

### 4.5 字段演进建议（兼容性）

1. 新增字段默认走可选，避免将可选字段直接升级为必填。
2. 不修改既有字段语义；若语义变化，按 breaking 候选进入专门评审。
3. 对 timeout_seconds 历史输入仅兼容读取并归一化到 timeout_ms，不再新增写入。

### 4.6 与后续任务映射

1. 直接输入 WP03-T004/T005：GoalContract 接收 goal_hint/constraint_set 的契约化收敛。
2. 直接输入 WP03-T010/T011：ContextPacket 消费入口语义与预算元数据摘要。
3. 直接输入合同测试：AgentRequest schema 边界检查（禁止 runtime/provider 字段）。

### 4.7 Mode Extension

Design 模式：

1. 本文档为评审材料，不改代码。
2. 已输出字段建议、边界说明、兼容性建议。
3. 可直接落盘路径：docs/todos/contracts/deliverables/WP03-T003-AgentRequest字段表.md。

Build 模式：

1. 代码变更清单（本次仅文档）：
   - 更新 WP-03 TODO 的 T003 状态与交付物链接。
   - 新增 T003 字段表文档。
2. 关键接口（后续实现占位）：
   - contracts/agent/AgentRequest.h 字段定义。
   - apps/*/RequestNormalizer 入参归一化接口。
3. 测试点（后续契约测试）：
   - 必填字段完整性校验。
   - 可选字段缺省兼容校验。
   - 禁止字段渗透校验（runtime/provider）。
4. 验证步骤：
   - 构建：在实现落盘后执行 contracts + tests 构建。
   - 测试：执行 contract tests 验证字段必填/可选与禁止项。
   - 契约校验：按 WP02-T013 Checklist 的 A/B/E 项逐条核查。

## 5. 验收清单

1. 已产出 AgentRequest 字段表，含必填与可选字段。
2. 字段范围仅覆盖入口请求、约束、预算、请求元数据。
3. 已给出禁止字段清单，防止 runtime/provider 污染。
4. 结论可追溯到 T002 与 WP-02 规则。

Quality Gate 回答：

1. 当前任务是否达成 Done Criteria：是。
2. 产出是否可被下一任务直接消费：是，T004/T005 可直接消费。
3. 是否引入 breaking change 风险：否，本次仅文档冻结。
4. 是否需要触发 ADR 或版本变更流程：否，未改写 ADR 结论。

## 6. 风险与回退

1. 风险：字段膨胀导致 AgentRequest 变成“万能入口对象”。
回退：超出四大分组的字段一律下沉至后续对象（GoalContract/ContextPacket/Runtime 内部态）。
2. 风险：实现阶段将可选字段误改为必填。
回退：按 WP02 字段演进规则处理，保留可选并通过新字段迁移。
3. 风险：历史字段单位或命名混用（如 timeout_seconds）。
回退：保持兼容读取 + 统一写入 timeout_ms + 评审门禁阻断新增秒级字段。

## 7. 下一任务建议（仅直接后继任务）

1. WP03-T004 定义 GoalContract 的职责边界。
