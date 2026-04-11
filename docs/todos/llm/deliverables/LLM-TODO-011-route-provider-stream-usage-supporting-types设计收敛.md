# LLM-TODO-011 route/provider/stream/usage supporting types 设计收敛

日期：2026-04-11
任务：LLM-TODO-011
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.4.2 已将 `ProviderDescriptor`、`ModelCatalogEntry`、`ResolvedModelRoute`、`ModelSelectionHint`、`StreamSessionRef`、`TokenEstimate`、`NormalizedUsageRecord` 全部定位为 llm module-local supporting types，而不是 shared contracts。
2. 同一设计文档的 6.4.3 已给出 [llm/include/provider/ProviderDescriptor.h](../../../../llm/include/provider/ProviderDescriptor.h)、[llm/include/provider/ModelCatalogEntry.h](../../../../llm/include/provider/ModelCatalogEntry.h)、[llm/include/route/ResolvedModelRoute.h](../../../../llm/include/route/ResolvedModelRoute.h)、[llm/include/route/ModelSelectionHint.h](../../../../llm/include/route/ModelSelectionHint.h)、[llm/include/TokenEstimate.h](../../../../llm/include/TokenEstimate.h)、[llm/include/NormalizedUsageRecord.h](../../../../llm/include/NormalizedUsageRecord.h) 的字段边界，足以完成 011 的头文件冻结。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.1、6.15.2、6.15.7、6.15.8 进一步冻结了这些 supporting types 的 owner 关系：`ResolvedModelRoute` / `ModelSelectionHint` 属于 ModelRouter，`ProviderDescriptor` / `ModelCatalogEntry` 属于 Provider Catalog，`TokenEstimate` 属于 TokenEstimator，`NormalizedUsageRecord` 属于 UsageAggregator。
4. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 1789、1941、1978 说明 streaming 生命周期仍后置，因此 011 中的 [llm/include/stream/StreamSessionRef.h](../../../../llm/include/stream/StreamSessionRef.h) 只能冻结为 module-local 生命周期锚点，不能提前扩成 shared StreamHandle。
5. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 011 的完成判定收敛为“supporting types 落盘且未反向推进 shared contracts”，因此本轮只落类型和接口测试，不推进资产加载器、ModelRouter、TokenEstimator、UsageAggregator 或 streaming 实现。

## 2. 外部参考

1. C++ Core Guidelines 的 C.2 / C.8 强调普通 value types 应保持为直接可见的数据聚合对象，避免无必要的继承和隐藏状态。011 据此将 supporting types 设计为简单 struct，便于后续 loader/router/aggregator 直接消费。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
2. 配置治理与事实源分层的常见实践要求“静态元数据”和“运行时观测”分离。011 据此将 `ModelCatalogEntry` 的 pricing/context/verification 字段与 `NormalizedUsageRecord` 的 usage/cost 记录分离，避免 usage 反向污染 catalog 真相源。

## 3. Design 结论

1. `ProviderDescriptor` 冻结 provider instance 的 adapter/api family、endpoint、auth_ref、header_refs、capability_tags 与 source_version，用于 Provider Catalog 与配置式 provider 接入，但不进入 shared contracts。
2. `ModelCatalogEntry` 冻结 context window、输出上限、tier traits、capability flags、pricing 与 verification_state，作为 ModelRouter、TokenEstimator 与 UsageAggregator 的静态事实源。
3. `ResolvedModelRoute` 与 `ModelSelectionHint` 冻结为 ModelRouter 的最小输入输出：前者表达主路、fallback 链和 streaming 标志，后者表达复杂度、SLA、预算、工具需求、预计 token 与前次失败数。
4. `TokenEstimate` 与 `NormalizedUsageRecord` 分别冻结预估 token 结果和归一化 usage 结果，保持“预算预估”和“实际用量归并”两条治理链分离。
5. `StreamSessionRef` 只冻结为 module-local 生命周期标识，不提前引入取消、observer、backpressure 或 shared StreamHandle 语义。
6. 本轮所有 supporting types 虽按 provider/route/stream 子目录落盘，但命名空间继续对齐现有 `ILLMAdapter` / `LLMGenerateRequest` 的前向声明，保持在顶层 `dasall::llm`，避免破坏既有公共接口签名。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 provider supporting types | [llm/include/provider/ProviderDescriptor.h](../../../../llm/include/provider/ProviderDescriptor.h)、[llm/include/provider/ModelCatalogEntry.h](../../../../llm/include/provider/ModelCatalogEntry.h) |
| 冻结 route supporting types | [llm/include/route/ResolvedModelRoute.h](../../../../llm/include/route/ResolvedModelRoute.h)、[llm/include/route/ModelSelectionHint.h](../../../../llm/include/route/ModelSelectionHint.h) |
| 冻结 stream 生命周期占位 | [llm/include/stream/StreamSessionRef.h](../../../../llm/include/stream/StreamSessionRef.h) |
| 冻结 token / usage supporting types | [llm/include/TokenEstimate.h](../../../../llm/include/TokenEstimate.h)、[llm/include/NormalizedUsageRecord.h](../../../../llm/include/NormalizedUsageRecord.h) |
| 在 llm 公共接口冻结测试中补齐 supporting types 断言 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增七个 supporting type 头文件，并扩展 `LLMInterfaceSurfaceTest` 覆盖 provider/route/stream/token/usage 字段边界与 module-local 语义。
2. 测试目标：验证 supporting types 可编译、字段集与详细设计一致，并保持不进入 shared contracts、不突破已有 `ILLMAdapter` / `LLMGenerateRequest` 公开签名。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_llm`、`dasall_unit_tests`
   - `RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest`

## 6. 风险与回退

1. `StreamSessionRef` 当前只冻结了生命周期标识；若后续 streaming 需要取消、observer 管理或 backpressure 细节，必须在后续 streaming 任务中单独评审，而不是直接扩大 011 的 supporting type。
2. `ResolvedModelRoute` 仍保持 module-local；若未来跨模块对 route 解释性对象的复用需求显著上升，必须走 037 的 shared admission 评审，而不是把 011 的头文件直接复制进 contracts。
3. `NormalizedUsageRecord` 当前只承载标准化 usage 与估算成本；若后续观测链需要 provider-private usage 细节，应由 UsageAggregator 或 observability bridge 内部保留，而不是反向污染统一 record。
