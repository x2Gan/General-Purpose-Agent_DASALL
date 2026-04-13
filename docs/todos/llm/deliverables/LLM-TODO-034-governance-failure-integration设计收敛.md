# LLM-TODO-034 governance failure integration 设计收敛

日期：2026-04-13
任务：LLM-TODO-034
状态：Done

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.5 与 6.5.6 已冻结 PromptPolicy / PromptPipeline 的 owner：allowlist、trusted source、tool visibility、render budget 都必须在发送前完成治理，治理失败后不得进入 adapter 调用。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.7.2 与 9.3 已明确：over-budget 必须在 llm 内部被识别并回流 Runtime，integration 层要补齐 `LLMGovernanceFailureIntegrationTest`，验证治理失败不会误入 route / adapter 链路。
3. [llm/src/prompt/PromptPolicy.cpp](../../../../llm/src/prompt/PromptPolicy.cpp) 已完整实现 `prompt_release_not_allowed`、`trusted_source_denied` 与 `render_budget_exceeded` 三条治理决策；[tests/unit/llm/PromptPolicyAllowlistTest.cpp](../../../../tests/unit/llm/PromptPolicyAllowlistTest.cpp) 与 [tests/unit/llm/PromptComposerOverBudgetTest.cpp](../../../../tests/unit/llm/PromptComposerOverBudgetTest.cpp) 已覆盖对应的 unit 语义。
4. [llm/src/prompt/PromptPipeline.cpp](../../../../llm/src/prompt/PromptPipeline.cpp) 会在 `registry_result`、`compose_result`、`policy_decision` 之间原样透传 `Deny` / `OverBudget` disposition，并把失败原因保存在 `PromptPipelineResult.reason`。
5. [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 的 `make_pipeline_failure(...)` 已证明当前 manager 失败映射策略：当 `PromptPipelineResult.registry_result.release` 缺失时，结果被归类为 `PromptAsset` / `ValidationFieldMissing`；否则被归类为 `PromptGovernance` / `PolicyDenied`。这意味着 trusted-source reject 目前在真实 manager 路径中表现为 selection-stage fail-closed，而不是 policy-stage governance fail。
6. [tests/unit/llm/LLMManagerFailureMappingTest.cpp](../../../../tests/unit/llm/LLMManagerFailureMappingTest.cpp) 已固定“pipeline deny -> PromptGovernance / PolicyDenied / adapter 不被调用”的 manager 级行为，因此 034 的 owner 是把这些语义带入真实 prompt/manager integration 闭环，而不是再补一个静态 pipeline mock 用例。
7. [tests/integration/llm/LLMPersonaSelectionIntegrationTest.cpp](../../../../tests/integration/llm/LLMPersonaSelectionIntegrationTest.cpp) 已提供 034 可复用的真实 prompt registry / prompt pipeline / manager / 单一路由 adapter fixture。

## 2. 外部参考

1. 本轮未引入新的外部 provider 协议或安全规范；034 的 owner 完全由 DASALL 已冻结的 Prompt governance design 和当前 manager 失败映射实现驱动。

## 3. Design 结论

1. 034 不需要新增生产代码修补。allowlist deny、over-budget 与 trusted-source fail-closed 的实现都已存在，缺失的是 integration 证据，而不是治理逻辑本身。
2. 为了隔离 governance failure 变量，034 继续把 provider catalog 收敛为单一 `deepseek-prod/deepseek-chat` route。测试目标不是 route/fallback，而是证明治理失败时 adapter 从未收到请求。
3. 034 的最小矩阵应覆盖三条路径：
   - allowlist deny：prompt selection 成功，但 PromptPolicy 以 `prompt_release_not_allowed` 否决
   - trusted-source reject：PromptRegistry 以 `trusted_source_rejected` fail-closed，manager 因未选中 release 将结果映射为 `PromptAsset`
   - over-budget：prompt selection 成功，但 PromptPolicy 以 `render_budget_exceeded` 否决
4. 034 的关键断言不是 observability bridge，而是失败收口事实：`response == nullopt`、adapter 调用计数为 0、`attempted_routes` 为空、`error.details.stage == llm.manager.generate`，以及不同路径下的真实 `result.code` / `failure_category`。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| allowlist deny / trusted-source reject / over-budget 三条治理失败路径 | [tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp](../../../../tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp) |
| integration test 注册 | [tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) |
| 034 证据与状态回写 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 5. Build 三件套

1. 代码目标：新增 `LLMGovernanceFailureIntegrationTest`，在真实 `PromptPipeline + LLMManager` 闭环中验证 allowlist deny、trusted-source reject 与 over-budget 三条治理失败路径都不会进入 adapter dispatch。
2. 测试目标：
   - allowlist deny：`PromptGovernance` + `PolicyDenied` + adapter 调用计数 0
   - trusted-source reject：`PromptAsset` + `ValidationFieldMissing` + adapter 调用计数 0
   - over-budget：`PromptGovernance` + `PolicyDenied` + adapter 调用计数 0
3. 验收命令：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_llm_governance_failure_integration_test`
   - `RunCtest_CMakeTools` 运行 `LLMGovernanceFailureIntegrationTest`
   - `Build_CMakeTools` 构建目标 `dasall_integration_tests`

## 6. 风险与回退

1. 034 当前固定的是“治理失败前不调 adapter”的真实行为，但 trusted-source reject 的 manager 分类仍是 `PromptAsset`，而不是 `PromptGovernance`；若后续设计要求把 trusted-source 失败统一提升为 governance category，需要单独的生产改动与兼容性评审，不能在 034 的测试任务里悄悄改语义。
2. over-budget 当前通过 `PromptPolicyDisposition::OverBudget` 进入 manager 失败映射，但 manager 结果码仍统一表现为 `PolicyDenied`；若后续需要单独的 `RuntimeResourceExhausted` 契约语义，也应在独立 owner 下完成，不应由 034 的 integration 测试直接改写现有单测冻结面。
3. 034 为了隔离治理变量继续使用单一路由 provider catalog；若后续想把 governance failure 与 fallback / profile / persona 叠加验证，需要在新的 owner 用例中明确说明多变量交织带来的断言复杂度。