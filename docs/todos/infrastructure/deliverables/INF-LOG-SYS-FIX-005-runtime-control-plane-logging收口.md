# INF-LOG-SYS-FIX-005 runtime control-plane logging 收口

日期：2026-05-28  
关联任务：INF-LOG-SYS-FIX-005 / INF-LOG-FIX-010 / INF-LOG-SYS-GATE-003  
关联冻结：`docs/ssot/KeySubsystemLoggingFieldMatrix.md`、`docs/architecture/DASALL_infra_logging模块详细设计.md` 6.10.13 / 6.10.17

## 1. 目标与约束

1. 让 runtime `transition`、`budget.reject`、`recovery.reject`、`safe_mode` control-plane event 在 live composition 下进入 shared runtime.log。
2. logging bridge 只投影 redacted operational attrs，不阻塞 `RuntimeEventBus`，也不改变 `audit=true` 事件的 audit owner；ordinary log 只允许留下 `audit_ref_pending` 这类 pending correlation marker。
3. raw recovery detail、checkpoint ref、prompt/context data、task output body 与完整 audit payload 不得进入 runtime.log；本轮只闭合 build-tree focused evidence，不外推到 installed/package authoritative evidence。

## 2. Design -> Build 映射

| Design 结论 | Build 落点 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| runtime telemetry 需要一个 `RuntimeEventEnvelope -> LogEvent` 的 owner-local 投影器 | 新增 `RuntimeLoggingBridge`，把 severity/category/context/allowlisted attrs 投影为 module=`runtime` 的 ordinary log record | `runtime/src/telemetry/RuntimeLoggingBridge.h`、`runtime/src/telemetry/RuntimeLoggingBridge.cpp` | `RuntimeLoggingBridgeTest` | `cmake --build build/vscode-linux-ninja --target dasall_runtime_logging_bridge_unit_test` |
| live composition 必须真的把 event bus 订阅到 shared logger，才能证明 runtime.log 落盘 | `RuntimeLiveDependencyComposition.cpp` 在创建 `RuntimeEventBus` / `RuntimeTelemetryBridge` 后注册 logging subscriber，并在 live composition 下触发四类 control-plane event | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`tests/integration/agent_loop/RuntimeProductionLoggingIntegrationTest.cpp` | `RuntimeProductionLoggingIntegrationTest` | `./build/vscode-linux-ninja/tests/integration/agent_loop/dasall_runtime_production_logging_integration_test` |
| 新 subscriber 不得破坏相邻 health/event-bus/backpressure 路径 | 复跑现有 runtime health maintenance integration，确认 event bus 侧向消费仍然成立 | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | `RuntimeHealthMaintenanceIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_runtime_health_maintenance_integration_test && ./build/vscode-linux-ninja/tests/integration/agent_loop/dasall_runtime_health_maintenance_integration_test` |

## 3. 改动事实

1. 新增 `RuntimeLoggingBridge`，把 `RuntimeEventEnvelope` 的 `event_name`、`category`、`severity`、request/session/trace/turn/checkpoint correlation 字段，以及 runtime owner allowlist attrs 投影为 module=`runtime` 的 `LogEvent`；ordinary log message 固定使用 event name，不复制 detail 文本。
2. `RuntimeLoggingBridge` 对 `audit=true` envelope 只补 `audit_ref_pending=true`，不把 actor/action/target/outcome、checkpoint ref 或完整 audit payload 写回 ordinary log；raw detail、`payload_json`、`checkpoint_ref` 等非 allowlisted attrs 现会被直接丢弃。
3. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 现会在 `RuntimeEventBus` 与 shared logger 都已存在时注册 logging subscriber，并通过捕获 `shared_ptr<RuntimeLoggingBridge>` 保持桥接对象生命周期；subscriber 只做 best-effort `handle(event)`，不回写 recovery、不持有 runtime 主循环锁。
4. 新增 `tests/unit/runtime/RuntimeLoggingBridgeTest.cpp`，focused 覆盖 transition envelope 的 allowlist 投影、forbidden attr 丢弃，以及 recovery reject 这类 `audit=true` envelope 只留下 `audit_ref_pending` marker 的 contract。
5. 新增 `tests/integration/agent_loop/RuntimeProductionLoggingIntegrationTest.cpp`，在 live composition 下触发 `runtime.transition`、`runtime.budget.reject`、`runtime.recovery.reject`、`runtime.safe_mode` 四类 control-plane event，验证 shared runtime.log 持久化、flush 与 redaction；`RuntimeHealthMaintenanceIntegrationTest` 同轮复验，用来守住 event bus/backpressure 相邻路径未回归。

## 4. 验证

1. `Build_CMakeTools(buildTargets=["dasall_runtime_logging_bridge_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["RuntimeLoggingBridgeTest"])`
   - 结果：命中仓库既有泛化“生成失败”；authoritative 结果以下面的 direct binary 为准。
3. `./build/vscode-linux-ninja/tests/unit/runtime/dasall_runtime_logging_bridge_unit_test`
   - 结果：通过。
4. `cmake --build build/vscode-linux-ninja --target dasall_runtime_production_logging_integration_test`
   - 结果：通过。
5. `./build/vscode-linux-ninja/tests/integration/agent_loop/dasall_runtime_production_logging_integration_test`
   - 结果：通过。
6. `cmake --build build/vscode-linux-ninja --target dasall_runtime_health_maintenance_integration_test && ./build/vscode-linux-ninja/tests/integration/agent_loop/dasall_runtime_health_maintenance_integration_test`
   - 结果：通过。

## 5. 非外推边界

1. 本任务只闭合 runtime owner 的 build-tree runtime.log / flush / health-adjacent evidence；installed/package authoritative evidence 与跨子系统 e2e 继续留给 `INF-LOG-SYS-FIX-007` / `INF-LOG-FIX-011`。
2. `audit_ref_pending` 只是 ordinary log 的 pending correlation marker，不表示 audit persistence 已完成；runtime 也没有因为本轮 bridge 引入新的 audit owner 语义。
3. 本轮不使用 qemu / kvm。