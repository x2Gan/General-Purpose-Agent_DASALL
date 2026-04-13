#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "support/TestAssertions.h"

#include "tracing/ISpan.h"

#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/observability/LLMMetricsBridge.h"
#include "../../../llm/src/observability/LLMTraceBridge.h"

#include "ModelRouterTestSupport.h"

namespace {

using dasall::llm::AdapterUsageFragment;
using dasall::llm::UsageAggregator;
using dasall::llm::observability::LLMCallSummary;
using dasall::llm::observability::LLMMetricsBridge;
using dasall::llm::observability::LLMTraceBridge;
using dasall::llm::observability::LLMTraceSpanKind;
using dasall::llm::observability::LLMTraceSpanSignal;
using dasall::llm::provider::ProviderModelMetadata;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class ScriptedLogger final : public dasall::infra::logging::ILogger {
 public:
  dasall::infra::logging::LogWriteResult log(
      const dasall::infra::logging::LogEvent& event) override {
    events.push_back(event);
    if (!scripted_results.empty()) {
      const auto result = scripted_results.front();
      scripted_results.pop_front();
      return result;
    }

    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  void set_level(dasall::infra::logging::LogLevel level) override {
    last_level = level;
  }

  std::deque<dasall::infra::logging::LogWriteResult> scripted_results;
  std::vector<dasall::infra::logging::LogEvent> events;
  dasall::infra::logging::LogLevel last_level =
      dasall::infra::logging::LogLevel::Info;
};

class ScriptedMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    recorded_samples.push_back(sample);
    if (!scripted_results.empty()) {
      const auto result = scripted_results.front();
      scripted_results.pop_front();
      return result;
    }

    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://llm/record");
  }

  std::deque<dasall::infra::metrics::MetricsOperationStatus> scripted_results;
  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class ScriptedProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit ScriptedProvider(std::shared_ptr<ScriptedMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://llm/provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://llm/provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://llm/provider-shutdown");
  }

  dasall::infra::metrics::MeterScope last_scope{};

 private:
  std::shared_ptr<ScriptedMeter> meter_;
};

struct RecordedEvent {
  std::string name;
  dasall::infra::tracing::TraceAttributeMap attrs;
};

class ScriptedSpan final : public dasall::infra::tracing::ISpan {
 public:
  explicit ScriptedSpan(dasall::infra::tracing::TraceContext context)
      : context_(std::move(context)) {}

  void set_attribute(std::string_view key,
                     const dasall::infra::tracing::TraceAttributeValue& value) override {
    attributes[std::string(key)] = value;
  }

  void add_event(std::string_view name,
                 const dasall::infra::tracing::TraceAttributeMap& attrs) override {
    events.push_back(RecordedEvent{.name = std::string(name), .attrs = attrs});
  }

  void set_status(dasall::infra::tracing::SpanStatusCode code,
                  std::string_view message) override {
    status_code = code;
    status_message = std::string(message);
  }

  dasall::infra::tracing::SpanEndResult end(
      std::optional<std::int64_t> end_ts_unix_ms = std::nullopt) override {
    ended_at = end_ts_unix_ms;
    return dasall::infra::tracing::SpanEndResult{
        .end_ts_unix_ms = end_ts_unix_ms,
        .status_code = status_code,
        .status_message = status_message,
        .dropped_attr_count = 0U,
    };
  }

  dasall::infra::tracing::TraceContext get_context() const override {
    return context_;
  }

  dasall::infra::tracing::TraceContext context_;
  dasall::infra::tracing::TraceAttributeMap attributes;
  std::vector<RecordedEvent> events;
  dasall::infra::tracing::SpanStatusCode status_code =
      dasall::infra::tracing::SpanStatusCode::Unset;
  std::string status_message;
  std::optional<std::int64_t> ended_at;
};

class ScriptedTracer final : public dasall::infra::tracing::ITracer {
 public:
  std::shared_ptr<dasall::infra::tracing::ISpan> start_span(
      const dasall::infra::tracing::SpanDescriptor& descriptor,
      const dasall::infra::tracing::TraceContext* parent) override {
    last_descriptor = descriptor;
    last_parent = parent == nullptr ? std::nullopt : std::optional(*parent);
    last_span = std::make_shared<ScriptedSpan>(make_active_context());
    return last_span;
  }

  void with_active_span(
      const std::shared_ptr<dasall::infra::tracing::ISpan>& span,
      const dasall::infra::tracing::ActiveSpanCallback& fn) override {
    active_span = span;
    if (fn) {
      fn();
    }
  }

  dasall::infra::tracing::TraceContext current_context() const override {
    return make_active_context();
  }

  static dasall::infra::tracing::TraceContext make_active_context() {
    return dasall::infra::tracing::TraceContext{
        .trace_id = "11111111111111111111111111111111",
        .span_id = "2222222222222222",
        .trace_flags = 1U,
        .trace_state = std::string(),
        .parent_span_id = "3333333333333333",
        .state = dasall::infra::tracing::TraceContextState::Active,
        .is_remote = false,
    };
  }

  dasall::infra::tracing::SpanDescriptor last_descriptor{};
  std::optional<dasall::infra::tracing::TraceContext> last_parent;
  std::shared_ptr<ScriptedSpan> last_span;
  std::shared_ptr<dasall::infra::tracing::ISpan> active_span;
};

const ProviderModelMetadata& require_model(std::string provider_id, std::string model_id) {
  static const auto catalog = dasall::llm::test_support::make_default_catalog();
  const auto* model = catalog.find_model(provider_id, model_id);
  if (model == nullptr) {
    throw std::runtime_error("observability fixture model metadata is missing");
  }

  return *model;
}

[[nodiscard]] bool has_identity(
    const std::vector<dasall::infra::metrics::MetricIdentity>& identities,
    std::string_view name,
    dasall::infra::metrics::MetricType type,
    std::string_view unit) {
  return std::any_of(identities.begin(), identities.end(), [&](const auto& identity) {
    return identity.name == name && identity.type == type && identity.unit == unit;
  });
}

[[nodiscard]] const dasall::infra::metrics::MetricSample* find_sample(
    const std::vector<dasall::infra::metrics::MetricSample>& samples,
    std::string_view identity_name) {
  const auto it = std::find_if(samples.begin(), samples.end(), [&](const auto& sample) {
    return sample.identity_ref.name == identity_name;
  });
  if (it == samples.end()) {
    return nullptr;
  }

  return &*it;
}

[[nodiscard]] const std::string* find_log_attr(
    const dasall::infra::logging::LogEvent& event,
    std::string_view key) {
  const auto it = event.attrs.find(std::string(key));
  if (it == event.attrs.end()) {
    return nullptr;
  }

  return &it->second;
}

[[nodiscard]] std::optional<std::string> trace_attr_as_string(
    const dasall::infra::tracing::TraceAttributeMap& attrs,
    std::string_view key) {
  const auto it = attrs.find(std::string(key));
  if (it == attrs.end()) {
    return std::nullopt;
  }

  if (const auto* value = std::get_if<std::string>(&it->second)) {
    return *value;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> trace_attr_as_uint64(
    const dasall::infra::tracing::TraceAttributeMap& attrs,
    std::string_view key) {
  const auto it = attrs.find(std::string(key));
  if (it == attrs.end()) {
    return std::nullopt;
  }

  if (const auto* value = std::get_if<std::uint64_t>(&it->second)) {
    return *value;
  }

  return std::nullopt;
}

[[nodiscard]] LLMCallSummary make_summary() {
  return LLMCallSummary{
      .request_id = "req-028-observe",
      .llm_call_id = "call-028-observe",
      .stage = "planner",
      .resolved_route = "deepseek-prod/deepseek-reasoner",
      .model_name = "deepseek-reasoner",
      .prompt_id = "prompt.planner.default",
      .prompt_version = "2026-04-13.1",
      .fallback_used = true,
      .completed_at_ms = 1712577600400,
      .latency_ms = 152U,
      .failure_category = "fallback_exhausted",
      .error_type = "provider",
      .selection_reason_codes = {"requires_reasoning", "fallback_chain_prepared"},
      .estimated_input_tokens = 4096U,
      .prompt_cache_hit_tokens = 128U,
      .prompt_cache_miss_tokens = 2048U,
      .actual_cost_estimate_usd = 0.123456,
      .reasoning_mode_requested = "chat",
      .reasoning_mode_effective = "thinking",
      .provider_id = "deepseek-prod",
      .profile_id = "desktop_full",
      .outcome = "degraded",
      .from_route = "deepseek-prod/deepseek-chat",
      .to_route = "deepseek-prod/deepseek-reasoner",
      .prompt_policy_denied = true,
      .prompt_compose_over_budget = true,
      .adapter_timeout = true,
      .health_degraded = true,
      .reasoning_escalated = true,
  };
}

[[nodiscard]] LLMTraceSpanSignal make_trace_signal() {
  return LLMTraceSpanSignal{
      .kind = LLMTraceSpanKind::AdapterInvoke,
      .request_id = "req-028-trace",
      .llm_call_id = "call-028-trace",
      .stage = "planner",
      .resolved_route = "deepseek-prod/deepseek-reasoner",
      .model_name = "deepseek-reasoner",
      .prompt_id = "prompt.planner.default",
      .prompt_version = "2026-04-13.1",
      .fallback_used = true,
      .latency_ms = 88U,
      .failure_category = "provider_protocol",
      .error_type = "provider",
      .selection_reason_codes = {"requires_reasoning", "fallback_chain_prepared"},
      .estimated_input_tokens = 2048U,
      .prompt_cache_hit_tokens = 64U,
      .prompt_cache_miss_tokens = 256U,
      .actual_cost_estimate_usd = 0.03125,
      .reasoning_mode_requested = "chat",
      .reasoning_mode_effective = "thinking",
      .completed_at_ms = 1712577600500,
      .parent_context = ScriptedTracer::make_active_context(),
      .detail_ref = "llm://trace/adapter-invoke",
      .outcome = "failure",
  };
}

void test_usage_record_keeps_cost_anchor_fields_observable() {
  UsageAggregator aggregator;
  const auto usage = aggregator.aggregate(
      AdapterUsageFragment{
          .prompt_tokens = 180U,
          .completion_tokens = 20U,
          .total_tokens = 200U,
          .prompt_cache_hit_tokens = 60U,
          .prompt_cache_miss_tokens = 120U,
      },
      require_model("deepseek-prod", "deepseek-chat"));

  assert_equal(std::string("deepseek-prod"), usage.provider_id,
               "UsageAggregator should keep provider_id visible for downstream observability sinks");
  assert_equal(std::string("deepseek-chat"), usage.model_id,
               "UsageAggregator should keep model_id visible for downstream observability sinks");
  assert_equal(std::string("pricing-2026.04.13"), usage.pricing_ref,
               "UsageAggregator should keep pricing_ref visible for downstream observability sinks");
  assert_true(usage.estimated_cost_usd > 0.0,
              "UsageAggregator should keep estimated_cost_usd visible for downstream observability sinks");
}

void test_missing_pricing_metadata_gracefully_falls_back_to_zero_cost() {
  UsageAggregator aggregator;
  auto model = require_model("deepseek-prod", "deepseek-chat");
  model.pricing_ref.clear();
  model.summary.input_cache_hit_usd_per_1m = 0.0;
  model.summary.input_cache_miss_usd_per_1m = 0.0;
  model.summary.output_usd_per_1m = 0.0;

  const auto usage = aggregator.aggregate(
      AdapterUsageFragment{
          .prompt_tokens = 200U,
          .completion_tokens = 50U,
          .total_tokens = 250U,
          .prompt_cache_hit_tokens = std::nullopt,
          .prompt_cache_miss_tokens = std::nullopt,
      },
      model);

  assert_equal(200, static_cast<int>(usage.prompt_tokens),
               "UsageAggregator should keep prompt_tokens even when pricing metadata is absent");
  assert_equal(50, static_cast<int>(usage.completion_tokens),
               "UsageAggregator should keep completion_tokens even when pricing metadata is absent");
  assert_true(usage.estimated_cost_usd == 0.0,
              "UsageAggregator should gracefully fall back to zero estimated cost when pricing metadata is absent");
}

void test_metrics_bridge_keeps_required_log_fields_and_registers_metric_families() {
  auto logger = std::make_shared<ScriptedLogger>();
  auto meter = std::make_shared<ScriptedMeter>();
  auto provider = std::make_shared<ScriptedProvider>(meter);
  LLMMetricsBridge bridge(logger, provider);

  const auto result = bridge.record_call(make_summary());
  const auto status = bridge.get_status();

  assert_true(result.emitted && result.has_consistent_state(),
              "LLMMetricsBridge should emit a consistent combined log and metrics result when both sinks are ready");
  assert_true(status.is_valid() && status.log_emitted_total == 1U &&
                  !status.degraded && status.metric_emission_attempt_total == 1U,
              "LLMMetricsBridge should keep a healthy local status after one successful combined emission");
  assert_equal(1, static_cast<int>(logger->events.size()),
               "LLMMetricsBridge should emit exactly one structured call summary log per summary");
  assert_equal(std::string("llm"), logger->events.front().module,
               "LLMMetricsBridge should pin module=llm on emitted call summary logs");
  assert_true(find_log_attr(logger->events.front(), "request_id") != nullptr &&
                  *find_log_attr(logger->events.front(), "request_id") == "req-028-observe" &&
                  *find_log_attr(logger->events.front(), "llm_call_id") == "call-028-observe" &&
                  *find_log_attr(logger->events.front(), "stage") == "planner" &&
                  *find_log_attr(logger->events.front(), "resolved_route") ==
                      "deepseek-prod/deepseek-reasoner" &&
                  *find_log_attr(logger->events.front(), "model_name") ==
                      "deepseek-reasoner" &&
                  *find_log_attr(logger->events.front(), "prompt_id") ==
                      "prompt.planner.default" &&
                  *find_log_attr(logger->events.front(), "prompt_version") ==
                      "2026-04-13.1" &&
                  *find_log_attr(logger->events.front(), "fallback_used") == "true" &&
                  *find_log_attr(logger->events.front(), "latency_ms") == "152" &&
                  *find_log_attr(logger->events.front(), "failure_category") ==
                      "fallback_exhausted" &&
                  *find_log_attr(logger->events.front(), "selection_reason_codes") ==
                      "requires_reasoning,fallback_chain_prepared" &&
                  *find_log_attr(logger->events.front(), "estimated_input_tokens") ==
                      "4096" &&
                  *find_log_attr(logger->events.front(), "prompt_cache_hit_tokens") ==
                      "128" &&
                  *find_log_attr(logger->events.front(), "prompt_cache_miss_tokens") ==
                      "2048" &&
                  *find_log_attr(logger->events.front(), "actual_cost_estimate_usd") ==
                      "0.123456" &&
                  *find_log_attr(logger->events.front(), "reasoning_mode_requested") ==
                      "chat" &&
                  *find_log_attr(logger->events.front(), "reasoning_mode_effective") ==
                      "thinking" &&
                  *find_log_attr(logger->events.front(), "error_type") == "provider",
              "LLMMetricsBridge should preserve the 028 required call summary fields in structured log attrs");
  assert_equal(std::string("llm.observability"), provider->last_scope.name,
               "LLMMetricsBridge should request the frozen llm.observability meter scope");
  assert_equal(std::string("v1"), provider->last_scope.version,
               "LLMMetricsBridge should preserve the frozen llm.observability meter scope version");
  assert_equal(12, static_cast<int>(meter->created_identities.size()),
               "LLMMetricsBridge should register the twelve frozen llm observability metric families on first emit");
  assert_true(has_identity(meter->created_identities,
                           "llm_calls_total",
                           dasall::infra::metrics::MetricType::Counter,
                           "1") &&
                  has_identity(meter->created_identities,
                               "llm_call_latency_ms",
                               dasall::infra::metrics::MetricType::Histogram,
                               "ms") &&
                  has_identity(meter->created_identities,
                               "llm_cost_estimate_usd_total",
                               dasall::infra::metrics::MetricType::Counter,
                               "usd"),
              "LLMMetricsBridge should preserve the frozen metric name/type/unit contract for representative llm metric families");
  assert_equal(12, static_cast<int>(meter->recorded_samples.size()),
               "LLMMetricsBridge should emit all twelve metric samples when the summary exercises every optional observability path");

  const auto* fallback_sample = find_sample(meter->recorded_samples, "llm_fallback_total");
  const auto* deny_sample = find_sample(meter->recorded_samples, "prompt_policy_deny_total");
  const auto* cost_sample = find_sample(meter->recorded_samples, "llm_cost_estimate_usd_total");
  assert_true(fallback_sample != nullptr && deny_sample != nullptr && cost_sample != nullptr,
              "LLMMetricsBridge should emit fallback, deny and cost samples when the corresponding summary flags are set");
  assert_true(fallback_sample->labels.stage ==
                  "fallback/planner/deepseek-prod/deepseek-chat_to_deepseek-prod/deepseek-reasoner" &&
                  fallback_sample->labels.outcome == "degraded" &&
                  deny_sample->labels.stage ==
                      "prompt_policy_deny/planner/requires_reasoning" &&
                  deny_sample->labels.outcome == "rejected" &&
                  cost_sample->labels.stage == "cost/deepseek-prod/deepseek-reasoner" &&
                  cost_sample->labels.error_code == "provider",
              "LLMMetricsBridge should project fallback, policy deny and cost metrics using the frozen stage/outcome/error_code mapping");
}

void test_trace_bridge_projects_route_reasoning_and_cost_into_stage_spans() {
  auto tracer = std::make_shared<ScriptedTracer>();
  LLMTraceBridge bridge(tracer);

  const auto result = bridge.record_span(make_trace_signal());
  const auto status = bridge.get_status();

  assert_true(result.emitted && result.has_consistent_state(),
              "LLMTraceBridge should emit a consistent span result when a tracer sink is available");
  assert_true(status.is_valid() && status.emitted_total == 1U && !status.degraded,
              "LLMTraceBridge should keep a healthy status after one successful span emission");
  assert_equal(std::string("llm.adapter.invoke"), tracer->last_descriptor.name,
               "LLMTraceBridge should freeze adapter invoke spans to the low-cardinality llm.adapter.invoke name");
  assert_true(tracer->last_descriptor.kind == dasall::infra::tracing::SpanKind::Client,
              "LLMTraceBridge should mark adapter invoke spans as client spans");
  assert_true(tracer->last_descriptor.start_ts_unix_ms.has_value() &&
                  *tracer->last_descriptor.start_ts_unix_ms == 1712577600412,
              "LLMTraceBridge should derive the span start time from completed_at minus latency_ms");
  assert_true(tracer->last_parent.has_value() && tracer->last_parent->is_valid(),
              "LLMTraceBridge should propagate a valid parent trace context when one is supplied");
  assert_true(trace_attr_as_string(tracer->last_descriptor.attrs, "resolved_route") ==
                      std::optional<std::string>("deepseek-prod/deepseek-reasoner") &&
                  trace_attr_as_string(tracer->last_descriptor.attrs, "selection_reason_codes") ==
                      std::optional<std::string>("requires_reasoning,fallback_chain_prepared") &&
                  trace_attr_as_string(tracer->last_descriptor.attrs, "reasoning_mode_requested") ==
                      std::optional<std::string>("chat") &&
                  trace_attr_as_string(tracer->last_descriptor.attrs, "reasoning_mode_effective") ==
                      std::optional<std::string>("thinking") &&
                  trace_attr_as_uint64(tracer->last_descriptor.attrs, "prompt_cache_hit_tokens") ==
                      std::optional<std::uint64_t>(64U),
              "LLMTraceBridge should preserve route, selection reasons, reasoning mode and usage facts as trace attrs");
  assert_equal(1, static_cast<int>(tracer->last_span->events.size()),
               "LLMTraceBridge should emit one structured selection event per span signal");
  assert_equal(std::string("llm.selection"), tracer->last_span->events.front().name,
               "LLMTraceBridge should use a stable llm.selection event name for selection reason evidence");
  assert_true(tracer->last_span->status_code == dasall::infra::tracing::SpanStatusCode::Error &&
                  tracer->last_span->status_message == "provider" &&
                  tracer->last_span->ended_at == std::optional<std::int64_t>(1712577600500),
              "LLMTraceBridge should mark failed spans as error and end them at the supplied completion timestamp");
}

}  // namespace

int main() {
  try {
    test_usage_record_keeps_cost_anchor_fields_observable();
    test_missing_pricing_metadata_gracefully_falls_back_to_zero_cost();
    test_metrics_bridge_keeps_required_log_fields_and_registers_metric_families();
    test_trace_bridge_projects_route_reasoning_and_cost_into_stage_spans();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}