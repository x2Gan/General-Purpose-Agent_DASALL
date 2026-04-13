# LLM-TODO-033 persona selection integration 设计收敛

日期：2026-04-13
任务：LLM-TODO-033
状态：Done

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.6.3 已冻结 scene/persona 选择边界：stage 是第一选择维度，task_type / language / model_family 是第二层维度，scene_id / persona_id / profile_id 是 module-local 的第三层选择维度；显式 `prompt_release_id` 的优先级仍高于自动选择。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 9.3 已明确要求新增 `LLMPersonaSelectionIntegrationTest`，用于验证“同一 stage 下基于 scene/persona 的 Prompt 变体选择与审计锚点”。
3. [llm/src/prompt/PromptRegistry.cpp](../../../../llm/src/prompt/PromptRegistry.cpp) 已完整实现 `explicit_prompt_release_id -> scene_persona_selector -> profile_selector -> default_release` 的选择链，并在 `selection_reason` 中保留选择路径。
4. [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 已把 `config.prompt_selector_overlay.active_scene` / `active_persona` 投影到 `PromptQuery`，因此 scene/persona 选择已进入真实 manager hot path，而不是停留在 registry 单测。
5. [llm/src/prompt/PromptPipeline.cpp](../../../../llm/src/prompt/PromptPipeline.cpp) 已把 `query.scene_id` / `query.persona_id` 注入 `PromptPolicyInput`，说明 033 不需要复制 Prompt 三段实现；现有 prompt pipeline 已能消费 scene/persona 选择输入。
6. [tests/unit/llm/PromptRegistrySelectionTest.cpp](../../../../tests/unit/llm/PromptRegistrySelectionTest.cpp) 已证明 registry 层的 scene/persona > profile > default 选择顺序，因此 033 的 owner 不是再补 unit，而是在真实 manager + adapter dispatch 闭环中固定选择结果。
7. [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp) 与 [tests/integration/llm/LLMPromptSourceSwitchIntegrationTest.cpp](../../../../tests/integration/llm/LLMPromptSourceSwitchIntegrationTest.cpp) 已提供 033 可复用的真实 prompt/manager integration 基座、单一路由 provider catalog 与 adapter request 断言模式。

## 2. 外部参考

1. 本轮未引入新的外部 provider 协议或 persona 规范；033 的 owner 完全由 DASALL 已冻结的 scene/persona selection design 和现有 registry/manager 实现驱动。

## 3. Design 结论

1. 033 不需要新增生产代码修补。scene/persona 选择所需的三个关键接缝都已存在：`LLMManager` 会投影 `active_scene/active_persona`，`PromptRegistry` 已实现 scene/persona/profile/default 选择链，`PromptPipeline` 已保留 scene/persona 到 governance 输入面的透传。
2. 因为 033 的目标是验证 persona 选择，而不是 route/fallback/source-layer 行为，测试必须继续把 provider catalog 收敛为单一 `deepseek-prod/deepseek-chat` route，让断言集中在 prompt release 选择和 compose 后的 messages，而不是让 routing 变量稀释结论。
3. 033 的最小覆盖矩阵应包含四条路径：scene/persona 精确命中、同一 stage 下 persona 变体命中、scene/persona miss 后回落到 profile selector、profile 也 miss 后回落到 default release。
4. “审计锚点”在本轮不通过扩 shared contracts 或新增 observability 字段实现，而是通过真实 `PromptRegistryResult.selection_reason`、`selected_version`，以及 manager 输出的 `response.prompt_id` / `response.prompt_version` 与 adapter dispatch 前的 composed `messages` 固定下来。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| scene/persona / profile / default 四条选择路径覆盖 | [tests/integration/llm/LLMPersonaSelectionIntegrationTest.cpp](../../../../tests/integration/llm/LLMPersonaSelectionIntegrationTest.cpp) |
| integration test 注册 | [tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) |
| 033 证据与状态回写 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 5. Build 三件套

1. 代码目标：新增 `LLMPersonaSelectionIntegrationTest`，在真实 `PromptPipeline + LLMManager` 闭环里动态生成 scene/persona prompt release 变体，并固定 scene/persona/profile/default 四条选择路径。
2. 测试目标：
   - 正例 1：`scene=operator`、`persona=planner` 命中 `scene_persona_selector`
   - 正例 2：`scene=general`、`persona=explainer` 命中 persona 变体 release
   - 回退 1：scene/persona miss 后回落到 `profile_selector`
   - 回退 2：profile 也 miss 后回落到 `default_release`
3. 验收命令：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_llm_persona_selection_integration_test`
   - `RunCtest_CMakeTools` 运行 `LLMPersonaSelectionIntegrationTest`
   - `Build_CMakeTools` 构建目标 `dasall_integration_tests`

## 6. 风险与回退

1. 033 当前固定的是 prompt release 选择与 compose 后 dispatch 结果，不等于 governance deny 或 profile 差异已经自动成立；034、035 仍必须作为独立 owner 分别收口。
2. 本轮把“审计锚点”收敛在 registry selection reason 和 manager response prompt identity 上，没有新增 scene/persona 专属 structured log / trace attrs；若后续需要更强的观测字段，应在 observability owner 下单独评审，而不是在 033 里临时扩 shared 或 bridge surface。
3. 033 为了隔离 persona 变量继续使用单一路由 provider catalog；若后续测试重新引入 reasoning 或 fallback 候选，需要在新的 owner 用例中显式说明 route 变量对 persona 断言的影响，不能直接复用 033 的结论。