# OTA-TODO-008 ArtifactCompatibilityEvaluator 骨架收敛

日期：2026-04-07
任务：OTA-TODO-008
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 已将 OTA-TODO-008 定义为“实现 ArtifactCompatibilityEvaluator 骨架”，验收出口是 hardware_selector/profile/dependency_refs 冲突必须被拒绝并阻断安装。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.2/6.8 已冻结 ArtifactCompatibilityEvaluator 的职责：在 verify 通过后，根据 hardware_selector、Profile 允许集与依赖关系产生 compatibility report。
3. OTA-TODO-007 已完成 PackageVerifier 骨架，本轮可以直接以 `VerifiedPackageManifest` 作为输入，不需要再次处理 trust anchor 或签名链。
4. OTA 设计与专项 TODO 都明确 008 只做 compatibility gate，不进入 install/switch，因此实现必须保持为纯判定骨架。

## 2. 研究学习结果

### 2.1 本地证据

1. OTA 设计 6.8 要求 compatibility failure 不得进入安装，这意味着 compatibility 报告必须在失败时清空 accepted artifacts，避免后续 install 误把“部分可用”当成可继续推进的信号。
2. OTATypes 中 `ArtifactDescriptor` 已冻结 `artifact_class`、`hardware_selector`、`dependency_refs`，因此 008 的最小实现不需要新增 public object，只需在 OTA 私有域补 capability/profile snapshot。
3. profile 裁剪约束来自蓝图 5.1 和 OTA 设计 6.10：Profile 只能裁剪能力，不得改变上层调用语义，所以 compatibility evaluator 的输出应是“兼容/阻断报告”，而不是 profile 特化的执行分支。

### 2.2 外部参考

1. RAUC 的 install-check hook 允许在真正安装前基于系统 compatible 字段和本机状态拒绝升级，这支持 DASALL 在 ArtifactCompatibilityEvaluator 中把 compatibility 冲突作为 install 前的显式 gate。
2. Android A/B OTA 文档强调未使用槽位写入前应先完成必要的设备侧判断，避免把不兼容工件写入 inactive slot；这支撑 DASALL 在 install 之前单独冻结 compatibility evaluator，而不是把这些冲突检查下沉到 install executor。

### 2.3 可落地启发

1. capability snapshot 只需要表达本轮真正使用的两类事实：`supported_hardware` 和 `available_dependency_refs`，避免在 008 里提前扩张为平台资源总表。
2. profile snapshot 只需要冻结 `profile_name` 和 slot/repo 工件允许位；更细的安装策略保持留在 009 之后。
3. compatibility failure 应统一落到 contract-shaped `ErrorInfo`，这样 009/010 后续只需要消费 compatibility report，而不需要重新定义失败对象。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 capability/profile snapshot | OTA 设计 6.2/6.8 | ArtifactCompatibilityEvaluator.h | capability/profile 全为只读输入，不扩写 public contract |
| D2 | 收敛 compatibility report 语义 | OTA 设计 6.8 | ArtifactCompatibilityEvaluator.cpp | 成功时保留 accepted artifacts，失败时清空并给出 blocking reasons |
| D3 | 覆盖三类冲突负例 | OTA TODO 008 验收要求 | ArtifactCompatibilityEvaluatorTest.cpp | hardware/profile/dependency 冲突均可二值判定 |
| D4 | 接线 unit 构建与 discoverability | 现有 infra/tests CMake | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt | 新测试进入 `dasall_unit_tests` 和 `unit;ota` 标签 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| capability snapshot 只冻结硬件和依赖引用 | 新增 DeviceCapabilitySnapshot |
| profile snapshot 只冻结 profile 名和 slot/repo 允许位 | 新增 ArtifactCompatibilityProfile |
| compatibility success/failure 明确分流 | 新增 CompatibilityReport |
| hardware/profile/dependency 三类冲突直连 unit 负例 | 新增 ArtifactCompatibilityEvaluatorTest.cpp |
| evaluator 纳入 OTA 源码和 unit 聚合图 | infra/tests CMake 接线 |

### 4.2 Build 三件套

1. 代码目标：新增 ArtifactCompatibilityEvaluator internal 骨架，实现 `evaluate(verified_manifest, capability, profile)`。
2. 测试目标：新增 ArtifactCompatibilityEvaluatorTest，覆盖 success、hardware conflict、profile conflict、dependency conflict。
3. 验收命令：
   - `cmake -S . -B build-ci`
   - `cmake --build build-ci --target dasall_artifact_compatibility_evaluator_unit_test`
   - `ctest --test-dir build-ci -N -R "ArtifactCompatibilityEvaluatorTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "ArtifactCompatibilityEvaluatorTest"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

### 4.3 D Gate

结论：PASS。

理由：

1. OTA-TODO-007 已提供 verified manifest 输入，008 不再受 trust anchor 或 signature blocker 约束。
2. 本轮保持为纯 compatibility gate，不提前进入 install executor 或 slot switch 逻辑。

## 5. Build 落地结果

1. 新增 infra/src/ota/ArtifactCompatibilityEvaluator.h 与 infra/src/ota/ArtifactCompatibilityEvaluator.cpp，冻结 `DeviceCapabilitySnapshot`、`ArtifactCompatibilityProfile` 与 `CompatibilityReport`。
2. ArtifactCompatibilityEvaluator 现在实现四类 compatibility gate：
   - verified manifest 结构合法性；
   - manifest `compatible_profiles` 是否允许当前 profile；
   - artifact `hardware_selector` 是否与设备能力相交；
   - artifact `dependency_refs` 是否可用且未被 profile 禁用。
3. compatibility 失败统一输出 contract-shaped `ErrorInfo`；一旦出现任一 blocker，就清空 accepted artifacts，确保 install 不会接收到部分兼容的残留状态。
4. 新增 tests/unit/infra/ota/ArtifactCompatibilityEvaluatorTest.cpp，覆盖 success、hardware conflict、profile conflict、dependency conflict 四类路径。
5. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt 与 tests/unit/infra/CMakeLists.txt，将 evaluator 骨架和 `ArtifactCompatibilityEvaluatorTest` 接入 `dasall_infra`、`dasall_unit_tests` 和 `unit;ota` 标签。

## 6. Build 合规复核

1. 代码注释：本轮未新增注释；snapshot/report 类型名和 gate helper 已直接表达 compatibility 语义，代码保持自解释。
2. 正负例：已覆盖 success 正例，以及 hardware/profile/dependency 三类负例。
3. 测试发现性：已通过 `ctest -N -R "ArtifactCompatibilityEvaluatorTest"` 验证新单测被发现，并通过 `-L unit` 确认未破坏仓库 unit 聚合门。
4. TODO/交付物/worklog：本轮同步回写专项 TODO、deliverable 与 worklog。
5. 提交前状态隔离：本轮仅包含 OTA-TODO-008 相关源码、单测、CMake 和文档回写文件。

## 7. 验证结果

1. `cmake -S . -B build-ci`：通过。
2. `cmake --build build-ci --target dasall_artifact_compatibility_evaluator_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "ArtifactCompatibilityEvaluatorTest"`：发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "ArtifactCompatibilityEvaluatorTest"`：通过，1/1 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，165/165 tests passed。

## 8. 结论

1. OTA-TODO-008 已把 compatibility gate 从“设计约束”推进到“可执行 evaluator 骨架”，manifest/profile/hardware/dependency 冲突现在都能在 install 前被显式拒绝。
2. 009 可直接消费 CompatibilityReport 的 accepted artifacts 结果，而无需再次重建 profile/hardware/dependency 判断逻辑。
3. 下一轮应进入 OTA-TODO-009，把 repo_bound/slot_bound 安装动作与 InstallEvidence 输出接到 precheck + verify + compatibility 之后。