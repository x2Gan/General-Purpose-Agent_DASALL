# INF-LOG-FIX-006 recovery / fallback 收口

关联任务：INF-LOG-FIX-006  
日期：2026-05-27  
结论级别：L3 build-tree degraded/fallback evidence

## 1. 本轮目标

把 `LoggingRecovery` 从 isolated fixture state machine 接到当前已冻结的 logging 主链：

1. `LoggingFacade` 在 formatter failure、direct dispatch failure、queue flush sink failure 与 queue saturation 四个 owner-safe 边界统一消费 `LoggingRecovery`。
2. primary sink failure 时切到 degraded fallback sink，保留 advisory signal，但不执行 Runtime recovery。
3. queue saturation 延续 deterministic queue contract，只补显式 dropped/advisory signal，不重写 `AsyncQueueController` 语义。
4. 保持 `INF-LOG-FIX-005` 已冻结的 live config projection 不变，不扩 public runtime policy schema。

## 2. 改动范围

1. `infra/src/logging/LoggingRecovery.h`
2. `infra/src/logging/LoggingRecovery.cpp`
3. `infra/src/logging/LoggingFacade.h`
4. `infra/src/logging/LoggingFacade.cpp`
5. `tests/unit/infra/logging/LoggingSinkFallbackTest.cpp`
6. `tests/unit/infra/logging/LoggingQueueFailureSignalTest.cpp`
7. `tests/integration/infra/logging/LoggingRecoveryIntegrationTest.cpp`
8. `tests/unit/CMakeLists.txt`
9. `tests/unit/infra/CMakeLists.txt`
10. `tests/integration/CMakeLists.txt`
11. `tests/integration/infra/logging/CMakeLists.txt`

## 3. 关键结论

1. `LoggingFacade` 现显式持有 `LoggingRecovery`，并把 formatter failure、direct dispatch failure、queue flush sink failure 与 queue saturation 全部接到 degraded fallback 路径。
2. default degraded fallback sink 现固定为 `ringbuffer + stderr`；当 primary sink 失败时，已结构化/已脱敏记录会 replay 到 fallback sink，`LoggingFacade` 也会保留 degraded/fallback state 与最近一次 recovery error code。
3. queue saturation 继续沿用 deterministic queue contract 的 `RuntimeRetryExhausted` 语义，但 `LoggingRecovery` 现会发出带 `LOG_E_QUEUE_FULL`、`recovery_advisory=queue_saturation` 与 `dropped_original_record=true` 的 degraded fallback advisory payload，显式区分“原始记录已 drop”和“advisory 已持久化”。
4. formatter failure 现在经由 `LoggingFacade` 进入 `LoggingRecovery::handle_format_failure()`；fallback payload 保留 pre-format message，但清空 formatter 生成 attrs，避免半成品 structured payload 混入成功路径。
5. 整个 recovery/fallback path 仍严格停留在 logging owner 内：不触发 Runtime recovery、不扩 `RuntimePolicySnapshot`、不改变 `INF-LOG-FIX-004` 已冻结的 queue/backpressure contract。

## 4. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_logging_recovery_integration_test","dasall_logging_sink_fallback_unit_test","dasall_logging_queue_failure_signal_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["LoggingRecoveryIntegrationTest","LoggingSinkFallbackTest","LoggingQueueFailureSignalTest"])`
   - 结果：命中仓库既有泛化 `生成失败`。
3. fallback 直接执行：
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_sink_fallback_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_queue_failure_signal_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_recovery_integration_test`
   - 结果：3/3 通过。
4. 相邻回归：
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_recovery_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_facade_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_flush_deadline_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_sink_failure_injection_test`
   - 结果：4/4 通过。

## 5. 非外推说明

1. 本轮只到 L3 build-tree degraded/fallback evidence；metrics/health、diagnostics artifact 与 installed package proof 继续留给 `INF-LOG-FIX-007~011`。
2. queue saturation advisory 不代表 queue contract 被重定义；它只是在 logging owner 内为已冻结的 backpressure result 补充显式 dropped/advisory signal。
3. 本轮未使用 qemu / kvm，也没有把 build-tree degraded/fallback 结果外推为 installed / package / release ready 证据。