# RT-TODO-019 Scheduler 设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 的 6.24.10 已把 `Scheduler` 固定为有限调度控制器，只负责 `enqueue(...)`、`acquire_worker(...)`、`release_worker(...)` 和 `backpressure_state()`，不拥有工作流逻辑。
2. 同一文档的 6.14.4 已冻结三类队列的深度与溢出策略：前台队列深度 1 且 busy 时拒绝新请求；恢复队列超限时进入 FailedSafe；后台维护队列深度 16，超限时丢弃最旧任务。
3. 同一文档的 6.14.2 已声明 `Scheduler::queue_mutex` 为 L5 锁，说明 019 需要显式落地互斥保护，但不能在控制器内部额外引入更高层锁。
4. `runtime/include/scheduling/IScheduler.h` 与 `SchedulerTicket.h` 已冻结 `SchedulerTicketRequest`、`SchedulerTicket`、`WorkerLeaseBudget` 与 `SchedulerBackpressureState`，说明 019 只应实现 runtime-private 控制器，不改 public ABI。
5. `tests/unit/runtime/SchedulerTest.cpp` 当前 fake 已覆盖前台拒绝、恢复 FailedSafe、维护 drop-oldest、worker saturation 与 `CancellationToken` 绑定传播，是 019 的直接实现蓝图。

## 外部参考

1. Microsoft Azure Architecture Center 的 Queue-Based Load Leveling 模式强调：排队层应显式吸收突发流量，并把 overflow / throttling 设计成可观察的策略，而不是隐式副作用。
2. 同一模式指出任务入口与执行资源应解耦，这支持 DASALL 在 `Scheduler` 内部分离“队列溢出背压”和“worker 饱和背压”两类信号，而不是混成单一 busy 标志。

## 设计结论

1. `Scheduler` 作为 runtime 私有控制器落在：
   - `runtime/src/scheduling/Scheduler.h`
   - `runtime/src/scheduling/Scheduler.cpp`
2. 控制器内部使用：
   - `std::mutex queue_mutex_` 保护前台、恢复、维护三类队列；
   - `std::deque<SchedulerTicket>` 作为三类队列承载；
   - `WorkerLeaseBudget worker_budget_` 维护当前 worker 占用快照；
   - `std::uint64_t next_sequence_` 生成稳定 enqueue 顺序。
3. `enqueue(...)` 的固定语义为：
   - `ticket_id` / `request_id` 缺失直接拒绝；
   - 前台队列深度达到 1 时返回 `RejectNew + ForegroundBusy`；
   - 恢复队列深度达到 `recovery_queue_limit_` 时返回 `EnterFailedSafe + RecoveryFailedSafe`；
   - 维护队列深度达到 `maintenance_queue_limit_` 时先丢弃最旧任务，再接受新 ticket，并返回 `DropOldest + MaintenanceDropOldest`。
4. `acquire_worker(...)` 的固定语义为：
   - 优先消费显式 `preferred_ticket_id`；
   - 否则按 `preferred_priority_class` 或默认优先级 `Foreground -> Recovery -> Maintenance` 出队；
   - 若 `worker_budget.has_capacity()==false`，返回 `WorkerPoolSaturated`；
   - 成功获取 worker 后，为 ticket 绑定 `assigned_worker_id` 并把状态推进到 `WorkerAssigned`。
5. `release_worker(...)` 的固定语义为：
   - 没有 `assigned_worker_id` 的 ticket 拒绝释放；
   - 有效 ticket 会回收 `busy_workers`，并把 backpressure state 恢复为释放后的真实视图。
6. `Scheduler` 不负责：
   - 决定 safe mode / recovery 终态；
   - 执行 worker 本体逻辑；
   - 解释 checkpoint、session 或 workflow 语义。
7. 019 只落地 runtime-local 控制器；profile 的真实 `max_replan_count` 注入留给 021 或更高层装配阶段，因此构造函数提供可测试的 queue limit 参数，默认值保持与 011 surface test 一致。

## 边界与职责表

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `Scheduler::enqueue` | 依据 priority class 执行入队与 overflow 策略 | 不决定恢复/安全模式终态 |
| `Scheduler::acquire_worker` | 选择可执行 ticket，并绑定 worker | 不执行具体 worker 任务 |
| `Scheduler::release_worker` | 回收 worker budget，恢复背压视图 | 不解释任务成功/失败语义 |
| `Scheduler::backpressure_state` | 输出当前队列与 worker 饱和快照 | 不写审计或事件总线 |

## 文件落点

| 设计项 | 文件 |
|---|---|
| 私有类声明 | `runtime/src/scheduling/Scheduler.h` |
| 私有类实现 | `runtime/src/scheduling/Scheduler.cpp` |
| 行为测试 | `tests/unit/runtime/SchedulerTest.cpp` |
| CMake 接线 | `runtime/CMakeLists.txt`、`tests/unit/runtime/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`runtime/src/scheduling/Scheduler.h`、`runtime/src/scheduling/Scheduler.cpp`
2. 测试目标：`tests/unit/runtime/SchedulerTest.cpp`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_scheduler_surface_unit_test && ctest --test-dir build-ci -R "^SchedulerTest$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 锁定三类队列与 L5 `queue_mutex` 的内部承载方式 | 前台/恢复/维护队列与锁顺序明确 | PASS |
| D2 | 锁定 overflow 与 worker saturation 的分离语义 | `RejectNew` / `EnterFailedSafe` / `DropOldest` / `WorkerPoolSaturated` 可二值断言 | PASS |
| D3 | 锁定 `CancellationToken` 在 ticket 生命周期中的共享语义 | enqueue 后 cancel，acquire 到的 ticket 仍能观测取消状态 | PASS |
| D4 | 锁定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 设计交付物已落盘。
2. 范围未越出 019 控制器边界。
3. Build 三件套已锁定。
4. 019 不受额外 blocker 约束，可直接进入 Build。

结论：D Gate = PASS，可进入 RT-TODO-019 的 Build 阶段。