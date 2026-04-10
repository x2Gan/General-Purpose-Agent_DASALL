# LLM-TODO-006 ILLMManager 输入输出与失败语义设计收敛

日期：2026-04-10
任务：LLM-TODO-006
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.2 已冻结 `ILLMManager::init()`、`generate()`、`stream_generate()`、`health_check()` 四个入口，并明确 Runtime 只应通过 manager 统一进入 llm，而不是绕过 manager 直接调用 router 或 adapter。
2. 同一设计文档的 6.9.2 明确要求 llm 只把失败分类、attempt trace 和 retryable hint 返回给 Runtime；尤其在 fallback exhausted 时，`LLMManagerResult` 必须带上 attempted routes、last failure category 和可观测原因。因此 006 不能只停留在“`response + error`”二元结构，必须把 fallback 语义一并冻结进结果对象。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.1 已把 ModelRouter 的最小输入冻结为 `stage`、`task_type`、`ModelSelectionHint`、profile 派生 route policy、health snapshot 与 provider catalog snapshot；因此 006 的 `LLMGenerateRequest` 至少需要稳定承载前 3 个 runtime handoff 维度，而不应把 route 展开职责下沉到 Runtime。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 LLM-TODO-006 定义为阶段 B 的第二个原子任务，完成判定是“manager 结果可表达 success / failure / fallback_used 且不吞错”。
5. [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) 已在 005 升级为真实接口冻结测试门，因此 006 应继续在同一测试壳上扩展 manager SPI、request 字段和 fallback 失败语义，而不是新开平行测试出口。

## 2. 外部参考

1. C++ Core Guidelines 的 C.121 / C.127 继续适用：作为 Runtime 统一入口的 `ILLMManager` 应保持纯抽象接口和虚析构，避免在稳定 SPI 面混入生命周期状态或实现细节。本轮据此保持 [llm/include/ILLMManager.h](../../../../llm/include/ILLMManager.h) 只暴露签名，不引入内部成员。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class
2. cppreference 对 `std::optional` 的说明明确指出：`optional` 用来表达“一个值可能存在，也可能不存在”，尤其适合不能伪造默认成功值的返回场景。本轮据此把 [llm/include/LLMManagerResult.h](../../../../llm/include/LLMManagerResult.h) 的 `code` 冻结为 `std::optional<ResultCode>`，避免因为当前 shared `ResultCode` 没有 success sentinel 而误引入 `0` 值伪协议。参考：https://en.cppreference.com/w/cpp/utility/optional

## 3. Design 结论

1. `ILLMManager` 保持为 Runtime 访问 llm 的纯抽象统一入口，签名冻结为：`init(const LLMSubsystemConfig&)`、`generate(const LLMGenerateRequest&)`、`stream_generate(const LLMGenerateRequest&, IStreamObserver*)`、`health_check() const`。
2. `ILLMManager.h` 只包含 [llm/include/LLMGenerateRequest.h](../../../../llm/include/LLMGenerateRequest.h) 与 [llm/include/LLMManagerResult.h](../../../../llm/include/LLMManagerResult.h)，继续前向声明 `LLMSubsystemConfig`、`HealthStatus`、`IStreamObserver`，避免把 011/012 的 supporting types 和配置投影提前揉进 006。
3. `LLMGenerateRequest` 冻结为 `stage`、`task_type`、预路由 `contracts::LLMRequest` 与 `selection_hint` 四部分：其中 `request` 表示经过 PromptPolicy 治理后的 provider-neutral handoff，`model_route` 在此阶段允许为空或只作为 pre-route hint，真正的最终 route 仍由 manager 内部的 ModelRouter 决定。
4. `selection_hint` 采用 `std::shared_ptr<const ModelSelectionHint>` 持有 opaque supporting type，而不是在 006 里直接展开 [llm/include/route/ModelSelectionHint.h](../../../../llm/include/route/ModelSelectionHint.h) 的定义；这样既冻结了 manager 需要消费该 hint 的边界，又不提前推进 011。
5. `LLMManagerResult` 的 success / failure 语义冻结为互斥边界：成功时只有 `response`；失败时同时带 `code`、`error`、`failure_category`；`fallback_used` 与 `attempted_routes` 用于表达 failover 事实；`resolved_route` 记录最终实际命中的 route。
6. `attempted_routes` 与 `failure_category` 在 006 正式进入 `LLMManagerResult`，因为 6.9.2 已要求 fallback exhausted 必须把这两类事实返回给 Runtime。若只保留 `response/error`，后续恢复与观测链无法区分“主路失败但 fallback 成功”和“fallback 全部耗尽”。
7. `retryable hint` 不再额外复制为独立布尔字段，而继续通过 `ErrorInfo.retryable` 提供单一真相源，避免 manager result 和 ErrorInfo 之间出现重复或冲突的 retry 语义。
8. `LLMManagerResult::has_consistent_values()` 作为本轮的 module-local 边界守卫，显式拒绝 success/failure 混合态、failure 缺少 `code` 或 `failure_category` 的半成品态、以及 fallback 已发生但 attempt trace 不完整的状态。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 Runtime 统一 llm 入口 SPI | [llm/include/ILLMManager.h](../../../../llm/include/ILLMManager.h) |
| 冻结 manager 输入 handoff 对象 | [llm/include/LLMGenerateRequest.h](../../../../llm/include/LLMGenerateRequest.h) |
| 冻结 manager 成功/失败/fallback 结果对象 | [llm/include/LLMManagerResult.h](../../../../llm/include/LLMManagerResult.h) |
| 继续在同一 llm unit 门上扩展签名与字段冻结断言 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `ILLMManager`、`LLMGenerateRequest`、`LLMManagerResult`，并把 `LLMInterfaceSurfaceTest` 扩展到 manager SPI、request handoff 字段和 fallback 失败语义。
2. 测试目标：验证 `ILLMManager` 仍是纯抽象统一入口；`LLMGenerateRequest` 可承载 pre-route handoff；`LLMManagerResult` 能稳定表达 success / failure / fallback_used 且不吞错。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_llm`、`dasall_unit_tests`
   - `RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest`

## 6. 风险与回退

1. 本轮没有定义 `LLMSubsystemConfig`、`HealthStatus`、`ModelSelectionHint`、`IStreamObserver` 的完整 supporting type，只冻结了 manager 对它们的依赖方向；若后续 011/012 不继续补齐定义，真正消费这些类型的实现阶段仍会暴露缺口。
2. `LLMGenerateRequest.request` 当前允许 `model_route` 为空或仅作为 pre-route hint；若后续实现误把它直接当作 adapter 可发送的最终 `LLMRequest`，会绕过 6.15.1 规定的 ModelRouter authority boundary。
3. `LLMManagerResult.code` 改为 optional 之后，后续实现不得再通过“默认构造 enum 值 0”去表达成功，否则会重新引入未定义成功哨兵；成功路径应只通过 `response` 和 `resolved_route` 判定。
