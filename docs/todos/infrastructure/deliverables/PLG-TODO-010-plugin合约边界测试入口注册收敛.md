# PLG-TODO-010 plugin 合约边界测试入口注册收敛

日期：2026-04-07
任务：PLG-TODO-010
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 将 PLG-TODO-010 定义为“注册 tests/contract/infra/plugin 合约边界测试入口”，完成判定是 plugin contract 用例可被 contract 视图发现并执行。
2. docs/architecture/DASALL_Engineering_Blueprint.md 4.3 与 plugin 模块详细设计 6.5/9.1 要求 plugin 只消费既有 contracts 语义，边界测试需要稳定阻断标识字段、错误码和 policy/manager 引用越权。
3. 当前仓库已有五个 plugin contract smoke 用例，但它们仍直接注册在 tests/contract/CMakeLists.txt 主文件中，尚未形成 plugin 专属 helper 和组件级入口。

## 2. 研究学习结果

### 2.1 本地证据

1. tests/contract/smoke 已存在 PluginDescriptorBoundaryContractTest、PluginCatalogBoundaryContractTest、PluginErrorCodeBoundaryContractTest、PluginManagerBoundaryContractTest、PluginPolicyGateBoundaryContractTest 五个用例文件。
2. tests/contract/CMakeLists.txt 已为 logging/audit/secret/metrics/tracing/ota 提供组件级 helper，但 plugin 仍沿用最原始的通用注册片段，导致入口风格不对称。
3. 009 已把 unit 入口收口到 tests/unit/infra/plugin/CMakeLists.txt；010 若不采用类似的组件级收口方式，Phase 3 的 unit/contract 对称入口仍不完整。

### 2.2 外部参考

1. CMake 官方 add_subdirectory 文档说明，子目录会立即加入当前构建图；这适合把 plugin contract 注册从主文件中剥离到 tests/contract/plugin/CMakeLists.txt。
2. CMake 官方 add_test / set_tests_properties 文档说明，测试应在其创建目录中设置属性；本轮据此通过 plugin 子目录 helper 统一赋予 `contract;smoke;plugin` 标签，确保组件级 discoverability。

### 2.3 可落地启发

1. 010 不需要改动 contract 用例源码，只需要重组注册入口、helper 和标签。
2. plugin contract helper 应复用现有 dasall_register_contract_test，而不是重新发明第二套 contract target 构建逻辑。
3. 010 完成后，Phase 3 的 build/unit/contract 三个入口就能全部以 plugin 组件级路径维护。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 将 plugin contract 注册逻辑下沉到子目录 | tests/contract 现状 | tests/contract/plugin/CMakeLists.txt | plugin contract 用例在子目录内完成注册 |
| D2 | 为 plugin contract 统一 helper 与标签 | tests/contract/CMakeLists.txt helper 风格 | plugin/CMakeLists.txt | 五个 plugin contract 用例都带 contract;smoke;plugin 标签 |
| D3 | 锁定 010 的 contract discoverability 验证链路 | plugin TODO 010 | 本交付物 + TODO 回写 | 有代码目标、测试目标、验收命令 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| plugin contract 入口应从主文件剥离 | 新增 tests/contract/plugin/CMakeLists.txt |
| 复用现有 contract 构建机制 | plugin helper 内继续调用 dasall_register_contract_test |
| plugin contract 需要组件级 discoverability | 五个 plugin contract 用例统一标注 contract;smoke;plugin |
| 主文件只负责挂接子目录 | tests/contract/CMakeLists.txt 改为 add_subdirectory(plugin) |

### 4.2 Build 三件套

1. 代码目标：新增 tests/contract/plugin/CMakeLists.txt，并更新 tests/contract/CMakeLists.txt。
2. 测试目标：五个 plugin contract 用例可被 contract 视图发现，且定向 `-R Plugin` 与 `-L plugin` 子集都可执行。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_contract_plugin_descriptor_boundary_test dasall_contract_plugin_catalog_boundary_test dasall_contract_plugin_error_code_boundary_test dasall_contract_plugin_manager_boundary_test dasall_contract_plugin_policy_gate_boundary_test
   - ctest --test-dir build-ci -N -L contract | grep -i Plugin
   - ctest --test-dir build-ci --output-on-failure -L contract -R "Plugin"

### 4.3 D Gate

结论：PASS。

理由：

1. 010 只调整 plugin contract 入口与标签，不修改 contract 测试语义或 infra/plugin 生产代码。
2. Build 三件套已锁定，contract discoverability 与可执行性都具备清晰的二值验证出口。

## 5. Build 落地结果

1. 新增 tests/contract/plugin/CMakeLists.txt，提供 dasall_register_plugin_contract_test(...) helper，并在子目录内注册五个 plugin contract 用例。
2. 更新 tests/contract/CMakeLists.txt，以 add_subdirectory(plugin) 替换原先主文件内联的五段 plugin contract 注册与 include/link 片段。
3. 在 plugin helper 中复用 dasall_register_contract_test，并统一为五个 plugin contract 用例补上 `contract;smoke;plugin` 标签。
4. 在 plugin 专项 TODO 中将 PLG-TODO-010 回写为 Done，并补充 010 的设计/构建证据。

## 6. Build 合规复核

1. 边界：本轮只收敛 contract 注册入口和标签，不改五个 plugin contract 用例源码。
2. 正负例：复用既有五个 plugin contract 用例的正负例断言，本轮不重写 contract 语义检查。
3. 测试发现性：同时验证 `ctest -N -L contract` 的发现性和 `ctest -L contract -R Plugin` 的执行结果，确保入口可发现且可运行。
4. 兼容性：继续复用现有 contract 聚合目标与 dasall_register_contract_test，不引入第二套构建机制。

## 7. 验证结果

1. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
2. cmake --build build-ci --target dasall_contract_plugin_descriptor_boundary_test dasall_contract_plugin_catalog_boundary_test dasall_contract_plugin_error_code_boundary_test dasall_contract_plugin_manager_boundary_test dasall_contract_plugin_policy_gate_boundary_test：通过。
3. ctest --test-dir build-ci -N -L contract | grep -i Plugin：通过，可发现 5 个 plugin contract 入口。
4. ctest --test-dir build-ci --output-on-failure -L contract -R "Plugin"：通过，5/5 tests passed。

## 8. 结论

1. PLG-TODO-010 已完成，plugin contract 边界测试现在通过 tests/contract/plugin/CMakeLists.txt 统一注册，并带有稳定的 plugin 组件标签。
2. 010 完成后，plugin Phase 3 的 build、unit、contract 三类入口都已按组件级路径收口，可直接进入后续 Phase 4 流程骨架任务。