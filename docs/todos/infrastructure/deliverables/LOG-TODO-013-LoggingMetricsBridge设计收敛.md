# LOG-TODO-013 LoggingMetricsBridge 设计收敛

日期：2026-04-03  
任务：LOG-TODO-013  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 已在 LOG-BLK-002 解阻后把 LOG-TODO-013 迁移为 Not Started，并将任务收敛到“实现 LoggingMetricsBridge 指标桥接骨架 + unit/contract 定向验收”。
2. [docs/architecture/DASALL_infra_metrics模块详细设计.md](docs/architecture/DASALL_infra_metrics模块详细设计.md) 6.6.1/6.8.1 已冻结 provider/meter/sample 唯一路径、五指标对象表、MetricLabels 取值规则与 non-recursive failure semantics。
3. [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging模块详细设计.md) 6.10 已冻结 logging 侧 meter scope、五指标 family 与 stage/outcome/error_code 约束。

## 2. 实现收敛结论

1. 新增 internal [infra/src/logging/LoggingMetricsBridge.h](infra/src/logging/LoggingMetricsBridge.h) / [infra/src/logging/LoggingMetricsBridge.cpp](infra/src/logging/LoggingMetricsBridge.cpp)，以 `LoggingMetricKind`、`LoggingMetricSignal` 和 `LoggingMetricsEmitResult` 固化 bridge 的内部输入/输出边界。
2. bridge 只通过 `IMetricsProvider::get_meter(MeterScope{.name="infra.logging", .version="v1"})` 获取 meter，并在首次 emit 时预注册五个 frozen metric family；不触碰 `IMetricExporter`。
3. bridge 在本地先校验 stage/outcome/error_code 白名单，再构造 `MetricSample`；provider/meter 失败被归一到 `MetricsErrorCode` 与 `MetricsOperationStatus`，保证失败语义仍停留在 contracts `ResultCode` / `ErrorInfo` 边界内。
4. provider/exporter/config/identity 失败会把 bridge 标记为 degraded 或 no-op，但不会递归回 LoggingFacade 再写一条“metrics failed”日志，符合 LOG-BLK-002 的 non-recursive failure 约束。

## 3. Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| 五个 frozen metric family | `LoggingMetricsBridge` 在首次 emit 时统一注册 `logging_write_total`、`logging_write_fail_total`、`logging_drop_total`、`logging_queue_depth`、`logging_flush_latency_ms` |
| MetricLabels 五元组有限取值 | `LoggingMetricSignal::has_consistent_values()` 与 bridge 本地 sample 校验拒绝非法 stage/outcome/error_code |
| non-recursive failure | `LoggingMetricsEmitResult` 暴露 `bridge_degraded` + `metrics_error_code` + `MetricsOperationStatus`，失败只进入 bridge-local state |
| unit + contract 定向验证 | fake provider/meter 覆盖成功发射、record 失败降级、非法 stage 拒绝，以及 frozen scope/identity/label contract |

## 4. 测试闭环

1. unit：新增 [tests/unit/infra/logging/LoggingMetricsBridgeTest.cpp](tests/unit/infra/logging/LoggingMetricsBridgeTest.cpp)，覆盖成功发射、provider timeout 降级、非法 stage 本地拒绝。
2. contract：新增 [tests/contract/smoke/LoggingMetricsBridgeBoundaryContractTest.cpp](tests/contract/smoke/LoggingMetricsBridgeBoundaryContractTest.cpp)，覆盖 frozen meter scope、五指标 family、contracts error type 边界与 label allowlist。
3. CMake：更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)、[tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)、[tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)，注册 `dasall_logging_metrics_bridge_unit_test` 与 `dasall_contract_logging_metrics_bridge_boundary_test`。

## 5. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_logging_metrics_bridge_unit_test dasall_contract_logging_metrics_bridge_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "(LoggingMetricsBridgeTest|LoggingMetricsBridgeBoundaryContractTest)"`：发现 2 个新增测试。
4. `ctest --test-dir build-ci --output-on-failure -R "(LoggingMetricsBridgeTest|LoggingMetricsBridgeBoundaryContractTest)"`：2/2 通过。
5. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：110/110 通过。
7. `ctest --test-dir build-ci --output-on-failure -L contract`：132/132 通过。

## 6. 后续边界

1. 本轮只完成 bridge skeleton，不把它接入 LoggingFacade / SinkDispatcher 主写入链，也不注册到 `dasall_infra` 静态库源码列表；这两件事继续留给后续 wiring 任务承接。
2. metrics 子域真实 runtime/exporter 仍可独立推进，但不再阻塞 logging 侧 bridge contract 和测试收敛。