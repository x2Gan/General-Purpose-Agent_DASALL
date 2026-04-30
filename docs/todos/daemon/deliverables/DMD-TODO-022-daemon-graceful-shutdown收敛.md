# DMD-TODO-022 daemon graceful shutdown 收敛

完成时间：2026-04-30

## 任务结论

DMD-TODO-022 已完成，graceful shutdown 的三个验收点已满足：

1. Draining 阶段拒绝新请求：`DaemonLifecycleController::begin_request()` 仅在 `Ready` 状态允许进入 inflight。
2. inflight 在窗口内排空：`DaemonBootstrap::stop(timeout)` 调用 `DaemonLifecycleController::shutdown(timeout)`，对 inflight 进行等待。
3. 超时 abandoned 审计事实可观测：`AccessGateway::shutdown(timeout)` 在超时且仍有 inflight 时通过 shutdown observer 上报 abandoned 数；`AccessObservabilityBridge::emit_shutdown_abandoned()` 固定事件名与字段。

## 代码与测试对照

1. `apps/daemon/src/DaemonLifecycleController.cpp`
   - `shutdown(timeout)` 在 `Draining` 下等待 inflight 清零；超时返回 `abandoned_requests`。
2. `apps/daemon/src/DaemonBootstrap.cpp`
   - `stop(timeout)` 触发 listener 关闭、gateway shutdown 与 lifecycle shutdown 的有序收口。
3. `access/src/AccessGateway.cpp`
   - shutdown 超时时调用 observer 上报 abandoned inflight 计数。
4. `access/src/AccessObservabilityBridge.cpp`
   - `emit_shutdown_abandoned()` 输出 `daemon.shutdown.abandoned` 事件及关键字段。

## 验收

1. `RunCtest_CMakeTools(tests=["DaemonGracefulShutdownTest","AccessGatewayLifecycleTest","DaemonShutdownAbandonedAuditTest"])`
   - 结果：通过（3/3）。

## 完成判定对照

1. Draining 拒绝新请求：已满足。
2. inflight 在窗口内排空：已满足。
3. 超时 abandoned 有审计事实：已满足。
4. daemon 不做业务恢复裁定：已满足（仅做生命周期排空与关闭，不新增恢复决策逻辑）。
