# RT-TODO-011 IScheduler 与 SchedulerTicket 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.10 已将 Scheduler 收敛为“提供有限调度能力，区分前台交互、恢复任务与后台维护任务优先级”的组件，建议接口固定为 `enqueue(...)`、`acquire_worker(...)`、`release_worker(...)`、`backpressure_state()`。
2. 同一文档的 6.14.4 已冻结三类队列的背压与溢出策略：前台队列深度 1 且 busy 时拒绝；恢复队列超限时进入 FailedSafe；后台维护队列深度 16，超限时丢弃最旧任务。
3. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 已将 RT-TODO-011 的 Build 范围限定为 `runtime/include/scheduling/IScheduler.h`、`runtime/include/scheduling/SchedulerTicket.h`，并要求 `SchedulerTest` 覆盖 queue/backpressure/ticket seam。
4. RT-TC007 要求队列、锁顺序和 backpressure 明确定义；RT-TC013 要求每个 Worker Ticket 绑定 `CancellationToken`，支持 step-level 取消传播。

## 外部参考

1. Microsoft Azure Architecture Center 的 Queue-Based Load Leveling 模式强调：应在任务入口和服务处理之间显式放置队列缓冲层，用速率控制和排队策略吸收突发流量，避免把负载尖峰直接传递给下游处理端。
2. 同一模式进一步强调 overflow 与 throttling 需要成为显式策略，而不是隐式副作用；这支持 DASALL 在 `SchedulerTicket` 公共面中直接暴露 priority/backpressure/overflow 信号，而不是把队列饱和留给实现层私有推断。

## 设计结论

1. `SchedulerTicket.h` 固定以下 supporting types：
   - `SchedulerPriorityClass`：明确区分 `ForegroundInteractive`、`Recovery`、`Maintenance`；
   - `SchedulerOverflowDisposition`：明确表达 `RejectNew`、`EnterFailedSafe`、`DropOldest` 三类溢出策略；
   - `SchedulerBackpressureSignal`：最小暴露 `ForegroundBusy`、`RecoveryFailedSafe`、`MaintenanceDropOldest`、`WorkerPoolSaturated`；
   - `WorkerLeaseBudget`：显式承载 worker 总预算与已占用数量；
   - `SchedulerTicketRequest` / `SchedulerTicket` / `SchedulerBackpressureState`。
2. `SchedulerTicket` 必须直接持有 `CancellationToken`，而不是只持有 deadline 值或 token id。这样 `enqueue()` 返回的 ticket 与后续 `acquire_worker()` 拿到的 worker ticket 可以共享同一取消状态，满足 RT-TC013 的 step-level 传播要求。
3. `SchedulerBackpressureState` 只暴露队列深度、各队列 limit、worker lease budget 和 dominant signal，不拥有失败解释权，不替代 `RecoveryManager` 或 `SafeModeController` 的裁定对象。
4. `IScheduler` 本轮固定以下最小 public contract：
   - `enqueue(const SchedulerTicketRequest&)`
   - `acquire_worker(const AcquireWorkerRequest&)`
   - `release_worker(const ReleaseWorkerRequest&)`
   - `backpressure_state() const`
5. `acquire_worker(...)` 通过 `WorkerLeaseBudget` 显式输入当前 worker 可用性，而不是让调用方隐式推断。这样接口面已经把“队列有任务但 worker 已饱和”的 backpressure 和“队列本身 overflow”区分开。
6. 本轮只冻结公共 include 面与 surface test，不实现 `runtime/src/scheduling/Scheduler.cpp`。真实队列、锁顺序和 `queue_mutex` 约束留给 RT-TODO-019 落地。

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `SchedulerTicketRequest` | 表达待入队任务与取消绑定点 | 不承担执行结果或业务语义 |
| `SchedulerTicket` | 表达已入队/已分配给 worker 的调度票据 | 不解释失败原因，不持有 workflow 状态机 |
| `SchedulerBackpressureState` | 暴露队列深度、worker 饱和与 dominant signal | 不直接进入 safe mode 或恢复路径 |
| `IScheduler` | 提供 ticket 生命周期与 backpressure 查询 | 不拥有主循环裁定权，不解释 RecoveryOutcome |

## 文件落点

| 设计项 | 文件 |
|---|---|
| `SchedulerPriorityClass` / `SchedulerOverflowDisposition` / `SchedulerBackpressureSignal` / `WorkerLeaseBudget` / `SchedulerTicket*` | `runtime/include/scheduling/SchedulerTicket.h` |
| `AcquireWorkerRequest` / `SchedulerEnqueueResult` / `AcquireWorkerResult` / `ReleaseWorker*` / `IScheduler` | `runtime/include/scheduling/IScheduler.h` |
| fake-based surface test | `tests/unit/runtime/SchedulerTest.cpp` |

## Design -> Build 映射

1. 代码目标：`runtime/include/scheduling/SchedulerTicket.h`、`runtime/include/scheduling/IScheduler.h`
2. 测试目标：`tests/unit/runtime/SchedulerTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_scheduler_surface_unit_test && ctest --test-dir build-ci -R "^SchedulerTest$" --output-on-failure`

## 验收点

| 验收点 | 说明 | 结果 |
|---|---|---|
| D1 | 固定前台/恢复/维护三类 priority class 与 overflow disposition | PASS |
| D2 | 固定 `IScheduler` 的 4 个最小 public 方法面 | PASS |
| D3 | `SchedulerTicket` 直接绑定 `CancellationToken`，可被 surface test 观察 | PASS |
| D4 | public seam 已显式暴露 queue depth 和 worker saturation backpressure | PASS |