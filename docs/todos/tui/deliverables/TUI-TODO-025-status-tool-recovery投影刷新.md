# TUI-TODO-025 status/tool/recovery 投影刷新

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只闭合 daemon-backed `poll_events()` 到 TUI status surface 的真消费证据：证明 `TuiStatusProjection`、`TuiToolSummaryView` 与 `TuiEventProjection` 可以沿着 `DaemonTuiDataSource -> TuiApp -> TuiReducer -> TuiStatusPanel` 路径刷新到 screen model 和用户可见文本 status panel。
2. 本任务不实现 foreground session close/query、route catalog projection 真消费、next-turn preference submit echo、implicit elevation、auto daemon bootstrap 或 bare `dasall` 命令迁移；这些继续分别后置到 `TUI-TODO-026~034`。
3. 本任务完成标准是：新增 focused contract/integration tests 与 discoverability guard 后，stage/tool/pending/budget/recovery/safe-mode projection 能随 poll 刷新，且渲染输出不泄漏 `status_delta`、`tool_summary`、`current_tool` 一类内部字段名或 owner object dump。

## 2. 本地事实与实现约束

1. TUI 详设 7.1、7.4、9.5.8 已明确：status panel 只允许消费受控 projection 和用户面 summary，不得展示 runtime/access 内部对象、secret、raw tool output 或 provider-private dump。
2. `docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` 已冻结 `TuiStatusProjection`、`TuiToolSummaryView`、`TuiEventProjection` 的字段边界；本轮只能验证这些 frozen projection 是否被正确消费，不能扩写新的 owner-private payload。
3. `docs/todos/tui/deliverables/TUI-TODO-017-status-panel-fake展示基线.md` 已冻结 `TuiStatusPanel` 的纯文本 badge、decision summary 与 unknown/degraded fallback；`docs/todos/tui/deliverables/TUI-TODO-023-daemon-data-source-contract基线.md` 与 `docs/todos/tui/deliverables/TUI-TODO-024-启动降级与daemon-unavailable路径.md` 已分别把 daemon-backed producer 与 `TuiApp` startup path 接到位。
4. 当前 gap 不在 owner 代码职责缺失，而在缺少 025 指定的 focused contract/integration evidence。实现必须优先复用已落盘的 daemon data source、reducer、status panel 与 renderer path，而不是为通过测试再发明第二套状态刷新逻辑。
5. 外部对照：Textual Reactivity 指南强调 state change 应驱动 view refresh，并通过 watch/reactive update 自动刷新 UI，而不是把 backing object 直接 dump 到界面。该原则与本任务要求的 `status_delta -> screen_model.status -> status panel` 单向刷新模型一致。

## 3. 落盘结论

### 3.1 projection contract 补齐

1. 已新增 `tests/unit/tui/TuiStatusProjectionContractTest.cpp`，通过 scripted IPC + real `DaemonTuiDataSource` 覆盖 `poll_events()` 正向 roundtrip：`stage`、`current_tool`、`pending_interaction`、`budget_summary`、`recovery_summary`、`health_summary`、`safe_mode_summary` 与 tool summary 的 `tool_name` / `risk_summary` / `observation_summary` / `latency_ms` / `badges` 都会被完整保留。
2. 同一测试还覆盖空 batch 负例，证明 `poll_events()` 在没有新事件时会保持 machine-readable empty result，而不是合成内部 dump 或伪事件。

### 3.2 status panel integration 补齐

1. 已新增 `tests/integration/tui/TuiStatusPanelIntegrationTest.cpp`，通过 real `TuiApp` + `DaemonTuiDataSource` + scripted IPC 驱动 `open_session -> route_catalog -> poll_events -> close_session` 的完整 app 组合路径。
2. integration test 断言 `app.screen_model().status` 在 poll 后保留完整 refreshed projection，并进一步检查渲染 screen 中能看到 stage/tool/pending/budget/recovery/safe-mode 文本更新。
3. 同一测试明确拒绝渲染 `status_delta`、`tool_summary`、`current_tool` 等内部字段名，确保用户面只呈现 panel label 和摘要文本，而不是 raw struct/member dump。

### 3.3 discoverability 与边界收口

1. 已更新 `tests/unit/tui/CMakeLists.txt`、`tests/integration/tui/CMakeLists.txt`、`tests/unit/tui/TuiUnitTopologySmokeTest.cpp` 与 `tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp`，把 `TuiStatusProjectionContractTest` / `TuiStatusPanelIntegrationTest` 注册进 focused discoverability guard。
2. 本轮没有扩写 `apps/tui/src/data/DaemonTuiDataSource.cpp`、`apps/tui/src/model/TuiReducer.cpp` 或 `apps/tui/src/view/TuiStatusPanel.cpp` 的 owner 语义；任务闭合点是“现有 status refresh path 已被 focused contract/integration 证明”，而不是借机改变 projection owner 或追加第二套 UI 状态模型。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| daemon poll projection contract | tests/unit/tui/TuiStatusProjectionContractTest.cpp、tests/unit/tui/CMakeLists.txt | TuiStatusProjectionContractTest | Build_CMakeTools(buildTargets=["dasall_tui_status_projection_contract_unit_test"]) |
| app-level status refresh integration | tests/integration/tui/TuiStatusPanelIntegrationTest.cpp、tests/integration/tui/CMakeLists.txt | TuiStatusPanelIntegrationTest | Build_CMakeTools(buildTargets=["dasall_tui_status_panel_integration_test"]) |
| discoverability guard | tests/unit/tui/TuiUnitTopologySmokeTest.cpp、tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp | TuiUnitTopologySmokeTest、TuiTestTopologyDiscoverability | ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiUnitTopologySmokeTest|TuiTestTopologyDiscoverability)$' |

## 5. 结果

1. `Build_CMakeTools(buildTargets=["dasall_tui_status_projection_contract_unit_test","dasall_tui_status_panel_integration_test"])` 通过；新增 focused unit/integration target 均已成功编译并链接。
2. `ListTests_CMakeTools()` 可发现 `TuiStatusProjectionContractTest` 与 `TuiStatusPanelIntegrationTest`。`RunCtest_CMakeTools(tests=["TuiStatusProjectionContractTest","TuiStatusPanelIntegrationTest"])` 仍命中仓库已知泛化 `生成失败`；已按 repo fallback 口径执行 `ctest --test-dir build/vscode-linux-ninja -N | rg 'TuiStatusProjectionContractTest|TuiStatusPanelIntegrationTest' && ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiStatusProjectionContractTest|TuiStatusPanelIntegrationTest)$'`，discoverability 命中且 2/2 通过。
3. 由于本轮更新了 test registration，又追加执行 `Build_CMakeTools(buildTargets=["dasall_tui_unit_topology_smoke_unit_test","dasall_tui_integration_topology_smoke_integration_test"])`；`RunCtest_CMakeTools(tests=["TuiUnitTopologySmokeTest","TuiTestTopologyDiscoverability"])` 同样命中仓库已知泛化 `生成失败`，已按回退口径执行 `ctest --test-dir build/vscode-linux-ninja -N | rg 'TuiUnitTopologySmokeTest|TuiTestTopologyDiscoverability' && ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiUnitTopologySmokeTest|TuiTestTopologyDiscoverability)$'`，2/2 通过。
4. 本轮结果表明：daemon-backed poll 返回的 status/tool/recovery projection 已能真实刷新到 TUI screen model 与 status panel 文本界面，且不会把内部字段名或 raw projection object dump 暴露给用户面。
5. 当前结果只闭合 `TUI-TODO-025` 的 projection refresh 证据，不代表 foreground session lifecycle、route catalog 真消费、next-turn preference submit echo、CJK/IME/resize manual gate 或 bare `dasall` 命令迁移已完成；这些继续后置到 `TUI-TODO-026~034`。

结论：`TUI-TODO-025` 已闭合。TUI 现在拥有 daemon-backed `poll_events()` 的 focused contract/integration evidence，能证明 status/tool/recovery projection 会沿现有 app/reducer/status panel 路径刷新，并保持用户面 fail-closed、no-internal-dump 的展示边界。
