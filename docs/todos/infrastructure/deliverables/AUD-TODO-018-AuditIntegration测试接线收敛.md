# AUD-TODO-018 Audit Integration 测试接线收敛

日期：2026-04-03  
任务：AUD-TODO-018  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 将 `AUD-TODO-018` 定义为“注册 audit integration 测试入口”，完成标准是 `InfraAuditHealthIntegrationTest` 完成目录、标签与顶层 integration 聚合收口。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 8.1/9.1 给出 audit integration 目录建议与集成测试矩阵，要求 health/metrics 协同用例具备可发现、可执行的稳定入口。
3. `AUD-TODO-014` 与 `AUD-TODO-015` 已完成，因此当前用例语义已经具备，018 的工作只剩 topology/registration 收口。

## 2. 研究学习结果

### 2.1 本地证据

1. [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) 在本轮前仍直接在根级注册 `InfraAuditHealthIntegrationTest`，导致 audit integration 与 logging/config 子目录模式不一致。
2. [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt) 的 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS` 在本轮前未纳入 `dasall_infra_audit_health_integration_test`，意味着顶层 `dasall_integration_tests` 聚合边界缺少 audit integration target。
3. [tests/integration/infra/logging/CMakeLists.txt](tests/integration/infra/logging/CMakeLists.txt) 已提供仓内先例：模块级 helper + 子目录 + `integration;<module>` 标签可以同时满足 discoverability 与顶层 gate 聚合。

### 2.2 外部参考

1. CTest 的 label 过滤和分目录组织适合在不改变测试语义的前提下收敛模块级 discoverability；018 的目标是让 audit integration 入口与 logging 等模块保持同一治理模式。

### 2.3 可落地启发

1. 018 不需要重写 `InfraAuditHealthIntegrationTest` 断言，只需要迁移目录并接好 helper/label/target list。
2. 顶层 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS` 必须显式包含 audit target，否则 discoverability 虽然存在，顶层 gate 仍不完整。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 收口 audit integration 子目录 | audit 设计 8.1 | `tests/integration/infra/audit/` | 用例不再挂在 infra 根级 |
| D2 | 收口 `integration;audit` 标签 | tests 现状；设计 9.1 | audit integration helper | 用例可被 `ctest -N -L audit` 命中 |
| D3 | 补齐顶层 integration target 聚合 | tests/integration 顶层 SSOT | `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS` | audit integration target 进入顶层 gate |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| audit integration 从根级迁入专属子目录 | 新增 [tests/integration/infra/audit/CMakeLists.txt](tests/integration/infra/audit/CMakeLists.txt) 与迁移后的 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp) |
| 根级 infra integration 入口只保留模块子目录聚合 | 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) |
| 顶层 integration gate 显式纳入 audit target | 更新 [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt) |

### 4.2 Build 三件套

1. 代码目标：新增 audit integration helper，迁移用例到 audit 子目录，并把 audit integration target 接入顶层 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`。
2. 测试目标：验证 `InfraAuditHealthIntegrationTest` 可被名字与 audit 标签同时发现，并保持执行通过。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest"`
   - `ctest --test-dir build-ci -N -L audit -R "InfraAuditHealthIntegrationTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`

### 4.3 D Gate

结论：PASS。

理由：

1. 当前轮只处理目录/标签/聚合，不扩张测试逻辑与桥接实现，任务边界清晰。
2. 通过名字与标签双重 discoverability 验证即可证明 topology/registration 收口完成。

## 5. Build 落地结果

1. 新增 [tests/integration/infra/audit/CMakeLists.txt](tests/integration/infra/audit/CMakeLists.txt)，定义 `dasall_register_audit_integration_test`，统一 `integration;audit` 标签与 `infra/src` include path。
2. 将现有用例迁移到 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)，保持 015 已落盘的 health/metrics 协同断言不变。
3. 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)，移除根级 audit 用例直连注册，改为 `add_subdirectory(audit)`。
4. 更新 [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt)，将 `dasall_infra_audit_health_integration_test` 纳入 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra_audit_health_integration_test`：通过。
3. `ctest --test-dir build-ci -N -R "InfraAuditHealthIntegrationTest"`：发现 1 个定向测试。
4. `ctest --test-dir build-ci -N -L audit -R "InfraAuditHealthIntegrationTest"`：发现同一测试，证明 `integration;audit` discoverability 已生效。
5. `ctest --test-dir build-ci --output-on-failure -R "InfraAuditHealthIntegrationTest"`：1/1 通过。

## 7. 结论

1. `AUD-TODO-018` 已将 audit integration 从“根级临时注册”推进到“audit 子目录 + `integration;audit` 标签 + 顶层 target 聚合”的稳定入口。
2. 019 现在可以只聚焦质量门与证据回写，不再承担测试接线或 discoverability 修复。
