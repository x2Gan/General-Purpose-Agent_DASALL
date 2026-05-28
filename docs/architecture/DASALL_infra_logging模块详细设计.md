# DASALL infra/logging 模块详细设计（Detailed Design）

## 1. 模块概览

### 1.1 模块定位

infra/logging 属于 Infrastructure Layer（Layer 1），负责提供统一运行日志能力，并与 infra/audit、tracing、metrics 协同形成可观测闭环。

- 设计对象：infra/logging 子模块
- 设计阶段：Detailed Design
- 目标状态：可在 x86 与 ARM 场景下稳定运行，支持结构化日志、审计隔离、可配置治理与故障降级

### 1.2 边界与依赖方向

- 上游调用方：runtime、cognition、tools、memory、knowledge、services、multi_agent、apps
- 下游依赖：platform 文件与时间能力、third_party 日志库（优先 spdlog）
- 同层协同：infra/tracing、infra/metrics、infra/config、infra/health
- 严格边界：infra/logging 不反向依赖任何业务模块实现

### 1.3 来源依据

1. 架构文档：docs/architecture/DASSALL_Agent_architecture.md（3.4.7、3.7、5.10、8.5、9.4）
2. 工程蓝图：docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.2、6、7）
3. ADR：docs/adr/ADR-005-architecture-review-baseline.md、ADR-006、ADR-007、ADR-008
4. contracts 计划/TODO：docs/plans/DASALL_contracts冻结实施计划.md、docs/todos/contracts/DASALL_contracts冻结TODO总表.md、docs/todos/contracts/WP-04-边界对象TODO.md、docs/todos/contracts/WP-05-子域细化与ContractTestsTODO.md
5. 工程规范：docs/development/DASALL_工程协作与编码规范.md
6. 代码现状：infra/CMakeLists.txt、infra/include/logging/、infra/src/{InfraServiceFacade.cpp,InfraErrorCode.cpp,audit/,plugin/,tracing/}、infra/src/logging/（空目录）
7. 行业参考：OpenTelemetry Logs 规范、spdlog 异步与 sinks 实践、Boost.Log 设计要点

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| LOG-C001 | 架构文档 3.4.7/5.10 | Must | logging 必须提供结构化日志，并与 trace、metrics、audit 协同 | 子组件/接口/流程 |
| LOG-C002 | 架构文档 8.5/9.4 | Must | 日志必须携带 request_id、session_id、trace_id，并支持本地落盘和按 trace/session 诊断拉取 | 核心对象/输出 |
| LOG-C003 | 工程蓝图 3.12 + infra 总设 6.4 | Must | logging 必须与 audit 协同，但不拥有 AuditService 主存储职责 | 子组件/依赖边界 |
| LOG-C004 | 工程蓝图 4.2 | Must-Not | infra 不得反向依赖任何业务模块实现 | 依赖关系 |
| LOG-C005 | 架构文档 3.7 | Must-Not | Platform/Infra 不反向依赖 Agent 业务模块 | 依赖关系 |
| LOG-C006 | 架构文档 8.8/工程蓝图 3.12 | Must | 审计日志必须独立保存并可导出，不得与普通运行日志混存 | 存储/异常恢复 |
| LOG-C007 | 架构文档 8.6 | Must | 配置必须支持四层覆盖：默认 -> Profile -> 部署 -> 运行时覆盖 | 配置策略 |
| LOG-C008 | ADR-005 | Must | 在 contracts 冻结基线下推进，不能用 logging 设计反向改写 contracts 已冻结语义 | 核心对象/流程 |
| LOG-C009 | ADR-006 | Must-Not | 不把 Prompt 渲染或 provider 细节写入跨模块语义对象；logging 只消费已冻结语义对象元数据 | 核心对象 |
| LOG-C010 | ADR-007 | Must-Not | logging 不拥有恢复决策权，仅记录失败语义与恢复执行证据 | 接口/流程 |
| LOG-C011 | ADR-008 | Must | 多 Agent 审计字段需保留 parent_task_id、lease_id、worker_type 等链路标识 | 核心对象/审计 |
| LOG-C012 | contracts 计划第 5/6 章 | Must-Not | 不把实现细节（sink 类型、线程池策略）写入 contracts 共享语义对象 | contracts 对齐 |
| LOG-C013 | 工程规范 3.6 | Must | 错误不能吞没；日志写入失败必须转化为可观测结果（metric/降级计数） | 异常处理 |
| LOG-C014 | 工程规范 3.7 | Should | 新增公共接口需同步 unit/contract 测试 | 测试策略 |
| LOG-C015 | 工程蓝图 5.1/3.13 | Must | Profile 裁剪只能裁能力和实现，不能绕过 Audit 主链路 | 配置/发布 |
| LOG-C016 | 工程蓝图 6 | Should | 对外暴露接口应遵循 infra/include/ 根下的稳定接口分布（如 logging/、audit/ 等子目录） | 目录/接口 |
| LOG-C017 | 行业参考 OTel Logs | Should | 日志记录应保留 TraceId/SpanId/Resource 相关属性以支持跨信号关联 | 可观测性 |
| LOG-C018 | 行业参考 spdlog async | Should | 异步队列溢出策略必须显式可配置（阻塞或覆盖最旧）并可观测 | 异常恢复/配置 |

### 2.2 约束抽取结论

- Must：四类观测能力联动、审计隔离、四层配置、诊断可回放、错误可观测。
- Should：优先采用成熟日志库与 OTel 兼容字段，保持接口目录一致。
- Must-Not：不得改写 ADR 结论、不得反向污染 contracts、不得跨模块扩张主控职责。

---

## 3. 现状与缺口

### 3.1 现状判定

- infra 模块当前已具备真实源码入口；logging 子域仍以接口冻结为主，尚无 logging 实现源码。
- infra/src/logging/ 目录存在但为空；infra/include/ 目前无头文件。
- contracts 已进入 M5/V1 Ready 阶段，边界对象与测试门禁已形成，logging 需要消费而非改写这些对象。

### 3.2 现状-目标差距表

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| 提供统一 ILogger 接口 | 缺失 | 无对外接口，调用方将各自实现日志导致风格漂移 | High | P0 |
| 结构化日志落盘（INFO/WARN/ERROR/AUDIT） | 缺失 | 无统一字段模型与格式约束 | High | P0 |
| 审计接口协同与关联透传 | 缺失 | logging 与 audit 尚无标准协同适配点 | High | P0 |
| 与 trace/metrics 关联 | 缺失 | 故障定位无法跨信号关联 | Medium | P1 |
| 多 Profile 配置覆盖 | 缺失 | 无法支持 edge_minimal 与 desktop_full 差异化 | Medium | P1 |
| 异步写入与背压策略 | 缺失 | 高负载下可能阻塞主流程或丢日志不可见 | High | P0 |
| 失败恢复与降级（fallback） | 缺失 | sink 故障时无兜底路径 | High | P0 |
| 测试基线（unit/integration/failure injection） | 缺失 | 无法验证稳定性和回归 | High | P0 |

### 3.3 风险冲突识别

1. 边界冲突风险：logging 若直接感知 runtime 状态机细节，会违反 infra 边界。
2. 语义重复风险：若在 logging 再定义 ErrorInfo/Observation 语义，会与 contracts 重复。
3. 依赖反转风险：调用方直接依赖具体 sink 实现，后续会反向绑死 infra。

---

## 4. 候选方案对比

### 4.1 候选方案描述

- 方案 A：同步直写单文件日志（最小实现）
- 方案 B：异步多 sink 结构化日志（spdlog 优先，审计独立）
- 方案 C：OTel 原生日志 SDK 直连 Collector（日志信号优先）

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| 方案 A 同步单 sink | 中 | 中 | 低 | 高并发阻塞、审计隔离难、扩展弱 | 淘汰：仅适合 PoC，不满足长期运行治理 |
| 方案 B 异步多 sink + 审计隔离 | 高 | 高 | 中 | 线程池配置与溢出策略需要严控 | 保留并采纳：满足可观测、可恢复、可裁剪 |
| 方案 C OTel 原生直连 | 中 | 中 | 高 | 引入复杂度高，当前仓库缺少完整 OTel 链路基建 | 淘汰：可作为后续演进目标 |

### 4.3 行业实践映射

1. spdlog：支持异步线程池、队列溢出策略（block/overrun_oldest）、rotating/daily/ringbuffer/dist sinks，适配当前 C++20 与 third_party 路径。
2. OpenTelemetry Logs：强调 logs-traces-metrics 关联，核心是 TraceId/SpanId/Resource 一致属性模型。
3. Boost.Log：强调 source/sink/filter/formatter 分层，适合作为抽象架构参考。

---

## 5. 决策结论

### 5.1 最终选型

选择方案 B：异步多 sink 的结构化日志架构（spdlog 优先实现），并保留 OTel 兼容字段和 exporter 预留点。

### 5.2 放弃其他方案原因

1. 放弃方案 A：无法满足长期运行、高并发、审计隔离与演进需求。
2. 放弃方案 C：当前阶段投入过高，且超出本模块最小闭环目标。

### 5.3 一致性说明

- 与架构一致：满足 Infrastructure Layer 职责与依赖方向。
- 与 ADR 一致：不改写 ADR-006/007/008，仅消费其冻结语义锚点字段。
- 与 contracts 冻结一致：不把 sink/线程模型写入 contracts 对象。

---

## 6. 详细设计

### 6.1 职责边界

logging 子模块职责：

1. 提供统一日志写入入口（普通日志 + 审计日志）。
2. 维护结构化字段规范与敏感信息脱敏。
3. 管理 sink 路由、异步队列、落盘轮转、失败降级。
4. 产出可观测统计（写入成功率、丢弃计数、队列深度、落盘延迟）。

logging 非职责：

1. 不承担业务语义判定与恢复策略决策。
2. 不改写 contracts 共享对象结构。
3. 不管理主流程状态机。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| LoggingFacade | 统一入口，承接调用方写日志请求 |
| LogContextEnricher | 注入 request_id/session_id/trace_id/task_id 等上下文字段 |
| StructuredFormatter | 将事件转为稳定结构化记录（JSON line 或 key-value） |
| AuditLinkAdapter | 对接 IAuditLogger 的审计协同适配器（不承担存储） |
| SinkDispatcher | 按级别和类别路由至 rotating/daily/console/ringbuffer 等 sink |
| AsyncQueueController | 管理异步线程池、队列容量、溢出策略 |
| RedactionFilter | 敏感字段遮蔽（token/secret/path 等） |
| LoggingHealthProbe | 暴露健康状态与降级状态给 infra/health |
| LoggingMetricsBridge | 输出 logging 自身指标给 infra/metrics |
| LogQueryService | 按 trace_id/session_id 生成受控 diagnostics artifact |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| LoggingFacade | 上游模块 LogEvent | SinkDispatcher | 至少包含 level、message、context |
| LogContextEnricher | LogEvent + 线程/请求上下文 | EnrichedLogEvent | 缺失关键 ID 时补齐 unknown 并计数 |
| StructuredFormatter | EnrichedLogEvent | FormattedRecord | 字段稳定，禁止未声明字段泄漏 |
| AuditLinkAdapter | 高风险日志事件上下文 | IAuditLogger | 仅转发审计关联，不落审计主存储 |
| SinkDispatcher | FormattedRecord | file/syslog/console/ringbuffer | 路由策略可配置 |
| AsyncQueueController | 记录写入请求 | 异步投递状态 | 支持 block/overrun_oldest |
| RedactionFilter | 原始 message/attrs | 脱敏后记录 | 敏感值不可明文落盘 |
| LoggingHealthProbe | 写入状态、错误计数 | HealthStatus | 反映 degraded 状态 |
| LoggingMetricsBridge | 管线统计 | IMetricsProvider/IMeter | 只经由 MetricSample 发射五个 frozen 指标 |
| LogQueryService | LogQueryRequest + LogQueryAccessContext | diagnostics/local artifact consumer | 只接受 trace/session 精确 selector，并输出本地 artifact_ref |

### 6.4 子组件依赖关系

- LoggingFacade -> LogContextEnricher -> RedactionFilter -> StructuredFormatter -> SinkDispatcher
- LoggingFacade -> AuditLinkAdapter（通过 IAuditLogger 协同，不承载审计存储）
- SinkDispatcher -> AsyncQueueController -> sink adapters
- SinkDispatcher/AsyncQueueController -> LoggingMetricsBridge/LoggingHealthProbe
- LogQueryService -> 本地已脱敏日志文件/索引 -> diagnostics/export consumer

依赖约束：全部通过 infra 内部接口连接，不直接依赖 runtime/cognition 实现类。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐 |
|---|---|---|---|
| LogContext | request_id, session_id, trace_id, task_id, parent_task_id, lease_id | 允许 unknown，不允许空字符串落盘 | 对齐 AgentRequest/WorkerTask/WorkerLease 的标识语义，仅消费 |
| LogEvent | level, category, message, attrs, timestamp | message 可空但 attrs 需可序列化 | 不新增 contracts 字段 |
| AuditRef | evidence_ref, trace_id, task_id | 用于日志与审计关联，不定义审计对象本体 | 引用 infra/audit 的 AuditEvent 语义 |
| SinkRoutePolicy | level->sink map, audit route | Profile 可覆盖 | 不进入 contracts |
| RedactionPolicy | ruleset_version, pattern_set | 配置可热更新 | 不进入 contracts |
| LogQueryRequest | query_id, selector_kind, selector_value, start_ts_ms, end_ts_ms, max_records | selector_kind 仅允许 TraceId/SessionId，必须带有序时间窗，禁止自由查询语法 | 只消费 LogContext 已冻结标识语义，不新增 contracts 对象 |
| LogQueryAccessContext | actor_ref, consumer_module, policy_decision_ref, infra_context | actor/consumer 必须可审计，policy_decision_ref 必须证明 Allow | 复用 InfraContext 与 policy::PolicyDecisionRef，不扩写 contracts |
| LogQueryResult | artifact_ref, match_count, truncated, checksum, created_at | 只返回本地 artifact 引用与摘要，不直接暴露可变记录集合 | 失败边界只使用 contracts::ResultCode/ErrorInfo |

### 6.6 核心接口语义定义

建议接口（落地在 infra/include/logging/）：

1. ILogger
   - log(event): 记录普通日志
   - flush(timeout_ms): 刷新缓冲
   - set_level(level): 动态调整级别
2. ILogContextProvider
   - current_context(): 提供标准上下文键值
3. ILogConfigurator
   - apply(config): 应用四层合并后的配置
4. IAuditLinkAdapter
   - attach_audit_ref(log_event): 为高风险日志关联 evidence_ref
5. ILogQueryService
  - query(request, access_context): 在通过权限证明与配置 gate 后生成本地诊断 artifact

ILogConfigurator 对象冻结补充（v1）：

1. `ILogConfigurator` 只接受已经过 ConfigCenter 结构校验的 `LoggingConfig` 对象，不直接接受 YAML/JSON/free-form patch。
2. `LoggingConfig` 最小字段冻结为：`level`、`format`、`async_enabled`、`queue_size`、`overflow_policy`、`file_path`、`rotate_max_size_mb`、`rotate_max_files`、`redaction_enabled`、`redaction_ruleset`、`enable_diag_pull`、`audit_required`、`source_entries`。
3. `source_entries` 复用 `config::TypedConfig` 保存当前生效 key 的来源层、来源标识和值类型，用于 logging 本地执行每个 key 的层级接受校验，而不是发明第二套 provenance 模型。
4. `ILogConfigurator::apply(config)` 返回 `LoggingConfigApplyResult`，结果对象只允许暴露 `contracts::ResultCode` 与 `contracts::ErrorInfo` 作为错误边界，不新增 logging 私有错误对象。

`LoggingConfig` 对象表：

| 字段 | 类型 | 默认值 | 约束 |
|---|---|---|---|
| `level` | `LogLevel` | `Info` | 支持 TRACE/DEBUG/INFO/WARN/ERROR/FATAL；允许 runtime override |
| `format` | `LoggingFormat` | `JsonLine` | 首版只接受 `json_line` / `key_value`；不允许 runtime override |
| `async_enabled` | `bool` | `true` | 只允许默认/Profile |
| `queue_size` | `uint32_t` | `8192` | 必须大于 0；运行期不允许热改 |
| `overflow_policy` | `LoggingOverflowPolicy` | `Block` | 只允许 `block` / `overrun_oldest`；运行期不允许热改 |
| `file_path` | `string` | `logs/runtime.log` | 不能为空；允许 deployment/runtime override |
| `rotate_max_size_mb` | `uint32_t` | `50` | 必须大于 0 |
| `rotate_max_files` | `uint32_t` | `10` | 必须大于 0 |
| `redaction_enabled` | `bool` | `true` | 只允许默认/Profile |
| `redaction_ruleset` | `string` | `default_v1` | 非空；允许 deployment/runtime override |
| `enable_diag_pull` | `bool` | `true` | 允许默认/Profile/部署；不允许 runtime override |
| `audit_required` | `bool` | `true` | 只接受 `true`；任何 `false` 都视为绕过 audit 主链 |
| `source_entries` | `vector<TypedConfig>` | N/A | 必须覆盖 frozen key set，且所有 entry 满足 schema/source validity |

`LoggingConfigApplyResult` 对象表：

| 字段 | 类型 | 说明 |
|---|---|---|
| `applied` | `bool` | 是否已成为当前生效 logging 配置 |
| `runtime_override_active` | `bool` | 当前生效配置中是否存在 runtime override 来源 |
| `rejected_keys` | `vector<string>` | 因局部层级规则或 audit 主链保护被拒绝的 key |
| `result_code` | `contracts::ResultCode` | 失败时只落入 contracts 已冻结错误域 |
| `error_info` | `optional<contracts::ErrorInfo>` | 可观测失败信息 |

前置条件：

- logger 已 init；配置已加载。
- `LogQueryService` 额外要求 `infra.logging.export.enable_diag_pull == true`，且 access context 持有来自 Policy Gate 的 allow 决策证明。

后置条件：

- 成功写入或返回可判定失败码，并更新写入指标。
- `LogQueryService` 成功时必须产出本地 `artifact_ref` 或结构化失败；不得返回未脱敏的原始记录容器。

错误语义：

- LOG_E_QUEUE_FULL
- LOG_E_SINK_IO
- LOG_E_FORMAT_INVALID
- LOG_E_CONFIG_INVALID

`LogQueryService` 失败边界冻结补充：

1. query 对象非法：`contracts::ResultCode::ValidationFieldMissing`
2. policy/config gate 拒绝：`contracts::ResultCode::PolicyDenied`
3. 本地 artifact 生成失败：`contracts::ResultCode::ToolExecutionFailed`

### 6.7 主流程时序（正常路径）

1. 调用方提交 LogEvent。
2. LoggingFacade 拉取并补齐 LogContext。
3. RedactionFilter 执行脱敏。
4. StructuredFormatter 生成结构化记录。
5. SinkDispatcher 根据 route 投递到异步队列。
6. Async worker 落盘并回传状态。
7. LoggingMetricsBridge 更新成功计数/延迟。

### 6.8 异常与恢复时序

异常分类：

1. 队列满：依据策略 block 或 overrun_oldest，并上报 drop_count。
2. sink IO 失败：切换 fallback sink（ringbuffer + stderr），标记 degraded。
3. 格式化失败：降级为最小字段文本格式并上报 LOG_E_FORMAT_INVALID。
4. 审计关联失败：记录 logging_audit_link_fail_total，并返回可判定告警状态。

恢复动作：

1. 周期性重试恢复主 sink。
2. 恢复成功后清除 degraded 状态并输出恢复审计事件。
3. 若持续失败超过阈值，触发健康检查失败信号给 infra/health。

### 6.9 配置项与默认策略

冻结补充：logging 不自行定义 runtime patch 结构，也不直接消费自由字典。四层覆盖顺序仍由 ConfigCenter 统一完成；LoggingConfigAdapter 只消费当前生效的 typed config，并在本地执行 logging 私有的 key 级接受规则。

key 域冻结规则：

1. 所有 logging 私有键统一使用 `infra.logging.*` 前缀，不再保留无命名空间的 `logging.*` 裸键。
2. `infra.audit.required` 虽属于跨组件键，但 logging adapter 必须将其作为配置准入门的一部分读取，以阻止任何 profile/override 绕过 audit 主链。
3. runtime override 只允许用于安全 tunable 键：`infra.logging.level`、`infra.logging.file.path`、`infra.logging.redaction.ruleset`。
4. ConfigCenter 负责四层全局顺序与 patch 结构校验；LoggingConfigAdapter 负责 key 本地语义接受和 audit gate，拒绝后必须保留当前稳定配置，不写入部分成功状态。

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.logging.level | INFO | 默认/Profile/部署/运行时 | 全局最小日志级别 |
| infra.logging.format | json_line | 默认/Profile/部署 | 结构化格式 |
| infra.logging.async.enabled | true | 默认/Profile | 是否异步 |
| infra.logging.async.queue_size | 8192 | 默认/Profile/部署 | 队列容量 |
| infra.logging.async.overflow_policy | block | 默认/Profile/部署 | block 或 overrun_oldest；选择规则遵循 docs/development/InfraConcurrencyPolicy.md |
| infra.logging.file.path | logs/runtime.log | 默认/部署/运行时 | 普通日志路径 |
| infra.logging.file.rotate.max_size_mb | 50 | 默认/Profile/部署 | 轮转阈值 |
| infra.logging.file.rotate.max_files | 10 | 默认/Profile/部署 | 保留份数 |
| infra.logging.redaction.enabled | true | 默认/Profile | 敏感信息脱敏 |
| infra.logging.redaction.ruleset | default_v1 | 默认/部署/运行时 | 规则集版本 |
| infra.logging.export.enable_diag_pull | true | 默认/Profile/部署 | 是否允许按 trace/session 导出 |
| infra.audit.required | true | 默认/Profile | 审计主链不可关闭；logging 仅允许降级，不允许旁路 |

### 6.10 可观测性（日志/指标/追踪/审计）

1. 日志：结构化字段至少含 timestamp、level、module、message、request_id、session_id、trace_id。
2. 指标：LoggingMetricsBridge 通过 IMetricsProvider::get_meter(MeterScope{.name = "infra.logging", .version = "v1"}) 获取 meter，并只经由 IMeter::record(MetricSample) 发射下列五个指标：

| 指标名 | 类型 | unit | stage | 说明 |
|---|---|---|---|---|
| logging_write_total | Counter | 1 | write | 普通写入成功总数 |
| logging_write_fail_total | Counter | 1 | write/recovery | 普通写入失败总数 |
| logging_drop_total | Counter | 1 | queue | 队列溢出或降级丢弃总数 |
| logging_queue_depth | Gauge | 1 | queue | 当前异步队列深度 |
| logging_flush_latency_ms | Histogram | ms | flush | flush 延迟分布 |

3. 标签约束：复用 metrics::MetricLabels 五元组，module 固定 logging，stage 仅允许 write、queue、flush、recovery，profile 缺失填 unknown，outcome 仅允许 success、failure、degraded，error_code 仅允许 none 或四个 LOG_E_*。
4. `INF-LOG-FIX-007` 必须继续沿这五个 frozen metric family 推进主链，不允许把 redacted path 再拆出第六个指标族；redaction 只代表 accepted sample 来自默认 `enrich -> redact -> format -> dispatch` 主链，而不是另一套 metrics taxonomy。
4. 指标桥接失败只允许把 LoggingMetricsBridge 标记为 degraded，并通过 health/audit 或非递归 failure hook 暴露；不得递归调用 LoggingFacade 再写失败日志。
5. 追踪：记录 trace_id/span_id 兼容字段（若可获得 span_id 则写入）。
6. 审计协同：关键动作日志需附 evidence_ref，并通过 IAuditLinkAdapter 关联至 infra/audit。

### 6.10.1 LoggingHealthProbe 契约冻结

`LoggingHealthProbe` 不定义 logging 私有 health result，也不扩写 infra/health 通用接口；其唯一对外边界固定为 `dasall::infra::IHealthProbe::probe() -> ProbeResult`。

| 项目 | 冻结结论 |
|---|---|
| 对外接口 | 直接实现 `IHealthProbe`，不新增第二套 logging health interface |
| 固定 descriptor | `probe_name = "infra.logging.pipeline"`、`group = "readiness"`、`criticality = Critical`、`interval_ms = 5000`、`timeout_ms = 100` |
| 输入信号 | `queue_depth`、`dropped_total_delta`、`recovery_degraded`、`fallback_active`、`unrecoverable_failure_total`、`metrics_bridge_degraded` |
| Healthy 条件 | 无降级、无未恢复失败、队列未越高水位、无新增 drop |
| Degraded 条件 | fallback 已启用，或 recovery/metrics bridge degraded，或队列高水位/新增 drop 被观测到 |
| Unhealthy 条件 | 主/降级写入链都不可用，或存在未恢复的不可恢复失败 |
| timeout 语义 | `probe()` 只做本地只读采样；若无法在 `timeout_ms` 内完成，返回结构化失败 `ProbeResult`，不得阻塞 LoggingFacade 主链 |
| detail_ref 约束 | 统一落到 `diag://infra/logging/health/...` 命名空间，供 diagnostics/log query 后续复用 |

补充冻结细节：`queue_high_watermark = max(1, active_logging_config.queue_size)`；direct-dispatch path 仍固定 `queue_high_watermark=1` 且 `queue_depth=0`。`dropped_total_delta >= 1`、`fallback_active=true`、`recovery_degraded=true`、`metrics_bridge_degraded=true` 或 `queue_depth >= queue_high_watermark` 一律视为 `Degraded`；只有 `unrecoverable_failure_total >= 1` 时才允许升级到 `Unhealthy`。sink unavailable 若已通过 degraded fallback 持久化，只能维持 `Degraded`，不得越权提升为 runtime recovery 结论。

冻结结果：LOG-BLK-003 只需要 logging 侧补齐 descriptor、状态映射和 timeout 语义文档，不需要再等待 health 子域补新的接口对象。

### 6.10.2 LogQueryService 查询与权限边界冻结

`LogQueryService` 的职责限定为“在本地已脱敏日志中，按 `trace_id` 或 `session_id` 生成受控 diagnostics artifact”。它不是通用检索引擎，不提供远程上传、全文搜索、任意 attr 扫描或跨模块二次授权。

| 项目 | 冻结结论 |
|---|---|
| 对外接口 | `query(const LogQueryRequest&, const LogQueryAccessContext&) -> LogQueryResult` |
| 查询对象 | `LogQueryRequest` 必填 `query_id`、`selector_kind`、`selector_value`、`start_ts_ms`、`end_ts_ms`、`max_records`；`selector_kind` 首版仅允许 `TraceId` 或 `SessionId`，且必须二选一 |
| 输入限制 | 只接受精确 selector + 有序时间窗；禁止 regex、全文检索、任意 attr filter、sort expression、cursor DSL 等自由查询语法 |
| 授权边界 | logging 不自行判定主体权限；调用方必须提供来自 `ISecurityPolicyManager`/Policy Gate 的 allow 证明，并在 access context 中携带 `actor_ref`、`consumer_module`、`policy_decision_ref` 与 `InfraContext` |
| 最小允许动作 | 仅接受 `PolicyDecision::Allow`；`RequireConfirmation` 与 `Deny` 均在 logging 边界外拒绝，不在本模块内二次确认或升级 |
| 配置 gate | 仅当 `infra.logging.export.enable_diag_pull == true` 时允许执行；该键只接受 默认/Profile/部署，runtime override 不可开启 |
| 返回结果 | `LogQueryResult` 只暴露 `artifact_ref`、`match_count`、`truncated`、`checksum`、`created_at`，失败只通过 `contracts::ResultCode` + `ErrorInfo` 暴露 |
| 导出约束 | 首版只生成本地 `diag://infra/logging/query/<query_id>` 或等价本地文件引用；不提供远程上传、长连接 streaming、跨文件游标分页或直接返回原始记录容器 |
| 与 diagnostics 关系 | diagnostics/export 若需远程导出，必须消费 `LogQueryResult.artifact_ref`；remote target allowlist 与导出格式策略继续由 diagnostics 子域持有 |
| 与 audit 关系 | 只复用日志中已存在的 `evidence_ref`/`audit_*` 关联字段，不开放对 audit 主存储的 join/export API |

冻结结果：LOG-BLK-005 的真实缺口是 query schema、allow 证明和导出限制未写成正式设计，而不是“按 trace/session 诊断拉取”能力本身不成立；后续实现可直接按 `LogQueryRequest` / `LogQueryAccessContext` / `LogQueryResult` 边界推进。

### 6.10.3 Production acceptance matrix 冻结补充

`docs/ssot/LoggingProductionAcceptanceMatrix.md` 现在是 logging production acceptance 的唯一 SSOT。当前阶段必须额外冻结以下结论，避免继续把骨架/fixture 结果误写成 production-ready：

1. v1 production primary backend 固定为 `spdlog-backed file / rotating sink`；`stderr + ringbuffer` 只作为 degraded fallback，不作为 primary owner。
2. build-tree 默认配置仍允许 `infra.logging.file.path=logs/runtime.log`，但 installed authoritative path 固定为 `state_root/logging/runtime.log`；packaged `state_root` 来自 `InstallLayout.state_root=/var/lib/dasall`，因此 canonical installed path 是 `/var/lib/dasall/logging/runtime.log`。
3. evidence level 统一按 matrix 解释：L1 只代表设计/SSOT 冻结，L2/L3 代表 build-tree focused/integrated evidence，L4 代表 local installed authoritative evidence，L5 只代表 packaging / release handoff。
4. 本轮只要求冻结 matrix，不要求宣称 code path 已 production-ready；local installed authoritative evidence 由后续 `INF-LOG-FIX-011` 产出 `logging-installed-proof.json` 与 `logging-runtime-proof.json` 后才能进入完成判定。
5. industry practice 只吸收结构：OpenTelemetry Logs 的 `TraceId / SpanId / Resource` correlation 字段分层，以及 spdlog 风格 async queue / overflow policy / rotation 语义；logging 仍不得越过 ADR-006 / ADR-007 / ADR-008。

冻结补充结论：本轮不把 qemu / kvm 作为 logging owner 当前验收前置；若后续存在 machine-isolated rerun，也只属于 packaging / release handoff，不改变 local installed authoritative evidence 的 owner 地位。

### 6.10.4 StructuredFormatter / RedactionFilter schema 冻结补充

`INF-LOG-FIX-002` 已把 logging 的 L2 focused schema 冻结到 `docs/ssot/LoggingProductionAcceptanceMatrix.md`：

1. `LoggingFacade` 默认主链固定为 `enrich -> redact -> format -> dispatch`；redaction/filter/formatter 不允许通过调用方可选分支绕过。
2. `StructuredFormatter` 首版固定 `schema_version=dasall.logging.event.v1`，并在 attrs 中补齐 `schema_version`、`correlation_id`、`idempotency_key`；`correlation_id` 优先级为 `trace_id -> request_id -> session_id -> task_id -> unknown`，`idempotency_key` tuple 固定为 `correlation_id|task_id|module|ts_ms`。
3. `RedactionFilter` 的 deny-by-default key fragments 固定为 `token`、`secret`、`password`、`authorization`、`api_key`、`apikey`；message/exception 文本模式最低必拦截集合固定为 `bearer `、`token=`、`token:`、`secret=`、`secret:`、`password=`、`password:`、`authorization=`、`authorization:`、`api_key=`、`apikey=`。
4. focused golden 证据固定为 `LoggingStructuredFormatterTest`、`LoggingRedactionFilterTest` 与 `LoggingFacadeRedactionIntegrationTest`；这些用例只证明 L2 focused schema/主链闭合，不外推为 sink/installed ready。

### 6.10.5 Installed / runtime log path 与权限策略冻结补充

`INF-LOG-FIX-003` 开始前，`BLK-INF-LOG-003` 必须先把 writable path / permission policy 冻结到 SSOT，防止 build-tree 临时路径和 installed authoritative path 再次混写：

1. `DASALL_STATE_ROOT` 是唯一允许的 state_root override；若设置该环境变量，installed/local-authoritative runtime log path 必须改写为 `${DASALL_STATE_ROOT}/logging/runtime.log`，不得另起 repo-relative pseudo install path。
2. build-tree focused slice 允许继续使用默认相对路径 `logs/runtime.log`，并将其解析到当前 build/test working directory；该路径只代表 L2/L3 build-tree evidence，不能回写成 installed/package smoke authoritative path。
3. installed authoritative runtime log path 固定为 `state_root/logging/runtime.log`；packaged canonical path 因 `InstallLayout.state_root=/var/lib/dasall` 而冻结为 `/var/lib/dasall/logging/runtime.log`，rotation family 固定在同目录 `runtime.log.<n>`。
4. primary file sink 只允许自动创建 build-tree `logs/` 与 installed/local-authoritative `state_root/logging/` 两类父目录；若遇到其他不可写、越界或权限拒绝路径，必须 fail-closed 返回 sink IO failure，不得 silently fallback 到 repo 根、`/tmp` 或 qemu guest-side 路径。
5. package smoke 与后续 installed proof 只接受 `state_root/logging/runtime.log` 及其 rotation family 作为 authoritative evidence；machine-isolated qemu / kvm rerun 只属于 packaging / release handoff，不进入当前 logging owner 验收。

### 6.10.6 FileLogSink / SinkDispatcher adapter 收口补充

`INF-LOG-FIX-003` 的实现目标不是把具体 backend 类型暴露给调用方，而是在当前 repo 依赖集内先闭合 file / rotation / fail-closed 这条最小 owner 行为链：

1. 新增 `ILogSink` public seam 与 `FileLogSink` 默认文件适配层；调用方只经由 `ILogSink::write()` / `flush()` 交互，不依赖具体 backend 类型。
2. `FileLogSink` 负责三类确定性行为：
  - build-tree focused 路径与 installed/state_root authoritative 路径解析；
  - `runtime.log.<n>` rotation family 维护；
  - 显式不可写/越界/父目录不可创建路径的 fail-closed sink IO failure。
3. `SinkDispatcher` 现支持按 `SinkRoute` 注入 basic/audit sink；若 route 对应 sink 未注入，则继续保留 skeleton 路径，不把该行为误写成 production-ready default persistence。
4. queue 语义在本轮仍只保留 bookkeeping / validation；async worker、flush deadline、drop/block policy 的 deterministic 行为继续留给 `INF-LOG-FIX-004`，避免把两个原子任务混成同一实现面。
5. focused 证据固定为 `FileLogSinkTest`、`SinkDispatcherRouteIntegrationTest`、`LoggingSinkFailureInjectionTest`；它们共同证明当前 adapter 已能承接 `LoggingFacade` 上游输出的 structured/redacted payload，并对 rotation 与 failure injection 给出可复验结果。

### 6.10.7 AsyncQueueController / LoggingFacade deterministic queue 收口补充

`INF-LOG-FIX-004` 已把 queue 行为从 bookkeeping 推进到 deterministic worker contract：

1. `AsyncQueueController` 现支持显式 `start()` / `stop()`、single worker drain、flush deadline 等待，以及 `processed_total`、`flush_timeout_total`、`blocked_write_attempt_total`、`dropped_total` 的单调计数。
2. `SinkDispatcher` 在存在 real sinks 时会注册 worker callback，由 queue worker 负责真正的 sink write；无 sink 注入时继续保留 skeleton queue，不把默认路径冒充 production persistence。
3. capacity 语义固定把 queue backlog 与 single worker in-flight slot 一并计入占用，因此 worker stuck 会对后续 log 产生 deterministic backpressure，而不是出现“queue 空但 sink 仍卡住”的假成功。
4. `LoggingFacade::flush()` 现对 drain success、deadline timeout、worker stuck 产生明确结果；`LoggingFacade::stop()` 会在进入 stopped 前先执行固定 shutdown deadline flush，使 shutdown drain 成为 public lifecycle contract。
5. focused 证据固定为 `AsyncQueueControllerWorkerTest`、`LoggingFlushDeadlineTest`、`LoggingBackpressureTest`；相邻回归继续以 `SinkDispatcherTest`、`SinkDispatcherRouteIntegrationTest`、`LoggingSinkFailureInjectionTest`、`LoggingFacadeTest` 证明 route、async sink failure observation 与 facade lifecycle 没有回退。

### 6.10.8 LoggingConfigAdapter / live composition config projection 收口补充

`INF-LOG-FIX-005` 已把 logging frozen config 真正接入 live composition：

1. `ObservabilityLiveCompositionOptions` 当前固定只暴露四个 logging projection 字段：`logging_level`、`logging_diag_pull_enabled`、`logging_config_entries`、`logging_state_root_override`。这意味着 `RuntimeLiveDependencyComposition` 当前只从 `RuntimePolicySnapshot` 投影 `ops_policy.log_level` 与 `remote_diagnostics_enabled`；其余 frozen logging keys 必须经 typed config entries 或 adapter fallback 进入主链，避免重新引入自由字典或隐式 schema 扩张。
2. `compose_live_observability()` 现先用 typed config 驱动 `LoggingConfigAdapter` 形成 active `LoggingConfig`，随后再依 `async_enabled` 选择 deterministic queue-backed `SinkDispatcher` 或 direct sink dispatcher，并把 `file_path` / `rotate_max_*` / `queue_size` / `overflow_policy` / `format` / `redaction_*` 真正投影到 `FileLogSink` 与 `LoggingFacade`。
3. `LoggingFacade` / `StructuredFormatter` / `RedactionFilter` 已补最小 config surface，因此 `format=json_line/key_value`、`redaction_enabled` 与 `redaction_ruleset` 不再停留在 adapter parse 阶段，而是成为 live logger 的实际运行参数。
4. `LoggingConfigAdapter::parse_uint32_value()` 现要求消费完整字符串；任何带 trailing junk 的 unsigned integer（如 `8192junk`、`50MB`）都必须 fail-closed，不允许静默吞尾缀后继续 apply。
5. focused 证据固定为 `LoggingConfigAdapterStrictParseTest`、`LoggingLiveCompositionConfigTest` 与 `DaemonRuntimeLiveDependencyCompositionTest`；其中 live composition test 直接验证 key-value formatter、redaction toggle、rotation family 与 queue-backed dispatcher 的投影行为。

### 6.10.9 LoggingRecovery / degraded fallback 收口补充

`INF-LOG-FIX-006` 已把 recovery/fallback 从 isolated fixture state machine 推进到 live logging 主链：

1. `LoggingFacade` 现显式持有 `LoggingRecovery`，并在三个 owner-safe 边界接入 recovery：formatter output 构建失败、primary dispatch 立即失败、deterministic queue flush/stop 暴露的 worker sink failure。logging 仍只产出 degraded/fallback/recovery advisory signal，不越过 ADR-007 去执行 Runtime recovery。
2. default degraded fallback sink 现固定为 `ringbuffer + stderr`。当 direct dispatch 或 queue flush 暴露 `LOG_E_SINK_IO` 时，`LoggingRecovery` 会把已结构化/已脱敏记录 replay 到 fallback sink，并把 degraded/fallback state 暴露给后续 `LoggingMetricsBridge` / `LoggingHealthProbe`；在 recovery 未被上层 owner 安排 retry cadence 之前，degraded 状态会持续保留而不是静默回切 primary。
3. queue saturation 继续沿用 `INF-LOG-FIX-004` 已冻结的 deterministic queue contract：`AsyncQueueController` 的 `RuntimeRetryExhausted` 语义不变，但 `LoggingRecovery` 现会把该 failure 投影成 `LOG_E_QUEUE_FULL` degraded fallback advisory payload，固定补齐 `recovery_advisory=queue_saturation` 与 `dropped_original_record=true`，从而显式区分“原始记录被 drop”与“fallback advisory 已持久化”。
4. formatter failure 现在通过 `LoggingFacade` 的 recovery seam 直接进入 `LoggingRecovery::handle_format_failure()`，并写出最小 fallback record；该最小记录保留 pre-format message，但剥离 formatter 生成 attrs，避免把半成品 structured payload 冒充成功格式化结果。
5. focused 证据固定为 `LoggingSinkFallbackTest`、`LoggingQueueFailureSignalTest` 与 `LoggingRecoveryIntegrationTest`；相邻回归继续由 `LoggingRecoveryTest`、`LoggingFacadeTest`、`LoggingFlushDeadlineTest` 与 `LoggingSinkFailureInjectionTest` 守住 queue deadline、direct facade lifecycle 与 raw sink failure observation 的既有边界。

### 6.10.10 AuditLinkAdapter / audit route / privacy split 冻结补充

`BLK-INF-LOG-007` 的真实缺口不是 `AuditLinkAdapter` 或 `SinkDispatcher` 完全不存在，而是 high-risk classifier、audit ref schema 与 privacy split 还没有被写成 owner-safe 设计结论。当前轮次冻结如下：

1. v1 high-risk classifier 只允许三条入口：`LogEvent.category() == "audit"`、`LogLevel::Fatal`，以及 attrs 显式声明 `event_kind=high_risk`。普通 `LogLevel::Error` 若没有这个显式 marker，必须继续走 ordinary log route，避免把大量错误级别日志静默升级成 audit owner persistence。
2. `AuditLinkAdapter::attach_audit_ref()` 的最小 `AuditRef` 冻结为 `evidence_ref.kind/ref + trace_id + task_id`。其中 `evidence_ref.kind` 只允许沿 `AuditEvidenceKind::{ToolResult, RecoveryOutcome, WorkerTask}` 映射到 `tool_result`、`recovery_outcome`、`worker_task`；缺任何一项都必须 fail-closed 返回 `ValidationFieldMissing`，且不允许在 `LogEvent.attrs` 留下部分 `audit_*` 片段。
3. ordinary log 面允许保留的 audit anchor attrs 固定为 `audit_ref_pending`、`evidence_ref`、`evidence_kind`、`audit_trace_id`、`audit_task_id`。它们只表达 route hint 与 correlation anchor，不新增 top-level `LogEvent` 字段，也不把 `AuditEvent` 本体投影成 logging 私有对象。
4. `IAuditLogger::write_audit()` 仍是 audit owner handoff 的唯一持久化入口；`actor`、`action`、`target`、`outcome`、`side_effects` 等完整 audit payload 必须继续停留在 audit owner persistence。logging 侧只保留 redacted message 与 correlation attrs，不对 audit 主存储开放 join/export 旁路。
5. 与 `docs/architecture/DASALL_infra_audit模块详细设计.md` 6.5 对齐，`target`、`evidence_ref.ref`、`side_effects` 在 v1 只承接结构化标识或 effect 名称，不得承载 access token、password、session id、原始文件路径或其他高敏感原文。即便某个 audit anchor attr 属于 allowlist，只要 value 命中 logging redaction 文本模式，仍必须保持 redacted。
6. `SinkDispatcher::select_route()` 后续只允许根据 `category==audit` 或完整 audit anchor attrs 做 route 判定；route selection 可以消费 `audit_ref_pending`/`evidence_ref` 这些 frozen anchor，但不能据此反向拼装 audit payload，更不能把 route 语义扩写成 audit persistence 已完成。
7. `LoggingFacade::log()` 的 build-track 实现现已固定为：ordinary log 先完成 `enrich -> redact -> format`，随后在 dispatch 前对 high-risk event fail-closed 执行 `IAuditLogger::write_audit()` handoff；`compose_live_observability()` 负责 attach concrete audit owner。`AuditLinkAdapter` 与 `SinkDispatcher` 继续只承担 correlation attrs 与 route 选择，不接管 audit payload persistence。

### 6.10.11 LogQueryService persisted artifact / retention / admin boundary 冻结补充

`BLK-INF-LOG-008` 的真实缺口不是 `LogQueryService` 完全不存在，而是 query artifact 的 default-disabled/admin-only 口径、retention cleanup 边界，以及 redaction-at-query 还没有被写成 owner-safe 设计结论。当前轮次冻结如下：

1. query artifact surface 继续 default-disabled/admin-only：`LogQueryService` 只接受来自 diagnostics/local artifact consumer 的显式调用，且调用方必须同时满足 `infra.logging.export.enable_diag_pull == true` 与 `PolicyDecision::Allow` 的完整 allow proof。daemon / CLI / installed command surface 若未显式启用，应继续返回 default-disabled/admin boundary，而不是把未开放 surface 混写成 query failure。
2. persisted reader 只允许读取本地 redacted runtime log 与同目录 rotation family；首版 selector 仍固定为精确 `trace_id` / `session_id` + 有序时间窗，不新增全文检索、任意 attr 扫描、cursor DSL、remote upload 或跨模块二次授权。
3. `diag://infra/logging/query/<query_id>` 仍只是 diagnostics local artifact 引用；build-track 允许 materialize 本地 artifact 文件与 owner-safe metadata index，但 index 只允许保存 `artifact_ref/query_id/selector_kind/selector_value/checksum/match_count/truncated/created_at` 这类摘要字段，不得额外缓存 raw record body、未脱敏 attrs 副本或 audit owner payload。
4. redaction-at-query 在本轮正式冻结：query artifact materialization 与 index write 必须对 matched record 再执行一次 redaction，保证 `message`、attrs，以及 `evidence_ref` / `audit_*` allowlist 字段中若命中 logging 文本模式，仍保持 redacted，而不是因为它们来自 persisted log 或 allowlist 就被豁免。
5. retention cleanup 只作用于 query artifact / index 自身：允许按 `created_at` retention window 与 max artifact count 清理过期 artifact 与陈旧 index entry，但不得删除、截断或重写 primary runtime log、rotation family、audit owner persistence 或 diagnostics retained snapshot store。
6. `INF-LOG-FIX-009` 的 focused build-track 证据后续固定为 `LogQueryServicePersistedReaderTest`、`LoggingDiagnosticsArtifactIntegrationTest` 与 `LogRetentionPolicyTest`；本轮 blocker 只闭合 L1 design / SSOT freeze，不把 default-disabled/admin-only 口径外推成 query artifact 已实现。

### 6.10.12 LogQueryService persisted reader / artifact index / retention 收口补充

`INF-LOG-FIX-009` 现已把 query artifact build-track 从 injected in-memory reader 骨架推进到真实 persisted reader/index/materialization/cleanup 主链：

1. `FileLogReader` 现实现 `ILogQueryRecordReader`，只读取本地 `runtime.log` 与同目录 rotation family，并解析 `StructuredFormatter` 产出的 `dasall.logging.event.v1` JSON-line；它仍只返回按有序时间窗过滤后的本地 redacted records，不新增全文检索、自由 attr filter、cursor DSL 或 remote upload。
2. `LogQueryService::query()` 现会在 allow proof 与 `infra.logging.export.enable_diag_pull` gate 通过后 materialize 本地 query artifact，而不再只返回摘要：artifact payload 固定落为本地 JSON 文件，artifact_ref 继续对外暴露为 `diag://infra/logging/query/<query_id>`，从而让 diagnostics/export consumer 继续只消费 artifact_ref 而不是 raw record container。
3. index publish 现固定为 owner-safe metadata JSONL：每个 entry 只保存 `artifact_ref`、`artifact_file_name`、`query_id`、`selector_kind`、`selector_value`、`checksum`、`match_count`、`truncated` 与 `created_at`。`LogQueryService` 不额外缓存 raw record body、未脱敏 attrs 副本或 audit owner payload。
4. redaction-at-query 已进入实现主链：artifact payload 与 metadata index publish 前都会对 matched record 再走一次 `RedactionFilter`，因此 `message`、attrs 与 `evidence_ref` / `audit_*` allowlist 字段中若再次命中 logging 文本模式，仍会保持 `<redacted>`，不会因为来源是 persisted log 就被豁免。
5. `LogRetentionPolicy::apply()` 现按 `created_at` retention window 与 max artifact count 清理 query artifact / index 自身；它只会删除 query artifact root 中过期或溢出的 artifact 文件，不会删除、截断或重写 primary runtime log、rotation family、audit owner persistence 或 diagnostics retained snapshot store。
6. focused 证据现固定为 `LogQueryServicePersistedReaderTest`、`LoggingDiagnosticsArtifactIntegrationTest` 与 `LogRetentionPolicyTest`；相邻回归继续使用 `LogQueryServiceTest` 与 `LogQueryIntegrationTest` 守住既有 exact selector + local artifact summary 契约。当前结论已到 L2/L3 build-tree persisted reader/index/materialization/cleanup evidence，但仍不外推成 installed/package authoritative evidence。

### 6.10.13 KeySubsystemLoggingFieldMatrix 冻结补充

`INF-LOG-SYS-FIX-001` 现已把跨子系统 logging 的 field/correlation 规则冻结到 `docs/ssot/KeySubsystemLoggingFieldMatrix.md`，用于解阻 `BLK-INF-LOG-009` 并约束后续 `INF-LOG-SYS-FIX-002~006` 与 `INF-LOG-FIX-010`：

1. cognition logging 只允许把 `stage`、`profile_id`、`request_id`、`trace_id`、`goal_id`、`decision_kind`、`confidence`、`selected_node_id`、`error_code`、`fallback_mode` 等 owner-safe 摘要字段投影为 ordinary log attrs；`clarification_question`、`response_summary`、raw prompt、raw `candidate_scores` 与 raw `payload_excerpt` 继续保持 redacted 或直接禁止落盘。
2. memory 当前虽然已经有 `ILogger`，但 `make_log_attrs()` 现阶段仍会把任意 field 透传到 attrs；后续 owner task 必须收紧为 allowlist，只保留 `request_id/session_id/trace_id/stage/profile_id` 与 bounded enum/count/bool 字段，禁止 raw context body、retrieval payload、summary text 与 embedding payload 进入 ordinary log。
3. knowledge 当前 primary/fallback logger 已经共享 `make_knowledge_log_event()`，因此本轮冻结 `request_id`、`component`、`snapshot_id`、`profile_id`、`query_kind`、`retrieval_mode`、`warning_summary`、`selected_corpora`、`reason_codes` 与 `telemetry_path` 为普通日志 attrs；raw `query/body`、ingest payload 与 corpus document text 继续留在 subsystem owner，不允许靠 fallback path 外泄。
4. runtime 当前只有 `RuntimeEventBus` / `RuntimeTelemetryBridge`，还没有 logger seam；后续 `RuntimeLoggingBridge` 只能投影 `RuntimeEventEnvelope` 中的 operational attrs，例如 `request_id`、`session_id`、`trace_id`、`turn_id`、`checkpoint_id`、`runtime_instance_id`、state/budget/recovery/safe-mode summary。`audit=true` envelope 继续交给 audit owner，logging 侧只写 redacted operational record。
5. services 当前只有 `ServiceAuditBridge`、`ServiceMetricsBridge` 与 `ServiceTraceBridge`；后续新增的 `ServiceLoggingBridge` 只允许写入 `request_id`、`capability_id`、`target_id`、`operation_name`、route metadata 与 outcome summary，禁止 raw `payload_json`、catalog/result body 或 adapter secret 落盘。现有 `request ledger` 不再充当 production logging 证据。
6. installed/package 侧统一冻结为 `logging-installed-proof.json.subsystems` 与 `logging-runtime-proof.json.subsystems`；每个 subsystem summary 至少要给出 `record_count`、`event_names`、`correlation_fields_present`、`redaction_proof`、`query_proof_ref`、`flush_observed` 与 `evidence_level`。当前 owner 验收上限仍是 local installed authoritative evidence，不外推到 qemu / kvm。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 统一入口接口 | 新增 logging 对外接口层 | 先稳定调用面，再实现细节 | infra/include/logging/ILogger.h | tests/unit/infra/logging/LoggingFacadeTest.cpp | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LoggingFacadeTest --output-on-failure | 无 |
| 结构化记录模型 | 新增 LogEvent/LogContext/AuditRef | 统一字段语义，防止调用方各自拼接 | infra/include/logging/LogTypes.h | tests/unit/infra/logging/LogTypesTest.cpp | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LogTypesTest --output-on-failure | 依赖 contracts 标识字段语义 |
| 异步多 sink | 新增 ILogSink + FileLogSink + SinkDispatcher + AsyncQueueController | 先闭合 backend-neutral route->sink adapter，再补 single worker drain、deadline flush 与 block policy 的 deterministic 语义 | infra/include/logging/ILogSink.h、infra/include/logging/FileLogSink.h、infra/src/logging/FileLogSink.cpp、infra/src/logging/SinkDispatcher.cpp、infra/src/logging/AsyncQueueController.cpp、infra/src/logging/LoggingFacade.cpp | tests/unit/infra/logging/FileLogSinkTest.cpp、tests/unit/infra/logging/AsyncQueueControllerWorkerTest.cpp、tests/unit/infra/logging/LoggingFlushDeadlineTest.cpp、tests/unit/infra/logging/LoggingBackpressureTest.cpp、tests/integration/infra/logging/SinkDispatcherRouteIntegrationTest.cpp、tests/integration/infra/logging/LoggingSinkFailureInjectionTest.cpp | cmake --build build-ci --target dasall_async_queue_controller_worker_unit_test dasall_logging_flush_deadline_unit_test dasall_logging_backpressure_unit_test dasall_sink_dispatcher_route_integration_test dasall_logging_sink_failure_injection_test && ctest --test-dir build-ci -R AsyncQueueControllerWorkerTest --output-on-failure && ctest --test-dir build-ci -R LoggingFlushDeadlineTest --output-on-failure && ctest --test-dir build-ci -R LoggingBackpressureTest --output-on-failure && ctest --test-dir build-ci -R SinkDispatcherRouteIntegrationTest --output-on-failure && ctest --test-dir build-ci -R LoggingSinkFailureInjectionTest --output-on-failure | 依赖已冻结 primary backend policy，但不向调用方暴露具体 backend |
| 审计协同 | 新增 AuditLinkAdapter 对接路径 | 明确 logging 不重复建设审计存储 | infra/src/logging/AuditLinkAdapter.cpp | tests/integration/infra/logging/AuditLinkIntegrationTest.cpp | cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R AuditLinkIntegrationTest --output-on-failure | 依赖 infra/audit IAuditLogger |
| 脱敏治理 | 新增 RedactionFilter 与 StructuredFormatter 主链 | 防止敏感数据明文落盘并固定结构化 schema | infra/src/logging/RedactionFilter.cpp、StructuredFormatter.cpp | tests/unit/infra/logging/LoggingRedactionFilterTest.cpp、tests/unit/infra/logging/LoggingStructuredFormatterTest.cpp、tests/integration/infra/logging/LoggingFacadeRedactionIntegrationTest.cpp | cmake --build build-ci --target dasall_logging_structured_formatter_unit_test dasall_logging_redaction_filter_unit_test dasall_logging_facade_redaction_integration_test && ctest --test-dir build-ci -R LoggingStructuredFormatterTest --output-on-failure && ctest --test-dir build-ci -R LoggingRedactionFilterTest --output-on-failure && ctest --test-dir build-ci -R LoggingFacadeRedactionIntegrationTest --output-on-failure | 规则集版本与 golden fixture 已由 INF-LOG-FIX-002 冻结 |
| sink 故障降级 | 接入 `LoggingRecovery` 到 `LoggingFacade` / deterministic queue failure 边界 | 保证 sink/format/queue failure 统一落到 degraded fallback 或 advisory signal，且不执行 Runtime recovery | infra/src/logging/LoggingRecovery.cpp、infra/src/logging/LoggingFacade.cpp | tests/unit/infra/logging/LoggingSinkFallbackTest.cpp、tests/unit/infra/logging/LoggingQueueFailureSignalTest.cpp、tests/integration/infra/logging/LoggingRecoveryIntegrationTest.cpp | Build_CMakeTools(buildTargets=["dasall_logging_recovery_integration_test","dasall_logging_sink_fallback_unit_test","dasall_logging_queue_failure_signal_unit_test"])；RunCtest_CMakeTools(tests=["LoggingRecoveryIntegrationTest","LoggingSinkFallbackTest","LoggingQueueFailureSignalTest"]) 命中仓库既有泛化“生成失败”后，fallback 直接执行对应 build/vscode-linux-ninja binaries，并补跑 `dasall_logging_recovery_unit_test`、`dasall_logging_facade_unit_test`、`dasall_logging_flush_deadline_unit_test`、`dasall_logging_sink_failure_injection_test` | 依赖 `INF-LOG-FIX-004` / `INF-LOG-FIX-005` 已冻结 deterministic queue contract 与 live config projection |
| 四层配置覆盖 | 接入 ILogConfigurator + ConfigCenter | 与 infra/config 一致，并由 logging 本地守住 audit 主链与 per-key 层级接受规则 | infra/include/logging/ILogConfigurator.h; infra/src/logging/LoggingConfigAdapter.cpp | tests/unit/infra/logging/LoggingConfigMergeTest.cpp; tests/contract/smoke/LogConfiguratorBoundaryContractTest.cpp | cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)" --output-on-failure | 依赖 infra/config 接口 |
| live composition config projection | 把 active LoggingConfig 接入 live logger 组装 | strict parse fail-closed + typed config live projection；最小 profile 投影仅限 `log_level` / `remote_diagnostics_enabled` | infra/include/ObservabilityLiveComposition.h、infra/src/ObservabilityLiveComposition.cpp、apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp、infra/src/logging/LoggingFacade.cpp、infra/src/logging/StructuredFormatter.cpp、infra/src/logging/RedactionFilter.cpp | tests/unit/infra/logging/LoggingConfigAdapterStrictParseTest.cpp、tests/integration/infra/logging/LoggingLiveCompositionConfigTest.cpp、tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp | Build_CMakeTools(buildTargets=["dasall_logging_config_adapter_strict_parse_unit_test","dasall_logging_live_composition_config_integration_test","dasall_access_daemon_runtime_live_dependency_composition_integration_test"])；RunCtest_CMakeTools(tests=["LoggingConfigAdapterStrictParseTest","LoggingLiveCompositionConfigTest","DaemonRuntimeLiveDependencyCompositionTest"]) 命中仓库既有泛化“生成失败”后，fallback 直接执行对应 build/vscode-linux-ninja binaries | 依赖 INF-LOG-FIX-004 已冻结 deterministic queue contract |
| 可观测指标桥接 | 新增 LoggingMetricsBridge | 保证 logging 自可观测 | infra/src/logging/LoggingMetricsBridge.cpp | tests/unit/infra/logging/LoggingMetricsBridgeTest.cpp | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LoggingMetricsBridgeTest --output-on-failure | 依赖 infra/metrics 接口 |
| 诊断拉取能力 | 接入 FileLogReader + LogQueryService local artifact/index/retention | 对齐 13.3 运维要求，把 query 从 in-memory skeleton 推进到 persisted reader / local artifact / owner-safe metadata index / cleanup | infra/src/logging/LogQueryService.cpp、infra/src/logging/FileLogReader.cpp | tests/unit/infra/logging/LogQueryServiceTest.cpp; tests/integration/infra/logging/LogQueryIntegrationTest.cpp; tests/unit/infra/logging/LogQueryServicePersistedReaderTest.cpp; tests/unit/infra/logging/LogRetentionPolicyTest.cpp; tests/integration/infra/logging/LoggingDiagnosticsArtifactIntegrationTest.cpp | Build_CMakeTools(buildTargets=["dasall_log_query_service_unit_test","dasall_log_query_integration_test","dasall_log_query_service_persisted_reader_unit_test","dasall_log_retention_policy_unit_test","dasall_logging_diagnostics_artifact_integration_test"])；RunCtest_CMakeTools(tests=["LogQueryServiceTest","LogQueryIntegrationTest","LogQueryServicePersistedReaderTest","LogRetentionPolicyTest","LoggingDiagnosticsArtifactIntegrationTest"]) 命中仓库既有泛化“生成失败”后，fallback 直接执行对应 build/vscode-linux-ninja binaries | 依赖 LOG-BLK-005 / BLK-INF-LOG-008 已解阻；远程导出仍由 diagnostics 持有 |
| 无法映射项：OTel exporter 直连 | 标记为后续版本任务 | 当前阶段先做字段兼容，不引入完整 OTel SDK | N/A | N/A | N/A | 阻塞：OTel SDK 依赖与部署链路尚未冻结 |

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

```text
infra/
  include/logging/
    ILogger.h
    ILogConfigurator.h
    ILogContextProvider.h
      IAuditLinkAdapter.h
    LogTypes.h
    LoggingErrors.h
  src/logging/
    LoggingFacade.cpp
    LogContextEnricher.cpp
    StructuredFormatter.cpp
    SinkDispatcher.cpp
    AsyncQueueController.cpp
   AuditLinkAdapter.cpp
    RedactionFilter.cpp
    LoggingRecovery.cpp
    LoggingConfigAdapter.cpp
    LoggingMetricsBridge.cpp
    LogQueryService.cpp
tests/
  unit/infra/logging/
  integration/infra/logging/
```

### 8.2 分阶段实施计划（最小可交付）

| 阶段 | 目标 | 关键动作 | 完成判定 |
|---|---|---|---|
| M0 设计冻结 | 固化接口、对象边界与 production acceptance matrix | 新增本详细设计文档、`LoggingProductionAcceptanceMatrix` 并评审通过 | 文档评审结论为 Pass |
| M1 最小日志闭环 | 支持普通结构化日志 | 新增 ILogger + LoggingFacade + basic file sink | 单元测试通过且可落盘 |
| M2 审计协同闭环 | 审计关联可追踪 | 新增 AuditLinkAdapter + evidence_ref 透传 | 与 infra/audit 联调通过 |
| M3 异步与降级 | 高负载与故障可恢复 | 引入 AsyncQueueController、fallback、degraded | 故障注入测试通过 |
| M4 配置与可观测 | 四层配置与指标联动 | 接入 LoggingConfigAdapter + MetricsBridge | 配置覆盖测试与指标测试通过 |
| M5 集成收口 | 多模块接入与回归 | runtime/tools/multi_agent 接入字段验证 | 集成测试通过，无边界违规 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| LOG-T001 | Not Started | 新增 logging 接口头文件 | 架构 5.10、蓝图 6 | include/logging/ILogger.h | LoggingFacadeTest | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LoggingFacadeTest --output-on-failure | 接口可编译且测试通过 |
| LOG-T002 | Not Started | 新增结构化对象定义 | 架构 8.5、蓝图 3.12 | LogTypes.h | LogTypesTest | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LogTypesTest --output-on-failure | 字段校验通过 |
| LOG-T003 | Not Started | 落地普通日志管线 | 详细设计 6.2/6.7 | LoggingFacade.cpp, SinkDispatcher.cpp | LoggingPipelineTest | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LoggingPipelineTest --output-on-failure | 正常写入通过 |
| LOG-T004 | Not Started | 落地审计协同适配器 | 架构 8.8、蓝图 3.12 | AuditLinkAdapter.cpp | AuditLinkIntegrationTest | cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R AuditLinkIntegrationTest --output-on-failure | 日志与审计 evidence_ref 关联可验证 |
| LOG-T005 | Not Started | 引入异步队列与溢出策略 | spdlog async 实践 | AsyncQueueController.cpp | AsyncQueueControllerTest | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R AsyncQueueControllerTest --output-on-failure | 队列满策略可判定 |
| LOG-T006 | Not Started | 补齐脱敏过滤器 | 工程规范 3.6 | RedactionFilter.cpp | RedactionFilterTest | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R RedactionFilterTest --output-on-failure | 敏感字段不明文 |
| LOG-T007 | Not Started | 实现 sink 故障恢复 | 架构 8.4、9.5 | LoggingRecovery.cpp | SinkFailureRecoveryIntegrationTest | cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R SinkFailureRecoveryIntegrationTest --output-on-failure | fallback 生效且可恢复 |
| LOG-T008 | Not Started | 接入四层配置适配 | 架构 8.6 | LoggingConfigAdapter.cpp | LoggingConfigMergeTest | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LoggingConfigMergeTest --output-on-failure | 配置覆盖符合优先级 |
| LOG-T009 | Not Started | 增加 logging 指标桥接 | 架构 8.7 | LoggingMetricsBridge.cpp | LoggingMetricsBridgeTest | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LoggingMetricsBridgeTest --output-on-failure | 指标完整可观测 |
| LOG-T010 | Not Started | 完成跨模块集成接入 | 蓝图依赖规则 | runtime/tools/multi_agent 调用点接入 | LoggingIntegrationPathTest | cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R LoggingIntegrationPathTest --output-on-failure | 关键链路均打点 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 维度 | 覆盖范围 | 核心用例 | Gate 建议 |
|---|---|---|---|
| 单元测试 | formatter、context enricher、redaction、queue policy | 字段完整性、脱敏正确性、溢出策略 | 必须 100% 通过 |
| 契约测试影响点 | contracts 标识字段消费一致性 | request_id/trace_id/task_id 映射正确 | 不新增 contracts 字段，消费一致 |
| 集成测试 | runtime/tools/multi_agent 与 logging 联动 | 主流程 + 多 Agent + Tool Audit | 关键路径全部通过 |
| 失败注入 | sink IO 故障、队列满、格式化异常 | fallback、生存性、degraded 状态 | 无崩溃，降级可恢复 |
| 兼容性检查 | Profile 差异、日志格式版本 | desktop_full vs edge_minimal 输出一致性 | 两档 profile 均通过 |

### 9.2 质量门建议清单

1. Gate-LOG-01：新增 logging 公共接口必须有 unit test。
2. Gate-LOG-02：审计日志不可与普通日志同文件。
3. Gate-LOG-03：写入失败必须有 metric 与错误码。
4. Gate-LOG-04：多 Agent 审计字段完整（trace_id/task_id/lease_id/parent_task_id）。
5. Gate-LOG-05：edge_minimal 下队列参数和落盘策略可运行。

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | 所有调用日志接口的模块 | 提供兼容 facade，先接入 ILogger 再逐步替换旧调用点 | 先在 simulator/daemon 灰度，再全模块启用 | 预留 OTel exporter、远程审计 sink |

### 10.1 风险分级说明

- 当前设计不修改 contracts 已冻结对象，故跨模块语义 breaking risk 为 Low。
- 若后续引入 OTel 原生 SDK 直连，可能上升为 Medium（部署与性能参数变化）。

### 10.2 演进路线

1. V1：完成本方案（异步多 sink + 审计隔离 + 四层配置）。
2. V2：接入 OTel logs exporter（可选），与 tracing/metrics 做 collector 统一出口。
3. V3：增加在线规则热更新与审计检索索引优化。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| third_party/spdlog 版本接口不一致 | LOG-T003/T005 | 明确版本与编译选项 | 固定 CMake 依赖版本并加编译探针 | 临时退回同步 basic sink |
| 审计目录权限或磁盘策略受限 | LOG-T004/T007 | 部署层提供独立可写目录 | 在部署配置中显式 audit.path | 审计进入 ringbuffer 并告警 |
| infra/config 接口未就绪 | LOG-T008 | 提供最小 IConfigCenter stub | 先做静态配置加载器 | 运行时覆盖能力暂缓 |
| failure injection 测试桩不足 | LOG-T007 | 增加 mock sink 与故障注入点 | 先补 mock sink 框架 | 仅保留基本错误计数，不做自动恢复 |
| 多模块接入节奏不一致 | LOG-T010 | 定义接入批次与责任人 | 先接 runtime 与 tools 主链路 | 其他模块保留适配层 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 监测信号 | 处置策略 |
|---|---|---|---|---|
| 异步队列阻塞主流程 | High | 高峰期写入暴涨 | queue_depth 持续高水位 | 调整 queue_size/overflow_policy，并按 docs/development/InfraConcurrencyPolicy.md 收敛 backpressure |
| 审计日志丢失 | High | audit sink 持续失败 | audit_fail_total 增长 | 强制 fallback + alert |
| 日志噪声过高 | Medium | level 配置过低 | 日志吞吐激增 | Profile 分层级别收敛 |
| 敏感信息泄漏 | High | 脱敏规则缺失 | 脱敏检测告警 | 扩展 ruleset 并加阻断测试 

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. 是否在 V1 即引入 span_id（当前 trace_id 必选，span_id 建议）。
2. 审计导出接口是否直接暴露给 tools/diagnostic，还是经 services 封装。
3. edge_minimal 场景下 audit 轮转保留策略的最小磁盘预算。
4. 是否需要为日志格式定义 schema version 字段（建议需要）。

### 12.2 后续任务建议

1. 发起 infra/logging 设计评审（含 runtime/tools/multi_agent 代表）。
2. 按 LOG-T001~LOG-T010 建立执行 TODO 并绑定责任人。
3. 在 build-ci 增加 logging 相关 unit/integration 目标与门禁。
4. 补充 docs/architecture 下 logging 实现追踪文档，记录 Design->Build 收敛证据。
