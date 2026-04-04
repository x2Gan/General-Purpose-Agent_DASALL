# SEC-TODO-010 SecretRotationCoordinator 轮换骨架收敛

日期：2026-04-04
任务：SEC-TODO-010
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-010 定义为“实现 SecretRotationCoordinator 轮换骨架”，验收出口是 create/test/promote/revoke/rollback 路径与 rollback fail 注入路径。
2. secret 详细设计 6.8.1 / 6.9 已在 SEC-BLK-002 中冻结 internal `ISecretRotationValidator` 最小接口、candidate version 推导，以及 `validation_required` / `dual_slot_enabled` / `grace_period_sec` 的时序语义。
3. SEC-TODO-009 已完成 SecretLeaseRegistry 生命周期基线，SEC-TODO-008 已完成 SecretManagerFacade 主链，因此本轮可以只补轮换协调与 manager rotate 委托，而不改 public contracts。

## 2. 研究学习结果

### 2.1 本地证据

1. secret 详细设计 6.8 明确轮换失败必须回退并保留旧版本为 current，回退失败则返回 `INF_E_SECRET_ROTATION_ROLLBACK_FAILED`，因此 coordinator 不能把 revoke/rollback 失败静默退化为普通 backend unavailable。
2. MockSecretBackend 已具备最小 `promote_version` / `revoke_version` 协议，适合直接作为 010 的正负例 backend；FileSecretBackend 仍保持 v1 skeleton，不影响本轮用 mock 验证轮换时序。
3. SecretManagerFacade 当前唯一剩余 placeholder 行为就是 `rotate` deferred failure，因此 010 除了新增 coordinator 外，还需要把 facade 切到真实委托，否则轮换能力仍然不可达。

### 2.2 外部参考

1. OWASP Secrets Management Cheat Sheet 建议自动化轮换采用显式 create / set / test / finish 多阶段流程，并支持“新版本写、旧版本读”的渐进切换与清晰审计证据，这支持 DASALL 将 validate / promote / revoke / rollback 分成可二值判定步骤。
2. Azure Key Vault secrets best practices 建议零停机轮换使用 dual credentials，并在轮换后刷新缓存与监控 lifecycle events，这支持 DASALL 在 dual-slot 模式下显式记录 backlog / rollback-ready 状态，而不是把 grace window 藏进隐式副作用。

### 2.3 可落地启发

1. internal validator 只需要冻结 validate_candidate(context) 单一入口，测试中通过 fake validator 即可覆盖 validation pass/fail，不需要提前引入外部 policy service。
2. coordinator 应自己推导 candidate version，避免打破既有 `RotationRequest` public interface。
3. dual-slot + grace window 的最小落地可以先表现为 revoke backlog 状态，并留待后续 health/integration 任务继续消费，不必在 010 同轮扩展 lease registry 语义。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 internal rotation validator | secret 设计 6.8.1 / 6.9 | SecretRotationValidator.h | validate_candidate context/result 可单独注入测试 |
| D2 | 落盘轮换状态机骨架 | secret 设计 6.8 | SecretRotationCoordinator.h/.cpp | validate / promote / revoke / rollback 路径可独立验证 |
| D3 | 接通 facade rotate 出口 | TODO 任务要求；设计 6.6 / 6.8 | SecretManagerFacade.h/.cpp | public rotate 不再返回 deferred failure |
| D4 | 固化轮换回归测试 | TODO 任务要求 | SecretRotationCoordinatorTest.cpp；SecretManagerFacadeTest.cpp | dual-slot backlog、validation fail、rollback、rollback fail 全部可二值判定 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| internal validator 只冻结最小 validate_candidate 入口 | infra/src/secret/SecretRotationValidator.h |
| coordinator 负责 candidate 推导、promote/revoke/rollback 和 backlog 状态 | infra/src/secret/SecretRotationCoordinator.h；infra/src/secret/SecretRotationCoordinator.cpp |
| SecretManagerFacade.rotate 改为委托 coordinator | infra/src/secret/SecretManagerFacade.h；infra/src/secret/SecretManagerFacade.cpp |
| 回退路径与 manager 委托都要可测试 | tests/unit/infra/secret/SecretRotationCoordinatorTest.cpp；tests/unit/infra/secret/SecretManagerFacadeTest.cpp |
| CMake 收口新源码和单测 discoverability | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：新增 SecretRotationValidator / SecretRotationCoordinator internal 实现，并让 SecretManagerFacade.rotate 委托 coordinator。
2. 测试目标：新增 SecretRotationCoordinatorTest，覆盖 dual-slot backlog、validator reject、rollback success、rollback fail；扩展 SecretManagerFacadeTest 覆盖 rotate delegation 回归。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_secret_rotation_coordinator_unit_test`
   - `ctest --test-dir build-ci -N -R "SecretManagerFacadeTest|SecretRotationCoordinatorTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacadeTest|SecretRotationCoordinatorTest"`

### 4.3 D Gate

结论：PASS。

理由：

1. 010 的前置依赖 SEC-TODO-003 / 005 / 009 已完成，SEC-BLK-002 也已解阻。
2. 本轮实现保持在 rotation skeleton 边界内，不提前进入 audit bridge、health probe 或 integration 扩张。

## 5. Build 落地结果

1. 新增 infra/src/secret/SecretRotationValidator.h，冻结 `SecretRotationValidationContext`、`SecretRotationValidationDecision` 与最小 `ISecretRotationValidator` 接口，并提供 allow-all default validator 作为 skeleton 默认实现。
2. 新增 infra/src/secret/SecretRotationCoordinator.h 与 infra/src/secret/SecretRotationCoordinator.cpp，落盘 candidate version 推导、validate-only、promote、deferred revoke backlog、rollback 与 rollback fail 计数。
3. 更新 infra/src/secret/SecretManagerFacade.h 与 infra/src/secret/SecretManagerFacade.cpp，新增 rotation validator 注入与 rotation status 读取，并让 `rotate` 正式委托 coordinator。
4. 新增 tests/unit/infra/secret/SecretRotationCoordinatorTest.cpp，覆盖 dual-slot backlog、validator reject、rollback success 和 rollback fail 四路径。
5. 更新 tests/unit/infra/secret/SecretManagerFacadeTest.cpp，新增 manager rotate delegation 回归。
6. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 coordinator 源码和 unit test target 纳入构建图。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_secret_manager_facade_unit_test dasall_secret_rotation_coordinator_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "SecretManagerFacadeTest|SecretRotationCoordinatorTest"`：通过，发现 2 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "SecretManagerFacadeTest|SecretRotationCoordinatorTest"`：通过，2/2 tests passed。

## 7. 结论

1. SEC-TODO-010 已把 secret 轮换链路从“manager deferred placeholder”推进到“存在 internal validator + coordinator + manager delegation 的可验证骨架”。
2. dual-slot backlog、validation fail、rollback success 与 rollback fail 现在都具备明确错误码和可重复执行的 unit 证据，为后续 SecretAuditBridge / SecretHealthProbe / integration 测试提供稳定输入面。