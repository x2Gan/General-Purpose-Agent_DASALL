# SEC-TODO-008 SecretManagerFacade 访问骨架收敛

日期：2026-04-03
任务：SEC-TODO-008
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-008 定义为“实现 SecretManagerFacade 访问骨架”，验收出口是访问链路可走通，以及 access_context 字段复用边界的 contract 守卫。
2. MockSecretBackend 与 FileSecretBackend 已在前两轮落盘，因此本轮无需扩展 backend 协议，只需在既有 backend 之上收敛 manager 主链。
3. ISecretManager、SecretTypes 和 SecretErrors 已冻结，因此返回值只能继续落在既有 `SecretHandleResult`、`SecretMaterializationResult`、`SecretLifecycleResult`、`SecretInspectionResult` 上，不能新增临时边界对象。

## 2. 研究学习结果

### 2.1 本地证据

1. 现有 tests/contract/smoke/SecretManagerInterfaceBoundaryContractTest.cpp 只冻结 `ISecretManager` 的 public interface 和“不得吸收 backend/health 协议”的边界，并不覆盖 008 所需的访问链行为与 access_context 复用边界。
2. MockSecretBackend 已具备 fetch/materialize/status 和 permission-domain 守卫，因此最适合作为本轮 unit 与 contract 测试夹具，验证 manager 是否只编排链路而不重写 backend 语义。
3. SecretLifecycleResult 的 frozen contract 要求 `secret_name` 始终非空，这意味着 manager 在 release/revoke 失败路径也必须输出可判定的 secret 标识，而不能返回空壳错误对象。

### 2.2 外部参考

1. OWASP Secrets Management Cheat Sheet 强调 secrets materialization 应尽量缩短 plaintext 暴露窗口，并保持访问请求可审计，这支持 DASALL 继续使用 metadata-only handle + SecureBuffer materialize 的分段访问链。
2. Azure Key Vault best practices 强调 secret store 的 access token、lease 和 materialized value 应保持边界清晰，这支持本轮将 request/task/session 继续约束在 `SecretAccessContext`，而不是复制进 handle/lease 对象。

### 2.3 可落地启发

1. SecretManagerFacade v1 可以只承担 orchestrate fetch/materialize/release/inspect，不提前吸收 009 的 lease registry 生命周期职责。
2. `rotate` 应返回明确 deferred failure，而不是假装完成，避免越界到 SEC-TODO-010。
3. access_context 边界最适合通过 contract test 验证“handle/lease 不新增 request/task/session 字段”以及“validation failure 仍只落在 contracts::ResultCode/ErrorInfo”。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 落盘 manager 主链 | secret 设计 6.2/6.7；ISecretManager | SecretManagerFacade.h/.cpp | get/materialize/release/inspect 可在既有 backend 之上二值执行 |
| D2 | 保持 rotation/revoke 边界最小化 | secret 设计 6.8；SEC-TODO-010 未开始 | SecretManagerFacade.cpp | rotate 明确 deferred；revoke 仅做最小 backend 委托 |
| D3 | 固化 unit/contract 验收出口 | TODO 任务要求；现有 contract 注册模式 | SecretManagerFacadeTest.cpp；SecretManagerFacadeBoundaryContractTest.cpp | 访问链路和上下文字段边界均可重复验证 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| manager 只编排 access chain，不扩展 public contracts | infra/src/secret/SecretManagerFacade.h；infra/src/secret/SecretManagerFacade.cpp |
| handle/lease 不复制 request/task/session 字段 | tests/contract/smoke/SecretManagerFacadeBoundaryContractTest.cpp |
| unit 走通 get -> materialize -> release -> inspect | tests/unit/infra/secret/SecretManagerFacadeTest.cpp |
| CMake 收口 unit/contract discoverability | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt；tests/contract/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：新增 SecretManagerFacade internal 头/源与 unit/contract tests，并将其接入现有 CMake 图。
2. 测试目标：新增 SecretManagerFacadeTest 覆盖访问链正向与 expired handle 负向；新增 SecretManagerFacadeBoundaryContractTest 覆盖 access_context 字段边界与错误载荷边界。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_contract_secret_manager_facade_boundary_test`
   - `ctest --test-dir build-ci -N -R "SecretManagerFacade(Test|BoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacade(Test|BoundaryContractTest)"`

### 4.3 D Gate

结论：PASS。

理由：

1. 008 的前置依赖都已完成，当前没有新的 blocker。
2. 本轮实现严格停留在 manager 访问骨架，不提前抽取 lease registry，也不抢占 rotation coordinator 的设计空间。

## 5. Build 落地结果

1. 新增 infra/src/secret/SecretManagerFacade.h 与 infra/src/secret/SecretManagerFacade.cpp，落盘 `get_secret`、`materialize`、`release`、`inspect` 主链，以及 `rotate` deferred failure 和 `revoke` 最小 backend 委托。
2. 新增 tests/unit/infra/secret/SecretManagerFacadeTest.cpp，覆盖访问链正向路径和 expired handle 负向路径。
3. 新增 tests/contract/smoke/SecretManagerFacadeBoundaryContractTest.cpp，固化 handle/lease 不吸收 request/task/session 字段，以及 validation failure 只引用 contracts error payload 的边界。
4. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt，将 manager facade 源码与新 unit/contract targets 纳入构建图。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_contract_secret_manager_facade_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "SecretManagerFacade(Test|BoundaryContractTest)"`：通过，发现 2 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacade(Test|BoundaryContractTest)"`：通过，2/2 tests passed。

## 7. 结论

1. SEC-TODO-008 已把 secret manager 从“只有接口定义”推进到“存在可验证的访问骨架”，后续可以在不改 public contracts 的前提下继续推进 009 的 lease 生命周期管理。
2. 当前访问链已经证明 SecretManagerFacade 可以复用现有 backend 协议，同时把 access_context 边界继续约束在 frozen contract 内。