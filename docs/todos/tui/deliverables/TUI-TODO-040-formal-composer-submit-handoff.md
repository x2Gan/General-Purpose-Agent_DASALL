# TUI-TODO-040 formal composer submit handoff

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只把 formal `TuiApp` 的 composer submit 从本地状态机闭环到 `ITuiDataSource::submit_turn()`，确保 app loop 能组装真实 `session_id`、`request_id`、`trace_id`、`user_input` 与 `next_preference`。
2. 本任务不宣称 true daemon-backed E2E 已完成，也不新增 installed smoke、formal/prototype purity gate 或 release-ready 结论；这些继续留给 `TUI-TODO-041~044`。
3. 本任务必须保持既有 owner 边界：`TuiComposer` 继续只负责本地输入状态机，`TuiApp` 负责 request orchestration 与 screen projection，不能把 submit owner 上抬到 runtime，也不能把 fake/scripted evidence外推为 true E2E。

## 2. 局部判别与实现决策

1. `apps/tui/src/view/TuiComposer.cpp` 已经在 `Enter` 时产出 `SubmitRequested`、记录 history、清空 draft 并切到 `submitting`，真正缺口不在 composer，而在 `apps/tui/src/app/TuiApp.cpp` 没有任何 submit request sink。
2. 为了保持 request action 和 reducer contract 一致，新增 `TuiActionType::TurnSubmitRequested`，并在 `TuiApp` 内新增 `dispatch_composer_submit()`；该方法复用 composer 的本地 `Enter` 语义，再由 app 组装 `TuiSubmitTurnRequest` 并调用 data source。
3. submit 成功路径使用 receipt 生成 `turn_receipt` transcript projection 与 info banner；submit 失败或 `validation_failed` 时恢复原 draft 并投影 error banner，避免用户输入在失败时静默丢失。

## 3. 落盘结果

1. `apps/tui/src/model/TuiAction.h` 与 `apps/tui/src/model/TuiReducer.cpp` 已新增 `TuiActionType::TurnSubmitRequested`，并把它纳入 request action no-op reducer contract。
2. `apps/tui/src/app/TuiApp.h/.cpp` 已新增：
   - `dispatch_composer_submit()`
   - `restore_composer_draft(...)`
   - app-local receipt event projection helper
   - submit unavailable / validation rejected / generic submit failure / submit success 的用户可见 banner 与 transcript 回显
3. `tests/integration/tui/TuiAppSubmitTurnIntegrationTest.cpp` 与 `tests/integration/tui/CMakeLists.txt` 已新增：
   - `dasall_tui_submit_turn_integration_test`
   - `TuiAppSubmitTurnIntegrationTest`
   - success path 对 request 组装、receipt transcript、info banner 的验证
   - validation rejected path 对 draft 恢复、banner reason code 与 `last_error()` 的验证
4. `tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp` 已补 discoverability 断言，确保新 test 名进入 integration topology 基线。

## 4. 验证

1. `Build_CMakeTools(buildTargets=["dasall_tui_submit_turn_integration_test","dasall_tui_integration_topology_smoke_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["TuiAppSubmitTurnIntegrationTest","TuiTestTopologyDiscoverability"])`
   - 结果：工具仍返回仓库已知泛化 `生成失败`，未给出具体失败用例。
3. `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_submit_turn_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_integration_topology_smoke_integration_test && echo PASS`
   - 结果：通过；输出 `PASS`。

## 5. 结果与剩余边界

1. `TUI-TODO-040` 已完成：formal `TuiApp` 现可把真实 composer draft、foreground session id、request/trace id 与 next-turn preference 组装成 `submit_turn()` request，并把 success / failure / `validation_failed` 投影回 screen model。
2. `BLK-TUI-010` 已关闭：submit UX 缺口已由 039 的 socket seam 与 040 的 app handoff 联合收口，下一原子任务转为 `TUI-TODO-041` 的真实 daemon-backed E2E harness。
3. 本轮仍然只建立 scripted/headless app integration 证据，不代表 true daemon-backed ready、installed release-ready 或 formal binary purity 已完成；`TUI-TODO-041~044` 继续保持未完成。