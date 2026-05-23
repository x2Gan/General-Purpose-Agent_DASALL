# TUI-TODO-022 TuiIpcController 错误归一化

状态：Done
日期：2026-05-23
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只把 `apps/tui/src/ipc/TuiIpcController.h` 已冻结的方法面继续落盘为 `apps/tui/src/ipc/TuiIpcController.cpp`：实现本地 IPC 请求、per-operation timeout、TUI envelope serialization/response decode 与稳定错误归一化，并补 focused unit tests。
2. 本任务不实现 `DaemonTuiDataSource`、startup failure path、session owner seam、route catalog 真消费或 bare `dasall` 命令迁移；这些仍分别后置到 `TUI-TODO-023~030`。
3. 本任务完成标准是：`TuiIpcController` 不修改 public header 草案，不把 CLI projection 或 raw daemon carrier 泄漏回 TUI header；`socket_missing`、`permission_denied`、`timeout`、`schema_mismatch`、`malformed_response` 在 focused tests 中以 machine-readable reason code 暴露，而不是依赖 message 猜测。

## 2. 本地事实与外部参考

1. TUI 详设 9.5.4 已明确：`TuiIpcController` 负责封装 TUI -> daemon 的本地 IPC 请求、超时、序列化与错误归一化，方法面固定为 `open_session()`、`submit_turn()`、`poll_events()`、`query_route_catalog()`、`close_session()`。
2. `docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` 已冻结稳定 reason code 矩阵：`socket_missing`、`permission_denied`、`timeout`、`peer_closed`、`schema_mismatch`、`malformed_response` 等必须 machine-readable，并允许 `metadata` 只做加法扩展。
3. `apps/cli/src/CliIpcClient.cpp` 已证明本仓库现有 local control-plane transport 的最小执行流是 `connect -> send -> receive -> close`，但 CLI 只返回自由文本 `failure_reason`；TUI 需要在不复用 CLI private projection 的前提下把同类 transport 故障映射为稳定 `TuiDataSourceIssue`。
4. `platform/include/PlatformError.h` 已冻结 `PlatformErrorCode::NotFound`、`PermissionDenied`、`Timeout`、`ConnectionRefused`、`PeerClosed`、`PayloadTooLarge` 等平台错误枚举；这些错误正是 022 的最小控制面输入。
5. Google AIP-193 与 `TUI-TODO-011` deliverable 都要求客户端分支逻辑绑定 `reason_domain` + `reason_code`，动态上下文放进 `metadata`，message 只做展示；这与 022 的错误归一化目标一致。

## 3. 落盘结论

### 3.1 transport / serialization 实现

1. 已新增 `apps/tui/src/ipc/TuiIpcController.cpp`，实现 `TuiIpcController` 的 JSON request/response envelope codec、per-operation deadline 选择、`platform::IIPC` roundtrip 与 TUI DTO/result 映射。
2. 默认 transport provider 使用 `platform::linux::UnixIpcProvider`，但仅在 `.cpp` 内部解析；public `TuiIpcController.h` 继续只暴露 TUI-local envelope、timeout policy 与方法面，不引入 platform/access private include。
3. 已新增 `apps/tui/src/ipc/TuiIpcControllerTestHooks.h` 作为 private test seam：允许 tests 注入 fake `IIPC` 并重用同一套 response encoder / request decoder；该 seam 不进入 public ABI，也不改变 `TuiIpcController.h` 的冻结边界。
4. controller 在进入 wire 之前补了最小 local validation：缺失 `request_id` / `trace_id` / `session_id` / `user_input` / `close_reason` 等必要字段时，直接返回 `request/validation_failed`，避免把明显的本地无效请求伪装成 daemon 拒绝。

### 3.2 稳定错误归一化

1. `connect()` 命中 `PlatformErrorCode::NotFound` 时，统一映射为 `transport/socket_missing`，并保留 `socket_path`、`operation`、`request_id`、`trace_id` 与 `syscall` / `errno` metadata。
2. `PermissionDenied` 统一映射为 `transport/permission_denied`，保持与 `socket_missing` 分离，避免启动路径把权限问题误判为 daemon 未启动。
3. `Timeout` 统一映射为 `transport/timeout`，并把 `retryable=true` 暴露给上层后续 startup recovery / retry 策略。
4. 响应 envelope `schema_version` 与 `tui_ipc.v1` 不一致时，统一映射为 `protocol/schema_mismatch`，并显式保留 `expected_schema_version` / `actual_schema_version` metadata。
5. 响应缺字段、payload 变体与 operation 不匹配、request/trace correlation 不一致、空响应或不可解析时，统一映射为 `protocol/malformed_response`，避免把 parse failure 伪装成业务拒绝。
6. daemon 显式返回 failure envelope 时，controller 直接保留 `reason_domain`、`reason_code`、`error_ref`、`metadata`，不解析自由文本 message 来猜语义；因此 `permission_denied` 等 owner-supplied 稳定错误能直通到 `TuiDataSourceIssue`。
7. 除题面要求的五个稳定 code 外，本轮也补齐了 `transport/peer_closed`、`daemon/daemon_unavailable` 与 `request/validation_failed` 的 additive path，供后续 `TUI-TODO-023~024` startup/session seam 复用。

### 3.3 focused tests 与注册

1. 已新增 `tests/unit/tui/TuiIpcControllerTest.cpp`：覆盖 `submit_turn` success roundtrip、request envelope 保真、`socket_missing`、`timeout`、`schema_mismatch` 与 `malformed_response` 的分离。
2. 已新增 `tests/unit/tui/TuiIpcPermissionDeniedTest.cpp`：覆盖 connect 阶段 `permission_denied` 归一化，以及 daemon failure envelope 中 `permission_denied` 的 verbatim preserve。
3. 已更新 `tests/unit/tui/CMakeLists.txt`，注册 `dasall_tui_ipc_controller_unit_test`、`dasall_tui_ipc_permission_denied_unit_test`、`TuiIpcControllerTest` 与 `TuiIpcPermissionDeniedTest`，并把两个新 target 纳入 `DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS`。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| controller 实现 | apps/tui/src/ipc/TuiIpcController.cpp | TuiIpcControllerTest、TuiIpcPermissionDeniedTest | Build_CMakeTools(buildTargets=["dasall_tui_ipc_controller_unit_test","dasall_tui_ipc_permission_denied_unit_test"]) |
| private test seam | apps/tui/src/ipc/TuiIpcControllerTestHooks.h | TuiIpcControllerTest、TuiIpcPermissionDeniedTest | ListTests_CMakeTools() |
| focused unit tests | tests/unit/tui/TuiIpcControllerTest.cpp、tests/unit/tui/TuiIpcPermissionDeniedTest.cpp | TuiIpcControllerTest、TuiIpcPermissionDeniedTest | RunCtest_CMakeTools(tests=["TuiIpcControllerTest","TuiIpcPermissionDeniedTest"]) |
| fallback ctest 证据 | build/vscode-linux-ninja | TuiIpcControllerTest、TuiIpcPermissionDeniedTest | ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiIpcControllerTest|TuiIpcPermissionDeniedTest)$' |

## 5. 结果

1. 已新增 `apps/tui/src/ipc/TuiIpcController.cpp` 与 `apps/tui/src/ipc/TuiIpcControllerTestHooks.h`，把 `TuiIpcController` 从 header 草案推进为可执行的 controller baseline，同时保持 public header 不泄漏 platform/CLI/raw daemon private surface。
2. 已新增 `tests/unit/tui/TuiIpcControllerTest.cpp`、`tests/unit/tui/TuiIpcPermissionDeniedTest.cpp` 并更新 `tests/unit/tui/CMakeLists.txt`，focused 守住 success roundtrip 与五类稳定错误归一化。
3. `Build_CMakeTools(buildTargets=["dasall_tui_ipc_controller_unit_test","dasall_tui_ipc_permission_denied_unit_test"])` 通过；首次构建暴露新测试里把 controller 声明为 `const` 的局部错误，已在同轮最小修复后重跑通过。
4. `ListTests_CMakeTools()` 可发现 `TuiIpcControllerTest` 与 `TuiIpcPermissionDeniedTest`。
5. `RunCtest_CMakeTools(tests=["TuiIpcControllerTest","TuiIpcPermissionDeniedTest"])` 仍命中仓库已知泛化 `生成失败`；已按 repo fallback 口径执行 `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiIpcControllerTest|TuiIpcPermissionDeniedTest)$'`，2/2 通过。
6. 当前结果只闭合 controller/serialization/error normalization 本身，不代表 `DaemonTuiDataSource`、startup failure path、session open/close/query seam、route catalog 真消费或命令迁移已完成；这些继续后置到 `TUI-TODO-023~030`。

结论：`TUI-TODO-022` 已闭合，`TuiIpcController` 现在具备可执行的 transport、serialization 与稳定错误归一化基线；下一步优先转入未阻塞的 `TUI-TODO-027`，继续冻结 route catalog projection 字段，同时保留 `TUI-TODO-023~026` 受 `BLK-TUI-007` 的 session seam 约束。