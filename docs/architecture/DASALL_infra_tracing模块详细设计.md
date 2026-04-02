# DASALL infra/tracing 模块详细设计（Detailed Design）

版本：v1.0  
日期：2026-03-24  
阶段：Detailed Design  
模块：infra/tracing

## 1. 模块概览

infra/tracing 属于 Infrastructure Layer（Layer 1），负责提供统一追踪能力：Span 生命周期管理、上下文传播、采样、导出与故障降级。该模块是可观测闭环中的 trace 信号层，不承担业务编排、失败语义判定、恢复裁定或最终响应生成。

来源依据：
1. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、3.7、3.8、5.10、8.7、9.5）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.2、4.3、5）
3. docs/adr/ADR-005-architecture-review-baseline.md
4. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
5. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
6. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
7. docs/plans/DASALL_contracts冻结实施计划.md
8. docs/todos/contracts/DASALL_contracts冻结TODO总表.md
9. docs/architecture/DASALL_infrastructure子系统详细设计.md
10. docs/development/DASALL_工程协作与编码规范.md
11. OpenTelemetry Trace API/SDK 规范（采样、并发、Shutdown/ForceFlush 语义）

## 2. 约束清单

### 2.1 Must / Should / Must-Not

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| TRC-C001 | DASSALL_Agent_architecture.md 3.4.7/5.10 | Must | infra 必须提供 trace 能力并与 logging/metrics/audit 协同 | 子组件、接口、流程 |
| TRC-C002 | DASSALL_Agent_architecture.md 3.7 + Blueprint 4.2 | Must | 依赖方向必须单向，infra 不反向依赖业务模块 | 依赖关系 |
| TRC-C003 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | 禁止 infra -> runtime/cognition/tools/memory 等业务实现依赖 | 目录与 include 关系 |
| TRC-C004 | DASALL_Engineering_Blueprint.md 4.3 | Must | 跨模块调用必须经过冻结 contracts 或稳定接口，不直连实现细节 | 接口语义 |
| TRC-C005 | ADR-005 | Must | contracts 与关键边界冻结前，不得用 tracing 设计反向改写架构结论 | 设计治理 |
| TRC-C006 | ADR-006 | Must-Not | tracing 不接管上下文语义装配和 Prompt 渲染，仅消费上下文标识并做追踪 | 职责边界 |
| TRC-C007 | ADR-007 | Must-Not | tracing 不做失败语义判断与恢复准入，只记录恢复相关 span 证据 | 异常语义 |
| TRC-C008 | ADR-008 | Must | tracing 不拥有主控调度权，只记录 orchestrator/coordinator/worker 链路 | 多 Agent 边界 |
| TRC-C009 | contracts 冻结计划 5/6 章 | Must-Not | 不把实现策略（采样器、批处理队列、导出协议）反写进 contracts 共享对象 | contracts 一致性 |
| TRC-C010 | contracts 冻结 TODO 总表 + M5 Gate | Must | 默认兼容优先：新增字段优先 optional，不做破坏式语义替换 | 演进策略 |
| TRC-C011 | 编码规范 3.6 | Must | 错误不能吞没，追踪失败必须可观测（日志/指标/审计） | 错误语义、恢复路径 |
| TRC-C012 | 编码规范 3.7 | Should | 新增公共 tracing 接口需同步 unit/integration/contract 测试 | 测试策略 |
| TRC-C013 | Blueprint 5 + 架构 7.5.1 | Must | Profile 仅可裁剪能力和替换实现，不能绕过 Audit 与 Runtime 主控链路 | 配置、发布 |
| TRC-C014 | OTel Trace SDK | Should | TracerProvider 配置、ForceFlush/Shutdown、采样与并发语义应与行业规范一致 | 接口语义、并发安全 |
| TRC-C015 | OTel Trace API | Should | TraceId/SpanId 语义、Span 生命周期与上下文传播应遵循 W3C Trace Context 兼容原则 | 核心对象 |

### 2.2 约束抽取结论

1. Must：分层单向依赖、trace 与其他信号协同、错误可观测、兼容优先。
2. Should：OTel 兼容语义、并发安全、可裁剪配置、测试门禁。
3. Must-Not：不越权到业务主控、不反向改写 ADR、不污染 contracts 共享语义。

## 3. 现状与缺口

### 3.1 现状识别

仓库现状（证据）：
1. infra/src/tracing/ 目录存在但为空。
2. infra/include/tracing 已落盘 ITracer、ITracerProvider、ISpan、ITraceContextPropagator、TraceTypes 与 TraceErrors 等公共头文件。
3. infra/CMakeLists.txt 当前已接入 core/tracing 锚点源码，不再是 placeholder-only 构建。
4. infra 子系统文档存在整体设计和 logging 模块详细设计，tracing 子模块详细设计缺失。

### 3.2 现状-目标差距表

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| 提供统一 ITracerProvider/ITracer 接口 | 部分实现 | ITracerProvider/ITracer 接口已冻结，provider/runtime 实现仍待补齐 | High | P0 |
| 支持 Span 生命周期（start/end/event/status） | 部分实现 | ISpan 与 TraceTypes 已冻结最小生命周期语义，真正追踪主链仍待实现 | High | P0 |
| 上下文传播（in-process + 跨模块） | 部分实现 | ITraceContextPropagator 与上下文对象已冻结，传播实现仍待补齐 | High | P0 |
| 采样策略（always_on/always_off/parent_based/ratio） | 缺失 | 高负载下无成本控制 | Medium | P1 |
| 导出与批处理（队列、flush、shutdown） | 缺失 | 结束时丢 span、故障不可控 | High | P0 |
| 异常降级（exporter 故障、队列满） | 缺失 | 追踪失效不可见 | High | P0 |
| 与 logging/metrics 关联 | 缺失 | 跨信号排障断裂 | Medium | P1 |
| 测试基线（unit/integration/failure） | 部分实现 | unit/contract 基线已接入，integration/failure 仍待补齐 | Medium | P0 |

### 3.3 风险冲突识别

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | tracing 侵入 runtime 恢复裁定逻辑 | 违反 ADR-007，形成双控制点 | High |
| 语义冲突 | tracing 自定义跨模块共享对象字段 | 与 contracts 冻结对象冲突 | High |
| 依赖反转 | 上层模块直接绑具体 exporter 实现 | 破坏 profile 裁剪与替换能力 | Medium |

## 4. 候选方案对比

### 4.1 候选方案

1. 方案 A：最小内存追踪（本地内存 ring buffer + 无远程导出）。
2. 方案 B：Provider-Processor-Exporter 分层方案（OTel 兼容语义，先实现本地/OTLP 可插拔）。
3. 方案 C：强绑定 OTel SDK 全量接入（直接依赖 OTel SDK 作为主实现）。

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| 方案 A 最小内存追踪 | 中 | 中 | 低 | 无持久导出、与运维诊断链路弱耦合 | 淘汰：仅适合 PoC |
| 方案 B 分层可插拔追踪 | 高 | 高 | 中 | 需要治理接口与批处理参数 | 保留并采纳：满足当前阶段平衡 |
| 方案 C 全量 OTel SDK 绑定 | 中高 | 高 | 高 | 引入成本与构建复杂度高，edge_minimal 压力大 | 暂不采纳：列为 v2 演进 |

### 4.3 行业调研结论（用于候选比较）

| 参考 | 关键结论 | 对 DASALL 的启示 |
|---|---|---|
| OTel Trace API | TracerProvider 持有配置，Tracer 负责创建 Span；Span 生命周期与上下文传播语义明确 | 采用 Provider/Tracer/Span 分层，不将配置散落到调用方 |
| OTel Trace SDK | 强调 ForceFlush/Shutdown、批处理队列、并发安全、采样策略 | 在 tracing 模块中必须显式定义 flush/shutdown 与批处理参数 |
| Jaeger/OTel 生态 | 远程采样与导出链路成熟，但部署依赖较重 | 先做接口预留与本地实现，逐步引入远程采样/collector |

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：Provider-Processor-Exporter 分层追踪方案（OTel 兼容语义，分阶段实现）。

### 5.2 放弃其他候选方案的理由

1. 放弃方案 A：不能满足长期诊断、导出与治理要求。
2. 放弃方案 C：当前仓库仍在骨架期，直接全量绑定 OTel SDK 会放大依赖和交付风险。

### 5.3 一致性说明

1. 与架构一致：满足 Infrastructure Layer 定位，输出 trace 能力并保持依赖单向。
2. 与 ADR 一致：不侵入 Context/Prompt、Reflection/Recovery、Orchestrator/Coordinator 边界。
3. 与 contracts 一致：仅消费冻结语义字段（request_id/session_id/trace_id/task_id/parent_task_id/lease_id），不反向扩写共享对象。

## 6. 详细设计

### 6.1 职责边界

职责：
1. 提供追踪埋点入口与 Span 生命周期管理。
2. 提供上下文传播与跨信号关联字段（trace_id/span_id）。
3. 提供采样、批处理导出、flush/shutdown 语义。
4. 在导出失败时输出可观测信号并触发降级。

非职责：
1. 不负责业务流程编排、恢复策略裁定、主状态机推进。
2. 不负责修改 contracts 公共语义对象。
3. 不直接承担日志持久化和指标聚合（只桥接关联）。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| TracingFacade | tracing 对外统一入口，提供 create/start/stop/flush |
| TracerProviderImpl | 持有 tracing 配置、采样器、processor、exporter 管线 |
| TracerImpl | 创建 Span 并管理 in-process 上下文绑定 |
| SpanImpl | Span 生命周期、属性/事件/状态记录 |
| ContextPropagationAdapter | 注入/提取 trace 上下文，桥接 runtime 与多线程执行环境 |
| SamplingPolicyEngine | 采样策略执行（always_on/off、parent_based、ratio） |
| SpanProcessorPipeline | OnStart/OnEnd 处理链，承接批处理与过滤 |
| BatchSpanBuffer | 批处理队列、触发导出、超时与背压管理 |
| SpanExporterAdapter | 可插拔导出器（noop/file/otlp 预留） |
| TraceHealthProbe | 追踪模块健康探针（队列、导出、丢弃状态） |
| TraceMetricsBridge | 输出追踪内部指标到 metrics 子模块 |
| TraceAuditBridge | 对关键追踪治理事件写审计（采样变更、导出连续失败） |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| TracingFacade | Runtime/各模块埋点请求 | TracerProviderImpl | 返回可判定成功/失败结果，不抛隐式业务异常 |
| TracerProviderImpl | TraceConfig/Profile 配置 | TracerImpl/处理管线 | 配置更新对新旧 tracing 实例生效策略显式定义 |
| TracerImpl | SpanStart 参数 + parent context | SpanImpl | 生成 trace_id/span_id，并保持上下文一致 |
| SpanImpl | attribute/event/status/end | SpanProcessorPipeline | end 后不可继续可变写入 |
| ContextPropagationAdapter | 上下文对象或 header map | TraceContext | 提供 inject/extract，支持 invalid/noop 场景 |
| SamplingPolicyEngine | span_name/kind/attrs/parent | SamplingDecision | 输出 DROP/RECORD_ONLY/RECORD_AND_SAMPLE |
| SpanProcessorPipeline | ended span | BatchSpanBuffer/Exporter | 不在 hot path 执行阻塞 I/O |
| BatchSpanBuffer | span batch | SpanExporterAdapter | 满队列策略可配置并可观测 |
| SpanExporterAdapter | batch spans | file/otlp/noop | export 失败可上报，不静默 |
| TraceMetricsBridge | pipeline stats | MetricsService | 输出 drop/export/latency 指标 |
| TraceAuditBridge | 关键治理事件 | AuditPipeline | 采样策略切换、导出退化必须审计 |

### 6.4 子组件依赖关系

1. TracingFacade -> TracerProviderImpl。
2. TracerProviderImpl -> SamplingPolicyEngine + SpanProcessorPipeline + SpanExporterAdapter。
3. TracerImpl -> SpanImpl + ContextPropagationAdapter。
4. SpanProcessorPipeline -> BatchSpanBuffer -> SpanExporterAdapter。
5. SpanProcessorPipeline/BatchSpanBuffer -> TraceMetricsBridge + TraceHealthProbe。
6. 关键配置变更与连续故障 -> TraceAuditBridge（写审计）。

约束：
1. 不允许 tracing 直接依赖 runtime/cognition 实现类。
2. 与 logging/metrics/audit 通过 infra 内部稳定接口协作。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | 对齐关系 |
|---|---|---|---|
| TraceContext | trace_id, span_id, trace_flags, trace_state, parent_span_id | trace_id/span_id 为空时必须是显式 invalid/noop 语义 | 消费 contracts 中 request/task 标识，不扩写 |
| SpanDescriptor | name, kind, start_ts, attrs, links | name 禁止高基数字段拼接 | 与 Observation/ToolResult 的引用关系通过 attrs 关联 |
| SpanEndResult | end_ts, status_code, status_message, dropped_attr_count | end 之后禁止再写入 | 错误语义映射到 infra 私有错误域 |
| SamplingDecision | decision, reason, sampler_desc | decision 必须可审计 | 不写入 contracts 共享对象 |
| ExportBatchReport | batch_size, success_count, failure_count, latency_ms | 失败必须可观测 | 供 metrics/audit 消费 |
| TraceModuleSnapshot | queue_depth, dropped_total, exporter_state, degraded | 周期性生成，供 health 使用 | 与 runtime 状态解耦 |

### 6.6 核心接口语义定义

建议头文件：infra/include/tracing/

1. ITracerProvider
- init(config): 初始化 provider、采样器和导出管线。
- get_tracer(scope): 返回 tracer 实例。
- force_flush(timeout_ms): 触发导出刷新。
- shutdown(timeout_ms): 关闭管线并返回状态。

2. ITracer
- start_span(descriptor, parent): 创建并启动 span。
- with_active_span(span, fn): 在作用域内注入 active span（RAII/回调式）。
- current_context(): 获取当前 trace context。

3. ISpan
- set_attribute(key, value)
- add_event(name, attrs)
- set_status(code, message)
- end(optional_end_ts)
- get_context()

4. ITraceContextPropagator
- inject(context, carrier)
- extract(carrier)

5. ISpanExporter
- export_batch(batch)
- force_flush(timeout_ms)
- shutdown(timeout_ms)

6. ITraceSampler
- should_sample(input) -> SamplingDecision
- description()

错误语义（infra/tracing 私有错误码域，向上映射 contracts::ResultCode）：
1. TRC_E_PROVIDER_NOT_READY
2. TRC_E_INVALID_CONTEXT
3. TRC_E_QUEUE_FULL
4. TRC_E_EXPORT_TIMEOUT
5. TRC_E_EXPORT_FAILURE
6. TRC_E_SHUTDOWN_TIMEOUT
7. TRC_E_CONFIG_INVALID

### 6.7 主流程时序

正常流程（单次工具调用链路示例）：
1. Runtime 进入某阶段时通过 TracingFacade 获取 tracer。
2. Tracer.start_span 创建 root 或 child span，并绑定 active context。
3. 子流程（tool/knowledge/worker）创建子 span，记录关键 attrs/events。
4. Span.end 后进入 SpanProcessorPipeline。
5. BatchSpanBuffer 聚合并按阈值/周期触发导出。
6. Export 成功后更新 metrics；失败则更新 failure 指标并触发健康降级判断。
7. Runtime 完成阶段时调用 force_flush（关键路径）或在 stop 时 shutdown。

### 6.8 异常与恢复时序

异常分类：
1. 上下文异常：parent context 无效或损坏。
2. 队列异常：batch 队列满。
3. 导出异常：export 超时/失败。
4. 关闭异常：shutdown 超时。

恢复动作：
1. 上下文异常：回退到 root/noop span，记录 TRC_E_INVALID_CONTEXT。
2. 队列满：按配置 block 或 drop_oldest，并上报 dropped_total。
3. 导出失败：切换 degraded_exporter（noop/file fallback）并告警审计。
4. shutdown 超时：返回失败状态，保留故障快照供诊断。

失败兜底：
1. 连续 N 次导出失败，进入 tracer_degraded_mode，仅保留本地最小追踪元数据与错误计数。
2. 不向上抛出不可控异常，统一返回可判定错误码。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| tracing.enabled | true | 默认/Profile/部署/运行时 | 是否启用 tracing |
| tracing.provider.type | internal | 默认/Profile | internal/otel_sdk(v2 预留) |
| tracing.sampler.type | parent_based_always_on | 默认/Profile/部署 | always_on/off/ratio/parent_based |
| tracing.sampler.ratio | 0.1 | Profile/部署 | ratio 生效阈值 |
| tracing.batch.enabled | true | 默认/Profile | 是否启用批处理 |
| tracing.batch.max_queue_size | 2048 | Profile/部署 | 队列上限 |
| tracing.batch.max_export_batch_size | 512 | Profile/部署 | 单批导出上限 |
| tracing.batch.schedule_delay_ms | 5000 | 默认/Profile | 定时导出间隔 |
| tracing.export.timeout_ms | 30000 | 默认/Profile/部署 | 导出超时 |
| tracing.exporter.type | noop | 默认/Profile/部署 | noop/file/otlp |
| tracing.exporter.otlp.endpoint | empty | 部署/运行时 | OTLP 地址（可选） |
| tracing.overflow.policy | drop_oldest | Profile/部署 | block/drop_oldest |
| tracing.force_flush_on_stop | true | 默认/Profile | 停机前强制刷新 |

### 6.10 可观测性（日志/指标/追踪/审计）

日志：
1. provider init/update/shutdown 生命周期日志。
2. 采样策略变更、导出失败、降级切换日志。

指标：
1. trace_span_started_total
2. trace_span_ended_total
3. trace_span_dropped_total
4. trace_export_success_total
5. trace_export_failure_total
6. trace_export_latency_ms
7. trace_batch_queue_depth
8. trace_context_invalid_total

追踪：
1. 对 tracing 自身关键操作（flush/export/reconfigure）可选自追踪 span。
2. 保证日志与审计记录包含 trace_id/span_id 关联字段。

审计：
1. 采样策略变更（谁、何时、从何值到何值）。
2. 连续导出失败触发 degraded 的事件。
3. shutdown 失败与强制回退行为。

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 建立 tracing 对外接口层 | 新增 ITracerProvider/ITracer/ISpan/ITraceContextPropagator | 先冻结调用面，避免上层直连实现 | infra/include/tracing/*.h | unit: TracingInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 依赖 contracts 既有标识语义 |
| 建立 provider + tracing + span 最小闭环 | 新增 TracerProviderImpl/TracerImpl/SpanImpl | 打通 start/end/context 的主链路 | infra/src/tracing/TracerProviderImpl.cpp 等 | unit: SpanLifecycleTest | ctest --test-dir build-ci -R SpanLifecycleTest --output-on-failure | 无 |
| 建立上下文传播能力 | 新增 ContextPropagationAdapter | 保证跨模块 trace 贯通 | infra/src/tracing/ContextPropagationAdapter.cpp | unit: TraceContextPropagationTest | ctest --test-dir build-ci -R TraceContextPropagationTest --output-on-failure | 依赖 carrier 统一格式 |
| 建立采样策略引擎 | 新增 SamplingPolicyEngine | 控制开销并支持 profile | infra/src/tracing/SamplingPolicyEngine.cpp | unit: SamplingPolicyTest | ctest --test-dir build-ci -R SamplingPolicyTest --output-on-failure | 阻塞：profile 采样键冻结 |
| 建立批处理导出链路 | 新增 SpanProcessorPipeline + BatchSpanBuffer + ExporterAdapter | 满足 flush/shutdown 与导出行为 | infra/src/tracing/SpanProcessorPipeline.cpp 等 | unit: BatchExportTest; integration: TraceExportIntegrationTest | ctest --test-dir build-ci -R "BatchExportTest|TraceExportIntegrationTest" --output-on-failure | 阻塞：OTLP exporter 选型 |
| 建立故障降级与可观测桥接 | 新增 TraceHealthProbe/TraceMetricsBridge/TraceAuditBridge | 失败可观测、可回退 | infra/src/tracing/TraceHealthProbe.cpp 等 | failure: TraceExporterFailureInjectionTest | ctest --test-dir build-ci -R TraceExporterFailureInjectionTest --output-on-failure | 依赖 infra/metrics 与 audit 接口 |
| 建立 CI Gate | 新增 tracing 相关 unit/integration/failure 标签测试 | 防止回归 | tests/unit/infra/tracing/*; tests/integration/infra/tracing/* | gate: tracing 标签全绿 | ctest --test-dir build-ci -L tracing --output-on-failure | 依赖 tests/CMakeLists 注册 |

无法立即映射项：
1. 全量 OTel SDK 深度绑定：当前阶段作为 v2 演进项，先保持接口兼容。
2. 远程采样服务（JaegerRemoteSampler）联动：受部署与安全策略阻塞，先预留接口。

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议落盘目录：
1. infra/include/tracing/
2. infra/src/tracing/
3. tests/unit/infra/tracing/
4. tests/integration/infra/tracing/
5. tests/contract/infra/（仅边界契约消费检查）

建议首批文件：
1. infra/include/tracing/ITracerProvider.h
2. infra/include/tracing/ITracer.h
3. infra/include/tracing/ISpan.h
4. infra/include/tracing/TraceTypes.h
5. infra/include/tracing/TraceErrors.h
6. infra/src/tracing/TracerProviderImpl.cpp
7. infra/src/tracing/TracerImpl.cpp
8. infra/src/tracing/SpanImpl.cpp
9. infra/src/tracing/ContextPropagationAdapter.cpp
10. infra/src/tracing/SamplingPolicyEngine.cpp
11. infra/src/tracing/SpanProcessorPipeline.cpp
12. infra/src/tracing/BatchSpanBuffer.cpp
13. infra/src/tracing/SpanExporterAdapter.cpp

### 8.2 分阶段实施计划

| 阶段 | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| TRC-M1 接口冻结 | Not Started | 新增并冻结 tracing 对外接口 | 架构 5.10 + 蓝图 6 | infra/include/tracing/*.h | TracingInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 接口可编译且无上层实现依赖 |
| TRC-M2 生命周期闭环 | Not Started | 补齐 provider/tracer/span 最小实现 | 本文档 6.2/6.7 | infra/src/tracing/core* | SpanLifecycleTest | ctest --test-dir build-ci -R SpanLifecycleTest | start/end/context 断言通过 |
| TRC-M3 采样与导出闭环 | Not Started | 增加采样引擎、批处理、导出器 | OTel SDK 语义 + 本文档 6.8 | infra/src/tracing/pipeline* | SamplingPolicyTest + BatchExportTest | ctest --test-dir build-ci -R "SamplingPolicyTest|BatchExportTest" | 采样与导出行为可判定 |
| TRC-M4 故障降级与桥接 | Not Started | 增加健康探针、指标桥接、审计桥接 | 编码规范 3.6 + infra 设计 6.10 | infra/src/tracing/bridge* | TraceExporterFailureInjectionTest | ctest --test-dir build-ci -R TraceExporterFailureInjectionTest | 导出失败可观测且有降级路径 |
| TRC-M5 集成与门禁 | Not Started | 接入 runtime/tools/multi_agent 埋点与 CI Gate | 蓝图依赖规则 + 测试规划 | tests/* + CMake 注册 | integration + contract | ctest --test-dir build-ci -L "tracing|integration|contract" | Gate 全绿且可重复执行 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| TRC-T001 | Not Started | 新增 ITracerProvider/ITracer/ISpan 接口 | 架构 5.10、蓝图 6 | infra/include/tracing/*.h | TracingInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 头文件可编译 |
| TRC-T002 | Not Started | 新增 TraceTypes 与错误码定义 | contracts 冻结计划 + 编码规范 3.6 | infra/include/tracing/TraceTypes.h, TraceErrors.h | TraceTypesTest | ctest --test-dir build-ci -R TraceTypesTest | 字段与错误码可判定 |
| TRC-T003 | Not Started | 实现 Span 生命周期最小闭环 | 本文档 6.7 | infra/src/tracing/SpanImpl.cpp | SpanLifecycleTest | ctest --test-dir build-ci -R SpanLifecycleTest | 生命周期语义通过 |
| TRC-T004 | Not Started | 实现 ContextPropagationAdapter | OTel Trace API | infra/src/tracing/ContextPropagationAdapter.cpp | TraceContextPropagationTest | ctest --test-dir build-ci -R TraceContextPropagationTest | inject/extract 正确 |
| TRC-T005 | Not Started | 实现 SamplingPolicyEngine | OTel SDK Sampling | infra/src/tracing/SamplingPolicyEngine.cpp | SamplingPolicyTest | ctest --test-dir build-ci -R SamplingPolicyTest | 采样决策符合预期 |
| TRC-T006 | Not Started | 实现 BatchSpanBuffer 与导出适配器 | OTel SDK Batch Processor | infra/src/tracing/BatchSpanBuffer.cpp, SpanExporterAdapter.cpp | BatchExportTest | ctest --test-dir build-ci -R BatchExportTest | 队列与导出行为可复现 |
| TRC-T007 | Not Started | 补齐失败降级与健康探针 | 编码规范 3.6 + infra 健康设计 | infra/src/tracing/TraceHealthProbe.cpp | TraceExporterFailureInjectionTest | ctest --test-dir build-ci -R TraceExporterFailureInjectionTest | 故障注入后降级成功 |
| TRC-T008 | Not Started | 增加 tracing 指标桥接 | 架构 8.7 | infra/src/tracing/TraceMetricsBridge.cpp | TraceMetricsBridgeTest | ctest --test-dir build-ci -R TraceMetricsBridgeTest | 指标完整且稳定 |
| TRC-T009 | Not Started | 增加 tracing 审计桥接 | 架构 8.5/9.5 + ADR-007 | infra/src/tracing/TraceAuditBridge.cpp | TraceAuditBridgeTest | ctest --test-dir build-ci -R TraceAuditBridgeTest | 审计事件完整 |
| TRC-T010 | Not Started | 完成跨模块集成埋点接入 | 蓝图依赖矩阵 | runtime/tools/multi_agent 调用点接入 | TracingIntegrationPathTest | ctest --test-dir build-ci -R TracingIntegrationPathTest | 关键路径 trace 连通 |

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | provider/tracer/span/context/sampler/batch/exporter | Span 生命周期、上下文传播、采样决策、队列策略 | 全部断言通过，错误码可判定 |
| Contract | tracer 对 contracts 字段消费边界 | request_id/trace_id/task_id/lease_id 映射一致性 | 不新增 contracts 字段，不越权 |
| Integration | runtime/tools/multi_agent 与 tracer 联动 | 主链路 trace 连通、跨子任务 parent-child 关系 | trace tree 连续且可查询 |
| Failure Injection | exporter 失败、队列满、shutdown 超时 | 降级切换、drop 计数、审计记录 | 每类故障有可观测证据 |
| Compatibility | profile 档位差异 | desktop_full/edge_balanced/edge_minimal 行为一致性 | 无 breaking 行为 |

### 9.2 质量 Gate 建议清单

| Gate ID | 检查项 | 失败判定 |
|---|---|---|
| TRC-G1 | tracing unit tests 全绿 | 任一 unit 失败即阻断 |
| TRC-G2 | tracing integration tests 全绿 | 任一 integration 失败即阻断 |
| TRC-G3 | failure injection 覆盖导出失败/队列满/上下文异常 | 任一故障无兜底动作即阻断 |
| TRC-G4 | contracts 边界检查通过 | 出现越权字段或语义漂移即阻断 |
| TRC-G5 | profile 兼容检查通过 | 任一 profile 行为不一致即阻断 |

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | runtime/tools/memory/knowledge/multi_agent 调用方 | 先提供 v1 接口 + 默认实现，逐步替换 placeholder 调用 | desktop_full 先灰度，稳定后推广到 edge_balanced，再到 edge_minimal | 预留 OTel SDK provider、remote sampler、OTLP exporter |
| Medium（接口签名变更时） | 全部 tracer 消费方 | 采用 v1/v2 并存 + 适配器迁移窗口 | 双写埋点与对比验证后切换 | 预留 TraceSchemaVersion 与字段映射器 |

演进原则：
1. 默认向后兼容，新增能力优先新增接口或 optional 字段。
2. breaking 变更必须先评审并给迁移脚本/适配层。
3. 先本地闭环，后远程导出与远程采样。

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理字段表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| B-TRC-01 exporter 选型未冻结（noop/file/otlp） | TRC-T006/TRC-M3 | 明确首版 exporter 组合与依赖策略 | 先实现 noop + file exporter 打通闭环 | 暂缓 OTLP，仅保留接口预留 |
| B-TRC-02 profile 采样配置键未统一 | TRC-T005/TRC-M3 | profiles 中 tracing.sampler.* 键冻结 | 先固定 parent_based_always_on 默认策略 | 采样策略热更新延后 |
| B-TRC-03 跨线程上下文传播载体未统一 | TRC-T004/TRC-T010 | runtime 线程上下文封装约定达成 | 先支持 in-process explicit context 传递 | 不做隐式 TLS 传播 |
| B-TRC-04 integration 环境缺少稳定导出端 | TRC-T010/TRC-M5 | 提供可控 mock exporter/collector | 先完成 unit + failure injection gate | 集成门禁延期到下一迭代 |
| B-TRC-05 contracts 边界字段仍在微调 | TRC-T002/TRC-T010 | contracts M5 基线确认不再变动 | 先封装字段映射适配层 | 若变更发生，回退到适配层修复，不改主接口 |

### 11.2 主要风险与回退策略

| 风险 | 级别 | 触发信号 | 回退策略 |
|---|---|---|---|
| 导出链路抖动导致大量丢 span | High | trace_export_failure_total 持续上升 | 切换到 file exporter + 降低采样率 |
| 高并发下队列溢出影响延迟 | Medium | trace_batch_queue_depth 长时间高位 | 改为 block 策略并下调埋点密度 |
| 接口设计不稳定导致上层返工 | Medium | 多模块频繁改签名 | 引入 v1/v2 适配层并冻结核心语义 |
| 多 Agent 链路 parent-child 关系混乱 | Medium | trace tree 出现断裂/孤儿 span | 强制 parent_task_id/lease_id 校验与补齐 |

## 12. 未决问题与后续任务

### 12.1 未决问题

1. 首版是否默认启用 OTLP exporter，还是仅保留 file/noop。
2. edge_minimal 档位下的默认采样率和批处理参数是否单独收敛。
3. runtime 的跨线程上下文容器最终采用显式参数透传还是 TLS 封装。
4. tracing 与 infra/logging 的字段字典（attr key）是否建立统一注册表。

### 12.2 后续任务建议

| ID | 优先级 | 任务描述 | 输入依据 | 完成判定 |
|---|---|---|---|---|
| TRC-NEXT-01 | P0 | 收敛 tracer 最小接口并提交评审 | 本文档 6.6 + 编码规范 | 评审结论 Pass |
| TRC-NEXT-02 | P0 | 实现 Span 生命周期与上下文传播最小闭环 | 本文档 6.7/6.8 | unit 测试全绿 |
| TRC-NEXT-03 | P1 | 引入采样和批处理导出并建立故障注入测试 | 本文档 6.9 + 9.1 | failure injection 全绿 |
| TRC-NEXT-04 | P1 | 与 runtime/tools/multi_agent 完成关键链路埋点对齐 | ADR-008 + 设计 6.5 | 集成链路 trace 连通 |
| TRC-NEXT-05 | P2 | 评估 OTel SDK 深度接入（v2）并形成 ADR 补充 | 行业调研 + 工程成本评估 | 决策文档冻结 |
