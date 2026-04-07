# OTA-TODO-016 OTA 测试入口注册收敛

日期：2026-04-07
任务：OTA-TODO-016
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-016 定义为“注册 ota 的 unit 与 contract 测试入口”，完成判定是新增 OTA 测试可被 `ctest -N` 发现，并在 `unit/contract` 标签下执行。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 9.1/9.2 要求 OTA 测试矩阵覆盖对象边界、核心链路骨架和错误映射，并通过统一 gate 被发现与执行。
3. 001~014 已逐步新增 OTA unit 与 contract 用例，但 tests 侧仍保留“每个用例各自注册、接口测试标签不一致、contract 缺少 ota 组件标签”的收口缺口。

## 2. 研究学习结果

### 2.1 本地证据

1. `tests/unit/CMakeLists.txt` 当前直接在总清单里逐行列出 OTA 目标，不利于后续 017 继续按组件增量扩展。
2. `tests/unit/infra/CMakeLists.txt` 中 OTA 测试块大量重复 `add_executable -> target_link_libraries -> add_test -> set_tests_properties` 模板，且 interface compile tests 仍只有 `unit` 标签，没有 `ota` 组件标签。
3. `tests/contract/CMakeLists.txt` 中 OTA contract tests 复用通用 smoke 注册函数，能进入 contract gate，但缺少统一的 `ota` 组件标签，组件级 discoverability 不完整。

### 2.2 外部参考

1. CMake 官方 `add_test(NAME ...)` 文档说明，测试属性应围绕测试创建点设置；`set_tests_properties(...)` 也要求在测试创建所在目录作用域内设置属性。这意味着把 OTA 测试的注册与标签收敛到专属 helper，是最稳妥的 discoverability 方案。

### 2.3 可落地启发

1. OTA unit tests 需要组件级聚合列表，避免总清单继续堆积离散目标名。
2. OTA unit/interface tests 应统一走同一 helper，并把 `unit;ota` 作为基础标签，保证 `ctest -L ota` 能直接筛出组件子集。
3. OTA contract smoke tests 应增加组件级 helper，把 `contract;smoke;ota` 作为默认标签，而不改变既有 smoke gate 语义。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 建立 OTA unit 目标聚合列表 | OTA TODO 016 | tests/unit/CMakeLists.txt | OTA unit 目标通过单一列表注入总聚合目标 |
| D2 | 统一 OTA unit/interface 注册模板 | OTA 设计 9.1 | tests/unit/infra/CMakeLists.txt | 所有 OTA unit tests 通过同一 helper 注册，并带 `unit;ota` 标签 |
| D3 | 统一 OTA contract smoke 注册模板 | OTA 设计 9.2 | tests/contract/CMakeLists.txt | 所有 OTA contract 边界测试带 `contract;smoke;ota` 标签 |
| D4 | 锁定 discoverability 验证出口 | OTA TODO 016 | 本交付物 + TODO 回写 | `ctest -N -L ota` 与 `ctest -L ota` 可二值判定 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| OTA unit 目标需要组件级聚合入口 | 在 `tests/unit/CMakeLists.txt` 新增 `DASALL_OTA_UNIT_TEST_EXECUTABLE_TARGETS` |
| OTA unit/interface 注册应模板化且标签一致 | 在 `tests/unit/infra/CMakeLists.txt` 新增 `dasall_register_ota_unit_test(...)` |
| OTA contract smoke 需要组件标签 | 在 `tests/contract/CMakeLists.txt` 新增 `dasall_register_ota_contract_test(...)` |
| discoverability 需要组件级筛选出口 | 使用 `ctest --test-dir build-ci -N -L ota` 与 `ctest --test-dir build-ci --output-on-failure -L ota` |

### 4.2 Build 三件套

1. 代码目标：收敛 OTA unit/contract 测试注册 helper 与组件级聚合列表。
2. 测试目标：保持现有 unit/contract 矩阵通过，并验证 OTA 子集可被独立发现与执行。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci -N -L ota`
   - `ctest --test-dir build-ci --output-on-failure -L ota`

### 4.3 D Gate

结论：PASS。

理由：

1. 016 只调整测试注册与标签，不改变已有测试断言和 OTA 运行时行为。
2. Build 三件套已锁定，discoverability 与执行出口都可以通过现有 CTest 命令直接验证。

## 5. Build 落地结果

1. 在 `tests/unit/CMakeLists.txt` 新增 `DASALL_OTA_UNIT_TEST_EXECUTABLE_TARGETS`，把 14 个 OTA unit/interface 目标收敛为组件级列表后，再注入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`。
2. 在 `tests/unit/infra/CMakeLists.txt` 新增 `dasall_register_ota_unit_test(...)`，自动处理 OTA 私有 include 目录、公共 link 依赖与 `unit;ota` 标签。
3. 通过 OTA unit helper 统一注册 `OTATypesCompileTest`、`OTAInterfaceCompileTest`、核心链路骨架 tests，以及 3 个 OTA interface compile tests，使先前仅带 `unit` 标签的 OTA interface tests 也纳入 `ota` 组件 discoverability。
4. 在 `tests/contract/CMakeLists.txt` 新增 `dasall_register_ota_contract_test(...)`，统一注册 OTA type/manager/package verifier/install executor/boot control adapter 五个 contract smoke tests，并赋予 `contract;smoke;ota` 标签。

## 6. Build 合规复核

1. 边界：本轮不新增测试语义，只收敛 OTA 测试入口与标签；已有正负例断言直接复用各测试文件中的现有覆盖。
2. 根因处理：解决的是 OTA 测试“已存在但注册模板分散、标签不一致”的维护问题，而不是继续手工复制新的 `add_test` 片段。
3. 可发现性：OTA interface unit tests 与 OTA contract smoke tests 现在都可通过 `ota` 标签统一筛出。
4. 兼容性：全局 `unit` / `contract` gate 语义不变，只新增更细粒度的组件标签和聚合列表。

## 7. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过。
3. `ctest --test-dir build-ci -N -L ota`：发现 19 个 OTA 标签测试入口。
4. `ctest --test-dir build-ci --output-on-failure -L ota`：通过，19/19 tests passed。

## 8. 结论

1. OTA-TODO-016 已完成，OTA 的 unit 与 contract 测试入口现在拥有统一 helper、统一聚合列表和统一 `ota` 标签。
2. 016 完成后，OTA 组件级 discoverability 已具备独立出口，后续 017 可以在不重复整理注册结构的前提下直接补 integration/failure 用例。