# DMD-TODO-031 CLI-daemon wire contract 收敛

状态：Done
日期：2026-05-02
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛 CLI 到 daemon 的 v1 命令与响应解析契约，不提前扩张到 unary 主链、async receipt 集成矩阵或部署路径选择。
2. 023 与 024 已提供 unit discoverability 和真实 daemon ping roundtrip，本任务直接复用这些前序能力，不重复规划 daemon listener/runtime gate。
3. 任务目标是把 CLI 从“只验证 send 成功”收敛到“编码 v1 request frame、读取并解析 `UdsResponseFrame`、按 disposition 输出稳定文本”。

## 2. 根因与设计结论

### 2.1 旧 CLI 为什么不满足 031

1. `apps/cli/src/CliIpcClient.cpp` 只执行 `connect/send`，返回 `bool`，没有 `receive()` 和响应解析步骤。
2. `apps/cli/src/main.cpp` 仅根据布尔值输出 `ping ok` 或 `submit accepted`，无法区分 `completed`、`accepted_async`、`rejected`、`not_ready`。
3. `CliCommandParser` 只支持 `ping/submit`，不能表达 `readiness/status/cancel/diag` 的 v1 参数形状。
4. `CliOutputFormatter` 只接受原始字符串，不掌握 `receipt_ref`、`error_ref`、`response_text` 等结构化字段。

### 2.2 本轮收敛结论

1. CLI 需要显式拥有 `DaemonClientResponse`，并将 transport 成功与 daemon disposition 成功分离表达。
2. 为避免在 CLI 再实现一套临时 JSON 解析器，应直接复用共享 `DaemonFrameCodec`，补齐 `encode_request_frame()` 与 `decode_response_frame()`。
3. `CliCommandParser` 需要支持 `run/status/cancel/readiness/diag`，其中 `submit` 仅保留为 `run` 兼容别名，`diag` 默认隐藏于 usage 之外。
4. `DaemonPingIntegrationTest` 应额外证明 `CliIpcClient` 可以在真实 daemon roundtrip 中读取 `pong/readiness`，而不是只留 daemon 侧 raw client 断言。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| CLI 需要共享 v1 frame 编码/解码 | `access/include/daemon/DaemonFrameCodec.h`、`access/src/daemon/DaemonFrameCodec.cpp` | `DaemonFrameCodecTest` 能 roundtrip request/response |
| CLI 需要结构化响应对象 | `apps/cli/src/CliIpcClient.*` | `CliIpcClientTest`、`CliIpcClientResponseTest` |
| CLI 需要完整命令行参数模型 | `apps/cli/src/CliCommandParser.*` | `CliDaemonCommandParserTest` |
| CLI 输出必须基于 disposition/receipt/error | `apps/cli/src/CliOutputFormatter.*` | `CliDaemonOutputFormatterTest` |
| 真实 ping 集成必须覆盖 CLI 读 pong | `tests/integration/access/DaemonPingIntegrationTest.cpp` | `DaemonPingIntegrationTest` |

## 4. 落盘结果

1. 更新 `access/include/daemon/DaemonFrameCodec.h` 与 `access/src/daemon/DaemonFrameCodec.cpp`：
   - 新增 `encode_request_frame()`。
   - 新增 `decode_response_frame()`。
   - 扩展最小 JSON reader 支持整数值，以解析 `exit_code_hint`。
2. 更新 `apps/cli/src/CliIpcClient.h` / `CliIpcClient.cpp`：
   - 新增 `DaemonClientResponse`。
   - 将 `ping_daemon()`、`submit()`、`query_status()`、`cancel()`、`read_readiness()`、`run_diagnostics()` 全部切到 request/response roundtrip。
   - 失败路径统一保留 `failure_reason`，并在成功路径解析 `receipt_ref`、`error_ref`、`response_text`。
3. 更新 `apps/cli/src/CliCommandParser.h` / `CliCommandParser.cpp`：
   - 支持 `run/status/cancel/readiness/diag`。
   - `submit` 作为 `run` 兼容别名。
   - `status/cancel` 收敛为 `receipt_ref + ownership_token + [actor_ref]`。
4. 更新 `apps/cli/src/CliOutputFormatter.h` / `CliOutputFormatter.cpp` 与 `apps/cli/src/main.cpp`：
   - 输出按 `completed/accepted_async/rejected/not_ready` 区分。
   - 成功路径展示 `receipt_ref`、`error_ref`、`response_text`。
   - 主程序按 disposition 决定退出码，而不是按 send 成功决定。
5. 更新 `tests/unit/access/CMakeLists.txt` 与 `tests/integration/access/CMakeLists.txt`：
   - 注册 `CliIpcClientResponseTest`、`CliDaemonCommandParserTest`、`CliDaemonOutputFormatterTest`。
   - 为 CLI 单测与 ping 集成目标补齐 `dasall_access` / `CliIpcClient.cpp` 依赖。
6. 更新 `tests/unit/access/CliIpcClientTest.cpp`、`CliIpcClientUnavailableTest.cpp`，并新增 `CliIpcClientResponseTest.cpp`、`CliDaemonCommandParserTest.cpp`、`CliDaemonOutputFormatterTest.cpp`。
7. 更新 `tests/integration/access/DaemonPingIntegrationTest.cpp`：
   - 在既有 raw response 断言后追加 `CliIpcClient::ping_daemon()` roundtrip，验证 CLI 真正读到 `completed` 与 readiness 摘要。

## 5. Validation

1. `Build_CMakeTools(buildTargets=["dasall_access_daemon_frame_codec_unit_test"])`
2. `RunCtest_CMakeTools(tests=["DaemonFrameCodecTest"])`
3. `Build_CMakeTools(buildTargets=["dasall_cli","dasall_access_cli_ipc_client_unit_test","dasall_access_cli_ipc_client_response_unit_test","dasall_access_cli_ipc_client_unavailable_unit_test","dasall_access_cli_command_parser_unit_test","dasall_access_cli_output_formatter_unit_test","dasall_access_daemon_ping_integration_test"])`
4. `RunCtest_CMakeTools(tests=["CliIpcClientTest","CliIpcClientResponseTest","CliIpcClientUnavailableTest","CliDaemonCommandParserTest","CliDaemonOutputFormatterTest","DaemonPingIntegrationTest"])`

结果摘要：

1. `DaemonFrameCodecTest` 通过，说明 CLI 复用共享 codec 的 request encode/response decode 能成立。
2. `CliIpcClientTest`、`CliIpcClientResponseTest`、`CliIpcClientUnavailableTest` 全部通过，说明 CLI 已能区分 transport failure、accepted_async、not_ready、rejected 与 completed。
3. `CliDaemonCommandParserTest`、`CliDaemonOutputFormatterTest` 通过，说明命令面和输出面已对齐 031 验收口径。
4. `DaemonPingIntegrationTest` 通过，说明真实 daemon roundtrip 中 CLI 已能读回 pong/readiness，而不是只断言 `send()` 成功。
5. CTest 工具 stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库现有基线计为有效证据。

## 6. 完成判定

DMD-TODO-031 已完成。判定依据：

1. CLI 现在会编码 v1 request frame、读取 daemon response，并解析为结构化 `DaemonClientResponse`。
2. `accepted_async/rejected/not_ready/receipt` 四类核心语义均有 focused 自动化覆盖。
3. 真实 ping 集成已经把 CLI 响应消费纳入断言，不再停留在 `connect/send` smoke。
4. 阻塞项 `DMD-BLK-007` 可清除，后续 unary/status/cancel/async 集成可直接复用当前 wire contract，不必再补客户端响应链路。