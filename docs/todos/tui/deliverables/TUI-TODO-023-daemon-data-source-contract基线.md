# TUI-TODO-023 DaemonTuiDataSource contract 基线

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只把 `apps/tui/src/data/ITuiDataSource.h` 已冻结的五个 operation 继续落盘为 `apps/tui/src/data/DaemonTuiDataSource.h/.cpp`：正式实现通过 `TuiIpcController` 暴露 `open_session()`、`submit_turn()`、`poll_events()`、`route_catalog()`、`close_session()` 的统一 contract，并补 focused contract test。
2. 本任务不实现 startup failure path、status/tool/recovery 真消费、`/exit` / `/clear` 的真实 foreground session lifecycle integration 或 bare `dasall` 命令迁移；这些仍分别后置到 `TUI-TODO-024~034`。
3. 本任务完成标准是：`DaemonTuiDataSource` 不直连 raw daemon carrier、CLI projection、`platform::IIPC` 或 runtime/access private surface；当 session owner 仍返回 `close_unavailable` 等 machine-readable failure 时，data source 保持 fail-closed 透传，而不是越权伪造“session 已成功 close/open”的事实。

## 2. 本地事实与实现约束

1. TUI 详设 9.5.3 已明确：`ITuiDataSource` 是正式 TUI 的关键 seam，prototype 用 `FakeTuiDataSource`，正式实现用 `DaemonTuiDataSource`，上层组件不感知 fake / daemon 差异。
2. TUI 详设 9.5.4 与 `docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` 已冻结：daemon-backed TUI v1 只能通过 `TuiIpcController` 消费 access/daemon owner 的 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope`；`DaemonTuiDataSource` 不能复用 CLI `DaemonClientResponse` 或 raw `UdsResponseFrame`。
3. `docs/todos/tui/deliverables/TUI-TODO-022-tui-ipc-controller-error-normalization.md` 已证明 `TuiIpcController` 具备可执行的 transport、serialization 与稳定 reason code baseline；`socket_missing`、`permission_denied`、`timeout`、`schema_mismatch`、`malformed_response` 与 owner-supplied failure envelope 已能归一化到 `TuiDataSourceIssue`。
4. `BLK-TUI-007` 当前仍然成立，但它约束的是 foreground session close/query 的真实 owner seam，而不是 023 的 attach baseline。`docs/todos/tui/deliverables/TUI-TODO-002-clear语义决策.md` 已明确：在该 blocker 未解前，`/exit` / `/clear` 只允许保留 `close_unavailable` 之类可观测失败，不允许假装后端 lifecycle 已 ready。

## 3. 落盘结论

### 3.1 data source 实现

1. 已新增 `apps/tui/src/data/DaemonTuiDataSource.h` 与 `apps/tui/src/data/DaemonTuiDataSource.cpp`，把五个 `ITuiDataSource` operation 统一实现为 `TuiIpcController` 的薄转发。
2. `route_catalog()` 继续复用 `TuiIpcController::query_route_catalog()`，不在 data source 层引入第二套 route 查询语义。
3. `DaemonTuiDataSource` 只依赖 `ITuiDataSource` 与 `TuiIpcController`，不包含 raw IPC provider、daemon carrier、CLI projection、runtime dispatch object 或 renderer 依赖。

### 3.2 focused contract tests

1. 已新增 `tests/unit/tui/DaemonTuiDataSourceContractTest.cpp`，通过 `TuiIpcControllerTestHooks` 注入 scripted IPC，覆盖五个 operation 的 success roundtrip、request envelope 保真，以及 `socket_missing` / `close_unavailable` 的 fail-closed 透传。
2. 新测试同时检查边界约束：`DaemonTuiDataSource` 头/实现不包含 access/runtime/apps private header，不直连 `IIPC`、`UnixIpcProvider`、`AgentRequest`、`RuntimeDispatchRequest` 或 CLI/raw daemon carrier。
3. 已更新 `tests/unit/tui/CMakeLists.txt`，注册 `dasall_tui_daemon_data_source_contract_unit_test` 与 `DaemonTuiDataSourceContractTest`，并纳入 `DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS`。

### 3.3 blocker 收缩结论

1. `TUI-TODO-023` 已证明：formal daemon-backed data source attach 可以在 `BLK-TUI-007` 未完全解除前先行落盘，只要实现严格停在 `TuiIpcController` + stable issue contract 边界内。
2. `BLK-TUI-007` 没有被误写为已关闭；它现在只继续约束 `TUI-TODO-026` 的真实 foreground session lifecycle integration，以及随后需要 owner-safe close/query 语义的交互路径。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| data source 实现 | apps/tui/src/data/DaemonTuiDataSource.h、apps/tui/src/data/DaemonTuiDataSource.cpp | DaemonTuiDataSourceContractTest | Build_CMakeTools(buildTargets=["dasall_tui_daemon_data_source_contract_unit_test"]) |
| contract test 注册 | tests/unit/tui/DaemonTuiDataSourceContractTest.cpp、tests/unit/tui/CMakeLists.txt | ListTests_CMakeTools() | ListTests_CMakeTools() |
| fallback ctest 证据 | build/vscode-linux-ninja | DaemonTuiDataSourceContractTest | ctest --test-dir build/vscode-linux-ninja -N && ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^DaemonTuiDataSourceContractTest$' |

## 5. 结果

1. 已新增 `apps/tui/src/data/DaemonTuiDataSource.h/.cpp`，把 daemon-backed TUI data source attach 收敛为基于 `TuiIpcController` 的单一薄适配层。
2. 已新增 `tests/unit/tui/DaemonTuiDataSourceContractTest.cpp` 并更新 `tests/unit/tui/CMakeLists.txt`，focused 守住五个 operation 的统一 contract、transport failure 透传，以及 `close_unavailable` 在 session seam 未 ready 时的 fail-closed 语义。
3. `Build_CMakeTools(buildTargets=["dasall_tui_daemon_data_source_contract_unit_test"])` 通过。
4. `ListTests_CMakeTools()` 可发现 `DaemonTuiDataSourceContractTest`。
5. `RunCtest_CMakeTools(tests=["DaemonTuiDataSourceContractTest"])` 仍命中仓库已知泛化 `生成失败`；已按 repo fallback 口径执行 `ctest --test-dir build/vscode-linux-ninja -N | rg '^\s*Test\s+#.*DaemonTuiDataSourceContractTest$' && ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^DaemonTuiDataSourceContractTest$'`，discoverability 命中且 1/1 通过。
6. 当前结果只闭合 formal daemon data source attach baseline，不代表 startup failure path、status/recovery 真消费、route catalog 真消费、foreground session lifecycle 或命令迁移已完成；这些继续后置到 `TUI-TODO-024~034`。

结论：`TUI-TODO-023` 已闭合。TUI 现在拥有不泄漏 raw carrier / CLI projection 的 `DaemonTuiDataSource` baseline，可为 `TUI-TODO-024` / `025` 直接复用，同时把 `BLK-TUI-007` 收缩到真实 session lifecycle integration，而不是继续阻断 formal data source attach。