# PLG-TODO-016 IPluginCompatibilityEngine 设计收敛

日期：2026-04-08  
任务：PLG-TODO-016  
状态：D Gate PASS / Build PASS

## 1. 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.5.2 已冻结 CompatibilityReport v1 的字段集合，6.6.1 已冻结 platform tag allow-list、host ABI 快照最小字段与 strict/non-strict 规则。
2. docs/todos/infrastructure/deliverables/PLG-BLK-01-03-INF-BLK-09-plugin对象与校验链路冻结.md 已明确 016 的 Build 目标是“compatibility engine public header + host ABI snapshot / compatibility report + matrix tests”。
3. 015 已完成 IPluginSignatureVerifier 边界冻结，当前 plugin public boundary 仍保持 ref-only validation aggregation，因此 016 不应提前改动 IPluginManager / PluginValidationPipeline 的 report 聚合面。
4. OTA 的 ArtifactCompatibilityEvaluator.h 已给出仓库当前的 compatibility gate 对象风格：输入 snapshot + 可二值判定 report，不在接口轮次直接混入 runtime 执行细节。

## 2. 外部参考

1. GNU triplet 与 SemVer 兼容规则的组合能够稳定表达平台与 ABI 版本边界；本轮据此把 platform tag allow-list、strict/non-strict 比较规则与 host snapshot 固定为 compatibility engine 的最小输入面。

## 3. Design 结论

1. 新增 IPluginCompatibilityEngine public header，并在同一头文件内冻结 `PluginHostAbiSnapshot`、`PluginDependencyMatrixSnapshot`、`PluginCompatibilityCheckRequest` 与 `CompatibilityReport`，保持 016 的原子粒度。
2. `PluginHostAbiSnapshot` 最小字段固定为 `platform_tag + abi_version + strict_mode + api_ready`；其中 `api_ready` 对应 design 中 `api_ok` 的当前阶段稳定布尔输入。
3. `PluginDependencyMatrixSnapshot` 只冻结 `required_dependency_refs` 与 `available_dependency_refs` 两个集合，不扩张到 runtime bridge 或平台资源总表；016 只需要给 `dependency_ok` 提供最小可判定输入。
4. `CompatibilityReport` 冻结为 `abi_ok/api_ok/dependency_ok/reason_codes/resolved_platform_tag/required_abi/evidence_ref` 七字段；任一检查失败时必须给出非空 `reason_codes`。
5. 本轮只冻结 compatibility engine public boundary，不接入 PluginValidationPipeline / IPluginManager aggregation；017 再统一处理 SignatureReport / CompatibilityReport 在 validation boundary 的正式接线与测试迁移。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 compatibility engine public boundary | infra/include/plugin/IPluginCompatibilityEngine.h |
| 冻结 platform tag allow-list 与 ABI 比较规则 | `is_plugin_compatibility_platform_tag_allowed()` + `plugin_abi_version_satisfies_requirement()` |
| 冻结 host ABI / dependency 输入面 | `PluginHostAbiSnapshot` + `PluginDependencyMatrixSnapshot` |
| 冻结 CompatibilityReport v1 字段与 matrix 语义 | `CompatibilityReport` |
| 验证 strict/non-strict matrix 与 boundary freeze | PluginCompatibilityEngineInterfaceCompileTest / PluginCompatibilityEngineBoundaryContractTest |

## 5. Build 三件套

1. 代码目标：新增 infra/include/plugin/IPluginCompatibilityEngine.h，并在 infra/CMakeLists.txt 中注册 public header。
2. 测试目标：新增 tests/unit/infra/plugin/PluginCompatibilityEngineInterfaceTest.cpp，覆盖 strict patch forward、strict/non-strict minor matrix、major mismatch + API/dependency fail；新增 tests/contract/smoke/PluginCompatibilityEngineBoundaryContractTest.cpp，覆盖 platform tag allow-list、dependency snapshot 去重与“无 runtime/policy 内部字段”边界。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_infra dasall_plugin_compatibility_engine_interface_unit_test dasall_contract_plugin_compatibility_engine_boundary_test
   - ctest --test-dir build-ci -N -R "PluginCompatibilityEngineInterfaceCompileTest|PluginCompatibilityEngineBoundaryContractTest"
   - ctest --test-dir build-ci --output-on-failure -R "PluginCompatibilityEngineInterfaceCompileTest|PluginCompatibilityEngineBoundaryContractTest"

## 6. 验收结果

1. `dasall_infra`、`dasall_plugin_compatibility_engine_interface_unit_test` 与 `dasall_contract_plugin_compatibility_engine_boundary_test` 全部构建通过。
2. `PluginCompatibilityEngineInterfaceCompileTest` 与 `PluginCompatibilityEngineBoundaryContractTest` 均已进入 CTest 图。
3. 两个测试 2/2 通过。

## 7. 风险与回退

1. 本轮没有把 compatibility engine 接入 PluginValidationPipeline 或 IPluginManager，避免在 016 中提前触发 validation aggregation breaking review。
2. `CompatibilityReport` 当前先作为 compatibility engine boundary 的最小输出对象存在；017 若统一抽取 report 头文件或迁移 aggregation 边界，应保持 016 已冻结接口签名不变。
3. dependency snapshot 当前只冻结 required/available refs 两个集合；若未来需要更细的平台依赖矩阵，应另起原子任务演进，而不是在 016 中隐式扩写。