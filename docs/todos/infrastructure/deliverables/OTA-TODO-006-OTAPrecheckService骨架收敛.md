# OTA-TODO-006 OTAPrecheckService 骨架收敛

日期：2026-04-07
任务：OTA-TODO-006
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 已将 OTA-TODO-006 定义为“实现 OTAPrecheckService 骨架”，验收出口是 health/resource/policy 任一 hard-fail 都能阻断 apply，且 precheck 失败无副作用。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.2/6.3/6.8/6.10 已冻结 OTAPrecheckService 的输入面、前置条件、precheck 失败即时返回语义，以及 `infra.ota.precheck.*` 阈值键。
3. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.4/6.6 已冻结 infra 只能通过 contracts ResultCode/ErrorInfo 暴露失败语义，不能为 OTA 预检新增共享错误模型。
4. 当前代码现状中 infra/include/ota 已冻结 IOTAManager/OTATypes，但 infra/src/ota 仍为空，因此本轮可以只补 internal precheck 骨架和 unit 验证，而不提前扩张到 verifier/install/switch 主链。

## 2. 研究学习结果

### 2.1 本地证据

1. OTA 设计 6.8.2 明确 `validate_only` 只允许执行 precheck/verify/compatibility，不得写 slot 或 boot target，因此 precheck 骨架必须保持纯读、零副作用。
2. OTA 设计 6.9.2 明确预检失败应“立即返回，不产生副作用，记录 audit 和指标”；在审计/指标桥尚未落盘前，本轮至少要把失败结果结构化为 contract-shaped blocking reasons。
3. OTA 设计 6.10 冻结了 `infra.ota.mode`、`infra.ota.precheck.min_free_space_mb`、`infra.ota.precheck.max_cpu_load_pct`、`infra.ota.precheck.require_health_ready`，这意味着 precheck 需要显式建模 policy/resource/health 三个内部输入面，而不是把阈值硬编码在测试里。

### 2.2 外部参考

1. Android A/B OTA 文档强调更新前必须保持当前活动槽位可启动，且只有在验证、写入和切换完成后才激活新槽位；这支持 DASALL 把 precheck 设计成严格的前置只读 gate，而不是把资源/健康判断混入 install 阶段。
2. RAUC `install-check`/`pre-install` hook 允许在真正安装前基于兼容性或系统状态拒绝升级，并要求失败返回显式原因；这支持 DASALL 在 OTAPrecheckService 中把 policy/health/resource hard-fail 统一映射成可审计的 blocking reasons。

### 2.3 可落地启发

1. 预检骨架可以先冻结 internal provider 边界，把 health/resource/policy 作为只读依赖注入，而不是提前绑定 health/config 的真实 facade。
2. `validate_only` 应该在 `dry_run` 下保持可通过，避免把“允许预检”与“允许 apply”混成同一条 policy gate。
3. 仓库级 `unit` 聚合 gate 才是本轮的真实验收门，因此 discoverability 和全量 `-L unit` 回归必须纳入证据，而不是只跑定向单测。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 internal precheck 依赖边界 | OTA 设计 6.3/6.10 | OTAPrecheckService.h | health/resource/policy 只读 provider 可单独注入测试 |
| D2 | 收敛 precheck 四维 gate | OTA 设计 6.8/6.9 | OTAPrecheckService.cpp | compatibility/health/resource/policy 任一失败都能给出二值结果 |
| D3 | 固化 unit 回归矩阵 | OTA TODO 006 验收要求 | OTAPrecheckServiceTest.cpp | 正例 + invalid plan/health/resource/policy 负例都可二值判定 |
| D4 | 接线构建与 unit discoverability | 现有 infra/tests CMake | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt | 新测试进入 `dasall_unit_tests` 和 `unit` 标签 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| precheck 只读依赖分为 health/resource/policy 三面 | 新增 infra/src/ota/OTAPrecheckService.h/.cpp |
| `validate_only` 允许在 `dry_run` 下通过，apply 仍需 `apply_enabled` | OTAPrecheckService.cpp policy gate |
| invalid plan 与 hard-fail 统一输出 contracts ErrorInfo | OTAPrecheckService.cpp blocking reason helper |
| 正负例覆盖 invalid plan、health fail、resource fail、policy fail | tests/unit/infra/ota/OTAPrecheckServiceTest.cpp |
| 新骨架进入 infra 和 unit 聚合图 | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：新增 OTAPrecheckService internal 骨架，冻结 OTAMode、policy/resource/health snapshot 和 provider 注入边界。
2. 测试目标：新增 OTAPrecheckServiceTest，覆盖 ready apply、validate_only in dry_run、invalid plan、health fail、resource fail、policy fail。
3. 验收命令：
   - `cmake -S . -B build-ci`
   - `cmake --build build-ci --target dasall_infra dasall_ota_precheck_service_unit_test`
   - `ctest --test-dir build-ci -N -R "OTAPrecheckServiceTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "OTAPrecheckServiceTest"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

### 4.3 D Gate

结论：PASS。

理由：

1. OTA-TODO-001/002 已完成，006 不依赖 verifier/install/switch/rollback 设计冻结，可独立进入 Build。
2. 本轮保持在 `infra/src/ota` 和 `tests/unit/infra/ota` 边界内，只引入一个 direct blocker fix 来恢复仓库级 unit 验收，不扩张到 diagnostics 工作包实现。

## 5. Build 落地结果

1. 新增 infra/src/ota/OTAPrecheckService.h 与 infra/src/ota/OTAPrecheckService.cpp，冻结 `OTAMode`、`OTAHealthSnapshot`、`OTAResourceSnapshot`、`OTAPrecheckPolicy` 以及三类 provider 接口。
2. OTAPrecheckService 将 precheck 明确拆成四个 gate：
   - `compatibility_ok`：当前用 UpgradePlan 结构合法性承接 precheck 输入完整性；
   - `policy_ok`：约束 `enabled`、`mode` 与 repeated-failure freeze；
   - `health_ok`：约束 readiness gate；
   - `resource_ok`：约束 free space / cpu load 阈值。
3. 所有失败路径统一映射到 contracts 既有 `ValidationFieldMissing`、`PolicyDenied`、`ProviderTimeout`，不新增共享错误语义。
4. 新增 tests/unit/infra/ota/OTAPrecheckServiceTest.cpp，覆盖 2 个正例和 4 类 hard-fail 负例。
5. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 OTA precheck 骨架与单测接入 `dasall_infra`、`dasall_unit_tests` 与 `unit;ota` 标签。
6. 为恢复本轮 `dasall_unit_tests` 聚合验收，同轮最小修复 tests/unit/infra/DiagnosticsSnapshotExportTest.cpp 中对 `CommandExecutionResult::success(...)` 的过期 4 参调用，补齐 `latency_ms` 参数；该修改只用于清除 direct validation blocker，不改变 diagnostics 行为边界。

## 6. Build 合规复核

1. 代码注释：本轮未额外补注释；依赖边界、gate 语义和错误构造均通过命名清晰的 snapshot/provider/helper 表达，代码保持自解释。
2. 正负例：已覆盖 ready apply、validate_only 正例，以及 invalid plan、health fail、resource fail、policy fail 负例。
3. 测试发现性：已通过 `ctest -N -R "OTAPrecheckServiceTest"` 验证新增 OTA 单测进入 ctest；并通过 `-L unit` 验证未破坏仓库 unit 聚合门。
4. TODO/交付物/worklog：本轮同步回写专项 TODO、deliverable 与 worklog。
5. 提交前状态隔离：除 OTA-TODO-006 本轮文件外，只包含 direct blocker fix 文件 tests/unit/infra/DiagnosticsSnapshotExportTest.cpp。

## 7. 验证结果

1. `cmake -S . -B build-ci`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_ota_precheck_service_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "OTAPrecheckServiceTest"`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "OTAPrecheckServiceTest"`：通过，1/1 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过；其间 direct blocker fix 恢复了 diagnostics snapshot export 测试的编译。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，163/163 tests passed。

## 8. 结论

1. OTA-TODO-006 已把 OTA 从“只有 public interface、没有 precheck runtime skeleton”推进到“存在 internal precheck provider 边界 + 四维 gate + unit 回归”的可执行状态。
2. precheck 失败现在可以在不触碰 install/switch/rollback 的前提下返回二值可判定结果，并保持失败语义停留在 contracts 既有 ResultCode/ErrorInfo 边界内。
3. 下一轮可按专项 TODO 串行进入 OTA-TODO-007，把 package/signature/trust-anchor 验证闭环补到 precheck 之后的 verify gate。