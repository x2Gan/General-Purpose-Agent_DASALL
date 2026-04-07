# OTA-TODO-007 PackageVerifier 骨架收敛

日期：2026-04-07
任务：OTA-TODO-007
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 已将 OTA-TODO-007 定义为“实现 PackageVerifier 骨架”，验收要求是签名失败、hash 失败、release_counter 回退三类失败路径必须可观测且不得进入安装。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.2/6.8/6.9 已冻结 PackageVerifier 的职责边界：校验 manifest、签名、hash、release_counter，并在 verify 失败时直接短路。
3. OTA-TODO-019 已在 OTA 详细设计 6.10.1 中冻结 `ed25519` / `ecdsa-p256-sha256` allow-list、`ITrustAnchorProvider.load_active_anchor(...)` 只读接口以及 `INF_E_OTA_VERIFY_FAIL` outward 映射，因此 007 已无前置 blocker。
4. OTA-TODO-006 已完成 side-effect-free precheck 骨架，本轮只需把 verify gate 接到 precheck 之后，不提前扩张到 compatibility/install/switch。

## 2. 研究学习结果

### 2.1 本地证据

1. OTA 设计 6.10.1 明确 `verify_required=true` 时算法配置为空、未知，或与 trust anchor 不匹配都必须短路失败，不得退化为 hash-only 校验。
2. OTA 设计 6.9.2 明确 verify_fail 应清理 staging 并保留失败证据；在 install/switch 尚未落盘前，本轮最小目标是保证 verify 失败返回 contract-shaped failure，而不是继续放行到 install。
3. infra 私有码域当前只冻结了 `INF_E_OTA_VERIFY_FAIL` 与 `INF_E_OTA_ROLLBACK_FAIL`；因此 007 对外仍必须落到 contracts 既有 ResultCode，而不能新增新的公共错误枚举。

### 2.2 外部参考

1. Uptane 设备端验证模型强调元数据真实性、内容哈希和版本单调性必须在安装前完成，rollback/freeze 风险需要通过 release counter 或等价 monotonic version gate 拦截；这直接支撑 DASALL 在 PackageVerifier 中强制 `release_counter` floor。
2. RAUC 的 bundle info/install 流程同样先验证 bundle 签名，再进入安装，且信任链材料由系统侧 keyring 提供；这支持 DASALL 把 trust anchor 读取和 signature verifier adapter 都收敛成 PackageVerifier 的只读依赖，而不是让 install 参与验证。

### 2.3 可落地启发

1. PackageVerifier 可以先冻结三个 internal 依赖面：trust anchor provider、policy provider、signature verifier adapter，而无需提前绑定 secret/config 的真实 facade。
2. 对外失败统一复用 `INF_E_OTA_VERIFY_FAIL -> contracts::ValidationFieldMissing` 映射，细分原因留在 message/stage/source_ref 即可满足当前 contracts 边界。
3. artifact 级 hash 校验可以与 package 验证共用同一个 adapter 抽象，从而在 007 里把 `verify_package/verify_artifact` 两个 public 入口都变成可测骨架。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 trust anchor / policy / signature adapter 三面依赖 | OTA 设计 6.10.1 | PackageVerifier.h | 依赖全为只读、可注入测试 |
| D2 | 收敛 package verify 主失败路径 | OTA 设计 6.8/6.9 | PackageVerifier.cpp | signature/hash/release_counter 三类失败可二值判定 |
| D3 | 覆盖 artifact verify 边界 | IOTAPackageVerifier public interface | PackageVerifier.cpp / PackageVerifierTest.cpp | verify_artifact 也能显式返回 hash failure |
| D4 | 接线 unit 构建与 discoverability | 现有 infra/tests CMake | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt | 新测试进入 `dasall_unit_tests` 和 `unit;ota` 标签 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| 只读 trust anchor provider 承接 secret 侧边界 | 新增 TrustAnchorMaterial / ITrustAnchorProvider |
| 验签策略通过 policy snapshot 冻结 | 新增 PackageVerifierPolicy / IPackageVerifierPolicyProvider |
| 签名/哈希/release_counter 全部留在 adapter 输出 | 新增 PackageVerificationReport / ArtifactVerificationReport / ISignatureVerifierAdapter |
| verify 失败统一快返，install 不可达 | PackageVerifier.cpp failure helper |
| 新增 OTA verifier 单测进入 unit 聚合图 | tests/unit/infra/ota/PackageVerifierTest.cpp 和 CMake 接线 |

### 4.2 Build 三件套

1. 代码目标：新增 PackageVerifier internal 骨架，实现 `IOTAPackageVerifier::verify_package/verify_artifact`。
2. 测试目标：新增 OTAPackageVerifierTest，覆盖 success、signature fail、hash fail、release_counter rollback、artifact hash fail。
3. 验收命令：
   - `cmake --build build-ci --target dasall_infra dasall_ota_package_verifier_unit_test`
   - `ctest --test-dir build-ci -N -R "OTAPackageVerifierTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "OTAPackageVerifierTest"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

### 4.3 D Gate

结论：PASS。

理由：

1. OTA-TODO-003/006 已完成，OTA-TODO-019 也已解阻 trust anchor 设计缺口，因此 007 已具备直接进入 Build 的前提。
2. 本轮保持在 `infra/src/ota` 与 `tests/unit/infra/ota` 范围内，没有提前扩张到 compatibility/install/switch。

## 5. Build 落地结果

1. 新增 infra/src/ota/PackageVerifier.h 与 infra/src/ota/PackageVerifier.cpp，冻结 `TrustAnchorMaterial`、`PackageVerifierPolicy`、`PackageVerificationReport`、`ArtifactVerificationReport` 与三类 internal provider/adapter 接口。
2. PackageVerifier 现在实现 `IOTAPackageVerifier`：
   - `verify_package`：校验 package descriptor、策略快照、allow-list 算法、trust anchor 加载、signature/hash 结果和 release_counter monotonic floor；
   - `verify_artifact`：校验 artifact descriptor 与 artifact hash 结果。
3. 所有 verify 失败统一通过 `INF_E_OTA_VERIFY_FAIL` outward 映射返回 contracts 既有失败语义，不引入新的 public error code。
4. 新增 tests/unit/infra/ota/PackageVerifierTest.cpp，覆盖 success、signature fail、hash fail、release_counter rollback、artifact hash fail 五类路径。
5. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt 与 tests/unit/infra/CMakeLists.txt，将 PackageVerifier 骨架和 `OTAPackageVerifierTest` 接入 `dasall_infra`、`dasall_unit_tests` 和 `unit;ota` 标签。

## 6. Build 合规复核

1. 代码注释：本轮未新增注释；边界类型和 helper 命名已把 trust anchor / policy / adapter / verify gate 语义显式化，代码保持自解释。
2. 正负例：已覆盖 success 正例，以及 signature fail、hash fail、release_counter rollback、artifact hash fail 负例。
3. 测试发现性：已通过 `ctest -N -R "OTAPackageVerifierTest"` 验证新单测被 ctest 发现，并通过 `-L unit` 确认未破坏仓库 unit 聚合门。
4. TODO/交付物/worklog：本轮同步回写专项 TODO、deliverable 与 worklog。
5. 提交前状态隔离：本轮仅包含 OTA-TODO-007 相关源码、单测、CMake 和文档回写文件。

## 7. 验证结果

1. `cmake --build build-ci --target dasall_infra dasall_ota_package_verifier_unit_test`：通过。
2. `ctest --test-dir build-ci -N -R "OTAPackageVerifierTest"`：发现 1 个定向测试。
3. `ctest --test-dir build-ci --output-on-failure -R "OTAPackageVerifierTest"`：通过，1/1 tests passed。
4. `cmake --build build-ci --target dasall_unit_tests`：通过。
5. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，164/164 tests passed。

## 8. 结论

1. OTA-TODO-007 已把 OTA verify gate 从“只有接口定义”推进到“存在可注入 trust anchor / policy / signature adapter 的可执行骨架”。
2. signature fail、hash fail 和 release_counter rollback 现在都能在 install 之前快返 contract-shaped failure，满足 OTA 设计对 verify 阶段的最小闭环要求。
3. 下一轮可进入 OTA-TODO-008，把 artifact compatibility 判定从“包和工件已被验证”推进到“hardware/profile/dependency_refs 冲突拒绝”。