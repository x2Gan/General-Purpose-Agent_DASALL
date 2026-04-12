# LLM-TODO-017 PromptComposer 装配流程设计收敛

日期：2026-04-12
任务：LLM-TODO-017
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.4 已冻结 PromptComposer 只消费 `PromptComposeRequest + PromptRelease + ModelBudgetHint`，输出 `PromptComposeResult`，且不回写 memory、不改变 ContextPacket ownership。
2. 同一设计文档的 6.2、6.3、6.7.2 和 Design 映射表 LLM-D5 进一步要求：PromptComposer 只负责 provider-neutral message 装配、模板槽位映射与预算告警；一旦发现预算超限，只能返回 warning / decision，不得在 llm 内自行做二次语义裁剪。
3. [contracts/include/prompt/PromptComposeRequest.h](../../../../contracts/include/prompt/PromptComposeRequest.h)、[contracts/include/prompt/PromptRelease.h](../../../../contracts/include/prompt/PromptRelease.h) 与 [contracts/include/prompt/PromptComposeResult.h](../../../../contracts/include/prompt/PromptComposeResult.h) 已冻结 shared prompt contracts。当前 contracts 仅暴露 `context_packet_id`、`task_type`、`visible_tools`、`model_route`、`output_schema_ref` 等字段，并没有 `user_goal` / `constraints` 之类的新语义槽位，也没有 few-shot 包根路径元数据，因此 017 不能通过扩 shared contracts 或伪造语义字段来“补齐输入”。
4. [llm/src/prompt/TemplateRenderer.h](../../../../llm/src/prompt/TemplateRenderer.h) / [llm/src/prompt/TemplateRenderer.cpp](../../../../llm/src/prompt/TemplateRenderer.cpp) 与 [llm/src/TokenEstimator.h](../../../../llm/src/TokenEstimator.h) / [llm/src/TokenEstimator.cpp](../../../../llm/src/TokenEstimator.cpp) 已在 039 / 016 落盘，因此 017 应复用既有模板安全规则和 token 预估器，而不是重新内嵌渲染或预算逻辑。
5. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已将 017 的完成判定冻结为“composer 只做装配与预算告警、依赖独立 TemplateRenderer 完成渲染，且 over-budget 不自行重排上下文”，验收出口固定为 `PromptComposerSlotMappingTest` 与 `PromptComposerOverBudgetTest`。
6. 017 最终落地了 [llm/src/prompt/PromptComposer.h](../../../../llm/src/prompt/PromptComposer.h)、[llm/src/prompt/PromptComposer.cpp](../../../../llm/src/prompt/PromptComposer.cpp)、[tests/unit/llm/PromptComposerSlotMappingTest.cpp](../../../../tests/unit/llm/PromptComposerSlotMappingTest.cpp)、[tests/unit/llm/PromptComposerOverBudgetTest.cpp](../../../../tests/unit/llm/PromptComposerOverBudgetTest.cpp)，并更新了 [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt)。

## 2. 外部参考

1. Anthropic 的 Prompting best practices 强调 system 指令要清晰直接、few-shot 示例是最可靠的格式约束手段之一，并建议把角色/说明/示例清楚分层。017 参考这一经验，固定采用 `system -> few-shot -> user` 的 provider-neutral message 顺序，并把模板变量与输出约束显式写回装配结果，而不是靠下游模型自行猜测。参考：https://platform.claude.com/docs/en/build-with-claude/prompt-engineering/claude-prompting-best-practices
2. Mustache 手册把 Mustache 定义为 logic-less templates。017 延续 039 已收敛的 `simple_var` 思路，只保留最小变量替换，不引入 section、partial、lambda、delimiter 切换等更强解释能力，从而把 Composer 控制在“槽位映射”而不是“模板执行引擎”边界内。参考：https://mustache.github.io/mustache.5.html

## 3. Design 结论

1. 017 在 [llm/src/prompt/PromptComposer.h](../../../../llm/src/prompt/PromptComposer.h) 内新增 module-local `PromptComposer`，实现 `IPromptComposer`，并通过构造注入 `ITemplateRenderer`、`TokenEstimator` 与可选 `FewShotResolver`，保持 PromptComposer 仍是可替换、可 mock 的内部 owner。
2. 槽位映射严格基于当前已冻结的 request / release 字段生成：`request_id`、`stage`、`task_type`、`context_packet_id`、`prompt_release_id`、`visible_tools` / `available_tools`、`model_route`、`output_schema_ref`、`response_format`、`tags`、`prompt_id`、`prompt_version`、`release_scope`、`trusted_source`。017 不猜测 `user_goal` / `constraints` 等不存在于 contracts 中的语义字段。
3. `system_instructions` 与 `task_template` 统一通过 [llm/src/prompt/TemplateRenderer.cpp](../../../../llm/src/prompt/TemplateRenderer.cpp) 渲染，渲染 warning 直接透传进 `PromptComposeResult.composition_warnings`，避免再造第二套模板错误面。
4. few-shot 注入收敛为两条安全路径：默认解析器支持 `inline:` 前缀内容直接注入；更复杂的 few-shot 来源通过注入的 `FewShotResolver` 解决。若 few-shot 数量超过 `max_few_shot_count`，只做配置级上限裁剪并产出 `few_shot_count_capped` warning；若引用无法解析，则保留为 `unresolved_few_shot_ref:*` warning，而不是伪造正文内容。
5. `estimated_tokens` 统一基于完整渲染后的 message payload 调用 `TokenEstimator` 计算；若预算超限，只追加 `over_budget` warning 并保留完整的 message 集，不做预算驱动的 few-shot 删除或语义重裁剪，从而满足 ADR-006 与专项 TODO 对 over-budget 回流的边界要求。
6. 未匹配模板变量继续保留字面占位文本，并发出 `unmatched_variable:<name>` warning。这样 017 可以在不扩张 shared contracts 的前提下如实暴露“输入证据不足”的事实，后续由 018 / Runtime 决定 deny、recompose 或回流，而不是由 Composer 擅自填空。
7. 017 没有改动 shared prompt contracts，也没有让 PromptComposer 读取 memory、knowledge 或 provider 资产仓储；它仍然只做消息装配、warning 汇总和预算预估。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落地 PromptComposer module-local owner 与可注入 few-shot 解析面 | [llm/src/prompt/PromptComposer.h](../../../../llm/src/prompt/PromptComposer.h) |
| 实现 request/release 槽位映射、模板渲染、warning 汇总与 token 预估 | [llm/src/prompt/PromptComposer.cpp](../../../../llm/src/prompt/PromptComposer.cpp) |
| 覆盖槽位替换、few-shot 注入 / 上限、未匹配变量 warning | [tests/unit/llm/PromptComposerSlotMappingTest.cpp](../../../../tests/unit/llm/PromptComposerSlotMappingTest.cpp) |
| 覆盖 over-budget warning、完整 payload 保留与禁止预算驱动裁剪 | [tests/unit/llm/PromptComposerOverBudgetTest.cpp](../../../../tests/unit/llm/PromptComposerOverBudgetTest.cpp) |
| 将 Composer 实现与两条单测接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `PromptComposer` 的 provider-neutral message 装配、deterministic slot mapping、few-shot 注入、warning 生成与 token 预算预估。
2. 测试目标：`PromptComposerSlotMappingTest` 覆盖基础变量替换、few-shot 注入 / 上限与未匹配槽位 warning；`PromptComposerOverBudgetTest` 覆盖 over-budget warning、完整 message 保留与“禁止预算驱动裁剪”边界。
3. 验收动作：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `PromptComposerSlotMappingTest`
   - `RunCtest_CMakeTools` 运行 `PromptComposerOverBudgetTest`

## 6. 风险与回退

1. 当前 shared `PromptComposeRequest` 仍不承载 `user_goal` / `constraints` 等 richer semantic slots；如果后续 Prompt 资产明确依赖这些槽位，应通过上游 request assembly 或 module-local 扩展输入补齐，而不是在 Composer 内伪造默认值。
2. 当前默认 few-shot 解析器只保证 `inline:` 路径和注入式 resolver；若未来 PromptAssetRepository 需要直接支持 package-local `few_shot_refs` 文件解析，应优先在仓储或选择结果中提供可直接消费的路径/内容，而不是让 shared `PromptRelease` 承担文件系统 ownership。
3. 017 已把 over-budget 边界收敛为“只给 warning，不自行裁剪”；若后续 018 需要给 Runtime 更明确的治理判定，应在 PromptPolicy 中输出 `OverBudget` 决策，而不是回退到 Composer 侧重新删减消息。