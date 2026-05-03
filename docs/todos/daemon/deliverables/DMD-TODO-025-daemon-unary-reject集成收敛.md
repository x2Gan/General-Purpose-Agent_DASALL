# DMD-TODO-025 daemon unary / reject 集成收敛

## 1. 任务范围

- 任务 ID：DMD-TODO-025
- 目标：验证 daemon unary 主链 happy path，以及 unknown command、auth deny、validation reject、runtime bridge unavailable 四类拒绝路径。
- 设计锚点：daemon 详设 6.6.2、9。

## 2. 实施结果

### 2.1 in-process daemon 集成夹具

新增 `tests/integration/access/DaemonIntegrationHarness.h`，把 daemon unary/reject 集成测试统一收敛到一个最小夹具：

1. 复用 `DaemonBootstrap`、`UnixIpcProvider`、`CliIpcClient` 和共享 `DaemonFrameCodec`，不引入测试私有协议分支。
2. 夹具统一完成临时 socket 目录创建、gateway 构造、daemon 线程启动、connect polling、raw request roundtrip 与停机 join。
3. 允许集成测试同时覆盖 CLI 标准命令与 raw frame 场景，其中 raw frame 仅用于构造 gateway routing 级 unknown-command 负例。

### 2.2 unary happy path

新增 `tests/integration/access/DaemonUnaryIntegrationTest.cpp`，覆盖：

1. CLI `run` 请求经 daemon -> AccessGateway -> RuntimeDispatchBackend -> response encode 完整往返。
2. runtime backend 收到稳定的 `request_id`、`actor_ref` 与 payload，证明共享 access core 已承接主体解析与请求投影。
3. daemon 返回 `completed` disposition，且 CLI 能读取 `response_text`。

### 2.3 reject path 矩阵

新增 `tests/integration/access/DaemonRejectPathIntegrationTest.cpp`，覆盖：

1. `unknown_command`：通过合法 `run` 帧 + 非法 routing key 进入 gateway reject 分支，证明拒绝发生在共享 access core，而不是 codec 旁路。
2. `auth deny`：本地主体不在 trusted allowlist 时返回 `authentication_required`，且不触发 runtime backend。
3. `validation reject`：payload 超出限制时返回 `payload_size_limit_exceeded`，且不触发 runtime backend。
4. `runtime bridge unavailable`：dispatch backend 为空时返回 `runtime bridge backend is not configured`，证明请求已通过 access 验证但在 RuntimeBridge fail-closed。

### 2.4 real daemon binary unary smoke

新增 `tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`，覆盖：

1. 启动 built `dasall_daemon` binary，走真实 `main.cpp -> AgentFacade::init()` 路径，而不是 in-process harness。
2. 使用 built `dasall_cli` 先执行 `ping` 等待 daemon ready，再执行 `run '{"prompt":"binary smoke"}'`。
3. 断言 CLI 返回 `submit: completed`，且响应文本包含 `runtime orchestrator skeleton completed`，证明 real binary wiring 与 protocol disposition 都正确。

## 3. 修改文件

1. `tests/integration/access/DaemonIntegrationHarness.h`
2. `tests/integration/access/DaemonUnaryIntegrationTest.cpp`
3. `tests/integration/access/DaemonRejectPathIntegrationTest.cpp`
4. `tests/integration/access/DaemonBinaryUnarySmokeTest.cpp`
5. `tests/integration/access/CMakeLists.txt`
6. `docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md`

## 4. 验证证据

执行命令：

```text
Build_CMakeTools(buildTargets=["dasall_daemon","dasall_cli","dasall_access_daemon_unary_integration_test","dasall_access_daemon_reject_path_integration_test","dasall_access_daemon_binary_unary_smoke_integration_test"])
RunCtest_CMakeTools(tests=["DaemonUnaryIntegrationTest","DaemonRejectPathIntegrationTest","DaemonBinaryUnarySmokeTest"])
```

结果：

1. `DaemonUnaryIntegrationTest`：Passed。
2. `DaemonRejectPathIntegrationTest`：Passed。
3. `DaemonBinaryUnarySmokeTest`：Passed。
4. `RunCtest_CMakeTools` stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按当前仓库基线计为有效 focused gate 证据。

## 5. 约束符合性

1. 未修改 daemon / access 生产主链，仅通过集成测试验证现有实现。
2. happy path 和 reject path 都经共享 access core，不通过测试私有 mock 绕过 `SubjectResolver`、`RequestValidator` 或 `RuntimeBridge`。
3. reject 场景均断言 runtime backend 未被调用，满足“拒绝不进入 Runtime”的完成判定。
4. real unary smoke 通过 built `dasall_daemon` + built `dasall_cli` 复验了真实 `main.cpp` 组合根，不再把 helper/harness green 误记为 binary gate 已通过。