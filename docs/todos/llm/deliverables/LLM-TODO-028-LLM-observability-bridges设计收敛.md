# LLM-TODO-028 LLM observability bridges 设计收敛

日期：2026-04-13
任务：LLM-TODO-028
状态：D Gate PASS / B Gate READY

## 1. 本地证据

1. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 028 固定为 [llm/src/observability/LLMTraceBridge.h](../../../../llm/src/observability/LLMTraceBridge.h)、[llm/src/observability/LLMTraceBridge.cpp](../../../../llm/src/observability/LLMTraceBridge.cpp)、[llm/src/observability/LLMMetricsBridge.h](../../../../llm/src/observability/LLMMetricsBridge.h)、[llm/src/observability/LLMMetricsBridge.cpp](../../../../llm/src/observability/LLMMetricsBridge.cpp)、[llm/src/observability/LLMAuditBridge.h](../../../../llm/src/observability/LLMAuditBridge.h)、[llm/src/observability/LLMAuditBridge.cpp](../../../../llm/src/observability/LLMAuditBridge.cpp) 与两组 observability 单测，不要求本轮扩 shared contracts 或改写 manager owner。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.12 已冻结 17 个最小日志字段、12 个建议指标、6 个 span 与 6 类建议 audit 场景；9.6 明确 O Gate 通过条件是关键日志、指标、追踪、审计字段完整可观测。
3. [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 已在 024 成功路径把 `route=`、`selection_reason=`、`provider_trace_id=`、`audit=`、`reasoning_content_stripped=true` 与 `usage:*` tags 追加到 [contracts/include/llm/LLMResponse.h](../../../../contracts/include/llm/LLMResponse.h) 的 provider-neutral tags，028 应消费这些稳定事实，而不是再扩 shared `LLMResponse` 面。
4. [tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp](../../../../tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp) 目前只覆盖 023 的 usage/cost anchor，可作为 028 扩展日志/指标/trace 字段完整性的现成验收入口；[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt) 也已经为这类 llm unit target 提供标准注册模式。
5. [infra/include/metrics/MetricTypes.h](../../../../infra/include/metrics/MetricTypes.h) 已冻结指标标签只允许 `module`、`stage`、`profile`、`outcome`、`error_code` 五个键；因此 028 不能把 route/model/reason/provider/profile 扩成第二套 labels，而要在 metric family + stage token 中做稳定投影。
6. [infra/include/logging/ILogger.h](../../../../infra/include/logging/ILogger.h)、[infra/include/metrics/IMetricsProvider.h](../../../../infra/include/metrics/IMetricsProvider.h)、[infra/include/tracing/ITracer.h](../../../../infra/include/tracing/ITracer.h) 与 [infra/include/audit/IAuditLogger.h](../../../../infra/include/audit/IAuditLogger.h) 已提供标准 sink seam；[infra/src/watchdog/WatchdogMetricsBridge.h](../../../../infra/src/watchdog/WatchdogMetricsBridge.h) 与 [infra/src/tracing/TraceAuditBridge.h](../../../../infra/src/tracing/TraceAuditBridge.h) 给出了仓库内 bridge-local result/status/fail-closed 模式，可直接复用。
7. [contracts/include/error/ResultCode.h](../../../../contracts/include/error/ResultCode.h) 已冻结 `classify_result_code()` 与 `result_code_category_name()`；[llm/include/LLMManagerResult.h](../../../../llm/include/LLMManagerResult.h) 已冻结 `LLMFailureCategory`。028 应使用这两层稳定枚举生成 `error_type` 与 `failure_category` 观测字段，而不是引入 provider-private 失败码。

## 2. 外部参考

1. OpenTelemetry Metrics API 强调 Counter/Histogram 应使用稳定的 instrument name 与 unit，并建议把高频业务计数放在同步 instrument 上；同时 instrument/attribute 需要保持 ASCII 兼容与低基数约束。028 因此将 `llm_calls_total`、`llm_fallback_total`、`llm_prompt_cache_*_tokens_total`、`llm_cost_estimate_usd_total` 收敛为 Counter，把 `llm_call_latency_ms` 收敛为 Histogram，并把高维 route/model/reason 信息压缩到稳定 stage token，而不是继续扩 label 面。
2. OpenTelemetry Tracing API 强调 span name 需要使用低基数、可概括的操作名，且 instrumentation library 在无错误时通常保持 `Unset`，仅在错误时置 `Error`。028 因此把 span 名固定为 `llm.prompt.select`、`llm.prompt.compose`、`llm.prompt.policy`、`llm.route.resolve`、`llm.adapter.invoke`、`llm.response.normalize` 六个常量，属性承载 route/model/reasoning/audit 字段，失败时才设置 `SpanStatusCode::Error`。

## 3. Design 结论

1. 028 只新增 module-local observability bridge，不扩 shared contracts，也不在本轮改写 [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 的 owner。bridge 输入面收敛为三个 module-local signal：
   - `LLMCallSummary`：日志 + metrics 的统一摘要输入。
   - `LLMTraceSpanSignal`：六个 stage span 的统一 trace 输入。
   - `LLMAuditEvent`：trusted source 失败、reasoning_content 剥离、metadata drift 三类审计事件输入。
2. `LLMMetricsBridge` 同时承担两件事：
   - 写一条 call summary structured log。
   - 向 metrics sink 发出 12 个 frozen metric family 中的相关样本。
   本轮不再额外新增 `LLMLoggingBridge`，原因是专项 TODO 的代码目标只冻结了 trace/metrics/audit 三个 bridge；为避免额外扩大文件面，日志由 metrics bridge 内部复用 `ILogger` 完成 fire-and-forget 输出。
3. `LLMCallSummary` 的字段面直接覆盖 6.12.1 的 17 个日志字段，并补齐 028 验收要求中的 `error_type`、`provider_id`、`profile_id`、`outcome`、`from_route`、`to_route` 等 bridge-local 投影字段。日志是“字段完整性 owner”，metrics 只保留低基数聚合投影，trace 保留结构化上下文，audit 记录高价值治理事件。
4. 由于 [infra/include/metrics/MetricTypes.h](../../../../infra/include/metrics/MetricTypes.h) 的 labels 被硬冻结为五元组，指标投影规则固定如下：
   - `module = "llm"`
   - `profile = profile_id`，为空时回退 `unknown`
   - `outcome = success/degraded/failure/rejected`
   - `error_code = none` 或 contracts/metrics 派生错误 token
   - `stage = <metric-scope>/<normalized-stage>/<stable-dimension-token>`
   其中 route/model/reason/provider 等维度统一编码进 stage token，避免扩 label cardinality。
5. `LLMMetricsBridge` 冻结 12 个 metric family：
   - `llm_calls_total`：Counter / `1`
   - `llm_call_latency_ms`：Histogram / `ms`
   - `llm_fallback_total`：Counter / `1`
   - `llm_model_selection_total`：Counter / `1`
   - `llm_reasoning_escalation_total`：Counter / `1`
   - `prompt_policy_deny_total`：Counter / `1`
   - `prompt_compose_over_budget_total`：Counter / `1`
   - `llm_adapter_timeout_total`：Counter / `1`
   - `llm_health_degraded_total`：Counter / `1`
   - `llm_prompt_cache_hit_tokens_total`：Counter / `1`
   - `llm_prompt_cache_miss_tokens_total`：Counter / `1`
   - `llm_cost_estimate_usd_total`：Counter / `usd`
6. `LLMTraceBridge` 对每个 `LLMTraceSpanSignal` 只创建一个已完成 span，不在 bridge 内管理长生命周期 active span。这样 028 可以在 unit test 里直接断言 descriptor/attributes/status/end timestamp，同时满足 T-015 的“fire-and-forget，不在 adapter I/O 锁内执行”。`llm.adapter.invoke` 使用 `SpanKind::Client`，其余 span 统一 `SpanKind::Internal`。
7. trace 属性保留完整但 provider-neutral 的 call facts：`request_id`、`llm_call_id`、`stage`、`resolved_route`、`model_name`、`prompt_id`、`prompt_version`、`fallback_used`、`latency_ms`、`failure_category`、`error_type`、`selection_reason_codes`、`reasoning_mode_requested`、`reasoning_mode_effective`、`estimated_input_tokens`、`prompt_cache_hit_tokens`、`prompt_cache_miss_tokens`、`actual_cost_estimate_usd`。成功 span 保持 `Unset`，失败 span 设 `Error`。
8. `LLMAuditBridge` 本轮只冻结三类 audit kind：
   - `TrustedSourceFailure`
   - `ReasoningContentStripped`
   - `MetadataDrift`
   其中 trusted source 失败映射为 `AuditOutcome::Rejected`，其余两类映射为 `AuditOutcome::Escalated`。这样既覆盖 028 验收要求，又不抢占 allowlist deny / cross-provider fallback 等后续 integration 任务空间。
9. 三个 bridge 都采用 fail-closed + local degraded status：sink 缺失、identity 非法、payload 缺字段时，只在 bridge 本地保留 failure/degraded 状态，不反向污染 llm 主调用结果。后续主链接线时，observability 失败依旧保持 best-effort，不获得调用准入裁定权。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 llm 日志/metrics 的统一摘要输入、12 个指标族与日志字段完整性 | [llm/src/observability/LLMMetricsBridge.h](../../../../llm/src/observability/LLMMetricsBridge.h)、[llm/src/observability/LLMMetricsBridge.cpp](../../../../llm/src/observability/LLMMetricsBridge.cpp)、[tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp](../../../../tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp) |
| 冻结六个低基数 span 名、属性投影与错误状态规则 | [llm/src/observability/LLMTraceBridge.h](../../../../llm/src/observability/LLMTraceBridge.h)、[llm/src/observability/LLMTraceBridge.cpp](../../../../llm/src/observability/LLMTraceBridge.cpp)、[tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp](../../../../tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp) |
| 冻结 trusted source 失败、reasoning_content 剥离、metadata drift 三类 audit 事件 | [llm/src/observability/LLMAuditBridge.h](../../../../llm/src/observability/LLMAuditBridge.h)、[llm/src/observability/LLMAuditBridge.cpp](../../../../llm/src/observability/LLMAuditBridge.cpp)、[tests/unit/llm/LLMAuditEventCoverageTest.cpp](../../../../tests/unit/llm/LLMAuditEventCoverageTest.cpp) |
| 把 observability 源文件纳入 llm 静态库，并把新增单测注册进 llm unit target | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：新增 `LLMMetricsBridge`、`LLMTraceBridge`、`LLMAuditBridge` 三个 module-local observability bridge，完成日志/metrics/trace/audit 的最小 provider-neutral 投影与本地 degraded status。
2. 测试目标：
   - `LLMObservabilityFieldCompletenessTest` 扩展为断言 17 个关键字段在日志/metrics/trace 上都有稳定投影，并保留既有 usage/cost anchor 回归。
   - `LLMAuditEventCoverageTest` 新增 trusted source 失败、reasoning_content 剥离、metadata drift 三类审计事件覆盖。
3. 验收命令：
   - `cmake --build build-ci --target dasall_llm_observability_field_completeness_unit_test dasall_llm_audit_event_coverage_unit_test`
   - `ctest --test-dir build-ci -R "LLM(ObservabilityFieldCompleteness|AuditEventCoverage)Test" --output-on-failure`

## 6. 风险与回退

1. 028 只冻结 bridge 与 unit-testable signal contract，不在本轮把 observability 真正织入 [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 的 runtime hot path；真正的主链闭环仍交由 029 smoke integration 验证。这是 architecture ready / implementation not yet fully wired 的有意切分，不是假装现状已完成整链接线。
2. metrics labels 由于 infra 限制只能保留五元组，route/model/reason/provider/profile 的全量维度不会全部进入 metrics labels；完整字段以 structured log 和 trace attrs 为准，metrics 只保留低基数聚合投影。
3. 028 不新增专门的 logging bridge，若后续 observability 规模继续扩大，需要再评估是否把 `LLMMetricsBridge` 内的 structured log 能力拆出为单独 owner；在此之前，日志与 metrics 共享同一摘要输入可减少重复字段组装与测试面。
4. metadata drift 只记录“发现漂移”的审计事实，不负责触发 catalog 回滚、profile 切换或 provider 下线；这些动作仍归后续 catalog / runtime / recovery 任务 owner。