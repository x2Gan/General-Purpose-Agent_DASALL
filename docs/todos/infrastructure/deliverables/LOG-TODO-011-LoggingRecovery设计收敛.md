# LOG-TODO-011 LoggingRecovery 设计收敛

日期：2026-04-03  
任务：LOG-TODO-011  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging%E7%BB%84%E4%BB%B6%E4%B8%93%E9%A1%B9TODO.md) 的 LOG-TODO-011 要求实现 `LoggingRecovery.cpp`，并把完成判定固定为“降级触发、恢复成功、恢复失败三类路径可二值判定”。
2. [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging%E6%A8%A1%E5%9D%97%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md) 6.8 已明确三条恢复主约束：sink IO 失败切换 fallback sink 并标记 degraded；格式化失败降级为最小字段文本格式并上报 `LOG_E_FORMAT_INVALID`；周期性重试主 sink，恢复成功后清除 degraded。
3. [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) 已提供当前仓库里最接近的 degraded/fallback 模式：主路径失败时切到 fallback、保留 degraded 状态，并把恢复语义留给后续更高层编排。
4. [infra/include/logging/LoggingErrors.h](infra/include/logging/LoggingErrors.h) 已在 LOG-TODO-010 中冻结 queue/sink/format/config 四个 logging 私有错误码，011 可以直接复用 `LOG_E_SINK_IO` 与 `LOG_E_FORMAT_INVALID` 而不再散落使用通用 contracts 码值。

## 2. 外部参考

1. Microsoft Azure Retry pattern 强调：瞬时依赖故障应进入显式 retry/fallback 路径，且需要记录失败明细；当重试持续失败时，不应假装系统恢复，而应保持降级并向上游暴露可观测状态。本轮据此把 retry success 与 retry failure 明确拆成两条二值路径。

## 3. Blocker 修复与 Design 结论

阻塞结论：

1. 011 的 blocker 不是恢复语义缺失，而是“失败注入桩不足”。如果没有可注入 sink，`degraded -> retry success/failure` 三条路径只能靠真实 IO 才能触发，无法稳定做单测。

最小 blocker-fix：

1. 在 `infra/src/logging/LoggingRecovery.h` 中定义 internal `ILogRecoverySink` 注入点，让 primary/fallback sink 都可被脚本化测试替身驱动。
2. 在 `LoggingRecoveryTest.cpp` 中使用脚本化 mock sink 顺序回放 primary fail、fallback success、retry success、retry fail，完成 failure-injection 覆盖。

设计结论：

1. `LoggingRecovery` 作为 logging 内部恢复骨架，不改 public `ILogger` 接口，也不提前接管 infra/health 的最终裁定权。
2. 对外冻结三条内部行为：
   - primary 写失败后切到 fallback 并标记 degraded；
   - 格式化失败后生成最小字段 fallback 记录并保留 `LOG_E_FORMAT_INVALID`；
   - `retry_primary_sink()` 作为周期性恢复入口，成功则清除 degraded，失败则保持 degraded。
3. 恢复结果采用独立 `LoggingRecoveryResult` 表达，以便在不破坏现有 `LogWriteResult` 边界的前提下区分 success、degraded success、failure 三类状态。
4. 本轮只保留 recovery success 的占位出口，不接入真实 audit/health 子域；后续若对接 013/健康探针，只能在现有状态机之上追加 bridge，不改 011 的基本判定语义。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 primary/fallback sink 注入边界 | infra/src/logging/LoggingRecovery.h |
| 落 degraded/fallback/retry 最小状态机 | infra/src/logging/LoggingRecovery.cpp |
| 固化 sink IO 降级与恢复成功/失败三类路径 | tests/unit/infra/logging/LoggingRecoveryTest.cpp |
| 固化 format failure 的最小 fallback 记录行为 | tests/unit/infra/logging/LoggingRecoveryTest.cpp |
| 注册 unit 目标并接入聚合 unit 列表 | tests/unit/infra/CMakeLists.txt, tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 [infra/src/logging/LoggingRecovery.h](infra/src/logging/LoggingRecovery.h) 与 [infra/src/logging/LoggingRecovery.cpp](infra/src/logging/LoggingRecovery.cpp)，实现 fallback sink、degraded 标记和周期重试入口。
2. 测试目标：新增 [tests/unit/infra/logging/LoggingRecoveryTest.cpp](tests/unit/infra/logging/LoggingRecoveryTest.cpp)，通过脚本化 mock sink 覆盖 sink IO 降级、format failure 降级、recovery success、recovery failure 四条路径，并在 CMake 中注册到 unit 聚合列表。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_logging_recovery_unit_test`
   - `ctest --test-dir build-ci -N -R "LoggingRecoveryTest"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. `LoggingRecovery` 目前只实现 internal sink 注入与状态机骨架，不承担真实 ringbuffer/stderr 设备管理；后续落真实 sink adapter 时，应复用同一注入接口而不是替换状态机。
2. format failure 的最小 fallback 记录目前通过清空 attrs 收敛为最小字段；若 StructuredFormatter 后续冻结了更精确的最小文本格式，应通过单点 helper 调整，而不是扩张恢复状态机职责。
3. recovery success 当前只清除 degraded 并保留占位出口；若后续要补恢复审计事件，应新增 bridge 或 audit adapter，不把 audit 写入逻辑直接塞进 `LoggingRecovery`。