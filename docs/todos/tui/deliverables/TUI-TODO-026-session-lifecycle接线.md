# TUI-TODO-026 session lifecycle 接线

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只闭合 `/exit` 与 `/clear` 在现有五个 IPC operation seam 上的前台 session lifecycle 消费证据：`/exit` 必须发出显式 `/exit` close reason；`/clear` 必须清空当前前台 transcript 与本地状态、保留 input history、清空当前 composer draft，并切到新的前台 session 语义。
2. 本任务不新增 standalone session query IPC、route catalog projection 字段扩写、next-turn preference submit echo、CJK/IME/resize 人工 gate、Debian/package smoke 迁移或 bare `dasall` 命令释放；这些继续分别后置到 `TUI-TODO-027~035`。
3. 本任务完成标准是：新增 focused reducer/integration/discoverability evidence 后，`/clear` 能在 close failure 可观测的前提下完成 local reset 与新 session rebind，`/exit` 能向 owner 发出稳定的 `/exit` close reason，而不是停留在 deferred banner 或伪成功 close。

## 2. 本地事实与实现约束

1. `docs/todos/tui/deliverables/TUI-TODO-002-clear语义决策.md` 已冻结 `/clear` 的产品语义：它不是单纯清屏，也不是 `/exit` 别名；必须清空当前 transcript / 本地状态并切到新的前台 session 语义，同时保留 input history。
2. `docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md`、`TUI-TODO-021`、`TUI-TODO-022` 与 `TUI-TODO-023` 已冻结并落盘 `open_session`、`route_catalog`、`close_session` 的 owner-local seam、稳定 reason code 与 thin adapter。026 必须复用既有 seam，不能为通过测试再发明第二套 session owner surface。
3. `TUI-TODO-020`、`TUI-TODO-024` 与 `TUI-TODO-025` 已把 `TuiApp`、daemon-backed startup path 与 status projection refresh 接到可执行基线。本轮真正缺口不在 parser 或 data source 缺 header，而在 app-loop 没把 slash action 翻译成真实 lifecycle 行为。
4. `/clear` 需要保留 input history，因此实现不能通过重建 `TuiComposer` 来“顺便清空草稿”；必须沿 `TuiComposer` 既有状态机接口清空 draft、恢复 ready 模式，避免把 history 一并抹掉。
5. 本轮新增的 `TuiAppOptions::scripted_actions` 仅用于 focused integration replay，不构成新的用户面命令入口，也不改变 real interactive path 的 owner 边界。

## 3. 落盘结论

### 3.1 reducer 与 app-loop 接线补齐

1. 已在 `apps/tui/src/model/TuiAction.h` 新增内部 action `ForegroundSessionResetApplied`，并在 `apps/tui/src/model/TuiReducer.cpp` 落盘对应状态迁移：清空 session、transcript、status、route、banner、modal 与 composer draft，恢复 composer ready / can_submit，并保留 `debug_reason`。
2. `apps/tui/src/app/TuiApp.h/.cpp` 现已把 `/clear` 从 deferred banner 改为真实 lifecycle：先以 `/clear` close reason 对旧前台 session 发起 best-effort `close_session()`，随后执行 local reset、清空 composer draft 但保留 history，再走 `open_session() -> route_catalog()` 重新绑定新的 foreground session。
3. 若旧 session close 失败，`TuiApp` 不会伪装成成功 close；失败会以 warning banner 保留在新 screen model 上，保持 `close_unavailable` 等 owner reason code 可观测。
4. `ExitRequested` 现会把 shutdown close reason 显式置为 `/exit`；最终 `shutdown()` 不再写死 `exit_requested` 占位串，而是向 data source 发出真实的 caller-visible close reason。

### 3.2 focused integration 与 discoverability 补齐

1. 已新增 `tests/integration/tui/TuiSessionLifecycleIntegrationTest.cpp`，通过 real `TuiApp` + `DaemonTuiDataSource` + scripted IPC 覆盖两条主路径：
   - `/clear` 在旧 session `close_unavailable` 时仍会完成 local reset、新 session rebind 与 route refresh，同时把 close failure 继续展示给用户面。
   - `/exit` 会向 owner 发出显式 `/exit` close reason，并在 owner ack 后干净退出。
2. 同一 integration test 通过 `TuiIpcControllerTestHooks::decode_request_envelope_for_test()` 直接断言发出的 close payload，避免只看 screen 文本而漏掉真实 IPC close reason。
3. 已更新 `tests/integration/tui/CMakeLists.txt` 与 `tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp`，把 `TuiSessionLifecycleIntegrationTest` 注册进 focused discoverability guard。

### 3.3 blocker 收口与边界说明

1. `BLK-TUI-007` 对 026 的阻塞点已收口：026 现在直接消费现有五个 operation seam 完成前台 session close/rebind，不再需要额外的 runtime/access unblock 原子任务。
2. 本轮没有新增 query IPC，也没有把 `/session` modal 扩写成新的 owner API；session summary 仍来自现有 screen model / route projection，不外推为 standalone query-ready。
3. 本轮也没有宣称 route catalog 真消费、submit echo、命令迁移、Debian/install gate 或 release-ready。026 的闭合只证明 slash action 已接上 foreground session lifecycle，不意味着 `TUI-TODO-027~035` 已自动满足。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| foreground reset reducer | apps/tui/src/model/TuiAction.h、apps/tui/src/model/TuiReducer.cpp、tests/unit/tui/TuiReducerTransitionTest.cpp | TuiReducerTransitionTest | Build_CMakeTools(buildTargets=["dasall_tui_reducer_unit_test"]) |
| app-level session lifecycle | apps/tui/src/app/TuiApp.h、apps/tui/src/app/TuiApp.cpp、tests/integration/tui/TuiSessionLifecycleIntegrationTest.cpp | TuiSessionLifecycleIntegrationTest | Build_CMakeTools(buildTargets=["dasall_tui_app_startup_integration_test","dasall_tui_session_lifecycle_integration_test"]) |
| discoverability guard | tests/integration/tui/CMakeLists.txt、tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp | TuiTestTopologyDiscoverability | Build_CMakeTools(buildTargets=["dasall_tui_integration_topology_smoke_integration_test"]) |

## 5. 结果

1. `Build_CMakeTools(buildTargets=["dasall_tui_reducer_unit_test"])` 通过；新增 `ForegroundSessionResetApplied` 的 reducer unit target 成功编译并链接。
2. `./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_reducer_unit_test && echo PASS` 通过；foreground session reset state transition 没有被后续 app-loop 改动破坏。
3. `Build_CMakeTools(buildTargets=["dasall_tui_app_startup_integration_test"])` 通过；`TuiApp` lifecycle 改动没有破坏既有 startup integration target。
4. `Build_CMakeTools(buildTargets=["dasall_tui_session_lifecycle_integration_test","dasall_tui_integration_topology_smoke_integration_test"])` 通过；新增 lifecycle / discoverability integration targets 成功编译并链接。
5. `ListTests_CMakeTools()` 可发现 `TuiSessionLifecycleIntegrationTest` 与 `TuiTestTopologyDiscoverability`。`RunCtest_CMakeTools(tests=["TuiSessionLifecycleIntegrationTest","TuiTestTopologyDiscoverability"])` 仍命中仓库已知泛化 `生成失败`；已按 repo 当前可执行回退口径直接执行 `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_session_lifecycle_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_integration_topology_smoke_integration_test && echo PASS`，通过。
6. 当前结果表明：`/clear` 与 `/exit` 已不再停留于 parser/local placeholder。TUI 现在能在现有 daemon-backed session seam 上完成 explicit close reason、best-effort close failure 可观测、local reset 与新 foreground session rebind。
7. 当前结果只闭合 `TUI-TODO-026` 的 foreground session lifecycle 接线，不代表 route catalog 真消费、next preference submit echo、terminal manual gate 或 bare `dasall` 命令迁移已完成；这些继续后置到 `TUI-TODO-027~035`。

结论：`TUI-TODO-026` 已闭合。TUI 现在拥有 `/exit` 与 `/clear` 的 focused lifecycle evidence，能证明 slash action 已接到真实 foreground session close/rebind 路径，并保持 fail-closed、可观测 close failure 与 input-history-preserving 的产品边界。