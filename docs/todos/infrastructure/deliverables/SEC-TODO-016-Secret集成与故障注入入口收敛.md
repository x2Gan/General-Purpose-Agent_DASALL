# SEC-TODO-016 Secret 集成与故障注入入口收敛

日期：2026-04-04
任务：SEC-TODO-016
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-016 定义为“注册 secret integration 与故障注入入口”，验收出口是 `ctest -N` discoverability 和 `ctest -L integration` 聚合执行通过。
2. SEC-TODO-015 已把 secret 的 unit/contract 测试入口收口到统一 target 与标签；当前剩余缺口是 integration 侧仍没有 secret 子目录、目标注册和最小 failure injection 用例。
3. tests 顶层 integration 拓扑已于 2026-03-30 解阻，因此 016 的核心不再是修复全局测试框架，而是把 secret 组件自身的 integration 入口落盘到现有拓扑。

## 2. 研究学习结果

### 2.1 本地证据

1. `tests/integration/CMakeLists.txt` 已提供 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS` 聚合表，说明 016 只需补充 secret executable targets，即可纳入 top-level `dasall_integration_tests`。
2. `tests/integration/infra/CMakeLists.txt` 当前只有 audit、config、logging 子目录，说明 infra integration 子树里此前并不存在 secret 组件入口。
3. `infra/src/secret` 已存在 `SecretManagerFacade`、`SecretRotationCoordinator`、`SecretAuditBridge` 和 `MockSecretBackend` 等最小骨架，足以支撑 rotation workflow 和 failure injection 两条最小 integration 路径。

### 2.2 可落地启发

1. 新增 `tests/integration/infra/secret/` 子目录并提供局部 helper，可以让 secret integration target 命名、link 规则和 label 策略保持一致。
2. 用 `SecretRotationWorkflowTest` 验证 dual-slot rotation 后旧 handle stale、新版本可重新获取，可以覆盖 secret 主链中最关键的跨组件集成路径。
3. 用 `SecretFailureInjectionTest` 验证 backend unavailable 与 audit sink failure 的透传，可以把 failure injection 从 unit 级骨架提升到 integration discoverability 入口。

## 3. Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| secret integration 需要独立子目录与统一注册 helper | tests/integration/infra/secret/CMakeLists.txt |
| integration 需要覆盖轮换主链 | tests/integration/infra/secret/SecretRotationWorkflowTest.cpp |
| integration 需要覆盖故障注入路径 | tests/integration/infra/secret/SecretFailureInjectionTest.cpp |
| secret integration targets 需要接入顶层聚合 | tests/integration/infra/CMakeLists.txt；tests/integration/CMakeLists.txt |

## 4. Build 落地结果

1. 新增 `tests/integration/infra/secret/CMakeLists.txt`，提供 `dasall_register_secret_integration_test(...)`，统一 secret integration executable 的 include/link 规则，并附加 `integration;secret` 标签。
2. 新增 `tests/integration/infra/secret/SecretRotationWorkflowTest.cpp`，以 `MockSecretBackend + SecretManagerFacade` 验证 dual-slot rotation 后旧 `v3` handle 失效，调用方可重新获取 `v4` handle。
3. 新增 `tests/integration/infra/secret/SecretFailureInjectionTest.cpp`，验证 backend unavailable 通过 `SecretManagerFacade::get_secret()` 暴露 provider 类失败，audit sink failure 通过 `SecretAuditBridge` 暴露 `INF_E_SECRET_AUDIT_WRITE_FAIL`。
4. 更新 `tests/integration/infra/CMakeLists.txt` 与 `tests/integration/CMakeLists.txt`，把 secret 子目录和两个 executable targets 接入现有 top-level integration 聚合图。

## 5. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_integration_tests`：通过。
3. `ctest --test-dir build-ci -N`：通过；已发现 `SecretRotationWorkflowTest` 与 `SecretFailureInjectionTest`。
4. `ctest --test-dir build-ci --output-on-failure -L integration`：通过，13/13 tests passed；`secret` 标签下 2 个测试。

## 6. 结论

1. SEC-TODO-016 已把 secret 的 integration/failure injection 入口从“顶层拓扑存在但组件缺位”推进到“存在 secret 子目录、统一注册 helper、可聚合执行的最小 integration matrix”。
2. secret 组件当前已具备 unit、contract、integration 三类最小测试入口；下一步可进入 SEC-TODO-017，统一回写质量门、阻塞变化和交付证据。