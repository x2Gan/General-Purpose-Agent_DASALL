# INF-LOG-SYS-FIX-006 services production logging 收口

日期：2026-05-28  
关联任务：INF-LOG-SYS-FIX-006 / INF-LOG-FIX-010 / INF-LOG-SYS-GATE-004  
关联冻结：`docs/ssot/KeySubsystemLoggingFieldMatrix.md`、`docs/architecture/DASALL_infra_logging模块详细设计.md` 6.10.13 / 6.10.18

## 1. 目标与约束

1. 让 services execute/query/catalog route receipt 在 live composition 与 loopback smoke 下进入 shared structured logging sink，而不是继续用 request ledger 充当 production logging 证据。
2. services logging bridge 只投影 owner-safe route attrs，不写 raw `payload_json`、catalog/result body、adapter secret、auth header 或未脱敏 side effect 原文；high-risk action 的 audit persistence 继续由 `ServiceAuditBridge` 持有。
3. 本轮只闭合 services owner 的 build-tree logger seam、bridge 与 focused evidence；installed/package authoritative evidence 与跨子系统 e2e 继续留给 `INF-LOG-SYS-FIX-007` / `INF-LOG-FIX-011`。

## 2. Design -> Build 映射

| Design 结论 | Build 落点 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| services 需要一个 `route receipt -> LogEvent` 的 owner-local 投影器 | 新增 `ServiceLoggingBridge`，把 execution/data query/catalog route 的 allowlisted attrs 投影为 module=`services` 的 ordinary log record | `services/src/bridges/ServiceLoggingBridge.h`、`services/src/bridges/ServiceLoggingBridge.cpp` | `ServiceLoggingBridgeTest` | `cmake --build build/vscode-linux-ninja --target dasall_service_logging_bridge_unit_test && ./build/vscode-linux-ninja/tests/unit/services/bridges/dasall_service_logging_bridge_unit_test` |
| live composition 必须真的把 shared logger 下发到 services lane，才能证明正式 sink 生效 | `ServiceLiveCompositionOptions` 新增 `logger`，`ServiceLiveComposition` 在 logger 存在时构造 `ServiceLoggingBridge` 并传给 execution/data lane；runtime_support 同步透传 shared logger | `services/include/ServiceLiveComposition.h`、`services/src/ServiceLiveComposition.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | `CapabilityServicesLoggingIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_services_logging_integration_test && ./build/vscode-linux-ninja/tests/integration/services/dasall_services_logging_integration_test` |
| request ledger 不得再充当 services production logging 证据 | loopback smoke 改为断言 shared logger sink 中的 request/capability/target attrs，同时保留 route 行为断言 | `tests/integration/services/CapabilityServicesSmokeIntegrationTest.cpp`、`tests/mocks/include/CapabilityServicesLoopbackFixture.h` | `CapabilityServicesSmokeIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_services_smoke_integration_test && ./build/vscode-linux-ninja/tests/integration/services/dasall_services_smoke_integration_test` |

## 3. 改动事实

1. 新增 `ServiceLoggingBridge`，把 services execution/data query/catalog route 的 `request_id`、`capability_id`、`target_id`、`request_kind`、`operation_name`、`route_kind`、`adapter_id`、`trust_class`、`availability_state`、`transport_outcome`、`provider_status_code`、`latency_ms`、`side_effect_count` 与 `evidence_ref_count` 投影为 module=`services` 的 `LogEvent`；ordinary log message 固定使用 event name，不复制 payload body。
2. `ServiceLiveCompositionOptions` 新增 `std::shared_ptr<infra::logging::ILogger> logger`，`ServiceLiveComposition` 在 logger 存在时创建 `ServiceLoggingBridge` 并传给 `ExecutionCommandLane` / `DataQueryLane`；`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 同步把 shared observability logger 透传给 `compose_live_services()`。
3. `ExecutionCommandLane` 与 `DataQueryLane` 现会在 route receipt 生成后调用 `ServiceLoggingBridge`，分别记录 `service.execution.route`、`service.data.query.route`、`service.data.catalog.route`；request ledger 保留为 fixture / fallback discoverability 语义，不再承担 production logging 证据职责。
4. `CapabilityServicesLoopbackFixture` 新增可选 `logging_bridge` 注入位，供 smoke 与后续 focused fixture 测试在不扩大 facade API 的前提下复用同一 services logging seam。
5. 新增 `tests/unit/services/bridges/ServiceLoggingBridgeTest.cpp`，focused 覆盖 allowlist attr 投影与缺失 logger fail-closed contract；新增 `tests/integration/services/CapabilityServicesLoggingIntegrationTest.cpp` 证明 live composition 下 shared logger sink 会收到 execute/query/catalog 三类 services route 记录；`CapabilityServicesSmokeIntegrationTest.cpp` 同轮迁移三条 request-ledger 字段断言到正式 logging sink，并保留 loopback route 行为断言。

## 4. 验证

1. `cmake --build build/vscode-linux-ninja --target dasall_service_logging_bridge_unit_test`
   - 结果：通过。
2. `./build/vscode-linux-ninja/tests/unit/services/bridges/dasall_service_logging_bridge_unit_test`
   - 结果：通过。
3. `cmake --build build/vscode-linux-ninja --target dasall_services_logging_integration_test`
   - 结果：通过。
4. `./build/vscode-linux-ninja/tests/integration/services/dasall_services_logging_integration_test`
   - 结果：通过。
5. `cmake --build build/vscode-linux-ninja --target dasall_services_smoke_integration_test`
   - 结果：通过。
6. `./build/vscode-linux-ninja/tests/integration/services/dasall_services_smoke_integration_test`
   - 结果：通过。

## 5. 非外推边界

1. 本任务只闭合 services owner 的 build-tree logging seam / sink / smoke migration evidence；installed/package authoritative evidence 与跨子系统 e2e 继续留给 `INF-LOG-SYS-FIX-007` / `INF-LOG-FIX-011`。
2. `request ledger` 现在只保留 fixture / fallback discoverability 语义，不表示 services production logging proof 仍依赖 request ledger；正式可观测字段以 shared logging sink 为准。
3. high-risk action 的审计持久化边界没有变化，`ServiceLoggingBridge` 不接管 audit owner 语义。本轮不使用 qemu / kvm。