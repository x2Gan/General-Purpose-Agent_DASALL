# PLG-TODO-009 plugin 单元测试入口注册收敛

日期：2026-04-07
任务：PLG-TODO-009
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 将 PLG-TODO-009 定义为“注册 tests/unit/infra/plugin 单元测试入口”，完成判定是 plugin 单测可被 ctest 的 unit 视图发现并执行。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 9.1 已冻结 plugin 的最小单测矩阵，覆盖 PluginDescriptor、PluginCatalog、PluginErrorCode、IPluginManager、IPluginPolicyGate 五类对象/接口。
3. 当前仓库已有 tests/unit/infra/plugin/*.cpp 五个用例文件，但它们仍直接在 tests/unit/infra/CMakeLists.txt 中注册，没有形成 plugin 子目录级入口，也没有 plugin 专属聚合列表。

## 2. 研究学习结果

### 2.1 本地证据

1. tests/unit/infra/plugin 目录已经存在五个 plugin 测试源文件，说明 009 的根因不是缺测试，而是注册入口还未下沉到组件子目录。
2. tests/unit/infra/CMakeLists.txt 当前把 plugin 用例与 health/watchdog 等其他组件注册逻辑混排，后续新增 plugin 单测仍需在父级大文件中手工补点，不符合 009 的“目录与入口注册”目标。
3. tests/unit/CMakeLists.txt 当前直接枚举五个 plugin 可执行目标；若不先收敛 plugin 子目录输出列表，就无法让顶层 unit 聚合真正消费组件级入口。

### 2.2 外部参考

1. CMake 官方 add_subdirectory 文档说明，子目录会在当前构建图中立即处理；这适合把 tests/unit/infra/plugin 的注册逻辑收敛到独立 CMakeLists，再由父级统一纳入构建。
2. CMake 官方 add_test 和 set_tests_properties 文档说明，测试应在创建目录作用域内注册并设置标签；本轮据此把 plugin unit 用例统一标注为 unit;plugin，确保 discoverability 与组件过滤一致。

### 2.3 可落地启发

1. 009 不需要改动测试语义或断言，只需要重组注册入口、聚合列表与标签。
2. plugin 子目录 CMake 应输出组件级 executable target list，供 tests/unit/CMakeLists.txt 聚合，而不是继续在顶层硬编码五个 plugin 目标名。
3. 009 完成后，010 可以沿用同样的“组件级 helper + discoverability 标签”思路收口 contract 入口。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 将 plugin unit 注册逻辑下沉到子目录 | tests/unit/infra/plugin 现状 | tests/unit/infra/plugin/CMakeLists.txt | plugin 用例在子目录内完成 add_executable/add_test |
| D2 | 让顶层 unit 聚合消费组件级列表 | tests/unit/CMakeLists.txt | tests/unit/CMakeLists.txt | 不再硬编码五个 plugin 目标名 |
| D3 | 锁定 009 的 discoverability 验证链路 | plugin TODO 009 | 本交付物 + TODO 回写 | 有代码目标、测试目标、验收命令 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| plugin unit 入口应收敛到子目录 | 新增 tests/unit/infra/plugin/CMakeLists.txt |
| 父级 infra/unit 只负责挂接子目录 | tests/unit/infra/CMakeLists.txt 改为 add_subdirectory(plugin) |
| 顶层 unit 聚合应消费组件级 target list | tests/unit/CMakeLists.txt 使用 DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS |
| 单测应具备组件级 discoverability | plugin unit 用例统一标注 unit;plugin |

### 4.2 Build 三件套

1. 代码目标：新增 tests/unit/infra/plugin/CMakeLists.txt，并更新 tests/unit/infra/CMakeLists.txt 与 tests/unit/CMakeLists.txt。
2. 测试目标：五个 plugin unit 用例可被 ctest 的 unit 视图发现，且 plugin 标签子集可直接执行。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_plugin_descriptor_unit_test dasall_plugin_catalog_unit_test dasall_plugin_error_code_unit_test dasall_plugin_manager_interface_unit_test dasall_plugin_policy_gate_interface_unit_test
   - ctest --test-dir build-ci -N -L unit | grep -i plugin
   - ctest --test-dir build-ci --output-on-failure -L plugin

### 4.3 D Gate

结论：PASS。

理由：

1. 009 仅调整 unit 注册入口与聚合列表，不修改测试断言或 plugin 生产代码。
2. Build 三件套已锁定，discoverability 与可执行性都能以二值结果验证。

## 5. Build 落地结果

1. 新增 tests/unit/infra/plugin/CMakeLists.txt，提供 dasall_register_plugin_unit_test(...) helper，并在子目录内注册五个 plugin unit 用例。
2. 更新 tests/unit/infra/CMakeLists.txt，用 add_subdirectory(plugin) 替换原先散落在父级的五段 plugin test 注册代码，并把 DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS 向上导出。
3. 更新 tests/unit/CMakeLists.txt，用 ${DASALL_PLUGIN_UNIT_TEST_EXECUTABLE_TARGETS} 替代硬编码的五个 plugin unit target 名称，使顶层聚合消费组件级列表。
4. 在 plugin 专项 TODO 中将 PLG-TODO-009 回写为 Done，并补充 009 的设计/构建证据。

## 6. Build 合规复核

1. 边界：本轮只移动注册入口与聚合列表，不改 plugin 单测源码和断言语义。
2. 正负例：复用既有五个 plugin 单测的正负例覆盖，不在 009 重写测试逻辑。
3. 测试发现性：除 unit 视图发现外，额外用 plugin 标签子集执行，验证新入口的 discoverability 与可运行性。
4. 兼容性：顶层 dasall_unit_tests 仍通过 target list 聚合，不改变现有 unit 总目标语义。

## 7. 验证结果

1. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
2. cmake --build build-ci --target dasall_plugin_descriptor_unit_test dasall_plugin_catalog_unit_test dasall_plugin_error_code_unit_test dasall_plugin_manager_interface_unit_test dasall_plugin_policy_gate_interface_unit_test：通过。
3. ctest --test-dir build-ci -N -L unit | grep -i plugin：通过，可发现 5 个 plugin 单测入口。
4. ctest --test-dir build-ci --output-on-failure -L plugin：通过，5/5 tests passed。

## 8. 结论

1. PLG-TODO-009 已完成，plugin unit 测试现在通过 tests/unit/infra/plugin/CMakeLists.txt 统一注册，并以组件级列表接入顶层 unit 聚合。
2. 009 完成后，010 可以继续沿用 helper + 标签的模式，把 plugin contract 入口从 tests/contract/CMakeLists.txt 收敛出来。