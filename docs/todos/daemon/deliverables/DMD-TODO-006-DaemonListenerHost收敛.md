# DMD-TODO-006 DaemonListenerHost 收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只把 daemon 的 direct-bind `listen/accept/close` 监听层从 `DaemonBootstrap::run()` 抽离到 `DaemonListenerHost`，不提前改写 decode/submit/publish 主链。
2. `DaemonListenerHost` 仍然保持 v1 单线程同步模型：accept 到连接后立刻交给 connection handler，再关闭该 channel。
3. 本轮不实现 UDS 权限、stale socket 清理或真实 OS listener 关闭；这些安全与部署细节继续留给 DMD-TODO-032 与后续 platform gate。

## 2. 研究与设计结论

### 2.1 本地证据

1. DMD-TODO-006 的唯一直接 owner 代码就是 `DaemonBootstrap::run()` 中的 `listen -> accept -> handle_connection -> close` 循环；在 005/007 完成后，bootstrap 已经承担 lifecycle 与 signal 配合，再继续把监听细节留在同一个类里，会阻碍 009 的组合根收敛。
2. DMD-TODO-029 已为 in-memory loopback 建立双向 request/response seam，这意味着 006 现在可以在不触碰 decode/submit 逻辑的前提下，把监听层独立成可单测的 direct-bind 组件。
3. 本轮最便宜的判别点是：`dasall_daemon` 能否继续编译，以及新的 `DaemonListenerHostTest` 能否单独覆盖 bind 参数、accept timeout、close 后拒绝和 listener error mapping。

### 2.2 设计结论

1. `DaemonListenerHost` 应该只拥有四项职责：`bind(endpoint)`、`set_connection_handler(...)`、`accept_loop(...)`、`close()`。
2. 监听层错误需要保留 platform error 语义，因此 `bind/accept_loop/close` 使用 `PlatformResult<bool>` 而不是裸 bool，避免 009 再次为错误映射补 seam。
3. `accept_loop(...)` 只把 `Timeout` 当作正常轮询事件，其余 accept/close 错误直接向上返回；连接处理逻辑仍由 bootstrap 传入 handler 决定。
4. `DaemonBootstrap` 只负责 lifecycle、gateway readiness 和 connection handler 装配，不再直接持有 listener 主循环实现。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| listener 主循环从 bootstrap 抽离 | `apps/daemon/src/DaemonListenerHost.{h,cpp}` | `dasall_daemon` 可继续编译并运行原有 bootstrap 主链 |
| bootstrap 只装配 listener host 和 connection handler | `apps/daemon/src/DaemonBootstrap.{h,cpp}` | `DaemonLoopbackFixtureTest` 继续通过 |
| direct-bind 参数与错误映射可单测 | `tests/unit/apps/daemon/DaemonListenerHostTest.cpp` | bind 参数、timeout、close 后拒绝、listener error mapping 全部可断言 |
| listener host 纳入 daemon 构建与 unit test 拓扑 | `apps/daemon/CMakeLists.txt`、`tests/unit/apps/daemon/CMakeLists.txt` | `DaemonListenerHostTest` target 被 CTest 发现并通过 |

## 4. 落盘结果

1. 新增 `apps/daemon/src/DaemonListenerHost.h` 与 `apps/daemon/src/DaemonListenerHost.cpp`：
   - `bind(...)` 固化 daemon direct-bind backlog / payload budget，并持有 listener handle。
   - `set_connection_handler(...)` 注入 bootstrap 的 connection handler。
   - `accept_loop(...)` 负责 accept timeout 轮询、连接分发与 channel close。
   - `close()` 负责 listener host 的本地关闭状态切换。
2. 更新 `apps/daemon/src/DaemonBootstrap.h` 与 `apps/daemon/src/DaemonBootstrap.cpp`：
   - 引入 `DaemonListenerHost listener_host_` 成员。
   - `run(...)` 不再直接调用 `ipc_->listen()` / `ipc_->accept()` / `ipc_->close()`，改为委托给 listener host。
   - connection handler 继续复用原有 `handle_connection(...)`，因此 decode/submit/publish 语义保持不变。
3. 更新 `apps/daemon/CMakeLists.txt`，把 `DaemonListenerHost` 纳入 `dasall_daemon`。
4. 新增 `tests/unit/apps/daemon/DaemonListenerHostTest.cpp`，使用 scripted fake IIPC 覆盖：
   - bind 参数转发
   - accept timeout 后继续轮询
   - close 后拒绝 accept loop
   - listener error mapping
5. 更新 `tests/unit/apps/daemon/CMakeLists.txt`：
   - 注册 `DaemonListenerHostTest`
   - 让既有 `DaemonLoopbackFixtureTest` target 链上新的 `DaemonListenerHost.cpp` 依赖

## 5. Validation

1. `Build_CMakeTools(buildTargets=["dasall_daemon"])`
2. `Build_CMakeTools(buildTargets=["dasall_daemon_listener_host_unit_test","dasall_daemon_loopback_fixture_unit_test"])`
3. `RunCtest_CMakeTools(tests=["DaemonListenerHostTest","DaemonLoopbackFixtureTest"])`

结果摘要：

1. `dasall_daemon` 编译通过，说明 listener host 抽离没有破坏 daemon 主构建。
2. `dasall_daemon_listener_host_unit_test` 与 `dasall_daemon_loopback_fixture_unit_test` 编译通过。
3. `DaemonListenerHostTest` 通过，证明 direct-bind 参数、timeout、close 后拒绝与 listener error mapping 已稳定可测。
4. `DaemonLoopbackFixtureTest` 回归通过，证明 006 没有破坏 029 建立的 daemon request/response loopback 主链。
5. CTest stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库基线计为有效证据。

## 6. 完成判定

DMD-TODO-006 已完成。判定依据：

1. `DaemonBootstrap` 已不再直接承担 `listen/accept/close` 监听层职责。
2. `DaemonListenerHost` 已把 direct-bind listener accept 主流程收敛为独立组件，并且可通过 unit test 单独验证。
3. 029 的 loopback fixture 回归仍通过，说明 listener host 的引入没有把 request/response 闭环重新耦回 bootstrap。
4. DMD-TODO-009 现在可以在 listener host seam 之上继续推进组合根收敛。