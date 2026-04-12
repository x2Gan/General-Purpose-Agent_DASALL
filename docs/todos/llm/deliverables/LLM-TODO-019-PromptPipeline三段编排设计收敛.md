# LLM-TODO-019 PromptPipeline 三段编排设计收敛

日期：2026-04-12
任务：LLM-TODO-019
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.6 已冻结 `IPromptPipeline` 的唯一职责：把 PromptRegistry、PromptComposer、PromptPolicy 封装为统一 façade，按 `select -> compose -> evaluate` 固定顺序执行，并在 `OverBudget`、`Deny`、`RequireRecompose` 场景下把失败语义原样透传给 Runtime。
2. 同一设计文档明确禁止 Pipeline 在内部发起模型调用、读取 memory 或绕过 PromptPolicy 直接产出 governed messages。因此 019 的主目标只能是“把 015/017/018 串起来”，而不是把新的治理逻辑继续塞进 Pipeline。
3. [llm/include/prompt/IPromptPipeline.h](../../../../llm/include/prompt/IPromptPipeline.h)、[llm/include/prompt/PromptPipelineConfig.h](../../../../llm/include/prompt/PromptPipelineConfig.h) 与 [llm/include/prompt/PromptPipelineResult.h](../../../../llm/include/prompt/PromptPipelineResult.h) 已在 010 冻结 façade SPI、配置聚合对象与返回类型。当前 public interface 只暴露 `PromptQuery`、`PromptComposeRequest` 与 `PromptPolicyInput`，并没有显式 `ModelBudgetHint`，因此 019 必须在实现侧把 `render_budget_tokens` 桥接成 composer 可消费的预算提示，而不能再扩公共接口。
4. [llm/src/prompt/PromptRegistry.cpp](../../../../llm/src/prompt/PromptRegistry.cpp)、[llm/src/prompt/PromptComposer.cpp](../../../../llm/src/prompt/PromptComposer.cpp)、[llm/src/prompt/PromptPolicy.cpp](../../../../llm/src/prompt/PromptPolicy.cpp) 已分别落盘 015、017、018。尤其是 018 新增的 `PromptPolicyInput.selected_release_scope`、`selected_trusted_source` 与 `visible_tools` 只是 module-local 输入面；若 019 不在运行时把 Registry/Compose/Tool Policy Gate 的实际事实灌入这些字段，018 的 direct blocker 仍然没有真正闭环。
5. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 019 的完成判定冻结为“只做三段编排、不吞错、不发起模型调用”，测试门要求至少覆盖 select 失败、OverBudget 透传、policy deny、Allow 四条路径。
6. 019 最终落地了 [llm/src/prompt/PromptPipeline.h](../../../../llm/src/prompt/PromptPipeline.h)、[llm/src/prompt/PromptPipeline.cpp](../../../../llm/src/prompt/PromptPipeline.cpp)、[tests/unit/llm/PromptPipelineTest.cpp](../../../../tests/unit/llm/PromptPipelineTest.cpp)，并更新了 [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt)。

## 2. Design 结论

1. 019 在 [llm/src/prompt/PromptPipeline.h](../../../../llm/src/prompt/PromptPipeline.h) 内新增 module-local `PromptPipeline` concrete owner，实现 `IPromptPipeline`，并通过构造注入 `IPromptRegistry`、`IPromptComposer`、`IPromptPolicy`。默认构造直接装配 015/017/018 的 concrete owner，测试则可注入 recording stub，从而在不改 public SPI 的前提下稳定覆盖四条路径。
2. `PromptPipeline::init()` 只负责把 `PromptPipelineConfig` 下发给三个 owner：任一 owner 初始化失败，Pipeline 自身也失败，不做局部启用或降级启用。
3. `PromptPipeline::run()` 严格按固定顺序执行：先 `select()`，再 `compose()`，最后 `evaluate()`。如果 Registry 返回失败或不一致结果，Pipeline 立即以 `Deny` 返回，仅保留 `registry_result`；如果 Compose 产物缺少 messages / selected prompt / estimated tokens，则同样立即停止，不调用 Policy。这样 019 把“不吞错、不跳步”落实成真实代码路径，而不是口头约束。
4. 针对 010 未暴露 `ModelBudgetHint` 的现实约束，019 采用最小桥接：把 `PromptPolicyInput.render_budget_tokens` 映射为 composer 侧 `ModelBudgetHint.context_window`。这样 017 仍可通过既有 budget hint 机制产出 `over_budget` warning，而 018 再基于 redaction 后 payload 做最终 budget 裁定，整个链路无需再扩 public interface。
5. 019 真正闭合了 018 的 direct blocker：Pipeline 在调用 Policy 前会富化 `PromptPolicyInput`，把 `active_scene` / `active_persona` 从 query 回填，把 `selected_release_scope` / `selected_trusted_source` 从 Registry 选中的 release 和 matched trusted source 回填，把 `visible_tools` 从 `PromptComposeRequest.visible_tools` 或 `PromptQuery.available_tools` 回填。至此，Policy 不再依赖测试注入才能获得本次调用的真实 Prompt release/source/tool 事实。
6. `PromptPipelineResult` 的顶层语义收敛为：Allow 路径保留全部中间产物且 `reason` 为空；非 Allow 路径原样保留 Registry/Compose/Policy 已产生的产物，并把失败原因透传给 Runtime。`OverBudget` 不做二次裁剪，`Deny` 不发起模型调用，`RequireRecompose` 不伪造 governed messages。
7. 019 没有新增任何 shared contracts，也没有把 Pipeline 演化成第二个 LLMManager；它仍然只是 Prompt 三段治理的 façade owner，为后续 024 的 unary 主链收口提供稳定入口。

## 3. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落地 PromptPipeline façade owner 与依赖注入面 | [llm/src/prompt/PromptPipeline.h](../../../../llm/src/prompt/PromptPipeline.h)、[llm/src/prompt/PromptPipeline.cpp](../../../../llm/src/prompt/PromptPipeline.cpp) |
| 把 `render_budget_tokens` 桥接为 composer 可消费的 `ModelBudgetHint` | [llm/src/prompt/PromptPipeline.cpp](../../../../llm/src/prompt/PromptPipeline.cpp) |
| 在运行时富化 `PromptPolicyInput` 的 selected release/source/tools 事实 | [llm/src/prompt/PromptPipeline.cpp](../../../../llm/src/prompt/PromptPipeline.cpp) |
| 覆盖 select 失败、OverBudget 透传、policy deny、Allow 四条 façade 路径 | [tests/unit/llm/PromptPipelineTest.cpp](../../../../tests/unit/llm/PromptPipelineTest.cpp) |
| 将 Pipeline 实现与新单测接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 4. Build 三件套

1. 代码目标：实现 `PromptPipeline` 的三段 façade 编排、失败即停、budget hint 桥接与 policy_input 富化，不新增模型调用或 memory 访问。
2. 测试目标：`PromptPipelineTest` 覆盖 select 失败、OverBudget 透传、policy deny、Allow 四条主路径，并额外断言 `render_budget_tokens` 桥接与 `selected_release_scope` / `selected_trusted_source` / `visible_tools` 富化。
3. 验收动作：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_prompt_pipeline_unit_test`
   - `RunCtest_CMakeTools` 运行 `PromptPipelineTest`
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`

## 5. 风险与回退

1. 当前 `render_budget_tokens -> ModelBudgetHint.context_window` 是在 façade 输入未显式暴露 budget hint 前的最小桥接；如果后续 Runtime 需要区分 `context_window`、`max_output_tokens`、`reserved_output_tokens` 三个预算维度，应优先评估是否要在 llm public SPI 层做正式补设计，而不是继续在 Pipeline 内堆叠隐式映射。
2. 019 目前把 selected source/scope/tools 富化逻辑放在 Pipeline 内部，适合默认“一步调用”模式；若未来 Runtime 选择分步调用 Registry/Composer/Policy，高级路径仍需在调用方显式传递这些 per-request 事实，不能假设 Policy 会自己推断。
3. Compose 失败目前通过 `PromptComposeResult` 是否具备最小可治理字段来判定，并回传现有 warning/兜底原因；如果后续需要更细的 compose error taxonomy，应优先扩 module-local 结果对象或 façade 内部错误码，而不是回改 shared `PromptComposeResult`。