# CAPSRV-FIX-003 subscription trace 链收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `CAPSRV-GAP-003` / `CAPSRV-FIX-003`。
2. 本轮目标：补齐 `IExecutionService.subscribe()` public ABI 的 trace 链，使 subscription 路径与 execute/query/catalog 一样具备一致的 facade/lane span 语义，并让 overflow 结果在 trace 上保留 `resync_required` / `dropped_count` / `next_cursor`。
3. 完成判定：`ServiceFacade::subscribe()` 已发射 facade span；`ExecutionSubscriptionHub` 已发射 hub lane span；`ServiceTraceBridge` 已能完成 `ExecutionSubscriptionResult` span；`ServiceFacadeTest`、`ServiceTraceBridgeTest`、`CapabilityServicesTraceIntegrationTest`、`CapabilityServicesFailureIntegrationTest` focused 验证通过；本轮不依赖 qemu / kvm，也不把结果外推为 installed / release / soak 证据。

## 2. 本地证据

1. `docs/architecture/DASALL_capability_services子系统详细设计.md` 已把 `ServiceTraceBridge` 定义为 services owner 的 trace bridge，并要求 trace 链保持 `Tool -> ServiceFacade -> lane -> adapter -> external` 的父子关系；subscription capability 虽是 public ABI，但 hub 实现保持 internal-only。
2. 变更前 `services/src/ServiceFacade.cpp` 中 `subscribe()` 与 execute/query/catalog 不同，直接调用 `subscribe_execution_state()`，没有 `start_facade_span()` / `with_span()` / `complete_span()`。
3. 变更前 `services/src/execution/ExecutionSubscriptionHub.h/.cpp` 只有 `metrics_bridge` 依赖，没有 `trace_bridge`，因此 overflow 与 cursor batch 虽有 metrics 证据，但没有 hub lane trace。
4. 变更前 `services/src/bridges/ServiceTraceBridge.h/.cpp` 只支持 command/query/diagnose/data/adapter receipt 的 complete overload，subscription result 不会把 `resync_required` / `dropped_count` / `next_cursor` 投到 span attribute，也不会统一映射 overflow error status。
5. 变更前 focused tests 里没有 subscription trace 专项断言：`ServiceFacadeTest` 不检查 subscribe facade span，`CapabilityServicesTraceIntegrationTest` 不包含 subscribe parent-child 链，`CapabilityServicesFailureIntegrationTest` 也不检查 overflow error span。
6. 本轮已更新 `services/src/ServiceFacade.cpp`、`services/src/execution/ExecutionSubscriptionHub.h/.cpp`、`services/src/bridges/ServiceTraceBridge.h/.cpp` 与 loopback fixture，并补齐 4 个 focused tests；build-tree direct binary 与 `ctest -R` 均已通过。

## 3. 外部参考

1. OpenTelemetry Traces 文档指出：子 span 应与根 span 共享同一 `trace_id`，并通过 `parent_id` 形成层级；span attribute 适合记录对时序不敏感的结构化元数据，span status 则用于显式表达 `Ok` / `Error` 结果。本轮 trace 收口直接采用这条实践：subscription hub span 作为 facade span 的子 span，overflow 事实通过 attribute 和 `Error` status 表达，而不是丢失在 metrics-only 路径中。

## 4. 设计结论

### 4.1 根因收口

1. `CAPSRV-GAP-003` 的根因不是 tracer provider 不可用，而是 subscription 这条 public ABI 路径根本没有接入与其他 services public 方法一致的 trace 控制流。
2. `ServiceFacade::subscribe()` 缺 facade root span，导致 tool parent context 无法折叠到 subscription public 方法上。
3. `ExecutionSubscriptionHub` 缺 lane span 与 result completion 语义，导致 overflow / resync 只能出现在 metrics 与 public result 中，不能进入 trace。

### 4.2 本轮决定

1. `ServiceFacade::subscribe()` 与 execute/query/catalog 对齐：使用 `start_facade_span()`、`with_span()`、`complete_span()` 包裹真正的 subscription handler。
2. `ExecutionSubscriptionHub` 增加内部 `trace_bridge` 依赖，并在 subscribe 入口发射固定命名的 hub lane span：`services.lane.execution.subscription_hub.subscribe`。
3. `ExecutionSubscriptionHub` 在 lane span 上记录 `services.stream_kind`，让 public ABI 的 stream 语义在 trace 中可见，而不把 hub 内部 buffer 细节暴露到公共 ABI。
4. `ServiceTraceBridge` 新增 `ExecutionSubscriptionResult` complete overload，把 `resync_required`、`dropped_count`、`next_cursor` 固定为 span attribute，并按 `result.error` 映射 overflow error status。
5. focused tests 按职责分层：
   - `ServiceFacadeTest` 只锁 facade subscribe root span。
   - `ServiceTraceBridgeTest` 只锁 subscription result complete 语义。
   - `CapabilityServicesTraceIntegrationTest` 只锁 facade -> hub 的 parent-child trace chain。
   - `CapabilityServicesFailureIntegrationTest` 只锁 overflow error span 与 attribute。

### 4.3 边界与不外推项

1. 本轮不把 subscription public ABI 扩成 adapter/external trace：当前 subscription 数据来自 internal hub，不额外伪造 adapter/external span。
2. 本轮不改变 `ExecutionSubscriptionHub` 的 cursor/batch、`drop_oldest`、`resync_required` 协议，只补 trace 证据，不改业务协议。
3. 本轮不处理 `ServiceLiveComposition` 是否对外暴露 subscribe hot path 的更高层组合根问题；那属于 production composition / installed evidence 范围，不是 `CAPSRV-FIX-003` 的最小任务边界。
4. 本轮不引入 qemu / kvm、installed package 或 external backend 证据；closeout 只覆盖 build-tree focused trace 语义。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | subscribe public ABI 必须与其他 facade 方法共享 root span 语义 | `services/src/ServiceFacade.cpp` |
| D2 | subscription internal hub 必须发出 lane span，并保留 stream_kind | `services/src/execution/ExecutionSubscriptionHub.h`、`services/src/execution/ExecutionSubscriptionHub.cpp` |
| D3 | subscription result 必须能在 trace 上表达 overflow / resync 事实 | `services/src/bridges/ServiceTraceBridge.h`、`services/src/bridges/ServiceTraceBridge.cpp` |
| D4 | loopback fixture 必须把 trace bridge 传到 hub，避免 integration 仍走断链路径 | `tests/mocks/include/CapabilityServicesLoopbackFixture.h` |
| D5 | focused tests 必须分层守住 facade、bridge、trace chain 与 overflow error span | `tests/unit/services/ServiceFacadeTest.cpp`、`tests/unit/services/bridges/ServiceTraceBridgeTest.cpp`、`tests/integration/services/CapabilityServicesTraceIntegrationTest.cpp`、`tests/integration/services/CapabilityServicesFailureIntegrationTest.cpp` |
| D6 | closeout 结论需回写总账、交付物与工作日志 | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 6. Build 三件套

1. 代码目标：让 subscription public ABI 具备 facade/hub trace span，并在 overflow 结果上保留 trace attribute 与 error status。
2. 测试目标：focused tests 分别覆盖 subscribe facade span、subscription result complete、facade/hub trace chain 与 overflow error span。
3. 验收命令：
   - `cmake --build build/vscode-linux-ninja --target dasall_service_facade_unit_test dasall_service_trace_bridge_unit_test dasall_services_trace_integration_test dasall_services_failure_integration_test -j4`
   - `./build/vscode-linux-ninja/tests/unit/services/dasall_service_facade_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/services/bridges/dasall_service_trace_bridge_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/services/dasall_services_trace_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/services/dasall_services_failure_integration_test`
   - `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(ServiceFacadeTest|ServiceTraceBridgeTest|CapabilityServicesTraceIntegrationTest|CapabilityServicesFailureIntegrationTest)$'`

## 7. Rollout Checklist

1. `subscribe()` 已与 execute/query/catalog 使用同一套 facade trace 包裹模式。
2. `ExecutionSubscriptionHub` 已保留 `stream_kind` 与 overflow 事实的 trace attribute，不泄漏 internal buffer 实现细节。
3. overflow subscription result 会在 facade 与 hub span 上都得到 `Error` status，而不是只留 metrics 证据。
4. direct binary 与 `ctest -R` 都已通过，说明 focused 行为和测试发现性均未回退。
5. 本轮没有把 trace closeout 外推为 services production backend、installed package 或 qemu gate 已完成。

## 8. 风险与回退

1. 当前 subscription trace 只覆盖 public facade + internal hub；如果后续引入真实 external subscription publisher，需要重新评估是否补 adapter/external span，而不是复用当前 internal-only 命名假装已经跨边界。
2. `ServiceTraceBridge::mark_error()` 当前不额外投射 `stage` 到 span attribute；如果后续 trace backend 需要按 stage 做聚合，可在不破坏现有 span 名称的前提下补 attribute，而不应改写本轮固定的 span chain 命名。
3. 本轮没有处理 production composition / installed evidence；若更高层 gate 失败，不应回退为本轮 trace 收口无效。

## 9. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 facade span、hub lane span、subscription result completion、focused tests 与文档回写。
3. Build 三件套已在本机 build tree 完成，且未使用 qemu / kvm。
4. 范围保持在 `CAPSRV-FIX-003`，未扩张到 adapter concrete backend、dynamic registry 或 production observability。

结论：D Gate = PASS；`CAPSRV-FIX-003` 已按 subscription facade/hub trace chain、overflow error span 与 focused regression evidence 收口。
