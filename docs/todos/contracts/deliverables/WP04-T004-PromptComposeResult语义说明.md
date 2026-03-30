# WP04-T004-D：PromptComposeResult 职责边界语义说明

> 版本：1.0 | 日期：2026-03-17 | 状态：Done
> 任务编号：WP04-T004-D
> 上游输入：ADR-006 §4/§6.3/§7、WP04-T001-D §4（PromptComposeResult 禁区）、WP04-T002-D/T003-D（Prompt 链路上游对象已冻结）、WP02-T009/T010/T012（横切基础规范）

---

## 1. 对象定位与调用链

### 1.1 核心职责

PromptComposeResult 是 **PromptComposer 的装配结果对象**：
用于表达一次 Prompt 装配完成后，能够下传给 PromptPolicy 和 LLMRequest 适配层的
“消息产物 + 装配元数据”。

PromptComposeResult **不是**：
- ContextPacket 的回写容器（不携带 context_update、history_update）。
- memory 的补丁对象（不携带 memory_write_back、belief_patch）。
- 检索或知识召回控制面（不携带 knowledge_recall）。
- 第二个执行结果对象（不承担 Observation/AgentResult 的执行状态语义）。

### 1.2 调用链位置（ADR-006 §4 固化）

```
Runtime
  ├── ContextOrchestrator → ContextPacket
  ├── PromptRegistry      → PromptRelease
  ├── Runtime 组装        → PromptComposeRequest
  └── PromptComposer(PromptComposeRequest)
        └── PromptComposeResult
              ├── messages
              ├── selected_prompt_id / selected_version
              ├── pruned_sections
              ├── estimated_tokens
              └── composition_warnings
  └── PromptPolicy(PromptComposeResult) → governed prompt payload / LLMRequest
```

**关键约束**：
1. PromptComposeResult 只表达装配产物和装配元数据，不回写 ContextPacket。
2. PromptComposeResult 是 PromptComposer 的唯一结果对象，不承担 memory 更新职责。
3. Provider-specific payload 允许在下游适配层派生，但不应把 memory/context 写回语义混入本对象。

---

## 2. 字段清单（必填 + 可选分组）

本任务只冻结职责边界与高层字段全集，为 WP04-T005 字段表提供输入；
不在 T004 中扩张到字段级组合规则。

### 2.1 必填字段（4 项）

| 字段 | 类型 | 语义 | 校验规则 | 来源约束 |
|---|---|---|---|---|
| `messages` | `vector<string>` | provider-neutral messages / prompt payload 片段 | present，向量非空，元素非空 | ADR-006 §6.3 |
| `selected_prompt_id` | `string` | 本次装配选中的 PromptSpec 标识 | 非空 | ADR-006 §6.3 |
| `selected_version` | `string` | 本次装配选中的 PromptRelease 版本 | 非空 | ADR-006 §6.3 |
| `estimated_tokens` | `int64` | PromptComposer 对本次装配结果的 token 预估 | present，非负 | ADR-006 §6.3 |

**必填字段设计理由：**
1. `messages`：没有装配后的消息载荷，PromptComposeResult 就失去存在意义。
2. `selected_prompt_id`：Prompt 资产治理、灰度、审计必须可追踪到具体 PromptSpec。
3. `selected_version`：版本与回滚能力是 Prompt 治理的核心审计点。
4. `estimated_tokens`：PromptPolicy 和 LLMRequest 衔接时需要预算元数据做前置治理。

### 2.2 可选字段（2 项）

| 字段 | 类型 | 语义 | 携带规则 | 来源约束 |
|---|---|---|---|---|
| `pruned_sections` | `vector<string>` | 本次装配过程中被裁剪的 section 标识 | 携带时向量非空且元素非空 | ADR-006 §6.3 |
| `composition_warnings` | `vector<string>` | 装配过程中的警告信号，如预算逼近、回退模板等 | 携带时向量非空且元素非空 | ADR-006 §6.3 |

**设计说明：**
1. `pruned_sections` 只有发生裁剪时才有意义，因此为可选。
2. `composition_warnings` 只在存在异常或降级信号时携带，避免把正常路径污染为“永远带告警”。
3. Provider-specific payload、rendered_prompt 作为更下游的模型适配表达，不在本任务强制入模；T004 先冻结职责边界，不扩张到 T005 字段表之外。

---

## 3. 禁区字段清单（引用 WP04-T001-D §4）

PromptComposeResult 不允许包含任何 memory/context 回写语义字段：

| 禁止字段/类型 | 禁止原因 | 来源 |
|---|---|---|
| `memory_write_back` | PromptComposer 不得直接写回 memory | ADR-006 §3.3 条款 5；T001-D §4 |
| `context_update` | ContextPacket 更新归 ContextOrchestrator | ADR-006 §3.2；T001-D §4 |
| `belief_patch` | BeliefState 修订归 Cognition/memory | ADR-006 §3.3 条款 3；T001-D §4 |
| `knowledge_recall` | 知识召回归 ContextOrchestrator | ADR-006 §3.3 条款 1；T001-D §4 |
| `history_update` | 历史记录写回归 memory 子系统 | ADR-006 §3.3 条款 3；T001-D §4 |

**边界守卫映射**：上述禁区字段已经在 `PromptBoundaryContracts.h` 的
`kComposeResultMemoryWritebackForbiddenFields` 中登记，
由 `evaluate_compose_result_field_boundary()` 提供统一拒绝结果。

---

## 4. PromptComposeResult 与相邻对象的边界

| 比较项 | PromptComposeRequest | PromptComposeResult | ContextPacket |
|---|---|---|---|
| 归属子系统 | llm（Runtime 组装请求） | llm（PromptComposer 产出） | memory |
| 语义定位 | 装配请求 | 装配结果与元数据 | 语义上下文载体 |
| 是否持有上下文原文 | ❌ 否 | ❌ 否 | ✅ 是 |
| 是否持有最终 messages | ❌ 否 | ✅ 是 | ❌ 否 |
| 是否可回写 memory | ❌ 否 | ❌ 否 | 由 ContextOrchestrator 管理 |
| 下游消费者 | PromptComposer | PromptPolicy / LLM 适配层 | Cognition + PromptComposer |

**边界结论**：
1. PromptComposeRequest 解决“如何请求装配”；PromptComposeResult 解决“装配出了什么”。
2. ContextPacket 仍是唯一语义上下文载体；PromptComposeResult 不能回头修改它。
3. PromptPolicy 只能治理 PromptComposeResult 的产物，不应借此回流语义上下文写入。

---

## 5. Design -> Build 映射

| Design 结论 | Build 落点 | 验证方式 |
|---|---|---|
| 4 必填 + 2 可选字段冻结 | `contracts/include/prompt/PromptComposeResult.h` | 编译通过 |
| Result 只表达产物与元数据 | `PromptComposeResult.h` 注释与字段定义 | `PromptComposeResultContractTest` |
| memory/context 写回禁区 | 复用 `PromptBoundaryContracts.h` 的 ComposeResult 守卫 | 负例：memory_write_back / belief_patch |
| 最小边界守卫 | `contracts/include/prompt/PromptComposeResultGuards.h` | required + boundary contract tests |

---

## 6. D Gate 结论

| D 原子项 | 状态 | 说明 |
|---|---|---|
| D1：对象定位与调用链 | ✅ Done | Producer/consumer/下游治理路径已明确 |
| D2：字段全集冻结 | ✅ Done | 4 必填 + 2 可选固定，供 T004-B/T005-D 复用 |
| D3：禁区字段清单 | ✅ Done | 5 类 memory/context 写回字段明确排除 |
| D4：相邻对象边界 | ✅ Done | Request / Result / ContextPacket 三者分工可追溯 |
| D5：D Gate 结论 | ✅ Done | PASS |

**Gate 结论：PASS — 可进入 WP04-T004-B**

进入 B 的条件：
1. ✅ `PromptComposeResult.h` 只需落地 6 个冻结字段，无需跨任务扩张。
2. ✅ `PromptComposeResultGuards.h` 只需提供 required/boundary 两层守卫。
3. ✅ `PromptComposeResultContractTest.cpp` 可专注验证必填语义与 memory write-back 禁区，不提前实现 T005 字段表规则。

---

## 7. 可追溯证据

### 本地文档证据

| 条款 | 文档路径 | 关键节 |
|---|---|---|
| PromptComposeResult 定义 | docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md | §6.3 |
| Prompt 调用顺序 | docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md | §4 |
| PromptComposer 职责禁区 | docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md | §3.3、§7 |
| Prompt 三段治理模型 | docs/architecture/DASSALL_Agent_architecture.md | §5.4.7 |
| contracts/prompt 目录归属 | docs/architecture/DASALL_Engineering_Blueprint.md | §3.1 |
| PromptComposeResult 禁区 | docs/todos/contracts/deliverables/WP04-T001-ADR006对象影响清单.md | §4 |

### 外部业界参考

1. Protobuf Best Practices：建议保持消息类型小而稳定、避免修改字段类型，并将不同职责对象拆成独立消息类型，以降低演进破坏面。来源：https://protobuf.dev/best-practices/dos-donts/
2. Pact Contract Testing：契约测试应优先验证 consumer 真正依赖的请求/响应内容与格式，尽早发现接口偏移，而不是等到慢速端到端测试。来源：https://docs.pact.io/getting_started/what_is_pact_good_for
