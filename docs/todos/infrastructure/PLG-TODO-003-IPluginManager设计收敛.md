# PLG-TODO-003 IPluginManager 设计收敛

日期：2026-04-01  
任务：PLG-TODO-003  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.6 明确 IPluginManager 必须覆盖 discover、validate、load、unload、list_active 五个治理入口。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5/6.8 仅对 LoadResult 字段给出最小锚点，对 ValidationResult、LoadOptions、ActivePluginSet 未给出对象级定义；若直接落接口，会留下未冻结的返回/参数边界。
3. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6 与 plugin 模块详细设计 6.6 在 discover/profile 和 load/load_options 两处存在签名粒度不一致：前者写 discover()/load(plugin_id)，后者写 discover(profile)/load(plugin_id, load_options)。
4. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 把 PLG-TODO-003 定位为 L2 接口冻结任务，且前置只要求 PLG-TODO-001/002 完成，不允许越界到 manifest/signature/ABI 的完整对象冻结。

## 2. 外部参考

1. Microsoft MSDN Magazine《Writing, Loading, and Accessing Plug-Ins》建议先冻结插件管理器的最小治理接口与结果边界，再把具体装载和校验细节留给后续实现阶段。本轮据此只固化 manager 调用面和 ref 型返回对象，不提前定义完整 Manifest/Signature/Compatibility 报告结构。

## 3. Blocker 修复与 Design 结论

阻塞结论：

1. 原始 003 任务缺少 validate/load/unload/list_active 的最小 request/result 对象，直接落接口会把未冻结对象隐式挪进实现层，属于同轮可修的 context blocker。
2. discover/profile 与 load/load_options 的签名粒度在两份详细设计之间不一致，若不先收敛会导致后续接口 breaking risk。

最小 blocker-fix：

1. 在 IPluginManager.h 内同步冻结最小边界对象：PluginValidationRequest、PluginLoadOptions、PluginValidationResult、PluginLoadResult、PluginUnloadResult、ActivePluginSet。
2. 对仍受 INF-BLK-09 影响的 Manifest/SignatureReport/CompatibilityReport，不提前定义完整对象，只保留 manifest_ref、signature_report_ref、compatibility_report_ref 三个 ref 锚点。
3. discover 统一冻结为 discover(profile_id)，load 统一冻结为 load(plugin_id, load_options)，以对齐 plugin 模块详细设计 6.3/6.6 与 6.9 配置项中的 profile/load_timeout/audit 约束。

设计结论：

1. IPluginManager 保留五个治理入口：discover、validate、load、unload、list_active。
2. discover 返回现有 PluginCatalog；list_active 返回 ActivePluginSet，内部只暴露 PluginDescriptor 集合，不暴露 runtime 调度状态。
3. validate 返回 PluginValidationResult，聚合 PolicyDecisionRef 与 signature/compatibility 的 ref，不越界定义被阻塞的完整报告对象。
4. load/unload 只冻结 phase/result/evidence/handle_ref 级别的最小输出，不提前进入 runtime bridge、safe_mode 或恢复判定实现。
5. 所有失败路径继续只使用 contracts::ResultCode 和 contracts::ErrorInfo，不新增共享错误语义。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 IPluginManager 五个治理入口 | infra/include/plugin/IPluginManager.h |
| 冻结 validate 的最小 request/result 边界 | PluginValidationRequest / PluginValidationResult |
| 冻结 load/unload 的最小 options/result 边界 | PluginLoadOptions / PluginLoadResult / PluginUnloadResult |
| 冻结 active set 只暴露 PluginDescriptor 集合 | ActivePluginSet |
| 提供空壳 manager skeleton | infra/src/plugin/PluginManager.cpp |
| 验证接口签名与边界类型可编译 | PluginManagerInterfaceCompileTest |
| 阻断 ResultCode/ErrorInfo 与报告对象越权扩张 | PluginManagerBoundaryContractTest |

## 5. Build 三件套

1. 代码目标：新增 infra/include/plugin/IPluginManager.h 与 infra/src/plugin/PluginManager.cpp，并在 infra/CMakeLists.txt 中注册 header/source。
2. 测试目标：
   - tests/unit/infra/plugin/PluginManagerInterfaceTest.cpp：冻结接口签名、request/result 类型与成功/失败最小行为。
   - tests/contract/smoke/PluginManagerBoundaryContractTest.cpp：验证 ResultCode/ErrorInfo 类型边界与 signature/compatibility 仅以 ref 暴露。
3. 验收命令：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_infra dasall_plugin_manager_interface_unit_test dasall_contract_plugin_manager_boundary_test
   - ctest --test-dir build-ci -N -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"
   - ctest --test-dir build-ci --output-on-failure -R "PluginManagerInterfaceCompileTest|PluginManagerBoundaryContractTest"

## 6. 风险与回退

1. validate 当前只冻结 manifest_ref 和 report_ref，而不是完整 Manifest/Signature/Compatibility 对象；待 INF-BLK-09 解阻后，应以增量对象承接而不是替换现有 ref 边界。
2. load_options 当前只冻结 profile_id/actor_ref/timeout_ms/audit_required/dry_run 五个字段；若后续需要 sandbox hint 或 rollback token，应通过新增字段评审扩展。
3. PluginManager.cpp 仅提供 not-implemented skeleton，目的是让 build/test/cmake 接线稳定，不代表进入完整生命周期实现。