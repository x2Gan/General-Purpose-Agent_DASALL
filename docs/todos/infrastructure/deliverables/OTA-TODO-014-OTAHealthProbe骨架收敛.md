# OTA-TODO-014 OTAHealthProbe 骨架收敛

日期：2026-04-07
任务：OTA-TODO-014
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-014 定义为“实现 OTAHealthProbe 骨架”，验收要求是 degraded 条件可判定，且 pending_confirm 计数准确。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.3 明确 OTAHealthProbe 的输入是“历史结果、当前 backlog、confirm state”，输出是 `ProbeResult`，且职责只能提供事实，不裁定恢复策略。
3. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.9.2 与 6.9.3 要求 rollback_fail、confirm timeout 和 pending_confirm 都必须进入健康出口，便于后续 gate 与运维查询。
4. OTA-TODO-011 已完成 BootConfirmationMonitor 骨架，因此 014 现在可以直接围绕 `pending_confirm / last_failure_code / detail_ref` 等 OTA 事实信号落健康桥接。

## 2. 研究学习结果

### 2.1 本地证据

1. `ProbeDescriptor` / `ProbeResult` 已冻结 health probe 公共边界，因此 014 只需要补 ota 私有 signal provider，不应再发明新的共享类型。
2. `OTAStatusSnapshot` 已提供 `last_plan_id / state / active_slot / pending_confirm / last_failure_code / backlog_count`，足够承载 OTAHealthProbe 的最小事实输入。
3. PolicyHealthProbe 现有模式已经验证了“私有 sample -> ProbeResult”的落地路径，014 直接复用这一结构最稳妥。

### 2.2 可落地启发

1. pending_confirm 更适合在 ota 私有 `OTAHealthSignals` 中暴露为 `pending_confirm_count()`，后续 gauge 或 diagnostics 直接复用，不需要扩写 ProbeResult。
2. backlog、recent failure、audit/rollback degraded 都应该映射为 `ProbeStatus::Degraded` 的事实状态，而不是 synthetic execution failure。
3. provider timeout 和 invalid sample 仍要走标准 ProbeResult failure 映射，避免健康查询自身失真时被误判成业务事实。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 OTAHealthProbe 私有 signal/sample 边界 | OTA 设计 6.3/6.11 | OTAHealthProbe.h | backlog、pending_confirm、last_failure 都停留在 ota 私有域 |
| D2 | 把 OTA 事实状态映射到 ProbeResult | OTA 设计 6.9 | OTAHealthProbe.cpp | healthy / degraded / timeout / invalid 四条路径可二值判定 |
| D3 | 保证 pending_confirm 与 backlog 可观测 | OTA TODO 014 验收要求 | OTAHealthSignals + OTAHealthProbeTest | pending_confirm count=0/1 可直接测试 |
| D4 | 补齐 unit/CMake 发现性 | OTA TODO 014 验收要求 | OTAHealthProbeTest.cpp 与 infra/tests CMake | 新单测进入 `dasall_unit_tests` 与 `unit;ota` 标签 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| OTAHealthProbe 只能输出事实，不裁定恢复策略 | 新增 `OTAHealthSignals / OTAHealthSample / IOTAHealthSignalProvider`，由 provider 提供事实，probe 只映射状态 |
| pending_confirm 与 backlog 需要可直接观测 | `detail_ref` 编码 `pending_confirm/count/<n>/backlog/<m>`，并新增 `pending_confirm_count()` helper |
| recent failure / rollback degraded / audit degraded 需要进入 degraded 健康状态 | `map_status` 统一把这些信号映射到 `ProbeStatus::Degraded` |
| provider timeout/invalid 需要结构化失败 | `probe()` 对 timeout/invalid 返回 contract-shaped failure ProbeResult |

### 4.2 Build 三件套

1. 代码目标：新增 OTAHealthProbe 骨架，实现 ota 私有 sample/provider 边界与 `ProbeResult` 映射。
2. 测试目标：新增 OTAHealthProbeTest，覆盖 frozen descriptor、pending_confirm count、degraded 条件、timeout failure。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_ota_health_probe_unit_test`
   - `ctest --test-dir build-ci -N -R "OTAHealthProbeTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "OTAHealthProbeTest"`
   - `cmake --build build-ci --target dasall_unit_tests`

### 4.3 D Gate

结论：PASS。

理由：

1. 014 只新增 ota 私有 health bridge，不引入新的 shared contracts，也不越权做 runtime 恢复裁定。
2. pending_confirm/backlog/last_failure 现在都能通过统一 ProbeResult 暴露，为后续 gate、metrics 和 diagnostics 提供直接事实输入。

## 5. Build 落地结果

1. 新增 infra/src/ota/OTAHealthProbe.h 与 infra/src/ota/OTAHealthProbe.cpp，冻结 `OTAHealthSignals`、`OTAHealthSample`、`IOTAHealthSignalProvider` 与 `OTAHealthProbe`。
2. `OTAHealthSignals` 现在提供 `pending_confirm_count()` 与 `has_recent_failure()`，把 pending_confirm 和 last_failure 从 ota 私有状态转成可直接被 probe 使用的事实 helper。
3. `OTAHealthProbe::map_status` 把 audit/rollback degraded、pending_confirm、backlog、last_failure 统一映射到 `ProbeStatus::Degraded`；正常 idle/ready 状态映射为 `Healthy`。
4. `OTAHealthProbe::detail_ref_for_state` 现在会把 pending_confirm/backlog 计数、last_failure category、rollback/audit degraded 细节编码到 `status://ota/health/...` 命名空间。
5. 新增 tests/unit/infra/ota/OTAHealthProbeTest.cpp，覆盖 frozen descriptor、pending_confirm count、pending_confirm/backlog degraded、recent failure degraded、timeout failure。
6. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt 与 tests/unit/infra/CMakeLists.txt，把 OTAHealthProbe 源码和 OTAHealthProbeTest 接入 `dasall_infra`、`dasall_unit_tests` 与 `unit;ota` 标签。

## 6. Build 合规复核

1. 边界：014 只桥接 OTA 健康事实，不扩 public OTA/health contract；恢复决策仍留在 runtime 与后续 orchestration。
2. 根因处理：把 OTA backlog/pending_confirm/last_failure 的分散事实收敛到 OTAHealthProbe，而不是继续要求调用方自行拼接健康语义。
3. 测试覆盖：正例覆盖 healthy 与 degraded 事实；负例覆盖 provider timeout；helper 覆盖 pending_confirm count 语义。
4. CMake：OTAHealthProbeTest 已进入 `dasall_unit_tests` 聚合目标，`ctest -N` 可以发现该新测试。

## 7. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_ota_health_probe_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "OTAHealthProbeTest"`：发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "OTAHealthProbeTest"`：通过，1/1 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `dasall_unit_tests` 聚合门结果：171/171 tests passed，`ota = 9 tests`。

## 8. 结论

1. OTA-TODO-014 已把 OTA 健康出口从设计约束推进为可执行骨架，backlog、pending_confirm、last_failure 和 timeout 现在都有稳定的 ProbeResult 映射。
2. OTA 的审计桥、boot confirm、健康桥三条观测链现在都已具备独立落地骨架，可以继续进入更高层的 manager/integration 接线任务。
3. 用户请求中的 013~014 串行推进已闭环完成；后续若继续推进 OTA，可转向 015/016/017 把新增骨架接到更完整的测试与构建入口。