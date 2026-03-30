# WP03-T014-D：AgentResult 最小输出语义说明

状态：Done
上游依赖：架构 §3.8.1/§3.8.4/§5.1/§11.1、ADR-008 §3.2/§5.2/§7.2、冻结计划 §阶段2、WP-02 ResultCode/ErrorInfo

---

## §1 任务理解

### 1.1 范围

- 定义 AgentResult 在主链路中的最终输出语义定位。
- 映射架构 §5.1 伪代码中的 5 个核心字段到契约结构。
- 添加架构 §3.8.1 要求的审计引用字段。
- 定义 AgentResultStatus 枚举（最终完成状态）。
- 定义两层守卫设计（L1 必填 + L2 边界约束）。
- 明确禁止字段与上下游边界。

### 1.2 排除项

- 不定义 ResponseBuilder 的组装逻辑（属 runtime 实现）。
- 不定义 structured_payload 的内容格式（属业务层定义）。
- 不定义 MultiAgentResult → AgentResult 的折叠策略（属 ADR-008 运行态）。
- 不涉及 Observation、BeliefState、Checkpoint 的内部结构（各自已冻结）。
- 后续 T015 负责端到端流图，本任务仅定义 AgentResult 自身。

---

## §2 约束与边界

### 2.1 直接约束（可追溯）

| 约束 | 来源 |
|---|---|
| AgentResult 是统一出口，包含文本回复、最终状态、结构化产物、审计引用 | 架构 §3.8.1 |
| 架构伪代码字段：ResultCode code, string response_text, JsonObject structured_payload, bool task_completed, ErrorInfo error | 架构 §5.1 |
| Multi-Agent 输出和 Tool 输出必须最终汇聚成 AgentResult | 架构 §3.8.4 #3 |
| 只有 AgentOrchestrator 可以提交最终 AgentResult | ADR-008 §7.2 #2 |
| MultiAgentResult 不直接等于 AgentResult | ADR-008 §5.2 |
| AgentResult 列为阶段2优先冻结对象 | 冻结计划 §阶段2 |
| ResultCode / ErrorInfo 已在 WP-02 冻结 | ResultCode.h, ErrorInfo.h |

### 2.2 边界与非目标

- **非目标**：不定义 ResponseBuilder 的组装过程。
- **非目标**：不定义 structured_payload 的 JSON schema。
- **非目标**：不包含 FSM 内部状态（属 Runtime/Checkpoint）。
- **非目标**：不包含认知状态（属 BeliefState）。
- **非目标**：不包含中间执行观察（属 Observation/ObservationDigest）。

### 2.3 前置依赖

| 前置 | 状态 |
|---|---|
| 架构 §3.8.1/§5.1/§3.8.4 (请求与结果契约) | Frozen |
| ADR-008 (AgentOrchestrator vs MultiAgentCoordinator) | Frozen |
| ResultCode.h (WP-02) | Done |
| ErrorInfo.h (WP-02) | Done |
| WP03-T001~T013 (主链路前置对象) | Done |

---

## §3 研究证据链

### 3.1 本地证据

| # | 文档 | 相关内容 |
|---|---|---|
| L1 | 架构 §3.8.1 | "AgentResult：统一出口，除了文本回复，还必须包含最终状态、结构化产物和审计引用" |
| L2 | 架构 §5.1 | struct AgentResult { ResultCode code; string response_text; JsonObject structured_payload; bool task_completed; ErrorInfo error; } |
| L3 | 架构 §3.8.4 #3 | "Multi-Agent 输出和 Tool 输出必须最终汇聚成统一 Observation 和 AgentResult" |
| L4 | 架构 §11.1 | `response_builder_.build(context_packet, plan, latest, decision)` 返回 AgentResult |
| L5 | ADR-008 §5.2 | "MultiAgentResult 不直接等于 AgentResult，只回传协同结果与冲突信息" |
| L6 | ADR-008 §7.2 #2 | "只有 AgentOrchestrator 可以提交最终 AgentResult 并控制用户面交互" |
| L7 | ADR-008 §3.2 #2 | AgentOrchestrator 管理"最终 AgentResult 生成时机" |
| L8 | 冻结计划 §阶段2 | "agent/AgentResult" 是阶段2优先冻结对象 |
| L9 | ResultCode.h | ResultCode enum + ResultCodeCategory 已冻结（WP-02） |
| L10 | ErrorInfo.h | ErrorInfo struct（failure_type, retryable, safe_to_replan, details, source_ref）已冻结 |
| L11 | Blueprint §8.1 | `agent/` 目录包含 AgentResult |
| L12 | 架构 §4.1 模块表 | Agent Facade: 输入 AgentRequest, 输出 AgentResult；ResponseBuilder: 输出 AgentResult |

### 3.2 外部参考

| # | 来源 | 启发 |
|---|---|---|
| E1 | Microsoft AutoGen — AgentMessage result schema: status + content + metadata | 结果对象包含状态码、内容、元数据的三层结构是行业标准 |
| E2 | LangChain AgentFinish — return_values + log + structured output separation | 结构化产物与文本回复分离，审计链独立 |
| E3 | Pact Contract Testing — consumer-driven output boundary | 输出契约边界由消费者驱动，必填/可选分离清晰 |

### 3.3 可落地启发

1. 架构 §5.1 的 5 字段（code, response_text, structured_payload, task_completed, error）是核心必填。
2. 架构 §3.8.1 要求"审计引用"→ 需添加 request_id, trace_id 溯源字段。
3. AgentResult 是单 Agent 主链路终态对象，不包含 Multi-Agent 协同细节。
4. structured_payload 使用 string 类型（JSON 序列化引用），不引入 JsonObject 依赖。
5. ResultCode / ErrorInfo 直接复用 WP-02 冻结定义，不重定义。

---

## §4 方案对比与决策

### 方案 A：最小输出契约（5 核心必填 + 审计溯源可选）

- 必填严格映射架构 §5.1 伪代码
- 添加 result_id（唯一标识）+ 审计溯源可选字段
- AgentResultStatus 枚举表达最终完成状态
- structured_payload 使用 string（序列化容器）。

### 方案 B：扩展输出契约（含嵌入 Checkpoint + 完整 Plan 状态）

- 嵌入最终 Checkpoint 详情
- 嵌入 PlanGraph 最终状态
- 膨胀为全量执行总结

### 决策

**选择方案 A**。理由：
1. 架构 §3.8.1 要求"最终状态、结构化产物和审计引用"，不要求嵌入内部执行细节。
2. Checkpoint 已是独立契约对象（T012），通过 checkpoint_ref 引用即可。
3. ADR-008 明确 AgentResult 是面向用户的最终输出，不是 runtime 内部审计日志。
4. AutoGen / LangChain 均采用 status + content + metadata 的精简模式。

---

## §5 最终产出

### 5.1 主链路位置

```
AgentRequest → GoalContract → ContextPacket → Observation
  → ObservationDigest → BeliefState → Checkpoint → [AgentResult]
```

AgentResult 是主链路终态对象，由 ResponseBuilder 组装、AgentOrchestrator 提交。

### 5.2 职责定义

AgentResult 是面向用户和上层系统的**最终输出契约对象**。其核心目的是：
1. 表达任务执行的终态结果码。
2. 提供人类可读的文本回复。
3. 承载结构化产物（如 JSON 数据、代码片段等的序列化引用）。
4. 标明任务是否完成。
5. 在失败时携带标准化错误信息。
6. 提供审计溯源引用（request_id, trace_id）。

使 Agent Facade、AccessGateway 和上层消费者能"统一消费 Agent 的任何执行路径结果"（架构 §3.8.4）。

### 5.3 AgentResultStatus 枚举

| 值 | 语义 | 来源 |
|---|---|---|
| Unspecified | 未指定（被守卫拒绝） | 全链路统一模式 |
| Completed | 任务正常完成 | 架构 §5.1 task_completed=true |
| Failed | 任务执行失败 | 架构 §5.1 ErrorInfo 非空 |
| PartiallyCompleted | 部分完成（降级输出） | 架构 §6.10 降级场景 |
| Cancelled | 用户或系统取消 | 架构 §3.4.2 等待态取消 |
| Timeout | 超时终止 | RuntimeBudget max_latency_ms 超限 |

### 5.4 必填字段（6 项，映射架构 §5.1 + §3.8.1）

| # | 字段名 | 类型 | 架构映射 | 语义 |
|---|---|---|---|---|
| R1 | result_id | string | 审计引用（§3.8.1） | 唯一结果标识，支持审计链路 |
| R2 | status | AgentResultStatus | task_completed + code 的语义合并 | 最终完成状态 |
| R3 | result_code | int32 | ResultCode code（§5.1） | WP-02 冻结结果码数值 |
| R4 | response_text | string | response_text（§5.1） | 人类可读文本回复 |
| R5 | task_completed | bool | task_completed（§5.1） | 任务是否完成的二值标记 |
| R6 | created_at | int64 | 通用时间戳 | 结果生成时间戳毫秒 |

> **设计说明**：
> - result_code 使用 int32 而非 ResultCode 枚举，因为 WP-02 设计允许扩展码值，int 更灵活。分类判定通过 classify_result_code_segment() 完成。
> - status 与 task_completed 有语义重叠但不等价：task_completed 是架构 §5.1 原始字段（简单二值），status 提供更细粒度的终态语义（6 值枚举）。保留两者以保持架构兼容性。
> - response_text 允许空字符串（某些结构化输出场景无文本回复）。

### 5.5 可选字段（7 项）

| # | 字段名 | 类型 | 语义 |
|---|---|---|---|
| O1 | request_id | string | 关联 AgentRequest，全链路溯源（§3.8.1 审计引用） |
| O2 | trace_id | string | 日志/事件/审计对齐（§3.8.1 审计引用） |
| O3 | structured_payload | string | 结构化产物序列化引用（§5.1 JsonObject 映射） |
| O4 | error_info | ErrorInfo | 失败时的标准化错误信息（§5.1 ErrorInfo） |
| O5 | checkpoint_ref | string | 最终 Checkpoint 标识引用（完成证明） |
| O6 | goal_id | string | 关联 GoalContract，多目标场景溯源 |
| O7 | tags | vector\<string\> | 审计/检索标签 |

> **设计说明**：
> - error_info 为可选：成功场景无需 ErrorInfo。
> - structured_payload 使用 string 而非结构体，因为 JsonObject 不是 contracts 层冻结类型，且内容格式由业务层定义。
> - checkpoint_ref 引用而非嵌入，与 Checkpoint 契约解耦。

### 5.6 禁止字段（来源 + 排除域）

| 禁止域 | 字段示例 | 归属对象 |
|---|---|---|
| 认知状态 | confirmed_facts, hypotheses, assumptions, evidence_refs, confidence | BeliefState (T009) |
| 执行记录 | payload, success, side_effects, tool_call_id, duration_ms | Observation (T006) |
| 推理摘要 | summary, key_facts, citations, omitted_details | ObservationDigest (T008) |
| 恢复状态 | working_memory_snapshot, pending_action, step_id, retry_count | Checkpoint (T012) |
| 消息渲染 | final_messages, rendered_prompt, prompt_bundle, system_instructions | PromptComposeResult (ADR-006) |
| 模型厂商 | model_provider_args, vendor_tool_schema | LLMAdapter (ADR-006) |
| 语义上下文 | user_turn, recent_history, retrieval_evidence, summary_memory | ContextPacket (T010) |
| 计划/决策 | plan_graph, step_list, action_decision, next_step | Planner/Reasoner |
| 多 Agent | worker_task_list, lease_id, delegation_policy, subtask_results | Multi-Agent (ADR-008) |
| FSM 内部 | fsm_state, state_transition_log, runtime_budget_consumed | Runtime 内部 |
| 入口请求 | attachments, request_channel, user_input | AgentRequest (T002) |

### 5.7 上下游关系

**生产者**：
- ResponseBuilder — 汇总 ContextPacket + Plan + Observation 生成 AgentResult（架构 §4.1, §11.1）
- AgentOrchestrator — 拥有最终 AgentResult 提交权（ADR-008 §7.2 #2）

**消费者**：
- Agent Facade — 统一出口接口（架构 §4.1）
- AccessGateway — 发布结果到用户端（Blueprint §5.7.3 AccessPublishRequest）
- Session Manager — 记录会话最终状态
- 审计系统 — 通过 request_id + trace_id 追溯

### 5.8 守卫设计（两层）

#### Layer 1：必填字段存在性校验（validate_agent_result_required_fields）

| 规则 | 校验内容 |
|---|---|
| R1-check | result_id.has_value() && !result_id->empty() |
| R2-check | status.has_value() && status != Unspecified |
| R3-check | result_code.has_value() |
| R4-check | response_text.has_value()（允许空字符串，结构化输出场景） |
| R5-check | task_completed.has_value() |
| R6-check | created_at.has_value() && created_at > 0 |

#### Layer 2：边界约束校验（validate_agent_result_boundary）

| 规则 | 校验内容 |
|---|---|
| 继承 L1 | 全部 R1-R6 校验 |
| R2-boundary | status 在已知 AgentResultStatus 枚举范围内 |
| R3-boundary | result_code 在 WP-02 冻结码值范围内（classify_result_code_segment != Unknown） |
| O1-boundary | request_id 若 present 必须 non-empty |
| O2-boundary | trace_id 若 present 必须 non-empty |
| O3-boundary | structured_payload 若 present 必须 non-empty |
| O5-boundary | checkpoint_ref 若 present 必须 non-empty |
| O6-boundary | goal_id 若 present 必须 non-empty |
| O7-boundary | tags 若 present 必须非空向量且元素 non-empty |

> **注**：result_code 的范围校验复用 WP-02 的 `classify_result_code_segment()`，Unknown 类别被拒绝。

---

## §6 Design→Build 映射

| 项 | 内容 |
|---|---|
| 代码目标 | contracts/include/agent/AgentResult.h; contracts/include/agent/AgentResultGuards.h |
| 测试目标 | tests/contract/agent/AgentResultContractTest.cpp |
| 验收命令 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R AgentResultContractTest --output-on-failure |

---

## §7 验收清单

| # | 验收项 | 判定 |
|---|---|---|
| 1 | 架构 §5.1 伪代码 5 字段全部映射 | ✅ |
| 2 | 架构 §3.8.1 审计引用字段覆盖 | ✅ |
| 3 | 只有 AgentOrchestrator 可提交（ADR-008 禁止 Worker 直接提交） | ✅ 注释明确 |
| 4 | ResultCode / ErrorInfo 复用 WP-02 | ✅ |
| 5 | 与 BeliefState/Observation/Checkpoint/ContextPacket 无字段重叠 | ✅ |
| 6 | 两层守卫设计与全链路一致 | ✅ |
| 7 | Design→Build 映射含三件套 | ✅ |

---

## §8 D Gate 结果

| 项 | 结果 |
|---|---|
| 进入 -B 条件 | ✅ 满足 |
| 阻塞项 | 无 |
| 理由 | 6R+7O 对齐架构+ADR，枚举覆盖终态场景，禁止字段与上下游边界清晰，ResultCode/ErrorInfo 直接复用 |

---

## §9 风险与回退

| 风险 | 影响 | 回退 |
|---|---|---|
| status 与 task_completed 语义重叠 | 消费者困惑 | 已在 §5.4 说明两者关系：task_completed 保持架构兼容，status 提供细粒度 |
| result_code 范围扩展 | 新 provider 码值被拒 | WP-02 设计预留 1000-5999 范围，新类别追加对应段 |
| structured_payload 内容校验缺失 | 无法验证 JSON 格式 | 内容格式由业务层定义，contracts 层仅保证"非空 when present" |

---

## §10 下一任务建议

1. **WP03-T014-B**：实现 AgentResult.h + AgentResultGuards.h + AgentResultContractTest.cpp。
2. **WP03-T015-D/B**：单 Agent 主链路对象流图 + 端到端契约冒烟测试。
