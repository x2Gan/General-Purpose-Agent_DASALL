# AUD-BLK-004 AuditMetricsBridge 设计收敛

日期：2026-04-03  
任务：AUD-BLK-004  
状态：解阻 PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 在本轮前将 `AUD-TODO-015` 标记为 Blocked，根因明确为“metrics/health 桥接接口、标签白名单与上报失败语义未冻结”。
2. [docs/architecture/DASALL_infra_metrics模块详细设计.md](docs/architecture/DASALL_infra_metrics模块详细设计.md) 已冻结 `IMetricsProvider`、`IMeter`、`MetricSample`、`MetricLabels` 与 logging bridge 的既有分层，但在本轮前没有 audit 专属的 meter scope、指标对象表和标签白名单。
3. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.2/6.3/6.10 已经固定 `AuditMetricsBridge` 的职责与 audit 指标清单，但此前缺少 bridge 如何取 meter、允许哪些标签、bridge 失败怎样映射到 `AuditHealthStatus` 的收敛规则。
4. [infra/src/logging/LoggingMetricsBridge.h](infra/src/logging/LoggingMetricsBridge.h) 已经提供仓内现成先例：logging 通过单一 provider/meter 路径复用 metrics 子系统，而不是再造私有 sink，这说明 audit blocker 的本质是桥接协议未成文，而不是 metrics 基础设施缺失。

## 2. 外部参考

1. OpenTelemetry Metrics API 强调稳定的 instrumentation 应沿 `MeterProvider -> Meter -> Instrument` 分层创建，instrument identity 由固定的 meter scope、instrument name 与 unit 决定；这支持 audit v1 在 bridge 落盘前先冻结 meter scope、指标族与标签边界，避免运行期拼接动态 metric family。

## 3. 阻塞修复与设计结论

阻塞结论：

1. `AUD-BLK-004` 的真实缺口不是没有 metrics 子系统，而是 audit 侧没有把“如何接入 `IMetricsProvider` / `IMeter`、允许发哪些指标、允许带哪些标签、bridge 失败如何降级且不反噬主链”冻结成 v1 协议，导致 `AuditMetricsBridge` 无法安全进入实现轮。

最小 blocker-fix：

1. 在 metrics 详细设计中新增 audit v1 的桥接协议章节，冻结 meter scope、七指标对象表、五元标签白名单和 non-recursive failure semantics。
2. 在 audit 详细设计中补齐 `AuditMetricsBridge` 与 `AuditHealthStatus` 的对齐关系，明确 bridge degraded 只能把 health 推到 `Degraded`，不能直接宣称 `Unavailable`。
3. 在 audit 专项 TODO 中将 `AUD-TODO-015` 从 Blocked 迁移到 Not Started，并把 blocker 证据回链到设计文档与本交付件。

设计结论：

1. `AuditMetricsBridge` 只允许依赖 `metrics::IMetricsProvider` 与 `metrics::IMeter`，不得直连 exporter 或声明第二套 audit 私有 metrics sink。
2. meter scope 固定为 `infra.audit@v1`。
3. audit v1 只允许发射以下七个指标族：
   - `audit_write_total`
   - `audit_write_fail_total`
   - `audit_fallback_total`
   - `audit_fallback_fail_total`
   - `audit_export_total`
   - `audit_export_fail_total`
   - `audit_queue_depth`
4. 标签白名单固定为 `module/stage/profile/outcome/error_code`；`actor`、`target`、`request_id`、`trace_id`、`task_id`、`evidence_ref` 等高基数字段不得进入 metrics label。
5. bridge 失败语义冻结为：
   - provider/exporter 问题只把 bridge 标记为 degraded，不覆盖 audit write/export 主结果；
   - `QueueFull` 只丢弃本次观测样本；
   - `IdentityInvalid`/`ConfigInvalid` 让 bridge 回退为 no-op，并把 `metrics_bridge_degraded=true` 暴露给 `AuditHealthStatus`。
6. health 对齐关系冻结为：metrics bridge degraded 只能把 `AuditHealthStatus.state` 推到 `Degraded`；只有主写链与 fallback 链都无法继续承接时，audit 才允许升级到 `Unavailable`。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `IMetricsProvider`/`IMeter` 接入路径、meter scope 与七指标对象表 | infra/src/audit/AuditMetricsBridge.cpp |
| 冻结五元标签白名单与 no-op/degraded 回退语义 | infra/src/audit/AuditMetricsBridge.cpp |
| 冻结 bridge degraded 到 `AuditHealthStatus` 的对齐关系 | infra/include/audit/IAuditHealthProbe.h |
| 验证桥接失败不反噬主链、且 integration 侧能观测降级状态 | tests/integration/infra/InfraAuditHealthIntegrationTest.cpp |

## 5. 对 AUD-TODO-015 的直接交接

1. `AUD-TODO-015` 可以从 Blocked 转为 Not Started，并按已冻结协议直接落盘 `AuditMetricsBridge.cpp` 骨架。
2. 后续实现不得新增 audit 私有 `IMetricSink`、不得动态拼接 metric family，也不得把高基数字段塞进 `MetricLabels`。
3. integration 轮次只允许验证已冻结的标签与 degraded 语义，不得在实现轮反向扩张 v1 协议边界。

## 6. 风险与回退

1. 若后续 `IMeter`、`MetricLabels` 或 `MetricsErrorCode` 公共边界发生回退，本 blocker 需要重新转为 Blocked。
2. 若 audit bridge 试图直接吸收 exporter 配置、批处理或私有队列职责，应视为越界，回退到当前 provider/meter-only 协议。
3. 本轮只解阻设计，不提前落盘 `AuditMetricsBridge` 代码实现或 integration wiring；若实现轮顺手扩张 metrics facade/health 公共接口，应视为越界。
