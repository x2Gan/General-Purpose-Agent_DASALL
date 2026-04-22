# RT-TODO-023 Observability / Health / Maintenance 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 6.3 已把 `RuntimeEventBus`、`RuntimeTelemetryBridge`、`RuntimeHealthProbe` / background jobs 固定为 Runtime 内部支撑，不跨模块承载业务控制语义。
2. 同文档 6.14.2 已明确 `RuntimeEventBus::dispatch_mutex` 属于锁序 L6，意味着 023 必须保持轻量且不反向持有主循环关键锁。
3. 同文档 6.14.4 已固定 `RuntimeEventBus` 队列深度语义：非审计事件允许丢弃并累计 `event_bus_drops`，审计类事件不能直接丢弃。
4. 同文档 6.18 已锁定 `request_id/session_id/trace_id/turn_id/checkpoint_id` 的所有权与传播路径，023 只能复用这些字段，不能新造并行 ID。
5. 同文档 6.21、6.23、6.24.12 与专项 TODO 6.3 中的 RT-TODO-023 已把 023 的 Build 面固定为四个 runtime-private 组件：`RuntimeTelemetryBridge`、`RuntimeEventBus`、`RuntimeHealthProbe`、`BackgroundMaintenanceHooks`。
6. `tools/src/ops/ToolHealthProbe.h/.cpp` 已证明仓库当前 health 设计的稳定模式是：provider 负责采样，probe 负责聚合 `infra::HealthSnapshot` / `infra::ProbeResult`，而不是把 live wiring 硬塞进 health probe 内部。

## 外部参考

1. OpenTelemetry Signals 文档强调 logs / metrics / traces / baggage 应共享统一关联字段而不是各自扩一套口径。023 借用的是“统一字段面 + 多信号复用”的原则，不引入 exporter 实现细节。

## 设计结论

1. 023 保持 runtime-private：所有实现放在 `runtime/src/**`，不扩 shared contracts，不宣称对外 ABI 稳定。
2. 本轮采用一套统一的 `RuntimeEventEnvelope` 作为 EventBus、Telemetry、BackgroundMaintenance 的共享事实面：
   - 统一承载 `request_id/session_id/trace_id/turn_id/checkpoint_id`；
   - 统一承载 `RuntimeErrorCode`、事件名、detail 与 attributes；
   - 统一区分 audit / non-audit 事件，为背压策略提供单一入口。
3. `RuntimeEventBus` 是 023 的最小控制面 owner：
   - 只负责 `publish/subscribe/dispatch_pending` 与非审计事件 drop 策略；
   - 不承担 exporter、业务补偿或复杂 DSL。
4. `RuntimeTelemetryBridge` 只做结构化 record 归一化与发射：
   - `emit_transition(...)`
   - `emit_budget_reject(...)`
   - `emit_recovery_reject(...)`
   - `emit_safe_mode(...)`
5. `RuntimeHealthProbe` 采用 provider-sample 聚合模式，输出 `infra::HealthSnapshot` / `infra::ProbeResult`，不直接拉起线程或阻塞主循环。
6. `BackgroundMaintenanceHooks` 只把空闲态维护 tick 转成标准化 event envelope，通过 EventBus 发布，不在主循环线程里执行维护逻辑本身。

## 边界与职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `RuntimeEventBus` | 受控发布、订阅、分发、背压与 drop 计数 | 不做 exporter、不定义完整事件 DSL |
| `RuntimeTelemetryBridge` | 把 transition / budget reject / recovery reject / safe mode 归一为结构化事件 | 不做日志框架适配、不拥有 trace exporter |
| `RuntimeHealthProbe` | 从 provider sample 聚合 `HealthSnapshot` 与 `ProbeResult` | 不采样真实外部依赖、不调度探测线程 |
| `BackgroundMaintenanceHooks` | 把 idle tick / flush / refresh 等维护时机挂到 EventBus | 不执行清理或刷新本体 |

## 数据与接口说明

1. `RuntimeEventEnvelope`：最小统一字段面。
2. `RuntimeEventBus::publish/subscribe/dispatch_pending`：固定 023 的事件进入口、订阅口与显式分发口。
3. `RuntimeTelemetryContext`：围绕 6.18 传播的五个关联 ID 建模，不新增并行字段。
4. `RuntimeHealthSample` / `IRuntimeHealthSignalProvider`：沿用 provider-sample 边界，保证 health probe 可以单测。
5. `BackgroundMaintenanceTick`：只表达“哪些维护动作 due”，不表达具体执行结果。

## 流程

1. 主循环或控制器把事实交给 `RuntimeTelemetryBridge` 或 `BackgroundMaintenanceHooks`。
2. 这些 helper 统一生成 `RuntimeEventEnvelope` 并交给 `RuntimeEventBus::publish(...)`。
3. `RuntimeEventBus` 在 L6 锁下入队：
   - 非审计事件超过上限时，丢弃最旧非审计事件并累计 drop；
   - 审计事件保留，不走同一 drop 逻辑。
4. `dispatch_pending(...)` 在锁外调用订阅 handler，避免把 handler 执行绑定到队列锁。
5. `RuntimeHealthProbe` 基于 provider sample 聚合 `infra::HealthSnapshot`，把 event bus overflow、telemetry degraded、maintenance backlog、watchdog/dependency 不健康统一折叠为 health 视图。

## 文件范围

| 设计项 | 文件 |
|---|---|
| EventBus | `runtime/src/telemetry/RuntimeEventBus.h`、`runtime/src/telemetry/RuntimeEventBus.cpp` |
| TelemetryBridge | `runtime/src/telemetry/RuntimeTelemetryBridge.h`、`runtime/src/telemetry/RuntimeTelemetryBridge.cpp` |
| HealthProbe | `runtime/src/health/RuntimeHealthProbe.h`、`runtime/src/health/RuntimeHealthProbe.cpp` |
| Maintenance hooks | `runtime/src/maintenance/BackgroundMaintenanceHooks.h`、`runtime/src/maintenance/BackgroundMaintenanceHooks.cpp` |
| 单测 | `tests/unit/runtime/RuntimeEventBusTest.cpp`、`tests/unit/runtime/RuntimeTelemetryBridgeTest.cpp`、`tests/unit/runtime/RuntimeHealthProbeTest.cpp`、`tests/unit/runtime/RuntimeBackgroundMaintenanceHookTest.cpp` |

## Design -> Build 映射

1. 代码目标：`runtime/src/telemetry/RuntimeEventBus.cpp`、`runtime/src/telemetry/RuntimeTelemetryBridge.cpp`、`runtime/src/health/RuntimeHealthProbe.cpp`、`runtime/src/maintenance/BackgroundMaintenanceHooks.cpp`
2. 测试目标：`RuntimeEventBusTest`、`RuntimeTelemetryBridgeTest`、`RuntimeHealthProbeTest`、`RuntimeBackgroundMaintenanceHookTest`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_event_bus_unit_test dasall_runtime_telemetry_bridge_unit_test dasall_runtime_health_probe_unit_test dasall_runtime_background_maintenance_hook_unit_test && ctest --test-dir build-ci -R "^(RuntimeEventBusTest|RuntimeTelemetryBridgeTest|RuntimeHealthProbeTest|RuntimeBackgroundMaintenanceHookTest)$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 固定共享事件字段面 | `RuntimeEventEnvelope` 能覆盖 6.18 五类关联 ID + RT_E_* | PASS |
| D2 | 固定 EventBus 背压语义 | 非审计 drop / 审计保留在单测里可二值验证 | PASS |
| D3 | 固定 health provider seam | `RuntimeHealthProbe` 可以独立单测，不依赖真实线程或 exporter | PASS |
| D4 | 固定 maintenance hook 边界 | hook 只发布 idle tick，不执行维护逻辑本体 | PASS |
| D5 | 固定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 023 设计交付物已落盘。
2. EventBus / Telemetry / Health / Maintenance 的边界已可直接转成私有头文件和单测入口。
3. 023 不扩 shared ABI，不外推真实 exporter / dispatch thread ready。

结论：D Gate = PASS，可进入 RT-TODO-023 Build。