# LOG-TODO-017 LoggingHealthProbe 骨架收敛

日期：2026-04-03  
任务：LOG-TODO-017  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 已将 `LOG-TODO-017` 定义为“实现 LoggingHealthProbe 健康探针骨架”，验收要求为 descriptor 合法、Healthy/Degraded/Unhealthy 三态映射稳定，以及 timeout failure 结构化返回。
2. [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging模块详细设计.md) 6.10.1 已冻结 `LoggingHealthProbe` 的 descriptor 固定值、输入信号、三态映射与 `detail_ref` 命名空间。
3. [docs/architecture/DASALL_infra_health模块详细设计.md](docs/architecture/DASALL_infra_health模块详细设计.md) 6.5/6.6 已冻结 `IHealthProbe`、`ProbeDescriptor`、`ProbeResult` 与“超时返回结构化失败”的通用语义。
4. 现有 logging 运行时骨架已经暴露 probe 所需的本地信号来源：
   - `SinkDispatcher::queue_depth()` / `dropped_total()`
   - `LoggingRecovery::is_degraded()` / `fallback_active()` / `recovery_failure_total()`
   - `LoggingMetricsBridge::is_degraded()`

## 2. 研究学习结果

### 2.1 本地证据

1. `LoggingHealthProbe` 只能实现 `IHealthProbe::probe()`，不能新增 logging 私有 health result，也不能把恢复裁定权带回 logging 主链。
2. health 通用契约要求 `ProbeResult` 在超时、异常、降级场景下仍然保留 contracts 可映射的 `error_code` 与可定位的 `detail_ref`。
3. 当前 logging 主链已经具备 queue depth、drop total、recovery degraded/fallback 与 metrics bridge degraded 等观测面；缺口主要是“如何把这些信号以只读、受限的方式汇聚成 probe”。

### 2.2 外部参考

1. Kubernetes readiness/liveness probe 指南强调：readiness probe 应保持低成本、持续运行，临时无法服务时应先标记 unready/degraded，而不是把探针本身实现成会拖垮主流程的重逻辑；timeout 也必须被当作明确的 probe failure 处理，而不是静默忽略。

### 2.3 可落地启发

1. `LoggingHealthProbe` 应只读取本地信号快照，不直接驱动 recovery、flush 或任何写入操作。
2. timeout 路径应返回结构化失败并保留 `detail_ref`，但不把 logging 主链卡在 probe 上。
3. 队列高水位阈值不应硬编码进 public contract；首版骨架通过内部 signal provider 注入高水位信息，避免扩张配置面。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 `LoggingHealthProbe` 的内部 provider 注入边界 | logging 设计 6.10.1；health 设计 6.6 | internal `ILoggingHealthSignalProvider` / signal snapshot | provider 只读、无副作用，且不扩写 public contract |
| D2 | 收敛三态映射与 timeout failure 规则 | logging 设计 6.10.1；health 设计 6.5/6.8 | status/detail_ref/error_code 映射 | Healthy/Degraded/Unhealthy 与 timeout failure 可二值判定 |
| D3 | 锁定 Build 三件套 | `LOG-TODO-017` 验收命令；tests/unit 现状 | 代码目标、测试目标、验收命令 | 三件套完整，允许进入 Build |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| 使用 internal `ILoggingHealthSignalProvider` 汇聚只读信号 | 新增 `infra/src/logging/LoggingHealthProbe.h/.cpp`，不改 public health contract |
| queue depth 高水位阈值保持在 internal signal snapshot 中 | unit 测试验证 queue 高水位触发 Degraded，而不新增配置键 |
| timeout 走结构化失败 `ProbeResult` | unit 测试验证 `ProviderTimeout` + `detail_ref` + Degraded 状态 |
| descriptor 固定为 `infra.logging.pipeline` / `readiness` / `Critical` / `5000ms` / `100ms` | unit 测试验证 descriptor 合法性与冻结值 |

### 4.2 Build 三件套

1. 代码目标：新增 `infra/src/logging/LoggingHealthProbe.h` 与 `infra/src/logging/LoggingHealthProbe.cpp`，并接入 `infra/CMakeLists.txt`。
2. 测试目标：新增 `tests/unit/infra/logging/LoggingHealthProbeTest.cpp`，覆盖 descriptor、Healthy/Degraded/Unhealthy、timeout failure 四类断言，并接入 `tests/unit/infra/CMakeLists.txt` 与 `tests/unit/CMakeLists.txt`。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_logging_health_probe_unit_test dasall_unit_tests`
   - `ctest --test-dir build-ci -N -R "LoggingHealthProbeTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "LoggingHealthProbeTest"`
   - `ctest --test-dir build-ci -N -L logging`
   - `ctest --test-dir build-ci --output-on-failure -L logging`

### 4.3 D Gate

结论：PASS。

理由：

1. provider 注入接口、状态映射与 timeout failure 的设计出口已经明确，不再依赖额外 blocker。
2. Build 三件套已锁定，且范围仍限定在 `infra/src/logging`、`tests/unit/infra/logging`、专项 TODO/交付物/worklog 之内。
3. 本轮不会新增 public health interface，也不会触碰 runtime 恢复裁定边界。

## 5. Build 合规提醒

1. 代码只允许读本地 logging 健康信号，不得在 `probe()` 内触发写日志、重试、flush 或 recovery 侧效果。
2. 测试必须覆盖至少 1 个正例和 1 个负例；本轮以 Healthy 作为正例，以 timeout failure 作为负例，并补齐 Degraded/Unhealthy 状态映射断言。
3. 由于本轮会触及 unit test 注册与 logging 标签 discoverability，Build 阶段除定向测试外还必须补 `ctest -N -L logging` 与 `ctest -L logging`。

## 6. Build 落地结果

1. 新增 [infra/src/logging/LoggingHealthProbe.h](infra/src/logging/LoggingHealthProbe.h) 与 [infra/src/logging/LoggingHealthProbe.cpp](infra/src/logging/LoggingHealthProbe.cpp)，用 internal `ILoggingHealthSignalProvider` 汇聚 queue depth、高水位阈值、drop delta、recovery degraded/fallback、unrecoverable failure 与 metrics bridge degraded 六类本地信号。
2. `LoggingHealthProbe` 直接实现 `IHealthProbe::probe()`，固定输出 frozen descriptor，并将：
   - queue 高水位 / drop / recovery degraded / fallback / metrics degraded 映射为 Degraded
   - unrecoverable failure 映射为 Unhealthy
   - timeout 映射为带 `ProviderTimeout` 的结构化 Degraded failure
3. 新增 [tests/unit/infra/logging/LoggingHealthProbeTest.cpp](tests/unit/infra/logging/LoggingHealthProbeTest.cpp)，覆盖 descriptor 固定值、Healthy、Degraded、Unhealthy 与 timeout failure 五类断言。
4. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)、[tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 与 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)，将 `LoggingHealthProbe` 纳入 `dasall_infra` 与 `logging` 标签测试面。

## 7. 验证结果

1. `ctest --test-dir build-ci -N -R "LoggingHealthProbeTest"`：发现 1 个定向测试。
2. `ctest --test-dir build-ci --output-on-failure -R "LoggingHealthProbeTest"`：1/1 通过。
3. `ctest --test-dir build-ci -N -L logging`：发现 24 个 logging 标签测试。
4. `ctest --test-dir build-ci --output-on-failure -L logging`：24/24 通过。
5. `ctest --test-dir build-ci --output-on-failure -L unit`：111/111 通过。
6. `ctest --test-dir build-ci -N`：发现 252 个全量测试。

## 8. 结论

1. `LOG-TODO-017` 已从“边界已冻结但实现未落盘”推进到“internal provider + status mapping + timeout failure 骨架已可执行验证”。
2. `LoggingHealthProbe` 没有引入新的 public health interface，也没有把恢复裁定或 diagnostics 查询职责带回 logging 主链，仍然满足 ADR-007/008 的边界要求。
3. 017 完成后，logging 专项剩余未完成原子任务收敛到 `LOG-TODO-019`。