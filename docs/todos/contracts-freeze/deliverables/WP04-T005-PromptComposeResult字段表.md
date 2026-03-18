# WP04-T005-D：PromptComposeResult 字段表

> 版本：1.0 | 日期：2026-03-17 | 状态：Done
> 任务编号：WP04-T005-D
> 上游输入：WP04-T004-D（PromptComposeResult 职责边界）、ADR-006 §3.4/§6.3、WP04-T001-D（禁区字段）、WP04-T003-D（字段表模式）、WP02-T009/T010/T012

---

## 1. 任务范围

本任务只处理 WP04-T005：
把 WP04-T004 已冻结的 PromptComposeResult 对象语义，进一步固化为字段级清单、字段校验规则，以及与 PromptPolicy、LLMRequest 的最小衔接约束，为 WP04-T005-B 提供直接的守卫实现依据。

本任务不处理：
1. 不新增 PromptComposeResult 字段。
2. 不修改 WP04-T004 已冻结的对象职责边界。
3. 不扩张到 PromptPolicyDecision 或 LLMRequest 具体字段设计。
4. 不把 provider-specific payload、memory/context 写回语义混入 Result 对象。

---

## 2. 字段全集与分组

PromptComposeResult 固定由 4 个必填字段 + 2 个可选字段组成。
字段总数锁定为 6，后续若演进，只能优先采用兼容式新增可选字段，不能重定义既有字段语义或类型。

### 2.1 必填字段（4 项）

| 字段 | 类型 | 语义 | 字段级规则 | 下游衔接 |
|---|---|---|---|---|
| `messages` | `std::optional<std::vector<std::string>>` | PromptComposer 产出的 provider-neutral 消息载荷 | present；向量非空；元素非空 | PromptPolicy 直接治理的输入主体；后续可映射到 LLMRequest 消息体 |
| `selected_prompt_id` | `std::optional<std::string>` | 本次装配选中的 PromptSpec 标识 | present 且 non-empty | 供 PromptPolicy 审计与 LLM 链路日志追踪 |
| `selected_version` | `std::optional<std::string>` | 本次装配选中的 PromptRelease 版本 | present 且 non-empty | 支撑灰度、回滚与下游请求审计 |
| `estimated_tokens` | `std::optional<std::int64_t>` | 本次装配结果的 token 预估 | present 且 non-negative | PromptPolicy 发送前预算治理，以及 LLMRequest 路由预算判断的输入元数据 |

### 2.2 可选字段（2 项）

| 字段 | 类型 | 语义 | 字段级规则 | 下游衔接 |
|---|---|---|---|---|
| `pruned_sections` | `std::optional<std::vector<std::string>>` | 本次装配因预算或治理被裁剪掉的 section 标识 | present 时：向量非空、元素非空、元素唯一 | PromptPolicy / 审计日志可据此解释为何最终消息与上游输入不完全一致 |
| `composition_warnings` | `std::optional<std::vector<std::string>>` | 装配阶段发出的告警信号，如预算逼近、模板回退 | present 时：向量非空、元素非空 | PromptPolicy 与运行日志可据此决定是否追加治理、告警或降级记录 |

---

## 3. 字段级规则总表

### 3.1 通用规则

| 规则编号 | 规则 | 适用字段 |
|---|---|---|
| R1 | required vector 字段必须 present 且向量非空 | `messages` |
| R2 | required vector 字段的元素必须 non-empty | `messages` |
| R3 | required string 字段必须 present 且 non-empty | `selected_prompt_id`, `selected_version` |
| R4 | required numeric 字段必须 present 且 non-negative | `estimated_tokens` |
| R5 | optional vector 字段若 present，则向量本身必须非空 | `pruned_sections`, `composition_warnings` |
| R6 | optional vector 字段若 present，则每个元素必须 non-empty | `pruned_sections`, `composition_warnings` |
| R7 | 具有 section 集合语义的字段若 present，则元素必须唯一 | `pruned_sections` |

### 3.2 字段组合规则

| 规则编号 | 非法组合 | 判定原因 | 来源 |
|---|---|---|---|
| C1 | `pruned_sections` present 且含重复 section 标识 | `pruned_sections` 是可审计的裁剪清单，不是事件流；重复标识会放大同一裁剪事实，破坏 PromptPolicy / 日志侧解释一致性 | ADR-006 §6.3、§8 审计要求 |

### 3.3 继承规则

WP04-T005-B 的字段级校验器必须继承 WP04-T004-B 的 L1/L2 规则，不允许重写既有必填口径。

具体要求：
1. 字段级 guard 先调用 `validate_prompt_compose_result_boundary()`。
2. 若 L1/L2 失败，字段级 guard 直接返回上游失败结果。
3. 仅在 L1/L2 通过后，再执行 R5-R7 与 C1。

---

## 4. 与 PromptPolicy、LLMRequest 的衔接边界

| 衔接对象 | PromptComposeResult 提供什么 | 不提供什么 | 结论 |
|---|---|---|---|
| `PromptPolicy` | `messages`、`estimated_tokens`、`pruned_sections`、`composition_warnings`，供发送前裁剪、来源过滤、预算治理 | 不提供 memory 检索、语义重排、工具越权启用 | Result 只作为治理输入，不替代 PolicyDecision |
| `LLMRequest` | `messages` 与审计元数据来源（`selected_prompt_id` / `selected_version` / `estimated_tokens`） | 不直接承载 provider-specific payload 结构 | Result 是 provider-neutral 中间层，不把厂商格式污染回 Prompt 链路 |

结论：
1. `messages` 是唯一必须下传到 PromptPolicy/LLMRequest 适配层的主体载荷。
2. `estimated_tokens` 是唯一冻结在 Result 上的预算元数据，足以支撑发送前治理。
3. `pruned_sections` 与 `composition_warnings` 只承担解释性和可审计性，不扩张为新的治理决策对象。

---

## 5. 非法字段与越界字段排除表

下列字段不属于 PromptComposeResult 字段表，不得出现在字段级设计或守卫实现中：

| 越界字段 | 排除原因 | 来源 |
|---|---|---|
| `provider_payload` | 属于 LLMRequest / provider 适配层，而非 provider-neutral Result | ADR-006 §6.1、架构文档 llm 章节 |
| `rendered_prompt` | 属于渲染副产物命名，不应重新引入第二套载荷口径 | ADR-006 §6.1、WP04-T001-D |
| `memory_write_back` | 属于 memory/context 写回语义，超出 PromptComposer 结果边界 | ADR-006 §3.3、§6.3 |
| `context_update` | 属于 ContextOrchestrator 语义上下文主权 | ADR-006 §7 |
| `belief_patch` | 属于 cognition/memory 变更，不是 Prompt 装配产物 | ADR-006 §3.3 |
| `tool_visibility_patch` | 属于 PromptPolicyDecision，而不是 PromptComposeResult | 架构文档 Prompt 三段模型 |

结论：WP04-T005-D 满足 TODO 的完成判定：
与 PromptPolicy、LLMRequest 的衔接明确，且没有把 provider 私有字段或 policy 决策字段提前塞回 Result。

---

## 6. Design -> Build 映射

| Design 结论 | Build 落点 | 验证方式 |
|---|---|---|
| 4 必填 + 2 可选字段不变 | `contracts/include/prompt/PromptComposeResult.h` 无结构改动 | 编译通过 |
| R5/R6：可选向量必须有意义 | `validate_prompt_compose_result_field_rules()` | `PromptComposeResultFieldContractTest` |
| R7/C1：`pruned_sections` 元素唯一 | `PromptComposeResultGuards.h` 新增唯一性校验 | 负例：duplicate pruned section |
| 继承 T004 L1/L2 | `validate_prompt_compose_result_field_rules()` 先调 boundary guard | 负例：missing messages / negative estimated_tokens |
| 不引入 provider/policy 越界字段 | 继续由 `PromptBoundaryContracts.h` + T004 contract test 守住 | 复用已有 smoke/contract |

---

## 7. D Gate 结论

| D 原子项 | 状态 | 说明 |
|---|---|---|
| D1：字段全集锁定 | ✅ Done | 6 个字段（4 必填 + 2 可选）全部列出 |
| D2：字段级规则 | ✅ Done | R1-R7 全部明文化 |
| D3：组合规则 | ✅ Done | C1 可程序化验证 |
| D4：与 PromptPolicy / LLMRequest 衔接 | ✅ Done | 主体载荷与预算元数据边界明确 |
| D5：越界字段排除 | ✅ Done | provider/policy/memory 字段均被排除 |
| D6：D Gate | ✅ Done | PASS |

Gate 结论：PASS，可进入 WP04-T005-B。

进入 B 的条件：
1. ✅ `PromptComposeResultGuards.h` 仅需做最小增量：新增字段级校验器。
2. ✅ `PromptComposeResultFieldContractTest.cpp` 可直接按 R5-R7/C1 生成正负例。
3. ✅ 无需修改 `PromptComposeResult.h` 结构定义，避免跨任务扩张。

---

## 8. 可追溯证据

### 本地文档证据

| 条款 | 文档 | 关键节 |
|---|---|---|
| PromptComposeResult 输出面 | `docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md` | §6.3 |
| PromptPolicy 位于 PromptComposer 之后 | `docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md` | §3.4 |
| Prompt 三段模型与 Result 下游位置 | `docs/architecture/DASSALL_Agent_architecture.md` | §5.4.7 |
| T004 冻结的 4 必填 + 2 可选 | `docs/todos/contracts-freeze/deliverables/WP04-T004-PromptComposeResult语义说明.md` | §2 |
| PromptComposeResult 禁区 | `docs/todos/contracts-freeze/deliverables/WP04-T001-ADR006对象影响清单.md` | §4 |
| contracts 冻结顺序 | `docs/plans/DASALL_contracts冻结实施计划.md` | 阶段 3 |

### 外部参考

1. Protobuf Best Practices：契约对象演进默认优先兼容扩展，避免重定义已有字段语义或类型。来源：`https://protobuf.dev/best-practices/dos-donts/`
2. Pact Contract Testing：契约测试应围绕 consumer 真实依赖的字段内容与格式建立，优先校验输出对象对下游消费者真正可用的字段。来源：`https://docs.pact.io/getting_started/what_is_pact_good_for`