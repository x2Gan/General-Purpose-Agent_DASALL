# LLM-GAP-002 production provider family closeout

日期：2026-05-19
来源任务：LLM-GAP-002
状态：Done

## 1. 任务边界

1. 本轮只收口 `LLM-GAP-002`，不合并 `LLM-GAP-003` 或 release / soak 证据。
2. 任务目标是确认 production factory 多 family 注册已由 `LLM-FIX-002` 闭合，并把该结论形成面向 GAP 的独立 closeout 记录。
3. 本轮不新增 provider adapter，不修改 provider catalog schema，不执行 qemu / kvm。

## 2. 本地证据

| 证据面 | 当前状态 | 判定 |
|---|---|---|
| factory family mapping | `LLMProductionFactory.cpp` 显式识别 `openai_compatible`、`ollama_native`、`local_runtime` | production factory 不再只支持单一 Cloud route |
| baseline catalog | `llm/assets/providers/catalog.yaml` 包含 `deepseek`、`ollama_lan`、`local_runtime` | Cloud / LAN / Local baseline provider package 均可被 repository 装载 |
| provider manifests | deepseek 使用 `openai_compatible`，ollama_lan 使用 `ollama_native`，local_runtime 使用 `local_runtime` | adapter_family 与 provider instance 已分层 |
| tests | `LLMProductionFactoryTest`、`LLMProviderAssetOnboardingIntegrationTest`、`LLMFallbackIntegrationTest` 覆盖 production registration / asset-only onboarding / fallback chain | 多 family 注册具备 focused / integration 反回归证据 |

## 3. 外部参考

LiteLLM provider 文档把多 Provider 接入区分为 provider 列表、OpenAI-compatible provider registration、Ollama provider 与 local / custom endpoint 等 family。这与 DASALL 当前把 provider instance 配置外部化到 catalog、再由 `adapter_family` 映射到 adapter factory 的做法一致：协议族复用 adapter，实例新增优先走资产配置。

## 4. Design -> Build 映射

| Design 判定 | Build 三件套 |
|---|---|
| provider instance 与 adapter family 必须分层 | 代码目标：复用 `LLMProductionFactory` 的 family -> adapter 映射和 baseline provider catalog；本轮不新增产品代码 |
| OpenAI-compatible / Ollama / Local family 都必须可注册和路由 | 测试目标：`LLMProductionFactoryTest`、`LLMProviderAssetOnboardingIntegrationTest`、`LLMFallbackIntegrationTest` |
| 关闭 GAP 时不得外推 streaming family 完整性或 release evidence | 验收命令：`RunCtest_CMakeTools(tests=["LLMProductionFactoryTest","LLMProviderAssetOnboardingIntegrationTest","LLMFallbackIntegrationTest"])`；如 CMake Tools test generation 失败，则使用同一 build tree 的 direct CTest fallback 并记录限制 |

## 5. D Gate

结果：PASS。

1. 范围单一：只处理 `LLM-GAP-002`。
2. 设计边界清楚：provider family registration closed；provider live endpoint / release / soak evidence 不外推。
3. Build 三件套已锁定：代码目标、测试目标、验收命令均可二值判断。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_llm_production_factory_unit_test","dasall_llm_provider_asset_onboarding_integration_test","dasall_llm_fallback_integration_test"])`
	- 结果：通过；三个 focused targets 构建成功。
2. `RunCtest_CMakeTools(tests=["LLMProductionFactoryTest","LLMProviderAssetOnboardingIntegrationTest","LLMFallbackIntegrationTest"])`
	- 结果：工具在 generation 层失败，未进入测试执行；该结果不代表测试失败。
3. fallback：`ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(LLMProductionFactoryTest|LLMProviderAssetOnboardingIntegrationTest|LLMFallbackIntegrationTest)$'`
	- 结果：通过；`100% tests passed, 0 tests failed out of 3`，`LLMProductionFactoryTest`、`LLMFallbackIntegrationTest`、`LLMProviderAssetOnboardingIntegrationTest` 均通过。

## 7. 完成判定

`LLM-GAP-002` 已关闭。

1. `LLMProductionFactory` 已能按 provider catalog 的 `adapter_family` 注册 OpenAI-compatible、Ollama native 与 Local runtime routes。
2. focused validation 证明 production factory、多 provider asset onboarding 与 fallback chain 未回退。
3. 本结论不外推为所有 provider family streaming 已完成，也不外推为 release / installed / soak 证据。