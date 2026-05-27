# INF-LOG-FIX-007 logging metrics / health main-chain 收口

关联任务：INF-LOG-FIX-007  
日期：2026-05-27  
结论级别：L2 build-tree main-chain wiring / live composition health registration

## 1. 收口结论

1. `LoggingFacade` 现已把 accepted / dropped / failed / flushed / queue_depth 主链结果接入 `LoggingMetricsBridge`，并将 `queue_high_watermark = max(1, active_logging_config.queue_size)`、`dropped_total_delta`、`fallback_active`、`recovery_degraded`、`unrecoverable_failure_total`、`metrics_bridge_degraded` 统一投影给 `LoggingHealthProbe`。
2. `compose_live_observability()` 现已在 logger、metrics provider 与 health monitor 均初始化成功后，fail-closed 注册 `probe_name=infra.logging.pipeline`、`probe_group=readiness` 的 logging probe；注册生命周期通过 `HealthProbeRegistration.keepalive` 固定，不再依赖裸指针悬挂。
3. `LoggingMetricsBridgeMainChainTest`、`LoggingHealthProbeLiveCompositionTest` 与扩展后的 `InfraHealthCadenceIntegrationTest` 已分别覆盖 main-chain 指标写入、live composition readiness/degraded 以及 health monitor healthy -> degraded transition 证据。

## 2. 代码回写

1. `infra/include/logging/LoggingMetricsBridge.h`、`infra/include/logging/LoggingHealthProbe.h`、`infra/src/logging/LoggingMetricsBridge.cpp`、`infra/src/logging/LoggingHealthProbe.cpp`
2. `infra/src/logging/LoggingFacade.h`、`infra/src/logging/LoggingFacade.cpp`
3. `infra/include/health/IHealthMonitor.h`、`infra/src/health/ProbeRegistry.h`、`infra/src/health/ProbeRegistry.cpp`
4. `infra/src/ObservabilityLiveComposition.cpp`
5. `tests/unit/infra/logging/LoggingMetricsBridgeMainChainTest.cpp`
6. `tests/integration/infra/logging/LoggingHealthProbeLiveCompositionTest.cpp`
7. `tests/integration/infra/health/InfraHealthCadenceIntegrationTest.cpp`

## 3. focused 验证

1. `ListBuildTargets_CMakeTools()` / `ListTests_CMakeTools()`
2. `Build_CMakeTools(buildTargets=["dasall_logging_metrics_bridge_main_chain_unit_test","dasall_logging_health_probe_live_composition_test","dasall_infra_health_cadence_integration_test"])`
3. `RunCtest_CMakeTools(tests=["LoggingMetricsBridgeMainChainTest","LoggingHealthProbeLiveCompositionTest","InfraHealthCadenceIntegrationTest"])`
   - 结果：命中仓库既有泛化 `生成失败`。
4. fallback 直接执行：
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_metrics_bridge_main_chain_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_health_probe_live_composition_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/health/dasall_infra_health_cadence_integration_test`
   - 结果：三项均通过；fallback sink 向 stderr 输出 degraded advisory / structured record 属预期证据，不代表测试失败。

## 4. 非外推边界

1. 本轮只闭合 build-tree / live composition 下的 logging metrics 与 health wiring，不把结论外推到 qemu / kvm。
2. installed/package authoritative logging proof、release soak 与 package smoke 继续留给 `INF-LOG-FIX-010` / `INF-LOG-FIX-011`，但后续 owner 验收仍以 installed authoritative evidence 为准，而不是回退到 qemu 口径。