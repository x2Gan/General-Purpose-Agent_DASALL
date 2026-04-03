# SEC-TODO-009 SecretLeaseRegistry 生命周期收敛

日期：2026-04-03
任务：SEC-TODO-009
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-009 定义为“实现 SecretLeaseRegistry 生命周期管理”，验收出口是创建、过期、释放、陈旧句柄四路径。
2. secret 详细设计 6.3、6.5、6.7、6.8 已明确 lease 需要跟踪 `lease_id`、`handle_id`、`consumer_ref`、`expires_at_ms`、`rotation_epoch` 和 `state`，并要求过期或代次漂移后不得继续 materialize。
3. SEC-TODO-008 已完成 SecretManagerFacade 访问骨架，因此本轮可以把 facade 内部临时 lease map 收敛为独立 registry，而不改 public contracts。

## 2. 研究学习结果

### 2.1 本地证据

1. secret 详细设计 6.3 明确 `SecretLeaseRegistry` 的语义契约是“输入 handle/consumer/deadline，输出 lease_id/expires_at/release status”，说明 registry 应只跟踪生命周期，而不负责权限、审计或 backend 协议。
2. secret 详细设计 6.8 明确 stale handle 必须返回 `INF_E_SECRET_VERSION_STALE`，因此 manager 不能再把版本漂移退化为 backend not found。
3. SecretManagerFacade 现有 `active_leases_` 只是临时 map，能支撑 release，但不足以表达 expired/stale/revoked 状态，也不利于 010 之后的 rotation epoch 交互。

### 2.2 外部参考

1. OWASP Secrets Management Cheat Sheet 强调 secret 生命周期中的短期凭据、显式过期和吊销行为必须可判定、可审计，这支持本轮显式建模 Active/Released/Expired/Stale/Revoked。
2. Azure Key Vault best practices 强调 secret 版本切换后旧引用必须快速失效，这支持 DASALL 以 `rotation_epoch` 漂移建模 stale handle，而不是把版本轮换伪装成普通未命中。

### 2.3 可落地启发

1. SecretLeaseRegistry 最小实现只需要 create/validate/expire/release 与按 secret 批量失效，不需要提前引入审计、缓存或轮换策略接口。
2. SecretManagerFacade.materialize 应先读取当前 backend 版本并校验 handle 版本，再走 registry create_lease，从而把 stale handle 明确映射到 `INF_E_SECRET_VERSION_STALE`。
3. release 逻辑应改为委托 registry，以便后续 rotation/revoke 只需要通知状态，而不是重新拼装手写 map。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 抽取独立 lease registry | secret 设计 6.3/6.5 | SecretLeaseRegistry.h/.cpp | lease 的创建、校验、过期、释放状态可单独测试 |
| D2 | 让 facade 复用 registry | secret 设计 6.7/6.8 | SecretManagerFacade.cpp | materialize/release/revoke 不再依赖临时 active_leases_ |
| D3 | 固化 expired/stale 回归测试 | TODO 任务要求；设计 6.8 | SecretLeaseRegistryTest.cpp；SecretManagerFacadeTest.cpp | 过期与版本漂移都有明确错误码回归 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| lease 生命周期从 facade map 抽离为独立 registry | infra/src/secret/SecretLeaseRegistry.h；infra/src/secret/SecretLeaseRegistry.cpp |
| stale handle 要映射为 VersionStale 而非 not found | infra/src/secret/SecretManagerFacade.cpp；tests/unit/infra/secret/SecretManagerFacadeTest.cpp |
| 创建/过期/释放/陈旧句柄四路径可重复验证 | tests/unit/infra/secret/SecretLeaseRegistryTest.cpp |
| CMake 收口 registry 与回归测试 discoverability | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：新增 SecretLeaseRegistry internal 头/源，并将 SecretManagerFacade 的 lease 生命周期改为委托 registry。
2. 测试目标：新增 SecretLeaseRegistryTest 覆盖创建/过期/释放/陈旧句柄；扩展 SecretManagerFacadeTest 覆盖 stale handle materialize 拒绝路径。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_secret_lease_registry_unit_test dasall_contract_secret_manager_facade_boundary_test`
   - `ctest --test-dir build-ci -N -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"`

### 4.3 D Gate

结论：PASS。

理由：

1. 009 的前置依赖 SEC-TODO-003 与 SEC-TODO-008 已完成，当前不存在新的 blocker。
2. 本轮实现仍停留在 lease 生命周期和 stale handle 守卫，不提前进入 dual-slot 轮换策略或审计桥实现。

## 5. Build 落地结果

1. 新增 infra/src/secret/SecretLeaseRegistry.h 与 infra/src/secret/SecretLeaseRegistry.cpp，落盘 `create_lease`、`validate_lease`、`expire_lease`、`release_lease` 和按 secret 批量失效的最小 registry。
2. 更新 infra/src/secret/SecretManagerFacade.h 与 infra/src/secret/SecretManagerFacade.cpp，移除临时 active lease map，把 materialize/release/revoke 的生命周期管理改为委托 SecretLeaseRegistry，并把 stale handle 显式映射到 `INF_E_SECRET_VERSION_STALE`。
3. 新增 tests/unit/infra/secret/SecretLeaseRegistryTest.cpp，覆盖 lease 创建、过期、释放和 rotation epoch 漂移导致的 stale 句柄。
4. 更新 tests/unit/infra/secret/SecretManagerFacadeTest.cpp，新增 backend 版本轮换后的 stale handle materialize 拒绝回归测试。
5. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 registry 源码和 unit test target 纳入构建图。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_secret_lease_registry_unit_test dasall_contract_secret_manager_facade_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"`：通过，发现 3 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacadeTest|SecretLeaseRegistryTest|SecretManagerFacadeBoundaryContractTest"`：通过，3/3 tests passed。

## 7. 结论

1. SEC-TODO-009 已把 secret 访问链从“manager 内部临时状态”推进到“独立 lease 生命周期组件 + facade 委托”，为后续 rotation coordinator 提供了可复用的 lease 状态基线。
2. 过期与 stale handle 都已具备明确错误码，不再依赖 backend 未命中来隐式表达生命周期失败。