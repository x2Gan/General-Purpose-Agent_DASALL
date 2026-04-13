# LLM-TODO-042 asset-only Provider onboarding integration 设计收敛

日期：2026-04-13
任务：LLM-TODO-042
状态：Done

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.10.1 已冻结 Provider 资产与 profile overlay 的边界，6.14 已明确“既有 admitted family 优先走配置式实例接入”，9.3 则把 `LLMProviderAssetOnboardingIntegrationTest` 列为阶段 H 的 owner 用例。
2. [docs/todos/llm/deliverables/LLM-TODO-041-ProviderConfig投影与mutable-overlay规则设计收敛.md](LLM-TODO-041-ProviderConfig%E6%8A%95%E5%BD%B1%E4%B8%8Emutable-overlay%E8%A7%84%E5%88%99%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 已证明 `auth_ref`、`header_refs`、`base_url_alias`、activation flag 与 `snapshot_version` 能从 Provider 资产和 profile overlay 安全投影到 `LLMAdapterConfig`；042 的 owner 是把这条投影链带入真实 manager dispatch 闭环。
3. [llm/src/provider/ProviderCatalogRepository.cpp](../../../../llm/src/provider/ProviderCatalogRepository.cpp) 已支持 baseline、deployment、snapshot 三层 provider catalog overlay；新增 provider package 可以在不改 baseline 资产的前提下进入发布 snapshot。
4. [llm/src/route/AdapterRegistry.cpp](../../../../llm/src/route/AdapterRegistry.cpp) 的 `initialize_and_register_provider_route(...)` 已能消费 `ProviderDescriptor + ProviderRuntimeProjectionView + model metadata`，完成 adapter init 与 route registration；042 不应再扩张新的生产 owner。
5. [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 已能消费 provider catalog snapshot，但当前不会自动把 snapshot 中的 provider/model route 注册进 registry；因此 042 的最小验证 owner 应放在 integration fixture 内，而不是把 route 自动装配强行塞回生产逻辑。
6. [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp) 与 [tests/integration/llm/LLMProfileIntegrationTest.cpp](../../../../tests/integration/llm/LLMProfileIntegrationTest.cpp) 已提供真实 `PromptPipeline + LLMManager + AdapterRegistry` integration 基座，说明 042 缺失的是 asset-only onboarding 证据，而不是新的 prompt/profile 主链实现。

## 2. 外部参考

1. 本轮未引入新的外部 Provider SDK、secret resolver 或 endpoint 发现协议；042 的 owner 完全由 DASALL 既有 Provider 资产模型、profile 投影边界与 llm integration 设计驱动。

## 3. Design 结论

1. 042 不需要新增生产修补。`ProviderCatalogRepository`、`project_llm_subsystem_config(...)`、`AdapterRegistry::initialize_and_register_provider_route(...)` 与 `LLMManager.generate()` 的最小链路已经存在，缺失的是 integration 证据，而不是 provider onboarding 逻辑本身。
2. 042 的最小接线必须显式区分“provider asset 被接入”和“provider route 被 profile 激活”两件事：deployment 层新增 provider package 只能证明实例进入 catalog snapshot，是否参与 dispatch 仍由 profile route 决定。
3. 042 的最小矩阵应覆盖两条路径：
   - 正例：deployment 层新增 `openclaw-prod/openclaw-chat`，profile 把 `response` route 显式投影到 `cloud.premium`，manager 应选中新 provider instance
   - 负例：相同 provider 资产已存在，但 profile 仍走 `cloud.default`，manager 应继续命中 baseline `deepseek-prod/deepseek-chat`，证明新 provider 不会被隐式启用
4. 042 必须同时固定 adapter init 投影证据：`adapter_family`、`provider_instance_id`、`base_url_alias`、`auth_ref` 与 `snapshot_version` 都应来自 Provider 资产/overlay，而不是测试私写的平行 adapter config。
5. 042 的 integration fixture 应复用真实 baseline DeepSeek catalog，再以 deployment overlay 动态生成 `openclaw` provider package；这样可以把“既有 family + 新 provider instance”收敛为真正的 asset-only onboarding，而不是伪造一套独立 provider family。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| asset-only provider onboarding integration owner 用例 | [tests/integration/llm/LLMProviderAssetOnboardingIntegrationTest.cpp](../../../../tests/integration/llm/LLMProviderAssetOnboardingIntegrationTest.cpp) |
| integration test 注册 | [tests/integration/llm/CMakeLists.txt](../../../../tests/integration/llm/CMakeLists.txt) |
| 042 证据与状态回写 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 5. Build 三件套

1. 代码目标：新增 `LLMProviderAssetOnboardingIntegrationTest`，在真实 `ProviderCatalogRepository + AdapterRegistry + PromptPipeline + LLMManager` 闭环中验证既有 admitted family 的 provider instance 可通过“只加 provider package + auth_ref + profile route”被启用。
2. 测试目标：
   - deployment overlay 新增 `openclaw` provider package 后，catalog snapshot 中出现新的 provider instance
   - `cloud.premium` profile route 会把 dispatch 切到 `openclaw-prod/openclaw-chat`
   - `cloud.default` profile route 不会隐式激活 `openclaw`，而会继续命中 baseline `deepseek-prod/deepseek-chat`
   - `openclaw` adapter init 会真实收到 `adapter_family/provider_instance_id/base_url_alias/auth_ref/snapshot_version` 投影
3. 验收命令：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_llm_provider_asset_onboarding_integration_test`
   - `RunCtest_CMakeTools` 运行 `LLMProviderAssetOnboardingIntegrationTest`
   - `Build_CMakeTools` 构建目标 `dasall_integration_tests`

## 6. 风险与回退

1. 042 当前验证的是“既有 admitted family 的配置式实例接入”，不等于 Cloud/LAN/Local 真 endpoint、真实 secret resolver 或真实 header 注入链已经联通；这些仍受 [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md) 中 `LLM-BLK-007` 的残余约束影响。
2. 本轮 fixture 通过显式 `snapshot -> registry` 注册收口了 042 的最小验证链；若后续要求生产 `LLMManager` 自动装配 catalog routes，需要独立 owner 任务评审生命周期、热更新与并发边界，而不能在 042 的 integration 任务里顺手扩张生产职责。
3. 042 只验证了 existing family `openai_compatible` 的 asset-only onboarding；若后续要同时叠加 cross-family onboarding、fallback exhausted 或 streaming 变量，应在新的 owner 用例里明确扩矩阵，而不是在已完成的 042 上继续堆叠变量。