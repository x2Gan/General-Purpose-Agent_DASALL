# SEC-TODO-006 MockSecretBackend 骨架收敛

日期：2026-04-03
任务：SEC-TODO-006
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-006 定义为“实现 MockSecretBackend 骨架”，验收出口为成功/未命中/拒绝/backend down 四路径单测。
2. docs/architecture/DASALL_infra_secret模块详细设计.md 6.2/6.3/6.6/6.7 已冻结 MockSecretBackend 的职责边界、ISecretBackend 协议、get/materialize 生命周期和 lease 基本语义。
3. infra/include/secret/ISecretBackend.h、infra/include/secret/SecretTypes.h、infra/include/secret/SecretErrors.h 已先行冻结 backend 协议、对象模型与错误码映射，因此本轮可以直接进入 internal backend 落盘。

## 2. 研究学习结果

### 2.1 本地证据

1. 当前仓库在本轮前只有 secret public headers 和 interface tests，还没有任何 secret backend 实现源码，因此 006 的根任务是先补一个可重复、可注入失败的 internal mock backend，而不是提前做 manager/facade。
2. ISecretBackend 的五个方法已经在 public header 冻结，说明 MockSecretBackend 首轮必须完整实现 fetch/materialize/promote/revoke/status 入口，即使验收重点只落在四条最小路径。
3. tests/unit/infra/CMakeLists.txt 已有直接注册 secret unit test 的既有模式，允许本轮只新增一个内部 backend 测试目标并补 `infra/src` include path，而不必等待后续统一测试接线任务。

### 2.2 外部参考

1. OWASP Secrets Management Cheat Sheet 强调最小权限、集中治理、受控 materialize 和尽量缩短 plaintext in memory 暴露窗口，这支持 mock backend 在 materialize 时继续通过 SecureBuffer 返回明文，而不是裸字符串。
2. Azure Key Vault best practices 强调 secret store 不应退化为普通配置数据面，且访问应受清晰安全边界约束，这支持 mock backend 只暴露 metadata record 与 permission-domain 守卫，而不把实现细节扩张到 contracts。

### 2.3 可落地启发

1. Mock backend 首版适合做成 internal header + source，供后续 SecretManagerFacade 直接组合复用。
2. 访问拒绝路径放在 materialize 阶段最合理，因为 ISecretBackend::fetch_record 不接收 access_context。
3. backend unavailable 状态除了返回错误码，还应同步沉淀到 get_backend_status，以便后续 health/facade 直接消费。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 internal MockSecretBackend 对象模型 | secret 设计 6.2/6.6；ISecretBackend | MockSecretBackend.h | 提供 seeded record、availability、rate_limit 最小控制面 |
| D2 | 实现四条验收路径 | secret 设计 6.7；SecretErrors | MockSecretBackend.cpp | 成功/未命中/拒绝/backend down 可二值判定 |
| D3 | 补齐 test 与 discoverability | 编码规范 3.7；tests 现状 | MockSecretBackendTest.cpp + CMake 改动 | ctest -N/-R 可发现并通过 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| internal mock backend 承载 seeded metadata + plaintext fixture | infra/src/secret/backends/MockSecretBackend.h；infra/src/secret/backends/MockSecretBackend.cpp |
| access denied 在 materialize 阶段按 permission_domain 判定 | infra/src/secret/backends/MockSecretBackend.cpp |
| backend unavailable 同时影响 fetch/materialize/status | infra/src/secret/backends/MockSecretBackend.cpp |
| 四路径单测与 discoverability 收口 | tests/unit/infra/secret/MockSecretBackendTest.cpp；tests/unit/infra/CMakeLists.txt；tests/unit/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：新增 MockSecretBackend internal 头/源文件，更新 infra/CMakeLists.txt 把源码纳入 dasall_infra，并更新 unit CMake 注册新的 backend test。
2. 测试目标：新增 MockSecretBackendTest，覆盖成功、未命中、权限拒绝和 backend down 四路径，并验证 discoverability。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_mock_secret_backend_unit_test`
   - `ctest --test-dir build-ci -N -R MockSecretBackendTest`
   - `ctest --test-dir build-ci --output-on-failure -R MockSecretBackendTest`

### 4.3 D Gate

结论：PASS。

理由：

1. 协议、对象和错误码都已冻结，本轮不需要额外 blocker recovery。
2. 任务范围保持在 mock backend + 单测 + 最小接线，没有扩张到 file backend、manager 或 lease registry。

## 5. Build 落地结果

1. 新增 infra/src/secret/backends/MockSecretBackend.h 与 infra/src/secret/backends/MockSecretBackend.cpp，提供 seeded record、availability/rate_limit 开关、fetch/materialize/promote/revoke/status 的最小骨架实现。
2. 新增 tests/unit/infra/secret/MockSecretBackendTest.cpp，覆盖成功、未命中、拒绝和 backend down 四条任务要求路径。
3. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 mock backend 实现与新 unit test 纳入构建图和 `dasall_unit_tests` 聚合目标。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_mock_secret_backend_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R MockSecretBackendTest`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R MockSecretBackendTest`：通过，1/1 tests passed。

## 7. 结论

1. SEC-TODO-006 已把 secret 子域从“只有 backend 接口”推进到“存在可运行的 mock backend implementation + unit 验收出口”的状态。
2. 后续 SEC-TODO-008 可以直接复用 MockSecretBackend 作为最小依赖，继续推进 get/materialize/release 访问链骨架。