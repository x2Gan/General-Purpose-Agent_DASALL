# WP04-T003-D：PromptComposeRequest 字段表

> 版本：1.0 | 日期：2026-03-17 | 状态：Done
> 任务编号：WP04-T003-D
> 上游输入：WP04-T002-D（PromptComposeRequest 职责边界）、ADR-006 §6.2/§7、WP04-T001-D（禁区字段）、WP02-T009/T010/T012、WP03 字段级 guard/test 模式

---

## 1. 任务范围

本任务只处理 WP04-T003：
把 WP04-T002 已冻结的 PromptComposeRequest 对象语义，进一步固化为**字段级清单 + 字段校验规则 + 非法组合规则**，并为 WP04-T003-B 提供直接的守卫实现依据。

本任务不处理：
1. 不新增 PromptComposeRequest 字段。
2. 不修改 WP04-T002 已冻结的对象职责边界。
3. 不扩张到 PromptComposeResult（归 T004/T005）。
4. 不引入 provider-specific payload、rendered prompt、messages 等结果层字段。

---

## 2. 字段全集与分组

PromptComposeRequest 固定由 **4 个必填字段 + 7 个可选字段** 组成。
字段总数锁定为 **11**，后续只能通过兼容优先方式新增可选字段，不能改变既有字段语义或类型。

### 2.1 必填字段（4 项）

| 字段 | 类型 | 语义 | 字段级规则 | 来源 |
|---|---|---|---|---|
| `request_id` | `std::optional<std::string>` | 请求级追溯 ID，来自 AgentRequest.request_id | present 且 non-empty | WP02-T009、T002-D §2.2 |
| `stage` | `std::optional<CompositionStage>` | 当前 Prompt 装配阶段 | present 且非 `Unspecified`；枚举值必须在已知范围内 | ADR-006 §6.2、WP02-T012 |
| `context_packet_id` | `std::optional<std::string>` | 本轮 ContextPacket 的引用 ID | present 且 non-empty；字段组合上必须与 `request_id` 一致 | ADR-006 §6.2、T002-D §1.2 |
| `created_at` | `std::optional<std::int64_t>` | 请求创建时间（毫秒） | present 且为正值 | WP02-T010 |

### 2.2 可选字段（7 项）

| 字段 | 类型 | 语义 | 字段级规则 | 来源 |
|---|---|---|---|---|
| `task_type` | `std::optional<std::string>` | 任务类型提示，辅助 PromptRegistry 选模版 | present 时 non-empty | ADR-006 §6.2 |
| `prompt_release_id` | `std::optional<std::string>` | 预选 PromptRelease 标识 | present 时 non-empty | ADR-006 §6.2 |
| `visible_tools` | `std::optional<std::vector<std::string>>` | 本轮可见工具 ID 集合 | present 时：向量非空、元素非空、元素唯一 | ADR-006 §6.2、T002-D §2.3 |
| `model_route` | `std::optional<std::string>` | 模型路由标识 | present 时 non-empty | ADR-006 §6.2 |
| `output_schema_ref` | `std::optional<std::string>` | 输出结构约束引用 | present 时 non-empty | ADR-006 §6.2 |
| `response_format` | `std::optional<std::string>` | Provider 响应格式提示 | present 时 non-empty | ADR-006 §6.2 |
| `tags` | `std::optional<std::vector<std::string>>` | 审计与追踪标签 | present 时：向量非空、元素非空 | WP02-T009 |

---

## 3. 字段级规则总表

### 3.1 通用规则

| 规则编号 | 规则 | 适用字段 |
|---|---|---|
| R1 | required string 字段必须 present 且 non-empty | `request_id`, `context_packet_id` |
| R2 | required enum 字段必须 present 且非 `Unspecified` | `stage` |
| R3 | required timestamp 字段必须为正值 | `created_at` |
| R4 | optional string 字段遵循 "carry meaningful content or omit" | `task_type`, `prompt_release_id`, `model_route`, `output_schema_ref`, `response_format` |
| R5 | optional vector 字段若 present，则向量本身必须非空 | `visible_tools`, `tags` |
| R6 | optional vector 字段若 present，则每个元素必须 non-empty | `visible_tools`, `tags` |
| R7 | 具有集合语义的字段若 present，则元素必须唯一 | `visible_tools` |

### 3.2 字段组合规则

| 规则编号 | 非法组合 | 判定原因 | 来源 |
|---|---|---|---|
| C1 | `request_id != context_packet_id` | PromptComposeRequest 必须绑定到当前请求对应的唯一 ContextPacket；若不一致，则装配请求脱离当前上下文链路 | T002-D §1.2、WP02-T009 |
| C2 | `visible_tools` present 且含重复元素 | `visible_tools` 定义为“工具标识集合”，重复元素会导致 PromptComposer 重复注入同一工具定义 | T002-D §2.3、工程蓝图 `contracts/prompt` 目录职责 |

### 3.3 继承规则

WP04-T003-B 的字段级校验器必须**继承** WP04-T002-B 的 L1/L2 规则，不允许另起一套字段判断口径。

具体要求：
1. 字段级 guard 先调用 `validate_prompt_compose_request_boundary()`。
2. 若 L1/L2 失败，字段级 guard 直接返回上游失败结果。
3. 仅在 L1/L2 通过后，再执行 R4-R7 与 C1-C2。

---

## 4. 非法字段与越界字段排除表

下列字段**不属于 PromptComposeRequest 字段表**，不得出现在字段级设计或守卫实现中：

| 越界字段 | 排除原因 | 来源 |
|---|---|---|
| `provider_payload` | 属于 PromptComposeResult / provider 适配层 | ADR-006 §6.1、T001-D §2 |
| `rendered_prompt` | 属于 PromptComposeResult 渲染产物 | ADR-006 §6.1、T001-D §2 |
| `messages` | 属于 PromptComposeResult 核心产出 | ADR-006 §6.3 |
| `estimated_tokens` | 属于 PromptComposeResult 元数据 | ADR-006 §6.3 |
| `memory_snapshot` | 属于 ContextOrchestrator 语义上下文主控 | ADR-006 §7、T001-D §3 |
| `retrieval_candidates` | 属于 ContextOrchestrator 检索职责 | ADR-006 §7、T001-D §3 |
| `knowledge_fragments` | 属于未经处理的知识输入，不应进入装配请求 | ADR-006 §3.3、T001-D §3 |

**结论**：WP04-T003-D 字段表满足 TODO 的完成判定：
**不含 `provider_payload` 或任何渲染结果字段。**

---

## 5. Design -> Build 映射

| Design 结论 | Build 落点 | 验证方式 |
|---|---|---|
| 4 必填 + 7 可选字段不变 | `PromptComposeRequest.h` 无结构改动 | 编译通过 |
| R4-R7 字段规则 | `validate_prompt_compose_request_field_rules()` | `PromptComposeRequestFieldContractTest` |
| C1：`request_id == context_packet_id` | `PromptComposeRequestGuards.h` 新增组合校验 | 负例：request/context mismatch |
| C2：`visible_tools` 元素唯一 | `PromptComposeRequestGuards.h` 新增唯一性校验 | 负例：duplicate visible tool |
| 禁止结果层字段混入 | 继续由 `PromptBoundaryContracts.h` 承担 | 复用 T001-B smoke test |

---

## 6. D Gate 结论

| D 原子项 | 状态 | 说明 |
|---|---|---|
| D1：字段全集锁定 | ✅ Done | 11 个字段（4 必填 + 7 可选）全部列出 |
| D2：字段级规则 | ✅ Done | R1-R7 全部明文化 |
| D3：非法组合规则 | ✅ Done | C1/C2 两类非法组合可程序化验证 |
| D4：越界字段排除 | ✅ Done | `provider_payload` / `rendered_prompt` / `messages` 等均被排除 |
| D5：D Gate | ✅ Done | PASS |

**Gate 结论：PASS — 可进入 WP04-T003-B**

进入 B 的条件：
1. ✅ `PromptComposeRequestGuards.h` 仅需做最小增量：补 C1/C2 校验即可
2. ✅ `PromptComposeRequestFieldContractTest.cpp` 可直接按 R4-R7/C1-C2 生成正负例
3. ✅ 无需修改 `PromptComposeRequest.h` 结构定义，避免跨任务扩张

---

## 7. 可追溯证据

### 本地文档证据

| 条款 | 文档 | 关键节 |
|---|---|---|
| PromptComposeRequest 输入面 | `docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md` | §6.2 |
| PromptComposeRequest 禁止成为第二上下文主控 | `docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md` | §7 |
| Prompt 边界禁区 | `docs/todos/contracts/deliverables/WP04-T001-ADR006对象影响清单.md` | §3 |
| PromptComposeRequest 4 必填 + 7 可选 | `docs/todos/contracts/deliverables/WP04-T002-PromptComposeRequest语义说明.md` | §2 |
| `context_packet_id` 引用模式 | `docs/todos/contracts/deliverables/WP04-T002-PromptComposeRequest语义说明.md` | §1.2 |
| contracts/prompt 目录职责 | `docs/architecture/DASALL_Engineering_Blueprint.md` | §1, contracts 目录结构 |
| 契约层 Gate 要求 | `docs/plans/DASALL_工程落地实现步骤指引.md` | 阶段 B |

### 外部参考

1. Protobuf Best Practices：枚举必须带 `UNSPECIFIED=0`，避免破坏性字段修改，契约演进优先兼容扩展。来源：`https://protobuf.dev/best-practices/dos-donts/`
2. Pact Contract Testing：契约测试应围绕 consumer 真实依赖的字段内容与格式建立，优先发现接口偏移，而不是依赖慢速 e2e。来源：`https://docs.pact.io/getting_started/what_is_pact_good_for`
