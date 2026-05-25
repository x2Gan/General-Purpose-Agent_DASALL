# TUI-TODO-038 daemon/access `tui_ipc.v1` server handler

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只实现 daemon/access 侧 `tui_ipc.v1` server handler：解析 `TuiIpcRequestEnvelope`、分发五个 operation、返回 `TuiIpcResponseEnvelope`，并把 `submit_turn` 接到现有 `IAccessGateway::submit()` 主链。
2. 本任务不实现 formal `dasall` socket override，不接通 composer submit UX，不新增 true daemon-backed E2E、installed smoke 或 binary purity gate；这些继续留给 `TUI-TODO-039~044`。
3. 本任务必须保持 037 已冻结的协议边界：server owner 落在 daemon/access，client 不回退复用既有 CLI daemon frame，旧 CLI control-plane 路径继续保留。

## 2. 局部判别与实现决策

1. 真实控制面在 `apps/daemon/src/DaemonBootstrap.cpp` 的 per-connection dispatch，而不在 `AccessGatewayFactory` 或 `apps/tui` client owner。`AccessGatewayFactory` 继续只负责 unary submit pipeline。
2. `access/` 不应反向依赖 `apps/tui` 的 owner-local 类型，因此 server 侧新增 `access/src/daemon/TuiIpcProtocolAdapter.h/.cpp`，在 access owner 内部镜像最小 DTO、JSON codec、decode error taxonomy 与 response envelope。
3. `open_session`、`route_catalog`、`poll_events`、`close_session` 先由 `TuiIpcSessionStore` 在 daemon/access owner 本地最小承接；`submit_turn` 复用既有 `IAccessGateway::submit()` 主链，只把 TUI request 重新投影成 `InboundPacket`。
4. `apps/daemon/src/DaemonBootstrap.cpp` 新增按 payload 分流：当连接 payload 命中 `tui_ipc.v1` operation envelope 时走 `TuiIpcProtocolAdapter`；否则继续走既有 `DaemonProtocolAdapter`，保证旧 CLI daemon frame 不回退。

## 3. 落盘结果

1. `access/src/daemon/TuiIpcProtocolAdapter.h/.cpp` 新增 daemon/access 私有协议适配层，提供：
   - `payload_looks_like_tui_ipc()`、`decode_tui_ipc_request()`、`dispatch_tui_ipc_operation()`、`encode_tui_ipc_response()`
   - `open_session`、`submit_turn`、`poll_events`、`route_catalog`、`close_session` 五个 operation dispatch
   - `malformed`、`schema_mismatch`、`unknown_operation`、`validation_failed` 等稳定 decode/error taxonomy
   - 本地 `TuiIpcSessionStore`、route catalog 默认投影、submit receipt/event queue 最小承接
2. `access/CMakeLists.txt` 已把 `src/daemon/TuiIpcProtocolAdapter.cpp` 接入 `dasall_access`。
3. `apps/daemon/src/DaemonBootstrap.h/.cpp` 已新增 `effective_profile_id_` 与 `tui_ipc.v1` 分流接线，连接级 peer identity 继续沿用 daemon/access owner 的 local-trusted 判定。
4. `tests/unit/access/daemon/TuiIpcProtocolAdapterTest.cpp` 与 `tests/unit/access/CMakeLists.txt` 已新增 focused test binary：
   - `TuiIpcProtocolAdapterTest` 覆盖 `open_session` happy path 与 `schema_mismatch` / `unknown_operation` / `validation_failed`
   - `DaemonTuiIpcServerHandlerTest` 覆盖 `open_session -> submit_turn -> poll_events -> close_session` 的最小 server dispatch 闭环

## 4. 验证

1. `Build_CMakeTools(buildTargets=["dasall_tui_ipc_protocol_adapter_unit_test","dasall-daemon"])`
   - 结果：通过。
2. `ListTests_CMakeTools()`
   - 结果：执行通过；仓库当前仍存在 CTest 集成工具的泛化生成问题。
3. `RunCtest_CMakeTools(tests=["TuiIpcProtocolAdapterTest","DaemonTuiIpcServerHandlerTest"])`
   - 结果：工具返回仓库已知泛化 `生成失败`，未给出具体失败用例。
4. `./build/vscode-linux-ninja/tests/unit/access/dasall_tui_ipc_protocol_adapter_unit_test && echo PASS`
   - 结果：通过；输出 `PASS`。
5. `./build/vscode-linux-ninja/tests/unit/apps/daemon/dasall-daemon_bootstrap_unit_test && echo PASS`
   - 结果：通过；binary 保持 exit 0，输出 `PASS`。

## 5. 结果与剩余边界

1. `TUI-TODO-038` 已完成：daemon/access 现已存在与 client `TuiIpcController` 对齐的 `tui_ipc.v1` server handler，`BLK-TUI-009` 可以关闭。
2. 本轮只建立了 L2 focused server evidence，不得把它外推为 true daemon-backed E2E 已闭合；`TUI-TODO-039`、`040`、`041` 仍负责 formal socket override、submit UX 与真实 daemon-backed roundtrip。
3. `TUI-TODO-042~044` 仍未开始；installed smoke、binary purity 与 closeout evidence 不因 038 完成而自动闭合。