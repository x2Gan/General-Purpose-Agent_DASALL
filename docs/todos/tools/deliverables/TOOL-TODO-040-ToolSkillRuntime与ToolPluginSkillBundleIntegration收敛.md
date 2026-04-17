# TOOL-TODO-040 ToolSkillRuntime 与 ToolPluginSkillBundleIntegration 收敛

日期：2026-04-17  
任务：TOOL-TODO-040  
状态：已完成

## 1. 目标

1. 把 037~039 已落地的 `SkillRegistry`、`SkillRuntime`、`ExternalSkillImporter`、`PluginSkillBundleImporter` 真正串成 integration gate，而不是继续停留在白盒单测通过的状态。
2. 验证 internal skill import -> register -> match -> instantiate 的闭环，以及 plugin skill bundle load -> import -> register -> unload -> revoke 的闭环。
3. 保持 040 的边界清晰：只补 integration tests 和 traceability，不回头扩 shared contracts，也不把 external dialect feature flag 从 module-local 状态放开。

## 2. 实现落点

1. 新增 `tests/integration/tools/ToolSkillRuntimeIntegrationTest.cpp`：
   - 通过 `PluginSkillBundleImporter` 导入 internal normalized bundle；
   - 把导入资产注册到 `SkillRegistry`；
   - 用 `SkillRuntime` 对匹配结果做实例化并断言 `WorkflowPlan`、step 数量和 output binding。
2. 新增 `tests/integration/tools/ToolPluginSkillBundleIntegrationTest.cpp`：
   - 用 `PluginExtensionBridge` 发布 plugin skill bundle delta；
   - 验证 external dialect 在 feature flag 关闭时不导入；
   - 验证 feature flag 打开后可导入并注册到 `SkillRegistry`；
   - 验证 plugin unload 后可按 `source_key` 做 source-scoped revoke。
3. 更新 `tests/integration/tools/CMakeLists.txt`，新增：
   - `dasall_tool_skill_runtime_integration_test`
   - `dasall_tool_plugin_skill_bundle_integration_test`

## 3. 关键设计结论

1. `ToolSkillRuntimeIntegrationTest` 证明了 runtime 只消费 normalized `SkillSpecAsset`，不需要在 integration 层旁路 importer 或临时拼 asset catalog。
2. `ToolPluginSkillBundleIntegrationTest` 证明了 plugin bundle 只通过 `PluginExtensionBridge` 暴露 `SkillAssetRef`，再经 importer 归一化并显式注册；plugin unload 后继续通过 source-scoped revoke 清理资产，不把 registry 生命周期隐式藏进 bridge。
3. external dialect 仍然只在 module-local feature flag 打开时导入；040 的 integration gate 强化了“默认关闭”的约束，而不是绕过它。

## 4. 测试覆盖

1. 新增 `tests/integration/tools/ToolSkillRuntimeIntegrationTest.cpp`：
   - 验证 internal bundle import -> register -> match -> instantiate -> release 的闭环；
   - 验证 `WorkflowPlan` 继续保留 canonical runtime incident workflow 的 step/output binding。
2. 新增 `tests/integration/tools/ToolPluginSkillBundleIntegrationTest.cpp`：
   - 验证 plugin skill bundle delta 发布；
   - 验证 external dialect feature flag 关闭分支；
   - 验证启用 importer 后的 register 路径与 unload revoke 路径。
3. 通过 `ListTests_CMakeTools` 验证两条新增 integration tests 已进入 discoverability。

## 5. 验证

1. 构建：
   - `Build_CMakeTools`
2. 定向测试：
   - `RunCtest_CMakeTools` tests: `ToolSkillRuntimeIntegrationTest`, `ToolPluginSkillBundleIntegrationTest`
3. discoverability：
   - `ListTests_CMakeTools`
4. 结果：
   - `dasall_tools` 与新增 integration targets 构建通过；
   - `ToolSkillRuntimeIntegrationTest`、`ToolPluginSkillBundleIntegrationTest` 全部通过；
   - `ListTests_CMakeTools` 可发现两条新增 integration tests；
   - 历史 `DartConfiguration.tcl` 噪声仍存在，但不影响通过结论。

## 6. 对后续任务的影响

1. Phase 5 的 skill runtime / importer / plugin bundle integration 闭环已经具备自动化验证，041 只剩 profile compatibility 与 discoverability gate 收口。
2. plugin-delivered skill bundle 现在已有 load/unload + revoke 的黑盒基线，后续若扩 dialect 或 bundle layout，应先保持这条 lifecycle 语义不变。
3. Gate-TOOL-09 / Gate-TOOL-10 的后续收口，不需要再回头补 skill runtime/importer 的基础生产能力。

## 7. 风险与后续

1. 当前 integration tests 仍围绕 036 冻结的 canonical sample 展开，未宣称 generic external dialect compatibility；feature flag 和 sample-scope 约束必须继续保留。
2. `DartConfiguration.tcl` 噪声仍会出现在 CMake Tools 测试输出中，041 做 discoverability gate 时仍需继续注明其不影响结论。
3. 如后续引入更复杂的 plugin bundle 资产布局，应先扩 integration fixture，再决定是否调整 importer parser，而不是直接放宽 quarantine 规则。