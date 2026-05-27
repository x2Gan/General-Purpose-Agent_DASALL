# BLK-INF-LOG-006 logging metrics / health threshold 冻结

关联任务：BLK-INF-LOG-006  
日期：2026-05-27  
结论级别：L1 design / SSOT freeze

## 1. blocker 结论

`BLK-INF-LOG-006` 的真实缺口不是 `LoggingMetricsBridge` / `LoggingHealthProbe` 是否已有骨架，而是以下 owner 口径仍停留在散落设计和历史 deliverable，没有进入当前 acceptance SSOT：

1. logging v1 到底固定哪几个 metric family；redacted write path 是否会扩成第六指标族。
2. `module/stage/profile/outcome/error_code` 的 label cardinality 是否已经冻结。
3. `queue_high_watermark`、`dropped_total_delta`、`unrecoverable_failure_total` 分别怎样映射到 `Degraded` / `Unhealthy`。
4. `compose_live_observability()` 是否必须把 logging probe 注册到 `HealthMonitorFacade`，以及注册失败是否 fail-closed。

本轮冻结后，后续 `INF-LOG-FIX-007` 必须统一遵守以下口径：

1. v1 main chain 只允许沿五个 frozen metric family 发射样本：`logging_write_total`、`logging_write_fail_total`、`logging_drop_total`、`logging_queue_depth`、`logging_flush_latency_ms`。
2. redacted write path 只能记入已脱敏后的 accepted write sample，不新增 redaction 专用第六指标族。
3. label cardinality 固定为 `module=logging`、`stage in {write,queue,flush,recovery}`、`outcome in {success,failure,degraded}`、`error_code in {none,LOG_E_QUEUE_FULL,LOG_E_SINK_IO,LOG_E_FORMAT_INVALID,LOG_E_CONFIG_INVALID}`，`profile` 缺失固定回填 `unknown`。
4. `queue_high_watermark = max(1, active_logging_config.queue_size)`；direct-dispatch path 继续固定 `queue_high_watermark=1` 且 `queue_depth=0`。
5. `fallback_active=true`、`recovery_degraded=true`、`metrics_bridge_degraded=true`、`dropped_total_delta >= 1` 或 `queue_depth >= queue_high_watermark` 一律映射为 `Degraded`；`unrecoverable_failure_total >= 1` 才允许映射为 `Unhealthy`。
6. `compose_live_observability()` 必须在 logger、metrics provider 与 health monitor 均初始化成功后注册 `probe_name=infra.logging.pipeline`、`probe_group=readiness` 的 logging probe；注册失败必须 fail-closed，而不是静默跳过。

## 2. 回写位置

1. `docs/ssot/LoggingProductionAcceptanceMatrix.md`
2. `docs/architecture/DASALL_infra_logging模块详细设计.md`
3. `docs/todos/DASALL_子系统查漏补缺专项记录.md`
4. `tests/contract/smoke/LoggingProductionAcceptanceContractTest.cpp`

## 3. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_logging_production_acceptance_contract_test"])`
2. `RunCtest_CMakeTools(tests=["LoggingProductionAcceptanceContractTest"])`
3. 命中仓库既有泛化 `生成失败` 后，fallback 直接执行 `./build/vscode-linux-ninja/tests/contract/dasall_logging_production_acceptance_contract_test`

## 4. 解阻结果

`BLK-INF-LOG-006` 现可关闭；后续 `INF-LOG-FIX-007` 可以只聚焦 main-chain metrics/health wiring 与 live composition 注册，不再重新争论 metric family、label cardinality 或 degraded/unhealthy threshold。