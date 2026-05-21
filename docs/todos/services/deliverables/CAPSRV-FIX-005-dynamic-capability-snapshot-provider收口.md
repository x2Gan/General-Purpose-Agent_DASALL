# CAPSRV-FIX-005 dynamic capability snapshot / candidate provider 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `CAPSRV-GAP-005` / `CAPSRV-FIX-005`。
2. 本轮目标：收敛 execution/data lanes 对单个 `CapabilitySnapshotView` 与静态 candidate 列表的固化依赖，让 lanes 可以按请求 capability 解析当前 route view，并让 build-tree live composition 从当前 registry 动态生成多 capability / 多 dataset 视图。
3. 完成判定：`ExecutionCommandLane` / `DataQueryLane` 已支持 provider-backed `CapabilityRouteView`；`ServiceLiveComposition` 已按当前 registry 动态生成 route view；`AdapterRouterTest`、`DataQueryLaneTest`、`ExecutionCommandLaneTest`、`CapabilityServicesProfileIntegrationTest` 与 `CapabilityServicesProductionAdapterIntegrationTest` 已覆盖 snapshot mismatch、hot update、availability unknown fail-closed 与 production multi-dataset route resolution；本轮不使用 qemu / kvm。

## 2. 本地证据

1. `docs/todos/DASALL_子系统查漏补缺专项记录.md` 的 `CAPSRV-GAP-005` 明确指出：`ExecutionCommandLaneDependencies` / `DataQueryLaneDependencies` 直接持有单个 `CapabilitySnapshotView` 与 candidate 列表，Data 路由用 `request.dataset` 对比该 snapshot，会掩盖多 dataset / hot update 的生产问题。
2. `services/src/adapters/AdapterRouter.cpp` 会先校验 `capability_snapshot.capability_id == request.capability_id`，随后再基于 route class、candidate capability、trust 与 availability 做 fail-closed 选择；因此只要 lane 固化了过期 snapshot，就会在 router 入口前把多 capability / 多 dataset 请求拦死。
3. `docs/todos/services/deliverables/CAPSRV-FIX-004-production-adapter-registry收口.md` 已明确把 dynamic capability snapshot / provider 排除在上一轮之外：上一轮只闭合 production handler 注入与 registry seam，没有解决 registry 当前状态如何按 capability 投影到 execution/data lanes。
4. 本轮代码已更新 `services/src/execution/ExecutionCommandLane.*`、`services/src/data/DataQueryLane.*`、`services/src/ServiceLiveComposition.cpp`，并扩展 `tests/unit/services/adapters/AdapterRouterTest.cpp`、`tests/unit/services/data/DataQueryLaneTest.cpp`、`tests/unit/services/execution/ExecutionCommandLaneTest.cpp`、`tests/integration/services/CapabilityServicesProductionAdapterIntegrationTest.cpp`。

## 3. 外部参考

1. Mark Seemann 在 Composition Root 模式总结中强调：对象图应尽量靠近应用入口统一组合，应用代码应只消费注入结果，而不应在业务路径里自行持有或扩散组合细节；DI 容器或装配逻辑只应停留在 Composition Root。这个原则直接约束本轮方案：动态 route view 的 owner 应该是 live composition root / provider，而不是让 execution/data lanes 或测试夹具继续复制静态 snapshot/candidate 列表。

## 4. 设计结论

### 4.1 根因收口

1. `CAPSRV-GAP-005` 的根因不是 `AdapterRouter` 缺 capability-aware 选择，而是 execution/data lanes 在构造时就把 snapshot/candidate 复制成固定值，后续请求无法按当前 capability/dataset 重新取视图。
2. `CAPSRV-FIX-004` 虽然引入了 `ServiceLiveAdapterRegistry`，但 live composition 仍在 lane 初始化时一次性生成 execution/data snapshot，因此 production registry 的“当前状态”没有继续传到路由面。
3. snapshot mismatch 与 unknown availability 本来就应该 fail-closed；缺口在于之前缺少针对这些语义的 focused regression，容易被单 fixture snapshot 掩盖。

### 4.2 本轮决定

1. 在 services internal 路由输入层新增 `CapabilityRouteView`，并为 `ExecutionCommandLaneDependencies` / `DataQueryLaneDependencies` 增加可选 `resolve_route_view` provider；未提供 provider 时保持原有静态 snapshot/candidate 行为，确保向后兼容。
2. `ExecutionCommandLane` / `DataQueryLane` 每次请求都先解析当前 `CapabilityRouteView`，再构造 `AdapterRouteRequest`；因此 router 看到的是“当前 capability 对应的 snapshot + candidates”，而不是 lane 构造时的固定快照。
3. `ServiceLiveComposition` 改为从当前 `registered_candidates_` 按 capability 动态生成 `CapabilityRouteView`，让 build-tree production composition 可以在同一 live registry 下解析多个 dataset，而不需要复制 lane 或重建静态 snapshot。
4. focused tests 分层守住行为：`AdapterRouterTest` 锁 snapshot mismatch 与 availability unknown fail-closed；`DataQueryLaneTest` / `ExecutionCommandLaneTest` 锁 hot update；`CapabilityServicesProductionAdapterIntegrationTest` 锁 live registry 的 multi-dataset route resolution；`CapabilityServicesProfileIntegrationTest` 继续守 profile-derived route / timeout / cache policy 相邻回归。

### 4.3 边界与不外推项

1. 本轮没有新增 services public ABI，也没有扩展 `profiles` schema；`CapabilityRouteView` 与 provider 都保持 internal-only。
2. 本轮不处理 `CAPSRV-GAP-006` 的 production observability / health sinks、不处理 `CAPSRV-GAP-007` 的 caller-domain owner，也不把结果外推为 `CAPSRV-GAP-008` 的 installed / release / soak 证据。
3. 本轮未使用 qemu / kvm；authoritative 证据只到 build-tree focused unit / integration。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | lane 不再固化单 snapshot/candidate，必须支持按 capability 动态取视图 | `services/src/execution/ExecutionCommandLane.h/.cpp`、`services/src/data/DataQueryLane.h/.cpp` |
| D2 | dynamic route view owner 设在 live composition root，而不是 router 调用侧或测试夹具 | `services/src/ServiceLiveComposition.cpp` |
| D3 | snapshot mismatch 与 unknown availability 必须显式 fail-closed | `tests/unit/services/adapters/AdapterRouterTest.cpp` |
| D4 | execution/data 都要证明在不重建 lane 的前提下支持 hot update | `tests/unit/services/execution/ExecutionCommandLaneTest.cpp`、`tests/unit/services/data/DataQueryLaneTest.cpp` |
| D5 | production registry 必须能在同一 live composition 下解析多个 dataset | `tests/integration/services/CapabilityServicesProductionAdapterIntegrationTest.cpp` |
| D6 | closeout 结论需回写总账与 worklog | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 6. Build 三件套

1. 代码目标：让 execution/data lanes 通过 provider 解析当前 `CapabilityRouteView`，并让 live composition 基于当前 registry 动态生成 per-capability snapshot/candidates。
2. 测试目标：覆盖 snapshot mismatch、availability unknown fail-closed、execution/data hot update，以及 production multi-dataset route resolution。
3. 验收命令：
   - `Build_CMakeTools(["dasall_adapter_router_unit_test","dasall_data_query_lane_unit_test","dasall_execution_command_lane_unit_test","dasall_services_profile_integration_test","dasall_services_production_adapter_integration_test"])`
   - `RunCtest_CMakeTools(["AdapterRouterTest","DataQueryLaneTest","ExecutionCommandLaneTest","CapabilityServicesProfileIntegrationTest"])`：当前 VS Code CMake Tools 在本仓库返回泛化 `生成失败`，因此 authoritative 结果改用同一 build tree 的 direct binaries。
   - `./build/vscode-linux-ninja/tests/unit/services/adapters/dasall_adapter_router_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/services/data/dasall_data_query_lane_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/services/execution/dasall_execution_command_lane_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/services/dasall_services_profile_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/services/dasall_services_production_adapter_integration_test`

## 7. Rollout Checklist

1. `ExecutionCommandLane` 与 `DataQueryLane` 已具备 provider-backed `CapabilityRouteView`，provider 缺省时仍保留原有静态回退。
2. `ServiceLiveComposition` 已根据当前 `registered_candidates_` 动态生成 per-capability snapshot / candidates，而不是把 execution/data snapshot 固化在构造时。
3. `AdapterRouterTest` 已锁定 snapshot mismatch 与 availability unknown fail-closed，后续改动不能再用模糊 fallback 掩盖这两类错误。
4. `DataQueryLaneTest` 与 `ExecutionCommandLaneTest` 已证明多 dataset / 多 capability hot update 不需要重建 lane。
5. `CapabilityServicesProductionAdapterIntegrationTest` 已证明同一 live registry 可解析 `inventory.devices` 与 `inventory.alerts` 两个 dataset。
6. 本轮未使用 qemu / kvm，且没有把结果外推为 installed / release / soak 证据。

## 8. 风险与回退

1. 当前 `ServiceLiveAdapterRegistry` 仍未区分 action/query 级 capability catalog；live composition 对 query path 默认声明 `default` / `catalog.list`，这是为了保持 current registry -> route view 的最小收敛面，后续若需要 richer projection catalog，应在 registry/source owner 处继续扩，不应把 projection 语义散落回 lane。
2. VS Code `RunCtest_CMakeTools` 在本仓库当前仍会对 focused tests 返回泛化 `生成失败`；本轮 authoritative evidence 已收敛到 direct binaries，不应把该工具态问题误判为代码失败。
3. 本轮没有验证 installed package、真实外部 endpoint、release runner 或 soak；若这些门禁失败，不应回退为本轮 dynamic snapshot/provider 收口无效。

## 9. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 lane provider、live composition 动态 route view、focused unit/integration 和 traceability 文档回写。
3. Build 三件套已在本机 build tree 完成，且未使用 qemu / kvm。
4. 范围保持在 `CAPSRV-FIX-005`，未扩张到 observability、caller-domain 或 installed/release 证据。

结论：D Gate = PASS；`CAPSRV-FIX-005` 已按 dynamic capability snapshot / candidate provider、hot update regression 与 production multi-dataset route resolution 收口。