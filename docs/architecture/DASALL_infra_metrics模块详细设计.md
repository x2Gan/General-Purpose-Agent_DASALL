# DASALL infra/metrics 模块详细设计（Detailed Design）

版本：v1.0  
日期：2026-03-24  
阶段：Detailed Design  
模块：infra/metrics

## 1. 模块概览

### 1.1 模块定位

infra/metrics 属于 Infrastructure Layer（Layer 1），负责提供统一指标采集、聚合、导出与告警信号桥接能力，是日志/追踪/审计闭环中的指标信号层。

模块目标：
1. 为 runtime、cognition、tools、memory、knowledge、services、multi_agent、apps 提供统一指标接口与语义约束。
2. 提供低开销、可裁剪、可降级的指标管线，支持 x86 与 ARM 场景。
3. 与 infra/logging、infra/tracing、infra/health、infra/config 保持职责解耦但语义关联。

来源依据：
1. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、5.10、8.7、9.5）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.1、4.2、6）
3. docs/architecture/DASALL_infrastructure子系统详细设计.md
4. docs/architecture/DASALL_infra_logging模块详细设计.md
5. docs/architecture/DASALL_infra_tracing模块详细设计.md

### 1.2 设计范围

纳入范围：
1. metrics 子组件、接口语义、对象模型、主/异常流程、配置与可观测。
2. Design -> Build 映射、实施分解、测试与 Gate、兼容演进建议。

不纳入范围：
1. runtime 主状态机与恢复裁定实现细节。
2. contracts 共享语义对象改写。
3. 完整 OTel SDK 强绑定实现（仅做兼容预留）。

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| MET-C001 | DASSALL_Agent_architecture.md 3.4.7/5.10 | Must | infra 必须提供 metrics 能力并与 logging/trace/audit 协同 | 子组件、接口 |
| MET-C002 | DASSALL_Agent_architecture.md 3.7 + Blueprint 4.2 | Must | 依赖方向单向：infra 不反向依赖业务模块实现 | 依赖关系 |
| MET-C003 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | 禁止 infra -> runtime/cognition/tools/memory/knowledge/services/multi_agent 业务实现依赖 | include/CMake |
| MET-C004 | DASALL_Engineering_Blueprint.md 4.3 | Must | 跨模块调用必须通过 contracts 或稳定接口，禁止直连实现类 | 接口语义 |
| MET-C005 | ADR-005 | Must | contracts 与主控边界冻结后推进模块设计，不反向改写既有评审结论 | 设计治理 |
| MET-C006 | ADR-006 | Must-Not | metrics 不拥有语义上下文装配与 Prompt 渲染职责，仅消费观测信号 | 边界职责 |
| MET-C007 | ADR-007 | Must-Not | metrics 不负责失败语义判定与恢复准入，只记录失败统计与恢复证据指标 | 异常职责 |
| MET-C008 | ADR-008 | Must | metrics 不拥有全局调度权，只记录 orchestrator/coordinator/worker 协同观测 | 多 Agent 边界 |
| MET-C009 | contracts 冻结计划第5/6章 | Must-Not | 不把实现细节（聚合窗口、桶策略、导出协议）写入 contracts 共享对象 | contracts 一致性 |
| MET-C010 | contracts 冻结 TODO 总表 M5 | Must | 以 Contracts V1 Ready 为输入，新增字段优先 optional，保持兼容优先 | 版本演进 |
| MET-C011 | 工程规范 3.6 | Must | 指标链路错误不能吞没，必须转化为日志/审计/降级计数 | 错误语义 |
| MET-C012 | 工程规范 3.7 | Should | 新增公共接口需同步 unit/contract/integration 测试 | 测试门禁 |
| MET-C013 | Blueprint 5.1/架构 7.5.1 | Must | Profile 只能裁剪能力与替换实现，不得绕过 Audit 与 Runtime 主控链路 | 配置策略 |
| MET-C014 | OTel Metrics Spec | Should | API 与 SDK 解耦，保留 no-op 与可插拔 Reader/Exporter 能力 | 架构形态 |
| MET-C015 | Prometheus Practices | Should | 指标命名含单位/类型后缀，限制高基数标签，直方图桶围绕 SLO 设计 | 指标治理 |
| MET-C016 | Google SRE Monitoring | Should | 优先覆盖四大金指标（延迟、流量、错误、饱和度）并控制告警噪声 | 指标体系 |

### 2.2 约束抽取结论

Must：
1. 保证 infra/metrics 为基础设施层能力，不反向依赖业务实现。
2. 保证 metrics 与 logging/tracing/audit 协同、错误可观测、Profile 可裁剪。
3. 保证 contracts 兼容优先，不引入破坏式语义扩写。

Should：
1. 兼容 OTel 语义与 Prometheus 运行实践。
2. 以四大金指标为优先观测集合，控制标签基数和告警噪声。

Must-Not：
1. 不改写已冻结 ADR 结论。
2. 不把实现细节写回 contracts。
3. 不越权到 runtime/cognition 控制职责域。

---

## 3. 现状与缺口

### 3.1 模块实现状态

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| infra/metrics 目录可编译可运行 | 部分实现 | 公共接口与对象已冻结并通过编译测试，运行时实现目录仍为空 | High | P0 |
| metrics 对外接口（IMetrics*） | 部分实现 | infra/include/metrics 已落盘 IMeter/IMetricsProvider/IMetricExporter 等接口，调用面已冻结 | High | P0 |
| 计数器/仪表/直方图采样 | 部分实现 | MetricTypes、IMeter 与 provider 接口已冻结，采样器与聚合器实现仍待补齐 | High | P0 |
| 指标导出（pull/push） | 部分实现 | IMetricExporter 已冻结，reader/exporter 运行时实现仍待补齐 | High | P0 |
| 配置与 profile 策略 | 部分实现 | IMetricConfigPolicy 已落盘，窗口/桶/标签白名单执行链仍待补齐 | Medium | P1 |
| 降级与恢复策略 | 部分实现 | MetricsErrors 与 IMetricsHealthProbe 已冻结，运行时兜底路径仍待实现 | High | P0 |
| 测试基线（unit/integration/failure） | 部分实现 | unit/contract 基线已接入，integration/failure 仍待补齐 | Medium | P0 |
| 与 logging/tracing/health 协同 | 缺失 | 三信号联动未建立 | Medium | P1 |

证据：
1. infra/src/metrics/ 为空目录。
2. infra/include/metrics 已落盘 IMeter、IMetricsProvider、IMetricExporter、IMetricConfigPolicy、IMetricsHealthProbe 与对象头文件。
3. infra/CMakeLists.txt 已接入 core/audit/plugin/tracing 等真实源码。
4. infra 当前不再依赖 placeholder-only 构建；metrics 缺口集中在 metrics 实现目录。

### 3.2 风险冲突识别

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | metrics 若直接采集 runtime 内部私有状态，将形成跨层实现耦合 | 破坏依赖方向与替换能力 | High |
| 语义重复 | 若在 metrics 重定义 ErrorInfo/Observation 语义 | 与 contracts V1 冲突、回归成本高 | High |
| 依赖反转 | 业务模块直接依赖具体 exporter | 失去 Profile 裁剪与跨平台可替换性 | Medium |
| 高基数失控 | 把 user_id/request_text 等动态值作为 label | 时间序列爆炸、内存压力上升 | High |

---

## 4. 候选方案对比

### 4.1 候选方案

1. 方案 A：最小同步计数方案（内存计数 + 周期日志打印）。
2. 方案 B：分层可插拔指标方案（API/SDK 分离 + 本地聚合 + 可插拔导出）。
3. 方案 C：OTel SDK 全量优先方案（直接深度绑定 OTel SDK + OTLP）。

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 最小同步计数 | 中 | 中 | 低 | 高并发阻塞、无跨信号关联、演进困难 | 淘汰：仅 PoC 可用 |
| B 分层可插拔 | 高 | 高 | 中 | 需要治理标签与导出队列参数 | 保留并采纳 |
| C OTel 全量优先 | 中高 | 高 | 高 | 引入成本高，edge 场景负担大，当前骨架准备不足 | 暂不采纳（v2 预留） |

### 4.3 行业实践映射

1. OTel Metrics：强调 API/SDK 分离、Reader/Exporter 插件化、no-op 能力，适合当前“先冻结接口再分阶段实现”的工程节奏。
2. Prometheus 指标实践：强调命名单位后缀、低基数标签、直方图可聚合，适合跨实例聚合与 SLO 计算。
3. Google SRE 监控原则：优先四大金指标 + 简单可解释告警，适合 DASALL 长稳运行与低噪声运维要求。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：分层可插拔指标方案（API/SDK 分离 + 本地聚合 + 可插拔导出），并保留 OTel 兼容字段与 exporter 扩展点。

### 5.2 放弃其他方案理由

1. 放弃 A：无法满足长期运行与跨模块联动观测要求。
2. 放弃 C：当前工程骨架阶段尚不足以承载全量 OTel 依赖，边缘档位成本过高。

### 5.3 一致性说明

1. 与架构一致：满足 Infrastructure Layer 的职责，不拥有业务控制权。
2. 与 ADR 一致：不侵入 context/prompt、reflection/recovery、orchestrator/coordinator 边界。
3. 与 contracts 冻结一致：只消费 V1 语义对象，不反向写入实现细节。

---

## 6. 详细设计

### 6.1 职责边界

metrics 职责：
1. 提供统一指标采集 API（Counter/Gauge/Histogram/UpDownCounter）。
2. 提供聚合、降采样、导出与故障降级能力。
3. 提供指标治理（命名、标签白名单、高基数防护、SLO 桶策略）。
4. 向 health/logging/audit 输出可观测结果。

metrics 非职责：
1. 不承担业务失败语义判定。
2. 不承担恢复动作准入与执行控制。
3. 不修改 contracts 共享语义对象。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| MetricsFacade | 模块统一入口，封装仪表注册与写入接口 |
| InstrumentRegistry | 管理 Instrument 定义、唯一性与生命周期 |
| MetricContextEnricher | 注入稳定维度标签（module/profile/stage 等） |
| CardinalityGuard | 标签白名单与高基数拦截/降级 |
| AggregationEngine | 执行 Counter/Gauge/Histogram 聚合与窗口滚动 |
| HistogramBucketPolicy | 管理 SLO 驱动的桶配置 |
| MetricReaderScheduler | 周期读取快照并触发导出 |
| MetricsExporterAdapter | 可插拔导出器（noop/prometheus_text/otlp 预留） |
| MetricsHealthProbe | 输出 metrics 链路健康状态 |
| MetricsAuditBridge | 记录关键治理事件（配置变更/降级切换） |
| MetricsLoggingBridge | 关键故障写入结构化日志 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| MetricsFacade | 上游模块采样调用 | Registry/AggregationEngine | 返回可判定状态码 |
| InstrumentRegistry | Instrument 定义请求 | InstrumentHandle | 同名同语义唯一 |
| MetricContextEnricher | 上下文与原始标签 | EnrichedLabels | 缺失标签填 unknown |
| CardinalityGuard | 标签集合 | 通过/拒绝/降级结果 | 拒绝必须可观测 |
| AggregationEngine | 采样事件流 | 聚合快照 | 线程安全、无锁优先 |
| HistogramBucketPolicy | 配置与 profile | bucket 边界 | 桶变更可审计 |
| MetricReaderScheduler | 时间触发 | 导出批次 | 支持 flush/shutdown |
| MetricsExporterAdapter | 导出批次 | 本地端点/远程出口 | 导出失败可回退 |
| MetricsHealthProbe | 队列深度、失败计数 | HealthSnapshot | 给出 ready/degraded |
| MetricsAuditBridge | 策略变更/连续失败 | AuditEvent | 不可静默丢失 |
| MetricsLoggingBridge | 关键错误 | LogEvent | 含 trace_id/request_id |

### 6.4 子组件依赖关系

1. MetricsFacade -> InstrumentRegistry -> AggregationEngine。
2. MetricsFacade -> MetricContextEnricher -> CardinalityGuard -> AggregationEngine。
3. AggregationEngine -> MetricReaderScheduler -> MetricsExporterAdapter。
4. AggregationEngine/Exporter -> MetricsHealthProbe + MetricsLoggingBridge + MetricsAuditBridge。
5. HistogramBucketPolicy 由 ConfigCenter 驱动并作用于 AggregationEngine。

依赖约束：
1. metrics 内部仅依赖 infra 稳定接口与 contracts 基础错误语义。
2. 不依赖 runtime/cognition/tools 等上层实现类。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| MetricIdentity | name, type, unit, description | name 必须唯一且含单位后缀策略 | 不扩写 contracts |
| MetricSample | identity_ref, value, ts, labels | value 与 type 匹配；ts 单调 | 消费 request/session/task 等标识语义 |
| MetricLabels | module, stage, profile, outcome, error_code | 限白名单；高基数拦截 | 对齐 ResultCode/阶段语义引用 |
| HistogramConfig | buckets, temporality, max_scale | 桶边界单调递增 | 不写入 contracts |
| ExportBatchReport | success_count, fail_count, latency_ms, dropped_count | 失败必须可观测 | 供 health/logging/audit 消费 |
| MetricsModuleSnapshot | queue_depth, guard_reject_total, exporter_state, degraded | 周期生成 | 与 runtime 状态解耦 |

### 6.6 核心接口语义定义

建议头文件路径：infra/include/metrics/

1. IMetricsProvider
- init(config): 初始化 provider 与聚合器。
- get_meter(scope): 获取 meter 句柄。
- force_flush(timeout_ms): 强制导出。
- shutdown(timeout_ms): 关闭并返回状态。

2. IMeter
- create_counter(identity)
- create_gauge(identity)
- create_histogram(identity)
- record(sample)

3. IMetricExporter
- export_batch(batch): 导出指标批次。
- force_flush(timeout_ms)
- shutdown(timeout_ms)

4. IMetricConfigPolicy
- validate_identity(identity)
- normalize_labels(labels)
- should_accept(labels)

5. IMetricsHealthProbe
- snapshot(): 返回 metrics 健康快照。

错误语义（metrics 私有错误码域，向上映射 contracts::ResultCode）：
1. MET_E_PROVIDER_NOT_READY
2. MET_E_IDENTITY_INVALID
3. MET_E_LABEL_CARDINALITY_EXCEEDED
4. MET_E_QUEUE_FULL
5. MET_E_EXPORT_FAILURE
6. MET_E_EXPORT_TIMEOUT
7. MET_E_CONFIG_INVALID

前置条件：
1. init 成功。
2. identity 注册通过。

后置条件：
1. 样本写入成功，或返回可判定错误并更新失败计数。

### 6.6.1 跨模块指标桥接协议（logging v1）

为解掉 logging 侧 LOG-BLK-002，冻结 logging -> metrics 的最小桥接协议如下：

1. LoggingMetricsBridge 只允许依赖 IMetricsProvider 与 IMeter；不得直连 IMetricExporter、导出配置对象或 exporter 内部队列。
2. meter 获取路径固定为 get_meter(MeterScope{.name = "infra.logging", .version = "v1"})；provider 继续负责配置与生命周期，保持与 OpenTelemetry MeterProvider -> Meter -> Instrument 的分层一致。
3. logging v1 指标只允许通过 create_counter/create_gauge/create_histogram + record(sample) 建立和发射，不新增 logging 私有 IMetricSink 或第二套 bridge 接口。

logging v1 指标对象冻结表：

| 指标名 | Instrument 类型 | unit | 创建接口 | 说明 |
|---|---|---|---|---|
| logging_write_total | Counter | 1 | IMeter::create_counter | 普通写入成功总数 |
| logging_write_fail_total | Counter | 1 | IMeter::create_counter | 普通写入失败总数 |
| logging_drop_total | Counter | 1 | IMeter::create_counter | 队列溢出或降级丢弃总数 |
| logging_queue_depth | Gauge | 1 | IMeter::create_gauge | 当前异步队列深度 |
| logging_flush_latency_ms | Histogram | ms | IMeter::create_histogram | flush 延迟分布 |

logging bridge 标签填充规则：

| 标签 | 取值规则 | 约束 |
|---|---|---|
| module | 固定为 logging | 不允许复用其他模块名 |
| stage | 仅允许 write、queue、flush、recovery | 不允许透传 sink 名称、文件路径等动态值 |
| profile | 当前 active profile_id；缺失填 unknown | 不暴露部署细节自由文本 |
| outcome | 仅允许 success、failure、degraded | 与桥接失败/降级语义对齐 |
| error_code | 仅允许 none、LOG_E_QUEUE_FULL、LOG_E_SINK_IO、LOG_E_FORMAT_INVALID、LOG_E_CONFIG_INVALID | 不允许透传 errno、路径或 request/session/trace 等高基数标识 |

补充约束：

1. LoggingMetricsBridge 不得把 request_id、session_id、trace_id、task_id 作为 metric label；这些标识只保留在日志/追踪信号中。
2. logging_write_total、logging_write_fail_total、logging_drop_total 保持 _total 后缀；logging_flush_latency_ms 保持当前 v1 命名冻结，若后续 Prometheus exporter 需要 seconds 基准换算，只能在 exporter 适配层完成，不回改 bridge API。
3. MetricSample.identity_ref 的 name、type、unit 必须与上表一致；运行期不允许按 sink/path 动态改 metric family。

### 6.6.2 跨模块指标桥接协议（audit v1）

为解掉 audit 侧 `AUD-BLK-004`，冻结 audit -> metrics 的最小桥接协议如下：

1. `AuditMetricsBridge` 只允许依赖 `IMetricsProvider` 与 `IMeter`；不得直连 `IMetricExporter`、exporter 配置对象或 metrics 内部队列。
2. meter 获取路径固定为 `get_meter(MeterScope{.name = "infra.audit", .version = "v1"})`；provider 继续负责配置与生命周期，保持与 OpenTelemetry `MeterProvider -> Meter -> Instrument` 的分层一致。
3. audit v1 指标只允许通过 `create_counter`/`create_gauge` + `record(sample)` 建立和发射，不新增 audit 私有 `IMetricSink` 或第二套 bridge 接口。

audit v1 指标对象冻结表：

| 指标名 | Instrument 类型 | unit | 创建接口 | 说明 |
|---|---|---|---|---|
| audit_write_total | Counter | 1 | IMeter::create_counter | 审计写入成功总数 |
| audit_write_fail_total | Counter | 1 | IMeter::create_counter | 审计写入失败总数 |
| audit_fallback_total | Counter | 1 | IMeter::create_counter | fallback 承接成功总数 |
| audit_fallback_fail_total | Counter | 1 | IMeter::create_counter | fallback 承接失败总数 |
| audit_export_total | Counter | 1 | IMeter::create_counter | 导出成功总数 |
| audit_export_fail_total | Counter | 1 | IMeter::create_counter | 导出失败总数 |
| audit_queue_depth | Gauge | 1 | IMeter::create_gauge | 当前 audit 队列/存储保留深度 |

audit bridge 标签填充规则：

| 标签 | 取值规则 | 约束 |
|---|---|---|
| module | 固定为 audit | 不允许复用其他模块名 |
| stage | 仅允许 write、fallback、export、retention、health | 不允许透传 actor、target、文件路径等动态值 |
| profile | 当前 active profile_id；缺失填 unknown | 不暴露部署自由文本 |
| outcome | 仅允许 success、failure、degraded | 与桥接失败/降级语义对齐 |
| error_code | 仅允许 none、INF_E_AUDIT_INVALID_EVENT、INF_E_AUDIT_WRITE_FAIL、INF_E_AUDIT_FALLBACK_FAIL、INF_E_AUDIT_EXPORT_DENIED、INF_E_AUDIT_EXPORT_FAIL、INF_E_AUDIT_RETENTION_FAIL | 不允许透传 errno、request/session/trace/task 等高基数标识 |

补充约束：

1. `AuditMetricsBridge` 不得把 `request_id`、`session_id`、`trace_id`、`task_id`、`evidence_ref.ref` 作为 metric label；这些标识只保留在日志/追踪/审计记录中。
2. `audit_write_total`、`audit_write_fail_total`、`audit_fallback_total`、`audit_fallback_fail_total`、`audit_export_total`、`audit_export_fail_total` 保持 `_total` 后缀；`audit_queue_depth` 继续冻结为 Gauge，后续若 exporter 需要单位换算，只能在 exporter 适配层完成，不回改 bridge API。
3. `MetricSample.identity_ref` 的 `name`、`type`、`unit` 必须与上表一致；运行期不允许按 actor/target/action 动态改 metric family。

### 6.7 主流程时序（正常路径）

1. 上游模块通过 MetricsFacade 获取 meter 与 instrument。
2. 写入 MetricSample（含基础标签与值）。
3. MetricContextEnricher 注入稳定维度标签。
4. CardinalityGuard 校验标签白名单与基数阈值。
5. AggregationEngine 聚合样本并维护窗口。
6. ReaderScheduler 周期触发导出批次。
7. ExporterAdapter 导出成功后更新成功指标与健康状态。

### 6.8 异常与恢复时序

异常分类：
1. 标签异常：标签超白名单或高基数超阈值。
2. 队列异常：导出队列积压或溢出。
3. 导出异常：远端不可达、超时、协议错误。
4. 配置异常：桶配置非法、单位冲突。

恢复动作：
1. 标签异常：拒绝该样本并计数 guard_reject_total，同时记录 sampling_drop 原因。
2. 队列异常：按策略 block 或 drop_oldest，强制写入 overflow 指标与告警日志。
3. 导出异常：切换 degraded exporter（noop 或本地文本快照），保留最小可观测闭环。
4. 配置异常：回退到上一个生效配置版本，记录配置回退审计事件。

失败兜底：
1. 连续 N 次导出失败进入 metrics_degraded_mode。
2. degraded_mode 下保留本地聚合与健康指标，不中断主业务流程。

### 6.8.1 logging 指标桥接失败语义

为避免 logging 与 metrics 之间形成递归失败链，冻结 LoggingMetricsBridge 的失败语义如下：

1. create_counter/create_gauge/create_histogram 或 record(sample) 失败时，只允许把 bridge 自身标记为 degraded，不得覆盖本次日志写入主结果。
2. bridge 失败时必须保留本地内存态的最近错误与失败计数，供 LoggingHealthProbe 或后续诊断读取；不得反向调用 LoggingFacade 再写一条“metrics failed”日志。
3. MetricsErrorCode::ProviderNotReady、ExportFailure、ExportTimeout 归类为 provider/exporter degraded：桥接进入 best-effort 模式，但原始 logging 主链继续执行。
4. MetricsErrorCode::QueueFull 归类为观测样本丢弃：仅丢弃本次指标观测，不把 backpressure 反推给日志主写入路径。
5. MetricsErrorCode::IdentityInvalid 与 ConfigInvalid 归类为 bridge 初始化失败：LoggingMetricsBridge 必须回退到 no-op bridge，并把问题暴露给 health/audit 侧，而不是带着部分初始化状态继续发射。

### 6.8.2 audit 指标桥接失败语义

为避免 audit 与 metrics 之间形成递归失败链，冻结 `AuditMetricsBridge` 的失败语义如下：

1. `create_counter`/`create_gauge` 或 `record(sample)` 失败时，只允许把 bridge 自身标记为 degraded，不得覆盖本次 audit write/export 的主结果。
2. bridge 失败时必须保留本地内存态的最近错误与失败计数，供 `AuditHealthStatus.metrics_bridge_degraded` 与后续诊断读取；不得反向调用 `IAuditLogger` 或额外写一条“metrics failed”审计记录。
3. `MetricsErrorCode::ProviderNotReady`、`ExportFailure`、`ExportTimeout` 归类为 provider/exporter degraded：桥接进入 best-effort 模式，但 audit 主写链继续执行。
4. `MetricsErrorCode::QueueFull` 归类为观测样本丢弃：仅丢弃本次指标观测，不把 backpressure 反推给 audit 主写或 fallback 路径。
5. `MetricsErrorCode::IdentityInvalid` 与 `ConfigInvalid` 归类为 bridge 初始化失败：`AuditMetricsBridge` 必须回退到 no-op bridge，并把问题暴露给 health 侧，而不是带着部分初始化状态继续发射。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.metrics.enabled | true | 默认/Profile/部署/运行时 | 是否启用 metrics |
| infra.metrics.provider.type | internal | 默认/Profile | internal/otel_sdk(v2预留) |
| infra.metrics.reader.interval_ms | 5000 | 默认/Profile/部署 | 导出调度周期 |
| infra.metrics.exporter.type | noop | 默认/Profile/部署 | noop/prom_text/otlp |
| infra.metrics.exporter.timeout_ms | 30000 | 默认/Profile/部署 | 导出超时 |
| infra.metrics.aggregation.temporality | cumulative | 默认/Profile | cumulative/delta |
| infra.metrics.queue.max_size | 4096 | Profile/部署 | 导出队列上限 |
| infra.metrics.queue.overflow_policy | drop_oldest | Profile/部署 | block/drop_oldest；选择规则遵循 docs/development/InfraConcurrencyPolicy.md |
| infra.metrics.labels.allowlist | module,stage,profile,outcome,error_code | 默认/Profile/部署 | 允许标签集合 |
| infra.metrics.labels.max_cardinality_per_metric | 200 | Profile/部署 | 单指标标签组合上限 |
| infra.metrics.histogram.default_buckets_seconds | 0.005,0.01,0.025,0.05,0.1,0.2,0.3,0.5,1,2,5 | 默认/Profile | 延迟类默认桶 |
| infra.metrics.audit.on_policy_change | true | 默认/Profile | 策略变更是否写审计 |

### 6.10 可观测性（日志/指标/追踪/审计）

日志：
1. provider init/reconfigure/flush/shutdown 生命周期。
2. guard 拒绝、队列溢出、导出失败、降级切换事件。
3. logging bridge 只允许记录 bridge 初始化失败、连续 export failure 与 degraded 切换，不记录每次 record(sample) 的成功明细，避免递归噪声。

指标（模块自观测）：
1. metrics_samples_total
2. metrics_samples_rejected_total
3. metrics_export_success_total
4. metrics_export_failure_total
5. metrics_export_latency_ms
6. metrics_queue_depth
7. metrics_guard_cardinality_reject_total
8. metrics_degraded_mode

追踪：
1. 导出流程可选生成 span（export_batch/flush/reconfigure）。
2. 日志审计事件保留 trace_id/span_id 关联字段。

审计：
1. 标签治理策略变更。
2. 直方图桶配置变更。
3. 连续导出失败触发 degraded。
4. degraded 恢复为 healthy。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 冻结 metrics 对外接口 | 新增 IMetricsProvider/IMeter/IMetricExporter | 先稳定调用面，防止上层直连实现 | infra/include/metrics/*.h | unit: MetricsInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 依赖 contracts ResultCode/ErrorInfo |
| 建立采样与聚合主链路 | 新增 MetricsFacade/Registry/AggregationEngine | 打通 record -> aggregate -> snapshot | infra/src/metrics/MetricsFacade.cpp 等 | unit: MetricsAggregationTest | ctest --test-dir build-ci -R MetricsAggregationTest --output-on-failure | 无 |
| 建立标签治理防线 | 新增 CardinalityGuard + allowlist | 防止高基数失控 | infra/src/metrics/CardinalityGuard.cpp | unit: MetricsCardinalityGuardTest | ctest --test-dir build-ci -R MetricsCardinalityGuardTest --output-on-failure | 阻塞：标签白名单初版需评审 |
| 建立导出器可插拔框架 | 新增 MetricReaderScheduler + ExporterAdapter | 满足 profile 可替换与导出降级 | infra/src/metrics/MetricReaderScheduler.cpp 等 | unit: MetricsExporterAdapterTest; integration: MetricsExportIntegrationTest | ctest --test-dir build-ci -R "MetricsExporterAdapterTest|MetricsExportIntegrationTest" --output-on-failure | 阻塞：OTLP 依赖暂未冻结 |
| 建立失败降级与恢复 | 新增 degraded 状态与 fallback 导出器 | 确保导出故障不拖垮主流程 | infra/src/metrics/MetricsRecovery.cpp | integration: MetricsDegradedModeIntegrationTest | ctest --test-dir build-ci -R MetricsDegradedModeIntegrationTest --output-on-failure | 依赖 health/logging 接口 |
| 建立配置策略闭环 | 新增 MetricsConfigPolicy + bucket policy | 支持四层配置与 SLO 桶策略 | infra/src/metrics/MetricsConfigPolicy.cpp | unit: MetricsConfigMergeTest | ctest --test-dir build-ci -R MetricsConfigMergeTest --output-on-failure | 阻塞：profiles 配置键一致性 |
| 建立 infra 测试 Gate | 新增 unit/integration/failure 测试集 | 将设计约束转自动化门禁 | tests/unit/infra/metrics/*, tests/integration/infra/metrics/* | unit+integration+failure | ctest --test-dir build-ci -L metrics --output-on-failure | 依赖 tests/CMakeLists 注册 |

无法立即映射项：
1. OTel SDK 全量接入：当前阶段保留 API 兼容点，不纳入本轮最小闭环。
2. 远程采样与动态规则下发：受部署安全链路与配置中心能力冻结状态影响，后续版本推进。

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议目录：
1. infra/include/metrics/
2. infra/src/metrics/
3. tests/unit/infra/metrics/
4. tests/integration/infra/metrics/
5. tests/contract/infra/（用于 contracts 消费一致性验证）

建议首批文件：
1. infra/include/metrics/IMetricsProvider.h
2. infra/include/metrics/IMeter.h
3. infra/include/metrics/IMetricExporter.h
4. infra/include/metrics/MetricTypes.h
5. infra/include/metrics/MetricsErrors.h
6. infra/src/metrics/MetricsFacade.cpp
7. infra/src/metrics/InstrumentRegistry.cpp
8. infra/src/metrics/AggregationEngine.cpp
9. infra/src/metrics/CardinalityGuard.cpp
10. infra/src/metrics/MetricReaderScheduler.cpp
11. infra/src/metrics/MetricsExporterAdapter.cpp
12. infra/src/metrics/MetricsRecovery.cpp
13. infra/src/metrics/MetricsConfigPolicy.cpp

### 8.2 分阶段实施计划（最小可交付）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| MET-M1 | Not Started | 新增并冻结 metrics 接口层 | 架构5.10 + 蓝图6 | infra/include/metrics/*.h | MetricsInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 接口可编译且无业务实现依赖 |
| MET-M2 | Not Started | 补齐采样与聚合最小闭环 | 本文档6.2/6.7 | MetricsFacade/Registry/AggregationEngine | MetricsAggregationTest | ctest --test-dir build-ci -R MetricsAggregationTest --output-on-failure | Counter/Gauge/Histogram 行为可验证 |
| MET-M3 | Not Started | 补齐标签治理与高基数防护 | Prometheus 命名与标签实践 | CardinalityGuard + policy | MetricsCardinalityGuardTest | ctest --test-dir build-ci -R MetricsCardinalityGuardTest --output-on-failure | 超阈值样本被拒绝且有观测 |
| MET-M4 | Not Started | 补齐导出与降级恢复 | infra 详细设计 + SRE 简化原则 | ReaderScheduler/ExporterAdapter/Recovery | MetricsExportIntegrationTest + MetricsDegradedModeIntegrationTest | ctest --test-dir build-ci -R "MetricsExportIntegrationTest|MetricsDegradedModeIntegrationTest" --output-on-failure | 导出失败可降级且可恢复 |
| MET-M5 | Not Started | 补齐配置策略与 CI Gate | 工程规范3.7 | MetricsConfigPolicy + tests 标签注册 | MetricsConfigMergeTest + metrics 标签全量测试 | ctest --test-dir build-ci -L metrics --output-on-failure | Gate 稳定通过，可重复执行 |

### 8.3 阻塞项、解阻条件与回退策略（建议级）

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| Profile 中 metrics 配置键未完全统一 | MET-M3/MET-M5 | profile.cmake/runtime_policy.yaml 定义统一键集合 | 先冻结最小键：enabled/exporter/interval/labels | 暂用默认配置 + 禁用动态覆盖 |
| OTLP exporter 依赖未确定 | MET-M4 | 明确三方依赖版本与构建方式 | 先实现 noop + prom_text exporter | OTLP 作为 v2 插件延后 |
| health/logging 接口签名未统一 | MET-M4 | infra 子模块接口评审通过 | 使用中间适配器桥接当前接口 | 降级只输出本地指标与日志 |
| 标签白名单领域划分未评审 | MET-M3 | 架构评审确认全局 label taxonomy | 先落地核心白名单并记录 rejected | 临时 strict 模式，默认拒绝未知标签 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试类型 | 覆盖范围 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | InstrumentRegistry, AggregationEngine, CardinalityGuard, ConfigPolicy | 同名指标冲突、聚合正确性、标签拒绝、配置回退 | 断言全部通过，失败可复现 |
| Contract | metrics 接口与 contracts 标识语义对齐 | request/session/task/error_code 引用语义稳定 | 无破坏式字段变化 |
| Integration | record -> aggregate -> export 全链路 | exporter 成功/失败、flush/shutdown、degraded 切换 | 端到端状态可判定 |
| Failure Injection | 导出超时、队列溢出、配置非法、标签爆炸 | 降级触发、恢复触发、日志审计落地 | 无静默失败 |
| Compatibility | 不同 profile/旧配置兼容加载 | edge_minimal 与 desktop_full 的行为差异可控 | 启停与导出行为一致可解释 |

### 9.2 质量门（Gate）建议清单

1. 编译 Gate：metrics 新增代码在 build-ci 下全量可编译通过。
2. 单测 Gate：metrics 标签 `unit` 全绿。
3. 集成 Gate：metrics 标签 `integration` 全绿。
4. 失败注入 Gate：关键异常路径全部覆盖并可判定。
5. 兼容 Gate：旧配置文件加载不崩溃，默认值回退生效。
6. 观测 Gate：导出失败时必须同时出现 fail 指标 + 错误日志 + 审计事件（至少二者）。

建议验收命令模板：
1. cmake --build build-ci --target dasall_infra
2. ctest --test-dir build-ci -L metrics --output-on-failure
3. ctest --test-dir build-ci -R "Metrics.*(Integration|Failure|Config)" --output-on-failure

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | runtime/cognition/tools/memory/knowledge/services/multi_agent | 先引入 IMetrics* 接口 + 默认 noop exporter，再分模块接入采样点 | 先在 desktop_full 开启，edge_minimal 保持最小集；按模块逐步放量 | OTel SDK exporter、远程规则下发、exemplar 关联 |
| Medium（标签治理切换） | 指标消费侧（仪表盘/告警规则） | 先提供兼容别名与迁移窗口，再移除旧标签 | 双写窗口：old_label + new_label 同时输出 | 标签 schema version 化 |
| None（内部聚合优化） | 无外部可见变更 | 保持同名指标与语义不变 | 灰度仅影响内部性能参数 | 自适应批量与压缩策略 |

迁移策略：
1. 版本 1：接口冻结 + noop/prom_text 导出。
2. 版本 2：OTLP 插件化导出（可选）。
3. 版本 3：exemplar 与 trace 深关联优化。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 风险清单

| 风险 | 等级 | 触发条件 | 影响 | 缓解措施 |
|---|---|---|---|---|
| 标签基数爆炸 | High | 调用方写入动态标签值 | 内存/CPU 激增，导出阻塞 | CardinalityGuard + allowlist + reject 计数 |
| 直方图桶不合理 | Medium | 桶未围绕 SLO 设置 | P95/P99 失真，告警误报 | HistogramBucketPolicy + profile 校准 |
| 导出端不稳定 | High | 网络抖动/Collector 不可达 | 指标丢失与排障盲区 | degraded exporter + 本地快照 + 重试退避 |
| 过度告警 | Medium | 将非症状指标直接分页 | 告警噪声、值班疲劳 | 按 SRE 原则区分 page 与 dashboard-only |

### 11.2 回退策略

1. 导出回退：OTLP 失败时回退到 prom_text/noop，保留本地聚合。
2. 配置回退：新策略加载失败回退到上一版生效配置。
3. 功能回退：通过 profile 关闭高开销指标，仅保留核心金指标。
4. 发布回退：灰度阶段异常超阈值即回滚到上一版本二进制与配置。

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. OTLP exporter 在本仓库的依赖引入方式（third_party 还是系统包）尚未冻结。
2. 全局标签 taxonomy（module/stage/operation/outcome/error_code）是否需要 contracts 辅助文档化仍待评审。
3. edge_minimal 档位下 histogram 默认桶是否需要进一步收敛为低内存版本。
4. metrics 与 alerting 规则资产放置路径（profiles 还是 scripts/ci）待统一。

### 12.2 后续原子任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| MET-T001 | Not Started | 新增 IMetricsProvider/IMeter/IMetricExporter 接口头文件 | 蓝图6 + 工程规范3.2 | infra/include/metrics/*.h | MetricsInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 接口编译通过 |
| MET-T002 | Not Started | 新增 MetricTypes 与错误码定义 | 本文档6.5/6.6 | MetricTypes.h/MetricsErrors.h | MetricsTypesTest | ctest --test-dir build-ci -R MetricsTypesTest --output-on-failure | 类型校验通过 |
| MET-T003 | Not Started | 新增 AggregationEngine 并实现 Counter/Gauge/Histogram | 本文档6.2/6.7 | infra/src/metrics/AggregationEngine.cpp | MetricsAggregationTest | ctest --test-dir build-ci -R MetricsAggregationTest --output-on-failure | 聚合断言通过 |
| MET-T004 | Not Started | 新增 CardinalityGuard 与标签白名单策略 | Prometheus 标签实践 | infra/src/metrics/CardinalityGuard.cpp | MetricsCardinalityGuardTest | ctest --test-dir build-ci -R MetricsCardinalityGuardTest --output-on-failure | 高基数拦截可观测 |
| MET-T005 | Not Started | 新增 ReaderScheduler 与 ExporterAdapter | OTel API/SDK 分离原则 | infra/src/metrics/MetricReaderScheduler.cpp | MetricsExporterAdapterTest | ctest --test-dir build-ci -R MetricsExporterAdapterTest --output-on-failure | 导出链路可用 |
| MET-T006 | Not Started | 补齐导出失败降级恢复实现 | infra 子系统降级约束 | infra/src/metrics/MetricsRecovery.cpp | MetricsDegradedModeIntegrationTest | ctest --test-dir build-ci -R MetricsDegradedModeIntegrationTest --output-on-failure | degraded/恢复双路径通过 |
| MET-T007 | Not Started | 补齐配置合并与桶策略适配 | 架构8.6 + profile 策略 | infra/src/metrics/MetricsConfigPolicy.cpp | MetricsConfigMergeTest | ctest --test-dir build-ci -R MetricsConfigMergeTest --output-on-failure | 配置回退可判定 |
| MET-T008 | Not Started | 新增 metrics 全套测试并接入标签 Gate | 工程规范3.7 | tests/unit/infra/metrics/* 等 | unit/integration/failure | ctest --test-dir build-ci -L metrics --output-on-failure | Gate 全绿 |

---

## 附：来源证据索引

1. 架构与分层：docs/architecture/DASSALL_Agent_architecture.md
2. 工程目录与依赖规则：docs/architecture/DASALL_Engineering_Blueprint.md
3. ADR 边界：docs/adr/ADR-005-architecture-review-baseline.md、ADR-006-context-orchestrator-vs-prompt-composer.md、ADR-007-reflection-engine-vs-recovery-manager.md、ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
4. contracts 冻结策略：docs/plans/DASALL_contracts冻结实施计划.md、docs/todos/contracts/DASALL_contracts冻结TODO总表.md
5. infra 子系统基线：docs/architecture/DASALL_infrastructure子系统详细设计.md、docs/architecture/DASALL_infra_logging模块详细设计.md、docs/architecture/DASALL_infra_tracing模块详细设计.md
6. 工程规范：docs/development/DASALL_工程协作与编码规范.md
7. 现状代码：infra/CMakeLists.txt、infra/include/metrics/、infra/src/{InfraServiceFacade.cpp,InfraErrorCode.cpp,audit/,plugin/,tracing/}、infra/src/metrics/
8. 行业实践：OpenTelemetry Metrics Spec、Prometheus practices（naming/histograms）、Google SRE Monitoring Principles
