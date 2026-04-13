# LLM-TODO-031 fallback integration 设计收敛

日期：2026-04-13
任务：LLM-TODO-031
状态：Done

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.7.2 已冻结 failover 边界：over-budget 不允许在 llm 内二次语义裁剪，fallback 只处理 provider route 失败，是否允许切换由 `degrade_policy` 控制，不由 adapter 私自决定。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.9.2 已明确：传输类失败先按 `timeout_policy` 的 retry budget 在同 route 内重试，再由 `degrade_policy` 决定是否切换 fallback；fallback exhausted 后，`LLMManagerResult` 必须带上 attempted routes、最终失败分类和可观测原因。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 9.3 已明确要求新增 `LLMFallbackIntegrationTest`，用于验证 Cloud 失败后切换到 LAN / Local。
4. [tests/unit/llm/LLMManagerFallbackTest.cpp](../../../../tests/unit/llm/LLMManagerFallbackTest.cpp) 已证明 manager 层的最小 fallback 语义：primary cloud route 失败后，manager 会继续尝试后续 route，并在 fallback 成功时返回 `fallback_used = true` 与完整 `attempted_routes`。
5. [tests/unit/llm/ModelRouterFallbackTest.cpp](../../../../tests/unit/llm/ModelRouterFallbackTest.cpp) 已证明 route 准备顺序：explicit stage fallback 要先于 degrade chain，且当 `allow_model_failover` 关闭时不会追加 degrade-chain routes。
6. [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp) 与 [tests/integration/llm/LLMIntegrationTestSupport.h](../../../../tests/integration/llm/LLMIntegrationTestSupport.h) 已提供 031 可复用的真实 llm integration 基座和 observability recorder，允许在不改生产逻辑的前提下补齐 fallback 的集成证据。

## 2. 外部参考

1. AWS Builders' Library 的《Timeouts, retries, and backoff with jitter》明确指出：重试必须有界，不能在多层栈里无限放大；更合理的做法是在单一层级限制重试次数，并尽早把失败上抛给更高层处理。这与 DASALL 6.9.2 的“同 route 内按 retry budget 重试，再由 manager 走显式 fallback，最终失败交还 Runtime 收口”一致。参考：https://aws.amazon.com/builders-library/timeouts-retries-and-backoff-with-jitter/

## 3. Design 结论

1. 031 继续复用 029 的真实 `PromptPipeline + LLMManager + ResponseNormalizer` 基座，不使用 fake pipeline；fallback integration 的证据必须来自真实 manager 调用闭环，而不是仅靠 unit mock 验证 route 列表。
2. 本轮至少覆盖三类结果：
   - primary 失败后，explicit stage fallback `lan.general` 成功。
   - primary 与 LAN fallback 均失败后，degrade-chain `local.small` 成功。
   - primary、LAN、Local 全部失败后，manager 返回 `FallbackExhausted`，并保留完整 `attempted_routes`。
3. 为了让 fallback 链稳定落在 `cloud -> lan -> local`，031 在 test catalog snapshot 内裁掉与本任务无关的 `deepseek-reasoner` 候选；该调整严格停留在 integration fixture，不回写生产 provider 资产或 routing 逻辑。
4. success fallback 路径除了最终 `resolved_route` 之外，还必须断言 `fallback_used = true`、`attempted_routes` 完整以及 structured log 中的 `outcome = degraded`；fallback exhausted 路径则必须断言 `failure_category = FallbackExhausted`、`attempted_routes` 覆盖全部 route，并保留最终失败原因。
5. 031 不改生产 retry/fallback 策略、provider assets 或 adapter skeleton；所有失败注入都停留在 integration fixture 内，通过 `MockLLMAdapter` 返回 transport-class error 完成。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| fallback integration 三类结果覆盖 | [tests/integration/llm/LLMFallbackIntegrationTest.cpp](../../../../tests/integration/llm/LLMFallbackIntegrationTest.cpp) |
| integration test 注册 | [tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) |
| 031 证据与状态回写 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 5. Build 三件套

1. 代码目标：新增 `LLMFallbackIntegrationTest`，在真实 llm manager 闭环中注入 Cloud/LAN/Local 三条 route 的 transport 成败矩阵，验证 fallback success、degrade-chain success 和 fallback exhausted。
2. 测试目标：
   - 正例 1：cloud 失败后 `lan-ollama/lan-general` 成功，`fallback_used = true`
   - 正例 2：cloud 与 LAN 失败后 `local-runtime/local-small` 成功，`attempted_routes` 覆盖三条 route
   - 负例：三条 route 全部失败时返回 `FallbackExhausted`，并保留最终失败分类与 attempted routes
3. 验收命令：
   - `Build_CMakeTools` 构建目标 `dasall_llm_fallback_integration_test`
   - `RunCtest_CMakeTools` 运行 `LLMFallbackIntegrationTest`
   - `Build_CMakeTools` 构建目标 `dasall_integration_tests`

## 6. 风险与回退

1. 若 fallback integration 在测试中误把 `request.model_route` 重新写入 prompt query，会再次触发 029/030 已经暴露过的 prompt 资产匹配问题；031 应继续沿用真实 smoke 的输入面，不把 pre-route hint 注入 PromptRegistry。
2. 若后续测试把 `deepseek-reasoner` 重新放回 031 的 catalog snapshot，fallback 链顺序可能从 `cloud -> lan -> local` 漂移到包含 reasoning route 的更长链路；因此 031 应继续把 reasoner 裁剪限制在本任务 fixture 内，并在需要验证 mixed-candidate fallback 时另开专门用例。
3. 若 `LLMFallbackIntegrationTest` 只能验证 fallback success 而没有 exhausted 路径，本轮不能把 031 标记为 Done，因为 6.9.2 明确要求 exhausted 结果携带 attempted routes 与 failure category。