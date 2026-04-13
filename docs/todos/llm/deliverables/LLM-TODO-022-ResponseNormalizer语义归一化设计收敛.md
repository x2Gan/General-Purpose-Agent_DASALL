# LLM-TODO-022 ResponseNormalizer 语义归一化设计收敛

日期：2026-04-13
任务：LLM-TODO-022
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.4 已把 `ResponseNormalizer` 冻结为 provider raw result 到 shared `LLMResponse` / `ErrorInfo` 的唯一收口点，并明确它同时拥有 provider-private 字段剥离与失败分类边界。因此 022 不能把这部分逻辑散落到 adapter、LLMManager 或 observability 里。
2. 同一文档 6.15.4 还把归一化顺序固定为“结构校验 -> 提取最终语义与 usage -> 剥离 `reasoning_content` 等 private 字段 -> 把 usage/cost side-channel 交给后续组件”。这意味着 022 必须同时产出“共享响应对象 + module-local usage side-channel”，而不是只返回一个 `LLMResponse`。
3. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 022 的完成判定写死为“provider-private 字段被剥离且 malformed payload fail-closed”，测试出口固定为 `ResponseNormalizerSemanticMappingTest`、`ResponseNormalizerReasoningContentStripTest` 与 `ResponseNormalizerUsageTest`。
4. [llm/src/adapters/AdapterCallResult.h](../../../../llm/src/adapters/AdapterCallResult.h) 在 022 开始前只能表达 `response/error/result_code` 三元组，缺少 raw usage fragment 与 provider-private diagnostics side channel。按照旧形态，022 无法承载 `reasoning_content` 剥离或 prompt cache hit/miss 等字段，因此需要做一个最小 direct blocker fix：只在 module-local `AdapterCallResult` 中补 usage/diagnostics side channel，不改共享 contracts。
5. [contracts/include/llm/LLMBoundaryGuards.h](../../../../contracts/include/llm/LLMBoundaryGuards.h) 已提供 `validate_llm_response_field_rules()`，可直接作为 022 的 malformed payload fail-closed 守卫。这使 022 不需要再发明第二套 `LLMResponse` 校验规则。

## 2. 外部参考

1. DeepSeek 官方 [Reasoning Model (`deepseek-reasoner`)](https://api-docs.deepseek.com/guides/reasoning_model) 文档明确说明 `reasoning_content` 与最终 `content` 同级输出，且“if the `reasoning_content` field is included in the sequence of input messages, the API will return a `400` error”。022 据此把 `reasoning_content` 固定为 module-local diagnostics，仅允许记录“已剥离”审计事实，不得进入 shared `LLMResponse` 或下一轮请求。
2. OpenAI 官方 [API 定价](https://openai.com/api/pricing/) 页面显式区分“输入 / 缓存输入 / 输出”三类费率。这与 6.15.8 的 `prompt_cache_hit_tokens` / `prompt_cache_miss_tokens` 设计方向一致，说明 cache hit/miss 应作为成本 side-channel 保留给后续 UsageAggregator，而不是挤进 shared `LLMResponse` 主体语义。

## 3. Design 结论

1. 022 在 [llm/src/adapters/AdapterCallResult.h](../../../../llm/src/adapters/AdapterCallResult.h) 中补入两个 module-local side channel：
   - `AdapterUsageFragment`：承载 `prompt_tokens`、`completion_tokens`、`total_tokens` 与 `prompt_cache_hit_tokens/miss_tokens`。
   - `AdapterProviderDiagnostics`：承载 `reasoning_content`、provider trace id 与内部审计标签。
   这两个对象只在 llm 内部消费，不进入 shared contracts。
2. 022 在 [llm/src/execution/ResponseNormalizer.h](../../../../llm/src/execution/ResponseNormalizer.h) 与 [llm/src/execution/ResponseNormalizer.cpp](../../../../llm/src/execution/ResponseNormalizer.cpp) 中新增 concrete `ResponseNormalizer` owner，并把输出固定为 `ResponseNormalizationResult`：成功时返回共享 `LLMResponse` 加 module-local usage fragment；失败时返回 `ErrorInfo/ResultCode`，并附带审计事件。
3. 由于当前 adapter SPI 尚未落真实 provider family，022 不引入 raw JSON parser，而采用“对 adapter 产出的 shared `LLMResponse` 做结构校验、字段补全、finish_reason 归一化与 private diagnostics 剥离”的最小闭环。这样既满足 6.15.4 的 owner 要求，也不提前为 025/026/027 引入尚未冻结的协议细节。
4. malformed payload 统一按 fail-closed 处理：只要 `LLMResponse` 缺少 required 字段、usage fragment 自相矛盾，或共享响应与 side-channel token 统计冲突，就直接映射为 module-local ProviderProtocol failure，使用 `ValidationFieldMissing` 承载 shared code，并写入 `malformed_payload:*` 审计事件。
5. finish reason 采用最小 canonicalization：`tool_calls -> tool_call`、`max_tokens -> length`、`content_filter -> refusal`，其余未知值收敛为 `unknown` 并附带 `unknown_finish_reason:*` 审计记录。022 不把 provider-specific 原值外泄到 shared `LLMResponse`。
6. usage side-channel 的职责止步于“提取并保持完整”：022 允许把 prompt/completion/total token 回填到 shared `LLMResponse`，但 `prompt_cache_hit_tokens/miss_tokens` 与成本估算继续只留在 module-local fragment 中，交给 023 的 `UsageAggregator` 处理。
7. 022 不做以下职责：
   - 不做 023 的成本计算。
   - 不做 024 的 fallback 编排与 `LLMManagerResult` 组装。
   - 不做 028 的 observability bridge 接线，只保留审计事件与 provider trace id 的 module-local 事实。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 为 AdapterCallResult 增加最小 usage/private diagnostics side channel | [llm/src/adapters/AdapterCallResult.h](../../../../llm/src/adapters/AdapterCallResult.h) |
| 落地 ResponseNormalizer concrete owner 与统一输出面 | [llm/src/execution/ResponseNormalizer.h](../../../../llm/src/execution/ResponseNormalizer.h)、[llm/src/execution/ResponseNormalizer.cpp](../../../../llm/src/execution/ResponseNormalizer.cpp) |
| 覆盖五类共享语义分支的归一化与 metadata 富化 | [tests/unit/llm/ResponseNormalizerSemanticMappingTest.cpp](../../../../tests/unit/llm/ResponseNormalizerSemanticMappingTest.cpp) |
| 覆盖 reasoning_content 剥离、unknown finish_reason 审计与 malformed payload fail-closed | [tests/unit/llm/ResponseNormalizerReasoningContentStripTest.cpp](../../../../tests/unit/llm/ResponseNormalizerReasoningContentStripTest.cpp) |
| 覆盖 token usage 与 prompt cache hit/miss side-channel 保留 | [tests/unit/llm/ResponseNormalizerUsageTest.cpp](../../../../tests/unit/llm/ResponseNormalizerUsageTest.cpp) |
| 将 022 的实现与三条新用例接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：补齐 `AdapterCallResult` side channel，并实现 `ResponseNormalizer` 的结构校验、metadata 富化、finish_reason 规范化、reasoning_content 剥离与 usage fragment 提取。
2. 测试目标：
   - `ResponseNormalizerSemanticMappingTest`：覆盖 DirectResponse / ToolCallIntent / ClarificationRequest / ReplanSuggestion / Refusal 五类语义。
   - `ResponseNormalizerReasoningContentStripTest`：覆盖 `reasoning_content` 剥离、malformed payload fail-closed、unknown finish_reason 审计。
   - `ResponseNormalizerUsageTest`：覆盖 shared token 回填与 prompt cache hit/miss side-channel 保留。
3. 验证动作：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `ResponseNormalizerSemanticMappingTest`
   - `RunCtest_CMakeTools` 运行 `ResponseNormalizerReasoningContentStripTest`
   - `RunCtest_CMakeTools` 运行 `ResponseNormalizerUsageTest`

## 6. 风险与回退

1. 当前 adapter SPI 还没有真实 provider raw payload；022 采用“先对 adapter 产出的 shared 响应做 fail-closed 守卫与 private side-channel 清洗”的最小实现。后续 025/026/027 若引入真实协议族，只需在 `AdapterCallResult` 内继续富化 module-local side channel，而不必回改 shared contracts。
2. 022 暂时用 `ValidationFieldMissing` 承载 malformed payload 的 shared code，这是因为现有 shared `ResultCode` 尚无 `ProviderProtocol` 专项码。真正的语义区分仍保留在 llm 内部 failure category / audit 维度，不在本轮扩大 shared contracts。
3. provider trace id 与 audit tags 当前只保存在 normalizer 的 module-local 输出中；若 028 后续需要统一接 observability，可直接消费该输出，而不需要在 022 回填到 shared `LLMResponse`。