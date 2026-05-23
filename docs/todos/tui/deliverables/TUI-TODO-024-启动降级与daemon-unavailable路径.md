# TUI-TODO-024 启动降级与 daemon unavailable 路径

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只把 `apps/tui/src/app/TuiApp.h/.cpp` 的启动序列继续落盘到 daemon-backed startup path：让 app 能消费 `TuiTerminalCapabilityProbe` 的 startup mode taxonomy、已冻结的 stable reason code，以及 `DaemonTuiDataSource` 提供的 startup failure result，并补 focused integration test。
2. 本任务不实现 status/tool/recovery 投影刷新、route catalog selector 真消费、foreground session close/query 真链路、隐式提权、自动启动 daemon 或 bare `dasall` 命令迁移；这些仍分别后置到 `TUI-TODO-025~034`。
3. 本任务完成标准是：non-TTY 继续 fail-closed，80x24 capable terminal 明确降级到 `Narrow`，`socket_missing`/`daemon_unavailable`、`permission_denied` 与 `profile_missing` 都有确定的 startup exit/result surface；默认 prototype 仍保持 fake-only，只有显式注入 `data_source_override` 时才走 daemon-backed path。

## 2. 本地事实与实现约束

1. TUI 详设 5.7、9.5.1、9.5.10 已明确：startup probe 属于 terminal-local 责任，app 只能消费 `FullScreen/Narrow/Line/FailClosed` 与 stable startup issue，不得在 startup 期隐式提权或替用户启动系统 daemon。
2. `docs/todos/tui/deliverables/TUI-TODO-001-启动身份与权限模型决策.md` 已冻结：daemon-backed 启动路径沿用 root/sudo-only operator backend，ordinary-user full-function 仍是 future-only，因此 `permission_denied` 必须保留为独立 startup issue，并给出 operator guidance。
3. `docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` 已冻结 stable reason code：`socket_missing`、`permission_denied`、`daemon_unavailable`、`profile_missing` 等都必须保留 machine-readable 边界；其中 `socket_missing` 可以在用户面表达为 daemon unavailable，但不能把 `permission_denied` 混入同一路径。
4. `docs/todos/tui/deliverables/TUI-TODO-018-terminal-capability-probe基线.md` 已闭合 terminal-local startup mode taxonomy；`docs/todos/tui/deliverables/TUI-TODO-023-daemon-data-source-contract基线.md` 已闭合 controller-backed `DaemonTuiDataSource` baseline。本任务只需要把两者接到 `TuiApp` 的 startup path，而不是改写 prototype 默认运行模式。

## 3. 落盘结论

### 3.1 startup path 接线

1. 已更新 `apps/tui/src/app/TuiApp.h`：`TuiAppOptions` 新增 `std::unique_ptr<data::ITuiDataSource> data_source_override`，让 focused integration test 可以显式注入 daemon-backed source，而默认 prototype 继续复用 fake-only source。
2. 已更新 `apps/tui/src/app/TuiApp.cpp`：`initialize_components()` 现优先消费 `data_source_override`，否则回退到 `FakeTuiDataSource(scenario_id_)`；原 `open_fake_session()` 统一提升为 `open_session()`，避免把 startup path 锁死在 fake-only 语义上。
3. `open_session()` 与 `load_route_catalog()` 的失败现在统一经过 `format_startup_issue_message()` 归一化，再由 `emit_startup_error()` 输出到 startup stream。`permission_denied`、`socket_missing`/`daemon_unavailable` 与 `profile_missing` 都有稳定的用户面文案，且 `permission_denied` 保持与 daemon unavailable 分流。

### 3.2 focused integration coverage

1. 已新增 `tests/integration/tui/TuiAppStartupFailureTest.cpp`，覆盖五个 startup 场景：80x24 窄屏降级、non-TTY fail-closed、`socket_missing` 用户面归一为 daemon unavailable、`permission_denied` 保持独立 startup issue，以及 `profile_missing` 在 startup route catalog path 上的 stable fail-closed。
2. 新测试通过真实 `DaemonTuiDataSource` + `TuiIpcControllerTestHooks::ScopedIpcOverride` 驱动 IPC 行为，而不是只用 fake-only stub；这样可直接验证 app 如何消费 023 已落盘的 daemon-backed source。
3. 已更新 `tests/integration/tui/CMakeLists.txt`，新增 `dasall_tui_app_startup_failure_integration_test` target，并把 `apps/tui/src/data/DaemonTuiDataSource.cpp` 与 `apps/tui/src/ipc/TuiIpcController.cpp` 显式编入测试 target，同时更新 `tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp` 守住 discoverability。

### 3.3 边界收口结果

1. `TuiApp` 现在拥有可执行的 startup failure/degradation baseline，但 default app mode 仍然是 fake-only prototype；本任务没有把 main entry 默认切到 daemon-backed mode，也没有引入任何隐式提权或自动 daemon bootstrap 行为。
2. `BLK-TUI-007` 没有被误写为已解除；它仍只继续约束 `TUI-TODO-026` 的真实 foreground session lifecycle integration。024 只闭合 startup path，不宣称 session close/query seam 已 ready。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| startup seam 接线 | apps/tui/src/app/TuiApp.h、apps/tui/src/app/TuiApp.cpp | dasall_tui_app_startup_integration_test | Build_CMakeTools(buildTargets=["dasall_tui_app_startup_integration_test"]) |
| daemon-backed startup failure test | tests/integration/tui/TuiAppStartupFailureTest.cpp、tests/integration/tui/CMakeLists.txt | TuiAppStartupFailureTest | Build_CMakeTools(buildTargets=["dasall_tui_app_startup_failure_integration_test"]) |
| discoverability guard | tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp、tests/integration/tui/CMakeLists.txt | TuiTestTopologyDiscoverability | ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiAppStartupFailureTest|TuiTestTopologyDiscoverability)$' |

## 5. 结果

1. 已把 startup degradation 与 daemon-backed startup failure 真正接入 `TuiApp`：non-TTY 会在 data source 之前 fail-closed，窄屏会稳定降级到 `Narrow`，而 `socket_missing`/`permission_denied`/`profile_missing` 都能以稳定 startup issue 文案退出。
2. 已新增 `TuiAppStartupFailureTest` 并更新 integration CMake/topology smoke；新测试直接消费 `DaemonTuiDataSource`，证明 023 的 attach baseline 已能被 app startup path 复用，而不会把 fake-only prototype 默认模式污染成 daemon-required。
3. `Build_CMakeTools(buildTargets=["dasall_tui_app_startup_failure_integration_test"])` 与 `Build_CMakeTools(buildTargets=["dasall_tui_integration_topology_smoke_integration_test"])` 通过；此前 `Build_CMakeTools(buildTargets=["dasall_tui_app_startup_integration_test"])` 也已通过，说明 `TuiApp` seam 变更没有破坏既有 startup integration target。
4. `ListTests_CMakeTools()` 可发现 `TuiAppStartupFailureTest` 与 `TuiTestTopologyDiscoverability`。`RunCtest_CMakeTools(tests=["TuiAppStartupFailureTest"])` 与 `RunCtest_CMakeTools(tests=["TuiTestTopologyDiscoverability"])` 仍命中仓库已知泛化 `生成失败`；已按 repo fallback 口径执行 `ctest --test-dir build/vscode-linux-ninja -N | rg '^\s*Test\s+#.*(TuiAppStartupFailureTest|TuiTestTopologyDiscoverability)$'` 与 `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiAppStartupFailureTest|TuiTestTopologyDiscoverability)$'`，discoverability 命中且 2/2 通过。
5. 当前结果只闭合 startup failure/degradation baseline，不代表 status/tool/recovery 真消费、route catalog selector consumption、foreground session lifecycle 或 bare `dasall` 命令迁移已完成；这些继续后置到 `TUI-TODO-025~034`。

结论：`TUI-TODO-024` 已闭合。TUI 现在拥有可执行的 startup degradation 与 daemon-backed startup failure baseline，`socket_missing` 可以在用户面稳定表达为 daemon unavailable，而 `permission_denied` / `profile_missing` 仍保持独立 startup issue；默认 prototype 继续保持 fake-only，不隐式提权，也不自动启动系统 daemon。