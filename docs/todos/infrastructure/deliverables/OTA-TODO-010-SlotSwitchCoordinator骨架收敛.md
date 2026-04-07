# OTA-TODO-010 SlotSwitchCoordinator 骨架收敛

日期：2026-04-07
任务：OTA-TODO-010
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-010 定义为“实现 SlotSwitchCoordinator 骨架”，验收出口是只允许切到 inactive slot，slot 不可用时必须拒绝，且切换前必须生成 rollback token。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.2/6.8/6.9 明确 SlotSwitchCoordinator 负责三件事：选择 inactive slot、写 next boot、生成 rollback token。
3. OTA-TODO-009 已提供 InstallExecutor 骨架和 `InstallEvidence`，因此 010 可以把已安装证据作为 rollback token 的输入，而不需要重建安装语义。
4. `OTA-BLK-01` 仍阻塞 rollback token 的持久化位置与过期恢复规则，因此 010 只能冻结内存态 token 预生成骨架，不能越权落地持久化恢复。

## 2. 研究学习结果

### 2.1 本地证据

1. 设计 6.8 的正常时序明确要求“先选择 inactive slot 并生成 RollbackToken，再写入 next boot target”，因此 010 不能把 token 生成放到 `set_next_boot` 之后。
2. 设计 6.9 把 slot unavailable 归为安装/切换失败路径的一部分，说明当前槽位组没有可用 inactive target 时应返回显式拒绝，而不是静默回退到 active slot。
3. public `IBootControlAdapter` 已冻结 `get_active_target / set_next_boot / mark_boot_success / mark_boot_failed` 四个动作，所以 010 的最小实现可以围绕 adapter 与私有 token factory 组装，不需要改动公共接口。

### 2.2 外部参考

1. Android A/B OTA 典型流程会在 boot slot 变更前先建立可回退信息，再切换下次启动目标；这支持 DASALL 在 010 中先构建 `SlotPlan + RollbackToken`，再调用 `set_next_boot`。
2. RAUC 把 slot 选择与 boot 切换作为独立控制面，要求 inactive slot 明确可见；这支撑 DASALL 把“slot unavailable”建模为显式拒绝，而不是隐式覆盖当前 active slot。

### 2.3 可落地启发

1. 010 的最小可执行面可以收缩为三类私有依赖：slot inventory provider、rollback token factory、boot control adapter。
2. `select_inactive_slot` 与 `set_next_boot` 必须分开，才能在测试中直接证明 token 生成先于 boot mutation。
3. rollback token 当前只验证字段完整性和生成顺序，不承载持久化语义；这正好与 `OTA-BLK-01` 的边界保持一致。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 slot inventory / token factory 私有边界 | OTA 设计 6.2/6.8 | SlotSwitchCoordinator.h | private dependency 不倒灌到 public header |
| D2 | 显式实现 inactive slot 选择 | OTA 设计 6.8/6.9 | SlotSwitchCoordinator.cpp | active slot 不可被再次选作 target |
| D3 | 先生成 rollback token 再执行 boot mutation | OTA 设计 6.8 | SlotSwitchCoordinator.cpp | build_slot_plan 返回 SlotPlan + RollbackToken |
| D4 | 覆盖 slot unavailable 与 target 不再 inactive 的失败路径 | OTA TODO 010 验收要求 | SlotSwitchCoordinatorTest.cpp | 两类拒绝路径均可观测 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| slot 选择必须来自显式 inventory | 新增 SlotInventory / ISlotInventoryProvider |
| rollback token 必须先于切槽生成 | 新增 IRollbackTokenFactory 与 SlotSwitchPreparationResult |
| boot mutation 必须再次验证 target 仍是 inactive | `set_next_boot` 先查询 `get_active_target()` |
| slot unavailable 必须可测试 | 新增 SlotSwitchCoordinatorTest.cpp |

### 4.2 Build 三件套

1. 代码目标：新增 SlotSwitchCoordinator internal 骨架，实现 `select_inactive_slot / build_slot_plan / set_next_boot`。
2. 测试目标：新增 SlotSwitchCoordinatorTest，覆盖 inactive slot 选择、token 预生成、slot unavailable 拒绝、target 不再 inactive 拒绝。
3. 验收命令：
   - `cmake -S . -B build-ci`
   - `cmake --build build-ci --target dasall_slot_switch_coordinator_unit_test`
   - `ctest --test-dir build-ci -N -R "SlotSwitchCoordinatorTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "SlotSwitchCoordinatorTest"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

### 4.3 D Gate

结论：PASS。

理由：

1. 009 已提供 InstallEvidence，010 可以直接构建 slot switch 骨架，不需要等待 rollback controller 实现。
2. 本轮只冻结内存态 rollback token 顺序，不触碰 token 持久化设计，因此没有越过 `OTA-BLK-01` 边界。

## 5. Build 落地结果

1. 新增 infra/src/ota/SlotSwitchCoordinator.h 与 infra/src/ota/SlotSwitchCoordinator.cpp，冻结 `SlotInventory`、`ISlotInventoryProvider`、`IRollbackTokenFactory`、`SwitchPolicySnapshot`、`SlotSelectionResult` 与 `SlotSwitchPreparationResult`。
2. `select_inactive_slot` 现在会基于 slot inventory 选择与 active slot 不同的 target；若槽位组不存在 inactive target，则返回可观测拒绝结果。
3. `build_slot_plan` 先消费 `InstallEvidence` 与 switch policy，生成 `SlotPlan` 和内存态 `RollbackToken`；只有 token 有效时才允许后续进入 `set_next_boot`。
4. `set_next_boot` 会在真正写 boot target 前再次查询当前 active target，若 target 已不再 inactive，则拒绝执行 boot mutation。
5. 新增 tests/unit/infra/ota/SlotSwitchCoordinatorTest.cpp，覆盖 token 先于切槽生成、slot unavailable 拒绝、以及 target 不再 inactive 的拒绝路径。
6. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt 与 tests/unit/infra/CMakeLists.txt，将 SlotSwitchCoordinator 源码和 SlotSwitchCoordinatorTest 接入 `dasall_infra`、`dasall_unit_tests` 与 `unit;ota` 标签。

## 6. Build 合规复核

1. 职责分离：010 只做 slot 选择、token 预生成和 next boot mutation，不吞并 boot confirm 或 rollback controller 责任。
2. 阻塞边界：rollback token 仍为内存态有效对象，没有假设持久化介质、过期恢复或跨重启恢复规则。
3. 测试性：通过把 `build_slot_plan` 与 `set_next_boot` 分离，单测可以直接验证“token 先生成，再切槽”的顺序约束。
4. contracts 一致性：所有新类型都留在 `infra/src/ota` 私有域；对外仍只复用 ResultCode/ErrorInfo 边界。
5. 聚合门：新增 OTA 单测已纳入 `dasall_unit_tests`，未破坏现有 009 之前的 OTA 流程测试。

## 7. 验证结果

1. `cmake -S . -B build-ci`：通过。
2. `cmake --build build-ci --target dasall_slot_switch_coordinator_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "SlotSwitchCoordinatorTest"`：发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "SlotSwitchCoordinatorTest"`：通过，1/1 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，167/167 tests passed。

## 8. 结论

1. OTA-TODO-010 已把 slot switch 阶段推进为可执行骨架：inactive slot 选择、rollback token 预生成和 next boot mutation 现在都可单独验证。
2. 切换前必须先生成 rollback token 的顺序已在代码和单测中固定，为 012 的 rollback controller 提供了前置输入。
3. 下一步应转入 `OTA-BLK-01` 的解阻，先冻结 rollback token 持久化位置与过期规则，再进入 OTA-TODO-012 的 rollback controller 骨架。