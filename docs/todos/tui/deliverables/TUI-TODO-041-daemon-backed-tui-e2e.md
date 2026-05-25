# TUI-TODO-041 daemon-backed TUI E2E harness

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只建立 build-tree 的真实 daemon-backed TUI E2E：必须在临时 socket 上启动真实 daemon/access 组合，再由 formal TUI data source 完成 `open_session -> route_catalog -> submit_turn -> poll_events -> close_session`。
2. 本任务不把 build-tree true E2E 外推成 installed release-ready，也不处理 formal/prototype purity 或最终 closeout；这些继续留给 `TUI-TODO-042~044`。
3. 本任务必须拒绝 `ScopedIpcOverride`、`ScriptedIpc` 与 fake data source；唯一允许的 client surface 是 `DaemonTuiDataSource` / `TuiIpcController` 走真实本地 socket，唯一允许的 server surface 是 `DaemonIntegrationHarness` 启动的真实 `DaemonBootstrap` + `IAccessGateway` 组合。

## 2. 局部判别与实现决策

1. 仓库中已有 `tests/integration/access/DaemonIntegrationHarness.h`，它能在临时目录启动真实 `DaemonBootstrap`、真实 Unix domain socket 和真实 access gateway；041 不需要重新发明 daemon 子进程/临时 socket harness。
2. `access/src/daemon/TuiIpcProtocolAdapter.cpp` 的 `handle_submit_turn()` 已在 accepted_async 时同步排入 `pending_events`，因此 041 的最短真链路可以直接复用 `DaemonTuiDataSource`：不需要额外补 app loop 或 handler 逻辑，就能在单测里做真实 `submit_turn -> poll_events` roundtrip。
3. 真链路第一次运行暴露了两个 fail-closed 缺口：
   - access gateway 的 `allowed_protocols` 默认只放行 `ipc_uds`，需显式加入 `tui_ipc.v1`
   - accepted_async 真链路必须启用 `AsyncTaskRegistry` 生成 ownership receipt，否则 access gateway 会以 `ownership_secret_unavailable` 拒绝 submit
   041 已在 test harness options 中显式补齐这两个真实约束，而不是在产品代码里绕过它们。

## 3. 落盘结果

1. `tests/integration/tui/TuiDaemonBackedE2ETest.cpp` 新增：
   - `TuiDaemonBackedE2EHarness`
   - `run_formal_tui_roundtrip()`
   - 使用真实 `DaemonIntegrationHarness` + `DaemonTuiDataSource` 完成 open/route/submit/poll/close
   - 对 accepted_async receipt、status/event projection、route catalog projection 与 close ack 的断言
2. `tests/integration/tui/CMakeLists.txt` 已新增：
   - `dasall_tui_daemon_backed_e2e_integration_test`
   - `TuiDaemonBackedE2ETest`
   - 041 所需的 `CliIpcClient.cpp`、`DaemonBootstrap.cpp`、`DaemonLifecycleController.cpp`、`DaemonListenerHost.cpp`、`DaemonSocketPolicy.cpp`、`DaemonSupervisorAdapter.cpp`、`DaemonTuiDataSource.cpp` 与 `TuiIpcController.cpp` 编译拼装
3. `tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp` 已补 discoverability 断言，确保 `TuiDaemonBackedE2ETest` 进入 integration topology 基线。

## 4. 验证

1. `Build_CMakeTools(buildTargets=["dasall_tui_daemon_backed_e2e_integration_test","dasall_tui_integration_topology_smoke_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["TuiDaemonBackedE2ETest","TuiTestTopologyDiscoverability"])`
   - 结果：工具仍返回仓库已知泛化 `生成失败`，未给出具体失败用例。
3. `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_daemon_backed_e2e_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_integration_topology_smoke_integration_test && echo PASS`
   - 结果：通过；输出 `PASS`。

## 5. 结果与剩余边界

1. `TUI-TODO-041` 已完成：build-tree formal TUI 现在有一条真实 daemon-backed E2E 证据，能在临时 socket 上完成 `open_session -> route_catalog -> submit_turn -> poll_events -> close_session`，且不依赖 fake/scripted IPC。
2. `Gate-TUI-12` 现已具备真实 build-tree E2E 证据；此前 038 的 focused server evidence、039 的 socket override seam 与 040 的 scripted/headless submit integration 不再需要替代 041 的职责。
3. 本轮仍未覆盖 installed smoke、formal binary purity 与最终 closeout；`TUI-TODO-042~044` 继续保持未完成，TUI 仍不能宣称 installed release-ready。