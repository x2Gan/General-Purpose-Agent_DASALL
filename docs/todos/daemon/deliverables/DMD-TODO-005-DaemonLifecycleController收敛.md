# DMD-TODO-005 DaemonLifecycleController 收敛

状态：Done
日期：2026-04-28
来源 TODO：docs/todos/daemon/DASALL_daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只收敛 daemon 壳层的生命周期状态机，不提前实现 listener host、signal handler、supervisor adapter 或完整 graceful shutdown 主链。
2. 状态机只冻结 v1 直接需要的状态推进、请求准入与 shutdown timeout 语义；真正的 listener close、publish 排空和 watchdog 通知仍留给 DMD-TODO-006、007、008、009、022。
3. 本轮允许对 `DaemonBootstrap` 做轻量接线，但不把 lifecycle 组件重新扩张成第二个 bootstrap 组合根。

## 2. 研究与设计结论

### 2.1 本地证据

1. daemon 详设 6.5.1 明确要求 `Bootstrapping -> Binding -> Ready -> Draining -> Stopped` 的 v1 生命周期，以及 `Draining` 必须拒绝新请求、只排空 inflight。
2. daemon 详设 6.4.2 把 `DaemonLifecycleController::start()` 与 `shutdown(timeout)` 定义为 apps/daemon private 核心接口，要求 Ready 前不接受业务请求。
3. 当前 `DaemonBootstrap` 只有 `stop_requested_` 和 `gateway_->is_ready()` 两个隐式生命周期判断，缺少可测试的非法转移、drain timeout 和 failed 观测语义。

### 2.2 外部参考

1. `systemd.service(5)` 指出 `TimeoutStopSec=` 既约束 stop 命令等待时间，也约束服务自身的停止窗口；若超时，服务会进入强制终止路径。因此 daemon v1 需要显式的 bounded draining 结果，而不是无限等待或静默丢弃 shutdown timeout。
2. 同一文档也强调长期运行服务需要区分 `starting` 与 `ready` 语义，这与 daemon 详设中 `STARTING/READY/STOPPING/NOT_READY/STOPPED` 的观测面一致。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| Bootstrapping、Binding、Ready、Draining、Stopped、Failed 需要独立可测 owner | `apps/daemon/src/DaemonLifecycleController.{h,cpp}` | `DaemonLifecycleControllerTest` 覆盖合法转移、非法转移和 failed 观测 |
| Ready 前必须拒绝新请求 | `allows_new_requests()`、`begin_request()` | Bootstrapping/Binding/Failed/Stopped 下 `begin_request()` 全部失败 |
| shutdown 必须进入 Draining，并在窗口内排空或返回 timeout 事实 | `shutdown(timeout)`、`finish_request()` | 单测覆盖 draining 中拒绝新请求、drain 成功、timeout 暴露 `abandoned_requests` |
| lifecycle 不应再只依赖 `DaemonBootstrap.stop_requested_` 这种隐式状态 | `apps/daemon/src/DaemonBootstrap.{h,cpp}` | bootstrap 运行路径使用 lifecycle 做 start/bind/ready/fail/stop 轻量接线 |

## 4. 落盘结果

1. 新增 `apps/daemon/src/DaemonLifecycleController.h` 与 `apps/daemon/src/DaemonLifecycleController.cpp`，定义：
   - `DaemonLifecycleState`
   - `DaemonLifecycleObservation`
   - `DaemonShutdownResult`
   - `start()`、`mark_binding()`、`mark_ready()`、`mark_failed()`、`begin_request()`、`finish_request()`、`shutdown(timeout)`
2. 状态机使用 mutex + condition variable 管理 inflight 计数，保证 shutdown 进入 `Draining` 后可以等待排空或在超时时返回结构化 abandoned 数量。
3. 更新 `apps/daemon/src/DaemonBootstrap.h` 与 `apps/daemon/src/DaemonBootstrap.cpp`，将 `start/bind/ready/fail/stop` 的状态推进轻量迁移到 lifecycle 组件，不再只靠 `stop_requested_` 表达生命周期。
4. 更新 `apps/daemon/CMakeLists.txt` 与 `tests/unit/apps/daemon/CMakeLists.txt`，将 lifecycle 组件和 `DaemonLifecycleControllerTest` 纳入构建。
5. 新增 `tests/unit/apps/daemon/DaemonLifecycleControllerTest.cpp`，覆盖：
   - 合法状态推进
   - 非法重复/越级转移
   - Draining 对新请求 fail-closed
   - drain timeout 暴露 abandoned inflight 计数
   - Failed -> NOT_READY 观测语义

## 5. Validation

1. `Build_CMakeTools(buildTargets=["dasall_daemon"])`
2. `Build_CMakeTools(buildTargets=["dasall_daemon_lifecycle_controller_unit_test"])`
3. `RunCtest_CMakeTools(tests=["DaemonLifecycleControllerTest"])`

结果摘要：

1. `dasall_daemon` 编译通过，说明 lifecycle 组件已接入当前 daemon 壳层而未破坏已有 bootstrap 路径。
2. `dasall_daemon_lifecycle_controller_unit_test` 编译通过。
3. `DaemonLifecycleControllerTest` 1/1 通过；CTest stderr 仍打印仓库既有 `DartConfiguration.tcl` 缺失提示，但返回码为 0，按仓库基线计为有效证据。

## 6. 完成判定

DMD-TODO-005 已完成。判定依据：

1. v1 lifecycle 状态表已有独立 owner，不再散落在 `DaemonBootstrap` 的布尔分支中。
2. 每个已冻结状态都具备可断言的新请求行为与观测语义。
3. shutdown 现在会显式给出 drain success 或 timeout/abandoned 结果，可直接服务后续 signal、graceful shutdown 和 supervisor 任务。