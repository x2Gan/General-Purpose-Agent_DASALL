# DMD-TODO-007 DaemonSignalHandler 收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛 daemon 的受控 signal 响应，不提前实现 hot-reload 配置应用、watchdog 通知或 graceful shutdown 排空细节。
2. signal handler 在 v1 只负责记录 shutdown/reload intent；真正的 `bootstrap.stop()` 与后续 gateway shutdown 必须在主线程完成，避免把复杂逻辑放进异步 signal 上下文。
3. SIGHUP 在本轮只形成 reload intent 和观测事实，不直接执行配置热更；真正的 reload 白名单与配置应用留给 DMD-TODO-033。

## 2. 研究与设计结论

### 2.1 本地证据

1. daemon 专项 TODO 明确要求 `main` 不再持有裸全局 bootstrap 指针，且 SIGHUP 只能触发 reload intent，不应越权执行业务恢复或重建 listener。
2. daemon 详设 6.5.2 指定主线程负责 startup、shutdown、signal 与状态推进，因此 signal 进入后由主线程消费 intent 才符合 owner 边界。
3. 005 之后 `DaemonBootstrap::stop()` 已不再是纯布尔翻转，signal handler 继续直接调用 bootstrap 会把 mutex/condition-variable 路径放进异步 signal 上下文，边界已经不安全。

### 2.2 外部参考

1. `signal-safety(7)` 指出复杂程序通常应采取“signal handler 只调用 async-signal-safe 操作并保持可重入”的策略，而不是在 handler 内调用非安全函数。
2. 这意味着 daemon 的 signal handler 应只更新最小 flag/intention，由主线程在正常执行上下文里完成 `bootstrap.stop()`、日志和后续 shutdown 协调。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| main 不再持有全局 bootstrap 裸指针 | `apps/daemon/src/DaemonSignalHandler.{h,cpp}` + `apps/daemon/src/main.cpp` | `DaemonSignalHandlerTest` 与 `dasall-daemon` 编译通过 |
| signal handler 只记录 intent，不直接执行业务逻辑 | `request_shutdown()`、`request_reload()`、`shutdown_requested()`、`reload_requested()` | SIGTERM/SIGINT 只置 shutdown intent，SIGHUP 只置 reload intent |
| shutdown 必须由主线程消费 signal intent 后执行 | `main.cpp` 轮询 `DaemonSignalHandler` 并在正常线程调用 `bootstrap.stop()` | signal handler 代码不再直接触达 bootstrap |
| reload 在 v1 只形成事实，不直接应用配置 | `main.cpp` 对 SIGHUP 打印最小提示并清理 intent | 007 不引入 reload side effect |

## 4. 落盘结果

1. 新增 `apps/daemon/src/DaemonSignalHandler.h` 与 `apps/daemon/src/DaemonSignalHandler.cpp`，定义：
   - `install_handlers()`
   - `request_shutdown()`
   - `request_reload()`
   - `shutdown_requested()` / `reload_requested()`
   - `last_signal()`
   - `clear_requests()`
2. 组件内部使用 `sig_atomic_t` 保存 shutdown/reload/last-signal 三类最小事实，并通过静态 dispatcher 统一接收 SIGTERM、SIGINT、SIGHUP。
3. 更新 `apps/daemon/src/main.cpp`：
   - 移除全局 bootstrap 裸指针和 `on_shutdown_signal()`。
   - 改为安装 `DaemonSignalHandler`。
   - 将 `bootstrap.run()` 放入单独线程运行。
   - 由主线程轮询 shutdown/reload intent，并在普通线程上下文里调用 `bootstrap.stop()` 或记录 reload 提示。
4. 更新 `apps/daemon/CMakeLists.txt` 与 `tests/unit/apps/daemon/CMakeLists.txt`，纳入 signal handler 组件与 `DaemonSignalHandlerTest`。
5. 新增 `tests/unit/apps/daemon/DaemonSignalHandlerTest.cpp`，覆盖 SIGTERM、SIGINT、SIGHUP 的最小受控语义。

## 5. Validation

1. `Build_CMakeTools(buildTargets=["dasall-daemon"])`
2. `Build_CMakeTools(buildTargets=["dasall-daemon_signal_handler_unit_test"])`
3. `RunCtest_CMakeTools(tests=["DaemonSignalHandlerTest"])`

结果摘要：

1. `dasall-daemon` 编译通过，说明主线程轮询 + run thread 的接线没有破坏现有 daemon 启动路径。
2. `dasall-daemon_signal_handler_unit_test` 编译通过。
3. `DaemonSignalHandlerTest` 1/1 通过；CTest stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库基线计为有效证据。

## 6. 完成判定

DMD-TODO-007 已完成。判定依据：

1. `main.cpp` 已不再持有全局 bootstrap 裸指针。
2. SIGTERM/SIGINT 现在只形成 shutdown intent，SIGHUP 只形成 reload intent，均可单测断言。
3. signal handler 不再直接执行复杂 shutdown 逻辑，主线程 owner 边界与 async-signal-safe 约束已收口。