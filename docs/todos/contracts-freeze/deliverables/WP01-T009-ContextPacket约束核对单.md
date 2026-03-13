# WP01-T009 ContextPacket 约束核对单

最近更新时间：2026-03-13
任务状态：In Review
任务编号：WP01-T009
上游输入：ADR-006，DASALL_contracts冻结实施计划，WP01-T008 contracts 边界说明 v1

## 1. 任务目标

核对 ContextPacket 的边界是否与 ADR-006 一致，重点确认以下字段被明确排除：

1. final_messages
2. provider_payload
3. rendered_prompt

## 2. 核对范围与方法

### 2.1 核对范围

1. ADR 约束原文（ADR-006）。
2. 计划文档中的二次落地约束（实施计划第 8 阶段 2）。
3. WP01 边界说明中的收束规则（T008）。
4. 仓库文档中是否存在与上述约束冲突的公开描述样例。

### 2.2 核对方法

1. 先确定“权威约束源”：ADR > 计划文档 > WP01 收束文档。
2. 逐项核对排除字段是否在三层文档中保持一致。
3. 检查是否存在“学习文档/草稿文档”中的旧口径残留，并标记为偏差项。

## 3. 权威约束摘录

### 3.1 ADR-006 结论

ContextPacket 只承载语义上下文，不承载最终消息；明确不得包含：

1. final_messages
2. provider_payload
3. rendered_prompt

同时 ADR-006 给出 ContextPacket 的建议语义组成（如 user_turn、goal 摘要、recent_history、summary_memory、retrieval_evidence、latest_observation_digest、active_tools/visible_capabilities 摘要、policy_digest、token_budget_report、belief_state）。

### 3.2 实施计划落地约束

计划文档在阶段 2 已复述 ADR-006 约束：ContextPacket 不包含 final_messages、provider_payload、rendered_prompt。

### 3.3 WP01-T008 收束约束

T008 已将该约束写入“ADR-006 对齐”条款：ContextPacket 为语义上下文对象，不得承载 final_messages、provider_payload、rendered_prompt。

## 4. 字段级核对结果

| 核对项 | ADR-006 | 实施计划 | T008 边界说明 | 结论 |
|---|---|---|---|---|
| 排除 final_messages | 明确排除 | 明确排除 | 明确排除 | 一致通过 |
| 排除 provider_payload | 明确排除 | 明确排除 | 明确排除 | 一致通过 |
| 排除 rendered_prompt | 明确排除 | 明确排除 | 明确排除 | 一致通过 |
| ContextPacket 定位为语义上下文对象 | 明确 | 明确 | 明确 | 一致通过 |

核对结论：

1. WP01 主链路文档体系内，ContextPacket 边界与 ADR-006 保持一致。
2. T009 完成判定“明确排除 final_messages、provider_payload、rendered_prompt”已达成。

## 5. 偏差扫描与处理建议

在仓库中发现一处学习材料存在旧口径（非 WP01 交付物）：

1. docs/LLM Agent学习.md 中出现了将 final_messages 放入 ContextPacket 的示例描述。

处理建议：

1. 该文件属于学习记录，不作为 contracts 冻结依据。
2. 建议在后续文档治理任务中追加“历史示例纠偏”动作，避免误读。
3. 在 T012 评审纪要中记录此偏差为“非阻塞遗留项”，不影响 T009 通过。

## 6. 评审检查单（可复用）

后续凡涉及 ContextPacket 的文档或字段设计，需同时满足：

1. 不得出现 final_messages。
2. 不得出现 provider_payload。
3. 不得出现 rendered_prompt。
4. 若出现消息渲染字段，应归入 PromptComposeRequest/PromptComposeResult 或其后续对象，而非 ContextPacket。
5. 若出现 provider 适配字段，应归入 llm/prompt 子域对象，而非 context 主对象。

## 7. 对后续任务的输入

### 7.1 对 T010 输入

1. 采用相同核对方法：权威 ADR 约束 + 计划文档复述 + WP01 收束文档一致性。

### 7.2 对 T011 输入

1. 延续“建议权/执行权、全局主控/协同子域”的边界核对模板。

## 8. 风险与回退策略

### 8.1 风险

1. 非权威学习材料中残留旧示例可能被误当当前规范。
2. 后续字段设计若从 Prompt/Provider 视角反向驱动，可能再次污染 ContextPacket 边界。

### 8.2 回退策略

1. 一旦发现 ContextPacket 出现三项禁入字段，立即按 ADR-006 回退并在评审中阻断。
2. 保留本核对单作为 T012 评审基准附件，避免重复争议。

## 9. 交付物映射

1. 本文件即 WP01-T009 交付物“ContextPacket 约束核对单”。
2. 可直接作为 WP01-T012 整体骨架评审输入。