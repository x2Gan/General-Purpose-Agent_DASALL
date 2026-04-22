# RT-TODO-030 RuntimeSafeMode 与 HealthMaintenance 设计收敛

日期：2026-04-22  
任务：RT-TODO-030  
状态：已完成

## 1. 本地证据

1. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 将 030 定义为 runtime-local gate：需要同时验证 safe mode、health degrade、idle maintenance、cancellation、backpressure 与 lock-order stress。
2. `tests/unit/runtime/SafeModeControllerTest.cpp`、`RuntimeHealthProbeTest.cpp`、`RuntimeBackgroundMaintenanceHookTest.cpp`、`CancellationTokenTest.cpp` 与 `SchedulerTest.cpp` 已经冻结了单组件语义，但在 030 开始前还没有任何 integration gate 把这些控制组件串成真实审计链或 backlog / drain 链。
3. 真实 profile 资产里的 `degrade_policy.fallback_chain` 不是测试私有 token，而是 concrete route：
   - `profiles/desktop_full/runtime_policy.yaml` 使用 `lan.general`、`local.small`；
   - `profiles/edge_minimal/runtime_policy.yaml` 使用 `builtin_only`。
   因此 safe-mode gate 若只识别 `allow_model_failover` / `allow_budget_degrade` 这类测试 token，就无法代表真实 runtime policy。
4. 第一轮 `RuntimeSafeModeIntegrationTest` 窄验证确实暴露了这个缺口：budget overrun 在 `desktop_full` 下没有进入 `Degraded`。这说明 030 不是纯测试任务，还需要修正 `SafeModeController` 对真实 profile fallback 语义的解释。

## 2. 设计结论

1. 030 最小实现分三块：
   - 新增 `RuntimeSafeModeIntegrationTest`，把真实 profile snapshot、`SafeModeController`、`RuntimeTelemetryBridge` 和 `RuntimeEventBus` 串成审计链；
   - 新增 `RuntimeHealthMaintenanceIntegrationTest`，把 `BackgroundMaintenanceHooks`、`RuntimeEventBus` 与 `RuntimeHealthProbe` 串成 backlog / drain 链；
   - 扩展 `SchedulerTest` 的并发 stress，补齐 queue bound、worker lease release 与 lock-order 证据。
2. `SafeModeController::select_fallback(...)` 必须先按布尔位解释能力，再按 route 解释具体路径：
   - `BudgetExhausted` 直接尊重 `allow_budget_degrade`；
   - `DependencyUnavailable` 在 `allow_model_failover=true` 时选取第一个真实 failover route；
   - 若 failover 不允许，则退回 `abort_safe` / `FailedSafe`。
3. health / maintenance integration 不需要改 production 逻辑；已有 `RuntimeEventBus` 的 drop-oldest 语义与 `RuntimeHealthProbe` 的 component aggregation 已足够形成可验证组合面。
4. cancellation / backpressure / concurrency 维持在 runtime unit gate，因为当前 production 中没有更高一级的稳定组合点能在不新增 seam 的前提下把这些行为再串成更真实的集成路径。

## 3. 边界 / 职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `SafeModeController` | 基于真实 `RuntimePolicySnapshot` 决定 `Degraded` / `SafeMode` / `FailedSafe` | 不负责把事件发到总线 |
| `RuntimeTelemetryBridge` | 将 safe-mode 决策投影成可审计事件 | 不决定 safe-mode 策略 |
| `RuntimeEventBus` | 承载审计事件与 maintenance idle tick，并提供 overflow / drain 语义 | 不判断健康状态 |
| `RuntimeHealthProbe` | 将 backlog / overflow / safe-mode 信号聚合为健康快照 | 不主动发布 maintenance 事件 |
| `BackgroundMaintenanceHooks` | 将 idle maintenance work 变成 event bus 上的 maintenance tick | 不决定是否 degraded |
| `Scheduler` / `CancellationToken` | 暴露 backpressure、queue bound 与取消可见性 | 不负责 safe-mode 审计投影 |

## 4. 数据 / 接口说明

1. safe-mode integration 的输入来自真实 profile 资产链：
   - `ProfileCatalog`
   - `RuntimePolicyProvider`
   - `RuntimePolicySnapshot`
2. safe-mode 触发输入：
   - `BudgetDecision` 用于 budget overrun；
   - `HealthSignal` 用于 dependency unavailable；
   - `SafeModeTriggerKind::WatchdogTimeout` 用于 watchdog timeout。
3. health / maintenance integration 的输入：
   - `BackgroundMaintenanceTick`
   - `RuntimeHealthSample`
   - `RuntimeEventBus::drop_count()` / `queue_depth()` 反馈的 backlog / overflow 状态。
4. concurrency gate 复用现有接口：
   - `SchedulerTicketRequest`
   - `AcquireWorkerRequest`
   - `ReleaseWorkerRequest`
   - `CancellationToken`

## 5. 流程 / 时序

1. `RuntimeSafeModeIntegrationTest`：
   - 加载 `desktop_full` / `edge_minimal` snapshot；
   - 触发 budget overrun、watchdog timeout、dependency unavailable；
   - 经 `RuntimeTelemetryBridge` 投影到 `RuntimeEventBus`；
   - 断言 `runtime.safe_mode` audit event 的 `target_mode`、error code 与 correlation fields。
2. `RuntimeHealthMaintenanceIntegrationTest`：
   - 通过 `BackgroundMaintenanceHooks` 向 `RuntimeEventBus` 连续发布 idle tick；
   - 让 event bus 触发 drop-oldest；
   - 由 `MutableHealthSignalProvider` 将 overflow / backlog 反馈给 `RuntimeHealthProbe`；
   - 先断言 degraded，再在 dispatch drain 后断言恢复 healthy。
3. `SchedulerTest` stress：
   - 多线程并发 enqueue / acquire / release；
   - 在循环中穿插 `backpressure_state()`；
   - 最终断言 accepted / acquired / released 都发生过，maintenance queue depth 保持有界，worker lease 没有长期饱和。

## 6. 文件范围

1. `runtime/src/safety/SafeModeController.cpp`
2. `tests/integration/agent_loop/CMakeLists.txt`
3. `tests/integration/agent_loop/RuntimeSafeModeIntegrationTest.cpp`
4. `tests/integration/agent_loop/RuntimeHealthMaintenanceIntegrationTest.cpp`
5. `tests/unit/runtime/SchedulerTest.cpp`

## 7. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| safe-mode terminal audit chain | `tests/integration/agent_loop/RuntimeSafeModeIntegrationTest.cpp` |
| health / maintenance backlog and drain chain | `tests/integration/agent_loop/RuntimeHealthMaintenanceIntegrationTest.cpp` |
| integration discoverability | `tests/integration/agent_loop/CMakeLists.txt` |
| lock-order / backpressure stress | `tests/unit/runtime/SchedulerTest.cpp` |
| real profile fallback semantics | `runtime/src/safety/SafeModeController.cpp` |

## 8. Build 三件套

1. 代码目标：
   - 新增 `RuntimeSafeModeIntegrationTest` 与 `RuntimeHealthMaintenanceIntegrationTest`；
   - 修复 `SafeModeController` 对真实 profile fallback route 的解释；
   - 扩展 `SchedulerTest` 的并发 stress。
2. 测试目标：
   - `RuntimeSafeModeIntegrationTest`
   - `RuntimeHealthMaintenanceIntegrationTest`
   - `CancellationTokenTest`
   - `SchedulerTest`
   - `RuntimeHealthProbeTest`
   - `RuntimeBackgroundMaintenanceHookTest`
3. 验收命令：
   - `cmake --build build-ci --target dasall_runtime_safe_mode_integration_test dasall_runtime_safe_mode_controller_unit_test && ctest --test-dir build-ci -R "^(RuntimeSafeModeIntegrationTest|SafeModeControllerTest)$" --output-on-failure`
   - `cmake --build build-ci --target dasall_runtime_safe_mode_integration_test dasall_runtime_health_maintenance_integration_test dasall_runtime_cancellation_token_unit_test dasall_runtime_scheduler_surface_unit_test dasall_runtime_health_probe_unit_test dasall_runtime_background_maintenance_hook_unit_test && ctest --test-dir build-ci -R "^(RuntimeSafeModeIntegrationTest|RuntimeHealthMaintenanceIntegrationTest|CancellationTokenTest|SchedulerTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest)$" --output-on-failure`
   - `ctest --test-dir build-ci -N | rg "RuntimeSafeModeIntegrationTest|RuntimeHealthMaintenanceIntegrationTest|CancellationTokenTest|SchedulerTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_integration_tests`
   - `cmake --build build-ci --target dasall_integration_tests`

## 9. 风险与回退

1. 如果未来 profile schema 把 failover route 与 degrade capability 分开建模，`SafeModeController` 应迁移到显式 schema 字段，而不是继续依赖 `fallback_chain` 的首项语义。
2. 030 的 acceptance 已经证明 runtime 自身 gates 正常，但仓库级全量绿灯仍受外部 blocker 影响：
   - `tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp` 现存语法损坏；
   - `InfraDiagnosticsSmokeTest` / `InfraDiagnosticsIntegrationTest` 现存失败。
3. 若后续要把 cancellation / scheduler 进一步提升到更真实的 control-plane integration，需要先引入稳定的 runtime-owned orchestration seam，而不是在 030 内继续扩测试私有拼装。