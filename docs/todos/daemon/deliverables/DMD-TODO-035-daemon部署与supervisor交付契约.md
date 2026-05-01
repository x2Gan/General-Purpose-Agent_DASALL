# DMD-TODO-035 daemon 部署与 supervisor 交付契约

状态：Done
日期：2026-05-02
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务收敛 direct-bind v1 的 daemon 部署样例、supervisor 最小契约和本地运维验收清单。
2. v1 明确只交付 `Type=simple`、`--validate-only`、`--socket-path`、`SIGTERM` graceful stop 和 `SIGHUP` allowlist reload intent。
3. socket activation、`sd_notify()`、daemon 直接读取 YAML/JSON 配置文件继续保持 v2 范围外。

## 2. 根因与设计结论

### 2.1 本轮进入 035 时的真实阻塞

1. 初始部署草稿已经存在，但 `UnixIpcProvider` 仍是进程内 map/queue 夹具，`listen()` 不会创建 OS 级 Unix socket。
2. 因此 daemon 虽然会打印 `starting on ...`，但外部客户端无法在文件系统上看到 `control.sock`，原生 Python UDS smoke 会直接 `FileNotFoundError`。
3. 如果不先解决这个平台层根因，035 只能产出“文档上可部署”的假契约，无法满足“本地运维者能按文档完成 validate、start、ping/readiness、stop”的完成判定。

### 2.2 本轮收敛结论

1. `UnixIpcProvider` 必须提供真实 `AF_UNIX` transport，而不是仅限单进程共享实例的 loopback 队列。
2. daemon ping/loopback 验证不能继续复用同一个 provider 实例，必须让 daemon 侧和 client 侧 provider 分离，才能证明部署契约不依赖测试私有内存状态。
3. 部署文档中的运维 smoke 需要明确使用 `SOCK_SEQPACKET` 原始 UDS frame，并与真实控制台输出逐字对齐。
4. supervisor 交付契约继续冻结为 no-op + watchdog bridge seam；`Type=notify` 和 socket activation 不进入 v1。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| daemon 需要真实 OS 级 UDS listener | `platform/src/linux/UnixIpcProvider.cpp` | 启动 daemon 后文件系统出现 `control.sock`，外部 client 可连接 |
| stop 需要关闭并回收真实 listener fd | `apps/daemon/src/DaemonListenerHost.cpp` | `SIGTERM` 后 daemon 输出 `stopped (run=ok)` 且 listener 不再接受新连接 |
| ping/loopback 测试不得共享同一 provider 实例 | `tests/unit/apps/daemon/DaemonLoopbackFixtureTest.cpp`、`tests/integration/access/DaemonPingIntegrationTest.cpp` | 分离 provider 后仍能完成 request/response roundtrip |
| provider 单测需要使用真实唯一 socket path | `tests/unit/platform/linux/UnixIpcProviderTest.cpp`、`UnixIpcProviderLoopbackTest.cpp`、`UnixIpcProviderPeerIdentityTest.cpp`、`tests/unit/access/DaemonProtocolAdapterLocalTrustedTest.cpp` | loopback、peer identity、peer_closed、local trusted 回归通过 |
| 运维文档必须与真实输出和 packet socket 对齐 | `docs/deploy/daemon/README.md`、`docs/deploy/daemon/ACCEPTANCE_CHECKLIST.md` | 文档步骤可按命令直接复现 |

## 4. 落盘结果

1. 更新 `platform/src/linux/UnixIpcProvider.cpp` / `platform/include/linux/UnixIpcProvider.h`：
   - `listen()` 改为创建真实 `AF_UNIX`/`SOCK_SEQPACKET` listener。
   - `accept()`、`connect()`、`send()`、`receive()`、`describe_peer()`、`close()` 改为基于 OS fd 与 `SO_PEERCRED` 运作。
   - listener/channel close 会关闭真实 fd，并在 listener 路径上执行 unlink。
2. 更新 `apps/daemon/src/DaemonListenerHost.cpp`：
   - stop/close 路径现在会关闭真实 listener fd。
   - accept loop 在 close/stop 并发下不会把正常停机误判为失败。
3. 更新 daemon 近邻测试：
   - `DaemonLoopbackFixtureTest` 与 `DaemonPingIntegrationTest` 改为 daemon/client 双 provider。
   - platform/access 单测改为使用唯一临时 socket 路径，避免 stale socket 与路径冲突。
4. 更新部署交付文档：
   - `docs/deploy/daemon/README.md`
   - `docs/deploy/daemon/ACCEPTANCE_CHECKLIST.md`
   - `docs/deploy/daemon/dasall-daemon.service`
   - `docs/deploy/daemon/daemon.example.json`
   - `docs/deploy/daemon/daemon.example.yaml`

## 5. Validation

### 5.1 Focused build/test

1. `Build_CMakeTools(buildTargets=["dasall_unix_ipc_provider_unit_test","dasall_unix_ipc_provider_peer_identity_unit_test","dasall_unix_ipc_provider_loopback_unit_test","dasall_access_daemon_protocol_adapter_local_trusted_unit_test","dasall_daemon_loopback_fixture_unit_test","dasall_access_daemon_ping_integration_test","dasall_daemon"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["UnixIpcProviderTest","UnixIpcProviderPeerIdentityTest","UnixIpcProviderLoopbackTest","DaemonProtocolAdapterLocalTrustedTest","DaemonLoopbackFixtureTest","DaemonPingIntegrationTest"])`
   - 结果：通过，6/6 通过。
3. `Build_CMakeTools(buildTargets=["dasall_daemon_config_validator_unit_test","dasall_daemon_graceful_shutdown_unit_test","dasall_access_daemon_ping_integration_test","dasall_daemon"])`
   - 结果：通过。
4. `RunCtest_CMakeTools(tests=["DaemonConfigValidatorTest","DaemonGracefulShutdownTest","DaemonPingIntegrationTest"])`
   - 结果：通过，3/3 通过。

### 5.2 Real smoke

1. `./build/vscode-linux-ninja/apps/daemon/dasall_daemon --validate-only --socket-path /tmp/dasall-dmd035/control.sock`
   - 结果：输出 `[dasall_daemon] config validation passed without creating listener resources`。
2. `./build/vscode-linux-ninja/apps/cli/dasall_cli ping`
   - 结果：在 daemon 未启动时输出 `[dasall_cli] daemon ping: FAILED — daemon unavailable or timeout`。
3. `./build/vscode-linux-ninja/apps/daemon/dasall_daemon --socket-path /tmp/dasall-dmd035/control.sock`
   - 结果：输出 `[dasall_daemon] starting on /tmp/dasall-dmd035/control.sock`，并在 `/tmp/dasall-dmd035/control.sock` 创建真实 Unix socket。
4. 原生 Python UDS smoke（`socket.AF_UNIX` + `socket.SOCK_SEQPACKET`）
   - 结果：`ping` 与 `readiness` 都能读回 completed response；`agent_result.response_text` 中分别包含 `daemon_version` / `readiness` 摘要与 readiness state 字段。
5. `kill -TERM <daemon-pid>`
   - 结果：输出 `[dasall_daemon] stopped (run=ok)`。

## 6. 完成判定

DMD-TODO-035 已完成。判定依据：

1. `docs/deploy/daemon/` 已冻结 direct-bind v1 的 service 样例、配置样例、README 与验收清单。
2. 真实 daemon 现在会创建 OS 级 Unix socket，本地运维者可按文档完成 validate、start、ping/readiness、stop。
3. supervisor 契约已明确冻结为 v1 的 `Type=simple` + no-op/watchdog bridge seam，socket activation 保持 v2 非交付项。
4. 035 的完成不再依赖“同一 provider 实例共享内存状态”的测试假设，而是由分离 provider 的自动化回归与真实 smoke 同时覆盖。