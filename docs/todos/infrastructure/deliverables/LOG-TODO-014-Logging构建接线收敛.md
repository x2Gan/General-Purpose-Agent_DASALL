# LOG-TODO-014 Logging 构建接线收敛

日期：2026-04-03  
任务：LOG-TODO-014  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 将 LOG-TODO-014 定义为“注册 logging 构建落点到 infra CMake”，完成判定是 `dasall_infra` 不再依赖 placeholder，且 logging 相关测试目标可继续链接。
2. [docs/worklog/DASALL_开发执行记录.md](docs/worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) 记录 #066 已明确指出：`LoggingMetricsBridge` 等 logging skeleton 已经落盘，但尚未接入 `dasall_infra` 静态库源码列表。
3. 当前 [infra/CMakeLists.txt](infra/CMakeLists.txt) 只编译 core/tracing 源文件，logging 行为骨架主要依赖 unit/contract 目标把 `infra/src/logging/*.cpp` 直接加入测试可执行文件，未形成真正的 infra 主库构建入口。

## 2. 收敛结论

1. 在 [infra/CMakeLists.txt](infra/CMakeLists.txt) 中新增 `DASALL_INFRA_LOGGING_SOURCES`，把 `AsyncQueueController.cpp`、`AuditLinkAdapter.cpp`、`LoggingConfigAdapter.cpp`、`LoggingFacade.cpp`、`LoggingMetricsBridge.cpp`、`LoggingRecovery.cpp`、`SinkDispatcher.cpp` 正式纳入 `dasall_infra`。
2. 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 与 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)，移除 logging 测试目标对同一批 `infra/src/logging/*.cpp` 的重复编译，改为统一链接 `dasall_infra`，避免后续主库接线后出现双份实现和链接漂移。
3. logging 单测和契约测试继续保留 `infra/src` 头文件搜索路径，用于包含 internal headers，但实现代码只保留主库唯一编译入口，形成清晰的“主库产物 + 测试链接”结构。

## 3. Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| logging skeleton 应成为 infra 主库正式源码，而非测试私有实现副本 | 在 `infra/CMakeLists.txt` 增加 `DASALL_INFRA_LOGGING_SOURCES` 并纳入 `target_sources(dasall_infra ...)` |
| 测试可以包含 internal headers，但不应再次编译相同实现文件 | 从 unit/contract logging 目标中移除 `infra/src/logging/*.cpp`，保留 `target_include_directories(... infra/src)` |
| 构建接线必须能稳定支撑既有 logging 测试矩阵 | 用显式 `cmake --build` 同时构建 `dasall_infra` 和 11 个受影响的 logging unit/contract 目标，再执行定向 `ctest` |

## 4. 验证闭环

1. `Build_CMakeTools` 与 `RunCtest_CMakeTools` 仍返回“无法配置项目”，本轮沿用仓库已验证的显式 CMake 链路作为兜底验证。
2. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
3. `cmake --build build-ci --target dasall_infra dasall_logging_facade_unit_test dasall_sink_dispatcher_unit_test dasall_async_queue_controller_unit_test dasall_audit_link_adapter_unit_test dasall_logging_recovery_unit_test dasall_logging_config_merge_unit_test dasall_logging_metrics_bridge_unit_test dasall_contract_sink_dispatcher_boundary_test dasall_contract_audit_link_adapter_boundary_test dasall_contract_log_configurator_boundary_test dasall_contract_logging_metrics_bridge_boundary_test`：通过。
4. `ctest --test-dir build-ci -N -R "(LoggingFacadeTest|SinkDispatcherTest|AsyncQueueControllerTest|AuditLinkAdapterTest|LoggingRecoveryTest|LoggingConfigMergeTest|LoggingMetricsBridgeTest|SinkDispatcherBoundaryContractTest|AuditLinkAdapterBoundaryContractTest|LogConfiguratorBoundaryContractTest|LoggingMetricsBridgeBoundaryContractTest)"`：发现 11 个受影响测试。
5. `ctest --test-dir build-ci --output-on-failure -R "(LoggingFacadeTest|SinkDispatcherTest|AsyncQueueControllerTest|AuditLinkAdapterTest|LoggingRecoveryTest|LoggingConfigMergeTest|LoggingMetricsBridgeTest|SinkDispatcherBoundaryContractTest|AuditLinkAdapterBoundaryContractTest|LogConfiguratorBoundaryContractTest|LoggingMetricsBridgeBoundaryContractTest)"`：11/11 通过。

## 5. 后续边界

1. 本轮只收敛 logging 构建接线，不新增测试注册语义或标签治理；这部分继续由 LOG-TODO-015 承接。
2. `Build_CMakeTools` 的工具态异常尚未消除，因此后续 gate 回写需要保留“显式 cmake/ctest 为实际验收链路”的证据说明。