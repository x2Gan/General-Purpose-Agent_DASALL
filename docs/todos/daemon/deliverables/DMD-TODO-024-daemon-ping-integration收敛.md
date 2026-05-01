# DMD-TODO-024 daemon ping integration 收敛

状态：Done
日期：2026-05-01
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛真实 daemon ping 集成，不提前扩展 CLI response parser、unary 主链、async/status/cancel 或部署契约。
2. 验收重点不是 `send()` 成功，而是 daemon fixture 真实消费 ping frame，并让 client 读回 completed response 与 readiness 摘要。
3. 若当前 loopback IPC 语义不足以支撑该集成，允许在本任务内做直接 blocker recovery，但不改 `IIPC` public interface。

## 2. 根因与设计结论

### 2.1 旧集成为什么是虚绿

1. 原 `tests/integration/access/CliDaemonPingIntegrationTest.cpp` 只验证 `CliIpcClient` 调用 `IIPC::connect/send` 返回 true，没有启动 daemon，也没有读取 response。
2. `UnixIpcProvider` 的 loopback 语义仍残留三处“假成功”路径：
   - `accept()` 在无待接连接时伪造 server channel。
   - `connect()` 在无 listener 时仍返回 standalone channel。
   - `receive()` 在 deadline 内没有 payload 时返回空成功，而不是等待或超时。
3. 这些行为会让 ping smoke 在没有 daemon、没有 request consumption、没有 response roundtrip 的情况下也可能通过，因此 024 必须先收紧 provider contract，再落真实集成测试。

### 2.2 本轮收敛结论

1. `UnixIpcProvider::accept()` 必须在没有 pending server channel 时 fail-closed 为 timeout，而不是伪造连接。
2. `UnixIpcProvider::connect()` 必须要求 listener 已存在，否则返回 timeout，不能再冒充 daemon available。
3. `UnixIpcProvider::receive()` 必须在 deadline 内等待 payload 或 peer close，避免 daemon 在 payload 还未送达时把请求误判为空。
4. ping/readiness 的 `PublishEnvelope` 必须把摘要投影到 `agent_result.response_text`，让现有 `UdsResponseFrame` 编码路径真正把摘要返回给 client。
5. 新的 `DaemonPingIntegrationTest` 必须以 in-process daemon fixture 启动 `DaemonBootstrap`，发送 v1 frame，并断言 `disposition=completed` 与 payload 中的 `daemon_version`/`readiness`。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| accept/connect 不再伪造成功 channel | `platform/src/linux/UnixIpcProvider.cpp` | 无 listener 时 `connect()` 失败；无 pending peer 时 `accept()` timeout |
| receive 在 deadline 内等待 payload | `platform/src/linux/UnixIpcProvider.cpp` | daemon fixture 不再因竞态把请求误判为空 |
| ping/readiness 摘要进入 wire response | `access/src/AccessGatewayFactory.cpp` | client 读回的 JSON 含 `agent_result.response_text` 摘要 |
| daemon loopback fixture 使用合规 socket path | `tests/unit/apps/daemon/DaemonLoopbackFixtureTest.cpp` | loopback fixture 在 DMD-TODO-032 的 socket policy 下稳定通过 |
| 真实 daemon ping 集成替换旧 send-smoke | `tests/integration/access/DaemonPingIntegrationTest.cpp`、`tests/integration/access/CMakeLists.txt` | `DaemonPingIntegrationTest` 启动 daemon 并验证 response roundtrip |

## 4. 落盘结果

1. 更新 `platform/src/linux/UnixIpcProvider.cpp`：
   - `accept()` 在没有 pending peer 时返回 `Timeout`。
   - `connect()` 在 listener 不存在时返回 `Timeout`。
   - `receive()` 在 deadline 内轮询等待 payload 或 peer close。
2. 更新 `access/src/AccessGatewayFactory.cpp`：
   - ping/readiness 响应新增 `agent_result.response_text` 与 `task_completed=true` 投影。
3. 更新 `tests/unit/platform/linux/UnixIpcProviderTest.cpp`、`UnixIpcProviderPeerIdentityTest.cpp`、`tests/unit/access/DaemonProtocolAdapterLocalTrustedTest.cpp`：
   - 对齐 provider 的 fail-closed connect 语义，先建立 listener 再 connect。
4. 更新 `tests/unit/apps/daemon/DaemonLoopbackFixtureTest.cpp`：
   - 使用 0700 临时目录满足 socket policy。
   - 改用真实 `create_daemon_access_gateway(...)`。
   - 断言 completed response 与 readiness 摘要，而不是旧 `status=ok` 文本。
5. 新增 `tests/integration/access/DaemonPingIntegrationTest.cpp`：
   - 启动 in-process daemon fixture。
   - 发送 `{"schema_version":"1","request_id":"ping-itg-001","command":"ping"}`。
   - 读取并断言 response payload。
6. 更新 `tests/integration/access/CMakeLists.txt`：
   - 取消旧 `CliDaemonPingIntegrationTest` 的集成注册。
   - 注册新的 `DaemonPingIntegrationTest` target/test。

## 5. Validation

1. `Build_CMakeTools(buildTargets=["dasall_unix_ipc_provider_unit_test","dasall_unix_ipc_provider_peer_identity_unit_test","dasall_access_daemon_protocol_adapter_local_trusted_unit_test","dasall_daemon_loopback_fixture_unit_test","dasall_unix_ipc_provider_loopback_unit_test"])`
2. `RunCtest_CMakeTools(tests=["UnixIpcProviderTest","UnixIpcProviderPeerIdentityTest","UnixIpcProviderLoopbackTest","DaemonProtocolAdapterLocalTrustedTest","DaemonLoopbackFixtureTest"])`
3. `Build_CMakeTools(buildTargets=["dasall_access_daemon_ping_integration_test"])`
4. `RunCtest_CMakeTools(tests=["DaemonPingIntegrationTest"])`

结果摘要：

1. provider contract 回归通过，说明 accept/connect/receive 的 fail-closed + wait 语义已稳定。
2. `DaemonLoopbackFixtureTest` 通过，证明 daemon listener 能真实消费 request 并返回 response。
3. `DaemonPingIntegrationTest` 通过，证明 024 已从“client send smoke”升级为“daemon 真实 roundtrip 集成”。
4. CTest 工具 stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库现有基线计为有效证据。

## 6. 完成判定

DMD-TODO-024 已完成。判定依据：

1. 现在存在独立的 `DaemonPingIntegrationTest`，并已注册到 access integration CTest 拓扑。
2. 该测试会真实启动 daemon fixture、发送 v1 ping frame、读取 daemon response，而不是只验证 `connect/send` 成功。
3. 为了支撑该集成，loopback provider 的三处虚绿语义已被收紧为 daemon-fit contract。
4. CLI 真实 ping/wire contract 仍属于 DMD-TODO-031，本任务只完成 daemon 侧真实 ping gate。