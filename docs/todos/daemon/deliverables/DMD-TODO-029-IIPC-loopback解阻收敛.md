# DMD-TODO-029 IIPC loopback 解阻收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只补齐 `UnixIpcProvider` 的测试可用双向 request/response loopback，不改 `IIPC` 公共接口语义，不引入 daemon 特例。
2. v1 目标是让 daemon listener fixture 能真实消费 client payload，并让 client 读回 daemon response；真实 OS UDS 行为与更高阶集成门继续留给后续任务。
3. 本轮不提前实现 `DaemonListenerHost`、`DaemonBootstrap::build(...)` 或 CLI response parser，只为 DMD-BLK-004 解阻并给 006/009/024 提供可信测试底座。

## 2. 研究与设计结论

### 2.1 本地证据

1. daemon 专项 TODO 的 DMD-BLK-004 指出当前 `connect/send` 与 listener `accept/receive` 不共享 payload，导致 ping/unary/async 集成门只能证明 client send 成功，不能证明 daemon 真实处理并返回 response。
2. `DaemonBootstrap::run(...)` 当前以 `listen()`、`accept()`、`receive()`、`send()` 串起最小主链；若 provider 没有成对 channel 语义，后续 006 的 listener host 拆分和 009 的组合根收敛都没有可信 fixture。
3. 029 的完成判定要求同时证明两件事：请求 payload 被 daemon listener 消费，以及响应 payload 被 client 读回。因此 provider 必须补齐成对 channel、队列投递与 close propagation，而不是继续用单向假通道冒充闭环。

### 2.2 设计结论

1. `UnixIpcProvider::listen(...)` 必须保存 endpoint 与 pending server-side channel 队列，使 `connect(...)` 能为匹配 listener 建立成对 channel，并由 `accept(...)` 返回对应 server channel。
2. 每个 channel 必须持有自己的 inbound queue 与 peer channel 引用，`send(...)` 要把 payload 投递到 peer queue，而不是仅返回 bytes_sent。
3. `receive(...)` 必须先投递已排队 payload，再返回 `peer_closed`，否则 daemon 在发送 response 后立即 close server channel 时，client 侧会丢失最后一个响应。
4. close propagation 必须把 peer 侧标为 `peer_closed`，并让后续 `send(...)` 返回 `PeerClosed`；payload budget 继续沿用 listener 的 `max_payload_bytes` 约束。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| listener/connect/accept 形成成对 channel | `platform/include/linux/UnixIpcProvider.h`、`platform/src/linux/UnixIpcProvider.cpp` | `UnixIpcProviderLoopbackTest` 能拿到 client/server 双端并双向收发 |
| send 投递到 peer inbound queue | `UnixIpcProvider::send()`、`UnixIpcProvider::receive()` | server 能收到 client request，client 能收到 server response |
| response close 不丢包 | `UnixIpcProvider::receive()` 先消费 queue 后暴露 `peer_closed` | `DaemonLoopbackFixtureTest` 能稳定读回 ping response |
| daemon fixture 真实消费 request 并返回 response | `tests/unit/apps/daemon/DaemonLoopbackFixtureTest.cpp` + `DaemonBootstrap.cpp` | ping payload 经 daemon listener 处理后返回 `{"status":"ok","service":"dasall-daemon"}` |
| 新增 loopback tests 纳入 CTest | `tests/unit/platform/linux/CMakeLists.txt`、`tests/unit/apps/daemon/CMakeLists.txt` | 两条新测试 target 可构建、CTest 可发现并通过 |

## 4. 落盘结果

1. 更新 `platform/include/linux/UnixIpcProvider.h`：
   - `ListenerState` 新增 `endpoint` 与 `pending_server_channels`。
   - `ChannelState` 新增 `peer_channel_fd` 与 `inbound_payloads`。
2. 更新 `platform/src/linux/UnixIpcProvider.cpp`：
   - `listen(...)` 保存 listener endpoint。
   - `connect(...)` 为匹配 listener 建立 client/server 成对 channel，并把 server channel 进入 pending accept 队列。
   - `accept(...)` 优先返回 pending server-side channel；无 pending 时保留原有 fallback 行为。
   - `send(...)` 将 payload 投递到 peer inbound queue，并在 peer 不可用时返回 `PeerClosed`。
   - `receive(...)` 先消费队列，再返回 `peer_closed`，避免响应在 close propagation 时被吞掉。
   - `close(...)` 向 peer 传播 `peer_closed` 事实。
3. 新增 `tests/unit/platform/linux/UnixIpcProviderLoopbackTest.cpp`，覆盖：
   - client -> server request 传递
   - server -> client response 传递
   - close propagation
   - payload-too-large 拒绝
4. 新增 `tests/unit/apps/daemon/DaemonLoopbackFixtureTest.cpp`，使用 `DaemonBootstrap` + ready fake gateway 验证 daemon listener 能消费 ping 并把 response 回传给 client。
5. 更新两处 unit CMake 注册，将 `UnixIpcProviderLoopbackTest` 与 `DaemonLoopbackFixtureTest` 纳入 CTest discoverability。

## 5. Validation

1. `Build_CMakeTools(buildTargets=["dasall_platform","dasall_unix_ipc_provider_loopback_unit_test","dasall_unix_ipc_provider_peer_identity_unit_test","dasall_daemon_loopback_fixture_unit_test"])`
2. `RunCtest_CMakeTools(tests=["UnixIpcProviderLoopbackTest","DaemonLoopbackFixtureTest","UnixIpcProviderPeerIdentityTest"])`

结果摘要：

1. `dasall_platform` 与三条 focused unit targets 均编译通过。
2. `UnixIpcProviderLoopbackTest` 通过，证明 paired channel、双向 payload 传递、close propagation 与 payload budget 已闭环。
3. `DaemonLoopbackFixtureTest` 通过，证明 daemon listener 已真实消费 ping payload，且 client 读回 daemon response。
4. `UnixIpcProviderPeerIdentityTest` 回归通过，说明 029 没有破坏既有 peer identity 语义。
5. CTest stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库基线计为有效证据。

## 6. 完成判定

DMD-TODO-029 已完成。判定依据：

1. `UnixIpcProvider` 现在具备测试可用的双向 loopback channel 绑定、队列投递与 close propagation。
2. `UnixIpcProviderLoopbackTest` 已证明 client/server 双向 request/response 闭环，而不是只验证 `send()` 成功。
3. `DaemonLoopbackFixtureTest` 已证明 daemon listener 真实消费请求并向 client 返回响应，满足 DMD-BLK-004 的解阻条件。
4. DMD-BLK-004 已可视为清除，DMD-TODO-006 与 DMD-TODO-009 可以继续进入实现轮次。