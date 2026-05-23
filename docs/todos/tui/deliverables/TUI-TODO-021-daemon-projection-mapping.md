# TUI-TODO-021 daemon projection 请求响应映射

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只把已冻结的 daemon-backed TUI projection seam 继续收敛为 `apps/tui/src/ipc/TuiIpcController.h` header 草案与同名 focused unit test：定义 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope`、五个 operation 的 payload 形状、per-operation timeout policy 与 `TuiIpcController` 的方法签名。
2. 本任务不实现 `apps/tui/src/ipc/TuiIpcController.cpp`，不接实际 platform IIPC/UDS transport，不把错误归一化逻辑或 daemon serialization 细节偷渡进 header，也不推进 `DaemonTuiDataSource`、startup failure path、session seam 或命令迁移。
3. 本任务完成标准是：header 不复用 CLI private projection 或 raw daemon carrier；`TuiDaemonProjectionMappingTest` 能以 focused compile/test 方式守住 operation 名称、request payload 映射、response envelope 成功/失败分离、controller 方法面与 no-private-include 边界。

## 2. 本地事实与外部参考

1. TUI 详设 7.4、9.5.4 已明确：daemon-backed TUI 只能经 access/daemon owner 的 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope` 接本地控制面，逻辑 operation 固定为 `open_session`、`submit_turn`、`poll_events`、`route_catalog`、`close_session`；`permission_denied`、`daemon_unavailable`、`timeout`、`schema_mismatch`、`malformed_response` 等失败必须以稳定 reason code 暴露。
2. `docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` 已冻结 owner 边界、DTO 家族与稳定 reason code taxonomy，但仍把“mapping/header 草案”留给 `TUI-TODO-021`。
3. `apps/tui/src/data/ITuiDataSource.h` 已冻结五个 operation 的 request/result supporting object 与 machine-readable `TuiDataSourceIssue`；因此 021 的最小 Build 目标应是把这些 module-local request/result 形状映射为独立的 IPC envelope，而不是重新发明另一套 TUI request DTO。
4. `apps/tui/src/app/TuiApp.cpp` 与 `TUI-TODO-020` deliverable 已给出 fake-only app loop 的稳定调用上下文：`request_id`、`trace_id`、`session_id`、poll cursor、selector mode、close reason 的 caller-visible 语义已经闭合，适合沉淀到 header 草案。
5. Azure CQRS 模式强调 read model 应返回为 presentation layer 优化的 DTO/projection，而不是直接暴露 write/domain model；这支持 TUI 保持 owner-local envelope + module-local DTO 的双层映射，而不是直接复用 CLI JSON envelope 或 raw daemon carrier。
6. Google AIP-193 强调错误 contract 应提供稳定的 machine-readable reason/domain，动态上下文进入 metadata，而不是让客户端解析 message；这支持 `TuiIpcResponseEnvelope` 把 `reason_domain`、`reason_code`、`message`、`metadata` 显式分离。

## 3. 冻结结论

### 3.1 request envelope 映射

1. `TuiIpcRequestEnvelope` 冻结公共字段：`schema_version`、`operation`、`request_id`、`trace_id`、可选 `session_id`、`deadline_ms` 与 `payload`；默认 schema 固定为 `tui_ipc.v1`。
2. `open_session` 只携带 `profile_id` 与 `startup_mode_hint`，不要求现有 `session_id`。
3. `submit_turn` 把 `session_id` 放在 envelope 头部，把 `user_input` 与 `next_preference` 放在 payload 内，保持“路由意图属于 submit payload、不是全局 controller 状态”的边界。
4. `poll_events` 把 `session_id` 放在 envelope 头部，把 `event_cursor` 放在 payload 内，避免把 poll cursor 混成 transport-only 细节。
5. `route_catalog` 允许同时携带可选 `session_id`、`profile_id` 与 `selector_mode`，为 selector 真链路预留 route filtering 所需最小上下文，但不提前冻结更深的 route catalog 字段。
6. `close_session` 只携带 `session_id` 与 `close_reason`；它不宣称 session registry 已 ready，只冻结 caller-visible close reason 的传递位置。

### 3.2 response envelope 映射

1. `TuiIpcResponseEnvelope` 冻结公共字段：`schema_version`、`operation`、`request_id`、`trace_id`、可选 `session_id`、`outcome`、可选 `payload`、可选 `reason_domain`、可选 `reason_code`、可选 `message`、可选 `retryable`、可选 `error_ref` 与 `metadata`。
2. 成功 payload 固定映射为五类：`TuiSessionView`、`TuiTurnReceipt`、`TuiIpcPollEventsBatch`、`TuiRouteCatalogView`、`TuiIpcCloseSessionAck`。其中 `poll_events` 通过 batch object 同时承载 `events` 与 `next_cursor`，`close_session` 通过显式 ack object 表达 close 结果，而不是把 `bool` 裸露成无语义 payload。
3. `outcome=Success` 时必须存在 payload，且不允许再携带 `reason_domain` / `reason_code` / `message` / `retryable` / `error_ref`；`outcome=Failure` 时必须移除 payload，并成对提供 `reason_domain` + `reason_code`。
4. `metadata` 继续允许作为 additive 上下文字段，例如 `socket_path`、`profile_id`、`selector_mode`、`receipt_ref`；但 header 不冻结任何 raw daemon carrier 字段名，也不把 CLI projection 文案当作稳定 key。

### 3.3 controller header 草案

1. `TuiIpcTimeoutPolicy` 在 header 中按 operation 冻结正数 deadline：`open_session`、`poll_events`、`route_catalog`、`close_session` 走短超时，`submit_turn` 允许更长超时；默认 socket path 继续跟随 `/run/dasall/daemon.sock`。
2. `TuiIpcController` 当前只在 header 声明方法面：`open_session()`、`submit_turn()`、`poll_events()`、`query_route_catalog()`、`close_session()`；实现与 transport/error normalization 继续后置到 `TUI-TODO-022`。
3. `apps/tui/src/ipc/TuiIpcController.h` 只依赖 `apps/tui/src/data/ITuiDataSource.h` 与标准库；它不 include access/runtime/llm/profiles private headers，不引用 `DaemonClientResponse`、`UdsRequestFrame`、`UdsResponseFrame`、`AgentRequest` 或 `RuntimeDispatchRequest`。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| IPC envelope/header 草案 | apps/tui/src/ipc/TuiIpcController.h | TuiDaemonProjectionMappingTest | Build_CMakeTools(buildTargets=["dasall_tui_daemon_projection_mapping_unit_test"]) |
| focused compile-style contract test | tests/unit/tui/TuiDaemonProjectionMappingTest.cpp | TuiDaemonProjectionMappingTest | RunCtest_CMakeTools(tests=["TuiDaemonProjectionMappingTest"]) |
| focused test registration | tests/unit/tui/CMakeLists.txt | TuiDaemonProjectionMappingTest | ctest --test-dir build/vscode-linux-ninja -N | rg "TuiDaemonProjectionMappingTest" |
| deliverable consistency | docs/todos/tui/deliverables/TUI-TODO-021-daemon-projection-mapping.md | design consistency | rg -n "open_session|submit_turn|poll_events|route_catalog|close_session|permission_denied|timeout|malformed_response" docs/todos/tui/deliverables/TUI-TODO-021-daemon-projection-mapping.md |

## 5. 结果

1. 已新增 `apps/tui/src/ipc/TuiIpcController.h`，冻结 `TuiIpcOperation`、`TuiIpcOutcome`、`TuiIpcTimeoutPolicy`、`TuiIpcControllerOptions`、五个 request payload、五类 response payload、request builder helpers 与 `TuiIpcController` 的 header-only 方法面。
2. 已新增 `tests/unit/tui/TuiDaemonProjectionMappingTest.cpp`，focused 覆盖 controller 方法签名、五个 operation 的 request payload 映射、response envelope 成功/失败分离、默认 socket path / timeout policy，以及 no-private-include / no-CLI-projection-reuse 边界。
3. 已更新 `tests/unit/tui/CMakeLists.txt`，注册 `dasall_tui_daemon_projection_mapping_unit_test` 与 `TuiDaemonProjectionMappingTest`，并把该 target 纳入 `DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS`。
4. `Build_CMakeTools(buildTargets=["dasall_tui_daemon_projection_mapping_unit_test"])` 通过。
5. `RunCtest_CMakeTools(tests=["TuiDaemonProjectionMappingTest"])` 仍命中仓库已知泛化 `生成失败`；已按 repo fallback 口径执行 `ctest --test-dir build/vscode-linux-ninja -N | rg '^\\s*Test\\s+#.*TuiDaemonProjectionMappingTest$' && ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^TuiDaemonProjectionMappingTest$'`，discoverability 命中且 1/1 通过。
6. 当前结果只闭合 mapping/header 草案，不代表 `TuiIpcController.cpp`、daemon transport、permission-denied normalization、session seam、`DaemonTuiDataSource`、startup failure path 或 route catalog 真消费已完成；这些继续后置到 `TUI-TODO-022~029`。

结论：`TUI-TODO-021` 已闭合，daemon projection 请求响应映射与 header 草案已落盘；下一步转入 `TUI-TODO-022`，实现 `TuiIpcController` 的 transport/serialization/error normalization。