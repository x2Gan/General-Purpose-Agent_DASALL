# TUI-TODO-043 formal binary purity

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只解决 formal/prototype binary purity：正式 installed/build-tree `dasall` 不能再把 `FakeTuiDataSource` 或 fake scenario 符号带进 release-facing binary。
2. 本任务不重做 042 的 installed smoke，也不把 purity gate 外推成最终 release-ready；最终口径仍保留给 `TUI-TODO-044`。
3. 本任务允许 prototype target 继续保留 fake/demo source，但 fake 注入必须只存在于 prototype 侧，不能继续藏在 shared `TuiApp` core 中。

## 2. 局部判别与实现决策

1. 043 的局部判别从现有 formal binary 开始：`nm -C build/vscode-linux-ninja/apps/tui/dasall | rg 'FakeTuiDataSource|FakeScenario'` 仍直接命中 fake 符号，证明 formal `dasall` 继续链接了 prototype-only implementation。
2. 邻近控制路径在 `apps/tui/CMakeLists.txt` 与 `apps/tui/src/app/TuiApp.cpp`：
   - `dasall-tui` 与 `dasall_tui_prototype` 同时链接 `dasall_tui_prototype_core`
   - `TuiApp::initialize_components()` 在没有 override 时会隐式 `std::make_unique<data::FakeTuiDataSource>(scenario_id_)`
3. 因此根修复不是只补一个 symbol audit，而是把 fake 默认注入从 shared core 挪走：
   - 新增 fake-free `dasall_tui_core`，只保留 shared app/model/view/terminal 代码
   - `dasall_tui_prototype_core` 缩成 fake-only extension，并显式依赖 `dasall_tui_core`
   - prototype `main.cpp` 与 fake-based integration tests 显式注入 `FakeTuiDataSource`
   - formal `dasall` 继续显式注入 `DaemonTuiDataSource`

## 3. 落盘结果

1. `apps/tui/CMakeLists.txt` 已拆出 `dasall_tui_core` 与 `dasall_tui_prototype_core`：formal `dasall` 现只链接 fake-free core，prototype target 继续保留 fake-only extension。
2. `apps/tui/src/app/TuiApp.cpp` 不再在 shared core 内隐式创建 `FakeTuiDataSource`；没有显式 `data_source_override` 时会直接 fail-closed，而不是偷偷回落到 fake path。
3. `apps/tui/src/main.cpp` 的 prototype 分支已显式注入 `FakeTuiDataSource`，formal 分支继续显式注入 `DaemonTuiDataSource`；`tests/integration/tui/TuiAppStartupTest.cpp`、`TuiAppStartupFailureTest.cpp`、`TuiPrototypeSmokeTest.cpp` 也已同步改为显式 fake 注入。
4. 新增 `tests/integration/tui/DasallTuiEntrypointPurityTest.cpp`，对 formal/prototype 两个 binary 做 `nm -C` 符号审计，并验证 CMake 接线已经切到分离的 core。

## 4. 验证

1. 初始局部判别
   - 命令：`nm -C build/vscode-linux-ninja/apps/tui/dasall | rg 'FakeTuiDataSource|FakeScenario'`
   - 结果：命中 fake 符号，证实 043 问题真实存在且位于 formal binary purity 边界。
2. `Build_CMakeTools(buildTargets=["dasall-tui"])`
   - 结果：通过；证明 fake-free shared core 不会打断 formal entrypoint 构建。
3. `Build_CMakeTools(buildTargets=["dasall-tui","dasall_tui_entrypoint_purity_integration_test"])`
   - 结果：通过；同时拉起 `dasall_tui_prototype` 与 purity test target。
4. `RunCtest_CMakeTools(tests=["DasallTuiEntrypointPurityTest"])`
   - 结果：命中仓库已知泛化 `生成失败`，未返回可采信断言结果。
5. `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_entrypoint_purity_integration_test`
   - 结果：通过；无输出退出，证明 formal binary 不再携带 `FakeTuiDataSource` / `FakeScenario` 符号，同时 prototype binary 仍保留 fake path。

## 5. 结果与剩余边界

1. 043 已闭合：formal `apps/tui/dasall` 不再把 prototype-only fake source 混入 release-facing binary，release binary purity gate 已具备 focused evidence。
2. prototype target 仍保持可构建、可执行、可承载 fake/demo scenario，证明这次拆分没有把 prototype owner path 一并删掉。
3. TUI 仍不能宣称 installed release-ready：`TUI-TODO-044` 仍需把 037~043 的 focused/scripted IPC、true daemon-backed E2E、installed smoke 与 binary purity 证据收口成最终 closeout 口径。