# DMD-TODO-027 daemon failure / shutdown / profile gate 收敛

## 1. 任务范围

- 任务 ID：DMD-TODO-027
- 目标：验证 daemon failure injection、graceful shutdown 与 profile 兼容门。
- 设计锚点：daemon 详设 6.10、6.12、9、11。

## 2. 实施结果

### 2.1 failure injection 集成测试

新增 `tests/integration/access/DaemonFailureInjectionIntegrationTest.cpp`，覆盖三类 daemon failure path：

1. `bind conflict`：注入 `AddressInUse` listener bind 失败，验证 daemon 在 Ready 前 fail-closed。
2. `peer identity unsupported`：注入 `describe_peer` failure，验证 daemon 返回稳定 rejected response，且不进入 runtime backend。
3. `runtime timeout`：通过 runtime backend failure injection 返回 `runtime_dispatch_timeout`，验证 daemon wire response 能稳定承载 timeout 键。

### 2.2 profile compatibility 集成测试

新增 `tests/integration/access/DaemonProfileCompatibilityTest.cpp`，覆盖五档 baseline profile：

1. 通过 `DaemonProfileProjection` 加载 profile daemon 键。
2. 使用投影出的 backlog/dispatch timeout/diag/watchdog 配置启动 in-process daemon。
3. 验证 unary 主链在五档 profile 下都保持可用。
4. 验证 diagnostics 仍默认关闭，返回 `diag_disabled`，且不会改变 unary 主流程。

### 2.3 graceful shutdown gate 复用

本轮 gate 同时复用既有 `DaemonGracefulShutdownTest` 作为 shutdown draining focused evidence，确保：

1. inflight request 未排空时，`stop()` 不会提前返回。
2. `shutdown(timeout)` 会向 gateway 透传排空窗口。
3. daemon loop 在 inflight 释放后可以 clean exit。

## 3. 修改文件

1. `tests/integration/access/CMakeLists.txt`
2. `tests/integration/access/DaemonFailureInjectionIntegrationTest.cpp`
3. `tests/integration/access/DaemonProfileCompatibilityTest.cpp`
4. `docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md`

## 4. 验证证据

执行命令：

```text
Build_CMakeTools(buildTargets=["dasall_access_daemon_failure_injection_integration_test","dasall_access_daemon_profile_compatibility_integration_test","dasall_daemon_graceful_shutdown_unit_test"])
RunCtest_CMakeTools(tests=["DaemonFailureInjectionTest","DaemonGracefulShutdownTest","DaemonProfileCompatibilityTest"])
```

结果：

1. `DaemonFailureInjectionTest`：Passed。
2. `DaemonGracefulShutdownTest`：Passed。
3. `DaemonProfileCompatibilityTest`：Passed。
4. `RunCtest_CMakeTools` stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按当前仓库基线计为有效 focused gate 证据。

## 5. 约束符合性

1. 未把 daemon 扩张成第二主控；failure injection 仅验证入口失败与 fail-closed 响应，不改写 Runtime/Recovery 边界。
2. profile compatibility 测试只消费 `DaemonProfileProjection` 的受管配置，不引入 daemon 私有旁路配置。
3. graceful shutdown 继续通过既有 draining 语义验证，不新增业务恢复裁定或额外调度逻辑。