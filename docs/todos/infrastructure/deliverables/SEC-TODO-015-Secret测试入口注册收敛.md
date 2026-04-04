# SEC-TODO-015 Secret 测试入口注册收敛

日期：2026-04-04
任务：SEC-TODO-015
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-015 定义为“注册 secret unit 与 contract 测试入口”，验收出口是 `dasall_unit_tests`、`dasall_contract_tests` 以及 `ctest -L unit/contract` 全量通过。
2. SEC-TODO-006~014 已把 secret 的实现、单测和 CMake 基线逐步落盘，因此 015 的核心不再是新增业务代码，而是把 secret 相关 unit/contract 测试入口集中收口为清晰矩阵。
3. 当前 secret implementation tests 已大多带有 `unit;secret` 标签，但若干接口/类型 unit tests 仍只有 `unit` 标签；secret contract tests 也仍只挂在 `contract;smoke` 下，缺少域内聚合标签。

## 2. 研究学习结果

### 2.1 本地证据

1. `tests/CMakeLists.txt` 已提供 `dasall_unit_tests` 与 `dasall_contract_tests` 聚合 target，说明 015 只需把 secret 相关 executable targets 和 CTest labels 收口，不需要再发明新的 gate 机制。
2. `tests/unit/CMakeLists.txt` 当前直接把 secret unit targets 散列在总目标列表中，不利于审查“类型、接口、访问、lease、轮换、审计、健康”矩阵是否完整。
3. `tests/contract/CMakeLists.txt` 已存在 logging/audit 专用注册 helper，因此为 secret 增加专用 helper 和统一标签是最小、最一致的收口方式。

### 2.2 可落地启发

1. 用 `DASALL_SECRET_UNIT_TEST_EXECUTABLE_TARGETS` 单列 secret unit matrix，可以把 top-level `dasall_unit_tests` 聚合表从“离散目标”收敛为“按域分组”。
2. 用 `dasall_register_secret_contract_test(...)` 统一给 secret contract tests 附加 `secret` 标签，可以在不改变现有 smoke 语义的前提下获得域内过滤能力。
3. 把 secret interface/type unit tests 的 label 从 `unit` 升级到 `unit;secret`，可以让 label-based gate 与 TODO 的 secret 测试矩阵保持一致。

## 3. Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| secret unit tests 需要集中分组 | tests/unit/CMakeLists.txt 中新增 `DASALL_SECRET_UNIT_TEST_EXECUTABLE_TARGETS` |
| secret interface/type unit tests 需要统一 secret 标签 | tests/unit/infra/CMakeLists.txt |
| secret contract tests 需要统一 secret 标签 | tests/contract/CMakeLists.txt 中新增 `dasall_register_secret_contract_test` |

## 4. Build 落地结果

1. 更新 tests/unit/CMakeLists.txt，新增 `DASALL_SECRET_UNIT_TEST_EXECUTABLE_TARGETS`，把 file/mock/backend interface/errors/manager/lease/rotation/audit/health 等 secret unit targets 聚合到顶层 unit matrix。
2. 更新 tests/unit/infra/CMakeLists.txt，为 `SecretBackendInterfaceTest`、`SecretErrorsTest`、`SecretHealthSourceInterfaceTest`、`SecretManagerInterfaceTest`、`SecureBufferTest`、`SecretTypesTest` 补齐 `secret` 标签。
3. 更新 tests/contract/CMakeLists.txt，新增 `dasall_register_secret_contract_test(...)`，并将 5 个 secret contract tests 统一切到 `contract;smoke;secret` 标签。

## 5. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过；unit 119/119 通过，contract 133/133 通过。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，119/119 tests passed。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，133/133 tests passed。

## 6. 结论

1. SEC-TODO-015 已把 secret 的 unit/contract 测试入口从“可运行但分散”推进到“按域聚合、统一标签、可直接 gate”的收口状态。
2. secret 相关测试现在可通过 `secret` 标签和 top-level aggregated targets 双路径过滤，为后续 016 的 integration/failure injection 注册和 017 的最终 gate 回写提供稳定入口。