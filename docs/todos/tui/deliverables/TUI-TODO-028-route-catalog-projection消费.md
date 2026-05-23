# TUI-TODO-028 route catalog projection 消费

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只把 027 已冻结的 route catalog projection 显式字段真正接入 `TuiModelSelector` 消费：pin option label 必须能展示 `verification_state`、`health` 与 `depth_tier` 摘要，disabled route 仍需 fail-closed 并保留稳定 disabled reason 文案。
2. 本任务不扩写 `NextTurnPreference` submit/echo 链路，也不把 effective route 回显一并做完；这些继续后置到 `TUI-TODO-029`。
3. 本任务不把 `TuiApp` 的 `selector_mode` 刷新逻辑扩大为新的 app/data source 变更面。当前 app 已在 route catalog 成功后调用 `selector_.set_route_catalog(*result.route_catalog)`，因此只要 selector 本地 option 生成逻辑完成 projection 消费，preview modal 就会自然展示新摘要。

## 2. 本地事实与设计依据

1. TUI 详设 6.3、6.4 与 9.5.6 明确要求 selector 过滤与展示消费 allowlist、verification 与 health summary，并在 `PinModel` 不满足条件时 fail-closed；A.4 样例明确给出类似 `verified healthy depth=standard` 的 option 摘要口径。
2. `docs/todos/tui/deliverables/TUI-TODO-027-route-catalog-projection.md` 已冻结 `TuiModelRouteProjection` / `TuiRouteCatalogEntry` 的 `verification_state`、`health`、`profile_allowlisted` 与 additive `disabled_reasons` 字段，并通过 IPC/data source roundtrip 证明 transport seam 已具备这些字段。
3. 本轮起点的真实缺口只在 selector 本地：`apps/tui/src/view/TuiModelSelector.cpp` 的 `build_pin_label()` 仍只输出 `provider/model (depth)`，`make_current_route_entry()` 没有复制 `verification_state`、`health`、`profile_allowlisted`，`render_disabled_reason()` 也还只是下划线转空格，导致 daemon route catalog 即使已带显式字段，selector 仍无法展示设计要求的 bounded summary。
4. 本轮没有扩大到 app 层的原因已经通过 focused test 判别：只要 route catalog 进入 `TuiModelSelector`，selector preview 就能完成展示；不存在必须先补 `selector_mode` refresh 才能让 028 成立的证据。

## 3. 实现结论

### 3.1 selector option 消费显式 projection 摘要

1. `apps/tui/src/view/TuiModelSelector.cpp` 新增 projection summary 拼装逻辑，`build_pin_label()` 现会在字段可用时输出 `provider/model [verified healthy depth=standard]` 这类稳定摘要；若旧路径未提供显式状态字段，则继续回退到既有 `provider/model (depth)` 形式，避免破坏 incomplete fixture。
2. `make_current_route_entry()` 现会把 `current_route` 上的 `verification_state`、`health` 与 `profile_allowlisted` 复制到 selector 内部 entry，确保 current route 与 candidate route 共享同一展示口径，而不是只有 candidate route 能看到显式摘要字段。

### 3.2 disabled reason 稳定人类文案

1. `render_disabled_reason()` 现对已冻结 reason code 做稳定映射：`verification_pending -> not verified`、`allowlist_blocked -> profile disallows route`、`provider_unhealthy -> provider unhealthy`。
2. 未命中特定映射的 reason code 仍保持 additive fallback：下划线转空格 + 去重拼接，避免 selector 因未来新增 reason code 直接丢失用户面解释。

### 3.3 daemon-backed focused evidence

1. 新增 `tests/unit/tui/TuiModelSelectorDaemonTest.cpp`，通过真实 `DaemonTuiDataSource` + scripted IPC 驱动 `route_catalog()`，再把返回的 route catalog 交给 `TuiModelSelector`，直接证明 daemon-backed projection 会变成 selector option label 与 disabled reason，而不是只在 fake fixture 中成立。
2. 新测试同时断言 request envelope 仍保留 `selector_mode=pin_model`，证明本轮虽然没有修改 `DaemonTuiDataSource.cpp` 的生产代码，但现有 seam 已足以承载 selector-specific route catalog request hint；028 的闭合点是 selector 真消费，不是再扩一个新的 transport owner 语义。
3. `tests/unit/tui/TuiRouteCatalogFilterTest.cpp` 与 `tests/unit/tui/TuiUnitTopologySmokeTest.cpp` 已同步更新，前者把 fake scenario 的期望文案对齐到新的 selector summary/reason mapping，后者注册 `TuiModelSelectorDaemonTest` 的 discoverability guard。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| selector projection summary 消费 | apps/tui/src/view/TuiModelSelector.cpp | TuiModelSelectorDaemonTest | Build_CMakeTools(buildTargets=["dasall_tui_model_selector_daemon_unit_test"]) |
| fake selector 基线兼容 | apps/tui/src/view/TuiModelSelector.cpp、tests/unit/tui/TuiRouteCatalogFilterTest.cpp | TuiRouteCatalogFilterTest | Build_CMakeTools(buildTargets=["dasall_tui_route_catalog_filter_unit_test"]) |
| route projection baseline 不回归 | tests/unit/tui/TuiRouteCatalogProjectionTest.cpp | TuiRouteCatalogProjectionTest | ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_route_catalog_projection_unit_test |
| discoverability guard | tests/unit/tui/CMakeLists.txt、tests/unit/tui/TuiUnitTopologySmokeTest.cpp | TuiUnitTopologySmokeTest | ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_unit_topology_smoke_unit_test |

## 5. 结果

1. `Build_CMakeTools(buildTargets=["dasall_tui_model_selector_daemon_unit_test"])` 通过；新增 daemon-backed focused test target 成功编译并链接。
2. `RunCtest_CMakeTools(tests=["TuiModelSelectorDaemonTest"])` 仍命中仓库当前已知泛化 `生成失败`；已按 repo 当前可执行回退口径直接执行 `./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_model_selector_daemon_unit_test`，通过。
3. `Build_CMakeTools(buildTargets=["dasall_tui_route_catalog_filter_unit_test","dasall_tui_route_catalog_projection_unit_test","dasall_tui_unit_topology_smoke_unit_test"])` 通过；selector 相邻 focused/unit topology target 重新编译成功。
4. 已按同一回退口径直接执行 `./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_route_catalog_filter_unit_test && ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_route_catalog_projection_unit_test && ./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_unit_topology_smoke_unit_test`，通过。
5. 当前结果证明：daemon-backed route catalog projection 已能在 selector option label 中稳定展示 verification/health/depth 摘要，并把 disabled reason 保持为 bounded、用户可读、fail-closed 的文本 surface。本轮结果不代表 `TUI-TODO-029` 的 submit echo / effective route 回显已完成。

结论：`TUI-TODO-028` 已闭合。TUI selector 现在真正消费 daemon route catalog projection，而不是继续依赖旧的 disabled reason 文案猜测 route 状态；同时没有扩大 owner 面到 app submit/echo 或 command migration，继续守住 028 的最小实现边界。