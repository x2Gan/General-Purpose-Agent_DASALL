# OTA-TODO-011 BootConfirmationMonitor 骨架收敛

日期：2026-04-07
任务：OTA-TODO-011
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-011 定义为“实现 BootConfirmationMonitor 骨架”，验收要求是 confirm 成功与超时失败都可验证，且未确认默认失败。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.8、6.9 与 6.10.3 已冻结 BootConfirmationMonitor 的 success/fail 判据：显式 self-check success、health liveness/readiness、required heartbeat freshness、slot_bound version report 一致，以及 timeout/即时失败分流。
3. OTA-TODO-020 已完成 boot confirm 成功判据设计收敛，因此 011 的实现职责是把冻结语义落成内部骨架，而不是继续讨论判据边界。
4. OTA-TODO-010 与 OTA-TODO-012 已提供 SlotPlan、RollbackToken 与 `IBootControlAdapter` 语义锚点，011 可以直接围绕 confirm 闭环补内部 provider 和状态对象。

## 2. 研究学习结果

### 2.1 本地证据

1. `IBootControlAdapter` 已冻结 `get_active_target / set_next_boot / mark_boot_success / mark_boot_failed` 四个动作，因此 011 不应再泄漏 platform bootloader 细节。
2. `IHealthMonitor::get_snapshot()` 和 `HealthSnapshot` 已具备 `liveness / readiness / degraded / failed_components / version / timestamp`，足够承载 health gate 的 frozen semantics。
3. 现有 watchdog public surface 只有 aggregate snapshot，不含 confirm-critical entity freshness，因此 011 需要引入 ota 私有 `IHeartbeatFreshnessSource`，保持 public contracts 不变。

### 2.2 可落地启发

1. BootConfirmationMonitor 适合沿用 OTA 现有骨架模式：私有 request/policy/result 对象 + contract-shaped failure，不新增 public OTA types。
2. confirm timeout 设计文档已明确使用 `INF_E_OTA_BOOT_CONFIRM_TIMEOUT`，因此需要同步补充 infra 私有 error mapping，避免文档和实现分裂。
3. pending_confirm 不应通过异常或隐式状态传递，而应进入 monitor status，为下一轮 014 的 OTAHealthProbe 直接复用。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 BootConfirmationMonitor 私有 request/policy/provider 边界 | OTA 设计 6.10.3 | BootConfirmationMonitor.h | success signal / heartbeat freshness / version report 全部留在 ota 私有域 |
| D2 | 落地 success、pending、即时失败与 timeout 分流 | OTA 设计 6.8/6.9/6.10.3 | BootConfirmationMonitor.cpp | `await_confirm / evaluate_self_check / handle_timeout` 均可二值判定 |
| D3 | 对齐 confirm timeout 的 infra 私有错误码 | OTA 设计 6.9/错误码表 | InfraErrorCode.h/.cpp 与测试 | `INF_E_OTA_BOOT_CONFIRM_TIMEOUT` 有稳定 name 与 contracts 映射 |
| D4 | 补足 unit 与 contract 回归 | OTA TODO 011 验收要求 | BootConfirmationMonitorTest.cpp + InfraErrorCode tests | 成功、pending、timeout、explicit self-check fail 全覆盖 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| success 判据必须组合 self-check / health / heartbeat / version report | `await_confirm` 顺序检查 active target、self-check、health gate、heartbeat freshness、version report |
| health gate 未通过只能 pending 到 deadline | `await_confirm` 对 health gate 不满足返回 `Pending`，且不触发 boot mutation |
| self-check false / heartbeat stale / version mismatch 必须即时失败 | `await_confirm` 对上述条件直接 `mark_boot_failed` 并返回 `Failed` |
| confirm timeout 必须走稳定 infra error code | `handle_timeout` 统一映射 `INF_E_OTA_BOOT_CONFIRM_TIMEOUT` |

### 4.2 Build 三件套

1. 代码目标：新增 BootConfirmationMonitor 骨架，实现 `await_confirm / evaluate_self_check / handle_timeout`，并补充 confirm timeout 的 infra error mapping。
2. 测试目标：新增 BootConfirmationMonitorTest，覆盖 success、pending、timeout、explicit self-check fail；同步回归 InfraErrorCode unit/contract。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_boot_confirmation_monitor_unit_test dasall_infra_error_code_unit_test dasall_contract_infra_error_code_boundary_test`
   - `ctest --test-dir build-ci --output-on-failure -R "BootConfirmationMonitorTest|InfraErrorCodeUnitTest|InfraErrorCodeMappingContractTest"`
   - `cmake --build build-ci --target dasall_unit_tests`

### 4.3 D Gate

结论：PASS。

理由：

1. 011 只把 020 冻结的 confirm 语义落到 ota 私有骨架，不扩写 shared contracts，也不跨到 runtime 恢复主控。
2. timeout error code 通过 InfraErrorCode 私有域补齐，保持 outward 仍只落在既有 contracts result code 范畴内。

## 5. Build 落地结果

1. 新增 infra/src/ota/BootConfirmationMonitor.h 与 infra/src/ota/BootConfirmationMonitor.cpp，冻结 BootConfirmationRequest、BootSuccessSignal、HeartbeatFreshnessReport、VersionReportSnapshot、BootConfirmationResult、BootConfirmationMonitorStatus 以及三个私有 provider interface。
2. `BootConfirmationMonitor::evaluate_self_check` 现在显式区分三类状态：未收到信号时 pending、`self_check_ok=false` 时 terminal failure、显式 success 时放行后续 gate。
3. `BootConfirmationMonitor::await_confirm` 现在实现 frozen confirm 顺序：active target 校验 -> rollback token armed -> self-check -> health gate -> heartbeat freshness -> slot_bound version report -> `mark_boot_success`。
4. `BootConfirmationMonitor::handle_timeout` 会统一调用 `mark_boot_failed(target_slot)`，并通过 `INF_E_OTA_BOOT_CONFIRM_TIMEOUT` outward 映射到既有 contracts category。
5. 扩展 infra/include/InfraErrorCode.h 与 infra/src/InfraErrorCode.cpp，新增 `OTABootConfirmTimeout` 私有错误码及稳定名字/映射；同步回归 unit 与 contract 测试。
6. 新增 tests/unit/infra/ota/BootConfirmationMonitorTest.cpp，并更新 infra/tests CMake，使其进入 `dasall_infra`、`dasall_unit_tests` 与 `unit;ota` 标签。

## 6. Build 合规复核

1. 边界：所有新的 success signal、heartbeat freshness、version report 边界都停留在 ota 私有头文件，没有进入 `infra/include/ota`。
2. 根因处理：不是在调用方散落地拼 confirm if/else，而是把 frozen 判据集中到 BootConfirmationMonitor 自身。
3. 测试覆盖：正例覆盖完整 confirm success；pending 覆盖 health gate 未 ready；负例覆盖 explicit self-check fail 与 confirm timeout；error mapping 通过 unit/contract 双重回归锁定。
4. CMake：BootConfirmationMonitorTest 已被 `dasall_unit_tests` 聚合目标编入，新增的 InfraErrorCode contract 也可独立编译通过。

## 7. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_boot_confirmation_monitor_unit_test dasall_infra_error_code_unit_test dasall_contract_infra_error_code_boundary_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R "BootConfirmationMonitorTest|InfraErrorCodeUnitTest|InfraErrorCodeMappingContractTest"`：通过，3/3 tests passed。
4. `cmake --build build-ci --target dasall_unit_tests`：通过。
5. `dasall_unit_tests` 聚合门结果：170/170 tests passed，`ota = 8 tests`。

## 8. 结论

1. OTA-TODO-011 已把 boot confirm 从设计约束推进为可执行骨架，success/pending/fail/timeout 四条路径现在都能二值判定。
2. `INF_E_OTA_BOOT_CONFIRM_TIMEOUT` 已在实现层落地，并保持 private infra error -> existing contracts result code 的映射边界。
3. OTA-TODO-014 现在具备直接实现前置条件，可以围绕 `pending_confirm / last_error_code / detail_ref` 等信号补齐 OTAHealthProbe。