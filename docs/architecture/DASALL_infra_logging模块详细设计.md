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
| LOG-C016 | 工程蓝图 6 | Should | 对外暴露接口应遵循 infra/include/dasall/infra 统一接口分布 | 目录/接口 |
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

### 6.4 子组件依赖关系

- LoggingFacade -> LogContextEnricher -> RedactionFilter -> StructuredFormatter -> SinkDispatcher
- LoggingFacade -> AuditLinkAdapter（通过 IAuditLogger 协同，不承载审计存储）
- SinkDispatcher -> AsyncQueueController -> sink adapters
- SinkDispatcher/AsyncQueueController -> LoggingMetricsBridge/LoggingHealthProbe

依赖约束：全部通过 infra 内部接口连接，不直接依赖 runtime/cognition 实现类。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐 |
|---|---|---|---|
| LogContext | request_id, session_id, trace_id, task_id, parent_task_id, lease_id | 允许 unknown，不允许空字符串落盘 | 对齐 AgentRequest/WorkerTask/WorkerLease 的标识语义，仅消费 |
| LogEvent | level, category, message, attrs, timestamp | message 可空但 attrs 需可序列化 | 不新增 contracts 字段 |
| AuditRef | evidence_ref, trace_id, task_id | 用于日志与审计关联，不定义审计对象本体 | 引用 infra/audit 的 AuditEvent 语义 |
| SinkRoutePolicy | level->sink map, audit route | Profile 可覆盖 | 不进入 contracts |
| RedactionPolicy | ruleset_version, pattern_set | 配置可热更新 | 不进入 contracts |

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

后置条件：

- 成功写入或返回可判定失败码，并更新写入指标。

错误语义：

- LOG_E_QUEUE_FULL
- LOG_E_SINK_IO
- LOG_E_FORMAT_INVALID
- LOG_E_CONFIG_INVALID

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
4. 指标桥接失败只允许把 LoggingMetricsBridge 标记为 degraded，并通过 health/audit 或非递归 failure hook 暴露；不得递归调用 LoggingFacade 再写失败日志。
5. 追踪：记录 trace_id/span_id 兼容字段（若可获得 span_id 则写入）。
6. 审计协同：关键动作日志需附 evidence_ref，并通过 IAuditLinkAdapter 关联至 infra/audit。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 统一入口接口 | 新增 logging 对外接口层 | 先稳定调用面，再实现细节 | infra/include/logging/ILogger.h | tests/unit/infra/logging/LoggingFacadeTest.cpp | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LoggingFacadeTest --output-on-failure | 无 |
| 结构化记录模型 | 新增 LogEvent/LogContext/AuditRef | 统一字段语义，防止调用方各自拼接 | infra/include/logging/LogTypes.h | tests/unit/infra/logging/LogTypesTest.cpp | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LogTypesTest --output-on-failure | 依赖 contracts 标识字段语义 |
| 异步多 sink | 新增 SinkDispatcher + AsyncQueueController | 满足高并发与可配置溢出策略 | infra/src/logging/SinkDispatcher.cpp, AsyncQueueController.cpp | tests/unit/infra/logging/AsyncQueueControllerTest.cpp | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R AsyncQueueControllerTest --output-on-failure | 依赖 third_party/spdlog |
| 审计协同 | 新增 AuditLinkAdapter 对接路径 | 明确 logging 不重复建设审计存储 | infra/src/logging/AuditLinkAdapter.cpp | tests/integration/infra/logging/AuditLinkIntegrationTest.cpp | cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R AuditLinkIntegrationTest --output-on-failure | 依赖 infra/audit IAuditLogger |
| 脱敏治理 | 新增 RedactionFilter 与规则配置 | 防止敏感数据明文落盘 | infra/src/logging/RedactionFilter.cpp | tests/unit/infra/logging/RedactionFilterTest.cpp | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R RedactionFilterTest --output-on-failure | 需规则样例 |
| sink 故障降级 | 新增 fallback + degraded 状态机 | 保证写入失败可恢复 | infra/src/logging/LoggingRecovery.cpp | tests/integration/infra/logging/SinkFailureRecoveryIntegrationTest.cpp | cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R SinkFailureRecoveryIntegrationTest --output-on-failure | 需故障注入桩 |
| 四层配置覆盖 | 接入 ILogConfigurator + ConfigCenter | 与 infra/config 一致，并由 logging 本地守住 audit 主链与 per-key 层级接受规则 | infra/include/logging/ILogConfigurator.h; infra/src/logging/LoggingConfigAdapter.cpp | tests/unit/infra/logging/LoggingConfigMergeTest.cpp; tests/contract/smoke/LogConfiguratorBoundaryContractTest.cpp | cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)" --output-on-failure | 依赖 infra/config 接口 |
| 可观测指标桥接 | 新增 LoggingMetricsBridge | 保证 logging 自可观测 | infra/src/logging/LoggingMetricsBridge.cpp | tests/unit/infra/logging/LoggingMetricsBridgeTest.cpp | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LoggingMetricsBridgeTest --output-on-failure | 依赖 infra/metrics 接口 |
| 诊断导出能力 | 新增按 trace/session 拉取接口 | 对齐 9.4 运维要求 | infra/src/logging/LogQueryService.cpp | tests/integration/infra/logging/LogQueryIntegrationTest.cpp | cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R LogQueryIntegrationTest --output-on-failure | 需查询索引策略 |
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
| M0 设计冻结 | 固化接口与对象边界 | 新增本详细设计文档并评审通过 | 文档评审结论为 Pass |
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
