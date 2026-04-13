# LLM-TODO-024 LLMManager unary 编排与结果装配设计收敛

日期：2026-04-13
任务：LLM-TODO-024
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.6 已冻结 `LLMManager` 为 Runtime 访问 llm 的唯一公共入口，但同时明确它只是编排 owner，不得重解释 profile、复制 PromptPolicy 或重写 ModelRouter 的路由评分逻辑。
2. 同一设计段已固定 unary 主链顺序为：治理后请求进入 `PromptPipeline`，随后交给 `ModelRouter`、`AdapterRegistry`、040 已落盘的 call execution、022 的 `ResponseNormalizer`、023 的 `UsageAggregator`，最后才装配 `LLMManagerResult` 并把 fallback 事实暴露给 Runtime。024 不能跳步，也不能把 028 的 observability bridge 提前揉进来。
3. [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 当前只包含 040 的 `LLMCallExecutor`，说明 024 的真实缺口是 manager facade 本身还没落盘，而不是执行治理还未完成。
4. [llm/src/execution/ResponseNormalizer.h](../../../../llm/src/execution/ResponseNormalizer.h) 和 [llm/src/UsageAggregator.h](../../../../llm/src/UsageAggregator.h) 已分别稳定提供 provider 语义归一化与 usage/cost 聚合，因此 024 只需要消费它们的结果，不应再回头解析 provider-private payload 或自行估算 cost。
5. [llm/include/LLMManagerResult.h](../../../../llm/include/LLMManagerResult.h) 已经预留了 `resolved_route`、`attempted_routes`、`failure_category` 与 `fallback_used`。024 的实现必须把这些字段填实，否则后续 028/031 的 observability 与 fallback integration 都会失去主链证据。

## 2. 外部参考

1. OpenAI Cookbook 的 [How to handle rate limits](https://cookbook.openai.com/examples/how_to_handle_rate_limits) 强调“同一 provider 内的瞬时失败可先做有限重试，再由调用方决定是否切换备选模型”。这与 6.9.2 的职责分层一致，支持 024 继续把同 route retry 留给 040，而把跨 route fallback 保持在 manager 编排层。
2. Kubernetes 的 [Pod lifecycle and restart policy](https://kubernetes.io/docs/concepts/workloads/pods/pod-lifecycle/) 明确区分单实例内部重试与更高层调度器的替换/切换责任。024 据此把 `LLMCallExecutor` 视为单 route 执行 owner，而把 fallback route 切换保持在 `LLMManager`，避免把两层恢复语义混成一个类。

## 3. Design 结论

1. 024 在 [llm/src/LLMManager.h](../../../../llm/src/LLMManager.h) 与 [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 中补一个 module-local concrete `LLMManager`，实现 `ILLMManager`，并允许通过构造注入 `PromptPipeline`、`ModelRouter`、`ProviderCatalogSnapshot`、`AdapterRegistry`、`LLMCallExecutor`、`ResponseNormalizer` 与 `UsageAggregator` 以支撑 unit 测试。
2. `generate()` 的固定顺序收敛为：
   - 校验初始化状态和 request 基本字段。
   - 构造 `PromptQuery`、`PromptComposeRequest`、`PromptPolicyInput` 并执行 `PromptPipeline`。
   - 若治理失败，则直接映射到 `PromptAsset` 或 `PromptGovernance`，不进入路由和 adapter。
   - 调用 `ModelRouter` 生成 primary + fallback chain，并按顺序调用 040 的 `LLMCallExecutor`。
   - 每次成功执行后，把 `AdapterCallResult` 交给 `ResponseNormalizer`；若 normalizer 判定 malformed payload，则映射为 `ProviderProtocol`。
   - 若有 usage fragment 且 catalog 中存在 model metadata，则调用 `UsageAggregator`，并把聚合后的 token/cost 写入 `LLMResponse.tags` 作为 028 前的最小可消费锚点。
   - 第一条成功 route 返回 success；若前序 route 失败但后续成功，则 `fallback_used=true`。
   - 全部 route 都失败时返回 `FallbackExhausted`，并保留完整 `attempted_routes` 与最后一次 failure facts。
3. 失败分类最小映射规则固定为：
   - `PromptPipeline` 无 release 或 `selection_reason` 非空失败：`PromptAsset`
   - `PromptPolicyDisposition` 为 `Deny`、`OverBudget`、`RequireRecompose`：`PromptGovernance`
   - `ModelRouter` 无路由：`Routing`
   - `LLMCallExecutor` 返回 `RouteUnavailable`、`RouteBlocked`、`ConcurrencyRejected`、`Timeout`、`AdapterFailure`：单 route 失败统一记为 `AdapterTransport`，若所有 route 均失败则最终升格为 `FallbackExhausted`
   - `ResponseNormalizer` 失败：`ProviderProtocol`
4. 024 不做以下职责：
   - 不新增公开 include 头或 shared contract 字段。
   - 不在 manager 内重算模型分数、重新拼接 prompt asset 或读取 provider 原始载荷。
   - 不实现 stream 生命周期；`stream_generate()` 继续明确 fail-closed 占位。
5. 为保证 024 的验收最小闭环，本轮新增两条 unit：
   - `LLMManagerSuccessPathTest`：验证 PromptPipeline + route + executor + normalizer + usage 成功装配。
   - `LLMManagerFailureMappingTest`：验证治理失败与 fallback exhausted 的分类与 attempted routes 收口。
   原有 [tests/unit/llm/LLMManagerFallbackTest.cpp](../../../../tests/unit/llm/LLMManagerFallbackTest.cpp) 则升级为真正的 manager fallback 测试，而不是仅验证 router 与 registry 的组合。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落地 `LLMManager` concrete owner 与 unary 编排链 | [llm/src/LLMManager.h](../../../../llm/src/LLMManager.h)、[llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) |
| 新增 success path 与 failure mapping 单测 | [tests/unit/llm/LLMManagerSuccessPathTest.cpp](../../../../tests/unit/llm/LLMManagerSuccessPathTest.cpp)、[tests/unit/llm/LLMManagerFailureMappingTest.cpp](../../../../tests/unit/llm/LLMManagerFailureMappingTest.cpp) |
| 将现有 fallback 用例提升为真正的 manager fallback 编排验收 | [tests/unit/llm/LLMManagerFallbackTest.cpp](../../../../tests/unit/llm/LLMManagerFallbackTest.cpp) |
| 将 024 新增测试接入 llm / unit 聚合 | [tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：在 `LLMManager.cpp` 中补 `ILLMManager::generate()` 的 unary 主链编排、fallback 收口、failure category 映射和最小 usage tag 装配。
2. 测试目标：
   - `LLMManagerSuccessPathTest`
   - `LLMManagerFallbackTest`
   - `LLMManagerFailureMappingTest`
3. 验证动作：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `LLMManagerSuccessPathTest`
   - `RunCtest_CMakeTools` 运行 `LLMManagerFallbackTest`
   - `RunCtest_CMakeTools` 运行 `LLMManagerFailureMappingTest`

## 6. 风险与回退

1. 024 暂时把 aggregated usage 通过 `LLMResponse.tags` 追加 `usage:*` / `cost:*` 锚点，目的是让 028 之前已有最小可消费出口；完整日志、trace、metrics、audit bridge 仍必须在 028 统一接线，不能误判为 observability 已完成。
2. `LLMCallExecutor` 目前只提供单 route 执行结果，因此 024 的 fallback exhausted 以“最后一次 route failure + attempted_routes”收口，而不在 manager 内持有更复杂的 attempt timeline。若后续 integration 需要更细粒度 trace，应在 028/031 通过 observability bridge 增补，而不是继续膨胀 `LLMManagerResult`。