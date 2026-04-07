# PLG-TODO-005 plugin 校验管线骨架收敛

日期：2026-04-07
任务：PLG-TODO-005
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 将 PLG-TODO-005 定义为“建立 PluginValidationPipeline 骨架与三检流程”，完成判定是 policy -> signature -> compatibility 三段失败枝条可判定、拒绝结果可溯源且编译通过。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.3/6.7/6.8 要求 plugin 在 load 之前完成准入判定、签名校验与兼容性检查，并把失败原因以 ref 边界回传，而不是提前固化完整 SignatureReport/CompatibilityReport 对象。
3. 当前仓库已完成 PLG-TODO-001/002/003/004/007 与 INF-TODO-017，具备 PluginValidationRequest、PluginPolicyRequest、PolicySnapshot、PolicyDecisionRef 和 plugin 私有错误码域；受 INF-BLK-09 影响的完整 manifest/signature/ABI 对象仍不能越界落盘。

## 2. 研究学习结果

### 2.1 本地证据

1. infra/src/plugin/PluginManager.cpp 的 validate() 仍返回统一 skeleton failure，说明 005 的缺口不是接口不存在，而是没有把 policy/signature/compat 三段校验收敛到单一出口。
2. infra/include/plugin/IPluginManager.h 的 PluginValidationRequest 只冻结了 plugin_id、manifest_ref、package_ref、profile_id 四个最小输入；而 infra/include/plugin/IPluginPolicyGate.h 的 PluginPolicyRequest 需要 governance-ready 的 PluginDescriptor，这构成了 005 的同轮 context blocker。
3. infra/CMakeLists.txt 在 008 轮新增了 DASALL_INFRA_PLUGIN_SOURCES 变量，但该变量定义晚于 DASALL_INFRA_CORE_SOURCES 的求值时机，导致新落的 plugin 源文件不会自动进入 dasall_infra，这同样是 005 的构建 blocker。

### 2.2 外部参考

1. PF4J 文档指出，插件只有在元数据与依赖约束满足后才应被插件管理器视为可加载对象；这支持本轮把 policy/signature/compat 明确建模为 load 前串行校验关卡，而不是把失败推迟到 lifecycle 阶段。
2. CMake 官方 target_sources 文档说明，对同一 target 的 repeated calls 会按调用顺序追加 sources；这直接支持本轮把 plugin sources 显式追加到 dasall_infra 的 target_sources，而不是依赖提前展开的聚合变量。

### 2.3 可落地启发

1. 005 不需要等待完整 SignatureReport/CompatibilityReport 对象冻结，只要继续维持 report_ref 边界，就可以先把三检顺序、失败枝条和聚合出口稳定下来。
2. PluginValidationRequest 与 PluginPolicyRequest 之间的缺口，可以通过 pipeline 内部合成最小 governance-ready descriptor 来修复，而不必回头打破 003/004 已冻结的公共接口。
3. plugin 构建入口的真实接线必须在 target_sources 层显式完成，否则新 pipeline 源文件只会存在于列表变量里，不会进入静态库链接图。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 PluginValidationPipeline 的最小职责边界 | plugin 详细设计 6.3/6.7/6.8 | infra/src/plugin/PluginValidationPipeline.h/.cpp | 仅收敛 policy/signature/compat 骨架，不扩写完整报告对象 |
| D2 | 修复 validation request 到 policy request 的边界缺口 | IPluginManager.h / IPluginPolicyGate.h | pipeline 内部 request 归一化逻辑 | PluginPolicyRequest 可从已冻结对象稳定派生 |
| D3 | 修复 plugin source 未真正进入 dasall_infra 的构建缺口 | infra/CMakeLists.txt 现状 | infra/CMakeLists.txt | pipeline 与 manager 源文件参与库构建 |
| D4 | 锁定 005 的 Build 三件套 | plugin 专项 TODO 005 | 本交付物 + TODO 回写 | 有代码目标、测试目标、验收命令 |

## 4. D Gate 结论

### 4.1 Blocker 修复与 Design -> Build 映射

阻塞结论：

1. PluginValidationRequest 不携带完整 governance-ready descriptor，而 PluginPolicyRequest 又要求 descriptor/status/manifest/profile 同时有效，若不先补边界归一化，pipeline 无法真正驱动 IPluginPolicyGate。
2. 005 任务行原始验收命令只构建 dasall_infra，不会显式构建新增 unit/contract 目标，无法证明 pipeline 测试出口可执行。
3. DASALL_INFRA_PLUGIN_SOURCES 在 008 轮后仍未真正进入 target_sources 的最终求值结果，新增 pipeline 源文件会在单测链接阶段暴露为 undefined reference。

最小 blocker-fix：

1. 在 PluginValidationPipeline 内部把 PluginValidationRequest 映射为最小 PluginPolicyRequest：descriptor 继续复用已冻结的 PluginDescriptor，version/abi 用占位值承接未解阻对象，source 复用 package_ref，status 固定为 Discovered。
2. 在 infra/CMakeLists.txt 中把 `${DASALL_INFRA_PLUGIN_SOURCES}` 显式追加到 dasall_infra 的 target_sources，并把 PluginValidationPipeline.h 注册为 private header，确保 plugin 私有实现真实参与构建图。
3. 将 005 的验收命令升级为显式构建 `dasall_plugin_validation_pipeline_unit_test` 与 `dasall_contract_plugin_validation_pipeline_boundary_test`，再定向执行对应 ctest 子集。

Design -> Build 映射：

| Design 结论 | Build 落地 |
|---|---|
| validation 只负责校验串行编排 | infra/src/plugin/PluginValidationPipeline.h/.cpp |
| manager.validate 应接入统一校验出口 | infra/src/plugin/PluginManager.cpp |
| 失败阶段仍只暴露 ref，不落完整 report 对象 | PluginValidationResult 继续复用 signature_report_ref / compatibility_report_ref |
| pipeline 需要可测的失败枝条 | tests/unit/infra/plugin/PluginValidationPipelineTest.cpp |
| report 聚合语义需要 contract 边界校验 | tests/contract/smoke/PluginValidationPipelineBoundaryContractTest.cpp |

### 4.2 Build 三件套

1. 代码目标：infra/src/plugin/PluginValidationPipeline.h、infra/src/plugin/PluginValidationPipeline.cpp、infra/src/plugin/PluginManager.cpp、infra/CMakeLists.txt。
2. 测试目标：
   - tests/unit/infra/plugin/PluginValidationPipelineTest.cpp
   - tests/contract/smoke/PluginValidationPipelineBoundaryContractTest.cpp
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_infra dasall_plugin_validation_pipeline_unit_test dasall_contract_plugin_validation_pipeline_boundary_test
   - ctest --test-dir build-ci --output-on-failure -R "PluginValidationPipelineTest|PluginValidationPipelineBoundaryContractTest"

### 4.3 D Gate

结论：PASS。

理由：

1. 005 的改动严格停留在 validation pipeline、manager validate 接线、构建入口修复和定向测试，不提前进入 runtime bridge、实际签名链验证或 ABI 规则冻结。
2. 三检失败枝条、report_ref 聚合边界和构建接线问题都已经有明确的二值验证出口。

## 5. Build 落地结果

1. 新增 infra/src/plugin/PluginValidationPipeline.h 与 PluginValidationPipeline.cpp，提供 policy -> signature -> compatibility 的串行骨架、可注入 stage callback，以及按阶段返回 traceable failure/success 的统一出口。
2. 更新 infra/src/plugin/PluginManager.cpp，使 validate() 从统一 skeleton message 改为委托 PluginValidationPipeline；当 policy 依赖未接线时，会返回显式的 validation failure，而不是模糊的 not implemented 文案。
3. 更新 infra/CMakeLists.txt，把 `${DASALL_INFRA_PLUGIN_SOURCES}` 显式追加到 dasall_infra 的 target_sources，并注册 PluginValidationPipeline.h 为 plugin 私有头，修复 plugin 源文件此前未真正进入静态库构建图的问题。
4. 更新 tests/unit/infra/plugin/CMakeLists.txt 与 tests/contract/plugin/CMakeLists.txt，为 plugin 测试统一追加 `${CMAKE_SOURCE_DIR}/infra/src` 私有头路径，并注册新的 pipeline unit/contract 用例。
5. 新增 tests/unit/infra/plugin/PluginValidationPipelineTest.cpp，覆盖 policy deny、signature fail、compatibility fail 和三段全通过四类路径。
6. 新增 tests/contract/smoke/PluginValidationPipelineBoundaryContractTest.cpp，验证 stage failure 仍只暴露 report_ref 且 ResultCode/ErrorInfo 保持在 contracts 边界内。

## 6. Build 合规复核

1. 边界：本轮只补 validation 流程骨架与必要构建修复，不提前定义 PluginManifest、IPluginSignatureVerifier、IPluginCompatibilityEngine、SignatureReport、CompatibilityReport。
2. 根因处理：修复的是 validation 无统一出口与 plugin 源文件未真正接入 dasall_infra 的根因，而不是仅在 PluginManager 里追加另一条静态错误分支。
3. 测试出口：unit 验证三类失败枝条和一类成功聚合；contract 验证 report 仍为 ref-only 边界，未扩张共享语义。
4. 兼容性：IPluginManager / IPluginPolicyGate 的公共签名未变；pipeline 通过私有实现与内部占位 descriptor 承接未解阻对象，后续 014/015/016 只需替换 stage 实现而不需要 breaking change。

## 7. 验证结果

1. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
2. cmake --build build-ci --target dasall_infra dasall_plugin_validation_pipeline_unit_test dasall_contract_plugin_validation_pipeline_boundary_test：通过。
3. ctest --test-dir build-ci --output-on-failure -R "PluginValidationPipelineTest|PluginValidationPipelineBoundaryContractTest"：通过，2/2 tests passed。

## 8. 结论

1. PLG-TODO-005 已完成，PluginValidationPipeline 现已把 policy、signature、compatibility 三段校验收敛到统一出口，且拒绝结果可按阶段稳定判定。
2. 本轮同时修复了 plugin 源文件未真正进入 dasall_infra 的构建缺口，为后续 006 的审计适配和 011 的 lifecycle 骨架提供了真实可链接的 plugin 私有实现基线。