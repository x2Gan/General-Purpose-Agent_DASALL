# OTA-TODO-009 InstallExecutor 骨架收敛

日期：2026-04-07
任务：OTA-TODO-009
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-009 定义为“实现 InstallExecutor 骨架”，验收要求是 repo_bound 与 slot_bound 分支可区分，且写入失败能够触发清理路径并产出 InstallEvidence。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.2/6.8/6.9 要求 InstallExecutor 在 compatibility 之后承担 staged materialization 责任：repo_bound 先写 staging 区，slot_bound 写 inactive target，安装失败时必须删除新写入目标或进入前置恢复。
3. OTA-TODO-008 已完成 compatibility gate，因此 009 可以直接消费 `ArtifactDescriptor` 与 target 输入，不再重复 profile/hardware/dependency 判定。
4. Slot 切换与 rollback token 生成仍归 010/012，因此 009 只能冻结安装骨架和清理行为，不能提前吞并 SlotSwitchCoordinator 或 RollbackController 的职责。

## 2. 研究学习结果

### 2.1 本地证据

1. 设计 6.8 明确 repo_bound 与 slot_bound 的安装顺序和介质不同，说明 InstallExecutor 必须有可区分的内部写入分支，而不是单一路径后靠 target 字符串猜测。
2. 设计 6.9 把“安装失败 -> 删除新写入目标”列为独立恢复动作，因此 cleanup 不能留给未来实现补洞，骨架阶段就必须形成显式接口和单测证据。
3. `IInstallExecutor` 已冻结 `stage_artifact / activate_plan / revert_install` 三个公共方法，因此 009 需要把 activation/revert 继续封装为私有适配器边界，保证 010/012 可以在不改 public interface 的前提下替换内部实现。

### 2.2 外部参考

1. RAUC 的 staged install 设计把“写入”和“切换”区分为两个明确阶段，这支持 DASALL 继续将 InstallExecutor 与 SlotSwitchCoordinator 拆开实现，而不是让 009 直接控制 boot target。
2. Android A/B OTA 把 inactive slot 写入与 boot slot 选择拆成前后两个动作，失败时先清理或回退 staging 状态，再决定是否切槽；这与 DASALL 当前 009/010 的串行拆分一致。

### 2.3 可落地启发

1. 安装骨架最小可执行面可以收缩为四类私有依赖：repo/slot 写入器、失败清理器、激活适配器、回退适配器。
2. 009 的完成标志不应依赖真实平台写入，而应依赖“分支被显式区分 + cleanup 被显式调用 + 输出保持 contract-shaped result”。
3. activate/revert 在 009 只保留边界透传，真正的 inactive slot 选择和 rollback token 恢复留给 010/012。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结安装私有依赖边界 | OTA 设计 6.2/6.8 | InstallExecutor.h | 写入/清理/激活/回退边界全部保留在 ota 私有域 |
| D2 | 区分 repo_bound 与 slot_bound 安装分支 | OTA 设计 6.6.2/6.8 | InstallExecutor.cpp | `stage_artifact` 不同 artifact class 走不同 writer |
| D3 | 落地写入失败 cleanup 路径 | OTA 设计 6.9 | InstallExecutor.cpp | materialization fail 时 cleanup 必经且 failure 可观测 |
| D4 | 补足 unit/CMake 发现性 | OTA TODO 009 验收要求 | InstallExecutorTest.cpp 与 infra/tests CMake | 新单测进入 `dasall_unit_tests` 与 `unit;ota` 标签 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| 安装写入必须显式区分 repo/slot 两类工件 | 新增 IArtifactWriter，并拆分 `write_repo_bound` / `write_slot_bound` |
| 安装失败必须显式 cleanup | 新增 IInstallCleanupHandler 与 CleanupResult |
| activation/revert 仍保留 public interface 稳定 | 新增 IPlanActivationAdapter 与 IInstallRevertAdapter |
| 安装骨架通过 unit 验证，不依赖真实平台设备 | 新增 InstallExecutorTest.cpp |

### 4.2 Build 三件套

1. 代码目标：新增 InstallExecutor internal 骨架，实现 `stage_artifact / activate_plan / revert_install`。
2. 测试目标：新增 InstallExecutorTest，覆盖 repo_bound/slot_bound 分支、materialization fail cleanup、activation/revert 边界透传。
3. 验收命令：
   - `cmake -S . -B build-ci`
   - `cmake --build build-ci --target dasall_install_executor_unit_test`
   - `ctest --test-dir build-ci -N -R "InstallExecutorTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "InstallExecutorTest"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

### 4.3 D Gate

结论：PASS。

理由：

1. 008 已提供 compatibility gate，009 可以直接进入 install skeleton，不受前序 blocker 影响。
2. 009 没有触碰 boot control 或 rollback token 生命周期冻结边界，因此不会提前跨入 010/012 的职责区。

## 5. Build 落地结果

1. 新增 infra/src/ota/InstallExecutor.h 与 infra/src/ota/InstallExecutor.cpp，冻结安装私有边界：`IArtifactWriter`、`IInstallCleanupHandler`、`IPlanActivationAdapter`、`IInstallRevertAdapter`。
2. `InstallExecutor::stage_artifact` 现在会按照 `artifact_class` 选择 `write_repo_bound` 或 `write_slot_bound`，将成功写入结果收敛为 `InstallEvidence`。
3. 若 materialization 返回无效结果，InstallExecutor 会强制调用 cleanup handler；cleanup 成功或失败都会输出 contract-shaped failure，且不把半写入状态伪装为成功。
4. `activate_plan` 与 `revert_install` 在 009 中保持为边界透传：只做输入校验和 contract-shaped result 守卫，把真正的 slot switch 与 rollback 细节留给后续任务。
5. 新增 tests/unit/infra/ota/InstallExecutorTest.cpp，覆盖 repo_bound/slot_bound 分支、cleanup 路径、activation/revert 适配器透传。
6. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt 与 tests/unit/infra/CMakeLists.txt，将 InstallExecutor 源码和 InstallExecutorTest 接入 `dasall_infra`、`dasall_unit_tests` 与 `unit;ota` 标签。

## 6. Build 合规复核

1. 边界：安装写入、cleanup、激活、回退全部保持在 `infra/src/ota` 私有域，没有扩写 contracts。
2. 根因修复：将“安装失败缺少 cleanup 路径”作为骨架的核心责任直接实现，而不是在测试层做表面兜底。
3. 测试覆盖：正例覆盖 repo/slot 双分支；负例覆盖写入失败后的 cleanup；同时验证 activation/revert 的公共边界仍可用。
4. CMake：新单测已加入 `dasall_unit_tests` 聚合目标，并通过 `ctest -N` 被发现。
5. 公共接口稳定性：未改动 `IInstallExecutor`，后续 010/012 可直接接在现有 public interface 之后演进。

## 7. 验证结果

1. `cmake -S . -B build-ci`：通过。
2. `cmake --build build-ci --target dasall_install_executor_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "InstallExecutorTest"`：发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "InstallExecutorTest"`：通过，1/1 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，166/166 tests passed。

## 8. 结论

1. OTA-TODO-009 已把安装阶段从“设计描述”推进为“可执行骨架”：repo_bound 与 slot_bound 写入路径现在被显式区分，写入失败也能强制进入 cleanup。
2. InstallExecutor 已能稳定产出 `InstallEvidence` 并守住 contract-shaped failure/result 边界，为 010 的 slot switch 和 012 的 rollback controller 提供稳定下游接口。
3. 下一轮应进入 OTA-TODO-010，实现 SlotSwitchCoordinator 骨架，把 inactive slot 选择、next boot 设置与 rollback token 生成接到 009 之后。