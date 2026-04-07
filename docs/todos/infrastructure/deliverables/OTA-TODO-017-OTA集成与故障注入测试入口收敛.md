# OTA-TODO-017 OTA 集成与故障注入测试入口收敛

日期：2026-04-07
任务：OTA-TODO-017
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md](../DASALL_infrastructure_ota组件专项TODO.md) 将 OTA-TODO-017 定义为“注册 ota integration/failure 测试入口”，验收要求是 `tests/integration/infra/ota/` 进入顶层 discoverability，并至少覆盖 `verify_fail`、`confirm_timeout`、`rollback_fail`。
2. [docs/architecture/DASALL_infra_OTA模块详细设计.md](../../../architecture/DASALL_infra_OTA模块详细设计.md) 9.1/9.2 明确 OTA-M5 需要 `OTAWorkflowTest` 与 `OTAFailureInjectionTest` 两类入口，且 `tests/integration/infra/ota/` 是推荐落盘位置。
3. 017 的前置 `OTA-TODO-015`、`OTA-TODO-016` 与 `OTA-BLK-04` 已完成，因此当前无额外 BLOCK 需要先修复。

## 2. 研究学习结果

### 2.1 本地证据

1. `tests/integration/CMakeLists.txt` 已存在 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS` 聚合列表和 `dasall_integration_tests` 门，说明 017 的最小缺口是把 OTA 目标加入这条链路，而不是新建测试拓扑。
2. `tests/integration/infra/secret/SecretFailureInjectionTest.cpp` 表明仓库当前 failure injection 约定是落在 integration 子树内，而不是另开 `tests/stress/` 目录。
3. OTA 各骨架实现的依赖面已被冻结为轻量 fake-able adapter/provider，因此可以在 integration 层通过 mock 组合覆盖 apply/switch/confirm 与关键 failure path，而不需要真实 platform adapter。

### 2.2 外部参考

1. CMake 官方 `add_test(NAME ...)` 文档说明：CTest 测试的 discoverability 建议通过 `add_test(NAME ... COMMAND <target>)` 建立，并让 target 名解析到构建产物。
2. CMake 官方 `set_tests_properties(...)` 文档说明：测试属性必须在创建测试的目录作用域内设置；因此 OTA integration/failure 标签最稳妥的方式是在 `tests/integration/infra/ota/CMakeLists.txt` 内统一注册。

### 2.3 可落地启发

1. 017 不需要引入新的测试层；沿用仓库现有 `tests/integration/infra/<component>` 结构最稳妥。
2. OTA integration 与 failure injection 可以共用一个 OTA helper，再通过额外 `failure` 标签细分失败用例。
3. 成功闭环与失败注入拆成两个可执行文件，便于 `ctest -L ota` 和 `ctest -R` 双重筛选。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 建立 OTA integration 子目录与统一 helper | OTA 设计 9.1/9.2 | tests/integration/infra/ota/CMakeLists.txt | OTA integration 测试通过同一 helper 注册 |
| D2 | 落最小成功闭环测试 | OTA TODO 017；OTA 设计 6.8/9.1 | OTAWorkflowTest.cpp | verify -> stage -> switch -> confirm success 可二值判定 |
| D3 | 落关键 failure injection 测试 | OTA TODO 017；OTA 设计 6.9/9.2 | OTAFailureInjectionTest.cpp | verify_fail、confirm_timeout、rollback_fail 三类失败均进入自动化 |
| D4 | 接入顶层 integration discoverability | tests/integration/CMakeLists.txt | CMake 更新 + TODO 回写 | `ctest -N -L integration` 与 `ctest -N -L ota` 可发现 OTA 测试 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| OTA integration 要遵循现有 infra integration 拓扑 | 在 `tests/integration/infra` 下新增 `ota/` 子目录并 `add_subdirectory(ota)` |
| 成功闭环与 failure injection 需要独立 discoverability | 新增 `OTAWorkflowTest.cpp` 与 `OTAFailureInjectionTest.cpp` 两个目标 |
| failure injection 不另建 stress 拓扑 | 延续 secret 组件现有做法，把 failure 用例落在 `tests/integration/infra/ota/` 并打 `failure` 标签 |
| 顶层 gate 需要认识 OTA 新目标 | 在 `tests/integration/CMakeLists.txt` 增补 OTA integration 目标名 |

### 4.2 Build 三件套

1. 代码目标：新增 OTA integration/failure 测试文件与对应 CMake 注册。
2. 测试目标：验证 OTA integration 目标可被 discover，并定向执行成功闭环和失败注入场景。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_integration_tests`
   - `ctest --test-dir build-ci -N -L integration`
   - `ctest --test-dir build-ci -N -L ota`
   - `ctest --test-dir build-ci --output-on-failure -L ota`

### 4.3 D Gate

结论：PASS。

理由：

1. 017 只在 tests/integration 域新增 OTA 测试入口，不改动 production OTA 行为，也不扩张到 runtime/platform 真实适配。
2. Build 三件套和 discoverability 出口已锁定，能直接以 CTest 二值判定。

## 5. Build 落地结果

1. 新增 `tests/integration/infra/ota/CMakeLists.txt`，通过 `dasall_register_ota_integration_test(...)` 统一 OTA integration 目标的 include、link 与 `integration;ota` 标签，并给 `OTAFailureInjectionTest` 追加 `failure` 标签。
2. 新增 `tests/integration/infra/ota/OTAWorkflowTest.cpp`，在 fake trust anchor / verifier / artifact writer / boot control / health monitor 等依赖上串联 PackageVerifier、ArtifactCompatibilityEvaluator、InstallExecutor、SlotSwitchCoordinator 与 BootConfirmationMonitor，覆盖 `apply -> switch -> confirm -> success` 的最小闭环。
3. 新增 `tests/integration/infra/ota/OTAFailureInjectionTest.cpp`，覆盖：
   - `verify_fail`：PackageVerifier 在签名失败时提前阻断 apply；
   - `confirm_timeout`：BootConfirmationMonitor timeout 后调用 `mark_boot_failed` 并暴露冻结的 timeout outward code；
   - `rollback_fail`：RollbackController 在 repo pointer recovery 失败时显式返回 rollback failure。
4. 更新 `tests/integration/infra/CMakeLists.txt` 与 `tests/integration/CMakeLists.txt`，把 OTA 子目录和两个新目标纳入顶层 integration discoverability 与 `dasall_integration_tests` 聚合门。

## 6. Build 合规复核

1. 边界：本轮只新增 OTA integration/failure 测试与注册，不修改 OTA 生产代码。
2. 根因处理：解决的是 OTA integration 层“拓扑已通、组件目录缺失”的实际缺口，而不是只在 TODO 里回写“理论可测”。
3. 测试覆盖：成功闭环 1 条，失败注入至少 3 条，满足 017 的最小覆盖要求。
4. 可发现性：新增 OTA 测试既能通过 `integration` 顶层 gate 发现，也能通过 `ota` 标签独立筛出。

## 7. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_integration_tests`：通过，20/20 integration tests passed。
3. `ctest --test-dir build-ci -N -L integration`：通过，发现 20 个 integration 测试入口，包含 `OTAWorkflowTest` 与 `OTAFailureInjectionTest`。
4. `ctest --test-dir build-ci -N -L ota`：发现 21 个 OTA 标签测试入口，其中新增 integration 入口为 `OTAWorkflowTest` 与 `OTAFailureInjectionTest`。
5. `ctest --test-dir build-ci --output-on-failure -L ota`：通过，21/21 OTA 标签测试通过。

## 8. 结论

1. OTA-TODO-017 已完成，OTA 的 integration/failure 测试入口现在已纳入仓库顶层 discoverability 和 `dasall_integration_tests` 聚合门。
2. 011 在本轮开始前已完成并推送，因此本轮未重复执行 011，而是直接推进用户请求链上唯一剩余的 017。