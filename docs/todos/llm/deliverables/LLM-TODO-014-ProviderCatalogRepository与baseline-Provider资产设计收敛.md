# LLM-TODO-014 ProviderCatalogRepository 与 baseline Provider 资产设计收敛

日期：2026-04-11
任务：LLM-TODO-014
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.6.4 已冻结 Provider 资产目录形态为 `catalog.yaml + <provider>/manifest.yaml + <provider>/models.yaml`，并明确 provider instance 应视为受管运行资产，而不是硬编码在 adapter 里。
2. 同一设计文档的 6.10.1 与 6.15.2 明确了 Provider Catalog 的三层装载顺序必须对齐 ConfigCenter 的 defaults、deployment override 与 runtime override，并要求只有显式声明为 mutable 的字段才允许被 overlay 改写。
3. 同一设计文档的 6.15.2 还把 Provider Catalog 的 owner 边界收敛为“provider/package manifest、models metadata、truth-source 分类与 overlay 规则的唯一 owner”，并禁止它静默改写 `context_window`、`default/max output`、pricing、`metadata_source_uri`、`metadata_effective_at` 与 feature-level `verification_state`。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 014 的 Build 三件套冻结为 `ProviderCatalogRepository.*`、baseline Provider 资产样例与 `ProviderCatalogParseTest` / `ProviderCatalogOverlayTest` / `ProviderModelMetadataParseTest` 三条单测，并以显式 unit 验证链作为本轮验收出口。
5. 本轮实现已落地 [llm/src/provider/ProviderCatalogRepository.h](../../../../llm/src/provider/ProviderCatalogRepository.h)、[llm/src/provider/ProviderCatalogRepository.cpp](../../../../llm/src/provider/ProviderCatalogRepository.cpp)、[llm/assets/providers/catalog.yaml](../../../../llm/assets/providers/catalog.yaml)、[llm/assets/providers/deepseek/manifest.yaml](../../../../llm/assets/providers/deepseek/manifest.yaml)、[llm/assets/providers/deepseek/models.yaml](../../../../llm/assets/providers/deepseek/models.yaml)，并补齐 [tests/unit/llm/ProviderCatalogParseTest.cpp](../../../../tests/unit/llm/ProviderCatalogParseTest.cpp)、[tests/unit/llm/ProviderCatalogOverlayTest.cpp](../../../../tests/unit/llm/ProviderCatalogOverlayTest.cpp) 与 [tests/unit/llm/ProviderModelMetadataParseTest.cpp](../../../../tests/unit/llm/ProviderModelMetadataParseTest.cpp)。

## 2. 外部参考

1. DeepSeek 官方文档被详细设计冻结为 Provider truth-source 的外部事实源，尤其是 `Models & Pricing`、`Token Usage` 与 `Context Caching` 页面，用于约束 `pricing_ref`、`metadata_source_uri`、`metadata_effective_at` 与 cache pricing 维度。参考入口已在详细设计中固化为 `https://api-docs.deepseek.com/quick_start/pricing` 等链接；当前执行环境无法在线抓取网页，因此本轮沿用仓库已审阅的外部引用而不重述未经现场验证的页面内容。
2. LiteLLM 的 openai-compatible provider 文档体现了“已 Admit 的协议家族可以通过配置接入新 provider 实例”的工程模式，这与 DASALL 里“已有 adapter family 走配置式接入、未知协议族才新增 adapter 代码”的分界一致。参考入口：`https://docs.litellm.ai/docs/providers/openai_compatible`；同样由于当前环境无法在线抓取，本轮将其作为与详细设计一致的外部对齐锚点，而不把未验证的网页细节写成实现事实。

## 3. Design 结论

1. Provider 资产必须保持“顶层索引 + provider 目录包”形态；本轮基线样例以 [llm/assets/providers/catalog.yaml](../../../../llm/assets/providers/catalog.yaml) 和 [llm/assets/providers/deepseek/](../../../../llm/assets/providers/deepseek) 落盘，并显式拆分 provider manifest 与 model metadata。
2. `ProviderCatalogRepository` 负责四件事：读取 baseline / deployment / snapshot 三层 Provider 资产、解析 manifest/models、生成 immutable snapshot、在 overlay 非法或包损坏时保留上一份 valid snapshot。它不负责 route policy、PromptPolicy、runtime budget judgement，也不生成真实 `LLMAdapterConfig` 实例。
3. provider overlay 只允许改写显式声明为 mutable 的字段。本轮把 `auth_ref`、`header_refs`、`base_url_alias` 与 `activation_flag` 收敛为可覆盖字段；`context_window`、pricing、`metadata_effective_at` 与 feature-level `verification_state` 仍视为静态真相源，任何 overlay 试图改写这些字段都会导致整轮 reload 失败。
4. baseline DeepSeek provider 样例把 `deepseek-chat` 与 `deepseek-reasoner` 建模为同一 provider instance 下的两个 model entry，并将 `reasoning_content` 保持为 provider-private `response_private_fields`，避免它进入 shared contracts 或后续主链默认语义。
5. 真实密钥继续停留在 infra secret / auth profile 侧，Provider 资产只允许出现 `secret://`、`profile://` 或 `header://` 这类 reference；本轮仓储实现对 `auth_ref` 与 `header_refs` 做了引用格式校验，从而显式拒绝明文 secret 漏入静态资产。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 Provider Catalog snapshot 与元数据对象 | [llm/src/provider/ProviderCatalogRepository.h](../../../../llm/src/provider/ProviderCatalogRepository.h) |
| 实现 Provider Catalog 解析、overlay 与坏包回退 | [llm/src/provider/ProviderCatalogRepository.cpp](../../../../llm/src/provider/ProviderCatalogRepository.cpp) |
| 提供 baseline Provider 资产样例 | [llm/assets/providers/catalog.yaml](../../../../llm/assets/providers/catalog.yaml)、[llm/assets/providers/deepseek/manifest.yaml](../../../../llm/assets/providers/deepseek/manifest.yaml)、[llm/assets/providers/deepseek/models.yaml](../../../../llm/assets/providers/deepseek/models.yaml) |
| 补齐 Provider 资产解析与 overlay 回归测试 | [tests/unit/llm/ProviderCatalogParseTest.cpp](../../../../tests/unit/llm/ProviderCatalogParseTest.cpp)、[tests/unit/llm/ProviderCatalogOverlayTest.cpp](../../../../tests/unit/llm/ProviderCatalogOverlayTest.cpp)、[tests/unit/llm/ProviderModelMetadataParseTest.cpp](../../../../tests/unit/llm/ProviderModelMetadataParseTest.cpp) |
| 将新实现接入 llm / unit 构建图 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `ProviderCatalogRepository` 与 baseline Provider 资产样例，使 llm 首次具备可解析、可 overlay、可回退的 Provider truth-source 目录。
2. 测试目标：`ProviderCatalogParseTest` 覆盖 provider manifest 与 mutable field 声明；`ProviderCatalogOverlayTest` 覆盖 mutable overlay 与静态元数据不可变；`ProviderModelMetadataParseTest` 覆盖 pricing/context/effective_at/verification_state 提取。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `ProviderCatalogParseTest`
   - `RunCtest_CMakeTools` 运行 `ProviderCatalogOverlayTest`
   - `RunCtest_CMakeTools` 运行 `ProviderModelMetadataParseTest`

## 6. 风险与回退

1. 本轮仍采用 key/list 级最小 YAML 子集与 keyed model map，而不是完整 YAML AST；若后续 trusted snapshot 或 richer provider metadata 需要更复杂结构，应扩展内部解析器，而不是把 schema 复杂度推入 shared contracts。
2. `verification_state` 当前以 feature-level map + 聚合 summary 形式落在 catalog snapshot 中，尚未连到后续 smoke/integration 回写链；该动作继续留给 028、030、031、042 等后继任务完成。
3. 本轮只冻结 Provider Catalog 的 truth-source 与 overlay 边界，不处理 `LLMAdapterConfig` 投影与 provider instance init；这些内容继续留给 041，在不破坏 014 既有静态真相源的前提下完成。