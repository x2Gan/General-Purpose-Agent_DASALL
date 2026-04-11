# LLM-TODO-015 PromptRegistry 选择逻辑设计收敛

日期：2026-04-11
任务：LLM-TODO-015
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.6.3 已明确 Prompt 选择优先级为“显式 `prompt_release_id` > active scene/persona selector > profile 默认 selector > 包内 default release”，因此 015 不能只落 scene/persona/default 三段，而必须补足显式 release override。
2. 同一设计文档的 6.15.5 已将 PromptRegistry 收敛为“选择 owner，不是装配 owner”，并要求它只消费 `PromptQuery`、catalog metadata 与 trusted source 过滤，不得读取 memory/knowledge 或执行 provider 私有分支。
3. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 015 的完成判定冻结为“显式 release > scene/persona > default 选择顺序稳定，trusted source 过滤 fail-closed”，对应代码目标为 `llm/src/prompt/PromptRegistry.cpp`，测试出口为 `PromptRegistrySelectionTest` 与 `PromptRegistryTrustSourceTest`。
4. [docs/todos/llm/deliverables/LLM-TODO-007-PromptRegistry选择面接口设计收敛.md](LLM-TODO-007-PromptRegistry选择面接口设计收敛.md) 在 007 轮次刻意未暴露 `prompt_release_id` 字段，但该结论与 6.6.3 / 6.15.5 的显式 override 要求冲突；015 因此必须把它作为 direct blocker fix 一并补入 [llm/include/prompt/PromptQuery.h](../../../../llm/include/prompt/PromptQuery.h)，而不是在实现里硬编码隐式旁路。
5. 015 最终落地了 [llm/src/prompt/PromptRegistry.h](../../../../llm/src/prompt/PromptRegistry.h)、[llm/src/prompt/PromptRegistry.cpp](../../../../llm/src/prompt/PromptRegistry.cpp)、[tests/unit/llm/PromptRegistrySelectionTest.cpp](../../../../tests/unit/llm/PromptRegistrySelectionTest.cpp)、[tests/unit/llm/PromptRegistryTrustSourceTest.cpp](../../../../tests/unit/llm/PromptRegistryTrustSourceTest.cpp)，并更新 [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) 与 [tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp)。

## 2. 外部参考

1. Langfuse 的 Prompt Version Control 文档把 prompt 选择收敛为“显式版本 / 标签优先于默认生产版本”，并强调版本回滚必须有稳定、可审计的显式标识。015 采用 `prompt_id@version` 作为 `prompt_release_id` 的最小格式，并让显式 release 命中路径优先于自动 selector，与这类治理实践一致。参考：https://langfuse.com/docs/prompt-management/features/prompt-version-control
2. C++ Core Guidelines 强调接口边界应把策略判断集中在单一 owner 内，而不是分散在多个调用方里。015 将 base-dimension 过滤、trusted source fail-closed 与稳定 tie-break 全部收敛在 `PromptRegistry`，避免 Runtime/Composer 各自重复实现 prompt 选择。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines

## 3. Design 结论

1. 015 将 `PromptQuery` 最小扩展为显式 `prompt_release_id` 维度，并在本轮定义其格式为 `prompt_id@version`。这样既满足 6.6.3 的显式 override 要求，也不需要回改 shared Prompt contracts。
2. PromptRegistry 的第一层筛选固定为 `stage`、`task_type`、`language`、`model_family` 四个 base dimensions；显式 release 只覆盖第三层 selector 与 default fallback，不绕过 base-dimension 兼容性检查。
3. trusted source 的有效 allowlist 采用“`PromptRegistryConfig.trusted_sources` 与 `PromptQuery.trusted_sources` 的交集”策略：两者都为空时直接 fail-closed；二者存在但无交集时也 fail-closed，并显式返回 `trusted_source_rejected`。这保证 query 不能静默放宽 registry policy。
4. 自动选择顺序固定为：显式 `prompt_release_id` > scene/persona selector > profile selector > package default release。scene/persona 只匹配 query 中显式给出的 selector 维度，不做半匹配升级；profile selector 仅在 scene/persona 未命中后再参与。
5. 为了保证同一 catalog 输入下的稳定结果，015 在每个 selector bucket 内使用固定 tie-break：`is_default_release` > `eval_status` > `source_layer` > `version` > `package_id` > `prompt_id`。这样即便存在多个候选 release，也不会随目录遍历顺序漂移。
6. 当前共享 `ResultCode` 还没有 prompt-not-found 专用枚举，因此 015 暂时将“显式 release 格式非法 / 明确 release 不存在 / 无 default release”统一收敛为 `ValidationFieldMissing`，将 trusted source 缺失或拒绝统一收敛为 `PolicyDenied`。这只是当前错误码空间下的最小闭环，不构成对 future prompt-specific code 的否决。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 补齐显式 release override 输入维度 | [llm/include/prompt/PromptQuery.h](../../../../llm/include/prompt/PromptQuery.h)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |
| 落地 PromptRegistry 具体实现与稳定选择规则 | [llm/src/prompt/PromptRegistry.h](../../../../llm/src/prompt/PromptRegistry.h)、[llm/src/prompt/PromptRegistry.cpp](../../../../llm/src/prompt/PromptRegistry.cpp) |
| 覆盖显式 release、scene/persona、profile、default 与稳定性 | [tests/unit/llm/PromptRegistrySelectionTest.cpp](../../../../tests/unit/llm/PromptRegistrySelectionTest.cpp) |
| 覆盖 trusted source 允许、query widening 拒绝与 allowlist 缺失 fail-closed | [tests/unit/llm/PromptRegistryTrustSourceTest.cpp](../../../../tests/unit/llm/PromptRegistryTrustSourceTest.cpp) |
| 将 Registry 实现与新测试接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `PromptRegistry`，补齐 `PromptQuery.prompt_release_id`，使 llm 首次具备显式 release override、scene/persona/profile 选择与 trusted source fail-closed 的稳定选择闭环。
2. 测试目标：`PromptRegistrySelectionTest` 覆盖显式 release 命中、scene/persona 命中、profile fallback、default fallback 与稳定重复选择；`PromptRegistryTrustSourceTest` 覆盖 trusted source 允许、query widening 拒绝与 allowlist 缺失 fail-closed。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `PromptRegistrySelectionTest`
   - `RunCtest_CMakeTools` 运行 `PromptRegistryTrustSourceTest`

## 6. 风险与回退

1. 015 目前只消费 `PromptRegistryConfig.asset_root` 作为 PromptAssetRepository 的 baseline root，尚未把 013 的 deployment/snapshot source chain 重新投影到 Registry config 面；后续 source switch integration 仍需通过 012/041/032 所在链路继续补齐，而不是在 015 中偷偷扩张 init 接口。
2. `prompt_release_id` 当前只冻结了 `prompt_id@version` 的最小字符串格式；若后续需要和 snapshot label、tenant label 或 rollout label 对齐，应新增独立设计任务，而不是破坏本轮已验证的显式版本语义。
3. 错误码暂借用 `ValidationFieldMissing` / `PolicyDenied` 收敛 prompt 选择失败事实；若后续 prompt-specific result code 被补齐，应优先新增枚举并调整 Registry 映射，而不是回退 015 的选择顺序和 fail-closed 策略。