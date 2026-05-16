#include <algorithm>
#include <deque>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../infra/include/InfraContext.h"
#include "../../../infra/include/audit/IAuditLogger.h"
#include "../../../infra/include/logging/ILogger.h"
#include "../../../infra/include/metrics/IMeter.h"
#include "../../../infra/include/metrics/IMetricsProvider.h"
#include "../../../infra/include/tracing/ISpan.h"
#include "../../../infra/include/tracing/ITracer.h"

#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"
#include "../../../llm/src/observability/LLMAuditBridge.h"
#include "../../../llm/src/observability/LLMMetricsBridge.h"
#include "../../../llm/src/observability/LLMTraceBridge.h"
#include "../../../llm/src/prompt/PromptPipeline.h"

#include "../../mocks/include/MockLLMAdapter.h"

#include "../../unit/llm/ModelRouterTestSupport.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::infra::tracing::ActiveSpanCallback;
using dasall::infra::tracing::ISpan;
using dasall::infra::tracing::ITracer;
using dasall::infra::tracing::SpanDescriptor;
using dasall::infra::tracing::SpanEndResult;
using dasall::infra::tracing::SpanStatusCode;
using dasall::infra::tracing::TraceAttributeMap;
using dasall::infra::tracing::TraceAttributeValue;
using dasall::infra::tracing::TraceContext;
using dasall::infra::tracing::TraceContextState;
using dasall::llm::AdapterCallResult;
using dasall::llm::AdapterUsageFragment;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::llm::LLMSubsystemConfig;
using dasall::llm::ModelSelectionHint;
using dasall::llm::observability::LLMAuditBridge;
using dasall::llm::observability::LLMAuditContext;
using dasall::llm::observability::LLMAuditEvent;
using dasall::llm::observability::LLMAuditEventKind;
using dasall::llm::observability::LLMMetricsBridge;
using dasall::llm::observability::LLMTraceBridge;
using dasall::llm::prompt::PromptPipeline;
using dasall::llm::route::AdapterRegistration;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kPromptAssetRoot = "/home/gangan/DASALL/llm/assets/prompts";

class RecordingLogger final : public dasall::infra::logging::ILogger {
 public:
  dasall::infra::logging::LogWriteResult log(
      const dasall::infra::logging::LogEvent& event) override {
    events.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  void set_level(dasall::infra::logging::LogLevel level) override {
    last_level = level;
  }

  std::vector<dasall::infra::logging::LogEvent> events;
  dasall::infra::logging::LogLevel last_level =
      dasall::infra::logging::LogLevel::Info;
};

class RecordingMeter final : public dasall::infra::metrics::IMeter {
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
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://llm/smoke");
  }

  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class RecordingMetricsProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
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
  std::shared_ptr<RecordingMeter> meter_;
};

[[nodiscard]] std::string hex_id(std::uint64_t value, std::size_t width) {
  std::ostringstream builder;
  builder << std::hex << std::nouppercase << std::setfill('0')
          << std::setw(static_cast<int>(width)) << value;
  auto encoded = builder.str();
  if (encoded.size() > width) {
    encoded = encoded.substr(encoded.size() - width);
  }
  if (encoded.size() < width) {
    encoded.insert(encoded.begin(), width - encoded.size(), '0');
  }
  if (std::all_of(encoded.begin(), encoded.end(), [](const char ch) {
        return ch == '0';
      })) {
    encoded.back() = '1';
  }
  return encoded;
}

class RecordingSpan final : public ISpan {
 public:
  explicit RecordingSpan(TraceContext context)
      : context_(std::move(context)) {}

  void set_attribute(std::string_view key,
                     const TraceAttributeValue& value) override {
    attributes[std::string(key)] = value;
  }

  void add_event(std::string_view,
                 const TraceAttributeMap&) override {}

  void set_status(SpanStatusCode code,
                  std::string_view message) override {
    status_code = code;
    status_message = std::string(message);
  }

  SpanEndResult end(
      std::optional<std::int64_t> end_ts_unix_ms = std::nullopt) override {
    return SpanEndResult{
        .end_ts_unix_ms = end_ts_unix_ms,
        .status_code = status_code,
        .status_message = status_message,
        .dropped_attr_count = 0U,
    };
  }

  TraceContext get_context() const override {
    return context_;
  }

  TraceContext context_;
  TraceAttributeMap attributes;
  SpanStatusCode status_code = SpanStatusCode::Unset;
  std::string status_message;
};

struct StartedSpanRecord {
  SpanDescriptor descriptor;
  std::shared_ptr<RecordingSpan> span;
};

class RecordingTracer final : public ITracer {
 public:
  std::shared_ptr<ISpan> start_span(const SpanDescriptor& descriptor,
                                    const TraceContext*) override {
    auto span = std::make_shared<RecordingSpan>(TraceContext{
        .trace_id = hex_id(++trace_seed_, dasall::infra::tracing::kTraceIdHexLength),
        .span_id = hex_id(++span_seed_, dasall::infra::tracing::kSpanIdHexLength),
        .trace_flags = 0x01U,
        .trace_state = std::string(),
        .parent_span_id = std::string(),
        .state = TraceContextState::Active,
        .is_remote = false,
    });
    started_spans.push_back(StartedSpanRecord{
        .descriptor = descriptor,
        .span = span,
    });
    return span;
  }

  void with_active_span(const std::shared_ptr<ISpan>&,
                        const ActiveSpanCallback& fn) override {
    if (fn) {
      fn();
    }
  }

  TraceContext current_context() const override {
    return TraceContext::noop();
  }

  std::vector<StartedSpanRecord> started_spans;

 private:
  std::uint64_t trace_seed_ = 0U;
  std::uint64_t span_seed_ = 0U;
};

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] const std::string* find_log_attr(
    const dasall::infra::logging::LogEvent& event,
    std::string_view key) {
  const auto it = event.attrs.find(std::string(key));
  if (it == event.attrs.end()) {
    return nullptr;
  }

  return &it->second;
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

[[nodiscard]] std::optional<std::string> trace_attr_as_string(
    const TraceAttributeMap& attrs,
    std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  if (attr == nullptr) {
    return std::nullopt;
  }

  if (const auto* value = std::get_if<std::string>(attr)) {
    return *value;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> trace_attr_as_uint64(
    const TraceAttributeMap& attrs,
    std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  if (attr == nullptr) {
    return std::nullopt;
  }

  if (const auto* value = std::get_if<std::uint64_t>(attr)) {
    return *value;
  }

  return std::nullopt;
}

[[nodiscard]] bool has_result_tag(const dasall::llm::LLMManagerResult& result,
                                  const std::string& expected_tag) {
  return result.response.has_value() && result.response->tags.has_value() &&
         std::find(result.response->tags->begin(),
                   result.response->tags->end(),
                   expected_tag) != result.response->tags->end();
}

[[nodiscard]] bool has_result_tag_prefix(
    const dasall::llm::LLMManagerResult& result,
    const std::string& prefix) {
  return result.response.has_value() && result.response->tags.has_value() &&
         std::find_if(result.response->tags->begin(),
                      result.response->tags->end(),
                      [&](const std::string& tag) {
                        return tag.rfind(prefix, 0U) == 0U;
                      }) != result.response->tags->end();
}

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

AdapterRegistration make_registration(std::shared_ptr<MockLLMAdapter> adapter) {
  return AdapterRegistration{
      .provider_id = "deepseek-prod",
      .model_id = "deepseek-chat",
      .adapter_id = "deepseek-cloud",
      .deployment_type = "cloud",
      .capability_tags = {"cloud", "external", "unary"},
      .supports_streaming = false,
      .adapter = std::move(adapter),
  };
}

LLMSubsystemConfig make_config() {
  auto config = dasall::llm::test_support::make_config(
      "planning", "cloud.reasoning", std::nullopt, {"local.small"}, false, false);
  config.profile_id = "desktop_full";
  config.prompt_asset_sources.baseline_root = std::string(kPromptAssetRoot);
  config.prompt_selector_overlay.active_scene = "general";
  config.prompt_selector_overlay.active_persona = "planner";
  return config;
}

LLMGenerateRequest make_request() {
  LLMRequest request;
  request.request_id = "req-029-smoke";
  request.llm_call_id = "call-029-smoke";
  request.model_route = "cloud.reasoning";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"draft the llm smoke integration acceptance"};
  request.created_at = 1712966400000LL;
  request.output_schema_ref = "schema://planner/default";
  request.response_format = "json_object";
  request.max_output_tokens = 1024U;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = 4096U,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"integration", "llm-smoke"};

  return LLMGenerateRequest{
      .stage = "planning",
      .task_type = "plan",
      .request = std::move(request),
      .prompt_release_id_override = std::nullopt,
      .selection_hint = std::make_shared<const ModelSelectionHint>(ModelSelectionHint{
          .stage = "planning",
          .task_type = "plan",
          .complexity_tier = "standard",
          .latency_sla_tier = "interactive",
          .budget_tier = "hard_cap",
          .requires_tools = true,
          .requires_reasoning = false,
          .prefers_visible_reasoning = false,
          .estimated_input_tokens = 2048U,
          .target_output_tokens = 1024U,
          .previous_route_failures = 0U,
      }),
  };
}

LLMAuditContext make_audit_context() {
  return LLMAuditContext{
      .infra_context = dasall::infra::InfraContext{
          .request_id = "req-029-smoke",
          .session_id = "sess-029-smoke",
          .trace_id = "trace-029-smoke",
          .task_id = "task-029-smoke",
          .parent_task_id = "parent-029-smoke",
          .lease_id = "lease-029-smoke",
      },
      .worker_type = "llm.integration.smoke",
  };
}

void test_llm_smoke_integration_closes_prompt_manager_mock_loop_and_emits_observability() {
  auto logger = std::make_shared<RecordingLogger>();
  auto meter = std::make_shared<RecordingMeter>();
  auto metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);
  auto tracer = std::make_shared<RecordingTracer>();
  auto metrics_bridge = std::make_shared<LLMMetricsBridge>(logger, metrics_provider);
  auto trace_bridge = std::make_shared<LLMTraceBridge>(tracer);
  auto audit_logger = std::make_shared<RecordingAuditLogger>();
  LLMAuditBridge audit_bridge(audit_logger);

  auto prompt_pipeline = std::make_shared<PromptPipeline>();
  auto router = std::make_shared<dasall::llm::route::ModelRouter>();
  auto registry = std::make_shared<dasall::llm::route::AdapterRegistry>();
  auto executor = std::make_shared<dasall::llm::LLMCallExecutor>();
  auto normalizer = std::make_shared<dasall::llm::execution::ResponseNormalizer>();
  auto aggregator = std::make_shared<dasall::llm::UsageAggregator>();
  auto catalog_snapshot =
      std::make_shared<const dasall::llm::provider::ProviderCatalogSnapshot>(
          dasall::llm::test_support::make_default_catalog());

  assert_true(registry->init(dasall::llm::route::AdapterRegistryConfig{
                  .blocked_failure_threshold = 6U,
              }),
              "LLM smoke integration should initialize AdapterRegistry before wiring the manager");

  auto adapter = std::make_shared<MockLLMAdapter>();
  adapter->set_generate_handler([](const LLMRequest& request) {
    LLMResponse response;
    response.request_id = request.request_id;
    response.llm_call_id = request.llm_call_id;
    response.response_kind = LLMResponseKind::DirectResponse;
    response.content_payload = "smoke-ok";
    response.finish_reason = "stop";

    AdapterCallResult result;
    result.response = std::move(response);
    result.usage = AdapterUsageFragment{
        .prompt_tokens = 64U,
        .completion_tokens = 32U,
        .total_tokens = 96U,
        .prompt_cache_hit_tokens = 16U,
        .prompt_cache_miss_tokens = 48U,
    };
    result.provider_diagnostics.reasoning_content =
        "provider-private chain of thought";
    result.provider_diagnostics.provider_trace_id = "trace-029-smoke";
    result.provider_diagnostics.audit_tags = {"normalized"};
    return result;
  });
  assert_true(registry->register_adapter(make_registration(adapter)),
              "LLM smoke integration should register a MockLLMAdapter for the selected route");

  LLMManager manager(prompt_pipeline,
                     router,
                     registry,
                     executor,
                     normalizer,
                     aggregator,
                     catalog_snapshot,
                     nullptr,
                     metrics_bridge,
                     trace_bridge);
  assert_true(manager.init(make_config()),
              "LLM smoke integration should initialize LLMManager with real PromptPipeline and observability bridges");

  const auto result = manager.generate(make_request());

  assert_true(result.has_consistent_values() && result.response.has_value(),
              "LLM smoke integration should return a consistent success result across PromptPipeline, routing, adapter invoke and response normalization");
  assert_equal(std::string("deepseek-prod/deepseek-chat"),
               result.resolved_route,
               "LLM smoke integration should resolve the concrete chat route through ModelRouter");
  assert_equal(1, static_cast<int>(result.attempted_routes.size()),
               "LLM smoke integration should complete on the primary route without fallback");
  assert_true(!result.fallback_used,
              "LLM smoke integration should keep fallback_used false on the unary happy path");
  assert_true(result.response->prompt_id.has_value() &&
                  *result.response->prompt_id == "planner" &&
                  result.response->prompt_version.has_value() &&
                  *result.response->prompt_version == "2026.04.11",
              "LLM smoke integration should stamp the selected prompt asset identity onto the normalized response");
  assert_true(result.response->model_name.has_value() &&
                  *result.response->model_name == "deepseek-chat",
              "LLM smoke integration should preserve the resolved provider model on the normalized response");
  assert_true(has_result_tag(result, "route=deepseek-prod/deepseek-chat") &&
                  has_result_tag(result, "provider_trace_id=trace-029-smoke") &&
                  has_result_tag(result, "audit=reasoning_content_stripped") &&
                  has_result_tag(result, "audit=provider_audit:normalized") &&
                  has_result_tag(result, "reasoning_content_stripped=true") &&
                  has_result_tag(result, "usage:prompt_cache_hit_tokens=16") &&
                  has_result_tag(result, "usage:prompt_cache_miss_tokens=48") &&
                  has_result_tag_prefix(result, "usage:estimated_cost_usd="),
              "LLM smoke integration should surface route, provider trace, audit, reasoning strip and usage anchors through provider-neutral response tags");
  assert_true(adapter->last_request().has_value() &&
                  adapter->last_request()->model_route.has_value() &&
                  *adapter->last_request()->model_route == "deepseek-prod/deepseek-chat" &&
                  adapter->last_request()->prompt_id.has_value() &&
                  *adapter->last_request()->prompt_id == "planner" &&
                  adapter->last_request()->prompt_version.has_value() &&
                  *adapter->last_request()->prompt_version == "2026.04.11" &&
                  adapter->last_request()->messages.has_value() &&
                  adapter->last_request()->messages->size() == 2U,
              "LLM smoke integration should hand PromptPipeline output and the concrete route into MockLLMAdapter");
  assert_true(adapter->last_request()->messages->at(0).rfind("system: ", 0U) == 0U &&
                  adapter->last_request()->messages->at(1).rfind("user: ", 0U) == 0U &&
                  adapter->last_request()->messages->at(1).find("用户目标：") != std::string::npos &&
                  adapter->last_request()->messages->at(1).find("约束条件：") != std::string::npos,
              "LLM smoke integration should compose real planner prompt assets into system/user messages before provider dispatch");

  assert_true(logger->events.size() == 1U,
              "LLM smoke integration should emit one structured llm call log entry on the success path");
  const auto& log_event = logger->events.front();
  assert_true(find_log_attr(log_event, "request_id") != nullptr &&
                  *find_log_attr(log_event, "request_id") == "req-029-smoke" &&
                  *find_log_attr(log_event, "llm_call_id") == "call-029-smoke" &&
                  *find_log_attr(log_event, "stage") == "planning" &&
                  *find_log_attr(log_event, "resolved_route") ==
                      "deepseek-prod/deepseek-chat" &&
                  *find_log_attr(log_event, "model_name") == "deepseek-chat" &&
                  *find_log_attr(log_event, "prompt_id") == "planner" &&
                  *find_log_attr(log_event, "prompt_version") == "2026.04.11" &&
                  *find_log_attr(log_event, "provider_id") == "deepseek-prod" &&
                  *find_log_attr(log_event, "profile_id") == "desktop_full" &&
                  *find_log_attr(log_event, "fallback_used") == "false" &&
                  *find_log_attr(log_event, "outcome") == "success" &&
                  *find_log_attr(log_event, "reasoning_mode_requested") == "chat" &&
                  *find_log_attr(log_event, "reasoning_mode_effective") == "non_thinking" &&
                  *find_log_attr(log_event, "prompt_cache_hit_tokens") == "16" &&
                  *find_log_attr(log_event, "prompt_cache_miss_tokens") == "48" &&
                  *find_log_attr(log_event, "actual_cost_estimate_usd") == "0.000037",
              "LLM smoke integration should project the required prompt/model/route/token/cost fields into structured logging");
  assert_true(find_log_attr(log_event, "selection_reason_codes") != nullptr &&
                  find_log_attr(log_event, "selection_reason_codes")
                          ->find("selected_primary_route") != std::string::npos &&
                  find_log_attr(log_event, "selection_reason_codes")
                          ->find("requires_tools") != std::string::npos,
              "LLM smoke integration should preserve route selection reason codes in structured logging");

  assert_true(metrics_provider->last_scope.name == "llm.observability" &&
                  metrics_provider->last_scope.version == "v1",
              "LLM smoke integration should request the frozen llm observability meter scope");
  assert_true(find_sample(meter->recorded_samples, "llm_calls_total") != nullptr &&
                  find_sample(meter->recorded_samples, "llm_call_latency_ms") != nullptr &&
                  find_sample(meter->recorded_samples, "llm_model_selection_total") != nullptr &&
                  find_sample(meter->recorded_samples, "llm_prompt_cache_hit_tokens_total") != nullptr &&
                  find_sample(meter->recorded_samples, "llm_prompt_cache_miss_tokens_total") != nullptr &&
                  find_sample(meter->recorded_samples, "llm_cost_estimate_usd_total") != nullptr,
              "LLM smoke integration should emit the frozen llm observability metric families on the success path");
  const auto* calls_total = find_sample(meter->recorded_samples, "llm_calls_total");
  assert_true(calls_total != nullptr &&
                  calls_total->labels.stage.rfind("call/planning/", 0U) == 0U &&
                  calls_total->labels.profile == "desktop_full" &&
                  calls_total->labels.outcome == "success",
              "LLM smoke integration should label success metrics with stage, profile, and outcome fields");

  assert_equal(3, static_cast<int>(tracer->started_spans.size()),
               "LLM smoke integration should emit route resolve, adapter invoke and response normalize trace spans");
  assert_equal(std::string("llm.route.resolve"),
               tracer->started_spans.at(0).descriptor.name,
               "LLM smoke integration should emit the frozen llm.route.resolve span name");
  assert_equal(std::string("llm.adapter.invoke"),
               tracer->started_spans.at(1).descriptor.name,
               "LLM smoke integration should emit the frozen llm.adapter.invoke span name");
  assert_equal(std::string("llm.response.normalize"),
               tracer->started_spans.at(2).descriptor.name,
               "LLM smoke integration should emit the frozen llm.response.normalize span name");
  assert_true(trace_attr_as_string(tracer->started_spans.at(0).descriptor.attrs, "resolved_route") ==
                      std::optional<std::string>("deepseek-prod/deepseek-chat") &&
                  trace_attr_as_string(tracer->started_spans.at(1).descriptor.attrs, "prompt_id") ==
                      std::optional<std::string>("planner") &&
                  trace_attr_as_string(tracer->started_spans.at(2).descriptor.attrs, "prompt_version") ==
                      std::optional<std::string>("2026.04.11") &&
                  trace_attr_as_string(tracer->started_spans.at(2).descriptor.attrs, "reasoning_mode_requested") ==
                      std::optional<std::string>("chat") &&
                  trace_attr_as_string(tracer->started_spans.at(2).descriptor.attrs, "reasoning_mode_effective") ==
                      std::optional<std::string>("non_thinking") &&
                  trace_attr_as_string(tracer->started_spans.at(2).descriptor.attrs, "outcome") ==
                      std::optional<std::string>("success") &&
                  trace_attr_as_uint64(tracer->started_spans.at(2).descriptor.attrs, "prompt_cache_hit_tokens") ==
                      std::optional<std::uint64_t>(16U),
              "LLM smoke integration should keep prompt/model/route/reasoning/cache fields visible on trace span attributes");

  const auto audit_result = audit_bridge.write_audit_event(LLMAuditEvent{
      .kind = LLMAuditEventKind::ReasoningContentStripped,
      .stage = "llm.response.normalize",
      .reason = "reasoning_content removed before shared llm response handoff",
      .context = make_audit_context(),
      .detail_ref = "llm://audit/reasoning-content-strip",
      .llm_call_id = result.response->llm_call_id.value_or(std::string{}),
      .prompt_id = result.response->prompt_id.value_or(std::string{}),
      .prompt_version = result.response->prompt_version.value_or(std::string{}),
      .resolved_route = result.resolved_route,
      .model_name = result.response->model_name.value_or(std::string{}),
      .profile_id = "desktop_full",
      .trusted_source = std::string(),
      .metadata_field = std::string(),
      .expected_value = std::string(),
      .observed_value = std::string(),
      .reasoning_mode_requested = "chat",
      .reasoning_mode_effective = "non_thinking",
      .timestamp_ms = result.response->completed_at.value_or(1712966400100LL),
  });

  assert_true(audit_result.emitted && audit_result.has_consistent_state() &&
                  audit_logger->events.size() == 1U,
              "LLM smoke integration should convert hot-path reasoning strip evidence into a persisted audit event");
  assert_equal(std::string("llm.reasoning_content_stripped"),
               audit_logger->events.front().action,
               "LLM smoke integration should keep the frozen reasoning_content_stripped audit action name");
  assert_true(has_side_effect(audit_logger->events.front(), "llm_call_id:call-029-smoke") &&
                  has_side_effect(audit_logger->events.front(), "reasoning_mode_requested:chat") &&
                  has_side_effect(audit_logger->events.front(), "reasoning_mode_effective:non_thinking"),
              "LLM smoke integration should preserve llm_call_id and reasoning mode facts in the audit side effects");
}

}  // namespace

int main() {
  try {
    test_llm_smoke_integration_closes_prompt_manager_mock_loop_and_emits_observability();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}