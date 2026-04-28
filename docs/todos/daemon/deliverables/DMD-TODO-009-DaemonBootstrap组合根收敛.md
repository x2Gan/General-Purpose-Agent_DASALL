# DMD-TODO-009 DaemonBootstrap 组合根收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只把 daemon 壳层依赖收口到 `DaemonBootstrap::build(config)` 与 `run(context)` 这条组合根路径，不提前扩展 frame codec、health/router 或 graceful shutdown 细节。
2. `build(config)` 负责把已经过结构化校验的 config 与外部依赖组装成只读 `DaemonProcessContext`；失败时不返回半初始化对象。
3. `run(context)` 负责消费 context 内的 lifecycle、listener、gateway 与 supervisor seam；main 只负责提供依赖和 signal 外围装配。

## 2. 研究与设计结论

### 2.1 本地证据

1. DMD-TODO-002 已定义 `DaemonBootstrapConfig` 与 `DaemonProcessContext`，但在 009 之前它们还没有真正成为 daemon 的组合根入口，`main.cpp` 仍直接 new `IIPC` / `AccessGateway` 然后传给旧构造器。
2. 005/006/007/008 已分别收敛 lifecycle、listener、signal、supervisor seam；若 009 不把这些 seam 通过 `build(config)` / `run(context)` 统一装配，bootstrap 仍然停留在“构造器塞满依赖”的中间态。
3. 009 的最小判别点是三件事：
   - config invalid 不得进入 bind；
   - gateway not ready 不得接受请求；
   - build 失败不得返回半初始化 context。

### 2.2 设计结论

1. `DaemonBootstrap::build(config, dependencies)` 必须在进入运行态前收敛 `ipc`、`access_gateway`、可选 `watchdog_service`、`effective_profile_id` 和 `config_revision`，成功时返回一致的 `DaemonProcessContext`。
2. `DaemonProcessContext` 需要显式承载可选 `watchdog_service`，这样 supervisor seam 才能在组合根层完成装配，而不把 `main.cpp` 变成事实上的 bootstrap owner。
3. `DaemonBootstrap::run(context)` 应从 context 派生 endpoint、listener listen options、dispatch timeout 和 supervisor adapter 选项；bootstrap 不再直接消费裸 socket path 或硬编码 listener 参数。
4. `main.cpp` 应改为先 `build(config)` 再 `run(context)`，从而把“依赖构造”与“运行执行”拆开。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| build 失败不返回半初始化 context | `apps/daemon/src/DaemonBootstrap.{h,cpp}` | `DaemonBootstrapTest` 断言 invalid config / missing deps 返回空 context |
| run(context) 消费 listener/supervisor seam | `DaemonBootstrap::run(context)`、`DaemonListenerHost::set_listen_options(...)` | `dasall_daemon` 编译通过，`DaemonLoopbackFixtureTest` 回归通过 |
| process context 承载 watchdog bridge | `apps/daemon/src/DaemonConfig.h` | context 成功路径保留 watchdog service、profile id、config revision |
| main 切到 build/run 组合根路径 | `apps/daemon/src/main.cpp` | `main` 不再手工拼 endpoint + 裸 run(endpoint) |

## 4. 落盘结果

1. 更新 `apps/daemon/src/DaemonConfig.h`：
   - `DaemonProcessContext` 新增可选 `watchdog_service` 句柄。
2. 更新 `apps/daemon/src/DaemonListenerHost.h` 与 `apps/daemon/src/DaemonListenerHost.cpp`：
   - 新增 `set_listen_options(...)`，让 bootstrap 能从 config 注入 backlog / payload budget。
3. 更新 `apps/daemon/src/DaemonBootstrap.h` 与 `apps/daemon/src/DaemonBootstrap.cpp`：
   - 新增 `BuildDependencies`。
   - 新增 `build(config, dependencies)`，在 config / deps 不一致时返回空结果。
   - 新增 `run(context)`，从 context 装配 listener host、supervisor adapter 与 receive deadline。
   - bootstrap 仍保留 lifecycle owner，但 listener/supervisor 配置现在由 context 派生，而不是构造器硬编码。
4. 更新 `apps/daemon/src/main.cpp`：
   - 先调用 `DaemonBootstrap::build(...)` 产出 `DaemonProcessContext`；
   - 再通过默认构造的 `DaemonBootstrap` 执行 `run(*context)`。
5. 新增 `tests/unit/apps/daemon/DaemonBootstrapTest.cpp`，覆盖：
   - build success 保留 profile id / config revision
   - build failure 不返回半初始化 context
   - gateway not ready 时 run(context) 不 bind / 不 accept
6. 更新 `tests/unit/apps/daemon/DaemonLoopbackFixtureTest.cpp` 与 `tests/unit/apps/daemon/CMakeLists.txt`：
   - loopback fixture 切换到 `build(config)` / `run(context)` 成功路径
   - 新增 `DaemonBootstrapTest` target，并为 bootstrap/loopback targets 补齐 supervisor adapter 依赖

## 5. Validation

1. `Build_CMakeTools(buildTargets=["dasall_daemon"])`
2. `Build_CMakeTools(buildTargets=["dasall_daemon_bootstrap_unit_test","dasall_daemon_loopback_fixture_unit_test"])`
3. `RunCtest_CMakeTools(tests=["DaemonBootstrapTest","DaemonLoopbackFixtureTest"])`

结果摘要：

1. `dasall_daemon` 编译通过，说明 build/run 组合根改造没有破坏主构建。
2. `DaemonBootstrapTest` 通过，证明 invalid config / missing deps 不返回 context，gateway not ready 不进入 bind/accept。
3. `DaemonLoopbackFixtureTest` 回归通过，证明 build(config) / run(context) 成功路径仍能驱动 daemon 消费 ping 并回传 response。
4. CTest stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库基线计为有效证据。

## 6. 完成判定

DMD-TODO-009 已完成。判定依据：

1. daemon 已具备显式 `build(config)` / `run(context)` 组合根路径。
2. build 失败不会泄露半初始化 `DaemonProcessContext`，gateway not ready 也不会进入 bind/accept。
3. `main.cpp` 已不再依赖旧的裸构造器 + `run(endpoint)` 路径，listener/supervisor seam 已进入 context 驱动的 bootstrap owner。