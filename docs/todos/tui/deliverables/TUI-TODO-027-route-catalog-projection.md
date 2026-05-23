# TUI-TODO-027 route catalog projection

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只冻结 TUI route catalog projection 的 DTO 字段与 seam 传递口径：`current_route` 与 `candidate_routes` 必须显式表达 current provider/model/depth、`verification_state`、`health`、`profile_allowlisted` 与 additive `disabled_reasons`。
2. 本任务不把 daemon route catalog 真正接入 `TuiModelSelector` 渲染，也不实现 next-turn preference 的 submit echo 或 effective route 回显；这些继续分别后置到 `TUI-TODO-028` 与 `TUI-TODO-029`。
3. 本任务完成标准是：route catalog projection 能表达 `Auto` / `PreferDepth` / `PinModel` 所需的当前 route 与 fail-closed 候选 route 摘要，同时保持 summary-only 边界，不泄漏 provider secret、完整 allowlist 文档、profile 文件路径或 profile 内容。

## 2. 本地事实与外部参考

1. TUI 详设 6.2~6.4、7.4 与 10 Phase 6 已把 route selector 真链路拆成三步：027 先冻结 projection 字段，028 再接 selector 消费，029 最后验证 submit echo 与 effective route 回显。本轮不能把三个子问题混成一次性“真链路完成”。
2. `docs/todos/tui/deliverables/TUI-TODO-004-next-turn-preference承载决策.md` 已冻结 `NextTurnPreference` 的 owner 边界：access/runtime typed request-scope carrier -> llm-local route input，明确拒绝 `request_context`、`client_capabilities` 与 profile override，并固定 `Auto` 无显式偏好、`PreferDepth` advisory、`PinModel` fail-closed 的规则。
3. `docs/todos/tui/deliverables/TUI-TODO-015-model-selector-fake交互基线.md` 已证明 fake selector 的本地 draft/apply/cancel、depth tier 聚合与 disabled reason 展示语义成立；但在 027 前，route catalog DTO 仍缺显式 `verification_state`、`health` 与 allowlist 字段，导致后续只能继续依赖 disabled reason 文案。
4. 外部约束采用 Kubernetes API conventions 与 Google AIP-193 的共同结论：machine-readable 状态字段应与人类可读 message 分离、状态字段应保持 bounded/summary-only、引用对象的敏感策略或 secret 不应被整个复制进 projection。本轮 DTO 只冻结 bounded summary 字段，不扩写 raw policy/secret dump。

## 3. 冻结结论

### 3.1 route catalog DTO 字段表

1. `apps/tui/src/data/TuiProjectionTypes.h` 已把 `TuiModelRouteProjection` 冻结为：`current_provider_id`、`current_model_id`、`current_depth_tier`、`verification_state`、`health`、`profile_allowlisted`、`disabled_reasons` 与 `next_preference`。
2. 同一 header 已把 `TuiRouteCatalogEntry` 冻结为：`provider_id`、`model_id`、`depth_tier`、`verification_state`、`health`、`profile_allowlisted`、`selectable` 与 `disabled_reasons`。
3. `TuiRouteCatalogView` 的 top-level shape 保持不变，仍只包含 `current_route`、`candidate_routes` 与 route-level `disabled_reasons`；027 只是把 nested projection 的 summary 字段补齐，而不是发明新的 owner API。
4. 默认值口径保持 additive 且最小：`verification_state` 与 `health` 默认为空字符串，`profile_allowlisted` 默认为 `true`。这保证旧调用点在未填字段时仍可编译，同时让 fail-closed 场景能通过显式 `false` 与 stable disabled reason 被清楚表达。

### 3.2 fixture、transport 与 contract guard

1. `apps/tui/src/data/FakeScenarioCatalog.h` 的 route catalog builder 现支持 `verification_state`、`health` 与 `profile_allowlisted` 参数，`route_switch` 场景把 `provider-local/deep-reasoner` 固定为 `pending`、`healthy`、`false`，并保留 `verification_pending` / `allowlist_blocked` additive disabled reason。
2. `apps/tui/src/ipc/TuiIpcController.cpp` 的 route catalog encode/decode 已接齐上述字段，避免 028/029 再去追加 transport shape。
3. `tests/unit/tui/DaemonTuiDataSourceContractTest.cpp` 已补 route catalog roundtrip 断言，证明 daemon data source 不会在 controller-backed seam 上丢失 `verification_state`、`health` 与 `profile_allowlisted`。
4. `tests/unit/tui/TuiRouteCatalogProjectionTest.cpp`、`tests/unit/tui/CMakeLists.txt` 与 `tests/unit/tui/TuiUnitTopologySmokeTest.cpp` 已把 027 的 focused behavior 与 discoverability guard 一并注册，避免字段冻结只停留在“头文件写了，但无测试守卫”。

### 3.3 summary-only 边界

1. 027 明确允许 TUI projection 暴露 current route、candidate route、`verification_state`、`health`、`profile_allowlisted` 与 machine-readable `disabled_reasons`。
2. 027 明确禁止 TUI projection 暴露 provider API key、secret page、完整 allowlist 文档、profile 文件路径、profile YAML/raw JSON 或其它 owner-internal 策略载荷。
3. `TuiRouteCatalogProjectionTest` 已直接读取 header 文本，断言存在 `verification_state` / `health` / `profile_allowlisted`，且不存在 `api_key`、`secret`、`profile_path`、`profile_yaml` 等敏感字段名。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| projection DTO 冻结 | apps/tui/src/data/TuiProjectionTypes.h、apps/tui/src/data/FakeScenarioCatalog.h、tests/unit/tui/TuiProjectionTypesTest.cpp、tests/unit/tui/TuiRouteCatalogProjectionTest.cpp | TuiProjectionTypesTest、TuiRouteCatalogProjectionTest | Build_CMakeTools(buildTargets=["dasall_tui_projection_types_unit_test","dasall_tui_route_catalog_projection_unit_test"]) |
| route seam 传递 | apps/tui/src/ipc/TuiIpcController.cpp、tests/unit/tui/DaemonTuiDataSourceContractTest.cpp | TuiIpcControllerTest、DaemonTuiDataSourceContractTest | Build_CMakeTools(buildTargets=["dasall_tui_ipc_controller_unit_test","dasall_tui_daemon_data_source_contract_unit_test"]) |
| selector 既有基线兼容 | apps/tui/src/data/FakeScenarioCatalog.h、tests/unit/tui/TuiRouteCatalogFilterTest.cpp | TuiFakeScenarioCatalogTest、TuiRouteCatalogFilterTest | Build_CMakeTools(buildTargets=["dasall_tui_fake_scenario_catalog_unit_test","dasall_tui_route_catalog_filter_unit_test"]) |
| discoverability guard | tests/unit/tui/CMakeLists.txt、tests/unit/tui/TuiUnitTopologySmokeTest.cpp | TuiUnitTopologySmokeTest | Build_CMakeTools(buildTargets=["dasall_tui_unit_topology_smoke_unit_test"]) |

## 5. 结果

1. `Build_CMakeTools(buildTargets=["dasall_tui_projection_types_unit_test"])` 通过；新增字段没有破坏既有 DTO baseline target。
2. `Build_CMakeTools(buildTargets=["dasall_tui_route_catalog_projection_unit_test","dasall_tui_unit_topology_smoke_unit_test"])` 通过；027 的 focused behavior target 与 discoverability smoke target 均成功编译并链接。
3. `ListBuildTargets_CMakeTools()` 与 `ListTests_CMakeTools()` 可发现 `dasall_tui_route_catalog_projection_unit_test` 与 `TuiRouteCatalogProjectionTest`，说明 CMake discoverability 已接通。
4. `RunCtest_CMakeTools(tests=["TuiRouteCatalogProjectionTest","TuiUnitTopologySmokeTest"])` 仍命中仓库已知泛化 `生成失败`；已按 repo 当前可执行回退口径直接执行 `./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_route_catalog_projection_unit_test && ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_unit_topology_smoke_unit_test`，通过。
5. `Build_CMakeTools(buildTargets=["dasall_tui_fake_scenario_catalog_unit_test","dasall_tui_route_catalog_filter_unit_test","dasall_tui_ipc_controller_unit_test","dasall_tui_daemon_data_source_contract_unit_test"])` 通过；027 的字段冻结没有破坏 fake scenario、selector filter、IPC controller 与 daemon data source contract 的相邻切片。
6. 已按同一回退口径直接执行 `./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_fake_scenario_catalog_unit_test && ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_route_catalog_filter_unit_test && ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_route_catalog_projection_unit_test && ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_ipc_controller_unit_test && ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_daemon_data_source_contract_unit_test && ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_unit_topology_smoke_unit_test`，均通过。
7. 当前结果证明：TUI route catalog projection 已具备稳定字段基线，能独立表达 current route、候选 route 的 verification/health/allowlist fail-closed 摘要，并保持 summary-only 边界。当前结果不代表 `TUI-TODO-028` 的 daemon selector consumption 或 `TUI-TODO-029` 的 submit echo 已完成。

结论：`TUI-TODO-027` 已闭合。route catalog projection 现在拥有稳定、可测试、可传输的 DTO 字段基线，能为后续 `TUI-TODO-028` / `TUI-TODO-029` 提供不依赖 disabled reason 文案猜测的 route summary surface，同时继续守住 owner boundary 与 no-secret/no-profile-dump 约束。