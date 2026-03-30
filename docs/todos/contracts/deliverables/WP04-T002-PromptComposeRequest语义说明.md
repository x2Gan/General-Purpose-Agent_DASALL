# WP04-T002-D：PromptComposeRequest 职责边界语义说明

> 版本：1.0 | 日期：2026-03-17 | 状态：Done
> 任务编号：WP04-T002-D
> 上游输入：ADR-006 §4/§6.2/§7、WP04-T001-D §3（禁区约束）、WP03-T010（ContextPacket 已冻结）、WP02-T009/T010/T012（横切基础规范）

---

## 1. 对象定位与调用链

### 1.1 核心职责

PromptComposeRequest 是 **PromptComposer 的装配请求对象**：
在 Runtime 调用链中，它是 ContextPacket 的下游消费者，负责把
"语义上下文引用 + PromptRelease + 模型路由 + 工具可见性 + 输出约束"
打包为一次合法的 Prompt 装配请求，供 PromptComposer 执行。

PromptComposeRequest **不是**：
- ContextPacket 的替代品（不持有上下文组装数据）。
- 第二个上下文主控对象（不含 memory_snapshot/retrieval_candidates）。
- 模型参数载体（不含 provider_payload/tool_schemas）。
- 执行结果对象（不含 messages/estimated_tokens）。

### 1.2 调用链位置（ADR-006 §4 固化）

```
Runtime
  ├── ContextOrchestrator → ContextPacket (request_id 关联)
  ├── PromptRegistry      → PromptRelease (prompt_release_id 关联)
  └── Runtime 组装 PromptComposeRequest
        ├── context_packet_id  ← ContextPacket.request_id
        ├── prompt_release_id  ← PromptRelease 标识
        ├── stage              ← 当前编排阶段
        ├── visible_tools      ← 权限系统确认的工具集
        └── model_route / output_schema_ref ← 路由与输出约束
  └── PromptComposer(PromptComposeRequest) → PromptComposeResult
```

**关键约束**：
1. PromptComposeRequest 通过 `context_packet_id` 引用 ContextPacket，不嵌入 ContextPacket 全结构。
2. `context_packet_id` 与 `request_id` 均根植于 WP02-T009 标识规范确立的追溯链。
3. PromptComposer 通过 `context_packet_id` 反查 ContextPacket 内容，不接受直接注入的上下文片段。

---

## 2. 字段清单（必填 + 可选分组）

### 2.1 枚举定义

#### CompositionStage（必填字段 `stage` 的类型）

PromptComposer 根据 `stage` 选择适合当前编排阶段的 PromptSpec 模板，
是 PromptRegistry 选择的关键控制参数。

| 枚举值 | 含义 | 说明 |
|---|---|---|
| `Unspecified` | 未指定（WP02-T012 哨兵） | 守卫必须拒绝此值 |
| `Planning` | 规划阶段 | Planner 调用 PromptComposer 为制定计划组装 Prompt |
| `Execution` | 执行阶段 | Reasoner/工具调用后的 Prompt 序列组装 |
| `Reflection` | 反思阶段 | ReflectionEngine 调用 PromptComposer 组装错误归因 Prompt |
| `Response` | 输出生成阶段 | ResponseBuilder 调用 PromptComposer 组装最终回复 Prompt |

### 2.2 必填字段（4 项）

| 字段 | 类型 | 语义 | 校验规则 | 来源约束 |
|---|---|---|---|---|
| `request_id` | `string` | 请求级追溯 ID，从 AgentRequest 透传 | 非空 | WP02-T009 |
| `stage` | `CompositionStage` | 当前编排阶段，决定 PromptSpec 选择策略 | 非 Unspecified | ADR-006 §6.2 |
| `context_packet_id` | `string` | 对本轮 ContextPacket 的引用（等值于 ContextPacket.request_id） | 非空 | ADR-006 §6.2，WP02-T009 |
| `created_at` | `int64` | 请求创建时间戳（毫秒） | 正值 | WP02-T010 |

**必填字段设计理由：**
- `request_id`：全链路追溯要求每个契约对象都携带请求级 ID（WP02-T009）。
- `stage`：PromptRegistry 必须知道当前阶段才能选择正确的 PromptSpec；缺失则无法组装。
- `context_packet_id`：PromptComposer 必须绑定本轮语义上下文，否则产出无法与当前请求关联。
- `created_at`：审计与延迟检测的必要时间基线（WP02-T010）。

### 2.3 可选字段（7 项）

| 字段 | 类型 | 语义 | 携带规则 | 来源约束 |
|---|---|---|---|---|
| `task_type` | `string` | 任务类型提示（如 "summarize"/"codegen"），辅助 PromptRegistry 精细选择 | 携带时非空 | ADR-006 §6.2 |
| `prompt_release_id` | `string` | PromptRegistry 预选的 PromptRelease 标识；absent 时由 PromptComposer 自行选择 | 携带时非空 | ADR-006 §6.2 |
| `visible_tools` | `vector<string>` | 本轮可见工具标识集合，由权限系统提供；absent 时 PromptComposer 不注入工具定义 | 携带时向量非空且元素非空 | ADR-006 §6.2 |
| `model_route` | `string` | 目标模型路由标识；absent 时由 PromptComposer 使用默认路由 | 携带时非空 | ADR-006 §6.2 |
| `output_schema_ref` | `string` | 输出结构约束引用（如 JSON Schema ref）；absent 时自由格式 | 携带时非空 | ADR-006 §6.2 |
| `response_format` | `string` | Provider 响应格式提示（如 "json_object"/"text"）；absent 时由 PromptPolicy 决定 | 携带时非空 | ADR-006 §6.2 |
| `tags` | `vector<string>` | 审计与追踪标签；不携带执行控制信号 | 携带时向量非空且元素非空 | WP02-T009 |

**"携带内容或省略"原则（WP03-T003 §4.3）**：
所有可选 string 字段，若 `has_value()` 为 true，则内容必须非空。
所有可选 vector 字段，若 `has_value()` 为 true，则向量非空且不含空字符串元素。

---

## 3. 禁区字段清单（引用 WP04-T001-D §3）

PromptComposeRequest 不允许包含以下类型的字段，否则它将演化为第二个上下文主控对象：

| 禁止字段/类型 | 禁止原因 | 来源 |
|---|---|---|
| `memory_snapshot` | 直接携带 WorkingMemory 内容，侵入 ContextOrchestrator 职责 | ADR-006 §3.2/§7 方案B不采纳；T001-D §3.3 |
| `retrieval_candidates` | 直接携带候选信息片段，侵入 ContextOrchestrator 检索职责 | ADR-006 §3.2/§3.3；T001-D §3.3 |
| `context_packet_internal` | 将 ContextPacket 内部数据透传进来，破坏对象边界 | ADR-006 §6.1；T001-D §3.3 |
| `knowledge_fragments` | 携带未经 ContextOrchestrator 处理的原始知识片段 | ADR-006 §3.3 条款1；T001-D §3.3 |

**边界守卫验证**：上述禁区字段已在 `PromptBoundaryContracts.h`（T001-B）的
`kComposeRequestContextOwnershipForbiddenFields` 中登记，
`evaluate_compose_request_field_boundary()` 函数可对名称进行守卫检查。

---

## 4. PromptComposeRequest 与相邻对象的边界

| 比较项 | ContextPacket | PromptComposeRequest | PromptComposeResult |
|---|---|---|---|
| 归属子系统 | memory | llm（Runtime 组装） | llm |
| 产出者 | ContextOrchestrator | Runtime | PromptComposer |
| 消费者 | Cognition 层 + PromptComposer | PromptComposer | PromptPolicy → LLMManager |
| 语义定位 | 语义上下文载体 | 装配请求 | 装配结果与元数据 |
| 含上下文数据 | ✅ 是（10 类槽位） | ❌ 否（仅持有引用 ID） | ❌ 否（仅产出消息） |
| 可回写 Memory | ❌ 禁止 | ❌ 禁止 | ❌ 禁止 |

---

## 5. D Gate 结论

| D 原子项 | 状态 | 说明 |
|---|---|---|
| D1：对象定位与消费链 | ✅ Done | 生产者/消费者/调用顺序已明确；context_packet_id 引用模式已选定 |
| D2：必填字段集 | ✅ Done | 4 必填（request_id/stage/context_packet_id/created_at），规则可追溯 |
| D3：可选字段集 | ✅ Done | 7 可选，覆盖 ADR-006 §6.2 全部 7 项输入，"携带或省略"规则明确 |
| D4：禁区字段清单 | ✅ Done | 4 类禁区，引用 T001-D §3，守卫已在 T001-B 实现 |
| D5：D Gate 结论 | ✅ Done | PASS |

**Gate 结论：PASS — 可进入 WP04-T002-B**

进入 B 的条件：
1. ✅ CompositionStage enum（5 值，含 Unspecified 哨兵）已设计完毕
2. ✅ 4 必填 + 7 可选字段类型与名称已确定，可直接编写头文件
3. ✅ 三层守卫（L1/L2/L3）规则已完整映射到字段级约束
4. ✅ 禁区守卫已通过 T001-B 验证，T002-B contract test 专注于字段合法性

---

## 6. 可追溯证据

### 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| PromptComposeRequest 字段定义 | docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md | §6.2 |
| PromptComposer 主职责 | docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md | §3.3 |
| 方案 B 不采纳结论 | docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md | §7 |
| 调用顺序 | docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md | §4 |
| ContextPacket 引用模式 | docs/todos/contracts/deliverables/WP03-T010-ContextPacket语义说明.md | §2.1 |
| 禁区字段来源 | docs/todos/contracts/deliverables/WP04-T001-ADR006对象影响清单.md | §3 |
| 标识规范 | docs/todos/contracts/deliverables/WP02-T009-标识元数据规范.md | — |
| 时间规范 | docs/todos/contracts/deliverables/WP02-T010-TimeDeadline规范.md | — |
| 枚举规范 | docs/todos/contracts/deliverables/WP02-T012-枚举规范.md | — |

### 外部业界参考

1. **OpenAI Agents SDK ContextInput**（2025-03）：装配请求引用 context 句柄，stage/model 为必填控制字段，不嵌入 ContextStore 数据。
2. **Google ADK CompositionRequest**（2025）：request 与 session/context 解耦，request 只持有 session_id/context_handle。
3. **LangGraph StateGraph input spec**（2025）：每个 node 的输入 spec 不携带 state 的原始数据，只持有 state_key 引用；写回由 reducer 控制。
