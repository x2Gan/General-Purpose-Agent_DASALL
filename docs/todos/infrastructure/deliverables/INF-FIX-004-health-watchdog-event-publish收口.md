# INF-FIX-004 health/watchdog event publish 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `INF-FIX-004`。
2. 本轮目标：收口 health/watchdog event publish gap，但不越权宣称 external bus ready；采用仓库现有最小等价接口，把 Access/Runtime/Services health probes 的状态变化真正转换成 aggregate transition event，并确认 watchdog 继续只做 policy-defined publish / advisory 输出。
3. 用户附加约束：先检查 `INF-FIX-003` 的代码实际，再推进 `INF-FIX-004`；不得使用 qemu / kvm 采集证据；完成后按仓库规范回写并提交推送。

## 2. 本地证据

1. `tools/src/bridge/ToolPluginLifecycleBridge.h/.cpp` 与 `tests/integration/tools/ToolPluginLifecycleBridgeIntegrationTest.cpp` 仍然存在且行为完整，说明 `INF-FIX-003` 的 plugin lifecycle bridge 是真实代码，不是 004 的前置 blocker。
2. `infra/src/watchdog/TimeoutEventPublisher.h/.cpp` 与 `infra/src/watchdog/RecoveryRequestEmitter.h/.cpp` 已实现 timeout publish / recovery advisory 的最小边界，并且维持“watchdog 只发布 policy-defined action，不直接执行 recovery”的分层。
3. `infra/include/health/HealthStateTypes.h`、`infra/include/health/IHealthMonitor.h`、`infra/src/health/HealthEvaluator.cpp`、`infra/src/health/ProbeExecutor.cpp` 已具备 `HealthTransition`、listener seam、probe execution 与 transition evaluation 的近端实现基础。
4. 真正缺口在 `infra/src/health/HealthMonitorFacade.cpp`：变更前它虽然能注册 probe，却在 `evaluate_now()` 中直接返回 placeholder healthy snapshot，没有执行已注册 probes，也没有发出任何 transition event。
5. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已把 tool / services / runtime control-plane probes 都注册到同一个 `health_monitor`；因此只要 `HealthMonitorFacade` 真正执行注册表，Access daemon live composition 就能立即获得真实 aggregate snapshot，而不再停留在占位 ready。

## 3. 设计结论

### 3.1 根因收口

1. `INF-FIX-004` 当前的根因不是 watchdog 缺 publisher，也不是 runtime 缺 event bus；watchdog 的 `TimeoutEventPublisher` / `RecoveryRequestEmitter` 和 runtime 的 `RuntimeEventBus` / `RuntimeHealthProbe` 都已存在。
2. 根因是 health owner 自己没有把“已注册 probe -> aggregate snapshot -> transition event”这条主链打通，导致 Access/Runtime/Services probes 即使被注册，也不会驱动 aggregate health 状态或 listener event。
3. 因此本轮最小修复应落在 `HealthMonitorFacade`，而不是再发散创建第二套 health cadence owner 或重写 watchdog。

### 3.2 最小等价接口

1. 本轮采用现有 `IHealthMonitor::subscribe(IHealthStateListener&)` 作为 `HealthEventPublisher` 的最小等价接口。
2. 该接口只承诺“状态变化时发出结构化 `HealthTransition` + 当前 aggregate snapshot”，不承诺 external bus delivery guarantee。
3. 这与 `docs/ssot/HealthCadenceAndEventBoundary.md` 一致：event publish 最小接口未冻结时，只能完成本地状态提交、transition event、logging/metrics/local cache fallback，不得宣称 bus-ready。

### 3.3 watchdog 边界

1. `TimeoutEventPublisher` 继续只负责 timeout publish / local buffer fallback。
2. `RecoveryRequestEmitter` 继续只发 advisory `RecoveryHintRequest`，不直接调用 runtime recovery path。
3. 因此 watchdog 本轮只需做近端核验和 traceability 回写，不需要追加代码修改。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | `HealthMonitorFacade` 必须执行已注册 probes，而不是返回 placeholder snapshot | `infra/src/health/HealthMonitorFacade.h`、`infra/src/health/HealthMonitorFacade.cpp` |
| D2 | health transition event 的最小等价接口固定为 `IHealthStateListener` | `infra/include/health/IHealthMonitor.h`、`tests/unit/infra/health/HealthMonitorFacadeTest.cpp` |
| D3 | runtime health probe 必须在 event-bus backpressure 下驱动 aggregate transition event | `tests/integration/agent_loop/RuntimeHealthMaintenanceIntegrationTest.cpp` |
| D4 | daemon live composition 必须真正执行 tool / services / runtime probes，并得到 ready aggregate snapshot | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` |
| D5 | watchdog 只保留 publish / advisory 边界，不进入 recovery execute owner | `infra/src/watchdog/TimeoutEventPublisher.h/.cpp`、`infra/src/watchdog/RecoveryRequestEmitter.h/.cpp` |

## 5. Build 三件套

1. 代码目标：
   - 更新 `HealthMonitorFacade`，通过 `ProbeExecutor + HealthEvaluator` 执行已注册 probes、生成 monotonic aggregate snapshot，并在状态变化时向 `IHealthStateListener` 发出 `HealthTransition`。
   - 扩展 `HealthMonitorFacadeTest`、`RuntimeHealthMaintenanceIntegrationTest` 与 `DaemonRuntimeLiveDependencyCompositionTest`，把 runtime probe transition 和 live composition aggregate health 变成 focused evidence。
   - 本轮不改 watchdog 产品代码，只保留现有 policy-defined publish / advisory 边界。
2. 测试目标：
   - `HealthMonitorFacadeTest`
   - `RuntimeHealthMaintenanceIntegrationTest`
   - `HealthSnapshotUnitTest`
   - `DaemonRuntimeLiveDependencyCompositionTest`
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_health_monitor_facade_unit_test","dasall_runtime_health_maintenance_integration_test","dasall_access_daemon_runtime_live_dependency_composition_integration_test","dasall_health_snapshot_unit_test"])`
   - `RunCtest_CMakeTools(tests=["RuntimeHealthMaintenanceIntegrationTest","HealthSnapshotUnitTest","DaemonRuntimeLiveDependencyCompositionTest"])` 当前仍返回仓库已知泛化 `生成失败`，因此按 fallback 直接执行：
     - `./build/vscode-linux-ninja/tests/unit/infra/dasall_health_monitor_facade_unit_test`
     - `./build/vscode-linux-ninja/tests/integration/agent_loop/dasall_runtime_health_maintenance_integration_test`
     - `./build/vscode-linux-ninja/tests/unit/infra/dasall_health_snapshot_unit_test`
     - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test`

## 6. Rollout Checklist

1. `HealthMonitorFacade` 不再返回 placeholder ready，而是消费注册表中的真实 probes。
2. `IHealthStateListener` 现在成为 health transition event 的 authoritative 等价接口。
3. runtime health probe 在 event-bus overflow / maintenance backlog 下能够驱动 aggregate degraded transition。
4. daemon live composition 现在真正评估 tool / services / runtime 三类 probes 的 aggregate ready baseline。
5. watchdog 继续保持 publish/advisory 边界，不越过 ADR-007 的 recovery execute owner。
6. 本轮不使用 qemu / kvm，也不把结果外推到 installed package / release-runner / soak。

## 7. 风险与回退

1. 若后续再次把 `HealthMonitorFacade` 退回 placeholder snapshot，Access/Runtime/Services health probe 将重新失去 aggregate owner，004 会再次回退成 documentation-only。
2. 若后续把 `IHealthStateListener` 误写成 external bus guarantee，则会违反 `HealthCadenceAndEventBoundary` 已冻结的 fallback rule。
3. 若 watchdog 后续直接调用 runtime recovery，而不是继续经 `RecoveryRequestEmitter` 输出 advisory request，将越权打破 ADR-007 owner boundary。

## 8. D Gate

1. `INF-FIX-003` 前置核验已完成，非 blocker。
2. `INF-FIX-004` 的 owner、等价接口、watchdog boundary、focused evidence 与回退边界都已落盘。
3. Build 三件套已明确，并保留仓库既有 `RunCtest_CMakeTools -> direct binary fallback` 口径。
4. 范围保持在 health/watchdog event publish，不扩张到 external bus freeze、qemu / kvm 或 release / soak 任务。

结论：D Gate = PASS；`INF-FIX-004` 可按 `HealthMonitorFacade` aggregate transition + focused runtime/access evidence 收口。