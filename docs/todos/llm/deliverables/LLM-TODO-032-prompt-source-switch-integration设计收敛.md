# LLM-TODO-032 prompt source switch integration 设计收敛

日期：2026-04-13
任务：LLM-TODO-032
状态：Done

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.6.2 已冻结 Prompt 资产 overlay 语义：baseline、deployment override 与 trusted runtime snapshot 必须按固定顺序覆盖，坏 snapshot 不能污染已发布 catalog。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.10.1 已明确 llm 配置必须消费 profile / deployment / runtime 三层投影视图，而不是在 llm 内部退化成只读 baseline 的平行配置系统。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 9.3 已明确要求新增 `LLMPromptSourceSwitchIntegrationTest`，用于验证 baseline、deployment、snapshot 三层 prompt source 的切换与回退。
4. [tests/unit/llm/PromptSourceOverlayTest.cpp](../../../../tests/unit/llm/PromptSourceOverlayTest.cpp) 已证明 `PromptAssetRepository` 自身支持 baseline / deployment / snapshot 三层 overlay，并在候选 snapshot 非法时保留 previous published catalog。
5. [llm/src/prompt/PromptRegistry.cpp](../../../../llm/src/prompt/PromptRegistry.cpp) 与 [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 在本轮前暴露出真实接缝：repository 已支持三层 source chain，但 registry init 与 manager prompt pipeline config 只把 baseline root 投给 `PromptRegistry`，导致真实 manager 主链无法消费 deployment / snapshot prompt source。
6. [tests/unit/llm/PromptRegistrySelectionTest.cpp](../../../../tests/unit/llm/PromptRegistrySelectionTest.cpp)、[tests/unit/llm/PromptRegistryTrustSourceTest.cpp](../../../../tests/unit/llm/PromptRegistryTrustSourceTest.cpp) 与 [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) 为 032 提供了接口冻结面、registry reload 行为和 trusted source fail-closed 的回归锚点。
7. [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp)、[tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp](../../../../tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp) 与 [tests/integration/llm/LLMFallbackIntegrationTest.cpp](../../../../tests/integration/llm/LLMFallbackIntegrationTest.cpp) 已提供 032 可复用的真实 `PromptPipeline + LLMManager + ResponseNormalizer + observability` integration 基座。

## 2. 外部参考

1. 本轮未引入新的外部 provider 规范或云厂商约束；032 的 owner 完全由 DASALL 已冻结的 Prompt overlay / config projection / integration design 驱动。

## 3. Design 结论

1. 032 不能只补 integration test。若继续让 `PromptRegistryConfig` 与 `LLMManager` 只透传 baseline root，那么 deployment override 与 trusted snapshot 即使在 repository 层存在，也无法进入真实 manager hot path；因此本轮必须先补齐 source-chain projection，再谈 integration 证据。
2. `PromptRegistryConfig` 需要从单一 `asset_root` 升级为完整 `PromptAssetSourceConfig asset_sources`，并由 `LLMManager::make_prompt_pipeline_config()` 把 `config.prompt_asset_sources` 全量透传给 `PromptRegistry`，保持 llm 继续消费既有 ConfigCenter 投影视图，而不是新造一套 prompt source 配置面。
3. `PromptRegistry::init()` 在 reload 失败时必须保留 previous valid catalog 的服务能力：当 repository reload 因损坏 snapshot 失败时，返回值仍应显式暴露失败，但 registry 不能把已经发布的 catalog 一并清空，否则 runtime prompt snapshot 的坏包会直接打断已在运行的 manager 闭环。
4. 032 的 integration 只验证 prompt source switch，不引入 route/fallback 变量。测试因此将 provider catalog 收敛为单一 `deepseek-prod/deepseek-chat` route，让断言集中在 `prompt_version` 与 composed `messages`，而不是在路由选择上引入额外漂移。
5. 最小覆盖矩阵必须包含四条路径：baseline only、deployment override over baseline、trusted snapshot over deployment/baseline，以及 snapshot 损坏后的 reload failure but keep previous valid catalog。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| prompt source chain 投影收口 | [llm/include/prompt/PromptRegistryConfig.h](../../../../llm/include/prompt/PromptRegistryConfig.h)、[llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) |
| registry reload 失败后保留 previous valid catalog | [llm/src/prompt/PromptRegistry.cpp](../../../../llm/src/prompt/PromptRegistry.cpp)、[tests/unit/llm/PromptRegistrySelectionTest.cpp](../../../../tests/unit/llm/PromptRegistrySelectionTest.cpp) |
| PromptRegistryConfig 接口冻结面同步 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp)、[tests/unit/llm/PromptRegistryTrustSourceTest.cpp](../../../../tests/unit/llm/PromptRegistryTrustSourceTest.cpp) |
| prompt source switch integration 覆盖与 discoverability | [tests/integration/llm/LLMPromptSourceSwitchIntegrationTest.cpp](../../../../tests/integration/llm/LLMPromptSourceSwitchIntegrationTest.cpp)、[tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) |
| 032 证据与状态回写 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 5. Build 三件套

1. 代码目标：补齐 `LLMSubsystemConfig.prompt_asset_sources -> PromptPipelineConfig.registry_config -> PromptRegistry` 的真实 source-chain projection，并在 registry reload 失败时保留 previous valid catalog 的服务能力。
2. 测试目标：
   - unit：`InterfaceSurfaceTest`、`PromptRegistrySelectionTest`、`PromptRegistryTrustSourceTest` 覆盖接口冻结面、reload failure retain previous valid catalog 与 trusted source fail-closed。
   - integration：`LLMPromptSourceSwitchIntegrationTest` 覆盖 baseline、deployment、snapshot、damaged snapshot retain previous catalog 四条路径。
3. 验收命令：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_llm_prompt_source_switch_integration_test`
   - `RunCtest_CMakeTools` 运行 `LLMPromptSourceSwitchIntegrationTest`
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `Build_CMakeTools` 构建目标 `dasall_integration_tests`

## 6. 风险与回退

1. 032 当前验证的是 prompt source switch，不等于 persona、governance 或 profile 差异已经自动成立；后续 033~035 仍必须在同一真实基座上分别收口，不能复用 032 证据直接宣称阶段 H 完成。
2. `PromptRegistry::init()` 在 reload 失败时会保留 previous valid catalog，但仍返回失败，以便上层显式感知 snapshot 损坏；若后续调用方把返回值直接当成“立即停止服务”的硬开关，需要在 owner 侧单独评审，而不能回退到“失败时清空当前 catalog”的实现。
3. 032 为了隔离 source switch 变量，把 integration provider catalog 收敛为单一路由；若后续测试重新引入 reasoning / fallback 候选，必须在新的 owner 用例里显式说明 route 漂移风险，不能把 032 的断言直接扩展到 mixed-candidate 场景。