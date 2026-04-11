# LLM-TODO-007 PromptRegistry 选择面接口设计收敛

日期：2026-04-11
任务：LLM-TODO-007
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.3 已冻结 `IPromptRegistry::init()` 和 `select()` 两个入口，并明确 PromptRegistry 只根据 `PromptQuery` 与资产目录做选择，不读取原始 memory / knowledge 候选。
2. 同一设计文档的 6.4.2 / 6.4.3 已将 [llm/include/prompt/PromptQuery.h](../../../../llm/include/prompt/PromptQuery.h)、[llm/include/prompt/PromptRegistryResult.h](../../../../llm/include/prompt/PromptRegistryResult.h)、[llm/include/prompt/PromptRegistryConfig.h](../../../../llm/include/prompt/PromptRegistryConfig.h) 列为 module-local supporting type，并给出 `stage`、`task_type`、`language`、`model_family`、`scene_id`、`persona_id`、`profile_id`、`available_tools`、`trusted_sources` 等选择维度示意。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.5 进一步要求 PromptRegistry 是选择 owner，不是装配 owner；它只根据 `PromptQuery`、catalog metadata 与 trusted source 过滤选择 release，不做消息装配、不做 provider 私有分支。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 LLM-TODO-007 的完成判定收敛为“PromptRegistry 只承载选择职责、不介入消息装配”，因此本轮必须停留在 SPI、输入对象、配置对象和返回元数据边界。
5. [contracts/include/prompt/PromptRelease.h](../../../../contracts/include/prompt/PromptRelease.h) 与 [contracts/include/prompt/PromptComposeRequest.h](../../../../contracts/include/prompt/PromptComposeRequest.h) 已提供 `PromptRelease`、`PromptEvalStatus` 和 `CompositionStage` 的共享定义；007 应直接复用这些 contracts，而不是自造 stage/release 副本。
6. [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) 已在 005/006 升级为 llm 公共接口冻结测试门，因此 007 应继续在同一出口补齐 PromptRegistry 的签名、字段与 success/failure 边界断言，而不是新开并行 surface test。

## 2. 外部参考

1. C++ Core Guidelines 的 C.121 / I.4 强调，作为稳定边界使用的接口应保持纯抽象，并通过强类型对象表达输入语义。本轮据此保持 [llm/include/prompt/IPromptRegistry.h](../../../../llm/include/prompt/IPromptRegistry.h) 为纯抽象 SPI，并以 `PromptQuery`、`PromptRegistryConfig` 取代离散字符串参数。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class
2. cppreference 对 `std::optional` 的说明指出，它适合表达“某值存在或不存在”的状态边界。本轮据此把 [llm/include/prompt/PromptRegistryResult.h](../../../../llm/include/prompt/PromptRegistryResult.h) 的 `code` 冻结为 `std::optional<ResultCode>`，避免在当前 shared `ResultCode` 没有 success sentinel 的前提下伪造成功码。参考：https://en.cppreference.com/w/cpp/utility/optional

## 3. Design 结论

1. `IPromptRegistry` 保持为 Prompt 资产选择面的纯抽象 SPI，签名冻结为 `init(const PromptRegistryConfig&)` 与 `select(const PromptQuery&) const`。
2. 007 只冻结 Registry 的选择面，不冻结 catalog 遍历算法、source overlay 装载顺序或具体匹配规则；这些实现细节继续留给 013/015，不在本轮接口面硬编码。
3. `PromptQuery` 冻结为九个选择维度：`stage`、`task_type`、`language`、`model_family`、`scene_id`、`persona_id`、`profile_id`、`available_tools`、`trusted_sources`。这保证 scene/persona/profile-aware Prompt 选择先停留在 module-local 边界，不提前扩张 shared Prompt contracts。
4. `PromptRegistryConfig` 只冻结 `asset_root` 与 `trusted_sources` 两项初始化输入，表达 Registry 依赖的最小配置面，不把 runtime profile 投影、repository snapshot 生命周期或 provider 配置混入 007。
5. `PromptRegistryResult` 冻结为 `code`、`release`、`selected_prompt_id`、`selected_version`、`selection_reason`、`trusted_sources_matched` 六部分：成功时返回选中的 `PromptRelease` 与审计元数据；失败时只返回失败事实，不伪造 release。
6. `PromptRegistryResult.code` 在本轮收敛为 optional 失败码，而不是非 optional 状态码。当前 shared `ResultCode` 没有 success sentinel，因此成功路径通过 `release` 是否存在来判定，失败路径才携带 `code`。
7. `PromptRegistryResult::has_consistent_values()` 作为 007 的 module-local 边界守卫，显式拒绝 success/failure 混合态、`selected_prompt_id` 与 `selected_version` 半缺失态，以及失败时仍携带 prompt identity 或 trusted source 匹配信息的脏状态。
8. 成功路径要求 `selected_prompt_id`、`selected_version` 与 `release.prompt_id`、`release.version` 保持一致，确保 Registry 返回的审计元数据和共享 PromptRelease 不会出现双重真相源。
9. 007 故意不在 `PromptQuery` 中补入显式 `prompt_release_id` override 字段；若后续 015 确认实现确需暴露该维度，应回到接口评审单独扩展，而不是在实现阶段绕过 007 直接加字段。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 Prompt 选择面 SPI | [llm/include/prompt/IPromptRegistry.h](../../../../llm/include/prompt/IPromptRegistry.h) |
| 冻结 Prompt 选择输入对象 | [llm/include/prompt/PromptQuery.h](../../../../llm/include/prompt/PromptQuery.h) |
| 冻结 PromptRegistry 初始化配置对象 | [llm/include/prompt/PromptRegistryConfig.h](../../../../llm/include/prompt/PromptRegistryConfig.h) |
| 冻结 Prompt 选择结果与审计元数据 | [llm/include/prompt/PromptRegistryResult.h](../../../../llm/include/prompt/PromptRegistryResult.h) |
| 在同一 llm surface test 出口扩展 Registry 边界断言 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `IPromptRegistry`、`PromptQuery`、`PromptRegistryConfig`、`PromptRegistryResult`，并扩展 `LLMInterfaceSurfaceTest` 覆盖 Registry SPI、选择维度和结果元数据边界。
2. 测试目标：验证 `IPromptRegistry` 仍是纯抽象选择 SPI；`PromptQuery` 保留 scene/persona/profile/tool/source 维度；`PromptRegistryResult` 能稳定表达 success/failure 二值边界和审计元数据一致性。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_llm`、`dasall_unit_tests`
   - `RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest`

## 6. 风险与回退

1. 本轮只冻结了 PromptRegistry 的接口面，没有定义 catalog 读取、overlay 顺序和稳定选择算法；若 013/015 在实现期偏离 6.15.5 的稳定选择要求，需要回到设计评审，而不是在 007 的接口文件上偷偷补逻辑字段。
2. `PromptRegistryResult.code` 目前使用 optional 表达失败码缺省；若后续 shared `ResultCode` 引入明确 success sentinel，应重新评估是否有必要统一到非 optional，但不得以破坏现有 success/failure 二值边界为代价。
3. `PromptQuery` 目前未承载显式 release override；若后续实现发现必须公开该维度，应新增独立接口冻结任务处理，而不是让 015 在不回写 TODO/交付物的情况下直接扩字段。