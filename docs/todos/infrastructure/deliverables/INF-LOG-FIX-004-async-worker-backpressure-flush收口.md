# INF-LOG-FIX-004 async worker / backpressure / flush deadline 收口

关联任务：INF-LOG-FIX-004  
日期：2026-05-27  
结论级别：L2/L3 build-tree focused evidence

## 1. 本轮目标

把 logging queue 从纯 bookkeeping 推进到 deterministic 行为闭合：

1. `AsyncQueueController` 拥有真实 single worker drain 行为。
2. `flush(deadline)` 对成功、超时、worker stuck 给出确定性返回。
3. block policy 在 worker 忙碌时能稳定触发 backpressure。
4. `LoggingFacade::stop()` 把 shutdown drain 变成 public lifecycle contract。

## 2. 改动范围

1. `infra/src/logging/AsyncQueueController.h`
2. `infra/src/logging/AsyncQueueController.cpp`
3. `infra/src/logging/SinkDispatcher.h`
4. `infra/src/logging/SinkDispatcher.cpp`
5. `infra/src/logging/LoggingFacade.cpp`
6. `tests/unit/infra/logging/AsyncQueueControllerTest.cpp`
7. `tests/unit/infra/logging/AsyncQueueControllerWorkerTest.cpp`
8. `tests/unit/infra/logging/LoggingFlushDeadlineTest.cpp`
9. `tests/unit/infra/logging/LoggingBackpressureTest.cpp`
10. `tests/unit/infra/logging/LoggingFacadeTest.cpp`
11. `tests/integration/infra/logging/SinkDispatcherRouteIntegrationTest.cpp`
12. `tests/integration/infra/logging/LoggingSinkFailureInjectionTest.cpp`
13. `tests/unit/CMakeLists.txt`
14. `tests/unit/infra/CMakeLists.txt`

## 3. 关键结论

1. `AsyncQueueController` 现支持显式 `start()` / `stop()` 与 single worker drain，并提供 `processed_total`、`flush_timeout_total`、`blocked_write_attempt_total`、`dropped_total` 等单调计数。
2. queue 容量现把 single worker in-flight slot 一并计入占用，因此 worker 卡住时，block policy 会稳定拒绝后续 record，而不是只剩 bookkeeping。
3. `SinkDispatcher` 在注入 real sinks 时把 sink write 移入 queue worker callback；未注入 sink 时继续保留 skeleton 行为，避免普通 tests 产生文件副作用。
4. `LoggingFacade::flush()` 和 `LoggingFacade::stop()` 现在都对 shutdown drain 给出确定性结果：超时显式失败，drain 成功才进入 stopped。

## 4. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_async_queue_controller_unit_test","dasall_async_queue_controller_worker_unit_test","dasall_logging_flush_deadline_unit_test","dasall_logging_backpressure_unit_test","dasall_logging_facade_unit_test","dasall_sink_dispatcher_unit_test","dasall_sink_dispatcher_route_integration_test","dasall_logging_sink_failure_injection_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["AsyncQueueControllerTest","AsyncQueueControllerWorkerTest","LoggingFlushDeadlineTest","LoggingBackpressureTest","LoggingFacadeTest","SinkDispatcherTest","SinkDispatcherRouteIntegrationTest","LoggingSinkFailureInjectionTest"])`
   - 结果：命中仓库既有泛化 `生成失败`。
3. fallback 直接执行：
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_async_queue_controller_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_async_queue_controller_worker_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_flush_deadline_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_backpressure_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_facade_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_sink_dispatcher_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_sink_dispatcher_route_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_sink_failure_injection_test`
   - 结果：8/8 通过。

## 5. 非外推说明

1. 本轮只到 L2/L3 build-tree evidence；recovery/fallback、metrics/health、config live projection 与 installed package proof 继续留给 `INF-LOG-FIX-005~011`。
2. async sink failure 目前只保证 deterministic observation，不在本轮引入 fallback/recovery owner 行为；那部分仍由 `INF-LOG-FIX-006` 负责。
3. qemu / kvm 仍不属于当前 logging owner 验收前置。