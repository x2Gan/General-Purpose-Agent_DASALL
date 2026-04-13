# LLM-TODO-023 UsageAggregator 用量与成本归并设计收敛

日期：2026-04-13
任务：LLM-TODO-023
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.8 已把 `UsageAggregator` 冻结为 raw usage fragment 到 `NormalizedUsageRecord` 的唯一归并点，并明确它既要处理标准 prompt/completion token，也要处理 `prompt_cache_hit_tokens` / `prompt_cache_miss_tokens` 这样的 provider 特有计费维度。
2. 同一文档 6.15.8 还明确禁止 `UsageAggregator` 回写静态 Provider Catalog、替代 observability bridge 或在内部发起 provider 调用。因此 023 的 scope 只能是纯函数式 token/cost 归并，不得把 028 的 bridge 接线或 024 的 manager 编排提前揉进来。
3. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 023 的验收固定为 `ResponseNormalizerUsageTest` 与 `LLMObservabilityFieldCompletenessTest`。其中后者原本属于 028 的完整 bridge 门禁，因此 023 需要做一个最小 acceptance 补位：先提供“成本锚点字段可观测”的同名 unit 用例，后续 028 再在原文件上扩展为完整 bridge 覆盖。
4. [llm/src/execution/ResponseNormalizer.h](../../../../llm/src/execution/ResponseNormalizer.h) 与 [llm/src/adapters/AdapterCallResult.h](../../../../llm/src/adapters/AdapterCallResult.h) 已在 022 稳定产出 `AdapterUsageFragment`，这使 023 无需再改 adapter SPI 或 shared `LLMResponse`，只需消费已有的 usage side-channel。
5. [llm/src/provider/ProviderCatalogRepository.h](../../../../llm/src/provider/ProviderCatalogRepository.h) 已提供 `ProviderModelMetadata.summary` 中的 `input_cache_hit_usd_per_1m`、`input_cache_miss_usd_per_1m`、`output_usd_per_1m` 与 `pricing_ref`，足够让 023 在不读取任何外部系统的前提下完成分价计算。

## 2. 外部参考

1. OpenAI 官方 [API 定价](https://openai.com/api/pricing/) 页面显式区分“输入 / 缓存输入 / 输出”三类费率，这直接支持 023 把 prompt cache hit 和 miss 分开计费，而不是把全部 prompt tokens 粗暴按一个输入费率处理。
2. OpenTelemetry 的 [Semantic conventions for generative client AI spans](https://opentelemetry.io/docs/specs/semconv/gen-ai/gen-ai-spans/) 建议记录 `gen_ai.usage.input_tokens`、`gen_ai.usage.output_tokens`、`gen_ai.usage.cache_read.input_tokens` 等 usage 属性，并指出 input token 总数应包含 cached tokens。023 据此把 cache hit/miss 保留在 `NormalizedUsageRecord` 中，供 028 后续 bridge 直接消费，而不是在 023 丢失这些观测锚点。

## 3. Design 结论

1. 023 在 [llm/src/UsageAggregator.h](../../../../llm/src/UsageAggregator.h) 与 [llm/src/UsageAggregator.cpp](../../../../llm/src/UsageAggregator.cpp) 中新增 module-local `UsageAggregator` concrete owner，输入固定为 022 产出的 `AdapterUsageFragment` 与 014 产出的 `ProviderModelMetadata`，输出固定为 `NormalizedUsageRecord`。
2. 成本计算规则固定为：
   - `prompt_cache_hit_tokens * input_cache_hit_usd_per_1m`
   - `prompt_cache_miss_tokens * input_cache_miss_usd_per_1m`
   - `completion_tokens * output_usd_per_1m`
   三段分别换算到 per-call 美元值后求和。
3. 当 usage fragment 没有显式给出 cache hit/miss 拆分时，023 采用最小保守规则：把全部 `prompt_tokens` 视作 miss tokens。这样标准 provider 的 usage 仍可正确计费，而不需要在 023 猜测不存在的缓存命中信息。
4. 当 usage fragment 只给出 hit 或 miss 其中一个时，023 可基于 `prompt_tokens` 反推另一个字段；若 pricing metadata 缺失（`pricing_ref` 为空且相关费率全为 0），023 选择 graceful fallback 为 `estimated_cost_usd = 0.0`，但仍完整保留 token totals 和 provider/model identity，不阻塞主链。
5. 023 不做以下职责：
   - 不改写 Provider Catalog 的静态 pricing metadata。
   - 不把 cost 塞回 shared `LLMResponse`。
   - 不在本轮实现 028 的 bridge sink，只提供 `NormalizedUsageRecord` 和成本锚点字段测试。
6. 为满足 023 当前验收而不越权完成 028，新增的 `LLMObservabilityFieldCompletenessTest` 只验证 `provider_id`、`model_id`、`pricing_ref` 与 `estimated_cost_usd` 这些 cost anchor 已可被消费；完整的 route / latency / prompt / error / reasoning mode 字段覆盖继续留在 028 扩展。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落地 UsageAggregator concrete owner 与分价计算规则 | [llm/src/UsageAggregator.h](../../../../llm/src/UsageAggregator.h)、[llm/src/UsageAggregator.cpp](../../../../llm/src/UsageAggregator.cpp) |
| 在既有 ResponseNormalizerUsageTest 上补齐标准 usage 和 cache split 成本断言 | [tests/unit/llm/ResponseNormalizerUsageTest.cpp](../../../../tests/unit/llm/ResponseNormalizerUsageTest.cpp) |
| 补位成本锚点字段可观测测试 | [tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp](../../../../tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp) |
| 将 023 的实现与补位测试接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `UsageAggregator`，覆盖标准 prompt/completion usage 归并、cache hit/miss 分价和 pricing metadata 缺失时的零成本回退。
2. 测试目标：
   - `ResponseNormalizerUsageTest`：继续验证 022 的 usage side-channel 保留，并新增 023 的标准 usage / cache split 成本断言。
   - `LLMObservabilityFieldCompletenessTest`：验证 `provider_id`、`model_id`、`pricing_ref` 与 `estimated_cost_usd` 等成本锚点已可被后续 observability 消费。
3. 验证动作：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `ResponseNormalizerUsageTest`
   - `RunCtest_CMakeTools` 运行 `LLMObservabilityFieldCompletenessTest`

## 6. 风险与回退

1. 023 当前只做静态 pricing metadata 的 per-call 估算，没有处理未来可能出现的区域加价、优先处理服务或 batch 折扣等运行时费率。这些属于 provider asset / profile 投影层面的扩展，应留给后续资产或配置任务，而不是在 023 内部引入第二套费率系统。
2. `LLMObservabilityFieldCompletenessTest` 本轮只是最小 acceptance 补位，验证成本锚点字段不丢失；后续 028 需要在同名文件基础上扩展完整 bridge 字段矩阵，不能误把 023 的最小测试当成 028 已完成。