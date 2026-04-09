# CAP-TODO-034 services 专项 Gate 与交付证据收敛

日期：2026-04-09
任务：CAP-TODO-034
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/todos/services/DASALL_capability_services子系统专项TODO.md](../DASALL_capability_services子系统专项TODO.md) 已把 CAP-TODO-034 定义为 services 专项串行链的最后一个 L2 收口任务，要求回写 Gate、阻塞、验收、回退记录，并明确 033 关闭 CAP-BLK-005 后才能执行 full gate。
2. [docs/todos/services/deliverables/CAP-TODO-033-IExecutionService-IDataService-admission评审收敛.md](CAP-TODO-033-IExecutionService-IDataService-admission评审收敛.md) 已完成 `IExecutionService` / `IDataService` 的 admission review，并把 InterfaceCatalog readiness 提升为 `ReviewReady`；034 不再处理 shared-contract admission 判定，只负责对 configure/build/discoverability/unit/contract/integration 结果做专项回写。
3. 本轮 full gate 实际执行了统一 `build-ci` 命令链：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests dasall_integration_tests`、`ctest --test-dir build-ci -N`、`ctest --test-dir build-ci --output-on-failure -L unit`、`ctest --test-dir build-ci --output-on-failure -L contract`、`ctest --test-dir build-ci --output-on-failure -L integration`。
4. 最终 gate 结果为：`ctest -N` 总测试数 `400`，`ctest -L unit` 为 `100% tests passed, 0 tests failed out of 213`，`ctest -L contract` 为 `100% tests passed, 0 tests failed out of 152`，`ctest -L integration` 为 `100% tests passed, 0 tests failed out of 35`。
5. 034 首次执行时曾被 [tests/unit/infra/DiagnosticsSnapshotStoreTest.cpp](../../../../tests/unit/infra/DiagnosticsSnapshotStoreTest.cpp) 的 retention window 场景阻塞；该用例依赖实时墙钟时间，不属于 services 产品语义回退。为保证全量 gate 的确定性，本轮在测试侧注入固定当前时间，并重建定向 unit target 后恢复通过。

## 2. Gate 执行证据

| Gate ID | 结论 | 命令证据 | 结果摘要 |
|---|---|---|---|
| CAP-GATE-01 | PASS | `cmake -S . -B build-ci -G "Unix Makefiles"`；`cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests dasall_integration_tests` | configure / generate 成功；services 静态库与 unit / contract / integration 聚合 target 全部重编通过 |
| CAP-GATE-02 | PASS | `cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests dasall_integration_tests` | `dasall_services` 已包含 facade、lane、adapter、bridge、health 等真实源码，专项构建不再依赖 placeholder-only 状态 |
| CAP-GATE-03 | PASS | `ctest --test-dir build-ci --output-on-failure -L contract` | contract 152/152 全部通过，CAP-BLK-001~005 对应的 taxonomy / route / receipt / admission 基线未回退 |
| CAP-GATE-04 | PASS | `ctest --test-dir build-ci -N`；`ctest --test-dir build-ci --output-on-failure -L unit` | `ctest -N` 总测试数为 400，unit 213/213 全部通过，services/infra unit 入口均可发现且可执行 |
| CAP-GATE-05 | PASS | `ctest --test-dir build-ci -N`；`ctest --test-dir build-ci --output-on-failure -L integration` | services smoke / failure / profile / audit / metrics / trace / health 集成入口保持 discoverable，integration 35/35 全部通过 |
| CAP-GATE-06 | PASS | `ctest --test-dir build-ci --output-on-failure -L integration` | 高风险审计、metrics、trace、health 集成链路未回退，services 集成套件 35/35 全部通过 |
| CAP-GATE-07 | PASS | `ctest --test-dir build-ci --output-on-failure -L contract` | InterfaceCatalog / InterfaceAdmission 所在 contract 套件保持全绿，`IExecutionService` / `IDataService` 的 `ReviewReady` 基线稳定 |
| CAP-GATE-08 | PASS | `ctest --test-dir build-ci --output-on-failure -L unit`；`ctest --test-dir build-ci --output-on-failure -L integration` | query / diagnose / system 只读路径与高风险 fail-closed 相关回归未退化，本轮只收口 Gate 证据，不新增越界命令语义 |

## 3. 阻塞变化与最小解阻

1. CAP-BLK-001~005 已在 012/013/014/028/033 中依次解除，034 启动时 services 专项范围内已经没有未解阻的显式 blocker。
2. 首次 full gate 的 unit 基线失败点是 `DiagnosticsSnapshotStoreTest`。根因不是 services 回归，而是 `test_snapshot_store_prunes_snapshots_outside_the_retention_window()` 依赖实时系统时间，使 retention window 边界在不同执行时刻不稳定。
3. 本轮最小解阻动作是在 [tests/unit/infra/DiagnosticsSnapshotStoreTest.cpp](../../../../tests/unit/infra/DiagnosticsSnapshotStoreTest.cpp) 中为 `SnapshotStore` 注入固定当前时间 `2026-04-08T10:00:00Z`，随后重建 `dasall_diagnostics_snapshot_store_unit_test` 并执行 `ctest --test-dir build-ci --output-on-failure -R DiagnosticsSnapshotStoreTest`，结果恢复为 `100% tests passed, 0 tests failed out of 1`。
4. 解阻动作严格限制在 tests-side 确定性修复；没有修改 services 公共 ABI、lane / adapter / bridge 行为，也没有放宽 contract 或 integration 断言。

## 4. 评审结论

1. CAP-TODO-034 通过。services 专项 Gate 的 configure/build/discoverability/unit/contract/integration 结果已全部回写到专项 TODO，并形成独立交付物与 worklog 证据。
2. 本轮 full gate 证明 services 专项 001~040、033~034 的串行链已经闭合；当前专项范围内不存在未解阻的 Build-ready 缺口。
3. 034 的完成态不等于允许继续扩张 shared contracts 或 system shared ABI。`ReviewReady` 仍只代表 admission baseline 已闭合，后续 shared header 落位必须另起任务。

## 5. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 9.4 Gate / blocker / rollback 收口 | 本文件、docs/todos/services/DASALL_capability_services子系统专项TODO.md |
| full gate 期间的最小 blocker recovery | tests/unit/infra/DiagnosticsSnapshotStoreTest.cpp |
| 执行记录与后续边界说明 | docs/worklog/DASALL_开发执行记录.md |

## 6. Build 三件套

1. 代码目标：回写 services 专项 Gate 的命令证据、阻塞变化、风险残留与完成态结论；若 full gate 被跨模块测试波动阻塞，只允许做最小 tests-side 解阻，不改写 services 产品语义。
2. 测试目标：`ctest -N` 必须保持 discoverability，`ctest -L unit` / `-L contract` / `-L integration` 必须全部恢复为全绿；若出现 blocker，需先定向复验失败目标，再回到 full gate。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_diagnostics_snapshot_store_unit_test`
   - `ctest --test-dir build-ci --output-on-failure -R DiagnosticsSnapshotStoreTest`
   - `cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests dasall_integration_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
   - `ctest --test-dir build-ci --output-on-failure -L integration`

## 7. 风险与回退

1. 本轮新增的 blocker-fix 只解决测试确定性问题；若后续再次出现 wall-clock dependent baseline 波动，优先继续修测试时钟注入或固定输入，而不是在 services 主链语义上做让步式修改。
2. `ReviewReady` / `Admit` 仍不等于 shared header 已正式迁入 `contracts/include`；若未来推进 include 迁移，必须单独评估 include 路径、兼容窗口与跨模块消费者影响。
3. 若 future gate 中的 unit / contract / integration 任一套件回退，应先判断是 services 回归还是外部 baseline 噪声，再决定是否回退本轮证据；不要让 TODO / deliverable / worklog 与真实 gate 状态脱节。
