# LOG-BLK-002 LoggingMetricsBridge 设计收敛

日期：2026-04-03  
任务：LOG-BLK-002  
状态：已解阻

## 1. blocker 分类

1. blocker 类型：Context blocker。
2. 根因不是 metrics 运行时缺失，而是 logging 侧缺少可执行的跨模块观测协议：虽然 metrics 已有 IMetricsProvider、IMeter、MetricLabels 与 MetricsErrorCode，但“logging 用哪条接口发射五个指标、标签怎么填、record 失败时如何不递归反噬 logging 主链”没有被正式冻结。

## 2. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 中 LOG-TODO-013 原状态为 Blocked，阻塞描述明确指向“指标出口接口、标签白名单、上报失败语义缺失”。
2. [infra/include/metrics/IMetricsProvider.h](infra/include/metrics/IMetricsProvider.h) 已冻结 get_meter(scope) / force_flush / shutdown，说明 logging 不需要再发明 provider 入口。
3. [infra/include/metrics/IMeter.h](infra/include/metrics/IMeter.h) 已冻结 create_counter/create_gauge/create_histogram/record，说明 logging 可直接依赖 meter + sample 语义。
4. [infra/include/metrics/MetricTypes.h](infra/include/metrics/MetricTypes.h) 已冻结 MetricLabels 五元组与 MetricSample 基础约束，但没有 logging 场景的取值收敛表。
5. [infra/include/metrics/MetricsErrors.h](infra/include/metrics/MetricsErrors.h) 已冻结 MET_E_* 码名与 contracts 映射，但没有 logging bridge 失败时的非递归降级语义。

## 3. 外部参考

1. OpenTelemetry Metrics API：MeterProvider 提供 Meter，Meter 负责创建 Instrument 并报告 Measurements；Meter 不拥有配置职责，适合 DASALL 当前的 provider -> meter -> sample 分层。
2. Prometheus metric naming：建议计数类使用 _total 后缀、避免高基数 label，并保持单一 quantity/unit，适合作为 logging 五个 frozen metric family 的命名和标签治理约束。

## 4. 设计收敛结论

1. LOG-TODO-013 不新增 logging 私有 IMetricSink，也不直连 IMetricExporter。v1 唯一发射路径冻结为：IMetricsProvider::get_meter(MeterScope{.name = "infra.logging", .version = "v1"}) -> IMeter::create_counter/create_gauge/create_histogram -> IMeter::record(MetricSample)。
2. logging v1 只允许发射五个 frozen 指标：logging_write_total、logging_write_fail_total、logging_drop_total、logging_queue_depth、logging_flush_latency_ms。
3. logging v1 只允许复用 MetricLabels 五元组，并把 label 取值限制为：

| 标签 | 冻结取值 |
|---|---|
| module | logging |
| stage | write、queue、flush、recovery |
| profile | active profile_id 或 unknown |
| outcome | success、failure、degraded |
| error_code | none、LOG_E_QUEUE_FULL、LOG_E_SINK_IO、LOG_E_FORMAT_INVALID、LOG_E_CONFIG_INVALID |

4. request_id、session_id、trace_id、task_id 等高基数标识不得进入 metric labels；这些标识只保留在日志/追踪信号。
5. bridge 失败语义冻结为 non-recursive degradation：instrument 创建失败或 record(sample) 失败只能把 LoggingMetricsBridge 标记为 degraded，保留 bridge-local failure state，并通过 health/audit 或非递归 failure hook 暴露，不得再通过 LoggingFacade 写一条“metrics failed”日志。
6. MetricsErrorCode::ProviderNotReady、ExportFailure、ExportTimeout 进入 best-effort degraded；QueueFull 只丢当前观测样本；IdentityInvalid/ConfigInvalid 使 bridge 回退为 no-op，不允许部分初始化继续运行。

## 5. Design -> Build 映射

| Design 结论 | Build 目标 |
|---|---|
| 只经由 provider/meter/sample 发射 | LoggingMetricsBridge 只依赖 IMetricsProvider/IMeter，不触碰 exporter 实现 |
| 五个 frozen 指标 family | bridge 预注册或懒注册五个 identity，并缓存 instrument 句柄 |
| MetricLabels 五元组有限取值 | 单测覆盖 success/failure/degraded 与 error_code 白名单 |
| non-recursive degraded failure | 负例测试覆盖 record 失败不改写原始 logging 主结果、bridge 进入 degraded |

## 6. D Gate

1. D Gate：PASS。
2. 原因：LOG-TODO-013 的接口入口、标签约束、失败语义与最小测试出口都已冻结，后续可以直接进入 bridge skeleton 实现，而不需要等待 metrics 运行时代码先落盘。

## 7. 下一步

1. 直接进入 LOG-TODO-013，实现 LoggingMetricsBridge 骨架、定向 unit/contract 测试与专项 TODO/worklog 回写。
2. metrics 子域真实 exporter/runtime 仍可后续独立推进，但不再阻塞 logging 本轮 bridge skeleton。