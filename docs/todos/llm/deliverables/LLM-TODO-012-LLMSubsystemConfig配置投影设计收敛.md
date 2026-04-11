# LLM-TODO-012 LLMSubsystemConfig 配置投影设计收敛

日期：2026-04-11
任务：LLM-TODO-012
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.10 已明确 `LLMSubsystemConfig` 只应承接 `model_profile`、`prompt_policy`、`degrade_policy`、`timeout_policy` 的 llm 消费视图，而不应把完整 `RuntimePolicySnapshot` 直接泄露给 llm 运行面。
2. 同一设计文档的 6.10.1 表格已把 `prompt asset sources`、`prompt selector overlay`、`provider catalog sources` 三类 module-local 配置项冻结为 llm 本地消费字段，因此 012 必须把 baseline root、deployment root、snapshot cache root、signature_required、merge_mode、active_scene、active_persona、default_prompt_bundle 收拢为 llm 本地 overlay，而不是回写 shared profiles/contracts。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.6.3 规定 Prompt 选择优先级为“显式 `prompt_release_id` > active scene/persona selector > profile 默认 selector > 包内 default release”，因此 012 中 `active_scene` / `active_persona` 的默认语义必须是“空值表示不显式覆盖”，不能预先硬编码到 profile 或 release 选择逻辑里。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 012 的验收边界固定为“只消费投影视图、不直接持有全量 `RuntimePolicySnapshot`，且 Provider 实例注入细节继续留给独立任务”，因此本轮只落投影对象、投影函数、接口冻结测试与投影行为单测，不提前进入 013/014/041 的资产解析或 provider init 逻辑。
5. 本轮实现已落地 [llm/include/LLMSubsystemConfig.h](../../../../llm/include/LLMSubsystemConfig.h)、[llm/src/LLMSubsystemConfig.cpp](../../../../llm/src/LLMSubsystemConfig.cpp)、[tests/unit/llm/LLMSubsystemConfigProjectionTest.cpp](../../../../tests/unit/llm/LLMSubsystemConfigProjectionTest.cpp)，并同步更新 [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) 与 llm/unit CMake 接线，证明确认 012 是“配置投影对象 + 测试证据”这一最小原子任务。

## 2. 外部参考

1. The Twelve-Factor App 的 Config 原则要求“部署差异配置应与代码分离”，这与 012 的做法一致：profile snapshot 负责共享治理事实，llm 只消费投影后的运行视图，而 Prompt/Provider 资产根路径与 selector overlay 保持为 module-local 配置输入，不在代码中展开为第二套全局配置系统。参考：https://12factor.net/config
2. C++ Core Guidelines 的 C.2 / C.8 强调“有复杂不变量时用 class，否则优先用 struct 表达值对象”。012 中的 `LLMSubsystemConfig` 及其子对象采用简单 struct + `has_consistent_values()` 的形式，目的是保持为可测试、可复制的投影视图值对象，而不是引入隐藏状态和双阶段初始化。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines

## 3. Design 结论

1. `LLMSubsystemConfig` 只投影 llm 运行面真正消费的字段：`profile_id`、stage route map、prompt allowlist / trusted sources / tool visibility rules、prompt/provider 资产源、selector overlay、degrade policy、llm timeout policy、worker_threads；它不保留 `RuntimePolicySnapshot` 本体，也不暴露 token budget、ops policy、execution policy 等当前任务未消费的域。
2. `LLMSubsystemConfigOverlay` 明确承接 llm module-local 配置输入：`PromptAssetSourceConfig`、`PromptSelectorOverlay`、`ProviderCatalogSourceConfig`。这保证 012 只做 profile -> llm 视图投影，不把 llm 重新变成一个配置系统 owner。
3. `PromptAssetSourceConfig.baseline_root` 固定为 `llm/assets/prompts`，`ProviderCatalogSourceConfig.baseline_root` 固定为 `llm/assets/providers`；`deployment_root` / `snapshot_cache_root` 默认为空，表示未显式配置 overlay 源。这样既冻结了 baseline 默认路径，又不给 013/014 预先捏造安装态目录假设。
4. `PromptSelectorOverlay.active_scene` 与 `active_persona` 默认为空字符串，语义是“当前无显式 selector override”。这允许后续 PromptRegistry 继续按设计文档约定回退到 profile 默认 selector 和包内 default release，而不是在 012 上过早固定成某个默认场景或人格。
5. `project_llm_subsystem_config()` 在 `RuntimePolicySnapshot` 或 overlay 不一致时返回 `std::nullopt`，保持 fail-fast，而不是悄悄拼出半有效配置；`PromptAssetSourceConfig` 仅当配置了 `snapshot_cache_root` 时才要求 `cache_ttl_ms > 0`，避免把未启用 snapshot cache 的默认配置误判为非法。
6. `make_prompt_policy_input()` 与 `stage_route_for()` 将 llm 消费端的两类常见读取方式固定为 helper：前者把 profile prompt policy 与 selector overlay 投影到 `PromptPolicyInput`，后者提供稳定的 stage route 查找入口，方便 020/021 后续任务直接消费而不需要再碰全量 snapshot。
7. Provider 资产中的 `auth_ref`、`header_refs`、`base_url alias`、activation flag 仍属于 041 的后续投影链，本轮不在 `LLMSubsystemConfig` 中提前扩张这些 provider init 细节，以保持 012 与 014/041 的边界干净。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 llm 配置投影视图类型 | [llm/include/LLMSubsystemConfig.h](../../../../llm/include/LLMSubsystemConfig.h) |
| 实现 snapshot -> llm consumer view 投影函数 | [llm/src/LLMSubsystemConfig.cpp](../../../../llm/src/LLMSubsystemConfig.cpp) |
| 在接口冻结测试中补齐 `LLMSubsystemConfig` public surface 断言 | [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |
| 新增 route / allowlist / timeout / 默认 selector 行为单测 | [tests/unit/llm/LLMSubsystemConfigProjectionTest.cpp](../../../../tests/unit/llm/LLMSubsystemConfigProjectionTest.cpp) |
| 将新实现与单测接入 llm / unit CMake | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：新增 `LLMSubsystemConfig`、overlay/value 子对象、投影函数与 helper，并把 `RuntimePolicySnapshot` 裁剪为 llm 可直接消费的配置视图。
2. 测试目标：验证公共接口面已冻结，且 `LLMSubsystemConfigProjectionTest` 能覆盖 route、allowlist、timeout、默认资产根路径、空 scene/persona 语义与 overlay 合法性。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_llm_interface_surface_unit_test`、`dasall_llm_subsystem_config_projection_unit_test`
   - `RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest`
   - `RunCtest_CMakeTools` 定向执行 `LLMSubsystemConfigProjectionTest`

## 6. 风险与回退

1. 012 当前只投影 llm timeout、degrade、prompt policy 与资产源，不投影 provider instance init 细节；若后续 041 需要 `auth_ref`、`header_refs`、`base_url alias` 等 provider 运行字段，必须在 041 中沿资产/route 链补齐，而不是在 012 上直接扩表。
2. `default_prompt_bundle` 当前默认留空，表示“不给 PromptRegistry 额外 bundle 提示”。若 013/015 后发现 baseline Prompt 资产必须依赖显式 bundle 常量，也应先回到配置投影评审，而不是在实现阶段静默写死到 Registry/Composer。
3. `LLMSubsystemConfig` 目前不承接 token budget、runtime budget 或 model metadata，因为这些域尚未在 012 的 llm 消费边界上落定；后续若 016/020/029 需要补投影，应通过新增明确字段或 helper 收敛，而不是反向把完整 snapshot 注入 llm 组件。