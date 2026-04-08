# PLG-TODO-017 PluginReports 与聚合收敛

日期：2026-04-08  
任务：PLG-TODO-017  
状态：D Gate PASS / Build PASS

## 1. 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5.2 已冻结 SignatureReport / CompatibilityReport v1 的字段集合，6.8 已要求 validate 失败路径可返回可观测证据且不吞没错误。
2. PLG-TODO-015 与 PLG-TODO-016 已分别在 verifier / compatibility engine public boundary 中冻结最小 report 输出对象，但 IPluginManager 与 PluginValidationPipeline 仍停留在 ref-only 聚合。
3. 当前 IPluginManager.h 与 PluginValidationPipeline.h/.cpp 已稳定暴露 `signature_report_ref` / `compatibility_report_ref`，现有 unit/contract 测试也把这些 ref 当作正式 traceability boundary。
4. 本轮若直接用对象替换 ref，会引入 aggregation public boundary breaking 风险；因此 017 必须采用“shared report object + 既有 ref”双承载收口，而不是破坏既有 traceability surface。

## 2. 外部参考

1. Semantic Versioning 2.0.0 要求对公开 API 的兼容演进保持向后兼容；本轮据此选择新增 optional shared report object，并保留既有 `*_report_ref` 字段，避免把 017 做成破坏性替换。

## 3. Design 结论

1. 新增 shared public header `PluginReports.h`，统一承载 `PluginSignatureChainStatus`、`is_plugin_signature_algorithm_allowed()`、`SignatureReport` 与 `CompatibilityReport`，消除 015/016 间的 report 重复定义。
2. IPluginSignatureVerifier 与 IPluginCompatibilityEngine 改为包含 `PluginReports.h`，保持 `verify(request)->SignatureReport` 与 `check(request)->CompatibilityReport` 的接口签名不变，只做承载抽取。
3. `PluginValidationResult` 与 `PluginValidationStageResult` 新增 optional `signature_report` / `compatibility_report`；既有 `signature_report_ref` / `compatibility_report_ref` 保留，继续作为 traceable ref 边界。
4. StageResult 单次最多携带一种 report object；validation success 可以同时聚合 signature + compatibility 两种 shared report object，并与 ref 同步返回。
5. `has_traceable_refs()` 在 optional object 存在时，要求对应 ref 非空且对象本身有效；借此保证 017 不是“多带一份松散对象”，而是受 ref 约束的正式共享边界。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 抽取 shared report public header | infra/include/plugin/PluginReports.h |
| 保持 015/016 接口签名不变并复用 shared reports | infra/include/plugin/IPluginSignatureVerifier.h、infra/include/plugin/IPluginCompatibilityEngine.h |
| 在 manager 边界引入 optional object + ref 双承载 | infra/include/plugin/IPluginManager.h |
| 在 validation stage/result 中接入 shared report aggregation | infra/src/plugin/PluginValidationPipeline.h、infra/src/plugin/PluginValidationPipeline.cpp |
| 验证 shared report 与 aggregation 边界 | PluginReportsTest、PluginReportsBoundaryContractTest、PluginManagerInterfaceCompileTest、PluginValidationPipelineTest、PluginManagerBoundaryContractTest、PluginValidationPipelineBoundaryContractTest |

## 5. Build 三件套

1. 代码目标：新增 infra/include/plugin/PluginReports.h；更新 IPluginSignatureVerifier.h、IPluginCompatibilityEngine.h、IPluginManager.h、PluginValidationPipeline.h/.cpp，并在 infra/CMakeLists.txt 注册 shared report public header。
2. 测试目标：新增 tests/unit/infra/plugin/PluginReportsTest.cpp 与 tests/contract/smoke/PluginReportsBoundaryContractTest.cpp；同步更新 PluginManagerInterfaceTest、PluginValidationPipelineTest、PluginManagerBoundaryContractTest、PluginValidationPipelineBoundaryContractTest，验证 optional object + ref 双承载聚合边界。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_infra dasall_plugin_signature_verifier_interface_unit_test dasall_plugin_compatibility_engine_interface_unit_test dasall_plugin_reports_unit_test dasall_plugin_manager_interface_unit_test dasall_plugin_validation_pipeline_unit_test dasall_contract_plugin_signature_verifier_boundary_test dasall_contract_plugin_compatibility_engine_boundary_test dasall_contract_plugin_reports_boundary_test dasall_contract_plugin_manager_boundary_test dasall_contract_plugin_validation_pipeline_boundary_test
   - ctest --test-dir build-ci -N -R "Plugin(SignatureVerifierInterfaceCompileTest|CompatibilityEngineInterfaceCompileTest|ReportsTest|ManagerInterfaceCompileTest|ValidationPipelineTest|SignatureVerifierBoundaryContractTest|CompatibilityEngineBoundaryContractTest|ReportsBoundaryContractTest|ManagerBoundaryContractTest|ValidationPipelineBoundaryContractTest)"
   - ctest --test-dir build-ci --output-on-failure -R "Plugin(SignatureVerifierInterfaceCompileTest|CompatibilityEngineInterfaceCompileTest|ReportsTest|ManagerInterfaceCompileTest|ValidationPipelineTest|SignatureVerifierBoundaryContractTest|CompatibilityEngineBoundaryContractTest|ReportsBoundaryContractTest|ManagerBoundaryContractTest|ValidationPipelineBoundaryContractTest)"

## 6. 验收结果

1. `dasall_infra` 与 017 涉及的 10 个 interface/unit/contract targets 全部构建通过。
2. `PluginReportsTest`、`PluginReportsBoundaryContractTest`、`PluginManagerInterfaceCompileTest`、`PluginValidationPipelineTest`、`PluginManagerBoundaryContractTest`、`PluginValidationPipelineBoundaryContractTest` 等 10 个用例均已进入 CTest 图。
3. 10/10 tests passed，shared report header 与 optional aggregation 边界验证通过。

## 7. 风险与回退

1. 本轮采用 optional object + ref 双承载，是为了保持 015/016/005 已冻结边界向后兼容；后续若要移除 ref-only 字段，必须另起 breaking review 任务。
2. 017 只收口 validation aggregation，不把 shared reports 扩展到 load/unload/runtime bridge 结果；若未来需要更广泛的 report 复用，应另起任务评审。
3. shared report 头当前只冻结对象与 helper，不引入持久化/export schema；若审计或远程导出需要稳定序列化格式，应另起原子任务完成。