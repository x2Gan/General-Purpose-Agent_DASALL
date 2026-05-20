# CAPSRV-FIX-004 production adapter registry / backend handlers 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `CAPSRV-GAP-004` / `CAPSRV-FIX-004`。
2. 本轮目标：在 build-tree production live composition 中建立显式 `ServiceLiveAdapterRegistry` / backend handler 接线，让 `ServiceLiveComposition` 与 `RuntimeLiveDependencyComposition` 能证明 `local_platform`、`local_service`、`remote_service` 三类后端的 production handler 注入、profile 派生与 fail-closed 行为，而不再只停留在 loopback fixture callback seam。
3. 完成判定：`ServiceLiveComposition` 已暴露 public adapter registry seam；runtime composition 已按 build manifest 派生 `local_platform_route_enabled` 并显式注入 platform/local/remote handlers；health probe readiness 已基于 registered candidates；`AdapterRouterTest`、三类 adapter unit、`CapabilityServicesProfileIntegrationTest`、`CapabilityServicesProductionAdapterIntegrationTest` 以及 `DaemonRuntimeLiveDependencyCompositionTest` / `GatewayRuntimeLiveDependencyCompositionTest` focused 验证通过；本轮不依赖 qemu / kvm，也不把结果外推为 installed / release / soak 证据。

## 2. 本地证据

1. `docs/architecture/DASALL_capability_services子系统详细设计.md` 已把 `AdapterRouter` 定义为 services owner 的语义路由层，要求它根据 `RuntimePolicySnapshot` / `ServicePolicyView` 派生的 capability、trust、availability 在 `LocalPlatformAdapter`、`LocalServiceAdapter`、`RemoteServiceAdapter` 三类后端间选择实现；`ServiceHealthProbe` 则负责聚合 adapter readiness 供 infra/health 使用。
2. 变更前 `services/include/ServiceLiveComposition.h` / `services/src/ServiceLiveComposition.cpp` 只有 build-tree 内部组合逻辑，没有 public production adapter registry / route binding seam；live composition 虽能拼装 services facade，但不能显式证明当前 runtime composition 注入了哪类 backend handler，也不能把 handler availability / trust / route class 和 health readiness 收口到同一组合根。
3. 变更前 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 仍把 `local_platform_route_enabled` 固定为 `false`，未从 build manifest 的 `platform_hal` 模块状态派生；因此 profile 启停无法真实控制 `local_platform` route，production composition 也无法证明 platform HAL 接线是否存在。
4. 变更前 focused tests 只有 router/adapter/profile fixture coverage，没有一条 build-tree production composition 集成测试去锁定 explicit registry injection、local_platform enabled/disabled、local_service unavailable remote fallback、remote timeout 与 fallback forbidden。
5. 本轮已更新 `services/include/ServiceLiveComposition.h`、`services/src/ServiceLiveComposition.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`tests/integration/services/CMakeLists.txt`，新增 `tests/integration/services/CapabilityServicesProductionAdapterIntegrationTest.cpp`，并用 focused `ctest` 与 daemon/gateway runtime live composition regression 收口 build-tree 证据。

## 3. 外部参考

1. Ports-and-Adapters / composition-root 的通用实践要求：具体 backend 的装配权应放在组合根，由上层策略决定可选 route，领域/车道代码只消费抽象 handler 契约。本轮 closeout 直接遵循这条原则：`AdapterRouter` 仍负责语义路由选择，但具体 platform/local/remote handler 的注入、启停和 health readiness 回写明确落在 live composition root，而不是散落在 test fixture 或硬编码布尔值里。

## 4. 设计结论

### 4.1 根因收口

1. `CAPSRV-GAP-004` 的根因不是三类 adapter 本身不存在，而是 production live composition 没有公开可组合的 adapter registry / backend handler seam，导致 services 层无法证明“当前实际注入了什么后端”。
2. `RuntimeLiveDependencyComposition` 把 `local_platform_route_enabled` 固定为 `false`，使 profile 对 platform HAL 的裁剪无法真实影响 adapter route，进而让 `LocalPlatformAdapter` 的接线只存在于代码结构而不在 production composition 证据里。
3. health probe readiness 若只依赖静态布尔值，而不依赖已注册 candidate，就可能在 backend 未真正注入时给出误导性的 ready 结论。

### 4.2 本轮决定

1. 在 `ServiceLiveComposition.h` 新增 public `ServiceLiveRequestKind`、`ServiceLiveRouteKind`、`ServiceLiveTrustClass`、`ServiceLiveAvailabilityState`、`ServiceLiveTransportOutcome`、`ServiceLiveBackendRequest`、`ServiceLiveBackendResult`、`ServiceLiveRouteBinding` 与 `ServiceLiveAdapterRegistry`，把 production backend wiring 变成显式组合根契约。
2. 在 `ServiceLiveComposition.cpp` 中把 public route binding 转成内部 adapter candidate / handler，按 registry 构建 `local_platform`、`local_service`、`remote_service` 三类 candidate，并让 health readiness 基于 registered candidates 与 policy 派生，而不是只看静态开关。
3. 在 `RuntimeLiveDependencyComposition.cpp` 中复用 build manifest 解析结果，从 `platform_hal` 模块状态派生 `local_platform_route_enabled`，并显式注入 `runtime.live.local_platform`、`runtime.live.local_service`、`runtime.live.remote_service` 三类 route binding 与 backend handler。
4. focused tests 分层守住行为：`CapabilityServicesProductionAdapterIntegrationTest` 锁 production-composed local_platform enabled/disabled、local_service unavailable remote fallback、remote timeout、fallback forbidden；相邻 `AdapterRouterTest`、三类 adapter unit、`CapabilityServicesProfileIntegrationTest` 守住路由与适配器语义；`DaemonRuntimeLiveDependencyCompositionTest` / `GatewayRuntimeLiveDependencyCompositionTest` 守住 app live composition 没有被新 registry seam 打断。

### 4.3 边界与不外推项

1. 本轮只收口 build-tree production composition 的 adapter registry / handler seam，不宣称 installed package、release runner 或 qemu/kvm guest 内的 external backend 证据已经完成。
2. 本轮不处理 `CAPSRV-GAP-005` 的 dynamic capability snapshot / candidate provider；当前 registry 解决的是 production handler 注入与 availability/trust/route-class 回写，不是多 capability 热更新问题。
3. 本轮不处理 `CAPSRV-GAP-006` 的 production observability / health sinks 注入；这里的 health probe closeout 只覆盖 readiness 派生口径，不覆盖 audit/metrics/trace/health sink 的生产注册。
4. 本轮不处理 `CAPSRV-GAP-007` caller-domain owner 边界，也不把结果外推为 `CAPSRV-GAP-008` installed / release / soak 证据。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | production composition 必须暴露显式 adapter registry / route binding seam | `services/include/ServiceLiveComposition.h` |
| D2 | live composition 必须按 registry 生成 adapter candidates、handler bridge 与 health readiness | `services/src/ServiceLiveComposition.cpp` |
| D3 | runtime composition 必须按 build manifest 派生 `local_platform_route_enabled` 并注入 platform/local/remote handlers | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| D4 | focused integration 必须锁 production-composed registry injection、timeout、fallback 和 fail-closed | `tests/integration/services/CMakeLists.txt`、`tests/integration/services/CapabilityServicesProductionAdapterIntegrationTest.cpp` |
| D5 | 相邻 app live composition 不能因新 registry seam 回退 | `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` |
| D6 | closeout 结论需回写总账、交付物与工作日志 | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 6. Build 三件套

1. 代码目标：让 build-tree production live composition 明确接入 `local_platform` / `local_service` / `remote_service` backend handlers，并使 route availability / trust / route class / health readiness 都由同一 registry seam 收口。
2. 测试目标：focused tests 覆盖 production adapter registry 行为，且 daemon/gateway runtime live composition 回归继续通过。
3. 验收命令：
   - `cmake --build build/vscode-linux-ninja --target dasall_adapter_router_unit_test dasall_local_platform_adapter_unit_test dasall_local_service_adapter_unit_test dasall_remote_service_adapter_unit_test dasall_services_profile_integration_test dasall_services_production_adapter_integration_test dasall_access_daemon_runtime_live_dependency_composition_test dasall_access_gateway_runtime_live_dependency_composition_test -j4`
   - `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(AdapterRouterTest|LocalPlatformAdapterTest|LocalServiceAdapterTest|RemoteServiceAdapterTest|CapabilityServicesProfileIntegrationTest|CapabilityServicesProductionAdapterIntegrationTest)$'`
   - `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(DaemonRuntimeLiveDependencyCompositionTest|GatewayRuntimeLiveDependencyCompositionTest)$'`

## 7. Rollout Checklist

1. `ServiceLiveComposition` 已具备 public adapter registry / route binding seam，不再只能靠内部默认装配。
2. `RuntimeLiveDependencyComposition` 已按 build manifest 派生 `local_platform_route_enabled`，显式注入 platform/local/remote handlers，而不是硬编码 `false`。
3. services health readiness 已与 registered candidates 对齐，不再脱离真实 handler 注入状态。
4. focused `ctest` 已覆盖 adapter/router/profile/production-adapter 6 项，daemon/gateway runtime live composition 2 项也已通过。
5. 本轮未使用 qemu / kvm，且没有把 build-tree closeout 外推为 installed / release / soak 证据。

## 8. 风险与回退

1. 当前 registry seam 解决的是 production handler 注入与 fail-closed 证明；如果后续需要多 capability / dataset 动态切换，仍应把 `CAPSRV-GAP-005` 的 snapshot/provider 单独收口，而不是继续把静态 registry 叠成动态目录。
2. 本轮 focused 验证发生过一次 VS Code `RunCtest_CMakeTools` 泛化 `生成失败` 的工具态问题；authoritative 证据已改用等价的显式 `ctest --test-dir build/vscode-linux-ninja` 固定，不应把该工具问题误判为产品代码失败。
3. 本轮没有验证 installed package、真实外部 endpoint、release-runner 或 long-running soak；若这些门禁失败，不应回退为本轮 production adapter registry 收口无效。

## 9. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 public registry seam、runtime handler injection、focused integration / runtime regressions 与 traceability 文档回写。
3. Build 三件套已在本机 build tree 完成，且未使用 qemu / kvm。
4. 范围保持在 `CAPSRV-FIX-004`，未扩张到 dynamic snapshot/provider、production observability、caller-domain owner 或 installed/release/soak 证据。

结论：D Gate = PASS；`CAPSRV-FIX-004` 已按 production adapter registry / backend handler seam、profile-derived local_platform route 与 focused runtime regression evidence 收口。