# TUI-TODO-039 formal TUI socket override 与 headless test seam

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只为 formal `dasall` 提供受控 daemon socket override：允许测试环境通过环境变量改写 socket path，同时保持生产默认仍为 `/run/dasall/daemon.sock`。
2. 本任务不接通 composer submit UX，不新增真实 daemon-backed E2E，也不改 installed smoke 或 binary purity gate；这些继续留给 `TUI-TODO-040~044`。
3. 本任务必须保持 037/038 已冻结的 owner 边界：socket override 仅是 formal entrypoint 的 headless seam，不新增隐式 daemon 启动，不把 fake-only prototype 路径混入 formal TUI。

## 2. 局部判别与实现决策

1. `apps/tui/src/ipc/TuiIpcController.h` 已有 `TuiIpcControllerOptions::socket_path` 和默认值 `kTuiDefaultDaemonSocketPath = "/run/dasall/daemon.sock"`；缺口不在 controller，而在 formal `main.cpp` 没有把外部 override 传进来。
2. 为了避免把环境变量策略散到 test 或 main 层，新增 `resolve_daemon_tui_controller_options_from_environment()` 到 `apps/tui/src/data/DaemonTuiDataSource.h/.cpp`，由 daemon-backed data source owner 解析 `DASALL_TUI_DAEMON_SOCKET`。
3. `apps/tui/src/main.cpp` 的 formal entrypoint 现在通过该 helper 构造 `DaemonTuiDataSource`；未设置或设置为空字符串时，仍 fail-closed 回退到生产默认 socket path。

## 3. 落盘结果

1. `apps/tui/src/data/DaemonTuiDataSource.h/.cpp` 新增：
   - `kTuiDaemonSocketOverrideEnv = "DASALL_TUI_DAEMON_SOCKET"`
   - `resolve_daemon_tui_controller_options_from_environment()`
2. `apps/tui/src/main.cpp` 的 formal entrypoint 已改为通过上述 helper 注入 `DaemonTuiDataSource`，不再硬编码默认构造的 daemon-backed data source。
3. `tests/integration/tui/DasallTuiSocketOverrideTest.cpp` 与 `tests/integration/tui/CMakeLists.txt` 已新增：
   - `dasall_tui_socket_override_integration_test`
   - `DasallTuiSocketOverrideTest`
   - 对默认 socket path、环境覆盖、空值回退、main wiring 与 integration CMake 注册的 headless 验证
4. `tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp` 已补 discoverability 断言，确保新 test 名进入 integration topology 基线。

## 4. 验证

1. `Build_CMakeTools(buildTargets=["dasall-tui","dasall_tui_socket_override_integration_test","dasall_tui_integration_topology_smoke_integration_test"])`
   - 结果：通过。
2. `ListTests_CMakeTools()`
   - 结果：通过；输出包含 `DasallTuiSocketOverrideTest`。
3. `RunCtest_CMakeTools(tests=["DasallTuiSocketOverrideTest","TuiTestTopologyDiscoverability"])`
   - 结果：工具仍返回仓库已知泛化 `生成失败`，未给出具体失败用例。
4. `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_socket_override_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_integration_topology_smoke_integration_test && echo PASS`
   - 结果：通过；输出 `PASS`。

## 5. 结果与剩余边界

1. `TUI-TODO-039` 已完成：formal `dasall` 现可在测试环境通过 `DASALL_TUI_DAEMON_SOCKET` 指向临时 socket，且默认 production socket path 不变。
2. 这只建立了 formal entrypoint 的 headless socket seam，不代表用户 submit 闭环或真实 daemon-backed E2E 已完成；`BLK-TUI-010` 继续保持 Open，后续由 `TUI-TODO-040` 和 `TUI-TODO-041` 处理。
3. 本轮没有改变 operator model，也没有引入隐式 daemon 启动；prototype fake-only path 继续与 formal daemon-backed path 分离。