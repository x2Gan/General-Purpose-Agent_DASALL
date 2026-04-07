# OTA-TODO-012 RollbackController 骨架收敛

日期：2026-04-07
任务：OTA-TODO-012
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-012 定义为“实现 RollbackController 骨架”，验收要求是 rollback 成功/失败双路径可判定，且 rollback_fail 必须独立可观测。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.2/6.9 将 RollbackController 的职责固定为：消费 `RollbackToken + InstallEvidence + 当前 boot 状态`，恢复 boot target、恢复 repo pointer，并输出 rollback evidence。
3. OTA-TODO-018 已在本轮前解阻 `OTA-BLK-01`，因此 012 可以直接消费冻结后的 token TTL、状态和重启恢复边界，而不需要再讨论 file/sqlite 介质选择。
4. OTA-TODO-009 与 OTA-TODO-010 已提供 InstallEvidence 与 RollbackToken 的稳定上游输入，012 只需把 rollback controller 的执行闭环补齐。

## 2. 研究学习结果

### 2.1 本地证据

1. 设计 6.9.2 把 rollback 拆成两个恢复动作：恢复旧 boot target、恢复 repo_bound 指针；这说明 012 必须把 `restore_boot_target` 与 `recover_repo_pointer` 作为独立可测试 helper，而不是隐藏在单个 monolithic rollback 中。
2. 设计 6.10.2 已冻结过期 token 不得自动回滚，因此 012 必须在任何副作用之前先做 `expires_at` 检查。
3. `IInstallExecutor` 已冻结 `RollbackResult` 边界，012 不应新增新的公共 rollback 返回对象，而应在私有域中收敛 repo pointer recovery 和 evidence writer 细节。

### 2.2 外部参考

1. A/B OTA 的失败恢复路径通常先恢复 boot target，再恢复附属发布指针或配置引用，这支持 DASALL 把 boot restore 与 repo pointer recovery 明确拆层。
2. 面向启动恢复的 rollback 关键状态应优先使用单一、可判定的结果对象输出，而不是让上层从多个 side effect 中自行拼接结论；这与 DASALL 当前 `RollbackResult` 边界一致。

### 2.3 可落地启发

1. rollback controller 的最小私有依赖可以收缩为四项：boot control adapter、repo pointer recovery adapter、evidence writer、time provider。
2. token 过期必须在任何 `set_next_boot` 或 repo recovery 之前短路。
3. evidence writer 不需要在 012 中落真实审计实现，但必须保证 rollback success 返回一个稳定 evidence ref，failure 则独立走 rollback_fail 语义。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 rollback 私有依赖边界 | OTA 设计 6.2/6.9/6.10.2 | RollbackController.h | boot/repo/evidence/time 都留在 ota 私有域 |
| D2 | 显式恢复旧 boot target | OTA 设计 6.9.2 | RollbackController.cpp | `restore_boot_target` 独立可测 |
| D3 | 显式恢复 repo pointer 并写 evidence | OTA 设计 6.9.2 | RollbackController.cpp | rollback success 必定携带 evidence ref |
| D4 | 覆盖 expired token 与 repo recovery fail | OTA TODO 012 验收要求 | RollbackControllerTest.cpp | 失败路径均 contract-shaped 且可观测 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| 回滚执行必须先判 token 是否过期 | `rollback(...)` 中新增 TTL 短路检查 |
| boot restore 与 repo recovery 必须拆开验证 | 新增 `restore_boot_target` 与 `recover_repo_pointer` |
| rollback success 必须携带 evidence ref | 新增 IRollbackEvidenceWriter / RollbackEvidenceResult |
| rollback_fail 必须独立可观测 | 新增 RollbackControllerTest 失败路径断言 |

### 4.2 Build 三件套

1. 代码目标：新增 RollbackController internal 骨架，实现 `rollback / restore_boot_target / recover_repo_pointer`。
2. 测试目标：新增 RollbackControllerTest，覆盖 success、expired token fail、repo recovery fail，以及 helper 边界可测性。
3. 验收命令：
   - `cmake -S . -B build-ci`
   - `cmake --build build-ci --target dasall_rollback_controller_unit_test`
   - `ctest --test-dir build-ci -N -R "RollbackControllerTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "RollbackControllerTest"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

### 4.3 D Gate

结论：PASS。

理由：

1. `OTA-BLK-01` 已被 018 解阻，012 现在具备清晰的 token 生命周期和恢复矩阵输入。
2. 009/010 已提供 InstallEvidence 与 RollbackToken 上游对象，012 可以直接完成 rollback skeleton 而不需要回改公共接口。

## 5. Build 落地结果

1. 新增 infra/src/ota/RollbackController.h 与 infra/src/ota/RollbackController.cpp，冻结 `IRepoPointerRecoveryAdapter`、`IRollbackEvidenceWriter`、`ITimeProvider`、`RepoPointerRecoveryResult` 与 `RollbackEvidenceResult`。
2. `RollbackController::rollback` 现在按固定顺序执行：
   - 校验 rollback token 与 install evidence；
   - 在任何副作用前校验 token 未过期；
   - `restore_boot_target(previous_boot_target)`；
   - `recover_repo_pointer(token, install_evidence)`；
   - 写 rollback evidence 并返回 `RollbackResult`。
3. `restore_boot_target` 与 `recover_repo_pointer` 被保留为独立 helper，可直接单测验证和后续集成替换。
4. 回滚失败统一映射到已冻结的 `INF_E_OTA_ROLLBACK_FAIL` outward category；细分失败原因继续通过 `stage / message / source_ref` 留痕，不扩写 contracts。
5. 新增 tests/unit/infra/ota/RollbackControllerTest.cpp，覆盖 success、expired token fail、repo recovery fail 和 helper 边界透传。
6. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt 与 tests/unit/infra/CMakeLists.txt，将 RollbackController 源码和 RollbackControllerTest 接入 `dasall_infra`、`dasall_unit_tests` 与 `unit;ota` 标签。

## 6. Build 合规复核

1. 边界：rollback token 生命周期仍由 018 的设计补丁定义；012 只消费冻结边界，不重新发明存储语义。
2. 根因处理：把 rollback fail 的独立可观测性收进 controller 本身，而不是依赖上层补日志来弥补失败结果不清晰的问题。
3. 公共接口稳定：未修改 `IOTAManager`、`IInstallExecutor` 或 `IBootControlAdapter` public surface。
4. 测试覆盖：正例覆盖完整 rollback；负例覆盖 expired token 与 repo recovery fail，满足 TODO 的双路径要求。
5. 聚合门：RollbackControllerTest 已加入 `dasall_unit_tests`，OTA label 从 5 提升到 6，仓库级 unit 门仍全部通过。

## 7. 验证结果

1. `cmake -S . -B build-ci`：通过。
2. `cmake --build build-ci --target dasall_rollback_controller_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "RollbackControllerTest"`：发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "RollbackControllerTest"`：通过，1/1 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，168/168 tests passed。

## 8. 结论

1. OTA-TODO-012 已完成，OTA 现在具备 RollbackController 骨架，能够在 token 未过期时恢复 boot target、恢复 repo pointer，并输出稳定 evidence ref。
2. rollback_fail 现在是独立、contract-shaped 的失败路径，不再与 install/switch 失败混淆。
3. 用户要求的 OTA 核心链路骨架 `006~010、012` 已按顺序完成；剩余 OTA 闭环重点转向 011、013、014 与 017 的确认、观测和 integration 门。