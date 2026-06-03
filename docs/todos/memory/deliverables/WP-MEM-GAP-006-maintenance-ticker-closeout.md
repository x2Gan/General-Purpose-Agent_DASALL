# WP-MEM-GAP-006 maintenance ticker closeout

来源任务：WP-MEM-GAP-006
关联缺口：GAP-P1-B
完成日期：2026-06-03

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-006 / GAP-P1-B`，不把 `external_evidence` 投影、ProductionLogging 字段补强或更高层 soak 采样混入同一轮。
2. authoritative 问题定义固定为：daemon 是否已成为 production maintenance cadence owner，并通过显式 profile 投影、focused tests 与 live composition 关停 internal auto_schedule 的方式消除双 ticker 风险。
3. owner 边界保持不变：maintenance 执行主体仍是 Memory 的 `IMemoryManager::run_maintenance()` / `MemoryMaintenanceWorker`；daemon 只拥有周期调度、失败退避与 runtime idle hook publish，不反向接管 Memory 内部实现。

## 2. 本轮代码结果

| 目标 | 落盘结果 | 对 closeout 的意义 |
|---|---|---|
| profile maintenance policy | 更新 [profiles/include/RuntimePolicySnapshot.h](../../../profiles/include/RuntimePolicySnapshot.h)、[profiles/src/RuntimePolicyProvider.cpp](../../../profiles/src/RuntimePolicyProvider.cpp)、[profiles/src/ProfileOverlayComposer.cpp](../../../profiles/src/ProfileOverlayComposer.cpp) 与五档 [profiles](../../../profiles) `runtime_policy.yaml`，新增 `memory.maintenance.{enabled, interval_ms, jitter_ms, retention_ms, checkpoint_strategy}` | daemon ticker 不再依赖 worker_threads 启发式猜测 cadence；maintenance cadence 现成为显式 runtime policy |
| Memory 投影与 live composition 去双 ticker | 更新 [memory/src/config/MemoryConfigProjector.cpp](../../../memory/src/config/MemoryConfigProjector.cpp) 与 [apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp](../../../apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp)：MemoryConfig 改从 `memory.maintenance.interval_ms/enabled` 投影，live composition 显式关闭 `maintenance.auto_schedule` | production path 明确改为 daemon-owned cadence，避免 live composition 下内部 worker 与外部 ticker 双调度 |
| daemon-owned ticker | 新增 [apps/daemon/src/MemoryMaintenanceTickerThread.h](../../../apps/daemon/src/MemoryMaintenanceTickerThread.h) 与 [apps/daemon/src/MemoryMaintenanceTickerThread.cpp](../../../apps/daemon/src/MemoryMaintenanceTickerThread.cpp)，并更新 [apps/daemon/src/main.cpp](../../../apps/daemon/src/main.cpp) / [apps/daemon/CMakeLists.txt](../../../apps/daemon/CMakeLists.txt) | daemon 现拥有单线程 maintenance ticker、runtime maintenance hook publish、warning/exception 审计与失败退避；ticker 启停不阻断主链路 |
| focused regression | 新增 [tests/unit/apps/daemon/MemoryMaintenanceTickerThreadTest.cpp](../../../tests/unit/apps/daemon/MemoryMaintenanceTickerThreadTest.cpp)，扩展 [tests/unit/apps/daemon/CMakeLists.txt](../../../tests/unit/apps/daemon/CMakeLists.txt)、[tests/integration/memory/MemoryMaintenanceIntegrationTest.cpp](../../../tests/integration/memory/MemoryMaintenanceIntegrationTest.cpp) 与 [tests/integration/memory/CMakeLists.txt](../../../tests/integration/memory/CMakeLists.txt) | `MemoryMaintenanceTickerCadenceTest`、`MemoryMaintenanceTickerFailureBackoffTest` 与扩展后的 `MemoryMaintenanceIntegrationTest` 现把 cadence/backoff/外部 ticker 驱动路径锁成自动化证据 |

## 3. 设计与关联依据

1. [docs/architecture/DASALL_memory子系统详细设计.md](../../../docs/architecture/DASALL_memory子系统详细设计.md) §6.23 已把 maintenance 定义为 checkpoint / retention / quarantine / vector rebuild 的周期治理面；本轮只补生产 owner，不改 maintenance 算法 owner。
2. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已把 `WP-MEM-GAP-006` 固定为 daemon-side `MemoryMaintenanceTickerThread`、profile 投影与 cadence/backoff focused tests。
3. Runtime 当前只有 `BackgroundMaintenanceHooks` publish seam，没有现成 scheduler；因此本轮选择 daemon-owned ticker + runtime hook publish 的最小 owner-safe 落点，而不是把调度所有权重新塞回 runtime 或 Memory 内部。

## 4. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| maintenance cadence 必须由显式 profile 键承接 | `RuntimePolicySnapshotTest`、`RuntimePolicyProviderTest`、`ProfileOverlayComposerTest`、`ProfileRuntimePolicySchemaContractTest` |
| Memory 投影与 live composition 不能再形成双 ticker | `MemoryProfileCompatibilityTest`、`dasall-daemon` 编译 |
| daemon 需要单线程 cadence + failure backoff + runtime hook publish | `MemoryMaintenanceTickerCadenceTest`、`MemoryMaintenanceTickerFailureBackoffTest` |
| internal auto_schedule 关闭后，外部 ticker 仍应驱动真实 sqlite maintenance | `MemoryMaintenanceIntegrationTest` |

## 5. D Gate

1. 调度 owner 单一：production live composition 明确关闭 `memory_config.maintenance.auto_schedule`，daemon 成为唯一周期触发 owner。
2. 执行 owner 不漂移：daemon 只调用 `IMemoryManager::run_maintenance()`，不直接操作 SQLite store / retention 细节，不越过 Memory owner。
3. 失败语义 fail-soft：ticker warning/exception 只触发 log + audit + 下一轮 backoff，不阻断 daemon 主链、gateway 初始化或 signal shutdown 流程。

## 6. 验证结果

1. `cmake --build build-ci --target dasall_runtime_policy_snapshot_unit_test dasall_runtime_policy_provider_unit_test dasall_profile_overlay_composer_unit_test dasall_contract_profile_runtime_policy_schema_test && ctest --test-dir build-ci --output-on-failure -R "RuntimePolicySnapshotTest|RuntimePolicyProviderTest|ProfileOverlayComposerTest|ProfileRuntimePolicySchemaContractTest"`
   - 结果：通过，4/4。
2. `cmake --build build-ci --target dasall_memory_profile_compatibility_integration_test && ctest --test-dir build-ci --output-on-failure -R "MemoryProfileCompatibilityTest"`
   - 结果：通过，1/1。
3. `cmake --build build-ci --target dasall-daemon`
   - 结果：通过；daemon 主程序已成功编译并接线新的 ticker thread。
4. `cmake -S . -B build-ci && cmake --build build-ci --target "dasall-daemon_memory_maintenance_ticker_unit_test" dasall_memory_maintenance_integration_test && ctest --test-dir build-ci --output-on-failure -R "MemoryMaintenanceTickerCadenceTest|MemoryMaintenanceTickerFailureBackoffTest|MemoryMaintenanceIntegrationTest"`
   - 结果：通过，3/3。

## 7. 完成判定

1. `WP-MEM-GAP-006 / GAP-P1-B` 已闭合。
2. production Memory maintenance cadence 现由 daemon 显式持有，且 live composition 已关闭 internal auto_schedule，双 ticker 根因已被消除。
3. Memory 当前剩余 P1 焦点收敛为 `WP-MEM-GAP-007 / -008` 与更高层 soak / GA 绿色记录；MaintenanceTicker 不再是本轮后的 P1 blocker。