#include <algorithm>
#include <deque>
#include <exception>
#include <filesystem>
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

#include "../../../infra/include/audit/IAuditLogger.h"
#include "../../../infra/include/logging/ILogger.h"
#include "../../../infra/include/metrics/IMeter.h"
#include "../../../infra/include/metrics/IMetricsProvider.h"
#include "../../../infra/include/metrics/MetricTypes.h"
#include "../../../infra/include/tracing/ISpan.h"
#include "../../../infra/include/tracing/ITracer.h"
#include "../../../infra/include/tracing/ITracerProvider.h"

#include "../../../llm/include/ILLMTransport.h"
#include "../../../llm/include/LLMProductionFactory.h"
#include "../../../llm/include/route/ModelSelectionHint.h"
#include "../../../profiles/include/RuntimePolicySnapshot.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMRequestMode;
using dasall::contracts::LLMResponseKind;
using dasall::infra::tracing::ActiveSpanCallback;
using dasall::infra::tracing::ISpan;
using dasall::infra::tracing::ITracer;
using dasall::infra::tracing::ITracerProvider;
using dasall::infra::tracing::SpanDescriptor;
using dasall::infra::tracing::SpanEndResult;
using dasall::infra::tracing::SpanStatusCode;
using dasall::infra::tracing::TraceAttributeMap;
using dasall::infra::tracing::TraceAttributeValue;
using dasall::infra::tracing::TraceContext;
using dasall::infra::tracing::TraceContextState;
using dasall::llm::ILLMTransport;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMProductionFactoryOptions;
using dasall::llm::LLMTransportRequest;
using dasall::llm::LLMTransportResponse;
using dasall::llm::ModelSelectionHint;
using dasall::llm::create_production_llm_manager;
using dasall::profiles::CapabilityCachePolicy;
using dasall::profiles::DegradePolicy;
using dasall::profiles::ExecutionPolicy;
using dasall::profiles::ModelProfile;
using dasall::profiles::ModelRoutePolicy;
using dasall::profiles::OpsPolicy;
using dasall::profiles::PromptPolicy;
using dasall::profiles::RuntimePolicySnapshot;
using dasall::profiles::TimeoutBudget;
using dasall::profiles::TimeoutPolicy;
using dasall::profiles::TokenBudgetPolicy;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

std::string repo_provider_catalog_root() {
  return (std::filesystem::path(DASALL_REPO_ROOT) / "llm/assets/providers")
      .generic_string();
}

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
        "metrics://llm/production-observability");
  }

  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class RecordingMetricsProvider final
    : public dasall::infra::metrics::IMetricsProvider {
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

class RecordingTracerProvider final : public ITracerProvider {
 public:
  explicit RecordingTracerProvider(std::shared_ptr<RecordingTracer> tracer)
      : tracer_(std::move(tracer)) {}

  dasall::infra::tracing::TraceOperationStatus init(
      const dasall::infra::tracing::TraceConfig&) override {
    return dasall::infra::tracing::TraceOperationStatus::success(
        "trace://llm/provider-init");
  }

  std::shared_ptr<ITracer> get_tracer(
      const dasall::infra::tracing::TracerScope& scope) override {
    last_scope = scope;
    return tracer_;
  }

  dasall::infra::tracing::TraceOperationStatus force_flush(
      std::uint32_t) override {
    return dasall::infra::tracing::TraceOperationStatus::success(
        "trace://llm/provider-flush");
  }

  dasall::infra::tracing::TraceOperationStatus shutdown(
      std::uint32_t) override {
    return dasall::infra::tracing::TraceOperationStatus::success(
        "trace://llm/provider-shutdown");
  }

  dasall::infra::tracing::TracerScope last_scope{};

 private:
  std::shared_ptr<RecordingTracer> tracer_;
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

class StubTransport final : public ILLMTransport {
 public:
  void push_response(std::string url_fragment, LLMTransportResponse response) {
    for (auto& stub : stubs_) {
      if (stub.url_fragment == url_fragment) {
        stub.responses.push_back(std::move(response));
        return;
      }
    }

    Stub stub;
    stub.url_fragment = std::move(url_fragment);
    stub.responses.push_back(std::move(response));
    stubs_.push_back(std::move(stub));
  }

  [[nodiscard]] LLMTransportResponse send(const LLMTransportRequest& request) override {
    requests.push_back(request);

    for (auto& stub : stubs_) {
      if (request.url.find(stub.url_fragment) == std::string::npos) {
        continue;
      }

      if (stub.responses.empty()) {
        break;
      }

      auto response = stub.responses.front();
      stub.responses.pop_front();
      return response;
    }

    return LLMTransportResponse{
        .status_code = 0U,
        .body = {},
        .error_message = "missing transport stub for " + request.url,
    };
  }

  std::vector<LLMTransportRequest> requests;

 private:
  struct Stub {
    std::string url_fragment;
    std::deque<LLMTransportResponse> responses;
  };

  std::vector<Stub> stubs_;
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

[[nodiscard]] bool has_result_tag(
    const dasall::llm::LLMManagerResult& result,
    const std::string& expected_tag) {
  return result.response.has_value() && result.response->tags.has_value() &&
         std::find(result.response->tags->begin(),
                   result.response->tags->end(),
                   expected_tag) != result.response->tags->end();
}

RuntimePolicySnapshot make_snapshot() {
  return RuntimePolicySnapshot{
      42U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 8192U,
          .max_turns = 16U,
          .max_tool_calls = 8U,
          .max_latency_ms = 5000U,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {
              {"planning",
               ModelRoutePolicy{.route = "cloud.reasoning",
                                .fallback_route = std::string("lan.general"),
                                .streaming_enabled = false}},
              {"response",
               ModelRoutePolicy{.route = "cloud.general",
                                .fallback_route = std::string("local.small"),
                                .streaming_enabled = true}},
          },
      },
      TokenBudgetPolicy{.max_input_tokens = 4096U,
                        .max_output_tokens = 1024U,
                        .max_history_turns = 8U,
                        .compression_threshold = 3000U},
      PromptPolicy{.allowed_prompt_releases = {"stable"},
                   .trusted_sources = {"profiles"},
                   .tool_visibility_rules = {"builtin:all"}},
      CapabilityCachePolicy{.refresh_interval_ms = 1000,
                            .expire_after_ms = 5000,
                            .stale_read_allowed = false,
                            .failure_backoff_ms = 500},
      DegradePolicy{.fallback_chain = {"cloud.general", "lan.general", "local.small"},
                    .allow_model_failover = true,
                    .allow_budget_degrade = true},
      TimeoutPolicy{.llm = TimeoutBudget{.timeout_ms = 4000,
                                         .retry_budget = 0U,
                                         .circuit_breaker_threshold = 4U},
                    .tool = TimeoutBudget{.timeout_ms = 1500,
                                          .retry_budget = 1U,
                                          .circuit_breaker_threshold = 3U},
                    .mcp = TimeoutBudget{.timeout_ms = 1500,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 3U},
                    .workflow = TimeoutBudget{.timeout_ms = 3000,
                                              .retry_budget = 1U,
                                              .circuit_breaker_threshold = 3U}},
      ExecutionPolicy{.requires_high_risk_confirmation = true,
                      .safe_mode_enabled = true,
                      .audit_level = "full",
                      .allowed_tool_domains = {"builtin", "mcp"}},
      OpsPolicy{.log_level = "info",
                .metrics_granularity = "full",
                .trace_sample_ratio = 0.25,
                .remote_diagnostics_enabled = true,
                .upgrade_strategy = "rolling"},
      3U,
      false,
  };
}

LLMGenerateRequest make_request() {
  LLMRequest request;
  request.request_id = "req-prod-obsv-001";
  request.llm_call_id = "call-prod-obsv-001";
  request.model_route = "cloud.reasoning";
  request.request_mode = LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"summarize current llm production telemetry"};
  request.created_at = 1712966405000LL;
  request.output_schema_ref = "schema://planner/default";
  request.response_format = "json_object";
  request.max_output_tokens = 128U;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = 4096U,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"integration", "production-observability"};

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
          .requires_tools = false,
          .requires_reasoning = false,
          .prefers_visible_reasoning = false,
          .estimated_input_tokens = 1024U,
          .target_output_tokens = 512U,
          .previous_route_failures = 0U,
      }),
  };
}

void test_production_factory_emits_observability_signals() {
  auto logger = std::make_shared<RecordingLogger>();
  auto meter = std::make_shared<RecordingMeter>();
  auto metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);
  auto tracer = std::make_shared<RecordingTracer>();
  auto tracer_provider = std::make_shared<RecordingTracerProvider>(tracer);
  auto audit_logger = std::make_shared<RecordingAuditLogger>();
  auto transport = std::make_shared<StubTransport>();
  transport->push_response(
      "api.deepseek.com/chat/completions",
      LLMTransportResponse{
          .status_code = 200U,
          .body = R"({
        "id":"chatcmpl-prod-obsv-001",
        "model":"deepseek-chat",
        "choices":[{
          "message":{
            "role":"assistant",
            "content":"{\"telemetry\":\"ok\"}",
            "reasoning_content":"internal reasoning trail"
          },
          "finish_reason":"stop"
        }],
        "usage":{
          "prompt_tokens":64,
          "completion_tokens":24,
          "total_tokens":88
        }
      })",
          .error_message = {},
      });

  const auto factory_result = create_production_llm_manager(
      make_snapshot(),
      LLMProductionFactoryOptions{
          .secret_backend = nullptr,
          .transport = transport,
          .provider_catalog_baseline_root = repo_provider_catalog_root(),
          .logger = logger,
          .metrics_provider = metrics_provider,
          .tracer_provider = tracer_provider,
          .audit_logger = audit_logger,
      });
  assert_true(factory_result.ok(), factory_result.error);

  const auto result = factory_result.manager->generate(make_request());
  assert_true(result.response.has_value(), "production factory request should succeed");
  assert_true(!result.error.has_value(), "production factory request should not fail");
  assert_true(result.response.has_value(), "llm response should be present");
  assert_true(has_result_tag(result, "reasoning_content_stripped=true"),
              "response should expose reasoning strip tag");

  assert_equal(logger->events.size(), static_cast<std::size_t>(1U),
               "structured log should be emitted once");
  const auto* resolved_route = find_log_attr(logger->events.front(), "resolved_route");
  assert_true(resolved_route != nullptr, "structured log should carry resolved_route");

  assert_true(!meter->recorded_samples.empty(), "metrics bridge should record samples");
  assert_equal(metrics_provider->last_scope.name,
               std::string("llm.observability"),
               "metrics scope name should match llm observability scope");
  assert_equal(metrics_provider->last_scope.version,
               std::string("v1"),
               "metrics scope version should match llm observability scope");

  assert_equal(tracer_provider->last_scope.name,
               std::string("llm.observability"),
               "trace scope name should match llm observability scope");
  assert_equal(tracer_provider->last_scope.version,
               std::string("v1"),
               "trace scope version should match llm observability scope");
  assert_equal(tracer->started_spans.size(), static_cast<std::size_t>(3U),
               "trace bridge should record route, adapter and normalize spans");

  assert_equal(audit_logger->events.size(), static_cast<std::size_t>(1U),
               "audit bridge should emit one event when reasoning content is stripped");
  assert_equal(audit_logger->events.front().action,
               std::string("llm.reasoning_content_stripped"),
               "audit action should identify reasoning strip event");
  assert_equal(audit_logger->contexts.front().request_id,
               std::string("req-prod-obsv-001"),
               "audit context should preserve request correlation");
}

}  // namespace

int main() {
  try {
    test_production_factory_emits_observability_signals();
  } catch (const std::exception& ex) {
    std::cerr << "LLMProductionObservabilityIntegrationTest failed: " << ex.what()
              << std::endl;
    return 1;
  }

  return 0;
}