# LLM-TODO-041 ProviderConfig 投影与 mutable overlay 规则设计收敛

日期：2026-04-13
任务：LLM-TODO-041
状态：D Gate PASS / B Gate In Progress

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.10.1 已冻结 llm 对 provider 配置的消费边界：`auth_ref`、`header_refs`、`base_url alias` 与 activation flag 必须来自 ConfigCenter 对齐的投影视图，而不是在 llm 内部再造平行配置系统。
2. 同一设计文档的 6.14 明确了新 Provider 接入顺序必须沿“adapter family 准入 -> Provider Catalog 实例化 -> registry 纳管 -> route 生效”推进；对已 Admit 的 family，应优先走配置式实例接入，而不是每次新增 provider 都复制 adapter 代码。
3. 同一设计文档的 6.15.2 已冻结 Provider Catalog 的 owner 边界：仓储层只负责 truth-source、mutable overlay 与 immutable snapshot，不直接生成运行态 `LLMAdapterConfig`，更不能把 secret 明文或运行态观测回写到静态 catalog。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 041 的代码目标收敛为 [llm/src/LLMSubsystemConfig.cpp](../../../../llm/src/LLMSubsystemConfig.cpp) 与 [llm/src/route/AdapterRegistry.cpp](../../../../llm/src/route/AdapterRegistry.cpp)，并把验收出口固定为 `ProviderConfigProjectionTest` 与 `ProviderCatalogOverlayTest`。
5. 现状代码里 [llm/src/provider/ProviderCatalogRepository.cpp](../../../../llm/src/provider/ProviderCatalogRepository.cpp) 已能解析并限制 `auth_ref`、`header_refs`、`base_url_alias`、`activation_flag`、`source_version` 的 mutable overlay，但 [llm/include/LLMAdapterConfig.h](../../../../llm/include/LLMAdapterConfig.h) 仍未承载 provider instance、snapshot version、base_url alias 与 activation flag 这些 adapter init 必需输入。

## 2. 外部参考

1. LiteLLM Proxy 配置文档强调 provider/model 部署应通过 alias、`api_base`、凭据引用和环境/secret 管理进行实例化，而不是把密钥或 provider 私有接线写死在调用方代码中。参考：`https://docs.litellm.ai/docs/proxy/configs`。
2. Envoy xDS 动态配置文档强调静态基线与运行时覆盖必须分层发布，更新时通过一致性快照切换而不是就地改写既有真相源；这与 DASALL 在 6.15.2 冻结的 immutable snapshot + overlay owner 边界一致。参考：`https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/operations/dynamic_configuration`。

## 3. Design 结论

1. Provider Catalog 继续只发布 truth-source 与 overlay 结果；`LLMAdapterConfig` 的生成 owner 收敛到 `LLMSubsystemConfig` 投影层，由它把 profile timeout/retry 与 provider runtime overlay 合并成 adapter init 输入。
2. `LLMAdapterConfig` 需要补齐四个 provider-instance 维度字段：`provider_instance_id`、`base_url_alias`、`activation_flag`、`snapshot_version`。其中 `base_url` 继续保留静态基线 endpoint，`base_url_alias` 作为可覆盖的运行态别名单独传递，避免 overlay 去篡改静态 URL 真相源。
3. ProviderConfig 投影必须 fail-closed：`auth_ref` 只允许 `secret://` 或 `profile://`，`header_refs` 只允许 reference 值，空的 `provider_instance_id`、`base_url_alias`、`snapshot_version` 或无效 timeout/retry 输入都不得生成 `LLMAdapterConfig`。
4. AdapterRegistry 在 041 只补“用投影配置初始化并注册 route”的最小 glue code：先投影 `LLMAdapterConfig`，再调用 adapter `init()`，最后把 provider/model route 纳入既有 copy-on-write snapshot。若 provider instance 被显式禁用，则 registry 必须拒绝注册，不得静默放行。
5. mutable overlay 规则继续由 Provider Catalog 仓储层守门；041 只消费已经被仓储层验证过的 overlay 结果，并通过 `snapshot_version` 让 adapter init 看见当前 provider instance 来源版本，从而为后续 042 的 asset-only onboarding 打基础。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 扩展 adapter init 输入，显式承载 provider instance / alias / activation / snapshot version | [llm/include/LLMAdapterConfig.h](../../../../llm/include/LLMAdapterConfig.h)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |
| 冻结 provider runtime overlay 到 adapter config 的投影函数 | [llm/include/LLMSubsystemConfig.h](../../../../llm/include/LLMSubsystemConfig.h)、[llm/src/LLMSubsystemConfig.cpp](../../../../llm/src/LLMSubsystemConfig.cpp) |
| 在 registry 内补齐 adapter init + route 注册 glue code | [llm/src/route/AdapterRegistry.h](../../../../llm/src/route/AdapterRegistry.h)、[llm/src/route/AdapterRegistry.cpp](../../../../llm/src/route/AdapterRegistry.cpp) |
| 补齐 provider projection 与 adapter init 输入回归 | [tests/unit/llm/ProviderConfigProjectionTest.cpp](../../../../tests/unit/llm/ProviderConfigProjectionTest.cpp)、[tests/unit/llm/ProviderCatalogOverlayTest.cpp](../../../../tests/unit/llm/ProviderCatalogOverlayTest.cpp) |
| 将新单测接入 llm unit 构建图 | [tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 provider runtime overlay 到 `LLMAdapterConfig` 的最小投影链，并让 AdapterRegistry 能用该投影视图初始化既有 family adapter。
2. 测试目标：`ProviderConfigProjectionTest` 覆盖 alias / secret ref / activation / snapshot version 与 adapter init 输入；`ProviderCatalogOverlayTest` 继续覆盖 mutable overlay 只改显式允许字段且不污染静态 model metadata。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci -R "Provider(ConfigProjection|CatalogOverlay)Test" --output-on-failure`

## 6. 风险与回退

1. 041 不引入 concrete adapter family，也不让 `LLMManager` 直接持有 provider instance 构造逻辑；真实 family skeleton 继续留给 025/026/027，asset-only onboarding 集成验证留给 042。
2. 若后续需要把 `base_url_alias` 解析到 infra/config 的真实 endpoint，解析 owner 仍应放在 adapter init 或其下游配置桥接层，而不是让 Provider Catalog 回写静态 `base_url`。
3. 若后续发现 `provider_instance_id` 需要从 `provider_id` 分裂为更细的 deployment identity，应优先扩展 overlay 输入对象而不是复用 `adapter_id` 做隐式编码，避免 route 身份和 adapter 实例身份重新耦合。